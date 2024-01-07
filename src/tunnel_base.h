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

typedef Pool<Tunnel, TunnelID, 64, 1048576> TunnelPool;
extern TunnelPool _tunnel_pool;

struct Tunnel : TunnelPool::PoolItem<&_tunnel_pool> {

	TileIndex tile_n; ///< North tile of tunnel.
	TileIndex tile_s; ///< South tile of tunnel.
	uint8_t height;   ///< Tunnel height
	bool is_chunnel;  ///< Whether this tunnel is a chunnel
	uint8_t style;    ///< Style (new signals) of tunnel.

	Tunnel() {}
	~Tunnel();

	Tunnel(TileIndex tile_n, TileIndex tile_s, uint8_t height, bool is_chunnel) : tile_n(tile_n), tile_s(tile_s), height(height), is_chunnel(is_chunnel)
	{
		this->UpdateIndexes();
	}

	void UpdateIndexes();

	static inline Tunnel *GetByTile(TileIndex tile)
	{
		return Tunnel::Get(GetTunnelIndex(tile));
	}

	static void PreCleanPool();
};

#endif /* TUNNEL_BASE_H */
