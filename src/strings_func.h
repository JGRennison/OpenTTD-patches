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

/**
 * Prepare the string parameters for the next formatting run, resetting the type information.
 * This is only necessary if parameters are reused for multiple format runs.
 */
static inline void PrepareArgsForNextRun(std::span<StringParameter> args)
{
	for (auto &param : args) param.type = 0;
}

[[nodiscard]] std::string GetStringWithArgs(StringID string, std::span<StringParameter> args);
[[nodiscard]] std::string GetString(StringID string);
[[nodiscard]] std::string_view GetStringPtr(StringID string);
void AppendStringWithArgsInPlace(struct format_target &result, StringID string, std::span<StringParameter> args);
void AppendStringWithArgsInPlace(std::string &result, StringID string, std::span<StringParameter> args);
uint32_t GetStringGRFID(StringID string);

uint ConvertKmhishSpeedToDisplaySpeed(uint speed, VehicleType type);
uint ConvertDisplaySpeedToKmhishSpeed(uint speed, VehicleType type);

StringID GetVelocityUnitName(VehicleType type);

/**
 * Pack velocity and vehicle type for use with SCC_VELOCITY string parameter.
 * @param speed Display speed for parameter.
 * @param type Type of vehicle for parameter.
 * @return Bit-packed velocity and vehicle type, for use with string parameters.
 */
inline int64_t PackVelocity(uint speed, VehicleType type)
{
	/* Vehicle type is a byte, so packed into the top 8 bits of the 64-bit
	 * parameter, although only values from 0-3 are relevant. */
	return speed | (static_cast<uint64_t>(type) << 56);
}

char32_t GetDecimalSeparatorChar();

uint64_t GetParamMaxValue(uint64_t max_value, uint min_count = 0, FontSize size = FS_NORMAL);
uint64_t GetParamMaxDigits(uint count, FontSize size = FS_NORMAL);

template <typename T, std::enable_if_t<StringParameterAsBase<T>, int> = 0>
uint64_t GetParamMaxValue(T max_value, uint min_count = 0, FontSize size = FS_NORMAL)
{
	return GetParamMaxValue(max_value.base(), min_count, size);
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
[[nodiscard]] auto MakeParameters(Args &&... args)
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
[[nodiscard]] std::string GetString(StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return GetStringWithArgs(string, params);
}

[[nodiscard]] EncodedString GetEncodedString(StringID str);
[[nodiscard]] EncodedString GetEncodedStringWithArgs(StringID str, std::span<const StringParameter> params);

/**
 * Encode a string with no parameters into an encoded string, if the string id is valid.
 * @note the return encoded string will be empty if the string id is not valid.
 * @param str String to encode.
 * @returns an EncodedString.
 */
static inline EncodedString GetEncodedStringIfValid(StringID str)
{
	if (str == INVALID_STRING_ID) return {};
	return GetEncodedString(str);
}

/**
 * Get an encoded string with parameters.
 * @param string String ID to encode.
 * @param args The parameters to set.
 * @return The encoded string.
 */
template <typename... Args>
[[nodiscard]] EncodedString GetEncodedString(StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return GetEncodedStringWithArgs(string, params);
}

/**
 * Resolve the given StringID and append in place into an existing format_buffer with most special stringcodes replaced by the string parameters.
 * @param result The format_target to append to.
 * @param string String ID to format.
 * @param args The parameters to set.
 */
template <typename... Args>
void AppendStringInPlace(struct format_target &result, StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return AppendStringWithArgsInPlace(result, string, params);
}

/**
 * Resolve the given StringID and append in place into an existing format_buffer with most special stringcodes replaced by the string parameters.
 * @param result The std::string to append to.
 * @param string String ID to format.
 * @param args The parameters to set.
 */
template <typename... Args>
void AppendStringInPlace(std::string &result, StringID string, Args &&... args)
{
	auto params = MakeParameters(std::forward<Args>(args)...);
	return AppendStringWithArgsInPlace(result, string, params);
}

/**
 * Resolve the given StringID into an existing format_buffer or std::string (replacing the existing contents), with most special stringcodes replaced by the string parameters.
 * @param buffer The format_buffer or std::string to write to.
 * @param string String ID to format.
 * @param args The parameters to set.
 * @return string_view of the output buffer
 */
template <typename T, typename... Args>
std::string_view GetStringInPlace(T &buffer, StringID string, Args &&... args)
{
	buffer.clear();
	auto params = MakeParameters(std::forward<Args>(args)...);
	AppendStringWithArgsInPlace(buffer, string, params);
	return buffer;
}

/**
 * Resolve the given StringID into an existing format_buffer or std::string (replacing the existing contents), with most special stringcodes replaced by the string parameters.
 * @param buffer The format_buffer or std::string to write to.
 * @param string String ID to format.
 * @param args Span of arguments for the string.
 * @return string_view of the output buffer
 */
template <typename T, typename... Args>
std::string_view GetStringWithArgsInPlace(T &buffer, StringID string, std::span<StringParameter> args)
{
	buffer.clear();
	AppendStringWithArgsInPlace(buffer, string, args);
	return buffer;
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
