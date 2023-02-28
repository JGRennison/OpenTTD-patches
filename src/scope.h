/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file scope.h Simple scope guard... */

#ifndef SCOPE_H
#define SCOPE_H

#include <utility>

template <typename T>
class scope_exit_obj {
	T f;
	bool shouldexec;


public:

	scope_exit_obj(T &&func)
		: f(std::move(func)), shouldexec(true) { }

	scope_exit_obj(const scope_exit_obj &copysrc) = delete;
	scope_exit_obj(scope_exit_obj &&movesrc)
		: f(std::move(movesrc.f)), shouldexec(movesrc.shouldexec) {
		movesrc.shouldexec = false;
	}

	~scope_exit_obj() {
		exec();
	}

	void exec() {
		if (shouldexec) {
			f();
			shouldexec = false;
		}
	}

	void cancel() {
		shouldexec = false;
	}
};

template <typename T>
scope_exit_obj<typename std::decay<T>::type> scope_guard(T &&func) {
	return scope_exit_obj<typename std::decay<T>::type>(std::forward<T>(func));
}

#endif /* SCOPE_H */
