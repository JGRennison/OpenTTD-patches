/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tile_map_extra.h Map writing/reading functions for tiles - separate from tile_map.h so that that does not have to include settings_type.h. */

#ifndef TILE_MAP_EXTRA_H
#define TILE_MAP_EXTRA_H

#include "map_func.h"
#include "settings_type.h"

/**
 * Check if a tile is within the map (not a border)
 *
 * @param tile The tile to check
 * @return Whether the tile is in the interior of the map
 * @pre tile < Map::Size()
 */
inline bool IsInnerTile(TileIndex tile)
{
	dbg_assert_tile(tile < Map::Size(), tile);

	uint x = TileX(tile);
	uint y = TileY(tile);

	return x < Map::MaxX() && y < Map::MaxY() && ((x > 0 && y > 0) || !_settings_game.construction.freeform_edges);
}

#endif /* TILE_MAP_EXTRA_H */
