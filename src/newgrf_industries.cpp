/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_industries.cpp Handling of NewGRF industries. */

#include "stdafx.h"
#include "debug.h"
#include "industry.h"
#include "newgrf_badge.h"
#include "newgrf_industries.h"
#include "newgrf_town.h"
#include "newgrf_cargo.h"
#include "window_func.h"
#include "town.h"
#include "company_base.h"
#include "error.h"
#include "strings_func.h"
#include "newgrf_dump.h"
#include "core/random_func.hpp"

#include "table/strings.h"

#include "safeguards.h"

/* Since the industry IDs defined by the GRF file don't necessarily correlate
 * to those used by the game, the IDs used for overriding old industries must be
 * translated when the idustry spec is set. */
IndustryOverrideManager _industry_mngr(NEW_INDUSTRYOFFSET, NUM_INDUSTRYTYPES, IT_INVALID);
IndustryTileOverrideManager _industile_mngr(NEW_INDUSTRYTILEOFFSET, NUM_INDUSTRYTILES, INVALID_INDUSTRYTILE);

/**
 * Map the GRF local type to an industry type.
 * @param grf_type The GRF local type.
 * @param grf_id The GRF of the local type.
 * @return The industry type in the global scope.
 */
IndustryType MapNewGRFIndustryType(IndustryType grf_type, uint32_t grf_id)
{
	if (grf_type == IT_INVALID) return IT_INVALID;
	if (!HasBit(grf_type, 7)) return GB(grf_type, 0, 7);

	return _industry_mngr.GetID(GB(grf_type, 0, 7), grf_id);
}

/**
 * Make an analysis of a tile and check for its belonging to the same
 * industry, and/or the same grf file
 * @param tile TileIndex of the tile to query
 * @param i Industry to which to compare the tile to
 * @param cur_grfid GRFID of the current callback chain
 * @return value encoded as per NFO specs
 */
uint32_t GetIndustryIDAtOffset(TileIndex tile, const Industry *i, uint32_t cur_grfid)
{
	if (!i->TileBelongsToIndustry(tile)) {
		/* No industry and/or the tile does not have the same industry as the one we match it with */
		return 0xFFFF;
	}

	IndustryGfx gfx = GetCleanIndustryGfx(tile);
	const IndustryTileSpec *indtsp = GetIndustryTileSpec(gfx);

	if (gfx < NEW_INDUSTRYTILEOFFSET) { // Does it belongs to an old type?
		/* It is an old tile.  We have to see if it's been overridden */
		if (indtsp->grf_prop.override == INVALID_INDUSTRYTILE) { // has it been overridden?
			return 0xFF << 8 | gfx; // no. Tag FF + the gfx id of that tile
		}
		/* Overridden */
		const IndustryTileSpec *tile_ovr = GetIndustryTileSpec(indtsp->grf_prop.override);

		if (tile_ovr->grf_prop.grfid == cur_grfid) {
			return tile_ovr->grf_prop.local_id; // same grf file
		} else {
			return 0xFFFE; // not the same grf file
		}
	}
	/* Not an 'old type' tile */
	if (indtsp->grf_prop.GetSpriteGroup() != nullptr) { // tile has a spritegroup ?
		if (indtsp->grf_prop.grfid == cur_grfid) { // same industry, same grf ?
			return indtsp->grf_prop.local_id;
		} else {
			return 0xFFFE; // Defined in another grf file
		}
	}
	/* The tile has no spritegroup */
	return 0xFF << 8 | indtsp->grf_prop.subst_id; // so just give it the substitute
}

uint32_t IndustriesScopeResolver::GetClosestIndustry(IndustryType type) const
{
	if (type >= NUM_INDUSTRYTYPES) return UINT32_MAX;

	if (this->location_distance_cache == nullptr) {
		this->location_distance_cache = std::make_unique<IndustryLocationDistanceCache>();
	} else if (this->location_distance_cache->valid.test(type)) {
		return this->location_distance_cache->distances[type];
	}

	uint32_t best_dist = UINT32_MAX;
	const IndustryID this_id = this->industry->index;
	for (const IndustryLocationCacheEntry &entry : Industry::industries[type]) {
		if (entry.id == this_id) continue;

		best_dist = std::min(best_dist, DistanceManhattan(tile, entry.tile));
	}
	this->location_distance_cache->valid.set(type);
	this->location_distance_cache->distances[type] = best_dist;
	return best_dist;
}

/**
 * Implementation of both var 67 and 68
 * since the mechanism is almost the same, it is easier to regroup them on the same
 * function.
 * @param param_setID parameter given to the callback, which is the set id, or the local id, in our terminology
 * @param layout_filter on what layout do we filter?
 * @param town_filter Do we filter on the same town as the current industry?
 * @return the formatted answer to the callback : rr(reserved) cc(count) dddd(manhattan distance of closest sister)
 */
uint32_t IndustriesScopeResolver::GetCountAndDistanceOfClosestInstance(uint8_t param_setID, uint8_t layout_filter, bool town_filter, uint32_t mask) const
{
	uint32_t GrfID = GetRegister(0x100);  ///< Get the GRFID of the definition to look for in register 100h
	IndustryType ind_index;
	uint32_t closest_dist = UINT32_MAX;
	uint count = 0;

	/* Determine what will be the industry type to look for */
	switch (GrfID) {
		case 0:  // this is a default industry type
			ind_index = param_setID;
			break;

		case 0xFFFFFFFF: // current grf
			GrfID = GetIndustrySpec(this->industry->type)->grf_prop.grfid;
			[[fallthrough]];

		default: // use the grfid specified in register 100h
			SetBit(param_setID, 7); // bit 7 means it is not an old type
			ind_index = MapNewGRFIndustryType(param_setID, GrfID);
			break;
	}

	/* If the industry type is invalid, there is none and the closest is far away. */
	if (ind_index >= NUM_INDUSTRYTYPES) return 0 | 0xFFFF;

	if (layout_filter == 0 && !town_filter) {
		/* If the filter is 0, it could be because none was specified as well as being really a 0.
		 * In either case, just do the regular var67 */
		if (mask & 0xFFFF) closest_dist = this->GetClosestIndustry(ind_index);
		if (mask & 0xFF0000) count = ClampTo<uint8_t>(Industry::GetIndustryTypeCount(ind_index));
	} else if (layout_filter == 0 && town_filter) {
		/* Count only those which match the same industry type and town */
		std::unique_ptr<IndustryLocationDistanceAndCountCache> &cache = this->town_location_distance_cache;
		if (cache == nullptr) {
			cache = std::make_unique<IndustryLocationDistanceAndCountCache>();
			MemSetT(cache->distances, 0xFF, NUM_INDUSTRYTYPES);
			MemSetT(cache->counts, 0, NUM_INDUSTRYTYPES);
			const IndustryID this_id = this->industry->index;
			for (const IndustryLocationCacheEntry &entry : this->industry->town->industry_cache) {
				if (entry.id == this_id || entry.type >= NUM_INDUSTRYTYPES) continue;

				uint dist = DistanceManhattan(this->tile, entry.tile);
				if (dist < (uint)cache->distances[entry.type]) cache->distances[entry.type] = (uint16_t)dist;
				cache->counts[entry.type] = SaturatingAdd<uint8_t>(cache->counts[entry.type], 1);
			}
		}
		closest_dist = cache->distances[ind_index];
		count = cache->counts[ind_index];
	} else if (town_filter) {
		/* Count only those who match the same industry type and layout filter using the town cache */
		const IndustryID this_id = this->industry->index;
		for (const IndustryLocationCacheEntry &entry : this->industry->town->industry_cache) {
			if (entry.type == ind_index && entry.id != this_id && entry.selected_layout == layout_filter) {
				closest_dist = std::min(closest_dist, DistanceManhattan(this->tile, entry.tile));
				count++;
			}
		}
		count = std::min<uint>(count, UINT8_MAX);
	} else {
		/* Count only those who match the same industry type and layout filter
		 * Unfortunately, we have to do it manually */
		const IndustryID this_id = this->industry->index;
		for (const IndustryLocationCacheEntry &entry : Industry::industries[ind_index]) {
			if (entry.id != this_id && entry.selected_layout == layout_filter) {
				closest_dist = std::min(closest_dist, DistanceManhattan(this->tile, entry.tile));
				count++;
			}
		}
		count = std::min<uint>(count, UINT8_MAX);
	}

	return count << 16 | std::min<uint>(closest_dist, 0xFFFF);
}

/* virtual */ uint32_t IndustriesScopeResolver::GetVariable(uint16_t variable, uint32_t parameter, GetVariableExtra &extra) const
{
	if (this->ro.callback == CBID_INDUSTRY_LOCATION) {
		/* Variables available during construction check. */

		switch (variable) {
			case 0x7A: return GetBadgeVariableResult(*this->ro.grffile, GetIndustrySpec(this->type)->badges, parameter);

			case 0x80: return this->tile.base();
			case 0x81: return GB(this->tile.base(), 8, 8);

			/* Pointer to the town the industry is associated with */
			case 0x82: return this->industry->town->index;
			case 0x83:
			case 0x84:
			case 0x85: Debug(grf, 0, "NewGRFs shouldn't be doing pointer magic"); break; // not supported

			/* Number of the layout */
			case 0x86: return this->industry->selected_layout;

			/* Ground type */
			case 0x87: return GetTerrainType(this->tile);

			/* Town zone */
			case 0x88: return GetTownRadiusGroup(this->industry->town, this->tile);

			/* Manhattan distance of the closest town */
			case 0x89: return ClampTo<uint8_t>(DistanceManhattan(this->industry->town->xy, this->tile));

			/* Lowest height of the tile */
			case 0x8A: return ClampTo<uint8_t>(GetTileZ(this->tile) * (this->ro.grffile->grf_version >= 8 ? 1 : TILE_HEIGHT));

			/* Distance to the nearest water/land tile */
			case 0x8B: return GetClosestWaterDistance(this->tile, !GetIndustrySpec(this->industry->type)->behaviour.Test(IndustryBehaviour::BuiltOnWater));

			/* Square of Euclidean distance from town */
			case 0x8D: return ClampTo<uint16_t>(DistanceSquare(this->industry->town->xy, this->tile));

			/* 32 random bits */
			case 0x8F: return this->random_bits;
		}
	}

	const IndustrySpec *indspec = GetIndustrySpec(this->type);

	if (this->industry == nullptr) {
		/* Unconditionally allow these, with a dummy result, so that they can be considered always available for optimisation purposes */
		if (variable == 0x67 || variable == 0x68) {
			return 0 | 0xFFFF;
		}

		Debug(grf, 1, "Unhandled variable 0x{:X} (no available industry) in callback 0x{:x}", variable, this->ro.callback);

		extra.available = false;
		return UINT_MAX;
	}

	switch (variable) {
		case 0x40:
		case 0x41:
		case 0x42: { // waiting cargo, but only if those two callback flags are set
			IndustryCallbackMasks callback = indspec->callback_mask;
			if (callback.Any({IndustryCallbackMask::ProductionCargoArrival, IndustryCallbackMask::Production256Ticks})) {
				if (indspec->behaviour.Test(IndustryBehaviour::ProdMultiHandling)) {
					if (this->industry->prod_level == 0) return 0;
					return ClampTo<uint16_t>(this->industry->GetAccepted(variable - 0x40).waiting / this->industry->prod_level);
				} else {
					return ClampTo<uint16_t>(this->industry->GetAccepted(variable - 0x40).waiting);
				}
			} else {
				return 0;
			}
		}

		/* Manhattan distance of closes dry/water tile */
		case 0x43:
			if (this->tile == INVALID_TILE) break;
			return GetClosestWaterDistance(this->tile, !indspec->behaviour.Test(IndustryBehaviour::BuiltOnWater));

		/* Layout number */
		case 0x44: return this->industry->selected_layout;

		/* Company info */
		case 0x45: {
			uint8_t colours = 0;
			bool is_ai = false;

			const Company *c = Company::GetIfValid(this->industry->founder);
			if (c != nullptr) {
				const Livery *l = &c->livery[LS_DEFAULT];

				is_ai = c->is_ai;
				colours = l->colour1 + l->colour2 * 16;
			}

			return this->industry->founder | (is_ai ? 0x10000 : 0) | (colours << 24);
		}

		case 0x46: return this->industry->construction_date.base(); // Date when built - long format - (in days)

		/* Override flags from GS */
		case 0x47: return this->industry->ctlflags.base();

		/* Get industry ID at offset param */
		case 0x60: return GetIndustryIDAtOffset(GetNearbyTile(parameter, this->industry->location.tile, false), this->industry, this->ro.grffile->grfid);

		/* Get random tile bits at offset param */
		case 0x61: {
			if (this->tile == INVALID_TILE) break;
			TileIndex tile = GetNearbyTile(parameter, this->tile, false);
			return this->industry->TileBelongsToIndustry(tile) ? GetIndustryRandomBits(tile) : 0;
		}

		/* Land info of nearby tiles */
		case 0x62:
			if (this->tile == INVALID_TILE) break;
			return GetNearbyIndustryTileInformation(parameter, this->tile, INVALID_INDUSTRY, false, this->ro.grffile->grf_version >= 8, extra.mask);

		/* Animation stage of nearby tiles */
		case 0x63: {
			if (this->tile == INVALID_TILE) break;
			TileIndex tile = GetNearbyTile(parameter, this->tile, false);
			if (this->industry->TileBelongsToIndustry(tile)) {
				return GetAnimationFrame(tile);
			}
			return 0xFFFFFFFF;
		}

		/* Distance of nearest industry of given type */
		case 0x64:
			if (this->tile == INVALID_TILE) break;
			return this->GetClosestIndustry(MapNewGRFIndustryType(parameter, indspec->grf_prop.grfid));
		/* Get town zone and Manhattan distance of closest town */
		case 0x65: {
			if (this->tile == INVALID_TILE) break;
			TileIndex tile = GetNearbyTile(parameter, this->tile, true);
			return GetTownRadiusGroup(this->industry->town, tile) << 16 | ClampTo<uint16_t>(DistanceManhattan(tile, this->industry->town->xy));
		}
		/* Get square of Euclidean distance of closest town */
		case 0x66: {
			if (this->tile == INVALID_TILE) break;
			TileIndex tile = GetNearbyTile(parameter, this->tile, true);
			return DistanceSquare(tile, this->industry->town->xy);
		}

		/* Count of industry, distance of closest instance
		 * 68 is the same as 67, but with a filtering on selected layout */
		case 0x67:
		case 0x68: {
			uint8_t layout_filter = 0;
			bool town_filter = false;
			if (variable == 0x68) {
				uint32_t reg = GetRegister(0x101);
				layout_filter = GB(reg, 0, 8);
				town_filter = HasBit(reg, 8);
			}
			return this->GetCountAndDistanceOfClosestInstance(parameter, layout_filter, town_filter, extra.mask);
		}

		case 0x69:
		case 0x6A:
		case 0x6B:
		case 0x6C:
		case 0x6D:
		case 0x70:
		case 0x71: {
			CargoType cargo = GetCargoTranslation(parameter, this->ro.grffile);
			if (cargo == INVALID_CARGO) return 0;
			int index = this->industry->GetCargoProducedIndex(cargo);
			if (index < 0) return 0; // invalid cargo
			const Industry::ProducedCargo &p = this->industry->produced[index];
			switch (variable) {
				case 0x69: return p.waiting;
				case 0x6A: return p.history[THIS_MONTH].production;
				case 0x6B: return p.history[THIS_MONTH].transported;
				case 0x6C: return p.history[LAST_MONTH].production;
				case 0x6D: return p.history[LAST_MONTH].transported;
				case 0x70: return p.rate;
				case 0x71: return p.history[LAST_MONTH].PctTransported();
				default: NOT_REACHED();
			}
		}


		case 0x6E:
		case 0x6F: {
			CargoType cargo = GetCargoTranslation(parameter, this->ro.grffile);
			if (cargo == INVALID_CARGO) return 0;
			int index = this->industry->GetCargoAcceptedIndex(cargo);
			if (index < 0) return 0; // invalid cargo
			const Industry::AcceptedCargo &a = this->industry->accepted[index];
			if (variable == 0x6E) return a.last_accepted.base();
			if (variable == 0x6F) return a.waiting;
			NOT_REACHED();
		}

		case 0x7A: return GetBadgeVariableResult(*this->ro.grffile, GetIndustrySpec(this->type)->badges, parameter);

		/* Get a variable from the persistent storage */
		case 0x7C: return (this->industry->psa != nullptr) ? this->industry->psa->GetValue(parameter) : 0;

		/* Industry structure access*/
		case 0x80: return this->industry->location.tile.base();
		case 0x81: return GB(this->industry->location.tile.base(), 8, 8);
		/* Pointer to the town the industry is associated with */
		case 0x82: return this->industry->town->index;
		case 0x83:
		case 0x84:
		case 0x85: Debug(grf, 0, "NewGRFs shouldn't be doing pointer magic"); break; // not supported
		case 0x86: return this->industry->location.w;
		case 0x87: return this->industry->location.h;// xy dimensions

		case 0x88:
		case 0x89: return this->industry->GetProduced(variable - 0x88).cargo;
		case 0x8A: return this->industry->GetProduced(0).waiting;
		case 0x8B: return GB(this->industry->GetProduced(0).waiting, 8, 8);
		case 0x8C: return this->industry->GetProduced(1).waiting;
		case 0x8D: return GB(this->industry->GetProduced(1).waiting, 8, 8);
		case 0x8E:
		case 0x8F: return this->industry->GetProduced(variable - 0x8E).rate;
		case 0x90:
		case 0x91:
		case 0x92: return this->industry->GetAccepted(variable - 0x90).cargo;
		case 0x93: return this->industry->prod_level;
		/* amount of cargo produced so far THIS month. */
		case 0x94: return this->industry->GetProduced(0).history[THIS_MONTH].production;
		case 0x95: return GB(this->industry->GetProduced(0).history[THIS_MONTH].production, 8, 8);
		case 0x96: return this->industry->GetProduced(1).history[THIS_MONTH].production;
		case 0x97: return GB(this->industry->GetProduced(1).history[THIS_MONTH].production, 8, 8);
		/* amount of cargo transported so far THIS month. */
		case 0x98: return this->industry->GetProduced(0).history[THIS_MONTH].transported;
		case 0x99: return GB(this->industry->GetProduced(0).history[THIS_MONTH].transported, 8, 8);
		case 0x9A: return this->industry->GetProduced(1).history[THIS_MONTH].transported;
		case 0x9B: return GB(this->industry->GetProduced(1).history[THIS_MONTH].transported, 8, 8);
		/* fraction of cargo transported LAST month. */
		case 0x9C:
		case 0x9D: return this->industry->GetProduced(variable - 0x9C).history[LAST_MONTH].PctTransported();
		/* amount of cargo produced LAST month. */
		case 0x9E: return this->industry->GetProduced(0).history[LAST_MONTH].production;
		case 0x9F: return GB(this->industry->GetProduced(0).history[LAST_MONTH].production, 8, 8);
		case 0xA0: return this->industry->GetProduced(1).history[LAST_MONTH].production;
		case 0xA1: return GB(this->industry->GetProduced(1).history[LAST_MONTH].production, 8, 8);
		/* amount of cargo transported last month. */
		case 0xA2: return this->industry->GetProduced(0).history[LAST_MONTH].transported;
		case 0xA3: return GB(this->industry->GetProduced(0).history[LAST_MONTH].transported, 8, 8);
		case 0xA4: return this->industry->GetProduced(1).history[LAST_MONTH].transported;
		case 0xA5: return GB(this->industry->GetProduced(1).history[LAST_MONTH].transported, 8, 8);

		case 0xA6: return indspec->grf_prop.local_id;
		case 0xA7: return this->industry->founder;
		case 0xA8: return this->industry->random_colour;
		case 0xA9: return ClampTo<uint8_t>(this->industry->last_prod_year - EconTime::ORIGINAL_BASE_YEAR);
		case 0xAA: return this->industry->counter;
		case 0xAB: return GB(this->industry->counter, 8, 8);
		case 0xAC: return this->industry->was_cargo_delivered;

		case 0xB0: return ClampTo<uint16_t>(this->industry->construction_date - CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR); // Date when built since 1920 (in days)
		case 0xB3: return this->industry->construction_type; // Construction type
		case 0xB4: {
			const auto &acc = this->industry->Accepted();
			if (acc.empty()) return 0;
			auto it = std::max_element(acc.begin(), acc.end(), [](const auto &a, const auto &b) { return a.last_accepted < b.last_accepted; });
			if (EconTime::UsingWallclockUnits()) return ClampTo<uint16_t>(it->last_accepted - EconTime::DAYS_TILL_ORIGINAL_BASE_YEAR_WALLCLOCK_MODE);
			return ClampTo<uint16_t>(it->last_accepted - EconTime::DAYS_TILL_ORIGINAL_BASE_YEAR); // Date last cargo accepted since 1920 (in days)
		}
	}

	Debug(grf, 1, "Unhandled industry variable 0x{:X}", variable);

	extra.available = false;
	return UINT_MAX;
}

/* virtual */ uint32_t IndustriesScopeResolver::GetRandomBits() const
{
	return this->industry != nullptr ? this->industry->random : 0;
}

/* virtual */ uint32_t IndustriesScopeResolver::GetTriggers() const
{
	return 0;
}

/* virtual */ void IndustriesScopeResolver::StorePSA(uint pos, int32_t value)
{
	if (this->industry->index == INVALID_INDUSTRY) return;

	if (this->industry->psa == nullptr) {
		/* There is no need to create a storage if the value is zero. */
		if (value == 0) return;

		/* Create storage on first modification. */
		const IndustrySpec *indsp = GetIndustrySpec(this->industry->type);
		assert(PersistentStorage::CanAllocateItem());
		this->industry->psa = new PersistentStorage(indsp->grf_prop.grfid, GSF_INDUSTRIES, this->industry->location.tile);
	}

	this->industry->psa->StoreValue(pos, value);
}

/**
 * Get the grf file associated with the given industry type.
 * @param type Industry type to query.
 * @return The associated GRF file, if any.
 */
static const GRFFile *GetGrffile(IndustryType type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);
	return (indspec != nullptr) ? indspec->grf_prop.grffile : nullptr;
}

/**
 * Constructor of the industries resolver.
 * @param tile %Tile owned by the industry.
 * @param indus %Industry being resolved.
 * @param type Type of the industry.
 * @param random_bits Random bits of the new industry.
 * @param callback Callback ID.
 * @param callback_param1 First parameter (var 10) of the callback.
 * @param callback_param2 Second parameter (var 18) of the callback.
 */
IndustriesResolverObject::IndustriesResolverObject(TileIndex tile, Industry *indus, IndustryType type, uint32_t random_bits,
		CallbackID callback, uint32_t callback_param1, uint32_t callback_param2)
	: ResolverObject(GetGrffile(type), callback, callback_param1, callback_param2),
	industries_scope(*this, tile, indus, type, random_bits)
{
	this->root_spritegroup = GetIndustrySpec(type)->grf_prop.GetSpriteGroup();
}

/**
 * Get or create the town scope object associated with the industry.
 * @return The associated town scope, if it exists.
 */
TownScopeResolver *IndustriesResolverObject::GetTown()
{
	if (!this->town_scope.has_value()) {
		Town *t = nullptr;
		bool readonly = true;
		if (this->industries_scope.industry != nullptr) {
			t = this->industries_scope.industry->town;
			readonly = this->industries_scope.industry->index == INVALID_INDUSTRY;
		} else if (this->industries_scope.tile != INVALID_TILE) {
			t = ClosestTownFromTile(this->industries_scope.tile, UINT_MAX);
		}
		if (t == nullptr) return nullptr;
		this->town_scope.emplace(*this, t, readonly);
	}
	return &*this->town_scope;
}

GrfSpecFeature IndustriesResolverObject::GetFeature() const
{
	return GSF_INDUSTRIES;
}

uint32_t IndustriesResolverObject::GetDebugID() const
{
	return GetIndustrySpec(this->industries_scope.type)->grf_prop.local_id;
}

/**
 * Perform an industry callback.
 * @param callback The callback to perform.
 * @param param1 The first parameter.
 * @param param2 The second parameter.
 * @param industry The industry to do the callback for.
 * @param type The type of industry to do the callback for.
 * @param tile The tile associated with the callback.
 * @return The callback result.
 */
uint16_t GetIndustryCallback(CallbackID callback, uint32_t param1, uint32_t param2, Industry *industry, IndustryType type, TileIndex tile)
{
	IndustriesResolverObject object(tile, industry, type, 0, callback, param1, param2);
	return object.ResolveCallback();
}

/**
 * Check that the industry callback allows creation of the industry.
 * @param tile %Tile to build the industry.
 * @param type Type of industry to build.
 * @param layout Layout number.
 * @param seed Seed for the random generator.
 * @param initial_random_bits The random bits the industry is going to have after construction.
 * @param founder Industry founder
 * @param creation_type The circumstances the industry is created under.
 * @return Succeeded or failed command.
 */
CommandCost CheckIfCallBackAllowsCreation(TileIndex tile, IndustryType type, size_t layout, uint32_t seed, uint16_t initial_random_bits, Owner founder, IndustryAvailabilityCallType creation_type)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	Industry ind;
	ind.index = INVALID_INDUSTRY;
	ind.location.tile = tile;
	ind.location.w = 0; // important to mark the industry invalid
	ind.type = type;
	ind.selected_layout = (uint8_t)layout;
	ind.town = ClosestTownFromTile(tile, UINT_MAX);
	ind.random = initial_random_bits;
	ind.founder = founder;
	ind.psa = nullptr;

	IndustriesResolverObject object(tile, &ind, type, seed, CBID_INDUSTRY_LOCATION, 0, creation_type);
	uint16_t result = object.ResolveCallback();

	/* Unlike the "normal" cases, not having a valid result means we allow
	 * the building of the industry, as that's how it's done in TTDP. */
	if (result == CALLBACK_FAILED) return CommandCost();

	return GetErrorMessageFromLocationCallbackResult(result, indspec->grf_prop.grffile, STR_ERROR_SITE_UNSUITABLE);
}

/**
 * Check with callback #CBID_INDUSTRY_PROBABILITY whether the industry can be built.
 * @param type Industry type to check.
 * @param creation_type Reason to construct a new industry.
 * @return If the industry has no callback or allows building, \c true is returned. Otherwise, \c false is returned.
 */
uint32_t GetIndustryProbabilityCallback(IndustryType type, IndustryAvailabilityCallType creation_type, uint32_t default_prob)
{
	const IndustrySpec *indspec = GetIndustrySpec(type);

	if (indspec->callback_mask.Test(IndustryCallbackMask::Probability)) {
		uint16_t res = GetIndustryCallback(CBID_INDUSTRY_PROBABILITY, 0, creation_type, nullptr, type, INVALID_TILE);
		if (res != CALLBACK_FAILED) {
			if (indspec->grf_prop.grffile->grf_version < 8) {
				/* Disallow if result != 0 */
				if (res != 0) default_prob = 0;
			} else {
				/* Use returned probability. 0x100 to use default */
				if (res < 0x100) {
					default_prob = res;
				} else if (res > 0x100) {
					ErrorUnknownCallbackResult(indspec->grf_prop.grfid, CBID_INDUSTRY_PROBABILITY, res);
				}
			}
		}
	}
	return default_prob;
}

static int32_t DerefIndProd(int field, bool use_register)
{
	return use_register ? (int32_t)GetRegister(field) : field;
}

/**
 * Get the industry production callback and apply it to the industry.
 * @param ind    the industry this callback has to be called for
 * @param reason the reason it is called (0 = incoming cargo, 1 = periodic tick callback)
 */
void IndustryProductionCallback(Industry *ind, int reason)
{
	const IndustrySpec *spec = GetIndustrySpec(ind->type);
	IndustriesResolverObject object(ind->location.tile, ind, ind->type);
	if (spec->behaviour.Test(IndustryBehaviour::ProdCallbackRandom)) object.callback_param1 = Random();
	int multiplier = 1;
	if (spec->behaviour.Test(IndustryBehaviour::ProdMultiHandling)) multiplier = ind->prod_level;
	object.callback_param2 = reason;

	for (uint loop = 0;; loop++) {
		/* limit the number of calls to break infinite loops.
		 * 'loop' is provided as 16 bits to the newgrf, so abort when those are exceeded. */
		if (loop >= 0x10000) {
			/* display error message */
			SetDParamStr(0, spec->grf_prop.grffile->filename);
			SetDParam(1, spec->name);
			ShowErrorMessage(STR_NEWGRF_BUGGY, STR_NEWGRF_BUGGY_ENDLESS_PRODUCTION_CALLBACK, WL_WARNING);

			/* abort the function early, this error isn't critical and will allow the game to continue to run */
			break;
		}

		SB(object.callback_param2, 8, 16, loop);
		const SpriteGroup *tgroup = object.Resolve();
		if (tgroup == nullptr || tgroup->type != SGT_INDUSTRY_PRODUCTION) break;
		const IndustryProductionSpriteGroup *group = (const IndustryProductionSpriteGroup *)tgroup;

		if (group->version == 0xFF) {
			/* Result was marked invalid on load, display error message */
			SetDParamStr(0, spec->grf_prop.grffile->filename);
			SetDParam(1, spec->name);
			SetDParam(2, ind->location.tile);
			ShowErrorMessage(STR_NEWGRF_BUGGY, STR_NEWGRF_BUGGY_INVALID_CARGO_PRODUCTION_CALLBACK, WL_WARNING);

			/* abort the function early, this error isn't critical and will allow the game to continue to run */
			break;
		}

		bool deref = (group->version >= 1);

		if (group->version < 2) {
			/* Callback parameters map directly to industry cargo slot indices */
			for (uint i = 0; i < group->num_input && i < ind->accepted_cargo_count; i++) {
				if (ind->accepted[i].cargo == INVALID_CARGO) continue;
				ind->accepted[i].waiting = ClampTo<uint16_t>(ind->accepted[i].waiting - DerefIndProd(group->subtract_input[i], deref) * multiplier);
			}
			for (uint i = 0; i < group->num_output && i < ind->produced_cargo_count; i++) {
				if (ind->produced[i].cargo == INVALID_CARGO) continue;
				ind->produced[i].waiting = ClampTo<uint16_t>(ind->produced[i].waiting + std::max(DerefIndProd(group->add_output[i], deref), 0) * multiplier);
			}
		} else {
			/* Callback receives list of cargos to apply for, which need to have their cargo slots in industry looked up */
			for (uint i = 0; i < group->num_input; i++) {
				int cargo_index = ind->GetCargoAcceptedIndex(group->cargo_input[i]);
				if (cargo_index < 0) continue;
				ind->accepted[cargo_index].waiting = ClampTo<uint16_t>(ind->accepted[cargo_index].waiting - DerefIndProd(group->subtract_input[i], deref) * multiplier);
			}
			for (uint i = 0; i < group->num_output; i++) {
				int cargo_index = ind->GetCargoProducedIndex(group->cargo_output[i]);
				if (cargo_index < 0) continue;
				ind->produced[cargo_index].waiting = ClampTo<uint16_t>(ind->produced[cargo_index].waiting + std::max(DerefIndProd(group->add_output[i], deref), 0) * multiplier);
			}
		}

		int32_t again = DerefIndProd(group->again, deref);
		if (again == 0) break;

		SB(object.callback_param2, 24, 8, again);
	}

	SetWindowDirty(WC_INDUSTRY_VIEW, ind->index);
}

/**
 * Check whether an industry temporarily refuses to accept a certain cargo.
 * @param ind The industry to query.
 * @param cargo_type The cargo to get information about.
 * @pre cargo_type is in ind->accepts_cargo.
 * @return Whether the given industry refuses to accept this cargo type.
 */
bool IndustryTemporarilyRefusesCargo(Industry *ind, CargoType cargo_type)
{
	assert(ind->IsCargoAccepted(cargo_type));

	const IndustrySpec *indspec = GetIndustrySpec(ind->type);
	if (indspec->callback_mask.Test(IndustryCallbackMask::RefuseCargo)) {
		uint16_t res = GetIndustryCallback(CBID_INDUSTRY_REFUSE_CARGO,
				0, indspec->grf_prop.grffile->cargo_map[cargo_type],
				ind, ind->type, ind->location.tile);
		if (res != CALLBACK_FAILED) return !ConvertBooleanCallback(indspec->grf_prop.grffile, CBID_INDUSTRY_REFUSE_CARGO, res);
	}
	return false;
}

void DumpIndustrySpriteGroup(const IndustrySpec *spec, SpriteGroupDumper &dumper)
{
	dumper.DumpSpriteGroup(spec->grf_prop.GetSpriteGroup(), 0);
}

void DumpIndustryTileSpriteGroup(const IndustryTileSpec *spec, SpriteGroupDumper &dumper)
{
	dumper.DumpSpriteGroup(spec->grf_prop.GetSpriteGroup(), 0);
}
