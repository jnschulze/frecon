/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <libtsm.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "font.h"
#include "input.h"
#include "shl_pty.h"
#include "term.h"
#include "util.h"
#include "video.h"

static terminal_t* terminals[MAX_TERMINALS];
static uint32_t current_terminal = 0;

struct term {
	struct tsm_screen* screen;
	struct tsm_vte* vte;
	struct shl_pty* pty;
	int pty_bridge;
	int pid;
	tsm_age_t age;
	int char_x, char_y;
	int pitch;
	uint32_t* dst_image;
};

struct _terminal_t {
	uint32_t background;
	bool background_valid;
	video_t* video;
	struct term* term;
	bool active;
	char** exec;
};


static char* interactive_cmd_line[] = {
	"/sbin/agetty",
	"-",
	"9600",
	"xterm",
	NULL
};


static char* noninteractive_cmd_line[] = {
	"/bin/cat",
	NULL
};


static void __attribute__ ((noreturn)) term_run_child(terminal_t* terminal)
{
	/* XXX figure out how to fix "top" for xterm-256color */
	setenv("TERM", "xterm", 1);
	execve(terminal->exec[0], terminal->exec, environ);
	exit(1);
}

static int term_draw_cell(struct tsm_screen* screen, uint32_t id,
			  const uint32_t* ch, size_t len,
			  unsigned int cwidth, unsigned int posx,
			  unsigned int posy,
			  const struct tsm_screen_attr* attr,
			  tsm_age_t age, void* data)
{
	terminal_t* terminal = (terminal_t*)data;
	uint32_t front_color, back_color;
	uint8_t br, bb, bg;
	uint32_t luminance;

	if (age && terminal->term->age && age <= terminal->term->age)
		return 0;

	if (terminal->background_valid) {
		br = (terminal->background >> 16) & 0xFF;
		bg = (terminal->background >> 8) & 0xFF;
		bb = (terminal->background) & 0xFF;
		luminance = (3 * br + bb + 4 * bg) >> 3;

		/*
		 * FIXME: black is chosen on a dark background, but it uses the
		 * default color for light backgrounds
		 */
		if (luminance > 128) {
			front_color = 0;
			back_color = terminal->background;
		} else {
			front_color = (attr->fr << 16) | (attr->fg << 8) | attr->fb;
			back_color = terminal->background;
		}
	} else {
			front_color = (attr->fr << 16) | (attr->fg << 8) | attr->fb;
			back_color = (attr->br << 16) | (attr->bg << 8) | attr->bb;
	}

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

static void term_redraw(terminal_t* terminal)
{
	uint32_t* video_buffer;
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

static void term_read_cb(struct shl_pty* pty, char* u8, size_t len, void* data)
{
	terminal_t* terminal = (terminal_t*)data;

	tsm_vte_input(terminal->term->vte, u8, len);

	term_redraw(terminal);
}

static void term_write_cb(struct tsm_vte* vte, const char* u8, size_t len,
				void* data)
{
	struct term* term = data;
	int r;

	r = shl_pty_write(term->pty, u8, len);
	if (r < 0)
		LOG(ERROR, "OOM in pty-write (%d)", r);

	shl_pty_dispatch(term->pty);
}

static const char* sev2str_table[] = {
	"FATAL",
	"ALERT",
	"CRITICAL",
	"ERROR",
	"WARNING",
	"NOTICE",
	"INFO",
	"DEBUG"
};

static const char* sev2str(unsigned int sev)
{
	if (sev > 7)
		return "DEBUG";

	return sev2str_table[sev];
}

#ifdef __clang__
__attribute__((__format__ (__printf__, 7, 0)))
#endif
static void log_tsm(void* data, const char* file, int line, const char* fn,
		    const char* subs, unsigned int sev, const char* format,
		    va_list args)
{
	fprintf(stderr, "%s: %s: ", sev2str(sev), subs);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

static int term_resize(terminal_t* term)
{
	uint32_t char_width, char_height;
	int status;

	font_init(video_getscaling(term->video));
	font_get_size(&char_width, &char_height);

	term->term->char_x = video_getwidth(term->video) / char_width;
	term->term->char_y = video_getheight(term->video) / char_height;
	term->term->pitch = video_getpitch(term->video);

	status = tsm_screen_resize(term->term->screen,
				   term->term->char_x, term->term->char_y);
	if (status < 0) {
		font_free();
		return -1;
	}

	status = shl_pty_resize(term->term->pty, term->term->char_x,
				term->term->char_y);
	if (status < 0) {
		font_free();
		return -1;
	}

	return 0;
}

terminal_t* term_init(bool interactive, video_t* video)
{
	const int scrollback_size = 200;
	int status;
	terminal_t* new_terminal;

	new_terminal = (terminal_t*)calloc(1, sizeof(*new_terminal));
	if (!new_terminal)
		return NULL;

	new_terminal->background_valid = false;

	if (video) {
		new_terminal->video = video;
		video_addref(video);
	}
	else
		new_terminal->video = video_init();

	if (!new_terminal->video) {
		term_close(new_terminal);
		return NULL;
	}

	new_terminal->term = (struct term*)calloc(1, sizeof(*new_terminal->term));
	if (!new_terminal->term) {
		term_close(new_terminal);
		return NULL;
	}

	if (interactive)
		new_terminal->exec = interactive_cmd_line;
	else
		new_terminal->exec = noninteractive_cmd_line;

	status = tsm_screen_new(&new_terminal->term->screen,
			log_tsm, new_terminal->term);
	if (status < 0) {
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
			term_read_cb, new_terminal, 1, 1);

	if (status < 0) {
		term_close(new_terminal);
		return NULL;
	} else if (status == 0) {
		term_run_child(new_terminal);
		exit(1);
	}

	status = shl_pty_bridge_add(new_terminal->term->pty_bridge, new_terminal->term->pty);
	if (status) {
		shl_pty_close(new_terminal->term->pty);
		term_close(new_terminal);
		return NULL;
	}

	new_terminal->term->pid = shl_pty_get_child(new_terminal->term->pty);

	status = term_resize(new_terminal);
	if (status < 0) {
		shl_pty_close(new_terminal->term->pty);
		term_close(new_terminal);
		return NULL;
	}

	if (interactive)
		new_terminal->active = true;

	return new_terminal;
}

void term_activate(terminal_t* terminal)
{
	term_set_current_to(terminal);
	terminal->active = true;
	video_setmode(terminal->video);
	term_redraw(terminal);
}

void term_deactivate(terminal_t* terminal)
{
	if (!terminal->active)
		return;

	terminal->active = false;
	video_release(terminal->video);
}

void term_close(terminal_t* term)
{
	if (!term)
		return;

	if (term->video) {
		video_close(term->video);
		term->video = NULL;
	}

	if (term->term) {
		free(term->term);
		term->term = NULL;
	}

	font_free();
	free(term);
}

bool term_is_child_done(terminal_t* terminal)
{
	int status;
	int ret;
	ret = waitpid(terminal->term->pid, &status, WNOHANG);

	if ((ret == -1) && (errno == ECHILD)) {
		return false;
	}
	return ret != 0;
}

void term_page_up(terminal_t* terminal)
{
	tsm_screen_sb_page_up(terminal->term->screen, 1);
	term_redraw(terminal);
}

void term_page_down(terminal_t* terminal)
{
	tsm_screen_sb_page_down(terminal->term->screen, 1);
	term_redraw(terminal);
}

void term_line_up(terminal_t* terminal)
{
	tsm_screen_sb_up(terminal->term->screen, 1);
	term_redraw(terminal);
}

void term_line_down(terminal_t* terminal)
{
	tsm_screen_sb_down(terminal->term->screen, 1);
	term_redraw(terminal);
}

bool term_is_valid(terminal_t* terminal)
{
	return ((terminal != NULL) && (terminal->term != NULL));
}

int term_fd(terminal_t* terminal)
{
	if (term_is_valid(terminal))
		return terminal->term->pty_bridge;
	else
		return -1;
}

void term_dispatch_io(terminal_t* terminal, fd_set* read_set)
{
	if (term_is_valid(terminal))
		if (FD_ISSET(terminal->term->pty_bridge, read_set))
			shl_pty_bridge_dispatch(terminal->term->pty_bridge, 0);
}

bool term_exception(terminal_t* terminal, fd_set* exception_set)
{
	if (term_is_valid(terminal)) {
		if (terminal->term->pty_bridge >= 0) {
			return FD_ISSET(terminal->term->pty_bridge,
					exception_set);
		}
	}

	return false;
}

bool term_is_active(terminal_t* terminal)
{
	if (term_is_valid(terminal))
		return terminal->active;

	return false;
}

int term_add_fds(terminal_t* terminal, fd_set* read_set, fd_set* exception_set)
{
	int maxfd = 0;

	if (term_is_valid(terminal)) {
		if (terminal->term->pty_bridge >= 0) {
			maxfd = MAX(maxfd, terminal->term->pty_bridge);
			FD_SET(terminal->term->pty_bridge, read_set);
			FD_SET(terminal->term->pty_bridge, exception_set);
		}
	}
	return maxfd;
}

const char* term_get_ptsname(terminal_t* terminal)
{
	return ptsname(shl_pty_get_fd(terminal->term->pty));
}

void term_set_background(terminal_t* terminal, uint32_t bg)
{
	terminal->background = bg;
	terminal->background_valid = true;
}

int term_show_image(terminal_t* terminal, image_t* image)
{
	return image_show(image, terminal->video);
}

void term_write_message(terminal_t* terminal, char* message)
{
	FILE* fp;

	fp = fopen(term_get_ptsname(terminal), "w");
	if (fp) {
		fputs(message, fp);
		fclose(fp);
	}
}

static void term_hide_cursor(terminal_t* terminal)
{
	term_write_message(terminal, "\033[?25l");
}

__attribute__ ((unused))
static void term_show_cursor(terminal_t* terminal)
{
	term_write_message(terminal, "\033[?25h");
}

video_t* term_getvideo(terminal_t* terminal)
{
	return terminal->video;
}

terminal_t* term_get_terminal(int num)
{
	return terminals[num];
}

void term_set_terminal(int num, terminal_t* terminal)
{
	terminals[num] = terminal;
}

terminal_t* term_create_term(int vt)
{
	terminal_t* terminal;

	terminal = term_get_terminal(vt - 1);
	if (term_is_active(terminal))
		return terminal;

	if (terminal == NULL) {
		term_set_terminal(vt - 1, term_init(false, NULL));
		terminal = term_get_terminal(vt - 1);
		if (!term_is_valid(terminal)) {
			LOG(ERROR, "create_term: Term init failed");
		}
	}

	return terminal;
}

terminal_t* term_create_splash_term(video_t* video)
{
	terminal_t* splash_terminal = term_init(false, video);
	term_set_terminal(SPLASH_TERMINAL, splash_terminal);

	// Hide the cursor on the splash screen
	term_hide_cursor(splash_terminal);

	return splash_terminal;
}

void term_destroy_splash_term()
{
	term_set_terminal(SPLASH_TERMINAL, NULL);
}

unsigned int term_get_max_terminals()
{
	return MAX_STD_TERMINALS;
}

void term_set_current(uint32_t t)
{
	if (t >= MAX_TERMINALS)
		LOG(ERROR, "set_current: larger than max");
	else
		current_terminal = t;
}

uint32_t term_get_current(void)
{
	return current_terminal;
}

terminal_t *term_get_current_terminal(void)
{
	return terminals[current_terminal];
}

void term_set_current_terminal(terminal_t *terminal)
{
	terminals[current_terminal] = terminal;
}

void term_set_current_to(terminal_t* terminal)
{
	if (!terminal) {
		terminals[current_terminal] = NULL;
		current_terminal = 0;
		return;
	}

	for (int i = 0; i < MAX_TERMINALS; i++) {
		if (terminal == terminals[i]) {
			current_terminal = i;
			return;
		}
	}
	LOG(ERROR, "set_current_to: terminal not in array");
}
