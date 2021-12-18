#ifndef _CIRC_H
#define _CIRC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int head, tail, len;
  int16_t *data;
} circ_bufS16;

void circ_initS16 (circ_bufS16 *buf, int16_t *data, int dataLen);
int circ_sizeS16 (const circ_bufS16 *buf);
bool circ_writeOneS16 (circ_bufS16 *buf, int16_t x);
bool circ_readOneS16 (circ_bufS16 *buf, int16_t *x);
//Read byte at offset without removing from queue
bool circ_peekOneS16 (circ_bufS16 *buf, int16_t *x, int offset);

#endif
