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

#include "score/mw/launch_manager/alive_monitor/details/ipc/ringbuffer.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <unistd.h>

#include "score/mw/launch_manager/alive_monitor/details/ipc/socket.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{

namespace
{

struct Item
{
    std::uint64_t seq;
    std::uint64_t inverse;
};

constexpr std::size_t kCap = 8U;
using Rb = RingBuffer<kCap, sizeof(Item)>;

class RingBufferTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        rb_ = std::make_unique<Rb>();
        rb_->initialize();
    }
    std::unique_ptr<Rb> rb_;
};

TEST_F(RingBufferTest, HandshakeFlagSetByInitialize)
{
    RecordProperty(
        "Description", "A new buffer reports not initialized until initialize() is called, then reports initialized.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    Rb fresh;
    EXPECT_FALSE(fresh.isInitialized());
    fresh.initialize();
    EXPECT_TRUE(fresh.isInitialized());
}

TEST_F(RingBufferTest, EmptyBufferRejectsReadOperations)
{
    RecordProperty("Description", "On an empty buffer tryDequeue, tryPeek and tryPop all fail.");
    RecordProperty("DerivationTechnique", "boundary-values");
    Item out{};
    Item* p{};
    EXPECT_TRUE(rb_->empty());
    EXPECT_FALSE(rb_->tryDequeue(out));
    EXPECT_FALSE(rb_->tryPeek(p));
    EXPECT_FALSE(rb_->tryPop());
}

TEST_F(RingBufferTest, EnqueueDequeueKeepsFifoOrderAcrossWrap)
{
    RecordProperty(
        "Description",
        "Elements are dequeued in the order they were enqueued, also when the slot index wraps around the capacity.");
    RecordProperty("DerivationTechnique", "boundary-values");
    std::uint64_t next_in = 0U;
    std::uint64_t next_out = 0U;
    for (int round = 0; round < 5; ++round)
    {
        for (std::size_t i = 0; i < 5U; ++i)
        {
            const Item it{next_in, ~next_in};
            ++next_in;
            ASSERT_TRUE(rb_->tryEnqueue(it));
        }
        for (std::size_t i = 0; i < 5U; ++i)
        {
            Item out{};
            ASSERT_TRUE(rb_->tryDequeue(out));
            EXPECT_EQ(out.seq, next_out);
            EXPECT_EQ(out.inverse, ~next_out);
            ++next_out;
        }
    }
    EXPECT_TRUE(rb_->empty());
}

TEST_F(RingBufferTest, EmplaceAndPeekPop)
{
    RecordProperty(
        "Description", "An element constructed in place can be peeked and then popped, leaving the buffer empty.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    ASSERT_TRUE(rb_->tryEmplace<Item>(Item{7U, ~std::uint64_t{7U}}));
    Item* p{};
    ASSERT_TRUE(rb_->tryPeek(p));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->seq, 7U);
    EXPECT_TRUE(rb_->tryPop());
    EXPECT_TRUE(rb_->empty());
}

TEST_F(RingBufferTest, FullRejectsNewestKeepsOldestAndSetsOverflow)
{
    RecordProperty(
        "Description",
        "Enqueue on a full buffer fails, keeps the oldest element and sets the overflow flag, which stays set until it "
        "is reset.");
    RecordProperty("DerivationTechnique", "boundary-values");
    for (std::uint64_t i = 0; i < kCap; ++i)
    {
        ASSERT_TRUE(rb_->tryEnqueue(Item{i, ~i}));
    }
    EXPECT_TRUE(rb_->full());
    EXPECT_FALSE(rb_->getOverflowFlag());
    EXPECT_FALSE(rb_->tryEnqueue(Item{99U, ~std::uint64_t{99U}}));
    EXPECT_TRUE(rb_->getOverflowFlag());
    EXPECT_TRUE(rb_->getOverflowFlag());  // sticky without reset
    Item out{};
    ASSERT_TRUE(rb_->tryDequeue(out));
    EXPECT_EQ(out.seq, 0U);
    EXPECT_TRUE(rb_->getOverflowFlag(true));
    EXPECT_FALSE(rb_->getOverflowFlag());
}

TEST_F(RingBufferTest, CorruptedStateFailsWithoutSlotAccessOrOverflowFlag)
{
    RecordProperty("Description", "With inconsistent counters (used > capacity) all operations fail without slot access and without setting the overflow flag.");
    RecordProperty("DerivationTechnique", "error-condition-injection");
    rb_->setCountersForTesting(0U, static_cast<std::uint32_t>(kCap) + 1U);
    Item out{};
    Item* p{};
    EXPECT_FALSE(rb_->tryEnqueue(Item{1U, ~std::uint64_t{1U}}));
    EXPECT_FALSE(rb_->tryEmplace<Item>(Item{1U, ~std::uint64_t{1U}}));
    EXPECT_FALSE(rb_->tryDequeue(out));
    EXPECT_FALSE(rb_->tryPeek(p));
    EXPECT_FALSE(rb_->tryPop());
    EXPECT_FALSE(rb_->getOverflowFlag());
}

TEST_F(RingBufferTest, CountersRollOverUint32Max)
{
    RecordProperty("Description", "Empty and full detection and FIFO order stay correct when the 32-bit counters wrap past UINT32_MAX.");
    RecordProperty("DerivationTechnique", "boundary-values");
    const std::uint32_t start = UINT32_MAX - 2U;
    rb_->setCountersForTesting(start, start);
    for (std::uint64_t i = 0; i < 20U; ++i)
    {
        ASSERT_TRUE(rb_->tryEnqueue(Item{i, ~i}));
        ASSERT_TRUE(rb_->tryEnqueue(Item{i + 100U, ~(i + 100U)}));
        Item a{};
        Item b{};
        ASSERT_TRUE(rb_->tryDequeue(a));
        ASSERT_TRUE(rb_->tryDequeue(b));
        EXPECT_EQ(a.seq, i);
        EXPECT_EQ(b.seq, i + 100U);
    }
    EXPECT_TRUE(rb_->empty());
    // Full detection across the rollover.
    rb_->setCountersForTesting(UINT32_MAX - 3U, UINT32_MAX - 3U);
    for (std::uint64_t i = 0; i < kCap; ++i)
    {
        ASSERT_TRUE(rb_->tryEnqueue(Item{i, ~i}));
    }
    EXPECT_TRUE(rb_->full());
    EXPECT_FALSE(rb_->tryEnqueue(Item{0U, ~std::uint64_t{0U}}));
    EXPECT_TRUE(rb_->getOverflowFlag());
}

TEST_F(RingBufferTest, TypeMismatchIsRejected)
{
    RecordProperty("Description", "Dequeue with a type whose size differs from the stored element fails.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    ASSERT_TRUE(rb_->tryEnqueue(Item{1U, ~std::uint64_t{1U}}));
    std::uint32_t small{};
    EXPECT_FALSE(rb_->tryDequeue(small));
    std::uint32_t* peeked{};
    EXPECT_FALSE(rb_->tryPeek(peeked));
    EXPECT_EQ(peeked, nullptr);
    Item out{};
    EXPECT_TRUE(rb_->tryDequeue(out));
}

TEST_F(RingBufferTest, RepeatedInitializeKeepsExistingContent)
{
    RecordProperty("Description", "A second initialize() call is a no-op and does not discard queued elements.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    ASSERT_TRUE(rb_->tryEnqueue(Item{3U, ~std::uint64_t{3U}}));
    rb_->initialize();
    EXPECT_TRUE(rb_->isInitialized());
    Item out{};
    ASSERT_TRUE(rb_->tryDequeue(out));
    EXPECT_EQ(out.seq, 3U);
}

TEST(RingBufferStress, OneProducerOneConsumerOneMillionTransfers)
{
    RecordProperty(
        "Description",
        "One producer and one consumer thread transfer one million elements without loss, duplication or reordering.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    auto rb = std::make_unique<Rb>();
    rb->initialize();
    constexpr std::uint64_t kTransfers = 1000000U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kTransfers && !failed.load(); ++i)
        {
            while (!rb->tryEnqueue(Item{i, ~i}))
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    failed = true;
                    return;
                }
                std::this_thread::yield();
            }
        }
    });
    std::thread consumer([&] {
        for (std::uint64_t i = 0; i < kTransfers; ++i)
        {
            Item out{};
            while (!rb->tryDequeue(out))
            {
                if (std::chrono::steady_clock::now() > deadline)
                {
                    failed = true;
                    return;
                }
                std::this_thread::yield();
            }
            if (out.seq != i || out.inverse != ~i)
            {
                failed = true;
                return;
            }
        }
    });
    producer.join();
    consumer.join();
    EXPECT_FALSE(failed.load());
}

TEST(RingBufferSocket, CreateConnectTransfersAcrossMappings)
{
    RecordProperty(
        "Description",
        "An element sent through a connected socket is received through the creating socket, then the queue reports "
        "empty.");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    const std::string name = "/rb_ut_" + std::to_string(::getpid());
    Socket<sizeof(Item), kCap> server;
    Socket<sizeof(Item), kCap> client;
    ASSERT_EQ(server.create(name.c_str(), 0600), ReturnCode::kOk);
    ASSERT_EQ(client.connect(name.c_str()), ReturnCode::kOk);
    Item in{5U, ~std::uint64_t{5U}};
    EXPECT_EQ(client.trySend(in), ReturnCode::kOk);
    Item out{};
    EXPECT_EQ(server.tryReceive(out), ReturnCode::kOk);
    EXPECT_EQ(out.seq, 5U);
    EXPECT_EQ(server.tryReceive(out), ReturnCode::kQueueEmpty);
    client.close();
    server.close();
}

}  // namespace
}  // namespace score::mw::lifecycle::internal::saf::ipc
