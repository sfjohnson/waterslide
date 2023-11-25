// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _CONFIG_H
#define _CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

// These must only be called from the main thread.
int config_init (const char *b64ConfigStr);
int config_deinit (void);

#ifdef __cplusplus
}
#endif
#endif
