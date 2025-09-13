/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file utf8_util.hpp Utilities for UTF-8 encoded data.
 */

#ifndef UTF8_UTIL_HPP
#define UTF8_UTIL_HPP

#include "bitmath_func.hpp"

template <char32_t MIN_C, char32_t MAX_C>
bool IsUtf8CharInControlCharRange(const char *str)
{
	/* Ensure 3 byte character sequence */
	static_assert(MIN_C <= MAX_C);
	static_assert(MIN_C >= 0x800);
	static_assert(MAX_C < 0x10000);

	auto check = [&](uint8_t c, const uint8_t base, const uint8_t s, const uint8_t n) {
		return c >= (base + GB(MIN_C, s, n)) && c <= (base + GB(MAX_C, s, n));
	};
	if (check(str[0], 0xE0, 12, 4) && check(str[1], 0x80, 6, 6) && check(str[2], 0x80, 0, 6)) {
		if constexpr (GB(MIN_C, 6, 10) != GB(MAX_C, 6, 10)) {
			/* First two bytes don't match, so do a fuller check. */
			char32_t c = GB(str[0], 0, 4) << 12 | GB(str[1], 0, 6) << 6 | GB(str[2], 0, 6);
			return c >= MIN_C && c <= MAX_C;
		} else {
			return true;
		}
	}
	return false;

}

static constexpr size_t UTF8_CONTROL_CHAR_LENGTH = 3;

#endif /* UTF8_UTIL_HPP */
