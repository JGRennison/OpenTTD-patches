/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_type.h Types related to stations. */

#ifndef STATION_TYPE_H
#define STATION_TYPE_H

#include "core/smallstack_type.hpp"
#include "tilearea_type.h"
#include "3rdparty/cpp-btree/btree_set.h"

typedef uint16_t StationID;
typedef uint16_t RoadStopID;
typedef uint16_t DockID;

struct BaseStation;
struct Station;
struct RoadStop;
struct StationSpec;
struct Waypoint;

static const StationID NEW_STATION = 0xFFFD;
static const StationID ADJACENT_STATION = 0xFFFE;
static const StationID INVALID_STATION = 0xFFFF;

static const uint MAX_STATION_CARGO_HISTORY_DAYS = 24;

typedef SmallStack<StationID, StationID, INVALID_STATION, 8, 0xFFFD> StationIDStack;

/** Station types */
enum class StationType : uint8_t {
	Rail,
	Airport,
	Truck,
	Bus,
	Oilrig,
	Dock,
	Buoy,
	RailWaypoint,
	RoadWaypoint,
	End,
};

/** Types of RoadStops */
enum class RoadStopType : uint8_t {
	Bus,   ///< A standard stop for buses
	Truck, ///< A standard stop for trucks
	End,   ///< End of valid types
};

/** The facilities a station might be having */
enum StationFacility : uint8_t {
	FACIL_NONE       = 0,      ///< The station has no facilities at all
	FACIL_TRAIN      = 1 << 0, ///< Station with train station
	FACIL_TRUCK_STOP = 1 << 1, ///< Station with truck stops
	FACIL_BUS_STOP   = 1 << 2, ///< Station with bus stops
	FACIL_AIRPORT    = 1 << 3, ///< Station with an airport
	FACIL_DOCK       = 1 << 4, ///< Station with a dock
	FACIL_WAYPOINT   = 1 << 7, ///< Station is a waypoint
};
DECLARE_ENUM_AS_BIT_SET(StationFacility)

/** Fake 'facility' to allow toggling display of recently-removed station signs. */
static constexpr StationFacility FACIL_GHOST{1U << 6};

/** The vehicles that may have visited a station */
enum StationHadVehicleOfType : uint8_t {
	HVOT_NONE     = 0,      ///< Station has seen no vehicles
	HVOT_TRAIN    = 1 << 1, ///< Station has seen a train
	HVOT_BUS      = 1 << 2, ///< Station has seen a bus
	HVOT_TRUCK    = 1 << 3, ///< Station has seen a truck
	HVOT_AIRCRAFT = 1 << 4, ///< Station has seen an aircraft
	HVOT_SHIP     = 1 << 5, ///< Station has seen a ship

	HVOT_WAYPOINT = 1 << 6, ///< Station is a waypoint (NewGRF only!)
};
DECLARE_ENUM_AS_BIT_SET(StationHadVehicleOfType)

/* The different catchment area sizes. */
static constexpr uint CA_NONE = 0; ///< Catchment when the station has no facilities
static constexpr uint CA_BUS = 3; ///< Catchment for bus stops with "modified catchment" enabled
static constexpr uint CA_TRUCK = 3; ///< Catchment for truck stops with "modified catchment" enabled
static constexpr uint CA_TRAIN = 4; ///< Catchment for train stations with "modified catchment" enabled
static constexpr uint CA_DOCK = 5; ///< Catchment for docks with "modified catchment" enabled

static constexpr uint CA_UNMODIFIED = 4; ///< Catchment for all stations with "modified catchment" disabled

static constexpr uint MAX_CATCHMENT = 10; ///< Maximum catchment for airports with "modified catchment" enabled

enum StationDelivery : uint8_t {
	SD_NEAREST_FIRST = 0, ///< Station delivers cargo only to the nearest accepting industry
	SD_BALANCED      = 1  ///< Station delivers cargo equally among accepting industries
};

static const uint MAX_LENGTH_STATION_NAME_CHARS = 128; ///< The maximum length of a station name in characters including '\0'

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

#endif /* STATION_TYPE_H */
