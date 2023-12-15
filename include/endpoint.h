// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _ENDPOINT_H
#define _ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

// Discovery, GotPeerAddr: dataLoop has control of endpoint
// Open, Close, WaitForReopen: openCloseLoop has control of endpoint
enum endpoint_state { Open, Discovery, GotPeerAddr, Close, WaitForReopen };

typedef struct {
  _Atomic enum endpoint_state state;
  int sock;
  char ifName[MAX_NET_IF_NAME_LEN + 1];
  uint32_t peerAddr;
  uint16_t peerPort;
  int reopenTickCounter;
  int discoveryTickCounter;
  int lastPacketUTime;
} endpoint_t;

int endpoint_init (int (*onPacket)(const uint8_t*, int, int));
int endpoint_send (const uint8_t *buf, int bufLen); // NOTE: this is not thread safe
void endpoint_deinit (void);

#endif
