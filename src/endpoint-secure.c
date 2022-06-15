#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "globals.h"
#include "utils.h"
#include "boringtun/wireguard_ffi.h"
#include "endpoint-secure.h"

#define UNUSED __attribute__((unused))

// DEBUG: implement IPv6 support
typedef struct {
  atomic_int socket;
  atomic_int timeToReopen;
  atomic_uint_fast64_t packedSrcAddr; // port and address in one atomic var
} endpoint_t;

static int endpointCount = 0;
static endpoint_t *sockets = NULL; // length: endpointCount
static pthread_t *recvThreads = NULL; // length: endpointCount
static pthread_t tickThread;
static atomic_bool threadsRunning = false;
// static atomic_bool handshakeComplete = false;
static struct wireguard_tunnel *tunnel = NULL;
static int (*_onPacket)(const uint8_t*, int, int) = NULL;

static void sendBufToAll (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < endpointCount; i++) {
    if (sockets[i].timeToReopen > 0) continue;

    ssize_t sentLen = bufLen;
    if (_onPacket == NULL) {
      // sender, we have already called connect
      sentLen = send(sockets[i].socket, buf, bufLen, 0);
    } else {
      // receiver, use the most recent received source port and address
      // If we have not received a packet yet, don't send anything
      uint64_t packedSrcAddr = sockets[i].packedSrcAddr;
      uint16_t srcPort = packedSrcAddr & 0xffff;
      if (srcPort != 0) {
        struct sockaddr_in srcSockaddr = { 0 };
        srcSockaddr.sin_family = AF_INET;
        srcSockaddr.sin_port = srcPort;
        srcSockaddr.sin_addr.s_addr = packedSrcAddr >> 16;

        sentLen = sendto(sockets[i].socket, buf, bufLen, 0, (struct sockaddr*)&srcSockaddr, sizeof(struct sockaddr_in));

        // DEBUG: log
        // printf("sendto %d.%d.%d.%d:%d length %d\n", srcSockaddr.sin_addr.s_addr & 0xff, (srcSockaddr.sin_addr.s_addr>>8) & 0xff, (srcSockaddr.sin_addr.s_addr>>16) & 0xff, srcSockaddr.sin_addr.s_addr >> 24, htons(srcSockaddr.sin_port), sentLen);
      }
    }

    // If send failed, close this socket and re-open on a timer
    if (sentLen != bufLen) {
      // DEBUG: log
      // printf("send or sendto failed: %s\n", strerror(errno));
      globals_set1uiv(statsEndpoints, open, i, 0);
      close(sockets[i].socket);
      sockets[i].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
    }
  }
}

static void wgRead (const uint8_t *buf, int bufLen, int epIndex) {
  static uint8_t dstBuf[1500] = { 0 };
  static struct wireguard_result result;

  while (true) {
    result = wireguard_read(tunnel, buf, bufLen, dstBuf, sizeof(dstBuf));

    switch (result.op) {
      case WIREGUARD_ERROR:
        globals_set1uiv(statsEndpoints, open, epIndex, 0);
        close(sockets[epIndex].socket);
        sockets[epIndex].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
        return;

      case WRITE_TO_TUNNEL_IPV4:
        if (result.size > 20 && _onPacket != NULL) {
          _onPacket(dstBuf + 20, result.size - 20, epIndex);
        }
        return;

      case WRITE_TO_NETWORK:
        if (result.size > 0) sendBufToAll(dstBuf, result.size);
        bufLen = 0;
        break;

      default:
        return;
    }
  }
}

static void *recvLoop (void *arg) {
  struct sockaddr_in srcSockaddr;
  uint8_t recvBuf[1500] = { 0 };
  intptr_t epIndex = (intptr_t)arg;

  while (threadsRunning) {
    if (sockets[epIndex].timeToReopen > 0) {
      // If we can't receive, don't max out the CPU core waiting
      usleep(ENDPOINT_TICK_INTERVAL);
      continue;
    }

    // Accepts packets from any source address
    socklen_t srcAddrLen = sizeof(struct sockaddr_in);
    ssize_t recvLen = recvfrom(sockets[epIndex].socket, recvBuf, sizeof(recvBuf), 0, (struct sockaddr*)&srcSockaddr, &srcAddrLen);

    // If recv failed, close this socket and re-open on a timer
    if (recvLen < 0) {
      globals_set1uiv(statsEndpoints, open, epIndex, 0);
      close(sockets[epIndex].socket);
      sockets[epIndex].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
      continue;
    }

    // Pack source port and address and assign in one atomic operation
    sockets[epIndex].packedSrcAddr = ((uint_fast64_t)srcSockaddr.sin_addr.s_addr << 16) | srcSockaddr.sin_port;

    wgRead(recvBuf, recvLen, epIndex);
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
    // If we are a sender don't bind to port
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
    err = connect(sockets[epIndex].socket, (struct sockaddr*)&connectAddrStruct, sizeof(struct sockaddr_in));
    if (err < 0) return -10;
  }

  return 0;
}

static void *tickLoop (UNUSED void *arg) {
  uint8_t tickBuf[1500] = { 0 };
  struct wireguard_result result;

  while (threadsRunning) {
    // Check if handshake is complete
    // struct stats wgStats = wireguard_stats(tunnel);
    // if (wgStats.time_since_last_handshake >= 0) {
    //   handshakeComplete = true;
    // } else {
    //   handshakeComplete = false;
    // }

    for (int i = 0; i < endpointCount; i++) {
      if (sockets[i].timeToReopen == 1) {
        if (openSocket(i) < 0) {
          sockets[i].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
        } else {
          sockets[i].packedSrcAddr = 0;
          sockets[i].timeToReopen = 0;
          globals_set1uiv(statsEndpoints, open, i, 1);
        }
      } else if (sockets[i].timeToReopen > 1) {
        sockets[i].timeToReopen--;
      }
    }

    result = wireguard_tick(tunnel, tickBuf, sizeof(tickBuf));
    if (result.op == WRITE_TO_NETWORK) sendBufToAll(tickBuf, result.size);

    usleep(ENDPOINT_TICK_INTERVAL);
  }

  return NULL;
}

int endpointsec_init (const char *privKey, const char *peerPubKey, int (*onPacket)(const uint8_t*, int, int)) {
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

  // Preshared keys are optional: https://www.procustodibus.com/blog/2021/09/wireguard-key-rotation/#preshared-keys
  tunnel = new_tunnel(privKey, peerPubKey, NULL, SEC_KEEP_ALIVE_INTERVAL, 0, NULL, 0);
  if (tunnel == NULL) return -20;

  // Set all the endpoints to open for stats
  for (int i = 0; i < endpointCount; i++) {
    globals_set1uiv(statsEndpoints, open, i, 1);
  }

  threadsRunning = true;

  err = pthread_create(&tickThread, NULL, tickLoop, NULL);
  if (err != 0) return -21;

  for (intptr_t i = 0; i < endpointCount; i++) {
    err = pthread_create(&recvThreads[i], NULL, recvLoop, (void*)i);
    if (err != 0) return -22;
  }

  return 0;
}

// NOTE: this function is not thread safe due to srcBuf and dstBuf being static.
int endpointsec_send (const uint8_t *buf, int bufLen) {
  // If handshake is not complete, drop buf to avoid flooding wireguard.
  // if (!handshakeComplete) return 0;

  // This buffer starts with a fake IPv4 header that passes BoringTun's packet checks.
  static uint8_t srcBuf[1500] = { 0x45, 0x00, 0x00, 0x00 };
  static const int maxSrcDataLen = sizeof(srcBuf) - 20;
  static uint8_t dstBuf[1500] = { 0 };

  if (bufLen > maxSrcDataLen) return -1;

  int srcBufLen = bufLen + 20;
  // Set length field in the fake header as it is checked by BoringTun.
  srcBuf[2] = srcBufLen >> 8;
  srcBuf[3] = srcBufLen & 0xff;

  memcpy(srcBuf + 20, buf, bufLen);

  struct wireguard_result result;
  result = wireguard_write(tunnel, srcBuf, srcBufLen, dstBuf, sizeof(dstBuf));
  if (result.op == WRITE_TO_NETWORK && result.size > 0) {
    sendBufToAll(dstBuf, result.size);
  }

  return 0;
}

void endpointsec_deinit () {
  if (tunnel != NULL) tunnel_free(tunnel);

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
