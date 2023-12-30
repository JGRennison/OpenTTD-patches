/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signal.cpp functions related to rail signals updating */

#include "stdafx.h"
#include "debug.h"
#include "station_map.h"
#include "tunnelbridge_map.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "train.h"
#include "company_base.h"
#include "gui.h"
#include "table/strings.h"
#include "programmable_signals.h"
#include "error.h"
#include "infrastructure_func.h"
#include "tunnelbridge.h"
#include "bridge_signal_map.h"
#include "newgrf_newsignals.h"
#include "core/checksum_func.hpp"
#include "core/hash_func.hpp"
#include "pathfinder/follow_track.hpp"

#include "safeguards.h"

uint8 _extra_aspects = 0;
uint64 _aspect_cfg_hash = 0;
SignalStyleMasks _signal_style_masks = {};
bool _signal_sprite_oversized = false;

/// List of signals dependent upon this one
typedef std::vector<SignalReference>     SignalDependencyList;

/// Map of dependencies. The key identifies the signal,
/// the value is a list of all of the signals which depend upon that signal.
typedef std::map<SignalReference, SignalDependencyList> SignalDependencyMap;

static SignalDependencyMap _signal_dependencies;
static void MarkDependencidesForUpdate(SignalReference sig);

/** these are the maximums used for updating signal blocks */
static const uint SIG_TBU_SIZE    =  64; ///< number of signals entering to block
static const uint SIG_TBD_SIZE    = 256; ///< number of intersections - open nodes in current block
static const uint SIG_GLOB_SIZE   = 128; ///< number of open blocks (block can be opened more times until detected)
static const uint SIG_GLOB_UPDATE =  64; ///< how many items need to be in _globset to force update

static_assert(SIG_GLOB_UPDATE <= SIG_GLOB_SIZE);

/** incidating trackbits with given enterdir */
static const TrackBits _enterdir_to_trackbits[DIAGDIR_END] = {
	TRACK_BIT_3WAY_NE,
	TRACK_BIT_3WAY_SE,
	TRACK_BIT_3WAY_SW,
	TRACK_BIT_3WAY_NW
};

/** incidating trackdirbits with given enterdir */
static const TrackdirBits _enterdir_to_trackdirbits[DIAGDIR_END] = {
	TRACKDIR_BIT_X_SW | TRACKDIR_BIT_UPPER_W | TRACKDIR_BIT_RIGHT_S,
	TRACKDIR_BIT_Y_NW | TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_RIGHT_N,
	TRACKDIR_BIT_X_NE | TRACKDIR_BIT_LOWER_E | TRACKDIR_BIT_LEFT_N,
	TRACKDIR_BIT_Y_SE | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_LEFT_S
};

/**
 * Set containing 'items' items of 'tile and Tdir'
 * No tree structure is used because it would cause
 * slowdowns in most usual cases
 */
template <typename Tdir, uint items>
struct SmallSet {
private:
	uint n;           // actual number of units
	bool overflowed;  // did we try to overflow the set?
	const char *name; // name, used for debugging purposes...

	/** Element of set */
	struct SSdata {
		TileIndex tile;
		Tdir dir;
	} data[items];

public:
	/** Constructor - just set default values and 'name' */
	SmallSet(const char *name) : n(0), overflowed(false), name(name) { }

	/** Reset variables to default values */
	void Reset()
	{
		this->n = 0;
		this->overflowed = false;
	}

	/**
	 * Returns value of 'overflowed'
	 * @return did we try to overflow the set?
	 */
	bool Overflowed()
	{
		return this->overflowed;
	}

	/**
	 * Checks for empty set
	 * @return is the set empty?
	 */
	bool IsEmpty()
	{
		return this->n == 0;
	}

	/**
	 * Checks for full set
	 * @return is the set full?
	 */
	bool IsFull()
	{
		return this->n == lengthof(data);
	}

	/**
	 * Reads the number of items
	 * @return current number of items
	 */
	uint Items()
	{
		return this->n;
	}


	/**
	 * Tries to remove first instance of given tile and dir
	 * @param tile tile
	 * @param dir and dir to remove
	 * @return element was found and removed
	 */
	bool Remove(TileIndex tile, Tdir dir)
	{
		for (uint i = 0; i < this->n; i++) {
			if (this->data[i].tile == tile && this->data[i].dir == dir) {
				this->data[i] = this->data[--this->n];
				return true;
			}
		}

		return false;
	}

	/**
	 * Tries to find given tile and dir in the set
	 * @param tile tile
	 * @param dir and dir to find
	 * @return true iff the tile & dir element was found
	 */
	bool IsIn(TileIndex tile, Tdir dir)
	{
		for (uint i = 0; i < this->n; i++) {
			if (this->data[i].tile == tile && this->data[i].dir == dir) return true;
		}

		return false;
	}

	/**
	 * Adds tile & dir into the set, checks for full set
	 * Sets the 'overflowed' flag if the set was full
	 * @param tile tile
	 * @param dir and dir to add
	 * @return true iff the item could be added (set wasn't full)
	 */
	bool Add(TileIndex tile, Tdir dir)
	{
		if (this->IsFull()) {
			overflowed = true;
			DEBUG(misc, 0, "SignalSegment too complex. Set %s is full (maximum %d)", name, items);
			return false; // set is full
		}

		this->data[this->n].tile = tile;
		this->data[this->n].dir = dir;
		this->n++;

		return true;
	}

	/**
	 * Reads the last added element into the set
	 * @param tile pointer where tile is written to
	 * @param dir pointer where dir is written to
	 * @return false iff the set was empty
	 */
	bool Get(TileIndex *tile, Tdir *dir)
	{
		if (this->n == 0) return false;

		this->n--;
		*tile = this->data[this->n].tile;
		*dir = this->data[this->n].dir;

		return true;
	}
};

static SmallSet<Trackdir, SIG_TBU_SIZE> _tbuset("_tbuset");         ///< set of signals that will be updated
static SmallSet<Trackdir, SIG_TBU_SIZE> _tbpset("_tbpset");         ///< set of PBS signals to update the aspect of
static SmallSet<DiagDirection, SIG_TBD_SIZE> _tbdset("_tbdset");    ///< set of open nodes in current signal block
static SmallSet<DiagDirection, SIG_GLOB_SIZE> _globset("_globset"); ///< set of places to be updated in following runs

static uint _num_signals_evaluated; ///< Number of programmable pre-signals evaluated

/** Check whether there is a train on rail, not in a depot */
static Vehicle *TrainOnTileEnum(Vehicle *v, void *)
{
	if (Train::From(v)->track == TRACK_BIT_DEPOT) return nullptr;

	return v;
}

/** Check whether there is a train only on ramp. */
static Vehicle *TrainInWormholeTileEnum(Vehicle *v, void *data)
{
	/* Only look for front engine or last wagon. */
	if ((v->Previous() != nullptr && v->Next() != nullptr)) return nullptr;
	TileIndex tile = (TileIndex) reinterpret_cast<uintptr_t>(data);
	if (tile != TileVirtXY(v->x_pos, v->y_pos)) return nullptr;
	if (!(Train::From(v)->track & TRACK_BIT_WORMHOLE) && !(Train::From(v)->track & GetAcrossTunnelBridgeTrackBits(tile))) return nullptr;
	return v;
}

/**
 * Perform some operations before adding data into Todo set
 * The new and reverse direction is removed from _globset, because we are sure
 * it doesn't need to be checked again
 * Also, remove reverse direction from _tbdset
 * This is the 'core' part so the graph searching won't enter any tile twice
 *
 * @param t1 tile we are entering
 * @param d1 direction (tile side) we are entering
 * @param t2 tile we are leaving
 * @param d2 direction (tile side) we are leaving
 * @return false iff reverse direction was in Todo set
 */
static inline bool CheckAddToTodoSet(TileIndex t1, DiagDirection d1, TileIndex t2, DiagDirection d2)
{
	_globset.Remove(t1, d1); // it can be in Global but not in Todo
	_globset.Remove(t2, d2); // remove in all cases

	assert(!_tbdset.IsIn(t1, d1)); // it really shouldn't be there already

	return !_tbdset.Remove(t2, d2);
}


/**
 * Perform some operations before adding data into Todo set
 * The new and reverse direction is removed from Global set, because we are sure
 * it doesn't need to be checked again
 * Also, remove reverse direction from Todo set
 * This is the 'core' part so the graph searching won't enter any tile twice
 *
 * @param t1 tile we are entering
 * @param d1 direction (tile side) we are entering
 * @param t2 tile we are leaving
 * @param d2 direction (tile side) we are leaving
 * @return false iff the Todo buffer would be overrun
 */
static inline bool MaybeAddToTodoSet(TileIndex t1, DiagDirection d1, TileIndex t2, DiagDirection d2)
{
	if (!CheckAddToTodoSet(t1, d1, t2, d2)) return true;

	return _tbdset.Add(t1, d1);
}


/** Current signal block state flags */
enum SigFlags {
	SF_NONE    = 0,
	SF_TRAIN   = 1 << 0, ///< train found in segment
	SF_FULL    = 1 << 1, ///< some of buffers was full, do not continue
	SF_PBS     = 1 << 2, ///< pbs signal found
	SF_JUNCTION= 1 << 3, ///< junction found
};

DECLARE_ENUM_AS_BIT_SET(SigFlags)

struct SigInfo {
	inline SigInfo()
	{
		flags = SF_NONE;
		num_exits = 0;
		num_green = 0;
		out_signal_tile = INVALID_TILE;
		out_signal_trackdir = INVALID_TRACKDIR;
	}
	SigFlags flags;
	uint num_exits;
	uint num_green;
	TileIndex out_signal_tile;
	Trackdir out_signal_trackdir;
};

/**
 * Search signal block
 *
 * @param owner owner whose signals we are updating
 * @return SigFlags
 */
static SigInfo ExploreSegment(Owner owner)
{
	SigInfo info;

	TileIndex tile = INVALID_TILE; // Stop GCC from complaining about a possibly uninitialized variable (issue #8280).
	DiagDirection enterdir = INVALID_DIAGDIR;

	while (_tbdset.Get(&tile, &enterdir)) { // tile and enterdir are initialized here, unless I'm mistaken.
		TileIndex oldtile = tile; // tile we are leaving
		DiagDirection exitdir = enterdir == INVALID_DIAGDIR ? INVALID_DIAGDIR : ReverseDiagDir(enterdir); // expected new exit direction (for straight line)

		switch (GetTileType(tile)) {
			case MP_RAILWAY: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;

				if (IsRailDepot(tile)) {
					if (enterdir == INVALID_DIAGDIR) { // from 'inside' - train just entered or left the depot
						if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) info.flags |= SF_PBS;
						if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
						exitdir = GetRailDepotDirection(tile);
						tile += TileOffsByDiagDir(exitdir);
						enterdir = ReverseDiagDir(exitdir);
						break;
					} else if (enterdir == GetRailDepotDirection(tile)) { // entered a depot
						if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) info.flags |= SF_PBS;
						if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
						continue;
					} else {
						continue;
					}
				}

				assert(IsValidDiagDirection(enterdir));
				TrackBits tracks = GetTrackBits(tile); // trackbits of tile
				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir]); // only incidating trackbits

				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) { // there is exactly one incidating track, no need to check
					tracks = tracks_masked;
					/* If no train detected yet, and there is not no train -> there is a train -> set the flag */
					if (!(info.flags & SF_TRAIN) && EnsureNoTrainOnTrackBits(tile, tracks).Failed()) info.flags |= SF_TRAIN;
				} else {
					if (tracks_masked == TRACK_BIT_NONE) continue; // no incidating track
					if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
				}

				if (HasSignals(tile)) { // there is exactly one track - not zero, because there is exit from this tile
					Track track = TrackBitsToTrack(tracks_masked); // mask TRACK_BIT_X and Y too
					if (HasSignalOnTrack(tile, track)) { // now check whole track, not trackdir
						SignalType sig = GetSignalType(tile, track);
						Trackdir trackdir = (Trackdir)FindFirstBit((tracks * 0x101) & _enterdir_to_trackdirbits[enterdir]);
						Trackdir reversedir = ReverseTrackdir(trackdir);
						/* add (tile, reversetrackdir) to 'to-be-updated' set when there is
						 * ANY conventional signal in REVERSE direction
						 * (if it is a presignal EXIT and it changes, it will be added to 'to-be-done' set later) */
						if (HasSignalOnTrackdir(tile, reversedir)) {
							if (IsPbsSignalNonExtended(sig)) {
								info.flags |= SF_PBS;
								if (_extra_aspects > 0 && GetSignalStateByTrackdir(tile, reversedir) == SIGNAL_STATE_GREEN && !IsRailSpecialSignalAspect(tile, track)) {
									_tbpset.Add(tile, reversedir);
								}
							} else if (!_tbuset.Add(tile, reversedir)) {
								info.flags |= SF_FULL;
								return info;
							}
						}

						if (HasSignalOnTrackdir(tile, trackdir)) {
							if (!IsOnewaySignal(sig)) info.flags |= SF_PBS;
							if (_extra_aspects > 0) {
								info.out_signal_tile = tile;
								info.out_signal_trackdir = trackdir;
								if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && GetSignalAlwaysReserveThrough(tile, track) &&
										GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_RED) {
									info.flags |= SF_PBS;
								}
							}

							/* if it is a presignal EXIT in OUR direction, count it */
							if (IsExitSignal(sig)) { // found presignal exit
								info.num_exits++;
								if (GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN) { // found green presignal exit
									info.num_green++;
								}
							}
						}

						continue;
					}
				} else if (!HasAtMostOneBit(tracks)) {
					info.flags |= SF_JUNCTION;
				}

				for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) { // test all possible exit directions
					if (dir != enterdir && (tracks & _enterdir_to_trackbits[dir])) { // any track incidating?
						TileIndex newtile = tile + TileOffsByDiagDir(dir);  // new tile to check
						DiagDirection newdir = ReverseDiagDir(dir); // direction we are entering from
						if (!MaybeAddToTodoSet(newtile, newdir, tile, dir)) {
							info.flags |= SF_FULL;
							return info;
						}
					}
				}

				continue; // continue the while() loop
				}

			case MP_STATION:
				if (!HasStationRail(tile)) continue;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;
				if (DiagDirToAxis(enterdir) != GetRailStationAxis(tile)) continue; // different axis
				if (IsStationTileBlocked(tile)) continue; // 'eye-candy' station tile

				if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_ROAD:
				if (!IsLevelCrossing(tile)) continue;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;
				if (DiagDirToAxis(enterdir) == GetCrossingRoadAxis(tile)) continue; // different axis

				if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
				if (_settings_game.vehicle.safer_crossings) info.flags |= SF_PBS;
				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_TUNNELBRIDGE: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) continue;
				DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(tile);

				if (enterdir == tunnel_bridge_dir) continue;

				TrackBits tracks = GetTunnelBridgeTrackBits(tile);
				TrackBits across_tracks = GetAcrossTunnelBridgeTrackBits(tile);

				auto check_train_present = [tile, tracks, across_tracks](DiagDirection enterdir) -> bool {
					if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) {
						if (_enterdir_to_trackbits[enterdir] & across_tracks) {
							return EnsureNoTrainOnTrackBits(tile, TRACK_BIT_WORMHOLE | across_tracks).Failed();
						} else {
							return EnsureNoTrainOnTrackBits(tile, tracks & (~across_tracks)).Failed();
						}
					} else {
						return HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum);
					}
				};

				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir == INVALID_DIAGDIR ? tunnel_bridge_dir : enterdir]); // only incidating trackbits
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (IsTunnelBridgeWithSignalSimulation(tile)) {
					if (enterdir == INVALID_DIAGDIR) {
						// incoming from the wormhole, onto signal
						if (!(info.flags & SF_TRAIN) && IsTunnelBridgeSignalSimulationExit(tile)) { // tunnel entrance is ignored
							if (HasVehicleOnPos(GetOtherTunnelBridgeEnd(tile), VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
						}
						if (IsTunnelBridgeSignalSimulationExit(tile) && !_tbuset.Add(tile, INVALID_TRACKDIR)) {
							info.flags |= SF_FULL;
							return info;
						}
						if (_extra_aspects > 0 && IsTunnelBridgeSignalSimulationEntrance(tile)) {
							info.out_signal_tile = tile;
							info.out_signal_trackdir = GetTunnelBridgeEntranceTrackdir(tile, tunnel_bridge_dir);
						}
						Trackdir exit_track = GetTunnelBridgeExitTrackdir(tile, tunnel_bridge_dir);
						exitdir = TrackdirToExitdir(exit_track);
						enterdir = ReverseDiagDir(exitdir);
						tile += TileOffsByDiagDir(exitdir); // just skip to next tile
						break;
					} else if (_enterdir_to_trackbits[enterdir] & GetAcrossTunnelBridgeTrackBits(tile)) {
						// NOT incoming from the wormhole!
						if (IsTunnelBridgeSignalSimulationExit(tile)) {
							if (IsTunnelBridgePBS(tile)) {
								info.flags |= SF_PBS;
								if (_extra_aspects > 0 && GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN) {
									Trackdir exit_td = GetTunnelBridgeExitTrackdir(tile, tunnel_bridge_dir);
									_tbpset.Add(tile, exit_td);
								}
							} else if (!_tbuset.Add(tile, INVALID_TRACKDIR)) {
								info.flags |= SF_FULL;
								return info;
							}
						}
						if (_extra_aspects > 0 && IsTunnelBridgeSignalSimulationEntrance(tile)) {
							info.out_signal_tile = tile;
							info.out_signal_trackdir = GetTunnelBridgeEntranceTrackdir(tile, tunnel_bridge_dir);
						}
						if (!(info.flags & SF_TRAIN)) {
							if (HasVehicleOnPos(tile, VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							if (!(info.flags & SF_TRAIN) && IsTunnelBridgeSignalSimulationExit(tile)) {
								if (HasVehicleOnPos(GetOtherTunnelBridgeEnd(tile), VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							}
						}
						continue;
					}
				} else if (!HasAtMostOneBit(tracks)) {
					info.flags |= SF_JUNCTION;
				}
				if (enterdir == INVALID_DIAGDIR) { // incoming from the wormhole
					if (!(info.flags & SF_TRAIN) && check_train_present(tunnel_bridge_dir)) info.flags |= SF_TRAIN;
					enterdir = tunnel_bridge_dir;
				} else if (enterdir != tunnel_bridge_dir) { // NOT incoming from the wormhole!
					if (tracks_masked == TRACK_BIT_NONE) continue; // no incidating track
					if (!(info.flags & SF_TRAIN) && check_train_present(enterdir)) info.flags |= SF_TRAIN;
				}
				for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) { // test all possible exit directions
					if (dir != enterdir && (tracks & _enterdir_to_trackbits[dir])) { // any track incidating?
						if (dir == tunnel_bridge_dir) {
							if (!MaybeAddToTodoSet(GetOtherTunnelBridgeEnd(tile), INVALID_DIAGDIR, tile, INVALID_DIAGDIR)) {
								info.flags |= SF_FULL;
								return info;
							}
						} else {
							TileIndex newtile = tile + TileOffsByDiagDir(dir);  // new tile to check
							DiagDirection newdir = ReverseDiagDir(dir); // direction we are entering from
							if (!MaybeAddToTodoSet(newtile, newdir, tile, dir)) {
								info.flags |= SF_FULL;
								return info;
							}
						}
					}
				}
				continue; // continue the while() loop
				}

			default:
				continue; // continue the while() loop
		}

		if (!MaybeAddToTodoSet(tile, enterdir, oldtile, exitdir)) {
			info.flags |= SF_FULL;
		}
	}

	return info;
}

static uint8 GetSignalledTunnelBridgeEntranceForwardAspect(TileIndex tile, TileIndex tile_exit)
{
	if (!IsTunnelBridgeSignalSimulationEntrance(tile)) return 0;
	const uint spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
	const uint signal_count = GetTunnelBridgeLength(tile, tile_exit) / spacing;
	if (IsBridge(tile)) {
		uint8 aspect = 0;
		for (uint i = 0; i < signal_count; i++) {
			if (GetBridgeEntranceSimulatedSignalState(tile, i) == SIGNAL_STATE_GREEN) {
				aspect++;
			} else {
				return std::min<uint>(aspect, GetMaximumSignalAspect());
			}
		}
		if (GetTunnelBridgeExitSignalState(tile_exit) == SIGNAL_STATE_GREEN) aspect += GetTunnelBridgeExitSignalAspect(tile_exit);
		return std::min<uint>(aspect, GetMaximumSignalAspect());
	} else {
		int free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(tile, tile_exit, tile);
		if (free_tiles == INT_MAX) {
			uint aspect = signal_count;
			if (GetTunnelBridgeExitSignalState(tile_exit) == SIGNAL_STATE_GREEN) aspect += GetTunnelBridgeExitSignalAspect(tile_exit);
			return std::min<uint>(aspect, GetMaximumSignalAspect());
		} else {
			if (free_tiles < (int)spacing) return 0;
			return std::min<uint>((free_tiles / spacing) - 1, GetMaximumSignalAspect());
		}
	}
}

uint8 GetForwardAspectFollowingTrack(TileIndex tile, Trackdir trackdir)
{
	Owner owner = GetTileOwner(tile);
	DiagDirection exitdir = TrackdirToExitdir(trackdir);
	DiagDirection enterdir = ReverseDiagDir(exitdir);
	bool wormhole = false;
	if (IsTileType(tile, MP_TUNNELBRIDGE) && TrackdirEntersTunnelBridge(tile, trackdir)) {
		TileIndex other = GetOtherTunnelBridgeEnd(tile);
		if (IsTunnelBridgeWithSignalSimulation(tile)) {
			return GetSignalledTunnelBridgeEntranceForwardAspect(tile, other);
		}
		tile = other;
		wormhole = true;
	} else {
		tile += TileOffsByDiagDir(exitdir);
	}
	while (true) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return 0;

				if (IsRailDepot(tile)) {
					return 0;
				}

				TrackBits tracks = GetTrackBits(tile); // trackbits of tile
				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir]); // only incidating trackbits

				if (tracks_masked == TRACK_BIT_NONE) return 0; // no incidating track
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (!HasAtMostOneBit(tracks)) {
					TrackBits reserved_bits = GetRailReservationTrackBits(tile) & tracks_masked;
					if (reserved_bits == TRACK_BIT_NONE) return 0; // no reservation on junction
					tracks = reserved_bits;
				}

				Track track = (Track)FIND_FIRST_BIT(tracks);
				trackdir = TrackEnterdirToTrackdir(track, ReverseDiagDir(enterdir));

				if (HasSignals(tile)) {
					if (HasSignalOnTrack(tile, track)) { // now check whole track, not trackdir
						if (HasSignalOnTrackdir(tile, trackdir)) {
							if (GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_RED) return 0;
							uint8 aspect = GetSignalAspect(tile, track);
							AdjustSignalAspectIfNonIncStyle(tile, track, aspect);
							return aspect;
						} else if (IsOnewaySignal(tile, track)) {
							return 0; // one-way signal facing the wrong way
						}
					}
				}

				exitdir = TrackdirToExitdir(trackdir);
				enterdir = ReverseDiagDir(exitdir);
				tile += TileOffsByDiagDir(exitdir);

				break;
			}

			case MP_STATION:
				if (!HasStationRail(tile)) return 0;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return 0;
				if (DiagDirToAxis(enterdir) != GetRailStationAxis(tile)) return 0; // different axis
				if (IsStationTileBlocked(tile)) return 0; // 'eye-candy' station tile

				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_ROAD:
				if (!IsLevelCrossing(tile)) return 0;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return 0;
				if (DiagDirToAxis(enterdir) == GetCrossingRoadAxis(tile)) return 0; // different axis

				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_TUNNELBRIDGE: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return 0;
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) return 0;
				if ((enterdir == GetTunnelBridgeDirection(tile)) != wormhole) return 0;

				TrackBits tracks = GetTunnelBridgeTrackBits(tile); // trackbits of tile
				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir]); // only incidating trackbits

				if (tracks_masked == TRACK_BIT_NONE) return 0; // no incidating track
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (!HasAtMostOneBit(tracks)) {
					TrackBits reserved_bits = GetTunnelBridgeReservationTrackBits(tile) & tracks_masked;
					if (reserved_bits == TRACK_BIT_NONE) return 0; // no reservation on junction
					tracks = reserved_bits;
				}

				Track track = (Track)FIND_FIRST_BIT(tracks);
				trackdir = TrackEnterdirToTrackdir(track, ReverseDiagDir(enterdir));

				if (IsTunnelBridgeWithSignalSimulation(tile) && HasTrack(GetAcrossTunnelBridgeTrackBits(tile), track)) {
					return GetSignalAspectGeneric(tile, trackdir, false);
				}

				if (TrackdirEntersTunnelBridge(tile, trackdir)) {
					tile = GetOtherTunnelBridgeEnd(tile);
					enterdir = GetTunnelBridgeDirection(tile);
					exitdir = ReverseDiagDir(enterdir);
					wormhole = true;
				} else {
					exitdir = TrackdirToExitdir(trackdir);
					enterdir = ReverseDiagDir(exitdir);
					tile += TileOffsByDiagDir(exitdir);
					wormhole = false;
				}
				break;
			}

			default:
				return 0;
		}
	}
}

static uint8 GetForwardAspect(const SigInfo &info, TileIndex tile, Trackdir trackdir)
{
	if (info.flags & (SF_JUNCTION | SF_PBS)) {
		return GetForwardAspectFollowingTrack(tile, trackdir);
	} else {
		return (info.out_signal_tile != INVALID_TILE) ? GetSignalAspectGeneric(info.out_signal_tile, info.out_signal_trackdir, true) : 0;
	}
}

static uint8 GetForwardAspectAndIncrement(const SigInfo &info, TileIndex tile, Trackdir trackdir, bool combined_normal_mode = false)
{
	return IncrementAspectForSignal(GetForwardAspect(info, tile, trackdir), combined_normal_mode);
}

static inline bool IsRailCombinedNormalShuntSignalStyle(TileIndex tile, Track track)
{
	return _signal_style_masks.combined_normal_shunt != 0 && HasBit(_signal_style_masks.combined_normal_shunt, GetSignalStyle(tile, track));
}

/**
 * Update signals around segment in _tbuset
 *
 * @param flags info about segment
 */
static void UpdateSignalsAroundSegment(SigInfo info)
{
	TileIndex tile = INVALID_TILE; // Stop GCC from complaining about a possibly uninitialized variable (issue #8280).
	Trackdir trackdir = INVALID_TRACKDIR;
	Track track = INVALID_TRACK;

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		if (_tbuset.Items() > 1) info.flags |= SF_PBS;
		if (info.flags & (SF_PBS | SF_JUNCTION)) info.flags |= SF_TRAIN;
	}

	while (_tbuset.Get(&tile, &trackdir)) {
		if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(tile)) {
			if (IsTunnelBridgePBS(tile) || (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && HasAcrossTunnelBridgeReservation(tile))) {
				if (_extra_aspects > 0 && GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN) {
					Trackdir exit_td = GetTunnelBridgeExitTrackdir(tile);
					uint8 aspect = GetForwardAspectAndIncrement(info, tile, exit_td);
					if (aspect != GetTunnelBridgeExitSignalAspect(tile)) {
						SetTunnelBridgeExitSignalAspect(tile, aspect);
						MarkTunnelBridgeSignalDirty(tile, true);
						PropagateAspectChange(tile, exit_td, aspect);
					}
				}
				continue;
			}
			SignalState old_state = GetTunnelBridgeExitSignalState(tile);
			SignalState new_state = (info.flags & SF_TRAIN) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
			bool refresh = false;
			if (old_state != new_state) {
				SetTunnelBridgeExitSignalState(tile, new_state);
				refresh = true;
			}
			if (_extra_aspects > 0) {
				const uint8 current_aspect = (old_state == SIGNAL_STATE_GREEN) ? GetTunnelBridgeExitSignalAspect(tile) : 0;
				uint8 aspect;
				if (new_state == SIGNAL_STATE_GREEN) {
					aspect = GetForwardAspectAndIncrement(info, tile, trackdir);
				} else {
					aspect = 0;
				}
				if (aspect != current_aspect || old_state != new_state) {
					if (new_state == SIGNAL_STATE_GREEN) SetTunnelBridgeExitSignalAspect(tile, aspect);
					refresh = true;
					Trackdir exit_td = GetTunnelBridgeExitTrackdir(tile);
					PropagateAspectChange(tile, exit_td, aspect);
				}
			}
			if (refresh) MarkTunnelBridgeSignalDirty(tile, true);

			continue;
		}

		assert_msg_tile(HasSignalOnTrackdir(tile, trackdir), tile, "trackdir: %u", trackdir);

		track = TrackdirToTrack(trackdir);
		SignalType sig = GetSignalType(tile, track);
		SignalState newstate = SIGNAL_STATE_GREEN;

		/* don't change signal state if tile is reserved in realistic braking mode */
		if ((_settings_game.vehicle.train_braking_model == TBM_REALISTIC && HasBit(GetRailReservationTrackBits(tile), track))) {
			if (_extra_aspects > 0 && GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN && !IsRailSpecialSignalAspect(tile, track)) {
				uint8 aspect = GetForwardAspectAndIncrement(info, tile, trackdir, IsRailCombinedNormalShuntSignalStyle(tile, TrackdirToTrack(trackdir)));
				uint8 old_aspect = GetSignalAspect(tile, track);
				if (aspect != old_aspect) {
					SetSignalAspect(tile, track, aspect);
					if (old_aspect != 0) MarkSingleSignalDirty(tile, trackdir);
					PropagateAspectChange(tile, trackdir, aspect);
				}
			}
			continue;
		}

		/* determine whether the new state is red */
		if (info.flags & SF_TRAIN || sig == SIGTYPE_NO_ENTRY) {
			/* train in the segment */
			newstate = SIGNAL_STATE_RED;
		} else if (sig == SIGTYPE_PROG &&
				_num_signals_evaluated > _settings_game.construction.maximum_signal_evaluations) {
			/* too many cascades */
			newstate = SIGNAL_STATE_RED;
		} else {
			/* is it a bidir combo? - then do not count its other signal direction as exit */
			if (IsComboSignal(sig) && HasSignalOnTrackdir(tile, ReverseTrackdir(trackdir))) {
				// Don't count ourselves
				uint exits = info.num_exits - 1;
				uint green = info.num_green;
				if (GetSignalStateByTrackdir(tile, ReverseTrackdir(trackdir)) == SIGNAL_STATE_GREEN)
					green--;

				if (sig == SIGTYPE_PROG) { /* Programmable */
					_num_signals_evaluated++;

					if (!RunSignalProgram(SignalReference(tile, track), exits, green))
						newstate = SIGNAL_STATE_RED;
				} else { /* traditional combo */
					if (!green && exits)
						newstate = SIGNAL_STATE_RED;
				}
			} else { // entry, at least one exit, no green exit
				if (IsEntrySignal(sig)) {
					if (sig == SIGTYPE_PROG) {
						_num_signals_evaluated++;
						if (!RunSignalProgram(SignalReference(tile, track), info.num_exits, info.num_green))
							newstate = SIGNAL_STATE_RED;
					} else { /* traditional combo */
						if (!info.num_green && info.num_exits) newstate = SIGNAL_STATE_RED;
					}
				}
			}
		}

		bool refresh = false;
		const SignalState current_state = GetSignalStateByTrackdir(tile, trackdir);

		if (_extra_aspects > 0) {
			const uint8 current_aspect = (current_state == SIGNAL_STATE_GREEN) ? GetSignalAspect(tile, track) : 0;
			uint8 aspect;
			if (newstate == SIGNAL_STATE_GREEN) {
				aspect = 1;
				if (info.out_signal_tile != INVALID_TILE) {
					/* Combined normal/shunt signals should never be encountered here as they are PBS-only and so will never be green if not reserved */
					aspect = IncrementAspectForSignal(GetSignalAspectGeneric(info.out_signal_tile, info.out_signal_trackdir, true), false);
				}
			} else {
				aspect = 0;
			}
			if (aspect != current_aspect || newstate != current_state) {
				SetSignalAspect(tile, track, aspect);
				refresh = true;
				PropagateAspectChange(tile, trackdir, aspect);
			}
		}

		/* only when the state changes */
		if (newstate != current_state) {
			if (IsExitSignal(sig)) {
				/* for pre-signal exits, add block to the global set */
				DiagDirection exitdir = TrackdirToExitdir(ReverseTrackdir(trackdir));
				_globset.Add(tile, exitdir); // do not check for full global set, first update all signals

				// Progsig dependencies
				MarkDependencidesForUpdate(SignalReference(tile, track));
			} else if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && GetSignalAlwaysReserveThrough(tile, track)) {
				/* for reserve through signals, add block to the global set */
				DiagDirection exitdir = TrackdirToExitdir(ReverseTrackdir(trackdir));
				_globset.Add(tile, exitdir); // do not check for full global set, first update all signals
			}
			SetSignalStateByTrackdir(tile, trackdir, newstate);
			refresh = true;
		}
		if (refresh) {
			MarkSingleSignalDirty(tile, trackdir);
		}
	}

	while (_tbpset.Get(&tile, &trackdir)) {
		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			uint8 aspect = GetForwardAspectAndIncrement(info, tile, trackdir);
			uint8 old_aspect = GetTunnelBridgeExitSignalAspect(tile);
			if (aspect != old_aspect) {
				SetTunnelBridgeExitSignalAspect(tile, aspect);
				if (old_aspect != 0) MarkTunnelBridgeSignalDirty(tile, true);
				PropagateAspectChange(tile, trackdir, aspect);
			}
		} else {
			uint8 aspect = GetForwardAspectAndIncrement(info, tile, trackdir, IsRailCombinedNormalShuntSignalStyle(tile, TrackdirToTrack(trackdir)));
			uint8 old_aspect = GetSignalAspect(tile, track);
			Track track = TrackdirToTrack(trackdir);
			if (aspect != old_aspect) {
				SetSignalAspect(tile, track, aspect);
				if (old_aspect != 0) MarkSingleSignalDirty(tile, trackdir);
				PropagateAspectChange(tile, trackdir, aspect);
			}
		}
	}
}


/** Reset all sets after one set overflowed */
static inline void ResetSets()
{
	_tbuset.Reset();
	_tbpset.Reset();
	_tbdset.Reset();
	_globset.Reset();
}


/**
 * Updates blocks in _globset buffer
 *
 * @param owner company whose signals we are updating
 * @return state of the first block from _globset
 * @pre Company::IsValidID(owner)
 */
static SigSegState UpdateSignalsInBuffer(Owner owner)
{
	assert(Company::IsValidID(owner));

	bool first = true;  // first block?
	SigSegState state = SIGSEG_FREE; // value to return
	_num_signals_evaluated = 0;

	TileIndex tile = INVALID_TILE; // Stop GCC from complaining about a possibly uninitialized variable (issue #8280).
	DiagDirection dir = INVALID_DIAGDIR;

	while (_globset.Get(&tile, &dir)) {
		assert(_tbuset.IsEmpty());
		assert(_tbdset.IsEmpty());

		/* After updating signal, data stored are always MP_RAILWAY with signals.
		 * Other situations happen when data are from outside functions -
		 * modification of railbits (including both rail building and removal),
		 * train entering/leaving block, train leaving depot...
		 */
		switch (GetTileType(tile)) {
			case MP_TUNNELBRIDGE: {
				/* 'optimization assert' - do not try to update signals when it is not needed */
				assert_tile(GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL, tile);
				if (IsTunnel(tile)) assert(dir == INVALID_DIAGDIR || dir == ReverseDiagDir(GetTunnelBridgeDirection(tile)));
				TrackBits across = GetAcrossTunnelBridgeTrackBits(tile);
				if (dir == INVALID_DIAGDIR || _enterdir_to_trackbits[dir] & across) {
					if (IsTunnelBridgeWithSignalSimulation(tile)) {
						/* Don't worry about other side of tunnel. */
						_tbdset.Add(tile, dir);
					} else {
						_tbdset.Add(tile, INVALID_DIAGDIR);  // we can safely start from wormhole centre
						_tbdset.Add(GetOtherTunnelBridgeEnd(tile), INVALID_DIAGDIR);
					}
					break;
				}
			}
				FALLTHROUGH;

			case MP_RAILWAY:
				if (IsRailDepotTile(tile)) {
					/* 'optimization assert' do not try to update signals in other cases */
					assert(dir == INVALID_DIAGDIR || dir == GetRailDepotDirection(tile));
					_tbdset.Add(tile, INVALID_DIAGDIR); // start from depot inside
					break;
				}
				FALLTHROUGH;

			case MP_STATION:
			case MP_ROAD:
				if ((TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & _enterdir_to_trackbits[dir]) != TRACK_BIT_NONE) {
					/* only add to set when there is some 'interesting' track */
					_tbdset.Add(tile, dir);
					_tbdset.Add(tile + TileOffsByDiagDir(dir), ReverseDiagDir(dir));
					break;
				}
				FALLTHROUGH;

			default:
				/* jump to next tile */
				tile = tile + TileOffsByDiagDir(dir);
				dir = ReverseDiagDir(dir);
				if ((TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & _enterdir_to_trackbits[dir]) != TRACK_BIT_NONE) {
					_tbdset.Add(tile, dir);
					break;
				}
				/* happens when removing a rail that wasn't connected at one or both sides */
				continue; // continue the while() loop
		}

		assert(!_tbdset.Overflowed()); // it really shouldn't overflow by these one or two items
		assert(!_tbdset.IsEmpty()); // it wouldn't hurt anyone, but shouldn't happen too

		SigInfo info = ExploreSegment(owner);

		if (first) {
			first = false;
			/* SIGSEG_FREE is set by default */
			if (info.flags & SF_PBS) {
				state = SIGSEG_PBS;
			} else if ((info.flags & SF_TRAIN) || ((info.num_exits) && !(info.num_green)) || (info.flags & SF_FULL)) {
				state = SIGSEG_FULL;
			}
		}

		/* do not do anything when some buffer was full */
		if (info.flags & SF_FULL) {
			ResetSets(); // free all sets
			break;
		}

		if (_num_signals_evaluated > _settings_game.construction.maximum_signal_evaluations) {
			ShowErrorMessage(STR_ERROR_SIGNAL_CHANGES, STR_EMPTY, WL_INFO);
		}

		UpdateSignalsAroundSegment(info);
	}

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) state = SIGSEG_PBS;

	return state;
}


static Owner _last_owner = INVALID_OWNER; ///< last owner whose track was put into _globset


/**
 * Update signals in buffer
 * Called from 'outside'
 */
void UpdateSignalsInBuffer()
{
	if (!_globset.IsEmpty()) {
		UpdateSignalsInBuffer(_last_owner);
		_last_owner = INVALID_OWNER; // invalidate
	}
}

/**
 * Update signals in buffer if the owner could not be added to the current buffer
 * Called from 'outside'
 */
void UpdateSignalsInBufferIfOwnerNotAddable(Owner owner)
{
	if (!_globset.IsEmpty() && !IsOneSignalBlock(owner, _last_owner)) {
		UpdateSignalsInBuffer(_last_owner);
		_last_owner = INVALID_OWNER; // invalidate
	}
}


/**
 * Add track to signal update buffer
 *
 * @param tile tile where we start
 * @param track track at which ends we will update signals
 * @param owner owner whose signals we will update
 */
void AddTrackToSignalBuffer(TileIndex tile, Track track, Owner owner)
{
	static const DiagDirection _search_dir_1[] = {
		DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_NE, DIAGDIR_SE, DIAGDIR_SW, DIAGDIR_SE
	};
	static const DiagDirection _search_dir_2[] = {
		DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NW, DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NE
	};

	/* do not allow signal updates for two companies in one run,
	 * if these companies are not part of the same signal block */
	assert(_globset.IsEmpty() || IsOneSignalBlock(owner, _last_owner));

	_last_owner = owner;

	DiagDirection wormhole_dir = IsTileType(tile, MP_TUNNELBRIDGE) ? GetTunnelBridgeDirection(tile) : INVALID_DIAGDIR;

	auto add_dir = [&](DiagDirection dir) {
		_globset.Add(tile, dir == wormhole_dir ? INVALID_DIAGDIR : dir);
	};
	add_dir(_search_dir_1[track]);
	add_dir(_search_dir_2[track]);

	if (_globset.Items() >= SIG_GLOB_UPDATE) {
		/* too many items, force update */
		UpdateSignalsInBuffer(_last_owner);
		_last_owner = INVALID_OWNER;
	}
}


/**
 * Add side of tile to signal update buffer
 *
 * @param tile tile where we start
 * @param side side of tile
 * @param owner owner whose signals we will update
 */
void AddSideToSignalBuffer(TileIndex tile, DiagDirection side, Owner owner)
{
	/* do not allow signal updates for two companies in one run,
	 * if these companies are not part of the same signal block */
	assert(_globset.IsEmpty() || IsOneSignalBlock(owner, _last_owner));

	_last_owner = owner;

	_globset.Add(tile, side);

	if (_globset.Items() >= SIG_GLOB_UPDATE) {
		/* too many items, force update */
		UpdateSignalsInBuffer(_last_owner);
		_last_owner = INVALID_OWNER;
	}
}

/**
 * Update signals, starting at one side of a tile
 * Will check tile next to this at opposite side too
 *
 * @see UpdateSignalsInBuffer()
 * @param tile tile where we start
 * @param side side of tile
 * @param owner owner whose signals we will update
 * @return the state of the signal segment
 */
SigSegState UpdateSignalsOnSegment(TileIndex tile, DiagDirection side, Owner owner)
{
	UpdateSignalsInBufferIfOwnerNotAddable(owner);
	_globset.Add(tile, side);

	_last_owner = INVALID_OWNER;
	return UpdateSignalsInBuffer(owner);
}


/**
 * Update signals at segments that are at both ends of
 * given (existent or non-existent) track
 *
 * @see UpdateSignalsInBuffer()
 * @param tile tile where we start
 * @param track track at which ends we will update signals
 * @param owner owner whose signals we will update
 */
void SetSignalsOnBothDir(TileIndex tile, Track track, Owner owner)
{
	assert(_globset.IsEmpty());

	AddTrackToSignalBuffer(tile, track, owner);
	UpdateSignalsInBuffer(owner);
}

void AddSignalDependency(SignalReference on, SignalReference dep)
{
	assert(GetTileOwner(on.tile) == GetTileOwner(dep.tile));
	SignalDependencyList &dependencies = _signal_dependencies[on];
	dependencies.push_back(dep);
}

void RemoveSignalDependency(SignalReference on, SignalReference dep)
{
	SignalDependencyList &dependencies = _signal_dependencies[on];
	auto ob = std::find(dependencies.begin(), dependencies.end(), dep);

	// Destroying both signals in same command
	if (ob == dependencies.end()) return;

	dependencies.erase(ob);
	if (dependencies.size() == 0) {
		_signal_dependencies.erase(on);
	}
}

void FreeSignalDependencies()
{
	_signal_dependencies.clear();
}

void UpdateSignalDependency(SignalReference sr)
{
	Trackdir td = TrackToTrackdir(sr.track);
	_globset.Add(sr.tile, TrackdirToExitdir(td));
	_globset.Add(sr.tile, TrackdirToExitdir(ReverseTrackdir(td)));
}

static void MarkDependencidesForUpdate(SignalReference on)
{
	SignalDependencyMap::iterator f = _signal_dependencies.find(on);
	if (f == _signal_dependencies.end()) return;

	SignalDependencyList &dependencies = f->second;
	for (const SignalReference &sr : dependencies) {
		assert(GetTileOwner(sr.tile) == GetTileOwner(on.tile));

		UpdateSignalDependency(sr);
	}
}

void CheckRemoveSignalsFromTile(TileIndex tile)
{
	if (!HasSignals(tile)) return;

	TrackBits tb = GetTrackBits(tile);
	Track tr;
	while ((tr = RemoveFirstTrack(&tb)) != INVALID_TRACK) {
		if (HasSignalOnTrack(tile, tr)) CheckRemoveSignal(tile, tr);
	}
}

static void NotifyRemovingDependentSignal(SignalReference being_removed, SignalReference dependant)
{
	SignalType t = GetSignalType(dependant.tile, dependant.track);
	if (IsProgrammableSignal(t)) {
		RemoveProgramDependencies(being_removed, dependant);
	} else {
		DEBUG(misc, 0, "Removing dependency held by non-programmable signal (Unexpected)");
	}
}

void CheckRemoveSignal(TileIndex tile, Track track)
{
	if (!HasSignalOnTrack(tile, track)) return;
	SignalReference thisRef(tile, track);

	SignalType t = GetSignalType(tile, track);
	if (IsProgrammableSignal(t)) {
		FreeSignalProgram(thisRef);
	}

	SignalDependencyMap::iterator i = _signal_dependencies.find(SignalReference(tile, track)),
			e = _signal_dependencies.end();

	if (i != e) {
		SignalDependencyList &dependencies = i->second;

		for (const SignalReference &ir : dependencies) {
			assert(GetTileOwner(ir.tile) == GetTileOwner(tile));
			NotifyRemovingDependentSignal(thisRef, ir);
		}

		_signal_dependencies.erase(i);
	}
}

uint8 GetSignalAspectGeneric(TileIndex tile, Trackdir trackdir, bool check_non_inc_style)
{
	switch (GetTileType(tile)) {
		case MP_RAILWAY:
			if (HasSignalOnTrackdir(tile, trackdir) && GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN) {
				uint8 aspect = GetSignalAspect(tile, TrackdirToTrack(trackdir));
				if (check_non_inc_style) AdjustSignalAspectIfNonIncStyle(tile, TrackdirToTrack(trackdir), aspect);
				return aspect;
			}
			break;

		case MP_TUNNELBRIDGE:
			if (IsTunnelBridgeSignalSimulationEntrance(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
				return (GetTunnelBridgeEntranceSignalState(tile) == SIGNAL_STATE_GREEN) ? GetTunnelBridgeEntranceSignalAspect(tile) : 0;
			}
			if (IsTunnelBridgeSignalSimulationExit(tile) && TrackdirExitsTunnelBridge(tile, trackdir)) {
				return (GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN) ? GetTunnelBridgeExitSignalAspect(tile) : 0;
			}
			break;

		default:
			break;
	}

	return 0;
}

void AdjustSignalAspectIfNonIncStyleIntl(TileIndex tile, Track track, uint8 &aspect)
{
	if (IsTileType(tile, MP_RAILWAY)) {
		uint8 style = GetSignalStyle(tile, track);
		if (HasBit(_signal_style_masks.combined_normal_shunt, style)) {
			aspect--;
			if (aspect == 0) return;
		}
		if (HasBit(_signal_style_masks.non_aspect_inc, style)) aspect--;
	}
}

static void RefreshBridgeOnExitAspectChange(TileIndex entrance, TileIndex exit)
{
	const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(entrance);
	const uint bridge_length = GetTunnelBridgeLength(entrance, exit);
	const TileIndexDiffC offset = TileIndexDiffCByDiagDir(GetTunnelBridgeDirection(entrance));
	const TileIndexDiff diff = TileDiffXY(offset.x * simulated_wormhole_signals, offset.y * simulated_wormhole_signals);
	const uint signal_count = bridge_length / simulated_wormhole_signals;
	if (signal_count == 0) return;
	TileIndex tile = entrance + ((int)signal_count * diff);
	const uint redraw_count = std::min<uint>(_extra_aspects, signal_count);
	for (uint i = 0; i < redraw_count; i++) {
		MarkSingleBridgeSignalDirty(tile, entrance);
		tile -= diff;
	}
}

void PropagateAspectChange(TileIndex tile, Trackdir trackdir, uint8 aspect)
{
	AdjustSignalAspectIfNonIncStyle(tile, TrackdirToTrack(trackdir), aspect);

	aspect = std::min<uint8>(aspect + 1, GetMaximumSignalAspect());
	Owner owner = GetTileOwner(tile);
	DiagDirection exitdir = TrackdirToExitdir(ReverseTrackdir(trackdir));
	DiagDirection enterdir = ReverseDiagDir(exitdir);
	bool wormhole = false;
	if (IsTileType(tile, MP_TUNNELBRIDGE) && TrackdirExitsTunnelBridge(tile, trackdir)) {
		TileIndex other = GetOtherTunnelBridgeEnd(tile);
		if (IsBridge(tile)) RefreshBridgeOnExitAspectChange(other, tile);
		aspect = std::min<uint>(GetSignalledTunnelBridgeEntranceForwardAspect(other, tile) + 1, GetMaximumSignalAspect());
		tile = other;
		wormhole = true;
	} else {
		tile += TileOffsByDiagDir(exitdir);
	}
	while (true) {
		switch (GetTileType(tile)) {
			case MP_RAILWAY: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return;

				if (IsRailDepot(tile)) {
					return;
				}

				TrackBits tracks = GetTrackBits(tile); // trackbits of tile
				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir]); // only incidating trackbits

				if (tracks_masked == TRACK_BIT_NONE) return; // no incidating track
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (!HasAtMostOneBit(tracks)) {
					TrackBits reserved_bits = GetRailReservationTrackBits(tile) & tracks_masked;
					if (reserved_bits == TRACK_BIT_NONE) return; // no reservation on junction
					tracks = reserved_bits;
				}

				Track track = (Track)FIND_FIRST_BIT(tracks);
				trackdir = TrackEnterdirToTrackdir(track, ReverseDiagDir(enterdir));

				if (HasSignals(tile)) {
					if (HasSignalOnTrack(tile, track)) { // now check whole track, not trackdir
						Trackdir reversedir = ReverseTrackdir(trackdir);

						if (HasSignalOnTrackdir(tile, reversedir)) {
							if (GetSignalStateByTrackdir(tile, reversedir) == SIGNAL_STATE_RED) return;
							bool combined_mode = IsRailCombinedNormalShuntSignalStyle(tile, track);
							const uint8 current_aspect = GetSignalAspect(tile, track);
							if (combined_mode && current_aspect == 1) {
								/* Don't change special combined_normal_shunt aspect */
								return;
							}
							if (combined_mode && aspect > 0) aspect = std::min<uint8>(aspect + 1, 7);
							if (current_aspect == aspect) return; // aspect already correct
							SetSignalAspect(tile, track, aspect);
							MarkSingleSignalDirty(tile, reversedir);
							AdjustSignalAspectIfNonIncStyle(tile, TrackdirToTrack(trackdir), aspect);
							aspect = std::min<uint8>(aspect + 1, GetMaximumSignalAspect());
						} else if (IsOnewaySignal(tile, track)) {
							return; // one-way signal facing the wrong way
						}
					}
				}

				exitdir = TrackdirToExitdir(trackdir);
				enterdir = ReverseDiagDir(exitdir);
				tile += TileOffsByDiagDir(exitdir);

				break;
			}

			case MP_STATION:
				if (!HasStationRail(tile)) return;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return;
				if (DiagDirToAxis(enterdir) != GetRailStationAxis(tile)) return; // different axis
				if (IsStationTileBlocked(tile)) return; // 'eye-candy' station tile

				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_ROAD:
				if (!IsLevelCrossing(tile)) return;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return;
				if (DiagDirToAxis(enterdir) == GetCrossingRoadAxis(tile)) return; // different axis

				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_TUNNELBRIDGE: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) return;
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) return;
				if ((enterdir == GetTunnelBridgeDirection(tile)) != wormhole) return;

				TrackBits tracks = GetTunnelBridgeTrackBits(tile); // trackbits of tile
				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir]); // only incidating trackbits

				if (tracks_masked == TRACK_BIT_NONE) return; // no incidating track
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (!HasAtMostOneBit(tracks)) {
					TrackBits reserved_bits = GetTunnelBridgeReservationTrackBits(tile) & tracks_masked;
					if (reserved_bits == TRACK_BIT_NONE) return; // no reservation on junction
					tracks = reserved_bits;
				}

				Track track = (Track)FIND_FIRST_BIT(tracks);
				trackdir = TrackEnterdirToTrackdir(track, ReverseDiagDir(enterdir));

				if (TrackdirEntersTunnelBridge(tile, trackdir)) {
					TileIndex other = GetOtherTunnelBridgeEnd(tile);
					if (IsTunnelBridgeWithSignalSimulation(tile)) {
						/* exit signal */
						if (!IsTunnelBridgeSignalSimulationExit(tile) || GetTunnelBridgeExitSignalState(tile) != SIGNAL_STATE_GREEN) return;
						if (GetTunnelBridgeExitSignalAspect(tile) == aspect) return;
						SetTunnelBridgeExitSignalAspect(tile, aspect);
						MarkTunnelBridgeSignalDirty(tile, true);
						if (IsBridge(tile)) RefreshBridgeOnExitAspectChange(other, tile);
						aspect = std::min<uint>(GetSignalledTunnelBridgeEntranceForwardAspect(other, tile) + 1, GetMaximumSignalAspect());
					}
					enterdir = GetTunnelBridgeDirection(other);
					exitdir = ReverseDiagDir(enterdir);
					tile = other;
					wormhole = true;
				} else {
					if (TrackdirEntersTunnelBridge(tile, ReverseTrackdir(trackdir))) {
						if (IsTunnelBridgeWithSignalSimulation(tile)) {
							/* entrance signal */
							if (!IsTunnelBridgeSignalSimulationEntrance(tile) || GetTunnelBridgeEntranceSignalState(tile) != SIGNAL_STATE_GREEN) return;
							if (GetTunnelBridgeEntranceSignalAspect(tile) == aspect) return;
							SetTunnelBridgeEntranceSignalAspect(tile, aspect);
							MarkTunnelBridgeSignalDirty(tile, false);
							aspect = std::min<uint>(aspect + 1, GetMaximumSignalAspect());
						}
					}
					exitdir = TrackdirToExitdir(trackdir);
					enterdir = ReverseDiagDir(exitdir);
					tile += TileOffsByDiagDir(exitdir);
					wormhole = false;
				}
				break;
			}

			default:
				return;
		}
	}
}

static std::vector<std::pair<TileIndex, Trackdir>> _deferred_aspect_updates;

struct DeferredCombinedNormalShuntModeItem {
	TileIndex tile;
	Trackdir trackdir;
	Order current_order;
	VehicleOrderID cur_real_order_index;
	StationID last_station_visited;
};
static std::vector<DeferredCombinedNormalShuntModeItem> _deferred_determine_combined_normal_shunt_mode;

struct DeferredLookaheadCombinedNormalShuntModeItem {
	TileIndex tile;
	Trackdir trackdir;
	int lookahead_position;
};
static std::vector<DeferredLookaheadCombinedNormalShuntModeItem> _deferred_lookahead_combined_normal_shunt_mode;

void UpdateAspectDeferred(TileIndex tile, Trackdir trackdir)
{
	_deferred_aspect_updates.push_back({ tile, trackdir });
}

void UpdateAspectDeferredWithVehicle(const Train *v, TileIndex tile, Trackdir trackdir, bool check_combined_normal_aspect)
{
	if (check_combined_normal_aspect && IsRailCombinedNormalShuntSignalStyle(tile, TrackdirToTrack(trackdir)) &&
			_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		DeferredCombinedNormalShuntModeItem &item = _deferred_determine_combined_normal_shunt_mode.emplace_back();
		item.tile = tile;
		item.trackdir = trackdir;
		if (IsRestrictedSignal(tile)) {
			item.current_order = v->current_order;
			item.cur_real_order_index = v->cur_real_order_index;
			item.last_station_visited = v->last_station_visited;
		}
	}
	_deferred_aspect_updates.push_back({ tile, trackdir });
}

void UpdateLookaheadCombinedNormalShuntSignalDeferred(TileIndex tile, Trackdir trackdir, int lookahead_position)
{
	_deferred_lookahead_combined_normal_shunt_mode.push_back({ tile, trackdir, lookahead_position });
}

void FlushDeferredAspectUpdates()
{
	/* Iterate in reverse order to reduce backtracking when updating the aspects of a new reservation */
	for (auto iter = _deferred_aspect_updates.rbegin(); iter != _deferred_aspect_updates.rend(); ++iter) {
		TileIndex tile = iter->first;
		Trackdir trackdir = iter->second;
		switch (GetTileType(tile)) {
			case MP_RAILWAY:
				if (HasSignalOnTrackdir(tile, trackdir) && GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN && GetSignalAspect(tile, TrackdirToTrack(trackdir)) == 0) {
					uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir, IsRailCombinedNormalShuntSignalStyle(tile, TrackdirToTrack(trackdir)));
					SetSignalAspect(tile, TrackdirToTrack(trackdir), aspect);
					PropagateAspectChange(tile, trackdir, aspect);
				}
				break;

			case MP_TUNNELBRIDGE:
				if (IsTunnelBridgeSignalSimulationEntrance(tile) && TrackdirEntersTunnelBridge(tile, trackdir) &&
						GetTunnelBridgeEntranceSignalState(tile) == SIGNAL_STATE_GREEN && GetTunnelBridgeEntranceSignalAspect(tile) == 0) {
					uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
					SetTunnelBridgeEntranceSignalAspect(tile, aspect);
					PropagateAspectChange(tile, trackdir, aspect);
				}
				if (IsTunnelBridgeSignalSimulationExit(tile) && TrackdirExitsTunnelBridge(tile, trackdir) &&
						GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN && GetTunnelBridgeExitSignalAspect(tile) == 0) {
					uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
					SetTunnelBridgeExitSignalAspect(tile, aspect);
					PropagateAspectChange(tile, trackdir, aspect);
				}
				break;

			default:
				break;
		}
	}
	_deferred_aspect_updates.clear();
}

void DetermineCombineNormalShuntModeWithLookahead(Train *v, TileIndex tile, Trackdir trackdir, int lookahead_position)
{
	size_t count = v->lookahead->items.size();
	for (size_t i = 0; i < count; i++) {
		TrainReservationLookAheadItem &item = v->lookahead->items[i];
		if (item.start == lookahead_position && item.type == TRLIT_SIGNAL && HasBit(item.data_aux, TRSLAI_COMBINED)) {
			DeferredCombinedNormalShuntModeItem res_item;
			bool have_orders = false;
			container_unordered_remove_if(_deferred_determine_combined_normal_shunt_mode, [&](DeferredCombinedNormalShuntModeItem &iter) -> bool {
				bool found = (iter.tile == tile && iter.trackdir == trackdir);
				if (found) {
					res_item = std::move(iter);
					have_orders = true;
				}
				return found;
			});

			if (IsRestrictedSignal(tile)) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
				if (prog && prog->actions_used_flags & TRPAUF_CMB_SIGNAL_MODE_CTRL) {
					TraceRestrictProgramResult out;
					TraceRestrictProgramInput input(tile, trackdir, [](const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode) {
						if (mode == TRPESAF_RES_END_TILE) {
							return v->lookahead->reservation_end_tile;
						} else {
							return INVALID_TILE;
						}
					}, nullptr);
					if (have_orders && (prog->actions_used_flags & TRPAUF_ORDER_CONDITIONALS)) {
						std::swap(res_item.current_order, v->current_order);
						std::swap(res_item.cur_real_order_index, v->cur_real_order_index);
						std::swap(res_item.last_station_visited, v->last_station_visited);
						prog->Execute(v, input, out);
						v->current_order = std::move(res_item.current_order);
						v->cur_real_order_index = res_item.cur_real_order_index;
						v->last_station_visited = res_item.last_station_visited;
					} else {
						prog->Execute(v, input, out);
					}
					if (out.flags & TRPRF_SIGNAL_MODE_NORMAL) {
						return;
					}
					if (out.flags & TRPRF_SIGNAL_MODE_SHUNT) {
						SetSignalAspect(tile, TrackdirToTrack(trackdir), 1);
						SetBit(item.data_aux, TRSLAI_COMBINED_SHUNT);
						return;
					}
				}
			}

			for (size_t j = i + 1; j < count; j++) {
				const TrainReservationLookAheadItem &ahead = v->lookahead->items[j];
				if (ahead.type == TRLIT_SIGNAL) {
					if (HasBit(ahead.data_aux, TRSLAI_COMBINED)) return;
					if (!HasBit(ahead.data_aux, TRSLAI_NO_ASPECT_INC) && !HasBit(ahead.data_aux, TRSLAI_NEXT_ONLY)) return;
				}
			}

			if (IsTileType(v->lookahead->reservation_end_tile, MP_TUNNELBRIDGE)) return;

			if (IsRailDepotTile(v->lookahead->reservation_end_tile)) {
				/* shunt mode */
				SetSignalAspect(tile, TrackdirToTrack(trackdir), 1);
				SetBit(item.data_aux, TRSLAI_COMBINED_SHUNT);
				return;
			}

			CFollowTrackRail ft(v);
			if (ft.Follow(v->lookahead->reservation_end_tile, v->lookahead->reservation_end_trackdir)) {
				if (KillFirstBit(ft.m_new_td_bits) != TRACKDIR_BIT_NONE) {
					/* reached a junction tile, shouldn't be reached, just assume normal route */
					return;
				}

				TileIndex new_tile = ft.m_new_tile;
				Trackdir new_trackdir = FindFirstTrackdir(ft.m_new_td_bits);

				if (!(IsTileType(new_tile, MP_RAILWAY) && HasSignalOnTrackdir(new_tile, new_trackdir) && !IsNoEntrySignal(new_tile, TrackdirToTrack(new_trackdir)) &&
						HasBit(_signal_style_masks.next_only, GetSignalStyle(new_tile, TrackdirToTrack(new_trackdir))))) {
					/* Didn't find a shunt signal at the end of the reservation */
					return;
				}
			} else {
				/* end of line, see if this is a bay with a shunt signal on the exit */
				TileIndex t = v->lookahead->reservation_end_tile;
				Trackdir td = ReverseTrackdir(v->lookahead->reservation_end_trackdir);
				while (true) {
					if (t == tile) {
						/* Reached this signal, don't follow any further */
						return;
					}
					if (IsTunnelBridgeWithSignalSimulation(t)) return;

					if (IsTileType(t, MP_RAILWAY) && HasSignalOnTrackdir(t, td)) {
						/* Found first signal on exit from bay where reservation ends */
						if (HasBit(_signal_style_masks.next_only, GetSignalStyle(t, TrackdirToTrack(td)))) {
							/* Shunt signal, use shunt route */
							break;
						} else {
							/* Use normal route */
							return;
						}
					}
					if (ft.Follow(t, td)) {
						TrackdirBits bits = ft.m_new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.m_new_tile));
						if (!HasExactlyOneBit(bits)) return;

						t = ft.m_new_tile;
						td = FindFirstTrackdir(bits);
					} else {
						return;
					}
				}
			}

			/* shunt mode */
			SetSignalAspect(tile, TrackdirToTrack(trackdir), 1);
			SetBit(item.data_aux, TRSLAI_COMBINED_SHUNT);
			return;
		}
	}
}

void FlushDeferredDetermineCombineNormalShuntMode(Train *v)
{
	for (const auto &iter : _deferred_lookahead_combined_normal_shunt_mode) {
		DetermineCombineNormalShuntModeWithLookahead(v, iter.tile, iter.trackdir, iter.lookahead_position);
	}
	_deferred_lookahead_combined_normal_shunt_mode.clear();

	for (const auto &iter : _deferred_determine_combined_normal_shunt_mode) {
		/* Reservation with no associated lookahead, default to a shunt route */
		SetSignalAspect(iter.tile, TrackdirToTrack(iter.trackdir), 1);
	}
	_deferred_determine_combined_normal_shunt_mode.clear();
}

void UpdateAllSignalAspects()
{
	for (TileIndex tile = 0; tile != MapSize(); ++tile) {
		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			TrackBits bits = GetTrackBits(tile);
			do {
				Track track = RemoveFirstTrack(&bits);
				if (HasSignalOnTrack(tile, track)) {
					Trackdir trackdir = TrackToTrackdir(track);
					if (!HasSignalOnTrackdir(tile, trackdir)) trackdir = ReverseTrackdir(trackdir);
					if (GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN && !IsRailSpecialSignalAspect(tile, track)) {
						uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir, IsRailCombinedNormalShuntSignalStyle(tile, track));
						SetSignalAspect(tile, track, aspect);
						PropagateAspectChange(tile, trackdir, aspect);
					}
				}
			} while (bits != TRACK_BIT_NONE);
		} else if (IsTunnelBridgeWithSignalSimulation(tile)) {
			if (IsTunnelBridgeSignalSimulationEntrance(tile) && GetTunnelBridgeEntranceSignalState(tile) == SIGNAL_STATE_GREEN) {
				Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(tile);
				uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
				SetTunnelBridgeEntranceSignalAspect(tile, aspect);
				PropagateAspectChange(tile, trackdir, aspect);
			}
			if (IsTunnelBridgeSignalSimulationExit(tile) && GetTunnelBridgeExitSignalState(tile) == SIGNAL_STATE_GREEN) {
				Trackdir trackdir = GetTunnelBridgeExitTrackdir(tile);
				uint8 aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
				SetTunnelBridgeExitSignalAspect(tile, aspect);
				PropagateAspectChange(tile, trackdir, aspect);
			}
		}
	}
}

void ClearNewSignalStyleMapping()
{
	_new_signal_style_mapping.fill({});
}

static bool RemapNewSignalStyles(const std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> &new_mapping)
{
	const std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> old_mapping = _new_signal_style_mapping;
	_new_signal_style_mapping = new_mapping;

	uint8 remap_table[MAX_NEW_SIGNAL_STYLES + 1] = {};
	remap_table[0] = 0;

	uint8 next_free = _num_new_signal_styles;

	std::array<bool, MAX_NEW_SIGNAL_STYLES> usage_table;
	const bool assume_all_styles_in_use = _networking && !_network_server;
	usage_table.fill(assume_all_styles_in_use);
	bool usage_table_populated = !assume_all_styles_in_use;

	auto populate_usage_table = [&]() {
		usage_table_populated = true;

		const TileIndex map_size = MapSize();
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				for (Track track : { TRACK_LOWER, TRACK_UPPER }) {
					uint8 old_style = GetSignalStyle(t, track);
					if (old_style > 0) usage_table[old_style - 1] = true;
				}
			}
			if (IsRailTunnelBridgeTile(t) && GetTunnelBridgeDirection(t) < DIAGDIR_SW) {
				/* Only process west end of tunnel/bridge */
				uint8 old_style = GetTunnelBridgeSignalStyle(t);
				if (old_style > 0) usage_table[old_style - 1] = true;
			}
		}
	};

	bool do_remap = false;
	for (uint i = 0; i < MAX_NEW_SIGNAL_STYLES; i++) {
		if (old_mapping[i].grfid == 0) {
			remap_table[i + 1] = 0;
			continue;
		}

		auto find_target = [&]() {
			for (uint j = 0; j < MAX_NEW_SIGNAL_STYLES; j++) {
				if (old_mapping[i].grfid == _new_signal_style_mapping[j].grfid && old_mapping[i].grf_local_id == _new_signal_style_mapping[j].grf_local_id) {
					remap_table[i + 1] = j + 1;
					if (i != j) {
						if (!usage_table_populated) populate_usage_table();
						if (usage_table[i]) do_remap = true;
					}
					return;
				}
			}

			if (!usage_table_populated) populate_usage_table();
			if (!usage_table[i]) {
				/* no signals to remap */
				remap_table[i + 1] = 0;
				return;
			}

			if (next_free < MAX_NEW_SIGNAL_STYLES) {
				remap_table[i + 1] = next_free + 1;
				_new_signal_style_mapping[next_free].grfid = old_mapping[i].grfid;
				_new_signal_style_mapping[next_free].grf_local_id = old_mapping[i].grf_local_id;
				if (i != next_free) do_remap = true;
				next_free++;
			} else {
				remap_table[i + 1] = 0;
				do_remap = true;
			}
		};
		find_target();
	}

	bool signal_remapped = false;
	if (do_remap) {
		const TileIndex map_size = MapSize();
		for (TileIndex t = 0; t < map_size; t++) {
			if (IsTileType(t, MP_RAILWAY) && HasSignals(t)) {
				for (Track track : { TRACK_LOWER, TRACK_UPPER }) {
					uint8 old_style = GetSignalStyle(t, track);
					uint8 new_style = remap_table[old_style];
					if (new_style != old_style) {
						SetSignalStyle(t, track, new_style);
						signal_remapped = true;
					}
				}
			}
			if (IsRailTunnelBridgeTile(t) && GetTunnelBridgeDirection(t) < DIAGDIR_SW) {
				/* Only process west end of tunnel/bridge */
				uint8 old_style = GetTunnelBridgeSignalStyle(t);
				uint8 new_style = remap_table[old_style];
				if (new_style != old_style) {
					SetTunnelBridgeSignalStyle(t, GetOtherTunnelBridgeEnd(t), new_style);
					signal_remapped = true;
				}
			}
		}
	}

	return signal_remapped;
}

static void DetermineSignalStyleMapping(std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> &mapping)
{
	mapping.fill({});

	for (uint i = 0; i < _num_new_signal_styles; i++) {
		mapping[i].grfid = _new_signal_styles[i].grffile->grfid;
		mapping[i].grf_local_id = _new_signal_styles[i].grf_local_id;
	}
}

static bool DetermineExtraAspectsVariable()
{
	bool changed = false;
	uint8 new_extra_aspects = 0;

	_signal_style_masks = {};

	_enabled_new_signal_styles_mask = 1;

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		for (RailType r = RAILTYPE_BEGIN; r != RAILTYPE_END; r++) {
			const RailTypeInfo *rti = GetRailTypeInfo(r);
			new_extra_aspects = std::max<uint8>(new_extra_aspects, rti->signal_extra_aspects);
		}
		for (const GRFFile *grf : _new_signals_grfs) {
			new_extra_aspects = std::max<uint8>(new_extra_aspects, grf->new_signal_extra_aspects);
		}
	}

	for (uint i = 0; i < _num_new_signal_styles; i++) {
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_NO_ASPECT_INC)) {
			SetBit(_signal_style_masks.non_aspect_inc, i + 1);
			SetBit(_signal_style_masks.no_tunnel_bridge, i + 1);
		}
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_ALWAYS_RESERVE_THROUGH)) {
			SetBit(_signal_style_masks.always_reserve_through, i + 1);
			SetBit(_signal_style_masks.no_tunnel_bridge, i + 1);
		}
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_LOOKAHEAD_SINGLE_SIGNAL)) {
			_new_signal_styles[i].lookahead_extra_aspects = 0;
			SetBit(_signal_style_masks.next_only, i + 1);
		} else if (HasBit(_new_signal_styles[i].style_flags, NSSF_LOOKAHEAD_ASPECTS_SET)) {
			if (_new_signal_styles[i].lookahead_extra_aspects != 255) {
				_new_signal_styles[i].lookahead_extra_aspects = std::min<uint8>(_new_signal_styles[i].lookahead_extra_aspects, _new_signal_styles[i].grffile->new_signal_extra_aspects);
			}
		} else {
			_new_signal_styles[i].lookahead_extra_aspects = _new_signal_styles[i].grffile->new_signal_extra_aspects;
		}
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_OPPOSITE_SIDE)) {
			SetBit(_signal_style_masks.signal_opposite_side, i + 1);
		}
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_COMBINED_NORMAL_SHUNT)) {
			SetBit(_signal_style_masks.combined_normal_shunt, i + 1);
			SetBit(_signal_style_masks.no_tunnel_bridge, i + 1);
			_new_signal_styles[i].electric_mask &= (1 << SIGTYPE_PBS) | (1 << SIGTYPE_PBS_ONEWAY) | (1 << SIGTYPE_NO_ENTRY);
			_new_signal_styles[i].semaphore_mask &= (1 << SIGTYPE_PBS) | (1 << SIGTYPE_PBS_ONEWAY) | (1 << SIGTYPE_NO_ENTRY);
		}
		uint8 mask = 0xFF;
		if (HasBit(_new_signal_styles[i].style_flags, NSSF_REALISTIC_BRAKING_ONLY) && _settings_game.vehicle.train_braking_model != TBM_REALISTIC) {
			mask = 0;
		} else if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			mask &= (1 << SIGTYPE_NORMAL) | (1 << SIGTYPE_PBS) | (1 << SIGTYPE_PBS_ONEWAY) | (1 << SIGTYPE_NO_ENTRY);
		}
		if ((_new_signal_styles[i].electric_mask | _new_signal_styles[i].semaphore_mask) & mask) SetBit(_enabled_new_signal_styles_mask, i + 1);
	}
	for (uint i = _num_new_signal_styles; i < MAX_NEW_SIGNAL_STYLES; i++) {
		_new_signal_styles[i].lookahead_extra_aspects = new_extra_aspects;
	}

	_extra_aspects = new_extra_aspects;

	SimpleChecksum64 checksum;
	checksum.Update(SimpleHash32(_extra_aspects));
	checksum.Update(SimpleHash32(_signal_style_masks.non_aspect_inc));
	checksum.Update(SimpleHash32(_signal_style_masks.always_reserve_through));
	checksum.Update(SimpleHash32(_signal_style_masks.combined_normal_shunt));

	if (checksum.state != _aspect_cfg_hash) {
		_aspect_cfg_hash = checksum.state;
		changed = true;
	}

	return changed;
}

void UpdateExtraAspectsVariable(bool update_always_reserve_through)
{
	std::array<NewSignalStyleMapping, MAX_NEW_SIGNAL_STYLES> new_mapping;
	DetermineSignalStyleMapping(new_mapping);

	bool style_remap = false;
	if (new_mapping != _new_signal_style_mapping) {
		style_remap = RemapNewSignalStyles(new_mapping);
	}

	bool style_change = DetermineExtraAspectsVariable();

	if (style_remap || style_change) {
		if (_networking && !_network_server && _game_mode != GM_MENU) {
			const char *msg = "Network client recalculating signal states and/or signal style mappings, this is likely to cause desyncs";
			DEBUG(desync, 0, "%s", msg);
			LogDesyncMsg(msg);
		}

		UpdateAllSignalReserveThroughBits();
		if (_extra_aspects > 0) UpdateAllSignalAspects();
		UpdateAllBlockSignals();
		MarkWholeScreenDirty();
	} else if (update_always_reserve_through) {
		UpdateAllSignalReserveThroughBits();
	}
}

void InitialiseExtraAspectsVariable()
{
	DetermineSignalStyleMapping(_new_signal_style_mapping);
	DetermineExtraAspectsVariable();
}

bool IsRailSpecialSignalAspect(TileIndex tile, Track track)
{
	return _signal_style_masks.combined_normal_shunt != 0 && GetSignalAspect(tile, track) == 1 &&
			HasBit(_signal_style_masks.combined_normal_shunt, GetSignalStyle(tile, track));
}

void UpdateSignalReserveThroughBit(TileIndex tile, Track track, bool update_signal)
{
	bool reserve_through = false;
	if (NonZeroSignalStylePossiblyOnTile(tile) && _signal_style_masks.always_reserve_through != 0 &&
			HasBit(_signal_style_masks.always_reserve_through, GetSignalStyle(tile, track))) {
		reserve_through = true;
	} else {
		if (IsRestrictedSignal(tile)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, track);
			if (prog && prog->actions_used_flags & TRPAUF_RESERVE_THROUGH_ALWAYS) reserve_through = true;
		}
	}

	if (reserve_through != GetSignalAlwaysReserveThrough(tile, track)) {
		SetSignalAlwaysReserveThrough(tile, track, reserve_through);
		if (update_signal && _settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			AddTrackToSignalBuffer(tile, track, GetTileOwner(tile));
			UpdateSignalsInBuffer();
		}
	}
}

void UpdateAllSignalReserveThroughBits()
{
	TileIndex tile = 0;
	do {
		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile)) {
			TrackBits bits = GetTrackBits(tile);
			do {
				Track track = RemoveFirstTrack(&bits);
				if (HasSignalOnTrack(tile, track)) {
					UpdateSignalReserveThroughBit(tile, track, false);
				}
			} while (bits != TRACK_BIT_NONE);
		}
	} while (++tile != MapSize());
}
