#ifndef _SYNCER_H
#define _SYNCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ck/ck_ring.h"

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize);

// NOTES:
// - This function is thread-safe (call it from anywhere).
// - This function is asynchronous and returns immediately but the actual rate change takes a while to complete.
// - The rate change could take > 100 ms due to constructing a new resampler which is expensive.
// - Changing the srcRate will cause a smooth blip on the waveform lasting a few ms, but it should not cause a click. I'm not sure if it's very audible.
// - Try to keep each rate change within +/- 10 Hz.
// - Decreasing srcRate makes receiverSync become less negative (increasing), and makes the ring increase in size.
// - Increasing srcRate makes the ring become less full.
int syncer_changeRate (double srcRate);

// NOTES:
// - This should only be called from one thread (not thread-safe).
// - This must be audio callback safe (no syscalls).
// - seq must be negative for sender.
// returns: number of audio frames added to ring (after resampling), or negative error code
// If setStats is true, set the monitor audio levels after resampling
int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Android
int syncer_enqueueBufS24Packed (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for PCM
int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Android
int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq); // for Opus and macOS

void syncer_deinit (void);

#ifdef __cplusplus
}
#endif

#endif
