# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tests/utils/bazel:unit_test.bzl", "linker_wrap_symbols", "lm_cc_test")

_SYSCALL_WRAPS = [
    "fstat",
    "close",
    "getpid",
    "mmap",
    "fork",
    "execve",
    "kill",
    "wait",
    "shm_open",
    "shm_unlink",
    "ftruncate",
    "access",
    "munmap",
    "setpgid",
    "setgid",
    "setuid",
    "sched_setscheduler",
    "chdir",
    "setrlimit",
    "fcntl",
    "sem_destroy",
    "msync",
]

_ACL_WRAPS = [
    "acl_init",
    "acl_create_entry",
    "acl_set_tag_type",
    "acl_set_qualifier",
    "acl_get_permset",
    "acl_add_perm",
    "acl_valid",
    "acl_set_fd",
    "acl_free",
]

cc_library(
    name = "ipc_if",
    hdrs = [
        "IpcBase.hpp",
        "IpcClient.hpp",
        "IpcServer.hpp",
    ],
    include_prefix = "score/mw/launch_manager/alive_monitor/details/ipc",
    strip_include_prefix = "/score/launch_manager/src/daemon/src/alive_monitor/details/ipc",
    visibility = [
        "//score/launch_manager/src/alive:__subpackages__",
        "//score/launch_manager/src/daemon/src/alive_monitor:__subpackages__",
    ],
    deps = [":ipc_impl"] + select({
        "@platforms//os:qnx": [],
        "@platforms//os:linux": ["//externals/acl"],
    }),
    alwayslink = True,
)

cc_library(
    name = "ipc_impl",
    hdrs = [
        "ringbuffer.hpp",
        "socket.hpp",
    ],
    include_prefix = "score/mw/launch_manager/alive_monitor/details/ipc",
    strip_include_prefix = "/score/launch_manager/src/daemon/src/alive_monitor/details/ipc",
    visibility = [
        "//examples/control_application:__pkg__",
        "//score/launch_manager/src/daemon/src/alive_monitor/details/ifexm:__pkg__",
    ],
)

cc_library(
    name = "mock_socket",
    testonly = True,
    hdrs = ["MockSocket.hpp"],
    include_prefix = "score/mw/launch_manager/alive_monitor/details/ipc",
    strip_include_prefix = "/score/launch_manager/src/daemon/src/alive_monitor/details/ipc",
    visibility = ["//score/launch_manager/src/daemon/src/alive_monitor:__subpackages__"],
    deps = [
        ":ipc_impl",
        "@googletest//:gtest_main",
    ],
)

lm_cc_test(
    name = "RingBuffer_UT",
    srcs = ["RingBuffer_UT.cpp"],
    deps = [
        ":ipc_impl",
        "@googletest//:gtest_main",
    ],
)

lm_cc_test(
    name = "Socket_UT",
    srcs = ["Socket_UT.cpp"],
    linkopts = linker_wrap_symbols(_SYSCALL_WRAPS),
    linkstatic = True,
    deps = [
        ":ipc_impl",
        "//tests/utils/mocks:mock_syscalls",
        "@googletest//:gtest_main",
    ],
)

lm_cc_test(
    name = "IpcClient_UT",
    srcs = ["IpcClient_UT.cpp"],
    deps = [
        ":ipc_if",
        ":mock_socket",
        "@googletest//:gtest_main",
    ],
)

lm_cc_test(
    name = "IpcServer_UT",
    srcs = ["IpcServer_UT.cpp"],
    linkopts = linker_wrap_symbols(_SYSCALL_WRAPS + _ACL_WRAPS),
    linkstatic = True,
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":ipc_if",
        ":mock_socket",
        "//tests/utils/mocks:mock_acl",
        "//tests/utils/mocks:mock_syscalls",
        "@googletest//:gtest_main",
    ],
)
