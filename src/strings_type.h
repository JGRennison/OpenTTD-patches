/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_type.h Types related to strings. */

#ifndef STRINGS_TYPE_H
#define STRINGS_TYPE_H

#include "core/strong_typedef_type.hpp"
#include <optional>

/**
 * Numeric value that represents a string, independent of the selected language.
 */
typedef uint32_t StringID;
static const StringID INVALID_STRING_ID = 0xFFFF; ///< Constant representing an invalid string (16bit in case it is used in savegames)
static const int MAX_CHAR_LENGTH        = 4;      ///< Max. length of UTF-8 encoded unicode character
static const uint MAX_LANG              = 0x7F;   ///< Maximum number of languages supported by the game, and the NewGRF specs

/** Directions a text can go to */
enum TextDirection : uint8_t {
	TD_LTR, ///< Text is written left-to-right by default
	TD_RTL, ///< Text is written right-to-left by default
};

/** StringTabs to group StringIDs */
enum StringTab : uint8_t {
	/* Tabs 0..1 for regular strings */
	TEXT_TAB_TOWN             =  4,
	TEXT_TAB_INDUSTRY         =  9,
	TEXT_TAB_STATION          = 12,
	TEXT_TAB_SPECIAL          = 14,
	TEXT_TAB_OLD_CUSTOM       = 15,
	TEXT_TAB_VEHICLE          = 16,
	/* Tab 17 for regular strings */
	TEXT_TAB_OLD_NEWGRF       = 26,
	TEXT_TAB_END              = 32, ///< End of language files.
	TEXT_TAB_GAMESCRIPT_START = 32, ///< Start of GameScript supplied strings.
	TEXT_TAB_NEWGRF_START     = 64, ///< Start of NewGRF supplied strings.
};

/** The index/offset of a string within a #StringTab. */
struct StringIndexInTabTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare, StrongType::Integer> {};
using StringIndexInTab = StrongType::Typedef<StringIndexInTabTag>;

/** Number of bits for the StringIndex within a StringTab */
static const uint TAB_SIZE_BITS       = 11;
/** Number of strings per StringTab */
static const uint TAB_SIZE            = 1 << TAB_SIZE_BITS;

/** Number of strings for GameScripts */
static const uint TAB_SIZE_GAMESCRIPT = TAB_SIZE * 32;

/** Number of strings for NewGRFs */
static const uint TAB_SIZE_NEWGRF     = TAB_SIZE * 256;

extern std::string _temp_special_strings[16];

/** The number of builtin generators for town names. */
static constexpr uint32_t BUILTIN_TOWNNAME_GENERATOR_COUNT = 21;

/** Special strings for town names. The town name is generated dynamically on request. */
static constexpr StringID SPECSTR_TOWNNAME_START = 0x20C0;
static constexpr StringID SPECSTR_TOWNNAME_END = SPECSTR_TOWNNAME_START + BUILTIN_TOWNNAME_GENERATOR_COUNT;

/** Special strings for company names on the form "TownName transport". */
static constexpr StringID SPECSTR_COMPANY_NAME_START = 0x70EA;
static constexpr StringID SPECSTR_COMPANY_NAME_END = SPECSTR_COMPANY_NAME_START + BUILTIN_TOWNNAME_GENERATOR_COUNT;

static constexpr StringID SPECSTR_SILLY_NAME = 0x70E5; ///< Special string for silly company names.
static constexpr StringID SPECSTR_ANDCO_NAME = 0x70E6; ///< Special string for Surname & Co company names.
static constexpr StringID SPECSTR_PRESIDENT_NAME = 0x70E7; ///< Special string for the president's name.

static constexpr StringID SPECSTR_TEMP_START = 0x7000; ///< First string ID for _temp_special_strings

/** Data that is to be stored when backing up StringParameters. */
struct StringParameterBackup {
	uint64_t data; ///< The data field; valid *when* string has no value.
	std::optional<std::string> string; ///< The string value.

	/**
	 * Assign the numeric data with the given value, while clearing the stored string.
	 * @param data The new value of the data field.
	 * @return This object.
	 */
	StringParameterBackup &operator=(uint64_t data)
	{
		this->string.reset();
		this->data = data;
		return *this;
	}

	/**
	 * Assign a copy of the given string to the string field, while clearing the data field.
	 * @param string The new value of the string.
	 * @return This object.
	 */
	StringParameterBackup &operator=(const std::string_view string)
	{
		this->data = 0;
		this->string.emplace(string);
		return *this;
	}
};

#endif /* STRINGS_TYPE_H */
