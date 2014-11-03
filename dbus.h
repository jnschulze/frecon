/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DBUS_H_
#define DBUS_H_

#include <dbus/dbus.h>
#include <stdbool.h>
#include <memory.h>
#include <stdio.h>

typedef struct _dbus_t dbus_t;

typedef void (*dbus_message_handler_t)(dbus_t*, void*);

dbus_t* dbus_init();
bool dbus_method_call0(dbus_t* dbus, const char* service_name,
    const char* service_path, const char* service_interface,
    const char* method);
bool dbus_method_call1(dbus_t* dbus, const char* service_name,
    const char* service_path, const char* service_interface,
    const char* method, int* param);
bool dbus_signal_match_handler(
    dbus_t* dbus,
    const char* signal,
    const char* path,
    const char* interface,
    const char* rule,
    dbus_message_handler_t handler,
    void *user_data);
void dbus_wait_for_messages(dbus_t *dbus);
void dbus_stop_wait(dbus_t* dbus);
void dbus_destroy(dbus_t* dbus);


#endif // DBUS_H_
