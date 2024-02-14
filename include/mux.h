// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _MUX_H
#define _MUX_H

#include <stdint.h>

// NOTES:
//
// See demux.h for definitions.
//
// The terminology used in the mux module is consistent with the demux module. The buf passed into
// mux_writeData is called a "packet" in Opus terminology, but this clashes with the terminology used in
// demux, which refers to a packet as an actual network packet (what you pass to sendto(2)).
//
// The buf passed to mux_writeData must not be larger than one block less length fields
// dataBufLen <= (symbolLen * sourceSymbolsPerBlock - 8)
//
// A block before FEC encoding is arranged as follows:
// length  |  data type    |  description
// ------------------------------------
// 4       |  uint32_t LE  |  length of the remaining piece of current data
// 0 to n  |               |  current data
// 4       |  uint32_t LE  |  length of the next piece of data
// 1 to n  |               |  next data
// with the last two fields repeating as necessary
// uint32_t fields must not be larger than 2^31 - 1 (essentially int32_t with sign bit zero)
//
// One chunk from each channel (4+symbolLen) plus mux protocol overhead must be <= maxPacketSize

// onPacket will be called by the packet thread only
int mux_init (int (*onPacket)(const uint8_t *, size_t));
void mux_deinit (void);

// symbolLen must be: 64, 128, 256, 512 or 1024
// returns chId or negative error
int mux_addChannel (int maxDataLen, int sourceSymbolsPerBlock, int repairSymbolsPerBlock, int symbolLen);

// call this once before mux_writeData
// the anchor channel should be the channel that consistently has the highest chunk / sec rate e.g. video
// note that the chunk / sec rate includes both source and repair symbols
// if the highest rate channel changes frequently (e.g. due to VBR encoding), the other channels may be
// starved, and this could cause overflows in the chunk ring in mux, and underflows in demux
// in this case we need to design a more sophisticated approach to packet timing
int mux_setAnchorChannel (uint8_t chId);

// call this from one thread per chId
// first each buf passed is arranged as above into a block, then when a block is full
// FEC encoding is done in the calling thread, then the block is sent to the packet thread
// (one packet thread for all channels)
// bufLen must be <= maxDataLen for the corresponding chId
int mux_writeData (uint8_t chId, const uint8_t *dataBuf, int dataBufLen);

#endif
