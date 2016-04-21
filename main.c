/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <getopt.h>
#include <libtsm.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dbus.h"
#include "dbus_interface.h"
#include "dev.h"
#include "input.h"
#include "main.h"
#include "splash.h"
#include "term.h"
#include "util.h"

#define  FLAG_CLEAR                        'c'
#define  FLAG_DAEMON                       'd'
#define  FLAG_ENABLE_VTS                   'e'
#define  FLAG_FRAME_INTERVAL               'f'
#define  FLAG_GAMMA                        'g'
#define  FLAG_IMAGE                        'i'
#define  FLAG_IMAGE_HIRES                  'I'
#define  FLAG_LOOP_COUNT                   'C'
#define  FLAG_LOOP_START                   'l'
#define  FLAG_LOOP_INTERVAL                'L'
#define  FLAG_LOOP_OFFSET                  'o'
#define  FLAG_OFFSET                       'O'
#define  FLAG_PRINT_RESOLUTION             'p'
#define  FLAG_SPLASH_ONLY                  's'

static struct option command_options[] = {
	{ "clear", required_argument, NULL, FLAG_CLEAR },
	{ "daemon", no_argument, NULL, FLAG_DAEMON },
	{ "dev-mode", no_argument, NULL, FLAG_ENABLE_VTS },
	{ "enable-vts", no_argument, NULL, FLAG_ENABLE_VTS },
	{ "frame-interval", required_argument, NULL, FLAG_FRAME_INTERVAL },
	{ "gamma", required_argument, NULL, FLAG_GAMMA },
	{ "image", required_argument, NULL, FLAG_IMAGE },
	{ "image-hires", required_argument, NULL, FLAG_IMAGE_HIRES },
	{ "loop-count", required_argument, NULL, FLAG_LOOP_COUNT },
	{ "loop-start", required_argument, NULL, FLAG_LOOP_START },
	{ "loop-interval", required_argument, NULL, FLAG_LOOP_INTERVAL },
	{ "loop-offset", required_argument, NULL, FLAG_LOOP_OFFSET },
	{ "offset", required_argument, NULL, FLAG_OFFSET },
	{ "print-resolution", no_argument, NULL, FLAG_PRINT_RESOLUTION },
	{ "splash-only", no_argument, NULL, FLAG_SPLASH_ONLY },
	{ NULL, 0, NULL, 0 }
};

commandflags_t command_flags = { 0 };

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
	dev_add_fds(&read_set, &exception_set, &maxfd);

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

	dev_dispatch_io(&read_set, &exception_set);
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
				 * it, once it's safe to do so.
				 */
				term_set_terminal(SPLASH_TERMINAL, NULL);
				return -1;
			}
			term_set_current_terminal(term_init(true));
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

int main_loop(void)
{
	int status;

	while (1) {
		status = main_process_events(0);
		if (status != 0) {
			LOG(ERROR, "Input process returned %d.", status);
			break;
		}
	}

	return 0;
}

bool set_drm_master_relax(void)
{
	int fd;
	int num_written;

	/*
	 * Setting drm_master_relax flag in kernel allows us to transfer DRM master
	 * between Chrome and frecon.
	 */
	fd = open("/sys/kernel/debug/dri/drm_master_relax", O_WRONLY);
	if (fd != -1) {
		num_written = write(fd, "Y", 1);
		close(fd);
		if (num_written != 1) {
			LOG(ERROR, "Unable to set drm_master_relax.");
			return false;
		}
	} else {
		LOG(ERROR, "Unable to open drm_master_relax.");
		return false;
	}
	return true;
}

static void main_on_login_prompt_visible(void* ptr)
{
	if (command_flags.daemon && !command_flags.enable_vts) {
		LOG(INFO, "Chrome started, our work is done, exiting.");
		exit(EXIT_SUCCESS);
	} else
	if (ptr) {
		LOG(INFO, "Chrome started, splash screen is not needed anymore.");
		splash_destroy((splash_t*)ptr);
	}
}

int main(int argc, char* argv[])
{
	int ret;
	int c;
	int32_t x, y;
	splash_t* splash;
	drm_t* drm;

	/* Find out if we are going to be a daemon .*/
	optind = 1;
	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);
		if (c == -1) {
			break;
		} else if (c == FLAG_DAEMON) {
			command_flags.daemon = true;
		}
	}

	/* Handle resolution special before splash init. */
	optind = 1;
	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);
		if (c == -1) {
			break;
		} else if (c == FLAG_PRINT_RESOLUTION) {
			drm_t *drm = drm_scan();
			if (!drm)
				return EXIT_FAILURE;

			printf("%d %d", drm_gethres(drm),
			       drm_getvres(drm));
			drm_delref(drm);
			return EXIT_SUCCESS;
		}
	}

	splash = splash_init();
	if (splash == NULL) {
		LOG(ERROR, "Splash init failed.");
		return EXIT_FAILURE;
	}

	if (command_flags.daemon) {
		splash_present_term_file(splash);
		daemonize();
	}

	ret = input_init();
	if (ret) {
		LOG(ERROR, "Input init failed.");
		return EXIT_FAILURE;
	}

	ret = dev_init();
	if (ret) {
		LOG(ERROR, "Device management init failed.");
		return EXIT_FAILURE;
	}

	drm_set(drm = drm_scan());
	/* Update DRM object in splash term and set video mode. */
	splash_redrm(splash);

	optind = 1;
	for (;;) {
		c = getopt_long(argc, argv, "", command_options, NULL);

		if (c == -1)
			break;

		switch (c) {
			case FLAG_CLEAR:
				splash_set_clear(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_FRAME_INTERVAL:
				splash_set_default_duration(splash, strtoul(optarg, NULL, 0));
				break;

			case FLAG_ENABLE_VTS:
				command_flags.enable_vts = true;
				break;

			case FLAG_IMAGE:
				if (!splash_is_hires(splash))
					splash_add_image(splash, optarg);
				break;

			case FLAG_IMAGE_HIRES:
				if (splash_is_hires(splash))
					splash_add_image(splash, optarg);
				break;

			case FLAG_LOOP_COUNT:
				splash_set_loop_count(splash, strtoul(optarg, NULL, 0));
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

			case FLAG_SPLASH_ONLY:
				command_flags.splash_only = true;
				break;
		}
	}

	for (int i = optind; i < argc; i++)
		splash_add_image(splash, argv[i]);

	if (splash_num_images(splash) > 0) {
		ret = splash_run(splash);
		if (ret) {
			LOG(ERROR, "Splash_run failed: %d.", ret);
			return EXIT_FAILURE;
		}
	}

	if (command_flags.splash_only)
		goto main_done;

	/*
	 * The DBUS service launches later than the boot-splash service, and
	 * as a result, when splash_run starts DBUS is not yet up, but, by
	 * the time splash_run completes, it is running.
	 * We really need DBUS now, so we can interact with Chrome.
	 */
	dbus_init_wait();

	/*
	 * Ask DBUS to call us back so we can destroy splash (or quit) when login
	 * prompt is visible.
	 */
	dbus_set_login_prompt_visible_callback(main_on_login_prompt_visible,
					       (void*)splash);

	if (command_flags.daemon) {
		if (command_flags.enable_vts)
			set_drm_master_relax(); /* TODO(dbehr) Remove when Chrome is fixed to actually release master. */
		drm_dropmaster(drm);
		term_background();
	} else {
		/* Create and switch to first term in interactve mode. */
		terminal_t* terminal;
		set_drm_master_relax(); /* TODO(dbehr) Remove when Chrome is fixed to actually release master. */
		term_foreground();
		term_set_current_terminal(term_init(true));
		terminal = term_get_current_terminal();
		term_activate(terminal);
	}

	ret = main_loop();

main_done:
	input_close();
	dev_close();
	dbus_destroy();
	drm_close();

	return ret;
}
