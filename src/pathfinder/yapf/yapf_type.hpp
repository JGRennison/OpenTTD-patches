/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_type.hpp Types used by YAPF. */

#ifndef YAPF_TYPE_HPP
#define YAPF_TYPE_HPP

#include "../../core/enum_type.hpp"

/* Enum used in PfCalcCost() to see why was the segment closed. */
enum EndSegmentReason : uint8_t {
	/* The following reasons can be saved into cached segment */
	ESR_DEAD_END = 0,      ///< track ends here
	ESR_DEAD_END_EOL,      ///< track ends here bit refers to the next tile, the last tile of the segment itself is usable
	ESR_RAIL_TYPE,         ///< the next tile has a different rail type than our tiles
	ESR_INFINITE_LOOP,     ///< infinite loop detected
	ESR_SEGMENT_TOO_LONG,  ///< the segment is too long (possible infinite loop)
	ESR_CHOICE_FOLLOWS,    ///< the next tile contains a choice (the track splits to more than one segments)
	ESR_DEPOT,             ///< stop in the depot (could be a target next time)
	ESR_WAYPOINT,          ///< waypoint encountered (could be a target next time)
	ESR_STATION,           ///< station encountered (could be a target next time)
	ESR_SAFE_TILE,         ///< safe waiting position found (could be a target)

	/* The following reasons are used only internally by PfCalcCost().
	 *  They should not be found in the cached segment. */
	ESR_PATH_TOO_LONG,     ///< the path is too long (searching for the nearest depot in the given radius)
	ESR_FIRST_TWO_WAY_RED, ///< first signal was 2-way and it was red
	ESR_LOOK_AHEAD_END,    ///< we have just passed the last look-ahead signal
	ESR_TARGET_REACHED,    ///< we have just reached the destination
	ESR_REVERSE,           ///< we should reverse after this point

	/* Special values */
	ESR_NONE = 0xFF,          ///< no reason to end the segment here
};

enum EndSegmentReasonBits : uint16_t {
	ESRB_NONE = 0,

	ESRB_DEAD_END          = 1 << ESR_DEAD_END,
	ESRB_DEAD_END_EOL      = 1 << ESR_DEAD_END_EOL,
	ESRB_RAIL_TYPE         = 1 << ESR_RAIL_TYPE,
	ESRB_INFINITE_LOOP     = 1 << ESR_INFINITE_LOOP,
	ESRB_SEGMENT_TOO_LONG  = 1 << ESR_SEGMENT_TOO_LONG,
	ESRB_CHOICE_FOLLOWS    = 1 << ESR_CHOICE_FOLLOWS,
	ESRB_DEPOT             = 1 << ESR_DEPOT,
	ESRB_WAYPOINT          = 1 << ESR_WAYPOINT,
	ESRB_STATION           = 1 << ESR_STATION,
	ESRB_SAFE_TILE         = 1 << ESR_SAFE_TILE,

	ESRB_PATH_TOO_LONG     = 1 << ESR_PATH_TOO_LONG,
	ESRB_FIRST_TWO_WAY_RED = 1 << ESR_FIRST_TWO_WAY_RED,
	ESRB_LOOK_AHEAD_END    = 1 << ESR_LOOK_AHEAD_END,
	ESRB_TARGET_REACHED    = 1 << ESR_TARGET_REACHED,
	ESRB_REVERSE           = 1 << ESR_REVERSE,

	/* Additional (composite) values. */

	/* What reasons mean that the target can be found and needs to be detected. */
	ESRB_POSSIBLE_TARGET = ESRB_DEPOT | ESRB_WAYPOINT | ESRB_STATION | ESRB_SAFE_TILE,

	/* What reasons can be stored back into cached segment. */
	ESRB_CACHED_MASK = ESRB_DEAD_END | ESRB_DEAD_END_EOL | ESRB_RAIL_TYPE | ESRB_INFINITE_LOOP | ESRB_SEGMENT_TOO_LONG | ESRB_CHOICE_FOLLOWS | ESRB_DEPOT | ESRB_WAYPOINT | ESRB_STATION | ESRB_SAFE_TILE | ESRB_REVERSE,

	/* Reasons to abort pathfinding in this direction. */
	ESRB_ABORT_PF_MASK = ESRB_DEAD_END | ESRB_PATH_TOO_LONG | ESRB_INFINITE_LOOP | ESRB_FIRST_TWO_WAY_RED,

	/* Reasons to abort pathfinding in this direction, when reversing is pending. */
	ESRB_ABORT_PF_MASK_PENDING_REVERSE = ESRB_ABORT_PF_MASK & ~ESRB_DEAD_END,
};

DECLARE_ENUM_AS_BIT_SET(EndSegmentReasonBits)

std::string ValueStr(EndSegmentReasonBits bits);

#endif /* YAPF_TYPE_HPP */
