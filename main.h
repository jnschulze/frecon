/*
 * Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MAIN_H
#define MAIN_H

int main_process_events(uint32_t usec);

typedef struct {
	bool    daemon;
	bool    enable_vts;
} commandflags_t;

extern commandflags_t command_flags;

#endif