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
// 1i: scalar signed integer (32-bit or 64-bit depending on arch)
// 1ui: scalar unsigned integer (32-bit or 64-bit depending on arch)
// 1iv: individually addressable array of signed integers (32-bit or 64-bit depending on arch)
// 1uiv: individually addressable array of unsigned integers (32-bit or 64-bit depending on arch)
// 1ff: scalar double-precision float
// 1ffv: individually addressable array of doubles
// 1s: null-terminated char array
// 1sv: individually addressable null-terminated char arrays

#define globals_set1i(GROUP, NAME, VALUE) globals_1i_##GROUP##_##NAME = (VALUE)
#define globals_set1ui(GROUP, NAME, VALUE) globals_1ui_##GROUP##_##NAME = (VALUE)
#define globals_set1iv(GROUP, NAME, INDEX, VALUE) globals_1iv_##GROUP##_##NAME[INDEX] = (VALUE)
#define globals_set1uiv(GROUP, NAME, INDEX, VALUE) globals_1uiv_##GROUP##_##NAME[INDEX] = (VALUE)
#define globals_set1ff(GROUP, NAME, VALUE) _globals_set1ff_##GROUP##_##NAME(VALUE)
#define globals_set1ffv(GROUP, NAME, INDEX, VALUE) _globals_set1ffv_##GROUP##_##NAME(INDEX, VALUE)
#define globals_set1s(GROUP, NAME, VALUE) _globals_set1s_##GROUP##_##NAME(VALUE)
#define globals_set1sv(GROUP, NAME, INDEX, VALUE) _globals_set1sv_##GROUP##_##NAME(INDEX, VALUE)

#define globals_get1i(GROUP, NAME) (globals_1i_##GROUP##_##NAME)
#define globals_get1ui(GROUP, NAME) (globals_1ui_##GROUP##_##NAME)
#define globals_get1iv(GROUP, NAME, INDEX) (globals_1iv_##GROUP##_##NAME[INDEX])
#define globals_get1uiv(GROUP, NAME, INDEX) (globals_1uiv_##GROUP##_##NAME[INDEX])

#define globals_get1ff(GROUP, NAME, VAR) _globals_get1ff_##GROUP##_##NAME(VAR)
#define globals_get1ffv(GROUP, NAME, INDEX, VAR) _globals_get1ffv_##GROUP##_##NAME(INDEX, VAR)
// MAX_LEN is the maximum number of bytes that can be written to VAR, including the null terminator
#define globals_get1s(GROUP, NAME, VAR, MAX_LEN) _globals_get1s_##GROUP##_##NAME(VAR, MAX_LEN)
#define globals_get1sv(GROUP, NAME, INDEX, VAR, MAX_LEN) _globals_get1sv_##GROUP##_##NAME(INDEX, VAR, MAX_LEN)

#define globals_add1i(GROUP, NAME, VALUE) atomic_fetch_add_explicit(&globals_1i_##GROUP##_##NAME, (int)(VALUE), memory_order_relaxed)
#define globals_add1ui(GROUP, NAME, VALUE) atomic_fetch_add_explicit(&globals_1ui_##GROUP##_##NAME, (unsigned int)(VALUE), memory_order_relaxed)
#define globals_add1iv(GROUP, NAME, INDEX, VALUE) atomic_fetch_add_explicit(&globals_1iv_##GROUP##_##NAME[INDEX], (int)(VALUE), memory_order_relaxed)
#define globals_add1uiv(GROUP, NAME, INDEX, VALUE) atomic_fetch_add_explicit(&globals_1uiv_##GROUP##_##NAME[INDEX], (unsigned int)(VALUE), memory_order_relaxed)

// These go in the header file
#define globals_declare1i(GROUP, NAME) \
extern atomic_int globals_1i_##GROUP##_##NAME;

#define globals_declare1ui(GROUP, NAME) \
extern atomic_uint globals_1ui_##GROUP##_##NAME;

#define globals_declare1iv(GROUP, NAME) \
extern atomic_int globals_1iv_##GROUP##_##NAME[];

#define globals_declare1uiv(GROUP, NAME) \
extern atomic_uint globals_1uiv_##GROUP##_##NAME[];

// NOTE: If we don't do an explicit store for set1ff and set1ffv (and just memcpy directly into the atomic),
// g++ (but not gcc or clang) complains about atomic_uint_fast64_t having no trivial copy-assignment.

// atomic_uint_fast64_t punned as double
// https://blog.regehr.org/archives/959
#define globals_declare1ff(GROUP, NAME) \
extern atomic_uint_fast64_t globals_1ff_##GROUP##_##NAME; \
static inline void _globals_get1ff_##GROUP##_##NAME (double *x) { \
  memcpy(x, &globals_1ff_##GROUP##_##NAME, 8); \
} \
static inline void _globals_set1ff_##GROUP##_##NAME (double x) { \
  uint_fast64_t xInt = 0; \
  memcpy(&xInt, &x, 8); \
  globals_1ff_##GROUP##_##NAME = xInt; \
}

#define globals_declare1ffv(GROUP, NAME) \
extern atomic_uint_fast64_t globals_1ffv_##GROUP##_##NAME[]; \
static inline void _globals_get1ffv_##GROUP##_##NAME (size_t index, double *x) { \
  memcpy(x, &globals_1ffv_##GROUP##_##NAME[index], 8); \
} \
static inline void _globals_set1ffv_##GROUP##_##NAME (size_t index, double x) { \
  uint_fast64_t xInt = 0; \
  memcpy(&xInt, &x, 8); \
  globals_1ffv_##GROUP##_##NAME[index] = xInt; \
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

#define globals_declare1sv(GROUP, NAME) \
extern char globals_1sv_##GROUP##_##NAME[]; \
extern size_t globals_len_1sv_##GROUP##_##NAME; \
extern pthread_mutex_t globals_lock_1sv_##GROUP##_##NAME; \
static inline int _globals_get1sv_##GROUP##_##NAME (size_t index, char *s, size_t maxLen) { \
  const char *srcStr = &globals_1sv_##GROUP##_##NAME[(globals_len_1sv_##GROUP##_##NAME+1) * index]; \
  size_t len = strlen(srcStr); \
  if (len >= maxLen) return -1; \
  pthread_mutex_lock(&globals_lock_1sv_##GROUP##_##NAME); \
  memcpy(s, srcStr, len + 1); \
  pthread_mutex_unlock(&globals_lock_1sv_##GROUP##_##NAME); \
  return len; \
} \
static inline int _globals_set1sv_##GROUP##_##NAME (size_t index, const char *s) { \
  size_t len = strlen(s); \
  char *destStr = &globals_1sv_##GROUP##_##NAME[(globals_len_1sv_##GROUP##_##NAME+1) * index]; \
  if (len > globals_len_1sv_##GROUP##_##NAME) return -1; \
  pthread_mutex_lock(&globals_lock_1sv_##GROUP##_##NAME); \
  memcpy(destStr, s, len + 1); \
  pthread_mutex_unlock(&globals_lock_1sv_##GROUP##_##NAME); \
  return len; \
}

// These go in the source file
#define globals_define1i(GROUP, NAME) atomic_int globals_1i_##GROUP##_##NAME = 0;
#define globals_define1ui(GROUP, NAME) atomic_uint globals_1ui_##GROUP##_##NAME = 0;
#define globals_define1iv(GROUP, NAME, LEN) atomic_int globals_1iv_##GROUP##_##NAME[LEN] = { 0 };
#define globals_define1uiv(GROUP, NAME, LEN) atomic_uint globals_1uiv_##GROUP##_##NAME[LEN] = { 0 };
#define globals_define1ff(GROUP, NAME) atomic_uint_fast64_t globals_1ff_##GROUP##_##NAME = 0;
#define globals_define1ffv(GROUP, NAME, LEN) atomic_uint_fast64_t globals_1ffv_##GROUP##_##NAME[LEN] = { 0 };

#define globals_define1s(GROUP, NAME, MAX_LEN) \
char globals_1s_##GROUP##_##NAME[MAX_LEN + 1] = { 0 }; \
size_t globals_len_1s_##GROUP##_##NAME = MAX_LEN; \
pthread_mutex_t globals_lock_1s_##GROUP##_##NAME = PTHREAD_MUTEX_INITIALIZER;

// STR_COUNT: number of individually addressable strings
// MAX_STR_LEN: maximum number of characters per string, excluding null terminator (one additional char per string is allocated for it).
#define globals_define1sv(GROUP, NAME, STR_COUNT, MAX_STR_LEN) \
char globals_1sv_##GROUP##_##NAME[(MAX_STR_LEN+1) * STR_COUNT] = { 0 }; \
size_t globals_len_1sv_##GROUP##_##NAME = MAX_STR_LEN; \
pthread_mutex_t globals_lock_1sv_##GROUP##_##NAME = PTHREAD_MUTEX_INITIALIZER;

#endif
