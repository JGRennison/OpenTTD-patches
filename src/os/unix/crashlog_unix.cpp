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
#include "../../core/format.hpp"

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
	format_to_fixed_z::format_to(name, lastof(name), "{}openttd-tmp-XXXXXX", _personal_dir);
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

static bool ExecReadStdout(const char *file, char *const *args, format_target &buffer)
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

	bool ok = true;
	while (ok && !buffer.has_overflowed()) {
		buffer.append_span_func(2048, [&](std::span<char> buf) -> size_t {
			ssize_t res = read(pipefd[0], buf.data(), buf.size());
			if (res < 0) {
				if (errno != EINTR) ok = false;
				return 0;
			} else if (res == 0) {
				ok = false;
				return 0;
			} else {
				return (size_t)res;
			}
		});
	}
	buffer.push_back('\n');

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
static bool ExecReadStdoutThroughFile(const char *file, char *const *args, format_target &buffer)
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
		bool ok = true;
		while (ok && !buffer.has_overflowed()) {
			buffer.append_span_func(2048, [&](std::span<char> buf) -> size_t {
				ssize_t res = read(fd, buf.data(), buf.size());
				if (res < 0) {
					if (errno != EINTR) ok = false;
					return 0;
				} else if (res == 0) {
					ok = false;
					return 0;
				} else {
					return (size_t)res;
				}
			});
		}
		buffer.push_back('\n');
		close(fd);
		return true;
	}
}
#endif /* WITH_DBG_GDB */

/**
 * Unix implementation for the crash logger.
 */
class CrashLogUnix final : public CrashLog {
	/** Signal that has been thrown. */
	int signum;
#ifdef WITH_SIGACTION
	siginfo_t *si;
	void *context;
	bool signal_instruction_ptr_valid;
	void *signal_instruction_ptr;
#endif
	int crash_file = -1;

	bool OpenLogFile(const char *filename) override
	{
		int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd >= 0) {
			this->crash_file = fd;
			return true;
		} else {
			return false;
		}
	}

	void WriteToFd(int fd, std::string_view data)
	{
		while (!data.empty()) {
			ssize_t res = write(fd, data.data(), data.size());
			if (res < 0) {
				if (errno != EINTR) break;
			} else if (res == 0) {
				break;
			} else {
				data.remove_prefix(res);
			}
		}
	}

	void WriteToLogFile(std::string_view data) override
	{
		this->WriteToFd(this->crash_file, data);
	}

	void WriteToStdout(std::string_view data) override
	{
		this->WriteToFd(STDOUT_FILENO, data);
	}

	void CloseLogFile() override
	{
		close(this->crash_file);
		this->crash_file = -1;
	}

	void LogOSVersion(format_target &buffer) const override
	{
		struct utsname name;
		if (uname(&name) < 0) {
			buffer.format("Could not get OS version: {}\n", StrErrorDumper().GetLast());
			return;
		}

		buffer.format(
				"Operating system:\n"
				" Name:     {}\n"
				" Release:  {}\n"
				" Version:  {}\n"
				" Machine:  {}\n",
				name.sysname,
				name.release,
				name.version,
				name.machine
		);
	}

	void LogOSVersionDetail(format_target &buffer) const override
	{
		struct utsname name;
		if (uname(&name) < 0) return;

		if (strcmp(name.sysname, "Linux") == 0) {
			size_t orig = buffer.size();
			buffer.append("Distro version:\n");

			const char *args[] = { "/bin/sh", "-c", "lsb_release -a || find /etc -maxdepth 1 -type f -a \\( -name '*release' -o -name '*version' \\) -exec head -v {} \\+", nullptr };
			if (!ExecReadStdout("/bin/sh", const_cast<char* const*>(args), buffer)) {
				buffer.restore_size(orig);
			}
		}
	}

	void LogError(format_target &buffer, const char *message) const override
	{
		buffer.format(
				"Crash reason:\n"
				" Signal:  {} ({})\n",
				strsignal(this->signum),
				this->signum);
#ifdef WITH_SIGACTION
		if (this->si) {
			buffer.format(
					"          si_code: {}",
					this->si->si_code);
			if (this->signum == SIGSEGV) {
				switch (this->si->si_code) {
					case SEGV_MAPERR:
						buffer.append(" (SEGV_MAPERR)");
						break;
					case SEGV_ACCERR:
						buffer.append(" (SEGV_ACCERR)");
						break;
					default:
						break;
				}
			}
			buffer.push_back('\n');
			if (this->signum != SIGABRT) {
				buffer.format(
						"          Fault address: {}\n",
						fmt::ptr(this->si->si_addr));
				if (this->signal_instruction_ptr_valid) {
					buffer.format(
							"          Instruction address: {}\n",
							fmt::ptr(this->signal_instruction_ptr));
				}
			}

#if defined(WITH_UCONTEXT) && (defined(__x86_64__) || defined(__i386))
			if (this->signal_instruction_ptr_valid && this->signum == SIGSEGV) {
				auto err = static_cast<ucontext_t *>(this->context)->uc_mcontext.gregs[REG_ERR];
				buffer.format(
						"          REG_ERR: {}{}{}{}{}\n",
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
		buffer.format(
				" Message: {}\n\n",
				message == nullptr ? "<none>" : message
		);
	}

	/**
	 * Log GDB information if available
	 */
	void LogDebugExtra(format_target &buffer) const override
	{
		this->LogGdbInfo(buffer);
	}

	/**
	 * Log crash trailer
	 */
	void LogCrashTrailer(format_target &buffer) const override
	{
		uint32_t other_crashed_threads = _crash_other_threads.load();
		if (other_crashed_threads > 0) {
			buffer.format("\n*** {} other threads have also crashed ***\n\n", other_crashed_threads);
		}
	}

	/**
	 * Show registers if possible
	 */
	void LogRegisters(format_target &buffer) const override
	{
#ifdef WITH_UCONTEXT
		ucontext_t *ucontext = static_cast<ucontext_t *>(context);
#if defined(__x86_64__)
		const gregset_t &gregs = ucontext->uc_mcontext.gregs;
		buffer.format(
			"Registers:\n"
			" rax: 0x{:016X} rbx: 0x{:016X} rcx: 0x{:016X} rdx: 0x{:016X}\n"
			" rsi: 0x{:016X} rdi: 0x{:016X} rbp: 0x{:016X} rsp: 0x{:016X}\n"
			" r8:  0x{:016X} r9:  0x{:016X} r10: 0x{:016X} r11: 0x{:016X}\n"
			" r12: 0x{:016X} r13: 0x{:016X} r14: 0x{:016X} r15: 0x{:016X}\n"
			" rip: 0x{:016X} eflags: 0x{:08X}, err: 0x{:X}\n\n",
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
		buffer.format(
			"Registers:\n"
			" eax: 0x{:08X} ebx: 0x{:08X} ecx: 0x{:08X} edx: 0x{:08X}\n"
			" esi: 0x{:08X} edi: 0x{:08X} ebp: 0x{:08X} esp: 0x{:08X}\n"
			" eip: 0x{:08X} eflags: 0x{:08X}, err: 0x{:X}\n\n",
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
	void LogGdbInfo(format_target &buffer) const
	{
#if defined(WITH_DBG_GDB)

#if defined(WITH_PRCTL_PT)
		prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif /* WITH_PRCTL_PT */

		pid_t tid = syscall(SYS_gettid);

		size_t orig = buffer.size();
		buffer.append("GDB info:\n");

		char tid_buffer[16];
		char disasm_buffer[32];

		format_to_fixed_z::format_to(tid_buffer, lastof(tid_buffer), "{:d}", tid);

		std::array<const char *, 32> args;
		size_t next_arg = 0;
		auto add_arg = [&](const char *str) {
			assert(next_arg < args.size());
			args[next_arg++] = str;
		};

		add_arg("gdb");
		add_arg("-n");
		add_arg("-p");
		add_arg(tid_buffer);
		add_arg("-batch");

		add_arg("-ex");
		add_arg("echo \\nBacktrace:\\n");
		add_arg("-ex");
		add_arg("bt full 100");

#ifdef WITH_SIGACTION
		if (this->GetMessage() == nullptr && this->signal_instruction_ptr_valid) {
			format_to_fixed_z::format_to(disasm_buffer, lastof(disasm_buffer), "x/1i {:#x}", reinterpret_cast<uintptr_t>(this->signal_instruction_ptr));
			add_arg("-ex");
			add_arg("set disassembly-flavor intel");
			add_arg("-ex");
			add_arg("echo \\nFault instruction:\\n");
			add_arg("-ex");
			add_arg(disasm_buffer);
		}
#endif

		add_arg(nullptr);
		if (!ExecReadStdoutThroughFile("gdb", const_cast<char* const*>(&(args[0])), buffer)) {
			buffer.restore_size(orig);
		}
#endif /* WITH_DBG_GDB */
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
	 * section file name returned from libdl. This is because libbfd also does line numbers,
	 * and knows about more symbols than libdl does.
	 * If demangling support is available, try to demangle whatever symbol name we got back.
	 * If we could find a symbol address from libdl or libbfd, show the offset from that to the frame address.
	 */
	void LogStacktrace(format_target &buffer) const override
	{
		buffer.append("Stacktrace:\n");

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
				size_t saved_position = buffer.size();
				char addr_ptr_buffer[64];
				/* subtract one to get the line before the return address, i.e. the function call line */
				format_to_fixed_z::format_to(addr_ptr_buffer, lastof(addr_ptr_buffer), "{:x}", (char *)trace[i] - (char *)dl_lm->l_addr - 1);
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
				buffer.format(" [{:02}] {:{}} {:<40} ", i, fmt::ptr(trace[i]), ptr_str_size, info.dli_fname);
				size_t start_pos = buffer.size();
				bool result = ExecReadStdout("addr2line", const_cast<char* const*>(args), buffer);
				if (result) {
					std::string_view result_str = std::string_view(buffer.data() + start_pos, buffer.size() - start_pos);
					if (result_str.find("??") == result_str.npos) {
						while (result_str.ends_with("\n\n")) result_str.remove_suffix(1); // Replace double newlines with single newlines
						buffer.restore_size(start_pos + result_str.size());
						continue;
					}
				}
				buffer.restore_size(saved_position);
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
				buffer.format(" [{:02}] {:{}} {:<40} {} + 0x{:X}", i,
						fmt::ptr(trace[i]), ptr_str_size, info.dli_fname, name, (char *)trace[i] - (char *)func_addr);
				free(demangled);
			} else if (dladdr_result && info.dli_fname) {
				buffer.format(" [{:02}] {:{}} {:<40} + 0x{:X}", i,
						fmt::ptr(trace[i]), ptr_str_size, info.dli_fname, (char *)trace[i] - (char *)info.dli_fbase);
			} else {
				ok = false;
			}
			if (ok && file_name != nullptr) {
				buffer.format(" at {}:{}", file_name, line_num);
			}
			if (ok) buffer.push_back('\n');
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
						buffer.format(" [inlined] {:{}} {}", "", ptr_str_size + 36, name);
						free(demangled);
					} else if (file_name) {
						buffer.append(" [inlined]");
					}
					if (file_name != nullptr) {
						buffer.format(" at {}:{}", file_name, line_num);
					}
					buffer.push_back('\n');
				}
			}
#endif /* WITH_BFD */
			if (ok) continue;
#endif /* WITH_DL */
			buffer.format(" [{:02}] {}\n", i, messages[i]);
		}
		free(messages);

/* end of __GLIBC__ */
#else
		buffer.append(" Not supported.\n");
#endif
		buffer.push_back('\n');
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
		this->FlushCrashLogBuffer(buffer);
		internal_fault_saved_buffer = buffer;

		int signum = setjmp(internal_fault_jmp_buf);
		if (signum != 0) {
			if (internal_fault_saved_buffer == nullptr) {
				/* if we get here, things are unrecoverable */
				_exit(43);
			}

			buffer = internal_fault_saved_buffer;
			internal_fault_saved_buffer = nullptr;

			buffer = format_to_fixed_z::format_to(buffer, last,
					"\nSomething went seriously wrong when attempting to fill the '{}' section of the crash log: signal: {} ({}).\n"
					"This is probably due to an invalid pointer or other corrupt data.\n\n",
					section_name, strsignal(signum), signum);

			sigset_t sigs;
			sigemptyset(&sigs);
			for (int signum : _signals_to_handle) {
				sigaddset(&sigs, signum);
			}
			sigprocmask(SIG_UNBLOCK, &sigs, nullptr);

			return buffer;
		}

		format_to_fixed buf(buffer, last - buffer);
		writer(this, buf);
		buffer += buf.size();

		internal_fault_saved_buffer = nullptr;
		return buffer;
	}

	/* virtual */ void CrashLogFaultSectionCheckpoint(format_target &buffer) const override
	{
		if (internal_fault_saved_buffer == nullptr) return;

		char *b = buffer.end();
		if (b > internal_fault_saved_buffer) {
			internal_fault_saved_buffer = b;
			const_cast<CrashLogUnix *>(this)->FlushCrashLogBuffer(b);
		}
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

/* static */ void CrashLog::VersionInfoLog(format_target &buffer)
{
	CrashLogUnix log(CrashLogUnix::DesyncTag{});
	log.FillVersionInfoLog(buffer);
}
