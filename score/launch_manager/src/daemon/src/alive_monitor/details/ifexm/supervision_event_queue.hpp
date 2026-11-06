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
#ifndef SUPERVISION_EVENT_QUEUE_HPP
#define SUPERVISION_EVENT_QUEUE_HPP

#include "score/mw/launch_manager/alive_monitor/details/ifexm/supervision_event.hpp"
#include "score/mw/launch_manager/common/concurrency/mpsc_bounded_queue.hpp"
#include <atomic>
#include <cstddef>

namespace score::mw::lifecycle
{

namespace BufferConstants
{

/// @brief Supervision event queue size
constexpr std::size_t BUFFER_QUEUE_SIZE = 4096UL;

}  // namespace BufferConstants

/// @brief Multi-producer, single-consumer queue of supervision events with a sticky overflow flag.
/// @details Drops the newest event when full. Only one thread may call tryDequeue().
class SupervisionEventQueue
{
  public:
    explicit SupervisionEventQueue(std::size_t capacity = BufferConstants::BUFFER_QUEUE_SIZE) : queue_(capacity)
    {
    }

    // The queue asserts it is stopped on destruction; no producer may be active at this point.
    ~SupervisionEventQueue()
    {
        queue_.stop();
    }

    SupervisionEventQueue(const SupervisionEventQueue&) = delete;
    SupervisionEventQueue& operator=(const SupervisionEventQueue&) = delete;
    SupervisionEventQueue(SupervisionEventQueue&&) = delete;
    SupervisionEventQueue& operator=(SupervisionEventQueue&&) = delete;

    bool tryEnqueue(const SupervisionEvent& event)
    {
        const auto result = queue_.push(event);

        if (result.has_value())
        {
            return true;
        }

        if (result.error() == internal::ConcurrencyErrc::kOverflow)
        {
            overflow_.store(true, std::memory_order_relaxed);
        }

        return false;
    }

    bool tryDequeue(SupervisionEvent& event)
    {
        auto item = queue_.tryPop();
        if (!item.has_value())
        {
            return false;
        }
        event = *item;
        return true;
    }

    bool getOverflowFlag(bool reset = false)
    {
        return reset ? overflow_.exchange(false, std::memory_order_relaxed) : overflow_.load(std::memory_order_relaxed);
    }

  private:
    score::mw::lifecycle::internal::MpscBoundedQueue<SupervisionEvent> queue_;
    std::atomic<bool> overflow_{false};
};

using SupervisionBufferType = SupervisionEventQueue;

}  // namespace score::mw::lifecycle

#endif  // SUPERVISION_EVENT_QUEUE_HPP
