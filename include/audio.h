#ifndef _AUDIO_H
#define _AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ck/ck_ring.h"

// call in this order:
// stats_init
// audio_init
// audio_getDeviceLatency
// audio_start
// syncer_enqueueBuf
// audio_deinit

int audio_init (bool receiver);
double audio_getDeviceLatency (void); // in seconds
int audio_start (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, unsigned int fullRingSize);
int audio_deinit (void);

#ifdef __cplusplus
}
#endif

#endif
