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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "score/mw/launch_manager/alive_monitor/details/ipc/IpcClient.hpp"
#include "score/mw/launch_manager/alive_monitor/details/ipc/MockSocket.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{
namespace
{

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StrictMock;

using Client = IpcClient<TestPayload, 4U, MockSocket>;

class IpcTestBase : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        RecordProperty("TestType", "unit-test");
        auto socket = std::make_unique<StrictMock<MockSocket>>();
        socket_ = socket.get();
        client_ = std::make_unique<Client>(std::move(socket));
    }

    void initializeClient()
    {
        EXPECT_CALL(*socket_, connect(_)).WillOnce(Return(ReturnCode::kOk));
        EXPECT_EQ(client_->init("test"), Client::EIpcInitResult::kOk);
    }

    StrictMock<MockSocket>* socket_{nullptr};
    std::unique_ptr<Client> client_;
};

class IpcClientInitTest : public IpcTestBase
{
};

class IpcBaseTest : public IpcTestBase
{
};

class IpcClientMoveTest : public IpcTestBase
{
};

TEST_F(IpcClientInitTest, ConnectSucceeds_ReturnsOk)
{
    RecordProperty("Description", "init() returns kOk when the socket connects.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*socket_, connect(::testing::StrEq("test"))).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_EQ(client_->init("test"), Client::EIpcInitResult::kOk);
}

TEST_F(IpcClientInitTest, ConnectDenied_ReturnsPermissionDenied)
{
    RecordProperty("Description", "init() maps kPermissionDenied from the socket.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*socket_, connect(_)).WillOnce(Return(ReturnCode::kPermissionDenied));

    EXPECT_EQ(client_->init("test"), Client::EIpcInitResult::kPermissionDenied);
}

TEST_F(IpcClientInitTest, ConnectFails_ReturnsFailed)
{
    RecordProperty("Description", "init() maps any other socket error to kFailed.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*socket_, connect(_)).WillOnce(Return(ReturnCode::kError));

    EXPECT_EQ(client_->init("test"), Client::EIpcInitResult::kFailed);
}

TEST_F(IpcClientInitTest, DefaultConstructor_CreatesSocket)
{
    RecordProperty("Description", "The default constructor argument allocates a socket.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_NO_THROW({ Client defaultClient; });
}

TEST_F(IpcBaseTest, GetPath_ReturnsSocketName)
{
    RecordProperty("Description", "getPath() forwards to the socket name.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*socket_, getName()).WillOnce(Return(std::string_view{"/test"}));

    EXPECT_EQ(client_->getPath(), "/test");
}
////
TEST_F(IpcBaseTest, HasOverflowWhenNotInitialized_ReturnsFalseWithoutQueryingSocket)
{
    RecordProperty("Description", "hasOverflow() is false before init() and does not touch the socket.");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_FALSE(client_->hasOverflow());
}

TEST_F(IpcBaseTest, HasOverflowWhenInitialized_ReturnsSocketFlag)
{
    RecordProperty("Description", "hasOverflow() returns the socket overflow flag once initialized.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    EXPECT_CALL(*socket_, getOverflowFlagMock()).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_TRUE(client_->hasOverflow());
    EXPECT_FALSE(client_->hasOverflow());
}

TEST_F(IpcBaseTest, SendEmplaceWhenNotInitialized_ReturnsFalse)
{
    RecordProperty("Description", "sendEmplace() is rejected before init().");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_FALSE(client_->sendEmplace(1U));
}

TEST_F(IpcBaseTest, SendEmplaceWhenInitialized_ReflectsSocketResult)
{
    RecordProperty("Description", "sendEmplace() returns true only when the socket reports kOk.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    InSequence seq;
    EXPECT_CALL(*socket_, sendEmplaceMock()).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, sendEmplaceMock()).WillOnce(Return(ReturnCode::kError));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_TRUE(client_->sendEmplace(1U));
    EXPECT_FALSE(client_->sendEmplace(2U));
}

TEST_F(IpcBaseTest, PeekWhenNotInitialized_ReturnsNoDataToRead)
{
    RecordProperty("Description", "peek() reports no data before init().");
    RecordProperty("DerivationTechnique", "boundary-values");

    TestPayload* element{nullptr};
    EXPECT_EQ(client_->peek(element), Client::EIpcPeekResult::kNoDataToRead);
}

TEST_F(IpcBaseTest, PeekWhenInitialized_MapsSocketResults)
{
    RecordProperty("Description", "peek() maps kOk, kQueueEmpty and any other result to the matching enum value.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    InSequence seq;
    EXPECT_CALL(*socket_, tryPeek(_)).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, tryPeek(_)).WillOnce(Return(ReturnCode::kQueueEmpty));
    EXPECT_CALL(*socket_, tryPeek(_)).WillOnce(Return(ReturnCode::kError));
    EXPECT_CALL(*socket_, close()).Times(1);

    TestPayload* element{nullptr};
    EXPECT_EQ(client_->peek(element), Client::EIpcPeekResult::kOk);
    EXPECT_EQ(client_->peek(element), Client::EIpcPeekResult::kNoDataToRead);
    EXPECT_EQ(client_->peek(element), Client::EIpcPeekResult::kIpcError);
}

TEST_F(IpcBaseTest, PopWhenNotInitialized_ReturnsFalse)
{
    RecordProperty("Description", "pop() is rejected before init().");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_FALSE(client_->pop());
}

TEST_F(IpcBaseTest, PopWhenInitialized_ReflectsSocketResult)
{
    RecordProperty("Description", "pop() returns true only when the socket reports kOk.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    InSequence seq;
    EXPECT_CALL(*socket_, tryPop()).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, tryPop()).WillOnce(Return(ReturnCode::kError));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_TRUE(client_->pop());
    EXPECT_FALSE(client_->pop());
}

TEST_F(IpcBaseTest, DestructorWhenNotInitialized_DoesNotCloseSocket)
{
    RecordProperty("Description", "An uninitialized channel does not close the socket on destruction.");
    RecordProperty("DerivationTechnique", "boundary-values");

    client_.reset();
}

TEST_F(IpcBaseTest, DestructorWhenInitialized_ClosesSocket)
{
    RecordProperty("Description", "An initialized channel closes the socket on destruction.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    EXPECT_CALL(*socket_, close()).Times(1);

    client_.reset();
}

TEST_F(IpcClientMoveTest, MoveConstructedClient_TakesOverSocketAndMovedFromDestructsSafely)
{
    RecordProperty("Description", "A moved-from client owns no socket and its destruction does nothing.");
    RecordProperty("DerivationTechnique", "error-guessing");

    initializeClient();
    EXPECT_CALL(*socket_, close()).Times(1);

    {
        Client moved{std::move(*client_)};
    }
    client_.reset();
}

TEST_F(IpcClientMoveTest, MoveAssignedClient_TakesOverSocket)
{
    RecordProperty("Description", "Move assignment transfers the socket and the initialized state.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeClient();
    auto otherSocket = std::make_unique<StrictMock<MockSocket>>();
    Client other{std::move(otherSocket)};
    EXPECT_CALL(*socket_, tryPop()).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, close()).Times(1);

    other = std::move(*client_);

    EXPECT_TRUE(other.pop());
}

}  // namespace
}  // namespace score::mw::lifecycle::internal::saf::ipc
