/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug_fmt.h Functions related to debugging. */

#ifndef DEBUG_FMT_H
#define DEBUG_FMT_H

#include "debug.h"
#include "3rdparty/fmt/format.h"

/**
 * Ouptut a line of debugging information.
 * @param name The category of debug information.
 * @param level The maximum debug level this message should be shown at. When the debug level for this category is set lower, then the message will not be shown.
 * @param format_string The formatting string of the message.
 */
#define Debug(name, level, format_string, ...) if ((level) == 0 || _debug_ ## name ## _level >= (level)) debug_print(#name, fmt::format(FMT_STRING(format_string), ## __VA_ARGS__).c_str())

#endif /* DEBUG_FMT_H */
