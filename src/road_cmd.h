/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_cmd.h Road related functions. */

#ifndef ROAD_CMD_H
#define ROAD_CMD_H

#include "direction_type.h"
#include "road_type.h"
#include "command_type.h"

void DrawRoadDepotSprite(int x, int y, DiagDirection dir, RoadType rt);
void UpdateNearestTownForRoadTiles(bool invalidate);

enum class BuildRoadFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	NoCustomBridgeHeads   = (1U << 0), ///< Disable custom bridge heads.
};
DECLARE_ENUM_AS_BIT_SET(BuildRoadFlags)

DEF_CMD_TUPLE(CMD_BUILD_LONG_ROAD,  CmdBuildLongRoad,  CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, RoadType, Axis, DisallowedRoadDirections, bool, bool, bool>)
DEF_CMD_TUPLE(CMD_REMOVE_LONG_ROAD, CmdRemoveLongRoad,              CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, RoadType, Axis, bool, bool>) // towns may disallow removing road bits (as they are connected) in test, but in exec they're removed and thus removing is allowed.
DEF_CMD_TUPLE(CMD_BUILD_ROAD,       CmdBuildRoad,      CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<RoadBits, RoadType, DisallowedRoadDirections, TownID, BuildRoadFlags>)
DEF_CMD_TUPLE(CMD_BUILD_ROAD_DEPOT, CmdBuildRoadDepot,             CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<RoadType, DiagDirection>)
DEF_CMD_TUPLE(CMD_CONVERT_ROAD,     CmdConvertRoad,                                     {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, RoadType, bool>)

#endif /* ROAD_CMD_H */
