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
#include "widgets/dropdown_func.h"
#include "gui.h"
#include "gfx_func.h"
#include "rail_map.h"
#include "tile_cmd.h"
#include "error.h"
#include "table/sprites.h"

enum TraceRestrictWindowWidgets {
	TR_WIDGET_CAPTION,
	TR_WIDGET_INSTRUCTION_LIST,
	TR_WIDGET_SCROLLBAR,

	TR_WIDGET_SEL_TOP_LEFT,
	TR_WIDGET_SEL_TOP_MIDDLE,
	TR_WIDGET_SEL_TOP_RIGHT,

	TR_WIDGET_TYPE,
	TR_WIDGET_COMPARATOR,
	TR_WIDGET_VALUE_INT,
	TR_WIDGET_VALUE_DROPDOWN,

	TR_WIDGET_BLANK_L,
	TR_WIDGET_BLANK_M,
	TR_WIDGET_BLANK_R,

	TR_WIDGET_GOTO_SIGNAL,
	TR_WIDGET_INSERT,
	TR_WIDGET_REMOVE,
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
	DPR_BLANK,
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
		STR_TRACE_RESTRICT_VARIABLE_UNDEFINED,
		INVALID_STRING_ID,
	};
	static const uint val_cond[] = {
		TRIT_COND_TRAIN_LENGTH,
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

static const StringID _program_cond_type[] = {
	/* 0          */          STR_TRACE_RESTRICT_CONDITIONAL_IF,
	/* TRCF_ELSE  */          STR_TRACE_RESTRICT_CONDITIONAL_ELIF,
	/* TRCF_OR    */          STR_TRACE_RESTRICT_CONDITIONAL_ORIF,
};

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
		} else if (properties.value_type == TRVT_INT) {
			instruction_string = STR_TRACE_RESTRICT_CONDITIONAL_COMPARE_INTEGER;

			assert(GetTraceRestrictCondFlags(item) <= TRCF_OR);
			SetDParam(0, _program_cond_type[GetTraceRestrictCondFlags(item)]);
			SetDParam(1, GetTypeString(GetTraceRestrictType(item)));
			SetDParam(2, GetDropDownStringByValue(GetCondOpDropDownListSet(properties.cond_type), GetTraceRestrictCondOp(item)));
			SetDParam(3, GetTraceRestrictValue(item));
		} else {
			NOT_REACHED();
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

public:
	TraceRestrictWindow(WindowDesc *desc, TileIndex tile, Track track)
			: Window(desc)
	{
		this->tile = tile;
		this->track = track;
		this->selected_instruction = -1;
		this->expecting_inserted_item = static_cast<TraceRestrictItem>(0);

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
				if (GetTraceRestrictTypeProperties(item).value_type == TRVT_INT) {
					SetDParam(0, GetTraceRestrictValue(item));
					ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 6, this, CS_NUMERAL, QSF_NONE); // 5 digit num, + terminating null
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

			case TR_WIDGET_GOTO_SIGNAL:
				ScrollMainWindowToTile(this->tile);
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (StrEmpty(str)) {
			return;
		}

		TraceRestrictItem item = GetSelected();
		if (GetTraceRestrictTypeProperties(item).value_type != TRVT_INT) {
			return;
		}

		uint value = atoi(str);
		if (value >= (1 << TRIFA_VALUE_COUNT)) {
			SetDParam(0, (1 << TRIFA_VALUE_COUNT) - 1);
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
				if (GetTraceRestrictTypeProperties(item).value_type == TRVT_INT) {
					SetDParam(0, GetTraceRestrictValue(item));
				}
			} break;
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

		NWidgetStacked *left_sel   = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_LEFT);
		NWidgetStacked *middle_sel = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel  = this->GetWidget<NWidgetStacked>(TR_WIDGET_SEL_TOP_RIGHT);

		this->DisableWidget(TR_WIDGET_TYPE);
		this->DisableWidget(TR_WIDGET_COMPARATOR);
		this->DisableWidget(TR_WIDGET_VALUE_INT);
		this->DisableWidget(TR_WIDGET_VALUE_DROPDOWN);

		this->DisableWidget(TR_WIDGET_INSERT);
		this->DisableWidget(TR_WIDGET_REMOVE);

		this->DisableWidget(TR_WIDGET_BLANK_L);
		this->DisableWidget(TR_WIDGET_BLANK_M);
		this->DisableWidget(TR_WIDGET_BLANK_R);

		left_sel->SetDisplayedPlane(DPL_BLANK);
		middle_sel->SetDisplayedPlane(DPM_BLANK);
		right_sel->SetDisplayedPlane(DPR_BLANK);

		// Don't allow modifications if don't own, or have selected invalid instruction
		if (this->GetOwner() != _local_company || this->selected_instruction < 1) {
			this->SetDirty();
			return;
		}

		TraceRestrictItem item = this->GetSelected();
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

				if (properties.value_type == TRVT_INT) {
					right_sel->SetDisplayedPlane(DPR_VALUE_INT);
					this->EnableWidget(TR_WIDGET_VALUE_INT);
				} else if (properties.value_type == TRVT_DENY) {
					right_sel->SetDisplayedPlane(DPR_VALUE_DROPDOWN);
					this->EnableWidget(TR_WIDGET_VALUE_DROPDOWN);
					this->GetWidget<NWidgetCore>(TR_WIDGET_VALUE_DROPDOWN)->widget_data =
							GetTraceRestrictValue(item) ? STR_TRACE_RESTRICT_PF_ALLOW : STR_TRACE_RESTRICT_PF_DENY;
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
		NWidget(WWT_PANEL, COLOUR_GREY, TR_WIDGET_INSTRUCTION_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_TRACE_RESTRICT_SIGNAL_GUI_TOOLTIP), SetResize(1, 1), EndContainer(),
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
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TR_WIDGET_REMOVE), SetMinimalSize(186, 12), SetFill(1, 0),
														SetDataTip(STR_TRACE_RESTRICT_REMOVE, STR_TRACE_RESTRICT_REMOVE_TOOLTIP), SetResize(1, 0),
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
