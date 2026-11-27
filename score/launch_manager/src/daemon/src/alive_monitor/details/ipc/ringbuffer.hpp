/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#ifndef IPC_DROPIN_RINGBUFFER_HPP_
#define IPC_DROPIN_RINGBUFFER_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace score::mw::lifecycle::internal::saf::ipc
{

/// @brief Lock-free single-producer / single-consumer ring buffer for shared memory.
///
/// SPSC contract: exactly ONE producer thread (tryEnqueue/tryEmplace) and exactly ONE consumer thread
/// (tryDequeue/tryPeek/tryPop) may use the buffer at any time; they may live in different processes. A second
/// producer or consumer violates the single-writer ownership of write_/read_ and can lose index updates, collide on
/// slots and overwrite unread elements.
///
/// empty()/full() are transient observations only; every queue operation validates the state itself.
/// The pointer returned by tryPeek() is valid only for the single consumer and only until the corresponding
/// successful tryPop() advances read_.
///
/// write_ and read_ are free-running 32-bit counters; used = uint32_t(write_ - read_) is in [0, Capacity] for valid
/// operation, also across counter rollover. used > Capacity is a corrupted state: no slot is accessed, the overflow
/// flag is not set and the operation fails.
///
/// Capacity must be a power of two. The creator must construct the object in the mapped storage (placement new)
/// and then call initialize(); a connector must never reconstruct or reset it.
template <std::size_t Capacity, std::size_t ElementSize>
class RingBuffer
{
    static_assert(
        Capacity > 0U && (Capacity & (Capacity - 1U)) == 0U && Capacity <= (1ULL << 31U),
        "Capacity must be a power of two and <= 2^31");
    static_assert(ElementSize > 0, "ElementSize must be > 0");
    static_assert(
        std::atomic<std::uint32_t>::is_always_lock_free,
        "std::atomic<std::uint32_t> must be lock-free for use in shared memory");

  public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /// @brief Resets the counters and flags and publishes the buffer as initialized
    /// @details Creator side only, called once after placement new. Does nothing if the buffer is already initialized.
    void initialize() noexcept
    {
        static_assert(std::is_standard_layout<RingBuffer>::value, "RingBuffer must have a fixed layout");
        static_assert(std::is_trivially_destructible<RingBuffer>::value, "RingBuffer must own no resources");
        if (initialized_.load(std::memory_order_acquire) != 0U)
        {
            return;
        }
        write_.store(0U, std::memory_order_relaxed);
        read_.store(0U, std::memory_order_relaxed);
        overflow_flag_.store(0U, std::memory_order_relaxed);
        initialized_.store(1U, std::memory_order_release);
    }

    /// @brief Checks whether the creator has finished initialize()
    /// @return true if the buffer is initialized
    bool isInitialized() const noexcept
    {
        return initialized_.load(std::memory_order_acquire) != 0U;
    }

    /// @brief Copies a value into the next free slot (producer only)
    /// @details Never blocks. If the buffer is full, the newest value is rejected and the overflow flag is set.
    /// @tparam T Trivially copyable payload type, sizeof(T) <= ElementSize
    /// @param [in] value The value to enqueue
    /// @return true if the value was enqueued, false if the buffer is full or its state is corrupted
    template <typename T>
    bool tryEnqueue(const T& value)
    {
        static_assert(std::is_trivially_copyable<T>::value, "RingBuffer only supports trivially copyable types");
        static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");
        static_assert(
            sizeof(T) <= ElementSize,
            "RingBuffer payload size mismatch: sizeof(T) must be <= ElementSize template parameter");
        std::uint32_t w{};
        if (!acquireWriteSlot(w))
        {
            return false;
        }
        Node& n = nodes_[w & kMask];
        std::memcpy(n.storage, std::addressof(value), sizeof(T));
        n.size_ = sizeof(T);
        write_.store(w + 1U, std::memory_order_release);
        return true;
    }

    /// @brief Constructs a value in place in the next free slot (producer only)
    /// @details Same full and corruption behavior as tryEnqueue().
    /// @tparam T Trivially copyable, nothrow constructible payload type, sizeof(T) <= ElementSize
    /// @param [in] args Constructor arguments of T
    /// @return true if the value was constructed, false if the buffer is full or its state is corrupted
    template <typename T, typename... Args>
    bool tryEmplace(Args&&... args)
    {
        static_assert(
            std::is_nothrow_constructible<T, Args&&...>::value, "T must be nothrow constructible with Args&&...");
        static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
        static_assert(std::is_trivially_destructible<T>::value, "T must be trivially destructible");
        static_assert(
            sizeof(T) <= ElementSize,
            "RingBuffer payload size mismatch: sizeof(T) must be <= ElementSize template parameter");
        std::uint32_t w{};
        if (!acquireWriteSlot(w))
        {
            return false;
        }
        Node& n = nodes_[w & kMask];
        new (n.storage) T(std::forward<Args>(args)...);
        n.size_ = sizeof(T);
        write_.store(w + 1U, std::memory_order_release);
        return true;
    }

    /// @brief Copies the oldest value out and removes it (consumer only)
    /// @tparam T Trivially copyable payload type, must match the type that was enqueued
    /// @param [out] out Receives the oldest value
    /// @return true if a value was returned, false if the buffer is empty, corrupted or holds a value of another size
    template <typename T>
    bool tryDequeue(T& out)
    {
        static_assert(std::is_trivially_copyable<T>::value, "RingBuffer only supports trivially copyable types");
        static_assert(
            sizeof(T) <= ElementSize,
            "RingBuffer payload size mismatch: sizeof(T) must be <= ElementSize template parameter");
        std::uint32_t r{};
        if (!acquireReadSlot(r))
        {
            return false;
        }
        Node& n = nodes_[r & kMask];
        if (n.size_ != sizeof(T))
        {
            return false;
        }
        // codeql[c/cert/library-function-argument-out-of-bounds] False positive: CodeQL sizes &out as its first member only (e.g. the 8-byte IdentifierHash of SupervisionEvent), not as a whole T. The source holds sizeof(T) by the static_assert on ElementSize and the n.size_ check.
        std::memcpy(&out, n.storage, sizeof(T));
        // The slot must not be accessed after this store: it hands the slot back to the producer.
        read_.store(r + 1U, std::memory_order_release);
        return true;
    }

    /// @brief Removes the oldest value without reading it (consumer only)
    /// @return true if a value was removed, false if the buffer is empty or corrupted
    bool tryPop()
    {
        std::uint32_t r{};
        if (!acquireReadSlot(r))
        {
            return false;
        }
        read_.store(r + 1U, std::memory_order_release);
        return true;
    }

    /// @brief Gives access to the oldest value without removing it (consumer only)
    /// @details The pointer is valid only until the next successful tryPop().
    /// @tparam T Trivially copyable payload type, must match the type that was enqueued
    /// @param [out] out Points to the oldest value in the buffer
    /// @return true if a value is available, false if the buffer is empty, corrupted or holds a value of another size
    template <typename T>
    bool tryPeek(T*& out)
    {
        static_assert(std::is_trivially_copyable<T>::value, "RingBuffer only supports trivially copyable types");
        static_assert(
            sizeof(T) <= ElementSize,
            "RingBuffer payload size mismatch: sizeof(T) must be <= ElementSize template parameter");
        std::uint32_t r{};
        if (!acquireReadSlot(r))
        {
            return false;
        }
        Node& n = nodes_[r & kMask];
        if (n.size_ != sizeof(T))
        {
            return false;
        }
        out = reinterpret_cast<T*>(n.storage);
        return true;
    }

    /// @brief Reports whether an enqueue was ever rejected because the buffer was full
    /// @param [in] reset If true, the flag is cleared after reading
    /// @return true if the overflow flag was set
    bool getOverflowFlag(bool reset = false)
    {
        const std::uint32_t v = reset ? overflow_flag_.exchange(0U, std::memory_order_relaxed)
                                      : overflow_flag_.load(std::memory_order_relaxed);
        return v != 0U;
    }

    /// @brief Checks whether the buffer holds no values
    /// @details Transient observation only; the other side may change the state immediately afterwards.
    /// @return true if the buffer is empty
    bool empty()
    {
        return static_cast<std::uint32_t>(
                   write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed)) == 0U;
    }

    /// @brief Checks whether the buffer holds Capacity values
    /// @details Transient observation only; a corrupted state (used > Capacity) is also reported as full.
    /// @return true if the buffer is full
    bool full()
    {
        return static_cast<std::uint32_t>(
                   write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire)) >= Capacity;
    }

    /// @brief Overwrites the read/write counters.
    /// @note For testing purposes only (counter rollover, corrupted state); must not be used in production code.
    void setCountersForTesting(std::uint32_t read, std::uint32_t write) noexcept
    {
        read_.store(read, std::memory_order_relaxed);
        write_.store(write, std::memory_order_relaxed);
    }

  private:
    static constexpr std::uint32_t kMask = static_cast<std::uint32_t>(Capacity - 1U);
    static constexpr std::uint32_t kCapacity = static_cast<std::uint32_t>(Capacity);

    /// @brief Producer side: checks that a free slot exists
    /// @param [out] w On success, the counter value of the slot to write
    /// @return true if a slot is free, false if the buffer is full (overflow flag set) or corrupted (no flag)
    bool acquireWriteSlot(std::uint32_t& w) noexcept
    {
        w = write_.load(std::memory_order_relaxed);
        std::uint32_t used = w - read_.load(std::memory_order_acquire);
        if (used >= kCapacity)
        {
            if (used > kCapacity)
            {
                return false;  // corrupted: no slot access, no overflow flag
            }
            // Re-load once: the first read_ may be stale after the consumer released a slot.
            used = w - read_.load(std::memory_order_acquire);
            if (used > kCapacity)
            {
                return false;
            }
            if (used == kCapacity)
            {
                overflow_flag_.store(1U, std::memory_order_relaxed);
                return false;
            }
        }
        return true;
    }

    /// @brief Consumer side: checks that a value is available
    /// @param [out] r On success, the counter value of the slot to read
    /// @return true if a value is available, false if the buffer is empty or corrupted
    bool acquireReadSlot(std::uint32_t& r) noexcept
    {
        r = read_.load(std::memory_order_relaxed);
        const std::uint32_t used = write_.load(std::memory_order_acquire) - r;
        return (used != 0U) && (used <= kCapacity);  // used > Capacity is corrupted: fail without slot access
    }

    struct Node
    {
        alignas(std::max_align_t) unsigned char storage[ElementSize];
        std::size_t size_;
    };

    alignas(64) std::atomic<std::uint32_t> write_{0U};          // producer-owned
    alignas(64) std::atomic<std::uint32_t> read_{0U};           // consumer-owned
    alignas(64) std::atomic<std::uint32_t> overflow_flag_{0U};  // 0U / 1U only
    std::atomic<std::uint32_t> initialized_{0U};                // 0U / 1U only
    Node nodes_[Capacity] = {};
};

}  // namespace score::mw::lifecycle::internal::saf::ipc

#endif
