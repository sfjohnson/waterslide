// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "boringtun/wireguard_ffi.h"
#include "globals.h"
#include "utils.h"
#include "wsocket.h"
#include "endpoint.h"

#define WG_READ_BUF_LEN 1500

static int endpointCount = 0;
static wsocket_t *sockets = NULL; // length: endpointCount
static pthread_t *discoveryThreads = NULL; // length: endpointCount
static pthread_t tickThread;
static _Atomic(struct wireguard_tunnel *)tunnel = NULL; // _Atomic specifier protects tunnel var when being set to or from NULL. The struct fields are protected by a mutex in the Rust code.
static atomic_bool tunnelUp = false;
static uint8_t *wgReadBuf;
static int (*_onPacket)(const uint8_t*, int, int) = NULL;

static void sendBufToAll (const uint8_t *buf, int bufLen) {
  int err;
  for (int i = 0; i < endpointCount; i++) {
    err = wsocket_sendToPeer(&sockets[i], buf, bufLen);
    if (err == -1) continue; // wsocket is not ready to send yet
    if (err != 0) {
      // DEBUG: send error, do something?
      continue;
    }

    // Accounts for IP and UDP headers
    // TODO: This assumes IPv4
    globals_add1uiv(statsEndpoints, bytesOut, i, bufLen + 28);
  }
}

static void *tickLoop (UNUSED void *arg) {
  uint8_t tickBuf[1500] = { 0 };
  struct wireguard_result result;

  // Calling wireguard_tick locks tunnel which onPeerPacket and endpoint_send contend,
  // and their work is more important, so bump this thread up to prevent priority inversion.
  #if defined(__linux__) || defined(__ANDROID__)
  utils_setCallerThreadRealtime(98, 0);
  #elif defined(__APPLE__)
  utils_setCallerThreadPrioHigh();
  #endif

  while (tunnel != NULL) {
    // for (int i = 0; i < endpointCount; i++) {
    //   if (sockets[i].timeToReopen == 1) {
    //     if (openSocket(i) < 0) {
    //       sockets[i].timeToReopen = ENDPOINT_REOPEN_INTERVAL;
    //     } else {
    //       sockets[i].packedSrcAddr = 0;
    //       sockets[i].timeToReopen = 0;
    //       globals_set1uiv(statsEndpoints, open, i, 1);
    //     }
    //   } else if (sockets[i].timeToReopen > 1) {
    //     sockets[i].timeToReopen--;
    //   }
    // }

    if (!tunnelUp) {
      struct stats tunnelStats = wireguard_stats(tunnel);
      if (tunnelStats.time_since_last_handshake >= 0) {
        tunnelUp = true;
      }
    }

    result = wireguard_tick(tunnel, tickBuf, sizeof(tickBuf));
    if (result.op == WRITE_TO_NETWORK) sendBufToAll(tickBuf, result.size);

    utils_usleep(ENDPOINT_TICK_INTERVAL);
  }

  return NULL;
}

// NOTE: this is called by multiple threads
static int onPeerPacket (const uint8_t *buf, int bufLen, int epIndex) {
  if (tunnel == NULL) return 0;

  // Accounts for IP and UDP headers
  // TODO: This assumes IPv4
  globals_add1uiv(statsEndpoints, bytesIn, epIndex, bufLen + 28);

  // Use a slice of wgReadBuf so multiple receive threads aren't fighting over the same memory.
  uint8_t *dstBuf = &wgReadBuf[WG_READ_BUF_LEN * epIndex];
  while (true) {
    struct wireguard_result result = wireguard_read(tunnel, buf, bufLen, dstBuf, WG_READ_BUF_LEN);

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
        if (result.size > 20 && _onPacket != NULL) {
          _onPacket(dstBuf + 20, result.size - 20, epIndex);
        }
        return 0;

      case WRITE_TO_NETWORK:
        if (result.size > 0) sendBufToAll(dstBuf, result.size);
        bufLen = 0;
        break;

      default:
        return 0;
    }
  }

  return 0;
}

static void *startDiscovery (void *arg) {
  intptr_t epIndex = (intptr_t)arg;
  wsocket_waitForPeerAddr(&sockets[epIndex]);

  // DEBUG: inet_ntop fails on Android
  char addrString[15] = { 0 };
  inet_ntop(AF_INET, &sockets[epIndex].peerAddr, addrString, sizeof(addrString));
  printf("(epIndex %d) got peer addr %s:%d\n", epIndex, addrString, ntohs(sockets[epIndex].peerPort));

  return NULL;
}

int endpoint_init (int (*onPacket)(const uint8_t*, int, int)) {
  endpointCount = globals_get1i(endpoints, endpointCount);
  if (endpointCount == 0) {
    printf("Endpoint: No endpoints specified!\n");
    return -1;
  }

  int err;
  _onPacket = onPacket;
  sockets = (wsocket_t *)malloc(sizeof(wsocket_t) * endpointCount);
  discoveryThreads = (pthread_t *)malloc(sizeof(pthread_t) * endpointCount);
  wgReadBuf = (uint8_t *)malloc(WG_READ_BUF_LEN * endpointCount);

  memset(sockets, 0, sizeof(wsocket_t) * endpointCount);

  char privKeyStr[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPubKeyStr[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privKeyStr, sizeof(privKeyStr));
  globals_get1s(root, peerPublicKey, peerPubKeyStr, sizeof(peerPubKeyStr));

  struct x25519_key myPrivKey = { 0 };
  struct x25519_key peerPubKey = { 0 };
  err = utils_x25519Base64ToBuf(myPrivKey.key, privKeyStr);
  if (err != 0) return -2;
  err = utils_x25519Base64ToBuf(peerPubKey.key, peerPubKeyStr);
  if (err != 0) return -3;

  struct x25519_key myPubKey = x25519_public_key(myPrivKey);

  char ifName[MAX_NET_IF_NAME_LEN + 1] = { 0 };
  for (int i = 0; i < endpointCount; i++) {
    int ifLen = globals_get1sv(endpoints, interface, i, ifName, sizeof(ifName));
    if (ifLen <= 0) return -4;
    err = wsocket_init(&sockets[i], myPubKey.key, peerPubKey.key, i, ifName, ifLen, onPeerPacket);
    if (err < 0) return err - 4;
  }

  for (intptr_t i = 0; i < endpointCount; i++) {
    if (pthread_create(&discoveryThreads[i], NULL, startDiscovery, (void*)i) != 0) return -15;
  }

  for (intptr_t i = 0; i < endpointCount; i++) {
    if (pthread_join(discoveryThreads[i], NULL) != 0) return -16;
  }

  // All endpoints are now in GotPeerAddr state

  // Preshared keys are optional: https://www.procustodibus.com/blog/2021/09/wireguard-key-rotation/#preshared-keys
  tunnel = new_tunnel(privKeyStr, peerPubKeyStr, NULL, SEC_KEEP_ALIVE_INTERVAL, 0);
  if (tunnel == NULL) return -17;

  err = pthread_create(&tickThread, NULL, tickLoop, NULL);
  if (err != 0) return -18;

  // Set all the endpoints to open for stats
  for (int i = 0; i < endpointCount; i++) {
    globals_set1uiv(statsEndpoints, open, i, 1);
  }

  return 0;
}

// NOTE: this function is not thread safe due to srcBuf and dstBuf being static.
int endpoint_send (const uint8_t *buf, int bufLen) {
  if (tunnel == NULL || !tunnelUp) return -1;

  // This buffer starts with a fake IPv4 header that passes BoringTun's packet checks.
  static uint8_t srcBuf[1500] = { 0x45, 0x00, 0x00, 0x00 };
  static const int maxSrcDataLen = sizeof(srcBuf) - 20;
  static uint8_t dstBuf[1500] = { 0 };

  if (bufLen > maxSrcDataLen) return -2;

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

void endpoint_deinit (void) {
  tunnelUp = false;
  if (tunnel != NULL) tunnel_free(tunnel);

  // TODO
  // if (threadsRunning) {
  //   threadsRunning = false;
  //   pthread_join(tickThread, NULL);
  //   for (int i = 0; i < endpointCount; i++) {
  //     pthread_join(recvThreads[i], NULL);
  //     close(sockets[i].socket);
  //   }
  //   free(sockets);
  // }
}
