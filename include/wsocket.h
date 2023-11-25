// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

enum wsocket_state { Idle, GotPeerAddr };

typedef struct {
  int sock;
  uint8_t myPubKey[32];
  uint8_t peerPubKey[32];
  int epIndex;
  _Atomic enum wsocket_state state;
  atomic_uint_fast32_t peerAddr;
  atomic_uint_fast16_t peerPort;
  pthread_t recvThread;
  int (*onPacket)(const uint8_t *, int, int);
} wsocket_t;

const char *wsocket_getErrorStr (int returnCode);

// sock: wsocket_t struct to initialise, allocated and freed by caller
// myPubKey: My x25519 public key as 32 bytes
// peerPubKey: Peer's x25519 public key as 32 bytes
// epIndex: Endpoint index starting at 0
// ifName: Unix name of the interface to bind to e.g. "eth0". Must be null-terminated
// ifLen: Length of ifName not including null terminator e.g. 4 for "eth0"
// onPacket: function that is called when a packet is received from the corresponding remote wsocket with the same epIndex
// returns: 0 for success, negative for error code
int wsocket_init (wsocket_t *sock, const uint8_t *myPubKey, const uint8_t *peerPubKey, int epIndex, const char *ifName, int ifLen, int (*onPacket)(const uint8_t *, int, int));

// sock: initialised wsocket_t struct, allocated and freed by caller
// serverAddrStr: discovery server IP address as null-terminated string
// serverPort: server destination port
// returns: 0 for success, negative for error code
int wsocket_waitForPeerAddr (wsocket_t *sock);

int wsocket_sendToPeer (const wsocket_t *sock, const uint8_t *buf, int bufLen);
