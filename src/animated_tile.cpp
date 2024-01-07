/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file animated_tile.cpp Everything related to animated tiles. */

#include "stdafx.h"
#include "animated_tile.h"
#include "core/alloc_func.hpp"
#include "core/container_func.hpp"
#include "tile_cmd.h"
#include "viewport_func.h"
#include "framerate_type.h"
#include "date_func.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include INCLUDE_FOR_PREFETCH_NTA

#include "safeguards.h"

/** The table/list with animated tiles. */
btree::btree_map<TileIndex, AnimatedTileInfo> _animated_tiles;

/**
 * Removes the given tile from the animated tile table.
 * @param tile the tile to remove
 */
void DeleteAnimatedTile(TileIndex tile)
{
	auto to_remove = _animated_tiles.find(tile);
	if (to_remove != _animated_tiles.end() && !to_remove->second.pending_deletion) {
		to_remove->second.pending_deletion = true;
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
}

static void UpdateAnimatedTileSpeed(TileIndex tile, AnimatedTileInfo &info)
{
	extern uint8_t GetAnimatedTileSpeed_Town(TileIndex tile);
	extern uint8_t GetAnimatedTileSpeed_Station(TileIndex tile);
	extern uint8_t GetAnimatedTileSpeed_Industry(TileIndex tile);
	extern uint8_t GetNewObjectTileAnimationSpeed(TileIndex tile);

	switch (GetTileType(tile)) {
		case MP_HOUSE:
			info.speed = GetAnimatedTileSpeed_Town(tile);
			break;

		case MP_STATION:
			info.speed = GetAnimatedTileSpeed_Station(tile);
			break;

		case MP_INDUSTRY:
			info.speed = GetAnimatedTileSpeed_Industry(tile);
			break;

		case MP_OBJECT:
			info.speed = GetNewObjectTileAnimationSpeed(tile);
			break;

		default:
			info.speed = 0;
			break;
	}
}

/**
 * Add the given tile to the animated tile table (if it does not exist
 * on that table yet). Also increases the size of the table if necessary.
 * @param tile the tile to make animated
 */
void AddAnimatedTile(TileIndex tile, bool mark_dirty)
{
	if (mark_dirty) MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	AnimatedTileInfo &info = _animated_tiles[tile];
	UpdateAnimatedTileSpeed(tile, info);
	info.pending_deletion = false;
}

int GetAnimatedTileSpeed(TileIndex tile)
{
	const auto iter = _animated_tiles.find(tile);
	if (iter != _animated_tiles.end() && !iter->second.pending_deletion) {
		return iter->second.speed;
	}
	return -1;
}

/**
 * Animate all tiles in the animated tile list, i.e.\ call AnimateTile on them.
 */
void AnimateAnimatedTiles()
{
	extern void AnimateTile_Town(TileIndex tile);
	extern void AnimateTile_Station(TileIndex tile);
	extern void AnimateTile_Industry(TileIndex tile);
	extern void AnimateTile_Object(TileIndex tile);

	PerformanceAccumulator framerate(PFE_GL_LANDSCAPE);

	const uint32_t ticks = (uint) _scaled_tick_counter;
	const uint8_t max_speed = (ticks == 0) ? 32 : FindFirstBit(ticks);

	auto iter = _animated_tiles.begin();
	while (iter != _animated_tiles.end()) {
		if (iter->second.pending_deletion) {
			iter = _animated_tiles.erase(iter);
			continue;
		}

		auto next = iter;
		++next;
		if (next != _animated_tiles.end()) {
			PREFETCH_NTA(&(next->second));
		}

		if (iter->second.speed <= max_speed) {
			const TileIndex curr = iter->first;
			switch (GetTileType(curr)) {
				case MP_HOUSE:
					AnimateTile_Town(curr);
					break;

				case MP_STATION:
					AnimateTile_Station(curr);
					break;

				case MP_INDUSTRY:
					AnimateTile_Industry(curr);
					break;

				case MP_OBJECT:
					AnimateTile_Object(curr);
					break;

				default:
					NOT_REACHED();
			}
		}
		iter = next;
	}
}

void UpdateAllAnimatedTileSpeeds()
{
	auto iter = _animated_tiles.begin();
	while (iter != _animated_tiles.end()) {
		if (iter->second.pending_deletion) {
			iter = _animated_tiles.erase(iter);
			continue;
		}
		UpdateAnimatedTileSpeed(iter->first, iter->second);
		++iter;
	}
}

/**
 * Initialize all animated tile variables to some known begin point
 */
void InitializeAnimatedTiles()
{
	_animated_tiles.clear();
}
