/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "fb.h"

static int fb_buffer_create(fb_t* fb,
			    int* pitch)
{
	struct drm_mode_create_dumb create_dumb;
	struct drm_mode_destroy_dumb destroy_dumb;
	int ret;

	memset(&create_dumb, 0, sizeof (create_dumb));
	create_dumb.bpp = 32;
	create_dumb.width = fb->drm->crtc->mode.hdisplay;
	create_dumb.height = fb->drm->crtc->mode.vdisplay;

	ret = drmIoctl(fb->drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
	if (ret) {
		LOG(ERROR, "CREATE_DUMB failed");
		return ret;
	}

	fb->buffer_properties.size = create_dumb.size;
	fb->buffer_handle = create_dumb.handle;

	struct drm_mode_map_dumb map_dumb;
	map_dumb.handle = create_dumb.handle;
	ret = drmIoctl(fb->drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
	if (ret) {
		LOG(ERROR, "MAP_DUMB failed");
		goto destroy_buffer;
	}

	fb->lock.map_offset = map_dumb.offset;

	uint32_t offset = 0;
	ret = drmModeAddFB2(fb->drm->fd, fb->drm->crtc->mode.hdisplay, fb->drm->crtc->mode.vdisplay,
			    DRM_FORMAT_XRGB8888, &create_dumb.handle,
			    &create_dumb.pitch, &offset, &fb->fb_id, 0);
	if (ret) {
		LOG(ERROR, "drmModeAddFB2 failed");
		goto destroy_buffer;
	}

	*pitch = create_dumb.pitch;

	return 0;

destroy_buffer:
	destroy_dumb.handle = create_dumb.handle;

	drmIoctl(fb->drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);

	return ret;
}

void fb_buffer_destroy(fb_t* fb)
{
	struct drm_mode_destroy_dumb destroy_dumb;

	if (fb->buffer_handle <= 0)
		return;

	drmModeRmFB(fb->drm->fd, fb->fb_id);
	fb->fb_id = 0;
	destroy_dumb.handle = fb->buffer_handle;
	drmIoctl(fb->drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
	fb->buffer_handle = 0;
	fb->lock.map = NULL;
	drm_delref(fb->drm);
	fb->drm = NULL;
}

static bool parse_edid_dtd(uint8_t* dtd, drmModeModeInfo* mode,
			   int32_t* hdisplay_size, int32_t* vdisplay_size) {
	int32_t clock;
	int32_t hactive, hbl, hso, hsw, hsize;
	int32_t vactive, vbl, vso, vsw, vsize;

	clock = ((int32_t)dtd[DTD_PCLK_HI] << 8) | dtd[DTD_PCLK_LO];
	if (!clock)
		return false;

	hactive = ((int32_t)(dtd[DTD_HABL_HI] & 0xf0) << 4) + dtd[DTD_HA_LO];
	vactive = ((int32_t)(dtd[DTD_VABL_HI] & 0xf0) << 4) + dtd[DTD_VA_LO];
	hbl = ((int32_t)(dtd[DTD_HABL_HI] & 0x0f) << 8) + dtd[DTD_HBL_LO];
	vbl = ((int32_t)(dtd[DTD_VABL_HI] & 0x0f) << 8) + dtd[DTD_VBL_LO];
	hso = ((int32_t)(dtd[DTD_HVSX_HI] & 0xc0) << 2) + dtd[DTD_HSO_LO];
	vso = ((int32_t)(dtd[DTD_HVSX_HI] & 0x0c) << 2) + (dtd[DTD_VSX_LO] >> 4);
	hsw = ((int32_t)(dtd[DTD_HVSX_HI] & 0x30) << 4) + dtd[DTD_HSW_LO];
	vsw = ((int32_t)(dtd[DTD_HVSX_HI] & 0x03) << 4) + (dtd[DTD_VSX_LO] & 0xf);
	hsize = ((int32_t)(dtd[DTD_HVSIZE_HI] & 0xf0) << 4) + dtd[DTD_HSIZE_LO];
	vsize = ((int32_t)(dtd[DTD_HVSIZE_HI] & 0x0f) << 8) + dtd[DTD_VSIZE_LO];

	mode->clock = clock * 10;
	mode->hdisplay = hactive;
	mode->vdisplay = vactive;
	mode->hsync_start = hactive + hso;
	mode->vsync_start = vactive + vso;
	mode->hsync_end = mode->hsync_start + hsw;
	mode->vsync_end = mode->vsync_start + vsw;
	mode->htotal = hactive + hbl;
	mode->vtotal = vactive + vbl;
	*hdisplay_size = hsize;
	*vdisplay_size = vsize;
	return true;
}

static bool parse_edid_dtd_display_size(drm_t* drm, int32_t* hsize_mm, int32_t* vsize_mm) {
	drmModeModeInfo* mode = &drm->crtc->mode;

	for (int i = 0; i < EDID_N_DTDS; i++) {
		uint8_t* dtd = (uint8_t*)&drm->edid[EDID_DTD_BASE + i * DTD_SIZE];
		drmModeModeInfo dtd_mode;
		int32_t hdisplay_size, vdisplay_size;
		if (!parse_edid_dtd(dtd, &dtd_mode, &hdisplay_size, &vdisplay_size) ||
				mode->clock != dtd_mode.clock ||
				mode->hdisplay != dtd_mode.hdisplay ||
				mode->vdisplay != dtd_mode.vdisplay ||
				mode->hsync_start != dtd_mode.hsync_start ||
				mode->vsync_start != dtd_mode.vsync_start ||
				mode->hsync_end != dtd_mode.hsync_end ||
				mode->vsync_end != dtd_mode.vsync_end ||
				mode->htotal != dtd_mode.htotal ||
				mode->vtotal != dtd_mode.vtotal)
			continue;
		*hsize_mm = hdisplay_size;
		*vsize_mm = vdisplay_size;
		return true;
	}
	return false;
}

int fb_buffer_init(fb_t* fb)
{
	int32_t width, height, pitch;
	int32_t hsize_mm, vsize_mm;
	int r;

	/* some reasonable defaults */
	fb->buffer_properties.width = 640;
	fb->buffer_properties.height = 480;
	fb->buffer_properties.pitch = 640 * 4;
	fb->buffer_properties.scaling = 1;

	fb->drm = drm_addref();

	if (!fb->drm) {
		LOG(WARNING, "No monitor available, running headless!");
		return -ENODEV;
	}

	width = fb->drm->crtc->mode.hdisplay;
	height = fb->drm->crtc->mode.vdisplay;

	r = fb_buffer_create(fb, &pitch);
	if (r < 0) {
		LOG(ERROR, "fb_buffer_create failed");
		return r;
	}

	fb->buffer_properties.width = width;
	fb->buffer_properties.height = height;
	fb->buffer_properties.pitch = pitch;

	hsize_mm = fb->drm->main_monitor_connector->mmWidth;
	vsize_mm = fb->drm->main_monitor_connector->mmHeight;
	if (drm_read_edid(fb->drm))
		parse_edid_dtd_display_size(fb->drm, &hsize_mm, &vsize_mm);

	if (hsize_mm) {
		int dots_per_cm = width * 10 / hsize_mm;
		if (dots_per_cm > 133)
			fb->buffer_properties.scaling = 4;
		else if (dots_per_cm > 100)
			fb->buffer_properties.scaling = 3;
		else if (dots_per_cm > 67)
			fb->buffer_properties.scaling = 2;
	}

	return 0;
}

fb_t* fb_init(void)
{
	fb_t* fb;

	fb = (fb_t*)calloc(1, sizeof(fb_t));
	if (!fb)
		return NULL;

	fb_buffer_init(fb);

	return fb;
}

void fb_close(fb_t* fb)
{
	if (!fb)
		return;

	fb_buffer_destroy(fb);

	free(fb);
}

int32_t fb_setmode(fb_t* fb)
{
	/* headless mode */
	if (!drm_valid(fb->drm))
		return 0;

	return drm_setmode(fb->drm, fb->fb_id);
}

uint32_t* fb_lock(fb_t* fb)
{
	if (fb->lock.count == 0 && fb->buffer_handle > 0) {
		fb->lock.map =
			mmap(0, fb->buffer_properties.size, PROT_READ | PROT_WRITE,
					MAP_SHARED, fb->drm->fd, fb->lock.map_offset);
		if (fb->lock.map == MAP_FAILED) {
			LOG(ERROR, "mmap failed");
			return NULL;
		}
	}
	fb->lock.count++;

	return fb->lock.map;
}

void fb_unlock(fb_t* fb)
{
	if (fb->lock.count > 0)
		fb->lock.count--;
	else
		LOG(ERROR, "fb locking unbalanced");

	if (fb->lock.count == 0 && fb->buffer_handle > 0) {
		int32_t ret;
		struct drm_clip_rect clip_rect = {
			0, 0, fb->buffer_properties.width, fb->buffer_properties.height
		};
		munmap(fb->lock.map, fb->buffer_properties.size);
		ret = drmModeDirtyFB(fb->drm->fd, fb->fb_id, &clip_rect, 1);
		if (ret && errno != ENOSYS)
			LOG(ERROR, "drmModeDirtyFB failed: %m");
	}
}

int32_t fb_getwidth(fb_t* fb)
{
	return fb->buffer_properties.width;
}

int32_t fb_getheight(fb_t* fb)
{
	return fb->buffer_properties.height;
}

int32_t fb_getpitch(fb_t* fb)
{
	return fb->buffer_properties.pitch;
}

int32_t fb_getscaling(fb_t* fb)
{
	return fb->buffer_properties.scaling;
}
