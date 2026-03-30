/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file timer_game_common.h Definition of the common class inherited by both calendar and economy timers. */

#ifndef TIMER_GAME_COMMON_H
#define TIMER_GAME_COMMON_H

/**
 * Template class for all TimerGame based timers. As Calendar and Economy are very similar, this class is used to share code between them.
 *
 * IntervalTimer and TimeoutTimer based on this Timer are a bit unusual, as their count is always one.
 * You create those timers based on a transition: a new day, a new month or a new year.
 *
 * Additionally, you need to set a priority. To ensure deterministic behaviour, events are executed
 * in priority. It is important that if you assign NONE, you do not use Random() in your callback.
 * Other than that, make sure you only set one callback per priority.
 *
 * For example:
 *   IntervalTimer<TimerGameCalendar>({TimerGameCalendar::Trigger::Day, TimerGameCalendar::Priority::None}, [](uint count) {});
 *
 * @note Callbacks are executed in the game-thread.
 */
template <class T>
class TimerGame {
public:
	/** Trigger reasons for the timer based triggers. */
	enum class Trigger : uint8_t {
		Day, ///< Triggeres daily.
		Week, ///< Triggers every Tuesday.
		Month, ///< Triggered at the first of the month.
		Quarter, ///< Triggered every first of January, April, July and October.
		Year, ///< Triggered every first of January.
	};

	/** Different levels of priority to run the timers in. */
	enum class Priority : uint8_t {
		None, ///< These timers can be executed in any order; there is no Random() in them, so order is not relevant.

		/* All other may have a Random() call in them, so order is important.
		 * For safety, you can only setup a single timer on a single priority. */

		Company, ///< Changes to companies.
		Disaster, ///< Running disaster logic.
		Engine, ///< Running engine availability updates.
		Industry, ///< Running industry production changes.
		Station, ///< Processing of goods statistics.
		Subsidy, ///< Creating new subsidies.
		Town, ///< Town growth and rating management.
		Vehicle, ///< Income and profit warnings.
	};

	struct TPeriod {
		Trigger trigger;
		Priority priority;

		TPeriod(Trigger trigger, Priority priority) : trigger(trigger), priority(priority)
		{}

		bool operator < (const TPeriod &other) const
		{
			if (this->trigger != other.trigger) return this->trigger < other.trigger;
			return this->priority < other.priority;
		}

		bool operator == (const TPeriod &other) const
		{
			return this->trigger == other.trigger && this->priority == other.priority;
		}
	};

	struct TStorage {};
};

#endif /* TIMER_GAME_COMMON_H */
