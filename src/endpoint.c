#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include "endpoint.h"

static int sock = -1;
static uint16_t port = 0;
static atomic_bool recvLoopRunning = true;
static uint8_t recvBuf[1600] = { 0 };
static int (*onPacket)(const uint8_t*, int) = NULL;

static void *recvLoop (void *arg) {
  while (recvLoopRunning) {
    ssize_t recvLen = recv(sock, recvBuf, sizeof(recvBuf), 0);
    if (recvLen == -1) continue;

    onPacket(recvBuf, recvLen);
  }

  return NULL;
}

int endpoint_init (bool rx, uint16_t _port, uint32_t remoteAddr, uint32_t bindAddr, int (*_onPacket)(const uint8_t*, int)) {
  onPacket = _onPacket;
  port = _port;
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return -1;

  int err;
  if (rx) {
    struct sockaddr_in bindAddrStruct = { 0 };
    bindAddrStruct.sin_family = AF_INET;
    bindAddrStruct.sin_port = htons(port);
    bindAddrStruct.sin_addr.s_addr = bindAddr;
    err = bind(sock, (struct sockaddr*)&bindAddrStruct, sizeof(bindAddrStruct));
    if (err < 0) return -2;
    printf("Endpoint: bound to UDP port %d\n", port);
  } else {
    struct sockaddr_in connectAddrStruct = { 0 };
    connectAddrStruct.sin_family = AF_INET;
    connectAddrStruct.sin_port = htons(port);
    connectAddrStruct.sin_addr.s_addr = remoteAddr;
    err = connect(sock, (struct sockaddr*)&connectAddrStruct, sizeof(connectAddrStruct));
    if (err < 0) return -3;
  }

  if (onPacket != NULL) {
    pthread_t recvThread;
    err = pthread_create(&recvThread, NULL, recvLoop, NULL);
    if (err != 0) return -4;
  }

  return 0;
}

int endpoint_send (const uint8_t *buf, int bufLen) {
  ssize_t sentLen = send(sock, buf, bufLen, 0);
  if (sentLen != bufLen) return -1;
  return 0;
}

// DEBUG: hack for relay
int endpoint_sendRelay (const uint8_t *buf, int bufLen, uint32_t remoteAddr) {
  struct sockaddr_in remoteAddrStruct = { 0 };
  remoteAddrStruct.sin_family = AF_INET;
  remoteAddrStruct.sin_port = htons(port);
  remoteAddrStruct.sin_addr.s_addr = remoteAddr;
  ssize_t sentLen = sendto(sock, buf, bufLen, 0, (struct sockaddr*)&remoteAddrStruct, sizeof(remoteAddrStruct));
  if (sentLen != bufLen) return -1;
  return 0;
}
