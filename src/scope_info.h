/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file scope_info.h Scope info debug functions. */

#ifndef SCOPE_INFO_H
#define SCOPE_INFO_H

#ifdef USE_SCOPE_INFO
#include <functional>
#include <vector>

struct Vehicle;

extern std::vector<std::function<int(char *, const char *)>> _scope_stack;

struct scope_info_func_obj {
	scope_info_func_obj(std::function<int(char *, const char *)> func)
	{
		_scope_stack.emplace_back(std::move(func));
	}

	scope_info_func_obj(const scope_info_func_obj &copysrc) = delete;

	~scope_info_func_obj()
	{
		_scope_stack.pop_back();
	}
};

int WriteScopeLog(char *buf, const char *last);

#define SCOPE_INFO_PASTE(a, b) a ## b

/**
 * This creates a lambda in the current scope with the specified capture which outputs the given args as a format string.
 * This lambda is then captured by reference in a std::function which is pushed onto the scope stack
 * The scope stack is popped at the end of the scope
 */
#define SCOPE_INFO_FMT(capture, ...) auto SCOPE_INFO_PASTE(_sc_lm_, __LINE__) = capture (char *buf, const char *last) { return seprintf(buf, last, __VA_ARGS__); }; scope_info_func_obj SCOPE_INFO_PASTE(_sc_obj_, __LINE__) ([&](char *buf, const char *last) -> int { return SCOPE_INFO_PASTE(_sc_lm_, __LINE__) (buf, last); });

// helper functions
char *DumpCompanyInfo(int company_id);
char *DumpVehicleInfo(const Vehicle *v);

#else /* USE_SCOPE_INFO */

#define SCOPE_INFO_FMT(...)

#endif /* USE_SCOPE_INFO */

#endif /* SCOPE_INFO_H */
