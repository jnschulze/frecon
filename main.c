/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
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
#include "main.h"
#include "splash.h"
#include "term.h"
#include "video.h"
#include "util.h"

#define  FLAG_CLEAR                        'c'
#define  FLAG_DAEMON                       'd'
#define  FLAG_DEV_MODE                     'e'
#define  FLAG_FRAME_INTERVAL               'f'
#define  FLAG_GAMMA                        'g'
#define  FLAG_PRINT_RESOLUTION             'p'

static struct option command_options[] = {
	{ "clear", required_argument, NULL, FLAG_CLEAR },
	{ "daemon", no_argument, NULL, FLAG_DAEMON },
	{ "dev-mode", no_argument, NULL, FLAG_DEV_MODE },
	{ "frame-interval", required_argument, NULL, FLAG_FRAME_INTERVAL },
	{ "gamma", required_argument, NULL, FLAG_GAMMA },
	{ "print-resolution", no_argument, NULL, FLAG_PRINT_RESOLUTION },
	{ NULL, 0, NULL, 0 }
};

commandflags_t command_flags;

static char *default_cmd_line[] = {
	"/sbin/agetty",
	"-",
	"9600",
	"xterm",
	NULL
};

int main(int argc, char* argv[])
{
	int ret;
	int c;
	int i;
	splash_t *splash;
	video_t  *video;
	dbus_t *dbus;

	memset(&command_flags, 0, sizeof(command_flags));
	command_flags.standalone = true;

	ret = input_init();
	if (ret) {
		LOG(ERROR, "Input init failed");
		return EXIT_FAILURE;
	}

	sync_lock(true);
	splash = splash_init();
	if (splash == NULL) {
		LOG(ERROR, "splash init failed");
		return EXIT_FAILURE;
	}

	for (i = 0; i < MAX_TERMINALS; i++)
		command_flags.exec[i] = default_cmd_line;

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

			case FLAG_DEV_MODE:
				splash_set_devmode(splash);
				break;

			case FLAG_PRINT_RESOLUTION:
				command_flags.print_resolution = true;
				break;

			case FLAG_FRAME_INTERVAL:
				splash_set_frame_rate(splash, strtoul(optarg, NULL, 0));
				for (i = optind; i < argc; i++) {
					 splash_add_image(splash, argv[i]);
				}
				command_flags.frame_interval = true;
				break;
		}
	}

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
	else if (command_flags.frame_interval) {
		ret = splash_run(splash, &dbus);
		if (ret) {
				LOG(ERROR, "splash_run failed: %d", ret);
				return EXIT_FAILURE;
		}
	}

	/*
	 * If splash_run didn't create the dbus object (for example, if
	 * we didn't supply the frame-interval parameter, then go ahead
	 * and create it now
	 */
	if (dbus == NULL) {
		dbus = dbus_init();
	}

	input_set_dbus(dbus);
	ret = input_run(command_flags.standalone);

	input_close();

	return ret;
}
