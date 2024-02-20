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

using Ticks = int32_t;                    ///< The type to store ticks in
static constexpr Ticks INVALID_TICKS = -1; ///< Representation of an invalid number of ticks

using ScaledTickCounter = uint64_t;       ///< The type for the scaled tick counter

using YearDelta = StrongType::Typedef<int32_t, struct YearDeltaTag, StrongType::Compare, StrongType::IntegerScalable>;
using DateDelta = StrongType::Typedef<int32_t, struct DateDeltaTag, StrongType::Compare, StrongType::IntegerScalable>;
using DateTicksDelta = StrongType::Typedef<int64_t, struct DateTicksDeltaTag, StrongType::Compare, StrongType::IntegerScalable>;

namespace DateDetail {
	/* Mixin for DateTicks */
	template <typename TDate, typename TDateFract>
	struct DateTicksOperations {
		template <typename TType, typename TBaseType>
		struct mixin {
		private:
			TBaseType GetBase() const { return static_cast<const TType &>(*this).base(); }

		public:
			TDate ToDate() const { return this->GetBase() / DAY_TICKS; }
			TDateFract ToDateFractRemainder() const { return this->GetBase() % DAY_TICKS; }
		};
	};

	template <typename T>
	struct BaseTime {
		/* The type to store our dates in */
		template <class ST> struct DateDeltaTag;

		template <class ST> struct DateTag;
		using Date = StrongType::Typedef<int32_t, struct DateTag<T>, StrongType::Compare, StrongType::IntegerDelta<DateDelta>>;

		using DateFract = uint16_t; ///< The fraction of a date we're in, i.e. the number of ticks since the last date changeover

		/* The type to store dates in when tick-precision is required */
		template <class ST> struct DateTicksTag;
		using DateTicks = StrongType::Typedef<int64_t, struct DateTicksTag<T>, StrongType::Compare, StrongType::IntegerDelta<DateTicksDelta>, DateTicksOperations<Date, DateFract>>;

		static constexpr DateTicks DateToDateTicks(Date date, DateFract fract = 0)
		{
			return ((int64_t)date.base() * DAY_TICKS) + fract;
		}

		/* Year type */
		template <class ST> struct YearTag;
		using Year = StrongType::Typedef<int32_t, struct YearTag<T>, StrongType::Compare, StrongType::IntegerDelta<YearDelta>>;

		using Month = uint8_t;     ///< Type for the month, note: 0 based, i.e. 0 = January, 11 = December.
		using Day = uint8_t;       ///< Type for the day of the month, note: 1 based, first day of a month is 1.

		/**
		 * Data structure to convert between Date and triplet (year, month, and day).
		 * @see ConvertDateToYMD(), ConvertYMDToDate()
		 */
		struct YearMonthDay {
			Year  year;   ///< Year (0...)
			Month month;  ///< Month (0..11)
			Day   day;    ///< Day (1..31)
		};

		struct Detail {
			/**
			 * Calculate the date of the first day of a given year.
			 * @param year the year to get the first day of.
			 * @return the date.
			 */
			static constexpr Date DateAtStartOfCalendarYear(Year year)
			{
				int32_t year_as_int = year.base();
				uint number_of_leap_years = (year == 0) ? 0 : ((year_as_int - 1) / 4 - (year_as_int - 1) / 100 + (year_as_int - 1) / 400 + 1);

				/* Hardcode the number of days in a year because we can't access CalendarTime from here. */
				return (365 * year_as_int) + number_of_leap_years;
			}
		};

		/**
		 * Checks whether the given year is a leap year or not.
		 * @param year The year to check.
		 * @return True if \c year is a leap year, otherwise false.
		 */
		static constexpr bool IsLeapYear(Year year)
		{
			int32_t year_as_int = year.base();
			return year_as_int % 4 == 0 && (year_as_int % 100 != 0 || year_as_int % 400 == 0);
		}

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
		 * The offset in days from the '_date == 0' till
		 * 'ConvertYMDToDate(ORIGINAL_BASE_YEAR, 0, 1)'
		 */
		static constexpr Date DAYS_TILL_ORIGINAL_BASE_YEAR = Detail::DateAtStartOfCalendarYear(ORIGINAL_BASE_YEAR);

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
		static constexpr Year MAX_YEAR  = 5000000;

		/** The number of days till the last day */
		static constexpr Date MAX_DATE = Detail::DateAtStartOfCalendarYear(MAX_YEAR + 1) - 1;

		static constexpr Year       INVALID_YEAR        = -1; ///< Representation of an invalid year
		static constexpr Date       INVALID_DATE        = -1; ///< Representation of an invalid date
		static constexpr DateTicks  INVALID_DATE_TICKS  = -1; ///< Representation of an invalid date ticks
	};
};

struct CalTime : public DateDetail::BaseTime<struct CalendarTimeTag> {
	using ParentBaseTime = DateDetail::BaseTime<struct CalendarTimeTag>;

	/* Use a state struct to make backup/restore/init simpler */
	struct State {
		YearMonthDay cal_ymd;
		Date         cal_date;
		DateFract    cal_date_fract;
		uint16_t     sub_date_fract; ///< Subpart of date_fract that we use when calendar days are slower than economy days.
	};
	/* Use a detail struct/namespace to more easily control writes */
	struct Detail {
		static State now;

		static void SetDate(Date date, DateFract fract);
		static State NewState(Year year);
	};

	static constexpr int DEF_MINUTES_PER_YEAR = 12;
	static constexpr int FROZEN_MINUTES_PER_YEAR = 0;
	static constexpr int MAX_MINUTES_PER_YEAR = 10080; // One week of real time. The actual max that doesn't overflow TimerGameCalendar::sub_date_fract is 10627, but this is neater.

	static inline const YearMonthDay &CurYMD()             { return Detail::now.cal_ymd; }
	static inline Year                CurYear()            { return Detail::now.cal_ymd.year; }
	static inline Month               CurMonth()           { return Detail::now.cal_ymd.month; }
	static inline Day                 CurDay()             { return Detail::now.cal_ymd.day; }
	static inline Date                CurDate()            { return Detail::now.cal_date; }
	static inline DateFract           CurDateFract()       { return Detail::now.cal_date_fract; }
	static inline uint16_t            CurSubDateFract()    { return Detail::now.sub_date_fract; }

	static YearMonthDay ConvertDateToYMD(Date date);
	static Date ConvertYMDToDate(Year year, Month month, Day day);

	static inline Date ConvertYMDToDate(const YearMonthDay &ymd)
	{
		return ConvertYMDToDate(ymd.year, ymd.month, ymd.day);
	}

	static bool IsCalendarFrozen(bool newgame = false);

	static Day NumberOfDaysInMonth(Year year, Month month);

	/**
	 * Calculate the year of a given date.
	 * @param date The date to consider.
	 * @return the year.
	 */
	static constexpr Year DateToYear(Date date)
	{
		return date.base() / DAYS_IN_LEAP_YEAR;
	}

	/**
	 * Calculate the date of the first day of a given year.
	 * @param year the year to get the first day of.
	 * @return the date.
	 */
	static constexpr Date DateAtStartOfYear(Year year)
	{
		return ParentBaseTime::Detail::DateAtStartOfCalendarYear(year);
	}
};

struct EconTime : public DateDetail::BaseTime<struct EconTimeTag> {
	using ParentBaseTime = DateDetail::BaseTime<struct EconTimeTag>;

	/* Use a state struct to make backup/restore/init simpler */
	struct State {
		YearMonthDay econ_ymd;
		Date         econ_date;
		DateFract    econ_date_fract;
	};

	static constexpr int DAYS_IN_ECONOMY_WALLCLOCK_YEAR = 360; ///< Days in an economy year, when in wallclock timekeeping mode.
	static constexpr int DAYS_IN_ECONOMY_WALLCLOCK_MONTH = 30; ///< Days in an economy month, when in wallclock timekeeping mode.

	/* Use a detail struct/namespace to more easily control writes */
	struct Detail {
		static State now;

		static void SetDate(Date date, DateFract fract);
		static State NewState(Year year);

		/**
		 * Calculate the date of the first day of a given year.
		 * @param year the year to get the first day of.
		 * @return the date (when using wallclock 30-day months).
		 */
		static constexpr Date DateAtStartOfWallclockModeYear(Year year)
		{
			return DAYS_IN_ECONOMY_WALLCLOCK_YEAR * year.base();
		}
	};

	/**
	 * The offset in days from the '_date == 0' till
	 * 'ConvertYMDToDate(ORIGINAL_BASE_YEAR, 0, 1)', when using wallclock 30-day months
	 */
	static constexpr Date DAYS_TILL_ORIGINAL_BASE_YEAR_WALLCLOCK_MODE = DAYS_IN_ECONOMY_WALLCLOCK_YEAR * ORIGINAL_BASE_YEAR.base();

	static inline const YearMonthDay &CurYMD()             { return Detail::now.econ_ymd; }
	static inline Year                CurYear()            { return Detail::now.econ_ymd.year; }
	static inline Month               CurMonth()           { return Detail::now.econ_ymd.month; }
	static inline Day                 CurDay()             { return Detail::now.econ_ymd.day; }
	static inline Date                CurDate()            { return Detail::now.econ_date; }
	static inline DateFract           CurDateFract()       { return Detail::now.econ_date_fract; }
	static inline DateTicks           CurDateTicks()       { return DateToDateTicks(CurDate(), CurDateFract()); }

	static YearMonthDay ConvertDateToYMD(Date date);
	static Date ConvertYMDToDate(Year year, Month month, Day day);

	static inline Date ConvertYMDToDate(const YearMonthDay &ymd)
	{
		return ConvertYMDToDate(ymd.year, ymd.month, ymd.day);
	}

	static bool UsingWallclockUnits(bool newgame = false);

	/**
	 * Calculate the date of the first day of a given year.
	 * @param year the year to get the first day of.
	 * @return the date.
	 */
	static inline Date DateAtStartOfYear(Year year)
	{
		if (UsingWallclockUnits()) return Detail::DateAtStartOfWallclockModeYear(year);
		return ParentBaseTime::Detail::DateAtStartOfCalendarYear(year);
	}
};

namespace DateDetail {
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
};

/* The type to store state ticks (this always ticks at the same rate regardless of day length, even in the scenario editor */
using StateTicksDelta = StrongType::Typedef<int64_t, struct StateTicksDeltaTag, StrongType::Compare, StrongType::IntegerScalable, DateDetail::StateTicksDeltaOperations>;
using StateTicks = StrongType::Typedef<int64_t, struct StateTicksTag, StrongType::Compare, StrongType::IntegerDelta<StateTicksDelta>>;

namespace DateDetail {
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
};

/* The type to store general clock-face minutes in (i.e. 0..1440) */
using ClockFaceMinutes = StrongType::Typedef<int, struct ClockFaceMinutesTag, StrongType::Compare, StrongType::Integer, DateDetail::MinuteOperations<false>, DateDetail::ClockFaceMinuteOperations>;

namespace DateDetail {
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
};

/* The type to store StateTicks-based minutes in */
using TickMinutes = StrongType::Typedef<int64_t, struct TickMinutesTag, StrongType::Compare, StrongType::Integer, DateDetail::MinuteOperations<true>, DateDetail::TickMinuteOperations>;

static const int STATION_RATING_TICKS     = 185; ///< cycle duration for updating station rating
static const int STATION_ACCEPTANCE_TICKS = 250; ///< cycle duration for updating station acceptance
static const int STATION_LINKGRAPH_TICKS  = 504; ///< cycle duration for cleaning dead links
static const int CARGO_AGING_TICKS        = 185; ///< cycle duration for aging cargo
static const int INDUSTRY_PRODUCE_TICKS   = 256; ///< cycle duration for industry production
static const int TOWN_GROWTH_TICKS        = 70;  ///< cycle duration for towns trying to grow. (this originates from the size of the town array in TTD
static const int INDUSTRY_CUT_TREE_TICKS  = INDUSTRY_PRODUCE_TICKS * 2; ///< cycle duration for lumber mill's extra action

/** An initial value for StateTicks when starting a new game */
static constexpr StateTicks INITIAL_STATE_TICKS_VALUE = 1 << 24;

/** Invalid state ticks value */
static constexpr StateTicks INVALID_STATE_TICKS = INT64_MIN;

#endif /* DATE_TYPE_H */
