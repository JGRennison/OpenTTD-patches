/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file highway.h Base of the highway class. */

#include "town.h"

/**
 * Checks if the road is highway
 * @param tile the tile to check
 * @return is tile highway
 */
static inline bool IsHighway(TileIndex tile)
{
	if (!IsOneWayRoad(tile))
		return false;
	if (_settings_game.vehicle.one_way_roads_out_town_as_highway && !IsInTown(tile))
		return true;

	RoadBits road = GetRoadBits(tile, RTT_ROAD);
	if (road != ROAD_X && road != ROAD_Y)
		return false;

	DisallowedRoadDirections drd = GetDisallowedRoadDirections(tile);
	Direction currentDirection =
		road == ROAD_X
			? (drd == DRD_NORTHBOUND ? DIR_NE : DIR_SW)
			: (drd == DRD_NORTHBOUND ? DIR_SE : DIR_NW);

	bool isRight = _settings_game.vehicle.road_side;
	DirDiff directionDiff = isRight ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT;
	Direction direction = ChangeDir(currentDirection, directionDiff);
	TileIndex needed_tile = TileAddByDir(tile, direction);
	return IsTileType(needed_tile, MP_OBJECT);
}