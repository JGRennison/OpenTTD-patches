/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnel_base.h Base for all tunnels */

#ifndef TUNNEL_BASE_H
#define TUNNEL_BASE_H

#include "tunnel_map.h"
#include "core/pool_type.hpp"

struct Tunnel;

typedef Pool<Tunnel, TunnelID, 64, 64000> TunnelPool;
extern TunnelPool _tunnel_pool;

struct Tunnel : TunnelPool::PoolItem<&_tunnel_pool> {

	TileIndex tile_n; // North tile of tunnel.
	TileIndex tile_s; // South tile of tunnel.
	bool is_chunnel;

	Tunnel(TileIndex tile_n = INVALID_TILE) : tile_n(tile_n) {}
	~Tunnel();

	static inline Tunnel *GetByTile(TileIndex tile)
	{
		return Tunnel::Get(GetTunnelIndex(tile));
	}
};

#define FOR_ALL_TUNNELS_FROM(var, start) FOR_ALL_ITEMS_FROM(Tunnel, tunnel_index, var, start)
#define FOR_ALL_TUNNELS(var) FOR_ALL_TUNNELS_FROM(var, 0)

#endif /* TUNNEL_BASE_H */
