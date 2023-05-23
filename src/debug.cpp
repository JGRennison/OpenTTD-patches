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
#include "thread.h"
#include <array>
#include <mutex>
#if defined(__MINGW32__)
#include "3rdparty/mingw-std-threads/mingw.mutex.h"
#endif

#if defined(_WIN32)
#include "os/windows/win32.h"
#endif

#include "walltime_func.h"

#include "network/network_admin.h"
SOCKET _debug_socket = INVALID_SOCKET;

#if defined(RANDOM_DEBUG) && defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

#include "safeguards.h"

/** Element in the queue of debug messages that have to be passed to either NetworkAdminConsole or IConsolePrint.*/
struct QueuedDebugItem {
	std::string level;   ///< The used debug level.
	std::string message; ///< The actual formatted message.
};
std::atomic<bool> _debug_remote_console; ///< Whether we need to send data to either NetworkAdminConsole or IConsolePrint.
std::mutex _debug_remote_console_mutex; ///< Mutex to guard the queue of debug messages for either NetworkAdminConsole or IConsolePrint.
std::vector<QueuedDebugItem> _debug_remote_console_queue; ///< Queue for debug messages to be passed to NetworkAdminConsole or IConsolePrint.
std::vector<QueuedDebugItem> _debug_remote_console_queue_spare; ///< Spare queue to swap with _debug_remote_console_queue.

int _debug_driver_level;
int _debug_grf_level;
int _debug_map_level;
int _debug_misc_level;
int _debug_net_level;
int _debug_sprite_level;
int _debug_oldloader_level;
int _debug_npf_level;
int _debug_yapf_level;
int _debug_fontcache_level;
int _debug_script_level;
int _debug_sl_level;
int _debug_gamelog_level;
int _debug_desync_level;
int _debug_yapfdesync_level;
int _debug_console_level;
int _debug_linkgraph_level;
int _debug_sound_level;
int _debug_command_level;
#ifdef RANDOM_DEBUG
int _debug_random_level;
int _debug_statecsum_level;
#endif

const char *_savegame_DBGL_data = nullptr;
std::string _loadgame_DBGL_data;
bool _save_DBGC_data = false;
std::string _loadgame_DBGC_data;

uint32 _misc_debug_flags;

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
	DEBUG_LEVEL(fontcache),
	DEBUG_LEVEL(script),
	DEBUG_LEVEL(sl),
	DEBUG_LEVEL(gamelog),
	DEBUG_LEVEL(desync),
	DEBUG_LEVEL(yapfdesync),
	DEBUG_LEVEL(console),
	DEBUG_LEVEL(linkgraph),
	DEBUG_LEVEL(sound),
	DEBUG_LEVEL(command),
#ifdef RANDOM_DEBUG
	DEBUG_LEVEL(random),
	DEBUG_LEVEL(statecsum),
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
void debug_print(const char *dbg, const char *buf)
{
	if (_debug_socket != INVALID_SOCKET) {
		char buf2[1024 + 32];

		seprintf(buf2, lastof(buf2), "%sdbg: [%s] %s\n", GetLogPrefix(), dbg, buf);

		/* Prevent sending a message concurrently, as that might cause interleaved messages. */
		static std::mutex _debug_socket_mutex;
		std::lock_guard<std::mutex> lock(_debug_socket_mutex);

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
	} else if (strcmp(dbg, "random") == 0 || strcmp(dbg, "statecsum") == 0) {
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
		fputs(buffer, stderr);
	}
#else
	fputs(buffer, stderr);
#endif

	if (_debug_remote_console.load()) {
		/* Only add to the queue when there is at least one consumer of the data. */
		if (IsNonGameThread()) {
			std::lock_guard<std::mutex> lock(_debug_remote_console_mutex);
			_debug_remote_console_queue.push_back({ dbg, buf });
		} else {
			NetworkAdminConsole(dbg, buf);
			if (_settings_client.gui.developer >= 2) IConsolePrintF(CC_DEBUG, "dbg: [%s] %s", dbg, buf);
		}
	}
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
 * @param error_func The function to call if a parse error occurs.
 */
void SetDebugString(const char *s, void (*error_func)(const char *))
{
	int v;
	char *end;
	const char *t;

	/* Store planned changes into map during parse */
	std::map<const char *, int> new_levels;

	/* Global debugging level? */
	if (*s >= '0' && *s <= '9') {
		const DebugLevel *i;

		v = strtoul(s, &end, 0);
		s = end;

		for (i = debug_level; i != endof(debug_level); ++i) {
			new_levels[i->name] = v;
		}
	}

	/* Individual levels */
	for (;;) {
		/* skip delimiters */
		while (*s == ' ' || *s == ',' || *s == '\t') s++;
		if (*s == '\0') break;

		t = s;
		while (*s >= 'a' && *s <= 'z') s++;

		/* check debugging levels */
		const DebugLevel *found = nullptr;
		for (const DebugLevel *i = debug_level; i != endof(debug_level); ++i) {
			if (s == t + strlen(i->name) && strncmp(t, i->name, s - t) == 0) {
				found = i;
				break;
			}
		}

		if (*s == '=') s++;
		v = strtoul(s, &end, 0);
		s = end;
		if (found != nullptr) {
			new_levels[found->name] = v;
		} else {
			char buf[1024];
			seprintf(buf, lastof(buf), "Unknown debug level '%*s'", (int)(s - t), t);
			error_func(buf);
			return;
		}
	}

	/* Apply the changes after parse is successful */
	for (const DebugLevel *i = debug_level; i != endof(debug_level); ++i) {
		const auto &nl = new_levels.find(i->name);
		if (nl != new_levels.end()) {
			*i->level = nl->second;
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
		LocalTime::Format(_log_prefix, lastof(_log_prefix), "[%Y-%m-%d %H:%M:%S] ");
	} else {
		*_log_prefix = '\0';
	}
	return _log_prefix;
}

struct DesyncMsgLogEntry {
	Date date;
	DateFract date_fract;
	uint8 tick_skip_counter;
	uint32 src_id;
	std::string msg;

	DesyncMsgLogEntry() { }

	DesyncMsgLogEntry(std::string msg)
			: date(_date), date_fract(_date_fract), tick_skip_counter(_tick_skip_counter), src_id(0), msg(msg) { }
};

struct DesyncMsgLog {
	std::array<DesyncMsgLogEntry, 256> log;
	unsigned int count = 0;
	unsigned int next = 0;

	void Clear()
	{
		this->count = 0;
		this->next = 0;
	}

	void LogMsg(DesyncMsgLogEntry entry)
	{
		this->log[this->next] = std::move(entry);
		this->next = (this->next + 1) % this->log.size();
		this->count++;
	}

	template <typename F>
	char *Dump(char *buffer, const char *last, const char *prefix, F handler)
	{
		if (!this->count) return buffer;

		const unsigned int count = std::min<unsigned int>(this->count, (uint)this->log.size());
		unsigned int log_index = (this->next + (uint)this->log.size() - count) % (uint)this->log.size();
		unsigned int display_num = this->count - count;

		buffer += seprintf(buffer, last, "%s:\n Showing most recent %u of %u messages\n", prefix, count, this->count);

		for (unsigned int i = 0 ; i < count; i++) {
			const DesyncMsgLogEntry &entry = this->log[log_index];

			buffer += handler(display_num, buffer, last, entry);
			log_index = (log_index + 1) % this->log.size();
			display_num++;
		}
		buffer += seprintf(buffer, last, "\n");
		return buffer;
	}
};

static DesyncMsgLog _desync_msg_log;
static DesyncMsgLog _remote_desync_msg_log;

void ClearDesyncMsgLog()
{
	_desync_msg_log.Clear();
}

char *DumpDesyncMsgLog(char *buffer, const char *last)
{
	buffer = _desync_msg_log.Dump(buffer, last, "Desync Msg Log", [](int display_num, char *buffer, const char *last, const DesyncMsgLogEntry &entry) -> int {
		YearMonthDay ymd;
		ConvertDateToYMD(entry.date, &ymd);
		return seprintf(buffer, last, "%5u | %4i-%02i-%02i, %2i, %3i | %s\n", display_num, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, entry.msg.c_str());
	});
	buffer = _remote_desync_msg_log.Dump(buffer, last, "Remote Client Desync Msg Log", [](int display_num, char *buffer, const char *last, const DesyncMsgLogEntry &entry) -> int {
		YearMonthDay ymd;
		ConvertDateToYMD(entry.date, &ymd);
		return seprintf(buffer, last, "%5u | Client %5u | %4i-%02i-%02i, %2i, %3i | %s\n", display_num, entry.src_id, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, entry.msg.c_str());
	});
	return buffer;
}

void LogDesyncMsg(std::string msg)
{
	if (_networking && !_network_server) {
		NetworkClientSendDesyncMsg(msg.c_str());
	}
	_desync_msg_log.LogMsg(DesyncMsgLogEntry(std::move(msg)));
}

void LogRemoteDesyncMsg(Date date, DateFract date_fract, uint8 tick_skip_counter, uint32 src_id, std::string msg)
{
	DesyncMsgLogEntry entry(std::move(msg));
	entry.date = date;
	entry.date_fract = date_fract;
	entry.tick_skip_counter = tick_skip_counter;
	entry.src_id = src_id;
	_remote_desync_msg_log.LogMsg(std::move(entry));
}

/**
 * Send the queued Debug messages to either NetworkAdminConsole or IConsolePrint from the
 * GameLoop thread to prevent concurrent accesses to both the NetworkAdmin's packet queue
 * as well as IConsolePrint's buffers.
 *
 * This is to be called from the GameLoop thread.
 */
void DebugSendRemoteMessages()
{
	if (!_debug_remote_console.load()) return;

	{
		std::lock_guard<std::mutex> lock(_debug_remote_console_mutex);
		std::swap(_debug_remote_console_queue, _debug_remote_console_queue_spare);
	}

	for (auto &item : _debug_remote_console_queue_spare) {
		NetworkAdminConsole(item.level.c_str(), item.message.c_str());
		if (_settings_client.gui.developer >= 2) IConsolePrintF(CC_DEBUG, "dbg: [%s] %s", item.level.c_str(), item.message.c_str());
	}

	_debug_remote_console_queue_spare.clear();
}

/**
 * Reconsider whether we need to send debug messages to either NetworkAdminConsole
 * or IConsolePrint. The former is when they have enabled console handling whereas
 * the latter depends on the gui.developer setting's value.
 *
 * This is to be called from the GameLoop thread.
 */
void DebugReconsiderSendRemoteMessages()
{
	bool enable = _settings_client.gui.developer >= 2;

	if (!enable) {
		for (ServerNetworkAdminSocketHandler *as : ServerNetworkAdminSocketHandler::IterateActive()) {
			if (as->update_frequency[ADMIN_UPDATE_CONSOLE] & ADMIN_FREQUENCY_AUTOMATIC) {
				enable = true;
				break;
			}
		}
	}

	_debug_remote_console.store(enable);
}
