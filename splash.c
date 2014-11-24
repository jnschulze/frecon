/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include <math.h>
#include <png.h>

#include "util.h"
#include "splash.h"
#include "dbus_interface.h"

#define  MAX_SPLASH_IMAGES      (30)
#define  FILENAME_LENGTH        (100)
#define  MAX_SPLASH_WAITTIME    (5000)

typedef union {
	uint32_t  *as_pixels;
	png_byte  *as_png_bytes;
	char      *address;
} splash_layout_t;

typedef struct {
	char            filename[FILENAME_LENGTH];
	FILE           *fp;
	splash_layout_t layout;
	png_uint_32     width;
	png_uint_32     height;
	png_uint_32     pitch;
} splash_image_t;

struct _splash_t {
	video_t         *video;
	int              num_images;
	splash_image_t   images[MAX_SPLASH_IMAGES];
	int              frame_interval;
	uint32_t         clear;
	bool             terminated;
	bool             devmode;
	dbus_t          *dbus;
};

static void splash_rgb(png_struct *png, png_row_info *row_info, png_byte *data)
{
	unsigned int i;

	for (i = 0; i < row_info->rowbytes; i+= 4) {
		uint8_t r, g, b, a;
		uint32_t pixel;

		r = data[i + 0];
		g = data[i + 1];
		b = data[i + 2];
		a = data[i + 3];
		pixel = (a << 24) | (r << 16) | (g << 8) | b;
		memcpy(data + i, &pixel, sizeof(pixel));
	}
}

static int splash_load_image_from_file(splash_t* splash, splash_image_t* image)
{
	png_struct   *png;
	png_info     *info;
	png_uint_32   width, height, pitch, row;
	int           bpp, color_type, interlace_mthd;
	png_byte    **rows;

	if (image->fp != NULL)
		return 1;

	image->fp = fopen(image->filename, "rb");
	if (image->fp == NULL)
		return 1;

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info = png_create_info_struct(png);

	if (info == NULL)
		return 1;

	png_init_io(png, image->fp);

	if (setjmp(png_jmpbuf(png)) != 0) {
		fclose(image->fp);
		return 1;
	}

	png_read_info(png, info);
	png_get_IHDR(png, info, &width, &height, &bpp, &color_type,
			&interlace_mthd, NULL, NULL);

	pitch = 4 * width;

	switch (color_type)
	{
		case PNG_COLOR_TYPE_PALETTE:
			png_set_palette_to_rgb(png);
			break;

		case PNG_COLOR_TYPE_GRAY:
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			png_set_gray_to_rgb(png);
	}

	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);

	switch (bpp)
	{
		default:
			if (bpp < 8)
				png_set_packing(png);
			break;
		case 16:
			png_set_strip_16(png);
			break;
	}

	if (interlace_mthd != PNG_INTERLACE_NONE)
		png_set_interlace_handling(png);

	png_set_filler(png, 0xff, PNG_FILLER_AFTER);

	png_set_read_user_transform_fn(png, splash_rgb);
	png_read_update_info(png, info);

	rows = malloc(height * sizeof(*rows));
	image->layout.address = malloc(height * pitch);

	for (row = 0; row < height; row++) {
		rows[row] = &image->layout.as_png_bytes[row * pitch];
	}

	png_read_image(png, rows);
	free(rows);

	png_read_end(png, info);
	fclose(image->fp);
	png_destroy_read_struct(&png, &info, NULL);

	image->width = width;
	image->height = height;
	image->pitch = pitch;

	return 0;
}

static int splash_image_show(splash_t *splash,
		splash_image_t* image,
		uint32_t *video_buffer)
{
	uint32_t j;
	uint32_t startx, starty;
	buffer_properties_t *bp;
	uint32_t *buffer;


	bp = video_get_buffer_properties(splash->video);
	startx = (bp->width - image->width) / 2;
	starty = (bp->height - image->height) / 2;

	buffer = video_lock(splash->video);

	if (buffer != NULL) {
		for (j = starty; j < starty + image->height; j++) {
			memcpy(buffer + j * bp->pitch/4 + startx,
					image->layout.address + (j - starty)*image->pitch, image->pitch);
		}
	}

	video_unlock(splash->video);
	return 0;
}

splash_t* splash_init()
{
	splash_t* splash;
	FILE *cookie_fp;

	splash = (splash_t*)calloc(1, sizeof(splash_t));
	if (splash == NULL)
		return NULL;

	splash->num_images = 0;
	splash->video = video_init();

	cookie_fp = fopen("/tmp/display_info.bin", "wb");
	if (cookie_fp) {
		fwrite(&splash->video->internal_panel, sizeof(char), 1, cookie_fp);
		fwrite(splash->video->edid, EDID_SIZE, 1, cookie_fp);
		fclose(cookie_fp);
	}

	return splash;
}

int splash_destroy(splash_t* splash)
{
	return 0;
}

int splash_set_frame_rate(splash_t *splash, int32_t rate)
{
	if (rate <= 0 || rate > 120)
		return 1;

	splash->frame_interval = rate;
	return 0;
}

int splash_set_clear(splash_t *splash, int32_t clear_color)
{
	splash->clear = clear_color;
	return 0;
}

int splash_add_image(splash_t* splash, const char* filename)
{
	if (splash->num_images >= MAX_SPLASH_IMAGES)
		return 1;

	strcpy(splash->images[splash->num_images].filename, filename);
	splash->num_images++;
	return 0;
}

static void
frecon_dbus_path_message_func(dbus_t* dbus, void* user_data)
{
	splash_t* splash = (splash_t*)user_data;

	if (!splash->devmode)
		exit(EXIT_SUCCESS);

	dbus_stop_wait(dbus);
	video_close(splash->video);
}

static void splash_clear_screen(splash_t *splash, uint32_t *video_buffer)
{
	int i,j;
	buffer_properties_t *bp;

	video_setmode(splash->video);

	bp = video_get_buffer_properties(splash->video);

		for (j = 0; j < bp->height; j++) {
			for (i = 0; i < bp->width; i++) {
				 (video_buffer + bp->pitch/4 * j)[i] = splash->clear;
			}
		}
}

int splash_run(splash_t* splash, dbus_t** dbus)
{
	int i;
	uint32_t* video_buffer;
	int status;
	bool db_status;
	int64_t last_show_ms;
	int64_t now_ms;
	int64_t sleep_ms;
	struct timespec sleep_spec;
	int fd;
	int num_written;
	int wfm_status;

	status = 0;

	/*
	 * First draw the actual splash screen
	 */
	video_buffer = video_lock(splash->video);
	if (video_buffer != NULL) {
		splash_clear_screen(splash, video_buffer);
		last_show_ms = -1;
		for (i = 0; i < splash->num_images; i++) {
			status = splash_load_image_from_file(splash, &splash->images[i]);
			if (status != 0) {
				LOG(WARNING, "splash_load_image_from_file failed: %d\n", status);
				break;
			}

			now_ms = get_monotonic_time_ms();
			if (last_show_ms > 0) {
				sleep_ms = splash->frame_interval - (now_ms - last_show_ms);
				if (sleep_ms > 0) {
					sleep_spec.tv_sec = sleep_ms / MS_PER_SEC;
					sleep_spec.tv_nsec = (sleep_ms % MS_PER_SEC) * NS_PER_MS;
					nanosleep(&sleep_spec, NULL);
				}
			}

			now_ms = get_monotonic_time_ms();

			status = splash_image_show(splash, &splash->images[i], video_buffer);
			if (status != 0) {
				LOG(WARNING, "splash_image_show failed: %d", status);
				break;
			}
			last_show_ms = now_ms;
		}
		video_unlock(splash->video);

		/*
		 * Next wait until chrome has drawn on top of the splash.  In dev mode,
		 * dbus_wait_for_messages will return when chrome is visible.  In
		 * verified mode, the frecon app will exit before dbus_wait_for_messages
		 * returns
		 */
		do {
			*dbus = dbus_init();
			usleep(50000);
		} while (*dbus == NULL);

		splash_set_dbus(splash, *dbus);

		db_status = dbus_signal_match_handler(*dbus,
				kLoginPromptVisibleSignal,
				kSessionManagerServicePath,
				kSessionManagerInterface,
				kLoginPromptVisiibleRule,
				frecon_dbus_path_message_func, splash);

		if (db_status) {
			wfm_status = dbus_wait_for_messages(*dbus, MAX_SPLASH_WAITTIME);
			switch (wfm_status) {
				case DBUS_STATUS_TIMEOUT:
					LOG(WARNING, "timed out waiting for messages\n");
					break;
			}
		}


		if (splash->devmode) {
			/*
			 * Now set drm_master_relax so that we can transfer drm_master between
			 * chrome and frecon
			 */
			fd = open("/sys/kernel/debug/dri/drm_master_relax", O_WRONLY);
			if (fd != -1) {
				num_written = write(fd, "Y", 1);
				close(fd);

				/*
				 * If we can't set drm_master relax, then transitions between chrome
				 * and frecon won't work.  No point in having frecon hold any resources
				 */
				if (num_written != 1) {
					LOG(ERROR, "Unable to set drm_master_relax");
					splash->devmode = false;
				}
			} else {
				LOG(ERROR, "unable to open drm_master_relax");
			}
		}
	}


	/* Let chrome know it's ok to take drmMaster */
	video_release(splash->video);
	(void)dbus_method_call0(splash->dbus,
		kLibCrosServiceName,
		kLibCrosServicePath,
		kLibCrosServiceInterface,
		kTakeDisplayOwnership);
	return status;
}

void splash_set_dbus(splash_t* splash, dbus_t* dbus)
{
	splash->dbus = dbus;
}

void splash_set_devmode(splash_t* splash)
{
	splash->devmode = true;
}
