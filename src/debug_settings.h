/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug_settings.h Debug settings. */

#ifndef DEBUG_SETTINGS_H
#define DEBUG_SETTINGS_H

#include "settings_type.h"
#include "core/bitmath_func.hpp"

enum ChickenBitFlags {
	DCBF_VEH_TICK_CACHE            = 0,
	DCBF_MP_NO_STATE_CSUM_CHECK    = 1,
};

inline bool HasChickenBit(ChickenBitFlags flag)
{
	return HasBit(_settings_game.debug.chicken_bits, flag);
}

#endif /* DEBUG_SETTINGS_H */
