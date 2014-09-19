/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define ARRAY_SIZE(A) (sizeof(A)/sizeof(*(A)))

#define  MS_PER_SEC             (1000LL)
#define  NS_PER_SEC             (1000LL * 1000LL * 1000LL)
#define  NS_PER_MS              (NS_PER_SEC / MS_PER_SEC);

/* Returns the current CLOCK_MONOTONIC time in milliseconds. */
inline int64_t get_monotonic_time_ms() {
	struct timespec spec;
	clock_gettime(CLOCK_MONOTONIC, &spec);
	return MS_PER_SEC * spec.tv_sec + spec.tv_nsec / NS_PER_MS;
}

void LOG(int severity, const char* fmt, ...);
void daemonize();


#define ERROR                 (1)
#define WARNING               (2)
#define INFO                  (4)

#endif
