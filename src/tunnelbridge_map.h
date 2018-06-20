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
#include "tunnel_base.h"
#include "cmd_helper.h"
#include "signal_type.h"


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
* Is this a rail bridge or tunnel?
* @param t the tile that might be a rail bridge or tunnel
* @return true if and only if this tile is a rail bridge or tunnel
*/
static inline bool IsRailTunnelBridgeTile(TileIndex t)
{
	TransportType tt = Extract<TransportType, 2, 2>(_m[t].m5);

	return IsTileType(t, MP_TUNNELBRIDGE) && (tt == TRANSPORT_RAIL);
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
 * Get the reservation state of the rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reservation state
 */
static inline bool HasTunnelBridgeReservation(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL);
	return HasBit(_m[t].m5, 4);
}

/**
 * Set the reservation state of the rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @param b the reservation state
 */
static inline void SetTunnelBridgeReservation(TileIndex t, bool b)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	assert(GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL);
	SB(_m[t].m5, 4, 1, b ? 1 : 0);
}

/**
 * Get the reserved track bits for a rail tunnel/bridge
 * @pre IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reserved track bits
 */
static inline TrackBits GetTunnelBridgeReservationTrackBits(TileIndex t)
{
	return HasTunnelBridgeReservation(t) ? DiagDirToDiagTrackBits(GetTunnelBridgeDirection(t)) : TRACK_BIT_NONE;
}

/**
 * Declare tunnel/bridge entrance with signal simulation.
 * @param t the tunnel/bridge tile.
 */
static inline void SetTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	SetBit(_m[t].m5, 5);
}

/**
 * Remove tunnel/bridge entrance with signal simulation.
 * @param t the tunnel/bridge tile.
 */
static inline void ClrTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	ClrBit(_m[t].m5, 5);
}

/**
 * Declare tunnel/bridge exit with signal simulation.
 * @param t the tunnel/bridge tile.
 */
static inline void SetTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	SetBit(_m[t].m5, 6);
}

/**
 * Remove tunnel/bridge exit with signal simulation.
 * @param t the tunnel/bridge tile.
 */
static inline void ClrTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	ClrBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge pair with signal simulation?
 * On tunnel/bridge pair minimal one of the two bits is set.
 * @param t the tile that might be a tunnel/bridge.
 * @return true if and only if this tile is a tunnel/bridge with signal simulation.
 */
static inline bool IsTunnelBridgeWithSignalSimulation(TileIndex t)
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
static inline bool IsTunnelBridgeSignalSimulationEntrance(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_m[t].m5, 5);
}

/**
 * Is this a tunnel/bridge exit?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge exit.
 */
static inline bool IsTunnelBridgeSignalSimulationExit(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge exit only?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge exit only.
 */
static inline bool IsTunnelBridgeSignalSimulationExitOnly(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return !HasBit(_m[t].m5, 5) && HasBit(_m[t].m5, 6);
}

/**
 * Is this a tunnel/bridge entrance and exit?
 * @param t the tile that might be a tunnel/bridge.
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel/bridge entrance and exit.
 */
static inline bool IsTunnelBridgeSignalSimulationBidirectional(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_m[t].m5, 5) && HasBit(_m[t].m5, 6);
}

/**
 * Get the signal state for a tunnel/bridge entrance with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @return signal state
 */
static inline SignalState GetTunnelBridgeEntranceSignalState(TileIndex t)
{
	assert(IsTunnelBridgeSignalSimulationEntrance(t));
	return HasBit(_me[t].m6, 0) ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED;
}

/**
 * Get the signal state for a tunnel/bridge exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @return signal state
 */
static inline SignalState GetTunnelBridgeExitSignalState(TileIndex t)
{
	assert(IsTunnelBridgeSignalSimulationExit(t));
	return HasBit(_me[t].m6, 7) ? SIGNAL_STATE_GREEN : SIGNAL_STATE_RED;
}

/**
 * Set the signal state for a tunnel/bridge entrance or exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @param state signal state
 */
static inline void SetTunnelBridgeEntranceSignalState(TileIndex t, SignalState state)
{
	assert(IsTunnelBridgeSignalSimulationEntrance(t));
	SB(_me[t].m6, 0, 1, (state == SIGNAL_STATE_GREEN) ? 1 : 0);
}

/**
 * Set the signal state for a tunnel/bridge entrance or exit with signal simulation
 * @param t the tunnel/bridge entrance or exit tile with signal simulation
 * @pre IsTunnelBridgeWithSignalSimulation(t)
 * @param state signal state
 */
static inline void SetTunnelBridgeExitSignalState(TileIndex t, SignalState state)
{
	assert(IsTunnelBridgeSignalSimulationExit(t));
	SB(_me[t].m6, 7, 1, (state == SIGNAL_STATE_GREEN) ? 1 : 0);
}

static inline bool IsTunnelBridgeSemaphore(TileIndex t)
{
	assert(IsTunnelBridgeWithSignalSimulation(t));
	return HasBit(_me[t].m6, 1);
}

static inline void SetTunnelBridgeSemaphore(TileIndex t, bool is_semaphore)
{
	assert(IsTunnelBridgeWithSignalSimulation(t));
	SB(_me[t].m6, 1, 1, is_semaphore ? 1 : 0);
}

static inline bool IsTunnelBridgePBS(TileIndex t)
{
	assert(IsTunnelBridgeWithSignalSimulation(t));
	return HasBit(_me[t].m6, 6);
}

static inline void SetTunnelBridgePBS(TileIndex t, bool is_pbs)
{
	assert(IsTunnelBridgeWithSignalSimulation(t));
	SB(_me[t].m6, 6, 1, is_pbs ? 1 : 0);
}

void AddRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);
void SubtractRoadTunnelBridgeInfrastructure(TileIndex begin, TileIndex end);

#endif /* TUNNELBRIDGE_MAP_H */
