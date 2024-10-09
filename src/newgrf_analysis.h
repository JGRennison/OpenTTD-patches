/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_analysis.h NewGRF analysis. */

#ifndef NEWGRF_ANALYSIS_H
#define NEWGRF_ANALYSIS_H

#include "newgrf_commons.h"

#include "3rdparty/robin_hood/robin_hood.h"

struct SpriteGroup;

enum AnalyseCallbackOperationMode : uint8_t {
	ACOM_CB_VAR,
	ACOM_CB36_PROP,
	ACOM_FIND_CB_RESULT,
	ACOM_CB36_SPEED,
	ACOM_INDUSTRY_TILE,
	ACOM_CB_REFIT_CAPACITY,
	ACOM_FIND_RANDOM_TRIGGER,
};

struct AnalyseCallbackOperationIndustryTileData;

enum AnalyseCallbackOperationResultFlags : uint8_t {
	ACORF_NONE                              = 0,
	ACORF_CB_RESULT_FOUND                   = 1 << 0,
	ACORF_CB_REFIT_CAP_NON_WHITELIST_FOUND  = 1 << 1,
	ACORF_CB_REFIT_CAP_SEEN_VAR_47          = 1 << 2,
};
DECLARE_ENUM_AS_BIT_SET(AnalyseCallbackOperationResultFlags)

struct AnalyseCallbackOperation {
	struct FindCBResultData {
		uint16_t callback;
		bool check_var_10;
		uint8_t var_10_value;
	};

	robin_hood::unordered_flat_set<const SpriteGroup *> seen;
	AnalyseCallbackOperationMode mode;
	SpriteGroupCallbacksUsed callbacks_used = SGCU_NONE;
	AnalyseCallbackOperationResultFlags result_flags = ACORF_NONE;
	uint64_t properties_used = 0;
	union {
		FindCBResultData cb_result;
		AnalyseCallbackOperationIndustryTileData *indtile;
	} data;

	AnalyseCallbackOperation(AnalyseCallbackOperationMode mode) :
		mode(mode) {}
};

#endif /* NEWGRF_ANALYSIS_H */
