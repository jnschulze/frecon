/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include "dbus.h"
#include "util.h"

struct _dbus_t {
	DBusConnection *conn;
	int terminate;
	struct {
		DBusObjectPathVTable vtable;
		const char* interface;
		const char* signal;
		const char* rule;
		void* user_data;
		dbus_message_handler_t signal_handler;
	} signal;
};

dbus_t* dbus_init()
{
	dbus_t* new_dbus;
	DBusError err;

	dbus_error_init(&err);

	new_dbus = (dbus_t*)calloc(1, sizeof(*new_dbus));

	new_dbus->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (dbus_error_is_set(&err)) {
		LOG(ERROR, "Cannot get dbus connection");
		free(new_dbus);
		return NULL;
	}

	dbus_connection_set_exit_on_disconnect(new_dbus->conn, FALSE);

	return new_dbus;
}


bool dbus_method_call0(dbus_t* dbus, const char* service_name,
		const char* service_path, const char* service_interface,
		const char* method)
{
	DBusMessage *msg = NULL;

	msg = dbus_message_new_method_call(service_name,
			service_path, service_interface, method);

	if (!msg)
		return false;

	if (!dbus_connection_send_with_reply_and_block(dbus->conn,
				msg, -1, NULL)) {
		dbus_message_unref(msg);
		return false;
	}

	dbus_connection_flush(dbus->conn);
	dbus_message_unref(msg);

	return true;
}

bool dbus_method_call1(dbus_t* dbus, const char* service_name,
		const char* service_path, const char* service_interface,
		const char* method, int* param)
{
	DBusMessage *msg = NULL;

	msg = dbus_message_new_method_call(service_name,
			service_path, service_interface, method);

	if (!msg)
		return false;

	if (!dbus_message_append_args(msg,
				DBUS_TYPE_INT32, param, DBUS_TYPE_INVALID)) {
		dbus_message_unref(msg);
		return false;
	}

	if (!dbus_connection_send_with_reply_and_block(dbus->conn,
				msg, -1, NULL)) {
		dbus_message_unref(msg);
		return false;
	}

	dbus_connection_flush(dbus->conn);
	dbus_message_unref(msg);

	return true;
}

static void
dbus_path_unregister_function(DBusConnection *connection, void *user_data)
{
}

static DBusHandlerResult
dbus_message_function(DBusConnection *connection,
		DBusMessage *message, void* user_data)
{
	dbus_t* dbus = (dbus_t*)user_data;

	if (dbus_message_is_signal(message, dbus->signal.interface,
				dbus->signal.signal)) {
		dbus->signal.signal_handler(dbus, dbus->signal.user_data);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

bool dbus_signal_match_handler(
		dbus_t* dbus,
		const char* signal,
		const char* path,
		const char* interface,
		const char* rule,
		dbus_message_handler_t handler,
		void *user_data)
{
	DBusError	 err;
	dbus->signal.vtable.unregister_function = dbus_path_unregister_function;
	dbus->signal.vtable.message_function = dbus_message_function;
	dbus->signal.signal_handler = handler;
	dbus->signal.signal = signal;
	dbus->signal.user_data = user_data;
	dbus->signal.interface = interface;

	if (!dbus_connection_register_object_path(dbus->conn, path,
				&dbus->signal.vtable, dbus)) {
		LOG(ERROR, "register_object_path failed");
		return false;
	}

	dbus_error_init(&err);
	dbus_bus_add_match(dbus->conn, rule, &err);
	if (dbus_error_is_set(&err)) {
		LOG(ERROR, "add_match failed: %s", err.message);
		return false;
	}

	return true;
}


int dbus_wait_for_messages(dbus_t *dbus, int64_t timeout_ms)
{
	int ret = DBUS_STATUS_NOERROR;
	int64_t terminate_ms = -1;
	if (timeout_ms > 0)
		terminate_ms = get_monotonic_time_ms() + timeout_ms;

	while (dbus_connection_read_write_dispatch(dbus->conn, -1)) {
		if ((terminate_ms > 0) && (get_monotonic_time_ms() > terminate_ms)) {
			dbus->terminate = true;
			ret = DBUS_STATUS_TIMEOUT;
		}

		if (dbus->terminate)
			break;
	}

	return ret;
}


void dbus_stop_wait(dbus_t* dbus)
{
	dbus->terminate = true;
}


void dbus_destroy(dbus_t* dbus)
{
	/* FIXME - not sure what the right counterpart to
		dbus_bus_get() is, unref documentation is rather
		unclear. Not a big issue but it would be nice to
		clean up properly here */
	/* dbus_connection_unref(dbus->conn); */
	free(dbus);
}
