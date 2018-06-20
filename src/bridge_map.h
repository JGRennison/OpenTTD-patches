/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.h Map accessor functions for bridges. */

#ifndef BRIDGE_MAP_H
#define BRIDGE_MAP_H

#include "road_map.h"
#include "bridge.h"

/**
 * Checks if this is a bridge, instead of a tunnel
 * @param t The tile to analyze
 * @pre IsTileType(t, MP_TUNNELBRIDGE)
 * @return true if the structure is a bridge one
 */
static inline bool IsBridge(TileIndex t)
{
	assert(IsTileType(t, MP_TUNNELBRIDGE));
	return HasBit(_m[t].m5, 7);
}

/**
 * checks if there is a bridge on this tile
 * @param t The tile to analyze
 * @return true if a bridge is present
 */
static inline bool IsBridgeTile(TileIndex t)
{
	return IsTileType(t, MP_TUNNELBRIDGE) && IsBridge(t);
}

/**
 * checks if a bridge is set above the ground of this tile
 * @param t The tile to analyze
 * @return true if a bridge is detected above
 */
static inline bool IsBridgeAbove(TileIndex t)
{
	return GB(_m[t].type, 2, 2) != 0;
}

/**
 * Determines the type of bridge on a tile
 * @param t The tile to analyze
 * @pre IsBridgeTile(t)
 * @return The bridge type
 */
static inline BridgeType GetBridgeType(TileIndex t)
{
	assert(IsBridgeTile(t));
	return GB(_me[t].m6, 2, 4);
}

/**
 * Get the axis of the bridge that goes over the tile. Not the axis or the ramp.
 * @param t The tile to analyze
 * @pre IsBridgeAbove(t)
 * @return the above mentioned axis
 */
static inline Axis GetBridgeAxis(TileIndex t)
{
	assert(IsBridgeAbove(t));
	return (Axis)(GB(_m[t].type, 2, 2) - 1);
}

TileIndex GetNorthernBridgeEnd(TileIndex t);
TileIndex GetSouthernBridgeEnd(TileIndex t);
TileIndex GetOtherBridgeEnd(TileIndex t);

int GetBridgeHeight(TileIndex tile);
/**
 * Get the height ('z') of a bridge in pixels.
 * @param tile the bridge ramp tile to get the bridge height from
 * @return the height of the bridge in pixels
 */
static inline int GetBridgePixelHeight(TileIndex tile)
{
	return GetBridgeHeight(tile) * TILE_HEIGHT;
}

/**
 * Remove the bridge over the given axis.
 * @param t the tile to remove the bridge from
 * @param a the axis of the bridge to remove
 */
static inline void ClearSingleBridgeMiddle(TileIndex t, Axis a)
{
	ClrBit(_m[t].type, 2 + a);
}

/**
 * Removes bridges from the given, that is bridges along the X and Y axis.
 * @param t the tile to remove the bridge from
 */
static inline void ClearBridgeMiddle(TileIndex t)
{
	ClearSingleBridgeMiddle(t, AXIS_X);
	ClearSingleBridgeMiddle(t, AXIS_Y);
}

/**
 * Set that there is a bridge over the given axis.
 * @param t the tile to add the bridge to
 * @param a the axis of the bridge to add
 */
static inline void SetBridgeMiddle(TileIndex t, Axis a)
{
	SetBit(_m[t].type, 2 + a);
}

/**
 * Generic part to make a bridge ramp for both roads and rails.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param bridgetype the type of bridge this bridge ramp belongs to
 * @param d          the direction this ramp must be facing
 * @param tt         the transport type of the bridge
 * @param rt         the road or rail type
 * @note this function should not be called directly.
 */
static inline void MakeBridgeRamp(TileIndex t, Owner o, BridgeType bridgetype, DiagDirection d, TransportType tt, uint rt)
{
	SetTileType(t, MP_TUNNELBRIDGE);
	SetTileOwner(t, o);
	_m[t].m2 = 0;
	if (tt == TRANSPORT_RAIL) {
		SB(_m[t].m1, 7, 1, GB(rt, 4, 1));
		SB(_m[t].m3, 0, 4, GB(rt, 0, 4));
		SB(_m[t].m3, 4, 4, 0);
	} else {
		_m[t].m3 = rt;
	}
	_m[t].m4 = 0;
	_m[t].m5 = 1 << 7 | tt << 2 | d;
	SB(_me[t].m6, 2, 4, bridgetype);
	_me[t].m7 = 0;
}

/**
 * Make a bridge ramp for roads.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param owner_road the new owner of the road on the bridge
 * @param owner_tram the new owner of the tram on the bridge
 * @param bridgetype the type of bridge this bridge ramp belongs to
 * @param d          the direction this ramp must be facing
 * @param r          the road type of the bridge
 * @param upgrade    whether the bridge is an upgrade instead of a totally new bridge
 */
static inline void MakeRoadBridgeRamp(TileIndex t, Owner o, Owner owner_road, Owner owner_tram, BridgeType bridgetype, DiagDirection d, RoadTypes r, bool upgrade)
{
	// Backup custom bridgehead data.
	uint custom_bridge_head_backup = GB(_m[t].m2, 0, 8);

	MakeBridgeRamp(t, o, bridgetype, d, TRANSPORT_ROAD, 0);
	SetRoadOwner(t, ROADTYPE_ROAD, owner_road);
	if (owner_tram != OWNER_TOWN) SetRoadOwner(t, ROADTYPE_TRAM, owner_tram);
	SetRoadTypes(t, r);

	// Restore custom bridgehead data if we're upgrading an existing bridge.
	if (upgrade) SB(_m[t].m2, 0, 8, custom_bridge_head_backup);
}

/**
 * Make a bridge ramp for rails.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param bridgetype the type of bridge this bridge ramp belongs to
 * @param d          the direction this ramp must be facing
 * @param r          the rail type of the bridge
 * @param upgrade    whether the bridge is an upgrade instead of a totally new bridge
 */
static inline void MakeRailBridgeRamp(TileIndex t, Owner o, BridgeType bridgetype, DiagDirection d, RailType r, bool upgrade)
{
	// Backup bridge signal data.
	auto m2_backup = _m[t].m2;
	auto m5_backup = _m[t].m5;
	auto m6_backup = _me[t].m6;

	MakeBridgeRamp(t, o, bridgetype, d, TRANSPORT_RAIL, r);

	// Restore bridge signal data if we're upgrading an existing bridge.
	if (upgrade) {
		_m[t].m2 = m2_backup;
		SB(_m[t].m5, 4, 3, GB(m5_backup, 4, 3));
		SB(_me[t].m6, 0, 2, GB(m6_backup, 0, 2));
		SB(_me[t].m6, 6, 1, GB(m6_backup, 6, 1));
	}
}

/**
 * Make a bridge ramp for aqueducts.
 * @param t          the tile to make a bridge ramp
 * @param o          the new owner of the bridge ramp
 * @param d          the direction this ramp must be facing
 */
static inline void MakeAqueductBridgeRamp(TileIndex t, Owner o, DiagDirection d)
{
	MakeBridgeRamp(t, o, 0, d, TRANSPORT_WATER, 0);
}

/**
 * Checks if this road bridge head is a custom bridge head
 * @param t The tile to analyze
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD
 * @return true if it is a custom bridge head
 */
static inline bool IsRoadCustomBridgeHead(TileIndex t)
{
	assert(IsBridgeTile(t) && (TransportType)GB(_m[t].m5, 2, 2) == TRANSPORT_ROAD);
	return GB(_m[t].m2, 0, 8) != 0;
}

/**
 * Checks if this tile is a road bridge head with a custom bridge head
 * @param t The tile to analyze
 * @return true if it is a road bridge head with a custom bridge head
 */
static inline bool IsRoadCustomBridgeHeadTile(TileIndex t)
{
	return IsBridgeTile(t) && (TransportType)GB(_m[t].m5, 2, 2) == TRANSPORT_ROAD && IsRoadCustomBridgeHead(t);
}

/**
 * Returns the road bits for a (possibly custom) road bridge head
 * @param t The tile to analyze
 * @param rt Road type.
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD
 * @return road bits for the bridge head
 */
static inline RoadBits GetCustomBridgeHeadRoadBits(TileIndex t, RoadType rt)
{
	assert(IsBridgeTile(t));
	if (!HasTileRoadType(t, rt)) return (RoadBits) 0;
	RoadBits bits = (GB(_m[t].m5, 0, 1) ? ROAD_Y : ROAD_X) ^ (RoadBits) GB(_m[t].m2, rt == ROADTYPE_TRAM ? 4 : 0, 4);
	return bits;
}

/**
 * Returns the road bits for a (possibly custom) road bridge head, for all road types
 * @param t The tile to analyze
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD
 * @return road bits for the bridge head
 */
static inline RoadBits GetCustomBridgeHeadAllRoadBits(TileIndex t)
{
	return GetCustomBridgeHeadRoadBits(t, ROADTYPE_ROAD) | GetCustomBridgeHeadRoadBits(t, ROADTYPE_TRAM);
}

/**
 * Sets the road bits for a (possibly custom) road bridge head
 * @param t The tile to modify
 * @param rt Road type.
 * @param bits The road bits.
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_ROAD
 * @pre HasTileRoadType() must be set correctly before calling this
 */
static inline void SetCustomBridgeHeadRoadBits(TileIndex t, RoadType rt, RoadBits bits)
{
	assert(IsBridgeTile(t));
	if (HasTileRoadType(t, rt)) {
		assert(bits != ROAD_NONE);
		SB(_m[t].m2, rt == ROADTYPE_TRAM ? 4 : 0, 4, bits ^ (GB(_m[t].m5, 0, 1) ? ROAD_Y : ROAD_X));
	} else {
		assert(bits == ROAD_NONE);
		SB(_m[t].m2, rt == ROADTYPE_TRAM ? 4 : 0, 4, 0);
	}
}

#endif /* BRIDGE_MAP_H */
