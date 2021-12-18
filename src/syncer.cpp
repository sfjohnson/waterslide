#include <stdio.h>
#include "r8brain-free-src/CDSPResampler.h"
#include "globals.h"
#include "syncer.h"
#include "stats.h"

using namespace r8b;

static CDSPResampler16 **resamps;
static double *inBufDouble;
static int audioChannelCount;
static double levelFastAttack, levelFastRelease;
static double levelSlowAttack, levelSlowRelease;

static int16_t *frameBuf;
static double **outBufsDouble;
static int *outBufsDoubleLen;

int syncer_init (double srcRate, double dstRate, int maxInBufLen) {
  try {
    audioChannelCount = globals_get1i(audio, channelCount);
    globals_get1ff(audio, levelFastAttack, &levelFastAttack);
    globals_get1ff(audio, levelFastRelease, &levelFastRelease);
    globals_get1ff(audio, levelSlowAttack, &levelSlowAttack);
    globals_get1ff(audio, levelSlowRelease, &levelSlowRelease);

    resamps = new CDSPResampler16*[audioChannelCount];
    for (int i = 0; i < audioChannelCount; i++) {
      resamps[i] = new CDSPResampler16(srcRate, dstRate, maxInBufLen, 4.0);
    }

    inBufDouble = new double[maxInBufLen];

    frameBuf = new int16_t[audioChannelCount];
    outBufsDouble = new double*[audioChannelCount];
    outBufsDoubleLen = new int[audioChannelCount];
  } catch (...) {
    return -1;
  }

  return 0;
}

static void setAudioStats (double dSample, int iSample, int channel) {
  if (iSample > 32767 || iSample < -32768) {
    stats_ch1.audioClippingCount[channel]++;
  }

  double levelFast, levelSlow;
  // https://blog.regehr.org/archives/959
  memcpy(&levelFast, &stats_ch1.audioLevelsFast[channel], 8);
  memcpy(&levelSlow, &stats_ch1.audioLevelsSlow[channel], 8);

  double levelFastDiff = fabs(dSample) - levelFast;
  double levelSlowDiff = fabs(dSample) - levelSlow;

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

  memcpy(&stats_ch1.audioLevelsFast[channel], &levelFast, 8);
  memcpy(&stats_ch1.audioLevelsSlow[channel], &levelSlow, 8);
}

int syncer_enqueueBuf (const int16_t *inBuf, int inFrameCount, ck_ring_t *ring, ck_ring_buffer_t *ringBuf) {
  for (int i = 0; i < audioChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
      inBufDouble[j] = (double)inBuf[audioChannelCount*j + i] / 32768.0;
    }
    outBufsDoubleLen[i] = resamps[i]->process(inBufDouble, inFrameCount, outBufsDouble[i]);
  }

  // DEBUG: what do we do if outBufsDoubleLen aren't all the same?
  for (int i = 1; i < audioChannelCount; i++) {
    if (outBufsDoubleLen[i] != outBufsDoubleLen[0]) {
      // DEBUG: log
      printf("syncer: resampler desync\n");
      return -1;
    }
  }

  for (int i = 0; i < outBufsDoubleLen[0]; i++) {
    intptr_t outFrame = 0;

    for (int j = 0; j < audioChannelCount; j++) {
      int intSample = 32767.0 * outBufsDouble[j][i];
      // setAudioStats will detect 16-bit overflow (clipping) and tell the user
      setAudioStats(outBufsDouble[j][i], intSample, j);
      // Now clip to make sure the sample doesn't wrap around when converted to int16_t
      if (intSample > 32767) {
        intSample = 32767;
      } else if (intSample < -32768) {
        intSample = -32768;
      }

      // DEBUG
      // if (i > outBufsDoubleLen[0] - 10) {
      //   intSample = 0x7fff;
      // }

      frameBuf[j] = (int16_t)intSample;
    }

    // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
    // DEBUG: max 2 channels for 32-bit arch, max 4 channels for 64-bit
    memcpy(&outFrame, frameBuf, 2 * audioChannelCount);

    if (!ck_ring_enqueue_spsc(ring, ringBuf, (void*)outFrame)) {
      stats_ch1.ringOverrunCount++;
      return -2;
    }
  }

  return 0;
}

void syncer_deinit () {
  for (int i = 0; i < audioChannelCount; i++) {
    delete resamps[i];
  }

  delete[] resamps;
  delete[] inBufDouble;

  delete[] frameBuf;
  delete[] outBufsDouble;
  delete[] outBufsDoubleLen;
}
