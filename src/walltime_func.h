/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

 /** @file walltime_func.h Functionality related to the time of the clock on your wall. */

#ifndef WALLTIME_FUNC_H
#define WALLTIME_FUNC_H

#include <ctime>

/** Helper for safely converting a std::time_t to a local time std::tm using localtime_s. */
struct LocalTimeToStruct {
	static inline std::tm ToTimeStruct(std::time_t time_since_epoch)
	{
		std::tm time = {};
#ifdef _WIN32
		/* Windows has swapped the parameters around for localtime_s. */
		localtime_s(&time, &time_since_epoch);
#else
		localtime_r(&time_since_epoch, &time);
#endif
		return time;
	}
};

/** Helper for safely converting a std::time_t to a UTC time std::tm. */
struct UTCTimeToStruct {
	static std::tm ToTimeStruct(std::time_t time_since_epoch);
};

/**
 * Container for wall clock time related functionality not directly provided by C++.
 * @tparam T The type of the time-to-struct implementation class.
 */
template <typename T>
struct Time {
	class FmtTime {
		friend Time<T>;

		std::time_t time_since_epoch;
		const char *format;

		FmtTime(std::time_t time_since_epoch, const char *format) : time_since_epoch(time_since_epoch), format(format) {}

	public:
		void fmt_format_value(struct format_target &output) const;
	};

	/**
	 * Format the given time stamp with the given strftime format specifiers.
	 * @param buffer The buffer to write the time string to.
	 * @param last   The last element in the buffer.
	 * @param time_since_epoch Time since epoch.
	 * @param format The format according to strftime format specifiers.
	 * @return The number of characters that were written to the buffer.
	 */
	static size_t Format(char *buffer, const char *last, std::time_t time_since_epoch, const char *format) NOACCESS(2) WARN_TIME_FORMAT(4);

	/**
	 * Format the given time stamp with the given strftime format specifiers.
	 * @param buffer The buffer to write the time string to.
	 * @param time_since_epoch Time since epoch.
	 * @param format The format according to strftime format specifiers.
	 */
	static void FormatTo(struct format_target &buffer, std::time_t time_since_epoch, const char *format) WARN_TIME_FORMAT(3);

	/**
	 * Create a format argument of the given time stamp with the given strftime format specifiers.
	 * @param time_since_epoch Time since epoch.
	 * @param format The format according to strftime format specifiers.
	 */
	static FmtTime FormatArg(std::time_t time_since_epoch, const char *format) WARN_TIME_FORMAT(2)
	{
		return FmtTime{time_since_epoch, format};
	}

	/**
	 * Format the current time with the given strftime format specifiers.
	 * @param buffer The buffer to write the time string to.
	 * @param last   The last element in the buffer.
	 * @param format The format according to strftime format specifiers.
	 * @return The number of characters that were written to the buffer.
	 */
	static size_t Format(char *buffer, const char *last, const char *format) NOACCESS(2) WARN_TIME_FORMAT(3);

	/**
	 * Format the current time with the given strftime format specifiers.
	 * @param buffer The buffer to write the time string to.
	 * @param time_since_epoch Time since epoch.
	 * @param format The format according to strftime format specifiers.
	 */
	static void FormatTo(struct format_target &buffer, const char *format) WARN_TIME_FORMAT(2);

	/**
	 * Create a format argument of the current time with the given strftime format specifiers.
	 * @param format The format according to strftime format specifiers.
	 */
	static FmtTime FormatArg(const char *format) WARN_TIME_FORMAT(1);
};

/** Wall clock time functionality using the local time zone. */
using LocalTime = Time<LocalTimeToStruct>;
/** Wall clock time functionality using the UTC time zone. */
using UTCTime = Time<UTCTimeToStruct>;

#endif
