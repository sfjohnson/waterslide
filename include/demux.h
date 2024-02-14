// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _DEMUX_H
#define _DEMUX_H

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

void demux_deinit (void);

// each time demux_addChannel is called a new thread is created, so each onData is called from a different thread
// symbolLen must be: 64, 128, 256, 512 or 1024
// additional channels may be added after calling demux_readPacket
int demux_addChannel (int maxDataLen, int sourceSymbolsPerBlock, int repairSymbolsPerBlock, int symbolLen, void (*onData)(const uint8_t *, int));

// call demux_readPacket from one thread only (RT network thread); the data is passed to other threads for decoding
int demux_readPacket (const uint8_t *buf, size_t bufLen, int endpointIndex);

#endif
