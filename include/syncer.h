#ifndef _SYNCER_H
#define _SYNCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ck/ck_ring.h"

/////////////////////
// private
/////////////////////

int _syncer_initResampState (double srcRate, double dstRate, int maxInBufFrames);
int _syncer_enqueueSamples (double **samples, int frameCount, bool setStats);
int _syncer_stepResampState (double **samples, int frameCount, bool setStats, int offset);
void _syncer_deinitResampState (void);

/////////////////////
// public
/////////////////////

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize);

// NOTES:
// - This returns the new ratio when the rate change has completed, not immediately after calling syncer_changeRate
// - This is thread-safe, happy days!
double syncer_getRateRatio (void);

void syncer_onAudio (unsigned int frameCount);
void syncer_onPacket (int seq, int audioFrameSize);

// NOTES:
// - This function is thread-safe (call it from anywhere).
// - This function is asynchronous and returns immediately but the actual rate change takes a while to complete.
// - The rate change could take > 100 ms due to constructing a new resampler which is expensive.
// - Changing the srcRate will cause a smooth blip on the waveform lasting a few ms, but it should not cause a click. I'm not sure if it's very audible.
// - Try to keep each rate change within +/- 10 Hz.
// - Decreasing srcRate = receiverSync slopes up (increasing) = ring more full
// - Increasing srcRate = receiverSync slopes down (decreasing) = ring less full
int syncer_changeRate (double srcRate);

// NOTES:
// - This should only be called from one thread (not thread-safe).
// - This is audio callback safe (no syscalls).
// - If setStats is true, set the monitor audio levels after resampling
// returns: number of audio frames enqueued onto ring (after resampling), or negative error code
// errors:
// -1: ring buffer overrun, no frames were enqueued onto ring
// -2: mixResamps: The resampler with more output samples was not the one we expected. This is very bad!
// -3: mixResamps: overflow in abMixOverflowLen, rate change was too much (fromOverflows)
// -4: mixResamps: overflow in abMixOverflowLen, rate change was too much (toOverflows)
// -5: stepResampState was called while in StoppingManager state. This is very bad!
int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats); // for Android
int syncer_enqueueBufS24Packed (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats); // for PCM
int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats); // for Android
int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats); // for Opus and macOS

void syncer_deinit (void);

#ifdef __cplusplus
}
#endif

#endif
