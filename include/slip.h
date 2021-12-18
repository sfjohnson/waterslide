#ifndef _SLIP_H
#define _SLIP_H

#include <stdint.h>

// a dry run of slip_frame that returns the frame length without writing anything
int slip_frameLength (const uint8_t *data, int dataLen);

// dest must be large enough, use slip_frameLength first
int slip_frame (const uint8_t *src, int srcLen, uint8_t *dest);

// srcLen is number of bytes in src, not including ENDs
// src should not include END bytes
// returns number of bytes written into dest
// dest must be at least srcLen in length
int slip_deframe (const uint8_t *src, int srcLen, uint8_t *dest);

#endif
