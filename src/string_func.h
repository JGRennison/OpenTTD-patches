/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file string_func.h Functions related to low-level strings.
 *
 * @note Be aware of "dangerous" string functions; string functions that
 * have behaviour that could easily cause buffer overruns and such:
 * - strncpy: does not '\0' terminate when input string is longer than
 *   the size of the output string. Use strecpy instead.
 * - [v]snprintf: returns the length of the string as it would be written
 *   when the output is large enough, so it can be more than the size of
 *   the buffer and than can underflow size_t (uint-ish) which makes all
 *   subsequent snprintf-like functions write outside of the buffer. Use
 *   [v]seprintf instead; it will return the number of bytes actually
 *   added so no [v]seprintf will cause outside of bounds writes.
 * - [v]sprintf: does not bounds checking: use [v]seprintf instead.
 */

#ifndef STRING_FUNC_H
#define STRING_FUNC_H

#include <iosfwd>
#include <iterator>

#include "core/bitmath_func.hpp"
#include "core/utf8.hpp"
#include "string_type.h"

char *strecpy(char *dst, const char *src, const char *last, bool quiet_mode = false) NOACCESS(3);
char *stredup(const char *src, const char *last = nullptr) NOACCESS(2);

void strecpy(std::span<char> dst, std::string_view src);

std::string FormatArrayAsHex(std::span<const uint8_t> data, bool upper_case = true);

char *StrMakeValidInPlace(char *str, const char *last, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK) NOACCESS(2);
[[nodiscard]] std::string StrMakeValid(std::string_view str, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK);
void StrMakeValidInPlace(char *str, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK);

inline void StrMakeValidInPlace(std::string &str, StringValidationSettings settings = SVS_REPLACE_WITH_QUESTION_MARK)
{
	if (str.empty()) return;
	char *buf = str.data();
	str.resize(StrMakeValidInPlace(buf, buf + str.size(), settings) - buf);
}

void str_strip_colours(char *str);
const char *strip_leading_colours(const char *str);

inline const char *strip_leading_colours(const std::string &str)
{
	return strip_leading_colours(str.c_str());
}


std::string str_strip_all_scc(const char *str);
char *str_replace_wchar(char *str, const char *last, char32_t find, char32_t replace);
bool strtolower(char *str);
bool strtolower(std::string &str, std::string::size_type offs = 0);

[[nodiscard]] bool StrValid(std::span<const char> str);
void StrTrimInPlace(std::string &str);
std::string_view StrTrimView(std::string_view str);

const char *StrLastPathSegment(const char *path);

inline const char *StrLastPathSegment(const std::string &path)
{
	return StrLastPathSegment(path.c_str());
}

[[nodiscard]] bool StrStartsWithIgnoreCase(std::string_view str, const std::string_view prefix);
[[nodiscard]] bool StrEndsWithIgnoreCase(std::string_view str, const std::string_view suffix);

[[nodiscard]] int StrCompareIgnoreCase(const std::string_view str1, const std::string_view str2);
[[nodiscard]] bool StrEqualsIgnoreCase(const std::string_view str1, const std::string_view str2);
[[nodiscard]] int StrNaturalCompare(std::string_view s1, std::string_view s2, bool ignore_garbage_at_front = false);
[[nodiscard]] bool StrNaturalContains(const std::string_view str, const std::string_view value);
[[nodiscard]] bool StrNaturalContainsIgnoreCase(const std::string_view str, const std::string_view value);

bool ConvertHexToBytes(std::string_view hex, std::span<uint8_t> bytes);

/** Case insensitive comparator for strings, for example for use in std::map. */
struct CaseInsensitiveComparator {
	bool operator()(const std::string_view s1, const std::string_view s2) const { return StrCompareIgnoreCase(s1, s2) < 0; }
};

/**
 * Check if a string buffer is empty.
 *
 * @param s The pointer to the first element of the buffer
 * @return true if the buffer starts with the terminating null-character or
 *         if the given pointer points to nullptr else return false
 */
inline bool StrEmpty(const char *s)
{
	return s == nullptr || s[0] == '\0';
}

/**
 * Get the length of a string, within a limited buffer.
 *
 * @param str The pointer to the first element of the buffer
 * @param maxlen The maximum size of the buffer
 * @return The length of the string
 */
inline size_t ttd_strnlen(const char *str, size_t maxlen)
{
	const char *t;
	for (t = str; static_cast<size_t>(t - str) < maxlen && *t != '\0'; t++) {}
	return t - str;
}

bool IsValidChar(char32_t key, CharSetFilter afilter);

size_t Utf8Decode(char32_t *c, const char *s);
size_t Utf8Encode(char *buf, char32_t c);
size_t Utf8Encode(std::ostreambuf_iterator<char> &buf, char32_t c);
size_t Utf8Encode(std::back_insert_iterator<std::string> &buf, char32_t c);
size_t Utf8TrimString(char *s, size_t maxlen);


inline char32_t Utf8Consume(const char **s)
{
	char32_t c;
	*s += Utf8Decode(&c, *s);
	return c;
}

template <class Titr>
inline char32_t Utf8Consume(Titr &s)
{
	char32_t c;
	s += Utf8Decode(&c, &*s);
	return c;
}

/**
 * Return the length of a UTF-8 encoded character.
 * @param c Unicode character.
 * @return Length of UTF-8 encoding for character.
 */
inline int8_t Utf8CharLen(char32_t c)
{
	if (c < 0x80)       return 1;
	if (c < 0x800)      return 2;
	if (c < 0x10000)    return 3;
	if (c < 0x110000)   return 4;

	/* Invalid valid, we encode as a '?' */
	return 1;
}


/**
 * Return the length of an UTF-8 encoded value based on a single char. This
 * char should be the first byte of the UTF-8 encoding. If not, or encoding
 * is invalid, return value is 0
 * @param c char to query length of
 * @return requested size
 */
inline int8_t Utf8EncodedCharLen(char c)
{
	if (GB(c, 3, 5) == 0x1E) return 4;
	if (GB(c, 4, 4) == 0x0E) return 3;
	if (GB(c, 5, 3) == 0x06) return 2;
	if (GB(c, 7, 1) == 0x00) return 1;

	/* Invalid UTF8 start encoding */
	return 0;
}

/**
 * Retrieve the previous UNICODE character in an UTF-8 encoded string.
 * @param s char pointer pointing to (the first char of) the next character
 * @return a pointer in 's' to the previous UNICODE character's first byte
 * @note The function should not be used to determine the length of the previous
 * encoded char because it might be an invalid/corrupt start-sequence
 */
inline char *Utf8PrevChar(char *s)
{
	char *ret = s;
	while (IsUtf8Part(*--ret)) {}
	return ret;
}

inline const char *Utf8PrevChar(const char *s)
{
	const char *ret = s;
	while (IsUtf8Part(*--ret)) {}
	return ret;
}

size_t Utf8StringLength(const char *s);
size_t Utf8StringLength(const std::string &str);

/**
 * Is the given character a lead surrogate code point?
 * @param c The character to test.
 * @return True if the character is a lead surrogate code point.
 */
inline bool Utf16IsLeadSurrogate(uint c)
{
	return c >= 0xD800 && c <= 0xDBFF;
}

/**
 * Is the given character a lead surrogate code point?
 * @param c The character to test.
 * @return True if the character is a lead surrogate code point.
 */
inline bool Utf16IsTrailSurrogate(uint c)
{
	return c >= 0xDC00 && c <= 0xDFFF;
}

/**
 * Convert an UTF-16 surrogate pair to the corresponding Unicode character.
 * @param lead Lead surrogate code point.
 * @param trail Trail surrogate code point.
 * @return Decoded Unicode character.
 */
inline char32_t Utf16DecodeSurrogate(uint lead, uint trail)
{
	return 0x10000 + (((lead - 0xD800) << 10) | (trail - 0xDC00));
}

/**
 * Decode an UTF-16 character.
 * @param c Pointer to one or two UTF-16 code points.
 * @return Decoded Unicode character.
 */
inline char32_t Utf16DecodeChar(const uint16_t *c)
{
	if (Utf16IsLeadSurrogate(c[0])) {
		return Utf16DecodeSurrogate(c[0], c[1]);
	} else {
		return *c;
	}
}

/**
 * Is the given character a text direction character.
 * @param c The character to test.
 * @return true iff the character is used to influence
 *         the text direction.
 */
inline bool IsTextDirectionChar(char32_t c)
{
	switch (c) {
		case CHAR_TD_LRM:
		case CHAR_TD_RLM:
		case CHAR_TD_LRE:
		case CHAR_TD_RLE:
		case CHAR_TD_LRO:
		case CHAR_TD_RLO:
		case CHAR_TD_PDF:
			return true;

		default:
			return false;
	}
}

inline bool IsPrintable(char32_t c)
{
	if (c < 0x20)   return false;
	if (c < 0xE000) return true;
	if (c < 0xE200) return false;
	return true;
}

/**
 * Check whether UNICODE character is whitespace or not, i.e. whether
 * this is a potential line-break character.
 * @param c UNICODE character to check
 * @return a boolean value whether 'c' is a whitespace character or not
 * @see http://www.fileformat.info/info/unicode/category/Zs/list.htm
 */
inline bool IsWhitespace(char32_t c)
{
	return c == 0x0020 /* SPACE */ || c == 0x3000; /* IDEOGRAPHIC SPACE */
}

/* Needed for NetBSD version (so feature) testing */
#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <sys/param.h>
#endif

/* strcasestr is available for _GNU_SOURCE, BSD and some Apple */
#if defined(_GNU_SOURCE) || (defined(__BSD_VISIBLE) && __BSD_VISIBLE) || (defined(__APPLE__) && (!defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE))) || defined(_NETBSD_SOURCE)
#	undef DEFINE_STRCASESTR
#else
#	define DEFINE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle);
#endif /* strcasestr is available */

/**
 * The use of a struct is so that when used as an argument to seprintf/etc, the buffer lives
 * on the stack with a lifetime which lasts until the end of the statement.
 * This avoids using a static buffer which is thread-unsafe, or needing to call malloc, which would then need to be freed.
 */
struct StrErrorDumper {
	const char *Get(int errornum);
	const char *GetLast();

private:
	char buf[128];
};

#endif /* STRING_FUNC_H */
