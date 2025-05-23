/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_map.h Hides the direct accesses to the map array with map accessors */

#ifndef RAIL_MAP_H
#define RAIL_MAP_H

#include "rail_type.h"
#include "depot_type.h"
#include "signal_func.h"
#include "track_func.h"
#include "tile_map.h"
#include "water_map.h"
#include "signal_type.h"
#include "tunnelbridge_map.h"


/** Different types of Rail-related tiles */
enum RailTileType : uint8_t {
	RAIL_TILE_NORMAL   = 0, ///< Normal rail tile without signals
	RAIL_TILE_SIGNALS  = 1, ///< Normal rail tile with signals
	RAIL_TILE_DEPOT    = 3, ///< Depot (one entrance)
};

/**
 * Returns the RailTileType (normal with or without signals,
 * waypoint or depot).
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_RAILWAY)
 * @return the RailTileType
 */
debug_inline static RailTileType GetRailTileType(TileIndex t)
{
	dbg_assert_tile(IsTileType(t, MP_RAILWAY), t);
	return (RailTileType)GB(_m[t].m5, 6, 2);
}

/**
 * Returns whether this is plain rails, with or without signals. Iow, if this
 * tiles RailTileType is RAIL_TILE_NORMAL or RAIL_TILE_SIGNALS.
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_RAILWAY)
 * @return true if and only if the tile is normal rail (with or without signals)
 */
debug_inline static bool IsPlainRail(TileIndex t)
{
	RailTileType rtt = GetRailTileType(t);
	return rtt == RAIL_TILE_NORMAL || rtt == RAIL_TILE_SIGNALS;
}

/**
 * Checks whether the tile is a rail tile or rail tile with signals.
 * @param t the tile to get the information from
 * @return true if and only if the tile is normal rail (with or without signals)
 */
debug_inline static bool IsPlainRailTile(TileIndex t)
{
	return IsTileType(t, MP_RAILWAY) && IsPlainRail(t);
}


/**
 * Checks if a rail tile has signals.
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_RAILWAY)
 * @return true if and only if the tile has signals
 */
inline bool HasSignals(TileIndex t)
{
	return GetRailTileType(t) == RAIL_TILE_SIGNALS;
}

/**
 * Add/remove the 'has signal' bit from the RailTileType
 * @param tile the tile to add/remove the signals to/from
 * @param signals whether the rail tile should have signals or not
 * @pre IsPlainRailTile(tile)
 */
inline void SetHasSignals(TileIndex tile, bool signals)
{
	dbg_assert_tile(IsPlainRailTile(tile), tile);
	AssignBit(_m[tile].m5, 6, signals);
}

/**
 * Is this rail tile a rail depot?
 * @param t the tile to get the information from
 * @pre IsTileType(t, MP_RAILWAY)
 * @return true if and only if the tile is a rail depot
 */
debug_inline static bool IsRailDepot(TileIndex t)
{
	return GetRailTileType(t) == RAIL_TILE_DEPOT;
}

/**
 * Is this tile rail tile and a rail depot?
 * @param t the tile to get the information from
 * @return true if and only if the tile is a rail depot
 */
debug_inline static bool IsRailDepotTile(TileIndex t)
{
	return IsTileType(t, MP_RAILWAY) && IsRailDepot(t);
}

/**
 * Gets the rail type of the given tile
 * @param t the tile to get the rail type from
 * @return the rail type of the tile
 */
inline RailType GetRailType(TileIndex t)
{
	return (RailType)GB(_me[t].m8, 0, 6);
}

/**
 * Sets the rail type of the given tile
 * @param t the tile to set the rail type of
 * @param r the new rail type for the tile
 */
inline void SetRailType(TileIndex t, RailType r)
{
	SB(_me[t].m8, 0, 6, r);
}

/**
 * Gets the second rail type of the given tile
 * @param t the tile to get the rail type from
 * @return the rail type of the tile
 */
inline RailType GetSecondaryRailType(TileIndex t)
{
	return (RailType)GB(_me[t].m8, 6, 6);
}

/**
 * Sets the second rail type of the given tile
 * @param t the tile to set the rail type of
 * @param r the new rail type for the tile
 */
inline void SetSecondaryRailType(TileIndex t, RailType r)
{
	SB(_me[t].m8, 6, 6, r);
}

/**
 * Gets the second rail type of the given tile
 * @param t the tile to get the rail type from
 * @return the rail type of the tile
 */
inline RailType GetPlainRailParallelTrackRailTypeByTrackBit(TileIndex t, TrackBits b)
{
	return b & TRACK_BIT_RT_1 ? GetRailType(t) : GetSecondaryRailType(t);
}

/**
 * Gets the track bits of the given tile
 * @param tile the tile to get the track bits from
 * @return the track bits of the tile
 */
inline TrackBits GetTrackBits(TileIndex tile)
{
	dbg_assert_tile(IsPlainRailTile(tile), tile);
	return (TrackBits)GB(_m[tile].m5, 0, 6);
}

/**
 * Sets the track bits of the given tile
 * @param t the tile to set the track bits of
 * @param b the new track bits for the tile
 */
inline void SetTrackBits(TileIndex t, TrackBits b)
{
	dbg_assert_tile(IsPlainRailTile(t), t);
	SB(_m[t].m5, 0, 6, b);
}

/**
 * Returns whether the given track is present on the given tile.
 * @param tile  the tile to check the track presence of
 * @param track the track to search for on the tile
 * @pre IsPlainRailTile(tile)
 * @return true if and only if the given track exists on the tile
 */
inline bool HasTrack(TileIndex tile, Track track)
{
	return HasBit(GetTrackBits(tile), track);
}

/**
 * Returns the direction the depot is facing to
 * @param t the tile to get the depot facing from
 * @pre IsRailDepotTile(t)
 * @return the direction the depot is facing
 */
inline DiagDirection GetRailDepotDirection(TileIndex t)
{
	return (DiagDirection)GB(_m[t].m5, 0, 2);
}

/**
 * Returns the track of a depot, ignoring direction
 * @pre IsRailDepotTile(t)
 * @param t the tile to get the depot track from
 * @return the track of the depot
 */
inline Track GetRailDepotTrack(TileIndex t)
{
	return DiagDirToDiagTrack(GetRailDepotDirection(t));
}


/**
 * Returns the reserved track bits of the tile
 * @pre IsPlainRailTile(t)
 * @param t the tile to query
 * @return the track bits
 */
inline TrackBits GetRailReservationTrackBits(TileIndex t)
{
	dbg_assert_tile(IsPlainRailTile(t), t);
	uint8_t track_b = GB(_m[t].m2, 8, 3);
	Track track = (Track)(track_b - 1);    // map array saves Track+1
	if (track_b == 0) return TRACK_BIT_NONE;
	return (TrackBits)(TrackToTrackBits(track) | (HasBit(_m[t].m2, 11) ? TrackToTrackBits(TrackToOppositeTrack(track)) : 0));
}

/**
 * Sets the reserved track bits of the tile
 * @pre IsPlainRailTile(t) && !TracksOverlap(b)
 * @param t the tile to change
 * @param b the track bits
 */
inline void SetTrackReservation(TileIndex t, TrackBits b)
{
	dbg_assert_tile(IsPlainRailTile(t), t);
	dbg_assert(b != INVALID_TRACK_BIT);
	dbg_assert(!TracksOverlap(b));
	Track track = RemoveFirstTrack(&b);
	SB(_m[t].m2, 8, 3, track == INVALID_TRACK ? 0 : track + 1);
	AssignBit(_m[t].m2, 11, b != TRACK_BIT_NONE);
}

/**
 * Try to reserve a specific track on a tile
 * @pre IsPlainRailTile(t) && HasTrack(tile, t)
 * @param tile the tile
 * @param t the rack to reserve
 * @return true if successful
 */
inline bool TryReserveTrack(TileIndex tile, Track t)
{
	dbg_assert_tile(HasTrack(tile, t), tile);
	TrackBits bits = TrackToTrackBits(t);
	TrackBits res = GetRailReservationTrackBits(tile);
	if ((res & bits) != TRACK_BIT_NONE) return false;  // already reserved
	res |= bits;
	if (TracksOverlap(res)) return false;  // crossing reservation present
	SetTrackReservation(tile, res);
	return true;
}

/**
 * Lift the reservation of a specific track on a tile
 * @pre IsPlainRailTile(t) && HasTrack(tile, t)
 * @param tile the tile
 * @param t the track to free
 */
inline void UnreserveTrack(TileIndex tile, Track t)
{
	dbg_assert_tile(HasTrack(tile, t), tile);
	TrackBits res = GetRailReservationTrackBits(tile);
	res &= ~TrackToTrackBits(t);
	SetTrackReservation(tile, res);
}

/**
 * Get the reservation state of the depot
 * @pre IsRailDepot(t)
 * @param t the depot tile
 * @return reservation state
 */
inline bool HasDepotReservation(TileIndex t)
{
	dbg_assert_tile(IsRailDepot(t), t);
	return HasBit(_m[t].m5, 4);
}

/**
 * Set the reservation state of the depot
 * @pre IsRailDepot(t)
 * @param t the depot tile
 * @param b the reservation state
 */
inline void SetDepotReservation(TileIndex t, bool b)
{
	dbg_assert_tile(IsRailDepot(t), t);
	AssignBit(_m[t].m5, 4, b);
}

/**
 * Get the reserved track bits for a depot
 * @pre IsRailDepot(t)
 * @param t the tile
 * @return reserved track bits
 */
inline TrackBits GetDepotReservationTrackBits(TileIndex t)
{
	return HasDepotReservation(t) ? TrackToTrackBits(GetRailDepotTrack(t)) : TRACK_BIT_NONE;
}

inline SignalType GetSignalType(TileIndex t, Track track)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 0;
	return (SignalType)GB(_m[t].m2, pos, 3);
}

inline void SetSignalType(TileIndex t, Track track, SignalType s)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 0;
	SB(_m[t].m2, pos, 3, s);
	if (track == INVALID_TRACK) SB(_m[t].m2, 4, 3, s);
}

inline bool IsPresignalEntry(TileIndex t, Track track)
{
	return IsEntrySignal(GetSignalType(t, track));
}

inline bool IsPresignalExit(TileIndex t, Track track)
{
	return IsExitSignal(GetSignalType(t, track));
}

inline bool IsPresignalCombo(TileIndex t, Track track)
{
	return IsComboSignal(GetSignalType(t, track));
}

inline bool IsPresignalProgrammable(TileIndex t, Track track)
{
	return IsProgrammableSignal(GetSignalType(t, track));
}

inline bool IsNoEntrySignal(TileIndex t, Track track)
{
	return IsNoEntrySignal(GetSignalType(t, track));
}

/** One-way signals can't be passed the 'wrong' way. */
inline bool IsOnewaySignal(TileIndex t, Track track)
{
	return IsOnewaySignal(GetSignalType(t, track));
}

inline void CycleSignalSide(TileIndex t, Track track)
{
	uint8_t sig;
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 6;

	sig = GB(_m[t].m3, pos, 2);
	if (--sig == 0) sig = (IsPbsSignal(GetSignalType(t, track)) || _settings_game.vehicle.train_braking_model == TBM_REALISTIC) ? 2 : 3;
	SB(_m[t].m3, pos, 2, sig);
}

inline SignalVariant GetSignalVariant(TileIndex t, Track track)
{
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 7 : 3;
	return (SignalVariant)GB(_m[t].m2, pos, 1);
}

inline void SetSignalVariant(TileIndex t, Track track, SignalVariant v)
{
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 7 : 3;
	SB(_m[t].m2, pos, 1, v);
	if (track == INVALID_TRACK) SB(_m[t].m2, 7, 1, v);
}

inline uint8_t GetSignalAspect(TileIndex t, Track track)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 3 : 0;
	return GB(_me[t].m7, pos, 3);
}

inline void SetSignalAspect(TileIndex t, Track track, uint8_t aspect)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 3 : 0;
	SB(_me[t].m7, pos, 3, aspect);
}

inline bool NonZeroSignalStylePossiblyOnTile(TileIndex t)
{
	return _me[t].m6 != 0;
}

inline uint8_t GetSignalStyle(TileIndex t, Track track)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 0;
	return GB(_me[t].m6, pos, 4);
}

inline uint8_t GetSignalStyleGeneric(TileIndex t, Track track)
{
	switch (GetTileType(t)) {
		case MP_RAILWAY:
			return GetSignalStyle(t, track);
		case MP_TUNNELBRIDGE:
			return GetTunnelBridgeSignalStyle(t);
		default:
			return 0;
	}
}

inline void SetSignalStyle(TileIndex t, Track track, uint8_t style)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 4 : 0;
	SB(_me[t].m6, pos, 4, style);
}

inline bool GetSignalAlwaysReserveThrough(TileIndex t, Track track)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 7 : 6;
	return HasBit(_me[t].m7, pos);
}

inline void SetSignalAlwaysReserveThrough(TileIndex t, Track track, bool reserve_through)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 7 : 6;
	AssignBit(_me[t].m7, pos, reserve_through);
}

inline bool GetSignalSpecialPropagationFlag(TileIndex t, Track track)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 6 : 5;
	return HasBit(_m[t].m1, pos);
}

inline void SetSignalSpecialPropagationFlag(TileIndex t, Track track, bool special)
{
	dbg_assert_tile(GetRailTileType(t) == RAIL_TILE_SIGNALS, t);
	uint8_t pos = (track == TRACK_LOWER || track == TRACK_RIGHT) ? 6 : 5;
	AssignBit(_m[t].m1, pos, special);
}

/**
 * Set the states of the signals (Along/AgainstTrackDir)
 * @param tile  the tile to set the states for
 * @param state the new state
 */
inline void SetSignalStates(TileIndex tile, uint state)
{
	SB(_m[tile].m4, 4, 4, state);
}

/**
 * Set the states of the signals (Along/AgainstTrackDir)
 * @param tile  the tile to set the states for
 * @return the state of the signals
 */
inline uint GetSignalStates(TileIndex tile)
{
	return GB(_m[tile].m4, 4, 4);
}

/**
 * Get the state of a single signal
 * @param t         the tile to get the signal state for
 * @param signalbit the signal
 * @return the state of the signal
 */
inline SignalState GetSingleSignalState(TileIndex t, uint8_t signalbit)
{
	return (SignalState)HasBit(GetSignalStates(t), signalbit);
}

/**
 * Set whether the given signals are present (Along/AgainstTrackDir)
 * @param tile    the tile to set the present signals for
 * @param signals the signals that have to be present
 */
inline void SetPresentSignals(TileIndex tile, uint signals)
{
	SB(_m[tile].m3, 4, 4, signals);
}

/**
 * Get whether the given signals are present (Along/AgainstTrackDir)
 * @param tile the tile to get the present signals for
 * @return the signals that are present
 */
inline uint GetPresentSignals(TileIndex tile)
{
	return GB(_m[tile].m3, 4, 4);
}

/**
 * Checks whether the given signals is present
 * @param t         the tile to check on
 * @param signalbit the signal
 * @return true if and only if the signal is present
 */
inline bool IsSignalPresent(TileIndex t, uint8_t signalbit)
{
	return HasBit(GetPresentSignals(t), signalbit);
}

/**
 * Checks for the presence of signals (either way) on the given track on the
 * given rail tile.
 */
inline bool HasSignalOnTrack(TileIndex tile, Track track)
{
	dbg_assert(IsValidTrack(track));
	return GetRailTileType(tile) == RAIL_TILE_SIGNALS && (GetPresentSignals(tile) & SignalOnTrack(track)) != 0;
}

/**
 * Checks for the presence of signals along the given trackdir on the given
 * rail tile.
 *
 * Along meaning if you are currently driving on the given trackdir, this is
 * the signal that is facing us (for which we stop when it's red).
 */
inline bool HasSignalOnTrackdir(TileIndex tile, Trackdir trackdir)
{
	dbg_assert (IsValidTrackdir(trackdir));
	return GetRailTileType(tile) == RAIL_TILE_SIGNALS && GetPresentSignals(tile) & SignalAlongTrackdir(trackdir);
}

/**
 * Gets the state of the signal along the given trackdir.
 *
 * Along meaning if you are currently driving on the given trackdir, this is
 * the signal that is facing us (for which we stop when it's red).
 */
inline SignalState GetSignalStateByTrackdir(TileIndex tile, Trackdir trackdir)
{
	dbg_assert(IsValidTrackdir(trackdir));
	dbg_assert_tile(HasSignalOnTrack(tile, TrackdirToTrack(trackdir)), tile);
	return GetSignalStates(tile) & SignalAlongTrackdir(trackdir) ?
		SIGNAL_STATE_GREEN : SIGNAL_STATE_RED;
}

/**
 * Sets the state of the signal along the given trackdir.
 */
inline void SetSignalStateByTrackdir(TileIndex tile, Trackdir trackdir, SignalState state)
{
	if (state == SIGNAL_STATE_GREEN) { // set 1
		SetSignalStates(tile, GetSignalStates(tile) | SignalAlongTrackdir(trackdir));
	} else {
		SetSignalStates(tile, GetSignalStates(tile) & ~SignalAlongTrackdir(trackdir));
	}
}

/**
 * Is a pbs signal present along the trackdir?
 * @param tile the tile to check
 * @param td the trackdir to check
 */
inline bool HasPbsSignalOnTrackdir(TileIndex tile, Trackdir td)
{
	return IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, td) &&
			IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)));
}

/**
 * Is a one-way signal blocking the trackdir? A one-way signal on the
 * trackdir against will block, but signals on both trackdirs won't.
 * @param tile the tile to check
 * @param td the trackdir to check
 */
inline bool HasOnewaySignalBlockingTrackdir(TileIndex tile, Trackdir td)
{
	if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, ReverseTrackdir(td)) &&
			!HasSignalOnTrackdir(tile, td) && IsOnewaySignal(tile, TrackdirToTrack(td))) {
		return true;
	}
	if (IsTileType(tile, MP_RAILWAY) && HasSignalOnTrackdir(tile, td) &&
			IsNoEntrySignal(tile, TrackdirToTrack(td))) {
		return true;
	}
	if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExitOnly(tile) &&
			TrackdirEntersTunnelBridge(tile, td)) {
		return true;
	}
	return false;
}

/**
 * Does signal tile have "one or more trace restrict mappings present" bit set
 * @param tile the tile to check
 */
inline bool IsRestrictedSignal(TileIndex tile)
{
	dbg_assert_tile(GetRailTileType(tile) == RAIL_TILE_SIGNALS, tile);
	return (bool) GB(_m[tile].m2, 12, 1);
}

/**
 * Set signal tile "one or more trace restrict mappings present" bit
 * @param tile the tile to set
 */
inline void SetRestrictedSignal(TileIndex tile, bool is_restricted)
{
	dbg_assert_tile(GetRailTileType(tile) == RAIL_TILE_SIGNALS, tile);
	AssignBit(_m[tile].m2, 12, is_restricted);
}


RailType GetTileRailType(TileIndex tile);
RailType GenericGetRailTypeByTrack(TileIndex t, Track track, bool return_invalid);
RailType GenericGetRailTypeByTrackBit(TileIndex t, TrackBits track, bool return_invalid);
RailType GenericGetRailTypeByEntryDir(TileIndex t, DiagDirection enterdir, bool return_invalid);
RailType GetTileSecondaryRailTypeIfValid(TileIndex t);

inline RailType GetTileRailTypeByTrack(TileIndex t, Track track) { return GenericGetRailTypeByTrack(t, track, true); }
inline RailType GetTileRailTypeByTrackBit(TileIndex t, TrackBits track) { return GenericGetRailTypeByTrackBit(t, track, true); }
inline RailType GetTileRailTypeByEntryDir(TileIndex t, DiagDirection enterdir) { return GenericGetRailTypeByEntryDir(t, enterdir, true); }

inline RailType GetRailTypeByTrack(TileIndex t, Track track) { return GenericGetRailTypeByTrack(t, track, false); }
inline RailType GetRailTypeByTrackBit(TileIndex t, TrackBits track) { return GenericGetRailTypeByTrackBit(t, track, false); }
inline RailType GetRailTypeByEntryDir(TileIndex t, DiagDirection enterdir) { return GenericGetRailTypeByEntryDir(t, enterdir, false); }

/** The ground 'under' the rail */
enum RailGroundType : uint8_t {
	RAIL_GROUND_BARREN       =  0, ///< Nothing (dirt)
	RAIL_GROUND_GRASS        =  1, ///< Grassy
	RAIL_GROUND_FENCE_NW     =  2, ///< Grass with a fence at the NW edge
	RAIL_GROUND_FENCE_SE     =  3, ///< Grass with a fence at the SE edge
	RAIL_GROUND_FENCE_SENW   =  4, ///< Grass with a fence at the NW and SE edges
	RAIL_GROUND_FENCE_NE     =  5, ///< Grass with a fence at the NE edge
	RAIL_GROUND_FENCE_SW     =  6, ///< Grass with a fence at the SW edge
	RAIL_GROUND_FENCE_NESW   =  7, ///< Grass with a fence at the NE and SW edges
	RAIL_GROUND_FENCE_VERT1  =  8, ///< Grass with a fence at the eastern side
	RAIL_GROUND_FENCE_VERT2  =  9, ///< Grass with a fence at the western side
	RAIL_GROUND_FENCE_HORIZ1 = 10, ///< Grass with a fence at the southern side
	RAIL_GROUND_FENCE_HORIZ2 = 11, ///< Grass with a fence at the northern side
	RAIL_GROUND_ICE_DESERT   = 12, ///< Icy or sandy
	RAIL_GROUND_WATER        = 13, ///< Grass with a fence and shore or water on the free halftile
	RAIL_GROUND_HALF_SNOW    = 14, ///< Snow only on higher part of slope (steep or one corner raised)
};

inline void SetRailGroundType(TileIndex t, RailGroundType rgt)
{
	SB(_m[t].m4, 0, 4, rgt);
}

inline RailGroundType GetRailGroundType(TileIndex t)
{
	return (RailGroundType)GB(_m[t].m4, 0, 4);
}

inline bool IsSnowRailGround(TileIndex t)
{
	return GetRailGroundType(t) == RAIL_GROUND_ICE_DESERT;
}

RailGroundType GetTunnelBridgeGroundType(TileIndex tile);

inline void MakeRailNormal(TileIndex t, Owner o, TrackBits b, RailType r)
{
	SetTileType(t, MP_RAILWAY);
	SetTileOwner(t, o);
	SetDockingTile(t, false);
	_m[t].m2 = 0;
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = RAIL_TILE_NORMAL << 6 | b;
	SB(_me[t].m6, 2, 4, 0);
	_me[t].m7 = 0;
	_me[t].m8 = r;
}


inline void MakeRailDepot(TileIndex t, Owner o, DepotID did, DiagDirection d, RailType r)
{
	SetTileType(t, MP_RAILWAY);
	SetTileOwner(t, o);
	SetDockingTile(t, false);
	_m[t].m2 = did;
	_m[t].m3 = 0;
	_m[t].m4 = 0;
	_m[t].m5 = RAIL_TILE_DEPOT << 6 | d;
	SB(_me[t].m6, 2, 4, 0);
	_me[t].m7 = 0;
	_me[t].m8 = r;
}

#endif /* RAIL_MAP_H */
