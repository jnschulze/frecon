/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <png.h>

#include "video.h"


typedef struct _image_t image_t;

image_t* image_create();
void image_set_filename(image_t* image, char* filename);
void image_set_offset(image_t* image, int32_t offset_x, int32_t offset_y);
void image_set_location(image_t* image, uint32_t location_x, uint32_t location_y);
int image_load_image_from_file(image_t* image);
int image_show(image_t* image, video_t* video);
void image_release(image_t* image);
void image_destroy(image_t* image);

#endif
