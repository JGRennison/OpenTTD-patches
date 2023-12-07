/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_industrytiles_analysis.h NewGRF handling of industry tiles: analysis. */

#ifndef NEWGRF_INDUSTRYTILES_ANALYSIS_H
#define NEWGRF_INDUSTRYTILES_ANALYSIS_H

#include "industrytype.h"

struct AnalyseCallbackOperationIndustryTileData {
	const IndustryTileLayout *layout;
	uint64 check_mask;
	uint64 *result_mask;
	uint8 layout_index;
	bool anim_state_at_offset;
	bool check_anim_next_frame_cb;
};

#endif /* NEWGRF_INDUSTRYTILES_ANALYSIS_H */
