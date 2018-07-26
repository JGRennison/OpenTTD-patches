/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pbs.cpp PBS support routines */

#include "stdafx.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "newgrf_station.h"
#include "pathfinder/follow_track.hpp"
#include "tracerestrict.h"

#include "safeguards.h"

/**
 * Get the reserved trackbits for any tile, regardless of type.
 * @param t the tile
 * @return the reserved trackbits. TRACK_BIT_NONE on nothing reserved or
 *     a tile without rail.
 */
TrackBits GetReservedTrackbits(TileIndex t)
{
	switch (GetTileType(t)) {
		case MP_RAILWAY:
			if (IsRailDepot(t)) return GetDepotReservationTrackBits(t);
			if (IsPlainRail(t)) return GetRailReservationTrackBits(t);
			break;

		case MP_ROAD:
			if (IsLevelCrossing(t)) return GetCrossingReservationTrackBits(t);
			break;

		case MP_STATION:
			if (HasStationRail(t)) return GetStationReservationTrackBits(t);
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) return GetTunnelBridgeReservationTrackBits(t);
			break;

		default:
			break;
	}
	return TRACK_BIT_NONE;
}

/**
 * Set the reservation for a complete station platform.
 * @pre IsRailStationTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert_tile(IsRailStationTile(start), start);
	assert_tile(GetRailStationAxis(start) == DiagDirToAxis(dir), start);

	do {
		SetRailStationReservation(tile, b);
		MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
		tile = TILE_ADD(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}

/**
 * Try to reserve a specific track on a tile
 * This also sets PBS signals to green if reserving through the facing track direction
 * @param tile the tile
 * @param t the track
 * @param trigger_stations whether to call station randomisation trigger
 * @return \c true if reservation was successful, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 */
bool TryReserveRailTrackdir(TileIndex tile, Trackdir td, bool trigger_stations)
{
	bool success = TryReserveRailTrack(tile, TrackdirToTrack(td), trigger_stations);
	if (success && HasPbsSignalOnTrackdir(tile, td)) {
		SetSignalStateByTrackdir(tile, td, SIGNAL_STATE_GREEN);
	}
	return success;
}

/**
 * Try to reserve a specific track on a tile
 * @param tile the tile
 * @param t the track
 * @param trigger_stations whether to call station randomisation trigger
 * @return \c true if reservation was successful, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 */
bool TryReserveRailTrack(TileIndex tile, Track t, bool trigger_stations)
{
	assert_msg_tile((TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(t)) != 0, tile,
			"%X, %X, %X", TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)), t, TrackToTrackBits(t));

	if (_settings_client.gui.show_track_reservation) {
		/* show the reserved rail if needed */
		if (IsBridgeTile(tile)) {
			MarkBridgeDirty(tile, ZOOM_LVL_DRAW_MAP);
		} else {
			MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
		}
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsPlainRail(tile)) return TryReserveTrack(tile, t);
			if (IsRailDepot(tile)) {
				if (!HasDepotReservation(tile)) {
					SetDepotReservation(tile, true);
					MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP); // some GRFs change their appearance when tile is reserved
					return true;
				}
			}
			break;

		case MP_ROAD:
			if (IsLevelCrossing(tile) && !HasCrossingReservation(tile)) {
				if (_settings_game.vehicle.safer_crossings) {
					if (IsCrossingOccupiedByRoadVehicle(tile)) return false;
					if (_settings_game.vehicle.adjacent_crossings) {
						const Axis axis = GetCrossingRoadAxis(tile);
						for (TileIndex t = tile; IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, AxisToDiagDir(GetCrossingRoadAxis(t)))) {
							if (IsCrossingOccupiedByRoadVehicle(t)) return false;
						}
						for (TileIndex t = tile; IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, ReverseDiagDir(AxisToDiagDir(GetCrossingRoadAxis(t))))) {
							if (IsCrossingOccupiedByRoadVehicle(t)) return false;
						}
					}
				}
				SetCrossingReservation(tile, true);
				UpdateLevelCrossing(tile, false);
				return true;
			}
			break;

		case MP_STATION:
			if (HasStationRail(tile) && !HasStationReservation(tile)) {
				SetRailStationReservation(tile, true);
				if (trigger_stations && IsRailStation(tile)) TriggerStationRandomisation(NULL, tile, SRT_PATH_RESERVATION);
				MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP); // some GRFs need redraw after reserving track
				return true;
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile) && !HasTunnelReservation(tile)) {
					SetTunnelReservation(tile, true);
					MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
					return true;
				}
				if (IsBridge(tile)) {
					if (TryReserveRailBridgeHead(tile, t)) {
						MarkBridgeOrTunnelDirtyOnReservationChange(tile, ZOOM_LVL_DRAW_MAP);
						return true;
					}
				}
			}
			break;

		default:
			break;
	}
	return false;
}

/**
 * Lift the reservation of a specific trackdir on a tile
 * This also sets PBS signals to red if unreserving through the facing track direction
 * @param tile the tile
 * @param t the track
 */
void UnreserveRailTrackdir(TileIndex tile, Trackdir td)
{
	if (HasPbsSignalOnTrackdir(tile, td)) {
		SetSignalStateByTrackdir(tile, td, SIGNAL_STATE_RED);
	}
	UnreserveRailTrack(tile, TrackdirToTrack(td));
}

/**
 * Lift the reservation of a specific track on a tile
 * @param tile the tile
 * @param t the track
 */
void UnreserveRailTrack(TileIndex tile, Track t)
{
	assert_msg_tile(TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(t), tile, "track: %u", t);

	if (_settings_client.gui.show_track_reservation) {
		if (IsBridgeTile(tile)) {
			MarkBridgeDirty(tile, ZOOM_LVL_DRAW_MAP);
		} else {
			MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
		}
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsRailDepot(tile)) {
				SetDepotReservation(tile, false);
				MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
				break;
			}
			if (IsPlainRail(tile)) UnreserveTrack(tile, t);
			break;

		case MP_ROAD:
			if (IsLevelCrossing(tile)) {
				SetCrossingReservation(tile, false);
				UpdateLevelCrossing(tile);
			}
			break;

		case MP_STATION:
			if (HasStationRail(tile)) {
				SetRailStationReservation(tile, false);
				MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile)) {
					SetTunnelReservation(tile, false);
				} else {
					UnreserveRailBridgeHeadTrack(tile, t);
				}
				if (IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgePBS(tile) && IsTrackAcrossTunnelBridge(tile, t)) SetTunnelBridgeExitSignalState(tile, SIGNAL_STATE_RED);
				MarkBridgeOrTunnelDirtyOnReservationChange(tile, ZOOM_LVL_DRAW_MAP);
			}
			break;

		default:
			break;
	}
}


/** Follow a reservation starting from a specific tile to the end. */
static PBSTileInfo FollowReservation(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, bool ignore_oneway = false)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return PBSTileInfo(tile, trackdir, false);

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	while (ft.Follow(tile, trackdir)) {
		TrackdirBits reserved = ft.m_new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.m_new_tile));

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) {
			if (ft.m_is_station) {
				/* Check skipped station tiles as well, maybe our reservation ends inside the station. */
				TileIndexDiff diff = TileOffsByDiagDir(ft.m_exitdir);
				while (ft.m_tiles_skipped-- > 0) {
					ft.m_new_tile -= diff;
					if (HasStationReservation(ft.m_new_tile)) {
						tile = ft.m_new_tile;
						trackdir = DiagDirToDiagTrackdir(ft.m_exitdir);
						break;
					}
				}
			}
			break;
		}

		/* Can't have more than one reserved trackdir */
		Trackdir new_trackdir = FindFirstTrackdir(reserved);

		/* One-way signal against us. The reservation can't be ours as it is not
		 * a safe position from our direction and we can never pass the signal. */
		if (!ignore_oneway && HasOnewaySignalBlockingTrackdir(ft.m_new_tile, new_trackdir)) break;

		tile = ft.m_new_tile;
		trackdir = new_trackdir;

		if (first_loop) {
			/* Update the start tile after we followed the track the first
			 * time. This is necessary because the track follower can skip
			 * tiles (in stations for example) which means that we might
			 * never visit our original starting tile again. */
			start_tile = tile;
			start_trackdir = trackdir;
			first_loop = false;
		} else {
			/* Loop encountered? */
			if (tile == start_tile && trackdir == start_trackdir) break;
		}
		/* Depot tile? Can't continue. */
		if (IsRailDepotTile(tile)) break;
		/* Non-pbs signal? Reservation can't continue. */
		if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
		if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(tile) && IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) break;
	}

	return PBSTileInfo(tile, trackdir, false);
}

/**
 * Helper struct for finding the best matching vehicle on a specific track.
 */
struct FindTrainOnTrackInfo {
	PBSTileInfo res; ///< Information about the track.
	Train *best;     ///< The currently "best" vehicle we have found.

	/** Init the best location to NULL always! */
	FindTrainOnTrackInfo() : best(NULL) {}
};

/** Callback for Has/FindVehicleOnPos to find a train on a specific track. */
static Vehicle *FindTrainOnTrackEnum(Vehicle *v, void *data)
{
	FindTrainOnTrackInfo *info = (FindTrainOnTrackInfo *)data;

	if (v->type != VEH_TRAIN || (v->vehstatus & VS_CRASHED)) return NULL;

	Train *t = Train::From(v);
	if (t->track & TRACK_BIT_WORMHOLE) {
		/* Do not find trains inside signalled bridge/tunnels.
		 * Trains on the ramp/entrance itself are found though.
		 */
		if (IsTileType(info->res.tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(info->res.tile) && info->res.tile != TileVirtXY(t->x_pos, t->y_pos)) {
			return NULL;
		}
	}
	if (t->track & TRACK_BIT_WORMHOLE || HasBit((TrackBits)t->track, TrackdirToTrack(info->res.trackdir))) {
		t = t->First();

		/* ALWAYS return the lowest ID (anti-desync!) */
		if (info->best == NULL || t->index < info->best->index) info->best = t;
		return t;
	}

	return NULL;
}

/**
 * Follow a train reservation to the last tile.
 *
 * @param v the vehicle
 * @param train_on_res Is set to a train we might encounter
 * @returns The last tile of the reservation or the current train tile if no reservation present.
 */
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res)
{
	assert(v->type == VEH_TRAIN);

	TileIndex tile = v->tile;
	Trackdir  trackdir = v->GetVehicleTrackdir();

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) return PBSTileInfo(tile, trackdir, false);

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(v->owner, GetRailTypeInfo(v->railtype)->compatible_railtypes, tile, trackdir);
	ftoti.res.okay = IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg);
	if (train_on_res != NULL) {
		FindVehicleOnPos(ftoti.res.tile, &ftoti, FindTrainOnTrackEnum);
		if (ftoti.best != NULL) *train_on_res = ftoti.best->First();
		if (*train_on_res == NULL && IsRailStationTile(ftoti.res.tile)) {
			/* The target tile is a rail station. The track follower
			 * has stopped on the last platform tile where we haven't
			 * found a train. Also check all previous platform tiles
			 * for a possible train. */
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; *train_on_res == NULL && IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				FindVehicleOnPos(st_tile, &ftoti, FindTrainOnTrackEnum);
				if (ftoti.best != NULL) *train_on_res = ftoti.best->First();
			}
		}
		if (*train_on_res == NULL && IsTileType(ftoti.res.tile, MP_TUNNELBRIDGE) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir)) && !IsTunnelBridgeWithSignalSimulation(ftoti.res.tile)) {
			/* The target tile is a bridge/tunnel, also check the other end tile. */
			FindVehicleOnPos(GetOtherTunnelBridgeEnd(ftoti.res.tile), &ftoti, FindTrainOnTrackEnum);
			if (ftoti.best != NULL) *train_on_res = ftoti.best->First();
		}
	}
	return ftoti.res;
}

/**
 * Find the train which has reserved a specific path.
 *
 * @param tile A tile on the path.
 * @param track A reserved track on the tile.
 * @return The vehicle holding the reservation or NULL if the path is stray.
 */
Train *GetTrainForReservation(TileIndex tile, Track track)
{
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: %u", track);
	Trackdir  trackdir = TrackToTrackdir(track);

	RailTypes rts = GetRailTypeInfo(GetTileRailType(tile))->compatible_railtypes;

	/* Follow the path from tile to both ends, one of the end tiles should
	 * have a train on it. We need FollowReservation to ignore one-way signals
	 * here, as one of the two search directions will be the "wrong" way. */
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		/* If the tile has a one-way block signal in the current trackdir, skip the
		 * search in this direction as the reservation can't come from this side.*/
		if (HasOnewaySignalBlockingTrackdir(tile, ReverseTrackdir(trackdir)) && !HasPbsSignalOnTrackdir(tile, trackdir)) continue;

		FindTrainOnTrackInfo ftoti;
		ftoti.res = FollowReservation(GetTileOwner(tile), rts, tile, trackdir, true);

		FindVehicleOnPos(ftoti.res.tile, &ftoti, FindTrainOnTrackEnum);
		if (ftoti.best != NULL) return ftoti.best;

		/* Special case for stations: check the whole platform for a vehicle. */
		if (IsRailStationTile(ftoti.res.tile)) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				FindVehicleOnPos(st_tile, &ftoti, FindTrainOnTrackEnum);
				if (ftoti.best != NULL) return ftoti.best;
			}
		}

		/* Special case for bridges/tunnels: check the other end as well. */
		if (IsTileType(ftoti.res.tile, MP_TUNNELBRIDGE) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir))) {
			FindVehicleOnPos(GetOtherTunnelBridgeEnd(ftoti.res.tile), &ftoti, FindTrainOnTrackEnum);
			if (ftoti.best != NULL) return ftoti.best;
		}
	}

	return NULL;
}

/**
 * This is called to retrieve the previous signal, as required
 * This is not run all the time as it is somewhat expensive and most restrictions will not test for the previous signal
 */
TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *)
{
	if (IsRailDepotTile(v->tile)) {
		return v->tile;
	}

	// scan forwards from vehicle position, for the case that train is waiting at/approaching PBS signal

	TileIndex tile = v->tile;
	Trackdir  trackdir = v->GetVehicleTrackdir();

	CFollowTrackRail ft(v);

	for (;;) {
		if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir)) {
			if (HasPbsSignalOnTrackdir(tile, trackdir)) {
				// found PBS signal
				return tile;
			} else {
				// wrong type of signal
				return INVALID_TILE;
			}
		}

		// advance to next tile
		if (!ft.Follow(tile, trackdir)) {
			// ran out of track
			return INVALID_TILE;
		}

		if (KillFirstBit(ft.m_new_td_bits) != TRACKDIR_BIT_NONE) {
			// reached a junction tile
			return INVALID_TILE;
		}

		tile = ft.m_new_tile;
		trackdir = FindFirstTrackdir(ft.m_new_td_bits);
	}
}

/**
 * Determine whether a certain track on a tile is a safe position to end a path.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param include_line_end Should end-of-line tiles be considered safe?
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @return True if it is a safe position
 */
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg)
{
	if (IsRailDepotTile(tile)) return true;

	if (IsTileType(tile, MP_RAILWAY)) {
		/* For non-pbs signals, stop on the signal tile. */
		if (HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return true;
	}

	if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL &&  IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) {
		if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
			return true;
		}
	}

	/* Check next tile. For performance reasons, we check for 90 degree turns ourself. */
	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->compatible_railtypes);

	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (forbid_90deg && ft.m_tiles_skipped == 0) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);
	if (ft.m_new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;

	if (ft.m_new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.m_new_td_bits);
		/* PBS signal on next trackdir? Conditionally safe position. */
		if (HasPbsSignalOnTrackdir(ft.m_new_tile, td)) {
			if (IsRestrictedSignal(ft.m_new_tile)) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(ft.m_new_tile, TrackdirToTrack(td));
				if (prog && prog->actions_used_flags & TRPAUF_RESERVE_THROUGH) {
					TraceRestrictProgramResult out;
					prog->Execute(v, TraceRestrictProgramInput(ft.m_new_tile, td, &VehiclePosTraceRestrictPreviousSignalCallback, NULL), out);
					if (out.flags & TRPRF_RESERVE_THROUGH) {
						return false;
					}
				}
			}
			return true;
		}
		/* One-way PBS signal against us? Safe if end-of-line is allowed. */
		if (IsTileType(ft.m_new_tile, MP_RAILWAY) && HasSignalOnTrackdir(ft.m_new_tile, ReverseTrackdir(td)) &&
				GetSignalType(ft.m_new_tile, TrackdirToTrack(td)) == SIGTYPE_PBS_ONEWAY) {
			return include_line_end;
		}
		if (IsTileType(ft.m_new_tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(ft.m_new_tile) == TRANSPORT_RAIL &&
				IsTrackAcrossTunnelBridge(ft.m_new_tile, TrackdirToTrack(td)) &&
				IsTunnelBridgeSignalSimulationExitOnly(ft.m_new_tile) && IsTunnelBridgePBS(ft.m_new_tile)) {
			return include_line_end;
		}
	}

	return false;
}

/**
 * Check if a safe position is free.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @return True if the position is free
 */
bool IsWaitingPositionFree(const Train *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg, PBSWaitingPositionRestrictedSignalInfo *restricted_signal_info)
{
	Track     track = TrackdirToTrack(trackdir);
	TrackBits reserved = GetReservedTrackbits(tile);

	/* Tile reserved? Can never be a free waiting position. */
	if (TrackOverlapsTracks(reserved, track)) return false;

	/* Not reserved and depot or not a pbs signal -> free. */
	if (IsRailDepotTile(tile)) return true;

	auto pbs_res_end_wait_test = [v, restricted_signal_info](TileIndex t, Trackdir td) -> bool {
		if (IsRestrictedSignal(t)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(t, TrackdirToTrack(td));
			if (restricted_signal_info && prog) {
				restricted_signal_info->tile = t;
				restricted_signal_info->trackdir = td;
			}
			if (prog && prog->actions_used_flags & TRPAUF_PBS_RES_END_WAIT) {
				TraceRestrictProgramInput input(t, td, &VehiclePosTraceRestrictPreviousSignalCallback, NULL);
				input.permitted_slot_operations = TRPISP_PBS_RES_END_ACQ_DRY;
				TraceRestrictProgramResult out;
				prog->Execute(v, input, out);
				if (out.flags & TRPRF_PBS_RES_END_WAIT) {
					return false;
				}
			}
		}
		return true;
	};

	if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, track))) {
		return pbs_res_end_wait_test(tile, trackdir);
	}

	if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && IsTunnelBridgeSignalSimulationEntrance(tile)
			&& IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) {
		if (IsTunnelBridgeSignalSimulationBidirectional(tile)) {
			TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
			if (HasAcrossTunnelBridgeReservation(other_end) && GetTunnelBridgeExitSignalState(other_end) == SIGNAL_STATE_RED) return false;
			Direction dir = DiagDirToDir(GetTunnelBridgeDirection(other_end));
			if (HasVehicleOnPos(other_end, &dir, [](Vehicle *v, void *data) -> Vehicle * {
				if (v->type != VEH_TRAIN) return nullptr;
				DirDiff diff = DirDifference(v->direction, *((Direction *) data));
				if (diff == DIRDIFF_SAME) return v;
				if (diff == DIRDIFF_45RIGHT || diff == DIRDIFF_45LEFT) {
					if (GetAcrossTunnelBridgeTrackBits(v->tile) & Train::From(v)->track) return v;
				}
				return nullptr;
			})) return false;
		}
		return true;
	}

	/* Check the next tile, if it's a PBS signal, it has to be free as well. */
	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->compatible_railtypes);

	if (!ft.Follow(tile, trackdir)) return true;

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (forbid_90deg) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);

	if (HasReservedTracks(ft.m_new_tile, TrackdirBitsToTrackBits(ft.m_new_td_bits))) return false;

	if (ft.m_new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.m_new_td_bits);
		/* PBS signal on next trackdir? */
		if (HasPbsSignalOnTrackdir(ft.m_new_tile, td)) {
			return pbs_res_end_wait_test(ft.m_new_tile, td);
		}
	}

	return true;
}
