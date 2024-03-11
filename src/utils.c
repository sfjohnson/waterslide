// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#elif defined(__linux__) || defined(__ANDROID__)
#define _GNU_SOURCE
#include <sched.h>
#endif

#include <pthread.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include "boringtun/wireguard_ffi.h"
#include "globals.h"
#include "audio.h"
#include "utils.h"

static double levelFastAttack = 0.0, levelFastRelease = 0.0, levelSlowAttack = 0.0, levelSlowRelease = 0.0;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

int utils_ringInit (ck_ring_t *ring, ck_ring_buffer_t **ringBuf, int size) {
  #ifdef W_32_BIT_POINTERS
  // on 32-bit arch we will store 1 double using 2 ring elements (2 x intptr_t)
  size *= 2;
  #endif

  // ck requires the size to be a power of two but we will pretend ring contains
  // size number of double values, and ignore the rest.
  int ringAllocSize = utils_roundUpPowerOfTwo(size);
  *ringBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * ringAllocSize);
  if (*ringBuf == NULL) return -1;
  memset(*ringBuf, 0, sizeof(ck_ring_buffer_t) * ringAllocSize);

  ck_ring_init(ring, ringAllocSize);
  return 0;
}

// DEBUG: making these functions inline would be nice because they are used to process samples,
// but I need to solve "static function is used in an inline function with external linkage"

unsigned int utils_ringSize (const ck_ring_t *ring) {
  #ifdef W_32_BIT_POINTERS
  return ck_ring_size(ring) / 2;
  #else
  return ck_ring_size(ring);
  #endif
}

double utils_ringDequeueSample (ck_ring_t *ring, ck_ring_buffer_t *ringBuf) {
  double sampleDouble;

  #ifdef W_32_BIT_POINTERS
  intptr_t sample[2] __attribute__ ((aligned (8)));
  ck_ring_dequeue_spsc(ring, ringBuf, (void*)&sample[0]);
  ck_ring_dequeue_spsc(ring, ringBuf, (void*)&sample[1]);
  // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
  memcpy(&sampleDouble, sample, 8);
  #else
  intptr_t sample = 0;
  ck_ring_dequeue_spsc(ring, ringBuf, (void*)&sample);
  memcpy(&sampleDouble, &sample, 8);
  #endif

  return sampleDouble;
}

void utils_ringEnqueueSample (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, double x) {
  #ifdef W_32_BIT_POINTERS
  intptr_t sample[2] __attribute__ ((aligned (8)));
  memcpy(sample, &x, 8);
  ck_ring_enqueue_spsc(ring, ringBuf, (void*)sample[0]);
  ck_ring_enqueue_spsc(ring, ringBuf, (void*)sample[1]);
  #else
  intptr_t sample = 0;
  memcpy(&sample, &x, 8);
  ck_ring_enqueue_spsc(ring, ringBuf, (void*)sample);
  #endif
}

void utils_ringDeinit (UNUSED ck_ring_t *ring, ck_ring_buffer_t *ringBuf) {
  free(ringBuf);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void utils_usleep (unsigned int us) {
  #if defined(__linux__) || defined(__ANDROID__)
  struct timespec tsp;
  tsp.tv_nsec = 1000 * us;
  tsp.tv_sec = 0;
  clock_nanosleep(CLOCK_MONOTONIC, 0, &tsp, NULL);
  #else
  usleep(us);
  #endif
}

int utils_getCurrentUTime (void) {
  struct timespec tsp = { 0 };
  // NOTE: CLOCK_MONOTONIC has been observed to jump backwards on macOS https://discussions.apple.com/thread/253778121
  clock_gettime(CLOCK_MONOTONIC_RAW, &tsp);
  return 1000000 * (tsp.tv_sec % 1000) + (tsp.tv_nsec / 1000);
}

int utils_getElapsedUTime (int lastUTime) {
  int intervalUTime = utils_getCurrentUTime() - lastUTime;
  // handle overflow
  return intervalUTime < 0 ? intervalUTime + 1000000000 : intervalUTime;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

int utils_setCallerThreadRealtime (UNUSED int priority, UNUSED int core) {
#if defined(__linux__) || defined(__ANDROID__)
  // Pin to CPU core
  cpu_set_t cpuSet;
  CPU_ZERO(&cpuSet);
  CPU_SET(core, &cpuSet);
  if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuSet) < 0) return -1;

  // Set to RT
  struct sched_param sp;
  memset(&sp, 0, sizeof(sp));
  sp.sched_priority = priority;
  if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) return -2;

  return 0;

#elif defined(__APPLE__)
  // Some light reading
  // - https://user.cockos.com/~deadbeef/index.php?article=854
  // - https://stackoverflow.com/a/44310370
  // - https://gist.github.com/cjappl/20fed4c5631099989af9ca900db68bfa
  // NOTE: 
  // - audio_init() must be called first
  // - priority and core are ignored for macOS

  double deviceLatency = audio_getDeviceLatency();
  if (deviceLatency == 0.0) return -1;

  mach_timebase_info_data_t timebase;
  kern_return_t kr = mach_timebase_info(&timebase);
  if (kr != KERN_SUCCESS) return -2;

  // Set the thread priority.
  struct thread_time_constraint_policy ttcpolicy;
  thread_port_t threadport = pthread_mach_thread_np(pthread_self());

  // In ticks. Therefore to convert nanoseconds to ticks multiply by (timebase.denom / timebase.numer).
  ttcpolicy.period = 1000000000.0 * deviceLatency * timebase.denom / timebase.numer; // Period over which we demand scheduling.
  ttcpolicy.computation = 500000000.0 * deviceLatency * timebase.denom / timebase.numer; // Minimum time in a period where we must be running.
  ttcpolicy.constraint = 1000000000.0 * deviceLatency * timebase.denom / timebase.numer; // Maximum time between start and end of our computation in the period.
  ttcpolicy.preemptible = 1;

  kr = thread_policy_set(threadport, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&ttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (kr != KERN_SUCCESS) return -3;

  // TODO: join workgroup, different ttcpolicy for video

  return 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

inline uint16_t utils_readU16LE (const uint8_t *buf) {
  return ((uint16_t)buf[1] << 8) | buf[0];
}

inline int utils_writeU16LE (uint8_t *buf, uint16_t val) {
  buf[0] = val & 0xff;
  buf[1] = val >> 8;
  return 2;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void utils_setAudioLevelFilters (void) {
  globals_get1ff(audio, levelFastAttack, &levelFastAttack);
  globals_get1ff(audio, levelFastRelease, &levelFastRelease);
  globals_get1ff(audio, levelSlowAttack, &levelSlowAttack);
  globals_get1ff(audio, levelSlowRelease, &levelSlowRelease);
}

// NOTE: this must be audio callback safe
void utils_setAudioStats (double sample, int channel) {
  if (sample >= 1.0 || sample <= -1.0) {
    globals_add1uiv(statsCh1Audio, clippingCounts, channel, 1);
  }

  double levelFast, levelSlow;
  globals_get1ffv(statsCh1Audio, levelsFast, channel, &levelFast);
  globals_get1ffv(statsCh1Audio, levelsSlow, channel, &levelSlow);

  double levelFastDiff = fabs(sample) - levelFast;
  double levelSlowDiff = fabs(sample) - levelSlow;

  if (levelFastDiff > 0) {
    levelFast += levelFastAttack * levelFastDiff;
  } else {
    levelFast += levelFastRelease * levelFastDiff;
  }
  if (levelSlowDiff > 0) {
    levelSlow += levelSlowAttack * levelSlowDiff;
  } else {
    levelSlow += levelSlowRelease * levelSlowDiff;
  }

  globals_set1ffv(statsCh1Audio, levelsFast, channel, levelFast);
  globals_set1ffv(statsCh1Audio, levelsSlow, channel, levelSlow);
}

inline double utils_s16ToDouble (const int16_t *inBuf, int index) {
  double sample = inBuf[index];
  // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
  return sample > 0.0 ? sample/32767.0 : sample/32768.0;
}

// 24-bit index, i.e. 1 unit in index equals 3 bytes in inBuf
inline double utils_s24ToDouble (const uint8_t *inBuf, int index) {
  int32_t sampleInt = 0;
  // Leave the least significant byte of sampleInt empty and then shift back into it to sign extend.
  memcpy((uint8_t *)&sampleInt + 1, inBuf + 3*index, 3);
  sampleInt >>= 8;
  return sampleInt > 0 ? sampleInt/8388607.0 : sampleInt/8388608.0;
}

inline double utils_s32ToDouble (const int32_t *inBuf, int index) {
  double sample = inBuf[index];
  return sample > 0.0 ? sample/2147483647.0 : sample/2147483648.0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

inline int utils_randBetween (int min, int max) {
  return min + rand() % (max - min);
}

// NOTE: this function is undefined for x = 0 or x = 1
inline int utils_roundUpPowerOfTwo (unsigned int x) {
  return 1 << (1 + __builtin_clz(1) - __builtin_clz(x-1));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// https://stackoverflow.com/a/37109258
static const int b64Index[256] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0,  0,  0,  0,  0,  0,
  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,  0,  0,  0,  63,
  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
};

int utils_x25519Base64ToBuf (uint8_t *keyBuf, const char *keyStr) {
  if (check_base64_encoded_x25519_key(keyStr) == 0) return -1;

  int n, pos = 0;
  const unsigned char *s = (unsigned char *)keyStr;

  for (int i = 0; i < 40; i += 4) {
    n = b64Index[s[i]] << 18 |
      b64Index[s[i+1]] << 12 |
      b64Index[s[i+2]] << 6 |
      b64Index[s[i+3]];
    keyBuf[pos++] = n >> 16;
    keyBuf[pos++] = (n >> 8) & 0xff;
    keyBuf[pos++] = n & 0xff;
  }

  n = b64Index[s[40]] << 18 | b64Index[s[41]] << 12;
  keyBuf[pos++] = n >> 16;
  n |= b64Index[s[42]] << 6;
  keyBuf[pos++] = (n >> 8) & 0xff;

  return 0;
}

/*
* Base64 encoding/decoding (RFC1341)
* Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
*
* This software may be distributed under the terms of the BSD license.
* See README for more details.
*/

static const uint8_t base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * base64_decode - Base64 decode
 * @src: Data to be decoded
 * @len: Length of the data to be decoded
 * @out_len: Pointer to output length variable
 * Returns: Allocated buffer of out_len bytes of decoded data,
 * or %NULL on failure
 *
 * Caller is responsible for freeing the returned buffer.
 */
uint8_t *utils_base64Decode (const uint8_t *src, size_t len, size_t *out_len) {
  uint8_t dtable[256], *out, *pos, in[4], block[4], tmp;
  size_t i, count, olen;
  memset(dtable, 0x80, 256);
  for (i = 0; i < sizeof(base64_table) - 1; i++) {
    dtable[base64_table[i]] = (uint8_t) i;
  }

  dtable[(unsigned char)'='] = 0;
  count = 0;
  for (i = 0; i < len; i++) {
    if (dtable[src[i]] != 0x80)
      count++;
  }

  if (count == 0 || count % 4) {
    return NULL;
  }

  olen = count / 4 * 3;
  pos = out = (uint8_t*)malloc(olen);
  if (out == NULL) {
    return NULL;
  }

  count = 0;
  for (i = 0; i < len; i++) {
    tmp = dtable[src[i]];
    if (tmp == 0x80)
      continue;
    in[count] = src[i];
    block[count] = tmp;
    count++;
    if (count == 4) {
      *pos++ = (block[0] << 2) | (block[1] >> 4);
      *pos++ = (block[1] << 4) | (block[2] >> 2);
      *pos++ = (block[2] << 6) | block[3];
      count = 0;
    }
  }

  if (pos > out) {
    if (in[2] == '=') {
      pos -= 2;
    } else if (in[3] == '=') {
      pos--;
    }
  }

  *out_len = pos - out;
  return out;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// Poly: 0x8005
// https://stackoverflow.com/questions/10564491/function-to-calculate-a-crc16-checksum#comment83704063_10569892
uint16_t utils_crc16 (uint16_t crc, const uint8_t *buf, int bufLen) {
  while (bufLen--) {
    crc ^= *buf++;
    for (int k = 0; k < 8; k++) {
      crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
  }

  return crc;
}

// Poly: 0x04C11DB7
// https://web.mit.edu/freebsd/head/sys/libkern/crc32.c
static const uint32_t crc32Table[] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
  0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
  0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
  0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
  0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
  0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
  0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
  0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
  0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
  0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
  0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
  0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
  0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
  0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
  0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
  0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
  0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
  0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
  0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
  0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
  0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
  0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
  0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
  0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
  0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
  0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
  0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
  0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
  0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
  0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
  0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
  0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
  0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t utils_crc32 (uint32_t crc, const uint8_t *buf, int bufLen) {
  const uint8_t *p = buf;
  while (bufLen--) {
    crc = crc32Table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  }
  return crc ^ 0xffffffffu;
}
