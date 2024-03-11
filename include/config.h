// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _CONFIG_H
#define _CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

// called publicly by both
int config_init (const char *b64ConfigStr);

// called publicly by receiver only
// may cause a non thread-safe mixer update on Linux
int config_parseBuf (const uint8_t *buf, size_t bufLen);

// called publicly by sender only
// sender must call config_init first
// allocates memory for destBuf and returns its length
// caller must free destBuf
int config_encodeReceiverConfig (uint8_t **destBuf);

// called publicly by both
// may cause a non thread-safe mixer update on Linux
int config_deinit (void);

#ifdef __cplusplus
}
#endif
#endif
