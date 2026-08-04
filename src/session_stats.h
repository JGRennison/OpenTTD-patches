/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file session_stats.h Session stats. */

#ifndef SESSION_STATS_H
#define SESSION_STATS_H

#include "time_type.h"
#include <optional>

struct GameSessionStats {
	std::optional<TimeType::SteadyTimePoint> start_time;             ///< Time when the current game was started.
	std::string savegame_id;                                         ///< Unique ID of the savegame.
	std::optional<size_t> savegame_size;                             ///< Size of the last saved savegame in bytes, or std::nullopt if not saved yet.
};

extern GameSessionStats _game_session_stats;

#endif /* SESSION_STATS_H */
