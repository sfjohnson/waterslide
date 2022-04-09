#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>

int utils_setCallerThreadPrioHigh ();

int utils_encodeVarintU64 (uint8_t *buf, int len, uint64_t val);
int utils_decodeVarintU64 (const uint8_t *buf, int len, uint64_t *result);
int utils_encodeVarintU16 (uint8_t *buf, int len, uint16_t val);
int utils_decodeVarintU16 (const uint8_t *buf, int len, uint16_t *result);

uint16_t utils_readU16LE (const uint8_t *buf);
int utils_writeU16LE (uint8_t *buf, uint16_t val);

// NOTE: this function is undefined for x = 0 or x = 1
int utils_roundUpPowerOfTwo (unsigned int x);

uint32_t utils_crc32 (uint32_t crc, const uint8_t *buf, int bufLen);

#endif
