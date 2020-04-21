/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.cpp Handling of printing debug messages. */

#include "stdafx.h"
#include <stdarg.h>
#include "console_func.h"
#include "debug.h"
#include "string_func.h"
#include "fileio_func.h"
#include "settings_type.h"
#include "date_func.h"
#include <array>

#if defined(_WIN32)
#include "os/windows/win32.h"
#endif

#include <time.h>

#include "network/network_admin.h"
SOCKET _debug_socket = INVALID_SOCKET;

#if defined(RANDOM_DEBUG) && defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

#include "safeguards.h"

int _debug_driver_level;
int _debug_grf_level;
int _debug_map_level;
int _debug_misc_level;
int _debug_net_level;
int _debug_sprite_level;
int _debug_oldloader_level;
int _debug_npf_level;
int _debug_yapf_level;
int _debug_freetype_level;
int _debug_script_level;
int _debug_sl_level;
int _debug_gamelog_level;
int _debug_desync_level;
int _debug_yapfdesync_level;
int _debug_console_level;
int _debug_linkgraph_level;
int _debug_sound_level;
#ifdef RANDOM_DEBUG
int _debug_random_level;
#endif

const char *_savegame_DBGL_data = nullptr;
std::string _loadgame_DBGL_data;
bool _save_DBGC_data = false;
std::string _loadgame_DBGC_data;

uint32 _realtime_tick = 0;

struct DebugLevel {
	const char *name;
	int *level;
};

#define DEBUG_LEVEL(x) { #x, &_debug_##x##_level }
	static const DebugLevel debug_level[] = {
	DEBUG_LEVEL(driver),
	DEBUG_LEVEL(grf),
	DEBUG_LEVEL(map),
	DEBUG_LEVEL(misc),
	DEBUG_LEVEL(net),
	DEBUG_LEVEL(sprite),
	DEBUG_LEVEL(oldloader),
	DEBUG_LEVEL(npf),
	DEBUG_LEVEL(yapf),
	DEBUG_LEVEL(freetype),
	DEBUG_LEVEL(script),
	DEBUG_LEVEL(sl),
	DEBUG_LEVEL(gamelog),
	DEBUG_LEVEL(desync),
	DEBUG_LEVEL(yapfdesync),
	DEBUG_LEVEL(console),
	DEBUG_LEVEL(linkgraph),
	DEBUG_LEVEL(sound),
#ifdef RANDOM_DEBUG
	DEBUG_LEVEL(random),
#endif
	};
#undef DEBUG_LEVEL

/**
 * Dump the available debug facility names in the help text.
 * @param buf Start address for storing the output.
 * @param last Last valid address for storing the output.
 * @return Next free position in the output.
 */
char *DumpDebugFacilityNames(char *buf, char *last)
{
	size_t length = 0;
	for (const DebugLevel *i = debug_level; i != endof(debug_level); ++i) {
		if (length == 0) {
			buf = strecpy(buf, "List of debug facility names:\n", last);
		} else {
			buf = strecpy(buf, ", ", last);
			length += 2;
		}
		buf = strecpy(buf, i->name, last);
		length += strlen(i->name);
	}
	if (length > 0) {
		buf = strecpy(buf, "\n\n", last);
	}
	return buf;
}

/**
 * Internal function for outputting the debug line.
 * @param dbg Debug category.
 * @param buf Text line to output.
 */
static void debug_print(const char *dbg, const char *buf)
{
	if (_debug_socket != INVALID_SOCKET) {
		char buf2[1024 + 32];

		seprintf(buf2, lastof(buf2), "%sdbg: [%s] %s\n", GetLogPrefix(), dbg, buf);
		/* Sending out an error when this fails would be nice, however... the error
		 * would have to be send over this failing socket which won't work. */
		send(_debug_socket, buf2, (int)strlen(buf2), 0);
		return;
	}
	if (strcmp(dbg, "desync") == 0) {
		static FILE *f = FioFOpenFile("commands-out.log", "wb", AUTOSAVE_DIR);
		if (f != nullptr) {
			fprintf(f, "%s%s\n", GetLogPrefix(), buf);
			fflush(f);
		}
#ifdef RANDOM_DEBUG
	} else if (strcmp(dbg, "random") == 0) {
#if defined(UNIX) && defined(__GLIBC__)
		static bool have_inited = false;
		static FILE *f = nullptr;

		if (!have_inited) {
			have_inited = true;
			unsigned int num = 0;
			int pid = getpid();
			const char *fn = nullptr;
			for(;;) {
				free(fn);
				fn = str_fmt("random-out-%d-%u.log", pid, num);
				f = FioFOpenFile(fn, "wx", AUTOSAVE_DIR);
				if (f == nullptr && errno == EEXIST) {
					num++;
					continue;
				}
				break;
			}
			free(fn);
		}
#else
		static FILE *f = FioFOpenFile("random-out.log", "wb", AUTOSAVE_DIR);
#endif
		if (f != nullptr) {
			fprintf(f, "%s\n", buf);
			return;
		}
#endif
	}

	char buffer[512];
	seprintf(buffer, lastof(buffer), "%sdbg: [%s] %s\n", GetLogPrefix(), dbg, buf);

	str_strip_colours(buffer);

	/* do not write desync messages to the console on Windows platforms, as they do
	 * not seem able to handle text direction change characters in a console without
	 * crashing, and NetworkTextMessage includes these */
#if defined(_WIN32)
	if (strcmp(dbg, "desync") != 0) {
		TCHAR system_buf[512];
		convert_to_fs(buffer, system_buf, lengthof(system_buf), true);
		_fputts(system_buf, stderr);
	}
#else
	fputs(buffer, stderr);
#endif

	NetworkAdminConsole(dbg, buf);
	IConsoleDebug(dbg, buf);
}

/**
 * Output a debug line.
 * @note Do not call directly, use the #DEBUG macro instead.
 * @param dbg Debug category.
 * @param format Text string a la printf, with optional arguments.
 */
void CDECL debug(const char *dbg, const char *format, ...)
{
	char buf[1024];

	va_list va;
	va_start(va, format);
	vseprintf(buf, lastof(buf), format, va);
	va_end(va);

	debug_print(dbg, buf);
}

/**
 * Set debugging levels by parsing the text in \a s.
 * For setting individual levels a string like \c "net=3,grf=6" should be used.
 * If the string starts with a number, the number is used as global debugging level.
 * @param s Text describing the wanted debugging levels.
 */
void SetDebugString(const char *s)
{
	int v;
	char *end;
	const char *t;

	/* global debugging level? */
	if (*s >= '0' && *s <= '9') {
		const DebugLevel *i;

		v = strtoul(s, &end, 0);
		s = end;

		for (i = debug_level; i != endof(debug_level); ++i) *i->level = v;
	}

	/* individual levels */
	for (;;) {
		const DebugLevel *i;
		int *p;

		/* skip delimiters */
		while (*s == ' ' || *s == ',' || *s == '\t') s++;
		if (*s == '\0') break;

		t = s;
		while (*s >= 'a' && *s <= 'z') s++;

		/* check debugging levels */
		p = nullptr;
		for (i = debug_level; i != endof(debug_level); ++i) {
			if (s == t + strlen(i->name) && strncmp(t, i->name, s - t) == 0) {
				p = i->level;
				break;
			}
		}

		if (*s == '=') s++;
		v = strtoul(s, &end, 0);
		s = end;
		if (p != nullptr) {
			*p = v;
		} else {
			ShowInfoF("Unknown debug level '%.*s'", (int)(s - t), t);
			return;
		}
	}
}

/**
 * Print out the current debug-level.
 * Just return a string with the values of all the debug categories.
 * @return string with debug-levels
 */
const char *GetDebugString()
{
	const DebugLevel *i;
	static char dbgstr[150];
	char dbgval[20];

	memset(dbgstr, 0, sizeof(dbgstr));
	i = debug_level;
	seprintf(dbgstr, lastof(dbgstr), "%s=%d", i->name, *i->level);

	for (i++; i != endof(debug_level); i++) {
		seprintf(dbgval, lastof(dbgval), ", %s=%d", i->name, *i->level);
		strecat(dbgstr, dbgval, lastof(dbgstr));
	}

	return dbgstr;
}

/**
 * Get the prefix for logs; if show_date_in_logs is enabled it returns
 * the date, otherwise it returns nothing.
 * @return the prefix for logs (do not free), never nullptr
 */
const char *GetLogPrefix()
{
	static char _log_prefix[24];
	if (_settings_client.gui.show_date_in_logs) {
		time_t cur_time = time(nullptr);
		strftime(_log_prefix, sizeof(_log_prefix), "[%Y-%m-%d %H:%M:%S] ", localtime(&cur_time));
	} else {
		*_log_prefix = '\0';
	}
	return _log_prefix;
}

struct DesyncMsgLogEntry {
	Date date;
	DateFract date_fract;
	uint8 tick_skip_counter;
	std::string msg;

	DesyncMsgLogEntry() { }

	DesyncMsgLogEntry(std::string msg)
			: date(_date), date_fract(_date_fract), tick_skip_counter(_tick_skip_counter), msg(msg) { }
};

static std::array<DesyncMsgLogEntry, 64> desync_msg_log;
static unsigned int desync_msg_log_count = 0;
static unsigned int desync_msg_log_next = 0;

void ClearDesyncMsgLog()
{
	desync_msg_log_count = 0;
	desync_msg_log_next = 0;
}

char *DumpDesyncMsgLog(char *buffer, const char *last)
{
	if (!desync_msg_log_count) return buffer;

	const unsigned int count = min<unsigned int>(desync_msg_log_count, desync_msg_log.size());
	unsigned int log_index = (desync_msg_log_next + desync_msg_log.size() - count) % desync_msg_log.size();
	unsigned int display_num = desync_msg_log_count - count;

	buffer += seprintf(buffer, last, "Desync Msg Log:\n Showing most recent %u of %u messages\n", count, desync_msg_log_count);

	for (unsigned int i = 0 ; i < count; i++) {
		const DesyncMsgLogEntry &entry = desync_msg_log[log_index];

		YearMonthDay ymd;
		ConvertDateToYMD(entry.date, &ymd);
		buffer += seprintf(buffer, last, "%5u | %4i-%02i-%02i, %2i, %3i | %s\n", display_num, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, entry.msg.c_str());
		log_index = (log_index + 1) % desync_msg_log.size();
		display_num++;
	}
	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

void LogDesyncMsg(std::string msg)
{
	desync_msg_log[desync_msg_log_next] = DesyncMsgLogEntry(std::move(msg));
	desync_msg_log_next = (desync_msg_log_next + 1) % desync_msg_log.size();
	desync_msg_log_count++;
}
