// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _RECEIVER_H
#define _RECEIVER_H

int receiver_init (void);
int receiver_waitForConfig (void);
int receiver_deinit (void);

#endif
