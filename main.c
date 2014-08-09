/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <libtsm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "font.h"
#include "input.h"
#include "term.h"
#include "video.h"

int main()
{
	int32_t width, height, pitch;
	int ret;

	ret = video_init(&width, &height, &pitch);
	if (ret) {
		printf("Video init failed\n");
		return EXIT_FAILURE;
	}

	ret = input_init();
	if (ret) {
		printf("Input init failed\n");
		video_close();
		return EXIT_FAILURE;
	}

	ret = term_init(width, height, pitch);
	if (ret) {
		printf("Term init failed\n");
		input_close();
		video_close();
		return EXIT_FAILURE;
	}

	ret = term_run();

	input_close();
	term_close();
	video_close();

	return ret;
}
