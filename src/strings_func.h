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
#include "string_type.h"
#include "gfx_type.h"
#include "core/bitmath_func.hpp"
#include "vehicle_type.h"
#include <array>
#include <optional>
#include <vector>

/**
 * Extract the StringTab from a StringID.
 * @param str String identifier
 * @return StringTab from \a str
 */
inline StringTab GetStringTab(StringID str)
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
inline StringIndexInTab GetStringIndex(StringID str)
{
	return StringIndexInTab{str - (GetStringTab(str) << TAB_SIZE_BITS)};
}

/**
 * Create a StringID
 * @param tab StringTab
 * @param index Index of the string within the given tab.
 * @return StringID composed from \a tab and \a index
 */
inline StringID MakeStringID(StringTab tab, StringIndexInTab index)
{
	if (tab == TEXT_TAB_NEWGRF_START) {
		assert(index < TAB_SIZE_NEWGRF);
	} else if (tab == TEXT_TAB_GAMESCRIPT_START) {
		assert(index < TAB_SIZE_GAMESCRIPT);
	} else {
		assert(tab < TEXT_TAB_END);
		assert(index < TAB_SIZE);
	}
	return (tab << TAB_SIZE_BITS) + index.base();
}

std::string GetStringWithArgs(StringID string, std::span<StringParameter> args);
std::string GetString(StringID string);
const char *GetStringPtr(StringID string);
void AppendStringInPlace(std::string &result, StringID string);
void AppendStringInPlace(struct format_buffer &result, StringID string);
void AppendStringInPlaceWithArgs(struct format_buffer &result, StringID string, std::span<StringParameter> args);
uint32_t GetStringGRFID(StringID string);

uint ConvertKmhishSpeedToDisplaySpeed(uint speed, VehicleType type);
uint ConvertDisplaySpeedToKmhishSpeed(uint speed, VehicleType type);

StringID GetVelocityUnitName(VehicleType type);

/**
 * Pack velocity and vehicle type for use with SCC_VELOCITY string parameter.
 * @param speed Display speed for parameter.
 * @param type Type of vehicle for parameter.
 * @return Bit-packed velocity and vehicle type, for use with SetDParam().
 */
inline int64_t PackVelocity(uint speed, VehicleType type)
{
	/* Vehicle type is a byte, so packed into the top 8 bits of the 64-bit
	 * parameter, although only values from 0-3 are relevant. */
	return speed | (static_cast<uint64_t>(type) << 56);
}

char32_t GetDecimalSeparatorChar();

/**
 * Set a string parameter \a v at index \a n in the global string parameter array.
 * @param n Index of the string parameter.
 * @param v Value of the string parameter.
 */
template <typename T>
inline void SetDParam(size_t n, T &&v) {
	extern std::array<StringParameter, 20> _global_string_params_data;
	assert(n < _global_string_params_data.size());
	_global_string_params_data[n] = StringParameter{std::forward<T>(v)};
}

void SetDParamMaxValue(size_t n, uint64_t max_value, uint min_count = 0, FontSize size = FS_NORMAL);
void SetDParamMaxDigits(size_t n, uint count, FontSize size = FS_NORMAL);

template <typename T, std::enable_if_t<StringParameterAsBase<T>, int> = 0>
void SetDParamMaxValue(size_t n, T max_value, uint min_count = 0, FontSize size = FS_NORMAL)
{
	SetDParamMaxValue(n, max_value.base(), min_count, size);
}

void SetDParamStr(size_t n, const char *str);
void SetDParamStr(size_t n, std::string str);

inline void SetDParamStr(size_t n, std::string_view str)
{
	SetDParamStr(n, std::string{str});
}

void CopyInDParam(const std::span<const StringParameterData> backup, uint offset = 0);
void CopyOutDParam(std::vector<StringParameterData> &backup, size_t num);

/**
 * Get the current string parameter at index \a n from the global string parameter array.
 * @param n Index of the string parameter.
 * @return Value of the requested string parameter.
 */
inline uint64_t GetDParam(size_t n)
{
	extern std::array<StringParameter, 20> _global_string_params_data;
	assert(n < _global_string_params_data.size());
	return std::get<uint64_t>(_global_string_params_data[n].data);
}

extern TextDirection _current_text_dir; ///< Text direction of the currently selected language

void InitializeLanguagePacks();
const char *GetCurrentLanguageIsoCode();
std::string_view GetListSeparator();

/**
 * Helper to create the StringParameters with its own buffer with the given
 * parameter values.
 * @param args The parameters to set for the to be created StringParameters.
 * @return The constructed StringParameters.
 */
template <typename... Args>
auto MakeParameters(Args &&... args)
{
	return std::array<StringParameter, sizeof...(args)>({StringParameter{std::forward<Args>(args)}...});
}

/**
 * Get a parsed string with most special stringcodes replaced by the string parameters.
 * @param string String ID to format.
 * @param args The parameters to set.
 * @return The parsed string.
 */
template <typename... Args>
std::string GetString(StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return GetStringWithArgs(string, params);
}

/**
 * Resolve the given StringID and append in place into an existing format_buffer with most special stringcodes replaced by the string parameters.
 * @param result The format_buffer to append to.
 * @param string String ID to format.
 * @param args The parameters to set.
 */
template <typename... Args>
void AppendStringInPlace(struct format_buffer &result, StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return AppendStringInPlaceWithArgs(result, string, params);
}

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
