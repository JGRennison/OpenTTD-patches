/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file time_type.h Wrapper for time values to avoid needing to include <chrono> everywhere
 *
 * <chrono> is very slow to compile (notably it includes all of std::format).
 */

#ifndef TIME_TYPE_H
#define TIME_TYPE_H

#include "core/strong_typedef_type.hpp"

namespace TimeType {
	namespace Detail {
		template<typename T>
		concept Duration = requires { typename T::rep; };

		template<typename T>
		concept TimePoint = requires { typename T::duration; };
	};

	struct MillisecondsTag : public StrongType::TypedefTraits<int64_t, StrongType::Compare, StrongType::Integer> {};
	struct Milliseconds : public StrongType::Typedef<MillisecondsTag> {
		using Parent = StrongType::Typedef<MillisecondsTag>;
		using Parent::Parent;

		template <typename T> requires Detail::Duration<T>
		constexpr Milliseconds(T duration);
	};

	class SteadyTimePoint {
		int64_t ticks;

		constexpr SteadyTimePoint(int64_t ticks) : ticks(ticks) {}

public:
		constexpr int64_t NanosecondsSinceEpoch() const { return this->ticks; }
		int64_t SecondsBeforeNow() const;

		template <typename T> requires Detail::TimePoint<T>
		constexpr SteadyTimePoint(T time_point);

		static SteadyTimePoint Now();
	};
};

uint64_t MicrosecondsRealtimeTicks();
uint64_t NanosecondsRealtimeTicks();

#endif /* TIME_TYPE_H */
