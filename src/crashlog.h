/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog.h Functions to be called to log a crash */

#ifndef CRASHLOG_H
#define CRASHLOG_H

#include "core/enum_type.hpp"
#include <string>

struct DesyncExtraInfo {
	enum Flags {
		DEIF_NONE       = 0,      ///< no flags
		DEIF_RAND1      = 1 << 0, ///< random 1 mismatch
		DEIF_RAND2      = 1 << 1, ///< random 2 mismatch
		DEIF_STATE      = 1 << 2, ///< state mismatch
		DEIF_DBL_RAND   = 1 << 3, ///< double-seed sent
	};

	Flags flags = DEIF_NONE;
	FILE **log_file = nullptr; ///< save unclosed log file handle here
};
DECLARE_ENUM_AS_BIT_SET(DesyncExtraInfo::Flags)

/**
 * Helper class for creating crash logs.
 */
class CrashLog {
private:
	/** Pointer to the error message. */
	static const char *message;

	/** Temporary 'local' location of the buffer. */
	static char *gamelog_buffer;

	/** Temporary 'local' location of the end of the buffer. */
	static const char *gamelog_last;

	static void GamelogFillCrashLog(const char *s);
protected:
	/**
	 * Writes OS' version to the buffer.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogOSVersion(char *buffer, const char *last) const = 0;

	/**
	 * Writes compiler (and its version, if available) to the buffer.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogCompiler(char *buffer, const char *last) const;

	/**
	 * Writes OS' version detail to the buffer, if available.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogOSVersionDetail(char *buffer, const char *last) const;

	/**
	 * Writes actually encountered error to the buffer.
	 * @param buffer  The begin where to write at.
	 * @param last    The last position in the buffer to write to.
	 * @param message Message passed to use for possible errors. Can be nullptr.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogError(char *buffer, const char *last, const char *message) const = 0;

	/**
	 * Writes the stack trace to the buffer, if there is information about it
	 * available.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogStacktrace(char *buffer, const char *last) const = 0;

	/**
	 * Writes information about the data in the registers, if there is
	 * information about it available.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogRegisters(char *buffer, const char *last) const;

	/**
	 * Writes the dynamically linked libraries/modules to the buffer, if there
	 * is information about it available.
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogModules(char *buffer, const char *last) const;

#ifdef USE_SCOPE_INFO
	/**
	 * Writes the scope info log to the buffer.
	 * This may only be called when IsMainThread() returns true
	 * @param buffer The begin where to write at.
	 * @param last   The last position in the buffer to write to.
	 * @return the position of the \c '\0' character after the buffer.
	 */
	virtual char *LogScopeInfo(char *buffer, const char *last) const;
#endif

	char *LogOpenTTDVersion(char *buffer, const char *last) const;
	char *LogConfiguration(char *buffer, const char *last) const;
	char *LogLibraries(char *buffer, const char *last) const;
	char *LogGamelog(char *buffer, const char *last) const;
	char *LogRecentNews(char *buffer, const char *list) const;
	char *LogCommandLog(char *buffer, const char *last) const;

public:
	/** Buffer for the filename name prefix */
	char name_buffer[64];

	/** Stub destructor to silence some compilers. */
	virtual ~CrashLog() {}

	char *FillCrashLog(char *buffer, const char *last) const;
	char *FillDesyncCrashLog(char *buffer, const char *last, const DesyncExtraInfo &info) const;
	char *FillVersionInfoLog(char *buffer, const char *last) const;
	bool WriteCrashLog(const char *buffer, char *filename, const char *filename_last, const char *name = "crash", FILE **crashlog_file = nullptr) const;

	/**
	 * Write the (crash) dump to a file.
	 * @note On success the filename will be filled with the full path of the
	 *       crash dump file. Make sure filename is at least \c MAX_PATH big.
	 * @param filename      Output for the filename of the written file.
	 * @param filename_last The last position in the filename buffer.
	 * @return if less than 0, error. If 0 no dump is made, otherwise the dump
	 *         was successful (not all OSes support dumping files).
	 */
	virtual int WriteCrashDump(char *filename, const char *filename_last) const;
	bool WriteSavegame(char *filename, const char *filename_last, const char *name = "crash") const;
	bool WriteScreenshot(char *filename, const char *filename_last, const char *name = "crash") const;

	bool MakeCrashLog();
	bool MakeDesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info) const;
	bool MakeVersionInfoLog() const;
	bool MakeCrashSavegameAndScreenshot() const;

	/**
	 * Initialiser for crash logs; do the appropriate things so crashes are
	 * handled by our crash handler instead of returning straight to the OS.
	 * @note must be implemented by all implementers of CrashLog.
	 */
	static void InitialiseCrashLog();

	static void DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info);
	static void VersionInfoLog();

	static void SetErrorMessage(const char *message);
	static void AfterCrashLogCleanup();

	inline const char *GetMessage() const { return this->message; }

	static const char *GetAbortCrashlogReason();

	static const CrashLog *main_thread_pending_crashlog;

	static void MainThreadExitCheckPendingCrashlog();
};

#endif /* CRASHLOG_H */
