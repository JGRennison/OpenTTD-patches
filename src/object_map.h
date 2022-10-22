/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file object_map.h Map accessors for object tiles. */

#ifndef OBJECT_MAP_H
#define OBJECT_MAP_H

#include "water_map.h"
#include "object_type.h"

enum ObjectGround {
	OBJECT_GROUND_GRASS       = 0, ///< Grass or bare
	OBJECT_GROUND_SNOW_DESERT = 1, ///< Snow or desert
	OBJECT_GROUND_SHORE       = 2, ///< Shore
};

ObjectType GetObjectType(TileIndex t);

/**
 * Check whether the object on a tile is of a specific type.
 * @param t Tile to test.
 * @param type Type to test.
 * @pre IsTileType(t, MP_OBJECT)
 * @return True if type matches.
 */
static inline bool IsObjectType(TileIndex t, ObjectType type)
{
	return GetObjectType(t) == type;
}

/**
 * Check whether a tile is a object tile of a specific type.
 * @param t Tile to test.
 * @param type Type to test.
 * @return True if type matches.
 */
static inline bool IsObjectTypeTile(TileIndex t, ObjectType type)
{
	return IsTileType(t, MP_OBJECT) && GetObjectType(t) == type;
}

/**
 * Get the index of which object this tile is attached to.
 * @param t the tile
 * @pre IsTileType(t, MP_OBJECT)
 * @return The ObjectID of the object.
 */
static inline ObjectID GetObjectIndex(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return _m[t].m2 | _m[t].m5 << 16;
}

/**
 * Get the random bits of this tile.
 * @param t The tile to get the bits for.
 * @pre IsTileType(t, MP_OBJECT)
 * @return The random bits.
 */
static inline byte GetObjectRandomBits(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return _m[t].m3;
}

/**
 * Get the ground type of ths tile.
 * @param t The tile to get the ground type of.
 * @pre IsTileType(t, MP_OBJECT)
 * @return The ground type.
 */
static inline ObjectGround GetObjectGroundType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return (ObjectGround)GB(_m[t].m4, 2, 2);
}

/**
 * Get the ground density of this tile.
 * Only meaningful for some ground types.
 * @param t The tile to get the density of.
 * @pre IsTileType(t, MP_OBJECT)
 * @return the density
 */
static inline uint GetObjectGroundDensity(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return GB(_m[t].m4, 0, 2);
}

/**
 * Set the ground density of this tile.
 * Only meaningful for some ground types.
 * @param t The tile to set the density of.
 * @param d the new density
 * @pre IsTileType(t, MP_OBJECT)
 */
static inline void SetObjectGroundDensity(TileIndex t, uint d)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	SB(_m[t].m4, 0, 2, d);
}

/**
 * Get the counter used to advance to the next ground density type.
 * @param t The tile to get the counter of.
 * @pre IsTileType(t, MP_OBJECT)
 * @return The value of the counter
 */
static inline uint GetObjectGroundCounter(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return GB(_m[t].m4, 5, 3);
}

/**
 * Increments the counter used to advance to the next ground density type.
 * @param t the tile to increment the counter of
 * @param c the amount to increment the counter with
 * @pre IsTileType(t, MP_OBJECT)
 */
static inline void AddObjectGroundCounter(TileIndex t, int c)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	_m[t].m4 += c << 5;
}

/**
 * Sets the counter used to advance to the next ground density type.
 * @param t The tile to set the counter of.
 * @param c The amount to set the counter to.
 * @pre IsTileType(t, MP_OBJECT)
 */
static inline void SetObjectGroundCounter(TileIndex t, uint c)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	SB(_m[t].m4, 5, 3, c);
}


/**
 * Sets ground type and density in one go, also sets the counter to 0
 * @param t       the tile to set the ground type and density for
 * @param type    the new ground type of the tile
 * @param density the density of the ground tile
 * @pre IsTileType(t, MP_OBJECT)
 */
static inline void SetObjectGroundTypeDensity(TileIndex t, ObjectGround type, uint density)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	_m[t].m4 = 0 << 5 | type << 2 | density;
}

static inline ObjectEffectiveFoundationType GetObjectEffectiveFoundationType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return (ObjectEffectiveFoundationType)GB(_me[t].m6, 0, 2);
}

static inline void SetObjectEffectiveFoundationType(TileIndex t, ObjectEffectiveFoundationType foundation_type)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	SB(_me[t].m6, 0, 2, foundation_type);
}

static inline bool GetObjectHasViewportMapViewOverride(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	return HasBit(_m[t].m4, 4);
}

static inline void SetObjectHasViewportMapViewOverride(TileIndex t, bool map_view_override)
{
	dbg_assert_tile(IsTileType(t, MP_OBJECT), t);
	SB(_m[t].m4, 4, 1, map_view_override ? 1 : 0);
}

/**
 * Make an Object tile.
 * @param t      The tile to make and object tile.
 * @param o      The new owner of the tile.
 * @param index  Index to the object.
 * @param wc     Water class for this object.
 * @param random Random data to store on the tile
 */
static inline void MakeObject(TileIndex t, Owner o, ObjectID index, WaterClass wc, byte random)
{
	SetTileType(t, MP_OBJECT);
	SetTileOwner(t, o);
	SetWaterClass(t, wc);
	_m[t].m2 = index;
	_m[t].m3 = random;
	_m[t].m4 = 0;
	_m[t].m5 = index >> 16;
	SB(_me[t].m6, 2, 4, 0);
	_me[t].m7 = 0;
}

#endif /* OBJECT_MAP_H */
