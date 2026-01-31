/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_type.h Types related to strings. */

#ifndef STRINGS_TYPE_H
#define STRINGS_TYPE_H

#include "string_type.h"
#include "core/strong_typedef_type.hpp"
#include <optional>
#include <variant>

/**
 * Numeric value that represents a string, independent of the selected language.
 */
typedef uint32_t StringID;
static const StringID STR_NULL          = 0x0;
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

template <typename T>
concept StringParameterAsBase = T::string_parameter_as_base || false;

/** This is a separate type instead of just string_view to ensure that it cannot be created by accident. */
struct StringParameterDataStringView {
	std::string_view view;

	explicit StringParameterDataStringView(std::string_view view) : view(view) {}
};

using StringParameterData = std::variant<std::monostate, uint64_t, std::string, StringParameterDataStringView>;

/** The data required to format and validate a single parameter of a string. */
struct StringParameter {
	StringParameterData data; ///< The data of the parameter.
	char32_t type = 0; ///< The #StringControlCode to interpret this data with when it's the first parameter, otherwise '\0'.

private:
	template <bool REF>
	struct Helper {
		static inline StringParameterData Init(const std::monostate &v)
		{
			return v;
		}

		static inline StringParameterData Init(uint64_t v)
		{
			return v;
		}

		static inline StringParameterData Init(const char *str)
		{
			if constexpr (REF) {
				return StringParameterDataStringView(std::string_view{str});
			} else {
				return std::string{str};
			}
		}

		static inline StringParameterData Init(std::string_view str)
		{
			if constexpr (REF) {
				return StringParameterDataStringView(str);
			} else {
				return std::string{str};
			}
		}

		static inline StringParameterData Init(std::string &&str)
		{
			return std::move(str);
		}

		template <typename T, std::enable_if_t<StringParameterAsBase<T>, int> = 0>
		static inline StringParameterData Init(const T &v)
		{
			return Init(v.base());
		}
	};

public:
	struct ReferenceCaptureTag {};

	StringParameter() = default;
	inline StringParameter(StringParameterData &&data) : data(std::move(data)), type(0) {}
	inline StringParameter(const StringParameterData &data) : data(data), type(0) {}

	template <typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, StringParameter>, int> = 0>
	inline StringParameter(T &&v) : data(Helper<false>::Init(std::forward<T>(v))), type(0) {}

	inline StringParameter(ReferenceCaptureTag, StringParameterData &&data) : data(std::move(data)), type(0) {}
	inline StringParameter(ReferenceCaptureTag, const StringParameterData &data) : data(data), type(0) {}

	template <typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, StringParameter>, int> = 0>
	inline StringParameter(ReferenceCaptureTag, T &&v) : data(Helper<true>::Init(std::forward<T>(v))), type(0) {}

	inline StringParameter(ReferenceCaptureTag, const StringParameter &param) : data(param.data), type(param.type) {}
	inline StringParameter(ReferenceCaptureTag, StringParameter &&param) : data(std::move(param.data)), type(param.type) {}
};

/**
 * Container for an encoded string, created by GetEncodedString.
 */
class EncodedString {
public:
	EncodedString() = default;

	auto operator<=>(const EncodedString &) const = default;

	std::string GetDecodedString() const;
	EncodedString ReplaceParam(size_t param, StringParameter &&value) const;
	void AppendDecodedStringInPlace(struct format_target &result) const;

	std::string_view GetDecodedStringView(struct format_buffer_base &buffer) const;

	inline void clear() { this->string.clear(); }
	inline bool empty() const { return this->string.empty(); }

	template <typename T>
	void Serialise(T &&buffer) const { buffer.Send_string(this->string); }

	template <typename T>
	bool Deserialise(T &buffer, StringValidationSettings default_string_validation);

	inline void Sanitise(StringValidationSettings default_string_validation);

private:
	std::string string; ///< The encoded string.

	/* An EncodedString can only be created by GetEncodedStringWithArgs(). */
	explicit EncodedString(std::string &&string) : string(std::move(string)) {}

	friend EncodedString GetEncodedStringWithArgs(StringID str, std::span<const StringParameter> params);
	friend EncodedString GetEncodedRawString(std::string_view str);

	friend class ScriptText;
};
static_assert(sizeof(EncodedString) == sizeof(std::string)); // EncodedString is saved/loaded directly, std::string must be at offset 0.

#endif /* STRINGS_TYPE_H */
