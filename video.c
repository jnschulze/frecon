/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drm_fourcc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "util.h"
#include "video.h"
#include "dbus_interface.h"
#include "dbus.h"


static int kms_open()
{
	const char *module_list[] = { "cirrus", "exynos", "i915", "rockchip",
							"tegra" };
	int fd = -1;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(module_list); i++) {
		fd = drmOpen(module_list[i], NULL);
		if (fd >= 0)
			break;
	}

	return fd;
}

static drmModeCrtc *find_crtc_for_connector(int fd,
							drmModeRes *resources,
							drmModeConnector *connector)
{
	int i;
	unsigned encoder_crtc_id = 0;

	/* Find the encoder */
	for (i = 0; i < resources->count_encoders; i++) {
		drmModeEncoder *encoder =
				drmModeGetEncoder(fd, resources->encoders[i]);

		if (encoder) {
			if (encoder->encoder_id == connector->encoder_id) {
				encoder_crtc_id = encoder->crtc_id;
				drmModeFreeEncoder(encoder);
				break;
			}
			drmModeFreeEncoder(encoder);
		}
	}

	if (!encoder_crtc_id)
		return NULL;

	/* Find the crtc */
	for (i = 0; i < resources->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[i]);

		if (crtc) {
			if (encoder_crtc_id == crtc->crtc_id)
				return crtc;
			drmModeFreeCrtc(crtc);
		}
	}

	return NULL;
}

static bool is_connector_used(int fd,
						drmModeRes *resources,
						drmModeConnector *connector)
{
	bool result = false;
	drmModeCrtc *crtc = find_crtc_for_connector(fd, resources, connector);

	if (crtc) {
		result = crtc->buffer_id != 0;
		drmModeFreeCrtc(crtc);
	}

	return result;
}

static drmModeConnector *find_used_connector_by_type(int fd,
								 drmModeRes *resources,
								 unsigned type)
{
	int i;
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector) {
			if ((connector->connector_type == type) &&
					(connector->connection == DRM_MODE_CONNECTED))
				return connector;

			drmModeFreeConnector(connector);
		}
	}
	return NULL;
}

static drmModeConnector *find_first_used_connector(int fd,
							 drmModeRes *resources)
{
	int i;
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector) {
			if (is_connector_used(fd, resources, connector))
				return connector;

			drmModeFreeConnector(connector);
		}
	}
	return NULL;
}

static drmModeConnector *find_main_monitor(int fd, drmModeRes *resources)
{
	unsigned i = 0;
	/*
	 * Find the LVDS/eDP/DSI connectors. Those are the main screens.
	 */
	unsigned kConnectorPriority[] = {
		DRM_MODE_CONNECTOR_LVDS,
		DRM_MODE_CONNECTOR_eDP,
		DRM_MODE_CONNECTOR_DSI,
	};

	drmModeConnector *main_monitor_connector = NULL;
	do {
		main_monitor_connector = find_used_connector_by_type(fd,
										 resources,
										 kConnectorPriority[i]);
		i++;
	} while (!main_monitor_connector && i < ARRAY_SIZE(kConnectorPriority));

	/*
	 * If we didn't find a connector, grab the first one in use.
	 */
	if (!main_monitor_connector)
		main_monitor_connector =
				find_first_used_connector(fd, resources);

	return main_monitor_connector;
}

static void disable_connector(int fd,
						drmModeRes *resources,
						drmModeConnector *connector)
{
	drmModeCrtc *crtc = find_crtc_for_connector(fd, resources, connector);

	if (crtc) {
		drmModeSetCrtc(fd, crtc->crtc_id, 0, // buffer_id
						 0, 0,  // x,y
						 NULL,  // connectors
						 0,     // connector_count
						 NULL); // mode
		drmModeFreeCrtc(crtc);
	}
}

static void disable_non_main_connectors(int fd,
					drmModeRes *resources,
					drmModeConnector *main_connector)
{
	int i;

	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector->connector_id != main_connector->connector_id)
			disable_connector(fd, resources, connector);

		drmModeFreeConnector(connector);
	}
}

static int video_buffer_create(video_t *video, drmModeCrtc *crtc, drmModeConnector *connector,
						 int *pitch)
{
	struct drm_mode_create_dumb create_dumb;
	int ret;

	memset(&create_dumb, 0, sizeof (create_dumb));
	create_dumb.bpp = 32;
	create_dumb.width = crtc->mode.hdisplay;
	create_dumb.height = crtc->mode.vdisplay;

	ret = drmIoctl(video->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
	if (ret) {
		LOG(ERROR, "CREATE_DUMB failed");
		return ret;
	}

	video->buffer_properties.size = create_dumb.size;
	video->buffer_handle = create_dumb.handle;

	struct drm_mode_map_dumb map_dumb;
	map_dumb.handle = create_dumb.handle;
	ret = drmIoctl(video->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
	if (ret) {
		LOG(ERROR, "MAP_DUMB failed");
		goto destroy_buffer;
	}

	video->lock.map_offset = map_dumb.offset;

	uint32_t offset = 0;
	ret = drmModeAddFB2(video->fd, crtc->mode.hdisplay, crtc->mode.vdisplay,
					DRM_FORMAT_XRGB8888, &create_dumb.handle,
					&create_dumb.pitch, &offset, &video->fb_id, 0);
	if (ret) {
		LOG(ERROR, "drmModeAddFB2 failed");
		goto destroy_buffer;
	}

	*pitch = create_dumb.pitch;

	return ret;

destroy_buffer:
	;
	struct drm_mode_destroy_dumb destroy_dumb;
	destroy_dumb.handle = create_dumb.handle;

	drmIoctl(video->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);

	return ret;
}

video_t* video_init()
{
	int32_t width, height, scaling, pitch;
	int i;
	video_t *new_video = (video_t*)calloc(1, sizeof(video_t));

	new_video->fd = -1;

	new_video->fd = kms_open();

	if (new_video->fd < 0) {
		LOG(ERROR, "Unable to open a KMS module");
		return NULL;
	}

	if (drmSetMaster(new_video->fd) != 0) {
		LOG(ERROR, "video_init unable to get master");
	}

	new_video->drm_resources = drmModeGetResources(new_video->fd);
	if (!new_video->drm_resources) {
		LOG(ERROR, "Unable to get mode resources");
		goto fail;
	}

	new_video->main_monitor_connector = find_main_monitor(new_video->fd, new_video->drm_resources);

	if (!new_video->main_monitor_connector) {
		LOG(ERROR, "main_monitor_connector is nil");
		goto fail;
	}

	for (i = 0; i < new_video->main_monitor_connector->count_props; i++) {
		drmModePropertyPtr prop;
		drmModePropertyBlobPtr blob_ptr;
		prop = drmModeGetProperty(new_video->fd, new_video->main_monitor_connector->props[i]);
		if (prop) {
			if (strcmp(prop->name, "EDID") == 0) {
				blob_ptr = drmModeGetPropertyBlob(new_video->fd,
					new_video->main_monitor_connector->prop_values[i]);
				if (blob_ptr) {
					switch (new_video->main_monitor_connector->connector_type) {
						case DRM_MODE_CONNECTOR_LVDS:
						case DRM_MODE_CONNECTOR_eDP:
							new_video->internal_panel = 1;
							break;
						default:
							new_video->internal_panel = 0;
					}
					memcpy(&new_video->edid, blob_ptr->data, EDID_SIZE);
					drmModeFreePropertyBlob(blob_ptr);
				}
			}
		}
	}

	disable_non_main_connectors(new_video->fd,
			new_video->drm_resources, new_video->main_monitor_connector);


	new_video->crtc = find_crtc_for_connector(new_video->fd,
			new_video->drm_resources, new_video->main_monitor_connector);

	if (!new_video->crtc) {
		LOG(ERROR, "unable to find a crtc");
		goto fail;
	}

	if (video_buffer_create(new_video, new_video->crtc,
				new_video->main_monitor_connector, &pitch)) {
		LOG(ERROR, "video_buffer_create failed");
		goto fail;
	}

	width = new_video->crtc->mode.hdisplay;
	height = new_video->crtc->mode.vdisplay;

	if (!new_video->main_monitor_connector->mmWidth)
		scaling = 1;
	else {
		int dots_per_cm = width * 10 / new_video->main_monitor_connector->mmWidth;
		if (dots_per_cm > 133)
			scaling = 4;
		else if (dots_per_cm > 100)
			scaling = 3;
		else if (dots_per_cm > 67)
			scaling = 2;
		else
			scaling = 1;
	}

	new_video->buffer_properties.width = width;
	new_video->buffer_properties.height = height;
	new_video->buffer_properties.pitch = pitch;
	new_video->buffer_properties.scaling = scaling;

	if (drmDropMaster(new_video->fd) != 0) {
		LOG(WARNING, "video_init unable to drop master");
	}

	return new_video;

fail:
	if (new_video->drm_resources)
		drmModeFreeResources(new_video->drm_resources);

	if (new_video->main_monitor_connector)
		drmModeFreeConnector(new_video->main_monitor_connector);

	if (new_video->crtc)
		drmModeFreeCrtc(new_video->crtc);

	if (drmDropMaster(new_video->fd) != 0) {
		LOG(WARNING, "video_init unable to drop master");
	}

	if (new_video->fd >= 0)
		drmClose(new_video->fd);

	return NULL;
}


int32_t video_setmode(video_t* video)
{
	int32_t ret;

	drmSetMaster(video->fd);
	ret = drmModeSetCrtc(video->fd, video->crtc->crtc_id,
					 video->fb_id,
					 0, 0,  // x,y
					 &video->main_monitor_connector->connector_id,
					 1,  // connector_count
					 &video->crtc->mode); // mode

	if (ret) {
		LOG(ERROR, "Unable to set crtc");
		goto done;
	}

	ret = drmModeSetCursor(video->fd, video->crtc->crtc_id,
			0, 0, 0);

	if (ret)
		LOG(ERROR, "Unable to hide cursor");

done:

	return ret;
}

void video_release(video_t* video)
{
	drmDropMaster(video->fd);
}

void video_close(video_t *video)
{
	struct drm_mode_destroy_dumb destroy_dumb;

	if (!video)
		return;

	video_release(video);

	destroy_dumb.handle = video->buffer_handle;
	drmIoctl(video->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);

	if (video->fd >= 0) {
		if (video->main_monitor_connector) {
			disable_connector(video->fd, video->drm_resources,
					video->main_monitor_connector);
			drmModeFreeConnector(video->main_monitor_connector);
			video->main_monitor_connector = NULL;
		}

		if (video->crtc) {
			drmModeFreeCrtc(video->crtc);
			video->crtc = NULL;
		}

		if (video->drm_resources) {
			drmModeFreeResources(video->drm_resources);
			video->drm_resources = NULL;
		}

		drmClose(video->fd);
		video->fd = -1;
	}

	free(video);

}


uint32_t* video_lock(video_t *video)
{
	if (video->lock.count == 0) {
		video->lock.map =
			mmap(0, video->buffer_properties.size, PROT_READ | PROT_WRITE,
					MAP_SHARED, video->fd, video->lock.map_offset);
		if (video->lock.map == MAP_FAILED) {
			LOG(ERROR, "mmap failed");
			return NULL;
		}
	}
	return video->lock.map;
}

void video_unlock(video_t *video)
{
	/* XXX Cache flush maybe? */
	if (video->lock.count > 0) {
		video->lock.count--;
	}

	if (video->lock.count == 0) {
		munmap(video->lock.map, video->buffer_properties.size);
	}
}

bool video_load_gamma_ramp(video_t *video, const char* filename)
{
	int i;
	int r = 0;
	unsigned char red[kGammaSize];
	unsigned char green[kGammaSize];
	unsigned char blue[kGammaSize];
	gamma_ramp_t *ramp;

	FILE* f = fopen(filename, "rb");
	if (f == NULL)
		return false;

	ramp = &video->gamma_ramp;

	r += fread(red, sizeof(red), 1, f);
	r += fread(green, sizeof(green), 1, f);
	r += fread(blue, sizeof(blue), 1, f);
	fclose(f);

	if (r != 3)
		return false;

	for (i = 0; i < kGammaSize; ++i) {
		ramp->red[i]   = (uint16_t)red[i] * 257;
		ramp->green[i] = (uint16_t)green[i] * 257;
		ramp->blue[i]  = (uint16_t)blue[i] * 257;
	}

	return true;
}

bool video_set_gamma(video_t* video, const char *filename)
{
	bool status;
	drmModeCrtcPtr mode;
	int drm_status;

	status = video_load_gamma_ramp(video, filename);
	if (status == false) {
		LOG(WARNING, "Unable to load gamma ramp");
		return false;
	}

	mode = drmModeGetCrtc(video->fd, video->crtc->crtc_id);
	drm_status = drmModeCrtcSetGamma(video->fd,
			mode->crtc_id,
			mode->gamma_size,
			video->gamma_ramp.red,
			video->gamma_ramp.green,
			video->gamma_ramp.blue);

	return drm_status == 0;
}


buffer_properties_t* video_get_buffer_properties(video_t *video)
{
	return &video->buffer_properties;
}

int32_t video_getwidth(video_t *video)
{
	return video->buffer_properties.width;
}

int32_t video_getheight(video_t *video)
{
	return video->buffer_properties.height;
}


int32_t video_getpitch(video_t *video)
{
	return video->buffer_properties.pitch;
}

int32_t video_getscaling(video_t *video)
{
	return video->buffer_properties.scaling;
}

