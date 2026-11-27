/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "control.hpp"
#include "score/mw/launch_manager/alive_monitor/details/ipc/socket.hpp"

namespace
{
/// @brief Holds an exclusive inter-process lock for its lifetime
/// @details Serializes senders because the control queue allows only one producer at a time. The OS releases the
/// lock if the process dies.
class SendLock
{
  public:
    /// @brief Opens the lock file and blocks until the exclusive lock is acquired
    /// @details On failure the object stays unlocked and holds no file descriptor.
    SendLock() : fd_(::open(control_send_lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600))
    {
        if (fd_ < 0)
        {
            return;
        }
        struct flock lock{};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        while (::fcntl(fd_, F_SETLKW, &lock) == -1)
        {
            if (errno != EINTR)
            {
                ::close(fd_);
                fd_ = -1;
                return;
            }
        }
    }
    ~SendLock()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }
    SendLock(const SendLock&) = delete;
    SendLock& operator=(const SendLock&) = delete;
    SendLock(SendLock&&) = delete;
    SendLock& operator=(SendLock&&) = delete;

    /// @brief Checks whether the lock is held
    /// @return true if the lock was acquired
    bool isLocked() const
    {
        return fd_ >= 0;
    }

  private:
    int fd_;
};
}  // namespace

int main(int argc, char** argv)
{
    if (argc <= 1)
    {
        std::cout << "Usage: " << argv[0] << " MyRunTargetName" << std::endl;
        return EXIT_FAILURE;
    }

    score::mw::lifecycle::internal::saf::ipc::
        Socket<static_cast<size_t>(sizeof(RunTargetInfo)), control_socket_capacity>
            sm_control_socket{};
    if (sm_control_socket.connect(control_socket_path) != score::mw::lifecycle::internal::saf::ipc::ReturnCode::kOk)
    {
        std::cerr << "Could not connect to control socket" << std::endl;
        return EXIT_FAILURE;
    }

    SendLock send_lock;
    if (!send_lock.isLocked())
    {
        std::cerr << "Could not lock control socket for sending" << std::endl;
        return EXIT_FAILURE;
    }

    RunTargetInfo info{};
    std::strncpy(info.runTargetName, argv[1], sizeof(info.runTargetName) - 1);
    if (score::mw::lifecycle::internal::saf::ipc::ReturnCode::kOk == sm_control_socket.trySend(info))
    {
        std::cout << "Successfully sent request" << std::endl;
        return EXIT_SUCCESS;
    }
    else
    {
        std::cerr << "Request could not be sent" << std::endl;
        return EXIT_FAILURE;
    }
}
