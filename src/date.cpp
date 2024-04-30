/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file date.cpp Handling of dates in our native format and transforming them to something human readable. */

#include "stdafx.h"
#include "network/network.h"
#include "network/network_func.h"
#include "currency.h"
#include "window_func.h"
#include "settings_type.h"
#include "date_func.h"
#include "vehicle_base.h"
#include "rail_gui.h"
#include "linkgraph/linkgraphschedule.h"
#include "sl/saveload.h"
#include "newgrf_profiling.h"
#include "console_func.h"
#include "debug.h"
#include "landscape.h"
#include "widgets/statusbar_widget.h"
#include "event_logs.h"

#include "safeguards.h"

uint64_t _tick_counter;                    ///< Ever incrementing tick counter for setting off various events
ScaledTickCounter _scaled_tick_counter;    ///< Tick counter in daylength-scaled ticks
StateTicks _state_ticks;                   ///< Current state tick
uint32_t _quit_after_days;                 ///< Quit after this many days of run time

CalTime::State CalTime::Detail::now;
EconTime::State EconTime::Detail::now;
YearDelta EconTime::Detail::years_elapsed;
YearDelta EconTime::Detail::period_display_offset;

namespace DateDetail {
	StateTicksDelta _state_ticks_offset;   ///< Offset to add when calculating a StateTicks value from an economy date, date fract and tick skip counter
	uint8_t _tick_skip_counter;            ///< Counter for ticks, when only vehicles are moving and nothing else happens
	uint8_t _effective_day_length;         ///< Current effective day length
	Ticks _ticks_per_calendar_day;         ///< Current ticks per calendar day
};

extern void ClearOutOfDateSignalSpeedRestrictions();

void CheckStateTicksWrap()
{
	StateTicksDelta tick_adjust = 0;
	auto get_tick_adjust = [&](StateTicksDelta target) {
		int32_t rounding = _settings_time.time_in_minutes * 1440;
		return target - (target.base() % rounding);
	};
	if (_state_ticks >= ((int64_t)1 << 60)) {
		tick_adjust = get_tick_adjust(_state_ticks - INITIAL_STATE_TICKS_VALUE);
	} else if (_state_ticks <= -((int64_t)1 << 60)) {
		tick_adjust = -get_tick_adjust(INITIAL_STATE_TICKS_VALUE - _state_ticks);
	} else {
		return;
	}

	DateDetail::_state_ticks_offset -= tick_adjust;
	_state_ticks -= tick_adjust;
	_game_load_state_ticks -= tick_adjust;

	extern void AdjustAllSignalSpeedRestrictionTickValues(StateTicksDelta delta);
	AdjustAllSignalSpeedRestrictionTickValues(-tick_adjust);

	extern void AdjustVehicleStateTicksBase(StateTicksDelta delta);
	AdjustVehicleStateTicksBase(-tick_adjust);
}

/**
 * Set the date.
 * @param date  New date
 * @param fract The number of ticks that have passed on this date.
 */
void CalTime::Detail::SetDate(CalTime::Date date, CalTime::DateFract fract)
{
	assert(fract < DAY_TICKS);

	CalTime::Detail::now.cal_date = date;
	CalTime::Detail::now.cal_date_fract = fract;
	CalTime::Detail::now.cal_ymd = CalTime::ConvertDateToYMD(date);
	UpdateCachedSnowLine();
}

void EconTime::Detail::SetDate(EconTime::Date date, EconTime::DateFract fract)
{
	assert(fract < DAY_TICKS);

	EconTime::Detail::now.econ_date = date;
	EconTime::Detail::now.econ_date_fract = fract;
	EconTime::Detail::now.econ_ymd = EconTime::ConvertDateToYMD(date);
	RecalculateStateTicksOffset();
}

CalTime::State CalTime::Detail::NewState(CalTime::Year year)
{
	CalTime::State state{};
	state.cal_ymd = { year, 0, 1 };
	state.cal_date = CalTime::ConvertYMDToDate(year, 0, 1);
	return state;
}

EconTime::State EconTime::Detail::NewState(EconTime::Year year)
{
	EconTime::State state{};
	state.econ_ymd = { year, 0, 1 };
	state.econ_date = EconTime::ConvertYMDToDate(year, 0, 1);
	return state;
}

int32_t EconTime::Detail::WallClockYearToDisplay(EconTime::Year year)
{
	return (year + EconTime::Detail::period_display_offset).base();
}

StateTicks GetStateTicksFromDateWithoutOffset(EconTime::Date date, EconTime::DateFract date_fract)
{
	return ((int64_t)(EconTime::DateToDateTicks(date, date_fract).base()) * DayLengthFactor()) + TickSkipCounter();
}

void RecalculateStateTicksOffset()
{
	DateDetail::_state_ticks_offset = _state_ticks - GetStateTicksFromDateWithoutOffset(EconTime::CurDate(), EconTime::CurDateFract());
}

void UpdateEffectiveDayLengthFactor()
{
	DateDetail::_effective_day_length = _settings_game.EffectiveDayLengthFactor();

	if (EconTime::UsingWallclockUnits()) {
		if (CalTime::IsCalendarFrozen()) {
			DateDetail::_ticks_per_calendar_day = INT32_MAX;
		} else {
			DateDetail::_ticks_per_calendar_day = (_settings_game.economy.minutes_per_calendar_year * DAY_TICKS) / CalTime::DEF_MINUTES_PER_YEAR;
		}
	} else {
		DateDetail::_ticks_per_calendar_day = DAY_TICKS * DateDetail::_effective_day_length;
	}

	SetupTileLoopCounts();
	UpdateCargoScalers();
}

CalTime::Date StateTicksToCalendarDate(StateTicks ticks)
{
	if (!EconTime::UsingWallclockUnits()) return StateTicksToDate(ticks).base();

	if (CalTime::IsCalendarFrozen()) return CalTime::CurDate();

	Ticks ticks_per_cal_day = TicksPerCalendarDay();
	uint subticks_left_this_day = ((DAY_TICKS - CalTime::CurDateFract()) * ticks_per_cal_day) - CalTime::CurSubDateFract();
	Ticks ticks_into_this_day = ticks_per_cal_day - CeilDiv(subticks_left_this_day, DAY_TICKS);

	return CalTime::CurDate().base() + (int32_t)(((ticks - _state_ticks).base() + ticks_into_this_day) / ticks_per_cal_day);
}

#define M(a, b) ((a << 5) | b)
static const uint16_t _month_date_from_year_day[] = {
	M( 0, 1), M( 0, 2), M( 0, 3), M( 0, 4), M( 0, 5), M( 0, 6), M( 0, 7), M( 0, 8), M( 0, 9), M( 0, 10), M( 0, 11), M( 0, 12), M( 0, 13), M( 0, 14), M( 0, 15), M( 0, 16), M( 0, 17), M( 0, 18), M( 0, 19), M( 0, 20), M( 0, 21), M( 0, 22), M( 0, 23), M( 0, 24), M( 0, 25), M( 0, 26), M( 0, 27), M( 0, 28), M( 0, 29), M( 0, 30), M( 0, 31),
	M( 1, 1), M( 1, 2), M( 1, 3), M( 1, 4), M( 1, 5), M( 1, 6), M( 1, 7), M( 1, 8), M( 1, 9), M( 1, 10), M( 1, 11), M( 1, 12), M( 1, 13), M( 1, 14), M( 1, 15), M( 1, 16), M( 1, 17), M( 1, 18), M( 1, 19), M( 1, 20), M( 1, 21), M( 1, 22), M( 1, 23), M( 1, 24), M( 1, 25), M( 1, 26), M( 1, 27), M( 1, 28), M( 1, 29),
	M( 2, 1), M( 2, 2), M( 2, 3), M( 2, 4), M( 2, 5), M( 2, 6), M( 2, 7), M( 2, 8), M( 2, 9), M( 2, 10), M( 2, 11), M( 2, 12), M( 2, 13), M( 2, 14), M( 2, 15), M( 2, 16), M( 2, 17), M( 2, 18), M( 2, 19), M( 2, 20), M( 2, 21), M( 2, 22), M( 2, 23), M( 2, 24), M( 2, 25), M( 2, 26), M( 2, 27), M( 2, 28), M( 2, 29), M( 2, 30), M( 2, 31),
	M( 3, 1), M( 3, 2), M( 3, 3), M( 3, 4), M( 3, 5), M( 3, 6), M( 3, 7), M( 3, 8), M( 3, 9), M( 3, 10), M( 3, 11), M( 3, 12), M( 3, 13), M( 3, 14), M( 3, 15), M( 3, 16), M( 3, 17), M( 3, 18), M( 3, 19), M( 3, 20), M( 3, 21), M( 3, 22), M( 3, 23), M( 3, 24), M( 3, 25), M( 3, 26), M( 3, 27), M( 3, 28), M( 3, 29), M( 3, 30),
	M( 4, 1), M( 4, 2), M( 4, 3), M( 4, 4), M( 4, 5), M( 4, 6), M( 4, 7), M( 4, 8), M( 4, 9), M( 4, 10), M( 4, 11), M( 4, 12), M( 4, 13), M( 4, 14), M( 4, 15), M( 4, 16), M( 4, 17), M( 4, 18), M( 4, 19), M( 4, 20), M( 4, 21), M( 4, 22), M( 4, 23), M( 4, 24), M( 4, 25), M( 4, 26), M( 4, 27), M( 4, 28), M( 4, 29), M( 4, 30), M( 4, 31),
	M( 5, 1), M( 5, 2), M( 5, 3), M( 5, 4), M( 5, 5), M( 5, 6), M( 5, 7), M( 5, 8), M( 5, 9), M( 5, 10), M( 5, 11), M( 5, 12), M( 5, 13), M( 5, 14), M( 5, 15), M( 5, 16), M( 5, 17), M( 5, 18), M( 5, 19), M( 5, 20), M( 5, 21), M( 5, 22), M( 5, 23), M( 5, 24), M( 5, 25), M( 5, 26), M( 5, 27), M( 5, 28), M( 5, 29), M( 5, 30),
	M( 6, 1), M( 6, 2), M( 6, 3), M( 6, 4), M( 6, 5), M( 6, 6), M( 6, 7), M( 6, 8), M( 6, 9), M( 6, 10), M( 6, 11), M( 6, 12), M( 6, 13), M( 6, 14), M( 6, 15), M( 6, 16), M( 6, 17), M( 6, 18), M( 6, 19), M( 6, 20), M( 6, 21), M( 6, 22), M( 6, 23), M( 6, 24), M( 6, 25), M( 6, 26), M( 6, 27), M( 6, 28), M( 6, 29), M( 6, 30), M( 6, 31),
	M( 7, 1), M( 7, 2), M( 7, 3), M( 7, 4), M( 7, 5), M( 7, 6), M( 7, 7), M( 7, 8), M( 7, 9), M( 7, 10), M( 7, 11), M( 7, 12), M( 7, 13), M( 7, 14), M( 7, 15), M( 7, 16), M( 7, 17), M( 7, 18), M( 7, 19), M( 7, 20), M( 7, 21), M( 7, 22), M( 7, 23), M( 7, 24), M( 7, 25), M( 7, 26), M( 7, 27), M( 7, 28), M( 7, 29), M( 7, 30), M( 7, 31),
	M( 8, 1), M( 8, 2), M( 8, 3), M( 8, 4), M( 8, 5), M( 8, 6), M( 8, 7), M( 8, 8), M( 8, 9), M( 8, 10), M( 8, 11), M( 8, 12), M( 8, 13), M( 8, 14), M( 8, 15), M( 8, 16), M( 8, 17), M( 8, 18), M( 8, 19), M( 8, 20), M( 8, 21), M( 8, 22), M( 8, 23), M( 8, 24), M( 8, 25), M( 8, 26), M( 8, 27), M( 8, 28), M( 8, 29), M( 8, 30),
	M( 9, 1), M( 9, 2), M( 9, 3), M( 9, 4), M( 9, 5), M( 9, 6), M( 9, 7), M( 9, 8), M( 9, 9), M( 9, 10), M( 9, 11), M( 9, 12), M( 9, 13), M( 9, 14), M( 9, 15), M( 9, 16), M( 9, 17), M( 9, 18), M( 9, 19), M( 9, 20), M( 9, 21), M( 9, 22), M( 9, 23), M( 9, 24), M( 9, 25), M( 9, 26), M( 9, 27), M( 9, 28), M( 9, 29), M( 9, 30), M( 9, 31),
	M(10, 1), M(10, 2), M(10, 3), M(10, 4), M(10, 5), M(10, 6), M(10, 7), M(10, 8), M(10, 9), M(10, 10), M(10, 11), M(10, 12), M(10, 13), M(10, 14), M(10, 15), M(10, 16), M(10, 17), M(10, 18), M(10, 19), M(10, 20), M(10, 21), M(10, 22), M(10, 23), M(10, 24), M(10, 25), M(10, 26), M(10, 27), M(10, 28), M(10, 29), M(10, 30),
	M(11, 1), M(11, 2), M(11, 3), M(11, 4), M(11, 5), M(11, 6), M(11, 7), M(11, 8), M(11, 9), M(11, 10), M(11, 11), M(11, 12), M(11, 13), M(11, 14), M(11, 15), M(11, 16), M(11, 17), M(11, 18), M(11, 19), M(11, 20), M(11, 21), M(11, 22), M(11, 23), M(11, 24), M(11, 25), M(11, 26), M(11, 27), M(11, 28), M(11, 29), M(11, 30), M(11, 31),
};
#undef M

enum DaysTillMonth {
	ACCUM_JAN = 0,
	ACCUM_FEB = ACCUM_JAN + 31,
	ACCUM_MAR = ACCUM_FEB + 29,
	ACCUM_APR = ACCUM_MAR + 31,
	ACCUM_MAY = ACCUM_APR + 30,
	ACCUM_JUN = ACCUM_MAY + 31,
	ACCUM_JUL = ACCUM_JUN + 30,
	ACCUM_AUG = ACCUM_JUL + 31,
	ACCUM_SEP = ACCUM_AUG + 31,
	ACCUM_OCT = ACCUM_SEP + 30,
	ACCUM_NOV = ACCUM_OCT + 31,
	ACCUM_DEC = ACCUM_NOV + 30,
};

/** Number of days to pass from the first day in the year before reaching the first of a month. */
static const uint16_t _accum_days_for_month[] = {
	ACCUM_JAN, ACCUM_FEB, ACCUM_MAR, ACCUM_APR,
	ACCUM_MAY, ACCUM_JUN, ACCUM_JUL, ACCUM_AUG,
	ACCUM_SEP, ACCUM_OCT, ACCUM_NOV, ACCUM_DEC,
};

/**
 * Converts a Date to a Year, Month & Day.
 * @param date the date to convert from
 * @param ymd  the year, month and day to write to
 */
CalTime::YearMonthDay CalTime::ConvertDateToYMD(CalTime::Date date)
{
	/* Year determination in multiple steps to account for leap
	 * years. First do the large steps, then the smaller ones.
	 */

	/* There are 97 leap years in 400 years */
	CalTime::Year yr = 400 * (date.base() / (DAYS_IN_YEAR * 400 + 97));
	int rem = date.base() % (DAYS_IN_YEAR * 400 + 97);
	uint16_t x;

	if (rem >= DAYS_IN_YEAR * 100 + 25) {
		/* There are 25 leap years in the first 100 years after
		 * every 400th year, as every 400th year is a leap year */
		yr  += 100;
		rem -= DAYS_IN_YEAR * 100 + 25;

		/* There are 24 leap years in the next couple of 100 years */
		yr += 100 * (rem / (DAYS_IN_YEAR * 100 + 24));
		rem = (rem % (DAYS_IN_YEAR * 100 + 24));
	}

	if (!CalTime::IsLeapYear(yr) && rem >= DAYS_IN_YEAR * 4) {
		/* The first 4 year of the century are not always a leap year */
		yr  += 4;
		rem -= DAYS_IN_YEAR * 4;
	}

	/* There is 1 leap year every 4 years */
	yr += 4 * (rem / (DAYS_IN_YEAR * 4 + 1));
	rem = rem % (DAYS_IN_YEAR * 4 + 1);

	/* The last (max 3) years to account for; the first one
	 * can be, but is not necessarily a leap year */
	while (rem >= (CalTime::IsLeapYear(yr) ? DAYS_IN_LEAP_YEAR : DAYS_IN_YEAR)) {
		rem -= CalTime::IsLeapYear(yr) ? DAYS_IN_LEAP_YEAR : DAYS_IN_YEAR;
		yr++;
	}

	/* Skip the 29th of February in non-leap years */
	if (!CalTime::IsLeapYear(yr) && rem >= ACCUM_MAR - 1) rem++;

	CalTime::YearMonthDay ymd;
	ymd.year = yr;

	x = _month_date_from_year_day[rem];
	ymd.month = x >> 5;
	ymd.day = x & 0x1F;

	return ymd;
}

/**
 * Converts a tuple of Year, Month and Day to a Date.
 * @param year  is a number between 0..MAX_YEAR
 * @param month is a number between 0..11
 * @param day   is a number between 1..31
 */
CalTime::Date CalTime::ConvertYMDToDate(CalTime::Year year, CalTime::Month month, CalTime::Day day)
{
	/* Day-offset in a leap year */
	int days = _accum_days_for_month[month] + day - 1;

	/* Account for the missing of the 29th of February in non-leap years */
	if (!IsLeapYear(year) && days >= ACCUM_MAR) days--;

	return CalTime::DateAtStartOfYear(year) + days;
}

EconTime::YearMonthDay EconTime::ConvertDateToYMD(EconTime::Date date)
{
	if (EconTime::UsingWallclockUnits()) {
		/* If we're using wallclock units, economy months have 30 days and an economy year has 360 days. */
		EconTime::YearMonthDay ymd;
		ymd.year = date.base() / EconTime::DAYS_IN_ECONOMY_WALLCLOCK_YEAR;
		ymd.month = (date.base() % EconTime::DAYS_IN_ECONOMY_WALLCLOCK_YEAR) / EconTime::DAYS_IN_ECONOMY_WALLCLOCK_MONTH;
		ymd.day = (date.base() % EconTime::DAYS_IN_ECONOMY_WALLCLOCK_MONTH) + 1;
		return ymd;
	}

	/* Process the same as calendar time */
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(date.base());
	return { ymd.year.base(), ymd.month, ymd.day };
}

EconTime::Date EconTime::ConvertYMDToDate(EconTime::Year year, EconTime::Month month, EconTime::Day day)
{
	if (EconTime::UsingWallclockUnits()) {
		/* If we're using wallclock units, economy months have 30 days and an economy year has 360 days. */
		const int total_months = (year.base() * MONTHS_IN_YEAR) + month;
		return (total_months * EconTime::DAYS_IN_ECONOMY_WALLCLOCK_MONTH) + day - 1; // Day is 1-indexed but Date is 0-indexed, hence the - 1.
	}

	/* Process the same as calendar time */
	return CalTime::ConvertYMDToDate(year.base(), month, day).base();
}

bool CalTime::IsCalendarFrozen(bool newgame)
{
	GameSettings &settings = (newgame) ? _settings_newgame : _settings_game;
	return settings.economy.timekeeping_units == TKU_WALLCLOCK && settings.economy.minutes_per_calendar_year == CalTime::FROZEN_MINUTES_PER_YEAR;
}

CalTime::Day CalTime::NumberOfDaysInMonth(Year year, Month month)
{
	switch (month) {
		case  0: return 31;
		case  1: return CalTime::IsLeapYear(year) ? 29 : 28;
		case  2: return 31;
		case  3: return 30;
		case  4: return 31;
		case  5: return 30;
		case  6: return 31;
		case  7: return 31;
		case  8: return 30;
		case  9: return 31;
		case 10: return 30;
		case 11: return 31;
		default: NOT_REACHED();
	}
}

bool EconTime::UsingWallclockUnits(bool newgame)
{
	if (newgame) return (_settings_newgame.economy.timekeeping_units == TKU_WALLCLOCK);

	return (_settings_game.economy.timekeeping_units == TKU_WALLCLOCK);
}

/** Functions used by the IncreaseDate function */

extern void EnginesDailyLoop();
extern void DisasterDailyLoop();
extern void IndustryDailyLoop();

extern void CompaniesCalendarMonthlyLoop();
extern void CompaniesEconomyMonthlyLoop();
extern void EnginesMonthlyLoop();
extern void TownsMonthlyLoop();
extern void IndustryMonthlyLoop();
extern void StationDailyLoop();
extern void StationMonthlyLoop();
extern void SubsidyMonthlyLoop();

extern void CompaniesYearlyLoop();
extern void VehiclesYearlyLoop();
extern void TownsYearlyLoop();

extern void ShowEndGameChart();

/**
 * Runs various procedures that have to be done yearly
 */
static void OnNewCalendarYear()
{
	InvalidateWindowClassesData(WC_BUILD_STATION);
	InvalidateWindowClassesData(WC_BUS_STATION);
	InvalidateWindowClassesData(WC_TRUCK_STATION);
	if (_network_server) NetworkServerCalendarYearlyLoop();

	if (CalTime::CurYear() == _settings_client.gui.semaphore_build_before) ResetSignalVariant();

	/* check if we reached end of the game (end of ending year); 0 = never */
	if (CalTime::CurYear() == _settings_game.game_creation.ending_year + 1 && _settings_game.game_creation.ending_year != 0) {
		ShowEndGameChart();
	}

	/* check if we reached the maximum year, decrement dates by a year */
	if (CalTime::CurYear() == CalTime::MAX_YEAR + 1) {
		CalTime::Detail::now.cal_ymd.year--;
		int days_this_year = CalTime::IsLeapYear(CalTime::Detail::now.cal_ymd.year) ? DAYS_IN_LEAP_YEAR : DAYS_IN_YEAR;
		CalTime::Detail::now.cal_date -= days_this_year;
	}

	if (_settings_client.gui.auto_euro) CheckSwitchToEuro();
	IConsoleCmdExec("exec scripts/on_newyear.scr 0");
}

/**
 * Runs various procedures that have to be done yearly
 */
static void OnNewEconomyYear()
{
	EconTime::Detail::years_elapsed++;
	CompaniesYearlyLoop();
	VehiclesYearlyLoop();
	TownsYearlyLoop();
	if (_network_server) NetworkServerEconomyYearlyLoop();

	/* check if we reached the maximum year, decrement dates by a year */
	if (EconTime::CurYear() == EconTime::MAX_YEAR + 1) {
		EconTime::Detail::period_display_offset++;
		EconTime::Detail::now.econ_ymd.year--;
		int days_this_year = EconTime::IsLeapYear(EconTime::Detail::now.econ_ymd.year) ? DAYS_IN_LEAP_YEAR : DAYS_IN_YEAR;
		EconTime::Detail::now.econ_date -= days_this_year;
		LinkGraphSchedule::instance.ShiftDates(-days_this_year);
		UpdateOrderUIOnDateChange();
		ShiftVehicleDates(-days_this_year);
		RecalculateStateTicksOffset();
	}

	CheckStateTicksWrap();
}

/**
 * Runs various procedures that have to be done monthly
 */
static void OnNewCalendarMonth()
{
	SetWindowClassesDirty(WC_CHEATS);
	CompaniesCalendarMonthlyLoop();
	EnginesMonthlyLoop();
	IConsoleCmdExec("exec scripts/on_newmonth.scr 0");
}

/**
 * Runs various procedures that have to be done monthly
 */
static void OnNewEconomyMonth()
{
	CompaniesEconomyMonthlyLoop();
	TownsMonthlyLoop();
	IndustryMonthlyLoop();
	SubsidyMonthlyLoop();
	StationMonthlyLoop();
	if (_network_server) NetworkServerEconomyMonthlyLoop();
}

/**
 * Runs various procedures that have to be done daily
 */
static void OnNewCalendarDay()
{
	EnginesDailyLoop();

	if (!_settings_time.time_in_minutes || _settings_client.gui.date_with_time > 0) {
		SetWindowWidgetDirty(WC_STATUS_BAR, 0, WID_S_LEFT);
	}
	/* Refresh after possible snowline change */
	SetWindowClassesDirty(WC_TOWN_VIEW);
	IConsoleCmdExec("exec scripts/on_newday.scr 0");
}

/**
 * Runs various procedures that have to be done daily
 */
static void OnNewEconomyDay()
{
	if (_network_server) NetworkServerEconomyDailyLoop();

	DisasterDailyLoop();
	IndustryDailyLoop();
	StationDailyLoop();

	ClearOutOfDateSignalSpeedRestrictions();

	if (_quit_after_days > 0) {
		if (--_quit_after_days == 0) {
			DEBUG(misc, 0, "Quitting as day limit reached");
			_exit_game = true;
		}
	}
}

void IncreaseCalendarDate()
{
	/* If calendar day progress is frozen, don't try to advance time. */
	if (CalTime::IsCalendarFrozen()) return;

	/* If we are using a non-default calendar progression speed, we need to check the sub_date_fract before updating date_fract. */
	if (_settings_game.economy.timekeeping_units == TKU_WALLCLOCK && _settings_game.economy.minutes_per_calendar_year != CalTime::DEF_MINUTES_PER_YEAR) {
		CalTime::Detail::now.sub_date_fract += DAY_TICKS;

		/* Check if we are ready to increment date_fract */
		const uint16_t threshold = TicksPerCalendarDay();
		if (CalTime::Detail::now.sub_date_fract < threshold) return;

		CalTime::Detail::now.sub_date_fract = std::min<uint16_t>(CalTime::Detail::now.sub_date_fract - threshold, DAY_TICKS - 1);
	}

	CalTime::Detail::now.cal_date_fract++;
	if (CalTime::Detail::now.cal_date_fract < DAY_TICKS) return;
	CalTime::Detail::now.cal_date_fract = 0;
	CalTime::Detail::now.sub_date_fract = 0;

	/* increase day counter */
	CalTime::Detail::now.cal_date++;

	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(CalTime::Detail::now.cal_date);

	/* check if we entered a new month? */
	bool new_month = ymd.month != CalTime::Detail::now.cal_ymd.month;

	/* check if we entered a new year? */
	bool new_year = ymd.year != CalTime::Detail::now.cal_ymd.year;

	/* update internal variables before calling the daily/monthly/yearly loops */
	CalTime::Detail::now.cal_ymd = ymd;

	UpdateCachedSnowLine();

	/* yes, call various daily loops */
	OnNewCalendarDay();

	/* yes, call various monthly loops */
	if (new_month) OnNewCalendarMonth();

	/* yes, call various yearly loops */
	if (new_year) OnNewCalendarYear();
}

static void IncreaseEconomyDate()
{
	EconTime::Detail::now.econ_date_fract++;
	if (EconTime::Detail::now.econ_date_fract < DAY_TICKS) return;
	EconTime::Detail::now.econ_date_fract = 0;

	/* increase day counter */
	EconTime::Detail::now.econ_date++;

	EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(EconTime::Detail::now.econ_date);

	/* check if we entered a new month? */
	bool new_month = ymd.month != EconTime::Detail::now.econ_ymd.month;

	/* check if we entered a new year? */
	bool new_year = ymd.year != EconTime::Detail::now.econ_ymd.year;

	/* update internal variables before calling the daily/monthly/yearly loops */
	EconTime::Detail::now.econ_ymd = ymd;

	/* yes, call various daily loops */
	OnNewEconomyDay();

	/* yes, call various monthly loops */
	if (new_month) OnNewEconomyMonth();

	/* yes, call various yearly loops */
	if (new_year) OnNewEconomyYear();
}

/**
 * Increases the tick counter, increases date  and possibly calls
 * procedures that have to be called daily, monthly or yearly.
 */
void IncreaseDate()
{
	/* increase day, and check if a new day is there? */
	_tick_counter++;

	if (_game_mode == GM_MENU || _game_mode == GM_BOOTSTRAP) return;

	IncreaseCalendarDate();
	IncreaseEconomyDate();
}

const char *debug_date_dumper::HexDate(EconTime::Date date, EconTime::DateFract date_fract, uint8_t tick_skip_counter)
{
	seprintf(this->buffer, lastof(this->buffer), "date{%08x; %02x; %02x}", date.base(), date_fract, tick_skip_counter);
	return this->buffer;
}
