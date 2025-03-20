/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_map.h Accessors for towns */

#ifndef TOWN_MAP_H
#define TOWN_MAP_H

#include "road_map.h"
#include "house.h"

/**
 * Get the index of which town this house/street is attached to.
 * @param t the tile
 * @pre IsTileType(t, MP_HOUSE) or IsTileType(t, MP_ROAD) but not a road depot
 * @return TownID
 */
inline TownID GetTownIndex(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE) || (IsTileType(t, MP_ROAD) && !IsRoadDepot(t)), t);
	return _m[t].m2;
}

/**
 * Set the town index for a road or house tile.
 * @param t the tile
 * @param index the index of the town
 * @pre IsTileType(t, MP_HOUSE) or IsTileType(t, MP_ROAD) but not a road depot
 */
inline void SetTownIndex(TileIndex t, TownID index)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE) || (IsTileType(t, MP_ROAD) && !IsRoadDepot(t)), t);
	_m[t].m2 = index;
}

/**
 * Get the type of this house, which is an index into the house spec array
 * without doing any NewGRF related translations.
 * @param t the tile
 * @pre IsTileType(t, MP_HOUSE)
 * @return house type
 */
inline HouseID GetCleanHouseType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return GB(_me[t].m8, 0, 12);
}

/**
 * Get the type of this house, which is an index into the house spec array
 * @param t the tile
 * @pre IsTileType(t, MP_HOUSE)
 * @return house type
 */
inline HouseID GetHouseType(TileIndex t)
{
	return GetTranslatedHouseID(GetCleanHouseType(t));
}

/**
 * Set the house type.
 * @param t the tile
 * @param house_id the new house type
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void SetHouseType(TileIndex t, HouseID house_id)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	SB(_me[t].m8, 0, 12, house_id);
}

/**
 * Check if the house is protected from removal by towns.
 * @param t The tile.
 * @return If the house is protected from the town upgrading it.
 */
inline bool IsHouseProtected(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return HasBit(_m[t].m3, 5);
}

/**
 * Set a house as protected from removal by towns.
 * @param t The tile.
 * @param house_protected Whether the house is protected from the town upgrading it.
 */
inline void SetHouseProtected(TileIndex t, bool house_protected)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	AssignBit(_m[t].m3, 5, house_protected);
}

/**
 * Check if the lift of this animated house has a destination
 * @param t the tile
 * @return has destination
 */
inline bool LiftHasDestination(TileIndex t)
{
	return HasBit(_me[t].m7, 0);
}

/**
 * Set the new destination of the lift for this animated house, and activate
 * the LiftHasDestination bit.
 * @param t the tile
 * @param dest new destination
 */
inline void SetLiftDestination(TileIndex t, uint8_t dest)
{
	SetBit(_me[t].m7, 0);
	SB(_me[t].m7, 1, 3, dest);
}

/**
 * Get the current destination for this lift
 * @param t the tile
 * @return destination
 */
inline uint8_t GetLiftDestination(TileIndex t)
{
	return GB(_me[t].m7, 1, 3);
}

/**
 * Stop the lift of this animated house from moving.
 * Clears the first 4 bits of m7 at once, clearing the LiftHasDestination bit
 * and the destination.
 * @param t the tile
 */
inline void HaltLift(TileIndex t)
{
	SB(_me[t].m7, 0, 4, 0);
}

/**
 * Get the position of the lift on this animated house
 * @param t the tile
 * @return position, from 0 to 36
 */
inline uint8_t GetLiftPosition(TileIndex t)
{
	return GB(_me[t].m6, 2, 6);
}

/**
 * Set the position of the lift on this animated house
 * @param t the tile
 * @param pos position, from 0 to 36
 */
inline void SetLiftPosition(TileIndex t, uint8_t pos)
{
	SB(_me[t].m6, 2, 6, pos);
}

/**
 * Get the completion of this house
 * @param t the tile
 * @return true if it is, false if it is not
 */
inline bool IsHouseCompleted(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return HasBit(_m[t].m3, 7);
}

/**
 * Mark this house as been completed
 * @param t the tile
 * @param status
 */
inline void SetHouseCompleted(TileIndex t, bool status)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	SB(_m[t].m3, 7, 1, !!status);
}

/**
 * House Construction Scheme.
 *  Construction counter, for buildings under construction. Incremented on every
 *  periodic tile processing.
 *  On wraparound, the stage of building in is increased.
 *  GetHouseBuildingStage is taking care of the real stages,
 *  (as the sprite for the next phase of house building)
 *  (Get|Inc)HouseConstructionTick is simply a tick counter between the
 *  different stages
 */

/**
 * Gets the building stage of a house
 * Since the stage is used for determining what sprite to use,
 * if the house is complete (and that stage no longer is available),
 * fool the system by returning the TOWN_HOUSE_COMPLETE (3),
 * thus showing a beautiful complete house.
 * @param t the tile of the house to get the building stage of
 * @pre IsTileType(t, MP_HOUSE)
 * @return the building stage of the house
 */
inline uint8_t GetHouseBuildingStage(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return IsHouseCompleted(t) ? (uint8_t)TOWN_HOUSE_COMPLETED : GB(_m[t].m5, 3, 2);
}

/**
 * Gets the construction stage of a house
 * @param t the tile of the house to get the construction stage of
 * @pre IsTileType(t, MP_HOUSE)
 * @return the construction stage of the house
 */
inline uint8_t GetHouseConstructionTick(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return IsHouseCompleted(t) ? 0 : GB(_m[t].m5, 0, 3);
}

/**
 * Sets the increment stage of a house
 * It is working with the whole counter + stage 5 bits, making it
 * easier to work:  the wraparound is automatic.
 * @param t the tile of the house to increment the construction stage of
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void IncHouseConstructionTick(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	AB(_m[t].m5, 0, 5, 1);

	if (GB(_m[t].m5, 3, 2) == TOWN_HOUSE_COMPLETED) {
		/* House is now completed.
		 * Store the year of construction as well, for newgrf house purpose */
		SetHouseCompleted(t, true);
	}
}

/**
 * Sets the age of the house to zero.
 * Needs to be called after the house is completed. During construction stages the map space is used otherwise.
 * @param t the tile of this house
 * @pre IsTileType(t, MP_HOUSE) && IsHouseCompleted(t)
 */
inline void ResetHouseAge(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE) && IsHouseCompleted(t), t);
	_m[t].m5 = 0;
}

/**
 * Increments the age of the house.
 * @param t the tile of this house
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void IncrementHouseAge(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	if (IsHouseCompleted(t) && _m[t].m5 < 0xFF) _m[t].m5++;
}

/**
 * Get the age of the house
 * @param t the tile of this house
 * @pre IsTileType(t, MP_HOUSE)
 * @return year
 */
inline CalTime::Year GetHouseAge(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return CalTime::Year{IsHouseCompleted(t) ? _m[t].m5 : 0};
}

/**
 * Set the random bits for this house.
 * This is required for newgrf house
 * @param t      the tile of this house
 * @param random the new random bits
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void SetHouseRandomBits(TileIndex t, uint8_t random)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	_m[t].m1 = random;
}

/**
 * Get the random bits for this house.
 * This is required for newgrf house
 * @param t the tile of this house
 * @pre IsTileType(t, MP_HOUSE)
 * @return random bits
 */
inline uint8_t GetHouseRandomBits(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return _m[t].m1;
}

/**
 * Set the activated triggers bits for this house.
 * This is required for newgrf house
 * @param t        the tile of this house
 * @param triggers the activated triggers
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void SetHouseTriggers(TileIndex t, uint8_t triggers)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	SB(_m[t].m3, 0, 5, triggers);
}

/**
 * Get the already activated triggers bits for this house.
 * This is required for newgrf house
 * @param t the tile of this house
 * @pre IsTileType(t, MP_HOUSE)
 * @return triggers
 */
inline uint8_t GetHouseTriggers(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return GB(_m[t].m3, 0, 5);
}

/**
 * Get the amount of time remaining before the tile loop processes this tile.
 * @param t the house tile
 * @pre IsTileType(t, MP_HOUSE)
 * @return time remaining
 */
inline uint8_t GetHouseProcessingTime(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	return GB(_me[t].m6, 2, 6);
}

/**
 * Set the amount of time remaining before the tile loop processes this tile.
 * @param t the house tile
 * @param time the time to be set
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void SetHouseProcessingTime(TileIndex t, uint8_t time)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	SB(_me[t].m6, 2, 6, time);
}

/**
 * Decrease the amount of time remaining before the tile loop processes this tile.
 * @param t the house tile
 * @pre IsTileType(t, MP_HOUSE)
 */
inline void DecHouseProcessingTime(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_HOUSE), t);
	_me[t].m6 -= 1 << 2;
}

/**
 * Make the tile a house.
 * @param t tile index
 * @param tid Town index
 * @param counter of construction step
 * @param stage of construction (used for drawing)
 * @param type of house.  Index into house specs array
 * @param random_bits required for newgrf houses
 * @param house_protected Whether the house is protected from the town upgrading it.
 * @pre IsTileType(t, MP_CLEAR)
 */
inline void MakeHouseTile(TileIndex t, TownID tid, uint8_t counter, uint8_t stage, HouseID type, uint8_t random_bits, bool house_protected)
{
	dbg_assert_tile(IsTileType(t, MP_CLEAR), t);

	SetTileType(t, MP_HOUSE);
	_m[t].m1 = random_bits;
	_m[t].m2 = tid;
	_m[t].m3 = 0;
	SetHouseType(t, type);
	SetHouseCompleted(t, stage == TOWN_HOUSE_COMPLETED);
	_m[t].m5 = IsHouseCompleted(t) ? 0 : (stage << 3 | counter);
	SetHouseProtected(t, house_protected);
	SetAnimationFrame(t, 0);
	SetHouseProcessingTime(t, HouseSpec::Get(type)->processing_time);
}

#endif /* TOWN_MAP_H */
