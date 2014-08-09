/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TERM_H
#define TERM_H

#include "input.h"

int term_init(int32_t width, int32_t height, int32_t pitch);
void term_close();
void term_redraw();
int term_run();

#endif
