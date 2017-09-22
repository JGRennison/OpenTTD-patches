/* $Id: dock.cpp $ */

/*
* This file is part of OpenTTD.
* OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
* OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
*/

/** @file dock.cpp Implementation of the dock base class. */

#include "stdafx.h"
#include "core/pool_func.hpp"
#include "dock_base.h"
#include "station_base.h"

/** The pool of docks. */
DockPool _dock_pool("Dock");
INSTANTIATE_POOL_METHODS(Dock)

/**
* Find a dock at a given tile.
* @param tile Tile with a dock.
* @return The dock in the given tile.
* @pre IsDockTile()
*/
/* static */ Dock *Dock::GetByTile(TileIndex tile)
{
	const Station *st = Station::GetByTile(tile);

	for (Dock *d = st->GetPrimaryDock();; d = d->next) {
		if (d->sloped == tile || d->flat == tile) return d;
		assert(d->next != NULL);
	}

	NOT_REACHED();
}
