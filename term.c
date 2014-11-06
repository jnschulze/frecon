/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <libtsm.h>
#include <paths.h>
#include <stdio.h>
#include <sys/select.h>

#include "font.h"
#include "input.h"
#include "keysym.h"
#include "shl_pty.h"
#include "term.h"
#include "util.h"
#include "video.h"
#include "dbus_interface.h"

struct term {
	struct tsm_screen *screen;
	struct tsm_vte *vte;
	struct shl_pty *pty;
	int pty_bridge;
	int pid;
	tsm_age_t age;
	int char_x, char_y;
	int pitch;
	uint32_t *dst_image;
	int shift_state;
	int control_state;
	int alt_state;
};


static void __attribute__ ((noreturn)) term_run_child()
{
	char **argv = (char *[]) {
		getenv("SHELL") ? : _PATH_BSHELL,
		"-il",
		NULL
	};

	printf("Welcome to frecon!\n");
	printf("running %s\n", argv[0]);
	/* XXX figure out how to fix "top" for xterm-256color */
	setenv("TERM", "xterm", 1);
	execve(argv[0], argv, environ);
	exit(1);
}

static int term_draw_cell(struct tsm_screen *screen, uint32_t id,
				const uint32_t *ch, size_t len,
				unsigned int cwidth, unsigned int posx,
				unsigned int posy,
				const struct tsm_screen_attr *attr,
				tsm_age_t age, void *data)
{
	terminal_t *terminal = (terminal_t*)data;
	uint32_t front_color, back_color;

	if (age && terminal->term->age && age <= terminal->term->age)
		return 0;

	front_color = (attr->fr << 16) | (attr->fg << 8) | attr->fb;
	back_color = (attr->br << 16) | (attr->bg << 8) | attr->bb;

	if (attr->inverse) {
		uint32_t tmp = front_color;
		front_color = back_color;
		back_color = tmp;
	}

	if (len)
		font_render(terminal->term->dst_image, posx, posy, terminal->term->pitch, *ch,
					front_color, back_color);
	else
		font_fillchar(terminal->term->dst_image, posx, posy, terminal->term->pitch,
						front_color, back_color);

	return 0;
}

void term_redraw(terminal_t *terminal)
{
	uint32_t *video_buffer;
	video_buffer = video_lock(terminal->video);
	if (video_buffer != NULL) {
		terminal->term->dst_image = video_buffer;
		terminal->term->age =
			tsm_screen_draw(terminal->term->screen, term_draw_cell, terminal);
		video_unlock(terminal->video);
	}
}

void term_key_event(terminal_t* terminal, uint32_t keysym, int32_t unicode)
{

	if (tsm_vte_handle_keyboard(terminal->term->vte, keysym, 0, 0, unicode))
		tsm_screen_sb_reset(terminal->term->screen);

	term_redraw(terminal);
}

static void term_read_cb(struct shl_pty *pty, char *u8, size_t len, void *data)
{
	terminal_t *terminal = (terminal_t*)data;

	tsm_vte_input(terminal->term->vte, u8, len);

	term_redraw(terminal);
}

static void term_write_cb(struct tsm_vte *vte, const char *u8, size_t len,
				void *data)
{
	struct term *term = data;
	int r;

	r = shl_pty_write(term->pty, u8, len);
	if (r < 0)
		LOG(ERROR, "OOM in pty-write (%d)", r);

	shl_pty_dispatch(term->pty);
}

static const char *sev2str_table[] = {
	"FATAL",
	"ALERT",
	"CRITICAL",
	"ERROR",
	"WARNING",
	"NOTICE",
	"INFO",
	"DEBUG"
};

static const char *sev2str(unsigned int sev)
{
	if (sev > 7)
		return "DEBUG";

	return sev2str_table[sev];
}

#ifdef __clang__
__attribute__((__format__ (__printf__, 7, 0)))
#endif
static void log_tsm(void *data, const char *file, int line, const char *fn,
				const char *subs, unsigned int sev, const char *format,
				va_list args)
{
	fprintf(stderr, "%s: %s: ", sev2str(sev), subs);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

static int term_special_key(terminal_t *terminal, struct input_key_event *ev)
{
	unsigned int i;

	uint32_t ignore_keys[] = {
		BTN_TOUCH, // touchpad events
		BTN_TOOL_FINGER,
		BTN_TOOL_DOUBLETAP,
		BTN_TOOL_TRIPLETAP,
		BTN_TOOL_QUADTAP,
		BTN_TOOL_QUINTTAP,
		BTN_LEFT, // mouse buttons
		BTN_RIGHT,
		BTN_MIDDLE,
		BTN_SIDE,
		BTN_EXTRA,
		BTN_FORWARD,
		BTN_BACK,
		BTN_TASK
	};

	for (i = 0; i < ARRAY_SIZE(ignore_keys); i++)
		if (ev->code == ignore_keys[i])
			return 1;

	switch (ev->code) {
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
		terminal->term->shift_state = ! !ev->value;
		return 1;
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
		terminal->term->control_state = ! !ev->value;
		return 1;
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
		terminal->term->alt_state = ! !ev->value;
		return 1;
	}

	if (terminal->term->shift_state && ev->value) {
		switch (ev->code) {
		case KEY_PAGEUP:
			tsm_screen_sb_page_up(terminal->term->screen, 1);
			term_redraw(terminal);
			return 1;
		case KEY_PAGEDOWN:
			tsm_screen_sb_page_down(terminal->term->screen, 1);
			term_redraw(terminal);
			return 1;
		case KEY_UP:
			tsm_screen_sb_up(terminal->term->screen, 1);
			term_redraw(terminal);
			return 1;
		case KEY_DOWN:
			tsm_screen_sb_down(terminal->term->screen, 1);
			term_redraw(terminal);
			return 1;
		}
	}

	if (terminal->term->alt_state && terminal->term->control_state && ev->value) {
		switch (ev->code) {
			case KEY_F1:
				input_ungrab();
				terminal->active = false;
				(void)dbus_method_call0(terminal->dbus,
					kLibCrosServiceName,
					kLibCrosServicePath,
					kLibCrosServiceInterface,
					kTakeDisplayOwnership);
				break;
			case KEY_F2:
			case KEY_F3:
			case KEY_F4:
			case KEY_F5:
			case KEY_F6:
			case KEY_F7:
			case KEY_F8:
			case KEY_F9:
			case KEY_F10:
				(void)dbus_method_call0(terminal->dbus,
					kLibCrosServiceName,
					kLibCrosServicePath,
					kLibCrosServiceInterface,
					kReleaseDisplayOwnership);
				break;
		}

		if (ev->code == KEY_F2) {
			terminal->active = true;
			input_grab();
			video_setmode(terminal->video);
			term_redraw(terminal);
		}
		return 1;

	}


	return 0;
}

static void term_get_keysym_and_unicode(terminal_t* terminal,
		struct input_key_event *event,
		uint32_t *keysym, uint32_t *unicode)
{
	struct {
		uint32_t code;
		uint32_t keysym;
	} non_ascii_keys[] = {
		{ KEY_ESC, KEYSYM_ESC},
		{ KEY_HOME, KEYSYM_HOME},
		{ KEY_LEFT, KEYSYM_LEFT},
		{ KEY_UP, KEYSYM_UP},
		{ KEY_RIGHT, KEYSYM_RIGHT},
		{ KEY_DOWN, KEYSYM_DOWN},
		{ KEY_PAGEUP, KEYSYM_PAGEUP},
		{ KEY_PAGEDOWN, KEYSYM_PAGEDOWN},
		{ KEY_END, KEYSYM_END},
		{ KEY_INSERT, KEYSYM_INSERT},
		{ KEY_DELETE, KEYSYM_DELETE},
	};

	for (unsigned i = 0; i < ARRAY_SIZE(non_ascii_keys); i++) {
		if (non_ascii_keys[i].code == event->code) {
			*keysym = non_ascii_keys[i].keysym;
			*unicode = -1;
			return;
		}
	}

	if (event->code >= ARRAY_SIZE(keysym_table) / 2) {
		*keysym = '?';
	} else {
		*keysym = keysym_table[event->code * 2 + terminal->term->shift_state];
		if ((terminal->term->control_state) && isascii(*keysym))
			*keysym = tolower(*keysym) - 'a' + 1;
	}

	*unicode = *keysym;
}

int term_run(terminal_t* terminal)
{
	int pty_fd = terminal->term->pty_bridge;
	fd_set read_set, exception_set;

	video_setmode(terminal->video);

	while (1) {
		FD_ZERO(&read_set);
		FD_ZERO(&exception_set);
		FD_SET(pty_fd, &read_set);
		FD_SET(pty_fd, &exception_set);
		int maxfd = input_setfds(&read_set, &exception_set);

		maxfd = MAX(maxfd, pty_fd) + 1;

		select(maxfd, &read_set, NULL, &exception_set, NULL);

		if (FD_ISSET(pty_fd, &exception_set))
			return -1;

		struct input_key_event *event;
		event = input_get_event(&read_set, &exception_set);
		if (event) {
			if (!term_special_key(terminal, event) && event->value) {
				uint32_t keysym, unicode;
				if (terminal->active) {
					term_get_keysym_and_unicode(terminal, event,
									&keysym,
									&unicode);
					term_key_event(terminal, keysym, unicode);
				}
			}

			input_put_event(event);
		}

		if (FD_ISSET(pty_fd, &read_set)) {
			shl_pty_bridge_dispatch(terminal->term->pty_bridge, 0);
		}
	}
	return 0;
}

terminal_t* term_init(video_t* video)
{
	const int scrollback_size = 200;
	uint32_t char_width, char_height;
	int status;
	terminal_t *new_terminal;

	new_terminal = (terminal_t*)calloc(1, sizeof(*new_terminal));
	new_terminal->video = video;
	new_terminal->term = (struct term*)calloc(1, sizeof(*new_terminal->term));

	font_init(video_getscaling(video));
	font_get_size(&char_width, &char_height);

	new_terminal->term->char_x = video_getwidth(video) / char_width;
	new_terminal->term->char_y = video_getheight(video) / char_height;
	new_terminal->term->pitch = video_getpitch(video);

	status = tsm_screen_new(&new_terminal->term->screen,
			log_tsm, new_terminal->term);
	if (new_terminal < 0) {
		term_close(new_terminal);
		return NULL;
	}
	

	tsm_screen_set_max_sb(new_terminal->term->screen, scrollback_size);

	status = tsm_vte_new(&new_terminal->term->vte, new_terminal->term->screen,
			term_write_cb, new_terminal->term, log_tsm, new_terminal->term);

	if (status < 0) {
		term_close(new_terminal);
		return NULL;
	}

	new_terminal->term->pty_bridge = shl_pty_bridge_new();
	if (new_terminal->term->pty_bridge < 0) {
		term_close(new_terminal);
		return NULL;
	}

	status = shl_pty_open(&new_terminal->term->pty,
			term_read_cb, new_terminal, new_terminal->term->char_x,
			new_terminal->term->char_y);
	if (status < 0) {
		term_close(new_terminal);
		return NULL;
	} else if (status == 0) {
		term_run_child();
		exit(1);
	}

	status = shl_pty_bridge_add(new_terminal->term->pty_bridge, new_terminal->term->pty);
	if (status) {
		shl_pty_close(new_terminal->term->pty);
		term_close(new_terminal);
		return NULL;
	}

	new_terminal->term->pid = shl_pty_get_child(new_terminal->term->pty);

	status = tsm_screen_resize(new_terminal->term->screen,
			new_terminal->term->char_x, new_terminal->term->char_y);
	if (status < 0) {
		shl_pty_close(new_terminal->term->pty);
		term_close(new_terminal);
		return NULL;
	}

	status = shl_pty_resize(new_terminal->term->pty, new_terminal->term->char_x, new_terminal->term->char_y);
	if (status < 0) {
		shl_pty_close(new_terminal->term->pty);
		term_close(new_terminal);
		return NULL;
	}

	return new_terminal;
}

void term_set_dbus(terminal_t *term, dbus_t* dbus)
{
	term->dbus = dbus;
}

void term_close(terminal_t *term)
{
	if (!term)
		return;

	if (term->term) {
		free(term->term);
		term->term = NULL;
	}

	free(term);
}
