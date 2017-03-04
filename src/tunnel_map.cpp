/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnel_map.cpp Map accessors for tunnels. */

#include "stdafx.h"
#include "tunnelbridge_map.h"

#include "core/pool_func.hpp"

#include "safeguards.h"

/** All tunnel portals tucked away in a pool. */
TunnelPool _tunnel_pool("Tunnel");
INSTANTIATE_POOL_METHODS(Tunnel)

/**
 * Clean up a tunnel tile
 */
Tunnel::~Tunnel()
{
	if (CleaningPool()) return;
}

/**
 * Gets the other end of the tunnel. Where a vehicle would reappear when it
 * enters at the given tile.
 * @param tile the tile to search from.
 * @return the tile of the other end of the tunnel.
 */
TileIndex GetOtherTunnelEnd(TileIndex tile)
{
	Tunnel *t = Tunnel::GetByTile(tile);
	return t->tile_n == tile ? t->tile_s : t->tile_n;
}

/**
 * Is there a tunnel in the way in any direction?
 * @param tile the tile to search from.
 * @param z the 'z' to search on.
 * @param not_allowed Only terra forming does not search between tunnel portals.
 * @return true if and only if there is a tunnel.
 */
bool IsTunnelInWay(TileIndex tile, int z, bool not_allowed)
{
	uint x = TileX(tile);
	uint y = TileY(tile);

	Tunnel *t;
	FOR_ALL_TUNNELS(t)  {
		if (t->tile_n > tile || tile > t->tile_s) continue;

		if (t->tile_s - t->tile_n > MapMaxX()){
			if (TileX(t->tile_n) != x || (int)TileHeight(t->tile_n) != z) continue; // dir DIAGDIR_SE
		} else {
			if (TileY(t->tile_n) != y || (int)TileHeight(t->tile_n) != z) continue; // dir DIAGDIR_SW
		}

		if (t->is_chunnel > not_allowed) {
			/* Only if tunnel was build over water terraforming is allowed between portals. */
			TileIndexDiff delta = GetTunnelBridgeDirection(t->tile_n) == DIAGDIR_SE ? TileOffsByDiagDir(DIAGDIR_SE) * 4 : 4;  // 4 tiles ramp.
			if (tile < t->tile_n + delta || t->tile_s - delta < tile) return true;
			continue;
		}
		return true;
	}
	return false;
}
