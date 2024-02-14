// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <pthread.h>
#include <atomic>
#include <math.h>
#include "globals.h"
#include "syncer.h"

static double _srcRate = 0.0;
static pthread_t receiverSyncThread;
static atomic_int threadState;
static atomic_bool active = false;
static atomic_int_fast64_t receiverSync = 0;

/////////////////////
// private
/////////////////////

// NOTE: not bounds checked, make sure length >= 2 and tail < length
static double linearRegressionSlope (const double *data, int tail, int length) {
  double yAvg = 0.0;
  for (int i = 0; i < length; i++) {
    yAvg += data[tail];
    if (++tail == length) tail = 0;
  }
  yAvg /= (double)length;

  double xAvg = (length - 1) / 2.0;
  double variance = 0.0, covariance = 0.0;
  for (int i = 0; i < length; i++) {
    double xDiff = i - xAvg;
    variance += xDiff * xDiff;
    covariance += xDiff * (data[tail] - yAvg);
    if (++tail == length) tail = 0;
  }

  if (variance == 0.0) return 0.0; // division by zero check, not sure if it's required
  return covariance / variance;
}

// NOTE: not bounds checked, make sure length >= 2 and tail < length
static double variance (const double *data, int tail, int length) {
  double avg = 0.0;
  for (int i = 0; i < length; i++) {
    avg += data[tail];
    if (++tail == length) tail = 0;
  }
  avg /= (double)length;

  double squaresSum = 0.0;
  for (int i = 0; i < length; i++) {
    double diff = data[tail] - avg;
    squaresSum += diff * diff;
    if (++tail == length) tail = 0;
  }
  return squaresSum / (double)(length - 1);
}

// this is not a realtime thread
static void *startReceiverSync (UNUSED void *arg) {
  double rsHistory[RS_HISTORY_LENGTH] = { 0.0 }; // ring buffer
  double errorWindow[RS_ERROR_VARIANCE_WINDOW] = { 0.0 }; // ring buffer
  int rsSampleCount = 0, errorSampleCount = 0;
  double rsAdjustedA = 0.0, rsAdjustedB = 0.0;
  bool rsB = false;
  static double currentSrcRate = _srcRate;

  while (true) {
    atomic_wait(&threadState, 1);
    if (atomic_load(&threadState) == 0) return NULL; // 0 means deinit
    atomic_store(&threadState, 1);

    int_fast64_t rs = atomic_load(&receiverSync);

    if (rsB) {
      rsAdjustedB = 0.00000001 * (double)rs;
    } else {
      rsAdjustedA = 0.00000001 * (double)rs;
    }
    rsB = !rsB;

    rsHistory[rsSampleCount % RS_HISTORY_LENGTH] = rsAdjustedA > rsAdjustedB ? rsAdjustedA : rsAdjustedB;
    rsSampleCount++;
    if (rsSampleCount < 2) continue;

    double errorSamplesPerSecond;
    if (rsSampleCount <= RS_HISTORY_LENGTH) {
      errorSamplesPerSecond = linearRegressionSlope(rsHistory, 0, rsSampleCount);
    } else {
      errorSamplesPerSecond = linearRegressionSlope(rsHistory, rsSampleCount % RS_HISTORY_LENGTH, RS_HISTORY_LENGTH);
    }

    errorSamplesPerSecond /= RS_SAMPLE_INTERVAL; // DEBUG: not sure if this scaling is correct
    errorWindow[errorSampleCount % RS_ERROR_VARIANCE_WINDOW] = errorSamplesPerSecond;
    errorSampleCount++;

    if (errorSampleCount >= RS_ERROR_VARIANCE_WINDOW) {
      double errorVariance = variance(errorWindow, errorSampleCount % RS_ERROR_VARIANCE_WINDOW, RS_ERROR_VARIANCE_WINDOW);
      if (errorVariance < RS_ERROR_VARIANCE_TARGET && fabs(errorSamplesPerSecond) > RS_ERROR_THRESHOLD) {
        currentSrcRate += errorSamplesPerSecond; 
        double clockErrorPPM = 1000000.0 * (1.0 - _srcRate / currentSrcRate);
        globals_set1ff(statsCh1Audio, clockError, clockErrorPPM);

        // clear rsHistory and errorWindow
        rsSampleCount = 0;
        errorSampleCount = 0;

        syncer_changeRate(currentSrcRate);
      }
    }
  }

  return NULL;
}

int _syncer_initReceiverSync (double srcRate) {
  _srcRate = srcRate;

  atomic_store(&threadState, 1); // running
  if (pthread_create(&receiverSyncThread, NULL, startReceiverSync, NULL) != 0) return -1;
  return 0;
}

void _syncer_deinitReceiverSync (void) {
  atomic_store(&threadState, 0);
  pthread_join(receiverSyncThread, NULL);
}

/////////////////////
// public
/////////////////////

// DEBUG: handle temporary network loss better

// NOTE:
// - not thread-safe, call from one thread only
// - this is a realtime thread
// - must call _syncer_initReceiverSync first (not checked)
void syncer_onAudio (unsigned int frameCount) {
  static int sampleIntervalFrames = 0;

  if (atomic_load_explicit(&active, memory_order_relaxed)) {
    atomic_fetch_sub_explicit(&receiverSync, 100000000 * (int_fast64_t)frameCount, memory_order_relaxed);

    sampleIntervalFrames += frameCount;
    if (sampleIntervalFrames >= RS_SAMPLE_INTERVAL * (int)_srcRate) {
      atomic_store(&threadState, 2);
      atomic_notify_one(&threadState);
      sampleIntervalFrames = 0;
    }
  }
}

// NOTE:
// - not thread-safe, call from one thread only
// - this is a realtime thread
// - must call _syncer_initReceiverSync first (not checked)
void syncer_onPacket (int seq, int frameCount) {
  static int seqLast = -1;

  // TODO: this can be optimised by having resamp-state update it, instead of calling syncer_getRateRatio for every packet
  double rateRatio = syncer_getRateRatio();

  if (seqLast >= 0) {
    int seqDiff = seq - seqLast;
    // Overflow
    if (seqDiff < -32768) { 
      seqDiff += 65536;
    } else if (seqDiff > 32768) {
      seqDiff -= 65536;
    }

    if (seqDiff >= 1) {
      double rsDiff = 100000000.0 * rateRatio * (double)frameCount * (double)seqDiff;
      atomic_fetch_add_explicit(&receiverSync, (int_fast64_t)rsDiff, memory_order_relaxed);
      atomic_store_explicit(&active, true, memory_order_relaxed);
    }
  }
  seqLast = seq;
}
