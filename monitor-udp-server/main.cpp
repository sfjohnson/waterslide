// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <atomic>
#include "uWebSockets/libuwebsockets.h"

#define UNUSED __attribute__((unused))
#define WS_PORT 7681
#define UDP_PORT 26173
#define RECV_BUF_LEN 5000
#define RECV_LOOP_IDLE_INTERVAL 200000 // microseconds

static std::atomic_bool running = true;
static std::atomic<uws_ws_t*> wsClient = NULL;

static void openHandler(uws_ws_t *ws) {
  printf("ws client connected\n");
  wsClient = ws;
}

static void messageHandler(UNUSED uws_ws_t *ws, UNUSED const char *msg, UNUSED size_t length, UNUSED unsigned char opCode) {
  // printf("message (code: %d, length: %zu): %.*s\n", opCode, length, length, msg);
}

static void closeHandler(UNUSED uws_ws_t *ws, UNUSED int code) {
  printf("ws client disconnected\n");
  wsClient = NULL;
}

static void listenHandler(void *listenSocket) {
  if (listenSocket) {
    printf("ws server listening on port %d\n", WS_PORT);
  }
}

static void *startWsApp (UNUSED void *arg) {
  uws_app_t *app = uws_createApp();
  uws_appWs(app, "/*", openHandler, messageHandler, closeHandler);
  uws_appListen(app, WS_PORT, listenHandler);
  uws_appRun(app);

  // there is no uws_appClose :shrugs:
  return NULL;
}

static void sigintHandler (UNUSED int signum) {
  running = false;
}

int main () {
  printf("Waterslide monitor UDP server, build 1\n");

  uint8_t *recvBuf = (uint8_t *)malloc(RECV_BUF_LEN);
  if (recvBuf == NULL) {
    printf("malloc failed\n");
    return EXIT_FAILURE;
  }

  pthread_t wsThread;
  if (pthread_create(&wsThread, NULL, startWsApp, NULL) != 0) {
    printf("pthread_create failed\n");
    return EXIT_FAILURE;
  } 

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    printf("socket failed\n");
    return EXIT_FAILURE;
  }

  // set recv timeout so the loop doesn't become unresponsive when there are no packets
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = RECV_LOOP_IDLE_INTERVAL;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
    printf("setsockopt failed\n");
    return EXIT_FAILURE;
  }

  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  bindAddr.sin_port = htons(UDP_PORT);
  if (bind(sock, (const struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
    printf("bind failed\n");
    return EXIT_FAILURE;
  }

  printf("bound to UDP port %d\n", UDP_PORT);

  struct sockaddr_in recvAddr;
  memset(&recvAddr, 0, sizeof(recvAddr));

  signal(SIGINT, sigintHandler);

  while (running) {
    socklen_t recvAddrLen = sizeof(recvAddr);
    ssize_t recvLen = recvfrom(sock, recvBuf, RECV_BUF_LEN, 0, (struct sockaddr*)&recvAddr, &recvAddrLen);

    if (wsClient == NULL || recvLen < 0 || recvAddrLen != sizeof(recvAddr)) {
      // if something is going wrong keep the CPU usage down
      struct timespec tsp;
      tsp.tv_nsec = 20000000; // 20 ms
      tsp.tv_sec = 0;
      clock_nanosleep(CLOCK_MONOTONIC, 0, &tsp, NULL);
      continue;
    }

    uws_wsSend(wsClient, (char *)recvBuf, recvLen, UWS_OPCODE_BINARY);
  }

  printf("\nclosing...\n");

  close(sock);
  return EXIT_SUCCESS;
}
