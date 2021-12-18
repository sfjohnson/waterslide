#ifndef _GLOBALS_DEFS_H
#define _GLOBALS_DEFS_H

#include <pthread.h>
#ifdef __cplusplus
#include <atomic>
using namespace std;
#else
#include <stdatomic.h>
#endif
#include <stdint.h>
#include <string.h>

// Data types
// 1i: scalar 32-bit signed integer
// 1ui: scalar 32-bit unsigned integer
// 1iv: vector (array) of 32-bit signed integers
// 1uiv: vector (array) of 32-bit unsigned integers
// 1ff: scalar double-precision float
// 1s: null-terminated char array

#define globals_set1i(GROUP, NAME, VALUE) globals_1i_##GROUP##_##NAME = (VALUE)
#define globals_set1ui(GROUP, NAME, VALUE) globals_1ui_##GROUP##_##NAME = (VALUE)
#define globals_set1iv(GROUP, NAME, INDEX, VALUE) globals_1iv_##GROUP##_##NAME[INDEX] = (VALUE)
#define globals_set1uiv(GROUP, NAME, INDEX, VALUE) globals_1uiv_##GROUP##_##NAME[INDEX] = (VALUE)
#define globals_set1ff(GROUP, NAME, VALUE) globals_1ff_##GROUP##_##NAME = (VALUE)
#define globals_set1s(GROUP, NAME, VALUE) _globals_set1s_##GROUP##_##NAME(VALUE)

// #define globals_get1i(GROUP, NAME, VAR) _globals_get1i_##GROUP##_##NAME(VAR);
// #define globals_get1ui(GROUP, NAME, VAR) _globals_get1ui_##GROUP##_##NAME(VAR);
// #define globals_get1iv(GROUP, NAME, INDEX, VAR) _globals_get1iv_##GROUP##_##NAME(INDEX, VAR);
// #define globals_get1uiv(GROUP, NAME, INDEX, VAR) _globals_get1uiv_##GROUP##_##NAME(INDEX, VAR);
#define globals_get1i(GROUP, NAME) (globals_1i_##GROUP##_##NAME)
#define globals_get1ui(GROUP, NAME) (globals_1ui_##GROUP##_##NAME)
#define globals_get1iv(GROUP, NAME, INDEX) (globals_1iv_##GROUP##_##NAME[INDEX])
#define globals_get1uiv(GROUP, NAME, INDEX) (globals_1uiv_##GROUP##_##NAME[INDEX])

#define globals_get1ff(GROUP, NAME, VAR) _globals_get1ff_##GROUP##_##NAME(VAR)
#define globals_get1s(GROUP, NAME, VAR, MAX_LEN) _globals_get1s_##GROUP##_##NAME(VAR, MAX_LEN)

// These go in the header file
#define globals_declare1i(GROUP, NAME) \
extern atomic_int_fast32_t globals_1i_##GROUP##_##NAME;
// static inline void _globals_get1i_##GROUP##_##NAME (int32_t *x) { *x = globals_1i_##GROUP##_##NAME; }

#define globals_declare1ui(GROUP, NAME) \
extern atomic_uint_fast32_t globals_1ui_##GROUP##_##NAME;
// static inline void _globals_get1ui_##GROUP##_##NAME (uint32_t *x) { *x = globals_1ui_##GROUP##_##NAME; }

#define globals_declare1iv(GROUP, NAME) \
extern atomic_int_fast32_t globals_1iv_##GROUP##_##NAME[];
// static inline void _globals_get1iv_##GROUP##_##NAME (int index, int32_t *x) { *x = globals_1iv_##GROUP##_##NAME[index]; }

#define globals_declare1uiv(GROUP, NAME) \
extern atomic_uint_fast32_t globals_1uiv_##GROUP##_##NAME[];
// static inline void _globals_get1uiv_##GROUP##_##NAME (int index, uint32_t *x) { *x = globals_1uiv_##GROUP##_##NAME[index]; }

// atomic_uint_fast64_t punned as double
// https://blog.regehr.org/archives/959
#define globals_declare1ff(GROUP, NAME) \
extern atomic_uint_fast64_t globals_1ff_##GROUP##_##NAME; \
static inline void _globals_get1ff_##GROUP##_##NAME (double *x) { \
  memcpy(x, &globals_1ff_##GROUP##_##NAME, 8); \
}

#define globals_declare1s(GROUP, NAME) \
extern char globals_1s_##GROUP##_##NAME[]; \
extern size_t globals_len_1s_##GROUP##_##NAME; \
extern pthread_mutex_t globals_lock_1s_##GROUP##_##NAME; \
static inline int _globals_get1s_##GROUP##_##NAME (char *s, size_t maxLen) { \
  size_t len = strlen(globals_1s_##GROUP##_##NAME); \
  if (len >= maxLen) return -1; \
  pthread_mutex_lock(&globals_lock_1s_##GROUP##_##NAME); \
  memcpy(s, globals_1s_##GROUP##_##NAME, len + 1); \
  pthread_mutex_unlock(&globals_lock_1s_##GROUP##_##NAME); \
  return len; \
} \
static inline int _globals_set1s_##GROUP##_##NAME (const char *s) { \
  size_t len = strlen(s); \
  if (len > globals_len_1s_##GROUP##_##NAME) return -1; \
  pthread_mutex_lock(&globals_lock_1s_##GROUP##_##NAME); \
  memcpy(globals_1s_##GROUP##_##NAME, s, len + 1); \
  pthread_mutex_unlock(&globals_lock_1s_##GROUP##_##NAME); \
  return len; \
}

// These go in the source file
#define globals_define1i(GROUP, NAME) atomic_int_fast32_t globals_1i_##GROUP##_##NAME = 0;
#define globals_define1ui(GROUP, NAME) atomic_uint_fast32_t globals_1ui_##GROUP##_##NAME = 0;
#define globals_define1iv(GROUP, NAME, LEN) atomic_int_fast32_t globals_1iv_##GROUP##_##NAME[LEN] = { 0 };
#define globals_define1uiv(GROUP, NAME, LEN) atomic_uint_fast32_t globals_1uiv_##GROUP##_##NAME[LEN] = { 0 };
#define globals_define1ff(GROUP, NAME) atomic_uint_fast64_t globals_1ff_##GROUP##_##NAME = 0;

#define globals_define1s(GROUP, NAME, MAX_LEN) \
char globals_1s_##GROUP##_##NAME[MAX_LEN + 1] = { 0 }; \
size_t globals_len_1s_##GROUP##_##NAME = MAX_LEN; \
pthread_mutex_t globals_lock_1s_##GROUP##_##NAME = PTHREAD_MUTEX_INITIALIZER;

#endif
