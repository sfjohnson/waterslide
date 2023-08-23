#include <atomic>
#include "globals.h"
#include "syncer.h"

static atomic_bool active = false;
static atomic_int_fast64_t receiverSync = 0;

/////////////////////
// public
/////////////////////

void syncer_onAudio (unsigned int frameCount) {
  static int audioCbCounter = 0;
  static double rsAdjustedA = 0.0, rsAdjustedB = 0.0;
  static bool rsB = false;

  if (atomic_load_explicit(&active, memory_order_relaxed)) {
    int_fast64_t rs = atomic_fetch_sub_explicit(&receiverSync, 100000000 * (int_fast64_t)frameCount, memory_order_relaxed);

    audioCbCounter += frameCount;
    if (audioCbCounter >= 5 * 48000) { // TODO: different sample rates
      if (rsB) {
        rsAdjustedB = 0.00000001 * (double)rs;
      } else {
        rsAdjustedA = 0.00000001 * (double)rs;
      }
      rsB = !rsB;

      double rsAdjusted = rsAdjustedA > rsAdjustedB ? rsAdjustedA : rsAdjustedB;
      globals_set1ff(statsCh1Audio, receiverSyncFilt, rsAdjusted);

      audioCbCounter = 0;
    }
  }
}

// NOTE: not thread-safe
void syncer_onPacket (int seq, int frameCount) {
  static int seqLast = -1;

  // TODO: this can be optimised by having resamp-state update it, instead of calling syncer_getRateRatio for every packet
  double rateRatio = syncer_getRateRatio();

  // DEBUG:
  // static int seqRecording[50];
  // static int seqRecordingPos = 0;
  // static int seqEventCounter = 0;
  // seqRecording[seqRecordingPos++] = seq;
  // if (seqRecordingPos == 50) seqRecordingPos = 0;
  // if (seqEventCounter > 0) {
  //   if (++seqEventCounter == 25) {
  //     seqEventCounter = -1;
  //     for (int i = 0; i < 50; i++) {
  //       if (++seqRecordingPos == 50) seqRecordingPos = 0;
  //       printf("%d\n", seqRecording[seqRecordingPos]);
  //     }
  //   }
  // }

  if (seqLast >= 0) {
    int seqDiff = seq - seqLast;
    // Overflow
    if (seqDiff < -32768) { 
      seqDiff += 65536;
    } else if (seqDiff > 32768) {
      seqDiff -= 65536;
    }

    // DEBUG
    // if (seqDiff != 1 && seqEventCounter == 0) seqEventCounter = 1;

    if (seqDiff >= 1) {
      double rsDiff = 100000000.0 * rateRatio * (double)frameCount * (double)seqDiff;
      atomic_fetch_add_explicit(&receiverSync, (int_fast64_t)rsDiff, memory_order_relaxed);
      atomic_store_explicit(&active, true, memory_order_relaxed);
    }
  }
  seqLast = seq;
}
