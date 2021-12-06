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
	DCBF_VEH_TICK_CACHE                = 0,
	DCBF_MP_NO_STATE_CSUM_CHECK        = 1,
	DCBF_DESYNC_CHECK_PERIODIC         = 2,
	DCBF_DESYNC_CHECK_POST_COMMAND     = 3,
	DCBF_DESYNC_CHECK_NO_GENERAL       = 4,
	DCBF_DESYNC_CHECK_PERIODIC_SIGNALS = 5,
};

inline bool HasChickenBit(ChickenBitFlags flag)
{
	return HasBit(_settings_game.debug.chicken_bits, flag);
}

enum MiscDebugFlags {
	MDF_OVERHEAT_BREAKDOWN_OPEN_WIN,
	MDF_ZONING_RS_WATER_FLOOD_STATE,
};
extern uint32 _misc_debug_flags;

#endif /* DEBUG_SETTINGS_H */
