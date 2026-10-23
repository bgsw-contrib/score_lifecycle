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

#ifndef B0A85C59_3E7B_4E92_A574_9AFF9C9D869F
#define B0A85C59_3E7B_4E92_A574_9AFF9C9D869F

#ifndef MOCK_ACL
#define MOCK_ACL

#include <sys/acl.h>
#include <memory>

#include <gmock/gmock.h>

// Linux only. The test must link with -Wl,--wrap=<fn> for every function mocked here and
// include this header in a single translation unit. Calls are never forwarded to libacl.

// NOLINTBEGIN - clang-tidy does not like C linkage wrappers

class AclMock
{
  public:
    MOCK_METHOD(acl_t, acl_init, (int), ());
    MOCK_METHOD(int, acl_create_entry, (acl_t*, acl_entry_t*), ());
    MOCK_METHOD(int, acl_set_tag_type, (acl_entry_t, acl_tag_t), ());
    MOCK_METHOD(int, acl_set_qualifier, (acl_entry_t, const void*), ());
    MOCK_METHOD(int, acl_get_permset, (acl_entry_t, acl_permset_t*), ());
    MOCK_METHOD(int, acl_add_perm, (acl_permset_t, acl_perm_t), ());
    MOCK_METHOD(int, acl_valid, (acl_t), ());
    MOCK_METHOD(int, acl_set_fd, (int, acl_t), ());
    MOCK_METHOD(int, acl_free, (void*), ());
};

std::unique_ptr<AclMock> g_acl_mock = nullptr;

extern "C" {
acl_t __wrap_acl_init(int count)
{
    return g_acl_mock->acl_init(count);
}

int __wrap_acl_create_entry(acl_t* acl, acl_entry_t* entry)
{
    return g_acl_mock->acl_create_entry(acl, entry);
}

int __wrap_acl_set_tag_type(acl_entry_t entry, acl_tag_t tag)
{
    return g_acl_mock->acl_set_tag_type(entry, tag);
}

int __wrap_acl_set_qualifier(acl_entry_t entry, const void* qualifier)
{
    return g_acl_mock->acl_set_qualifier(entry, qualifier);
}

int __wrap_acl_get_permset(acl_entry_t entry, acl_permset_t* permset)
{
    return g_acl_mock->acl_get_permset(entry, permset);
}

int __wrap_acl_add_perm(acl_permset_t permset, acl_perm_t perm)
{
    return g_acl_mock->acl_add_perm(permset, perm);
}

int __wrap_acl_valid(acl_t acl)
{
    return g_acl_mock->acl_valid(acl);
}

int __wrap_acl_set_fd(int fd, acl_t acl)
{
    return g_acl_mock->acl_set_fd(fd, acl);
}

int __wrap_acl_free(void* obj)
{
    return g_acl_mock->acl_free(obj);
}
}

// NOLINTEND

#endif  // MOCK_ACL


#endif /* B0A85C59_3E7B_4E92_A574_9AFF9C9D869F */
