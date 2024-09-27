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
#include <limits>

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

/*
 * Cut down version of std::from_chars, base is fixed at 10.
 * Returns true on success
 */
template <typename T>
inline bool IntFromChars(const char* first, const char* last, T& value)
{
	static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value, "T must be an integer");

	bool negative = false;
	if (std::is_signed<T>::value) {
		if (first != last && *first == '-') {
			first++;
			negative = true;
		}
	}

	T out = 0;
	const char * const start = first;
	while (first != last) {
		const char c = *first;
		if (c >= '0' && c <= '9') {
#ifdef WITH_OVERFLOW_BUILTINS
			if (unlikely(__builtin_mul_overflow(out, 10, &out))) return false;
			if (unlikely(__builtin_add_overflow(out, c - '0', &out))) return false;
#else
			if (unlikely(out > std::numeric_limits<T>::max() / 10)) return false;
			out *= 10;
			if (unlikely(out > (std::numeric_limits<T>::max() - (c - '0')))) return false;
			out += (c - '0');
#endif
			first++;
		} else {
			break;
		}
	}
	if (start == first) return false;
	if (std::is_signed<T>::value) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4146)
#endif
		value = negative ? -out : out;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	} else {
		value = out;
	}
	return true;
}

#endif /* STRING_FUNC_EXTRA_H */
