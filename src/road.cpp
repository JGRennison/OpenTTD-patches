/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road.cpp Generic road related functions. */

#include "stdafx.h"
#include <algorithm>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "rail_map.h"
#include "road_map.h"
#include "water_map.h"
#include "genworld.h"
#include "company_func.h"
#include "company_base.h"
#include "engine_base.h"
#include "date_func.h"
#include "landscape.h"
#include "road.h"
#include "town.h"
#include "pathfinder/npf/aystar.h"
#include "tunnelbridge.h"
#include "road_func.h"
#include "roadveh.h"
#include "map_func.h"
#include "core/backup_type.hpp"
#include "core/random_func.hpp"
#include "3rdparty/robin_hood/robin_hood.h"

#include <numeric>

#include "cheat_func.h"
#include "command_func.h"
#include "safeguards.h"

uint32 _road_layout_change_counter = 0;

/** Whether to build public roads */
enum PublicRoadsConstruction {
	PRC_NONE,         ///< Generate no public roads
	PRC_WITH_CURVES,  ///< Generate roads with lots of curves
	PRC_AVOID_CURVES, ///< Generate roads avoiding curves if possible
};

/**
 * Return if the tile is a valid tile for a crossing.
 *
 * @param tile the current tile
 * @param ax the axis of the road over the rail
 * @return true if it is a valid tile
 */
static bool IsPossibleCrossing(const TileIndex tile, Axis ax)
{
	return (IsTileType(tile, MP_RAILWAY) &&
		GetRailTileType(tile) == RAIL_TILE_NORMAL &&
		GetTrackBits(tile) == (ax == AXIS_X ? TRACK_BIT_Y : TRACK_BIT_X) &&
		GetFoundationSlope(tile) == SLOPE_FLAT);
}

/**
 * Clean up unnecessary RoadBits of a planned tile.
 * @param tile current tile
 * @param org_rb planned RoadBits
 * @return optimised RoadBits
 */
RoadBits CleanUpRoadBits(const TileIndex tile, RoadBits org_rb)
{
	if (!IsValidTile(tile)) return ROAD_NONE;
	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		TileIndex neighbor_tile = TileAddByDiagDir(tile, dir);

		/* Get the Roadbit pointing to the neighbor_tile */
		const RoadBits target_rb = DiagDirToRoadBits(dir);

		/* If the roadbit is in the current plan */
		if (org_rb & target_rb) {
			bool connective = false;
			const RoadBits mirrored_rb = MirrorRoadBits(target_rb);

			test_tile:
			if (IsValidTile(neighbor_tile)) {
				switch (GetTileType(neighbor_tile)) {
					/* Always connective ones */
					case MP_CLEAR: case MP_TREES:
						connective = true;
						break;

					/* The conditionally connective ones */
					case MP_TUNNELBRIDGE:
					case MP_STATION:
					case MP_ROAD:
						if (IsNormalRoadTile(neighbor_tile)) {
							/* Always connective */
							connective = true;
						} else {
							const RoadBits neighbor_rb = GetAnyRoadBits(neighbor_tile, RTT_ROAD) | GetAnyRoadBits(neighbor_tile, RTT_TRAM);

							/* Accept only connective tiles */
							connective = (neighbor_rb & mirrored_rb) != ROAD_NONE;
						}
						break;

					case MP_RAILWAY: {
						if (IsPossibleCrossing(neighbor_tile, DiagDirToAxis(dir))) {
							/* Check far side of crossing */
							neighbor_tile = TileAddByDiagDir(neighbor_tile, dir);
							goto test_tile;
						}
						break;
					}

					case MP_WATER:
						/* Check for real water tile */
						connective = !IsWater(neighbor_tile);
						break;

					/* The definitely not connective ones */
					default: break;
				}
			}

			/* If the neighbor tile is inconnective, remove the planned road connection to it */
			if (!connective) org_rb ^= target_rb;
		}
	}

	return org_rb;
}

/**
 * Finds out, whether given company has a given RoadType available for construction.
 * @param company ID of company
 * @param roadtypet RoadType to test
 * @return true if company has the requested RoadType available
 */
bool HasRoadTypeAvail(const CompanyID company, RoadType roadtype)
{
	if (company == OWNER_DEITY || company == OWNER_TOWN || _game_mode == GM_EDITOR || _generating_world) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(roadtype);
		if (rti->label == 0) return false;

		bool available = (rti->flags & ROTFB_HIDDEN) == 0;
		if (!available && (company == OWNER_TOWN || _game_mode == GM_EDITOR || _generating_world)) {
			if (roadtype == GetTownRoadType()) return true;
		}
		return available;
	} else {
		const Company *c = Company::GetIfValid(company);
		if (c == nullptr) return false;
		return HasBit(c->avail_roadtypes & ~_roadtypes_hidden_mask, roadtype);
	}
}

static RoadTypes GetMaskForRoadTramType(RoadTramType rtt)
{
	return rtt == RTT_TRAM ? _roadtypes_type : ~_roadtypes_type;
}

/**
 * Test if any buildable RoadType is available for a company.
 * @param company the company in question
 * @return true if company has any RoadTypes available
 */
bool HasAnyRoadTypesAvail(CompanyID company, RoadTramType rtt)
{
	return (Company::Get(company)->avail_roadtypes & ~_roadtypes_hidden_mask & GetMaskForRoadTramType(rtt)) != ROADTYPES_NONE;
}

/**
 * Validate functions for rail building.
 * @param roadtype road type to check.
 * @return true if the current company may build the road.
 */
bool ValParamRoadType(RoadType roadtype)
{
	return roadtype < ROADTYPE_END && HasRoadTypeAvail(_current_company, roadtype);
}

/**
 * Add the road types that are to be introduced at the given date.
 * @param rt      Roadtype
 * @param current The currently available roadtypes.
 * @param date    The date for the introduction comparisons.
 * @return The road types that should be available when date
 *         introduced road types are taken into account as well.
 */
RoadTypes AddDateIntroducedRoadTypes(RoadTypes current, Date date)
{
	RoadTypes rts = current;

	for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		/* Unused road type. */
		if (rti->label == 0) continue;

		/* Not date introduced. */
		if (!IsInsideMM(rti->introduction_date, 0, MAX_DAY)) continue;

		/* Not yet introduced at this date. */
		if (rti->introduction_date > date) continue;

		/* Have we introduced all required roadtypes? */
		RoadTypes required = rti->introduction_required_roadtypes;
		if ((rts & required) != required) continue;

		rts |= rti->introduces_roadtypes;
	}

	/* When we added roadtypes we need to run this method again; the added
	 * roadtypes might enable more rail types to become introduced. */
	return rts == current ? rts : AddDateIntroducedRoadTypes(rts, date);
}

/**
 * Get the road types the given company can build.
 * @param company the company to get the road types for.
 * @param introduces If true, include road types introduced by other road types
 * @return the road types.
 */
RoadTypes GetCompanyRoadTypes(CompanyID company, bool introduces)
{
	RoadTypes rts = ROADTYPES_NONE;

	for (const Engine *e : Engine::IterateType(VEH_ROAD)) {
		const EngineInfo *ei = &e->info;

		if (HasBit(ei->climates, _settings_game.game_creation.landscape) &&
				(HasBit(e->company_avail, company) || _date >= e->intro_date + DAYS_IN_YEAR)) {
			const RoadVehicleInfo *rvi = &e->u.road;
			assert(rvi->roadtype < ROADTYPE_END);
			if (introduces) {
				rts |= GetRoadTypeInfo(rvi->roadtype)->introduces_roadtypes;
			} else {
				SetBit(rts, rvi->roadtype);
			}
		}
	}

	if (introduces) return AddDateIntroducedRoadTypes(rts, _date);
	return rts;
}

/**
 * Get list of road types, regardless of company availability.
 * @param introduces If true, include road types introduced by other road types
 * @return the road types.
 */
RoadTypes GetRoadTypes(bool introduces)
{
	RoadTypes rts = ROADTYPES_NONE;

	for (const Engine *e : Engine::IterateType(VEH_ROAD)) {
		const EngineInfo *ei = &e->info;
		if (!HasBit(ei->climates, _settings_game.game_creation.landscape)) continue;

		const RoadVehicleInfo *rvi = &e->u.road;
		assert(rvi->roadtype < ROADTYPE_END);
		if (introduces) {
			rts |= GetRoadTypeInfo(rvi->roadtype)->introduces_roadtypes;
		} else {
			SetBit(rts, rvi->roadtype);
		}
	}

	if (introduces) return AddDateIntroducedRoadTypes(rts, MAX_DAY);
	return rts;
}

/**
 * Get the road type for a given label.
 * @param label the roadtype label.
 * @param allow_alternate_labels Search in the alternate label lists as well.
 * @return the roadtype.
 */
RoadType GetRoadTypeByLabel(RoadTypeLabel label, bool allow_alternate_labels)
{
	/* Loop through each road type until the label is found */
	for (RoadType r = ROADTYPE_BEGIN; r != ROADTYPE_END; r++) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(r);
		if (rti->label == label) return r;
	}

	if (allow_alternate_labels) {
		/* Test if any road type defines the label as an alternate. */
		for (RoadType r = ROADTYPE_BEGIN; r != ROADTYPE_END; r++) {
			const RoadTypeInfo *rti = GetRoadTypeInfo(r);
			if (std::find(rti->alternate_labels.begin(), rti->alternate_labels.end(), label) != rti->alternate_labels.end()) return r;
		}
	}

	/* No matching label was found, so it is invalid */
	return INVALID_ROADTYPE;
}

/**
 * Returns the available RoadSubTypes for the provided RoadType
 * If the given company is valid then will be returned a list of the available sub types at the current date, while passing
 * a deity company will make all the sub types available
 * @param rt the RoadType to filter
 * @param c the company ID to check the roadtype against
 * @param any_date whether to return only currently introduced roadtypes or also future ones
 * @returns the existing RoadSubTypes
 */
RoadTypes ExistingRoadTypes(CompanyID c)
{
	/* Check only players which can actually own vehicles, editor and gamescripts are considered deities */
	if (c < OWNER_END) {
		const Company *company = Company::GetIfValid(c);
		if (company != nullptr) return company->avail_roadtypes;
	}

	RoadTypes known_roadtypes = ROADTYPES_NONE;

	/* Find used roadtypes */
	for (Engine *e : Engine::IterateType(VEH_ROAD)) {
		/* Check if the roadtype can be used in the current climate */
		if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;

		/* Check whether available for all potential companies */
		if (e->company_avail != MAX_UVALUE(CompanyMask)) continue;

		known_roadtypes |= GetRoadTypeInfo(e->u.road.roadtype)->introduces_roadtypes;
	}

	/* Get the date introduced roadtypes as well. */
	known_roadtypes = AddDateIntroducedRoadTypes(known_roadtypes, MAX_DAY);

	return known_roadtypes;
}


/* ========================================================================= */
/*                                PUBLIC ROADS                               */
/* ========================================================================= */

CommandCost CmdBuildBridge(TileIndex end_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text = nullptr);
CommandCost CmdBuildTunnel(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text = nullptr);
CommandCost CmdBuildRoad(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text = nullptr);

static RoadType _public_road_type;
static const uint _public_road_hash_size = 8U; ///< The number of bits the hash for river finding should have.

/** Helper function to check if a slope along a certain direction is going up an inclined slope. */
static bool IsUpwardsSlope(const Slope slope, DiagDirection road_direction)
{
	if (!IsInclinedSlope(slope)) return false;

	const auto slope_direction = GetInclinedSlopeDirection(slope);

	return road_direction == slope_direction;
}

/** Helper function to check if a slope along a certain direction is going down an inclined slope. */
static bool IsDownwardsSlope(const Slope slope, const DiagDirection road_direction)
{
	if (!IsInclinedSlope(slope)) return false;

	const auto slope_direction = GetInclinedSlopeDirection(slope);

	return road_direction == ReverseDiagDir(slope_direction);
}

/** Helper function to check if a slope is effectively flat. */
static bool IsSufficientlyFlatSlope(const Slope slope)
{
	return !IsSteepSlope(slope) && HasBit(VALID_LEVEL_CROSSING_SLOPES, slope);
}

static TileIndex BuildTunnel(PathNode *current, TileIndex end_tile = INVALID_TILE, const bool build_tunnel = false)
{
	const TileIndex start_tile = current->node.tile;
	int start_z;
	GetTileSlope(start_tile, &start_z);

	if (start_z == 0) return INVALID_TILE;

	const DiagDirection direction = GetInclinedSlopeDirection(GetTileSlope(start_tile));

	if (!build_tunnel) {
		// We are not building yet, so we still need to find the end_tile.
		const TileIndexDiff delta = TileOffsByDiagDir(direction);
		end_tile = start_tile;
		int end_z;
		const uint tunnel_length_limit = std::min<uint>(_settings_game.construction.max_tunnel_length, 30);

		for (uint tunnel_length = 1;; tunnel_length++) {
			end_tile += delta;

			if (!IsValidTile(end_tile)) return INVALID_TILE;
			if (tunnel_length > tunnel_length_limit) return INVALID_TILE;

			GetTileSlope(end_tile, &end_z);

			if (start_z == end_z) break;

			if (!_cheats.crossing_tunnels.value && IsTunnelInWay(end_tile, start_z)) return INVALID_TILE;
		}

		// No too long or super-short tunnels and always ending up on a matching upwards slope.
		if (IsSteepSlope(GetTileSlope(end_tile)) || IsHalftileSlope(GetTileSlope(end_tile))) return INVALID_TILE;
		if (GetTileSlope(start_tile) != ComplementSlope(GetTileSlope(end_tile))) return INVALID_TILE;
		if (AreTilesAdjacent(start_tile, end_tile)) return INVALID_TILE;
		if (!IsValidTile(end_tile)) return INVALID_TILE;
		if (!IsTileType(end_tile, MP_CLEAR) && !IsTileType(end_tile, MP_TREES)) return INVALID_TILE;
	}

	assert(!build_tunnel || (IsValidTile(end_tile) && GetTileSlope(start_tile) == ComplementSlope(GetTileSlope(end_tile))));

	Backup cur_company(_current_company, OWNER_DEITY, FILE_LINE);
	const auto build_tunnel_cmd = CmdBuildTunnel(start_tile, DC_AUTO | (build_tunnel ? DC_EXEC : DC_NONE), _public_road_type | (TRANSPORT_ROAD << 8), 0);
	cur_company.Restore();

	assert(!build_tunnel || build_tunnel_cmd.Succeeded());
	assert(!build_tunnel || (IsTileType(start_tile, MP_TUNNELBRIDGE) && IsTileType(end_tile, MP_TUNNELBRIDGE)));

	if (!build_tunnel_cmd.Succeeded()) return INVALID_TILE;

	return end_tile;
}

static TileIndex BuildBridge(PathNode *current, TileIndex end_tile = INVALID_TILE, const bool build_bridge = false)
{
	const TileIndex start_tile = current->node.tile;

	// We are not building yet, so we still need to find the end_tile.
	// We will only build a bridge if we need to cross a river, so first check for that.
	if (!build_bridge) {
		const DiagDirection direction = ReverseDiagDir(GetInclinedSlopeDirection(GetTileSlope(start_tile)));

		TileIndex tile = start_tile + TileOffsByDiagDir(direction);
		const bool is_over_water = IsValidTile(tile) && IsTileType(tile, MP_WATER) && IsSea(tile);
		uint bridge_length = 0;
		const uint bridge_length_limit = std::min<uint>(_settings_game.construction.max_bridge_length, is_over_water ? 20 : 10);

		// We are not building yet, so we still need to find the end_tile.
		for (;
			IsValidTile(tile) &&
			(bridge_length <= bridge_length_limit) &&
			(GetTileZ(start_tile) < (GetTileZ(tile) + _settings_game.construction.max_bridge_height)) &&
			(GetTileZ(tile) <= GetTileZ(start_tile));
			tile += TileOffsByDiagDir(direction), bridge_length++) {

			auto is_complementary_slope =
				!IsSteepSlope(GetTileSlope(tile)) &&
				!IsHalftileSlope(GetTileSlope(tile)) &&
				GetTileSlope(start_tile) == ComplementSlope(GetTileSlope(tile));

			// No super-short bridges and always ending up on a matching upwards slope.
			if (!AreTilesAdjacent(start_tile, tile) && is_complementary_slope) {
				end_tile = tile;
				break;
			}
		}

		if (!IsValidTile(end_tile)) return INVALID_TILE;
		if (GetTileSlope(start_tile) != ComplementSlope(GetTileSlope(end_tile))) return INVALID_TILE;
		if (!IsTileType(end_tile, MP_CLEAR) && !IsTileType(end_tile, MP_TREES) && !IsCoastTile(end_tile)) return INVALID_TILE;
	}

	assert(!build_bridge || (IsValidTile(end_tile) && GetTileSlope(start_tile) == ComplementSlope(GetTileSlope(end_tile))));

	const uint length = GetTunnelBridgeLength(start_tile, end_tile);

	std::vector<BridgeType> available_bridge_types;
	for (BridgeType i = 0; i < MAX_BRIDGES; ++i) {
		if (MayTownBuildBridgeType(i) && CheckBridgeAvailability(i, length).Succeeded()) {
			available_bridge_types.push_back(i);
		}
	}

	assert(!build_bridge || !available_bridge_types.empty());
	if (available_bridge_types.empty()) return INVALID_TILE;

	const auto bridge_type = available_bridge_types[build_bridge ? RandomRange(uint32(available_bridge_types.size())) : 0];

	Backup cur_company(_current_company, OWNER_DEITY, FILE_LINE);
	const auto build_bridge_cmd = CmdBuildBridge(end_tile, DC_AUTO | (build_bridge ? DC_EXEC : DC_NONE), start_tile, bridge_type | (_public_road_type << 8) | (TRANSPORT_ROAD << 15));
	cur_company.Restore();

	assert(!build_bridge || build_bridge_cmd.Succeeded());
	assert(!build_bridge || (IsTileType(start_tile, MP_TUNNELBRIDGE) && IsTileType(end_tile, MP_TUNNELBRIDGE)));

	if (!build_bridge_cmd.Succeeded()) return INVALID_TILE;

	return end_tile;
}

static TileIndex BuildRiverBridge(PathNode *current, const DiagDirection road_direction, TileIndex end_tile = INVALID_TILE, const bool build_bridge = false)
{
	const TileIndex start_tile = current->node.tile;
	const int start_tile_z = GetTileMaxZ(start_tile);

	if (!build_bridge) {
		// We are not building yet, so we still need to find the end_tile.
		// We will only build a bridge if we need to cross a river, so first check for that.
		TileIndex tile = start_tile + TileOffsByDiagDir(road_direction);

		if (!IsWaterTile(tile) || !IsRiver(tile)) return INVALID_TILE;

		// Now let's see if we can bridge it. But don't bridge anything more than 4 river tiles. Cities aren't allowed to, so public roads we are not either.
		// Only bridges starting at slopes should be longer ones. The others look like crap when built this way. Players can build them but the map generator
		// should not force that on them. This is just to bridge rivers, not to make long bridges.
		for (;
			IsValidTile(tile) &&
			(GetTunnelBridgeLength(start_tile, tile) <= std::min(_settings_game.construction.max_bridge_length, (uint16)3)) &&
			(start_tile_z < (GetTileZ(tile) + _settings_game.construction.max_bridge_height)) &&
			(GetTileZ(tile) <= start_tile_z);
			tile += TileOffsByDiagDir(road_direction)) {

			if ((IsTileType(tile, MP_CLEAR) || IsTileType(tile, MP_TREES) || IsCoastTile(tile)) &&
					GetTileZ(tile) <= start_tile_z &&
					IsSufficientlyFlatSlope(GetTileSlope(tile))) {
				end_tile = tile;
				break;
			}
		}

		if (!IsValidTile(end_tile)) return INVALID_TILE;
		if (!IsTileType(end_tile, MP_CLEAR) && !IsTileType(end_tile, MP_TREES) && !IsCoastTile(end_tile)) return INVALID_TILE;
	}

	assert(!build_bridge || IsValidTile(end_tile));

	const uint length = GetTunnelBridgeLength(start_tile, end_tile);

	std::vector<BridgeType> available_bridge_types;
	for (BridgeType i = 0; i < MAX_BRIDGES; ++i) {
		if (MayTownBuildBridgeType(i) && CheckBridgeAvailability(i, length).Succeeded()) {
			available_bridge_types.push_back(i);
		}
	}

	const auto bridge_type = available_bridge_types[build_bridge ? RandomRange(uint32(available_bridge_types.size())) : 0];

	Backup cur_company(_current_company, OWNER_DEITY, FILE_LINE);
	const auto build_bridge_cmd = CmdBuildBridge(end_tile, DC_AUTO | (build_bridge ? DC_EXEC : DC_NONE), start_tile, bridge_type | (_public_road_type << 8) | (TRANSPORT_ROAD << 15));
	cur_company.Restore();

	assert(!build_bridge || build_bridge_cmd.Succeeded());
	assert(!build_bridge || (IsTileType(start_tile, MP_TUNNELBRIDGE) && IsTileType(end_tile, MP_TUNNELBRIDGE)));

	if (!build_bridge_cmd.Succeeded()) return INVALID_TILE;

	return end_tile;
}

static bool IsValidNeighbourOfPreviousTile(const TileIndex tile, const TileIndex previous_tile)
{
	if (!IsValidTile(tile) || (tile == previous_tile)) return false;

	const auto forward_direction = DiagdirBetweenTiles(previous_tile, tile);

	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		if (GetOtherTunnelBridgeEnd(tile) == previous_tile) return true;

		const auto tunnel_direction = GetTunnelBridgeDirection(tile);

		return (tunnel_direction == forward_direction);
	}

	if (!IsTileType(tile, MP_CLEAR) && !IsTileType(tile, MP_TREES) && !IsTileType(tile, MP_ROAD) && !IsCoastTile(tile)) return false;

	struct slope_desc {
		int tile_z;
		Slope tile_slope;
		int z;
		Slope slope;
	};

	auto get_slope_info = [](TileIndex t) -> slope_desc {
		slope_desc desc;

		desc.tile_slope = GetTileSlope(t, &desc.tile_z);

		desc.z = desc.tile_z;
		desc.slope = GetFoundationSlopeFromTileSlope(t, desc.tile_slope, &desc.z);

		if (desc.slope == desc.tile_slope && desc.slope != SLOPE_FLAT && HasBit(VALID_LEVEL_CROSSING_SLOPES, desc.slope)) {
			/* Synthesise a trivial flattening foundation */
			desc.slope = SLOPE_FLAT;
			desc.z++;
		}

		return desc;
	};
	const slope_desc sd = get_slope_info(tile);
	if (IsSteepSlope(sd.slope)) return false;

	const slope_desc previous_sd = get_slope_info(previous_tile);

	auto is_non_trivial_foundation = [](const slope_desc &sd) -> bool {
		return sd.slope != sd.tile_slope && !HasBit(VALID_LEVEL_CROSSING_SLOPES, sd.tile_slope);
	};

	/* Check non-trivial foundations (those which aren't 3 corners raised or 2 opposite corners raised -> flat) */
	if (is_non_trivial_foundation(sd) || is_non_trivial_foundation(previous_sd)) {
		static const Corner test_corners[16] = {
			// DIAGDIR_NE
			CORNER_N, CORNER_W,
			CORNER_E, CORNER_S,

			// DIAGDIR_SE
			CORNER_S, CORNER_W,
			CORNER_E, CORNER_N,

			// DIAGDIR_SW
			CORNER_S, CORNER_E,
			CORNER_W, CORNER_N,

			// DIAGDIR_NW
			CORNER_N, CORNER_E,
			CORNER_W, CORNER_S
		};
		const Corner *corners = test_corners + (forward_direction * 4);
		return ((previous_sd.z + GetSlopeZInCorner(previous_sd.slope, corners[0])) == (sd.z + GetSlopeZInCorner(sd.slope, corners[1]))) &&
				((previous_sd.z + GetSlopeZInCorner(previous_sd.slope, corners[2])) == (sd.z + GetSlopeZInCorner(sd.slope, corners[3])));
	}

	if (IsInclinedSlope(sd.slope)) {
		const auto slope_direction = GetInclinedSlopeDirection(sd.slope);

		if (slope_direction != forward_direction && ReverseDiagDir(slope_direction) != forward_direction) {
			return false;
		}
	} else if (!HasBit(VALID_LEVEL_CROSSING_SLOPES, sd.slope)) {
		return false;
	} else {
		/* Check whether the previous tile was an inclined slope, and whether we are leaving the previous tile from a valid direction */
		if (sd.tile_slope != SLOPE_FLAT) {
			if (IsInclinedSlope(previous_sd.slope)) {
				const DiagDirection slope_direction = GetInclinedSlopeDirection(previous_sd.slope);
				if (slope_direction != forward_direction && ReverseDiagDir(slope_direction) != forward_direction) return false;
			}
		}
	}

	return true;
}

static bool AreParallelOverlapping(const Point &start_a, const Point &end_a, const Point &start_b, const Point &end_b)
{
	// Check parallel overlaps.
	if (start_a.x == end_a.x && start_b.x == end_b.x && start_a.x == start_b.x) {
		if ((start_a.y <= start_b.y && end_a.y >= start_b.y) || (start_a.y >= start_b.y && end_a.y <= start_b.y) ||
			(start_a.y <= end_b.y && end_a.y >= end_b.y) || (start_a.y >= end_b.y && end_a.y <= end_b.y)) {
			return true;
		}
	}

	if (start_a.y == end_a.y && start_b.y == end_b.y && start_a.y == start_b.y) {
		if ((start_a.x <= start_b.x && end_a.x >= start_b.x) || (start_a.x >= start_b.x && end_a.x <= start_b.x) ||
			(start_a.x <= end_b.x && end_a.x >= end_b.x) || (start_a.x >= end_b.x && end_a.x <= end_b.x)) {
			return true;
		}
	}

	return false;
}

static bool AreIntersecting(const Point &start_a, const Point &end_a, const Point &start_b, const Point &end_b)
{
	if (start_a.x == end_a.x && start_b.y == end_b.y) {
		if ((start_b.x <= start_a.x && end_b.x >= start_a.x) || (start_b.x >= start_a.x && end_b.x <= start_a.x)) {
			if ((start_a.y <= start_b.y && end_a.y >= start_b.y) || (start_a.y >= start_b.y && end_a.y <= start_b.y)) {
				return true;
			}
		}
	}

	if (start_a.y == end_a.y && start_b.x == end_b.x) {
		if ((start_b.y <= start_a.y && end_b.y >= start_a.y) || (start_b.y >= start_a.y && end_b.y <= start_a.y)) {
			if ((start_a.x <= start_b.x && end_a.x >= start_b.x) || (start_a.x >= start_b.x && end_a.x <= start_b.x)) {
				return true;
			}
		}
	}

	return false;
}

static bool IsBlockedByPreviousBridgeOrTunnel(OpenListNode *current, TileIndex start_tile, TileIndex end_tile)
{
	PathNode* start = &current->path;
	PathNode* end = current->path.parent;

	Point start_b {};
	start_b.x = TileX(start_tile);
	start_b.y = TileY(start_tile);
	Point end_b {};
	end_b.x = TileX(end_tile);
	end_b.y = TileY(end_tile);

	while (end != nullptr) {
		Point start_a {};
		start_a.x = TileX(start->node.tile);
		start_a.y = TileY(start->node.tile);
		Point end_a {};
		end_a.x = TileX(end->node.tile);
		end_a.y = TileY(end->node.tile);

		if (!AreTilesAdjacent(start->node.tile, end->node.tile) &&
			(AreIntersecting(start_a, end_a, start_b, end_b) || AreParallelOverlapping(start_a, end_a, start_b, end_b))) {
			return true;
		}

		start = end;
		end = start->parent;
	}

	return false;
}

/** AyStar callback for getting the neighbouring nodes of the given node. */
static void PublicRoad_GetNeighbours(AyStar *aystar, OpenListNode *current)
{
	const auto current_tile = current->path.node.tile;
	const auto previous_tile = current->path.parent != nullptr ? current->path.parent->node.tile : INVALID_TILE;
	const auto forward_direction = DiagdirBetweenTiles(previous_tile, current_tile);

	aystar->num_neighbours = 0;

	// Check if we just went through a tunnel or a bridge.
	if (IsValidTile(previous_tile) && !AreTilesAdjacent(current_tile, previous_tile)) {

		// We went through a tunnel or bridge, this limits our options to proceed to only forward.
		const TileIndex next_tile = current_tile + TileOffsByDiagDir(forward_direction);

		if (IsValidNeighbourOfPreviousTile(next_tile, current_tile)) {
			aystar->neighbours[aystar->num_neighbours].tile = next_tile;
			aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
			aystar->num_neighbours++;
		}
	} else if (IsTileType(current_tile, MP_TUNNELBRIDGE)) {
		// Handle existing tunnels and bridges
		const auto tunnel_bridge_end = GetOtherTunnelBridgeEnd(current_tile);
		aystar->neighbours[aystar->num_neighbours].tile = tunnel_bridge_end;
		aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
		aystar->num_neighbours++;
	} else {
		// Handle regular neighbors.
		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
			const auto neighbour = current_tile + TileOffsByDiagDir(d);

			if (neighbour == previous_tile) {
				continue;
			}

			if (IsValidNeighbourOfPreviousTile(neighbour, current_tile)) {
				aystar->neighbours[aystar->num_neighbours].tile = neighbour;
				aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
				aystar->num_neighbours++;
			}
		}

		// Check if we can turn this into a tunnel or a bridge.
		if (IsValidTile(previous_tile)) {
			const Slope current_tile_slope = GetTileSlope(current_tile);
			if (IsUpwardsSlope(current_tile_slope, forward_direction)) {
				const TileIndex tunnel_end = BuildTunnel(&current->path);

				if (IsValidTile(tunnel_end)) {
					const Slope tunnel_end_slope = GetTileSlope(tunnel_end);
					if (!IsBlockedByPreviousBridgeOrTunnel(current, current_tile, tunnel_end) &&
							!IsSteepSlope(tunnel_end_slope) &&
							!IsHalftileSlope(tunnel_end_slope) &&
							(tunnel_end_slope == ComplementSlope(current_tile_slope))) {
						assert(IsValidDiagDirection(DiagdirBetweenTiles(current_tile, tunnel_end)));
						aystar->neighbours[aystar->num_neighbours].tile = tunnel_end;
						aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
						aystar->num_neighbours++;
					}
				}
			} else if (IsDownwardsSlope(current_tile_slope, forward_direction)) {
				const TileIndex bridge_end = BuildBridge(&current->path, forward_direction);

				if (IsValidTile(bridge_end)) {
					const Slope bridge_end_slope = GetTileSlope(bridge_end);
					if (!IsBlockedByPreviousBridgeOrTunnel(current, current_tile, bridge_end) &&
							!IsSteepSlope(bridge_end_slope) &&
							!IsHalftileSlope(bridge_end_slope) &&
							(bridge_end_slope == ComplementSlope(current_tile_slope))) {
						assert(IsValidDiagDirection(DiagdirBetweenTiles(current_tile, bridge_end)));
						aystar->neighbours[aystar->num_neighbours].tile = bridge_end;
						aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
						aystar->num_neighbours++;
					}
				}
			} else if (IsSufficientlyFlatSlope(current_tile_slope)) {
				// Check if we could bridge a river from a flat tile. Not looking pretty on the map but you gotta do what you gotta do.
				const auto bridge_end = BuildRiverBridge(&current->path, forward_direction);
				assert(!IsValidTile(bridge_end) || IsSufficientlyFlatSlope(GetTileSlope(bridge_end)));

				if (IsValidTile(bridge_end) &&
						!IsBlockedByPreviousBridgeOrTunnel(current, current_tile, bridge_end)) {
					assert(IsValidDiagDirection(DiagdirBetweenTiles(current_tile, bridge_end)));
					aystar->neighbours[aystar->num_neighbours].tile = bridge_end;
					aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
					aystar->num_neighbours++;
				}
			}
		}
	}
}

/** AyStar callback for checking whether we reached our destination. */
static int32 PublicRoad_EndNodeCheck(const AyStar *aystar, const OpenListNode *current)
{
	return current->path.node.tile == static_cast<TileIndex>(reinterpret_cast<uintptr_t>(aystar->user_target)) ? AYSTAR_FOUND_END_NODE : AYSTAR_DONE;
}

/** AyStar callback when an route has been found. */
static void PublicRoad_FoundEndNode(AyStar *aystar, OpenListNode *current)
{
	PathNode* child = nullptr;

	for (PathNode *path = &current->path; path != nullptr; path = path->parent) {
		const TileIndex tile = path->node.tile;

		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			// Just follow the path; infrastructure is already in place.
			continue;
		}

		if (path->parent == nullptr || AreTilesAdjacent(tile, path->parent->node.tile)) {
			RoadBits road_bits = ROAD_NONE;

			if (child != nullptr) {
				const TileIndex tile2 = child->node.tile;
				road_bits |= DiagDirToRoadBits(DiagdirBetweenTiles(tile, tile2));
			}
			if (path->parent != nullptr) {
				const TileIndex tile2 = path->parent->node.tile;
				road_bits |= DiagDirToRoadBits(DiagdirBetweenTiles(tile, tile2));
			}

			if (child != nullptr || path->parent != nullptr) {
				// Check if we need to build anything.
				bool need_to_build_road = true;

				if (IsNormalRoadTile(tile)) {
					const RoadBits existing_bits = GetRoadBits(tile, RTT_ROAD);
					CLRBITS(road_bits, existing_bits);
					if (road_bits == ROAD_NONE) need_to_build_road = false;
				} else if (MayHaveRoad(tile)) {
					/* Tile already has road which can't be modified: level crossings, depots, drive-through stops, etc */
					need_to_build_road = false;
				}

				// If it is already a road and has the right bits, we are good. Otherwise build the needed ones.
				if (need_to_build_road) {
					Backup cur_company(_current_company, OWNER_DEITY, FILE_LINE);
					CmdBuildRoad(tile, DC_EXEC, _public_road_type << 4 | road_bits, 0);
					cur_company.Restore();
				}
			}
		} else {
			// We only get here if we have a parent and we're not adjacent to it. River/Tunnel time!
			const DiagDirection road_direction = DiagdirBetweenTiles(tile, path->parent->node.tile);

			auto end_tile = INVALID_TILE;

			const Slope tile_slope = GetTileSlope(tile);
			if (IsUpwardsSlope(tile_slope, road_direction)) {
				end_tile = BuildTunnel(path, path->parent->node.tile, true);
				assert(IsValidTile(end_tile) && IsDownwardsSlope(GetTileSlope(end_tile), road_direction));
			} else if (IsDownwardsSlope(tile_slope, road_direction)) {
				// Provide the function with the end tile, since we already know it, but still check the result.
				end_tile = BuildBridge(path, path->parent->node.tile, true);
				assert(IsValidTile(end_tile) && IsUpwardsSlope(GetTileSlope(end_tile), road_direction));
			} else {
				// River bridge is the last possibility.
				assert(IsSufficientlyFlatSlope(tile_slope));
				end_tile = BuildRiverBridge(path, road_direction, path->parent->node.tile, true);
				assert(IsValidTile(end_tile) && IsSufficientlyFlatSlope(GetTileSlope(end_tile)));
			}
		}

		child = path;
	}
}

static const int32 BASE_COST_PER_TILE  = 1;      // Cost for existing road or tunnel/bridge.
static const int32 COST_FOR_NEW_ROAD   = 10;    // Cost for building a new road.
static const int32 COST_FOR_SLOPE      = 50;     // Additional cost if the road heads up or down a slope.

/** AyStar callback for getting the cost of the current node. */
static int32 PublicRoad_CalculateG(AyStar *, AyStarNode *current, OpenListNode *parent)
{
	int32 cost = 0;

	const int32 distance = DistanceManhattan(parent->path.node.tile, current->tile);

	if (IsTileType(current->tile, MP_ROAD) || IsTileType(current->tile, MP_TUNNELBRIDGE)) {
		cost += distance * BASE_COST_PER_TILE;
	} else {
		cost += distance * COST_FOR_NEW_ROAD;

		if (GetTileMaxZ(parent->path.node.tile) != GetTileMaxZ(current->tile)) {
			cost += COST_FOR_SLOPE;

			auto current_node = &parent->path;
			auto parent_node = parent->path.parent;

			// Force the pathfinder to build serpentine roads by punishing every slope in the last couple of tiles.
			for (int i = 0; i < 3; ++i) {
				if (current_node == nullptr || parent_node == nullptr) {
					break;
				}

				if (GetTileMaxZ(current_node->node.tile) != GetTileMaxZ(parent_node->node.tile)) {
					cost += COST_FOR_SLOPE;
				}

				current_node = parent_node;
				parent_node = current_node->parent;
			}
		}

		if (distance > 1) {
			// We are planning to build a bridge or tunnel. Make that a bit more expensive.
			cost += 6 * COST_FOR_SLOPE;
			cost += distance * 2 * COST_FOR_NEW_ROAD;
		}
	}

	if (_settings_game.game_creation.build_public_roads == PRC_AVOID_CURVES &&
		parent->path.parent != nullptr &&
		DiagdirBetweenTiles(parent->path.parent->node.tile, parent->path.node.tile) != DiagdirBetweenTiles(parent->path.node.tile, current->tile)) {
		cost += 1;
	}

	return cost;
}

/** AyStar callback for getting the estimated cost to the destination. */
static int32 PublicRoad_CalculateH(AyStar *aystar, AyStarNode *current, OpenListNode *parent)
{
	return DistanceManhattan(static_cast<TileIndex>(reinterpret_cast<uintptr_t>(aystar->user_target)), current->tile) * BASE_COST_PER_TILE;
}

static AyStar PublicRoadAyStar()
{
	AyStar finder {};
	finder.CalculateG = PublicRoad_CalculateG;
	finder.CalculateH = PublicRoad_CalculateH;
	finder.GetNeighbours = PublicRoad_GetNeighbours;
	finder.EndNodeCheck = PublicRoad_EndNodeCheck;
	finder.FoundEndNode = PublicRoad_FoundEndNode;
	finder.max_search_nodes = 1 << 20;

	finder.Init(1 << _public_road_hash_size);

	return finder;
}

static bool PublicRoadFindPath(AyStar& finder, const TileIndex from, TileIndex to)
{
	finder.user_target = reinterpret_cast<void *>(static_cast<uintptr_t>(to));

	AyStarNode start {};
	start.tile = from;
	start.direction = INVALID_TRACKDIR;
	finder.AddStartNode(&start, 0);

	int result = AYSTAR_STILL_BUSY;

	while (result == AYSTAR_STILL_BUSY) {
		result = finder.Main();
	}

	const bool found_path = (result == AYSTAR_FOUND_END_NODE);

	finder.Clear();

	return found_path;
}

struct TownNetwork
{
	uint failures_to_connect {};
	std::vector<TileIndex> towns;
};

void PostProcessNetworks(AyStar &finder, const std::vector<std::unique_ptr<TownNetwork>> &town_networks)
{
	for (const auto &network : town_networks) {
		if (network->towns.size() <= 3) {
			continue;
		}

		std::vector towns(network->towns);

		for (auto town_a : network->towns) {
			std::partial_sort(towns.begin(), towns.begin() + 4, towns.end(), [&](const TileIndex& a, const TileIndex& b) { return DistanceManhattan(a, town_a) < DistanceManhattan(b, town_a); });

			TileIndex second_closest_town = towns[2];
			TileIndex third_closest_town = towns[3];

			PublicRoadFindPath(finder, town_a, second_closest_town);
			PublicRoadFindPath(finder, town_a, third_closest_town);

			IncreaseGeneratingWorldProgress(GWP_PUBLIC_ROADS);
		}
	}
}

/**
* Build the public road network connecting towns using AyStar.
*/
void GeneratePublicRoads()
{
	if (_settings_game.game_creation.build_public_roads == PRC_NONE) return;

	std::vector<TileIndex> towns;
	towns.clear();
	{
		for (const Town *town : Town::Iterate()) {
			towns.push_back(town->xy);
		}
	}

	if (towns.empty()) {
		return;
	}

	SetGeneratingWorldProgress(GWP_PUBLIC_ROADS, uint(towns.size() * 2));


	// Create a list of networks which also contain a value indicating how many times we failed to connect to them.
	std::vector<std::unique_ptr<TownNetwork>> networks;
	robin_hood::unordered_flat_map<TileIndex, TownNetwork *> town_to_network_map;

	TileIndex main_town = *std::max_element(towns.begin(), towns.end(), [&](TileIndex a, TileIndex b) { return DistanceFromEdge(a) < DistanceFromEdge(b); });
	towns.erase(towns.begin());

	_public_road_type = GetTownRoadType();
	robin_hood::unordered_flat_set<TileIndex> checked_towns;

	std::unique_ptr<TownNetwork> new_main_network = std::make_unique<TownNetwork>();
	TownNetwork *main_network = new_main_network.get();
	networks.push_back(std::move(new_main_network));

	main_network->towns.push_back(main_town);
	main_network->failures_to_connect = 0;

	town_to_network_map[main_town] = main_network;

	IncreaseGeneratingWorldProgress(GWP_PUBLIC_ROADS);

	auto town_network_distance = [](const TileIndex town, const TownNetwork *network) -> uint {
		uint best = UINT_MAX;
		for (TileIndex t : network->towns) {
			best = std::min<uint>(best, DistanceManhattan(t, town));
		}
		return best;
	};

	std::sort(towns.begin(), towns.end(), [&](TileIndex a, TileIndex b) { return DistanceManhattan(a, main_town) < DistanceManhattan(b, main_town); });

	AyStar finder = PublicRoadAyStar();

	for (auto start_town : towns) {
		// Check if we can connect to any of the networks.
		checked_towns.clear();

		auto reachable_from_town = town_to_network_map.find(start_town);
		bool found_path = false;

		if (reachable_from_town != town_to_network_map.end()) {
			TownNetwork *reachable_network = reachable_from_town->second;

			const TileIndex end_town = *std::min_element(reachable_network->towns.begin(), reachable_network->towns.end(), [&](TileIndex a, TileIndex b) { return DistanceManhattan(start_town, a) < DistanceManhattan(start_town, b); });
			checked_towns.insert(end_town);

			found_path = PublicRoadFindPath(finder, start_town, end_town);

			if (found_path) {
				reachable_network->towns.push_back(start_town);
				if (reachable_network->failures_to_connect > 0) {
					reachable_network->failures_to_connect--;
				}
			} else {
				town_to_network_map.erase(reachable_from_town);
				reachable_network->failures_to_connect++;
			}
		}

		if (!found_path) {
			std::vector<std::unique_ptr<TownNetwork>>::iterator networks_end;

			if (networks.size() > 5) {
				networks_end = networks.begin() + 5;
			} else {
				networks_end = networks.end();
			}

			std::partial_sort(networks.begin(), networks_end, networks.end(), [&](const std::unique_ptr<TownNetwork> &a, const std::unique_ptr<TownNetwork> &b) {
				return town_network_distance(start_town, a.get()) < town_network_distance(start_town, b.get());
			});

			auto can_reach = [&](const std::unique_ptr<TownNetwork> &network) {
				if (reachable_from_town != town_to_network_map.end() && network.get() == reachable_from_town->second) {
					return false;
				}

				// Try to connect to the town in the network that is closest to us.
				// If we can't connect to that one, we can't connect to any of them since they are all interconnected.
				const TileIndex end_town = *std::min_element(network->towns.begin(), network->towns.end(), [&](TileIndex a, TileIndex b) { return DistanceManhattan(start_town, a) < DistanceManhattan(start_town, b); });

				if (checked_towns.find(end_town) != checked_towns.end()) {
					return false;
				}

				checked_towns.insert(end_town);

				found_path = PublicRoadFindPath(finder, start_town, end_town);

				if (found_path) {
					network->towns.push_back(start_town);
					if (network->failures_to_connect > 0) {
						network->failures_to_connect--;
					}
					town_to_network_map[start_town] = network.get();
				} else {
					network->failures_to_connect++;
				}

				return found_path;
			};

			std::sort(networks.begin(), networks_end, [&](const std::unique_ptr<TownNetwork> &a, const std::unique_ptr<TownNetwork> &b) {
				return a->failures_to_connect < b->failures_to_connect;
			});

			if (!std::any_of(networks.begin(), networks_end, can_reach)) {
				// We failed so many networks, we are a separate network. Let future towns try to connect to us.
				std::unique_ptr<TownNetwork> new_network = std::make_unique<TownNetwork>();
				new_network->towns.push_back(start_town);
				new_network->failures_to_connect = 0;

				// We basically failed to connect to this many towns.
				int towns_already_in_networks = std::accumulate(networks.begin(), networks.end(), 0, [&](int accumulator, const std::unique_ptr<TownNetwork> &network) {
					return accumulator + static_cast<int>(network->towns.size());
				});

				new_network->failures_to_connect += towns_already_in_networks;
				town_to_network_map[start_town] = new_network.get();
				networks.push_back(std::move(new_network));
			}
		}

		IncreaseGeneratingWorldProgress(GWP_PUBLIC_ROADS);
	}

	PostProcessNetworks(finder, networks);

	finder.Free();
}

/* ========================================================================= */
/*                              END PUBLIC ROADS                             */
/* ========================================================================= */
