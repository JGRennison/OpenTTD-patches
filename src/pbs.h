/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pbs.h PBS support routines */

#ifndef PBS_H
#define PBS_H

#include "tile_type.h"
#include "direction_type.h"
#include "track_type.h"
#include "vehicle_type.h"

TrackBits GetReservedTrackbits(TileIndex t);

void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b);

bool TryReserveRailTrack(TileIndex tile, Track t, bool trigger_stations = true);
bool TryReserveRailTrackdir(TileIndex tile, Trackdir td, bool trigger_stations = true);
void UnreserveRailTrack(TileIndex tile, Track t);
void UnreserveRailTrackdir(TileIndex tile, Trackdir td);

/** This struct contains information about the end of a reserved path. */
struct PBSTileInfo {
	TileIndex tile;      ///< Tile the path ends, INVALID_TILE if no valid path was found.
	Trackdir  trackdir;  ///< The reserved trackdir on the tile.
	bool      okay;      ///< True if tile is a safe waiting position, false otherwise.

	/**
	 * Create an empty PBSTileInfo.
	 */
	PBSTileInfo() : tile(INVALID_TILE), trackdir(INVALID_TRACKDIR), okay(false) {}

	/**
	 * Create a PBSTileInfo with given tile, track direction and safe waiting position information.
	 * @param _t The tile where the path ends.
	 * @param _td The reserved track dir on the tile.
	 * @param _okay Whether the tile is a safe waiting point or not.
	 */
	PBSTileInfo(TileIndex _t, Trackdir _td, bool _okay) : tile(_t), trackdir(_td), okay(_okay) {}
};

struct PBSWaitingPositionRestrictedSignalInfo {
	TileIndex tile = INVALID_TILE;
	Trackdir  trackdir = INVALID_TRACKDIR;
};

enum TrainReservationLookAheadItemType : byte {
	TRLIT_STATION                = 0,     ///< Station/waypoint
	TRLIT_REVERSE                = 1,     ///< Reverse behind signal
	TRLIT_TRACK_SPEED            = 2,     ///< Track or bridge speed limit
	TRLIT_SPEED_RESTRICTION      = 3,     ///< Speed restriction
	TRLIT_SIGNAL                 = 4,     ///< Signal
	TRLIT_CURVE_SPEED            = 5,     ///< Curve speed limit
};

struct TrainReservationLookAheadItem {
	int32 start;
	int32 end;
	int16 z_pos;
	uint16 data_id;
	TrainReservationLookAheadItemType type;
};

struct TrainReservationLookAheadCurve {
	int32 position;
	DirDiff dir_diff;
};

enum TrainReservationLookAheadFlags {
	TRLF_TB_EXIT_FREE      = 0,           ///< Reservation ends at signalled tunnel/bridge entrance and the corresponding exit is free, but may not be reserved
	TRLF_DEPOT_END         = 1,           ///< Reservation ends at a depot
	TRLF_APPLY_ADVISORY    = 2,           ///< Apply advisory speed limit on next iteration
	TRLF_CHUNNEL           = 3,           ///< Reservation ends at a signalled chunnel entrance
};

struct TrainReservationLookAhead {
	TileIndex reservation_end_tile;       ///< Tile the reservation ends.
	Trackdir  reservation_end_trackdir;   ///< The reserved trackdir on the end tile.
	int32 current_position;               ///< Current position of the train on the reservation
	int32 reservation_end_position;       ///< Position of the end of the reservation
	int16 reservation_end_z;              ///< The z coordinate of the reservation end
	int16 tunnel_bridge_reserved_tiles;   ///< How many tiles a reservation into the tunnel/bridge currently extends into the wormhole
	uint16 flags;                         ///< Flags (TrainReservationLookAheadFlags)
	uint16 speed_restriction;
	std::deque<TrainReservationLookAheadItem> items;
	std::deque<TrainReservationLookAheadCurve> curves;

	int32 RealEndPosition() const
	{
		return this->reservation_end_position - (this->tunnel_bridge_reserved_tiles * TILE_SIZE);
	}

	void AddStation(int tiles, StationID id, int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end, end + (((int)TILE_SIZE) * tiles), z_pos, id, TRLIT_STATION });
	}

	void AddReverse(int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end, end, z_pos, 0, TRLIT_REVERSE });
	}

	void AddTrackSpeedLimit(uint16 speed, int offset, int duration, int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset + duration, z_pos, speed, TRLIT_TRACK_SPEED });
	}

	void AddSpeedRestriction(uint16 speed, int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end, end, z_pos, speed, TRLIT_SPEED_RESTRICTION });
		this->speed_restriction = speed;
	}

	void AddSignal(uint16 target_speed, int offset, int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset, z_pos, target_speed, TRLIT_SIGNAL });
	}

	void AddCurveSpeedLimit(uint16 target_speed, int offset, int16 z_pos)
	{
		int end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset, z_pos, target_speed, TRLIT_CURVE_SPEED });
	}
};

/** Flags for FollowTrainReservation */
enum FollowTrainReservationFlags {
	FTRF_NONE                 = 0,        ///< No flags
	FTRF_IGNORE_LOOKAHEAD     = 0x01,     ///< No use of cached lookahead
	FTRF_OKAY_UNUSED          = 0x02,     ///< 'okay' return value is not used
};
DECLARE_ENUM_AS_BIT_SET(FollowTrainReservationFlags)

bool ValidateLookAhead(const Train *v);
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res = nullptr, FollowTrainReservationFlags flags = FTRF_NONE);
void ApplyAvailableFreeTunnelBridgeTiles(TrainReservationLookAhead *lookahead, int free_tiles, TileIndex tile, TileIndex end);
void TryCreateLookAheadForTrainInTunnelBridge(Train *t);
void FillTrainReservationLookAhead(Train *v);
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg = false);
bool IsWaitingPositionFree(const Train *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg = false, PBSWaitingPositionRestrictedSignalInfo *restricted_signal_info = nullptr);

Train *GetTrainForReservation(TileIndex tile, Track track);
CommandCost CheckTrainReservationPreventsTrackModification(TileIndex tile, Track track);
CommandCost CheckTrainReservationPreventsTrackModification(const Train *v);
CommandCost CheckTrainInTunnelBridgePreventsTrackModification(TileIndex start, TileIndex end);

/**
 * Check whether some of tracks is reserved on a tile.
 *
 * @param tile the tile
 * @param tracks the tracks to test
 * @return true if at least on of tracks is reserved
 */
static inline bool HasReservedTracks(TileIndex tile, TrackBits tracks)
{
	return (GetReservedTrackbits(tile) & tracks) != TRACK_BIT_NONE;
}

#endif /* PBS_H */
