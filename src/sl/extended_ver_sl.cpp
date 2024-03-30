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
 * uint32_t                             chunk version
 * uint32_t                             chunk flags
 * uint32_t                             number of sub chunks/features
 *     For each of N sub chunk/feature:
 *     uint32_t                         feature flags (SlxiSubChunkFlags)
 *     uint16_t                         feature version
 *     SLE_STR                          feature name
 *     uint32_t*                        extra data length [only present iff feature flags & XSCF_EXTRA_DATA_PRESENT]
 *         N bytes                      extra data
 *     uint32_t*                        chunk ID list count [only present iff feature flags & XSCF_CHUNK_ID_LIST_PRESENT]
 *         N x uint32_t                 chunk ID list
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
#include "saveload_buffer.h"
#include "extended_ver_sl.h"
#include "../timetable.h"
#include "../map_func.h"
#include "../rev.h"
#include "../strings_func.h"
#include "../company_func.h"
#include "table/strings.h"

#include <vector>

#include "../safeguards.h"

std::array<uint16_t, XSLFI_SIZE> _sl_xv_feature_versions;        ///< array of all known feature types and their current versions
std::array<uint16_t, XSLFI_SIZE> _sl_xv_feature_static_versions; ///< array of all known feature types and their static current version versions
bool _sl_is_ext_version;                                         ///< is this an extended savegame version, with more info in the SLXI chunk?
bool _sl_is_faked_ext;                                           ///< is this a faked extended savegame version, with no SLXI chunk? See: SlXvCheckSpecialSavegameVersions.
bool _sl_maybe_springpp;                                         ///< is this possibly a SpringPP savegame?
bool _sl_maybe_chillpp;                                          ///< is this possibly a ChillPP v8 savegame?
bool _sl_upstream_mode;                                          ///< load game using upstream loader
std::vector<uint32_t> _sl_xv_discardable_chunk_ids;              ///< list of chunks IDs which we can discard if no chunk loader exists
std::string _sl_xv_version_label;                                ///< optional SLXI version label
SaveLoadVersion _sl_xv_upstream_version;                         ///< optional SLXI upstream version

static const uint32_t _sl_xv_slxi_chunk_version = 0;             ///< current version of SLXI chunk

static void loadVL(const SlxiSubChunkInfo *info, uint32_t length);
static uint32_t saveVL(const SlxiSubChunkInfo *info, bool dry_run);
static void loadUV(const SlxiSubChunkInfo *info, uint32_t length);
static uint32_t saveUV(const SlxiSubChunkInfo *info, bool dry_run);
static void loadLC(const SlxiSubChunkInfo *info, uint32_t length);
static uint32_t saveLC(const SlxiSubChunkInfo *info, bool dry_run);
static void loadSTC(const SlxiSubChunkInfo *info, uint32_t length);
static uint32_t saveSTC(const SlxiSubChunkInfo *info, bool dry_run);

const SlxiSubChunkInfo _sl_xv_sub_chunk_infos[] = {
	{ XSLFI_VERSION_LABEL,                    XSCF_IGNORABLE_ALL,       1,   1, "version_label",                    saveVL,  loadVL,  nullptr          },
	{ XSLFI_UPSTREAM_VERSION,                 XSCF_NULL,                1,   1, "upstream_version",                 saveUV,  loadUV,  nullptr          },
	{ XSLFI_TRACE_RESTRICT,                   XSCF_NULL,               17,  17, "tracerestrict",                    nullptr, nullptr, "TRRM,TRRP,TRRS" },
	{ XSLFI_TRACE_RESTRICT_OWNER,             XSCF_NULL,                1,   1, "tracerestrict_owner",              nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_ORDRCND,           XSCF_NULL,                4,   4, "tracerestrict_order_cond",         nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_STATUSCND,         XSCF_NULL,                2,   2, "tracerestrict_status_cond",        nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_REVERSE,           XSCF_NULL,                1,   1, "tracerestrict_reverse",            nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_NEWSCTRL,          XSCF_NULL,                1,   1, "tracerestrict_newsctrl",           nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_COUNTER,           XSCF_NULL,                1,   1, "tracerestrict_counter",            nullptr, nullptr, "TRRC"           },
	{ XSLFI_TRACE_RESTRICT_TIMEDATE,          XSCF_NULL,                2,   2, "tracerestrict_timedate",           nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_BRKCND,            XSCF_NULL,                3,   3, "tracerestrict_braking_cond",       nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_CTGRYCND,          XSCF_NULL,                1,   1, "tracerestrict_ctgry_cond",         nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_PENCTRL,           XSCF_NULL,                1,   1, "tracerestrict_pfpenctrl",          nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_TUNBRIDGE,         XSCF_NULL,                1,   1, "tracerestrict_sigtunbridge",       nullptr, nullptr, nullptr          },
	{ XSLFI_TRACE_RESTRICT_SPDADAPTCTRL,      XSCF_NULL,                1,   1, "tracerestrict_spdadaptctrl",       nullptr, nullptr, nullptr          },
	{ XSLFI_PROG_SIGS,                        XSCF_NULL,                2,   2, "programmable_signals",             nullptr, nullptr, "SPRG"           },
	{ XSLFI_ADJACENT_CROSSINGS,               XSCF_NULL,                1,   1, "adjacent_crossings",               nullptr, nullptr, nullptr          },
	{ XSLFI_SAFER_CROSSINGS,                  XSCF_NULL,                1,   1, "safer_crossings",                  nullptr, nullptr, nullptr          },
	{ XSLFI_DEPARTURE_BOARDS,                 XSCF_IGNORABLE_UNKNOWN,   1,   1, "departure_boards",                 nullptr, nullptr, nullptr          },
	{ XSLFI_TIMETABLES_START_TICKS,           XSCF_NULL,                3,   3, "timetable_start_ticks",            nullptr, nullptr, nullptr          },
	{ XSLFI_TOWN_CARGO_ADJ,                   XSCF_IGNORABLE_UNKNOWN,   3,   3, "town_cargo_adj",                   nullptr, nullptr, nullptr          },
	{ XSLFI_SIG_TUNNEL_BRIDGE,                XSCF_NULL,               10,  10, "signal_tunnel_bridge",             nullptr, nullptr, "XBSS"           },
	{ XSLFI_IMPROVED_BREAKDOWNS,              XSCF_NULL,                8,   8, "improved_breakdowns",              nullptr, nullptr, nullptr          },
	{ XSLFI_CONSIST_BREAKDOWN_FLAG,           XSCF_NULL,                1,   1, "consist_breakdown_flag",           nullptr, nullptr, nullptr          },
	{ XSLFI_TT_WAIT_IN_DEPOT,                 XSCF_NULL,                2,   2, "tt_wait_in_depot",                 nullptr, nullptr, nullptr          },
	{ XSLFI_AUTO_TIMETABLE,                   XSCF_NULL,                5,   5, "auto_timetables",                  nullptr, nullptr, nullptr          },
	{ XSLFI_VEHICLE_REPAIR_COST,              XSCF_NULL,                2,   2, "vehicle_repair_cost",              nullptr, nullptr, nullptr          },
	{ XSLFI_ENH_VIEWPORT_PLANS,               XSCF_IGNORABLE_ALL,       4,   4, "enh_viewport_plans",               nullptr, nullptr, "PLAN"           },
	{ XSLFI_INFRA_SHARING,                    XSCF_NULL,                2,   2, "infra_sharing",                    nullptr, nullptr, "CPDP"           },
	{ XSLFI_VARIABLE_DAY_LENGTH,              XSCF_NULL,                6,   6, "variable_day_length",              nullptr, nullptr, nullptr          },
	{ XSLFI_ORDER_OCCUPANCY,                  XSCF_NULL,                2,   2, "order_occupancy",                  nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_COND_ORDERS,                 XSCF_NULL,               17,  17, "more_cond_orders",                 nullptr, nullptr, nullptr          },
	{ XSLFI_EXTRA_LARGE_MAP,                  XSCF_NULL,                0,   1, "extra_large_map",                  nullptr, nullptr, nullptr          },
	{ XSLFI_REVERSE_AT_WAYPOINT,              XSCF_NULL,                1,   1, "reverse_at_waypoint",              nullptr, nullptr, nullptr          },
	{ XSLFI_VEH_LIFETIME_PROFIT,              XSCF_NULL,                1,   1, "veh_lifetime_profit",              nullptr, nullptr, nullptr          },
	{ XSLFI_LINKGRAPH_DAY_SCALE,              XSCF_NULL,                6,   6, "linkgraph_day_scale",              nullptr, nullptr, nullptr          },
	{ XSLFI_TEMPLATE_REPLACEMENT,             XSCF_NULL,                9,   9, "template_replacement",             nullptr, nullptr, "TRPL,TMPL"      },
	{ XSLFI_MORE_RAIL_TYPES,                  XSCF_NULL,                0,   1, "more_rail_types",                  nullptr, nullptr, nullptr          },
	{ XSLFI_CARGO_TYPE_ORDERS,                XSCF_NULL,                3,   3, "cargo_type_orders",                nullptr, nullptr, "ORDX,VEOX"      },
	{ XSLFI_EXTENDED_GAMELOG,                 XSCF_NULL,                2,   2, "extended_gamelog",                 nullptr, nullptr, nullptr          },
	{ XSLFI_STATION_CATCHMENT_INC,            XSCF_NULL,                1,   1, "station_catchment_inc",            nullptr, nullptr, nullptr          },
	{ XSLFI_CUSTOM_BRIDGE_HEADS,              XSCF_NULL,                4,   4, "custom_bridge_heads",              nullptr, nullptr, nullptr          },
	{ XSLFI_CHUNNEL,                          XSCF_NULL,                2,   2, "chunnel",                          nullptr, nullptr, "TUNN"           },
	{ XSLFI_SCHEDULED_DISPATCH,               XSCF_NULL,                7,   7, "scheduled_dispatch",               nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_TOWN_GROWTH_RATES,           XSCF_NULL,                1,   1, "more_town_growth_rates",           nullptr, nullptr, nullptr          },
	{ XSLFI_MULTIPLE_DOCKS,                   XSCF_NULL,                2,   2, "multiple_docks",                   nullptr, nullptr, nullptr          },
	{ XSLFI_TIMETABLE_EXTRA,                  XSCF_NULL,                7,   7, "timetable_extra",                  nullptr, nullptr, "ORDX"           },
	{ XSLFI_TRAIN_FLAGS_EXTRA,                XSCF_NULL,                1,   1, "train_flags_extra",                nullptr, nullptr, nullptr          },
	{ XSLFI_VEHICLE_FLAGS_EXTRA,              XSCF_NULL,                1,   1, "veh_flags_extra",                  nullptr, nullptr, nullptr          },
	{ XSLFI_TRAIN_THROUGH_LOAD,               XSCF_NULL,                2,   2, "train_through_load",               nullptr, nullptr, nullptr          },
	{ XSLFI_ORDER_EXTRA_DATA,                 XSCF_NULL,                3,   3, "order_extra_data",                 nullptr, nullptr, nullptr          },
	{ XSLFI_WHOLE_MAP_CHUNK,                  XSCF_NULL,                2,   2, "whole_map_chunk",                  nullptr, nullptr, "WMAP"           },
	{ XSLFI_ST_LAST_VEH_TYPE,                 XSCF_NULL,                1,   1, "station_last_veh_type",            nullptr, nullptr, nullptr          },
	{ XSLFI_SELL_AT_DEPOT_ORDER,              XSCF_NULL,                1,   1, "sell_at_depot_order",              nullptr, nullptr, nullptr          },
	{ XSLFI_BUY_LAND_RATE_LIMIT,              XSCF_NULL,                1,   1, "buy_land_rate_limit",              nullptr, nullptr, nullptr          },
	{ XSLFI_DUAL_RAIL_TYPES,                  XSCF_NULL,                1,   1, "dual_rail_types",                  nullptr, nullptr, nullptr          },
	{ XSLFI_CONSIST_SPEED_RD_FLAG,            XSCF_NULL,                1,   1, "consist_speed_rd_flag",            nullptr, nullptr, nullptr          },
	{ XSLFI_SAVEGAME_UNIQUE_ID,               XSCF_IGNORABLE_ALL,       1,   1, "savegame_unique_id",               nullptr, nullptr, nullptr          },
	{ XSLFI_RV_OVERTAKING,                    XSCF_NULL,                2,   2, "roadveh_overtaking",               nullptr, nullptr, nullptr          },
	{ XSLFI_LINKGRAPH_MODES,                  XSCF_NULL,                1,   1, "linkgraph_modes",                  nullptr, nullptr, nullptr          },
	{ XSLFI_GAME_EVENTS,                      XSCF_NULL,                1,   1, "game_events",                      nullptr, nullptr, nullptr          },
	{ XSLFI_ROAD_LAYOUT_CHANGE_CTR,           XSCF_NULL,                1,   1, "road_layout_change_ctr",           nullptr, nullptr, nullptr          },
	{ XSLFI_TOWN_CARGO_MATRIX,                XSCF_NULL,                0,   1, "town_cargo_matrix",                nullptr, nullptr, nullptr          },
	{ XSLFI_STATE_CHECKSUM,                   XSCF_NULL,                1,   1, "state_checksum",                   nullptr, nullptr, nullptr          },
	{ XSLFI_DEBUG,                            XSCF_IGNORABLE_ALL,       1,   1, "debug",                            nullptr, nullptr, "DBGL,DBGC"      },
	{ XSLFI_FLOW_STAT_FLAGS,                  XSCF_NULL,                1,   1, "flow_stat_flags",                  nullptr, nullptr, nullptr          },
	{ XSLFI_SPEED_RESTRICTION,                XSCF_NULL,                1,   1, "speed_restriction",                nullptr, nullptr, "VESR"           },
	{ XSLFI_STATION_GOODS_EXTRA,              XSCF_NULL,                1,   1, "station_goods_extra",              nullptr, nullptr, nullptr          },
	{ XSLFI_DOCKING_CACHE_VER,                XSCF_IGNORABLE_ALL,       3,   3, "docking_cache_ver",                nullptr, nullptr, nullptr          },
	{ XSLFI_EXTRA_CHEATS,                     XSCF_NULL,                1,   1, "extra_cheats",                     nullptr, nullptr, "CHTX"           },
	{ XSLFI_TOWN_MULTI_BUILDING,              XSCF_NULL,                1,   1, "town_multi_building",              nullptr, nullptr, nullptr          },
	{ XSLFI_SHIP_LOST_COUNTER,                XSCF_NULL,                1,   1, "ship_lost_counter",                nullptr, nullptr, nullptr          },
	{ XSLFI_BUILD_OBJECT_RATE_LIMIT,          XSCF_NULL,                1,   1, "build_object_rate_limit",          nullptr, nullptr, nullptr          },
	{ XSLFI_LOCAL_COMPANY,                    XSCF_IGNORABLE_ALL,       1,   1, "local_company",                    saveLC,  loadLC,  nullptr          },
	{ XSLFI_THROUGH_TRAIN_DEPOT,              XSCF_NULL,                1,   1, "drive_through_train_depot",        nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_VEHICLE_ORDERS,              XSCF_NULL,                1,   1, "more_veh_orders",                  nullptr, nullptr, nullptr          },
	{ XSLFI_ORDER_FLAGS_EXTRA,                XSCF_NULL,                1,   1, "order_flags_extra",                nullptr, nullptr, nullptr          },
	{ XSLFI_ONE_WAY_DT_ROAD_STOP,             XSCF_NULL,                1,   1, "one_way_dt_road_stop",             nullptr, nullptr, nullptr          },
	{ XSLFI_ONE_WAY_ROAD_STATE,               XSCF_NULL,                1,   1, "one_way_road_state",               nullptr, nullptr, nullptr          },
	{ XSLFI_VENC_CHUNK,                       XSCF_IGNORABLE_ALL,       0,   1, "venc_chunk",                       nullptr, nullptr, "VENC"           },
	{ XSLFI_ANIMATED_TILE_EXTRA,              XSCF_NULL,                1,   1, "animated_tile_extra",              nullptr, nullptr, nullptr          },
	{ XSLFI_NEWGRF_INFO_EXTRA,                XSCF_NULL,                1,   1, "newgrf_info_extra",                nullptr, nullptr, nullptr          },
	{ XSLFI_INDUSTRY_CARGO_ADJ,               XSCF_IGNORABLE_UNKNOWN,   2,   2, "industry_cargo_adj",               nullptr, nullptr, nullptr          },
	{ XSLFI_REALISTIC_TRAIN_BRAKING,          XSCF_NULL,               11,  11, "realistic_train_braking",          nullptr, nullptr, "VLKA"           },
	{ XSLFI_INFLATION_FIXED_DATES,            XSCF_IGNORABLE_ALL,       1,   1, "inflation_fixed_dates",            nullptr, nullptr, nullptr          },
	{ XSLFI_WATER_FLOODING,                   XSCF_NULL,                2,   2, "water_flooding",                   nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_HOUSES,                      XSCF_NULL,                2,   2, "more_houses",                      nullptr, nullptr, nullptr          },
	{ XSLFI_CUSTOM_TOWN_ZONE,                 XSCF_IGNORABLE_UNKNOWN,   1,   1, "custom_town_zone",                 nullptr, nullptr, nullptr          },
	{ XSLFI_STATION_CARGO_HISTORY,            XSCF_NULL,                2,   2, "station_cargo_history",            nullptr, nullptr, nullptr          },
	{ XSLFI_TRAIN_SPEED_ADAPTATION,           XSCF_NULL,                2,   2, "train_speed_adaptation",           nullptr, nullptr, "TSAS"           },
	{ XSLFI_EXTRA_STATION_NAMES,              XSCF_NULL,                1,   1, "extra_station_names",              nullptr, nullptr, nullptr          },
	{ XSLFI_DEPOT_ORDER_EXTRA_FLAGS,          XSCF_IGNORABLE_UNKNOWN,   1,   1, "depot_order_extra_flags",          nullptr, nullptr, nullptr          },
	{ XSLFI_EXTRA_SIGNAL_TYPES,               XSCF_NULL,                1,   1, "extra_signal_types",               nullptr, nullptr, nullptr          },
	{ XSLFI_BANKRUPTCY_EXTRA,                 XSCF_NULL,                2,   2, "bankruptcy_extra",                 nullptr, nullptr, nullptr          },
	{ XSLFI_OBJECT_GROUND_TYPES,              XSCF_NULL,                4,   4, "object_ground_types",              nullptr, nullptr, nullptr          },
	{ XSLFI_LINKGRAPH_AIRCRAFT,               XSCF_NULL,                1,   1, "linkgraph_aircraft",               nullptr, nullptr, nullptr          },
	{ XSLFI_COMPANY_PW,                       XSCF_IGNORABLE_ALL,       2,   2, "company_password",                 nullptr, nullptr, "PLYP"           },
	{ XSLFI_ST_INDUSTRY_CARGO_MODE,           XSCF_IGNORABLE_UNKNOWN,   1,   1, "st_industry_cargo_mode",           nullptr, nullptr, nullptr          },
	{ XSLFI_TL_SPEED_LIMIT,                   XSCF_IGNORABLE_UNKNOWN,   1,   1, "tl_speed_limit",                   nullptr, nullptr, nullptr          },
	{ XSLFI_RAIL_DEPOT_SPEED_LIMIT,           XSCF_IGNORABLE_UNKNOWN,   1,   1, "rail_depot_speed_limit",           nullptr, nullptr, nullptr          },
	{ XSLFI_WAYPOINT_FLAGS,                   XSCF_NULL,                1,   1, "waypoint_flags",                   nullptr, nullptr, nullptr          },
	{ XSLFI_ROAD_WAYPOINTS,                   XSCF_NULL,                1,   1, "road_waypoints",                   nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_STATION_TYPES,               XSCF_NULL,                1,   1, "more_station_types",               nullptr, nullptr, nullptr          },
	{ XSLFI_RV_ORDER_EXTRA_FLAGS,             XSCF_IGNORABLE_UNKNOWN,   1,   1, "rv_order_extra_flags",             nullptr, nullptr, nullptr          },
	{ XSLFI_GRF_ROADSTOPS,                    XSCF_NULL,                3,   3, "grf_road_stops",                   nullptr, nullptr, nullptr          },
	{ XSLFI_INDUSTRY_ANIM_MASK,               XSCF_IGNORABLE_ALL,       1,   1, "industry_anim_mask",               nullptr, nullptr, nullptr          },
	{ XSLFI_NEW_SIGNAL_STYLES,                XSCF_NULL,                2,   2, "new_signal_styles",                nullptr, nullptr, "XBST,NSID"      },
	{ XSLFI_NO_TREE_COUNTER,                  XSCF_IGNORABLE_ALL,       1,   1, "no_tree_counter",                  nullptr, nullptr, nullptr          },
	{ XSLFI_TOWN_SETTING_OVERRIDE,            XSCF_NULL,                1,   1, "town_setting_override",            nullptr, nullptr, nullptr          },
	{ XSLFI_LINKGRAPH_SPARSE_EDGES,           XSCF_NULL,                1,   1, "linkgraph_sparse_edges",           nullptr, nullptr, nullptr          },
	{ XSLFI_AUX_TILE_LOOP,                    XSCF_NULL,                1,   1, "aux_tile_loop",                    nullptr, nullptr, nullptr          },
	{ XSLFI_NEWGRF_ENTITY_EXTRA,              XSCF_NULL,                2,   2, "newgrf_entity_extra",              nullptr, nullptr, nullptr          },
	{ XSLFI_TNNC_CHUNK,                       XSCF_IGNORABLE_ALL,       0,   1, "tnnc_chunk",                       nullptr, nullptr, "TNNC"           },
	{ XSLFI_MULTI_CARGO_SHIPS,                XSCF_NULL,                1,   1, "multi_cargo_ships",                nullptr, nullptr, nullptr          },
	{ XSLFI_REMAIN_NEXT_ORDER_STATION,        XSCF_IGNORABLE_UNKNOWN,   1,   1, "remain_next_order_station",        nullptr, nullptr, nullptr          },
	{ XSLFI_LABEL_ORDERS,                     XSCF_NULL,                2,   2, "label_orders",                     nullptr, nullptr, nullptr          },
	{ XSLFI_VARIABLE_TICK_RATE,               XSCF_IGNORABLE_ALL,       1,   1, "variable_tick_rate",               nullptr, nullptr, nullptr          },
	{ XSLFI_ROAD_VEH_FLAGS,                   XSCF_NULL,                1,   1, "road_veh_flags",                   nullptr, nullptr, nullptr          },
	{ XSLFI_STATION_TILE_CACHE_FLAGS,         XSCF_IGNORABLE_ALL,       1,   1, "station_tile_cache_flags",         saveSTC, loadSTC, nullptr          },
	{ XSLFI_INDUSTRY_CARGO_TOTALS,            XSCF_NULL,                1,   1, "industry_cargo_totals",            nullptr, nullptr, nullptr          },

	{ XSLFI_SCRIPT_INT64,                     XSCF_NULL,                1,   1, "script_int64",                     nullptr, nullptr, nullptr          },
	{ XSLFI_U64_TICK_COUNTER,                 XSCF_NULL,                1,   1, "u64_tick_counter",                 nullptr, nullptr, nullptr          },
	{ XSLFI_LINKGRAPH_TRAVEL_TIME,            XSCF_NULL,                1,   1, "linkgraph_travel_time",            nullptr, nullptr, nullptr          },
	{ XSLFI_LAST_LOADING_TICK,                XSCF_NULL,                3,   3, "last_loading_tick",                nullptr, nullptr, nullptr          },
	{ XSLFI_SCRIPT_LEAGUE_TABLES,             XSCF_NULL,                1,   1, "script_league_tables",             nullptr, nullptr, "LEAE,LEAT"      },
	{ XSLFI_VELOCITY_NAUTICAL,                XSCF_IGNORABLE_ALL,       1,   1, "velocity_nautical",                nullptr, nullptr, nullptr          },
	{ XSLFI_CONSISTENT_PARTIAL_Z,             XSCF_NULL,                1,   1, "consistent_partial_z",             nullptr, nullptr, nullptr          },
	{ XSLFI_MORE_CARGO_AGE,                   XSCF_NULL,                1,   1, "more_cargo_age",                   nullptr, nullptr, nullptr          },
	{ XSLFI_AI_START_DATE,                    XSCF_NULL,                1,   1, "slv_ai_start_date",                nullptr, nullptr, nullptr          },
	{ XSLFI_EXTEND_VEHICLE_RANDOM,            XSCF_NULL,                1,   1, "slv_extend_vehicle_random",        nullptr, nullptr, nullptr          },
	{ XSLFI_DISASTER_VEH_STATE,               XSCF_NULL,                1,   1, "slv_disaster_veh_state",           nullptr, nullptr, nullptr          },
	{ XSLFI_SAVEGAME_ID,                      XSCF_NULL,                1,   1, "slv_savegame_id",                  nullptr, nullptr, nullptr          },
	{ XSLFI_NEWGRF_LAST_SERVICE,              XSCF_NULL,                1,   1, "slv_newgrf_last_service",          nullptr, nullptr, nullptr          },
	{ XSLFI_CARGO_TRAVELLED,                  XSCF_NULL,                1,   1, "slv_cargo_travelled",              nullptr, nullptr, nullptr          },
	{ XSLFI_SHIP_ACCELERATION,                XSCF_NULL,                1,   1, "slv_ship_acceleration",            nullptr, nullptr, nullptr          },
	{ XSLFI_DEPOT_UNBUNCHING,                 XSCF_NULL,                1,   1, "slv_depot_unbunching",             nullptr, nullptr, "VUBS"           },
	{ XSLFI_VEHICLE_ECONOMY_AGE,              XSCF_NULL,                1,   1, "slv_vehicle_economy_age",          nullptr, nullptr, nullptr          },

	{ XSLFI_TABLE_PATS,                       XSCF_NULL,                1,   1, "table_pats",                       nullptr, nullptr, nullptr          },
	{ XSLFI_TABLE_MISC_SL,                    XSCF_NULL,                2,   2, "table_misc_sl",                    nullptr, nullptr, nullptr          },
	{ XSLFI_TABLE_SCRIPT_SL,                  XSCF_NULL,                1,   1, "table_script_sl",                  nullptr, nullptr, nullptr          },
	{ XSLFI_TABLE_NEWGRF_SL,                  XSCF_NULL,                2,   2, "table_newgrf_sl",                  nullptr, nullptr, nullptr          },
	{ XSLFI_TABLE_INDUSTRY_SL,                XSCF_NULL,                1,   1, "table_industry_sl",                nullptr, nullptr, nullptr          },

	{ XSLFI_NULL, XSCF_NULL, 0, 0, nullptr, nullptr, nullptr, nullptr }, // This is the end marker
};

/**
 * Extended save/load feature test
 *
 * First performs a traditional check on the provided @p savegame_version against @p savegame_version_from and @p savegame_version_to.
 * Then, if the feature set in the constructor is not XSLFI_NULL, also check that the feature version is inclusively bounded by @p min_version and @p max_version,
 * and return the combination of the two tests using the operator defined in the constructor.
 * Otherwise just returns the result of the savegame version test
 */
bool SlXvFeatureTest::IsFeaturePresent(const std::array<uint16_t, XSLFI_SIZE> &feature_versions, SaveLoadVersion savegame_version, SaveLoadVersion savegame_version_from, SaveLoadVersion savegame_version_to) const
{
	bool savegame_version_ok = savegame_version >= savegame_version_from && savegame_version < savegame_version_to;

	if (this->functor) return (*this->functor)(savegame_version, savegame_version_ok, feature_versions);

	if (this->feature == XSLFI_NULL) return savegame_version_ok;

	bool feature_ok = SlXvIsFeaturePresent(feature_versions, this->feature, this->min_version, this->max_version);

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
bool SlXvIsFeaturePresent(const std::array<uint16_t, XSLFI_SIZE> &feature_versions, SlXvFeatureIndex feature, uint16_t min_version, uint16_t max_version)
{
	assert(feature < XSLFI_SIZE);
	return feature_versions[feature] >= min_version && feature_versions[feature] <= max_version;
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
	_sl_upstream_mode = false;
	_sl_xv_discardable_chunk_ids.clear();
	std::fill(_sl_xv_feature_versions.begin(), _sl_xv_feature_versions.end(), 0);
	_sl_xv_version_label.clear();
	_sl_xv_upstream_version = SL_MIN_VERSION;
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
	if (IsScenarioSave()) {
		_sl_xv_feature_versions[XSLFI_WHOLE_MAP_CHUNK] = 0;
	}
	if (IsNetworkServerSave()) {
		_sl_xv_feature_versions[XSLFI_VENC_CHUNK] = 1;
		_sl_xv_feature_versions[XSLFI_TNNC_CHUNK] = 1;
	}
}

/**
 * Set all extended feature versions in the current static version array to their currently enabled versions, i.e. versions suitable for saving
 */
void SlXvSetStaticCurrentVersions()
{
	std::fill(_sl_xv_feature_static_versions.begin(), _sl_xv_feature_static_versions.end(), 0);

	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		_sl_xv_feature_static_versions[info->index] = info->save_version;
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
	if (_sl_version == SL_CHILLPP_201) { /* 201 */
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
bool SlXvIsChunkDiscardable(uint32_t id)
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
static uint32_t WriteChunkIdList(const char *chunk_list, bool dry_run)
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
	};

	// calculate lengths
	uint32_t item_count = 0;
	uint32_t length = 12;
	std::vector<uint32_t> extra_data_lengths;
	std::vector<uint32_t> chunk_counts;
	extra_data_lengths.resize(XSLFI_SIZE);
	chunk_counts.resize(XSLFI_SIZE);
	const SlxiSubChunkInfo *info = _sl_xv_sub_chunk_infos;
	for (; info->index != XSLFI_NULL; ++info) {
		if (_sl_xv_feature_versions[info->index] > 0) {
			item_count++;
			length += 6;
			length += (uint32_t)SlCalcObjLength(info, _xlsi_sub_chunk_desc);
			if (info->save_proc) {
				uint32_t extra_data_length = info->save_proc(info, true);
				if (extra_data_length) {
					extra_data_lengths[info->index] = extra_data_length;
					length += 4 + extra_data_length;
				}
			}
			if (info->chunk_list) {
				uint32_t chunk_count = WriteChunkIdList(info->chunk_list, true);
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
		uint16_t save_version = _sl_xv_feature_versions[info->index];
		if (save_version > 0) {
			SlxiSubChunkFlags flags = info->flags;
			assert(!(flags & (XSCF_EXTRA_DATA_PRESENT | XSCF_CHUNK_ID_LIST_PRESENT)));
			uint32_t extra_data_length = extra_data_lengths[info->index];
			uint32_t chunk_count = chunk_counts[info->index];
			if (extra_data_length > 0) flags |= XSCF_EXTRA_DATA_PRESENT;
			if (chunk_count > 0) flags |= XSCF_CHUNK_ID_LIST_PRESENT;
			SlWriteUint32(flags);
			SlWriteUint16(save_version);
			SlObject(const_cast<SlxiSubChunkInfo *>(info), _xlsi_sub_chunk_desc);

			if (extra_data_length > 0) {
				SlWriteUint32(extra_data_length);
				[[maybe_unused]] size_t written = SlGetBytesWritten();
				info->save_proc(info, false);
				assert(SlGetBytesWritten() == written + extra_data_length);
			}
			if (chunk_count > 0) {
				SlWriteUint32(chunk_count);
				[[maybe_unused]] size_t written = SlGetBytesWritten();
				WriteChunkIdList(info->chunk_list, false);
				assert(SlGetBytesWritten() == written + (chunk_count * 4));
			}
		}
	}
}

static void Load_SLXI()
{
	if (_sl_is_faked_ext || !_sl_is_ext_version) {
		SlErrorCorrupt("SLXI chunk is unexpectedly present");
	}

	SlXvResetState();
	_sl_is_ext_version = true;

	uint32_t version = SlReadUint32();
	if (version > _sl_xv_slxi_chunk_version) SlErrorCorruptFmt("SLXI chunk: version: %u is too new (expected max: %u)", version, _sl_xv_slxi_chunk_version);

	uint32_t chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("SLXI chunk: unknown chunk header flags: 0x%X", chunk_flags);

	char name_buffer[256];
	const SaveLoad xlsi_sub_chunk_name_desc[] = {
		SLEG_STR(name_buffer, SLE_STRB),
	};

	auto version_error = [](StringID str, const char *feature, int64_t p1, int64_t p2) {
		auto tmp_params = MakeParameters(_sl_xv_version_label.empty() ? STR_EMPTY : STR_GAME_SAVELOAD_FROM_VERSION, _sl_xv_version_label, feature, p1, p2);
		SlError(STR_JUST_RAW_STRING, GetStringWithArgs(str, tmp_params));
	};

	uint32_t item_count = SlReadUint32();
	for (uint32_t i = 0; i < item_count; i++) {
		SlxiSubChunkFlags flags = static_cast<SlxiSubChunkFlags>(SlReadUint32());
		uint16_t version = SlReadUint16();
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
					version_error(STR_GAME_SAVELOAD_ERROR_TOO_NEW_FEATURE_VERSION, name_buffer, version, info->max_version);
				}
			} else {
				// success path :)

				_sl_xv_feature_versions[info->index] = version;
				if (flags & XSCF_EXTRA_DATA_PRESENT) {
					uint32_t extra_data_size = SlReadUint32();
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
				version_error(STR_GAME_SAVELOAD_ERROR_UNKNOWN_FEATURE, name_buffer, version, 0);
			}
		}

		// at this point the extra data field should have been consumed
		// handle chunk ID list field
		if (flags & XSCF_CHUNK_ID_LIST_PRESENT) {
			uint32_t chunk_count = SlReadUint32();
			for (uint32_t j = 0; j < chunk_count; j++) {
				uint32_t chunk_id = SlReadUint32();
				if (discard_chunks) {
					_sl_xv_discardable_chunk_ids.push_back(chunk_id);
					DEBUG(sl, 2, "SLXI chunk: unknown feature: '%s', discarding chunk: %c%c%c%c", name_buffer, chunk_id >> 24, chunk_id >> 16, chunk_id >> 8, chunk_id);
				}
			}
		}
	}
}

static void IgnoreWrongLengthExtraData(const SlxiSubChunkInfo *info, uint32_t length)
{
	DEBUG(sl, 1, "SLXI chunk: feature: '%s', version: %d, has data of wrong length: %u", info->name, _sl_xv_feature_versions[info->index], length);
	ReadBuffer::GetCurrent()->SkipBytes(length);
}

static void loadVL(const SlxiSubChunkInfo *info, uint32_t length)
{
	_sl_xv_version_label.resize(length);
	ReadBuffer::GetCurrent()->CopyBytes(reinterpret_cast<byte *>(_sl_xv_version_label.data()), length);
	DEBUG(sl, 2, "SLXI version label: %s", _sl_xv_version_label.c_str());
}

static uint32_t saveVL(const SlxiSubChunkInfo *info, bool dry_run)
{
	const size_t length = strlen(_openttd_revision);
	if (!dry_run) MemoryDumper::GetCurrent()->CopyBytes(reinterpret_cast<const byte *>(_openttd_revision), length);
	return static_cast<uint32_t>(length);
}

static void loadUV(const SlxiSubChunkInfo *info, uint32_t length)
{
	if (length == 2) {
		_sl_xv_upstream_version = (SaveLoadVersion)SlReadUint16();
		DEBUG(sl, 2, "SLXI upstream version: %u", _sl_xv_upstream_version);
	} else {
		IgnoreWrongLengthExtraData(info, length);
	}
}

static uint32_t saveUV(const SlxiSubChunkInfo *info, bool dry_run)
{
	if (!dry_run) SlWriteUint16(SL_MAX_VERSION - 1);
	return 2;
}

static void loadLC(const SlxiSubChunkInfo *info, uint32_t length)
{
	if (length == 1) {
		_loaded_local_company = (CompanyID) ReadBuffer::GetCurrent()->ReadByte();
	} else {
		IgnoreWrongLengthExtraData(info, length);
	}
}

static uint32_t saveLC(const SlxiSubChunkInfo *info, bool dry_run)
{
	if (!dry_run) MemoryDumper::GetCurrent()->WriteByte(_local_company);
	return 1;
}

static void loadSTC(const SlxiSubChunkInfo *info, uint32_t length)
{
	extern uint64_t _station_tile_cache_hash;
	if (length == 8) {
		_station_tile_cache_hash = SlReadUint64();
	} else {
		IgnoreWrongLengthExtraData(info, length);
	}
}

static uint32_t saveSTC(const SlxiSubChunkInfo *info, bool dry_run)
{
	extern uint64_t _station_tile_cache_hash;
	if (!dry_run) SlWriteUint64(_station_tile_cache_hash);
	return 8;
}

extern const ChunkHandler version_ext_chunk_handlers[] = {
	{ 'SLXI', Save_SLXI, Load_SLXI, nullptr, Load_SLXI, CH_RIFF },
};
extern const ChunkHandlerTable _version_ext_chunk_handlers(version_ext_chunk_handlers);
