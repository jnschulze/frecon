/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <libtsm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"

static int fd;

int input_init()
{
	int ret;

	/* XXX open all evdev devices and poll all the FDs */
	fd = open("/dev/input/event0", O_RDONLY);
	if (fd < 0)
		return fd;

	if (!isatty(fileno(stdout)))
		setbuf(stdout, NULL);

	/* Check for grabs. */
	ret = ioctl(fd, EVIOCGRAB, (void *) 1);

	if (!ret) {
		ioctl(fd, EVIOCGRAB, (void *) 0);
	} else {
		printf("Evdev device grabbed by another process\n");
		close(fd);
		return -1;
	}

	return 0;
}

void input_close()
{
	close(fd);
}

struct input_key_event *input_get_event()
{
	struct input_event ev;
	int ret;

	ret = read(fd, &ev, sizeof (struct input_event));
	if (ret < (int) sizeof (struct input_event)) {
		printf("expected %d bytes, got %d\n",
		       (int) sizeof (struct input_event), ret);
		return NULL;
	}

	if (ev.type == EV_KEY) {
		struct input_key_event *event = malloc(sizeof (*event));
		event->code = ev.code;
		event->value = ev.value;
		return event;
	}

	return NULL;
}

void input_put_event(struct input_key_event *event)
{
	free(event);
}

int input_get_fd()
{
	return fd;
}
