/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_gui.cpp GUI code for Trace Restrict
 *
 * This is largely based on the programmable signals patch's GUI
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

#include "safeguards.h"

/** Widget IDs */
enum TraceRestrictWindowWidgets {
	TR_WIDGET_CAPTION,
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
	TR_WIDGET_VALUE_INT,
	TR_WIDGET_VALUE_DECIMAL,
	TR_WIDGET_VALUE_DROPDOWN,
	TR_WIDGET_VALUE_DEST,
	TR_WIDGET_VALUE_SIGNAL,
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
	TR_WIDGET_SHARE,
	TR_WIDGET_UNSHARE,
};

/** Selection mappings for NWID_SELECTION selectors */
enum PanelWidgets {
	// Left 2
	DPL2_TYPE = 0,
	DPL2_CONDFLAGS,
	DPL2_BLANK,

	// Left
	DPL_TYPE = 0,
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
	DPR_BLANK,

	// Share
	DPS_SHARE = 0,
	DPS_UNSHARE,

	// Copy
	DPC_COPY = 0,
	DPC_APPEND,
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
	INVALID_STRING_ID
};
static const uint32 _program_insert_else_hide_mask    = 8;     ///< disable bitmask for else
static const uint32 _program_insert_or_if_hide_mask   = 4;     ///< disable bitmask for orif
static const uint32 _program_insert_else_if_hide_mask = 2;     ///< disable bitmask for elif
static const uint32 _program_wait_pbs_hide_mask = 0x100;       ///< disable bitmask for wait at PBS
static const uint32 _program_slot_hide_mask = 0x200;           ///< disable bitmask for slot
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
	INVALID_STRING_ID
};
static const uint _long_reserve_value_val[] = {
	0,
	1,
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
	INVALID_STRING_ID
};
static const uint _direction_value_val[] = {
	TRDTSV_FRONT,
	TRDTSV_BACK,
	TRNTSV_NE,
	TRNTSV_SE,
	TRNTSV_SW,
	TRNTSV_NW,
};

/** value drop down list for direction type strings and values */
static const TraceRestrictDropDownListSet _direction_value = {
	_direction_value_str, _direction_value_val,
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

/**
 * Return the appropriate type dropdown TraceRestrictDropDownListSet for the given item type @p type
 */
static const TraceRestrictDropDownListSet *GetTypeDropDownListSet(TraceRestrictGuiItemType type, uint32 *hide_mask = nullptr)
{
	static const StringID str_action[] = {
		STR_TRACE_RESTRICT_PF_DENY,
		STR_TRACE_RESTRICT_PF_PENALTY,
		STR_TRACE_RESTRICT_RESERVE_THROUGH,
		STR_TRACE_RESTRICT_LONG_RESERVE,
		STR_TRACE_RESTRICT_WAIT_AT_PBS,
		STR_TRACE_RESTRICT_SLOT_OP,
		INVALID_STRING_ID,
	};
	static const uint val_action[] = {
		TRIT_PF_DENY,
		TRIT_PF_PENALTY,
		TRIT_RESERVE_THROUGH,
		TRIT_LONG_RESERVE,
		TRIT_WAIT_AT_PBS,
		TRIT_SLOT,
	};
	static const TraceRestrictDropDownListSet set_action = {
		str_action, val_action,
	};

	static const StringID str_cond[] = {
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_LENGTH,
		STR_TRACE_RESTRICT_VARIABLE_MAX_SPEED,
		STR_TRACE_RESTRICT_VARIABLE_CURRENT_ORDER,
		STR_TRACE_RESTRICT_VARIABLE_NEXT_ORDER,
		STR_TRACE_RESTRICT_VARIABLE_LAST_VISITED_STATION,
		STR_TRACE_RESTRICT_VARIABLE_CARGO,
		STR_TRACE_RESTRICT_VARIABLE_ENTRY_DIRECTION,
		STR_TRACE_RESTRICT_VARIABLE_PBS_ENTRY_SIGNAL,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_GROUP,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_OWNER,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_WEIGHT,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_POWER,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_MAX_TE,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_POWER_WEIGHT_RATIO,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_MAX_TE_WEIGHT_RATIO,
		STR_TRACE_RESTRICT_VARIABLE_TRAIN_SLOT,
		STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY,
		STR_TRACE_RESTRICT_VARIABLE_SLOT_OCCUPANCY_REMAINING,
		STR_TRACE_RESTRICT_VARIABLE_UNDEFINED,
		INVALID_STRING_ID,
	};
	static const uint val_cond[] = {
		TRIT_COND_TRAIN_LENGTH,
		TRIT_COND_MAX_SPEED,
		TRIT_COND_CURRENT_ORDER,
		TRIT_COND_NEXT_ORDER,
		TRIT_COND_LAST_STATION,
		TRIT_COND_CARGO,
		TRIT_COND_ENTRY_DIRECTION,
		TRIT_COND_PBS_ENTRY_SIGNAL,
		TRIT_COND_TRAIN_GROUP,
		TRIT_COND_TRAIN_OWNER,
		TRIT_COND_PHYS_PROP | (TRPPCAF_WEIGHT << 16),
		TRIT_COND_PHYS_PROP | (TRPPCAF_POWER << 16),
		TRIT_COND_PHYS_PROP | (TRPPCAF_MAX_TE << 16),
		TRIT_COND_PHYS_RATIO | (TRPPRCAF_POWER_WEIGHT << 16),
		TRIT_COND_PHYS_RATIO | (TRPPRCAF_MAX_TE_WEIGHT << 16),
		TRIT_COND_TRAIN_IN_SLOT,
		TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_OCCUPANTS << 16),
		TRIT_COND_SLOT_OCCUPANCY | (TRSOCAF_REMAINING << 16),
		TRIT_COND_UNDEFINED,
	};
	static const TraceRestrictDropDownListSet set_cond = {
		str_cond, val_cond,
	};

	bool is_conditional = IsTraceRestrictTypeConditional(ItemTypeFromGuiType(type));
	if (hide_mask) {
		if (_settings_client.gui.show_adv_tracerestrict_features) {
			*hide_mask = 0;
		} else {
			*hide_mask = is_conditional ? 0x38000 : 0x30;
		}
	}
	return is_conditional ? &set_cond : &set_action;
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

	for (size_t i = 0; i < _sorted_standard_cargo_specs_size; ++i) {
		const CargoSpec *cs = _sorted_cargo_specs[i];
		cargo_list_str[i] = cs->name;
		cargo_list_id[i] = cs->Index();
	}
	cargo_list_str[_sorted_standard_cargo_specs_size] = INVALID_STRING_ID;

	return &cargo_list;
}

/**
 * Get a DropDownList of the group list
 */
static DropDownList *GetGroupDropDownList(Owner owner, GroupID group_id, int &selected)
{
	typedef GUIList<const Group*> GUIGroupList;
	extern int CDECL GroupNameSorter(const Group * const *a, const Group * const *b);

	GUIGroupList list;

	const Group *g;
	FOR_ALL_GROUPS(g) {
		if (g->owner == owner && g->vehicle_type == VEH_TRAIN) {
			*list.Append() = g;
		}
	}

	list.ForceResort();
	list.Sort(&GroupNameSorter);

	DropDownList *dlist = new DropDownList();
	selected = -1;

	if (group_id == DEFAULT_GROUP) selected = DEFAULT_GROUP;
	*dlist->Append() = new DropDownListStringItem(STR_GROUP_DEFAULT_TRAINS, DEFAULT_GROUP, false);

	for (size_t i = 0; i < list.Length(); ++i) {
		const Group *g = list[i];
		if (group_id == g->index) selected = group_id;
		DropDownListParamStringItem *item = new DropDownListParamStringItem(STR_GROUP_NAME, g->index, false);
		item->SetParam(0, g->index);
		*dlist->Append() = item;
	}

	return dlist;
}

/** Sort slots by their name */
static int CDECL SlotNameSorter(const TraceRestrictSlot * const *a, const TraceRestrictSlot * const *b)
{
	int r = strnatcmp((*a)->name.c_str(), (*b)->name.c_str()); // Sort by name (natural sorting).
	if (r == 0) return (*a)->index - (*b)->index;
	return r;
}

/**
 * Get a DropDownList of the group list
 */
DropDownList *GetSlotDropDownList(Owner owner, TraceRestrictSlotID slot_id, int &selected)
{
	GUIList<const TraceRestrictSlot*> list;

	const TraceRestrictSlot *slot;
	FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
		if (slot->owner == owner) {
			*list.Append() = slot;
		}
	}

	if (list.Length() == 0) return NULL;

	list.ForceResort();
	list.Sort(&SlotNameSorter);

	DropDownList *dlist = new DropDownList();
	selected = -1;

	for (size_t i = 0; i < list.Length(); ++i) {
		const TraceRestrictSlot *s = list[i];
		if (slot_id == s->index) selected = slot_id;
		DropDownListParamStringItem *item = new DropDownListParamStringItem(STR_TRACE_RESTRICT_SLOT_NAME, s->index, false);
		item->SetParam(0, s->index);
		*dlist->Append() = item;
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

static const StringID _slot_op_cond_ops_str[] = {
	STR_TRACE_RESTRICT_SLOT_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_RELEASE_FRONT,
	STR_TRACE_RESTRICT_SLOT_RELEASE_BACK,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_ACQUIRE_WAIT,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_TRY_ACQUIRE,
	STR_TRACE_RESTRICT_SLOT_PBS_RES_END_RELEASE,
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
};
/** cargo conditional operators dropdown list set */
static const TraceRestrictDropDownListSet _slot_op_cond_ops = {
	_slot_op_cond_ops_str, _slot_op_cond_ops_val,
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
	return GetDropDownStringByValue(GetTypeDropDownListSet(type), type);
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

	switch (properties.cond_type) {
		case TRCOT_NONE:
			return NULL;

		case TRCOT_BINARY:
			return &set_short;

		case TRCOT_ALL:
			return &set_long;
	}
	NOT_REACHED();
	return NULL;
}

/**
 * Return true if item type field @p type is an integer value type
 */
static bool IsIntegerValueType(TraceRestrictValueType type)
{
	switch (type) {
		case TRVT_INT:
		case TRVT_SPEED:
		case TRVT_WEIGHT:
		case TRVT_POWER:
		case TRVT_FORCE:
			return true;

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
					? ConvertSpeedToDisplaySpeed(in) * 10 / 16
					: ConvertDisplaySpeedToSpeed(in) * 16 / 10;

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
					? ConvertForceToDisplayForce(in)
					: ConvertDisplayForceToForce(in);
			break;

		case TRVT_PF_PENALTY:
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
			ConvertForceWeightRatioToDisplay(in, value, decimal);
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
			break;

		case TRVT_FORCE_WEIGHT_RATIO:
			return ConvertDisplayToForceWeightRatio(in);
			break;

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
 * @param prog The program (may be NULL)
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
			switch (properties.value_type) {
				case TRVT_INT:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
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
					if (GetTraceRestrictValue(item) >= TRDTSV_FRONT) {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_SIGNAL_FACE;
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_ENTRY_DIRECTION;
					}
					SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
					SetDParam(1, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
					SetDParam(2, GetDropDownStringByValue(&_direction_value, GetTraceRestrictValue(item)));
					break;

				case TRVT_TILE_INDEX: {
					assert(prog != NULL);
					assert(GetTraceRestrictType(item) == TRIT_COND_PBS_ENTRY_SIGNAL);
					TileIndex tile = *(TraceRestrictProgram::InstructionAt(prog->items, index - 1) + 1);
					if (tile == INVALID_TILE) {
						DrawInstructionStringConditionalInvalidValue(item, properties, instruction_string, selected);
					} else {
						instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_TILE_INDEX;
						SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
						SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_PBS_ENTRY_SIGNAL_LONG);
						SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties), GetTraceRestrictCondOp(item)));
						SetDParam(3, TileX(tile));
						SetDParam(4, TileY(tile));
					}
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
						SetDParam(2, GetTraceRestrictValue(item));
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
						SetDParam(1, GetTypeString(GetTraceRestrictType(item)));
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
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_POWER_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_POWER_WEIGHT_RATIO;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
					break;

				case TRVT_FORCE_WEIGHT_RATIO:
					instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_FORCE_WEIGHT_RATIO;
					DrawInstructionStringConditionalIntegerCommon(item, properties);
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
					assert(prog != NULL);
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
						uint16 index = GetTraceRestrictValue(item);
						assert(index < TRPPPI_END);
						SetDParam(0, _pf_penalty_dropdown_str[index]);
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
				instruction_string = GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_LONG_RESERVE_CANCEL : STR_TRACE_RESTRICT_LONG_RESERVE;
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

			default:
				NOT_REACHED();
				break;
		}
	}

	DrawString(left + indent * 16, right, y, instruction_string, selected ? TC_WHITE : TC_BLACK);
}

/** Main GUI window class */
class TraceRestrictWindow: public Window {
	TileIndex tile;                                                             ///< tile this window is for
	Track track;                                                                ///< track this window is for
	int selected_instruction;                                                   ///< selected instruction index, this is offset by one due to the display of the "start" item
	Scrollbar *vscroll;                                                         ///< scrollbar widget
	std::map<int, const TraceRestrictDropDownListSet *> drop_down_list_mapping; ///< mapping of widget IDs to drop down list sets
	bool value_drop_down_is_company;                                            ///< TR_WIDGET_VALUE_DROPDOWN is a company list
	TraceRestrictItem expecting_inserted_item;                                  ///< set to instruction when performing an instruction insertion, used to handle selection update on insertion
	int current_placement_widget;                                               ///< which widget has a SetObjectToPlaceWnd, if any
	int current_left_aux_plane;                                                 ///< current plane for TR_WIDGET_SEL_TOP_LEFT_AUX widget

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

	virtual void OnClick(Point pt, int widget, int click_count) OVERRIDE
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST: {
				int sel = this->GetItemIndexFromPt(pt.y);

				if (_ctrl_pressed) {
					// scroll to target (for stations, waypoints, depots)

					if (sel == -1) return;

					TraceRestrictItem item = this->GetItem(this->GetProgram(), sel);
					if (GetTraceRestrictTypeProperties(item).value_type == TRVT_ORDER) {
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
					} else if (GetTraceRestrictTypeProperties(item).value_type == TRVT_TILE_INDEX) {
						TileIndex tile = *(TraceRestrictProgram::InstructionAt(this->GetProgram()->items, sel - 1) + 1);
						if (tile != INVALID_TILE) {
							ScrollMainWindowToTile(tile);
						}
					}
					return;
				}

				this->DeleteChildWindows();
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
				if (!_settings_client.gui.show_adv_tracerestrict_features) hidden |= _program_slot_hide_mask | _program_wait_pbs_hide_mask;

				this->ShowDropDownListWithValue(&_program_insert, 0, true, TR_WIDGET_INSERT, disabled, hidden, 0);
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

				this->ShowDropDownListWithValue(&_condflags_dropdown, type, false, TR_WIDGET_CONDFLAGS, disabled, 0, 0);
				break;
			}

			case TR_WIDGET_TYPE_COND:
			case TR_WIDGET_TYPE_NONCOND: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictGuiItemType type = GetItemGuiType(item);

				if (type != TRIT_NULL) {
					uint32 hide_mask = 0;
					const TraceRestrictDropDownListSet *set = GetTypeDropDownListSet(type, &hide_mask);
					this->ShowDropDownListWithValue(set, type, false, widget, 0, hide_mask, 0);
				}
				break;
			}

			case TR_WIDGET_COMPARATOR: {
				TraceRestrictItem item = this->GetSelected();
				const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(GetTraceRestrictTypeProperties(item));
				if (list_set) {
					this->ShowDropDownListWithValue(list_set, GetTraceRestrictCondOp(item), false, TR_WIDGET_COMPARATOR, 0, 0, 0);
				}
				break;
			}

			case TR_WIDGET_SLOT_OP: {
				TraceRestrictItem item = this->GetSelected();
				this->ShowDropDownListWithValue(&_slot_op_cond_ops, GetTraceRestrictCondOp(item), false, TR_WIDGET_SLOT_OP, 0, 0, 0);
				break;
			}

			case TR_WIDGET_VALUE_INT: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsIntegerValueType(type)) {
					SetDParam(0, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
					ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				} else if (type == TRVT_SLOT_INDEX_INT) {
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
					char *saved = _settings_game.locale.digit_group_separator;
					_settings_game.locale.digit_group_separator = const_cast<char*>("");
					ShowQueryString(STR_JUST_DECIMAL, STR_TRACE_RESTRICT_VALUE_CAPTION, 16, this, CS_NUMERAL_DECIMAL, QSF_NONE);
					_settings_game.locale.digit_group_separator = saved;
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				switch (GetTraceRestrictTypeProperties(item).value_type) {
					case TRVT_DENY:
						this->ShowDropDownListWithValue(&_deny_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_CARGO_ID:
						this->ShowDropDownListWithValue(GetSortedCargoTypeDropDownListSet(), GetTraceRestrictValue(item), true, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0); // current cargo is permitted to not be in list
						break;

					case TRVT_DIRECTION:
						this->ShowDropDownListWithValue(&_direction_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_PF_PENALTY:
						this->ShowDropDownListWithValue(&_pf_penalty_dropdown, GetPathfinderPenaltyDropdownIndex(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_RESERVE_THROUGH:
						this->ShowDropDownListWithValue(&_reserve_through_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_LONG_RESERVE:
						this->ShowDropDownListWithValue(&_long_reserve_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_WAIT_AT_PBS:
						this->ShowDropDownListWithValue(&_wait_at_pbs_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
						break;

					case TRVT_GROUP_INDEX: {
						int selected;
						DropDownList *dlist = GetGroupDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected);
						ShowDropDownList(this, dlist, selected, TR_WIDGET_VALUE_DROPDOWN);
						break;
					}

					case TRVT_OWNER:
						this->ShowCompanyDropDownListWithValue(static_cast<CompanyID>(GetTraceRestrictValue(item)), false, TR_WIDGET_VALUE_DROPDOWN);
						break;

					case TRVT_SLOT_INDEX: {
						int selected;
						DropDownList *dlist = GetSlotDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected);
						if (dlist != NULL) ShowDropDownList(this, dlist, selected, TR_WIDGET_VALUE_DROPDOWN);
						break;
					}

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
						DropDownList *dlist = GetSlotDropDownList(this->GetOwner(), GetTraceRestrictValue(item), selected);
						if (dlist != NULL) ShowDropDownList(this, dlist, selected, TR_WIDGET_LEFT_AUX_DROPDOWN);
						break;
					}

					default:
						break;
				}
			}

			case TR_WIDGET_VALUE_DEST: {
				SetObjectToPlaceAction(widget, ANIMCURSOR_PICKSTATION);
				break;
			}

			case TR_WIDGET_VALUE_SIGNAL: {
				SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
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
				SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
				break;

			case TR_WIDGET_UNSHARE: {
				TraceRestrictProgMgmtDoCommandP(tile, track, TRDCT_PROG_UNSHARE, STR_TRACE_RESTRICT_ERROR_CAN_T_UNSHARE_PROGRAM);
				break;
			}
		}
	}

	virtual void OnQueryTextFinished(char *str) OVERRIDE
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
		} else if (type == TRVT_SLOT_INDEX_INT) {
			value = atoi(str);
			TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_DUAL_ITEM, this->selected_instruction - 1, value, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
			return;
		} else {
			return;
		}

		SetTraceRestrictValue(item, value);
		TraceRestrictDoCommandP(tile, track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnDropdownSelect(int widget, int index) OVERRIDE
	{
		TraceRestrictItem item = GetSelected();
		if (item == 0 || index < 0 || this->selected_instruction < 1) {
			return;
		}

		if (widget == TR_WIDGET_VALUE_DROPDOWN || widget == TR_WIDGET_LEFT_AUX_DROPDOWN) {
			TraceRestrictTypePropertySet type = GetTraceRestrictTypeProperties(item);
			if (this->value_drop_down_is_company || type.value_type == TRVT_GROUP_INDEX || type.value_type == TRVT_SLOT_INDEX || type.value_type == TRVT_SLOT_INDEX_INT) {
				// this is a special company drop-down or group/slot-index drop-down
				SetTraceRestrictValue(item, index);
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				return;
			}
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

			case TR_WIDGET_TYPE_COND:
			case TR_WIDGET_TYPE_NONCOND: {
				SetTraceRestrictTypeAndNormalise(item, static_cast<TraceRestrictItemType>(value & 0xFFFF), value >> 16);

				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_COMPARATOR:
			case TR_WIDGET_SLOT_OP: {
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

	virtual void OnPlaceObject(Point pt, TileIndex tile) OVERRIDE
	{
		int widget = this->current_placement_widget;
		this->current_placement_widget = -1;

		this->RaiseButtons();
		ResetObjectToPlace();

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
				OnPlaceObjectSignal(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			case TR_WIDGET_VALUE_DEST:
				OnPlaceObjectDestination(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;

			case TR_WIDGET_VALUE_SIGNAL:
				OnPlaceObjectSignalTileValue(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
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
		if (!IsPlainRailTile(source_tile)) {
			ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
			return;
		}

		TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(source_tile, TRANSPORT_RAIL, 0));
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

		if (!HasTrack(source_tile, source_track)) {
			ShowErrorMessage(error_message, STR_ERROR_THERE_IS_NO_RAILROAD_TRACK, WL_INFO);
			return;
		}

		if (!HasSignalOnTrack(source_tile, source_track)) {
			ShowErrorMessage(error_message, STR_ERROR_THERE_ARE_NO_SIGNALS, WL_INFO);
			return;
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

		if (!IsTileOwner(tile, _local_company)) {
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
		if (GetTraceRestrictTypeProperties(item).value_type != TRVT_TILE_INDEX) return;

		if (!IsTileOwner(tile, _local_company)) {
			ShowErrorMessage(error_message, STR_ERROR_AREA_IS_OWNED_BY_ANOTHER, WL_INFO);
			return;
		}

		if (IsRailDepotTile(tile)) {
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

	virtual void OnPlaceObjectAbort() OVERRIDE
	{
		this->RaiseButtons();
		this->current_placement_widget = -1;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) OVERRIDE
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnResize() OVERRIDE
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, TR_WIDGET_INSTRUCTION_LIST);
	}

	virtual void OnPaint() OVERRIDE
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const OVERRIDE
	{
		if (widget != TR_WIDGET_INSTRUCTION_LIST) return;

		int y = r.top + WD_FRAMERECT_TOP;
		int line_height = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST)->resize_y;
		int scroll_position = this->vscroll->GetPosition();

		// prog may be NULL
		const TraceRestrictProgram *prog = this->GetProgram();

		int count = this->GetItemCount(prog);
		uint indent = 1;
		for(int i = 0; i < count; i++) {
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
				DrawInstructionString(prog, item, i, y, i == this->selected_instruction, this_indent, r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT);
				y += line_height;
			}
		}
	}

	virtual void OnInvalidateData(int data, bool gui_scope) OVERRIDE
	{
		if (gui_scope) {
			this->ReloadProgramme();
		}
	}

	virtual void SetStringParameters(int widget) const OVERRIDE
	{
		switch (widget) {
			case TR_WIDGET_VALUE_INT: {
				SetDParam(0, 0);
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsIntegerValueType(type)) {
					SetDParam(0, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
				} else if (type == TRVT_SLOT_INDEX_INT) {
					SetDParam(0, *(TraceRestrictProgram::InstructionAt(this->GetProgram()->items, this->selected_instruction - 1) + 1));
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
				if (type.value_type == TRVT_SLOT_INDEX_INT) {
					SetDParam(0, GetTraceRestrictValue(item));
				}
				break;
			}
		}
	}

	virtual EventState OnCTRLStateChange() OVERRIDE
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
			return 2 + prog->GetInstructionCount();
		} else {
			return 2;
		}
	}

	/**
	 * Get current program
	 * This may return NULL if no program currently exists
	 */
	const TraceRestrictProgram *GetProgram() const
	{
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(tile, track), false);
	}

	/**
	 * Get instruction at @p index in program @p prog
	 * This correctly handles start/end markers, offsets, etc.
	 * This returns a 0 instruction if out of bounds
	 * @p prog may be NULL
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
		int sel = (y - nwid->pos_y - WD_FRAMERECT_TOP) / nwid->resize_y; // Selected line

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
		this->RaiseWidget(TR_WIDGET_VALUE_INT);
		this->RaiseWidget(TR_WIDGET_VALUE_DECIMAL);
		this->RaiseWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->RaiseWidget(TR_WIDGET_VALUE_DEST);
		this->RaiseWidget(TR_WIDGET_VALUE_SIGNAL);
		this->RaiseWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);

		NWidgetStacked *left_2_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_2);
		NWidgetStacked *left_sel   = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT);
		NWidgetStacked *left_aux_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT_AUX);
		NWidgetStacked *middle_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_RIGHT);
		NWidgetStacked *share_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_SHARE);
		NWidgetStacked *copy_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_COPY);

		this->DisableWidget(TR_WIDGET_TYPE_COND);
		this->DisableWidget(TR_WIDGET_TYPE_NONCOND);
		this->DisableWidget(TR_WIDGET_CONDFLAGS);
		this->DisableWidget(TR_WIDGET_COMPARATOR);
		this->DisableWidget(TR_WIDGET_SLOT_OP);
		this->DisableWidget(TR_WIDGET_VALUE_INT);
		this->DisableWidget(TR_WIDGET_VALUE_DECIMAL);
		this->DisableWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->DisableWidget(TR_WIDGET_VALUE_DEST);
		this->DisableWidget(TR_WIDGET_VALUE_SIGNAL);
		this->DisableWidget(TR_WIDGET_LEFT_AUX_DROPDOWN);

		this->DisableWidget(TR_WIDGET_INSERT);
		this->DisableWidget(TR_WIDGET_REMOVE);
		this->DisableWidget(TR_WIDGET_RESET);
		this->DisableWidget(TR_WIDGET_COPY);
		this->DisableWidget(TR_WIDGET_SHARE);
		this->DisableWidget(TR_WIDGET_UNSHARE);

		this->DisableWidget(TR_WIDGET_BLANK_L2);
		this->DisableWidget(TR_WIDGET_BLANK_L);
		this->DisableWidget(TR_WIDGET_BLANK_M);
		this->DisableWidget(TR_WIDGET_BLANK_R);

		this->DisableWidget(TR_WIDGET_UP_BTN);
		this->DisableWidget(TR_WIDGET_DOWN_BTN);

		this->EnableWidget(TR_WIDGET_COPY_APPEND);

		left_2_sel->SetDisplayedPlane(DPL2_BLANK);
		left_sel->SetDisplayedPlane(DPL_BLANK);
		left_aux_sel->SetDisplayedPlane(SZSP_NONE);
		middle_sel->SetDisplayedPlane(DPM_BLANK);
		right_sel->SetDisplayedPlane(DPR_BLANK);
		share_sel->SetDisplayedPlane(DPS_SHARE);
		copy_sel->SetDisplayedPlane(_ctrl_pressed ? DPC_APPEND : DPC_COPY);

		const TraceRestrictProgram *prog = this->GetProgram();

		this->GetWidget<NWidgetCore>(TR_WIDGET_CAPTION)->widget_data =
				(prog && prog->refcount > 1) ? STR_TRACE_RESTRICT_CAPTION_SHARED : STR_TRACE_RESTRICT_CAPTION;

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

		if (prog && prog->refcount > 1) {
			// program is shared, show and enable unshare button, and reset button
			share_sel->SetDisplayedPlane(DPS_UNSHARE);
			this->EnableWidget(TR_WIDGET_UNSHARE);
			this->EnableWidget(TR_WIDGET_RESET);
		} else if (this->GetItemCount(prog) > 2) {
			// program is non-empty and not shared, enable reset button
			this->EnableWidget(TR_WIDGET_RESET);
		} else {
			// program is empty and not shared, show copy and share buttons
			this->EnableWidget(TR_WIDGET_COPY);
			this->EnableWidget(TR_WIDGET_SHARE);
		}

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
							right_sel->SetDisplayedPlane(DPR_VALUE_SIGNAL);
							this->EnableWidget(TR_WIDGET_VALUE_SIGNAL);
							break;

						case TRVT_PF_PENALTY:
							right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
							this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
							if (GetTraceRestrictAuxField(item) == TRPPAF_VALUE) {
								this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data = STR_BLACK_COMMA;
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
									GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_LONG_RESERVE_CANCEL : STR_TRACE_RESTRICT_LONG_RESERVE;
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

							const TraceRestrictSlot *slot;
							FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
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

							const TraceRestrictSlot *slot;
							FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
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

						default:
							break;
					}
				}

				this->EnableWidget(TR_WIDGET_INSERT);
				this->EnableWidget(TR_WIDGET_REMOVE);
			}
			if (this->IsUpDownBtnUsable(true)) this->EnableWidget(TR_WIDGET_UP_BTN);
			if (this->IsUpDownBtnUsable(false)) this->EnableWidget(TR_WIDGET_DOWN_BTN);
		}

		this->SetDirty();
	}

	/**
	 * Show a drop down list using @p list_set, setting the pre-selected item to the one corresponding to @p value
	 * This asserts if @p value is not in @p list_set, and @p missing_ok is false
	 */
	void ShowDropDownListWithValue(const TraceRestrictDropDownListSet *list_set, uint value, bool missing_ok,
			int button, uint32 disabled_mask, uint32 hidden_mask, uint width)
	{
		this->drop_down_list_mapping[button] = list_set;
		int selected = GetDropDownListIndexByValue(list_set, value, missing_ok);
		if (button == TR_WIDGET_VALUE_DROPDOWN) this->value_drop_down_is_company = false;
		ShowDropDownMenu(this, list_set->string_array, selected, button, disabled_mask, hidden_mask, width);
	}

	/**
	 * Show a drop down list using @p list_set, setting the pre-selected item to the one corresponding to @p value
	 * This asserts if @p value is not in @p list_set, and @p missing_ok is false
	 */
	void ShowCompanyDropDownListWithValue(CompanyID value, bool missing_ok, int button)
	{
		DropDownList *list = new DropDownList();

		Company *c;
		FOR_ALL_COMPANIES(c) {
			*(list->Append()) = MakeCompanyDropDownListItem(c->index);
			if (c->index == value) missing_ok = true;
		}
		*(list->Append()) = new DropDownListStringItem(STR_TRACE_RESTRICT_UNDEFINED_COMPANY, INVALID_COMPANY, false);
		if (INVALID_COMPANY == value) missing_ok = true;

		assert(missing_ok == true);
		assert(button == TR_WIDGET_VALUE_DROPDOWN);
		this->value_drop_down_is_company = true;

		ShowDropDownList(this, list, value, button, 0, true, false);
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

		uint array_offset = TraceRestrictProgram::InstructionOffsetToArrayOffset(items, offset);
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
	 * Run GenericElseInsertionDryRun with an elif instruction
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
														SetDataTip(STR_BLACK_COMMA, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DECIMAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_BLACK_DECIMAL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_VALUE_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DEST), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_TARGET, STR_TRACE_RESTRICT_SELECT_TARGET), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_SIGNAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_SIGNAL, STR_TRACE_RESTRICT_SELECT_SIGNAL), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_R), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, TR_WIDGET_GOTO_SIGNAL), SetMinimalSize(12, 12), SetDataTip(SPR_ARROW_RIGHT, STR_TRACE_RESTRICT_GOTO_SIGNAL_TOOLTIP),
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
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_SHARE),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_SHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SHARE, STR_TRACE_RESTRICT_SHARE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_UNSHARE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_UNSHARE, STR_TRACE_RESTRICT_UNSHARE_TOOLTIP), SetResize(1, 0),
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
	if (BringWindowToFrontById(WC_TRACE_RESTRICT, MakeTraceRestrictRefId(tile, track)) != NULL) {
		return;
	}

	new TraceRestrictWindow(&_program_desc, tile, track);
}

/** Slot GUI widget IDs */
enum TraceRestrictSlotWindowWidgets {
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
	WID_TRSL_LIST_VEHICLE,
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
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalTextLines(1, WD_DROPDOWNTEXT_TOP + WD_DROPDOWNTEXT_BOTTOM), SetFill(1, 0), EndContainer(),
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
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(12, 12), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_TRSL_LIST_VEHICLE), SetMinimalSize(248, 0), SetMatrixDataTip(1, 0, STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP), SetResize(1, 1), SetFill(1, 0), SetScrollbar(WID_TRSL_LIST_VEHICLE_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_TRSL_LIST_VEHICLE_SCROLLBAR),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(1, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), EndContainer(),
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

		this->slots.Clear();

		const TraceRestrictSlot *slot;
		FOR_ALL_TRACE_RESTRICT_SLOTS(slot) {
			if (slot->owner == owner) {
				*(this->slots.Append()) = slot;
			}
		}

		this->slots.ForceResort();
		this->slots.Sort(&SlotNameSorter);
		this->slots.Compact();
		this->slots.RebuildDone();
	}

	/**
	 * Compute tiny_step_height and column_size
	 * @return Total width required for the group list.
	 */
	uint ComputeSlotInfoSize()
	{
		this->column_size[VGC_NAME] = GetStringBoundingBox(STR_GROUP_ALL_TRAINS);
		this->column_size[VGC_NAME].width = max(170u, this->column_size[VGC_NAME].width);
		this->tiny_step_height = this->column_size[VGC_NAME].height;

		SetDParamMaxValue(0, 9999, 3, FS_SMALL);
		SetDParamMaxValue(1, 9999, 3, FS_SMALL);
		this->column_size[VGC_NUMBER] = GetStringBoundingBox(STR_TRACE_RESTRICT_SLOT_MAX_OCCUPANCY);
		this->tiny_step_height = max(this->tiny_step_height, this->column_size[VGC_NUMBER].height);

		this->tiny_step_height += WD_MATRIX_TOP;

		return WD_FRAMERECT_LEFT + 8 +
			this->column_size[VGC_NAME].width + 8 +
			this->column_size[VGC_NUMBER].width + 2 +
			WD_FRAMERECT_RIGHT;
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
			GfxFillRect(left + WD_FRAMERECT_LEFT, y + WD_FRAMERECT_TOP, right - WD_FRAMERECT_RIGHT, y + this->tiny_step_height - WD_FRAMERECT_BOTTOM - WD_MATRIX_TOP, _colour_gradient[COLOUR_GREY][7]);
		}

		/* draw the selected group in white, else we draw it in black */
		TextColour colour = slot_id == this->vli.index ? TC_WHITE : TC_BLACK;
		bool rtl = _current_text_dir == TD_RTL;

		/* draw group name */
		StringID str;
		if (slot_id == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
			str = STR_GROUP_ALL_TRAINS;
		} else {
			SetDParam(0, slot_id);
			str = STR_TRACE_RESTRICT_SLOT_NAME;
		}
		int x = rtl ? right - WD_FRAMERECT_RIGHT - 8 - this->column_size[VGC_NAME].width + 1 : left + WD_FRAMERECT_LEFT + 8;
		DrawString(x, x + this->column_size[VGC_NAME].width - 1, y + (this->tiny_step_height - this->column_size[VGC_NAME].height) / 2, str, colour);

		if (slot_id == ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) return;

		const TraceRestrictSlot *slot = TraceRestrictSlot::Get(slot_id);

		/* draw the number of vehicles of the group */
		x = rtl ? x - 2 - this->column_size[VGC_NUMBER].width : x + 2 + this->column_size[VGC_NAME].width;
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
		this->sorting = &_sorting.train;

		this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		this->slot_sel = INVALID_TRACE_RESTRICT_SLOT_ID;
		this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
		this->slot_set_max_occupancy = false;
		this->slot_over = INVALID_TRACE_RESTRICT_SLOT_ID;

		this->vehicles.SetListing(*this->sorting);
		this->vehicles.ForceRebuild();
		this->vehicles.NeedResort();

		this->BuildVehicleList();
		this->SortVehicleList();

		this->slots.ForceRebuild();
		this->slots.NeedResort();
		this->BuildSlotList(vli.company);

		this->FinishInitNested(window_number);
		this->owner = vli.company;
	}

	~TraceRestrictSlotWindow()
	{
		*this->sorting = this->vehicles.GetListing();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TRSL_LIST_SLOTS: {
				size->width = this->ComputeSlotInfoSize();
				resize->height = this->tiny_step_height;

				/* Minimum height is the height of the list widget minus all vehicles... */
				size->height =  4 * GetVehicleListHeight(this->vli.vtype, this->tiny_step_height) - this->tiny_step_height;

				/* ... minus the buttons at the bottom ... */
				uint max_icon_height = GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_CREATE_SLOT)->widget_data).height;
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_DELETE_SLOT)->widget_data).height);
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_RENAME_SLOT)->widget_data).height);
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_TRSL_SET_SLOT_MAX_OCCUPANCY)->widget_data).height);

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
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->vehicles.ForceRebuild();
			this->slots.ForceRebuild();
		} else {
			this->vehicles.ForceResort();
			this->slots.ForceResort();
		}

		/* Process ID-invalidation in command-scope as well */
		if (this->slot_rename != INVALID_TRACE_RESTRICT_SLOT_ID && this->slot_rename != NEW_TRACE_RESTRICT_SLOT_ID &&
				!TraceRestrictSlot::IsValidID(this->slot_rename)) {
			DeleteWindowByClass(WC_QUERY_STRING);
			this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
		}

		if (this->vli.index != ALL_TRAINS_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(this->vli.index)) {
			this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
		}

		if (gui_scope) this->CheckCargoFilterEnableState(WID_TRSL_FILTER_BY_CARGO_SEL, true);

		this->SetDirty();
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_TRSL_FILTER_BY_CARGO:
				SetDParam(0, this->cargo_filter_texts[this->cargo_filter_criteria]);
				break;
		}
	}

	virtual void OnPaint()
	{
		/* If we select the all vehicles, this->list will contain all vehicles of the owner
		 * else this->list will contain all vehicles which belong to the selected group */
		this->BuildVehicleList();
		this->SortVehicleList();

		this->BuildSlotList(this->owner);

		this->slot_sb->SetCount(this->slots.Length());
		this->vscroll->SetCount(this->vehicles.Length());

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
		this->GetWidget<NWidgetCore>(WID_TRSL_SORT_BY_DROPDOWN)->widget_data = this->vehicle_sorter_names[this->vehicles.SortType()];

		this->GetWidget<NWidgetCore>(WID_TRSL_FILTER_BY_CARGO)->widget_data = this->cargo_filter_texts[this->cargo_filter_criteria];

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES:
				DrawSlotInfo(r.top + WD_FRAMERECT_TOP, r.left, r.right, ALL_TRAINS_TRACE_RESTRICT_SLOT_ID);
				break;

			case WID_TRSL_LIST_SLOTS: {
				int y1 = r.top + WD_FRAMERECT_TOP;
				int max = min(this->slot_sb->GetPosition() + this->slot_sb->GetCapacity(), this->slots.Length());
				for (int i = this->slot_sb->GetPosition(); i < max; ++i) {
					const TraceRestrictSlot *slot = this->slots[i];

					assert(slot->owner == this->owner);

					DrawSlotInfo(y1, r.left, r.right, slot->index);

					y1 += this->tiny_step_height;
				}
				break;
			}

			case WID_TRSL_SORT_BY_ORDER:
				this->DrawSortButtonState(WID_TRSL_SORT_BY_ORDER, this->vehicles.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
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

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_ORDER: // Flip sorting method ascending/descending
				this->vehicles.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_TRSL_SORT_BY_DROPDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->vehicle_sorter_names, this->vehicles.SortType(),  WID_TRSL_SORT_BY_DROPDOWN, 0, 0);
				return;

			case WID_TRSL_FILTER_BY_CARGO: // Cargo filter dropdown
				ShowDropDownMenu(this, this->cargo_filter_texts, this->cargo_filter_criteria, WID_TRSL_FILTER_BY_CARGO, 0, 0);
				break;

			case WID_TRSL_ALL_VEHICLES: // All vehicles button
				if (this->vli.index != ALL_TRAINS_TRACE_RESTRICT_SLOT_ID) {
					this->vli.index = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
					this->slot_sel = INVALID_TRACE_RESTRICT_SLOT_ID;
					this->vehicles.ForceRebuild();
					this->SetDirty();
				}
				break;

			case WID_TRSL_LIST_SLOTS: { // Matrix Slot
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0, this->tiny_step_height);
				if (id_s >= this->slots.Length()) return;

				this->slot_sel = this->vli.index = this->slots[id_s]->index;

				this->vehicles.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_TRSL_LIST_VEHICLE: { // Matrix Vehicle
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_VEHICLE);
				if (id_v >= this->vehicles.Length()) return; // click out of list bound

				const Vehicle *v = this->vehicles[id_v];
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

				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0, this->tiny_step_height);
				if (id_s >= this->slots.Length()) return; // click out of list bound

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
				if (id_v >= this->vehicles.Length()) return; // click out of list bound

				const Vehicle *v = this->vehicles[id_v];
				if (!VehicleClicked(v) && vindex == v->index) {
					ShowVehicleViewWindow(v);
				}
				break;
			}
		}
	}

	virtual void OnDragDrop(Point pt, int widget)
	{
		if (this->vehicle_sel != INVALID_VEHICLE) OnDragDrop_Vehicle(pt, widget);

		_cursor.vehchain = false;
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str != NULL) {
			if (this->slot_set_max_occupancy) {
				if (!StrEmpty(str)) DoCommandP(0, this->slot_rename | (1 << 16), atoi(str), CMD_ALTER_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_SET_MAX_OCCUPANCY));
			} else if (this->slot_rename == NEW_TRACE_RESTRICT_SLOT_ID) {
				DoCommandP(0, 0, 0, CMD_CREATE_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_CREATE), NULL, str);
			} else {
				DoCommandP(0, this->slot_rename, 0, CMD_ALTER_TRACERESTRICT_SLOT | CMD_MSG(STR_TRACE_RESTRICT_ERROR_SLOT_CAN_T_RENAME), NULL, str);
			}
		}
		this->slot_rename = INVALID_TRACE_RESTRICT_SLOT_ID;
	}

	virtual void OnResize()
	{
		this->slot_sb->SetCapacityFromWidget(this, WID_TRSL_LIST_SLOTS);
		this->vscroll->SetCapacityFromWidget(this, WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case WID_TRSL_SORT_BY_DROPDOWN:
				this->vehicles.SetSortType(index);
				break;

			case WID_TRSL_FILTER_BY_CARGO: // Select a cargo filter criteria
				this->SetCargoFilterIndex(index);
				break;

			default: NOT_REACHED();
		}

		this->SetDirty();
	}

	virtual void OnTick()
	{
		if (_pause_mode != PM_UNPAUSED) return;
		if (this->slots.NeedResort() || this->vehicles.NeedResort()) {
			this->SetDirty();
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		/* abort drag & drop */
		this->vehicle_sel = INVALID_VEHICLE;
		this->DirtyHighlightedSlotWidget();
		this->slot_over = INVALID_GROUP;
		this->SetWidgetDirty(WID_TRSL_LIST_VEHICLE);
	}

	virtual void OnMouseDrag(Point pt, int widget)
	{
		if (this->vehicle_sel == INVALID_VEHICLE) return;

		/* A vehicle is dragged over... */
		TraceRestrictSlotID new_slot_over = INVALID_TRACE_RESTRICT_SLOT_ID;
		switch (widget) {
			case WID_TRSL_ALL_VEHICLES: // ... all trains.
				new_slot_over = ALL_TRAINS_TRACE_RESTRICT_SLOT_ID;
				break;

			case WID_TRSL_LIST_SLOTS: { // ... the list of slots.
				uint id_s = this->slot_sb->GetScrolledRowFromWidget(pt.y, this, WID_TRSL_LIST_SLOTS, 0, this->tiny_step_height);
				if (id_s < this->slots.Length()) new_slot_over = this->slots[id_s]->index;
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
void ShowTraceRestrictSlotWindow(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	WindowNumber num = VehicleListIdentifier(VL_SLOT_LIST, VEH_TRAIN, company).Pack();
	AllocateWindowDescFront<TraceRestrictSlotWindow>(&_slot_window_desc, num);
}

/**
 * Finds a group list window determined by vehicle type and owner
 * @param vt vehicle type
 * @param owner owner of groups
 * @return pointer to VehicleGroupWindow, NULL if not found
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
	if (w != NULL) w->UnselectVehicle(v->index);
}
