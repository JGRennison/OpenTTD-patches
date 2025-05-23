/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_text_type.h Header of Action 04 "universal holder" structure */

#ifndef NEWGRF_TEXT_TYPE_H
#define NEWGRF_TEXT_TYPE_H

#include "core/strong_typedef_type.hpp"

#include <utility>
#include <vector>

/** Type for GRF-internal string IDs. */
struct GRFStringIDTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare, StrongType::Integer> {};
using GRFStringID = StrongType::Typedef<GRFStringIDTag>;

static constexpr GRFStringID GRFSTR_MISC_GRF_TEXT{0xD000}; ///< Miscellaneous GRF text range.

/** This character (thorn) indicates a unicode string to NFO. */
static const char32_t NFO_UTF8_IDENTIFIER = 0x00DE;

/** A GRF text with associated language ID. */
struct GRFText {
	uint8_t langid;      ///< The language associated with this GRFText.
	std::string text; ///< The actual (translated) text.
};

/** A GRF text with a list of translations. */
using GRFTextList = std::vector<GRFText>;
/** Reference counted wrapper around a GRFText pointer. */
using GRFTextWrapper = std::shared_ptr<GRFTextList>;

/** Mapping of language data between a NewGRF and OpenTTD. */
struct LanguageMap {
	/** Mapping between NewGRF and OpenTTD IDs. */
	struct Mapping {
		uint8_t newgrf_id;  ///< NewGRF's internal ID for a case/gender.
		uint8_t openttd_id; ///< OpenTTD's internal ID for a case/gender.
	};

	/* We need a vector and can't use SmallMap due to the fact that for "setting" a
	 * gender of a string or requesting a case for a substring we want to map from
	 * the NewGRF's internal ID to OpenTTD's ID whereas for the choice lists we map
	 * the genders/cases/plural OpenTTD IDs to the NewGRF's internal IDs. In this
	 * case a NewGRF developer/translator might want a different translation for
	 * both cases. Thus we are basically implementing a multi-map. */
	std::vector<Mapping> gender_map; ///< Mapping of NewGRF and OpenTTD IDs for genders.
	std::vector<Mapping> case_map;   ///< Mapping of NewGRF and OpenTTD IDs for cases.
	int plural_form;                 ///< The plural form used for this language.

	int GetMapping(int newgrf_id, bool gender) const;
	int GetReverseMapping(int openttd_id, bool gender) const;
	static const LanguageMap *GetLanguageMap(uint32_t grfid, uint8_t language_id);
};

#endif /* NEWGRF_TEXT_TYPE_H */
