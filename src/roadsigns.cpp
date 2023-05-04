/* $Id: yieldsign.cpp $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yieldsign.cpp Handling of yield signs. */

#include "stdafx.h"
#include "openttd.h"
#include "landscape.h"
#include "sprite.h"
#include "viewport_func.h"
#include "road_map.h"
#include "command_func.h"
#include "cheat_func.h"
#include "animated_tile_func.h"
#include "economy_func.h"
#include "road_cmd.h"
#include "company_func.h"
#include "company_base.h"
#include "settings_type.h"
#include "roadsigns.h"
#include "date_func.h"
#include "company_func.h"
#include "station_map.h"
#include "road_map.h"
#include "highway.h"

#include "table/sprites.h"
#include "table/strings.h"

#include <set>
#include <vehicle_func.h>
#include <house.h>
#include <town.h>
#include <roadveh.h>

const uint16 min_vehicle_speed_km_h = 20;
const RoadBits rb[4] = {ROAD_SW, ROAD_SE, ROAD_NW, ROAD_NE};
const int rbToI[ROAD_NE + 1] = {-1, 2, 0, -1, 1, -1, -1, -1, 3};
const TileIndexDiffC neighbors[4] = {{1, 0},   // SW.
                                     {0, 1},   // SE.
                                     {0, -1},  // NW.
                                     {-1, 0}}; // NE.

RoadSignDirection GetYieldSignDirection(TileIndex tile)
{
    RoadBits road = GetAllRoadBits(tile);
    RoadBits whereToBuild = (road & ROAD_X) == ROAD_X ? road & ~ROAD_X : road & ~ROAD_Y;

    int i = rbToI[whereToBuild];
    if (i > ROAD_SIGN_DIRECTION_END)
        return ROAD_SIGN_DIRECTION_NONE;

    const TileIndex neededNeighbor = AddTileIndexDiffCWrap(tile, neighbors[i]);
    if (IsAnyRoadStopTile(neededNeighbor) || IsRoadBridgeHeadTile(neededNeighbor) || IsRoadTunnelTile(neededNeighbor))
        return (RoadSignDirection)i;

    if (neededNeighbor == INVALID_TILE || !IsTileType(neededNeighbor, MP_ROAD))
        return ROAD_SIGN_DIRECTION_NONE;
    if (GetRoadTileType(neededNeighbor) == ROAD_TILE_CROSSING)
        return (RoadSignDirection)i;
    if (GetRoadTileType(neededNeighbor) == ROAD_TILE_NORMAL)
    {
        RoadBits neighborRoad = GetAllRoadBits(neededNeighbor);
        if (!(neighborRoad & rb[3 - i]))
            return ROAD_SIGN_DIRECTION_NONE;

        DisallowedRoadDirections drd = GetDisallowedRoadDirections(neededNeighbor);
        if (drd == ((i % 2 == 0) ? DRD_SOUTHBOUND : DRD_NORTHBOUND) || drd == DRD_BOTH)
            return ROAD_SIGN_DIRECTION_NONE;
    }

    return (RoadSignDirection)i;
}

static const TrackdirBits _ys_to_trackdir[4] = {
    /*TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE |           ///< 1) all directions from
		TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LOWER_W |    ///<    y sides
		TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_RIGHT_N,      ///<    are disallowed
	TRACKDIR_BIT_MASK,                                ///< 2) all directions disallowed
	TRACKDIR_BIT_MASK,                                ///< 3) all directions disallowed
	TRACKDIR_BIT_X_SW | TRACKDIR_BIT_X_NE |           ///< 4) all directions from
		TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_LOWER_E |    ///<    x sides
		TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_RIGHT_S,      ///<    are disallowed
	TRACKDIR_BIT_MASK,                                ///< 5) all directions disallowed
	TRACKDIR_BIT_MASK,                                ///< 6) all directions disallowed*/
    TRACKDIR_BIT_X_SW | TRACKDIR_BIT_X_NE |
        TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_LOWER_E | ///<    x sides
        TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_RIGHT_S,
    TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE |
        TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LOWER_W | ///<    y sides
        TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_RIGHT_N,
    TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE |
        TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LOWER_W | ///<    y sides
        TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_RIGHT_N,
    TRACKDIR_BIT_X_SW | TRACKDIR_BIT_X_NE |
        TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_LOWER_E | ///<    x sides
        TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_RIGHT_S,
};

struct VehicleChekTowardsData
{
    int z;
    DiagDirection direction;
};

static Vehicle *CheckVehicleHeadingNeededDirection(Vehicle *v, void *data)
{
    VehicleChekTowardsData *d = (VehicleChekTowardsData *)data;
    return v->z_pos > d->z || !IsValidDiagDirection(DirToDiagDir(v->direction)) || DirToDiagDir(v->direction) != d->direction || v->cur_speed < (min_vehicle_speed_km_h * 2) + 2
               ? nullptr
               : v;
}

struct VehicleOnIntersectionCheckData
{
    int z;
    bool isMoving;
};

static Vehicle *CheckVehicleIsMovingOnTile(Vehicle *v, void *data)
{
    VehicleOnIntersectionCheckData *d = (VehicleOnIntersectionCheckData *)data;
    return v->z_pos > d->z || (!d->isMoving && v->cur_speed > 5) || (d->isMoving && v->cur_speed <= 5)
               ? nullptr
               : v;
}

static bool CheckVehicleOnTile(TileIndex tile, TileIndex towards)
{
    int z = GetTileMaxPixelZ(tile);
    DiagDirection direction = DiagdirBetweenTiles(tile, towards);
    VehicleChekTowardsData data = {z, direction};
    return HasVehicleOnPos(tile, VEH_ROAD, &data, CheckVehicleHeadingNeededDirection);
}

static bool CheckIsAnyMovingVehicleOnTile(TileIndex tile)
{
    int z = GetTileMaxPixelZ(tile);
    VehicleOnIntersectionCheckData data = {z, true};

    return HasVehicleOnPos(tile, VEH_ROAD, &data, CheckVehicleIsMovingOnTile);
}

static bool CheckIsAnyMovingOrWaitingVehicleOnTile(TileIndex tile, bool isMoving)
{
    int x = TileX(tile) * TILE_SIZE;
    int y = TileY(tile) * TILE_SIZE;
    int z = GetTileMaxPixelZ(tile);

    VehicleOnIntersectionCheckData data = {z, isMoving};

    return HasVehicleOnPosXY(x, y, VEH_ROAD, &data, CheckVehicleIsMovingOnTile);
}

TrackdirBits GetYieldSignDisallowedDirections(TileIndex tile)
{
    RoadSignDirection direction = GetYieldSignDirection(tile);
    if (direction == ROAD_SIGN_DIRECTION_NONE)
        return TRACKDIR_BIT_NONE;

    RoadBits other = GetAllRoadBits(tile) & ~rb[direction];
    RoadBits other1, other2;
    for (int i = 0; i < 4; i++)
    {
        if (other & rb[i])
        {
            other1 = rb[i];
            other2 = other & ~rb[i];
            break;
        }
    }

    TileIndex otherTile1 = AddTileIndexDiffCWrap(tile, neighbors[rbToI[other1]]);
    TileIndex otherTile2 = AddTileIndexDiffCWrap(tile, neighbors[rbToI[other2]]);

    if (CheckVehicleOnTile(tile, tile) || CheckVehicleOnTile(otherTile1, tile) || CheckVehicleOnTile(otherTile2, tile))
    {
        return _ys_to_trackdir[direction];
    }

    return TRACKDIR_BIT_NONE;
}

TrackdirBits GetStopSignDisallowedDirections(TileIndex tile)
{
    if (CheckIsAnyMovingVehicleOnTile(tile))
        return TRACKDIR_BIT_MASK; // If there is any vehicle moving on intersection - stop;
    //if (!CheckIsAnyMovingOrWaitingVehicleOnTile(tile, false))  return TRACKDIR_BIT_MASK; // If there is no vehicle waiting on intersection - stop;

    return TRACKDIR_BIT_NONE;
}

/**
 * Build yieldsigns on a crossing.
 * @param tile         Tile where to place the traffic yield sign.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdBuildYieldSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check if road signs are enabled. */
    if (!_settings_game.construction.road_signs)
        return CMD_ERROR; // Sanity check.

    /* Check for correct location (road). */
    if (!IsTileType(tile, MP_ROAD) || GetRoadTileType(tile) != ROAD_TILE_NORMAL)
        return_cmd_error(STR_ERROR_THERE_IS_NO_ROAD);

    /* Check owner only if a valid player is executing this command. */
    if (Company::IsValidID(_current_company))
    {
        Owner owner = GetTileOwner(tile);
        if (owner == OWNER_TOWN)
        {
            if (!_settings_game.construction.allow_building_rs_in_towns)
                return_cmd_error(STR_ERROR_ROAD_SIGNS_NOT_ALLOWED_ON_TOWN_ROADS);
        }
        else
        {
            if (owner != OWNER_NONE && !IsTileOwner(tile, _current_company))
                return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER); // Owned by ... already displayed in CheckOwnership.
        }
    }

    /* Check junction and already built. */
    if (CountBits(GetAllRoadBits(tile)) != 3)
        return_cmd_error(STR_ERROR_CAN_ONLY_BE_PLACED_ON_3WAY_ROAD_JUNCTIONS);
    if (HasTrafficLights(tile))
        return_cmd_error(STR_ERROR_ALREADY_BUILT);
    if (HasYieldSign(tile))
        return_cmd_error(STR_ERROR_ALREADY_BUILT);
    if (HasStopSign(tile))
        return_cmd_error(STR_ERROR_ALREADY_BUILT);

    RoadSignDirection direction = GetYieldSignDirection(tile);
    if (direction == ROAD_SIGN_DIRECTION_NONE)
        return_cmd_error(STR_ERROR_CAN_ONLY_BE_PLACED_ON_3WAY_ROAD_JUNCTIONS);

    /* Now we may build the traffic lights. */
    if (flags & DC_EXEC)
    {
        MakeYieldSign(tile);
        //AddAnimatedTile(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
}

/**
 * Removes yield sign from a tile.
 * @param tile         Tile where to remove the yield sign.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdRemoveYieldSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check for correct location (road with traffic lights). */
    if (!IsTileType(tile, MP_ROAD) || GetRoadTileType(tile) != ROAD_TILE_NORMAL || !HasYieldSign(tile))
        return CMD_ERROR;

    /* Check owner, but only if a valid player is executing this command. */
    if (Company::IsValidID(_current_company))
    {
        Owner owner = GetTileOwner(tile);
        if (owner == OWNER_TOWN)
        {
            if (!_settings_game.construction.allow_building_rs_in_towns && !_cheats.magic_bulldozer.value)
                return_cmd_error(STR_ERROR_ROAD_SIGNS_NOT_ALLOWED_ON_TOWN_ROADS);
        }
        else
        {
            if (owner != OWNER_NONE && !IsTileOwner(tile, _current_company))
                return CMD_ERROR; // Owned by ... already displayed in CheckOwnership.
        }
    }

    /* Now we may remove the traffic lights. */
    if (flags & DC_EXEC)
    {
        //DeleteAnimatedTile(tile);
        ClearYieldSign(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
}
/**
 * Clear all yield signs from the map.
 */
void ClearAllYieldSigns()
{
    /* Iterate over the whole map and remove any trafficlights found. */
    for (TileIndex tile = 0; tile < MapSize(); tile++)
    {
        if (HasYieldSign(tile))
        {
            DoCommand(tile, 0, 0, DC_EXEC, CMD_REMOVE_YIELDSIGN);
        }
    }
}

/**
 * Build stop signs on a crossing.
 * @param tile         Tile where to place the stop sign.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdBuildStopSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check if road signs are enabled. */
    if (!_settings_game.construction.road_signs)
        return CMD_ERROR; // Sanity check.

    /* Check for correct location (road). */
    if (!IsTileType(tile, MP_ROAD) || GetRoadTileType(tile) != ROAD_TILE_NORMAL)
        return_cmd_error(STR_ERROR_THERE_IS_NO_ROAD);

    /* Check owner only if a valid player is executing this command. */
    if (Company::IsValidID(_current_company))
    {
        Owner owner = GetTileOwner(tile);
        if (owner == OWNER_TOWN)
        {
            if (!_settings_game.construction.allow_building_rs_in_towns)
                return_cmd_error(STR_ERROR_ROAD_SIGNS_NOT_ALLOWED_ON_TOWN_ROADS);
        }
        else
        {
            if (owner != OWNER_NONE && !IsTileOwner(tile, _current_company))
                return_cmd_error(STR_ERROR_AREA_IS_OWNED_BY_ANOTHER); // Owned by ... already displayed in CheckOwnership.
        }
    }

    /* Check junction and already built. */
    if (CountBits(GetAllRoadBits(tile)) < 3)
        return_cmd_error(STR_ERROR_CAN_ONLY_BE_PLACED_ON_ROAD_JUNCTIONS);
    if (HasTrafficLights(tile))
        return_cmd_error(STR_ERROR_ALREADY_BUILT);
    if (HasStopSign(tile))
        return_cmd_error(STR_ERROR_ALREADY_BUILT);

    CommandCost cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
    if (HasYieldSign(tile))
    {
        CommandCost ys_ret = CmdRemoveYieldSign(tile, flags, 0, 0, 0);
        if (ys_ret.Failed())
            return ys_ret;
        cost.AddCost(ys_ret);
    }

    /* Now we may build the traffic lights. */
    if (flags & DC_EXEC)
    {
        MakeStopSign(tile);
        //AddAnimatedTile(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return cost;
}

/**
 * Removes stop sign from a tile.
 * @param tile         Tile where to remove the stop sign.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdRemoveStopSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check for correct location (road with traffic lights). */
    if (!IsTileType(tile, MP_ROAD) || GetRoadTileType(tile) != ROAD_TILE_NORMAL || !HasStopSign(tile))
        return CMD_ERROR;

    /* Check owner, but only if a valid player is executing this command. */
    if (Company::IsValidID(_current_company))
    {
        Owner owner = GetTileOwner(tile);
        if (owner == OWNER_TOWN)
        {
            if (!_settings_game.construction.allow_building_rs_in_towns && !_cheats.magic_bulldozer.value)
                return_cmd_error(STR_ERROR_ROAD_SIGNS_NOT_ALLOWED_ON_TOWN_ROADS);
        }
        else
        {
            if (owner != OWNER_NONE && !IsTileOwner(tile, _current_company))
                return CMD_ERROR; // Owned by ... already displayed in CheckOwnership.
        }
    }

    /* Now we may remove the traffic lights. */
    if (flags & DC_EXEC)
    {
        //DeleteAnimatedTile(tile);
        ClearStopSign(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
}
/**
 * Clear all yield signs from the map.
 */
void ClearAllStopSigns()
{
    /* Iterate over the whole map and remove any trafficlights found. */
    for (TileIndex tile = 0; tile < MapSize(); tile++)
    {
        if (HasStopSign(tile))
        {
            DoCommand(tile, 0, 0, DC_EXEC, CMD_REMOVE_STOPSIGN);
        }
    }
}

/**
 * Draws yield sign on a tile.
 * @param ti TileInfo of the tile to draw on.
 */
void DrawYieldSign(TileInfo *ti)
{
    RoadSignDirection direction = GetYieldSignDirection(ti->tile);
    if (direction == ROAD_SIGN_DIRECTION_NONE)
        return;

    /* Draw the four directions. */
    byte rs = _settings_game.vehicle.road_side;
    DrawRoadDetail(_ys_to_sprites[direction], ti, _ys_offsets[rs][direction].x, _ys_offsets[rs][direction].y, 11, false);
}

/**
 * Draws stop sign on a tile.
 * @param ti TileInfo of the tile to draw on.
 */
void DrawStopSign(TileInfo *ti)
{
    RoadBits road = GetAllRoadBits(ti->tile);

    const TileIndex neighbor[4] = {ti->tile + TileDiffXY(1, 0),   // SW.
                                   ti->tile + TileDiffXY(0, 1),   // SE.
                                   ti->tile + TileDiffXY(0, -1),  // NW.
                                   ti->tile + TileDiffXY(-1, 0)}; // NE.
    const RoadBits rb[4] = {ROAD_SW, ROAD_SE, ROAD_NW, ROAD_NE};

    /* Draw the four directions. */
    byte rs = _settings_game.vehicle.road_side;
    for (int i = 0; i < 4; i++)
    {
        if (road & rb[i] && !HasYieldSign(neighbor[i]))
        {
            DisallowedRoadDirections drd = DRD_NONE;
            if (IsTileType(neighbor[i], MP_ROAD) && IsNormalRoad(neighbor[i]))
                drd = GetDisallowedRoadDirections(neighbor[i]);
            if (drd != ((i % 2 == 0) ? DRD_SOUTHBOUND : DRD_NORTHBOUND) && drd != DRD_BOTH)
                DrawRoadDetail(_ss_to_sprites[i], ti, _ys_offsets[rs][i].x, _ys_offsets[rs][i].y, 11, false);
        }
    }
}

void DrawStreetCrossingSign(TileInfo *ti)
{
    if (!IsInTown(ti->tile) || !IsTileType(ti->tile, MP_ROAD))
        return;
    byte rs = _settings_game.vehicle.road_side;
    if (CountBits(GetAllRoadBits(ti->tile)) == 3)
    {
        RoadBits road = GetAllRoadBits(ti->tile);
        RoadBits whereToBuild = (road & ROAD_X) == ROAD_X ? road & ~ROAD_X : road & ~ROAD_Y;

        int i = rbToI[whereToBuild];
        DrawRoadDetail(SPR_STREET_CROSSING, ti, _sc_offsets[rs][i].x, _sc_offsets[rs][i].y, 11, false);
    }
    else if (CountBits(GetAllRoadBits(ti->tile)) > 3)
    {
        DrawRoadDetail(SPR_STREET_CROSSING, ti, _sc_offsets[rs][ROAD_SIGN_DIRECTION_SW].x, _sc_offsets[rs][ROAD_SIGN_DIRECTION_SW].y, 10, false);
        DrawRoadDetail(SPR_STREET_CROSSING, ti, _sc_offsets[rs][ROAD_SIGN_DIRECTION_NE].x, _sc_offsets[rs][ROAD_SIGN_DIRECTION_NE].y, 10, false);
    }
}

void DrawHydrant(TileInfo *ti)
{
    if (ti->tile % 3 != 0 || !IsInTown(ti->tile) || !IsTileType(ti->tile, MP_ROAD))
        return;

    RoadBits road = GetAllRoadBits(ti->tile);
    if (!IsStraightRoad(GetAllRoadBits(ti->tile)) || IsHighway(ti->tile))
        return;
    int whereToBuild = (road & ROAD_X) == ROAD_X ? 0 : 1;
    byte rs = _settings_game.vehicle.road_side;
    DisallowedRoadDirections oneway = GetDisallowedRoadDirections(ti->tile);
    if (oneway == DRD_SOUTHBOUND)
        rs = 1 - rs; // To avoid situations when it's placed on highway's wrong side

    DrawRoadDetail(SPR_HYDRANT, ti, _hydrant_offsets[rs][whereToBuild].x, _hydrant_offsets[rs][whereToBuild].y, 6, false);
}

void DrawAdditionalSigns(TileInfo *ti)
{
    DrawStreetCrossingSign(ti);
    DrawHydrant(ti);
}
