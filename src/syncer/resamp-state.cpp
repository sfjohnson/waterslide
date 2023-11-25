// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <atomic>
// TODO: fix anonymous structs in r8brain
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "r8brain-free-src/CDSPResampler.h"
#pragma GCC diagnostic pop
#include "globals.h"
#include "syncer.h"

using namespace r8b;

enum ResampStateEnum { RunningA, FeedingB, MixingAtoB, RunningB, FeedingA, MixingBtoA, StoppingManager };

static std::atomic<ResampStateEnum> resampState = RunningA;
static std::atomic_flag managerFlag = ATOMIC_FLAG_INIT; // false
static int feedSampleCount = 0;
static double abMix = 0.0; // 0.0 means all FROM, 1.0 means all TO, changes at SYNCER_SWITCH_SPEED
static int abMixOverflowLen = 0;
static double **abMixOverflowBufs;
static CDSPResampler24 **resampsA, **resampsB;
static double resampsARatio, resampsBRatio;

static int _maxInBufFrames;
static double _srcRate, _dstRate;
static pthread_t resampManagerThread;
static int networkChannelCount, deviceChannelCount;
static double **tempBufsDouble;

/////////////////////
// private
/////////////////////

// CDSPResampler24 construction and destruction can be expensive so it's done in this lower priority thread to not cause an audio glitch
static void *startResampManager (UNUSED void *arg) {
  while (true) {
    managerFlag.wait(false);
    managerFlag.clear();

    switch (resampState) {
      case StoppingManager:
        for (int i = 0; i < networkChannelCount; i++) {
          delete resampsA[i];
          delete resampsB[i];
        }
        return NULL;

      case RunningA:
        for (int i = 0; i < networkChannelCount; i++) {
          delete resampsB[i];
          resampsB[i] = new CDSPResampler24(_srcRate, _dstRate, _maxInBufFrames, SYNCER_TRANSITION_BAND);
          resampsBRatio = _dstRate / _srcRate;
          if (i == 0) {
            feedSampleCount = resampsB[i]->getInputRequiredForOutput(1);
          } else if (resampsB[i]->getInputRequiredForOutput(1) != feedSampleCount) {
            // DEBUG: feedSampleCounts were not all the same. What should we do? This never happens!
            printf("syncer: resampler desync!\n");
          }
        }
        resampState = FeedingB;
        break;

      case RunningB:
        for (int i = 0; i < networkChannelCount; i++) {
          delete resampsA[i];
          resampsA[i] = new CDSPResampler24(_srcRate, _dstRate, _maxInBufFrames, SYNCER_TRANSITION_BAND);
          resampsARatio = _dstRate / _srcRate;
          if (i == 0) {
            feedSampleCount = resampsA[i]->getInputRequiredForOutput(1);
          } else if (resampsA[i]->getInputRequiredForOutput(1) != feedSampleCount) {
            // DEBUG: feedSampleCounts were not all the same. What should we do? This never happens!
            printf("syncer: resampler desync!\n");
          }
        }
        resampState = FeedingA;
        break;

      default:
        break; // should never be here
    }
  }

  return NULL;
}

int _syncer_initResampState (double srcRate, double dstRate, int maxInBufFrames) {
  try {
    _srcRate = srcRate;
    _dstRate = dstRate;
    _maxInBufFrames = maxInBufFrames;

    networkChannelCount = globals_get1i(audio, networkChannelCount);
    deviceChannelCount = globals_get1i(audio, deviceChannelCount);

    resampsA = new CDSPResampler24*[networkChannelCount];
    resampsB = new CDSPResampler24*[networkChannelCount];
    abMixOverflowBufs = new double*[networkChannelCount];
    tempBufsDouble = new double*[networkChannelCount];

    for (int i = 0; i < networkChannelCount; i++) {
      resampsA[i] = new CDSPResampler24(srcRate, dstRate, maxInBufFrames, SYNCER_TRANSITION_BAND);
      resampsB[i] = new CDSPResampler24(srcRate, dstRate, maxInBufFrames, SYNCER_TRANSITION_BAND);
      resampsARatio = resampsBRatio = dstRate / srcRate;
      abMixOverflowBufs[i] = new double[SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES];
    }

    pthread_create(&resampManagerThread, NULL, startResampManager, NULL);
  } catch (...) {
    return -1;
  }

  return 0;
}

static int mixResamps (double **samples, int inFrameCount, int offset, CDSPResampler24 **fromResamps, CDSPResampler24 **toResamps, bool fromOverflows, bool setStats) {
  double abMixSaved = abMix;
  int abMixOverflowLenSaved = abMixOverflowLen;
  int outFrameCount = 0;
  int returnErr = 0;

  for (int i = 0; i < networkChannelCount; i++) {
    double *fromOutBuf, *toOutBuf;
    int fromOutLen = fromResamps[i]->process(samples[i] + offset, inFrameCount - offset, fromOutBuf);
    int toOutLen = toResamps[i]->process(samples[i] + offset, inFrameCount - offset, toOutBuf);

    if (returnErr < 0) continue;

    if ((fromOverflows && toOutLen > fromOutLen) || (!fromOverflows && fromOutLen > toOutLen)) {
      // DEBUG: The resampler with more output samples was not the one we expected. This never happens!
      returnErr = -2;
      printf("syncer: rate change failed in mixResamps!\n");
      continue;
    }

    // NOTE: I'm allowed to modify toOutBuf after process() because I'm special, see here: https://github.com/avaneev/r8brain-free-src/issues/17#issuecomment-1621159016
    int outBufPos = 0;
    if (fromOverflows) { // abMixOverflowBufs contains samples from fromResamps
      // mix all of abMixOverflowBufs, then fromOutBuf with toOutBuf
      for (int j = 0; j < toOutLen; j++) {
        double fromSample = abMixOverflowLen > 0 ? abMixOverflowBufs[i][--abMixOverflowLen] : fromOutBuf[outBufPos++];
        double toSample = toOutBuf[j];
        toOutBuf[j] = (1.0-abMix)*fromSample + abMix*toSample;
        abMix += SYNCER_SWITCH_SPEED;
        if (abMix > 1.0) abMix = 1.0;
      }
      // add any remaning samples in fromOutBuf to abMixOverflowBufs
      for (int j = outBufPos; j < fromOutLen; j++) {
        if (abMixOverflowLen == SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES) {
          returnErr = -3;
          // DEBUG: log
          printf("syncer: overflow in mixResamps, rate change was too much\n");
          continue;
        }
        abMixOverflowBufs[i][abMixOverflowLen++] = fromOutBuf[j];
      }
      // Provide the mixed samples to be enqueued onto the ring. The pointer returned by process() above will remain alive until the next call to process() and can be used outside this function.
      tempBufsDouble[i] = toOutBuf;
      outFrameCount = toOutLen;
    } else { // abMixOverflowBufs contains samples from toResamps
      for (int j = 0; j < fromOutLen; j++) {
        double fromSample = fromOutBuf[j];
        double toSample = abMixOverflowLen > 0 ? abMixOverflowBufs[i][--abMixOverflowLen] : toOutBuf[outBufPos++];
        fromOutBuf[j] = (1.0-abMix)*fromSample + abMix*toSample;
        abMix += SYNCER_SWITCH_SPEED;
        if (abMix > 1.0) abMix = 1.0;
      }
      for (int j = outBufPos; j < toOutLen; j++) {
        if (abMixOverflowLen == SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES) {
          returnErr = -4;
          // DEBUG: log
          printf("syncer: overflow in mixResamps, rate change was too much\n");
          continue;
        }
        abMixOverflowBufs[i][abMixOverflowLen++] = toOutBuf[j];
      }
      tempBufsDouble[i] = fromOutBuf;
      outFrameCount = fromOutLen;
    }

    if (i < networkChannelCount - 1) {
      abMixOverflowLen = abMixOverflowLenSaved;
      abMix = abMixSaved;
    }
  }

  // Wait until we are out of the loop before we error out, this way every resampler gets fed exactly the same, even if there is an error.
  if (returnErr < 0) {
    abMixOverflowLen = 0;
    abMix = 0.0;
    return returnErr;
  }

  returnErr = _syncer_enqueueSamples(tempBufsDouble, outFrameCount, setStats);
  if (returnErr < 0) {
    abMixOverflowLen = 0;
    abMix = 0.0;
    return returnErr;
  }

  if (abMix >= 1.0) {
    if (!fromOverflows) { // if fromOverflows is true, discard contents of abMixOverflowBufs
      returnErr = _syncer_enqueueSamples(abMixOverflowBufs, abMixOverflowLen, setStats);
      if (returnErr < 0) {
        abMixOverflowLen = 0;
        abMix = 0.0;
        return returnErr;
      }
      outFrameCount += abMixOverflowLen;
    }
    abMixOverflowLen = 0;
  }

  return outFrameCount;
}

// If feedSampleCount is < 0 after feedResamp is complete we need to run mixResamps on the remaining frames.
static int feedResamp (double **samples, int frameCount, CDSPResampler24 **resampsToFeed) {
  double *discard;
  int processSampleCount = frameCount < feedSampleCount ? frameCount : feedSampleCount;
  for (int i = 0; i < networkChannelCount; i++) {
    resampsToFeed[i]->process(samples[i], processSampleCount, discard);
  }
  // discard should now point to an empty buffer here
  feedSampleCount -= frameCount;
  return -feedSampleCount;
}

static int processResamp (double **samples, int frameCount, CDSPResampler24 **resamps, bool setStats) {
  // NOTES:
  // - after process() is called, outNodes[outNodesIndex].bufs[i] is set to memory owned by the resampler
  // - don't call process() twice on the same CDSPResampler24 object without reading the output first, otherwise the second call to process() might clobber the output from the first
  // - each CDSPResampler24 object when constructed with the same arguments and fed the same samples always returns the same value to tempBufsDouble[i]
  int outFrameCount = 0;
  for (int i = 0; i < networkChannelCount; i++) {
    outFrameCount = resamps[i]->process(samples[i], frameCount, tempBufsDouble[i]);
  }

  return _syncer_enqueueSamples(tempBufsDouble, outFrameCount, setStats);
}

int _syncer_stepResampState (double **samples, int frameCount, bool setStats, int offset) {
  int outFrameCount = 0, result;
  CDSPResampler24 **fromResamps, **toResamps;
  ResampStateEnum rs = resampState.load();
  bool onA = rs == RunningA || rs == FeedingA || rs == MixingAtoB;
  bool fromOverflows;
  int processSampleCount, remainingSampleCount;

  switch (rs) {
    case RunningA:
    case RunningB:
      return processResamp(samples, frameCount, onA ? resampsA : resampsB, setStats);

    case FeedingA:
    case FeedingB:
      processSampleCount = frameCount < feedSampleCount ? frameCount : feedSampleCount;
      fromResamps = onA ? resampsB : resampsA;
      outFrameCount = processResamp(samples, processSampleCount, fromResamps, setStats);
      if (outFrameCount < 0) {
        // ring is full, don't bother with smooth rate change
        resampState = onA ? RunningA : RunningB;
        return outFrameCount;
      }

      fromResamps = onA ? resampsA : resampsB;
      remainingSampleCount = feedResamp(samples, frameCount, fromResamps);
      if (remainingSampleCount == 0) {
        resampState = onA ? MixingBtoA : MixingAtoB;
      } else if (remainingSampleCount > 0) {
        resampState = onA ? MixingBtoA : MixingAtoB;
        // run mixResamps on the remaining samples that were not fed to resamps
        result = _syncer_stepResampState(samples, frameCount, setStats, processSampleCount);
        if (result < 0) {
          resampState = onA ? RunningA : RunningB;
          return result;
        }
        outFrameCount += result;
      }
      break;

    case MixingAtoB:
    case MixingBtoA:
      fromOverflows = onA ? resampsARatio > resampsBRatio : resampsBRatio > resampsARatio;
      fromResamps = onA ? resampsA : resampsB;
      toResamps = onA ? resampsB : resampsA;
      outFrameCount = mixResamps(samples, frameCount, offset, fromResamps, toResamps, fromOverflows, setStats);
      if (outFrameCount < 0) {
        // mixResamps failed, let's continue anyways
        resampState = onA ? RunningB : RunningA;
        return outFrameCount;
      }
      if (abMix >= 1.0) {
        // mixResamps completed successfully
        abMix = 0.0;
        resampState = onA ? RunningB : RunningA;
      }
      break;

    case StoppingManager:
      // we should not be here!
      return -5;
  }

  return outFrameCount;
}

void _syncer_deinitResampState (void) {
  resampState = StoppingManager;
  managerFlag.test_and_set();
  managerFlag.notify_one();
  pthread_join(resampManagerThread, NULL);

  for (int i = 0; i < networkChannelCount; i++) {
    delete[] abMixOverflowBufs[i];
  }

  delete[] resampsA;
  delete[] resampsB;
  delete[] tempBufsDouble;
  delete[] abMixOverflowBufs;
}

/////////////////////
// public
/////////////////////

// NOTE: this is thread-safe because if the manager thread is modifying
// resamps(A|B)Ratio, it will be the opposite to the one we are reading here,
// protected by resampState which is atomic.
double syncer_getRateRatio (void) {
  switch (resampState) {
    case RunningA:
    case FeedingB:
    case MixingAtoB:
      return resampsARatio;
    case RunningB:
    case FeedingA:
    case MixingBtoA:
      return resampsBRatio;
    case StoppingManager:
      return 1.0; // should not be calling this after deinit, just return whatever
  }
  return 1.0; // never gonna be here, return whatever to make g++ happy
}

int syncer_changeRate (double srcRate) {
  if (resampState != RunningA && resampState != RunningB) return -1; // currently switching

  _srcRate = srcRate;
  managerFlag.test_and_set();
  managerFlag.notify_one();
  return 0;
}
