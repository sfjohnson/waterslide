#ifndef _XSEM_H
#define _XSEM_H

#include <stdint.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

// https://stackoverflow.com/a/27847103
typedef struct {
#ifdef __APPLE__
  dispatch_semaphore_t sem;
#else
  sem_t sem;
#endif
} xsem_t;

inline void xsem_init (xsem_t *s, uint32_t value) {
#ifdef __APPLE__
  dispatch_semaphore_t *sem = &s->sem;
  *sem = dispatch_semaphore_create(value);
#else
  sem_init(&s->sem, 0, value);
#endif
}

inline void xsem_wait (xsem_t *s) {
#ifdef __APPLE__
  dispatch_semaphore_wait(s->sem, DISPATCH_TIME_FOREVER);
#else
  int r;
  do {
    r = sem_wait(&s->sem);
  } while (r == -1 && errno == EINTR);
#endif
}

inline void xsem_post (xsem_t *s) {
#ifdef __APPLE__
  dispatch_semaphore_signal(s->sem);
#else
  sem_post(&s->sem);
#endif
}

#endif
