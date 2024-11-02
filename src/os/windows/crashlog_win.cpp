/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog_win.cpp Implementation of a crashlogger for Windows */

#include "../../stdafx.h"
#include "../../crashlog.h"
#include "../../crashlog_bfd.h"
#include "win32.h"
#include "../../core/alloc_func.hpp"
#include "../../core/format.hpp"
#include "../../core/math_func.hpp"
#include "../../string_func.h"
#include "../../fileio_func.h"
#include "../../strings_func.h"
#include "../../gamelog.h"
#include "../../sl/saveload.h"
#include "../../video/video_driver.hpp"
#include "../../library_loader.h"
#include "../../screenshot.h"
#include "../../debug.h"
#include "../../settings_type.h"
#include "../../thread.h"
#include "../../walltime_func.h"
#include "../../scope.h"
#if defined(WITH_DEMANGLE)
#include <cxxabi.h>
#endif

#include <windows.h>
#include <mmsystem.h>
#include <signal.h>
#include <psapi.h>
#include <memoryapi.h>
#if defined(_MSC_VER)
#include <excpt.h>
#else
#include <setjmp.h>
#endif
#include <atomic>

#include "../../safeguards.h"

/* fmt format specification for 32/64-bit addresses. */
#ifdef _M_AMD64
#define FMT_LOC "{:016X}"
#else
#define FMT_LOC "{:08X}"
#endif

#if !defined(_MSC_VER) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

void CrashLogWindowsInitThread();

[[noreturn]] static void ImmediateExitProcess(uint exit_code)
{
	/* TerminateProcess may fail in some special edge cases, fall back to ExitProcess in this case */
	TerminateProcess(GetCurrentProcess(), exit_code);
	ExitProcess(exit_code);
}

/**
 * Windows implementation for the crash logger.
 */
class CrashLogWindows final : public CrashLog {
	/** Information about the encountered exception */
	EXCEPTION_POINTERS *ep;

	HANDLE crash_file = INVALID_HANDLE_VALUE;

public:
	DWORD crash_thread_id;
	std::atomic<uint32_t> other_crash_threads;

	void LogOSVersion(format_target &buffer) const override;
	void LogError(format_target &buffer, const char *message) const override;
#if defined(_MSC_VER) || defined(WITH_DBGHELP)
	void LogStacktrace(format_target &buffer) const override;
#endif /* _MSC_VER || WITH_DBGHELP */
	void LogRegisters(format_target &buffer) const override;
	void LogCrashTrailer(format_target &buffer) const override;

protected:
	char *TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer) override;
	void CrashLogFaultSectionCheckpoint(format_target &buffer) const override;

public:

	bool OpenLogFile(const char *filename) override
	{
		wchar_t wfilename[MAX_PATH];
		convert_to_fs(filename, wfilename);
		HANDLE file = CreateFile(wfilename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, 0);
		if (file != INVALID_HANDLE_VALUE) {
			this->crash_file = file;
			return true;
		} else {
			return false;
		}
	}

	void WriteToFile(HANDLE file, std::string_view data)
	{
		while (!data.empty()) {
			DWORD written = 0;
			bool ok = WriteFile(file, data.data(), (DWORD)data.size(), &written, nullptr);
			if (!ok) {
				break;
			} else if (written == 0) {
				break;
			} else {
				data.remove_prefix(written);
			}
		}
	}

	void WriteToLogFile(std::string_view data) override
	{
		this->WriteToFile(this->crash_file, data);
	}

	void WriteToStdout(std::string_view data) override
	{
		HANDLE file = GetStdHandle(STD_OUTPUT_HANDLE);
		if (GetFileType(file) == FILE_TYPE_CHAR) {
			wchar_t wdata[2048];
			int len = MultiByteToWideChar(CP_UTF8, 0, data.data(), static_cast<int>(data.size()), wdata, static_cast<int>(std::size(wdata)));
			if (len > 0) {
				WriteConsoleW(file, wdata, len, nullptr, nullptr);
			}
		} else {
			this->WriteToFile(file, data);
		}
	}

	void CloseLogFile() override
	{
		CloseHandle(this->crash_file);
		this->crash_file = INVALID_HANDLE_VALUE;
	}

#if defined(_MSC_VER)
	int WriteCrashDump(char *filename, const char *filename_last) const override;
#endif /* _MSC_VER */

	/** Buffer for the generated crash log */
	std::span<char> crashlog_buffer;

	/**
	 * A crash log is always generated when it's generated.
	 * @param ep the data related to the exception.
	 */
	CrashLogWindows(EXCEPTION_POINTERS *ep = nullptr) :
		ep(ep), crash_thread_id(GetCurrentThreadId()) {}

	/**
	 * Points to the current crash log.
	 */
	static std::atomic<CrashLogWindows *> current;

	char *internal_fault_saved_buffer = nullptr;
#if !defined(_MSC_VER)
	jmp_buf internal_fault_jmp_buf;
#endif
};

/* static */ std::atomic<CrashLogWindows *> CrashLogWindows::current = nullptr;

/* virtual */ void CrashLogWindows::LogOSVersion(format_target &buffer) const
{
	_OSVERSIONINFOA os;
	os.dwOSVersionInfoSize = sizeof(os);
	GetVersionExA(&os);

	buffer.format(
			"Operating system:\n"
			" Name:     Windows\n"
			" Release:  {}.{}.{} ({})\n",
			os.dwMajorVersion,
			os.dwMinorVersion,
			os.dwBuildNumber,
			os.szCSDVersion
	);
}

static const char *GetAccessViolationTypeString(uint type)
{
	switch (type) {
		case 0:
			return "read";
		case 1:
			return "write";
		case 8:
			return "user-mode DEP";
		default:
			return "???";
	}
}

/* virtual */ void CrashLogWindows::LogError(format_target &buffer, const char *message) const
{
	buffer.append("Crash reason:\n");
	for (auto record = ep->ExceptionRecord; record != nullptr; record = record->ExceptionRecord) {
		buffer.format(
				" Exception:  {:08X}\n"
				" Location:   {:X}\n",
				record->ExceptionCode,
				(size_t)record->ExceptionAddress
		);
		if (record->ExceptionCode == 0xC0000005 && record->NumberParameters == 2) {
			buffer.format(
					" Fault type: {} ({})\n"
					" Fault addr: " FMT_LOC "\n",
					(uint) record->ExceptionInformation[0],
					GetAccessViolationTypeString(record->ExceptionInformation[0]),
					(size_t)record->ExceptionInformation[1]
			);
		} else {
			for (uint i = 0; i < (uint) record->NumberParameters; i++) {
				buffer.format(
						" Info {}:     " FMT_LOC "\n",
						i,
						(size_t)record->ExceptionInformation[i]
				);
			}
		}
	}
	this->CrashLogFaultSectionCheckpoint(buffer);
	buffer.format(" Message:    {}\n\n",
			message == nullptr ? "<none>" : message);

	if (message != nullptr && strcasestr(message, "out of memory") != nullptr) {
		PROCESS_MEMORY_COUNTERS pmc;
		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
			buffer.format(" WorkingSetSize: {}\n", pmc.WorkingSetSize);
			buffer.format(" PeakWorkingSetSize: {}\n", pmc.PeakWorkingSetSize);
			buffer.format(" QuotaPagedPoolUsage: {}\n", pmc.QuotaPagedPoolUsage);
			buffer.format(" QuotaPeakPagedPoolUsage: {}\n", pmc.QuotaPeakPagedPoolUsage);
			buffer.format(" QuotaNonPagedPoolUsage: {}\n", pmc.QuotaNonPagedPoolUsage);
			buffer.format(" QuotaPeakNonPagedPoolUsage: {}\n", pmc.QuotaPeakNonPagedPoolUsage);
			buffer.format(" PagefileUsage: {}\n", pmc.PagefileUsage);
			buffer.format(" PeakPagefileUsage: {}\n\n", pmc.PeakPagefileUsage);
		}
		PERFORMANCE_INFORMATION perf;
		if (GetPerformanceInfo(&perf, sizeof(perf))) {
			buffer.format(" CommitTotal: {}\n", perf.CommitTotal);
			buffer.format(" CommitLimit: {}\n", perf.CommitLimit);
			buffer.format(" CommitPeak: {}\n", perf.CommitPeak);
			buffer.format(" PhysicalTotal: {}\n", perf.PhysicalTotal);
			buffer.format(" PhysicalAvailable: {}\n", perf.PhysicalAvailable);
			buffer.format(" SystemCache: {}\n", perf.SystemCache);
			buffer.format(" KernelTotal: {}\n", perf.KernelTotal);
			buffer.format(" KernelPaged: {}\n", perf.KernelPaged);
			buffer.format(" KernelNonpaged: {}\n", perf.KernelNonpaged);
			buffer.format(" PageSize: {}\n", perf.PageSize);
			buffer.format(" HandleCount: {}\n", perf.HandleCount);
			buffer.format(" ProcessCount: {}\n", perf.ProcessCount);
			buffer.format(" ThreadCount: {}\n\n", perf.ThreadCount);
		}
	}
}

/* virtual */ void CrashLogWindows::LogRegisters(format_target &buffer) const
{
	buffer.append("Registers:\n");
#ifdef _M_AMD64
	buffer.format(
		" RAX: {:016X} RBX: {:016X} RCX: {:016X} RDX: {:016X}\n"
		" RSI: {:016X} RDI: {:016X} RBP: {:016X} RSP: {:016X}\n"
		" R8:  {:016X} R9:  {:016X} R10: {:016X} R11: {:016X}\n"
		" R12: {:016X} R13: {:016X} R14: {:016X} R15: {:016X}\n"
		" RIP: {:016X} EFLAGS: {:08X}\n",
		ep->ContextRecord->Rax,
		ep->ContextRecord->Rbx,
		ep->ContextRecord->Rcx,
		ep->ContextRecord->Rdx,
		ep->ContextRecord->Rsi,
		ep->ContextRecord->Rdi,
		ep->ContextRecord->Rbp,
		ep->ContextRecord->Rsp,
		ep->ContextRecord->R8,
		ep->ContextRecord->R9,
		ep->ContextRecord->R10,
		ep->ContextRecord->R11,
		ep->ContextRecord->R12,
		ep->ContextRecord->R13,
		ep->ContextRecord->R14,
		ep->ContextRecord->R15,
		ep->ContextRecord->Rip,
		ep->ContextRecord->EFlags
	);
#elif defined(_M_IX86)
	buffer.format(
		" EAX: {:08X} EBX: {:08X} ECX: {:08X} EDX: {:08X}\n"
		" ESI: {:08X} EDI: {:08X} EBP: {:08X} ESP: {:08X}\n"
		" EIP: {:08X} EFLAGS: {:08X}\n",
		ep->ContextRecord->Eax,
		ep->ContextRecord->Ebx,
		ep->ContextRecord->Ecx,
		ep->ContextRecord->Edx,
		ep->ContextRecord->Esi,
		ep->ContextRecord->Edi,
		ep->ContextRecord->Ebp,
		ep->ContextRecord->Esp,
		ep->ContextRecord->Eip,
		ep->ContextRecord->EFlags
	);
#elif defined(_M_ARM64)
	buffer.format(
		" X0:  {:016X} X1:  {:016X} X2:  {:016X} X3:  {:016X}\n"
		" X4:  {:016X} X5:  {:016X} X6:  {:016X} X7:  {:016X}\n"
		" X8:  {:016X} X9:  {:016X} X10: {:016X} X11: {:016X}\n"
		" X12: {:016X} X13: {:016X} X14: {:016X} X15: {:016X}\n"
		" X16: {:016X} X17: {:016X} X18: {:016X} X19: {:016X}\n"
		" X20: {:016X} X21: {:016X} X22: {:016X} X23: {:016X}\n"
		" X24: {:016X} X25: {:016X} X26: {:016X} X27: {:016X}\n"
		" X28: {:016X} Fp:  {:016X} Lr:  {:016X}\n",
		ep->ContextRecord->X0,
		ep->ContextRecord->X1,
		ep->ContextRecord->X2,
		ep->ContextRecord->X3,
		ep->ContextRecord->X4,
		ep->ContextRecord->X5,
		ep->ContextRecord->X6,
		ep->ContextRecord->X7,
		ep->ContextRecord->X8,
		ep->ContextRecord->X9,
		ep->ContextRecord->X10,
		ep->ContextRecord->X11,
		ep->ContextRecord->X12,
		ep->ContextRecord->X13,
		ep->ContextRecord->X14,
		ep->ContextRecord->X15,
		ep->ContextRecord->X16,
		ep->ContextRecord->X17,
		ep->ContextRecord->X18,
		ep->ContextRecord->X19,
		ep->ContextRecord->X20,
		ep->ContextRecord->X21,
		ep->ContextRecord->X22,
		ep->ContextRecord->X23,
		ep->ContextRecord->X24,
		ep->ContextRecord->X25,
		ep->ContextRecord->X26,
		ep->ContextRecord->X27,
		ep->ContextRecord->X28,
		ep->ContextRecord->Fp,
		ep->ContextRecord->Lr
	);
#endif

	this->CrashLogFaultSectionCheckpoint(buffer);

	buffer.append("\n Bytes at instruction pointer:\n");
#ifdef _M_AMD64
	uint8_t *b = (uint8_t*)ep->ContextRecord->Rip;
#elif defined(_M_IX86)
	uint8_t *b = (uint8_t*)ep->ContextRecord->Eip;
#elif defined(_M_ARM64)
	uint8_t *b = (uint8_t*)ep->ContextRecord->Pc;
#endif
	for (int i = 0; i != 24; i++) {
		if (IsBadReadPtr(b, 1)) {
			buffer.append(" ??"); // OCR: WAS: , 0);
		} else {
			buffer.format(" {:02X}", *b);
		}
		b++;
	}
	buffer.append("\n\n");
}

/**
 * Log crash trailer
 */
void CrashLogWindows::LogCrashTrailer(format_target &buffer) const
{
	uint32_t other_crashed_threads = this->other_crash_threads.load();
	if (other_crashed_threads > 0) {
		buffer.format("\n*** {} other threads have also crashed ***\n\n", other_crashed_threads);
	}
}

#if defined(_MSC_VER) || defined(WITH_DBGHELP)
static const uint MAX_SYMBOL_LEN = 512;
static const uint MAX_FRAMES     = 64;
#if defined(_MSC_VER)
#pragma warning(disable:4091)
#endif
#include <dbghelp.h>
#if defined(_MSC_VER)
#pragma warning(default:4091)
#endif

/* virtual */ void CrashLogWindows::LogStacktrace(format_target &buffer) const
{
	LibraryLoader dbghelp("dbghelp.dll");
	struct ProcPtrs {
		BOOL (WINAPI * pSymInitialize)(HANDLE, PCSTR, BOOL);
		BOOL (WINAPI * pSymSetOptions)(DWORD);
		BOOL (WINAPI * pSymCleanup)(HANDLE);
		BOOL (WINAPI * pStackWalk64)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
		PVOID (WINAPI * pSymFunctionTableAccess64)(HANDLE, DWORD64);
		DWORD64 (WINAPI * pSymGetModuleBase64)(HANDLE, DWORD64);
		BOOL (WINAPI * pSymGetModuleInfo64)(HANDLE, DWORD64, PIMAGEHLP_MODULE64);
		BOOL (WINAPI * pSymGetSymFromAddr64)(HANDLE, DWORD64, PDWORD64, PIMAGEHLP_SYMBOL64);
		BOOL (WINAPI * pSymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
	} proc = {
		dbghelp.GetFunction("SymInitialize"),
		dbghelp.GetFunction("SymSetOptions"),
		dbghelp.GetFunction("SymCleanup"),
		dbghelp.GetFunction("StackWalk64"),
		dbghelp.GetFunction("SymFunctionTableAccess64"),
		dbghelp.GetFunction("SymGetModuleBase64"),
		dbghelp.GetFunction("SymGetModuleInfo64"),
		dbghelp.GetFunction("SymGetSymFromAddr64"),
		dbghelp.GetFunction("SymGetLineFromAddr64"),
	};

	buffer.append("Decoded stack trace:\n");

	/* Try to load the functions from the DLL, if that fails because of a too old dbghelp.dll, just skip it. */
	if (!dbghelp.HasError()) {
		/* Initialize symbol handler. */
		HANDLE hCur = GetCurrentProcess();
		proc.pSymInitialize(hCur, nullptr, TRUE);
		/* Load symbols only when needed, fail silently on errors, demangle symbol names. */
		proc.pSymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_UNDNAME);

		/* Initialize starting stack frame from context record. */
		STACKFRAME64 frame;
		memset(&frame, 0, sizeof(frame));
#ifdef _M_AMD64
		frame.AddrPC.Offset = ep->ContextRecord->Rip;
		frame.AddrFrame.Offset = ep->ContextRecord->Rbp;
		frame.AddrStack.Offset = ep->ContextRecord->Rsp;
#elif defined(_M_IX86)
		frame.AddrPC.Offset = ep->ContextRecord->Eip;
		frame.AddrFrame.Offset = ep->ContextRecord->Ebp;
		frame.AddrStack.Offset = ep->ContextRecord->Esp;
#elif defined(_M_ARM64)
		frame.AddrPC.Offset = ep->ContextRecord->Pc;
		frame.AddrFrame.Offset = ep->ContextRecord->Fp;
		frame.AddrStack.Offset = ep->ContextRecord->Sp;
#endif
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Mode = AddrModeFlat;

		/* Copy context record as StackWalk64 may modify it. */
		CONTEXT ctx;
		memcpy(&ctx, ep->ContextRecord, sizeof(ctx));

		/* Allocate space for symbol info.
		 * The total initialised size must be sufficient for a null-terminating char at sym_info->Name[sym_info->MaxNameLength],
		 * SymGetSymFromAddr64 is not required to write a null-terminating char.
		 * sizeof(IMAGEHLP_SYMBOL64) includes at least one char of the Name buffer. */
		std::array<char, sizeof(IMAGEHLP_SYMBOL64) + MAX_SYMBOL_LEN> sym_info_raw{};
		IMAGEHLP_SYMBOL64 *sym_info = reinterpret_cast<IMAGEHLP_SYMBOL64*>(sym_info_raw.data());
		sym_info->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		sym_info->MaxNameLength = MAX_SYMBOL_LEN;

		std::array<DWORD64, 8> last_offsets = {};

#if defined(WITH_BFD)
		sym_bfd_obj_cache bfd_cache;
		bfd_init();
#endif /* WITH_BFD */

		/* Walk stack at most MAX_FRAMES deep in case the stack is corrupt. */
		for (uint num = 0; num < MAX_FRAMES; num++) {
			auto guard = scope_guard([&]() {
				this->CrashLogFaultSectionCheckpoint(buffer);
			});
			if (!proc.pStackWalk64(
#ifdef _M_AMD64
				IMAGE_FILE_MACHINE_AMD64,
#else
				IMAGE_FILE_MACHINE_I386,
#endif
				hCur, GetCurrentThread(), &frame, &ctx, nullptr, proc.pSymFunctionTableAccess64, proc.pSymGetModuleBase64, nullptr)) break;

			if (std::find_if(last_offsets.begin(), last_offsets.end(), [&](DWORD64 offset) { return offset != frame.AddrPC.Offset; }) == last_offsets.end()) {
				buffer.append(" <infinite loop>\n");
				break;
			}

			last_offsets[num % last_offsets.size()] = frame.AddrPC.Offset;

			/* Get module name. */
			const char *mod_name = "???";
			const char *image_name = nullptr;
			[[maybe_unused]] DWORD64 image_base = 0;

			IMAGEHLP_MODULE64 module;
			module.SizeOfStruct = sizeof(module);
			if (proc.pSymGetModuleInfo64(hCur, frame.AddrPC.Offset, &module)) {
				mod_name = module.ModuleName;
				image_name = module.ImageName;
				image_base = module.BaseOfImage;
			}

			/* Print module and instruction pointer. */
			buffer.format("[{:02}] {:<20} 0x" FMT_LOC, num, mod_name, (uintptr_t) frame.AddrPC.Offset);

			/* Get symbol name and line info if possible. */
			DWORD64 offset;
			if (proc.pSymGetSymFromAddr64(hCur, frame.AddrPC.Offset, &offset, sym_info)) {
				if (offset > INT64_MAX) {
					buffer.format(" {} - {}", sym_info->Name, (DWORD64)(-(int64_t)offset));
				} else {
					buffer.format(" {} + {}", sym_info->Name, offset);
				}

				DWORD line_offs;
				IMAGEHLP_LINE64 line;
				line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
				if (proc.pSymGetLineFromAddr64(hCur, frame.AddrPC.Offset, &line_offs, &line)) {
					buffer.format(" ({}:{})", line.FileName, line.LineNumber);
				}
			} else if (image_name != nullptr) {
#if defined (WITH_BFD)
				/* subtract one to get the line before the return address, i.e. the function call line */
				sym_info_bfd bfd_info(static_cast<bfd_vma>(frame.AddrPC.Offset) - static_cast<bfd_vma>(image_base) - 1);
				lookup_addr_bfd(image_name, bfd_cache, bfd_info);
				if (bfd_info.function_name != nullptr) {
					const char *func_name = bfd_info.function_name;
#if defined(WITH_DEMANGLE)
					int status = -1;
					char *demangled = abi::__cxa_demangle(func_name, nullptr, 0, &status);
					if (demangled != nullptr && status == 0) {
						func_name = demangled;
					}
#endif
					bool symbol_ok = strncmp(func_name, ".rdata$", 7) != 0 && strncmp(func_name, ".debug_loc", 10) != 0;
					if (symbol_ok) {
						buffer.format(" {}", func_name);
					}
#if defined(WITH_DEMANGLE)
					free(demangled);
#endif
					if (symbol_ok && bfd_info.function_addr) {
						DWORD64 adjusted_addrpc = frame.AddrPC.Offset - image_base + bfd_info.image_base;
						if (bfd_info.function_addr > adjusted_addrpc) {
							buffer.format(" - {}", static_cast<DWORD64>(bfd_info.function_addr) - adjusted_addrpc);
						} else {
							buffer.format(" + {}", adjusted_addrpc - static_cast<DWORD64>(bfd_info.function_addr));
						}
					}
				}
				if (bfd_info.file_name != nullptr) {
					buffer.format(" ({}:{})", bfd_info.file_name, bfd_info.line);
				}
				if (bfd_info.found && bfd_info.abfd) {
					const char *file_name = nullptr;
					const char *func_name = nullptr;
					uint line_num = 0;
					uint iteration_limit = 32;
					while (iteration_limit-- && bfd_find_inliner_info(bfd_info.abfd, &file_name, &func_name, &line_num)) {
						buffer.format("\n[inlined]{:{}}", "", (int)(19 + (sizeof(void *) * 2)));
						if (func_name) {
							int status = -1;
							char *demangled = nullptr;
#if defined(WITH_DEMANGLE)
							demangled = abi::__cxa_demangle(func_name, nullptr, 0, &status);
#endif
							const char *name = (demangled != nullptr && status == 0) ? demangled : func_name;
							buffer.format(" {}", name);
							free(demangled);
						}
						if (file_name != nullptr) {
							buffer.format(" ({}:{})", file_name, line_num);
						}
					}
				}
#endif
			}
			buffer.push_back('\n');
		}

		proc.pSymCleanup(hCur);
	}

	buffer.push_back('\n');
}
#endif /* _MSC_VER  || WITH_DBGHELP */

#if defined(_MSC_VER)
/* virtual */ int CrashLogWindows::WriteCrashDump(char *filename, const char *filename_last) const
{
	if (_settings_client.gui.developer == 0) return 0;

	int ret = 0;
	HMODULE dbghelp = LoadLibrary(L"dbghelp.dll");
	if (dbghelp != nullptr) {
		typedef BOOL (WINAPI *MiniDumpWriteDumpT)(HANDLE, DWORD, HANDLE,
				MINIDUMP_TYPE,
				CONST PMINIDUMP_EXCEPTION_INFORMATION,
				CONST PMINIDUMP_USER_STREAM_INFORMATION,
				CONST PMINIDUMP_CALLBACK_INFORMATION);
		MiniDumpWriteDumpT funcMiniDumpWriteDump = (MiniDumpWriteDumpT) GetProcAddress(dbghelp, "MiniDumpWriteDump");
		if (funcMiniDumpWriteDump != nullptr) {
			format_to_fixed_z::format_to(filename, filename_last, "{}crash.dmp", _personal_dir);
			HANDLE file  = CreateFile(OTTD2FS(filename).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, 0);
			HANDLE proc  = GetCurrentProcess();
			DWORD procid = GetCurrentProcessId();
			MINIDUMP_EXCEPTION_INFORMATION mdei;
			MINIDUMP_USER_STREAM userstream;
			MINIDUMP_USER_STREAM_INFORMATION musi;

			userstream.Type        = LastReservedStream + 1;
			userstream.Buffer      = const_cast<void *>(static_cast<const void*>(this->crashlog_buffer.data()));
			userstream.BufferSize  = (ULONG)strlen(this->crashlog_buffer.data()) + 1;

			musi.UserStreamCount   = 1;
			musi.UserStreamArray   = &userstream;

			mdei.ThreadId = GetCurrentThreadId();
			mdei.ExceptionPointers  = ep;
			mdei.ClientPointers     = false;

			funcMiniDumpWriteDump(proc, procid, file, MiniDumpWithDataSegs, &mdei, &musi, nullptr);
			ret = 1;
		} else {
			ret = -1;
		}
		FreeLibrary(dbghelp);
	}
	return ret;
}
#endif /* _MSC_VER  || WITH_DBGHELP */

#if defined(_MSC_VER)
	/**
	 * Call the handler from within an SEH try/except block for the case where we trigger another exception
	 * during the course of calling the given log section writer.
	 *
	 * If an exception does occur, restore the buffer pointer to either the original value, or
	 * the value provided in any later checkpoint.
	 * Insert a message describing the problem and give up on the section.
	 */
	/* virtual */ char *CrashLogWindows::TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer)
	{
		this->FlushCrashLogBuffer(buffer);
		this->internal_fault_saved_buffer = buffer;

		__try {
			format_to_fixed buf(buffer, last - buffer);
			writer(this, buf);
			buffer += buf.size();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (this->internal_fault_saved_buffer == nullptr) {
				/* if we get here, things are unrecoverable */
				ImmediateExitProcess(43);
			}

			buffer = this->internal_fault_saved_buffer;
			this->internal_fault_saved_buffer = nullptr;

			buffer = format_to_fixed_z::format_to(buffer, last,
					"\nSomething went seriously wrong when attempting to fill the '{}' section of the crash log: exception: {:08X}.\n"
					"This is probably due to an invalid pointer or other corrupt data.\n\n",
					section_name, GetExceptionCode());
		}

		this->internal_fault_saved_buffer = nullptr;
		return buffer;
	}
#else /* _MSC_VER */
	/**
	 * Set up a longjmp to be called from the vectored exception handler for the case where we trigger another exception
	 * during the course of calling the given log section writer.
	 *
	 * If an exception does occur, restore the buffer pointer to either the original value, or
	 * the value provided in any later checkpoint.
	 * Insert a message describing the problem and give up on the section.
	 */
	/* virtual */ char *CrashLogWindows::TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer)
	{
		this->FlushCrashLogBuffer(buffer);
		this->internal_fault_saved_buffer = buffer;

		int exception_num = setjmp(this->internal_fault_jmp_buf);
		if (exception_num != 0) {
			if (this->internal_fault_saved_buffer == nullptr) {
				/* if we get here, things are unrecoverable */
				ImmediateExitProcess(43);
			}

			buffer = this->internal_fault_saved_buffer;
			this->internal_fault_saved_buffer = nullptr;

			buffer = format_to_fixed_z::format_to(buffer, last,
					"\nSomething went seriously wrong when attempting to fill the '{}' section of the crash log: exception: {:08X}.\n"
					"This is probably due to an invalid pointer or other corrupt data.\n\n",
					section_name, (DWORD)exception_num);

			return buffer;
		}

		format_to_fixed buf(buffer, last - buffer);
		writer(this, buf);
		buffer += buf.size();

		this->internal_fault_saved_buffer = nullptr;
		return buffer;
	}
#endif /* _MSC_VER */

	/* virtual */ void CrashLogWindows::CrashLogFaultSectionCheckpoint(format_target &buffer) const
	{
		CrashLogWindows *self = const_cast<CrashLogWindows *>(this);

		if (self->internal_fault_saved_buffer == nullptr) return;

		char *b = buffer.end();
		if (b > self->internal_fault_saved_buffer) {
			self->internal_fault_saved_buffer = b;
			self->FlushCrashLogBuffer(b);
		}
	}

extern bool CloseConsoleLogIfActive();
static void ShowCrashlogWindow();

/**
 * Stack pointer for use when 'starting' the crash handler.
 * Not static as gcc's inline assembly needs it that way.
 */
thread_local void *_safe_esp = nullptr;

static LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS *ep)
{
	/* Restore system timer resolution. */
	timeEndPeriod(1);

	/* Disable our event loop. */
	SetWindowLongPtr(GetActiveWindow(), GWLP_WNDPROC, (LONG_PTR)&DefWindowProc);

	CrashLogWindows *log = nullptr;
	CrashLogWindows *cur = CrashLogWindows::current.load();
	do {
		if (cur != nullptr) {
			if (cur->crash_thread_id == GetCurrentThreadId()) {
				/* The same thread has recursively reached the exception handler */
				CrashLog::AfterCrashLogCleanup();
				ImmediateExitProcess(2);
			} else {
				/* Another thread has also reached the exception handler, just pause/suspend it */
				cur->other_crash_threads++;
				while (true) {
					Sleep(INFINITE);
				}
			}
			return EXCEPTION_EXECUTE_HANDLER;
		} else if (log == nullptr) {
			/* Use VirtualAlloc to allocate the buffer for the crash log object and text buffer.
			 * It is too large for the stack, and the crash may have been caused by heap corruption.
			 * Make the crash log text buffer at least 4 x 64k, round allocation up to multiple of 64k. */
			const size_t alloc_size = Align(sizeof(CrashLogWindows) + 0x40000, 0x10000);
			void *raw_buffer = VirtualAlloc(nullptr, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
			log = new (raw_buffer) CrashLogWindows(ep);
			log->crashlog_buffer = std::span<char>(reinterpret_cast<char *>(raw_buffer) + sizeof(CrashLogWindows), reinterpret_cast<char *>(raw_buffer) + alloc_size);
		}
	} while (!CrashLogWindows::current.compare_exchange_weak(cur, log));

	const char *abort_reason = CrashLog::GetAbortCrashlogReason();
	if (abort_reason != nullptr) {
		wchar_t _emergency_crash[512];
		_snwprintf(_emergency_crash, lengthof(_emergency_crash),
				L"A serious fault condition occurred in the game. The game will shut down. (%s)\n", OTTD2FS(abort_reason).c_str());
		MessageBox(nullptr, _emergency_crash, L"Fatal Application Failure", MB_ICONERROR);
		ImmediateExitProcess(3);
	}

	log->MakeCrashLog(log->crashlog_buffer.data(), log->crashlog_buffer.data() + log->crashlog_buffer.size() - 1);

	/* Close any possible log files */
	CloseConsoleLogIfActive();

	void *crash_win_esp = _safe_esp;
	if (crash_win_esp == nullptr) {
		/* If _safe_esp is not set for the current thread,
		 * try to read the stack base from the thread environment block instead. */
#ifdef _M_AMD64
		/* The stack pointer for AMD64 must always be 16-byte aligned inside a
		 * function. As we are simulating a function call with the safe ESP value,
		 * we need to subtract 8 for the imaginary return address otherwise stack
		 * alignment would be wrong in the called function. */
		crash_win_esp = (void *)(__readgsqword(8) - 8);
#elif defined(_M_IX86)
		crash_win_esp = (void *)__readfsdword(4);
#endif
	}

	if ((VideoDriver::GetInstance() == nullptr || VideoDriver::GetInstance()->HasGUI()) && crash_win_esp != nullptr) {
#ifdef _M_AMD64
		ep->ContextRecord->Rip = (DWORD64)ShowCrashlogWindow;
		ep->ContextRecord->Rsp = (DWORD64)crash_win_esp;
#elif defined(_M_IX86)
		ep->ContextRecord->Eip = (DWORD)ShowCrashlogWindow;
		ep->ContextRecord->Esp = (DWORD)crash_win_esp;
#elif defined(_M_ARM64)
		ep->ContextRecord->Pc = (DWORD64)ShowCrashlogWindow;
		ep->ContextRecord->Sp = (DWORD64)crash_win_esp;
#endif
		return EXCEPTION_CONTINUE_EXECUTION;
	}

	CrashLog::AfterCrashLogCleanup();
	ImmediateExitProcess(1);
	return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS *ep)
{
	CrashLogWindows *cur = CrashLogWindows::current.load();
	if (cur != nullptr && cur->crash_thread_id == GetCurrentThreadId() && cur->internal_fault_saved_buffer != nullptr) {
#if defined(_MSC_VER)
		return EXCEPTION_CONTINUE_SEARCH;
#else
		longjmp(cur->internal_fault_jmp_buf, ep->ExceptionRecord->ExceptionCode);
#endif
	}

	if (ep->ExceptionRecord->ExceptionCode == 0xC0000374 /* heap corruption */) {
		return ExceptionHandler(ep);
	}
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
		return ExceptionHandler(ep);
	}
	if (ep->ExceptionRecord->ExceptionCode == 0xE1212012) {
		return ExceptionHandler(ep);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void CDECL CustomAbort(int)
{
	RaiseException(0xE1212012, 0, 0, nullptr);
}

/* static */ void CrashLog::InitialiseCrashLog()
{
	CrashLogWindowsInitThread();

	/* SIGABRT is not an unhandled exception, so we need to intercept it. */
	signal(SIGABRT, CustomAbort);
#if defined(_MSC_VER)
	/* Don't show abort message as we will get the crashlog window anyway. */
	_set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif
	SetUnhandledExceptionFilter(ExceptionHandler);

	using VEX_HANDLER_TYPE = LONG WINAPI (EXCEPTION_POINTERS *);
	void* (WINAPI *AddVectoredExceptionHandler)(ULONG, VEX_HANDLER_TYPE*);
	static LibraryLoader _kernel32("Kernel32.dll");
	AddVectoredExceptionHandler = _kernel32.GetFunction("AddVectoredExceptionHandler");
	if (AddVectoredExceptionHandler != nullptr) {
		AddVectoredExceptionHandler(1, VectoredExceptionHandler);
	}
}

/**
 * Prepare crash log handler for a newly started thread.
 */
void CrashLogWindowsInitThread()
{
#if defined(_M_AMD64) || defined(_M_ARM64)
	CONTEXT ctx;
	RtlCaptureContext(&ctx);

	/* The stack pointer for AMD64 must always be 16-byte aligned inside a
	 * function. As we are simulating a function call with the safe ESP value,
	 * we need to subtract 8 for the imaginary return address otherwise stack
	 * alignment would be wrong in the called function. */
#	if defined(_M_ARM64)
	_safe_esp = (void *)(ctx.Sp - 8);
#	else
	_safe_esp = (void *)(ctx.Rsp - 8);
#	endif
#else
	void *safe_esp = nullptr;
#	if defined(_MSC_VER)
	_asm {
		mov safe_esp, esp
	}
#	else
	asm("movl %%esp, %0" : "=rm" (safe_esp));
#	endif
	_safe_esp = safe_esp;
#endif
}

/* static */ void CrashLog::DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info)
{
	CrashLogWindows log(nullptr);
	log.MakeDesyncCrashLog(log_in, log_out, info);
}

/* static */ void CrashLog::InconsistencyLog(const InconsistencyExtraInfo &info)
{
	CrashLogWindows log(nullptr);
	log.MakeInconsistencyLog(info);
}

/* static */ void CrashLog::VersionInfoLog(format_target &buffer)
{
	CrashLogWindows log(nullptr);
	log.FillVersionInfoLog(buffer);
}

/* The crash log GUI */

static bool _expanded;

static const TCHAR _crash_desc[] =
	L"A serious fault condition occurred in the game. The game will shut down.\n"
	L"Please send the crash information (log files and crash saves, if any) to the patchpack developer.\n"
	L"This will greatly help debugging. The correct place to do this is https://www.tt-forums.net/viewtopic.php?f=33&t=73469"
	L" or https://github.com/JGRennison/OpenTTD-patches\n"
	L"The information contained in the report is displayed below.\n";

static const wchar_t * const _expand_texts[] = {L"S&how report >>", L"&Hide report <<" };

static void SetWndSize(HWND wnd, int mode)
{
	RECT r, r2;

	GetWindowRect(wnd, &r);
	SetDlgItemText(wnd, 15, _expand_texts[mode == 1]);

	if (mode >= 0) {
		GetWindowRect(GetDlgItem(wnd, 11), &r2);
		int offs = r2.bottom - r2.top + 10;
		if (mode == 0) offs = -offs;
		SetWindowPos(wnd, HWND_TOPMOST, 0, 0,
			r.right - r.left, r.bottom - r.top + offs, SWP_NOMOVE | SWP_NOZORDER);
	} else {
		SetWindowPos(wnd, HWND_TOPMOST,
			(GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
			(GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
			0, 0, SWP_NOSIZE);
	}
}

static INT_PTR CALLBACK CrashDialogFunc(HWND wnd, UINT msg, WPARAM wParam, LPARAM)
{
	switch (msg) {
		case WM_INITDIALOG: {
			uint crashlog_length = 0;
			CrashLogWindows *cur = CrashLogWindows::current.load();
			for (const char *p = cur->crashlog_buffer.data(); *p != 0; p++) {
				if (*p == '\n') {
					/* Reserve extra space for LF to CRLF conversion */
					crashlog_length++;
				}
				crashlog_length++;
			}

			/* We need to put the crash-log in a separate buffer because the default
			 * buffer in MB_TO_WIDE is not large enough (512 chars).
			 * Use VirtualAlloc to allocate pages for the buffer to avoid overflowing the stack,
			 * due to the increased maximum size of the crash log.
			 * Avoid the heap in case the crash is because the heap became corrupted. */
			const size_t crash_desc_buf_length = lengthof(_crash_desc) + (MAX_PATH * 4); // Add an extra MAX_PATH for additional space
			const size_t crash_msgW_length = ((crashlog_length + 16) * 3) / 2;
			const size_t dos_nl_length = (crashlog_length + 16);
			void *raw_buffer = VirtualAlloc(nullptr, (crash_desc_buf_length * sizeof(wchar_t)) + (crash_msgW_length * sizeof(wchar_t)) + dos_nl_length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

			wchar_t *crash_desc_buf = reinterpret_cast<wchar_t *>(raw_buffer);
			wchar_t *crash_msgW = crash_desc_buf + crash_desc_buf_length;
			char *dos_nl = reinterpret_cast<char *>(crash_msgW + crash_msgW_length);

			/* Convert unix -> dos newlines because the edit box only supports that properly :( */
			const char *unix_nl = cur->crashlog_buffer.data();
			char *p = dos_nl;
			char32_t c;
			while ((c = Utf8Consume(&unix_nl)) && p < (dos_nl + dos_nl_length - 1) - 4) { // 4 is max number of bytes per character
				if (c == '\n') p += Utf8Encode(p, '\r');
				p += Utf8Encode(p, c);
			}
			*p = '\0';

			/* Add path to all files to the crash window text */
			const wchar_t * const crash_desc_buf_last = crash_desc_buf + crash_desc_buf_length - 1;
			wcsncpy_s(crash_desc_buf, crash_desc_buf_length, _crash_desc, _TRUNCATE);
			wchar_t *desc = crash_desc_buf + wcslen(crash_desc_buf);

			auto append_str = [&](std::string_view name) {
				if (desc >= crash_desc_buf_last - 1) return;
				desc += MultiByteToWideChar(CP_UTF8, 0, name.data(), (int)name.size(), desc, (int)(crash_desc_buf_last - desc));
				*desc = L'\0';
			};
			auto append_newline = [&]() {
				if (desc >= crash_desc_buf_last - 1) return;
				*desc = L'\n';
				desc++;
				*desc = L'\0';
			};

			append_str(cur->crashlog_filename);
			if (_settings_client.gui.developer > 0 && cur->crashdump_filename[0] != 0) {
				append_newline();
				append_str(cur->crashdump_filename);
			}
			if (cur->savegame_filename[0] != 0) {
				append_newline();
				append_str(cur->savegame_filename);
			}
			if (cur->screenshot_filename[0] != 0) {
				append_newline();
				append_str(cur->screenshot_filename);
			}

			SetDlgItemText(wnd, 10, crash_desc_buf);
			SetDlgItemText(wnd, 11, convert_to_fs(dos_nl, {crash_msgW, crash_msgW_length}));
			SendDlgItemMessage(wnd, 11, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), FALSE);
			SetWndSize(wnd, -1);
		} return TRUE;
		case WM_COMMAND:
			switch (wParam) {
				case 12: // Close
					CrashLog::AfterCrashLogCleanup();
					ImmediateExitProcess(2);
				case 15: // Expand window to show crash-message
					_expanded = !_expanded;
					SetWndSize(wnd, _expanded);
					break;
			}
			return TRUE;
		case WM_CLOSE:
			CrashLog::AfterCrashLogCleanup();
			ImmediateExitProcess(2);
	}

	return FALSE;
}

static void ShowCrashlogWindow()
{
	ShowCursor(TRUE);
	ShowWindow(GetActiveWindow(), FALSE);
	DialogBox(GetModuleHandle(nullptr), MAKEINTRESOURCE(100), nullptr, CrashDialogFunc);
}
