/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TERM_H
#define TERM_H

#include "video.h"
#include "input.h"

typedef struct _terminal_t {
  video_t  *video;
  dbus_t   *dbus;
  struct term *term;
  bool active;
} terminal_t;

terminal_t *term_init(video_t *video);
void term_close(terminal_t* terminal);
void term_redraw(terminal_t* terminal);
void term_set_dbus(terminal_t* terminal, dbus_t* dbus);
int term_run(terminal_t* terminal);
void term_close(terminal_t* terminal);

#endif
