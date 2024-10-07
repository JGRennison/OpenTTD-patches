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
#include "settings_type.h"
#include "vehicle_type.h"

extern uint8_t _extra_aspects;
extern uint64_t _aspect_cfg_hash;

inline uint8_t GetMaximumSignalAspect()
{
	return _extra_aspects + 1;
}

struct SignalStyleMasks {
	uint16_t non_aspect_inc = 0;
	uint16_t next_only = 0;
	uint16_t always_reserve_through = 0;
	uint16_t no_tunnel_bridge = 0;
	uint16_t signal_opposite_side = 0;
	uint16_t signal_both_sides = 0;
	uint16_t combined_normal_shunt = 0;
};
extern SignalStyleMasks _signal_style_masks;

extern bool _signal_sprite_oversized;

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction along with the trackdir.
 */
inline uint8_t SignalAlongTrackdir(Trackdir trackdir)
{
	extern const uint8_t _signal_along_trackdir[TRACKDIR_END];
	return _signal_along_trackdir[trackdir];
}

/**
 * Maps a trackdir to the bit that stores its status in the map arrays, in the
 * direction against the trackdir.
 */
inline uint8_t SignalAgainstTrackdir(Trackdir trackdir)
{
	extern const uint8_t _signal_against_trackdir[TRACKDIR_END];
	return _signal_against_trackdir[trackdir];
}

/**
 * Maps a Track to the bits that store the status of the two signals that can
 * be present on the given track.
 */
inline uint8_t SignalOnTrack(Track track)
{
	extern const uint8_t _signal_on_track[TRACK_END];
	return _signal_on_track[track];
}

/// Is a given signal type a presignal entry signal?
inline bool IsEntrySignal(SignalType type)
{
	return type == SIGTYPE_ENTRY || type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a presignal exit signal?
inline bool IsExitSignal(SignalType type)
{
	return type == SIGTYPE_EXIT || type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a presignal combo signal?
inline bool IsComboSignal(SignalType type)
{
	return type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Is a given signal type a PBS signal?
inline bool IsPbsSignal(SignalType type)
{
	return _settings_game.vehicle.train_braking_model == TBM_REALISTIC || type == SIGTYPE_PBS || type == SIGTYPE_PBS_ONEWAY || type == SIGTYPE_NO_ENTRY;
}

/// Is a given signal type a PBS signal?
inline bool IsPbsSignalNonExtended(SignalType type)
{
	return type == SIGTYPE_PBS || type == SIGTYPE_PBS_ONEWAY;
}

/// Is this a programmable pre-signal?
inline bool IsProgrammableSignal(SignalType type)
{
	return type == SIGTYPE_PROG;
}

/// Is this a programmable pre-signal?
inline bool IsNoEntrySignal(SignalType type)
{
	return type == SIGTYPE_NO_ENTRY;
}

/** One-way signals can't be passed the 'wrong' way. */
inline bool IsOnewaySignal(SignalType type)
{
	return type != SIGTYPE_PBS && type != SIGTYPE_NO_ENTRY;
}

/// Is this signal type unsuitable for realistic braking?
inline bool IsSignalTypeUnsuitableForRealisticBraking(SignalType type)
{
	return type == SIGTYPE_ENTRY || type == SIGTYPE_EXIT || type == SIGTYPE_COMBO || type == SIGTYPE_PROG;
}

/// Does a given signal have a PBS sprite?
inline bool IsSignalSpritePBS(SignalType type)
{
	return type >= SIGTYPE_FIRST_PBS_SPRITE;
}

SignalType NextSignalType(SignalType cur, SignalCycleGroups which_signals);

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
void UpdateSignalsInBufferIfOwnerNotAddable(Owner owner);
uint8_t GetForwardAspectFollowingTrack(TileIndex tile, Trackdir trackdir);
uint8_t GetSignalAspectGeneric(TileIndex tile, Trackdir trackdir, bool check_non_inc_style);
void PropagateAspectChange(TileIndex tile, Trackdir trackdir, uint8_t aspect);
void UpdateAspectDeferred(TileIndex tile, Trackdir trackdir);
void UpdateAspectDeferredWithVehicle(const Train *v, TileIndex tile, Trackdir trackdir, bool check_combined_normal_aspect);
void UpdateLookaheadCombinedNormalShuntSignalDeferred(TileIndex tile, Trackdir trackdir, int lookahead_position);
void FlushDeferredAspectUpdates();
void FlushDeferredDetermineCombineNormalShuntMode(Train *v);
void UpdateAllSignalAspects();
void UpdateExtraAspectsVariable(bool update_always_reserve_through = false);
void InitialiseExtraAspectsVariable();
bool IsRailSpecialSignalAspect(TileIndex tile, Track track);

inline void AdjustSignalAspectIfNonIncStyle(TileIndex tile, Track track, uint8_t &aspect)
{
	extern void AdjustSignalAspectIfNonIncStyleIntl(TileIndex tile, Track track, uint8_t &aspect);
	if (aspect > 0 && (_signal_style_masks.non_aspect_inc != 0 || _signal_style_masks.combined_normal_shunt != 0)) AdjustSignalAspectIfNonIncStyleIntl(tile, track, aspect);
}

inline uint8_t IncrementAspectForSignal(uint8_t aspect, bool combined_normal_mode)
{
	aspect = std::min<uint8_t>(aspect + 1, GetMaximumSignalAspect());
	if (combined_normal_mode) aspect = std::min<uint8_t>(aspect + 1, 7);
	return aspect;
}

inline uint8_t GetForwardAspectFollowingTrackAndIncrement(TileIndex tile, Trackdir trackdir, bool combined_normal_mode = false)
{
	return IncrementAspectForSignal(GetForwardAspectFollowingTrack(tile, trackdir), combined_normal_mode);
}

void UpdateSignalReserveThroughBit(TileIndex tile, Track track, bool update_signal);
void UpdateAllSignalReserveThroughBits();
void UpdateSignalSpecialPropagationFlag(TileIndex tile, Track track, const struct TraceRestrictProgram *prog, bool update_signal);
void UpdateRailSignalSpecialPropagationFlag(TileIndex tile, Track track, const struct TraceRestrictProgram *prog, bool update_signal);
void UpdateTunnelBridgeSignalSpecialPropagationFlag(TileIndex tile, bool update_signal);
void UpdateTunnelBridgeSignalSpecialPropagationFlag(TileIndex tile, Track track, const TraceRestrictProgram *prog, bool update_signal);
void UpdateAllSignalsSpecialPropagationFlag();

#endif /* SIGNAL_FUNC_H */
