/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog_osx.cpp OS X crash log handler */

#include "../../stdafx.h"
#include "../../crashlog.h"
#include "../../string_func.h"
#include "../../gamelog.h"
#include "../../saveload/saveload.h"
#include "../../thread.h"
#include "../../screenshot.h"
#include "../../debug.h"
#include "../../video/video_driver.hpp"
#include "macos.h"

#include <errno.h>
#include <signal.h>
#include <mach-o/arch.h>
#include <dlfcn.h>
#include <cxxabi.h>
#ifdef WITH_UCONTEXT
#include <sys/ucontext.h>
#endif

#include "../../safeguards.h"


/* Macro testing a stack address for valid alignment. */
#if defined(__i386__)
#define IS_ALIGNED(addr) (((uintptr_t)(addr) & 0xf) == 8)
#else
#define IS_ALIGNED(addr) (((uintptr_t)(addr) & 0xf) == 0)
#endif

/* printf format specification for 32/64-bit addresses. */
#ifdef __LP64__
#define PRINTF_PTR "0x%016lx"
#else
#define PRINTF_PTR "0x%08lx"
#endif

#define MAX_STACK_FRAMES 64

#if !defined(WITHOUT_DBG_LLDB)
static bool ExecReadStdout(const char *file, char *const *args, char *&buffer, const char *last)
{
	int pipefd[2];
	if (pipe(pipefd) == -1) return false;

	int pid = fork();
	if (pid < 0) return false;

	if (pid == 0) {
		/* child */

		close(pipefd[0]); /* Close unused read end */
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		int null_fd = open("/dev/null", O_RDWR);
		if (null_fd != -1) {
			dup2(null_fd, STDERR_FILENO);
			dup2(null_fd, STDIN_FILENO);
		}

		execvp(file, args);
		exit(42);
	}

	/* parent */

	close(pipefd[1]); /* Close unused write end */

	while (buffer < last) {
		ssize_t res = read(pipefd[0], buffer, last - buffer);
		if (res < 0) {
			if (errno == EINTR) continue;
			break;
		} else if (res == 0) {
			break;
		} else {
			buffer += res;
		}
	}
	buffer += seprintf(buffer, last, "\n");

	close(pipefd[0]); /* close read end */

	int status;
	int wait_ret = waitpid(pid, &status, 0);
	if (wait_ret == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		/* command did not appear to run successfully */
		return false;
	} else {
		/* command executed successfully */
		return true;
	}
}
#endif /* !WITHOUT_DBG_LLDB */

/**
 * OSX implementation for the crash logger.
 */
class CrashLogOSX : public CrashLog {
	/** Signal that has been thrown. */
	int signum;
	siginfo_t *si;
	void *context;
	bool signal_instruction_ptr_valid;
	void *signal_instruction_ptr;

	char filename_log[MAX_PATH];        ///< Path of crash.log
	char filename_save[MAX_PATH];       ///< Path of crash.sav
	char filename_screenshot[MAX_PATH]; ///< Path of crash.(png|bmp|pcx)

	char *LogOSVersion(char *buffer, const char *last) const override
	{
		int ver_maj, ver_min, ver_bug;
		GetMacOSVersion(&ver_maj, &ver_min, &ver_bug);

		const NXArchInfo *arch = NXGetLocalArchInfo();

		return buffer + seprintf(buffer, last,
				"Operating system:\n"
				" Name:     Mac OS X\n"
				" Release:  %d.%d.%d\n"
				" Machine:  %s\n"
				" Min Ver:  %d\n"
				" Max Ver:  %d\n",
				ver_maj, ver_min, ver_bug,
				arch != nullptr ? arch->description : "unknown",
				MAC_OS_X_VERSION_MIN_REQUIRED,
				MAC_OS_X_VERSION_MAX_ALLOWED
		);
	}

	char *LogError(char *buffer, const char *last, const char *message) const override
	{
		buffer += seprintf(buffer, last,
				"Crash reason:\n"
				" Signal:  %s (%d)\n",
				strsignal(this->signum),
				this->signum
		);
		if (this->si) {
			buffer += seprintf(buffer, last,
					"          si_code: %d\n",
					this->si->si_code);
			if (this->signum != SIGABRT) {
				buffer += seprintf(buffer, last,
						"          Fault address: %p\n",
						this->si->si_addr);
				if (this->signal_instruction_ptr_valid) {
					buffer += seprintf(buffer, last,
							"          Instruction address: %p\n",
							this->signal_instruction_ptr);
				}
			}
		}
		buffer += seprintf(buffer, last,
				" Message: %s\n\n",
				message == nullptr ? "<none>" : message
		);
		return buffer;
	}

	char *LogStacktrace(char *buffer, const char *last) const override
	{
		/* As backtrace() is only implemented in 10.5 or later,
		 * we're rolling our own here. Mostly based on
		 * http://stackoverflow.com/questions/289820/getting-the-current-stack-trace-on-mac-os-x
		 * and some details looked up in the Darwin sources. */
		buffer += seprintf(buffer, last, "\nStacktrace:\n");

		void **frame;
		int i = 0;

		auto print_frame = [&](void *ip) {
			/* Print running index. */
			buffer += seprintf(buffer, last, " [%02d]", i);

			Dl_info dli;
			bool dl_valid = dladdr(ip, &dli) != 0;

			const char *fname = "???";
			if (dl_valid && dli.dli_fname) {
				/* Valid image name? Extract filename from the complete path. */
				const char *s = strrchr(dli.dli_fname, '/');
				if (s != nullptr) {
					fname = s + 1;
				} else {
					fname = dli.dli_fname;
				}
			}
			/* Print image name and IP. */
			buffer += seprintf(buffer, last, " %-20s " PRINTF_PTR, fname, (uintptr_t)ip);

			/* Print function offset if information is available. */
			if (dl_valid && dli.dli_sname != nullptr && dli.dli_saddr != nullptr) {
				/* Try to demangle a possible C++ symbol. */
				int status = -1;
				char *func_name = abi::__cxa_demangle(dli.dli_sname, nullptr, 0, &status);

				long int offset = (intptr_t)ip - (intptr_t)dli.dli_saddr;
				buffer += seprintf(buffer, last, " (%s + %ld)", func_name != nullptr ? func_name : dli.dli_sname, offset);

				free(func_name);
			}
			buffer += seprintf(buffer, last, "\n");
		};

#if defined(__ppc__) || defined(__ppc64__)
		/* Apple says __builtin_frame_address can be broken on PPC. */
		__asm__ volatile("mr %0, r1" : "=r" (frame));
#else
		frame = (void **)__builtin_frame_address(0);
#endif

		for (; frame != nullptr && i < MAX_STACK_FRAMES; i++) {
			/* Get IP for current stack frame. */
#if defined(__ppc__) || defined(__ppc64__)
			void *ip = frame[2];
#else
			void *ip = frame[1];
#endif
			if (ip == nullptr) break;

			print_frame(ip);

			/* Get address of next stack frame. */
			void **next = (void **)frame[0];
			/* Frame address not increasing or not aligned? Broken stack, exit! */
			if (next <= frame || !IS_ALIGNED(next)) break;
			frame = next;
		}

		return buffer + seprintf(buffer, last, "\n");
	}

	/**
	 * Get a stack backtrace of the current thread's stack and other info using the gdb debugger, if available.
	 *
	 * Using LLDB is useful as it knows about inlined functions and locals, and generally can
	 * do a more thorough job than in LogStacktrace.
	 * This is done in addition to LogStacktrace as lldb cannot be assumed to be present
	 * and there is some potentially useful information in the output from LogStacktrace
	 * which is not in lldb's output.
	 */
	char *LogLldbInfo(char *buffer, const char *last) const
	{

#if !defined(WITHOUT_DBG_LLDB)
		pid_t pid = getpid();

		char *buffer_orig = buffer;
		buffer += seprintf(buffer, last, "LLDB info:\n");

		char pid_buffer[16];
		char disasm_buffer[64];

		seprintf(pid_buffer, lastof(pid_buffer), "%d", pid);

		std::vector<const char *> args;
		args.push_back("lldb");
		args.push_back("-x");
		args.push_back("-p");
		args.push_back(pid_buffer);
		args.push_back("--batch");

		args.push_back("-o");
		args.push_back(IsNonMainThread() ? "bt all" : "bt 100");

		if (this->GetMessage() == nullptr && this->signal_instruction_ptr_valid) {
			seprintf(disasm_buffer, lastof(disasm_buffer), "disassemble -b -F intel -c 1 -s %p", this->signal_instruction_ptr);
			args.push_back("-o");
			args.push_back(disasm_buffer);
		}

		args.push_back(nullptr);
		if (!ExecReadStdout("lldb", const_cast<char* const*>(&(args[0])), buffer, last)) {
			buffer = buffer_orig;
		}
#endif /* !WITHOUT_DBG_LLDB */

		return buffer;
	}

	/**
	 * Log LLDB information if available
	 */
	char *LogRegisters(char *buffer, const char *last) const override
	{
		buffer = LogLldbInfo(buffer, last);

#ifdef WITH_UCONTEXT
		ucontext_t *ucontext = static_cast<ucontext_t *>(context);
#if defined(__x86_64__)
		const auto &gregs = ucontext->uc_mcontext->__ss;
		buffer += seprintf(buffer, last,
			"Registers:\n"
			" rax: %#16llx rbx: %#16llx rcx: %#16llx rdx: %#16llx\n"
			" rsi: %#16llx rdi: %#16llx rbp: %#16llx rsp: %#16llx\n"
			" r8:  %#16llx r9:  %#16llx r10: %#16llx r11: %#16llx\n"
			" r12: %#16llx r13: %#16llx r14: %#16llx r15: %#16llx\n"
			" rip: %#16llx rflags: %#8llx\n\n",
			gregs.__rax,
			gregs.__rbx,
			gregs.__rcx,
			gregs.__rdx,
			gregs.__rsi,
			gregs.__rdi,
			gregs.__rbp,
			gregs.__rsp,
			gregs.__r8,
			gregs.__r9,
			gregs.__r10,
			gregs.__r11,
			gregs.__r12,
			gregs.__r13,
			gregs.__r14,
			gregs.__r15,
			gregs.__rip,
			gregs.__rflags
		);
#elif defined(__i386)
		const auto &gregs = ucontext->uc_mcontext->__ss;
		buffer += seprintf(buffer, last,
			"Registers:\n"
			" eax: %#8x ebx: %#8x ecx: %#8x edx: %#8x\n"
			" esi: %#8x edi: %#8x ebp: %#8x esp: %#8x\n"
			" eip: %#8x eflags: %#8x\n\n",
			gregs.__eax,
			gregs.__ebx,
			gregs.__ecx,
			gregs.__edx,
			gregs.__esi,
			gregs.__edi,
			gregs.__ebp,
			gregs.__esp,
			gregs.__eip,
			gregs.__eflags
		);
#endif
#endif

		return buffer;
	}

public:
	struct DesyncTag {};

	/**
	 * A crash log is always generated by signal.
	 * @param signum the signal that was caused by the crash.
	 */
	CrashLogOSX(int signum, siginfo_t *si, void *context) : signum(signum), si(si), context(context)
	{
		filename_log[0] = '\0';
		filename_save[0] = '\0';
		filename_screenshot[0] = '\0';

		this->signal_instruction_ptr_valid = false;

#ifdef WITH_UCONTEXT
		ucontext_t *ucontext = static_cast<ucontext_t *>(context);
#if defined(__x86_64__)
		this->signal_instruction_ptr = (void *) ucontext->uc_mcontext->__ss.__rip;
		this->signal_instruction_ptr_valid = true;
#elif defined(__i386)
		this->signal_instruction_ptr = (void *) ucontext->uc_mcontext->__ss.__eip;
		this->signal_instruction_ptr_valid = true;
#endif
#endif /* WITH_UCONTEXT */
	}

	CrashLogOSX(DesyncTag tag) : signum(0), si(nullptr), context(nullptr), signal_instruction_ptr_valid(false)
	{
		filename_log[0] = '\0';
		filename_save[0] = '\0';
		filename_screenshot[0] = '\0';
	}

	/** Generate the crash log. */
	bool MakeCrashLog()
	{
		char buffer[65536 * 4];
		bool ret = true;

		printf("Crash encountered, generating crash log...\n");
		this->FillCrashLog(buffer, lastof(buffer));
		printf("%s\n", buffer);
		printf("Crash log generated.\n\n");

		printf("Writing crash log to disk...\n");
		if (!this->WriteCrashLog(buffer, filename_log, lastof(filename_log))) {
			filename_log[0] = '\0';
			ret = false;
		}

		printf("Writing crash savegame...\n");
		_savegame_DBGL_data = buffer;
		_save_DBGC_data = true;
		if (!this->WriteSavegame(filename_save, lastof(filename_save))) {
			filename_save[0] = '\0';
			ret = false;
		}

		printf("Writing crash screenshot...\n");
		SetScreenshotAuxiliaryText("Crash Log", buffer);
		if (!this->WriteScreenshot(filename_screenshot, lastof(filename_screenshot))) {
			filename_screenshot[0] = '\0';
			ret = false;
		}

		return ret;
	}

	/** Show a dialog with the crash information. */
	void DisplayCrashDialog() const
	{
		static const char crash_title[] =
			"A serious fault condition occurred in the game. The game will shut down.";

		char message[1024];
		seprintf(message, lastof(message),
				 "Please send the generated crash information and the last (auto)save to the patchpack developer. "
				 "This will greatly help debugging. The correct place to do this is https://www.tt-forums.net/viewtopic.php?f=33&t=73469"
				 " or https://github.com/JGRennison/OpenTTD-patches\n\n"
				 "Generated file(s):\n%s\n%s\n%s",
				 this->filename_log, this->filename_save, this->filename_screenshot);

		ShowMacDialog(crash_title, message, "Quit");
	}
};

/** The signals we want our crash handler to handle. */
static const int _signals_to_handle[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL, SIGSYS };

/**
 * Entry point for the crash handler.
 * @note Not static so it shows up in the backtrace.
 * @param signum the signal that caused us to crash.
 */
void CDECL HandleCrash(int signum, siginfo_t *si, void *context)
{
	/* Disable all handling of signals by us, so we don't go into infinite loops. */
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
		signal(*i, SIG_DFL);
	}

	const char *abort_reason = CrashLog::GetAbortCrashlogReason();
	if (abort_reason != nullptr) {
		ShowMacDialog("A serious fault condition occurred in the game. The game will shut down.",
				abort_reason,
				"Quit");
		abort();
	}

	CrashLogOSX log(signum, si, context);
	log.MakeCrashLog();
	if (VideoDriver::GetInstance() == nullptr || VideoDriver::GetInstance()->HasGUI()) {
		log.DisplayCrashDialog();
	}

	CrashLog::AfterCrashLogCleanup();
	abort();
}

/* static */ void CrashLog::InitialiseCrashLog()
{
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = HandleCrash;
		sigaction(*i, &sa, nullptr);
	}
}

/* static */ void CrashLog::DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info)
{
	CrashLogOSX log(CrashLogOSX::DesyncTag{});
	log.MakeDesyncCrashLog(log_in, log_out, info);
}

/* static */ void CrashLog::VersionInfoLog()
{
	CrashLogOSX log(CrashLogOSX::DesyncTag{});
	log.MakeVersionInfoLog();
}
