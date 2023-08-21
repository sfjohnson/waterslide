#include <atomic>
#include "globals.h"
#include "syncer.h"

static atomic_bool active = false;
static atomic_int_fast64_t receiverSync = 0;

/////////////////////
// public
/////////////////////

void syncer_onAudio (unsigned int frameCount) {
  static double receiverSyncFiltLpf = 0.0;

  if (atomic_load_explicit(&active, memory_order_relaxed)) {
    int_fast64_t rs = atomic_fetch_sub_explicit(&receiverSync, 100000000 * (int_fast64_t)frameCount, memory_order_relaxed);

    receiverSyncFiltLpf += 0.005 * (0.00000001 * (double)rs - receiverSyncFiltLpf);
    globals_set1ff(statsCh1Audio, receiverSyncFilt, receiverSyncFiltLpf);
  }
}

// NOTE: not thread-safe
void syncer_onPacket (int seq, int frameCount) {
  static int seqLast = -1;

  // TODO: this can be optimised by having resamp-state update it, instead of calling syncer_getRateRatio for every packet
  double rateRatio = syncer_getRateRatio();

  if (seqLast >= 0) {
    int seqDiff;
    if (seqLast - seq > 32768) {
      // Overflow
      seqDiff = 65536 - seqLast + seq;
    } else {
      seqDiff = seq - seqLast;
    }

    if (seqDiff >= 1) {
      double rsDiff = 100000000.0 * rateRatio * (double)frameCount * (double)seqDiff;
      atomic_fetch_add_explicit(&receiverSync, (int_fast64_t)rsDiff, memory_order_relaxed);
      atomic_store_explicit(&active, true, memory_order_relaxed);
    }
  }
  seqLast = seq;
}
