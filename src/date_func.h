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
extern uint64    _tick_counter;
extern uint8     _tick_skip_counter;
extern uint64    _scaled_tick_counter;
extern DateTicksScaled _scaled_date_ticks;
extern DateTicksScaled _scaled_date_ticks_offset;
extern uint32    _quit_after_days;

extern YearMonthDay _game_load_cur_date_ymd;
extern DateFract _game_load_date_fract;
extern uint8 _game_load_tick_skip_counter;

void SetDate(Date date, DateFract fract, bool preserve_scaled_ticks = true);
void ConvertDateToYMD(Date date, YearMonthDay *ymd);
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
static inline bool IsLeapYear(Year yr)
{
	return yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0);
}

static inline Date ScaledDateTicksToDate(DateTicksScaled ticks)
{
	return (ticks.base() - _scaled_date_ticks_offset.base()) / (DAY_TICKS * _settings_game.economy.day_length_factor);
}

static inline DateTicksScaled DateToScaledDateTicks(Date date)
{
	return ((int64)date.base() * DAY_TICKS * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset.base();
}

static inline DateTicks ScaledDateTicksToDateTicks(DateTicksScaled ticks)
{
	return (ticks.base() - _scaled_date_ticks_offset.base()) / _settings_game.economy.day_length_factor;
}

static inline DateTicksScaled DateTicksToScaledDateTicks(DateTicks date_ticks)
{
	return ((int64)date_ticks.base() * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset.base();
}

/**
 * Calculate the year of a given date.
 * @param date The date to consider.
 * @return the year.
 */
static constexpr Year DateToYear(Date date)
{
	return date.base() / DAYS_IN_LEAP_YEAR;
}

static constexpr Year DateDeltaToYears(DateDelta date)
{
	return date.base() / DAYS_IN_LEAP_YEAR;
}

static constexpr DateTicks DateToDateTicks(Date date, DateFract fract = 0)
{
	return (date.base() * DAY_TICKS) + fract;
}

static constexpr DateTicksDelta DateDeltaToDateTicksDelta(DateDelta date, DateFract fract = 0)
{
	return (date.base() * DAY_TICKS) + fract;
}

static inline DateTicks NowDateTicks()
{
	return DateToDateTicks(_date, _date_fract);
}

#endif /* DATE_FUNC_H */
