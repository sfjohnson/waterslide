// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _MUX_H
#define _MUX_H

#include <stdint.h>

typedef struct {
  uint64_t chId;
  int chunksPerBlock, chunkLen; // chunksPerBlock includes repair symbols
  uint8_t *blockBuf; // sizeof(blockBuf) := chunksPerBlock * chunkLen
  // private
  int _currentChunk;
} mux_channel_t;

typedef struct {
  uint8_t flags;
  int chCount;
  mux_channel_t *channels;
} mux_transfer_t;

int mux_init (void);
int mux_initTransfer (mux_transfer_t *transfer);
void mux_deinitTransfer (mux_transfer_t *transfer);
void mux_resetTransfer (mux_transfer_t *transfer);
int mux_setChannel (mux_transfer_t *transfer, uint64_t chId, int chunksPerBlock, int chunkLen, uint8_t *blockBuf);
// emitPackets also resets transfer once all packets are emitted
int mux_emitPackets (mux_transfer_t *transfer, int (*onPacket)(const uint8_t *, int)); // NOTE: this is not thread safe

#endif
