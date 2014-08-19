# Copyright 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include common.mk

PC_DEPS = libdrm libtsm libudev dbus-1
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))

CPPFLAGS += -std=c99 -D_GNU_SOURCE=1
CFLAGS += -Wall -Wsign-compare -Wpointer-arith -Wcast-qual -Wcast-align

CPPFLAGS += $(PC_CFLAGS)
LDLIBS += $(PC_LIBS)

CC_BINARY(frecon): $(C_OBJECTS)

all: CC_BINARY(frecon)

clean: CLEAN(frecon)

install: all
	mkdir -p $(DESTDIR)/sbin
	install -m 755 $(OUT)/frecon $(DESTDIR)/sbin