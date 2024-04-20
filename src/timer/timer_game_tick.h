/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timer_game_tick.h Definition of the tick-based game-timer */

#ifndef TIMER_GAME_TICK_H
#define TIMER_GAME_TICK_H

#include "../gfx_type.h"

#include <chrono>

/**
 * Timer that represents the game-ticks. It will pause when the game is paused.
 *
 * @note Callbacks are executed in the game-thread.
 */
class TimerGameTick {
public:
	enum Priority {
		NONE, ///< These timers can be executed in any order; the order is not relevant.

		/* For all other priorities, the order is important.
		 * For safety, you can only setup a single timer on a single priority. */
		COMPETITOR_TIMEOUT,
	};

	struct TPeriod {
		Priority priority;
		uint value;

		TPeriod(Priority priority, uint value) : priority(priority), value(value)
		{}

		bool operator < (const TPeriod &other) const
		{
			/* Sort by priority before value, such that changes in value for priorities other than NONE do not change the container order */
			if (this->priority != other.priority) return this->priority < other.priority;
			return this->value < other.value;
		}

		bool operator == (const TPeriod &other) const
		{
			return this->priority == other.priority && this->value == other.value;
		}
	};

	using TElapsed = uint;
	struct TStorage {
		uint elapsed;
	};
};

#endif /* TIMER_GAME_TICK_H */
