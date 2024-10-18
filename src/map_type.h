/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file map_type.h Types related to maps. */

#ifndef MAP_TYPE_H
#define MAP_TYPE_H

/**
 * Data that is stored per tile. Also used TileExtended for this.
 * Look at docs/landscape.html for the exact meaning of the members.
 */
struct Tile {
	uint8_t   type;     ///< The type (bits 4..7), bridges (2..3), rainforest/desert (0..1)
	uint8_t   height;   ///< The height of the northern corner.
	uint16_t  m2;       ///< Primarily used for indices to towns, industries and stations
	uint8_t   m1;       ///< Primarily used for ownership information
	uint8_t   m3;       ///< General purpose
	uint8_t   m4;       ///< General purpose
	uint8_t   m5;       ///< General purpose
};

static_assert(sizeof(Tile) == 8);

/**
 * Data that is stored per tile. Also used Tile for this.
 * Look at docs/landscape.html for the exact meaning of the members.
 */
struct TileExtended {
	uint8_t  m6; ///< General purpose
	uint8_t  m7; ///< Primarily used for newgrf support
	uint16_t m8; ///< General purpose
};

/**
 * An offset value between two tiles.
 *
 * This value is used for the difference between
 * two tiles. It can be added to a TileIndex to get
 * the resulting TileIndex of the start tile applied
 * with this saved difference.
 *
 * @see TileDiffXY(int, int)
 */
typedef int32_t TileIndexDiff;

/**
 * A pair-construct of a TileIndexDiff.
 *
 * This can be used to save the difference between to
 * tiles as a pair of x and y value.
 */
struct TileIndexDiffC {
	int16_t x;      ///< The x value of the coordinate
	int16_t y;      ///< The y value of the coordinate
};

/** Minimal and maximal map width and height */
static const uint MIN_MAP_SIZE_BITS  = 6;                        ///< Minimal size of map is equal to 2 ^ MIN_MAP_SIZE_BITS
static const uint MAX_MAP_SIZE_BITS  = 20;                       ///< Maximal size of map is equal to 2 ^ MAX_MAP_SIZE_BITS
static const uint MAX_MAP_TILES_BITS = 28;                       ///< Maximal number of tiles in a map is equal to 2 ^ MAX_MAP_TILES_BITS.
static const uint MIN_MAP_SIZE       = 1U << MIN_MAP_SIZE_BITS;  ///< Minimal map size = 64
static const uint MAX_MAP_SIZE       = 1U << MAX_MAP_SIZE_BITS;  ///< Maximal map size = 1M
static const uint MAX_MAP_TILES      = 1U << MAX_MAP_TILES_BITS; ///< Maximal number of tiles in a map = 256M (16k x 16k)

/** Argument for CmdLevelLand describing what to do. */
enum LevelMode : uint8_t {
	LM_LEVEL, ///< Level the land.
	LM_LOWER, ///< Lower the land.
	LM_RAISE, ///< Raise the land.
};

#endif /* MAP_TYPE_H */
