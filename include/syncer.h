#ifndef _SYNCER_H
#define _SYNCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ck/ck_ring.h"

int syncer_init (double srcRate, double dstRate, int maxInBufLen);
int syncer_enqueueBuf (const int16_t *inBuf, int inFrameCount, ck_ring_t *ring, ck_ring_buffer_t *ringBuf);
void syncer_deinit ();

#ifdef __cplusplus
}
#endif

#endif
