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
#include "input.h"
#include "dbus_interface.h"
#include "dbus.h"
#include "util.h"

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
	dbus_t *dbus;
} input = {
	.udev = NULL,
	.udev_monitor = NULL,
	.udev_fd = -1,
	.ndevs = 0,
	.devs = NULL,
	.dbus = NULL
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
		LOG(ERROR, "Evdev device %s grabbed by another process",
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

	if (input.ndevs == 0) {
		LOG(ERROR, "No valid inputs for terminal");
		exit(EXIT_SUCCESS);
	}

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

	dbus_destroy(input.dbus);

}

void input_set_dbus(dbus_t* dbus)
{
	input.dbus = dbus;
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
	int activity_type = USER_ACTIVITY_OTHER;

	dbus_method_call1(input.dbus, kPowerManagerServiceName,
			kPowerManagerServicePath,
			kPowerManagerInterface,
			kHandleUserActivityMethod,
			&activity_type);

	(void)dbus_message_new_method_call(kPowerManagerServiceName,
					   kPowerManagerServicePath,
					   kPowerManagerInterface,
					   kHandleUserActivityMethod);

}

struct input_key_event *input_get_event(fd_set * read_set,
					fd_set * exception_set)
{
	unsigned int u;
	struct input_event ev;
	int ret;

	if (FD_ISSET(input.udev_fd, exception_set)) {
		/* udev died on us? */
		LOG(ERROR, "Exception on udev fd");
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
				LOG(ERROR, "expected %d bytes, got %d",
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

void input_grab()
{
	unsigned int i;
	for (i = 0; i < input.ndevs; i++) {
		(void)ioctl(input.devs[i].fd, EVIOCGRAB, (void *) 1);
	}
}

void input_ungrab()
{
	unsigned int i;
	for (i = 0; i < input.ndevs; i++) {
		(void)ioctl(input.devs[i].fd, EVIOCGRAB, (void*) 0);
	}
}
