#include <atomic>
#include <stdio.h>
#include <string.h>
#include "xsem.h"
// TODO: fix anonymous structs in r8brain
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "r8brain-free-src/CDSPResampler.h"
#pragma GCC diagnostic pop
#include "globals.h"
#include "utils.h"
#include "syncer.h"

using namespace r8b;

enum InBufTypeEnum { S16, S24, S32, F32 };
enum ResampStateEnum { RunningA, FeedingB, MixingAtoB, RunningB, FeedingA, MixingBtoA, StoppingManager };

static std::atomic<ResampStateEnum> resampState = RunningA;
static int feedSampleCount = 0;
static double abMix = 0.0; // 0.0 means all FROM, 1.0 means all TO, changes at SYNCER_SWITCH_SPEED
static int abMixOverflowLen = 0;
static double **abMixOverflowBufs;
static CDSPResampler24 **resampsA, **resampsB;
static double resampsARatio, resampsBRatio;

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize, _maxInBufFrames;
static double _srcRate, _dstRate;
static pthread_t resampManagerThread;
static xsem_t resampManagerSem; // post this when resamp manager has work to do
static int networkChannelCount, deviceChannelCount;
static double **inBufsDouble, **tempBufsDouble;

// CDSPResampler24 construction and destruction can be expensive so it's done in this lower priority thread to not cause an audio glitch
static void *startResampManager (UNUSED void *arg) {
  while (true) {
    xsem_wait(&resampManagerSem);

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
          resampsBRatio = _srcRate / _dstRate;
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
          resampsARatio = _srcRate / _dstRate;
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

int syncer_init (double srcRate, double dstRate, int maxInBufFrames, ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  try {
    _ring = ring;
    _ringBuf = ringBuf;
    _fullRingSize = fullRingSize;
    _srcRate = srcRate;
    _dstRate = dstRate;
    _maxInBufFrames = maxInBufFrames;

    utils_setAudioLevelFilters();

    networkChannelCount = globals_get1i(audio, networkChannelCount);
    deviceChannelCount = globals_get1i(audio, deviceChannelCount);

    resampsA = new CDSPResampler24*[networkChannelCount];
    resampsB = new CDSPResampler24*[networkChannelCount];
    inBufsDouble = new double*[networkChannelCount];
    abMixOverflowBufs = new double*[networkChannelCount];
    tempBufsDouble = new double*[networkChannelCount];

    // DEBUG: sync test
    // srcRate negative offset makes receiverSync become less negative (increasing), and makes the ring increase in size
    // srcRate positive offset makes the ring become less full
    // srcRate -= 10.0;

    for (int i = 0; i < networkChannelCount; i++) {
      resampsA[i] = new CDSPResampler24(srcRate, dstRate, maxInBufFrames, SYNCER_TRANSITION_BAND);
      resampsB[i] = new CDSPResampler24(srcRate, dstRate, maxInBufFrames, SYNCER_TRANSITION_BAND);
      resampsARatio = resampsBRatio = srcRate / dstRate;
      inBufsDouble[i] = new double[maxInBufFrames];
      abMixOverflowBufs[i] = new double[SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES];
    }

    xsem_init(&resampManagerSem, 0);
    pthread_create(&resampManagerThread, NULL, startResampManager, NULL);
  } catch (...) {
    return -1;
  }

  return 0;
}

int syncer_changeRate (double srcRate) {
  if (resampState != RunningA && resampState != RunningB) return -1; // currently switching

  _srcRate = srcRate;
  xsem_post(&resampManagerSem);
  return 0;
}

static int enqueueSamples (double **samples, int frameCount, bool setStats) {
  // Don't ever let the ring fill completely, that way the channels stay in order
  if ((int)utils_ringSize(_ring) + networkChannelCount*frameCount > _fullRingSize) {
    globals_add1ui(statsCh1Audio, bufferOverrunCount, 1);
    return -1;
  }

  // DEBUG: test code for outputting resampState on audio channel 2
  // ResampStateEnum rs = resampState.load();
  // for (int i = 0; i < frameCount; i++) {
  //   for (int j = 0; j < networkChannelCount; j++) {
  //     double rsd = rs / 3.5 - 0.9;
  //     if (setStats) utils_setAudioStats(samples[j][i], j);
  //     if (j == 1) {
  //       utils_ringEnqueueSample(_ring, _ringBuf, rsd);
  //     } else {
  //       utils_ringEnqueueSample(_ring, _ringBuf, samples[j][i]);
  //     }
  //   }
  // }

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

  if (enqueueSamples(tempBufsDouble, outFrameCount, setStats) < 0) {
    abMixOverflowLen = 0;
    abMix = 0.0;
    return -5;
  }

  if (abMix >= 1.0) {
    if (!fromOverflows) { // if fromOverflows is true, discard contents of abMixOverflowBufs
      if (enqueueSamples(abMixOverflowBufs, abMixOverflowLen, setStats) < 0) {
        abMixOverflowLen = 0;
        abMix = 0.0;
        return -6;
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

  return enqueueSamples(tempBufsDouble, outFrameCount, setStats);
}

static int stepResampState (double **samples, int frameCount, bool setStats, int offset = 0) {
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
        return -7;
      }

      fromResamps = onA ? resampsA : resampsB;
      remainingSampleCount = feedResamp(samples, frameCount, fromResamps);
      if (remainingSampleCount == 0) {
        resampState = onA ? MixingBtoA : MixingAtoB;
      } else if (remainingSampleCount > 0) {
        resampState = onA ? MixingBtoA : MixingAtoB;
        // run mixResamps on the remaining samples that were not fed to resamps
        result = stepResampState(samples, frameCount, setStats, processSampleCount);
        if (result < 0) {
          resampState = onA ? RunningA : RunningB;
          return -8;
        }
        outFrameCount += result;
      }
      break;

    case MixingAtoB:
    case MixingBtoA:
      fromOverflows = onA ? resampsARatio < resampsBRatio : resampsBRatio < resampsARatio;
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
      return -9;
  }

  return outFrameCount;
}

// NOTES:
// - This should only be called from one thread (not thread-safe).
// - This must be audio callback safe (no syscalls).
// - seq must be negative for sender.
// returns: number of audio frames added to ring (after resampling), or negative error code

static int syncer_enqueueBuf(enum InBufTypeEnum inBufType, const void *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  // receiverSync
  static int seqLast = -1;

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

  int outFrameCount = stepResampState(inBufsDouble, inFrameCount, setStats);
  if (outFrameCount < 0) return outFrameCount;

  // receiverSync
  if (seqLast >= 0) {
    int seqDiff;
    if (seqLast - seq > 32768) {
      // Overflow
      seqDiff = 65536 - seqLast + seq;
    } else {
      seqDiff = seq - seqLast;
    }

    if (seqDiff >= 1) {
      // We can assume every packet has outFrameCount frames and fill in any skipped packets (skipped means seqDiff > 1)
      // DEBUG: does this assumption become incorrect enough to cause issues when we switch sample rate?
      globals_add1i(statsCh1Audio, receiverSync, outFrameCount * seqDiff);
    }
  }
  seqLast = seq;

  return outFrameCount;
}

int syncer_enqueueBufS16 (const int16_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  return syncer_enqueueBuf(S16, inBuf, inFrameCount, inChannelCount, setStats, seq);
}

int syncer_enqueueBufS24Packed (const uint8_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  return syncer_enqueueBuf(S24, inBuf, inFrameCount, inChannelCount, setStats, seq);
}

int syncer_enqueueBufS32 (const int32_t *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  return syncer_enqueueBuf(S32, inBuf, inFrameCount, inChannelCount, setStats, seq);
}

int syncer_enqueueBufF32 (const float *inBuf, int inFrameCount, int inChannelCount, bool setStats, int seq) {
  return syncer_enqueueBuf(F32, inBuf, inFrameCount, inChannelCount, setStats, seq);
}

void syncer_deinit (void) {
  resampState = StoppingManager;
  xsem_post(&resampManagerSem);
  pthread_join(resampManagerThread, NULL);
  xsem_destroy(&resampManagerSem);

  for (int i = 0; i < networkChannelCount; i++) {
    delete[] inBufsDouble[i];
    delete[] abMixOverflowBufs[i];
  }

  delete[] resampsA;
  delete[] resampsB;
  delete[] inBufsDouble;
  delete[] tempBufsDouble;
  delete[] abMixOverflowBufs;
}
