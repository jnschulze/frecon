/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TERM_H
#define TERM_H

#include "image.h"
#include "video.h"

#define MAX_STD_TERMINALS     (3)
#define NUM_SPLASH_TERMINAL   (1)
#define MAX_TERMINALS         (MAX_STD_TERMINALS + NUM_SPLASH_TERMINAL)
#define SPLASH_TERMINAL       (MAX_TERMINALS - 1)

typedef struct _terminal_t terminal_t;
terminal_t* term_init(bool interactive, video_t* video);
void term_close(terminal_t* terminal);
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
void term_activate(terminal_t*);
void term_deactivate(terminal_t* terminal);
void term_add_fds(terminal_t* terminal, fd_set* read_set, fd_set* exception_set, int *maxfd);
const char* term_get_ptsname(terminal_t* terminal);
void term_set_background(terminal_t* term, uint32_t bg);
int term_show_image(terminal_t* terminal, image_t* image);
void term_write_message(terminal_t* terminal, char* message);
video_t* term_getvideo(terminal_t* terminal);
terminal_t* term_get_terminal(int num);
void term_set_terminal(int num, terminal_t* terminal);
terminal_t* term_create_term(int vt);
terminal_t* term_create_splash_term(video_t* video);
void term_destroy_splash_term();
unsigned int term_get_max_terminals();
void term_set_current(uint32_t t);
uint32_t term_get_current(void);
terminal_t *term_get_current_terminal(void);
void term_set_current_terminal(terminal_t *terminal);
void term_set_current_to(terminal_t* terminal);

#endif
