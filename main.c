/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <getopt.h>
#include <libtsm.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dbus.h"
#include "input.h"
#include "splash.h"
#include "term.h"
#include "video.h"
#include "util.h"

#define  FLAG_CLEAR                        'c'
#define  FLAG_DAEMON                       'd'
#define  FLAG_DEV_MODE                     'e'
#define  FLAG_FRAME_INTERVAL               'f'
#define  FLAG_GAMMA                        'g'
#define  FLAG_LOOP_START                   'l'
#define  FLAG_LOOP_INTERVAL                'L'
#define  FLAG_LOOP_OFFSET                  'o'
#define  FLAG_OFFSET                       'O'
#define  FLAG_PRINT_RESOLUTION             'p'

static struct option command_options[] = {
	{ "clear", required_argument, NULL, FLAG_CLEAR },
	{ "daemon", no_argument, NULL, FLAG_DAEMON },
	{ "dev-mode", no_argument, NULL, FLAG_DEV_MODE },
	{ "frame-interval", required_argument, NULL, FLAG_FRAME_INTERVAL },
	{ "gamma", required_argument, NULL, FLAG_GAMMA },
	{ "loop-start", required_argument, NULL, FLAG_LOOP_START },
	{ "loop-interval", required_argument, NULL, FLAG_LOOP_INTERVAL },
	{ "loop-offset", required_argument, NULL, FLAG_LOOP_OFFSET },
	{ "offset", required_argument, NULL, FLAG_OFFSET },
	{ "print-resolution", no_argument, NULL, FLAG_PRINT_RESOLUTION },
	{ NULL, 0, NULL, 0 }
};

typedef struct {
	bool    print_resolution;
	bool    standalone;
	bool    devmode;
} commandflags_t;

static
void parse_offset(char* param, int32_t* x, int32_t* y)
{
	char* token;
	char* saveptr;

	token = strtok_r(param, ",", &saveptr);
	if (token)
		*x = strtol(token, NULL, 0);

	token = strtok_r(NULL, ",", &saveptr);
	if (token)
		*y = strtol(token, NULL, 0);
}

int main(int argc, char* argv[])
{
	int ret;
	int c;
	int i;
	int32_t x, y;
	splash_t *splash;
	video_t  *video;
	dbus_t *dbus;
	commandflags_t command_flags;

	memset(&command_flags, 0, sizeof(command_flags));
	command_flags.standalone = true;

	ret = input_init();
	if (ret) {
		LOG(ERROR, "Input init failed");
		return EXIT_FAILURE;
	}

	splash = splash_init();
	if (splash == NULL) {
		LOG(ERROR, "splash init failed");
		return EXIT_FAILURE;
	}

	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);

		if (c == -1)
			break;

		switch (c) {
			case FLAG_CLEAR:
				splash_set_clear(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_DAEMON:
				daemonize();
				command_flags.standalone = false;
				break;

			case FLAG_FRAME_INTERVAL:
				splash_set_default_duration(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_DEV_MODE:
				command_flags.devmode = true;
				splash_set_devmode(splash);
				break;

			case FLAG_LOOP_START:
				splash_set_loop_start(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_LOOP_INTERVAL:
				splash_set_loop_duration(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_LOOP_OFFSET:
				parse_offset(optarg, &x, &y);
				splash_set_loop_offset(splash, x, y);
				break;

			case FLAG_OFFSET:
				parse_offset(optarg, &x, &y);
				splash_set_offset(splash, x, y);
				break;

			case FLAG_PRINT_RESOLUTION:
				command_flags.print_resolution = true;
				break;
		}
	}

	for (i = optind; i < argc; i++)
		splash_add_image(splash, argv[i]);

	/*
	 * The DBUS service launches later than the boot-splash service, and
	 * as a result, when splash_run starts dbus is not yet up, but, by
	 * the time splash_run completes, it is running.  At the same time,
	 * splash_run needs dbus to determine when chrome is visible.  So,
	 * it creates the dbus object and then passes it back to the caller
	 * who can then pass it to the other objects that need it
	 */
	dbus = NULL;
	if (command_flags.print_resolution) {
		video = video_init();
		printf("%d %d", video_getwidth(video), video_getheight(video));
		return EXIT_SUCCESS;
	}
	else if (splash_num_images(splash) > 0) {
		ret = splash_run(splash, &dbus);
		if (ret) {
				LOG(ERROR, "splash_run failed: %d", ret);
				return EXIT_FAILURE;
		}
	}

	/*
	 * If splash_run didn't create the dbus object (for example, if
	 * there are no splash screen images), then go ahead and create
	 * it now
	 */
	if (dbus == NULL) {
		dbus = dbus_init();
	}
	input_set_dbus(dbus);

	ret = input_run(command_flags.standalone);

	input_close();

	return ret;
}
