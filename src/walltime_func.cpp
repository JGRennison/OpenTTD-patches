/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file walltime_func.cpp Functionality related to the time of the clock on your wall. */

#include "stdafx.h"
#include "walltime_func.h"
#include "core/format.hpp"

#include <chrono>

#include "safeguards.h"

std::tm UTCTimeToStruct::ToTimeStruct(std::time_t time_since_epoch)
{
	std::tm time = {};

#if (defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L) || (defined(__GNUC__) && (__GNUC__ >= 11))

	const std::chrono::time_point syst = std::chrono::system_clock::from_time_t(time_since_epoch);
	const std::chrono::sys_days sysday = std::chrono::floor<std::chrono::days>(syst);
	const std::chrono::year_month_day ymd{sysday};
	const std::chrono::sys_days first_day_of_year = std::chrono::year_month_day(ymd.year(), std::chrono::January, std::chrono::day(1));
	const std::chrono::weekday weekday{sysday};
	const std::chrono::hh_mm_ss tod{syst - sysday};

	time.tm_sec = tod.seconds().count();
	time.tm_min = tod.minutes().count();
	time.tm_hour = tod.hours().count();
	time.tm_mday = (int)(uint)ymd.day();
	time.tm_mon = (int)(uint)ymd.month() - 1;
	time.tm_year = (int)ymd.year() - 1900;
	time.tm_wday = weekday.c_encoding();
	time.tm_yday = (sysday - first_day_of_year).count();
	time.tm_mday = (int)(uint)ymd.day();

#else /* chrono */

#ifdef _WIN32
	/* Windows has swapped the parameters around for gmtime_s. */
	gmtime_s(&time, &time_since_epoch);
#else
	gmtime_r(&time_since_epoch, &time);
#endif

#endif /* chrono */

	return time;
}

#ifndef _MSC_VER
		/* GCC bug #39438; unlike for printf where the appropriate attribute prevent the
		 * "format non literal" warning, that does not happen for strftime. Even though
		 * format warnings will be created for invalid strftime formats. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif /* _MSC_VER */

template <typename T>
size_t Time<T>::Format(char *buffer, const char *last, std::time_t time_since_epoch, const char *format)
{
	std::tm time_struct = T::ToTimeStruct(time_since_epoch);
	return strftime(buffer, last - buffer + 1, format, &time_struct);
}

template <typename T>
size_t Time<T>::Format(char *buffer, const char *last, const char *format)
{
	return Time<T>::Format(buffer, last, time(nullptr), format);
}

template <typename T>
void Time<T>::FormatTo(format_target &buffer, std::time_t time_since_epoch, const char *format)
{
	buffer.append_ptr_last_func(128, [&](char *buf, const char *last) -> char * {
		return buf + Format(buf, last, time_since_epoch, format);
	});
}

template <typename T>
void Time<T>::FormatTo(format_target &buffer, const char *format)
{
	Time<T>::FormatTo(buffer, time(nullptr), format);
}

template struct Time<LocalTimeToStruct>;
template struct Time<UTCTimeToStruct>;

#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif /* _MSC_VER */
