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
#include "command_func.h"
#include "rail_map.h"
#include "tile_type.h"
#include "group_type.h"
#include <map>
#include <vector>

struct Train;

/** Program pool ID type. */
typedef uint32 TraceRestrictProgramID;
struct TraceRestrictProgram;

/** Tile/track mapping type. */
typedef uint32 TraceRestrictRefId;

/** Type of the pool for trace restrict programs. */
typedef Pool<TraceRestrictProgram, TraceRestrictProgramID, 16, 256000> TraceRestrictProgramPool;
/** The actual pool for trace restrict nodes. */
extern TraceRestrictProgramPool _tracerestrictprogram_pool;

extern const uint16 _tracerestrict_pathfinder_penalty_preset_values[];

#define FOR_ALL_TRACE_RESTRICT_PROGRAMS_FROM(var, start) FOR_ALL_ITEMS_FROM(TraceRestrictProgram, tr_index, var, start)
#define FOR_ALL_TRACE_RESTRICT_PROGRAMS(var) FOR_ALL_TRACE_RESTRICT_PROGRAMS_FROM(var, 0)

/** Type used for the TraceRestrictRefId -> TraceRestrictProgramID mapping */
struct TraceRestrictMappingItem {
	TraceRestrictProgramID program_id;

	TraceRestrictMappingItem() { }

	TraceRestrictMappingItem(TraceRestrictProgramID program_id_)
			: program_id(program_id_) { }
};

typedef std::map<TraceRestrictRefId, TraceRestrictMappingItem> TraceRestrictMapping;

/** The actual mapping from TraceRestrictRefId to TraceRestrictProgramID. */
extern TraceRestrictMapping _tracerestrictprogram_mapping;

void ClearTraceRestrictMapping();

/** Type of a single instruction, this is bit-packed as per TraceRestrictItemFlagAllocation */
typedef uint32 TraceRestrictItem;

/**
 * Describes the allocation of bits to fields in TraceRestrictItem
 * Of the fields below, the type seem the most likely
 * to need future expansion, hence the reserved bits are placed
 * immediately after them
 *
 * COUNT values describe the field bit width
 * OFFSET values describe the field bit offset
 */
enum TraceRestrictItemFlagAllocation {
	TRIFA_TYPE_COUNT              = 5,
	TRIFA_TYPE_OFFSET             = 0,

	/* 3 bits reserved for future use */

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
 * Enumeration of TraceRestrictItem type field
 * This is split into two halves:
 * * non-conditionals < TRIT_COND_BEGIN
 * * conditionals, >= TRIT_COND_BEGIN
 */
enum TraceRestrictItemType {
	TRIT_NULL                     = 0,    ///< Null-type, not in programs and not valid for execution, mainly used with TraceRestrictNullTypeSpecialValue for start/end
	TRIT_PF_DENY                  = 1,    ///< Pathfinder deny/allow
	TRIT_PF_PENALTY               = 2,    ///< Add to pathfinder penalty
	TRIT_RESERVE_THROUGH          = 3,    ///< Reserve through PBS signal
	TRIT_LONG_RESERVE             = 4,    ///< Long reserve PBS signal

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
	//TRIT_COND_TRAIN_OWNER       = 24,   ///< Test train owner: reserved for future use
	/* space up to 31 */
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
 * TraceRestrictItem auxiliary type field, for order type conditionals
 */
enum TraceRestrictPathfinderPenaltyAuxField {
	TRPPAF_VALUE                  = 0,       ///< value field is a the pathfinder penalty to use
	TRPPAF_PRESET                 = 1,       ///< value field is a pathfinder penalty prefix index: TraceRestrictPathfinderPenaltyPresetIndex
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
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramResultFlags)

/**
 * Enumeration for TraceRestrictProgram::actions_used_flags
 */
enum TraceRestrictProgramActionsUsedFlags {
	TRPAUF_PF                     = 1 << 0,  ///< Pathfinder deny or penalty are present
	TRPAUF_RESERVE_THROUGH        = 1 << 1,  ///< Reserve through action is present
	TRPAUF_LONG_RESERVE           = 1 << 2,  ///< Long reserve action is present
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramActionsUsedFlags)

/**
 * Execution input of a TraceRestrictProgram
 */
struct TraceRestrictProgramInput {
	typedef TileIndex PreviousSignalProc(const Train *v, const void *ptr);

	TileIndex tile;                               ///< Tile of restrict signal, for direction testing
	Trackdir trackdir;                            ///< Track direction on tile of restrict signal, for direction testing
	PreviousSignalProc *previous_signal_callback; ///< Callback to retrieve tile and direction of previous signal, may be NULL
	const void *previous_signal_ptr;              ///< Opaque pointer suitable to be passed to previous_signal_callback

	TraceRestrictProgramInput(TileIndex tile_, Trackdir trackdir_, PreviousSignalProc *previous_signal_callback_, const void *previous_signal_ptr_)
			: tile(tile_), trackdir(trackdir_), previous_signal_callback(previous_signal_callback_), previous_signal_ptr(previous_signal_ptr_) { }
};

/**
 * Execution result of a TraceRestrictProgram
 */
struct TraceRestrictProgramResult {
	uint32 penalty;                          ///< Total additional pathfinder penalty
	TraceRestrictProgramResultFlags flags;   ///< Flags of other actions to take

	TraceRestrictProgramResult()
			: penalty(0), flags(static_cast<TraceRestrictProgramResultFlags>(0)) { }
};

/**
 * Program type, this stores the instruction list
 * This is refcounted, see info at top of tracerestrict.cpp
 */
struct TraceRestrictProgram : TraceRestrictProgramPool::PoolItem<&_tracerestrictprogram_pool> {
	std::vector<TraceRestrictItem> items;
	uint32 refcount;
	TraceRestrictProgramActionsUsedFlags actions_used_flags;

	TraceRestrictProgram()
			: refcount(0), actions_used_flags(static_cast<TraceRestrictProgramActionsUsedFlags>(0)) { }

	void Execute(const Train *v, const TraceRestrictProgramInput &input, TraceRestrictProgramResult &out) const;

	/**
	 * Increment ref count, only use when creating a mapping
	 */
	void IncrementRefCount() { refcount++; }

	void DecrementRefCount();

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
static inline TraceRestrictItemType GetTraceRestrictType(TraceRestrictItem item)
{
	return static_cast<TraceRestrictItemType>(GB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT));
}

/** Get TraceRestrictItem condition flags field */
static inline TraceRestrictCondFlags GetTraceRestrictCondFlags(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondFlags>(GB(item, TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT));
}

/** Get TraceRestrictItem condition operator field */
static inline TraceRestrictCondOp GetTraceRestrictCondOp(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondOp>(GB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT));
}

/** Get TraceRestrictItem auxiliary field */
static inline uint8 GetTraceRestrictAuxField(TraceRestrictItem item)
{
	return GB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT);
}

/** Get TraceRestrictItem value field */
static inline uint16 GetTraceRestrictValue(TraceRestrictItem item)
{
	return static_cast<uint16>(GB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT));
}

/** Set TraceRestrictItem type field */
static inline void SetTraceRestrictType(TraceRestrictItem &item, TraceRestrictItemType type)
{
	SB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT, type);
}

/** Set TraceRestrictItem condition operator field */
static inline void SetTraceRestrictCondOp(TraceRestrictItem &item, TraceRestrictCondOp condop)
{
	SB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT, condop);
}

/** Set TraceRestrictItem condition flags field */
static inline void SetTraceRestrictCondFlags(TraceRestrictItem &item, TraceRestrictCondFlags condflags)
{
	SB(item, TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT, condflags);
}

/** Set TraceRestrictItem auxiliary field */
static inline void SetTraceRestrictAuxField(TraceRestrictItem &item, uint8 data)
{
	SB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT, data);
}

/** Set TraceRestrictItem value field */
static inline void SetTraceRestrictValue(TraceRestrictItem &item, uint16 value)
{
	SB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT, value);
}

/** Is TraceRestrictItemType a conditional type? */
static inline bool IsTraceRestrictTypeConditional(TraceRestrictItemType type)
{
	return type >= TRIT_COND_BEGIN;
}

/** Is TraceRestrictItem type field a conditional type? */
static inline bool IsTraceRestrictConditional(TraceRestrictItem item)
{
	return IsTraceRestrictTypeConditional(GetTraceRestrictType(item));
}

/** Is TraceRestrictItem a double-item type? */
static inline bool IsTraceRestrictDoubleItem(TraceRestrictItem item)
{
	return GetTraceRestrictType(item) == TRIT_COND_PBS_ENTRY_SIGNAL;
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
	TRVT_LONG_RESERVE             = 11,///< takes a value 0 = long reserve, 1 = cancel previous long reserve
	TRVT_GROUP_INDEX              = 12,///< takes a GroupID
	TRVT_WEIGHT                   = 13,///< takes a weight
	TRVT_POWER                    = 14,///< takes a power
	TRVT_FORCE                    = 15,///< takes a force
};

/**
 * Describes formats of TraceRestrictItem condition op and value fields
 */
struct TraceRestrictTypePropertySet {
	TraceRestrictConditionOpType cond_type;
	TraceRestrictValueType value_type;
};

void SetTraceRestrictValueDefault(TraceRestrictItem &item, TraceRestrictValueType value_type);
void SetTraceRestrictTypeAndNormalise(TraceRestrictItem &item, TraceRestrictItemType type, uint8 aux_data = 0);

/**
 * Get TraceRestrictTypePropertySet for a given instruction, only looks at value field
 */
static inline TraceRestrictTypePropertySet GetTraceRestrictTypeProperties(TraceRestrictItem item)
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

			default:
				NOT_REACHED();
				break;
		}
	} else {
		out.cond_type = TRCOT_NONE;
		if (GetTraceRestrictType(item) == TRIT_PF_PENALTY) {
			out.value_type = TRVT_PF_PENALTY;
		} else if (GetTraceRestrictType(item) == TRIT_PF_DENY) {
			out.value_type = TRVT_DENY;
		} else if (GetTraceRestrictType(item) == TRIT_RESERVE_THROUGH) {
			out.value_type = TRVT_RESERVE_THROUGH;
		} else if (GetTraceRestrictType(item) == TRIT_LONG_RESERVE) {
			out.value_type = TRVT_LONG_RESERVE;
		} else {
			out.value_type = TRVT_NONE;
		}
	}

	return out;
}

/** Get mapping ref ID from tile and track */
static inline TraceRestrictRefId MakeTraceRestrictRefId(TileIndex t, Track track)
{
	return (t << 3) | track;
}

/** Get tile from mapping ref ID */
static inline TileIndex GetTraceRestrictRefIdTileIndex(TraceRestrictRefId ref)
{
	return static_cast<TileIndex>(ref >> 3);
}

/** Get track from mapping ref ID */
static inline Track GetTraceRestrictRefIdTrack(TraceRestrictRefId ref)
{
	return static_cast<Track>(ref & 7);
}

void TraceRestrictCreateProgramMapping(TraceRestrictRefId ref, TraceRestrictProgram *prog);
bool TraceRestrictRemoveProgramMapping(TraceRestrictRefId ref);

TraceRestrictProgram *GetTraceRestrictProgram(TraceRestrictRefId ref, bool create_new);

void TraceRestrictNotifySignalRemoval(TileIndex tile, Track track);

/**
 * Gets the existing signal program for the tile identified by @p t and @p track, or NULL
 */
static inline const TraceRestrictProgram *GetExistingTraceRestrictProgram(TileIndex t, Track track)
{
	if (IsRestrictedSignal(t)) {
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(t, track), false);
	} else {
		return NULL;
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

	TRDCT_PROG_COPY,                         ///< copy program operation. Do not re-order this with respect to other values
	TRDCT_PROG_SHARE,                        ///< share program operation
	TRDCT_PROG_UNSHARE,                      ///< unshare program (copy as a new program)
	TRDCT_PROG_RESET,                        ///< reset program state of signal
};

void TraceRestrictDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32 offset, uint32 value, StringID error_msg);

void TraceRestrictProgMgmtWithSourceDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type,
		TileIndex source_tile, Track source_track, StringID error_msg);

/**
 * Short-hand to call TraceRestrictProgMgmtWithSourceDoCommandP with 0 for source tile/track
 */
inline void TraceRestrictProgMgmtDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, StringID error_msg)
{
	TraceRestrictProgMgmtWithSourceDoCommandP(tile, track, type, static_cast<TileIndex>(0), static_cast<Track>(0), error_msg);
}

CommandCost CmdProgramSignalTraceRestrict(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text);
CommandCost CmdProgramSignalTraceRestrictProgMgmt(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text);

void ShowTraceRestrictProgramWindow(TileIndex tile, Track track);

void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, uint16 index);
void TraceRestrictRemoveGroupID(GroupID index);

#endif /* TRACERESTRICT_H */
