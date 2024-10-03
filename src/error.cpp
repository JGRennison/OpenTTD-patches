/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file error.cpp Reporting of errors. */

#include "stdafx.h"
#include "error_func.h"

[[noreturn]] void not_reached_error(int line, const char *file)
{
	FatalErrorI(fmt::format("NOT_REACHED triggered at line {} of {}", line, file));
}

void assert_str_error(int line, const char *file, const char *expr, std::string_view str)
{
	std::string msg;

	fmt::format_to(std::back_inserter(msg), "Assertion failed at line {} of {}: {}", line, file, expr);
	if (!str.empty()) {
		msg += '\n';
		msg += str;
	}
	FatalErrorI(msg);
}

/* Do strlen here instead of assert call site for common char* case */
void assert_str_error(int line, const char *file, const char *expr, const char *str)
{
	assert_str_error(line, file, expr, std::string_view(str));
}

/* Don't require an extra argument for common case of no extra info */
void assert_str_error(int line, const char *file, const char *expr)
{
	assert_str_error(line, file, expr, {});
}
