/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.h Functions related to debugging. */

#ifndef DEBUG_H
#define DEBUG_H

#include "core/format.hpp"
#include <array>
#include <string>

/* Debugging messages policy:
 * These should be the severities used for direct Debug() calls
 * maximum debugging level should be 10 if really deep, deep
 * debugging is needed.
 * (there is room for exceptions, but you have to have a good cause):
 * 0   - errors or severe warnings
 * 1   - other non-fatal, non-severe warnings
 * 2   - crude progress indicator of functionality
 * 3   - important debugging messages (function entry)
 * 4   - debugging messages (crude loop status, etc.)
 * 5   - detailed debugging information
 * 6.. - extremely detailed spamming
 */

enum class DebugLevelID : uint8_t {
	driver,
	grf,
	map,
	misc,
	net,
	sprite,
	oldloader,
	yapf,
	fontcache,
	script,
	sl,
	gamelog,
	desync,
	yapfdesync,
	console,
	linkgraph,
	sound,
	command,
#ifdef RANDOM_DEBUG
	random,
	statecsum,
#endif
	END,
};
static constexpr uint DebugLevelCount = static_cast<uint>(DebugLevelID::END);

extern std::array<int8_t, DebugLevelCount> _debug_levels;

inline int8_t GetDebugLevel(DebugLevelID id) {
	return _debug_levels[static_cast<uint>(id)];
}

const char *GetDebugLevelName(DebugLevelID id);

template <typename... T>
void DebugIntl(DebugLevelID dbg, int8_t level, fmt::format_string<T...> msg, T&&... args)
{
	extern void DebugIntlVFmt(DebugLevelID dbg, int8_t level, fmt::string_view msg, fmt::format_args args);
	DebugIntlVFmt(dbg, level, msg, fmt::make_format_args(args...));
}

/**
 * Ouptut a line of debugging information.
 * @param name The category of debug information.
 * @param level The maximum debug level this message should be shown at. When the debug level for this category is set lower, then the message will not be shown.
 * @param format_string The formatting string of the message.
 */
#define Debug(name, level, format_string, ...) do { if ((level) == 0 || GetDebugLevel(DebugLevelID::name) >= (level)) DebugIntl(DebugLevelID::name, level, FMT_STRING(format_string), ## __VA_ARGS__); } while (false)

extern const char *_savegame_DBGL_data;
extern std::string _loadgame_DBGL_data;
extern bool _save_DBGC_data;
extern std::string _loadgame_DBGC_data;

void CDECL debug(DebugLevelID dbg, int8_t level, const char *format, ...) WARN_FORMAT(3, 4);
void debug_print(DebugLevelID dbg, int8_t level, std::string_view msg);

void DumpDebugFacilityNames(struct format_target &output);
void SetDebugString(const char *s, void (*error_func)(std::string));
std::string GetDebugString();

/* Shorter form for passing filename and linenumber */
#define FILE_LINE __FILE__, __LINE__

void ShowInfoI(std::string_view str);

template <typename... T>
void ShowInfo(fmt::format_string<T...> msg, T&&... args)
{
	extern void ShowInfoVFmt(fmt::string_view msg, fmt::format_args args);
	ShowInfoVFmt(msg, fmt::make_format_args(args...));
}

struct log_prefix {
	const char *GetLogPrefix(bool force = false);

private:
	char buffer[24];
};

void ClearDesyncMsgLog();
void LogDesyncMsg(std::string msg);
void DumpDesyncMsgLog(struct format_target &buffer);

void DebugSendRemoteMessages();
void DebugReconsiderSendRemoteMessages();

#endif /* DEBUG_H */
