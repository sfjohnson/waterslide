#ifndef _AUDIO_H
#define _AUDIO_H
#ifdef __cplusplus
extern "C" {
#endif

#include "ck/ck_ring.h"

// call these after stats_init()
int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize);
int audio_start (const char *audioDeviceName);

// NOTE: this should only be called from one thread (not thread-safe). This must be audio callback safe (no syscalls).
// returns: number of audio frames added to ring (after resampling), or negative error code
int audio_enqueueBuf (const float *inBuf, int inFrameCount, int inChannelCount);

#ifdef __cplusplus
}
#endif
#endif
