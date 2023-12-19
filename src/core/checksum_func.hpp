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

#ifdef RANDOM_DEBUG
#include "../network/network.h"
#include "../network/network_server.h"
#include "../network/network_internal.h"
#include "../company_func.h"
#include "../fileio_func.h"
#include "../date_func.h"
#include "../debug.h"
#endif /* RANDOM_DEBUG */

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
	if (_networking) _state_checksum.Update(input);
}

#ifdef RANDOM_DEBUG
inline bool ShouldLogUpdateStateChecksum()
{
	return _networking && (!_network_server || (NetworkClientSocket::IsValidID(0) && NetworkClientSocket::Get(0)->status != NetworkClientSocket::STATUS_INACTIVE));
}
#	define DEBUG_UPDATESTATECHECKSUM(str, ...) if (ShouldLogUpdateStateChecksum()) DEBUG(statecsum, 0, "date{%08x; %02x; %02x}; %04x; %02x; " OTTD_PRINTFHEX64PAD "; %s:%d " str, \
		_date.base(), _date_fract, _tick_skip_counter, _frame_counter, (byte)_current_company, _state_checksum.state, __FILE__, __LINE__, __VA_ARGS__);
#else
#	define DEBUG_UPDATESTATECHECKSUM(str, ...)
#endif /* RANDOM_DEBUG */

#endif /* CHECKSUM_FUNC_HPP */
