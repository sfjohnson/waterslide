#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "globals.h"
#include "utils.h"
#include "endpoint.h"

#define UNUSED __attribute__((unused))

// DEBUG: implement IPv6 support
typedef struct {
  atomic_int socket;
  atomic_int timeToReopen;
} endpoint_t;

static int endpointCount = 0;
static endpoint_t *sockets = NULL; // length: endpointCount
static pthread_t *recvThreads = NULL; // length: endpointCount
static pthread_t tickThread;
static atomic_bool threadsRunning = false;
static int (*_onPacket)(const uint8_t*, int, int) = NULL;

static void *recvLoop (void *arg) {
  uint8_t recvBuf[1500] = { 0 };
  intptr_t epIndex = (intptr_t)arg;

  while (threadsRunning) {
    if (sockets[epIndex].timeToReopen > 0) {
      // If we can't receive, don't max out the CPU core waiting
      usleep(ENDPOINT_TICK_INTERVAL);
      continue;
    }

    // Accepts packets from any source address
    ssize_t recvLen = recv(sockets[epIndex].socket, recvBuf, sizeof(recvBuf), 0);

    // If recv failed, close this socket and re-open on a timer
    if (recvLen < 0) {
      globals_set1uiv(statsEndpoints, open, epIndex, 0);
      close(sockets[epIndex].socket);
      sockets[epIndex].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
      continue;
    }

    // Accounts for IP and UDP headers
    // DEBUG: This assumes IPv4
    globals_add1uiv(statsEndpoints, bytesIn, epIndex, recvLen + 28);

    _onPacket(recvBuf, recvLen, epIndex);
  }

  return NULL;
}

static int openSocket (int epIndex) {
  int err;

  sockets[epIndex].socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockets[epIndex].socket < 0) return -1;

  char ifName[MAX_NET_IF_NAME_LEN + 1] = { 0 };
  int ifLen = globals_get1sv(endpoints, interface, epIndex, ifName, sizeof(ifName));
  if (ifLen > 0) {
    int port = _onPacket == NULL ? -1 : globals_get1iv(endpoints, port, epIndex);
    err = utils_bindSocketToIf(sockets[epIndex].socket, ifName, ifLen, port);
    if (err < 0) return err - 1;
  }

  if (_onPacket == NULL) {
    // sender
    struct sockaddr_in connectAddrStruct = { 0 };
    connectAddrStruct.sin_family = AF_INET;
    connectAddrStruct.sin_port = htons(globals_get1iv(endpoints, port, epIndex));
    connectAddrStruct.sin_addr.s_addr = globals_get1uiv(endpoints, addr, epIndex);
    err = connect(sockets[epIndex].socket, (struct sockaddr*)&connectAddrStruct, sizeof(connectAddrStruct));
    if (err < 0) return -10;
  }

  return 0;
}

static void *tickLoop (UNUSED void *arg) {
  while (threadsRunning) {
    for (int i = 0; i < endpointCount; i++) {
      if (sockets[i].timeToReopen == 1) {
        if (openSocket(i) < 0) {
          sockets[i].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
        } else {
          sockets[i].timeToReopen = 0;
          globals_set1uiv(statsEndpoints, open, i, 1);
        }
      } else if (sockets[i].timeToReopen > 1) {
        sockets[i].timeToReopen--;
      }
    }

    usleep(ENDPOINT_TICK_INTERVAL);
  }

  return NULL;
}

int endpoint_init (int (*onPacket)(const uint8_t*, int, int)) {
  int err;
  _onPacket = onPacket;
  endpointCount = globals_get1i(endpoints, endpointCount);
  sockets = (endpoint_t*)malloc(sizeof(endpoint_t) * endpointCount);
  recvThreads = (pthread_t*)malloc(sizeof(pthread_t) * endpointCount);

  memset(sockets, 0, sizeof(endpoint_t) * endpointCount);

  // Check endpoints are configured correctly
  if (endpointCount == 0) {
    printf("Endpoint: No endpoints specified!\n");
    return -1;
  }

  if (endpointCount > 1) {
    printf("*** Multihoming enabled\n");
    for (int i = 0; i < endpointCount; i++) {
      char ifName[MAX_NET_IF_NAME_LEN + 1] = { 0 };
      int ifLen = globals_get1sv(endpoints, interface, i, ifName, sizeof(ifName));
      if (ifLen <= 0) {
        printf("Endpoint: In multihoming mode, all endpoints must have an interface specified!\n");
        return -2;
      }
    }
  } else {
    printf("*** Multihoming disabled\n");
  }

  for (int i = 0; i < endpointCount; i++) {
    err = openSocket(i);
    if (err < 0) return err - 2;
  }

  // Set all the endpoints to open for stats
  for (int i = 0; i < endpointCount; i++) {
    globals_set1uiv(statsEndpoints, open, i, 1);
  }

  threadsRunning = true;

  err = pthread_create(&tickThread, NULL, tickLoop, NULL);
  if (err != 0) return -21;

  // If we are a sender then we are done, no recvLoop needed
  if (onPacket == NULL) return 0;

  for (intptr_t i = 0; i < endpointCount; i++) {
    err = pthread_create(&recvThreads[i], NULL, recvLoop, (void*)i);
    if (err != 0) return -22;
  }

  return 0;
}

int endpoint_send (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < endpointCount; i++) {
    if (sockets[i].timeToReopen > 0) continue;

    ssize_t sentLen = send(sockets[i].socket, buf, bufLen, 0);

    if (sentLen != bufLen) {
      // If send failed, close this socket and re-open on a timer
      globals_set1uiv(statsEndpoints, open, i, 0);
      close(sockets[i].socket);
      sockets[i].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
    } else {
      // Accounts for IP and UDP headers
      // DEBUG: This assumes IPv4
      globals_add1uiv(statsEndpoints, bytesOut, i, bufLen + 28);
    }
  }

  return 0;
}

void endpoint_deinit () {
  if (threadsRunning) {
    threadsRunning = false;
    pthread_join(tickThread, NULL);
    for (int i = 0; i < endpointCount; i++) {
      pthread_join(recvThreads[i], NULL);
      close(sockets[i].socket);
    }
    free(sockets);
  }
}
