/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file extended_ver_sl.cpp Functions related to handling save/load extended version info.
 *
 * Known extended features are stored in _sl_xv_feature_versions, features which are currently enabled/in use and their versions are stored in the savegame.
 * On load, the list of features and their versions are loaded from the savegame. If the savegame contains a feature which is either unknown, or has too high a version,
 * loading can be either aborted, or the feature can be ignored if the the feature flags in the savegame indicate that it can be ignored. The savegame may also list any additional
 * chunk IDs which are associated with an extended feature, these can be discarded if the feature is discarded.
 * This information is stored in the SLXI chunk, the contents of which has the following format:
 *
 * uint32                               chunk version
 * uint32                               chunk flags
 * uint32                               number of sub chunks/features
 *     For each of N sub chunk/feature:
 *     uint32                           feature flags (SlxiSubChunkFlags)
 *     uint16                           feature version
 *     SLE_STR                          feature name
 *     uint32*                          extra data length [only present iff feature flags & XSCF_EXTRA_DATA_PRESENT]
 *         N bytes                      extra data
 *     uint32*                          chunk ID list count [only present iff feature flags & XSCF_CHUNK_ID_LIST_PRESENT]
 *         N x uint32                   chunk ID list
 *
 * Extended features as recorded in the SLXI chunk, above, MAY add, remove, change, or otherwise modify fields in chunks
 * not owned by the feature and therefore not listed in the sub chunk/feature information in the SLXI chunk.
 * In this case the XSCF_IGNORABLE_UNKNOWN flag SHOULD NOT be set, as it is not possible to correctly load the modified chunk without
 * knowledge of the feature.
 * In the case where the modifications to other chunks vary with respect to lower feature versions, the XSCF_IGNORABLE_VERSION flag
 * also SHOULD NOT be set.
 * Use of the XSCF_IGNORABLE_UNKNOWN and XSCF_IGNORABLE_VERSION flags MUST ONLY be used in the cases where the feature and any
 * associated chunks can be cleanly dropped, and the savegame can be correctly loaded by a client with no knowledge of the feature.
 */

#include "../stdafx.h"
#include "../debug.h"
#include "saveload.h"
#include "extended_ver_sl.h"
#include "../timetable.h"
#include "../map_func.h"

#include <vector>

#include "../safeguards.h"

uint16 _sl_xv_feature_versions[XSLFI_SIZE];                 ///< array of all known feature types and their current versions
bool _sl_is_ext_version;                                    ///< is this an extended savegame version, with more info in the SLXI chunk?
bool _sl_is_faked_ext;                                      ///< is this a faked extended savegame version, with no SLXI chunk? See: SlXvCheckSpecialSavegameVersions.
bool _sl_maybe_springpp;                                    ///< is this possibly a SpringPP savegame?
bool _sl_maybe_chillpp;                                     ///< is this possibly a ChillPP v8 savegame?
std::vector<uint32> _sl_xv_discardable_chunk_ids;           ///< list of chunks IDs which we can discard if no chunk loader exists

static const uint32 _sl_xv_slxi_chunk_version = 0;          ///< current version of SLXI chunk

const SlxiSubChunkInfo _sl_xv_sub_chunk_infos[] = {
	{ XSLFI_TRACE_RESTRICT,         XSCF_NULL,               11,  11, "tracerestrict",             nullptr, nullptr, "TRRM,TRRP,TRRS" },
	{ XSLFI_TRACE_RESTRICT_OWNER,   XSCF_NULL,                1,   1, "tracerestrict_owner",       nullptr, nullptr, nullptr        },
	{ XSLFI_TRACE_RESTRICT_ORDRCND, XSCF_NULL,                3,   3, "tracerestrict_order_cond",  nullptr, nullptr, nullptr        },
	{ XSLFI_TRACE_RESTRICT_STATUSCND,XSCF_NULL,               1,   1, "tracerestrict_status_cond", nullptr, nullptr, nullptr        },
	{ XSLFI_TRACE_RESTRICT_REVERSE, XSCF_NULL,                1,   1, "tracerestrict_reverse",     nullptr, nullptr, nullptr        },
	{ XSLFI_PROG_SIGS,              XSCF_NULL,                1,   1, "programmable_signals",      nullptr, nullptr, "SPRG"      },
	{ XSLFI_ADJACENT_CROSSINGS,     XSCF_NULL,                1,   1, "adjacent_crossings",        nullptr, nullptr, nullptr        },
	{ XSLFI_SAFER_CROSSINGS,        XSCF_NULL,                1,   1, "safer_crossings",           nullptr, nullptr, nullptr        },
	{ XSLFI_DEPARTURE_BOARDS,       XSCF_IGNORABLE_UNKNOWN,   1,   1, "departure_boards",          nullptr, nullptr, nullptr        },
	{ XSLFI_TIMETABLES_START_TICKS, XSCF_NULL,                2,   2, "timetable_start_ticks",     nullptr, nullptr, nullptr        },
	{ XSLFI_TOWN_CARGO_ADJ,         XSCF_IGNORABLE_UNKNOWN,   2,   2, "town_cargo_adj",            nullptr, nullptr, nullptr        },
	{ XSLFI_SIG_TUNNEL_BRIDGE,      XSCF_NULL,                7,   7, "signal_tunnel_bridge",      nullptr, nullptr, "XBSS"      },
	{ XSLFI_IMPROVED_BREAKDOWNS,    XSCF_NULL,                6,   6, "improved_breakdowns",       nullptr, nullptr, nullptr        },
	{ XSLFI_CONSIST_BREAKDOWN_FLAG, XSCF_NULL,                1,   1, "consist_breakdown_flag",    nullptr, nullptr, nullptr        },
	{ XSLFI_TT_WAIT_IN_DEPOT,       XSCF_NULL,                1,   1, "tt_wait_in_depot",          nullptr, nullptr, nullptr        },
	{ XSLFI_AUTO_TIMETABLE,         XSCF_NULL,                4,   4, "auto_timetables",           nullptr, nullptr, nullptr        },
	{ XSLFI_VEHICLE_REPAIR_COST,    XSCF_NULL,                2,   2, "vehicle_repair_cost",       nullptr, nullptr, nullptr        },
	{ XSLFI_ENH_VIEWPORT_PLANS,     XSCF_IGNORABLE_ALL,       3,   3, "enh_viewport_plans",        nullptr, nullptr, "PLAN"      },
	{ XSLFI_INFRA_SHARING,          XSCF_NULL,                2,   2, "infra_sharing",             nullptr, nullptr, "CPDP"      },
	{ XSLFI_VARIABLE_DAY_LENGTH,    XSCF_NULL,                2,   2, "variable_day_length",       nullptr, nullptr, nullptr        },
	{ XSLFI_ORDER_OCCUPANCY,        XSCF_NULL,                2,   2, "order_occupancy",           nullptr, nullptr, nullptr        },
	{ XSLFI_MORE_COND_ORDERS,       XSCF_NULL,                5,   5, "more_cond_orders",          nullptr, nullptr, nullptr        },
	{ XSLFI_EXTRA_LARGE_MAP,        XSCF_NULL,                0,   1, "extra_large_map",           nullptr, nullptr, nullptr        },
	{ XSLFI_REVERSE_AT_WAYPOINT,    XSCF_NULL,                1,   1, "reverse_at_waypoint",       nullptr, nullptr, nullptr        },
	{ XSLFI_VEH_LIFETIME_PROFIT,    XSCF_NULL,                1,   1, "veh_lifetime_profit",       nullptr, nullptr, nullptr        },
	{ XSLFI_LINKGRAPH_DAY_SCALE,    XSCF_NULL,                1,   1, "linkgraph_day_scale",       nullptr, nullptr, nullptr        },
	{ XSLFI_TEMPLATE_REPLACEMENT,   XSCF_NULL,                5,   5, "template_replacement",      nullptr, nullptr, "TRPL,TMPL" },
	{ XSLFI_MORE_RAIL_TYPES,        XSCF_NULL,                0,   1, "more_rail_types",           nullptr, nullptr, nullptr        },
	{ XSLFI_CARGO_TYPE_ORDERS,      XSCF_NULL,                3,   3, "cargo_type_orders",         nullptr, nullptr, "ORDX,VEOX" },
	{ XSLFI_EXTENDED_GAMELOG,       XSCF_NULL,                1,   1, "extended_gamelog",          nullptr, nullptr, nullptr        },
	{ XSLFI_STATION_CATCHMENT_INC,  XSCF_NULL,                1,   1, "station_catchment_inc",     nullptr, nullptr, nullptr        },
	{ XSLFI_CUSTOM_BRIDGE_HEADS,    XSCF_NULL,                3,   3, "custom_bridge_heads",       nullptr, nullptr, nullptr        },
	{ XSLFI_CHUNNEL,                XSCF_NULL,                2,   2, "chunnel",                   nullptr, nullptr, "TUNN"      },
	{ XSLFI_SCHEDULED_DISPATCH,     XSCF_NULL,                2,   2, "scheduled_dispatch",        nullptr, nullptr, nullptr        },
	{ XSLFI_MORE_TOWN_GROWTH_RATES, XSCF_NULL,                1,   1, "more_town_growth_rates",    nullptr, nullptr, nullptr        },
	{ XSLFI_MULTIPLE_DOCKS,         XSCF_NULL,                2,   2, "multiple_docks",            nullptr, nullptr, nullptr        },
	{ XSLFI_TIMETABLE_EXTRA,        XSCF_NULL,                6,   6, "timetable_extra",           nullptr, nullptr, "ORDX"      },
	{ XSLFI_TRAIN_FLAGS_EXTRA,      XSCF_NULL,                1,   1, "train_flags_extra",         nullptr, nullptr, nullptr        },
	{ XSLFI_TRAIN_THROUGH_LOAD,     XSCF_NULL,                2,   2, "train_through_load",        nullptr, nullptr, nullptr        },
	{ XSLFI_ORDER_EXTRA_DATA,       XSCF_NULL,                1,   1, "order_extra_data",          nullptr, nullptr, nullptr        },
	{ XSLFI_WHOLE_MAP_CHUNK,        XSCF_NULL,                2,   2, "whole_map_chunk",           nullptr, nullptr, "WMAP"      },
	{ XSLFI_ST_LAST_VEH_TYPE,       XSCF_NULL,                1,   1, "station_last_veh_type",     nullptr, nullptr, nullptr        },
	{ XSLFI_SELL_AT_DEPOT_ORDER,    XSCF_NULL,                1,   1, "sell_at_depot_order",       nullptr, nullptr, nullptr        },
	{ XSLFI_BUY_LAND_RATE_LIMIT,    XSCF_NULL,                1,   1, "buy_land_rate_limit",       nullptr, nullptr, nullptr        },
	{ XSLFI_DUAL_RAIL_TYPES,        XSCF_NULL,                1,   1, "dual_rail_types",           nullptr, nullptr, nullptr        },
	{ XSLFI_CONSIST_SPEED_RD_FLAG,  XSCF_NULL,                1,   1, "consist_speed_rd_flag",     nullptr, nullptr, nullptr        },
	{ XSLFI_SAVEGAME_UNIQUE_ID,     XSCF_IGNORABLE_ALL,       1,   1, "savegame_unique_id",        nullptr, nullptr, nullptr        },
	{ XSLFI_RV_OVERTAKING,          XSCF_NULL,                1,   1, "roadveh_overtaking",        nullptr, nullptr, nullptr        },
	{ XSLFI_LINKGRAPH_MODES,        XSCF_NULL,                1,   1, "linkgraph_modes",           nullptr, nullptr, nullptr        },
	{ XSLFI_GAME_EVENTS,            XSCF_NULL,                1,   1, "game_events",               nullptr, nullptr, nullptr        },
	{ XSLFI_ROAD_LAYOUT_CHANGE_CTR, XSCF_NULL,                1,   1, "road_layout_change_ctr",    nullptr, nullptr, nullptr        },
	{ XSLFI_TOWN_CARGO_MATRIX,      XSCF_NULL,                1,   1, "town_cargo_matrix",         nullptr, nullptr, nullptr        },
	{ XSLFI_STATE_CHECKSUM,         XSCF_NULL,                1,   1, "state_checksum",            nullptr, nullptr, nullptr        },
	{ XSLFI_DEBUG,                  XSCF_IGNORABLE_ALL,       1,   1, "debug",                     nullptr, nullptr, "DBGL,DBGC"    },
	{ XSLFI_FLOW_STAT_FLAGS,        XSCF_NULL,                1,   1, "flow_stat_flags",           nullptr, nullptr, nullptr        },
	{ XSLFI_SPEED_RESTRICTION,      XSCF_NULL,                1,   1, "speed_restriction",         nullptr, nullptr, "VESR"         },
	{ XSLFI_STATION_GOODS_EXTRA,    XSCF_NULL,                1,   1, "station_goods_extra",       nullptr, nullptr, nullptr        },
	{ XSLFI_DOCKING_CACHE_VER,      XSCF_IGNORABLE_ALL,       1,   1, "docking_cache_ver",         nullptr, nullptr, nullptr        },
	{ XSLFI_EXTRA_CHEATS,           XSCF_NULL,                1,   1, "extra_cheats",              nullptr, nullptr, "CHTX"         },
	{ XSLFI_NULL, XSCF_NULL, 0, 0, nullptr, nullptr, nullptr, nullptr },// This is the end marker
};

/**
 * Extended save/load feature test
 *
 * First performs a tradional check on the provided @p savegame_version against @p savegame_version_from and @p savegame_version_to.
 * Then, if the feature set in the constructor is not XSLFI_NULL, also check than the feature version is inclusively bounded by @p min_version and @p max_version,
 * and return the combination of the two tests using the operator defined in the constructor.
 * Otherwise just returns the result of the savegame version test
 */
bool SlXvFeatureTest::IsFeaturePresent(SaveLoadVersion savegame_version, SaveLoadVersion savegame_version_from, SaveLoadVersion savegame_version_to) const
{
	bool savegame_version_ok = savegame_version >= savegame_version_from && savegame_version < savegame_version_to;

	if (this->functor) return (*this->functor)(savegame_version, savegame_version_ok);

	if (this->feature == XSLFI_NULL) return savegame_version_ok;

	bool feature_ok = SlXvIsFeaturePresent(this->feature, this->min_version, this->max_version);

	switch (op) {
		case XSLFTO_OR:
			return savegame_version_ok || feature_ok;

		case XSLFTO_AND:
			return savegame_version_ok && feature_ok;

		default:
			NOT_REACHED();
			return false;
	}
}

/**
 * Returns true if @p feature is present and has a version inclusively bounded by @p min_version and @p max_version
 */
bool SlXvIsFeaturePresent(SlXvFeatureIndex feature, uint16 min_version, uint16 max_version)
{
	assert(feature < XSLFI_SIZE);
	return _sl_xv_feature_versions[feature] >= min_version && _sl_xv_feature_versions[feature] <= max_version;
}

/**
 * Returns true if @p feature is present and has a version inclusively bounded by @p min_version and @p max_version
 */
const char *SlXvGetFeatureName(SlXvFeatureIndex feature)
{
	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		if (info->index == feature) {
			return info->name;
		}
	}
	return "(unknown feature)";
}

/**
 * Resets all extended feature versions to 0
 */
void SlXvResetState()
{
	_sl_is_ext_version = false;
	_sl_is_faked_ext = false;
	_sl_maybe_springpp = false;
	_sl_maybe_chillpp = false;
	_sl_xv_discardable_chunk_ids.clear();
	memset(_sl_xv_feature_versions, 0, sizeof(_sl_xv_feature_versions));
}

/**
 * Resets all extended feature versions to their currently enabled versions, i.e. versions suitable for saving
 */
void SlXvSetCurrentState()
{
	SlXvResetState();
	_sl_is_ext_version = true;

	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		_sl_xv_feature_versions[info->index] = info->save_version;
	}
	if (MapSizeX() > 8192 || MapSizeY() > 8192) {
		_sl_xv_feature_versions[XSLFI_EXTRA_LARGE_MAP] = 1;
	}
}

/**
 * Check for "special" savegame versions (i.e. known patchpacks) and set correct savegame version, settings, etc.
 */
bool SlXvCheckSpecialSavegameVersions()
{
	// Checks for special savegame versions go here
	extern SaveLoadVersion _sl_version;

	if (_sl_version == SL_TRACE_RESTRICT_2000) {
		DEBUG(sl, 1, "Loading a trace restrict patch savegame version %d as version 194", _sl_version);
		_sl_version = SLV_194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 1;
		return true;
	}
	if (_sl_version == SL_TRACE_RESTRICT_2001) {
		DEBUG(sl, 1, "Loading a trace restrict patch savegame version %d as version 195", _sl_version);
		_sl_version = SLV_195;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 6;
		return true;
	}
	if (_sl_version == SL_TRACE_RESTRICT_2002) {
		DEBUG(sl, 1, "Loading a trace restrict patch savegame version %d as version 196", _sl_version);
		_sl_version = SLV_196;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 6;
		return true;
	}
	if (_sl_version >= SL_SPRING_2013_v2_0_102 && _sl_version <= SL_SPRING_2013_v2_4) { /* 220 - 227 */
		_sl_maybe_springpp = true;
		return true;
	}
	if (_sl_version >= SL_JOKER_1_19 && _sl_version <= SL_JOKER_1_27) { /* 278 - 286 */
		DEBUG(sl, 1, "Loading a JokerPP savegame version %d as version 197", _sl_version);
		_sl_xv_feature_versions[XSLFI_JOKERPP] = _sl_version;
		_sl_xv_feature_versions[XSLFI_TOWN_CARGO_ADJ] = 1;
		_sl_xv_feature_versions[XSLFI_TEMPLATE_REPLACEMENT] = 1;
		_sl_xv_feature_versions[XSLFI_VEH_LIFETIME_PROFIT] = 1;
		_sl_xv_feature_versions[XSLFI_TRAIN_FLAGS_EXTRA] = 1;
		_sl_xv_feature_versions[XSLFI_SIG_TUNNEL_BRIDGE] = 5;
		_sl_xv_feature_versions[XSLFI_REVERSE_AT_WAYPOINT] = 1;
		_sl_xv_feature_versions[XSLFI_MULTIPLE_DOCKS] = 1;
		_sl_xv_feature_versions[XSLFI_ST_LAST_VEH_TYPE] = 1;
		_sl_xv_feature_versions[XSLFI_MORE_RAIL_TYPES] = 1;
		_sl_xv_feature_versions[XSLFI_CHUNNEL] = 1;
		_sl_xv_feature_versions[XSLFI_MORE_COND_ORDERS] = 1;
		_sl_xv_feature_versions[XSLFI_TRACE_RESTRICT] = 1;
		_sl_xv_feature_versions[XSLFI_CARGO_TYPE_ORDERS] = 1;
		_sl_xv_feature_versions[XSLFI_RAIL_AGEING] = 1;
		if (_sl_version >= SL_JOKER_1_21) _sl_xv_feature_versions[XSLFI_LINKGRAPH_DAY_SCALE] = 1;
		if (_sl_version >= SL_JOKER_1_24) _sl_xv_feature_versions[XSLFI_TIMETABLE_EXTRA] = 1;
		if (_sl_version >= SL_JOKER_1_24) _sl_xv_feature_versions[XSLFI_ORDER_EXTRA_DATA] = 1;
		_sl_xv_discardable_chunk_ids.push_back('SPRG');
		_sl_xv_discardable_chunk_ids.push_back('SLNK');
		_sl_version = SLV_197;
		_sl_is_faked_ext = true;
		return true;
	}
	if (_sl_version == SL_CHILLPP_201) { /* 232 - 233 */
		_sl_maybe_chillpp = true;
		return true;
	}
	if (_sl_version >= SL_CHILLPP_232 && _sl_version <= SL_CHILLPP_233) { /* 232 - 233 */
		DEBUG(sl, 1, "Loading a ChillPP v14.7 savegame version %d as version 160", _sl_version);
		_sl_xv_feature_versions[XSLFI_CHILLPP] = _sl_version;
		_sl_xv_feature_versions[XSLFI_ZPOS_32_BIT] = 1;
		_sl_xv_feature_versions[XSLFI_TOWN_CARGO_ADJ] = 1;
		_sl_xv_feature_versions[XSLFI_TRAFFIC_LIGHTS] = 1;
		_sl_xv_feature_versions[XSLFI_IMPROVED_BREAKDOWNS] = 1;
		_sl_xv_feature_versions[XSLFI_INFRA_SHARING] = 1;
		_sl_xv_feature_versions[XSLFI_AUTO_TIMETABLE] = 1;
		_sl_xv_feature_versions[XSLFI_SIG_TUNNEL_BRIDGE] = 1;
		_sl_xv_feature_versions[XSLFI_RAIL_AGEING] = 1;
		_sl_xv_discardable_chunk_ids.push_back('LGRP');
		_sl_xv_discardable_chunk_ids.push_back('SSIG');
		_sl_version = SLV_160;
		_sl_is_faked_ext = true;
		return true;
	}
	return false;
}

void SlXvSpringPPSpecialSavegameVersions()
{
	extern SaveLoadVersion _sl_version;

	if (_sl_version == SL_SPRING_2013_v2_0_102) { /* 220 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.0.102 savegame version %d as version 187", _sl_version);

		_sl_version = SLV_187;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 1;
	} else if (_sl_version == SL_SPRING_2013_v2_1_108) { /* 221 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.1.108 savegame version %d as version 188", _sl_version);

		_sl_version = SLV_188;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 2;
	} else if (_sl_version == SL_SPRING_2013_v2_1_147) { /* 222 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.1.147 savegame version %d as version 194", _sl_version);

		_sl_version = SLV_194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 4; // Note that this break in numbering is deliberate
	} else if (_sl_version == SL_SPRING_2013_v2_3_XXX) { /* 223 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.3.xxx savegame version %d as version 194", _sl_version);

		_sl_version = SLV_194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 3; // Note that this break in numbering is deliberate
	} else if (_sl_version == SL_SPRING_2013_v2_3_b3) { /* 224 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.3.b3 savegame version %d as version 194", _sl_version);

		_sl_version = SLV_194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 5;
	} else if (_sl_version == SL_SPRING_2013_v2_3_b4) { /* 225 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.3.b4 savegame version %d as version 194", _sl_version);

		_sl_version = SLV_194;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 6;
	} else if (_sl_version == SL_SPRING_2013_v2_3_b5) { /* 226 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.3.b5 savegame version %d as version 195", _sl_version);

		_sl_version = SLV_195;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 7;
	} else if (_sl_version == SL_SPRING_2013_v2_4) { /* 227 */
		DEBUG(sl, 1, "Loading a SpringPP 2013 v2.4 savegame version %d as version 195", _sl_version);

		_sl_version = SLV_195;
		_sl_is_faked_ext = true;
		_sl_xv_feature_versions[XSLFI_SPRINGPP] = 8;
	}

	if (_sl_xv_feature_versions[XSLFI_SPRINGPP]) {
		_sl_xv_feature_versions[XSLFI_RIFF_HEADER_60_BIT] = 1;
		_sl_xv_feature_versions[XSLFI_HEIGHT_8_BIT] = 1;
		_sl_xv_feature_versions[XSLFI_MIGHT_USE_PAX_SIGNALS] = 1;
		_sl_xv_feature_versions[XSLFI_TRAFFIC_LIGHTS] = 1;
		_sl_xv_feature_versions[XSLFI_RAIL_AGEING] = 1;

		_sl_xv_feature_versions[XSLFI_TIMETABLES_START_TICKS] = 1;
		_sl_xv_feature_versions[XSLFI_VEHICLE_REPAIR_COST] = 1;
		_sl_xv_feature_versions[XSLFI_IMPROVED_BREAKDOWNS] = 1;
		_sl_xv_feature_versions[XSLFI_INFRA_SHARING] = 1;
		_sl_xv_feature_versions[XSLFI_AUTO_TIMETABLE] = 1;
		_sl_xv_feature_versions[XSLFI_MORE_COND_ORDERS] = 1;
		_sl_xv_feature_versions[XSLFI_SIG_TUNNEL_BRIDGE] = 1;

		_sl_xv_discardable_chunk_ids.push_back('SNOW');
	}
}

void SlXvChillPPSpecialSavegameVersions()
{
	extern SaveLoadVersion _sl_version;

	if (_sl_version == SL_CHILLPP_201) { /* 201 */
		DEBUG(sl, 1, "Loading a ChillPP v8 savegame version %d as version 143", _sl_version);
		_sl_xv_feature_versions[XSLFI_CHILLPP] = _sl_version;
		_sl_xv_feature_versions[XSLFI_ZPOS_32_BIT] = 1;
		_sl_xv_feature_versions[XSLFI_TOWN_CARGO_ADJ] = 1;
		_sl_xv_feature_versions[XSLFI_AUTO_TIMETABLE] = 1;
		_sl_xv_feature_versions[XSLFI_SIG_TUNNEL_BRIDGE] = 1;
		_sl_xv_feature_versions[XSLFI_RAIL_AGEING] = 1;
		_sl_xv_discardable_chunk_ids.push_back('LGRP');
		_sl_version = SLV_143;
		_sl_is_faked_ext = true;
	}
}

/**
 * Return true if this chunk has been marked as discardable
 */
bool SlXvIsChunkDiscardable(uint32 id)
{
	for (size_t i = 0; i < _sl_xv_discardable_chunk_ids.size(); i++) {
		if (_sl_xv_discardable_chunk_ids[i] == id) {
			return true;
		}
	}
	return false;
}

/**
 * Writes a chunk ID list string to the savegame, returns the number of chunks written
 * In dry run mode, only returns the number of chunk which would have been written
 */
static uint32 WriteChunkIdList(const char *chunk_list, bool dry_run)
{
	unsigned int chunk_count = 0;  // number of chunks output
	unsigned int id_offset = 0;    // how far are we into the ID
	for (; *chunk_list != 0; chunk_list++) {
		if (id_offset == 4) {
			assert(*chunk_list == ',');
			id_offset = 0;
		} else {
			if (!dry_run) {
				SlWriteByte(*chunk_list);
			}
			if (id_offset == 3) {
				chunk_count++;
			}
			id_offset++;
		}
	}
	assert(id_offset == 4);
	return chunk_count;
}

static void Save_SLXI()
{
	SlXvSetCurrentState();

	static const SaveLoad _xlsi_sub_chunk_desc[] = {
		SLE_STR(SlxiSubChunkInfo, name,           SLE_STR, 0),
		SLE_END()
	};

	// calculate lengths
	uint32 item_count = 0;
	uint32 length = 12;
	std::vector<uint32> extra_data_lengths;
	std::vector<uint32> chunk_counts;
	extra_data_lengths.resize(XSLFI_SIZE);
	chunk_counts.resize(XSLFI_SIZE);
	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		if (_sl_xv_feature_versions[info->index] > 0) {
			item_count++;
			length += 6;
			length += SlCalcObjLength(info, _xlsi_sub_chunk_desc);
			if (info->save_proc) {
				uint32 extra_data_length = info->save_proc(info, true);
				if (extra_data_length) {
					extra_data_lengths[info->index] = extra_data_length;
					length += 4 + extra_data_length;
				}
			}
			if (info->chunk_list) {
				uint32 chunk_count = WriteChunkIdList(info->chunk_list, true);
				if (chunk_count) {
					chunk_counts[info->index] = chunk_count;
					length += 4 * (1 + chunk_count);
				}
			}
		}
	}

	// write header
	SlSetLength(length);
	SlWriteUint32(_sl_xv_slxi_chunk_version);               // chunk version
	SlWriteUint32(0);                                       // flags
	SlWriteUint32(item_count);                              // item count

	// write data
	info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		uint16 save_version = _sl_xv_feature_versions[info->index];
		if (save_version > 0) {
			SlxiSubChunkFlags flags = info->flags;
			assert(!(flags & (XSCF_EXTRA_DATA_PRESENT | XSCF_CHUNK_ID_LIST_PRESENT)));
			uint32 extra_data_length = extra_data_lengths[info->index];
			uint32 chunk_count = chunk_counts[info->index];
			if (extra_data_length > 0) flags |= XSCF_EXTRA_DATA_PRESENT;
			if (chunk_count > 0) flags |= XSCF_CHUNK_ID_LIST_PRESENT;
			SlWriteUint32(flags);
			SlWriteUint16(save_version);
			SlObject(const_cast<SlxiSubChunkInfo *>(info), _xlsi_sub_chunk_desc);

			if (extra_data_length > 0) {
				SlWriteUint32(extra_data_length);
				size_t written = SlGetBytesWritten();
				info->save_proc(info, false);
				assert(SlGetBytesWritten() == written + extra_data_length);
			}
			if (chunk_count > 0) {
				SlWriteUint32(chunk_count);
				size_t written = SlGetBytesWritten();
				WriteChunkIdList(info->chunk_list, false);
				assert(SlGetBytesWritten() == written + (chunk_count * 4));
			}
		}
	}
}

static void Load_SLXI()
{
	if (_sl_is_faked_ext || !_sl_is_ext_version) {
		SlErrorCorrupt("SXLI chunk is unexpectedly present");
	}

	SlXvResetState();
	_sl_is_ext_version = true;

	uint32 version = SlReadUint32();
	if (version > _sl_xv_slxi_chunk_version) SlErrorCorruptFmt("SLXI chunk: version: %u is too new (expected max: %u)", version, _sl_xv_slxi_chunk_version);

	uint32 chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("SLXI chunk: unknown chunk header flags: 0x%X", chunk_flags);

	char name_buffer[256];
	const SaveLoadGlobVarList xlsi_sub_chunk_name_desc[] = {
		SLEG_STR(name_buffer, SLE_STRB),
		SLEG_END()
	};

	uint32 item_count = SlReadUint32();
	for (uint32 i = 0; i < item_count; i++) {
		SlxiSubChunkFlags flags = static_cast<SlxiSubChunkFlags>(SlReadUint32());
		uint16 version = SlReadUint16();
		SlGlobList(xlsi_sub_chunk_name_desc);

		// linearly scan through feature list until found name match
		bool found = false;
		const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
		for (; info->index != XSLFI_NULL; ++info) {
			if (strcmp(name_buffer, info->name) == 0) {
				found = true;
				break;
			}
		}

		bool discard_chunks = false;
		if (found) {
			if (version > info->max_version) {
				if (flags & XSCF_IGNORABLE_VERSION) {
					// version too large but carry on regardless
					discard_chunks = true;
					if (flags & XSCF_EXTRA_DATA_PRESENT) {
						SlSkipBytes(SlReadUint32()); // skip extra data field
					}
					DEBUG(sl, 1, "SLXI chunk: too large version for feature: '%s', version: %d, max version: %d, ignoring", name_buffer, version, info->max_version);
				} else {
					SlErrorCorruptFmt("SLXI chunk: too large version for feature: '%s', version: %d, max version: %d", name_buffer, version, info->max_version);
				}
			} else {
				// success path :)

				_sl_xv_feature_versions[info->index] = version;
				if (flags & XSCF_EXTRA_DATA_PRESENT) {
					uint32 extra_data_size = SlReadUint32();
					if (extra_data_size) {
						if (info->load_proc) {
							size_t read = SlGetBytesRead();
							info->load_proc(info, extra_data_size);
							if (SlGetBytesRead() != read + extra_data_size) {
								SlErrorCorruptFmt("SLXI chunk: feature: %s, version: %d, extra data length mismatch", name_buffer, version);
							}
						} else {
							SlErrorCorruptFmt("SLXI chunk: feature: %s, version: %d, unexpectedly includes extra data", name_buffer, version);
						}
					}
				}

				DEBUG(sl, 1, "SLXI chunk: found known feature: '%s', version: %d, max version: %d", name_buffer, version, info->max_version);
			}
		} else {
			if (flags & XSCF_IGNORABLE_UNKNOWN) {
				// not found but carry on regardless
				discard_chunks = true;
				if (flags & XSCF_EXTRA_DATA_PRESENT) {
					SlSkipBytes(SlReadUint32()); // skip extra data field
				}
				DEBUG(sl, 1, "SLXI chunk: unknown feature: '%s', version: %d, ignoring", name_buffer, version);
			} else {
				SlErrorCorruptFmt("SLXI chunk: unknown feature: %s, version: %d", name_buffer, version);
			}
		}

		// at this point the extra data field should have been consumed
		// handle chunk ID list field
		if (flags & XSCF_CHUNK_ID_LIST_PRESENT) {
			uint32 chunk_count = SlReadUint32();
			for (uint32 j = 0; j < chunk_count; j++) {
				uint32 chunk_id = SlReadUint32();
				if (discard_chunks) {
					_sl_xv_discardable_chunk_ids.push_back(chunk_id);
					DEBUG(sl, 2, "SLXI chunk: unknown feature: '%s', discarding chunk: %c%c%c%c", name_buffer, chunk_id >> 24, chunk_id >> 16, chunk_id >> 8, chunk_id);
				}
			}
		}
	}
}

extern const ChunkHandler _version_ext_chunk_handlers[] = {
	{ 'SLXI', Save_SLXI, Load_SLXI, nullptr, Load_SLXI, CH_RIFF | CH_LAST},
};
