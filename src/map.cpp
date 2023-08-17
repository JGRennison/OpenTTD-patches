/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file map.cpp Base functions related to the map and distances on them. */

#include "stdafx.h"
#include "debug.h"
#include "core/alloc_func.hpp"
#include "water_map.h"
#include "string_func.h"
#include "rail_map.h"
#include "tunnelbridge_map.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include <array>
#include <deque>
#include <memory>

#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "safeguards.h"

#if defined(_MSC_VER)
/* Why the hell is that not in all MSVC headers?? */
extern "C" _CRTIMP void __cdecl _assert(void *, void *, unsigned);
#endif

uint _map_log_x;     ///< 2^_map_log_x == _map_size_x
uint _map_log_y;     ///< 2^_map_log_y == _map_size_y
uint _map_size_x;    ///< Size of the map along the X
uint _map_size_y;    ///< Size of the map along the Y
uint _map_size;      ///< The number of tiles on the map
uint _map_tile_mask; ///< _map_size - 1 (to mask the mapsize)

Tile *_m = nullptr;          ///< Tiles of the map
TileExtended *_me = nullptr; ///< Extended Tiles of the map

#if defined(__linux__) && defined(MADV_HUGEPAGE)
static size_t _munmap_size = 0;
#endif

/**
 * Validates whether a map with the given dimension is valid
 * @param size_x the width of the map along the NE/SW edge
 * @param size_y the 'height' of the map along the SE/NW edge
 * @return true if valid, or false if not valid
 */
bool ValidateMapSize(uint size_x, uint size_y)
{
	/* Make sure that the map size is within the limits and that
	 * size of both axes is a power of 2. */
	if (size_x * size_y > MAX_MAP_TILES ||
			size_x < MIN_MAP_SIZE ||
			size_y < MIN_MAP_SIZE ||
			(size_x & (size_x - 1)) != 0 ||
			(size_y & (size_y - 1)) != 0) {
		return false;
	}
	return true;
}

/**
 * (Re)allocates a map with the given dimension
 * @param size_x the width of the map along the NE/SW edge
 * @param size_y the 'height' of the map along the SE/NW edge
 */
void AllocateMap(uint size_x, uint size_y)
{
	DEBUG(map, 2, "Min/max map size %d/%d, max map tiles %d", MIN_MAP_SIZE, MAX_MAP_SIZE, MAX_MAP_TILES);
	DEBUG(map, 1, "Allocating map of size %dx%d", size_x, size_y);

	if (!ValidateMapSize(size_x, size_y)) {
		error("Invalid map size");
	}

	_map_log_x = FindFirstBit(size_x);
	_map_log_y = FindFirstBit(size_y);
	_map_size_x = size_x;
	_map_size_y = size_y;
	_map_size = size_x * size_y;
	_map_tile_mask = _map_size - 1;

#if defined(__linux__) && defined(MADV_HUGEPAGE)
	if (_munmap_size != 0) {
		munmap(_m, _munmap_size);
		_munmap_size = 0;
		_m = nullptr;
	}
#endif

	free(_m);

	const size_t total_size = (sizeof(Tile) + sizeof(TileExtended)) * _map_size;

	byte *buf = nullptr;
#if defined(__linux__) && defined(MADV_HUGEPAGE)
	const size_t alignment = 2 * 1024 * 1024;
	/* First try mmap with a 2MB alignment, if that fails, just use calloc */
	if (total_size >= alignment) {
		size_t allocated = total_size + alignment;
		void * const ret = mmap(nullptr, allocated, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (ret != MAP_FAILED) {
			void *target = ret;
			assert(std::align(alignment, total_size, target, allocated));

			/* target is now aligned, allocated has been adjusted accordingly */

			const size_t remove_front = static_cast<byte *>(target) - static_cast<byte *>(ret);
			if (remove_front != 0) {
				munmap(ret, remove_front);
			}

			const size_t remove_back = allocated - total_size;
			if (remove_back != 0) {
				munmap(static_cast<char *>(target) + total_size, remove_back);
			}

			madvise(target, total_size, MADV_HUGEPAGE);
			DEBUG(map, 2, "Using mmap for map allocation");

			buf = static_cast<byte *>(target);
			_munmap_size = total_size;
		}
	}
#endif

	if (buf == nullptr) buf = CallocT<byte>(total_size);

	_m = reinterpret_cast<Tile *>(buf);
	_me = reinterpret_cast<TileExtended *>(buf + (_map_size * sizeof(Tile)));
}


#ifdef _DEBUG
TileIndex TileAdd(TileIndex tile, TileIndexDiff add,
	const char *exp, const char *file, int line)
{
	int dx;
	int dy;
	uint x;
	uint y;

	dx = add & MapMaxX();
	if (dx >= (int)MapSizeX() / 2) dx -= MapSizeX();
	dy = (add - dx) / (int)MapSizeX();

	x = TileX(tile) + dx;
	y = TileY(tile) + dy;

	if (x >= MapSizeX() || y >= MapSizeY()) {
		char buf[512];

		seprintf(buf, lastof(buf), "TILE_ADD(%s) when adding 0x%.4X and 0x%.4X failed",
			exp, tile, add);
#if !defined(_MSC_VER)
		fprintf(stderr, "%s:%d %s\n", file, line, buf);
#else
		_assert(buf, (char*)file, line);
#endif
	}

	dbg_assert(TileXY(x, y) == TILE_MASK(tile + add));

	return TileXY(x, y);
}
#endif

/**
 * This function checks if we add addx/addy to tile, if we
 * do wrap around the edges. For example, tile = (10,2) and
 * addx = +3 and addy = -4. This function will now return
 * INVALID_TILE, because the y is wrapped. This is needed in
 * for example, farmland. When the tile is not wrapped,
 * the result will be tile + TileDiffXY(addx, addy)
 *
 * @param tile the 'starting' point of the adding
 * @param addx the amount of tiles in the X direction to add
 * @param addy the amount of tiles in the Y direction to add
 * @return translated tile, or INVALID_TILE when it would've wrapped.
 */
TileIndex TileAddWrap(TileIndex tile, int addx, int addy)
{
	uint x = TileX(tile) + addx;
	uint y = TileY(tile) + addy;

	/* Disallow void tiles at the north border. */
	if ((x == 0 || y == 0) && _settings_game.construction.freeform_edges) return INVALID_TILE;

	/* Are we about to wrap? */
	if (x >= MapMaxX() || y >= MapMaxY()) return INVALID_TILE;

	return TileXY(x, y);
}

/**
 * This function checks if we add addx/addy to tile, if we
 * do wrap around the edges. Instead of wrapping, saturate at the map edge.
 *
 * @param tile the 'starting' point of the adding
 * @param addx the amount of tiles in the X direction to add
 * @param addy the amount of tiles in the Y direction to add
 * @return translated tile
 */
TileIndex TileAddSaturating(TileIndex tile, int addx, int addy)
{
	int x = TileX(tile) + addx;
	int y = TileY(tile) + addy;

	auto clamp = [&](int coord, int map_max) -> uint {
		return Clamp<int>(coord, _settings_game.construction.freeform_edges ? 1 : 0, map_max - 1);
	};
	return TileXY(clamp(x,  MapMaxX()), clamp(y,  MapMaxY()));
}

/** 'Lookup table' for tile offsets given a DiagDirection */
extern const TileIndexDiffC _tileoffs_by_diagdir[] = {
	{-1,  0}, ///< DIAGDIR_NE
	{ 0,  1}, ///< DIAGDIR_SE
	{ 1,  0}, ///< DIAGDIR_SW
	{ 0, -1}  ///< DIAGDIR_NW
};

/** 'Lookup table' for tile offsets given a Direction */
extern const TileIndexDiffC _tileoffs_by_dir[] = {
	{-1, -1}, ///< DIR_N
	{-1,  0}, ///< DIR_NE
	{-1,  1}, ///< DIR_E
	{ 0,  1}, ///< DIR_SE
	{ 1,  1}, ///< DIR_S
	{ 1,  0}, ///< DIR_SW
	{ 1, -1}, ///< DIR_W
	{ 0, -1}  ///< DIR_NW
};

/**
 * Gets the Manhattan distance between the two given tiles.
 * The Manhattan distance is the sum of the delta of both the
 * X and Y component.
 * Also known as L1-Norm
 * @param t0 the start tile
 * @param t1 the end tile
 * @return the distance
 */
uint DistanceManhattan(TileIndex t0, TileIndex t1)
{
	const uint dx = Delta(TileX(t0), TileX(t1));
	const uint dy = Delta(TileY(t0), TileY(t1));
	return dx + dy;
}


/**
 * Gets the 'Square' distance between the two given tiles.
 * The 'Square' distance is the square of the shortest (straight line)
 * distance between the two tiles.
 * Also known as euclidian- or L2-Norm squared.
 * @param t0 the start tile
 * @param t1 the end tile
 * @return the distance
 */
uint DistanceSquare(TileIndex t0, TileIndex t1)
{
	const int dx = TileX(t0) - TileX(t1);
	const int dy = TileY(t0) - TileY(t1);
	return dx * dx + dy * dy;
}


/**
 * Gets the biggest distance component (x or y) between the two given tiles.
 * Also known as L-Infinity-Norm.
 * @param t0 the start tile
 * @param t1 the end tile
 * @return the distance
 */
uint DistanceMax(TileIndex t0, TileIndex t1)
{
	const uint dx = Delta(TileX(t0), TileX(t1));
	const uint dy = Delta(TileY(t0), TileY(t1));
	return std::max(dx, dy);
}


/**
 * Gets the biggest distance component (x or y) between the two given tiles
 * plus the Manhattan distance, i.e. two times the biggest distance component
 * and once the smallest component.
 * @param t0 the start tile
 * @param t1 the end tile
 * @return the distance
 */
uint DistanceMaxPlusManhattan(TileIndex t0, TileIndex t1)
{
	const uint dx = Delta(TileX(t0), TileX(t1));
	const uint dy = Delta(TileY(t0), TileY(t1));
	return dx > dy ? 2 * dx + dy : 2 * dy + dx;
}

/**
 * Param the minimum distance to an edge
 * @param tile the tile to get the distance from
 * @return the distance from the edge in tiles
 */
uint DistanceFromEdge(TileIndex tile)
{
	const uint xl = TileX(tile);
	const uint yl = TileY(tile);
	const uint xh = MapSizeX() - 1 - xl;
	const uint yh = MapSizeY() - 1 - yl;
	const uint minl = std::min(xl, yl);
	const uint minh = std::min(xh, yh);
	return std::min(minl, minh);
}

/**
 * Gets the distance to the edge of the map in given direction.
 * @param tile the tile to get the distance from
 * @param dir the direction of interest
 * @return the distance from the edge in tiles
 */
uint DistanceFromEdgeDir(TileIndex tile, DiagDirection dir)
{
	switch (dir) {
		case DIAGDIR_NE: return             TileX(tile) - (_settings_game.construction.freeform_edges ? 1 : 0);
		case DIAGDIR_NW: return             TileY(tile) - (_settings_game.construction.freeform_edges ? 1 : 0);
		case DIAGDIR_SW: return MapMaxX() - TileX(tile) - 1;
		case DIAGDIR_SE: return MapMaxY() - TileY(tile) - 1;
		default: NOT_REACHED();
	}
}

/**
 * Function performing a search around a center tile and going outward, thus in circle.
 * Although it really is a square search...
 * Every tile will be tested by means of the callback function proc,
 * which will determine if yes or no the given tile meets criteria of search.
 * @param tile to start the search from. Upon completion, it will return the tile matching the search
 * @param size: number of tiles per side of the desired search area
 * @param proc: callback testing function pointer.
 * @param user_data to be passed to the callback function. Depends on the implementation
 * @return result of the search
 * @pre proc != nullptr
 * @pre size > 0
 */
bool CircularTileSearch(TileIndex *tile, uint size, TestTileOnSearchProc proc, void *user_data)
{
	dbg_assert(proc != nullptr);
	dbg_assert(size > 0);

	if (size % 2 == 1) {
		/* If the length of the side is uneven, the center has to be checked
		 * separately, as the pattern of uneven sides requires to go around the center */
		if (proc(*tile, user_data)) return true;

		/* If tile test is not successful, get one tile up,
		 * ready for a test in first circle around center tile */
		*tile = TileAddByDir(*tile, DIR_N);
		return CircularTileSearch(tile, size / 2, 1, 1, proc, user_data);
	} else {
		return CircularTileSearch(tile, size / 2, 0, 0, proc, user_data);
	}
}

/**
 * Generalized circular search allowing for rectangles and a hole.
 * Function performing a search around a center rectangle and going outward.
 * The center rectangle is left out from the search. To do a rectangular search
 * without a hole, set either h or w to zero.
 * Every tile will be tested by means of the callback function proc,
 * which will determine if yes or no the given tile meets criteria of search.
 * @param tile to start the search from. Upon completion, it will return the tile matching the search.
 *  This tile should be directly north of the hole (if any).
 * @param radius How many tiles to search outwards. Note: This is a radius and thus different
 *                from the size parameter of the other CircularTileSearch function, which is a diameter.
 * @param w the width of the inner rectangle
 * @param h the height of the inner rectangle
 * @param proc callback testing function pointer.
 * @param user_data to be passed to the callback function. Depends on the implementation
 * @return result of the search
 * @pre proc != nullptr
 * @pre radius > 0
 */
bool CircularTileSearch(TileIndex *tile, uint radius, uint w, uint h, TestTileOnSearchProc proc, void *user_data)
{
	dbg_assert(proc != nullptr);
	dbg_assert(radius > 0);

	uint x = TileX(*tile) + w + 1;
	uint y = TileY(*tile);

	const uint extent[DIAGDIR_END] = { w, h, w, h };

	for (uint n = 0; n < radius; n++) {
		for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
			/* Is the tile within the map? */
			for (uint j = extent[dir] + n * 2 + 1; j != 0; j--) {
				if (x < MapSizeX() && y < MapSizeY()) {
					TileIndex t = TileXY(x, y);
					/* Is the callback successful? */
					if (proc(t, user_data)) {
						/* Stop the search */
						*tile = t;
						return true;
					}
				}

				/* Step to the next 'neighbour' in the circular line */
				x += _tileoffs_by_diagdir[dir].x;
				y += _tileoffs_by_diagdir[dir].y;
			}
		}
		/* Jump to next circle to test */
		x += _tileoffs_by_dir[DIR_W].x;
		y += _tileoffs_by_dir[DIR_W].y;
	}

	*tile = INVALID_TILE;
	return false;
}

/**
 * Generalized contiguous matching tile area size threshold function.
 * Contiguous means directly adjacent by DiagDirection directions.
 *
 * @param tile to start the search from.
 * @param threshold minimum number of matching tiles for success, searching is halted when this is reached.
 * @param proc callback testing function pointer.
 * @param user_data to be passed to the callback function. Depends on the implementation
 * @return whether the contiguous tile area size is >= threshold
 * @pre proc != nullptr
 */
bool EnoughContiguousTilesMatchingCondition(TileIndex tile, uint threshold, TestTileOnSearchProc proc, void *user_data)
{
	dbg_assert(proc != nullptr);
	if (threshold == 0) return true;

	static_assert(MAX_MAP_TILES_BITS <= 30);

	btree::btree_set<uint32> processed_tiles;
	std::deque<uint32> candidates;
	uint matching_count = 0;

	auto process_tile = [&](TileIndex t, DiagDirection exclude_onward_dir) {
		auto iter = processed_tiles.lower_bound(t);
		if (iter != processed_tiles.end() && *iter == t) {
			/* done this tile already */
		} else {
			if (proc(t, user_data)) {
				matching_count++;
				for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
					if (dir == exclude_onward_dir) continue;
					TileIndex neighbour_tile = AddTileIndexDiffCWrap(t, TileIndexDiffCByDiagDir(dir));
					if (IsValidTile(neighbour_tile)) {
						candidates.push_back(neighbour_tile | (ReverseDiagDir(dir) << 30));
					}
				}
			}
			processed_tiles.insert(iter, t);
		}
	};
	process_tile(tile, INVALID_DIAGDIR);

	while (matching_count < threshold && !candidates.empty()) {
		uint32 next = candidates.front();
		candidates.pop_front();
		TileIndex t = GB(next, 0, 30);
		DiagDirection exclude_onward_dir = (DiagDirection)GB(next, 30, 2);
		process_tile(t, exclude_onward_dir);
	}
	return matching_count >= threshold;
}

/**
 * Finds the distance for the closest tile with water/land given a tile
 * @param tile  the tile to find the distance too
 * @param water whether to find water or land
 * @return distance to nearest water (max 0x7F) / land (max 0x1FF; 0x200 if there is no land)
 */
uint GetClosestWaterDistance(TileIndex tile, bool water)
{
	if (HasTileWaterGround(tile) == water) return 0;

	uint max_dist = water ? 0x7F : 0x200;

	int x = TileX(tile);
	int y = TileY(tile);

	uint max_x = MapMaxX();
	uint max_y = MapMaxY();
	uint min_xy = _settings_game.construction.freeform_edges ? 1 : 0;

	/* go in a 'spiral' with increasing manhattan distance in each iteration */
	for (uint dist = 1; dist < max_dist; dist++) {
		/* next 'diameter' */
		y--;

		/* going counter-clockwise around this square */
		for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
			static const int8 ddx[DIAGDIR_END] = { -1,  1,  1, -1};
			static const int8 ddy[DIAGDIR_END] = {  1,  1, -1, -1};

			int dx = ddx[dir];
			int dy = ddy[dir];

			/* each side of this square has length 'dist' */
			for (uint a = 0; a < dist; a++) {
				/* MP_VOID tiles are not checked (interval is [min; max) for IsInsideMM())*/
				if (IsInsideMM(x, min_xy, max_x) && IsInsideMM(y, min_xy, max_y)) {
					TileIndex t = TileXY(x, y);
					if (HasTileWaterGround(t) == water) return dist;
				}
				x += dx;
				y += dy;
			}
		}
	}

	if (!water) {
		/* no land found - is this a water-only map? */
		for (TileIndex t = 0; t < MapSize(); t++) {
			if (!IsTileType(t, MP_VOID) && !IsTileType(t, MP_WATER)) return 0x1FF;
		}
	}

	return max_dist;
}

static const char *tile_type_names[16] = {
	"MP_CLEAR",
	"MP_RAILWAY",
	"MP_ROAD",
	"MP_HOUSE",
	"MP_TREES",
	"MP_STATION",
	"MP_WATER",
	"MP_VOID",
	"MP_INDUSTRY",
	"MP_TUNNELBRIDGE",
	"MP_OBJECT",
	"INVALID_B",
	"INVALID_C",
	"INVALID_D",
	"INVALID_E",
	"INVALID_F",
};

char *DumpTileInfo(char *b, const char *last, TileIndex tile)
{
	if (tile == INVALID_TILE) {
		b += seprintf(b, last, "tile: %X (INVALID_TILE)", tile);
	} else {
		b += seprintf(b, last, "tile: %X (%u x %u)", tile, TileX(tile), TileY(tile));
	}
	if (!_m || !_me) {
		b += seprintf(b, last, ", NO MAP ALLOCATED");
	} else {
		if (tile >= MapSize()) {
			b += seprintf(b, last, ", TILE OUTSIDE MAP");
		} else {
			b += seprintf(b, last, ", type: %02X (%s), height: %02X, data: %02X %04X %02X %02X %02X %02X %02X %04X",
					_m[tile].type, tile_type_names[GB(_m[tile].type, 4, 4)], _m[tile].height,
					_m[tile].m1, _m[tile].m2, _m[tile].m3, _m[tile].m4, _m[tile].m5, _me[tile].m6, _me[tile].m7, _me[tile].m8);
		}
	}
	return b;
}

void DumpMapStats(char *b, const char *last)
{
	std::array<uint, 16> tile_types;
	uint restricted_signals = 0;
	uint prog_signals = 0;
	uint dual_rail_type = 0;
	uint road_works = 0;

	enum TunnelBridgeBits {
		TBB_BRIDGE            = 1 << 0,
		TBB_ROAD              = 1 << 1,
		TBB_TRAM              = 1 << 2,
		TBB_RAIL              = 1 << 3,
		TBB_WATER             = 1 << 4,
		TBB_CUSTOM_HEAD       = 1 << 5,
		TBB_DUAL_RT           = 1 << 6,
		TBB_SIGNALLED         = 1 << 7,
		TBB_SIGNALLED_BIDI    = 1 << 8,
	};
	btree::btree_map<uint, uint> tunnel_bridge_stats;

	for (uint type = 0; type < 16; type++) {
		tile_types[type] = 0;
	}

	for (TileIndex t = 0; t < MapSize(); t++) {
		tile_types[GetTileType(t)]++;

		if (IsTileType(t, MP_RAILWAY)) {
			if (GetRailTileType(t) == RAIL_TILE_SIGNALS) {
				if (IsRestrictedSignal(t)) restricted_signals++;
				if (HasSignalOnTrack(t, TRACK_LOWER) && GetSignalType(t, TRACK_LOWER) == SIGTYPE_PROG) prog_signals++;
				if (HasSignalOnTrack(t, TRACK_UPPER) && GetSignalType(t, TRACK_UPPER) == SIGTYPE_PROG) prog_signals++;
			}
		}

		bool dual_rt = false;
		RailType rt1 = GetTileRailType(t);
		if (rt1 != INVALID_RAILTYPE) {
			RailType rt2 = GetTileSecondaryRailTypeIfValid(t);
			if (rt2 != INVALID_RAILTYPE && rt1 != rt2) {
				dual_rail_type++;
				dual_rt = true;
			}
		}

		if (IsNormalRoadTile(t) && HasRoadWorks(t)) road_works++;

		if (IsTileType(t, MP_TUNNELBRIDGE)) {
			uint bucket = 0;
			if (IsBridge(t)) bucket |= TBB_BRIDGE;
			if (IsTunnelBridgeWithSignalSimulation(t)) {
				bucket |= TBB_SIGNALLED;
				if (IsTunnelBridgeSignalSimulationBidirectional(t)) bucket |= TBB_SIGNALLED_BIDI;
				if (IsTunnelBridgeRestrictedSignal(t)) restricted_signals++;
			}
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD) {
				if (HasTileRoadType(t, RTT_ROAD)) bucket |= TBB_ROAD;
				if (HasTileRoadType(t, RTT_TRAM)) bucket |= TBB_TRAM;
			}
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) bucket |= TBB_RAIL;
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_WATER) bucket |= TBB_WATER;
			if (IsCustomBridgeHeadTile(t)) bucket |= TBB_CUSTOM_HEAD;
			if (dual_rt) bucket |= TBB_DUAL_RT;
			tunnel_bridge_stats[bucket]++;
		}
	}

	for (uint type = 0; type < 16; type++) {
		if (tile_types[type]) b += seprintf(b, last, "%-20s %20u\n", tile_type_names[type], tile_types[type]);
	}

	b += seprintf(b, last, "\n");

	if (restricted_signals) b += seprintf(b, last, "restricted signals   %20u\n", restricted_signals);
	if (prog_signals)       b += seprintf(b, last, "prog signals         %20u\n", prog_signals);
	if (dual_rail_type)     b += seprintf(b, last, "dual rail type       %20u\n", dual_rail_type);
	if (road_works)         b += seprintf(b, last, "road works           %20u\n", road_works);

	for (auto it : tunnel_bridge_stats) {
		b = strecpy(b, it.first & TBB_BRIDGE ? "bridge" : "tunnel", last, true);
		if (it.first & TBB_ROAD) b = strecpy(b, ", road", last, true);
		if (it.first & TBB_TRAM) b = strecpy(b, ", tram", last, true);
		if (it.first & TBB_RAIL) b = strecpy(b, ", rail", last, true);
		if (it.first & TBB_WATER) b = strecpy(b, ", water", last, true);
		if (it.first & TBB_CUSTOM_HEAD) b = strecpy(b, ", custom head", last, true);
		if (it.first & TBB_DUAL_RT) b = strecpy(b, ", dual rail type", last, true);
		if (it.first & TBB_SIGNALLED) b = strecpy(b, ", signalled", last, true);
		if (it.first & TBB_SIGNALLED_BIDI) b = strecpy(b, ", bidi", last, true);
		b += seprintf(b, last, ": %u\n", it.second);
	}
}
