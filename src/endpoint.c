#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include "globals.h"
#include "endpoint.h"

#define UNUSED __attribute__((unused))

static int endpointCount = 0;
static int *sockets = NULL;
static pthread_t *recvThreads = NULL;
static atomic_bool recvLoopRunning = true;
static int (*_onPacket)(const uint8_t*, int) = NULL;

static void *recvLoop (void *arg) {
  uint8_t recvBuf[1600] = { 0 };
  int socket = *((int*)arg);

  while (recvLoopRunning) {
    ssize_t recvLen = recv(socket, recvBuf, sizeof(recvBuf), 0);
    if (recvLen == -1) continue;

    _onPacket(recvBuf, recvLen);
  }

  return NULL;
}

int endpoint_init (bool rx, int (*onPacket)(const uint8_t*, int)) {
  _onPacket = onPacket;
  endpointCount = globals_get1i(endpoints, endpointCount);
  sockets = (int*)malloc(sizeof(int) * endpointCount);
  recvThreads = (pthread_t*)malloc(sizeof(pthread_t) * endpointCount);

  for (int i = 0; i < endpointCount; i++) {
    sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets[i] < 0) return -1;
  }

  int err;

  struct ifaddrs *ifList;
  err = getifaddrs(&ifList);
  if (err < 0) return -2;
  struct ifaddrs *currentIf = ifList;
  printf("\nAvailable network interfaces:\n");
  while (currentIf != NULL) {
    if (currentIf->ifa_addr == NULL) {
      currentIf = currentIf->ifa_next;
      continue;
    }

    if (currentIf->ifa_addr->sa_family == AF_INET) {
      unsigned int addr = ((struct sockaddr_in *)currentIf->ifa_addr)->sin_addr.s_addr;
      printf("* %s, ", currentIf->ifa_name);
      printf("%d.%d.%d.%d\n", addr & 0xff, (addr>>8) & 0xff, (addr>>16) & 0xff, addr >> 24);
    } else if (currentIf->ifa_addr->sa_family == AF_INET6) {
      uint8_t *addr = ((struct sockaddr_in6 *)currentIf->ifa_addr)->sin6_addr.s6_addr;
      printf("* %s, ", currentIf->ifa_name);
      for (int i = 0; i < 16; i += 2) {
        printf("%02x%02x", addr[i], addr[i+1]);
        if (i != 14) printf(":");
      }
      printf("\n");
    }

    currentIf = currentIf->ifa_next;
  }
  printf("\n");
  freeifaddrs(ifList);

  if (rx) {
    struct sockaddr_in bindAddrStruct = { 0 };
    bindAddrStruct.sin_family = AF_INET;
    bindAddrStruct.sin_addr.s_addr = INADDR_ANY;

    for (int i = 0; i < endpointCount; i++) {
      int port = globals_get1iv(endpoints, port, i);
      bindAddrStruct.sin_port = htons(port);
      err = bind(sockets[i], (struct sockaddr*)&bindAddrStruct, sizeof(bindAddrStruct));
      if (err < 0) return -3;
      printf("Endpoint: bound to UDP port %d\n", port);
    }
  } else {
    struct sockaddr_in connectAddrStruct = { 0 };
    connectAddrStruct.sin_family = AF_INET;

    for (int i = 0; i < endpointCount; i++) {
      connectAddrStruct.sin_port = htons(globals_get1iv(endpoints, port, i));
      connectAddrStruct.sin_addr.s_addr = globals_get1uiv(endpoints, addr, i);
      err = connect(sockets[i], (struct sockaddr*)&connectAddrStruct, sizeof(connectAddrStruct));
      if (err < 0) return -4;
    }
  }

  if (onPacket == NULL) return 0;

  for (int i = 0; i < endpointCount; i++) {
    err = pthread_create(&recvThreads[i], NULL, recvLoop, &sockets[i]);
    if (err != 0) return -5;
  }

  return 0;
}

int endpoint_send (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < endpointCount; i++) {
    ssize_t sentLen = send(sockets[i], buf, bufLen, 0);
    if (sentLen != bufLen) return -i - 1;
  }
  return 0;
}

void endpoint_deinit () {
  recvLoopRunning = false;
  for (int i = 0; i < endpointCount; i++) {
    pthread_join(recvThreads[i], NULL);
  }
  free(sockets);
}
