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
#include <vector>

struct DesyncDeferredSaveInfo {
	std::string name_buffer;
};

struct DesyncExtraInfo {
	enum Flags {
		DEIF_NONE       = 0,      ///< no flags
		DEIF_RAND       = 1 << 0, ///< random mismatch
		DEIF_STATE      = 1 << 1, ///< state mismatch
	};

	Flags flags = DEIF_NONE;
	const char *client_name = nullptr;
	int client_id = -1;
	std::string desync_frame_info;
	FILE **log_file = nullptr; ///< save unclosed log file handle here
	DesyncDeferredSaveInfo *defer_savegame_write = nullptr;
};
DECLARE_ENUM_AS_BIT_SET(DesyncExtraInfo::Flags)

struct InconsistencyExtraInfo {
	std::vector<std::string> check_caches_result;
};

/**
 * Helper class for creating crash logs.
 */
class CrashLog {
private:
	/** Pointer to the error message. */
	static const char *message;

	/** Whether a crash has already occured */
	static bool have_crashed;

protected:
	/**
	 * Writes OS' version to the buffer.
	 * @param buffer The output buffer.
	 */
	virtual void LogOSVersion(struct format_target &buffer) const = 0;

	/**
	 * Writes compiler (and its version, if available) to the buffer.
	 * @param buffer The output buffer.
	 */
	virtual void LogCompiler(struct format_target &buffer) const;

	/**
	 * Writes OS' version detail to the buffer, if available.
	 * @param buffer The output buffer.
	 */
	virtual void LogOSVersionDetail(struct format_target &buffer) const;

	/**
	 * Writes actually encountered error to the buffer.
	 * @param buffer The output buffer.
	 * @param message Message passed to use for possible errors. Can be nullptr.
	 */
	virtual void LogError(struct format_target &buffer, const char *message) const = 0;

	/**
	 * Writes the stack trace to the buffer, if there is information about it
	 * available.
	 * @param buffer The output buffer.
	 */
	virtual void LogStacktrace(struct format_target &buffer) const = 0;

	/**
	 * Writes information about extra debug info, if there is
	 * information about it available.
	 * @param buffer The output buffer.
	 */
	virtual void LogDebugExtra(struct format_target &buffer) const;

	/**
	 * Writes information about the data in the registers, if there is
	 * information about it available.
	 * @param buffer The output buffer.
	 */
	virtual void LogRegisters(struct format_target &buffer) const;

	/**
	 * Writes a final section in the crash log, if there is anything
	 * to add at the end.
	 * @param buffer The output buffer.
	 */
	virtual void LogCrashTrailer(struct format_target &buffer) const;

#if !defined(DISABLE_SCOPE_INFO)
	/**
	 * Writes the scope info log to the buffer.
	 * This may only be called when IsMainThread() returns true
	 * @param buffer The output buffer.
	 */
	void LogScopeInfo(struct format_target &buffer) const;
#endif

	void LogOpenTTDVersion(struct format_target &buffer) const;
	void LogConfiguration(struct format_target &buffer) const;
	void LogLibraries(struct format_target &buffer) const;
	void LogPlugins(struct format_target &buffer) const;
	void LogGamelog(struct format_target &buffer) const;
	void LogRecentNews(struct format_target &buffer) const;
	void LogCommandLog(struct format_target &buffer) const;
	void LogSettings(struct format_target &buffer) const;

	virtual void StartCrashLogFaultHandler();
	virtual void StopCrashLogFaultHandler();

	using CrashLogSectionWriter = void(CrashLog *self, struct format_target &buffer);
	virtual char *TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer);
	virtual void CrashLogFaultSectionCheckpoint(struct format_target &buffer) const;

public:
	/** Buffer for the filename name prefix */
	char name_buffer[64];
	FILE *crash_file = nullptr;
	const char *crash_buffer_write = nullptr;

	/** Buffer for the filename of the crash log */
	char crashlog_filename[MAX_PATH];
	/** Buffer for the filename of the crash dump */
	char crashdump_filename[MAX_PATH];
	/** Buffer for the filename of the crash savegame */
	char savegame_filename[MAX_PATH];
	/** Buffer for the filename of the crash screenshot */
	char screenshot_filename[MAX_PATH];

	CrashLog() {
		this->crashlog_filename[0] = '\0';
		this->crashdump_filename[0] = '\0';
		this->savegame_filename[0] = '\0';
		this->screenshot_filename[0] = '\0';
	}

	/** Stub destructor to silence some compilers. */
	virtual ~CrashLog() = default;

	char *FillCrashLog(char *buffer, const char *last);
	void FlushCrashLogBuffer(const char *end);
	void CloseCrashLogFile(const char *end);
	void FillDesyncCrashLog(struct format_target &buffer, const DesyncExtraInfo &info) const;
	void FillInconsistencyLog(struct format_target &buffer, const InconsistencyExtraInfo &info) const;
	void FillVersionInfoLog(struct format_target &buffer) const;
	bool WriteCrashLog(std::string_view data, char *filename, const char *filename_last, const char *name = "crash", FILE **crashlog_file = nullptr) const;

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

	static bool WriteSavegame(char *filename, const char *filename_last, const char *name = "crash");
	static bool WriteDiagnosticSavegame(char *filename, const char *filename_last, const char *name);
	static bool WriteScreenshot(char *filename, const char *filename_last, const char *name = "crash");

	void MakeCrashLog(char *buffer, const char *last);
	void MakeCrashLogWithStackBuffer();
	void MakeDesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info) const;
	static bool WriteDesyncSavegame(const char *log_data, const char *name_buffer);
	void MakeInconsistencyLog(const InconsistencyExtraInfo &info) const;
	void MakeCrashSavegameAndScreenshot();

	void SendSurvey() const;

	/**
	 * Initialiser for crash logs; do the appropriate things so crashes are
	 * handled by our crash handler instead of returning straight to the OS.
	 * @note must be implemented by all implementers of CrashLog.
	 */
	static void InitialiseCrashLog();

	static void DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info);
	static void InconsistencyLog(const InconsistencyExtraInfo &info);
	static void VersionInfoLog(struct format_target &buffer);

	static void RegisterCrashed() { CrashLog::have_crashed = true; }
	static bool HaveAlreadyCrashed() { return CrashLog::have_crashed; }
	static void SetErrorMessage(const char *message);
	static void AfterCrashLogCleanup();

	inline const char *GetMessage() const { return this->message; }

	static const char *GetAbortCrashlogReason();
};

#endif /* CRASHLOG_H */
