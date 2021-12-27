#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <atomic>
#include <stdint.h>
#include <string.h>
#include "uWebSockets/libuwebsockets.h"
#include "globals.h"
#include "protobufs/monitor.pb.h"
#include "monitor.h"

static int audioChannelCount;
static std::atomic<uws_ws_t*> wsClient = NULL;

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
    int wsPort = globals_get1i(monitor, wsPort);
    printf("Monitor: WebSocket server listening on port %d\n", wsPort);
  }
}

static void *startWsApp (void *arg) {
  uws_app_t *app = uws_createApp();
  uws_appWs(app, "/*", openHandler, messageHandler, closeHandler);
  uws_appListen(app, globals_get1i(monitor, wsPort), listenHandler);
  uws_appRun(app);

  return NULL;
}

static void *statsLoop (void *arg) {
  MonitorProto proto;
  MonitorProto_MuxChannelStats *protoCh1 = proto.add_muxchannel();
  MonitorProto_AudioChannel **protoAudioChannels = new MonitorProto_AudioChannel*[audioChannelCount];

  for (int i = 0; i < audioChannelCount; i++) {
    protoAudioChannels[i] = protoCh1->mutable_audiostats()->add_audiochannel();
  }

  while (true) {
    usleep(50000);
    if (wsClient == NULL) continue;

    for (int i = 0; i < audioChannelCount; i++) {
      protoAudioChannels[i]->set_clippingcount(globals_get1uiv(statsCh1Audio, clippingCounts, i));
      double levelFast, levelSlow;
      globals_get1ffv(statsCh1Audio, levelsFast, i, &levelFast);
      globals_get1ffv(statsCh1Audio, levelsSlow, i, &levelSlow);
      protoAudioChannels[i]->set_levelfast(levelFast);
      protoAudioChannels[i]->set_levelslow(levelSlow);
    }

    protoCh1->set_dupblockcount(globals_get1ui(statsCh1, dupBlockCount));
    protoCh1->set_oooblockcount(globals_get1ui(statsCh1, oooBlockCount));
    protoCh1->set_lastblocksbndiff(globals_get1i(statsCh1, lastBlockSbnDiff));
    protoCh1->set_duppacketcount(globals_get1ui(statsCh1, dupPacketCount));
    protoCh1->set_ooopacketcount(globals_get1ui(statsCh1, oooPacketCount));
    protoCh1->mutable_audiostats()->set_bufferoverruncount(globals_get1ui(statsCh1Audio, bufferOverrunCount));
    protoCh1->mutable_audiostats()->set_bufferunderruncount(globals_get1ui(statsCh1Audio, bufferUnderrunCount));
    protoCh1->mutable_audiostats()->set_codecerrorcount(globals_get1ui(statsCh1Audio, codecErrorCount));
    protoCh1->mutable_audiostats()->set_streambufferpos(globals_get1ui(statsCh1Audio, streamBufferPos));

    std::string protoData;
    proto.SerializeToString(&protoData);
    int error = uws_wsSend(wsClient, protoData.c_str(), protoData.length(), UWS_OPCODE_BINARY);
    if (error < 0) printf("uws_wsSend error: %d\n", error); // DEBUG: log
  }

  delete[] protoAudioChannels;
  return NULL;
}

int monitor_init () {
  audioChannelCount = globals_get1i(audio, channelCount);

  pthread_t wsThread, statsThread;
  int err = pthread_create(&wsThread, NULL, startWsApp, NULL);
  if (err != 0) return -2;
  err = pthread_create(&statsThread, NULL, statsLoop, NULL);
  if (err != 0) return -3;

  return 0;
}
