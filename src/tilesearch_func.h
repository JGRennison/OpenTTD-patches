/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tilesearch_func.h Tile search functions. */

#ifndef TILESEARCH_FUNC_H
#define TILESEARCH_FUNC_H

#include "tile_type.h"

/**
 * A callback function type for searching tiles.
 *
 * @param tile The tile to test
 * @param user_data additional data for the callback function to use
 * @return A boolean value, depend on the definition of the function.
 */
typedef bool TestTileOnSearchProc(TileIndex tile, void *user_data);

bool CircularTileSearch(TileIndex *tile, uint size, TestTileOnSearchProc proc, void *user_data);
bool CircularTileSearch(TileIndex *tile, uint radius, uint w, uint h, TestTileOnSearchProc proc, void *user_data);

bool EnoughContiguousTilesMatchingCondition(TileIndex tile, uint threshold, TestTileOnSearchProc proc, void *user_data);

/**
 * A callback function type for iterating tiles.
 *
 * @param tile The tile to test
 * @param user_data additional data for the callback function to use
 */
typedef void TileIteratorProc(TileIndex tile, void *user_data);

void IterateCurvedCircularTileArea(TileIndex centre_tile, uint diameter, TileIteratorProc proc, void *user_data);

#endif /* TILESEARCH_FUNC_H */
