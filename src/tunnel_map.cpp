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
#include "3rdparty/robin_hood/robin_hood.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include "safeguards.h"

/** All tunnel portals tucked away in a pool. */
TunnelPool _tunnel_pool("Tunnel");
INSTANTIATE_POOL_METHODS(Tunnel)

static robin_hood::unordered_map<TileIndex, TunnelID> tunnel_tile_index_map;
static btree::btree_multimap<uint64_t, Tunnel*> tunnel_axis_height_index;

static uint64_t GetTunnelAxisHeightCacheKey(TileIndex tile, uint8_t height, bool y_axis) {
	if (y_axis) {
		// tunnel extends along Y axis (DIAGDIR_SE from north end), has same X values
		return TileX(tile) | (((uint64_t) height) << 24) | (((uint64_t) 1) << 32);
	} else {
		// tunnel extends along X axis (DIAGDIR_SW from north end), has same Y values
		return TileY(tile) | (((uint64_t) height) << 24);
	}
}

static inline uint64_t GetTunnelAxisHeightCacheKey(const Tunnel* t) {
	return GetTunnelAxisHeightCacheKey(t->tile_n, t->height, t->tile_s - t->tile_n > MapMaxX());
}

/**
 * Clean up a tunnel tile
 */
Tunnel::~Tunnel()
{
	if (CleaningPool()) return;

	if (this->index >= TUNNEL_ID_MAP_LOOKUP) {
		tunnel_tile_index_map.erase(this->tile_n);
		tunnel_tile_index_map.erase(this->tile_s);
	}

	[[maybe_unused]] bool have_erased = false;
	const auto key = GetTunnelAxisHeightCacheKey(this);
	for (auto it = tunnel_axis_height_index.lower_bound(key); it != tunnel_axis_height_index.end() && it->first == key; ++it) {
		if (it->second == this) {
			tunnel_axis_height_index.erase(it);
			have_erased = true;
			break;
		}
	}
	assert(have_erased);
}

/**
 * Update tunnel indexes
 */
void Tunnel::UpdateIndexes()
{
	if (this->index >= TUNNEL_ID_MAP_LOOKUP) {
		tunnel_tile_index_map[this->tile_n] = this->index;
		tunnel_tile_index_map[this->tile_s] = this->index;
	}

	tunnel_axis_height_index.insert({ GetTunnelAxisHeightCacheKey(this), this });
}

/**
 * Tunnel pool is about to be cleaned
 */
void Tunnel::PreCleanPool()
{
	tunnel_tile_index_map.clear();
	tunnel_axis_height_index.clear();
}

TunnelID GetTunnelIndexByLookup(TileIndex t)
{
	auto iter = tunnel_tile_index_map.find(t);
	assert_tile(iter != tunnel_tile_index_map.end(), t);
	return iter->second;
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

static inline bool IsTunnelInWaySingleAxis(TileIndex tile, int z, IsTunnelInWayFlags flags, bool y_axis, TileIndexDiff tile_diff)
{
	const auto key = GetTunnelAxisHeightCacheKey(tile, z, y_axis);
	for (auto it = tunnel_axis_height_index.lower_bound(key); it != tunnel_axis_height_index.end() && it->first == key; ++it) {
		const Tunnel *t = it->second;
		if (t->tile_n > tile || tile > t->tile_s) continue;

		if (!t->is_chunnel && (flags & ITIWF_CHUNNEL_ONLY)) {
			continue;
		}
		if (t->is_chunnel && (flags & ITIWF_IGNORE_CHUNNEL)) {
			/* Only if tunnel was built over water is terraforming is allowed between portals. */
			const TileIndexDiff delta = tile_diff * 4;  // 4 tiles ramp.
			if (tile < t->tile_n + delta || t->tile_s - delta < tile) return true;
			continue;
		}
		return true;
	}
	return false;
}

/**
 * Is there a tunnel in the way in any direction?
 * @param tile the tile to search from.
 * @param z the 'z' to search on.
 * @param chunnel_allowed True if chunnel mid-parts are allowed, used when terraforming.
 * @return true if and only if there is a tunnel.
 */
bool IsTunnelInWay(TileIndex tile, int z, IsTunnelInWayFlags flags)
{
	return IsTunnelInWaySingleAxis(tile, z, flags, false, 1) || IsTunnelInWaySingleAxis(tile, z, flags, true, TileOffsByDiagDir(DIAGDIR_SE));
}

void SetTunnelSignalStyle(TileIndex t, TileIndex end, uint8_t style)
{
	if (style == 0) {
		/* Style already 0 */
		if (!HasBit(_m[t].m3, 7)) return;

		ClrBit(_m[t].m3, 7);
		ClrBit(_m[end].m3, 7);
	} else {
		SetBit(_m[t].m3, 7);
		SetBit(_m[end].m3, 7);
	}
	Tunnel::GetByTile(t)->style = style;
}

uint8_t GetTunnelSignalStyleExtended(TileIndex t)
{
	return Tunnel::GetByTile(t)->style;
}
