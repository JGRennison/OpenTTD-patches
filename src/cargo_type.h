/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargo_type.h Types related to cargoes... */

#ifndef CARGO_TYPE_H
#define CARGO_TYPE_H

#include "core/enum_type.hpp"
#include "core/strong_typedef_type.hpp"

/** Globally unique label of a cargo type. */
struct CargoLabelTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare> {};
using CargoLabel = StrongType::Typedef<CargoLabelTag>;

#include <algorithm>
#include <array>

/**
 * Cargo slots to indicate a cargo type within a game.
 */
using CargoType = uint8_t;

/**
 * Available types of cargo
 * Labels may be re-used between different climates.
 */

/* Temperate */
static constexpr CargoLabel CT_PASSENGERS{'PASS'};
static constexpr CargoLabel CT_COAL{'COAL'};
static constexpr CargoLabel CT_MAIL{'MAIL'};
static constexpr CargoLabel CT_OIL{'OIL_'};
static constexpr CargoLabel CT_LIVESTOCK{'LVST'};
static constexpr CargoLabel CT_GOODS{'GOOD'};
static constexpr CargoLabel CT_GRAIN{'GRAI'};
static constexpr CargoLabel CT_WOOD{'WOOD'};
static constexpr CargoLabel CT_IRON_ORE{'IORE'};
static constexpr CargoLabel CT_STEEL{'STEL'};
static constexpr CargoLabel CT_VALUABLES{'VALU'};

/* Arctic */
static constexpr CargoLabel CT_WHEAT{'WHEA'};
static constexpr CargoLabel CT_PAPER{'PAPR'};
static constexpr CargoLabel CT_GOLD{'GOLD'};
static constexpr CargoLabel CT_FOOD{'FOOD'};

/* Tropic */
static constexpr CargoLabel CT_RUBBER{'RUBR'};
static constexpr CargoLabel CT_FRUIT{'FRUT'};
static constexpr CargoLabel CT_MAIZE{'MAIZ'};
static constexpr CargoLabel CT_COPPER_ORE{'CORE'};
static constexpr CargoLabel CT_WATER{'WATR'};
static constexpr CargoLabel CT_DIAMONDS{'DIAM'};

/* Toyland */
static constexpr CargoLabel CT_SUGAR{'SUGR'};
static constexpr CargoLabel CT_TOYS{'TOYS'};
static constexpr CargoLabel CT_BATTERIES{'BATT'};
static constexpr CargoLabel CT_CANDY{'SWET'};
static constexpr CargoLabel CT_TOFFEE{'TOFF'};
static constexpr CargoLabel CT_COLA{'COLA'};
static constexpr CargoLabel CT_COTTON_CANDY{'CTCD'};
static constexpr CargoLabel CT_BUBBLES{'BUBL'};
static constexpr CargoLabel CT_PLASTIC{'PLST'};
static constexpr CargoLabel CT_FIZZY_DRINKS{'FZDR'};

/** Dummy label for engines that carry no cargo; they actually carry 0 passengers. */
static constexpr CargoLabel CT_NONE = CT_PASSENGERS;

static constexpr CargoLabel CT_INVALID{UINT32_MAX}; ///< Invalid cargo type.

static const CargoType NUM_ORIGINAL_CARGO = 12; ///< Original number of cargo types.
static const CargoType NUM_CARGO = 64; ///< Maximum number of cargo types in a game.

/* CARGO_AUTO_REFIT and CARGO_NO_REFIT are stored in save-games for refit-orders, so should not be changed. */
static const CargoType CARGO_AUTO_REFIT = 0xFD; ///< Automatically choose cargo type when doing auto refitting.
static const CargoType CARGO_NO_REFIT = 0xFE; ///< Do not refit cargo of a vehicle (used in vehicle orders and auto-replace/auto-renew).

static const CargoType INVALID_CARGO = UINT8_MAX;

/** Mixed cargo types for definitions with cargo that can vary depending on climate. */
enum MixedCargoType : uint8_t {
	MCT_LIVESTOCK_FRUIT, ///< Cargo can be livestock or fruit.
	MCT_GRAIN_WHEAT_MAIZE, ///< Cargo can be grain, wheat or maize.
	MCT_VALUABLES_GOLD_DIAMONDS, ///< Cargo can be valuables, gold or diamonds.
};

/**
 * Special cargo filter criteria.
 * These are used by user interface code only and must not be assigned to any entity. Not all values are valid for every UI filter.
 */
namespace CargoFilterCriteria {
	static constexpr CargoType CF_ANY     = NUM_CARGO;     ///< Show all items independent of carried cargo (i.e. no filtering)
	static constexpr CargoType CF_NONE    = NUM_CARGO + 1; ///< Show only items which do not carry cargo (e.g. train engines)
	static constexpr CargoType CF_ENGINES = NUM_CARGO + 2; ///< Show only engines (for rail vehicles only)
	static constexpr CargoType CF_FREIGHT = NUM_CARGO + 3; ///< Show only vehicles which carry any freight (non-passenger) cargo

	static constexpr CargoType CF_NO_RATING   = NUM_CARGO + 4; ///< Show items with no rating (station list)
	static constexpr CargoType CF_SELECT_ALL  = NUM_CARGO + 5; ///< Select all items (station list)
	static constexpr CargoType CF_EXPAND_LIST = NUM_CARGO + 6; ///< Expand list to show all items (station list)
};

/** Test whether cargo type is not INVALID_CARGO */
inline bool IsValidCargoType(CargoType t) { return t != INVALID_CARGO; }

typedef uint64_t CargoTypes;

static const CargoTypes ALL_CARGOTYPES = (CargoTypes)UINT64_MAX;

/** Class for storing amounts of cargo */
struct CargoArray : std::array<uint, NUM_CARGO> {
	/** Reset all entries. */
	inline void Clear()
	{
		for (uint &it : *this) {
			it = 0;
		}
	}

	/**
	 * Get the sum of all cargo amounts.
	 * @return The sum.
	 */
	template <typename T>
	inline const T GetSum() const
	{
		T result = 0;
		for (const uint &it : *this) {
			result += it;
		}
		return result;
	}

	/**
	 * Get the amount of cargos that have an amount.
	 * @return The amount.
	 */
	inline uint GetCount() const
	{
		return std::ranges::count_if(*this, [](uint amount) { return amount != 0; });
	}
};


/** Types of cargo source and destination */
enum class SourceType : uint8_t {
	Industry,     ///< Source/destination is an industry
	Town,         ///< Source/destination is a town
	Headquarters, ///< Source/destination are company headquarters
};

typedef uint16_t SourceID; ///< Contains either industry ID, town ID or company ID (or INVALID_SOURCE)
static const SourceID INVALID_SOURCE = 0xFFFF; ///< Invalid/unknown index of source

/** A location from where cargo can come from (or go to). Specifically industries, towns and headquarters. */
struct Source {
	SourceID id; ///< Index of industry/town/HQ, INVALID_SOURCE if unknown/invalid.
	SourceType type; ///< Type of \c source_id.

	auto operator<=>(const Source &source) const = default;

	template <typename T>
	void Serialise(T &&buffer) const { buffer.Send_generic_seq(this->id, this->type); }

	template <typename T, typename V>
	bool Deserialise(T &buffer, V &&default_string_validation) { buffer.Recv_generic_seq(default_string_validation, this->id, this->type); return true; }

	void fmt_format_value(struct format_target &output) const;
};

#endif /* CARGO_TYPE_H */
