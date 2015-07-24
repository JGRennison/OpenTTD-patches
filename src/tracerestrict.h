/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict.h Header file for Trace Restriction. */

#ifndef TRACERESTRICT_H
#define TRACERESTRICT_H

#include "stdafx.h"
#include "core/bitmath_func.hpp"
#include "core/enum_type.hpp"
#include "core/pool_type.hpp"
#include "command_func.h"
#include "rail_map.h"
#include "tile_type.h"
#include <map>
#include <vector>

struct Train;

/** Unique identifiers for a trace restrict nodes. */
typedef uint32 TraceRestrictProgramID;
struct TraceRestrictProgram;

typedef uint32 TraceRestrictRefId;

/** Type of the pool for trace restrict programs. */
typedef Pool<TraceRestrictProgram, TraceRestrictProgramID, 16, 256000> TraceRestrictProgramPool;
/** The actual pool for trace restrict nodes. */
extern TraceRestrictProgramPool _tracerestrictprogram_pool;

#define FOR_ALL_TRACE_RESTRICT_PROGRAMS_FROM(var, start) FOR_ALL_ITEMS_FROM(TraceRestrictProgram, tr_index, var, start)
#define FOR_ALL_TRACE_RESTRICT_PROGRAMS(var) FOR_ALL_TRACE_RESTRICT_PROGRAMS_FROM(var, 0)

struct TraceRestrictMappingItem {
	TraceRestrictProgramID program_id;

	TraceRestrictMappingItem() { }

	TraceRestrictMappingItem(TraceRestrictProgramID program_id_)
			: program_id(program_id_) { }
};

typedef std::map<TraceRestrictRefId, TraceRestrictMappingItem> TraceRestrictMapping;
extern TraceRestrictMapping _tracerestrictprogram_mapping;

void ClearTraceRestrictMapping();

/// Of the fields below, the type and cond flags seem the most likely
/// to need future expansion, hence the reserved bits are placed
/// immediately after them
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

enum TraceRestrictItemType {
	TRIT_NULL                     = 0,
	TRIT_PF_DENY                  = 1,
	TRIT_PF_PENALTY               = 2,

	TRIT_COND_BEGIN               = 8,    ///< Start of conditional item types
	TRIT_COND_ENDIF               = 8,    ///< This is an endif block or an else block
	TRIT_COND_UNDEFINED           = 9,    ///< This condition has no type defined (evaluate as false)
	TRIT_COND_TRAIN_LENGTH        = 10,   ///< Test train length
	TRIT_COND_MAX_SPEED           = 11,   ///< Test train max speed
	TRIT_COND_CURRENT_ORDER       = 12,   ///< Test train current order (station, waypoint or depot)
	/* space up to 31 */
};

/* no flags set indicates end if for TRIT_COND_ENDIF, if otherwise */
enum TraceRestrictCondFlags {
	TRCF_ELSE                     = 1 << 0,
	TRCF_OR                       = 1 << 1,
	/* 1 bit spare */
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictCondFlags)

enum TraceRestictNullTypeSpecialValue {
	TRNTSV_NULL                   = 0,
	TRNTSV_START                  = 1,
	TRNTSV_END                    = 2,
};

enum TraceRestrictCondOp {
	TRCO_IS                       = 0,
	TRCO_ISNOT                    = 1,
	TRCO_LT                       = 2,
	TRCO_LTE                      = 3,
	TRCO_GT                       = 4,
	TRCO_GTE                      = 5,
	/* space up to 7 */
};

enum TraceRestrictOrderCondAuxField {
	TROCAF_STATION                = 0,
	TROCAF_WAYPOINT               = 1,
	TROCAF_DEPOT                  = 2,
	/* space up to 7 */
};

enum TraceRestrictProgramResultFlags {
	TRPRF_DENY                    = 1 << 0,
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictProgramResultFlags)

struct TraceRestrictProgramResult {
	uint32 penalty;
	TraceRestrictProgramResultFlags flags;

	TraceRestrictProgramResult()
			: penalty(0), flags(static_cast<TraceRestrictProgramResultFlags>(0)) { }
};

typedef uint32 TraceRestrictItem;

struct TraceRestrictProgram : TraceRestrictProgramPool::PoolItem<&_tracerestrictprogram_pool> {
	std::vector<TraceRestrictItem> items;
	uint32 refcount;

	TraceRestrictProgram()
			: refcount(0) { }

	void Execute(const Train *v, TraceRestrictProgramResult &out) const;

	void IncrementRefCount() { refcount++; }

	void DecrementRefCount();

	static CommandCost Validate(const std::vector<TraceRestrictItem> &items);

	CommandCost Validate() const { return TraceRestrictProgram::Validate(items); }
};

static inline TraceRestrictItemType GetTraceRestrictType(TraceRestrictItem item)
{
	return static_cast<TraceRestrictItemType>(GB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT));
}

static inline TraceRestrictCondFlags GetTraceRestrictCondFlags(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondFlags>(GB(item, TRIFA_COND_FLAGS_OFFSET, TRIFA_COND_FLAGS_COUNT));
}

static inline TraceRestrictCondOp GetTraceRestrictCondOp(TraceRestrictItem item)
{
	return static_cast<TraceRestrictCondOp>(GB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT));
}

static inline uint8 GetTraceRestrictAuxField(TraceRestrictItem item)
{
	return GB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT);
}

static inline uint16 GetTraceRestrictValue(TraceRestrictItem item)
{
	return static_cast<uint16>(GB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT));
}

static inline void SetTraceRestrictType(TraceRestrictItem &item, TraceRestrictItemType type)
{
	SB(item, TRIFA_TYPE_OFFSET, TRIFA_TYPE_COUNT, type);
}

static inline void SetTraceRestrictCondOp(TraceRestrictItem &item, TraceRestrictCondOp condop)
{
	SB(item, TRIFA_COND_OP_OFFSET, TRIFA_COND_OP_COUNT, condop);
}

static inline void SetTraceRestrictAuxField(TraceRestrictItem &item, uint8 data)
{
	SB(item, TRIFA_AUX_FIELD_OFFSET, TRIFA_AUX_FIELD_COUNT, data);
}

void SetTraceRestrictTypeAndNormalise(TraceRestrictItem &item, TraceRestrictItemType type);

static inline void SetTraceRestrictValue(TraceRestrictItem &item, uint16 value)
{
	SB(item, TRIFA_VALUE_OFFSET, TRIFA_VALUE_COUNT, value);
}

static inline bool IsTraceRestrictTypeConditional(TraceRestrictItemType type)
{
	return type >= TRIT_COND_BEGIN;
}

static inline bool IsTraceRestrictConditional(TraceRestrictItem item)
{
	return IsTraceRestrictTypeConditional(GetTraceRestrictType(item));
}

enum TraceRestrictConditionOpType {
	TRCOT_NONE                    = 0, ///< takes no condition op
	TRCOT_BINARY                  = 1, ///< takes "is" and "is not" condition ops
	TRCOT_ALL                     = 2, ///< takes all condition ops (i.e. all relational ops)
};

enum TraceRestrictValueType {
	TRVT_NONE                     = 0, ///< value field not used (set to 0)
	TRVT_SPECIAL                  = 1, ///< special handling of value field
	TRVT_INT                      = 2, ///< takes an integer value
	TRVT_DENY                     = 3, ///< takes a value 0 = deny, 1 = allow (cancel previous deny)
	TRVT_SPEED                    = 4, ///< takes an integer speed value
	TRVT_ORDER                    = 5, ///< takes an order target ID, as per the auxiliary field as type: TraceRestrictOrderCondAuxField
};

struct TraceRestrictTypePropertySet {
	TraceRestrictConditionOpType cond_type;
	TraceRestrictValueType value_type;
};

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
				out.value_type = TRVT_ORDER;
				out.cond_type = TRCOT_BINARY;
				break;

			default:
				NOT_REACHED();
				break;
		}
	} else {
		out.cond_type = TRCOT_NONE;
		if (GetTraceRestrictType(item) == TRIT_PF_PENALTY) {
			out.value_type = TRVT_INT;
		} else if (GetTraceRestrictType(item) == TRIT_PF_DENY) {
			out.value_type = TRVT_DENY;
		} else {
			out.value_type = TRVT_NONE;
		}
	}

	return out;
}

static inline TraceRestrictRefId MakeTraceRestrictRefId(TileIndex t, Track track)
{
	return (t << 3) | track;
}

static inline TileIndex GetTraceRestrictRefIdTileIndex(TraceRestrictRefId ref)
{
	return static_cast<TileIndex>(ref >> 3);
}

static inline Track GetTraceRestrictRefIdTrack(TraceRestrictRefId ref)
{
	return static_cast<Track>(ref & 7);
}

void TraceRestrictCreateProgramMapping(TraceRestrictRefId ref, TraceRestrictProgram *prog);
void TraceRestrictRemoveProgramMapping(TraceRestrictRefId ref);

/// Gets the signal program for the tile identified by @p t and @p track.
/// An empty program will be constructed if none exists, and create_new is true
/// unless the pool is full
TraceRestrictProgram *GetTraceRestrictProgram(TraceRestrictRefId ref, bool create_new);

/// Notify that a signal is being removed
/// Remove any trace restrict items associated with it
void TraceRestrictNotifySignalRemoval(TileIndex tile, Track track);

/// Gets the signal program for the tile identified by @p t and @p track, or NULL
static inline const TraceRestrictProgram *GetExistingTraceRestrictProgram(TileIndex t, Track track)
{
	if (IsRestrictedSignal(t)) {
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(t, track), false);
	} else {
		return NULL;
	}
}

// do not re-order
enum TraceRestrictDoCommandType {
	TRDCT_INSERT_ITEM             = 0,
	TRDCT_MODIFY_ITEM             = 1,
	TRDCT_REMOVE_ITEM             = 2,

	TRDCT_PROG_COPY               = 3,
	TRDCT_PROG_SHARE              = 4,
	TRDCT_PROG_UNSHARE            = 5,
	TRDCT_PROG_RESET              = 6,
};

void TraceRestrictDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32 offset, uint32 value, StringID error_msg);

void TraceRestrictProgMgmtWithSourceDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type,
		TileIndex source_tile, Track source_track, StringID error_msg);

inline void TraceRestrictProgMgmtDoCommandP(TileIndex tile, Track track, TraceRestrictDoCommandType type, StringID error_msg)
{
	TraceRestrictProgMgmtWithSourceDoCommandP(tile, track, type, static_cast<TileIndex>(0), static_cast<Track>(0), error_msg);
}

CommandCost CmdProgramSignalTraceRestrict(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text);
CommandCost CmdProgramSignalTraceRestrictProgMgmt(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text);

void ShowTraceRestrictProgramWindow(TileIndex tile, Track track);

void TraceRestrictRemoveDestinationID(TraceRestrictOrderCondAuxField type, uint16 index);

#endif /* TRACERESTRICT_H */
