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
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
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
		MarkSingleSignalDirty(tile, td);
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
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
		} else {
			MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		}
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsPlainRail(tile)) return TryReserveTrack(tile, t);
			if (IsRailDepot(tile)) {
				if (!HasDepotReservation(tile)) {
					SetDepotReservation(tile, true);
					MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE); // some GRFs change their appearance when tile is reserved
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
				if (trigger_stations && IsRailStation(tile)) TriggerStationRandomisation(nullptr, tile, SRT_PATH_RESERVATION);
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE); // some GRFs need redraw after reserving track
				return true;
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile) && !HasTunnelReservation(tile)) {
					SetTunnelReservation(tile, true);
					MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
					return true;
				}
				if (IsBridge(tile)) {
					if (TryReserveRailBridgeHead(tile, t)) {
						MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
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
		MarkSingleSignalDirty(tile, td);
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
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
		} else {
			MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		}
	}

	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (IsRailDepot(tile)) {
				SetDepotReservation(tile, false);
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
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
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		case MP_TUNNELBRIDGE:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile)) {
					SetTunnelReservation(tile, false);
				} else {
					UnreserveRailBridgeHeadTrack(tile, t);
				}
				if (IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile) && IsTrackAcrossTunnelBridge(tile, t)) {
					if (IsTunnelBridgePBS(tile)) {
						SetTunnelBridgeExitSignalState(tile, SIGNAL_STATE_RED);
					} else {
						UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, GetTileOwner(tile));
					}
				}
				MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		default:
			break;
	}
}

/** Flags for FollowReservation */
enum FollowReservationFlags {
	FRF_NONE                 = 0,        ///< No flags
	FRF_IGNORE_ONEWAY        = 0x01,     ///< Ignore one way signals in the opposite direction
	FRF_TB_EXIT_FREE         = 0x02,     ///< Exit of starting tunnel/bridge is free
};
DECLARE_ENUM_AS_BIT_SET(FollowReservationFlags)

static void CheckCurveLookAhead(const Train *v, TrainReservationLookAhead *lookahead, int end_position, int z, RailType rt)
{
	/* Coarse filter: remove curves beyond train length */
	while (!lookahead->curves.empty() && lookahead->curves.front().position < end_position - v->gcache.cached_total_length) {
		lookahead->curves.pop_front();
	}

	if (lookahead->curves.empty() || v->Next() == nullptr) return;

	static const int absolute_max_speed = UINT16_MAX;
	int max_speed = absolute_max_speed;

	int curvecount[2] = {0, 0};

	/* first find the curve speed limit */
	int numcurve = 0;
	int sum = 0;
	int pos = 0;
	int lastpos = -1;
	const Train *u = v->Next();
	int veh_offset = v->CalcNextVehicleOffset();
	for (auto iter = lookahead->curves.rbegin(); iter != lookahead->curves.rend(); ++iter) {
		const TrainReservationLookAheadCurve &curve = *iter;
		int delta = end_position - curve.position;
		while (delta >= veh_offset) {
			if (u->Next() != nullptr) {
				veh_offset += u->CalcNextVehicleOffset();
				u = u->Next();
				pos++;
			} else {
				u = nullptr;
				break;
			}
		}
		if (u == nullptr) break;

		if (curve.dir_diff == DIRDIFF_45LEFT) curvecount[0]++;
		if (curve.dir_diff == DIRDIFF_45RIGHT) curvecount[1]++;
		if (curve.dir_diff == DIRDIFF_45LEFT || curve.dir_diff == DIRDIFF_45RIGHT) {
			if (lastpos != -1) {
				numcurve++;
				sum += pos - lastpos;
				if (pos - lastpos == 1 && max_speed > 88) {
					max_speed = 88;
				}
			}
			lastpos = pos;
		}

		/* if we have a 90 degree turn, fix the speed limit to 60 */
		if (curve.dir_diff == DIRDIFF_90LEFT || curve.dir_diff == DIRDIFF_90RIGHT) {
			max_speed = 61;
		}
	}

	if (numcurve > 0 && max_speed > 88) {
		if (curvecount[0] == 1 && curvecount[1] == 1) {
			max_speed = absolute_max_speed;
		} else {
			sum /= numcurve;
			max_speed = 232 - (13 - Clamp(sum, 1, 12)) * (13 - Clamp(sum, 1, 12));
		}
	}

	if (max_speed != absolute_max_speed) {
		/* Apply the engine's rail type curve speed advantage, if it slowed by curves */
		const RailtypeInfo *rti = GetRailTypeInfo(rt);
		max_speed += (max_speed / 2) * rti->curve_speed;

		if (v->tcache.cached_tflags & TCF_TILT) {
			/* Apply max_speed bonus of 20% for a tilting train */
			max_speed += max_speed / 5;
		}

		lookahead->AddCurveSpeedLimit(max_speed, 4, z);
	}
}

static int LookaheadTileHeightForChunnel(int length, int offset)
{
	if (offset == 0) return 0;
	if (offset < 3) return -1 * (int)TILE_HEIGHT;
	if (offset < length - 3) return -2 * (int)TILE_HEIGHT;
	if (offset < length) return -1 * (int)TILE_HEIGHT;
	return 0;
}

/** Follow a reservation starting from a specific tile to the end. */
static PBSTileInfo FollowReservation(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, FollowReservationFlags flags, const Train *v, TrainReservationLookAhead *lookahead)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!(flags & FRF_TB_EXIT_FREE) && !HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return PBSTileInfo(tile, trackdir, false);

	RailType rt = INVALID_RAILTYPE;
	Direction dir = INVALID_DIR;
	int z = 0;
	auto update_z = [&](TileIndex t, Trackdir td, bool force) {
		if (force || TrackdirToTrack(td) == TRACK_X || TrackdirToTrack(td) == TRACK_Y) {
			if (IsBridgeTile(t) && TrackdirToExitdir(td) == GetTunnelBridgeDirection(t)) {
				z = GetBridgePixelHeight(t);
			} else {
				int x = (TileX(t) * TILE_SIZE) + 8;
				int y = (TileY(t) * TILE_SIZE) + 8;
				if (!IsTunnelTile(tile)) {
					switch (TrackdirToExitdir(td)) {
						case DIAGDIR_NE: x -= 8; break;
						case DIAGDIR_SE: y += 7; break;
						case DIAGDIR_SW: x += 7; break;
						case DIAGDIR_NW: y -= 8; break;
						default: NOT_REACHED();
					}
				}
				z = GetSlopePixelZ(x, y);
			}
		}
	};

	if (lookahead != nullptr) {
		rt = GetRailTypeByTrack(tile, TrackdirToTrack(trackdir));
		dir = TrackdirToDirection(trackdir);
		update_z(tile, trackdir, true);
	}

	auto check_rail_type = [&](TileIndex t, Trackdir td, int offset) {
		RailType new_rt = GetRailTypeByTrack(t, TrackdirToTrack(td));
		if (new_rt != rt) {
			uint16 rail_speed = GetRailTypeInfo(new_rt)->max_speed;
			if (rail_speed > 0) lookahead->AddTrackSpeedLimit(rail_speed, offset, 4, z);
			if (GetRailTypeInfo(rt)->curve_speed != GetRailTypeInfo(new_rt)->curve_speed) {
				CheckCurveLookAhead(v, lookahead, lookahead->RealEndPosition() + 4 + offset, z, new_rt);
			}
			rt = new_rt;
		}
	};

	auto check_direction = [&](Direction new_dir, int offset, TileIndex tile) {
		if (dir == new_dir) return;
		DirDiff dirdiff = DirDifference(dir, new_dir);
		int end = lookahead->RealEndPosition() + 4;
		lookahead->curves.push_back({ end + offset, dirdiff });
		dir = new_dir;
		CheckCurveLookAhead(v, lookahead, end + offset, z, rt);
	};

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	auto check_tunnel_bridge = [&]() -> bool {
		if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && IsTunnelBridgeWithSignalSimulation(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntrance(tile)) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && GetTunnelBridgeExitSignalState(end) == SIGNAL_STATE_GREEN &&
						((flags & FRF_TB_EXIT_FREE) || TunnelBridgeIsFree(tile, end, nullptr, true).Succeeded())) {
					/* skip far end */
					if (lookahead != nullptr) {
						lookahead->reservation_end_position += (DistanceManhattan(tile, end) - 1) * TILE_SIZE;
					}
					Trackdir end_trackdir = TrackEnterdirToTrackdir(FindFirstTrack(GetAcrossTunnelBridgeTrackBits(end)), ReverseDiagDir(GetTunnelBridgeDirection(end)));
					if (lookahead != nullptr) {
						if ((flags & FRF_TB_EXIT_FREE) && GetTunnelBridgeLength(tile, end) > 1) {
							/* middle part of bridge is in wormhole direction */
							dir = DiagDirToDir(GetTunnelBridgeDirection(tile));
						}
						check_direction(TrackdirToDirection(end_trackdir), 0, end);
						lookahead->reservation_end_position += (IsDiagonalTrackdir(end_trackdir) ? 16 : 8);
						update_z(end, end_trackdir, false);
					}
					tile = end;
					trackdir = end_trackdir;
					return true;
				}
			}
			if ((flags & FRF_IGNORE_ONEWAY) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationExit(tile) &&
					GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && TunnelBridgeIsFree(tile, end, nullptr, true).Succeeded()) {
					/* skip far end */
					tile = end;
					trackdir = TrackEnterdirToTrackdir(FindFirstTrack(GetAcrossTunnelBridgeTrackBits(tile)), ReverseDiagDir(GetTunnelBridgeDirection(tile)));
					return true;
				}
			}
			return false;
		}
		return true;
	};
	while (check_tunnel_bridge() && ft.Follow(tile, trackdir)) {
		flags &= ~FRF_TB_EXIT_FREE;
		TrackdirBits reserved = ft.m_new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.m_new_tile));

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) {
			if (ft.m_is_station) {
				/* Check skipped station tiles as well, maybe our reservation ends inside the station. */
				TileIndexDiff diff = TileOffsByDiagDir(ft.m_exitdir);
				while (ft.m_tiles_skipped-- > 0) {
					ft.m_new_tile -= diff;
					if (HasStationReservation(ft.m_new_tile)) {
						if (lookahead != nullptr) {
							lookahead->AddStation(1 + ft.m_tiles_skipped, GetStationIndex(ft.m_new_tile), z);
							lookahead->reservation_end_position += (1 + ft.m_tiles_skipped) * TILE_SIZE;
						}
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
		if (!(flags & FRF_IGNORE_ONEWAY) && HasOnewaySignalBlockingTrackdir(ft.m_new_tile, new_trackdir)) break;

		tile = ft.m_new_tile;
		trackdir = new_trackdir;

		if (lookahead != nullptr) {
			if (ft.m_tiles_skipped > 0) {
				DiagDirection skip_dir = ReverseDiagDir(TrackdirToExitdir(ReverseTrackdir(trackdir)));
				check_direction(DiagDirToDir(skip_dir), 0, tile);
			}
			if (ft.m_is_station) {
				if (ft.m_tiles_skipped > 0) {
					TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(trackdir));
					TileIndex start = tile - (diff * ft.m_tiles_skipped);
					for (int i = 0; i < ft.m_tiles_skipped; i++) {
						check_rail_type(start, trackdir, i * TILE_SIZE);
						start += diff;
					}
				}
				check_rail_type(tile, trackdir, ft.m_tiles_skipped * TILE_SIZE);
				lookahead->AddStation(1 + ft.m_tiles_skipped, GetStationIndex(ft.m_new_tile), z);
			} else {
				check_rail_type(tile, trackdir, 0);
			}
			check_direction(TrackdirToDirection(trackdir), ft.m_tiles_skipped * TILE_SIZE, tile);
			if (IsTileType(tile, MP_TUNNELBRIDGE) && TrackdirEntersTunnelBridge(tile, trackdir)) {
				uint16 bridge_speed = 0;
				if (IsBridge(tile)) {
					bridge_speed = GetBridgeSpec(GetBridgeType(tile))->speed;
					lookahead->AddTrackSpeedLimit(bridge_speed, 0, 8, z);
				}
				const int start_offset = (IsDiagonalTrackdir(trackdir) ? 16 : 8);
				const TileIndex end = GetOtherTunnelBridgeEnd(tile);
				const int length = GetTunnelBridgeLength(tile, end);
				if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
					const int spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
					const int signals = length / spacing;

					uint16 signal_speed = GetRailTypeInfo(rt)->max_speed;
					if (signal_speed == 0 || (lookahead->speed_restriction != 0 && lookahead->speed_restriction < signal_speed)) signal_speed = lookahead->speed_restriction;
					if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;

					/* Entrance signal */
					lookahead->AddSignal(signal_speed, 0, z);

					update_z(tile, trackdir, false);

					if (length > 1) {
						check_direction(DiagDirToDir(GetTunnelBridgeDirection(tile)), start_offset, tile);
					}

					bool chunnel = IsTunnel(tile) && Tunnel::GetByTile(tile)->is_chunnel;

					/* Middle signals */
					int offset = start_offset - TILE_SIZE;
					for (int i = 0; i < signals; i++) {
						offset += TILE_SIZE * spacing;
						lookahead->AddSignal(signal_speed, offset, chunnel ? LookaheadTileHeightForChunnel(length, i * spacing) : z);
					}

					/* Exit signal */
					const int end_offset = start_offset + (TILE_SIZE * length) /* + ((DiagDirToDiagTrackBits(GetTunnelBridgeDirection(end)) & GetTunnelBridgeTrackBits(end)) ? 16 : 8)*/;
					lookahead->AddSignal(signal_speed, end_offset, z);
				} else {
					update_z(tile, trackdir, false);
					if (length > 1) {
						check_direction(DiagDirToDir(GetTunnelBridgeDirection(tile)), start_offset, tile);
					}
				}
			}

			if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrack(tile, TrackdirToTrack(trackdir))) {
				TraceRestrictProgramActionsUsedFlags au_flags;
				if (HasSignalOnTrackdir(tile, trackdir)) {
					/* Passing through a signal from the front side */
					au_flags = TRPAUF_SPEED_RESTRICTION;
				} else {
					/* Passing through a signal from the rear side */
					au_flags = TRPAUF_SPEED_RESTRICTION | TRPAUF_REVERSE;
				}
				uint16 speed_restriction = lookahead->speed_restriction;
				if (v != nullptr) {
					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
					if (prog && prog->actions_used_flags & au_flags) {
						TraceRestrictProgramResult out;
						TraceRestrictProgramInput input(tile, trackdir, nullptr, nullptr);
						prog->Execute(v, input, out);
						if (out.flags & TRPRF_REVERSE && au_flags & TRPAUF_REVERSE) {
							lookahead->AddReverse(z);
						}
						if (out.flags & TRPRF_SPEED_RETRICTION_SET) {
							lookahead->AddSpeedRestriction(out.speed_restriction, z);
							if (out.speed_restriction != 0 && (speed_restriction == 0 || out.speed_restriction < speed_restriction)) {
								/* lower of the speed restrictions before or after the signal */
								speed_restriction = out.speed_restriction;
							}
						}
					}
				}
				if (!(au_flags & TRPAUF_REVERSE)) {
					/* Passing through a signal from the front side */
					uint16 signal_speed = GetRailTypeInfo(rt)->max_speed;
					if (signal_speed == 0 || (speed_restriction != 0 && speed_restriction < signal_speed)) signal_speed = speed_restriction;
					lookahead->AddSignal(signal_speed, 0, z);
				}
			}

			lookahead->reservation_end_position += (IsDiagonalTrackdir(trackdir) ? 16 : 8) + (ft.m_tiles_skipped * 16);
			update_z(tile, trackdir, false);
		}

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
		if (IsRailDepotTile(tile)) {
			if (lookahead != nullptr) SetBit(lookahead->flags, TRLF_DEPOT_END);
			break;
		}
		/* Non-pbs signal? Reservation can't continue. */
		if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
	}

	if (lookahead != nullptr) lookahead->reservation_end_z = z;

	return PBSTileInfo(tile, trackdir, false);
}

/**
 * Helper struct for finding the best matching vehicle on a specific track.
 */
struct FindTrainOnTrackInfo {
	PBSTileInfo res; ///< Information about the track.
	Train *best;     ///< The currently "best" vehicle we have found.

	/** Init the best location to nullptr always! */
	FindTrainOnTrackInfo() : best(nullptr) {}
};

/** Callback for Has/FindVehicleOnPos to find a train on a specific track. */
static Vehicle *FindTrainOnTrackEnum(Vehicle *v, void *data)
{
	FindTrainOnTrackInfo *info = (FindTrainOnTrackInfo *)data;

	if ((v->vehstatus & VS_CRASHED)) return nullptr;

	Train *t = Train::From(v);
	if (t->track & TRACK_BIT_WORMHOLE) {
		/* Do not find trains inside signalled bridge/tunnels.
		 * Trains on the ramp/entrance itself are found though.
		 */
		if (IsTileType(info->res.tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(info->res.tile) && info->res.tile != TileVirtXY(t->x_pos, t->y_pos)) {
			return nullptr;
		}
	}
	if (t->track & TRACK_BIT_WORMHOLE || HasBit((TrackBits)t->track, TrackdirToTrack(info->res.trackdir))) {
		t = t->First();

		/* ALWAYS return the lowest ID (anti-desync!) */
		if (info->best == nullptr || t->index < info->best->index) info->best = t;
		return t;
	}

	return nullptr;
}

bool ValidateLookAhead(const Train *v)
{
	TileIndex tile = v->lookahead->reservation_end_tile;
	Trackdir trackdir = v->lookahead->reservation_end_trackdir;

	if (HasBit(v->lookahead->flags, TRLF_TB_EXIT_FREE)) {
		if (!likely(IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && TrackdirEntersTunnelBridge(tile, trackdir))) {
			return false;
		}
	}
	if (HasBit(v->lookahead->flags, TRLF_DEPOT_END) && !IsRailDepotTile(tile)) return false;

	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));
	if (!HasTrackdir(trackdirbits, trackdir)) return false;

	return true;
}

/**
 * Follow a train reservation to the last tile.
 *
 * @param v the vehicle
 * @param train_on_res Is set to a train we might encounter
 * @returns The last tile of the reservation or the current train tile if no reservation present.
 */
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res, FollowTrainReservationFlags flags)
{
	assert(v->type == VEH_TRAIN);

	TileIndex tile;
	Trackdir  trackdir;

	if (!(flags & FTRF_IGNORE_LOOKAHEAD) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->lookahead != nullptr) {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
		if (HasBit(v->lookahead->flags, TRLF_DEPOT_END)) return PBSTileInfo(tile, trackdir, false);
		if (HasBit(v->lookahead->flags, TRLF_TB_EXIT_FREE)) {
			TileIndex exit_tile = GetOtherTunnelBridgeEnd(tile);
			if (IsTunnelBridgeSignalSimulationExit(exit_tile) && GetTunnelBridgeExitSignalState(exit_tile) == SIGNAL_STATE_GREEN && HasAcrossTunnelBridgeReservation(exit_tile)) {
				tile = exit_tile;
				DiagDirection exit_dir = ReverseDiagDir(GetTunnelBridgeDirection(exit_tile));
				trackdir = TrackEnterdirToTrackdir(FindFirstTrack(GetAcrossTunnelBridgeTrackBits(exit_tile)), exit_dir);
			}
		}
	} else {
		tile = v->tile;
		trackdir = v->GetVehicleTrackdir();
	}

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) return PBSTileInfo(tile, trackdir, false);

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(v->owner, GetRailTypeInfo(v->railtype)->all_compatible_railtypes, tile, trackdir, FRF_NONE, v, nullptr);
	ftoti.res.okay = (flags & FTRF_OKAY_UNUSED) ? false : IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg);
	if (train_on_res != nullptr) {
		FindVehicleOnPos(ftoti.res.tile, VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
		if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		if (*train_on_res == nullptr && IsRailStationTile(ftoti.res.tile)) {
			/* The target tile is a rail station. The track follower
			 * has stopped on the last platform tile where we haven't
			 * found a train. Also check all previous platform tiles
			 * for a possible train. */
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; *train_on_res == nullptr && IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				FindVehicleOnPos(st_tile, VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
				if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
			}
		}
		if (*train_on_res == nullptr && IsTileType(ftoti.res.tile, MP_TUNNELBRIDGE) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir)) && !IsTunnelBridgeWithSignalSimulation(ftoti.res.tile)) {
			/* The target tile is a bridge/tunnel, also check the other end tile. */
			FindVehicleOnPos(GetOtherTunnelBridgeEnd(ftoti.res.tile), VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
			if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		}
	}
	return ftoti.res;
}

void ApplyAvailableFreeTunnelBridgeTiles(TrainReservationLookAhead *lookahead, int free_tiles, TileIndex tile, TileIndex end)
{
	SB(lookahead->flags, TRLF_TB_EXIT_FREE, 1, free_tiles == INT_MAX ? 1 : 0);
	if (free_tiles == INT_MAX) {
		/* whole tunnel/bridge is empty */
		if (unlikely(end == INVALID_TILE)) end = GetOtherTunnelBridgeEnd(tile);
		free_tiles = DistanceManhattan(tile, end) - 1;
	} else {
		if (free_tiles > 0) {
			int spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
			free_tiles = (((free_tiles - 1) / spacing) * spacing) - 1;
		} else {
			free_tiles = -1;
		}
	}
	lookahead->reservation_end_position += ((free_tiles - lookahead->tunnel_bridge_reserved_tiles) * TILE_SIZE);
	lookahead->tunnel_bridge_reserved_tiles = free_tiles;
	if (HasBit(lookahead->flags, TRLF_CHUNNEL)) {
		if (unlikely(end == INVALID_TILE)) end = GetOtherTunnelBridgeEnd(tile);
		lookahead->reservation_end_z = LookaheadTileHeightForChunnel(GetTunnelBridgeLength(tile, end), free_tiles + 1);
	}
}

void FillLookAheadCurveDataFromTrainPosition(Train *t)
{
	TileIndex tile = TileVirtXY(t->x_pos, t->y_pos);
	Direction dir = t->direction;
	int32 current_pos = t->lookahead->reservation_end_position + 4 - ((dir & 1) ? 16 : 8);
	for (Train *u = t->Next(); u != nullptr; u = u->Next()) {
		TileIndex cur_tile = TileVirtXY(u->x_pos, u->y_pos);
		if (cur_tile == tile) continue;
		tile = cur_tile;
		if (u->direction != dir) {
			DirDiff dirdiff = DirDifference(u->direction, dir);
			t->lookahead->curves.push_front({ current_pos, dirdiff });
			dir = u->direction;
		}
		current_pos -= ((dir & 1) ? 16 : 8);
	}
}

static int ScanTrainPositionForLookAheadStation(Train *t, TileIndex start_tile)
{
	StationID prev = INVALID_STATION;
	int offset = 0;
	int start_offset_tiles = 0;
	TileIndex cur_tile = start_tile;
	for (const Train *u = t; u != nullptr; u = u->Next()) {
		if (u != t) {
			TileIndex u_tile = TileVirtXY(u->x_pos, u->y_pos);
			if (u_tile != cur_tile) {
				offset += (IsDiagonalTrackdir(u->GetVehicleTrackdir()) ? 16 : 8);
				cur_tile = u_tile;
			}
		}
		if (HasStationTileRail(u->tile)) {
			StationID current = GetStationIndex(u->tile);
			if (current != prev) {
				/* Train is in a station, add that to the lookahead */
				TileIndex tile = u->tile;
				Trackdir trackdir = u->GetVehicleTrackdir();

				RailType rt = GetRailTypeByTrack(tile, TrackdirToTrack(trackdir));
				int z = GetTileMaxPixelZ(tile);

				DiagDirection forward_dir = TrackdirToExitdir(trackdir);
				TileIndexDiff diff = TileOffsByDiagDir(forward_dir);
				uint forward_length = BaseStation::GetByTile(tile)->GetPlatformLength(tile, forward_dir);
				uint reverse_length = BaseStation::GetByTile(tile)->GetPlatformLength(tile, ReverseDiagDir(forward_dir));

				if (u == t) {
					for (uint i = 1; i < forward_length; i++) {
						/* Check for mid platform rail type change */
						RailType new_rt = GetRailTypeByTrack(tile + (i * diff), TrackdirToTrack(trackdir));
						if (new_rt != rt) {
							uint16 rail_speed = GetRailTypeInfo(new_rt)->max_speed;
							if (rail_speed > 0) t->lookahead->AddTrackSpeedLimit(rail_speed, (i - 1) * TILE_SIZE, 4, z);
							rt = new_rt;
						}
					}
					start_offset_tiles = forward_length - 1;
				}

				t->lookahead->AddStation(forward_length - 1, current, z);
				t->lookahead->items.back().start -= offset + (reverse_length * TILE_SIZE);
				t->lookahead->items.back().end -= offset;

				prev = current;
			}
		} else {
			prev = INVALID_STATION;
		}
		if (!HasBit(u->flags, VRF_BEYOND_PLATFORM_END)) break;
	}
	return start_offset_tiles;
}

void TryCreateLookAheadForTrainInTunnelBridge(Train *t)
{
	if (IsTunnelBridgeSignalSimulationExitOnly(t->tile)) return;
	DiagDirection tb_dir = GetTunnelBridgeDirection(t->tile);
	if (DirToDiagDirAlongAxis(t->direction, DiagDirToAxis(tb_dir)) == tb_dir) {
		/* going in the right direction, allocate a new lookahead */
		t->lookahead.reset(new TrainReservationLookAhead());
		t->lookahead->reservation_end_tile = t->tile;
		t->lookahead->reservation_end_trackdir = TrackExitdirToTrackdir(FindFirstTrack(GetAcrossTunnelBridgeTrackBits(t->tile)), GetTunnelBridgeDirection(t->tile));
		t->lookahead->reservation_end_z = t->z_pos;
		t->lookahead->current_position = 0;
		t->lookahead->tunnel_bridge_reserved_tiles = DistanceManhattan(t->tile, TileVirtXY(t->x_pos, t->y_pos));
		t->lookahead->reservation_end_position = GetTileMarginInFrontOfTrain(t);
		t->lookahead->flags = 0;
		t->lookahead->speed_restriction = t->speed_restriction;
		if (IsTunnel(t->tile) && Tunnel::GetByTile(t->tile)->is_chunnel) SetBit(t->lookahead->flags, TRLF_CHUNNEL);

		if (IsTunnelBridgeSignalSimulationEntrance(t->tile)) {
			uint16 bridge_speed = IsBridge(t->tile) ? GetBridgeSpec(GetBridgeType(t->tile))->speed : 0;
			const int length = GetTunnelBridgeLength(t->tile, GetOtherTunnelBridgeEnd(t->tile));
			const int spacing = GetTunnelBridgeSignalSimulationSpacing(t->tile);
			const int signals = length / spacing;

			uint16 signal_speed = GetRailTypeInfo(GetRailTypeByTrack(t->tile, TrackdirToTrack(t->lookahead->reservation_end_trackdir)))->max_speed;
			if (signal_speed == 0 || (t->speed_restriction != 0 && t->speed_restriction < signal_speed)) signal_speed = t->speed_restriction;
			if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;

			int z = IsBridge(t->tile) ? GetBridgeHeight(t->tile) : GetTilePixelZ(t->tile);

			/* Middle signals */
			int offset = -(int)TILE_SIZE;
			for (int i = 0; i < signals; i++) {
				offset += TILE_SIZE * spacing;
				t->lookahead->AddSignal(signal_speed, offset, HasBit(t->lookahead->flags, TRLF_CHUNNEL) ? LookaheadTileHeightForChunnel(length, i * spacing) : z);
			}

			/* Exit signal */
			const int end_offset = TILE_SIZE * length;
			t->lookahead->AddSignal(signal_speed, end_offset, z);
		}

		FillLookAheadCurveDataFromTrainPosition(t);
		TileIndex end = GetOtherTunnelBridgeEnd(t->tile);
		int raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(t->tile, end, t->lookahead->tunnel_bridge_reserved_tiles + 1);
		ApplyAvailableFreeTunnelBridgeTiles(t->lookahead.get(), raw_free_tiles, t->tile, end);
		ScanTrainPositionForLookAheadStation(t, TileVirtXY(t->x_pos, t->y_pos));
	}
}

void FillTrainReservationLookAhead(Train *v)
{
	TileIndex tile;
	Trackdir  trackdir;

	if (v->lookahead == nullptr && (v->track & TRACK_BIT_WORMHOLE)) {
		TryCreateLookAheadForTrainInTunnelBridge(v);
		if (v->lookahead == nullptr) return;
	}

	if (v->lookahead == nullptr) {
		v->lookahead.reset(new TrainReservationLookAhead());
		v->lookahead->current_position = 0;

		/* Special case, if called from TrainController,
		 * v->tile, v->track and v->direction can be updated to the new tile,
		 * but v->x_pos and v->y_pos can still use the cordinates on the old tile,
		 * GetTileMarginInFrontOfTrain could erroneously return -5 if the old and
		 * new directions don't match. */
		v->lookahead->reservation_end_position = std::max(GetTileMarginInFrontOfTrain(v), -4);

		v->lookahead->tunnel_bridge_reserved_tiles = 0;
		v->lookahead->flags = 0;
		v->lookahead->speed_restriction = v->speed_restriction;
		FillLookAheadCurveDataFromTrainPosition(v);
		tile = v->tile;
		trackdir = v->GetVehicleTrackdir();
		TileIndex virt_tile = TileVirtXY(v->x_pos, v->y_pos);
		if (tile != virt_tile) {
			v->lookahead->reservation_end_position += (IsDiagonalDirection(v->direction) ? 16 : 8);
		}
		int station_offset_tiles = ScanTrainPositionForLookAheadStation(v, tile);
		if (station_offset_tiles > 0) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(trackdir));
			tile += station_offset_tiles * diff;
			v->lookahead->reservation_end_position += station_offset_tiles * TILE_SIZE;
		}
	} else {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
		if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && IsTunnelBridgeSignalSimulationEntrance(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
			TileIndex end = GetOtherTunnelBridgeEnd(tile);
			int raw_free_tiles;
			if (HasBit(v->lookahead->flags, TRLF_TB_EXIT_FREE)) {
				raw_free_tiles = INT_MAX;
			} else {
				raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(tile, end, v->lookahead->tunnel_bridge_reserved_tiles + 1);
				ApplyAvailableFreeTunnelBridgeTiles(v->lookahead.get(), raw_free_tiles, tile, end);
			}
			if (!(HasAcrossTunnelBridgeReservation(end) && GetTunnelBridgeExitSignalState(end) == SIGNAL_STATE_GREEN && raw_free_tiles == INT_MAX)) {
				/* do not attempt to follow through a signalled tunnel/bridge if it is not empty or the far end is not reserved */
				return;
			}
		}
	}

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) return;

	FollowReservationFlags flags = FRF_NONE;
	if (HasBit(v->lookahead->flags, TRLF_TB_EXIT_FREE)) flags |= FRF_TB_EXIT_FREE;
	PBSTileInfo res = FollowReservation(v->owner, GetRailTypeInfo(v->railtype)->all_compatible_railtypes, tile, trackdir, flags, v, v->lookahead.get());

	if (IsTunnelBridgeWithSignalSimulation(res.tile) && TrackdirEntersTunnelBridge(res.tile, res.trackdir)) {
		SB(v->lookahead->flags, TRLF_CHUNNEL, 1, (IsTunnel(res.tile) && Tunnel::GetByTile(res.tile)->is_chunnel) ? 1 : 0);
		if (v->lookahead->current_position < v->lookahead->reservation_end_position - ((int)TILE_SIZE * (1 + v->lookahead->tunnel_bridge_reserved_tiles))) {
			/* Vehicle is not itself in this tunnel/bridge, scan how much is available */
			TileIndex end = INVALID_TILE;
			int free_tiles;
			if (GetTunnelBridgeEntranceSignalState(res.tile) == SIGNAL_STATE_GREEN) {
				end = GetOtherTunnelBridgeEnd(res.tile);
				free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(res.tile, end, res.tile);
			} else {
				free_tiles = -1;
			}
			ApplyAvailableFreeTunnelBridgeTiles(v->lookahead.get(), free_tiles, res.tile, end);
		}
	} else {
		ClrBit(v->lookahead->flags, TRLF_TB_EXIT_FREE);
		ClrBit(v->lookahead->flags, TRLF_CHUNNEL);
		if (v->lookahead->tunnel_bridge_reserved_tiles != 0) {
			v->lookahead->reservation_end_position -= (v->lookahead->tunnel_bridge_reserved_tiles * (int)TILE_SIZE);
			v->lookahead->tunnel_bridge_reserved_tiles = 0;
		}
	}

	v->lookahead->reservation_end_tile = res.tile;
	v->lookahead->reservation_end_trackdir = res.trackdir;
}

/**
 * Find the train which has reserved a specific path.
 *
 * @param tile A tile on the path.
 * @param track A reserved track on the tile.
 * @return The vehicle holding the reservation or nullptr if the path is stray.
 */
Train *GetTrainForReservation(TileIndex tile, Track track)
{
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: %u", track);
	Trackdir  trackdir = TrackToTrackdir(track);

	RailTypes rts = GetRailTypeInfo(GetTileRailTypeByTrack(tile, track))->all_compatible_railtypes;

	/* Follow the path from tile to both ends, one of the end tiles should
	 * have a train on it. We need FollowReservation to ignore one-way signals
	 * here, as one of the two search directions will be the "wrong" way. */
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		/* If the tile has a one-way block signal in the current trackdir, skip the
		 * search in this direction as the reservation can't come from this side.*/
		if (HasOnewaySignalBlockingTrackdir(tile, ReverseTrackdir(trackdir)) && !HasPbsSignalOnTrackdir(tile, trackdir)) continue;

		FindTrainOnTrackInfo ftoti;
		ftoti.res = FollowReservation(GetTileOwner(tile), rts, tile, trackdir, FRF_IGNORE_ONEWAY, nullptr, nullptr);

		FindVehicleOnPos(ftoti.res.tile, VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
		if (ftoti.best != nullptr) return ftoti.best;

		/* Special case for stations: check the whole platform for a vehicle. */
		if (IsRailStationTile(ftoti.res.tile)) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				FindVehicleOnPos(st_tile, VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
				if (ftoti.best != nullptr) return ftoti.best;
			}
		}

		if (IsTileType(ftoti.res.tile, MP_TUNNELBRIDGE) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir))) {
			if (IsTunnelBridgeWithSignalSimulation(ftoti.res.tile)) {
				/* Special case for signalled bridges/tunnels: find best train on bridge/tunnel if exit reserved. */
				if (IsTunnelBridgeSignalSimulationExit(ftoti.res.tile) && !(IsTunnelBridgeEffectivelyPBS(ftoti.res.tile) && GetTunnelBridgeExitSignalState(ftoti.res.tile) == SIGNAL_STATE_RED)) {
					ftoti.best = GetTrainClosestToTunnelBridgeEnd(ftoti.res.tile, GetOtherTunnelBridgeEnd(ftoti.res.tile));
				}
			} else {
				/* Special case for bridges/tunnels: check the other end as well. */
				FindVehicleOnPos(GetOtherTunnelBridgeEnd(ftoti.res.tile), VEH_TRAIN, &ftoti, FindTrainOnTrackEnum);
			}
			if (ftoti.best != nullptr) return ftoti.best;
		}
	}

	return nullptr;
}

CommandCost CheckTrainReservationPreventsTrackModification(TileIndex tile, Track track)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		return CheckTrainReservationPreventsTrackModification(GetTrainForReservation(tile, track));
	}
	return CommandCost();
}

CommandCost CheckTrainReservationPreventsTrackModification(const Train *v)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && v != nullptr && v->UsingRealisticBraking() && (v->cur_speed > 0 || !(v->vehstatus & (VS_STOPPED | VS_CRASHED)))) {
		return_cmd_error(STR_ERROR_CANNOT_MODIFY_TRACK_TRAIN_APPROACHING);
	}
	return CommandCost();
}

static Vehicle *TrainInTunnelBridgePreventsTrackModificationEnum(Vehicle *v, void *)
{
	if (CheckTrainReservationPreventsTrackModification(Train::From(v)->First()).Failed()) return v;

	return nullptr;
}

CommandCost CheckTrainInTunnelBridgePreventsTrackModification(TileIndex start, TileIndex end)
{
	if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC) return CommandCost();

	if (HasVehicleOnPos(start, VEH_TRAIN, nullptr, &TrainInTunnelBridgePreventsTrackModificationEnum) ||
			HasVehicleOnPos(end, VEH_TRAIN, nullptr, &TrainInTunnelBridgePreventsTrackModificationEnum)) {
		return_cmd_error(STR_ERROR_CANNOT_MODIFY_TRACK_TRAIN_APPROACHING);
	}
	return CommandCost();
}

/**
 * This is called to retrieve the previous signal, as required
 * This is not run all the time as it is somewhat expensive and most restrictions will not test for the previous signal
 */
TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode)
{
	TileIndex tile;
	Trackdir  trackdir;

	if (mode == TRPESAF_RES_END && v->lookahead != nullptr) {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
	} else {
		if (IsRailDepotTile(v->tile)) {
			return v->tile;
		}
		if (v->track & TRACK_BIT_WORMHOLE && IsTileType(v->tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(v->tile) && IsTunnelBridgeEffectivelyPBS(v->tile)) {
			return v->tile;
		}
		tile = v->tile;
		trackdir = v->GetVehicleTrackdir();
	}

	// scan forwards from vehicle position, for the case that train is waiting at/approaching PBS signal

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

		if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile) && TrackdirExitsTunnelBridge(tile, trackdir)) {
			return tile;
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

	if (IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL && IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) {
		if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
			return true;
		}
	}

	/* Check next tile. For performance reasons, we check for 90 degree turns ourself. */
	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->all_compatible_railtypes);

	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (ft.m_tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.m_old_tile, ft.m_new_tile, ft.m_old_td, forbid_90deg)) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);
	if (ft.m_new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;

	if (ft.m_new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.m_new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.m_new_td_bits);
		/* PBS signal on next trackdir? Conditionally safe position. */
		if (HasPbsSignalOnTrackdir(ft.m_new_tile, td)) {
			if (IsRestrictedSignal(ft.m_new_tile)) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(ft.m_new_tile, TrackdirToTrack(td));
				if (prog && prog->actions_used_flags & TRPAUF_RESERVE_THROUGH) {
					TraceRestrictProgramResult out;
					prog->Execute(v, TraceRestrictProgramInput(ft.m_new_tile, td, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr), out);
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
				IsTunnelBridgeSignalSimulationExitOnly(ft.m_new_tile) && IsTunnelBridgeEffectivelyPBS(ft.m_new_tile)) {
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
				TraceRestrictProgramInput input(t, td, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
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
			if (HasVehicleOnPos(other_end, VEH_TRAIN, &dir, [](Vehicle *v, void *data) -> Vehicle * {
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
	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->all_compatible_railtypes);

	if (!ft.Follow(tile, trackdir)) return true;

	/* Check for reachable tracks. */
	ft.m_new_td_bits &= DiagdirReachesTrackdirs(ft.m_exitdir);
	if (Rail90DegTurnDisallowedTilesFromTrackdir(ft.m_old_tile, ft.m_new_tile, ft.m_old_td, forbid_90deg)) ft.m_new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);

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
