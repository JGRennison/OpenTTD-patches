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

enum NewGRFOptimiserFlags {
	NGOF_NO_OPT_VARACT2                 = 0,
	NGOF_NO_OPT_VARACT2_DSE             = 1,
	NGOF_NO_OPT_VARACT2_GROUP_PRUNE     = 2,
	NGOF_NO_OPT_VARACT2_EXPENSIVE_VARS  = 3,
	NGOF_NO_OPT_VARACT2_SIMPLIFY_STORES = 4,
	NGOF_NO_OPT_VARACT2_ADJUST_ORDERING = 5,
	NGOF_NO_OPT_VARACT2_INSERT_JUMPS    = 6,
};

inline bool HasGrfOptimiserFlag(NewGRFOptimiserFlags flag)
{
	return HasBit(_settings_game.debug.newgrf_optimiser_flags, flag);
}

enum MiscDebugFlags {
	MDF_OVERHEAT_BREAKDOWN_OPEN_WIN,
	MDF_ZONING_RS_WATER_FLOOD_STATE,
	MDF_ZONING_RS_TROPIC_ZONE,
	MDF_ZONING_RS_ANIMATED_TILE,
	MDF_NEWGRF_SG_SAVE_RAW,
	MDF_NEWGRF_SG_DUMP_MORE_DETAIL,
};
extern uint32 _misc_debug_flags;

#endif /* DEBUG_SETTINGS_H */
