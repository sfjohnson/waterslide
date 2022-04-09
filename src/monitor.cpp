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

#define UNUSED __attribute__((unused))

static int audioChannelCount, endpointCount;
static std::atomic<uws_ws_t*> wsClient = NULL;

static void openHandler(uws_ws_t *ws) {
  wsClient = ws;
}

static void messageHandler(UNUSED uws_ws_t *ws, UNUSED const char *msg, UNUSED size_t length, UNUSED unsigned char opCode) {
  // printf("message (code: %d, length: %zu): %.*s\n", opCode, length, length, msg);
}

static void closeHandler(UNUSED uws_ws_t *ws, UNUSED int code) {
  wsClient = NULL;
}

static void listenHandler(void *listenSocket) {
  if (listenSocket) {
    int wsPort = globals_get1i(monitor, wsPort);
    printf("Monitor: WebSocket server listening on port %d\n", wsPort);
  }
}

static void *startWsApp (UNUSED void *arg) {
  uws_app_t *app = uws_createApp();
  uws_appWs(app, "/*", openHandler, messageHandler, closeHandler);
  uws_appListen(app, globals_get1i(monitor, wsPort), listenHandler);
  uws_appRun(app);

  return NULL;
}

static void *statsLoop (UNUSED void *arg) {
  MonitorProto proto;
  MonitorProto_MuxChannelStats *protoCh1 = proto.add_muxchannel();
  MonitorProto_AudioChannel **protoAudioChannels = new MonitorProto_AudioChannel*[audioChannelCount];
  MonitorProto_EndpointStats **protoEndpoints = new MonitorProto_EndpointStats*[endpointCount];

  for (int i = 0; i < audioChannelCount; i++) {
    protoAudioChannels[i] = protoCh1->mutable_audiostats()->add_audiochannel();
  }
  for (int i = 0; i < endpointCount; i++) {
    protoEndpoints[i] = protoCh1->add_endpoint();
    std::string ifName(MAX_NET_IF_NAME_LEN + 1, '\0');
    int ifLen = globals_get1sv(endpoints, interface, i, &ifName[0], MAX_NET_IF_NAME_LEN + 1);
    if (ifLen <= 0) {
      ifName = "any";
    } else {
      ifName.resize(ifLen);
    }
    protoEndpoints[i]->set_interfacename(ifName);
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
    int lastSbn0 = globals_get1iv(statsCh1Endpoints, lastSbn, 0);
    for (int i = 0; i < endpointCount; i++) {
      int relSbn = globals_get1iv(statsCh1Endpoints, lastSbn, i) - lastSbn0;
      if (relSbn > 127) relSbn -= 256;
      if (relSbn < -128) relSbn += 256;
      protoEndpoints[i]->set_lastrelativesbn(relSbn);
      protoEndpoints[i]->set_duppacketcount(globals_get1uiv(statsCh1Endpoints, dupPacketCount, i));
      protoEndpoints[i]->set_ooopacketcount(globals_get1uiv(statsCh1Endpoints, oooPacketCount, i));
    }
    protoCh1->mutable_audiostats()->set_bufferoverruncount(globals_get1ui(statsCh1Audio, bufferOverrunCount));
    protoCh1->mutable_audiostats()->set_bufferunderruncount(globals_get1ui(statsCh1Audio, bufferUnderrunCount));
    protoCh1->mutable_audiostats()->set_codecerrorcount(globals_get1ui(statsCh1Audio, codecErrorCount));
    protoCh1->mutable_audiostats()->set_streambufferpos(globals_get1ui(statsCh1Audio, streamBufferPos));
    protoCh1->mutable_audiostats()->set_encodethreadjittercount(globals_get1ui(statsCh1Audio, encodeThreadJitterCount));

    std::string protoData;
    proto.SerializeToString(&protoData);
    int error = uws_wsSend(wsClient, protoData.c_str(), protoData.length(), UWS_OPCODE_BINARY);
    if (error < 0) printf("uws_wsSend error: %d\n", error); // DEBUG: log
  }

  delete[] protoAudioChannels;
  delete[] protoEndpoints;
  return NULL;
}

int monitor_init () {
  audioChannelCount = globals_get1i(audio, networkChannelCount);
  endpointCount = globals_get1i(endpoints, endpointCount);

  pthread_t wsThread, statsThread;
  int err = pthread_create(&wsThread, NULL, startWsApp, NULL);
  if (err != 0) return -2;
  err = pthread_create(&statsThread, NULL, statsLoop, NULL);
  if (err != 0) return -3;

  return 0;
}
