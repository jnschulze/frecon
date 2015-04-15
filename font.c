/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "font.h"
#include "glyphs.h"
#include "util.h"

#define UNICODE_REPLACEMENT_CHARACTER_CODE_POINT 0xFFFD

static int font_scaling;

void font_init(int scaling)
{
	font_scaling = scaling;
}

void font_get_size(uint32_t *char_width, uint32_t *char_height)
{
	*char_width = GLYPH_WIDTH * font_scaling;
	*char_height = GLYPH_HEIGHT * font_scaling;
}

void font_fillchar(uint32_t *dst_pointer, int dst_char_x, int dst_char_y,
		   int32_t pitch, uint32_t front_color, uint32_t back_color)
{
	int dst_x = dst_char_x * GLYPH_WIDTH * font_scaling;
	int dst_y = dst_char_y * GLYPH_HEIGHT * font_scaling;

	for (int j = 0; j < GLYPH_HEIGHT * font_scaling; j++)
		for (int i = 0; i < GLYPH_WIDTH * font_scaling; i++)
			dst_pointer[dst_x + i + (dst_y + j) * pitch / 4] =
			    back_color;
}

void font_render(uint32_t *dst_pointer, int dst_char_x, int dst_char_y,
		 int32_t pitch, uint32_t ch, uint32_t front_color,
		 uint32_t back_color)
{
	int dst_x = dst_char_x * GLYPH_WIDTH * font_scaling;
	int dst_y = dst_char_y * GLYPH_HEIGHT * font_scaling;

	int32_t glyph_index = code_point_to_glpyh_index(ch);
	if (glyph_index < 0) {
		glyph_index = code_point_to_glpyh_index(
			UNICODE_REPLACEMENT_CHARACTER_CODE_POINT);
		if (glyph_index < 0) {
			return;
		}
	}

	const uint8_t *glyph = glyphs[glyph_index];

	for (int j = 0; j < GLYPH_HEIGHT; j++)
		for (int i = 0; i < GLYPH_WIDTH; i++) {
			uint8_t glyph_pixel = glyph[i / 8 +
						    j * GLYPH_BYTES_PER_ROW];

			uint32_t pixel = glyph_pixel & (0x1 << (7 - (i % 8))) ?
				front_color : back_color;

			for(int sx = 0; sx < font_scaling; sx++)
				for(int sy = 0; sy < font_scaling; sy++)
					dst_pointer[dst_x + font_scaling * i +
						    sx + (dst_y + font_scaling *
						    j + sy) * pitch / 4] = pixel;
		}
}
