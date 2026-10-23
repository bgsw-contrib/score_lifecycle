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
#include <string>
#include <utility>

#include "tests/utils/mocks/mock_acl.hpp"
#include "tests/utils/mocks/mock_syscalls.hpp"

#include "score/mw/launch_manager/alive_monitor/details/ipc/IpcServer.hpp"
#include "score/mw/launch_manager/alive_monitor/details/ipc/MockSocket.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{
namespace
{

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;

using Server = IpcServer<TestPayload, 4U, MockSocket>;

constexpr int kFd = 7;
// Number of per-entry ACL calls made by setAccessRights() when everything succeeds.
constexpr int kTotalAclEntryCalls = 22;
int g_fake_acl{0};

acl_t fakeAcl()
{
    return reinterpret_cast<acl_t>(&g_fake_acl);
}

class IpcServerTestBase : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        RecordProperty("TestType", "unit-test");
        g_syscall_mock = std::unique_ptr<SyscallMock>(new NiceMock<SyscallMock>());
        g_acl_mock = std::unique_ptr<AclMock>(new NiceMock<AclMock>());
        installAclEntryDefaults();

        auto socket = std::make_unique<StrictMock<MockSocket>>();
        socket_ = socket.get();
        server_ = std::make_unique<Server>(std::move(socket));
    }

    void TearDown() override
    {
        server_.reset();
        g_acl_mock.reset();
        g_syscall_mock.reset();
    }

    void initializeServer()
    {
        EXPECT_CALL(*socket_, create(_, _)).WillOnce(Return(ReturnCode::kOk));
        EXPECT_EQ(server_->init("test"), Server::EIpcInitResult::kOk);
    }

    // Per-entry ACL calls succeed, except the aclFailAt_-th one (1-based) which fails; 0 disables injection.
    void installAclEntryDefaults()
    {
        auto step = [this](auto&&...) {
            ++aclEntryCalls_;
            return (aclEntryCalls_ == aclFailAt_) ? -1 : 0;
        };
        ON_CALL(*g_acl_mock, acl_create_entry(_, _)).WillByDefault(step);
        ON_CALL(*g_acl_mock, acl_set_tag_type(_, _)).WillByDefault(step);
        ON_CALL(*g_acl_mock, acl_set_qualifier(_, _)).WillByDefault(step);
        ON_CALL(*g_acl_mock, acl_get_permset(_, _)).WillByDefault(step);
        ON_CALL(*g_acl_mock, acl_add_perm(_, _)).WillByDefault(step);
    }

    int aclFailAt_{0};
    int aclEntryCalls_{0};
    StrictMock<MockSocket>* socket_{nullptr};
    std::unique_ptr<Server> server_;
};

class IpcServerInitTest : public IpcServerTestBase
{
};

class IpcServerAccessRightsTest : public IpcServerTestBase
{
  protected:
    void SetUp() override
    {
        IpcServerTestBase::SetUp();
        EXPECT_CALL(*socket_, getName()).WillRepeatedly(Return(std::string_view{"/test"}));
    }
};

class IpcServerAccessRightsEntryFailureTest : public IpcServerAccessRightsTest,
                                              public ::testing::WithParamInterface<int>
{
};

class IpcServerMoveTest : public IpcServerTestBase
{
};

TEST_F(IpcServerInitTest, CreateSucceeds_UsesDefaultOwnerReadWriteMode)
{
    RecordProperty("Description", "init() creates the channel with mode 0600 by default.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*socket_, create(StrEq("test"), static_cast<mode_t>(0600))).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_EQ(server_->init("test"), Server::EIpcInitResult::kOk);
}

TEST_F(IpcServerInitTest, CreateSucceedsWithCustomMode_ForwardsMode)
{
    RecordProperty("Description", "init() forwards an explicitly given permission mode.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*socket_, create(_, static_cast<mode_t>(0660))).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, close()).Times(1);

    EXPECT_EQ(server_->init("test", 0660), Server::EIpcInitResult::kOk);
}

TEST_F(IpcServerInitTest, CreateDenied_ReturnsPermissionDenied)
{
    RecordProperty("Description", "init() maps kPermissionDenied and stays uninitialized.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*socket_, create(_, _)).WillOnce(Return(ReturnCode::kPermissionDenied));

    EXPECT_EQ(server_->init("test"), Server::EIpcInitResult::kPermissionDenied);
    EXPECT_FALSE(server_->pop());
}

TEST_F(IpcServerInitTest, CreateFails_ReturnsFailed)
{
    RecordProperty("Description", "init() maps any other socket error to kFailed.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*socket_, create(_, _)).WillOnce(Return(ReturnCode::kError));

    EXPECT_EQ(server_->init("test"), Server::EIpcInitResult::kFailed);
}

TEST_F(IpcServerInitTest, DefaultConstructor_CreatesSocket)
{
    RecordProperty("Description", "The default constructor argument allocates a socket.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_NO_THROW({ Server defaultServer; });
}

TEST_F(IpcServerAccessRightsTest, ShmOpenFails_ReturnsFalse)
{
    RecordProperty("Description", "setAccessRights() fails if the shared memory cannot be opened.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(StrEq("/test"), O_RDWR, _)).WillOnce(Return(-1));

    EXPECT_FALSE(server_->setAccessRights(1000U));
}

TEST_F(IpcServerAccessRightsTest, AclInitFails_ClosesDescriptorAndReturnsFalse)
{
    RecordProperty("Description", "setAccessRights() releases the descriptor when ACL allocation fails.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_acl_mock, acl_init(5)).WillOnce(Return(nullptr));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    EXPECT_FALSE(server_->setAccessRights(1000U));
}

TEST_F(IpcServerAccessRightsTest, AclInvalid_FreesAclClosesDescriptorAndReturnsFalse)
{
    RecordProperty("Description", "setAccessRights() fails if the assembled ACL is invalid.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_acl_mock, acl_init(_)).WillOnce(Return(fakeAcl()));
    EXPECT_CALL(*g_acl_mock, acl_valid(fakeAcl())).WillOnce(Return(-1));
    EXPECT_CALL(*g_acl_mock, acl_free(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    EXPECT_FALSE(server_->setAccessRights(1000U));
    EXPECT_EQ(aclEntryCalls_, kTotalAclEntryCalls);
}

TEST_F(IpcServerAccessRightsTest, AclSetFdFails_FreesAclClosesDescriptorAndReturnsFalse)
{
    RecordProperty("Description", "setAccessRights() fails if the ACL cannot be applied to the descriptor.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_acl_mock, acl_init(_)).WillOnce(Return(fakeAcl()));
    EXPECT_CALL(*g_acl_mock, acl_valid(_)).WillOnce(Return(0));
    EXPECT_CALL(*g_acl_mock, acl_set_fd(kFd, fakeAcl())).WillOnce(Return(-1));
    EXPECT_CALL(*g_acl_mock, acl_free(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    EXPECT_FALSE(server_->setAccessRights(1000U));
}

TEST_F(IpcServerAccessRightsTest, AllStepsSucceed_ReturnsTrue)
{
    RecordProperty("Description", "setAccessRights() builds, validates and applies the ACL for the given user.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_acl_mock, acl_init(_)).WillOnce(Return(fakeAcl()));
    EXPECT_CALL(*g_acl_mock, acl_valid(_)).WillOnce(Return(0));
    EXPECT_CALL(*g_acl_mock, acl_set_fd(kFd, _)).WillOnce(Return(0));
    EXPECT_CALL(*g_acl_mock, acl_free(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    EXPECT_TRUE(server_->setAccessRights(1000U));
    EXPECT_EQ(aclEntryCalls_, kTotalAclEntryCalls);
}

TEST_P(IpcServerAccessRightsEntryFailureTest, NthAclEntryCallFails_FreesAclAndReturnsFalse)
{
    RecordProperty("Description", "A failure in any single ACL entry call aborts setAccessRights().");
    RecordProperty("DerivationTechnique", "fault-injection");

    aclFailAt_ = GetParam();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_acl_mock, acl_init(_)).WillOnce(Return(fakeAcl()));
    EXPECT_CALL(*g_acl_mock, acl_free(_)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    EXPECT_FALSE(server_->setAccessRights(1000U));
}

INSTANTIATE_TEST_SUITE_P(EachAclEntryCall,
                         IpcServerAccessRightsEntryFailureTest,
                         ::testing::Range(1, kTotalAclEntryCalls + 1));

TEST_F(IpcServerMoveTest, MoveAssignedServer_TakesOverSocket)
{
    RecordProperty("Description", "Move assignment transfers the socket and the initialized state.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    initializeServer();
    Server other{std::make_unique<StrictMock<MockSocket>>()};
    EXPECT_CALL(*socket_, tryPop()).WillOnce(Return(ReturnCode::kOk));
    EXPECT_CALL(*socket_, close()).Times(1);

    other = std::move(*server_);

    EXPECT_TRUE(other.pop());
}

}  // namespace
}  // namespace score::mw::lifecycle::internal::saf::ipc
