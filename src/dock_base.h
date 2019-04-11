/* $Id: dock_base.h $ */

/*
* This file is part of OpenTTD.
* OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
* OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
*/

/** @file dock_base.h Base class for docks. */

#ifndef DOCK_BASE_H
#define DOCK_BASE_H

#include "station_type.h"
#include "tile_type.h"
#include "map_func.h"
#include "core/pool_type.hpp"

typedef Pool<Dock, DockID, 32, 64000> DockPool;
extern DockPool _dock_pool;

/** A Dock structure. */
struct Dock : DockPool::PoolItem<&_dock_pool> {
	TileIndex sloped; ///< The sloped tile of the dock.
	TileIndex flat;   ///< Position on the map of the flat tile.
	Dock *next;       ///< Next dock of the given type at this station.

	Dock(TileIndex s = INVALID_TILE, TileIndex f = INVALID_TILE) : sloped(s), flat(f), next(nullptr) { }

	~Dock() {}

	inline Dock *GetNextDock() const { return this->next; }

	inline TileIndex GetDockingTile() const
	{
		return this->flat + TileOffsByDiagDir(DiagdirBetweenTiles(this->sloped, this->flat));
	}

	static Dock *GetByTile(TileIndex tile);
};

#define FOR_ALL_DOCKS_FROM(var, start) FOR_ALL_ITEMS_FROM(Dock, dock_index, var, start)
#define FOR_ALL_DOCKS(var) FOR_ALL_DOCKS_FROM(var, 0)

#endif /* DOCK_BASE_H */
