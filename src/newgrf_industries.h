/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_industries.h Functions for NewGRF industries. */

#ifndef NEWGRF_INDUSTRIES_H
#define NEWGRF_INDUSTRIES_H

#include "newgrf_town.h"
#include <bitset>

struct IndustryLocationDistanceCache {
	std::bitset<NUM_INDUSTRYTYPES> valid;
	uint16_t distances[NUM_INDUSTRYTYPES];
};

struct IndustryLocationDistanceAndCountCache {
	uint16_t distances[NUM_INDUSTRYTYPES];
	uint8_t counts[NUM_INDUSTRYTYPES];
};

/** Resolver for industry scopes. */
struct IndustriesScopeResolver : public ScopeResolver {
	TileIndex tile;       ///< Tile owned by the industry.
	uint32_t random_bits; ///< Random bits of the new industry.
	Industry *industry;   ///< %Industry being resolved.
	IndustryType type;    ///< Type of the industry.

	mutable std::unique_ptr<IndustryLocationDistanceCache> location_distance_cache;
	mutable std::unique_ptr<IndustryLocationDistanceAndCountCache> town_location_distance_cache;

	/**
	 * Scope resolver for industries.
	 * @param ro Surrounding resolver.
	 * @param tile %Tile owned by the industry.
	 * @param industry %Industry being resolved.
	 * @param type Type of the industry.
	 * @param random_bits Random bits of the new industry.
	 */
	IndustriesScopeResolver(ResolverObject &ro, TileIndex tile, Industry *industry, IndustryType type, uint32_t random_bits = 0)
		: ScopeResolver(ro), tile(tile), random_bits(random_bits), industry(industry), type(type)
	{
	}

	uint32_t GetRandomBits() const override;
	uint32_t GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const override;
	uint32_t GetTriggers() const override;
	void StorePSA(uint pos, int32_t value) override;

	uint32_t GetCountAndDistanceOfClosestInstance(uint8_t param_setID, uint8_t layout_filter, bool town_filter, uint32_t mask) const;
	uint32_t GetClosestIndustry(IndustryType type) const;
};

/** Resolver for industries. */
struct IndustriesResolverObject : public ResolverObject {
	IndustriesScopeResolver industries_scope; ///< Scope resolver for the industry.
	std::optional<TownScopeResolver> town_scope = std::nullopt; ///< Scope resolver for the associated town (if needed and available, else \c std::nullopt).

	IndustriesResolverObject(TileIndex tile, Industry *indus, IndustryType type, uint32_t random_bits = 0,
			CallbackID callback = CBID_NO_CALLBACK, uint32_t callback_param1 = 0, uint32_t callback_param2 = 0);

	TownScopeResolver *GetTown();

	ScopeResolver *GetScope(VarSpriteGroupScope scope = VSG_SCOPE_SELF, VarSpriteGroupScopeOffset relative = 0) override
	{
		switch (scope) {
			case VSG_SCOPE_SELF: return &industries_scope;
			case VSG_SCOPE_PARENT: {
				TownScopeResolver *tsr = this->GetTown();
				if (tsr != nullptr) return tsr;
			}
			[[fallthrough]];

			default:
				return ResolverObject::GetScope(scope, relative);
		}
	}

	GrfSpecFeature GetFeature() const override;
	uint32_t GetDebugID() const override;
};

/** When should the industry(tile) be triggered for random bits? */
enum IndustryTrigger : uint8_t {
	/** Triggered each tile loop */
	INDUSTRY_TRIGGER_TILELOOP_PROCESS = 1,
	/** Triggered (whole industry) each 256 ticks */
	INDUSTRY_TRIGGER_256_TICKS        = 2,
	/** Triggered on cargo delivery */
	INDUSTRY_TRIGGER_CARGO_DELIVERY   = 4,
};

/** From where has callback #CBID_INDUSTRY_PROBABILITY been called */
enum IndustryAvailabilityCallType : uint8_t {
	IACT_MAPGENERATION,    ///< during random map generation
	IACT_RANDOMCREATION,   ///< during creation of random ingame industry
	IACT_USERCREATION,     ///< from the Fund/build window
	IACT_PROSPECTCREATION, ///< from the Fund/build using prospecting
};

/* in newgrf_industry.cpp */
uint16_t GetIndustryCallback(CallbackID callback, uint32_t param1, uint32_t param2, Industry *industry, IndustryType type, TileIndex tile);
uint32_t GetIndustryIDAtOffset(TileIndex new_tile, const Industry *i, uint32_t cur_grfid);
void IndustryProductionCallback(Industry *ind, int reason);
CommandCost CheckIfCallBackAllowsCreation(TileIndex tile, IndustryType type, size_t layout, uint32_t seed, uint16_t initial_random_bits, Owner founder, IndustryAvailabilityCallType creation_type);
uint32_t GetIndustryProbabilityCallback(IndustryType type, IndustryAvailabilityCallType creation_type, uint32_t default_prob);
bool IndustryTemporarilyRefusesCargo(Industry *ind, CargoType cargo_type);

IndustryType MapNewGRFIndustryType(IndustryType grf_type, uint32_t grf_id);

/* in newgrf_industrytiles.cpp*/
uint32_t GetNearbyIndustryTileInformation(uint8_t parameter, TileIndex tile, IndustryID index, bool signed_offsets, bool grf_version8, uint32_t mask);

#endif /* NEWGRF_INDUSTRIES_H */
