/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file water.h Functions related to water (management) */

#ifndef WATER_H
#define WATER_H

#include "water_map.h"

/**
 * Describes the behaviour of a tile during flooding.
 */
enum FloodingBehaviour : uint8_t {
	FLOOD_NONE,    ///< The tile does not flood neighboured tiles.
	FLOOD_ACTIVE,  ///< The tile floods neighboured tiles.
	FLOOD_PASSIVE, ///< The tile does not actively flood neighboured tiles, but it prevents them from drying up.
	FLOOD_DRYUP,   ///< The tile drys up if it is not constantly flooded from neighboured tiles.
};

FloodingBehaviour GetFloodingBehaviour(TileIndex tile);
void ClearNeighbourNonFloodingStates(TileIndex tile);

void TileLoop_Water(TileIndex tile);
void TileLoopWaterFlooding(FloodingBehaviour flooding_behaviour, TileIndex tile);
bool FloodHalftile(TileIndex t);

void ConvertGroundTilesIntoWaterTiles();

void DrawShipDepotSprite(int x, int y, Axis axis, DepotPart part);
void DrawWaterClassGround(const struct TileInfo *ti);
void DrawShoreTile(Slope tileh);

void MakeWaterKeepingClass(TileIndex tile, Owner o);
void CheckForDockingTile(TileIndex t);

void RiverModifyDesertZone(TileIndex tile, void *data);
static const uint RIVER_OFFSET_DESERT_DISTANCE = 5; ///< Circular tile search diameter to create non-desert around a river tile.

bool IsWateredTile(TileIndex tile, Direction from);

void ForceClearWaterTile(TileIndex tile);

Money CanalMaintenanceCost(uint32_t num);

#endif /* WATER_H */
