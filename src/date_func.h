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
#include <utility>

extern YearMonthDay _cur_date_ymd;
extern Date      _date;
extern DateFract _date_fract;
extern uint64_t  _tick_counter;
extern uint8_t   _tick_skip_counter;
extern uint64_t  _scaled_tick_counter;
extern DateTicksScaled _scaled_date_ticks;
extern DateTicksScaled _scaled_date_ticks_offset;
extern uint32_t  _quit_after_days;

extern YearMonthDay _game_load_cur_date_ymd;
extern DateFract _game_load_date_fract;
extern uint8_t _game_load_tick_skip_counter;

void SetDate(Date date, DateFract fract, bool preserve_scaled_ticks = true);
YearMonthDay ConvertDateToYMD(Date date);
Date ConvertYMDToDate(Year year, Month month, Day day);
void SetScaledTickVariables();

inline Date ConvertYMDToDate(const YearMonthDay &ymd)
{
	return ConvertYMDToDate(ymd.year, ymd.month, ymd.day);
}

#define _cur_year (_cur_date_ymd.year)

/**
 * Checks whether the given year is a leap year or not.
 * @param yr The year to check.
 * @return True if \c yr is a leap year, otherwise false.
 */
inline bool IsLeapYear(Year yr)
{
	return yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0);
}

inline Date ScaledDateTicksToDate(DateTicksScaled ticks)
{
	return (ticks.base() - _scaled_date_ticks_offset.base()) / (DAY_TICKS * _settings_game.economy.day_length_factor);
}

inline DateTicksScaled DateToScaledDateTicks(Date date)
{
	return ((int64_t)date.base() * DAY_TICKS * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset.base();
}

inline DateTicks ScaledDateTicksToDateTicks(DateTicksScaled ticks)
{
	return (ticks.base() - _scaled_date_ticks_offset.base()) / _settings_game.economy.day_length_factor;
}

inline DateTicksScaled DateTicksToScaledDateTicks(DateTicks date_ticks)
{
	return ((int64_t)date_ticks.base() * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset.base();
}

/**
 * Calculate the year of a given date.
 * @param date The date to consider.
 * @return the year.
 */
inline constexpr Year DateToYear(Date date)
{
	return date.base() / DAYS_IN_LEAP_YEAR;
}

inline constexpr Year DateDeltaToYears(DateDelta date)
{
	return date.base() / DAYS_IN_LEAP_YEAR;
}

inline constexpr DateTicks DateToDateTicks(Date date, DateFract fract = 0)
{
	return ((int64_t)date.base() * DAY_TICKS) + fract;
}

inline constexpr DateTicksDelta DateDeltaToDateTicksDelta(DateDelta date, DateFract fract = 0)
{
	return ((int64_t)date.base() * DAY_TICKS) + fract;
}

inline DateTicks NowDateTicks()
{
	return DateToDateTicks(_date, _date_fract);
}

struct debug_date_dumper {
	const char *HexDate(Date date, DateFract date_fract, uint8_t tick_skip_counter);

	inline const char *HexDate() { return this->HexDate(_date, _date_fract, _tick_skip_counter); }

private:
	char buffer[24];
};

#endif /* DATE_FUNC_H */
