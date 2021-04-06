/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file thread.h Base of all threads. */

#ifndef THREAD_H
#define THREAD_H

#include "debug.h"
#include "crashlog.h"
#include <system_error>
#include <thread>
#if defined(__MINGW32__)
#include "3rdparty/mingw-std-threads/mingw.thread.h"
#endif

/**
 * Sleep on the current thread for a defined time.
 * @param milliseconds Time to sleep for in milliseconds.
 */
inline void CSleep(int milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

/**
 * Name the thread this function is called on for the debugger.
 * @param name Name to set for the thread..
 */
void SetCurrentThreadName(const char *name);

/**
 * Get the name of the current thread, if any.
 * @param str The start of the buffer.
 * @param last The last char of the buffer.
 * @return Number of chars written to str.
 */
int GetCurrentThreadName(char *str, const char *last);

/**
 * Set the current thread as the "main" thread
 */
void SetSelfAsMainThread();

/**
 * Perform per-thread setup
 */
void PerThreadSetup();

/**
 * Setup thread functionality required for later calls to PerThreadSetup
 */
void PerThreadSetupInit();

/**
 * @return true if the current thread definitely the "main" thread. If in doubt returns false.
 */
bool IsMainThread();

/**
 * @return true if the current thread definitely a "non-main" thread. If in doubt returns false.
 */
bool IsNonMainThread();


/**
 * Start a new thread.
 * @tparam TFn Type of the function to call on the thread.
 * @tparam TArgs Type of the parameters of the thread function.
 * @param thr Pointer to a thread object; may be \c nullptr if a detached thread is wanted.
 * @param name Name of the thread.
 * @param _Fx Function to call on the thread.
 * @param _Ax Arguments for the thread function.
 * @return True if the thread was successfully started, false otherwise.
 */
template<class TFn, class... TArgs>
inline bool StartNewThread(std::thread *thr, const char *name, TFn&& _Fx, TArgs&&... _Ax)
{
#ifndef NO_THREADS
	try {
		std::thread t([] (const char *name, TFn&& F, TArgs&&... A) {
				SetCurrentThreadName(name);
				PerThreadSetup();
				CrashLog::InitThread();
				try {
					/* Call user function with the given arguments. */
					F(A...);
				} catch (...) {
					NOT_REACHED();
				}
			}, std::forward<const char *>(name), std::forward<TFn>(_Fx), std::forward<TArgs>(_Ax)...);

		if (thr != nullptr) {
			*thr = std::move(t);
		} else {
			t.detach();
		}

		return true;
	} catch (const std::system_error& e) {
		/* Something went wrong, the system we are running on might not support threads. */
		DEBUG(misc, 1, "Can't create thread '%s': %s", name, e.what());
	}
#endif

	return false;
}

#endif /* THREAD_H */
