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

#ifndef ED1D7E55_4340_4EAF_96C7_3180EF5FB46B
#define ED1D7E55_4340_4EAF_96C7_3180EF5FB46B

#ifndef MOCK_IPC_SOCKET_HPP_INCLUDED
#define MOCK_IPC_SOCKET_HPP_INCLUDED

#include <cstdint>
#include <string_view>

#include <gmock/gmock.h>

#include "score/mw/launch_manager/alive_monitor/details/ipc/socket.hpp"

namespace score::mw::lifecycle::internal::saf::ipc
{

struct TestPayload
{
    std::uint32_t value{0U};

    TestPayload() = default;
    explicit TestPayload(std::uint32_t f_value) noexcept : value(f_value)
    {
    }
};

/// @brief Socket replacement injected into IpcBase / IpcClient / IpcServer through their Socket template parameter.
class MockSocket
{
  public:
    virtual ~MockSocket() = default;

    MOCK_METHOD(ReturnCode, create, (const char*, mode_t), ());
    MOCK_METHOD(ReturnCode, connect, (const char*), (noexcept));
    MOCK_METHOD(void, close, (), (noexcept));
    MOCK_METHOD(std::string_view, getName, (), (const, noexcept));
    MOCK_METHOD(bool, getOverflowFlagMock, (), (noexcept));
    MOCK_METHOD(ReturnCode, tryPeek, (TestPayload * &), ());
    MOCK_METHOD(ReturnCode, tryPop, (), ());
    MOCK_METHOD(ReturnCode, sendEmplaceMock, (), ());

    bool getOverflowFlag(bool = false) noexcept
    {
        return getOverflowFlagMock();
    }

    template <class Payload, typename... Args>
    ReturnCode trySendEmplace(Args&&...)
    {
        return sendEmplaceMock();
    }
};

}  // namespace score::mw::lifecycle::internal::saf::ipc

#endif


#endif /* ED1D7E55_4340_4EAF_96C7_3180EF5FB46B */
