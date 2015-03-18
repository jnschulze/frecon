/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <pwd.h>

#include "util.h"

void sync_lock(bool acquire)
{
	static int lock = -1;
	int stat;
	struct passwd* pw;

	if (lock < 0)
		lock = open("/run/frecon", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	if (lock >= 0) {
		pw = getpwnam("chronos");
		if (pw) {
			stat = fchown(lock, pw->pw_uid, pw->pw_gid);
			if (stat != 0)
				LOG(ERROR, "fchown returned %d", stat);
		}

		if (flock(lock, acquire ? LOCK_EX : LOCK_UN) < 0)
			LOG(ERROR, "Failed to operate on synch_lock(acquire = %d):%d, lock=%d: %m",
					acquire, stat, lock);
	}
}


void daemonize()
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid == -1)
		return;
	else if (pid != 0)
		exit(EXIT_SUCCESS);

	if (setsid() == -1)
		return;

	// Re-direct stderr/stdout to the system message log
	close(0);
	close(1);
	close(2);

	open("/dev/kmsg", O_RDWR);

	fd = dup(0);
	if (fd != STDOUT_FILENO) {
		close(fd);
		return;
	}
	fd = dup(0);
	if (fd != STDERR_FILENO) {
		close(fd);
		return;
	}
}

#ifdef __clang__
__attribute__((format (__printf__, 2, 0)))
#endif
void LOG(int severity, const char* fmt, ...)
{
	va_list arg_list;
	fprintf(stderr, "frecon: ");
	va_start( arg_list, fmt);
	vfprintf(stderr, fmt, arg_list);
	va_end(arg_list);
	fprintf(stderr, "\n");
}
