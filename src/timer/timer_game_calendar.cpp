/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file timer_game_calendar.cpp
 * This file implements the timer logic for the game-calendar-timer.
 */

/**
 * Calendar time is used for technology and time-of-year changes, including:
 * - Vehicle, airport, station, object introduction and obsolescence
 * - Vehicle and engine age
 * - NewGRF variables for visual styles or behavior based on year or time of year (e.g. variable snow line)
 * - Inflation, since it is tied to original game years. One interpretation of inflation is that it compensates for faster and higher capacity vehicles,
 *   another is that it compensates for more established companies. Each of these point to a different choice of calendar versus economy time, but we have to pick one
 *   so we follow a previous decision to tie inflation to original TTD game years.
 */

#include "../stdafx.h"
#include "timer.h"
#include "timer_game_calendar.h"
#include "../core/bitmath_func.hpp"

#include "../safeguards.h"

template<>
void IntervalTimer<TimerGameCalendar>::Elapsed(TimerGameCalendar::TElapsed triggers)
{
	if (HasBit(triggers, this->period.trigger)) {
		this->callback(1);
	}
}

template<>
void TimeoutTimer<TimerGameCalendar>::Elapsed(TimerGameCalendar::TElapsed triggers)
{
	if (this->fired) return;

	if (HasBit(triggers, this->period.trigger)) {
		this->callback();
		this->fired = true;
	}
}

template<>
void TimerManager<TimerGameCalendar>::Elapsed(TimerGameCalendar::TElapsed triggers)
{
	for (auto timer : TimerManager<TimerGameCalendar>::GetTimerVector()) {
		timer->Elapsed(triggers);
	}
}

#ifdef WITH_ASSERT
template<>
void TimerManager<TimerGameCalendar>::Validate(TimerGameCalendar::TPeriod period)
{
	if (period.priority == TimerGameCalendar::Priority::NONE) return;

	/* Validate we didn't make a developer error and scheduled more than one
	 * entry on the same priority/trigger. There can only be one timer on
	 * a specific trigger/priority, to ensure we are deterministic. */
	for (const auto &timer : TimerManager<TimerGameCalendar>::GetTimers()) {
		if (timer->period.trigger != period.trigger) continue;

		assert(timer->period.priority != period.priority);
	}
}
#endif /* WITH_ASSERT */
