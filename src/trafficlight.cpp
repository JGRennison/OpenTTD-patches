/* $Id: trafficlight.cpp $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file trafficlight.cpp Handling of trafficlights. */

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
#include "trafficlight.h"
#include "trafficlight_type.h"
#include "date_func.h"
#include "company_func.h"

#include "roadsigns_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include <set>

/* A traffic light consist (TLC) is a set of adjacent tiles with traffic lights on them.
 * They are linked together to form a big traffic light junction. */
typedef std::set<TileIndex> TLC;

/**
 * Gets the traffic light consist (a set of adjacent tiles with traffic lights).
 * If specified by the checkroadworks parameter, it returns 0 instead if road works are found within the consist.
 * @param tile           Tile of the traffic light consist.
 * @param checkroadworks Should we check for roadworks in the consist (and return 0 if found).
 * @return visited       The traffic light consist (TLC); if checkroadworks == true and roadworks were found, return 0 instead.
 */
TLC *GetTrafficLightConsist(TileIndex tile, bool checkroadworks)
{
    TLC *visited;
    TLC *candidates;

    visited = new TLC;
    candidates = new TLC;
    candidates->insert(tile);

    while (!candidates->empty())
    {
        TLC::iterator cur = candidates->begin();
        if (checkroadworks && HasRoadWorks(*cur))
        {
            delete visited;
            delete candidates;
            return 0;
        }
        uint8 distance_between_traffic_lights = _tlc_distance[_settings_game.construction.max_tlc_distance];
        for (int i = 0; i < distance_between_traffic_lights; i++)
        {
            TileIndex neighbor = *cur + ToTileIndexDiff(_tl_check_offsets[i]);
            if (HasTrafficLights(neighbor) && (visited->find(neighbor) == visited->end()))
                candidates->insert(neighbor);
        }
        visited->insert(*cur);
        candidates->erase(cur);
    }
    delete candidates;
    return visited;
}

/**
 * Gets the lowest TileIndex of the traffic light consist or 0 if roadworks
 * are found in the consist.
 * @param tile    Tile of the traffic light consist.
 * @return lowest TileIndex in the consist or 0 if roadworks were found.
 */
TileIndex GetTLCLowestTileIndexOrRoadWorks(TileIndex tile)
{
    TLC *consist = GetTrafficLightConsist(tile, true);
    TileIndex result = 0;
    if (consist != 0)
        result = *(consist->begin());
    delete consist;
    return result;
}

/**
 * Returns the state of the trafficlights on a tile.
 * @note In the scenario editor trafficlights are disabled.
 * @param tile This tile.
 * @pre        The tile must have trafficlights.
 * @return     Trafficlights state or disabled state.
 */
TrafficLightState GetTLState(TileIndex tile)
{
    assert(HasTrafficLights(tile));
    if (_game_mode == GM_EDITOR)
        return TLS_OFF; ///< All lights are off in scenario editor.
    tile = GetTLCLowestTileIndexOrRoadWorks(tile);
    if (tile == 0)
        return TLS_OFF; ///< All lights are off when roadworks are in the consist.

    uint16 tl_total = 16 * _settings_game.construction.traffic_lights_green_phase;          // There are (16 * patchsetting) "TL ticks".
    uint16 tl_tick = ((_tick_counter / 16) + 5 * TileX(tile) + 7 * TileY(tile)) % tl_total; // Each "TL tick" consists of 16 gameticks.

    if (tl_tick < ((tl_total / 2) - 2))
        return TLS_X_GREEN_Y_RED; ///< SW and NE are green, NW and SE are red.
    if (tl_tick < ((tl_total / 2) - 1))
        return TLS_X_YELLOW_Y_RED; ///< SW and NE are yellow, NW and SE are red.
    if (tl_tick < (tl_total / 2))
        return TLS_X_RED_Y_REDYELLOW; ///< SW and NE are red, NW and SE are red-yellow.
    if (tl_tick < (tl_total - 2))
        return TLS_X_RED_Y_GREEN; ///< SW and NE are red, NW and SE are green.
    if (tl_tick < (tl_total - 1))
        return TLS_X_RED_Y_YELLOW; ///< SW and NE are red, NW and SE are yellow.
    if (tl_tick < tl_total)
        return TLS_X_REDYELLOW_Y_RED; ///< SW and NE are red-yellow, NW and SE are red.

    NOT_REACHED();
    return TLS_OFF;
}

/**
 * Which directions are disallowed due to the TLState (red lights..).
 */
static const TrackdirBits _tls_to_trackdir[7] = {
    TRACKDIR_BIT_MASK,                                ///< 0) all directions disallowed
    TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_Y_SE |           ///< 1) all directions from
        TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LOWER_W | ///<    y sides
        TRACKDIR_BIT_LEFT_S | TRACKDIR_BIT_RIGHT_N,   ///<    are disallowed
    TRACKDIR_BIT_MASK,                                ///< 2) all directions disallowed
    TRACKDIR_BIT_MASK,                                ///< 3) all directions disallowed
    TRACKDIR_BIT_X_SW | TRACKDIR_BIT_X_NE |           ///< 4) all directions from
        TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_LOWER_E | ///<    x sides
        TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_RIGHT_S,   ///<    are disallowed
    TRACKDIR_BIT_MASK,                                ///< 5) all directions disallowed
    TRACKDIR_BIT_MASK,                                ///< 6) all directions disallowed
};

/**
 * Which directions in tile are allowed to be taken due to adjacent traffic lights (traffic light consist).
 * @param tile          Tile to search on.
 * @return trackdirbits Bitmask of allowed directions.
 */
TrackdirBits GetIntraTLCAllowedDirections(TileIndex tile)
{
    TrackdirBits trackdirbits = TRACKDIR_BIT_NONE;

    if (HasTrafficLights(tile + TileDiffXY(1, 0))) // SW.
        trackdirbits |= TRACKDIR_BIT_X_NE | TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_LEFT_N;
    if (HasTrafficLights(tile + TileDiffXY(0, 1))) // SE
        trackdirbits |= TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_RIGHT_N;
    if (HasTrafficLights(tile + TileDiffXY(0, -1))) // NW.
        trackdirbits |= TRACKDIR_BIT_Y_SE | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LEFT_S;
    if (HasTrafficLights(tile + TileDiffXY(-1, 0))) // NE.
        trackdirbits |= TRACKDIR_BIT_X_SW | TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_RIGHT_S;

    return trackdirbits;
}

/**
 * Get a bitmask of the directions forbidden to drive on due to traffic light(s).
 * @param tile Tile to check.
 * @return     Bitmask of forbidden directions.
 */
TrackdirBits GetTrafficLightDisallowedDirections(TileIndex tile)
{
    return (_tls_to_trackdir[GetTLState(tile)] & ~GetIntraTLCAllowedDirections(tile));
}

/**
 * Checks if the size of a traffic light consist is within the allowed range.
 * @param tile    Tile to check (can also be a new tile to be added to the TLC).
 * @return result True if the TLC size is within the allowed range, else false.
 */
bool CheckTLCSize(TileIndex tile)
{
    if (_settings_game.construction.max_tlc_size == 0)
        return true;
    TLC *consist = GetTrafficLightConsist(tile, false);
    bool result = (consist->size() <= _settings_game.construction.max_tlc_size);
    delete consist;
    return result;
}

/**
 * Build traffic lights on a crossing.
 * @param tile         Tile where to place the traffic lights.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdBuildTrafficLights(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check if traffic lights are enabled. */
    if (!_settings_game.construction.traffic_lights)
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
            if (!_settings_game.construction.allow_building_tls_in_towns)
                return_cmd_error(STR_ERROR_TRAFFIC_LIGHTS_NOT_ALLOWED_ON_TOWN_ROADS);
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

    if (!CheckTLCSize(tile))
        return_cmd_error(STR_ERROR_TRAFFIC_LIGHT_CONSIST_TOO_BIG);

    CommandCost cost = CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
    if (HasYieldSign(tile))
    {
        CommandCost ys_ret = CmdRemoveYieldSign(tile, flags, 0, 0, 0);
        if (ys_ret.Failed())
            return ys_ret;
        cost.AddCost(ys_ret);
    }

    if (HasStopSign(tile))
    {
        CommandCost ys_ret = CmdRemoveStopSign(tile, flags, 0, 0, 0);
        if (ys_ret.Failed())
            return ys_ret;
        cost.AddCost(ys_ret);
    }

    /* Now we may build the traffic lights. */
    if (flags & DC_EXEC)
    {
        MakeTrafficLights(tile);
        AddAnimatedTile(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return cost;
}

/**
 * Removes traffic lights from a tile.
 * @param tile         Tile where to remove the traffic lights.
 * @param flags        Operation to perform.
 * @param p1           Unused.
 * @param p2           Unused.
 * @return CommandCost Cost or error.
 */
CommandCost CmdRemoveTrafficLights(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
    /* Check for correct location (road with traffic lights). */
    if (!IsTileType(tile, MP_ROAD) || GetRoadTileType(tile) != ROAD_TILE_NORMAL || !HasTrafficLights(tile))
        return CMD_ERROR;

    /* Check owner, but only if a valid player is executing this command. */
    if (Company::IsValidID(_current_company))
    {
        Owner owner = GetTileOwner(tile);
        if (owner == OWNER_TOWN)
        {
            if (!_settings_game.construction.allow_building_tls_in_towns && !_cheats.magic_bulldozer.value)
                return_cmd_error(STR_ERROR_TRAFFIC_LIGHTS_NOT_ALLOWED_ON_TOWN_ROADS);
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
        DeleteAnimatedTile(tile);
        ClearTrafficLights(tile);
        MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
    }
    return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_SIGNALS]);
}
/**
 * Clear all traffic lights from the map.
 */
void ClearAllTrafficLights()
{
    /* Iterate over the whole map and remove any trafficlights found. */
    for (TileIndex tile = 0; tile < MapSize(); tile++)
    {
        if (HasTrafficLights(tile))
        {
            DoCommand(tile, 0, 0, DC_EXEC, CMD_REMOVE_TRAFFICLIGHTS);
        }
    }
}

/**
 * Draws traffic lights on a tile.
 * @param ti TileInfo of the tile to draw on.
 */
void DrawTrafficLights(TileInfo *ti)
{
    RoadBits road = GetAllRoadBits(ti->tile);
    TrafficLightState state = GetTLState(ti->tile);

    const TileIndex neighbor[4] = {ti->tile + TileDiffXY(1, 0),   // SW.
                                   ti->tile + TileDiffXY(0, 1),   // SE.
                                   ti->tile + TileDiffXY(0, -1),  // NW.
                                   ti->tile + TileDiffXY(-1, 0)}; // NE.
    const RoadBits rb[4] = {ROAD_SW, ROAD_SE, ROAD_NW, ROAD_NE};

    /* Draw the four directions. */
    byte rs = _settings_game.vehicle.road_side;
    for (int i = 0; i < 4; i++)
    {
        if (road & rb[i] && !HasTrafficLights(neighbor[i]))
        {
            DisallowedRoadDirections drd = DRD_NONE;
            if (IsTileType(neighbor[i], MP_ROAD) && IsNormalRoad(neighbor[i]))
                drd = GetDisallowedRoadDirections(neighbor[i]);
            if (drd != ((i % 2 == 0) ? DRD_SOUTHBOUND : DRD_NORTHBOUND) && drd != DRD_BOTH)
                DrawRoadDetail(_tls_to_sprites[state][i], ti, _tl_offsets[rs][i].x, _tl_offsets[rs][i].y, 12, false);
        }
    }
}