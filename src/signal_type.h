/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signal_type.h Types and classes related to signals. */

#ifndef SIGNAL_TYPE_H
#define SIGNAL_TYPE_H

#include "core/enum_type.hpp"
#include "track_type.h"
#include "tile_type.h"
#include "zoom_type.h"

/** Variant of the signal, i.e. how does the signal look? */
enum SignalVariant : uint8_t {
	SIG_ELECTRIC  = 0, ///< Light signal
	SIG_SEMAPHORE = 1, ///< Old-fashioned semaphore signal
};


/** Type of signal, i.e. how does the signal behave? */
enum SignalType : uint8_t {
	SIGTYPE_BLOCK      = 0, ///< block signal
	SIGTYPE_ENTRY      = 1, ///< presignal block entry
	SIGTYPE_EXIT       = 2, ///< presignal block exit
	SIGTYPE_COMBO      = 3, ///< presignal inter-block
	SIGTYPE_PBS        = 4, ///< normal pbs signal
	SIGTYPE_PBS_ONEWAY = 5, ///< one-way PBS signal
	SIGTYPE_PROG       = 6, ///< programmable presignal
	SIGTYPE_NO_ENTRY   = 7, ///< no-entry signal

	SIGTYPE_END,
	SIGTYPE_LAST       = SIGTYPE_NO_ENTRY,
	SIGTYPE_FIRST_PBS_SPRITE = SIGTYPE_PBS,
};
/** Helper information for extract tool. */
template <> struct EnumPropsT<SignalType> : MakeEnumPropsT<SignalType, uint8_t, SIGTYPE_BLOCK, SIGTYPE_END, SIGTYPE_END, 3> {};

/** Reference to a signal
 *
 * A reference to a signal by its tile and track
 */
struct SignalReference {
	inline SignalReference(TileIndex t, Track tr) : tile(t), track(tr) {}
	inline bool operator<(const SignalReference& o) const { return tile < o.tile || (tile == o.tile && track < o.track); }
	inline bool operator==(const SignalReference& o) const { return tile == o.tile && track == o.track; }
	inline bool operator!=(const SignalReference& o) const { return tile != o.tile || track != o.track; }

	TileIndex tile;
	Track track;
};
DECLARE_ENUM_AS_ADDABLE(SignalType)

/**
 * These are states in which a signal can be. Currently these are only two, so
 * simple boolean logic will do. But do try to compare to this enum instead of
 * normal boolean evaluation, since that will make future additions easier.
 */
enum SignalState {
	SIGNAL_STATE_RED   = 0, ///< The signal is red
	SIGNAL_STATE_GREEN = 1, ///< The signal is green
	SIGNAL_STATE_MAX = SIGNAL_STATE_GREEN,
};

/** Signal groups to cycle through. */
enum SignalCycleGroups : uint8_t {
	SCG_CURRENT_GROUP = 0,
	SCG_BLOCK         = 1 << 0,
	SCG_PBS           = 1 << 1,
};
DECLARE_ENUM_AS_BIT_SET(SignalCycleGroups)

static const int SIGNAL_DIRTY_LEFT   = 14 * ZOOM_LVL_BASE;
static const int SIGNAL_DIRTY_RIGHT  = 14 * ZOOM_LVL_BASE;
static const int SIGNAL_DIRTY_TOP    = 30 * ZOOM_LVL_BASE;
static const int SIGNAL_DIRTY_BOTTOM =  5 * ZOOM_LVL_BASE;

#endif /* SIGNAL_TYPE_H */
