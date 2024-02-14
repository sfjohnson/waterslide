// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _UTILS_H
#define _UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ck/ck_ring.h"

// wrapper functions for ck_ring that work on both 32-bit and 64-bit architectures
// size is the number of doubles ring can hold
int utils_ringInit (ck_ring_t *ring, ck_ring_buffer_t **ringBuf, int size);
unsigned int utils_ringSize (const ck_ring_t *ring);
// NOTE: enqueue and dequeue have no bounds checking, call ringSize first!
double utils_ringDequeueSample (ck_ring_t *ring, ck_ring_buffer_t *ringBuf);
void utils_ringEnqueueSample (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, double x);
void utils_ringDeinit (ck_ring_t *ring, ck_ring_buffer_t *ringBuf);

// NOTE: us must be < 1000000 (1 second)
void utils_usleep (unsigned int us);
// return value is between 0 and 999_999_999 and will roll back to zero every 1000 seconds
int utils_getCurrentUTime (void);
// return value is in microseconds, intervals of > 500_000_000 us may return an incorrect value
int utils_getElapsedUTime (int lastUTime);

int utils_setCallerThreadRealtime (int priority, int core);

uint16_t utils_readU16LE (const uint8_t *buf);
int utils_writeU16LE (uint8_t *buf, uint16_t val);

// call utils_setAudioLevelFilters before using utils_setAudioStats
void utils_setAudioLevelFilters (void);
void utils_setAudioStats (double sample, int channel);

double utils_s16ToDouble (const int16_t *inBuf, int index);
// 24-bit index, i.e. 1 unit in index equals 3 bytes in inBuf
double utils_s24ToDouble (const uint8_t *inBuf, int index);
double utils_s32ToDouble (const int32_t *inBuf, int index);

// min is inclusive, max is not inclusive
// call srand() first
int utils_randBetween (int min, int max);

// NOTE: this function is undefined for x = 0 or x = 1
int utils_roundUpPowerOfTwo (unsigned int x);

// caller must allocate 32 bytes for keyBuf
// returns 0 for success or a negative error code
int utils_x25519Base64ToBuf (uint8_t *keyBuf, const char *keyStr);

uint32_t utils_crc32 (uint32_t crc, const uint8_t *buf, int bufLen);
uint16_t utils_crc16 (uint16_t crc, const uint8_t *buf, int bufLen);

#ifdef __cplusplus
}
#endif

#endif
