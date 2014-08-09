/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef VIDEO_H
#define VIDEO_H

int video_init(int32_t * width, int32_t * height, int32_t * pitch);
void video_close();
void *video_lock();
void video_unlock();

#endif
