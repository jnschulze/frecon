/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef VIDEO_H
#define VIDEO_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "edid_utils.h"

#define kGammaSize         (256)

typedef struct {
	int32_t width;
	int32_t height;
	int32_t pitch;
	int32_t scaling;
	int32_t size;
} buffer_properties_t;

typedef struct {
	int32_t count;
	uint64_t map_offset;
	uint32_t* map;
} video_lock_t;

typedef struct _gamme_ramp_t{
	uint16_t red[kGammaSize];
	uint16_t green[kGammaSize];
	uint16_t blue[kGammaSize];
} gamma_ramp_t;

gamma_ramp_t g_gamma_ramp;

typedef struct {
	int fd;
	buffer_properties_t buffer_properties;
	video_lock_t lock;
	drmModeRes* drm_resources;
	drmModePlaneResPtr drm_plane_resources;
	drmModeConnector* main_monitor_connector;
	drmModeCrtc* crtc;
	uint32_t buffer_handle;

	uint32_t fb_id;

	gamma_ramp_t gamma_ramp;
	char edid[EDID_SIZE];
	int ref;
} video_t;

video_t* video_init();
int32_t video_getwidth(video_t* video);
int32_t video_getheight(video_t* video);
int32_t video_getpitch(video_t* video);
int32_t video_getscaling(video_t* video);
int32_t video_setmode(video_t* video);
void video_release(video_t* video);
bool video_set_gamma(video_t* video, const char* filename);
void video_close(video_t*);
uint32_t* video_lock(video_t* video);
void video_unlock(video_t* video);
void video_addref(video_t* video);
void video_delref(video_t* video);
int video_init_connector(video_t* video);

#endif
