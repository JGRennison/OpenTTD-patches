/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnel_map.h Map accessors for tunnels. */

#ifndef TUNNEL_MAP_H
#define TUNNEL_MAP_H

#include "road_map.h"

typedef uint32_t TunnelID; ///< Type for the unique identifier of tunnels.

static const TunnelID TUNNEL_ID_MAP_LOOKUP = 0xFFFF; ///< Sentinel ID value to store in m2 to indicate that the ID should be looked up instead

/**
 * Is this a tunnel (entrance)?
 * @param t the tile that might be a tunnel
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if and only if this tile is a tunnel (entrance)
 */
inline bool IsTunnel(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
	return !HasBit(_m[t].m5, 7);
}

/**
 * Is this a tunnel (entrance)?
 * @param t the tile that might be a tunnel
 * @return true if and only if this tile is a tunnel (entrance)
 */
inline bool IsTunnelTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && IsTunnel(t);
}

/**
 * Get the index of tunnel tile.
 * @param t the tile
 * @pre IsTunnelTile(t)
 * @return TunnelID
 */
inline TunnelID GetTunnelIndex(TileIndex t)
{
	extern TunnelID GetTunnelIndexByLookup(TileIndex t);

	dbg_assert_tile(IsTunnelTile(t), t);
	TunnelID map_id = _m[t].m2;
	return map_id == TUNNEL_ID_MAP_LOOKUP ? GetTunnelIndexByLookup(t) : map_id;
}

/**
 * Checks if this tile is a rail tunnel
 * @param t the tile that might be a rail tunnel
 * @return true if it is a rail tunnel
 */
inline bool IsRailTunnelTile(TileIndex t)
{
	return IsTunnelTile(t) && (TransportType)GB(_m[t].m5, 2, 2) == TRANSPORT_RAIL;
}

/**
 * Get the reservation state of the rail tunnel
 * @pre IsRailTunnelTile(t)
 * @param t the tile
 * @return reservation state
 */
inline bool HasTunnelReservation(TileIndex t)
{
	dbg_assert_tile(IsRailTunnelTile(t), t);
	return HasBit(_m[t].m5, 4);
}

/**
 * Set the reservation state of the rail tunnel
 * @pre IsRailTunnelTile(t)
 * @param t the tile
 * @param b the reservation state
 */
inline void SetTunnelReservation(TileIndex t, bool b)
{
	dbg_assert_tile(IsRailTunnelTile(t), t);
	AssignBit(_m[t].m5, 4, b);
}

TileIndex GetOtherTunnelEnd(TileIndex);

/** Flags for miscellaneous industry tile specialities */
enum IsTunnelInWayFlags {
	ITIWF_NONE                  = 0,
	ITIWF_IGNORE_CHUNNEL        = 1 << 0, ///< Chunnel mid-parts are ignored, used when terraforming.
	ITIWF_CHUNNEL_ONLY          = 1 << 1, ///< Only check for chunnels
};
DECLARE_ENUM_AS_BIT_SET(IsTunnelInWayFlags)

bool IsTunnelInWay(TileIndex, int z, IsTunnelInWayFlags flags = ITIWF_NONE);

/**
 * Set the index of tunnel tile.
 * @param t the tile
 * @param id the tunnel ID
 * @pre IsTunnelTile(t)
 */
inline void SetTunnelIndex(TileIndex t, TunnelID id)
{
	dbg_assert_tile(IsTunnelTile(t), t);
	_m[t].m2 = (id >= TUNNEL_ID_MAP_LOOKUP) ? TUNNEL_ID_MAP_LOOKUP : id;
}

void SetTunnelSignalStyle(TileIndex t, uint8_t style);

inline uint8_t GetTunnelSignalStyle(TileIndex t)
{
	if (likely(!HasBit(_m[t].m3, 7))) return 0;

	extern uint8_t GetTunnelSignalStyleExtended(TileIndex t);
	return GetTunnelSignalStyleExtended(t);
}

/**
 * Makes a road tunnel entrance
 * @param t the entrance of the tunnel
 * @param o the owner of the entrance
 * @param id the tunnel ID
 * @param d the direction facing out of the tunnel
 * @param r the road type used in the tunnel
 */
inline void MakeRoadTunnel(TileIndex t, Owner o, TunnelID id, DiagDirection d, RoadType road_rt, RoadType tram_rt)
{
	SetTileType(t, MP_TUNNELBRIDGE);
	SetTileOwner(t, o);
	SetTunnelIndex(t, id);
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = TRANSPORT_ROAD << 2 | d;
	SB(_me[t].m6, 2, 4, 0);
	_me[t].m7 = 0;
	_me[t].m8 = 0;
	SetRoadOwner(t, RTT_ROAD, o);
	if (o != OWNER_TOWN) SetRoadOwner(t, RTT_TRAM, o);
	SetRoadTypes(t, road_rt, tram_rt);
}

/**
 * Makes a rail tunnel entrance
 * @param t the entrance of the tunnel
 * @param o the owner of the entrance
 * @param id the tunnel ID
 * @param d the direction facing out of the tunnel
 * @param r the rail type used in the tunnel
 */
inline void MakeRailTunnel(TileIndex t, Owner o, TunnelID id, DiagDirection d, RailType r)
{
	SetTileType(t, MP_TUNNELBRIDGE);
	SetTileOwner(t, o);
	SetTunnelIndex(t, id);
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = TRANSPORT_RAIL << 2 | d;
	SB(_me[t].m6, 2, 4, 0);
	_me[t].m7 = 0;
	_me[t].m8 = r;
}

#endif /* TUNNEL_MAP_H */
