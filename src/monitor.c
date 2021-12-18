#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "uWebSockets/libuwebsockets.h"
#include "globals.h"
#include "stats.h"
#include "monitor.h"

static int audioChannelCount, monitorStatsBufLen;

// DEBUG: GCC or clang only! https://stackoverflow.com/questions/45116212/are-packed-structs-portable
// DEBUG: change to protobuf
// static union {
//   struct __attribute__((__packed__)) {
//     double audioLevelsFast[AUDIO_CHANNEL_COUNT];
//     double audioLevelsSlow[AUDIO_CHANNEL_COUNT];
//     int32_t audioClippingCount[AUDIO_CHANNEL_COUNT];
//     int32_t streamBufferPos;
//     int32_t bufferOverrunCount;
//     int32_t bufferUnderrunCount;
//     int32_t dupBlockCount;
//     int32_t oooBlockCount;
//     int32_t codecErrorCount;
//   } stats;
//   uint8_t buf[MONITOR_STATS_BUF_LEN];
// } statsData;

static uws_ws_t* _Atomic wsClient = NULL;

static void openHandler(uws_ws_t *ws) {
  wsClient = ws;
}

static void messageHandler(uws_ws_t *ws, const char *msg, size_t length, unsigned char opCode) {
  // printf("message (code: %d, length: %zu): %.*s\n", opCode, length, length, msg);
}

static void closeHandler(uws_ws_t *ws, int code) {
  wsClient = NULL;
}

static void listenHandler(void *listenSocket) {
  if (listenSocket) {
    printf("Monitor: WebSocket server listening on port %ld\n", globals_get1i(monitor, wsPort));
  }
}

static void *startWsApp (void *arg) {
  uws_app_t *app = uws_createApp();
  uws_appWs(app, "/*", openHandler, messageHandler, closeHandler);
  uws_appListen(app, globals_get1i(monitor, wsPort), listenHandler);
  uws_appRun(app);

  return NULL;
}

// static inline int clampInt (int x, int min, int max) {
//   if (x > max) {
//     return max;
//   } else if (x < min) {
//     return min;
//   }
//   return x;
// }

static void *statsLoop (void *arg) {
  while (true) {
    if (wsClient != NULL) {
      // for (int i = 0; i < audioChannelCount; i++) {
      //   // https://blog.regehr.org/archives/959
      //   memcpy(&statsData.stats.audioLevelsFast[i], &stats_ch1.audioLevelsFast[i], 8);
      //   memcpy(&statsData.stats.audioLevelsSlow[i], &stats_ch1.audioLevelsSlow[i], 8);
      //   statsData.stats.audioClippingCount[i] = stats_ch1.audioClippingCount[i];
      // }

      // statsData.stats.streamBufferPos = stats_ch1.lastRingSize;
      // statsData.stats.bufferOverrunCount = stats_ch1.ringOverrunCount;
      // statsData.stats.bufferUnderrunCount = stats_ch1.ringUnderrunCount;
      // statsData.stats.dupBlockCount = stats_ch1.dupBlockCount;
      // statsData.stats.oooBlockCount = stats_ch1.oooBlockCount;
      // statsData.stats.codecErrorCount = stats_ch1.codecErrorCount;

      // int error = uws_wsSend(wsClient, (const char *)statsData.buf, monitorStatsBufLen, UWS_OPCODE_BINARY);
      // if (error < 0) printf("uws_wsSend error: %d\n", error); // DEBUG: log
    }
    usleep(50000);
  }

  return NULL;
}

int monitor_init () {
  if (sizeof(float) != 4) return -1;

  audioChannelCount = globals_get1i(audio, channelCount);
  monitorStatsBufLen = 20*audioChannelCount + 24;

  pthread_t wsThread, statsThread;
  int err = pthread_create(&wsThread, NULL, startWsApp, NULL);
  if (err != 0) return -2;
  err = pthread_create(&statsThread, NULL, statsLoop, NULL);
  if (err != 0) return -3;

  return 0;
}
