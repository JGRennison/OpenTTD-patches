/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings.cpp Handling of translated strings. */

#include "stdafx.h"
#include "currency.h"
#include "station_base.h"
#include "town.h"
#include "waypoint_base.h"
#include "depot_base.h"
#include "industry.h"
#include "newgrf_text.h"
#include "fileio_func.h"
#include "signs_base.h"
#include "fontdetection.h"
#include "error.h"
#include "error_func.h"
#include "strings_func.h"
#include "core/string_builder.hpp"
#include "core/utf8.hpp"
#include "rev.h"
#include "core/endian_func.hpp"
#include "date_func.h"
#include "vehicle_base.h"
#include "engine_base.h"
#include "language.h"
#include "townname_func.h"
#include "string_func.h"
#include "company_base.h"
#include "smallmap_gui.h"
#include "window_func.h"
#include "debug.h"
#include "unit_conversion.h"
#include "tracerestrict.h"
#include "game/game_text.hpp"
#include "network/network_content_gui.h"
#include "newgrf_engine.h"
#include "tbtr_template_vehicle_func.h"
#include "core/backup_type.hpp"
#include "gfx_layout.h"
#include "core/y_combinator.hpp"
#include "3rdparty/svector/svector.h"
#include <stack>
#include <charconv>
#include <cmath>
#include <optional>

#include "table/strings.h"
#include "table/control_codes.h"

#include "strings_internal.h"

#include "safeguards.h"

std::string _config_language_file;                ///< The file (name) stored in the configuration.
LanguageList _languages;                          ///< The actual list of language meta data.
const LanguageMetadata *_current_language = nullptr; ///< The currently loaded language.

TextDirection _current_text_dir = TD_LTR; ///< Text direction of the currently selected language.

std::string _temp_special_strings[16];

/**
 * Get the next parameter from our parameters.
 * This updates the offset, so the next time this is called the next parameter
 * will be read.
 * @return The next parameter.
 */
const StringParameter &StringParameters::GetNextParameterReference()
{
	assert(this->next_type == 0 || (SCC_CONTROL_START <= this->next_type && this->next_type <= SCC_CONTROL_END));
	if (this->offset >= this->parameters.size()) {
		throw std::out_of_range("Trying to read invalid string parameter");
	}

	auto &param = this->parameters[this->offset++];
	if (param.type != 0 && param.type != this->next_type) {
		this->next_type = 0;
		throw std::out_of_range("Trying to read string parameter with wrong type");
	}
	param.type = this->next_type;
	this->next_type = 0;
	return param;
}

/**
 * Encode a string with no parameters into an encoded string.
 * @param str The StringID to format.
 * @returns The encoded string.
 */
EncodedString GetEncodedString(StringID str)
{
	return GetEncodedStringWithArgs(str, {});
}

/**
 * Encode a string with its parameters into an encoded string.
 * The encoded string can be stored and decoded later without requiring parameters to be stored separately.
 * @param str The StringID to format.
 * @param params The parameters of the string.
 * @returns The encoded string.
 */
EncodedString GetEncodedStringWithArgs(StringID str, std::span<const StringParameter> params)
{
	std::string result;
	auto output = std::back_inserter(result);
	Utf8Encode(output, SCC_ENCODED_INTERNAL);
	fmt::format_to(output, "{:X}", str);

	struct visitor {
		std::back_insert_iterator<std::string> &output;

		void operator()(const std::monostate &) {}

		void operator()(const uint64_t &arg)
		{
			Utf8Encode(output, SCC_ENCODED_NUMERIC);
			fmt::format_to(this->output, "{:X}", arg);
		}

		void visit_string(std::string_view value)
		{
#ifdef WITH_ASSERT
			/* Don't allow an encoded string to contain another encoded string. */
			if (!value.empty()) {
				char32_t c;
				const char *p = value.data();
				if (Utf8Decode(&c, p)) {
					assert(c != SCC_ENCODED && c != SCC_ENCODED_INTERNAL);
				}
			}
#endif /* WITH_ASSERT */
			Utf8Encode(output, SCC_ENCODED_STRING);
			fmt::format_to(this->output, "{}", value);
		}

		void operator()(const std::string &value)
		{
			this->visit_string(value);
		}

		void operator()(const StringParameterDataStringView &value)
		{
			this->visit_string(value.view);
		}
	};

	visitor v{output};
	for (const auto &param : params) {
		*output = SCC_RECORD_SEPARATOR;
		std::visit(v, param.data);
	}

	return EncodedString{std::move(result)};
}

/**
 * Replace a parameter of this EncodedString.
 * @note If the string cannot be decoded for some reason, an empty EncodedString will be returned instead.
 * @param param Index of parameter to replace.
 * @param data New data for parameter.
 * @returns a new EncodedString with the parameter replaced.
 */
EncodedString EncodedString::ReplaceParam(size_t param, StringParameter &&data) const
{
	if (this->empty()) return {};

	std::vector<StringParameter> params;

	/* We need char * for std::from_chars. Iterate the underlying data, as string's own iterators may interfere. */
	const char *p = this->string.data();
	const char *e = this->string.data() + this->string.length();

	char32_t c = Utf8Consume(p);
	if (c != SCC_ENCODED_INTERNAL) return {};

	StringID str;
	auto result = std::from_chars(p, e, str, 16);
	if (result.ec != std::errc()) return {};
	if (result.ptr != e && *result.ptr != SCC_RECORD_SEPARATOR) return {};
	p = result.ptr;

	while (p != e) {
		auto s = ++p;

		/* Find end of the parameter. */
		for (; p != e && *p != SCC_RECORD_SEPARATOR; ++p) {}

		if (s == p) {
			/* This is an empty parameter. */
			params.emplace_back(std::monostate{});
			continue;
		}

		/* Get the parameter type. */
		char32_t parameter_type;
		size_t len = Utf8Decode(&parameter_type, s);
		s += len;

		switch (parameter_type) {
			case SCC_ENCODED_NUMERIC: {
				uint64_t value;
				result = std::from_chars(s, p, value, 16);
				if (result.ec != std::errc() || result.ptr != p) return {};
				params.emplace_back(value);
				break;
			}

			case SCC_ENCODED_STRING: {
				params.emplace_back(std::string(s, p));
				break;
			}

			default:
				/* Unknown parameter, make it blank. */
				params.emplace_back(std::monostate{});
				break;
		}
	}

	if (param >= std::size(params)) return {};
	params[param] = data;
	return GetEncodedStringWithArgs(str, params);
}

/**
 * Decode the encoded string and append in place into an existing format_buffer.
 * @param result The format_buffer to append to.
 */
void EncodedString::AppendDecodedStringInPlace(format_buffer &result) const
{
	AppendStringInPlace(result, STR_JUST_RAW_STRING, this->string);
}

/**
 * Decode the encoded string.
 * @returns Decoded raw string.
 */
std::string EncodedString::GetDecodedString() const
{
	return GetString(STR_JUST_RAW_STRING, this->string);
}

/**
 * Get some number that is suitable for string size computations.
 * @param count Number of digits which shall be displayable.
 * @param size  Font of the number
 * @returns Number to use for string size computations.
 */
uint64_t GetParamMaxDigits(uint count, FontSize size)
{
	auto [front, next] = GetBroadestDigit(size);
	uint64_t val = count > 1 ? front : next;
	for (; count > 1; count--) {
		val = 10 * val + next;
	}
	return val;
}

/**
 * Get some number that is suitable for string size computations.
 * @param max_value The biggest value which shall be displayed.
 *                  For the result only the number of digits of \a max_value matter.
 * @param min_count Minimum number of digits independent of \a max.
 * @param size  Font of the number
 * @returns Number to use for string size computations.
 */
uint64_t GetParamMaxValue(uint64_t max_value, uint min_count, FontSize size)
{
	uint num_digits = GetBase10DigitsRequired(max_value);
	return GetParamMaxDigits(std::max(min_count, num_digits), size);
}

static void StationGetSpecialString(StringBuilder builder, StationFacilities x);
static bool GetSpecialNameString(StringBuilder builder, StringID string, StringParameters &args);

static void FormatString(StringBuilder builder, std::string_view str, StringParameters &args, uint case_index = 0, bool game_script = false, bool dry_run = false);

/**
 * Parse most format codes within a string and write the result to a buffer.
 * This is a wrapper for a span of StringParameter which creates the StringParameters state and forwards to the regular call.
 * @param builder The string builder to write the final string to.
 * @param str Pointer to string to format.
 * @param params The span of parameters to pass.
 * @param case_index The current case index.
 * @param game_script True when doing GameScript text processing.
 * @param dry_run True when the args' type data is not yet initialized.
 */
static void FormatString(StringBuilder builder, std::string_view str, std::span<StringParameter> params, uint case_index = 0, bool game_script = false, bool dry_run = false)
{
	StringParameters tmp_params{params};
	FormatString(builder, str, tmp_params, case_index, game_script, dry_run);
}

struct LanguagePack : public LanguagePackHeader {
	char data[]; // list of strings

	inline void operator delete(void *ptr) { ::operator delete (ptr); }
};

struct LanguagePackDeleter {
	void operator()(LanguagePack *langpack)
	{
		/* LanguagePack is in fact reinterpreted char[], we need to reinterpret it back to free it properly. */
		delete[] reinterpret_cast<uint8_t *>(langpack);
	}
};

struct LoadedLanguagePack {
	std::unique_ptr<LanguagePack, LanguagePackDeleter> langpack;

	std::vector<std::string_view> strings;

	std::array<uint, TEXT_TAB_END> langtab_num;   ///< Offset into langpack offs
	std::array<uint, TEXT_TAB_END> langtab_start; ///< Offset into langpack offs

	std::string list_separator; ///< Current list separator string.
};

static LoadedLanguagePack _langpack;

static bool _scan_for_gender_data = false;  ///< Are we scanning for the gender of the current string? (instead of formatting it)

/**
 * Get the list separator string for the current language.
 * @returns string containing list separator to use.
 */
std::string_view GetListSeparator()
{
	return _langpack.list_separator;
}

std::string_view GetStringPtr(StringID string)
{
	switch (GetStringTab(string)) {
		case TEXT_TAB_GAMESCRIPT_START: return GetGameStringPtr(GetStringIndex(string));
		/* 0xD0xx and 0xD4xx IDs have been converted earlier. */
		case TEXT_TAB_OLD_NEWGRF: NOT_REACHED();
		case TEXT_TAB_NEWGRF_START: return GetGRFStringPtr(GetStringIndex(string));
		default: {
			const size_t offset = _langpack.langtab_start[GetStringTab(string)] + GetStringIndex(string).base();
			if (offset < _langpack.strings.size()) return _langpack.strings[offset];
			return "(undefined string)";
		}
	}
}

/**
 * Get a parsed string with most special stringcodes replaced by the string parameters.
 * @param builder     The builder of the string.
 * @param string      The ID of the string to parse.
 * @param args        Arguments for the string.
 * @param case_index  The "case index". This will only be set when FormatString wants to print the string in a different case.
 * @param game_script The string is coming directly from a game script.
 */
void GetStringWithArgs(StringBuilder builder, StringID string, StringParameters &args, uint case_index, bool game_script)
{
	if (string == 0) {
		GetStringWithArgs(builder, STR_UNDEFINED, args);
		return;
	}

	StringIndexInTab index = GetStringIndex(string);
	StringTab tab = GetStringTab(string);

	switch (tab) {
		case TEXT_TAB_TOWN:
			if (IsInsideMM(string, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_END) && !game_script) {
				GenerateTownNameString(builder, string - SPECSTR_TOWNNAME_START, args.GetNextParameter<uint32_t>());
				return;
			}
			break;

		case TEXT_TAB_SPECIAL:
			if (!game_script) {
				if (GetSpecialNameString(builder, string, args)) return;
			}
			if (index < lengthof(_temp_special_strings) && !game_script) {
				FormatString(builder, _temp_special_strings[index.base()].c_str(), args, case_index);
				return;
			}
			break;

		case TEXT_TAB_OLD_CUSTOM:
			/* Old table for custom names. This is no longer used */
			if (!game_script) {
				FatalError("Incorrect conversion of custom name string.");
			}
			break;

		case TEXT_TAB_GAMESCRIPT_START: {
			FormatString(builder, GetGameStringPtr(index), args, case_index, true);
			return;
		}

		case TEXT_TAB_OLD_NEWGRF:
			NOT_REACHED();

		case TEXT_TAB_NEWGRF_START: {
			FormatString(builder, GetGRFStringPtr(index), args, case_index);
			return;
		}

		default:
			break;
	}

	if (index >= _langpack.langtab_num[tab]) {
		if (game_script) {
			return GetStringWithArgs(builder, STR_UNDEFINED, args);
		}
		FatalError("String 0x{:X} is invalid. You are probably using an old version of the .lng file.\n", string);
	}

	FormatString(builder, GetStringPtr(string), args, case_index);
}

/**
 * Get a parsed string with most special stringcodes replaced by the string parameters.
 * @param builder The builder of the string.
 * @param string The ID of the string to parse.
 * @param args Span of arguments for the string.
 * @param case_index The "case index". This will only be set when FormatString wants to print the string in a different case.
 * @param game_script The string is coming directly from a game script.
 */
void GetStringWithArgs(StringBuilder builder, StringID string, std::span<StringParameter> params, uint case_index, bool game_script)
{
	StringParameters tmp_params{params};
	GetStringWithArgs(builder, string, tmp_params, case_index, game_script);
}

/**
 * Resolve the given StringID into a std::string with formatting but no parameters.
 * @param string The unique identifier of the translatable string.
 * @return The std::string of the translated string.
 */
std::string GetString(StringID string)
{
	format_buffer buffer;
	GetStringWithArgs(StringBuilder(buffer), string, {});
	return buffer.to_string();
}

/**
 * Resolve the given StringID and append in place with most special stringcodes replaced by the string parameters.
 * @param result The format_target to append the translated string.
 * @param string The unique identifier of the translatable string.
 * @param args Span of arguments for the string.
 */
void AppendStringWithArgsInPlace(format_target &result, StringID string, std::span<StringParameter> args)
{
	if (unlikely(result.has_overflowed())) return;

	StringParameters params{args};
	GetStringWithArgs(result, string, params);
}

/**
 * Resolve the given StringID and append in place with most special stringcodes replaced by the string parameters.
 * @param result The std::string to append the translated string.
 * @param string The unique identifier of the translatable string.
 * @param args Span of arguments for the string.
 */
void AppendStringWithArgsInPlace(std::string &result, StringID string, std::span<StringParameter> args)
{
	format_buffer buffer;
	AppendStringWithArgsInPlace(buffer, string, args);
	result += (std::string_view)buffer;
}

/**
 * Get a parsed string with most special stringcodes replaced by the string parameters.
 * @param string The ID of the string to parse.
 * @param args   Arguments for the string.
 * @return The parsed string.
 */
std::string GetStringWithArgs(StringID string, std::span<StringParameter> args)
{
	format_buffer result;
	StringBuilder builder(result);
	GetStringWithArgs(builder, string, args);
	return result.to_string();
}

/**
 * Format a number into a string.
 * @param builder   the string builder to write to
 * @param number    the number to write down
 * @param last      the last element in the buffer
 * @param separator the thousands-separator to use
 * @param zerofill  minimum number of digits to print for the integer part. The number will be filled with zeros at the front if necessary.
 * @param fractional_digits number of fractional digits to display after a decimal separator. The decimal separator is inserted
 *                          in front of the \a fractional_digits last digit of \a number.
 */
static void FormatNumber(StringBuilder builder, int64_t number, const char *separator, int zerofill = 1, int fractional_digits = 0)
{
	static const int max_digits = 20;
	uint64_t divisor = 10000000000000000000ULL;
	zerofill += fractional_digits;
	int thousands_offset = (max_digits - fractional_digits - 1) % 3;

	if (number < 0) {
		builder += '-';
		number = -number;
	}

	uint64_t num = number;
	uint64_t tot = 0;
	for (int i = 0; i < max_digits; i++) {
		if (i == max_digits - fractional_digits) {
			const char *decimal_separator = _settings_game.locale.digit_decimal_separator.c_str();
			if (StrEmpty(decimal_separator)) decimal_separator = _langpack.langpack->digit_decimal_separator;
			builder += decimal_separator;
		}

		uint64_t quot = 0;
		if (num >= divisor) {
			quot = num / divisor;
			num = num % divisor;
		}
		if ((tot |= quot) || i >= max_digits - zerofill) {
			builder += '0' + quot; // quot is a single digit
			if ((i % 3) == thousands_offset && i < max_digits - 1 - fractional_digits) builder += separator;
		}

		divisor /= 10;
	}
}

static void FormatCommaNumber(StringBuilder builder, int64_t number, int fractional_digits = 0)
{
	const char *separator = _settings_game.locale.digit_group_separator.c_str();
	if (StrEmpty(separator)) separator = _langpack.langpack->digit_group_separator;
	FormatNumber(builder, number, separator, 1, fractional_digits);
}

static void FormatNoCommaNumber(StringBuilder builder, int64_t number)
{
	FormatNumber(builder, number, "");
}

static void FormatZerofillNumber(StringBuilder builder, int64_t number, int count)
{
	FormatNumber(builder, number, "", count);
}

static void FormatHexNumber(StringBuilder builder, uint64_t number)
{
	builder.Format("0x{:X}", number);
}

char32_t GetDecimalSeparatorChar()
{
	char32_t decimal_char = '.';
	const char *decimal_separator = _settings_game.locale.digit_decimal_separator.c_str();
	if (StrEmpty(decimal_separator)) decimal_separator = _langpack.langpack->digit_decimal_separator;
	if (!StrEmpty(decimal_separator)) Utf8Decode(&decimal_char, decimal_separator);
	return decimal_char;
}

/**
 * Format a given number as a number of bytes with the SI prefix.
 * @param builder the string builder to write to
 * @param number  the number of bytes to write down
 */
static void FormatBytes(StringBuilder builder, int64_t number)
{
	assert(number >= 0);

	/*                                   1   2^10  2^20  2^30  2^40  2^50  2^60 */
	const char * const iec_prefixes[] = {"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei"};
	uint id = 1;
	while (number >= 1024 * 1024) {
		number /= 1024;
		id++;
	}

	const char *decimal_separator = _settings_game.locale.digit_decimal_separator.c_str();
	if (StrEmpty(decimal_separator)) decimal_separator = _langpack.langpack->digit_decimal_separator;

	if (number < 1024) {
		id = 0;
		builder.Format("{}", number);
	} else if (number < 1024 * 10) {
		builder.Format("{}{}{:02}", number / 1024, decimal_separator, (number % 1024) * 100 / 1024);
	} else if (number < 1024 * 100) {
		builder.Format("{}{}{:01}", number / 1024, decimal_separator, (number % 1024) * 10 / 1024);
	} else {
		assert(number < 1024 * 1024);
		builder.Format("{}", number / 1024);
	}

	assert(id < lengthof(iec_prefixes));
	builder.Format(NBSP "{}B", iec_prefixes[id]);
}

static void FormatStateTicksHHMMString(StringBuilder builder, StateTicks ticks, uint case_index)
{
	TickMinutes minutes = _settings_time.ToTickMinutes(ticks);
	char hour[3], minute[3];
	format_to_fixed_z::format_to(hour,   lastof(hour),   "{:02}", minutes.ClockHour());
	format_to_fixed_z::format_to(minute, lastof(minute), "{:02}", minutes.ClockMinute());
	auto tmp_params = MakeParameters(hour, minute);
	FormatString(builder, GetStringPtr(STR_FORMAT_DATE_MINUTES), tmp_params, case_index);
}

static void FormatTimeHHMMString(StringBuilder builder, uint time, uint case_index)
{
	char hour[9], minute[3];
	format_to_fixed_z::format_to(hour,   lastof(hour),   "{:02}", (int) time / 100);
	format_to_fixed_z::format_to(minute, lastof(minute), "{:02}", (int) time % 100);
	auto tmp_params = MakeParameters(hour, minute);
	return FormatString(builder, GetStringPtr(STR_FORMAT_DATE_MINUTES), tmp_params, case_index);
}

static void FormatYmdString(StringBuilder builder, CalTime::Date date, uint case_index)
{
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(date);

	auto tmp_params = MakeParameters(STR_DAY_NUMBER_1ST + ymd.day - 1, STR_MONTH_ABBREV_JAN + ymd.month, ymd.year);
	FormatString(builder, GetStringPtr(STR_FORMAT_DATE_LONG), tmp_params, case_index);
}

static void FormatMonthAndYear(StringBuilder builder, CalTime::Date date, uint case_index)
{
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(date);

	auto tmp_params = MakeParameters(STR_MONTH_JAN + ymd.month, ymd.year);
	FormatString(builder, GetStringPtr(STR_FORMAT_DATE_SHORT), tmp_params, case_index);
}

static void FormatTinyOrISODate(StringBuilder builder, CalTime::Date date, StringID str)
{
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(date);

	/* Day and month are zero-padded with ZEROFILL_NUM, hence the two 2s. */
	auto tmp_params = MakeParameters(ymd.day, 2, ymd.month + 1, 2, ymd.year);
	FormatString(builder, GetStringPtr(str), tmp_params);
}

static void FormatGenericCurrency(StringBuilder builder, const CurrencySpec *spec, Money number, bool compact)
{
	/* We are going to make number absolute for printing, so
	 * keep this piece of data as we need it later on */
	bool negative = number < 0;

	number *= spec->rate;

	/* convert from negative */
	if (number < 0) {
		builder.Utf8Encode(SCC_PUSH_COLOUR);
		builder.Utf8Encode(SCC_RED);
		builder += '-';
		number = -number;
	}

	/* Add prefix part, following symbol_pos specification.
	 * Here, it can can be either 0 (prefix) or 2 (both prefix and suffix).
	 * The only remaining value is 1 (suffix), so everything that is not 1 */
	if (spec->symbol_pos != 1) builder += spec->prefix;

	StringID number_str = STR_NULL;

	/* For huge numbers, compact the number. */
	if (compact) {
		/* Take care of the thousand rounding. Having 1 000 000 k
		 * and 1 000 M is inconsistent, so always use 1 000 M. */
		if (number >= Money(1'000'000'000'000'000) - 500'000'000) {
			number = (number + Money(500'000'000'000)) / Money(1'000'000'000'000);
			number_str = STR_CURRENCY_SHORT_TERA;
		} else if (number >= Money(1'000'000'000'000) - 500'000) {
			number = (number + 500'000'000) / 1'000'000'000;
			number_str = STR_CURRENCY_SHORT_GIGA;
		} else if (number >= 1'000'000'000 - 500) {
			number = (number + 500'000) / 1'000'000;
			number_str = STR_CURRENCY_SHORT_MEGA;
		} else if (number >= 1'000'000) {
			number = (number + 500) / 1'000;
			number_str = STR_CURRENCY_SHORT_KILO;
		}
	}

	const char *separator = _settings_game.locale.digit_group_separator_currency.c_str();
	if (StrEmpty(separator)) separator = GetCurrency().separator.c_str();
	if (StrEmpty(separator)) separator = _langpack.langpack->digit_group_separator_currency;
	FormatNumber(builder, number, separator);
	if (number_str != STR_NULL) {
		FormatString(builder, GetStringPtr(number_str), {});
	}

	/* Add suffix part, following symbol_pos specification.
	 * Here, it can can be either 1 (suffix) or 2 (both prefix and suffix).
	 * The only remaining value is 1 (prefix), so everything that is not 0 */
	if (spec->symbol_pos != 0) builder += spec->suffix;

	if (negative) {
		builder.Utf8Encode(SCC_POP_COLOUR);
	}
}

/**
 * Determine the "plural" index given a plural form and a number.
 * @param count       The number to get the plural index of.
 * @param plural_form The plural form we want an index for.
 * @return The plural index for the given form.
 */
static int DeterminePluralForm(int64_t count, int plural_form)
{
	/* The absolute value determines plurality */
	uint64_t n = abs(count);

	switch (plural_form) {
		default:
			NOT_REACHED();

		/* Two forms: singular used for one only.
		 * Used in:
		 *   Danish, Dutch, English, German, Norwegian, Swedish, Estonian, Finnish,
		 *   Greek, Hebrew, Italian, Portuguese, Spanish, Esperanto */
		case 0:
			return n != 1 ? 1 : 0;

		/* Only one form.
		 * Used in:
		 *   Hungarian, Japanese, Turkish */
		case 1:
			return 0;

		/* Two forms: singular used for 0 and 1.
		 * Used in:
		 *   French, Brazilian Portuguese */
		case 2:
			return n > 1 ? 1 : 0;

		/* Three forms: special cases for 0, and numbers ending in 1 except when ending in 11.
		 * Note: Cases are out of order for hysterical reasons. '0' is last.
		 * Used in:
		 *   Latvian */
		case 3:
			return n % 10 == 1 && n % 100 != 11 ? 0 : n != 0 ? 1 : 2;

		/* Five forms: special cases for 1, 2, 3 to 6, and 7 to 10.
		 * Used in:
		 *   Gaelige (Irish) */
		case 4:
			return n == 1 ? 0 : n == 2 ? 1 : n < 7 ? 2 : n < 11 ? 3 : 4;

		/* Three forms: special cases for numbers ending in 1 except when ending in 11, and 2 to 9 except when ending in 12 to 19.
		 * Used in:
		 *   Lithuanian */
		case 5:
			return n % 10 == 1 && n % 100 != 11 ? 0 : n % 10 >= 2 && (n % 100 < 10 || n % 100 >= 20) ? 1 : 2;

		/* Three forms: special cases for numbers ending in 1 except when ending in 11, and 2 to 4 except when ending in 12 to 14.
		 * Used in:
		 *   Croatian, Russian, Ukrainian */
		case 6:
			return n % 10 == 1 && n % 100 != 11 ? 0 : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1 : 2;

		/* Three forms: special cases for 1, and numbers ending in 2 to 4 except when ending in 12 to 14.
		 * Used in:
		 *   Polish */
		case 7:
			return n == 1 ? 0 : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? 1 : 2;

		/* Four forms: special cases for numbers ending in 01, 02, and 03 to 04.
		 * Used in:
		 *   Slovenian */
		case 8:
			return n % 100 == 1 ? 0 : n % 100 == 2 ? 1 : n % 100 == 3 || n % 100 == 4 ? 2 : 3;

		/* Two forms: singular used for numbers ending in 1 except when ending in 11.
		 * Used in:
		 *   Icelandic */
		case 9:
			return n % 10 == 1 && n % 100 != 11 ? 0 : 1;

		/* Three forms: special cases for 1, and 2 to 4
		 * Used in:
		 *   Czech, Slovak */
		case 10:
			return n == 1 ? 0 : n >= 2 && n <= 4 ? 1 : 2;

		/* Two forms: cases for numbers ending with a consonant, and with a vowel.
		 * Korean doesn't have the concept of plural, but depending on how a
		 * number is pronounced it needs another version of a particle.
		 * As such the plural system is misused to give this distinction.
		 */
		case 11:
			switch (n % 10) {
				case 0: // yeong
				case 1: // il
				case 3: // sam
				case 6: // yuk
				case 7: // chil
				case 8: // pal
					return 0;

				case 2: // i
				case 4: // sa
				case 5: // o
				case 9: // gu
					return 1;

				default:
					NOT_REACHED();
			}

		/* Four forms: special cases for 1, 0 and numbers ending in 02 to 10, and numbers ending in 11 to 19.
		 * Used in:
		 *  Maltese */
		case 12:
			return (n == 1 ? 0 : n == 0 || (n % 100 > 1 && n % 100 < 11) ? 1 : (n % 100 > 10 && n % 100 < 20) ? 2 : 3);
		/* Four forms: special cases for 1 and 11, 2 and 12, 3 .. 10 and 13 .. 19, other
		 * Used in:
		 *  Scottish Gaelic */
		case 13:
			return ((n == 1 || n == 11) ? 0 : (n == 2 || n == 12) ? 1 : ((n > 2 && n < 11) || (n > 12 && n < 20)) ? 2 : 3);

		/* Three forms: special cases for 1, 0 and numbers ending in 01 to 19.
		 * Used in:
		 *   Romanian */
		case 14:
			return n == 1 ? 0 : (n == 0 || (n % 100 > 0 && n % 100 < 20)) ? 1 : 2;
	}
}

static const char *ParseStringChoice(const char *b, uint form, StringBuilder builder)
{
	/* <NUM> {Length of each string} {each string} */
	uint n = (uint8_t)*b++;
	size_t form_offset = 0, form_len = 0, total_len = 0;
	for (uint i = 0; i != n; i++) {
		uint len = (uint8_t)*b++;
		if (i == form) {
			form_offset = total_len;
			form_len = len;
		}
		total_len += len;
	}

	builder += std::string_view(b + form_offset, form_len);
	return b + total_len;
}

/** Helper for unit conversion. */
struct UnitConversion {
	double factor; ///< Amount to multiply or divide upon conversion.

	/**
	 * Convert value from OpenTTD's internal unit into the displayed value.
	 * @param input The input to convert.
	 * @param round Whether to round the value or not.
	 * @return The converted value.
	 */
	int64_t ToDisplay(int64_t input, bool round = true) const
	{
		return round
			? (int64_t)std::round(input * this->factor)
			: (int64_t)(input * this->factor);
	}

	/**
	 * Convert the displayed value back into a value of OpenTTD's internal unit.
	 * @param input The input to convert.
	 * @param round Whether to round the value up or not.
	 * @param divider Divide the return value by this.
	 * @return The converted value.
	 */
	int64_t FromDisplay(int64_t input, bool round = true, int64_t divider = 1) const
	{
		return round
			? (int64_t)std::round(input / this->factor / divider)
			: (int64_t)(input / this->factor / divider);
	}
};

/** Information about a specific unit system. */
struct Units {
	UnitConversion c; ///< Conversion
	StringID s;       ///< String for the unit
	unsigned int decimal_places; ///< Number of decimal places embedded in the value. For example, 1 if the value is in tenths, and 3 if the value is in thousandths.
};

/** Information about a specific unit system with a long variant. */
struct UnitsLong {
	UnitConversion c; ///< Conversion
	StringID s;       ///< String for the short variant of the unit
	StringID l;       ///< String for the long variant of the unit
	unsigned int decimal_places; ///< Number of decimal places embedded in the value. For example, 1 if the value is in tenths, and 3 if the value is in thousandths.
};

/** Unit conversions for velocity. */
static const Units _units_velocity_calendar[] = {
	{ { 1.0      }, STR_UNITS_VELOCITY_IMPERIAL,      0 },
	{ { 1.609344 }, STR_UNITS_VELOCITY_METRIC,        0 },
	{ { 0.44704  }, STR_UNITS_VELOCITY_SI,            0 },
	{ { 0.578125 }, STR_UNITS_VELOCITY_GAMEUNITS_DAY, 1 },
	{ { 0.868976 }, STR_UNITS_VELOCITY_KNOTS,         0 },
};

/** Unit conversions for velocity. */
static const Units _units_velocity_realtime[] = {
	{ { 1.0      }, STR_UNITS_VELOCITY_IMPERIAL,      0 },
	{ { 1.609344 }, STR_UNITS_VELOCITY_METRIC,        0 },
	{ { 0.44704  }, STR_UNITS_VELOCITY_SI,            0 },
	{ { 0.289352 }, STR_UNITS_VELOCITY_GAMEUNITS_SEC, 1 },
	{ { 0.868976 }, STR_UNITS_VELOCITY_KNOTS,         0 },
};

/** Unit conversions for power. */
static const Units _units_power[] = {
	{ { 1.0      }, STR_UNITS_POWER_IMPERIAL, 0 },
	{ { 1.01387  }, STR_UNITS_POWER_METRIC,   0 },
	{ { 0.745699 }, STR_UNITS_POWER_SI,       0 },
};

/** Unit conversions for power to weight. */
static const Units _units_power_to_weight[] = {
	{ { 0.907185 }, STR_UNITS_POWER_IMPERIAL_TO_WEIGHT_IMPERIAL, 1 },
	{ { 1.0      }, STR_UNITS_POWER_IMPERIAL_TO_WEIGHT_METRIC,   1 },
	{ { 1.0      }, STR_UNITS_POWER_IMPERIAL_TO_WEIGHT_SI,       1 },
	{ { 0.919768 }, STR_UNITS_POWER_METRIC_TO_WEIGHT_IMPERIAL,   1 },
	{ { 1.01387  }, STR_UNITS_POWER_METRIC_TO_WEIGHT_METRIC,     1 },
	{ { 1.01387  }, STR_UNITS_POWER_METRIC_TO_WEIGHT_SI,         1 },
	{ { 0.676487 }, STR_UNITS_POWER_SI_TO_WEIGHT_IMPERIAL,       1 },
	{ { 0.745699 }, STR_UNITS_POWER_SI_TO_WEIGHT_METRIC,         1 },
	{ { 0.745699 }, STR_UNITS_POWER_SI_TO_WEIGHT_SI,             1 },
};

/** Unit conversions for weight. */
static const UnitsLong _units_weight[] = {
	{ {    1.102311 }, STR_UNITS_WEIGHT_SHORT_IMPERIAL, STR_UNITS_WEIGHT_LONG_IMPERIAL, 0 },
	{ {    1.0      }, STR_UNITS_WEIGHT_SHORT_METRIC,   STR_UNITS_WEIGHT_LONG_METRIC,   0 },
	{ { 1000.0      }, STR_UNITS_WEIGHT_SHORT_SI,       STR_UNITS_WEIGHT_LONG_SI,       0 },
};

/** Unit conversions for volume. */
static const UnitsLong _units_volume[] = {
	{ {  264.172 }, STR_UNITS_VOLUME_SHORT_IMPERIAL, STR_UNITS_VOLUME_LONG_IMPERIAL, 0 },
	{ { 1000.0   }, STR_UNITS_VOLUME_SHORT_METRIC,   STR_UNITS_VOLUME_LONG_METRIC,   0 },
	{ {    1.0   }, STR_UNITS_VOLUME_SHORT_SI,       STR_UNITS_VOLUME_LONG_SI,       0 },
};

/** Unit conversions for force. */
static const Units _units_force[] = {
	{ { 0.224809 }, STR_UNITS_FORCE_IMPERIAL, 0 },
	{ { 0.101972 }, STR_UNITS_FORCE_METRIC,   0 },
	{ { 0.001    }, STR_UNITS_FORCE_SI,       0 },
};

/** Unit conversions for height. */
static const Units _units_height[] = {
	{ { 3.0 }, STR_UNITS_HEIGHT_IMPERIAL, 0 }, // "Wrong" conversion factor for more nicer GUI values
	{ { 1.0 }, STR_UNITS_HEIGHT_METRIC,   0 },
	{ { 1.0 }, STR_UNITS_HEIGHT_SI,       0 },
};

/** Unit conversions for time in calendar days or wallclock seconds */
static const Units _units_time_days_or_seconds[] = {
	{ { 1 }, STR_UNITS_DAYS,    0 },
	{ { 2 }, STR_UNITS_SECONDS, 0 },
};

/** Unit conversions for time in calendar months or wallclock minutes */
static const Units _units_time_months_or_minutes[] = {
	{ { 1 }, STR_UNITS_MONTHS,  0 },
	{ { 1 }, STR_UNITS_MINUTES, 0 },
	{ { 1 }, STR_UNITS_PRODUCTION_INTERVALS, 0 },
};

/** Unit conversions for time in calendar years or economic periods */
static const Units _units_time_years_or_periods[] = {
	{ { 1 }, STR_UNITS_YEARS,  0 },
	{ { 1 }, STR_UNITS_PERIODS, 0 },
};

/** Unit conversions for time in calendar years or wallclock minutes */
static const Units _units_time_years_or_minutes[] = {
	{ { 1  }, STR_UNITS_YEARS,  0 },
	{ { 12 }, STR_UNITS_MINUTES, 0 },
	{ { 1  }, STR_UNITS_PERIODS, 0 },
};

StringID GetVelocityUnitName(VehicleType type)
{
	uint8_t setting = (type == VEH_SHIP || type == VEH_AIRCRAFT) ? _settings_game.locale.units_velocity_nautical : _settings_game.locale.units_velocity;

	assert(setting < lengthof(_units_velocity_calendar));
	assert(setting < lengthof(_units_velocity_realtime));
	static_assert(lengthof(_units_velocity_calendar) == 5 && lengthof(_units_velocity_realtime) == 5);

	switch (setting) {
		case 0:
		case 1:
		case 2:
			return STR_UNIT_NAME_VELOCITY_IMPERIAL + setting;

		case 3:
			return EconTime::UsingWallclockUnits() ? STR_UNIT_NAME_VELOCITY_GAMEUNITS_WALLCLOCK : STR_UNIT_NAME_VELOCITY_GAMEUNITS;

		case 4:
			return STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_KNOTS;

		default:
			NOT_REACHED();
	}
}

/**
 * Get the correct velocity units depending on the vehicle type and whether we're using real-time units.
 * @param type VehicleType to convert velocity for.
 * @return The Units for the proper vehicle and time mode.
 */
static const Units GetVelocityUnits(VehicleType type)
{
	uint8_t setting = (type == VEH_SHIP || type == VEH_AIRCRAFT) ? _settings_game.locale.units_velocity_nautical : _settings_game.locale.units_velocity;

	assert(setting < lengthof(_units_velocity_calendar));
	assert(setting < lengthof(_units_velocity_realtime));

	if (EconTime::UsingWallclockUnits()) return _units_velocity_realtime[setting];

	return _units_velocity_calendar[setting];
}

/**
 * Convert the given (internal) speed to the display speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertSpeedToDisplaySpeed(uint speed, VehicleType type)
{
	/* For historical reasons we don't want to mess with the
	 * conversion for speed. So, don't round it and keep the
	 * original conversion factors instead of the real ones. */
	return GetVelocityUnits(type).c.ToDisplay(speed, false);
}

/**
 * Convert the given (internal) speed to the display speed, in units (not decimal values).
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertSpeedToUnitDisplaySpeed(uint speed, VehicleType type)
{
	const Units &units = GetVelocityUnits(type);
	uint result = units.c.ToDisplay(speed, false);
	for (uint i = 0; i < units.decimal_places; i++) {
		result /= 10;
	}
	return result;
}

/**
 * Convert the given display speed to the (internal) speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertDisplaySpeedToSpeed(uint speed, VehicleType type)
{
	return GetVelocityUnits(type).c.FromDisplay(speed);
}

/**
 * Convert the given km/h-ish speed to the display speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertKmhishSpeedToDisplaySpeed(uint speed, VehicleType type)
{
	return GetVelocityUnits(type).c.ToDisplay(speed * 10, false) / 16;
}

/**
 * Convert the given display speed to the km/h-ish speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertDisplaySpeedToKmhishSpeed(uint speed, VehicleType type)
{
	return GetVelocityUnits(type).c.FromDisplay(speed * 16, true, 10);
}

/**
 * Convert the given internal weight to the display weight.
 * @param weight the weight to convert
 * @return the converted weight.
 */
uint ConvertWeightToDisplayWeight(uint weight)
{
	return _units_weight[_settings_game.locale.units_weight].c.ToDisplay(weight);
}

/**
 * Convert the given display weight to the (internal) weight.
 * @param weight the weight to convert
 * @return the converted weight.
 */
uint ConvertDisplayWeightToWeight(uint weight)
{
	return _units_weight[_settings_game.locale.units_weight].c.FromDisplay(weight);
}

/**
 * Convert the given internal power to the display power.
 * @param power the power to convert
 * @return the converted power.
 */
uint ConvertPowerToDisplayPower(uint power)
{
	return _units_power[_settings_game.locale.units_power].c.ToDisplay(power);
}

/**
 * Convert the given display power to the (internal) power.
 * @param power the power to convert
 * @return the converted power.
 */
uint ConvertDisplayPowerToPower(uint power)
{
	return _units_power[_settings_game.locale.units_power].c.FromDisplay(power);
}

/**
 * Convert the given internal force to the display force.
 * @param force the force to convert
 * @return the converted force.
 */
int64_t ConvertForceToDisplayForce(int64_t force)
{
	return _units_force[_settings_game.locale.units_force].c.ToDisplay(force);
}

/**
 * Convert the given display force to the (internal) force.
 * @param force the force to convert
 * @return the converted force.
 */
int64_t ConvertDisplayForceToForce(int64_t force)
{
	return _units_force[_settings_game.locale.units_force].c.FromDisplay(force);
}

static DecimalValue ConvertWeightRatioToDisplay(const Units &unit, int64_t ratio)
{
	int64_t input = ratio;
	int64_t decimals = 2;
	if (_settings_game.locale.units_weight == 2) {
		input *= 1000;
		decimals += 3;
	}

	const UnitConversion &weight_conv = _units_weight[_settings_game.locale.units_weight].c;
	UnitConversion conv = unit.c;
	conv.factor /= weight_conv.factor;

	int64_t value = conv.ToDisplay(input);

	if (unit.c.factor > 100) {
		value /= 100;
		decimals -= 2;
	}

	return { value, decimals };
}

static uint ConvertDisplayToWeightRatio(const Units &unit, double in)
{
	const UnitConversion &weight_conv = _units_weight[_settings_game.locale.units_weight].c;
	UnitConversion conv = unit.c;
	conv.factor /= weight_conv.factor;
	int64_t multiplier = _settings_game.locale.units_weight == 2 ? 1000 : 1;

	return conv.FromDisplay(in * 100 * multiplier, true, multiplier);
}

static void FormatUnitWeightRatio(StringBuilder builder, const Units &unit, int64_t raw_value)
{
	std::string_view unit_str = GetStringPtr(unit.s);
	std::string_view weight_str = GetStringPtr(_units_weight[_settings_game.locale.units_weight].s);

	format_buffer_sized<128> tmp_buffer;
	tmp_buffer.append(unit_str);

	for (char32_t c : Utf8View(weight_str)) {
		if (c == 0xA0) {
			/* NBSP */
			continue;
		}
		if (c == SCC_DECIMAL) {
			c = '/';
		}
		tmp_buffer.append_utf8(c);
	}

	DecimalValue dv = ConvertWeightRatioToDisplay(unit, raw_value);

	auto tmp_params = MakeParameters(dv.value, dv.decimals);
	FormatString(builder, tmp_buffer, tmp_params);
}

/**
 * Convert the given internal power / weight ratio to the display decimal.
 * @param ratio the power / weight ratio to convert
 * @return value the output value and decimal offset
 */
DecimalValue ConvertPowerWeightRatioToDisplay(int64_t ratio)
{
	return ConvertWeightRatioToDisplay(_units_power[_settings_game.locale.units_power], ratio);
}

/**
 * Convert the given internal force / weight ratio to the display decimal.
 * @param ratio the force / weight ratio to convert
 * @return value the output value and decimal offset
 */
DecimalValue ConvertForceWeightRatioToDisplay(int64_t ratio)
{
	return ConvertWeightRatioToDisplay(_units_force[_settings_game.locale.units_force], ratio);
}

/**
 * Convert the given display value to the internal power / weight ratio.
 * @param in the display value
 * @return the converted power / weight ratio.
 */
uint ConvertDisplayToPowerWeightRatio(double in)
{
	return ConvertDisplayToWeightRatio(_units_power[_settings_game.locale.units_power], in);
}

/**
 * Convert the given display value to the internal force / weight ratio.
 * @param in the display value
 * @return the converted force / weight ratio.
 */
uint ConvertDisplayToForceWeightRatio(double in)
{
	return ConvertDisplayToWeightRatio(_units_force[_settings_game.locale.units_force], in);
}

uint ConvertCargoQuantityToDisplayQuantity(CargoType cargo, uint quantity)
{
	switch (CargoSpec::Get(cargo)->units_volume) {
		case STR_TONS:
			return _units_weight[_settings_game.locale.units_weight].c.ToDisplay(quantity);

		case STR_LITERS:
			return _units_volume[_settings_game.locale.units_volume].c.ToDisplay(quantity);

		default:
			break;
	}
	return quantity;
}

uint ConvertDisplayQuantityToCargoQuantity(CargoType cargo, uint quantity)
{
	switch (CargoSpec::Get(cargo)->units_volume) {
		case STR_TONS:
			return _units_weight[_settings_game.locale.units_weight].c.FromDisplay(quantity);

		case STR_LITERS:
			return _units_volume[_settings_game.locale.units_volume].c.FromDisplay(quantity);

		default:
			break;
	}
	return quantity;
}

/**
 * Decodes an encoded string during FormatString.
 * @param str The buffer of the encoded string.
 * @param game_script Set if decoding a GameScript-encoded string. This affects how string IDs are handled.
 * @param builder The string builder to write the string to.
 * @returns Updated position position in input buffer.
 */
static const char *DecodeEncodedString(const char *str, bool game_script, StringBuilder &builder)
{
	ankerl::svector<StringParameter, 10> sub_args;

	char *p;
	StringIndexInTab id(std::strtoul(str, &p, 16));
	if (*p != SCC_RECORD_SEPARATOR && *p != '\0') {
		while (*p != '\0') p++;
		builder += "(invalid SCC_ENCODED)";
		return p;
	}
	if (game_script && id >= TAB_SIZE_GAMESCRIPT) {
		while (*p != '\0') p++;
		builder += "(invalid StringID)";
		return p;
	}

	while (*p != '\0') {
		/* The start of parameter. */
		const char *s = ++p;

		/* Find end of the parameter. */
		for (; *p != '\0' && *p != SCC_RECORD_SEPARATOR; ++p) {}

		if (s == p) {
			/* This is an empty parameter. */
			sub_args.emplace_back(std::monostate{});
			continue;
		}

		/* Get the parameter type. */
		char32_t parameter_type;
		size_t len = Utf8Decode(&parameter_type, s);
		s += len;

		switch (parameter_type) {
			case SCC_ENCODED: {
				uint64_t param = std::strtoull(s, &p, 16);
				if (param >= TAB_SIZE_GAMESCRIPT) {
					while (*p != '\0') p++;
					builder += "(invalid sub-StringID)";
					return p;
				}
				param = MakeStringID(TEXT_TAB_GAMESCRIPT_START, StringIndexInTab(param));
				sub_args.emplace_back(param);
				break;
			}

			case SCC_ENCODED_NUMERIC: {
				uint64_t param = std::strtoull(s, &p, 16);
				sub_args.emplace_back(param);
				break;
			}

			case SCC_ENCODED_STRING: {
				sub_args.emplace_back(std::string(s, p - s));
				break;
			}

			default:
				/* Unknown parameter, make it blank. */
				sub_args.emplace_back(std::monostate{});
				break;
		}
	}

	StringID stringid = game_script ? MakeStringID(TEXT_TAB_GAMESCRIPT_START, id) : StringID{id.base()};
	GetStringWithArgs(builder, stringid, sub_args, true);

	return p;
}

/**
 * Parse most format codes within a string and write the result to a buffer.
 * @param builder The string builder to write the final string to.
 * @param str_arg The original string with format codes.
 * @param args    Pointer to extra arguments used by various string codes.
 * @param dry_run True when the args' type data is not yet initialized.
 */
static void FormatString(StringBuilder builder, std::string_view str_arg, StringParameters &args, uint orig_case_index, bool game_script, bool dry_run)
{
	size_t orig_first_param_offset = args.GetOffset();

	if (!dry_run) {
		/*
		 * This function is normally called with `dry_run` false, then we call this function again
		 * with `dry_run` being true. The dry run is required for the gender formatting. For the
		 * gender determination we need to format a sub string to get the gender, but for that we
		 * need to know as what string control code type the specific parameter is encoded. Since
		 * gendered words can be before the "parameter" words, this needs to be determined before
		 * the actual formatting.
		 */
		format_buffer buffer;
		StringBuilder dry_run_builder(buffer);
		FormatString(dry_run_builder, str_arg, args, orig_case_index, game_script, true);
		/* We have to restore the original offset here to to read the correct values. */
		args.SetOffset(orig_first_param_offset);
	}
	uint next_substr_case_index = 0;
	struct StrStackItem {
		const char *str;
		const char *end;
		size_t first_param_offset;
		uint case_index;

		StrStackItem(std::string_view view, size_t first_param_offset, uint case_index)
			: str(view.data()), end(view.data() + view.size()), first_param_offset(first_param_offset), case_index(case_index)
		{}
	};
	std::stack<StrStackItem, std::vector<StrStackItem>> str_stack;
	str_stack.emplace(str_arg, orig_first_param_offset, orig_case_index);

	for (;;) {
		try {
			while (!str_stack.empty() && str_stack.top().str >= str_stack.top().end) {
				str_stack.pop();
			}
			if (str_stack.empty()) break;
			const char *&str = str_stack.top().str;
			const size_t ref_param_offset = str_stack.top().first_param_offset;
			const uint case_index = str_stack.top().case_index;
			char32_t b = Utf8Consume(&str);
			assert(b != 0);

			if (_scan_for_gender_data && !builder.IsEmpty()) {
				/* Early exit when scanning for gender data if target string is already non-empty */
				return;
			}

			if (SCC_NEWGRF_FIRST <= b && b <= SCC_NEWGRF_LAST) {
				/* We need to pass some stuff as it might be modified. */
				b = RemapNewGRFStringControlCode(b, &str);
				if (b == 0) continue;
			}

			if (b < SCC_CONTROL_START || b > SCC_CONTROL_END) {
				builder.Utf8Encode(b);
				continue;
			}

			args.SetTypeOfNextParameter(b);
			switch (b) {
				case SCC_ENCODED:
				case SCC_ENCODED_INTERNAL:
					str = DecodeEncodedString(str, b == SCC_ENCODED, builder);
					break;

				case SCC_NEWGRF_STRINL: {
					StringID substr = Utf8Consume(&str);
					std::string_view ptr = GetStringPtr(substr);
					str_stack.emplace(ptr, args.GetOffset(), next_substr_case_index); // this may invalidate "str"
					next_substr_case_index = 0;
					break;
				}

				case SCC_NEWGRF_PRINT_WORD_STRING_ID: {
					StringID substr = args.GetNextParameter<StringID>();
					std::string_view ptr = GetStringPtr(substr);
					str_stack.emplace(ptr, args.GetOffset(), next_substr_case_index); // this may invalidate "str"
					next_substr_case_index = 0;
					break;
				}

				case SCC_GENDER_LIST: { // {G 0 Der Die Das}
					/* First read the meta data from the language file. */
					size_t offset = ref_param_offset + (uint8_t)*str++;
					int gender = 0;
					if (offset >= args.GetNumParameters()) {
						/* The offset may come from an external NewGRF, and be invalid. */
						builder += "(invalid GENDER parameter)";
					} else if (!dry_run && args.GetTypeAtOffset(offset) != 0) {
						/* Now we need to figure out what text to resolve, i.e.
						 * what do we need to draw? So get the actual raw string
						 * first using the control code to get said string. */
						char input[4 + 1];
						char *p = input + Utf8Encode(input, args.GetTypeAtOffset(offset));
						*p = '\0';

						/* The gender is stored at the start of the formatted string. */
						bool old_sgd = _scan_for_gender_data;
						_scan_for_gender_data = true;
						format_buffer buffer;
						StringBuilder tmp_builder(buffer);
						StringParameters tmp_params = args.GetRemainingParameters(offset);
						FormatString(tmp_builder, input, tmp_params);
						_scan_for_gender_data = old_sgd;

						/* And determine the string. */
						const char *s = buffer.c_str();
						char32_t c = Utf8Consume(&s);
						/* Does this string have a gender, if so, set it */
						if (c == SCC_GENDER_INDEX) gender = (uint8_t)s[0];
					}
					str = ParseStringChoice(str, gender, builder);
					break;
				}

				/* This sets up the gender for the string.
				 * We just ignore this one. It's used in {G 0 Der Die Das} to determine the case. */
				case SCC_GENDER_INDEX: // {GENDER 0}
					if (_scan_for_gender_data) {
						builder.Utf8Encode(SCC_GENDER_INDEX);
						builder += *str++;
						return; // Exit early
					} else {
						str++;
					}
					break;

				case SCC_PLURAL_LIST: { // {P}
					int plural_form = *str++;          // contains the plural form for this string
					size_t offset = ref_param_offset + (uint8_t)*str++;
					const uint64_t *v = nullptr;
					/* The offset may come from an external NewGRF, and be invalid. */
					if (offset < args.GetNumParameters()) {
						v = std::get_if<uint64_t>(&args.GetParam(offset)); // contains the number that determines plural
					}
					if (v != nullptr) {
						str = ParseStringChoice(str, DeterminePluralForm(static_cast<int64_t>(*v), plural_form), builder);
					} else {
						builder += "(invalid PLURAL parameter)";
					}
					break;
				}

				case SCC_ARG_INDEX: { // Move argument pointer
					args.SetOffset(ref_param_offset + (uint8_t)*str++);
					break;
				}

				case SCC_SET_CASE: { // {SET_CASE}
					/* This is a pseudo command, it's outputted when someone does {STRING.ack}
					 * The modifier is added to all subsequent GetStringWithArgs that accept the modifier. */
					next_substr_case_index = (uint8_t)*str++;
					break;
				}

				case SCC_SWITCH_CASE: { // {Used to implement case switching}
					/* <0x9E> <NUM CASES> <CASE1> <LEN1> <STRING1> <CASE2> <LEN2> <STRING2> <CASE3> <LEN3> <STRING3> <LENDEFAULT> <STRINGDEFAULT>
					 * Each LEN is printed using 2 bytes in little endian order. */
					uint num = (uint8_t)*str++;
					std::optional<std::string_view> found;
					for (; num > 0; --num) {
						uint8_t index = static_cast<uint8_t>(str[0]);
						uint16_t len = static_cast<uint8_t>(str[1]) + (static_cast<uint8_t>(str[2]) << 8);
						str += 3;
						if (index == case_index) {
							/* Found the case */
							found.emplace(str, len);
						}
						str += len;
					}
					uint16_t default_len = static_cast<uint8_t>(str[0]) + (static_cast<uint8_t>(str[1]) << 8);
					str += 2;
					if (!found.has_value()) found.emplace(str, default_len);
					str += default_len;
					assert(str <= str_stack.top().end);
					str_stack.emplace(*found, ref_param_offset, case_index); // this may invalidate "str"
					break;
				}

				case SCC_REVISION: // {REV}
					builder += _openttd_revision;
					break;

				case SCC_RAW_STRING_POINTER: { // {RAW_STRING}
					FormatString(builder, args.GetNextParameterString(), args);
					break;
				}

				case SCC_STRING: {// {STRING}
					StringID string_id = args.GetNextParameter<StringID>();
					if (game_script && GetStringTab(string_id) != TEXT_TAB_GAMESCRIPT_START) break;
					/* It's prohibited for the included string to consume any arguments. */
					StringParameters tmp_params(args, game_script ? args.GetDataLeft() : 0);
					GetStringWithArgs(builder, string_id, tmp_params, next_substr_case_index, game_script);
					next_substr_case_index = 0;
					break;
				}

				case SCC_STRING1:
				case SCC_STRING2:
				case SCC_STRING3:
				case SCC_STRING4:
				case SCC_STRING5:
				case SCC_STRING6:
				case SCC_STRING7: { // {STRING1..7}
					/* Strings that consume arguments */
					StringID string_id = args.GetNextParameter<StringID>();
					if (game_script && GetStringTab(string_id) != TEXT_TAB_GAMESCRIPT_START) break;
					uint size = b - SCC_STRING1 + 1;
					if (size > args.GetDataLeft()) {
						builder += "(consumed too many parameters)";
					} else {
						StringParameters sub_args(args, game_script ? args.GetDataLeft() : size);
						GetStringWithArgs(builder, string_id, sub_args, next_substr_case_index, game_script);
						args.AdvanceOffset(size);
					}
					next_substr_case_index = 0;
					break;
				}

				case SCC_COMMA: // {COMMA}
					FormatCommaNumber(builder, args.GetNextParameter<int64_t>());
					break;

				case SCC_DECIMAL: { // {DECIMAL}
					int64_t number = args.GetNextParameter<int64_t>();
					int digits = args.GetNextParameter<int>();
					FormatCommaNumber(builder, number, digits);
					break;
				}

				case SCC_DECIMAL1: {// {DECIMAL1}
					int64_t number = args.GetNextParameter<int64_t>();
					FormatCommaNumber(builder, number, 1);
					break;
				}

				case SCC_NUM: // {NUM}
					FormatNoCommaNumber(builder, args.GetNextParameter<int64_t>());
					break;

				case SCC_PLUS_NUM: { // {PLUS_NUM}
					int64_t number = args.GetNextParameter<int64_t>();
					if (number > 0) {
						builder += '+';
					}
					FormatNoCommaNumber(builder, number);
					break;
				}

				case SCC_ZEROFILL_NUM: { // {ZEROFILL_NUM}
					int64_t num = args.GetNextParameter<int64_t>();
					FormatZerofillNumber(builder, num, args.GetNextParameter<int>());
					break;
				}

				case SCC_HEX: // {HEX}
					FormatHexNumber(builder, args.GetNextParameter<uint64_t>());
					break;

				case SCC_BYTES: // {BYTES}
					FormatBytes(builder, args.GetNextParameter<int64_t>());
					break;

				case SCC_CARGO_TINY: { // {CARGO_TINY}
					/* Tiny description of cargotypes. Layout:
					 * param 1: cargo type
					 * param 2: cargo count */
					CargoType cargo = args.GetNextParameter<CargoType>();
					int64_t amount = args.GetNextParameter<int64_t>();

					if (cargo >= CargoSpec::GetArraySize()) {
						builder += "(invalid cargo type)";
						break;
					}

					switch (CargoSpec::Get(cargo)->units_volume) {
						case STR_TONS:
							amount = _units_weight[_settings_game.locale.units_weight].c.ToDisplay(amount);
							break;

						case STR_LITERS:
							amount = _units_volume[_settings_game.locale.units_volume].c.ToDisplay(amount);
							break;

						default:
							break;
					}

					FormatCommaNumber(builder, amount);
					break;
				}

				case SCC_CARGO_SHORT: { // {CARGO_SHORT}
					/* Short description of cargotypes. Layout:
					 * param 1: cargo type
					 * param 2: cargo count */
					CargoType cargo = args.GetNextParameter<CargoType>();
					int64_t amount = args.GetNextParameter<int64_t>();

					if (cargo >= CargoSpec::GetArraySize()) {
						builder += "(invalid cargo type)";
						break;
					}

					StringID cargo_str = CargoSpec::Get(cargo)->units_volume;
					switch (cargo_str) {
						case STR_TONS: {
							assert(_settings_game.locale.units_weight < lengthof(_units_weight));
							const auto &x = _units_weight[_settings_game.locale.units_weight];
							auto tmp_params = MakeParameters(x.c.ToDisplay(amount), x.decimal_places);
							FormatString(builder, GetStringPtr(x.l), tmp_params);
							break;
						}

						case STR_LITERS: {
							assert(_settings_game.locale.units_volume < lengthof(_units_volume));
							const auto &x = _units_volume[_settings_game.locale.units_volume];
							auto tmp_params = MakeParameters(x.c.ToDisplay(amount), x.decimal_places);
							FormatString(builder, GetStringPtr(x.l), tmp_params);
							break;
						}

						default: {
							auto tmp_params = MakeParameters(amount);
							GetStringWithArgs(builder, cargo_str, tmp_params);
							break;
						}
					}
					break;
				}

				case SCC_CARGO_LONG: { // {CARGO_LONG}
					/* First parameter is cargo type, second parameter is cargo count */
					CargoType cargo = args.GetNextParameter<CargoType>();
					int64_t amount = args.GetNextParameter<int64_t>();
					if (cargo < CargoSpec::GetArraySize()) {
						auto tmp_args = MakeParameters(amount);
						GetStringWithArgs(builder, CargoSpec::Get(cargo)->quantifier, tmp_args);
					} else if (!IsValidCargoType(cargo)) {
						GetStringWithArgs(builder, STR_QUANTITY_N_A, {});
					} else {
						builder += "(invalid cargo type)";
					}
					break;
				}

				case SCC_CARGO_LIST: { // {CARGO_LIST}
					CargoTypes cmask = args.GetNextParameter<CargoTypes>();
					bool first = true;

					std::string_view list_separator = GetListSeparator();
					for (const auto &cs : _sorted_cargo_specs) {
						if (!HasBit(cmask, cs->Index())) continue;

						if (first) {
							first = false;
						} else {
							/* Add a comma if this is not the first item */
							builder += list_separator;
						}

						GetStringWithArgs(builder, cs->name, args, next_substr_case_index, game_script);
					}

					/* If first is still true then no cargo is accepted */
					if (first) GetStringWithArgs(builder, STR_JUST_NOTHING, args, next_substr_case_index, game_script);

					next_substr_case_index = 0;
					break;
				}

				case SCC_CURRENCY_SHORT: // {CURRENCY_SHORT}
					FormatGenericCurrency(builder, &GetCurrency(), args.GetNextParameter<int64_t>(), true);
					break;

				case SCC_CURRENCY_LONG: // {CURRENCY_LONG}
					FormatGenericCurrency(builder, &GetCurrency(), args.GetNextParameter<int64_t>(), false);
					break;

				case SCC_DATE_TINY: // {DATE_TINY}
					FormatTinyOrISODate(builder, args.GetNextParameter<CalTime::Date>(), STR_FORMAT_DATE_TINY);
					break;

				case SCC_DATE_SHORT: // {DATE_SHORT}
					FormatMonthAndYear(builder, args.GetNextParameter<CalTime::Date>(), next_substr_case_index);
					next_substr_case_index = 0;
					break;

				case SCC_DATE_LONG: // {DATE_LONG}
					FormatYmdString(builder, args.GetNextParameter<CalTime::Date>(), next_substr_case_index);
					next_substr_case_index = 0;
					break;

				case SCC_DATE_ISO: // {DATE_ISO}
					FormatTinyOrISODate(builder, args.GetNextParameter<CalTime::Date>(), STR_FORMAT_DATE_ISO);
					break;

				case SCC_TIME_HHMM: // {TIME_HHMM}
					FormatTimeHHMMString(builder, args.GetNextParameter<uint>(), next_substr_case_index);
					break;

				case SCC_TT_TICKS:      // {TT_TICKS}
				case SCC_TT_TICKS_LONG: // {TT_TICKS_LONG}
					if (_settings_client.gui.timetable_in_ticks) {
						auto tmp_params = MakeParameters(args.GetNextParameter<int64_t>());
						FormatString(builder, GetStringPtr(STR_UNITS_TICKS), tmp_params);
					} else {
						StringID str;
						if (_settings_time.time_in_minutes) {
							str = STR_TIMETABLE_MINUTES;
						} else if (EconTime::UsingWallclockUnits()) {
							str = STR_UNITS_SECONDS;
						} else {
							str = STR_UNITS_DAYS;
						}
						const int64_t ticks = args.GetNextParameter<int64_t>();
						const int64_t ratio = TimetableDisplayUnitSize();
						const int64_t units = ticks / ratio;
						const int64_t leftover = _settings_client.gui.timetable_leftover_ticks ? ticks % ratio : 0;
						auto tmp_params = MakeParameters(units);
						FormatString(builder, GetStringPtr(str), tmp_params);
						if (b == SCC_TT_TICKS_LONG && _settings_time.time_in_minutes && units > 59) {
							int64_t hours = units / 60;
							int64_t minutes = units % 60;
							auto tmp_params = MakeParameters(
								(minutes != 0) ? STR_TIMETABLE_HOURS_MINUTES : STR_TIMETABLE_HOURS,
								hours,
								minutes
							);
							FormatString(builder, GetStringPtr(STR_TIMETABLE_MINUTES_SUFFIX), tmp_params);
						}
						if (leftover != 0) {
							auto tmp_params = MakeParameters(leftover);
							FormatString(builder, GetStringPtr(STR_TIMETABLE_LEFTOVER_TICKS), tmp_params);
						}
					}
					break;

				case SCC_TT_TIME:       // {TT_TIME}
				case SCC_TT_TIME_ABS: { // {TT_TIME_ABS}
					if (_settings_time.time_in_minutes) {
						FormatStateTicksHHMMString(builder, args.GetNextParameter<StateTicks>(), next_substr_case_index);
					} else if (EconTime::UsingWallclockUnits() && b == SCC_TT_TIME) {
						StateTicks tick = args.GetNextParameter<StateTicks>();
						StateTicksDelta offset = tick - _state_ticks;
						auto tmp_params = MakeParameters(offset / TICKS_PER_SECOND);
						FormatString(builder, GetStringPtr(STR_UNITS_SECONDS_SHORT), tmp_params);
					} else {
						FormatTinyOrISODate(builder, StateTicksToCalendarDate(args.GetNextParameter<StateTicks>()), STR_FORMAT_DATE_TINY);
					}
					break;
				}

				case SCC_FORCE: { // {FORCE}
					assert(_settings_game.locale.units_force < lengthof(_units_force));
					const auto &x = _units_force[_settings_game.locale.units_force];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_HEIGHT: { // {HEIGHT}
					assert(_settings_game.locale.units_height < lengthof(_units_height));
					const auto &x = _units_height[_settings_game.locale.units_height];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_POWER: { // {POWER}
					assert(_settings_game.locale.units_power < lengthof(_units_power));
					const auto &x = _units_power[_settings_game.locale.units_power];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_POWER_TO_WEIGHT: { // {POWER_TO_WEIGHT}
					auto setting = _settings_game.locale.units_power * 3u + _settings_game.locale.units_weight;
					assert(setting < lengthof(_units_power_to_weight));
					const auto &x = _units_power_to_weight[setting];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_VELOCITY: { // {VELOCITY}
					int64_t arg = args.GetNextParameter<int64_t>();
					// Unpack vehicle type from packed argument to get desired units.
					VehicleType vt = static_cast<VehicleType>(GB(arg, 56, 8));
					const auto &x = GetVelocityUnits(vt);
					auto tmp_params = MakeParameters(ConvertKmhishSpeedToDisplaySpeed(GB(arg, 0, 56), vt), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_VOLUME_SHORT: { // {VOLUME_SHORT}
					assert(_settings_game.locale.units_volume < lengthof(_units_volume));
					const auto &x = _units_volume[_settings_game.locale.units_volume];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_VOLUME_LONG: { // {VOLUME_LONG}
					assert(_settings_game.locale.units_volume < lengthof(_units_volume));
					const auto &x = _units_volume[_settings_game.locale.units_volume];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.l), tmp_params);
					break;
				}

				case SCC_WEIGHT_SHORT: { // {WEIGHT_SHORT}
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));
					const auto &x = _units_weight[_settings_game.locale.units_weight];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_WEIGHT_LONG: { // {WEIGHT_LONG}
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));
					const auto &x = _units_weight[_settings_game.locale.units_weight];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.l), tmp_params);
					break;
				}

				case SCC_POWER_WEIGHT_RATIO: { // {POWER_WEIGHT_RATIO}
					assert(_settings_game.locale.units_power < lengthof(_units_power));
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));

					FormatUnitWeightRatio(builder, _units_power[_settings_game.locale.units_power], args.GetNextParameter<int64_t>());
					break;
				}

				case SCC_FORCE_WEIGHT_RATIO: { // {FORCE_WEIGHT_RATIO}
					assert(_settings_game.locale.units_force < lengthof(_units_force));
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));

					FormatUnitWeightRatio(builder, _units_force[_settings_game.locale.units_force], args.GetNextParameter<int64_t>());
					break;
				}

				case SCC_UNITS_DAYS_OR_SECONDS: { // {UNITS_DAYS_OR_SECONDS}
					uint8_t realtime = EconTime::UsingWallclockUnits(_game_mode == GM_MENU);
					const auto &x = _units_time_days_or_seconds[realtime];
					int64_t duration = args.GetNextParameter<int64_t>();
					if (realtime) duration *= DayLengthFactor();
					auto tmp_params = MakeParameters(x.c.ToDisplay(duration), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_UNITS_MONTHS_OR_MINUTES: { // {UNITS_MONTHS_OR_MINUTES}
					uint8_t realtime = EconTime::UsingWallclockUnits(_game_mode == GM_MENU);
					if (realtime > 0 && ReplaceWallclockMinutesUnit()) realtime++;
					const auto &x = _units_time_months_or_minutes[realtime];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_UNITS_YEARS_OR_PERIODS: { // {UNITS_YEARS_OR_PERIODS}
					uint8_t realtime = EconTime::UsingWallclockUnits(_game_mode == GM_MENU);
					const auto &x = _units_time_years_or_periods[realtime];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_UNITS_YEARS_OR_MINUTES: { // {UNITS_YEARS_OR_MINUTES}
					uint8_t realtime = EconTime::UsingWallclockUnits(_game_mode == GM_MENU);
					if (realtime > 0 && ReplaceWallclockMinutesUnit()) realtime++;
					const auto &x = _units_time_years_or_minutes[realtime];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64_t>()), x.decimal_places);
					FormatString(builder, GetStringPtr(x.s), tmp_params);
					break;
				}

				case SCC_COMPANY_NAME: { // {COMPANY}
					const Company *c = Company::GetIfValid(args.GetNextParameter<CompanyID>());
					if (c == nullptr) break;

					if (!c->name.empty()) {
						auto tmp_params = MakeReferenceParameters(c->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						auto tmp_params = MakeReferenceParameters(c->name_2);
						GetStringWithArgs(builder, c->name_1, tmp_params);
					}
					break;
				}

				case SCC_COMPANY_NUM: { // {COMPANY_NUM}
					CompanyID company = args.GetNextParameter<CompanyID>();

					/* Nothing is added for AI or inactive companies */
					if (Company::IsValidHumanID(company)) {
						auto tmp_params = MakeParameters(company + 1);
						GetStringWithArgs(builder, STR_FORMAT_COMPANY_NUM, tmp_params);
					}
					break;
				}

				case SCC_DEPOT_NAME: { // {DEPOT}
					VehicleType vt = args.GetNextParameter<VehicleType>();
					if (vt == VEH_AIRCRAFT) {
						auto tmp_params = MakeParameters(args.GetNextParameter<StationID>());
						GetStringWithArgs(builder, STR_FORMAT_DEPOT_NAME_AIRCRAFT, tmp_params);
						break;
					}

					const Depot *d = Depot::Get(args.GetNextParameter<DepotID>());
					if (!d->name.empty()) {
						auto tmp_params = MakeReferenceParameters(d->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						auto tmp_params = MakeParameters(d->town->index, d->town_cn + 1);
						GetStringWithArgs(builder, STR_FORMAT_DEPOT_NAME_TRAIN + 2 * vt + (d->town_cn == 0 ? 0 : 1), tmp_params);
					}
					break;
				}

				case SCC_ENGINE_NAME: { // {ENGINE}
					int64_t arg = args.GetNextParameter<int64_t>();
					const Engine *e = Engine::GetIfValid(static_cast<EngineID>(arg));
					if (e == nullptr) break;

					if (!e->name.empty() && e->IsEnabled()) {
						auto tmp_params = MakeReferenceParameters(e->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
						break;
					}

					if (e->info.callback_mask.Test(VehicleCallbackMask::Name)) {
						uint16_t callback = GetVehicleCallback(CBID_VEHICLE_NAME, static_cast<uint32_t>(arg >> 32), 0, e->index, nullptr);
						/* Not calling ErrorUnknownCallbackResult due to being inside string processing. */
						if (callback != CALLBACK_FAILED && callback < 0x400) {
							const GRFFile *grffile = e->GetGRF();
							assert(grffile != nullptr);

							builder += GetGRFStringWithTextStack(grffile, GRFSTR_MISC_GRF_TEXT + callback, 6);
							break;
						}
					}

					GetStringWithArgs(builder, e->info.string_id, {});
					break;
				}

				case SCC_GROUP_NAME: { // {GROUP}
					uint32_t id = args.GetNextParameter<uint32_t>();
					bool recurse = _settings_client.gui.show_group_hierarchy_name && (id & GROUP_NAME_HIERARCHY);
					id &= ~GROUP_NAME_HIERARCHY;
					const Group *group = Group::GetIfValid(GroupID(id));
					if (group == nullptr) break;

					auto handle_group = y_combinator([&](auto handle_group, const Group *g) -> void {
						if (recurse && g->parent != GroupID::Invalid()) {
							handle_group(Group::Get(g->parent));
							auto tmp_params = MakeParameters();
							GetStringWithArgs(builder, STR_HIERARCHY_SEPARATOR, tmp_params);
						}
						if (!g->name.empty()) {
							auto tmp_params = MakeReferenceParameters(g->name);
							GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
						} else {
							auto tmp_params = MakeParameters(g->number);
							GetStringWithArgs(builder, STR_FORMAT_GROUP_NAME, tmp_params);
						}
					});
					handle_group(group);
					break;
				}

				case SCC_INDUSTRY_NAME: { // {INDUSTRY}
					const Industry *i = Industry::GetIfValid(args.GetNextParameter<IndustryID>());
					if (i == nullptr) break;

					static bool use_cache = true;
					if (_scan_for_gender_data) {
						/* Gender is defined by the industry type.
						 * STR_FORMAT_INDUSTRY_NAME may have the town first, so it would result in the gender of the town name */
						FormatString(builder, GetStringPtr(GetIndustrySpec(i->type)->name), {}, next_substr_case_index);
					} else if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						builder += i->GetCachedName();
					} else {
						/* First print the town name and the industry type name. */
						auto tmp_params = MakeParameters(i->town->index, GetIndustrySpec(i->type)->name);
						FormatString(builder, GetStringPtr(STR_FORMAT_INDUSTRY_NAME), tmp_params, next_substr_case_index);
					}
					next_substr_case_index = 0;
					break;
				}

				case SCC_PRESIDENT_NAME: { // {PRESIDENT_NAME}
					const Company *c = Company::GetIfValid(args.GetNextParameter<CompanyID>());
					if (c == nullptr) break;

					if (!c->president_name.empty()) {
						auto tmp_params = MakeReferenceParameters(c->president_name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						auto tmp_params = MakeParameters(c->president_name_2);
						GetStringWithArgs(builder, c->president_name_1, tmp_params);
					}
					break;
				}

				case SCC_STATION_NAME: { // {STATION}
					StationID sid = args.GetNextParameter<StationID>();
					const Station *st = Station::GetIfValid(sid);

					if (st == nullptr) {
						/* The station doesn't exist anymore. The only place where we might
						 * be "drawing" an invalid station is in the case of cargo that is
						 * in transit. */
						GetStringWithArgs(builder, STR_UNKNOWN_STATION, {});
						break;
					}

					static bool use_cache = true;
					if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						builder += st->GetCachedName();
					} else if (!st->name.empty()) {
						auto tmp_params = MakeReferenceParameters(st->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						StringID string_id = st->string_id;
						if (st->indtype != IT_INVALID) {
							/* Special case where the industry provides the name for the station */
							const IndustrySpec *indsp = GetIndustrySpec(st->indtype);

							/* Industry GRFs can change which might remove the station name and
							 * thus cause very strange things. Here we check for that before we
							 * actually set the station name. */
							if (indsp->station_name != STR_NULL && indsp->station_name != STR_UNDEFINED) {
								string_id = indsp->station_name;
							}
						}
						if (st->extra_name_index != UINT16_MAX && st->extra_name_index < _extra_station_names.size()) {
							string_id = _extra_station_names[st->extra_name_index].str;
						}

						auto tmp_params = MakeParameters(STR_TOWN_NAME, st->town->index, st->index);
						GetStringWithArgs(builder, string_id, tmp_params);
					}
					break;
				}

				case SCC_TOWN_NAME: { // {TOWN}
					const Town *t = Town::GetIfValid(args.GetNextParameter<TownID>());
					if (t == nullptr) break;

					static bool use_cache = true;
					if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						builder += t->GetCachedName();
					} else if (!t->name.empty()) {
						auto tmp_params = MakeReferenceParameters(t->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						GetTownName(builder, t);
					}
					break;
				}

				case SCC_VIEWPORT_TOWN_LABEL1:
				case SCC_VIEWPORT_TOWN_LABEL2: { // {VIEWPORT_TOWN_LABEL1..2}
					int32_t t = args.GetNextParameter<int32_t>();
					uint64_t data = args.GetNextParameter<uint64_t>();

					bool tiny = (b == SCC_VIEWPORT_TOWN_LABEL2);
					StringID string_id = STR_VIEWPORT_TOWN_COLOUR;
					if (!tiny) string_id += GB(data, 40, 2);
					auto tmp_params = MakeParameters(t, GB(data, 32, 8), GB(data, 0, 32));
					GetStringWithArgs(builder, string_id, tmp_params);
					break;
				}

				case SCC_WAYPOINT_NAME: { // {WAYPOINT}
					Waypoint *wp = Waypoint::GetIfValid(args.GetNextParameter<StationID>());
					if (wp == nullptr) break;

					if (!wp->name.empty()) {
						auto tmp_params = MakeReferenceParameters(wp->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						auto tmp_params = MakeParameters(wp->town->index, wp->town_cn + 1);
						StringID string_id = ((wp->string_id == STR_SV_STNAME_BUOY) ? STR_FORMAT_BUOY_NAME : STR_FORMAT_WAYPOINT_NAME);
						if (wp->town_cn != 0) string_id++;
						GetStringWithArgs(builder, string_id, tmp_params);
					}
					break;
				}

				case SCC_VEHICLE_NAME: { // {VEHICLE}
					uint32_t id = args.GetNextParameter<uint32_t>();
					uint8_t vehicle_names = _settings_client.gui.vehicle_names;
					if (id & VEHICLE_NAME_NO_GROUP) {
						id &= ~VEHICLE_NAME_NO_GROUP;
						/* Change format from long to traditional */
						if (vehicle_names == 2) vehicle_names = 0;
					}

					const Vehicle *v = Vehicle::GetIfValid(id);
					if (v == nullptr) break;

					if (!v->name.empty()) {
						auto tmp_params = MakeReferenceParameters(v->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else if (v->group_id != DEFAULT_GROUP && vehicle_names != 0 && v->type < VEH_COMPANY_END) {
						/* The vehicle has no name, but is member of a group, so print group name */
						uint32_t group_name = v->group_id.base();
						if (_settings_client.gui.show_vehicle_group_hierarchy_name) group_name |= GROUP_NAME_HIERARCHY;
						if (vehicle_names == 1) {
							auto tmp_params = MakeParameters(group_name, v->unitnumber);
							GetStringWithArgs(builder, STR_FORMAT_GROUP_VEHICLE_NAME, tmp_params);
						} else {
							auto tmp_params = MakeParameters(group_name, STR_TRADITIONAL_TRAIN_NAME + v->type, v->unitnumber);
							GetStringWithArgs(builder, STR_FORMAT_GROUP_VEHICLE_NAME_LONG, tmp_params);
						}
					} else {
						auto tmp_params = MakeParameters(v->unitnumber);

						StringID string_id;
						if (v->type < VEH_COMPANY_END) {
							string_id = ((vehicle_names == 1) ? STR_SV_TRAIN_NAME : STR_TRADITIONAL_TRAIN_NAME) + v->type;
						} else {
							string_id = STR_INVALID_VEHICLE;
						}

						GetStringWithArgs(builder, string_id, tmp_params);
					}
					break;
				}

				case SCC_SIGN_NAME: { // {SIGN}
					const Sign *si = Sign::GetIfValid(args.GetNextParameter<SignID>());
					if (si == nullptr) break;

					if (!si->name.empty()) {
						auto tmp_params = MakeReferenceParameters(si->name);
						GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					} else {
						GetStringWithArgs(builder, STR_DEFAULT_SIGN_NAME, {});
					}
					break;
				}

				case SCC_TR_SLOT_NAME: { // {TRSLOT}
					const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(args.GetNextParameter<uint32_t>());
					if (slot == nullptr) break;
					auto tmp_params = MakeReferenceParameters(slot->name);
					GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					break;
				}

				case SCC_TR_SLOT_GROUP_NAME: { // {TRSLOTGROUP}
					const TraceRestrictSlotGroup *slot = TraceRestrictSlotGroup::GetIfValid(args.GetNextParameter<uint32_t>());
					if (slot == nullptr) break;
					auto tmp_params = MakeReferenceParameters(slot->name);
					GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					break;
				}

				case SCC_TR_COUNTER_NAME: { // {TRCOUNTER}
					const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(args.GetNextParameter<uint32_t>());
					if (ctr == nullptr) break;
					auto tmp_params = MakeReferenceParameters(ctr->name);
					GetStringWithArgs(builder, STR_JUST_RAW_STRING, tmp_params);
					break;
				}

				case SCC_STATION_FEATURES: { // {STATIONFEATURES}
					StationGetSpecialString(builder, args.GetNextParameter<StationFacilities>());
					break;
				}

				case SCC_COLOUR: { // {COLOUR}
					StringControlCode scc = (StringControlCode)(SCC_BLUE + args.GetNextParameter<Colours>());
					if (IsInsideMM(scc, SCC_BLUE, SCC_COLOUR)) builder.Utf8Encode(scc);
					break;
				}

				case SCC_CONSUME_ARG:
					// do nothing
					break;

				default:
					builder.Utf8Encode(b);
					break;
			}
		} catch (std::out_of_range &e) {
			Debug(misc, 0, "FormatString: {}", e.what());
			builder += "(invalid parameter)";
		}
	}
}


static void StationGetSpecialString(StringBuilder builder, StationFacilities x)
{
	if (x.Test(StationFacility::Train)) builder.Utf8Encode(SCC_TRAIN);
	if (x.Test(StationFacility::TruckStop)) builder.Utf8Encode(SCC_LORRY);
	if (x.Test(StationFacility::BusStop)) builder.Utf8Encode(SCC_BUS);
	if (x.Test(StationFacility::Dock)) builder.Utf8Encode(SCC_SHIP);
	if (x.Test(StationFacility::Airport)) builder.Utf8Encode(SCC_PLANE);
}

static const char * const _silly_company_names[] = {
	"Bloggs Brothers",
	"Tiny Transport Ltd.",
	"Express Travel",
	"Comfy-Coach & Co.",
	"Crush & Bump Ltd.",
	"Broken & Late Ltd.",
	"Sam Speedy & Son",
	"Supersonic Travel",
	"Mike's Motors",
	"Lightning International",
	"Pannik & Loozit Ltd.",
	"Inter-City Transport",
	"Getout & Pushit Ltd."
};

static const char * const _surname_list[] = {
	"Adams",
	"Allan",
	"Baker",
	"Bigwig",
	"Black",
	"Bloggs",
	"Brown",
	"Campbell",
	"Gordon",
	"Hamilton",
	"Hawthorn",
	"Higgins",
	"Green",
	"Gribble",
	"Jones",
	"McAlpine",
	"MacDonald",
	"McIntosh",
	"Muir",
	"Murphy",
	"Nelson",
	"O'Donnell",
	"Parker",
	"Phillips",
	"Pilkington",
	"Quigley",
	"Sharkey",
	"Thomson",
	"Watkins"
};

static const char * const _silly_surname_list[] = {
	"Grumpy",
	"Dozy",
	"Speedy",
	"Nosey",
	"Dribble",
	"Mushroom",
	"Cabbage",
	"Sniffle",
	"Fishy",
	"Swindle",
	"Sneaky",
	"Nutkins"
};

static const char _initial_name_letters[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'P', 'R', 'S', 'T', 'W',
};

static std::span<const char * const> GetSurnameOptions()
{
	if (_settings_game.game_creation.landscape == LandscapeType::Toyland) return _silly_surname_list;
	return _surname_list;
}

/**
 * Get the surname of the president with the given seed.
 * @param seed The seed the surname was generated from.
 * @return The surname.
 */
static const char *GetSurname(uint32_t seed)
{
	auto surname_options = GetSurnameOptions();
	return surname_options[surname_options.size() * GB(seed, 16, 8) >> 8];
}

static void GenAndCoName(StringBuilder &builder, uint32_t seed)
{
	builder += GetSurname(seed);
	builder += " & Co.";
}

static void GenPresidentName(StringBuilder builder, uint32_t seed)
{
	builder += _initial_name_letters[std::size(_initial_name_letters) * GB(seed, 0, 8) >> 8];
	builder += ". ";

	/* The second initial is optional. */
	size_t index = (std::size(_initial_name_letters) + 35) * GB(seed, 8, 8) >> 8;
	if (index < std::size(_initial_name_letters)) {
		builder += _initial_name_letters[index];
		builder += ". ";
	}

	builder += GetSurname(seed);
}

static bool GetSpecialNameString(StringBuilder builder, StringID string, StringParameters &args)
{
	switch (string) {
		case SPECSTR_SILLY_NAME: // Not used in new companies, but retained for old-loader savegames
			builder += _silly_company_names[std::min<size_t>(args.GetNextParameter<uint16_t>(), std::size(_silly_company_names) - 1)];
			return true;

		case SPECSTR_ANDCO_NAME: // used for Foobar & Co company names
			GenAndCoName(builder, args.GetNextParameter<uint32_t>());
			return true;

		case SPECSTR_PRESIDENT_NAME: // President name
			GenPresidentName(builder, args.GetNextParameter<uint32_t>());
			return true;
	}

	/* TownName Transport company names, with the appropriate town name. */
	if (IsInsideMM(string, SPECSTR_COMPANY_NAME_START, SPECSTR_COMPANY_NAME_END)) {
		GenerateTownNameString(builder, string - SPECSTR_COMPANY_NAME_START, args.GetNextParameter<uint32_t>());
		builder += " Transport";
		return true;
	}

	return false;
}

/**
 * Check whether the header is a valid header for OpenTTD.
 * @return true iff the header is deemed valid.
 */
bool LanguagePackHeader::IsValid() const
{
	return this->ident        == TO_LE32(LanguagePackHeader::IDENT) &&
	       this->version      == TO_LE32(LANGUAGE_PACK_VERSION) &&
	       this->plural_form  <  LANGUAGE_MAX_PLURAL &&
	       this->text_dir     <= 1 &&
	       this->newgrflangid < MAX_LANG &&
	       this->num_genders  < MAX_NUM_GENDERS &&
	       this->num_cases    < MAX_NUM_CASES &&
	       StrValid(this->name) &&
	       StrValid(this->own_name) &&
	       StrValid(this->isocode) &&
	       StrValid(this->digit_group_separator) &&
	       StrValid(this->digit_group_separator_currency) &&
	       StrValid(this->digit_decimal_separator);
}

/**
 * Check whether a translation is sufficiently finished to offer it to the public.
 */
bool LanguagePackHeader::IsReasonablyFinished() const
{
	/* "Less than 25% missing" is "sufficiently finished". */
	return 4 * this->missing < LANGUAGE_TOTAL_STRINGS;
}

/**
 * Read a particular language.
 * @param lang The metadata about the language.
 * @return Whether the loading went okay or not.
 */
bool ReadLanguagePack(const LanguageMetadata *lang)
{
	/* Current language pack */
	std::optional<UniqueBuffer<uint8_t>> result = ReadFileToBuffer(lang->file, 1U << 20);
	if (!result.has_value() || !*result) return false;

	size_t total_len = result->size();
	std::unique_ptr<LanguagePack, LanguagePackDeleter> lang_pack(reinterpret_cast<LanguagePack *>(result->release_buffer().release()));

	/* End of read data (+ terminating zero added in ReadFileToBuffer()) */
	const char *end = (char *)lang_pack.get() + total_len + 1;

	/* We need at least one byte of lang_pack->data */
	if (end <= lang_pack->data || !lang_pack->IsValid()) {
		return false;
	}

	std::array<uint, TEXT_TAB_END> tab_start, tab_num;

	uint count = 0;
	for (uint i = 0; i < TEXT_TAB_END; i++) {
		uint16_t num = FROM_LE16(lang_pack->offsets[i]);
		if (num > TAB_SIZE) return false;

		tab_start[i] = count;
		tab_num[i] = num;
		count += num;
	}

	/* Allocate offsets */
	std::vector<std::string_view> strings;

	/* Fill offsets */
	char *s = lang_pack->data;
	for (uint i = 0; i < count; i++) {
		size_t len = static_cast<uint8_t>(*s++);
		if (s + len >= end) return false;

		if (len >= 0xC0) {
			len = ((len & 0x3F) << 8) + static_cast<uint8_t>(*s++);
			if (s + len >= end) return false;
		}
		strings.emplace_back(s, len);
		s += len;
	}
	assert(strings.size() == count);

	_langpack.langpack = std::move(lang_pack);
	_langpack.strings = std::move(strings);
	_langpack.langtab_num = tab_num;
	_langpack.langtab_start = tab_start;

	_current_language = lang;
	const TextDirection old_text_dir = _current_text_dir;
	_current_text_dir = (TextDirection)_current_language->text_dir;
	const char *c_file = StrLastPathSegment(_current_language->file);
	_config_language_file = c_file;
	SetCurrentGrfLangID(_current_language->newgrflangid);
	_langpack.list_separator = GetString(STR_LIST_SEPARATOR);

#ifdef _WIN32
	extern void Win32SetCurrentLocaleName(std::string iso_code);
	Win32SetCurrentLocaleName(_current_language->isocode);
#endif

#ifdef WITH_COCOA
	extern void MacOSSetCurrentLocaleName(const char *iso_code);
	MacOSSetCurrentLocaleName(_current_language->isocode);
#endif

#ifdef WITH_ICU_I18N
	extern void ICUSetupCollators(const char *iso_code);
	ICUSetupCollators(_current_language->isocode);
#endif /* WITH_ICU_I18N */

	Layouter::Initialize();

	/* Some lists need to be sorted again after a language change. */
	ReconsiderGameScriptLanguage();
	InitializeSortedCargoSpecs();
	SortIndustryTypes();
	BuildIndustriesLegend();
	BuildContentTypeStringList();
	InvalidateWindowClassesData(WC_BUILD_VEHICLE);      // Build vehicle window.
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN);// Build template trains window.
	InvalidateWindowClassesData(WC_TRAINS_LIST);        // Train group window.
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS);// Trace restrict slots window.
	InvalidateWindowClassesData(WC_ROADVEH_LIST);       // Road vehicle group window.
	InvalidateWindowClassesData(WC_SHIPS_LIST);         // Ship group window.
	InvalidateWindowClassesData(WC_AIRCRAFT_LIST);      // Aircraft group window.
	InvalidateWindowClassesData(WC_INDUSTRY_DIRECTORY); // Industry directory window.
	InvalidateWindowClassesData(WC_STATION_LIST);       // Station list window.

	if (old_text_dir != _current_text_dir) {
		InvalidateTemplateReplacementImages();
	}

	return true;
}

/* Win32 implementation in win32.cpp.
 * OS X implementation in os/macosx/macos.mm. */
#if !(defined(_WIN32) || defined(__APPLE__))
/**
 * Determine the current charset based on the environment
 * First check some default values, after this one we passed ourselves
 * and if none exist return the value for $LANG
 * @param param environment variable to check conditionally if default ones are not
 *        set. Pass nullptr if you don't want additional checks.
 * @return return string containing current charset, or nullptr if not-determinable
 */
const char *GetCurrentLocale(const char *param)
{
	const char *env;

	env = std::getenv("LANGUAGE");
	if (env != nullptr) return env;

	env = std::getenv("LC_ALL");
	if (env != nullptr) return env;

	if (param != nullptr) {
		env = std::getenv(param);
		if (env != nullptr) return env;
	}

	return std::getenv("LANG");
}
#else
const char *GetCurrentLocale(const char *param);
#endif /* !(defined(_WIN32) || defined(__APPLE__)) */

/**
 * Get the language with the given NewGRF language ID.
 * @param newgrflangid NewGRF languages ID to check.
 * @return The language's metadata, or nullptr if it is not known.
 */
const LanguageMetadata *GetLanguage(uint8_t newgrflangid)
{
	for (const LanguageMetadata &lang : _languages) {
		if (newgrflangid == lang.newgrflangid) return &lang;
	}

	return nullptr;
}

/**
 * Reads the language file header and checks compatibility.
 * @param file the file to read
 * @param hdr  the place to write the header information to
 * @return true if and only if the language file is of a compatible version
 */
static bool GetLanguageFileHeader(const char *file, LanguagePackHeader *hdr)
{
	auto f = FileHandle::Open(file, "rb");
	if (!f.has_value()) return false;

	size_t read = fread(hdr, sizeof(*hdr), 1, *f);

	bool ret = read == 1 && hdr->IsValid();

	/* Convert endianness for the windows language ID */
	if (ret) {
		hdr->missing = FROM_LE16(hdr->missing);
		hdr->winlangid = FROM_LE16(hdr->winlangid);
	}
	return ret;
}

/**
 * Gets a list of languages from the given directory.
 * @param path  the base directory to search in
 */
static void GetLanguageList(const char *path)
{
	DIR *dir = opendir(OTTD2FS(path).c_str());
	if (dir != nullptr) {
		struct dirent *dirent;
		while ((dirent = readdir(dir)) != nullptr) {
			std::string d_name = FS2OTTD(dirent->d_name);
			const char *extension = strrchr(d_name.c_str(), '.');

			/* Not a language file */
			if (extension == nullptr || strcmp(extension, ".lng") != 0) continue;

			LanguageMetadata lmd;
			lmd.file = path;
			lmd.file += d_name;

			/* Check whether the file is of the correct version */
			if (!GetLanguageFileHeader(lmd.file.c_str(), &lmd)) {
				Debug(misc, 3, "{} is not a valid language file", lmd.file);
			} else if (GetLanguage(lmd.newgrflangid) != nullptr) {
				Debug(misc, 3, "{}'s language ID is already known", lmd.file);
			} else {
				_languages.push_back(std::move(lmd));
			}
		}
		closedir(dir);
	}
}

/**
 * Make a list of the available language packs. Put the data in
 * #_languages list.
 */
void InitializeLanguagePacks()
{
	for (Searchpath sp : _valid_searchpaths) {
		std::string path = FioGetDirectory(sp, LANG_DIR);
		GetLanguageList(path.c_str());
	}
	if (_languages.empty()) UserError("No available language packs (invalid versions?)");

	/* Acquire the locale of the current system */
	const char *lang = GetCurrentLocale("LC_MESSAGES");
	if (lang == nullptr) lang = "en_GB";

	const LanguageMetadata *chosen_language   = nullptr; ///< Matching the language in the configuration file or the current locale
	const LanguageMetadata *language_fallback = nullptr; ///< Using pt_PT for pt_BR locale when pt_BR is not available
	const LanguageMetadata *en_GB_fallback    = _languages.data(); ///< Fallback when no locale-matching language has been found

	/* Find a proper language. */
	for (const LanguageMetadata &lng : _languages) {
		/* We are trying to find a default language. The priority is by
		 * configuration file, local environment and last, if nothing found,
		 * English. */
		const char *lang_file = StrLastPathSegment(lng.file);
		if (_config_language_file == lang_file) {
			chosen_language = &lng;
			break;
		}

		if (strcmp (lng.isocode, "en_GB") == 0) en_GB_fallback    = &lng;

		/* Only auto-pick finished translations */
		if (!lng.IsReasonablyFinished()) continue;

		if (strncmp(lng.isocode, lang, 5) == 0) chosen_language   = &lng;
		if (strncmp(lng.isocode, lang, 2) == 0) language_fallback = &lng;
	}

	/* We haven't found the language in the config nor the one in the locale.
	 * Now we set it to one of the fallback languages */
	if (chosen_language == nullptr) {
		chosen_language = (language_fallback != nullptr) ? language_fallback : en_GB_fallback;
	}

	if (!ReadLanguagePack(chosen_language)) UserError("Can't read language pack '{}'", chosen_language->file);
}

/**
 * Get the ISO language code of the currently loaded language.
 * @return the ISO code.
 */
const char *GetCurrentLanguageIsoCode()
{
	return _langpack.langpack->isocode;
}

/**
 * Check whether there are glyphs missing in the current language.
 * @return If glyphs are missing, return \c true, else return \c false.
 */
bool MissingGlyphSearcher::FindMissingGlyphs()
{
	InitFontCache(this->Monospace());

	this->Reset();
	for (auto text = this->NextString(); text.has_value(); text = this->NextString()) {
		auto src = text->cbegin();

		FontSize size = this->DefaultSize();
		FontCache *fc = FontCache::Get(size);
		while (src != text->cend()) {
			char32_t c = Utf8Consume(src);

			if (c >= SCC_FIRST_FONT && c <= SCC_LAST_FONT) {
				size = (FontSize)(c - SCC_FIRST_FONT);
				fc = FontCache::Get(size);
			} else if (!IsInsideMM(c, SCC_SPRITE_START, SCC_SPRITE_END) && IsPrintable(c) && !IsTextDirectionChar(c) && fc->MapCharToGlyph(c, false) == 0) {
				/* The character is printable, but not in the normal font. This is the case we were testing for. */
				std::string size_name;

				switch (size) {
					case FS_NORMAL: size_name = "medium"; break;
					case FS_SMALL: size_name = "small"; break;
					case FS_LARGE: size_name = "large"; break;
					case FS_MONO: size_name = "mono"; break;
					default: NOT_REACHED();
				}

				Debug(fontcache, 0, "Font is missing glyphs to display char 0x{:X} in {} font size", (int)c, size_name);
				return true;
			}
		}
	}
	return false;
}

/** Helper for searching through the language pack. */
class LanguagePackGlyphSearcher : public MissingGlyphSearcher {
	uint i; ///< Iterator for the primary language tables.
	uint j; ///< Iterator for the secondary language tables.

	void Reset() override
	{
		this->i = 0;
		this->j = 0;
	}

	FontSize DefaultSize() override
	{
		return FS_NORMAL;
	}

	std::optional<std::string_view> NextString() override
	{
		if (this->i >= TEXT_TAB_END) return std::nullopt;

		std::string_view ret = _langpack.strings[_langpack.langtab_start[this->i] + this->j];

		this->j++;
		while (this->i < TEXT_TAB_END && this->j >= _langpack.langtab_num[this->i]) {
			this->i++;
			this->j = 0;
		}

		return ret;
	}

	bool Monospace() override
	{
		return false;
	}

	void SetFontNames([[maybe_unused]] FontCacheSettings *settings, [[maybe_unused]] const char *font_name, [[maybe_unused]] const void *os_data) override
	{
#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
		settings->small.font = font_name;
		settings->medium.font = font_name;
		settings->large.font = font_name;

		settings->small.os_handle = os_data;
		settings->medium.os_handle = os_data;
		settings->large.os_handle = os_data;
#endif
	}
};

/**
 * Check whether the currently loaded language pack
 * uses characters that the currently loaded font
 * does not support. If this is the case an error
 * message will be shown in English. The error
 * message will not be localized because that would
 * mean it might use characters that are not in the
 * font, which is the whole reason this check has
 * been added.
 * @param base_font Whether to look at the base font as well.
 * @param searcher  The methods to use to search for strings to check.
 *                  If nullptr the loaded language pack searcher is used.
 */
void CheckForMissingGlyphs(bool base_font, MissingGlyphSearcher *searcher)
{
	static LanguagePackGlyphSearcher pack_searcher;
	if (searcher == nullptr) searcher = &pack_searcher;
	bool bad_font = !base_font || searcher->FindMissingGlyphs();
#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
	if (bad_font) {
		/* We found an unprintable character... lets try whether we can find
		 * a fallback font that can print the characters in the current language. */
		bool any_font_configured = !_fcsettings.medium.font.empty();
		FontCacheSettings backup = _fcsettings;

		_fcsettings.mono.os_handle = nullptr;
		_fcsettings.medium.os_handle = nullptr;

		bad_font = !SetFallbackFont(&_fcsettings, _langpack.langpack->isocode, searcher);

		_fcsettings = std::move(backup);

		if (!bad_font && any_font_configured) {
			/* If the user configured a bad font, and we found a better one,
			 * show that we loaded the better font instead of the configured one.
			 * The colour 'character' might change in the
			 * future, so for safety we just Utf8 Encode it into the string,
			 * which takes exactly three characters, so it replaces the "XXX"
			 * with the colour marker. */
			static std::string err_str("XXXThe current font is missing some of the characters used in the texts for this language. Using system fallback font instead.");
			Utf8Encode(err_str.data(), SCC_YELLOW);
			ShowErrorMessage(GetEncodedString(STR_JUST_RAW_STRING, err_str), {}, WL_WARNING);
		}

		if (bad_font && base_font) {
			/* Our fallback font does miss characters too, so keep the
			 * user chosen font as that is more likely to be any good than
			 * the wild guess we made */
			InitFontCache(searcher->Monospace());
		}
	}
#endif

	if (bad_font) {
		/* All attempts have failed. Display an error. As we do not want the string to be translated by
		 * the translators, we 'force' it into the binary and 'load' it via a BindCString. To do this
		 * properly we have to set the colour of the string, otherwise we end up with a lot of artifacts.
		 * The colour 'character' might change in the future, so for safety we just Utf8 Encode it into
		 * the string, which takes exactly three characters, so it replaces the "XXX" with the colour marker. */
		static std::string err_str("XXXThe current font is missing some of the characters used in the texts for this language. Go to Help & Manuals > Fonts, or read the file docs/fonts.md in your OpenTTD directory, to see how to solve this.");
		Utf8Encode(err_str.data(), SCC_YELLOW);
		ShowErrorMessage(GetEncodedString(STR_JUST_RAW_STRING, err_str), {}, WL_WARNING);

		/* Reset the font width */
		LoadStringWidthTable(searcher->Monospace());
		ReInitAllWindows(false);
		return;
	}

	/* Update the font with cache */
	LoadStringWidthTable(searcher->Monospace());
	ReInitAllWindows(false);

#if !(defined(WITH_ICU_I18N) && defined(WITH_HARFBUZZ)) && !defined(WITH_UNISCRIBE) && !defined(WITH_COCOA)
	/*
	 * For right-to-left languages we need the ICU library. If
	 * we do not have support for that library we warn the user
	 * about it with a message. As we do not want the string to
	 * be translated by the translators, we 'force' it into the
	 * binary and 'load' it via a BindCString. To do this
	 * properly we have to set the colour of the string,
	 * otherwise we end up with a lot of artifacts. The colour
	 * 'character' might change in the future, so for safety
	 * we just Utf8 Encode it into the string, which takes
	 * exactly three characters, so it replaces the "XXX" with
	 * the colour marker.
	 */
	if (_current_text_dir != TD_LTR) {
		static std::string err_str("XXXThis version of OpenTTD does not support right-to-left languages. Recompile with ICU + Harfbuzz enabled.");
		Utf8Encode(err_str.data(), SCC_YELLOW);
		ShowErrorMessage(GetEncodedString(STR_JUST_RAW_STRING, err_str), {}, WL_ERROR);
	}
#endif /* !(WITH_ICU_I18N && WITH_HARFBUZZ) && !WITH_UNISCRIBE && !WITH_COCOA */
}
