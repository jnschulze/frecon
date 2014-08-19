/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef INPUT_H
#define INPUT_H

#include <linux/input.h>

struct input_key_event {
	uint16_t code;
	unsigned char value;
};

int input_init();
void input_close();
int input_setfds(fd_set *read_set, fd_set *exception_set);
struct input_key_event *input_get_event(fd_set *read_fds, fd_set *exception_set);
void input_put_event(struct input_key_event *event);

#endif
