/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "dbus.h"
#include "dbus_interface.h"
#include "image.h"
#include "main.h"
#include "term.h"
#include "util.h"

#define COMMAND_MAKE_VT               "MakeVT"
#define COMMAND_SWITCH_VT             "SwitchVT"
#define COMMAND_TERMINATE             "Terminate"
#define COMMAND_IMAGE                 "Image"

#define DBUS_WAIT_DELAY_US             (50000)
#define DBUS_DEFAULT_DELAY             3000
#define DBUS_INIT_TIMEOUT_MS           (60*1000)

typedef struct _dbus_t dbus_t;

static void (*login_prompt_visible_callback)(void*) = NULL;
static void* login_prompt_visible_callback_userptr = NULL;
static bool chrome_is_already_up = false;
static bool dbus_connect_fail = false;
static int64_t dbus_connect_fail_time;
static bool dbus_first_init = true;
static int64_t dbus_first_init_time;

struct _dbus_t {
	DBusConnection* conn;
	DBusWatch* watch;
	int fd;
};

static dbus_t *dbus = NULL;

static DBusHandlerResult handle_switchvt(DBusConnection* connection,
					 DBusMessage* message)
{
	DBusMessage* reply;
	DBusMessage* msg;
	DBusError error;
	dbus_bool_t stat;
	terminal_t* terminal;
	unsigned int vt;

	dbus_error_init(&error);
	stat = dbus_message_get_args(message, &error, DBUS_TYPE_UINT32,
			&vt, DBUS_TYPE_INVALID);

	if (!stat) {
		LOG(ERROR, "SwitchVT method error, no VT argument");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (vt > term_get_max_terminals()) {
		LOG(ERROR, "SwtichVT: invalid terminal");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (vt == 0) {
		terminal = term_create_term(vt);
		if (term_is_active(terminal)) {
			term_deactivate(terminal);
			msg = dbus_message_new_method_call(
				kLibCrosServiceName,
				kLibCrosServicePath,
				kLibCrosServiceInterface,
				kTakeDisplayOwnership);
			dbus_connection_send_with_reply_and_block(connection, msg,
					DBUS_DEFAULT_DELAY, NULL);
		}
		reply = dbus_message_new_method_return(message);
		dbus_connection_send(connection, reply, NULL);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else {
		/*
		 * If we are switching to a new term, and if a
		 * given term is active, then de-activate the
		 * current terminal
		 */
		terminal = term_get_current_terminal();
		if (term_is_active(terminal))
			term_deactivate(terminal);

		terminal = term_create_term(vt);
		if (term_is_valid(terminal)) {
			msg = dbus_message_new_method_call(
				kLibCrosServiceName,
				kLibCrosServicePath,
				kLibCrosServiceInterface,
				kReleaseDisplayOwnership);
			dbus_connection_send_with_reply_and_block(connection, msg,
					DBUS_DEFAULT_DELAY, NULL);
			term_activate(terminal);

			reply = dbus_message_new_method_return(message);
			dbus_connection_send(connection, reply, NULL);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_makevt(DBusConnection* connection,
				       DBusMessage* message)
{
	DBusMessage* reply;
	DBusError error;
	dbus_bool_t stat;
	terminal_t* terminal;
	unsigned int vt;
	const char* reply_str;

	dbus_error_init(&error);
	stat = dbus_message_get_args(message, &error, DBUS_TYPE_UINT32,
			&vt, DBUS_TYPE_INVALID);

	if (!stat) {
		LOG(ERROR, "SwitchVT method error, not VT argument");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if ((vt < 1) || (vt > term_get_max_terminals())) {
		LOG(ERROR, "SwtichVT: invalid terminal");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	terminal = term_create_term(vt);
	reply_str = term_get_ptsname(terminal);

	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply,
			DBUS_TYPE_STRING, &reply_str,
			DBUS_TYPE_INVALID);
	dbus_connection_send(connection, reply, NULL);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_terminate(DBusConnection* connection,
					  DBusMessage* message)
{
	DBusMessage* reply;

	reply = dbus_message_new_method_return(message);
	dbus_connection_send(connection, reply, NULL);
	exit(EXIT_SUCCESS);
}

#define NUM_IMAGE_PARAMETERS     (2)
static DBusHandlerResult handle_image(DBusConnection* connection,
				      DBusMessage* message)
{
	DBusMessage* reply;
	DBusError error;
	dbus_bool_t stat;
	terminal_t* terminal;
	image_t* image;
	int i;
	int x, y;
	char* option[NUM_IMAGE_PARAMETERS];
	char* optname;
	char* optval;
	int status;

	dbus_error_init(&error);
	stat = dbus_message_get_args(message, &error,
			DBUS_TYPE_STRING, &option[0],
			DBUS_TYPE_STRING, &option[1],
			DBUS_TYPE_INVALID);

	image = image_create();
	if (image == NULL) {
		LOG(WARNING, "failed to create image");
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (stat) {
		for (i = 0; i < NUM_IMAGE_PARAMETERS; i++) {
			optname = NULL;
			optval = NULL;
			parse_image_option(option[i], &optname, &optval);
			if (strncmp(optname, "image", strlen("image")) == 0) {
				image_set_filename(image, optval);
			} else if (strncmp(optname, "location", strlen("location")) == 0) {
				parse_location(optval, &x, &y);
				image_set_location(image, x, y);
			} else if (strncmp(optname, "offset", strlen("offset")) == 0) {
				parse_location(optval, &x, &y);
				image_set_offset(image, x, y);
			}
			if (optname)
				free(optname);
			if (optval)
				free(optval);
		}
	} else {
		goto fail;
	}

	status = image_load_image_from_file(image);
	if (status != 0) {
		LOG(WARNING, "image_load_image_from_file failed: %d", status);
		goto fail;
	}

	terminal = term_get_current_terminal();
	if (!terminal)
		goto fail;

	status = term_show_image(terminal, image);
	if (status != 0) {
		LOG(WARNING, "term_show_image failed: %d", status);
		goto fail;
	}
	image_release(image);

	reply = dbus_message_new_method_return(message);
	dbus_connection_send(connection, reply, NULL);

	return DBUS_HANDLER_RESULT_HANDLED;
fail:
	if (image)
		image_release(image);
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void frecon_dbus_unregister(DBusConnection* connection, void* user_data)
{
}

static DBusHandlerResult frecon_dbus_message_handler(DBusConnection* connection,
						     DBusMessage* message,
						     void* user_data)
{
	if (dbus_message_is_method_call(message,
				kFreconDbusInterface, COMMAND_SWITCH_VT)) {
		return handle_switchvt(connection, message);
	} else if (dbus_message_is_method_call(message,
				kFreconDbusInterface, COMMAND_MAKE_VT)) {
		return handle_makevt(connection, message);
	}
	else if (dbus_message_is_method_call(message,
				kFreconDbusInterface, COMMAND_TERMINATE)) {
		return handle_terminate(connection, message);
	} else if (dbus_message_is_method_call(message,
				kFreconDbusInterface, COMMAND_IMAGE)) {
		return handle_image(connection, message);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable
frecon_vtable = {
	frecon_dbus_unregister,
	frecon_dbus_message_handler,
	NULL
};

static dbus_bool_t add_watch(DBusWatch* w, void* data)
{
	dbus_t* dbus = (dbus_t*)data;
	dbus->watch = w;

	return TRUE;
}

static void remove_watch(DBusWatch* w, void* data)
{
}

static void toggle_watch(DBusWatch* w, void* data)
{
}

static DBusHandlerResult handle_login_prompt_visible(DBusMessage* message)
{
	if (login_prompt_visible_callback) {
		login_prompt_visible_callback(login_prompt_visible_callback_userptr);
		login_prompt_visible_callback = NULL;
		login_prompt_visible_callback_userptr = NULL;
	}
	chrome_is_already_up = true;

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult frecon_dbus_message_filter(DBusConnection* connection,
						    DBusMessage* message,
						    void* user_data)
{
	if (dbus_message_is_signal(message,
				kSessionManagerInterface, kLoginPromptVisibleSignal))
		return handle_login_prompt_visible(message);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool dbus_is_initialized(void)
{
	return !!dbus;
}

bool dbus_init()
{
	dbus_t* new_dbus;
	DBusError err;
	int result;
	dbus_bool_t stat;

	if (dbus_first_init) {
		dbus_first_init = false;
		dbus_first_init_time = get_monotonic_time_ms();
	}
	dbus_error_init(&err);

	new_dbus = (dbus_t*)calloc(1, sizeof(*new_dbus));

	if (!new_dbus)
		return false;

	new_dbus->fd = -1;

	new_dbus->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
	if (dbus_error_is_set(&err)) {
		if (!dbus_connect_fail) {
			LOG(ERROR, "Cannot get DBUS connection");
			dbus_connect_fail = true;
			dbus_connect_fail_time = get_monotonic_time_ms();
		}
		free(new_dbus);
		return false;
	}

	if (dbus_connect_fail) {
		int64_t t = get_monotonic_time_ms() - dbus_connect_fail_time;
		LOG(INFO, "DBUS connected after %.1f seconds", (float)t / 1000.0f);
	}

	result = dbus_bus_request_name(new_dbus->conn, kFreconDbusInterface,
			DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);

	if (result <= 0) {
		LOG(ERROR, "Unable to get name for server");
	}

	stat = dbus_connection_register_object_path(new_dbus->conn,
			kFreconDbusPath,
			&frecon_vtable,
			NULL);

	if (!stat) {
		LOG(ERROR, "failed to register object path");
	}

	dbus_bus_add_match(new_dbus->conn, kLoginPromptVisibleRule, &err);

	stat = dbus_connection_add_filter(new_dbus->conn, frecon_dbus_message_filter, NULL, NULL);
	if (!stat) {
		LOG(ERROR, "failed to add message filter");
	}

	stat = dbus_connection_set_watch_functions(new_dbus->conn,
			add_watch, remove_watch, toggle_watch,
			new_dbus, NULL);

	if (!stat) {
		LOG(ERROR, "Failed to set watch functions");
	}

	dbus_connection_set_exit_on_disconnect(new_dbus->conn, FALSE);

	dbus = new_dbus;
	return true;
}

bool dbus_init_wait()
{
	while (!dbus_is_initialized()) {
		if (!dbus_init()) {
			int64_t t = get_monotonic_time_ms() - dbus_first_init_time;
			if (t >= DBUS_INIT_TIMEOUT_MS) {
				LOG(ERROR, "DBUS init failed after a timeout of %u sec", DBUS_INIT_TIMEOUT_MS/1000);
				return false;
			}
		}
		usleep(DBUS_WAIT_DELAY_US);
	}
	return true;
}

static bool dbus_method_call0(const char* service_name,
			      const char* service_path,
			      const char* service_interface,
			      const char* method)
{
	DBusMessage* msg = NULL;
	if (!dbus) {
		LOG(ERROR, "dbus not initialized");
		return false;
	}

	msg = dbus_message_new_method_call(service_name,
			service_path, service_interface, method);

	if (!msg)
		return false;

	if (!dbus_connection_send_with_reply_and_block(dbus->conn,
				msg, DBUS_DEFAULT_DELAY, NULL)) {
		dbus_message_unref(msg);
		return false;
	}

	dbus_connection_flush(dbus->conn);
	dbus_message_unref(msg);

	return true;
}

static bool dbus_method_call1(const char* service_name,
			      const char* service_path,
			      const char* service_interface,
			      const char* method, int arg_type, void* param)
{
	DBusMessage* msg = NULL;
	if (!dbus) {
		LOG(ERROR, "dbus not initialized");
		return false;
	}

	msg = dbus_message_new_method_call(service_name,
			service_path, service_interface, method);

	if (!msg)
		return false;

	if (!dbus_message_append_args(msg,
				arg_type, param, DBUS_TYPE_INVALID)) {
		dbus_message_unref(msg);
		return false;
	}

	if (!dbus_connection_send_with_reply_and_block(dbus->conn,
				msg, DBUS_DEFAULT_DELAY, NULL)) {
		dbus_message_unref(msg);
		return false;
	}

	dbus_connection_flush(dbus->conn);
	dbus_message_unref(msg);

	return true;
}

void dbus_destroy(void)
{
	/* FIXME - not sure what the right counterpart to
	 * dbus_bus_get() is, unref documentation is rather
	 * unclear. Not a big issue but it would be nice to
	 * clean up properly here
	 */
	/* dbus_connection_unref(dbus->conn); */
	if (dbus) {
		free(dbus);
		dbus = NULL;
	}
}

void dbus_add_fds(fd_set* read_set, fd_set* exception_set, int *maxfd)
{
	if (!dbus)
		return;

	if (dbus->fd < 0)
		dbus->fd = dbus_watch_get_unix_fd(dbus->watch);

	if (dbus->fd >= 0) {
		FD_SET(dbus->fd, read_set);
		FD_SET(dbus->fd, exception_set);
	}

	if (dbus->fd > *maxfd)
		*maxfd = dbus->fd;
}

void dbus_dispatch_io(void)
{
	if (!dbus)
		return;

	dbus_watch_handle(dbus->watch, DBUS_WATCH_READABLE);
	while (dbus_connection_get_dispatch_status(dbus->conn)
			== DBUS_DISPATCH_DATA_REMAINS) {
		dbus_connection_dispatch(dbus->conn);
	}
}

void dbus_report_user_activity(int activity_type)
{
	dbus_bool_t allow_off = false;
	if (!dbus)
		return;

	dbus_method_call1(kPowerManagerServiceName,
			kPowerManagerServicePath,
			kPowerManagerInterface,
			kHandleUserActivityMethod,
			DBUS_TYPE_INT32, &activity_type);

	switch (activity_type) {
		case USER_ACTIVITY_BRIGHTNESS_UP_KEY_PRESS:
				(void)dbus_method_call0(kPowerManagerServiceName,
					kPowerManagerServicePath,
					kPowerManagerInterface,
					kIncreaseScreenBrightnessMethod);
				break;
		case USER_ACTIVITY_BRIGHTNESS_DOWN_KEY_PRESS:
				/*
				 * Shouldn't allow the screen to go
				 * completely off while frecon is active
				 * so passing false to allow_off
				 */
				(void)dbus_method_call1(kPowerManagerServiceName,
					kPowerManagerServicePath,
					kPowerManagerInterface,
					kDecreaseScreenBrightnessMethod,
					DBUS_TYPE_BOOLEAN, &allow_off);
				break;
	}
}

/*
 * tell Chrome to take ownership of the display (DRM master)
 */
void dbus_take_display_ownership(void)
{
	if (!dbus)
		return;
	(void)dbus_method_call0(kLibCrosServiceName,
				kLibCrosServicePath,
				kLibCrosServiceInterface,
				kTakeDisplayOwnership);
}

/*
 * ask Chrome to give up display ownership (DRM master)
 */
void dbus_release_display_ownership(void)
{
	if (!dbus)
		return;
	(void)dbus_method_call0(kLibCrosServiceName,
				kLibCrosServicePath,
				kLibCrosServiceInterface,
				kReleaseDisplayOwnership);
}

void dbus_set_login_prompt_visible_callback(void (*callback)(void*),
					    void* userptr)
{
	if (chrome_is_already_up) {
		if (callback)
			callback(userptr);
	} else {
		if (login_prompt_visible_callback && callback) {
			LOG(ERROR, "trying to register login prompt visible callback multiple times");
			return;
		}
		login_prompt_visible_callback = callback;
		login_prompt_visible_callback_userptr = userptr;
	}
}
