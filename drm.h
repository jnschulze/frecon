/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DRM_H
#define DRM_H


#include <stdbool.h>
#include <stdio.h>
#include <edid_utils.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct _drm_t {
	int refcount;
	int fd;
	drmModeRes* resources;
	drmModePlaneResPtr plane_resources;
	drmModeConnector* main_monitor_connector;
	drmModeCrtc* crtc;
	uint32_t selected_mode;
	bool edid_found;
	char edid[EDID_SIZE];
	uint32_t delayed_rmfb_fb_id;
} drm_t;

drm_t* drm_scan(void);
void drm_set(drm_t* drm);
void drm_close(void);
drm_t* drm_addref(void);
void drm_delref(drm_t* drm);
int drm_dropmaster(drm_t* drm);
int drm_setmaster(drm_t* drm);
bool drm_rescan(void);
bool drm_valid(drm_t* drm);
int32_t drm_setmode(drm_t* drm, uint32_t fb_id);
void drm_rmfb(drm_t* drm, uint32_t fb_id);
bool drm_read_edid(drm_t* drm);
uint32_t drm_gethres(drm_t* drm);
uint32_t drm_getvres(drm_t* drm);

#endif
