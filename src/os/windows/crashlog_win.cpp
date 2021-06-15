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
#include "../../core/math_func.hpp"
#include "../../string_func.h"
#include "../../fileio_func.h"
#include "../../strings_func.h"
#include "../../gamelog.h"
#include "../../saveload/saveload.h"
#include "../../video/video_driver.hpp"
#include "../../screenshot.h"
#include "../../debug.h"
#include "../../settings_type.h"
#include "../../thread.h"
#if defined(WITH_DEMANGLE)
#include <cxxabi.h>
#endif

#include <windows.h>
#include <mmsystem.h>
#include <signal.h>

#include "../../safeguards.h"

/* printf format specification for 32/64-bit addresses. */
#ifdef _M_AMD64
#define PRINTF_PTR "0x%016IX"
#define PRINTF_LOC "%.16IX"
#else
#define PRINTF_PTR "0x%08X"
#define PRINTF_LOC "%.8IX"
#endif

/**
 * Windows implementation for the crash logger.
 */
class CrashLogWindows : public CrashLog {
	/** Information about the encountered exception */
	EXCEPTION_POINTERS *ep;

	char *LogOSVersion(char *buffer, const char *last) const override;
	char *LogError(char *buffer, const char *last, const char *message) const override;
	char *LogStacktrace(char *buffer, const char *last) const override;
	char *LogRegisters(char *buffer, const char *last) const override;
	char *LogModules(char *buffer, const char *last) const override;
public:
#if defined(_MSC_VER) || defined(WITH_DBGHELP)
	int WriteCrashDump(char *filename, const char *filename_last) const override;
	char *AppendDecodedStacktrace(char *buffer, const char *last) const;
#else
	char *AppendDecodedStacktrace(char *buffer, const char *last) const { return buffer; }
#endif /* _MSC_VER || WITH_DBGHELP */


	/** Buffer for the generated crash log */
	char crashlog[65536 * 4];
	/** Buffer for the filename of the crash log */
	char crashlog_filename[MAX_PATH];
	/** Buffer for the filename of the crash dump */
	char crashdump_filename[MAX_PATH];
	/** Buffer for the filename of the crash screenshot */
	char screenshot_filename[MAX_PATH];

	/**
	 * A crash log is always generated when it's generated.
	 * @param ep the data related to the exception.
	 */
	CrashLogWindows(EXCEPTION_POINTERS *ep = nullptr) :
		ep(ep)
	{
		this->crashlog[0] = '\0';
		this->crashlog_filename[0] = '\0';
		this->crashdump_filename[0] = '\0';
		this->screenshot_filename[0] = '\0';
		this->name_buffer[0] = '\0';
	}

	/**
	 * Points to the current crash log.
	 */
	static CrashLogWindows *current;
};

/* static */ CrashLogWindows *CrashLogWindows::current = nullptr;

/* virtual */ char *CrashLogWindows::LogOSVersion(char *buffer, const char *last) const
{
	_OSVERSIONINFOA os;
	os.dwOSVersionInfoSize = sizeof(os);
	GetVersionExA(&os);

	return buffer + seprintf(buffer, last,
			"Operating system:\n"
			" Name:     Windows\n"
			" Release:  %d.%d.%d (%s)\n",
			(int)os.dwMajorVersion,
			(int)os.dwMinorVersion,
			(int)os.dwBuildNumber,
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

/* virtual */ char *CrashLogWindows::LogError(char *buffer, const char *last, const char *message) const
{
	buffer += seprintf(buffer, last, "Crash reason:\n");
	for (auto record = ep->ExceptionRecord; record != nullptr; record = record->ExceptionRecord) {
		buffer += seprintf(buffer, last,
				" Exception:  %.8X\n"
				" Location:   " PRINTF_LOC "\n",
				(int)record->ExceptionCode,
				(size_t)record->ExceptionAddress
		);
		if (record->ExceptionCode == 0xC0000005 && record->NumberParameters == 2) {
			buffer += seprintf(buffer, last,
					" Fault type: %u (%s)\n"
					" Fault addr: " PRINTF_LOC "\n",
					(uint) record->ExceptionInformation[0],
					GetAccessViolationTypeString(record->ExceptionInformation[0]),
					record->ExceptionInformation[1]
			);
		} else {
			for (uint i = 0; i < (uint) record->NumberParameters; i++) {
				buffer += seprintf(buffer, last,
						" Info %u:     " PRINTF_LOC "\n",
						i,
						record->ExceptionInformation[i]
				);
			}
		}
	}
	buffer += seprintf(buffer, last, " Message:    %s\n\n",
			message == nullptr ? "<none>" : message);
	return buffer;
}

struct DebugFileInfo {
	uint32 size;
	uint32 crc32;
	SYSTEMTIME file_time;
};

static uint32 *_crc_table;

static void MakeCRCTable(uint32 *table)
{
	uint32 crc, poly = 0xEDB88320L;
	int i;
	int j;

	_crc_table = table;

	for (i = 0; i != 256; i++) {
		crc = i;
		for (j = 8; j != 0; j--) {
			crc = (crc & 1 ? (crc >> 1) ^ poly : crc >> 1);
		}
		table[i] = crc;
	}
}

static uint32 CalcCRC(byte *data, uint size, uint32 crc)
{
	for (; size > 0; size--) {
		crc = ((crc >> 8) & 0x00FFFFFF) ^ _crc_table[(crc ^ *data++) & 0xFF];
	}
	return crc;
}

static void GetFileInfo(DebugFileInfo *dfi, const wchar_t *filename)
{
	HANDLE file;
	memset(dfi, 0, sizeof(*dfi));

	file = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, 0);
	if (file != INVALID_HANDLE_VALUE) {
		byte buffer[1024];
		DWORD numread;
		uint32 filesize = 0;
		FILETIME write_time;
		uint32 crc = (uint32)-1;

		for (;;) {
			if (ReadFile(file, buffer, sizeof(buffer), &numread, nullptr) == 0 || numread == 0) {
				break;
			}
			filesize += numread;
			crc = CalcCRC(buffer, numread, crc);
		}
		dfi->size = filesize;
		dfi->crc32 = crc ^ (uint32)-1;

		if (GetFileTime(file, nullptr, nullptr, &write_time)) {
			FileTimeToSystemTime(&write_time, &dfi->file_time);
		}
		CloseHandle(file);
	}
}


static char *PrintModuleInfo(char *output, const char *last, HMODULE mod)
{
	wchar_t buffer[MAX_PATH];
	DebugFileInfo dfi;

	GetModuleFileName(mod, buffer, MAX_PATH);
	GetFileInfo(&dfi, buffer);
	output += seprintf(output, last, " %-20s handle: %p size: %d crc: %.8X date: %d-%.2d-%.2d %.2d:%.2d:%.2d\n",
		FS2OTTD(buffer).c_str(),
		mod,
		dfi.size,
		dfi.crc32,
		dfi.file_time.wYear,
		dfi.file_time.wMonth,
		dfi.file_time.wDay,
		dfi.file_time.wHour,
		dfi.file_time.wMinute,
		dfi.file_time.wSecond
	);
	return output;
}

/* virtual */ char *CrashLogWindows::LogModules(char *output, const char *last) const
{
	MakeCRCTable(AllocaM(uint32, 256));
	BOOL (WINAPI *EnumProcessModules)(HANDLE, HMODULE*, DWORD, LPDWORD);

	output += seprintf(output, last, "Module information:\n");

	if (LoadLibraryList((Function*)&EnumProcessModules, "psapi.dll\0EnumProcessModules\0\0")) {
		HMODULE modules[100];
		DWORD needed;
		BOOL res;

		HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
		if (proc != nullptr) {
			res = EnumProcessModules(proc, modules, sizeof(modules), &needed);
			CloseHandle(proc);
			if (res) {
				size_t count = std::min<DWORD>(needed / sizeof(HMODULE), lengthof(modules));

				for (size_t i = 0; i != count; i++) output = PrintModuleInfo(output, last, modules[i]);
				return output + seprintf(output, last, "\n");
			}
		}
	}
	output = PrintModuleInfo(output, last, nullptr);
	return output + seprintf(output, last, "\n");
}

/* virtual */ char *CrashLogWindows::LogRegisters(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last, "Registers:\n");
#ifdef _M_AMD64
	buffer += seprintf(buffer, last,
		" RAX: %.16I64X RBX: %.16I64X RCX: %.16I64X RDX: %.16I64X\n"
		" RSI: %.16I64X RDI: %.16I64X RBP: %.16I64X RSP: %.16I64X\n"
		" R8:  %.16I64X R9:  %.16I64X R10: %.16I64X R11: %.16I64X\n"
		" R12: %.16I64X R13: %.16I64X R14: %.16I64X R15: %.16I64X\n"
		" RIP: %.16I64X EFLAGS: %.8lX\n",
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
	buffer += seprintf(buffer, last,
		" EAX: %.8X EBX: %.8X ECX: %.8X EDX: %.8X\n"
		" ESI: %.8X EDI: %.8X EBP: %.8X ESP: %.8X\n"
		" EIP: %.8X EFLAGS: %.8X\n",
		(int)ep->ContextRecord->Eax,
		(int)ep->ContextRecord->Ebx,
		(int)ep->ContextRecord->Ecx,
		(int)ep->ContextRecord->Edx,
		(int)ep->ContextRecord->Esi,
		(int)ep->ContextRecord->Edi,
		(int)ep->ContextRecord->Ebp,
		(int)ep->ContextRecord->Esp,
		(int)ep->ContextRecord->Eip,
		(int)ep->ContextRecord->EFlags
	);
#elif defined(_M_ARM64)
	buffer += seprintf(buffer, last,
		" X0:  %.16I64X X1:  %.16I64X X2:  %.16I64X X3:  %.16I64X\n"
		" X4:  %.16I64X X5:  %.16I64X X6:  %.16I64X X7:  %.16I64X\n"
		" X8:  %.16I64X X9:  %.16I64X X10: %.16I64X X11: %.16I64X\n"
		" X12: %.16I64X X13: %.16I64X X14: %.16I64X X15: %.16I64X\n"
		" X16: %.16I64X X17: %.16I64X X18: %.16I64X X19: %.16I64X\n"
		" X20: %.16I64X X21: %.16I64X X22: %.16I64X X23: %.16I64X\n"
		" X24: %.16I64X X25: %.16I64X X26: %.16I64X X27: %.16I64X\n"
		" X28: %.16I64X Fp:  %.16I64X Lr:  %.16I64X\n",
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

	buffer += seprintf(buffer, last, "\n Bytes at instruction pointer:\n");
#ifdef _M_AMD64
	byte *b = (byte*)ep->ContextRecord->Rip;
#elif defined(_M_IX86)
	byte *b = (byte*)ep->ContextRecord->Eip;
#elif defined(_M_ARM64)
	byte *b = (byte*)ep->ContextRecord->Pc;
#endif
	for (int i = 0; i != 24; i++) {
		if (IsBadReadPtr(b, 1)) {
			buffer += seprintf(buffer, last, " ??"); // OCR: WAS: , 0);
		} else {
			buffer += seprintf(buffer, last, " %.2X", *b);
		}
		b++;
	}
	return buffer + seprintf(buffer, last, "\n\n");
}

/* virtual */ char *CrashLogWindows::LogStacktrace(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last, "Stack trace:\n");
#ifdef _M_AMD64
	uint32 *b = (uint32*)ep->ContextRecord->Rsp;
#elif defined(_M_IX86)
	uint32 *b = (uint32*)ep->ContextRecord->Esp;
#elif defined(_M_ARM64)
	uint32 *b = (uint32*)ep->ContextRecord->Sp;
#endif
	for (int j = 0; j != 24; j++) {
		for (int i = 0; i != 8; i++) {
			if (IsBadReadPtr(b, sizeof(uint32))) {
				buffer += seprintf(buffer, last, " ????????"); // OCR: WAS - , 0);
			} else {
				buffer += seprintf(buffer, last, " %.8X", *b);
			}
			b++;
		}
		buffer += seprintf(buffer, last, "\n");
	}
	return buffer + seprintf(buffer, last, "\n");
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

char *CrashLogWindows::AppendDecodedStacktrace(char *buffer, const char *last) const
{
#define M(x) x "\0"
	static const char dbg_import[] =
		M("dbghelp.dll")
		M("SymInitialize")
		M("SymSetOptions")
		M("SymCleanup")
		M("StackWalk64")
		M("SymFunctionTableAccess64")
		M("SymGetModuleBase64")
		M("SymGetModuleInfo64")
		M("SymGetSymFromAddr64")
		M("SymGetLineFromAddr64")
		M("")
		;
#undef M

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
	} proc;

	buffer += seprintf(buffer, last, "\nDecoded stack trace:\n");

	/* Try to load the functions from the DLL, if that fails because of a too old dbghelp.dll, just skip it. */
	if (LoadLibraryList((Function*)&proc, dbg_import)) {
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

		/* Allocate space for symbol info. */
		IMAGEHLP_SYMBOL64 *sym_info = (IMAGEHLP_SYMBOL64*)alloca(sizeof(IMAGEHLP_SYMBOL64) + MAX_SYMBOL_LEN - 1);
		sym_info->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		sym_info->MaxNameLength = MAX_SYMBOL_LEN;

		/* Walk stack at most MAX_FRAMES deep in case the stack is corrupt. */
		for (uint num = 0; num < MAX_FRAMES; num++) {
			if (!proc.pStackWalk64(
#ifdef _M_AMD64
				IMAGE_FILE_MACHINE_AMD64,
#else
				IMAGE_FILE_MACHINE_I386,
#endif
				hCur, GetCurrentThread(), &frame, &ctx, nullptr, proc.pSymFunctionTableAccess64, proc.pSymGetModuleBase64, nullptr)) break;

			if (frame.AddrPC.Offset == frame.AddrReturn.Offset) {
				buffer += seprintf(buffer, last, " <infinite loop>\n");
				break;
			}

			/* Get module name. */
			const char *mod_name = "???";
			const char *image_name = nullptr;

			IMAGEHLP_MODULE64 module;
			module.SizeOfStruct = sizeof(module);
			if (proc.pSymGetModuleInfo64(hCur, frame.AddrPC.Offset, &module)) {
				mod_name = module.ModuleName;
				image_name = module.ImageName;
			}

			/* Print module and instruction pointer. */
			buffer += seprintf(buffer, last, "[%02d] %-20s " PRINTF_PTR, num, mod_name, (uintptr_t) frame.AddrPC.Offset);

			/* Get symbol name and line info if possible. */
			DWORD64 offset;
			if (proc.pSymGetSymFromAddr64(hCur, frame.AddrPC.Offset, &offset, sym_info)) {
				buffer += seprintf(buffer, last, " %s + %I64u", sym_info->Name, offset);

				DWORD line_offs;
				IMAGEHLP_LINE64 line;
				line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
				if (proc.pSymGetLineFromAddr64(hCur, frame.AddrPC.Offset, &line_offs, &line)) {
					buffer += seprintf(buffer, last, " (%s:%u)", line.FileName, (uint) line.LineNumber);
				}
			} else if (image_name != nullptr) {
#if defined (WITH_BFD)
				/* subtract one to get the line before the return address, i.e. the function call line */
				sym_info_bfd bfd_info(static_cast<bfd_vma>(frame.AddrPC.Offset) - 1);
				lookup_addr_bfd(image_name, bfd_info);
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
						buffer += seprintf(buffer, last, " %s", func_name);
					}
#if defined(WITH_DEMANGLE)
					free(demangled);
#endif
					if (symbol_ok && bfd_info.function_addr) {
						buffer += seprintf(buffer, last, " + %I64u", frame.AddrPC.Offset - static_cast<DWORD64>(bfd_info.function_addr));
					}
				}
				if (bfd_info.file_name != nullptr) {
					buffer += seprintf(buffer, last, " (%s:%d)", bfd_info.file_name, bfd_info.line);
				}
#endif
			}
			buffer += seprintf(buffer, last, "\n");
		}

		proc.pSymCleanup(hCur);
	}

	return buffer + seprintf(buffer, last, "\n*** End of additional info ***\n");
}

/* virtual */ int CrashLogWindows::WriteCrashDump(char *filename, const char *filename_last) const
{
	if (_settings_client.gui.developer == 0) return 0;

	int ret = 0;
	HMODULE dbghelp = LoadLibrary(L"dbghelp.dll");
	if (dbghelp != nullptr) {
		typedef BOOL (WINAPI *MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE,
				MINIDUMP_TYPE,
				CONST PMINIDUMP_EXCEPTION_INFORMATION,
				CONST PMINIDUMP_USER_STREAM_INFORMATION,
				CONST PMINIDUMP_CALLBACK_INFORMATION);
		MiniDumpWriteDump_t funcMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress(dbghelp, "MiniDumpWriteDump");
		if (funcMiniDumpWriteDump != nullptr) {
			seprintf(filename, filename_last, "%scrash.dmp", _personal_dir.c_str());
			HANDLE file  = CreateFile(OTTD2FS(filename).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, 0);
			HANDLE proc  = GetCurrentProcess();
			DWORD procid = GetCurrentProcessId();
			MINIDUMP_EXCEPTION_INFORMATION mdei;
			MINIDUMP_USER_STREAM userstream;
			MINIDUMP_USER_STREAM_INFORMATION musi;

			userstream.Type        = LastReservedStream + 1;
			userstream.Buffer      = const_cast<void *>(static_cast<const void*>(this->crashlog));
			userstream.BufferSize  = (ULONG)strlen(this->crashlog) + 1;

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

	if (CrashLogWindows::current != nullptr) {
		CrashLog::AfterCrashLogCleanup();
		ExitProcess(2);
	}

	const char *abort_reason = CrashLog::GetAbortCrashlogReason();
	if (abort_reason != nullptr) {
		wchar_t _emergency_crash[512];
		_snwprintf(_emergency_crash, lengthof(_emergency_crash),
				L"A serious fault condition occurred in the game. The game will shut down. (%s)\n", OTTD2FS(abort_reason).c_str());
		MessageBox(nullptr, _emergency_crash, L"Fatal Application Failure", MB_ICONERROR);
		ExitProcess(3);
	}

	VideoDriver::EmergencyAcquireGameLock(1000, 5);

	CrashLogWindows *log = new CrashLogWindows(ep);
	CrashLogWindows::current = log;
	char *buf = log->FillCrashLog(log->crashlog, lastof(log->crashlog));
	char *name_buffer_date = log->name_buffer + seprintf(log->name_buffer, lastof(log->name_buffer), "crash-");
	time_t cur_time = time(nullptr);
	strftime(name_buffer_date, lastof(log->name_buffer) - name_buffer_date, "%Y%m%dT%H%M%SZ", gmtime(&cur_time));
	log->WriteCrashDump(log->crashdump_filename, lastof(log->crashdump_filename));
	log->AppendDecodedStacktrace(buf, lastof(log->crashlog));
	log->WriteCrashLog(log->crashlog, log->crashlog_filename, lastof(log->crashlog_filename), log->name_buffer);
	SetScreenshotAuxiliaryText("Crash Log", log->crashlog);
	log->WriteScreenshot(log->screenshot_filename, lastof(log->screenshot_filename), log->name_buffer);

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
	return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode == 0xC0000374 /* heap corruption */) {
		return ExceptionHandler(ep);
	}
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
		return ExceptionHandler(ep);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static void CDECL CustomAbort(int signal)
{
	RaiseException(0xE1212012, 0, 0, nullptr);
}

/* static */ void CrashLog::InitialiseCrashLog()
{
	CrashLog::InitThread();

	/* SIGABRT is not an unhandled exception, so we need to intercept it. */
	signal(SIGABRT, CustomAbort);
#if defined(_MSC_VER)
	/* Don't show abort message as we will get the crashlog window anyway. */
	_set_abort_behavior(0, _WRITE_ABORT_MSG);
#endif
	SetUnhandledExceptionFilter(ExceptionHandler);

	using VEX_HANDLER_TYPE = LONG WINAPI (EXCEPTION_POINTERS *);
	void* (WINAPI *AddVectoredExceptionHandler)(ULONG, VEX_HANDLER_TYPE*);
	if (LoadLibraryList((Function*)&AddVectoredExceptionHandler, "kernel32.dll\0AddVectoredExceptionHandler\0\0")) {
		AddVectoredExceptionHandler(1, VectoredExceptionHandler);
	}
}

/* static */ void CrashLog::InitThread()
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
	asm("movl %%esp, %0" : "=rm" ( safe_esp ));
#	endif
	_safe_esp = safe_esp;
#endif
}

/* static */ void CrashLog::DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info)
{
	CrashLogWindows log(nullptr);
	log.MakeDesyncCrashLog(log_in, log_out, info);
}

/* static */ void CrashLog::VersionInfoLog()
{
	CrashLogWindows log(nullptr);
	log.MakeVersionInfoLog();
}

/* The crash log GUI */

static bool _expanded;

static const TCHAR _crash_desc[] =
	L"A serious fault condition occurred in the game. The game will shut down.\n"
	L"Please send the crash information (log files and crash saves, if any) to the patchpack developer.\n"
	L"This will greatly help debugging. The correct place to do this is https://www.tt-forums.net/viewtopic.php?f=33&t=73469"
	L" or https://github.com/JGRennison/OpenTTD-patches\n"
	L"The information contained in the report is displayed below.\n"
	L"Press \"Emergency save\" to attempt saving the game. Generated file(s):\n"
	L"%s";

static const wchar_t _save_succeeded[] =
	L"Emergency save succeeded.\nIts location is '%s'.\n"
	L"Be aware that critical parts of the internal game state may have become "
	L"corrupted. The saved game is not guaranteed to work.";

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

static INT_PTR CALLBACK CrashDialogFunc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
		case WM_INITDIALOG: {
			/* We need to put the crash-log in a separate buffer because the default
			 * buffer in MB_TO_WIDE is not large enough (512 chars) */
			wchar_t filenamebuf[MAX_PATH * 2];
			wchar_t crash_msgW[lengthof(CrashLogWindows::current->crashlog)];
			/* Convert unix -> dos newlines because the edit box only supports that properly :( */
			const char *unix_nl = CrashLogWindows::current->crashlog;
			char dos_nl[lengthof(CrashLogWindows::current->crashlog)];
			char *p = dos_nl;
			WChar c;
			while ((c = Utf8Consume(&unix_nl)) && p < lastof(dos_nl) - 4) { // 4 is max number of bytes per character
				if (c == '\n') p += Utf8Encode(p, '\r');
				p += Utf8Encode(p, c);
			}
			*p = '\0';

			/* Add path to crash.log and crash.dmp (if any) to the crash window text */
			size_t len = wcslen(_crash_desc) + 2;
			len += wcslen(convert_to_fs(CrashLogWindows::current->crashlog_filename, filenamebuf, lengthof(filenamebuf))) + 2;
			len += wcslen(convert_to_fs(CrashLogWindows::current->crashdump_filename, filenamebuf, lengthof(filenamebuf))) + 2;
			len += wcslen(convert_to_fs(CrashLogWindows::current->screenshot_filename, filenamebuf, lengthof(filenamebuf))) + 1;

			wchar_t *text = AllocaM(wchar_t, len);
			int printed = _snwprintf(text, len, _crash_desc, convert_to_fs(CrashLogWindows::current->crashlog_filename, filenamebuf, lengthof(filenamebuf)));
			if (printed < 0 || (size_t)printed > len) {
				MessageBox(wnd, L"Catastrophic failure trying to display crash message. Could not perform text formatting.", L"OpenTTD", MB_ICONERROR);
				return FALSE;
			}
			if (_settings_client.gui.developer > 0 && convert_to_fs(CrashLogWindows::current->crashdump_filename, filenamebuf, lengthof(filenamebuf))[0] != L'\0') {
				wcscat(text, L"\n");
				wcscat(text, filenamebuf);
			}
			if (convert_to_fs(CrashLogWindows::current->screenshot_filename, filenamebuf, lengthof(filenamebuf))[0] != L'\0') {
				wcscat(text, L"\n");
				wcscat(text, filenamebuf);
			}

			SetDlgItemText(wnd, 10, text);
			SetDlgItemText(wnd, 11, convert_to_fs(dos_nl, crash_msgW, lengthof(crash_msgW)));
			SendDlgItemMessage(wnd, 11, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), FALSE);
			SetWndSize(wnd, -1);
		} return TRUE;
		case WM_COMMAND:
			switch (wParam) {
				case 12: // Close
					CrashLog::AfterCrashLogCleanup();
					ExitProcess(2);
				case 13: // Emergency save
					_savegame_DBGL_data = CrashLogWindows::current->crashlog;
					_save_DBGC_data = true;
					wchar_t filenamebuf[MAX_PATH * 2];
					char filename[MAX_PATH];
					if (CrashLogWindows::current->WriteSavegame(filename, lastof(filename), CrashLogWindows::current->name_buffer)) {
						convert_to_fs(filename, filenamebuf, lengthof(filenamebuf));
						size_t len = lengthof(_save_succeeded) + wcslen(filenamebuf) + 1;
						wchar_t *text = AllocaM(wchar_t, len);
						_snwprintf(text, len, _save_succeeded, filenamebuf);
						MessageBox(wnd, text, L"Save successful", MB_ICONINFORMATION);
					} else {
						MessageBox(wnd, L"Save failed", L"Save failed", MB_ICONINFORMATION);
					}
					_savegame_DBGL_data = nullptr;
					_save_DBGC_data = false;
					break;
				case 15: // Expand window to show crash-message
					_expanded = !_expanded;
					SetWndSize(wnd, _expanded);
					break;
			}
			return TRUE;
		case WM_CLOSE:
			CrashLog::AfterCrashLogCleanup();
			ExitProcess(2);
	}

	return FALSE;
}

static void ShowCrashlogWindow()
{
	ShowCursor(TRUE);
	ShowWindow(GetActiveWindow(), FALSE);
	DialogBox(GetModuleHandle(nullptr), MAKEINTRESOURCE(100), nullptr, CrashDialogFunc);
}
