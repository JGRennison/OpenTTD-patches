/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog_unix.cpp Unix crash log handler */

#include "../../stdafx.h"
#include "../../crashlog.h"
#include "../../crashlog_bfd.h"
#include "../../string_func.h"
#include "../../gamelog.h"
#include "../../saveload/saveload.h"

#include <errno.h>
#include <signal.h>
#include <sys/utsname.h>
#include <setjmp.h>

#if defined(WITH_DBG_GDB)
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif /* WITH_DBG_GDB */

#if defined(WITH_PRCTL_PT)
#include <sys/prctl.h>
#endif /* WITH_PRCTL_PT */

#if defined(__GLIBC__)
/* Execinfo (and thus making stacktraces) is a GNU extension */
#	include <execinfo.h>
#if defined(WITH_DL)
#   include <dlfcn.h>
#endif
#if defined(WITH_DEMANGLE)
#   include <cxxabi.h>
#endif
#if defined(WITH_BFD)
#   include <bfd.h>
#endif
#elif defined(SUNOS)
#	include <ucontext.h>
#	include <dlfcn.h>
#endif

#if defined(__NetBSD__)
#include <unistd.h>
#endif

#include "../../safeguards.h"

#if defined(__GLIBC__) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

#if defined(__GLIBC__)
static char *logStacktraceSavedBuffer;
static jmp_buf logStacktraceJmpBuf;

static void LogStacktraceSigSegvHandler(int  sig)
{
	signal(SIGSEGV, SIG_DFL);
	longjmp(logStacktraceJmpBuf, 1);
}
#endif

/**
 * Unix implementation for the crash logger.
 */
class CrashLogUnix : public CrashLog {
	/** Signal that has been thrown. */
	int signum;
#ifdef WITH_SIGACTION
	siginfo_t *si;
#endif

	/* virtual */ char *LogOSVersion(char *buffer, const char *last) const
	{
		struct utsname name;
		if (uname(&name) < 0) {
			return buffer + seprintf(buffer, last, "Could not get OS version: %s\n", strerror(errno));
		}

		return buffer + seprintf(buffer, last,
				"Operating system:\n"
				" Name:     %s\n"
				" Release:  %s\n"
				" Version:  %s\n"
				" Machine:  %s\n",
				name.sysname,
				name.release,
				name.version,
				name.machine
		);
	}

	/* virtual */ char *LogError(char *buffer, const char *last, const char *message) const
	{
		buffer += seprintf(buffer, last,
				"Crash reason:\n"
				" Signal:  %s (%d)\n",
				strsignal(this->signum),
				this->signum);
#ifdef WITH_SIGACTION
		if (this->si) {
			buffer += seprintf(buffer, last,
					"          si_code: %d\n",
					this->si->si_code);
			if (this->signum != SIGABRT) {
				buffer += seprintf(buffer, last,
						"          fault address: %p\n",
						this->si->si_addr);
			}
		}
#endif
		buffer += seprintf(buffer, last,
				" Message: %s\n\n",
				message == NULL ? "<none>" : message
		);

		return buffer;
	}

#if defined(SUNOS)
	/** Data needed while walking up the stack */
	struct StackWalkerParams {
		char **bufptr;    ///< Buffer
		const char *last; ///< End of buffer
		int counter;      ///< We are at counter-th stack level
	};

	/**
	 * Callback used while walking up the stack.
	 * @param pc program counter
	 * @param sig 'active' signal (unused)
	 * @param params parameters
	 * @return always 0, continue walking up the stack
	 */
	static int SunOSStackWalker(uintptr_t pc, int sig, void *params)
	{
		StackWalkerParams *wp = (StackWalkerParams *)params;

		/* Resolve program counter to file and nearest symbol (if possible) */
		Dl_info dli;
		if (dladdr((void *)pc, &dli) != 0) {
			*wp->bufptr += seprintf(*wp->bufptr, wp->last, " [%02i] %s(%s+0x%x) [0x%x]\n",
					wp->counter, dli.dli_fname, dli.dli_sname, (int)((byte *)pc - (byte *)dli.dli_saddr), (uint)pc);
		} else {
			*wp->bufptr += seprintf(*wp->bufptr, wp->last, " [%02i] [0x%x]\n", wp->counter, (uint)pc);
		}
		wp->counter++;

		return 0;
	}
#endif

	/**
	 * Get a stack backtrace of the current thread's stack using the gdb debugger, if available.
	 *
	 * Using GDB is useful as it knows about inlined functions and locals, and generally can
	 * do a more thorough job than in LogStacktrace.
	 * This is done in addition to LogStacktrace as gdb cannot be assumed to be present
	 * and there is some potentially useful information in the output from LogStacktrace
	 * which is not in gdb's output.
	 */
	char *LogStacktraceGdb(char *buffer, const char *last) const
	{
#if defined(WITH_DBG_GDB)

#if defined(WITH_PRCTL_PT)
		prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif /* WITH_PRCTL_PT */

		pid_t tid = syscall(SYS_gettid);

		int pipefd[2];
		if (pipe(pipefd) == -1) return buffer;

		int pid = fork();
		if (pid < 0) return buffer;

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
			char buffer[16];
			seprintf(buffer, lastof(buffer), "%d", tid);
			execlp("gdb", "gdb", "-n", "-p", buffer, "-batch", "-ex", "bt full", NULL);
			exit(42);
		}

		/* parent */

		close(pipefd[1]); /* Close unused write end */

		char *buffer_orig = buffer;

		buffer += seprintf(buffer, last, "Stacktrace (GDB):\n");
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
			/* gdb did not appear to run successfully */
			buffer = buffer_orig;
		}
#endif /* WITH_DBG_GDB */

		return buffer;
	}

	/**
	 * Get a stack backtrace of the current thread's stack.
	 *
	 * This has several modes/options, the most full-featured/complex of which is GLIBC mode.
	 *
	 * This gets the backtrace using backtrace() and backtrace_symbols().
	 * backtrace() is prone to crashing if the stack is invalid.
	 * Also these functions freely use malloc which is not technically OK in a signal handler, as
	 * malloc is not re-entrant.
	 * For that reason, set up another SIGSEGV handler to handle the case where we trigger a SIGSEGV
	 * during the course of getting the backtrace.
	 *
	 * If libdl is present, try to use that to get the section file name and possibly the symbol
	 * name/address instead of using the string from backtrace_symbols().
	 * If libdl and libbfd are present, try to use that to get the symbol name/address using the
	 * section file name returned from libdl. This is becuase libbfd also does line numbers,
	 * and knows about more symbols than libdl does.
	 * If demangling support is available, try to demangle whatever symbol name we got back.
	 * If we could find a symbol address from libdl or libbfd, show the offset from that to the frame address.
	 *
	 * Note that GCC complains about 'buffer' being clobbered by the longjmp.
	 * This is not an issue as we save/restore it explicitly, so silence the warning.
	 */
	/* virtual */ char *LogStacktrace(char *buffer, const char *last) const
	{
		buffer = LogStacktraceGdb(buffer, last);

		buffer += seprintf(buffer, last, "Stacktrace:\n");

#if defined(__GLIBC__)
		logStacktraceSavedBuffer = buffer;

		if (setjmp(logStacktraceJmpBuf) != 0) {
			buffer = logStacktraceSavedBuffer;
			buffer += seprintf(buffer, last, "\nSomething went seriously wrong when attempting to decode the stacktrace (SIGSEGV in signal handler)\n");
			buffer += seprintf(buffer, last, "This is probably due to either: a crash caused by an attempt to call an invalid function\n");
			buffer += seprintf(buffer, last, "pointer, some form of stack corruption, or an attempt was made to call malloc recursively.\n\n");
			return buffer;
		}

		signal(SIGSEGV, LogStacktraceSigSegvHandler);
		sigset_t sigs;
		sigset_t oldsigs;
		sigemptyset(&sigs);
		sigaddset(&sigs, SIGSEGV);
		sigprocmask(SIG_UNBLOCK, &sigs, &oldsigs);

		void *trace[64];
		int trace_size = backtrace(trace, lengthof(trace));

		char **messages = backtrace_symbols(trace, trace_size);

#if defined(WITH_BFD)
		bfd_init();
#endif /* WITH_BFD */

		for (int i = 0; i < trace_size; i++) {
#if defined(WITH_DL)
			Dl_info info;
			int dladdr_result = dladdr(trace[i], &info);
			const char *func_name = info.dli_sname;
			void *func_addr = info.dli_saddr;
			const char *file_name = NULL;
			unsigned int line_num = 0;
#if defined(WITH_BFD)
			/* subtract one to get the line before the return address, i.e. the function call line */
			sym_info_bfd bfd_info(reinterpret_cast<bfd_vma>(trace[i]) - 1);
			if (dladdr_result && info.dli_fname) {
				lookup_addr_bfd(info.dli_fname, bfd_info);
				if (bfd_info.file_name != NULL) file_name = bfd_info.file_name;
				if (bfd_info.function_name != NULL) func_name = bfd_info.function_name;
				if (bfd_info.function_addr != 0) func_addr = reinterpret_cast<void *>(bfd_info.function_addr);
				line_num = bfd_info.line;
			}
#endif /* WITH_BFD */
			bool ok = true;
			const int ptr_str_size = (2 + sizeof(void*) * 2);
			if (dladdr_result && func_name) {
				int status = -1;
				char *demangled = NULL;
#if defined(WITH_DEMANGLE)
				demangled = abi::__cxa_demangle(func_name, NULL, 0, &status);
#endif /* WITH_DEMANGLE */
				const char *name = (demangled != NULL && status == 0) ? demangled : func_name;
				buffer += seprintf(buffer, last, " [%02i] %*p %-40s %s + 0x%zx\n", i, ptr_str_size,
						trace[i], info.dli_fname, name, (char *)trace[i] - (char *)func_addr);
				free(demangled);
			} else if (dladdr_result && info.dli_fname) {
				buffer += seprintf(buffer, last, " [%02i] %*p %-40s + 0x%zx\n", i, ptr_str_size,
						trace[i], info.dli_fname, (char *)trace[i] - (char *)info.dli_fbase);
			} else {
				ok = false;
			}
			if (file_name != NULL) {
				buffer += seprintf(buffer, last, "%*s%s:%u\n", 7 + ptr_str_size, "", file_name, line_num);
			}
			if (ok) continue;
#endif /* WITH_DL */
			buffer += seprintf(buffer, last, " [%02i] %s\n", i, messages[i]);
		}
		free(messages);

		signal(SIGSEGV, SIG_DFL);
		sigprocmask(SIG_SETMASK, &oldsigs, NULL);

/* end of __GLIBC__ */
#elif defined(SUNOS)
		ucontext_t uc;
		if (getcontext(&uc) != 0) {
			buffer += seprintf(buffer, last, " getcontext() failed\n\n");
			return buffer;
		}

		StackWalkerParams wp = { &buffer, last, 0 };
		walkcontext(&uc, &CrashLogUnix::SunOSStackWalker, &wp);

/* end of SUNOS */
#else
		buffer += seprintf(buffer, last, " Not supported.\n");
#endif
		return buffer + seprintf(buffer, last, "\n");
	}

#if defined(USE_SCOPE_INFO) && defined(__GLIBC__)
	/**
	 * This is a wrapper around the generic LogScopeInfo function which sets
	 * up a signal handler to catch any SIGSEGVs which may occur due to invalid data
	 */
	/* virtual */ char *LogScopeInfo(char *buffer, const char *last) const
	{
		logStacktraceSavedBuffer = buffer;

		if (setjmp(logStacktraceJmpBuf) != 0) {
			buffer = logStacktraceSavedBuffer;
			buffer += seprintf(buffer, last, "\nSomething went seriously wrong when attempting to dump the scope info (SIGSEGV in signal handler).\n");
			buffer += seprintf(buffer, last, "This is probably due to an invalid pointer or other corrupt data.\n\n");
			return buffer;
		}

		signal(SIGSEGV, LogStacktraceSigSegvHandler);
		sigset_t sigs;
		sigset_t oldsigs;
		sigemptyset(&sigs);
		sigaddset(&sigs, SIGSEGV);
		sigprocmask(SIG_UNBLOCK, &sigs, &oldsigs);

		buffer = this->CrashLog::LogScopeInfo(buffer, last);

		signal(SIGSEGV, SIG_DFL);
		sigprocmask(SIG_SETMASK, &oldsigs, NULL);
		return buffer;
	}
#endif

public:
	/**
	 * A crash log is always generated by signal.
	 * @param signum the signal that was caused by the crash.
	 */
#ifdef WITH_SIGACTION
	CrashLogUnix(int signum, siginfo_t *si) :
		signum(signum), si(si)
	{
	}
#else
	CrashLogUnix(int signum) :
		signum(signum)
	{
	}
#endif
};

/** The signals we want our crash handler to handle. */
static const int _signals_to_handle[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL };

/**
 * Entry point for the crash handler.
 * @note Not static so it shows up in the backtrace.
 * @param signum the signal that caused us to crash.
 */
#ifdef WITH_SIGACTION
static void CDECL HandleCrash(int signum, siginfo_t *si, void *context)
#else
static void CDECL HandleCrash(int signum)
#endif
{
	/* Disable all handling of signals by us, so we don't go into infinite loops. */
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
		signal(*i, SIG_DFL);
	}

	if (GamelogTestEmergency()) {
		printf("A serious fault condition occurred in the game. The game will shut down.\n");
		printf("As you loaded an emergency savegame no crash information will be generated.\n");
		abort();
	}

	if (SaveloadCrashWithMissingNewGRFs()) {
		printf("A serious fault condition occurred in the game. The game will shut down.\n");
		printf("As you loaded an savegame for which you do not have the required NewGRFs\n");
		printf("no crash information will be generated.\n");
		abort();
	}

#ifdef WITH_SIGACTION
	CrashLogUnix log(signum, si);
#else
	CrashLogUnix log(signum);
#endif
	log.MakeCrashLog();

	CrashLog::AfterCrashLogCleanup();
	abort();
}

/* static */ void CrashLog::InitialiseCrashLog()
{
	for (const int *i = _signals_to_handle; i != endof(_signals_to_handle); i++) {
#ifdef WITH_SIGACTION
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = HandleCrash;
		sigaction(*i, &sa, NULL);
#else
		signal(*i, HandleCrash);
#endif
	}
}
