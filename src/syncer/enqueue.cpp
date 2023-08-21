#include <string.h>
#include "globals.h"
#include "utils.h"
#include "syncer.h"

enum InBufTypeEnum { S16, S24, S32, F32 };

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int networkChannelCount;
static double **inBufsDouble;
static int _fullRingSize;

/////////////////////
// private
/////////////////////

int _syncer_enqueueSamples (double **samples, int frameCount, bool setStats) {
  // Don't ever let the ring fill completely, that way the channels stay in order
  if ((int)utils_ringSize(_ring) + networkChannelCount*frameCount > _fullRingSize) return -1;

  for (int i = 0; i < frameCount; i++) {
    for (int j = 0; j < networkChannelCount; j++) {
      // NOTE: Sometimes the resampler pushes things a little bit outside of (-1.0, 1.0).
      // If that happens, it will show on the stats.
      if (setStats) utils_setAudioStats(samples[j][i], j);
      utils_ringEnqueueSample(_ring, _ringBuf, samples[j][i]);
    }
  }

  return frameCount;
}

static int syncer_enqueueBuf(enum InBufTypeEnum inBufType, const void *inBuf, int inFrameCount, int inChannelCount, bool setStats) {
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      // If the inBuf has more channels than we want to send over the network, use the first n channels of the inBuf.
      switch (inBufType) {
        case S16:
          inBufsDouble[i][j] = utils_s16ToDouble((const int16_t *)inBuf, inChannelCount*j + i);
          break;
        case S24:
          inBufsDouble[i][j] = utils_s24ToDouble((const uint8_t *)inBuf, inChannelCount*j + i);
          break;
        case S32:
          inBufsDouble[i][j] = utils_s32ToDouble((const int32_t *)inBuf, inChannelCount*j + i);
          break;
        case F32:
          inBufsDouble[i][j] = ((const float *)inBuf)[inChannelCount*j + i];
          break;
      }
    }
  }

  int outFrameCount = _syncer_stepResampState(inBufsDouble, inFrameCount, setStats, 0);
  if (outFrameCount < 0) return outFrameCount;

  return outFrameCount;
}

/////////////////////
// public
/////////////////////

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  try {
    _fullRingSize = fullRingSize;
    _ring = ring;
    _ringBuf = ringBuf;
    networkChannelCount = globals_get1i(audio, networkChannelCount);
    inBufsDouble = new double*[networkChannelCount];

    utils_setAudioLevelFilters();

    for (int i = 0; i < networkChannelCount; i++) {
      inBufsDouble[i] = new double[maxInBufFrames];
    }
  } catch (...) {
    return -1;
  }

  if (_syncer_initResampState(srcRate, dstRate, maxInBufFrames) < 0) return -1;
  return 0;
}

int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats) {
  return syncer_enqueueBuf(S16, inBuf, inFrameCount, inChannelCount, setStats);
}

int syncer_enqueueBufS24Packed (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats) {
  return syncer_enqueueBuf(S24, inBuf, inFrameCount, inChannelCount, setStats);
}

int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats) {
  return syncer_enqueueBuf(S32, inBuf, inFrameCount, inChannelCount, setStats);
}

int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats) {
  return syncer_enqueueBuf(F32, inBuf, inFrameCount, inChannelCount, setStats);
}

void syncer_deinit (void) {
  for (int i = 0; i < networkChannelCount; i++) {
    delete[] inBufsDouble[i];
  }
  delete[] inBufsDouble;

  _syncer_deinitResampState();
}
