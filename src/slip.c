#include <stdbool.h>
#include "slip.h"

int slip_frameLength (const uint8_t *data, int dataLen) {
  int pos = 1;

  for (int i = 0; i < dataLen; i++) {
    if (data[i] == 0xc0 || data[i] == 0xdb) {
      pos += 2;
    } else {
      pos++;
    }
  }

  return pos;
}

int slip_frame (const uint8_t *src, int srcLen, uint8_t *dest) {
  int pos = 0;

  for (int i = 0; i < srcLen; i++) {
    switch (src[i]) {
      case 0xc0:
        dest[pos++] = 0xdb;
        dest[pos++] = 0xdc;
        break;

      case 0xdb:
        dest[pos++] = 0xdb;
        dest[pos++] = 0xdd;
        break;

      default:
        dest[pos++] = src[i];
    }
  }

  dest[pos++] = 0xc0;
  return pos;
}

int slip_deframe (const uint8_t *src, int srcLen, uint8_t *dest) {
  int pos = 0;
  bool esc = false;

  for (int i = 0; i < srcLen; i++) {
    if (esc) {
      if (src[i] == 0xdc) {
        dest[pos++] = 0xc0;
      } else if (src[i] == 0xdd) {
        dest[pos++] = 0xdb;
      }

      esc = false;
      continue;
    }

    if (src[i] == 0xdb) {
      esc = true;
    } else {
      dest[pos++] = src[i];
    }
  }

  return pos;
}
