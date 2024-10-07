/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file yapf_common.cpp Pathfinding common functions. */

#include "../../stdafx.h"

#include "yapf.hpp"
#include "../../core/format.hpp"
#include "../../misc/dbg_helpers.h"

#include "../../safeguards.h"

std::string ValueStr(EndSegmentReasonBits bits)
{
	static const char * const end_segment_reason_names[] = {
		"DEAD_END", "DEAD_END_EOL", "RAIL_TYPE", "INFINITE_LOOP", "SEGMENT_TOO_LONG", "CHOICE_FOLLOWS",
		"DEPOT", "WAYPOINT", "STATION", "SAFE_TILE",
		"PATH_TOO_LONG", "FIRST_TWO_WAY_RED", "LOOK_AHEAD_END", "TARGET_REACHED",
		"REVERSE"
	};

	return fmt::format("0x{:04X} ({})", bits, ComposeNameT(bits, end_segment_reason_names, "UNK", ESRB_NONE, "NONE"));
}
