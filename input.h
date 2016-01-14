/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INPUT_H
#define INPUT_H

#include <linux/input.h>

#include "term.h"
#include "video.h"

int input_init();
void input_close();
int input_add_fds(fd_set* read_set, fd_set* exception_set);
void input_dispatch_io(fd_set* read_set, fd_set* exception_set);

#endif
