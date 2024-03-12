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
#include "../../sl/saveload.h"
#include "../../scope.h"

#include <errno.h>
#include <signal.h>
#include <sys/utsname.h>
#include <setjmp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>

#if defined(WITH_DBG_GDB)
#include <sys/syscall.h>
#endif /* WITH_DBG_GDB */

#if defined(WITH_PRCTL_PT)
#include <sys/prctl.h>
#endif /* WITH_PRCTL_PT */

#if defined(__GLIBC__)
/* Execinfo (and thus making stacktraces) is a GNU extension */
#	include <execinfo.h>
#if defined(WITH_DL)
#   include <dlfcn.h>
#if defined(WITH_DL2)
#	include <link.h>
#endif
#endif
#if defined(WITH_DEMANGLE)
#   include <cxxabi.h>
#endif
#if defined(WITH_BFD)
#   include <bfd.h>
#endif
#endif /* __GLIBC__ */

#include <atomic>
#include <vector>

#if defined(__EMSCRIPTEN__)
#	include <emscripten.h>
/* We avoid abort(), as it is a SIGBART, and use _exit() instead. But emscripten doesn't know _exit(). */
#	define _exit emscripten_force_exit
#else
#include <unistd.h>
#endif

#include "../../safeguards.h"

/** The signals we want our crash handler to handle. */
static const int _signals_to_handle[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL, SIGQUIT };

std::atomic<pid_t> _crash_tid;
std::atomic<uint32_t> _crash_other_threads;

#if defined(__GLIBC__) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

#if defined(__GLIBC__) && defined(WITH_SIGACTION)
static char *internal_fault_saved_buffer;
static jmp_buf internal_fault_jmp_buf;
sigset_t internal_fault_old_sig_proc_mask;
std::atomic<bool> internal_fault_use_signal_handler;

static void InternalFaultSigHandler(int sig)
{
	longjmp(internal_fault_jmp_buf, sig);
	if (internal_fault_saved_buffer == nullptr) {
		/* if we get here, things are unrecoverable */
		_exit(43);
	}
}
#endif

static int GetTemporaryFD()
{
	char name[MAX_PATH];
	extern std::string _personal_dir;
	seprintf(name, lastof(name), "%sopenttd-tmp-XXXXXX", _personal_dir.c_str());
	int fd = mkstemp(name);
	if (fd != -1) {
		/* Unlink file but leave fd open until finished with */
		unlink(name);
	}
	return fd;
}

struct ExecReadNullHandler {
	int fd[2] = { -1, -1 };

	bool Init() {
		this->fd[0] = open("/dev/null", O_RDWR);
		if (this->fd[0] == -1) {
			this->fd[0] = GetTemporaryFD();
			if (this->fd[0] == -1) {
				return false;
			}
			this->fd[1] = GetTemporaryFD();
			if (this->fd[1] == -1) {
				this->Close();
				return false;
			}
		} else {
			this->fd[1] = this->fd[0];
		}
		return true;
	}

	void Close() {
		if (this->fd[0] != -1) close(this->fd[0]);
		if (this->fd[1] != -1 && this->fd[0] != this->fd[1]) close(this->fd[1]);
		this->fd[0] = -1;
		this->fd[1] = -1;
	}
};

static bool ExecReadStdout(const char *file, char *const *args, char *&buffer, const char *last)
{
	ExecReadNullHandler nulls;
	if (!nulls.Init()) return false;

	int pipefd[2];
	if (pipe(pipefd) == -1) {
		nulls.Close();
		return false;
	}

	int pid = fork();
	if (pid < 0) {
		nulls.Close();
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0) {
		/* child */

		close(pipefd[0]); /* Close unused read end */
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		dup2(nulls.fd[0], STDERR_FILENO);
		dup2(nulls.fd[1], STDIN_FILENO);
		nulls.Close();

		execvp(file, args);
		_exit(42);
	}

	/* parent */

	nulls.Close();
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

#if defined(WITH_DBG_GDB)
static bool ExecReadStdoutThroughFile(const char *file, char *const *args, char *&buffer, const char *last)
{
	ExecReadNullHandler nulls;
	if (!nulls.Init()) return false;

	int fd = GetTemporaryFD();
	if (fd == -1) {
		nulls.Close();
		return false;
	}

	int pid = fork();
	if (pid < 0) {
		nulls.Close();
		close(fd);
		return false;
	}

	if (pid == 0) {
		/* child */

		dup2(fd, STDOUT_FILENO);
		close(fd);
		dup2(nulls.fd[0], STDERR_FILENO);
		dup2(nulls.fd[1], STDIN_FILENO);
		nulls.Close();

		execvp(file, args);
		_exit(42);
	}

	/* parent */

	nulls.Close();

	int status;
	int wait_ret = waitpid(pid, &status, 0);
	if (wait_ret == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		/* command did not appear to run successfully */
		close(fd);
		return false;
	} else {
		/* command executed successfully */
		lseek(fd, 0, SEEK_SET);
		while (buffer < last) {
			ssize_t res = read(fd, buffer, last - buffer);
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
		close(fd);
		return true;
	}
}
#endif /* WITH_DBG_GDB */

/**
 * Unix implementation for the crash logger.
 */
class CrashLogUnix : public CrashLog {
	/** Signal that has been thrown. */
	int signum;
#ifdef WITH_SIGACTION
	siginfo_t *si;
	void *context;
	bool signal_instruction_ptr_valid;
	void *signal_instruction_ptr;
#endif

	char *LogOSVersion(char *buffer, const char *last) const override
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

	char *LogOSVersionDetail(char *buffer, const char *last) const override
	{
		struct utsname name;
		if (uname(&name) < 0) return buffer;

		if (strcmp(name.sysname, "Linux") == 0) {
			char *buffer_orig = buffer;
			buffer += seprintf(buffer, last, "Distro version:\n");

			const char *args[] = { "/bin/sh", "-c", "lsb_release -a || find /etc -maxdepth 1 -type f -a \\( -name '*release' -o -name '*version' \\) -exec head -v {} \\+", nullptr };
			if (!ExecReadStdout("/bin/sh", const_cast<char* const*>(args), buffer, last)) {
				buffer = buffer_orig;
			}
		}
		return buffer;
	}

	char *LogError(char *buffer, const char *last, const char *message) const override
	{
		buffer += seprintf(buffer, last,
				"Crash reason:\n"
				" Signal:  %s (%d)\n",
				strsignal(this->signum),
				this->signum);
#ifdef WITH_SIGACTION
		if (this->si) {
			buffer += seprintf(buffer, last,
					"          si_code: %d",
					this->si->si_code);
			if (this->signum == SIGSEGV) {
				switch (this->si->si_code) {
					case SEGV_MAPERR:
						buffer += seprintf(buffer, last, " (SEGV_MAPERR)");
						break;
					case SEGV_ACCERR:
						buffer += seprintf(buffer, last, " (SEGV_ACCERR)");
						break;
					default:
						break;
				}
			}
			buffer += seprintf(buffer, last, "\n");
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

#if defined(WITH_UCONTEXT) && (defined(__x86_64__) || defined(__i386))
			if (this->signal_instruction_ptr_valid && this->signum == SIGSEGV) {
				auto err = static_cast<ucontext_t *>(this->context)->uc_mcontext.gregs[REG_ERR];
				buffer += seprintf(buffer, last,
						"          REG_ERR: %s%s%s%s%s\n",
							(err & 1) ? "protection fault" : "no page",
							(err & 2) ? ", write" : ", read",
							(err & 4) ? "" : ", kernel",
							(err & 8) ? ", reserved bit" : "",
							(err & 16) ? ", instruction fetch" : ""
						);
			}
#endif /* defined(WITH_UCONTEXT) && (defined(__x86_64__) || defined(__i386)) */

		}
#endif /* WITH_SIGACTION */
		this->CrashLogFaultSectionCheckpoint(buffer);
		buffer += seprintf(buffer, last,
				" Message: %s\n\n",
				message == nullptr ? "<none>" : message
		);

		return buffer;
	}

	/**
	 * Log GDB information if available
	 */
	char *LogDebugExtra(char *buffer, const char *last) const override
	{
		return this->LogGdbInfo(buffer, last);
	}

	/**
	 * Log crash trailer
	 */
	char *LogCrashTrailer(char *buffer, const char *last) const override
	{
		uint32_t other_crashed_threads = _crash_other_threads.load();
		if (other_crashed_threads > 0) {
			buffer += seprintf(buffer, last, "\n*** %u other threads have also crashed ***\n\n", other_crashed_threads);
		}
		return buffer;
	}

	/**
	 * Show registers if possible
	 */
	char *LogRegisters(char *buffer, const char *last) const override
	{
#ifdef WITH_UCONTEXT
		ucontext_t *ucontext = static_cast<ucontext_t *>(context);
#if defined(__x86_64__)
		const gregset_t &gregs = ucontext->uc_mcontext.gregs;
		buffer += seprintf(buffer, last,
			"Registers:\n"
			" rax: %#16llx rbx: %#16llx rcx: %#16llx rdx: %#16llx\n"
			" rsi: %#16llx rdi: %#16llx rbp: %#16llx rsp: %#16llx\n"
			" r8:  %#16llx r9:  %#16llx r10: %#16llx r11: %#16llx\n"
			" r12: %#16llx r13: %#16llx r14: %#16llx r15: %#16llx\n"
			" rip: %#16llx eflags: %#8llx, err: %#llx\n\n",
			gregs[REG_RAX],
			gregs[REG_RBX],
			gregs[REG_RCX],
			gregs[REG_RDX],
			gregs[REG_RSI],
			gregs[REG_RDI],
			gregs[REG_RBP],
			gregs[REG_RSP],
			gregs[REG_R8],
			gregs[REG_R9],
			gregs[REG_R10],
			gregs[REG_R11],
			gregs[REG_R12],
			gregs[REG_R13],
			gregs[REG_R14],
			gregs[REG_R15],
			gregs[REG_RIP],
			gregs[REG_EFL],
			gregs[REG_ERR]
		);
#elif defined(__i386)
		const gregset_t &gregs = ucontext->uc_mcontext.gregs;
		buffer += seprintf(buffer, last,
			"Registers:\n"
			" eax: %#8x ebx: %#8x ecx: %#8x edx: %#8x\n"
			" esi: %#8x edi: %#8x ebp: %#8x esp: %#8x\n"
			" eip: %#8x eflags: %#8x, err: %#x\n\n",
			gregs[REG_EAX],
			gregs[REG_EBX],
			gregs[REG_ECX],
			gregs[REG_EDX],
			gregs[REG_ESI],
			gregs[REG_EDI],
			gregs[REG_EBP],
			gregs[REG_ESP],
			gregs[REG_EIP],
			gregs[REG_EFL],
			gregs[REG_ERR]
		);
#endif
#endif
		return buffer;
	}

	/**
	 * Get a stack backtrace of the current thread's stack and other info using the gdb debugger, if available.
	 *
	 * Using GDB is useful as it knows about inlined functions and locals, and generally can
	 * do a more thorough job than in LogStacktrace.
	 * This is done in addition to LogStacktrace as gdb cannot be assumed to be present
	 * and there is some potentially useful information in the output from LogStacktrace
	 * which is not in gdb's output.
	 */
	char *LogGdbInfo(char *buffer, const char *last) const
	{
#if defined(WITH_DBG_GDB)

#if defined(WITH_PRCTL_PT)
		prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif /* WITH_PRCTL_PT */

		pid_t tid = syscall(SYS_gettid);

		char *buffer_orig = buffer;
		buffer += seprintf(buffer, last, "GDB info:\n");

		char tid_buffer[16];
		char disasm_buffer[32];

		seprintf(tid_buffer, lastof(tid_buffer), "%d", tid);

		std::vector<const char *> args;
		args.push_back("gdb");
		args.push_back("-n");
		args.push_back("-p");
		args.push_back(tid_buffer);
		args.push_back("-batch");

		args.push_back("-ex");
		args.push_back("echo \\nBacktrace:\\n");
		args.push_back("-ex");
		args.push_back("bt full 100");

#ifdef WITH_SIGACTION
		if (this->GetMessage() == nullptr && this->signal_instruction_ptr_valid) {
			seprintf(disasm_buffer, lastof(disasm_buffer), "x/1i %p", this->signal_instruction_ptr);
			args.push_back("-ex");
			args.push_back("set disassembly-flavor intel");
			args.push_back("-ex");
			args.push_back("echo \\nFault instruction:\\n");
			args.push_back("-ex");
			args.push_back(disasm_buffer);
		}
#endif

		args.push_back(nullptr);
		if (!ExecReadStdoutThroughFile("gdb", const_cast<char* const*>(&(args[0])), buffer, last)) {
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
	 *
	 * If libdl is present, try to use that to get the section file name and possibly the symbol
	 * name/address instead of using the string from backtrace_symbols().
	 * If libdl and libbfd are present, try to use that to get the symbol name/address using the
	 * section file name returned from libdl. This is becuase libbfd also does line numbers,
	 * and knows about more symbols than libdl does.
	 * If demangling support is available, try to demangle whatever symbol name we got back.
	 * If we could find a symbol address from libdl or libbfd, show the offset from that to the frame address.
	 */
	char *LogStacktrace(char *buffer, const char *last) const override
	{
		buffer += seprintf(buffer, last, "Stacktrace:\n");

#if defined(__GLIBC__)
		void *trace[64];
		int trace_size = backtrace(trace, lengthof(trace));

		char **messages = backtrace_symbols(trace, trace_size);

#if defined(WITH_BFD)
		sym_bfd_obj_cache bfd_cache;
		bfd_init();
#endif /* WITH_BFD */

		for (int i = 0; i < trace_size; i++) {
			auto guard = scope_guard([&]() {
				this->CrashLogFaultSectionCheckpoint(buffer);
			});
#if defined(WITH_DL)
			Dl_info info;
#if defined(WITH_DL2)
			struct link_map *dl_lm = nullptr;
			int dladdr_result = dladdr1(trace[i], &info, (void **)&dl_lm, RTLD_DL_LINKMAP);
#else
			int dladdr_result = dladdr(trace[i], &info);
#endif /* WITH_DL2 */
			const char *func_name = info.dli_sname;
			void *func_addr = info.dli_saddr;
			const char *file_name = nullptr;
			unsigned int line_num = 0;
			const int ptr_str_size = (2 + sizeof(void*) * 2);
#if defined(WITH_DL2)
			if (dladdr_result && info.dli_fname && dl_lm != nullptr) {
				char *saved_buffer = buffer;
				char addr_ptr_buffer[64];
				/* subtract one to get the line before the return address, i.e. the function call line */
				seprintf(addr_ptr_buffer, lastof(addr_ptr_buffer), PRINTF_SIZEX, (char *)trace[i] - (char *)dl_lm->l_addr - 1);
				const char *args[] = {
					"addr2line",
					"-e",
					info.dli_fname,
					"-C",
					"-i",
					"-f",
					"-p",
					addr_ptr_buffer,
					nullptr,
				};
				buffer += seprintf(buffer, last, " [%02i] %*p %-40s ", i, ptr_str_size, trace[i], info.dli_fname);
				const char *buffer_start = buffer;
				bool result = ExecReadStdout("addr2line", const_cast<char* const*>(args), buffer, last);
				if (result && strstr(buffer_start, "??") == nullptr) {
					while (buffer[-1] == '\n' && buffer[-2] == '\n') buffer--;
					*buffer = 0;
					continue;
				}
				buffer = saved_buffer;
				*buffer = 0;
			}
#endif /* WITH_DL2 */
#if defined(WITH_BFD)
			/* subtract one to get the line before the return address, i.e. the function call line */
			sym_info_bfd bfd_info(reinterpret_cast<bfd_vma>(trace[i]) - reinterpret_cast<bfd_vma>(info.dli_fbase) - 1);
			if (dladdr_result && info.dli_fname) {
				lookup_addr_bfd(info.dli_fname, bfd_cache, bfd_info);
				if (bfd_info.file_name != nullptr) file_name = bfd_info.file_name;
				if (bfd_info.function_name != nullptr) func_name = bfd_info.function_name;
				if (bfd_info.function_addr != 0) func_addr = reinterpret_cast<void *>(bfd_info.function_addr + reinterpret_cast<bfd_vma>(info.dli_fbase));
				line_num = bfd_info.line;
			}
#endif /* WITH_BFD */
			bool ok = true;
			if (dladdr_result && func_name) {
				int status = -1;
				char *demangled = nullptr;
#if defined(WITH_DEMANGLE)
				demangled = abi::__cxa_demangle(func_name, nullptr, 0, &status);
#endif /* WITH_DEMANGLE */
				const char *name = (demangled != nullptr && status == 0) ? demangled : func_name;
				buffer += seprintf(buffer, last, " [%02i] %*p %-40s %s + 0x%zx", i, ptr_str_size,
						trace[i], info.dli_fname, name, (char *)trace[i] - (char *)func_addr);
				free(demangled);
			} else if (dladdr_result && info.dli_fname) {
				buffer += seprintf(buffer, last, " [%02i] %*p %-40s + 0x%zx", i, ptr_str_size,
						trace[i], info.dli_fname, (char *)trace[i] - (char *)info.dli_fbase);
			} else {
				ok = false;
			}
			if (ok && file_name != nullptr) {
				buffer += seprintf(buffer, last, " at %s:%u", file_name, line_num);
			}
			if (ok) buffer += seprintf(buffer, last, "\n");
#if defined(WITH_BFD)
			if (ok && bfd_info.found && bfd_info.abfd) {
				uint iteration_limit = 32;
				while (iteration_limit-- && bfd_find_inliner_info(bfd_info.abfd, &file_name, &func_name, &line_num)) {
					if (func_name) {
						int status = -1;
						char *demangled = nullptr;
#if defined(WITH_DEMANGLE)
						demangled = abi::__cxa_demangle(func_name, nullptr, 0, &status);
#endif /* WITH_DEMANGLE */
						const char *name = (demangled != nullptr && status == 0) ? demangled : func_name;
						buffer += seprintf(buffer, last, " [inlined] %*s %s", ptr_str_size + 36, "",
								name);
						free(demangled);
					} else if (file_name) {
						buffer += seprintf(buffer, last, " [inlined]");
					}
					if (file_name != nullptr) {
						buffer += seprintf(buffer, last, " at %s:%u", file_name, line_num);
					}
					buffer += seprintf(buffer, last, "\n");
				}
			}
#endif /* WITH_BFD */
			if (ok) continue;
#endif /* WITH_DL */
			buffer += seprintf(buffer, last, " [%02i] %s\n", i, messages[i]);
		}
		free(messages);

/* end of __GLIBC__ */
#else
		buffer += seprintf(buffer, last, " Not supported.\n");
#endif
		return buffer + seprintf(buffer, last, "\n");
	}

#if defined(__GLIBC__) && defined(WITH_SIGACTION)
	/* virtual */ void StartCrashLogFaultHandler() override
	{
		internal_fault_saved_buffer = nullptr;

		sigset_t sigs;
		sigemptyset(&sigs);
		for (int signum : _signals_to_handle) {
			sigaddset(&sigs, signum);
		}

		internal_fault_use_signal_handler.store(true);
		sigprocmask(SIG_UNBLOCK, &sigs, &internal_fault_old_sig_proc_mask);
	}

	/* virtual */ void StopCrashLogFaultHandler() override
	{
		internal_fault_saved_buffer = nullptr;

		internal_fault_use_signal_handler.store(false);
		sigprocmask(SIG_SETMASK, &internal_fault_old_sig_proc_mask, nullptr);
	}

	/**
	 * Set up further signal handlers to handle the case where we trigger another fault signal
	 * during the course of calling the given log section writer.
	 *
	 * If a signal does occur, restore the buffer pointer to either the original value, or
	 * the value provided in any later checkpoint.
	 * Insert a message describing the problem and give up on the section.
	 *
	 * Note that GCC complains about 'buffer' being clobbered by the longjmp.
	 * This is not an issue as we save/restore it explicitly, so silence the warning.
	 */
	/* virtual */ char *TryCrashLogFaultSection(char *buffer, const char *last, const char *section_name, CrashLogSectionWriter writer) override
	{
		this->FlushCrashLogBuffer();
		internal_fault_saved_buffer = buffer;

		int signum = setjmp(internal_fault_jmp_buf);
		if (signum != 0) {
			if (internal_fault_saved_buffer == nullptr) {
				/* if we get here, things are unrecoverable */
				_exit(43);
			}

			buffer = internal_fault_saved_buffer;
			internal_fault_saved_buffer = nullptr;

			buffer += seprintf(buffer, last, "\nSomething went seriously wrong when attempting to fill the '%s' section of the crash log: signal: %s (%d).\n", section_name, strsignal(signum), signum);
			buffer += seprintf(buffer, last, "This is probably due to an invalid pointer or other corrupt data.\n\n");

			sigset_t sigs;
			sigemptyset(&sigs);
			for (int signum : _signals_to_handle) {
				sigaddset(&sigs, signum);
			}
			sigprocmask(SIG_UNBLOCK, &sigs, nullptr);

			return buffer;
		}

		buffer = writer(this, buffer, last);
		internal_fault_saved_buffer = nullptr;
		return buffer;
	}

	/* virtual */ void CrashLogFaultSectionCheckpoint(char *buffer) const override
	{
		if (internal_fault_saved_buffer != nullptr && buffer > internal_fault_saved_buffer) {
			internal_fault_saved_buffer = buffer;
		}

		const_cast<CrashLogUnix *>(this)->FlushCrashLogBuffer();
	}
#endif /* __GLIBC__ && WITH_SIGACTION */

public:
	struct DesyncTag {};

	/**
	 * A crash log is always generated by signal.
	 * @param signum the signal that was caused by the crash.
	 */
#ifdef WITH_SIGACTION
	CrashLogUnix(int signum, siginfo_t *si, void *context) :
		signum(signum), si(si), context(context)
	{
		this->signal_instruction_ptr_valid = false;

#ifdef WITH_UCONTEXT
		ucontext_t *ucontext = static_cast<ucontext_t *>(context);
#if defined(__x86_64__)
		this->signal_instruction_ptr = (void *) ucontext->uc_mcontext.gregs[REG_RIP];
		this->signal_instruction_ptr_valid = true;
#elif defined(__i386)
		this->signal_instruction_ptr = (void *) ucontext->uc_mcontext.gregs[REG_EIP];
		this->signal_instruction_ptr_valid = true;
#endif
#endif /* WITH_UCONTEXT */
	}
#else
	CrashLogUnix(int signum) :
		signum(signum)
	{
	}
#endif /* WITH_SIGACTION */

	CrashLogUnix(DesyncTag tag) : signum(0)
	{
#ifdef WITH_SIGACTION
		this->si = nullptr;
		this->context = nullptr;
		this->signal_instruction_ptr_valid = false;
#endif
	}
};

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
	pid_t tid = 1;
#if defined(WITH_DBG_GDB)
	tid = syscall(SYS_gettid);
#endif

	pid_t already_crashed = _crash_tid.load();
	do {
		/* Is this a recursive call from the crash thread */
		if (already_crashed == tid) {
#if defined(__GLIBC__) && defined(WITH_SIGACTION)
			if (internal_fault_use_signal_handler.load()) {
				InternalFaultSigHandler(signum);
				return;
			}
#endif

			/* This should never be reached, just give up at this point */
			_exit(43);
		}

		/* Is a different thread in the crash logger already? */
		if (already_crashed != 0) {
			/* Just sleep forever while the other thread is busy logging the crash */
			_crash_other_threads++;
			while (true) {
				pause();
			}
		}

		/* Atomically mark this thread as the crashing thread */
	} while (!_crash_tid.compare_exchange_weak(already_crashed, tid));

#ifndef WITH_SIGACTION
	/* Disable all handling of signals by us, so we don't go into infinite loops. */
	for (int signum : _signals_to_handle) {
		signal(signum, SIG_DFL);
	}
#endif

	const char *abort_reason = CrashLog::GetAbortCrashlogReason();
	if (abort_reason != nullptr) {
		printf("A serious fault condition occurred in the game. The game will shut down.\n%s", abort_reason);
		abort();
	}

#ifdef WITH_SIGACTION
	CrashLogUnix log(signum, si, context);
#else
	CrashLogUnix log(signum);
#endif
	const size_t length = 65536 * 16;
	char *buffer = (char *)mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buffer != MAP_FAILED) {
		log.MakeCrashLog(buffer, buffer + length - 1);
	} else {
		log.MakeCrashLogWithStackBuffer();
	}

	CrashLog::AfterCrashLogCleanup();
	abort();
}

/* static */ void CrashLog::InitialiseCrashLog()
{
#ifdef WITH_SIGALTSTACK
	const size_t stack_size = std::max<size_t>(SIGSTKSZ, 512*1024);
	stack_t ss;
	ss.ss_sp = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ss.ss_size = stack_size;
	ss.ss_flags = 0;
	sigaltstack(&ss, nullptr);
#endif

#ifdef WITH_SIGACTION
	sigset_t sigs;
	sigemptyset(&sigs);
	for (int signum : _signals_to_handle) {
		sigaddset(&sigs, signum);
	}
	for (int signum : _signals_to_handle) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
#ifdef WITH_SIGALTSTACK
		sa.sa_flags |= SA_ONSTACK;
#endif
		sa.sa_mask = sigs;
		sa.sa_sigaction = HandleCrash;
		sigaction(signum, &sa, nullptr);
	}
#else
	for (int signum : _signals_to_handle) {
		signal(signum, HandleCrash);
	}
#endif
}

/* static */ void CrashLog::InitThread()
{
}

/* static */ void CrashLog::DesyncCrashLog(const std::string *log_in, std::string *log_out, const DesyncExtraInfo &info)
{
	CrashLogUnix log(CrashLogUnix::DesyncTag{});
	log.MakeDesyncCrashLog(log_in, log_out, info);
}

/* static */ void CrashLog::InconsistencyLog(const InconsistencyExtraInfo &info)
{
	CrashLogUnix log(CrashLogUnix::DesyncTag{});
	log.MakeInconsistencyLog(info);
}

/* static */ void CrashLog::VersionInfoLog(char *buffer, const char *last)
{
	CrashLogUnix log(CrashLogUnix::DesyncTag{});
	log.FillVersionInfoLog(buffer, last);
}
