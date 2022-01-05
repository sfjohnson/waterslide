#include <netinet/in.h>
#include <net/if.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <string.h>
#include "globals.h"
#include "endpoint.h"

#define UNUSED __attribute__((unused))

static int endpointCount = 0;
static int *sockets = NULL; // length: endpointCount
static pthread_t *recvThreads = NULL; // length: endpointCount
static atomic_bool recvLoopRunning = true;
static int (*_onPacket)(const uint8_t*, int, int) = NULL;

static void *recvLoop (void *arg) {
  uint8_t recvBuf[1600] = { 0 };
  intptr_t epIndex = (intptr_t)arg;

  while (recvLoopRunning) {
    // Accepts packets from any source address
    ssize_t recvLen = recv(sockets[epIndex], recvBuf, sizeof(recvBuf), 0);
    if (recvLen < 0) continue;

    _onPacket(recvBuf, recvLen, epIndex);
  }

  return NULL;
}

static int bindSocketToIf (int socket, const char *ifName, UNUSED int ifLen, int port) {
  int err;
  #if defined(__ANDROID__) || defined(__linux__)
  err = setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, ifName, ifLen);
  if (err < 0) {
    printf("Endpoint: interface bind failed. CAP_NET_RAW or root are required!\n");
    return -1;
  }
  #elif defined(__MACH__)
  int ifIndex = if_nametoindex(ifName);
  if (ifIndex == 0) {
    printf("Endpoint: interface not found!\n");
    return -2;
  }
  err = setsockopt(socket, IPPROTO_IP, IP_BOUND_IF, &ifIndex, sizeof(ifIndex));
  if (err < 0) {
    printf("Endpoint: interface bind failed!\n");
    return -3;
  }
  #else
  printf("Endpoint: interface bind failed, this OS is not supported!\n");
  return -4;
  #endif

  if (port < 0) return 0;

  bool bound = false;
  struct ifaddrs *ifList;
  err = getifaddrs(&ifList);
  if (err < 0) return -5;
  struct ifaddrs *currentIf = ifList;
  while (currentIf != NULL) {
    if (currentIf->ifa_addr == NULL || strcmp(currentIf->ifa_name, ifName) != 0) {
      currentIf = currentIf->ifa_next;
      continue;
    }

    if (currentIf->ifa_addr->sa_family == AF_INET) {
      struct sockaddr_in *foundAddr = (struct sockaddr_in *)currentIf->ifa_addr;
      uint32_t addrBytes = foundAddr->sin_addr.s_addr;
      foundAddr->sin_port = htons(port);

      err = bind(socket, (struct sockaddr*)foundAddr, sizeof(struct sockaddr_in));
      if (err < 0) {
        freeifaddrs(ifList);
        return -6;
      }

      printf("Bound to %d.%d.%d.%d:%d on %s\n", addrBytes & 0xff, (addrBytes>>8) & 0xff, (addrBytes>>16) & 0xff, addrBytes >> 24, port, ifName);
      bound = true;
      break;
    } else if (currentIf->ifa_addr->sa_family == AF_INET6) {
      // uint8_t *addr = ((struct sockaddr_in6 *)currentIf->ifa_addr)->sin6_addr.s6_addr;
      // printf("* %s, ", currentIf->ifa_name);
      // for (int i = 0; i < 16; i += 2) {
      //   printf("%02x%02x", addr[i], addr[i+1]);
      //   if (i != 14) printf(":");
      // }
      // printf("\n");
    }

    currentIf = currentIf->ifa_next;
  }

  freeifaddrs(ifList);
  if (!bound) return -7;
  return 0;
}

int endpoint_init (bool rx, int (*onPacket)(const uint8_t*, int, int)) {
  int err;
  _onPacket = onPacket;
  endpointCount = globals_get1i(endpoints, endpointCount);
  sockets = (int*)malloc(sizeof(int) * endpointCount);
  recvThreads = (pthread_t*)malloc(sizeof(pthread_t) * endpointCount);

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
      sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
      if (sockets[i] < 0) return -3;
      err = bindSocketToIf(sockets[i], ifName, ifLen, rx ? globals_get1iv(endpoints, port, i) : -1);
      if (err < 0) return err - 3;
    }
  } else {
    printf("*** Multihoming disabled\n");
    sockets[0] = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockets[0] < 0) return -11;
    char ifName[MAX_NET_IF_NAME_LEN + 1] = { 0 };
    int ifLen = globals_get1sv(endpoints, interface, 0, ifName, sizeof(ifName));
    if (ifLen > 0) {
      err = bindSocketToIf(sockets[0], ifName, ifLen, rx ? globals_get1iv(endpoints, port, 0) : -1);
      if (err < 0) return err - 11;
    }
  }

  if (!rx) {
    struct sockaddr_in connectAddrStruct = { 0 };
    connectAddrStruct.sin_family = AF_INET;

    for (int i = 0; i < endpointCount; i++) {
      connectAddrStruct.sin_port = htons(globals_get1iv(endpoints, port, i));
      connectAddrStruct.sin_addr.s_addr = globals_get1uiv(endpoints, addr, i);
      err = connect(sockets[i], (struct sockaddr*)&connectAddrStruct, sizeof(connectAddrStruct));
      if (err < 0) return -19;
    }
  }

  if (onPacket == NULL) return 0;

  for (intptr_t i = 0; i < endpointCount; i++) {
    err = pthread_create(&recvThreads[i], NULL, recvLoop, (void*)i);
    if (err != 0) return -20;
  }

  return 0;
}

int endpoint_send (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < endpointCount; i++) {
    /*ssize_t sentLen = */send(sockets[i], buf, bufLen, 0);
    // DEBUG: if send failed, close this socket and try to re-open on a timer
    // if (sentLen != bufLen) return -i - 1;
  }
  return 0;
}

void endpoint_deinit () {
  recvLoopRunning = false;
  for (int i = 0; i < endpointCount; i++) {
    pthread_join(recvThreads[i], NULL);
    close(sockets[i]);
  }
  free(sockets);
}
