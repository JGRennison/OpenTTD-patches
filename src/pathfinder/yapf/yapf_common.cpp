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

using namespace std::literals::string_view_literals;

std::string ValueStr(EndSegmentReasons flags)
{
	static const std::initializer_list<std::string_view> end_segment_reason_names = {
		"DEAD_END"sv, "DEAD_END_EOL"sv, "RAIL_TYPE"sv, "INFINITE_LOOP"sv, "SEGMENT_TOO_LONG"sv, "CHOICE_FOLLOWS"sv,
		"DEPOT"sv, "WAYPOINT"sv, "STATION"sv, "SAFE_TILE"sv,
		"PATH_TOO_LONG"sv, "FIRST_TWO_WAY_RED"sv, "LOOK_AHEAD_END"sv, "TARGET_REACHED"sv,
		"REVERSE"sv
	};

	return fmt::format("0x{:04X} ({})", flags.base(), ComposeNameT(flags, end_segment_reason_names, "UNK"));
}
