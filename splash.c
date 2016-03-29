/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dbus.h"
#include "dbus_interface.h"
#include "image.h"
#include "input.h"
#include "main.h"
#include "splash.h"
#include "term.h"
#include "util.h"

#define  MAX_SPLASH_IMAGES      (30)
#define  MAX_SPLASH_WAITTIME    (8)

typedef struct {
	image_t* image;
	uint32_t duration;
} splash_frame_t;

struct _splash_t {
	terminal_t* terminal;
	int num_images;
	uint32_t clear;
	splash_frame_t image_frames[MAX_SPLASH_IMAGES];
	bool terminated;
	int32_t loop_start;
	int32_t loop_count;
	uint32_t loop_duration;
	uint32_t default_duration;
	int32_t offset_x;
	int32_t offset_y;
	int32_t loop_offset_x;
	int32_t loop_offset_y;
};


splash_t* splash_init()
{
	splash_t* splash;

	splash = (splash_t*)calloc(1, sizeof(splash_t));
	if (!splash)
		return NULL;

	splash->terminal = term_create_splash_term();
	splash->loop_start = -1;
	splash->loop_count = -1;
	splash->default_duration = 25;
	splash->loop_duration = 25;

	return splash;
}

int splash_destroy(splash_t* splash)
{
	if (splash->terminal) {
		term_close(splash->terminal);
		splash->terminal = NULL;
	}
	free(splash);
	term_destroy_splash_term();
	return 0;
}

int splash_set_clear(splash_t* splash, uint32_t clear_color)
{
	splash->clear = clear_color;
	return 0;
}

int splash_add_image(splash_t* splash, char* filespec)
{
	image_t* image;
	int32_t offset_x, offset_y;
	char* filename;
	uint32_t duration;
	if (splash->num_images >= MAX_SPLASH_IMAGES)
		return 1;

	filename = (char*)malloc(strlen(filespec) + 1);
	parse_filespec(filespec,
			filename,
			&offset_x, &offset_y, &duration,
			splash->default_duration,
			splash->offset_x,
			splash->offset_y);

	image = image_create();
	image_set_filename(image, filename);
	image_set_offset(image, offset_x, offset_y);
	splash->image_frames[splash->num_images].image = image;
	splash->image_frames[splash->num_images].duration = duration;
	splash->num_images++;

	free(filename);
	return 0;
}

static void splash_clear_screen(splash_t* splash)
{
	term_set_background(splash->terminal, splash->clear);
}

int splash_run(splash_t* splash)
{
	int i;
	int status = 0;
	int64_t last_show_ms;
	int64_t now_ms;
	int64_t sleep_ms;
	struct timespec sleep_spec;
	image_t* image;
	uint32_t duration;
	int32_t c, loop_start, loop_count;

	/*
	 * First draw the actual splash screen
	 */
	splash_clear_screen(splash);
	term_activate(splash->terminal);
	last_show_ms = -1;
	loop_count = (splash->loop_start >= 0 && splash->loop_start < splash->num_images) ? splash->loop_count : 1;
	loop_start = (splash->loop_start >= 0 && splash->loop_start < splash->num_images) ? splash->loop_start : 0;

	for (c = 0; ((loop_count < 0) ? true : (c < loop_count)); c++)
	for (i = (c > 0) ? loop_start : 0; i < splash->num_images; i++) {
		image = splash->image_frames[i].image;
		status = image_load_image_from_file(image);
		if (status != 0) {
			LOG(WARNING, "image_load_image_from_file failed: %d", status);
			break;
		}

		now_ms = get_monotonic_time_ms();
		if (last_show_ms > 0) {
			if (splash->loop_start >= 0 && i >= splash->loop_start)
				duration = splash->loop_duration;
			else
				duration = splash->image_frames[i].duration;
			sleep_ms = duration - (now_ms - last_show_ms);
			if (sleep_ms > 0) {
				sleep_spec.tv_sec = sleep_ms / MS_PER_SEC;
				sleep_spec.tv_nsec = (sleep_ms % MS_PER_SEC) * NS_PER_MS;
				nanosleep(&sleep_spec, NULL);
			}
		}

		now_ms = get_monotonic_time_ms();

		if (i >= splash->loop_start) {
			image_set_offset(image,
					splash->loop_offset_x,
					splash->loop_offset_y);
		}

		status = term_show_image(splash->terminal, image);
		if (status != 0) {
			LOG(WARNING, "term_show_image failed: %d", status);
			break;
		}
		status = main_process_events(1);
		if (status != 0) {
			LOG(WARNING, "input_process failed: %d", status);
			break;
		}
		last_show_ms = now_ms;

		image_release(image);
		/* see if we can initialize DBUS */
		if (!dbus_is_initialized())
			dbus_init();
	}

	for (i = 0; i < splash->num_images; i++) {
		image_destroy(splash->image_frames[i].image);
	}

	term_set_current_to(NULL);

	return status;
}

void splash_set_offset(splash_t* splash, int32_t x, int32_t y)
{
	if (splash) {
		splash->offset_x = x;
		splash->offset_y = y;
	}
}

int splash_num_images(splash_t* splash)
{
	if (splash)
		return splash->num_images;

	return 0;
}

void splash_set_loop_count(splash_t* splash, int32_t count)
{
	if (splash)
		splash->loop_count = count;
}

void splash_set_default_duration(splash_t* splash, uint32_t duration)
{
	if (splash)
		splash->default_duration = duration;
}

void splash_set_loop_start(splash_t* splash, int32_t loop_start)
{
	if (splash)
		splash->loop_start = loop_start;
}

void splash_set_loop_duration(splash_t* splash, uint32_t duration)
{
	if (splash)
		splash->loop_duration = duration;
}

void splash_set_loop_offset(splash_t* splash, int32_t x, int32_t y)
{
	if (splash) {
		splash->loop_offset_x = x;
		splash->loop_offset_y = y;
	}
}

void splash_present_term_file(splash_t* splash)
{
	fprintf(stdout, "%s\n", term_get_ptsname(splash->terminal));
}

int splash_is_hires(splash_t* splash)
{
	if (splash && splash->terminal && term_getfb(splash->terminal))
		return fb_getwidth(term_getfb(splash->terminal)) > 1920;
	return 0;
}
