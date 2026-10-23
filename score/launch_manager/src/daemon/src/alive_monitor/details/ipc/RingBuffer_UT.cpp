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

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "score/mw/launch_manager/alive_monitor/details/ipc/ringbuffer.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{
namespace
{

constexpr std::size_t kCapacity = 4U;
constexpr std::size_t kElementSize = 8U;
using Buffer = RingBuffer<kCapacity, kElementSize>;

class RingBufferTestBase : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        RecordProperty("TestType", "unit-test");
        buffer_ = std::make_unique<Buffer>();
        buffer_->initialize();
    }

    void fill()
    {
        for (std::uint32_t i = 0U; i < kCapacity; ++i)
        {
            EXPECT_TRUE(buffer_->tryEnqueue(i));
        }
    }

    std::unique_ptr<Buffer> buffer_;
};

class RingBufferInitializeTest : public RingBufferTestBase
{
};

class RingBufferEnqueueTest : public RingBufferTestBase
{
};

class RingBufferEmplaceTest : public RingBufferTestBase
{
};

class RingBufferDequeueTest : public RingBufferTestBase
{
};

class RingBufferPeekPopTest : public RingBufferTestBase
{
};

TEST_F(RingBufferInitializeTest, NewBuffer_IsNotInitializedUntilInitializeCalled)
{
    RecordProperty("Description", "A fresh buffer reports not initialized, and initialize() flips the state.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    Buffer fresh;
    EXPECT_FALSE(fresh.isInitialized());
    fresh.initialize();
    EXPECT_TRUE(fresh.isInitialized());
}

TEST_F(RingBufferInitializeTest, InitializeTwice_DoesNotResetContent)
{
    RecordProperty("Description", "A second initialize() returns early and keeps the queued elements.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_TRUE(buffer_->tryEnqueue(std::uint32_t{7U}));
    buffer_->initialize();

    EXPECT_FALSE(buffer_->empty());
}

TEST_F(RingBufferEnqueueTest, EnqueueUntilFull_SucceedsThenSetsOverflowAndFails)
{
    RecordProperty("Description", "Enqueue on a full buffer fails and sets the overflow flag.");
    RecordProperty("DerivationTechnique", "boundary-values");

    fill();

    EXPECT_TRUE(buffer_->full());
    EXPECT_FALSE(buffer_->getOverflowFlag());
    EXPECT_FALSE(buffer_->tryEnqueue(std::uint32_t{99U}));
    EXPECT_TRUE(buffer_->getOverflowFlag());
}

TEST_F(RingBufferEnqueueTest, OverflowFlag_ResetClearsFlag)
{
    RecordProperty("Description", "getOverflowFlag(true) returns the flag and clears it.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    fill();
    EXPECT_FALSE(buffer_->tryEnqueue(std::uint32_t{1U}));

    EXPECT_TRUE(buffer_->getOverflowFlag(true));
    EXPECT_FALSE(buffer_->getOverflowFlag());
}

TEST_F(RingBufferEmplaceTest, EmplaceUntilFull_SucceedsThenSetsOverflowAndFails)
{
    RecordProperty("Description", "Emplace on a full buffer fails and sets the overflow flag.");
    RecordProperty("DerivationTechnique", "boundary-values");

    for (std::uint32_t i = 0U; i < kCapacity; ++i)
    {
        EXPECT_TRUE(buffer_->tryEmplace<std::uint32_t>(i));
    }

    EXPECT_FALSE(buffer_->tryEmplace<std::uint32_t>(std::uint32_t{5U}));
    EXPECT_TRUE(buffer_->getOverflowFlag());
}

TEST_F(RingBufferDequeueTest, DequeueOnEmpty_ReturnsFalse)
{
    RecordProperty("Description", "Dequeue on an empty buffer fails.");
    RecordProperty("DerivationTechnique", "boundary-values");

    std::uint32_t out{0U};
    EXPECT_FALSE(buffer_->tryDequeue(out));
}

TEST_F(RingBufferDequeueTest, DequeueAfterWrapAround_ReturnsElementsInFifoOrder)
{
    RecordProperty("Description", "Elements are returned in FIFO order after head indices wrap around.");
    RecordProperty("DerivationTechnique", "boundary-values");

    fill();
    std::uint32_t out{0U};
    EXPECT_TRUE(buffer_->tryDequeue(out));
    EXPECT_TRUE(buffer_->tryDequeue(out));
    EXPECT_TRUE(buffer_->tryEnqueue(std::uint32_t{10U}));
    EXPECT_TRUE(buffer_->tryEmplace<std::uint32_t>(std::uint32_t{11U}));

    for (const std::uint32_t expected : {2U, 3U, 10U, 11U})
    {
        EXPECT_TRUE(buffer_->tryDequeue(out));
        EXPECT_EQ(out, expected);
    }
    EXPECT_TRUE(buffer_->empty());
}

TEST_F(RingBufferDequeueTest, DequeueWithDifferentTypeSize_ReturnsFalseAndKeepsElement)
{
    RecordProperty("Description", "Dequeue with a type whose size differs from the stored element fails.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_TRUE(buffer_->tryEnqueue(std::uint8_t{1U}));

    std::uint32_t out{0U};
    EXPECT_FALSE(buffer_->tryDequeue(out));
    EXPECT_FALSE(buffer_->empty());
}

TEST_F(RingBufferPeekPopTest, PeekOnEmpty_ReturnsFalse)
{
    RecordProperty("Description", "Peek on an empty buffer fails.");
    RecordProperty("DerivationTechnique", "boundary-values");

    std::uint32_t* out{nullptr};
    EXPECT_FALSE(buffer_->tryPeek(out));
    EXPECT_EQ(out, nullptr);
}

TEST_F(RingBufferPeekPopTest, PeekWithMatchingType_ReturnsPointerWithoutRemoving)
{
    RecordProperty("Description", "Peek exposes the head element and leaves it in the queue.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_TRUE(buffer_->tryEnqueue(std::uint32_t{42U}));

    std::uint32_t* out{nullptr};
    ASSERT_TRUE(buffer_->tryPeek(out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(*out, 42U);
    EXPECT_FALSE(buffer_->empty());
}

TEST_F(RingBufferPeekPopTest, PeekWithDifferentTypeSize_ReturnsFalse)
{
    RecordProperty("Description", "Peek with a type whose size differs from the stored element fails.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_TRUE(buffer_->tryEnqueue(std::uint8_t{1U}));

    std::uint32_t* out{nullptr};
    EXPECT_FALSE(buffer_->tryPeek(out));
}

TEST_F(RingBufferPeekPopTest, PopOnEmpty_ReturnsFalse)
{
    RecordProperty("Description", "Pop on an empty buffer fails.");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_FALSE(buffer_->tryPop());
}

TEST_F(RingBufferPeekPopTest, PopWithElement_RemovesIt)
{
    RecordProperty("Description", "Pop removes the head element.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_TRUE(buffer_->tryEnqueue(std::uint32_t{1U}));

    EXPECT_TRUE(buffer_->tryPop());
    EXPECT_TRUE(buffer_->empty());
}

}  // namespace
}  // namespace score::mw::lifecycle::internal::saf::ipc
