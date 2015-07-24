/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_gui.cpp GUI related to signal tracerestrict */

#include "stdafx.h"
#include "tracerestrict.h"
#include "command_func.h"
#include "window_func.h"
#include "strings_func.h"
#include "string_func.h"
#include "viewport_func.h"
#include "textbuf_gui.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "widgets/dropdown_func.h"
#include "gui.h"
#include "gfx_func.h"
#include "rail_map.h"
#include "depot_map.h"
#include "tile_cmd.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "depot_base.h"
#include "error.h"
#include "table/sprites.h"

extern uint ConvertSpeedToDisplaySpeed(uint speed);
extern uint ConvertDisplaySpeedToSpeed(uint speed);

enum TraceRestrictWindowWidgets {
	TR_WIDGET_CAPTION,
	TR_WIDGET_INSTRUCTION_LIST,
	TR_WIDGET_SCROLLBAR,

	TR_WIDGET_SEL_TOP_LEFT,
	TR_WIDGET_SEL_TOP_MIDDLE,
	TR_WIDGET_SEL_TOP_RIGHT,
	TR_WIDGET_SEL_SHARE,

	TR_WIDGET_TYPE,
	TR_WIDGET_COMPARATOR,
	TR_WIDGET_VALUE_INT,
	TR_WIDGET_VALUE_DROPDOWN,
	TR_WIDGET_VALUE_DEST,

	TR_WIDGET_BLANK_L,
	TR_WIDGET_BLANK_M,
	TR_WIDGET_BLANK_R,

	TR_WIDGET_GOTO_SIGNAL,
	TR_WIDGET_INSERT,
	TR_WIDGET_REMOVE,
	TR_WIDGET_RESET,
	TR_WIDGET_COPY,
	TR_WIDGET_SHARE,
	TR_WIDGET_UNSHARE,
};

enum PanelWidgets {
	// Left
	DPL_TYPE = 0,
	DPL_BLANK,

	// Middle
	DPM_COMPARATOR = 0,
	DPM_BLANK,

	// Right
	DPR_VALUE_INT = 0,
	DPR_VALUE_DROPDOWN,
	DPR_VALUE_DEST,
	DPR_BLANK,

	// Share
	DPS_SHARE = 0,
	DPS_UNSHARE,
};

/// value_array *must* be at least as long as string_array,
/// where the length of string_array is defined as the offset
/// of the first INVALID_STRING_ID
struct TraceRestrictDropDownListSet {
	const StringID *string_array;
	const uint *value_array;
};

static const StringID _program_insert_str[] = {
	STR_TRACE_RESTRICT_CONDITIONAL_IF,
	STR_TRACE_RESTRICT_PF_DENY,
	STR_TRACE_RESTRICT_PF_PENALTY,
	INVALID_STRING_ID
};
static const uint _program_insert_val[] = {
	TRIT_COND_UNDEFINED,
	TRIT_PF_DENY,
	TRIT_PF_PENALTY,
};

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

static const TraceRestrictDropDownListSet _deny_value = {
	_deny_value_str, _deny_value_val,
};

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

static StringID GetDropDownStringByValue(const TraceRestrictDropDownListSet *list_set, uint value)
{
	return list_set->string_array[GetDropDownListIndexByValue(list_set, value, false)];
}

static const TraceRestrictDropDownListSet *GetTypeDropDownListSet(TraceRestrictItemType type)
{
	static const StringID str_action[] = {
		STR_TRACE_RESTRICT_PF_DENY,
		STR_TRACE_RESTRICT_PF_PENALTY,
		INVALID_STRING_ID,
	};
	static const uint val_action[] = {
		TRIT_PF_DENY,
		TRIT_PF_PENALTY,
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
		STR_TRACE_RESTRICT_VARIABLE_UNDEFINED,
		INVALID_STRING_ID,
	};
	static const uint val_cond[] = {
		TRIT_COND_TRAIN_LENGTH,
		TRIT_COND_MAX_SPEED,
		TRIT_COND_CURRENT_ORDER,
		TRIT_COND_NEXT_ORDER,
		TRIT_COND_LAST_STATION,
		TRIT_COND_UNDEFINED,
	};
	static const TraceRestrictDropDownListSet set_cond = {
		str_cond, val_cond,
	};

	return IsTraceRestrictTypeConditional(type) ? &set_cond : &set_action;
}

static StringID GetTypeString(TraceRestrictItemType type)
{
	return GetDropDownStringByValue(GetTypeDropDownListSet(type), type);
}

static const TraceRestrictDropDownListSet *GetCondOpDropDownListSet(TraceRestrictConditionOpType type)
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

	switch (type) {
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

static bool IsIntegerValueType(TraceRestrictValueType type)
{
	switch (type) {
		case TRVT_INT:
		case TRVT_SPEED:
			return true;

		default:
			return false;
	}
}

static uint ConvertIntegerValue(TraceRestrictValueType type, uint in, bool to_display)
{
	switch (type) {
		case TRVT_INT:
			return in;

		case TRVT_SPEED:
			return to_display
					? ConvertSpeedToDisplaySpeed(in) * 10 / 16
					: ConvertDisplaySpeedToSpeed(in) * 16 / 10;

		default:
			NOT_REACHED();
			return 0;
	}
}

static const StringID _program_cond_type[] = {
	/* 0          */          STR_TRACE_RESTRICT_CONDITIONAL_IF,
	/* TRCF_ELSE  */          STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	/* TRCF_OR    */          STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
};

static void DrawInstructionStringConditionalCommon(TraceRestrictItem item, const TraceRestrictTypePropertySet &properties)
{
	assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
	SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
	SetDParam(1, GetTypeString(GetTraceRestrictType(item)));
	SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties.cond_type), GetTraceRestrictCondOp(item)));
}

static void DrawInstructionStringConditionalIntegerCommon(TraceRestrictItem item, const TraceRestrictTypePropertySet &properties)
{
	DrawInstructionStringConditionalCommon(item, properties);
	SetDParam(3, GetTraceRestrictValue(item));
}

/**
 * Draws an instruction in the programming GUI
 * @param instruction The instruction to draw
 * @param y Y position for drawing
 * @param selected True, if the order is selected
 * @param indent How many levels the instruction is indented
 * @param left Left border for text drawing
 * @param right Right border for text drawing
 */
static void DrawInstructionString(TraceRestrictItem item, int y, bool selected, int indent, int left, int right)
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
								instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_UNDEFINED;
								DrawInstructionStringConditionalCommon(item, properties);
								SetDParam(3, selected ? STR_TRACE_RESTRICT_WHITE : STR_EMPTY);
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
				instruction_string = STR_TRACE_RESTRICT_PF_PENALTY_ITEM;
				SetDParam(0, GetTraceRestrictValue(item));
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

	DrawString(left + indent * 16, right, y, instruction_string, selected ? TC_WHITE : TC_BLACK);
}

class TraceRestrictWindow: public Window {
	TileIndex tile;
	Track track;
	int selected_instruction; // NB: this is offset by one due to the display of the "start" item
	Scrollbar *vscroll;
	std::map<int, const TraceRestrictDropDownListSet *> drop_down_list_mapping;
	TraceRestrictItem expecting_inserted_item;
	int current_placement_widget;

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
		this->FinishInitNested(MakeTraceRestrictRefId(tile, track));

		this->ReloadProgramme();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST: {
				int sel = this->GetInstructionFromPt(pt.y);

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
				this->ShowDropDownListWithValue(&_program_insert, 0, true, TR_WIDGET_INSERT, 0, 0, 0);
				break;
			}

			case TR_WIDGET_REMOVE: {
				TraceRestrictItem item = this->GetSelected();
				if (this->GetOwner() != _local_company || item == 0) {
					return;
				}

				TraceRestrictDoCommandP(tile, track, TRDCT_REMOVE_ITEM, this->selected_instruction - 1, 0, STR_TRACE_RESTRICT_ERROR_CAN_T_REMOVE_ITEM);
				break;
			}

			case TR_WIDGET_TYPE: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictItemType type = GetTraceRestrictType(item);

				if (type != TRIT_NULL) {
					this->ShowDropDownListWithValue(GetTypeDropDownListSet(type), type, false, TR_WIDGET_TYPE, 0, 0, 0);
				}
				break;
			}

			case TR_WIDGET_COMPARATOR: {
				TraceRestrictItem item = this->GetSelected();
				const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(GetTraceRestrictTypeProperties(item).cond_type);
				if (list_set) {
					this->ShowDropDownListWithValue(list_set, GetTraceRestrictCondOp(item), false, TR_WIDGET_COMPARATOR, 0, 0, 0);
				}
				break;
			}

			case TR_WIDGET_VALUE_INT: {
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsIntegerValueType(type)) {
					SetDParam(0, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
					ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				}
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				TraceRestrictItem item = this->GetSelected();
				if (GetTraceRestrictTypeProperties(item).value_type == TRVT_DENY) {
					this->ShowDropDownListWithValue(&_deny_value, GetTraceRestrictValue(item), false, TR_WIDGET_VALUE_DROPDOWN, 0, 0, 0);
				}
				break;
			}

			case TR_WIDGET_VALUE_DEST: {
				SetObjectToPlaceAction(widget, ANIMCURSOR_PICKSTATION);
				break;
			}

			case TR_WIDGET_GOTO_SIGNAL:
				ScrollMainWindowToTile(this->tile);
				break;

			case TR_WIDGET_RESET: {
				TraceRestrictProgMgmtDoCommandP(tile, track, TRDCT_PROG_RESET, STR_TRACE_RESTRICT_ERROR_CAN_T_RESET_SIGNAL);
				break;
			}

			case TR_WIDGET_COPY:
			case TR_WIDGET_SHARE:
				SetObjectToPlaceAction(widget, ANIMCURSOR_BUILDSIGNALS);
				break;

			case TR_WIDGET_UNSHARE: {
				TraceRestrictProgMgmtDoCommandP(tile, track, TRDCT_PROG_UNSHARE, STR_TRACE_RESTRICT_ERROR_CAN_T_UNSHARE_PROGRAM);
				break;
			}
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (StrEmpty(str)) {
			return;
		}

		TraceRestrictItem item = GetSelected();
		TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
		if (!IsIntegerValueType(type)) {
			return;
		}

		uint value = ConvertIntegerValue(type, atoi(str), false);
		if (value >= (1 << TRIFA_VALUE_COUNT)) {
			SetDParam(0, ConvertIntegerValue(type, (1 << TRIFA_VALUE_COUNT) - 1, true));
			ShowErrorMessage(STR_TRACE_RESTRICT_ERROR_VALUE_TOO_LARGE, STR_EMPTY, WL_INFO);
			return;
		}

		SetTraceRestrictValue(item, value);
		TraceRestrictDoCommandP(tile, track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		TraceRestrictItem item = GetSelected();
		if (item == 0 || index < 0 || this->selected_instruction < 1) {
			return;
		}

		const TraceRestrictDropDownListSet *list_set = this->drop_down_list_mapping[widget];
		if (!list_set) {
			return;
		}

		uint value = list_set->value_array[index];

		switch (widget) {
			case TR_WIDGET_INSERT: {
				TraceRestrictItem insert_item = 0;
				SetTraceRestrictTypeAndNormalise(insert_item, static_cast<TraceRestrictItemType>(value));
				this->expecting_inserted_item = insert_item;
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_INSERT_ITEM, this->selected_instruction - 1, insert_item, STR_TRACE_RESTRICT_ERROR_CAN_T_INSERT_ITEM);
				break;
			}

			case TR_WIDGET_TYPE: {
				SetTraceRestrictTypeAndNormalise(item, static_cast<TraceRestrictItemType>(value));
				if (GetTraceRestrictType(item) == TRIT_COND_LAST_STATION && GetTraceRestrictAuxField(item) != TROCAF_STATION) {
					// if changing type from another order type to last visited station, reset value if not currently a station
					SetTraceRestrictValueDefault(item, TRVT_ORDER);
				}
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_COMPARATOR: {
				SetTraceRestrictCondOp(item, static_cast<TraceRestrictCondOp>(value));
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}

			case TR_WIDGET_VALUE_DROPDOWN: {
				SetTraceRestrictValue(item, value);
				TraceRestrictDoCommandP(this->tile, this->track, TRDCT_MODIFY_ITEM, this->selected_instruction - 1, item, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;
			}
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
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

			case TR_WIDGET_SHARE:
				OnPlaceObjectSignal(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			case TR_WIDGET_VALUE_DEST:
				OnPlaceObjectDestination(pt, tile, widget, STR_TRACE_RESTRICT_ERROR_CAN_T_MODIFY_ITEM);
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

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

			case TR_WIDGET_SHARE:
				TraceRestrictProgMgmtWithSourceDoCommandP(this->tile, this->track, TRDCT_PROG_SHARE,
						source_tile, source_track, STR_TRACE_RESTRICT_ERROR_CAN_T_SHARE_PROGRAM);
				break;

			default:
				NOT_REACHED();
				break;
		}
	}

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

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->current_placement_widget = -1;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case TR_WIDGET_INSTRUCTION_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnResize()
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, TR_WIDGET_INSTRUCTION_LIST);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
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
				DrawInstructionString(item, y, i == this->selected_instruction, this_indent, r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT);
				y += line_height;
			}
		}
	}

	virtual void OnInvalidateData(int data, bool gui_scope) {
		if (gui_scope) {
			this->ReloadProgramme();
		}
	}


	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case TR_WIDGET_VALUE_INT: {
				SetDParam(0, 0);
				TraceRestrictItem item = this->GetSelected();
				TraceRestrictValueType type = GetTraceRestrictTypeProperties(item).value_type;
				if (IsIntegerValueType(type)) {
					SetDParam(0, ConvertIntegerValue(type, GetTraceRestrictValue(item), true));
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
		}
	}

private:
	TraceRestrictItem MakeSpecialItem(TraceRestictNullTypeSpecialValue value) const
	{
		TraceRestrictItem item = 0;
		SetTraceRestrictType(item, TRIT_NULL);
		SetTraceRestrictValue(item, value);
		return item;
	}

	int GetItemCount(const TraceRestrictProgram *prog) const
	{
		if (prog) {
			return 2 + prog->items.size();
		} else {
			return 2;
		}
	}

	/// This may return NULL if no program currently exists
	const TraceRestrictProgram *GetProgram() const
	{
		return GetTraceRestrictProgram(MakeTraceRestrictRefId(tile, track), false);
	}

	/// prog may be NULL
	TraceRestrictItem GetItem(const TraceRestrictProgram *prog, int index) const
	{
		if (index < 0) {
			return 0;
		}

		if (index == 0) {
			return MakeSpecialItem(TRNTSV_START);
		}

		if (prog) {
			const std::vector<TraceRestrictItem> &items = prog->items;

			if (static_cast<size_t>(index) == items.size() + 1) {
				return MakeSpecialItem(TRNTSV_END);
			}

			if (static_cast<size_t>(index) > items.size() + 1) {
				return 0;
			}

			return items[index - 1];
		} else {
			// No program defined, this is equivalent to an empty program
			if (index == 1) {
				return MakeSpecialItem(TRNTSV_END);
			} else {
				return 0;
			}
		}
	}

	TraceRestrictItem GetSelected() const
	{
		return this->GetItem(this->GetProgram(), this->selected_instruction);
	}

	Owner GetOwner()
	{
		return GetTileOwner(tile);
	}

	int GetInstructionFromPt(int y)
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(TR_WIDGET_INSTRUCTION_LIST);
		int sel = (y - nwid->pos_y - WD_FRAMERECT_TOP) / nwid->resize_y; // Selected line

		if ((uint)sel >= this->vscroll->GetCapacity()) return -1;

		sel += this->vscroll->GetPosition();

		return (sel < this->GetItemCount(this->GetProgram()) && sel >= 0) ? sel : -1;
	}

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

	void UpdateButtonState()
	{
		this->RaiseWidget(TR_WIDGET_INSERT);
		this->RaiseWidget(TR_WIDGET_REMOVE);
		this->RaiseWidget(TR_WIDGET_TYPE);
		this->RaiseWidget(TR_WIDGET_COMPARATOR);
		this->RaiseWidget(TR_WIDGET_VALUE_INT);
		this->RaiseWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->RaiseWidget(TR_WIDGET_VALUE_DEST);

		NWidgetStacked *left_sel   = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT);
		NWidgetStacked *middle_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_RIGHT);
		NWidgetStacked *share_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_SHARE);

		this->DisableWidget(TR_WIDGET_TYPE);
		this->DisableWidget(TR_WIDGET_COMPARATOR);
		this->DisableWidget(TR_WIDGET_VALUE_INT);
		this->DisableWidget(TR_WIDGET_VALUE_DROPDOWN);
		this->DisableWidget(TR_WIDGET_VALUE_DEST);

		this->DisableWidget(TR_WIDGET_INSERT);
		this->DisableWidget(TR_WIDGET_REMOVE);
		this->DisableWidget(TR_WIDGET_RESET);
		this->DisableWidget(TR_WIDGET_COPY);
		this->DisableWidget(TR_WIDGET_SHARE);
		this->DisableWidget(TR_WIDGET_UNSHARE);

		this->DisableWidget(TR_WIDGET_BLANK_L);
		this->DisableWidget(TR_WIDGET_BLANK_M);
		this->DisableWidget(TR_WIDGET_BLANK_R);

		left_sel->SetDisplayedPlane(DPL_BLANK);
		middle_sel->SetDisplayedPlane(DPM_BLANK);
		right_sel->SetDisplayedPlane(DPR_BLANK);
		share_sel->SetDisplayedPlane(DPS_SHARE);

		const TraceRestrictProgram *prog = this->GetProgram();

		this->GetWidget<NWidgetCore>(TR_WIDGET_CAPTION)->widget_data =
				(prog && prog->refcount > 1) ? STR_TRACE_RESTRICT_CAPTION_SHARED : STR_TRACE_RESTRICT_CAPTION;

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
					// this is not an end if, enable removing
					this->EnableWidget(TR_WIDGET_REMOVE);
				}
			} else {
				TraceRestrictTypePropertySet properties = GetTraceRestrictTypeProperties(item);

				left_sel->SetDisplayedPlane(DPL_TYPE);
				this->EnableWidget(TR_WIDGET_TYPE);

				this->GetWidget<NWidgetCore>(TR_WIDGET_TYPE)->widget_data =
						GetTypeString(GetTraceRestrictType(item));

				if (properties.cond_type == TRCOT_BINARY || properties.cond_type == TRCOT_ALL) {
					middle_sel->SetDisplayedPlane(DPM_COMPARATOR);
					this->EnableWidget(TR_WIDGET_COMPARATOR);

					const TraceRestrictDropDownListSet *list_set = GetCondOpDropDownListSet(properties.cond_type);

					if (list_set) {
						this->GetWidget<NWidgetCore>(TR_WIDGET_COMPARATOR)->widget_data =
								GetDropDownStringByValue(list_set, GetTraceRestrictCondOp(item));
					}
				}

				if (IsIntegerValueType(properties.value_type)) {
					right_sel->SetDisplayedPlane(DPR_VALUE_INT);
					this->EnableWidget(TR_WIDGET_VALUE_INT);
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

						default:
							break;
					}
				}

				this->EnableWidget(TR_WIDGET_INSERT);
				this->EnableWidget(TR_WIDGET_REMOVE);
			}
		}

		this->SetDirty();
	}

	void ShowDropDownListWithValue(const TraceRestrictDropDownListSet *list_set, uint value, bool missing_ok,
			int button, uint32 disabled_mask, uint32 hidden_mask, uint width)
	{
		drop_down_list_mapping[button] = list_set;
		int selected = GetDropDownListIndexByValue(list_set, value, missing_ok);
		ShowDropDownMenu(this, list_set->string_array, selected, button, disabled_mask, hidden_mask, width);
	}

	void SetObjectToPlaceAction(int widget, CursorID cursor)
	{
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
		NWidget(WWT_PANEL, COLOUR_GREY, TR_WIDGET_INSTRUCTION_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_TRACE_RESTRICT_INSTRUCTION_LIST_TOOLTIP), SetResize(1, 1), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TR_WIDGET_SCROLLBAR),
	EndContainer(),

	// Button Bar
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_LEFT),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_TYPE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_TYPE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_L), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_MIDDLE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_M), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, TR_WIDGET_SEL_TOP_RIGHT),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_INT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_BLACK_COMMA, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, TR_WIDGET_VALUE_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_VALUE_DEST), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_SELECT_TARGET, STR_TRACE_RESTRICT_SELECT_TARGET), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_BLANK_R), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_EMPTY, STR_NULL), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_GREY, TR_WIDGET_GOTO_SIGNAL), SetMinimalSize(12, 12), SetDataTip(SPR_ARROW_RIGHT, STR_TRACE_RESTRICT_GOTO_SIGNAL_TOOLTIP),
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
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_COPY), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_COPY, STR_TRACE_RESTRICT_COPY_TOOLTIP), SetResize(1, 0),
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

void ShowTraceRestrictProgramWindow(TileIndex tile, Track track)
{
	if (BringWindowToFrontById(WC_TRACE_RESTRICT, MakeTraceRestrictRefId(tile, track)) != NULL) {
		return;
	}

	new TraceRestrictWindow(&_program_desc, tile, track);
}
