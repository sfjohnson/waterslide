#include <pthread.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include "boringtun/wireguard_ffi.h"
#include "utils.h"

#if defined(__MACH__)
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#endif

#define UNUSED __attribute__((unused))

int utils_setCallerThreadPrioHigh () {
  // DEBUG: implement for other platforms
  #if defined(__MACH__)
  // https://stackoverflow.com/a/44310370
  mach_timebase_info_data_t timebase;
  kern_return_t kr = mach_timebase_info(&timebase);
  if (kr != KERN_SUCCESS) return -1;

  // Set the thread priority.
  struct thread_time_constraint_policy ttcpolicy;
  thread_port_t threadport = pthread_mach_thread_np(pthread_self());

  // In ticks. Therefore to convert nanoseconds to ticks multiply by (timebase.denom / timebase.numer).
  ttcpolicy.period = 500 * 1000 * timebase.denom / timebase.numer; // Period over which we demand scheduling.
  ttcpolicy.computation = 100 * 1000 * timebase.denom / timebase.numer; // Minimum time in a period where we must be running.
  ttcpolicy.constraint = 100 * 1000 * timebase.denom / timebase.numer; // Maximum time between start and end of our computation in the period.
  ttcpolicy.preemptible = FALSE;

  kr = thread_policy_set(threadport, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&ttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (kr != KERN_SUCCESS) return -2;
  #endif

  return 0;
}

int utils_slipEncode (const uint8_t *inBuf, int inBufLen, uint8_t *outBuf) {
  int outPos = 0;
  for (int i = 0; i < inBufLen; i++) {
    switch (inBuf[i]) {
      case 0xc0:
        outBuf[outPos++] = 0xdb;
        outBuf[outPos++] = 0xdc;
        break;
      case 0xdb:
        outBuf[outPos++] = 0xdb;
        outBuf[outPos++] = 0xdd;
        break;
      default:
        outBuf[outPos++] = inBuf[i];
    }
  }

  return outPos;
}

int utils_encodeVarintU64 (uint8_t *buf, int len, uint64_t val) {
  int pos = 0;

  do {
    if (pos >= len) return -1;
    buf[pos] = (val & 0x7f) | 0x80;
    val >>= 7;
    pos++;
  } while (val > 0);

  buf[pos-1] &= 0x7f;
  return pos;
}

int utils_decodeVarintU64 (const uint8_t *buf, int len, uint64_t *result) {
  int bytePos = 0;
  int bitPos = 0;
  uint64_t dest = 0;

  do {
    if (bitPos >= 64) return -1;
    if (bytePos >= len) return -2;
    dest |= (uint64_t)(buf[bytePos] & 0x7f) << bitPos;
    bitPos += 7;
    bytePos++;
  } while (buf[bytePos-1] & 0x80);

  *result = dest;
  return bytePos;
}

int utils_encodeVarintU16 (uint8_t *buf, int len, uint16_t val) {
  int pos = 0;

  do {
    if (pos >= len) return -1;
    buf[pos] = (val & 0x7f) | 0x80;
    val >>= 7;
    pos++;
  } while (val > 0);

  buf[pos-1] &= 0x7f;
  return pos;
}

int utils_decodeVarintU16 (const uint8_t *buf, int len, uint16_t *result) {
  int bytePos = 0;
  int bitPos = 0;
  uint16_t dest = 0;

  do {
    if (bitPos >= 16) return -1;
    if (bytePos >= len) return -2;
    dest |= (uint16_t)(buf[bytePos] & 0x7f) << bitPos;
    bitPos += 7;
    bytePos++;
  } while (buf[bytePos-1] & 0x80);

  *result = dest;
  return bytePos;
}

uint16_t utils_readU16LE (const uint8_t *buf) {
  return ((uint16_t)buf[1] << 8) | buf[0];
}

int utils_writeU16LE (uint8_t *buf, uint16_t val) {
  buf[0] = val & 0xff;
  buf[1] = val >> 8;
  return 2;
}

// NOTE: this function is undefined for x = 0 or x = 1
int utils_roundUpPowerOfTwo (unsigned int x) {
  return 1 << (1 + __builtin_clz(1) - __builtin_clz(x-1));
}

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
