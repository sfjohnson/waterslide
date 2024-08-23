// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "boringtun/wireguard_ffi.h"
#include "globals.h"
#include "utils.h"
#include "endpoint.h"

// DEBUG: I can't find SO_BINDTODEVICE anywhere, if you know what's going on plz tell me
#if defined(__linux__)
#define SO_BINDTODEVICE	25
#endif

#define WG_READ_BUF_LEN 1500

static endpoint_t *endpoints = NULL;
static pthread_t dataThread, openCloseThread;
static int endpointCount = 0;
static uint8_t myPubKey[32], peerPubKey[32];
static struct wireguard_tunnel *tunnel = NULL;
static atomic_bool tunnelUp = false;
static atomic_bool threadsRunning = true;
static int (*_onPacket)(const uint8_t*, size_t, int) = NULL;

/////////////////////
// private
/////////////////////

static void sendBufToAll (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < endpointCount; i++) {
    endpoint_t *ep = &endpoints[i];
    if (ep->state != GotPeerAddr) continue;

    struct sockaddr_in peerAddr = { 0 };
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_addr.s_addr = ep->peerAddr;
    peerAddr.sin_port = ep->peerPort;
    ssize_t sendLen = sendto(ep->sock, buf, bufLen, 0, (struct sockaddr*)&peerAddr, sizeof(peerAddr));
    if (sendLen < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        globals_add1uiv(statsEndpoints, sendCongestion, i, 1);
      } else {
        // send failed, close this endpoint and re-open after a delay
        ep->state = Close;
      }
    } else {
      // Accounts for IP and UDP headers
      // TODO: This assumes IPv4
      globals_add1uiv(statsEndpoints, bytesOut, i, bufLen + 28);
    }
  }
}

static void tickDiscovery (int epIndex) {
  static uint8_t sendBuf[65];
  endpoint_t *ep = &endpoints[epIndex];

  if (--ep->discoveryTickCounter > 0) return;

  ep->discoveryTickCounter = ENDPOINT_DISCOVERY_INTERVAL;

  struct sockaddr_in serverAddr = { 0 };
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(globals_get1i(discovery, serverPort));
  serverAddr.sin_addr.s_addr = globals_get1ui(discovery, serverAddr);
  memcpy(&sendBuf[0], myPubKey, 32);
  memcpy(&sendBuf[32], peerPubKey, 32);
  sendBuf[64] = epIndex;
  sendto(ep->sock, sendBuf, sizeof(sendBuf), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
}

static void tickTunnel (void) {
  static uint8_t tickBuf[1500] = { 0 };

  if (!tunnelUp) {
    struct stats tunnelStats = wireguard_stats(tunnel);
    if (tunnelStats.time_since_last_handshake >= 0) {
      tunnelUp = true;
    }
  }

  struct wireguard_result result = wireguard_tick(tunnel, tickBuf, sizeof(tickBuf));
  if (result.op == WRITE_TO_NETWORK) sendBufToAll(tickBuf, result.size);
}

static int openEndpoint (int epIndex) {
  endpoint_t *ep = &endpoints[epIndex];
  ep->peerAddr = 0;
  ep->peerPort = 0;
  ep->lastPacketUTime = -1;

  ep->sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (ep->sock < 0) return -1;

  int flags = fcntl(ep->sock, F_GETFL);
  if (flags < 0) return -2;
  if (fcntl(ep->sock, F_SETFL, flags | O_NONBLOCK) < 0) return -3;

  int err;
  // bind socket to interface
  #if defined(__ANDROID__) || defined(__linux__)
  err = setsockopt(ep->sock, SOL_SOCKET, SO_BINDTODEVICE, ep->ifName, strlen(ep->ifName));
  if (err < 0) return -4;
  #elif defined(__APPLE__)
  int ifIndex = if_nametoindex(ep->ifName);
  if (ifIndex == 0) return -5;
  err = setsockopt(ep->sock, IPPROTO_IP, IP_BOUND_IF, &ifIndex, sizeof(ifIndex));
  if (err < 0) return -6;
  #endif

  // bind source ports to 26173 for endpoint 0, 26174 for endpoint 1 etc...
  // these ports may need to be forwarded if you're on a restrictive NAT with no IPv6
  struct sockaddr_in bindAddr = { 0 };
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  bindAddr.sin_port = htons(26173 + epIndex);
  if (bind(ep->sock, (const struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
    return -7;
  }

  // DEBUG: log
  printf("epIndex %d bound to interface %s on UDP port %d\n", epIndex, ep->ifName, 26173 + epIndex);

  return 0;
}

static int onPeerPacket (const uint8_t *buf, int bufLen, int epIndex) {
  static uint8_t wgReadBuf[WG_READ_BUF_LEN] = { 0 };

  while (true) {
    struct wireguard_result result = wireguard_read(tunnel, buf, bufLen, wgReadBuf, WG_READ_BUF_LEN);

    switch (result.op) {
      case WIREGUARD_ERROR:
        // Multihoming will cause WireGuard errors due to dup packets.
        // We are safe to ignore this.
        if (result.size != 11) { // DuplicateCounter
          // TODO: Investigate wg errors when using multihoming.
          // I have observed errors 10, 7 and 2 but they don't seem to cause any issues higher up the stack.
          // printf("wg error: %zu\n", result.size);
        }
        return 0;

      case WRITE_TO_TUNNEL_IPV4:
        if (result.size > 0 && _onPacket != NULL) {
          _onPacket(wgReadBuf, result.size, epIndex);
        }
        return 0;

      case WRITE_TO_NETWORK:
        if (result.size > 0) sendBufToAll(wgReadBuf, result.size);
        bufLen = 0;
        break;

      default:
        return 0;
    }
  }

  return 0;
}

static void handleRes (int epIndex, uint8_t *buf, ssize_t len) {
  endpoint_t *ep = &endpoints[epIndex];

  // Accounts for IP and UDP headers
  // TODO: This assumes IPv4
  globals_add1uiv(statsEndpoints, bytesIn, epIndex, len + 28);

  switch (atomic_load(&ep->state)) {
    case Discovery:
      if (len != 38) return;

      for (int i = 0; i < 32; i++) {
        if (buf[i] != peerPubKey[i]) return;
      }

      for (int i = 0; i < 6; i++) {
        // TODO: secure discovery
        // XOR remote addr and port with myPubKey
        buf[32 + i] ^= myPubKey[i];
      }

      memcpy(&ep->peerAddr, &buf[32], 4);
      memcpy(&ep->peerPort, &buf[36], 2);
      ep->state = GotPeerAddr;
      ep->lastPacketUTime = utils_getCurrentUTime();
      globals_set1uiv(statsEndpoints, open, epIndex, 1);

      char addrString[16] = { 0 };
      inet_ntop(AF_INET, &ep->peerAddr, addrString, sizeof(addrString));
      printf("(epIndex %d) got peer addr %s:%d\n", epIndex, addrString, ntohs(ep->peerPort));
      break;

    case GotPeerAddr:
      onPeerPacket(buf, len, epIndex);
      break;

    default:
      break;
  }
}

/////////////////////
// public
/////////////////////

// NOTE: this function is not thread safe due to dstBuf being static.
int endpoint_send (const uint8_t *buf, size_t bufLen) {
  if (!tunnelUp) return -1;

  static uint8_t dstBuf[1500] = { 0 };

  struct wireguard_result result;
  result = wireguard_write(tunnel, buf, bufLen, dstBuf, sizeof(dstBuf));
  if (result.op == WRITE_TO_NETWORK && result.size > 0) {
    sendBufToAll(dstBuf, result.size);
  }

  return 0;
}

/////////////////////
// threads
/////////////////////

static void *openCloseLoop (UNUSED void *arg) {
  while (threadsRunning) {
    for (int epIndex = 0; epIndex < endpointCount; epIndex++) {
      endpoint_t *ep = &endpoints[epIndex];

      if (ep->state == Open) {
        int err = openEndpoint(epIndex);
        if (err < 0) {
          ep->state = Close;
        } else {
          ep->discoveryTickCounter = ENDPOINT_DISCOVERY_INTERVAL;
          ep->state = Discovery;
        }
      } else if (ep->state == Close) {
        globals_set1uiv(statsEndpoints, open, epIndex, 0);
        close(ep->sock);
        ep->reopenTickCounter = utils_randBetween(ENDPOINT_REOPEN_INTERVAL_MIN, ENDPOINT_REOPEN_INTERVAL_MAX);
        ep->state = WaitForReopen;
      } else if (ep->state == WaitForReopen) {
        if (--ep->reopenTickCounter == 0) {
          ep->state = Open;
        }
      }
    }

    utils_usleep(ENDPOINT_TICK_INTERVAL_US);
  }

  return NULL;
}

static void *dataLoop (UNUSED void *arg) {
  uint8_t recvBuf[1500] = { 0 };
  struct sockaddr_in recvAddr = { 0 };
  struct pollfd pfds[endpointCount];
  int lastTickUTime = utils_getCurrentUTime();

  // dividing by 2 means the max possible time between handleTick calls will be 
  // 1.5 * ENDPOINT_TICK_INTERVAL_US instead of 2 * ENDPOINT_TICK_INTERVAL_US
  int tickTimeoutUs = ENDPOINT_TICK_INTERVAL_US / 2;
  int tickTimeoutMs = tickTimeoutUs / 1000;

  // this thread is relatively lightweight; demux will pass all the heavy decoding
  // to other thread(s)
  utils_setCallerThreadRealtime(98, 0);

  while (threadsRunning) {
    bool tick = false;
    int elapsedUTime = utils_getElapsedUTime(lastTickUTime);
    if (elapsedUTime >= ENDPOINT_TICK_INTERVAL_US) {
      tickTunnel();
      lastTickUTime = utils_getCurrentUTime();
      tick = true;
    }

    bool allClosed = true;
    for (int i = 0; i < endpointCount; i++) {
      if (tick &&
        endpoints[i].state == GotPeerAddr &&
        endpoints[i].lastPacketUTime >= 0 &&
        utils_getElapsedUTime(endpoints[i].lastPacketUTime) > 15000000
      ) {
        // wait for 15 seconds before closing
        // this time is unnecessarily long for an established WireGuard session,
        // but is just enough time for a few re-transmissions of the wg handshake
        // which is necessary for port restricted NAT as you have to send a packet
        // to the peer before you can receive from that peer
        endpoints[i].state = Close;
      }

      switch (atomic_load(&endpoints[i].state)) {
        case Discovery:
          if (tick) tickDiscovery(i);
        case GotPeerAddr: // intentional fall-through
          pfds[i].fd = endpoints[i].sock;
          pfds[i].events = POLLIN;
          allClosed = false;
          break;
        default:
          pfds[i].fd = -1;
      }

      pfds[i].revents = 0;
    }

    if (allClosed) {
      utils_usleep(tickTimeoutUs);
      continue;
    }

    int err = poll(pfds, endpointCount, tickTimeoutMs);
    if (err == -1) {
      // This is bad, can't really do anything
      utils_usleep(tickTimeoutUs);
      continue;
    }

    for (int i = 0; i < endpointCount; i++) {
      endpoint_t *ep = &endpoints[i];

      if (pfds[i].revents == 0) continue;
      if (pfds[i].revents != POLLIN) {
        ep->state = Close;
        continue;
      }

      socklen_t recvAddrLen = sizeof(recvAddr);
      ssize_t recvLen = recvfrom(ep->sock, recvBuf, sizeof(recvBuf), 0, (struct sockaddr*)&recvAddr, &recvAddrLen);

      if (recvLen < 0 || recvAddrLen != sizeof(recvAddr)) {
        ep->state = Close;
        continue;
      }

      // this line is required if the peer has symmetric NAT, as 
      // moving from the discovery server to the peer counts as
      // a new mapping
      ep->peerPort = recvAddr.sin_port;

      ep->lastPacketUTime = utils_getCurrentUTime();

      // this is where all the magic happens for receiver
      handleRes(i, recvBuf, recvLen);
    }
  }

  return NULL;
}

/////////////////////
// init, deinit
/////////////////////

int endpoint_init (int (*onPacket)(const uint8_t*, size_t, int)) {
  endpointCount = globals_get1i(endpoints, endpointCount);
  if (endpointCount == 0) {
    printf("Endpoint: No endpoints specified!\n");
    return -1;
  }

  int err;
  _onPacket = onPacket;
  endpoints = (endpoint_t *)malloc(sizeof(endpoint_t) * endpointCount);
  memset(endpoints, 0, sizeof(endpoint_t) * endpointCount);

  char privKeyStr[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPubKeyStr[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privKeyStr, sizeof(privKeyStr));
  globals_get1s(root, peerPublicKey, peerPubKeyStr, sizeof(peerPubKeyStr));

  struct x25519_key myPrivKeyStruct = { 0 };
  err = utils_x25519Base64ToBuf(myPrivKeyStruct.key, privKeyStr);
  if (err != 0) return -2;
  err = utils_x25519Base64ToBuf(peerPubKey, peerPubKeyStr);
  if (err != 0) return -3;

  struct x25519_key myPubKeyStruct = x25519_public_key(myPrivKeyStruct);
  memcpy(myPubKey, myPubKeyStruct.key, 32);

  char ifName[MAX_NET_IF_NAME_LEN + 1] = { 0 };
  for (int i = 0; i < endpointCount; i++) {
    int ifLen = globals_get1sv(endpoints, interface, i, ifName, sizeof(ifName));
    if (ifLen <= 0) return -4;

    memcpy(endpoints[i].ifName, ifName, ifLen + 1);
    endpoints[i].state = Open;
    endpoints[i].lastPacketUTime = -1;
  }

  // Preshared keys are optional: https://www.procustodibus.com/blog/2021/09/wireguard-key-rotation/#preshared-keys
  tunnel = new_tunnel(privKeyStr, peerPubKeyStr, NULL, ENDPOINT_KEEP_ALIVE_MS, 0);
  if (tunnel == NULL) return -5;

  err = pthread_create(&openCloseThread, NULL, openCloseLoop, NULL);
  if (err != 0) return -6;

  err = pthread_create(&dataThread, NULL, dataLoop, NULL);
  if (err != 0) return -7;

  return 0;
}

void endpoint_deinit (void) {
  tunnelUp = false;
  tunnel_free(tunnel);

  threadsRunning = false;
  pthread_join(dataThread, NULL);
  pthread_join(openCloseThread, NULL);
  for (int i = 0; i < endpointCount; i++) {
    close(endpoints[i].sock);
  }

  free(endpoints);
}

