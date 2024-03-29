// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "xwait.h"
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <semaphore.h>
#include "tinyalsa/pcm.h"
#include "globals.h"
#include "utils.h"
#include "syncer.h"
#include "audio.h"

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static unsigned int _fullRingSize;
static bool _receiver;
static unsigned int bytesPerSample, networkChannelCount, deviceChannelCount, audioEncoding;
static pthread_t audioLoopThread;
static xwait_t audioLoopInitWait;
static atomic_int audioLoopStatus = 0;

// This is on the RT thread for receiver
static void dmaBufWrite (uint8_t *dmaBuf, unsigned int frameCount) {
  static bool ringUnderrun = true; // let ring fill to half before we start dequeuing
  unsigned int ringCurrentSize = utils_ringSize(_ring);

  memset(dmaBuf, 0, bytesPerSample * deviceChannelCount * frameCount);

  syncer_onAudio(frameCount);

  if (ringUnderrun) {
    // Let the ring fill up to about half-way before pulling from it again, while outputting silence.
    if (ringCurrentSize < _fullRingSize / 2) {
      return;
    } else {
      ringUnderrun = false;
    }
  }

  globals_add1uiv(statsCh1Audio, streamMeterBins, (STATS_STREAM_METER_BINS-1) * ringCurrentSize / _fullRingSize, 1);

  // Don't ever let the ring empty completely, that way the channels stay in order
  if (ringCurrentSize < networkChannelCount * frameCount) {
    ringUnderrun = true;
    globals_add1ui(statsCh1Audio, bufferUnderrunCount, 1);
    return;
  }

  for (unsigned int i = 0; i < frameCount; i++) {
    for (unsigned int j = 0; j < networkChannelCount; j++) {
      // If networkChannelCount > networkChannelCount, dequeue the sample then discard it.
      // If networkChannelCount < deviceChannelCount, don't write to the remaining channels in outBuf,
      // they are already set to zero above.
      // NOTE: audio-linux and audio-macos have different behaviour when deviceChannelCount < networkChannelCount:
      // - audio-linux: output the first deviceChannelCount channels and discard the rest
      // - audio-macos: don't proceed, return an error from audio_init
      double outSampleDouble = utils_ringDequeueSample(_ring, _ringBuf);
      if (j >= deviceChannelCount) continue;
      if (outSampleDouble < -1.0) outSampleDouble = -1.0;
      else if (outSampleDouble > 1.0) outSampleDouble = 1.0;
      // NOTE: Only bytesPerSample = 4 is implemented
      // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
      int32_t sampleInt = outSampleDouble > 0.0 ? 2147483647.0*outSampleDouble : 2147483648.0*outSampleDouble;
      memcpy(&dmaBuf[4 * (deviceChannelCount*i + j)], &sampleInt, 4);
      utils_setAudioStats(outSampleDouble, j);
    }
  }
}

// This is on the RT thread for sender
static void dmaBufRead (const uint8_t *dmaBuf, unsigned int frameCount) {
  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      if (bytesPerSample == 4) {
        syncer_enqueueBufS32((int32_t *)dmaBuf, frameCount, deviceChannelCount, true);
      } else { // bytesPerSample == 2
        syncer_enqueueBufS16((int16_t *)dmaBuf, frameCount, deviceChannelCount, true);
      }
      break;

    case AUDIO_ENCODING_PCM:
      for (unsigned int i = 0; i < frameCount; i++) {
        for (unsigned int j = 0; j < networkChannelCount; j++) {
          double inSampleDouble;

          // No clipping is required as we are converting from int to float
          // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
          if (bytesPerSample == 4) {
            int32_t sampleInt;
            memcpy(&sampleInt, &dmaBuf[4 * (deviceChannelCount*i + j)], 4);
            inSampleDouble = sampleInt > 0 ? sampleInt/2147483647.0 : sampleInt/2147483648.0;
          } else { // bytesPerSample == 2
            int16_t sampleInt;
            memcpy(&sampleInt, &dmaBuf[2 * (deviceChannelCount*i + j)], 2);
            inSampleDouble = sampleInt > 0 ? sampleInt/32767.0 : sampleInt/32768.0;
          }

          // DEBUG: I think we need to check for overrun here before enqueuing. We are doing it above for AUDIO_ENCODING_OPUS when we call syncer_enqueueBuf
          utils_ringEnqueueSample(_ring, _ringBuf, inSampleDouble);
          utils_setAudioStats(inSampleDouble, j);
        }
      }
      break;
  }
}

static inline void setAudioLoopStatus (int status) {
  audioLoopStatus = status;
  xwait_notify(&audioLoopInitWait);
}

static void *startAudioLoop (UNUSED void *arg) {
  unsigned int cardId, deviceId, periodSize, periodCount, networkSampleRate;
  double deviceSampleRate;
  globals_get1ff(audio, deviceSampleRate, &deviceSampleRate);
  cardId = globals_get1i(audio, cardId);
  deviceId = globals_get1i(audio, deviceId);
  periodSize = globals_get1i(audio, periodSize);
  periodCount = globals_get1i(audio, periodCount);
  networkSampleRate = globals_get1i(audio, networkSampleRate);

  enum pcm_format format;
  if (bytesPerSample == 4) {
    format = PCM_FORMAT_S32_LE;
  } else if (bytesPerSample == 2 && !_receiver) {
    format = PCM_FORMAT_S16_LE;
  } else {
    // NOTE: 16 bit (receiver) and 24 bit (receiver and sender) are not implemented
    setAudioLoopStatus(-1);
    return NULL;
  }

  struct pcm_config config = {
    .channels = deviceChannelCount,
    .rate = (int)deviceSampleRate,
    .format = format,
    .period_size = periodSize,
    .period_count = periodCount,
    .start_threshold = 1,
    .silence_threshold = 0,
    .silence_size = 0,
    .stop_threshold = 1000000000
  };

  printf("\n%s device: hw:%u,%u\n", _receiver ? "Output" : "Input", cardId, deviceId);
  printf("Device channels: %u\n", deviceChannelCount);
  printf("%s channels: %d\n", _receiver ? "Receiver" : "Sender", networkChannelCount);
  printf("Device latency (ms): %f\n", 1000.0 * audio_getDeviceLatency());
  printf("Device sample rate: %f\n", deviceSampleRate);

  unsigned int dmaBufLen = periodSize * periodCount;
  int err = 0;

  if (!_receiver && audioEncoding == AUDIO_ENCODING_OPUS) {
    int framesPerCallbackBuffer = dmaBufLen / 2;
    err = syncer_init(deviceSampleRate, networkSampleRate, framesPerCallbackBuffer, _ring, _ringBuf, _fullRingSize);
  } else if (_receiver) {
    int framesPerCallbackBuffer;
    if (audioEncoding == AUDIO_ENCODING_OPUS) {
      framesPerCallbackBuffer = globals_get1i(opus, frameSize);
    } else {
      framesPerCallbackBuffer = globals_get1i(pcm, frameSize);
    }
    err = syncer_init(networkSampleRate, deviceSampleRate, framesPerCallbackBuffer, _ring, _ringBuf, _fullRingSize);
  } else {
    // PCM sender uses deviceSampleRate, no syncer required.
  }
  if (err < 0) {
    setAudioLoopStatus(err - 1);
    return NULL;
  }

  unsigned int flags = (_receiver ? PCM_OUT : PCM_IN) | PCM_MMAP | PCM_NOIRQ;
  struct pcm *pcm = pcm_open(cardId, deviceId, flags, &config);
  if (pcm == NULL) {
    setAudioLoopStatus(-3);
    return NULL;
  }

  // Disable the automatic stop
  config.stop_threshold = pcm_get_boundary(pcm);
  // Close and open again to apply new stop_threshold
  pcm_close(pcm);
  pcm = pcm_open(cardId, deviceId, flags, &config);
  if (!pcm_is_ready(pcm)) {
    setAudioLoopStatus(-4);
    return NULL;
  }
  if (pcm_prepare(pcm) < 0) {
    setAudioLoopStatus(-5);
    return NULL;
  }

  uint8_t *dmaBuf;
  unsigned int unused1 = 0; // offset: this always gets set to zero
  unsigned int unused2 = 0; // frames: this gets set to periodSize * periodCount for PCM_OUT or 0 for PCM_IN
  if (pcm_mmap_begin(pcm, (void**)&dmaBuf, &unused1, &unused2) < 0) {
    setAudioLoopStatus(-6);
    return NULL;
  }
  if (_receiver) {
    memset(dmaBuf, 0, bytesPerSample * deviceChannelCount * dmaBufLen);
  }

  if (pcm_start(pcm) < 0) {
    setAudioLoopStatus(-7);
    return NULL;
  }

  err = utils_setCallerThreadRealtime(99, 0);
  if (err < 0) {
    setAudioLoopStatus(err - 7);
    return NULL;
  }

  struct timespec loopSleep, tsp; // tsp is unused
  loopSleep.tv_nsec = 1000 * globals_get1i(audio, loopSleep);
  loopSleep.tv_sec = 0;

  unsigned int hwPos = 0, lastHwPos = 0; // DEBUG: overflow at approx. 25 hours at 48 kHz on 32-bit arch
  bool lastBufHalf = false;

  // Wait a bit, otherwise pcm_mmap_get_hw_ptr will error out due to the timestamp being 0
  utils_usleep(50000);

  // successfully initialised, tell the main thread
  setAudioLoopStatus(1);
  while (audioLoopStatus == 1) {
    if (pcm_mmap_get_hw_ptr(pcm, &hwPos, &tsp) < 0) {
      pcm_close(pcm);
      audioLoopStatus = -10; // don't xwait_notify in the loop, the other thread is not waiting anymore
      return NULL;
    }

    bool bufHalf = (hwPos % dmaBufLen) >= (dmaBufLen / 2);
    if (bufHalf != lastBufHalf) {
      if (bufHalf && _receiver) {
        dmaBufWrite(dmaBuf, dmaBufLen / 2);
      } else if (bufHalf && !_receiver) {
        dmaBufRead(dmaBuf, dmaBufLen / 2);
      } else if (!bufHalf && _receiver) {
        dmaBufWrite(&dmaBuf[bytesPerSample * deviceChannelCount * dmaBufLen / 2], dmaBufLen / 2);
      } else { // !bufHalf && !_receiver
        dmaBufRead(&dmaBuf[bytesPerSample * deviceChannelCount * dmaBufLen / 2], dmaBufLen / 2);
      }
      lastBufHalf = bufHalf;
    }

    if (lastHwPos != 0 && hwPos - lastHwPos > periodSize * periodCount / 2) {
      // DEBUG: will this throw off receiver sync?
      globals_add1ui(statsCh1Audio, audioLoopXrunCount, 1);
    }
    lastHwPos = hwPos;

    clock_nanosleep(CLOCK_MONOTONIC, 0, &loopSleep, NULL);
  }

  pcm_close(pcm);
  return NULL;
}

int audio_init (bool receiver) {
  _receiver = receiver;
  bytesPerSample = globals_get1i(audio, bitsPerSample) / 8;
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  deviceChannelCount = globals_get1i(audio, deviceChannelCount);
  audioEncoding = globals_get1ui(audio, encoding);

  utils_setAudioLevelFilters();

  if (!receiver && deviceChannelCount < networkChannelCount) {
    printf("Device does not have enough output channels.\n");
    return -1;
  }

  return 0;
}

double audio_getDeviceLatency (void) {
  int periodSize = globals_get1i(audio, periodSize);
  int periodCount = globals_get1i(audio, periodCount);
  double deviceSampleRate;
  globals_get1ff(audio, deviceSampleRate, &deviceSampleRate);
  return (double)(periodSize * periodCount / 2) / deviceSampleRate;
}

int audio_start (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, unsigned int fullRingSize) {
  _ring = ring;
  _ringBuf = ringBuf;
  _fullRingSize = fullRingSize;

  xwait_init(&audioLoopInitWait);
  if (pthread_create(&audioLoopThread, NULL, startAudioLoop, NULL) != 0) return -2;

  // Wait for audioLoop to initialise
  xwait_wait(&audioLoopInitWait);
  if (audioLoopStatus < 0) {
    if (pthread_join(audioLoopThread, NULL) != 0) return -3;
    xwait_destroy(&audioLoopInitWait);
    return audioLoopStatus - 3;
  }

  return 0;
}

int audio_deinit (void) {
  if (audioLoopStatus != 0) {
    if (audioLoopStatus < 0) return audioLoopStatus; // audioLoopThread has already errored out
    audioLoopStatus = 0;
    pthread_join(audioLoopThread, NULL);
    xwait_destroy(&audioLoopInitWait);
  }

  return 0;
}
