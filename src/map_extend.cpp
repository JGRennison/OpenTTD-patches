/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file map_extend.cpp Enlarge the map and reposition the existing content (the 'extend_map' console command). */

#include "stdafx.h"
#include "map_extend.h"
#include "map_func.h"
#include "tile_type.h"
#include "tile_map.h"
#include "water_map.h"
#include "void_map.h"
#include "core/bitmath_func.hpp"

#include "vehicle_base.h"
#include "vehicle_func.h"
#include "train.h"
#include "roadveh.h"
#include "ship.h"
#include "track_type.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "base_station_base.h"
#include "station_base.h"
#include "roadstop_base.h"
#include "waypoint_base.h"
#include "tunnel_base.h"
#include "town.h"
#include "industry.h"
#include "depot_base.h"
#include "signs_base.h"
#include "object_base.h"
#include "cargopacket.h"
#include "company_base.h"
#include "animated_tile.h"
#include "viewport_func.h"
#include "viewport_kdtree.h"
#include "gfx_func.h"

#include <vector>
#include <utility>

#include "safeguards.h"

static uint _reloc_dx;    ///< tile offset applied to every absolute tile/coord
static uint _reloc_dy;
static uint _reloc_old_w; ///< OLD map width, used to decode stored tile indices before re-encoding into the new map.

/** Shift an absolute tile by the relocation offset (INVALID_TILE passes through).
 * The input index is decoded with the OLD width and re-encoded with the new map. */
static inline TileIndex RelocTile(TileIndex t)
{
	if (t == INVALID_TILE) return t;
	const uint32_t v = t.base();
	return TileXY((v % _reloc_old_w) + _reloc_dx, (v / _reloc_old_w) + _reloc_dy);
}

/** Whether (w, h) is a valid new map size: each a power of two within the engine limits. */
static bool ValidNewSize(uint w, uint h)
{
	if ((w & (w - 1)) != 0 || (h & (h - 1)) != 0) return false;          // powers of two
	if (w < MIN_MAP_SIZE || h < MIN_MAP_SIZE) return false;
	if (w > MAX_MAP_SIZE || h > MAX_MAP_SIZE) return false;
	if ((uint64_t)w * (uint64_t)h > MAX_MAP_TILES) return false;
	return true;
}

/**
 * Enlarge the map and move the current map content into it at a chosen offset.
 * The new area is filled with sea, every absolute tile index and world coordinate stored
 * in the object pools is shifted by the offset, and the derived caches are rebuilt.
 * @param new_w New map width (power of two, at least the current width).
 * @param new_h New map height (power of two, at least the current height).
 * @param dx Tile X offset at which the current map's north-west corner is placed.
 * @param dy Tile Y offset at which the current map's north-west corner is placed.
 * @return True on success, false if the parameters are invalid.
 */
bool ExtendMap(uint new_w, uint new_h, uint dx, uint dy)
{
	const uint ow = Map::SizeX();
	const uint oh = Map::SizeY();

	if (!ValidNewSize(new_w, new_h)) return false;
	if (new_w < ow || new_h < oh) return false;
	if (new_w == ow && new_h == oh) return false; /* no growth -> nothing to relocate (offset is always 0) */
	if (dx + ow > new_w || dy + oh > new_h) return false;

	_reloc_dx = dx;
	_reloc_dy = dy;
	_reloc_old_w = ow;

	/* While the OLD map is still consistent, free every train's track reservation
	 * (FreeTrainTrackReservation follows the reserved path through the current map)
	 * and clear vehicle path caches. Otherwise a train ticking after the move clears
	 * a stale reservation and asserts (UnreserveAcrossRailTunnelBridge). Trains and
	 * road/ship vehicles simply re-path / re-reserve on the next tick. */
	for (Train *t : Train::Iterate()) {
		if (t->IsFrontEngine()) FreeTrainTrackReservation(t);
		t->lookahead.reset();
	}
	for (RoadVehicle *rv : RoadVehicle::Iterate()) rv->cached_path.reset();
	for (Ship *sh : Ship::Iterate()) sh->cached_path.clear();

	/* Snapshot the old tile arrays (AllocateMap frees them). */
	const uint old_size = Map::Size();
	std::vector<Tile> old_m(_m.tile_data, _m.tile_data + old_size);
	std::vector<TileExtended> old_me(_me.tile_data, _me.tile_data + old_size);

	AllocateMap(new_w, new_h);

	/* Fill: sea everywhere, void on the new south (MaxY) and east (MaxX) edges. */
	for (uint y = 0; y < new_h; y++) {
		for (uint x = 0; x < new_w; x++) {
			TileIndex t = TileXY(x, y);
			if (x == Map::MaxX() || y == Map::MaxY()) {
				MakeVoid(t);
			} else {
				MakeSea(t);
			}
		}
	}

	/* Overlay the old tiles at (dx,dy). Skip old void tiles so the old border
	 * does not turn into interior void. */
	for (uint y = 0; y < oh; y++) {
		for (uint x = 0; x < ow; x++) {
			const size_t oi = (size_t)y * ow + x;
			if (GB(old_m[oi].type, 4, 4) == static_cast<uint8_t>(TileType::Void)) continue;
			TileIndex nt = TileXY(x + dx, y + dy);
			_m[nt] = old_m[oi];
			_me[nt] = old_me[oi];
		}
	}

	/* Offset every absolute tile index / world coordinate in the object pools. */
	const int wdx = (int)dx * (int)TILE_SIZE;
	const int wdy = (int)dy * (int)TILE_SIZE;

	for (Vehicle *v : Vehicle::Iterate()) {
		v->tile = RelocTile(v->tile);
		v->dest_tile = RelocTile(v->dest_tile);
		v->x_pos += wdx;
		v->y_pos += wdy;
	}

	for (Station *st : Station::Iterate()) {
		st->xy = RelocTile(st->xy);
		st->train_station.tile = RelocTile(st->train_station.tile);
		st->bus_station.tile = RelocTile(st->bus_station.tile);
		st->truck_station.tile = RelocTile(st->truck_station.tile);
		st->airport.tile = RelocTile(st->airport.tile);
		st->ship_station.tile = RelocTile(st->ship_station.tile);
		st->docking_station.tile = RelocTile(st->docking_station.tile);
		for (TileIndex &dt : st->docking_tiles) dt = RelocTile(dt);
	}
	for (Waypoint *wp : Waypoint::Iterate()) {
		wp->xy = RelocTile(wp->xy);
		wp->train_station.tile = RelocTile(wp->train_station.tile);
	}
	for (RoadStop *rs : RoadStop::Iterate()) {
		rs->xy = RelocTile(rs->xy);
	}
	for (Town *t : Town::Iterate()) {
		t->xy = RelocTile(t->xy);
	}
	for (Industry *i : Industry::Iterate()) {
		i->location.tile = RelocTile(i->location.tile);
	}
	for (Depot *d : Depot::Iterate()) {
		d->xy = RelocTile(d->xy);
	}
	for (Object *o : Object::Iterate()) {
		o->location.tile = RelocTile(o->location.tile);
	}
	for (Sign *s : Sign::Iterate()) {
		s->x += wdx;
		s->y += wdy;
	}
	/* CargoPacket::source_xy is private (origin of in-transit cargo, used only for
	 * payment distance); offsetting it is deferred -- minor economic inaccuracy only. */
	for (Company *c : Company::Iterate()) {
		c->location_of_HQ = RelocTile(c->location_of_HQ);
	}
	/* JGRPP stores tunnel endpoints in a pool (the TUNN chunk); the map tiles moved
	 * but these stored tiles didn't, so GetOtherTunnelEnd returns stale tiles. The
	 * tunnel spatial indexes are rebuilt from these on load. */
	for (Tunnel *tun : Tunnel::Iterate()) {
		tun->tile_n = RelocTile(tun->tile_n);
		tun->tile_s = RelocTile(tun->tile_s);
	}

	/* Animated tiles are keyed by TileIndex; rebuild the map with shifted keys. */
	{
		btree::btree_map<TileIndex, AnimatedTileInfo> shifted;
		for (auto &kv : _animated_tiles) shifted[RelocTile(kv.first)] = kv.second;
		_animated_tiles = std::move(shifted);
	}

	/* Rebuild derived caches that depend on tile positions. */
	YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
	ResetVehicleHash();
	/* ResetVehicleHash cleared the viewport-hash array but (unlike the tile hash, which it
	 * marks with hash_tile_current = INVALID_TILE) left each vehicle's coord and link
	 * pointers stale. Invalidate coord first so UpdateViewport does a clean re-insert
	 * instead of unlinking from the cleared hash via stale pointers (causing flicker). */
	for (Vehicle *v : Vehicle::Iterate()) {
		v->coord.left = INVALID_COORD;
		v->UpdateViewport(false);
	}
	RebuildStationKdtree();
	RebuildTownKdtree();
	RebuildViewportKdtree();
	UpdateAllVirtCoords();
	MarkWholeScreenDirty();

	return true;
}
