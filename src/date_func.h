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
	extern Ticks _ticks_per_calendar_day;
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

inline bool ReplaceWallclockMinutesUnit()
{
	return DayLengthFactor() > 1 || _settings_time.time_in_minutes;
}

inline Ticks TicksPerCalendarDay()
{
	return DateDetail::_ticks_per_calendar_day;
}

void UpdateEffectiveDayLengthFactor();

inline constexpr EconTime::YearDelta DateDeltaToYearDelta(EconTime::DateDelta date)
{
	return EconTime::YearDelta{date.base() / DAYS_IN_LEAP_YEAR};
}

inline constexpr EconTime::DateTicksDelta DateDeltaToDateTicksDelta(EconTime::DateDelta date, uint16_t fract = 0)
{
	return EconTime::DateTicksDelta{((int64_t)date.base() * DAY_TICKS) + fract};
}

inline constexpr CalTime::YearDelta DateDeltaToYearDelta(CalTime::DateDelta date)
{
	return CalTime::YearDelta{date.base() / DAYS_IN_LEAP_YEAR};
}

inline constexpr CalTime::DateTicksDelta DateDeltaToDateTicksDelta(CalTime::DateDelta date, uint16_t fract = 0)
{
	return CalTime::DateTicksDelta{((int64_t)date.base() * DAY_TICKS) + fract};
}

inline EconTime::Date StateTicksToDate(StateTicks ticks)
{
	return EconTime::Date{static_cast<int>((ticks.base() - DateDetail::_state_ticks_offset.base()) / (DAY_TICKS * DayLengthFactor()))};
}

CalTime::Date StateTicksToCalendarDate(StateTicks ticks);

inline StateTicks DateToStateTicks(EconTime::Date date)
{
	return StateTicks{((int64_t)date.base() * DAY_TICKS * DayLengthFactor()) + DateDetail::_state_ticks_offset.base()};
}

inline EconTime::DateTicks StateTicksToDateTicks(StateTicks ticks)
{
	return EconTime::DateTicks{(ticks.base() - DateDetail::_state_ticks_offset.base()) / DayLengthFactor()};
}

inline StateTicks DateTicksToStateTicks(EconTime::DateTicks date_ticks)
{
	return StateTicks{((int64_t)date_ticks.base() * DayLengthFactor()) + DateDetail::_state_ticks_offset.base()};
}

inline Ticks TimetableDisplayUnitSize()
{
	if (_settings_time.time_in_minutes) {
		return _settings_time.ticks_per_minute;
	} else if (EconTime::UsingWallclockUnits()) {
		return TICKS_PER_SECOND;
	} else {
		return TicksPerCalendarDay();
	}
}

inline Ticks TimetableAbsoluteDisplayUnitSize()
{
	if (_settings_time.time_in_minutes) {
		return _settings_time.ticks_per_minute;
	} else {
		return TicksPerCalendarDay();
	}
}

/* Casts from economy date/year to the equivalent calendar type, this is only for use when not using wallclock mode or during saveload conversion */
inline CalTime::Date ToCalTimeCast(EconTime::Date date) { return CalTime::Date{date.base()}; }
inline CalTime::Year ToCalTimeCast(EconTime::Year year) { return CalTime::Year{year.base()}; }
inline CalTime::DateDelta ToCalTimeCast(EconTime::DateDelta date_delta) { return CalTime::DateDelta{date_delta.base()}; }
inline CalTime::YearDelta ToCalTimeCast(EconTime::YearDelta year_delta) { return CalTime::YearDelta{year_delta.base()}; }

/* Casts from calendar date/year to the equivalent economy type, this is only for use when not using wallclock mode or during saveload conversion */
inline EconTime::Date ToEconTimeCast(CalTime::Date date) { return EconTime::Date{date.base()}; }
inline EconTime::Year ToEconTimeCast(CalTime::Year year) { return EconTime::Year{year.base()}; }
inline EconTime::DateDelta ToEconTimeCast(CalTime::DateDelta date_delta) { return EconTime::DateDelta{date_delta.base()}; }
inline EconTime::YearDelta ToEconTimeCast(CalTime::YearDelta year_delta) { return EconTime::YearDelta{year_delta.base()}; }

struct debug_date_dumper {
	const char *HexDate(EconTime::Date date, EconTime::DateFract date_fract, uint8_t tick_skip_counter);

	inline const char *HexDate() { return this->HexDate(EconTime::CurDate(), EconTime::CurDateFract(), TickSkipCounter()); }

private:
	char buffer[24];
};

#endif /* DATE_FUNC_H */
