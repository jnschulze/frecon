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

static int font_scaling = 1;
static int glyph_size = GLYPH_BYTES_PER_ROW * GLYPH_HEIGHT;
static uint8_t *prescaled_glyphs = NULL;

static uint8_t get_bit(const uint8_t *buffer, int bit_offset)
{
	return (buffer[bit_offset / 8] >> (7 - (bit_offset % 8))) & 0x1;
}

static void set_bit(uint8_t *buffer, int bit_offset)
{
	buffer[bit_offset / 8] |= (0x1 << (7 - (bit_offset % 8)));
}

static uint8_t glyph_pixel(const uint8_t *glyph, int x, int y)
{
	if (x < 0 || x >= GLYPH_WIDTH || y < 0 || y >= GLYPH_HEIGHT)
		return 0;
	return get_bit(&glyph[y * GLYPH_BYTES_PER_ROW], x);
}

static uint8_t scale_pixel(uint8_t neighbors, int sx, int sy, int scaling)
{
	/*
	 * Scale a pixel by a factor of |scaling| using the following rules:
	 * If the center pixel is 1, always return 1;
	 * If the center pixel is 0:
	 *   Return 0 if all four neighbor pixels are 1;
	 *   Otherwise, return 1 if two adjacent neighbor pixels are 1, and
	 *     (sx, sy) falls inside the isosceles right triangle adjoining
	 *     these two neighbor pixels and with legs of length |scaling - 1|.
	 */
	return ((neighbors & 0x1) ||
		(neighbors != 0x1e &&
		((sy < sx && (neighbors & 0xc) == 0xc) ||
		(sx < sy && (neighbors & 0x12) == 0x12) ||
		(sx + sy > scaling - 1 && (neighbors & 0x14) == 0x14) ||
		(sx + sy < scaling - 1 && (neighbors & 0xa) == 0xa))));
}

static void scale_glyph(uint8_t *dst, const uint8_t *src, int scaling)
{
	for (int y = 0; y < GLYPH_HEIGHT; y++) {
		for (int x = 0; x < GLYPH_WIDTH; x++) {
			uint8_t neighbors =
				glyph_pixel(src, x, y) |
				(glyph_pixel(src, x - 1, y) << 1) |
				(glyph_pixel(src, x + 1, y) << 2) |
				(glyph_pixel(src, x, y - 1) << 3) |
				(glyph_pixel(src, x, y + 1) << 4);
			for (int sy = 0; sy < scaling; sy++) {
				uint8_t *dst_row = &dst[(y * scaling + sy) *
					GLYPH_BYTES_PER_ROW * scaling];
				for (int sx = 0; sx < scaling; sx++) {
					if (scale_pixel(neighbors, sx, sy,
							scaling)) {
						set_bit(dst_row,
							x * scaling + sx);
					}
				}
			}
		}
	}
}

static void prescale_font(int scaling)
{
	int glyph_count = sizeof(glyphs) / (GLYPH_BYTES_PER_ROW * GLYPH_HEIGHT);
	glyph_size = GLYPH_BYTES_PER_ROW * GLYPH_HEIGHT * scaling * scaling;
	prescaled_glyphs = (uint8_t *)calloc(glyph_count, glyph_size);
	for (int i = 0; i < glyph_count; i++) {
		const uint8_t *src_glyph = glyphs[i];
		uint8_t *dst_glyph = &prescaled_glyphs[i * glyph_size];
		scale_glyph(dst_glyph, src_glyph, scaling);
	}
}

void font_init(int scaling)
{
	font_scaling = scaling;
	if (scaling > 1) {
		prescale_font(scaling);
	}
}

void font_free()
{
	if (prescaled_glyphs) {
		free(prescaled_glyphs);
		prescaled_glyphs = NULL;
	}
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

	int32_t glyph_index = code_point_to_glyph_index(ch);
	if (glyph_index < 0) {
		glyph_index = code_point_to_glyph_index(
			UNICODE_REPLACEMENT_CHARACTER_CODE_POINT);
		if (glyph_index < 0) {
			return;
		}
	}

	const uint8_t *glyph;
	if (font_scaling == 1) {
		glyph = glyphs[glyph_index];
	} else {
		glyph = &prescaled_glyphs[glyph_index * glyph_size];
	}

	for (int j = 0; j < GLYPH_HEIGHT * font_scaling; j++) {
		const uint8_t *src_row =
			&glyph[j * GLYPH_BYTES_PER_ROW * font_scaling];
		for (int i = 0; i < GLYPH_WIDTH * font_scaling; i++) {
			dst_pointer[dst_x + i + (dst_y + j) * pitch / 4] =
				get_bit(src_row, i) ? front_color : back_color;
		}
	}
}
