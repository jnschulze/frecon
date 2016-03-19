/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FB_H
#define FB_H

#include "drm.h"

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
} fb_lock_t;

typedef struct {
	drm_t *drm;
	buffer_properties_t buffer_properties;
	fb_lock_t lock;
	uint32_t buffer_handle;
	uint32_t fb_id;
} fb_t;

fb_t* fb_init(void);
void fb_close(fb_t* fb);
int32_t fb_setmode(fb_t* fb);
int fb_buffer_init(fb_t* fb);
void fb_buffer_destroy(fb_t* fb);
uint32_t* fb_lock(fb_t* fb);
void fb_unlock(fb_t* fb);
int32_t fb_getwidth(fb_t* fb);
int32_t fb_getheight(fb_t* fb);
int32_t fb_getpitch(fb_t* fb);
int32_t fb_getscaling(fb_t* fb);

#endif
