#ifndef _UTILS_H
#define _UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#if defined(__linux__) || defined(__ANDROID__)
int utils_setCallerThreadRealtime (int priority, int core);
#elif defined(__APPLE__)
int utils_setCallerThreadPrioHigh (void);
#endif

// int utils_bindSocketToIf (int socket, const char *ifName, int ifLen, int port);

// NOTE: There is no length check, outBuf must be large enough!
int utils_slipEncode (const uint8_t *inBuf, int inBufLen, uint8_t *outBuf);

int utils_encodeVarintU64 (uint8_t *buf, int len, uint64_t val);
int utils_decodeVarintU64 (const uint8_t *buf, int len, uint64_t *result);
int utils_encodeVarintU16 (uint8_t *buf, int len, uint16_t val);
int utils_decodeVarintU16 (const uint8_t *buf, int len, uint16_t *result);

uint16_t utils_readU16LE (const uint8_t *buf);
int utils_writeU16LE (uint8_t *buf, uint16_t val);

// Call utils_setAudioLevelFilters before using utils_setAudioStats
void utils_setAudioLevelFilters (void);
void utils_setAudioStats (double sample, int channel);

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
