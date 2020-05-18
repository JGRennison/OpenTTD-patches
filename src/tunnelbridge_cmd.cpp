/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file tunnelbridge_cmd.cpp
 * This file deals with tunnels and bridges (non-gui stuff)
 * @todo separate this file into two
 */

#include "stdafx.h"
#include "newgrf_object.h"
#include "viewport_func.h"
#include "cmd_helper.h"
#include "command_func.h"
#include "town.h"
#include "train.h"
#include "ship.h"
#include "roadveh.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "newgrf_sound.h"
#include "autoslope.h"
#include "tunnelbridge_map.h"
#include "bridge_signal_map.h"
#include "tunnel_base.h"
#include "strings_func.h"
#include "date_func.h"
#include "clear_func.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "sound_func.h"
#include "tunnelbridge.h"
#include "cheat_type.h"
#include "elrail_func.h"
#include "pbs.h"
#include "company_base.h"
#include "newgrf_railtype.h"
#include "newgrf_roadtype.h"
#include "object_base.h"
#include "water.h"
#include "company_gui.h"
#include "viewport_func.h"
#include "station_map.h"
#include "industry_map.h"
#include "object_map.h"
#include "newgrf_station.h"
#include "station_func.h"

#include "table/strings.h"
#include "table/bridge_land.h"

#include "safeguards.h"


BridgeSpec _bridge[MAX_BRIDGES]; ///< The specification of all bridges.
TileIndex _build_tunnel_endtile; ///< The end of a tunnel; as hidden return from the tunnel build command for GUI purposes.

/** Z position of the bridge sprites relative to bridge height (downwards) */
static const int BRIDGE_Z_START = 3;

extern void DrawTrackBits(TileInfo *ti, TrackBits track);
extern void DrawRoadBits(TileInfo *ti);
extern const RoadBits _invalid_tileh_slopes_road[2][15];

extern CommandCost IsRailStationBridgeAboveOk(TileIndex tile, const StationSpec *statspec, byte layout, TileIndex northern_bridge_end, TileIndex southern_bridge_end, int bridge_height,
		BridgeType bridge_type, TransportType bridge_transport_type);

extern CommandCost IsRoadStopBridgeAboveOK(TileIndex tile, bool drive_through, DiagDirection entrance,
		TileIndex northern_bridge_end, TileIndex southern_bridge_end, int bridge_height,
		BridgeType bridge_type, TransportType bridge_transport_type);

/**
 * Mark bridge tiles dirty.
 * Note: The bridge does not need to exist, everything is passed via parameters.
 * @param begin Start tile.
 * @param end End tile.
 * @param direction Direction from \a begin to \a end.
 * @param bridge_height Bridge height level.
 */
void MarkBridgeDirty(TileIndex begin, TileIndex end, DiagDirection direction, uint bridge_height, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	TileIndexDiff delta = TileOffsByDiagDir(direction);
	for (TileIndex t = begin; t != end; t += delta) {
		MarkTileDirtyByTile(t, mark_dirty_if_zoomlevel_is_below, bridge_height - TileHeight(t));
	}
	MarkTileDirtyByTile(end, mark_dirty_if_zoomlevel_is_below);
}

/**
 * Mark bridge tiles dirty.
 * @param tile Bridge head.
 */
void MarkBridgeDirty(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	MarkBridgeDirty(tile, GetOtherTunnelBridgeEnd(tile), GetTunnelBridgeDirection(tile), GetBridgeHeight(tile), mark_dirty_if_zoomlevel_is_below);
}

/**
 * Mark bridge or tunnel tiles dirty.
 * @param tile Bridge head or tunnel entrance.
 */
void MarkBridgeOrTunnelDirty(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	if (IsBridge(tile)) {
		MarkBridgeDirty(tile, mark_dirty_if_zoomlevel_is_below);
	} else {
		MarkTileDirtyByTile(tile, mark_dirty_if_zoomlevel_is_below);
		MarkTileDirtyByTile(GetOtherTunnelBridgeEnd(tile), mark_dirty_if_zoomlevel_is_below);
	}
}

/**
 * Mark bridge or tunnel tiles dirty on tunnel/bridge head reservation change
 * @param tile Bridge head or tunnel entrance.
 */
void MarkBridgeOrTunnelDirtyOnReservationChange(TileIndex tile, const ZoomLevel mark_dirty_if_zoomlevel_is_below)
{
	if (IsTunnelBridgeWithSignalSimulation(tile)) {
		MarkTileDirtyByTile(tile, mark_dirty_if_zoomlevel_is_below);
	} else if (IsBridge(tile)) {
		MarkBridgeDirty(tile, mark_dirty_if_zoomlevel_is_below);
	} else {
		MarkTileDirtyByTile(tile, mark_dirty_if_zoomlevel_is_below);
	}
}

/**
 * Get number of signals on bridge or tunnel with signal simulation.
 * @param begin The begin of the tunnel or bridge.
 * @param end   The end of the tunnel or bridge.
 * @pre IsTunnelBridgeWithSignalSimulation(begin)
 */
uint GetTunnelBridgeSignalSimulationSignalCount(TileIndex begin, TileIndex end)
{
	uint result = 2 + (GetTunnelBridgeLength(begin, end) / _settings_game.construction.simulated_wormhole_signals);
	if (IsTunnelBridgeSignalSimulationBidirectional(begin)) result *= 2;
	return result;
}

/** Reset the data been eventually changed by the grf loaded. */
void ResetBridges()
{
	/* First, free sprite table data */
	for (BridgeType i = 0; i < MAX_BRIDGES; i++) {
		if (_bridge[i].sprite_table != nullptr) {
			for (BridgePieces j = BRIDGE_PIECE_NORTH; j < BRIDGE_PIECE_INVALID; j++) free(_bridge[i].sprite_table[j]);
			free(_bridge[i].sprite_table);
		}
	}

	/* Then, wipe out current bridges */
	memset(&_bridge, 0, sizeof(_bridge));
	/* And finally, reinstall default data */
	memcpy(&_bridge, &_orig_bridge, sizeof(_orig_bridge));
}

/**
 * Calculate the price factor for building a long bridge.
 * Basically the cost delta is 1,1, 1, 2,2, 3,3,3, 4,4,4,4, 5,5,5,5,5, 6,6,6,6,6,6,  7,7,7,7,7,7,7,  8,8,8,8,8,8,8,8,
 * @param length Length of the bridge.
 * @return Price factor for the bridge.
 */
int CalcBridgeLenCostFactor(int length)
{
	if (length < 2) return length;

	length -= 2;
	int sum = 2;
	for (int delta = 1;; delta++) {
		for (int count = 0; count < delta; count++) {
			if (length == 0) return sum;
			sum += delta;
			length--;
		}
	}
}

/**
 * Get the foundation for a bridge.
 * @param tileh The slope to build the bridge on.
 * @param axis The axis of the bridge entrance.
 * @return The foundation required.
 */
Foundation GetBridgeFoundation(Slope tileh, Axis axis)
{
	if (tileh == SLOPE_FLAT ||
			((tileh == SLOPE_NE || tileh == SLOPE_SW) && axis == AXIS_X) ||
			((tileh == SLOPE_NW || tileh == SLOPE_SE) && axis == AXIS_Y)) return FOUNDATION_NONE;

	return (HasSlopeHighestCorner(tileh) ? InclinedFoundation(axis) : FlatteningFoundation(tileh));
}

/**
 * Determines if the track on a bridge ramp is flat or goes up/down.
 *
 * @param tileh Slope of the tile under the bridge head
 * @param axis Orientation of bridge
 * @return true iff the track is flat.
 */
bool HasBridgeFlatRamp(Slope tileh, Axis axis)
{
	ApplyFoundationToSlope(GetBridgeFoundation(tileh, axis), &tileh);
	/* If the foundation slope is flat the bridge has a non-flat ramp and vice versa. */
	return (tileh != SLOPE_FLAT);
}

static inline const PalSpriteID *GetBridgeSpriteTable(int index, BridgePieces table)
{
	const BridgeSpec *bridge = GetBridgeSpec(index);
	assert(table < BRIDGE_PIECE_INVALID);
	if (bridge->sprite_table == nullptr || bridge->sprite_table[table] == nullptr) {
		return _bridge_sprite_table[index][table];
	} else {
		return bridge->sprite_table[table];
	}
}


/**
 * Determines the foundation for the bridge head, and tests if the resulting slope is valid.
 *
 * @param bridge_piece Direction of the bridge head.
 * @param axis Axis of the bridge
 * @param tileh Slope of the tile under the north bridge head; returns slope on top of foundation
 * @param z TileZ corresponding to tileh, gets modified as well
 * @return Error or cost for bridge foundation
 */
static CommandCost CheckBridgeSlope(BridgePieces bridge_piece, Axis axis, Slope *tileh, int *z)
{
	assert(bridge_piece == BRIDGE_PIECE_NORTH || bridge_piece == BRIDGE_PIECE_SOUTH);

	Foundation f = GetBridgeFoundation(*tileh, axis);
	*z += ApplyFoundationToSlope(f, tileh);

	Slope valid_inclined;
	if (bridge_piece == BRIDGE_PIECE_NORTH) {
		valid_inclined = (axis == AXIS_X ? SLOPE_NE : SLOPE_NW);
	} else {
		valid_inclined = (axis == AXIS_X ? SLOPE_SW : SLOPE_SE);
	}
	if ((*tileh != SLOPE_FLAT) && (*tileh != valid_inclined)) return CMD_ERROR;

	if (f == FOUNDATION_NONE) return CommandCost();

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
}

/**
 * Is a bridge of the specified type and length available?
 * @param bridge_type Wanted type of bridge.
 * @param bridge_len  Wanted length of the bridge.
 * @param flags       Type of operation.
 * @return A succeeded (the requested bridge is available) or failed (it cannot be built) command.
 */
CommandCost CheckBridgeAvailability(BridgeType bridge_type, uint bridge_len, DoCommandFlag flags)
{
	if (flags & DC_QUERY_COST) {
		if (bridge_len <= _settings_game.construction.max_bridge_length) return CommandCost();
		return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
	}

	if (bridge_type >= MAX_BRIDGES) return CMD_ERROR;

	const BridgeSpec *b = GetBridgeSpec(bridge_type);
	if (b->avail_year > _cur_year) return CMD_ERROR;

	uint max = min(b->max_length, _settings_game.construction.max_bridge_length);

	if (b->min_length > bridge_len) return CMD_ERROR;
	if (bridge_len <= max) return CommandCost();
	return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
}

/**
 * Build a Bridge
 * @param end_tile end tile
 * @param flags type of operation
 * @param p1 packed start tile coords (~ dx)
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0- 7) - bridge type (hi bh)
 * - p2 = (bit  8-13) - rail type or road types.
 * - p2 = (bit 15-16) - transport type.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildBridge(TileIndex end_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID company = _current_company;

	RailType railtype = INVALID_RAILTYPE;
	RoadType roadtype = INVALID_ROADTYPE;

	/* unpack parameters */
	BridgeType bridge_type = GB(p2, 0, 8);

	if (!IsValidTile(p1)) return_cmd_error(STR_ERROR_BRIDGE_THROUGH_MAP_BORDER);

	TransportType transport_type = Extract<TransportType, 15, 2>(p2);

	/* type of bridge */
	switch (transport_type) {
		case TRANSPORT_ROAD:
			roadtype = Extract<RoadType, 8, 6>(p2);
			if (!ValParamRoadType(roadtype)) return CMD_ERROR;
			break;

		case TRANSPORT_RAIL:
			railtype = Extract<RailType, 8, 6>(p2);
			if (!ValParamRailtype(railtype)) return CMD_ERROR;
			break;

		case TRANSPORT_WATER:
			break;

		default:
			/* Airports don't have bridges. */
			return CMD_ERROR;
	}
	TileIndex tile_start = p1;
	TileIndex tile_end = end_tile;

	if (company == OWNER_DEITY) {
		if (transport_type != TRANSPORT_ROAD) return CMD_ERROR;
		const Town *town = CalcClosestTownFromTile(tile_start);

		company = OWNER_TOWN;

		/* If we are not within a town, we are not owned by the town */
		if (town == nullptr || DistanceSquare(tile_start, town->xy) > town->cache.squared_town_zone_radius[HZB_TOWN_EDGE]) {
			company = OWNER_NONE;
		}
	}

	if (tile_start == tile_end) {
		return_cmd_error(STR_ERROR_CAN_T_START_AND_END_ON);
	}

	Axis direction;
	if (TileX(tile_start) == TileX(tile_end)) {
		direction = AXIS_Y;
	} else if (TileY(tile_start) == TileY(tile_end)) {
		direction = AXIS_X;
	} else {
		return_cmd_error(STR_ERROR_START_AND_END_MUST_BE_IN);
	}

	if (tile_end < tile_start) Swap(tile_start, tile_end);

	uint bridge_len = GetTunnelBridgeLength(tile_start, tile_end);
	if (transport_type != TRANSPORT_WATER) {
		/* set and test bridge length, availability */
		CommandCost ret = CheckBridgeAvailability(bridge_type, bridge_len, flags);
		if (ret.Failed()) return ret;
	} else {
		if (bridge_len > _settings_game.construction.max_bridge_length) return_cmd_error(STR_ERROR_BRIDGE_TOO_LONG);
	}

	int z_start;
	int z_end;
	Slope tileh_start = GetTileSlope(tile_start, &z_start);
	Slope tileh_end = GetTileSlope(tile_end, &z_end);

	CommandCost terraform_cost_north = CheckBridgeSlope(BRIDGE_PIECE_NORTH, direction, &tileh_start, &z_start);
	CommandCost terraform_cost_south = CheckBridgeSlope(BRIDGE_PIECE_SOUTH, direction, &tileh_end,   &z_end);

	/* Aqueducts can't be built of flat land. */
	if (transport_type == TRANSPORT_WATER && (tileh_start == SLOPE_FLAT || tileh_end == SLOPE_FLAT)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
	if (z_start != z_end) return_cmd_error(STR_ERROR_BRIDGEHEADS_NOT_SAME_HEIGHT);

	CommandCost cost(EXPENSES_CONSTRUCTION);
	Owner owner;
	bool is_new_owner;
	bool is_upgrade = false;
	if (IsBridgeTile(tile_start) && IsBridgeTile(tile_end) &&
			GetOtherBridgeEnd(tile_start) == tile_end &&
			GetTunnelBridgeTransportType(tile_start) == transport_type) {
		/* Replace a current bridge. */

		/* If this is a railway bridge, make sure the railtypes match. */
		if (transport_type == TRANSPORT_RAIL && GetRailType(tile_start) != railtype) {
			return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
		}

		/* If this is a road bridge, make sure the roadtype matches. */
		if (transport_type == TRANSPORT_ROAD) {
			RoadType start_existing_rt = GetRoadType(tile_start, GetRoadTramType(roadtype));
			RoadType end_existing_rt = GetRoadType(tile_end, GetRoadTramType(roadtype));
			if ((start_existing_rt != roadtype && start_existing_rt != INVALID_ROADTYPE) || (end_existing_rt != roadtype && end_existing_rt != INVALID_ROADTYPE)) {
				return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
			}
		}

		/* Do not replace town bridges with lower speed bridges, unless in scenario editor. */
		if (!(flags & DC_QUERY_COST) && IsTileOwner(tile_start, OWNER_TOWN) &&
				GetBridgeSpec(bridge_type)->speed < GetBridgeSpec(GetBridgeType(tile_start))->speed &&
				_game_mode != GM_EDITOR) {
			Town *t = ClosestTownFromTile(tile_start, UINT_MAX);

			if (t == nullptr) {
				return CMD_ERROR;
			} else {
				SetDParam(0, t->index);
				return_cmd_error(STR_ERROR_LOCAL_AUTHORITY_REFUSES_TO_ALLOW_THIS);
			}
		}

		/* Do not replace the bridge with the same bridge type. */
		if (!(flags & DC_QUERY_COST) && (bridge_type == GetBridgeType(tile_start)) && (transport_type != TRANSPORT_ROAD || (GetRoadType(tile_start, GetRoadTramType(roadtype)) == roadtype && GetRoadType(tile_end, GetRoadTramType(roadtype)) == roadtype))) {
			return_cmd_error(STR_ERROR_ALREADY_BUILT);
		}

		/* Do not allow replacing another company's bridges. */
		if (!IsTileOwner(tile_start, company) && !IsTileOwner(tile_start, OWNER_TOWN) && !IsTileOwner(tile_start, OWNER_NONE)) {
			return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER);
		}

		cost.AddCost((bridge_len + 1) * _price[PR_CLEAR_BRIDGE]); // The cost of clearing the current bridge.
		owner = GetTileOwner(tile_start);

		/* If bridge belonged to bankrupt company, it has a new owner now */
		is_new_owner = (owner == OWNER_NONE);
		if (is_new_owner) owner = company;

		TileIndexDiff delta = (direction == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));
		for (TileIndex tile = tile_start + delta; tile != tile_end; tile += delta) {
			if (IsTileType(tile, MP_STATION)) {
				switch (GetStationType(tile)) {
					case STATION_RAIL:
					case STATION_WAYPOINT: {
						CommandCost ret = IsRailStationBridgeAboveOk(tile, GetStationSpec(tile), GetStationGfx(tile), tile_start, tile_end, z_start + 1, bridge_type, transport_type);
						if (ret.Failed()) {
							if (ret.GetErrorMessage() != INVALID_STRING_ID) return ret;
							ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
							if (ret.Failed()) return ret;
						}
						break;
					}

					case STATION_BUS:
					case STATION_TRUCK: {
						CommandCost ret = IsRoadStopBridgeAboveOK(tile, IsDriveThroughStopTile(tile), GetRoadStopDir(tile),
								tile_start, tile_end, z_start + 1, bridge_type, transport_type);
						if (ret.Failed()) {
							if (ret.GetErrorMessage() != INVALID_STRING_ID) return ret;
							ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
							if (ret.Failed()) return ret;
						}
						break;
					}

					case STATION_BUOY:
						/* Buoys are always allowed */
						break;

					default:
						if (!(GetStationType(tile) == STATION_DOCK && _settings_game.construction.allow_docks_under_bridges)) {
							CommandCost ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
							if (ret.Failed()) return ret;
						}
						break;
				}
			}
		}

		is_upgrade = true;
	} else {
		/* Build a new bridge. */

		bool allow_on_slopes = (_settings_game.construction.build_on_slopes && transport_type != TRANSPORT_WATER);

		/* Try and clear the start landscape */
		CommandCost ret = DoCommand(tile_start, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost = ret;

		if (terraform_cost_north.Failed() || (terraform_cost_north.GetCost() != 0 && !allow_on_slopes)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		cost.AddCost(terraform_cost_north);

		/* Try and clear the end landscape */
		ret = DoCommand(tile_end, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);

		/* false - end tile slope check */
		if (terraform_cost_south.Failed() || (terraform_cost_south.GetCost() != 0 && !allow_on_slopes)) return_cmd_error(STR_ERROR_LAND_SLOPED_IN_WRONG_DIRECTION);
		cost.AddCost(terraform_cost_south);

		const TileIndex heads[] = {tile_start, tile_end};
		for (int i = 0; i < 2; i++) {
			if (IsBridgeAbove(heads[i])) {
				TileIndex north_head = GetNorthernBridgeEnd(heads[i]);

				if (direction == GetBridgeAxis(heads[i])) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);

				if (z_start + 1 == GetBridgeHeight(north_head)) {
					return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
				}
			}
		}

		TileIndexDiff delta = (direction == AXIS_X ? TileDiffXY(1, 0) : TileDiffXY(0, 1));
		for (TileIndex tile = tile_start + delta; tile != tile_end; tile += delta) {
			if (GetTileMaxZ(tile) > z_start) return_cmd_error(STR_ERROR_BRIDGE_TOO_LOW_FOR_TERRAIN);

			if (z_start >= (GetTileZ(tile) + _settings_game.construction.max_bridge_height)) {
				/*
				 * Disallow too high bridges.
				 * Properly rendering a map where very high bridges (might) exist is expensive.
				 * See http://www.tt-forums.net/viewtopic.php?f=33&t=40844&start=980#p1131762
				 * for a detailed discussion. z_start here is one heightlevel below the bridge level.
				 */
				return_cmd_error(STR_ERROR_BRIDGE_TOO_HIGH_FOR_TERRAIN);
			}

			if (IsBridgeAbove(tile)) {
				/* Disallow crossing bridges for the time being */
				return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
			}

			switch (GetTileType(tile)) {
				case MP_WATER:
					if (!IsWater(tile) && !IsCoast(tile)) goto not_valid_below;
					break;

				case MP_RAILWAY:
					if (!IsPlainRail(tile)) goto not_valid_below;
					break;

				case MP_ROAD:
					if (IsRoadDepot(tile)) goto not_valid_below;
					break;

				case MP_TUNNELBRIDGE:
					if (IsTunnel(tile)) break;
					if (direction == DiagDirToAxis(GetTunnelBridgeDirection(tile))) goto not_valid_below;
					if (z_start < GetBridgeHeight(tile)) goto not_valid_below;
					break;

				case MP_OBJECT: {
					if (_settings_game.construction.allow_grf_objects_under_bridges && GetObjectType(tile) >= NEW_OBJECT_OFFSET) break;
					const ObjectSpec *spec = ObjectSpec::GetByTile(tile);
					if ((spec->flags & OBJECT_FLAG_ALLOW_UNDER_BRIDGE) == 0) goto not_valid_below;
					if (GetTileMaxZ(tile) + spec->height > z_start) goto not_valid_below;
					break;
				}

				case MP_STATION: {
					switch (GetStationType(tile)) {
						case STATION_AIRPORT:
							goto not_valid_below;

						case STATION_RAIL:
						case STATION_WAYPOINT: {
							CommandCost ret = IsRailStationBridgeAboveOk(tile, GetStationSpec(tile), GetStationGfx(tile), tile_start, tile_end, z_start + 1, bridge_type, transport_type);
							if (ret.Failed()) {
								if (ret.GetErrorMessage() != INVALID_STRING_ID) return ret;
								goto not_valid_below;
							}
							break;
						}

						case STATION_BUS:
						case STATION_TRUCK: {
							CommandCost ret = IsRoadStopBridgeAboveOK(tile, IsDriveThroughStopTile(tile), GetRoadStopDir(tile),
									tile_start, tile_end, z_start + 1, bridge_type, transport_type);
							if (ret.Failed()) {
								if (ret.GetErrorMessage() != INVALID_STRING_ID) return ret;
								goto not_valid_below;
							}
							break;
						}

						case STATION_BUOY:
							/* Buoys are always allowed */
							break;

						default:
							if (!(GetStationType(tile) == STATION_DOCK && _settings_game.construction.allow_docks_under_bridges)) goto not_valid_below;
							break;
					}
					break;
				}

				case MP_CLEAR:
					break;

				default:
	not_valid_below:;
					/* try and clear the middle landscape */
					ret = DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
					if (ret.Failed()) return ret;
					cost.AddCost(ret);
					break;
			}

			if (flags & DC_EXEC) {
				/* We do this here because when replacing a bridge with another
				 * type calling SetBridgeMiddle isn't needed. After all, the
				 * tile already has the has_bridge_above bits set. */
				SetBridgeMiddle(tile, direction);
			}
		}

		owner = company;
		is_new_owner = true;
	}

	/* do the drill? */
	if (flags & DC_EXEC) {
		DiagDirection dir = AxisToDiagDir(direction);

		Company *c = Company::GetIfValid(company);
		switch (transport_type) {
			case TRANSPORT_RAIL:
				if (is_upgrade) SubtractRailTunnelBridgeInfrastructure(tile_start, tile_end);
				/* Add to company infrastructure count if required. */
				MakeRailBridgeRamp(tile_start, owner, bridge_type, dir,                 railtype, is_upgrade);
				MakeRailBridgeRamp(tile_end,   owner, bridge_type, ReverseDiagDir(dir), railtype, is_upgrade);
				AddRailTunnelBridgeInfrastructure(tile_start, tile_end);
				break;

			case TRANSPORT_ROAD: {
				if (is_upgrade) SubtractRoadTunnelBridgeInfrastructure(tile_start, tile_end);
				auto make_bridge_ramp = [company, owner, is_upgrade, is_new_owner, bridge_type, roadtype](TileIndex t, DiagDirection d) {
					RoadType road_rt = is_upgrade ? GetRoadTypeRoad(t) : INVALID_ROADTYPE;
					RoadType tram_rt = is_upgrade ? GetRoadTypeTram(t) : INVALID_ROADTYPE;
					bool hasroad = road_rt != INVALID_ROADTYPE;
					bool hastram = tram_rt != INVALID_ROADTYPE;
					if (RoadTypeIsRoad(roadtype)) road_rt = roadtype;
					if (RoadTypeIsTram(roadtype)) tram_rt = roadtype;
					if (is_new_owner) {
						/* Also give unowned present roadtypes to new owner */
						if (hasroad && GetRoadOwner(t, RTT_ROAD) == OWNER_NONE) hasroad = false;
						if (hastram && GetRoadOwner(t, RTT_TRAM) == OWNER_NONE) hastram = false;
					}

					Owner owner_road = hasroad ? GetRoadOwner(t, RTT_ROAD) : company;
					Owner owner_tram = hastram ? GetRoadOwner(t, RTT_TRAM) : company;
					MakeRoadBridgeRamp(t, owner, owner_road, owner_tram, bridge_type, d, road_rt, tram_rt, is_upgrade);
					if (is_upgrade) {
						if (road_rt != INVALID_ROADTYPE) SetCustomBridgeHeadRoadBits(t, RTT_ROAD, GetCustomBridgeHeadRoadBits(t, RTT_ROAD) | DiagDirToRoadBits(d));
						if (tram_rt != INVALID_ROADTYPE) SetCustomBridgeHeadRoadBits(t, RTT_TRAM, GetCustomBridgeHeadRoadBits(t, RTT_TRAM) | DiagDirToRoadBits(d));
					}
				};
				make_bridge_ramp(tile_start, dir);
				make_bridge_ramp(tile_end, ReverseDiagDir(dir));
				AddRoadTunnelBridgeInfrastructure(tile_start, tile_end);
				if (IsRoadCustomBridgeHead(tile_start) || IsRoadCustomBridgeHead(tile_end)) {
					NotifyRoadLayoutChanged();
				} else {
					NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(tile_start, tile_end, dir, GetRoadTramType(roadtype));
				}
				break;
			}

			case TRANSPORT_WATER:
				if (is_new_owner && c != nullptr) c->infrastructure.water += (bridge_len + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR;
				MakeAqueductBridgeRamp(tile_start, owner, dir);
				MakeAqueductBridgeRamp(tile_end,   owner, ReverseDiagDir(dir));
				CheckForDockingTile(tile_start);
				CheckForDockingTile(tile_end);
				break;

			default:
				NOT_REACHED();
		}

		/* Mark all tiles dirty */
		MarkBridgeDirty(tile_start, tile_end, AxisToDiagDir(direction), z_start);
		DirtyCompanyInfrastructureWindows(company);
	}

	if ((flags & DC_EXEC) && transport_type == TRANSPORT_RAIL) {
		Track track = AxisToTrack(direction);
		AddSideToSignalBuffer(tile_start, INVALID_DIAGDIR, company);
		YapfNotifyTrackLayoutChange(tile_start, track);
	}

	/* Human players that build bridges get a selection to choose from (DC_QUERY_COST)
	 * It's unnecessary to execute this command every time for every bridge.
	 * So it is done only for humans and cost is computed in bridge_gui.cpp.
	 * For (non-spectated) AI, Towns this has to be of course calculated. */
	Company *c = Company::GetIfValid(company);
	if (!(flags & DC_QUERY_COST) || (c != nullptr && c->is_ai && company != _local_company)) {
		bridge_len += 2; // begin and end tiles/ramps

		switch (transport_type) {
			case TRANSPORT_ROAD: {
				cost.AddCost(bridge_len * 2 * RoadBuildCost(roadtype));
				if (is_upgrade && DiagDirToRoadBits(GetTunnelBridgeDirection(tile_start)) & GetCustomBridgeHeadRoadBits(tile_start, OtherRoadTramType(GetRoadTramType(roadtype)))) {
					cost.AddCost(bridge_len * 2 * RoadBuildCost(GetRoadType(tile_start, OtherRoadTramType(GetRoadTramType(roadtype)))));
				}
				break;
			}

			case TRANSPORT_RAIL: cost.AddCost(bridge_len * RailBuildCost(railtype)); break;
			default: break;
		}

		if (c != nullptr) bridge_len = CalcBridgeLenCostFactor(bridge_len);

		if (transport_type != TRANSPORT_WATER) {
			cost.AddCost((int64)bridge_len * _price[PR_BUILD_BRIDGE] * GetBridgeSpec(bridge_type)->price >> 8);
		} else {
			/* Aqueducts use a separate base cost. */
			cost.AddCost((int64)bridge_len * _price[PR_BUILD_AQUEDUCT]);
		}

	}

	return cost;
}

/**
 * Check if the amount of tiles of the chunnel ramp is between allowed limits.
 * @param tile the actual tile.
 * @param ramp ramp_start tile.
 * @param delta the tile offset.
 * @return an empty string if between limits or a formatted string for the error message.
 */
static inline StringID IsRampBetweenLimits(TileIndex ramp_start, TileIndex tile, TileIndexDiff delta)
{
	uint min_length = 4;
	uint max_length = 7;
	if (Delta(ramp_start, tile) < (uint)abs(delta) * min_length || (uint)abs(delta) * max_length < Delta(ramp_start, tile)) {
		/* Add 1 in message to have consistency with cursor count in game. */
		SetDParam(0, max_length + 1);
		return STR_ERROR_CHUNNEL_RAMP;
	}

	return STR_NULL;
}

/**
 * See if chunnel building is possible.
 * All chunnel related issues are tucked away in one procedure
 * @pre   only on z level 0.
 * @param tile start tile of tunnel.
 * @param direction the direction we want to build.
 * @param is_chunnel pointer to set if chunnel is allowed or not.
 * @param sea_tiles pointer for the amount of tiles used to cross a sea.
 * @return an error message or if success the is_chunnel flag is set to true and the amount of tiles needed to cross the water is returned.
 */
static inline CommandCost CanBuildChunnel(TileIndex tile, DiagDirection direction, bool &is_chunnel, int &sea_tiles)
{
	const int start_z = 0;
	bool crossed_sea = false;
	TileIndex ramp_start = tile;

	if (GetTileZ(tile) > 0) return_cmd_error(STR_ERROR_CHUNNEL_ONLY_OVER_SEA);

	const TileIndexDiff delta = TileOffsByDiagDir(direction);
	for (;;) {
		tile += delta;
		if (!IsValidTile(tile)) return_cmd_error(STR_ERROR_CHUNNEL_THROUGH_MAP_BORDER);
		_build_tunnel_endtile = tile;
		int end_z;
		Slope end_tileh = GetTileSlope(tile, &end_z);

		if (start_z == end_z) {

			/* Handle chunnels only on sea level and only one time crossing. */
			if (!crossed_sea &&
					(IsCoastTile(tile) ||
					(IsValidTile(tile + delta) && HasTileWaterGround(tile + delta)) ||
					(IsValidTile(tile + delta * 2) && HasTileWaterGround(tile + delta * 2)))) {

				/* A shore was found, check if start ramp was too short or too long. */
				StringID err_msg = IsRampBetweenLimits(ramp_start, tile, delta);
				if (err_msg > STR_NULL) return_cmd_error(err_msg);

				/* Pass the water and find a proper shore tile that potentially
				 * could have a tunnel portal behind. */
				for (;;) {
					end_tileh = GetTileSlope(tile);
					if (direction == DIAGDIR_NE && (end_tileh & SLOPE_NE) == SLOPE_NE) break;
					if (direction == DIAGDIR_SE && (end_tileh & SLOPE_SE) == SLOPE_SE) break;
					if (direction == DIAGDIR_SW && (end_tileh & SLOPE_SW) == SLOPE_SW) break;
					if (direction == DIAGDIR_NW && (end_tileh & SLOPE_NW) == SLOPE_NW) break;

					/* No drilling under oil rigs.*/
					if ((IsTileType(tile, MP_STATION) && IsOilRig(tile)) ||
							(IsTileType(tile, MP_INDUSTRY)               &&
							GetIndustryGfx(tile) >= GFX_OILRIG_1         &&
							GetIndustryGfx(tile) <= GFX_OILRIG_5)) return_cmd_error(STR_ERROR_NO_DRILLING_ABOVE_CHUNNEL);

					if (IsTileType(tile, MP_WATER) && IsSea(tile)) crossed_sea = true;
					if (!_cheats.crossing_tunnels.value && IsTunnelInWay(tile, start_z)) return_cmd_error(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);

					tile += delta;
					if (!IsValidTile(tile)) return_cmd_error(STR_ERROR_CHUNNEL_THROUGH_MAP_BORDER);
					_build_tunnel_endtile = tile;
					sea_tiles++;
				}
				if (!crossed_sea) return_cmd_error(STR_ERROR_CHUNNEL_ONLY_OVER_SEA);
				ramp_start = tile;
			} else {
				/* Check if end ramp was too short or too long after crossing the sea. */
				if (crossed_sea) {
					StringID err_msg = IsRampBetweenLimits(ramp_start, tile, delta);
					if (err_msg > STR_NULL) return_cmd_error(err_msg);
				}

				break;
			}
		}
		if (!_cheats.crossing_tunnels.value && IsTunnelInWay(tile, start_z)) return_cmd_error(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);
	}
	is_chunnel = crossed_sea;

	return CommandCost();
}

/**
 * Build Tunnel.
 * @param start_tile start tile of tunnel
 * @param flags type of operation
 * @param p1 bit 0-5 railtype or roadtype
 *           bit 8-9 transport type
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildTunnel(TileIndex start_tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID company = _current_company;

	TransportType transport_type = Extract<TransportType, 8, 2>(p1);
	RailType railtype = INVALID_RAILTYPE;
	RoadType roadtype = INVALID_ROADTYPE;
	_build_tunnel_endtile = 0;
	switch (transport_type) {
		case TRANSPORT_RAIL:
			railtype = Extract<RailType, 0, 6>(p1);
			if (!ValParamRailtype(railtype)) return CMD_ERROR;
			break;

		case TRANSPORT_ROAD:
			roadtype = Extract<RoadType, 0, 6>(p1);
			if (!ValParamRoadType(roadtype)) return CMD_ERROR;
			break;

		default: return CMD_ERROR;
	}

	if (company == OWNER_DEITY) {
		if (transport_type != TRANSPORT_ROAD) return CMD_ERROR;
		const Town *town = CalcClosestTownFromTile(start_tile);

		company = OWNER_TOWN;

		/* If we are not within a town, we are not owned by the town */
		if (town == nullptr || DistanceSquare(start_tile, town->xy) > town->cache.squared_town_zone_radius[HZB_TOWN_EDGE]) {
			company = OWNER_NONE;
		}
	}

	int start_z;
	int end_z;
	Slope start_tileh = GetTileSlope(start_tile, &start_z);
	DiagDirection direction = GetInclinedSlopeDirection(start_tileh);
	if (direction == INVALID_DIAGDIR) return_cmd_error(STR_ERROR_SITE_UNSUITABLE_FOR_TUNNEL);

	if (HasTileWaterGround(start_tile)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);

	CommandCost ret = DoCommand(start_tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return ret;

	/* XXX - do NOT change 'ret' in the loop, as it is used as the price
	 * for the clearing of the entrance of the tunnel. Assigning it to
	 * cost before the loop will yield different costs depending on start-
	 * position, because of increased-cost-by-length: 'cost += cost >> 3' */

	TileIndexDiff delta = TileOffsByDiagDir(direction);

	TileIndex end_tile = start_tile;

	/* Tile shift coefficient. Will decrease for very long tunnels to avoid exponential growth of price*/
	int tiles_coef = 3;
	/* Number of tiles from start of tunnel */
	int tiles = 0;
	/* Number of tiles at which the cost increase coefficient per tile is halved */
	int tiles_bump = 25;
	/* flags for chunnels. */
	bool is_chunnel = false;
	bool crossed_sea = false;
	/* Number of tiles counted for crossing sea */
	int sea_tiles = 0;

	if (start_z == 0 && _settings_game.construction.chunnel) {
		CommandCost chunnel_test = CanBuildChunnel(start_tile, direction, is_chunnel, sea_tiles);
		if (chunnel_test.Failed()) return chunnel_test;
	}

	Slope end_tileh;
	for (;;) {
		end_tile += delta;
		if (!IsValidTile(end_tile)) return_cmd_error(STR_ERROR_TUNNEL_THROUGH_MAP_BORDER);
		end_tileh = GetTileSlope(end_tile, &end_z);

		if (start_z == end_z) {
			if (is_chunnel && !crossed_sea){
				end_tile += sea_tiles * delta;
				tiles += sea_tiles;
				crossed_sea = true;
			} else {
				break;
			}
		}
		if (!_cheats.crossing_tunnels.value && IsTunnelInWay(end_tile, start_z)) {
			_build_tunnel_endtile = end_tile;
			return_cmd_error(STR_ERROR_ANOTHER_TUNNEL_IN_THE_WAY);
		}
		tiles++;
	}
	/* The cost of the digging. */
	CommandCost cost(EXPENSES_CONSTRUCTION);
	for (int i = 1; i <= tiles; i++) {
		if (i == tiles_bump) {
			tiles_coef++;
			tiles_bump *= 2;
		}

		cost.AddCost(_price[PR_BUILD_TUNNEL]);
		cost.AddCost(cost.GetCost() >> tiles_coef); // add a multiplier for longer tunnels
	}

	/* Add the cost of the entrance */
	cost.AddCost(_price[PR_BUILD_TUNNEL]);
	cost.AddCost(ret);

	/* if the command fails from here on we want the end tile to be highlighted */
	_build_tunnel_endtile = end_tile;

	if (tiles > _settings_game.construction.max_tunnel_length) return_cmd_error(STR_ERROR_TUNNEL_TOO_LONG);

	if (HasTileWaterGround(end_tile)) return_cmd_error(STR_ERROR_CAN_T_BUILD_ON_WATER);

	/* Clear the tile in any case */
	ret = DoCommand(end_tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
	if (ret.Failed()) return_cmd_error(STR_ERROR_UNABLE_TO_EXCAVATE_LAND);
	cost.AddCost(ret);

	/* slope of end tile must be complementary to the slope of the start tile */
	if (end_tileh != ComplementSlope(start_tileh)) {
		/* Mark the tile as already cleared for the terraform command.
		 * Do this for all tiles (like trees), not only objects. */
		ClearedObjectArea *coa = FindClearedObject(end_tile);
		if (coa == nullptr) {
			/*C++17: coa = &*/ _cleared_object_areas.push_back({end_tile, TileArea(end_tile, 1, 1)});
			coa = &_cleared_object_areas.back();
		}

		/* Hide the tile from the terraforming command */
		TileIndex old_first_tile = coa->first_tile;
		coa->first_tile = INVALID_TILE;

		/* CMD_TERRAFORM_LAND may append further items to _cleared_object_areas,
		 * however it will never erase or re-order existing items.
		 * _cleared_object_areas is a value-type SmallVector, therefore appending items
		 * may result in a backing-store re-allocation, which would invalidate the coa pointer.
		 * The index of the coa pointer into the _cleared_object_areas vector remains valid,
		 * and can be used safely after the CMD_TERRAFORM_LAND operation.
		 * Deliberately clear the coa pointer to avoid leaving dangling pointers which could
		 * inadvertently be dereferenced.
		 */
		ClearedObjectArea *begin = _cleared_object_areas.data();
		assert(coa >= begin && coa < begin + _cleared_object_areas.size());
		size_t coa_index = coa - begin;
		assert(coa_index < UINT_MAX); // more than 2**32 cleared areas would be a bug in itself
		coa = nullptr;

		ret = DoCommand(end_tile, end_tileh & start_tileh, 0, flags, CMD_TERRAFORM_LAND);
		_cleared_object_areas[(uint)coa_index].first_tile = old_first_tile;
		if (ret.Failed()) return_cmd_error(STR_ERROR_UNABLE_TO_EXCAVATE_LAND);
		cost.AddCost(ret);
	}
	cost.AddCost(_price[PR_BUILD_TUNNEL]);

	/* Pay for the rail/road in the tunnel including entrances */
	switch (transport_type) {
		case TRANSPORT_ROAD: cost.AddCost((tiles + 2) * RoadBuildCost(roadtype) * 2); break;
		case TRANSPORT_RAIL: cost.AddCost((tiles + 2) * RailBuildCost(railtype)); break;
		default: NOT_REACHED();
	}

	if (is_chunnel) cost.MultiplyCost(2);

	if (flags & DC_EXEC) {
		Company *c = Company::GetIfValid(company);
		uint num_pieces = (tiles + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR;

		/* The most northern tile first. */
		TileIndex tn = start_tile;
		TileIndex ts = end_tile;
		if(start_tile > end_tile) Swap(tn, ts);

		if (!Tunnel::CanAllocateItem()) return_cmd_error(STR_ERROR_TUNNEL_TOO_MANY);
		const int height = TileHeight(tn);
		const Tunnel *t = new Tunnel(tn, ts, height, is_chunnel);
		ViewportMapStoreTunnel(tn, ts, height, true);

		if (transport_type == TRANSPORT_RAIL) {
			if (!IsTunnelTile(start_tile) && c != nullptr) c->infrastructure.rail[railtype] += num_pieces;
			MakeRailTunnel(start_tile, company, t->index, direction,                 railtype);
			MakeRailTunnel(end_tile,   company, t->index, ReverseDiagDir(direction), railtype);
			AddSideToSignalBuffer(start_tile, INVALID_DIAGDIR, company);
			YapfNotifyTrackLayoutChange(start_tile, DiagDirToDiagTrack(direction));
		} else {
			if (c != nullptr) c->infrastructure.road[roadtype] += num_pieces * 2; // A full diagonal road has two road bits.
			NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(start_tile, end_tile, direction, GetRoadTramType(roadtype));
			RoadType road_rt = RoadTypeIsRoad(roadtype) ? roadtype : INVALID_ROADTYPE;
			RoadType tram_rt = RoadTypeIsTram(roadtype) ? roadtype : INVALID_ROADTYPE;
			MakeRoadTunnel(start_tile, company, t->index, direction,                 road_rt, tram_rt);
			MakeRoadTunnel(end_tile,   company, t->index, ReverseDiagDir(direction), road_rt, tram_rt);
		}
		DirtyCompanyInfrastructureWindows(company);
	}

	return cost;
}


/**
 * Are we allowed to remove the tunnel or bridge at \a tile?
 * @param tile End point of the tunnel or bridge.
 * @return A succeeded command if the tunnel or bridge may be removed, a failed command otherwise.
 */
static inline CommandCost CheckAllowRemoveTunnelBridge(TileIndex tile)
{
	/* Floods can remove anything as well as the scenario editor */
	if (_current_company == OWNER_WATER || _game_mode == GM_EDITOR) return CommandCost();

	switch (GetTunnelBridgeTransportType(tile)) {
		case TRANSPORT_ROAD: {
			RoadType road_rt = GetRoadTypeRoad(tile);
			RoadType tram_rt = GetRoadTypeTram(tile);
			Owner road_owner = _current_company;
			Owner tram_owner = _current_company;

			if (road_rt != INVALID_ROADTYPE) road_owner = GetRoadOwner(tile, RTT_ROAD);
			if (tram_rt != INVALID_ROADTYPE) tram_owner = GetRoadOwner(tile, RTT_TRAM);

			/* We can remove unowned road and if the town allows it */
			if (road_owner == OWNER_TOWN && _current_company != OWNER_TOWN && !(_settings_game.construction.extra_dynamite || _cheats.magic_bulldozer.value)) {
				/* Town does not allow */
				return CheckTileOwnership(tile);
			}
			if (road_owner == OWNER_NONE || road_owner == OWNER_TOWN) road_owner = _current_company;
			if (tram_owner == OWNER_NONE) tram_owner = _current_company;

			CommandCost ret = CheckOwnership(road_owner, tile);
			if (ret.Succeeded()) ret = CheckOwnership(tram_owner, tile);
			return ret;
		}

		case TRANSPORT_RAIL:
			return CheckOwnership(GetTileOwner(tile));

		case TRANSPORT_WATER: {
			/* Always allow to remove aqueducts without owner. */
			Owner aqueduct_owner = GetTileOwner(tile);
			if (aqueduct_owner == OWNER_NONE) aqueduct_owner = _current_company;
			return CheckOwnership(aqueduct_owner);
		}

		default: NOT_REACHED();
	}
}

/**
 * Remove a tunnel from the game, update town rating, etc.
 * @param tile Tile containing one of the endpoints of the tunnel.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost DoClearTunnel(TileIndex tile, DoCommandFlag flags)
{
	CommandCost ret = CheckAllowRemoveTunnelBridge(tile);
	if (ret.Failed()) return ret;

	const Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(tile));
	TileIndex endtile = GetOtherTunnelEnd(tile);

	ret = TunnelBridgeIsFree(tile, endtile);
	if (ret.Failed()) return ret;

	_build_tunnel_endtile = endtile;

	Town *t = nullptr;
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		t = ClosestTownFromTile(tile, UINT_MAX); // town penalty rating

		/* Check if you are allowed to remove the tunnel owned by a town
		 * Removal depends on difficulty settings */
		CommandCost ret = CheckforTownRating(flags, t, TUNNELBRIDGE_REMOVE);
		if (ret.Failed()) return ret;
	}

	/* checks if the owner is town then decrease town rating by RATING_TUNNEL_BRIDGE_DOWN_STEP until
	 * you have a "Poor" (0) town rating */
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		ChangeTownRating(t, RATING_TUNNEL_BRIDGE_DOWN_STEP, RATING_TUNNEL_BRIDGE_MINIMUM, flags);
	}

	const bool is_chunnel = Tunnel::GetByTile(tile)->is_chunnel;

	uint len = GetTunnelBridgeLength(tile, endtile) + 2; // Don't forget the end tiles.

	if (flags & DC_EXEC) {
		if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
			/* We first need to request values before calling DoClearSquare */
			DiagDirection dir = GetTunnelBridgeDirection(tile);
			Track track = DiagDirToDiagTrack(dir);
			Owner owner = GetTileOwner(tile);

			Train *v = nullptr;
			if (HasTunnelReservation(tile)) {
				v = GetTrainForReservation(tile, track);
				if (v != nullptr) FreeTrainTrackReservation(v);
			}

			if (Company::IsValidID(owner)) {
				Company::Get(owner)->infrastructure.rail[GetRailType(tile)] -= len * TUNNELBRIDGE_TRACKBIT_FACTOR;
				if (IsTunnelBridgeWithSignalSimulation(tile)) { // handle tunnel/bridge signals.
					Company::Get(GetTileOwner(tile))->infrastructure.signal -= GetTunnelBridgeSignalSimulationSignalCount(tile, endtile);
				}
				DirtyCompanyInfrastructureWindows(owner);
			}

			delete Tunnel::GetByTile(tile);

			DoClearSquare(tile);
			DoClearSquare(endtile);

			/* cannot use INVALID_DIAGDIR for signal update because the tunnel doesn't exist anymore */
			AddSideToSignalBuffer(tile,    ReverseDiagDir(dir), owner);
			AddSideToSignalBuffer(endtile, dir,                 owner);

			YapfNotifyTrackLayoutChange(tile,    track);
			YapfNotifyTrackLayoutChange(endtile, track);

			if (v != nullptr) TryPathReserve(v);
		} else {
			/* A full diagonal road tile has two road bits. */
			UpdateCompanyRoadInfrastructure(GetRoadTypeRoad(tile), GetRoadOwner(tile, RTT_ROAD), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));
			UpdateCompanyRoadInfrastructure(GetRoadTypeTram(tile), GetRoadOwner(tile, RTT_TRAM), -(int)(len * 2 * TUNNELBRIDGE_TRACKBIT_FACTOR));
			NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(tile, endtile, GetTunnelBridgeDirection(tile), RTT_ROAD);
			NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(tile, endtile, GetTunnelBridgeDirection(tile), RTT_TRAM);

			delete Tunnel::GetByTile(tile);

			DoClearSquare(tile);
			DoClearSquare(endtile);
		}
		ViewportMapInvalidateTunnelCacheByTile(tile < endtile ? tile : endtile, axis);
	}
	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_TUNNEL] * len * (is_chunnel ? 2 : 1));
}


/**
 * Remove a bridge from the game, update town rating, etc.
 * @param tile Tile containing one of the endpoints of the bridge.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost DoClearBridge(TileIndex tile, DoCommandFlag flags)
{
	CommandCost ret = CheckAllowRemoveTunnelBridge(tile);
	if (ret.Failed()) return ret;

	TileIndex endtile = GetOtherBridgeEnd(tile);

	ret = TunnelBridgeIsFree(tile, endtile);
	if (ret.Failed()) return ret;

	DiagDirection direction = GetTunnelBridgeDirection(tile);
	TileIndexDiff delta = TileOffsByDiagDir(direction);

	Town *t = nullptr;
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		t = ClosestTownFromTile(tile, UINT_MAX); // town penalty rating

		/* Check if you are allowed to remove the bridge owned by a town
		 * Removal depends on difficulty settings */
		CommandCost ret = CheckforTownRating(flags, t, TUNNELBRIDGE_REMOVE);
		if (ret.Failed()) return ret;
	}

	/* checks if the owner is town then decrease town rating by RATING_TUNNEL_BRIDGE_DOWN_STEP until
	 * you have a "Poor" (0) town rating */
	if (IsTileOwner(tile, OWNER_TOWN) && _game_mode != GM_EDITOR) {
		ChangeTownRating(t, RATING_TUNNEL_BRIDGE_DOWN_STEP, RATING_TUNNEL_BRIDGE_MINIMUM, flags);
	}

	CommandCost cost(EXPENSES_CONSTRUCTION);

	const bool rail = GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL;
	TrackBits tile_tracks = TRACK_BIT_NONE;
	TrackBits endtile_tracks = TRACK_BIT_NONE;
	if (rail) {
		tile_tracks = GetCustomBridgeHeadTrackBits(tile);
		endtile_tracks = GetCustomBridgeHeadTrackBits(endtile);
		cost.AddCost(RailClearCost(GetRailType(tile)) * (CountBits(GetPrimaryTunnelBridgeTrackBits(tile)) + CountBits(GetPrimaryTunnelBridgeTrackBits(endtile)) - 2));
		if (GetSecondaryTunnelBridgeTrackBits(tile)) cost.AddCost(RailClearCost(GetSecondaryRailType(tile)));
		if (GetSecondaryTunnelBridgeTrackBits(endtile)) cost.AddCost(RailClearCost(GetSecondaryRailType(endtile)));
	}

	Money base_cost = (GetTunnelBridgeTransportType(tile) != TRANSPORT_WATER) ? _price[PR_CLEAR_BRIDGE] : _price[PR_CLEAR_AQUEDUCT];
	uint middle_len = GetTunnelBridgeLength(tile, endtile);
	uint len = middle_len + 2; // Don't forget the end tiles.

	cost.AddCost(len * base_cost);

	if (flags & DC_EXEC) {
		/* read this value before actual removal of bridge */
		Owner owner = GetTileOwner(tile);
		int height = GetBridgeHeight(tile);
		std::vector<Train *> vehicles_affected;

		if (rail) {
			auto find_train_reservations = [&vehicles_affected](TileIndex tile) {
				TrackBits reserved = GetBridgeReservationTrackBits(tile);
				Track track;
				while ((track = RemoveFirstTrack(&reserved)) != INVALID_TRACK) {
					Train *v = GetTrainForReservation(tile, track);
					if (v != nullptr) {
						FreeTrainTrackReservation(v);
						vehicles_affected.push_back(v);
					}
				}
			};
			find_train_reservations(tile);
			find_train_reservations(endtile);
		}

		bool removetile = false;
		bool removeendtile = false;

		/* Update company infrastructure counts. */
		if (rail) {
			SubtractRailTunnelBridgeInfrastructure(tile, endtile);
		} else if (GetTunnelBridgeTransportType(tile) == TRANSPORT_ROAD) {
			SubtractRoadTunnelBridgeInfrastructure(tile, endtile);
			if (IsRoadCustomBridgeHead(tile) || IsRoadCustomBridgeHead(endtile)) {
				NotifyRoadLayoutChanged();
			} else {
				if (HasRoadTypeRoad(tile)) NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(tile, endtile, direction, RTT_ROAD);
				if (HasRoadTypeTram(tile)) NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(tile, endtile, direction, RTT_TRAM);
			}
		} else { // Aqueduct
			if (Company::IsValidID(owner)) Company::Get(owner)->infrastructure.water -= len * TUNNELBRIDGE_TRACKBIT_FACTOR;
			removetile    = IsDockingTile(tile);
			removeendtile = IsDockingTile(endtile);
		}
		DirtyAllCompanyInfrastructureWindows();

		if (IsTunnelBridgeSignalSimulationEntrance(tile)) ClearBridgeEntranceSimulatedSignals(tile);
		if (IsTunnelBridgeSignalSimulationEntrance(endtile)) ClearBridgeEntranceSimulatedSignals(endtile);

		DoClearSquare(tile);
		DoClearSquare(endtile);

		if (removetile)    RemoveDockingTile(tile);
		if (removeendtile) RemoveDockingTile(endtile);
		for (TileIndex c = tile + delta; c != endtile; c += delta) {
			/* do not let trees appear from 'nowhere' after removing bridge */
			if (IsNormalRoadTile(c) && GetRoadside(c) == ROADSIDE_TREES) {
				int minz = GetTileMaxZ(c) + 3;
				if (height < minz) SetRoadside(c, ROADSIDE_PAVED);
			}
			ClearBridgeMiddle(c);
			MarkTileDirtyByTile(c, ZOOM_LVL_DRAW_MAP, height - TileHeight(c));
		}

		if (rail) {
			/* cannot use INVALID_DIAGDIR for signal update because the bridge doesn't exist anymore */

			auto notify_track_change = [owner](TileIndex tile, DiagDirection direction, TrackBits tracks) {
				auto check_dir = [&](DiagDirection d) {
					if (DiagdirReachesTracks(d) & tracks) AddSideToSignalBuffer(tile, d, owner);
				};
				check_dir(ChangeDiagDir(direction, DIAGDIRDIFF_90RIGHT));
				check_dir(ChangeDiagDir(direction, DIAGDIRDIFF_REVERSE));
				check_dir(ChangeDiagDir(direction, DIAGDIRDIFF_90LEFT));
				while (tracks != TRACK_BIT_NONE) {
					YapfNotifyTrackLayoutChange(tile, RemoveFirstTrack(&tracks));
				}
			};
			notify_track_change(tile, direction, tile_tracks);
			notify_track_change(endtile, ReverseDiagDir(direction), endtile_tracks);

			for (uint i = 0; i < vehicles_affected.size(); ++i) {
				TryPathReserve(vehicles_affected[i], true);
			}
		}
	}

	return cost;
}

/**
 * Remove a tunnel or a bridge from the game.
 * @param tile Tile containing one of the endpoints.
 * @param flags Command flags.
 * @return Succeeded or failed command.
 */
static CommandCost ClearTile_TunnelBridge(TileIndex tile, DoCommandFlag flags)
{
	if (IsTunnel(tile)) {
		if (flags & DC_AUTO) return_cmd_error(STR_ERROR_MUST_DEMOLISH_TUNNEL_FIRST);
		return DoClearTunnel(tile, flags);
	} else { // IsBridge(tile)
		if (flags & DC_AUTO) return_cmd_error(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
		return DoClearBridge(tile, flags);
	}
}

/**
 * Draw a single pillar sprite.
 * @param psid      Pillarsprite
 * @param x         Pillar X
 * @param y         Pillar Y
 * @param z         Pillar Z
 * @param w         Bounding box size in X direction
 * @param h         Bounding box size in Y direction
 * @param subsprite Optional subsprite for drawing halfpillars
 */
static inline void DrawPillar(const PalSpriteID *psid, int x, int y, int z, int w, int h, const SubSprite *subsprite)
{
	static const int PILLAR_Z_OFFSET = TILE_HEIGHT - BRIDGE_Z_START; ///< Start offset of pillar wrt. bridge (downwards)
	AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, w, h, BB_HEIGHT_UNDER_BRIDGE - PILLAR_Z_OFFSET, z, IsTransparencySet(TO_BRIDGES), 0, 0, -PILLAR_Z_OFFSET, subsprite);
}

/**
 * Draw two bridge pillars (north and south).
 * @param z_bottom Bottom Z
 * @param z_top    Top Z
 * @param psid     Pillarsprite
 * @param x        Pillar X
 * @param y        Pillar Y
 * @param w        Bounding box size in X direction
 * @param h        Bounding box size in Y direction
 * @return Reached Z at the bottom
 */
static int DrawPillarColumn(int z_bottom, int z_top, const PalSpriteID *psid, int x, int y, int w, int h)
{
	int cur_z;
	for (cur_z = z_top; cur_z >= z_bottom; cur_z -= TILE_HEIGHT) {
		DrawPillar(psid, x, y, cur_z, w, h, nullptr);
	}
	return cur_z;
}

/**
 * Draws the pillars under high bridges.
 *
 * @param psid Image and palette of a bridge pillar.
 * @param ti #TileInfo of current bridge-middle-tile.
 * @param axis Orientation of bridge.
 * @param drawfarpillar Whether to draw the pillar at the back
 * @param x Sprite X position of front pillar.
 * @param y Sprite Y position of front pillar.
 * @param z_bridge Absolute height of bridge bottom.
 */
static void DrawBridgePillars(const PalSpriteID *psid, const TileInfo *ti, Axis axis, bool drawfarpillar, int x, int y, int z_bridge)
{
	static const int bounding_box_size[2]  = {16, 2}; ///< bounding box size of pillars along bridge direction
	static const int back_pillar_offset[2] = { 0, 9}; ///< sprite position offset of back facing pillar

	static const int INF = 1000; ///< big number compared to sprite size
	static const SubSprite half_pillar_sub_sprite[2][2] = {
		{ {  -14, -INF, INF, INF }, { -INF, -INF, -15, INF } }, // X axis, north and south
		{ { -INF, -INF,  15, INF }, {   16, -INF, INF, INF } }, // Y axis, north and south
	};

	if (psid->sprite == 0) return;

	/* Determine ground height under pillars */
	DiagDirection south_dir = AxisToDiagDir(axis);
	int z_front_north = ti->z;
	int z_back_north = ti->z;
	int z_front_south = ti->z;
	int z_back_south = ti->z;
	GetSlopePixelZOnEdge(ti->tileh, south_dir, &z_front_south, &z_back_south);
	GetSlopePixelZOnEdge(ti->tileh, ReverseDiagDir(south_dir), &z_front_north, &z_back_north);

	/* Shared height of pillars */
	int z_front = max(z_front_north, z_front_south);
	int z_back = max(z_back_north, z_back_south);

	/* x and y size of bounding-box of pillars */
	int w = bounding_box_size[axis];
	int h = bounding_box_size[OtherAxis(axis)];
	/* sprite position of back facing pillar */
	int x_back = x - back_pillar_offset[axis];
	int y_back = y - back_pillar_offset[OtherAxis(axis)];

	/* Draw front pillars */
	int bottom_z = DrawPillarColumn(z_front, z_bridge, psid, x, y, w, h);
	if (z_front_north < z_front) DrawPillar(psid, x, y, bottom_z, w, h, &half_pillar_sub_sprite[axis][0]);
	if (z_front_south < z_front) DrawPillar(psid, x, y, bottom_z, w, h, &half_pillar_sub_sprite[axis][1]);

	/* Draw back pillars, skip top two parts, which are hidden by the bridge */
	int z_bridge_back = z_bridge - 2 * (int)TILE_HEIGHT;
	if (drawfarpillar && (z_back_north <= z_bridge_back || z_back_south <= z_bridge_back)) {
		bottom_z = DrawPillarColumn(z_back, z_bridge_back, psid, x_back, y_back, w, h);
		if (z_back_north < z_back) DrawPillar(psid, x_back, y_back, bottom_z, w, h, &half_pillar_sub_sprite[axis][0]);
		if (z_back_south < z_back) DrawPillar(psid, x_back, y_back, bottom_z, w, h, &half_pillar_sub_sprite[axis][1]);
	}
}

/**
 * Retrieve the sprites required for catenary on a road/tram bridge.
 * @param rti              RoadTypeInfo for the road or tram type to get catenary for
 * @param head_tile        Bridge head tile with roadtype information
 * @param offset           Sprite offset identifying flat to sloped bridge tiles
 * @param head             Are we drawing bridge head?
 * @param[out] spr_back    Back catenary sprite to use
 * @param[out] spr_front   Front catenary sprite to use
 */
static void GetBridgeRoadCatenary(const RoadTypeInfo *rti, TileIndex head_tile, int offset, bool head, SpriteID &spr_back, SpriteID &spr_front)
{
	static const SpriteID back_offsets[6]  = { 95,  96,  99, 102, 100, 101 };
	static const SpriteID front_offsets[6] = { 97,  98, 103, 106, 104, 105 };

	/* Simplified from DrawRoadTypeCatenary() to remove all the special cases required for regular ground road */
	spr_back = GetCustomRoadSprite(rti, head_tile, ROTSG_CATENARY_BACK, head ? TCX_NORMAL : TCX_ON_BRIDGE);
	spr_front = GetCustomRoadSprite(rti, head_tile, ROTSG_CATENARY_FRONT, head ? TCX_NORMAL : TCX_ON_BRIDGE);
	if (spr_back == 0 && spr_front == 0) {
		spr_back = SPR_TRAMWAY_BASE + back_offsets[offset];
		spr_front = SPR_TRAMWAY_BASE + front_offsets[offset];
	} else {
		if (spr_back != 0) spr_back += 23 + offset;
		if (spr_front != 0) spr_front += 23 + offset;
	}
}

/**
 * Draws the road and trambits over an already drawn (lower end) of a bridge.
 * @param head_tile    bridge head tile with roadtype information
 * @param x            the x of the bridge
 * @param y            the y of the bridge
 * @param z            the z of the bridge
 * @param offset       sprite offset identifying flat to sloped bridge tiles
 * @param head         are we drawing bridge head?
 */
static void DrawBridgeRoadBits(TileIndex head_tile, int x, int y, int z, int offset, bool head)
{
	RoadType road_rt = GetRoadTypeRoad(head_tile);
	RoadType tram_rt = GetRoadTypeTram(head_tile);
	if (IsRoadCustomBridgeHeadTile(head_tile)) {
		RoadBits entrance_bit = DiagDirToRoadBits(GetTunnelBridgeDirection(head_tile));
		if (road_rt != INVALID_ROADTYPE && !(GetCustomBridgeHeadRoadBits(head_tile, RTT_ROAD) & entrance_bit)) road_rt = INVALID_ROADTYPE;
		if (tram_rt != INVALID_ROADTYPE && !(GetCustomBridgeHeadRoadBits(head_tile, RTT_TRAM) & entrance_bit)) tram_rt = INVALID_ROADTYPE;
	}
	const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
	const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);

	SpriteID seq_back[4] = { 0 };
	bool trans_back[4] = { false };
	SpriteID seq_front[4] = { 0 };
	bool trans_front[4] = { false };

	static const SpriteID overlay_offsets[6] = {   0,   1,  11,  12,  13,  14 };
	if (head || !IsInvisibilitySet(TO_BRIDGES)) {
		/* Road underlay takes precedence over tram */
		trans_back[0] = !head && IsTransparencySet(TO_BRIDGES);
		if (road_rti != nullptr) {
			if (road_rti->UsesOverlay()) {
				seq_back[0] = GetCustomRoadSprite(road_rti, head_tile, ROTSG_BRIDGE, head ? TCX_NORMAL : TCX_ON_BRIDGE) + offset;
			}
		} else if (tram_rti != nullptr) {
			if (tram_rti->UsesOverlay()) {
				seq_back[0] = GetCustomRoadSprite(tram_rti, head_tile, ROTSG_BRIDGE, head ? TCX_NORMAL : TCX_ON_BRIDGE) + offset;
			} else {
				seq_back[0] = SPR_TRAMWAY_BRIDGE + offset;
			}
		}

		/* Draw road overlay */
		trans_back[1] = !head && IsTransparencySet(TO_BRIDGES);
		if (road_rti != nullptr) {
			if (road_rti->UsesOverlay()) {
				seq_back[1] = GetCustomRoadSprite(road_rti, head_tile, ROTSG_OVERLAY, head ? TCX_NORMAL : TCX_ON_BRIDGE);
				if (seq_back[1] != 0) seq_back[1] += overlay_offsets[offset];
			}
		}

		/* Draw tram overlay */
		trans_back[2] = !head && IsTransparencySet(TO_BRIDGES);
		if (tram_rti != nullptr) {
			if (tram_rti->UsesOverlay()) {
				seq_back[2] = GetCustomRoadSprite(tram_rti, head_tile, ROTSG_OVERLAY, head ? TCX_NORMAL : TCX_ON_BRIDGE);
				if (seq_back[2] != 0) seq_back[2] += overlay_offsets[offset];
			} else if (road_rti != nullptr) {
				seq_back[2] = SPR_TRAMWAY_OVERLAY + overlay_offsets[offset];
			}
		}

		/* Road catenary takes precedence over tram */
		trans_back[3] = IsTransparencySet(TO_CATENARY);
		trans_front[0] = IsTransparencySet(TO_CATENARY);
		if (road_rti != nullptr && HasRoadCatenaryDrawn(road_rt)) {
			GetBridgeRoadCatenary(road_rti, head_tile, offset, head, seq_back[3], seq_front[0]);
		} else if (tram_rti != nullptr && HasRoadCatenaryDrawn(tram_rt)) {
			GetBridgeRoadCatenary(tram_rti, head_tile, offset, head, seq_back[3], seq_front[0]);
		}
	}

	static const uint size_x[6] = {  1, 16, 16,  1, 16,  1 };
	static const uint size_y[6] = { 16,  1,  1, 16,  1, 16 };
	static const uint front_bb_offset_x[6] = { 15,  0,  0, 15,  0, 15 };
	static const uint front_bb_offset_y[6] = {  0, 15, 15,  0, 15,  0 };

	/* The sprites under the vehicles are drawn as SpriteCombine. StartSpriteCombine() has already been called
	 * The bounding boxes here are the same as for bridge front/roof */
	for (uint i = 0; i < lengthof(seq_back); ++i) {
		if (seq_back[i] != 0) {
			AddSortableSpriteToDraw(seq_back[i], PAL_NONE,
				x, y, size_x[offset], size_y[offset], 0x28, z,
				trans_back[i]);
		}
	}

	/* Start a new SpriteCombine for the front part */
	EndSpriteCombine();
	StartSpriteCombine();

	for (uint i = 0; i < lengthof(seq_front); ++i) {
		if (seq_front[i] != 0) {
			AddSortableSpriteToDraw(seq_front[i], PAL_NONE,
				x, y, size_x[offset] + front_bb_offset_x[offset], size_y[offset] + front_bb_offset_y[offset], 0x28, z,
				trans_front[i],
				front_bb_offset_x[offset], front_bb_offset_y[offset]);
		}
	}
}

static void DrawTunnelBridgeRampSingleSignal(const TileInfo *ti, bool is_green, uint position, SignalType type, bool show_exit)
{
	bool side = (_settings_game.vehicle.road_side != 0) &&_settings_game.construction.train_signal_side;
	DiagDirection dir = GetTunnelBridgeDirection(ti->tile);

	static const Point SignalPositions[2][4] = {
		{   /*  X         X         Y         Y     Signals on the left side */
			{13,  3}, { 2, 13}, { 3,  4}, {13, 14}
		}, {/*  X         X         Y         Y     Signals on the right side */
			{14, 13}, { 3,  3}, {13,  2}, { 3, 13}
		}
	};

	uint x = TileX(ti->tile) * TILE_SIZE + SignalPositions[side != show_exit][position ^ (show_exit ? 1 : 0)].x;
	uint y = TileY(ti->tile) * TILE_SIZE + SignalPositions[side != show_exit][position ^ (show_exit ? 1 : 0)].y;
	uint z = ti->z;

	if (ti->tileh == SLOPE_FLAT && side == show_exit && dir == DIAGDIR_SE) z += 2;
	if (ti->tileh == SLOPE_FLAT && side != show_exit && dir == DIAGDIR_SW) z += 2;

	if (ti->tileh != SLOPE_FLAT && IsBridge(ti->tile)) z += 8; // sloped bridge head
	SignalVariant variant = IsTunnelBridgeSemaphore(ti->tile) ? SIG_SEMAPHORE : SIG_ELECTRIC;
	const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));

	SpriteID sprite = GetCustomSignalSprite(rti, ti->tile, type, variant, is_green ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED);
	bool is_custom_sprite = (sprite != 0);

	if (is_custom_sprite) {
		sprite += position;
	} else {
		if (variant == SIG_ELECTRIC && type == SIGTYPE_NORMAL) {
			/* Normal electric signals are picked from original sprites. */
			sprite = SPR_ORIGINAL_SIGNALS_BASE + ((position << 1) + is_green);
		} else {
			/* All other signals are picked from add on sprites. */
			sprite = SPR_SIGNALS_BASE + ((type - 1) * 16 + variant * 64 + (position << 1) + is_green) + (IsSignalSpritePBS(type) ? 64 : 0);
		}
	}

	AddSortableSpriteToDraw(sprite, PAL_NONE, x, y, 1, 1, TILE_HEIGHT, z, false, 0, 0, BB_Z_SEPARATOR);
}

/* Draws a signal on tunnel / bridge entrance tile. */
static void DrawTunnelBridgeRampSignal(const TileInfo *ti)
{
	DiagDirection dir = GetTunnelBridgeDirection(ti->tile);

	uint position;
	switch (dir) {
		default: NOT_REACHED();
		case DIAGDIR_NE: position = 0; break;
		case DIAGDIR_SE: position = 2; break;
		case DIAGDIR_SW: position = 1; break;
		case DIAGDIR_NW: position = 3; break;
	}

	if (IsTunnelBridgeSignalSimulationExit(ti->tile)) {
		SignalType type = SIGTYPE_NORMAL;
		if (IsTunnelBridgePBS(ti->tile)) {
			type = IsTunnelBridgeSignalSimulationEntrance(ti->tile) ? SIGTYPE_PBS : SIGTYPE_PBS_ONEWAY;
		}
		DrawTunnelBridgeRampSingleSignal(ti, (GetTunnelBridgeExitSignalState(ti->tile) == SIGNAL_STATE_GREEN), position ^ 1, type, true);
	}
	if (IsTunnelBridgeSignalSimulationEntrance(ti->tile)) {
		DrawTunnelBridgeRampSingleSignal(ti, (GetTunnelBridgeEntranceSignalState(ti->tile) == SIGNAL_STATE_GREEN), position, SIGTYPE_NORMAL, false);
	}
}

/* Draws a signal on tunnel / bridge entrance tile. */
static void DrawBridgeSignalOnMiddlePart(const TileInfo *ti, TileIndex bridge_start_tile, uint z)
{

	uint bridge_signal_position = 0;
	int m2_position = 0;

	uint bridge_section = GetTunnelBridgeLength(ti->tile, bridge_start_tile) + 1;

	while (bridge_signal_position <= bridge_section) {
		bridge_signal_position += _settings_game.construction.simulated_wormhole_signals;
		if (bridge_signal_position == bridge_section) {
			bool side = (_settings_game.vehicle.road_side != 0) && _settings_game.construction.train_signal_side;

			static const Point SignalPositions[2][4] = {
				{   /*  X         X         Y         Y     Signals on the left side */
					{11,  3}, { 4, 13}, { 3,  4}, {11, 13}
				}, {/*  X         X         Y         Y     Signals on the right side */
					{11, 13}, { 4,  3}, {13,  4}, { 3, 11}
				}
			};

			uint position;

			switch (GetTunnelBridgeDirection(bridge_start_tile)) {
				default: NOT_REACHED();
				case DIAGDIR_NE: position = 0; break;
				case DIAGDIR_SE: position = 2; break;
				case DIAGDIR_SW: position = 1; break;
				case DIAGDIR_NW: position = 3; break;
			}

			uint x = TileX(ti->tile) * TILE_SIZE + SignalPositions[side][position].x;
			uint y = TileY(ti->tile) * TILE_SIZE + SignalPositions[side][position].y;
			z += 5;

			SignalVariant variant = IsTunnelBridgeSemaphore(bridge_start_tile) ? SIG_SEMAPHORE : SIG_ELECTRIC;

			SpriteID sprite = (GetBridgeEntranceSimulatedSignalState(bridge_start_tile, m2_position) == SIGNAL_STATE_GREEN);

			if (variant == SIG_ELECTRIC) {
				/* Normal electric signals are picked from original sprites. */
				sprite += SPR_ORIGINAL_SIGNALS_BASE + (position << 1);
			} else {
				/* All other signals are picked from add on sprites. */
				sprite += SPR_SIGNALS_BASE + (SIGTYPE_NORMAL - 1) * 16 + variant * 64 + (position << 1);
			}

			AddSortableSpriteToDraw(sprite, PAL_NONE, x, y, 1, 1, TILE_HEIGHT, z, false, 0, 0, BB_Z_SEPARATOR);
		}
		m2_position++;
	}
}

/**
 * Draws a tunnel of bridge tile.
 * For tunnels, this is rather simple, as you only need to draw the entrance.
 * Bridges are a bit more complex. base_offset is where the sprite selection comes into play
 * and it works a bit like a bitmask.<p> For bridge heads:
 * @param ti TileInfo of the structure to draw
 * <ul><li>Bit 0: direction</li>
 * <li>Bit 1: northern or southern heads</li>
 * <li>Bit 2: Set if the bridge head is sloped</li>
 * <li>Bit 3 and more: Railtype Specific subset</li>
 * </ul>
 * Please note that in this code, "roads" are treated as railtype 1, whilst the real railtypes are 0, 2 and 3
 */
static void DrawTile_TunnelBridge(TileInfo *ti, DrawTileProcParams params)
{
	TransportType transport_type = GetTunnelBridgeTransportType(ti->tile);
	DiagDirection tunnelbridge_direction = GetTunnelBridgeDirection(ti->tile);

	if (IsTunnel(ti->tile)) {
		/* Front view of tunnel bounding boxes:
		 *
		 *   122223  <- BB_Z_SEPARATOR
		 *   1    3
		 *   1    3                1,3 = empty helper BB
		 *   1    3                  2 = SpriteCombine of tunnel-roof and catenary (tram & elrail)
		 *
		 */

		static const int _tunnel_BB[4][12] = {
			/*  tunnnel-roof  |  Z-separator  | tram-catenary
			 * w  h  bb_x bb_y| x   y   w   h |bb_x bb_y w h */
			{  1,  0, -15, -14,  0, 15, 16,  1, 0, 1, 16, 15 }, // NE
			{  0,  1, -14, -15, 15,  0,  1, 16, 1, 0, 15, 16 }, // SE
			{  1,  0, -15, -14,  0, 15, 16,  1, 0, 1, 16, 15 }, // SW
			{  0,  1, -14, -15, 15,  0,  1, 16, 1, 0, 15, 16 }, // NW
		};
		const int *BB_data = _tunnel_BB[tunnelbridge_direction];

		bool catenary = false;

		SpriteID image;
		SpriteID railtype_overlay = 0;
		if (transport_type == TRANSPORT_RAIL) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			image = rti->base_sprites.tunnel;
			if (rti->UsesOverlay()) {
				/* Check if the railtype has custom tunnel portals. */
				railtype_overlay = GetCustomRailSprite(rti, ti->tile, RTSG_TUNNEL_PORTAL);
				if (railtype_overlay != 0) image = SPR_RAILTYPE_TUNNEL_BASE; // Draw blank grass tunnel base.
			}
		} else {
			image = SPR_TUNNEL_ENTRY_REAR_ROAD;
		}

		if (HasTunnelBridgeSnowOrDesert(ti->tile)) image += railtype_overlay != 0 ? 8 : 32;

		image += tunnelbridge_direction * 2;
		DrawGroundSprite(image, PAL_NONE);

		if (transport_type == TRANSPORT_ROAD) {
			RoadType road_rt = GetRoadTypeRoad(ti->tile);
			RoadType tram_rt = GetRoadTypeTram(ti->tile);
			const RoadTypeInfo *road_rti = road_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(road_rt);
			const RoadTypeInfo *tram_rti = tram_rt == INVALID_ROADTYPE ? nullptr : GetRoadTypeInfo(tram_rt);
			uint sprite_offset = DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? 1 : 0;

			DrawRoadOverlays(ti, PAL_NONE, road_rti, tram_rti, sprite_offset, sprite_offset);

			/* Road catenary takes precedence over tram */
			SpriteID catenary_sprite_base = 0;
			if (road_rti != nullptr && HasRoadCatenaryDrawn(road_rt)) {
				catenary_sprite_base = GetCustomRoadSprite(road_rti, ti->tile, ROTSG_CATENARY_FRONT);
				if (catenary_sprite_base == 0) {
					catenary_sprite_base = SPR_TRAMWAY_TUNNEL_WIRES;
				} else {
					catenary_sprite_base += 19;
				}
			} else if (tram_rti != nullptr && HasRoadCatenaryDrawn(tram_rt)) {
				catenary_sprite_base = GetCustomRoadSprite(tram_rti, ti->tile, ROTSG_CATENARY_FRONT);
				if (catenary_sprite_base == 0) {
					catenary_sprite_base = SPR_TRAMWAY_TUNNEL_WIRES;
				} else {
					catenary_sprite_base += 19;
				}
			}

			if (catenary_sprite_base != 0) {
				catenary = true;
				StartSpriteCombine();
				AddSortableSpriteToDraw(catenary_sprite_base + tunnelbridge_direction, PAL_NONE, ti->x, ti->y, BB_data[10], BB_data[11], TILE_HEIGHT, ti->z, IsTransparencySet(TO_CATENARY), BB_data[8], BB_data[9], BB_Z_SEPARATOR);
			}
		} else {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			if (rti->UsesOverlay()) {
				SpriteID surface = GetCustomRailSprite(rti, ti->tile, RTSG_TUNNEL);
				if (surface != 0) DrawGroundSprite(surface + tunnelbridge_direction, PAL_NONE);
			}

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && HasTunnelReservation(ti->tile)) {
				if (rti->UsesOverlay()) {
					SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
					DrawGroundSprite(overlay + RTO_X + DiagDirToAxis(tunnelbridge_direction), PALETTE_CRASH);
				} else {
					DrawGroundSprite(DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH);
				}
			}

			if (HasRailCatenaryDrawn(GetRailType(ti->tile))) {
				/* Maybe draw pylons on the entry side */
				DrawRailCatenary(ti);

				catenary = true;
				StartSpriteCombine();
				/* Draw wire above the ramp */
				DrawRailCatenaryOnTunnel(ti);
			}
		}

		if (railtype_overlay != 0 && !catenary) StartSpriteCombine();

		AddSortableSpriteToDraw(image + 1, PAL_NONE, ti->x + TILE_SIZE - 1, ti->y + TILE_SIZE - 1, BB_data[0], BB_data[1], TILE_HEIGHT, ti->z, false, BB_data[2], BB_data[3], BB_Z_SEPARATOR);
		/* Draw railtype tunnel portal overlay if defined. */
		if (railtype_overlay != 0) AddSortableSpriteToDraw(railtype_overlay + tunnelbridge_direction, PAL_NONE, ti->x + TILE_SIZE - 1, ti->y + TILE_SIZE - 1, BB_data[0], BB_data[1], TILE_HEIGHT, ti->z, false, BB_data[2], BB_data[3], BB_Z_SEPARATOR);

		if (catenary || railtype_overlay != 0) EndSpriteCombine();

		/* Add helper BB for sprite sorting that separates the tunnel from things beside of it. */
		AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, ti->x,              ti->y,              BB_data[6], BB_data[7], TILE_HEIGHT, ti->z);
		AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, ti->x + BB_data[4], ti->y + BB_data[5], BB_data[6], BB_data[7], TILE_HEIGHT, ti->z);

		/* Draw signals for tunnel. */
		if (IsTunnelBridgeWithSignalSimulation(ti->tile)) DrawTunnelBridgeRampSignal(ti);

		DrawBridgeMiddle(ti);
	} else { // IsBridge(ti->tile)
		if (transport_type == TRANSPORT_ROAD && IsRoadCustomBridgeHead(ti->tile)) {
			DrawRoadBits(ti);
			DrawBridgeMiddle(ti);
			return;
		}
		if (transport_type == TRANSPORT_RAIL && IsRailCustomBridgeHead(ti->tile)) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			DrawTrackBits(ti, GetCustomBridgeHeadTrackBits(ti->tile));
			if (HasBit(_display_opt, DO_FULL_DETAIL)) {
				extern void DrawTrackDetails(const TileInfo *ti, const RailtypeInfo *rti, const RailGroundType rgt);
				DrawTrackDetails(ti, rti, GetTunnelBridgeGroundType(ti->tile));
			}
			if (HasRailCatenaryDrawn(GetRailType(ti->tile), GetTileSecondaryRailTypeIfValid(ti->tile))) {
				DrawRailCatenary(ti);
			}

			if (IsTunnelBridgeWithSignalSimulation(ti->tile)) {
				extern void DrawSingleSignal(TileIndex tile, const RailtypeInfo *rti, Track track, SignalState condition,
						SignalOffsets image, uint pos, SignalType type, SignalVariant variant, bool show_restricted);

				DiagDirection dir = GetTunnelBridgeDirection(ti->tile);
				SignalVariant variant = IsTunnelBridgeSemaphore(ti->tile) ? SIG_SEMAPHORE : SIG_ELECTRIC;

				Track t = FindFirstTrack(GetAcrossTunnelBridgeTrackBits(ti->tile));
				auto draw_signals = [&](uint position, SignalOffsets image, DiagDirection towards) {
					if (dir == towards) {
						/* flip signal directions */
						position ^= 1;
						image = (SignalOffsets)(image ^ 1);
					}
					if (IsTunnelBridgeSignalSimulationEntrance(ti->tile)) {
						DrawSingleSignal(ti->tile, rti, t, GetTunnelBridgeEntranceSignalState(ti->tile), image, position, SIGTYPE_NORMAL, variant, false);
					}
					if (IsTunnelBridgeSignalSimulationExit(ti->tile)) {
						SignalType type = SIGTYPE_NORMAL;
						if (IsTunnelBridgePBS(ti->tile)) {
							type = IsTunnelBridgeSignalSimulationEntrance(ti->tile) ? SIGTYPE_PBS : SIGTYPE_PBS_ONEWAY;
						}
						DrawSingleSignal(ti->tile, rti, t, GetTunnelBridgeExitSignalState(ti->tile), (SignalOffsets)(image ^ 1), position ^ 1, type, variant, false);
					}
				};
				switch (t) {
					default: NOT_REACHED();
					case TRACK_X:     draw_signals( 8, SIGNAL_TO_SOUTHWEST, DIAGDIR_SW); break;
					case TRACK_Y:     draw_signals(10, SIGNAL_TO_SOUTHEAST, DIAGDIR_NW); break;
					case TRACK_UPPER: draw_signals( 4, SIGNAL_TO_WEST,      DIAGDIR_NW); break;
					case TRACK_LOWER: draw_signals( 6, SIGNAL_TO_WEST,      DIAGDIR_SW); break;
					case TRACK_LEFT:  draw_signals( 0, SIGNAL_TO_NORTH,     DIAGDIR_NW); break;
					case TRACK_RIGHT: draw_signals( 2, SIGNAL_TO_NORTH,     DIAGDIR_NE); break;
				}
			}

			DrawBridgeMiddle(ti);
			return;
		}

		const PalSpriteID *psid;
		int base_offset;
		bool ice = HasTunnelBridgeSnowOrDesert(ti->tile);

		if (transport_type == TRANSPORT_RAIL) {
			base_offset = GetRailTypeInfo(GetRailType(ti->tile))->bridge_offset;
			assert(base_offset != 8); // This one is used for roads
		} else {
			base_offset = 8;
		}

		/* as the lower 3 bits are used for other stuff, make sure they are clear */
		assert( (base_offset & 0x07) == 0x00);

		DrawFoundation(ti, GetBridgeFoundation(ti->tileh, DiagDirToAxis(tunnelbridge_direction)));

		/* HACK Wizardry to convert the bridge ramp direction into a sprite offset */
		base_offset += (6 - tunnelbridge_direction) % 4;

		/* Table number BRIDGE_PIECE_HEAD always refers to the bridge heads for any bridge type */
		if (transport_type != TRANSPORT_WATER) {
			if (ti->tileh == SLOPE_FLAT) base_offset += 4; // sloped bridge head
			psid = &GetBridgeSpriteTable(GetBridgeType(ti->tile), BRIDGE_PIECE_HEAD)[base_offset];
		} else {
			psid = _aqueduct_sprites + base_offset;
		}

		if (!ice) {
			TileIndex next = ti->tile + TileOffsByDiagDir(tunnelbridge_direction);
			if (ti->tileh != SLOPE_FLAT && ti->z == 0 && HasTileWaterClass(next) && GetWaterClass(next) == WATER_CLASS_SEA) {
				DrawShoreTile(ti->tileh);
			} else {
				DrawClearLandTile(ti, 3);
			}
		} else {
			DrawGroundSprite(SPR_FLAT_SNOW_DESERT_TILE + SlopeToSpriteOffset(ti->tileh), PAL_NONE);
		}

		/* draw ramp */

		/* Draw Trambits and PBS Reservation as SpriteCombine */
		if (transport_type == TRANSPORT_ROAD || transport_type == TRANSPORT_RAIL) StartSpriteCombine();

		/* HACK set the height of the BB of a sloped ramp to 1 so a vehicle on
		 * it doesn't disappear behind it
		 */
		/* Bridge heads are drawn solid no matter how invisibility/transparency is set */
		AddSortableSpriteToDraw(psid->sprite, psid->pal, ti->x, ti->y, 16, 16, ti->tileh == SLOPE_FLAT ? 0 : 8, ti->z);

		if (transport_type == TRANSPORT_ROAD) {
			uint offset = tunnelbridge_direction;
			int z = ti->z;
			if (ti->tileh != SLOPE_FLAT) {
				offset = (offset + 1) & 1;
				z += TILE_HEIGHT;
			} else {
				offset += 2;
			}

			/* DrawBridgeRoadBits() calls EndSpriteCombine() and StartSpriteCombine() */
			DrawBridgeRoadBits(ti->tile, ti->x, ti->y, z, offset, true);

			EndSpriteCombine();
		} else if (transport_type == TRANSPORT_RAIL) {
			const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(ti->tile));
			if (rti->UsesOverlay()) {
				SpriteID surface = GetCustomRailSprite(rti, ti->tile, RTSG_BRIDGE);
				if (surface != 0) {
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(surface + ((DiagDirToAxis(tunnelbridge_direction) == AXIS_X) ? RTBO_X : RTBO_Y), PAL_NONE, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(surface + RTBO_SLOPE + tunnelbridge_direction, PAL_NONE, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				}
				/* Don't fallback to non-overlay sprite -- the spec states that
				 * if an overlay is present then the bridge surface must be
				 * present. */
			}

			/* PBS debugging, draw reserved tracks darker */
			if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && GetBridgeReservationTrackBits(ti->tile) != TRACK_BIT_NONE) {
				if (rti->UsesOverlay()) {
					SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(overlay + RTO_X + DiagDirToAxis(tunnelbridge_direction), PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(overlay + RTO_SLOPE_NE + tunnelbridge_direction, PALETTE_CRASH, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				} else {
					if (HasBridgeFlatRamp(ti->tileh, DiagDirToAxis(tunnelbridge_direction))) {
						AddSortableSpriteToDraw(DiagDirToAxis(tunnelbridge_direction) == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, ti->z + 8);
					} else {
						AddSortableSpriteToDraw(rti->base_sprites.single_sloped + tunnelbridge_direction, PALETTE_CRASH, ti->x, ti->y, 16, 16, 8, ti->z);
					}
				}
			}

			EndSpriteCombine();
			if (HasRailCatenaryDrawn(GetRailType(ti->tile))) {
				DrawRailCatenary(ti);
			}
		}

		/* Draw signals for bridge. */
		if (IsTunnelBridgeWithSignalSimulation(ti->tile)) DrawTunnelBridgeRampSignal(ti);

		DrawBridgeMiddle(ti);
	}
}


/**
 * Compute bridge piece. Computes the bridge piece to display depending on the position inside the bridge.
 * bridges pieces sequence (middle parts).
 * Note that it is not covering the bridge heads, which are always referenced by the same sprite table.
 * bridge len 1: BRIDGE_PIECE_NORTH
 * bridge len 2: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_SOUTH
 * bridge len 3: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_MIDDLE_ODD   BRIDGE_PIECE_SOUTH
 * bridge len 4: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 5: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_MIDDLE_EVEN  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 6: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * bridge len 7: BRIDGE_PIECE_NORTH  BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_MIDDLE_ODD   BRIDGE_PIECE_INNER_NORTH  BRIDGE_PIECE_INNER_SOUTH  BRIDGE_PIECE_SOUTH
 * #0 - always as first, #1 - always as last (if len>1)
 * #2,#3 are to pair in order
 * for odd bridges: #5 is going in the bridge middle if on even position, #4 on odd (counting from 0)
 * @param north Northernmost tile of bridge
 * @param south Southernmost tile of bridge
 * @return Index of bridge piece
 */
static BridgePieces CalcBridgePiece(uint north, uint south)
{
	if (north == 1) {
		return BRIDGE_PIECE_NORTH;
	} else if (south == 1) {
		return BRIDGE_PIECE_SOUTH;
	} else if (north < south) {
		return north & 1 ? BRIDGE_PIECE_INNER_SOUTH : BRIDGE_PIECE_INNER_NORTH;
	} else if (north > south) {
		return south & 1 ? BRIDGE_PIECE_INNER_NORTH : BRIDGE_PIECE_INNER_SOUTH;
	} else {
		return north & 1 ? BRIDGE_PIECE_MIDDLE_EVEN : BRIDGE_PIECE_MIDDLE_ODD;
	}
}

BridgePiecePillarFlags GetBridgeTilePillarFlags(TileIndex tile, TileIndex northern_bridge_end, TileIndex southern_bridge_end, BridgeType bridge_type, TransportType bridge_transport_type)
{
	if (bridge_transport_type == TRANSPORT_WATER) return BPPF_ALL_CORNERS;

	BridgePieces piece = CalcBridgePiece(
		GetTunnelBridgeLength(tile, northern_bridge_end) + 1,
		GetTunnelBridgeLength(tile, southern_bridge_end) + 1
	);
	assert(piece < BRIDGE_PIECE_HEAD);

	const BridgeSpec *spec = GetBridgeSpec(bridge_type);
	const Axis axis = TileX(northern_bridge_end) == TileX(southern_bridge_end) ? AXIS_Y : AXIS_X;
	if (!HasBit(spec->ctrl_flags, BSCF_INVALID_PILLAR_FLAGS)) {
		return (BridgePiecePillarFlags) spec->pillar_flags[piece * 2 + (axis == AXIS_Y ? 1 : 0)];
	} else {
		uint base_offset;
		if (bridge_transport_type == TRANSPORT_RAIL) {
			base_offset = GetRailTypeInfo(GetRailType(southern_bridge_end))->bridge_offset;
		} else {
			base_offset = 8;
		}

		const PalSpriteID *psid = base_offset + GetBridgeSpriteTable(bridge_type, piece);
		if (axis == AXIS_Y) psid += 4;
		return (BridgePiecePillarFlags) (psid[2].sprite != 0 ? BPPF_ALL_CORNERS : 0);
	}
}

/**
 * Draw the middle bits of a bridge.
 * @param ti Tile information of the tile to draw it on.
 */
void DrawBridgeMiddle(const TileInfo *ti)
{
	/* Sectional view of bridge bounding boxes:
	 *
	 *  1           2                                1,2 = SpriteCombine of Bridge front/(back&floor) and RoadCatenary
	 *  1           2                                  3 = empty helper BB
	 *  1     7     2                                4,5 = pillars under higher bridges
	 *  1 6 88888 6 2                                  6 = elrail-pylons
	 *  1 6 88888 6 2                                  7 = elrail-wire
	 *  1 6 88888 6 2  <- TILE_HEIGHT                  8 = rail-vehicle on bridge
	 *  3333333333333  <- BB_Z_SEPARATOR
	 *                 <- unused
	 *    4       5    <- BB_HEIGHT_UNDER_BRIDGE
	 *    4       5
	 *    4       5
	 *
	 */

	if (!IsBridgeAbove(ti->tile)) return;

	TileIndex rampnorth = GetNorthernBridgeEnd(ti->tile);
	TileIndex rampsouth = GetSouthernBridgeEnd(ti->tile);
	TransportType transport_type = GetTunnelBridgeTransportType(rampsouth);

	Axis axis = GetBridgeAxis(ti->tile);
	BridgePieces piece = CalcBridgePiece(
		GetTunnelBridgeLength(ti->tile, rampnorth) + 1,
		GetTunnelBridgeLength(ti->tile, rampsouth) + 1
	);

	const PalSpriteID *psid;
	bool drawfarpillar;
	if (transport_type != TRANSPORT_WATER) {
		BridgeType type =  GetBridgeType(rampsouth);
		drawfarpillar = !HasBit(GetBridgeSpec(type)->flags, 0);

		uint base_offset;
		if (transport_type == TRANSPORT_RAIL) {
			base_offset = GetRailTypeInfo(GetRailType(rampsouth))->bridge_offset;
		} else {
			base_offset = 8;
		}

		psid = base_offset + GetBridgeSpriteTable(type, piece);
	} else {
		drawfarpillar = true;
		psid = _aqueduct_sprites;
	}

	if (axis != AXIS_X) psid += 4;

	int x = ti->x;
	int y = ti->y;
	uint bridge_z = GetBridgePixelHeight(rampsouth);
	int z = bridge_z - BRIDGE_Z_START;

	/* Add a bounding box that separates the bridge from things below it. */
	AddSortableSpriteToDraw(SPR_EMPTY_BOUNDING_BOX, PAL_NONE, x, y, 16, 16, 1, bridge_z - TILE_HEIGHT + BB_Z_SEPARATOR);

	/* Draw Trambits as SpriteCombine */
	if (transport_type == TRANSPORT_ROAD || transport_type == TRANSPORT_RAIL) StartSpriteCombine();

	/* Draw floor and far part of bridge*/
	if (!IsInvisibilitySet(TO_BRIDGES)) {
		if (axis == AXIS_X) {
			AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 16, 1, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 0, BRIDGE_Z_START);
		} else {
			AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 1, 16, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 0, BRIDGE_Z_START);
		}
	}

	psid++;

	if (transport_type == TRANSPORT_ROAD) {
		/* DrawBridgeRoadBits() calls EndSpriteCombine() and StartSpriteCombine() */
		DrawBridgeRoadBits(rampsouth, x, y, bridge_z, axis ^ 1, false);
	} else if (transport_type == TRANSPORT_RAIL) {
		const RailtypeInfo *rti = GetRailTypeInfo(GetRailType(rampsouth));
		if (rti->UsesOverlay() && !IsInvisibilitySet(TO_BRIDGES)) {
			SpriteID surface = GetCustomRailSprite(rti, rampsouth, RTSG_BRIDGE, TCX_ON_BRIDGE);
			if (surface != 0) {
				AddSortableSpriteToDraw(surface + axis, PAL_NONE, x, y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			}
		}

		if (_game_mode != GM_MENU && _settings_client.gui.show_track_reservation && !IsInvisibilitySet(TO_BRIDGES)
				&& !IsTunnelBridgeWithSignalSimulation(rampnorth) && (HasAcrossBridgeReservation(rampnorth) || HasAcrossBridgeReservation(rampsouth))) {
			if (rti->UsesOverlay()) {
				SpriteID overlay = GetCustomRailSprite(rti, ti->tile, RTSG_OVERLAY);
				AddSortableSpriteToDraw(overlay + RTO_X + axis, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			} else {
				AddSortableSpriteToDraw(axis == AXIS_X ? rti->base_sprites.single_x : rti->base_sprites.single_y, PALETTE_CRASH, ti->x, ti->y, 16, 16, 0, bridge_z, IsTransparencySet(TO_BRIDGES));
			}
		}

		EndSpriteCombine();

		if (HasRailCatenaryDrawn(GetRailType(rampsouth))) {
			DrawRailCatenaryOnBridge(ti);
		}
		if (IsTunnelBridgeSignalSimulationEntrance(rampsouth)) DrawBridgeSignalOnMiddlePart(ti, rampsouth, z);
		if (IsTunnelBridgeSignalSimulationEntrance(rampnorth)) DrawBridgeSignalOnMiddlePart(ti, rampnorth, z);
	}

	/* draw roof, the component of the bridge which is logically between the vehicle and the camera */
	if (!IsInvisibilitySet(TO_BRIDGES)) {
		if (axis == AXIS_X) {
			y += 12;
			if (psid->sprite & SPRITE_MASK) AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 16, 4, 0x28, z, IsTransparencySet(TO_BRIDGES), 0, 3, BRIDGE_Z_START);
		} else {
			x += 12;
			if (psid->sprite & SPRITE_MASK) AddSortableSpriteToDraw(psid->sprite, psid->pal, x, y, 4, 16, 0x28, z, IsTransparencySet(TO_BRIDGES), 3, 0, BRIDGE_Z_START);
		}
	}

	/* Draw TramFront as SpriteCombine */
	if (transport_type == TRANSPORT_ROAD) EndSpriteCombine();

	/* Do not draw anything more if bridges are invisible */
	if (IsInvisibilitySet(TO_BRIDGES)) return;

	psid++;
	if (ti->z + 5 == z) {
		/* draw poles below for small bridges */
		if (psid->sprite != 0) {
			SpriteID image = psid->sprite;
			SpriteID pal   = psid->pal;
			if (IsTransparencySet(TO_BRIDGES)) {
				SetBit(image, PALETTE_MODIFIER_TRANSPARENT);
				pal = PALETTE_TO_TRANSPARENT;
			}

			DrawGroundSpriteAt(image, pal, x - ti->x, y - ti->y, z - ti->z);
		}
	} else {
		/* draw pillars below for high bridges */
		DrawBridgePillars(psid, ti, axis, drawfarpillar, x, y, z);
	}
}


static int GetSlopePixelZ_TunnelBridge(TileIndex tile, uint x, uint y)
{
	int z;
	Slope tileh = GetTilePixelSlope(tile, &z);

	x &= 0xF;
	y &= 0xF;

	if (IsTunnel(tile)) {
		uint pos = (DiagDirToAxis(GetTunnelBridgeDirection(tile)) == AXIS_X ? y : x);

		/* In the tunnel entrance? */
		if (5 <= pos && pos <= 10) return z;
	} else { // IsBridge(tile)
		if (IsCustomBridgeHeadTile(tile)) {
			return z + TILE_HEIGHT + (IsSteepSlope(tileh) ? TILE_HEIGHT : 0);
		}

		DiagDirection dir = GetTunnelBridgeDirection(tile);
		uint pos = (DiagDirToAxis(dir) == AXIS_X ? y : x);

		z += ApplyPixelFoundationToSlope(GetBridgeFoundation(tileh, DiagDirToAxis(dir)), &tileh);

		/* On the bridge ramp? */
		if (5 <= pos && pos <= 10) {
			int delta;

			if (tileh != SLOPE_FLAT) return z + TILE_HEIGHT;

			switch (dir) {
				default: NOT_REACHED();
				case DIAGDIR_NE: delta = (TILE_SIZE - 1 - x) / 2; break;
				case DIAGDIR_SE: delta = y / 2; break;
				case DIAGDIR_SW: delta = x / 2; break;
				case DIAGDIR_NW: delta = (TILE_SIZE - 1 - y) / 2; break;
			}
			return z + 1 + delta;
		}
	}

	return z + GetPartialPixelZ(x, y, tileh);
}

static Foundation GetFoundation_TunnelBridge(TileIndex tile, Slope tileh)
{
	if (IsCustomBridgeHeadTile(tile)) return FOUNDATION_LEVELED;
	return IsTunnel(tile) ? FOUNDATION_NONE : GetBridgeFoundation(tileh, DiagDirToAxis(GetTunnelBridgeDirection(tile)));
}

static void GetTileDesc_TunnelBridge(TileIndex tile, TileDesc *td)
{
	TransportType tt = GetTunnelBridgeTransportType(tile);

	if (IsTunnel(tile)) {
		if (Tunnel::GetByTile(tile)->is_chunnel) {
			td->str = (tt == TRANSPORT_RAIL) ? IsTunnelBridgeWithSignalSimulation(tile) ? STR_LAI_TUNNEL_DESCRIPTION_RAILROAD_SIGNAL_CHUNNEL : STR_LAI_TUNNEL_DESCRIPTION_RAILROAD_CHUNNEL : STR_LAI_TUNNEL_DESCRIPTION_ROAD_CHUNNEL;
		} else {
			td->str = (tt == TRANSPORT_RAIL) ? IsTunnelBridgeWithSignalSimulation(tile) ? STR_LAI_TUNNEL_DESCRIPTION_RAILROAD_SIGNAL : STR_LAI_TUNNEL_DESCRIPTION_RAILROAD : STR_LAI_TUNNEL_DESCRIPTION_ROAD;
		}
	} else { // IsBridge(tile)
		td->str = (tt == TRANSPORT_WATER) ? STR_LAI_BRIDGE_DESCRIPTION_AQUEDUCT : IsTunnelBridgeWithSignalSimulation(tile) ? STR_LAI_BRIDGE_DESCRIPTION_RAILROAD_SIGNAL : GetBridgeSpec(GetBridgeType(tile))->transport_name[tt];
	}
	td->owner[0] = GetTileOwner(tile);

	if (tt == TRANSPORT_ROAD) {
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

		/* Is there a mix of owners? */
		if ((tram_owner != INVALID_OWNER && tram_owner != td->owner[0]) ||
				(road_owner != INVALID_OWNER && road_owner != td->owner[0])) {
			uint i = 1;
			if (road_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_LAND_AREA_INFORMATION_ROAD_OWNER;
				td->owner[i] = road_owner;
				i++;
			}
			if (tram_owner != INVALID_OWNER) {
				td->owner_type[i] = STR_LAND_AREA_INFORMATION_TRAM_OWNER;
				td->owner[i] = tram_owner;
			}
		}

		if (!IsTunnel(tile)) {
			uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;
			if (road_rt != INVALID_ROADTYPE && (td->road_speed == 0 || spd < td->road_speed)) td->road_speed = spd;
			if (tram_rt != INVALID_ROADTYPE && (td->tram_speed == 0 || spd < td->tram_speed)) td->tram_speed = spd;
		}
	}

	if (tt == TRANSPORT_RAIL) {
		RailType rt = GetRailType(tile);
		const RailtypeInfo *rti = GetRailTypeInfo(rt);
		td->rail_speed = rti->max_speed;
		td->railtype = rti->strings.name;
		RailType secondary_rt = GetTileSecondaryRailTypeIfValid(tile);
		if (secondary_rt != rt && secondary_rt != INVALID_RAILTYPE) {
			const RailtypeInfo *secondary_rti = GetRailTypeInfo(secondary_rt);
			td->rail_speed2 = secondary_rti->max_speed;
			td->railtype2 = secondary_rti->strings.name;
		}

		if (!IsTunnel(tile)) {
			uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;
			if (td->rail_speed == 0 || spd < td->rail_speed) {
				td->rail_speed = spd;
			}
		}
	}
}

static const RailGroundType _tunnel_bridge_fence_table[4][5] = {
	{ // DIAGDIR_NE
		RAIL_GROUND_FENCE_NW,
		RAIL_GROUND_FENCE_SE,
		RAIL_GROUND_FENCE_SW,
		RAIL_GROUND_FENCE_VERT2,
		RAIL_GROUND_FENCE_HORIZ1,
	},
	{ // DIAGDIR_SE
		RAIL_GROUND_FENCE_NW,
		RAIL_GROUND_FENCE_NE,
		RAIL_GROUND_FENCE_SW,
		RAIL_GROUND_FENCE_VERT2,
		RAIL_GROUND_FENCE_HORIZ2,
	},
	{ // DIAGDIR_SW
		RAIL_GROUND_FENCE_NW,
		RAIL_GROUND_FENCE_SE,
		RAIL_GROUND_FENCE_NE,
		RAIL_GROUND_FENCE_VERT1,
		RAIL_GROUND_FENCE_HORIZ2,
	},
	{ // DIAGDIR_NW
		RAIL_GROUND_FENCE_SE,
		RAIL_GROUND_FENCE_NE,
		RAIL_GROUND_FENCE_SW,
		RAIL_GROUND_FENCE_VERT1,
		RAIL_GROUND_FENCE_HORIZ1,
	},
};

RailGroundType GetTunnelBridgeGroundType(TileIndex tile)
{
	uint8 ground_bits = GetTunnelBridgeGroundBits(tile);
	if (ground_bits == 0) return RAIL_GROUND_GRASS;
	if (ground_bits == 1) return RAIL_GROUND_ICE_DESERT;
	if (ground_bits == 2) return RAIL_GROUND_BARREN;
	return _tunnel_bridge_fence_table[GetTunnelBridgeDirection(tile)][ground_bits - 3];
}

static uint8 MapTunnelBridgeGroundTypeBits(TileIndex tile, RailGroundType type)
{
	uint8 ground_bits;
	switch (type) {
		case RAIL_GROUND_BARREN:
			ground_bits = 2;
			break;

		case RAIL_GROUND_GRASS:
			ground_bits = 0;
			break;

		case RAIL_GROUND_FENCE_NW:
			ground_bits = 3;
			break;

		case RAIL_GROUND_FENCE_SE:
			ground_bits = GetTunnelBridgeDirection(tile) == DIAGDIR_NW ? 3 : 4;
			break;

		case RAIL_GROUND_FENCE_NE:
			ground_bits = GetTunnelBridgeDirection(tile) == DIAGDIR_SW ? 5 : 4;
			break;

		case RAIL_GROUND_FENCE_SW:
			ground_bits = 5;
			break;

		case RAIL_GROUND_FENCE_VERT1:
		case RAIL_GROUND_FENCE_VERT2:
			ground_bits = 6;
			break;

		case RAIL_GROUND_FENCE_HORIZ1:
		case RAIL_GROUND_FENCE_HORIZ2:
			ground_bits = 7;
			break;

		case RAIL_GROUND_ICE_DESERT:
			ground_bits = 1;
			break;

		default:
			NOT_REACHED();
	}
	return ground_bits;
}

static void TileLoop_TunnelBridge(TileIndex tile)
{
	const uint8 old_ground_bits = GetTunnelBridgeGroundBits(tile);
	bool snow_or_desert = false;
	switch (_settings_game.game_creation.landscape) {
		case LT_ARCTIC: {
			/* As long as we do not have a snow density, we want to use the density
			 * from the entry edge. For tunnels this is the lowest point for bridges the highest point.
			 * (Independent of foundations) */
			int z = IsBridge(tile) ? GetTileMaxZ(tile) : GetTileZ(tile);
			snow_or_desert = (z > GetSnowLine());
			break;
		}

		case LT_TROPIC:
			snow_or_desert = (GetTropicZone(tile) == TROPICZONE_DESERT);
			break;

		default:
			break;
	}

	RailGroundType new_ground;
	if (snow_or_desert) {
		new_ground = RAIL_GROUND_ICE_DESERT;
	} else {
		new_ground = RAIL_GROUND_GRASS;
		if (IsRailCustomBridgeHeadTile(tile) && old_ground_bits != 2) { // wait until bottom is green
			/* determine direction of fence */
			TrackBits rail = GetCustomBridgeHeadTrackBits(tile);
			extern RailGroundType RailTrackToFence(TileIndex tile, TrackBits rail);
			new_ground = RailTrackToFence(tile, rail);
		}
	}
	uint8 ground_bits = MapTunnelBridgeGroundTypeBits(tile, new_ground);
	if (ground_bits != old_ground_bits) {
		SetTunnelBridgeGroundBits(tile, ground_bits);
		MarkTileDirtyByTile(tile);
	}
}

static bool ClickTile_TunnelBridge(TileIndex tile)
{
	/* Show vehicles found in tunnel. */
	if (IsTunnelTile(tile)) {
		int count = 0;
		TileIndex tile_end = GetOtherTunnelBridgeEnd(tile);
		for (const Train *t : Train::Iterate()) {
			if (!t->IsFrontEngine()) continue;
			if (tile == t->tile || tile_end == t->tile) {
				ShowVehicleViewWindow(t);
				count++;
			}
			if (count > 19) break;  // no more than 20 windows open
		}
		if (count > 0) return true;
	}
	return false;
}

extern const TrackBits _road_trackbits[16];

static TrackStatus GetTileTrackStatus_TunnelBridge(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	TransportType transport_type = GetTunnelBridgeTransportType(tile);
	if (transport_type != mode || (transport_type == TRANSPORT_ROAD && !HasTileRoadType(tile, (RoadTramType)sub_mode))) return 0;

	DiagDirection dir = GetTunnelBridgeDirection(tile);

	if (side != INVALID_DIAGDIR && side == dir) return 0;
	if (mode == TRANSPORT_ROAD && IsRoadCustomBridgeHeadTile(tile)) {
		TrackBits bits = _road_trackbits[GetCustomBridgeHeadRoadBits(tile, (RoadTramType)sub_mode)];
		return CombineTrackStatus(TrackBitsToTrackdirBits(bits), TRACKDIR_BIT_NONE);
	}
	return CombineTrackStatus(TrackBitsToTrackdirBits(mode == TRANSPORT_RAIL ? GetTunnelBridgeTrackBits(tile) : DiagDirToDiagTrackBits(dir)), TRACKDIR_BIT_NONE);
}

static void UpdateRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end, bool add) {
	/* A full diagonal road has two road bits. */
	const uint middle_len = 2 * GetTunnelBridgeLength(begin, end) * TUNNELBRIDGE_TRACKBIT_FACTOR;
	const uint len = middle_len + (4 * TUNNELBRIDGE_TRACKBIT_FACTOR);

	FOR_ALL_ROADTRAMTYPES(rtt) {
		RoadType rt = GetRoadType(begin, rtt);
		if (rt == INVALID_ROADTYPE) continue;
		Company * const c = Company::GetIfValid(GetRoadOwner(begin, rtt));
		if (c != nullptr) {
			uint infra = 0;
			if (IsBridge(begin)) {
				const RoadBits bits = GetCustomBridgeHeadRoadBits(begin, rtt);
				infra += CountBits(bits) * TUNNELBRIDGE_TRACKBIT_FACTOR;
				if (bits & DiagDirToRoadBits(GetTunnelBridgeDirection(begin))) {
					infra += middle_len;
				}
			} else {
				infra += len;
			}
			if (add) {
				c->infrastructure.road[rt] += infra;
			} else {
				c->infrastructure.road[rt] -= infra;
			}
		}
	}
	FOR_ALL_ROADTRAMTYPES(rtt) {
		RoadType rt = GetRoadType(end, rtt);
		if (rt == INVALID_ROADTYPE) continue;
		Company * const c = Company::GetIfValid(GetRoadOwner(end, rtt));
		if (c != nullptr) {
			uint infra = 0;
			if (IsBridge(end)) {
				const RoadBits bits = GetCustomBridgeHeadRoadBits(end, rtt);
				infra += CountBits(bits) * TUNNELBRIDGE_TRACKBIT_FACTOR;
			}
			if (add) {
				c->infrastructure.road[rt] += infra;
			} else {
				c->infrastructure.road[rt] -= infra;
			}
		}
	}
}

void AddRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end) {
	UpdateRoadTunnelBridgeInfrastructure(begin, end, true);
}

void SubtractRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end) {
	UpdateRoadTunnelBridgeInfrastructure(begin, end, false);
}

static void UpdateRailTunnelBridgeInfrastructure(Company *c, TileIndex begin, TileIndex end, bool add) {
	const uint middle_len = GetTunnelBridgeLength(begin, end) * TUNNELBRIDGE_TRACKBIT_FACTOR;

	if (c != nullptr) {
		uint primary_count = middle_len + GetTunnelBridgeHeadOnlyPrimaryRailInfrastructureCount(begin) + GetTunnelBridgeHeadOnlyPrimaryRailInfrastructureCount(end);
		if (add) {
			c->infrastructure.rail[GetRailType(begin)] += primary_count;
		} else {
			c->infrastructure.rail[GetRailType(begin)] -= primary_count;
		}

		auto add_secondary_railtype = [&](TileIndex t) {
			uint secondary_count = GetTunnelBridgeHeadOnlySecondaryRailInfrastructureCount(t);
			if (secondary_count) {
				if (add) {
					c->infrastructure.rail[GetSecondaryRailType(t)] += secondary_count;
				} else {
					c->infrastructure.rail[GetSecondaryRailType(t)] -= secondary_count;
				}
			}
		};
		add_secondary_railtype(begin);
		add_secondary_railtype(end);

		if (IsTunnelBridgeWithSignalSimulation(begin)) {
			if (add) {
				c->infrastructure.signal += GetTunnelBridgeSignalSimulationSignalCount(begin, end);
			} else {
				c->infrastructure.signal -= GetTunnelBridgeSignalSimulationSignalCount(begin, end);
			}
		}
	}
}

void AddRailTunnelBridgeInfrastructure(Company *c, TileIndex begin, TileIndex end) {
	UpdateRailTunnelBridgeInfrastructure(c, begin, end, true);
}

void SubtractRailTunnelBridgeInfrastructure(Company *c, TileIndex begin, TileIndex end) {
	UpdateRailTunnelBridgeInfrastructure(c, begin, end, false);
}

void AddRailTunnelBridgeInfrastructure(TileIndex begin, TileIndex end) {
	UpdateRailTunnelBridgeInfrastructure(Company::GetIfValid(GetTileOwner(begin)), begin, end, true);
}

void SubtractRailTunnelBridgeInfrastructure(TileIndex begin, TileIndex end) {
	UpdateRailTunnelBridgeInfrastructure(Company::GetIfValid(GetTileOwner(begin)), begin, end, false);
}

static void ChangeTileOwner_TunnelBridge(TileIndex tile, Owner old_owner, Owner new_owner)
{
	const TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
	const TransportType tt = GetTunnelBridgeTransportType(tile);

	if (tt == TRANSPORT_ROAD && tile < other_end) {
		/* Only execute this for one of the two ends */
		SubtractRoadTunnelBridgeInfrastructure(tile, other_end);

		FOR_ALL_ROADTRAMTYPES(rtt) {
			/* Update all roadtypes, no matter if they are present */
			if (GetRoadOwner(tile, rtt) == old_owner) {
				SetRoadOwner(tile, rtt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
			}
			if (GetRoadOwner(other_end, rtt) == old_owner) {
				SetRoadOwner(other_end, rtt, new_owner == INVALID_OWNER ? OWNER_NONE : new_owner);
			}
		}

		AddRoadTunnelBridgeInfrastructure(tile, other_end);
	}

	if (!IsTileOwner(tile, old_owner)) return;

	/* Update company infrastructure counts for rail and water as well.
	 * No need to dirty windows here, we'll redraw the whole screen anyway. */

	Company *old = Company::Get(old_owner);
	if (tt == TRANSPORT_RAIL && tile < other_end) {
		/* Only execute this for one of the two ends */
		SubtractRailTunnelBridgeInfrastructure(old, tile, other_end);
		if (new_owner != INVALID_OWNER) AddRailTunnelBridgeInfrastructure(Company::Get(new_owner), tile, other_end);
	}
	if (tt == TRANSPORT_WATER) {
		/* Set number of pieces to zero if it's the southern tile as we
		 * don't want to update the infrastructure counts twice. */
		const uint num_pieces = tile < other_end ? (GetTunnelBridgeLength(tile, other_end) + 2) * TUNNELBRIDGE_TRACKBIT_FACTOR : 0;
		old->infrastructure.water -= num_pieces;
		if (new_owner != INVALID_OWNER) Company::Get(new_owner)->infrastructure.water += num_pieces;
	}

	if (new_owner != INVALID_OWNER) {
		SetTileOwner(tile, new_owner);
	} else {
		if (tt == TRANSPORT_RAIL) {
			/* Since all of our vehicles have been removed, it is safe to remove the rail
			 * bridge / tunnel. */
			CommandCost ret = DoCommand(tile, 0, 0, DC_EXEC | DC_BANKRUPT, CMD_LANDSCAPE_CLEAR);
			assert(ret.Succeeded());
		} else {
			/* In any other case, we can safely reassign the ownership to OWNER_NONE. */
			SetTileOwner(tile, OWNER_NONE);
		}
	}
}

/**
 * Frame when the 'enter tunnel' sound should be played. This is the second
 * frame on a tile, so the sound is played shortly after entering the tunnel
 * tile, while the vehicle is still visible.
 */
static const byte TUNNEL_SOUND_FRAME = 1;

/**
 * Frame when a vehicle should be hidden in a tunnel with a certain direction.
 * This differs per direction, because of visibility / bounding box issues.
 * Note that direction, in this case, is the direction leading into the tunnel.
 * When entering a tunnel, hide the vehicle when it reaches the given frame.
 * When leaving a tunnel, show the vehicle when it is one frame further
 * to the 'outside', i.e. at (TILE_SIZE-1) - (frame) + 1
 */
extern const byte _tunnel_visibility_frame[DIAGDIR_END] = {12, 8, 8, 12};

extern const byte _tunnel_turnaround_pre_visibility_frame[DIAGDIR_END] = {31, 27, 27, 31};

static VehicleEnterTileStatus VehicleEnter_TunnelBridge(Vehicle *v, TileIndex tile, int x, int y)
{
	/* Direction into the wormhole */
	const DiagDirection dir = GetTunnelBridgeDirection(tile);
	/* New position of the vehicle on the tile */
	int pos = (DiagDirToAxis(dir) == AXIS_X ? x - (TileX(tile) * TILE_SIZE) : y - (TileY(tile) * TILE_SIZE));
	/* Number of units moved by the vehicle since entering the tile */
	int frame = (dir == DIAGDIR_NE || dir == DIAGDIR_NW) ? TILE_SIZE - 1 - pos : pos;

	if (frame > (int) TILE_SIZE || frame < 0) return VETSB_CANNOT_ENTER;
	if (frame == TILE_SIZE) {
		TileIndexDiffC offset = TileIndexDiffCByDiagDir(ReverseDiagDir(dir));
		x += offset.x;
		y += offset.y;
	}

	int z = GetSlopePixelZ(x, y) - v->z_pos;

	if (abs(z) > 2) return VETSB_CANNOT_ENTER;

	if (IsTunnel(tile)) {
		/* Direction of the vehicle */
		const DiagDirection vdir = DirToDiagDir(v->direction);
		if (v->type == VEH_TRAIN) {
			Train *t = Train::From(v);

			if (!(t->track & TRACK_BIT_WORMHOLE) && dir == vdir) {
				if (t->IsFrontEngine() && frame == TUNNEL_SOUND_FRAME) {
					if (!PlayVehicleSound(t, VSE_TUNNEL) && RailVehInfo(t->engine_type)->engclass == 0) {
						SndPlayVehicleFx(SND_05_TRAIN_THROUGH_TUNNEL, v);
					}
					return VETSB_CONTINUE;
				}
				if (frame == _tunnel_visibility_frame[dir]) {
					t->tile = tile;
					t->track = TRACK_BIT_WORMHOLE;
					if (Tunnel::GetByTile(tile)->is_chunnel) SetBit(t->gv_flags, GVF_CHUNNEL_BIT);
					t->vehstatus |= VS_HIDDEN;
					t->UpdateIsDrawn();
					return VETSB_ENTERED_WORMHOLE;
				}
			}

			if (dir == ReverseDiagDir(vdir) && frame == (int) (_tunnel_visibility_frame[dir] - 1) && z == 0) {
				/* We're at the tunnel exit ?? */
				if (t->tile != tile && GetOtherTunnelEnd(t->tile) != tile) return VETSB_CONTINUE; // In chunnel
				t->tile = tile;
				t->track = DiagDirToDiagTrackBits(vdir);
				assert(t->track);
				t->vehstatus &= ~VS_HIDDEN;
				t->UpdateIsDrawn();
				return VETSB_ENTERED_WORMHOLE;
			}
		} else if (v->type == VEH_ROAD) {
			RoadVehicle *rv = RoadVehicle::From(v);

			/* Enter tunnel? */
			if (rv->state != RVSB_WORMHOLE && dir == vdir) {
				if (frame == _tunnel_visibility_frame[dir]) {
					/* Frame should be equal to the next frame number in the RV's movement */
					assert_msg(frame == rv->frame + 1 || rv->frame == _tunnel_turnaround_pre_visibility_frame[dir],
							"frame: %u, rv->frame: %u, dir: %u, _tunnel_turnaround_pre_visibility_frame[dir]: %u", frame, rv->frame, dir, _tunnel_turnaround_pre_visibility_frame[dir]);
					rv->tile = tile;
					rv->cur_image_valid_dir = INVALID_DIR;
					rv->state = RVSB_WORMHOLE;
					if (Tunnel::GetByTile(tile)->is_chunnel) SetBit(rv->gv_flags, GVF_CHUNNEL_BIT);
					rv->vehstatus |= VS_HIDDEN;
					rv->UpdateIsDrawn();
					return VETSB_ENTERED_WORMHOLE;
				} else {
					return VETSB_CONTINUE;
				}
			}

			/* We're at the tunnel exit ?? */
			if (dir == ReverseDiagDir(vdir) && frame == (int) (_tunnel_visibility_frame[dir] - 1) && z == 0) {
				if (rv->tile != tile && GetOtherTunnelEnd(rv->tile) != tile) return VETSB_CONTINUE; // In chunnel
				rv->tile = tile;
				rv->cur_image_valid_dir = INVALID_DIR;
				rv->state = DiagDirToDiagTrackdir(vdir);
				rv->frame = TILE_SIZE - (frame + 1);
				rv->vehstatus &= ~VS_HIDDEN;
				rv->UpdateIsDrawn();
				return VETSB_ENTERED_WORMHOLE;
			}
		}
	} else { // IsBridge(tile)
		if (v->vehstatus & VS_HIDDEN) return VETSB_CONTINUE; // Building bridges between chunnel portals allowed.
		if (v->type != VEH_SHIP) {
			/* modify speed of vehicle */
			uint16 spd = GetBridgeSpec(GetBridgeType(tile))->speed;

			if (v->type == VEH_ROAD) spd *= 2;
			Vehicle *first = v->First();
			first->cur_speed = min(first->cur_speed, spd);
		}

		const Direction bridge_dir = DiagDirToDir(dir);
		if (v->direction == bridge_dir) {
			switch (v->type) {
				case VEH_TRAIN: {
					/* Trains enter bridge at the first frame beyond this tile. */
					if (frame != TILE_SIZE) return VETSB_CONTINUE;
					Train *t = Train::From(v);
					t->track = TRACK_BIT_WORMHOLE;
					SetBit(t->First()->flags, VRF_CONSIST_SPEED_REDUCTION);
					ClrBit(t->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(t->gv_flags, GVF_GOINGDOWN_BIT);
					break;
				}

				case VEH_ROAD: {
					/* Non-train vehicles enter bridge at the last frame inside this tile. */
					if (frame != TILE_SIZE - 1) return VETSB_CONTINUE;
					RoadVehicle *rv = RoadVehicle::From(v);
					if (IsRoadCustomBridgeHeadTile(tile)) {
						RoadBits bits = ROAD_NONE;
						if (HasRoadTypeRoad(tile) && HasBit(rv->compatible_roadtypes, GetRoadTypeRoad(tile))) bits |= GetCustomBridgeHeadRoadBits(tile, RTT_ROAD);
						if (HasRoadTypeTram(tile) && HasBit(rv->compatible_roadtypes, GetRoadTypeTram(tile))) bits |= GetCustomBridgeHeadRoadBits(tile, RTT_TRAM);
						if (!(bits & DiagDirToRoadBits(GetTunnelBridgeDirection(tile)))) return VETSB_CONTINUE;
					}
					rv->cur_image_valid_dir = INVALID_DIR;
					rv->state = RVSB_WORMHOLE;
					/* There are no slopes inside bridges / tunnels. */
					ClrBit(rv->gv_flags, GVF_GOINGUP_BIT);
					ClrBit(rv->gv_flags, GVF_GOINGDOWN_BIT);
					break;
				}

				case VEH_SHIP:
					/* Non-train vehicles enter bridge at the last frame inside this tile. */
					if (frame != TILE_SIZE - 1) return VETSB_CONTINUE;
					Ship::From(v)->state = TRACK_BIT_WORMHOLE;
					break;

				default: NOT_REACHED();
			}
			return VETSB_ENTERED_WORMHOLE;
		} else if (v->direction == ReverseDir(bridge_dir)) {
			switch (v->type) {
				case VEH_TRAIN: {
					Train *t = Train::From(v);
					if (t->track & TRACK_BIT_WORMHOLE) {
						if (IsRailCustomBridgeHeadTile(tile)) {
							return VETSB_ENTERED_WORMHOLE;
						} else {
							v->tile = tile;
							t->track = DiagDirToDiagTrackBits(DirToDiagDir(v->direction));
						}
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				case VEH_ROAD: {
					v->tile = tile;
					RoadVehicle *rv = RoadVehicle::From(v);
					if (rv->state == RVSB_WORMHOLE) {
						rv->cur_image_valid_dir = INVALID_DIR;
						rv->state = DiagDirToDiagTrackdir(DirToDiagDir(v->direction));
						rv->frame = 0;
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				case VEH_SHIP: {
					v->tile = tile;
					Ship *ship = Ship::From(v);
					if (ship->state == TRACK_BIT_WORMHOLE) {
						ship->state = DiagDirToDiagTrackBits(DirToDiagDir(v->direction));
						return VETSB_ENTERED_WORMHOLE;
					}
					break;
				}

				default: NOT_REACHED();
			}
		} else if (v->type == VEH_TRAIN && IsRailCustomBridgeHeadTile(tile)) {
			DirDiff dir_diff = DirDifference(v->direction, bridge_dir);
			DirDiff reverse_dir_diff = DirDifference(v->direction, ReverseDir(bridge_dir));

			if (dir_diff == DIRDIFF_45RIGHT || dir_diff == DIRDIFF_45LEFT) {
				if (frame != TILE_SIZE) return VETSB_CONTINUE;

				Train *t = Train::From(v);
				TileIndex other = GetOtherTunnelBridgeEnd(tile);
				if (GetTunnelBridgeLength(tile, other) == 0 && IsRailCustomBridgeHead(other))  {
					t->track |= TRACK_BIT_WORMHOLE;
				} else {
					t->direction = bridge_dir;
					t->track = TRACK_BIT_WORMHOLE;
				}
				SetBit(t->First()->flags, VRF_CONSIST_SPEED_REDUCTION);
				ClrBit(t->gv_flags, GVF_GOINGUP_BIT);
				ClrBit(t->gv_flags, GVF_GOINGDOWN_BIT);
				return VETSB_ENTERED_WORMHOLE;
			}
			if (reverse_dir_diff == DIRDIFF_45RIGHT || reverse_dir_diff == DIRDIFF_45LEFT) {
				Train *t = Train::From(v);
				if (t->track & TRACK_BIT_WORMHOLE) return VETSB_ENTERED_WORMHOLE;
			}
		}
	}
	return VETSB_CONTINUE;
}

static CommandCost TerraformTile_TunnelBridge(TileIndex tile, DoCommandFlag flags, int z_new, Slope tileh_new)
{
	if (_settings_game.construction.build_on_slopes && AutoslopeEnabled() && IsBridge(tile) && GetTunnelBridgeTransportType(tile) != TRANSPORT_WATER) {
		DiagDirection direction = GetTunnelBridgeDirection(tile);
		Axis axis = DiagDirToAxis(direction);
		CommandCost res;
		int z_old;
		Slope tileh_old = GetTileSlope(tile, &z_old);

		if (IsRoadCustomBridgeHeadTile(tile)) {
			const RoadBits pieces = GetCustomBridgeHeadAllRoadBits(tile);
			const RoadBits entrance_piece = DiagDirToRoadBits(direction);

			/* Steep slopes behave the same as slopes with one corner raised. */
			const Slope normalised_tileh_new = IsSteepSlope(tileh_new) ? SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh_new)) : tileh_new;

			if ((_invalid_tileh_slopes_road[0][normalised_tileh_new & SLOPE_ELEVATED] & (pieces & ~entrance_piece)) != ROAD_NONE) {
				return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}
		}
		if (IsRailCustomBridgeHeadTile(tile)) {
			extern bool IsValidFlatRailBridgeHeadTrackBits(Slope normalised_slope, DiagDirection bridge_direction, TrackBits tracks);

			/* Steep slopes behave the same as slopes with one corner raised. */
			const Slope normalised_tileh_new = IsSteepSlope(tileh_new) ? SlopeWithOneCornerRaised(GetHighestSlopeCorner(tileh_new)) : tileh_new;

			if (!IsValidFlatRailBridgeHeadTrackBits(normalised_tileh_new, direction, GetCustomBridgeHeadTrackBits(tile))) {
				return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
			}
		}

		/* Check if new slope is valid for bridges in general (so we can safely call GetBridgeFoundation()) */
		if ((direction == DIAGDIR_NW) || (direction == DIAGDIR_NE)) {
			CheckBridgeSlope(BRIDGE_PIECE_SOUTH, axis, &tileh_old, &z_old);
			res = CheckBridgeSlope(BRIDGE_PIECE_SOUTH, axis, &tileh_new, &z_new);
		} else {
			CheckBridgeSlope(BRIDGE_PIECE_NORTH, axis, &tileh_old, &z_old);
			res = CheckBridgeSlope(BRIDGE_PIECE_NORTH, axis, &tileh_new, &z_new);
		}

		/* Surface slope is valid and remains unchanged? */
		if (res.Succeeded() && (z_old == z_new) && (tileh_old == tileh_new)) return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_FOUNDATION]);
	}

	return DoCommand(tile, 0, 0, flags, CMD_LANDSCAPE_CLEAR);
}

extern const TileTypeProcs _tile_type_tunnelbridge_procs = {
	DrawTile_TunnelBridge,           // draw_tile_proc
	GetSlopePixelZ_TunnelBridge,     // get_slope_z_proc
	ClearTile_TunnelBridge,          // clear_tile_proc
	nullptr,                            // add_accepted_cargo_proc
	GetTileDesc_TunnelBridge,        // get_tile_desc_proc
	GetTileTrackStatus_TunnelBridge, // get_tile_track_status_proc
	ClickTile_TunnelBridge,          // click_tile_proc
	nullptr,                            // animate_tile_proc
	TileLoop_TunnelBridge,           // tile_loop_proc
	ChangeTileOwner_TunnelBridge,    // change_tile_owner_proc
	nullptr,                            // add_produced_cargo_proc
	VehicleEnter_TunnelBridge,       // vehicle_enter_tile_proc
	GetFoundation_TunnelBridge,      // get_foundation_proc
	TerraformTile_TunnelBridge,      // terraform_tile_proc
};
