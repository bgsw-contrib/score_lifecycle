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
    Rb fresh;
    EXPECT_FALSE(fresh.isInitialized());
    fresh.initialize();
    EXPECT_TRUE(fresh.isInitialized());
}

TEST_F(RingBufferTest, EmptyBufferRejectsReadOperations)
{
    Item out{};
    Item* p{};
    EXPECT_TRUE(rb_->empty());
    EXPECT_FALSE(rb_->tryDequeue(out));
    EXPECT_FALSE(rb_->tryPeek(p));
    EXPECT_FALSE(rb_->tryPop());
}

TEST_F(RingBufferTest, EnqueueDequeueKeepsFifoOrderAcrossWrap)
{
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
    ASSERT_TRUE(rb_->tryEnqueue(Item{1U, ~std::uint64_t{1U}}));
    std::uint32_t small{};
    EXPECT_FALSE(rb_->tryDequeue(small));
}

TEST(RingBufferStress, OneProducerOneConsumerOneMillionTransfers)
{
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
