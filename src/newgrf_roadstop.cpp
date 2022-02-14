/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command.cpp Handling of NewGRF road stops. */

#include "stdafx.h"
#include "debug.h"
#include "station_base.h"
#include "roadstop_base.h"
#include "newgrf_roadstop.h"
#include "newgrf_class_func.h"
#include "newgrf_cargo.h"
#include "newgrf_roadtype.h"
#include "gfx_type.h"
#include "company_func.h"
#include "road.h"
#include "window_type.h"
#include "date_func.h"
#include "town.h"
#include "viewport_func.h"

#include "safeguards.h"

template <typename Tspec, typename Tid, Tid Tmax>
void NewGRFClass<Tspec, Tid, Tmax>::InsertDefaults()
{
	/* Set up initial data */
	classes[0].global_id = 'DFLT';
	classes[0].name = STR_STATION_CLASS_DFLT;
	classes[0].Insert(nullptr);

	classes[1].global_id = 'WAYP';
	classes[1].name = STR_STATION_CLASS_WAYP;
	classes[1].Insert(nullptr);
}

template <typename Tspec, typename Tid, Tid Tmax>
bool NewGRFClass<Tspec, Tid, Tmax>::IsUIAvailable(uint index) const
{
	return true;
}

INSTANTIATE_NEWGRF_CLASS_METHODS(RoadStopClass, RoadStopSpec, RoadStopClassID, ROADSTOP_CLASS_MAX)

static const uint NUM_ROADSTOPSPECS_PER_STATION = 63; ///< Maximum number of parts per station.

uint32 RoadStopScopeResolver::GetRandomBits() const
{
	if (this->st == nullptr) return 0;

	uint32 bits = this->st->random_bits;
	if (this->tile != INVALID_TILE && Station::IsExpected(this->st)) {
		bits |= Station::From(this->st)->GetRoadStopRandomBits(this->tile) << 16;
	}
	return bits;
}

uint32 RoadStopScopeResolver::GetTriggers() const
{
	return this->st == nullptr ? 0 : this->st->waiting_triggers;
}

uint32 RoadStopScopeResolver::GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const
{
	switch (variable) {
		case 0x40: return this->view; // view

		case 0x41: // stop type
			if (this->type == STATION_BUS) return 0;
			if (this->type == STATION_TRUCK) return 1;
			return 2;

		case 0x42: return this->tile == INVALID_TILE ? 0 : GetTerrainType(this->tile, TCX_NORMAL); // terrain_type

		case 0x43: return this->tile == INVALID_TILE ? 0 : GetReverseRoadTypeTranslation(GetRoadTypeRoad(this->tile), this->roadstopspec->grf_prop.grffile); // road_type

		case 0x44: return this->tile == INVALID_TILE ? 0 : GetReverseRoadTypeTranslation(GetRoadTypeTram(this->tile), this->roadstopspec->grf_prop.grffile); // tram_type

		case 0x45: { // town_zone
			if (this->tile == INVALID_TILE) return HZB_TOWN_EDGE;
			const Town *t = ClosestTownFromTile(this->tile, UINT_MAX);
			return t != nullptr ? GetTownRadiusGroup(t, this->tile) : HZB_TOWN_EDGE;
		}

		case 0x46: return GetCompanyInfo(this->st == nullptr ? _current_company : this->st->owner); // company_type

		case 0xF0: return this->st == nullptr ? 0 : this->st->facilities; // facilities

		case 0xFA: return Clamp((this->st == nullptr ? _date : this->st->build_date) - DAYS_TILL_ORIGINAL_BASE_YEAR, 0, 65535); // build date
	}

	if (this->st != nullptr) return this->st->GetNewGRFVariable(this->ro, variable, parameter, &(extra->available));

	extra->available = false;
	return UINT_MAX;
}

const SpriteGroup *RoadStopResolverObject::ResolveReal(const RealSpriteGroup *group) const
{
	if (group == nullptr) return nullptr;

	return group->loading[0];
}

RoadStopResolverObject::RoadStopResolverObject(const RoadStopSpec *roadstopspec, BaseStation *st, TileIndex tile, const RoadTypeInfo *rti, StationType type, uint8 view,
		CallbackID callback, uint32 param1, uint32 param2)
	: ResolverObject(roadstopspec->grf_prop.grffile, callback, param1, param2), roadstop_scope(*this, st, roadstopspec, tile, rti, type, view)
	{

	this->town_scope = nullptr;

	CargoID ctype = CT_DEFAULT_NA;

	if (st == nullptr) {
		/* No station, so we are in a purchase list */
		ctype = CT_PURCHASE;
	} else if (Station::IsExpected(st)) {
		const Station *station = Station::From(st);
		/* Pick the first cargo that we have waiting */
		for (const CargoSpec *cs : CargoSpec::Iterate()) {
			if (roadstopspec->grf_prop.spritegroup[cs->Index()] != nullptr &&
					station->goods[cs->Index()].cargo.TotalCount() > 0) {
				ctype = cs->Index();
				break;
			}
		}
	}

	if (roadstopspec->grf_prop.spritegroup[ctype] == nullptr) {
		ctype = CT_DEFAULT;
	}

	/* Remember the cargo type we've picked */
	this->roadstop_scope.cargo_type = ctype;
	this->root_spritegroup = roadstopspec->grf_prop.spritegroup[ctype];
}

RoadStopResolverObject::~RoadStopResolverObject()
{
	delete this->town_scope;
}

TownScopeResolver* RoadStopResolverObject::GetTown()
{
	if (this->town_scope == nullptr) {
		Town *t;
		if (this->roadstop_scope.st != nullptr) {
			t = this->roadstop_scope.st->town;
		} else {
			t = ClosestTownFromTile(this->roadstop_scope.tile, UINT_MAX);
		}
		if (t == nullptr) return nullptr;
		this->town_scope = new TownScopeResolver(*this, t, this->roadstop_scope.st == nullptr);
	}
	return this->town_scope;
}

/**
 * Draw representation of a road stop tile for GUI purposes.
 * @param x position x of image.
 * @param y position y of image.
 * @param image an int offset for the sprite.
 * @param roadtype the RoadType of the underlying road.
 * @param spec the RoadStop's spec.
 * @return true of the tile was drawn (allows for fallback to default graphics)
 */
void DrawRoadStopTile(int x, int y, RoadType roadtype, const RoadStopSpec *spec, StationType type, int view)
{
	assert(roadtype != INVALID_ROADTYPE);
	assert(spec != nullptr);

	const RoadTypeInfo *rti = GetRoadTypeInfo(roadtype);
	RoadStopResolverObject object(spec, nullptr, INVALID_TILE, rti, type, view);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_TILELAYOUT) return;
	const DrawTileSprites *dts = ((const TileLayoutSpriteGroup *)group)->ProcessRegisters(nullptr);

	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);

	SpriteID image = dts->ground.sprite;
	PaletteID pal  = dts->ground.pal;

	if (GB(image, 0, SPRITE_WIDTH) != 0) {
		DrawSprite(image, GroundSpritePaletteTransform(image, pal, palette), x, y);
	}

	if (view >= 4) {
		/* Drive-through stop */
		uint sprite_offset = 5 - view;

		/* Road underlay takes precedence over tram */
		if (HasBit(spec->draw_mode, ROADSTOP_DRAW_MODE_OVERLAY)) {
			TileInfo ti {};
			ti.tile = INVALID_TILE;
			DrawRoadOverlays(&ti, PAL_NONE, rti, rti, sprite_offset, sprite_offset);
		}

		if (rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_GROUND);
			DrawSprite(ground + sprite_offset, PAL_NONE, x, y);

			SpriteID overlay = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_OVERLAY);
			if (overlay) DrawSprite(overlay + sprite_offset, PAL_NONE, x, y);
		} else if (RoadTypeIsTram(roadtype)) {
			DrawSprite(SPR_TRAMWAY_TRAM + sprite_offset, PAL_NONE, x, y);
		}
	} else {
		/* Drive-in stop */
		if (rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_ROADSTOP);
			DrawSprite(ground + view, PAL_NONE, x, y);
		}
	}

	DrawCommonTileSeqInGUI(x, y, dts, 0, 0, palette, true);
}

/**
 * Trigger road stop randomisation
 *
 * @param st the station being triggered
 * @param tile the exact tile of the station that should be triggered
 * @param trigger trigger type
 * @param cargo_type cargo type causing the trigger
 */
void TriggerRoadStopRandomisation(Station *st, TileIndex tile, RoadStopRandomTrigger trigger, CargoID cargo_type)
{
	if (st == nullptr) st = Station::GetByTile(tile);

	/* Check the cached cargo trigger bitmask to see if we need
	 * to bother with any further processing. */
	if (st->roadstop_cached_cargo_triggers == 0) return;
	if (cargo_type != CT_INVALID && !HasBit(st->roadstop_cached_cargo_triggers, cargo_type)) return;

	SetBit(st->waiting_triggers, trigger);

	uint32 whole_reseed = 0;

	CargoTypes empty_mask = 0;
	if (trigger == RSRT_CARGO_TAKEN) {
		/* Create a bitmask of completely empty cargo types to be matched */
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (st->goods[i].cargo.TotalCount() == 0) {
				SetBit(empty_mask, i);
			}
		}
	}

	uint32 used_triggers = 0;
	auto process_tile = [&](TileIndex cur_tile) {
		const RoadStopSpec *ss = GetRoadStopSpec(tile);
		if (ss == nullptr) return;

		/* Cargo taken "will only be triggered if all of those
		 * cargo types have no more cargo waiting." */
		if (trigger == RSRT_CARGO_TAKEN) {
			if ((ss->cargo_triggers & ~empty_mask) != 0) return;
		}

		if (cargo_type == CT_INVALID || HasBit(ss->cargo_triggers, cargo_type)) {
			int dir = GetRoadStopDir(cur_tile);
			if (IsDriveThroughStopTile(cur_tile)) dir += 4;

			RoadStopResolverObject object(ss, st, cur_tile, nullptr, GetStationType(cur_tile), dir);
			object.waiting_triggers = st->waiting_triggers;

			const SpriteGroup *group = object.Resolve();
			if (group == nullptr) return;

			used_triggers |= object.used_triggers;

			uint32 reseed = object.GetReseedSum();
			if (reseed != 0) {
				whole_reseed |= reseed;
				reseed >>= 16;

				/* Set individual tile random bits */
				uint8 random_bits = st->GetRoadStopRandomBits(cur_tile);
				random_bits &= ~reseed;
				random_bits |= Random() & reseed;
				st->SetRoadStopRandomBits(cur_tile, random_bits);

				MarkTileDirtyByTile(cur_tile, VMDF_NOT_MAP_MODE);
			}
		}
	};
	if (trigger == RSRT_NEW_CARGO || trigger == RSRT_CARGO_TAKEN) {
		for (TileIndex cur_tile : st->custom_road_stop_tiles) {
			process_tile(cur_tile);
		}
	} else {
		process_tile(tile);
	}

	/* Update whole station random bits */
	st->waiting_triggers &= ~used_triggers;
	if ((whole_reseed & 0xFFFF) != 0) {
		st->random_bits &= ~whole_reseed;
		st->random_bits |= Random() & whole_reseed;
	}
}

/**
 * Checks if there's any new stations by a specific RoadStopType
 * @param rs the RoadStopType to check for.
 * @return true if there was any new RoadStopSpec's found for the given RoadStopType, else false.
 */
bool GetIfNewStopsByType(RoadStopType rs)
{
	if (!(RoadStopClass::GetClassCount() > 1 || RoadStopClass::Get(ROADSTOP_CLASS_DFLT)->GetSpecCount() > 1)) return false;
	for (uint i = 0; i < RoadStopClass::GetClassCount(); i++) {
		// We don't want to check the default or waypoint classes. These classes are always available.
		if (i == ROADSTOP_CLASS_DFLT || i == ROADSTOP_CLASS_WAYP) continue;
		RoadStopClass *roadstopclass = RoadStopClass::Get((RoadStopClassID)i);
		if (GetIfClassHasNewStopsByType(roadstopclass, rs)) return true;
	}
	return false;
}

/**
 * Checks if the given RoadStopClass has any specs assigned to it, compatible with the given RoadStopType.
 * @param roadstopclass the RoadStopClass to check.
 * @param rs the RoadStopType to check by.
 * @return true if the roadstopclass has any specs compatible with the given RoadStopType.
 */
bool GetIfClassHasNewStopsByType(RoadStopClass *roadstopclass, RoadStopType rs)
{
	for (uint j = 0; j < roadstopclass->GetSpecCount(); j++) {
		if (GetIfStopIsForType(roadstopclass->GetSpec(j), rs)) return true;
	}
	return false;
}

/**
 * Checks if the given RoadStopSpec is compatible with the given RoadStopType.
 * @param roadstopspec the RoadStopSpec to check.
 * @param rs the RoadStopType to check by.
 * @return true if the roadstopspec is compatible with the given RoadStopType.
 */
bool GetIfStopIsForType(const RoadStopSpec *roadstopspec, RoadStopType rs)
{
	// The roadstopspec is nullptr, must be the default station, always return true.
	if (roadstopspec == nullptr) return true;
	if (roadstopspec->stop_type == ROADSTOPTYPE_ALL) return true;

	switch (rs) {
		case ROADSTOP_BUS:          if (roadstopspec->stop_type == ROADSTOPTYPE_PASSENGER) return true; break;
		case ROADSTOP_TRUCK:        if (roadstopspec->stop_type == ROADSTOPTYPE_FREIGHT)   return true; break;
	}
	return false;
}

const RoadStopSpec *GetRoadStopSpec(TileIndex t)
{
	if (!IsCustomRoadStopSpecIndex(t)) return nullptr;

	const BaseStation *st = BaseStation::GetByTile(t);
	uint specindex = GetCustomRoadStopSpecIndex(t);
	return specindex < st->num_roadstop_specs ? st->roadstop_speclist[specindex].spec : nullptr;
}

int AllocateRoadStopSpecToStation(const RoadStopSpec *statspec, BaseStation *st, bool exec)
{
	uint i;

	if (statspec == nullptr || st == nullptr) return 0;

	/* Try to find the same spec and return that one */
	for (i = 1; i < st->num_roadstop_specs && i < NUM_ROADSTOPSPECS_PER_STATION; i++) {
		if (st->roadstop_speclist[i].spec == statspec) return i;
	}

	/* Try to find an unused spec slot */
	for (i = 1; i < st->num_roadstop_specs && i < NUM_ROADSTOPSPECS_PER_STATION; i++) {
		if (st->roadstop_speclist[i].spec == nullptr && st->roadstop_speclist[i].grfid == 0) break;
	}

	if (i == NUM_ROADSTOPSPECS_PER_STATION) {
		/* Full, give up */
		return -1;
	}

	if (exec) {
		if (i >= st->num_roadstop_specs) {
			st->num_roadstop_specs = i + 1;
			st->roadstop_speclist = ReallocT(st->roadstop_speclist, st->num_roadstop_specs);

			if (st->num_roadstop_specs == 2) {
				/* Initial allocation */
				st->roadstop_speclist[0].spec     = nullptr;
				st->roadstop_speclist[0].grfid    = 0;
				st->roadstop_speclist[0].localidx = 0;
			}
		}

		st->roadstop_speclist[i].spec     = statspec;
		st->roadstop_speclist[i].grfid    = statspec->grf_prop.grffile->grfid;
		st->roadstop_speclist[i].localidx = statspec->grf_prop.local_id;

		StationUpdateRoadStopCachedTriggers(st);
	}

	return i;
}

void DeallocateRoadStopSpecFromStation(BaseStation *st, byte specindex)
{
	/* specindex of 0 (default) is never freeable */
	if (specindex == 0) return;

	/* Check custom road stop tiles if the specindex is still in use */
	for (TileIndex tile : st->custom_road_stop_tiles) {
		if (GetCustomRoadStopSpecIndex(tile) == specindex) {
			return;
		}
	}

	/* This specindex is no longer in use, so deallocate it */
	st->roadstop_speclist[specindex].spec     = nullptr;
	st->roadstop_speclist[specindex].grfid    = 0;
	st->roadstop_speclist[specindex].localidx = 0;

	/* If this was the highest spec index, reallocate */
	if (specindex == st->num_roadstop_specs - 1) {
		for (; st->roadstop_speclist[st->num_roadstop_specs - 1].grfid == 0 && st->num_roadstop_specs > 1; st->num_roadstop_specs--) {}

		if (st->num_roadstop_specs > 1) {
			st->roadstop_speclist = ReallocT(st->roadstop_speclist, st->num_roadstop_specs);
		} else {
			free(st->roadstop_speclist);
			st->num_roadstop_specs = 0;
			st->roadstop_speclist  = nullptr;
			st->roadstop_cached_cargo_triggers = 0;
			return;
		}
	}

	StationUpdateRoadStopCachedTriggers(st);
}

/**
 * Update the cached animation trigger bitmask for a station.
 * @param st Station to update.
 */
void StationUpdateRoadStopCachedTriggers(BaseStation *st)
{
	st->roadstop_cached_cargo_triggers = 0;

	/* Combine animation trigger bitmask for all road stop specs
	 * of this station. */
	for (uint i = 0; i < st->num_roadstop_specs; i++) {
		const RoadStopSpec *ss = st->roadstop_speclist[i].spec;
		if (ss != nullptr) {
			st->roadstop_cached_cargo_triggers |= ss->cargo_triggers;
		}
	}
}

void DumpRoadStopSpriteGroup(const BaseStation *st, const RoadStopSpec *spec, std::function<void(const char *)> print)
{
	CargoID ctype = CT_DEFAULT_NA;

	if (st == nullptr) {
		/* No station, so we are in a purchase list */
		ctype = CT_PURCHASE;
	} else if (Station::IsExpected(st)) {
		const Station *station = Station::From(st);
		/* Pick the first cargo that we have waiting */
		for (const CargoSpec *cs : CargoSpec::Iterate()) {
			if (spec->grf_prop.spritegroup[cs->Index()] != nullptr &&
					station->goods[cs->Index()].cargo.TotalCount() > 0) {
				ctype = cs->Index();
				break;
			}
		}
	}

	if (spec->grf_prop.spritegroup[ctype] == nullptr) {
		ctype = CT_DEFAULT;
	}

	DumpSpriteGroup(spec->grf_prop.spritegroup[ctype], std::move(print));
}
