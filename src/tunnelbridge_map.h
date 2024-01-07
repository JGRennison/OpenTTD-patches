/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnelbridge_map.h Functions that have tunnels and bridges in common */

#ifndef TUNNELBRIDGE_MAP_H
#define TUNNELBRIDGE_MAP_H

#include "bridge_map.h"
#include "tunnel_base.h"
#include "cmd_helper.h"
#include "signal_type.h"
#include "tunnel_map.h"
#include "track_func.h"
#include "settings_type.h"
#include "vehicle_type.h"
#include "core/bitmath_func.hpp"


/**
 * Get the direction pointing to the other end.
 *
 * Tunnel: Get the direction facing into the tunnel
 * Bridge: Get the direction pointing onto the bridge
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return the above mentioned direction
 */
inline DiagDirection GetTunnelBridgeDirection(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return (DiagDirection)GB(_m[t].m5, 0, 2);
}

/**
 * Tunnel: Get the transport type of the tunnel (road or rail)
 * Bridge: Get the transport type of the bridge's ramp
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return the transport type in the tunnel/bridge
 */
inline TransportType GetTunnelBridgeTransportType(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return (TransportType)GB(_m[t].m5, 2, 2);
}

inline uint8 GetTunnelBridgeGroundBits(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return GB(_me[t].m7, 5, 3);
}

/**
 * Tunnel: Is this tunnel entrance in a snowy or desert area?
 * Bridge: Does the bridge ramp lie in a snow or desert area?
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if the tile is in a snowy/desert area
 */
inline bool HasTunnelBridgeSnowOrDesert(TileIndex t)
{
	return GetTunnelBridgeGroundBits(t) == 1;
}

/**
* Is this a rail bridge or tunnel?
* @param t the tile that might be a rail bridge or tunnel
* @return true if and only if this tile is a rail bridge or tunnel
*/
inline bool IsRailTunnelBridgeTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && (Extract<TransportType, 2, 2>(_m[t].m5) == TRANSPORT_RAIL);
}

inline void SetTunnelBridgeGroundBits(TileIndex t, uint8 bits)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	SB(_me[t].m7, 5, 3, bits);
}

/**
 * Tunnel: Places this tunnel entrance in a snowy or desert area, or takes it out of there.
 * Bridge: Sets whether the bridge ramp lies in a snow or desert area.
 * @param t the tunnel entrance / bridge ramp tile
 * @param snow_or_desert is the entrance/ramp in snow or desert (true), when
 *                       not in snow and not in desert false
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 */
inline void SetTunnelBridgeSnowOrDesert(TileIndex t, bool snow_or_desert)
{
	SetTunnelBridgeGroundBits(t, snow_or_desert ? 1 : 0);
}

/**
 * Determines type of the wormhole and returns its other end
 * @param t one end
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return other end
 */
inline TileIndex GetOtherTunnelBridgeEnd(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return IsTunnel(t) ? GetOtherTunnelEnd(t) : GetOtherBridgeEnd(t);
}

/**
 * Get the track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetTunnelBridgeTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t));
	} else {
		return GetCustomBridgeHeadTrackBits(t);
	}
}

/**
 * Get the primary railtype track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetPrimaryTunnelBridgeTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t));
	} else {
		TrackBits bits = GetCustomBridgeHeadTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return bits & GetAcrossBridgePossibleTrackBits(t);
		} else {
			return bits;
		}
	}
}

/**
 * Get the secondary railtype track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetSecondaryTunnelBridgeTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return TRACK_BIT_NONE;
	} else {
		TrackBits bits = GetCustomBridgeHeadTrackBits(t);
		if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) {
			return bits & (~GetAcrossBridgePossibleTrackBits(t));
		} else {
			return TRACK_BIT_NONE;
		}
	}
}

/**
 * Get the track bits for a rail tunnel/bridge onto/across the tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetAcrossTunnelBridgeTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t));
	} else {
		return GetCustomBridgeHeadTrackBits(t) & GetAcrossBridgePossibleTrackBits(t);
	}
}

/**
 * Get the reserved track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetTunnelBridgeReservationTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return HasTunnelReservation(t) ? DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t)) : TRACK_BIT_NONE;
	} else {
		return GetBridgeReservationTrackBits(t);
	}
}

/**
 * Get the reserved track bits for a rail tunnel/bridge onto/across the tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetAcrossTunnelBridgeReservationTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return HasTunnelReservation(t) ? DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t)) : TRACK_BIT_NONE;
	} else {
		return GetAcrossBridgeReservationTrackBits(t);
	}
}

/**
 * Get whether there are reserved track bits for a rail tunnel/bridge onto/across the tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return whether there are reserved track bits
 */
inline bool HasAcrossTunnelBridgeReservation(TileIndex t)
{
	if (IsTunnel(t)) {
		return HasTunnelReservation(t);
	} else {
		return GetAcrossBridgeReservationTrackBits(t) != TRACK_BIT_NONE;
	}
}

/**
 * Get the rail infrastructure count of a rail tunnel/bridge head tile (excluding the tunnel/bridge middle)
 * @param bits the track bits
 * @return rail infrastructure count
 */
inline uint GetTunnelBridgeHeadOnlyRailInfrastructureCountFromTrackBits(TrackBits bits)
{
	uint pieces = CountBits(bits);
	if (TracksOverlap(bits)) pieces *= pieces;
	return (TUNNELBRIDGE_TRACKBIT_FACTOR / 2) * (1 + pieces);
}

/**
 * Get the primary railtype rail infrastructure count of a rail tunnel/bridge head tile (excluding the tunnel/bridge middle)
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return rail infrastructure count
 */
inline uint GetTunnelBridgeHeadOnlyPrimaryRailInfrastructureCount(TileIndex t)
{
	return IsBridge(t) ? GetTunnelBridgeHeadOnlyRailInfrastructureCountFromTrackBits(GetPrimaryTunnelBridgeTrackBits(t)) : TUNNELBRIDGE_TRACKBIT_FACTOR;
}

/**
 * Get the secondary railtype rail infrastructure count of a rail tunnel/bridge head tile (excluding the tunnel/bridge middle)
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return rail infrastructure count
 */
inline uint GetTunnelBridgeHeadOnlySecondaryRailInfrastructureCount(TileIndex t)
{
	return IsBridge(t) && GetSecondaryTunnelBridgeTrackBits(t) ? (TUNNELBRIDGE_TRACKBIT_FACTOR / 2) : 0;
}

/**
 * Check if the given track direction on a rail bridge head tile enters the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @param td track direction
 * @return reservation state
 */
inline bool TrackdirEntersTunnelBridge(TileIndex t, Trackdir td)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	assert_tile(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL, t);
	return TrackdirToExitdir(td) == GetTunnelBridgeDirection(t);
}

/**
 * Check if the given track direction on a rail bridge head tile exits the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @param td track direction
 * @return reservation state
 */
inline bool TrackdirExitsTunnelBridge(TileIndex t, Trackdir td)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	assert_tile(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL, t);
	return TrackdirToExitdir(ReverseTrackdir(td)) == GetTunnelBridgeDirection(t);
}

/**
 * Check if the given track on a rail bridge head tile enters/exits the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param tile the tile
 * @param t track
 * @return reservation state
 */
inline bool IsTrackAcrossTunnelBridge(TileIndex tile, Track t)
{
	assert_tile(IsTileType(tile, MP_TUNNELBRIDGE), tile);
	assert_tile(GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL, tile);
	return DiagdirReachesTracks(ReverseDiagDir(GetTunnelBridgeDirection(tile))) & TrackToTrackBits(t);
}

/**
 * Lift the reservation of a specific track on a tunnel or rail bridge head tile
 * @pre IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL
 * @param tile the tile
 */
inline void UnreserveAcrossRailTunnelBridge(TileIndex tile)
{
	assert_tile(IsTileType(tile, MP_TUNNELBRIDGE), tile);
	assert_tile(GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL, tile);
	if (IsTunnel(tile)) {
		SetTunnelReservation(tile, false);
	} else {
		UnreserveAcrossRailBridgeHead(tile);
	}
}

/**
 * Declare tunnel/bridge entrance with signal simulation.
 * @param t the tunnel/bridge tile.
 */
inline void SetTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	SetBit(_m[t].m5, 5);
}

/**
 * Remove tunnel/bridge entrance with signal simulation.
 * @param t the tunnel/bridge tile.
 */
inline void ClrTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	ClrBit(_m[t].m5, 5);
}

/**
 * Declare tunnel/bridge exit with signal simulation.
 * @param t the tunnel/bridge tile.
 */
inline void SetTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	SetBit(_m[t].m5, 6);
}

/**
 * Remove tunnel/bridge exit with signal simulation.
 * @param t the tunnel/bridge tile.
 */
inline void ClrTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	ClrBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge pair with signal simulation?
 * On tunnel/bridge pair minimal one of the two bits is set.
 * @param t the tile that might be a tunnel/bridge.
 * @return true if and only if this tile is a tunnel/bridge with signal simulation.
 */
inline bool IsTunnelBridgeWithSignalSimulation(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && (HasBit(_m[t].m5, 5) || HasBit(_m[t].m5, 6));
}

/**
 * Is this a tunnel/bridge entrance tile with signal?
 * Tunnel bridge signal simulation has allways bit 5 on at entrance.
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge entrance.
 */
inline bool IsTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return HasBit(_m[t].m5, 5);
}

/**
 * Is this a tunnel/bridge entrance tile with signal?
 * Tunnel bridge signal simulation has allways bit 5 on at entrance.
 * @param t the tile that might be a tunnel/bridge.
 * @return true if and only if this tile is a tunnel/bridge entrance.
 */
inline bool IsTunnelBridgeSignalSimulationEntranceTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && HasBit(_m[t].m5, 5);
}

/**
 * Is this a tunnel/bridge entrance tile with signal only?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge entrance only.
 */
inline bool IsTunnelBridgeSignalSimulationEntranceOnly(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return HasBit(_m[t].m5, 5) && !HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge exit?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge exit.
 */
inline bool IsTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge exit?
 * @param t the tile that might be a tunnel/bridge.
 * @return true if and only if this tile is a tunnel/bridge exit.
 */
inline bool IsTunnelBridgeSignalSimulationExitTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge exit only?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge exit only.
 */
inline bool IsTunnelBridgeSignalSimulationExitOnly(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return !HasBit(_m[t].m5, 5) && HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge entrance and exit?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge entrance and exit.
 */
inline bool IsTunnelBridgeSignalSimulationBidirectional(TileIndex t)
{
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return HasBit(_m[t].m5, 5) && HasBit(_m[t].m5, 6);
}

/**
 * Get the signal state for a tunnel/bridge entrance with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @return signal state
 */
inline SignalState GetTunnelBridgeEntranceSignalState(TileIndex t)
{
	assert_tile(IsTunnelBridgeSignalSimulationEntrance(t), t);
	return HasBit(_me[t].m6, 0) ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED;
}

/**
 * Get the signal state for a tunnel/bridge exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @return signal state
 */
inline SignalState GetTunnelBridgeExitSignalState(TileIndex t)
{
	assert_tile(IsTunnelBridgeSignalSimulationExit(t), t);
	return HasBit(_me[t].m6, 7) ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED;
}

/**
 * Set the signal state for a tunnel/bridge entrance or exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @param state signal state
 */
inline void SetTunnelBridgeEntranceSignalState(TileIndex t, SignalState state)
{
	assert_tile(IsTunnelBridgeSignalSimulationEntrance(t), t);
	SB(_me[t].m6, 0, 1, (state == SIGNAL_STATE_GREEN) ? 1 : 0);
}

/**
 * Set the signal state for a tunnel/bridge entrance or exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @param state signal state
 */
inline void SetTunnelBridgeExitSignalState(TileIndex t, SignalState state)
{
	assert_tile(IsTunnelBridgeSignalSimulationExit(t), t);
	SB(_me[t].m6, 7, 1, (state == SIGNAL_STATE_GREEN) ? 1 : 0);
}

inline bool IsTunnelBridgeSemaphore(TileIndex t)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	return HasBit(_me[t].m6, 1);
}

inline void SetTunnelBridgeSemaphore(TileIndex t, bool is_semaphore)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	SB(_me[t].m6, 1, 1, is_semaphore ? 1 : 0);
}

inline bool IsTunnelBridgePBS(TileIndex t)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	return HasBit(_me[t].m6, 6);
}

inline bool IsTunnelBridgeEffectivelyPBS(TileIndex t)
{
	return _settings_game.vehicle.train_braking_model == TBM_REALISTIC || IsTunnelBridgePBS(t);
}

inline void SetTunnelBridgePBS(TileIndex t, bool is_pbs)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	SB(_me[t].m6, 6, 1, is_pbs ? 1 : 0);
}

inline uint8 GetTunnelBridgeEntranceSignalAspect(TileIndex t)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	return GB(_m[t].m3, 0, 3);
}

inline void SetTunnelBridgeEntranceSignalAspect(TileIndex t, uint8 aspect)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	SB(_m[t].m3, 0, 3, aspect);
}

inline uint8 GetTunnelBridgeExitSignalAspect(TileIndex t)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	return GB(_m[t].m3, 3, 3);
}

inline void SetTunnelBridgeExitSignalAspect(TileIndex t, uint8 aspect)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(t), t);
	SB(_m[t].m3, 3, 3, aspect);
}

inline uint GetTunnelBridgeSignalSimulationSpacing(TileIndex t)
{
	assert_tile(IsRailTunnelBridgeTile(t), t);
	return 1 + GB(_me[t].m8, 12, 4);
}

inline void SetTunnelBridgeSignalSimulationSpacing(TileIndex t, uint spacing)
{
	assert_tile(IsRailTunnelBridgeTile(t), t);
	SB(_me[t].m8, 12, 4, spacing - 1);
}

/**
 * Does tunnel/bridge signal tile have "one or more trace restrict mappings present" bit set
 * @param tile the tile to check
 */
inline bool IsTunnelBridgeRestrictedSignal(TileIndex tile)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(tile), tile);
	return (bool) GB(_m[tile].m3, 6, 1);
}

/**
 * Set tunnel/bridge signal tile "one or more trace restrict mappings present" bit
 * @param tile the tile to set
 */
inline void SetTunnelBridgeRestrictedSignal(TileIndex tile, bool is_restricted)
{
	assert_tile(IsTunnelBridgeWithSignalSimulation(tile), tile);
	SB(_m[tile].m3, 6, 1, is_restricted);
}

inline Trackdir GetTunnelBridgeExitTrackdir(TileIndex t, DiagDirection tunnel_bridge_dir)
{
	return TrackEnterdirToTrackdir((Track)FIND_FIRST_BIT(GetAcrossTunnelBridgeTrackBits(t)), ReverseDiagDir(tunnel_bridge_dir));
}

inline Trackdir GetTunnelBridgeExitTrackdir(TileIndex t)
{
	return GetTunnelBridgeExitTrackdir(t, GetTunnelBridgeDirection(t));
}

inline Trackdir GetTunnelBridgeEntranceTrackdir(TileIndex t, DiagDirection tunnel_bridge_dir)
{
	return TrackExitdirToTrackdir((Track)FIND_FIRST_BIT(GetAcrossTunnelBridgeTrackBits(t)), tunnel_bridge_dir);
}

inline Trackdir GetTunnelBridgeEntranceTrackdir(TileIndex t)
{
	return GetTunnelBridgeEntranceTrackdir(t, GetTunnelBridgeDirection(t));
}

inline void SetTunnelBridgeSignalStyle(TileIndex t, TileIndex end, uint8 style)
{
	if (style == 0 && !HasBit(_m[t].m3, 7)) return;

	extern void SetTunnelBridgeSignalStyleExtended(TileIndex t, TileIndex end, uint8 style);
	SetTunnelBridgeSignalStyleExtended(t, end, style);
}

inline uint8 GetTunnelBridgeSignalStyle(TileIndex t)
{
	if (likely(!HasBit(_m[t].m3, 7))) return 0;

	if (IsTunnel(t)) {
		extern uint8 GetTunnelSignalStyleExtended(TileIndex t);
		return GetTunnelSignalStyleExtended(t);
	} else {
		extern uint8 GetBridgeSignalStyleExtended(TileIndex t);
		return GetBridgeSignalStyleExtended(t);
	}
}

void AddRailTunnelBridgeInfrastructure(Company *c, TileIndex begin, TileIndex end);
void SubtractRailTunnelBridgeInfrastructure(Company *c, TileIndex begin, TileIndex end);
void AddRailTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);
void SubtractRailTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);
void AddRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);
void SubtractRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);

#endif /* TUNNELBRIDGE_MAP_H */
