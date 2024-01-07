/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tile_map.h Map writing/reading functions for tiles. */

#ifndef TILE_MAP_H
#define TILE_MAP_H

#include "slope_type.h"
#include "map_func.h"
#include "core/bitmath_func.hpp"
#include "settings_type.h"

/**
 * Returns the height of a tile
 *
 * This function returns the height of the northern corner of a tile.
 * This is saved in the global map-array. It does not take affect by
 * any slope-data of the tile.
 *
 * @param tile The tile to get the height from
 * @return the height of the tile
 * @pre tile < MapSize()
 */
debug_inline static uint TileHeight(TileIndex tile)
{
	/* this method is inlined in many places and is performance-critical, drop assertion in non-debug builds */
#ifdef _DEBUG
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X", tile, MapSize());
#endif
	return _m[tile].height;
}

/**
 * Returns the height of a tile, also for tiles outside the map (virtual "black" tiles).
 *
 * @param x X coordinate of the tile, may be outside the map.
 * @param y Y coordinate of the tile, may be outside the map.
 * @return The height in the same unit as TileHeight.
 */
inline uint TileHeightOutsideMap(int x, int y)
{
	return TileHeight(TileXY(Clamp(x, 0, MapMaxX()), Clamp(y, 0, MapMaxY())));
}

/**
 * Sets the height of a tile.
 *
 * This function sets the height of the northern corner of a tile.
 *
 * @param tile The tile to change the height
 * @param height The new height value of the tile
 * @pre tile < MapSize()
 * @pre height <= MAX_TILE_HEIGHT
 */
inline void SetTileHeight(TileIndex tile, uint height)
{
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X", tile, MapSize());
	dbg_assert(height <= MAX_TILE_HEIGHT);
	_m[tile].height = height;
}

/**
 * Returns the height of a tile in pixels.
 *
 * This function returns the height of the northern corner of a tile in pixels.
 *
 * @param tile The tile to get the height
 * @return The height of the tile in pixel
 */
inline uint TilePixelHeight(TileIndex tile)
{
	return TileHeight(tile) * TILE_HEIGHT;
}

/**
 * Returns the height of a tile in pixels, also for tiles outside the map (virtual "black" tiles).
 *
 * @param x X coordinate of the tile, may be outside the map.
 * @param y Y coordinate of the tile, may be outside the map.
 * @return The height in pixels in the same unit as TilePixelHeight.
 */
inline uint TilePixelHeightOutsideMap(int x, int y)
{
	return TileHeightOutsideMap(x, y) * TILE_HEIGHT;
}

/**
 * Get the tiletype of a given tile.
 *
 * @param tile The tile to get the TileType
 * @return The tiletype of the tile
 * @pre tile < MapSize()
 */
debug_inline static TileType GetTileType(TileIndex tile)
{
	/* this method is inlined in many places and is performance-critical, drop assertion in non-debug builds */
#ifdef _DEBUG
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X", tile, MapSize());
#endif
	return (TileType)GB(_m[tile].type, 4, 4);
}

/**
 * Check if a tile is within the map (not a border)
 *
 * @param tile The tile to check
 * @return Whether the tile is in the interior of the map
 * @pre tile < MapSize()
 */
inline bool IsInnerTile(TileIndex tile)
{
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X", tile, MapSize());

	uint x = TileX(tile);
	uint y = TileY(tile);

	return x < MapMaxX() && y < MapMaxY() && ((x > 0 && y > 0) || !_settings_game.construction.freeform_edges);
}

/**
 * Set the type of a tile
 *
 * This functions sets the type of a tile. If the type
 * MP_VOID is selected the tile must be at the south-west or
 * south-east edges of the map and vice versa.
 *
 * @param tile The tile to save the new type
 * @param type The type to save
 * @pre tile < MapSize()
 * @pre type MP_VOID <=> tile is on the south-east or south-west edge.
 */
inline void SetTileType(TileIndex tile, TileType type)
{
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X, type: %d", tile, MapSize(), type);
	/* VOID tiles (and no others) are exactly allowed at the lower left and right
	 * edges of the map. If _settings_game.construction.freeform_edges is true,
	 * the upper edges of the map are also VOID tiles. */
	dbg_assert_msg(IsInnerTile(tile) == (type != MP_VOID), "tile: 0x%X (%d), type: %d", tile, IsInnerTile(tile), type);
	SB(_m[tile].type, 4, 4, type);
}

/**
 * Checks if a tile is a given tiletype.
 *
 * This function checks if a tile has the given tiletype.
 *
 * @param tile The tile to check
 * @param type The type to check against
 * @return true If the type matches against the type of the tile
 */
debug_inline static bool IsTileType(TileIndex tile, TileType type)
{
	return GetTileType(tile) == type;
}

/**
 * Checks if a tile is valid
 *
 * @param tile The tile to check
 * @return True if the tile is on the map and not one of MP_VOID.
 */
inline bool IsValidTile(TileIndex tile)
{
	return tile < MapSize() && !IsTileType(tile, MP_VOID);
}

/**
 * Returns the owner of a tile
 *
 * This function returns the owner of a tile. This cannot used
 * for tiles which type is one of MP_HOUSE, MP_VOID and MP_INDUSTRY
 * as no company owned any of these buildings.
 *
 * @param tile The tile to check
 * @return The owner of the tile
 * @pre IsValidTile(tile)
 * @pre The type of the tile must not be MP_HOUSE and MP_INDUSTRY
 */
inline Owner GetTileOwner(TileIndex tile)
{
	dbg_assert_msg(IsValidTile(tile), "tile: 0x%X, size: 0x%X", tile, MapSize());
	dbg_assert_msg(!IsTileType(tile, MP_HOUSE) && !IsTileType(tile, MP_INDUSTRY), "tile: 0x%X (%d)", tile, GetTileType(tile));

	return (Owner)GB(_m[tile].m1, 0, 5);
}

/**
 * Sets the owner of a tile
 *
 * This function sets the owner status of a tile. Note that you cannot
 * set a owner for tiles of type MP_HOUSE, MP_VOID and MP_INDUSTRY.
 *
 * @param tile The tile to change the owner status.
 * @param owner The new owner.
 * @pre IsValidTile(tile)
 * @pre The type of the tile must not be MP_HOUSE and MP_INDUSTRY
 */
inline void SetTileOwner(TileIndex tile, Owner owner)
{
	dbg_assert_msg(IsValidTile(tile), "tile: 0x%X, size: 0x%X, owner: %d", tile, MapSize(), owner);
	dbg_assert_msg(!IsTileType(tile, MP_HOUSE) && !IsTileType(tile, MP_INDUSTRY), "tile: 0x%X (%d), owner: %d", tile, GetTileType(tile), owner);

	SB(_m[tile].m1, 0, 5, owner);
}

/**
 * Checks if a tile belongs to the given owner
 *
 * @param tile The tile to check
 * @param owner The owner to check against
 * @return True if a tile belongs the the given owner
 */
inline bool IsTileOwner(TileIndex tile, Owner owner)
{
	return GetTileOwner(tile) == owner;
}

/**
 * Set the tropic zone
 * @param tile the tile to set the zone of
 * @param type the new type
 * @pre tile < MapSize()
 */
inline void SetTropicZone(TileIndex tile, TropicZone type)
{
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X, type: %d", tile, MapSize(), type);
	dbg_assert_msg(!IsTileType(tile, MP_VOID) || type == TROPICZONE_NORMAL, "tile: 0x%X (%d), type: %d", tile, GetTileType(tile), type);
	SB(_m[tile].type, 0, 2, type);
}

/**
 * Get the tropic zone
 * @param tile the tile to get the zone of
 * @pre tile < MapSize()
 * @return the zone type
 */
inline TropicZone GetTropicZone(TileIndex tile)
{
	dbg_assert_msg(tile < MapSize(), "tile: 0x%X, size: 0x%X", tile, MapSize());
	return (TropicZone)GB(_m[tile].type, 0, 2);
}

/**
 * Get the current animation frame
 * @param t the tile
 * @pre IsTileType(t, MP_HOUSE) || IsTileType(t, MP_OBJECT) || IsTileType(t, MP_INDUSTRY) || IsTileType(t, MP_STATION)
 * @return frame number
 */
inline byte GetAnimationFrame(TileIndex t)
{
	dbg_assert_msg(IsTileType(t, MP_HOUSE) || IsTileType(t, MP_OBJECT) || IsTileType(t, MP_INDUSTRY) || IsTileType(t, MP_STATION), "tile: 0x%X (%d)", t, GetTileType(t));
	return _me[t].m7;
}

/**
 * Set a new animation frame
 * @param t the tile
 * @param frame the new frame number
 * @pre IsTileType(t, MP_HOUSE) || IsTileType(t, MP_OBJECT) || IsTileType(t, MP_INDUSTRY) || IsTileType(t, MP_STATION)
 */
inline void SetAnimationFrame(TileIndex t, byte frame)
{
	dbg_assert_msg(IsTileType(t, MP_HOUSE) || IsTileType(t, MP_OBJECT) || IsTileType(t, MP_INDUSTRY) || IsTileType(t, MP_STATION), "tile: 0x%X (%d)", t, GetTileType(t));
	_me[t].m7 = frame;
}

Slope GetTileSlope(TileIndex tile, int *h = nullptr);
int GetTileZ(TileIndex tile);
int GetTileMaxZ(TileIndex tile);

bool IsTileFlat(TileIndex tile, int *h = nullptr);

/**
 * Return the slope of a given tile
 * @param tile Tile to compute slope of
 * @param h    If not \c nullptr, pointer to storage of z height
 * @return Slope of the tile, except for the HALFTILE part
 */
inline Slope GetTilePixelSlope(TileIndex tile, int *h)
{
	Slope s = GetTileSlope(tile, h);
	if (h != nullptr) *h *= TILE_HEIGHT;
	return s;
}

Slope GetTilePixelSlopeOutsideMap(int x, int y, int *h);

/**
 * Get bottom height of the tile
 * @param tile Tile to compute height of
 * @return Minimum height of the tile
 */
inline int GetTilePixelZ(TileIndex tile)
{
	return GetTileZ(tile) * TILE_HEIGHT;
}

/**
 * Get top height of the tile
 * @param tile Tile to compute height of
 * @return Maximum height of the tile
 */
inline int GetTileMaxPixelZ(TileIndex tile)
{
	return GetTileMaxZ(tile) * TILE_HEIGHT;
}

/**
 * Calculate a hash value from a tile position
 *
 * @param x The X coordinate
 * @param y The Y coordinate
 * @return The hash of the tile
 */
inline uint TileHash(uint x, uint y)
{
	uint hash = x >> 4;
	hash ^= x >> 6;
	hash ^= y >> 4;
	hash -= y >> 6;
	return hash;
}

/**
 * Get the last two bits of the TileHash
 *  from a tile position.
 *
 * @see TileHash()
 * @param x The X coordinate
 * @param y The Y coordinate
 * @return The last two bits from hash of the tile
 */
inline uint TileHash2Bit(uint x, uint y)
{
	return GB(TileHash(x, y), 0, 2);
}

#endif /* TILE_MAP_H */
