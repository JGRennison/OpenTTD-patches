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

#define _cur_year (static_cast<Year>(_cur_date_ymd.year))

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
	return (ticks - _scaled_date_ticks_offset) / (DAY_TICKS * _settings_game.economy.day_length_factor);
}

static inline DateTicksScaled DateToScaledDateTicks(Date date)
{
	return ((int64)date * DAY_TICKS * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset;
}

static inline DateTicks ScaledDateTicksToDateTicks(DateTicksScaled ticks)
{
	return (ticks - _scaled_date_ticks_offset) / _settings_game.economy.day_length_factor;
}

static inline DateTicksScaled DateTicksToScaledDateTicks(DateTicks date_ticks)
{
	return ((int64)date_ticks * _settings_game.economy.day_length_factor) + _scaled_date_ticks_offset;
}

/**
 * Calculate the year of a given date.
 * @param date The date to consider.
 * @return the year.
 */
static constexpr Year DateToYear(Date date)
{
	return date / DAYS_IN_LEAP_YEAR;
}

/**
 * Calculate the date of the first day of a given year.
 * @param year the year to get the first day of.
 * @return the date.
 */
static constexpr Date DateAtStartOfYear(Year year)
{
	int32 year_as_int = year;
	uint number_of_leap_years = (year == 0) ? 0 : ((year_as_int - 1) / 4 - (year_as_int - 1) / 100 + (year_as_int - 1) / 400 + 1);

	/* Hardcode the number of days in a year because we can't access CalendarTime from here. */
	return (365 * year_as_int) + number_of_leap_years;
}

#endif /* DATE_FUNC_H */
