/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file date_func.h Functions related to dates. */

#ifndef DATE_FUNC_H
#define DATE_FUNC_H

#include "date_type.h"
#include "settings_type.h"

extern uint64_t  _tick_counter;
extern ScaledTickCounter _scaled_tick_counter;
extern StateTicks _state_ticks;
extern uint32_t  _quit_after_days;

namespace DateDetail {
	extern StateTicksDelta _state_ticks_offset;
	extern uint8_t _tick_skip_counter;
	extern uint8_t _effective_day_length;
};

StateTicks GetStateTicksFromDateWithoutOffset(EconTime::Date date, EconTime::DateFract date_fract);
void RecalculateStateTicksOffset();

inline uint8_t TickSkipCounter()
{
	return DateDetail::_tick_skip_counter;
}

inline uint8_t DayLengthFactor()
{
	return DateDetail::_effective_day_length;
}

void UpdateEffectiveDayLengthFactor();

inline constexpr YearDelta DateDeltaToYearDelta(DateDelta date)
{
	return date.base() / DAYS_IN_LEAP_YEAR;
}

inline constexpr DateTicksDelta DateDeltaToDateTicksDelta(DateDelta date, uint16_t fract = 0)
{
	return ((int64_t)date.base() * DAY_TICKS) + fract;
}

inline EconTime::Date StateTicksToDate(StateTicks ticks)
{
	return (ticks.base() - DateDetail::_state_ticks_offset.base()) / (DAY_TICKS * DayLengthFactor());
}

CalTime::Date StateTicksToCalendarDate(StateTicks ticks);

inline StateTicks DateToStateTicks(EconTime::Date date)
{
	return ((int64_t)date.base() * DAY_TICKS * DayLengthFactor()) + DateDetail::_state_ticks_offset.base();
}

inline EconTime::DateTicks StateTicksToDateTicks(StateTicks ticks)
{
	return (ticks.base() - DateDetail::_state_ticks_offset.base()) / DayLengthFactor();
}

inline StateTicks DateTicksToStateTicks(EconTime::DateTicks date_ticks)
{
	return ((int64_t)date_ticks.base() * DayLengthFactor()) + DateDetail::_state_ticks_offset.base();
}

inline Ticks TimetableDisplayUnitSize()
{
	if (_settings_time.time_in_minutes) {
		return _settings_time.ticks_per_minute;
	} else {
		return DAY_TICKS * DayLengthFactor();
	}
}

struct debug_date_dumper {
	const char *HexDate(EconTime::Date date, EconTime::DateFract date_fract, uint8_t tick_skip_counter);

	inline const char *HexDate() { return this->HexDate(EconTime::CurDate(), EconTime::CurDateFract(), TickSkipCounter()); }

private:
	char buffer[24];
};

#endif /* DATE_FUNC_H */
