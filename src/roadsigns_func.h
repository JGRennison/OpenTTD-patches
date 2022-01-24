/* $Id: roadsigns_func.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file roadsigns_func.h Functions related to roadsigns signs. */

#include "road_map.h"

void DrawYieldSign(TileInfo* ti);
TrackdirBits GetYieldSignDisallowedDirections(TileIndex tile);
TrackdirBits GetStopSignDisallowedDirections(TileIndex tile);
CommandCost CmdBuildYieldSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char* text);
CommandCost CmdRemoveYieldSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char* text);
void ClearAllYieldSigns();

void DrawStopSign(TileInfo* ti);
TrackdirBits GetStopSignDisallowedDirections(TileIndex tile);
CommandCost CmdBuildStopSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char* text);
CommandCost CmdRemoveStopSign(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char* text);
void ClearAllStopSigns();

void DrawAdditionalSigns(TileInfo* ti);