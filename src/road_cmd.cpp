/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_cmd.cpp Commands related to road tiles. */

#include "stdafx.h"
#include "cmd_helper.h"
#include "road.h"
#include "road_internal.h"
#include "viewport_func.h"
#include "command_func.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "depot_base.h"
#include "newgrf.h"
#include "autoslope.h"
#include "tunnelbridge_map.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "cheat_type.h"
#include "effectvehicle_func.h"
#include "effectvehicle_base.h"
#include "elrail_func.h"
#include "roadveh.h"
#include "train.h"
#include "town.h"
#include "company_base.h"
#include "core/random_func.hpp"
#include "core/container_func.hpp"
#include "newgrf_debug.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"
#include "date_func.h"
#include "genworld.h"
#include "company_gui.h"
#include "road_func.h"
#include "roadstop_base.h"
#include "scope.h"

#include "table/strings.h"
#include "table/roadtypes.h"

#include "safeguards.h"

/** Helper type for lists/vectors of road vehicles */
typedef std::vector<RoadVehicle *> RoadVehicleList;

RoadTypeInfo _roadtypes[ROADTYPE_END];
std::vector<RoadType> _sorted_roadtypes;
RoadTypes _roadtypes_hidden_mask;
std::array<RoadTypes, RTCM_END> _collision_mode_roadtypes;
RoadTypes _roadtypes_non_train_colliding;

/**
 * Bitmap of road/tram types.
 * Bit if set if a roadtype is tram.
 */
RoadTypes _roadtypes_type;

/**
 * Reset all road type information to its default values.
 */
void ResetRoadTypes()
{
	static_assert(lengthof(_original_roadtypes) <= lengthof(_roadtypes));

	auto insert = std::copy(std::begin(_original_roadtypes), std::end(_original_roadtypes), std::begin(_roadtypes));
	std::fill(insert, std::end(_roadtypes), RoadTypeInfo{});

	_roadtypes_hidden_mask = ROADTYPES_NONE;
	_roadtypes_type        = ROADTYPES_TRAM;
}

void ResolveRoadTypeGUISprites(RoadTypeInfo *rti)
{
	SpriteID cursors_base = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_CURSORS);
	if (cursors_base != 0) {
		rti->gui_sprites.build_y_road = cursors_base +  0;
		rti->gui_sprites.build_x_road = cursors_base +  1;
		rti->gui_sprites.auto_road    = cursors_base +  2;
		rti->gui_sprites.build_depot  = cursors_base +  3;
		rti->gui_sprites.build_tunnel = cursors_base +  4;
		rti->gui_sprites.convert_road = cursors_base +  5;
		rti->cursor.road_swne         = cursors_base +  6;
		rti->cursor.road_nwse         = cursors_base +  7;
		rti->cursor.autoroad          = cursors_base +  8;
		rti->cursor.depot             = cursors_base +  9;
		rti->cursor.tunnel            = cursors_base + 10;
		rti->cursor.convert_road      = cursors_base + 11;
	}
}

/**
 * Compare roadtypes based on their sorting order.
 * @param first  The roadtype to compare to.
 * @param second The roadtype to compare.
 * @return True iff the first should be sorted before the second.
 */
static bool CompareRoadTypes(const RoadType &first, const RoadType &second)
{
	if (RoadTypeIsRoad(first) == RoadTypeIsRoad(second)) {
		return GetRoadTypeInfo(first)->sorting_order < GetRoadTypeInfo(second)->sorting_order;
	}
	return RoadTypeIsTram(first) < RoadTypeIsTram(second);
}

/**
 * Resolve sprites of custom road types
 */
void InitRoadTypes()
{
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		RoadTypeInfo *rti = &_roadtypes[rt];
		ResolveRoadTypeGUISprites(rti);
		if (HasBit(rti->flags, ROTF_HIDDEN)) SetBit(_roadtypes_hidden_mask, rt);
	}

	_sorted_roadtypes.clear();
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		if (_roadtypes[rt].label != 0 && !HasBit(_roadtypes_hidden_mask, rt)) {
			_sorted_roadtypes.push_back(rt);
		}
	}
	std::sort(_sorted_roadtypes.begin(), _sorted_roadtypes.end(), CompareRoadTypes);
}

void InitRoadTypesCaches()
{
	std::fill(_collision_mode_roadtypes.begin(), _collision_mode_roadtypes.end(), ROADTYPES_NONE);
	_roadtypes_non_train_colliding = ROADTYPES_NONE;

	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		const RoadTypeInfo &rti = _roadtypes[rt];
		SetBit(_collision_mode_roadtypes[rti.collision_mode], rt);
		if (HasBit(rti.extra_flags, RXTF_NO_TRAIN_COLLISION)) SetBit(_roadtypes_non_train_colliding, rt);
	}
}

/**
 * Allocate a new road type label
 */
RoadType AllocateRoadType(RoadTypeLabel label, RoadTramType rtt)
{
	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		RoadTypeInfo *rti = &_roadtypes[rt];

		if (rti->label == 0) {
			/* Set up new road type */
			*rti = _original_roadtypes[(rtt == RTT_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD];
			rti->label = label;
			rti->alternate_labels.clear();
			rti->flags = ROTFB_NONE;
			rti->extra_flags = RXTFB_NONE;
			rti->collision_mode = RTCM_NORMAL;
			rti->introduction_date = CalTime::INVALID_DATE;

			/* Make us compatible with ourself. */
			rti->powered_roadtypes = (RoadTypes)(1ULL << rt);

			/* We also introduce ourself. */
			rti->introduces_roadtypes = (RoadTypes)(1ULL << rt);

			/* Default sort order; order of allocation, but with some
			 * offsets so it's easier for NewGRF to pick a spot without
			 * changing the order of other (original) road types.
			 * The << is so you can place other roadtypes in between the
			 * other roadtypes, the 7 is to be able to place something
			 * before the first (default) road type. */
			rti->sorting_order = rt << 2 | 7;

			/* Set bitmap of road/tram types */
			if (rtt == RTT_TRAM) {
				SetBit(_roadtypes_type, rt);
			} else {
				ClrBit(_roadtypes_type, rt);
			}

			return rt;
		}
	}

	return INVALID_ROADTYPE;
}

/**
 * Verify whether a road vehicle has been built.
 * @return \c true if at least one road vehicle has been built, \c false if not
 */
bool RoadVehiclesAreBuilt()
{
	return !RoadVehicle::Iterate().empty();
}

/**
 * Verify whether a road vehicle has been built and is not in a depot.
 * @return \c true if at least one road vehicle has been built and is not in a depot, \c false if not
 */
bool RoadVehiclesExistOutsideDepots()
{
	for (const RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->IsFrontEngine() && !rv->IsChainInDepot()) return true;
	}
	return false;
}

static DisallowedRoadDirections GetOneWayRoadTileDisallowedRoadDirections(TileIndex tile)
{
	if (IsNormalRoadTile(tile)) return GetDisallowedRoadDirections(tile);
	if (IsDriveThroughStopTile(tile)) return GetDriveThroughStopDisallowedRoadDirections(tile);
	return DRD_NONE;
}

static DiagDirection OneWaySideJunctionRoadRoadBitsToDiagDir(RoadBits bits)
{
	/*
	 * Drive on left missing bit:
	 * ROAD_SE (bit 2) -> DIAGDIR_NE (0)
	 * ROAD_SW (bit 1) -> DIAGDIR_SE (1)
	 * ROAD_NW (bit 0) -> DIAGDIR_SW (2)
	 * ROAD_NE (bit 3) -> DIAGDIR_NW (3)
	 */
	uint8_t bit = FindFirstBit(bits ^ ROAD_ALL);
	bit ^= 3;
	return (DiagDirection)((bit + 3 + (_settings_game.vehicle.road_side * 2)) % 4);
}

inline bool IsOneWaySideJunctionRoadDRDsPresent(TileIndex tile, DiagDirection dir)
{
	const DisallowedRoadDirections diagdir_to_drd[DIAGDIR_END] = { DRD_NORTHBOUND, DRD_NORTHBOUND, DRD_SOUTHBOUND, DRD_SOUTHBOUND };

	TileIndexDiffC ti = TileIndexDiffCByDiagDir(dir);
	TileIndex ahead = AddTileIndexDiffCWrap(tile, ti);
	if (ahead == INVALID_TILE || GetOneWayRoadTileDisallowedRoadDirections(ahead) != diagdir_to_drd[dir]) return false;
	TileIndex behind = AddTileIndexDiffCWrap(tile, { (int16_t)(-ti.x), (int16_t)(-ti.y) });
	if (behind == INVALID_TILE || GetOneWayRoadTileDisallowedRoadDirections(behind) != diagdir_to_drd[dir]) return false;
	return true;
}

inline bool IsRoadCachedOneWayStateInterpolatableTile(TileIndex tile)
{
	return !IsTileType(tile, MP_STATION) || IsRoadWaypointTile(tile);
}

static btree::btree_set<TileIndex> _road_cache_one_way_state_pending_tiles;
static btree::btree_set<TileIndex> _road_cache_one_way_state_pending_interpolate_tiles;
static bool _defer_update_road_cache_one_way_state = false;
bool _mark_tile_dirty_on_road_cache_one_way_state_update = false;

static void RefreshTileOnCachedOneWayStateChange(TileIndex tile)
{
	if (IsAnyRoadStopTile(tile) && IsCustomRoadStopSpecIndex(tile)) {
		MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		return;
	}
	if (unlikely(_mark_tile_dirty_on_road_cache_one_way_state_update)) {
		MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		return;
	}
}

static void UpdateTileRoadCachedOneWayState(TileIndex tile)
{
	RefreshTileOnCachedOneWayStateChange(tile);

	DisallowedRoadDirections drd = GetOneWayRoadTileDisallowedRoadDirections(tile);
	if (drd != DRD_NONE) {
		SetRoadCachedOneWayState(tile, (RoadCachedOneWayState)drd);
		return;
	}
	if (IsNormalRoadTile(tile)) {
		RoadBits bits = GetRoadBits(tile, RTT_ROAD);
		if (HasExactlyOneBit(bits ^ ROAD_ALL)) {
			DiagDirection dir = OneWaySideJunctionRoadRoadBitsToDiagDir(bits);
			if (IsOneWaySideJunctionRoadDRDsPresent(tile, dir)) {
				DiagDirection side_dir = (DiagDirection)((dir + 3 + (_settings_game.vehicle.road_side * 2)) % 4);
				TileIndexDiffC ti = TileIndexDiffCByDiagDir(side_dir);
				TileIndex side = AddTileIndexDiffCWrap(tile, ti);

				const DisallowedRoadDirections diagdir_to_drd[DIAGDIR_END] = { DRD_SOUTHBOUND, DRD_SOUTHBOUND, DRD_NORTHBOUND, DRD_NORTHBOUND };
				SetRoadCachedOneWayState(tile, (GetOneWayRoadTileDisallowedRoadDirections(side) & diagdir_to_drd[side_dir]) ? RCOWS_SIDE_JUNCTION_NO_EXIT : RCOWS_SIDE_JUNCTION);
				return;
			}
		}
	}
	if (IsRoadCachedOneWayStateInterpolatableTile(tile)) _road_cache_one_way_state_pending_interpolate_tiles.insert(tile);
	SetRoadCachedOneWayState(tile, RCOWS_NORMAL);
}

/* Do not re-order, see: RoadCachedOneWayState */
enum InterpolateRoadResult {
	IRR_NONE,
	IRR_OUT,
	IRR_IN
};

static TileIndex InterpolateRoadFollowTileStep(TileIndex tile, uint8_t bit)
{
	DiagDirection outgoing = (DiagDirection)(bit ^ 3);
	if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(tile) == outgoing) {
		return GetOtherTunnelBridgeEnd(tile);
	}
	TileIndexDiffC ti = TileIndexDiffCByDiagDir(outgoing);
	TileIndex next = AddTileIndexDiffCWrap(tile, ti);
	if (next == INVALID_TILE) return INVALID_TILE;
	if (IsTileType(next, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(next) == ReverseDiagDir(outgoing)) {
		return INVALID_TILE;
	}
	return next;
}

static InterpolateRoadResult InterpolateRoadFollowRoadBit(TileIndex tile, uint8_t bit)
{
	const TileIndex start = tile;
	do {
		TileIndex next = InterpolateRoadFollowTileStep(tile, bit);
		if (next == INVALID_TILE) return IRR_NONE;
		DisallowedRoadDirections drd = GetOneWayRoadTileDisallowedRoadDirections(next);
		if (drd == DRD_BOTH) return IRR_NONE;
		if (drd != DRD_NONE) {
			const DisallowedRoadDirections outgoing_drd_by_exit_bit[4] = { DRD_SOUTHBOUND, DRD_SOUTHBOUND, DRD_NORTHBOUND, DRD_NORTHBOUND };
			return outgoing_drd_by_exit_bit[bit] == drd ? IRR_OUT : IRR_IN;
		}
		if (!IsRoadCachedOneWayStateInterpolatableTile(next)) return IRR_NONE;
		RoadBits incoming = (RoadBits)(1 << (bit ^ 2));
		RoadBits rb = GetAnyRoadBits(next, RTT_ROAD, true);
		if ((incoming & rb) == 0) return IRR_NONE;
		RoadBits remaining = rb & ~incoming;
		if (!HasExactlyOneBit(remaining)) return IRR_NONE;
		tile = next;
		bit = FindFirstBit(remaining);
	} while (tile != start);
	return IRR_NONE;
}

static void InterpolateRoadFollowRoadBitSetState(TileIndex tile, uint8_t bit, InterpolateRoadResult irr)
{
	const TileIndex start = tile;
	do {
		if (irr == IRR_NONE) {
			SetRoadCachedOneWayState(tile, RCOWS_NORMAL);
		} else {
			uint8_t inbit = FindFirstBit(GetAnyRoadBits(tile, RTT_ROAD, true) & ~static_cast<RoadBits>(1 << bit));
			/*   inbit    bit      piece    Outgoing Trackdir       IRR_IN case
			 *
			 *    0        1       ROAD_W   TRACKDIR_LEFT_S         RCOWS_NON_JUNCTION_A
			 *    0        2       ROAD_Y   TRACKDIR_Y_SE           RCOWS_NON_JUNCTION_A
			 *    0        3       ROAD_N   TRACKDIR_UPPER_E        RCOWS_NON_JUNCTION_A
			 *
			 *    1        0       ROAD_W   TRACKDIR_LEFT_N         RCOWS_NON_JUNCTION_B
			 *    1        2       ROAD_S   TRACKDIR_LOWER_E        RCOWS_NON_JUNCTION_A
			 *    1        3       ROAD_X   TRACKDIR_X_NE           RCOWS_NON_JUNCTION_A
			 *
			 *    2        0       ROAD_Y   TRACKDIR_Y_NW           RCOWS_NON_JUNCTION_B
			 *    2        1       ROAD_S   TRACKDIR_LOWER_W        RCOWS_NON_JUNCTION_B
			 *    2        3       ROAD_E   TRACKDIR_RIGHT_N        RCOWS_NON_JUNCTION_B
			 *
			 *    3        0       ROAD_N   TRACKDIR_UPPER_W        RCOWS_NON_JUNCTION_B
			 *    3        1       ROAD_X   TRACKDIR_X_SW           RCOWS_NON_JUNCTION_B
			 *    3        2       ROAD_E   TRACKDIR_RIGHT_S        RCOWS_NON_JUNCTION_A
			 */

			const uint16_t bits_to_rcows = 0x3B10;
			SetRoadCachedOneWayState(tile, (RoadCachedOneWayState)(irr ^ (HasBit(bits_to_rcows, (inbit << 2) | bit) ? 0 : 3)));
		}
		_road_cache_one_way_state_pending_interpolate_tiles.erase(tile);
		RefreshTileOnCachedOneWayStateChange(tile);
		TileIndex next = InterpolateRoadFollowTileStep(tile, bit);
		if (next == INVALID_TILE) return;
		DisallowedRoadDirections drd = GetOneWayRoadTileDisallowedRoadDirections(next);
		if (drd != DRD_NONE) {
			return;
		}
		if (!IsRoadCachedOneWayStateInterpolatableTile(next)) return;
		RoadBits incoming = (RoadBits)(1 << (bit ^ 2));
		RoadBits rb = GetAnyRoadBits(next, RTT_ROAD, true);
		if ((incoming & rb) == 0) return;
		RoadBits remaining = rb & ~incoming;
		if (!HasExactlyOneBit(remaining)) return;
		tile = next;
		bit = FindFirstBit(remaining);
	} while (tile != start);
}

static void InterpolateRoadCachedOneWayStates()
{
	while (!_road_cache_one_way_state_pending_interpolate_tiles.empty()) {
		auto iter = _road_cache_one_way_state_pending_interpolate_tiles.begin();
		TileIndex tile = *iter;
		_road_cache_one_way_state_pending_interpolate_tiles.erase(iter);

		const RoadBits bits = GetAnyRoadBits(tile, RTT_ROAD, true);
		if (CountBits(bits) != 2) continue;

		uint8_t first_bit = FindFirstBit(bits);
		uint8_t second_bit = FindFirstBit(KillFirstBit(bits));
		InterpolateRoadResult first_irr = InterpolateRoadFollowRoadBit(tile, first_bit);
		InterpolateRoadResult second_irr = first_irr;
		if (first_irr != IRR_NONE) {
			second_irr = InterpolateRoadFollowRoadBit(tile, second_bit);
			if (second_irr == IRR_NONE || second_irr == first_irr) first_irr = second_irr = IRR_NONE;
		}
		InterpolateRoadFollowRoadBitSetState(tile, first_bit, first_irr);
		InterpolateRoadFollowRoadBitSetState(tile, second_bit, second_irr);
	}
}

void RecalculateRoadCachedOneWayStates()
{
	for (TileIndex tile = 0; tile != MapSize(); tile++) {
		if (MayHaveRoad(tile)) UpdateTileRoadCachedOneWayState(tile);
	}
	InterpolateRoadCachedOneWayStates();
}

void UpdateRoadCachedOneWayStatesAroundTile(TileIndex tile)
{
	if (_generating_world) return;

	auto check_tile = [](TileIndex t) {
		if (_defer_update_road_cache_one_way_state) {
			_road_cache_one_way_state_pending_tiles.insert(t);
		} else if (MayHaveRoad(t)) {
			UpdateTileRoadCachedOneWayState(t);
		}
	};
	check_tile(tile);
	uint x_offset = TileXY(1, 0);
	if (tile >= x_offset) check_tile(tile - x_offset);
	if (tile + x_offset < MapSize()) check_tile(tile + x_offset);
	uint y_offset = TileXY(0, 1);
	if (tile >= y_offset) check_tile(tile - y_offset);
	if (tile + y_offset < MapSize()) check_tile(tile + y_offset);
	if (!_defer_update_road_cache_one_way_state) InterpolateRoadCachedOneWayStates();
}

void FlushDeferredUpdateRoadCachedOneWayStates()
{
	_defer_update_road_cache_one_way_state = false;
	for (TileIndex t : _road_cache_one_way_state_pending_tiles) {
		if (MayHaveRoad(t)) UpdateTileRoadCachedOneWayState(t);
	}
	_road_cache_one_way_state_pending_tiles.clear();
	InterpolateRoadCachedOneWayStates();
}

/**
 * Update road infrastructure counts for a company.
 * @param rt Road type to update count of.
 * @param o Owner of road piece.
 * @param count Number of road pieces to adjust.
 */
void UpdateCompanyRoadInfrastructure(RoadType rt, Owner o, int count)
{
	if (rt == INVALID_ROADTYPE) return;

	Company *c = Company::GetIfValid(o);
	if (c == nullptr) return;

	c->infrastructure.road[rt] += count;
	DirtyCompanyInfrastructureWindows(c->index);
}

/** Invalid RoadBits on slopes.  */
extern const RoadBits _invalid_tileh_slopes_road[2][15] = {
	/* The inverse of the mixable RoadBits on a leveled slope */
	{
		ROAD_NONE,         // SLOPE_FLAT
		ROAD_NE | ROAD_SE, // SLOPE_W
		ROAD_NE | ROAD_NW, // SLOPE_S

		ROAD_NE,           // SLOPE_SW
		ROAD_NW | ROAD_SW, // SLOPE_E
		ROAD_NONE,         // SLOPE_EW

		ROAD_NW,           // SLOPE_SE
		ROAD_NONE,         // SLOPE_WSE
		ROAD_SE | ROAD_SW, // SLOPE_N

		ROAD_SE,           // SLOPE_NW
		ROAD_NONE,         // SLOPE_NS
		ROAD_NONE,         // SLOPE_ENW

		ROAD_SW,           // SLOPE_NE
		ROAD_NONE,         // SLOPE_SEN
		ROAD_NONE          // SLOPE_NWS
	},
	/* The inverse of the allowed straight roads on a slope
	 * (with and without a foundation). */
	{
		ROAD_NONE, // SLOPE_FLAT
		ROAD_NONE, // SLOPE_W    Foundation
		ROAD_NONE, // SLOPE_S    Foundation

		ROAD_Y,    // SLOPE_SW
		ROAD_NONE, // SLOPE_E    Foundation
		ROAD_ALL,  // SLOPE_EW

		ROAD_X,    // SLOPE_SE
		ROAD_ALL,  // SLOPE_WSE
		ROAD_NONE, // SLOPE_N    Foundation

		ROAD_X,    // SLOPE_NW
		ROAD_ALL,  // SLOPE_NS
		ROAD_ALL,  // SLOPE_ENW

		ROAD_Y,    // SLOPE_NE
		ROAD_ALL,  // SLOPE_SEN
		ROAD_ALL   // SLOPE_NW
	}
};

Foundation GetRoadFoundation(Slope tileh, RoadBits bits);

void NotifyRoadLayoutChangedIfTileNonLeaf(TileIndex tile, RoadTramType rtt, RoadBits present_bits)
{
	uint connections = 0;
	if ((present_bits & ROAD_NE) && (GetAnyRoadBits(TILE_ADDXY(tile, -1,  0), rtt) & ROAD_SW)) connections++;
	if ((present_bits & ROAD_SE) && (GetAnyRoadBits(TILE_ADDXY(tile,  0,  1), rtt) & ROAD_NW)) connections++;
	if ((present_bits & ROAD_SW) && (GetAnyRoadBits(TILE_ADDXY(tile,  1,  0), rtt) & ROAD_NE)) connections++;
	if ((present_bits & ROAD_NW) && (GetAnyRoadBits(TILE_ADDXY(tile,  0, -1), rtt) & ROAD_SE)) connections++;
	if (connections >= 2) {
		NotifyRoadLayoutChanged();
	}
}

void NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(TileIndex start, TileIndex end, DiagDirection start_dir, RoadTramType rtt)
{
	if (!(GetAnyRoadBits(TileAddByDiagDir(start, ReverseDiagDir(start_dir)), rtt) & DiagDirToRoadBits(start_dir))) return;
	if (!(GetAnyRoadBits(TileAddByDiagDir(end, start_dir), rtt) & DiagDirToRoadBits(ReverseDiagDir(start_dir)))) return;

	NotifyRoadLayoutChanged();
}

/**
 * Is it allowed to remove the given road bits from the given tile?
 * @param tile      the tile to remove the road from
 * @param remove    the roadbits that are going to be removed
 * @param owner     the actual owner of the roadbits of the tile
 * @param rt        the road type to remove the bits from
 * @param flags     command flags
 * @param town_check Shall the town rating checked/affected
 * @return A succeeded command when it is allowed to remove the road bits, a failed command otherwise.
 */
CommandCost CheckAllowRemoveRoad(TileIndex tile, RoadBits remove, Owner owner, RoadTramType rtt, DoCommandFlag flags, bool town_check)
{
	if (_game_mode == GM_EDITOR || remove == ROAD_NONE) return CommandCost();

	/* Water can always flood and towns can always remove "normal" road pieces.
	 * Towns are not be allowed to remove non "normal" road pieces, like tram
	 * tracks as that would result in trams that cannot turn. */
	if (_current_company == OWNER_WATER ||
			(rtt == RTT_ROAD && !Company::IsValidID(_current_company))) return CommandCost();

	/* Only do the special processing if the road is owned
	 * by a town */
	if (owner != OWNER_TOWN) {
		if (owner == OWNER_NONE) return CommandCost();
		CommandCost ret = CheckOwnership(owner);
		return ret;
	}

	if (!town_check) return CommandCost();

	if (_cheats.magic_bulldozer.value) return CommandCost();

	Town *t = ClosestTownFromTile(tile, UINT_MAX);
	if (t == nullptr) return CommandCost();

	/* check if you're allowed to remove the street owned by a town
	 * removal allowance depends on difficulty setting */
	CommandCost ret = CheckforTownRating(flags, t, ROAD_REMOVE);
	if (ret.Failed()) return ret;

	/* Get a bitmask of which neighbouring roads has a tile */
	RoadBits n = ROAD_NONE;
	RoadBits present = GetAnyRoadBits(tile, rtt);
	if ((present & ROAD_NE) && (GetAnyRoadBits(TILE_ADDXY(tile, -1,  0), rtt) & ROAD_SW)) n |= ROAD_NE;
	if ((present & ROAD_SE) && (GetAnyRoadBits(TILE_ADDXY(tile,  0,  1), rtt) & ROAD_NW)) n |= ROAD_SE;
	if ((present & ROAD_SW) && (GetAnyRoadBits(TILE_ADDXY(tile,  1,  0), rtt) & ROAD_NE)) n |= ROAD_SW;
	if ((present & ROAD_NW) && (GetAnyRoadBits(TILE_ADDXY(tile,  0, -1), rtt) & ROAD_SE)) n |= ROAD_NW;

	int rating_decrease = RATING_ROAD_DOWN_STEP_EDGE;
	/* If 0 or 1 bits are set in n, or if no bits that match the bits to remove,
	 * then allow it */
	if (KillFirstBit(n) != ROAD_NONE && (n & remove) != ROAD_NONE) {
		/* you can remove all kind of roads with extra dynamite */
		if (!_settings_game.construction.extra_dynamite) {
			SetDParam(0, t->index);
			return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
		}
		rating_decrease = RATING_ROAD_DOWN_STEP_INNER;
	}
	ChangeTownRating(t, rating_decrease, RATING_ROAD_MINIMUM, flags);

	return CommandCost();
}


/**
 * Delete a piece of road.
 * @param tile tile where to remove road from
 * @param flags operation to perform
 * @param pieces roadbits to remove
 * @param rt roadtype to remove
 * @param town_check should we check if the town allows removal?
 */
static CommandCost RemoveRoad(TileIndex tile, DoCommandFlag flags, RoadBits pieces, RoadTramType rtt, bool town_check)
{
	assert(pieces != ROAD_NONE);

	RoadType existing_rt = MayHaveRoad(tile) ? GetRoadType(tile, rtt) : INVALID_ROADTYPE;
	/* The tile doesn't have the given road type */
	if (existing_rt == INVALID_ROADTYPE) return_cmd_error((rtt == RTT_TRAM) ? STR_ERROR_THERE_IS_NO_TRAMWAY : STR_ERROR_THERE_IS_NO_ROAD);

	switch (GetTileType(tile)) {
		case MP_ROAD: {
			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;
			break;
		}

		case MP_STATION: {
			if (!IsDriveThroughStopTile(tile)) return CMD_ERROR;

			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;
			break;
		}

		case MP_TUNNELBRIDGE: {
			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_ROAD) return CMD_ERROR;
			CommandCost ret = TunnelBridgeIsFree(tile, GetOtherTunnelBridgeEnd(tile));
			if (ret.Failed()) return ret;
			break;
		}

		default:
			return CMD_ERROR;
	}

	CommandCost ret = CheckAllowRemoveRoad(tile, pieces, GetRoadOwner(tile, rtt), rtt, flags, town_check);
	if (ret.Failed()) return ret;

	if (!IsTileType(tile, MP_ROAD)) {
		const bool custom_bridge_head = IsBridgeTile(tile) &&
				HasBridgeFlatRamp(GetTileSlope(tile), DiagDirToAxis(GetTunnelBridgeDirection(tile))) &&
				(_settings_game.construction.road_custom_bridge_heads || IsRoadCustomBridgeHead(tile));

		/* If it's the last roadtype, just clear the whole tile */
		if (!custom_bridge_head && GetRoadType(tile, OtherRoadTramType(rtt)) == INVALID_ROADTYPE) return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);

		CommandCost cost(EXPENSES_CONSTRUCTION);
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			const RoadBits entrance_piece = DiagDirToRoadBits(GetTunnelBridgeDirection(tile));
			const RoadBits axial_pieces = AxisToRoadBits(DiagDirToAxis(GetTunnelBridgeDirection(tile)));
			const RoadBits existing = IsBridge(tile) ? GetCustomBridgeHeadRoadBits(tile, rtt) : axial_pieces;

			/* handle case where we would otherwise leave a single bridge entrance piece */
			if ((existing & ~pieces) == entrance_piece) {
				pieces |= entrance_piece;
			}

			/* Removing any roadbit in the bridge axis removes the roadtype (that's the behaviour remove-long-roads needs) */
			if ((existing & pieces) == ROAD_NONE) return_cmd_error((rtt == RTT_TRAM) ? STR_ERROR_THERE_IS_NO_TRAMWAY : STR_ERROR_THERE_IS_NO_ROAD);

			if (!custom_bridge_head) pieces |= axial_pieces;

			const TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
			const uint middle_len = GetTunnelBridgeLength(other_end, tile);
			uint pieces_count = 0;

			const RoadBits other_end_existing = IsBridge(other_end) ? GetCustomBridgeHeadRoadBits(other_end, rtt) : axial_pieces;
			RoadBits other_end_pieces = ROAD_NONE;
			if (pieces & entrance_piece) {
				other_end_pieces |= MirrorRoadBits(entrance_piece);
				/* if removing the other end entrance would only leave one piece, remove that too */
				if (CountBits(other_end_existing & ~other_end_pieces) == 1) {
					other_end_pieces |= other_end_existing;
				}
				pieces_count += middle_len * 2;
				if (custom_bridge_head && ((GetCustomBridgeHeadRoadBits(tile, OtherRoadTramType(rtt)) & entrance_piece) == ROAD_NONE)) {
					/* can't leave no entrance pieces for any road type */
					return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
				}
			}
			pieces_count += CountBits(pieces & existing);
			pieces_count += CountBits(other_end_pieces & other_end_existing);

			cost.AddCost(pieces_count * RoadClearCost(existing_rt));
			if (flags & DC_EXEC) {
				SubtractRoadTunnelBridgeInfrastructure(tile, other_end);

				const RoadBits bits = existing & ~pieces;
				const RoadBits other_bits = other_end_existing & ~other_end_pieces;

				if (bits == ROAD_NONE) SetRoadType(tile, rtt, INVALID_ROADTYPE);
				if (other_bits == ROAD_NONE) SetRoadType(other_end, rtt, INVALID_ROADTYPE);

				if (IsBridge(tile)) {
					SetCustomBridgeHeadRoadBits(tile, rtt, bits);
					SetCustomBridgeHeadRoadBits(other_end, rtt, other_bits);
				}

				if (bits == ROAD_NONE && other_bits == ROAD_NONE) {
					/* If the owner of the bridge sells all its road, also move the ownership
					 * to the owner of the other roadtype, unless the bridge owner is a town. */

					Owner other_owner = GetRoadOwner(tile, OtherRoadTramType(rtt));
					if (!IsTileOwner(tile, other_owner) && !IsTileOwner(tile, OWNER_TOWN)) {
						SetTileOwner(tile, other_owner);
						SetTileOwner(other_end, other_owner);
					}
				}

				/* Mark tiles dirty that have been repaved */
				if (IsBridge(tile)) {
					MarkBridgeDirty(tile);
				} else {
					MarkTileDirtyByTile(tile);
					MarkTileDirtyByTile(other_end);
				}

				AddRoadTunnelBridgeInfrastructure(tile, other_end);
				DirtyAllCompanyInfrastructureWindows();

				/* Todo: Change this to be more fine-grained if necessary */
				NotifyRoadLayoutChanged(false);
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(tile);
					UpdateRoadCachedOneWayStatesAroundTile(other_end);
				}
			}
		} else {
			assert_tile(IsDriveThroughStopTile(tile), tile);
			cost.AddCost(RoadClearCost(existing_rt) * 2);
			if (flags & DC_EXEC) {
				/* A full diagonal road tile has two road bits. */
				UpdateCompanyRoadInfrastructure(existing_rt, GetRoadOwner(tile, rtt), -2);
				if (rtt == RTT_ROAD) {
					SetDriveThroughStopDisallowedRoadDirections(tile, DRD_NONE);
				}
				SetRoadType(tile, rtt, INVALID_ROADTYPE);
				MarkTileDirtyByTile(tile);
				NotifyRoadLayoutChanged(false);
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(tile);
				}
			}
		}
		return cost;
	}

	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_NORMAL: {
			Slope tileh = GetTileSlope(tile);

			/* Steep slopes behave the same as slopes with one corner raised. */
			if (IsSteepSlope(tileh)) {
				tileh = SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh));
			}

			RoadBits present = GetRoadBits(tile, rtt);
			const RoadBits other = GetRoadBits(tile, OtherRoadTramType(rtt));
			const Foundation f = GetRoadFoundation(tileh, present);

			if (HasRoadWorks(tile) && _current_company != OWNER_WATER) return_cmd_error(STR_ERROR_ROAD_WORKS_IN_PROGRESS);

			/* Autocomplete to a straight road
			 * @li if the bits of the other roadtypes result in another foundation
			 * @li if build on slopes is disabled */
			if ((IsStraightRoad(other) && (other & _invalid_tileh_slopes_road[0][tileh & SLOPE_ELEVATED]) != ROAD_NONE) ||
					(tileh != SLOPE_FLAT && !_settings_game.construction.build_on_slopes)) {
				pieces |= MirrorRoadBits(pieces);
			}

			/* limit the bits to delete to the existing bits. */
			pieces &= present;
			if (pieces == ROAD_NONE) return_cmd_error((rtt == RTT_TRAM) ? STR_ERROR_THERE_IS_NO_TRAMWAY : STR_ERROR_THERE_IS_NO_ROAD);

			/* Now set present what it will be after the remove */
			present ^= pieces;

			/* Check for invalid RoadBit combinations on slopes */
			if (tileh != SLOPE_FLAT && present != ROAD_NONE &&
					(present & _invalid_tileh_slopes_road[0][tileh & SLOPE_ELEVATED]) == present) {
				return CMD_ERROR;
			}

			if (flags & DC_EXEC) {
				if (HasRoadWorks(tile)) {
					/* flooding tile with road works, don't forget to remove the effect vehicle too */
					assert(_current_company == OWNER_WATER);
					for (EffectVehicle *v : EffectVehicle::Iterate()) {
						if (TileVirtXY(v->x_pos, v->y_pos) == tile) {
							delete v;
						}
					}
				}

				if (RoadLayoutChangeNotificationEnabled(false)) NotifyRoadLayoutChangedIfTileNonLeaf(tile, rtt, present | pieces);
				UpdateCompanyRoadInfrastructure(existing_rt, GetRoadOwner(tile, rtt), -(int)CountBits(pieces));

				if (present == ROAD_NONE) {
					/* No other road type, just clear tile. */
					if (GetRoadType(tile, OtherRoadTramType(rtt)) == INVALID_ROADTYPE) {
						/* Includes MarkTileDirtyByTile() */
						DoClearSquare(tile);
						DeleteNewGRFInspectWindow(GSF_ROADTYPES, tile);
					} else {
						if (rtt == RTT_ROAD && IsRoadOwner(tile, rtt, OWNER_TOWN)) {
							/* Update nearest-town index */
							const Town *town = CalcClosestTownFromTile(tile);
							SetTownIndex(tile, town == nullptr ? INVALID_TOWN : town->index);
						}
						if (rtt == RTT_ROAD) SetDisallowedRoadDirections(tile, DRD_NONE);
						SetRoadBits(tile, ROAD_NONE, rtt);
						SetRoadType(tile, rtt, INVALID_ROADTYPE);
						MarkTileDirtyByTile(tile);
					}
				} else {
					/* When bits are removed, you *always* end up with something that
					 * is not a complete straight road tile. However, trams do not have
					 * onewayness, so they cannot remove it either. */
					if (rtt == RTT_ROAD) SetDisallowedRoadDirections(tile, DRD_NONE);
					SetRoadBits(tile, present, rtt);
					MarkTileDirtyByTile(tile);
				}
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(tile);
				}
			}

			CommandCost cost(EXPENSES_CONSTRUCTION, CountBits(pieces) * RoadClearCost(existing_rt));
			/* If we build a foundation we have to pay for it. */
			if (f == FOUNDATION_NONE && GetRoadFoundation(tileh, present) != FOUNDATION_NONE) cost.AddCost(_price[PR_BUILD_FOUNDATION]);

			return cost;
		}

		case ROAD_TILE_CROSSING: {
			if (pieces & ComplementRoadBits(GetCrossingRoadBits(tile))) {
				return CMD_ERROR;
			}

			if (flags & DC_EXEC) {
				UpdateAdjacentLevelCrossingTilesOnRemove(tile, GetCrossingRoadAxis(tile));

				/* A full diagonal road tile has two road bits. */
				UpdateCompanyRoadInfrastructure(existing_rt, GetRoadOwner(tile, rtt), -2);

				Track railtrack = GetCrossingRailTrack(tile);
				if (RoadLayoutChangeNotificationEnabled(false)) NotifyRoadLayoutChangedIfTileNonLeaf(tile, rtt, GetCrossingRoadBits(tile));
				if (GetRoadType(tile, OtherRoadTramType(rtt)) == INVALID_ROADTYPE) {
					TrackBits tracks = GetCrossingRailBits(tile);
					bool reserved = HasCrossingReservation(tile);
					MakeRailNormal(tile, GetTileOwner(tile), tracks, GetRailType(tile));
					if (reserved) SetTrackReservation(tile, tracks);

					/* Update rail count for level crossings. The plain track should still be accounted
					 * for, so only subtract the difference to the level crossing cost. */
					Company *c = Company::GetIfValid(GetTileOwner(tile));
					if (c != nullptr) {
						c->infrastructure.rail[GetRailType(tile)] -= LEVELCROSSING_TRACKBIT_FACTOR - 1;
						DirtyCompanyInfrastructureWindows(c->index);
					}

					if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
						AddTrackToSignalBuffer(tile, railtrack, GetTileOwner(tile));
						UpdateSignalsInBuffer();
					}

					DeleteNewGRFInspectWindow(GSF_ROADTYPES, tile);
				} else {
					SetRoadType(tile, rtt, INVALID_ROADTYPE);
				}
				MarkTileDirtyByTile(tile);
				YapfNotifyTrackLayoutChange(tile, railtrack);
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(tile);
				}
			}
			return CommandCost(EXPENSES_CONSTRUCTION, RoadClearCost(existing_rt) * 2);
		}

		default:
		case ROAD_TILE_DEPOT:
			return CMD_ERROR;
	}
}


/**
 * Calculate the costs for roads on slopes
 *  Aside modify the RoadBits to fit on the slopes
 *
 * @note The RoadBits are modified too!
 * @param tileh The current slope
 * @param pieces The RoadBits we want to add
 * @param existing The existent RoadBits of the current type
 * @param other The other existent RoadBits
 * @return The costs for these RoadBits on this slope
 */
static CommandCost CheckRoadSlope(Slope tileh, RoadBits *pieces, RoadBits existing, RoadBits other)
{
	/* Remove already build pieces */
	CLRBITS(*pieces, existing);

	/* If we can't build anything stop here */
	if (*pieces == ROAD_NONE) return CMD_ERROR;

	/* All RoadBit combos are valid on flat land */
	if (tileh == SLOPE_FLAT) return CommandCost();

	/* Steep slopes behave the same as slopes with one corner raised. */
	if (IsSteepSlope(tileh)) {
		tileh = SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh));
	}

	/* Save the merge of all bits of the current type */
	RoadBits type_bits = existing | *pieces;

	/* Roads on slopes */
	if (_settings_game.construction.build_on_slopes && (_invalid_tileh_slopes_road[0][tileh] & (other | type_bits)) == ROAD_NONE) {

		/* If we add leveling we've got to pay for it */
		if ((other | existing) == ROAD_NONE) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);

		return CommandCost();
	}

	/* Autocomplete uphill roads */
	*pieces |= MirrorRoadBits(*pieces);
	type_bits = existing | *pieces;

	/* Uphill roads */
	if (IsStraightRoad(type_bits) && (other == type_bits || other == ROAD_NONE) &&
			(_invalid_tileh_slopes_road[1][tileh] & (other | type_bits)) == ROAD_NONE) {

		/* Slopes with foundation ? */
		if (IsSlopeWithOneCornerRaised(tileh)) {

			/* Prevent build on slopes if it isn't allowed */
			if (_settings_game.construction.build_on_slopes) {

				/* If we add foundation we've got to pay for it */
				if ((other | existing) == ROAD_NONE) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);

				return CommandCost();
			}
		} else {
			if (HasExactlyOneBit(existing) && GetRoadFoundation(tileh, existing) == FOUNDATION_NONE) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
			return CommandCost();
		}
	}
	return CMD_ERROR;
}

/**
 * Checks the tile and returns whether the current player is allowed to convert the roadtype to another roadtype without taking ownership
 * @param owner the tile owner.
 * @param rtt Road/tram type.
 * @return whether the road is convertible
 */
static bool CanConvertUnownedRoadType(Owner owner, RoadTramType rtt)
{
	return (owner == OWNER_NONE || (owner == OWNER_TOWN && rtt == RTT_ROAD));
}

/**
 * Build a piece of road.
 * @param tile tile where to build road
 * @param flags operation to perform
 * @param p1 bit 0..3 road pieces to build (RoadBits)
 *           bit 4..9 road type
 *           bit 11..12 disallowed directions to toggle
 *           bit 13   disable custom bridge heads
 * @param p2 the town that is building the road (0 if not applicable)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildRoad(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	CompanyID company = _current_company;
	CommandCost cost(EXPENSES_CONSTRUCTION);

	RoadBits existing = ROAD_NONE;
	RoadBits other_bits = ROAD_NONE;

	/* Road pieces are max 4 bitset values (NE, NW, SE, SW) and town can only be non-zero
	 * if a non-company is building the road */
	if ((Company::IsValidID(company) && p2 != 0) || (company == OWNER_TOWN && !Town::IsValidID(p2)) || (company == OWNER_DEITY && p2 != 0)) return CMD_ERROR;
	if (company != OWNER_TOWN) {
		const Town *town = CalcClosestTownFromTile(tile);
		p2 = (town != nullptr) ? town->index : INVALID_TOWN;

		if (company == OWNER_DEITY) {
			company = OWNER_TOWN;

			/* If we are not within a town, we are not owned by the town */
			if (town == nullptr || DistanceSquare(tile, town->xy) > town->cache.squared_town_zone_radius[HZB_TOWN_EDGE]) {
				company = OWNER_NONE;
			}
		}
	}

	RoadBits pieces = Extract<RoadBits, 0, 4>(p1);

	/* do not allow building 'zero' road bits, code wouldn't handle it */
	if (pieces == ROAD_NONE) return CMD_ERROR;

	RoadType rt = Extract<RoadType, 4, 6>(p1);
	if (!ValParamRoadType(rt)) return CMD_ERROR;

	DisallowedRoadDirections toggle_drd = Extract<DisallowedRoadDirections, 11, 2>(p1);

	bool disable_custom_bridge_heads = HasBit(p1, 13);

	if ((flags & DC_TOWN) && !MayTownModifyRoad(tile)) return CMD_ERROR;

	Slope tileh = GetTileSlope(tile);
	RoadTramType rtt = GetRoadTramType(rt);

	bool need_to_clear = false;
	switch (GetTileType(tile)) {
		case MP_ROAD:
			switch (GetRoadTileType(tile)) {
				case ROAD_TILE_NORMAL: {
					if (HasRoadWorks(tile)) return_cmd_error(STR_ERROR_ROAD_WORKS_IN_PROGRESS);

					other_bits = GetRoadBits(tile, OtherRoadTramType(rtt));
					if (!HasTileRoadType(tile, rtt)) break;

					existing = GetRoadBits(tile, rtt);
					bool crossing = !IsStraightRoad(existing | pieces);
					if (rtt == RTT_ROAD && (GetDisallowedRoadDirections(tile) != DRD_NONE || toggle_drd != DRD_NONE) && crossing) {
						/* Junctions cannot be one-way */
						return_cmd_error(STR_ERROR_ONEWAY_ROADS_CAN_T_HAVE_JUNCTION);
					}
					if ((existing & pieces) == pieces) {
						/* We only want to set the (dis)allowed road directions */
						if (toggle_drd != DRD_NONE && rtt == RTT_ROAD) {
							if (crossing) return_cmd_error(STR_ERROR_ONEWAY_ROADS_CAN_T_HAVE_JUNCTION);

							Owner owner = GetRoadOwner(tile, rtt);
							if (owner != OWNER_NONE) {
								CommandCost ret = CheckOwnership(owner, tile);
								if (ret.Failed()) return ret;
							}

							DisallowedRoadDirections dis_existing = GetDisallowedRoadDirections(tile);
							DisallowedRoadDirections dis_new      = dis_existing ^ toggle_drd;

							/* We allow removing disallowed directions to break up
							 * deadlocks, but adding them can break articulated
							 * vehicles. As such, only when less is disallowed,
							 * i.e. bits are removed, we skip the vehicle check. */
							if (CountBits(dis_existing) <= CountBits(dis_new)) {
								CommandCost ret = EnsureNoVehicleOnGround(tile);
								if (ret.Failed()) return ret;
							}

							/* Ignore half built tiles */
							if ((flags & DC_EXEC) && IsStraightRoad(existing)) {
								SetDisallowedRoadDirections(tile, dis_new);
								MarkTileDirtyByTile(tile);
								NotifyRoadLayoutChanged(CountBits(dis_existing) > CountBits(dis_new));
								UpdateRoadCachedOneWayStatesAroundTile(tile);
							}
							return CommandCost();
						}
						return_cmd_error(STR_ERROR_ALREADY_BUILT);
					}
					/* Disallow breaking end-of-line of someone else
					 * so trams can still reverse on this tile. */
					if (rtt == RTT_TRAM && HasExactlyOneBit(existing)) {
						Owner owner = GetRoadOwner(tile, rtt);
						if (Company::IsValidID(owner)) {
							CommandCost ret = CheckOwnership(owner);
							if (ret.Failed()) return ret;
						}
					}
					break;
				}

				case ROAD_TILE_CROSSING:
					if (RoadNoLevelCrossing(rt)) {
						return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_ROAD);
					}

					other_bits = GetCrossingRoadBits(tile);
					if (pieces & ComplementRoadBits(other_bits)) goto do_clear;
					pieces = other_bits; // we need to pay for both roadbits

					if (HasTileRoadType(tile, rtt)) return_cmd_error(STR_ERROR_ALREADY_BUILT);
					break;

				case ROAD_TILE_DEPOT:
					if ((GetAnyRoadBits(tile, rtt) & pieces) == pieces) return_cmd_error(STR_ERROR_ALREADY_BUILT);
					goto do_clear;

				default: NOT_REACHED();
			}
			break;

		case MP_RAILWAY: {
			if (IsSteepSlope(tileh)) {
				return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
			}

			/* Level crossings may only be built on these slopes */
			if (!HasBit(VALID_LEVEL_CROSSING_SLOPES, tileh)) {
				return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
			}

			if (!_settings_game.construction.crossing_with_competitor && company != OWNER_TOWN && company != OWNER_DEITY) {
				CommandCost ret = CheckTileOwnership(tile);
				if (ret.Failed()) return ret;
			}

			if (GetRailTileType(tile) != RAIL_TILE_NORMAL) goto do_clear;

			if (RoadNoLevelCrossing(rt)) {
				return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_ROAD);
			}

			if (RailNoLevelCrossings(GetRailType(tile))) {
				return_cmd_error(STR_ERROR_CROSSING_DISALLOWED_RAIL);
			}

			Axis roaddir;
			switch (GetTrackBits(tile)) {
				case TRACK_BIT_X:
					if (pieces & ROAD_X) goto do_clear;
					roaddir = AXIS_Y;
					break;

				case TRACK_BIT_Y:
					if (pieces & ROAD_Y) goto do_clear;
					roaddir = AXIS_X;
					break;

				default: goto do_clear;
			}

			CommandCost ret = EnsureNoVehicleOnGround(tile);
			if (ret.Failed()) return ret;

			if (flags & DC_EXEC) {
				Track railtrack = AxisToTrack(OtherAxis(roaddir));
				YapfNotifyTrackLayoutChange(tile, railtrack);
				/* Update company infrastructure counts. A level crossing has two road bits. */
				UpdateCompanyRoadInfrastructure(rt, company, 2);

				/* Update rail count for level crossings. The plain track is already
				 * counted, so only add the difference to the level crossing cost. */
				Company *c = Company::GetIfValid(GetTileOwner(tile));
				if (c != nullptr) {
					c->infrastructure.rail[GetRailType(tile)] += LEVELCROSSING_TRACKBIT_FACTOR - 1;
					DirtyCompanyInfrastructureWindows(c->index);
				}

				/* Always add road to the roadtypes (can't draw without it) */
				bool reserved = HasBit(GetRailReservationTrackBits(tile), railtrack);
				MakeRoadCrossing(tile, company, company, GetTileOwner(tile), roaddir, GetRailType(tile), rtt == RTT_ROAD ? rt : INVALID_ROADTYPE, (rtt == RTT_TRAM) ? rt : INVALID_ROADTYPE, p2);
				SetCrossingReservation(tile, reserved);
				UpdateLevelCrossing(tile, false);
				MarkDirtyAdjacentLevelCrossingTilesOnAdd(tile, GetCrossingRoadAxis(tile));
				if (RoadLayoutChangeNotificationEnabled(true)) NotifyRoadLayoutChangedIfTileNonLeaf(tile, rtt, GetCrossingRoadBits(tile));
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(tile);
				}
				if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
					AddTrackToSignalBuffer(tile, railtrack, GetTileOwner(tile));
					UpdateSignalsInBuffer();
				}
				MarkTileDirtyByTile(tile);
			}
			return CommandCost(EXPENSES_CONSTRUCTION, 2 * RoadBuildCost(rt));
		}

		case MP_STATION: {
			if ((GetAnyRoadBits(tile, rtt) & pieces) == pieces) {
				if (toggle_drd != DRD_NONE && rtt == RTT_ROAD && IsDriveThroughStopTile(tile)) {
					Owner owner = GetRoadOwner(tile, rtt);
					if (owner != OWNER_NONE) {
						CommandCost ret = CheckOwnership(owner, tile);
						if (ret.Failed()) return ret;
					}

					DisallowedRoadDirections dis_existing = GetDriveThroughStopDisallowedRoadDirections(tile);
					DisallowedRoadDirections dis_new      = dis_existing ^ toggle_drd;

					/* We allow removing disallowed directions to break up
					 * deadlocks, but adding them can break articulated
					 * vehicles. As such, only when less is disallowed,
					 * i.e. bits are removed, we skip the vehicle check. */
					if (CountBits(dis_existing) <= CountBits(dis_new)) {
						CommandCost ret = EnsureNoVehicleOnGround(tile);
						if (ret.Failed()) return ret;
					}

					if (flags & DC_EXEC) {
						if (IsRoadWaypoint(tile)) {
							SetDriveThroughStopDisallowedRoadDirections(tile, dis_new);
						} else {
							RoadStop *rs = RoadStop::GetByTile(tile, GetRoadStopType(tile));
							rs->ChangeDriveThroughDisallowedRoadDirections(dis_new);
						}
						MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
						NotifyRoadLayoutChanged(CountBits(dis_existing) > CountBits(dis_new));
						UpdateRoadCachedOneWayStatesAroundTile(tile);
					}
					return CommandCost();
				}
				return_cmd_error(STR_ERROR_ALREADY_BUILT);
			} else {
				toggle_drd = DRD_NONE;
			}
			if (!IsDriveThroughStopTile(tile)) goto do_clear;

			RoadBits curbits = AxisToRoadBits(DiagDirToAxis(GetRoadStopDir(tile)));
			if (pieces & ~curbits) goto do_clear;
			pieces = curbits; // we need to pay for both roadbits

			if (HasTileRoadType(tile, rtt)) return_cmd_error(STR_ERROR_ALREADY_BUILT);
			break;
		}

		case MP_TUNNELBRIDGE: {
			if (GetTunnelBridgeTransportType(tile) != TRANSPORT_ROAD) goto do_clear;

			const TileIndex other_end = GetOtherTunnelBridgeEnd(tile);

			if (IsBridge(tile)) {
				const DiagDirection entrance_dir = GetTunnelBridgeDirection(tile);
				const RoadBits entrance_piece = DiagDirToRoadBits(entrance_dir);
				const RoadBits axial_pieces = AxisToRoadBits(DiagDirToAxis(entrance_dir));
				existing = GetCustomBridgeHeadRoadBits(tile, rtt);

				if (!(_settings_game.construction.road_custom_bridge_heads && HasBridgeFlatRamp(tileh, DiagDirToAxis(entrance_dir))) || disable_custom_bridge_heads) {
					/* Ordinary bridge heads only */
					/* Only allow building the outer roadbit, so building long roads stops at existing bridges */
					if (MirrorRoadBits(entrance_piece) != pieces) goto do_clear;
					pieces = axial_pieces;
				}
				if ((existing & pieces) == pieces) return_cmd_error(STR_ERROR_ALREADY_BUILT);
				if ((pieces & ~axial_pieces) && !_settings_game.construction.build_on_slopes) {
					return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
				}

				/* Steep slopes behave the same as slopes with one corner raised. */
				const Slope normalised_tileh = IsSteepSlope(tileh) ? SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh)) : tileh;

				if ((_invalid_tileh_slopes_road[0][normalised_tileh & SLOPE_ELEVATED] & (pieces & ~entrance_piece)) != ROAD_NONE) {
					return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
				}

				/* Don't allow adding roadtype to the bridge/tunnel when vehicles are already driving on it */
				CommandCost ret = TunnelBridgeIsFree(tile, other_end);
				if (ret.Failed()) return ret;


				if ((existing | pieces) == entrance_piece) {
					/*
					 * Don't allow the custom bridge head bits to be only the entrance piece
					 * as this makes road vehicles go haywire
					 */
					pieces = axial_pieces;
				}

				RoadBits added_pieces = (existing | pieces) & ~existing;
				uint added_pieces_count = CountBits(added_pieces);

				RoadType existing_rt = GetRoadType(tile, rtt);
				if (existing_rt != INVALID_ROADTYPE && existing_rt != rt) {
					if (HasPowerOnRoad(rt, existing_rt)) {
						rt = existing_rt;
					} else if (HasPowerOnRoad(existing_rt, rt)) {
						cost.AddCost(CountBits(existing) * RoadConvertCost(existing_rt, rt));
						if (existing & entrance_piece) {
							cost.AddCost((CountBits(GetCustomBridgeHeadRoadBits(other_end, rtt)) + (GetTunnelBridgeLength(tile, other_end) * 2)) * RoadConvertCost(existing_rt, rt));
						}
					} else {
						return CMD_ERROR;
					}
				}

				RoadBits other_end_added_pieces = ROAD_NONE;
				RoadBits other_end_existing = ROAD_NONE;

				if (added_pieces & entrance_piece) {
					/* adding road to whole bridge */

					other_end_added_pieces = MirrorRoadBits(entrance_piece);
					added_pieces_count += 1 + (GetTunnelBridgeLength(tile, other_end) * 2);

					other_end_existing = GetCustomBridgeHeadRoadBits(other_end, rtt);
					assert((other_end_added_pieces & other_end_existing) == ROAD_NONE);

					if (other_end_existing == ROAD_NONE) {
						/*
						 * Don't allow the other end custom bridge head bits to be only the entrance piece
						 * as this makes road vehicles go haywire
						 */
						other_end_added_pieces = axial_pieces;
						added_pieces_count++;
					} else {
						RoadType other_end_rt = GetRoadType(other_end, rtt);
						if (other_end_rt != rt) {
							/* Also check owner of the other side of the bridge, in case it differs */
							Owner other_end_owner = GetRoadOwner(other_end, rtt);
							if (!CanConvertUnownedRoadType(other_end_owner, rtt)) {
								CommandCost ret = CheckOwnership(other_end_owner, other_end);
								if (ret.Failed()) {
									return ret;
								}
							}
							if (HasPowerOnRoad(other_end_rt, rt)) {
								cost.AddCost(CountBits(other_end_existing) * RoadConvertCost(other_end_rt, rt));
							} else {
								return CMD_ERROR;
							}
						}
					}
				}

				cost.AddCost(added_pieces_count * RoadBuildCost(rt));

				if (flags & DC_EXEC) {
					SubtractRoadTunnelBridgeInfrastructure(tile, other_end);

					SetRoadType(tile, rtt, rt);
					if (!existing) {
						SetRoadOwner(tile, rtt, company);
					}
					SetCustomBridgeHeadRoadBits(tile, rtt, existing | pieces);
					if (other_end_added_pieces) {
						SetRoadType(other_end, rtt, rt);
						if (!other_end_existing) {
							SetRoadOwner(other_end, rtt, company);
						}
						SetCustomBridgeHeadRoadBits(other_end, rtt, other_end_existing | other_end_added_pieces);
					} else if (existing & entrance_piece) {
						SetRoadType(other_end, rtt, rt);
					}

					MarkBridgeDirty(tile);

					AddRoadTunnelBridgeInfrastructure(tile, other_end);
					NotifyRoadLayoutChanged(true);
					if (rtt == RTT_ROAD) {
						UpdateRoadCachedOneWayStatesAroundTile(tile);
						UpdateRoadCachedOneWayStatesAroundTile(other_end);
					}
					DirtyAllCompanyInfrastructureWindows();
				}

				return cost;
			} else { // IsTunnel(tile)
				/* Only allow building the outer roadbit, so building long roads stops at existing bridges */
				if (MirrorRoadBits(DiagDirToRoadBits(GetTunnelBridgeDirection(tile))) != pieces) goto do_clear;
				if (HasTileRoadType(tile, rtt)) return_cmd_error(STR_ERROR_ALREADY_BUILT);
				if (RoadNoTunnels(rt)) return_cmd_error(STR_ERROR_TUNNEL_DISALLOWED_ROAD);

				/* Don't allow adding roadtype to the bridge/tunnel when vehicles are already driving on it */
				CommandCost ret = TunnelBridgeIsFree(tile, other_end);
				if (ret.Failed()) return ret;
			}
			break;
		}

		default: {
do_clear:;
			need_to_clear = true;
			break;
		}
	}

	if (need_to_clear) {
		CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);
	}

	if (other_bits != pieces) {
		/* Check the foundation/slopes when adding road/tram bits */
		CommandCost ret = CheckRoadSlope(tileh, &pieces, existing, other_bits);
		/* Return an error if we need to build a foundation (ret != 0) but the
		 * current setting is turned off */
		if (ret.Failed() || (ret.GetCost() != 0 && !_settings_game.construction.build_on_slopes)) {
			return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		}
		cost.AddCost(ret);
	}

	if (!need_to_clear) {
		if (IsTileType(tile, MP_ROAD)) {
			/* Don't put the pieces that already exist */
			pieces &= ComplementRoadBits(existing);

			/* Check if new road bits will have the same foundation as other existing road types */
			if (IsNormalRoad(tile)) {
				Slope slope = GetTileSlope(tile);
				Foundation found_new = GetRoadFoundation(slope, pieces | existing);

				RoadBits bits = GetRoadBits(tile, OtherRoadTramType(rtt));
				/* do not check if there are not road bits of given type */
				if (bits != ROAD_NONE && GetRoadFoundation(slope, bits) != found_new) {
					return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
				}
			}
		}

		CommandCost ret = EnsureNoVehicleOnGround(tile);
		if (ret.Failed()) return ret;

		if (IsNormalRoadTile(tile)) {
			/* If the road types don't match, try to convert only if vehicles of
			 * the new road type are not powered on the present road type and vehicles of
			 * the present road type are powered on the new road type. */
			RoadType existing_rt = GetRoadType(tile, rtt);
			if (existing_rt != INVALID_ROADTYPE && existing_rt != rt) {
				if (HasPowerOnRoad(rt, existing_rt)) {
					rt = existing_rt;
				} else if (HasPowerOnRoad(existing_rt, rt)) {
					CommandCost ret = DoCommand(tile, tile, rt, flags, CMD_CONVERT_ROAD);
					if (ret.Failed()) return ret;
					cost.AddCost(ret);
				} else {
					return CMD_ERROR;
				}
			}
		}
	}

	uint num_pieces = (!need_to_clear && IsTileType(tile, MP_TUNNELBRIDGE)) ?
			/* There are 2 pieces on *every* tile of the bridge or tunnel */
			2 * (GetTunnelBridgeLength(GetOtherTunnelBridgeEnd(tile), tile) + 2) :
			/* Count pieces */
			CountBits(pieces);

	cost.AddCost(num_pieces * RoadBuildCost(rt));

	if (flags & DC_EXEC) {
		switch (GetTileType(tile)) {
			case MP_ROAD: {
				RoadTileType rttype = GetRoadTileType(tile);
				if (existing == ROAD_NONE || rttype == ROAD_TILE_CROSSING) {
					SetRoadType(tile, rtt, rt);
					SetRoadOwner(tile, rtt, company);
					if (rtt == RTT_ROAD) SetTownIndex(tile, p2);
				}
				if (rttype != ROAD_TILE_CROSSING) SetRoadBits(tile, existing | pieces, rtt);
				if (RoadLayoutChangeNotificationEnabled(true)) NotifyRoadLayoutChangedIfTileNonLeaf(tile, rtt, existing | pieces);
				break;
			}

			case MP_TUNNELBRIDGE: {
				TileIndex other_end = GetOtherTunnelBridgeEnd(tile);

				SetRoadType(other_end, rtt, rt);
				SetRoadType(tile, rtt, rt);
				SetRoadOwner(other_end, rtt, company);
				SetRoadOwner(tile, rtt, company);

				/* Mark tiles dirty that have been repaved */
				if (IsBridge(tile)) {
					NOT_REACHED();
				} else {
					MarkTileDirtyByTile(other_end);
					MarkTileDirtyByTile(tile);
				}
				if (rtt == RTT_ROAD) {
					UpdateRoadCachedOneWayStatesAroundTile(other_end);
				}
				NotifyRoadLayoutChanged(true);
				break;
			}

			case MP_STATION: {
				assert_tile(IsDriveThroughStopTile(tile), tile);
				SetRoadType(tile, rtt, rt);
				SetRoadOwner(tile, rtt, company);
				NotifyRoadLayoutChanged(true);
				break;
			}

			default:
				MakeRoadNormal(tile, pieces, (rtt == RTT_ROAD) ? rt : INVALID_ROADTYPE, (rtt == RTT_TRAM) ? rt : INVALID_ROADTYPE, p2, company, company);
				if (RoadLayoutChangeNotificationEnabled(true)) NotifyRoadLayoutChangedIfTileNonLeaf(tile, rtt, pieces);
				break;
		}

		/* Update company infrastructure count. */
		if (IsTileType(tile, MP_TUNNELBRIDGE)) num_pieces *= TUNNELBRIDGE_TRACKBIT_FACTOR;
		UpdateCompanyRoadInfrastructure(rt, GetRoadOwner(tile, rtt), num_pieces);

		if (rtt == RTT_ROAD && IsNormalRoadTile(tile)) {
			existing |= pieces;
			SetDisallowedRoadDirections(tile, IsStraightRoad(existing) ?
					GetDisallowedRoadDirections(tile) ^ toggle_drd : DRD_NONE);
		}
		if (rtt == RTT_ROAD) {
			UpdateRoadCachedOneWayStatesAroundTile(tile);
		}

		MarkTileDirtyByTile(tile);
	}
	return cost;
}

/**
 * Checks whether a road or tram connection can be found when building a new road or tram.
 * @param tile Tile at which the road being built will end.
 * @param rt Roadtype of the road being built.
 * @param dir Direction that the road is following.
 * @return True if the next tile at dir direction is suitable for being connected directly by a second roadbit at the end of the road being built.
 */
static bool CanConnectToRoad(TileIndex tile, RoadType rt, DiagDirection dir)
{
	tile += TileOffsByDiagDir(dir);
	if (!IsValidTile(tile) || !MayHaveRoad(tile)) return false;

	RoadTramType rtt = GetRoadTramType(rt);
	RoadType existing = GetRoadType(tile, rtt);
	if (existing == INVALID_ROADTYPE) return false;
	if (!HasPowerOnRoad(existing, rt) && !HasPowerOnRoad(rt, existing)) return false;

	RoadBits bits = GetAnyRoadBits(tile, rtt, false);
	return (bits & DiagDirToRoadBits(ReverseDiagDir(dir))) != 0;
}

/**
 * Build a long piece of road.
 * @param start_tile start tile of drag (the building cost will appear over this tile)
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0) - start tile starts in the 2nd half of tile (p2 & 1). Only used if bit 6 is set or if we are building a single tile
 * - p2 = (bit 1) - end tile starts in the 2nd half of tile (p2 & 2). Only used if bit 6 is set or if we are building a single tile
 * - p2 = (bit 2) - direction: 0 = along x-axis, 1 = along y-axis (p2 & 4)
 * - p2 = (bit 3..8) - road type
 * - p2 = (bit 10) - set road direction
 * - p2 = (bit 11) - defines two different behaviors for this command:
 *      - 0 = Build up to an obstacle. Do not build the first and last roadbits unless they can be connected to something, or if we are building a single tile
 *      - 1 = Fail if an obstacle is found. Always take into account bit 0 and 1. Disable custom bridge heads. This behavior is used for scripts
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildLongRoad(TileIndex start_tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	DisallowedRoadDirections drd = DRD_NORTHBOUND;

	if (p1 >= MapSize()) return CMD_ERROR;
	TileIndex end_tile = p1;

	RoadType rt = Extract<RoadType, 3, 6>(p2);
	if (!ValParamRoadType(rt)) return CMD_ERROR;

	Axis axis = Extract<Axis, 2, 1>(p2);
	/* Only drag in X or Y direction dictated by the direction variable */
	if (axis == AXIS_X && TileY(start_tile) != TileY(end_tile)) return CMD_ERROR; // x-axis
	if (axis == AXIS_Y && TileX(start_tile) != TileX(end_tile)) return CMD_ERROR; // y-axis

	DiagDirection dir = AxisToDiagDir(axis);

	/* Swap direction, also the half-tile drag var (bit 0 and 1) */
	if (start_tile > end_tile || (start_tile == end_tile && HasBit(p2, 0))) {
		dir = ReverseDiagDir(dir);
		p2 ^= 3;
		drd = DRD_SOUTHBOUND;
	}

	/* On the X-axis, we have to swap the initial bits, so they
	 * will be interpreted correctly in the GTTS. Furthermore
	 * when you just 'click' on one tile to build them. */
	if ((axis == AXIS_Y) == (start_tile == end_tile && HasBit(p2, 0) == HasBit(p2, 1))) drd ^= DRD_BOTH;
	/* No disallowed direction bits have to be toggled */
	if (!HasBit(p2, 10)) drd = DRD_NONE;

	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost last_error = CMD_ERROR;
	TileIndex tile = start_tile;
	bool had_success = false;
	bool is_ai = HasBit(p2, 11);

	_defer_update_road_cache_one_way_state = true;
	auto guard = scope_guard([]() {
		FlushDeferredUpdateRoadCachedOneWayStates();
	});

	/* Start tile is the first tile clicked by the user. */
	for (;;) {
		RoadBits bits = AxisToRoadBits(axis);

		/* Determine which road parts should be built. */
		if (!is_ai && start_tile != end_tile) {
			/* Only build the first and last roadbit if they can connect to something. */
			if (tile == end_tile && !CanConnectToRoad(tile, rt, dir)) {
				bits = DiagDirToRoadBits(ReverseDiagDir(dir));
			} else if (tile == start_tile && !CanConnectToRoad(tile, rt, ReverseDiagDir(dir))) {
				bits = DiagDirToRoadBits(dir);
			}
		} else {
			/* Road parts only have to be built at the start tile or at the end tile. */
			if (tile == end_tile && !HasBit(p2, 1)) bits &= DiagDirToRoadBits(ReverseDiagDir(dir));
			if (tile == start_tile && HasBit(p2, 0)) bits &= DiagDirToRoadBits(dir);
		}

		CommandCost ret = DoCommand(tile, drd << 11 | rt << 4 | bits | (is_ai ? 1 << 13 : 0), 0, flags, CMD_BUILD_ROAD);
		if (ret.Failed()) {
			last_error = ret;
			if (last_error.GetErrorMessage() != STR_ERROR_ALREADY_BUILT) {
				if (is_ai) return last_error;
				if (had_success) break; // Keep going if we haven't constructed any road yet, skipping the start of the drag
			}
		} else {
			had_success = true;
			cost.AddCost(ret);
		}
		/* Do not run into or across bridges/tunnels */
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			if (GetTunnelBridgeDirection(tile) == dir) break;
		}

		if (tile == end_tile) break;

		tile += TileOffsByDiagDir(dir);

		/* Do not run onto a bridge/tunnel tile from below/above */
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			if (GetTunnelBridgeDirection(tile) == ReverseDiagDir(dir)) break;
		}
	}

	return had_success ? cost : last_error;
}

/**
 * Remove a long piece of road.
 * @param start_tile start tile of drag
 * @param flags operation to perform
 * @param p1 end tile of drag
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0) - start tile starts in the 2nd half of tile (p2 & 1)
 * - p2 = (bit 1) - end tile starts in the 2nd half of tile (p2 & 2)
 * - p2 = (bit 2) - direction: 0 = along x-axis, 1 = along y-axis (p2 & 4)
 * - p2 = (bit 3 - 8) - road type
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveLongRoad(TileIndex start_tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);

	if (p1 >= MapSize()) return CMD_ERROR;

	TileIndex end_tile = p1;
	RoadType rt = Extract<RoadType, 3, 6>(p2);
	if (!ValParamRoadType(rt)) return CMD_ERROR;

	Axis axis = Extract<Axis, 2, 1>(p2);
	/* Only drag in X or Y direction dictated by the direction variable */
	if (axis == AXIS_X && TileY(start_tile) != TileY(end_tile)) return CMD_ERROR; // x-axis
	if (axis == AXIS_Y && TileX(start_tile) != TileX(end_tile)) return CMD_ERROR; // y-axis

	/* Swap start and ending tile, also the half-tile drag var (bit 0 and 1) */
	if (start_tile > end_tile || (start_tile == end_tile && HasBit(p2, 0))) {
		TileIndex t = start_tile;
		start_tile = end_tile;
		end_tile = t;
		p2 ^= IsInsideMM(p2 & 3, 1, 3) ? 3 : 0;
	}

	_defer_update_road_cache_one_way_state = true;
	auto guard = scope_guard([]() {
		FlushDeferredUpdateRoadCachedOneWayStates();
	});

	Money money_available = GetAvailableMoneyForCommand();
	Money money_spent = 0;
	TileIndex tile = start_tile;
	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	/* Start tile is the small number. */
	for (;;) {
		RoadBits bits = AxisToRoadBits(axis);

		if (tile == end_tile && !HasBit(p2, 1)) bits &= ROAD_NW | ROAD_NE;
		if (tile == start_tile && HasBit(p2, 0)) bits &= ROAD_SE | ROAD_SW;

		/* try to remove the halves. */
		if (bits != 0) {
			RoadTramType rtt = GetRoadTramType(rt);
			CommandCost ret = RemoveRoad(tile, flags & ~DC_EXEC, bits, rtt, true);
			if (ret.Succeeded()) {
				if (flags & DC_EXEC) {
					money_spent += ret.GetCost();
					if (money_spent > 0 && money_spent > money_available) {
						_additional_cash_required = DoCommand(start_tile, end_tile, p2, flags & ~DC_EXEC, CMD_REMOVE_LONG_ROAD).GetCost();
						return cost;
					}
					RemoveRoad(tile, flags, bits, rtt, false);
				}
				cost.AddCost(ret);
				had_success = true;
			} else {
				/* Some errors are more equal than others. */
				switch (last_error.GetErrorMessage()) {
					case STR_ERROR_OWNED_BY:
					case STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS:
						break;
					default:
						last_error = ret;
				}
			}
		}

		if (tile == end_tile) break;

		tile += (axis == AXIS_Y) ? TileDiffXY(0, 1) : TileDiffXY(1, 0);
	}

	return had_success ? cost : last_error;
}

/**
 * Build a road depot.
 * @param tile tile where to build the depot
 * @param flags operation to perform
 * @param p1 bit 0..1 entrance direction (DiagDirection)
 *           bit 2..7 road type
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 *
 * @todo When checking for the tile slope,
 * distinguish between "Flat land required" and "land sloped in wrong direction"
 */
CommandCost CmdBuildRoadDepot(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	DiagDirection dir = Extract<DiagDirection, 0, 2>(p1);

	RoadType rt = Extract<RoadType, 2, 6>(p1);
	if (!ValParamRoadType(rt)) return CMD_ERROR;

	CommandCost cost(EXPENSES_CONSTRUCTION);

	Slope tileh = GetTileSlope(tile);
	if (tileh != SLOPE_FLAT) {
		if (!_settings_game.construction.build_on_slopes || !CanBuildDepotByTileh(dir, tileh)) {
			return_cmd_error(STR_ERROR_FLAT_LAND_REQUIRED);
		}
		cost.AddCost(_price[PR_BUILD_FOUNDATION]);
	}

	cost.AddCost(DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR));
	if (cost.Failed()) return cost;

	if (IsBridgeAbove(tile)) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

	if (!Depot::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Depot *dep = new Depot(tile);
		dep->build_date = CalTime::CurDate();

		/* A road depot has two road bits. */
		UpdateCompanyRoadInfrastructure(rt, _current_company, ROAD_DEPOT_TRACKBIT_FACTOR);

		MakeRoadDepot(tile, _current_company, dep->index, dir, rt);
		MarkTileDirtyByTile(tile);
		MakeDefaultName(dep);

		NotifyRoadLayoutChanged(true);
	}
	cost.AddCost(_price[PR_BUILD_DEPOT_ROAD]);
	return cost;
}

static CommandCost RemoveRoadDepot(TileIndex tile, DoCommandFlag flags)
{
	if (_current_company != OWNER_WATER) {
		CommandCost ret = CheckTileOwnership(tile);
		if (ret.Failed()) return ret;
	}

	CommandCost ret = EnsureNoVehicleOnGround(tile);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		Company *c = Company::GetIfValid(GetTileOwner(tile));
		if (c != nullptr) {
			/* A road depot has two road bits. */
			RoadType rt = GetRoadTypeRoad(tile);
			if (rt == INVALID_ROADTYPE) rt = GetRoadTypeTram(tile);
			c->infrastructure.road[rt] -= ROAD_DEPOT_TRACKBIT_FACTOR;
			DirtyCompanyInfrastructureWindows(c->index);
		}

		delete Depot::GetByTile(tile);
		DoClearSquare(tile);

		NotifyRoadLayoutChanged(false);
		DeleteNewGRFInspectWindow(GSF_ROADTYPES, tile);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_DEPOT_ROAD]);
}

static CommandCost ClearTile_Road(TileIndex tile, DoCommandFlag flags)
{
	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_NORMAL: {
			RoadBits b = GetAllRoadBits(tile);

			/* Clear the road if only one piece is on the tile OR we are not using the DC_AUTO flag */
			if ((HasExactlyOneBit(b) && GetRoadBits(tile, RTT_TRAM) == ROAD_NONE) || !(flags & DC_AUTO)) {
				CommandCost ret(EXPENSES_CONSTRUCTION);
				for (RoadTramType rtt : _roadtramtypes) {
					if (!MayHaveRoad(tile) || GetRoadType(tile, rtt) == INVALID_ROADTYPE) continue;

					CommandCost tmp_ret = RemoveRoad(tile, flags, GetRoadBits(tile, rtt), rtt, true);
					if (tmp_ret.Failed()) return tmp_ret;
					ret.AddCost(tmp_ret);
				}
				return ret;
			}
			return_cmd_error(STR_ERROR_MUST_REMOVE_ROAD_FIRST);
		}

		case ROAD_TILE_CROSSING: {
			CommandCost ret(EXPENSES_CONSTRUCTION);

			if (flags & DC_AUTO) return_cmd_error(STR_ERROR_MUST_REMOVE_ROAD_FIRST);

			/* Must iterate over the roadtypes in a reverse manner because
			 * tram tracks must be removed before the road bits. */
			for (RoadTramType rtt : { RTT_TRAM, RTT_ROAD }) {
				if (!MayHaveRoad(tile) || GetRoadType(tile, rtt) == INVALID_ROADTYPE) continue;

				CommandCost tmp_ret = RemoveRoad(tile, flags, GetCrossingRoadBits(tile), rtt, true);
				if (tmp_ret.Failed()) return tmp_ret;
				ret.AddCost(tmp_ret);
			}

			if (flags & DC_EXEC) {
				DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}
			return ret;
		}

		default:
		case ROAD_TILE_DEPOT:
			if (flags & DC_AUTO) {
				return_cmd_error(STR_ERROR_BUILDING_MUST_BE_DEMOLISHED);
			}
			return RemoveRoadDepot(tile, flags);
	}
}


struct DrawRoadTileStruct {
	uint16_t image;
	byte subcoord_x;
	byte subcoord_y;
};

#include "table/road_land.h"

/**
 * Get the foundationtype of a RoadBits Slope combination
 *
 * @param tileh The Slope part
 * @param bits The RoadBits part
 * @return The resulting Foundation
 */
Foundation GetRoadFoundation(Slope tileh, RoadBits bits)
{
	/* Flat land and land without a road doesn't require a foundation */
	if (tileh == SLOPE_FLAT || bits == ROAD_NONE) return FOUNDATION_NONE;

	/* Steep slopes behave the same as slopes with one corner raised. */
	if (IsSteepSlope(tileh)) {
		tileh = SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh));
	}

	/* Leveled RoadBits on a slope */
	if ((_invalid_tileh_slopes_road[0][tileh] & bits) == ROAD_NONE) return FOUNDATION_LEVELED;

	/* Straight roads without foundation on a slope */
	if (!IsSlopeWithOneCornerRaised(tileh) &&
			(_invalid_tileh_slopes_road[1][tileh] & bits) == ROAD_NONE)
		return FOUNDATION_NONE;

	/* Roads on steep Slopes or on Slopes with one corner raised */
	return (bits == ROAD_X ? FOUNDATION_INCLINED_X : FOUNDATION_INCLINED_Y);
}

const byte _road_sloped_sprites[14] = {
	0,  0,  2,  0,
	0,  1,  0,  0,
	3,  0,  0,  0,
	0,  0
};

/**
 * Get the sprite offset within a spritegroup.
 * @param slope Slope
 * @param bits Roadbits
 * @return Offset for the sprite within the spritegroup.
 */
uint GetRoadSpriteOffset(Slope slope, RoadBits bits)
{
	if (slope != SLOPE_FLAT) {
		switch (slope) {
			case SLOPE_NE: return 11;
			case SLOPE_SE: return 12;
			case SLOPE_SW: return 13;
			case SLOPE_NW: return 14;
			default: NOT_REACHED();
		}
	} else {
		static const uint offsets[] = {
			0, 18, 17, 7,
			16, 0, 10, 5,
			15, 8, 1, 4,
			9, 3, 6, 2
		};
		return offsets[bits];
	}
}

/**
 * Should the road be drawn as a unpaved snow/desert road?
 * By default, roads are always drawn as unpaved if they are on desert or
 * above the snow line, but NewGRFs can override this for desert.
 *
 * @param snow_or_desert Is snowy or desert tile
 * @param roadside What sort of road this is
 * @return True if snow/desert road sprites should be used.
 */
static bool DrawRoadAsSnowDesert(bool snow_or_desert, Roadside roadside)
{
	return (snow_or_desert &&
			!(_settings_game.game_creation.landscape == LT_TROPIC && HasGrfMiscBit(GMB_DESERT_PAVED_ROADS) &&
				roadside != ROADSIDE_BARREN && roadside != ROADSIDE_GRASS && roadside != ROADSIDE_GRASS_ROAD_WORKS));
}

/**
 * Draws the catenary for the RoadType of the given tile
 * @param ti information about the tile (slopes, height etc)
 * @param rt road type to draw catenary for
 * @param rb the roadbits for the tram
 */
void DrawRoadTypeCatenary(const TileInfo *ti, RoadType rt, RoadBits rb)
{
	/* Don't draw the catenary under a low bridge */
	if (IsBridgeAbove(ti->tile) && !IsTransparencySet(TO_CATENARY)) {
		int height = GetBridgeHeight(GetNorthernBridgeEnd(ti->tile));

		if (height <= GetTileMaxZ(ti->tile) + 1) return;
	}

	if (CountBits(rb) > 2) {
		/* On junctions we check whether neighbouring tiles also have catenary, and possibly
		 * do not draw catenary towards those neighbours, which do not have catenary. */
		RoadBits rb_new = ROAD_NONE;
		for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
			if (rb & DiagDirToRoadBits(dir)) {
				TileIndex neighbour = TileAddByDiagDir(ti->tile, dir);
				if (MayHaveRoad(neighbour)) {
					RoadType rt_road = GetRoadTypeRoad(neighbour);
					RoadType rt_tram = GetRoadTypeTram(neighbour);

					if ((rt_road != INVALID_ROADTYPE && HasRoadCatenary(rt_road)) ||
							(rt_tram != INVALID_ROADTYPE && HasRoadCatenary(rt_tram))) {
						rb_new |= DiagDirToRoadBits(dir);
					}
				}
			}
		}
		if (CountBits(rb_new) >= 2) rb = rb_new;
	}

	const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
	SpriteID front = GetCustomRoadSprite(rti, ti->tile, ROTSG_CATENARY_FRONT);
	SpriteID back = GetCustomRoadSprite(rti, ti->tile, ROTSG_CATENARY_BACK);

	if (front != 0 || back != 0) {
		if (front != 0) front += GetRoadSpriteOffset(ti->tileh, rb);
		if (back != 0) back += GetRoadSpriteOffset(ti->tileh, rb);
	} else if (ti->tileh != SLOPE_FLAT) {
		back  = SPR_TRAMWAY_BACK_WIRES_SLOPED  + _road_sloped_sprites[ti->tileh - 1];
		front = SPR_TRAMWAY_FRONT_WIRES_SLOPED + _road_sloped_sprites[ti->tileh - 1];
	} else {
		back  = SPR_TRAMWAY_BASE + _road_backpole_sprites_1[rb];
		front = SPR_TRAMWAY_BASE + _road_frontwire_sprites_1[rb];
	}

	/* Catenary uses 1st company colour to help identify owner.
	 * For tiles with OWNER_TOWN or OWNER_NONE, recolour CC to grey as a neutral colour. */
	Owner owner = GetRoadOwner(ti->tile, GetRoadTramType(rt));
	PaletteID pal = (owner == OWNER_NONE || owner == OWNER_TOWN ? GENERAL_SPRITE_COLOUR(COLOUR_GREY) : COMPANY_SPRITE_COLOUR(owner));
	int z_wires = (ti->tileh == SLOPE_FLAT ? 0 : TILE_HEIGHT) + BB_HEIGHT_UNDER_BRIDGE;
	if (back != 0) {
		/* The "back" sprite contains the west, north and east pillars.
		 * We cut the sprite at 3/8 of the west/east edges to create 3 sprites.
		 * 3/8 is chosen so that sprites can somewhat graphically extend into the tile. */
		static const int INF = 1000; ///< big number compared to sprite size
		static const SubSprite west  = { -INF, -INF, -12, INF };
		static const SubSprite north = {  -12, -INF,  12, INF };
		static const SubSprite east  = {   12, -INF, INF, INF };
		AddSortableSpriteToDraw(back, pal, ti->x, ti->y, 16,  1, z_wires, ti->z, IsTransparencySet(TO_CATENARY), 15,  0, GetSlopePixelZInCorner(ti->tileh, CORNER_W), &west);
		AddSortableSpriteToDraw(back, pal, ti->x, ti->y,  1,  1, z_wires, ti->z, IsTransparencySet(TO_CATENARY),  0,  0, GetSlopePixelZInCorner(ti->tileh, CORNER_N), &north);
		AddSortableSpriteToDraw(back, pal, ti->x, ti->y,  1, 16, z_wires, ti->z, IsTransparencySet(TO_CATENARY),  0, 15, GetSlopePixelZInCorner(ti->tileh, CORNER_E), &east);
	}
	if (front != 0) {
		/* Draw the "front" sprite (containing south pillar and wires) at a Z height that is both above the vehicles and above the "back" pillars. */
		AddSortableSpriteToDraw(front, pal, ti->x, ti->y, 16, 16, z_wires + 1, ti->z, IsTransparencySet(TO_CATENARY), 0, 0, z_wires);
	}
}

/**
 * Draws the catenary for the given tile
 * @param ti information about the tile (slopes, height etc)
 */
void DrawRoadCatenary(const TileInfo *ti)
{
	RoadBits road = ROAD_NONE;
	RoadBits tram = ROAD_NONE;

	if (IsTileType(ti->tile, MP_ROAD)) {
		if (IsNormalRoad(ti->tile)) {
			road = GetRoadBits(ti->tile, RTT_ROAD);
			tram = GetRoadBits(ti->tile, RTT_TRAM);
		} else if (IsLevelCrossing(ti->tile)) {
			tram = road = (GetCrossingRailAxis(ti->tile) == AXIS_Y ? ROAD_X : ROAD_Y);
		}
	} else if (IsTileType(ti->tile, MP_STATION)) {
		if (IsAnyRoadStop(ti->tile)) {
			if (IsDriveThroughStopTile(ti->tile)) {
				Axis axis = GetRoadStopDir(ti->tile) == DIAGDIR_NE ? AXIS_X : AXIS_Y;
				tram = road = (axis == AXIS_X ? ROAD_X : ROAD_Y);
			} else {
				tram = road = DiagDirToRoadBits(GetRoadStopDir(ti->tile));
			}
		}
	} else if (IsTileType(ti->tile, MP_TUNNELBRIDGE)) {
		road = GetCustomBridgeHeadRoadBits(ti->tile, RTT_ROAD);
		tram = GetCustomBridgeHeadRoadBits(ti->tile, RTT_TRAM);
	} else {
		// No road here, no catenary to draw
		return;
	}

	RoadType rt = GetRoadTypeRoad(ti->tile);
	if (rt != INVALID_ROADTYPE && HasRoadCatenaryDrawn(rt)) {
		DrawRoadTypeCatenary(ti, rt, road);
	}

	rt = GetRoadTypeTram(ti->tile);
	if (rt != INVALID_ROADTYPE && HasRoadCatenaryDrawn(rt)) {
		DrawRoadTypeCatenary(ti, rt, tram);
	}
}

/**
 * Draws details on/around the road
 * @param img the sprite to draw
 * @param ti  the tile to draw on
 * @param dx  the offset from the top of the BB of the tile
 * @param dy  the offset from the top of the BB of the tile
 * @param h   the height of the sprite to draw
 * @param transparent  whether the sprite should be transparent (used for roadside trees)
 */
static void DrawRoadDetail(SpriteID img, const TileInfo *ti, int dx, int dy, int h, bool transparent)
{
	int x = ti->x | dx;
	int y = ti->y | dy;
	int z = ti->z;
	if (ti->tileh != SLOPE_FLAT) z = GetSlopePixelZ(x, y);
	AddSortableSpriteToDraw(img, PAL_NONE, x, y, 2, 2, h, z, transparent);
}

/**
 * Draw road underlay and overlay sprites.
 * @param ti TileInfo
 * @param road_rti Road road type information
 * @param tram_rti Tram road type information
 * @param road_offset Road sprite offset (based on road bits)
 * @param tram_offset Tram sprite offset (based on road bits)
 * @param draw_underlay Whether to draw underlays
 */
void DrawRoadOverlays(const TileInfo *ti, PaletteID pal, const RoadTypeInfo *road_rti, const RoadTypeInfo *tram_rti, uint road_offset, uint tram_offset, bool draw_underlay)
{
	if (draw_underlay) {
		/* Road underlay takes precedence over tram */
		if (road_rti != nullptr) {
			if (road_rti->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_GROUND);
				DrawGroundSprite(ground + road_offset, pal);
			}
		} else {
			if (tram_rti->UsesOverlay()) {
				SpriteID ground = GetCustomRoadSprite(tram_rti, ti->tile, ROTSG_GROUND);
				DrawGroundSprite(ground + tram_offset, pal);
			} else {
				DrawGroundSprite(SPR_TRAMWAY_TRAM + tram_offset, pal);
			}
		}
	}

	/* Draw road overlay */
	if (road_rti != nullptr) {
		if (road_rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_OVERLAY);
			if (ground != 0) DrawGroundSprite(ground + road_offset, pal);
		}
	}

	/* Draw tram overlay */
	if (tram_rti != nullptr) {
		if (tram_rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(tram_rti, ti->tile, ROTSG_OVERLAY);
			if (ground != 0) DrawGroundSprite(ground + tram_offset, pal);
		} else if (road_rti != nullptr) {
			DrawGroundSprite(SPR_TRAMWAY_OVERLAY + tram_offset, pal);
		}
	}
}

/**
 * Get ground sprite to draw for a road tile.
 * @param ti TileInof
 * @param roadside Road side type
 * @param rti Road type info
 * @param offset Road sprite offset
 * @param[out] pal Palette to draw.
 */
static SpriteID GetRoadGroundSprite(const TileInfo *ti, Roadside roadside, const RoadTypeInfo *rti, uint offset, PaletteID *pal, bool snow_or_desert)
{
	/* Draw bare ground sprite if no road or road uses overlay system. */
	if (rti == nullptr || rti->UsesOverlay()) {
		if (DrawRoadAsSnowDesert(snow_or_desert, roadside)) {
			return SPR_FLAT_SNOW_DESERT_TILE + SlopeToSpriteOffset(ti->tileh);
		}

		switch (roadside) {
			case ROADSIDE_BARREN:           *pal = PALETTE_TO_BARE_LAND;
			                                return SPR_FLAT_GRASS_TILE + SlopeToSpriteOffset(ti->tileh);
			case ROADSIDE_GRASS:
			case ROADSIDE_GRASS_ROAD_WORKS: return SPR_FLAT_GRASS_TILE + SlopeToSpriteOffset(ti->tileh);
			default:                        break; // Paved
		}
	}
	/* Draw original road base sprite */
	SpriteID image = SPR_ROAD_Y + offset;
	if (DrawRoadAsSnowDesert(snow_or_desert, roadside)) {
		image += 19;
	} else {
		switch (roadside) {
			case ROADSIDE_BARREN:           *pal = PALETTE_TO_BARE_LAND; break;
			case ROADSIDE_GRASS:            break;
			case ROADSIDE_GRASS_ROAD_WORKS: break;
			default:                        image -= 19; break; // Paved
		}
	}

	return image;
}

/**
 * Draw ground sprite and road pieces
 * @param ti TileInfo
 */
void DrawRoadBits(TileInfo *ti, RoadBits road, RoadBits tram, Roadside roadside, bool snow_or_desert, bool draw_catenary)
{
	const bool is_road_tile = IsTileType(ti->tile, MP_ROAD);

	RoadType road_rt = GetRoadTypeRoad(ti->tile);
	RoadType tram_rt = GetRoadTypeTram(ti->tile);
	const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
	const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);

	if (ti->tileh != SLOPE_FLAT) {
		DrawFoundation(ti, !is_road_tile ? FOUNDATION_LEVELED : GetRoadFoundation(ti->tileh, road | tram));
		/* DrawFoundation() is_road_tile ti. */
	}

	/* Determine sprite offsets */
	uint road_offset = GetRoadSpriteOffset(ti->tileh, road);
	uint tram_offset = GetRoadSpriteOffset(ti->tileh, tram);

	/* Draw baseset underlay */
	PaletteID pal = PAL_NONE;
	SpriteID image = GetRoadGroundSprite(ti, roadside, road_rti, road == ROAD_NONE ? tram_offset : road_offset, &pal, snow_or_desert);
	DrawGroundSprite(image, pal);

	DrawRoadOverlays(ti, pal, road_rti, tram_rti, road_offset, tram_offset);

	/* Draw one way */
	if (is_road_tile && road_rti != nullptr) {
		DisallowedRoadDirections drd = GetDisallowedRoadDirections(ti->tile);
		if (drd != DRD_NONE) {
			SpriteID oneway = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_ONEWAY);

			if (oneway == 0) oneway = SPR_ONEWAY_BASE;

			if ((ti->tileh == SLOPE_NE) || (ti->tileh == SLOPE_NW)) {
				oneway += SPR_ONEWAY_SLOPE_N_OFFSET;
			} else if ((ti->tileh == SLOPE_SE) || (ti->tileh == SLOPE_SW)) {
				oneway += SPR_ONEWAY_SLOPE_S_OFFSET;
			}

			DrawGroundSpriteAt(oneway + drd - 1 + ((road == ROAD_X) ? 0 : 3), PAL_NONE, 8, 8, GetPartialPixelZ(8, 8, ti->tileh));
		}
	}

	if (is_road_tile && HasRoadWorks(ti->tile)) {
		/* Road works */
		DrawGroundSprite((road | tram) & ROAD_X ? SPR_EXCAVATION_X : SPR_EXCAVATION_Y, PAL_NONE);
		return;
	}

	if (draw_catenary) {
		/* Draw road, tram catenary */
		DrawRoadCatenary(ti);
	}

	/* Return if full detail is disabled, or we are zoomed fully out. */
	if (!HasBit(_display_opt, DO_FULL_DETAIL) || _cur_dpi->zoom > ZOOM_LVL_DETAIL) return;

	/* Do not draw details (street lights, trees) under low bridge */
	if (IsBridgeAbove(ti->tile) && (roadside == ROADSIDE_TREES || roadside == ROADSIDE_STREET_LIGHTS)) {
		int height = GetBridgeHeight(GetNorthernBridgeEnd(ti->tile));
		int minz = GetTileMaxZ(ti->tile) + 2;

		if (roadside == ROADSIDE_TREES) minz++;

		if (height < minz) return;
	}

	/* If there are no road bits, return, as there is nothing left to do */
	if (HasAtMostOneBit(road)) return;

	/* Do not draw details when invisible. */
	if (roadside == ROADSIDE_TREES && IsInvisibilitySet(TO_TREES)) return;
	if (roadside == ROADSIDE_STREET_LIGHTS && IsInvisibilitySet(TO_HOUSES)) return;

	/* Check whether details should be transparent. */
	bool is_transparent = false;
	if (roadside == ROADSIDE_TREES && IsTransparencySet(TO_TREES)) {
		is_transparent = true;
	}
	if (roadside == ROADSIDE_STREET_LIGHTS && IsTransparencySet(TO_HOUSES)) {
		is_transparent = true;
	}

	/* Draw extra details. */
	for (const DrawRoadTileStruct *drts = _road_display_table[roadside][road | tram]; drts->image != 0; drts++) {
		DrawRoadDetail(drts->image, ti, drts->subcoord_x, drts->subcoord_y, 0x10, is_transparent);
	}
}

void DrawRoadBitsRoad(TileInfo *ti)
{
	DrawRoadBits(ti, GetRoadBits(ti->tile, RTT_ROAD), GetRoadBits(ti->tile, RTT_TRAM), GetRoadside(ti->tile), IsOnSnow(ti->tile), true);
}

void DrawRoadBitsTunnelBridge(TileInfo *ti)
{
	DrawRoadBits(ti, GetCustomBridgeHeadRoadBits(ti->tile, RTT_ROAD), GetCustomBridgeHeadRoadBits(ti->tile, RTT_TRAM), ROADSIDE_PAVED, false, true);
}

/** Tile callback function for rendering a road tile to the screen */
static void DrawTile_Road(TileInfo *ti, DrawTileProcParams params)
{
	if (!IsBridgeAbove(ti->tile) && GetRoadTileType(ti->tile) != ROAD_TILE_DEPOT && params.min_visible_height > (int)((TILE_HEIGHT + BB_HEIGHT_UNDER_BRIDGE) * ZOOM_LVL_BASE)) return;

	switch (GetRoadTileType(ti->tile)) {
		case ROAD_TILE_NORMAL:
			DrawRoadBitsRoad(ti);
			break;

		case ROAD_TILE_CROSSING: {
			if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

			Axis axis = GetCrossingRailAxis(ti->tile);

			const RailTypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

			RoadType road_rt = GetRoadTypeRoad(ti->tile);
			RoadType tram_rt = GetRoadTypeTram(ti->tile);
			const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
			const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);

			PaletteID pal = PAL_NONE;

			/* Draw base ground */
			if (rti->UsesOverlay()) {
				SpriteID image = SPR_ROAD_Y + axis;

				Roadside roadside = GetRoadside(ti->tile);
				if (DrawRoadAsSnowDesert(IsOnSnow(ti->tile), roadside)) {
					image += 19;
				} else {
					switch (roadside) {
						case ROADSIDE_BARREN: pal = PALETTE_TO_BARE_LAND; break;
						case ROADSIDE_GRASS:  break;
						default:              image -= 19; break; // Paved
					}
				}

				DrawGroundSprite(image, pal);
			} else {
				SpriteID image = rti->base_sprites.crossing + axis;
				if (IsCrossingBarred(ti->tile)) image += 2;

				Roadside roadside = GetRoadside(ti->tile);
				if (DrawRoadAsSnowDesert(IsOnSnow(ti->tile), roadside)) {
					image += 8;
				} else {
					switch (roadside) {
						case ROADSIDE_BARREN: pal = PALETTE_TO_BARE_LAND; break;
						case ROADSIDE_GRASS:  break;
						default:              image += 4; break; // Paved
					}
				}

				DrawGroundSprite(image, pal);
			}

			DrawRoadOverlays(ti, pal, road_rti, tram_rti, axis, axis);

			/* Draw rail/PBS overlay */
			bool draw_pbs = _game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasCrossingReservation(ti->tile);
			if (rti->UsesOverlay()) {
				pal = draw_pbs ? PALETTE_CRASH : PAL_NONE;
				SpriteID rail = GetCustomRailSprite(rti, ti->tile, RTSG_CROSSING) + axis;
				DrawGroundSprite(rail, pal);

				auto is_usable_crossing = [&](TileIndex t) -> bool {
					if (HasRoadTypeRoad(t) && !HasBit(_roadtypes_non_train_colliding, GetRoadTypeRoad(t))) return true;
					if (HasRoadTypeTram(t) && !HasBit(_roadtypes_non_train_colliding, GetRoadTypeTram(t))) return true;
					return false;
				};

				if (!is_usable_crossing(ti->tile)) {
					/* Do not draw crossing overlays */
				} else if (_settings_game.vehicle.adjacent_crossings) {
					const Axis axis = GetCrossingRoadAxis(ti->tile);
					const DiagDirection dir1 = AxisToDiagDir(axis);
					const DiagDirection dir2 = ReverseDiagDir(dir1);
					uint adjacent_diagdirs = 0;
					for (DiagDirection dir : { dir1, dir2 }) {
						const TileIndex t = TileAddByDiagDir(ti->tile, dir);
						if (t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis && is_usable_crossing(t)) {
							SetBit(adjacent_diagdirs, dir);
						}
					}

					switch (adjacent_diagdirs) {
						case 0:
							DrawRailTileSeq(ti, &_crossing_layout, TO_CATENARY, rail, 0, PAL_NONE);
							break;

						case (1 << DIAGDIR_NE):
							DrawRailTileSeq(ti, &_crossing_layout_SW, TO_CATENARY, rail, 0, PAL_NONE);
							break;

						case (1 << DIAGDIR_SE):
							DrawRailTileSeq(ti, &_crossing_layout_NW, TO_CATENARY, rail, 0, PAL_NONE);
							break;

						case (1 << DIAGDIR_SW):
							DrawRailTileSeq(ti, &_crossing_layout_NE, TO_CATENARY, rail, 0, PAL_NONE);
							break;

						case (1 << DIAGDIR_NW):
							DrawRailTileSeq(ti, &_crossing_layout_SE, TO_CATENARY, rail, 0, PAL_NONE);
							break;

						default:
							/* Show no sprites */
							break;
					}
				} else {
					DrawRailTileSeq(ti, &_crossing_layout, TO_CATENARY, rail, 0, PAL_NONE);
				}
			} else if (draw_pbs || tram_rti != nullptr || road_rti->UsesOverlay()) {
				/* Add another rail overlay, unless there is only the base road sprite. */
				pal = draw_pbs ? PALETTE_CRASH : PAL_NONE;
				SpriteID rail = GetCrossingRoadAxis(ti->tile) == AXIS_Y ? GetRailTypeInfo(GetRailType(ti->tile))->base_sprites.single_x : GetRailTypeInfo(GetRailType(ti->tile))->base_sprites.single_y;
				DrawGroundSprite(rail, pal);
			}

			/* Draw road, tram catenary */
			DrawRoadCatenary(ti);

			/* Draw rail catenary */
			if (HasRailCatenaryDrawn(GetRailType(ti->tile))) DrawRailCatenary(ti);

			break;
		}

		default:
		case ROAD_TILE_DEPOT: {
			if (ti->tileh != SLOPE_FLAT) DrawFoundation(ti, FOUNDATION_LEVELED);

			PaletteID palette = COMPANY_SPRITE_COLOUR(GetTileOwner(ti->tile));

			RoadType road_rt = GetRoadTypeRoad(ti->tile);
			RoadType tram_rt = GetRoadTypeTram(ti->tile);
			const RoadTypeInfo *rti = GetRoadTypeInfo(road_rt == INVALID_ROADTYPE ? tram_rt : road_rt);

			int relocation = GetCustomRoadSprite(rti, ti->tile, ROTSG_DEPOT);
			bool default_gfx = relocation == 0;
			if (default_gfx) {
				if (HasBit(rti->flags, ROTF_CATENARY)) {
					if (_loaded_newgrf_features.tram == TRAMWAY_REPLACE_DEPOT_WITH_TRACK && road_rt == INVALID_ROADTYPE && !rti->UsesOverlay()) {
						/* Sprites with track only work for default tram */
						relocation = SPR_TRAMWAY_DEPOT_WITH_TRACK - SPR_ROAD_DEPOT;
						default_gfx = false;
					} else {
						/* Sprites without track are always better, if provided */
						relocation = SPR_TRAMWAY_DEPOT_NO_TRACK - SPR_ROAD_DEPOT;
					}
				}
			} else {
				relocation -= SPR_ROAD_DEPOT;
			}

			DiagDirection dir = GetRoadDepotDirection(ti->tile);
			const DrawTileSprites *dts = &_road_depot[dir];
			DrawGroundSprite(dts->ground.sprite, PAL_NONE);

			if (default_gfx) {
				uint offset = GetRoadSpriteOffset(SLOPE_FLAT, DiagDirToRoadBits(dir));
				if (rti->UsesOverlay()) {
					SpriteID ground = GetCustomRoadSprite(rti, ti->tile, ROTSG_OVERLAY);
					if (ground != 0) DrawGroundSprite(ground + offset, PAL_NONE);
				} else if (road_rt == INVALID_ROADTYPE) {
					DrawGroundSprite(SPR_TRAMWAY_OVERLAY + offset, PAL_NONE);
				}
			}

			DrawRailTileSeq(ti, dts, TO_BUILDINGS, relocation, 0, palette);
			break;
		}
	}
	DrawBridgeMiddle(ti);
}

/**
 * Draw the road depot sprite.
 * @param x   The x offset to draw at.
 * @param y   The y offset to draw at.
 * @param dir The direction the depot must be facing.
 * @param rt  The road type of the depot to draw.
 */
void DrawRoadDepotSprite(int x, int y, DiagDirection dir, RoadType rt)
{
	PaletteID palette = COMPANY_SPRITE_COLOUR(_local_company);

	const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
	int relocation = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_DEPOT);
	bool default_gfx = relocation == 0;
	if (default_gfx) {
		if (HasBit(rti->flags, ROTF_CATENARY)) {
			if (_loaded_newgrf_features.tram == TRAMWAY_REPLACE_DEPOT_WITH_TRACK && RoadTypeIsTram(rt) && !rti->UsesOverlay()) {
				/* Sprites with track only work for default tram */
				relocation = SPR_TRAMWAY_DEPOT_WITH_TRACK - SPR_ROAD_DEPOT;
				default_gfx = false;
			} else {
				/* Sprites without track are always better, if provided */
				relocation = SPR_TRAMWAY_DEPOT_NO_TRACK - SPR_ROAD_DEPOT;
			}
		}
	} else {
		relocation -= SPR_ROAD_DEPOT;
	}

	const DrawTileSprites *dts = &_road_depot[dir];
	DrawSprite(dts->ground.sprite, PAL_NONE, x, y);

	if (default_gfx) {
		uint offset = GetRoadSpriteOffset(SLOPE_FLAT, DiagDirToRoadBits(dir));
		if (rti->UsesOverlay()) {
			SpriteID ground = GetCustomRoadSprite(rti, INVALID_TILE, ROTSG_OVERLAY);
			if (ground != 0) DrawSprite(ground + offset, PAL_NONE, x, y);
		} else if (RoadTypeIsTram(rt)) {
			DrawSprite(SPR_TRAMWAY_OVERLAY + offset, PAL_NONE, x, y);
		}
	}

	DrawRailTileSeqInGUI(x, y, dts, relocation, 0, palette);
}

/**
 * Updates cached nearest town for all road tiles
 * @param invalidate are we just invalidating cached data?
 * @pre invalidate == true implies _generating_world == true
 */
void UpdateNearestTownForRoadTiles(bool invalidate)
{
	assert(!invalidate || _generating_world);

	for (TileIndex t = 0; t < MapSize(); t++) {
		if (IsTileType(t, MP_ROAD) && !IsRoadDepot(t) && !HasTownOwnedRoad(t)) {
			TownID tid = INVALID_TOWN;
			if (!invalidate) {
				const Town *town = CalcClosestTownFromTile(t);
				if (town != nullptr) tid = town->index;
			}
			SetTownIndex(t, tid);
		}
	}
}

static int GetSlopePixelZ_Road(TileIndex tile, uint x, uint y, bool)
{

	if (IsNormalRoad(tile)) {
		int z;
		Slope tileh = GetTilePixelSlope(tile, &z);
		if (tileh == SLOPE_FLAT) return z;

		Foundation f = GetRoadFoundation(tileh, GetAllRoadBits(tile));
		z += ApplyPixelFoundationToSlope(f, &tileh);
		return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
	} else {
		return GetTileMaxPixelZ(tile);
	}
}

static Foundation GetFoundation_Road(TileIndex tile, Slope tileh)
{
	if (IsNormalRoad(tile)) {
		return GetRoadFoundation(tileh, GetAllRoadBits(tile));
	} else {
		return FlatteningFoundation(tileh);
	}
}

static const Roadside _town_road_types[][2] = {
	{ ROADSIDE_GRASS,         ROADSIDE_GRASS },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_TREES,         ROADSIDE_TREES },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED }
};

static_assert(lengthof(_town_road_types) == HZB_END);

static const Roadside _town_road_types_2[][2] = {
	{ ROADSIDE_GRASS,         ROADSIDE_GRASS },
	{ ROADSIDE_PAVED,         ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED },
	{ ROADSIDE_STREET_LIGHTS, ROADSIDE_PAVED }
};

static_assert(lengthof(_town_road_types_2) == HZB_END);


static void TileLoop_Road(TileIndex tile)
{
	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC: {
			/* Flat foundation tiles should look the same as the tiles they visually connect to. */
			int tile_z = GetTileZ(tile);
			if (tile_z == GetSnowLine()) {
				GetFoundationSlope(tile, &tile_z);
			}

			if (IsOnSnow(tile) != (tile_z > GetSnowLine())) {
				ToggleSnow(tile);
				MarkTileDirtyByTile(tile);
			}
			break;
		}

		case LT_TROPIC:
			if (GetTropicZone(tile) == TROPICZONE_DESERT && !IsOnDesert(tile)) {
				ToggleDesert(tile);
				MarkTileDirtyByTile(tile);
			}
			break;
	}

	if (IsRoadDepot(tile)) return;

	const Town *t = ClosestTownFromTile(tile, UINT_MAX);
	if (!HasRoadWorks(tile)) {
		HouseZonesBits grp = HZB_TOWN_EDGE;

		if (t != nullptr) {
			grp = GetTownRadiusGroup(t, tile);

			/* Show an animation to indicate road work */
			if ((t->road_build_months != 0 || Chance16(_settings_game.economy.random_road_reconstruction, 1000)) &&
					(DistanceManhattan(t->xy, tile) < 8 || grp != HZB_TOWN_EDGE) &&
					IsNormalRoad(tile) && !HasAtMostOneBit(GetAllRoadBits(tile))) {
				if (GetFoundationSlope(tile) == SLOPE_FLAT && EnsureNoVehicleOnGround(tile).Succeeded() && Chance16(1, 40)) {
					StartRoadWorks(tile);

					if (_settings_client.sound.ambient) SndPlayTileFx(SND_21_ROAD_WORKS, tile);
					CreateEffectVehicleAbove(
						TileX(tile) * TILE_SIZE + 7,
						TileY(tile) * TILE_SIZE + 7,
						0,
						EV_BULLDOZER);
					MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
					return;
				}
			}
		}

		{
			/* Adjust road ground type depending on 'grp' (grp is the distance to the center) */
			const Roadside *new_rs = (_settings_game.game_creation.landscape == LT_TOYLAND) ? _town_road_types_2[grp] : _town_road_types[grp];
			Roadside cur_rs = GetRoadside(tile);

			/* We have our desired type, do nothing */
			if (cur_rs == new_rs[0]) return;

			/* We have the pre-type of the desired type, switch to the desired type */
			if (cur_rs == new_rs[1]) {
				cur_rs = new_rs[0];
			/* We have barren land, install the pre-type */
			} else if (cur_rs == ROADSIDE_BARREN) {
				cur_rs = new_rs[1];
			/* We're totally off limits, remove any installation and make barren land */
			} else {
				cur_rs = ROADSIDE_BARREN;
			}
			SetRoadside(tile, cur_rs);
			MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		}
	} else if (IncreaseRoadWorksCounter(tile)) {
		TerminateRoadWorks(tile);

		if (_settings_game.economy.mod_road_rebuild) {
			/* Generate a nicer town surface */
			const RoadBits old_rb = GetAnyRoadBits(tile, RTT_ROAD);
			const RoadBits new_rb = CleanUpRoadBits(tile, old_rb);

			if (old_rb != new_rb) {
				RemoveRoad(tile, DC_EXEC | DC_AUTO | DC_NO_WATER, (old_rb ^ new_rb), RTT_ROAD, true);

				/* If new_rb is 0, there are now no road pieces left and the tile is no longer a road tile */
				if (new_rb == 0) {
					MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
					return;
				}
			}
		}

		/* Possibly change road type */
		if (GetRoadOwner(tile, RTT_ROAD) == OWNER_TOWN) {
			RoadType rt = GetTownRoadType();
			if (rt != GetRoadTypeRoad(tile)) {
				SetRoadType(tile, RTT_ROAD, rt);
			}
		}

		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
}

static bool ClickTile_Road(TileIndex tile)
{
	if (!IsRoadDepot(tile)) return false;

	ShowDepotWindow(tile, VEH_ROAD);
	return true;
}

/* Converts RoadBits to TrackBits */
extern const TrackBits _road_trackbits[16] = {
	TRACK_BIT_NONE,                                  // ROAD_NONE
	TRACK_BIT_NONE,                                  // ROAD_NW
	TRACK_BIT_NONE,                                  // ROAD_SW
	TRACK_BIT_LEFT,                                  // ROAD_W
	TRACK_BIT_NONE,                                  // ROAD_SE
	TRACK_BIT_Y,                                     // ROAD_Y
	TRACK_BIT_LOWER,                                 // ROAD_S
	TRACK_BIT_LEFT | TRACK_BIT_LOWER | TRACK_BIT_Y,  // ROAD_Y | ROAD_SW
	TRACK_BIT_NONE,                                  // ROAD_NE
	TRACK_BIT_UPPER,                                 // ROAD_N
	TRACK_BIT_X,                                     // ROAD_X
	TRACK_BIT_LEFT | TRACK_BIT_UPPER | TRACK_BIT_X,  // ROAD_X | ROAD_NW
	TRACK_BIT_RIGHT,                                 // ROAD_E
	TRACK_BIT_RIGHT | TRACK_BIT_UPPER | TRACK_BIT_Y, // ROAD_Y | ROAD_NE
	TRACK_BIT_RIGHT | TRACK_BIT_LOWER | TRACK_BIT_X, // ROAD_X | ROAD_SE
	TRACK_BIT_ALL,                                   // ROAD_ALL
};

static TrackStatus GetTileTrackStatus_Road(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TrackdirBits trackdirbits = TRACKDIR_BIT_NONE;
	TrackdirBits red_signals = TRACKDIR_BIT_NONE; // crossing barred
	switch (mode) {
		case TRANSPORT_RAIL:
			if (IsLevelCrossing(tile)) trackdirbits = TrackBitsToTrackdirBits(GetCrossingRailBits(tile));
			break;

		case TRANSPORT_ROAD: {
			RoadTramType rtt = (RoadTramType)GB(sub_mode, 0, 8);
			if (!HasTileRoadType(tile, rtt)) break;
			switch (GetRoadTileType(tile)) {
				case ROAD_TILE_NORMAL: {
					const uint drd_to_multiplier[DRD_END] = { 0x101, 0x100, 0x1, 0x0 };
					const TrackdirBits left_turns = TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_RIGHT_S;
					const TrackdirBits right_turns = TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_RIGHT_N;
					const TrackdirBits no_exit_turns[4] = {
						TRACKDIR_BIT_RIGHT_S | TRACKDIR_BIT_LOWER_E, // ROAD_NW
						TRACKDIR_BIT_RIGHT_N | TRACKDIR_BIT_UPPER_E, // ROAD_SW
						TRACKDIR_BIT_LEFT_N  | TRACKDIR_BIT_UPPER_W, // ROAD_SE
						TRACKDIR_BIT_LEFT_S  | TRACKDIR_BIT_LOWER_W  // ROAD_NE
					};

					RoadBits bits = GetRoadBits(tile, rtt);

					/* no roadbit at this side of tile, return 0 */
					if (side != INVALID_DIAGDIR && (DiagDirToRoadBits(side) & bits) == 0) break;

					if (!HasRoadWorks(tile)) {
						RoadCachedOneWayState rcows = (rtt == RTT_TRAM) ? RCOWS_NORMAL : GetRoadCachedOneWayState(tile);
						switch (rcows) {
							case RCOWS_NORMAL:
							case RCOWS_NON_JUNCTION_A:
							case RCOWS_NON_JUNCTION_B:
							case RCOWS_NO_ACCESS:
								trackdirbits = (TrackdirBits)(_road_trackbits[bits] * drd_to_multiplier[(DisallowedRoadDirections)rcows]);
								break;

							case RCOWS_SIDE_JUNCTION:
							case RCOWS_SIDE_JUNCTION_NO_EXIT:
								trackdirbits = (TrackdirBits)((_road_trackbits[bits] * 0x101) & ~(_settings_game.vehicle.road_side ? left_turns : right_turns));
								if (rcows == RCOWS_SIDE_JUNCTION_NO_EXIT) trackdirbits &= ~no_exit_turns[FindFirstBit(bits ^ ROAD_ALL) & 3];
								break;

							default:
								NOT_REACHED();
						}
					}
					break;
				}

				case ROAD_TILE_CROSSING: {
					Axis axis = GetCrossingRoadAxis(tile);

					if (side != INVALID_DIAGDIR && axis != DiagDirToAxis(side)) break;

					trackdirbits = TrackBitsToTrackdirBits(AxisToTrackBits(axis));
					auto is_non_colliding = [&]() -> bool {
						uint8_t rtfield = GB(sub_mode, 8, 8);
						if (rtfield == 0) return false;
						RoadType rt = (RoadType)(rtfield - 1);
						return HasBit(_roadtypes_non_train_colliding, rt);
					};
					if (!(sub_mode & TTSSM_NO_RED_SIGNALS) && IsCrossingBarred(tile) && !is_non_colliding()) {
						red_signals = trackdirbits;
						if (TrainOnCrossing(tile)) break;

						auto mask_red_signal_bits_if_crossing_barred = [&](TileIndex t, TrackdirBits mask) {
							if (IsLevelCrossingTile(t) && IsCrossingBarred(t)) red_signals &= mask;
						};
						/* Check for blocked adjacent crossing to south, keep only southbound red signal trackdirs, allow northbound traffic */
						mask_red_signal_bits_if_crossing_barred(TileAddByDiagDir(tile, AxisToDiagDir(axis)), TRACKDIR_BIT_X_SW | TRACKDIR_BIT_Y_SE);
						/* Check for blocked adjacent crossing to north, keep only northbound red signal trackdirs, allow southbound traffic */
						mask_red_signal_bits_if_crossing_barred(TileAddByDiagDir(tile, ReverseDiagDir(AxisToDiagDir(axis))), TRACKDIR_BIT_X_NE | TRACKDIR_BIT_Y_NW);
					}
					break;
				}

				default:
				case ROAD_TILE_DEPOT: {
					DiagDirection dir = GetRoadDepotDirection(tile);

					if (side != INVALID_DIAGDIR && side != dir) break;

					trackdirbits = TrackBitsToTrackdirBits(DiagDirToDiagTrackBits(dir));
					break;
				}
			}
			break;
		}

		default: break;
	}
	return CombineTrackStatus(trackdirbits, red_signals);
}

static const StringID _road_tile_strings[] = {
	STR_LAI_ROAD_DESCRIPTION_ROAD,
	STR_LAI_ROAD_DESCRIPTION_ROAD,
	STR_LAI_ROAD_DESCRIPTION_ROAD,
	STR_LAI_ROAD_DESCRIPTION_ROAD_WITH_STREETLIGHTS,
	STR_LAI_ROAD_DESCRIPTION_ROAD,
	STR_LAI_ROAD_DESCRIPTION_TREE_LINED_ROAD,
	STR_LAI_ROAD_DESCRIPTION_ROAD,
	STR_LAI_ROAD_DESCRIPTION_ROAD,
};

static void GetTileDesc_Road(TileIndex tile, TileDesc *td)
{
	Owner rail_owner = INVALID_OWNER;
	Owner road_owner = INVALID_OWNER;
	Owner tram_owner = INVALID_OWNER;

	RoadType road_rt = GetRoadTypeRoad(tile);
	RoadType tram_rt = GetRoadTypeTram(tile);
	if (road_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(road_rt);
		td->roadtype = rti->strings.name;
		td->road_speed = rti->max_speed / 2;
		road_owner = GetRoadOwner(tile, RTT_ROAD);
	}
	if (tram_rt != INVALID_ROADTYPE) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(tram_rt);
		td->tramtype = rti->strings.name;
		td->tram_speed = rti->max_speed / 2;
		tram_owner = GetRoadOwner(tile, RTT_TRAM);
	}

	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_CROSSING: {
			td->str = STR_LAI_ROAD_DESCRIPTION_ROAD_RAIL_LEVEL_CROSSING;
			rail_owner = GetTileOwner(tile);

			const RailTypeInfo *rti = GetRailTypeInfo(GetRailType(tile));
			td->railtype = rti->strings.name;
			td->rail_speed = rti->max_speed;

			break;
		}

		case ROAD_TILE_DEPOT:
			td->str = STR_LAI_ROAD_DESCRIPTION_ROAD_VEHICLE_DEPOT;
			td->build_date = Depot::GetByTile(tile)->build_date;
			break;

		default: {
			td->str = (road_rt != INVALID_ROADTYPE ? _road_tile_strings[GetRoadside(tile)] : STR_LAI_ROAD_DESCRIPTION_TRAMWAY);
			break;
		}
	}

	/* Now we have to discover, if the tile has only one owner or many:
	 *   - Find a first_owner of the tile. (Currently road or tram must be present, but this will break when the third type becomes available)
	 *   - Compare the found owner with the other owners, and test if they differ.
	 * Note: If road exists it will be the first_owner.
	 */
	Owner first_owner = (road_owner == INVALID_OWNER ? tram_owner : road_owner);
	bool mixed_owners = (tram_owner != INVALID_OWNER && tram_owner != first_owner) || (rail_owner != INVALID_OWNER && rail_owner != first_owner);

	if (mixed_owners) {
		/* Multiple owners */
		td->owner_type[0] = (rail_owner == INVALID_OWNER ? STR_NULL : STR_LAND_AREA_INFORMATION_RAIL_OWNER);
		td->owner[0] = rail_owner;
		td->owner_type[1] = (road_owner == INVALID_OWNER ? STR_NULL : STR_LAND_AREA_INFORMATION_ROAD_OWNER);
		td->owner[1] = road_owner;
		td->owner_type[2] = (tram_owner == INVALID_OWNER ? STR_NULL : STR_LAND_AREA_INFORMATION_TRAM_OWNER);
		td->owner[2] = tram_owner;
	} else {
		/* One to rule them all */
		td->owner[0] = first_owner;
	}
}

/**
 * Given the direction the road depot is pointing, this is the direction the
 * vehicle should be travelling in in order to enter the depot.
 */
static const byte _roadveh_enter_depot_dir[4] = {
	TRACKDIR_X_SW, TRACKDIR_Y_NW, TRACKDIR_X_NE, TRACKDIR_Y_SE
};

static VehicleEnterTileStatus VehicleEnter_Road(Vehicle *v, TileIndex tile, int, int)
{
	switch (GetRoadTileType(tile)) {
		case ROAD_TILE_DEPOT: {
			if (v->type != VEH_ROAD) break;

			RoadVehicle *rv = RoadVehicle::From(v);
			if (rv->frame == RVC_DEPOT_STOP_FRAME &&
					_roadveh_enter_depot_dir[GetRoadDepotDirection(tile)] == rv->state) {
				rv->InvalidateImageCache();
				rv->state = RVSB_IN_DEPOT;
				rv->vehstatus |= VS_HIDDEN;
				rv->direction = ReverseDir(rv->direction);
				if (rv->Next() == nullptr) VehicleEnterDepot(rv->First());
				rv->tile = tile;
				rv->UpdateIsDrawn();

				InvalidateWindowData(WC_VEHICLE_DEPOT, rv->tile);
				return VETSB_ENTERED_WORMHOLE;
			}
			break;
		}

		case ROAD_TILE_CROSSING: {
			if (v->type != VEH_ROAD) break;
			SetCrossingOccupiedByRoadVehicle(tile, true);
			SetBit(RoadVehicle::From(v->First())->rvflags, RVF_ON_LEVEL_CROSSING);
			break;
		}

		default: break;
	}
	return VETSB_CONTINUE;
}


static void ChangeTileOwner_Road(TileIndex tile, Owner old_owner, Owner new_owner)
{
	if (IsRoadDepot(tile)) {
		if (GetTileOwner(tile) == old_owner) {
			if (new_owner == INVALID_OWNER) {
				DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
			} else {
				/* A road depot has two road bits. No need to dirty windows here, we'll redraw the whole screen anyway. */
				RoadType rt = GetRoadTypeRoad(tile);
				if (rt == INVALID_ROADTYPE) rt = GetRoadTypeTram(tile);
				Company::Get(old_owner)->infrastructure.road[rt] -= 2;
				Company::Get(new_owner)->infrastructure.road[rt] += 2;

				SetTileOwner(tile, new_owner);
				for (RoadTramType rtt : _roadtramtypes) {
					if (GetRoadOwner(tile, rtt) == old_owner) {
						SetRoadOwner(tile, rtt, new_owner);
					}
				}
			}
		}
		return;
	}

	for (RoadTramType rtt : _roadtramtypes) {
		/* Update all roadtypes, no matter if they are present */
		if (GetRoadOwner(tile, rtt) == old_owner) {
			RoadType rt = GetRoadType(tile, rtt);
			if (rt != INVALID_ROADTYPE) {
				/* A level crossing has two road bits. No need to dirty windows here, we'll redraw the whole screen anyway. */
				uint num_bits = IsLevelCrossing(tile) ? 2 : CountBits(GetRoadBits(tile, rtt));
				Company::Get(old_owner)->infrastructure.road[rt] -= num_bits;
				if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.road[rt] += num_bits;
			}

			SetRoadOwner(tile, rtt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
		}
	}

	if (IsLevelCrossing(tile)) {
		if (GetTileOwner(tile) == old_owner) {
			if (new_owner == INVALID_OWNER) {
				DoCommand(tile, 0, GetCrossingRailTrack(tile), DC_EXEC | DC_BANKRUPT, CMD_REMOVE_SINGLE_RAIL);
			} else {
				/* Update infrastructure counts. No need to dirty windows here, we'll redraw the whole screen anyway. */
				Company::Get(old_owner)->infrastructure.rail[GetRailType(tile)] -= LEVELCROSSING_TRACKBIT_FACTOR;
				Company::Get(new_owner)->infrastructure.rail[GetRailType(tile)] += LEVELCROSSING_TRACKBIT_FACTOR;

				SetTileOwner(tile, new_owner);
			}
		}
	}
}

static CommandCost TerraformTile_Road(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled()) {
		switch (GetRoadTileType(tile)) {
			case ROAD_TILE_CROSSING:
				if (!IsSteepSlope(tileh_new) && (GetTileMaxZ(tile) == z_new + GetSlopeMaxZ(tileh_new)) && HasBit(VALID_LEVEL_CROSSING_SLOPES, tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
				break;

			case ROAD_TILE_DEPOT:
				if (AutoslopeCheckForEntranceEdge(tile, z_new, tileh_new, GetRoadDepotDirection(tile))) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
				break;

			case ROAD_TILE_NORMAL: {
				RoadBits bits = GetAllRoadBits(tile);
				RoadBits bits_copy = bits;
				/* Check if the slope-road_bits combination is valid at all, i.e. it is safe to call GetRoadFoundation(). */
				if (CheckRoadSlope(tileh_new, &bits_copy, ROAD_NONE, ROAD_NONE).Succeeded()) {
					/* CheckRoadSlope() sometimes changes the road_bits, if it does not agree with them. */
					if (bits == bits_copy) {
						int z_old;
						Slope tileh_old = GetTileSlope(tile, &z_old);

						/* Get the slope on top of the foundation */
						z_old += ApplyFoundationToSlope(GetRoadFoundation(tileh_old, bits), &tileh_old);
						z_new += ApplyFoundationToSlope(GetRoadFoundation(tileh_new, bits), &tileh_new);

						/* The surface slope must not be changed */
						if ((z_old == z_new) && (tileh_old == tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
					}
				}
				break;
			}

			default: NOT_REACHED();
		}
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

/** Update power of road vehicle under which is the roadtype being converted */
static Vehicle *UpdateRoadVehPowerProc(Vehicle *v, void *data)
{
	RoadVehicleList *affected_rvs = static_cast<RoadVehicleList*>(data);
	include(*affected_rvs, RoadVehicle::From(v)->First());

	return nullptr;
}

/**
 * Convert the ownership of the RoadType of the tile if applicable
 * @param tile the tile of which convert ownership
 * @param num_pieces the count of the roadbits to assign to the new owner
 * @param owner the current owner of the RoadType
 * @param from_type the old road type
 * @param to_type the new road type
 */
static void ConvertRoadTypeOwner(TileIndex tile, uint num_pieces, Owner owner, RoadType from_type, RoadType to_type)
{
	// Scenario editor, maybe? Don't touch the owners when converting roadtypes...
	if (_current_company >= MAX_COMPANIES) return;

	// We can't get a company from invalid owners but we can get ownership of roads without an owner
	if (owner >= MAX_COMPANIES && owner != OWNER_NONE) return;

	assert(from_type != INVALID_ROADTYPE && to_type != INVALID_ROADTYPE);

	Company *c;

	switch (owner) {
	case OWNER_NONE:
		SetRoadOwner(tile, GetRoadTramType(to_type), (Owner)_current_company);
		if (num_pieces > 0) UpdateCompanyRoadInfrastructure(to_type, _current_company, num_pieces);
		break;

	default:
		if (num_pieces > 0) {
			c = Company::Get(owner);
			c->infrastructure.road[from_type] -= num_pieces;
			c->infrastructure.road[to_type] += num_pieces;
			DirtyCompanyInfrastructureWindows(c->index);
		}
		break;
	}
}

/**
 * Convert one road subtype to another.
 * Not meant to convert from road to tram.
 *
 * @param tile end tile of road conversion drag
 * @param flags operation to perform
 * @param p1 start tile of drag
 * @param p2 various bitstuffed elements:
 * - p2 = (bit  0..5) new roadtype to convert to.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdConvertRoad(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	RoadType to_type = Extract<RoadType, 0, 6>(p2);

	TileIndex area_start = p1;
	TileIndex area_end = tile;

	if (!ValParamRoadType(to_type)) return CMD_ERROR;
	if (area_start >= MapSize()) return CMD_ERROR;

	RoadVehicleList affected_rvs;
	RoadTramType rtt = GetRoadTramType(to_type);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost error = CommandCost((rtt == RTT_TRAM) ? STR_ERROR_NO_SUITABLE_TRAMWAY : STR_ERROR_NO_SUITABLE_ROAD); // by default, there is no road to convert.
	bool found_convertible_road = false; // whether we actually did convert any road/tram (see bug #7633)

	OrthogonalTileIterator iter(area_start, area_end);
	for (; (tile = *iter) != INVALID_TILE; ++iter) {
		/* Is road present on tile? */
		if (!MayHaveRoad(tile)) continue;

		/* Converting to the same subtype? */
		RoadType from_type = GetRoadType(tile, rtt);
		if (from_type == INVALID_ROADTYPE || from_type == to_type) continue;

		/* Check if there is any infrastructure on tile */
		TileType tt = GetTileType(tile);
		switch (tt) {
			case MP_STATION:
				if (!IsAnyRoadStop(tile)) continue;
				break;
			case MP_ROAD:
				if (IsLevelCrossing(tile) && RoadNoLevelCrossing(to_type)) {
					error.MakeError(STR_ERROR_CROSSING_DISALLOWED_ROAD);
					continue;
				}
				break;
			case MP_TUNNELBRIDGE:
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_ROAD) continue;
				if (IsTunnel(tile) && RoadNoTunnels(to_type)) {
					error.MakeError(STR_ERROR_TUNNEL_DISALLOWED_ROAD);
					continue;
				}
				break;
			default: continue;
		}

		/* Trying to convert other's road */
		Owner owner = GetRoadOwner(tile, rtt);
		if (!CanConvertUnownedRoadType(owner, rtt)) {
			CommandCost ret = CheckOwnership(owner, tile);
			if (ret.Failed()) {
				error = ret;
				continue;
			}
		}

		/* Base the ability to replace town roads and bridges on the town's
		 * acceptance of destructive actions. */
		if (owner == OWNER_TOWN) {
			Town *t = ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
			CommandCost ret = CheckforTownRating(DC_NONE, t, tt == MP_TUNNELBRIDGE ? TUNNELBRIDGE_REMOVE : ROAD_REMOVE);
			if (ret.Failed()) {
				error = ret;
				continue;
			}
		}

		/* Disallow converting town roads to types which do not allow houses, unless this is allowed */
		if (rtt == RTT_ROAD && owner == OWNER_TOWN && HasBit(GetRoadTypeInfo(to_type)->flags, ROTF_NO_HOUSES)
				&& !_settings_game.construction.convert_town_road_no_houses) {
			SetDParamsForOwnedBy(OWNER_TOWN, tile);
			error.MakeError(STR_ERROR_OWNED_BY);
			continue;
		}

		/* Vehicle on the tile when not converting normal <-> powered
		 * Tunnels and bridges have special check later */
		if (tt != MP_TUNNELBRIDGE) {
			if (!HasPowerOnRoad(from_type, to_type)) {
				CommandCost ret = EnsureNoVehicleOnGround(tile);
				if (ret.Failed()) {
					error = ret;
					continue;
				}

				if (rtt == RTT_ROAD && owner == OWNER_TOWN) {
					SetDParamsForOwnedBy(OWNER_TOWN, tile);
					error.MakeError(STR_ERROR_OWNED_BY);
					continue;
				}
			}

			uint num_pieces;
			if (IsRoadDepotTile(tile) || IsAnyRoadStopTile(tile)) {
				num_pieces = HasTileRoadType(tile, rtt) ? 2 : 0;
			} else {
				num_pieces = CountBits(GetAnyRoadBits(tile, rtt));
			}

			found_convertible_road = true;
			cost.AddCost(num_pieces * RoadConvertCost(from_type, to_type));

			if (flags & DC_EXEC) { // we can safely convert, too
				/* Update the company infrastructure counters. */
				if (!IsAnyRoadStopTile(tile) && owner == _current_company) {
					ConvertRoadTypeOwner(tile, num_pieces, owner, from_type, to_type);
				} else {
					UpdateCompanyRoadInfrastructure(from_type, owner, -(int)num_pieces);
					UpdateCompanyRoadInfrastructure(to_type, owner, num_pieces);
				}

				/* Perform the conversion */
				SetRoadType(tile, rtt, to_type);
				MarkTileDirtyByTile(tile);

				/* update power of train on this tile */
				FindVehicleOnPos(tile, VEH_ROAD, &affected_rvs, &UpdateRoadVehPowerProc);

				if (IsRoadDepotTile(tile)) {
					/* Update build vehicle window related to this depot */
					InvalidateWindowData(WC_VEHICLE_DEPOT, tile);
					InvalidateWindowData(WC_BUILD_VEHICLE, tile);
				}
			}
		} else {
			TileIndex endtile = GetOtherTunnelBridgeEnd(tile);

			const bool include_middle = !IsBridge(tile) || GetCustomBridgeHeadRoadBits(tile, rtt) & DiagDirToRoadBits(GetTunnelBridgeDirection(tile));

			/* If both ends of tunnel/bridge are in the range, do not try to convert twice -
			 * it would cause assert because of different test and exec runs */
			if (include_middle && endtile < tile) {
				if (OrthogonalTileArea(area_start, area_end).Contains(endtile)) continue;
			}

			if (IsBridge(tile) && include_middle) {
				/* Also check owner of the other side of the bridge, in case it differs */
				Owner end_owner = GetRoadOwner(endtile, rtt);
				if (!CanConvertUnownedRoadType(end_owner, rtt)) {
					CommandCost ret = CheckOwnership(end_owner, endtile);
					if (ret.Failed()) {
						error = ret;
						continue;
					}
				}
			}

			/* When not converting rail <-> el. rail, any vehicle cannot be in tunnel/bridge */
			if (!HasPowerOnRoad(from_type, to_type)) {
				CommandCost ret = TunnelBridgeIsFree(tile, endtile);
				if (ret.Failed()) {
					error = ret;
					continue;
				}

				if (rtt == RTT_ROAD && owner == OWNER_TOWN) {
					SetDParamsForOwnedBy(OWNER_TOWN, tile);
					error.MakeError(STR_ERROR_OWNED_BY);
					continue;
				}
			}

			/* There are 2 pieces on *every* tile of the bridge or tunnel */
			uint num_pieces = (GetTunnelBridgeLength(tile, endtile) + 2) * 2;
			found_convertible_road = true;
			cost.AddCost(num_pieces * RoadConvertCost(from_type, to_type));

			uint tunnel_length = GetTunnelBridgeLength(tile, endtile);
			auto num_pieces_per_side = [tunnel_length, rtt](TileIndex t, bool middle) -> uint {
				uint num_pieces = 0;
				if (IsBridge(t)) {
					const RoadBits bits = GetCustomBridgeHeadRoadBits(t, rtt);
					num_pieces += CountBits(bits) * TUNNELBRIDGE_TRACKBIT_FACTOR;
				} else {
					num_pieces += 2 * TUNNELBRIDGE_TRACKBIT_FACTOR;
				}
				if (middle) {
					num_pieces += 2 * tunnel_length * TUNNELBRIDGE_TRACKBIT_FACTOR;
				}
				return num_pieces;
			};
			uint tile_pieces = num_pieces_per_side(tile, include_middle);
			uint end_pieces = include_middle ? num_pieces_per_side(endtile, false) : 0;
			cost.AddCost((tile_pieces + end_pieces) * RoadConvertCost(from_type, to_type));

			if (flags & DC_EXEC) {
				/* Update the company infrastructure counters. */
				SubtractRoadTunnelBridgeInfrastructure(tile, endtile);

				if (owner == _current_company) {
					ConvertRoadTypeOwner(tile, 0, owner, from_type, to_type);
					if (include_middle) {
						ConvertRoadTypeOwner(endtile, 0, owner, from_type, to_type);
						SetTunnelBridgeOwner(tile, endtile, _current_company);
					}
				}

				/* Perform the conversion */
				SetRoadType(tile, rtt, to_type);
				if (include_middle) SetRoadType(endtile, rtt, to_type);

				AddRoadTunnelBridgeInfrastructure(tile, endtile);

				FindVehicleOnPos(tile, VEH_ROAD, &affected_rvs, &UpdateRoadVehPowerProc);
				FindVehicleOnPos(endtile, VEH_ROAD, &affected_rvs, &UpdateRoadVehPowerProc);

				if (IsBridge(tile)) {
					MarkBridgeDirty(tile);
				} else {
					MarkTileDirtyByTile(tile);
					MarkTileDirtyByTile(endtile);
				}
			}
		}
	}

	if (flags & DC_EXEC) {
		/* Roadtype changed, update roadvehicles as when entering different track */
		for (RoadVehicle *v : affected_rvs) {
			v->CargoChanged();
		}
	}

	return found_convertible_road ? cost : error;
}


/** Tile callback functions for road tiles */
extern const TileTypeProcs _tile_type_road_procs = {
	DrawTile_Road,           // draw_tile_proc
	GetSlopePixelZ_Road,     // get_slope_z_proc
	ClearTile_Road,          // clear_tile_proc
	nullptr,                    // add_accepted_cargo_proc
	GetTileDesc_Road,        // get_tile_desc_proc
	GetTileTrackStatus_Road, // get_tile_track_status_proc
	ClickTile_Road,          // click_tile_proc
	nullptr,                    // animate_tile_proc
	TileLoop_Road,           // tile_loop_proc
	ChangeTileOwner_Road,    // change_tile_owner_proc
	nullptr,                    // add_produced_cargo_proc
	VehicleEnter_Road,       // vehicle_enter_tile_proc
	GetFoundation_Road,      // get_foundation_proc
	TerraformTile_Road,      // terraform_tile_proc
};
