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

terminal_t *term_init(unsigned int term_id);
void term_close(terminal_t* terminal);
void term_redraw(terminal_t* terminal);
void term_set_dbus(terminal_t* terminal, dbus_t* dbus);
void term_close(terminal_t* terminal);
void term_key_event(terminal_t* terminal, uint32_t keysym, int32_t unicode);
bool term_is_child_done(terminal_t* terminal);

void term_page_up(terminal_t* terminal);
void term_page_down(terminal_t* terminal);
void term_line_up(terminal_t* terminal);
void term_line_down(terminal_t* terminal);

bool term_is_valid(terminal_t* terminal);
int term_fd(terminal_t* terminal);
void term_dispatch_io(terminal_t* terminal, fd_set* read_set);
bool term_exception(terminal_t*, fd_set* exception_set);
bool term_is_active(terminal_t*);
void term_add_fd(terminal_t* terminal, fd_set* read_set, fd_set* exception_set);

#endif
