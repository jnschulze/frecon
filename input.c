/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fcntl.h>
#include <libtsm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <libudev.h>
#include <dbus/dbus.h>
#include "input.h"
#include "mini_power_manager.h"

struct input_dev {
	int fd;
	char *path;
};

struct {
	struct udev *udev;
	struct udev_monitor *udev_monitor;
	int udev_fd;
	unsigned int ndevs;
	struct input_dev *devs;
	DBusConnection *dbus_conn;
} input = {
	.udev = NULL,
	.udev_monitor = NULL,
	.udev_fd = -1,
	.ndevs = 0,
	.devs = NULL,
	.dbus_conn = NULL
};

static int input_add(const char *devname)
{
	int ret = 0, fd = -1;
	/* for some reason every device has a null enumerations and notifications
	   of every device come with NULL string first */
	if (!devname) {
		ret = -EINVAL;
		goto errorret;
	}
	ret = fd = open(devname, O_RDONLY);
	if (fd < 0)
		goto errorret;

	ret = ioctl(fd, EVIOCGRAB, (void *) 1);

	if (!ret) {
		ioctl(fd, EVIOCGRAB, (void *) 0);
	} else {
		fprintf(stderr, "Evdev device %s grabbed by another process\n",
			devname);
		ret = -EBUSY;
		goto closefd;
	}

	struct input_dev *newdevs =
	    realloc(input.devs, (input.ndevs + 1) * sizeof (struct input_dev));
	if (!newdevs) {
		ret = -ENOMEM;
		goto closefd;
	}
	input.devs = newdevs;
	input.devs[input.ndevs].fd = fd;
	input.devs[input.ndevs].path = strdup(devname);
	if (!input.devs[input.ndevs].path) {
		ret = -ENOMEM;
		goto closefd;
	}
	input.ndevs++;

	return fd;

closefd:
	close(fd);
errorret:
	return ret;
}

static void input_remove(const char *devname)
{
	if (!devname) {
		return;
	}
	unsigned int u;
	for (u = 0; u < input.ndevs; u++) {
		if (!strcmp(devname, input.devs[u].path)) {
			free(input.devs[u].path);
			close(input.devs[u].fd);
			input.ndevs--;
			if (u != input.ndevs) {
				input.devs[u] = input.devs[input.ndevs];
			}
			return;
		}
	}
}

static bool check_dbus_error(DBusError * err, const char *msg)
{
	if (dbus_error_is_set(err)) {
		fprintf(stderr, "%s name:%s message:%s\n", msg, err->name,
			err->message);
		return true;
	}
	return false;
}

int input_init()
{
	input.udev = udev_new();
	if (!input.udev)
		return -ENOENT;
	input.udev_monitor = udev_monitor_new_from_netlink(input.udev, "udev");
	if (!input.udev_monitor) {
		udev_unref(input.udev);
		return -ENOENT;
	}
	udev_monitor_filter_add_match_subsystem_devtype(input.udev_monitor, "input",
							NULL);
	udev_monitor_enable_receiving(input.udev_monitor);
	input.udev_fd = udev_monitor_get_fd(input.udev_monitor);

	struct udev_enumerate *udev_enum;
	struct udev_list_entry *devices, *deventry;
	udev_enum = udev_enumerate_new(input.udev);
	udev_enumerate_add_match_subsystem(udev_enum, "input");
	udev_enumerate_scan_devices(udev_enum);
	devices = udev_enumerate_get_list_entry(udev_enum);
	udev_list_entry_foreach(deventry, devices) {
		const char *syspath;
		struct udev_device *dev;
		syspath = udev_list_entry_get_name(deventry);
		dev = udev_device_new_from_syspath(input.udev, syspath);
		input_add(udev_device_get_devnode(dev));
		udev_device_unref(dev);
	}
	udev_enumerate_unref(udev_enum);

	if (!isatty(fileno(stdout)))
		setbuf(stdout, NULL);

	return 0;
}

void input_close()
{
	unsigned int u;
	for (u = 0; u < input.ndevs; u++) {
		free(input.devs[u].path);
		close(input.devs[u].fd);
	}
	free(input.devs);
	input.devs = NULL;
	input.ndevs = 0;

	udev_monitor_unref(input.udev_monitor);
	input.udev_monitor = NULL;
	udev_unref(input.udev);
	input.udev = NULL;
	input.udev_fd = -1;

	if (input.dbus_conn) {
		/* FIXME - not sure what the right counterpart to
		  dbus_bus_get() is, unref documentation is rather
		  unclear. Not a big issue but it would be nice to
		  clean up properly here */
		/* dbus_connection_unref(input.dbus_conn); */
		input.dbus_conn = NULL;
	}
}

int input_setfds(fd_set * read_set, fd_set * exception_set)
{
	unsigned int u;
	int max = -1;
	for (u = 0; u < input.ndevs; u++) {
		FD_SET(input.devs[u].fd, read_set);
		FD_SET(input.devs[u].fd, exception_set);
		if (input.devs[u].fd > max)
			max = input.devs[u].fd;
	}

	FD_SET(input.udev_fd, read_set);
	FD_SET(input.udev_fd, exception_set);
	if (input.udev_fd > max)
		max = input.udev_fd;
	return max;
}

static void report_user_activity(void)
{
	DBusError err;
	dbus_error_init(&err);

	if (!input.dbus_conn) {
		input.dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
		if (check_dbus_error(&err, "Cannot get dbus connection"))
			return;
		dbus_connection_set_exit_on_disconnect(input.dbus_conn, FALSE);
	}

	if (!dbus_bus_name_has_owner(input.dbus_conn, kPowerManagerServiceName, &err)) {
		fprintf(stderr, "Power_manager not available on dbus!\n");
		return;
	}

	DBusMessage *msg = NULL;
	unsigned int activity_type = USER_ACTIVITY_OTHER;

	msg = dbus_message_new_method_call(kPowerManagerServiceName,
					   kPowerManagerServicePath,
					   kPowerManagerInterface,
					   kHandleUserActivityMethod);
	if (!msg)
		return;
	dbus_message_set_no_reply(msg, TRUE);
	if (!dbus_message_append_args(msg,
				      DBUS_TYPE_UINT32, &activity_type,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(msg);
		return;
	}
	if (!dbus_connection_send(input.dbus_conn, msg, NULL)) {
	}
	dbus_connection_flush(input.dbus_conn);
	dbus_message_unref(msg);
}

struct input_key_event *input_get_event(fd_set * read_set,
					fd_set * exception_set)
{
	unsigned int u;
	struct input_event ev;
	int ret;

	if (FD_ISSET(input.udev_fd, exception_set)) {
		/* udev died on us? */
		fprintf(stderr, "Exception on udev fd\n");
	}

	if (FD_ISSET(input.udev_fd, read_set)
	    && !FD_ISSET(input.udev_fd, exception_set)) {
		/* we got an udev notification */
		struct udev_device *dev =
		    udev_monitor_receive_device(input.udev_monitor);
		if (dev) {
			if (!strcmp("add", udev_device_get_action(dev))) {
				input_add(udev_device_get_devnode(dev));
			} else
			    if (!strcmp("remove", udev_device_get_action(dev)))
			{
				input_remove(udev_device_get_devnode(dev));
			}
			udev_device_unref(dev);
		}
	}

	for (u = 0; u < input.ndevs; u++) {
		if (FD_ISSET(input.devs[u].fd, read_set)
		    && !FD_ISSET(input.devs[u].fd, exception_set)) {
			ret =
			    read(input.devs[u].fd, &ev, sizeof (struct input_event));
			if (ret < (int) sizeof (struct input_event)) {
				printf("expected %d bytes, got %d\n",
				       (int) sizeof (struct input_event), ret);
				return NULL;
			}

			if (ev.type == EV_KEY) {
				struct input_key_event *event =
				    malloc(sizeof (*event));
				event->code = ev.code;
				event->value = ev.value;
				report_user_activity();
				return event;
			}
		}
	}

	return NULL;
}

void input_put_event(struct input_key_event *event)
{
	free(event);
}
