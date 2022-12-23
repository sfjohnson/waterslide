#ifndef _UTILS_H
#define _UTILS_H

#include <stdint.h>

int utils_setCallerThreadPrioHigh ();

// int utils_bindSocketToIf (int socket, const char *ifName, int ifLen, int port);

// NOTE: There is no length check, outBuf must be large enough!
int utils_slipEncode (const uint8_t *inBuf, int inBufLen, uint8_t *outBuf);

int utils_encodeVarintU64 (uint8_t *buf, int len, uint64_t val);
int utils_decodeVarintU64 (const uint8_t *buf, int len, uint64_t *result);
int utils_encodeVarintU16 (uint8_t *buf, int len, uint16_t val);
int utils_decodeVarintU16 (const uint8_t *buf, int len, uint16_t *result);

uint16_t utils_readU16LE (const uint8_t *buf);
int utils_writeU16LE (uint8_t *buf, uint16_t val);

// NOTE: this function is undefined for x = 0 or x = 1
int utils_roundUpPowerOfTwo (unsigned int x);

// caller must allocate 32 bytes for keyBuf
// returns 0 for success or a negative error code
int utils_x25519Base64ToBuf (uint8_t *keyBuf, const char *keyStr);

uint32_t utils_crc32 (uint32_t crc, const uint8_t *buf, int bufLen);
uint16_t utils_crc16 (uint16_t crc, const uint8_t *buf, int bufLen);

#endif
