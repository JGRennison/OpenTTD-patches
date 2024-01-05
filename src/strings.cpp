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
#include "strings_func.h"
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
#include "core/y_combinator.hpp"
#include <stack>
#include <cmath>
#include <optional>

#include "table/strings.h"
#include "table/control_codes.h"

#include "safeguards.h"

std::string _config_language_file;                ///< The file (name) stored in the configuration.
LanguageList _languages;                          ///< The actual list of language meta data.
const LanguageMetadata *_current_language = nullptr; ///< The currently loaded language.

TextDirection _current_text_dir = TD_LTR; ///< Text direction of the currently selected language.

#ifdef WITH_ICU_I18N
std::unique_ptr<icu::Collator> _current_collator;    ///< Collator for the language currently in use.
#endif /* WITH_ICU_I18N */

ArrayStringParameters<20> _global_string_params;

std::string _temp_special_strings[16];

/**
 * Prepare the string parameters for the next formatting run. This means
 * resetting the type information and resetting the offset to the begin.
 */
void StringParameters::PrepareForNextRun()
{
	for (auto &param : this->parameters) param.type = 0;
	this->offset = 0;
}

/**
 * Get the next parameter from our parameters.
 * This updates the offset, so the next time this is called the next parameter
 * will be read.
 * @return The pointer to the next parameter.
 */
StringParameter *StringParameters::GetNextParameterPointer()
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
	return &param;
}

/**
 * Set DParam n to some number that is suitable for string size computations.
 * @param n Index of the string parameter.
 * @param max_value The biggest value which shall be displayed.
 *                  For the result only the number of digits of \a max_value matter.
 * @param min_count Minimum number of digits independent of \a max.
 * @param size  Font of the number
 */
void SetDParamMaxValue(size_t n, uint64 max_value, uint min_count, FontSize size)
{
	uint num_digits = 1;
	while (max_value >= 10) {
		num_digits++;
		max_value /= 10;
	}
	SetDParamMaxDigits(n, std::max(min_count, num_digits), size);
}

/**
 * Set DParam n to some number that is suitable for string size computations.
 * @param n Index of the string parameter.
 * @param count Number of digits which shall be displayable.
 * @param size  Font of the number
 */
void SetDParamMaxDigits(size_t n, uint count, FontSize size)
{
	SetDParam(n, GetBroadestDigitsValue(count, size));
}

/**
 * Copy the parameters from the backup into the global string parameter array.
 * @param backup The backup to copy from.
 */
void CopyInDParam(const span<const StringParameterBackup> backup, uint offset)
{
	for (size_t i = 0; i < backup.size(); i++) {
		auto &value = backup[i];
		if (value.string.has_value()) {
			_global_string_params.SetParam(i + offset, value.string.value());
		} else {
			_global_string_params.SetParam(i + offset, value.data);
		}
	}
}

/**
 * Copy \a num string parameters from the global string parameter array to the \a backup.
 * @param backup The backup to write to.
 * @param num Number of string parameters to copy.
 */
void CopyOutDParam(std::vector<StringParameterBackup> &backup, size_t num)
{
	backup.resize(num);
	for (size_t i = 0; i < backup.size(); i++) {
		const char *str = _global_string_params.GetParamStr(i);
		if (str != nullptr) {
			backup[i] = str;
		} else {
			backup[i] = _global_string_params.GetParam(i);
		}
	}
}

/**
 * Checks whether the global string parameters have changed compared to the given backup.
 * @param backup The backup to check against.
 * @return True when the parameters have changed, otherwise false.
 */
bool HaveDParamChanged(const std::vector<StringParameterBackup> &backup)
{
	bool changed = false;
	for (size_t i = 0; !changed && i < backup.size(); i++) {
		bool global_has_string = _global_string_params.GetParamStr(i) != nullptr;
		if (global_has_string != backup[i].string.has_value()) return true;

		if (global_has_string) {
			changed = backup[i].string.value() != _global_string_params.GetParamStr(i);
		} else {
			changed = backup[i].data != _global_string_params.GetParam(i);
		}
	}
	return changed;
}

static char *StationGetSpecialString(char *buff, StationFacility x, const char *last);
static char *GetSpecialTownNameString(char *buff, int ind, uint32 seed, const char *last);
static char *GetSpecialNameString(char *buff, int ind, StringParameters &args, const char *last);

static char *FormatString(char *buff, const char *str, StringParameters &args, const char *last, uint case_index = 0, bool game_script = false, bool dry_run = false);

struct LanguagePack : public LanguagePackHeader {
	char data[]; // list of strings

	inline void operator delete(void *ptr) { ::operator delete (ptr); }
};

struct LanguagePackDeleter {
	void operator()(LanguagePack *langpack)
	{
		/* LanguagePack is in fact reinterpreted char[], we need to reinterpret it back to free it properly. */
		delete[] reinterpret_cast<char*>(langpack);
	}
};

struct LoadedLanguagePack {
	std::unique_ptr<LanguagePack, LanguagePackDeleter> langpack;

	std::vector<char *> offsets;

	std::array<uint, TEXT_TAB_END> langtab_num;   ///< Offset into langpack offs
	std::array<uint, TEXT_TAB_END> langtab_start; ///< Offset into langpack offs
};

static LoadedLanguagePack _langpack;

static bool _scan_for_gender_data = false;  ///< Are we scanning for the gender of the current string? (instead of formatting it)


const char *GetStringPtr(StringID string)
{
	switch (GetStringTab(string)) {
		case TEXT_TAB_GAMESCRIPT_START: return GetGameStringPtr(GetStringIndex(string));
		/* 0xD0xx and 0xD4xx IDs have been converted earlier. */
		case TEXT_TAB_OLD_NEWGRF: NOT_REACHED();
		case TEXT_TAB_NEWGRF_START: return GetGRFStringPtr(GetStringIndex(string));
		default: return _langpack.offsets[_langpack.langtab_start[GetStringTab(string)] + GetStringIndex(string)];
	}
}

/**
 * Get a parsed string with most special stringcodes replaced by the string parameters.
 * @param buffr  Pointer to a string buffer where the formatted string should be written to.
 * @param string
 * @param args   Arguments for the string.
 * @param last   Pointer just past the end of \a buffr.
 * @param case_index  The "case index". This will only be set when FormatString wants to print the string in a different case.
 * @param game_script The string is coming directly from a game script.
 * @return       Pointer to the final zero byte of the formatted string.
 */
char *GetStringWithArgs(char *buffr, StringID string, StringParameters &args, const char *last, uint case_index, bool game_script)
{
	if (string == 0) return GetStringWithArgs(buffr, STR_UNDEFINED, args, last);

	uint index = GetStringIndex(string);
	StringTab tab = GetStringTab(string);

	switch (tab) {
		case TEXT_TAB_TOWN:
			if (index >= 0xC0 && !game_script) {
				return GetSpecialTownNameString(buffr, index - 0xC0, args.GetNextParameter<uint32>(), last);
			}
			break;

		case TEXT_TAB_SPECIAL:
			if (index >= 0xE4 && !game_script) {
				return GetSpecialNameString(buffr, index - 0xE4, args, last);
			}
			if (index < lengthof(_temp_special_strings) && !game_script) {
				return FormatString(buffr, _temp_special_strings[index].c_str(), args, last, case_index);
			}
			break;

		case TEXT_TAB_OLD_CUSTOM:
			/* Old table for custom names. This is no longer used */
			if (!game_script) {
				error("Incorrect conversion of custom name string.");
			}
			break;

		case TEXT_TAB_GAMESCRIPT_START:
			return FormatString(buffr, GetGameStringPtr(index), args, last, case_index, true);

		case TEXT_TAB_OLD_NEWGRF:
			NOT_REACHED();

		case TEXT_TAB_NEWGRF_START:
			return FormatString(buffr, GetGRFStringPtr(index), args, last, case_index);

		default:
			break;
	}

	if (index >= _langpack.langtab_num[tab]) {
		if (game_script) {
			return GetStringWithArgs(buffr, STR_UNDEFINED, args, last);
		}
		error("String 0x%X is invalid. You are probably using an old version of the .lng file.\n", string);
	}

	return FormatString(buffr, GetStringPtr(string), args, last, case_index);
}

char *GetString(char *buffr, StringID string, const char *last)
{
	_global_string_params.PrepareForNextRun();
	return GetStringWithArgs(buffr, string, _global_string_params, last);
}

/**
 * Resolve the given StringID into a std::string with all the associated
 * DParam lookups and formatting.
 * @param string The unique identifier of the translatable string.
 * @return The std::string of the translated string.
 */
std::string GetString(StringID string)
{
	char buffer[DRAW_STRING_BUFFER];
	char *last = GetString(buffer, string, lastof(buffer));
	return { buffer, last };
}

/**
 * This function is used to "bind" a C string to a OpenTTD dparam slot.
 * @param n slot of the string
 * @param str string to bind
 */
void SetDParamStr(size_t n, const char *str)
{
	_global_string_params.SetParam(n, str);
}

/**
 * This function is used to "bind" the std::string to a OpenTTD dparam slot.
 * Contrary to the other \c SetDParamStr function, this moves the string into
 * the parameter slot.
 * @param n slot of the string
 * @param str string to bind
 */
void SetDParamStr(size_t n, std::string str)
{
	_global_string_params.SetParam(n, std::move(str));
}

/**
 * Format a number into a string.
 * @param buff      the buffer to write to
 * @param number    the number to write down
 * @param last      the last element in the buffer
 * @param separator the thousands-separator to use
 * @param zerofill  minimum number of digits to print for the integer part. The number will be filled with zeros at the front if necessary.
 * @param fractional_digits number of fractional digits to display after a decimal separator. The decimal separator is inserted
 *                          in front of the \a fractional_digits last digit of \a number.
 * @return till where we wrote
 */
static char *FormatNumber(char *buff, int64 number, const char *last, const char *separator, int zerofill = 1, int fractional_digits = 0)
{
	static const int max_digits = 20;
	uint64 divisor = 10000000000000000000ULL;
	zerofill += fractional_digits;
	int thousands_offset = (max_digits - fractional_digits - 1) % 3;

	if (number < 0) {
		if (buff != last) *buff++ = '-';
		number = -number;
	}

	uint64 num = number;
	uint64 tot = 0;
	for (int i = 0; i < max_digits; i++) {
		if (i == max_digits - fractional_digits) {
			const char *decimal_separator = _settings_game.locale.digit_decimal_separator.c_str();
			if (StrEmpty(decimal_separator)) decimal_separator = _langpack.langpack->digit_decimal_separator;
			buff = strecpy(buff, decimal_separator, last);
		}

		uint64 quot = 0;
		if (num >= divisor) {
			quot = num / divisor;
			num = num % divisor;
		}
		if ((tot |= quot) || i >= max_digits - zerofill) {
			if (buff != last) *buff++ = '0' + quot; // quot is a single digit
			if ((i % 3) == thousands_offset && i < max_digits - 1 - fractional_digits) buff = strecpy(buff, separator, last);
		}

		divisor /= 10;
	}

	*buff = '\0';

	return buff;
}

static char *FormatCommaNumber(char *buff, int64 number, const char *last, int fractional_digits = 0)
{
	const char *separator = _settings_game.locale.digit_group_separator.c_str();
	if (StrEmpty(separator)) separator = _langpack.langpack->digit_group_separator;
	return FormatNumber(buff, number, last, separator, 1, fractional_digits);
}

static char *FormatNoCommaNumber(char *buff, int64 number, const char *last)
{
	return FormatNumber(buff, number, last, "");
}

static char *FormatZerofillNumber(char *buff, int64 number, int count, const char *last)
{
	return FormatNumber(buff, number, last, "", count);
}

static char *FormatHexNumber(char *buff, uint64 number, const char *last)
{
	return buff + seprintf(buff, last, "0x" OTTD_PRINTFHEX64, number);
}

WChar GetDecimalSeparatorChar()
{
	WChar decimal_char = '.';
	const char *decimal_separator = _settings_game.locale.digit_decimal_separator.c_str();
	if (StrEmpty(decimal_separator)) decimal_separator = _langpack.langpack->digit_decimal_separator;
	if (!StrEmpty(decimal_separator)) Utf8Decode(&decimal_char, decimal_separator);
	return decimal_char;
}

/**
 * Format a given number as a number of bytes with the SI prefix.
 * @param buff   the buffer to write to
 * @param number the number of bytes to write down
 * @param last   the last element in the buffer
 * @return till where we wrote
 */
static char *FormatBytes(char *buff, int64 number, const char *last)
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
		buff += seprintf(buff, last, "%i", (int)number);
	} else if (number < 1024 * 10) {
		buff += seprintf(buff, last, "%i%s%02i", (int)number / 1024, decimal_separator, (int)(number % 1024) * 100 / 1024);
	} else if (number < 1024 * 100) {
		buff += seprintf(buff, last, "%i%s%01i", (int)number / 1024, decimal_separator, (int)(number % 1024) * 10 / 1024);
	} else {
		assert(number < 1024 * 1024);
		buff += seprintf(buff, last, "%i", (int)number / 1024);
	}

	assert(id < lengthof(iec_prefixes));
	buff += seprintf(buff, last, NBSP "%sB", iec_prefixes[id]);

	return buff;
}

static char *FormatWallClockString(char *buff, DateTicksScaled ticks, const char *last, bool show_date, uint case_index)
{
	TickMinutes minutes = _settings_time.ToTickMinutes(ticks);
	char hour[3], minute[3];
	seprintf(hour,   lastof(hour),   "%02i", minutes.ClockHour());
	seprintf(minute, lastof(minute), "%02i", minutes.ClockMinute());
	if (show_date) {
		Date date = ScaledDateTicksToDate(ticks);
		int64 final_arg;
		if (_settings_client.gui.date_with_time == 1) {
			YearMonthDay ymd = ConvertDateToYMD(date);
			final_arg = ymd.year;
		} else {
			final_arg = date.base();
		}
		auto tmp_params = MakeParameters(hour, minute, final_arg);
		return FormatString(buff, GetStringPtr(STR_FORMAT_DATE_MINUTES + _settings_client.gui.date_with_time), tmp_params, last, case_index);
	} else {
		auto tmp_params = MakeParameters(hour, minute);
		return FormatString(buff, GetStringPtr(STR_FORMAT_DATE_MINUTES), tmp_params, last, case_index);
	}
}

static char *FormatTimeHHMMString(char *buff, uint time, const char *last, uint case_index)
{
	char hour[9], minute[3];
	seprintf(hour,   lastof(hour),   "%02i", (int) time / 100);
	seprintf(minute, lastof(minute), "%02i", (int) time % 100);
	auto tmp_params = MakeParameters(hour, minute);
	return FormatString(buff, GetStringPtr(STR_FORMAT_DATE_MINUTES), tmp_params, last, case_index);
}

static char *FormatYmdString(char *buff, Date date, const char *last, uint case_index)
{
	YearMonthDay ymd = ConvertDateToYMD(date);

	auto tmp_params = MakeParameters(ymd.day + STR_DAY_NUMBER_1ST - 1, STR_MONTH_ABBREV_JAN + ymd.month, ymd.year);
	return FormatString(buff, GetStringPtr(STR_FORMAT_DATE_LONG), tmp_params, last, case_index);
}

static char *FormatMonthAndYear(char *buff, Date date, const char *last, uint case_index)
{
	YearMonthDay ymd = ConvertDateToYMD(date);

	auto tmp_params = MakeParameters(STR_MONTH_JAN + ymd.month, ymd.year);
	return FormatString(buff, GetStringPtr(STR_FORMAT_DATE_SHORT), tmp_params, last, case_index);
}

static char *FormatTinyOrISODate(char *buff, Date date, StringID str, const char *last)
{
	YearMonthDay ymd = ConvertDateToYMD(date);

	/* Day and month are zero-padded with ZEROFILL_NUM, hence the two 2s. */
	auto tmp_params = MakeParameters(ymd.day, 2, ymd.month + 1, 2, ymd.year);
	return FormatString(buff, GetStringPtr(str), tmp_params, last);
}

static char *FormatGenericCurrency(char *buff, const CurrencySpec *spec, Money number, bool compact, const char *last)
{
	/* We are going to make number absolute for printing, so
	 * keep this piece of data as we need it later on */
	bool negative = number < 0;
	const char *multiplier = "";

	number *= spec->rate;

	/* convert from negative */
	if (number < 0) {
		if (buff + Utf8CharLen(SCC_PUSH_COLOUR) > last) return buff;
		buff += Utf8Encode(buff, SCC_PUSH_COLOUR);
		if (buff + Utf8CharLen(SCC_RED) > last) return buff;
		buff += Utf8Encode(buff, SCC_RED);
		buff = strecpy(buff, "-", last);
		number = -number;
	}

	/* Add prefix part, following symbol_pos specification.
	 * Here, it can can be either 0 (prefix) or 2 (both prefix and suffix).
	 * The only remaining value is 1 (suffix), so everything that is not 1 */
	if (spec->symbol_pos != 1) buff = strecpy(buff, spec->prefix.c_str(), last);

	/* for huge numbers, compact the number into k or M */
	if (compact) {
		/* Take care of the 'k' rounding. Having 1 000 000 k
		 * and 1 000 M is inconsistent, so always use 1 000 M. */
		if (number >= 1000000000 - 500) {
			number = (number + 500000) / 1000000;
			multiplier = NBSP "M";
		} else if (number >= 1000000) {
			number = (number + 500) / 1000;
			multiplier = NBSP "k";
		}
	}

	const char *separator = _settings_game.locale.digit_group_separator_currency.c_str();
	if (StrEmpty(separator)) separator = _currency->separator.c_str();
	if (StrEmpty(separator)) separator = _langpack.langpack->digit_group_separator_currency;
	buff = FormatNumber(buff, number, last, separator);
	buff = strecpy(buff, multiplier, last);

	/* Add suffix part, following symbol_pos specification.
	 * Here, it can can be either 1 (suffix) or 2 (both prefix and suffix).
	 * The only remaining value is 1 (prefix), so everything that is not 0 */
	if (spec->symbol_pos != 0) buff = strecpy(buff, spec->suffix.c_str(), last);

	if (negative) {
		if (buff + Utf8CharLen(SCC_POP_COLOUR) > last) return buff;
		buff += Utf8Encode(buff, SCC_POP_COLOUR);
		*buff = '\0';
	}

	return buff;
}

/**
 * Determine the "plural" index given a plural form and a number.
 * @param count       The number to get the plural index of.
 * @param plural_form The plural form we want an index for.
 * @return The plural index for the given form.
 */
static int DeterminePluralForm(int64 count, int plural_form)
{
	/* The absolute value determines plurality */
	uint64 n = abs(count);

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

static const char *ParseStringChoice(const char *b, uint form, char **dst, const char *last)
{
	/* <NUM> {Length of each string} {each string} */
	uint n = (byte)*b++;
	uint pos, i, mypos = 0;

	for (i = pos = 0; i != n; i++) {
		uint len = (byte)*b++;
		if (i == form) mypos = pos;
		pos += len;
	}

	*dst += seprintf(*dst, last, "%s", b + mypos);
	return b + pos;
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
	int64 ToDisplay(int64 input, bool round = true) const
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
	int64 FromDisplay(int64 input, bool round = true, int64 divider = 1) const
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
static const Units _units_velocity[] = {
	{ { 1.0      }, STR_UNITS_VELOCITY_IMPERIAL,  0 },
	{ { 1.609344 }, STR_UNITS_VELOCITY_METRIC,    0 },
	{ { 0.44704  }, STR_UNITS_VELOCITY_SI,        0 },
	{ { 0.578125 }, STR_UNITS_VELOCITY_GAMEUNITS, 1 },
	{ { 0.868976 }, STR_UNITS_VELOCITY_KNOTS,     0 },
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

/**
 * Get index for velocity conversion units for a vehicle type.
 * @param type VehicleType to convert velocity for.
 * @return Index within velocity conversion units for vehicle type.
 */
static byte GetVelocityUnits(VehicleType type)
{
	if (type == VEH_SHIP || type == VEH_AIRCRAFT) return _settings_game.locale.units_velocity_nautical;

	return _settings_game.locale.units_velocity;
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
	return _units_velocity[GetVelocityUnits(type)].c.ToDisplay(speed, false);
}

/**
 * Convert the given (internal) speed to the display speed, in units (not decimal values).
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertSpeedToUnitDisplaySpeed(uint speed, VehicleType type)
{
	uint result = ConvertSpeedToDisplaySpeed(speed, type);
	for (uint i = 0; i < _units_velocity[_settings_game.locale.units_velocity].decimal_places; i++) {
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
	return _units_velocity[GetVelocityUnits(type)].c.FromDisplay(speed);
}

/**
 * Convert the given km/h-ish speed to the display speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertKmhishSpeedToDisplaySpeed(uint speed, VehicleType type)
{
	return _units_velocity[GetVelocityUnits(type)].c.ToDisplay(speed * 10, false) / 16;
}

/**
 * Convert the given display speed to the km/h-ish speed.
 * @param speed the speed to convert
 * @return the converted speed.
 */
uint ConvertDisplaySpeedToKmhishSpeed(uint speed, VehicleType type)
{
	return _units_velocity[GetVelocityUnits(type)].c.FromDisplay(speed * 16, true, 10);
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
int64 ConvertForceToDisplayForce(int64 force)
{
	return _units_force[_settings_game.locale.units_force].c.ToDisplay(force);
}

/**
 * Convert the given display force to the (internal) force.
 * @param force the force to convert
 * @return the converted force.
 */
int64 ConvertDisplayForceToForce(int64 force)
{
	return _units_force[_settings_game.locale.units_force].c.FromDisplay(force);
}

static void ConvertWeightRatioToDisplay(const Units &unit, int64 ratio, int64 &value, int64 &decimals)
{
	int64 input = ratio;
	decimals = 2;
	if (_settings_game.locale.units_weight == 2) {
		input *= 1000;
		decimals += 3;
	}

	const UnitConversion &weight_conv = _units_weight[_settings_game.locale.units_weight].c;
	UnitConversion conv = unit.c;
	conv.factor /= weight_conv.factor;

	value = conv.ToDisplay(input);

	if (unit.c.factor > 100) {
		value /= 100;
		decimals -= 2;
	}
}

static uint ConvertDisplayToWeightRatio(const Units &unit, double in)
{
	const UnitConversion &weight_conv = _units_weight[_settings_game.locale.units_weight].c;
	UnitConversion conv = unit.c;
	conv.factor /= weight_conv.factor;
	int64 multiplier = _settings_game.locale.units_weight == 2 ? 1000 : 1;

	return conv.FromDisplay(in * 100 * multiplier, true, multiplier);
}

static char *FormatUnitWeightRatio(char *buff, const char *last, const Units &unit, int64 raw_value)
{
	const char *unit_str = GetStringPtr(unit.s);
	const char *weight_str = GetStringPtr(_units_weight[_settings_game.locale.units_weight].s);

	char tmp_buffer[128];
	char *insert_pt = strecpy(tmp_buffer, unit_str, lastof(tmp_buffer));
	strecpy(insert_pt, weight_str, lastof(tmp_buffer));
	str_replace_wchar(insert_pt, lastof(tmp_buffer), SCC_DECIMAL, '/');
	str_replace_wchar(insert_pt, lastof(tmp_buffer), 0xA0 /* NBSP */, 0);

	int64 value, decimals;
	ConvertWeightRatioToDisplay(unit, raw_value, value, decimals);

	auto tmp_params = MakeParameters(value, decimals);
	buff = FormatString(buff, tmp_buffer, tmp_params, last);
	return buff;
}

/**
 * Convert the given internal power / weight ratio to the display decimal.
 * @param ratio the power / weight ratio to convert
 * @param value the output value
 * @param decimals the output decimal offset
 */
void ConvertPowerWeightRatioToDisplay(int64 ratio, int64 &value, int64 &decimals)
{
	ConvertWeightRatioToDisplay(_units_power[_settings_game.locale.units_power], ratio, value, decimals);
}

/**
 * Convert the given internal force / weight ratio to the display decimal.
 * @param ratio the force / weight ratio to convert
 * @param value the output value
 * @param decimals the output decimal offset
 */
void ConvertForceWeightRatioToDisplay(int64 ratio, int64 &value, int64 &decimals)
{
	ConvertWeightRatioToDisplay(_units_force[_settings_game.locale.units_force], ratio, value, decimals);
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

uint ConvertCargoQuantityToDisplayQuantity(CargoID cargo, uint quantity)
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

uint ConvertDisplayQuantityToCargoQuantity(CargoID cargo, uint quantity)
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
 * Parse most format codes within a string and write the result to a buffer.
 * @param buff    The buffer to write the final string to.
 * @param str_arg The original string with format codes.
 * @param args    Pointer to extra arguments used by various string codes.
 * @param last    Pointer to just past the end of the buff array.
 * @param dry_run True when the argt array is not yet initialized.
 */
static char *FormatString(char *buff, const char *str_arg, StringParameters &args, const char *last, uint case_index, bool game_script, bool dry_run)
{
	size_t orig_offset = args.GetOffset();

	/* When there is no array with types there is no need to do a dry run. */
	if (!dry_run) {
		if (UsingNewGRFTextStack()) {
			/* Values from the NewGRF text stack are only copied to the normal
			 * argv array at the time they are encountered. That means that if
			 * another string command references a value later in the string it
			 * would fail. We solve that by running FormatString twice. The first
			 * pass makes sure the argv array is correctly filled and the second
			 * pass can reference later values without problems. */
			struct TextRefStack *backup = CreateTextRefStackBackup();
			FormatString(buff, str_arg, args, last, case_index, game_script, true);
			RestoreTextRefStackBackup(backup);
		} else {
			FormatString(buff, str_arg, args, last, case_index, game_script, true);
		}
		/* We have to restore the original offset here to to read the correct values. */
		args.SetOffset(orig_offset);
	}
	WChar b = '\0';
	uint next_substr_case_index = 0;
	char *buf_start = buff;
	std::stack<const char *, std::vector<const char *>> str_stack;
	str_stack.push(str_arg);

	for (;;) {
		try {
			while (!str_stack.empty() && (b = Utf8Consume(&str_stack.top())) == '\0') {
				str_stack.pop();
			}
			if (str_stack.empty()) break;
			const char *&str = str_stack.top();

			if (SCC_NEWGRF_FIRST <= b && b <= SCC_NEWGRF_LAST) {
				/* We need to pass some stuff as it might be modified. */
				StringParameters remaining = args.GetRemainingParameters();
				b = RemapNewGRFStringControlCode(b, buf_start, &buff, &str, remaining, dry_run);
				if (b == 0) continue;
			}

			if (b < SCC_CONTROL_START || b > SCC_CONTROL_END) {
				if (buff + Utf8CharLen(b) < last) buff += Utf8Encode(buff, b);
				continue;
			}

			args.SetTypeOfNextParameter(b);
			switch (b) {
				case SCC_ENCODED: {
					ArrayStringParameters<20> sub_args;

					char *p;
					uint32 stringid = std::strtoul(str, &p, 16);
					if (*p != ':' && *p != '\0') {
						while (*p != '\0') p++;
						str = p;
						buff = strecpy(buff, "(invalid SCC_ENCODED)", last);
						break;
					}
					if (stringid >= TAB_SIZE_GAMESCRIPT) {
						while (*p != '\0') p++;
						str = p;
						buff = strecpy(buff, "(invalid StringID)", last);
						break;
					}

					int i = 0;
					while (*p != '\0' && i < 20) {
						uint64 param;
						const char *s = ++p;

						/* Find the next value */
						bool instring = false;
						bool escape = false;
						for (;; p++) {
							if (*p == '\\') {
								escape = true;
								continue;
							}
							if (*p == '"' && escape) {
								escape = false;
								continue;
							}
							escape = false;

							if (*p == '"') {
								instring = !instring;
								continue;
							}
							if (instring) {
								continue;
							}

							if (*p == ':') break;
							if (*p == '\0') break;
						}

						if (*s != '"') {
							/* Check if we want to look up another string */
							WChar l;
							size_t len = Utf8Decode(&l, s);
							bool lookup = (l == SCC_ENCODED);
							if (lookup) s += len;

							param = std::strtoull(s, &p, 16);

							if (lookup) {
								if (param >= TAB_SIZE_GAMESCRIPT) {
									while (*p != '\0') p++;
									str = p;
									buff = strecpy(buff, "(invalid sub-StringID)", last);
									break;
								}
								param = MakeStringID(TEXT_TAB_GAMESCRIPT_START, param);
							}

							sub_args.SetParam(i++, param);
						} else {
							s++; // skip the leading \"
							sub_args.SetParam(i++, std::string(s, p - s - 1)); // also skip the trailing \".
						}
					}
					/* If we didn't error out, we can actually print the string. */
					if (*str != '\0') {
						str = p;
						buff = GetStringWithArgs(buff, MakeStringID(TEXT_TAB_GAMESCRIPT_START, stringid), sub_args, last, true);
					}
					break;
				}

				case SCC_NEWGRF_STRINL: {
					StringID substr = Utf8Consume(&str);
					str_stack.push(GetStringPtr(substr));
					break;
				}

				case SCC_NEWGRF_PRINT_WORD_STRING_ID: {
					StringID substr = args.GetNextParameter<StringID>();
					str_stack.push(GetStringPtr(substr));
					case_index = next_substr_case_index;
					next_substr_case_index = 0;
					break;
				}


				case SCC_GENDER_LIST: { // {G 0 Der Die Das}
					/* First read the meta data from the language file. */
					size_t offset = orig_offset + (byte)*str++;
					int gender = 0;
					if (!dry_run && args.GetTypeAtOffset(offset) != 0) {
						/* Now we need to figure out what text to resolve, i.e.
						 * what do we need to draw? So get the actual raw string
						 * first using the control code to get said string. */
						char input[4 + 1];
						char *p = input + Utf8Encode(input, args.GetTypeAtOffset(offset));
						*p = '\0';

						/* Now do the string formatting. */
						char buf[256];
						bool old_sgd = _scan_for_gender_data;
						_scan_for_gender_data = true;
						StringParameters tmp_params = args.GetRemainingParameters(offset);
						p = FormatString(buf, input, tmp_params, lastof(buf));
						_scan_for_gender_data = old_sgd;
						*p = '\0';

						/* And determine the string. */
						const char *s = buf;
						WChar c = Utf8Consume(&s);
						/* Does this string have a gender, if so, set it */
						if (c == SCC_GENDER_INDEX) gender = (byte)s[0];
					}
					str = ParseStringChoice(str, gender, &buff, last);
					break;
				}

				/* This sets up the gender for the string.
				 * We just ignore this one. It's used in {G 0 Der Die Das} to determine the case. */
				case SCC_GENDER_INDEX: // {GENDER 0}
					if (_scan_for_gender_data) {
						buff += Utf8Encode(buff, SCC_GENDER_INDEX);
						*buff++ = *str++;
					} else {
						str++;
					}
					break;

				case SCC_PLURAL_LIST: { // {P}
					int plural_form = *str++;          // contains the plural form for this string
					size_t offset = orig_offset + (byte)*str++;
					int64 v = args.GetParam(offset); // contains the number that determines plural
					str = ParseStringChoice(str, DeterminePluralForm(v, plural_form), &buff, last);
					break;
				}

				case SCC_ARG_INDEX: { // Move argument pointer
					args.SetOffset(orig_offset + (byte)*str++);
					break;
				}

				case SCC_SET_CASE: { // {SET_CASE}
					/* This is a pseudo command, it's outputted when someone does {STRING.ack}
					 * The modifier is added to all subsequent GetStringWithArgs that accept the modifier. */
					next_substr_case_index = (byte)*str++;
					break;
				}

				case SCC_SWITCH_CASE: { // {Used to implement case switching}
					/* <0x9E> <NUM CASES> <CASE1> <LEN1> <STRING1> <CASE2> <LEN2> <STRING2> <CASE3> <LEN3> <STRING3> <STRINGDEFAULT>
					 * Each LEN is printed using 2 bytes in big endian order. */
					uint num = (byte)*str++;
					while (num) {
						if ((byte)str[0] == case_index) {
							/* Found the case, adjust str pointer and continue */
							str += 3;
							break;
						}
						/* Otherwise skip to the next case */
						str += 3 + (str[1] << 8) + str[2];
						num--;
					}
					break;
				}

				case SCC_REVISION: // {REV}
					buff = strecpy(buff, _openttd_revision, last);
					break;

				case SCC_RAW_STRING_POINTER: { // {RAW_STRING}
					const char *raw_string = args.GetNextParameterString();
					/* raw_string can be nullptr. */
					if (raw_string == nullptr) {
						buff = strecpy(buff, "(invalid RAW_STRING parameter)", last);
						break;
					}
					buff = FormatString(buff, raw_string, args, last);
					break;
				}

				case SCC_STRING: {// {STRING}
					StringID string_id = args.GetNextParameter<StringID>();
					if (game_script && GetStringTab(string_id) != TEXT_TAB_GAMESCRIPT_START) break;
					/* It's prohibited for the included string to consume any arguments. */
					StringParameters tmp_params(args, game_script ? args.GetDataLeft() : 0);
					buff = GetStringWithArgs(buff, string_id, tmp_params, last, next_substr_case_index, game_script);
					next_substr_case_index = 0;
					break;
				}

				case SCC_STRING1:
				case SCC_STRING2:
				case SCC_STRING3:
				case SCC_STRING4:
				case SCC_STRING5:
				case SCC_STRING6:
				case SCC_STRING7:
				case SCC_STRING8: { // {STRING1..8}
					/* Strings that consume arguments */
					StringID string_id = args.GetNextParameter<StringID>();
					if (game_script && GetStringTab(string_id) != TEXT_TAB_GAMESCRIPT_START) break;
					uint size = b - SCC_STRING1 + 1;
					if (size > args.GetDataLeft()) {
						buff = strecpy(buff, "(too many parameters)", last);
					} else {
						StringParameters sub_args(args, game_script ? args.GetDataLeft() : size);
						buff = GetStringWithArgs(buff, string_id, sub_args, last, next_substr_case_index, game_script);
						args.AdvanceOffset(size);
					}
					next_substr_case_index = 0;
					break;
				}

				case SCC_COMMA: // {COMMA}
					buff = FormatCommaNumber(buff, args.GetNextParameter<int64>(), last);
					break;

				case SCC_DECIMAL: {// {DECIMAL}
					int64 number = args.GetNextParameter<int64>();
					int digits = args.GetNextParameter<int>();
					buff = FormatCommaNumber(buff, number, last, digits);
					break;
				}

				case SCC_DECIMAL1: {// {DECIMAL1}
					int64 number = args.GetNextParameter<int64>();
					buff = FormatCommaNumber(buff, number, last, 1);
					break;
				}

				case SCC_NUM: // {NUM}
					buff = FormatNoCommaNumber(buff, args.GetNextParameter<int64>(), last);
					break;

				case SCC_PLUS_NUM: { // {PLUS_NUM}
					int64 num = args.GetNextParameter<int64>();
					if (num > 0) {
						buff += seprintf(buff, last, "+");
					}
					buff = FormatNoCommaNumber(buff, num, last);
					break;
				}

				case SCC_ZEROFILL_NUM: { // {ZEROFILL_NUM}
					int64 num = args.GetNextParameter<int64>();
					buff = FormatZerofillNumber(buff, num, args.GetNextParameter<int>(), last);
					break;
				}

				case SCC_HEX: // {HEX}
					buff = FormatHexNumber(buff, args.GetNextParameter<uint64>(), last);
					break;

				case SCC_BYTES: // {BYTES}
					buff = FormatBytes(buff, args.GetNextParameter<int64>(), last);
					break;

				case SCC_CARGO_TINY: { // {CARGO_TINY}
					/* Tiny description of cargotypes. Layout:
					 * param 1: cargo type
					 * param 2: cargo count */
					CargoID cargo = args.GetNextParameter<CargoID>();
					if (cargo >= CargoSpec::GetArraySize()) break;

					StringID cargo_str = CargoSpec::Get(cargo)->units_volume;
					int64 amount = 0;
					switch (cargo_str) {
						case STR_TONS:
							amount = _units_weight[_settings_game.locale.units_weight].c.ToDisplay(args.GetNextParameter<int64>());
							break;

						case STR_LITERS:
							amount = _units_volume[_settings_game.locale.units_volume].c.ToDisplay(args.GetNextParameter<int64>());
							break;

						default: {
							amount = args.GetNextParameter<int64>();
							break;
						}
					}

					buff = FormatCommaNumber(buff, amount, last);
					break;
				}

				case SCC_CARGO_SHORT: { // {CARGO_SHORT}
					/* Short description of cargotypes. Layout:
					 * param 1: cargo type
					 * param 2: cargo count */
					CargoID cargo = args.GetNextParameter<CargoID>();
					if (cargo >= CargoSpec::GetArraySize()) break;

					StringID cargo_str = CargoSpec::Get(cargo)->units_volume;
					switch (cargo_str) {
						case STR_TONS: {
							assert(_settings_game.locale.units_weight < lengthof(_units_weight));
							const auto &x = _units_weight[_settings_game.locale.units_weight];
							auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
							buff = FormatString(buff, GetStringPtr(x.l), tmp_params, last);
							break;
						}

						case STR_LITERS: {
							assert(_settings_game.locale.units_volume < lengthof(_units_volume));
							const auto &x = _units_volume[_settings_game.locale.units_volume];
							auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
							buff = FormatString(buff, GetStringPtr(x.l), tmp_params, last);
							break;
						}

						default: {
							auto tmp_params = MakeParameters(args.GetNextParameter<int64>());
							buff = GetStringWithArgs(buff, cargo_str, tmp_params, last);
							break;
						}
					}
					break;
				}

				case SCC_CARGO_LONG: { // {CARGO_LONG}
					/* First parameter is cargo type, second parameter is cargo count */
					CargoID cargo = args.GetNextParameter<CargoID>();
					if (cargo != CT_INVALID && cargo >= CargoSpec::GetArraySize()) break;

					StringID cargo_str = (cargo == CT_INVALID) ? STR_QUANTITY_N_A : CargoSpec::Get(cargo)->quantifier;
					auto tmp_args = MakeParameters(args.GetNextParameter<int64>());
					buff = GetStringWithArgs(buff, cargo_str, tmp_args, last);
					break;
				}

				case SCC_CARGO_LIST: { // {CARGO_LIST}
					CargoTypes cmask = args.GetNextParameter<CargoTypes>();
					bool first = true;

					for (const auto &cs : _sorted_cargo_specs) {
						if (!HasBit(cmask, cs->Index())) continue;

						if (buff >= last - 2) break; // ',' and ' '

						if (first) {
							first = false;
						} else {
							/* Add a comma if this is not the first item */
							*buff++ = ',';
							*buff++ = ' ';
						}

						buff = GetStringWithArgs(buff, cs->name, args, last, next_substr_case_index, game_script);
					}

					/* If first is still true then no cargo is accepted */
					if (first) buff = GetStringWithArgs(buff, STR_JUST_NOTHING, args, last, next_substr_case_index, game_script);

					*buff = '\0';
					next_substr_case_index = 0;

					/* Make sure we detect any buffer overflow */
					assert(buff < last);
					break;
				}

				case SCC_CURRENCY_SHORT: // {CURRENCY_SHORT}
					buff = FormatGenericCurrency(buff, _currency, args.GetNextParameter<int64>(), true, last);
					break;

				case SCC_CURRENCY_LONG: // {CURRENCY_LONG}
					buff = FormatGenericCurrency(buff, _currency, args.GetNextParameter<int64>(), false, last);
					break;

				case SCC_DATE_TINY: // {DATE_TINY}
					buff = FormatTinyOrISODate(buff, args.GetNextParameter<Date>(), STR_FORMAT_DATE_TINY, last);
					break;

				case SCC_DATE_SHORT: // {DATE_SHORT}
					buff = FormatMonthAndYear(buff, args.GetNextParameter<Date>(), last, next_substr_case_index);
					next_substr_case_index = 0;
					break;

				case SCC_DATE_LONG: // {DATE_LONG}
					buff = FormatYmdString(buff, args.GetNextParameter<Date>(), last, next_substr_case_index);
					next_substr_case_index = 0;
					break;

				case SCC_DATE_WALLCLOCK_LONG: { // {DATE_WALLCLOCK_LONG}
					if (_settings_time.time_in_minutes) {
						buff = FormatWallClockString(buff, args.GetNextParameter<DateTicksScaled>(), last, _settings_client.gui.date_with_time, next_substr_case_index);
					} else {
						buff = FormatYmdString(buff, ScaledDateTicksToDate(args.GetNextParameter<DateTicksScaled>()), last, next_substr_case_index);
					}
					break;
				}

				case SCC_DATE_WALLCLOCK_SHORT: { // {DATE_WALLCLOCK_SHORT}
					if (_settings_time.time_in_minutes) {
						buff = FormatWallClockString(buff, args.GetNextParameter<DateTicksScaled>(), last, _settings_client.gui.date_with_time, next_substr_case_index);
					} else {
						buff = FormatYmdString(buff, ScaledDateTicksToDate(args.GetNextParameter<DateTicksScaled>()), last, next_substr_case_index);
					}
					break;
				}

				case SCC_DATE_WALLCLOCK_TINY: { // {DATE_WALLCLOCK_TINY}
					if (_settings_time.time_in_minutes) {
						buff = FormatWallClockString(buff, args.GetNextParameter<DateTicksScaled>(), last, false, next_substr_case_index);
					} else {
						buff = FormatTinyOrISODate(buff, ScaledDateTicksToDate(args.GetNextParameter<DateTicksScaled>()), STR_FORMAT_DATE_TINY, last);
					}
					break;
				}

				case SCC_DATE_WALLCLOCK_ISO: { // {DATE_WALLCLOCK_ISO}
					if (_settings_time.time_in_minutes) {
						buff = FormatWallClockString(buff, args.GetNextParameter<DateTicksScaled>(), last, false, next_substr_case_index);
					} else {
						buff = FormatTinyOrISODate(buff, ScaledDateTicksToDate(args.GetNextParameter<DateTicksScaled>()), STR_FORMAT_DATE_ISO, last);
					}
					break;
				}

				case SCC_DATE_ISO: // {DATE_ISO}
					buff = FormatTinyOrISODate(buff, args.GetNextParameter<Date>(), STR_FORMAT_DATE_ISO, last);
					break;

				case SCC_TIME_HHMM: // {TIME_HHMM}
					buff = FormatTimeHHMMString(buff, args.GetNextParameter<uint>(), last, next_substr_case_index);
					break;

				case SCC_TT_TICKS:      // {TT_TICKS}
				case SCC_TT_TICKS_LONG: // {TT_TICKS_LONG}
					if (_settings_client.gui.timetable_in_ticks) {
						auto tmp_params = MakeParameters(args.GetNextParameter<int64>());
						buff = FormatString(buff, GetStringPtr(STR_UNITS_TICKS), tmp_params, last);
					} else {
						StringID str = _settings_time.time_in_minutes ? STR_TIMETABLE_MINUTES : STR_UNITS_DAYS;
						int64 ticks = args.GetNextParameter<int64>();
						int64 ratio = DATE_UNIT_SIZE;
						int64 units = ticks / ratio;
						int64 leftover = _settings_client.gui.timetable_leftover_ticks ? ticks % ratio : 0;
						auto tmp_params = MakeParameters(units);
						buff = FormatString(buff, GetStringPtr(str), tmp_params, last);
						if (b == SCC_TT_TICKS_LONG && _settings_time.time_in_minutes && units > 59) {
							int64 hours = units / 60;
							int64 minutes = units % 60;
							auto tmp_params = MakeParameters(
								(minutes != 0) ? STR_TIMETABLE_HOURS_MINUTES : STR_TIMETABLE_HOURS,
								hours,
								minutes
							);
							buff = FormatString(buff, GetStringPtr(STR_TIMETABLE_MINUTES_SUFFIX), tmp_params, last);
						}
						if (leftover != 0) {
							auto tmp_params = MakeParameters(leftover);
							buff = FormatString(buff, GetStringPtr(STR_TIMETABLE_LEFTOVER_TICKS), tmp_params, last);
						}
					}
					break;

				case SCC_FORCE: { // {FORCE}
					assert(_settings_game.locale.units_force < lengthof(_units_force));
					const auto &x = _units_force[_settings_game.locale.units_force];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_HEIGHT: { // {HEIGHT}
					assert(_settings_game.locale.units_height < lengthof(_units_height));
					const auto &x = _units_height[_settings_game.locale.units_height];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_POWER: { // {POWER}
					assert(_settings_game.locale.units_power < lengthof(_units_power));
					const auto &x = _units_power[_settings_game.locale.units_power];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_POWER_TO_WEIGHT: { // {POWER_TO_WEIGHT}
					auto setting = _settings_game.locale.units_power * 3u + _settings_game.locale.units_weight;
					assert(setting < lengthof(_units_power_to_weight));
					const auto &x = _units_power_to_weight[setting];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_VELOCITY: { // {VELOCITY}
					int64 arg = args.GetNextParameter<int64>();
					// Unpack vehicle type from packed argument to get desired units.
					VehicleType vt = static_cast<VehicleType>(GB(arg, 56, 8));
					byte units = GetVelocityUnits(vt);
					assert(units < lengthof(_units_velocity));
					const auto &x = _units_velocity[units];
					auto tmp_params = MakeParameters(ConvertKmhishSpeedToDisplaySpeed(GB(arg, 0, 56), vt), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_VOLUME_SHORT: { // {VOLUME_SHORT}
					assert(_settings_game.locale.units_volume < lengthof(_units_volume));
					const auto &x = _units_volume[_settings_game.locale.units_volume];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_VOLUME_LONG: { // {VOLUME_LONG}
					assert(_settings_game.locale.units_volume < lengthof(_units_volume));
					const auto &x = _units_volume[_settings_game.locale.units_volume];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.l), tmp_params, last);
					break;
				}

				case SCC_WEIGHT_SHORT: { // {WEIGHT_SHORT}
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));
					const auto &x = _units_weight[_settings_game.locale.units_weight];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.s), tmp_params, last);
					break;
				}

				case SCC_WEIGHT_LONG: { // {WEIGHT_LONG}
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));
					const auto &x = _units_weight[_settings_game.locale.units_weight];
					auto tmp_params = MakeParameters(x.c.ToDisplay(args.GetNextParameter<int64>()), x.decimal_places);
					buff = FormatString(buff, GetStringPtr(x.l), tmp_params, last);
					break;
				}

				case SCC_POWER_WEIGHT_RATIO: { // {POWER_WEIGHT_RATIO}
					assert(_settings_game.locale.units_power < lengthof(_units_power));
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));

					buff = FormatUnitWeightRatio(buff, last, _units_power[_settings_game.locale.units_power], args.GetNextParameter<int64>());
					break;
				}

				case SCC_FORCE_WEIGHT_RATIO: { // {FORCE_WEIGHT_RATIO}
					assert(_settings_game.locale.units_force < lengthof(_units_force));
					assert(_settings_game.locale.units_weight < lengthof(_units_weight));

					buff = FormatUnitWeightRatio(buff, last, _units_force[_settings_game.locale.units_force], args.GetNextParameter<int64>());
					break;
				}

				case SCC_COMPANY_NAME: { // {COMPANY}
					const Company *c = Company::GetIfValid(args.GetNextParameter<CompanyID>());
					if (c == nullptr) break;

					if (!c->name.empty()) {
						auto tmp_params = MakeParameters(c->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						auto tmp_params = MakeParameters(c->name_2);
						buff = GetStringWithArgs(buff, c->name_1, tmp_params, last);
					}
					break;
				}

				case SCC_COMPANY_NUM: { // {COMPANY_NUM}
					CompanyID company = args.GetNextParameter<CompanyID>();

					/* Nothing is added for AI or inactive companies */
					if (Company::IsValidHumanID(company)) {
						auto tmp_params = MakeParameters(company + 1);
						buff = GetStringWithArgs(buff, STR_FORMAT_COMPANY_NUM, tmp_params, last);
					}
					break;
				}

				case SCC_DEPOT_NAME: { // {DEPOT}
					VehicleType vt = args.GetNextParameter<VehicleType>();
					if (vt == VEH_AIRCRAFT) {
						auto tmp_params = MakeParameters(args.GetNextParameter<StationID>());
						buff = GetStringWithArgs(buff, STR_FORMAT_DEPOT_NAME_AIRCRAFT, tmp_params, last);
						break;
					}

					const Depot *d = Depot::Get(args.GetNextParameter<DepotID>());
					if (!d->name.empty()) {
						auto tmp_params = MakeParameters(d->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						auto tmp_params = MakeParameters(d->town->index, d->town_cn + 1);
						buff = GetStringWithArgs(buff, STR_FORMAT_DEPOT_NAME_TRAIN + 2 * vt + (d->town_cn == 0 ? 0 : 1), tmp_params, last);
					}
					break;
				}

				case SCC_ENGINE_NAME: { // {ENGINE}
					int64 arg = args.GetNextParameter<int64>();
					const Engine *e = Engine::GetIfValid(static_cast<EngineID>(arg));
					if (e == nullptr) break;

					if (!e->name.empty() && e->IsEnabled()) {
						auto tmp_params = MakeParameters(e->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);

						break;
					}

					if (HasBit(e->info.callback_mask, CBM_VEHICLE_NAME)) {
						uint16 callback = GetVehicleCallback(CBID_VEHICLE_NAME, static_cast<uint32>(arg >> 32), 0, e->index, nullptr);
						/* Not calling ErrorUnknownCallbackResult due to being inside string processing. */
						if (callback != CALLBACK_FAILED && callback < 0x400) {
							const GRFFile *grffile = e->GetGRF();
							assert(grffile != nullptr);

							StartTextRefStackUsage(grffile, 6);
							ArrayStringParameters<6> tmp_params;
							buff = GetStringWithArgs(buff, GetGRFStringID(grffile->grfid, 0xD000 + callback), tmp_params, last);
							StopTextRefStackUsage();

							break;
						}
					}

					auto tmp_params = MakeParameters();
					buff = GetStringWithArgs(buff, e->info.string_id, tmp_params, last);
					break;
				}

				case SCC_GROUP_NAME: { // {GROUP}
					uint32 id = args.GetNextParameter<uint32>();
					bool recurse = _settings_client.gui.show_group_hierarchy_name && (id & GROUP_NAME_HIERARCHY);
					id &= ~GROUP_NAME_HIERARCHY;
					const Group *group = Group::GetIfValid(id);
					if (group == nullptr) break;

					auto handle_group = y_combinator([&](auto handle_group, const Group *g) -> void {
						if (recurse && g->parent != INVALID_GROUP) {
							handle_group(Group::Get(g->parent));
							auto tmp_params = MakeParameters();
							buff = GetStringWithArgs(buff, STR_HIERARCHY_SEPARATOR, tmp_params, last);
						}
						if (!g->name.empty()) {
							auto tmp_params = MakeParameters(g->name.c_str());
							buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
						} else {
							auto tmp_params = MakeParameters(g->index);

							buff = GetStringWithArgs(buff, STR_FORMAT_GROUP_NAME, tmp_params, last);
						}
					});
					handle_group(group);
					break;
				}

				case SCC_INDUSTRY_NAME: { // {INDUSTRY}
					const Industry *i = Industry::GetIfValid(args.GetNextParameter<IndustryID>());
					if (i == nullptr) break;

					static bool use_cache = true;
					if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						buff = strecpy(buff, i->GetCachedName().c_str(), last);
					} else if (_scan_for_gender_data) {
						/* Gender is defined by the industry type.
						 * STR_FORMAT_INDUSTRY_NAME may have the town first, so it would result in the gender of the town name */
						auto tmp_params = MakeParameters();
						buff = FormatString(buff, GetStringPtr(GetIndustrySpec(i->type)->name), tmp_params, last, next_substr_case_index);
					} else {
						/* First print the town name and the industry type name. */
						auto tmp_params = MakeParameters(i->town->index, GetIndustrySpec(i->type)->name);

						buff = FormatString(buff, GetStringPtr(STR_FORMAT_INDUSTRY_NAME), tmp_params, last, next_substr_case_index);
					}
					next_substr_case_index = 0;
					break;
				}

				case SCC_PRESIDENT_NAME: { // {PRESIDENT_NAME}
					const Company *c = Company::GetIfValid(args.GetNextParameter<CompanyID>());
					if (c == nullptr) break;

					if (!c->president_name.empty()) {
						auto tmp_params = MakeParameters(c->president_name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						auto tmp_params = MakeParameters(c->president_name_2);
						buff = GetStringWithArgs(buff, c->president_name_1, tmp_params, last);
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
						auto tmp_params = MakeParameters();
						buff = GetStringWithArgs(buff, STR_UNKNOWN_STATION, tmp_params, last);
						break;
					}

					static bool use_cache = true;
					if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						buff = strecpy(buff, st->GetCachedName(), last);
					} else if (!st->name.empty()) {
						auto tmp_params = MakeParameters(st->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
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
						if (st->extra_name_index != UINT16_MAX && st->extra_name_index < _extra_station_names_used) {
							string_id = _extra_station_names[st->extra_name_index].str;
						}

						auto tmp_params = MakeParameters(STR_TOWN_NAME, st->town->index, st->index);
						buff = GetStringWithArgs(buff, string_id, tmp_params, last);
					}
					break;
				}

				case SCC_TOWN_NAME: { // {TOWN}
					const Town *t = Town::GetIfValid(args.GetNextParameter<TownID>());
					if (t == nullptr) break;

					static bool use_cache = true;
					if (use_cache) { // Use cached version if first call
						AutoRestoreBackup cache_backup(use_cache, false);
						buff = strecpy(buff, t->GetCachedName(), last);
					} else if (!t->name.empty()) {
						auto tmp_params = MakeParameters(t->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						buff = GetTownName(buff, t, last);
					}
					break;
				}

				case SCC_VIEWPORT_TOWN_LABEL1:
				case SCC_VIEWPORT_TOWN_LABEL2: { // {VIEWPORT_TOWN_LABEL1..2}
					int32 t = args.GetNextParameter<int32>();
					uint64 data = args.GetNextParameter<uint64>();

					bool tiny = (b == SCC_VIEWPORT_TOWN_LABEL2);
					StringID string_id = STR_VIEWPORT_TOWN_COLOUR;
					if (!tiny) string_id += GB(data, 40, 2);
					auto tmp_params = MakeParameters(t, GB(data, 32, 8), GB(data, 0, 32));
					buff = GetStringWithArgs(buff, string_id, tmp_params, last);
					break;
				}

				case SCC_WAYPOINT_NAME: { // {WAYPOINT}
					Waypoint *wp = Waypoint::GetIfValid(args.GetNextParameter<StationID>());
					if (wp == nullptr) break;

					if (!wp->name.empty()) {
						auto tmp_params = MakeParameters(wp->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						auto tmp_params = MakeParameters(wp->town->index, wp->town_cn + 1);
						StringID string_id = ((wp->string_id == STR_SV_STNAME_BUOY) ? STR_FORMAT_BUOY_NAME : STR_FORMAT_WAYPOINT_NAME);
						if (wp->town_cn != 0) string_id++;
						buff = GetStringWithArgs(buff, string_id, tmp_params, last);
					}
					break;
				}

				case SCC_VEHICLE_NAME: { // {VEHICLE}
					uint32 id = args.GetNextParameter<uint32>();
					uint8 vehicle_names = _settings_client.gui.vehicle_names;
					if (id & VEHICLE_NAME_NO_GROUP) {
						id &= ~VEHICLE_NAME_NO_GROUP;
						/* Change format from long to traditional */
						if (vehicle_names == 2) vehicle_names = 0;
					}

					const Vehicle *v = Vehicle::GetIfValid(id);
					if (v == nullptr) break;

					if (!v->name.empty()) {
						auto tmp_params = MakeParameters(v->name.c_str());
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else if (v->group_id != DEFAULT_GROUP && vehicle_names != 0 && v->type < VEH_COMPANY_END) {
						/* The vehicle has no name, but is member of a group, so print group name */
						uint32 group_name = v->group_id;
						if (_settings_client.gui.show_vehicle_group_hierarchy_name) group_name |= GROUP_NAME_HIERARCHY;
						if (vehicle_names == 1) {
							auto tmp_params = MakeParameters(group_name, v->unitnumber);
							buff = GetStringWithArgs(buff, STR_FORMAT_GROUP_VEHICLE_NAME, tmp_params, last);
						} else {
							auto tmp_params = MakeParameters(group_name, STR_TRADITIONAL_TRAIN_NAME + v->type, v->unitnumber);
							buff = GetStringWithArgs(buff, STR_FORMAT_GROUP_VEHICLE_NAME_LONG, tmp_params, last);
						}
					} else {
						auto tmp_params = MakeParameters(v->unitnumber);

						StringID string_id;
						if (v->type < VEH_COMPANY_END) {
							string_id = ((vehicle_names == 1) ? STR_SV_TRAIN_NAME : STR_TRADITIONAL_TRAIN_NAME) + v->type;
						} else {
							string_id = STR_INVALID_VEHICLE;
						}

						buff = GetStringWithArgs(buff, string_id, tmp_params, last);
					}
					break;
				}

				case SCC_SIGN_NAME: { // {SIGN}
					const Sign *si = Sign::GetIfValid(args.GetNextParameter<SignID>());
					if (si == nullptr) break;

					if (!si->name.empty()) {
						auto tmp_params = MakeParameters(si->name);
						buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					} else {
						auto tmp_params = MakeParameters();
						buff = GetStringWithArgs(buff, STR_DEFAULT_SIGN_NAME, tmp_params, last);
					}
					break;
				}

				case SCC_TR_SLOT_NAME: { // {TRSLOT}
					const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(args.GetNextParameter<uint32>());
					if (slot == nullptr) break;
					auto tmp_params = MakeParameters(slot->name.c_str());
					buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					break;
				}

				case SCC_TR_COUNTER_NAME: { // {TRCOUNTER}
					const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(args.GetNextParameter<uint32>());
					if (ctr == nullptr) break;
					auto tmp_params = MakeParameters(ctr->name.c_str());
					buff = GetStringWithArgs(buff, STR_JUST_RAW_STRING, tmp_params, last);
					break;
				}

				case SCC_STATION_FEATURES: { // {STATIONFEATURES}
					buff = StationGetSpecialString(buff, args.GetNextParameter<StationFacility>(), last);
					break;
				}

				case SCC_COLOUR: {// {COLOUR}
					int64 tc = args.GetNextParameter<Colours>();
					if (tc >= 0 && tc < TC_END) {
						buff += Utf8Encode(buff, SCC_BLUE + tc);
					}
					break;
				}

				case SCC_CONSUME_ARG:
					// do nothing
					break;

				default:
					if (buff + Utf8CharLen(b) < last) buff += Utf8Encode(buff, b);
					break;
			}
		} catch (std::out_of_range &e) {
			DEBUG(misc, 0, "FormatString: %s", e.what());
			buff = strecpy(buff, "(invalid parameter)", last);
		}
	}
	*buff = '\0';
	return buff;
}


static char *StationGetSpecialString(char *buff, StationFacility x, const char *last)
{
	if ((x & FACIL_TRAIN)      && (buff + Utf8CharLen(SCC_TRAIN) < last)) buff += Utf8Encode(buff, SCC_TRAIN);
	if ((x & FACIL_TRUCK_STOP) && (buff + Utf8CharLen(SCC_LORRY) < last)) buff += Utf8Encode(buff, SCC_LORRY);
	if ((x & FACIL_BUS_STOP)   && (buff + Utf8CharLen(SCC_BUS)   < last)) buff += Utf8Encode(buff, SCC_BUS);
	if ((x & FACIL_DOCK)       && (buff + Utf8CharLen(SCC_SHIP)  < last)) buff += Utf8Encode(buff, SCC_SHIP);
	if ((x & FACIL_AIRPORT)    && (buff + Utf8CharLen(SCC_PLANE) < last)) buff += Utf8Encode(buff, SCC_PLANE);
	*buff = '\0';
	return buff;
}

static char *GetSpecialTownNameString(char *buff, int ind, uint32 seed, const char *last)
{
	return GenerateTownNameString(buff, last, ind, seed);
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

static char *GenAndCoName(char *buff, uint32 arg, const char *last)
{
	const char * const *base;
	uint num;

	if (_settings_game.game_creation.landscape == LT_TOYLAND) {
		base = _silly_surname_list;
		num  = lengthof(_silly_surname_list);
	} else {
		base = _surname_list;
		num  = lengthof(_surname_list);
	}

	buff = strecpy(buff, base[num * GB(arg, 16, 8) >> 8], last);
	buff = strecpy(buff, " & Co.", last);

	return buff;
}

static char *GenPresidentName(char *buff, uint32 x, const char *last)
{
	char initial[] = "?. ";
	const char * const *base;
	uint num;
	uint i;

	initial[0] = _initial_name_letters[sizeof(_initial_name_letters) * GB(x, 0, 8) >> 8];
	buff = strecpy(buff, initial, last);

	i = (sizeof(_initial_name_letters) + 35) * GB(x, 8, 8) >> 8;
	if (i < sizeof(_initial_name_letters)) {
		initial[0] = _initial_name_letters[i];
		buff = strecpy(buff, initial, last);
	}

	if (_settings_game.game_creation.landscape == LT_TOYLAND) {
		base = _silly_surname_list;
		num  = lengthof(_silly_surname_list);
	} else {
		base = _surname_list;
		num  = lengthof(_surname_list);
	}

	buff = strecpy(buff, base[num * GB(x, 16, 8) >> 8], last);

	return buff;
}

static char *GetSpecialNameString(char *buff, int ind, StringParameters &args, const char *last)
{
	switch (ind) {
		case 1: // not used
			return strecpy(buff, _silly_company_names[std::min<uint>(args.GetNextParameter<uint32>() & 0xFFFF, lengthof(_silly_company_names) - 1)], last);

		case 2: // used for Foobar & Co company names
			return GenAndCoName(buff, args.GetNextParameter<uint32>(), last);

		case 3: // President name
			return GenPresidentName(buff, args.GetNextParameter<uint32>(), last);
	}

	/* town name? */
	if (IsInsideMM(ind - 6, 0, SPECSTR_TOWNNAME_LAST - SPECSTR_TOWNNAME_START + 1)) {
		buff = GetSpecialTownNameString(buff, ind - 6, args.GetNextParameter<uint32>(), last);
		return strecpy(buff, " Transport", last);
	}

	NOT_REACHED();
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
	       StrValid(this->name,                           lastof(this->name)) &&
	       StrValid(this->own_name,                       lastof(this->own_name)) &&
	       StrValid(this->isocode,                        lastof(this->isocode)) &&
	       StrValid(this->digit_group_separator,          lastof(this->digit_group_separator)) &&
	       StrValid(this->digit_group_separator_currency, lastof(this->digit_group_separator_currency)) &&
	       StrValid(this->digit_decimal_separator,        lastof(this->digit_decimal_separator));
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
	size_t len = 0;
	std::unique_ptr<LanguagePack, LanguagePackDeleter> lang_pack(reinterpret_cast<LanguagePack *>(ReadFileToMem(lang->file, len, 1U << 20).release()));
	if (!lang_pack) return false;

	/* End of read data (+ terminating zero added in ReadFileToMem()) */
	const char *end = (char *)lang_pack.get() + len + 1;

	/* We need at least one byte of lang_pack->data */
	if (end <= lang_pack->data || !lang_pack->IsValid()) {
		return false;
	}

#if TTD_ENDIAN == TTD_BIG_ENDIAN
	for (uint i = 0; i < TEXT_TAB_END; i++) {
		lang_pack->offsets[i] = ReadLE16Aligned(&lang_pack->offsets[i]);
	}
#endif /* TTD_ENDIAN == TTD_BIG_ENDIAN */

	std::array<uint, TEXT_TAB_END> tab_start, tab_num;

	uint count = 0;
	for (uint i = 0; i < TEXT_TAB_END; i++) {
		uint16 num = lang_pack->offsets[i];
		if (num > TAB_SIZE) return false;

		tab_start[i] = count;
		tab_num[i] = num;
		count += num;
	}

	/* Allocate offsets */
	std::vector<char *> offs(count);

	/* Fill offsets */
	char *s = lang_pack->data;
	len = (byte)*s++;
	for (uint i = 0; i < count; i++) {
		if (s + len >= end) return false;

		if (len >= 0xC0) {
			len = ((len & 0x3F) << 8) + (byte)*s++;
			if (s + len >= end) return false;
		}
		offs[i] = s;
		s += len;
		len = (byte)*s;
		*s++ = '\0'; // zero terminate the string
	}

	_langpack.langpack = std::move(lang_pack);
	_langpack.offsets = std::move(offs);
	_langpack.langtab_num = tab_num;
	_langpack.langtab_start = tab_start;

	_current_language = lang;
	const TextDirection old_text_dir = _current_text_dir;
	_current_text_dir = (TextDirection)_current_language->text_dir;
	const char *c_file = strrchr(_current_language->file, PATHSEPCHAR) + 1;
	_config_language_file = c_file;
	SetCurrentGrfLangID(_current_language->newgrflangid);

#ifdef _WIN32
	extern void Win32SetCurrentLocaleName(std::string iso_code);
	Win32SetCurrentLocaleName(_current_language->isocode);
#endif

#ifdef WITH_COCOA
	extern void MacOSSetCurrentLocaleName(const char *iso_code);
	MacOSSetCurrentLocaleName(_current_language->isocode);
#endif

#ifdef WITH_ICU_I18N
	/* Create a collator instance for our current locale. */
	UErrorCode status = U_ZERO_ERROR;
	_current_collator.reset(icu::Collator::createInstance(icu::Locale(_current_language->isocode), status));
	/* Sort number substrings by their numerical value. */
	if (_current_collator) _current_collator->setAttribute(UCOL_NUMERIC_COLLATION, UCOL_ON, status);
	/* Avoid using the collator if it is not correctly set. */
	if (U_FAILURE(status)) {
		_current_collator.reset();
	}
#endif /* WITH_ICU_I18N */

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
const LanguageMetadata *GetLanguage(byte newgrflangid)
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
	FILE *f = fopen(file, "rb");
	if (f == nullptr) return false;

	size_t read = fread(hdr, sizeof(*hdr), 1, f);
	fclose(f);

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
	DIR *dir = ttd_opendir(path);
	if (dir != nullptr) {
		struct dirent *dirent;
		while ((dirent = readdir(dir)) != nullptr) {
			std::string d_name = FS2OTTD(dirent->d_name);
			const char *extension = strrchr(d_name.c_str(), '.');

			/* Not a language file */
			if (extension == nullptr || strcmp(extension, ".lng") != 0) continue;

			LanguageMetadata lmd;
			seprintf(lmd.file, lastof(lmd.file), "%s%s", path, d_name.c_str());

			/* Check whether the file is of the correct version */
			if (!GetLanguageFileHeader(lmd.file, &lmd)) {
				DEBUG(misc, 3, "%s is not a valid language file", lmd.file);
			} else if (GetLanguage(lmd.newgrflangid) != nullptr) {
				DEBUG(misc, 3, "%s's language ID is already known", lmd.file);
			} else {
				_languages.push_back(lmd);
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
	if (_languages.empty()) usererror("No available language packs (invalid versions?)");

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
		const char *lang_file = strrchr(lng.file, PATHSEPCHAR) + 1;
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

	if (!ReadLanguagePack(chosen_language)) usererror("Can't read language pack '%s'", chosen_language->file);
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
	const Sprite *question_mark[FS_END];

	for (FontSize size = this->Monospace() ? FS_MONO : FS_BEGIN; size < (this->Monospace() ? FS_END : FS_MONO); size++) {
		question_mark[size] = GetGlyph(size, '?');
	}

	this->Reset();
	for (auto text = this->NextString(); text.has_value(); text = this->NextString()) {
		auto src = text->cbegin();

		FontSize size = this->DefaultSize();
		while (src != text->cend()) {
			WChar c = Utf8Consume(src);

			if (c >= SCC_FIRST_FONT && c <= SCC_LAST_FONT) {
				size = (FontSize)(c - SCC_FIRST_FONT);
			} else if (!IsInsideMM(c, SCC_SPRITE_START, SCC_SPRITE_END) && IsPrintable(c) && !IsTextDirectionChar(c) && c != '?' && GetGlyph(size, c) == question_mark[size]) {
				/* The character is printable, but not in the normal font. This is the case we were testing for. */
				std::string size_name;

				switch (size) {
					case FS_NORMAL: size_name = "medium"; break;
					case FS_SMALL: size_name = "small"; break;
					case FS_LARGE: size_name = "large"; break;
					case FS_MONO: size_name = "mono"; break;
					default: NOT_REACHED();
				}

				DEBUG(fontcache, 0, "Font is missing glyphs to display char 0x%X in %s font size", c, size_name.c_str());
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

		const char *ret = _langpack.offsets[_langpack.langtab_start[this->i] + this->j];

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

		bad_font = !SetFallbackFont(&_fcsettings, _langpack.langpack->isocode, _langpack.langpack->winlangid, searcher);

		_fcsettings = backup;

		if (!bad_font && any_font_configured) {
			/* If the user configured a bad font, and we found a better one,
			 * show that we loaded the better font instead of the configured one.
			 * The colour 'character' might change in the
			 * future, so for safety we just Utf8 Encode it into the string,
			 * which takes exactly three characters, so it replaces the "XXX"
			 * with the colour marker. */
			static std::string err_str("XXXThe current font is missing some of the characters used in the texts for this language. Using system fallback font instead.");
			Utf8Encode(err_str.data(), SCC_YELLOW);
			SetDParamStr(0, err_str);
			ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_WARNING);
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
		static std::string err_str("XXXThe current font is missing some of the characters used in the texts for this language. Read the readme to see how to solve this.");
		Utf8Encode(err_str.data(), SCC_YELLOW);
		SetDParamStr(0, err_str);
		ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_WARNING);

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
		SetDParamStr(0, err_str);
		ShowErrorMessage(STR_JUST_RAW_STRING, INVALID_STRING_ID, WL_ERROR);
	}
#endif /* !(WITH_ICU_I18N && WITH_HARFBUZZ) && !WITH_UNISCRIBE && !WITH_COCOA */
}
