// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// Request format - length 65
// offset | fieldName
// 0      | myPubKey
// 32     | remotePubKey
// 64     | endpointIndex

// Response format - length 38
// offset | fieldName
// 0      | remotePubKey
// 32     | remoteAddr
// 36     | remotePort

#define SERVER_BIND_PORT 26172
#define MAX_PEERS 1000
#define MAX_ENDPOINTS 5
#define PEER_EXPIRY_TIME 4000000 // microseconds
#define RECV_LOOP_IDLE_INTERVAL 1 // second

typedef struct {
  uint8_t myPubKey[32];
  struct sockaddr_in myAddrs[MAX_ENDPOINTS];
  int lastUpdatedUTime;
} peer_t;

static peer_t peerList[MAX_PEERS] = { 0 };
static atomic_int serverSock = -1;

void utils_usleep (unsigned int us) {
  #if defined(__linux__) || defined(__ANDROID__)
  struct timespec tsp;
  tsp.tv_nsec = 1000 * us;
  tsp.tv_sec = 0;
  clock_nanosleep(CLOCK_MONOTONIC, 0, &tsp, NULL);
  #else
  usleep(us);
  #endif
}

int utils_getCurrentUTime (void) {
  struct timespec tsp = { 0 };
  // NOTE: CLOCK_MONOTONIC has been observed to jump backwards on macOS https://discussions.apple.com/thread/253778121
  clock_gettime(CLOCK_MONOTONIC_RAW, &tsp);
  return 1000000 * (tsp.tv_sec % 1000) + (tsp.tv_nsec / 1000);
}

int utils_getElapsedUTime (int lastUTime) {
  int intervalUTime = utils_getCurrentUTime() - lastUTime;
  // handle overflow
  return intervalUTime < 0 ? intervalUTime + 1000000000 : intervalUTime;
}

static bool pubKeyMatch (const uint8_t *key1, const uint8_t *key2) {
  for (int i = 0; i < 32; i++) {
    if (key1[i] != key2[i]) return false;
  }
  return true;
}

static void sendRes (const struct sockaddr_in *yourAddr, const struct sockaddr_in *remoteAddr, const uint8_t *myPubKey, const uint8_t *remotePubKey) {
  static uint8_t sendBuf[38];

  memcpy(&sendBuf[0], remotePubKey, 32);
  memcpy(&sendBuf[32], &remoteAddr->sin_addr.s_addr, 4);
  memcpy(&sendBuf[36], &remoteAddr->sin_port, 2);

  for (int i = 0; i < 6; i++) {
    // XOR remoteAddr with myPubKey
    sendBuf[32 + i] ^= myPubKey[i];
  }

  sendto(serverSock, sendBuf, sizeof(sendBuf), 0, (struct sockaddr*)yourAddr, sizeof(struct sockaddr_in));
}

static void removePeerIfExpired (peer_t *peer) {
  if (utils_getElapsedUTime(peer->lastUpdatedUTime) >= PEER_EXPIRY_TIME) {
    // Zero out everything for a bit more safety.
    memset(peer->myPubKey, 0, 32);
    for (int j = 0; j < MAX_ENDPOINTS; j++) {
      memset(&peer->myAddrs[j], 0, sizeof(struct sockaddr_in));
    }
    peer->lastUpdatedUTime = -1;
  }
}

static void handleReq (const uint8_t *buf, ssize_t bufLen, const struct sockaddr_in *addr) {
  if (bufLen != 65) return;
  if (addr->sin_family != AF_INET) return; // No IPv6 support yet.

  const uint8_t *myPubKey = &buf[0];
  const uint8_t *remotePubKey = &buf[32];
  int endpointIndex = buf[64];

  if (endpointIndex >= MAX_ENDPOINTS) return;

  int insertIndex = -1;
  bool entryUpdated = false;
  for (int i = 0; i < MAX_PEERS; i++) {
    peer_t *peer = &peerList[i];

    if (peer->lastUpdatedUTime == -1) {
      insertIndex = i;
      continue;
    }

    if (pubKeyMatch(myPubKey, peer->myPubKey)) {
      // myPubKey is already in the peerList, update its address and port
      memcpy(&peer->myAddrs[endpointIndex], addr, sizeof(struct sockaddr_in));
      peer->lastUpdatedUTime = utils_getCurrentUTime();
      entryUpdated = true;
    }

    if (pubKeyMatch(remotePubKey, peer->myPubKey)) {
      // We found the remotePubKey the requester was looking for!
      if (peer->myAddrs[endpointIndex].sin_family != 0) {
        sendRes(addr, &peer->myAddrs[endpointIndex], myPubKey, remotePubKey);
      }
    }

    removePeerIfExpired(peer);
  }

  if (!entryUpdated && insertIndex != -1) {
    // Insert a new entry for myPubKey
    memcpy(peerList[insertIndex].myPubKey, myPubKey, 32);
    memcpy(&peerList[insertIndex].myAddrs[endpointIndex], addr, sizeof(struct sockaddr_in));
    peerList[insertIndex].lastUpdatedUTime = utils_getCurrentUTime();
  }
}

int main (void) {
  static uint8_t recvBuf[1500] = { 0 };
  static struct sockaddr_in recvAddr = { 0 };

  printf("Waterslide discovery server, build 3\n");

  serverSock = socket(AF_INET, SOCK_DGRAM, 0);
  if (serverSock < 0) {
    printf("socket() failed.\n");
    return EXIT_FAILURE;
  }

  // Set recv timeout so we can still remove expired peers when no packets are being received.
  struct timeval tv;
  tv.tv_sec = RECV_LOOP_IDLE_INTERVAL;
  tv.tv_usec = 0;
  setsockopt(serverSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

  struct sockaddr_in bindAddr = { 0 };
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  bindAddr.sin_port = htons(SERVER_BIND_PORT);

  if (bind(serverSock, (const struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
    printf("bind() failed.\n");
    return EXIT_FAILURE;
  }

  printf("Bound to port %d\n", SERVER_BIND_PORT);

  for (int i = 0; i < MAX_PEERS; i++) peerList[i].lastUpdatedUTime = -1;

  while (true) {
    socklen_t recvAddrLen = sizeof(recvAddr);
    ssize_t recvLen = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (struct sockaddr*)&recvAddr, &recvAddrLen);

    if (recvLen < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Timeout reached, do periodic cleanup
      for (int i = 0; i < MAX_PEERS; i++) {
        if (peerList[i].lastUpdatedUTime != -1) removePeerIfExpired(&peerList[i]);
      }
      continue;
    }

    // If recv failed, ignore it but wait a bit first
    if (recvLen < 0 || recvAddrLen != sizeof(recvAddr)) {
      utils_usleep(10000);
      continue;
    }

    handleReq(recvBuf, recvLen, &recvAddr);
  }

  return EXIT_SUCCESS;
}
