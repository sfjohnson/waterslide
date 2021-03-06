#ifndef _SYNCER_H
#define _SYNCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ck/ck_ring.h"

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize);

// NOTE: this is not thread-safe
int syncer_enqueueBuf (const float *inBuf, int inFrameCount, int inChannelCount);

void syncer_deinit ();

#ifdef __cplusplus
}
#endif

#endif
