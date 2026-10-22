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

#ifndef MOCK_CLOCK
#define MOCK_CLOCK

#include <ctime>

#include <atomic>

// Requires linking with -Wl,--wrap=clock_gettime and -Wl,--wrap=clock_getres.
inline std::atomic<bool> g_fail_monotonic_clock_gettime{false};
inline std::atomic<bool> g_fail_clock_getres{false};

extern "C" {
extern int __real_clock_getres(clockid_t clock_id, struct timespec* res);

int __wrap_clock_getres(clockid_t clock_id, struct timespec* res)
{
    if (g_fail_clock_getres.load())
    {
        return -1;
    }

    return __real_clock_getres(clock_id, res);
}

extern int __real_clock_gettime(clockid_t clock_id, struct timespec* ts);

int __wrap_clock_gettime(clockid_t clock_id, struct timespec* ts)
{
    if (clock_id == CLOCK_MONOTONIC && g_fail_monotonic_clock_gettime.load())
    {
        return -1;
    }

    return __real_clock_gettime(clock_id, ts);
}
}

#endif  // MOCK_CLOCK
