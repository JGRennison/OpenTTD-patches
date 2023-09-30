/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file extended_ver_sl.h Functions/types related to handling save/load extended version info. */

#ifndef SL_EXTENDED_VER_SL_H
#define SL_EXTENDED_VER_SL_H

#include "../core/bitmath_func.hpp"
#include "../core/enum_type.hpp"

#include <array>
#include <vector>

enum SaveLoadVersion : uint16;

/**
 * List of extended features, each feature has its own (16 bit) version
 */
enum SlXvFeatureIndex {
	XSLFI_NULL                          = 0,      ///< Unused value, to indicate that no extended feature test is in use
	XSLFI_VERSION_LABEL,                          ///< Version label
	XSLFI_UPSTREAM_VERSION,                       ///< Corresponding upstream savegame version
	XSLFI_TRACE_RESTRICT,                         ///< Trace restrict
	XSLFI_TRACE_RESTRICT_OWNER,                   ///< Trace restrict: train owner test
	XSLFI_TRACE_RESTRICT_ORDRCND,                 ///< Trace restrict: slot conditional order
	XSLFI_TRACE_RESTRICT_STATUSCND,               ///< Trace restrict: train status condition
	XSLFI_TRACE_RESTRICT_REVERSE,                 ///< Trace restrict: reverse
	XSLFI_TRACE_RESTRICT_NEWSCTRL,                ///< Trace restrict: news control
	XSLFI_TRACE_RESTRICT_COUNTER,                 ///< Trace restrict: counters
	XSLFI_TRACE_RESTRICT_TIMEDATE,                ///< Trace restrict: time/date
	XSLFI_TRACE_RESTRICT_BRKCND,                  ///< Trace restrict: realistic braking related conditionals
	XSLFI_TRACE_RESTRICT_CTGRYCND,                ///< Trace restrict: category conditionals
	XSLFI_TRACE_RESTRICT_PENCTRL,                 ///< Trace restrict: PF penalty control
	XSLFI_TRACE_RESTRICT_TUNBRIDGE,               ///< Trace restrict: restricted signalled tunnel/bridge support
	XSLFI_TRACE_RESTRICT_SPDADAPTCTRL,            ///< Trace restrict: speed adaptation control
	XSLFI_PROG_SIGS,                              ///< programmable pre-signals patch
	XSLFI_ADJACENT_CROSSINGS,                     ///< Adjacent level crossings closure patch
	XSLFI_SAFER_CROSSINGS,                        ///< Safer level crossings
	XSLFI_DEPARTURE_BOARDS,                       ///< Departure boards patch, in ticks mode
	XSLFI_TIMETABLES_START_TICKS,                 ///< Timetable start time is in ticks, instead of days (from departure boards patch)
	XSLFI_TOWN_CARGO_ADJ,                         ///< Town cargo adjustment patch
	XSLFI_SIG_TUNNEL_BRIDGE,                      ///< Signals on tunnels and bridges
	XSLFI_IMPROVED_BREAKDOWNS,                    ///< Improved breakdowns patch
	XSLFI_CONSIST_BREAKDOWN_FLAG,                 ///< Consist breakdown flag
	XSLFI_TT_WAIT_IN_DEPOT,                       ///< Timetabling waiting time in depot patch
	XSLFI_AUTO_TIMETABLE,                         ///< Auto timetables and separation patch
	XSLFI_VEHICLE_REPAIR_COST,                    ///< Vehicle repair costs patch
	XSLFI_ENH_VIEWPORT_PLANS,                     ///< Enhanced viewport patch: plans
	XSLFI_INFRA_SHARING,                          ///< Infrastructure sharing patch
	XSLFI_VARIABLE_DAY_LENGTH,                    ///< Variable day length patch
	XSLFI_ORDER_OCCUPANCY,                        ///< Running average of order occupancy
	XSLFI_MORE_COND_ORDERS,                       ///< More conditional orders patch
	XSLFI_EXTRA_LARGE_MAP,                        ///< Extra large map
	XSLFI_REVERSE_AT_WAYPOINT,                    ///< Reverse at waypoint orders
	XSLFI_VEH_LIFETIME_PROFIT,                    ///< Vehicle lifetime profit patch
	XSLFI_LINKGRAPH_DAY_SCALE,                    ///< Linkgraph job duration & interval may be in non-scaled days
	XSLFI_TEMPLATE_REPLACEMENT,                   ///< Template-based train replacement
	XSLFI_MORE_RAIL_TYPES,                        ///< Increased number of rail types
	XSLFI_CARGO_TYPE_ORDERS,                      ///< Cargo-specific load/unload order flags
	XSLFI_EXTENDED_GAMELOG,                       ///< Extended gamelog
	XSLFI_STATION_CATCHMENT_INC,                  ///< Station catchment radius increase
	XSLFI_CUSTOM_BRIDGE_HEADS,                    ///< Custom bridge heads
	XSLFI_CHUNNEL,                                ///< Tunnels under water (channel tunnel)
	XSLFI_SCHEDULED_DISPATCH,                     ///< Scheduled vehicle dispatching
	XSLFI_MORE_TOWN_GROWTH_RATES,                 ///< More town growth rates
	XSLFI_MULTIPLE_DOCKS,                         ///< Multiple docks
	XSLFI_TIMETABLE_EXTRA,                        ///< Vehicle timetable extra fields
	XSLFI_TRAIN_FLAGS_EXTRA,                      ///< Train flags field extra size
	XSLFI_VEHICLE_FLAGS_EXTRA,                    ///< Vehicle flags field extra size
	XSLFI_TRAIN_THROUGH_LOAD,                     ///< Train through load/unload
	XSLFI_ORDER_EXTRA_DATA,                       ///< Order extra data field(s)
	XSLFI_WHOLE_MAP_CHUNK,                        ///< Whole map chunk
	XSLFI_ST_LAST_VEH_TYPE,                       ///< Per-cargo station last vehicle type
	XSLFI_SELL_AT_DEPOT_ORDER,                    ///< Sell vehicle on arrival at depot orders
	XSLFI_BUY_LAND_RATE_LIMIT,                    ///< Buy land rate limit
	XSLFI_DUAL_RAIL_TYPES,                        ///< Two rail-types per tile
	XSLFI_CONSIST_SPEED_RD_FLAG,                  ///< Consist speed reduction flag
	XSLFI_SAVEGAME_UNIQUE_ID,                     ///< Savegame unique ID
	XSLFI_RV_OVERTAKING,                          ///< Roadvehicle overtaking
	XSLFI_LINKGRAPH_MODES,                        ///< Linkgraph additional distribution modes
	XSLFI_GAME_EVENTS,                            ///< Game event flags
	XSLFI_ROAD_LAYOUT_CHANGE_CTR,                 ///< Road layout change counter
	XSLFI_TOWN_CARGO_MATRIX,                      ///< Town cargo matrix savegame format changes (now obsolete)
	XSLFI_STATE_CHECKSUM,                         ///< State checksum
	XSLFI_DEBUG,                                  ///< Debugging info
	XSLFI_FLOW_STAT_FLAGS,                        ///< FlowStat flags
	XSLFI_SPEED_RESTRICTION,                      ///< Train speed restrictions
	XSLFI_STATION_GOODS_EXTRA,                    ///< Extra station goods entry statuses
	XSLFI_DOCKING_CACHE_VER,                      ///< Multiple docks - docking tile cache version
	XSLFI_EXTRA_CHEATS,                           ///< Extra cheats
	XSLFI_TOWN_MULTI_BUILDING,                    ///< Allow multiple stadium/church buildings in a single town
	XSLFI_SHIP_LOST_COUNTER,                      ///< Ship lost counter
	XSLFI_BUILD_OBJECT_RATE_LIMIT,                ///< Build object rate limit
	XSLFI_LOCAL_COMPANY,                          ///< Local company ID
	XSLFI_THROUGH_TRAIN_DEPOT,                    ///< Drive-through train depots
	XSLFI_MORE_VEHICLE_ORDERS,                    ///< More vehicle orders - VehicleOrderID is 16 bits instead of 8
	XSLFI_ORDER_FLAGS_EXTRA,                      ///< Order flags field extra size
	XSLFI_ONE_WAY_DT_ROAD_STOP,                   ///< One-way drive-through road stops
	XSLFI_ONE_WAY_ROAD_STATE,                     ///< One-way road state cache
	XSLFI_VENC_CHUNK,                             ///< VENC chunk
	XSLFI_ANIMATED_TILE_EXTRA,                    ///< Animated tile extra info
	XSLFI_NEWGRF_INFO_EXTRA,                      ///< Extra NewGRF info in savegame
	XSLFI_INDUSTRY_CARGO_ADJ,                     ///< Industry cargo adjustment patch
	XSLFI_REALISTIC_TRAIN_BRAKING,                ///< Realistic train braking
	XSLFI_INFLATION_FIXED_DATES,                  ///< Inflation is applied between fixed dates
	XSLFI_WATER_FLOODING,                         ///< Water flooding map bit
	XSLFI_MORE_HOUSES,                            ///< More house types
	XSLFI_CUSTOM_TOWN_ZONE,                       ///< Custom town zones
	XSLFI_STATION_CARGO_HISTORY,                  ///< Station waiting cargo history
	XSLFI_TRAIN_SPEED_ADAPTATION,                 ///< Train speed adaptation
	XSLFI_EXTRA_STATION_NAMES,                    ///< Extra station names
	XSLFI_DEPOT_ORDER_EXTRA_FLAGS,                ///< Depot order extra flags
	XSLFI_EXTRA_SIGNAL_TYPES,                     ///< Extra signal types
	XSLFI_BANKRUPTCY_EXTRA,                       ///< Extra company bankruptcy fields
	XSLFI_OBJECT_GROUND_TYPES,                    ///< Object ground types
	XSLFI_LINKGRAPH_AIRCRAFT,                     ///< Link graph last aircraft update field and aircraft link scaling setting
	XSLFI_COMPANY_PW,                             ///< Company passwords
	XSLFI_ST_INDUSTRY_CARGO_MODE,                 ///< Station industry cargo mode setting
	XSLFI_TL_SPEED_LIMIT,                         ///< Through load maximum speed setting
	XSLFI_RAIL_DEPOT_SPEED_LIMIT,                 ///< Rail depot maximum speed setting
	XSLFI_WAYPOINT_FLAGS,                         ///< Waypoint flags
	XSLFI_ROAD_WAYPOINTS,                         ///< Road waypoints
	XSLFI_MORE_STATION_TYPES,                     ///< More station types (field widening)
	XSLFI_RV_ORDER_EXTRA_FLAGS,                   ///< Road vehicle order extra flags
	XSLFI_GRF_ROADSTOPS,                          ///< NewGRF road stops
	XSLFI_INDUSTRY_ANIM_MASK,                     ///< Industry tile animation masking
	XSLFI_NEW_SIGNAL_STYLES,                      ///< New signal styles
	XSLFI_NO_TREE_COUNTER,                        ///< No tree counter
	XSLFI_TOWN_SETTING_OVERRIDE,                  ///< Town setting overrides
	XSLFI_LINKGRAPH_SPARSE_EDGES,                 ///< Link graph edge matrix is stored in sparse format, and saved in order
	XSLFI_AUX_TILE_LOOP,                          ///< Auxiliary tile loop
	XSLFI_NEWGRF_ENTITY_EXTRA,                    ///< NewGRF entity mappings are 16 bit
	XSLFI_TNNC_CHUNK,                             ///< TNNC chunk
	XSLFI_MULTI_CARGO_SHIPS,                      ///< Multi-cargo ships
	XSLFI_REMAIN_NEXT_ORDER_STATION,              ///< Remain in station if next order is for same station
	XSLFI_LABEL_ORDERS,                           ///< Label orders
	XSLFI_VARIABLE_TICK_RATE,                     ///< Variable tick rate
	XSLFI_ROAD_VEH_FLAGS,                         ///< Road vehicle flags
	XSLFI_STATION_TILE_CACHE_FLAGS,               ///< Station tile cache flags

	XSLFI_SCRIPT_INT64,                           ///< See: SLV_SCRIPT_INT64
	XSLFI_U64_TICK_COUNTER,                       ///< See: SLV_U64_TICK_COUNTER
	XSLFI_LINKGRAPH_TRAVEL_TIME,                  ///< See: SLV_LINKGRAPH_TRAVEL_TIME
	XSLFI_LAST_LOADING_TICK,                      ///< See: SLV_LAST_LOADING_TICK
	XSLFI_SCRIPT_LEAGUE_TABLES,                   ///< See: Scriptable league tables (PR #10001)
	XSLFI_VELOCITY_NAUTICAL,                      ///< See: SLV_VELOCITY_NAUTICAL (PR #10594)
	XSLFI_CONSISTENT_PARTIAL_Z,                   ///< See: SLV_CONSISTENT_PARTIAL_Z (PR #10570)
	XSLFI_MORE_CARGO_AGE,                         ///< See: SLV_MORE_CARGO_AGE (PR #10596)
	XSLFI_AI_START_DATE,                          ///< See: SLV_AI_START_DATE (PR #10653)
	XSLFI_EXTEND_VEHICLE_RANDOM,                  ///< See: SLV_EXTEND_VEHICLE_RANDOM (PR #10701)
	XSLFI_DISASTER_VEH_STATE,                     ///< See: SLV_DISASTER_VEH_STATE (PR #10798)
	XSLFI_SAVEGAME_ID,                            ///< See: SLV_SAVEGAME_ID (PR #10719)
	XSLFI_NEWGRF_LAST_SERVICE,                    ///< See: SLV_NEWGRF_LAST_SERVICE (PR #11124)

	XSLFI_RIFF_HEADER_60_BIT,                     ///< Size field in RIFF chunk header is 60 bit
	XSLFI_HEIGHT_8_BIT,                           ///< Map tile height is 8 bit instead of 4 bit, but savegame version may be before this became true in trunk
	XSLFI_ZPOS_32_BIT,                            ///< Vehicle/sign z_pos is 32 bit instead of 8 bit, but savegame version may be before this became true in trunk
	XSLFI_MIGHT_USE_PAX_SIGNALS,                  ///< This save game might use the pax-signals feature
	XSLFI_TRAFFIC_LIGHTS,                         ///< This save game uses road traffic lights
	XSLFI_RAIL_AGEING,                            ///< This save game uses the rail aging patch
	XSLFI_SPRINGPP,                               ///< This is a SpringPP game, use this for loading some settings
	XSLFI_JOKERPP,                                ///< This is a JokerPP game, use this for loading some settings
	XSLFI_CHILLPP,                                ///< This is a ChillPP game, use this for loading some settings

	XSLFI_SIZE,                                   ///< Total count of features, including null feature
};

extern std::array<uint16, XSLFI_SIZE> _sl_xv_feature_versions;
extern std::array<uint16, XSLFI_SIZE> _sl_xv_feature_static_versions;

/**
 * Operator to use when combining traditional savegame number test with an extended feature version test
 */
enum SlXvFeatureTestOperator {
	XSLFTO_OR                           = 0,      ///< Test if traditional savegame version is in bounds OR extended feature is in version bounds
	XSLFTO_AND                                    ///< Test if traditional savegame version is in bounds AND extended feature is in version bounds
};

/**
 * Structure to describe an extended feature version test, and how it combines with a traditional savegame version test
 */
struct SlXvFeatureTest {
	using TestFunctorPtr = bool (*)(uint16, bool, const std::array<uint16, XSLFI_SIZE> &);  ///< Return true if feature present, first parameter is standard savegame version, second is whether standard savegame version is within bounds

private:
	uint16 min_version;
	uint16 max_version;
	SlXvFeatureIndex feature;
	SlXvFeatureTestOperator op;
	TestFunctorPtr functor = nullptr;

public:
	SlXvFeatureTest()
			: min_version(0), max_version(0), feature(XSLFI_NULL), op(XSLFTO_OR) { }

	SlXvFeatureTest(SlXvFeatureTestOperator op_, SlXvFeatureIndex feature_, uint16 min_version_ = 1, uint16 max_version_ = 0xFFFF)
			: min_version(min_version_), max_version(max_version_), feature(feature_), op(op_) { }

	SlXvFeatureTest(TestFunctorPtr functor_)
			: min_version(0), max_version(0), feature(XSLFI_NULL), op(XSLFTO_OR), functor(functor_) { }

	bool IsFeaturePresent(const std::array<uint16, XSLFI_SIZE> &feature_versions, SaveLoadVersion savegame_version, SaveLoadVersion savegame_version_from, SaveLoadVersion savegame_version_to) const;

	inline bool IsFeaturePresent(SaveLoadVersion savegame_version, SaveLoadVersion savegame_version_from, SaveLoadVersion savegame_version_to) const
	{
		return this->IsFeaturePresent(_sl_xv_feature_versions, savegame_version, savegame_version_from, savegame_version_to);
	}
};

bool SlXvIsFeaturePresent(const std::array<uint16, XSLFI_SIZE> &feature_versions, SlXvFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF);

inline bool SlXvIsFeaturePresent(SlXvFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF)
{
	return SlXvIsFeaturePresent(_sl_xv_feature_versions, feature, min_version, max_version);
}

/**
 * Returns true if @p feature is missing (i.e. has a version of 0, or less than the specified minimum version)
 */
inline bool SlXvIsFeatureMissing(SlXvFeatureIndex feature, uint16 min_version = 1)
{
	return !SlXvIsFeaturePresent(feature, min_version);
}

/**
 * Returns true if @p feature is missing (i.e. has a version of 0, or less than the specified minimum version)
 */
inline bool SlXvIsFeatureMissing(const std::array<uint16, XSLFI_SIZE> &feature_versions, SlXvFeatureIndex feature, uint16 min_version = 1)
{
	return !SlXvIsFeaturePresent(feature_versions, feature, min_version);
}

const char *SlXvGetFeatureName(SlXvFeatureIndex feature);

/**
 * sub chunk flags, this is saved as-is
 * (XSCF_EXTRA_DATA_PRESENT and XSCF_CHUNK_ID_LIST_PRESENT must only be set by the save code, and read by the load code)
 */
enum SlxiSubChunkFlags {
	XSCF_NULL                     = 0,       ///< zero value
	XSCF_IGNORABLE_UNKNOWN        = 1 << 0,  ///< the loader is free to ignore this without aborting the load if it doesn't know what it is at all
	XSCF_IGNORABLE_VERSION        = 1 << 1,  ///< the loader is free to ignore this without aborting the load if the version is greater than the maximum that can be loaded
	XSCF_EXTRA_DATA_PRESENT       = 1 << 2,  ///< extra data field is present, extra data in some sub-chunk/feature specific format
	XSCF_CHUNK_ID_LIST_PRESENT    = 1 << 3,  ///< chunk ID list field is present, list of chunks which this sub-chunk/feature adds to the save game, this can be used to discard the chunks if the feature is unknown

	XSCF_IGNORABLE_ALL            = XSCF_IGNORABLE_UNKNOWN | XSCF_IGNORABLE_VERSION, ///< all "ignorable" flags
};
DECLARE_ENUM_AS_BIT_SET(SlxiSubChunkFlags)

struct SlxiSubChunkInfo;

typedef uint32 SlxiSubChunkSaveProc(const SlxiSubChunkInfo *info, bool dry_run);  ///< sub chunk save procedure type, must return length and write no data when dry_run is true
typedef void SlxiSubChunkLoadProc(const SlxiSubChunkInfo *info, uint32 length);   ///< sub chunk load procedure, must consume length bytes

/** Handlers and description of chunk. */
struct SlxiSubChunkInfo {
	SlXvFeatureIndex index;                       ///< feature index, this is saved
	SlxiSubChunkFlags flags;                      ///< flags, this is saved
	uint16 save_version;                          ///< version to save
	uint16 max_version;                           ///< maximum version to accept on load
	const char *name;                             ///< feature name, this *IS* saved, so must be globally unique
	SlxiSubChunkSaveProc *save_proc;              ///< save procedure of the sub chunk, this may be nullptr in which case no extra chunk data is saved
	SlxiSubChunkLoadProc *load_proc;              ///< load procedure of the sub chunk, this may be nullptr in which case the extra chunk data must be missing or of 0 length
	const char *chunk_list;                       ///< this is a list of chunks that this feature uses, which should be written to the savegame, this must be a comma-seperated list of 4-character IDs, with no spaces, or nullptr
};

void SlXvResetState();

void SlXvSetCurrentState();
void SlXvSetStaticCurrentVersions();

bool SlXvCheckSpecialSavegameVersions();

bool SlXvIsChunkDiscardable(uint32 id);

#endif /* SL_EXTENDED_VER_SL_H */
