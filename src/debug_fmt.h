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
#include "core/format.hpp"

#define ShowInfo(format_string, ...) ShowInfoI(fmt::format(FMT_STRING(format_string), ## __VA_ARGS__))

template <typename... T>
void DebugIntl(DebugLevelID dbg, int8_t level, fmt::format_string<T...> msg, T&&... args)
{
	fmt::memory_buffer buf{};

	extern void DebugIntlSetup(fmt::memory_buffer &buf, DebugLevelID id, int8_t level);
	DebugIntlSetup(buf, dbg, level);
	size_t prefix_size = buf.size();

	fmt::format_to(std::back_inserter(buf), msg, std::forward<T>(args)...);

	extern void debug_print_partial_buffer(DebugLevelID dbg, int8_t level, fmt::memory_buffer &buf, size_t prefix_size);
	debug_print_partial_buffer(dbg, level, buf, prefix_size);
}

/**
 * Ouptut a line of debugging information.
 * @param name The category of debug information.
 * @param level The maximum debug level this message should be shown at. When the debug level for this category is set lower, then the message will not be shown.
 * @param format_string The formatting string of the message.
 */
#define Debug(name, level, format_string, ...) do { if ((level) == 0 || GetDebugLevel(DebugLevelID::name) >= (level)) DebugIntl(DebugLevelID::name, level, FMT_STRING(format_string), ## __VA_ARGS__); } while (false)

[[noreturn]] void usererror_str(const char *msg);
[[noreturn]] void fatalerror_str(const char *msg);

#define UserError(format_string, ...) usererror_str(fmt::format(FMT_STRING(format_string), ## __VA_ARGS__).c_str())
#define FatalError(format_string, ...) fatalerror_str(fmt::format(FMT_STRING(format_string), ## __VA_ARGS__).c_str())

#endif /* DEBUG_FMT_H */
