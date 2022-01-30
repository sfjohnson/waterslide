#include <stdio.h>
#include "r8brain-free-src/CDSPResampler.h"
#include "globals.h"
#include "syncer.h"

using namespace r8b;

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static CDSPResampler24 **resamps;
static int networkChannelCount, deviceChannelCount;
static double levelFastAttack, levelFastRelease;
static double levelSlowAttack, levelSlowRelease;

static float *frameBuf;
static double **inBufsDouble;
static double **outBufsDouble;

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  try {
    _ring = ring;
    _ringBuf = ringBuf;
    _fullRingSize = fullRingSize;

    networkChannelCount = globals_get1i(audio, networkChannelCount);
    deviceChannelCount = globals_get1i(audio, deviceChannelCount);
    globals_get1ff(audio, levelFastAttack, &levelFastAttack);
    globals_get1ff(audio, levelFastRelease, &levelFastRelease);
    globals_get1ff(audio, levelSlowAttack, &levelSlowAttack);
    globals_get1ff(audio, levelSlowRelease, &levelSlowRelease);

    frameBuf = new float[networkChannelCount];
    resamps = new CDSPResampler24*[networkChannelCount];
    inBufsDouble = new double*[networkChannelCount];
    outBufsDouble = new double*[networkChannelCount];

    for (int i = 0; i < networkChannelCount; i++) {
      resamps[i] = new CDSPResampler24(srcRate, dstRate, maxInBufFrames);
      inBufsDouble[i] = new double[maxInBufFrames];
    }

  } catch (...) {
    return -1;
  }

  return 0;
}

static void setAudioStats (double sample, int channel) {
  if (sample >= 1.0 || sample <= -1.0) {
    globals_add1uiv(statsCh1Audio, clippingCounts, channel, 1);
  }

  double levelFast, levelSlow;
  globals_get1ffv(statsCh1Audio, levelsFast, channel, &levelFast);
  globals_get1ffv(statsCh1Audio, levelsSlow, channel, &levelSlow);

  double levelFastDiff = fabs(sample) - levelFast;
  double levelSlowDiff = fabs(sample) - levelSlow;

  if (levelFastDiff > 0) {
    levelFast += levelFastAttack * levelFastDiff;
  } else {
    levelFast += levelFastRelease * levelFastDiff;
  }
  if (levelSlowDiff > 0) {
    levelSlow += levelSlowAttack * levelSlowDiff;
  } else {
    levelSlow += levelSlowRelease * levelSlowDiff;
  }

  globals_set1ffv(statsCh1Audio, levelsFast, channel, levelFast);
  globals_set1ffv(statsCh1Audio, levelsSlow, channel, levelSlow);
}

// NOTE: this is not thread-safe
int syncer_enqueueBuf (const float *inBuf, int inFrameCount, int inChannelCount) {
  int lastOutBufLen = -1;
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      // If the inBuf has more channels than we want to send over the network, use the first n channels of the inBuf.
      inBufsDouble[i][j] = inBuf[inChannelCount*j + i];
    }
    int outBufLen = resamps[i]->process(inBufsDouble[i], inFrameCount, outBufsDouble[i]);
    if (lastOutBufLen != -1 && outBufLen != lastOutBufLen) {
      // DEBUG: outBufLens were not all the same. What should we do? This never happens!
      printf("syncer: resampler desync!\n");
      return -1;
    }
    lastOutBufLen = outBufLen;
  }

  if ((int)ck_ring_size(_ring) + networkChannelCount*lastOutBufLen > _fullRingSize) {
    globals_add1ui(statsCh1Audio, bufferOverrunCount, 1);
    return -2;
  }

  for (int i = 0; i < lastOutBufLen; i++) {
    for (int j = 0; j < networkChannelCount; j++) {
      // setAudioStats will detect clipping and tell the user
      setAudioStats(outBufsDouble[j][i], j);

      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      intptr_t outSample = 0;
      float floatSample = outBufsDouble[j][i];
      memcpy(&outSample, &floatSample, 4);
      ck_ring_enqueue_spsc(_ring, _ringBuf, (void*)outSample);
    }
  }

  return 0;
}

void syncer_deinit () {
  for (int i = 0; i < networkChannelCount; i++) {
    delete resamps[i];
    delete[] inBufsDouble[i];
  }

  delete[] resamps;
  delete[] frameBuf;
  delete[] outBufsDouble;
}
