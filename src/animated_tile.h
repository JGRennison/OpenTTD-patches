/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file animated_tile.h %Tile animation! */

#ifndef ANIMATED_TILE_H
#define ANIMATED_TILE_H

#include "tile_type.h"
#include "3rdparty/cpp-btree/btree_map.h"

struct AnimatedTileInfo {
	uint8 speed = 0;
	bool pending_deletion = false;
};

extern btree::btree_map<TileIndex, AnimatedTileInfo> _animated_tiles;

#endif /* ANIMATED_TILE_H */
