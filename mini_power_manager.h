/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MINI_POWER_MANAGER_H
#define MINI_POWER_MANAGER_H
/* Minimal set of power manager constants copied from
   platform/system_api/dbus/service_constants.h which are C++
   header file so we can't use it in our code directly */

const char kPowerManagerInterface[] = "org.chromium.PowerManager";
const char kPowerManagerServicePath[] = "/org/chromium/PowerManager";
const char kPowerManagerServiceName[] = "org.chromium.PowerManager";
/* Methods exposed by powerd. */
const char kDecreaseScreenBrightnessMethod[] = "DecreaseScreenBrightness";
const char kIncreaseScreenBrightnessMethod[] = "IncreaseScreenBrightness";
const char kHandleUserActivityMethod[] = "HandleUserActivity";
/* Values */
const int kBrightnessTransitionGradual = 1;
const int kBrightnessTransitionInstant = 2;
enum UserActivityType {
  USER_ACTIVITY_OTHER = 0,
  USER_ACTIVITY_BRIGHTNESS_UP_KEY_PRESS = 1,
  USER_ACTIVITY_BRIGHTNESS_DOWN_KEY_PRESS = 2,
  USER_ACTIVITY_VOLUME_UP_KEY_PRESS = 3,
  USER_ACTIVITY_VOLUME_DOWN_KEY_PRESS = 4,
  USER_ACTIVITY_VOLUME_MUTE_KEY_PRESS = 5,
};

#endif /* MINI_POWER_MANAGER_H */

