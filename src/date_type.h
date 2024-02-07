/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file date_type.h Types related to the dates in OpenTTD. */

#ifndef DATE_TYPE_H
#define DATE_TYPE_H

#include "core/strong_typedef_type.hpp"
#include "core/math_func.hpp"

/**
 * 1 day is 74 ticks; _date_fract used to be uint16_t and incremented by 885. On
 *                    an overflow the new day begun and 65535 / 885 = 74.
 * 1 tick is approximately 27 ms.
 * 1 day is thus about 2 seconds (74 * 27 = 1998) on a machine that can run OpenTTD normally
 */
static const int DAY_TICKS         =  74; ///< ticks per day
static const int DAYS_IN_YEAR      = 365; ///< days per year
static const int DAYS_IN_LEAP_YEAR = 366; ///< sometimes, you need one day more...
static const int MONTHS_IN_YEAR    =  12; ///< months per year

static const int SECONDS_PER_DAY   = 2;   ///< approximate seconds per day, not for precise calculations

typedef uint16_t DateFract; ///< The fraction of a date we're in, i.e. the number of ticks since the last date changeover
typedef int32_t  Ticks;     ///< The type to store ticks in

typedef int32_t  Year;  ///< Type for the year, note: 0 based, i.e. starts at the year 0.
typedef uint8_t  Month; ///< Type for the month, note: 0 based, i.e. 0 = January, 11 = December.
typedef uint8_t  Day;   ///< Type for the day of the month, note: 1 based, first day of a month is 1.

/* The type to store our dates in */
using DateDelta = StrongType::Typedef<int32_t, struct DateDeltaTag, StrongType::Compare, StrongType::IntegerScalable>;
using Date = StrongType::Typedef<int32_t, struct DateTag, StrongType::Compare, StrongType::IntegerDelta<DateDelta>>;

/* Mixin for DateTicks */
struct DateTicksOperations {
	template <typename TType, typename TBaseType>
	struct mixin {
	private:
		TBaseType GetBase() const { return static_cast<const TType &>(*this).base(); }

	public:
		Date ToDate() const { return this->GetBase() / DAY_TICKS; }
		DateFract ToDateFractRemainder() const { return this->GetBase() % DAY_TICKS; }
	};
};

/* The type to store dates in when tick-precision is required */
using DateTicksDelta = StrongType::Typedef<int64_t, struct DateTicksDeltaTag, StrongType::Compare, StrongType::IntegerScalable>;
using DateTicks = StrongType::Typedef<int64_t, struct DateTicksTag, StrongType::Compare, StrongType::IntegerDelta<DateTicksDelta>, DateTicksOperations>;

/* Mixin for StateTicksDelta */
struct StateTicksDeltaOperations {
	template <typename TType, typename TBaseType>
	struct mixin {
	private:
		TBaseType GetBase() const { return static_cast<const TType &>(*this).base(); }

	public:
		template<typename T>
		T AsTicksT() const { return ClampTo<T>(this->GetBase()); }

		Ticks AsTicks() const { return this->AsTicksT<Ticks>(); }
	};
};

/* The type to store state ticks (this always ticks at the same rate regardless of day length, even in the scenario editor */
using StateTicksDelta = StrongType::Typedef<int64_t, struct StateTicksDeltaTag, StrongType::Compare, StrongType::IntegerScalable, StateTicksDeltaOperations>;
using StateTicks = StrongType::Typedef<int64_t, struct StateTicksTag, StrongType::Compare, StrongType::IntegerDelta<StateTicksDelta>>;

/* Mixin for TickMinutes, ClockFaceMinutes */
template <bool TNegativeCheck>
struct MinuteOperations {
	template <typename TType, typename TBaseType>
	struct mixin {
	private:
		TBaseType GetBase() const
		{
			TBaseType value = static_cast<const TType &>(*this).base();
			if constexpr (TNegativeCheck) {
				if (value < 0) {
					value = (value % 1440) + 1440;
				}
			}
			return value;
		}

	public:
		int ClockMinute() const { return this->GetBase() % 60; }
		int ClockHour() const { return (this->GetBase() / 60) % 24; }
		int ClockHHMM() const { return (this->ClockHour() * 100) + this->ClockMinute(); }
	};
};

/* Mixin for ClockFaceMinutes */
struct ClockFaceMinuteOperations {
	template <typename TType, typename TBaseType>
	struct mixin {
		static constexpr TType FromClockFace(int hours, int minutes)
		{
			return (TBaseType(hours) * 60) + minutes;
		}
	};
};

/* The type to store general clock-face minutes in (i.e. 0..1440) */
using ClockFaceMinutes = StrongType::Typedef<int, struct ClockFaceMinutesTag, StrongType::Compare, StrongType::Integer, MinuteOperations<false>, ClockFaceMinuteOperations>;

/* Mixin for TickMinutes */
struct TickMinuteOperations {
	template <typename TType, typename TBaseType>
	struct mixin {
	private:
		TBaseType GetBase() const { return static_cast<const TType &>(*this).base(); }

	public:
		TType ToSameDayClockTime(int hour, int minute) const
		{
			TBaseType day = DivTowardsNegativeInf<TBaseType>(this->GetBase(), 1440);
			return (day * 1440) + (hour * 60) + minute;
		}

		ClockFaceMinutes ToClockFaceMinutes() const
		{
			TBaseType minutes = this->GetBase() % 1440;
			if (minutes < 0) minutes += 1440;
			return minutes;
		}
	};
};

/* The type to store StateTicks-based minutes in */
using TickMinutes = StrongType::Typedef<int64_t, struct TickMinutesTag, StrongType::Compare, StrongType::Integer, MinuteOperations<true>, TickMinuteOperations>;

static const int STATION_RATING_TICKS     = 185; ///< cycle duration for updating station rating
static const int STATION_ACCEPTANCE_TICKS = 250; ///< cycle duration for updating station acceptance
static const int STATION_LINKGRAPH_TICKS  = 504; ///< cycle duration for cleaning dead links
static const int CARGO_AGING_TICKS        = 185; ///< cycle duration for aging cargo
static const int INDUSTRY_PRODUCE_TICKS   = 256; ///< cycle duration for industry production
static const int TOWN_GROWTH_TICKS        = 70;  ///< cycle duration for towns trying to grow. (this originates from the size of the town array in TTD
static const int INDUSTRY_CUT_TREE_TICKS  = INDUSTRY_PRODUCE_TICKS * 2; ///< cycle duration for lumber mill's extra action

/*
 * ORIGINAL_BASE_YEAR, ORIGINAL_MAX_YEAR and DAYS_TILL_ORIGINAL_BASE_YEAR are
 * primarily used for loading newgrf and savegame data and returning some
 * newgrf (callback) functions that were in the original (TTD) inherited
 * format, where '_date == 0' meant that it was 1920-01-01.
 */

/** The minimum starting year/base year of the original TTD */
static constexpr Year ORIGINAL_BASE_YEAR = 1920;
/** The original ending year */
static constexpr Year ORIGINAL_END_YEAR  = 2051;
/** The maximum year of the original TTD */
static constexpr Year ORIGINAL_MAX_YEAR  = 2090;

/**
 * Calculate the date of the first day of a given year.
 * @param year the year to get the first day of.
 * @return the date.
 */
static constexpr Date DateAtStartOfYear(Year year)
{
	int32_t year_as_int = year;
	uint number_of_leap_years = (year == 0) ? 0 : ((year_as_int - 1) / 4 - (year_as_int - 1) / 100 + (year_as_int - 1) / 400 + 1);

	/* Hardcode the number of days in a year because we can't access CalendarTime from here. */
	return (365 * year_as_int) + number_of_leap_years;
}

/**
 * The offset in days from the '_date == 0' till
 * 'ConvertYMDToDate(ORIGINAL_BASE_YEAR, 0, 1)'
 */
static constexpr Date DAYS_TILL_ORIGINAL_BASE_YEAR = DateAtStartOfYear(ORIGINAL_BASE_YEAR);

static constexpr Date MIN_DATE = 0;

/** The absolute minimum & maximum years in OTTD */
static constexpr Year MIN_YEAR = 0;

/** The default starting year */
static constexpr Year DEF_START_YEAR = 1950;
/** The default scoring end year */
static constexpr Year DEF_END_YEAR = ORIGINAL_END_YEAR - 1;

/**
 * MAX_YEAR, nicely rounded value of the number of years that can
 * be encoded in a single 32 bits date, about 2^31 / 366 years.
 */
static const Year MAX_YEAR  = 5000000;

/** The number of days till the last day */
static constexpr Date MAX_DATE = DateAtStartOfYear(MAX_YEAR + 1) - 1;

/** An initial value for StateTicks when starting a new game */
static constexpr StateTicks INITIAL_STATE_TICKS_VALUE = 1 << 24;

/**
 * Data structure to convert between Date and triplet (year, month, and day).
 * @see ConvertDateToYMD(), ConvertYMDToDate()
 */
struct YearMonthDay {
	Year  year;   ///< Year (0...)
	Month month;  ///< Month (0..11)
	Day   day;    ///< Day (1..31)
};

static constexpr Year       INVALID_YEAR        = -1; ///< Representation of an invalid year
static constexpr Date       INVALID_DATE        = -1; ///< Representation of an invalid date
static constexpr DateTicks  INVALID_DATE_TICKS  = -1; ///< Representation of an invalid date ticks
static constexpr Ticks      INVALID_TICKS       = -1; ///< Representation of an invalid number of ticks

#endif /* DATE_TYPE_H */
