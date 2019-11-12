/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file checksum_func.hpp Checksum utility functions. */

#ifndef CHECKSUM_FUNC_HPP
#define CHECKSUM_FUNC_HPP

#include "bitmath_func.hpp"

struct SimpleChecksum64 {
	uint64 state = 0;

	void Update(uint64 input)
	{
		this->state = ROL(this->state, 1) ^ input ^ 0x123456789ABCDEF7ULL;
	}
};

extern SimpleChecksum64 _state_checksum;

inline void UpdateStateChecksum(uint64 input)
{
	_state_checksum.Update(input);
}

#endif /* CHECKSUM_FUNC_HPP */
