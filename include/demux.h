// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _DEMUX_H
#define _DEMUX_H

#include <pthread.h>
#include <stdint.h>

// Definitions:
// block: a buffer of data that is error corrected (consistent with RaptorQ terminology).
// symbol: an equal-sized partition of a block (consistent with RaptorQ terminology).
// chunk: a symbol with its Payload ID (this is called a packet in RaptorQ). chunkLen := 4 + symbolLen
// Payload ID: 1 byte Source Block Number (SBN) + 3 byte Encoded Symbol Identifier (ESI).
// SBN: like a sequence number but for an entire block.
// ESI: used internally by RaptorQ to identify lost / out-of-order symbols.
// packet: what is actually sent/received over the network, consists of one or more chunks each with a chId.
// chId: channel ID.
// channel: a stream of data, error corrected indepentenly from the other channels.

typedef struct {
  uint64_t chId;
  // symbolLen must be: 64, 128, 256, 512 or 1024
  int symbolsPerBlock, symbolLen;
  void (*onBlock)(const uint8_t *, int);
  // private
  uint8_t *_blockBuf;
} demux_channel_t;

int demux_init (void);
int demux_addChannel (demux_channel_t *channel);
int demux_readPacket (const uint8_t *buf, int bufLen, int endpointIndex);

#endif
