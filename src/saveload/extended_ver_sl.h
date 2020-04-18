/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file extended_ver_sl.h Functions/types related to handling save/load extended version info. */

#ifndef EXTENDED_VER_SL_H
#define EXTENDED_VER_SL_H

#include "../core/bitmath_func.hpp"

#include <vector>

enum SaveLoadVersion : uint16;

/**
 * List of extended features, each feature has its own (16 bit) version
 */
enum SlXvFeatureIndex {
	XSLFI_NULL                          = 0,      ///< Unused value, to indicate that no extended feature test is in use
	XSLFI_TRACE_RESTRICT,                         ///< Trace restrict
	XSLFI_TRACE_RESTRICT_OWNER,                   ///< Trace restrict: train owner test
	XSLFI_TRACE_RESTRICT_ORDRCND,                 ///< Trace restrict: slot conditional order
	XSLFI_TRACE_RESTRICT_STATUSCND,               ///< Trace restrict: train status condition
	XSLFI_TRACE_RESTRICT_REVERSE,                 ///< Trace restrict: reverse
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
	XSLFI_TOWN_CARGO_MATRIX,                      ///< Town cargo matrix savegame format changes
	XSLFI_STATE_CHECKSUM,                         ///< State checksum
	XSLFI_DEBUG,                                  ///< Debugging info
	XSLFI_FLOW_STAT_FLAGS,                        ///< FlowStat flags
	XSLFI_SPEED_RESTRICTION,                      ///< Train speed restrictions
	XSLFI_STATION_GOODS_EXTRA,                    ///< Extra station goods entry statuses
	XSLFI_DOCKING_CACHE_VER,                      ///< Multiple docks - docking tile cache version
	XSLFI_EXTRA_CHEATS,                           ///< Extra cheats

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

extern uint16 _sl_xv_feature_versions[XSLFI_SIZE];

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
	using TestFunctorPtr = bool (*)(uint16, bool);  ///< Return true if feature present, first parameter is standard savegame version, second is whether standard savegame version is within bounds

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

	bool IsFeaturePresent(SaveLoadVersion savegame_version, SaveLoadVersion savegame_version_from, SaveLoadVersion savegame_version_to) const;
};

bool SlXvIsFeaturePresent(SlXvFeatureIndex feature, uint16 min_version = 1, uint16 max_version = 0xFFFF);

/**
 * Returns true if @p feature is missing (i.e. has a version of 0)
 */
inline bool SlXvIsFeatureMissing(SlXvFeatureIndex feature)
{
	return !SlXvIsFeaturePresent(feature);
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
	XSCF_EXTRA_DATA_PRESENT       = 1 << 2,  ///< extra data field is present, extra data in some sub-chunk/feature specific format, not used for anything yet
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

bool SlXvCheckSpecialSavegameVersions();

bool SlXvIsChunkDiscardable(uint32 id);

#endif /* EXTENDED_VER_SL_H */
