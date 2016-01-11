/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FRECON_DBUS_H
#define FRECON_DBUS_H

#include <sys/select.h>
#include <dbus/dbus.h>
#include <stdbool.h>
#include <memory.h>
#include <stdio.h>

#define DBUS_STATUS_NOERROR     (0)
#define DBUS_STATUS_TIMEOUT     (-1)

bool dbus_init();
void dbus_destroy(void);
int dbus_add_fds(fd_set* read_set, fd_set* exception_set);
void dbus_dispatch_io(void);
void dbus_report_user_activity(int activity_type);
void dbus_take_display_ownership(void);
void dbus_release_display_ownership(void);
bool dbus_is_initialized(void);


#endif // FRECON_DBUS_H
