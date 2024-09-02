/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.h TicToc functionality for debugging. */

#ifndef DEBUG_TICTOC_H
#define DEBUG_TICTOC_H

#include "debug.h"
#include <chrono>
#include <string>

/** TicToc profiling.
 * Usage:
 * static TicToc::State state("A name", 1);
 * TicToc tt(state);
 * --Do your code--
 */
struct TicToc {
	/** Persistent state for TicToc profiling. */
	struct State {
		const std::string_view name;
		const uint32_t max_count;
		uint32_t count = 0;
		uint64_t chrono_sum = 0;

		constexpr State(std::string_view name, uint32_t max_count) : name(name), max_count(max_count) { }
	};

	State &state;
	std::chrono::high_resolution_clock::time_point chrono_start; ///< real time count.

	inline TicToc(State &state) : state(state), chrono_start(std::chrono::high_resolution_clock::now()) { }

	inline ~TicToc()
	{
		this->state.chrono_sum += (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - this->chrono_start)).count();
		if (++this->state.count == this->state.max_count) {
			this->PrintAndReset();
		}
	}

private:
	void PrintAndReset();
};

#endif /* DEBUG_TICTOC_H */
