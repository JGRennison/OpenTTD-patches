/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string.cpp Handling of C-type strings (char*). */

#include "stdafx.h"
#include "debug.h"
#include "core/alloc_func.hpp"
#include "core/math_func.hpp"
#include "core/utf8.hpp"
#include "error_func.h"
#include "string_func.h"
#include "string_base.h"
#include "stringfilter_type.h"
#include "core/utf8.hpp"

#include "table/control_codes.h"

#include <ctype.h> /* required for tolower() */

#ifdef _MSC_VER
#	define strncasecmp strnicmp
#endif

#ifdef _WIN32
#	include "os/windows/win32.h"
#endif

#ifdef WITH_UNISCRIBE
#	include "os/windows/string_uniscribe.h"
#endif

#ifdef WITH_ICU_I18N
/* Required by ICUSetupCollators. */
#	include <unicode/coll.h>
#	include <unicode/stsearch.h>
#	include <unicode/ustring.h>
#	include "language.h"
#endif /* WITH_ICU_I18N */

#if defined(WITH_COCOA)
#	include "os/macosx/string_osx.h"
#endif

#include "safeguards.h"

#ifdef WITH_ICU_I18N
static std::unique_ptr<icu::Collator> _current_collator;    ///< Collator for the language currently in use.
static std::unique_ptr<icu::RuleBasedCollator> _current_collator_search; ///< Collator for the language currently in use.
static std::unique_ptr<icu::RuleBasedCollator> _current_collator_search_case_insensitive; ///< Collator for the language currently in use.
#endif /* WITH_ICU_I18N */

/**
 * Copies characters from one buffer to another.
 *
 * Copies the source string to the destination buffer with respect of the
 * terminating null-character and the last pointer to the last element in
 * the destination buffer.
 *
 * @note usage: strecpy(dst, src, lastof(dst));
 * @note lastof() applies only to fixed size arrays
 *
 * @param dst The destination buffer
 * @param src The buffer containing the string to copy
 * @param last The pointer to the last element of the destination buffer
 * @param quiet_mode If set to true, emitted warning for truncating the input string is emitted at level 1 instead of 0
 * @return The pointer to the terminating null-character in the destination buffer
 */
char *strecpy(char *dst, const char *src, const char *last, bool quiet_mode)
{
	dbg_assert(dst <= last);
	while (dst != last && *src != '\0') {
		*dst++ = *src++;
	}
	*dst = '\0';

	if (dst == last && *src != '\0') {
#if defined(STRGEN) || defined(SETTINGSGEN)
		FatalError("String too long for destination buffer");
#else /* STRGEN || SETTINGSGEN */
		Debug(misc, quiet_mode ? 1 : 0, "String too long for destination buffer");
#endif /* STRGEN || SETTINGSGEN */
	}
	return dst;
}

/**
 * Copies characters from one buffer to another.
 *
 * Copies the source string to the destination buffer with respect of the
 * terminating null-character and the size of the destination buffer.
 *
 * @note usage: strecpy(dst, src);
 *
 * @param dst The destination buffer
 * @param src The buffer containing the string to copy
 */
void strecpy(std::span<char> dst, std::string_view src)
{
	/* Ensure source string fits with NUL terminator; dst must be at least 1 character longer than src. */
	if (std::empty(dst) || std::size(src) >= std::size(dst) - 1U) {
#if defined(STRGEN) || defined(SETTINGSGEN)
		FatalError("String too long for destination buffer");
#else /* STRGEN || SETTINGSGEN */
		Debug(misc, 0, "String too long for destination buffer");
		src = src.substr(0, std::size(dst) - 1U);
#endif /* STRGEN || SETTINGSGEN */
	}

	auto it = std::copy(std::begin(src), std::end(src), std::begin(dst));
	*it = '\0';
}

/**
 * Create a duplicate of the given string.
 * @param s    The string to duplicate.
 * @param last The last character that is safe to duplicate. If nullptr, the whole string is duplicated.
 * @note The maximum length of the resulting string might therefore be last - s + 1.
 * @return The duplicate of the string.
 */
char *stredup(const char *s, const char *last)
{
	size_t len = last == nullptr ? strlen(s) : ttd_strnlen(s, last - s + 1);
	char *tmp = MallocT<char>(len + 1);
	memcpy(tmp, s, len);
	tmp[len] = '\0';
	return tmp;
}

/**
 * Format a byte array into a continuous hex string.
 * @param data Array to format
 * @return Converted string.
 */
std::string FormatArrayAsHex(std::span<const uint8_t> data, bool upper_case)
{
	format_buffer buf;

	for (uint i = 0; i < data.size(); ++i) {
		if (upper_case) {
			buf.format("{:02X}", data[i]);
		} else {
			buf.format("{:02x}", data[i]);
		}
	}

	return buf.to_string();
}

/**
 * Test if a character is (only) part of an encoded string.
 * @param c Character to test.
 * @returns True iff the character is an encoded string control code.
 */
static bool IsSccEncodedCode(char32_t c)
{
	switch (c) {
		case SCC_RECORD_SEPARATOR:
		case SCC_ENCODED:
		case SCC_ENCODED_INTERNAL:
		case SCC_ENCODED_NUMERIC:
		case SCC_ENCODED_STRING:
			return true;

		default:
			return false;
	}
}

/**
 * Copies the valid (UTF-8) characters from \c str up to \c last to the \c dst.
 * Depending on the \c settings invalid characters can be replaced with a
 * question mark, as well as determining what characters are deemed invalid.
 *
 * It is allowed for \c dst to be the same as \c src, in which case the string
 * is make valid in place.
 * @param dst The destination to write to.
 * @param str The string to validate.
 * @param end The character beyond the end of the string (where the null-terminator is/would be).
 * @param settings The settings for the string validation.
 */
template <class T>
static void StrMakeValid(T &dst, const char *str, const char *end, StringValidationSettings settings)
{
	/* Assume the ABSOLUTE WORST to be in str as it comes from the outside. */

	while (str < end && *str != '\0') {
		size_t len = Utf8EncodedCharLen(*str);
		char32_t c;
		/* If the first byte does not look like the first byte of an encoded
		 * character, i.e. encoded length is 0, then this byte is definitely bad
		 * and it should be skipped.
		 * When the first byte looks like the first byte of an encoded character,
		 * then the remaining bytes in the string are checked whether the whole
		 * encoded character can be there. If that is not the case, this byte is
		 * skipped.
		 * Finally we attempt to decode the encoded character, which does certain
		 * extra validations to see whether the correct number of bytes were used
		 * to encode the character. If that is not the case, the byte is probably
		 * invalid and it is skipped. We could emit a question mark, but then the
		 * logic below cannot just copy bytes, it would need to re-encode the
		 * decoded characters as the length in bytes may have changed.
		 *
		 * The goals here is to get as much valid Utf8 encoded characters from the
		 * source string to the destination string.
		 *
		 * Note: a multi-byte encoded termination ('\0') will trigger the encoded
		 * char length and the decoded length to differ, so it will be ignored as
		 * invalid character data. If it were to reach the termination, then we
		 * would also reach the "last" byte of the string and a normal '\0'
		 * termination will be placed after it.
		 */
		if (len == 0 || str + len > end || len != Utf8Decode(&c, str)) {
			/* Maybe the next byte is still a valid character? */
			str++;
			continue;
		}

		if ((IsPrintable(c) && (c < SCC_SPRITE_START || c > SCC_SPRITE_END)) || (settings.Test(StringValidationSetting::AllowControlCode) && IsSccEncodedCode(c))) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else if (settings.Test(StringValidationSetting::AllowNewline) && c == '\n') {
			*dst++ = *str++;
		} else {
			if (settings.Test(StringValidationSetting::AllowNewline) && c == '\r' && str[1] == '\n') {
				str += len;
				continue;
			}
			str += len;
			if (settings.Test(StringValidationSetting::ReplaceTabCrNlWithSpace) && (c == '\r' || c == '\n' || c == '\t')) {
				/* Replace the tab, carriage return or newline with a space. */
				*dst++ = ' ';
			} else if (settings.Test(StringValidationSetting::ReplaceWithQuestionMark)) {
				/* Replace the undesirable character with a question mark */
				*dst++ = '?';
			}
		}
	}

	/* String termination, if needed, is left to the caller of this function. */
}

/**
 * Scans the string for invalid characters and replaces then with a
 * question mark '?' (if not ignored).
 * @param str The string to validate.
 * @param end The character beyond the end of the string (where the null-terminator is/would be).
 * @param settings The settings for the string validation.
 * @return pointer to the end (where a terminating 0 would go). A terminating 0 is not written.
 */
char *StrMakeValidInPlaceIntl(char *str, const char *end, StringValidationSettings settings)
{
	char *dst = str;
	StrMakeValid(dst, str, end, settings);
	return dst;
}

/**
 * Scans the string for invalid characters and replaces then with a
 * question mark '?' (if not ignored).
 * Only use this function when you are sure the string ends with a '\0';
 * otherwise use StrMakeValidInPlace(str, last, settings) variant.
 * @param str The string (of which you are sure ends with '\0') to validate.
 */
void StrMakeValidInPlace(char *str, StringValidationSettings settings)
{
	if (*str == '\0') return;

	/* We know it is '\0' terminated. */
	char *end = StrMakeValidInPlaceIntl(str, str + strlen(str), settings);
	*end = '\0';
}

void AppendStrMakeValidInPlace(struct format_target &buf, std::string_view str, StringValidationSettings settings)
{
	if (str.empty()) return;

	auto dst_iter = buf.back_inserter();
	StrMakeValid(dst_iter, str.data(), str.data() + str.size(), settings);
}

void AppendStrMakeValidInPlace(std::string &output, std::string_view str, StringValidationSettings settings)
{
	if (str.empty()) return;

	format_buffer buf;
	AppendStrMakeValidInPlace(buf, str, settings);
	output += (std::string_view)buf;
}

/**
 * Copies the valid (UTF-8) characters from \c str to the returned string.
 * Depending on the \c settings invalid characters can be replaced with a
 * question mark, as well as determining what characters are deemed invalid.
 * @param str The string to validate.
 * @param settings The settings for the string validation.
 */
std::string StrMakeValid(std::string_view str, StringValidationSettings settings)
{
	if (str.empty()) return {};

	format_buffer dst;
	AppendStrMakeValidInPlace(dst, str, settings);
	return dst.to_string();
}

/**
 * Checks whether the given string is valid, i.e. contains only
 * valid (printable) characters and is properly terminated.
 * @note std::span is used instead of std::string_view as we are validating fixed-length string buffers, and
 * std::string_view's constructor will assume a C-string that ends with a NUL terminator, which is one of the things
 * we are checking.
 * @param str Span of chars to validate.
 */
bool StrValid(std::span<const char> str)
{
	/* Assume the ABSOLUTE WORST to be in str as it comes from the outside. */
	auto it = std::begin(str);
	auto last = std::prev(std::end(str));

	while (it <= last && *it != '\0') {
		size_t len = Utf8EncodedCharLen(*it);
		/* Encoded length is 0 if the character isn't known.
		 * The length check is needed to prevent Utf8Decode to read
		 * over the terminating '\0' if that happens to be placed
		 * within the encoding of an UTF8 character. */
		if (len == 0 || it + len > last) return false;

		char32_t c;
		len = Utf8Decode(&c, &*it);
		if (!IsPrintable(c) || (c >= SCC_SPRITE_START && c <= SCC_SPRITE_END)) {
			return false;
		}

		it += len;
	}

	return *it == '\0';
}

/**
 * Trim the spaces from given string in place, i.e. the string buffer that
 * is passed will be modified whenever spaces exist in the given string.
 * When there are spaces at the begin, the whole string is moved forward
 * and when there are spaces at the back the '\0' termination is moved.
 * @param str The string to perform the in place trimming on.
 */
void StrTrimInPlace(std::string &str)
{
	str = StrTrimView(str);
}

std::string_view StrTrimView(std::string_view str)
{
	size_t first_pos = str.find_first_not_of(' ');
	if (first_pos == std::string::npos) {
		return std::string_view{};
	}
	size_t last_pos = str.find_last_not_of(' ');
	return str.substr(first_pos, last_pos - first_pos + 1);
}

const char *StrLastPathSegment(const char *path)
{
	const char *best = path;
	for (; *path != '\0'; path++) {
		if (*path == PATHSEPCHAR || *path == '/') {
			if (*(path + 1) != '\0') best = path + 1;
		}
	}
	return best;
}

/**
 * Check whether the given string starts with the given prefix, ignoring case.
 * @param str    The string to look at.
 * @param prefix The prefix to look for.
 * @return True iff the begin of the string is the same as the prefix, ignoring case.
 */
bool StrStartsWithIgnoreCase(std::string_view str, const std::string_view prefix)
{
	if (str.size() < prefix.size()) return false;
	return StrEqualsIgnoreCase(str.substr(0, prefix.size()), prefix);
}

/** Case insensitive implementation of the standard character type traits. */
struct CaseInsensitiveCharTraits : public std::char_traits<char> {
	static bool eq(char c1, char c2) { return toupper(c1) == toupper(c2); }
	static bool ne(char c1, char c2) { return toupper(c1) != toupper(c2); }
	static bool lt(char c1, char c2) { return toupper(c1) <  toupper(c2); }

	static int compare(const char *s1, const char *s2, size_t n)
	{
		while (n-- != 0) {
			if (toupper(*s1) < toupper(*s2)) return -1;
			if (toupper(*s1) > toupper(*s2)) return 1;
			++s1; ++s2;
		}
		return 0;
	}

	static const char *find(const char *s, size_t n, char a)
	{
		for (; n > 0; --n, ++s) {
			if (toupper(*s) == toupper(a)) return s;
		}
		return nullptr;
	}
};

/** Case insensitive string view. */
typedef std::basic_string_view<char, CaseInsensitiveCharTraits> CaseInsensitiveStringView;

/**
 * Check whether the given string ends with the given suffix, ignoring case.
 * @param str    The string to look at.
 * @param suffix The suffix to look for.
 * @return True iff the end of the string is the same as the suffix, ignoring case.
 */
bool StrEndsWithIgnoreCase(std::string_view str, const std::string_view suffix)
{
	if (str.size() < suffix.size()) return false;
	return StrEqualsIgnoreCase(str.substr(str.size() - suffix.size()), suffix);
}

/**
 * Compares two string( view)s, while ignoring the case of the characters.
 * @param str1 The first string.
 * @param str2 The second string.
 * @return Less than zero if str1 < str2, zero if str1 == str2, greater than
 *         zero if str1 > str2. All ignoring the case of the characters.
 */
int StrCompareIgnoreCase(const std::string_view str1, const std::string_view str2)
{
	CaseInsensitiveStringView ci_str1{ str1.data(), str1.size() };
	CaseInsensitiveStringView ci_str2{ str2.data(), str2.size() };
	return ci_str1.compare(ci_str2);
}

/**
 * Compares two string( view)s for equality, while ignoring the case of the characters.
 * @param str1 The first string.
 * @param str2 The second string.
 * @return True iff both strings are equal, barring the case of the characters.
 */
bool StrEqualsIgnoreCase(const std::string_view str1, const std::string_view str2)
{
	if (str1.size() != str2.size()) return false;
	return StrCompareIgnoreCase(str1, str2) == 0;
}

/** Scans the string for colour codes and strips them */
void str_strip_colours(char *str)
{
	char *dst = str;
	char32_t c;
	size_t len;

	for (len = Utf8Decode(&c, str); c != '\0'; len = Utf8Decode(&c, str)) {
		if (c < SCC_BLUE || c > SCC_BLACK) {
			/* Copy the character back. Even if dst is current the same as str
			 * (i.e. no characters have been changed) this is quicker than
			 * moving the pointers ahead by len */
			do {
				*dst++ = *str++;
			} while (--len != 0);
		} else {
			/* Just skip (strip) the colour codes */
			str += len;
		}
	}
	*dst = '\0';
}

/** Advances the pointer over any colour codes at the start of the string */
const char *strip_leading_colours(const char *str)
{
	while (true) {
		char32_t c;
		size_t len = Utf8Decode(&c, str);
		if (c < SCC_BLUE || c > SCC_BLACK) break;
		str += len;
	}

	return str;
}

std::string str_strip_all_scc(const char *str)
{
	std::string out;
	if (!str) return out;

	char32_t c;
	size_t len;

	for (len = Utf8Decode(&c, str); c != '\0'; len = Utf8Decode(&c, str)) {
		if (c < SCC_CONTROL_START || c > SCC_SPRITE_END) {
			/* Copy the characters */
			do {
				out.push_back(*str++);
			} while (--len != 0);
		} else {
			/* Just skip (strip) the control codes */
			str += len;
		}
	}
	return out;
}

/** Scans the string for a wchar and replace it with another wchar
 * @param str The input string view
 * @param find The character to find
 * @param replace The character to replace
 * @return A std::string of the replaced string
 */
void str_replace_wchar(struct format_target &buf, std::string_view str, char32_t find, char32_t replace)
{
	for (char32_t c : Utf8View(str)) {
		buf.push_back_utf8(c == find ? replace : c);
	}
}

/** Scans the string for a wchar and replace it with another wchar
 * @param str The input string view
 * @param find The character to find
 * @param replace The character to replace
 * @return A std::string of the replaced string
 */
std::string str_replace_wchar(std::string_view str, char32_t find, char32_t replace)
{
	format_buffer buf;
	str_replace_wchar(buf, str, find, replace);
	return buf.to_string();
}

/**
 * Checks if a string is contained in another string, while ignoring the case of the characters.
 *
 * @param str The string to search in.
 * @param value The string to search for.
 * @return True if a match was found.
 */
bool StrContainsIgnoreCase(const std::string_view str, const std::string_view value)
{
	CaseInsensitiveStringView ci_str{ str.data(), str.size() };
	CaseInsensitiveStringView ci_value{ value.data(), value.size() };
	return ci_str.find(ci_value) != ci_str.npos;
}

/**
 * Get the length of an UTF-8 encoded string in number of characters
 * and thus not the number of bytes that the encoded string contains.
 * @param s The string to get the length for.
 * @return The length of the string in characters.
 */
size_t Utf8StringLength(std::string_view str)
{
	Utf8View view(str);
	return std::distance(view.begin(), view.end());
}

/**
 * Convert a given ASCII string to lowercase.
 * NOTE: only support ASCII characters, no UTF8 fancy. As currently
 * the function is only used to lowercase data-filenames if they are
 * not found, this is sufficient. If more, or general functionality is
 * needed, look to r7271 where it was removed because it was broken when
 * using certain locales: eg in Turkish the uppercase 'I' was converted to
 * '?', so just revert to the old functionality
 * @param str string to convert
 * @return String has changed.
 */
bool strtolower(char *str)
{
	bool changed = false;
	for (; *str != '\0'; str++) {
		char new_str = tolower(*str);
		changed |= new_str != *str;
		*str = new_str;
	}
	return changed;
}

bool strtolower(std::string &str, std::string::size_type offs)
{
	bool changed = false;
	for (auto ch = str.begin() + offs; ch != str.end(); ++ch) {
		auto new_ch = static_cast<char>(tolower(static_cast<unsigned char>(*ch)));
		changed |= new_ch != *ch;
		*ch = new_ch;
	}
	return changed;
}

/**
 * Only allow certain keys. You can define the filter to be used. This makes
 *  sure no invalid keys can get into an editbox, like BELL.
 * @param key character to be checked
 * @param afilter the filter to use
 * @return true or false depending if the character is printable/valid or not
 */
bool IsValidChar(char32_t key, CharSetFilter afilter)
{
#if !defined(STRGEN) && !defined(SETTINGSGEN)
	extern char32_t GetDecimalSeparatorChar();
#endif
	switch (afilter) {
		case CS_ALPHANUMERAL:  return IsPrintable(key);
		case CS_NUMERAL:       return (key >= '0' && key <= '9');
		case CS_NUMERAL_SIGNED:  return (key >= '0' && key <= '9') || key == '-';
#if !defined(STRGEN) && !defined(SETTINGSGEN)
		case CS_NUMERAL_DECIMAL: return (key >= '0' && key <= '9') || key == '.' || key == GetDecimalSeparatorChar();
		case CS_NUMERAL_DECIMAL_SIGNED: return (key >= '0' && key <= '9') || key == '.' || key == '-' || key == GetDecimalSeparatorChar();
#else
		case CS_NUMERAL_DECIMAL: return (key >= '0' && key <= '9') || key == '.';
		case CS_NUMERAL_DECIMAL_SIGNED: return (key >= '0' && key <= '9') || key == '.' || key == '-';
#endif
		case CS_NUMERAL_SPACE: return (key >= '0' && key <= '9') || key == ' ';
		case CS_ALPHA:         return IsPrintable(key) && !(key >= '0' && key <= '9');
		case CS_HEXADECIMAL:   return (key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F');
		default: NOT_REACHED();
	}
}


/* UTF-8 handling routines */


/**
 * Decode and consume the next UTF-8 encoded character.
 * @param c Buffer to place decoded character.
 * @param s Character stream to retrieve character from.
 * @return Number of characters in the sequence.
 */
size_t Utf8Decode(char32_t *c, const char *s)
{
	dbg_assert(c != nullptr);

	if (!HasBit(s[0], 7)) {
		/* Single byte character: 0xxxxxxx */
		*c = s[0];
		return 1;
	} else if (GB(s[0], 5, 3) == 6) {
		if (IsUtf8Part(s[1])) {
			/* Double byte character: 110xxxxx 10xxxxxx */
			*c = GB(s[0], 0, 5) << 6 | GB(s[1], 0, 6);
			if (*c >= 0x80) return 2;
		}
	} else if (GB(s[0], 4, 4) == 14) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2])) {
			/* Triple byte character: 1110xxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 4) << 12 | GB(s[1], 0, 6) << 6 | GB(s[2], 0, 6);
			if (*c >= 0x800) return 3;
		}
	} else if (GB(s[0], 3, 5) == 30) {
		if (IsUtf8Part(s[1]) && IsUtf8Part(s[2]) && IsUtf8Part(s[3])) {
			/* 4 byte character: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
			*c = GB(s[0], 0, 3) << 18 | GB(s[1], 0, 6) << 12 | GB(s[2], 0, 6) << 6 | GB(s[3], 0, 6);
			if (*c >= 0x10000 && *c <= 0x10FFFF) return 4;
		}
	}

	*c = '?';
	return 1;
}

/**
 * Test if a unicode character is considered garbage to be skipped.
 * @param c Character to test.
 * @returns true iff the character should be skipped.
 */
static bool IsGarbageCharacter(char32_t c)
{
	if (c >= '0' && c <= '9') return false;
	if (c >= 'A' && c <= 'Z') return false;
	if (c >= 'a' && c <= 'z') return false;
	if (c >= SCC_CONTROL_START && c <= SCC_CONTROL_END) return true;
	if (c >= 0xC0 && c <= 0x10FFFF) return false;

	return true;
}

/**
 * Skip some of the 'garbage' in the string that we don't want to use
 * to sort on. This way the alphabetical sorting will work better as
 * we would be actually using those characters instead of some other
 * characters such as spaces and tildes at the begin of the name.
 * @param str The string to skip the initial garbage of.
 * @return The string with the garbage skipped.
 */
static std::string_view SkipGarbage(std::string_view str)
{
	Utf8View view(str);
	auto it = view.begin();
	const auto end = view.end();
	while (it != end && IsGarbageCharacter(*it)) ++it;
	return str.substr(it.GetByteOffset());
}

static int StrNaturalCompareIntl(std::string_view str1, std::string_view str2)
{
	const char *s1 = str1.data();
	const char *s2 = str2.data();
	const char *s1_end = s1 + str1.size();
	const char *s2_end = s2 + str2.size();
	while (s1 != s1_end && s2 != s2_end) {
		if (IsInsideBS(*s1, '0', 10) && IsInsideBS(*s2, '0', 10)) {
			uint n1 = 0;
			uint n2 = 0;
			for (; IsInsideBS(*s1, '0', 10); s1++) {
				n1 = (n1 * 10) + (*s1 - '0');
			}
			for (; IsInsideBS(*s2, '0', 10); s2++) {
				n2 = (n2 * 10) + (*s2 - '0');
			}
			if (n1 != n2) return n1 > n2 ? 1 : -1;
		} else {
			char c1 = tolower(*s1);
			char c2 = tolower(*s2);
			if (c1 != c2) {
				return c1 > c2 ? 1 : -1;
			}
			s1++;
			s2++;
		}
	}
	const bool s1_remaining = (s1 != s1_end);
	const bool s2_remaining = (s2 != s2_end);
	if (s1_remaining && !s2_remaining) {
		return 1;
	} else if (s2_remaining && !s1_remaining) {
		return -1;
	} else {
		return 0;
	}
}

/**
 * Compares two strings using case insensitive natural sort.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @param ignore_garbage_at_front Skip punctuation characters in the front
 * @return Less than zero if s1 < s2, zero if s1 == s2, greater than zero if s1 > s2.
 */
int StrNaturalCompare(std::string_view s1, std::string_view s2, bool ignore_garbage_at_front)
{
	if (ignore_garbage_at_front) {
		s1 = SkipGarbage(s1);
		s2 = SkipGarbage(s2);
	}

#ifdef WITH_ICU_I18N
	if (_current_collator) {
		UErrorCode status = U_ZERO_ERROR;
		int result = _current_collator->compareUTF8(icu::StringPiece(s1.data(), s1.size()), icu::StringPiece(s2.data(), s2.size()), status);
		if (U_SUCCESS(status)) return result;
	}
#endif /* WITH_ICU_I18N */

#if defined(_WIN32) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = OTTDStringCompare(s1, s2);
	if (res != 0) return res - 2; // Convert to normal C return values.
#endif

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = MacOSStringCompare(s1, s2);
	if (res != 0) return res - 2; // Convert to normal C return values.
#endif

	/* Do a manual natural sort comparison if ICU is missing or if we cannot create a collator. */
	return StrNaturalCompareIntl(s1, s2);
}

#ifdef WITH_ICU_I18N
/**
 * Search if a string is contained in another string using the current locale.
 *
 * @param str String to search in.
 * @param value String to search for.
 * @param case_insensitive Search case-insensitive.
 * @return 1 if value was found, 0 if it was not found, or -1 if not supported by the OS.
 */
static int ICUStringContains(const std::string_view str, const std::string_view value, bool case_insensitive)
{
	icu::RuleBasedCollator *coll = case_insensitive ? _current_collator_search_case_insensitive.get() : _current_collator_search.get();
	if (coll != nullptr) {
		UErrorCode status = U_ZERO_ERROR;
		auto u_str = icu::UnicodeString::fromUTF8(icu::StringPiece(str.data(), str.size()));
		auto u_value = icu::UnicodeString::fromUTF8(icu::StringPiece(value.data(), value.size()));
		icu::StringSearch u_searcher(u_value, u_str, coll, nullptr, status);
		if (U_SUCCESS(status)) {
			auto pos = u_searcher.first(status);
			if (U_SUCCESS(status)) return pos != USEARCH_DONE ? 1 : 0;
		}
	}

	return -1;
}

static std::unique_ptr<icu::RuleBasedCollator> MakeICUSearchCollator(icu::Collator::ECollationStrength strength)
{
	UErrorCode status = U_ZERO_ERROR;
	std::unique_ptr<icu::RuleBasedCollator> coll(dynamic_cast<icu::RuleBasedCollator *>(_current_collator->clone()));
	if (coll) {
		coll->setStrength(strength);
		coll->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_OFF, status);
		if (U_FAILURE(status)) coll.reset();
	}
	return coll;
}

void ICUSetupCollators(const char *iso_code)
{
	/* Create a collator instance for our current locale. */
	UErrorCode status = U_ZERO_ERROR;
	_current_collator.reset(icu::Collator::createInstance(icu::Locale(iso_code), status));
	/* Sort number substrings by their numerical value. */
	if (_current_collator) _current_collator->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, status);
	/* Avoid using the collator if it is not correctly set. */
	if (U_FAILURE(status)) {
		_current_collator.reset();
	}

	if (_current_collator) {
		_current_collator_search = MakeICUSearchCollator(icu::Collator::TERTIARY);
		_current_collator_search_case_insensitive = MakeICUSearchCollator(icu::Collator::SECONDARY);
	}
}
#endif /* WITH_ICU_I18N */

/**
 * Checks if a string is contained in another string with a locale-aware comparison that is case sensitive.
 *
 * @param str The string to search in.
 * @param value The string to search for.
 * @return True if a match was found.
 */
[[nodiscard]] bool StrNaturalContains(const std::string_view str, const std::string_view value)
{
#ifdef WITH_ICU_I18N
	int res_u = ICUStringContains(str, value, false);
	if (res_u >= 0) return res_u > 0;
#endif /* WITH_ICU_I18N */

#if defined(_WIN32) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = Win32StringContains(str, value, false);
	if (res >= 0) return res > 0;
#endif

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = MacOSStringContains(str, value, false);
	if (res >= 0) return res > 0;
#endif

	return str.find(value) != std::string_view::npos;
}

/**
 * Checks if a string is contained in another string with a locale-aware comparison that is case insensitive.
 *
 * @param str The string to search in.
 * @param value The string to search for.
 * @return True if a match was found.
 */
[[nodiscard]] bool StrNaturalContainsIgnoreCase(const std::string_view str, const std::string_view value)
{
#ifdef WITH_ICU_I18N
	int res_u = ICUStringContains(str, value, true);
	if (res_u >= 0) return res_u > 0;
#endif /* WITH_ICU_I18N */

#if defined(_WIN32) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = Win32StringContains(str, value, true);
	if (res >= 0) return res > 0;
#endif

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
	int res = MacOSStringContains(str, value, true);
	if (res >= 0) return res > 0;
#endif

	CaseInsensitiveStringView ci_str{ str.data(), str.size() };
	CaseInsensitiveStringView ci_value{ value.data(), value.size() };
	return ci_str.find(ci_value) != CaseInsensitiveStringView::npos;
}

#ifdef WITH_LOCALE_STRING
struct LocaleString {
#ifdef WITH_ICU_I18N
	icu::UnicodeString icu_str;
#endif
#ifdef _WIN32
	UniqueBuffer<wchar_t> win32_str;
#endif
};

LocaleStringList::LocaleStringList() = default;
LocaleStringList::LocaleStringList(LocaleStringList &&) = default;
LocaleStringList::~LocaleStringList() = default;
LocaleStringList& LocaleStringList::operator = (LocaleStringList&&) = default;

void StringFilterSetupLocale(StringFilter &sf)
{
	sf.locale_words.items.clear();
	if (!sf.locale_aware) return;
	sf.locale_words.items.reserve(sf.word_index.size());
	for (const StringFilter::WordState &ws : sf.word_index) {
#ifdef WITH_ICU_I18N
		sf.locale_words.items.emplace_back(icu::UnicodeString::fromUTF8(icu::StringPiece(ws.word.data(), ws.word.size())));
#endif
#ifdef _WIN32
		extern UniqueBuffer<wchar_t> Win32LocaleStringForStringContains(std::string_view str);
		sf.locale_words.items.emplace_back(Win32LocaleStringForStringContains(ws.word));
#endif
	}
}

bool StringFilterAddLocaleLine(StringFilter &sf, std::string_view str)
{
	const bool match_case = sf.case_sensitive != nullptr && *sf.case_sensitive;

	auto found_match = [&](StringFilter::WordState &ws) {
		ws.match = true;
		sf.word_matches++;
	};

	auto fallback_match = [&](StringFilter::WordState &ws) {
		if (match_case) {
			if (str.find(ws.word) != std::string_view::npos) found_match(ws);
		} else {
			if (StrContainsIgnoreCase(str, ws.word)) found_match(ws);
		}
	};

#ifdef WITH_ICU_I18N
	icu::RuleBasedCollator *coll = match_case ? _current_collator_search.get() : _current_collator_search_case_insensitive.get();
	if (coll == nullptr) return false;

	auto u_str = icu::UnicodeString::fromUTF8(icu::StringPiece(str.data(), str.size()));
	for (size_t i = 0; i < sf.word_index.size(); i++) {
		StringFilter::WordState &ws = sf.word_index[i];
		if (ws.match) continue;
		UErrorCode status = U_ZERO_ERROR;
		icu::StringSearch u_searcher(sf.locale_words.items[i].icu_str, u_str, coll, nullptr, status);
		if (U_SUCCESS(status)) {
			auto pos = u_searcher.first(status);
			if (U_SUCCESS(status)) {
				if (pos != USEARCH_DONE) found_match(ws);
				continue;
			}
		}
		/* Fall back to standard search */
		fallback_match(ws);
	}
	return true;
#endif
#ifdef _WIN32
	extern UniqueBuffer<wchar_t> Win32LocaleStringForStringContains(std::string_view str);
	extern int Win32StringContains(std::span<const wchar_t> str, std::span<const wchar_t> value, bool case_insensitive);
	UniqueBuffer<wchar_t> u_str = Win32LocaleStringForStringContains(str);
	if (u_str.size() == 0) return false;

	for (size_t i = 0; i < sf.word_index.size(); i++) {
		StringFilter::WordState &ws = sf.word_index[i];
		if (ws.match) continue;
		const UniqueBuffer<wchar_t> &word = sf.locale_words.items[i].win32_str;
		int result = Win32StringContains(std::span<const wchar_t>{u_str.get(), u_str.size()}, std::span<const wchar_t>{word.get(), word.size()}, !match_case);
		if (result >= 0) {
			if (result > 0) found_match(ws);
			continue;
		}
		/* Fall back to standard search */
		fallback_match(ws);
	}
	return true;
#endif

	return false;
}
#endif /* WITH_LOCALE_STRING */

/**
 * Convert a single hex-nibble to a byte.
 *
 * @param c The hex-nibble to convert.
 * @return The byte the hex-nibble represents, or -1 if it is not a valid hex-nibble.
 */
static int ConvertHexNibbleToByte(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c + 10 - 'A';
	if (c >= 'a' && c <= 'f') return c + 10 - 'a';
	return -1;
}

/**
 * Convert a hex-string to a byte-array, while validating it was actually hex.
 *
 * @param hex The hex-string to convert.
 * @param bytes The byte-array to write the result to.
 *
 * @note The length of the hex-string has to be exactly twice that of the length
 * of the byte-array, otherwise conversion will fail.
 *
 * @return True iff the hex-string was valid and the conversion succeeded.
 */
bool ConvertHexToBytes(std::string_view hex, std::span<uint8_t> bytes)
{
	if (bytes.size() != hex.size() / 2) {
		return false;
	}

	/* Hex-string lengths are always divisible by 2. */
	if (hex.size() % 2 != 0) {
		return false;
	}

	for (size_t i = 0; i < hex.size() / 2; i++) {
		auto hi = ConvertHexNibbleToByte(hex[i * 2]);
		auto lo = ConvertHexNibbleToByte(hex[i * 2 + 1]);

		if (hi < 0 || lo < 0) {
			return false;
		}

		bytes[i] = (hi << 4) | lo;
	}

	return true;
}

#ifdef WITH_UNISCRIBE

/* static */ std::unique_ptr<StringIterator> StringIterator::Create()
{
	return std::make_unique<UniscribeStringIterator>();
}

#elif defined(WITH_ICU_I18N)

#include <unicode/utext.h>
#include <unicode/brkiter.h>

/** String iterator using ICU as a backend. */
class IcuStringIterator : public StringIterator
{
	icu::BreakIterator *char_itr; ///< ICU iterator for characters.
	icu::BreakIterator *word_itr; ///< ICU iterator for words.

	std::vector<UChar> utf16_str;      ///< UTF-16 copy of the string.
	std::vector<size_t> utf16_to_utf8; ///< Mapping from UTF-16 code point position to index in the UTF-8 source string.

public:
	IcuStringIterator() : char_itr(nullptr), word_itr(nullptr)
	{
		UErrorCode status = U_ZERO_ERROR;
		this->char_itr = icu::BreakIterator::createCharacterInstance(icu::Locale(_current_language != nullptr ? _current_language->isocode : "en"), status);
		this->word_itr = icu::BreakIterator::createWordInstance(icu::Locale(_current_language != nullptr ? _current_language->isocode : "en"), status);

		this->utf16_str.push_back('\0');
		this->utf16_to_utf8.push_back(0);
	}

	~IcuStringIterator() override
	{
		delete this->char_itr;
		delete this->word_itr;
	}

	void SetString(std::string_view s) override
	{
		/* Unfortunately current ICU versions only provide rudimentary support
		 * for word break iterators (especially for CJK languages) in combination
		 * with UTF-8 input. As a work around we have to convert the input to
		 * UTF-16 and create a mapping back to UTF-8 character indices. */
		this->utf16_str.clear();
		this->utf16_to_utf8.clear();

		Utf8View view(s);
		for (auto it = view.begin(), end = view.end(); it != end; ++it) {
			size_t idx = it.GetByteOffset();
			char32_t c = *it;
			if (c < 0x10000) {
				this->utf16_str.push_back((UChar)c);
			} else {
				/* Make a surrogate pair. */
				this->utf16_str.push_back((UChar)(0xD800 + ((c - 0x10000) >> 10)));
				this->utf16_str.push_back((UChar)(0xDC00 + ((c - 0x10000) & 0x3FF)));
				this->utf16_to_utf8.push_back(idx);
			}
			this->utf16_to_utf8.push_back(idx);
		}
		this->utf16_str.push_back('\0');
		this->utf16_to_utf8.push_back(s.size());

		UText text = UTEXT_INITIALIZER;
		UErrorCode status = U_ZERO_ERROR;
		utext_openUChars(&text, this->utf16_str.data(), this->utf16_str.size() - 1, &status);
		this->char_itr->setText(&text, status);
		this->word_itr->setText(&text, status);
		this->char_itr->first();
		this->word_itr->first();
	}

	size_t SetCurPosition(size_t pos) override
	{
		/* Convert incoming position to an UTF-16 string index. */
		uint utf16_pos = 0;
		for (uint i = 0; i < this->utf16_to_utf8.size(); i++) {
			if (this->utf16_to_utf8[i] == pos) {
				utf16_pos = i;
				break;
			}
		}

		/* isBoundary has the documented side-effect of setting the current
		 * position to the first valid boundary equal to or greater than
		 * the passed value. */
		this->char_itr->isBoundary(utf16_pos);
		return this->utf16_to_utf8[this->char_itr->current()];
	}

	size_t Next(IterType what) override
	{
		int32_t pos;
		switch (what) {
			case ITER_CHARACTER:
				pos = this->char_itr->next();
				break;

			case ITER_WORD:
				pos = this->word_itr->following(this->char_itr->current());
				/* The ICU word iterator considers both the start and the end of a word a valid
				 * break point, but we only want word starts. Move to the next location in
				 * case the new position points to whitespace. */
				while (pos != icu::BreakIterator::DONE &&
						IsWhitespace(Utf16DecodeChar((const uint16_t *)&this->utf16_str[pos]))) {
					int32_t new_pos = this->word_itr->next();
					/* Don't set it to DONE if it was valid before. Otherwise we'll return END
					 * even though the iterator wasn't at the end of the string before. */
					if (new_pos == icu::BreakIterator::DONE) break;
					pos = new_pos;
				}

				this->char_itr->isBoundary(pos);
				break;

			default:
				NOT_REACHED();
		}

		return pos == icu::BreakIterator::DONE ? END : this->utf16_to_utf8[pos];
	}

	size_t Prev(IterType what) override
	{
		int32_t pos;
		switch (what) {
			case ITER_CHARACTER:
				pos = this->char_itr->previous();
				break;

			case ITER_WORD:
				pos = this->word_itr->preceding(this->char_itr->current());
				/* The ICU word iterator considers both the start and the end of a word a valid
				 * break point, but we only want word starts. Move to the previous location in
				 * case the new position points to whitespace. */
				while (pos != icu::BreakIterator::DONE &&
						IsWhitespace(Utf16DecodeChar((const uint16_t *)&this->utf16_str[pos]))) {
					int32_t new_pos = this->word_itr->previous();
					/* Don't set it to DONE if it was valid before. Otherwise we'll return END
					 * even though the iterator wasn't at the start of the string before. */
					if (new_pos == icu::BreakIterator::DONE) break;
					pos = new_pos;
				}

				this->char_itr->isBoundary(pos);
				break;

			default:
				NOT_REACHED();
		}

		return pos == icu::BreakIterator::DONE ? END : this->utf16_to_utf8[pos];
	}
};

/* static */ std::unique_ptr<StringIterator> StringIterator::Create()
{
	return std::make_unique<IcuStringIterator>();
}

#else

/** Fallback simple string iterator. */
class DefaultStringIterator : public StringIterator
{
	Utf8View string; ///< Current string.
	Utf8View::iterator cur_pos; //< Current iteration position.

public:
	void SetString(std::string_view s) override
	{
		this->string = s;
		this->cur_pos = this->string.begin();
	}

	size_t SetCurPosition(size_t pos) override
	{
		this->cur_pos = this->string.GetIterAtByte(pos);
		return this->cur_pos.GetByteOffset();
	}

	size_t Next(IterType what) override
	{
		const auto end = this->string.end();
		/* Already at the end? */
		if (this->cur_pos >= end) return END;

		switch (what) {
			case ITER_CHARACTER:
				++this->cur_pos;
				return this->cur_pos.GetByteOffset();

			case ITER_WORD:
				/* Consume current word. */
				while (this->cur_pos != end && !IsWhitespace(*this->cur_pos)) {
					++this->cur_pos;
				}
				/* Consume whitespace to the next word. */
				while (this->cur_pos != end && IsWhitespace(*this->cur_pos)) {
					++this->cur_pos;
				}
				return this->cur_pos.GetByteOffset();

			default:
				NOT_REACHED();
		}

		return END;
	}

	size_t Prev(IterType what) override
	{
		const auto begin = this->string.begin();
		/* Already at the beginning? */
		if (this->cur_pos == begin) return END;

		switch (what) {
			case ITER_CHARACTER:
				--this->cur_pos;
				return this->cur_pos.GetByteOffset();

			case ITER_WORD:
				/* Consume preceding whitespace. */
				do {
					--this->cur_pos;
				} while (this->cur_pos != begin && IsWhitespace(*this->cur_pos));
				/* Consume preceding word. */
				while (this->cur_pos != begin && !IsWhitespace(*this->cur_pos)) {
					--this->cur_pos;
				}
				/* Move caret back to the beginning of the word. */
				if (IsWhitespace(*this->cur_pos)) ++this->cur_pos;
				return this->cur_pos.GetByteOffset();

			default:
				NOT_REACHED();
		}

		return END;
	}
};

#if defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN)
/* static */ std::unique_ptr<StringIterator> StringIterator::Create()
{
	std::unique_ptr<StringIterator> i = OSXStringIterator::Create();
	if (i != nullptr) return i;

	return std::make_unique<DefaultStringIterator>();
}
#else
/* static */ std::unique_ptr<StringIterator> StringIterator::Create()
{
	return std::make_unique<DefaultStringIterator>();
}
#endif /* defined(WITH_COCOA) && !defined(STRGEN) && !defined(SETTINGSGEN) */

#endif

const char *StrErrorDumper::Get(int errornum)
{
#if defined(_WIN32)
	if (strerror_s(this->buf, lengthof(this->buf), errornum) == 0) {
		return this->buf;
	}
#else
	struct StrErrorRHelper {
		static bool Success(char *result) { return true; }      ///< GNU-specific
		static bool Success(int result) { return result == 0; } ///< XSI-compliant

		static const char *GetString(char *result, const char *buffer) { return result; } ///< GNU-specific
		static const char *GetString(int result, const char *buffer) { return buffer; }   ///< XSI-compliant
	};

	auto result = strerror_r(errornum, this->buf, lengthof(this->buf));
	if (StrErrorRHelper::Success(result)) {
		return StrErrorRHelper::GetString(result, this->buf);
	}
#endif

	format_to_fixed_z::format_to(this->buf, lastof(this->buf), "Unknown error {}", errornum);
	return this->buf;
}

const char *StrErrorDumper::GetLast()
{
	return this->Get(errno);
}
