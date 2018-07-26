/* $Id$ */

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

#include "safeguards.h"

/// List of signals dependent upon this one
typedef SmallVector<SignalReference, 4>     SignalDependencyList;

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

assert_compile(SIG_GLOB_UPDATE <= SIG_GLOB_SIZE);

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
static SmallSet<DiagDirection, SIG_TBD_SIZE> _tbdset("_tbdset");    ///< set of open nodes in current signal block
static SmallSet<DiagDirection, SIG_GLOB_SIZE> _globset("_globset"); ///< set of places to be updated in following runs

static uint _num_signals_evaluated; ///< Number of programmable signals evaluated

/** Check whether there is a train on rail, not in a depot */
static Vehicle *TrainOnTileEnum(Vehicle *v, void *)
{
	if (v->type != VEH_TRAIN || Train::From(v)->track == TRACK_BIT_DEPOT) return NULL;

	return v;
}

/** Check whether there is a train only on ramp. */
static Vehicle *TrainInWormholeTileEnum(Vehicle *v, void *data)
{
	/* Only look for front engine or last wagon. */
	if (v->type != VEH_TRAIN || (v->Previous() != NULL && v->Next() != NULL)) return NULL;
	TileIndex tile = *(TileIndex *)data;
	if (tile != TileVirtXY(v->x_pos, v->y_pos)) return NULL;
	if (!(Train::From(v)->track & TRACK_BIT_WORMHOLE) && !(Train::From(v)->track & GetAcrossTunnelBridgeTrackBits(tile))) return NULL;
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

	if (_tbdset.Remove(t2, d2)) return false;

	return true;
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
};

DECLARE_ENUM_AS_BIT_SET(SigFlags)

struct SigInfo {
	inline SigInfo()
	{
		flags = SF_NONE;
		num_exits = 0;
		num_green = 0;
	}
	SigFlags flags;
	uint num_exits;
	uint num_green;
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

	TileIndex tile;
	DiagDirection enterdir;

	while (_tbdset.Get(&tile, &enterdir)) {
		TileIndex oldtile = tile; // tile we are leaving
		DiagDirection exitdir = enterdir == INVALID_DIAGDIR ? INVALID_DIAGDIR : ReverseDiagDir(enterdir); // expected new exit direction (for straight line)

		switch (GetTileType(tile)) {
			case MP_RAILWAY: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;

				if (IsRailDepot(tile)) {
					if (enterdir == INVALID_DIAGDIR) { // from 'inside' - train just entered or left the depot
						if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, NULL, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
						exitdir = GetRailDepotDirection(tile);
						tile += TileOffsByDiagDir(exitdir);
						enterdir = ReverseDiagDir(exitdir);
						break;
					} else if (enterdir == GetRailDepotDirection(tile)) { // entered a depot
						if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, NULL, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
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
					if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, NULL, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
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
							if (IsPbsSignal(sig)) {
								info.flags |= SF_PBS;
							} else if (!_tbuset.Add(tile, reversedir)) {
								info.flags |= SF_FULL;
								return info;
							}
						}
						if (HasSignalOnTrackdir(tile, trackdir) && !IsOnewaySignal(tile, track)) info.flags |= SF_PBS;

						/* if it is a presignal EXIT in OUR direction, count it */
						if (IsPresignalExit(tile, track) && HasSignalOnTrackdir(tile, trackdir)) { // found presignal exit
							info.num_exits++;
							if (GetSignalStateByTrackdir(tile, trackdir) == SIGNAL_STATE_GREEN) { // found green presignal exit
								info.num_green++;
							}
						}

						continue;
					}
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

				if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, NULL, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_ROAD:
				if (!IsLevelCrossing(tile)) continue;
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;
				if (DiagDirToAxis(enterdir) == GetCrossingRoadAxis(tile)) continue; // different axis

				if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, NULL, &TrainOnTileEnum)) info.flags |= SF_TRAIN;
				if (_settings_game.vehicle.safer_crossings) info.flags |= SF_PBS;
				tile += TileOffsByDiagDir(exitdir);
				break;

			case MP_TUNNELBRIDGE: {
				if (!IsOneSignalBlock(owner, GetTileOwner(tile))) continue;
				if (GetTunnelBridgeTransportType(tile) != TRANSPORT_RAIL) continue;
				DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(tile);
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
						return HasVehicleOnPos(tile, NULL, &TrainOnTileEnum);
					}
				};

				TrackBits tracks_masked = (TrackBits)(tracks & _enterdir_to_trackbits[enterdir == INVALID_DIAGDIR ? tunnel_bridge_dir : enterdir]); // only incidating trackbits
				if (tracks == TRACK_BIT_HORZ || tracks == TRACK_BIT_VERT) tracks = tracks_masked;

				if (IsTunnelBridgeWithSignalSimulation(tile)) {
					if (enterdir == INVALID_DIAGDIR) {
						// incoming from the wormhole, onto signal
						if (!(info.flags & SF_TRAIN) && IsTunnelBridgeSignalSimulationExit(tile)) { // tunnel entrance is ignored
							if (HasVehicleOnPos(GetOtherTunnelBridgeEnd(tile), &tile, &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							if (!(info.flags & SF_TRAIN) && HasVehicleOnPos(tile, &tile, &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
						}
						if (IsTunnelBridgeSignalSimulationExit(tile) && !_tbuset.Add(tile, INVALID_TRACKDIR)) {
							info.flags |= SF_FULL;
							return info;
						}
						Trackdir exit_track = TrackEnterdirToTrackdir(FindFirstTrack(GetAcrossTunnelBridgeTrackBits(tile)), ReverseDiagDir(tunnel_bridge_dir));
						exitdir = TrackdirToExitdir(exit_track);
						enterdir = ReverseDiagDir(exitdir);
						tile += TileOffsByDiagDir(exitdir); // just skip to next tile
						break;
					} else if (_enterdir_to_trackbits[enterdir] & GetAcrossTunnelBridgeTrackBits(tile)) {
						// NOT incoming from the wormhole!
						if (IsTunnelBridgeSignalSimulationExit(tile)) {
							if (IsTunnelBridgePBS(tile)) {
								info.flags |= SF_PBS;
							} else if (!_tbuset.Add(tile, INVALID_TRACKDIR)) {
								info.flags |= SF_FULL;
								return info;
							}
						}
						if (!(info.flags & SF_TRAIN)) {
							if (HasVehicleOnPos(tile, &tile, &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							if (!(info.flags & SF_TRAIN) && IsTunnelBridgeSignalSimulationExit(tile)) {
								if (HasVehicleOnPos(GetOtherTunnelBridgeEnd(tile), &tile, &TrainInWormholeTileEnum)) info.flags |= SF_TRAIN;
							}
						}
						continue;
					}
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


/**
 * Update signals around segment in _tbuset
 *
 * @param flags info about segment
 */
static void UpdateSignalsAroundSegment(SigInfo info)
{
	TileIndex tile;
	Trackdir trackdir;
	Track track;

	while (_tbuset.Get(&tile, &trackdir)) {
		if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(tile)) {
			if (IsTunnelBridgePBS(tile)) continue;
			SignalState old_state = GetTunnelBridgeExitSignalState(tile);
			SignalState new_state = (info.flags & SF_TRAIN) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
			if (old_state != new_state) {
				SetTunnelBridgeExitSignalState(tile, new_state);
				MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
			}
			continue;
		}

		assert_msg_tile(HasSignalOnTrackdir(tile, trackdir), tile, "trackdir: %u", trackdir);

		track = TrackdirToTrack(trackdir);
		SignalType sig = GetSignalType(tile, track);
		SignalState newstate = SIGNAL_STATE_GREEN;

		/* determine whether the new state is red */
		if (info.flags & SF_TRAIN) {
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

		/* only when the state changes */
		if (newstate != GetSignalStateByTrackdir(tile, trackdir)) {
			if (IsExitSignal(sig)) {
				/* for pre-signal exits, add block to the global set */
				DiagDirection exitdir = TrackdirToExitdir(ReverseTrackdir(trackdir));
				_globset.Add(tile, exitdir); // do not check for full global set, first update all signals

				// Progsig dependencies
				MarkDependencidesForUpdate(SignalReference(tile, track));
			}
			SetSignalStateByTrackdir(tile, trackdir, newstate);
			MarkTileDirtyByTile(tile, ZOOM_LVL_DRAW_MAP);
		}
	}

}


/** Reset all sets after one set overflowed */
static inline void ResetSets()
{
	_tbuset.Reset();
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

	TileIndex tile;
	DiagDirection dir;

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
					_tbdset.Add(tile, INVALID_DIAGDIR);  // we can safely start from wormhole centre
					if (!IsTunnelBridgeWithSignalSimulation(tile)) {  // Don't worry with other side of tunnel.
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
				if ((TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)) & _enterdir_to_trackbits[dir]) != TRACK_BIT_NONE) {
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
				if ((TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0)) & _enterdir_to_trackbits[dir]) != TRACK_BIT_NONE) {
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
	assert(_globset.IsEmpty());
	_globset.Add(tile, side);

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
	(*dependencies.Append()) = dep;
}

void RemoveSignalDependency(SignalReference on, SignalReference dep)
{
	SignalDependencyList &dependencies = _signal_dependencies[on];
	SignalReference *ob = dependencies.Find(dep);

	// Destroying both signals in same command
	if(ob == dependencies.End())
		return;

	dependencies.Erase(ob);
	if (dependencies.Length() == 0)
		_signal_dependencies.erase(on);
}

void FreeSignalDependencies()
{
	_signal_dependencies.clear();
}

static void MarkDependencidesForUpdate(SignalReference on)
{
	SignalDependencyMap::iterator f = _signal_dependencies.find(on);
	if (f == _signal_dependencies.end()) return;

	SignalDependencyList &dependencies = f->second;
	for (SignalReference *i = dependencies.Begin(), *e = dependencies.End();
			i != e; ++i) {
		assert(GetTileOwner(i->tile) == GetTileOwner(on.tile));

		Trackdir td = TrackToTrackdir(i->track);
		_globset.Add(i->tile, TrackdirToExitdir(td));
		_globset.Add(i->tile, TrackdirToExitdir(ReverseTrackdir(td)));
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

		for (SignalReference *ir = dependencies.Begin(), *er = dependencies.End(); ir != er; ++ir) {
			assert(GetTileOwner(ir->tile) == GetTileOwner(tile));
			NotifyRemovingDependentSignal(thisRef, *ir);
		}

		_signal_dependencies.erase(i);
	}
}
