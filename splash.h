/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SPLASH_H
#define SPLASH_H

#include "video.h"

typedef struct _splash_t splash_t;

splash_t* splash_init(video_t *video);
int splash_destroy(splash_t*);
int splash_add_image(splash_t*, const char* path);
int splash_set_frame_rate(splash_t *splash, int32_t rate);
int splash_set_clear(splash_t* splash, int32_t clear_color);
void splash_set_dbus(splash_t* splash, dbus_t* dbus);
void splash_set_devmode(splash_t* splash);
int splash_run(splash_t*, dbus_t **dbus);

#endif  // SPLASH_H
