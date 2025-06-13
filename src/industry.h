/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry.h Base of all industries. */

#ifndef INDUSTRY_H
#define INDUSTRY_H

#include "newgrf_storage.h"
#include "subsidy_type.h"
#include "industry_map.h"
#include "industrytype.h"
#include "tilearea_type.h"
#include "station_base.h"


typedef Pool<Industry, IndustryID, 64, 64000> IndustryPool;
extern IndustryPool _industry_pool;

struct IndustryLocationCacheEntry {
	IndustryID id;
	IndustryType type;
	uint8_t selected_layout;
	TileIndex tile;
};
static_assert(sizeof(IndustryLocationCacheEntry) == 8);

static const EconTime::YearDelta PROCESSING_INDUSTRY_ABANDONMENT_YEARS{5}; ///< If a processing industry doesn't produce for this many consecutive years, it may close.

/*
 * Production level maximum, minimum and default values.
 * It is not a value been really used in order to change, but rather an indicator
 * of how the industry is behaving.
 */
static constexpr uint8_t PRODLEVEL_CLOSURE = 0x00; ///< signal set to actually close the industry
static constexpr uint8_t PRODLEVEL_MINIMUM = 0x04; ///< below this level, the industry is set to be closing
static constexpr uint8_t PRODLEVEL_DEFAULT = 0x10; ///< default level set when the industry is created
static constexpr uint8_t PRODLEVEL_MAXIMUM = 0x80; ///< the industry is running at full speed

/**
 * Flags to control/override the behaviour of an industry.
 * These flags are controlled by game scripts.
 */
enum class IndustryControlFlag : uint8_t {
	/** When industry production change is evaluated, rolls to decrease are ignored. */
	NoProductionDecrease = 0,
	/** When industry production change is evaluated, rolls to increase are ignored. */
	NoProductionIncrease = 1,
	/**
	 * Industry can not close regardless of production level or time since last delivery.
	 * This does not prevent a closure already announced. */
	NoClosure = 2,
	/** Indicates that the production level of the industry is externally controlled. */
	ExternalProdLevel = 3,
	End,
};
using IndustryControlFlags = EnumBitSet<IndustryControlFlag, uint8_t, IndustryControlFlag::End>;

static const int THIS_MONTH = 0;
static const int LAST_MONTH = 1;

/**
 * Defines the internal data of a functional industry.
 */
struct Industry : IndustryPool::PoolItem<&_industry_pool> {
	struct ProducedHistory {
		uint32_t production; ///< Total produced
		uint32_t transported; ///< Total transported

		uint8_t PctTransported() const
		{
			if (this->production == 0) return 0;
			return ClampTo<uint8_t>(((uint64_t)this->transported) * 256 / this->production);
		}
	};

	struct ProducedCargo {
		CargoType cargo;                         ///< Cargo type
		uint8_t rate;                            ///< Production rate
		uint16_t waiting;                        ///< Amount of cargo produced
		std::array<ProducedHistory, 25> history; ///< History of cargo produced and transported
	};

	struct AcceptedCargo {
		CargoType cargo;              ///< Cargo type
		uint16_t waiting;             ///< Amount of cargo waiting to processed
		EconTime::Date last_accepted; ///< Last day cargo was accepted by this industry
	};

	IndustryType type;                                          ///< Type of industry.
	Owner owner;                                                ///< Owner of the industry.  Which SHOULD always be (imho) OWNER_NONE
	CalTime::Date construction_date;                            ///< Date of the construction of the industry
	TileArea location;                                          ///< Location of the industry
	Town *town;                                                 ///< Nearest town
	Station *neutral_station;                                   ///< Associated neutral station

	StationList stations_near;          ///< NOSAVE: List of nearby stations.
	mutable std::string cached_name;    ///< NOSAVE: Cache of the resolved name of the industry

	std::unique_ptr<ProducedCargo[]> produced;
	std::unique_ptr<AcceptedCargo[]> accepted;
	uint8_t produced_cargo_count{};
	uint8_t accepted_cargo_count{};

	std::span<ProducedCargo> Produced() { return { this->produced.get(), this->produced_cargo_count }; }
	std::span<const ProducedCargo> Produced() const { return { this->produced.get(), this->produced_cargo_count }; }
	std::span<AcceptedCargo> Accepted() { return { this->accepted.get(), this->accepted_cargo_count }; }
	std::span<const AcceptedCargo> Accepted() const { return { this->accepted.get(), this->accepted_cargo_count }; }

	uint16_t counter;                   ///< used for animation and/or production (if available cargo)
	uint8_t prod_level;                 ///< general production level
	Colours random_colour;              ///< randomized colour of the industry, for display purpose
	EconTime::Year last_prod_year;      ///< last year of production
	uint8_t was_cargo_delivered;        ///< flag that indicate this has been the closest industry chosen for cargo delivery by a station. see DeliverGoodsToIndustry
	IndustryControlFlags ctlflags;      ///< flags overriding standard behaviours

	PartOfSubsidy part_of_subsidy;      ///< NOSAVE: is this industry a source/destination of a subsidy?

	Owner founder;                      ///< Founder of the industry
	uint8_t construction_type;          ///< Way the industry was constructed (@see IndustryConstructionType)
	uint8_t selected_layout;            ///< Which tile layout was used when creating the industry
	Owner exclusive_supplier;           ///< Which company has exclusive rights to deliver cargo (INVALID_OWNER = anyone)
	Owner exclusive_consumer;           ///< Which company has exclusive rights to take cargo (INVALID_OWNER = anyone)
	std::string text;                   ///< General text with additional information.

	uint16_t random;                    ///< Random value used for randomisation of all kinds of things

	PersistentStorage *psa;             ///< Persistent storage for NewGRF industries.

	Industry(TileIndex tile = INVALID_TILE) : location(tile, 0, 0) {}
	~Industry();

	void RecomputeProductionMultipliers();

	/**
	 * Check if a given tile belongs to this industry.
	 * @param tile The tile to check.
	 * @return True if the tile is part of this industry.
	 */
	inline bool TileBelongsToIndustry(TileIndex tile) const
	{
		return IsTileType(tile, MP_INDUSTRY) && GetIndustryIndex(tile) == this->index;
	}

	/**
	 * Safely get a produced cargo slot, or an empty data if the slot does not exist.
	 * @param slot produced cargo slot to retrieve.
	 * @return the real slot, or an empty slot.
	 */
	inline const ProducedCargo &GetProduced(size_t slot) const
	{
		static const ProducedCargo empty{INVALID_CARGO, 0, 0, {}};
		return slot < (size_t)this->produced_cargo_count ? this->produced[slot] : empty;
	}

	/**
	 * Safely get an accepted cargo slot, or an empty data if the slot does not exist.
	 * @param slot accepted cargo slot to retrieve.
	 * @return the real slot, or an empty slot.
	 */
	inline const AcceptedCargo &GetAccepted(size_t slot) const
	{
		static const AcceptedCargo empty{INVALID_CARGO, 0, {}};
		return slot < (size_t)this->accepted_cargo_count ? this->accepted[slot] : empty;
	}

	inline int GetCargoProducedIndex(CargoType cargo) const
	{
		if (cargo == INVALID_CARGO) return -1;
		for (uint8_t i = 0; i < this->produced_cargo_count; i++) {
			if (this->produced[i].cargo == cargo) return i;
		}
		return -1;
	}

	inline int GetCargoAcceptedIndex(CargoType cargo) const
	{
		if (cargo == INVALID_CARGO) return -1;
		for (uint8_t i = 0; i < this->accepted_cargo_count; i++) {
			if (this->accepted[i].cargo == cargo) return i;
		}
		return -1;
	}

	/**
	 * Test if this industry accepts any cargo.
	 * @return true iff the industry accepts any cargo.
	 */
	bool IsCargoAccepted() const { return std::any_of(this->accepted.get(), this->accepted.get() + this->accepted_cargo_count, [](const AcceptedCargo &a) { return IsValidCargoType(a.cargo); }); }

	/**
	 * Test if this industry produces any cargo.
	 * @return true iff the industry produces any cargo.
	 */
	bool IsCargoProduced() const { return std::any_of(this->produced.get(), this->produced.get() + this->produced_cargo_count, [](const ProducedCargo &p) { return IsValidCargoType(p.cargo); }); }

	/**
	 * Test if this industry accepts a specific cargo.
	 * @param cargo Cargo type to test.
	 * @return true iff the industry accepts the given cargo type.
	 */
	bool IsCargoAccepted(CargoType cargo) const { return std::any_of(this->accepted.get(), this->accepted.get() + this->accepted_cargo_count, [&cargo](const AcceptedCargo &a) { return a.cargo == cargo; }); }

	/**
	 * Test if this industry produces a specific cargo.
	 * @param cargo Cargo type to test.
	 * @return true iff the industry produces the given cargo types.
	 */
	bool IsCargoProduced(CargoType cargo) const { return std::any_of(this->produced.get(), this->produced.get() + this->produced_cargo_count, [&cargo](const ProducedCargo &p) { return p.cargo == cargo; }); }

	/**
	 * Get the industry of the given tile
	 * @param tile the tile to get the industry from
	 * @pre IsTileType(t, MP_INDUSTRY)
	 * @return the industry
	 */
	static inline Industry *GetByTile(TileIndex tile)
	{
		return Industry::Get(GetIndustryIndex(tile));
	}

	static Industry *GetRandom();
	static void PostDestructor(size_t index);

	/**
	 * Get the count of industries for this type.
	 * @param type IndustryType to query
	 * @pre type < NUM_INDUSTRYTYPES
	 */
	static inline uint16_t GetIndustryTypeCount(IndustryType type)
	{
		assert(type < NUM_INDUSTRYTYPES);
		return static_cast<uint16_t>(std::size(industries[type]));
	}

	void AddToLocationCache();
	void RemoveFromLocationCache();

	inline const std::string &GetCachedName() const
	{
		if (this->cached_name.empty()) this->FillCachedName();
		return this->cached_name;
	}

	static std::array<std::vector<IndustryLocationCacheEntry>, NUM_INDUSTRYTYPES> industries; ///< List of industries of each type.

private:
	void FillCachedName() const;
};

void AddIndustriesToLocationCaches();

void ClearAllIndustryCachedNames();

void PlantRandomFarmField(const Industry *i);

void ReleaseDisastersTargetingIndustry(IndustryID);

bool IsTileForestIndustry(TileIndex tile);

/** Data for managing the number of industries of a single industry type. */
struct IndustryTypeBuildData {
	uint32_t probability;  ///< Relative probability of building this industry.
	uint8_t  min_number;   ///< Smallest number of industries that should exist (either \c 0 or \c 1).
	uint16_t target_count; ///< Desired number of industries of this type.
	uint16_t max_wait;     ///< Starting number of turns to wait (copied to #wait_count).
	uint16_t wait_count;   ///< Number of turns to wait before trying to build again.

	void Reset();

	bool GetIndustryTypeData(IndustryType it);
};

/**
 * Data for managing the number and type of industries in the game.
 */
struct IndustryBuildData {
	IndustryTypeBuildData builddata[NUM_INDUSTRYTYPES]; ///< Industry build data for every industry type.
	uint32_t wanted_inds; ///< Number of wanted industries (bits 31-16), and a fraction (bits 15-0).

	void Reset();

	void SetupTargetCount();
	void TryBuildNewIndustry();

	void MonthlyLoop();
};

extern IndustryBuildData _industry_builder;


/** Special values for the industry list window for the data parameter of #InvalidateWindowData. */
enum IndustryDirectoryInvalidateWindowData : uint8_t {
	IDIWD_FORCE_REBUILD,
	IDIWD_PRODUCTION_CHANGE,
	IDIWD_FORCE_RESORT,
};

void TrimIndustryAcceptedProduced(Industry *ind);

/* Old array structure used for savegames before SLV_INDUSTRY_CARGO_REORGANISE. */
struct OldIndustryAccepted {
	std::array<CargoType, INDUSTRY_NUM_INPUTS> old_cargo;
	std::array<uint16_t, INDUSTRY_NUM_INPUTS> old_waiting;
	std::array<EconTime::Date, INDUSTRY_NUM_INPUTS> old_last_accepted;

	void Reset();
};

/* Old array structure used for savegames before SLV_INDUSTRY_CARGO_REORGANISE. */
struct OldIndustryProduced {
	std::array<CargoType, INDUSTRY_NUM_OUTPUTS> old_cargo;
	std::array<uint16_t, INDUSTRY_NUM_OUTPUTS> old_waiting;
	std::array<uint8_t, INDUSTRY_NUM_OUTPUTS> old_rate;
	std::array<uint32_t, INDUSTRY_NUM_OUTPUTS> old_this_month_production;
	std::array<uint32_t, INDUSTRY_NUM_OUTPUTS> old_this_month_transported;
	std::array<uint32_t, INDUSTRY_NUM_OUTPUTS> old_last_month_production;
	std::array<uint32_t, INDUSTRY_NUM_OUTPUTS> old_last_month_transported;

	void Reset();
};

#endif /* INDUSTRY_H */
