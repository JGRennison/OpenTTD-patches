/* $Id$ */

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
#include "tunnel_map.h"
#include "track_func.h"
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
static inline DiagDirection GetTunnelBridgeDirection(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return (DiagDirection)GB(_m[t].m5, 0, 2);
}

/**
 * Tunnel: Get the transport type of the tunnel (road or rail)
 * Bridge: Get the transport type of the bridge's ramp
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return the transport type in the tunnel/bridge
 */
static inline TransportType GetTunnelBridgeTransportType(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return (TransportType)GB(_m[t].m5, 2, 2);
}

/**
 * Tunnel: Is this tunnel entrance in a snowy or desert area?
 * Bridge: Does the bridge ramp lie in a snow or desert area?
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if the tile is in a snowy/desert area
 */
static inline bool HasTunnelBridgeSnowOrDesert(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_me[t].m7, 5);
}

/**
 * Tunnel: Places this tunnel entrance in a snowy or desert area, or takes it out of there.
 * Bridge: Sets whether the bridge ramp lies in a snow or desert area.
 * @param t the tunnel entrance / bridge ramp tile
 * @param snow_or_desert is the entrance/ramp in snow or desert (true), when
 *                       not in snow and not in desert false
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 */
static inline void SetTunnelBridgeSnowOrDesert(TileIndex t, bool snow_or_desert)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	SB(_me[t].m7, 5, 1, snow_or_desert);
}

/**
 * Determines type of the wormhole and returns its other end
 * @param t one end
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return other end
 */
static inline TileIndex GetOtherTunnelBridgeEnd(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return IsTunnel(t) ? GetOtherTunnelEnd(t) : GetOtherBridgeEnd(t);
}

/**
 * Get the track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
static inline TrackBits GetTunnelBridgeTrackBits(TileIndex t)
{
	if (IsTunnel(t)) {
		return DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t));
	} else {
		return GetCustomBridgeHeadTrackBits(t);
	}
}

/**
 * Get the track bits for a rail tunnel/bridge onto/across the tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
static inline TrackBits GetAcrossTunnelBridgeTrackBits(TileIndex t)
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
static inline TrackBits GetTunnelBridgeReservationTrackBits(TileIndex t)
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
static inline TrackBits GetAcrossTunnelBridgeReservationTrackBits(TileIndex t)
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
static inline bool HasAcrossTunnelBridgeReservation(TileIndex t)
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
static inline uint GetTunnelBridgeHeadOnlyRailInfrastructureCountFromTrackBits(TrackBits bits)
{
	uint pieces = CountBits(bits);
	if (TracksOverlap(bits)) pieces *= pieces;
	return (TUNNELBRIDGE_TRACKBIT_FACTOR / 2) * (1 + pieces);
}

/**
 * Get the rail infrastructure count of a rail tunnel/bridge head tile (excluding the tunnel/bridge middle)
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return rail infrastructure count
 */
static inline uint GetTunnelBridgeHeadOnlyRailInfrastructureCount(TileIndex t)
{
	return IsBridge(t) ? GetTunnelBridgeHeadOnlyRailInfrastructureCountFromTrackBits(GetTunnelBridgeTrackBits(t)) : TUNNELBRIDGE_TRACKBIT_FACTOR;
}

/**
 * Check if the given track direction on a rail bridge head tile enters the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @param td track direction
 * @return reservation state
 */
static inline bool TrackdirEntersTunnelBridge(TileIndex t, Trackdir td)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL);
	return TrackdirToExitdir(td) == GetTunnelBridgeDirection(t);
}

/**
 * Check if the given track direction on a rail bridge head tile exits the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @param td track direction
 * @return reservation state
 */
static inline bool TrackdirExitsTunnelBridge(TileIndex t, Trackdir td)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL);
	return TrackdirToExitdir(ReverseTrackdir(td)) == GetTunnelBridgeDirection(t);
}

/**
 * Check if the given track on a rail bridge head tile enters/exits the bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param tile the tile
 * @param t track
 * @return reservation state
 */
static inline bool IsTrackAcrossTunnelBridge(TileIndex tile, Track t)
{
	assert(IsTileType(tile, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL);
	return DiagdirReachesTracks(ReverseDiagDir(GetTunnelBridgeDirection(tile))) & TrackToTrackBits(t);
}

/**
 * Lift the reservation of a specific track on a tunnel or rail bridge head tile
 * @pre IsTileType(tile, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL
 * @param tile the tile
 */
static inline void UnreserveAcrossRailTunnelBridge(TileIndex tile)
{
	assert(IsTileType(tile, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL);
	if (IsTunnel(tile)) {
		SetTunnelReservation(tile, false);
	} else {
		UnreserveAcrossRailBridgeHead(tile);
	}
}

void AddRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);
void SubtractRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);

#endif /* TUNNELBRIDGE_MAP_H */
