/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MAIN_H
#define MAIN_H

#define MAX_TERMINALS    (5)

typedef struct {
		bool    print_resolution;
		bool    frame_interval;
		bool    standalone;
		char    **exec[MAX_TERMINALS];
} commandflags_t;

extern commandflags_t command_flags;

#endif