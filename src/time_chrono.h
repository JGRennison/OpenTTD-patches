/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file time_chrono.h Wrapper for time values to avoid needing to include <chrono> everywhere - <chrono> part
 *
 * <chrono> is very slow to compile (notably it includes all of std::format).
 */

#ifndef TIME_CHRONO_H
#define TIME_CHRONO_H

#include "time_type.h"
#include <chrono>

namespace TimeType {

template <typename T> requires Detail::Duration<T>
constexpr Milliseconds::Milliseconds(T duration)
{
	std::chrono::milliseconds d = duration;
	this->edit_base() = d.count();
}

inline std::chrono::steady_clock::time_point FromSteadyTimePoint(SteadyTimePoint pt)
{
	std::chrono::nanoseconds offset{pt.NanosecondsSinceEpoch()};
	return std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(offset));
}

template <typename T> requires Detail::TimePoint<T>
constexpr SteadyTimePoint::SteadyTimePoint(T input_time_point)
{
	using namespace std::chrono;
	steady_clock::time_point tp = input_time_point;
	this->ticks = static_cast<int64_t>(duration_cast<nanoseconds>(tp.time_since_epoch()).count());
}

};

#endif /* TIME_CHRONO_H */
