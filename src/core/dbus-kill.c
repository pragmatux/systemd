/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2012 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <dbus/dbus.h>

#include "dbus-kill.h"
#include "dbus-common.h"

DEFINE_BUS_PROPERTY_APPEND_ENUM(bus_kill_append_mode, kill_mode, KillMode);

const BusProperty bus_kill_context_properties[] = {
        { "KillMode",    bus_kill_append_mode,     "s", offsetof(KillContext, kill_mode)    },
        { "KillSignal",  bus_property_append_int,  "i", offsetof(KillContext, kill_signal)  },
        { "SendSIGKILL", bus_property_append_bool, "b", offsetof(KillContext, send_sigkill) },
        { NULL, }
};
