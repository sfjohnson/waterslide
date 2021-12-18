#include <stdlib.h>
#include "globals.h"
#include "stats.h"

stats_audioStream_t stats_ch1;

int stats_init () {
  stats_ch1.lastRingSize = 0;
  stats_ch1.ringOverrunCount = 0;
  stats_ch1.ringUnderrunCount = 0;
  stats_ch1.dupBlockCount = 0;
  stats_ch1.oooBlockCount = 0;
  stats_ch1.lastBlockSbnDiff = 0;
  stats_ch1.codecErrorCount = 0;

  int audioChannelCount = globals_get1i(audio, channelCount);

  stats_ch1.audioClippingCount = (atomic_int*)malloc(sizeof(atomic_int) * audioChannelCount);
  if (stats_ch1.audioClippingCount == NULL) return -1;
  stats_ch1.audioLevelsFast = (atomic_int_fast64_t*)malloc(8 * audioChannelCount);
  if (stats_ch1.audioLevelsFast == NULL) return -2;
  stats_ch1.audioLevelsSlow = (atomic_int_fast64_t*)malloc(8 * audioChannelCount);
  if (stats_ch1.audioLevelsSlow == NULL) return -3;

  for (int i = 0; i < audioChannelCount; i++) {
    stats_ch1.audioClippingCount[i] = 0;
    stats_ch1.audioLevelsFast[i] = 0;  // punned as double
    stats_ch1.audioLevelsSlow[i] = 0;  // punned as double
  }

  return 0;
}
