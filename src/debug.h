/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.h Functions related to debugging. */

#ifndef DEBUG_H
#define DEBUG_H

#include <string>

/* Debugging messages policy:
 * These should be the severities used for direct DEBUG() calls
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

/**
 * Output a line of debugging information.
 * @param name Category
 * @param level Debugging level, higher levels means more detailed information.
 */
#define DEBUG(name, level, ...) do { if ((level) == 0 || _debug_ ## name ## _level >= (level)) debug(#name, level, __VA_ARGS__); } while (false)

extern int _debug_driver_level;
extern int _debug_grf_level;
extern int _debug_map_level;
extern int _debug_misc_level;
extern int _debug_net_level;
extern int _debug_sprite_level;
extern int _debug_oldloader_level;
extern int _debug_yapf_level;
extern int _debug_fontcache_level;
extern int _debug_script_level;
extern int _debug_sl_level;
extern int _debug_gamelog_level;
extern int _debug_desync_level;
extern int _debug_yapfdesync_level;
extern int _debug_console_level;
extern int _debug_linkgraph_level;
extern int _debug_sound_level;
extern int _debug_command_level;
#ifdef RANDOM_DEBUG
extern int _debug_random_level;
extern int _debug_statecsum_level;
#endif

extern const char *_savegame_DBGL_data;
extern std::string _loadgame_DBGL_data;
extern bool _save_DBGC_data;
extern std::string _loadgame_DBGC_data;

void CDECL debug(const char *dbg, int level, const char *format, ...) WARN_FORMAT(3, 4);
void debug_print(const char *dbg, int level, const char *buf);

char *DumpDebugFacilityNames(char *buf, char *last);
void SetDebugString(const char *s, void (*error_func)(const char *));
std::string GetDebugString();

/* Shorter form for passing filename and linenumber */
#define FILE_LINE __FILE__, __LINE__

void ShowInfoI(const char *str);
void CDECL ShowInfoF(const char *str, ...) WARN_FORMAT(1, 2);

inline void ShowInfoI(const std::string &str)
{
	ShowInfoI(str.c_str());
}

struct log_prefix {
	const char *GetLogPrefix(bool force = false);

private:
	char buffer[24];
};

void ClearDesyncMsgLog();
void LogDesyncMsg(std::string msg);
char *DumpDesyncMsgLog(char *buffer, const char *last);

void DebugSendRemoteMessages();
void DebugReconsiderSendRemoteMessages();

#endif /* DEBUG_H */
