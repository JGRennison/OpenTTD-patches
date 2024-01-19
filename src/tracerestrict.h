/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict.h Header file for Trace Restrict */

#ifndef TRACERESTRICT_H
#define TRACERESTRICT_H

#include "stdafx.h"
#include "core/bitmath_func.hpp"
#include "core/enum_type.hpp"
#include "core/pool_type.hpp"
#include "core/container_func.hpp"
#include "command_func.h"
#include "rail_map.h"
#include "tile_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "signal_type.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include <map>
#include <vector>

struct Train;

/** Program pool ID type. */
typedef uint32_t TraceRestrictProgramID;
struct TraceRestrictProgram;

/** Tile/track mapping type. */
typedef uint32_t TraceRestrictRefId;

/** Type of the pool for trace restrict programs. */
typedef Pool<TraceRestrictProgram, TraceRestrictProgramID, 16, 256000> TraceRestrictProgramPool;
/** The actual pool for trace restrict nodes. */
extern TraceRestrictProgramPool _tracerestrictprogram_pool;

/** Slot pool ID type. */
typedef uint16_t TraceRestrictSlotID;
struct TraceRestrictSlot;

/** Type of the pool for trace restrict slots. */
typedef Pool<TraceRestrictSlot, TraceRestrictSlotID, 16, 0xFFF0> TraceRestrictSlotPool;
/** The actual pool for trace restrict slots. */
extern TraceRestrictSlotPool _tracerestrictslot_pool;

static const TraceRestrictSlotID NEW_TRACE_RESTRICT_SLOT_ID = 0xFFFD;        // for GUI use only
static const TraceRestrictSlotID ALL_TRAINS_TRACE_RESTRICT_SLOT_ID = 0xFFFE; // for GUI use only
static const TraceRestrictSlotID INVALID_TRACE_RESTRICT_SLOT_ID = 0xFFFF;

/** Counter pool ID type. */
typedef uint16_t TraceRestrictCounterID;
struct TraceRestrictCounter;

/** Type of the pool for trace restrict counters. */
typedef Pool<TraceRestrictCounter, TraceRestrictCounterID, 16, 0xFFF0> TraceRestrictCounterPool;
/** The actual pool for trace restrict counters. */
extern TraceRestrictCounterPool _tracerestrictcounter_pool;

static const TraceRestrictCounterID NEW_TRACE_RESTRICT_COUNTER_ID = 0xFFFE;        // for GUI use only
static const TraceRestrictCounterID INVALID_TRACE_RESTRICT_COUNTER_ID = 0xFFFF;

extern const uint16_t _tracerestrict_pathfinder_penalty_preset_values[];

/** Type used for the TraceRestrictRefId -> TraceRestrictProgramID mapping */
struct TraceRestrictMappingItem {
	TraceRestrictProgramID program_id;

	TraceRestrictMappingItem() { }

	TraceRestrictMappingItem(TraceRestrictProgramID program_id_)
			: program_id(program_id_) { }
};

typedef btree::btree_map<TraceRestrictRefId, TraceRestrictMappingItem> TraceRestrictMapping;

/** The actual mapping from TraceRestrictRefId to TraceRestrictProgramID. */
extern TraceRestrictMapping _tracerestrictprogram_mapping;

void ClearTraceRestrictMapping();

/** Type of a single instruction, this is bit-packed as per TraceRestrictItemFlagAllocation */
typedef uint32_t TraceRestrictItem;

/**
 * Describes the allocation of bits to fields in TraceRestrictItem.
 * Of the fields below, the type seems the most likely
 * to need future expansion, hence the reserved bits are placed
 * immediately after them.
 *
 * COUNT values describe the field bit width
 * OFFSET values describe the field bit offset
 */
enum TraceRestrictItemFlagAllocation {
	TRIFA_TYPE_COUNT              = 6,
	TRIFA_TYPE_OFFSET             = 0,

	/* 2 bits reserved for future use */

	TRIFA_COND_FLAGS_COUNT        = 3,
	TRIFA_COND_FLAGS_OFFSET       = 8,

	TRIFA_AUX_FIELD_COUNT         = 2,
	TRIFA_AUX_FIELD_OFFSET        = 11,

	TRIFA_COND_OP_COUNT           = 3,
	TRIFA_COND_OP_OFFSET          = 13,

	TRIFA_VALUE_COUNT             = 16,
	TRIFA_VALUE_OFFSET            = 16,
};

/**
 * Enumeration of TraceRestrictItem type field.
 * This has a width of 6 bits (0 - 63).
 * This is split into three sections:
 * * non-conditionals: 0 <= type < TRIT_COND_BEGIN
 * * conditionals: TRIT_COND_BEGIN <= type < TRIT_COND_END
 * * non-conditionals: TRIT_COND_END <= type < max (63)
 *
 * This was previously 5 bits (0 - 31), which is why it's in three sections, not two.
 */
enum TraceRestrictItemType {
	TRIT_NULL                     = 0,    ///< Null-type, not in programs and not valid for execution, mainly used with TraceRestrictNullTypeSpecialValue for start/end
	TRIT_PF_DENY                  = 1,    ///< Pathfinder deny/allow
	TRIT_PF_PENALTY               = 2,    ///< Add to pathfinder penalty
	TRIT_RESERVE_THROUGH          = 3,    ///< Reserve through PBS signal
	TRIT_LONG_RESERVE             = 4,    ///< Long reserve PBS signal
	TRIT_WAIT_AT_PBS              = 5,    ///< Wait at PBS signal
	TRIT_SLOT                     = 6,    ///< Slot operation

	TRIT_COND_BEGIN               = 8,    ///< Start of conditional item types, note that this has the same value as TRIT_COND_ENDIF
	TRIT_COND_ENDIF               = 8,    ///< This is an endif block or an else block
	TRIT_COND_UNDEFINED           = 9,    ///< This condition has no type defined (evaluate as false)
	TRIT_COND_TRAIN_LENGTH        = 10,   ///< Test train length
	TRIT_COND_MAX_SPEED           = 11,   ///< Test train max speed
	TRIT_COND_CURRENT_ORDER       = 12,   ///< Test train current order (station, waypoint or depot)
	TRIT_COND_NEXT_ORDER          = 13,   ///< Test train next order (station, waypoint or depot)
	TRIT_COND_LAST_STATION        = 14,   ///< Test train last visited station
	TRIT_COND_CARGO               = 15,   ///< Test if train can carry cargo type
	TRIT_COND_ENTRY_DIRECTION     = 16,   ///< Test which side of signal/signal tile is being entered from
	TRIT_COND_PBS_ENTRY_SIGNAL    = 17,   ///< Test tile and PBS-state of previous signal
	TRIT_COND_TRAIN_GROUP         = 18,   ///< Test train group membership
	TRIT_COND_PHYS_PROP           = 19,   ///< Test train physical property
	TRIT_COND_PHYS_RATIO          = 20,   ///< Test train physical property ratio
	TRIT_COND_TRAIN_IN_SLOT       = 21,   ///< Test train slot membership
	TRIT_COND_SLOT_OCCUPANCY      = 22,   ///< Test train slot occupancy state
	TRIT_COND_TRAIN_OWNER         = 24,   ///< Test train owner
	TRIT_COND_TRAIN_STATUS        = 25,   ///< Test train status
	TRIT_COND_LOAD_PERCENT        = 26,   ///< Test train load percentage
	TRIT_COND_COUNTER_VALUE       = 27,   ///< Test counter value
	TRIT_COND_TIME_DATE_VALUE     = 28,   ///< Test time/date value
	TRIT_COND_RESERVED_TILES      = 29,   ///< Test reserved tiles ahead of train
	TRIT_COND_CATEGORY            = 30,   ///< Test train category
	TRIT_COND_TARGET_DIRECTION    = 31,   ///< Test direction of order target tile relative to this signal tile
	TRIT_COND_RESERVATION_THROUGH = 32,   ///< Test if train reservation passes through tile

	TRIT_COND_END                 = 48,   ///< End (exclusive) of conditional item types, note that this has the same value as TRIT_REVERSE
	TRIT_REVERSE                  = 48,   ///< Reverse behind signal
	TRIT_SPEED_RESTRICTION        = 49,   ///< Speed restriction
	TRIT_NEWS_CONTROL             = 50,   ///< News control
	TRIT_COUNTER                  = 51,   ///< Change counter value
	TRIT_PF_PENALTY_CONTROL       = 52,   ///< Control base signal penalties
	TRIT_SPEED_ADAPTATION_CONTROL = 53,   ///< Control speed adaptation
	TRIT_SIGNAL_MODE_CONTROL      = 54,   ///< Control signal modes

	/* space up to 63 */
};

/**
 * TraceRestrictItem condition flags field, only valid with conditional types (IsTraceRestrictTypeConditional() is true)
 */
enum TraceRestrictCondFlags {
	TRCF_DEFAULT                  = 0,       ///< indicates end if for type: TRIT_COND_ENDIF, if otherwise
	TRCF_ELSE                     = 1 << 0,  ///< indicates an else block for type: TRIT_COND_ENDIF, elif otherwise
	TRCF_OR                       = 1 << 1,  ///< indicates an orif block, not valid with type: TRIT_COND_ENDIF
	/* 1 bit spare */
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictCondFlags)

/**
 * Enumeration of TraceRestrictItemvalue type field when type is TRIT_NULL
 */
enum TraceRestrictNullTypeSpecialValue {
	TRNTSV_NULL                   = 0,       ///< null, what you get when you zero-init a TraceRestrictItemvalue
	TRNTSV_START                  = 1,       ///< start tag, generated within GUI
	TRNTSV_END                    = 2,       ///< end tag, generated within GUI
};

/**
 * Enumeration of TraceRestrictItemvalue type field when value type is TRVT_DIRECTION
 */
enum TraceRestrictDirectionTypeSpecialValue {
	TRNTSV_NE                     = 0,       ///< DIAGDIR_NE: entering at NE tile edge
	TRNTSV_SE                     = 1,       ///< DIAGDIR_SE: entering at SE tile edge
	TRNTSV_SW                     = 2,       ///< DIAGDIR_SW: entering at SW tile edge
	TRNTSV_NW                     = 3,       ///< DIAGDIR_NW: entering at NW tile edge
	TRDTSV_FRONT                  = 4,       ///< entering at front face of signal
	TRDTSV_BACK                   = 5,       ///< entering at rear face of signal
	TRDTSV_TUNBRIDGE_ENTER        = 32,      ///< signal is a tunnel/bridge entrance
	TRDTSV_TUNBRIDGE_EXIT         = 33,      ///< signal is a tunnel/bridge exit
};

/**
 * TraceRestrictItem condition operator field, only valid with conditional types (IsTraceRestrictTypeConditional() is true)
 */
enum TraceRestrictCondOp {
	TRCO_IS                       = 0,       ///< equality test, or can carry test for cargo
	TRCO_ISNOT                    = 1,       ///< inequality test, or can't carry test for cargo
	TRCO_LT                       = 2,       ///< less than test
	TRCO_LTE                      = 3,       ///< less than or equal test
	TRCO_GT                       = 4,       ///< greater than test
	TRCO_GTE                      = 5,       ///< greater than or equal test
	/* space up to 7 */
};

/**
 * TraceRestrictItem auxiliary type field, for order type conditionals
 */
enum TraceRestrictOrderCondAuxField {
	TROCAF_STATION                = 0,       ///< value field is a station StationID
	TROCAF_WAYPOINT               = 1,       ///< value field is a waypoint StationID
	TROCAF_DEPOT                  = 2,       ///< value field is a depot DepotID
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for physical property type conditionals
 */
enum TraceRestrictPhysPropCondAuxField {
	TRPPCAF_WEIGHT                = 0,       ///< value field is a weight
	TRPPCAF_POWER                 = 1,       ///< value field is a power
	TRPPCAF_MAX_TE                = 2,       ///< value field is a tractive effort
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for physical property ratio type conditionals
 */
enum TraceRestrictPhysPropRatioCondAuxField {
	TRPPRCAF_POWER_WEIGHT         = 0,       ///< value field is a 100 * power / weight ratio
	TRPPRCAF_MAX_TE_WEIGHT        = 1,       ///< value field is a 100 * tractive effort / weight ratio
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for category type conditionals
 */
enum TraceRestrictCatgeoryCondAuxField {
	TRCCAF_ENGINE_CLASS           = 0,       ///< value field is an EngineClass type
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_PF_PENALTY
 */
enum TraceRestrictPathfinderPenaltyAuxField {
	TRPPAF_VALUE                  = 0,       ///< value field is a the pathfinder penalty to use
	TRPPAF_PRESET                 = 1,       ///< value field is a pathfinder penalty prefix index: TraceRestrictPathfinderPenaltyPresetIndex
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_TARGET_DIRECTION
 */
enum TraceRestrictTargetDirectionCondAuxField {
	TRTDCAF_CURRENT_ORDER         = 0,       ///< Current order
	TRTDCAF_NEXT_ORDER            = 1,       ///< Next order
	/* space up to 3 */
};

/**
 * TraceRestrictItem value field, for TRIT_LONG_RESERVE
 */
enum TraceRestrictLongReserveValueField {
	TRLRVF_LONG_RESERVE                 = 0,     ///< Long reserve
	TRLRVF_CANCEL_LONG_RESERVE          = 1,     ///< Cancel long reserve
	TRLRVF_LONG_RESERVE_UNLESS_STOPPING = 2,     ///< Long reserve (unless passed stop)
};

/**
 * TraceRestrictItem value field, for TRIT_WAIT_AT_PBS
 */
enum TraceRestrictWaitAtPbsValueField {
	TRWAPVF_WAIT_AT_PBS                = 0,       ///< Wait at PBS
	TRWAPVF_CANCEL_WAIT_AT_PBS         = 1,       ///< Cancel wait at PBS
	TRWAPVF_PBS_RES_END_WAIT           = 2,       ///< PBS reservations ending at this signal wait
	TRWAPVF_CANCEL_PBS_RES_END_WAIT    = 3,       ///< Cancel PBS reservations ending at this signal wait
};

/**
 * TraceRestrictItem value field, for TRIT_REVERSE
 */
enum TraceRestrictReverseValueField {
	TRRVF_REVERSE                      = 0,       ///< Reverse
	TRRVF_CANCEL_REVERSE               = 1,       ///< Cancel reverse
};

/**
 * TraceRestrictItem value field, for TRIT_NEWS_CONTROL
 */
enum TraceRestrictNewsControlField {
	TRNCF_TRAIN_NOT_STUCK              = 0,       ///< Train is not stuck
	TRNCF_CANCEL_TRAIN_NOT_STUCK       = 1,       ///< Cancel train is not stuck
};

/**
 * TraceRestrictItem value field, for TRIT_PF_PENALTY_CONTROL
 */
enum TraceRestrictPfPenaltyControlField {
	TRPPCF_NO_PBS_BACK_PENALTY         = 0,       ///< Do not apply PBS signal back penalty
	TRPPCF_CANCEL_NO_PBS_BACK_PENALTY  = 1,       ///< Cancel do not apply PBS signal back penalty
};

/**
 * TraceRestrictItem value field, for TRIT_SPEED_ADAPTATION_CONTROL
 */
enum TraceRestrictSpeedAdaptationControlField {
	TRSACF_SPEED_ADAPT_EXEMPT          = 0,       ///< Make train exempt from speed adaptation
	TRSACF_REMOVE_SPEED_ADAPT_EXEMPT   = 1,       ///< Remove train exempt from speed adaptation
};

/**
 * TraceRestrictItem value field, for TRIT_SIGNAL_MODE_CONTROL
 */
enum TraceRestrictSignalModeControlField {
	TRSMCF_NORMAL_ASPECT               = 0,       ///< Combined normal/shunt aspect signals: use normal mode
	TRSMCF_SHUNT_ASPECT                = 1,       ///< Combined normal/shunt aspect signals: use shunt mode
};

/**
 * TraceRestrictItem value field, for TRIT_COND_TRAIN_STATUS
 */
enum TraceRestrictTrainStatusValueField {
	TRTSVF_EMPTY                       =  0,      ///< Train is empty
	TRTSVF_FULL                        =  1,      ///< Train is full
	TRTSVF_BROKEN_DOWN                 =  2,      ///< Train is broken down
	TRTSVF_NEEDS_REPAIR                =  3,      ///< Train needs repair
	TRTSVF_REVERSING                   =  4,      ///< Train is reversing
	TRTSVF_HEADING_TO_STATION_WAYPOINT =  5,      ///< Train is en-route to a station or waypoint
	TRTSVF_HEADING_TO_DEPOT            =  6,      ///< Train is en-route to a depot
	TRTSVF_LOADING                     =  7,      ///< Train is loading
	TRTSVF_WAITING                     =  8,      ///< Train is waiting
	TRTSVF_LOST                        =  9,      ///< Train is lost
	TRTSVF_REQUIRES_SERVICE            = 10,      ///< Train requires service
	TRTSVF_STOPPING_AT_STATION_WAYPOINT= 11,      ///< Train stops at destination station/waypoint
};

/**
 * TraceRestrictItem value field, for TRIT_COND_TIME_DATE_VALUE
 */
enum TraceRestrictTimeDateValueField {
	TRTDVF_MINUTE                 =  0,      ///< Minute
	TRTDVF_HOUR                   =  1,      ///< Hour
	TRTDVF_HOUR_MINUTE            =  2,      ///< Hour and minute
	TRTDVF_DAY                    =  3,      ///< Day
	TRTDVF_MONTH                  =  4,      ///< Month
	TRTDVF_END                    =  5,      ///< End tag
};

/**
 * TraceRestrictItem repurposed condition operator field, for slot operation type actions
 */
enum TraceRestrictSlotCondOpField {
	TRSCOF_ACQUIRE_WAIT           = 0,       ///< acquire a slot, or wait at the current signal
	TRSCOF_ACQUIRE_TRY            = 1,       ///< try to acquire a slot, or carry on otherwise
	TRSCOF_RELEASE_BACK           = 2,       ///< release a slot (back of train)
	TRSCOF_RELEASE_FRONT          = 3,       ///< release a slot (front of train)
	TRSCOF_PBS_RES_END_ACQ_WAIT   = 4,       ///< PBS reservations ending at this signal: acquire a slot, or wait
	TRSCOF_PBS_RES_END_ACQ_TRY    = 5,       ///< PBS reservations ending at this signal: acquire a slot, or carry on otherwise
	TRSCOF_PBS_RES_END_RELEASE    = 6,       ///< PBS reservations ending at this signal: release a slot
	TRSCOF_ACQUIRE_TRY_ON_RESERVE = 7,       ///< try to acquire a slot (on reserve), or carry on otherwise
	/* space up to 7 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_SLOT_OCCUPANCY
 */
enum TraceRestrictSlotOccupancyCondAuxField {
	TRSOCAF_OCCUPANTS             = 0,       ///< value field is the occupancy count of the slot
	TRSOCAF_REMAINING             = 1,       ///< value field is the remaining occupancy of the slot
	/* space up to 3 */
};

/**
 * TraceRestrictItem repurposed condition operator field, for counter operation type actions
 */
enum TraceRestrictCounterCondOpField {
	TRCCOF_INCREASE               = 0,       ///< increase counter by value
	TRCCOF_DECREASE               = 1,       ///< decrease counter by value
	TRCCOF_SET                    = 2,       ///< set counter to value
	/* space up to 7 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_PBS_ENTRY_SIGNAL
 */
enum TraceRestrictPBSEntrySignalAuxField {
	TRPESAF_VEH_POS               = 0,       ///< vehicle position signal
	TRPESAF_RES_END               = 1,       ///< reservation end signal
	TRPESAF_RES_END_TILE          = 2,       ///< reservation end tile
	/* space up to 3 */
};

/**
 * TraceRestrictItem pathfinder penalty preset index
 * This may not be shortened, only lengthened, as preset indexes are stored in save games
 */
enum TraceRestrictPathfinderPenaltyPresetIndex {
	TRPPPI_SMALL                  = 0,       ///< small preset value
	TRPPPI_MEDIUM                 = 1,       ///< medium preset value
	TRPPPI_LARGE                  = 2,       ///< large preset value
	TRPPPI_END,                              ///< end value
};

/**
 * Enumeration for TraceRestrictProgramResult::flags
 */
enum TraceRestrictProgramResultFlags {
	TRPRF_DENY                    = 1 << 0,  ///< Pathfinder deny is set
	TRPRF_RESERVE_THROUGH         = 1 << 1,  ///< Reserve through is set
	TRPRF_LONG_RESERVE            = 1 << 2,  ///< Long reserve is set
	TRPRF_WAIT_AT_PBS             = 1 << 3,  ///< Wait at PBS signal is set
	TRPRF_PBS_RES_END_WAIT        = 1 << 4,  ///< PBS reservations ending at this signal wait is set
	TRPRF_REVERSE                 = 1 << 5,  ///< Reverse behind signal
	TRPRF_SPEED_RESTRICTION_SET   = 1 << 6,  ///< Speed restriction field set
	TRPRF_TRAIN_NOT_STUCK         = 1 << 7,  ///< Train is not stuck
	TRPRF_NO_PBS_BACK_PENALTY     = 1 << 8,  ///< Do not apply PBS back penalty
	TRPRF_SPEED_ADAPT_EXEMPT      = 1 << 9,  ///< Make speed adaptation exempt
	TRPRF_RM_SPEED_ADAPT_EXEMPT   = 1 << 10, ///< Remove speed adaptation exemption
	TRPRF_SIGNAL_MODE_NORMAL      = 1 << 11, ///< Combined normal/shunt signal mode control: normal
	TRPRF_SIGNAL_MODE_SHUNT       = 1 << 12, ///< Combined normal/shunt signal mode control: shunt
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramResultFlags)

/**
 * Enumeration for TraceRestrictProgram::actions_used_flags
 */
enum TraceRestrictProgramActionsUsedFlags {
	TRPAUF_PF                     = 1 << 0,  ///< Pathfinder deny or penalty are present
	TRPAUF_RESERVE_THROUGH        = 1 << 1,  ///< Reserve through action is present
	TRPAUF_LONG_RESERVE           = 1 << 2,  ///< Long reserve action is present
	TRPAUF_WAIT_AT_PBS            = 1 << 3,  ///< Wait at PBS signal action is present
	TRPAUF_SLOT_ACQUIRE           = 1 << 4,  ///< Slot acquire action is present
	TRPAUF_SLOT_RELEASE_BACK      = 1 << 5,  ///< Slot release (back) action is present
	TRPAUF_SLOT_RELEASE_FRONT     = 1 << 6,  ///< Slot release (front) action is present
	TRPAUF_PBS_RES_END_WAIT       = 1 << 7,  ///< PBS reservations ending at this signal wait action is present
	TRPAUF_PBS_RES_END_SLOT       = 1 << 8,  ///< PBS reservations ending at this signal slot action is present
	TRPAUF_REVERSE                = 1 << 9,  ///< Reverse behind signal
	TRPAUF_SPEED_RESTRICTION      = 1 << 10, ///< Speed restriction
	TRPAUF_TRAIN_NOT_STUCK        = 1 << 11, ///< Train is not stuck
	TRPAUF_CHANGE_COUNTER         = 1 << 12, ///< Change counter value is present
	TRPAUF_NO_PBS_BACK_PENALTY    = 1 << 13, ///< No PBS back penalty is present
	TRPAUF_SLOT_ACQUIRE_ON_RES    = 1 << 14, ///< Slot acquire (on reserve) action is present
	TRPAUF_SPEED_ADAPTATION       = 1 << 15, ///< Speed adaptation control
	TRPAUF_PBS_RES_END_SIMULATE   = 1 << 16, ///< PBS reservations ending at this signal slot changes must be fully simulated in dry run mode
	TRPAUF_RESERVE_THROUGH_ALWAYS = 1 << 17, ///< Reserve through action is unconditionally set
	TRPAUF_CMB_SIGNAL_MODE_CTRL   = 1 << 18, ///< Combined normal/shunt signal mode control
	TRPAUF_ORDER_CONDITIONALS     = 1 << 19, ///< Order conditionals are present
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramActionsUsedFlags)

/**
 * Enumeration for TraceRestrictProgramInput::permitted_slot_operations
 */
enum TraceRestrictProgramInputSlotPermissions : uint8_t {
	TRPISP_ACQUIRE                = 1 << 0,  ///< Slot acquire is permitted
	TRPISP_RELEASE_BACK           = 1 << 1,  ///< Slot release (back) is permitted
	TRPISP_RELEASE_FRONT          = 1 << 2,  ///< Slot release (front) is permitted
	TRPISP_PBS_RES_END_ACQUIRE    = 1 << 3,  ///< Slot acquire (PBS reservations ending at this signal) is permitted
	TRPISP_PBS_RES_END_ACQ_DRY    = 1 << 4,  ///< Dry-run slot acquire (PBS reservations ending at this signal) is permitted
	TRPISP_PBS_RES_END_RELEASE    = 1 << 5,  ///< Slot release (PBS reservations ending at this signal) is permitted
	TRPISP_CHANGE_COUNTER         = 1 << 6,  ///< Change counter value is permitted
	TRPISP_ACQUIRE_ON_RES         = 1 << 7,  ///< Slot acquire (reserving route) is permitted
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramInputSlotPermissions)

/**
 * Enumeration for TraceRestrictProgramInput::input_flags
 */
enum TraceRestrictProgramInputFlags : uint8_t {
	TRPIF_PASSED_STOP             = 1 << 0,  ///< Train has passed stop
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramInputFlags)

/**
 * Execution input of a TraceRestrictProgram
 */
struct TraceRestrictProgramInput {
	typedef TileIndex PreviousSignalProc(const Train *v, const void *ptr, TraceRestrictPBSEntrySignalAuxField mode);

	TileIndex tile;                               ///< Tile of restrict signal, for direction testing
	Trackdir trackdir;                            ///< Track direction on tile of restrict signal, for direction testing
	PreviousSignalProc *previous_signal_callback; ///< Callback to retrieve tile and direction of previous signal, may be nullptr
	const void *previous_signal_ptr;              ///< Opaque pointer suitable to be passed to previous_signal_callback
	TraceRestrictProgramInputSlotPermissions permitted_slot_operations; ///< Permitted slot operations
	TraceRestrictProgramInputFlags input_flags;   ///< Input flags

	TraceRestrictProgramInput(TileIndex tile_, Trackdir trackdir_, PreviousSignalProc *previous_signal_callback_, const void *previous_signal_ptr_)
			: tile(tile_), trackdir(trackdir_), previous_signal_callback(previous_signal_callback_), previous_signal_ptr(previous_signal_ptr_),
			permitted_slot_operations(static_cast<TraceRestrictProgramInputSlotPermissions>(0)),
			input_flags(static_cast<TraceRestrictProgramInputFlags>(0)) { }
};

/**
 * Execution result of a TraceRestrictProgram
 */
struct TraceRestrictProgramResult {
	uint32_t penalty;                        ///< Total additional pathfinder penalty
	TraceRestrictProgramResultFlags flags;   ///< Flags of other actions to take
	uint16_t speed_restriction;              ///< Speed restriction to apply (if TRPRF_SPEED_RESTRICTION_SET flag present)

	TraceRestrictProgramResult()
			: penalty(0), flags(static_cast<TraceRestrictProgramResultFlags>(0)) { }
};

/**
 * Program type, this stores the instruction list
 * This is refcounted, see info at top of tracerestrict.cpp
 */
struct TraceRestrictProgram : TraceRestrictProgramPool::PoolItem<&_tracerestrictprogram_pool> {
	std::vector<TraceRestrictItem> items;
	uint32_t refcount;
	TraceRestrictProgramActionsUsedFlags actions_used_flags;

private:

	struct ptr_buffer {
		TraceRestrictRefId *buffer;
		uint32_t elem_capacity;
	};
	union refid_list_union {
		TraceRestrictRefId inline_ref_ids[4];
		ptr_buffer ptr_ref_ids;

		// Actual construction/destruction done by struct TraceRestrictProgram
		refid_list_union() {}
		~refid_list_union() {}
	};
	refid_list_union ref_ids;

	void ClearRefIds();

	inline TraceRestrictRefId *GetRefIdsPtr() { return this->refcount <= 4 ? this->ref_ids.inline_ref_ids : this->ref_ids.ptr_ref_ids.buffer; };

public:

	TraceRestrictProgram()
			: refcount(0), actions_used_flags(static_cast<TraceRestrictProgramActionsUsedFlags>(0)) { }

	~TraceRestrictProgram()
	{
		this->ClearRefIds();
	}

	void Execute(const Train *v, const TraceRestrictProgramInput &input, TraceRestrictProgramResult &out) const;

	inline const TraceRestrictRefId *GetRefIdsPtr() const { return const_cast<TraceRestrictProgram *>(this)->GetRefIdsPtr(); }

	void IncrementRefCount(TraceRestrictRefId ref_id);

	void DecrementRefCount(TraceRestrictRefId ref_id);

	static CommandCost Validate(const std::vector<TraceRestrictItem> &items, TraceRestrictProgramActionsUsedFlags &actions_used_flags);

	static size_t InstructionOffsetToArrayOffset(const std::vector<TraceRestrictItem> &items, size_t offset);

	static size_t ArrayOffsetToInstructionOffset(const std::vector<TraceRestrictItem> &items, size_t offset);

	/** Call InstructionOffsetToArrayOffset on current program instruction list */
	size_t InstructionOffsetToArrayOffset(size_t offset) const
	{
		return TraceRestrictProgram::InstructionOffsetToArrayOffset(this->items, offset);
	}

	/** Call ArrayOffsetToInstructionOffset on current program instruction list */
	size_t ArrayOffsetToInstructionOffset(size_t offset) const
	{
		return TraceRestrictProgram::ArrayOffsetToInstructionOffset(this->items, offset);
	}

	/** Get number of instructions in @p items */
	static size_t GetInstructionCount(const std::vector<TraceRestrictItem> &items)
	{
		return ArrayOffsetToInstructionOffset(items, items.size());
	}

	/** Call GetInstructionCount on current program instruction list */
	size_t GetInstructionCount() const
	{
		return TraceRestrictProgram::GetInstructionCount(this->items);
	}

	/** Get an iterator to the instruction at a given @p instruction_offset in @p items */
	static std::vector<TraceRestrictItem>::iterator InstructionAt(std::vector<TraceRestrictItem> &items, size_t instruction_offset)
	{
		return items.begin() + TraceRestrictProgram::InstructionOffsetToArrayOffset(items, instruction_offset);
	}

	/** Get a const_iterator to the instruction at a given @p instruction_offset in @p items */
	static std::vector<TraceRestrictItem>::const_iterator InstructionAt(const std::vector<TraceRestrictItem> &items, size_t instruction_offset)
	{
		return items.begin() + TraceRestrictProgram::InstructionOffsetToArrayOffset(items, instruction_offset);
	}

	/** Call validation function on current program instruction list and set actions_used_flags */
	CommandCost Validate()
	{
		return TraceRestrictProgram::Validate(items, actions_used_flags);
	}
};

/** Get TraceRestrictItem type field */
inline TraceRestrictItemType GetTraceRestrictType(TraceRestrictItem item)
{
	return static_cast<TraceRestrictItemType>(GB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT));
}

/** Get TraceRestrictItem condition flags field */
inline TraceRestrictCondFlags GetTraceRestrictCondFlags(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondFlags>(GB(item, TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT));
}

/** Get TraceRestrictItem condition operator field */
inline TraceRestrictCondOp GetTraceRestrictCondOp(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondOp>(GB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT));
}

/** Get TraceRestrictItem auxiliary field */
inline uint8_t GetTraceRestrictAuxField(TraceRestrictItem item)
{
	return GB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT);
}

/** Get TraceRestrictItem value field */
inline uint16_t GetTraceRestrictValue(TraceRestrictItem item)
{
	return static_cast<uint16_t>(GB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT));
}

/** Set TraceRestrictItem type field */
inline void SetTraceRestrictType(TraceRestrictItem &item, TraceRestrictItemType type)
{
	SB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT, type);
}

/** Set TraceRestrictItem condition operator field */
inline void SetTraceRestrictCondOp(TraceRestrictItem &item, TraceRestrictCondOp condop)
{
	SB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT, condop);
}

/** Set TraceRestrictItem condition flags field */
inline void SetTraceRestrictCondFlags(TraceRestrictItem &item, TraceRestrictCondFlags condflags)
{
	SB(item, TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT, condflags);
}

/** Set TraceRestrictItem auxiliary field */
inline void SetTraceRestrictAuxField(TraceRestrictItem &item, uint8_t data)
{
	SB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT, data);
}

/** Set TraceRestrictItem value field */
inline void SetTraceRestrictValue(TraceRestrictItem &item, uint16_t value)
{
	SB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT, value);
}

/** Is TraceRestrictItemType a conditional type? */
inline bool IsTraceRestrictTypeConditional(TraceRestrictItemType type)
{
	return type >= TRIT_COND_BEGIN && type < TRIT_COND_END;
}

/** Is TraceRestrictItem type field a conditional type? */
inline bool IsTraceRestrictConditional(TraceRestrictItem item)
{
	return IsTraceRestrictTypeConditional(GetTraceRestrictType(item));
}

/** Is TraceRestrictItem a double-item type? */
inline bool IsTraceRestrictDoubleItem(TraceRestrictItem item)
{
	const TraceRestrictItemType type = GetTraceRestrictType(item);
	return type == TRIT_COND_PBS_ENTRY_SIGNAL || type == TRIT_COND_SLOT_OCCUPANCY || type == TRIT_COUNTER ||
			type == TRIT_COND_COUNTER_VALUE || type == TRIT_COND_TIME_DATE_VALUE || type == TRIT_COND_RESERVATION_THROUGH;
}

/**
 * Categorisation of what is allowed in the TraceRestrictItem condition op field
 * see TraceRestrictTypePropertySet
 */
enum TraceRestrictConditionOpType {
	TRCOT_NONE                    = 0, ///< takes no condition op
	TRCOT_BINARY                  = 1, ///< takes "is" and "is not" condition ops
	TRCOT_ALL                     = 2, ///< takes all condition ops (i.e. all relational ops)
};

/**
 * Categorisation of what is in the TraceRestrictItem value field
 * see TraceRestrictTypePropertySet
 */
enum TraceRestrictValueType {
	TRVT_NONE                     = 0, ///< value field not used (set to 0)
	TRVT_SPECIAL                  = 1, ///< special handling of value field
	TRVT_INT                      = 2, ///< takes an unsigned integer value
	TRVT_DENY                     = 3, ///< takes a value 0 = deny, 1 = allow (cancel previous deny)
	TRVT_SPEED                    = 4, ///< takes an integer speed value
	TRVT_ORDER                    = 5, ///< takes an order target ID, as per the auxiliary field as type: TraceRestrictOrderCondAuxField
	TRVT_CARGO_ID                 = 6, ///< takes a CargoID
	TRVT_DIRECTION                = 7, ///< takes a TraceRestrictDirectionTypeSpecialValue
	TRVT_TILE_INDEX               = 8, ///< takes a TileIndex in the next item slot
	TRVT_PF_PENALTY               = 9, ///< takes a pathfinder penalty value or preset index, as per the auxiliary field as type: TraceRestrictPathfinderPenaltyAuxField
	TRVT_RESERVE_THROUGH          = 10,///< takes a value 0 = reserve through, 1 = cancel previous reserve through
	TRVT_LONG_RESERVE             = 11,///< takes a TraceRestrictLongReserveValueField
	TRVT_GROUP_INDEX              = 12,///< takes a GroupID
	TRVT_WEIGHT                   = 13,///< takes a weight
	TRVT_POWER                    = 14,///< takes a power
	TRVT_FORCE                    = 15,///< takes a force
	TRVT_POWER_WEIGHT_RATIO       = 16,///< takes a power / weight ratio, * 100
	TRVT_FORCE_WEIGHT_RATIO       = 17,///< takes a force / weight ratio, * 100
	TRVT_WAIT_AT_PBS              = 18,///< takes a TraceRestrictWaitAtPbsValueField value
	TRVT_SLOT_INDEX               = 19,///< takes a TraceRestrictSlotID
	TRVT_SLOT_INDEX_INT           = 20,///< takes a TraceRestrictSlotID, and an integer in the next item slot
	TRVT_PERCENT                  = 21,///> takes a unsigned integer percentage value between 0 and 100
	TRVT_OWNER                    = 40,///< takes a CompanyID
	TRVT_TRAIN_STATUS             = 41,///< takes a TraceRestrictTrainStatusValueField
	TRVT_REVERSE                  = 42,///< takes a TraceRestrictReverseValueField
	TRVT_NEWS_CONTROL             = 43,///< takes a TraceRestrictNewsControlField
	TRVT_COUNTER_INDEX_INT        = 44,///< takes a TraceRestrictCounterID, and an integer in the next item slot
	TRVT_TIME_DATE_INT            = 45,///< takes a TraceRestrictTimeDateValueField, and an integer in the next item slot
	TRVT_ENGINE_CLASS             = 46,///< takes a EngineClass
	TRVT_PF_PENALTY_CONTROL       = 47,///< takes a TraceRestrictPfPenaltyControlField
	TRVT_SPEED_ADAPTATION_CONTROL = 48,///< takes a TraceRestrictSpeedAdaptationControlField
	TRVT_SIGNAL_MODE_CONTROL      = 49,///< takes a TraceRestrictSignalModeControlField
	TRVT_ORDER_TARGET_DIAGDIR     = 50,///< takes a DiagDirection, and the order type in the auxiliary field
	TRVT_TILE_INDEX_THROUGH       = 51,///< takes a TileIndex in the next item slot (passes through)
};

/**
 * Describes formats of TraceRestrictItem condition op and value fields
 */
struct TraceRestrictTypePropertySet {
	TraceRestrictConditionOpType cond_type;
	TraceRestrictValueType value_type;
};

void SetTraceRestrictValueDefault(TraceRestrictItem &item, TraceRestrictValueType value_type);
void SetTraceRestrictTypeAndNormalise(TraceRestrictItem &item, TraceRestrictItemType type, uint8_t aux_data = 0);

/**
 * Get TraceRestrictTypePropertySet for a given instruction, only looks at value field
 */
inline TraceRestrictTypePropertySet GetTraceRestrictTypeProperties(TraceRestrictItem item)
{
	TraceRestrictTypePropertySet out;

	if (GetTraceRestrictType(item) == TRIT_NULL) {
		out.cond_type = TRCOT_NONE;
		out.value_type = TRVT_SPECIAL;
	} else if (GetTraceRestrictType(item) == TRIT_COND_ENDIF ||
			GetTraceRestrictType(item) == TRIT_COND_UNDEFINED) {
		out.cond_type = TRCOT_NONE;
		out.value_type = TRVT_NONE;
	} else if (IsTraceRestrictConditional(item)) {
		out.cond_type = TRCOT_ALL;

		switch (GetTraceRestrictType(item)) {
			case TRIT_COND_TRAIN_LENGTH:
				out.value_type = TRVT_INT;
				break;

			case TRIT_COND_MAX_SPEED:
				out.value_type = TRVT_SPEED;
				break;

			case TRIT_COND_CURRENT_ORDER:
			case TRIT_COND_NEXT_ORDER:
			case TRIT_COND_LAST_STATION:
				out.value_type = TRVT_ORDER;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_CARGO:
				out.value_type = TRVT_CARGO_ID;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_ENTRY_DIRECTION:
				out.value_type = TRVT_DIRECTION;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_PBS_ENTRY_SIGNAL:
				out.value_type = TRVT_TILE_INDEX;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_TRAIN_GROUP:
				out.value_type = TRVT_GROUP_INDEX;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_TRAIN_IN_SLOT:
				out.value_type = TRVT_SLOT_INDEX;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_SLOT_OCCUPANCY:
				out.value_type = TRVT_SLOT_INDEX_INT;
				break;

			case TRIT_COND_PHYS_PROP:
				switch (static_cast<TraceRestrictPhysPropCondAuxField>(GetTraceRestrictAuxField(item))) {
					case TRPPCAF_WEIGHT:
						out.value_type = TRVT_WEIGHT;
						break;

					case TRPPCAF_POWER:
						out.value_type = TRVT_POWER;
						break;

					case TRPPCAF_MAX_TE:
						out.value_type = TRVT_FORCE;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_COND_PHYS_RATIO:
				switch (static_cast<TraceRestrictPhysPropRatioCondAuxField>(GetTraceRestrictAuxField(item))) {
					case TRPPRCAF_POWER_WEIGHT:
						out.value_type = TRVT_POWER_WEIGHT_RATIO;
						break;

					case TRPPRCAF_MAX_TE_WEIGHT:
						out.value_type = TRVT_FORCE_WEIGHT_RATIO;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_COND_TRAIN_OWNER:
				out.value_type = TRVT_OWNER;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_TRAIN_STATUS:
				out.value_type = TRVT_TRAIN_STATUS;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_LOAD_PERCENT:
				out.value_type = TRVT_PERCENT;
				break;

			case TRIT_COND_COUNTER_VALUE:
				out.value_type = TRVT_COUNTER_INDEX_INT;
				break;

			case TRIT_COND_TIME_DATE_VALUE:
				out.value_type = TRVT_TIME_DATE_INT;
				break;

			case TRIT_COND_RESERVED_TILES:
				out.value_type = TRVT_INT;
				break;

			case TRIT_COND_CATEGORY:
				switch (static_cast<TraceRestrictCatgeoryCondAuxField>(GetTraceRestrictAuxField(item))) {
					case TRCCAF_ENGINE_CLASS:
						out.value_type = TRVT_ENGINE_CLASS;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_COND_TARGET_DIRECTION:
				out.value_type = TRVT_ORDER_TARGET_DIAGDIR;
				out.cond_type = TRCOT_BINARY;
				break;

			case TRIT_COND_RESERVATION_THROUGH:
				out.value_type = TRVT_TILE_INDEX_THROUGH;
				out.cond_type = TRCOT_BINARY;
				break;

			default:
				NOT_REACHED();
				break;
		}
	} else {
		out.cond_type = TRCOT_NONE;

		switch (GetTraceRestrictType(item)) {
			case TRIT_PF_PENALTY:
				out.value_type = TRVT_PF_PENALTY;
				break;

			case TRIT_PF_DENY:
				out.value_type = TRVT_DENY;
				break;

			case TRIT_RESERVE_THROUGH:
				out.value_type = TRVT_RESERVE_THROUGH;
				break;

			case TRIT_LONG_RESERVE:
				out.value_type = TRVT_LONG_RESERVE;
				break;

			case TRIT_WAIT_AT_PBS:
				out.value_type = TRVT_WAIT_AT_PBS;
				break;

			case TRIT_SLOT:
				out.value_type = TRVT_SLOT_INDEX;
				break;

			case TRIT_REVERSE:
				out.value_type = TRVT_REVERSE;
				break;

			case TRIT_SPEED_RESTRICTION:
				out.value_type = TRVT_SPEED;
				break;

			case TRIT_NEWS_CONTROL:
				out.value_type = TRVT_NEWS_CONTROL;
				break;

			case TRIT_COUNTER:
				out.value_type = TRVT_COUNTER_INDEX_INT;
				break;

			case TRIT_PF_PENALTY_CONTROL:
				out.value_type = TRVT_PF_PENALTY_CONTROL;
				break;

			case TRIT_SPEED_ADAPTATION_CONTROL:
				out.value_type = TRVT_SPEED_ADAPTATION_CONTROL;
				break;

			case TRIT_SIGNAL_MODE_CONTROL:
				out.value_type = TRVT_SIGNAL_MODE_CONTROL;
				break;

			default:
				out.value_type = TRVT_NONE;
				break;
		}
	}

	return out;
}

/** Is the aux field for this TraceRestrictItemType used as a subtype which changes the type of the value field? */
inline bool IsTraceRestrictTypeAuxSubtype(TraceRestrictItemType type)
{
	switch (type) {
		case TRIT_COND_PHYS_PROP:
		case TRIT_COND_PHYS_RATIO:
		case TRIT_COND_SLOT_OCCUPANCY:
		case TRIT_COND_PBS_ENTRY_SIGNAL:
		case TRIT_COND_CATEGORY:
			return true;

		default:
			return false;
	}
}

/** May this TraceRestrictItemType take a slot of a different (non-train) vehicle type */
inline bool IsTraceRestrictTypeNonMatchingVehicleTypeSlot(TraceRestrictItemType type)
{
	switch (type) {
		case TRIT_COND_SLOT_OCCUPANCY:
			return true;

		default:
			return false;
	}
}

/** Get mapping ref ID from tile and track */
inline TraceRestrictRefId MakeTraceRestrictRefId(TileIndex t, Track track)
{
	return (t << 3) | track;
}

/** Get tile from mapping ref ID */
inline TileIndex GetTraceRestrictRefIdTileIndex(TraceRestrictRefId ref)
{
	return static_cast<TileIndex>(ref >> 3);
}

/** Get track from mapping ref ID */
inline Track GetTraceRestrictRefIdTrack(TraceRestrictRefId ref)
{
	return static_cast<Track>(ref & 7);
}

void TraceRestrictSetIsSignalRestrictedBit(TileIndex t);
void TraceRestrictCreateProgramMapping(TraceRestrictRefId ref, TraceRestrictProgram *prog);
bool TraceRestrictRemoveProgramMapping(TraceRestrictRefId ref);

TraceRestrictProgram *GetTraceRestrictProgram(TraceRestrictRefId ref, bool create_new);

void TraceRestrictNotifySignalRemoval(TileIndex tile, Track track);

inline bool IsRestrictedSignalTile(TileIndex t)
{
	switch (GetTileType(t)) {
		case MP_RAILWAY:
			return IsRestrictedSignal(t);
		case MP_TUNNELBRIDGE:
			return IsTunnelBridgeRestrictedSignal(t);
		default:
			return false;
	}
}

/**
 * Gets the existing signal program for the tile identified by @p t and @p track, or nullptr
 */
inline const TraceRestrictProgram *GetExistingTraceRestrictProgram(TileIndex t, Track track)
{
	if (IsRestrictedSignalTile(t)) {
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(t, track), false);
	} else {
		return nullptr;
	}
}

/**
 * Enumeration for command action type field, indicates what command to do
 */
enum TraceRestrictDoCommandType {
	TRDCT_INSERT_ITEM,                       ///< insert new instruction before offset field as given value
	TRDCT_MODIFY_ITEM,                       ///< modify instruction at offset field to given value
	TRDCT_MODIFY_DUAL_ITEM,                  ///< modify second item of dual-part instruction at offset field to given value
	TRDCT_REMOVE_ITEM,                       ///< remove instruction at offset field
	TRDCT_SHALLOW_REMOVE_ITEM,               ///< shallow remove instruction at offset field, does not delete contents of block
	TRDCT_MOVE_ITEM,                         ///< move instruction or block at offset field
	TRDCT_DUPLICATE_ITEM,                    ///< duplicate instruction/block at offset field

	TRDCT_PROG_COPY,                         ///< copy program operation. Do not re-order this with respect to other values
	TRDCT_PROG_COPY_APPEND,                  ///< copy and append program operation
	TRDCT_PROG_SHARE,                        ///< share program operation
	TRDCT_PROG_SHARE_IF_UNMAPPED,            ///< share program operation (if unmapped)
	TRDCT_PROG_UNSHARE,                      ///< unshare program (copy as a new program)
	TRDCT_PROG_RESET,                        ///< reset program state of signal
};

void TraceRestrictDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32_t offset, uint32_t value, StringID error_msg);

void TraceRestrictProgMgmtWithSourceDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type,
		TileIndex source_tile, Track source_track, StringID error_msg);

/**
 * Short-hand to call TraceRestrictProgMgmtWithSourceDoCommandP with 0 for source tile/track
 */
inline void TraceRestrictProgMgmtDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, StringID error_msg)
{
	TraceRestrictProgMgmtWithSourceDoCommandP(tile, track, type, static_cast<TileIndex>(0), static_cast<Track>(0), error_msg);
}

CommandCost CmdProgramSignalTraceRestrict(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text);
CommandCost CmdProgramSignalTraceRestrictProgMgmt(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text);

CommandCost TraceRestrictProgramRemoveItemAt(std::vector<TraceRestrictItem> &items, uint32_t offset, bool shallow_mode);
CommandCost TraceRestrictProgramMoveItemAt(std::vector<TraceRestrictItem> &items, uint32_t &offset, bool up, bool shallow_mode);
CommandCost TraceRestrictProgramDuplicateItemAt(std::vector<TraceRestrictItem> &items, uint32_t offset);
bool TraceRestrictProgramDuplicateItemAtDryRun(const std::vector<TraceRestrictItem> &items, uint32_t offset);

void ShowTraceRestrictProgramWindow(TileIndex tile, Track track);

int GetTraceRestrictTimeDateValue(TraceRestrictTimeDateValueField type);
int GetTraceRestrictTimeDateValueFromDate(TraceRestrictTimeDateValueField type, DateTicksScaled scaled_date_ticks);

void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, uint16_t index);
void TraceRestrictRemoveGroupID(GroupID index);
void TraceRestrictUpdateCompanyID(CompanyID old_company, CompanyID new_company);
void TraceRestrictRemoveSlotID(TraceRestrictSlotID index);
void TraceRestrictRemoveCounterID(TraceRestrictCounterID index);

void TraceRestrictRemoveVehicleFromAllSlots(VehicleID id);
void TraceRestrictTransferVehicleOccupantInAllSlots(VehicleID from, VehicleID to);
void TraceRestrictGetVehicleSlots(VehicleID id, std::vector<TraceRestrictSlotID> &out);

static const uint MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS = 128; ///< The maximum length of a slot name in characters including '\0'

/**
 * Slot type, used for slot operations
 */
struct TraceRestrictSlot : TraceRestrictSlotPool::PoolItem<&_tracerestrictslot_pool> {
	std::vector<VehicleID> occupants;
	uint32_t max_occupancy = 1;
	std::string name;
	Owner owner;
	VehicleType vehicle_type;

	std::vector<SignalReference> progsig_dependants;

	static std::vector<TraceRestrictSlotID> veh_temporarily_added;
	static std::vector<TraceRestrictSlotID> veh_temporarily_removed;

	static void RebuildVehicleIndex();
	static bool ValidateVehicleIndex();
	static void ValidateSlotOccupants(std::function<void(const char *)> log);
	static void PreCleanPool();
	static void RevertTemporaryChanges(VehicleID veh);

	TraceRestrictSlot(CompanyID owner = INVALID_COMPANY, VehicleType type = VEH_TRAIN)
	{
		this->owner = owner;
		this->vehicle_type = type;
	}

	~TraceRestrictSlot()
	{
		if (!CleaningPool()) this->Clear();
	}

	/** Test whether vehicle ID is already an occupant */
	bool IsOccupant(VehicleID id) const {
		for (size_t i = 0; i < occupants.size(); i++) {
			if (occupants[i] == id) return true;
		}
		return false;
	}

	bool Occupy(VehicleID id, bool force = false);
	bool OccupyDryRun(VehicleID ids);
	bool OccupyDryRunUsingTemporaryState(VehicleID id);
	void Vacate(VehicleID id);
	void VacateUsingTemporaryState(VehicleID id);
	void Clear();
	void UpdateSignals();

private:
	void DeIndex(VehicleID id);
};

/**
 * Counter type
 */
struct TraceRestrictCounter : TraceRestrictCounterPool::PoolItem<&_tracerestrictcounter_pool> {
	int32_t value = 0;
	std::string name;
	Owner owner;

	std::vector<SignalReference> progsig_dependants;

	TraceRestrictCounter(CompanyID owner = INVALID_COMPANY)
	{
		this->owner = owner;
	}

	void UpdateValue(int32_t new_value);

	static int32_t ApplyValue(int32_t current, TraceRestrictCounterCondOpField op, int32_t value);

	void ApplyUpdate(TraceRestrictCounterCondOpField op, int32_t value)
	{
		this->UpdateValue(TraceRestrictCounter::ApplyValue(this->value, op, value));
	}
};

#endif /* TRACERESTRICT_H */
