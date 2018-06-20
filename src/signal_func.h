/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signal_func.h Functions related to signals. */

#ifndef SIGNAL_FUNC_H
#define SIGNAL_FUNC_H

#include "signal_type.h"
#include "track_type.h"
#include "tile_type.h"
#include "direction_type.h"
#include "company_type.h"
#include "debug.h"

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction along with the trackdir.
 */
static inline byte SignalAlongTrackdir(Trackdir trackdir)
{
	extern const byte _signal_along_trackdir[TRACKDIR_END];
	return _signal_along_trackdir[trackdir];
}

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction against the trackdir.
 */
static inline byte SignalAgainstTrackdir(Trackdir trackdir)
{
	extern const byte _signal_against_trackdir[TRACKDIR_END];
	return _signal_against_trackdir[trackdir];
}

/**
 * Maps a Track to the bits that store the status of the two signals that can
 * be present on the given track.
 */
static inline byte SignalOnTrack(Track track)
{
	extern const byte _signal_on_track[TRACK_END];
	return _signal_on_track[track];
}

/// Is a given signal type a presignal entry signal?
static inline bool IsEntrySignal(SignalType type)
{
	return type == SIGTYPE_ENTRY || type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a presignal exit signal?
static inline bool IsExitSignal(SignalType type)
{
	return type == SIGTYPE_EXIT || type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a presignal combo signal?
static inline bool IsComboSignal(SignalType type)
{
	return type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a PBS signal?
static inline bool IsPbsSignal(SignalType type)
{
	return type == SIGTYPE_PBS || type == SIGTYPE_PBS_ONEWAY;
}

/// Is this a programmable signal?
static inline bool IsProgrammableSignal(SignalType type)
{
	return type == SIGTYPE_PROG;
}

/// Does a given signal have a PBS sprite?
static inline bool IsSignalSpritePBS(SignalType type)
{
	return type >= SIGTYPE_FIRST_PBS_SPRITE;
}

static inline SignalType NextSignalType(SignalType cur, uint which_signals)
{
	bool pbs   = (which_signals != 0);
	bool block = (which_signals != 1);

	switch(cur) {
		case SIGTYPE_NORMAL:     return block ? SIGTYPE_ENTRY      : SIGTYPE_PBS;
		case SIGTYPE_ENTRY:      return block ? SIGTYPE_EXIT       : SIGTYPE_PBS;
		case SIGTYPE_EXIT:       return block ? SIGTYPE_COMBO      : SIGTYPE_PBS;
		case SIGTYPE_COMBO:      return pbs   ? SIGTYPE_PBS        : SIGTYPE_NORMAL;
		case SIGTYPE_PROG:       return pbs   ? SIGTYPE_PBS        : SIGTYPE_NORMAL;
		case SIGTYPE_PBS:        return pbs   ? SIGTYPE_PBS_ONEWAY : SIGTYPE_NORMAL;
		case SIGTYPE_PBS_ONEWAY: return block ? SIGTYPE_NORMAL     : SIGTYPE_PBS;
		default:
			DEBUG(map, 0, "Attempt to cycle from signal type %d", cur);
			return SIGTYPE_NORMAL; // Fortunately mostly harmless
	}
}

/** State of the signal segment */
enum SigSegState {
	SIGSEG_FREE,    ///< Free and has no pre-signal exits or at least one green exit
	SIGSEG_FULL,    ///< Occupied by a train
	SIGSEG_PBS,     ///< Segment is a PBS segment
};

/** Checks for any data attached to any signals, and removes it. Call when performing
 * an action which may potentially remove signals from a tile, in order to avoid leaking
 * data.
 */
void CheckRemoveSignalsFromTile(TileIndex tile);

/** Checks for, and removes, any extra signal data. Call when removing a piece of track
 * which is potentially signalled, in order to free any extra data that may be associated
 * with said track.
 */
void CheckRemoveSignal(TileIndex tile, Track track);

/** Adds a signal dependency
 *  The signal identified by @p dep will be marked as dependend upon
 *  the signal identified by @p on
 */
void AddSignalDependency(SignalReference on, SignalReference dep);

/// Removes a signal dependency. Arguments same as AddSignalDependency
void RemoveSignalDependency(SignalReference on, SignalReference dep);

/// Frees signal dependencies (for newgame/load)
void FreeSignalDependencies();

SigSegState UpdateSignalsOnSegment(TileIndex tile, DiagDirection side, Owner owner);
void SetSignalsOnBothDir(TileIndex tile, Track track, Owner owner);
void AddTrackToSignalBuffer(TileIndex tile, Track track, Owner owner);
void AddSideToSignalBuffer(TileIndex tile, DiagDirection side, Owner owner);
void UpdateSignalsInBuffer();

#endif /* SIGNAL_FUNC_H */
