/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_container.h Types related to station containers. */

#ifndef STATION_CONTAINER_H
#define STATION_CONTAINER_H

#include "station_type.h"
#include "tilearea_type.h"
#include "3rdparty/cpp-btree/btree_set.h"

struct StationCompare {
	bool operator() (const Station *lhs, const Station *rhs) const;
};

/** List of stations */
typedef btree::btree_set<Station *, StationCompare> StationList;

/**
 * Structure contains cached list of stations nearby. The list
 * is created upon first call to GetStations()
 */
class StationFinder : TileArea {
	StationList stations; ///< List of stations nearby
public:
	/**
	 * Constructs StationFinder
	 * @param area the area to search from
	 */
	StationFinder(const TileArea &area) : TileArea(area) {}
	const StationList &GetStations();
};

#endif /* STATION_CONTAINER_H */
