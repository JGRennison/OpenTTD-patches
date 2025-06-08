/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_gui.cpp GUI code for Trace Restrict
 *
 * This is largely based on the programmable pre-signals patch's GUI
 * */

#include "stdafx.h"
#include "tracerestrict.h"
#include "tracerestrict_cmd.h"
#include "command_func.h"
#include "window_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "viewport_func.h"
#include "textbuf_gui.h"
#include "company_base.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "dropdown_func.h"
#include "dropdown_type.h"
#include "dropdown_common_type.h"
#include "gui.h"
#include "gfx_func.h"
#include "rail_map.h"
#include "depot_map.h"
#include "tile_cmd.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "depot_base.h"
#include "error.h"
#include "cargotype.h"
#include "sortlist_type.h"
#include "group.h"
#include "unit_conversion.h"
#include "company_base.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "vehicle_gui_base.h"
#include "scope.h"
#include "table/sprites.h"
#include "toolbar_gui.h"
#include "core/geometry_func.hpp"
#include "infrastructure_func.h"
#include "zoom_func.h"
#include "newgrf_debug.h"
#include "core/ring_buffer.hpp"
#include "core/y_combinator.hpp"
#include "3rdparty/cpp-btree/btree_map.h"

#include "safeguards.h"

static constexpr uint RECENT_SLOT_HISTORY_SIZE = 8;
static std::array<ring_buffer<TraceRestrictSlotID>, VEH_COMPANY_END> _recent_slots;
static std::array<ring_buffer<TraceRestrictSlotGroupID>, VEH_COMPANY_END> _recent_slot_groups;
static ring_buffer<TraceRestrictCounterID> _recent_counters;

static void EraseRecentSlotOrCounter(ring_buffer<uint16_t> &ring, uint16_t id)
{
	for (auto it = ring.begin(); it != ring.end();) {
		if (*it == id) {
			it = ring.erase(it);
		} else {
			++it;
		}
	}
}

static void RecordRecentSlotOrCounter(ring_buffer<uint16_t> &ring, uint16_t id)
{
	EraseRecentSlotOrCounter(ring, id);
	if (ring.size() >= RECENT_SLOT_HISTORY_SIZE) ring.erase(ring.begin() + RECENT_SLOT_HISTORY_SIZE - 1, ring.end());
	ring.push_front(id);
}

void TraceRestrictEraseRecentSlot(TraceRestrictSlotID index)
{
	for (ring_buffer<TraceRestrictSlotID> &ring : _recent_slots) {
		EraseRecentSlotOrCounter(ring, index);
	}
}

void TraceRestrictEraseRecentSlotGroup(TraceRestrictSlotGroupID index)
{
	for (ring_buffer<TraceRestrictSlotGroupID> &ring : _recent_slot_groups) {
		EraseRecentSlotOrCounter(ring, index);
	}
}

void TraceRestrictEraseRecentCounter(TraceRestrictCounterID index)
{
	EraseRecentSlotOrCounter(_recent_counters, index);
}

void TraceRestrictRecordRecentSlot(TraceRestrictSlotID index)
{
	const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(index);
	if (slot != nullptr && slot->owner == _local_company && slot->vehicle_type < _recent_slots.size()) {
		RecordRecentSlotOrCounter(_recent_slots[slot->vehicle_type], index);
	}
}

void TraceRestrictRecordRecentSlotGroup(TraceRestrictSlotGroupID index)
{
	const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(index);
	if (sg != nullptr && sg->owner == _local_company && sg->vehicle_type < _recent_slot_groups.size()) {
		RecordRecentSlotOrCounter(_recent_slot_groups[sg->vehicle_type], index);
	}
}

void TraceRestrictRecordRecentCounter(TraceRestrictCounterID index)
{
	const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(index);
	if (ctr != nullptr && ctr->owner == _local_company) {
		RecordRecentSlotOrCounter(_recent_counters , index);
	}
}

void TraceRestrictClearRecentSlotsAndCounters()
{
	for (auto &it : _recent_slots) {
		it.clear();
	}
	for (auto &it : _recent_slot_groups) {
		it.clear();
	}
	_recent_counters.clear();
}

/** Widget IDs */
enum TraceRestrictWindowWidgets : WidgetID {
	TR_WIDGET_CAPTION,
	TR_WIDGET_HIGHLIGHT,
	TR_WIDGET_INSTRUCTION_LIST,
	TR_WIDGET_SCROLLBAR,

	TR_WIDGET_SEL_TOP_LEFT_2,
	TR_WIDGET_SEL_TOP_LEFT,
	TR_WIDGET_SEL_TOP_LEFT_AUX,
	TR_WIDGET_SEL_TOP_MIDDLE,
	TR_WIDGET_SEL_TOP_RIGHT,
	TR_WIDGET_SEL_SHARE,
	TR_WIDGET_SEL_COPY,

	TR_WIDGET_UP_BTN,
	TR_WIDGET_DOWN_BTN,

	TR_WIDGET_TYPE_COND,
	TR_WIDGET_TYPE_NONCOND,
	TR_WIDGET_CONDFLAGS,
	TR_WIDGET_COMPARATOR,
	TR_WIDGET_SLOT_OP,
	TR_WIDGET_COUNTER_OP,
	TR_WIDGET_VALUE_INT,
	TR_WIDGET_VALUE_DECIMAL,
	TR_WIDGET_VALUE_DROPDOWN,
	TR_WIDGET_VALUE_DEST,
	TR_WIDGET_VALUE_SIGNAL,
	TR_WIDGET_VALUE_TILE,
	TR_WIDGET_LABEL,
	TR_WIDGET_LEFT_AUX_DROPDOWN,

	TR_WIDGET_BLANK_L2,
	TR_WIDGET_BLANK_L,
	TR_WIDGET_BLANK_M,
	TR_WIDGET_BLANK_R,

	TR_WIDGET_GOTO_SIGNAL,
	TR_WIDGET_INSERT,
	TR_WIDGET_REMOVE,
	TR_WIDGET_RESET,
	TR_WIDGET_COPY,
	TR_WIDGET_COPY_APPEND,
	TR_WIDGET_DUPLICATE,
	TR_WIDGET_SHARE,
	TR_WIDGET_UNSHARE,
	TR_WIDGET_SHARE_ONTO,
};

/** Selection mappings for NWID_SELECTION selectors */
enum PanelWidgets : WidgetID {
	/* Left 2 */
	DPL2_TYPE = 0,
	DPL2_CONDFLAGS,
	DPL2_BLANK,

	/* Left */
	DPL_TYPE = 0,
	DPL_COUNTER_OP,
	DPL_BLANK,

	/* Left aux */
	DPLA_DROPDOWN = 0,

	/* Middle */
	DPM_COMPARATOR = 0,
	DPM_SLOT_OP,
	DPM_BLANK,

	/* Right */
	DPR_VALUE_INT = 0,
	DPR_VALUE_DECIMAL,
	DPR_VALUE_DROPDOWN,
	DPR_VALUE_DEST,
	DPR_VALUE_SIGNAL,
	DPR_VALUE_TILE,
	DPR_LABEL_BUTTON,
	DPR_BLANK,

	/* Share */
	DPS_SHARE = 0,
	DPS_UNSHARE,
	DPS_SHARE_ONTO,

	/* Copy */
	DPC_COPY = 0,
	DPC_APPEND,
	DPC_DUPLICATE,
};

/**
 * drop down list string array, and corresponding integer values
 *
 * value_array *must* be at least as long as string_array
 */
struct TraceRestrictDropDownListSet {
	std::span<const StringID> string_array;
	std::span<const uint> value_array;

	TraceRestrictDropDownListSet(std::span<const StringID> string_array, std::span<const uint> value_array) : string_array(string_array), value_array(value_array.first(string_array.size()))
	{
		assert(value_array.size() >= string_array.size());
	}
};

static const StringID _program_insert_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_IF,
	STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ELSE,
};
static const uint32_t _program_insert_else_hide_mask    = 8;     ///< disable bitmask for else
static const uint32_t _program_insert_or_if_hide_mask   = 4;     ///< disable bitmask for orif
static const uint32_t _program_insert_else_if_hide_mask = 2;     ///< disable bitmask for elif
static const uint _program_insert_val[] = {
	TRIT_COND_UNDEFINED,                               // if block
	TRIT_COND_UNDEFINED | (TRCF_ELSE << 16),           // elif block
	TRIT_COND_UNDEFINED | (TRCF_OR << 16),             // orif block
	TRIT_COND_ENDIF | (TRCF_ELSE << 16),               // else block
};

/** insert drop down list strings and values */
static const TraceRestrictDropDownListSet _program_insert = {
	_program_insert_str, _program_insert_val,
};

static const StringID _deny_value_str[] = {
	STR_TRACE_RESTRICT_PF_DENY,
	STR_TRACE_RESTRICT_PF_ALLOW,
};
static const uint _deny_value_val[] = {
	0,
	1,
};

/** value drop down list for deny types strings and values */
static const TraceRestrictDropDownListSet _deny_value = {
	_deny_value_str, _deny_value_val,
};

static const StringID _reserve_through_value_str[] = {
	STR_TRACE_RESTRICT_RESERVE_THROUGH,
	STR_TRACE_RESTRICT_RESERVE_THROUGH_CANCEL,
};
static const uint _reserve_through_value_val[] = {
	0,
	1,
};

/** value drop down list for deny types strings and values */
static const TraceRestrictDropDownListSet _reserve_through_value = {
	_reserve_through_value_str, _reserve_through_value_val,
};

static const StringID _long_reserve_value_str[] = {
	STR_TRACE_RESTRICT_LONG_RESERVE,
	STR_TRACE_RESTRICT_LONG_RESERVE_CANCEL,
	STR_TRACE_RESTRICT_LONG_RESERVE_UNLESS_STOPPING,
};
static const uint _long_reserve_value_val[] = {
	0,
	1,
	2,
};

/** value drop down list for long reserve types strings and values */
static const TraceRestrictDropDownListSet _long_reserve_value = {
	_long_reserve_value_str, _long_reserve_value_val,
};

static const StringID _wait_at_pbs_value_str[] = {
	STR_TRACE_RESTRICT_WAIT_AT_PBS,
	STR_TRACE_RESTRICT_WAIT_AT_PBS_CANCEL,
	STR_TRACE_RESTRICT_PBS_RES_END_WAIT_SHORT,
	STR_TRACE_RESTRICT_PBS_RES_END_WAIT_CANCEL_SHORT,
};
static const uint _wait_at_pbs_value_val[] = {
	TRWAPVF_WAIT_AT_PBS,
	TRWAPVF_CANCEL_WAIT_AT_PBS,
	TRWAPVF_PBS_RES_END_WAIT,
	TRWAPVF_CANCEL_PBS_RES_END_WAIT,
};

/** value drop down list for wait at PBS types strings and values */
static const TraceRestrictDropDownListSet _wait_at_pbs_value = {
	_wait_at_pbs_value_str, _wait_at_pbs_value_val,
};

static const StringID _direction_value_str[] = {
	STR_TRACE_RESTRICT_DIRECTION_FRONT,
	STR_TRACE_RESTRICT_DIRECTION_BACK,
	STR_TRACE_RESTRICT_DIRECTION_NE,
	STR_TRACE_RESTRICT_DIRECTION_SE,
	STR_TRACE_RESTRICT_DIRECTION_SW,
	STR_TRACE_RESTRICT_DIRECTION_NW,
	STR_TRACE_RESTRICT_DIRECTION_TUNBRIDGE_ENTRANCE,
	STR_TRACE_RESTRICT_DIRECTION_TUNBRIDGE_EXIT,
};
static const uint _direction_value_val[] = {
	TRDTSV_FRONT,
	TRDTSV_BACK,
	TRNTSV_NE,
	TRNTSV_SE,
	TRNTSV_SW,
	TRNTSV_NW,
	TRDTSV_TUNBRIDGE_ENTER,
	TRDTSV_TUNBRIDGE_EXIT,
};

/** value drop down list for direction type strings and values */
static const TraceRestrictDropDownListSet _direction_value = {
	_direction_value_str, _direction_value_val,
};

static const StringID _train_status_value_str[] = {
	STR_TRACE_RESTRICT_TRAIN_STATUS_EMPTY,
	STR_TRACE_RESTRICT_TRAIN_STATUS_FULL,
	STR_TRACE_RESTRICT_TRAIN_STATUS_BROKEN_DOWN,
	STR_TRACE_RESTRICT_TRAIN_STATUS_NEEDS_REPAIR,
	STR_TRACE_RESTRICT_TRAIN_STATUS_REVERSING,
	STR_TRACE_RESTRICT_TRAIN_STATUS_HEADING_TO_STATION_WAYPOINT,
	STR_TRACE_RESTRICT_TRAIN_STATUS_HEADING_TO_DEPOT,
	STR_TRACE_RESTRICT_TRAIN_STATUS_LOADING,
	STR_TRACE_RESTRICT_TRAIN_STATUS_WAITING,
	STR_TRACE_RESTRICT_TRAIN_STATUS_LOST,
	STR_TRACE_RESTRICT_TRAIN_STATUS_REQUIRES_SERVICE,
	STR_TRACE_RESTRICT_TRAIN_STATUS_STOPPING_AT_STATION_WAYPOINT,
};
static const uint _train_status_value_val[] = {
	TRTSVF_EMPTY,
	TRTSVF_FULL,
	TRTSVF_BROKEN_DOWN,
	TRTSVF_NEEDS_REPAIR,
	TRTSVF_REVERSING,
	TRTSVF_HEADING_TO_STATION_WAYPOINT,
	TRTSVF_HEADING_TO_DEPOT,
	TRTSVF_LOADING,
	TRTSVF_WAITING,
	TRTSVF_LOST,
	TRTSVF_REQUIRES_SERVICE,
	TRTSVF_STOPPING_AT_STATION_WAYPOINT,
};

/** value drop down list for train status type strings and values */
static const TraceRestrictDropDownListSet _train_status_value = {
	_train_status_value_str, _train_status_value_val,
};

static const StringID _reverse_value_str[] = {
	STR_TRACE_RESTRICT_REVERSE_SIG,
	STR_TRACE_RESTRICT_REVERSE_SIG_CANCEL,
	STR_TRACE_RESTRICT_REVERSE_AT_SIG,
	STR_TRACE_RESTRICT_REVERSE_AT_SIG_CANCEL,
};
static const uint _reverse_value_val[] = {
	TRRVF_REVERSE_BEHIND,
	TRRVF_CANCEL_REVERSE_BEHIND,
	TRRVF_REVERSE_AT,
	TRRVF_CANCEL_REVERSE_AT,
};

/** value drop down list for reverse types strings and values */
static const TraceRestrictDropDownListSet _reverse_value = {
	_reverse_value_str, _reverse_value_val,
};

static const StringID _news_control_value_str[] = {
	STR_TRACE_RESTRICT_TRAIN_NOT_STUCK_SHORT,
	STR_TRACE_RESTRICT_TRAIN_NOT_STUCK_CANCEL_SHORT,
};
static const uint _news_control_value_val[] = {
	TRNCF_TRAIN_NOT_STUCK,
	TRNCF_CANCEL_TRAIN_NOT_STUCK,
};

/** value drop down list for news control types strings and values */
static const TraceRestrictDropDownListSet _news_control_value = {
	_news_control_value_str, _news_control_value_val,
};

static const StringID _time_date_value_str[] = {
	STR_TRACE_RESTRICT_TIME_MINUTE,
	STR_TRACE_RESTRICT_TIME_HOUR,
	STR_TRACE_RESTRICT_TIME_HOUR_MINUTE,
	STR_TRACE_RESTRICT_TIME_DAY,
	STR_TRACE_RESTRICT_TIME_MONTH,
};
static const uint _time_date_value_val[] = {
	TRTDVF_MINUTE,
	TRTDVF_HOUR,
	TRTDVF_HOUR_MINUTE,
	TRTDVF_DAY,
	TRTDVF_MONTH,
};

/** value drop down list for time/date types strings and values */
static const TraceRestrictDropDownListSet _time_date_value = {
	_time_date_value_str, _time_date_value_val,
};

static const StringID _engine_class_value_str[] = {
	STR_LIVERY_STEAM,
	STR_LIVERY_DIESEL,
	STR_LIVERY_ELECTRIC,
	STR_LIVERY_MONORAIL,
	STR_LIVERY_MAGLEV,
};
static const uint _engine_class_value_val[] = {
	EC_STEAM,    ///< Steam rail engine.
	EC_DIESEL,   ///< Diesel rail engine.
	EC_ELECTRIC, ///< Electric rail engine.
	EC_MONORAIL, ///< Mono rail engine.
	EC_MAGLEV,   ///< Maglev engine.
};

/** value drop down list for engine class type strings and values */
static const TraceRestrictDropDownListSet _engine_class_value = {
	_engine_class_value_str, _engine_class_value_val,
};

static const StringID _diagdir_value_str[] = {
	STR_TRACE_RESTRICT_DIRECTION_NE,
	STR_TRACE_RESTRICT_DIRECTION_SE,
	STR_TRACE_RESTRICT_DIRECTION_SW,
	STR_TRACE_RESTRICT_DIRECTION_NW,
};
static const uint _diagdir_value_val[] = {
	DIAGDIR_NE,
	DIAGDIR_SE,
	DIAGDIR_SW,
	DIAGDIR_NW,
};

/** value drop down list for DiagDirection strings and values */
static const TraceRestrictDropDownListSet _diagdir_value = {
	_diagdir_value_str, _diagdir_value_val,
};

static const StringID _dtarget_direction_aux_value_str[] = {
	STR_TRACE_RESTRICT_VARIABLE_CURRENT_ORDER,
	STR_TRACE_RESTRICT_VARIABLE_NEXT_ORDER,
};
static const uint _target_direction_aux_value_val[] = {
	TRTDCAF_CURRENT_ORDER,
	TRTDCAF_NEXT_ORDER,
};

/** value drop down list for TRIT_COND_TARGET_DIRECTION auxiliary type strings and values */
static const TraceRestrictDropDownListSet _target_direction_aux_value = {
	_dtarget_direction_aux_value_str, _target_direction_aux_value_val,
};

static const StringID _pf_penalty_control_value_str[] = {
	STR_TRACE_RESTRICT_NO_PBS_BACK_PENALTY_SHORT,
	STR_TRACE_RESTRICT_NO_PBS_BACK_PENALTY_CANCEL_SHORT,
};
static const uint _pf_penalty_control_value_val[] = {
	TRPPCF_NO_PBS_BACK_PENALTY,
	TRPPCF_CANCEL_NO_PBS_BACK_PENALTY,
};

/** value drop down list for PF penalty control types strings and values */
static const TraceRestrictDropDownListSet _pf_penalty_control_value = {
	_pf_penalty_control_value_str, _pf_penalty_control_value_val,
};

static const StringID _speed_adaptation_control_value_str[] = {
	STR_TRACE_RESTRICT_MAKE_TRAIN_SPEED_ADAPTATION_EXEMPT_SHORT,
	STR_TRACE_RESTRICT_REMOVE_TRAIN_SPEED_ADAPTATION_EXEMPT_SHORT,
};
static const uint _speed_adaptation_control_value_val[] = {
	TRSACF_SPEED_ADAPT_EXEMPT,
	TRSACF_REMOVE_SPEED_ADAPT_EXEMPT,
};

/** value drop down list for speed adaptation control types strings and values */
static const TraceRestrictDropDownListSet _speed_adaptation_control_value = {
	_speed_adaptation_control_value_str, _speed_adaptation_control_value_val,
};

static const StringID _signal_mode_control_value_str[] = {
	STR_TRACE_RESTRICT_USE_NORMAL_ASPECT_MODE_SHORT,
	STR_TRACE_RESTRICT_USE_SHUNT_ASPECT_MODE_SHORT,
};
static const uint _signal_mode_control_value_val[] = {
	TRSMCF_NORMAL_ASPECT,
	TRSMCF_SHUNT_ASPECT,
};

/** value drop down list for speed adaptation control types strings and values */
static const TraceRestrictDropDownListSet _signal_mode_control_value = {
	_signal_mode_control_value_str, _signal_mode_control_value_val,
};

/**
 * Get index of @p value in @p list_set
 * if @p value is not present, assert if @p missing_ok is false, otherwise return -1
 */
static int GetDropDownListIndexByValue(const TraceRestrictDropDownListSet *list_set, uint value, bool missing_ok)
{
	std::span<const uint> value_array = list_set->value_array;

	for (int i = 0; i < static_cast<int>(value_array.size()); i++) {
		if (value_array[i] == value) return i;
	}
	assert(missing_ok == true);
	return -1;
}

/**
 * Get StringID corresponding to @p value, in @list_set
 * @p value must be present
 */
static StringID GetDropDownStringByValue(const TraceRestrictDropDownListSet *list_set, uint value)
{
	return list_set->string_array[GetDropDownListIndexByValue(list_set, value, false)];
}

typedef uint TraceRestrictGuiItemType;

static TraceRestrictGuiItemType GetItemGuiType(TraceRestrictInstructionItem item)
{
	TraceRestrictItemType type = item.GetType();
	if (IsTraceRestrictTypeAuxSubtype(type)) {
		return type | (item.GetAuxField() << 16);
	} else {
		return type;
	}
}

static TraceRestrictItemType ItemTypeFromGuiType(TraceRestrictGuiItemType type)
{
	return static_cast<TraceRestrictItemType>(type & 0xFFFF);
}

enum TraceRestrictDropDownListItemFlags : uint8_t {
	TRDDLIF_NONE                      =      0,
	TRDDLIF_ADVANCED                  = 1 << 0,  ///< requires _settings_client.gui.show_adv_tracerestrict_features
	TRDDLIF_REALISTIC_BRAKING         = 1 << 1,  ///< requires realistic braking
	TRDDLIF_SPEED_ADAPTATION          = 1 << 2,  ///< requires speed adaptation
	TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE = 1 << 3,  ///< requires normal/shunt signal styles
	TRDDLIF_HIDDEN                    = 1 << 4,  ///< always hidden
};
DECLARE_ENUM_AS_BIT_SET(TraceRestrictDropDownListItemFlags)

struct TraceRestrictDropDownListItem {
	TraceRestrictGuiItemType type;
	StringID str;
	TraceRestrictDropDownListItemFlags flags;
};

static std::span<const TraceRestrictDropDownListItem> GetActionDropDownListItems()
{
	static const TraceRestrictDropDownListItem actions[] = {
		{ TRIT_PF_DENY,                  STR_TRACE_RESTRICT_PF_DENY,                  TRDDLIF_NONE },
		{ TRIT_PF_PENALTY,               STR_TRACE_RESTRICT_PF_PENALTY,               TRDDLIF_NONE },
		{ TRIT_RESERVE_THROUGH,          STR_TRACE_RESTRICT_RESERVE_THROUGH,          TRDDLIF_NONE },
		{ TRIT_LONG_RESERVE,             STR_TRACE_RESTRICT_LONG_RESERVE,             TRDDLIF_NONE },
		{ TRIT_NEWS_CONTROL,             STR_TRACE_RESTRICT_NEWS_CONTROL,             TRDDLIF_NONE },
		{ TRIT_SLOT,                     STR_TRACE_RESTRICT_SLOT_OP,                  TRDDLIF_NONE },
		{ TRIT_SLOT_GROUP,               STR_TRACE_RESTRICT_SLOT_GROUP_OP,            TRDDLIF_NONE },
		{ TRIT_WAIT_AT_PBS,              STR_TRACE_RESTRICT_WAIT_AT_PBS,              TRDDLIF_ADVANCED },
		{ TRIT_REVERSE,                  STR_TRACE_RESTRICT_REVERSE,                  TRDDLIF_ADVANCED },
		{ TRIT_SPEED_RESTRICTION,        STR_TRACE_RESTRICT_SPEED_RESTRICTION,        TRDDLIF_ADVANCED },
		{ TRIT_COUNTER,                  STR_TRACE_RESTRICT_COUNTER_OP,               TRDDLIF_ADVANCED },
		{ TRIT_PF_PENALTY_CONTROL,       STR_TRACE_RESTRICT_PF_PENALTY_CONTROL,       TRDDLIF_ADVANCED },
		{ TRIT_SPEED_ADAPTATION_CONTROL, STR_TRACE_RESTRICT_SPEED_ADAPTATION_CONTROL, TRDDLIF_ADVANCED | TRDDLIF_SPEED_ADAPTATION },
		{ TRIT_SIGNAL_MODE_CONTROL,      STR_TRACE_RESTRICT_SIGNAL_MODE_CONTROL,      TRDDLIF_ADVANCED | TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE },
		{ TRIT_GUI_LABEL,                STR_TRACE_RESTRICT_GUI_LABEL,                TRDDLIF_NONE },
	};
	return std::span<const TraceRestrictDropDownListItem>(actions);
}

static std::span<const TraceRestrictDropDownListItem> GetConditionDropDownListItems()
{
	static const TraceRestrictDropDownListItem conditions[] = {
		{ TRIT_COND_UNDEFINED,                                        STR_TRACE_RESTRICT_VARIABLE_UNDEFINED,                 TRDDLIF_HIDDEN },
		{ TRIT_COND_TRAIN_LENGTH,                                     STR_TRACE_RESTRICT_VARIABLE_TRAIN_LENGTH,              TRDDLIF_NONE },
		{ TRIT_COND_MAX_SPEED,                                        STR_TRACE_RESTRICT_VARIABLE_MAX_SPEED,                 TRDDLIF_NONE },
		{ TRIT_COND_CURRENT_ORDER,                                    STR_TRACE_RESTRICT_VARIABLE_CURRENT_ORDER,             TRDDLIF_NONE },
		{ TRIT_COND_NEXT_ORDER,                                       STR_TRACE_RESTRICT_VARIABLE_NEXT_ORDER,                TRDDLIF_NONE },
		{ TRIT_COND_LAST_STATION,                                     STR_TRACE_RESTRICT_VARIABLE_LAST_VISITED_STATION,      TRDDLIF_NONE },
		{ TRIT_COND_CARGO,                                            STR_TRACE_RESTRICT_VARIABLE_CARGO,                     TRDDLIF_NONE },
		{ TRIT_COND_LOAD_PERCENT,                                     STR_TRACE_RESTRICT_VARIABLE_LOAD_PERCENT,              TRDDLIF_NONE },
		{ TRIT_COND_ENTRY_DIRECTION,                                  STR_TRACE_RESTRICT_VARIABLE_ENTRY_DIRECTION,           TRDDLIF_NONE },
		{ TRIT_COND_TRAIN_GROUP,                                      STR_TRACE_RESTRICT_VARIABLE_TRAIN_GROUP,               TRDDLIF_NONE },
		{ TRIT_COND_TRAIN_OWNER,                                      STR_TRACE_RESTRICT_VARIABLE_TRAIN_OWNER,               TRDDLIF_NONE },
		{ TRIT_COND_TRAIN_STATUS,                                     STR_TRACE_RESTRICT_VARIABLE_TRAIN_STATUS,              TRDDLIF_NONE },
		{ TRIT_COND_PHYS_PROP | (TRPPCAF_WEIGHT << 16),               STR_TRACE_RESTRICT_VARIABLE_TRAIN_WEIGHT,              TRDDLIF_NONE },
		{ TRIT_COND_PHYS_PROP | (TRPPCAF_POWER << 16),                STR_TRACE_RESTRICT_VARIABLE_TRAIN_POWER,               TRDDLIF_NONE },
		{ TRIT_COND_PHYS_PROP | (TRPPCAF_MAX_TE << 16),               STR_TRACE_RESTRICT_VARIABLE_TRAIN_MAX_TE,              TRDDLIF_NONE },
		{ TRIT_COND_PHYS_RATIO | (TRPPRCAF_POWER_WEIGHT << 16),       STR_TRACE_RESTRICT_VARIABLE_TRAIN_POWER_WEIGHT_RATIO,  TRDDLIF_NONE },
		{ TRIT_COND_PHYS_RATIO | (TRPPRCAF_MAX_TE_WEIGHT << 16),      STR_TRACE_RESTRICT_VARIABLE_TRAIN_MAX_TE_WEIGHT_RATIO, TRDDLIF_NONE },
		{ TRIT_COND_CATEGORY | (TRCCAF_ENGINE_CLASS << 16),           STR_TRACE_RESTRICT_VARIABLE_TRAIN_ENGINE_CLASS,        TRDDLIF_NONE },
		{ TRIT_COND_TARGET_DIRECTION,                                 STR_TRACE_RESTRICT_VARIABLE_ORDER_TARGET_DIRECTION,    TRDDLIF_NONE },
		{ TRIT_COND_TRAIN_IN_SLOT,                                    STR_TRACE_RESTRICT_VARIABLE_TRAIN_SLOT,                TRDDLIF_NONE },
		{ TRIT_COND_TRAIN_IN_SLOT_GROUP,                              STR_TRACE_RESTRICT_VARIABLE_TRAIN_SLOT_GROUP,          TRDDLIF_NONE },
		{ TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_OCCUPANTS << 16),       STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY,            TRDDLIF_NONE },
		{ TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_REMAINING << 16),       STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_REMAINING,  TRDDLIF_NONE },
		{ TRIT_COND_COUNTER_VALUE,                                    STR_TRACE_RESTRICT_VARIABLE_COUNTER_VALUE,             TRDDLIF_ADVANCED },
		{ TRIT_COND_TIME_DATE_VALUE,                                  STR_TRACE_RESTRICT_VARIABLE_TIME_DATE_VALUE,           TRDDLIF_ADVANCED },
		{ TRIT_COND_RESERVED_TILES,                                   STR_TRACE_RESTRICT_VARIABLE_RESERVED_TILES_AHEAD,      TRDDLIF_ADVANCED | TRDDLIF_REALISTIC_BRAKING },
		{ TRIT_COND_RESERVATION_THROUGH,                              STR_TRACE_RESTRICT_VARIABLE_RESERVATION_THROUGH,       TRDDLIF_ADVANCED },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_VEH_POS << 16),       STR_TRACE_RESTRICT_VARIABLE_PBS_ENTRY_SIGNAL,          TRDDLIF_ADVANCED },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_RES_END << 16),       STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_SIGNAL,        TRDDLIF_ADVANCED | TRDDLIF_REALISTIC_BRAKING },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_RES_END_TILE << 16),  STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_TILE,          TRDDLIF_ADVANCED | TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE },
	};
	return std::span<const TraceRestrictDropDownListItem>(conditions);
}

/**
 * Return the appropriate type dropdown TraceRestrictDropDownListItem std::span for the given item type @p type
 */
static std::span<const TraceRestrictDropDownListItem> GetTypeDropDownListItems(TraceRestrictGuiItemType type)
{
	if (IsTraceRestrictTypeConditional(ItemTypeFromGuiType(type))) {
		return GetConditionDropDownListItems();
	} else {
		return GetActionDropDownListItems();
	}
}

static bool ShouldHideTypeDropDownListItem(TraceRestrictDropDownListItemFlags flags)
{
	if ((flags & TRDDLIF_ADVANCED) && !_settings_client.gui.show_adv_tracerestrict_features) return true;
	if ((flags & TRDDLIF_REALISTIC_BRAKING) && _settings_game.vehicle.train_braking_model != TBM_REALISTIC) return true;
	if ((flags & TRDDLIF_SPEED_ADAPTATION) && !_settings_game.vehicle.train_speed_adaptation) return true;
	if ((flags & TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE) && (_settings_game.vehicle.train_braking_model != TBM_REALISTIC || _signal_style_masks.combined_normal_shunt == 0)) return true;
	if (flags & TRDDLIF_HIDDEN) return true;
	return false;
}

/**
 * Get a TraceRestrictDropDownListSet of the sorted cargo list
 */
static const TraceRestrictDropDownListSet *GetSortedCargoTypeDropDownListSet()
{
	static StringID cargo_list_str[NUM_CARGO];
	static uint cargo_list_id[NUM_CARGO];
	static TraceRestrictDropDownListSet cargo_list({}, {});

	for (size_t i = 0; i < _sorted_standard_cargo_specs.size(); ++i) {
		const CargoSpec *cs = _sorted_cargo_specs[i];
		cargo_list_str[i] = cs->name;
		cargo_list_id[i] = cs->Index();
	}
	cargo_list.string_array = std::span<const StringID>(cargo_list_str, _sorted_standard_cargo_specs.size());
	cargo_list.value_array = std::span<const uint>(cargo_list_id, _sorted_standard_cargo_specs.size());

	return &cargo_list;
}

/**
 * Get a DropDownList of the group list
 */
static DropDownList GetGroupDropDownList(Owner owner, GroupID group_id, int &selected, bool include_default = true)
{
	std::vector<const Group *> list;
	robin_hood::unordered_set<GroupID> seen_parents;

	for (const Group *g : Group::Iterate()) {
		if (g->owner == owner && g->vehicle_type == VEH_TRAIN) {
			list.push_back(g);
			seen_parents.insert(g->parent);
		}
	}

	{
		/* Sort the groups by their parent group, then their name */
		const Group *last_group[2] = { nullptr, nullptr };
		format_buffer last_name[2] = { {}, {} };
		std::sort(list.begin(), list.end(), [&](const Group *a, const Group *b) {
			if (a->parent != b->parent) return a->parent < b->parent;

			if (a != last_group[0]) {
				last_group[0] = a;
				SetDParam(0, a->index);
				last_name[0].clear();
				AppendStringInPlace(last_name[0], STR_GROUP_NAME);
			}

			if (b != last_group[1]) {
				last_group[1] = b;
				SetDParam(0, b->index);
				last_name[1].clear();
				AppendStringInPlace(last_name[1], STR_GROUP_NAME);
			}

			int r = StrNaturalCompare(last_name[0], last_name[1]); // Sort by name (natural sorting).
			if (r == 0) return a->index < b->index;
			return r < 0;
		});
	}

	DropDownList dlist;
	selected = -1;

	if (include_default) {
		if (group_id == DEFAULT_GROUP) selected = DEFAULT_GROUP;
		dlist.push_back(MakeDropDownListStringItem(STR_GROUP_DEFAULT_TRAINS, DEFAULT_GROUP, false));
	}

	auto output_groups = y_combinator([&](auto output_groups, uint indent, GroupID parent_filter) -> void {
		auto start = std::lower_bound(list.begin(), list.end(), parent_filter, [&](const Group *g, GroupID parent_filter) {
			return g->parent < parent_filter;
		});
		for (auto it = start; it != list.end() && (*it)->parent == parent_filter; ++it) {
			const Group *g = *it;
			if (group_id == g->index) selected = group_id;
			SetDParam(0, g->index);
			dlist.push_back(MakeDropDownListIndentStringItem(indent, STR_GROUP_NAME, g->index, false));
			if (seen_parents.count(g->index)) {
				/* Output child groups */
				output_groups(indent + 1, g->index);
			}
		}
	});
	output_groups(0, INVALID_GROUP);

	return dlist;
}

enum class SlotItemType : uint8_t {
	None,
	Slot,
	Group,
	Special,
};

struct SlotItemInfo {
	const std::string &name;
	VehicleType vehicle_type;
	TraceRestrictSlotGroupID parent;
};

struct SlotItem {
	SlotItemType type{};
	uint16_t id{};

	SlotItemInfo GetInfo() const
	{
		if (this->type == SlotItemType::Slot) {
			const TraceRestrictSlot *slot = TraceRestrictSlot::Get(this->id);
			return SlotItemInfo{ slot->name, slot->vehicle_type, slot->parent_group };
		} else if (this->type == SlotItemType::Group) {
			const TraceRestrictSlotGroup *slot_group = TraceRestrictSlotGroup::Get(this->id);
			return SlotItemInfo{ slot_group->name, slot_group->vehicle_type, slot_group->parent };
		} else {
			NOT_REACHED();
		}
	}

	SlotItem GetParentItem() const
	{
		TraceRestrictSlotGroupID parent = INVALID_TRACE_RESTRICT_SLOT_GROUP;
		if (this->type == SlotItemType::Slot) {
			parent = TraceRestrictSlot::Get(this->id)->parent_group;
		} else if (this->type == SlotItemType::Group) {
			parent = TraceRestrictSlotGroup::Get(this->id)->parent;
		}
		if (parent == INVALID_TRACE_RESTRICT_SLOT_GROUP) {
			return SlotItem{};
		} else {
			return SlotItem{ SlotItemType::Group, parent };
		}
	}

	TraceRestrictSlotGroupID GetClosestGroupID() const
	{
		if (this->type == SlotItemType::Slot) {
			return this->GetParentItem().GetClosestGroupID();
		} else if (this->type == SlotItemType::Group) {
			return this->id;
		} else {
			return INVALID_TRACE_RESTRICT_SLOT_GROUP;
		}
	}

	bool IsInvalid() const
	{
		if (this->type == SlotItemType::Slot && this->id != NEW_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(this->id)) return true;
		if (this->type == SlotItemType::Group && this->id != NEW_TRACE_RESTRICT_SLOT_GROUP && !TraceRestrictSlotGroup::IsValidID(this->id)) return true;
		return false;
	}

	bool IsNone() const
	{
		return this->type == SlotItemType::None;
	}

	bool operator==(const SlotItem& c) const = default;
	auto operator<=>(const SlotItem& c) const = default;
};

static void GetSlotDropDownListIntl(DropDownList &dlist, Owner owner, TraceRestrictSlotID slot_id, int &selected, VehicleType vehtype, bool show_other_types, bool recently_used, bool public_only, bool group_only_mode)
{
	selected = -1;

	auto add_slot = [&](const TraceRestrictSlot *slot, TraceRestrictSlotID id, uint indent) {
		if (slot_id == id) selected = slot_id;
		if (indent == 0 || slot->vehicle_type == vehtype) {
			SetDParam(0, id);
			dlist.push_back(MakeDropDownListIndentStringItem(indent, STR_TRACE_RESTRICT_SLOT_NAME, id, false));
		} else {
			SetDParam(0, STR_REPLACE_VEHICLE_TRAIN + slot->vehicle_type);
			SetDParam(1, id);
			dlist.push_back(MakeDropDownListIndentStringItem(indent, STR_TRACE_RESTRICT_SLOT_NAME_PREFIXED, id, false));
		}
	};

	auto add_group = [&](const TraceRestrictSlotGroup *sg, TraceRestrictSlotGroupID id, uint indent) {
		if (group_only_mode) {
			if (static_cast<TraceRestrictSlotGroupID>(slot_id) == id) selected = id;
			SetDParam(0, id);
			dlist.push_back(MakeDropDownListIndentStringItem(indent, STR_TRACE_RESTRICT_SLOT_GROUP_NAME, id, false));
		} else if (indent == 0 || sg->vehicle_type == vehtype) {
			SetDParam(0, id);
			dlist.push_back(std::make_unique<DropDownUnselectable<DropDownListIndentStringItem>>(indent, STR_TRACE_RESTRICT_SLOT_GROUP_NAME_DOWN, id, false));
		} else {
			SetDParam(0, STR_REPLACE_VEHICLE_TRAIN + sg->vehicle_type);
			SetDParam(1, id);
			dlist.push_back(std::make_unique<DropDownUnselectable<DropDownListIndentStringItem>>(indent, STR_TRACE_RESTRICT_SLOT_GROUP_NAME_DOWN_PREFIXED, id, false));
		}
	};

	if (recently_used && !group_only_mode) {
		for (TraceRestrictSlotID id : _recent_slots[vehtype]) {
			add_slot(TraceRestrictSlot::Get(id), id, 0);
		}
		return;
	}

	std::vector<SlotItem> list;
	robin_hood::unordered_set<TraceRestrictSlotGroupID> seen_parents;

	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		if (slot->owner != owner) continue;
		if (!show_other_types && slot->vehicle_type != vehtype) continue;
		if (public_only && !HasFlag(slot->flags, TraceRestrictSlot::Flags::Public)) continue;

		if (!group_only_mode) {
			list.push_back({ SlotItemType::Slot, slot->index });
		}

		TraceRestrictSlotGroupID parent = slot->parent_group;
		while (parent != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
			auto res = seen_parents.insert(parent);
			if (!res.second) {
				/* Insert did not succeeded, was in set previously */
				break;
			}
			TraceRestrictSlotGroup *slot_group = TraceRestrictSlotGroup::GetIfValid(parent);
			if (slot_group == nullptr) break;
			list.push_back({ SlotItemType::Group, parent });
			parent = slot_group->parent;
		}
	}

	if (recently_used && group_only_mode) {
		for (TraceRestrictSlotGroupID id : _recent_slot_groups[vehtype]) {
			if (seen_parents.count(id) > 0) {
				add_group(TraceRestrictSlotGroup::Get(id), id, 0);
			}
		}
		return;
	}

	/* Sort the slots/groups by the vehicle type (if in use), then their parent group, then their name */
	std::sort(list.begin(), list.end(), [&](const SlotItem &a_item, const SlotItem &b_item) {
		SlotItemInfo a = a_item.GetInfo();
		SlotItemInfo b = b_item.GetInfo();

		if (a.vehicle_type != b.vehicle_type) {
			if (a.vehicle_type == vehtype) return true;
			if (b.vehicle_type == vehtype) return false;
			return a.vehicle_type < b.vehicle_type;
		}

		if (a.parent != b.parent) return a.parent < b.parent;

		int r = StrNaturalCompare(a.name, b.name); // Sort by name (natural sorting).
		if (r == 0) return a_item < b_item;
		return r < 0;
	});

	auto output_items = y_combinator([&](auto output_items, uint indent, TraceRestrictSlotGroupID parent_filter) -> void {
		for (const SlotItem &item : list) {
			if (item.type == SlotItemType::Slot) {
				const TraceRestrictSlot *slot = TraceRestrictSlot::Get(item.id);
				if (slot->parent_group != parent_filter) continue;
				add_slot(slot, item.id, indent);
			} else if (item.type == SlotItemType::Group) {
				const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::Get(item.id);
				if (sg->parent != parent_filter) continue;
				add_group(sg, item.id, indent);

				if (seen_parents.count(item.id)) {
					/* Output child items */
					output_items(indent + 1, item.id);
				}
			}
		}
	});
	output_items(0, INVALID_TRACE_RESTRICT_SLOT_GROUP);
}

/**
 * Get a DropDownList of the slot list
 */
DropDownList GetSlotDropDownList(Owner owner, TraceRestrictSlotID slot_id, int &selected, VehicleType vehtype, bool show_other_types)
{
	DropDownList dlist;

	if (_shift_pressed && _settings_game.economy.infrastructure_sharing[vehtype]) {
		for (const Company *c : Company::Iterate()) {
			if (c->index == owner) continue;

			int cselected;
			DropDownList clist;
			GetSlotDropDownListIntl(clist, c->index, slot_id, cselected, vehtype, show_other_types, false, true, false);
			if (clist.empty()) continue;

			if (!dlist.empty()) dlist.push_back(MakeDropDownListDividerItem());
			dlist.push_back(MakeCompanyDropDownListItem(c->index, false));

			if (cselected != -1) selected = cselected;
			dlist.insert(dlist.end(), std::make_move_iterator(clist.begin()), std::make_move_iterator(clist.end()));
		}
	} else {
		std::unique_ptr<DropDownListStringItem> new_item = std::make_unique<DropDownListStringItem>(STR_TRACE_RESTRICT_SLOT_CREATE_CAPTION, NEW_TRACE_RESTRICT_SLOT_ID, false);
		new_item->SetColourFlags(TC_FORCED);
		dlist.emplace_back(std::move(new_item));
		dlist.push_back(MakeDropDownListDividerItem());

		GetSlotDropDownListIntl(dlist, owner, slot_id, selected, vehtype, show_other_types, _ctrl_pressed, false, false);
	}

	return dlist;
}

/**
 * Get a DropDownList of the slot group list
 */
DropDownList GetSlotGroupDropDownList(Owner owner, TraceRestrictSlotGroupID slot_group_id, int &selected, VehicleType vehtype)
{
	DropDownList dlist;

	if (_shift_pressed && _settings_game.economy.infrastructure_sharing[vehtype]) {
		for (const Company *c : Company::Iterate()) {
			if (c->index == owner) continue;

			int cselected;
			DropDownList clist;
			GetSlotDropDownListIntl(clist, c->index, static_cast<TraceRestrictSlotID>(slot_group_id), cselected, vehtype, false, false, true, true);
			if (clist.empty()) continue;

			if (!dlist.empty()) dlist.push_back(MakeDropDownListDividerItem());
			dlist.push_back(MakeCompanyDropDownListItem(c->index, false));

			if (cselected != -1) selected = cselected;
			dlist.insert(dlist.end(), std::make_move_iterator(clist.begin()), std::make_move_iterator(clist.end()));
		}
	} else {
		GetSlotDropDownListIntl(dlist, owner, static_cast<TraceRestrictSlotID>(slot_group_id), selected, vehtype, false, _ctrl_pressed, false, true);
	}

	return dlist;
}

/** Sort counters by their name */
static bool CounterNameSorter(const TraceRestrictCounter * const &a, const TraceRestrictCounter * const &b)
{
	int r = StrNaturalCompare(a->name, b->name); // Sort by name (natural sorting).
	if (r == 0) return a->index < b->index;
	return r < 0;
}

static void GetCounterDropDownListIntl(DropDownList &dlist, Owner owner, TraceRestrictCounterID ctr_id, int &selected, bool recently_used, bool public_only)
{
	GUIList<const TraceRestrictCounter*> list;

	if (recently_used) {
		for (TraceRestrictCounterID id : _recent_counters) {
			list.push_back(TraceRestrictCounter::Get(id));
		}
	} else {
		for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
			if (public_only && !HasFlag(ctr->flags, TraceRestrictCounter::Flags::Public)) continue;
			if (ctr->owner == owner) {
				list.push_back(ctr);
			}
		}

		if (list.size() > 0) {
			list.ForceResort();
			list.Sort(&CounterNameSorter);
		}
	}

	selected = -1;

	for (const TraceRestrictCounter *s : list) {
		if (ctr_id == s->index) selected = ctr_id;
		SetDParam(0, s->index);
		dlist.push_back(MakeDropDownListStringItem(STR_TRACE_RESTRICT_COUNTER_NAME, s->index, false));
	}
}

/**
 * Get a DropDownList of the counter list
 */
DropDownList GetCounterDropDownList(Owner owner, TraceRestrictCounterID ctr_id, int &selected)
{
	DropDownList dlist;

	if (_shift_pressed && _settings_game.economy.infrastructure_sharing[VEH_TRAIN]) {
		for (const Company *c : Company::Iterate()) {
			if (c->index == owner) continue;

			int cselected;
			DropDownList clist;
			GetCounterDropDownListIntl(clist, c->index, ctr_id, cselected, false, true);
			if (clist.empty()) continue;

			if (!dlist.empty()) dlist.push_back(MakeDropDownListDividerItem());
			dlist.push_back(MakeCompanyDropDownListItem(c->index, false));

			if (cselected != -1) selected = cselected;
			dlist.insert(dlist.end(), std::make_move_iterator(clist.begin()), std::make_move_iterator(clist.end()));
		}
	} else {
		std::unique_ptr<DropDownListStringItem> new_item = std::make_unique<DropDownListStringItem>(STR_TRACE_RESTRICT_COUNTER_CREATE_CAPTION, NEW_TRACE_RESTRICT_COUNTER_ID, false);
		new_item->SetColourFlags(TC_FORCED);
		dlist.emplace_back(std::move(new_item));
		dlist.push_back(MakeDropDownListDividerItem());

		GetCounterDropDownListIntl(dlist, owner, ctr_id, selected, _ctrl_pressed, false);
	}

	return dlist;
}

static const StringID _cargo_cond_ops_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_CARGO_EQUALS,
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_CARGO_NOT_EQUALS,
};
static const uint _cargo_cond_ops_val[] = {
	TRCO_IS,
	TRCO_ISNOT,
};
/** cargo conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _cargo_cond_ops = {
	_cargo_cond_ops_str, _cargo_cond_ops_val,
};

static const StringID _train_status_cond_ops_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_HAS_STATUS,
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_DOESNT_HAVE_STATUS,
};
static const uint _train_status_cond_ops_val[] = {
	TRCO_IS,
	TRCO_ISNOT,
};
/** train status conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _train_status_cond_ops = {
	_train_status_cond_ops_str, _train_status_cond_ops_val,
};

static const StringID _passes_through_cond_ops_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_PASS,
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_DOESNT_PASS,
};
static const uint _passes_through_cond_ops_val[] = {
	TRCO_IS,
	TRCO_ISNOT,
};
/** passes through conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _passes_through_cond_ops = {
	_passes_through_cond_ops_str, _passes_through_cond_ops_val,
};

static const StringID _slot_op_subtypes_str[] = {
	STR_TRACE_RESTRICT_SLOT_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_RELEASE_FRONT,
	STR_TRACE_RESTRICT_SLOT_RELEASE_BACK,
	STR_TRACE_RESTRICT_SLOT_RELEASE_ON_RESERVE,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE,
};
static const uint _slot_op_subtypes_val[] = {
	TRSCOF_ACQUIRE_WAIT,
	TRSCOF_ACQUIRE_TRY,
	TRSCOF_RELEASE_FRONT,
	TRSCOF_RELEASE_BACK,
	TRSCOF_RELEASE_ON_RESERVE,
	TRSCOF_PBS_RES_END_ACQ_WAIT,
	TRSCOF_PBS_RES_END_ACQ_TRY,
	TRSCOF_PBS_RES_END_RELEASE,
};
/** slot op subtypes dropdown list set */
static const TraceRestrictDropDownListSet _slot_op_subtypes = {
	_slot_op_subtypes_str, _slot_op_subtypes_val,
};

static const StringID _slot_group_op_subtypes_str[] = {
	STR_TRACE_RESTRICT_SLOT_RELEASE_FRONT,
	STR_TRACE_RESTRICT_SLOT_RELEASE_BACK,
	STR_TRACE_RESTRICT_SLOT_RELEASE_ON_RESERVE,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE,
};
static const uint _slot_group_op_subtypes_val[] = {
	TRSCOF_RELEASE_FRONT,
	TRSCOF_RELEASE_BACK,
	TRSCOF_RELEASE_ON_RESERVE,
	TRSCOF_PBS_RES_END_RELEASE,
};
/** slot group op subtypes dropdown list set */
static const TraceRestrictDropDownListSet _slot_group_op_subtypes = {
	_slot_group_op_subtypes_str, _slot_group_op_subtypes_val,
};

static const StringID _counter_op_cond_ops_str[] = {
	STR_TRACE_RESTRICT_COUNTER_INCREASE,
	STR_TRACE_RESTRICT_COUNTER_DECREASE,
	STR_TRACE_RESTRICT_COUNTER_SET,
};
static const uint _counter_op_cond_ops_val[] = {
	TRCCOF_INCREASE,
	TRCCOF_DECREASE,
	TRCCOF_SET,
};
/** counter operators dropdown list set */
static const TraceRestrictDropDownListSet _counter_op_cond_ops = {
	_counter_op_cond_ops_str, _counter_op_cond_ops_val,
};

/**
 * Get the StringID for a given CargoType @p cargo, or STR_NEWGRF_INVALID_CARGO
 */
static StringID GetCargoStringByID(CargoType cargo)
{
	const CargoSpec *cs = CargoSpec::Get(cargo);
	return cs->IsValid() ? cs->name : STR_NEWGRF_INVALID_CARGO;
}

/**
 * Get the StringID for a given item type @p type
 */
static StringID GetTypeString(TraceRestrictInstructionItem item)
{
	TraceRestrictGuiItemType type = GetItemGuiType(item);
	for (const TraceRestrictDropDownListItem &item : GetTypeDropDownListItems(type)) {
		if (item.type == type) return item.str;
	}

	NOT_REACHED();
}

/**
 * Get the conditional operator field drop down list set for a given type property set @p properties
 */
static const TraceRestrictDropDownListSet *GetCondOpDropDownListSet(TraceRestrictTypePropertySet properties)
{
	static const StringID str_long[] = {
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_EQUALS,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_NOT_EQUALS,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_LESS_THAN,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_LESS_EQUALS,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_MORE_THAN,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_MORE_EQUALS,
	};
	static const uint val_long[] = {
		TRCO_IS,
		TRCO_ISNOT,
		TRCO_LT,
		TRCO_LTE,
		TRCO_GT,
		TRCO_GTE,
	};
	static const TraceRestrictDropDownListSet set_long = {
		str_long, val_long,
	};

	static const StringID str_short[] = {
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_EQUALS,
		STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_NOT_EQUALS,
	};
	static const uint val_short[] = {
		TRCO_IS,
		TRCO_ISNOT,
	};
	static const TraceRestrictDropDownListSet set_short = {
		str_short, val_short,
	};

	if (properties.value_type == TRVT_CARGO_ID) return &_cargo_cond_ops;
	if (properties.value_type == TRVT_TRAIN_STATUS) return &_train_status_cond_ops;
	if (properties.value_type == TRVT_ENGINE_CLASS) return &_train_status_cond_ops;
	if (properties.value_type == TRVT_TILE_INDEX_THROUGH) return &_passes_through_cond_ops;

	switch (properties.cond_type) {
		case TRCOT_NONE:
			return nullptr;

		case TRCOT_BINARY:
			return &set_short;

		case TRCOT_ALL:
			return &set_long;
	}
	NOT_REACHED();
}

/**
 * Return true if item type field @p type is an integer value type
 */
static bool IsIntegerValueType(TraceRestrictValueType type)
{
	switch (type) {
		case TRVT_INT:
		case TRVT_WEIGHT:
		case TRVT_POWER:
		case TRVT_FORCE:
		case TRVT_PERCENT:
			return true;

		case TRVT_SPEED:
			return _settings_game.locale.units_velocity != 3;

		default:
			return false;
	}
}

/**
 * Return true if item type field @p type is a decimal value type
 */
static bool IsDecimalValueType(TraceRestrictValueType type)
{
	switch (type) {
		case TRVT_POWER_WEIGHT_RATIO:
		case TRVT_FORCE_WEIGHT_RATIO:
			return true;

		case TRVT_SPEED:
			return _settings_game.locale.units_velocity == 3;

		default:
			return false;
	}
}

/**
 * Convert integer values or custom penalty values between internal units and display units
 */
static uint ConvertIntegerValue(TraceRestrictValueType type, uint in, bool to_display)
{
	switch (type) {
		case TRVT_INT:
			return in;

		case TRVT_SPEED:
			return to_display
					? ConvertKmhishSpeedToDisplaySpeed(in, VEH_TRAIN)
					: ConvertDisplaySpeedToKmhishSpeed(in, VEH_TRAIN);

		case TRVT_WEIGHT:
			return to_display
					? ConvertWeightToDisplayWeight(in)
					: ConvertDisplayWeightToWeight(in);
			break;

		case TRVT_POWER:
			return to_display
					? ConvertPowerToDisplayPower(in)
					: ConvertDisplayPowerToPower(in);
			break;

		case TRVT_FORCE:
			return to_display
					? ConvertForceToDisplayForce(static_cast<int64_t>(in) * 1000)
					: static_cast<uint>(ConvertDisplayForceToForce(in) / 1000);
			break;

		case TRVT_PF_PENALTY:
			return in;

		case TRVT_PERCENT:
			if (!to_display && in > 100) return 100;
			return in;

		default:
			NOT_REACHED();
			return 0;
	}
}

/**
 * Convert integer values to decimal display units
 */
static void ConvertValueToDecimal(TraceRestrictValueType type, uint in, int64_t &value, int64_t &decimal)
{
	switch (type) {
		case TRVT_POWER_WEIGHT_RATIO:
			ConvertPowerWeightRatioToDisplay(in, value, decimal);
			break;

		case TRVT_FORCE_WEIGHT_RATIO:
			ConvertForceWeightRatioToDisplay(static_cast<int64_t>(in) * 1000, value, decimal);
			break;

		case TRVT_SPEED:
			decimal = _settings_game.locale.units_velocity == 3 ? 1 : 0;
			value = ConvertKmhishSpeedToDisplaySpeed(in, VEH_TRAIN);
			break;

		default:
			NOT_REACHED();
	}
}

/**
 * Convert decimal (double) display units to integer values
 */
static uint ConvertDecimalToValue(TraceRestrictValueType type, double in)
{
	switch (type) {
		case TRVT_POWER_WEIGHT_RATIO:
			return ConvertDisplayToPowerWeightRatio(in);

		case TRVT_FORCE_WEIGHT_RATIO:
			return ConvertDisplayToForceWeightRatio(in) / 1000;

		case TRVT_SPEED:
			return ConvertDisplaySpeedToKmhishSpeed(in * (_settings_game.locale.units_velocity == 3 ? 10 : 1), VEH_TRAIN);

		default:
			NOT_REACHED();
			return 0;
	}
}

/** String values for TraceRestrictCondFlags, value gives offset into array */
static const StringID _program_cond_type[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_IF,                      // TRCF_DEFAULT
	STR_TRACE_RESTRICT_CONDITIONAL_ELIF,                    // TRCF_ELSE
	STR_TRACE_RESTRICT_CONDITIONAL_ORIF,                    // TRCF_OR
};

/** condition flags field drop down value types */
enum CondFlagsDropDownType : uint8_t {
	CFDDT_ELSE = 0,           ///< This is an else block
	CFDDT_ELIF = TRCF_ELSE,   ///< This is an else-if block
	CFDDT_ORIF = TRCF_OR,     ///< This is an or-if block
};

static const uint32_t _condflags_dropdown_else_hide_mask = 1;     ///< disable bitmask for CFDDT_ELSE
static const uint32_t _condflags_dropdown_else_if_hide_mask = 6;  ///< disable bitmask for CFDDT_ELIF and CFDDT_ORIF

static const StringID _condflags_dropdown_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_ELSE,
	STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
};
static const uint _condflags_dropdown_val[] = {
	CFDDT_ELSE,
	CFDDT_ELIF,
	CFDDT_ORIF,
};
/** condition flags dropdown list set */
static const TraceRestrictDropDownListSet _condflags_dropdown = {
	_condflags_dropdown_str, _condflags_dropdown_val,
};

static const StringID _pf_penalty_dropdown_str[] = {
	STR_TRACE_RESTRICT_PF_VALUE_SMALL,
	STR_TRACE_RESTRICT_PF_VALUE_MEDIUM,
	STR_TRACE_RESTRICT_PF_VALUE_LARGE,
	STR_TRACE_RESTRICT_PF_VALUE_CUSTOM,
};
static const uint _pf_penalty_dropdown_val[] = {
	TRPPPI_SMALL,
	TRPPPI_MEDIUM,
	TRPPPI_LARGE,
	TRPPPI_END,  // this is a placeholder for "custom"
};
/** Pathfinder penalty dropdown set */
static const TraceRestrictDropDownListSet _pf_penalty_dropdown = {
	_pf_penalty_dropdown_str, _pf_penalty_dropdown_val,
};

static uint GetPathfinderPenaltyDropdownIndex(TraceRestrictInstructionItem item)
{
	switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(item.GetAuxField())) {
		case TRPPAF_VALUE:
			return TRPPPI_END;

		case TRPPAF_PRESET: {
			uint16_t index = item.GetValue();
			assert(index < TRPPPI_END);
			return index;
		}

		default:
			NOT_REACHED();
	}
}

template <typename F>
void IterateActionsInsideConditional(const TraceRestrictProgram *prog, int index, F handler)
{
	int depth = 1;
	for (auto iter = TraceRestrictInstructionIteratorAt(prog->items, index); iter < prog->items.end(); ++iter) {
		TraceRestrictInstructionItem item = iter.Instruction();
		if (item.IsConditional()) {
			if (item.GetCondFlags() & (TRCF_ELSE | TRCF_OR)) {
				/* do nothing */
			} else if (item.GetType() == TRIT_COND_ENDIF) {
				depth--;
				if (depth == 0) return;
			} else {
				depth++;
			}
		} else {
			handler(item);
		}
	}
}

/** Common function for drawing an ordinary conditional instruction */
static void DrawInstructionStringConditionalCommon(TraceRestrictInstructionItem item, const TraceRestrictTypePropertySet &properties)
{
	assert(item.GetCondFlags() <= TRCF_OR);
	SetDParam(0, _program_cond_type[item.GetCondFlags()]);
	SetDParam(1, GetTypeString(item));
	SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
}

/** Common function for drawing an integer conditional instruction */
static void DrawInstructionStringConditionalIntegerCommon(TraceRestrictInstructionItem item, const TraceRestrictTypePropertySet &properties)
{
	DrawInstructionStringConditionalCommon(item, properties);
	SetDParam(3, item.GetValue());
}

/** Common function for drawing an integer conditional instruction with an invalid value */
static void DrawInstructionStringConditionalInvalidValue(TraceRestrictInstructionItem item, const TraceRestrictTypePropertySet &properties, StringID &instruction_string, bool selected)
{
	instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_UNDEFINED;
	DrawInstructionStringConditionalCommon(item, properties);
}

StringID GetSlotGroupWarning(TraceRestrictSlotID slot_group, Owner owner)
{
	const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(slot_group);
	if (sg == nullptr) return STR_NULL;

	if (sg->contained_slots.empty()) return STR_TRACE_RESTRICT_SLOT_GROUP_EMPTY_WARNING;

	if (sg->owner != owner) {
		for (TraceRestrictSlotID slot_id : sg->contained_slots) {
			if (!HasFlag(TraceRestrictSlot::Get(slot_id)->flags, TraceRestrictSlot::Flags::Public)) {
				return STR_TRACE_RESTRICT_SLOT_GROUP_NON_PUBLIC_WARNING;
			}
		}
	}

	return STR_NULL;
}

enum class DrawInstructionStringFlag : uint8_t {
	TunnelBridgeEntrance, ///< Tunnel/bridge entrance present
	TunnelBridgeExit,     ///< Tunnel/bridge exit present
};
using DrawInstructionStringFlags = EnumBitSet<DrawInstructionStringFlag, uint8_t>;

/**
 * Draws an instruction in the programming GUI
 * @param prog The program (may be nullptr)
 * @param item The instruction to draw
 * @param index The instruction index
 * @param y Y position for drawing
 * @param selected True, if the order is selected
 * @param indent How many levels the instruction is indented
 * @param left Left border for text drawing
 * @param right Right border for text drawing
 * @param owner Owning company ID
 * @param flags Flags
 */
static void DrawInstructionString(const TraceRestrictProgram *prog, TraceRestrictInstructionRecord instruction_record,
		int index, int y, bool selected, int indent, int left, int right, Owner owner, DrawInstructionStringFlags flags)
{
	StringID instruction_string = INVALID_STRING_ID;

	TraceRestrictInstructionItem item = instruction_record.instruction;
	TraceRestrictTypePropertySet properties = GetTraceRestrictTypeProperties(item);

	if (item.IsConditional()) {
		if (item.GetType() == TRIT_COND_ENDIF) {
			if (item.GetCondFlags() & TRCF_ELSE) {
				instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ELSE;
			} else {
				instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENDIF;
			}
		} else if (item.GetType() == TRIT_COND_UNDEFINED) {
			instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_UNDEFINED;
			SetDParam(0, _program_cond_type[item.GetCondFlags()]);
		} else {
			auto insert_warning = [&](uint dparam_index, StringID warning) {
				auto tmp_params = MakeParameters(GetDParam(dparam_index));
				_temp_special_strings[0] = GetStringWithArgs(warning, tmp_params);
				SetDParam(dparam_index, SPECSTR_TEMP_START);
			};

			switch (properties.value_type) {
				case TRVT_INT:
				case TRVT_PERCENT:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					if (item.GetType() == TRIT_COND_RESERVED_TILES && _settings_game.vehicle.train_braking_model != TBM_REALISTIC) {
						insert_warning(1, STR_TRACE_RESTRICT_WARNING_REQUIRES_REALISTIC_BRAKING);
					}
					break;

				case TRVT_SPEED:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_SPEED;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_ORDER: {
					switch (static_cast<TraceRestrictOrderCondAuxField>(item.GetAuxField())) {
						case TROCAF_STATION:
							if (item.GetValue() != INVALID_STATION) {
								instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ORDER_STATION;
								DrawInstructionStringConditionalIntegerCommon(item, properties);
							} else {
								/* This is an invalid station, use a separate string */
								DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
							}
							break;

						case TROCAF_WAYPOINT:
							instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ORDER_WAYPOINT;
							DrawInstructionStringConditionalIntegerCommon(item, properties);
							break;

						case TROCAF_DEPOT:
							instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ORDER_DEPOT;
							DrawInstructionStringConditionalCommon(item, properties);
							SetDParam(3, VEH_TRAIN);
							SetDParam(4, item.GetValue());
							break;

						default:
							NOT_REACHED();
							break;
					}
					break;
				}

				case TRVT_CARGO_ID:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_CARGO;
					assert(item.GetCondFlags() <= TRCF_OR);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(&_cargo_cond_ops, item.GetCondOp()));
					SetDParam(2, GetCargoStringByID(item.GetValue()));
					break;

				case TRVT_DIRECTION:
					if (item.GetValue() >= TRDTSV_TUNBRIDGE_ENTER) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_SIGNAL_TYPE;
					} else if (item.GetValue() >= TRDTSV_FRONT) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_SIGNAL_FACE;
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_DIRECTION;
					}
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					SetDParam(2, GetDropDownStringByValue(&_direction_value, item.GetValue()));
					break;

				case TRVT_TILE_INDEX: {
					assert(prog != nullptr);
					assert(item.GetType() == TRIT_COND_PBS_ENTRY_SIGNAL);
					TileIndex tile{instruction_record.secondary};
					if (tile == INVALID_TILE) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TILE_INDEX;
						SetDParam(0, _program_cond_type[item.GetCondFlags()]);
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
						SetDParam(3, TileX(tile));
						SetDParam(4, TileY(tile));
					}
					auto check_signal_mode_control = [&](bool allowed) {
						bool warn = false;
						IterateActionsInsideConditional(prog, index, [&](const TraceRestrictInstructionItem &item) {
							if ((item.GetType() == TRIT_SIGNAL_MODE_CONTROL) != allowed) warn = true;
						});
						if (warn) insert_warning(1, allowed ? STR_TRACE_RESTRICT_WARNING_SIGNAL_MODE_CONTROL_ONLY : STR_TRACE_RESTRICT_WARNING_NO_SIGNAL_MODE_CONTROL);
					};
					switch (static_cast<TraceRestrictPBSEntrySignalAuxField>(item.GetAuxField())) {
						case TRPESAF_VEH_POS:
							SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_PBS_ENTRY_SIGNAL_LONG);
							check_signal_mode_control(false);
							break;

						case TRPESAF_RES_END:
							SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_SIGNAL_LONG);
							check_signal_mode_control(false);
							if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC) insert_warning(1, STR_TRACE_RESTRICT_WARNING_REQUIRES_REALISTIC_BRAKING);
							break;

						case TRPESAF_RES_END_TILE:
							SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_TILE_LONG);
							check_signal_mode_control(true);
							if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC) insert_warning(1, STR_TRACE_RESTRICT_WARNING_REQUIRES_REALISTIC_BRAKING);
							break;

						default:
							NOT_REACHED();
					}

					break;
				}

				case TRVT_TILE_INDEX_THROUGH: {
					assert(prog != nullptr);
					assert(item.GetType() == TRIT_COND_RESERVATION_THROUGH);
					TileIndex tile{instruction_record.secondary};
					if (tile == INVALID_TILE) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_PASSES_TILE_INDEX;
						SetDParam(0, _program_cond_type[item.GetCondFlags()]);
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
						SetDParam(3, TileX(tile));
						SetDParam(4, TileY(tile));
					}
					SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_RESERVATION_THROUGH_SHORT);
					break;
				}

				case TRVT_GROUP_INDEX: {
					assert(item.GetCondFlags() <= TRCF_OR);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					if (item.GetValue() == INVALID_GROUP) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					} else if (item.GetValue() == DEFAULT_GROUP) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP_STR;
						SetDParam(2, STR_GROUP_DEFAULT_TRAINS);
					} else {
						const Group *g = Group::GetIfValid(item.GetValue());
						if (g != nullptr && g->owner != owner) {
							instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP_STR;
							auto tmp_params = MakeParameters(item.GetValue() | GROUP_NAME_HIERARCHY, g->owner);
							_temp_special_strings[0] = GetStringWithArgs(STR_TRACE_RESTRICT_OTHER_COMPANY_GROUP, tmp_params);
							SetDParam(2, SPECSTR_TEMP_START);
						} else {
							instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP;
							SetDParam(2, item.GetValue() | GROUP_NAME_HIERARCHY);
						}
					}
					break;
				}

				case TRVT_OWNER: {
					assert(item.GetCondFlags() <= TRCF_OR);
					CompanyID cid = static_cast<CompanyID>(item.GetValue());
					if (cid == INVALID_COMPANY) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_OWNER;
						SetDParam(0, _program_cond_type[item.GetCondFlags()]);
						SetDParam(1, GetTypeString(item));
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
						SetDParam(3, cid);
						SetDParam(4, cid);
					}
					break;
				}

				case TRVT_WEIGHT:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_WEIGHT;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_POWER:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_POWER;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_FORCE:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_FORCE;
					DrawInstructionStringConditionalCommon(item, properties);
					SetDParam(3, item.GetValue() * 1000);
					break;

				case TRVT_POWER_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_POWER_WEIGHT_RATIO;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_FORCE_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_FORCE_WEIGHT_RATIO;
					DrawInstructionStringConditionalCommon(item, properties);
					SetDParam(3, item.GetValue() * 1000);
					break;

				case TRVT_SLOT_INDEX:
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					if (item.GetValue() == INVALID_TRACE_RESTRICT_SLOT_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT;
						SetDParam(2, item.GetValue());
					}
					break;

				case TRVT_SLOT_INDEX_INT: {
					assert(prog != nullptr);
					assert(item.GetType() == TRIT_COND_SLOT_OCCUPANCY);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, item.GetAuxField() ? STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_REMAINING_SHORT : STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_SHORT);
					if (item.GetValue() == INVALID_TRACE_RESTRICT_SLOT_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_OCCUPANCY_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_OCCUPANCY;
						SetDParam(2, item.GetValue());
					}
					SetDParam(3, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					SetDParam(4, instruction_record.secondary);
					break;
				}

				case TRVT_SLOT_GROUP_INDEX:
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_GROUP;
					if (item.GetValue() == INVALID_TRACE_RESTRICT_SLOT_GROUP) {
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					} else {
						StringID warning = GetSlotGroupWarning(item.GetValue(), owner);
						if (warning != STR_NULL) {
							SetDParam(2, warning);
						} else {
							SetDParam(2, STR_TRACE_RESTRICT_SLOT_GROUP_NAME);
						}
						SetDParam(3, item.GetValue());
					}
					break;

				case TRVT_TRAIN_STATUS:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TRAIN_STATUS;
					assert(item.GetCondFlags() <= TRCF_OR);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(&_train_status_cond_ops, item.GetCondOp()));
					SetDParam(2, GetDropDownStringByValue(&_train_status_value, item.GetValue()));
					break;

				case TRVT_COUNTER_INDEX_INT: {
					assert(prog != nullptr);
					assert(item.GetType() == TRIT_COND_COUNTER_VALUE);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					if (item.GetValue() == INVALID_TRACE_RESTRICT_COUNTER_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COUNTER_STR;
						SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COUNTER;
						SetDParam(1, item.GetValue());
					}
					SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					SetDParam(3, instruction_record.secondary);
					break;
				}

				case TRVT_TIME_DATE_INT: {
					assert(prog != nullptr);
					assert(item.GetType() == TRIT_COND_TIME_DATE_VALUE);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					instruction_string = item.GetValue() == TRTDVF_HOUR_MINUTE ? STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_TIME_HHMM : STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;
					SetDParam(1, STR_TRACE_RESTRICT_TIME_MINUTE_ITEM + item.GetValue());
					SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					SetDParam(3, instruction_record.secondary);
					break;
				}

				case TRVT_ENGINE_CLASS:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENGINE_CLASSES;
					assert(item.GetCondFlags() <= TRCF_OR);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(&_train_status_cond_ops, item.GetCondOp()));
					SetDParam(2, GetDropDownStringByValue(&_engine_class_value, item.GetValue()));
					break;

				case TRVT_ORDER_TARGET_DIAGDIR:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TARGET_DIRECTION;
					assert(item.GetCondFlags() <= TRCF_OR);
					SetDParam(0, _program_cond_type[item.GetCondFlags()]);
					SetDParam(1, GetDropDownStringByValue(&_target_direction_aux_value, item.GetAuxField()));
					SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), item.GetCondOp()));
					SetDParam(3, GetDropDownStringByValue(&_diagdir_value, item.GetValue()));
					break;

				default:
					NOT_REACHED();
					break;
			}
		}
	} else {
		switch (item.GetType()) {
			case TRIT_NULL:
				switch (item.GetValue()) {
					case TRNTSV_START:
						instruction_string = STR_TRACE_RESTRICT_START;
						break;

					case TRNTSV_END:
						instruction_string = STR_TRACE_RESTRICT_END;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_PF_DENY:
				instruction_string = item.GetValue() ? STR_TRACE_RESTRICT_PF_ALLOW_LONG : STR_TRACE_RESTRICT_PF_DENY;
				break;

			case TRIT_PF_PENALTY:
				switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(item.GetAuxField())) {
					case TRPPAF_VALUE:
						instruction_string = STR_TRACE_RESTRICT_PF_PENALTY_ITEM;
						SetDParam(0, item.GetValue());
						break;

					case TRPPAF_PRESET: {
						instruction_string = STR_TRACE_RESTRICT_PF_PENALTY_ITEM_PRESET;
						uint16_t idx = item.GetValue();
						assert(idx < TRPPPI_END);
						SetDParam(0, _pf_penalty_dropdown_str[idx]);
						break;
					}

					default:
						NOT_REACHED();
				}
				break;

			case TRIT_RESERVE_THROUGH:
				instruction_string = (item.GetValue() != 0) ? STR_TRACE_RESTRICT_RESERVE_THROUGH_CANCEL : STR_TRACE_RESTRICT_RESERVE_THROUGH;

				if (flags.Any({ DrawInstructionStringFlag::TunnelBridgeEntrance, DrawInstructionStringFlag::TunnelBridgeExit })) {
					SetDParam(0, instruction_string);
					instruction_string = STR_TRACE_RESTRICT_WARNING_NOT_FOR_TUNNEL_BRIDGE;
				}
				break;

			case TRIT_LONG_RESERVE:
				switch (static_cast<TraceRestrictLongReserveValueField>(item.GetValue())) {
					case TRLRVF_LONG_RESERVE:
						instruction_string = STR_TRACE_RESTRICT_LONG_RESERVE;
						break;

					case TRLRVF_CANCEL_LONG_RESERVE:
						instruction_string = STR_TRACE_RESTRICT_LONG_RESERVE_CANCEL;
						break;

					case TRLRVF_LONG_RESERVE_UNLESS_STOPPING:
						instruction_string = STR_TRACE_RESTRICT_LONG_RESERVE_UNLESS_STOPPING;
						break;

					default:
						NOT_REACHED();
						break;
				}
				if (flags.Test(DrawInstructionStringFlag::TunnelBridgeEntrance)) {
					SetDParam(0, instruction_string);
					instruction_string = STR_TRACE_RESTRICT_WARNING_NOT_FOR_TUNNEL_BRIDGE_ENTRANCES;
				}
				break;

			case TRIT_WAIT_AT_PBS:
				switch (static_cast<TraceRestrictWaitAtPbsValueField>(item.GetValue())) {
					case TRWAPVF_WAIT_AT_PBS:
						instruction_string = STR_TRACE_RESTRICT_WAIT_AT_PBS;
						break;

					case TRWAPVF_CANCEL_WAIT_AT_PBS:
						instruction_string = STR_TRACE_RESTRICT_WAIT_AT_PBS_CANCEL;
						break;

					case TRWAPVF_PBS_RES_END_WAIT:
						instruction_string = STR_TRACE_RESTRICT_PBS_RES_END_WAIT;
						break;

					case TRWAPVF_CANCEL_PBS_RES_END_WAIT:
						instruction_string = STR_TRACE_RESTRICT_PBS_RES_END_WAIT_CANCEL;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_SLOT:
				switch (static_cast<TraceRestrictSlotSubtypeField>(item.GetCombinedAuxCondOpField())) {
					case TRSCOF_ACQUIRE_WAIT:
						instruction_string = STR_TRACE_RESTRICT_SLOT_ACQUIRE_WAIT_ITEM;
						break;

					case TRSCOF_ACQUIRE_TRY:
						instruction_string = STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE_ITEM;
						break;

					case TRSCOF_RELEASE_BACK:
						instruction_string = STR_TRACE_RESTRICT_SLOT_RELEASE_BACK_ITEM;
						break;

					case TRSCOF_RELEASE_FRONT:
						instruction_string = STR_TRACE_RESTRICT_SLOT_RELEASE_FRONT_ITEM;
						break;

					case TRSCOF_RELEASE_ON_RESERVE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_RELEASE_ON_RESERVE_ITEM;
						break;

					case TRSCOF_PBS_RES_END_ACQ_WAIT:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_ACQUIRE_WAIT_ITEM;
						break;

					case TRSCOF_PBS_RES_END_ACQ_TRY:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_TRY_ACQUIRE_ITEM;
						break;

					case TRSCOF_PBS_RES_END_RELEASE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE_ITEM;
						break;

					default:
						NOT_REACHED();
						break;
				}
				if (item.GetValue() == INVALID_TRACE_RESTRICT_SLOT_ID) {
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
				} else {
					SetDParam(0, STR_TRACE_RESTRICT_SLOT_NAME);
					SetDParam(1, item.GetValue());
				}
				break;

			case TRIT_SLOT_GROUP:
				switch (static_cast<TraceRestrictSlotSubtypeField>(item.GetCombinedAuxCondOpField())) {
					case TRSCOF_RELEASE_BACK:
						instruction_string = STR_TRACE_RESTRICT_SLOT_GROUP_RELEASE_BACK_ITEM;
						break;

					case TRSCOF_RELEASE_FRONT:
						instruction_string = STR_TRACE_RESTRICT_SLOT_GROUP_RELEASE_FRONT_ITEM;
						break;

					case TRSCOF_RELEASE_ON_RESERVE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_GROUP_RELEASE_ON_RESERVE_ITEM;
						break;

					case TRSCOF_PBS_RES_END_RELEASE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_GROUP_PBS_RES_END_RELEASE_ITEM;
						break;

					default:
						NOT_REACHED();
						break;
				}
				if (item.GetValue() == INVALID_TRACE_RESTRICT_SLOT_GROUP) {
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
				} else {
					StringID warning = GetSlotGroupWarning(item.GetValue(), owner);
					if (warning != STR_NULL) {
						SetDParam(0, warning);
					} else {
						SetDParam(0, STR_TRACE_RESTRICT_SLOT_GROUP_NAME);
					}
					SetDParam(1, item.GetValue());
				}
				break;

			case TRIT_GUI_LABEL:
				instruction_string = STR_TRACE_RESTRICT_GUI_LABEL_ITEM;
				SetDParamStr(0, prog->GetLabel(item.GetValue()));
				break;

			case TRIT_REVERSE:
				switch (static_cast<TraceRestrictReverseValueField>(item.GetValue())) {
					case TRRVF_REVERSE_BEHIND:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_SIG;
						break;

					case TRRVF_CANCEL_REVERSE_BEHIND:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_SIG_CANCEL;
						break;

					case TRRVF_REVERSE_AT:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_AT_SIG;
						break;

					case TRRVF_CANCEL_REVERSE_AT:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_AT_SIG_CANCEL;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_SPEED_RESTRICTION:
				if (item.GetValue() != 0) {
					SetDParam(0, item.GetValue());
					instruction_string = STR_TRACE_RESTRICT_SET_SPEED_RESTRICTION;
				} else {
					instruction_string = STR_TRACE_RESTRICT_REMOVE_SPEED_RESTRICTION;
				}
				break;

			case TRIT_NEWS_CONTROL:
				switch (static_cast<TraceRestrictNewsControlField>(item.GetValue())) {
					case TRNCF_TRAIN_NOT_STUCK:
						instruction_string = STR_TRACE_RESTRICT_TRAIN_NOT_STUCK;
						break;

					case TRNCF_CANCEL_TRAIN_NOT_STUCK:
						instruction_string = STR_TRACE_RESTRICT_TRAIN_NOT_STUCK_CANCEL;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_COUNTER: {
				switch (static_cast<TraceRestrictCounterCondOpField>(item.GetCondOp())) {
					case TRCCOF_INCREASE:
						instruction_string = STR_TRACE_RESTRICT_COUNTER_INCREASE_ITEM;
						break;

					case TRCCOF_DECREASE:
						instruction_string = STR_TRACE_RESTRICT_COUNTER_DECREASE_ITEM;
						break;

					case TRCCOF_SET:
						instruction_string = STR_TRACE_RESTRICT_COUNTER_SET_ITEM;
						break;

					default:
						NOT_REACHED();
						break;
				}
				if (item.GetValue() == INVALID_TRACE_RESTRICT_COUNTER_ID) {
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
				} else {
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_NAME);
					SetDParam(1, item.GetValue());
				}
				SetDParam(2, instruction_record.secondary);
				break;
			}

			case TRIT_PF_PENALTY_CONTROL:
				switch (static_cast<TraceRestrictPfPenaltyControlField>(item.GetValue())) {
					case TRPPCF_NO_PBS_BACK_PENALTY:
						instruction_string = STR_TRACE_RESTRICT_NO_PBS_BACK_PENALTY;
						break;

					case TRPPCF_CANCEL_NO_PBS_BACK_PENALTY:
						instruction_string = STR_TRACE_RESTRICT_NO_PBS_BACK_PENALTY_CANCEL;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_SPEED_ADAPTATION_CONTROL:
				switch (static_cast<TraceRestrictSpeedAdaptationControlField>(item.GetValue())) {
					case TRSACF_SPEED_ADAPT_EXEMPT:
						instruction_string = STR_TRACE_RESTRICT_MAKE_TRAIN_SPEED_ADAPTATION_EXEMPT;
						break;

					case TRSACF_REMOVE_SPEED_ADAPT_EXEMPT:
						instruction_string = STR_TRACE_RESTRICT_REMOVE_TRAIN_SPEED_ADAPTATION_EXEMPT;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_SIGNAL_MODE_CONTROL:
				switch (static_cast<TraceRestrictSignalModeControlField>(item.GetValue())) {
					case TRSMCF_NORMAL_ASPECT:
						instruction_string = STR_TRACE_RESTRICT_USE_NORMAL_ASPECT_MODE;
						break;

					case TRSMCF_SHUNT_ASPECT:
						instruction_string = STR_TRACE_RESTRICT_USE_SHUNT_ASPECT_MODE;
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
	}

	bool rtl = _current_text_dir == TD_RTL;
	TextColour colour = selected ? TC_WHITE : TC_BLACK;
	if (selected && item.GetType() == TRIT_GUI_LABEL) colour |= TC_FORCED;
	DrawString(left + (rtl ? 0 : ScaleGUITrad(indent * 16)), right - (rtl ? ScaleGUITrad(indent * 16) : 0), y, instruction_string, colour);
}


StringID TraceRestrictPrepareSlotCounterSelectTooltip(StringID base_str, VehicleType vtype)
{
	if (_settings_game.economy.infrastructure_sharing[vtype]) {
		SetDParam(0, STR_TRACE_RESTRICT_RECENTLY_USED_TOOLTIP_EXTRA);
		SetDParam(1, base_str);
		return STR_TRACE_RESTRICT_OTHER_COMPANY_TOOLTIP_EXTRA;
	} else {
		SetDParam(0, base_str);
		return STR_TRACE_RESTRICT_RECENTLY_USED_TOOLTIP_EXTRA;
	}
}

/** Main GUI window class */
class TraceRestrictWindow: public Window {
	TileIndex tile;                                                             ///< tile this window is for
	Track track;                                                                ///< track this window is for
	int selected_instruction;                                                   ///< selected instruction index, this is offset by one due to the display of the "start" item
	Scrollbar *vscroll;                                                         ///< scrollbar widget
	btree::btree_map<int, const TraceRestrictDropDownListSet *> drop_down_list_mapping; ///< mapping of widget IDs to drop down list sets
	bool value_drop_down_is_company;                                            ///< TR_WIDGET_VALUE_DROPDOWN is a company list
	TraceRestrictInstructionItem expecting_inserted_item;                       ///< set to instruction when performing an instruction insertion, used to handle selection update on insertion
	int current_placement_widget;                                               ///< which widget has a SetObjectToPlaceWnd, if any
	int current_left_aux_plane;                                                 ///< current plane for TR_WIDGET_SEL_TOP_LEFT_AUX widget
	int base_copy_plane;                                                        ///< base plane for TR_WIDGET_SEL_COPY widget
	int base_share_plane;                                                       ///< base plane for TR_WIDGET_SEL_SHARE widget

	enum QuerySubMode : uint8_t {
		QSM_DEFAULT,
		QSM_NEW_SLOT,
		QSM_NEW_COUNTER,
		QSM_SET_TEXT,
	};
	QuerySubMode query_submode = QSM_DEFAULT;                                   ///< sub-mode for query strings

	void TraceRestrictShowQueryString(std::string_view str, StringID caption, uint maxsize, CharSetFilter afilter, QueryStringFlags flags, QuerySubMode query_submode = QSM_DEFAULT)
	{
		CloseWindowByClass(WC_QUERY_STRING);
		this->query_submode = query_submode;
		ShowQueryString(str, caption, maxsize, this, afilter, flags);
	}

	void PostInstructionCommandAtOffset(uint32_t offset, TraceRestrictDoCommandType type, uint32_t value, StringID error_msg, std::string text = {})
	{
		Command<CMD_PROGRAM_TRACERESTRICT_SIGNAL>::Post(error_msg, this->tile, this->track, type, offset, value, std::move(text));
	}

	inline void PostInstructionCommand(TraceRestrictDoCommandType type, uint32_t value, StringID error_msg, std::string text = {})
	{
		this->PostInstructionCommandAtOffset(this->selected_instruction - 1, type, value, error_msg, std::move(text));
	}

public:
	TraceRestrictWindow(WindowDesc &desc, TileIndex tile, Track track)
			: Window(desc)
	{
		this->tile = tile;
		this->track = track;
		this->selected_instruction = -1;
		this->expecting_inserted_item = {};
		this->current_placement_widget = -1;

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(TR_WIDGET_SCROLLBAR);
		this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_AUX)->SetDisplayedPlane(SZSP_NONE);
		this->current_left_aux_plane = SZSP_NONE;
		this->FinishInitNested(MakeTraceRestrictRefId(tile, track));

		this->ReloadProgramme();
	}

	void Close(int data = 0) override
	{
		extern const TraceRestrictProgram *_viewport_highlight_tracerestrict_program;
		if (_viewport_highlight_tracerestrict_program != nullptr) {
			const TraceRestrictProgram *prog = this->GetProgram();
			if (prog != nullptr && prog == _viewport_highlight_tracerestrict_program) {
				SetViewportCatchmentTraceRestrictProgram(prog, false);
			}
		}
		this->Window::Close();
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST: {
				int sel = this->GetItemIndexFromPt(pt.y);

				if (_ctrl_pressed) {
					/* Scroll to target (for stations, waypoints, depots) */

					if (sel == -1) return;

					TraceRestrictInstructionRecord item = this->GetItem(this->GetProgram(), sel);
					TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item.instruction).value_type;
					if (val_type == TRVT_ORDER) {
						switch (static_cast<TraceRestrictOrderCondAuxField>(item.instruction.GetAuxField())) {
							case TROCAF_STATION:
							case TROCAF_WAYPOINT: {
								const BaseStation *st = BaseStation::GetIfValid(item.instruction.GetValue());
								if (st != nullptr) {
									ScrollMainWindowToTile(st->xy);
								}
								break;
							}

							case TROCAF_DEPOT: {
								const Depot *depot = Depot::GetIfValid(item.instruction.GetValue());
								if (depot != nullptr) {
									ScrollMainWindowToTile(depot->xy);
								}
								break;
							}
						}
					} else if (val_type == TRVT_TILE_INDEX || val_type == TRVT_TILE_INDEX_THROUGH) {
						TileIndex tile{item.secondary};
						if (tile != INVALID_TILE) {
							ScrollMainWindowToTile(tile);
						}
					}
					return;
				}

				this->CloseChildWindows();
				HideDropDownMenu(this);

				if (sel == -1 || this->GetOwner() != _local_company) {
					/* Deselect */
					this->selected_instruction = -1;
				} else {
					this->selected_instruction = sel;
				}

				this->expecting_inserted_item = {};

				this->RaiseButtons();
				this->UpdateButtonState();
				break;
			}

			case TR_WIDGET_INSERT: {
				if (this->GetOwner() != _local_company || this->selected_instruction < 1) {
					return;
				}

				uint32_t disabled = _program_insert_or_if_hide_mask;
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				if (item.GetType() == TRIT_COND_ENDIF ||
						(item.IsConditional() && item.GetCondFlags() != 0)) {
					/* This is either: an else/or if, an else, or an end if
					 * try to include else if, else in insertion list */
					if (!this->ElseInsertionDryRun(false)) disabled |= _program_insert_else_hide_mask;
					if (!this->ElseIfInsertionDryRun(false)) disabled |= _program_insert_else_if_hide_mask;
				} else {
					/* Can't insert else/end if here */
					disabled |= _program_insert_else_hide_mask | _program_insert_else_if_hide_mask;
				}
				if (this->selected_instruction > 1) {
					TraceRestrictInstructionItem prev_item = this->GetItem(this->GetProgram(), this->selected_instruction - 1).instruction;
					if (prev_item.IsConditional() && prev_item.GetType() != TRIT_COND_ENDIF) {
						/* Previous item is either: an if, or an else/or if */

						/* Else if has same validation rules as or if, use it instead of creating another test function */
						if (ElseIfInsertionDryRun(false)) disabled &= ~_program_insert_or_if_hide_mask;
					}
				}

				DropDownList dlist;
				uint i = 0;
				for (StringID str : _program_insert.string_array) {
					dlist.push_back(MakeDropDownListStringItem(str, _program_insert.value_array[i], i < 32 && HasBit(disabled, i)));
					++i;
				}
				for (const TraceRestrictDropDownListItem &item : GetActionDropDownListItems()) {
					if (!ShouldHideTypeDropDownListItem(item.flags)) {
						dlist.push_back(MakeDropDownListStringItem(item.str, item.type, false));
					}
				}
				ShowDropDownList(this, std::move(dlist), 0, TR_WIDGET_INSERT, 0);
				break;
			}

			case TR_WIDGET_REMOVE: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				this->PostInstructionCommand(_ctrl_pressed ? TRDCT_SHALLOW_REMOVE_ITEM : TRDCT_REMOVE_ITEM, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_REMOVE_ITEM);
				break;
			}

			case TR_WIDGET_UP_BTN:
			case TR_WIDGET_DOWN_BTN: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				TraceRestrictProgramSignalMoveFlags move_value{};
				if (widget == TR_WIDGET_UP_BTN) move_value |= TraceRestrictProgramSignalMoveFlags::Up;
				if (_ctrl_pressed) move_value |= TraceRestrictProgramSignalMoveFlags::Shallow;

				uint32_t offset = this->selected_instruction - 1;
				this->IsUpDownBtnUsable(widget == TR_WIDGET_UP_BTN, true); // Modifies this->selected_instruction

				this->PostInstructionCommandAtOffset(offset, TRDCT_MOVE_ITEM, to_underlying(move_value), STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
				break;
			}

			case TR_WIDGET_DUPLICATE: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				this->expecting_inserted_item = item;
				this->PostInstructionCommand(TRDCT_DUPLICATE_ITEM, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
				break;
			}

			case TR_WIDGET_CONDFLAGS: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				CondFlagsDropDownType type;
				if (item.GetType() == TRIT_COND_ENDIF) {
					if (item.GetCondFlags() == 0) return; // end if
					type = CFDDT_ELSE;
				} else if (item.IsConditional() && item.GetCondFlags() != 0) {
					type = static_cast<CondFlagsDropDownType>(item.GetCondFlags());
				} else {
					return;
				}

				uint32_t disabled = 0;
				if (!this->ElseInsertionDryRun(true)) disabled |= _condflags_dropdown_else_hide_mask;
				if (!this->ElseIfInsertionDryRun(true)) disabled |= _condflags_dropdown_else_if_hide_mask;

				this->ShowDropDownListWithValue(&_condflags_dropdown, type, false, TR_WIDGET_CONDFLAGS, disabled, 0);
				break;
			}

			case TR_WIDGET_TYPE_COND:
			case TR_WIDGET_TYPE_NONCOND: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				TraceRestrictGuiItemType type = GetItemGuiType(item);

				if (type != TRIT_NULL) {
					DropDownList dlist;
					for (const TraceRestrictDropDownListItem &item : GetTypeDropDownListItems(type)) {
						if (!ShouldHideTypeDropDownListItem(item.flags)) {
							dlist.push_back(MakeDropDownListStringItem(item.str, item.type, false));
						}
					}
					ShowDropDownList(this, std::move(dlist), type, widget, 0);
				}
				break;
			}

			case TR_WIDGET_COMPARATOR: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(GetTraceRestrictTypeProperties(item));
				if (list_set != nullptr) {
					this->ShowDropDownListWithValue(list_set, item.GetCondOp(), false, TR_WIDGET_COMPARATOR, 0, 0);
				}
				break;
			}

			case TR_WIDGET_SLOT_OP: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				const TraceRestrictDropDownListSet *list_set = (GetTraceRestrictTypeProperties(item).value_type == TRVT_SLOT_GROUP_INDEX) ? &_slot_group_op_subtypes : &_slot_op_subtypes;
				this->ShowDropDownListWithValue(list_set, item.GetCombinedAuxCondOpField(), false, TR_WIDGET_SLOT_OP, 0, 0);
				break;
			}

			case TR_WIDGET_COUNTER_OP: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				this->ShowDropDownListWithValue(&_counter_op_cond_ops, item.GetCondOp(), false, TR_WIDGET_COUNTER_OP, 0, 0);
				break;
			}

			case TR_WIDGET_VALUE_INT: {
				TraceRestrictInstructionRecord record = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(record.instruction).value_type;
				if (IsIntegerValueType(type)) {
					std::string str = GetString(STR_JUST_INT, ConvertIntegerValue(type, record.instruction.GetValue(), true));
					this->TraceRestrictShowQueryString(str, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, CS_NUMERAL, QSF_NONE);
				} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
					this->TraceRestrictShowQueryString(GetString(STR_JUST_INT, record.secondary), STR_TRACE_RESTRICT_VALUE_CAPTION, 10, CS_NUMERAL, QSF_NONE);
				}
				break;
			}

			case TR_WIDGET_VALUE_DECIMAL: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsDecimalValueType(type)) {
					int64_t value, decimal;
					ConvertValueToDecimal(type, item.GetValue(), value, decimal);
					std::string saved = std::move(_settings_game.locale.digit_group_separator);
					_settings_game.locale.digit_group_separator.clear();
					this->TraceRestrictShowQueryString(GetString(STR_JUST_DECIMAL, value, decimal), STR_TRACE_RESTRICT_VALUE_CAPTION, 16, CS_NUMERAL_DECIMAL, QSF_NONE);
					_settings_game.locale.digit_group_separator = std::move(saved);
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				switch (GetTraceRestrictTypeProperties(item).value_type) {
					case TRVT_DENY:
						this->ShowDropDownListWithValue(&_deny_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_CARGO_ID:
						this->ShowDropDownListWithValue(GetSortedCargoTypeDropDownListSet(), item.GetValue(), true, TR_WIDGET_VALUE_DROPDOWN, 0, 0); // current cargo is permitted to not be in list
						break;

					case TRVT_DIRECTION:
						this->ShowDropDownListWithValue(&_direction_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_PF_PENALTY:
						this->ShowDropDownListWithValue(&_pf_penalty_dropdown, GetPathfinderPenaltyDropdownIndex(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_RESERVE_THROUGH:
						this->ShowDropDownListWithValue(&_reserve_through_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_LONG_RESERVE: {
						uint hidden = 0;
						if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC) hidden |= 4;
						this->ShowDropDownListWithValue(&_long_reserve_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, hidden);
						break;
					}

					case TRVT_WAIT_AT_PBS:
						this->ShowDropDownListWithValue(&_wait_at_pbs_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_GROUP_INDEX: {
						int selected;
						DropDownList dlist;
						if (_shift_pressed && _settings_game.economy.infrastructure_sharing[VEH_TRAIN]) {
							selected = -1;
							if (item.GetValue() == DEFAULT_GROUP) selected = DEFAULT_GROUP;
							dlist.push_back(MakeDropDownListStringItem(STR_GROUP_DEFAULT_TRAINS, DEFAULT_GROUP, false));

							for (const Company *c : Company::Iterate()) {
								if (c->index == this->GetOwner()) continue;

								int cselected;
								DropDownList clist = GetGroupDropDownList(c->index, item.GetValue(), cselected, false);
								if (clist.empty()) continue;

								dlist.push_back(MakeDropDownListDividerItem());
								dlist.push_back(MakeCompanyDropDownListItem(c->index, false));

								if (cselected != -1) selected = cselected;
								dlist.insert(dlist.end(), std::make_move_iterator(clist.begin()), std::make_move_iterator(clist.end()));
							}
						} else {
							dlist = GetGroupDropDownList(this->GetOwner(), item.GetValue(), selected);
						}
						ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_VALUE_DROPDOWN, 0);
						break;
					}

					case TRVT_OWNER:
						this->ShowCompanyDropDownListWithValue(static_cast<CompanyID>(item.GetValue()), false, TR_WIDGET_VALUE_DROPDOWN);
						break;

					case TRVT_SLOT_INDEX: {
						int selected;
						DropDownList dlist = GetSlotDropDownList(this->GetOwner(), item.GetValue(), selected, VEH_TRAIN, IsTraceRestrictTypeNonMatchingVehicleTypeSlot(item.GetType()));
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_VALUE_DROPDOWN);
						break;
					}

					case TRVT_SLOT_GROUP_INDEX: {
						int selected;
						DropDownList dlist = GetSlotGroupDropDownList(this->GetOwner(), item.GetValue(), selected, VEH_TRAIN);
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_VALUE_DROPDOWN);
						break;
					}

					case TRVT_TRAIN_STATUS:
						this->ShowDropDownListWithValue(&_train_status_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_REVERSE:
						this->ShowDropDownListWithValue(&_reverse_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_NEWS_CONTROL:
						this->ShowDropDownListWithValue(&_news_control_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_ENGINE_CLASS:
						this->ShowDropDownListWithValue(&_engine_class_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_PF_PENALTY_CONTROL:
						this->ShowDropDownListWithValue(&_pf_penalty_control_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);;
						break;

					case TRVT_SPEED_ADAPTATION_CONTROL:
						this->ShowDropDownListWithValue(&_speed_adaptation_control_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_SIGNAL_MODE_CONTROL:
						this->ShowDropDownListWithValue(&_signal_mode_control_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_ORDER_TARGET_DIAGDIR:
						this->ShowDropDownListWithValue(&_diagdir_value, item.GetValue(), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					default:
						break;
				}
				break;
			}

			case TR_WIDGET_LEFT_AUX_DROPDOWN: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				switch (GetTraceRestrictTypeProperties(item).value_type) {
					case TRVT_SLOT_INDEX_INT: {
						int selected;
						DropDownList dlist = GetSlotDropDownList(this->GetOwner(), item.GetValue(), selected, VEH_TRAIN, IsTraceRestrictTypeNonMatchingVehicleTypeSlot(item.GetType()));
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_LEFT_AUX_DROPDOWN);
						break;
					}

					case TRVT_COUNTER_INDEX_INT: {
						int selected;
						DropDownList dlist = GetCounterDropDownList(this->GetOwner(), item.GetValue(), selected);
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_LEFT_AUX_DROPDOWN);
						break;
					}

					case TRVT_TIME_DATE_INT: {
						this->ShowDropDownListWithValue(&_time_date_value, item.GetValue(), false, TR_WIDGET_LEFT_AUX_DROPDOWN, _settings_game.game_time.time_in_minutes ? 0 : 7, 0);
						break;
					}

					case TRVT_ORDER_TARGET_DIAGDIR: {
						this->ShowDropDownListWithValue(&_target_direction_aux_value, item.GetAuxField(), false, TR_WIDGET_LEFT_AUX_DROPDOWN, 0, 0);
						break;
					}

					default:
						break;
				}
				break;
			}

			case TR_WIDGET_VALUE_DEST: {
				this->SetObjectToPlaceAction(widget, ANIMCURSOR_PICKSTATION);
				break;
			}

			case TR_WIDGET_VALUE_SIGNAL: {
				this->SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
				break;
			}

			case TR_WIDGET_VALUE_TILE: {
				this->SetObjectToPlaceAction(widget, SPR_CURSOR_MOUSE);
				break;
			}

			case TR_WIDGET_GOTO_SIGNAL: {
				ScrollMainWindowToTile(this->tile);
				this->RaiseButtons();
				this->UpdateButtonState();
				break;
			}

			case TR_WIDGET_RESET: {
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_RESET_SIGNAL, this->tile, this->track, TRMDCT_PROG_RESET, INVALID_TILE, INVALID_TRACK);
				break;
			}

			case TR_WIDGET_COPY:
			case TR_WIDGET_COPY_APPEND:
			case TR_WIDGET_SHARE:
			case TR_WIDGET_SHARE_ONTO:
				this->SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
				switch (this->current_placement_widget) {
					case TR_WIDGET_COPY:
						_thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_GREEN;
						break;

					case TR_WIDGET_COPY_APPEND:
						_thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_LIGHT_BLUE;
						break;

					case TR_WIDGET_SHARE:
						_thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_YELLOW;
						break;

					case TR_WIDGET_SHARE_ONTO:
						_thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_ORANGE;
						break;

					default:
						break;
				}
				break;

			case TR_WIDGET_UNSHARE: {
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_UNSHARE_PROGRAM, this->tile, this->track, TRMDCT_PROG_UNSHARE, INVALID_TILE, INVALID_TRACK);
				break;
			}

			case TR_WIDGET_HIGHLIGHT: {
				const TraceRestrictProgram *prog = this->GetProgram();
				if (prog != nullptr) {
					extern const TraceRestrictProgram *_viewport_highlight_tracerestrict_program;
					SetViewportCatchmentTraceRestrictProgram(prog, _viewport_highlight_tracerestrict_program != prog);
				}
				break;
			}

			case TR_WIDGET_LABEL: {
				const TraceRestrictProgram *prog = this->GetProgram();
				if (prog != nullptr) {
					TraceRestrictInstructionItem item = this->GetSelected().instruction;
					this->TraceRestrictShowQueryString(prog->GetLabel(item.GetValue()), STR_ORDER_LABEL_TEXT_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS, QSM_SET_TEXT);
				}
				break;
			}
		}
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str) override final
	{
		OnQueryTextFinished(str, {});
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str, std::optional<std::string> str2) override
	{
		if (!str.has_value() || (str->empty() && this->query_submode != QSM_SET_TEXT)) return;

		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;

		switch (this->query_submode) {
			case QSM_DEFAULT:
				break;

			case QSM_NEW_SLOT:
				if (type == TRVT_SLOT_INDEX || type == TRVT_SLOT_INDEX_INT) {
					TraceRestrictCreateSlotCmdData data;
					data.vehtype = VEH_TRAIN;
					data.parent = INVALID_TRACE_RESTRICT_SLOT_GROUP;
					data.name = std::move(*str);
					data.max_occupancy = (str2.has_value() && !str2->empty()) ? atoi(str2->c_str()) : TRACE_RESTRICT_SLOT_DEFAULT_MAX_OCCUPANCY;
					data.follow_up_cmd = { GetTraceRestrictCommandContainer(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item.base()) };
					DoCommandP<CMD_CREATE_TRACERESTRICT_SLOT>(data, STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_CREATE, CommandCallback::CreateTraceRestrictSlot);
				}
				return;

			case QSM_NEW_COUNTER:
				if (type == TRVT_COUNTER_INDEX_INT) {
					TraceRestrictCreateCounterCmdData data;
					data.name = std::move(*str);
					data.follow_up_cmd = { GetTraceRestrictCommandContainer(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item.base()) };
					DoCommandP<CMD_CREATE_TRACERESTRICT_COUNTER>(data, STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_CREATE, CommandCallback::CreateTraceRestrictCounter);
				}
				return;

			case QSM_SET_TEXT:
				if (type == TRVT_LABEL_INDEX) {
					this->PostInstructionCommand(TRDCT_SET_TEXT, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM, str->c_str());
				}
				return;
		}

		uint value;

		if (IsIntegerValueType(type) || type == TRVT_PF_PENALTY) {
			value = ConvertIntegerValue(type, atoi(str->c_str()), false);
			if (value >= (1 << TRIFA_VALUE_COUNT)) {
				SetDParam(0, ConvertIntegerValue(type, (1 << TRIFA_VALUE_COUNT) - 1, true));
				SetDParam(1, 0);
				ShowErrorMessage(STR_TRACE_RESTRICT_ERROR_VALUE_TOO_LARGE, STR_EMPTY, WL_INFO);
				return;
			}

			if (type == TRVT_PF_PENALTY) {
				item.SetAuxField(TRPPAF_VALUE);
			}
		} else if (IsDecimalValueType(type)) {
			char tmp_buffer[32];
			strecpy(tmp_buffer, str->c_str(), lastof(tmp_buffer));
			str_replace_wchar(tmp_buffer, lastof(tmp_buffer), GetDecimalSeparatorChar(), '.');
			value = ConvertDecimalToValue(type, atof(tmp_buffer));
			if (value >= (1 << TRIFA_VALUE_COUNT)) {
				int64_t value, decimal;
				ConvertValueToDecimal(type, (1 << TRIFA_VALUE_COUNT) - 1, value, decimal);
				SetDParam(0, value);
				SetDParam(1, decimal);
				ShowErrorMessage(STR_TRACE_RESTRICT_ERROR_VALUE_TOO_LARGE, STR_EMPTY, WL_INFO);
				return;
			}
		} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
			value = atoi(str->c_str());
			this->PostInstructionCommand(TRDCT_MODIFY_DUAL_ITEM, value, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
			return;
		} else {
			return;
		}

		item.SetValue(value);
		this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnDropdownSelect(WidgetID widget, int index) override
	{
		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		if (item == 0 || index < 0 || this->selected_instruction < 1) {
			return;
		}

		if (widget == TR_WIDGET_VALUE_DROPDOWN || widget == TR_WIDGET_LEFT_AUX_DROPDOWN) {
			TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
			if (((widget == TR_WIDGET_VALUE_DROPDOWN && type.value_type == TRVT_SLOT_INDEX) || (widget == TR_WIDGET_LEFT_AUX_DROPDOWN && type.value_type == TRVT_SLOT_INDEX_INT)) && index == NEW_TRACE_RESTRICT_SLOT_ID) {
				this->query_submode = QSM_NEW_SLOT;
				ShowSlotCreationQueryString(*this);
				return;
			}
			if (widget == TR_WIDGET_LEFT_AUX_DROPDOWN && type.value_type == TRVT_COUNTER_INDEX_INT && index == NEW_TRACE_RESTRICT_COUNTER_ID) {
				this->TraceRestrictShowQueryString({}, STR_TRACE_RESTRICT_COUNTER_CREATE_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS, QSM_NEW_COUNTER);
				return;
			}
			if ((widget == TR_WIDGET_VALUE_DROPDOWN && this->value_drop_down_is_company) || type.value_type == TRVT_GROUP_INDEX ||
					type.value_type == TRVT_SLOT_INDEX || type.value_type == TRVT_SLOT_INDEX_INT || type.value_type == TRVT_SLOT_GROUP_INDEX ||
					type.value_type == TRVT_COUNTER_INDEX_INT || type.value_type == TRVT_TIME_DATE_INT) {
				/* This is a special company drop-down or group/slot-index drop-down */
				item.SetValue(index);
				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				if (type.value_type == TRVT_SLOT_INDEX || type.value_type == TRVT_SLOT_INDEX_INT) {
					TraceRestrictRecordRecentSlot(index);
				}
				if (type.value_type == TRVT_SLOT_GROUP_INDEX) {
					TraceRestrictRecordRecentSlotGroup(index);
				}
				if (type.value_type == TRVT_COUNTER_INDEX_INT) {
					TraceRestrictRecordRecentCounter(index);
				}
				return;
			}
			if (type.value_type == TRVT_ORDER_TARGET_DIAGDIR && widget == TR_WIDGET_LEFT_AUX_DROPDOWN) {
				item.SetAuxField(index);
				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				return;
			}
		}

		if (widget == TR_WIDGET_TYPE_COND || widget == TR_WIDGET_TYPE_NONCOND) {
			SetTraceRestrictTypeAndNormalise(item, static_cast<TraceRestrictItemType>(index & 0xFFFF), index >> 16);

			this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
		}

		if (widget == TR_WIDGET_INSERT) {
			TraceRestrictInstructionItem insert_item{};

			SetTraceRestrictTypeAndNormalise(insert_item, static_cast<TraceRestrictItemType>(index & 0xFFFF));
			if (insert_item.IsConditional()) {
				/* Inserting an if/elif/orif/else */
				insert_item.SetCondFlags(static_cast<TraceRestrictCondFlags>(index >> 16)); // this needs to happen after calling SetTraceRestrictTypeAndNormalise
			}

			this->expecting_inserted_item = insert_item;
			this->PostInstructionCommand(TRDCT_INSERT_ITEM, insert_item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_INSERT_ITEM);
			return;
		}

		const TraceRestrictDropDownListSet *list_set = this->drop_down_list_mapping[widget];
		if (!list_set) {
			return;
		}

		uint value = list_set->value_array[index];

		switch (widget) {
			case TR_WIDGET_CONDFLAGS: {
				CondFlagsDropDownType cond_type = static_cast<CondFlagsDropDownType>(value);
				if (cond_type == CFDDT_ELSE) {
					SetTraceRestrictTypeAndNormalise(item, TRIT_COND_ENDIF);
					item.SetCondFlags(TRCF_ELSE);
				} else {
					if (item.GetType() == TRIT_COND_ENDIF) {
						/* Item is currently an else, convert to else/or if */
						SetTraceRestrictTypeAndNormalise(item, TRIT_COND_UNDEFINED);
					}

					item.SetCondFlags(static_cast<TraceRestrictCondFlags>(cond_type));
				}

				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_COMPARATOR:
			case TR_WIDGET_COUNTER_OP: {
				item.SetCondOp(static_cast<TraceRestrictCondOp>(value));
				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_SLOT_OP: {
				item.SetCombinedAuxCondOpField(value);
				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				if (GetTraceRestrictTypeProperties(item).value_type == TRVT_PF_PENALTY) {
					if (value == TRPPPI_END) {
						uint16_t penalty_value;
						if (item.GetAuxField() == TRPPAF_PRESET) {
							penalty_value = _tracerestrict_pathfinder_penalty_preset_values[item.GetValue()];
						} else {
							penalty_value = item.GetValue();
						}
						this->TraceRestrictShowQueryString(GetString(STR_JUST_INT, penalty_value), STR_TRACE_RESTRICT_VALUE_CAPTION, 10, CS_NUMERAL, QSF_NONE);
						return;
					} else {
						item.SetValue(value);
						item.SetAuxField(TRPPAF_PRESET);
					}
				} else {
					item.SetValue(value);
				}
				this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile) override
	{
		WidgetID widget = this->current_placement_widget;
		if (widget != TR_WIDGET_SHARE_ONTO) {
			this->ResetObjectToPlaceAction();

			this->RaiseButtons();
			ResetObjectToPlace();
		}

		if (widget < 0) {
			return;
		}

		switch (widget) {
			case TR_WIDGET_COPY:
				OnPlaceObjectSignal(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_PROGRAM);
				break;

			case TR_WIDGET_COPY_APPEND:
				OnPlaceObjectSignal(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_APPEND_PROGRAM);
				break;

			case TR_WIDGET_SHARE:
			case TR_WIDGET_SHARE_ONTO:
				OnPlaceObjectSignal(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			case TR_WIDGET_VALUE_DEST:
				OnPlaceObjectDestination(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;

			case TR_WIDGET_VALUE_SIGNAL:
				OnPlaceObjectSignalTileValue(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;

			case TR_WIDGET_VALUE_TILE:
				OnPlaceObjectTileValue(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

	/**
	 * Common OnPlaceObject handler for program management actions which involve clicking on a signal
	 */
	void OnPlaceObjectSignal(Point pt, TileIndex source_tile, WidgetID widget, int error_message)
	{
		if (!IsPlainRailTile(source_tile) && !IsRailTunnelBridgeTile(source_tile)) {
			ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
			return;
		}

		TrackBits trackbits = TrackdirBitsToTrackBits(GetTileTrackdirBits(source_tile, TRANSPORT_RAIL, 0));
		if (trackbits & TRACK_BIT_VERT) { // N-S direction
			trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
		}

		if (trackbits & TRACK_BIT_HORZ) { // E-W direction
			trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
		}
		Track source_track = FindFirstTrack(trackbits);
		if (source_track == INVALID_TRACK) {
			ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
			return;
		}

		if (IsTileType(source_tile, MP_RAILWAY)) {
			if (!HasTrack(source_tile, source_track)) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
				return;
			}

			if (!HasSignalOnTrack(source_tile, source_track)) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_ARE_NO_SIGNALS, WL_INFO);
				return;
			}
		} else {
			if (!HasTrack(GetTunnelBridgeTrackBits(source_tile), source_track)) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
				return;
			}

			if (!IsTunnelBridgeWithSignalSimulation(source_tile) || !HasTrack(GetAcrossTunnelBridgeTrackBits(source_tile), source_track)) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_ARE_NO_SIGNALS, WL_INFO);
				return;
			}
		}

		switch (widget) {
			case TR_WIDGET_COPY:
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_PROGRAM, this->tile, this->track, TRMDCT_PROG_COPY, source_tile, source_track);
				break;

			case TR_WIDGET_COPY_APPEND:
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_APPEND_PROGRAM, this->tile, this->track, TRMDCT_PROG_COPY_APPEND, source_tile, source_track);
				break;

			case TR_WIDGET_SHARE:
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM, this->tile, this->track, TRMDCT_PROG_SHARE, source_tile, source_track);
				break;

			case TR_WIDGET_SHARE_ONTO:
				Command<CMD_MANAGE_TRACERESTRICT_SIGNAL>::Post(STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM, source_tile, source_track, TRMDCT_PROG_SHARE_IF_UNMAPPED, this->tile, this->track);
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting an order target
	 */
	void OnPlaceObjectDestination(Point pt, TileIndex tile, WidgetID widget, int error_message)
	{
		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		if (GetTraceRestrictTypeProperties(item).value_type != TRVT_ORDER) return;

		bool stations_only = (item.GetType() == TRIT_COND_LAST_STATION);

		if (IsDepotTypeTile(tile, TRANSPORT_RAIL)) {
			if (stations_only) return;
			item.SetValue(GetDepotIndex(tile));
			item.SetAuxField(TROCAF_DEPOT);
		} else if (IsRailWaypointTile(tile)) {
			if (stations_only) return;
			item.SetValue(GetStationIndex(tile));
			item.SetAuxField(TROCAF_WAYPOINT);
		} else if (IsTileType(tile, MP_STATION)) {
			StationID st_index = GetStationIndex(tile);
			const Station *st = Station::Get(st_index);
			if (st->facilities & FACIL_TRAIN) {
				item.SetValue(st_index);
				item.SetAuxField(TROCAF_STATION);
			} else {
				return;
			}
		} else {
			return;
		}

		if (!IsInfraTileUsageAllowed(VEH_TRAIN, _local_company, tile)) {
			ShowErrorMessage(error_message, STR_ERROR_AREA_IS_OWNED_BY_ANOTHER, WL_INFO);
			return;
		}

		this->PostInstructionCommand(TRDCT_MODIFY_ITEM, item.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting a signal tile value
	 */
	void OnPlaceObjectSignalTileValue(Point pt, TileIndex tile, WidgetID widget, int error_message)
	{
		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item).value_type;
		if (val_type != TRVT_TILE_INDEX && val_type != TRVT_TILE_INDEX_THROUGH) return;

		if (!IsInfraTileUsageAllowed(VEH_TRAIN, _local_company, tile)) {
			ShowErrorMessage(error_message, STR_ERROR_AREA_IS_OWNED_BY_ANOTHER, WL_INFO);
			return;
		}

		if (IsRailDepotTile(tile)) {
			/* OK */
		} else if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(tile)) {
			/* OK */
		} else {
			if (!IsPlainRailTile(tile)) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
				return;
			}

			if (GetPresentSignals(tile) == 0) {
				ShowErrorMessage(error_message, STR_ERROR_THERE_ARE_NO_SIGNALS, WL_INFO);
				return;
			}
		}

		this->PostInstructionCommand(TRDCT_MODIFY_DUAL_ITEM, tile.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting a tile value
	 */
	void OnPlaceObjectTileValue(Point pt, TileIndex tile, WidgetID widget, int error_message)
	{
		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item).value_type;
		if (val_type != TRVT_TILE_INDEX && val_type != TRVT_TILE_INDEX_THROUGH) return;

		this->PostInstructionCommand(TRDCT_MODIFY_DUAL_ITEM, tile.base(), STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
		this->ResetObjectToPlaceAction();
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST:
				resize.height = GetCharacterHeight(FS_NORMAL);
				size.height = 6 * resize.height + WidgetDimensions::scaled.framerect.Vertical();
				break;

			case TR_WIDGET_GOTO_SIGNAL:
				size.width = std::max<uint>(12, NWidgetScrollbar::GetVerticalDimension().width);
				break;
		}
	}

	virtual void OnResize() override
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, TR_WIDGET_INSTRUCTION_LIST);
	}

	virtual void OnPaint() override
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != TR_WIDGET_INSTRUCTION_LIST) return;

		int y = r.top + WidgetDimensions::scaled.framerect.top;
		int line_height = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST)->resize_y;
		int scroll_position = this->vscroll->GetPosition();

		/* prog may be nullptr */
		const TraceRestrictProgram *prog = this->GetProgram();

		DrawInstructionStringFlags flags{};
		if (IsTunnelBridgeWithSignalSimulation(this->tile)) {
			if (IsTunnelBridgeSignalSimulationEntrance(this->tile)) flags.Set(DrawInstructionStringFlag::TunnelBridgeEntrance);
			if (IsTunnelBridgeSignalSimulationExit(this->tile)) flags.Set(DrawInstructionStringFlag::TunnelBridgeExit);
		}

		int count = this->GetItemCount(prog);
		uint indent = 1;
		for (int i = 0; i < count; i++) {
			TraceRestrictInstructionRecord item = this->GetItem(prog, i);
			uint this_indent = indent;
			if (item.instruction.IsConditional()) {
				if (item.instruction.GetCondFlags() & (TRCF_ELSE | TRCF_OR)) {
					this_indent--;
				} else if (item.instruction.GetType() == TRIT_COND_ENDIF) {
					indent--;
					this_indent--;
				} else {
					indent++;
				}
			} else if (item.instruction.GetType() == TRIT_NULL) {
				this_indent = 0;
			}

			if (i >= scroll_position && this->vscroll->IsVisible(i)) {
				DrawInstructionString(prog, item, i, y, i == this->selected_instruction, this_indent,
						r.left + WidgetDimensions::scaled.framerect.left, r.right - WidgetDimensions::scaled.framerect.right, this->GetOwner(), flags);
				y += line_height;
			}
		}
	}

	virtual void OnInvalidateData(int data, bool gui_scope) override
	{
		if (gui_scope) {
			this->ReloadProgramme();
		}
	}

	virtual void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case TR_WIDGET_VALUE_INT: {
				SetDParam(0, STR_JUST_COMMA);
				TraceRestrictInstructionRecord record = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(record.instruction).value_type;
				if (type == TRVT_TIME_DATE_INT && record.instruction.GetValue() == TRTDVF_HOUR_MINUTE) {
					SetDParam(0, STR_JUST_TIME_HHMM);
				}
				SetDParam(1, 0);
				if (IsIntegerValueType(type)) {
					SetDParam(1, ConvertIntegerValue(type, record.instruction.GetValue(), true));
				} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
					SetDParam(1, record.secondary);
				}
				break;
			}

			case TR_WIDGET_VALUE_DECIMAL: {
				SetDParam(0, 0);
				SetDParam(1, 0);
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsDecimalValueType(type)) {
					int64_t value, decimal;
					ConvertValueToDecimal(type, item.GetValue(), value, decimal);
					SetDParam(0, value);
					SetDParam(1, decimal);
				}
				break;
			}

			case TR_WIDGET_CAPTION: {
				const TraceRestrictProgram *prog = this->GetProgram();
				if (prog != nullptr) {
					SetDParam(0, prog->GetReferenceCount());
				} else {
					SetDParam(0, 1);
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
				if ((type.value_type == TRVT_PF_PENALTY &&
						item.GetAuxField() == TRPPAF_VALUE)
						|| type.value_type == TRVT_GROUP_INDEX
						|| type.value_type == TRVT_SLOT_INDEX
						|| type.value_type == TRVT_SLOT_GROUP_INDEX) {
					SetDParam(0, item.GetValue());
				}
				break;
			}

			case TR_WIDGET_LEFT_AUX_DROPDOWN: {
				TraceRestrictInstructionItem item = this->GetSelected().instruction;
				TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
				if (type.value_type == TRVT_SLOT_INDEX_INT || type.value_type == TRVT_COUNTER_INDEX_INT || type.value_type == TRVT_TIME_DATE_INT) {
					SetDParam(0, item.GetValue());
				}
				break;
			}
		}
	}

	bool OnTooltip(Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case TR_WIDGET_SHARE: {
				SetDParam(0, STR_TRACE_RESTRICT_SHARE_TOOLTIP);
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, close_cond, 1);
				return true;
			}

			case TR_WIDGET_UNSHARE: {
				SetDParam(0, STR_TRACE_RESTRICT_UNSHARE_TOOLTIP);
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, close_cond, 1);
				return true;
			}

			case TR_WIDGET_SHARE_ONTO: {
				SetDParam(0, (this->base_share_plane == DPS_UNSHARE) ? STR_TRACE_RESTRICT_UNSHARE_TOOLTIP : STR_TRACE_RESTRICT_SHARE_TOOLTIP);
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, close_cond, 1);
				return true;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				switch (GetTraceRestrictTypeProperties(this->GetSelected().instruction).value_type) {
					case TRVT_SLOT_INDEX:
					case TRVT_SLOT_GROUP_INDEX:
						GuiShowTooltips(this, TraceRestrictPrepareSlotCounterSelectTooltip(STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP, VEH_TRAIN), close_cond, 0);
						return true;

					case TRVT_GROUP_INDEX:
						if (_settings_game.economy.infrastructure_sharing[VEH_TRAIN]) {
							SetDParam(0, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP);
							SetDParam(1, STR_NULL);
							GuiShowTooltips(this, STR_TRACE_RESTRICT_OTHER_COMPANY_TOOLTIP_EXTRA, close_cond, 0);
							return true;
						}
						return false;

					default:
						return false;
				}
			}

			case TR_WIDGET_LEFT_AUX_DROPDOWN: {
				switch (GetTraceRestrictTypeProperties(this->GetSelected().instruction).value_type) {
					case TRVT_SLOT_INDEX_INT:
					case TRVT_COUNTER_INDEX_INT:
						GuiShowTooltips(this, TraceRestrictPrepareSlotCounterSelectTooltip(STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP, VEH_TRAIN), close_cond, 0);
						return true;

					default:
						return false;
				}
			}

			default:
				return false;
		}
	}

	virtual EventState OnCTRLStateChange() override
	{
		this->UpdateButtonState();
		return ES_NOT_HANDLED;
	}

	bool IsNewGRFInspectable() const override
	{
		return true;
	}

	void ShowNewGRFInspectWindow() const override
	{
		::ShowNewGRFInspectWindow(GSF_FAKE_TRACERESTRICT, MakeTraceRestrictRefId(this->tile, this->track));
	}

private:
	/**
	 * Helper function to make start and end instructions (these are not stored in the actual program)
	 */
	TraceRestrictInstructionItem MakeSpecialItem(TraceRestrictNullTypeSpecialValue value) const
	{
		TraceRestrictInstructionItem item{};
		item.SetType(TRIT_NULL);
		item.SetValue(value);
		return item;
	}

	/**
	 * Get item count of program, including start and end markers
	 */
	int GetItemCount(const TraceRestrictProgram *prog) const
	{
		if (prog != nullptr) {
			return 2 + static_cast<int>(prog->GetInstructionCount());
		} else {
			return 2;
		}
	}

	/**
	 * Get current program
	 * This may return nullptr if no program currently exists
	 */
	const TraceRestrictProgram *GetProgram() const
	{
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(this->tile, this->track), false);
	}

	/**
	 * Get instruction record at @p index in program @p prog
	 * This correctly handles start/end markers, offsets, etc.
	 * This returns a 0 instruction if out of bounds
	 * @p prog may be nullptr
	 */
	TraceRestrictInstructionRecord GetItem(const TraceRestrictProgram *prog, int index) const
	{
		if (index < 0) {
			return {};
		}

		if (index == 0) {
			return { MakeSpecialItem(TRNTSV_START) };
		}

		if (prog != nullptr) {
			size_t instruction_count = prog->GetInstructionCount();

			if (static_cast<size_t>(index) == instruction_count + 1) {
				return { MakeSpecialItem(TRNTSV_END) };
			}

			if (static_cast<size_t>(index) > instruction_count + 1) {
				return {};
			}

			return prog->GetInstructionRecordAt(index - 1);
		} else {
			/* No program defined, this is equivalent to an empty program */
			if (index == 1) {
				return { MakeSpecialItem(TRNTSV_END) };
			} else {
				return {};
			}
		}
	}

	/**
	 * Get selected instruction, or a zero instruction
	 */
	TraceRestrictInstructionRecord GetSelected() const
	{
		return this->GetItem(this->GetProgram(), this->selected_instruction);
	}

	/**
	 * Get owner of the signal tile this window is pointing at
	 */
	Owner GetOwner() const
	{
		return GetTileOwner(this->tile);
	}

	/**
	 * Return item index from point in instruction list widget
	 */
	int GetItemIndexFromPt(int y)
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST);
		int32_t sel = (y - nwid->pos_y - WidgetDimensions::scaled.framerect.top) / nwid->resize_y; // Selected line

		if (sel >= this->vscroll->GetCapacity()) return -1;

		sel += this->vscroll->GetPosition();

		return (sel < this->GetItemCount(this->GetProgram()) && sel >= 0) ? sel : -1;
	}

	/**
	 * Reload details of program, and adjust length/selection position as necessary
	 */
	void ReloadProgramme()
	{
		const TraceRestrictProgram *prog = this->GetProgram();

		if (this->vscroll->GetCount() != this->GetItemCount(prog)) {
			/* Program length has changed */

			if (this->GetItemCount(prog) < this->vscroll->GetCount() ||
					this->GetItem(prog, this->selected_instruction).instruction != this->expecting_inserted_item) {
				/* Length has shrunk or if we weren't expecting an insertion, deselect */
				this->selected_instruction = -1;
			}
			this->expecting_inserted_item = {};

			/* Update scrollbar size */
			this->vscroll->SetCount(this->GetItemCount(prog));
		}
		this->RaiseButtons();
		this->UpdateButtonState();
	}

	bool IsUpDownBtnUsable(bool up, bool update_selection = false) {
		const TraceRestrictProgram *prog = this->GetProgram();
		if (prog == nullptr) return false;

		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		if (item.GetType() == TRIT_NULL) return false;

		std::vector<TraceRestrictProgramItem> items = prog->items; // copy
		uint32_t offset = this->selected_instruction - 1;
		if (TraceRestrictProgramMoveItemAt(items, offset, up, _ctrl_pressed).Succeeded()) {
			TraceRestrictProgramActionsUsedFlags actions_used_flags;
			if (TraceRestrictProgram::Validate(items, actions_used_flags).Succeeded()) {
				if (update_selection) this->selected_instruction = offset + 1;
				return true;
			}
		}

		return false;
	}

	bool IsDuplicateBtnUsable() const {
		const TraceRestrictProgram *prog = this->GetProgram();
		if (prog == nullptr) return false;

		TraceRestrictInstructionItem item = this->GetSelected().instruction;
		if (item.GetType() == TRIT_NULL) return false;

		uint32_t offset = this->selected_instruction - 1;
		if (TraceRestrictProgramDuplicateItemAtDryRun(prog->items, offset)) {
			return true;
		}

		return false;
	}

	void UpdatePlaceObjectPlanes()
	{
		WidgetID widget = this->current_placement_widget;

		if (!(widget == TR_WIDGET_COPY || widget == TR_WIDGET_COPY_APPEND)) {
			NWidgetStacked *copy_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_COPY);
			copy_sel->SetDisplayedPlane(_ctrl_pressed ? DPC_APPEND : this->base_copy_plane);
			this->SetDirty();
		}

		if (!(widget == TR_WIDGET_SHARE || widget == TR_WIDGET_SHARE_ONTO)) {
			NWidgetStacked *share_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_SHARE);
			share_sel->SetDisplayedPlane(_ctrl_pressed ? DPS_SHARE_ONTO : this->base_share_plane);
			this->SetDirty();
		}
	}

	void RaiseButtons()
	{
		this->RaiseWidget(TR_WIDGET_INSERT);
		this->RaiseWidget(TR_WIDGET_REMOVE);
		this->RaiseWidget(TR_WIDGET_TYPE_COND);
		this->RaiseWidget(TR_WIDGET_TYPE_NONCOND);
		this->RaiseWidget(TR_WIDGET_CONDFLAGS);
		this->RaiseWidget(TR_WIDGET_COMPARATOR);
		this->RaiseWidget(TR_WIDGET_SLOT_OP);
		this->RaiseWidget(TR_WIDGET_COUNTER_OP);
		this->RaiseWidget(TR_WIDGET_VALUE_INT);
		this->RaiseWidget(TR_WIDGET_VALUE_DECIMAL);
		this->RaiseWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->RaiseWidget(TR_WIDGET_VALUE_DEST);
		this->RaiseWidget(TR_WIDGET_VALUE_SIGNAL);
		this->RaiseWidget(TR_WIDGET_VALUE_TILE);
		this->RaiseWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
	}

	/**
	 * Update button states, text values, etc.
	 */
	void UpdateButtonState()
	{
		NWidgetStacked *left_2_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_2);
		NWidgetStacked *left_sel   = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT);
		NWidgetStacked *left_aux_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_AUX);
		NWidgetStacked *middle_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_RIGHT);

		this->DisableWidget(TR_WIDGET_TYPE_COND);
		this->DisableWidget(TR_WIDGET_TYPE_NONCOND);
		this->DisableWidget(TR_WIDGET_CONDFLAGS);
		this->DisableWidget(TR_WIDGET_COMPARATOR);
		this->DisableWidget(TR_WIDGET_SLOT_OP);
		this->DisableWidget(TR_WIDGET_COUNTER_OP);
		this->DisableWidget(TR_WIDGET_VALUE_INT);
		this->DisableWidget(TR_WIDGET_VALUE_DECIMAL);
		this->DisableWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->DisableWidget(TR_WIDGET_VALUE_DEST);
		this->DisableWidget(TR_WIDGET_VALUE_SIGNAL);
		this->DisableWidget(TR_WIDGET_VALUE_TILE);
		this->DisableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);

		this->DisableWidget(TR_WIDGET_INSERT);
		this->DisableWidget(TR_WIDGET_REMOVE);
		this->DisableWidget(TR_WIDGET_RESET);
		this->DisableWidget(TR_WIDGET_COPY);
		this->DisableWidget(TR_WIDGET_SHARE);
		this->DisableWidget(TR_WIDGET_UNSHARE);
		this->DisableWidget(TR_WIDGET_SHARE_ONTO);

		this->DisableWidget(TR_WIDGET_BLANK_L2);
		this->DisableWidget(TR_WIDGET_BLANK_L);
		this->DisableWidget(TR_WIDGET_BLANK_M);
		this->DisableWidget(TR_WIDGET_BLANK_R);

		this->DisableWidget(TR_WIDGET_UP_BTN);
		this->DisableWidget(TR_WIDGET_DOWN_BTN);
		this->DisableWidget(TR_WIDGET_DUPLICATE);

		left_2_sel->SetDisplayedPlane(DPL2_BLANK);
		left_sel->SetDisplayedPlane(DPL_BLANK);
		left_aux_sel->SetDisplayedPlane(SZSP_NONE);
		middle_sel->SetDisplayedPlane(DPM_BLANK);
		right_sel->SetDisplayedPlane(DPR_BLANK);

		const TraceRestrictProgram *prog = this->GetProgram();

		this->GetWidget<NWidgetCore>(TR_WIDGET_CAPTION)->SetString((prog != nullptr && prog->GetReferenceCount() > 1) ? STR_TRACE_RESTRICT_CAPTION_SHARED : STR_TRACE_RESTRICT_CAPTION);

		this->SetWidgetDisabledState(TR_WIDGET_HIGHLIGHT, prog == nullptr);
		extern const TraceRestrictProgram *_viewport_highlight_tracerestrict_program;
		this->SetWidgetLoweredState(TR_WIDGET_HIGHLIGHT, prog != nullptr && _viewport_highlight_tracerestrict_program == prog);

		auto left_aux_guard = scope_guard([&]() {
			if (this->current_left_aux_plane != left_aux_sel->shown_plane) {
				this->current_left_aux_plane = left_aux_sel->shown_plane;
				this->ReInit();
			}
		});

		/* Don't allow modifications for non-owners */
		if (this->GetOwner() != _local_company) {
			this->SetDirty();
			return;
		}

		this->EnableWidget(TR_WIDGET_COPY_APPEND);
		this->EnableWidget(TR_WIDGET_SHARE_ONTO);

		this->base_copy_plane = DPC_DUPLICATE;
		this->base_share_plane = DPS_SHARE;

		if (prog != nullptr && prog->GetReferenceCount() > 1) {
			/* Program is shared, show and enable unshare button, and reset button */
			this->base_share_plane = DPS_UNSHARE;
			this->EnableWidget(TR_WIDGET_UNSHARE);
			this->EnableWidget(TR_WIDGET_RESET);
		} else if (this->GetItemCount(prog) > 2) {
			/* Program is non-empty and not shared, enable reset button */
			this->EnableWidget(TR_WIDGET_RESET);
		} else {
			/* Program is empty and not shared, show copy and share buttons */
			this->EnableWidget(TR_WIDGET_COPY);
			this->EnableWidget(TR_WIDGET_SHARE);
			this->base_copy_plane = DPC_COPY;
		}

		this->GetWidget<NWidgetCore>(TR_WIDGET_COPY_APPEND)->SetToolTip((this->base_copy_plane == DPC_DUPLICATE) ? STR_TRACE_RESTRICT_DUPLICATE_TOOLTIP : STR_TRACE_RESTRICT_COPY_TOOLTIP);
		this->UpdatePlaceObjectPlanes();

		/* Haven't selected instruction */
		if (this->selected_instruction < 1) {
			this->SetDirty();
			return;
		}

		TraceRestrictInstructionItem item = this->GetItem(prog, this->selected_instruction).instruction;
		if (item != 0) {
			if (item.GetType() == TRIT_NULL) {
				switch (item.GetValue()) {
					case TRNTSV_START:
						break;

					case TRNTSV_END:
						this->EnableWidget(TR_WIDGET_INSERT);
						break;

					default:
						NOT_REACHED();
						break;
				}
			} else if (item.GetType() == TRIT_COND_ENDIF) {
				this->EnableWidget(TR_WIDGET_INSERT);
				if (item.GetCondFlags() != 0) {
					/* This is not an end if, it must be an else, enable removing */
					this->EnableWidget(TR_WIDGET_REMOVE);

					/* Setup condflags dropdown to show else */
					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					this->EnableWidget(TR_WIDGET_CONDFLAGS);
					this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->SetString(STR_TRACE_RESTRICT_CONDITIONAL_ELSE);
				}
			} else {
				TraceRestrictTypePropertySet properties = GetTraceRestrictTypeProperties(item);

				int type_widget;
				if (item.IsConditional()) {
					/* Note that else and end if items are not handled here, they are handled above */

					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					left_sel->SetDisplayedPlane(DPL_TYPE);
					type_widget = TR_WIDGET_TYPE_COND;

					/* Setup condflags dropdown box */
					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					switch (item.GetCondFlags()) {
						case TRCF_DEFAULT:                            // opening if, leave disabled
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->SetString(STR_TRACE_RESTRICT_CONDITIONAL_IF);
							break;

						case TRCF_ELSE:                               // else-if
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->SetString(STR_TRACE_RESTRICT_CONDITIONAL_ELIF);
							this->EnableWidget(TR_WIDGET_CONDFLAGS);
							break;

						case TRCF_OR:                                 // or-if
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->SetString(STR_TRACE_RESTRICT_CONDITIONAL_ORIF);
							this->EnableWidget(TR_WIDGET_CONDFLAGS);
							break;

						default:
							NOT_REACHED();
							break;
					}
				} else {
					left_2_sel->SetDisplayedPlane(DPL2_TYPE);
					type_widget = TR_WIDGET_TYPE_NONCOND;
				}
				this->EnableWidget(type_widget);

				this->GetWidget<NWidgetCore>(type_widget)->SetString(GetTypeString(item));

				if (properties.cond_type == TRCOT_BINARY || properties.cond_type == TRCOT_ALL) {
					middle_sel->SetDisplayedPlane(DPM_COMPARATOR);
					this->EnableWidget(TR_WIDGET_COMPARATOR);

					const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(properties);

					if (list_set) {
						this->GetWidget<NWidgetCore>(TR_WIDGET_COMPARATOR)->SetString(GetDropDownStringByValue(list_set, item.GetCondOp()));
					}
				}

				if (IsIntegerValueType(properties.value_type)) {
					right_sel->SetDisplayedPlane(DPR_VALUE_INT);
					this->EnableWidget(TR_WIDGET_VALUE_INT);
				} else if (IsDecimalValueType(properties.value_type)) {
					right_sel->SetDisplayedPlane(DPR_VALUE_DECIMAL);
					this->EnableWidget(TR_WIDGET_VALUE_DECIMAL);
				} else {
					switch (properties.value_type) {
						case TRVT_DENY:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(item.GetValue() ? STR_TRACE_RESTRICT_PF_ALLOW : STR_TRACE_RESTRICT_PF_DENY);
							break;

						case TRVT_ORDER:
							right_sel->SetDisplayedPlane(DPR_VALUE_DEST);
							this->EnableWidget(TR_WIDGET_VALUE_DEST);
							break;

						case TRVT_CARGO_ID:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetCargoStringByID(item.GetValue()));
							break;

						case TRVT_DIRECTION:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_direction_value, item.GetValue()));
							break;

						case TRVT_TILE_INDEX:
							if (item.GetType() == TRIT_COND_PBS_ENTRY_SIGNAL && item.GetAuxField() == TRPESAF_RES_END_TILE) {
								right_sel->SetDisplayedPlane(DPR_VALUE_TILE);
								this->EnableWidget(TR_WIDGET_VALUE_TILE);
							} else {
								right_sel->SetDisplayedPlane(DPR_VALUE_SIGNAL);
								this->EnableWidget(TR_WIDGET_VALUE_SIGNAL);
							}
							break;

						case TRVT_TILE_INDEX_THROUGH:
							right_sel->SetDisplayedPlane(DPR_VALUE_TILE);
							this->EnableWidget(TR_WIDGET_VALUE_TILE);
							break;

						case TRVT_PF_PENALTY:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							if (item.GetAuxField() == TRPPAF_VALUE) {
								this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_JUST_COMMA);
							} else {
								this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_pf_penalty_dropdown, GetPathfinderPenaltyDropdownIndex(item)));
							}
							break;

						case TRVT_RESERVE_THROUGH:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(
									item.GetValue() ? STR_TRACE_RESTRICT_RESERVE_THROUGH_CANCEL : STR_TRACE_RESTRICT_RESERVE_THROUGH);
							break;

						case TRVT_LONG_RESERVE:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_long_reserve_value, item.GetValue()));
							break;

						case TRVT_WAIT_AT_PBS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_wait_at_pbs_value, item.GetValue()));
							break;

						case TRVT_GROUP_INDEX:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							switch (item.GetValue()) {
								case INVALID_GROUP:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
									break;

								case DEFAULT_GROUP:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_GROUP_DEFAULT_TRAINS);
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_GROUP_NAME);
									break;
							}
							break;

						case TRVT_OWNER:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_COMPANY);
							break;

						case TRVT_SLOT_INDEX: {
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							if (!item.IsConditional()) {
								middle_sel->SetDisplayedPlane(DPM_SLOT_OP);
								this->EnableWidget(TR_WIDGET_SLOT_OP);
							}
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);

							this->GetWidget<NWidgetCore>(TR_WIDGET_SLOT_OP)->SetString(GetDropDownStringByValue(&_slot_op_subtypes, item.GetCombinedAuxCondOpField()));
							switch (item.GetValue()) {
								case INVALID_TRACE_RESTRICT_SLOT_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_SLOT_NAME);
									break;
							}
							break;
						}

						case TRVT_SLOT_INDEX_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);

							switch (item.GetValue()) {
								case INVALID_TRACE_RESTRICT_SLOT_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(STR_TRACE_RESTRICT_SLOT_NAME);
									break;
							}
							break;
						}

						case TRVT_SLOT_GROUP_INDEX: {
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							if (!item.IsConditional()) {
								middle_sel->SetDisplayedPlane(DPM_SLOT_OP);
								this->EnableWidget(TR_WIDGET_SLOT_OP);
							}
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);

							this->GetWidget<NWidgetCore>(TR_WIDGET_SLOT_OP)->SetString(GetDropDownStringByValue(&_slot_op_subtypes, item.GetCombinedAuxCondOpField()));
							switch (item.GetValue()) {
								case INVALID_TRACE_RESTRICT_SLOT_GROUP:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(STR_TRACE_RESTRICT_SLOT_GROUP_NAME);
									break;
							}
							break;
						}

						case TRVT_TRAIN_STATUS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_train_status_value, item.GetValue()));
							break;

						case TRVT_REVERSE:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_reverse_value, item.GetValue()));
							break;

						case TRVT_NEWS_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_news_control_value, item.GetValue()));
							break;

						case TRVT_COUNTER_INDEX_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);
							if (!item.IsConditional()) {
								left_sel->SetDisplayedPlane(DPL_COUNTER_OP);
								this->EnableWidget(TR_WIDGET_COUNTER_OP);
								this->GetWidget<NWidgetCore>(TR_WIDGET_COUNTER_OP)->SetString(GetDropDownStringByValue(&_counter_op_cond_ops, item.GetCondOp()));
							}
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);

							switch (item.GetValue()) {
								case INVALID_TRACE_RESTRICT_COUNTER_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(STR_TRACE_RESTRICT_COUNTER_NAME);
									break;
							}
							break;
						}

						case TRVT_TIME_DATE_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(STR_TRACE_RESTRICT_TIME_MINUTE_SHORT + item.GetValue());
							break;
						}

						case TRVT_ENGINE_CLASS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_engine_class_value, item.GetValue()));
							break;

						case TRVT_PF_PENALTY_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_pf_penalty_control_value, item.GetValue()));
							break;

						case TRVT_SPEED_ADAPTATION_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_speed_adaptation_control_value, item.GetValue()));
							break;

						case TRVT_SIGNAL_MODE_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_signal_mode_control_value, item.GetValue()));
							break;

						case TRVT_ORDER_TARGET_DIAGDIR:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->SetString(GetDropDownStringByValue(&_diagdir_value, item.GetValue()));
							this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->SetString(GetDropDownStringByValue(&_target_direction_aux_value, item.GetAuxField()));
							break;

						case TRVT_LABEL_INDEX:
							right_sel->SetDisplayedPlane(DPR_LABEL_BUTTON);
							break;

						default:
							break;
					}
				}

				this->EnableWidget(TR_WIDGET_INSERT);
				this->EnableWidget(TR_WIDGET_REMOVE);
			}
			if (this->IsUpDownBtnUsable(true)) this->EnableWidget(TR_WIDGET_UP_BTN);
			if (this->IsUpDownBtnUsable(false)) this->EnableWidget(TR_WIDGET_DOWN_BTN);
			if (this->IsDuplicateBtnUsable()) this->EnableWidget(TR_WIDGET_DUPLICATE);
		}

		this->SetDirty();
	}

	/**
	 * Show a drop down list using @p list_set, setting the pre-selected item to the one corresponding to @p value
	 * This asserts if @p value is not in @p list_set, and @p missing_ok is false
	 */
	void ShowDropDownListWithValue(const TraceRestrictDropDownListSet *list_set, uint value, bool missing_ok,
			int button, uint32_t disabled_mask, uint32_t hidden_mask)
	{
		this->drop_down_list_mapping[button] = list_set;
		int selected = GetDropDownListIndexByValue(list_set, value, missing_ok);
		if (button == TR_WIDGET_VALUE_DROPDOWN) this->value_drop_down_is_company = false;

		ShowDropDownMenu(this, list_set->string_array, selected, button, disabled_mask, hidden_mask);
	}

	/**
	 * Show a drop down list using @p list_set, setting the pre-selected item to the one corresponding to @p value
	 * This asserts if @p value is not in @p list_set, and @p missing_ok is false
	 */
	void ShowCompanyDropDownListWithValue(CompanyID value, [[maybe_unused]] bool missing_ok, int button)
	{
		DropDownList list;

		for (const Company *c : Company::Iterate()) {
			list.emplace_back(MakeCompanyDropDownListItem(c->index));
			if (c->index == value) missing_ok = true;
		}
		list.push_back(MakeDropDownListStringItem(STR_TRACE_RESTRICT_UNDEFINED_COMPANY, INVALID_COMPANY, false));
		if (INVALID_COMPANY == value) missing_ok = true;

		assert(missing_ok == true);
		assert(button == TR_WIDGET_VALUE_DROPDOWN);
		this->value_drop_down_is_company = true;

		ShowDropDownList(this, std::move(list), value, button, 0);
	}

	/**
	 * Helper function to set or unset a SetObjectToPlaceWnd, for the given widget and cursor type
	 */
	void SetObjectToPlaceAction(WidgetID widget, CursorID cursor)
	{
		if (this->current_placement_widget != -1 && widget != this->current_placement_widget) {
			ResetObjectToPlace();
		}
		this->ToggleWidgetLoweredState(widget);
		this->SetWidgetDirty(widget);
		if (this->IsWidgetLowered(widget)) {
			SetObjectToPlaceWnd(cursor, PAL_NONE, HT_RECT, this);
			this->current_placement_widget = widget;
		} else {
			ResetObjectToPlace();
			this->current_placement_widget = -1;
		}
		this->UpdatePlaceObjectPlanes();
	}

	void ResetObjectToPlaceAction()
	{
		if (this->current_placement_widget != -1) {
			this->RaiseWidgetWhenLowered(this->current_placement_widget);
		}
		this->current_placement_widget = -1;
		this->UpdatePlaceObjectPlanes();
	}

	/**
	 * This used for testing whether else or else-if blocks could be inserted, or replace the selection
	 * If @p replace is true, replace selection with @p item, else insert @p item before selection
	 * Returns true if resulting instruction list passes validation
	 */
	bool GenericElseInsertionDryRun(TraceRestrictInstructionItem item, bool replace)
	{
		if (this->selected_instruction < 1) return false;
		uint offset = this->selected_instruction - 1;

		const TraceRestrictProgram *prog = this->GetProgram();
		if (prog == nullptr) return false;

		std::vector<TraceRestrictProgramItem> items = prog->items; // copy

		if (offset >= (TraceRestrictGetInstructionCount(items) + (replace ? 0 : 1))) return false; // off the end of the program

		auto iter = TraceRestrictInstructionIteratorAt(items, offset);
		if (replace) {
			iter.InstructionRef() = item;
		} else {
			items.insert(iter.ItemIter(), item.AsProgramItem());
		}

		TraceRestrictProgramActionsUsedFlags actions_used_flags;
		return TraceRestrictProgram::Validate(items, actions_used_flags).Succeeded();
	}

	/**
	 * Run GenericElseInsertionDryRun with an else instruction
	 */
	bool ElseInsertionDryRun(bool replace)
	{
		TraceRestrictInstructionItem item{};
		item.SetType(TRIT_COND_ENDIF);
		item.SetCondFlags(TRCF_ELSE);
		return this->GenericElseInsertionDryRun(item, replace);
	}

	/**
	 * Run GenericElseInsertionDr;yRun with an elif instruction
	 */
	bool ElseIfInsertionDryRun(bool replace)
	{
		TraceRestrictInstructionItem item{};
		item.SetType(TRIT_COND_UNDEFINED);
		item.SetCondFlags(TRCF_ELSE);
		return this->GenericElseInsertionDryRun(item, replace);
	}
};

static constexpr NWidgetPart _nested_program_widgets[] = {
	/* Title bar */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, TR_WIDGET_CAPTION), SetStringTip(STR_TRACE_RESTRICT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEBUGBOX, COLOUR_GREY),
		NWidget(WWT_IMGBTN, COLOUR_GREY, TR_WIDGET_HIGHLIGHT), SetAspect(1), SetSpriteTip(SPR_SHARED_ORDERS_ICON, STR_TRACE_RESTRICT_HIGHLIGHT_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	/* Program display */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, TR_WIDGET_INSTRUCTION_LIST), SetMinimalSize(372, 62), SetToolTip(STR_TRACE_RESTRICT_INSTRUCTION_LIST_TOOLTIP),
				SetResize(1, 1), SetScrollbar(TR_WIDGET_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TR_WIDGET_SCROLLBAR),
	EndContainer(),

	/* Button Bar */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_UP_BTN), SetAspect(WidgetDimensions::ASPECT_UP_DOWN_BUTTON), SetSpriteTip(SPR_ARROW_UP, STR_TRACE_RESTRICT_UP_BTN_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_DOWN_BTN), SetAspect(WidgetDimensions::ASPECT_UP_DOWN_BUTTON), SetSpriteTip(SPR_ARROW_DOWN, STR_TRACE_RESTRICT_DOWN_BTN_TOOLTIP),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT_2),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_TYPE_NONCOND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_TYPE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_CONDFLAGS), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_CONDFLAGS_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_L2), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_TYPE_COND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_TYPE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_COUNTER_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_COUNTER_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_L), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT_AUX),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_LEFT_AUX_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_MIDDLE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_COND_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_SLOT_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_SLOT_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_M), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_RIGHT),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_INT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_JUST_STRING1, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DECIMAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_JUST_DECIMAL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_VALUE_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetToolTip(STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DEST), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_SELECT_TARGET, STR_TRACE_RESTRICT_SELECT_TARGET), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_SIGNAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_SELECT_SIGNAL, STR_TRACE_RESTRICT_SELECT_SIGNAL), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_TILE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_SELECT_TILE, STR_TRACE_RESTRICT_SELECT_TILE), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_LABEL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_ORDER_LABEL_TEXT_BUTTON, STR_ORDER_LABEL_TEXT_BUTTON_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_R), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_GOTO_SIGNAL), SetAspect(WidgetDimensions::ASPECT_LOCATION), SetSpriteTip(SPR_GOTO_LOCATION, STR_TRACE_RESTRICT_GOTO_SIGNAL_TOOLTIP),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_INSERT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_INSERT, STR_TRACE_RESTRICT_INSERT_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_REMOVE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_REMOVE, STR_TRACE_RESTRICT_REMOVE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_RESET), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_RESET, STR_TRACE_RESTRICT_RESET_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_COPY),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_COPY), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_COPY, STR_TRACE_RESTRICT_COPY_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_COPY_APPEND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_APPEND, STR_TRACE_RESTRICT_COPY_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_DUPLICATE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_DUPLICATE, STR_TRACE_RESTRICT_DUPLICATE_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_SHARE),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_SHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_SHARE, STR_NULL), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TR_WIDGET_UNSHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_UNSHARE, STR_NULL), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_SHARE_ONTO), SetMinimalSize(124, 12), SetFill(1, 0),
														SetStringTip(STR_TRACE_RESTRICT_SHARE_ONTO, STR_NULL), SetResize(1, 0),
				EndContainer(),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _program_desc(__FILE__, __LINE__,
	WDP_AUTO, "trace_restrict_gui", 384, 100,
	WC_TRACE_RESTRICT, WC_BUILD_SIGNAL,
	WindowDefaultFlag::Construction,
	_nested_program_widgets
);

/**
 * Show or create program window for given @p tile and @p track
 */
void ShowTraceRestrictProgramWindow(TileIndex tile, Track track)
{
	if (BringWindowToFrontById(WC_TRACE_RESTRICT, MakeTraceRestrictRefId(tile, track)) != nullptr) {
		return;
	}

	new TraceRestrictWindow(_program_desc, tile, track);
}

/** Slot GUI widget IDs */
enum TraceRestrictSlotWindowWidgets : WidgetID {
	WID_TRSL_LIST_VEHICLE, // this must be first, see: DirtyVehicleListWindowForVehicle
	WID_TRSL_CAPTION,
	WID_TRSL_ALL_VEHICLES,
	WID_TRSL_LIST_SLOTS,
	WID_TRSL_LIST_SLOTS_SCROLLBAR,
	WID_TRSL_CREATE_SLOT,
	WID_TRSL_DELETE_SLOT,
	WID_TRSL_RENAME_SLOT,
	WID_TRSL_NEW_GROUP,
	WID_TRSL_COLLAPSE_ALL_GROUPS,
	WID_TRSL_EXPAND_ALL_GROUPS,
	WID_TRSL_SLOT_PUBLIC,
	WID_TRSL_SET_SLOT_MAX_OCCUPANCY,
	WID_TRSL_SORT_BY_ORDER,
	WID_TRSL_SORT_BY_DROPDOWN,
	WID_TRSL_FILTER_BY_CARGO,
	WID_TRSL_FILTER_BY_CARGO_SEL,
	WID_TRSL_LIST_VEHICLE_SCROLLBAR,
};


static constexpr NWidgetPart _nested_slot_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_TRSL_CAPTION), SetStringTip(STR_TRACE_RESTRICT_SLOT_CAPTION, STR_NULL),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		/* left part */
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalTextLines(1, WidgetDimensions::unscaled.dropdowntext.Vertical()), SetFill(1, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_TRSL_ALL_VEHICLES), SetFill(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_TRSL_LIST_SLOTS), SetMatrixDataTip(1, 0, STR_TRACE_RESTRICT_SLOT_GUI_LIST_TOOLTIP),
						SetFill(1, 0), SetResize(0, 1), SetScrollbar(WID_TRSL_LIST_SLOTS_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_TRSL_LIST_SLOTS_SCROLLBAR),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_CREATE_SLOT), SetFill(0, 1),
						SetToolTip(STR_TRACE_RESTRICT_SLOT_CREATE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_DELETE_SLOT), SetFill(0, 1),
						SetToolTip(STR_TRACE_RESTRICT_SLOT_DELETE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_RENAME_SLOT), SetFill(0, 1),
						SetToolTip(STR_TRACE_RESTRICT_SLOT_RENAME_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRSL_NEW_GROUP), SetFill(0, 1),
						SetStringTip(STR_TRACE_RESTRICT_NEW_SLOT_GROUP, STR_TRACE_RESTRICT_NEW_SLOT_GROUP_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRSL_COLLAPSE_ALL_GROUPS), SetFill(0, 1),
						SetStringTip(STR_GROUP_COLLAPSE_ALL, STR_GROUP_COLLAPSE_ALL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRSL_EXPAND_ALL_GROUPS), SetFill(0, 1),
						SetStringTip(STR_GROUP_EXPAND_ALL, STR_GROUP_EXPAND_ALL),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), EndContainer(),
				NWidget(WWT_IMGBTN, COLOUR_GREY, WID_TRSL_SLOT_PUBLIC), SetFill(0, 1),
						SetSpriteTip(SPR_IMG_GOAL, STR_TRACE_RESTRICT_SLOT_PUBLIC_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_SET_SLOT_MAX_OCCUPANCY), SetFill(0, 1),
						SetSpriteTip(SPR_IMG_SETTINGS, STR_TRACE_RESTRICT_SLOT_SET_MAX_OCCUPANCY_TOOLTIP),
			EndContainer(),
		EndContainer(),
		/* right part */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRSL_SORT_BY_ORDER), SetMinimalSize(81, 12), SetStringTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_TRSL_SORT_BY_DROPDOWN), SetMinimalSize(167, 12), SetToolTip(STR_TOOLTIP_SORT_CRITERIA),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_TRSL_FILTER_BY_CARGO_SEL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_TRSL_FILTER_BY_CARGO), SetMinimalSize(167, 12), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
				EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_TRSL_LIST_VEHICLE), SetMinimalSize(248, 0), SetMatrixDataTip(1, 0, STR_NULL), SetResize(1, 1), SetFill(1, 0), SetScrollbar(WID_TRSL_LIST_VEHICLE_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_TRSL_LIST_VEHICLE_SCROLLBAR),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(1, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), SetResize(1, 0), EndContainer(),
				NWidget(WWT_RESIZEBOX, COLOUR_GREY),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

class TraceRestrictSlotWindow : public BaseVehicleListWindow {
	struct GUISlotListItem {
		SlotItem item;       ///< Slot/group item
		uint8_t indent;      ///< Display indentation level.
		uint16_t level_mask; ///< Bitmask of indentation continuation.

		constexpr GUISlotListItem(SlotItem item, int8_t indent) : item(item), indent(indent), level_mask(0) {}
	};

private:
	/* Columns in the group list */
	enum ListColumns : uint8_t {
		VGC_FOLD,          ///< Fold / Unfold button.
		VGC_NAME,          ///< Slot name.
		VGC_PUBLIC,        ///< Slot public state.
		VGC_NUMBER,        ///< Slot occupancy numbers.

		VGC_END
	};

	enum class QuerySelectorMode : uint8_t {
		None,
		Rename,
		SetMaxOccupancy,
	};
	QuerySelectorMode qsm_mode = QuerySelectorMode::None; ///< Query selector mode

	SlotItem slot_sel{};     ///< Selected slot
	SlotItem slot_query{};   ///< Slot/group being created, renamed or max occupancy changed
	SlotItem slot_over{};    ///< Slot over which a vehicle is dragged
	SlotItem slot_confirm{}; ///< Slot awaiting delete confirmation
	SlotItem slot_drag{};    ///< Slot being dragged
	GUIList<GUISlotListItem> slots; ///< List of slots
	uint tiny_step_height;            ///< Step height for the slot list
	Scrollbar *slot_sb;

	Dimension column_size[VGC_END]; ///< Size of the columns in the group list.

	/**
	 * (Re)Build the slot list.
	 *
	 * @param owner The owner of the window
	 */
	void BuildSlotList(Owner owner)
	{
		if (!this->slots.NeedRebuild()) return;

		struct ListItem {
			SlotItem item;
			TraceRestrictSlotGroupID parent;
			const std::string *name; // This is not a reference so that ListItem can be swapped/move-assigned
		};
		std::vector<ListItem> list;

		for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
			if (slot->owner == owner && slot->vehicle_type == this->vli.vtype) {
				list.push_back({ SlotItem{ SlotItemType::Slot, slot->index }, slot->parent_group, &(slot->name) });
			}
		}
		for (const TraceRestrictSlotGroup *sg : TraceRestrictSlotGroup::Iterate()) {
			if (sg->owner == owner && sg->vehicle_type == this->vli.vtype) {
				list.push_back({ SlotItem{ SlotItemType::Group, sg->index }, sg->parent, &(sg->name) });
			}
		}

		/* Sort the slots/groups by their parent group, then their name */
		std::sort(list.begin(), list.end(), [&](const ListItem &a, const ListItem &b) {
			if (a.parent != b.parent) return a.parent < b.parent;

			int r = StrNaturalCompare(*(a.name), *(b.name)); // Sort by name (natural sorting).
			if (r == 0) return a.item < b.item;
			return r < 0;
		});

		this->slots.clear();

		bool enable_expand_all = false;
		bool enable_collapse_all = false;

		auto output_items = y_combinator([&](auto output_items, uint indent, TraceRestrictSlotGroupID parent_filter) -> uint {
			uint seen = 0;
			for (const ListItem &item : list) {
				if (item.parent != parent_filter) continue;

				this->slots.emplace_back(item.item, indent);
				seen++;

				if (item.item.type == SlotItemType::Group) {
					TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::Get(item.item.id);
					if (sg->folded) {
						/* Test if this group has children at all. If not, the folded flag should be cleared to avoid lingering unfold buttons in the list. */
						bool has_children = std::any_of(list.begin(), list.end(), [&](const ListItem &it) { return it.parent == item.item.id; });
						if (has_children) {
							enable_expand_all = true;
						} else {
							sg->folded = false;
						}
					} else {
						uint children = output_items(indent + 1, item.item.id);
						if (children > 0) enable_collapse_all = true;
					}
				}
			}

			return seen;
		});
		output_items(0, INVALID_TRACE_RESTRICT_SLOT_GROUP);

		this->SetWidgetDisabledState(WID_TRSL_EXPAND_ALL_GROUPS, !enable_expand_all);
		this->SetWidgetDisabledState(WID_TRSL_COLLAPSE_ALL_GROUPS, !enable_collapse_all);

		if (!this->slots.empty()) {
			/* Hierarchy is complete, traverse in reverse to find where indentation levels continue. */
			uint16_t level_mask = 0;
			for (auto it = std::rbegin(this->slots); std::next(it) != std::rend(this->slots); ++it) {
				auto next_it = std::next(it);
				AssignBit(level_mask, it->indent, it->indent <= next_it->indent);
				next_it->level_mask = level_mask;
			}
		}

		this->slots.RebuildDone();

		/* Change selection if slot/group is currently hidden by fold */
		SlotItem it = this->slot_sel;
		while (it.type == SlotItemType::Slot || it.type == SlotItemType::Group) {
			it = it.GetParentItem();
			if (it.type == SlotItemType::Group && TraceRestrictSlotGroup::Get(it.id)->folded) {
				this->slot_sel = it;
				this->vli.index = INVALID_TRACE_RESTRICT_SLOT_ID;
				this->vehgroups.ForceRebuild();
			}
		}
	}

	/**
	 * Compute tiny_step_height and column_size
	 * @return Total width required for the group list.
	 */
	uint ComputeSlotInfoSize()
	{
		this->column_size[VGC_FOLD] = maxdim(GetSpriteSize(SPR_CIRCLE_FOLDED), GetSpriteSize(SPR_CIRCLE_UNFOLDED));
		this->tiny_step_height = this->column_size[VGC_FOLD].height;

		this->column_size[VGC_NAME] = GetStringBoundingBox(STR_GROUP_ALL_TRAINS);
		this->column_size[VGC_NAME].width = std::max((170u * GetCharacterHeight(FS_NORMAL)) / 10u, this->column_size[VGC_NAME].width);
		this->tiny_step_height = std::max(this->tiny_step_height, this->column_size[VGC_NAME].height);

		SetDParamMaxValue(0, 9999, 3, FS_SMALL);
		SetDParamMaxValue(1, 9999, 3, FS_SMALL);
		this->column_size[VGC_NUMBER] = GetStringBoundingBox(STR_TRACE_RESTRICT_SLOT_MAX_OCCUPANCY);
		this->tiny_step_height = std::max(this->tiny_step_height, this->column_size[VGC_NUMBER].height);

		this->column_size[VGC_PUBLIC] = GetScaledSpriteSize(SPR_BLOT);
		this->tiny_step_height = std::max(this->tiny_step_height, this->column_size[VGC_PUBLIC].height);

		this->tiny_step_height += WidgetDimensions::scaled.matrix.Vertical();

		return WidgetDimensions::scaled.framerect.Horizontal() +
				this->column_size[VGC_FOLD].width + WidgetDimensions::scaled.hsep_normal +
				this->column_size[VGC_NAME].width + WidgetDimensions::scaled.hsep_wide +
				this->column_size[VGC_PUBLIC].width + WidgetDimensions::scaled.hsep_wide +
				this->column_size[VGC_NUMBER].width + WidgetDimensions::scaled.hsep_normal;
	}

	/**
	 * Draw a row in the slot list.
	 * @param draw_area Area to draw in.
	 * @param item GUI item.
	 */
	void DrawSlotInfo(Rect draw_area, const GUISlotListItem &item, bool has_shown_children = false) const
	{
		/* Highlight the slot if a vehicle is dragged over it */
		if (item.item == this->slot_over) {
			GfxFillRect(draw_area, GetColourGradient(COLOUR_GREY, SHADE_LIGHTEST));
		}

		const bool rtl = _current_text_dir == TD_RTL;
		Rect info_area = draw_area.Indent(WidgetDimensions::scaled.hsep_normal + this->column_size[VGC_FOLD].width, rtl).Indent(WidgetDimensions::scaled.hsep_normal, !rtl);

		/* draw the selected slot in white, else we draw it in black */
		TextColour colour = item.item == this->slot_sel ? TC_WHITE : TC_BLACK;

		Rect r = info_area.Indent(WidgetDimensions::scaled.vsep_wide + this->column_size[VGC_NUMBER].width, !rtl);

		switch (item.item.type) {
			case SlotItemType::Slot: {
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(item.item.id);
				if (slot == nullptr) break;

				Rect sub = info_area.WithWidth(this->column_size[VGC_NUMBER].width, !rtl);
				SetDParam(0, slot->occupants.size());
				SetDParam(1, slot->max_occupancy);
				DrawString(sub.left, sub.right - 1, sub.top + (this->tiny_step_height - this->column_size[VGC_NUMBER].height) / 2, STR_TRACE_RESTRICT_SLOT_MAX_OCCUPANCY, colour, SA_RIGHT | SA_FORCE);

				if (HasFlag(slot->flags, TraceRestrictSlot::Flags::Public)) {
					DrawSpriteIgnorePadding(SPR_BLOT, PALETTE_TO_BLUE, r.WithWidth(this->column_size[VGC_PUBLIC].width, !rtl), SA_CENTER);
				}
				break;
			}

			case SlotItemType::Group: {
				const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(item.item.id);
				if (sg == nullptr) break;

				if (has_shown_children || sg->folded) {
					/* draw fold / unfold button */
					Rect sub = draw_area.Indent(WidgetDimensions::scaled.hsep_indent * item.indent, rtl).WithWidth(this->column_size[VGC_FOLD].width, rtl);
					DrawSprite(sg->folded ? SPR_CIRCLE_FOLDED : SPR_CIRCLE_UNFOLDED, PAL_NONE, sub.left, sub.top + (this->tiny_step_height - this->column_size[VGC_FOLD].height) / 2);
				}
				break;
			}

			default:
				break;
		}

		r = r.Indent(WidgetDimensions::scaled.vsep_wide + this->column_size[VGC_PUBLIC].width, !rtl).Indent(WidgetDimensions::scaled.hsep_indent * item.indent, rtl);

		/* draw slot name */
		StringID str = STR_NULL;
		switch (item.item.type) {
			case SlotItemType::Slot:
				SetDParam(0, item.item.id);
				str = STR_TRACE_RESTRICT_SLOT_NAME;
				break;

			case SlotItemType::Group:
				SetDParam(0, item.item.id);
				str = STR_TRACE_RESTRICT_SLOT_GROUP_NAME;
				break;

			case SlotItemType::Special:
				str = STR_GROUP_ALL_TRAINS + this->vli.vtype;
				break;

			default:
				break;
		}
		DrawString(r.left, r.right - 1, r.top + (this->tiny_step_height - this->column_size[VGC_NAME].height) / 2, str, colour);
	}

	/**
	 * Mark the widget containing the currently highlighted slot as dirty.
	 */
	void DirtyHighlightedSlotWidget()
	{
		if (this->slot_over.IsNone()) return;

		if (this->slot_over == SlotItem{ SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID }) {
			this->SetWidgetDirty(WID_TRSL_ALL_VEHICLES);
		} else {
			this->SetWidgetDirty(WID_TRSL_LIST_SLOTS);
		}
	}

	void SetAllSlotGroupsFoldState(bool folded)
	{
		for (TraceRestrictSlotGroup *sg : TraceRestrictSlotGroup::Iterate()) {
			if (sg->owner == this->owner && sg->vehicle_type == this->vli.vtype) {
				sg->folded = folded;
			}
		}
		this->slots.ForceRebuild();
		this->SetDirty();
	}

public:
	TraceRestrictSlotWindow(WindowDesc &desc, WindowNumber window_number, const VehicleListIdentifier &vli) : BaseVehicleListWindow(desc, vli)
	{
		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_TRSL_LIST_VEHICLE_SCROLLBAR);
		this->slot_sb = this->GetScrollbar(WID_TRSL_LIST_SLOTS_SCROLLBAR);
		this->sorting = &_sorting[GB_NONE].train;
		this->grouping = GB_NONE;

		this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		this->slot_sel = { SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID };

		this->vehgroups.SetListing(*this->sorting);
		this->vehgroups.ForceRebuild();
		this->vehgroups.NeedResort();

		this->BuildVehicleList();
		this->SortVehicleList();

		this->slots.ForceRebuild();
		this->slots.NeedResort();
		this->BuildSlotList(vli.company);

		this->GetWidget<NWidgetCore>(WID_TRSL_CREATE_SLOT)->SetSprite(SPR_GROUP_CREATE_TRAIN + this->vli.vtype);
		this->GetWidget<NWidgetCore>(WID_TRSL_RENAME_SLOT)->SetSprite(SPR_GROUP_RENAME_TRAIN + this->vli.vtype);
		this->GetWidget<NWidgetCore>(WID_TRSL_DELETE_SLOT)->SetSprite(SPR_GROUP_DELETE_TRAIN + this->vli.vtype);
		this->GetWidget<NWidgetCore>(WID_TRSL_LIST_VEHICLE)->SetToolTip(STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP + this->vli.vtype);

		this->FinishInitNested(window_number);
		this->owner = vli.company;
	}

	void Close(int data = 0) override
	{
		*this->sorting = this->vehgroups.GetListing();
		this->Window::Close();
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_TRSL_LIST_SLOTS: {
				size.width = this->ComputeSlotInfoSize();
				resize.height = this->tiny_step_height;

				/* Minimum height is the height of the list widget minus all vehicles... */
				size.height = 4 * GetVehicleListHeight(this->vli.vtype, this->tiny_step_height) - this->tiny_step_height;

				/* ... minus the buttons at the bottom ... */
				uint max_icon_height = GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_CREATE_SLOT)->GetSprite()).height;
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_DELETE_SLOT)->GetSprite()).height);
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_RENAME_SLOT)->GetSprite()).height);
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_SLOT_PUBLIC)->GetSprite()).height);
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_SET_SLOT_MAX_OCCUPANCY)->GetSprite()).height);

				/* Get a multiple of tiny_step_height of that amount */
				size.height = Ceil(size.height - max_icon_height, tiny_step_height);
				break;
			}

			case WID_TRSL_ALL_VEHICLES:
				size.width = this->ComputeSlotInfoSize();
				size.height = this->tiny_step_height;
				break;

			case WID_TRSL_SORT_BY_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->GetString());
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}

			case WID_TRSL_LIST_VEHICLE:
				this->ComputeSlotInfoSize();
				resize.height = GetVehicleListHeight(this->vli.vtype, this->tiny_step_height);
				size.height = 4 * resize.height;
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->vehgroups.ForceRebuild();
			this->slots.ForceRebuild();
		} else {
			this->vehgroups.ForceResort();
			this->slots.ForceResort();
		}

		/* Process ID-invalidation in command-scope as well */
		if (this->slot_query.IsInvalid()) {
			CloseWindowByClass(WC_QUERY_STRING);
			this->slot_query = {};
		}
		if (this->slot_sel.IsInvalid()) {
			this->slot_sel = { SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID };
			this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		}

		this->SetDirty();
	}

	virtual void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_TRSL_FILTER_BY_CARGO:
				SetDParam(0, this->GetCargoFilterLabel(this->cargo_filter_criteria));
				break;

			case WID_TRSL_CAPTION:
				SetDParam(0, STR_VEHICLE_TYPE_TRAINS + this->vli.vtype);
				break;
		}
	}

	virtual void OnPaint() override
	{
		this->BuildSlotList(this->owner);

		/* If we select the all vehicles, this->list will contain all vehicles of the owner
		 * else this->list will contain all vehicles which belong to the selected group */
		this->BuildVehicleList();
		this->SortVehicleList();

		this->slot_sb->SetCount(static_cast<uint>(this->slots.size()));
		this->vscroll->SetCount(static_cast<uint>(this->vehgroups.size()));

		/* Disable the slot specific function when we select all vehicles */
		this->SetWidgetsDisabledState(this->slot_sel.type != SlotItemType::Slot || _local_company != this->vli.company,
				WID_TRSL_SLOT_PUBLIC,
				WID_TRSL_SET_SLOT_MAX_OCCUPANCY);
		this->SetWidgetsDisabledState((this->slot_sel.type != SlotItemType::Slot && this->slot_sel.type != SlotItemType::Group) || _local_company != this->vli.company,
				WID_TRSL_DELETE_SLOT,
				WID_TRSL_RENAME_SLOT);

		this->SetWidgetLoweredState(WID_TRSL_SLOT_PUBLIC, this->slot_sel.type == SlotItemType::Slot && TraceRestrictSlot::IsValidID(this->slot_sel.id) && HasFlag(TraceRestrictSlot::Get(this->slot_sel.id)->flags, TraceRestrictSlot::Flags::Public));

		/* Disable remaining buttons for non-local companies
		 * Needed while changing _local_company, eg. by cheats
		 * All procedures (eg. move vehicle to a slot)
		 *  verify, whether you are the owner of the vehicle,
		 *  so it doesn't have to be disabled
		 */
		this->SetWidgetsDisabledState(_local_company != this->vli.company,
				WID_TRSL_CREATE_SLOT, WID_TRSL_NEW_GROUP);

		/* Set text of sort by dropdown */
		this->GetWidget<NWidgetCore>(WID_TRSL_SORT_BY_DROPDOWN)->SetString(this->GetVehicleSorterNames()[this->vehgroups.SortType()]);

		this->GetWidget<NWidgetCore>(WID_TRSL_FILTER_BY_CARGO)->SetString(this->GetCargoFilterLabel(this->cargo_filter_criteria));

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES:
				DrawSlotInfo(r.WithHeight(this->tiny_step_height).Shrink(WidgetDimensions::scaled.framerect), { SlotItem{ SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID }, 0 });
				break;

			case WID_TRSL_LIST_SLOTS: {
				Rect ir = r.WithHeight(this->tiny_step_height).Shrink(WidgetDimensions::scaled.framerect);
				const int max = std::min<int>(this->slot_sb->GetPosition() + this->slot_sb->GetCapacity(), static_cast<int>(this->slots.size()));
				for (int i = this->slot_sb->GetPosition(); i < max; ++i) {
					const GUISlotListItem &item = this->slots[i];

					bool has_shown_children = false;
					if (item.item.type == SlotItemType::Group) {
						has_shown_children = i + 1 < max && this->slots[i + 1].indent > item.indent;
					}
					DrawSlotInfo(ir, item, has_shown_children);

					ir.top += this->tiny_step_height;
					ir.bottom += this->tiny_step_height;
				}
				break;
			}

			case WID_TRSL_SORT_BY_ORDER:
				this->DrawSortButtonState(WID_TRSL_SORT_BY_ORDER, this->vehgroups.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_TRSL_LIST_VEHICLE:
				this->DrawVehicleListItems(this->vehicle_sel, this->resize.step_height, r);
				break;
		}
	}

	static void DeleteSlotCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			TraceRestrictSlotWindow *w = (TraceRestrictSlotWindow*)win;
			if (w->slot_confirm.type == SlotItemType::Slot) {
				Command<CMD_DELETE_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_DELETE, w->slot_confirm.id);
			} else if (w->slot_confirm.type == SlotItemType::Group) {
				Command<CMD_DELETE_TRACERESTRICT_SLOT_GROUP>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_DELETE, w->slot_confirm.id);
			}
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_ORDER: // Flip sorting method ascending/descending
				this->vehgroups.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_TRSL_SORT_BY_DROPDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->GetVehicleSorterNames(), this->vehgroups.SortType(), WID_TRSL_SORT_BY_DROPDOWN, 0,
						this->GetSorterDisableMask(this->vli.vtype));
				return;

			case WID_TRSL_FILTER_BY_CARGO: // Cargo filter dropdown
				ShowDropDownList(this, this->BuildCargoDropDownList(false), this->cargo_filter_criteria, widget);
				break;

			case WID_TRSL_ALL_VEHICLES: // All vehicles button
				if (this->vli.index != ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
					this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
					this->slot_sel = { SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID };
					this->vehgroups.ForceRebuild();
					this->SetDirty();
				}
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix Slot
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s >= this->slots.size()) return;

				const GUISlotListItem &clicked = this->slots[id_s];
				if (clicked.item.type == SlotItemType::Group) {
					TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::Get(clicked.item.id);
					if (sg->folded || (id_s + 1 < this->slots.size() && this->slots[id_s + 1].indent > clicked.indent)) {
						/* The slot group has children, check if the user clicked the fold / unfold button. */
						NWidgetCore *group_display = this->GetWidget<NWidgetCore>(widget);
						int x = _current_text_dir == TD_RTL ?
								group_display->pos_x + group_display->current_x - WidgetDimensions::scaled.framerect.right - clicked.indent * WidgetDimensions::scaled.hsep_indent - this->column_size[VGC_FOLD].width :
								group_display->pos_x + WidgetDimensions::scaled.framerect.left + clicked.indent * WidgetDimensions::scaled.hsep_indent;
						if (click_count > 1 || (pt.x >= x && pt.x < (int)(x + this->column_size[VGC_FOLD].width))) {
							sg->folded = !sg->folded;
							this->slots.ForceRebuild();
							this->SetDirty();
							break;
						}
					}

					this->slot_sel = clicked.item;
					this->vli.index = INVALID_TRACE_RESTRICT_SLOT_ID;
				} else {
					this->slot_sel = clicked.item;
					this->vli.index = clicked.item.id;
				}

				this->slot_drag = this->slot_sel;
				SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);

				this->vehgroups.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_TRSL_LIST_VEHICLE: { // Matrix Vehicle
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_VEHICLE);
				if (id_v >= this->vehgroups.size()) return; // click out of list bound

				const Vehicle *v = this->vehgroups[id_v].GetSingleVehicle();
				if (VehicleClicked(v)) break;

				this->vehicle_sel = v->index;

				SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
				SetMouseCursorVehicle(v, EIT_IN_LIST);
				_cursor.vehchain = true;

				this->SetDirty();
				break;
			}

			case WID_TRSL_CREATE_SLOT: { // Create a new slot
				this->ShowCreateSlotWindow();
				break;
			}

			case WID_TRSL_DELETE_SLOT: { // Delete the selected slot
				this->slot_confirm = this->slot_sel;
				ShowQuery(STR_TRACE_RESTRICT_SLOT_QUERY_DELETE_CAPTION, STR_TRACE_RESTRICT_SLOT_DELETE_QUERY_TEXT, this, DeleteSlotCallback);
				break;
			}

			case WID_TRSL_RENAME_SLOT: // Rename the selected slot
				this->ShowRenameSlotWindow();
				break;

			case WID_TRSL_SLOT_PUBLIC: { // Toggle public state of the selected slot
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(this->vli.index);
				if (slot != nullptr) {
					Command<CMD_ALTER_TRACERESTRICT_SLOT>::Post(STR_ERROR_CAN_T_DO_THIS, this->vli.index, TRASO_SET_PUBLIC, HasFlag(slot->flags, TraceRestrictSlot::Flags::Public) ? 0 : 1, {});
				}
				break;
			}

			case WID_TRSL_SET_SLOT_MAX_OCCUPANCY: // Set max occupancy of the selected slot
				this->ShowSetSlotMaxOccupancyWindow();
				break;

			case WID_TRSL_NEW_GROUP:
				this->ShowCreateSlotGroupWindow();
				break;

			case WID_TRSL_COLLAPSE_ALL_GROUPS:
				this->SetAllSlotGroupsFoldState(true);
				break;

			case WID_TRSL_EXPAND_ALL_GROUPS:
				this->SetAllSlotGroupsFoldState(false);
				break;
		}
	}

	void OnDragDrop_Vehicle(Point pt, WidgetID widget)
	{
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // All vehicles
				if (this->slot_sel.type == SlotItemType::Slot) {
					Command<CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_REMOVE_VEHICLE, this->slot_sel.id, this->vehicle_sel);

					this->vehicle_sel = INVALID_VEHICLE;
					this->slot_over = {};

					this->SetDirty();
				}
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix slot
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->slot_over = {};
				this->SetDirty();

				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s >= this->slots.size()) return; // click out of list bound

				const GUISlotListItem &item = this->slots[id_s];
				if (item.item.type != SlotItemType::Slot) return; // Not a slot

				if (_ctrl_pressed && this->slot_sel.type == SlotItemType::Slot) {
					/* Remove from old group */
					Command<CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_REMOVE_VEHICLE, this->slot_sel.id, vindex);
				}
				Command<CMD_ADD_VEHICLE_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_ADD_VEHICLE, item.item.id, vindex);
				break;
			}

			case WID_TRSL_LIST_VEHICLE: { // Matrix vehicle
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->slot_over = {};
				this->SetDirty();

				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_VEHICLE);
				if (id_v >= this->vehgroups.size()) return; // click out of list bound

				const Vehicle *v = this->vehgroups[id_v].GetSingleVehicle();
				if (!VehicleClicked(v) && vindex == v->index) {
					ShowVehicleViewWindow(v);
				}
				break;
			}
		}
	}

	void OnDragDrop_Slot(Point pt, WidgetID widget)
	{
		if (this->slot_drag.IsInvalid()) {
			this->vehicle_sel = INVALID_VEHICLE;
			this->slot_drag = {};
			this->SetDirty();
			return;
		}

		auto set_parent = [&](TraceRestrictSlotGroupID parent) {
			if (this->slot_drag.type == SlotItemType::Slot) {
				Command<CMD_ALTER_TRACERESTRICT_SLOT>::Post(STR_ERROR_GROUP_CAN_T_SET_PARENT, this->slot_drag.id, TRASO_SET_PARENT_GROUP, parent, {});
			} else if (this->slot_drag.type == SlotItemType::Group) {
				Command<CMD_ALTER_TRACERESTRICT_SLOT_GROUP>::Post(STR_ERROR_GROUP_CAN_T_SET_PARENT, this->slot_drag.id, TRASGO_SET_PARENT_GROUP, parent, {});
			}
		};

		TraceRestrictSlotGroupID current_parent = this->slot_drag.GetInfo().parent;
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // All vehicles
				if (current_parent != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
					set_parent(INVALID_TRACE_RESTRICT_SLOT_GROUP);
				}

				this->slot_drag = {};
				this->slot_over = {};
				this->SetDirty();
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix slot
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s >= this->slots.size()) return; // click out of list bound

				const GUISlotListItem &item = this->slots[id_s];
				if (item.item.type != SlotItemType::Group) return; // Not a slot group

				if (current_parent != item.item.id && item.item != this->slot_drag) {
					set_parent(item.item.id);
				}

				this->slot_drag = {};
				this->slot_over = {};
				this->SetDirty();
				break;
			}
		}
	}

	virtual void OnDragDrop(Point pt, WidgetID widget) override
	{
		if (this->vehicle_sel != INVALID_VEHICLE) OnDragDrop_Vehicle(pt, widget);
		if (!this->slot_drag.IsNone()) OnDragDrop_Slot(pt, widget);

		_cursor.vehchain = false;
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str) override final
	{
		OnQueryTextFinished(str, {});
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str, std::optional<std::string> str2) override
	{
		if (str.has_value()) {
			switch (this->qsm_mode) {
				case QuerySelectorMode::None:
					break;

				case QuerySelectorMode::Rename:
					if (this->slot_query.type == SlotItemType::Slot) {
						if (this->slot_query.id == NEW_TRACE_RESTRICT_SLOT_ID) {
							TraceRestrictCreateSlotCmdData data;
							data.vehtype = this->vli.vtype;
							data.parent = this->slot_sel.GetClosestGroupID();
							data.name = std::move(*str);
							data.max_occupancy = (str2.has_value() && !str2->empty()) ? atoi(str2->c_str()) : TRACE_RESTRICT_SLOT_DEFAULT_MAX_OCCUPANCY;
							DoCommandP<CMD_CREATE_TRACERESTRICT_SLOT>(data, STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_CREATE, CommandCallback::CreateTraceRestrictSlot);
						} else {
							Command<CMD_ALTER_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_RENAME, this->slot_query.id, TRASO_RENAME, {}, std::move(*str));
						}
					} else if (this->slot_query.type == SlotItemType::Group) {
						if (this->slot_query.id == NEW_TRACE_RESTRICT_SLOT_GROUP) {
							Command<CMD_CREATE_TRACERESTRICT_SLOT_GROUP>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_CREATE, this->vli.vtype, this->slot_sel.GetClosestGroupID(), std::move(*str));
						} else {
							Command<CMD_ALTER_TRACERESTRICT_SLOT_GROUP>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_RENAME, this->slot_query.id, TRASGO_RENAME, {}, std::move(*str));
						}
					}
					break;

				case QuerySelectorMode::SetMaxOccupancy:
					if (this->slot_query.type == SlotItemType::Slot && !str->empty()) {
						Command<CMD_ALTER_TRACERESTRICT_SLOT>::Post(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_SET_MAX_OCCUPANCY, this->slot_query.id, TRASO_CHANGE_MAX_OCCUPANCY, atoi(str->c_str()), {});
					}
					break;
			}
		}
		this->slot_query = {};
	}

	virtual void OnResize() override
	{
		this->slot_sb->SetCapacityFromWidget(this, WID_TRSL_LIST_SLOTS);
		this->vscroll->SetCapacityFromWidget(this, WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_DROPDOWN:
				this->vehgroups.SetSortType(index);
				this->UpdateSortingInterval();
				break;

			case WID_TRSL_FILTER_BY_CARGO: // Select a cargo filter criteria
				this->SetCargoFilter(index);
				break;

			default: NOT_REACHED();
		}

		this->SetDirty();
	}

	virtual void OnGameTick() override
	{
		if (this->slots.NeedResort() || this->vehgroups.NeedResort()) {
			this->SetDirty();
		}
	}

	virtual void OnPlaceObjectAbort() override
	{
		/* Abort drag & drop */
		this->vehicle_sel = INVALID_VEHICLE;
		this->slot_drag = {};
		this->DirtyHighlightedSlotWidget();
		this->slot_over = {};
		this->SetWidgetDirty(WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnMouseDrag(Point pt, WidgetID widget) override
	{
		if (this->vehicle_sel == INVALID_VEHICLE && this->slot_drag.IsNone()) return;

		/* A vehicle is dragged over... */
		SlotItem new_slot_over = {};
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // ... all trains.
				new_slot_over = { SlotItemType::Special, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID };
				break;

			case WID_TRSL_LIST_SLOTS: { // ... the list of slots.
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s < this->slots.size()) {
					new_slot_over = this->slots[id_s].item;
				}
				break;
			}
		}

		/* Do not highlight when dragging over the current slot/group */
		if (this->slot_sel == new_slot_over) new_slot_over = {};

		if (this->vehicle_sel != INVALID_VEHICLE) {
			/* Do not highlight dragging vehicles over groups */
			if (new_slot_over.type == SlotItemType::Group) new_slot_over = {};
		}
		if (!this->slot_drag.IsNone()) {
			/* Do not highlight dragging slots/groups over slots */
			if (new_slot_over.type == SlotItemType::Slot) new_slot_over = {};

			/* Do not highlight dragging slot/group over its current parent */
			if (new_slot_over.type == SlotItemType::Group && this->slot_drag.GetInfo().parent == new_slot_over.id) new_slot_over = {};
		}

		/* Mark widgets as dirty if the group changed. */
		if (new_slot_over != this->slot_over) {
			this->DirtyHighlightedSlotWidget();
			this->slot_over = new_slot_over;
			this->DirtyHighlightedSlotWidget();
		}
	}

	void ShowRenameSlotWindow()
	{
		if (this->slot_sel.type != SlotItemType::Slot && this->slot_sel.type != SlotItemType::Group) return;
		this->qsm_mode = QuerySelectorMode::Rename;
		this->slot_query = this->slot_sel;
		if (this->slot_sel.type == SlotItemType::Slot) {
			ShowQueryString(GetString(STR_TRACE_RESTRICT_SLOT_NAME, this->slot_sel.id), STR_TRACE_RESTRICT_SLOT_RENAME_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
		} else if (this->slot_sel.type == SlotItemType::Group) {
			ShowQueryString(GetString(STR_TRACE_RESTRICT_SLOT_GROUP_NAME, this->slot_sel.id), STR_TRACE_RESTRICT_SLOT_GROUP_RENAME_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
		}
	}

	void ShowSetSlotMaxOccupancyWindow()
	{
		if (this->slot_sel.type != SlotItemType::Slot) return;
		this->qsm_mode = QuerySelectorMode::SetMaxOccupancy;
		this->slot_query = this->slot_sel;
		ShowQueryString(GetString(STR_JUST_INT, TraceRestrictSlot::Get(this->slot_sel.id)->max_occupancy), STR_TRACE_RESTRICT_SLOT_SET_MAX_OCCUPANCY_CAPTION, 5, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
	}

	void ShowCreateSlotWindow()
	{
		this->qsm_mode = QuerySelectorMode::Rename;
		this->slot_query = { SlotItemType::Slot, NEW_TRACE_RESTRICT_SLOT_ID };
		ShowSlotCreationQueryString(*this);
	}

	void ShowCreateSlotGroupWindow()
	{
		this->qsm_mode = QuerySelectorMode::Rename;
		this->slot_query = { SlotItemType::Group, NEW_TRACE_RESTRICT_SLOT_GROUP };
		ShowQueryString({}, STR_TRACE_RESTRICT_SLOT_GROUP_CREATE_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
	}

	/**
	 * Tests whether a given vehicle is selected in the window, and unselects it if necessary.
	 * Called when the vehicle is deleted.
	 * @param vehicle Vehicle that is going to be deleted
	 */
	void UnselectVehicle(VehicleID vehicle)
	{
		if (this->vehicle_sel == vehicle) ResetObjectToPlace();
	}
};

void CcCreateTraceRestrictSlot(const CommandCost &result)
{
	if (result.Succeeded() && result.HasResultData()) {
		TraceRestrictRecordRecentSlot(static_cast<TraceRestrictSlotID>(result.GetResultData()));
	}
}

static WindowDesc _slot_window_desc(__FILE__, __LINE__,
	WDP_AUTO, "list_tr_slots", 525, 246,
	WC_TRACE_RESTRICT_SLOTS, WC_NONE,
	{},
	_nested_slot_widgets
);

/**
 * Show the trace restrict slot window for the given company.
 * @param company The company to show the window for.
 */
void ShowTraceRestrictSlotWindow(CompanyID company, VehicleType vehtype)
{
	if (!Company::IsValidID(company)) return;

	VehicleListIdentifier vli(VL_SLOT_LIST, vehtype, company);
	AllocateWindowDescFront<TraceRestrictSlotWindow>(_slot_window_desc, vli.Pack(), vli);
}

/**
 * Finds a group list window determined by vehicle type and owner
 * @param vt vehicle type
 * @param owner owner of groups
 * @return pointer to VehicleGroupWindow, nullptr if not found
 */
static inline TraceRestrictSlotWindow *FindTraceRestrictSlotWindow(Owner owner)
{
	return (TraceRestrictSlotWindow *)FindWindowById(GetWindowClassForVehicleType(VEH_TRAIN), VehicleListIdentifier(VL_SLOT_LIST, VEH_TRAIN, owner).ToWindowNumber());
}

/**
 * Removes the highlight of a vehicle in a group window
 * @param *v Vehicle to remove all highlights from
 */
void DeleteTraceRestrictSlotHighlightOfVehicle(const Vehicle *v)
{
	/* If we haven't got any vehicles on the mouse pointer, we haven't got any highlighted in any group windows either
	 * If that is the case, we can skip looping though the windows and save time
	 */
	if (_special_mouse_mode != WSM_DRAGDROP) return;

	TraceRestrictSlotWindow *w = FindTraceRestrictSlotWindow(v->owner);
	if (w != nullptr) w->UnselectVehicle(v->index);
}

/** Counter GUI widget IDs */
enum TraceRestrictCounterWindowWidgets : WidgetID {
	WID_TRCL_CAPTION,
	WID_TRCL_LIST_COUNTERS,
	WID_TRCL_LIST_COUNTERS_SCROLLBAR,
	WID_TRCL_CREATE_COUNTER,
	WID_TRCL_DELETE_COUNTER,
	WID_TRCL_RENAME_COUNTER,
	WID_TRCL_COUNTER_PUBLIC,
	WID_TRCL_SET_COUNTER_VALUE,
};


static constexpr NWidgetPart _nested_counter_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_TRCL_CAPTION), SetStringTip(STR_TRACE_RESTRICT_COUNTER_CAPTION, STR_NULL),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_VERTICAL),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, WID_TRCL_LIST_COUNTERS), SetMatrixDataTip(1, 0, STR_TRACE_RESTRICT_COUNTER_GUI_LIST_TOOLTIP),
					SetFill(1, 1), SetResize(1, 1), SetScrollbar(WID_TRCL_LIST_COUNTERS_SCROLLBAR),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_TRCL_LIST_COUNTERS_SCROLLBAR),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_CREATE_COUNTER), SetMinimalSize(75, 12), SetFill(1, 0),
					SetStringTip(STR_TRACE_RESTRICT_COUNTER_CREATE, STR_TRACE_RESTRICT_COUNTER_CREATE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_DELETE_COUNTER), SetMinimalSize(75, 12), SetFill(1, 0),
					SetStringTip(STR_TRACE_RESTRICT_COUNTER_DELETE, STR_TRACE_RESTRICT_COUNTER_DELETE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_RENAME_COUNTER), SetMinimalSize(75, 12), SetFill(1, 0),
					SetStringTip(STR_TRACE_RESTRICT_COUNTER_RENAME, STR_TRACE_RESTRICT_COUNTER_RENAME_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TRCL_COUNTER_PUBLIC), SetMinimalSize(75, 12), SetFill(1, 0),
					SetStringTip(STR_TRACE_RESTRICT_COUNTER_PUBLIC, STR_TRACE_RESTRICT_COUNTER_PUBLIC_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_SET_COUNTER_VALUE), SetMinimalSize(75, 12), SetFill(1, 0),
					SetStringTip(STR_TRACE_RESTRICT_COUNTER_SET_VALUE, STR_TRACE_RESTRICT_COUNTER_SET_VALUE_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

class TraceRestrictCounterWindow : public Window {
private:
	enum QueryTextOperation : uint8_t {
		QTO_RENAME,
		QTO_SET_VALUE,
	};

	Owner ctr_company;                  ///< Company
	QueryTextOperation qto;             ///< Active query text operation
	TraceRestrictCounterID ctr_qt_op;   ///< Counter being adjusted in query text operation, INVALID_TRACE_RESTRICT_COUNTER_ID if none
	TraceRestrictCounterID ctr_confirm; ///< Counter awaiting delete confirmation
	TraceRestrictCounterID selected;    ///< Selected counter
	GUIList<const TraceRestrictCounter*> ctrs; ///< List of slots
	uint tiny_step_height;              ///< Step height for the counter list
	uint value_col_width;               ///< Value column width
	uint public_col_width;              ///< Public column width
	Scrollbar *sb;

	void BuildCounterList()
	{
		if (!this->ctrs.NeedRebuild()) return;

		this->ctrs.clear();

		for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
			if (ctr->owner == this->ctr_company) {
				this->ctrs.push_back(ctr);
			}
		}

		this->ctrs.ForceResort();
		this->ctrs.Sort(&CounterNameSorter);
		this->ctrs.shrink_to_fit();
		this->ctrs.RebuildDone();
	}

	/**
	 * Compute dimensions
	 * @return Total width required for the list.
	 */
	uint ComputeInfoSize()
	{
		SetDParamMaxValue(0, 9999, 3);
		Dimension dim = GetStringBoundingBox(STR_JUST_COMMA);
		this->tiny_step_height = dim.height;
		this->value_col_width = dim.width;

		Dimension public_dim = GetScaledSpriteSize(SPR_BLOT);
		this->tiny_step_height = std::max(this->tiny_step_height, public_dim.height);
		this->public_col_width = public_dim.width;

		this->tiny_step_height += WidgetDimensions::scaled.matrix.Vertical();

		return WidgetDimensions::scaled.framerect.Horizontal() + WidgetDimensions::scaled.vsep_wide +
				170 + WidgetDimensions::scaled.vsep_wide +
				dim.width + WidgetDimensions::scaled.vsep_wide +
				public_dim.width + WidgetDimensions::scaled.vsep_wide +
				WidgetDimensions::scaled.framerect.right;
	}

	/**
	 * Draw a row in the counter list.
	 * @param draw_area Area to draw in.
	 * @param ctr_id Counter ID.
	 */
	void DrawCounterInfo(Rect draw_area, TraceRestrictCounterID ctr_id) const
	{
		const TraceRestrictCounter *ctr = TraceRestrictCounter::Get(ctr_id);
		Rect info_area = draw_area.Shrink(WidgetDimensions::scaled.hsep_indent, 0);
		bool rtl = _current_text_dir == TD_RTL;

		/* draw the selected counter in white, else we draw it in black */
		TextColour colour = ctr_id == this->selected ? TC_WHITE : TC_BLACK;

		Rect r = info_area.Indent(this->value_col_width + WidgetDimensions::scaled.vsep_wide + this->public_col_width, !rtl);
		SetDParam(0, ctr_id);
		DrawString(r.left, r.right, r.top + (this->tiny_step_height - GetCharacterHeight(FS_NORMAL)) / 2, STR_TRACE_RESTRICT_COUNTER_NAME, colour);

		if (HasFlag(ctr->flags, TraceRestrictCounter::Flags::Public)) {
			r = info_area.Indent(this->value_col_width + WidgetDimensions::scaled.vsep_wide, !rtl).WithWidth(this->public_col_width, !rtl);
			DrawSpriteIgnorePadding(SPR_BLOT, PALETTE_TO_BLUE, r, SA_CENTER);
		}

		r = info_area.WithWidth(this->value_col_width, !rtl);
		SetDParam(0, ctr->value);
		DrawString(r.left, r.right, r.top + (this->tiny_step_height - GetCharacterHeight(FS_NORMAL)) / 2, STR_JUST_COMMA, colour, SA_RIGHT | SA_FORCE);
	}

public:
	TraceRestrictCounterWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->ctr_company = (Owner)window_number;
		this->CreateNestedTree();

		this->sb = this->GetScrollbar(WID_TRCL_LIST_COUNTERS_SCROLLBAR);

		this->ctr_qt_op = INVALID_TRACE_RESTRICT_COUNTER_ID;
		this->ctr_confirm = INVALID_TRACE_RESTRICT_COUNTER_ID;
		this->selected = INVALID_TRACE_RESTRICT_COUNTER_ID;

		this->ctrs.ForceRebuild();
		this->ctrs.NeedResort();
		this->BuildCounterList();

		this->FinishInitNested(window_number);
		this->owner = this->ctr_company;
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_TRCL_LIST_COUNTERS: {
				size.width = std::max<uint>(size.width, this->ComputeInfoSize());
				resize.height = this->tiny_step_height;
				size.height = std::max<uint>(size.height, 8 * resize.height);
				break;
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->ctrs.ForceRebuild();
		} else {
			this->ctrs.ForceResort();
		}

		if (this->ctr_qt_op != INVALID_TRACE_RESTRICT_COUNTER_ID && this->ctr_qt_op != NEW_TRACE_RESTRICT_COUNTER_ID &&
				!TraceRestrictCounter::IsValidID(this->ctr_qt_op)) {
			CloseWindowByClass(WC_QUERY_STRING);
			this->ctr_qt_op = INVALID_TRACE_RESTRICT_COUNTER_ID;
		}

		if (this->selected != INVALID_TRACE_RESTRICT_COUNTER_ID && !TraceRestrictCounter::IsValidID(this->selected)) {
			this->selected = INVALID_TRACE_RESTRICT_COUNTER_ID;
		}

		this->SetDirty();
	}

	virtual void OnPaint() override
	{
		this->BuildCounterList();

		this->sb->SetCount(static_cast<uint>(this->ctrs.size()));

		/* Disable the counter specific functions when no counter is selected */
		this->SetWidgetsDisabledState(this->selected == INVALID_TRACE_RESTRICT_COUNTER_ID || _local_company != this->ctr_company,
				WID_TRCL_DELETE_COUNTER,
				WID_TRCL_RENAME_COUNTER,
				WID_TRCL_COUNTER_PUBLIC,
				WID_TRCL_SET_COUNTER_VALUE);

		this->SetWidgetLoweredState(WID_TRCL_COUNTER_PUBLIC, this->selected != INVALID_TRACE_RESTRICT_COUNTER_ID && HasFlag(TraceRestrictCounter::Get(this->selected)->flags, TraceRestrictCounter::Flags::Public));

		/* Disable remaining buttons for non-local companies
		 * Needed while changing _local_company, eg. by cheats
		 * All procedures (eg. move vehicle to a slot)
		 *  verify, whether you are the owner of the vehicle,
		 *  so it doesn't have to be disabled
		 */
		this->SetWidgetsDisabledState(_local_company != this->ctr_company,
				WID_TRCL_CREATE_COUNTER);

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_TRCL_LIST_COUNTERS: {
				Rect ir = r.WithHeight(this->tiny_step_height).Shrink(WidgetDimensions::scaled.framerect);
				int max = std::min<int>(this->sb->GetPosition() + this->sb->GetCapacity(), static_cast<int>(this->ctrs.size()));
				for (int i = this->sb->GetPosition(); i < max; ++i) {
					const TraceRestrictCounter *ctr = this->ctrs[i];

					assert(ctr->owner == this->ctr_company);

					DrawCounterInfo(ir, ctr->index);

					ir.top += this->tiny_step_height;
					ir.bottom += this->tiny_step_height;
				}
				break;
			}
		}
	}

	static void DeleteCounterCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			TraceRestrictCounterWindow *w = (TraceRestrictCounterWindow*)win;
			w->selected = INVALID_TRACE_RESTRICT_COUNTER_ID;
			Command<CMD_DELETE_TRACERESTRICT_COUNTER>::Post(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_DELETE, w->ctr_confirm);
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_TRCL_LIST_COUNTERS: { // Matrix
				uint id_s = this->sb->GetScrolledRowFromWidget(pt.y, this, WID_TRCL_LIST_COUNTERS, 0);
				if (id_s >= this->ctrs.size()) return;

				this->selected = this->ctrs[id_s]->index;

				this->SetDirty();
				break;
			}

			case WID_TRCL_CREATE_COUNTER: { // Create a new counter
				this->ShowCreateCounterWindow();
				break;
			}

			case WID_TRCL_DELETE_COUNTER: { // Delete the selected counter
				this->ctr_confirm = this->selected;
				ShowQuery(STR_TRACE_RESTRICT_COUNTER_QUERY_DELETE_CAPTION, STR_TRACE_RESTRICT_COUNTER_DELETE_QUERY_TEXT, this, DeleteCounterCallback);
				break;
			}

			case WID_TRCL_RENAME_COUNTER: // Rename the selected counter
				this->ShowRenameCounterWindow(this->selected);
				break;

			case WID_TRCL_COUNTER_PUBLIC: { // Toggle public state of the selected counter
				const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(this->selected);
				if (ctr != nullptr) {
					Command<CMD_ALTER_TRACERESTRICT_COUNTER>::Post(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_MODIFY, this->selected, TRACO_SET_PUBLIC, HasFlag(ctr->flags, TraceRestrictCounter::Flags::Public) ? 0 : 1, {});
				}
				break;
			}

			case WID_TRCL_SET_COUNTER_VALUE:
				this->ShowSetCounterValueWindow(this->selected);
				break;
		}
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (str.has_value()) {
			switch (this->qto) {
				case QTO_RENAME:
					if (this->ctr_qt_op == NEW_TRACE_RESTRICT_COUNTER_ID) {
						TraceRestrictCreateCounterCmdData data;
						data.name = std::move(*str);
						DoCommandP<CMD_CREATE_TRACERESTRICT_COUNTER>(data, STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_CREATE, CommandCallback::CreateTraceRestrictCounter);
					} else {
						Command<CMD_ALTER_TRACERESTRICT_COUNTER>::Post(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_RENAME, this->ctr_qt_op, TRACO_RENAME, {}, std::move(*str));
					}
					break;

				case QTO_SET_VALUE:
					if (!str->empty()) {
						Command<CMD_ALTER_TRACERESTRICT_COUNTER>::Post(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_MODIFY, this->ctr_qt_op, TRACO_CHANGE_VALUE, atoi(str->c_str()), {});
					}
					break;
			}
		}
		this->ctr_qt_op = INVALID_TRACE_RESTRICT_COUNTER_ID;
	}

	virtual void OnResize() override
	{
		this->sb->SetCapacityFromWidget(this, WID_TRCL_LIST_COUNTERS);
	}

	virtual void OnGameTick() override
	{
		if (this->ctrs.NeedResort()) {
			this->SetDirty();
		}
	}

	void ShowRenameCounterWindow(TraceRestrictCounterID ctr_id)
	{
		assert(TraceRestrictCounter::IsValidID(ctr_id));
		this->qto = QTO_RENAME;
		this->ctr_qt_op = ctr_id;
		ShowQueryString(GetString(STR_TRACE_RESTRICT_COUNTER_NAME, ctr_id), STR_TRACE_RESTRICT_COUNTER_RENAME_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
	}

	void ShowSetCounterValueWindow(TraceRestrictCounterID ctr_id)
	{
		assert(TraceRestrictCounter::IsValidID(ctr_id));
		this->qto = QTO_SET_VALUE;
		this->ctr_qt_op = ctr_id;
		ShowQueryString(GetString(STR_JUST_INT, TraceRestrictCounter::Get(ctr_id)->value), STR_TRACE_RESTRICT_COUNTER_SET_VALUE_CAPTION, 5, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
	}

	void ShowCreateCounterWindow()
	{
		this->qto = QTO_RENAME;
		this->ctr_qt_op = NEW_TRACE_RESTRICT_COUNTER_ID;
		ShowQueryString({}, STR_TRACE_RESTRICT_COUNTER_CREATE_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_LEN_IN_CHARS);
	}
};

void CcCreateTraceRestrictCounter(const CommandCost &result)
{
	if (result.Succeeded() && result.HasResultData()) {
		TraceRestrictRecordRecentCounter(static_cast<TraceRestrictCounterID>(result.GetResultData()));
	}
}

static WindowDesc _counter_window_desc(__FILE__, __LINE__,
	WDP_AUTO, "list_tr_counters", 525, 246,
	WC_TRACE_RESTRICT_COUNTERS, WC_NONE,
	{},
	_nested_counter_widgets
);

/**
 * Show the trace restrict counter window for the given company.
 * @param company The company to show the window for.
 */
void ShowTraceRestrictCounterWindow(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<TraceRestrictCounterWindow>(_counter_window_desc, (WindowNumber)company);
}


/**
 * Show the slot creation query window.
 * @param parent Window to call OnQueryTextFinished on
 */
void ShowSlotCreationQueryString(Window &parent)
{
	std::string occupancy = GetString(STR_JUST_INT, TRACE_RESTRICT_SLOT_DEFAULT_MAX_OCCUPANCY);
	std::array<QueryEditboxDescription, 2> ed{{
		{{}, STR_TRACE_RESTRICT_SLOT_CREATE_SLOT_NAME, STR_TRACE_RESTRICT_SLOT_CREATE_SLOT_NAME, CS_ALPHANUMERAL, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS},
		{occupancy, STR_TRACE_RESTRICT_SLOT_SET_MAX_OCCUPANCY_CAPTION, STR_TRACE_RESTRICT_SLOT_CREATE_SLOT_MAX_OCCUPANCY, CS_NUMERAL, 5},
	}};
	ShowQueryString(std::span(ed), STR_TRACE_RESTRICT_SLOT_CREATE_CAPTION, &parent, QSF_LEN_IN_CHARS);
}
