// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _ENDPOINT_H
#define _ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

int endpoint_init (int (*onPacket)(const uint8_t*, int, int)); // NOTE: this will block until network discovery is completed
int endpoint_send (const uint8_t *buf, int bufLen); // NOTE: this is not thread safe
void endpoint_deinit (void);

#endif
