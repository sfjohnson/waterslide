// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _XWAIT_H
#define _XWAIT_H

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#define _GNU_SOURCE
#include <stdint.h>
#include <stdatomic.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

// NOTES: when testing C++20 atomic_wait on macOS I found that it was way too slow
// macOS Dispatch Semaphores work way better, but we still need a solution for Linux
// atomic_wait is probably implemented using Futexes on Linux, so let's use those directly
// macOS requires notify and wait calls to be balanced so the semaphore doesn't go negative
// refs:
// - https://stackoverflow.com/a/27847103
// - https://stackoverflow.com/a/77147230/23387122

#ifdef __APPLE__
  typedef dispatch_semaphore_t xwait_t;
#else
  typedef _Atomic uint32_t xwait_t;
#endif

static inline void xwait_init (xwait_t *handle) {
#ifdef __APPLE__
  *handle = dispatch_semaphore_create(0);
#else
  atomic_store(handle, 0);
#endif
}

static inline void xwait_wait (xwait_t *handle) {
#ifdef __APPLE__
  dispatch_semaphore_wait(*handle, DISPATCH_TIME_FOREVER);
#else
  // wait iff *handle == 0
  syscall(SYS_futex, handle, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
  atomic_fetch_sub(handle, 1);
#endif
}

static inline void xwait_notify (xwait_t *handle) {
#ifdef __APPLE__
  dispatch_semaphore_signal(*handle);
#else
  atomic_fetch_add(handle, 1);
  syscall(SYS_futex, handle, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
#endif
}

static inline void xwait_destroy (xwait_t *handle) {
#ifdef __APPLE__
  dispatch_release(*handle);
#else
  atomic_store(handle, 0);
#endif
}

#endif
