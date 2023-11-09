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
#include "command_func.h"
#include "window_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "viewport_func.h"
#include "textbuf_gui.h"
#include "company_base.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "widgets/dropdown_func.h"
#include "widgets/dropdown_type.h"
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
#include "group_gui_list.h"
#include "core/span_type.hpp"
#include "3rdparty/cpp-btree/btree_map.h"

#include "safeguards.h"

/** Widget IDs */
enum TraceRestrictWindowWidgets {
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
enum PanelWidgets {
	// Left 2
	DPL2_TYPE = 0,
	DPL2_CONDFLAGS,
	DPL2_BLANK,

	// Left
	DPL_TYPE = 0,
	DPL_COUNTER_OP,
	DPL_BLANK,

	// Left aux
	DPLA_DROPDOWN = 0,

	// Middle
	DPM_COMPARATOR = 0,
	DPM_SLOT_OP,
	DPM_BLANK,

	// Right
	DPR_VALUE_INT = 0,
	DPR_VALUE_DECIMAL,
	DPR_VALUE_DROPDOWN,
	DPR_VALUE_DEST,
	DPR_VALUE_SIGNAL,
	DPR_VALUE_TILE,
	DPR_BLANK,

	// Share
	DPS_SHARE = 0,
	DPS_UNSHARE,
	DPS_SHARE_ONTO,

	// Copy
	DPC_COPY = 0,
	DPC_APPEND,
	DPC_DUPLICATE,
};

/**
 * drop down list string array, and corresponding integer values
 *
 * value_array *must* be at least as long as string_array,
 * where the length of string_array is defined as the offset
 * of the first INVALID_STRING_ID
 */
struct TraceRestrictDropDownListSet {
	const StringID *string_array;
	const uint *value_array;
};

static const StringID _program_insert_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_IF,
	STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ELSE,
	STR_TRACE_RESTRICT_PF_DENY,
	STR_TRACE_RESTRICT_PF_PENALTY,
	STR_TRACE_RESTRICT_RESERVE_THROUGH,
	STR_TRACE_RESTRICT_LONG_RESERVE,
	STR_TRACE_RESTRICT_WAIT_AT_PBS,
	STR_TRACE_RESTRICT_SLOT_OP,
	STR_TRACE_RESTRICT_REVERSE,
	STR_TRACE_RESTRICT_SPEED_RESTRICTION,
	STR_TRACE_RESTRICT_NEWS_CONTROL,
	STR_TRACE_RESTRICT_COUNTER_OP,
	STR_TRACE_RESTRICT_PF_PENALTY_CONTROL,
	STR_TRACE_RESTRICT_SPEED_ADAPTATION_CONTROL,
	STR_TRACE_RESTRICT_SIGNAL_MODE_CONTROL,
	INVALID_STRING_ID
};
static const uint32 _program_insert_else_hide_mask    = 8;     ///< disable bitmask for else
static const uint32 _program_insert_or_if_hide_mask   = 4;     ///< disable bitmask for orif
static const uint32 _program_insert_else_if_hide_mask = 2;     ///< disable bitmask for elif
static const uint32 _program_wait_pbs_hide_mask = 0x100;       ///< disable bitmask for wait at PBS
static const uint32 _program_slot_hide_mask = 0x200;           ///< disable bitmask for slot
static const uint32 _program_reverse_hide_mask = 0x400;        ///< disable bitmask for reverse
static const uint32 _program_speed_res_hide_mask = 0x800;      ///< disable bitmask for speed restriction
static const uint32 _program_counter_hide_mask = 0x2000;       ///< disable bitmask for counter
static const uint32 _program_penalty_adj_hide_mask = 0x4000;   ///< disable bitmask for penalty adjust
static const uint32 _program_speed_adapt_hide_mask = 0x8000;   ///< disable bitmask for speed adaptation
static const uint32 _program_signal_mode_hide_mask = 0x10000;  ///< disable bitmask for signal mode control
static const uint _program_insert_val[] = {
	TRIT_COND_UNDEFINED,                               // if block
	TRIT_COND_UNDEFINED | (TRCF_ELSE << 16),           // elif block
	TRIT_COND_UNDEFINED | (TRCF_OR << 16),             // orif block
	TRIT_COND_ENDIF | (TRCF_ELSE << 16),               // else block
	TRIT_PF_DENY,                                      // deny
	TRIT_PF_PENALTY,                                   // penalty
	TRIT_RESERVE_THROUGH,                              // reserve through
	TRIT_LONG_RESERVE,                                 // long reserve
	TRIT_WAIT_AT_PBS,                                  // wait at PBS signal
	TRIT_SLOT,                                         // slot operation
	TRIT_REVERSE,                                      // reverse
	TRIT_SPEED_RESTRICTION,                            // speed restriction
	TRIT_NEWS_CONTROL,                                 // news control
	TRIT_COUNTER,                                      // counter operation
	TRIT_PF_PENALTY_CONTROL,                           // penalty control
	TRIT_SPEED_ADAPTATION_CONTROL,                     // speed adaptation control
	TRIT_SIGNAL_MODE_CONTROL,                          // signal mode control
};

/** insert drop down list strings and values */
static const TraceRestrictDropDownListSet _program_insert = {
	_program_insert_str, _program_insert_val,
};

static const StringID _deny_value_str[] = {
	STR_TRACE_RESTRICT_PF_DENY,
	STR_TRACE_RESTRICT_PF_ALLOW,
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
};
static const uint _reverse_value_val[] = {
	TRRVF_REVERSE,
	TRRVF_CANCEL_REVERSE,
};

/** value drop down list for reverse types strings and values */
static const TraceRestrictDropDownListSet _reverse_value = {
	_reverse_value_str, _reverse_value_val,
};

static const StringID _news_control_value_str[] = {
	STR_TRACE_RESTRICT_TRAIN_NOT_STUCK_SHORT,
	STR_TRACE_RESTRICT_TRAIN_NOT_STUCK_CANCEL_SHORT,
	INVALID_STRING_ID
};
static const uint _news_control_value_val[] = {
	TRRVF_REVERSE,
	TRRVF_CANCEL_REVERSE,
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	INVALID_STRING_ID
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
	const StringID *string_array = list_set->string_array;
	const uint *value_array = list_set->value_array;

	for (; *string_array != INVALID_STRING_ID; string_array++, value_array++) {
		if (*value_array == value) {
			return value_array - list_set->value_array;
		}
	}
	assert(missing_ok == true);
	return -1;
}

/**
 * Get StringID correspoding to @p value, in @list_set
 * @p value must be present
 */
static StringID GetDropDownStringByValue(const TraceRestrictDropDownListSet *list_set, uint value)
{
	return list_set->string_array[GetDropDownListIndexByValue(list_set, value, false)];
}

typedef uint TraceRestrictGuiItemType;

static TraceRestrictGuiItemType GetItemGuiType(TraceRestrictItem item)
{
	TraceRestrictItemType type = GetTraceRestrictType(item);
	if (IsTraceRestrictTypeAuxSubtype(type)) {
		return type | (GetTraceRestrictAuxField(item) << 16);
	} else {
		return type;
	}
}

static TraceRestrictItemType ItemTypeFromGuiType(TraceRestrictGuiItemType type)
{
	return static_cast<TraceRestrictItemType>(type & 0xFFFF);
}

enum TraceRestrictDropDownListItemFlags : uint8 {
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

/**
 * Return the appropriate type dropdown TraceRestrictDropDownListItem span for the given item type @p type
 */
static span<const TraceRestrictDropDownListItem> GetTypeDropDownListItems(TraceRestrictGuiItemType type)
{
	static const TraceRestrictDropDownListItem actions[] = {
		{ TRIT_PF_DENY,                  STR_TRACE_RESTRICT_PF_DENY,                  TRDDLIF_NONE },
		{ TRIT_PF_PENALTY,               STR_TRACE_RESTRICT_PF_PENALTY,               TRDDLIF_NONE },
		{ TRIT_RESERVE_THROUGH,          STR_TRACE_RESTRICT_RESERVE_THROUGH,          TRDDLIF_NONE },
		{ TRIT_LONG_RESERVE,             STR_TRACE_RESTRICT_LONG_RESERVE,             TRDDLIF_NONE },
		{ TRIT_NEWS_CONTROL,             STR_TRACE_RESTRICT_NEWS_CONTROL,             TRDDLIF_NONE },
		{ TRIT_WAIT_AT_PBS,              STR_TRACE_RESTRICT_WAIT_AT_PBS,              TRDDLIF_ADVANCED },
		{ TRIT_SLOT,                     STR_TRACE_RESTRICT_SLOT_OP,                  TRDDLIF_ADVANCED },
		{ TRIT_REVERSE,                  STR_TRACE_RESTRICT_REVERSE,                  TRDDLIF_ADVANCED },
		{ TRIT_SPEED_RESTRICTION,        STR_TRACE_RESTRICT_SPEED_RESTRICTION,        TRDDLIF_ADVANCED },
		{ TRIT_COUNTER,                  STR_TRACE_RESTRICT_COUNTER_OP,               TRDDLIF_ADVANCED },
		{ TRIT_PF_PENALTY_CONTROL,       STR_TRACE_RESTRICT_PF_PENALTY_CONTROL,       TRDDLIF_ADVANCED },
		{ TRIT_SPEED_ADAPTATION_CONTROL, STR_TRACE_RESTRICT_SPEED_ADAPTATION_CONTROL, TRDDLIF_ADVANCED | TRDDLIF_SPEED_ADAPTATION },
		{ TRIT_SIGNAL_MODE_CONTROL,      STR_TRACE_RESTRICT_SIGNAL_MODE_CONTROL,      TRDDLIF_ADVANCED | TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE },
	};

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
		{ TRIT_COND_TRAIN_IN_SLOT,                                    STR_TRACE_RESTRICT_VARIABLE_TRAIN_SLOT,                TRDDLIF_ADVANCED },
		{ TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_OCCUPANTS << 16),       STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY,            TRDDLIF_ADVANCED },
		{ TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_REMAINING << 16),       STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_REMAINING,  TRDDLIF_ADVANCED },
		{ TRIT_COND_COUNTER_VALUE,                                    STR_TRACE_RESTRICT_VARIABLE_COUNTER_VALUE,             TRDDLIF_ADVANCED },
		{ TRIT_COND_TIME_DATE_VALUE,                                  STR_TRACE_RESTRICT_VARIABLE_TIME_DATE_VALUE,           TRDDLIF_ADVANCED },
		{ TRIT_COND_RESERVED_TILES,                                   STR_TRACE_RESTRICT_VARIABLE_RESERVED_TILES_AHEAD,      TRDDLIF_ADVANCED | TRDDLIF_REALISTIC_BRAKING },
		{ TRIT_COND_RESERVATION_THROUGH,                              STR_TRACE_RESTRICT_VARIABLE_RESERVATION_THROUGH,       TRDDLIF_ADVANCED },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_VEH_POS << 16),       STR_TRACE_RESTRICT_VARIABLE_PBS_ENTRY_SIGNAL,          TRDDLIF_ADVANCED },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_RES_END << 16),       STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_SIGNAL,        TRDDLIF_ADVANCED | TRDDLIF_REALISTIC_BRAKING },
		{ TRIT_COND_PBS_ENTRY_SIGNAL | (TRPESAF_RES_END_TILE << 16),  STR_TRACE_RESTRICT_VARIABLE_PBS_RES_END_TILE,          TRDDLIF_ADVANCED | TRDDLIF_NORMAL_SHUNT_SIGNAL_STYLE },
	};

	if (IsTraceRestrictTypeConditional(ItemTypeFromGuiType(type))) {
		return span<const TraceRestrictDropDownListItem>(conditions);
	} else {
		return span<const TraceRestrictDropDownListItem>(actions);
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
	static StringID cargo_list_str[NUM_CARGO + 1];
	static uint cargo_list_id[NUM_CARGO];
	static const TraceRestrictDropDownListSet cargo_list = {
		cargo_list_str, cargo_list_id,
	};

	for (size_t i = 0; i < _sorted_standard_cargo_specs.size(); ++i) {
		const CargoSpec *cs = _sorted_cargo_specs[i];
		cargo_list_str[i] = cs->name;
		cargo_list_id[i] = cs->Index();
	}
	cargo_list_str[_sorted_standard_cargo_specs.size()] = INVALID_STRING_ID;

	return &cargo_list;
}

/**
 * Get a DropDownList of the group list
 */
static DropDownList GetGroupDropDownList(Owner owner, GroupID group_id, int &selected)
{
	GUIGroupList list;

	for (const Group *g : Group::Iterate()) {
		if (g->owner == owner && g->vehicle_type == VEH_TRAIN) {
			list.push_back(g);
		}
	}

	list.ForceResort();
	SortGUIGroupList(list);

	DropDownList dlist;
	selected = -1;

	if (group_id == DEFAULT_GROUP) selected = DEFAULT_GROUP;
	dlist.emplace_back(new DropDownListStringItem(STR_GROUP_DEFAULT_TRAINS, DEFAULT_GROUP, false));

	for (size_t i = 0; i < list.size(); ++i) {
		const Group *g = list[i];
		if (group_id == g->index) selected = group_id;
		SetDParam(0, g->index | GROUP_NAME_HIERARCHY);
		dlist.emplace_back(new DropDownListStringItem(STR_GROUP_NAME, g->index, false));
	}

	return dlist;
}

/** Sort slots by their name */
static bool SlotNameSorter(const TraceRestrictSlot * const &a, const TraceRestrictSlot * const &b)
{
	int r = StrNaturalCompare(a->name, b->name); // Sort by name (natural sorting).
	if (r == 0) return a->index < b->index;
	return r < 0;
}

static VehicleType _slot_sort_veh_type;

/** Sort slots by their type then name */
static bool SlotVehTypeNameSorter(const TraceRestrictSlot * const &a, const TraceRestrictSlot * const &b)
{
	if (a->vehicle_type == b->vehicle_type) return SlotNameSorter(a, b);
	if (a->vehicle_type == _slot_sort_veh_type) return true;
	if (b->vehicle_type == _slot_sort_veh_type) return false;
	return a->vehicle_type < b->vehicle_type;
}

/**
 * Get a DropDownList of the group list
 */
DropDownList GetSlotDropDownList(Owner owner, TraceRestrictSlotID slot_id, int &selected, VehicleType vehtype, bool show_other_types)
{
	GUIList<const TraceRestrictSlot*> list;
	DropDownList dlist;

	for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
		if (!show_other_types && slot->vehicle_type != vehtype) continue;
		if (slot->owner == owner) {
			list.push_back(slot);
		}
	}

	if (list.size() == 0) return dlist;

	list.ForceResort();
	_slot_sort_veh_type = vehtype;
	list.Sort(show_other_types ? &SlotVehTypeNameSorter : &SlotNameSorter);

	selected = -1;

	for (size_t i = 0; i < list.size(); ++i) {
		const TraceRestrictSlot *s = list[i];
		if (slot_id == s->index) selected = slot_id;
		if (s->vehicle_type == vehtype) {
			SetDParam(0, s->index);
			dlist.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_SLOT_NAME, s->index, false));
		} else {
			SetDParam(0, STR_REPLACE_VEHICLE_TRAIN + s->vehicle_type);
			SetDParam(1, s->index);
			dlist.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_SLOT_NAME_PREFIXED, s->index, false));
		}
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

/**
 * Get a DropDownList of the counter list
 */
DropDownList GetCounterDropDownList(Owner owner, TraceRestrictCounterID ctr_id, int &selected)
{
	GUIList<const TraceRestrictCounter*> list;
	DropDownList dlist;

	for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
		if (ctr->owner == owner) {
			list.push_back(ctr);
		}
	}

	if (list.size() == 0) return dlist;

	list.ForceResort();
	list.Sort(&CounterNameSorter);

	selected = -1;

	for (size_t i = 0; i < list.size(); ++i) {
		const TraceRestrictCounter *s = list[i];
		if (ctr_id == s->index) selected = ctr_id;
		SetDParam(0, s->index);
		dlist.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_COUNTER_NAME, s->index, false));
	}

	return dlist;
}

static const StringID _cargo_cond_ops_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_CARGO_EQUALS,
	STR_TRACE_RESTRICT_CONDITIONAL_COMPARATOR_CARGO_NOT_EQUALS,
	INVALID_STRING_ID,
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
	INVALID_STRING_ID,
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
	INVALID_STRING_ID,
};
static const uint _passes_through_cond_ops_val[] = {
	TRCO_IS,
	TRCO_ISNOT,
};
/** passes through conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _passes_through_cond_ops = {
	_passes_through_cond_ops_str, _passes_through_cond_ops_val,
};

static const StringID _slot_op_cond_ops_str[] = {
	STR_TRACE_RESTRICT_SLOT_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_RELEASE_FRONT,
	STR_TRACE_RESTRICT_SLOT_RELEASE_BACK,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE,
	STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE_ON_RES,
	INVALID_STRING_ID,
};
static const uint _slot_op_cond_ops_val[] = {
	TRSCOF_ACQUIRE_WAIT,
	TRSCOF_ACQUIRE_TRY,
	TRSCOF_RELEASE_FRONT,
	TRSCOF_RELEASE_BACK,
	TRSCOF_PBS_RES_END_ACQ_WAIT,
	TRSCOF_PBS_RES_END_ACQ_TRY,
	TRSCOF_PBS_RES_END_RELEASE,
	TRSCOF_ACQUIRE_TRY_ON_RESERVE,
};
/** cargo conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _slot_op_cond_ops = {
	_slot_op_cond_ops_str, _slot_op_cond_ops_val,
};

static const StringID _counter_op_cond_ops_str[] = {
	STR_TRACE_RESTRICT_COUNTER_INCREASE,
	STR_TRACE_RESTRICT_COUNTER_DECREASE,
	STR_TRACE_RESTRICT_COUNTER_SET,
	INVALID_STRING_ID,
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
 * Get the StringID for a given CargoID @p cargo, or STR_NEWGRF_INVALID_CARGO
 */
static StringID GetCargoStringByID(CargoID cargo)
{
	const CargoSpec *cs = CargoSpec::Get(cargo);
	return cs->IsValid() ? cs->name : STR_NEWGRF_INVALID_CARGO;
}

/**
 * Get the StringID for a given item type @p type
 */
static StringID GetTypeString(TraceRestrictItem item)
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
		INVALID_STRING_ID,
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
		INVALID_STRING_ID,
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
	return nullptr;
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
					? ConvertForceToDisplayForce(static_cast<int64>(in) * 1000)
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
static void ConvertValueToDecimal(TraceRestrictValueType type, uint in, int64 &value, int64 &decimal)
{
	switch (type) {
		case TRVT_POWER_WEIGHT_RATIO:
			ConvertPowerWeightRatioToDisplay(in, value, decimal);
			break;

		case TRVT_FORCE_WEIGHT_RATIO:
			ConvertForceWeightRatioToDisplay(static_cast<int64>(in) * 1000, value, decimal);
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
enum CondFlagsDropDownType {
	CFDDT_ELSE = 0,           ///< This is an else block
	CFDDT_ELIF = TRCF_ELSE,   ///< This is an else-if block
	CFDDT_ORIF = TRCF_OR,     ///< This is an or-if block
};

static const uint32 _condflags_dropdown_else_hide_mask = 1;     ///< disable bitmask for CFDDT_ELSE
static const uint32 _condflags_dropdown_else_if_hide_mask = 6;  ///< disable bitmask for CFDDT_ELIF and CFDDT_ORIF

static const StringID _condflags_dropdown_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_ELSE,
	STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
	INVALID_STRING_ID,
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
	INVALID_STRING_ID,
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

static uint GetPathfinderPenaltyDropdownIndex(TraceRestrictItem item)
{
	switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(GetTraceRestrictAuxField(item))) {
		case TRPPAF_VALUE:
			return TRPPPI_END;

		case TRPPAF_PRESET: {
			uint16 index = GetTraceRestrictValue(item);
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
	size_t instruction_count = prog->GetInstructionCount();
	int depth = 1;
	for (size_t i = index; i < instruction_count; i++) {
		TraceRestrictItem item = prog->items[prog->InstructionOffsetToArrayOffset(i)];
		if (IsTraceRestrictConditional(item)) {
			if (GetTraceRestrictCondFlags(item) & (TRCF_ELSE | TRCF_OR)) {
				/* do nothing */
			} else if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
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
static void DrawInstructionStringConditionalCommon(TraceRestrictItem item, const TraceRestrictTypePropertySet &properties)
{
	assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
	SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
	SetDParam(1, GetTypeString(item));
	SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
}

/** Common function for drawing an integer conditional instruction */
static void DrawInstructionStringConditionalIntegerCommon(TraceRestrictItem item, const TraceRestrictTypePropertySet &properties)
{
	DrawInstructionStringConditionalCommon(item, properties);
	SetDParam(3, GetTraceRestrictValue(item));
}

/** Common function for drawing an integer conditional instruction with an invalid value */
static void DrawInstructionStringConditionalInvalidValue(TraceRestrictItem item, const TraceRestrictTypePropertySet &properties, StringID &instruction_string, bool selected)
{
	instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_UNDEFINED;
	DrawInstructionStringConditionalCommon(item, properties);
	SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
}

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
 */
static void DrawInstructionString(const TraceRestrictProgram *prog, TraceRestrictItem item, int index, int y, bool selected, int indent, int left, int right)
{
	StringID instruction_string = INVALID_STRING_ID;

	TraceRestrictTypePropertySet properties = GetTraceRestrictTypeProperties(item);

	if (IsTraceRestrictConditional(item)) {
		if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
			if (GetTraceRestrictCondFlags(item) & TRCF_ELSE) {
				instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ELSE;
			} else {
				instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENDIF;
			}
		} else if (GetTraceRestrictType(item) == TRIT_COND_UNDEFINED) {
			instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_UNDEFINED;
			SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
			SetDParam(1, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
		} else {
			auto insert_warning = [&](uint dparam_index, StringID warning) {
				char buf[256];
				auto tmp_params = MakeParameters(GetDParam(dparam_index));
				char *end = GetStringWithArgs(buf, warning, tmp_params, lastof(buf));
				_temp_special_strings[0].assign(buf, end);
				SetDParam(dparam_index, SPECSTR_TEMP_START);
			};

			switch (properties.value_type) {
				case TRVT_INT:
				case TRVT_PERCENT:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					if (GetTraceRestrictType(item) == TRIT_COND_RESERVED_TILES && _settings_game.vehicle.train_braking_model != TBM_REALISTIC) {
						insert_warning(1, STR_TRACE_RESTRICT_WARNING_REQUIRES_REALISTIC_BRAKING);
					}
					break;

				case TRVT_SPEED:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_SPEED;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_ORDER: {
					switch (static_cast<TraceRestrictOrderCondAuxField>(GetTraceRestrictAuxField(item))) {
						case TROCAF_STATION:
							if (GetTraceRestrictValue(item) != INVALID_STATION) {
								instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ORDER_STATION;
								DrawInstructionStringConditionalIntegerCommon(item, properties);
							} else {
								// this is an invalid station, use a seperate string
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
							SetDParam(4, GetTraceRestrictValue(item));
							break;

						default:
							NOT_REACHED();
							break;
					}
					break;
				}

				case TRVT_CARGO_ID:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_CARGO;
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(&_cargo_cond_ops, GetTraceRestrictCondOp(item)));
					SetDParam(2, GetCargoStringByID(GetTraceRestrictValue(item)));
					break;

				case TRVT_DIRECTION:
					if (GetTraceRestrictValue(item) >= TRDTSV_TUNBRIDGE_ENTER) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_SIGNAL_TYPE;
					} else if (GetTraceRestrictValue(item) >= TRDTSV_FRONT) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_SIGNAL_FACE;
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_DIRECTION;
					}
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					SetDParam(2, GetDropDownStringByValue(&_direction_value, GetTraceRestrictValue(item)));
					break;

				case TRVT_TILE_INDEX: {
					assert(prog != nullptr);
					assert(GetTraceRestrictType(item) == TRIT_COND_PBS_ENTRY_SIGNAL);
					TileIndex tile = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					if (tile == INVALID_TILE) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TILE_INDEX;
						SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(3, TileX(tile));
						SetDParam(4, TileY(tile));
					}
					auto check_signal_mode_control = [&](bool allowed) {
						bool warn = false;
						IterateActionsInsideConditional(prog, index, [&](const TraceRestrictItem &item) {
							if ((GetTraceRestrictType(item) == TRIT_SIGNAL_MODE_CONTROL) != allowed) warn = true;
						});
						if (warn) insert_warning(1, allowed ? STR_TRACE_RESTRICT_WARNING_SIGNAL_MODE_CONTROL_ONLY : STR_TRACE_RESTRICT_WARNING_NO_SIGNAL_MODE_CONTROL);
					};
					switch (static_cast<TraceRestrictPBSEntrySignalAuxField>(GetTraceRestrictAuxField(item))) {
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
					assert(GetTraceRestrictType(item) == TRIT_COND_RESERVATION_THROUGH);
					TileIndex tile = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					if (tile == INVALID_TILE) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_PASSES_TILE_INDEX;
						SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(3, TileX(tile));
						SetDParam(4, TileY(tile));
					}
					SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_RESERVATION_THROUGH_SHORT);
					break;
				}

				case TRVT_GROUP_INDEX: {
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					if (GetTraceRestrictValue(item) == INVALID_GROUP) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
						SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
					} else if (GetTraceRestrictValue(item) == DEFAULT_GROUP) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP_STR;
						SetDParam(2, STR_GROUP_DEFAULT_TRAINS);
						SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_GROUP;
						SetDParam(2, GetTraceRestrictValue(item) | GROUP_NAME_HIERARCHY);
					}
					break;
				}

				case TRVT_OWNER: {
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					CompanyID cid = static_cast<CompanyID>(GetTraceRestrictValue(item));
					if (cid == INVALID_COMPANY) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_OWNER;
						SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
						SetDParam(1, GetTypeString(item));
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
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
					SetDParam(3, GetTraceRestrictValue(item) * 1000);
					break;

				case TRVT_POWER_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_POWER_WEIGHT_RATIO;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_FORCE_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_FORCE_WEIGHT_RATIO;
					DrawInstructionStringConditionalCommon(item, properties);
					SetDParam(3, GetTraceRestrictValue(item) * 1000);
					break;

				case TRVT_SLOT_INDEX:
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					if (GetTraceRestrictValue(item) == INVALID_TRACE_RESTRICT_SLOT_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
						SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT;
						SetDParam(2, GetTraceRestrictValue(item));
					}
					break;

				case TRVT_SLOT_INDEX_INT: {
					assert(prog != nullptr);
					assert(GetTraceRestrictType(item) == TRIT_COND_SLOT_OCCUPANCY);
					uint32 value = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetTraceRestrictAuxField(item) ? STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_REMAINING_SHORT : STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_SHORT);
					if (GetTraceRestrictValue(item) == INVALID_TRACE_RESTRICT_SLOT_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_OCCUPANCY_STR;
						SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
						SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
						SetDParam(4, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(5, value);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_SLOT_OCCUPANCY;
						SetDParam(2, GetTraceRestrictValue(item));
						SetDParam(3, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(4, value);
					}
					break;
				}

				case TRVT_TRAIN_STATUS:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TRAIN_STATUS;
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(&_train_status_cond_ops, GetTraceRestrictCondOp(item)));
					SetDParam(2, GetDropDownStringByValue(&_train_status_value, GetTraceRestrictValue(item)));
					break;

				case TRVT_COUNTER_INDEX_INT: {
					assert(prog != nullptr);
					assert(GetTraceRestrictType(item) == TRIT_COND_COUNTER_VALUE);
					uint32 value = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					if (GetTraceRestrictValue(item) == INVALID_TRACE_RESTRICT_COUNTER_ID) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COUNTER_STR;
						SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
						SetDParam(2, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
						SetDParam(3, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(4, value);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COUNTER;
						SetDParam(1, GetTraceRestrictValue(item));
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(3, value);
					}
					break;
				}

				case TRVT_TIME_DATE_INT: {
					assert(prog != nullptr);
					assert(GetTraceRestrictType(item) == TRIT_COND_TIME_DATE_VALUE);
					uint32 value = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					instruction_string = GetTraceRestrictValue(item) == TRTDVF_HOUR_MINUTE ? STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_TIME_HHMM : STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;
					SetDParam(1, STR_TRACE_RESTRICT_TIME_MINUTE_ITEM + GetTraceRestrictValue(item));
					SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					SetDParam(3, value);
					break;
				}

				case TRVT_ENGINE_CLASS:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENGINE_CLASSES;
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(&_train_status_cond_ops, GetTraceRestrictCondOp(item)));
					SetDParam(2, GetDropDownStringByValue(&_engine_class_value, GetTraceRestrictValue(item)));
					break;

				case TRVT_ORDER_TARGET_DIAGDIR:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TARGET_DIRECTION;
					assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(&_target_direction_aux_value, GetTraceRestrictAuxField(item)));
					SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					SetDParam(3, GetDropDownStringByValue(&_diagdir_value, GetTraceRestrictValue(item)));
					break;

				default:
					NOT_REACHED();
					break;
			}
		}
	} else {
		switch (GetTraceRestrictType(item)) {
			case TRIT_NULL:
				switch (GetTraceRestrictValue(item)) {
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
				instruction_string = GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_PF_ALLOW_LONG : STR_TRACE_RESTRICT_PF_DENY;
				break;

			case TRIT_PF_PENALTY:
				switch (static_cast<TraceRestrictPathfinderPenaltyAuxField>(GetTraceRestrictAuxField(item))) {
					case TRPPAF_VALUE:
						instruction_string = STR_TRACE_RESTRICT_PF_PENALTY_ITEM;
						SetDParam(0, GetTraceRestrictValue(item));
						break;

					case TRPPAF_PRESET: {
						instruction_string = STR_TRACE_RESTRICT_PF_PENALTY_ITEM_PRESET;
						uint16 idx = GetTraceRestrictValue(item);
						assert(idx < TRPPPI_END);
						SetDParam(0, _pf_penalty_dropdown_str[idx]);
						break;
					}

					default:
						NOT_REACHED();
				}
				break;

			case TRIT_RESERVE_THROUGH:
				instruction_string = GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_RESERVE_THROUGH_CANCEL : STR_TRACE_RESTRICT_RESERVE_THROUGH;
				break;

			case TRIT_LONG_RESERVE:
				switch (static_cast<TraceRestrictLongReserveValueField>(GetTraceRestrictValue(item))) {
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
				break;

			case TRIT_WAIT_AT_PBS:
				switch (static_cast<TraceRestrictWaitAtPbsValueField>(GetTraceRestrictValue(item))) {
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
				switch (static_cast<TraceRestrictSlotCondOpField>(GetTraceRestrictCondOp(item))) {
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

					case TRSCOF_PBS_RES_END_ACQ_WAIT:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_ACQUIRE_WAIT_ITEM;
						break;

					case TRSCOF_PBS_RES_END_ACQ_TRY:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_TRY_ACQUIRE_ITEM;
						break;

					case TRSCOF_PBS_RES_END_RELEASE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE_ITEM;
						break;

					case TRSCOF_ACQUIRE_TRY_ON_RESERVE:
						instruction_string = STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE_ITEM_RES_ONLY;
						break;

					default:
						NOT_REACHED();
						break;
				}
				if (GetTraceRestrictValue(item) == INVALID_TRACE_RESTRICT_SLOT_ID) {
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
				} else {
					SetDParam(0, STR_TRACE_RESTRICT_SLOT_NAME);
					SetDParam(1, GetTraceRestrictValue(item));
				}
				SetDParam(2, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
				break;

			case TRIT_REVERSE:
				switch (static_cast<TraceRestrictReverseValueField>(GetTraceRestrictValue(item))) {
					case TRRVF_REVERSE:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_SIG;
						break;

					case TRRVF_CANCEL_REVERSE:
						instruction_string = STR_TRACE_RESTRICT_REVERSE_SIG_CANCEL;
						break;

					default:
						NOT_REACHED();
						break;
				}
				break;

			case TRIT_SPEED_RESTRICTION:
				if (GetTraceRestrictValue(item) != 0) {
					SetDParam(0, GetTraceRestrictValue(item));
					instruction_string = STR_TRACE_RESTRICT_SET_SPEED_RESTRICTION;
				} else {
					instruction_string = STR_TRACE_RESTRICT_REMOVE_SPEED_RESTRICTION;
				}
				break;

			case TRIT_NEWS_CONTROL:
				switch (static_cast<TraceRestrictNewsControlField>(GetTraceRestrictValue(item))) {
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
				uint32 value = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
				switch (static_cast<TraceRestrictCounterCondOpField>(GetTraceRestrictCondOp(item))) {
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
				if (GetTraceRestrictValue(item) == INVALID_TRACE_RESTRICT_COUNTER_ID) {
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
				} else {
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_NAME);
					SetDParam(1, GetTraceRestrictValue(item));
				}
				SetDParam(2, value);
				break;
			}

			case TRIT_PF_PENALTY_CONTROL:
				switch (static_cast<TraceRestrictPfPenaltyControlField>(GetTraceRestrictValue(item))) {
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
				switch (static_cast<TraceRestrictSpeedAdaptationControlField>(GetTraceRestrictValue(item))) {
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
				switch (static_cast<TraceRestrictSignalModeControlField>(GetTraceRestrictValue(item))) {
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
	DrawString(left + (rtl ? 0 : ScaleGUITrad(indent * 16)), right - (rtl ? ScaleGUITrad(indent * 16) : 0), y, instruction_string, selected ? TC_WHITE : TC_BLACK);
}

/** Main GUI window class */
class TraceRestrictWindow: public Window {
	TileIndex tile;                                                             ///< tile this window is for
	Track track;                                                                ///< track this window is for
	int selected_instruction;                                                   ///< selected instruction index, this is offset by one due to the display of the "start" item
	Scrollbar *vscroll;                                                         ///< scrollbar widget
	btree::btree_map<int, const TraceRestrictDropDownListSet *> drop_down_list_mapping; ///< mapping of widget IDs to drop down list sets
	bool value_drop_down_is_company;                                            ///< TR_WIDGET_VALUE_DROPDOWN is a company list
	TraceRestrictItem expecting_inserted_item;                                  ///< set to instruction when performing an instruction insertion, used to handle selection update on insertion
	int current_placement_widget;                                               ///< which widget has a SetObjectToPlaceWnd, if any
	int current_left_aux_plane;                                                 ///< current plane for TR_WIDGET_SEL_TOP_LEFT_AUX widget
	int base_copy_plane;                                                        ///< base plane for TR_WIDGET_SEL_COPY widget
	int base_share_plane;                                                       ///< base plane for TR_WIDGET_SEL_SHARE widget

public:
	TraceRestrictWindow(WindowDesc *desc, TileIndex tile, Track track)
			: Window(desc)
	{
		this->tile = tile;
		this->track = track;
		this->selected_instruction = -1;
		this->expecting_inserted_item = static_cast<TraceRestrictItem>(0);
		this->current_placement_widget = -1;

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(TR_WIDGET_SCROLLBAR);
		this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_AUX)->SetDisplayedPlane(SZSP_NONE);
		this->current_left_aux_plane = SZSP_NONE;
		this->FinishInitNested(MakeTraceRestrictRefId(tile, track));

		this->ReloadProgramme();
	}

	void Close() override
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

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST: {
				int sel = this->GetItemIndexFromPt(pt.y);

				if (_ctrl_pressed) {
					// scroll to target (for stations, waypoints, depots)

					if (sel == -1) return;

					TraceRestrictItem item = this->GetItem(this->GetProgram(), sel);
					TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item).value_type;
					if (val_type == TRVT_ORDER) {
						switch (static_cast<TraceRestrictOrderCondAuxField>(GetTraceRestrictAuxField(item))) {
							case TROCAF_STATION:
							case TROCAF_WAYPOINT: {
								BaseStation *st = BaseStation::GetIfValid(GetTraceRestrictValue(item));
								if (st) {
									ScrollMainWindowToTile(st->xy);
								}
								break;
							}

							case TROCAF_DEPOT: {
								Depot *depot = Depot::GetIfValid(GetTraceRestrictValue(item));
								if (depot) {
									ScrollMainWindowToTile(depot->xy);
								}
								break;
							}
						}
					} else if (val_type == TRVT_TILE_INDEX || val_type == TRVT_TILE_INDEX_THROUGH) {
						TileIndex tile = *(TraceRestrictProgram::InstructionAt(this->GetProgram()->items, sel - 1) + 1);
						if (tile != INVALID_TILE) {
							ScrollMainWindowToTile(tile);
						}
					}
					return;
				}

				this->CloseChildWindows();
				HideDropDownMenu(this);

				if (sel == -1 || this->GetOwner() != _local_company) {
					// Deselect
					this->selected_instruction = -1;
				} else {
					this->selected_instruction = sel;
				}

				this->expecting_inserted_item = static_cast<TraceRestrictItem>(0);

				this->UpdateButtonState();
				break;
			}

			case TR_WIDGET_INSERT: {
				if (this->GetOwner() != _local_company || this->selected_instruction < 1) {
					return;
				}

				uint32 disabled = _program_insert_or_if_hide_mask;
				uint32 hidden = 0;
				TraceRestrictItem item = this->GetSelected();
				if (GetTraceRestrictType(item) == TRIT_COND_ENDIF ||
						(IsTraceRestrictConditional(item) && GetTraceRestrictCondFlags(item) != 0)) {
					// this is either: an else/or if, an else, or an end if
					// try to include else if, else in insertion list
					if (!ElseInsertionDryRun(false)) disabled |= _program_insert_else_hide_mask;
					if (!ElseIfInsertionDryRun(false)) disabled |= _program_insert_else_if_hide_mask;
				} else {
					// can't insert else/end if here
					disabled |= _program_insert_else_hide_mask | _program_insert_else_if_hide_mask;
				}
				if (this->selected_instruction > 1) {
					TraceRestrictItem prev_item = this->GetItem(this->GetProgram(), this->selected_instruction - 1);
					if (IsTraceRestrictConditional(prev_item) && GetTraceRestrictType(prev_item) != TRIT_COND_ENDIF) {
						// previous item is either: an if, or an else/or if

						// else if has same validation rules as or if, use it instead of creating another test function
						if (ElseIfInsertionDryRun(false)) disabled &= ~_program_insert_or_if_hide_mask;
					}
				}
				if (!_settings_client.gui.show_adv_tracerestrict_features) {
					hidden |= _program_slot_hide_mask | _program_wait_pbs_hide_mask | _program_reverse_hide_mask |
							_program_speed_res_hide_mask | _program_counter_hide_mask | _program_penalty_adj_hide_mask;
				}
				if (!_settings_client.gui.show_adv_tracerestrict_features || !_settings_game.vehicle.train_speed_adaptation) {
					hidden |= _program_speed_adapt_hide_mask;
				}
				if (!(_settings_client.gui.show_adv_tracerestrict_features && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && _signal_style_masks.combined_normal_shunt != 0)) {
					hidden |= _program_signal_mode_hide_mask;
				}

				this->ShowDropDownListWithValue(&_program_insert, 0, true, TR_WIDGET_INSERT, disabled, hidden);
				break;
			}

			case TR_WIDGET_REMOVE: {
				TraceRestrictItem item = this->GetSelected();
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				TraceRestrictDoCommandP(tile, track, _ctrl_pressed ? TRDCT_SHALLOW_REMOVE_ITEM : TRDCT_REMOVE_ITEM,
						this->selected_instruction - 1, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_REMOVE_ITEM);
				break;
			}

			case TR_WIDGET_UP_BTN:
			case TR_WIDGET_DOWN_BTN: {
				TraceRestrictItem item = this->GetSelected();
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				uint32 p2 = 0;
				if (widget == TR_WIDGET_UP_BTN) p2 |= 1;
				if (_ctrl_pressed) p2 |= 2;

				uint32 offset = this->selected_instruction - 1;

				this->IsUpDownBtnUsable(widget == TR_WIDGET_UP_BTN, true);

				TraceRestrictDoCommandP(tile, track, TRDCT_MOVE_ITEM,
						offset, p2, STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
				break;
			}

			case TR_WIDGET_DUPLICATE: {
				TraceRestrictItem item = this->GetSelected();
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				uint32 offset = this->selected_instruction - 1;
				this->expecting_inserted_item = item;
				TraceRestrictDoCommandP(tile, track, TRDCT_DUPLICATE_ITEM,
						offset, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_MOVE_ITEM);
				break;
			}

			case TR_WIDGET_CONDFLAGS: {
				TraceRestrictItem item = this->GetSelected();
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				CondFlagsDropDownType type;
				if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
					if (GetTraceRestrictCondFlags(item) == 0) return; // end if
					type = CFDDT_ELSE;
				} else if (IsTraceRestrictConditional(item) && GetTraceRestrictCondFlags(item) != 0) {
					type = static_cast<CondFlagsDropDownType>(GetTraceRestrictCondFlags(item));
				} else {
					return;
				}

				uint32 disabled = 0;
				if (!ElseInsertionDryRun(true)) disabled |= _condflags_dropdown_else_hide_mask;
				if (!ElseIfInsertionDryRun(true)) disabled |= _condflags_dropdown_else_if_hide_mask;

				this->ShowDropDownListWithValue(&_condflags_dropdown, type, false, TR_WIDGET_CONDFLAGS, disabled, 0);
				break;
			}

			case TR_WIDGET_TYPE_COND:
			case TR_WIDGET_TYPE_NONCOND: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictGuiItemType type = GetItemGuiType(item);

				if (type != TRIT_NULL) {
					DropDownList dlist;
					for (const TraceRestrictDropDownListItem &item : GetTypeDropDownListItems(type)) {
						if (!ShouldHideTypeDropDownListItem(item.flags)) {
							dlist.emplace_back(new DropDownListStringItem(item.str, item.type, false));
						}
					}
					ShowDropDownList(this, std::move(dlist), type, widget, 0);
				}
				break;
			}

			case TR_WIDGET_COMPARATOR: {
				TraceRestrictItem item = this->GetSelected();
				const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(GetTraceRestrictTypeProperties(item));
				if (list_set) {
					this->ShowDropDownListWithValue(list_set, GetTraceRestrictCondOp(item), false, TR_WIDGET_COMPARATOR, 0, 0);
				}
				break;
			}

			case TR_WIDGET_SLOT_OP: {
				TraceRestrictItem item = this->GetSelected();
				this->ShowDropDownListWithValue(&_slot_op_cond_ops, GetTraceRestrictCondOp(item), false, TR_WIDGET_SLOT_OP, 0, 0);
				break;
			}

			case TR_WIDGET_COUNTER_OP: {
				TraceRestrictItem item = this->GetSelected();
				this->ShowDropDownListWithValue(&_counter_op_cond_ops, GetTraceRestrictCondOp(item), false, TR_WIDGET_COUNTER_OP, 0, 0);
				break;
			}

			case TR_WIDGET_VALUE_INT: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsIntegerValueType(type)) {
					SetDParam(0, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
					ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
					SetDParam(0, *(TraceRestrictProgram::InstructionAt(this->GetProgram()->items, this->selected_instruction - 1) + 1));
					ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				}
				break;
			}

			case TR_WIDGET_VALUE_DECIMAL: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsDecimalValueType(type)) {
					int64 value, decimal;
					ConvertValueToDecimal(type, GetTraceRestrictValue(item), value, decimal);
					SetDParam(0, value);
					SetDParam(1, decimal);
					std::string saved = std::move(_settings_game.locale.digit_group_separator);
					_settings_game.locale.digit_group_separator.clear();
					ShowQueryString(STR_JUST_DECIMAL, STR_TRACE_RESTRICT_VALUE_CAPTION, 16, this, CS_NUMERAL_DECIMAL, QSF_NONE);
					_settings_game.locale.digit_group_separator = std::move(saved);
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				switch (GetTraceRestrictTypeProperties(item).value_type) {
					case TRVT_DENY:
						this->ShowDropDownListWithValue(&_deny_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_CARGO_ID:
						this->ShowDropDownListWithValue(GetSortedCargoTypeDropDownListSet(), GetTraceRestrictValue(item), true, TR_WIDGET_VALUE_DROPDOWN, 0, 0); // current cargo is permitted to not be in list
						break;

					case TRVT_DIRECTION:
						this->ShowDropDownListWithValue(&_direction_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_PF_PENALTY:
						this->ShowDropDownListWithValue(&_pf_penalty_dropdown, GetPathfinderPenaltyDropdownIndex(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_RESERVE_THROUGH:
						this->ShowDropDownListWithValue(&_reserve_through_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_LONG_RESERVE: {
						uint hidden = 0;
						if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC) hidden |= 4;
						this->ShowDropDownListWithValue(&_long_reserve_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, hidden);
						break;
					}

					case TRVT_WAIT_AT_PBS:
						this->ShowDropDownListWithValue(&_wait_at_pbs_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_GROUP_INDEX: {
						int selected;
						DropDownList dlist = GetGroupDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected);
						ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_VALUE_DROPDOWN, 0);
						break;
					}

					case TRVT_OWNER:
						this->ShowCompanyDropDownListWithValue(static_cast<CompanyID>(GetTraceRestrictValue(item)), false, TR_WIDGET_VALUE_DROPDOWN);
						break;

					case TRVT_SLOT_INDEX: {
						int selected;
						DropDownList dlist = GetSlotDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected, VEH_TRAIN, IsTraceRestrictTypeNonMatchingVehicleTypeSlot(GetTraceRestrictType(item)));
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_VALUE_DROPDOWN);
						break;
					}

					case TRVT_TRAIN_STATUS:
						this->ShowDropDownListWithValue(&_train_status_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_REVERSE:
						this->ShowDropDownListWithValue(&_reverse_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_NEWS_CONTROL:
						this->ShowDropDownListWithValue(&_news_control_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_ENGINE_CLASS:
						this->ShowDropDownListWithValue(&_engine_class_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_PF_PENALTY_CONTROL:
						this->ShowDropDownListWithValue(&_pf_penalty_control_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);;
						break;

					case TRVT_SPEED_ADAPTATION_CONTROL:
						this->ShowDropDownListWithValue(&_speed_adaptation_control_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_SIGNAL_MODE_CONTROL:
						this->ShowDropDownListWithValue(&_signal_mode_control_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					case TRVT_ORDER_TARGET_DIAGDIR:
						this->ShowDropDownListWithValue(&_diagdir_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0);
						break;

					default:
						break;
				}
				break;
			}

			case TR_WIDGET_LEFT_AUX_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				switch (GetTraceRestrictTypeProperties(item).value_type) {
					case TRVT_SLOT_INDEX_INT: {
						int selected;
						DropDownList dlist = GetSlotDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected, VEH_TRAIN, IsTraceRestrictTypeNonMatchingVehicleTypeSlot(GetTraceRestrictType(item)));
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_LEFT_AUX_DROPDOWN);
						break;
					}

					case TRVT_COUNTER_INDEX_INT: {
						int selected;
						DropDownList dlist = GetCounterDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected);
						if (!dlist.empty()) ShowDropDownList(this, std::move(dlist), selected, TR_WIDGET_LEFT_AUX_DROPDOWN);
						break;
					}

					case TRVT_TIME_DATE_INT: {
						this->ShowDropDownListWithValue(&_time_date_value, GetTraceRestrictValue(item), false, TR_WIDGET_LEFT_AUX_DROPDOWN, _settings_game.game_time.time_in_minutes ? 0 : 7, 0);
						break;
					}

					case TRVT_ORDER_TARGET_DIAGDIR: {
						this->ShowDropDownListWithValue(&_target_direction_aux_value, GetTraceRestrictAuxField(item), false, TR_WIDGET_LEFT_AUX_DROPDOWN, 0, 0);
						break;
					}

					default:
						break;
				}
				break;
			}

			case TR_WIDGET_VALUE_DEST: {
				SetObjectToPlaceAction(widget, ANIMCURSOR_PICKSTATION);
				break;
			}

			case TR_WIDGET_VALUE_SIGNAL: {
				SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
				break;
			}

			case TR_WIDGET_VALUE_TILE: {
				SetObjectToPlaceAction(widget, SPR_CURSOR_MOUSE);
				break;
			}

			case TR_WIDGET_GOTO_SIGNAL: {
				ScrollMainWindowToTile(this->tile);
				this->UpdateButtonState();
				break;
			}

			case TR_WIDGET_RESET: {
				TraceRestrictProgMgmtDoCommandP(tile, track, TRDCT_PROG_RESET, STR_TRACE_RESTRICT_ERROR_CAN_T_RESET_SIGNAL);
				break;
			}

			case TR_WIDGET_COPY:
			case TR_WIDGET_COPY_APPEND:
			case TR_WIDGET_SHARE:
			case TR_WIDGET_SHARE_ONTO:
				SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
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
				TraceRestrictProgMgmtDoCommandP(tile, track, TRDCT_PROG_UNSHARE, STR_TRACE_RESTRICT_ERROR_CAN_T_UNSHARE_PROGRAM);
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
		}
	}

	virtual void OnQueryTextFinished(char *str) override
	{
		if (StrEmpty(str)) {
			return;
		}

		TraceRestrictItem item = GetSelected();
		TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
		uint value;

		if (IsIntegerValueType(type) || type == TRVT_PF_PENALTY) {
			value = ConvertIntegerValue(type, atoi(str), false);
			if (value >= (1 << TRIFA_VALUE_COUNT)) {
				SetDParam(0, ConvertIntegerValue(type, (1 << TRIFA_VALUE_COUNT) - 1, true));
				SetDParam(1, 0);
				ShowErrorMessage(STR_TRACE_RESTRICT_ERROR_VALUE_TOO_LARGE, STR_EMPTY, WL_INFO);
				return;
			}

			if (type == TRVT_PF_PENALTY) {
				SetTraceRestrictAuxField(item, TRPPAF_VALUE);
			}
		} else if (IsDecimalValueType(type)) {
			char tmp_buffer[32];
			strecpy(tmp_buffer, str, lastof(tmp_buffer));
			str_replace_wchar(tmp_buffer, lastof(tmp_buffer), GetDecimalSeparatorChar(), '.');
			value = ConvertDecimalToValue(type, atof(tmp_buffer));
			if (value >= (1 << TRIFA_VALUE_COUNT)) {
				int64 value, decimal;
				ConvertValueToDecimal(type, (1 << TRIFA_VALUE_COUNT) - 1, value, decimal);
				SetDParam(0, value);
				SetDParam(1, decimal);
				ShowErrorMessage(STR_TRACE_RESTRICT_ERROR_VALUE_TOO_LARGE, STR_EMPTY, WL_INFO);
				return;
			}
		} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
			value = atoi(str);
			TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_DUAL_ITEM, this->selected_instruction - 1, value, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
			return;
		} else {
			return;
		}

		SetTraceRestrictValue(item, value);
		TraceRestrictDoCommandP(tile, track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnDropdownSelect(int widget, int index) override
	{
		TraceRestrictItem item = GetSelected();
		if (item == 0 || index < 0 || this->selected_instruction < 1) {
			return;
		}

		if (widget == TR_WIDGET_VALUE_DROPDOWN || widget == TR_WIDGET_LEFT_AUX_DROPDOWN) {
			TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
			if (this->value_drop_down_is_company || type.value_type == TRVT_GROUP_INDEX || type.value_type == TRVT_SLOT_INDEX || type.value_type == TRVT_SLOT_INDEX_INT || type.value_type == TRVT_COUNTER_INDEX_INT || type.value_type == TRVT_TIME_DATE_INT) {
				// this is a special company drop-down or group/slot-index drop-down
				SetTraceRestrictValue(item, index);
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				return;
			}
			if (type.value_type == TRVT_ORDER_TARGET_DIAGDIR && widget == TR_WIDGET_LEFT_AUX_DROPDOWN) {
				SetTraceRestrictAuxField(item, index);
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				return;
			}
		}

		if (widget == TR_WIDGET_TYPE_COND || widget == TR_WIDGET_TYPE_NONCOND) {
			SetTraceRestrictTypeAndNormalise(item, static_cast<TraceRestrictItemType>(index & 0xFFFF), index >> 16);

			TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
		}

		const TraceRestrictDropDownListSet *list_set = this->drop_down_list_mapping[widget];
		if (!list_set) {
			return;
		}

		uint value = list_set->value_array[index];

		switch (widget) {
			case TR_WIDGET_INSERT: {
				TraceRestrictItem insert_item = 0;

				TraceRestrictCondFlags cond_flags = static_cast<TraceRestrictCondFlags>(value >> 16);
				value &= 0xFFFF;
				SetTraceRestrictTypeAndNormalise(insert_item, static_cast<TraceRestrictItemType>(value));
				SetTraceRestrictCondFlags(insert_item, cond_flags); // this needs to happen after calling SetTraceRestrictTypeAndNormalise

				this->expecting_inserted_item = insert_item;
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_INSERT_ITEM, this->selected_instruction - 1, insert_item, STR_TRACE_RESTRICT_ERROR_CAN_T_INSERT_ITEM);
				break;
			}

			case TR_WIDGET_CONDFLAGS: {
				CondFlagsDropDownType cond_type = static_cast<CondFlagsDropDownType>(value);
				if (cond_type == CFDDT_ELSE) {
					SetTraceRestrictTypeAndNormalise(item, TRIT_COND_ENDIF);
					SetTraceRestrictCondFlags(item, TRCF_ELSE);
				} else {
					if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
						// item is currently an else, convert to else/or if
						SetTraceRestrictTypeAndNormalise(item, TRIT_COND_UNDEFINED);
					}

					SetTraceRestrictCondFlags(item, static_cast<TraceRestrictCondFlags>(cond_type));
				}

				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_COMPARATOR:
			case TR_WIDGET_SLOT_OP:
			case TR_WIDGET_COUNTER_OP: {
				SetTraceRestrictCondOp(item, static_cast<TraceRestrictCondOp>(value));
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				if (GetTraceRestrictTypeProperties(item).value_type == TRVT_PF_PENALTY) {
					if (value == TRPPPI_END) {
						uint16 penalty_value;
						if (GetTraceRestrictAuxField(item) == TRPPAF_PRESET) {
							penalty_value = _tracerestrict_pathfinder_penalty_preset_values[GetTraceRestrictValue(item)];
						} else {
							penalty_value = GetTraceRestrictValue(item);
						}
						SetDParam(0, penalty_value);
						ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
						return;
					} else {
						SetTraceRestrictValue(item, value);
						SetTraceRestrictAuxField(item, TRPPAF_PRESET);
					}
				} else {
					SetTraceRestrictValue(item, value);
				}
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile) override
	{
		int widget = this->current_placement_widget;
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
	void OnPlaceObjectSignal(Point pt, TileIndex source_tile, int widget, int error_message)
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
		if(source_track == INVALID_TRACK) {
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
				TraceRestrictProgMgmtWithSourceDoCommandP(this->tile, this->track, TRDCT_PROG_COPY,
						source_tile, source_track, STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_PROGRAM);
				break;

			case TR_WIDGET_COPY_APPEND:
				TraceRestrictProgMgmtWithSourceDoCommandP(this->tile, this->track, TRDCT_PROG_COPY_APPEND,
						source_tile, source_track, STR_TRACE_RESTRICT_ERROR_CAN_T_COPY_APPEND_PROGRAM);
				break;

			case TR_WIDGET_SHARE:
				TraceRestrictProgMgmtWithSourceDoCommandP(this->tile, this->track, TRDCT_PROG_SHARE,
						source_tile, source_track, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			case TR_WIDGET_SHARE_ONTO:
				TraceRestrictProgMgmtWithSourceDoCommandP(source_tile, source_track, TRDCT_PROG_SHARE_IF_UNMAPPED,
						this->tile, this->track, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting an order target
	 */
	void OnPlaceObjectDestination(Point pt, TileIndex tile, int widget, int error_message)
	{
		TraceRestrictItem item = GetSelected();
		if (GetTraceRestrictTypeProperties(item).value_type != TRVT_ORDER) return;

		bool stations_only = (GetTraceRestrictType(item) == TRIT_COND_LAST_STATION);

		if (IsDepotTypeTile(tile, TRANSPORT_RAIL)) {
			if (stations_only) return;
			SetTraceRestrictValue(item, GetDepotIndex(tile));
			SetTraceRestrictAuxField(item, TROCAF_DEPOT);
		} else if (IsRailWaypointTile(tile)) {
			if (stations_only) return;
			SetTraceRestrictValue(item, GetStationIndex(tile));
			SetTraceRestrictAuxField(item, TROCAF_WAYPOINT);
		} else if (IsTileType(tile, MP_STATION)) {
			StationID st_index = GetStationIndex(tile);
			const Station *st = Station::Get(st_index);
			if (st->facilities & FACIL_TRAIN) {
				SetTraceRestrictValue(item, st_index);
				SetTraceRestrictAuxField(item, TROCAF_STATION);
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

		TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting a signal tile value
	 */
	void OnPlaceObjectSignalTileValue(Point pt, TileIndex tile, int widget, int error_message)
	{
		TraceRestrictItem item = GetSelected();
		TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item).value_type;
		if (val_type != TRVT_TILE_INDEX && val_type != TRVT_TILE_INDEX_THROUGH) return;

		if (!IsInfraTileUsageAllowed(VEH_TRAIN, _local_company, tile)) {
			ShowErrorMessage(error_message, STR_ERROR_AREA_IS_OWNED_BY_ANOTHER, WL_INFO);
			return;
		}

		if (IsRailDepotTile(tile)) {
			// OK
		} else if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(tile)) {
			// OK
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

		TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_DUAL_ITEM, this->selected_instruction - 1, tile, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	/**
	 * Common OnPlaceObject handler for instruction value modification actions which involve selecting a tile value
	 */
	void OnPlaceObjectTileValue(Point pt, TileIndex tile, int widget, int error_message)
	{
		TraceRestrictItem item = GetSelected();
		TraceRestrictValueType val_type = GetTraceRestrictTypeProperties(item).value_type;
		if (val_type != TRVT_TILE_INDEX && val_type != TRVT_TILE_INDEX_THROUGH) return;

		TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_DUAL_ITEM, this->selected_instruction - 1, tile, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
		this->ResetObjectToPlaceAction();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + WidgetDimensions::scaled.framerect.Vertical();
				break;

			case TR_WIDGET_GOTO_SIGNAL:
				size->width = std::max<uint>(12, NWidgetScrollbar::GetVerticalDimension().width);
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

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != TR_WIDGET_INSTRUCTION_LIST) return;

		int y = r.top + WidgetDimensions::scaled.framerect.top;
		int line_height = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST)->resize_y;
		int scroll_position = this->vscroll->GetPosition();

		// prog may be nullptr
		const TraceRestrictProgram *prog = this->GetProgram();

		int count = this->GetItemCount(prog);
		uint indent = 1;
		for (int i = 0; i < count; i++) {
			TraceRestrictItem item = this->GetItem(prog, i);
			uint this_indent = indent;
			if (IsTraceRestrictConditional(item)) {
				if (GetTraceRestrictCondFlags(item) & (TRCF_ELSE | TRCF_OR)) {
					this_indent--;
				} else if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
					indent--;
					this_indent--;
				} else {
					indent++;
				}
			} else if (GetTraceRestrictType(item) == TRIT_NULL) {
				this_indent = 0;
			}

			if (i >= scroll_position && this->vscroll->IsVisible(i)) {
				DrawInstructionString(prog, item, i, y, i == this->selected_instruction, this_indent, r.left + WidgetDimensions::scaled.framerect.left, r.right - WidgetDimensions::scaled.framerect.right);
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

	virtual void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case TR_WIDGET_VALUE_INT: {
				SetDParam(0, STR_JUST_COMMA);
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (type == TRVT_TIME_DATE_INT && GetTraceRestrictValue(item) == TRTDVF_HOUR_MINUTE) {
					SetDParam(0, STR_JUST_TIME_HHMM);
				}
				SetDParam(1, 0);
				if (IsIntegerValueType(type)) {
					SetDParam(1, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
				} else if (type == TRVT_SLOT_INDEX_INT || type == TRVT_COUNTER_INDEX_INT || type == TRVT_TIME_DATE_INT) {
					SetDParam(1, *(TraceRestrictProgram::InstructionAt(this->GetProgram()->items, this->selected_instruction - 1) + 1));
				}
				break;
			}

			case TR_WIDGET_VALUE_DECIMAL: {
				SetDParam(0, 0);
				SetDParam(1, 0);
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsDecimalValueType(type)) {
					int64 value, decimal;
					ConvertValueToDecimal(type, GetTraceRestrictValue(item), value, decimal);
					SetDParam(0, value);
					SetDParam(1, decimal);
				}
				break;
			}

			case TR_WIDGET_CAPTION: {
				const TraceRestrictProgram *prog = this->GetProgram();
				if (prog) {
					SetDParam(0, prog->refcount);
				} else {
					SetDParam(0, 1);
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
				if ((type.value_type == TRVT_PF_PENALTY &&
						GetTraceRestrictAuxField(item) == TRPPAF_VALUE)
						|| type.value_type == TRVT_GROUP_INDEX
						|| type.value_type == TRVT_SLOT_INDEX) {
					SetDParam(0, GetTraceRestrictValue(item));
				}
				break;
			}

			case TR_WIDGET_LEFT_AUX_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
				if (type.value_type == TRVT_SLOT_INDEX_INT || type.value_type == TRVT_COUNTER_INDEX_INT || type.value_type == TRVT_TIME_DATE_INT) {
					SetDParam(0, GetTraceRestrictValue(item));
				}
				break;
			}
		}
	}

	bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case TR_WIDGET_SHARE: {
				uint64 arg = STR_TRACE_RESTRICT_SHARE_TOOLTIP;
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, 1, &arg, close_cond);
				return true;
			}

			case TR_WIDGET_UNSHARE: {
				uint64 arg = STR_TRACE_RESTRICT_UNSHARE_TOOLTIP;
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, 1, &arg, close_cond);
				return true;
			}

			case TR_WIDGET_SHARE_ONTO: {
				uint64 arg = (this->base_share_plane == DPS_UNSHARE) ? STR_TRACE_RESTRICT_UNSHARE_TOOLTIP : STR_TRACE_RESTRICT_SHARE_TOOLTIP;
				GuiShowTooltips(this, STR_TRACE_RESTRICT_SHARE_TOOLTIP_EXTRA, 1, &arg, close_cond);
				return true;
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

private:
	/**
	 * Helper function to make start and end instructions (these are not stored in the actual program)
	 */
	TraceRestrictItem MakeSpecialItem(TraceRestrictNullTypeSpecialValue value) const
	{
		TraceRestrictItem item = 0;
		SetTraceRestrictType(item, TRIT_NULL);
		SetTraceRestrictValue(item, value);
		return item;
	}

	/**
	 * Get item count of program, including start and end markers
	 */
	int GetItemCount(const TraceRestrictProgram *prog) const
	{
		if (prog) {
			return 2 + (int)prog->GetInstructionCount();
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
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(tile, track), false);
	}

	/**
	 * Get instruction at @p index in program @p prog
	 * This correctly handles start/end markers, offsets, etc.
	 * This returns a 0 instruction if out of bounds
	 * @p prog may be nullptr
	 */
	TraceRestrictItem GetItem(const TraceRestrictProgram *prog, int index) const
	{
		if (index < 0) {
			return 0;
		}

		if (index == 0) {
			return MakeSpecialItem(TRNTSV_START);
		}

		if (prog) {
			size_t instruction_count = prog->GetInstructionCount();

			if (static_cast<size_t>(index) == instruction_count + 1) {
				return MakeSpecialItem(TRNTSV_END);
			}

			if (static_cast<size_t>(index) > instruction_count + 1) {
				return 0;
			}

			return prog->items[prog->InstructionOffsetToArrayOffset(index - 1)];
		} else {
			// No program defined, this is equivalent to an empty program
			if (index == 1) {
				return MakeSpecialItem(TRNTSV_END);
			} else {
				return 0;
			}
		}
	}

	/**
	 * Get selected instruction, or a zero instruction
	 */
	TraceRestrictItem GetSelected() const
	{
		return this->GetItem(this->GetProgram(), this->selected_instruction);
	}

	/**
	 * Get owner of the signal tile this window is pointing at
	 */
	Owner GetOwner()
	{
		return GetTileOwner(this->tile);
	}

	/**
	 * Return item index from point in instruction list widget
	 */
	int GetItemIndexFromPt(int y)
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST);
		int sel = (y - nwid->pos_y - WidgetDimensions::scaled.framerect.top) / nwid->resize_y; // Selected line

		if ((uint)sel >= this->vscroll->GetCapacity()) return -1;

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
			// program length has changed

			if (this->GetItemCount(prog) < this->vscroll->GetCount() ||
					this->GetItem(prog, this->selected_instruction) != this->expecting_inserted_item) {
				// length has shrunk or if we weren't expecting an insertion, deselect
				this->selected_instruction = -1;
			}
			this->expecting_inserted_item = static_cast<TraceRestrictItem>(0);

			// update scrollbar size
			this->vscroll->SetCount(this->GetItemCount(prog));
		}
		this->UpdateButtonState();
	}

	bool IsUpDownBtnUsable(bool up, bool update_selection = false) {
		const TraceRestrictProgram *prog = this->GetProgram();
		if (!prog) return false;

		TraceRestrictItem item = this->GetSelected();
		if (GetTraceRestrictType(item) == TRIT_NULL) return false;

		std::vector<TraceRestrictItem> items = prog->items; // copy
		uint32 offset = this->selected_instruction - 1;
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
		if (!prog) return false;

		TraceRestrictItem item = this->GetSelected();
		if (GetTraceRestrictType(item) == TRIT_NULL) return false;

		uint32 offset = this->selected_instruction - 1;
		if (TraceRestrictProgramDuplicateItemAtDryRun(prog->items, offset)) {
			return true;
		}

		return false;
	}

	void UpdatePlaceObjectPlanes()
	{
		int widget = this->current_placement_widget;

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

	/**
	 * Update button states, text values, etc.
	 */
	void UpdateButtonState()
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

		this->GetWidget<NWidgetCore>(TR_WIDGET_CAPTION)->widget_data =
				(prog && prog->refcount > 1) ? STR_TRACE_RESTRICT_CAPTION_SHARED : STR_TRACE_RESTRICT_CAPTION;

		this->SetWidgetDisabledState(TR_WIDGET_HIGHLIGHT, prog == nullptr);
		extern const TraceRestrictProgram *_viewport_highlight_tracerestrict_program;
		this->SetWidgetLoweredState(TR_WIDGET_HIGHLIGHT, prog != nullptr && _viewport_highlight_tracerestrict_program == prog);

		auto left_aux_guard = scope_guard([&]() {
			if (this->current_left_aux_plane != left_aux_sel->shown_plane) {
				this->current_left_aux_plane = left_aux_sel->shown_plane;
				this->ReInit();
			}
		});

		// Don't allow modifications if don't own
		if (this->GetOwner() != _local_company) {
			this->SetDirty();
			return;
		}

		this->EnableWidget(TR_WIDGET_COPY_APPEND);
		this->EnableWidget(TR_WIDGET_SHARE_ONTO);

		this->base_copy_plane = DPC_DUPLICATE;
		this->base_share_plane = DPS_SHARE;

		if (prog != nullptr && prog->refcount > 1) {
			// program is shared, show and enable unshare button, and reset button
			this->base_share_plane = DPS_UNSHARE;
			this->EnableWidget(TR_WIDGET_UNSHARE);
			this->EnableWidget(TR_WIDGET_RESET);
		} else if (this->GetItemCount(prog) > 2) {
			// program is non-empty and not shared, enable reset button
			this->EnableWidget(TR_WIDGET_RESET);
		} else {
			// program is empty and not shared, show copy and share buttons
			this->EnableWidget(TR_WIDGET_COPY);
			this->EnableWidget(TR_WIDGET_SHARE);
			this->base_copy_plane = DPC_COPY;
		}

		this->GetWidget<NWidgetCore>(TR_WIDGET_COPY_APPEND)->tool_tip = (this->base_copy_plane == DPC_DUPLICATE) ? STR_TRACE_RESTRICT_DUPLICATE_TOOLTIP : STR_TRACE_RESTRICT_COPY_TOOLTIP;
		this->UpdatePlaceObjectPlanes();

		// haven't selected instruction
		if (this->selected_instruction < 1) {
			this->SetDirty();
			return;
		}

		TraceRestrictItem item = this->GetItem(prog, this->selected_instruction);
		if (item != 0) {
			if (GetTraceRestrictType(item) == TRIT_NULL) {
				switch (GetTraceRestrictValue(item)) {
					case TRNTSV_START:
						break;

					case TRNTSV_END:
						this->EnableWidget(TR_WIDGET_INSERT);
						break;

					default:
						NOT_REACHED();
						break;
				}
			} else if (GetTraceRestrictType(item) == TRIT_COND_ENDIF) {
				this->EnableWidget(TR_WIDGET_INSERT);
				if (GetTraceRestrictCondFlags(item) != 0) {
					// this is not an end if, it must be an else, enable removing
					this->EnableWidget(TR_WIDGET_REMOVE);

					// setup condflags dropdown to show else
					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					this->EnableWidget(TR_WIDGET_CONDFLAGS);
					this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->widget_data = STR_TRACE_RESTRICT_CONDITIONAL_ELSE;
				}
			} else {
				TraceRestrictTypePropertySet properties = GetTraceRestrictTypeProperties(item);

				int type_widget;
				if (IsTraceRestrictConditional(item)) {
					// note that else and end if items are not handled here, they are handled above

					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					left_sel->SetDisplayedPlane(DPL_TYPE);
					type_widget = TR_WIDGET_TYPE_COND;

					// setup condflags dropdown box
					left_2_sel->SetDisplayedPlane(DPL2_CONDFLAGS);
					switch (GetTraceRestrictCondFlags(item)) {
						case TRCF_DEFAULT:                            // opening if, leave disabled
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->widget_data = STR_TRACE_RESTRICT_CONDITIONAL_IF;
							break;

						case TRCF_ELSE:                               // else-if
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->widget_data = STR_TRACE_RESTRICT_CONDITIONAL_ELIF;
							this->EnableWidget(TR_WIDGET_CONDFLAGS);
							break;

						case TRCF_OR:                                 // or-if
							this->GetWidget<NWidgetCore>(TR_WIDGET_CONDFLAGS)->widget_data = STR_TRACE_RESTRICT_CONDITIONAL_ORIF;
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

				this->GetWidget<NWidgetCore>(type_widget)->widget_data =
						GetTypeString(item);

				if (properties.cond_type == TRCOT_BINARY || properties.cond_type == TRCOT_ALL) {
					middle_sel->SetDisplayedPlane(DPM_COMPARATOR);
					this->EnableWidget(TR_WIDGET_COMPARATOR);

					const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(properties);

					if (list_set) {
						this->GetWidget<NWidgetCore>(TR_WIDGET_COMPARATOR)->widget_data =
								GetDropDownStringByValue(list_set, GetTraceRestrictCondOp(item));
					}
				}

				if (IsIntegerValueType(properties.value_type)) {
					right_sel->SetDisplayedPlane(DPR_VALUE_INT);
					this->EnableWidget(TR_WIDGET_VALUE_INT);
				} else if(IsDecimalValueType(properties.value_type)) {
					right_sel->SetDisplayedPlane(DPR_VALUE_DECIMAL);
					this->EnableWidget(TR_WIDGET_VALUE_DECIMAL);
				} else {
					switch (properties.value_type) {
						case TRVT_DENY:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_PF_ALLOW : STR_TRACE_RESTRICT_PF_DENY;
							break;

						case TRVT_ORDER:
							right_sel->SetDisplayedPlane(DPR_VALUE_DEST);
							this->EnableWidget(TR_WIDGET_VALUE_DEST);
							break;

						case TRVT_CARGO_ID:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetCargoStringByID(GetTraceRestrictValue(item));
							break;

						case TRVT_DIRECTION:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_direction_value, GetTraceRestrictValue(item));
							break;

						case TRVT_TILE_INDEX:
							if (GetTraceRestrictType(item) == TRIT_COND_PBS_ENTRY_SIGNAL && GetTraceRestrictAuxField(item) == TRPESAF_RES_END_TILE) {
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
							if (GetTraceRestrictAuxField(item) == TRPPAF_VALUE) {
								this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_JUST_COMMA;
							} else {
								this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
										GetDropDownStringByValue(&_pf_penalty_dropdown, GetPathfinderPenaltyDropdownIndex(item));
							}
							break;

						case TRVT_RESERVE_THROUGH:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_RESERVE_THROUGH_CANCEL : STR_TRACE_RESTRICT_RESERVE_THROUGH;
							break;

						case TRVT_LONG_RESERVE:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_long_reserve_value, GetTraceRestrictValue(item));
							break;

						case TRVT_WAIT_AT_PBS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_wait_at_pbs_value, GetTraceRestrictValue(item));
							break;

						case TRVT_GROUP_INDEX:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							switch (GetTraceRestrictValue(item)) {
								case INVALID_GROUP:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
									break;

								case DEFAULT_GROUP:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_GROUP_DEFAULT_TRAINS;
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_GROUP_NAME;
									break;
							}
							break;

						case TRVT_OWNER:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_COMPANY;
							break;

						case TRVT_SLOT_INDEX: {
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							if (!IsTraceRestrictConditional(item)) {
								middle_sel->SetDisplayedPlane(DPM_SLOT_OP);
								this->EnableWidget(TR_WIDGET_SLOT_OP);
							}

							for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
								if (slot->vehicle_type != VEH_TRAIN && !IsTraceRestrictTypeNonMatchingVehicleTypeSlot(GetTraceRestrictType(item))) continue;
								if (slot->owner == this->GetOwner()) {
									this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
									break;
								}
							}

							this->GetWidget<NWidgetCore>(TR_WIDGET_SLOT_OP)->widget_data =
									GetDropDownStringByValue(&_slot_op_cond_ops, GetTraceRestrictCondOp(item));
							switch (GetTraceRestrictValue(item)) {
								case INVALID_TRACE_RESTRICT_SLOT_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_SLOT_NAME;
									break;
							}
							break;
						}

						case TRVT_SLOT_INDEX_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);

							for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
								if (slot->vehicle_type != VEH_TRAIN && !IsTraceRestrictTypeNonMatchingVehicleTypeSlot(GetTraceRestrictType(item))) continue;
								if (slot->owner == this->GetOwner()) {
									this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
									break;
								}
							}

							switch (GetTraceRestrictValue(item)) {
								case INVALID_TRACE_RESTRICT_SLOT_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_SLOT_NAME;
									break;
							}
							break;
						}

						case TRVT_TRAIN_STATUS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_train_status_value, GetTraceRestrictValue(item));
							break;

						case TRVT_REVERSE:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_reverse_value, GetTraceRestrictValue(item));
							break;

						case TRVT_NEWS_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_news_control_value, GetTraceRestrictValue(item));
							break;

						case TRVT_COUNTER_INDEX_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);
							if (!IsTraceRestrictConditional(item)) {
								left_sel->SetDisplayedPlane(DPL_COUNTER_OP);
								this->EnableWidget(TR_WIDGET_COUNTER_OP);
								this->GetWidget<NWidgetCore>(TR_WIDGET_COUNTER_OP)->widget_data =
										GetDropDownStringByValue(&_counter_op_cond_ops, GetTraceRestrictCondOp(item));
							}

							for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
								if (ctr->owner == this->GetOwner()) {
									this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
									break;
								}
							}

							switch (GetTraceRestrictValue(item)) {
								case INVALID_TRACE_RESTRICT_COUNTER_ID:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
									break;

								default:
									this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_COUNTER_NAME;
									break;
							}
							break;
						}

						case TRVT_TIME_DATE_INT: {
							right_sel->SetDisplayedPlane(DPR_VALUE_INT);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_INT);
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data = STR_TRACE_RESTRICT_TIME_MINUTE_SHORT + GetTraceRestrictValue(item);
							break;
						}

						case TRVT_ENGINE_CLASS:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_engine_class_value, GetTraceRestrictValue(item));
							break;

						case TRVT_PF_PENALTY_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_pf_penalty_control_value, GetTraceRestrictValue(item));
							break;

						case TRVT_SPEED_ADAPTATION_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_speed_adaptation_control_value, GetTraceRestrictValue(item));
							break;

						case TRVT_SIGNAL_MODE_CONTROL:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_signal_mode_control_value, GetTraceRestrictValue(item));
							break;

						case TRVT_ORDER_TARGET_DIAGDIR:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							left_aux_sel->SetDisplayedPlane(DPLA_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);
							this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_diagdir_value, GetTraceRestrictValue(item));
							this->GetWidget<NWidgetCore>(TR_WIDGET_LEFT_AUX_DROPDOWN)->widget_data =
									GetDropDownStringByValue(&_target_direction_aux_value, GetTraceRestrictAuxField(item));
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
			int button, uint32 disabled_mask, uint32 hidden_mask)
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
	void ShowCompanyDropDownListWithValue(CompanyID value, bool missing_ok, int button)
	{
		DropDownList list;

		for (Company *c : Company::Iterate()) {
			list.emplace_back(MakeCompanyDropDownListItem(c->index));
			if (c->index == value) missing_ok = true;
		}
		list.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_UNDEFINED_COMPANY, INVALID_COMPANY, false));
		if (INVALID_COMPANY == value) missing_ok = true;

		assert(missing_ok == true);
		assert(button == TR_WIDGET_VALUE_DROPDOWN);
		this->value_drop_down_is_company = true;

		ShowDropDownList(this, std::move(list), value, button, 0);
	}

	/**
	 * Helper function to set or unset a SetObjectToPlaceWnd, for the given widget and cursor type
	 */
	void SetObjectToPlaceAction(int widget, CursorID cursor)
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
		this->current_placement_widget = -1;
		this->UpdatePlaceObjectPlanes();
	}

	/**
	 * This used for testing whether else or else-if blocks could be inserted, or replace the selection
	 * If @p replace is true, replace selection with @p item, else insert @p item before selection
	 * Returns true if resulting instruction list passes validation
	 */
	bool GenericElseInsertionDryRun(TraceRestrictItem item, bool replace)
	{
		if (this->selected_instruction < 1) return false;
		uint offset = this->selected_instruction - 1;

		const TraceRestrictProgram *prog = this->GetProgram();
		if (!prog) return false;

		std::vector<TraceRestrictItem> items = prog->items; // copy

		if (offset >= (TraceRestrictProgram::GetInstructionCount(items) + (replace ? 0 : 1))) return false; // off the end of the program

		uint array_offset = (uint)TraceRestrictProgram::InstructionOffsetToArrayOffset(items, offset);
		if (replace) {
			items[array_offset] = item;
		} else {
			items.insert(items.begin() + array_offset, item);
		}

		TraceRestrictProgramActionsUsedFlags actions_used_flags;
		return TraceRestrictProgram::Validate(items, actions_used_flags).Succeeded();
	}

	/**
	 * Run GenericElseInsertionDryRun with an else instruction
	 */
	bool ElseInsertionDryRun(bool replace)
	{
		TraceRestrictItem item = 0;
		SetTraceRestrictType(item, TRIT_COND_ENDIF);
		SetTraceRestrictCondFlags(item, TRCF_ELSE);
		return GenericElseInsertionDryRun(item, replace);
	}

	/**
	 * Run GenericElseInsertionDr;yRun with an elif instruction
	 */
	bool ElseIfInsertionDryRun(bool replace)
	{
		TraceRestrictItem item = 0;
		SetTraceRestrictType(item, TRIT_COND_UNDEFINED);
		SetTraceRestrictCondFlags(item, TRCF_ELSE);
		return GenericElseInsertionDryRun(item, replace);
	}
};

static const NWidgetPart _nested_program_widgets[] = {
	// Title bar
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, TR_WIDGET_CAPTION), SetDataTip(STR_TRACE_RESTRICT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, TR_WIDGET_HIGHLIGHT), SetMinimalSize(12, 12), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_TRACE_RESTRICT_HIGHLIGHT_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	// Program display
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, TR_WIDGET_INSTRUCTION_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_TRACE_RESTRICT_INSTRUCTION_LIST_TOOLTIP),
				SetResize(1, 1), SetScrollbar(TR_WIDGET_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TR_WIDGET_SCROLLBAR),
	EndContainer(),

	// Button Bar
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_UP_BTN), SetMinimalSize(12, 12), SetDataTip(SPR_ARROW_UP, STR_TRACE_RESTRICT_UP_BTN_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_DOWN_BTN), SetMinimalSize(12, 12), SetDataTip(SPR_ARROW_DOWN, STR_TRACE_RESTRICT_DOWN_BTN_TOOLTIP),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT_2),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_TYPE_NONCOND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_TYPE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_CONDFLAGS), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_CONDFLAGS_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_L2), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_TYPE_COND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_TYPE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_COUNTER_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COUNTER_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_L), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT_AUX),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_LEFT_AUX_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_MIDDLE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_SLOT_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_SLOT_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_M), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_RIGHT),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_INT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_STRING1, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DECIMAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_DECIMAL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_VALUE_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DEST), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_TARGET, STR_TRACE_RESTRICT_SELECT_TARGET), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_SIGNAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_SIGNAL, STR_TRACE_RESTRICT_SELECT_SIGNAL), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_TILE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_TILE, STR_TRACE_RESTRICT_SELECT_TILE), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_R), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_GOTO_SIGNAL), SetMinimalSize(12, 12), SetDataTip(SPR_GOTO_LOCATION, STR_TRACE_RESTRICT_GOTO_SIGNAL_TOOLTIP),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_INSERT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_INSERT, STR_TRACE_RESTRICT_INSERT_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_REMOVE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_REMOVE, STR_TRACE_RESTRICT_REMOVE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_RESET), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_RESET, STR_TRACE_RESTRICT_RESET_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_COPY),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_COPY), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_COPY, STR_TRACE_RESTRICT_COPY_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_COPY_APPEND), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_APPEND, STR_TRACE_RESTRICT_COPY_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_DUPLICATE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_DUPLICATE, STR_TRACE_RESTRICT_DUPLICATE_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_SHARE),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_SHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SHARE, STR_NULL), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_UNSHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_UNSHARE, STR_NULL), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_SHARE_ONTO), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SHARE_ONTO, STR_NULL), SetResize(1, 0),
				EndContainer(),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _program_desc(
	WDP_AUTO, "trace_restrict_gui", 384, 100,
	WC_TRACE_RESTRICT, WC_BUILD_SIGNAL,
	WDF_CONSTRUCTION,
	_nested_program_widgets, lengthof(_nested_program_widgets)
);

/**
 * Show or create program window for given @p tile and @p track
 */
void ShowTraceRestrictProgramWindow(TileIndex tile, Track track)
{
	if (BringWindowToFrontById(WC_TRACE_RESTRICT, MakeTraceRestrictRefId(tile, track)) != nullptr) {
		return;
	}

	new TraceRestrictWindow(&_program_desc, tile, track);
}

/** Slot GUI widget IDs */
enum TraceRestrictSlotWindowWidgets {
	WID_TRSL_LIST_VEHICLE, // this must be first, see: DirtyVehicleListWindowForVehicle
	WID_TRSL_CAPTION,
	WID_TRSL_ALL_VEHICLES,
	WID_TRSL_LIST_SLOTS,
	WID_TRSL_LIST_SLOTS_SCROLLBAR,
	WID_TRSL_CREATE_SLOT,
	WID_TRSL_DELETE_SLOT,
	WID_TRSL_RENAME_SLOT,
	WID_TRSL_SET_SLOT_MAX_OCCUPANCY,
	WID_TRSL_SORT_BY_ORDER,
	WID_TRSL_SORT_BY_DROPDOWN,
	WID_TRSL_FILTER_BY_CARGO,
	WID_TRSL_FILTER_BY_CARGO_SEL,
	WID_TRSL_LIST_VEHICLE_SCROLLBAR,
};


static const NWidgetPart _nested_slot_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_TRSL_CAPTION), SetDataTip(STR_TRACE_RESTRICT_SLOT_CAPTION, STR_NULL),
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
						SetDataTip(SPR_GROUP_CREATE_TRAIN, STR_TRACE_RESTRICT_SLOT_CREATE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_DELETE_SLOT), SetFill(0, 1),
						SetDataTip(SPR_GROUP_DELETE_TRAIN, STR_TRACE_RESTRICT_SLOT_DELETE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_RENAME_SLOT), SetFill(0, 1),
						SetDataTip(SPR_GROUP_RENAME_TRAIN, STR_TRACE_RESTRICT_SLOT_RENAME_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), EndContainer(),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_TRSL_SET_SLOT_MAX_OCCUPANCY), SetFill(0, 1),
						SetDataTip(SPR_IMG_SETTINGS, STR_TRACE_RESTRICT_SLOT_SET_MAX_OCCUPANCY_TOOLTIP),
			EndContainer(),
		EndContainer(),
		/* right part */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRSL_SORT_BY_ORDER), SetMinimalSize(81, 12), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_TRSL_SORT_BY_DROPDOWN), SetMinimalSize(167, 12), SetDataTip(0x0, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_TRSL_FILTER_BY_CARGO_SEL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_TRSL_FILTER_BY_CARGO), SetMinimalSize(167, 12), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
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
private:
	/* Columns in the group list */
	enum ListColumns {
		VGC_NAME,          ///< Group name.
		VGC_NUMBER,        ///< Number of vehicles in the group.

		VGC_END
	};

	TraceRestrictSlotID slot_sel;     ///< Selected slot (for drag/drop)
	bool slot_set_max_occupancy;      ///< True if slot max occupancy is being changed, instead of renaming
	TraceRestrictSlotID slot_rename;  ///< Slot being renamed or max occupancy changed, INVALID_TRACE_RESTRICT_SLOT_ID if none
	TraceRestrictSlotID slot_over;    ///< Slot over which a vehicle is dragged, INVALID_TRACE_RESTRICT_SLOT_ID if none
	TraceRestrictSlotID slot_confirm; ///< Slot awaiting delete confirmation
	GUIList<const TraceRestrictSlot*> slots;   ///< List of slots
	uint tiny_step_height; ///< Step height for the slot list
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

		this->slots.clear();

		for (const TraceRestrictSlot *slot : TraceRestrictSlot::Iterate()) {
			if (slot->owner == owner && slot->vehicle_type == this->vli.vtype) {
				this->slots.push_back(slot);
			}
		}

		this->slots.ForceResort();
		this->slots.Sort(&SlotNameSorter);
		this->slots.shrink_to_fit();
		this->slots.RebuildDone();
	}

	/**
	 * Compute tiny_step_height and column_size
	 * @return Total width required for the group list.
	 */
	uint ComputeSlotInfoSize()
	{
		this->column_size[VGC_NAME] = GetStringBoundingBox(STR_GROUP_ALL_TRAINS);
		this->column_size[VGC_NAME].width = std::max((170u * FONT_HEIGHT_NORMAL) / 10u, this->column_size[VGC_NAME].width);
		this->tiny_step_height = this->column_size[VGC_NAME].height;

		SetDParamMaxValue(0, 9999, 3, FS_SMALL);
		SetDParamMaxValue(1, 9999, 3, FS_SMALL);
		this->column_size[VGC_NUMBER] = GetStringBoundingBox(STR_TRACE_RESTRICT_SLOT_MAX_OCCUPANCY);
		this->tiny_step_height = std::max(this->tiny_step_height, this->column_size[VGC_NUMBER].height);

		this->tiny_step_height += WidgetDimensions::scaled.matrix.top + ScaleGUITrad(1);

		return WidgetDimensions::scaled.framerect.Horizontal() + WidgetDimensions::scaled.vsep_wide +
			this->column_size[VGC_NAME].width + WidgetDimensions::scaled.vsep_wide +
			this->column_size[VGC_NUMBER].width + WidgetDimensions::scaled.vsep_normal;
	}

	/**
	 * Draw a row in the slot list.
	 * @param y Top of the row.
	 * @param left Left of the row.
	 * @param right Right of the row.
	 * @param g_id Group to list.
	 */
	void DrawSlotInfo(int y, int left, int right, TraceRestrictSlotID slot_id) const
	{
		/* Highlight the group if a vehicle is dragged over it */
		if (slot_id == this->slot_over) {
			GfxFillRect(left + WidgetDimensions::scaled.framerect.left, y + WidgetDimensions::scaled.framerect.top, right - WidgetDimensions::scaled.framerect.right,
					y + this->tiny_step_height - WidgetDimensions::scaled.framerect.bottom - WidgetDimensions::scaled.matrix.top, _colour_gradient[COLOUR_GREY][7]);
		}

		/* draw the selected group in white, else we draw it in black */
		TextColour colour = slot_id == this->vli.index ? TC_WHITE : TC_BLACK;
		bool rtl = _current_text_dir == TD_RTL;

		/* draw group name */
		StringID str;
		if (slot_id == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
			str = STR_GROUP_ALL_TRAINS + this->vli.vtype;
		} else {
			SetDParam(0, slot_id);
			str = STR_TRACE_RESTRICT_SLOT_NAME;
		}
		int x = rtl ? right - WidgetDimensions::scaled.framerect.right - WidgetDimensions::scaled.vsep_wide - this->column_size[VGC_NAME].width + 1 : left + WidgetDimensions::scaled.framerect.left + WidgetDimensions::scaled.vsep_wide;
		DrawString(x, x + this->column_size[VGC_NAME].width - 1, y + (this->tiny_step_height - this->column_size[VGC_NAME].height) / 2, str, colour);

		if (slot_id == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) return;

		const TraceRestrictSlot *slot = TraceRestrictSlot::Get(slot_id);

		/* draw the number of vehicles of the group */
		x = rtl ? x - WidgetDimensions::scaled.vsep_normal - this->column_size[VGC_NUMBER].width : x + WidgetDimensions::scaled.vsep_normal + this->column_size[VGC_NAME].width;
		SetDParam(0, slot->occupants.size());
		SetDParam(1, slot->max_occupancy);
		DrawString(x, x + this->column_size[VGC_NUMBER].width - 1, y + (this->tiny_step_height - this->column_size[VGC_NUMBER].height) / 2, STR_TRACE_RESTRICT_SLOT_MAX_OCCUPANCY, colour, SA_RIGHT | SA_FORCE);
	}

	/**
	 * Mark the widget containing the currently highlighted slot as dirty.
	 */
	void DirtyHighlightedSlotWidget()
	{
		if (this->slot_over == INVALID_TRACE_RESTRICT_SLOT_ID) return;

		if (this->slot_over == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
			this->SetWidgetDirty(WID_TRSL_ALL_VEHICLES);
		} else {
			this->SetWidgetDirty(WID_TRSL_LIST_SLOTS);
		}
	}

public:
	TraceRestrictSlotWindow(WindowDesc *desc, WindowNumber window_number) : BaseVehicleListWindow(desc, window_number)
	{
		this->CreateNestedTree();

		this->CheckCargoFilterEnableState(WID_TRSL_FILTER_BY_CARGO_SEL, false);

		this->vscroll = this->GetScrollbar(WID_TRSL_LIST_VEHICLE_SCROLLBAR);
		this->slot_sb = this->GetScrollbar(WID_TRSL_LIST_SLOTS_SCROLLBAR);
		this->sorting = &_sorting[GB_NONE].train;
		this->grouping = GB_NONE;

		this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		this->slot_sel = INVALID_TRACE_RESTRICT_SLOT_ID;
		this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
		this->slot_set_max_occupancy = false;
		this->slot_over = INVALID_TRACE_RESTRICT_SLOT_ID;

		this->vehgroups.SetListing(*this->sorting);
		this->vehgroups.ForceRebuild();
		this->vehgroups.NeedResort();

		this->BuildVehicleList();
		this->SortVehicleList();

		this->slots.ForceRebuild();
		this->slots.NeedResort();
		this->BuildSlotList(vli.company);

		this->GetWidget<NWidgetCore>(WID_TRSL_CREATE_SLOT)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_TRSL_DELETE_SLOT)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_TRSL_RENAME_SLOT)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_TRSL_LIST_VEHICLE)->tool_tip = STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP + this->vli.vtype;

		this->FinishInitNested(window_number);
		this->owner = vli.company;
	}

	void Close() override
	{
		*this->sorting = this->vehgroups.GetListing();
		this->Window::Close();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_TRSL_LIST_SLOTS: {
				size->width = this->ComputeSlotInfoSize();
				resize->height = this->tiny_step_height;

				/* Minimum height is the height of the list widget minus all vehicles... */
				size->height = 4 * GetVehicleListHeight(this->vli.vtype, this->tiny_step_height) - this->tiny_step_height;

				/* ... minus the buttons at the bottom ... */
				uint max_icon_height = GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_CREATE_SLOT)->widget_data).height;
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_DELETE_SLOT)->widget_data).height);
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_RENAME_SLOT)->widget_data).height);
				max_icon_height = std::max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_SET_SLOT_MAX_OCCUPANCY)->widget_data).height);

				/* Get a multiple of tiny_step_height of that amount */
				size->height = Ceil(size->height - max_icon_height, tiny_step_height);
				break;
			}

			case WID_TRSL_ALL_VEHICLES:
				size->width = this->ComputeSlotInfoSize();
				size->height = this->tiny_step_height;
				break;

			case WID_TRSL_SORT_BY_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_TRSL_LIST_VEHICLE:
				this->ComputeSlotInfoSize();
				resize->height = GetVehicleListHeight(this->vli.vtype, this->tiny_step_height);
				size->height = 4 * resize->height;
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
		if (this->slot_rename != INVALID_TRACE_RESTRICT_SLOT_ID && this->slot_rename != NEW_TRACE_RESTRICT_SLOT_ID &&
				!TraceRestrictSlot::IsValidID(this->slot_rename)) {
			CloseWindowByClass(WC_QUERY_STRING);
			this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
		}

		if (this->vli.index != ALL_TRAINS_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(this->vli.index)) {
			this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		}

		if (gui_scope) this->CheckCargoFilterEnableState(WID_TRSL_FILTER_BY_CARGO_SEL, true);

		this->SetDirty();
	}

	virtual void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_TRSL_FILTER_BY_CARGO:
				SetDParam(0, this->cargo_filter_texts[this->cargo_filter_criteria]);
				break;

			case WID_TRSL_CAPTION:
				SetDParam(0, STR_VEHICLE_TYPE_TRAINS + this->vli.vtype);
				break;
		}
	}

	virtual void OnPaint() override
	{
		/* If we select the all vehicles, this->list will contain all vehicles of the owner
		 * else this->list will contain all vehicles which belong to the selected group */
		this->BuildVehicleList();
		this->SortVehicleList();

		this->BuildSlotList(this->owner);

		this->slot_sb->SetCount((uint)this->slots.size());
		this->vscroll->SetCount((uint)this->vehgroups.size());

		/* Disable the slot specific function when we select all vehicles */
		this->SetWidgetsDisabledState(this->vli.index == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID || _local_company != this->vli.company,
				WID_TRSL_DELETE_SLOT,
				WID_TRSL_RENAME_SLOT,
				WID_TRSL_SET_SLOT_MAX_OCCUPANCY,
				WIDGET_LIST_END);

		/* Disable remaining buttons for non-local companies
		 * Needed while changing _local_company, eg. by cheats
		 * All procedures (eg. move vehicle to a slot)
		 *  verify, whether you are the owner of the vehicle,
		 *  so it doesn't have to be disabled
		 */
		this->SetWidgetsDisabledState(_local_company != this->vli.company,
				WID_TRSL_CREATE_SLOT,
				WIDGET_LIST_END);

		/* Set text of sort by dropdown */
		this->GetWidget<NWidgetCore>(WID_TRSL_SORT_BY_DROPDOWN)->widget_data = this->vehicle_group_none_sorter_names[this->vehgroups.SortType()];

		this->GetWidget<NWidgetCore>(WID_TRSL_FILTER_BY_CARGO)->widget_data = this->cargo_filter_texts[this->cargo_filter_criteria];

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES:
				DrawSlotInfo(r.top + WidgetDimensions::scaled.framerect.top, r.left, r.right, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID);
				break;

			case WID_TRSL_LIST_SLOTS: {
				int y1 = r.top + WidgetDimensions::scaled.framerect.top;
				int max = std::min<int>(this->slot_sb->GetPosition() + this->slot_sb->GetCapacity(), (int)this->slots.size());
				for (int i = this->slot_sb->GetPosition(); i < max; ++i) {
					const TraceRestrictSlot *slot = this->slots[i];

					assert(slot->owner == this->owner);

					DrawSlotInfo(y1, r.left, r.right, slot->index);

					y1 += this->tiny_step_height;
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
			w->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
			DoCommandP(0, w->slot_confirm, 0, CMD_DELETE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_DELETE));
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_ORDER: // Flip sorting method ascending/descending
				this->vehgroups.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_TRSL_SORT_BY_DROPDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->vehicle_group_none_sorter_names, this->vehgroups.SortType(), WID_TRSL_SORT_BY_DROPDOWN, 0,
						this->GetSorterDisableMask(this->vli.vtype), 0, DDSF_LOST_FOCUS);
				return;

			case WID_TRSL_FILTER_BY_CARGO: // Cargo filter dropdown
				ShowDropDownMenu(this, this->cargo_filter_texts, this->cargo_filter_criteria, WID_TRSL_FILTER_BY_CARGO, 0, 0);
				break;

			case WID_TRSL_ALL_VEHICLES: // All vehicles button
				if (this->vli.index != ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
					this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
					this->slot_sel = INVALID_TRACE_RESTRICT_SLOT_ID;
					this->vehgroups.ForceRebuild();
					this->SetDirty();
				}
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix Slot
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s >= this->slots.size()) return;

				this->slot_sel = this->vli.index = this->slots[id_s]->index;

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
				this->slot_confirm = this->vli.index;
				ShowQuery(STR_TRACE_RESTRICT_SLOT_QUERY_DELETE_CAPTION, STR_TRACE_RESTRICT_SLOT_DELETE_QUERY_TEXT, this, DeleteSlotCallback);
				break;
			}

			case WID_TRSL_RENAME_SLOT: // Rename the selected slot
				this->ShowRenameSlotWindow(this->vli.index);
				break;

			case WID_TRSL_SET_SLOT_MAX_OCCUPANCY: // Set max occupancy of the selected slot
				this->ShowSetSlotMaxOccupancyWindow(this->vli.index);
				break;
		}
	}

	void OnDragDrop_Vehicle(Point pt, int widget)
	{
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // All vehicles
				if (this->slot_sel != INVALID_TRACE_RESTRICT_SLOT_ID) {
					DoCommandP(0, this->slot_sel, this->vehicle_sel, CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_REMOVE_VEHICLE));

					this->vehicle_sel = INVALID_VEHICLE;
					this->slot_over = INVALID_GROUP;

					this->SetDirty();
				}
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix slot
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->slot_over = INVALID_GROUP;
				this->SetDirty();

				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s >= this->slots.size()) return; // click out of list bound

				if (_ctrl_pressed) {
					// remove from old group
					DoCommandP(0, this->slot_sel, vindex, CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_REMOVE_VEHICLE));
				}
				DoCommandP(0, this->slots[id_s]->index, vindex, CMD_ADD_VEHICLE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_ADD_VEHICLE));
				break;
			}

			case WID_TRSL_LIST_VEHICLE: { // Matrix vehicle
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->slot_over = INVALID_GROUP;
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

	virtual void OnDragDrop(Point pt, int widget) override
	{
		if (this->vehicle_sel != INVALID_VEHICLE) OnDragDrop_Vehicle(pt, widget);

		_cursor.vehchain = false;
	}

	virtual void OnQueryTextFinished(char *str) override
	{
		if (str != nullptr) {
			if (this->slot_set_max_occupancy) {
				if (!StrEmpty(str)) DoCommandP(0, this->slot_rename | (1 << 16), atoi(str), CMD_ALTER_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_SET_MAX_OCCUPANCY));
			} else if (this->slot_rename == NEW_TRACE_RESTRICT_SLOT_ID) {
				DoCommandP(0, this->vli.vtype, 0, CMD_CREATE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_CREATE), nullptr, str);
			} else {
				DoCommandP(0, this->slot_rename, 0, CMD_ALTER_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_RENAME), nullptr, str);
			}
		}
		this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
	}

	virtual void OnResize() override
	{
		this->slot_sb->SetCapacityFromWidget(this, WID_TRSL_LIST_SLOTS);
		this->vscroll->SetCapacityFromWidget(this, WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_DROPDOWN:
				this->vehgroups.SetSortType(index);
				this->UpdateSortingInterval();
				break;

			case WID_TRSL_FILTER_BY_CARGO: // Select a cargo filter criteria
				this->SetCargoFilterIndex(index);
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
		/* abort drag & drop */
		this->vehicle_sel = INVALID_VEHICLE;
		this->DirtyHighlightedSlotWidget();
		this->slot_over = INVALID_GROUP;
		this->SetWidgetDirty(WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnMouseDrag(Point pt, int widget) override
	{
		if (this->vehicle_sel == INVALID_VEHICLE) return;

		/* A vehicle is dragged over... */
		TraceRestrictSlotID new_slot_over = INVALID_TRACE_RESTRICT_SLOT_ID;
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // ... all trains.
				new_slot_over = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
				break;

			case WID_TRSL_LIST_SLOTS: { // ... the list of slots.
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0);
				if (id_s < this->slots.size()) new_slot_over = this->slots[id_s]->index;
				break;
			}
		}

		/* Do not highlight when dragging over the current group */
		if (this->slot_sel == new_slot_over) new_slot_over = INVALID_TRACE_RESTRICT_SLOT_ID;

		/* Mark widgets as dirty if the group changed. */
		if (new_slot_over != this->slot_over) {
			this->DirtyHighlightedSlotWidget();
			this->slot_over = new_slot_over;
			this->DirtyHighlightedSlotWidget();
		}
	}

	void ShowRenameSlotWindow(TraceRestrictSlotID slot_id)
	{
		assert(TraceRestrictSlot::IsValidID(slot_id));
		this->slot_set_max_occupancy = false;
		this->slot_rename = slot_id;
		SetDParam(0, slot_id);
		ShowQueryString(STR_TRACE_RESTRICT_SLOT_NAME, STR_TRACE_RESTRICT_SLOT_RENAME_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
	}

	void ShowSetSlotMaxOccupancyWindow(TraceRestrictSlotID slot_id)
	{
		this->slot_set_max_occupancy = true;
		this->slot_rename = slot_id;
		SetDParam(0, TraceRestrictSlot::Get(slot_id)->max_occupancy);
		ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_SLOT_SET_MAX_OCCUPANCY_CAPTION, 5, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
	}

	void ShowCreateSlotWindow()
	{
		this->slot_set_max_occupancy = false;
		this->slot_rename = NEW_TRACE_RESTRICT_SLOT_ID;
		ShowQueryString(STR_EMPTY, STR_TRACE_RESTRICT_SLOT_CREATE_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
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

static WindowDesc _slot_window_desc(
	WDP_AUTO, "list_groups_train", 525, 246,
	WC_TRACE_RESTRICT_SLOTS, WC_NONE,
	0,
	_nested_slot_widgets, lengthof(_nested_slot_widgets)
);

/**
 * Show the trace restrict slot window for the given company.
 * @param company The company to show the window for.
 */
void ShowTraceRestrictSlotWindow(CompanyID company, VehicleType vehtype)
{
	if (!Company::IsValidID(company)) return;

	WindowNumber num = VehicleListIdentifier(VL_SLOT_LIST, vehtype, company).Pack();
	AllocateWindowDescFront<TraceRestrictSlotWindow>(&_slot_window_desc, num);
}

/**
 * Finds a group list window determined by vehicle type and owner
 * @param vt vehicle type
 * @param owner owner of groups
 * @return pointer to VehicleGroupWindow, nullptr if not found
 */
static inline TraceRestrictSlotWindow *FindTraceRestrictSlotWindow(Owner owner)
{
	return (TraceRestrictSlotWindow *)FindWindowById(GetWindowClassForVehicleType(VEH_TRAIN), VehicleListIdentifier(VL_SLOT_LIST, VEH_TRAIN, owner).Pack());
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
enum TraceRestrictCounterWindowWidgets {
	WID_TRCL_CAPTION,
	WID_TRCL_LIST_COUNTERS,
	WID_TRCL_LIST_COUNTERS_SCROLLBAR,
	WID_TRCL_CREATE_COUNTER,
	WID_TRCL_DELETE_COUNTER,
	WID_TRCL_RENAME_COUNTER,
	WID_TRCL_SET_COUNTER_VALUE,
};


static const NWidgetPart _nested_counter_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_TRCL_CAPTION), SetDataTip(STR_TRACE_RESTRICT_COUNTER_CAPTION, STR_NULL),
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
					SetDataTip(STR_TRACE_RESTRICT_COUNTER_CREATE, STR_TRACE_RESTRICT_COUNTER_CREATE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_DELETE_COUNTER), SetMinimalSize(75, 12), SetFill(1, 0),
					SetDataTip(STR_TRACE_RESTRICT_COUNTER_DELETE, STR_TRACE_RESTRICT_COUNTER_DELETE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_RENAME_COUNTER), SetMinimalSize(75, 12), SetFill(1, 0),
					SetDataTip(STR_TRACE_RESTRICT_COUNTER_RENAME, STR_TRACE_RESTRICT_COUNTER_RENAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TRCL_SET_COUNTER_VALUE), SetMinimalSize(75, 12), SetFill(1, 0),
					SetDataTip(STR_TRACE_RESTRICT_COUNTER_SET_VALUE, STR_TRACE_RESTRICT_COUNTER_SET_VALUE_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

class TraceRestrictCounterWindow : public Window {
private:
	enum QueryTextOperation {
		QTO_RENAME,
		QTO_SET_VALUE,
	};

	Owner ctr_company;                  ///< Company
	QueryTextOperation qto;             ///< Active query text operation
	TraceRestrictCounterID ctr_qt_op;   ///< Counter being adjusted in query text operation, INVALID_TRACE_RESTRICT_COUNTER_ID if none
	TraceRestrictCounterID ctr_confirm; ///< Counter awaiting delete confirmation
	TraceRestrictCounterID selected;    ///< Selected counter
	GUIList<const TraceRestrictCounter*> ctrs;   ///< List of slots
	uint tiny_step_height; ///< Step height for the counter list
	uint value_col_width;  ///< Value column width
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
	 * Compute tiny_step_height and column_size
	 * @return Total width required for the group list.
	 */
	uint ComputeInfoSize()
	{
		SetDParamMaxValue(0, 9999, 3);
		Dimension dim = GetStringBoundingBox(STR_JUST_COMMA);
		this->tiny_step_height = dim.height + WidgetDimensions::scaled.matrix.top;
		this->value_col_width = dim.width;

		return WidgetDimensions::scaled.framerect.Horizontal() + WidgetDimensions::scaled.vsep_wide +
			170 + WidgetDimensions::scaled.vsep_wide +
			dim.width + WidgetDimensions::scaled.vsep_wide +
			WidgetDimensions::scaled.framerect.right;
	}

	/**
	 * Draw a row in the slot list.
	 * @param y Top of the row.
	 * @param left Left of the row.
	 * @param right Right of the row.
	 * @param g_id Group to list.
	 */
	void DrawCounterInfo(int y, int left, int right, TraceRestrictCounterID ctr_id) const
	{
		/* draw the selected counter in white, else we draw it in black */
		TextColour colour = ctr_id == this->selected ? TC_WHITE : TC_BLACK;
		bool rtl = _current_text_dir == TD_RTL;

		SetDParam(0, ctr_id);
		DrawString(left + WidgetDimensions::scaled.vsep_wide + (rtl ? this->value_col_width + WidgetDimensions::scaled.vsep_wide : 0),
				right - WidgetDimensions::scaled.vsep_wide - (rtl ? 0 : this->value_col_width + WidgetDimensions::scaled.vsep_wide),
				y, STR_TRACE_RESTRICT_COUNTER_NAME, colour);

		SetDParam(0, TraceRestrictCounter::Get(ctr_id)->value);
		DrawString(rtl ? left + WidgetDimensions::scaled.vsep_wide : right - WidgetDimensions::scaled.vsep_wide - this->value_col_width,
				rtl ? left + WidgetDimensions::scaled.vsep_wide + this->value_col_width : right - WidgetDimensions::scaled.vsep_wide,
				y, STR_JUST_COMMA, colour, SA_RIGHT | SA_FORCE);
	}

public:
	TraceRestrictCounterWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
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

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_TRCL_LIST_COUNTERS: {
				size->width = std::max<uint>(size->width, this->ComputeInfoSize());
				resize->height = this->tiny_step_height;
				size->height = std::max<uint>(size->height, 8 * resize->height);
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

		this->sb->SetCount((uint)this->ctrs.size());

		/* Disable the counter specific functions when no counter is selected */
		this->SetWidgetsDisabledState(this->selected == INVALID_TRACE_RESTRICT_COUNTER_ID || _local_company != this->ctr_company,
				WID_TRCL_DELETE_COUNTER,
				WID_TRCL_RENAME_COUNTER,
				WID_TRCL_SET_COUNTER_VALUE,
				WIDGET_LIST_END);

		/* Disable remaining buttons for non-local companies
		 * Needed while changing _local_company, eg. by cheats
		 * All procedures (eg. move vehicle to a slot)
		 *  verify, whether you are the owner of the vehicle,
		 *  so it doesn't have to be disabled
		 */
		this->SetWidgetsDisabledState(_local_company != this->ctr_company,
				WID_TRCL_CREATE_COUNTER,
				WIDGET_LIST_END);

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_TRCL_LIST_COUNTERS: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				int y1 = ir.top;
				int max = std::min<int>(this->sb->GetPosition() + this->sb->GetCapacity(), (int)this->ctrs.size());
				for (int i = this->sb->GetPosition(); i < max; ++i) {
					const TraceRestrictCounter *ctr = this->ctrs[i];

					assert(ctr->owner == this->ctr_company);

					DrawCounterInfo(y1, ir.left, ir.right, ctr->index);

					y1 += this->tiny_step_height;
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
			DoCommandP(0, w->ctr_confirm, 0, CMD_DELETE_TRACERESTRICT_COUNTER | CMD_MSG(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_DELETE));
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
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

			case WID_TRCL_SET_COUNTER_VALUE:
				this->ShowSetCounterValueWindow(this->selected);
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str) override
	{
		if (str != nullptr) {
			switch (this->qto) {
				case QTO_RENAME:
					if (this->ctr_qt_op == NEW_TRACE_RESTRICT_COUNTER_ID) {
						DoCommandP(0, 0, 0, CMD_CREATE_TRACERESTRICT_COUNTER | CMD_MSG(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_CREATE), nullptr, str);
					} else {
						DoCommandP(0, this->ctr_qt_op, 0, CMD_ALTER_TRACERESTRICT_COUNTER | CMD_MSG(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_RENAME), nullptr, str);
					}
					break;

				case QTO_SET_VALUE:
					if (!StrEmpty(str)) DoCommandP(0, this->ctr_qt_op | (1 << 16), atoi(str), CMD_ALTER_TRACERESTRICT_COUNTER | CMD_MSG(STR_TRACE_RESTRICT_ERROR_COUNTER_CAN_T_MODIFY));
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
		SetDParam(0, ctr_id);
		ShowQueryString(STR_TRACE_RESTRICT_COUNTER_NAME, STR_TRACE_RESTRICT_COUNTER_RENAME_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
	}

	void ShowSetCounterValueWindow(TraceRestrictCounterID ctr_id)
	{
		assert(TraceRestrictCounter::IsValidID(ctr_id));
		this->qto = QTO_SET_VALUE;
		this->ctr_qt_op = ctr_id;
		SetDParam(0, TraceRestrictCounter::Get(ctr_id)->value);
		ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_COUNTER_SET_VALUE_CAPTION, 5, this, CS_NUMERAL, QSF_ENABLE_DEFAULT);
	}

	void ShowCreateCounterWindow()
	{
		this->qto = QTO_RENAME;
		this->ctr_qt_op = NEW_TRACE_RESTRICT_COUNTER_ID;
		ShowQueryString(STR_EMPTY, STR_TRACE_RESTRICT_COUNTER_CREATE_CAPTION, MAX_LENGTH_TRACE_RESTRICT_SLOT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
	}
};

static WindowDesc _counter_window_desc(
	WDP_AUTO, "list_tr_counters", 525, 246,
	WC_TRACE_RESTRICT_COUNTERS, WC_NONE,
	0,
	_nested_counter_widgets, lengthof(_nested_counter_widgets)
);

/**
 * Show the trace restrict counter window for the given company.
 * @param company The company to show the window for.
 */
void ShowTraceRestrictCounterWindow(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<TraceRestrictCounterWindow>(&_counter_window_desc, (WindowNumber)company);
}
