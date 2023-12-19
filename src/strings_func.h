/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_func.h Functions related to OTTD's strings. */

#ifndef STRINGS_FUNC_H
#define STRINGS_FUNC_H

#include "strings_type.h"
#include "strings_internal.h"
#include "string_type.h"
#include "gfx_type.h"
#include "core/bitmath_func.hpp"
#include "core/span_type.hpp"
#include "vehicle_type.h"
#include <array>
#include <optional>
#include <vector>

extern ArrayStringParameters<20> _global_string_params;

/**
 * Extract the StringTab from a StringID.
 * @param str String identifier
 * @return StringTab from \a str
 */
static inline StringTab GetStringTab(StringID str)
{
	StringTab result = (StringTab)(str >> TAB_SIZE_BITS);
	if (result >= TEXT_TAB_NEWGRF_START) return TEXT_TAB_NEWGRF_START;
	if (result >= TEXT_TAB_GAMESCRIPT_START) return TEXT_TAB_GAMESCRIPT_START;
	return result;
}

/**
 * Extract the StringIndex from a StringID.
 * @param str String identifier
 * @return StringIndex from \a str
 */
static inline uint GetStringIndex(StringID str)
{
	return str - (GetStringTab(str) << TAB_SIZE_BITS);
}

/**
 * Create a StringID
 * @param tab StringTab
 * @param index StringIndex
 * @return StringID composed from \a tab and \a index
 */
static inline StringID MakeStringID(StringTab tab, uint index)
{
	if (tab == TEXT_TAB_NEWGRF_START) {
		assert(index < TAB_SIZE_NEWGRF);
	} else if (tab == TEXT_TAB_GAMESCRIPT_START) {
		assert(index < TAB_SIZE_GAMESCRIPT);
	} else {
		assert(tab < TEXT_TAB_END);
		assert(index < TAB_SIZE);
	}
	return (tab << TAB_SIZE_BITS) + index;
}

char *GetString(char *buffr, StringID string, const char *last);
std::string GetString(StringID string);
char *GetStringWithArgs(char *buffr, StringID string, StringParameters &args, const char *last, uint case_index = 0, bool game_script = false);
const char *GetStringPtr(StringID string);
uint32 GetStringGRFID(StringID string);

uint ConvertKmhishSpeedToDisplaySpeed(uint speed, VehicleType type);
uint ConvertDisplaySpeedToKmhishSpeed(uint speed, VehicleType type);

/**
 * Pack velocity and vehicle type for use with SCC_VELOCITY string parameter.
 * @param speed Display speed for parameter.
 * @param type Type of vehicle for parameter.
 * @return Bit-packed velocity and vehicle type, for use with SetDParam().
 */
static inline int64 PackVelocity(uint speed, VehicleType type)
{
	/* Vehicle type is a byte, so packed into the top 8 bits of the 64-bit
	 * parameter, although only values from 0-3 are relevant. */
	return speed | (static_cast<uint64>(type) << 56);
}

WChar GetDecimalSeparatorChar();

/**
 * Set a string parameter \a v at index \a n in the global string parameter array.
 * @param n Index of the string parameter.
 * @param v Value of the string parameter.
 */
template <typename T>
static inline void SetDParam(size_t n, T &&v) {
	_global_string_params.SetParam(n, std::forward<T>(v));
}

void SetDParamMaxValue(size_t n, uint64 max_value, uint min_count = 0, FontSize size = FS_NORMAL);
void SetDParamMaxDigits(size_t n, uint count, FontSize size = FS_NORMAL);

void SetDParamStr(size_t n, const char *str);
void SetDParamStr(size_t n, std::string str);

void CopyInDParam(const span<const StringParameterBackup> backup, uint offset = 0);
void CopyOutDParam(std::vector<StringParameterBackup> &backup, size_t num);
bool HaveDParamChanged(const std::vector<StringParameterBackup> &backup);

/**
 * Get the current string parameter at index \a n from the global string parameter array.
 * @param n Index of the string parameter.
 * @return Value of the requested string parameter.
 */
static inline uint64 GetDParam(size_t n)
{
	return _global_string_params.GetParam(n);
}

extern TextDirection _current_text_dir; ///< Text direction of the currently selected language

void InitializeLanguagePacks();
const char *GetCurrentLanguageIsoCode();

/**
 * A searcher for missing glyphs.
 */
class MissingGlyphSearcher {
public:
	/** Make sure everything gets destructed right. */
	virtual ~MissingGlyphSearcher() = default;

	/**
	 * Get the next string to search through.
	 * @return The next string or nullopt if there is none.
	 */
	virtual std::optional<std::string_view> NextString() = 0;

	/**
	 * Get the default (font) size of the string.
	 * @return The font size.
	 */
	virtual FontSize DefaultSize() = 0;

	/**
	 * Reset the search, i.e. begin from the beginning again.
	 */
	virtual void Reset() = 0;

	/**
	 * Whether to search for a monospace font or not.
	 * @return True if searching for monospace.
	 */
	virtual bool Monospace() = 0;

	/**
	 * Set the right font names.
	 * @param settings  The settings to modify.
	 * @param font_name The new font name.
	 * @param os_data Opaque pointer to OS-specific data.
	 */
	virtual void SetFontNames(struct FontCacheSettings *settings, const char *font_name, const void *os_data = nullptr) = 0;

	bool FindMissingGlyphs();
};

void CheckForMissingGlyphs(bool base_font = true, MissingGlyphSearcher *search = nullptr);

#endif /* STRINGS_FUNC_H */
