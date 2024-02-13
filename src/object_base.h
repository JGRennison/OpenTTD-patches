/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file object_base.h Base for all objects. */

#ifndef OBJECT_BASE_H
#define OBJECT_BASE_H

#include "core/pool_type.hpp"
#include "object_type.h"
#include "tilearea_type.h"
#include "town_type.h"
#include "date_type.h"
#include <vector>

typedef Pool<Object, ObjectID, 64, 0xFF0000> ObjectPool;
extern ObjectPool _object_pool;

/** An object, such as transmitter, on the map. */
struct Object : ObjectPool::PoolItem<&_object_pool> {
	ObjectType type;    ///< Type of the object
	Town *town;         ///< Town the object is built in
	TileArea location;  ///< Location of the object
	CalTime::Date build_date; ///< Date of construction
	byte colour;        ///< Colour of the object, for display purpose
	byte view;          ///< The view setting for this object

	/** Make sure the object isn't zeroed. */
	Object() {}
	/** Make sure the right destructor is called as well! */
	~Object() {}

	static Object *GetByTile(TileIndex tile);

	/**
	 * Increment the count of objects for this type.
	 * @param type ObjectType to increment
	 * @pre type < NUM_OBJECTS
	 */
	static inline void IncTypeCount(ObjectType type)
	{
		dbg_assert(type < NUM_OBJECTS);
		if (type >= counts.size()) counts.resize(type + 1);
		counts[type]++;
	}

	/**
	 * Decrement the count of objects for this type.
	 * @param type ObjectType to decrement
	 * @pre type < NUM_OBJECTS
	 */
	static inline void DecTypeCount(ObjectType type)
	{
		dbg_assert(type < NUM_OBJECTS);
		dbg_assert(type < counts.size());
		counts[type]--;
	}

	/**
	 * Get the count of objects for this type.
	 * @param type ObjectType to query
	 * @pre type < NUM_OBJECTS
	 */
	static inline uint16_t GetTypeCount(ObjectType type)
	{
		dbg_assert(type < NUM_OBJECTS);
		if (type >= counts.size()) return 0;
		return counts[type];
	}

	/** Resets object counts. */
	static inline void ResetTypeCounts()
	{
		counts.clear();
	}

protected:
	static std::vector<uint16_t> counts; ///< Number of objects per type ingame
};

/**
 * Keeps track of removed objects during execution/testruns of commands.
 */
struct ClearedObjectArea {
	TileIndex first_tile;  ///< The first tile being cleared, which then causes the whole object to be cleared.
	TileArea area;         ///< The area of the object.
};

ClearedObjectArea *FindClearedObject(TileIndex tile);
extern std::vector<ClearedObjectArea> _cleared_object_areas;

#endif /* OBJECT_BASE_H */
