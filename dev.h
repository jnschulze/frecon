/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DEV_H
#define DEV_H

int dev_init(void);
void dev_close(void);
void dev_add_existing_input_devs(void);
void dev_add_fds(fd_set* read_set, fd_set* exception_set, int *maxfd);
void dev_dispatch_io(fd_set* read_set, fd_set* exception_set);

#endif
