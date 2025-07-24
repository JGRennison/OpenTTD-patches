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
#include <optional>
#include <type_traits>

template <typename F>
inline void ProcessLineByLine(std::string_view str, F line_functor)
{
	const char *p = str.data();
	const char *p2 = str.data();
	const char *end = str.data() + str.size();
	/* Process output line by line */
	for (; p2 != end; p2++) {
		if (*p2 == '\n') {
			line_functor(std::string_view(p, p2));
			p = p2 + 1;
		}
	}
	if (p < p2) line_functor(std::string_view(p, p2));
}

/*
 * Cut down version of std::from_chars, base is fixed at 10.
 * @param str The characters to parse
 * @param allow_trailing Whether to allow trailing characters after the integer
 * @return The parsed integer or std::nullopt
 */
template <typename T>
[[nodiscard]] inline std::optional<T> IntFromChars(std::string_view str, bool allow_trailing = false)
{
	static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value, "T must be an integer");

	const char *first = str.data();
	const char *last = first + str.size();

	bool negative = false;
	if constexpr (std::is_signed<T>::value) {
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
			if (unlikely(__builtin_mul_overflow(out, 10, &out))) return std::nullopt;
			if (unlikely(__builtin_add_overflow(out, c - '0', &out))) return std::nullopt;
#else
			if (unlikely(out > std::numeric_limits<T>::max() / 10)) return std::nullopt;
			out *= 10;
			if (unlikely(out > (std::numeric_limits<T>::max() - (c - '0')))) return std::nullopt;
			out += (c - '0');
#endif
			first++;
		} else {
			break;
		}
	}
	if (start == first) return std::nullopt;
	if (first != last && !allow_trailing) return std::nullopt;
	if constexpr (std::is_signed<T>::value) {
		if (negative) out = -out;
	}
	return out;
}

#endif /* STRING_FUNC_EXTRA_H */
