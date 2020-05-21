/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef STRING_FUNC_EXTRA_H
#define STRING_FUNC_EXTRA_H

#include "string_func.h"
#include <string>

static inline void str_validate_inplace(std::string &str, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK)
{
	if (str.empty()) return;
	char *buf = const_cast<char *>(str.c_str());
	str.resize(str_validate(buf, buf + str.size(), settings) - buf);
}

template <typename F>
inline void ProcessLineByLine(char *buf, F line_functor)
{
	char *p = buf;
	char *p2 = buf;
	/* Print output line by line */
	for (; *p2 != '\0'; p2++) {
		if (*p2 == '\n') {
			*p2 = '\0';
			line_functor(p);
			p = p2 + 1;
		}
	}
	if (p < p2) line_functor(p);
}


#endif /* STRING_FUNC_EXTRA_H */
