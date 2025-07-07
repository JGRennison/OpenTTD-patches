/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict.h Header file for Trace Restrict */

#ifndef TRACERESTRICT_H
#define TRACERESTRICT_H

#include "core/bitmath_func.hpp"
#include "core/enum_type.hpp"
#include "core/pool_type.hpp"
#include "core/container_func.hpp"
#include "core/strong_typedef_type.hpp"
#include "rail_map.h"
#include "tile_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "signal_type.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include "3rdparty/svector/svector.h"
#include <map>
#include <vector>

struct Train;
struct Window;

/** Program pool ID type. */
using TraceRestrictProgramID = PoolID<uint32_t, struct TraceRestrictProgramIDTag, 256000, 0xFFFFFFFF>;
struct TraceRestrictProgram;

/** Tile/track mapping type. */
typedef uint32_t TraceRestrictRefId;

/** Type of the pool for trace restrict programs. */
using TraceRestrictProgramPool = Pool<TraceRestrictProgram, TraceRestrictProgramID, 64, TraceRestrictProgramID::End().base()>;
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

static const uint32_t TRACE_RESTRICT_SLOT_DEFAULT_MAX_OCCUPANCY = 1;

/** Slot group pool ID type. */
typedef uint16_t TraceRestrictSlotGroupID;
struct TraceRestrictSlotGroup;

/** Type of the pool for trace restrict slot groups. */
typedef Pool<TraceRestrictSlotGroup, TraceRestrictSlotGroupID, 16, 0xFFF0> TraceRestrictSlotGroupPool;
/** The actual pool for trace restrict slot groups. */
extern TraceRestrictSlotGroupPool _tracerestrictslotgroup_pool;

static const TraceRestrictSlotGroupID NEW_TRACE_RESTRICT_SLOT_GROUP     = 0xFFFE; ///< Sentinel for a to-be-created group.
static const TraceRestrictSlotGroupID INVALID_TRACE_RESTRICT_SLOT_GROUP = 0xFFFF; ///< Sentinel for invalid slot groups. Ungrouped slots are in this group.

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

/**
 * Type of a single program item, as in std::vector<TraceRestrictProgramItem>.
 * Each instruction is formed of a TraceRestrictInstructionItem value, optionally followed by
 * a free-form item in the next slot (if TraceRestrictInstructionItem::IsDoubleItem() is true).
 */
struct TraceRestrictProgramItemTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare> {};
using TraceRestrictProgramItem = StrongType::Typedef<TraceRestrictProgramItemTag>;

/**
 * Describes the allocation of bits to fields in TraceRestrictInstructionItem.
 * Of the fields below, the type seems the most likely
 * to need future expansion, hence the reserved bits are placed
 * immediately after them.
 *
 * This only applies to the first item of dual-item instructions.
 *
 *   0                                       1                                       2                                       3
 *   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1   2   3   4   5   6   7   8   9   0   1
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * |          Type         | Free  | Cond  |Fr |  Aux  |    Cond   |                            Value                              |
 * |                       |       | Flags |ee |       |     Op    |                                                               |
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *                                                 |         |
 *                                               Combined wider field (TRIFA_CMB_AUX_COND)
 *
 * COUNT values describe the field bit width
 * OFFSET values describe the field bit offset
 */
enum TraceRestrictInstructionItemFlagAllocation {
	TRIFA_TYPE_COUNT              = 6,
	TRIFA_TYPE_OFFSET             = 0,

	/* 2 bits reserved for future use */

	TRIFA_COND_FLAGS_COUNT        = 2,
	TRIFA_COND_FLAGS_OFFSET       = 8,

	/* 1 bit reserved for future use */

	TRIFA_AUX_FIELD_COUNT         = 2,
	TRIFA_AUX_FIELD_OFFSET        = 11,

	TRIFA_COND_OP_COUNT           = 3,
	TRIFA_COND_OP_OFFSET          = 13,

	TRIFA_CMB_AUX_COND_COUNT      = 5, ///< This aliases the AUX_FIELD and COND_OP fields as a single wider field
	TRIFA_CMB_AUX_COND_OFFSET     = 11,

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
enum TraceRestrictItemType : uint8_t {
	TRIT_NULL                     = 0,    ///< Null-type, not in programs and not valid for execution, mainly used with TraceRestrictNullTypeSpecialValue for start/end
	TRIT_PF_DENY                  = 1,    ///< Pathfinder deny/allow
	TRIT_PF_PENALTY               = 2,    ///< Add to pathfinder penalty
	TRIT_RESERVE_THROUGH          = 3,    ///< Reserve through PBS signal
	TRIT_LONG_RESERVE             = 4,    ///< Long reserve PBS signal
	TRIT_WAIT_AT_PBS              = 5,    ///< Wait at PBS signal
	TRIT_SLOT                     = 6,    ///< Slot operation
	TRIT_GUI_LABEL                = 7,    ///< GUI label

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
	TRIT_COND_TRAIN_IN_SLOT_GROUP = 33,   ///< Test train slot membership

	TRIT_COND_END                 = 48,   ///< End (exclusive) of conditional item types, note that this has the same value as TRIT_REVERSE
	TRIT_REVERSE                  = 48,   ///< Reverse behind/at signal
	TRIT_SPEED_RESTRICTION        = 49,   ///< Speed restriction
	TRIT_NEWS_CONTROL             = 50,   ///< News control
	TRIT_COUNTER                  = 51,   ///< Change counter value
	TRIT_PF_PENALTY_CONTROL       = 52,   ///< Control base signal penalties
	TRIT_SPEED_ADAPTATION_CONTROL = 53,   ///< Control speed adaptation
	TRIT_SIGNAL_MODE_CONTROL      = 54,   ///< Control signal modes
	TRIT_SLOT_GROUP               = 55,   ///< Slot group operation

	/* space up to 63 */
};

/**
 * TraceRestrictItem condition flags field, only valid with conditional types (IsTraceRestrictTypeConditional() is true)
 */
enum TraceRestrictCondFlags : uint8_t {
	TRCF_DEFAULT                  = 0,       ///< indicates end if for type: TRIT_COND_ENDIF, if otherwise
	TRCF_ELSE                     = 1 << 0,  ///< indicates an else block for type: TRIT_COND_ENDIF, elif otherwise
	TRCF_OR                       = 1 << 1,  ///< indicates an orif block, not valid with type: TRIT_COND_ENDIF
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictCondFlags)

/**
 * Enumeration of TraceRestrictItemvalue type field when type is TRIT_NULL
 */
enum TraceRestrictNullTypeSpecialValue : uint8_t {
	TRNTSV_NULL                   = 0,       ///< null, what you get when you zero-init a TraceRestrictItemvalue
	TRNTSV_START                  = 1,       ///< start tag, generated within GUI
	TRNTSV_END                    = 2,       ///< end tag, generated within GUI
};

/**
 * Enumeration of TraceRestrictItemvalue type field when value type is TRVT_DIRECTION
 */
enum TraceRestrictDirectionTypeSpecialValue : uint8_t {
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
enum TraceRestrictCondOp : uint8_t {
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
enum TraceRestrictOrderCondAuxField : uint8_t {
	TROCAF_STATION                = 0,       ///< value field is a station StationID
	TROCAF_WAYPOINT               = 1,       ///< value field is a waypoint StationID
	TROCAF_DEPOT                  = 2,       ///< value field is a depot DepotID
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for physical property type conditionals
 */
enum TraceRestrictPhysPropCondAuxField : uint8_t {
	TRPPCAF_WEIGHT                = 0,       ///< value field is a weight
	TRPPCAF_POWER                 = 1,       ///< value field is a power
	TRPPCAF_MAX_TE                = 2,       ///< value field is a tractive effort
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for physical property ratio type conditionals
 */
enum TraceRestrictPhysPropRatioCondAuxField : uint8_t {
	TRPPRCAF_POWER_WEIGHT         = 0,       ///< value field is a 100 * power / weight ratio
	TRPPRCAF_MAX_TE_WEIGHT        = 1,       ///< value field is a 100 * tractive effort / weight ratio
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for category type conditionals
 */
enum TraceRestrictCatgeoryCondAuxField : uint8_t {
	TRCCAF_ENGINE_CLASS           = 0,       ///< value field is an EngineClass type
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_PF_PENALTY
 */
enum TraceRestrictPathfinderPenaltyAuxField : uint8_t {
	TRPPAF_VALUE                  = 0,       ///< value field is a the pathfinder penalty to use
	TRPPAF_PRESET                 = 1,       ///< value field is a pathfinder penalty prefix index: TraceRestrictPathfinderPenaltyPresetIndex
	/* space up to 3 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_TARGET_DIRECTION
 */
enum TraceRestrictTargetDirectionCondAuxField : uint8_t {
	TRTDCAF_CURRENT_ORDER         = 0,       ///< Current order
	TRTDCAF_NEXT_ORDER            = 1,       ///< Next order
	/* space up to 3 */
};

/**
 * TraceRestrictItem value field, for TRIT_LONG_RESERVE
 */
enum TraceRestrictLongReserveValueField : uint8_t {
	TRLRVF_LONG_RESERVE                 = 0,     ///< Long reserve
	TRLRVF_CANCEL_LONG_RESERVE          = 1,     ///< Cancel long reserve
	TRLRVF_LONG_RESERVE_UNLESS_STOPPING = 2,     ///< Long reserve (unless passed stop)
};

/**
 * TraceRestrictItem value field, for TRIT_WAIT_AT_PBS
 */
enum TraceRestrictWaitAtPbsValueField : uint8_t {
	TRWAPVF_WAIT_AT_PBS                = 0,       ///< Wait at PBS
	TRWAPVF_CANCEL_WAIT_AT_PBS         = 1,       ///< Cancel wait at PBS
	TRWAPVF_PBS_RES_END_WAIT           = 2,       ///< PBS reservations ending at this signal wait
	TRWAPVF_CANCEL_PBS_RES_END_WAIT    = 3,       ///< Cancel PBS reservations ending at this signal wait
};

/**
 * TraceRestrictItem value field, for TRIT_REVERSE
 */
enum TraceRestrictReverseValueField : uint8_t {
	TRRVF_REVERSE_BEHIND               = 0,       ///< Reverse behind signal
	TRRVF_CANCEL_REVERSE_BEHIND        = 1,       ///< Cancel reverse behind signal
	TRRVF_REVERSE_AT                   = 2,       ///< Reverse at PBS signal
	TRRVF_CANCEL_REVERSE_AT            = 3,       ///< Cancel reverse at PBS signal
};

/**
 * TraceRestrictItem value field, for TRIT_NEWS_CONTROL
 */
enum TraceRestrictNewsControlField : uint8_t {
	TRNCF_TRAIN_NOT_STUCK              = 0,       ///< Train is not stuck
	TRNCF_CANCEL_TRAIN_NOT_STUCK       = 1,       ///< Cancel train is not stuck
};

/**
 * TraceRestrictItem value field, for TRIT_PF_PENALTY_CONTROL
 */
enum TraceRestrictPfPenaltyControlField : uint8_t {
	TRPPCF_NO_PBS_BACK_PENALTY         = 0,       ///< Do not apply PBS signal back penalty
	TRPPCF_CANCEL_NO_PBS_BACK_PENALTY  = 1,       ///< Cancel do not apply PBS signal back penalty
};

/**
 * TraceRestrictItem value field, for TRIT_SPEED_ADAPTATION_CONTROL
 */
enum TraceRestrictSpeedAdaptationControlField : uint8_t {
	TRSACF_SPEED_ADAPT_EXEMPT          = 0,       ///< Make train exempt from speed adaptation
	TRSACF_REMOVE_SPEED_ADAPT_EXEMPT   = 1,       ///< Remove train exempt from speed adaptation
};

/**
 * TraceRestrictItem value field, for TRIT_SIGNAL_MODE_CONTROL
 */
enum TraceRestrictSignalModeControlField : uint8_t {
	TRSMCF_NORMAL_ASPECT               = 0,       ///< Combined normal/shunt aspect signals: use normal mode
	TRSMCF_SHUNT_ASPECT                = 1,       ///< Combined normal/shunt aspect signals: use shunt mode
};

/**
 * TraceRestrictItem value field, for TRIT_COND_TRAIN_STATUS
 */
enum TraceRestrictTrainStatusValueField : uint8_t {
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
enum TraceRestrictTimeDateValueField : uint8_t {
	TRTDVF_MINUTE                 =  0,      ///< Minute
	TRTDVF_HOUR                   =  1,      ///< Hour
	TRTDVF_HOUR_MINUTE            =  2,      ///< Hour and minute
	TRTDVF_DAY                    =  3,      ///< Day
	TRTDVF_MONTH                  =  4,      ///< Month
	TRTDVF_END                    =  5,      ///< End tag
};

/**
 * TraceRestrictItem subtype field, using the combined auxiliary and cond op bits, for slot operation type actions
 */
enum TraceRestrictSlotSubtypeField : uint8_t {
	TRSCOF_ACQUIRE_WAIT           = 0,       ///< acquire a slot, or wait at the current signal
	TRSCOF_ACQUIRE_TRY            = 1,       ///< try to acquire a slot, or carry on otherwise
	TRSCOF_RELEASE_BACK           = 2,       ///< release a slot (back of train)
	TRSCOF_RELEASE_FRONT          = 3,       ///< release a slot (front of train)
	TRSCOF_PBS_RES_END_ACQ_WAIT   = 4,       ///< PBS reservations ending at this signal: acquire a slot, or wait
	TRSCOF_PBS_RES_END_ACQ_TRY    = 5,       ///< PBS reservations ending at this signal: acquire a slot, or carry on otherwise
	TRSCOF_PBS_RES_END_RELEASE    = 6,       ///< PBS reservations ending at this signal: release a slot
	TRSCOF_RELEASE_ON_RESERVE     = 7,       ///< release a slot (on reserve)
	/* space up to 31 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_SLOT_OCCUPANCY
 */
enum TraceRestrictSlotOccupancyCondAuxField : uint8_t {
	TRSOCAF_OCCUPANTS             = 0,       ///< value field is the occupancy count of the slot
	TRSOCAF_REMAINING             = 1,       ///< value field is the remaining occupancy of the slot
	/* space up to 3 */
};

/**
 * TraceRestrictItem repurposed condition operator field, for counter operation type actions
 */
enum TraceRestrictCounterCondOpField : uint8_t {
	TRCCOF_INCREASE               = 0,       ///< increase counter by value
	TRCCOF_DECREASE               = 1,       ///< decrease counter by value
	TRCCOF_SET                    = 2,       ///< set counter to value
	/* space up to 7 */
};

/**
 * TraceRestrictItem auxiliary type field, for TRIT_COND_PBS_ENTRY_SIGNAL
 */
enum TraceRestrictPBSEntrySignalAuxField : uint8_t {
	TRPESAF_VEH_POS               = 0,       ///< vehicle position signal
	TRPESAF_RES_END               = 1,       ///< reservation end signal
	TRPESAF_RES_END_TILE          = 2,       ///< reservation end tile
	/* space up to 3 */
};

/**
 * TraceRestrictItem pathfinder penalty preset index
 * This may not be shortened, only lengthened, as preset indexes are stored in save games
 */
enum TraceRestrictPathfinderPenaltyPresetIndex : uint8_t {
	TRPPPI_SMALL                  = 0,       ///< small preset value
	TRPPPI_MEDIUM                 = 1,       ///< medium preset value
	TRPPPI_LARGE                  = 2,       ///< large preset value
	TRPPPI_END,                              ///< end value
};

/** Is TraceRestrictItemType a conditional type? */
inline bool IsTraceRestrictTypeConditional(TraceRestrictItemType type)
{
	return type >= TRIT_COND_BEGIN && type < TRIT_COND_END;
}

/** Is TraceRestrictItem a double-item type? */
inline bool IsTraceRestrictDoubleItemType(TraceRestrictItemType type)
{
	return type == TRIT_COND_PBS_ENTRY_SIGNAL || type == TRIT_COND_SLOT_OCCUPANCY || type == TRIT_COUNTER ||
			type == TRIT_COND_COUNTER_VALUE || type == TRIT_COND_TIME_DATE_VALUE || type == TRIT_COND_RESERVATION_THROUGH;
}

namespace TracerestrictDetail {
	/* Mixin for TraceRestrictInstructionItem */
	struct InstructionItemOperations {
		template <typename TType, typename TBaseType>
		struct mixin {
		private:
			TBaseType base() const { return static_cast<const TType &>(*this).base(); }
			TBaseType &edit_base() { return static_cast<TType &>(*this).edit_base(); }

		public:
			/** Get TraceRestrictItem type field */
			TraceRestrictItemType GetType() const
			{
				return static_cast<TraceRestrictItemType>(GB(this->base(), TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT));
			}

			/** Get TraceRestrictItem condition flags field */
			TraceRestrictCondFlags GetCondFlags() const
			{
				return static_cast<TraceRestrictCondFlags>(GB(this->base(), TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT));
			}

			/** Get condition operator field */
			inline TraceRestrictCondOp GetCondOp() const
			{
				return static_cast<TraceRestrictCondOp>(GB(this->base(), TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT));
			}

			/** Get auxiliary field */
			uint8_t GetAuxField() const
			{
				return GB(this->base(), TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT);
			}

			/** Get combined condition operator and auxiliary fields */
			uint8_t GetCombinedAuxCondOpField() const
			{
				return GB(this->base(), TRIFA_CMB_AUX_COND_OFFSET, TRIFA_CMB_AUX_COND_COUNT);
			}

			/** Get value field */
			uint16_t GetValue() const
			{
				return static_cast<uint16_t>(GB(this->base(), TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT));
			}

			/** Set type field */
			inline void SetType(TraceRestrictItemType type)
			{
				SB(this->edit_base(), TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT, type);
			}

			/** Set condition operator field */
			inline void SetCondOp(TraceRestrictCondOp condop)
			{
				SB(this->edit_base(), TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT, condop);
			}

			/** Set condition flags field */
			inline void SetCondFlags(TraceRestrictCondFlags condflags)
			{
				SB(this->edit_base(), TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT, condflags);
			}

			/** Set auxiliary field */
			inline void SetAuxField(uint8_t data)
			{
				SB(this->edit_base(), TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT, data);
			}

			/** Set combined condition operator and auxiliary fields */
			inline void SetCombinedAuxCondOpField(uint8_t data)
			{
				SB(this->edit_base(), TRIFA_CMB_AUX_COND_OFFSET, TRIFA_CMB_AUX_COND_COUNT, data);
			}

			/** Set value field */
			inline void SetValue(uint16_t value)
			{
				SB(this->edit_base(), TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT, value);
			}

			/** Is the type field a conditional type? */
			inline bool IsConditional() const
			{
				return IsTraceRestrictTypeConditional(this->GetType());
			}

			/** Is this a double-item type? */
			inline bool IsDoubleItem() const
			{
				return IsTraceRestrictDoubleItemType(this->GetType());
			}

			/** Get TraceRestrictProgramItem of this instruction item */
			inline TraceRestrictProgramItem AsProgramItem() const
			{
				return TraceRestrictProgramItem{this->base()};
			}
		};
	};
};

/**
 * Type of a single instruction item. Instructions are bit-packed as per TraceRestrictItemFlagAllocation.
 */
struct TraceRestrictInstructionItemTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare, TracerestrictDetail::InstructionItemOperations> {};
using TraceRestrictInstructionItem = StrongType::Typedef<TraceRestrictInstructionItemTag>;

/** Reference wrapper type for TraceRestrictInstructionItem */
using TraceRestrictInstructionItemRef = StrongType::BaseRefTypedef<TraceRestrictInstructionItemTag>;

template <typename ITER>
struct TraceRestrictInstructionEndSentinel {
private:
	ITER iter; // Underlying iterator type

public:
	constexpr TraceRestrictInstructionEndSentinel(ITER iter) : iter(iter) {}
	constexpr TraceRestrictInstructionEndSentinel(const TraceRestrictInstructionEndSentinel &other) = default;

	ITER ItemIter() const { return this->iter; }
};

template <typename ITER>
struct TraceRestrictInstructionIterator {
	using difference_type = std::ptrdiff_t;
	using value_type = TraceRestrictInstructionIterator;
	using reference = TraceRestrictInstructionIterator;
	using pointer = TraceRestrictInstructionIterator;
	using iterator_category = std::forward_iterator_tag;

private:
	ITER iter; // Underlying iterator type

	void Next()
	{
		if (this->Instruction().IsDoubleItem()) ++this->iter;
		++this->iter;
	}

public:
	constexpr TraceRestrictInstructionIterator(ITER iter) : iter(iter) {}
	constexpr TraceRestrictInstructionIterator(const TraceRestrictInstructionIterator &other) = default;

	bool operator==(const TraceRestrictInstructionIterator &other) const = default;
	bool operator<(const TraceRestrictInstructionIterator &other) const = default;
	bool operator==(const ITER &other) const { return this->iter == other; }
	bool operator<(const ITER &other) const { return this->iter < other; }

	/* For instruction iteration, use operator < on the underlying iterator instead of !=, this is to disallow skipping over the end in pre-validation saveload edits */
	bool operator!=(const TraceRestrictInstructionEndSentinel<ITER> &end) const { return this->iter < end.ItemIter(); }

	TraceRestrictInstructionItem Instruction() const { return TraceRestrictInstructionItem{this->iter->base()}; }
	TraceRestrictInstructionItemRef InstructionRef() const { return TraceRestrictInstructionItemRef{this->iter->edit_base()}; }

	uint32_t Secondary() const
	{
		dbg_assert(this->Instruction().IsDoubleItem());
		return (this->iter + 1)->base();
	}

	uint32_t &SecondaryRef() const
	{
		dbg_assert(this->Instruction().IsDoubleItem());
		return (this->iter + 1)->edit_base();
	}

	ITER ItemIter() const { return this->iter; }

	/* Increment operator (postfix) */
	TraceRestrictInstructionIterator operator ++(int)
	{
		TraceRestrictInstructionIterator tmp = *this;
		this->Next();
		return tmp;
	}

	/* Increment operator (prefix) */
	TraceRestrictInstructionIterator &operator ++()
	{
		this->Next();
		return *this;
	}

	/* For range for, don't automatically dereference, but return a const ref to prevent the iterator being modified */
	const TraceRestrictInstructionIterator &operator *() { return *this; }
};

template <typename C>
struct TraceRestrictInstructionIterateWrapper {
	C &container;

	constexpr TraceRestrictInstructionIterateWrapper(C &container) : container(container) {}

	auto begin() { return TraceRestrictInstructionIterator(this->container.begin()); }
	auto end() { return TraceRestrictInstructionEndSentinel(this->container.end()); }
};


size_t TraceRestrictInstructionOffsetToArrayOffset(const std::span<const TraceRestrictProgramItem> items, size_t offset);
size_t TraceRestrictArrayOffsetToInstructionOffset(const std::span<const TraceRestrictProgramItem> items, size_t offset);

/** Get number of instructions in @p items */
inline size_t TraceRestrictGetInstructionCount(const std::span<const TraceRestrictProgramItem> items)
{
	return TraceRestrictArrayOffsetToInstructionOffset(items, items.size());
}

/** Get an instruction iterator at a given @p instruction_offset in @p items */
template <typename T>
auto TraceRestrictInstructionIteratorAt(T &items, size_t instruction_offset)
{
	return TraceRestrictInstructionIterator(items.begin() + TraceRestrictInstructionOffsetToArrayOffset(items, instruction_offset));
}

/**
 * Type to hold a complete instruction, including the optional second item.
 * The second item is 0 if unused.
 * Not used for instruction storage.
 */
struct TraceRestrictInstructionRecord {
	TraceRestrictInstructionItem instruction = {};
	uint32_t secondary = {};
};

/**
 * Enumeration for TraceRestrictProgramResult::flags
 */
enum TraceRestrictProgramResultFlags : uint16_t {
	TRPRF_DENY                    = 1 << 0,  ///< Pathfinder deny is set
	TRPRF_RESERVE_THROUGH         = 1 << 1,  ///< Reserve through is set
	TRPRF_LONG_RESERVE            = 1 << 2,  ///< Long reserve is set
	TRPRF_WAIT_AT_PBS             = 1 << 3,  ///< Wait at PBS signal is set
	TRPRF_PBS_RES_END_WAIT        = 1 << 4,  ///< PBS reservations ending at this signal wait is set
	TRPRF_REVERSE_BEHIND          = 1 << 5,  ///< Reverse behind signal
	TRPRF_SPEED_RESTRICTION_SET   = 1 << 6,  ///< Speed restriction field set
	TRPRF_TRAIN_NOT_STUCK         = 1 << 7,  ///< Train is not stuck
	TRPRF_NO_PBS_BACK_PENALTY     = 1 << 8,  ///< Do not apply PBS back penalty
	TRPRF_SPEED_ADAPT_EXEMPT      = 1 << 9,  ///< Make speed adaptation exempt
	TRPRF_RM_SPEED_ADAPT_EXEMPT   = 1 << 10, ///< Remove speed adaptation exemption
	TRPRF_SIGNAL_MODE_NORMAL      = 1 << 11, ///< Combined normal/shunt signal mode control: normal
	TRPRF_SIGNAL_MODE_SHUNT       = 1 << 12, ///< Combined normal/shunt signal mode control: shunt
	TRPRF_REVERSE_AT              = 1 << 13, ///< Reverse at PBS signal is set
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramResultFlags)

/**
 * Enumeration for TraceRestrictProgram::actions_used_flags
 */
enum TraceRestrictProgramActionsUsedFlags : uint32_t {
	TRPAUF_NONE                   = 0,       ///< No flags set
	TRPAUF_PF                     = 1 << 0,  ///< Pathfinder deny or penalty are present
	TRPAUF_RESERVE_THROUGH        = 1 << 1,  ///< Reserve through action is present
	TRPAUF_LONG_RESERVE           = 1 << 2,  ///< Long reserve action is present
	TRPAUF_WAIT_AT_PBS            = 1 << 3,  ///< Wait at PBS signal action is present
	TRPAUF_SLOT_ACQUIRE           = 1 << 4,  ///< Slot acquire and/or release (on reserve) actions are present
	TRPAUF_SLOT_RELEASE_BACK      = 1 << 5,  ///< Slot release (back) action is present
	TRPAUF_SLOT_RELEASE_FRONT     = 1 << 6,  ///< Slot release (front) action is present
	TRPAUF_PBS_RES_END_WAIT       = 1 << 7,  ///< PBS reservations ending at this signal wait action is present
	TRPAUF_PBS_RES_END_SLOT       = 1 << 8,  ///< PBS reservations ending at this signal slot action is present
	TRPAUF_REVERSE_BEHIND         = 1 << 9,  ///< Reverse behind signal
	TRPAUF_SPEED_RESTRICTION      = 1 << 10, ///< Speed restriction
	TRPAUF_TRAIN_NOT_STUCK        = 1 << 11, ///< Train is not stuck
	TRPAUF_CHANGE_COUNTER         = 1 << 12, ///< Change counter value is present
	TRPAUF_NO_PBS_BACK_PENALTY    = 1 << 13, ///< No PBS back penalty is present
	TRPAUF_SLOT_CONDITIONALS      = 1 << 14, ///< Slot conditionals are present
	TRPAUF_SPEED_ADAPTATION       = 1 << 15, ///< Speed adaptation control
	TRPAUF_PBS_RES_END_SIMULATE   = 1 << 16, ///< PBS reservations ending at this signal slot changes must be fully simulated in dry run mode
	TRPAUF_RESERVE_THROUGH_ALWAYS = 1 << 17, ///< Reserve through action is unconditionally set
	TRPAUF_CMB_SIGNAL_MODE_CTRL   = 1 << 18, ///< Combined normal/shunt signal mode control
	TRPAUF_ORDER_CONDITIONALS     = 1 << 19, ///< Order conditionals are present
	TRPAUF_REVERSE_AT             = 1 << 20, ///< Reverse at signal
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramActionsUsedFlags)

static constexpr TraceRestrictProgramActionsUsedFlags TRPAUF_SPECIAL_ASPECT_PROPAGATION_FLAG_MASK = TRPAUF_WAIT_AT_PBS | TRPAUF_REVERSE_AT | TRPAUF_PBS_RES_END_WAIT | TRPAUF_RESERVE_THROUGH;

/**
 * Enumeration for TraceRestrictProgramInput::permitted_slot_operations
 */
enum TraceRestrictProgramInputSlotPermissions : uint8_t {
	TRPISP_NONE                   = 0,       ///< No permissions
	TRPISP_ACQUIRE                = 1 << 0,  ///< Slot acquire and release (on reserve) are permitted
	TRPISP_RELEASE_BACK           = 1 << 1,  ///< Slot release (back) is permitted
	TRPISP_RELEASE_FRONT          = 1 << 2,  ///< Slot release (front) is permitted
	TRPISP_PBS_RES_END_ACQUIRE    = 1 << 3,  ///< Slot acquire/release (PBS reservations ending at this signal) is permitted
	TRPISP_PBS_RES_END_ACQ_DRY    = 1 << 4,  ///< Dry-run slot acquire/release (PBS reservations ending at this signal) is permitted
	TRPISP_ACQUIRE_TEMP_STATE     = 1 << 5,  ///< Slot acquire/release is permitted, using temporary state, TraceRestrictSlotTemporaryState::change_stack must be non-empty
	TRPISP_CHANGE_COUNTER         = 1 << 6,  ///< Change counter value is permitted
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramInputSlotPermissions)

/**
 * Enumeration for TraceRestrictProgramInput::input_flags
 */
enum TraceRestrictProgramInputFlags : uint8_t {
	TRPIF_NONE                    = 0,       ///< No flags set
	TRPIF_PASSED_STOP             = 1 << 0,  ///< Train has passed stop
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramInputFlags)

struct TraceRestrictSlotTemporaryState {
	ankerl::svector<TraceRestrictSlotID, 8> veh_temporarily_added;
	ankerl::svector<TraceRestrictSlotID, 8> veh_temporarily_removed;

private:
	bool is_active = false;

	static std::vector<TraceRestrictSlotTemporaryState *> change_stack;

	void ApplyTemporaryChanges(const Vehicle *v);
	void ApplyTemporaryChangesToParent(VehicleID veh, TraceRestrictSlotTemporaryState *parent);

public:
	static TraceRestrictSlotTemporaryState *GetCurrent() { return change_stack.back(); }
	static std::span<const TraceRestrictSlotTemporaryState * const> GetChangeStack() { return change_stack; }

	static void ClearChangeStackApplyAllTemporaryChanges(const Vehicle *v)
	{
		while (!change_stack.empty()) {
			change_stack.back()->PopFromChangeStackApplyTemporaryChanges(v);
		}
	}

	bool IsActive() const { return this->is_active; }

	void RevertTemporaryChanges(VehicleID veh);

	void PushToChangeStack()
	{
		assert(!this->is_active);
		this->is_active = true;
		this->change_stack.push_back(this);
	}

	void PopFromChangeStackRevertTemporaryChanges(VehicleID veh)
	{
		assert(this->change_stack.back() == this);
		this->change_stack.pop_back();
		this->RevertTemporaryChanges(veh);
		this->is_active = false;
	}

	void PopFromChangeStackApplyTemporaryChanges(const Vehicle *v);

	bool IsEmpty() const
	{
		return this->veh_temporarily_added.empty() && this->veh_temporarily_removed.empty();
	}
};

/**
 * Execution input of a TraceRestrictProgram
 */
struct TraceRestrictProgramInput {
	typedef TileIndex PreviousSignalProc(const Train *v, const void *ptr, TraceRestrictPBSEntrySignalAuxField mode);

	TileIndex tile;                               ///< Tile of restrict signal, for direction testing
	Trackdir trackdir;                            ///< Track direction on tile of restrict signal, for direction testing
	TraceRestrictProgramInputFlags input_flags;   ///< Input flags
	TraceRestrictProgramInputSlotPermissions permitted_slot_operations; ///< Permitted slot operations
	PreviousSignalProc *previous_signal_callback; ///< Callback to retrieve tile and direction of previous signal, may be nullptr
	const void *previous_signal_ptr;              ///< Opaque pointer suitable to be passed to previous_signal_callback

	TraceRestrictProgramInput(TileIndex tile_, Trackdir trackdir_, PreviousSignalProc *previous_signal_callback_, const void *previous_signal_ptr_)
			: tile(tile_), trackdir(trackdir_), input_flags(TRPIF_NONE), permitted_slot_operations(TRPISP_NONE),
			previous_signal_callback(previous_signal_callback_), previous_signal_ptr(previous_signal_ptr_) { }
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

struct TraceRestrictProgramTexts {
	std::vector<std::string> labels;

	bool IsEmpty() const
	{
		return this->labels.empty();
	}
};

/**
 * Program type, this stores the instruction list
 * This is refcounted, see info at top of tracerestrict.cpp
 */
struct TraceRestrictProgram : TraceRestrictProgramPool::PoolItem<&_tracerestrictprogram_pool> {
private:
	ankerl::svector<TraceRestrictRefId, 3> references;

public:
	std::vector<TraceRestrictProgramItem> items;
	TraceRestrictProgramActionsUsedFlags actions_used_flags = TRPAUF_NONE;
	std::unique_ptr<TraceRestrictProgramTexts> texts;

	void Execute(const Train *v, const TraceRestrictProgramInput &input, TraceRestrictProgramResult &out) const;

	inline uint32_t GetReferenceCount() const { return static_cast<uint32_t>(this->references.size()); }

	inline std::span<const TraceRestrictRefId> GetReferences() const { return this->references; }

	/**
	 * We need an (empty) constructor so struct isn't zeroed (as C++ standard states)
	 */
	TraceRestrictProgram() { }

	/**
	 * (Empty) destructor has to be defined else operator delete might be called with nullptr parameter
	 */
	~TraceRestrictProgram() { }

	/**
	 * Increment ref count, only use when creating a mapping
	 */
	void IncrementRefCount(TraceRestrictRefId ref_id)
	{
		this->references.push_back(ref_id);
	}

	void DecrementRefCount(TraceRestrictRefId ref_id);

	static CommandCost Validate(const std::span<const TraceRestrictProgramItem> items, TraceRestrictProgramActionsUsedFlags &actions_used_flags);

	/** Get instruction count of current program instruction list */
	size_t GetInstructionCount() const
	{
		return TraceRestrictGetInstructionCount(this->items);
	}

	/** Get an instruction record at the given instruction offset, this reads the second item if suitable for the instruction type. */
	TraceRestrictInstructionRecord GetInstructionRecordAt(size_t instruction_offset) const
	{
		auto iter = TraceRestrictInstructionIteratorAt(this->items, instruction_offset);
		TraceRestrictInstructionRecord record{ iter.Instruction() };
		if (record.instruction.IsDoubleItem()) {
			dbg_assert(iter.ItemIter() + 1 < this->items.end());
			record.secondary = iter.Secondary();
		}
		return record;
	}

	/** Call validation function on current program instruction list and set actions_used_flags */
	CommandCost Validate()
	{
		return TraceRestrictProgram::Validate(this->items, this->actions_used_flags);
	}

	auto IterateInstructions() const { return TraceRestrictInstructionIterateWrapper(this->items); }
	auto IterateInstructionsMutable() { return TraceRestrictInstructionIterateWrapper(this->items); }

	uint16_t AddLabel(std::string_view str);
	void TrimLabels(const std::span<const TraceRestrictProgramItem> items);
	std::string_view GetLabel(uint16_t id) const;
};

/**
 * Categorisation of what is allowed in the TraceRestrictItem condition op field
 * see TraceRestrictTypePropertySet
 */
enum TraceRestrictConditionOpType : uint8_t {
	TRCOT_NONE                    = 0, ///< takes no condition op
	TRCOT_BINARY                  = 1, ///< takes "is" and "is not" condition ops
	TRCOT_ALL                     = 2, ///< takes all condition ops (i.e. all relational ops)
};

/**
 * Categorisation of what is in the TraceRestrictItem value field
 * see TraceRestrictTypePropertySet
 */
enum TraceRestrictValueType : uint8_t {
	TRVT_NONE,                     ///< value field not used (set to 0)
	TRVT_SPECIAL,                  ///< special handling of value field
	TRVT_INT,                      ///< takes an unsigned integer value
	TRVT_DENY,                     ///< takes a value 0 = deny, 1 = allow (cancel previous deny)
	TRVT_SPEED,                    ///< takes an integer speed value
	TRVT_ORDER,                    ///< takes an order target ID, as per the auxiliary field as type: TraceRestrictOrderCondAuxField
	TRVT_CARGO_ID,                 ///< takes a CargoType
	TRVT_DIRECTION,                ///< takes a TraceRestrictDirectionTypeSpecialValue
	TRVT_TILE_INDEX,               ///< takes a TileIndex in the next item slot
	TRVT_PF_PENALTY,               ///< takes a pathfinder penalty value or preset index, as per the auxiliary field as type: TraceRestrictPathfinderPenaltyAuxField
	TRVT_RESERVE_THROUGH,          ///< takes a value 0 = reserve through, 1 = cancel previous reserve through
	TRVT_LONG_RESERVE,             ///< takes a TraceRestrictLongReserveValueField
	TRVT_GROUP_INDEX,              ///< takes a GroupID
	TRVT_WEIGHT,                   ///< takes a weight
	TRVT_POWER,                    ///< takes a power
	TRVT_FORCE,                    ///< takes a force
	TRVT_POWER_WEIGHT_RATIO,       ///< takes a power / weight ratio, * 100
	TRVT_FORCE_WEIGHT_RATIO,       ///< takes a force / weight ratio, * 100
	TRVT_WAIT_AT_PBS,              ///< takes a TraceRestrictWaitAtPbsValueField value
	TRVT_SLOT_INDEX,               ///< takes a TraceRestrictSlotID
	TRVT_SLOT_INDEX_INT,           ///< takes a TraceRestrictSlotID, and an integer in the next item slot
	TRVT_SLOT_GROUP_INDEX,         ///< takes a TraceRestrictSlotGroupID
	TRVT_PERCENT,                  ///> takes a unsigned integer percentage value between 0 and 100
	TRVT_OWNER,                    ///< takes a CompanyID
	TRVT_TRAIN_STATUS,             ///< takes a TraceRestrictTrainStatusValueField
	TRVT_REVERSE,                  ///< takes a TraceRestrictReverseValueField
	TRVT_NEWS_CONTROL,             ///< takes a TraceRestrictNewsControlField
	TRVT_COUNTER_INDEX_INT,        ///< takes a TraceRestrictCounterID, and an integer in the next item slot
	TRVT_TIME_DATE_INT,            ///< takes a TraceRestrictTimeDateValueField, and an integer in the next item slot
	TRVT_ENGINE_CLASS,             ///< takes a EngineClass
	TRVT_PF_PENALTY_CONTROL,       ///< takes a TraceRestrictPfPenaltyControlField
	TRVT_SPEED_ADAPTATION_CONTROL, ///< takes a TraceRestrictSpeedAdaptationControlField
	TRVT_SIGNAL_MODE_CONTROL,      ///< takes a TraceRestrictSignalModeControlField
	TRVT_ORDER_TARGET_DIAGDIR,     ///< takes a DiagDirection, and the order type in the auxiliary field
	TRVT_TILE_INDEX_THROUGH,       ///< takes a TileIndex in the next item slot (passes through)
	TRVT_LABEL_INDEX,              ///< takes a label ID
};

/**
 * Describes formats of TraceRestrictItem condition op and value fields
 */
struct TraceRestrictTypePropertySet {
	TraceRestrictConditionOpType cond_type;
	TraceRestrictValueType value_type;
};

void SetTraceRestrictValueDefault(TraceRestrictInstructionItemRef item, TraceRestrictValueType value_type);
void SetTraceRestrictTypeAndNormalise(TraceRestrictInstructionItemRef item, TraceRestrictItemType type, uint8_t aux_data = 0);

/**
 * Get TraceRestrictTypePropertySet for a given instruction, using the instruction type and where appropriate the auxiliary type field
 */
inline TraceRestrictTypePropertySet GetTraceRestrictTypeProperties(TraceRestrictInstructionItem item)
{
	TraceRestrictTypePropertySet out;

	if (item.GetType() == TRIT_NULL) {
		out.cond_type = TRCOT_NONE;
		out.value_type = TRVT_SPECIAL;
	} else if (item.GetType() == TRIT_COND_ENDIF ||
			item.GetType() == TRIT_COND_UNDEFINED) {
		out.cond_type = TRCOT_NONE;
		out.value_type = TRVT_NONE;
	} else if (item.IsConditional()) {
		out.cond_type = TRCOT_ALL;

		switch (item.GetType()) {
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
				switch (static_cast<TraceRestrictPhysPropCondAuxField>(item.GetAuxField())) {
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
				switch (static_cast<TraceRestrictPhysPropRatioCondAuxField>(item.GetAuxField())) {
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
				switch (static_cast<TraceRestrictCatgeoryCondAuxField>(item.GetAuxField())) {
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

			case TRIT_COND_TRAIN_IN_SLOT_GROUP:
				out.value_type = TRVT_SLOT_GROUP_INDEX;
				out.cond_type = TRCOT_BINARY;
				break;

			default:
				NOT_REACHED();
				break;
		}
	} else {
		out.cond_type = TRCOT_NONE;

		switch (item.GetType()) {
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

			case TRIT_GUI_LABEL:
				out.value_type = TRVT_LABEL_INDEX;
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

			case TRIT_SLOT_GROUP:
				out.value_type = TRVT_SLOT_GROUP_INDEX;
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
	return (t.base() << 3) | track;
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

CommandCost TraceRestrictProgramRemoveItemAt(std::vector<TraceRestrictProgramItem> &items, uint32_t offset, bool shallow_mode);
CommandCost TraceRestrictProgramMoveItemAt(std::vector<TraceRestrictProgramItem> &items, uint32_t &offset, bool up, bool shallow_mode);
CommandCost TraceRestrictProgramDuplicateItemAt(std::vector<TraceRestrictProgramItem> &items, uint32_t offset);
bool TraceRestrictProgramDuplicateItemAtDryRun(const std::vector<TraceRestrictProgramItem> &items, uint32_t offset);

void ShowTraceRestrictProgramWindow(TileIndex tile, Track track);

int GetTraceRestrictTimeDateValue(TraceRestrictTimeDateValueField type);
int GetTraceRestrictTimeDateValueFromStateTicks(TraceRestrictTimeDateValueField type, StateTicks state_ticks);

void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, struct DestinationID index);
void TraceRestrictRemoveGroupID(GroupID index);
void TraceRestrictUpdateCompanyID(CompanyID old_company, CompanyID new_company);
void TraceRestrictRemoveSlotID(TraceRestrictSlotID index);
void TraceRestrictRemoveSlotGroupID(TraceRestrictSlotGroupID index);
void TraceRestrictRemoveCounterID(TraceRestrictCounterID index);
void TraceRestrictRemoveNonOwnedReferencesFromInstructionRange(std::span<TraceRestrictProgramItem> instructions, Owner instructions_owner);
void TraceRestrictRemoveNonOwnedReferencesFromOrder(struct Order *o, Owner order_owner);

void TraceRestrictRemoveVehicleFromAllSlots(VehicleID id);
void TraceRestrictTransferVehicleOccupantInAllSlots(VehicleID from, VehicleID to);
void TraceRestrictGetVehicleSlots(VehicleID id, std::vector<TraceRestrictSlotID> &out);
void TraceRestrictVacateSlotGroup(const TraceRestrictSlotGroup *sg, Owner owner, const Vehicle *v);
bool TraceRestrictIsVehicleInSlotGroup(const TraceRestrictSlotGroup *sg, Owner owner, const Vehicle *v);

void TraceRestrictRecordRecentSlot(TraceRestrictSlotID index);
void TraceRestrictRecordRecentSlotGroup(TraceRestrictSlotGroupID index);
void TraceRestrictRecordRecentCounter(TraceRestrictCounterID index);
void TraceRestrictClearRecentSlotsAndCounters();

StringID TraceRestrictPrepareSlotCounterSelectTooltip(StringID base_str, VehicleType vtype);

static const uint MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS = 128; ///< The maximum length of a slot name in characters including '\0'

/**
 * Slot type, used for slot operations
 */
struct TraceRestrictSlot : TraceRestrictSlotPool::PoolItem<&_tracerestrictslot_pool> {
	friend TraceRestrictSlotTemporaryState;

	enum class Flag : uint8_t {
		Public, ///< Public slot.
	};
	using Flags = EnumBitSet<Flag, uint8_t>;

	Owner owner;
	Flags flags{};
	VehicleType vehicle_type;
	TraceRestrictSlotGroupID parent_group = INVALID_TRACE_RESTRICT_SLOT_GROUP;
	uint32_t max_occupancy = 1;
	std::string name;
	ankerl::svector<VehicleID, 3> occupants;
	ankerl::svector<SignalReference, 0> progsig_dependants;

	static void RebuildVehicleIndex();
	static bool ValidateVehicleIndex();
	static void ValidateSlotOccupants(std::function<void(std::string_view)> log);
	static void ValidateSlotGroupDescendants(std::function<void(std::string_view)> log);
	static void PreCleanPool();

	TraceRestrictSlot(CompanyID owner = INVALID_COMPANY, VehicleType type = VEH_TRAIN) : owner(owner), vehicle_type(type) {}

	~TraceRestrictSlot()
	{
		if (!CleaningPool()) {
			this->Clear();
			this->RemoveFromParentGroups();
		}
	}

	/** Test whether vehicle ID is already an occupant */
	bool IsOccupant(VehicleID id) const
	{
		for (size_t i = 0; i < this->occupants.size(); i++) {
			if (this->occupants[i] == id) return true;
		}
		return false;
	}

	inline bool IsUsableByOwner(Owner using_owner) const
	{
		return this->owner == using_owner || this->flags.Test(Flag::Public);
	}

	bool Occupy(const Vehicle *v, bool force = false);
	bool OccupyDryRun(VehicleID ids);
	bool OccupyUsingTemporaryState(VehicleID id, TraceRestrictSlotTemporaryState *state);
	void Vacate(const Vehicle *v);
	void VacateUsingTemporaryState(VehicleID id, TraceRestrictSlotTemporaryState *state);
	void Clear();
	void UpdateSignals();
	void AddToParentGroups();
	void RemoveFromParentGroups();

private:
	void AddIndex(const Vehicle *v);
	void DeIndex(VehicleID id, const Vehicle *v);
};


struct TraceRestrictVehicleTemporarySlotMembershipState {
private:
	ankerl::svector<TraceRestrictSlotID, 8> vehicle_slots;
	ankerl::svector<TraceRestrictSlotID, 8> current_slots;
	const Vehicle *vehicle = nullptr;

	void InitialiseFromVehicle(const Vehicle *v);

public:
	bool IsValid() const { return this->vehicle != nullptr; }

	bool IsInSlot(TraceRestrictSlotID slot_id) const
	{
		for (TraceRestrictSlotID s : this->current_slots) {
			if (s == slot_id) return true;
		}
		return false;
	}

	void AddSlot(TraceRestrictSlotID slot_id);
	void RemoveSlot(TraceRestrictSlotID slot_id);
	int GetSlotOccupancyDelta(TraceRestrictSlotID slot_id);
	void ApplyToVehicle();

	void Initialise(const Vehicle *v)
	{
		if (!this->IsValid()) this->InitialiseFromVehicle(v);
	}

	void Clear()
	{
		this->current_slots.clear();
		this->vehicle = nullptr;
	}
};

/**
 * Slot group type
 */
struct TraceRestrictSlotGroup : TraceRestrictSlotGroupPool::PoolItem<&_tracerestrictslotgroup_pool> {
	std::string name;           ///< Slot group Name
	Owner owner;                ///< Slot group owner
	VehicleType vehicle_type;   ///< Vehicle type of the slot group
	TraceRestrictSlotGroupID parent; ///< Parent slot group

	ankerl::svector<TraceRestrictSlotID, 8> contained_slots; ///< NOSAVE: slots directly and indirectly contained in this slot group, sorted
	bool folded = false;        ///< NOSAVE: Is this slot group folded in the slot view?

	TraceRestrictSlotGroup(CompanyID owner = INVALID_COMPANY, VehicleType type = VEH_TRAIN) : owner(owner), vehicle_type(type), parent(INVALID_TRACE_RESTRICT_SLOT_GROUP) {}

	void AddSlotsToParentGroups();
	void RemoveSlotsFromParentGroups();

	bool CompanyCanReferenceSlotGroup(Owner owner) const;
};

/**
 * Counter type
 */
struct TraceRestrictCounter : TraceRestrictCounterPool::PoolItem<&_tracerestrictcounter_pool> {
	enum class Flag : uint8_t {
		Public, ///< Public counter.
	};
	using Flags = EnumBitSet<Flag, uint8_t>;

	Owner owner;
	Flags flags{};
	int32_t value = 0;
	std::string name;
	ankerl::svector<SignalReference, 0> progsig_dependants;

	TraceRestrictCounter(CompanyID owner = INVALID_COMPANY) : owner(owner) {}

	void UpdateValue(int32_t new_value);

	static int32_t ApplyValue(int32_t current, TraceRestrictCounterCondOpField op, int32_t value);

	void ApplyUpdate(TraceRestrictCounterCondOpField op, int32_t value)
	{
		this->UpdateValue(TraceRestrictCounter::ApplyValue(this->value, op, value));
	}

	inline bool IsUsableByOwner(Owner using_owner) const
	{
		return this->owner == using_owner || this->flags.Test(Flag::Public);
	}
};


void ShowSlotCreationQueryString(Window &parent);
#endif /* TRACERESTRICT_H */
