/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FONT_H
#define FONT_H

void font_init(int scaling);
void font_free();
void font_fillchar(uint32_t* dst_pointer, int dst_char_x, int dst_char_y,
		   int32_t pitch, uint32_t front_color, uint32_t back_color);
void font_render(uint32_t* dst_pointer, int dst_char_x, int dst_char_y,
		 int32_t pitch, uint32_t ch, uint32_t front_color,
		 uint32_t back_color);
void font_get_size(uint32_t* char_width, uint32_t* char_height);
int font_get_scaling();

#endif
