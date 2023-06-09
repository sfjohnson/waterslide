#ifndef _SYNCER_H
#define _SYNCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ck/ck_ring.h"

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize);

// NOTES:
// - This should only be called from one thread (not thread-safe).
// - This must be audio callback safe (no syscalls).
// - seq must be negative for sender.
// returns: number of audio frames added to ring (after resampling), or negative error code
// If setStats is true, set the monitor audio levels after resampling
int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Android
int syncer_enqueueBufS24 (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for PCM
int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Android
int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Opus and macOS

void syncer_deinit (void);

#ifdef __cplusplus
}
#endif

#endif
