#ifndef _STATS_H
#define _STATS_H

#ifdef __cplusplus
#include <atomic>
using namespace std;
#else
#include <stdatomic.h>
#endif

typedef struct {
  atomic_int lastRingSize;
  atomic_int ringOverrunCount;
  atomic_int ringUnderrunCount;
  atomic_int dupBlockCount, oooBlockCount;
  atomic_int lastBlockSbnDiff;
  atomic_int codecErrorCount;
  atomic_int *audioClippingCount;
  atomic_int_fast64_t *audioLevelsFast; // punned as double
  atomic_int_fast64_t *audioLevelsSlow; // punned as double
} stats_audioStream_t;

extern stats_audioStream_t stats_ch1;

int stats_init ();

#endif
