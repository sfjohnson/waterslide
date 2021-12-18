#include "circ.h"

//Reference: http://embedjournal.com/implementing-circular-buffer-embedded-c/

void circ_initS16 (circ_bufS16 *buf, int16_t *data, int dataLen) {
  buf->head = 0;
  buf->tail = 0;
  buf->len = dataLen;
  buf->data = data;
}

int circ_sizeS16 (const circ_bufS16 *buf) {
  int diff = buf->head - buf->tail;
  if (diff < 0) diff += buf->len;

  return diff;
}

bool circ_writeOneS16 (circ_bufS16 *buf, int16_t x) {
  int nextHead = buf->head + 1;
  if (nextHead == buf->len) nextHead = 0;

  if (nextHead == buf->tail) return false; // full

  buf->data[buf->head] = x;
  buf->head = nextHead;
  return true;
}

bool circ_readOneS16 (circ_bufS16 *buf, int16_t *x) {
  if (buf->head == buf->tail) return false; // empty

  int nextTail = buf->tail + 1;
  if (nextTail == buf->len) nextTail = 0;

  *x = buf->data[buf->tail];
  buf->tail = nextTail;
  return true;
}

bool circ_peekOneS16 (circ_bufS16 *buf, int16_t *x, int offset) {
  if (offset >= circ_sizeS16(buf)) return false;

  int newTail = buf->tail + offset;
  if (newTail >= buf->len) newTail -= buf->len;

  *x = buf->data[newTail];
  return true;
}
