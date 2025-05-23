/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_map.h Maps accessors for stations. */

#ifndef STATION_MAP_H
#define STATION_MAP_H

#include "rail_map.h"
#include "road_map.h"
#include "water_map.h"
#include "station_func.h"
#include "rail.h"
#include "road.h"

typedef uint8_t StationGfx; ///< Index of station graphics. @see _station_display_datas

/**
 * Get StationID from a tile
 * @param t Tile to query station ID from
 * @pre IsTileType(t, MP_STATION)
 * @return Station ID of the station at \a t
 */
inline StationID GetStationIndex(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return (StationID)_m[t].m2;
}


static const int GFX_DOCK_BASE_WATER_PART          =  4; ///< The offset for the water parts.
static const int GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET =  4; ///< The offset for the drive through parts.

/**
 * Get the station type of this tile
 * @param t the tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return the station type
 */
inline StationType GetStationType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return (StationType)GB(_me[t].m6, 3, 4);
}

/**
 * Get the road stop type of this tile
 * @param t the tile to query
 * @pre GetStationType(t) == StationType::Truck || GetStationType(t) == StationType::Bus
 * @return the road stop type
 */
inline RoadStopType GetRoadStopType(TileIndex t)
{
	dbg_assert_tile(GetStationType(t) == StationType::Truck || GetStationType(t) == StationType::Bus, t);
	return GetStationType(t) == StationType::Truck ? RoadStopType::Truck : RoadStopType::Bus;
}

/**
 * Get the station graphics of this tile
 * @param t the tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return the station graphics
 */
inline StationGfx GetStationGfx(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return _m[t].m5;
}

/**
 * Set the station graphics of this tile
 * @param t the tile to update
 * @param gfx the new graphics
 * @pre IsTileType(t, MP_STATION)
 */
inline void SetStationGfx(TileIndex t, StationGfx gfx)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	_m[t].m5 = gfx;
}

/**
 * Is this station tile a rail station?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a rail station
 */
inline bool IsRailStation(TileIndex t)
{
	return GetStationType(t) == StationType::Rail;
}

/**
 * Is this tile a station tile and a rail station?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a rail station
 */
inline bool IsRailStationTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRailStation(t);
}

/**
 * Is this station tile a rail waypoint?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is a rail waypoint
 */
inline bool IsRailWaypoint(TileIndex t)
{
	return GetStationType(t) == StationType::RailWaypoint;
}

/**
 * Is this tile a station tile and a rail waypoint?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a rail waypoint
 */
inline bool IsRailWaypointTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRailWaypoint(t);
}

/**
 * Has this station tile a rail? In other words, is this station
 * tile a rail station or rail waypoint?
 * @param t the tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile has rail
 */
inline bool HasStationRail(TileIndex t)
{
	return IsRailStation(t) || IsRailWaypoint(t);
}

/**
 * Has this station tile a rail? In other words, is this station
 * tile a rail station or rail waypoint?
 * @param t the tile to check
 * @return true if and only if the tile is a station tile and has rail
 */
inline bool HasStationTileRail(TileIndex t)
{
	return IsTileType(t, MP_STATION) && HasStationRail(t);
}

/**
 * Is this station tile an airport?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_STATION)
 * @return true if and only if the tile is an airport
 */
inline bool IsAirport(TileIndex t)
{
	return GetStationType(t) == StationType::Airport;
}

/**
 * Is this tile a station tile and an airport tile?
 * @param t the tile to get the information from
 * @return true if and only if the tile is an airport
 */
inline bool IsAirportTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsAirport(t);
}

bool IsHangar(TileIndex t);

/**
 * Is the station at \a t a truck stop?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station is a truck stop, \c false otherwise
 */
inline bool IsTruckStop(TileIndex t)
{
	return GetStationType(t) == StationType::Truck;
}

/**
 * Is the station at \a t a bus stop?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station is a bus stop, \c false otherwise
 */
inline bool IsBusStop(TileIndex t)
{
	return GetStationType(t) == StationType::Bus;
}

/**
 * Is the station at \a t a road waypoint?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station is a road waypoint, \c false otherwise
 */
inline bool IsRoadWaypoint(TileIndex t)
{
	return GetStationType(t) == StationType::RoadWaypoint;
}

/**
 * Is this tile a station tile and a road waypoint?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a road waypoint
 */
inline bool IsRoadWaypointTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsRoadWaypoint(t);
}

/**
 * Is the station at \a t a road station?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station at the tile is a bus stop, truck stop \c false otherwise
 */
inline bool IsStationRoadStop(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return IsTruckStop(t) || IsBusStop(t);
}

/**
 * Is tile \a t a road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a road stop
 */
inline bool IsStationRoadStopTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsStationRoadStop(t);
}

/**
 * Is the station at \a t a road station?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if station at the tile is a bus stop, truck stop or road waypoint, \c false otherwise
 */
inline bool IsAnyRoadStop(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return IsTruckStop(t) || IsBusStop(t) || IsRoadWaypoint(t);
}

/**
 * Is tile \a t a road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a road stop
 */
inline bool IsAnyRoadStopTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsAnyRoadStop(t);
}

/**
 * Is tile \a t a bay (non-drive through) road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a bay road stop
 */
inline bool IsBayRoadStopTile(TileIndex t)
{
	return IsAnyRoadStopTile(t) && GetStationGfx(t) < GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET;
}

/**
 * Is tile \a t a drive through road stop station?
 * @param t Tile to check
 * @return \c true if the tile is a station tile and a drive through road stop
 */
inline bool IsDriveThroughStopTile(TileIndex t)
{
	return IsAnyRoadStopTile(t) && GetStationGfx(t) >= GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET;
}

/**
 * Gets the disallowed directions
 * @param t the tile to get the directions from
 * @return the disallowed directions
 */
inline DisallowedRoadDirections GetDriveThroughStopDisallowedRoadDirections(TileIndex t)
{
	dbg_assert_tile(IsDriveThroughStopTile(t), t);
	return (DisallowedRoadDirections)GB(_m[t].m3, 0, 2);
}

/**
 * Sets the disallowed directions
 * @param t   the tile to set the directions for
 * @param drd the disallowed directions
 */
inline void SetDriveThroughStopDisallowedRoadDirections(TileIndex t, DisallowedRoadDirections drd)
{
	dbg_assert_tile(IsDriveThroughStopTile(t), t);
	dbg_assert(drd < DRD_END);
	SB(_m[t].m3, 0, 2, drd);
}

/**
 * Get the decorations of a road waypoint.
 * @param tile The tile to query.
 * @return The road decoration of the tile.
 */
inline Roadside GetRoadWaypointRoadside(TileIndex tile)
{
	dbg_assert_tile(IsRoadWaypointTile(tile), tile);
	return (Roadside)GB(_m[tile].m3, 2, 2);
}

/**
 * Set the decorations of a road waypoint.
 * @param tile The tile to change.
 * @param s    The new road decoration of the tile.
 */
inline void SetRoadWaypointRoadside(TileIndex tile, Roadside s)
{
	dbg_assert_tile(IsRoadWaypointTile(tile), tile);
	SB(_m[tile].m3, 2, 2, s);
}

/**
 * Check if a road waypoint tile has snow/desert.
 * @param t The tile to query.
 * @return True if the tile has snow/desert.
 */
inline bool IsRoadWaypointOnSnowOrDesert(TileIndex t)
{
	dbg_assert_tile(IsRoadWaypointTile(t), t);
	return HasBit(_me[t].m8, 15);
}

/**
 * Toggle the snow/desert state of a road waypoint tile.
 * @param t The tile to change.
 */
inline void ToggleRoadWaypointOnSnowOrDesert(TileIndex t)
{
	dbg_assert_tile(IsRoadWaypointTile(t), t);
	ToggleBit(_me[t].m8, 15);
}

StationGfx GetTranslatedAirportTileID(StationGfx gfx);

/**
 * Get the station graphics of this airport tile
 * @param t the tile to query
 * @pre IsAirport(t)
 * @return the station graphics
 */
inline StationGfx GetAirportGfx(TileIndex t)
{
	dbg_assert_tile(IsAirport(t), t);
	return GetTranslatedAirportTileID(GetStationGfx(t));
}

/**
 * Gets the direction the bay road stop entrance points towards.
 * @param t the tile of the road stop
 * @pre IsBayRoadStopTile(t)
 * @return the direction of the entrance
 */
inline DiagDirection GetBayRoadStopDir(TileIndex t)
{
	dbg_assert_tile(IsBayRoadStopTile(t), t);
	return static_cast<DiagDirection>(GetStationGfx(t));
}

/**
 * Gets the axis of the drive through stop.
 * @param t the tile of the road stop
 * @pre IsDriveThroughStopTile(t)
 * @return the axis the drive through is in
 */
inline Axis GetDriveThroughStopAxis(TileIndex t)
{
	dbg_assert_tile(IsDriveThroughStopTile(t), t);
	return static_cast<Axis>(GetStationGfx(t) - GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET);
}

/**
 * Is tile \a t part of an oilrig?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is an oilrig tile
 */
inline bool IsOilRig(TileIndex t)
{
	return GetStationType(t) == StationType::Oilrig;
}

/**
 * Is tile \a t a dock tile?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is a dock
 */
inline bool IsDock(TileIndex t)
{
	return GetStationType(t) == StationType::Dock;
}

/**
 * Is tile \a t a dock tile?
 * @param t Tile to check
 * @return \c true if the tile is a dock
 */
inline bool IsDockTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && GetStationType(t) == StationType::Dock;
}

/**
 * Is tile \a t a buoy tile?
 * @param t Tile to check
 * @pre IsTileType(t, MP_STATION)
 * @return \c true if the tile is a buoy
 */
inline bool IsBuoy(TileIndex t)
{
	return GetStationType(t) == StationType::Buoy;
}

/**
 * Is tile \a t a buoy tile?
 * @param t Tile to check
 * @return \c true if the tile is a buoy
 */
inline bool IsBuoyTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsBuoy(t);
}

/**
 * Is tile \a t an hangar tile?
 * @param t Tile to check
 * @return \c true if the tile is an hangar
 */
inline bool IsHangarTile(TileIndex t)
{
	return IsTileType(t, MP_STATION) && IsHangar(t);
}

/**
 * Is tile \a t a blocked tile?
 * @pre HasStationRail(t)
 * @param t Tile to check
 * @return \c true if the tile is blocked
 */
inline bool IsStationTileBlocked(TileIndex t)
{
	assert(HasStationRail(t));
	return HasBit(_me[t].m6, 0);
}

/**
 * Set the blocked state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @param b the blocked state
 */
inline void SetStationTileBlocked(TileIndex t, bool b)
{
	assert(HasStationRail(t));
	AssignBit(_me[t].m6, 0, b);
}

/**
 * Can tile \a t have catenary wires?
 * @pre HasStationRail(t)
 * @param t Tile to check
 * @return \c true if the tile can have catenary wires
 */
inline bool CanStationTileHaveWires(TileIndex t)
{
	assert(HasStationRail(t));
	return HasBit(_me[t].m6, 1);
}

/**
 * Set the catenary wires state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @param b the catenary wires state
 */
inline void SetStationTileHaveWires(TileIndex t, bool b)
{
	assert(HasStationRail(t));
	AssignBit(_me[t].m6, 1, b);
}

/**
 * Can tile \a t have catenary pylons?
 * @pre HasStationRail(t)
 * @param t Tile to check
 * @return \c true if the tile can have catenary pylons
 */
inline bool CanStationTileHavePylons(TileIndex t)
{
	assert(HasStationRail(t));
	return HasBit(_me[t].m6, 7);
}

/**
 * Set the catenary pylon state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @param b the catenary pylons state
 */
inline void SetStationTileHavePylons(TileIndex t, bool b)
{
	assert(HasStationRail(t));
	AssignBit(_me[t].m6, 7, b);
}

/**
 * Get the rail direction of a rail station.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The direction of the rails on tile \a t.
 */
inline Axis GetRailStationAxis(TileIndex t)
{
	dbg_assert_tile(HasStationRail(t), t);
	return HasBit(GetStationGfx(t), 0) ? AXIS_Y : AXIS_X;
}

/**
 * Get the rail track of a rail station tile.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The rail track of the rails on tile \a t.
 */
inline Track GetRailStationTrack(TileIndex t)
{
	return AxisToTrack(GetRailStationAxis(t));
}

/**
 * Get the trackbits of a rail station tile.
 * @param t Tile to query
 * @pre HasStationRail(t)
 * @return The trackbits of the rails on tile \a t.
 */
inline TrackBits GetRailStationTrackBits(TileIndex t)
{
	return AxisToTrackBits(GetRailStationAxis(t));
}

/**
 * Check if a tile is a valid continuation to a railstation tile.
 * The tile \a test_tile is a valid continuation to \a station_tile, if all of the following are true:
 * \li \a test_tile is a rail station tile
 * \li the railtype of \a test_tile is compatible with the railtype of \a station_tile
 * \li the tracks on \a test_tile and \a station_tile are in the same direction
 * \li both tiles belong to the same station
 * \li \a test_tile is not blocked (@see IsStationTileBlocked)
 * @param test_tile Tile to test
 * @param station_tile Station tile to compare with
 * @pre IsRailStationTile(station_tile)
 * @return true if the two tiles are compatible
 */
inline bool IsCompatibleTrainStationTile(TileIndex test_tile, TileIndex station_tile)
{
	dbg_assert_tile(IsRailStationTile(station_tile), station_tile);
	return IsRailStationTile(test_tile) && !IsStationTileBlocked(test_tile) &&
			IsCompatibleRail(GetRailType(test_tile), GetRailType(station_tile)) &&
			GetRailStationAxis(test_tile) == GetRailStationAxis(station_tile) &&
			GetStationIndex(test_tile) == GetStationIndex(station_tile);
}

/**
 * Get the reservation state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @return reservation state
 */
inline bool HasStationReservation(TileIndex t)
{
	dbg_assert_tile(HasStationRail(t), t);
	return HasBit(_me[t].m6, 2);
}

/**
 * Set the reservation state of the rail station
 * @pre HasStationRail(t)
 * @param t the station tile
 * @param b the reservation state
 */
inline void SetRailStationReservation(TileIndex t, bool b)
{
	dbg_assert_tile(HasStationRail(t), t);
	AssignBit(_me[t].m6, 2, b);
}

/**
 * Get the reserved track bits for a waypoint
 * @pre HasStationRail(t)
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetStationReservationTrackBits(TileIndex t)
{
	return HasStationReservation(t) ? GetRailStationTrackBits(t) : TRACK_BIT_NONE;
}

/**
 * Get the direction of a dock.
 * @param t Tile to query
 * @pre IsDock(t)
 * @pre \a t is the land part of the dock
 * @return The direction of the dock on tile \a t.
 */
inline DiagDirection GetDockDirection(TileIndex t)
{
	StationGfx gfx = GetStationGfx(t);
	dbg_assert_tile(IsDock(t) && gfx < GFX_DOCK_BASE_WATER_PART, t);
	return (DiagDirection)(gfx);
}

/**
 * Check whether a dock tile is the tile on water.
 */
inline bool IsDockWaterPart(TileIndex t)
{
	assert(IsDockTile(t));
	StationGfx gfx = GetStationGfx(t);
	return gfx >= GFX_DOCK_BASE_WATER_PART;
}

/**
 * Is there a custom rail station spec on this tile?
 * @param t Tile to query
 * @pre HasStationTileRail(t)
 * @return True if this station is part of a newgrf station.
 */
inline bool IsCustomStationSpecIndex(TileIndex t)
{
	dbg_assert_tile(HasStationTileRail(t), t);
	return _m[t].m4 != 0;
}

/**
 * Set the custom station spec for this tile.
 * @param t Tile to set the stationspec of.
 * @param specindex The new spec.
 * @pre HasStationTileRail(t)
 */
inline void SetCustomStationSpecIndex(TileIndex t, uint8_t specindex)
{
	dbg_assert_tile(HasStationTileRail(t), t);
	_m[t].m4 = specindex;
}

/**
 * Get the custom station spec for this tile.
 * @param t Tile to query
 * @pre HasStationTileRail(t)
 * @return The custom station spec of this tile.
 */
inline uint GetCustomStationSpecIndex(TileIndex t)
{
	dbg_assert_tile(HasStationTileRail(t), t);
	return _m[t].m4;
}

/**
 * Is there a custom road stop spec on this tile?
 * @param t Tile to query
 * @pre IsAnyRoadStopTile(t)
 * @return True if this station is part of a newgrf station.
 */
inline bool IsCustomRoadStopSpecIndex(TileIndex t)
{
	dbg_assert_tile(IsAnyRoadStopTile(t), t);
	return GB(_me[t].m8, 0, 6) != 0;
}

/**
 * Set the custom road stop spec for this tile.
 * @param t Tile to set the stationspec of.
 * @param specindex The new spec.
 * @pre IsAnyRoadStopTile(t)
 */
inline void SetCustomRoadStopSpecIndex(TileIndex t, uint8_t specindex)
{
	dbg_assert_tile(IsAnyRoadStopTile(t), t);
	SB(_me[t].m8, 0, 6, specindex);
}

/**
 * Get the custom road stop spec for this tile.
 * @param t Tile to query
 * @pre IsAnyRoadStopTile(t)
 * @return The custom station spec of this tile.
 */
inline uint GetCustomRoadStopSpecIndex(TileIndex t)
{
	dbg_assert_tile(IsAnyRoadStopTile(t), t);
	return GB(_me[t].m8, 0, 6);
}

/**
 * Set the random bits for a station tile.
 * @param t Tile to set random bits for.
 * @param random_bits The random bits.
 * @pre IsTileType(t, MP_STATION)
 */
inline void SetStationTileRandomBits(TileIndex t, uint8_t random_bits)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	SB(_m[t].m3, 4, 4, random_bits);
}

/**
 * Get the random bits of a station tile.
 * @param t Tile to query
 * @pre IsTileType(t, MP_STATION)
 * @return The random bits for this station tile.
 */
inline uint8_t GetStationTileRandomBits(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_STATION), t);
	return GB(_m[t].m3, 4, 4);
}

/**
 * Make the given tile a station tile.
 * @param t the tile to make a station tile
 * @param o the owner of the station
 * @param sid the station to which this tile belongs
 * @param st the type this station tile
 * @param section the StationGfx to be used for this tile
 * @param wc The water class of the station
 */
inline void MakeStation(TileIndex t, Owner o, StationID sid, StationType st, uint8_t section, WaterClass wc = WATER_CLASS_INVALID)
{
	SetTileType(t, MP_STATION);
	SetTileOwner(t, o);
	SetWaterClass(t, wc);
	SetDockingTile(t, false);
	_m[t].m2 = sid;
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = section;
	SB(_me[t].m6, 2, 1, 0);
	SB(_me[t].m6, 3, 4, to_underlying(st));
	_me[t].m7 = 0;
	_me[t].m8 = 0;
}

/**
 * Make the given tile a rail station tile.
 * @param t the tile to make a rail station tile
 * @param o the owner of the station
 * @param sid the station to which this tile belongs
 * @param a the axis of this tile
 * @param section the StationGfx to be used for this tile
 * @param rt the railtype of this tile
 */
inline void MakeRailStation(TileIndex t, Owner o, StationID sid, Axis a, uint8_t section, RailType rt)
{
	MakeStation(t, o, sid, StationType::Rail, section + a);
	SetRailType(t, rt);
	SetRailStationReservation(t, false);
}

/**
 * Make the given tile a rail waypoint tile.
 * @param t the tile to make a rail waypoint
 * @param o the owner of the waypoint
 * @param sid the waypoint to which this tile belongs
 * @param a the axis of this tile
 * @param section the StationGfx to be used for this tile
 * @param rt the railtype of this tile
 */
inline void MakeRailWaypoint(TileIndex t, Owner o, StationID sid, Axis a, uint8_t section, RailType rt)
{
	MakeStation(t, o, sid, StationType::RailWaypoint, section + a);
	SetRailType(t, rt);
	SetRailStationReservation(t, false);
}

/**
 * Make the given tile a roadstop tile.
 * @param t the tile to make a roadstop
 * @param o the owner of the roadstop
 * @param sid the station to which this tile belongs
 * @param rst the type of roadstop to make this tile
 * @param road_rt the road roadtype on this tile
 * @param tram_rt the tram roadtype on this tile
 * @param d the direction of the roadstop
 */
inline void MakeRoadStop(TileIndex t, Owner o, StationID sid, RoadStopType rst, RoadType road_rt, RoadType tram_rt, DiagDirection d)
{
	MakeStation(t, o, sid, (rst == RoadStopType::Bus ? StationType::Bus : StationType::Truck), d);
	SetRoadTypes(t, road_rt, tram_rt);
	SetRoadOwner(t, RTT_ROAD, o);
	SetRoadOwner(t, RTT_TRAM, o);
}

/**
 * Make the given tile a drivethrough roadstop tile.
 * @param t the tile to make a roadstop
 * @param station the owner of the roadstop
 * @param road the owner of the road
 * @param tram the owner of the tram
 * @param sid the station to which this tile belongs
 * @param rst the type of roadstop to make this tile
 * @param road_rt the road roadtype on this tile
 * @param tram_rt the tram roadtype on this tile
 * @param a the direction of the roadstop
 */
inline void MakeDriveThroughRoadStop(TileIndex t, Owner station, Owner road, Owner tram, StationID sid, StationType rst, RoadType road_rt, RoadType tram_rt, Axis a)
{
	MakeStation(t, station, sid, rst, GFX_TRUCK_BUS_DRIVETHROUGH_OFFSET + a);
	SetRoadTypes(t, road_rt, tram_rt);
	SetRoadOwner(t, RTT_ROAD, road);
	SetRoadOwner(t, RTT_TRAM, tram);
}

/**
 * Make the given tile an airport tile.
 * @param t the tile to make a airport
 * @param o the owner of the airport
 * @param sid the station to which this tile belongs
 * @param section the StationGfx to be used for this tile
 * @param wc the type of water on this tile
 */
inline void MakeAirport(TileIndex t, Owner o, StationID sid, uint8_t section, WaterClass wc)
{
	MakeStation(t, o, sid, StationType::Airport, section, wc);
}

/**
 * Make the given tile a buoy tile.
 * @param t the tile to make a buoy
 * @param sid the station to which this tile belongs
 * @param wc the type of water on this tile
 */
inline void MakeBuoy(TileIndex t, StationID sid, WaterClass wc)
{
	/* Make the owner of the buoy tile the same as the current owner of the
	 * water tile. In this way, we can reset the owner of the water to its
	 * original state when the buoy gets removed. */
	MakeStation(t, GetTileOwner(t), sid, StationType::Buoy, 0, wc);
}

/**
 * Make the given tile a dock tile.
 * @param t the tile to make a dock
 * @param o the owner of the dock
 * @param sid the station to which this tile belongs
 * @param d the direction of the dock
 * @param wc the type of water on this tile
 */
inline void MakeDock(TileIndex t, Owner o, StationID sid, DiagDirection d, WaterClass wc)
{
	MakeStation(t, o, sid, StationType::Dock, d);
	MakeStation(t + TileOffsByDiagDir(d), o, sid, StationType::Dock, GFX_DOCK_BASE_WATER_PART + DiagDirToAxis(d), wc);
}

/**
 * Make the given tile an oilrig tile.
 * @param t the tile to make an oilrig
 * @param sid the station to which this tile belongs
 * @param wc the type of water on this tile
 */
inline void MakeOilrig(TileIndex t, StationID sid, WaterClass wc)
{
	MakeStation(t, OWNER_NONE, sid, StationType::Oilrig, 0, wc);
}

#endif /* STATION_MAP_H */
