/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drm_fourcc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "util.h"
#include "video.h"

static int fd;
static uint8_t *map;

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
					    drmModeRes * resources,
					    drmModeConnector * connector)
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
			      drmModeRes * resources,
			      drmModeConnector * connector)
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
						     drmModeRes * resources,
						     unsigned type)
{
	int i;
	for (i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector;

		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector) {
			if ((connector->connector_type == type) &&
			    (is_connector_used(fd, resources, connector)))
				return connector;

			drmModeFreeConnector(connector);
		}
	}
	return NULL;
}

static drmModeConnector *find_first_used_connector(int fd,
						   drmModeRes * resources)
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

static drmModeConnector *find_main_monitor(int fd, drmModeRes * resources)
{
	unsigned i = 0;
	/*
	 * Find the LVDS/eDP/DSI connectors. Those are the main screens.
	 */
	unsigned kConnectorPriority[] = {
		DRM_MODE_CONNECTOR_LVDS,
		DRM_MODE_CONNECTOR_eDP,
		/*
		 * XXX update the kernel headers to support DSI
		 * see crbug.com/402127
		 */
		/* DRM_MODE_CONNECTOR_DSI, */
	};

	drmModeConnector *main_monitor_connector = NULL;
	do {
		main_monitor_connector = find_used_connector_by_type(fd,
								     resources,
								     kConnectorPriority
								     [i]);
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
			      drmModeRes * resources,
			      drmModeConnector * connector)
{
	drmModeCrtc *crtc = find_crtc_for_connector(fd, resources, connector);

	if (crtc) {
		drmModeSetCrtc(fd, crtc->crtc_id, 0,	// buffer_id
			       0, 0,	// x,y
			       NULL,	// connectors
			       0,	// connector_count
			       NULL);	// mode
		drmModeFreeCrtc(crtc);
	}
}

static void disable_non_main_connectors(int fd,
					drmModeRes * resources,
					drmModeConnector * main_connector)
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

static int video_buffer_create(drmModeCrtc * crtc, drmModeConnector * connector,
			       int *pitch)
{
	struct drm_mode_create_dumb create_dumb;
	int ret;

	memset(&create_dumb, 0, sizeof (create_dumb));
	create_dumb.bpp = 32;
	create_dumb.width = crtc->mode.hdisplay;
	create_dumb.height = crtc->mode.vdisplay;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
	if (ret)
		return ret;

	struct drm_mode_map_dumb map_dumb;
	map_dumb.handle = create_dumb.handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb);
	if (ret)
		goto destroy_buffer;

	map =
	    mmap(0, create_dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 map_dumb.offset);
	if (!map)
		goto destroy_buffer;

	uint32_t offset = 0;
	uint32_t fb_id;
	ret = drmModeAddFB2(fd, crtc->mode.hdisplay, crtc->mode.vdisplay,
			    DRM_FORMAT_XRGB8888, &create_dumb.handle,
			    &create_dumb.pitch, &offset, &fb_id, 0);
	if (ret)
		goto unmap_buffer;

	*pitch = create_dumb.pitch;

	ret = drmModeSetCrtc(fd, crtc->crtc_id, fb_id,	// buffer_id
			     0, 0,	// x,y
			     &connector->connector_id,	// connectors
			     1,	// connector_count
			     &crtc->mode);	// mode

	return ret;

unmap_buffer:
	munmap(map, create_dumb.size);

destroy_buffer:
	;
	struct drm_mode_destroy_dumb destroy_dumb;
	destroy_dumb.handle = create_dumb.handle;

	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);

	return ret;
}

int video_init(int32_t * width, int32_t * height, int32_t * pitch)
{
	fd = kms_open();

	if (fd < 0) {
		printf("Unable to open a KMS module\n");
		return 1;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		printf("Unable to get mode resources\n");
		goto fail;
	}

	drmModeConnector *main_monitor_connector =
	    find_main_monitor(fd, resources);

	if (!main_monitor_connector) {
		drmModeFreeResources(resources);
		goto fail;
	}

	disable_non_main_connectors(fd, resources, main_monitor_connector);

	drmModeCrtc *crtc =
	    find_crtc_for_connector(fd, resources, main_monitor_connector);
	if (!crtc) {
		drmModeFreeResources(resources);
		drmModeFreeConnector(main_monitor_connector);
		goto fail;
	}

	if (video_buffer_create(crtc, main_monitor_connector, pitch)) {
		drmModeFreeResources(resources);
		drmModeFreeConnector(main_monitor_connector);
		drmModeFreeCrtc(crtc);
		goto fail;
	}

	*width = crtc->mode.hdisplay;
	*height = crtc->mode.vdisplay;

	drmModeFreeResources(resources);
	drmModeFreeConnector(main_monitor_connector);
	drmModeFreeCrtc(crtc);

	return 0;

fail:
	drmClose(fd);
	return 1;
}

void video_close()
{
	if (fd >= 0) {
		drmClose(fd);
		fd = -1;
	}
}

void *video_lock()
{
	return map;
}

void video_unlock()
{
	/* XXX Cache flush maybe? */
}
