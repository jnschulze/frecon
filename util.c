/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

static int openfd(char *path, int flags, int reqfd)
{
	int fd = open(path, flags);
	if (fd < 0)
		return -1;

	if (fd == reqfd)
		return reqfd;

	if (dup2(fd, reqfd) >= 0) {
		close(fd);
		return reqfd;
	}

	close(fd);
	return -1;
}

static int init_daemon_stdio(void)
{
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	if (openfd("/dev/null", O_RDONLY, STDIN_FILENO) < 0)
		return -1;

	if (openfd("/dev/kmsg", O_WRONLY, STDOUT_FILENO) < 0)
		return -1;

	if (openfd("/dev/kmsg", O_WRONLY, STDERR_FILENO) < 0)
		return -1;

	return 0;
}

void daemonize()
{
	pid_t pid;

	pid = fork();
	if (pid == -1)
		return;
	else if (pid != 0)
		exit(EXIT_SUCCESS);

	if (setsid() == -1)
		return;

	init_daemon_stdio();
}

static int is_valid_fd(int fd)
{
    return fcntl(fd, F_GETFL) != -1 || errno != EBADF;
}

void fix_stdio(void)
{
	if (!is_valid_fd(STDIN_FILENO)
	    || !is_valid_fd(STDOUT_FILENO)
	    || !is_valid_fd(STDERR_FILENO))
		init_daemon_stdio();
}

#ifdef __clang__
__attribute__((format (__printf__, 2, 0)))
#endif
void LOG(int severity, const char* fmt, ...)
{
	va_list arg_list;
	fprintf(stderr, "frecon(%d): ", getpid());
	va_start( arg_list, fmt);
	vfprintf(stderr, fmt, arg_list);
	va_end(arg_list);
	fprintf(stderr, "\n");
}

void parse_location(char* loc_str, int* x, int* y)
{
	int count = 0;
	char* savedptr;
	char* str;
	int* results[] = {x, y};
	long tmp;

	for (char* token = str = loc_str; token != NULL; str = NULL) {
		if (count > 1)
			break;

		token = strtok_r(str, ",", &savedptr);
		if (token) {
			tmp = MIN(INT_MAX, strtol(token, NULL, 0));
			*(results[count++]) = (int)tmp;
		}
	}
}

void parse_filespec(char* filespec, char* filename,
		    int32_t* offset_x, int32_t* offset_y, uint32_t* duration,
		    uint32_t default_duration,
		    int32_t default_x, int32_t default_y)
{
	char* saved_ptr;
	char* token;

	// defaults
	*offset_x = default_x;
	*offset_y = default_y;
	*duration = default_duration;

	token = filespec;
	token = strtok_r(token, ":", &saved_ptr);
	if (token)
		strcpy(filename, token);

	token = strtok_r(NULL, ":", &saved_ptr);
	if (token) {
		*duration = strtoul(token, NULL, 0);
		token = strtok_r(NULL, ",", &saved_ptr);
		if (token) {
			token = strtok_r(token, ",", &saved_ptr);
			if (token) {
				*offset_x = strtol(token, NULL, 0);
				token = strtok_r(token, ",", &saved_ptr);
				if (token)
					*offset_y = strtol(token, NULL, 0);
			}
		}
	}
}

void parse_image_option(char* optionstr, char** name, char** val)
{
	char** result[2] = { name, val };
	int count = 0;
	char* str;
	char* savedptr;

	for (char* token = str = optionstr; token != NULL; str = NULL) {
		if (count > 1)
			break;

		token = strtok_r(str, ":", &savedptr);
		if (token) {
			*(result[count]) = malloc(strlen(token) + 1);
			strcpy(*(result[count]), token);
			count++;
		}
	}
}

