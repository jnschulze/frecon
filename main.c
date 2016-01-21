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
#include "dbus_interface.h"
#include "input.h"
#include "main.h"
#include "splash.h"
#include "term.h"
#include "util.h"
#include "video.h"

#define  FLAG_CLEAR                        'c'
#define  FLAG_DAEMON                       'd'
#define  FLAG_DEV_MODE                     'e'
#define  FLAG_FRAME_INTERVAL               'f'
#define  FLAG_GAMMA                        'g'
#define  FLAG_IMAGE                        'i'
#define  FLAG_IMAGE_HIRES                  'I'
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
	{ "image", required_argument, NULL, FLAG_IMAGE },
	{ "image-hires", required_argument, NULL, FLAG_IMAGE_HIRES },
	{ "loop-start", required_argument, NULL, FLAG_LOOP_START },
	{ "loop-interval", required_argument, NULL, FLAG_LOOP_INTERVAL },
	{ "loop-offset", required_argument, NULL, FLAG_LOOP_OFFSET },
	{ "offset", required_argument, NULL, FLAG_OFFSET },
	{ "print-resolution", no_argument, NULL, FLAG_PRINT_RESOLUTION },
	{ NULL, 0, NULL, 0 }
};

typedef struct {
	bool    standalone;
} commandflags_t;

static void parse_offset(char* param, int32_t* x, int32_t* y)
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

int main_process_events(uint32_t usec)
{
	terminal_t* terminal;
	terminal_t* new_terminal;
	fd_set read_set, exception_set;
	int maxfd = -1;
	int sstat;
	struct timeval tm;
	struct timeval* ptm;

	terminal = term_get_current_terminal();

	FD_ZERO(&read_set);
	FD_ZERO(&exception_set);

	dbus_add_fds(&read_set, &exception_set, &maxfd);

	input_add_fds(&read_set, &exception_set, &maxfd);

	for (int i = 0; i < MAX_TERMINALS; i++) {
		if (term_is_valid(term_get_terminal(i))) {
			terminal_t* current_term = term_get_terminal(i);
			term_add_fds(current_term, &read_set, &exception_set, &maxfd);
		}
	}

	if (usec) {
		ptm = &tm;
		tm.tv_sec = 0;
		tm.tv_usec = usec;
	} else
		ptm = NULL;

	sstat = select(maxfd + 1, &read_set, NULL, &exception_set, ptm);
	if (sstat == 0)
		return 0;

	dbus_dispatch_io();

	if (term_exception(terminal, &exception_set))
		return -1;

	input_dispatch_io(&read_set, &exception_set);

	for (int i = 0; i < MAX_TERMINALS; i++) {
		if (term_is_valid(term_get_terminal(i))) {
			terminal_t* current_term = term_get_terminal(i);
			term_dispatch_io(current_term, &read_set);
		}
	}

	if (term_is_valid(terminal)) {
		if (term_is_child_done(terminal)) {
			if (terminal == term_get_terminal(SPLASH_TERMINAL)) {
				/*
				 * Note: reference is not lost because it is still referenced
				 * by the splash_t structure which will ultimately destroy
				 * it, once it's safe to do so
				 */
				term_set_terminal(SPLASH_TERMINAL, NULL);
				return -1;
			}
			term_set_current_terminal(term_init(true, term_getvideo(terminal)));
			new_terminal = term_get_current_terminal();
			if (!term_is_valid(new_terminal)) {
				return -1;
			}
			term_activate(new_terminal);
			term_close(terminal);
		}
	}

	return 0;
}

int main_loop(bool standalone)
{
	terminal_t* terminal;
	int status;

	if (standalone) {
		dbus_take_display_ownership();
		term_set_current_terminal(term_init(true, NULL));
		terminal = term_get_current_terminal();
		term_activate(terminal);
	}

	while (1) {
		status = main_process_events(0);
		if (status != 0) {
			LOG(ERROR, "input process returned %d", status);
			break;
		}
	}

	return 0;
}


int main(int argc, char* argv[])
{
	int ret;
	int c;
	int32_t x, y;
	splash_t* splash;
	commandflags_t command_flags;

	/* Handle resolution special before splash init */
	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);
		if (c == -1) {
			break;
		} else if (c == FLAG_PRINT_RESOLUTION) {
			video_t *video = video_init();
			if (!video)
				return EXIT_FAILURE;

			printf("%d %d", video_getwidth(video),
			       video_getheight(video));
			video_close(video);
			return EXIT_SUCCESS;
		}
	}

	/* Reset option parsing */
	optind = 1;

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
				command_flags.standalone = false;
				break;

			case FLAG_FRAME_INTERVAL:
				splash_set_default_duration(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_DEV_MODE:
				splash_set_devmode(splash);
				break;

			case FLAG_IMAGE:
				if (!splash_is_hires(splash))
					splash_add_image(splash, optarg);
				break;

			case FLAG_IMAGE_HIRES:
				if (splash_is_hires(splash))
					splash_add_image(splash, optarg);
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
		}
	}

	for (int i = optind; i < argc; i++)
		splash_add_image(splash, argv[i]);

	/*
	 * The DBUS service launches later than the boot-splash service, and
	 * as a result, when splash_run starts dbus is not yet up, but, by
	 * the time splash_run completes, it is running.  At the same time,
	 * splash_run needs dbus to determine when chrome is visible.  So,
	 * it creates the dbus object and then passes it back to the caller
	 * who can then pass it to the other objects that need it
	 */
	if (command_flags.standalone == false) {
		splash_present_term_file(splash);
		daemonize();
	}
	if (splash_num_images(splash) > 0) {
		ret = splash_run(splash);
		if (ret) {
			LOG(ERROR, "splash_run failed: %d", ret);
			return EXIT_FAILURE;
		}
	}
	splash_destroy(splash);

	/*
	 * If splash_run didn't create the dbus object (for example, if
	 * there are no splash screen images), then go ahead and create
	 * it now
	 */
	if (!dbus_is_initialized()) {
		dbus_init();
	}

	ret = main_loop(command_flags.standalone);

	input_close();
	dbus_destroy();

	return ret;
}
