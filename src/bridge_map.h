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
	assert_tile(IsTileType(t, MP_TUNNELBRIDGE), t);
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
	assert_tile(IsBridgeTile(t), t);
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
	assert_tile(IsBridgeAbove(t), t);
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
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = 1 << 7 | tt << 2 | d;
	SB(_me[t].m6, 2, 4, bridgetype);
	_me[t].m7 = 0;
	_me[t].m8 = rt;
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
	/* Backup bridge signal and custom bridgehead data. */
	auto m2_backup = _m[t].m2;
	auto m4_backup = _m[t].m4;
	auto m5_backup = _m[t].m5;
	auto m6_backup = _me[t].m6;

	MakeBridgeRamp(t, o, bridgetype, d, TRANSPORT_RAIL, r);

	if (upgrade) {
		/* Restore bridge signal and custom bridgehead data if we're upgrading an existing bridge. */
		_m[t].m2 = m2_backup;
		SB(_m[t].m4, 0, 6, GB(m4_backup, 0, 6));
		SB(_m[t].m5, 4, 3, GB(m5_backup, 4, 3));
		SB(_me[t].m6, 0, 2, GB(m6_backup, 0, 2));
		SB(_me[t].m6, 6, 1, GB(m6_backup, 6, 1));
	} else {
		/* Set bridge head tracks to axial track only. */
		SB(_m[t].m4, 0, 6, DiagDirToDiagTrackBits(d));
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
	assert_tile(IsBridgeTile(t) && (TransportType)GB(_m[t].m5, 2, 2) == TRANSPORT_ROAD, t);
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
	assert_tile(IsBridgeTile(t), t);
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
	assert_tile(IsBridgeTile(t), t);
	if (HasTileRoadType(t, rt)) {
		assert(bits != ROAD_NONE);
		SB(_m[t].m2, rt == ROADTYPE_TRAM ? 4 : 0, 4, bits ^ (GB(_m[t].m5, 0, 1) ? ROAD_Y : ROAD_X));
	} else {
		assert(bits == ROAD_NONE);
		SB(_m[t].m2, rt == ROADTYPE_TRAM ? 4 : 0, 4, 0);
	}
}

/**
 * Checks if this tile is a rail bridge head
 * @param t The tile to analyze
 * @return true if it is a rail bridge head
 */
static inline bool IsRailBridgeHeadTile(TileIndex t)
{
	return IsBridgeTile(t) && (TransportType)GB(_m[t].m5, 2, 2) == TRANSPORT_RAIL;
}

/**
 * Checks if this tile is a flat rail bridge head
 * @param t The tile to analyze
 * @return true if it is a flat rail bridge head
 */
static inline bool IsFlatRailBridgeHeadTile(TileIndex t)
{
	return IsRailBridgeHeadTile(t) && HasBridgeFlatRamp(GetTileSlope(t), DiagDirToAxis((DiagDirection)GB(_m[t].m5, 0, 2)));
}

/**
 * Returns the track bits for a (possibly custom) rail bridge head
 * @param tile the tile to get the track bits from
 * @pre IsRailBridgeHeadTile(t)
 * @return road bits for the bridge head
 */
static inline TrackBits GetCustomBridgeHeadTrackBits(TileIndex t)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	return (TrackBits)GB(_m[t].m4, 0, 6);
}

/**
 * Sets the road track for a (possibly custom) rail bridge head
 * @param t the tile to set the track bits of
 * @param b the new track bits for the tile
 * @pre IsRailBridgeHeadTile(t)
 */
static inline void SetCustomBridgeHeadTrackBits(TileIndex t, TrackBits b)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	SB(_m[t].m4, 0, 6, b);
}

/**
 * Checks if this rail bridge head is a custom bridge head
 * @param t The tile to analyze
 * @pre IsRailBridgeHeadTile(t)
 * @return true if it is a custom bridge head
 */
static inline bool IsRailCustomBridgeHead(TileIndex t)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	return GetCustomBridgeHeadTrackBits(t) != DiagDirToDiagTrackBits((DiagDirection)GB(_m[t].m5, 0, 2));
}

/**
 * Checks if this tile is a rail bridge head with a custom bridge head
 * @param t The tile to analyze
 * @return true if it is a rail bridge head with a custom bridge head
 */
static inline bool IsRailCustomBridgeHeadTile(TileIndex t)
{
	return IsRailBridgeHeadTile(t) && IsRailCustomBridgeHead(t);
}

/**
 * Checks if this tile is a bridge head with a custom bridge head
 * @param t The tile to analyze
 * @return true if it is a bridge head with a custom bridge head
 */
static inline bool IsCustomBridgeHeadTile(TileIndex t)
{
	return IsRailCustomBridgeHeadTile(t) || IsRoadCustomBridgeHeadTile(t);
}

/**
 * Get the reserved track bits for a rail bridge head
 * @pre IsRailBridgeHeadTile(t)
 * @param t the tile
 * @return reserved track bits
 */
static inline TrackBits GetBridgeReservationTrackBits(TileIndex t)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	byte track_b = GB(_m[t].m2, 0, 3);
	Track track = (Track)(track_b - 1);    // map array saves Track+1
	if (track_b == 0) return TRACK_BIT_NONE;
	return (TrackBits)(TrackToTrackBits(track) | (HasBit(_m[t].m2, 3) ? TrackToTrackBits(TrackToOppositeTrack(track)) : 0));
}

/**
 * Sets the reserved track bits of the rail bridge head
 * @pre IsRailBridgeHeadTile(t)
 * @param t the tile to change
 * @param b the track bits
 */
static inline void SetBridgeReservationTrackBits(TileIndex t, TrackBits b)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	assert(!TracksOverlap(b));
	Track track = RemoveFirstTrack(&b);
	SB(_m[t].m2, 0, 3, track == INVALID_TRACK ? 0 : track + 1);
	SB(_m[t].m2, 3, 1, (byte)(b != TRACK_BIT_NONE));
}


/**
 * Try to reserve a specific track on a rail bridge head tile
 * @pre IsRailBridgeHeadTile(t) && HasBit(GetCustomBridgeHeadTrackBits(tile), t)
 * @param tile the tile
 * @param t the rack to reserve
 * @return true if successful
 */
static inline bool TryReserveRailBridgeHead(TileIndex tile, Track t)
{
	assert_tile(IsRailBridgeHeadTile(tile), tile);
	assert_tile(HasBit(GetCustomBridgeHeadTrackBits(tile), t), tile);
	TrackBits bits = TrackToTrackBits(t);
	TrackBits res = GetBridgeReservationTrackBits(tile);
	if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved
	res |= bits;
	if (TracksOverlap(res)) return false;  // crossing reservation present
	SetBridgeReservationTrackBits(tile, res);
	return true;
}


/**
 * Lift the reservation of a specific track on a rail bridge head tile
 * @pre IsRailBridgeHeadTile(t) && HasBit(GetCustomBridgeHeadTrackBits(tile), t)
 * @param tile the tile
 * @param t the track to free
 */
static inline void UnreserveRailBridgeHeadTrack(TileIndex tile, Track t)
{
	assert_tile(IsRailBridgeHeadTile(tile), tile);
	assert(HasBit(GetCustomBridgeHeadTrackBits(tile), t));
	TrackBits res = GetBridgeReservationTrackBits(tile);
	res &= ~TrackToTrackBits(t);
	SetBridgeReservationTrackBits(tile, res);
}

/**
 * Get the possible track bits of the bridge head tile onto/across the bridge
 * @pre IsRailBridgeHeadTile(t)
 * @param t the tile
 * @return reservation state
 */
static inline TrackBits GetAcrossBridgePossibleTrackBits(TileIndex t)
{
	assert_tile(IsRailBridgeHeadTile(t), t);
	return DiagdirReachesTracks(ReverseDiagDir((DiagDirection)GB(_m[t].m5, 0, 2)));
}

/**
 * Get the reserved track bits of the bridge head tile onto/across the bridge
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reservation state
 */
static inline TrackBits GetAcrossBridgeReservationTrackBits(TileIndex t)
{
	return GetBridgeReservationTrackBits(t) & GetAcrossBridgePossibleTrackBits(t);
}

/**
 * Get the reservation state of the bridge head tile onto/across the bridge
 * @pre IsBridgeTile(t) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL
 * @param t the tile
 * @return reservation state
 */
static inline bool HasAcrossBridgeReservation(TileIndex t)
{
	return GetAcrossBridgeReservationTrackBits(t) != TRACK_BIT_NONE;
}

/**
 * Lift the reservation of a specific track on a rail bridge head tile
 * @pre IsRailBridgeHeadTile(t)
 * @param tile the tile
 */
static inline void UnreserveAcrossRailBridgeHead(TileIndex tile)
{
	assert_tile(IsRailBridgeHeadTile(tile), tile);
	TrackBits res = GetAcrossBridgeReservationTrackBits(tile);
	if (res != TRACK_BIT_NONE) {
		SetBridgeReservationTrackBits(tile, GetBridgeReservationTrackBits(tile) & ~res);
	}
}

#endif /* BRIDGE_MAP_H */
