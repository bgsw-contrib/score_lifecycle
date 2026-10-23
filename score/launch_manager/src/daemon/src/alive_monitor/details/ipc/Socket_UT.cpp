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

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "tests/utils/mocks/mock_syscalls.hpp"

#include "score/mw/launch_manager/alive_monitor/details/ipc/socket.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{
namespace
{

using ::testing::_;
using ::testing::Ne;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

constexpr std::size_t kElementSize = 8U;
constexpr std::size_t kCapacity = 4U;
constexpr int kFd = 5;

using TestSocket = Socket<kElementSize, kCapacity>;
using Buffer = RingBuffer<kCapacity, kElementSize>;

auto FailWith(int f_errno, int f_ret = -1)
{
    return [f_errno, f_ret](auto&&...) {
        errno = f_errno;
        return f_ret;
    };
}

auto MmapFailWith(int f_errno)
{
    return [f_errno](auto&&...) -> void* {
        errno = f_errno;
        return MAP_FAILED;
    };
}

class SocketTestBase : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        RecordProperty("TestType", "unit-test");
        g_syscall_mock = std::unique_ptr<SyscallMock>(new NiceMock<SyscallMock>());
        new (storage_) Buffer();
    }

    void TearDown() override
    {
        g_syscall_mock.reset();
    }

    Buffer& buffer()
    {
        return *reinterpret_cast<Buffer*>(storage_);
    }

    void expectNothingExists()
    {
        EXPECT_CALL(*g_syscall_mock, shm_open(_, O_RDONLY, _)).WillRepeatedly(Return(-1));
    }

    void expectCreateSucceeds()
    {
        expectNothingExists();
        EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillRepeatedly(Return(kFd));
        EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillRepeatedly(Return(0));
        EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillRepeatedly(Return(storage_));
    }

    void connectInitialized(TestSocket& f_socket)
    {
        buffer().initialize();
        EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillRepeatedly(Return(kFd));
        EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillRepeatedly(Return(storage_));
        EXPECT_EQ(f_socket.connect("test"), ReturnCode::kOk);
    }

    alignas(std::max_align_t) unsigned char storage_[sizeof(Buffer)]{};
};

class SocketCreateTest : public SocketTestBase
{
};

class SocketConnectTest : public SocketTestBase
{
};

class SocketCloseTest : public SocketTestBase
{
};

class SocketOperationsTest : public SocketTestBase
{
};

TEST_F(SocketCreateTest, NameWithoutSlash_PrefixesSlashAndInitializesBuffer)
{
    RecordProperty("Description", "create() prepends '/' to the name and initializes the mapped ring buffer.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    expectCreateSucceeds();
    EXPECT_CALL(*g_syscall_mock, shm_unlink(StrEq("/test"))).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, munmap(_, _)).Times(1).WillOnce(Return(0));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kOk);
    EXPECT_EQ(socket.getName(), "/test");
    EXPECT_TRUE(buffer().isInitialized());
}

TEST_F(SocketCreateTest, NameWithSlash_IsKeptUnchanged)
{
    RecordProperty("Description", "create() does not add a second '/' to an already absolute name.");
    RecordProperty("DerivationTechnique", "boundary-values");

    expectCreateSucceeds();

    TestSocket socket;
    EXPECT_EQ(socket.create("/test", 0600), ReturnCode::kOk);
    EXPECT_EQ(socket.getName(), "/test");
}

TEST_F(SocketCreateTest, EmptyName_DoesNotUnlinkOnClose)
{
    RecordProperty("Description", "An empty name is not prefixed and is not unlinked during cleanup.");
    RecordProperty("DerivationTechnique", "boundary-values");

    expectCreateSucceeds();
    EXPECT_CALL(*g_syscall_mock, shm_unlink(_)).Times(0);

    TestSocket socket;
    EXPECT_EQ(socket.create("", 0600), ReturnCode::kOk);
    EXPECT_EQ(socket.getName(), "");
}

TEST_F(SocketCreateTest, StaleSharedMemory_IsUnlinkedBeforeCreation)
{
    RecordProperty("Description", "Existing shared memory is closed and unlinked before it is created again.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, O_RDONLY, _)).WillOnce(Return(4));
    EXPECT_CALL(*g_syscall_mock, close(4)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, shm_unlink(StrEq("/test"))).Times(2).WillRepeatedly(Return(0));
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(Return(storage_));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kOk);
}

TEST_F(SocketCreateTest, StaleSharedMemoryUnlinkDenied_ReturnsPermissionDenied)
{
    RecordProperty("Description", "EACCES while unlinking stale shared memory maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, O_RDONLY, _)).WillOnce(Return(4));
    EXPECT_CALL(*g_syscall_mock, shm_unlink(_)).WillOnce(FailWith(EACCES));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, StaleSharedMemoryUnlinkFails_ReturnsError)
{
    RecordProperty("Description", "A non-EACCES error while unlinking stale shared memory maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, O_RDONLY, _)).WillOnce(Return(4));
    EXPECT_CALL(*g_syscall_mock, shm_unlink(_)).WillOnce(FailWith(EIO));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kError);
}

TEST_F(SocketCreateTest, ShmOpenDenied_ReturnsPermissionDenied)
{
    RecordProperty("Description", "EACCES from shm_open(O_CREAT) maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(FailWith(EACCES));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, ShmOpenFails_ReturnsError)
{
    RecordProperty("Description", "A non-EACCES error from shm_open(O_CREAT) maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(FailWith(EMFILE));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kError);
}

TEST_F(SocketCreateTest, FtruncateFailsWithEacces_CleansUpAndReturnsPermissionDenied)
{
    RecordProperty("Description", "ftruncate EACCES releases the descriptor, unlinks, and returns kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(FailWith(EACCES));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, shm_unlink(StrEq("/test"))).Times(1).WillOnce(Return(0));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, FtruncateFailsWithEperm_ReturnsPermissionDenied)
{
    RecordProperty("Description", "ftruncate EPERM maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(FailWith(EPERM));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, FtruncateFails_ReturnsError)
{
    RecordProperty("Description", "Any other ftruncate error maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(FailWith(ENOSPC));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kError);
}

TEST_F(SocketCreateTest, MmapFailsWithEacces_ReturnsPermissionDenied)
{
    RecordProperty("Description", "mmap EACCES maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(EACCES));
    EXPECT_CALL(*g_syscall_mock, munmap(_, _)).Times(0);

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, MmapFailsWithEperm_ReturnsPermissionDenied)
{
    RecordProperty("Description", "mmap EPERM maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(EPERM));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kPermissionDenied);
}

TEST_F(SocketCreateTest, MmapFails_ReturnsError)
{
    RecordProperty("Description", "Any other mmap error maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    expectNothingExists();
    EXPECT_CALL(*g_syscall_mock, shm_open(_, Ne(O_RDONLY), _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, ftruncate(kFd, _)).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(ENOMEM));

    TestSocket socket;
    EXPECT_EQ(socket.create("test", 0600), ReturnCode::kError);
}

TEST_F(SocketConnectTest, InitializedBuffer_ReturnsOkAndDoesNotUnlinkOnClose)
{
    RecordProperty("Description", "connect() maps an initialized buffer; a client never unlinks the shared memory.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    buffer().initialize();
    EXPECT_CALL(*g_syscall_mock, shm_open(StrEq("/test"), O_RDWR | O_CLOEXEC, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(Return(storage_));
    EXPECT_CALL(*g_syscall_mock, shm_unlink(_)).Times(0);

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kOk);
    EXPECT_EQ(socket.getName(), "/test");
}

TEST_F(SocketConnectTest, NameWithSlash_IsKeptUnchanged)
{
    RecordProperty("Description", "connect() does not add a second '/' to an already absolute name.");
    RecordProperty("DerivationTechnique", "boundary-values");

    buffer().initialize();
    EXPECT_CALL(*g_syscall_mock, shm_open(StrEq("/test"), _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(Return(storage_));

    TestSocket socket;
    EXPECT_EQ(socket.connect("/test"), ReturnCode::kOk);
}

TEST_F(SocketConnectTest, EmptyName_IsNotPrefixed)
{
    RecordProperty("Description", "An empty name is passed through without a '/' prefix.");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_CALL(*g_syscall_mock, shm_open(StrEq(""), _, _)).WillOnce(FailWith(ENOENT));

    TestSocket socket;
    EXPECT_EQ(socket.connect(""), ReturnCode::kError);
}

TEST_F(SocketConnectTest, ShmOpenDenied_ReturnsPermissionDenied)
{
    RecordProperty("Description", "EACCES from shm_open maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(FailWith(EACCES));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kPermissionDenied);
}

TEST_F(SocketConnectTest, ShmOpenFails_ReturnsError)
{
    RecordProperty("Description", "Any other shm_open error maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(FailWith(ENOENT));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kError);
}

TEST_F(SocketConnectTest, MmapFailsWithEacces_ClosesDescriptorAndReturnsPermissionDenied)
{
    RecordProperty("Description", "mmap EACCES releases the descriptor and maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(EACCES));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kPermissionDenied);
}

TEST_F(SocketConnectTest, MmapFailsWithEperm_ReturnsPermissionDenied)
{
    RecordProperty("Description", "mmap EPERM maps to kPermissionDenied.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(EPERM));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kPermissionDenied);
}

TEST_F(SocketConnectTest, MmapFails_ReturnsError)
{
    RecordProperty("Description", "Any other mmap error maps to kError.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(MmapFailWith(ENOMEM));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kError);
}

TEST_F(SocketConnectTest, UninitializedBuffer_ReturnsError)
{
    RecordProperty("Description", "connect() fails if the server has not initialized the ring buffer yet.");
    RecordProperty("DerivationTechnique", "error-guessing");

    EXPECT_CALL(*g_syscall_mock, shm_open(_, _, _)).WillOnce(Return(kFd));
    EXPECT_CALL(*g_syscall_mock, mmap(_, _, _, _, kFd, _)).WillOnce(Return(storage_));

    TestSocket socket;
    EXPECT_EQ(socket.connect("test"), ReturnCode::kError);
}

TEST_F(SocketCloseTest, WithoutBuffer_DoesNotUnmap)
{
    RecordProperty("Description", "close() on a socket that never mapped memory does not call munmap.");
    RecordProperty("DerivationTechnique", "boundary-values");

    EXPECT_CALL(*g_syscall_mock, munmap(_, _)).Times(0);

    TestSocket socket;
    socket.close();
}

TEST_F(SocketCloseTest, WithBuffer_UnmapsOnce)
{
    RecordProperty("Description", "close() unmaps the buffer once; a second close() is a no-op.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    EXPECT_CALL(*g_syscall_mock, munmap(storage_, sizeof(Buffer))).Times(1).WillOnce(Return(0));
    EXPECT_CALL(*g_syscall_mock, close(kFd)).Times(1).WillOnce(Return(0));

    TestSocket socket;
    connectInitialized(socket);
    socket.close();
    socket.close();
}

TEST_F(SocketOperationsTest, WithoutBuffer_ReturnsFailureCodes)
{
    RecordProperty("Description", "All data operations on an unmapped socket report an error.");
    RecordProperty("DerivationTechnique", "error-guessing");

    TestSocket socket;
    std::uint32_t value{1U};
    std::uint32_t received{0U};
    std::uint32_t* peeked{nullptr};

    EXPECT_FALSE(socket.getOverflowFlag());
    EXPECT_EQ(socket.trySendEmplace<std::uint32_t>(value), ReturnCode::kError);
    EXPECT_EQ(socket.tryPeek(peeked), ReturnCode::kError);
    EXPECT_EQ(socket.tryPop(), ReturnCode::kError);
    EXPECT_EQ(socket.tryReceive(received), ReturnCode::kQueueStateCorrupt);
    EXPECT_EQ(socket.trySend(value), ReturnCode::kError);
}

TEST_F(SocketOperationsTest, SendEmplacePeekPop_RoundTrip)
{
    RecordProperty("Description", "An emplaced element can be peeked and popped.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    TestSocket socket;
    connectInitialized(socket);
    std::uint32_t* peeked{nullptr};

    EXPECT_EQ(socket.tryPeek(peeked), ReturnCode::kQueueEmpty);
    EXPECT_EQ(socket.trySendEmplace<std::uint32_t>(std::uint32_t{7U}), ReturnCode::kOk);
    ASSERT_EQ(socket.tryPeek(peeked), ReturnCode::kOk);
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 7U);
    EXPECT_EQ(socket.tryPop(), ReturnCode::kOk);
    EXPECT_EQ(socket.tryPop(), ReturnCode::kError);
}

TEST_F(SocketOperationsTest, PeekWithMismatchingElementSize_ReturnsError)
{
    RecordProperty("Description", "tryPeek() reports kError when the stored element has a different size.");
    RecordProperty("DerivationTechnique", "error-guessing");

    TestSocket socket;
    connectInitialized(socket);
    EXPECT_TRUE(buffer().tryEnqueue(std::uint8_t{1U}));
    std::uint32_t* peeked{nullptr};

    EXPECT_EQ(socket.tryPeek(peeked), ReturnCode::kError);
}

TEST_F(SocketOperationsTest, SendAndReceive_RoundTrip)
{
    RecordProperty("Description", "trySend() followed by tryReceive() returns the same value.");
    RecordProperty("DerivationTechnique", "requirements-analysis");

    TestSocket socket;
    connectInitialized(socket);
    std::uint32_t sent{11U};
    std::uint32_t received{0U};

    EXPECT_EQ(socket.tryReceive(received), ReturnCode::kQueueEmpty);
    EXPECT_EQ(socket.trySend(sent), ReturnCode::kOk);
    EXPECT_EQ(socket.tryReceive(received), ReturnCode::kOk);
    EXPECT_EQ(received, sent);
}

TEST_F(SocketOperationsTest, ReceiveWithMismatchingElementSize_ReturnsError)
{
    RecordProperty("Description", "tryReceive() reports kError when the stored element has a different size.");
    RecordProperty("DerivationTechnique", "error-guessing");

    TestSocket socket;
    connectInitialized(socket);
    EXPECT_TRUE(buffer().tryEnqueue(std::uint8_t{1U}));
    std::uint32_t received{0U};

    EXPECT_EQ(socket.tryReceive(received), ReturnCode::kError);
}

TEST_F(SocketOperationsTest, FullBuffer_SendFailsAndOverflowFlagCanBeReset)
{
    RecordProperty("Description", "Sending into a full buffer fails and sets the resettable overflow flag.");
    RecordProperty("DerivationTechnique", "boundary-values");

    TestSocket socket;
    connectInitialized(socket);
    std::uint32_t value{1U};
    for (std::size_t i = 0U; i < kCapacity; ++i)
    {
        EXPECT_EQ(socket.trySend(value), ReturnCode::kOk);
    }

    EXPECT_FALSE(socket.getOverflowFlag());
    EXPECT_EQ(socket.trySend(value), ReturnCode::kError);
    EXPECT_EQ(socket.trySendEmplace<std::uint32_t>(value), ReturnCode::kError);
    EXPECT_TRUE(socket.getOverflowFlag(true));
    EXPECT_FALSE(socket.getOverflowFlag());
}

}  // namespace
}  // namespace score::mw::lifecycle::internal::saf::ipc
