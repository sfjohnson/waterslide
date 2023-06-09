#include <stdio.h>
#include <string.h>
#include "r8brain-free-src/CDSPResampler.h"
#include "globals.h"
#include "utils.h"
#include "syncer.h"

using namespace r8b;

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static CDSPResampler24 **resamps;
static int networkChannelCount, deviceChannelCount;

static double **inBufsDouble;
static double **outBufsDouble;

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  try {
    _ring = ring;
    _ringBuf = ringBuf;
    _fullRingSize = fullRingSize;

    utils_setAudioLevelFilters();

    networkChannelCount = globals_get1i(audio, networkChannelCount);
    deviceChannelCount = globals_get1i(audio, deviceChannelCount);

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

static void doTheFirstBit (int seq, int inFrameCount) {
  // receiverSync
  static int seqLast = -1;
  if (seqLast >= 0) {
    int seqDiff;
    if (seqLast - seq > 32768) {
      // Overflow
      seqDiff = 65536 - seqLast + seq;
    } else {
      seqDiff = seq - seqLast;
    }

    if (seqDiff > 1) {
      // Out-of-order, previous packet(s) were dropped.
      // We can assume every packet has inFrameCount frames.
      globals_add1i(audio, receiverSync, inFrameCount * (seqDiff - 1));
    }
  }
  seqLast = seq;
  globals_add1i(audio, receiverSync, inFrameCount);
}

// TODO: ok so i know this code structure looks bad and weird and it is but i've re-worked this code over and over again and I fear discussions on pointer aliasing and UB and it's getting late so sorry for the mess

static int doTheLastBit (int lastOutBufLen, bool setStats) {
  // Don't ever let the ring fill completely, that way the channels stay in order
  if ((int)ck_ring_size(_ring) + networkChannelCount*lastOutBufLen > _fullRingSize) {
    globals_add1ui(statsCh1Audio, bufferOverrunCount, 1);
    return -3;
  }

  for (int i = 0; i < lastOutBufLen; i++) {
    for (int j = 0; j < networkChannelCount; j++) {
      // TODO: The following trick won't work on the RPi Zero as it's 32-bit.
      // We need to do it the proper way and allocate memory for the samples
      // instead of using the ring pointers to represent the actual samples.
      intptr_t outSample = 0;
      // NOTE: Sometimes the resampler pushes things a little bit outside of (-1.0, 1.0).
      // If that happens, it will show on the stats.
      if (setStats) utils_setAudioStats(outBufsDouble[j][i], j);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      memcpy(&outSample, &outBufsDouble[j][i], 8);
      ck_ring_enqueue_spsc(_ring, _ringBuf, (void*)outSample);
    }
  }

  return lastOutBufLen;
}

// NOTES:
// - Don't look too close at this code please.
// - This should only be called from one thread (not thread-safe).
// - This must be audio callback safe (no syscalls).
// - seq must be negative for sender.
// returns: number of audio frames added to ring (after resampling), or negative error code

int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  doTheFirstBit(seq, inFrameCount);

  int lastOutBufLen = -1;
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      // If the inBuf has more channels than we want to send over the network, use the first n channels of the inBuf.
      double sample = inBuf[inChannelCount*j + i];
      // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
      inBufsDouble[i][j] = sample > 0.0 ? sample/32767.0 : sample/32768.0;
    }
    int outBufLen = resamps[i]->process(inBufsDouble[i], inFrameCount, outBufsDouble[i]);
    if (lastOutBufLen != -1 && outBufLen != lastOutBufLen) {
      // DEBUG: outBufLens were not all the same. What should we do? This never happens!
      printf("syncer: resampler desync!\n");
      return -2;
    }
    lastOutBufLen = outBufLen;
  }

  return doTheLastBit(lastOutBufLen, setStats);
}

int syncer_enqueueBufS24 (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  doTheFirstBit(seq, inFrameCount);

  int lastOutBufLen = -1;
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      int32_t sampleInt = 0;
      // Leave the least significant byte of sampleInt empty and then shift back into it to sign extend.
      memcpy((uint8_t *)&sampleInt + 1, inBuf + 3*(inChannelCount*j+i), 3);
      sampleInt >>= 8;
      inBufsDouble[i][j] = sampleInt > 0 ? sampleInt/8388607.0 : sampleInt/8388608.0;
    }
    int outBufLen = resamps[i]->process(inBufsDouble[i], inFrameCount, outBufsDouble[i]);
    if (lastOutBufLen != -1 && outBufLen != lastOutBufLen) {
      printf("syncer: resampler desync!\n");
      return -2;
    }
    lastOutBufLen = outBufLen;
  }

  return doTheLastBit(lastOutBufLen, setStats);
}

int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  doTheFirstBit(seq, inFrameCount);

  int lastOutBufLen = -1;
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      double sample = inBuf[inChannelCount*j + i];
      inBufsDouble[i][j] = sample > 0.0 ? sample/2147483647.0 : sample/2147483648.0;
    }
    int outBufLen = resamps[i]->process(inBufsDouble[i], inFrameCount, outBufsDouble[i]);
    if (lastOutBufLen != -1 && outBufLen != lastOutBufLen) {
      printf("syncer: resampler desync!\n");
      return -2;
    }
    lastOutBufLen = outBufLen;
  }

  return doTheLastBit(lastOutBufLen, setStats);
}

int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  doTheFirstBit(seq, inFrameCount);

  int lastOutBufLen = -1;
  for (int i = 0; i < networkChannelCount; i++) {
    for (int j = 0; j < inFrameCount; j++) {
      inBufsDouble[i][j] = inBuf[inChannelCount*j + i];
    }
    int outBufLen = resamps[i]->process(inBufsDouble[i], inFrameCount, outBufsDouble[i]);
    if (lastOutBufLen != -1 && outBufLen != lastOutBufLen) {
      printf("syncer: resampler desync!\n");
      return -2;
    }
    lastOutBufLen = outBufLen;
  }

  return doTheLastBit(lastOutBufLen, setStats);
}

void syncer_deinit (void) {
  for (int i = 0; i < networkChannelCount; i++) {
    delete resamps[i];
    delete[] inBufsDouble[i];
  }

  delete[] resamps;
  delete[] outBufsDouble;
}
