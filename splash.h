/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SPLASH_H
#define SPLASH_H

#include "video.h"

typedef struct _splash_t splash_t;

splash_t* splash_init();
int splash_destroy(splash_t*);
int splash_add_image(splash_t*, char* filespec);
int splash_set_clear(splash_t* splash, int32_t clear_color);
void splash_set_dbus(splash_t* splash, dbus_t* dbus);
void splash_set_devmode(splash_t* splash);
int splash_run(splash_t*, dbus_t **dbus);
void splash_set_offset(splash_t* splash, int32_t x, int32_t y);
int splash_num_images(splash_t* splash);
void splash_set_default_duration(splash_t* splash, uint32_t duration);
void splash_set_loop_start(splash_t* splash, int32_t start_location);
void splash_set_loop_duration(splash_t* splash, uint32_t duration);
void splash_set_loop_offset(splash_t* splash, int32_t x, int32_t y);

#endif  // SPLASH_H
