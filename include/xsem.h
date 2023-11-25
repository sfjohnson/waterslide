// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _XSEM_H
#define _XSEM_H

#include <stdint.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

// https://stackoverflow.com/a/27847103
#ifdef __APPLE__
  typedef dispatch_semaphore_t xsem_t;
#else
  typedef sem_t xsem_t;
#endif

inline int xsem_init (xsem_t *sem, uint32_t value) {
#ifdef __APPLE__
  *sem = dispatch_semaphore_create(value);
  return 0;
#else
  return sem_init(sem, 0, value);
#endif
}

inline int xsem_wait (xsem_t *sem) {
#ifdef __APPLE__
  return dispatch_semaphore_wait(*sem, DISPATCH_TIME_FOREVER) == 0 ? 0 : -1;
#else
  return sem_wait(sem);
#endif
}

inline int xsem_post (xsem_t *sem) {
#ifdef __APPLE__
  dispatch_semaphore_signal(*sem);
  return 0;
#else
  return sem_post(sem);
#endif
}

inline int xsem_getvalue (xsem_t *sem) {
#ifdef __APPLE__
  dispatch_semaphore_signal(*sem);
  return 0;
#else
  int val = 0;
  sem_getvalue(sem, &val);
  return val;
#endif
}

inline int xsem_destroy (xsem_t *sem) {
#ifdef __APPLE__
  dispatch_release(*sem);
  return 0;
#else
  return sem_destroy(sem);
#endif
}

#endif
