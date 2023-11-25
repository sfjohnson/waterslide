// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _EVENTRECORDER_H
#define _EVENTRECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// NOTE: after calling eventrecorder_writeFile, you need to call eventrecorder_init again before eventrecorder_event1i

int eventrecorder_init (void);
int eventrecorder_event1i (int32_t id, int32_t val); // this is thread safe and realtime safe (no syscalls)
int eventrecorder_writeFile (const char *filename); // this is not

#ifdef __cplusplus
}
#endif

#endif
