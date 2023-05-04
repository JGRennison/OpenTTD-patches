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
#include "newgrf_animation_base.h"
#include "newgrf_sound.h"
#include "newgrf_extension.h"

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

uint32 RoadStopScopeResolver::GetNearbyRoadStopsInfo(uint32 parameter, bool v2) const
{
	if (this->tile == INVALID_TILE) return 0xFFFFFFFF;
	TileIndex nearby_tile = GetNearbyTile(parameter, this->tile);

	if (!IsAnyRoadStopTile(nearby_tile)) return 0xFFFFFFFF;

	uint32 grfid = this->st->roadstop_speclist[GetCustomRoadStopSpecIndex(this->tile)].grfid;
	bool same_orientation = GetStationGfx(this->tile) == GetStationGfx(nearby_tile);
	bool same_station = GetStationIndex(nearby_tile) == this->st->index;
	uint32 res = GetStationGfx(nearby_tile) << 12 | !same_orientation << 11 | !!same_station << 10;
	StationType type = GetStationType(nearby_tile);
	if (type == STATION_TRUCK) res |= (1 << 16);
	if (type == STATION_ROADWAYPOINT) res |= (2 << 16);
	if (type == this->type) SetBit(res, 20);

	uint16 localidx = 0;
	if (IsCustomRoadStopSpecIndex(nearby_tile)) {
		const RoadStopSpecList ssl = BaseStation::GetByTile(nearby_tile)->roadstop_speclist[GetCustomRoadStopSpecIndex(nearby_tile)];
		localidx = ssl.localidx;
		res |= 1 << (ssl.grfid != grfid ? 9 : 8);
	}
	if (IsDriveThroughStopTile(nearby_tile)) {
		res |= (GetDriveThroughStopDisallowedRoadDirections(nearby_tile) << 21);
	}

	if (v2) {
		return (res << 8) | localidx;
	} else {
		return res | (localidx & 0xFF) | ((localidx & 0xFF00) << 16);
	}
}

uint32 RoadStopScopeResolver::GetVariable(uint16 variable, uint32 parameter, GetVariableExtra *extra) const
{
	auto get_road_type_variable = [&](RoadTramType rtt) -> uint32 {
		RoadType rt;
		if (this->tile == INVALID_TILE) {
			rt = (GetRoadTramType(this->roadtype) == rtt) ? this->roadtype : INVALID_ROADTYPE;
		} else {
			rt = GetRoadType(this->tile, rtt);
		}
		if (rt == INVALID_ROADTYPE) {
			return 0xFFFFFFFF;
		} else {
			return GetReverseRoadTypeTranslation(rt, this->roadstopspec->grf_prop.grffile);
		}
	};

	switch (variable) {
		/* View/rotation */
		case 0x40: return this->view;

		/* Stop type: 0: bus, 1: truck, 2: waypoint */
		case 0x41:
			if (this->type == STATION_BUS) return 0;
			if (this->type == STATION_TRUCK) return 1;
			return 2;

		/* Terrain type */
		case 0x42: return this->tile == INVALID_TILE ? 0 : GetTerrainType(this->tile, TCX_NORMAL); // terrain_type

		/* Road type */
		case 0x43: return get_road_type_variable(RTT_ROAD);

		/* Tram type */
		case 0x44: return get_road_type_variable(RTT_TRAM);

		/* Town zone and Manhattan distance of closest town */
		case 0x45: {
			if (this->tile == INVALID_TILE) return HZB_TOWN_EDGE << 16;
			const Town *t = (this->st == nullptr) ? ClosestTownFromTile(this->tile, UINT_MAX) : this->st->town;
			return t != nullptr ? (GetTownRadiusGroup(t, this->tile) << 16 | std::min(DistanceManhattan(this->tile, t->xy), 0xFFFFu)) : HZB_TOWN_EDGE << 16;
		}

		/* Get square of Euclidian distance of closest town */
		case 0x46: {
			if (this->tile == INVALID_TILE) return 0;
			const Town *t = (this->st == nullptr) ? ClosestTownFromTile(this->tile, UINT_MAX) : this->st->town;
			return t != nullptr ? DistanceSquare(this->tile, t->xy) : 0;
		}

		/* Company information */
		case 0x47: return GetCompanyInfo(this->st == nullptr ? _current_company : this->st->owner);

		/* Animation frame */
		case 0x49: return this->tile == INVALID_TILE ? 0 : this->st->GetRoadStopAnimationFrame(this->tile);

		/* Misc info */
		case 0x50: {
			uint32 result = 0;
			if (this->tile != INVALID_TILE) {
				if (IsDriveThroughStopTile(this->tile)) {
					result |= GetDriveThroughStopDisallowedRoadDirections(this->tile);
					RoadCachedOneWayState rcows = GetRoadCachedOneWayState(this->tile);
					if (rcows <= RCOWS_NO_ACCESS) result |= (rcows << 2);
				}
			} else {
				SetBit(result, 4);
			}
			return result;
		}

		/* Variables which use the parameter */
		/* Variables 0x60 to 0x65 and 0x69 are handled separately below */

		/* Animation frame of nearby tile */
		case 0x66: {
			if (this->tile == INVALID_TILE) return UINT_MAX;
			TileIndex tile = this->tile;
			if (parameter != 0) tile = GetNearbyTile(parameter, tile);
			return (IsAnyRoadStopTile(tile) && GetStationIndex(tile) == this->st->index) ? this->st->GetRoadStopAnimationFrame(tile) : UINT_MAX;
		}

		/* Land info of nearby tile */
		case 0x67: {
			if (this->tile == INVALID_TILE) return 0;
			TileIndex tile = this->tile;
			if (parameter != 0) tile = GetNearbyTile(parameter, tile); // only perform if it is required
			return GetNearbyTileInformation(tile, this->ro.grffile->grf_version >= 8, extra->mask);
		}

		/* Road stop info of nearby tiles */
		case 0x68: {
			return this->GetNearbyRoadStopsInfo(parameter, false);
		}

		/* Road stop info of nearby tiles: v2 */
		case A2VRI_ROADSTOP_INFO_NEARBY_TILES_V2: {
			return this->GetNearbyRoadStopsInfo(parameter, true);
		}

		/* GRFID of nearby road stop tiles */
		case 0x6A: {
			if (this->tile == INVALID_TILE) return 0xFFFFFFFF;
			TileIndex nearby_tile = GetNearbyTile(parameter, this->tile);

			if (!IsAnyRoadStopTile(nearby_tile)) return 0xFFFFFFFF;
			if (!IsCustomRoadStopSpecIndex(nearby_tile)) return 0;

			const RoadStopSpecList ssl = BaseStation::GetByTile(nearby_tile)->roadstop_speclist[GetCustomRoadStopSpecIndex(nearby_tile)];
			return ssl.grfid;
		}

		/* Road info of nearby tiles */
		case 0x6B: {
			if (this->tile == INVALID_TILE) return 0xFFFFFFFF;
			TileIndex nearby_tile = GetNearbyTile(parameter, this->tile);

			if (!IsNormalRoadTile(nearby_tile)) return 0xFFFFFFFF;

			RoadBits road = GetRoadBits(nearby_tile, RTT_ROAD);
			RoadBits tram = GetRoadBits(nearby_tile, RTT_TRAM);
			Slope tileh = GetTileSlope(nearby_tile);
			extern uint GetRoadSpriteOffset(Slope slope, RoadBits bits);
			uint road_offset = (road == 0) ? 0xFF : GetRoadSpriteOffset(tileh, road);
			uint tram_offset = (tram == 0) ? 0xFF : GetRoadSpriteOffset(tileh, tram);

			return (tram_offset << 16) | (road_offset << 8) | (tram << 4) | (road);
		}

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

RoadStopResolverObject::RoadStopResolverObject(const RoadStopSpec *roadstopspec, BaseStation *st, TileIndex tile, RoadType roadtype, StationType type, uint8 view,
		CallbackID callback, uint32 param1, uint32 param2)
	: ResolverObject(roadstopspec->grf_prop.grffile, callback, param1, param2), roadstop_scope(*this, st, roadstopspec, tile, roadtype, type, view)
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

uint16 GetRoadStopCallback(CallbackID callback, uint32 param1, uint32 param2, const RoadStopSpec *roadstopspec, BaseStation *st, TileIndex tile, RoadType roadtype, StationType type, uint8 view)
{
	RoadStopResolverObject object(roadstopspec, st, tile, roadtype, type, view, callback, param1, param2);
	return object.ResolveCallback();
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
	RoadStopResolverObject object(spec, nullptr, INVALID_TILE, roadtype, type, view);
	const SpriteGroup *group = object.Resolve();
	if (group == nullptr || group->type != SGT_TILELAYOUT) return;
	const DrawTileSprites *dts = ((const TileLayoutSpriteGroup *)group)->ProcessRegisters(nullptr);

	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);

	SpriteID image = dts->ground.sprite;
	PaletteID pal  = dts->ground.pal;

	RoadStopDrawMode draw_mode;
	if (HasBit(spec->flags, RSF_DRAW_MODE_REGISTER)) {
		draw_mode = (RoadStopDrawMode)GetRegister(0x100);
	} else {
		draw_mode = spec->draw_mode;
	}

	if (type == STATION_ROADWAYPOINT) {
		DrawSprite(SPR_ROAD_PAVED_STRAIGHT_X, PAL_NONE, x, y);
		if ((draw_mode & ROADSTOP_DRAW_MODE_WAYP_GROUND) && GB(image, 0, SPRITE_WIDTH) != 0) {
			DrawSprite(image, GroundSpritePaletteTransform(image, pal, palette), x, y);
		}
	} else if (GB(image, 0, SPRITE_WIDTH) != 0) {
		DrawSprite(image, GroundSpritePaletteTransform(image, pal, palette), x, y);
	}

	if (view >= 4) {
		/* Drive-through stop */
		uint sprite_offset = 5 - view;

		/* Road underlay takes precedence over tram */
		if (type == STATION_ROADWAYPOINT || draw_mode & ROADSTOP_DRAW_MODE_OVERLAY) {
			if (rti->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_GROUND);
				DrawSprite(ground + sprite_offset, PAL_NONE, x, y);

				SpriteID overlay = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_OVERLAY);
				if (overlay) DrawSprite(overlay + sprite_offset, PAL_NONE, x, y);
			} else if (RoadTypeIsTram(roadtype)) {
				DrawSprite(SPR_TRAMWAY_TRAM + sprite_offset, PAL_NONE, x, y);
			}
		}
	} else {
		/* Drive-in stop */
		if ((draw_mode & ROADSTOP_DRAW_MODE_ROAD) && rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_ROADSTOP);
			DrawSprite(ground + view, PAL_NONE, x, y);
		}
	}

	DrawCommonTileSeqInGUI(x, y, dts, 0, 0, palette, true);
}

/** Wrapper for animation control, see GetRoadStopCallback. */
uint16 GetAnimRoadStopCallback(CallbackID callback, uint32 param1, uint32 param2, const RoadStopSpec *roadstopspec, BaseStation *st, TileIndex tile, int extra_data)
{
	return GetRoadStopCallback(callback, param1, param2, roadstopspec, st, tile, INVALID_ROADTYPE, GetStationType(tile), GetStationGfx(tile));
}

struct RoadStopAnimationFrameAnimationHelper {
	static byte Get(BaseStation *st, TileIndex tile) { return st->GetRoadStopAnimationFrame(tile); }
	static bool Set(BaseStation *st, TileIndex tile, byte frame) { return st->SetRoadStopAnimationFrame(tile, frame); }
};

/** Helper class for animation control. */
struct RoadStopAnimationBase : public AnimationBase<RoadStopAnimationBase, RoadStopSpec, BaseStation, int, GetAnimRoadStopCallback, RoadStopAnimationFrameAnimationHelper> {
	static const CallbackID cb_animation_speed      = CBID_STATION_ANIMATION_SPEED;
	static const CallbackID cb_animation_next_frame = CBID_STATION_ANIM_NEXT_FRAME;

	static const RoadStopCallbackMask cbm_animation_speed      = CBM_ROAD_STOP_ANIMATION_SPEED;
	static const RoadStopCallbackMask cbm_animation_next_frame = CBM_ROAD_STOP_ANIMATION_NEXT_FRAME;
};

void AnimateRoadStopTile(TileIndex tile)
{
	const RoadStopSpec *ss = GetRoadStopSpec(tile);
	if (ss == nullptr) return;

	RoadStopAnimationBase::AnimateTile(ss, BaseStation::GetByTile(tile), tile, HasBit(ss->flags, RSF_CB141_RANDOM_BITS));
}

uint8 GetRoadStopTileAnimationSpeed(TileIndex tile)
{
	const RoadStopSpec *ss = GetRoadStopSpec(tile);
	if (ss == nullptr) return 0;

	return RoadStopAnimationBase::GetAnimationSpeed(ss);
}

void TriggerRoadStopAnimation(BaseStation *st, TileIndex trigger_tile, StationAnimationTrigger trigger, CargoID cargo_type)
{
	/* Get Station if it wasn't supplied */
	if (st == nullptr) st = BaseStation::GetByTile(trigger_tile);

	/* Check the cached animation trigger bitmask to see if we need
	 * to bother with any further processing. */
	if (!HasBit(st->cached_roadstop_anim_triggers, trigger)) return;

	uint16 random_bits = Random();
	auto process_tile = [&](TileIndex cur_tile) {
		const RoadStopSpec *ss = GetRoadStopSpec(cur_tile);
		if (ss != nullptr && HasBit(ss->animation.triggers, trigger)) {
			CargoID cargo;
			if (cargo_type == CT_INVALID) {
				cargo = CT_INVALID;
			} else {
				cargo = ss->grf_prop.grffile->cargo_map[cargo_type];
			}
			RoadStopAnimationBase::ChangeAnimationFrame(CBID_STATION_ANIM_START_STOP, ss, st, cur_tile, (random_bits << 16) | Random(), (uint8)trigger | (cargo << 8));
		}
	};

	if (trigger == SAT_NEW_CARGO || trigger == SAT_CARGO_TAKEN || trigger == SAT_250_TICKS) {
		for (const RoadStopTileData &tile_data : st->custom_roadstop_tile_data) {
			process_tile(tile_data.tile);
		}
	} else {
		process_tile(trigger_tile);
	}
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
	if (st->cached_roadstop_cargo_triggers == 0) return;
	if (cargo_type != CT_INVALID && !HasBit(st->cached_roadstop_cargo_triggers, cargo_type)) return;

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
		const RoadStopSpec *ss = GetRoadStopSpec(cur_tile);
		if (ss == nullptr) return;

		/* Cargo taken "will only be triggered if all of those
		 * cargo types have no more cargo waiting." */
		if (trigger == RSRT_CARGO_TAKEN) {
			if ((ss->cargo_triggers & ~empty_mask) != 0) return;
		}

		if (cargo_type == CT_INVALID || HasBit(ss->cargo_triggers, cargo_type)) {
			RoadStopResolverObject object(ss, st, cur_tile, INVALID_ROADTYPE, GetStationType(cur_tile), GetStationGfx(cur_tile));
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
		for (const RoadStopTileData &tile_data : st->custom_roadstop_tile_data) {
			process_tile(tile_data.tile);
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
 * @param rs the RoadStopType to check.
 * @param roadtype the RoadType to check.
 * @return true if there was any new RoadStopSpec's found for the given RoadStopType and RoadType, else false.
 */
bool GetIfNewStopsByType(RoadStopType rs, RoadType roadtype)
{
	if (!(RoadStopClass::GetClassCount() > 1 || RoadStopClass::Get(ROADSTOP_CLASS_DFLT)->GetSpecCount() > 1)) return false;
	for (uint i = 0; RoadStopClass::IsClassIDValid((RoadStopClassID)i); i++) {
		// We don't want to check the default or waypoint classes. These classes are always available.
		if (i == ROADSTOP_CLASS_DFLT || i == ROADSTOP_CLASS_WAYP) continue;
		RoadStopClass *roadstopclass = RoadStopClass::Get((RoadStopClassID)i);
		if (GetIfClassHasNewStopsByType(roadstopclass, rs, roadtype)) return true;
	}
	return false;
}

/**
 * Checks if the given RoadStopClass has any specs assigned to it, compatible with the given RoadStopType.
 * @param roadstopclass the RoadStopClass to check.
 * @param rs the RoadStopType to check.
 * @param roadtype the RoadType to check.
 * @return true if the RoadStopSpec has any specs compatible with the given RoadStopType and RoadType.
 */
bool GetIfClassHasNewStopsByType(RoadStopClass *roadstopclass, RoadStopType rs, RoadType roadtype)
{
	for (uint j = 0; j < roadstopclass->GetSpecCount(); j++) {
		if (GetIfStopIsForType(roadstopclass->GetSpec(j), rs, roadtype)) return true;
	}
	return false;
}

/**
 * Checks if the given RoadStopSpec is compatible with the given RoadStopType.
 * @param roadstopspec the RoadStopSpec to check.
 * @param rs the RoadStopType to check.
 * @param roadtype the RoadType to check.
 * @return true if the RoadStopSpec is compatible with the given RoadStopType and RoadType.
 */
bool GetIfStopIsForType(const RoadStopSpec *roadstopspec, RoadStopType rs, RoadType roadtype)
{
	// The roadstopspec is nullptr, must be the default station, always return true.
	if (roadstopspec == nullptr) return true;

	if (HasBit(roadstopspec->flags, RSF_BUILD_MENU_ROAD_ONLY) && !RoadTypeIsRoad(roadtype)) return false;
	if (HasBit(roadstopspec->flags, RSF_BUILD_MENU_TRAM_ONLY) && !RoadTypeIsTram(roadtype)) return false;

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
	return specindex < st->roadstop_speclist.size() ? st->roadstop_speclist[specindex].spec : nullptr;
}

int AllocateRoadStopSpecToStation(const RoadStopSpec *statspec, BaseStation *st, bool exec)
{
	uint i;

	if (statspec == nullptr || st == nullptr) return 0;

	/* Try to find the same spec and return that one */
	for (i = 1; i < st->roadstop_speclist.size() && i < NUM_ROADSTOPSPECS_PER_STATION; i++) {
		if (st->roadstop_speclist[i].spec == statspec) return i;
	}

	/* Try to find an unused spec slot */
	for (i = 1; i < st->roadstop_speclist.size() && i < NUM_ROADSTOPSPECS_PER_STATION; i++) {
		if (st->roadstop_speclist[i].spec == nullptr && st->roadstop_speclist[i].grfid == 0) break;
	}

	if (i == NUM_ROADSTOPSPECS_PER_STATION) {
		/* Full, give up */
		return -1;
	}

	if (exec) {
		if (i >= st->roadstop_speclist.size()) st->roadstop_speclist.resize(i + 1);
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
	for (const RoadStopTileData &tile_data : st->custom_roadstop_tile_data) {
		if (GetCustomRoadStopSpecIndex(tile_data.tile) == specindex) {
			return;
		}
	}

	/* This specindex is no longer in use, so deallocate it */
	st->roadstop_speclist[specindex].spec     = nullptr;
	st->roadstop_speclist[specindex].grfid    = 0;
	st->roadstop_speclist[specindex].localidx = 0;

	/* If this was the highest spec index, reallocate */
	if (specindex == st->roadstop_speclist.size() - 1) {
		size_t num_specs;
		for (num_specs = st->roadstop_speclist.size() - 1; num_specs > 0; num_specs--) {
			if (st->roadstop_speclist[num_specs].grfid != 0) break;
		}

		if (num_specs > 0) {
			st->roadstop_speclist.resize(num_specs + 1);
		} else {
			st->roadstop_speclist.clear();
			st->cached_roadstop_anim_triggers = 0;
			st->cached_roadstop_cargo_triggers = 0;
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
	st->cached_roadstop_anim_triggers = 0;
	st->cached_roadstop_cargo_triggers = 0;

	/* Combine animation trigger bitmask for all road stop specs
	 * of this station. */
	for (uint i = 0; i < st->roadstop_speclist.size(); i++) {
		const RoadStopSpec *ss = st->roadstop_speclist[i].spec;
		if (ss != nullptr) {
			st->cached_roadstop_anim_triggers |= ss->animation.triggers;
			st->cached_roadstop_cargo_triggers |= ss->cargo_triggers;
		}
	}
}

void DumpRoadStopSpriteGroup(const BaseStation *st, const RoadStopSpec *spec, DumpSpriteGroupPrinter print)
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
