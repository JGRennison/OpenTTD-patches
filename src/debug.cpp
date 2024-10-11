/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.cpp Handling of printing debug messages. */

#include "stdafx.h"
#include "console_func.h"
#include "core/math_func.hpp"
#include "debug.h"
#include "debug_tictoc.h"
#include "string_func.h"
#include "fileio_func.h"
#include "settings_type.h"
#include "date_func.h"
#include "thread.h"
#include "map_func.h"
#include <array>
#include <mutex>

#if defined(_WIN32)
#include "os/windows/win32.h"
#endif

#include "walltime_func.h"

#include "network/network_admin.h"

#if defined(RANDOM_DEBUG) && defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

#include "safeguards.h"
#undef vsnprintf // Required for debug implementation

/** Element in the queue of debug messages that have to be passed to either NetworkAdminConsole or IConsolePrint.*/
struct QueuedDebugItem {
	DebugLevelID category; ///< The used debug category.
	int8_t level;          ///< The used debug level.
	std::string message;   ///< The actual formatted message.
};
std::atomic<bool> _debug_remote_console; ///< Whether we need to send data to either NetworkAdminConsole or IConsolePrint.
std::mutex _debug_remote_console_mutex; ///< Mutex to guard the queue of debug messages for either NetworkAdminConsole or IConsolePrint.
std::vector<QueuedDebugItem> _debug_remote_console_queue; ///< Queue for debug messages to be passed to NetworkAdminConsole or IConsolePrint.
std::vector<QueuedDebugItem> _debug_remote_console_queue_spare; ///< Spare queue to swap with _debug_remote_console_queue.

std::array<int8_t, DebugLevelCount> _debug_levels;

const char *_savegame_DBGL_data = nullptr;
std::string _loadgame_DBGL_data;
bool _save_DBGC_data = false;
std::string _loadgame_DBGC_data;

uint32_t _misc_debug_flags;

std::array<const char *, DebugLevelCount> _debug_level_names {
	"driver",
	"grf",
	"map",
	"misc",
	"net",
	"sprite",
	"oldloader",
	"yapf",
	"fontcache",
	"script",
	"sl",
	"gamelog",
	"desync",
	"yapfdesync",
	"console",
	"linkgraph",
	"sound",
	"command",
#ifdef RANDOM_DEBUG
	"random",
	"statecsum",
#endif
};

const char *GetDebugLevelName(DebugLevelID id) { return _debug_level_names[static_cast<uint>(id)]; }

/**
 * Dump the available debug facility names in the help text.
 * @param output Where to store the output.
 */
void DumpDebugFacilityNames(format_target &output)
{
	bool written = false;
	for (uint i = 0; i < DebugLevelCount; i++) {
		if (!written) {
			output.append("List of debug facility names:\n");
		} else {
			output.append(", ");
		}
		output.append(_debug_level_names[i]);
		written = true;
	}
	output.append("\n\n");
}

void DebugIntlSetup(fmt::memory_buffer &buf, DebugLevelID dbg, int8_t level)
{
#ifdef RANDOM_DEBUG
	if (dbg == DebugLevelID::random || dbg == DebugLevelID::statecsum) {
		return;
	}
#endif
	fmt::format_to(std::back_inserter(buf), FMT_STRING("{}dbg: [{}:{}] "), log_prefix().GetLogPrefix(), GetDebugLevelName(dbg), level);
}

void debug_print_intl(DebugLevelID dbg, int8_t level, const char *buf, size_t prefix_size)
{

	if (dbg == DebugLevelID::desync) {
		static FILE *f = FioFOpenFile("commands-out.log", "wb", AUTOSAVE_DIR);
		if (f != nullptr) {
			fprintf(f, "%s%s", log_prefix().GetLogPrefix(true), buf + prefix_size);
			fflush(f);
		}
#ifdef RANDOM_DEBUG
	} else if (dbg == DebugLevelID::random || dbg == DebugLevelID::statecsum) {
#if defined(UNIX) && defined(__GLIBC__)
		static bool have_inited = false;
		static FILE *f = nullptr;

		if (!have_inited) {
			have_inited = true;
			unsigned int num = 0;
			int pid = getpid();
			for(;;) {
				std::string fn = fmt::format("random-out-{}-{}.log", pid, num);
				f = FioFOpenFile(fn.c_str(), "wx", AUTOSAVE_DIR);
				if (f == nullptr && errno == EEXIST) {
					num++;
					continue;
				}
				break;
			}
		}
#else
		static FILE *f = FioFOpenFile("random-out.log", "wb", AUTOSAVE_DIR);
#endif
		if (f != nullptr) {
			fprintf(f, "%s", buf + prefix_size);
			return;
		}
#endif
	}

	/* do not write desync messages to the console on Windows platforms, as they do
	 * not seem able to handle text direction change characters in a console without
	 * crashing, and NetworkTextMessage includes these */
#if defined(_WIN32)
	if (dbg != DebugLevelID::desync) {
		fputs(buf, stderr);
	}
#else
	fputs(buf, stderr);
#endif

	if (_debug_remote_console.load()) {
		/* Only add to the queue when there is at least one consumer of the data, exclude added newline. */
		std::string_view msg = { buf + prefix_size, strlen(buf + prefix_size) - 1 };
		if (IsNonGameThread()) {
			std::lock_guard<std::mutex> lock(_debug_remote_console_mutex);
			_debug_remote_console_queue.push_back({ dbg, level, std::string{msg} });
		} else {
			NetworkAdminConsole(GetDebugLevelName(dbg), msg);
			if (_settings_client.gui.developer >= 2) IConsolePrint(CC_DEBUG, "dbg: [{}:{}] {}", GetDebugLevelName(dbg), level, msg);
		}
	}
}

void debug_print_partial_buffer(DebugLevelID dbg, int8_t level, fmt::memory_buffer &buf, size_t prefix_size)
{
	buf.push_back('\n');
	buf.push_back('\0');

	str_strip_colours(buf.data() + prefix_size);
	debug_print_intl(dbg, level, buf.data(), prefix_size);
}

void DebugIntlVFmt(DebugLevelID dbg, int8_t level, fmt::string_view msg, fmt::format_args args)
{
	fmt::memory_buffer buf{};

	DebugIntlSetup(buf, dbg, level);
	size_t prefix_size = buf.size();

	fmt::vformat_to(std::back_inserter(buf), msg, args);
	debug_print_partial_buffer(dbg, level, buf, prefix_size);
}

/**
 * Internal function for outputting the debug line.
 * @param dbg Debug category.
 * @param level Debug level.
 * @param buf Text line to output.
 */
void debug_print(DebugLevelID dbg, int8_t level, std::string_view msg)
{
	fmt::memory_buffer buf{};

	DebugIntlSetup(buf, dbg, level);
	size_t prefix_size = buf.size();

	buf.append(msg.data(), msg.data() + msg.size());
	debug_print_partial_buffer(dbg, level, buf, prefix_size);
}

/**
 * Set debugging levels by parsing the text in \a s.
 * For setting individual levels a string like \c "net=3,grf=6" should be used.
 * If the string starts with a number, the number is used as global debugging level.
 * @param s Text describing the wanted debugging levels.
 * @param error_func The function to call if a parse error occurs.
 */
void SetDebugString(const char *s, void (*error_func)(std::string))
{
	int v;
	char *end;
	const char *t;

	/* Store planned changes into map during parse */
	std::map<DebugLevelID, int> new_levels;

	/* Global debugging level? */
	if (*s >= '0' && *s <= '9') {
		v = std::strtoul(s, &end, 0);
		s = end;

		for (uint i = 0; i < DebugLevelCount; i++) {
			new_levels[static_cast<DebugLevelID>(i)] = v;
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
		DebugLevelID found = DebugLevelID::END;
		for (uint i = 0; i < DebugLevelCount; i++) {
			if (s == t + strlen(_debug_level_names[i]) && strncmp(t, _debug_level_names[i], s - t) == 0) {
				found = static_cast<DebugLevelID>(i);
				break;
			}
		}

		if (*s == '=') s++;
		v = std::strtoul(s, &end, 0);
		s = end;
		if (found != DebugLevelID::END) {
			new_levels[found] = v;
		} else {
			error_func(fmt::format("Unknown debug level '{}'", std::string_view(t, s - t)));
			return;
		}
	}

	/* Apply the changes after parse is successful */
	for (const auto &it : new_levels) {
		_debug_levels[static_cast<uint>(it.first)] = ClampTo<int8_t>(it.second);
	}
}

/**
 * Print out the current debug-level.
 * Just return a string with the values of all the debug categories.
 * @return string with debug-levels
 */
std::string GetDebugString()
{
	auto buffer = fmt::memory_buffer();
	for (uint i = 0; i < DebugLevelCount; i++) {
		fmt::format_to(std::back_inserter(buffer), "{}{}={}", buffer.size() == 0 ? "" : ", ", _debug_levels[i], _debug_level_names[i]);
	}
	return fmt::to_string(buffer);
}

/**
 * Get the prefix for logs.
 *
 * If show_date_in_logs or \p force is enabled it returns
 * the date, otherwise it returns an empty string.
 *
 * @return the prefix for logs (do not free), never nullptr.
 */
const char *log_prefix::GetLogPrefix(bool force)
{
	if (force || _settings_client.gui.show_date_in_logs) {
		LocalTime::Format(this->buffer, lastof(this->buffer), "[%Y-%m-%d %H:%M:%S] ");
	} else {
		this->buffer[0] = '\0';
	}
	return this->buffer;
}

struct DesyncMsgLogEntry {
	EconTime::Date date;
	EconTime::DateFract date_fract;
	uint8_t tick_skip_counter;
	uint32_t src_id;
	std::string msg;

	DesyncMsgLogEntry() { }

	DesyncMsgLogEntry(std::string msg)
			: date(EconTime::CurDate()), date_fract(EconTime::CurDateFract()), tick_skip_counter(TickSkipCounter()), src_id(0), msg(msg) { }
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
	void Dump(format_target &buffer, const char *prefix, F handler)
	{
		if (this->count == 0) return;

		const unsigned int count = std::min<unsigned int>(this->count, (uint)this->log.size());
		unsigned int log_index = (this->next + (uint)this->log.size() - count) % (uint)this->log.size();
		unsigned int display_num = this->count - count;

		buffer.format("{}:\n Showing most recent {} of {} messages\n", prefix, count, this->count);

		for (unsigned int i = 0 ; i < count; i++) {
			const DesyncMsgLogEntry &entry = this->log[log_index];

			handler(display_num, buffer, entry);
			log_index = (log_index + 1) % this->log.size();
			display_num++;
		}
		buffer.push_back('\n');
	}
};

static DesyncMsgLog _desync_msg_log;
static DesyncMsgLog _remote_desync_msg_log;

void ClearDesyncMsgLog()
{
	_desync_msg_log.Clear();
}

void DumpDesyncMsgLog(format_target &buffer)
{
	_desync_msg_log.Dump(buffer, "Desync Msg Log", [](int display_num, format_target &buffer, const DesyncMsgLogEntry &entry) {
		EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(entry.date);
		buffer.format("{:5} | {:4}-{:02}-{:02}, {:2}, {:3} | {}\n", display_num, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, entry.msg);
	});
	_remote_desync_msg_log.Dump(buffer, "Remote Client Desync Msg Log", [](int display_num, format_target &buffer, const DesyncMsgLogEntry &entry) {
		EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(entry.date);
		buffer.format("{:5} | Client {:5} | {:4}-{:02}-{:02}, {:2}, {:3} | {}\n", display_num, entry.src_id, ymd.year, ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter, entry.msg);
	});
}

void LogDesyncMsg(std::string msg)
{
	if (_networking && !_network_server) {
		NetworkClientSendDesyncMsg(msg.c_str());
	}
	_desync_msg_log.LogMsg(DesyncMsgLogEntry(std::move(msg)));
}

void LogRemoteDesyncMsg(EconTime::Date date, EconTime::DateFract date_fract, uint8_t tick_skip_counter, uint32_t src_id, std::string msg)
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
		NetworkAdminConsole(GetDebugLevelName(item.category), item.message.c_str());
		if (_settings_client.gui.developer >= 2) IConsolePrint(CC_DEBUG, "dbg: [{}:{}] {}", GetDebugLevelName(item.category), item.level, item.message);
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

void TicToc::PrintAndReset()
{
	Debug(misc, 0, "[{}] {} us [avg: {:.1f} us]", this->state.name, this->state.chrono_sum, this->state.chrono_sum / static_cast<double>(this->state.count));
	this->state.count = 0;
	this->state.chrono_sum = 0;
}

[[noreturn]] void AssertMsgErrorVFmt(int line, const char *file, const char *expr, fmt::string_view msg, fmt::format_args args)
{
	format_buffer out;
	out.vformat(msg, args);

	assert_str_error(line, file, expr, out);
}

[[noreturn]] void AssertMsgTileErrorVFmt(int line, const char *file, const char *expr, uint32_t tile, fmt::string_view msg, fmt::format_args args)
{
	format_buffer out;
	DumpTileInfo(out, tile);
	out.append(", ");
	out.vformat(msg, args);

	assert_str_error(line, file, expr, out);
}

void assert_tile_error(int line, const char *file, const char *expr, uint32_t tile)
{
	format_buffer out;
	DumpTileInfo(out, tile);

	assert_str_error(line, file, expr, out);
}
