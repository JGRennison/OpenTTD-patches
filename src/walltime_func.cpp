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

#include "safeguards.h"

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
