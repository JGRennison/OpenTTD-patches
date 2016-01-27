/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file programmable_signals_gui.cpp GUI related to programming signals */

#include "stdafx.h"
#include "programmable_signals.h"
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
#include "tilehighlight_func.h"
#include "rail_map.h"
#include "tile_cmd.h"
#include "error.h"

#include "table/sprites.h"
#include "table/strings.h"

enum ProgramWindowWidgets {
	PROGRAM_WIDGET_CAPTION,
	PROGRAM_WIDGET_INSTRUCTION_LIST,
	PROGRAM_WIDGET_SCROLLBAR,

	PROGRAM_WIDGET_SEL_TOP_LEFT,
	PROGRAM_WIDGET_SEL_TOP_MIDDLE,
	PROGRAM_WIDGET_SEL_TOP_RIGHT,

	PROGRAM_WIDGET_SET_STATE,
	PROGRAM_WIDGET_COND_VARIABLE,
	PROGRAM_WIDGET_COND_COMPARATOR,
	PROGRAM_WIDGET_COND_VALUE,
	PROGRAM_WIDGET_COND_GOTO_SIGNAL,
	PROGRAM_WIDGET_COND_SET_SIGNAL,

	PROGRAM_WIDGET_GOTO_SIGNAL,
	PROGRAM_WIDGET_INSERT,
	PROGRAM_WIDGET_REMOVE,

	PROGRAM_WIDGET_REMOVE_PROGRAM,
	PROGRAM_WIDGET_COPY_PROGRAM,
};

enum PanelWidgets {
	// Left
	DPL_COND_VARIABLE = 0,
	DPL_SET_STATE,

	// Middle
	DPM_COND_COMPARATOR = 0,
	DPM_COND_GOTO_SIGNAL,

	// Right
	DPR_COND_VALUE = 0,
	DPR_COND_SET_SIGNAL
};

static const StringID _program_insert[] = {
	STR_PROGSIG_INSERT_IF,
	STR_PROGSIG_INSERT_SET_SIGNAL,
	INVALID_STRING_ID
};

static SignalOpcode OpcodeForIndex(int index)
{
	switch (index) {
		case 0: return PSO_IF;
		case 1: return PSO_SET_SIGNAL;
		default: NOT_REACHED();
	}
}

static bool IsConditionComparator(SignalCondition *cond)
{
	switch (cond->ConditionCode()) {
		case PSC_NUM_GREEN:
		case PSC_NUM_RED:
			return true;

		default:
			return false;
	}
}

static const StringID _program_condvar[] = {
	/* PSC_ALWAYS   */    STR_PROGSIG_COND_ALWAYS,
	/* PSC_NEVER    */    STR_PROGSIG_COND_NEVER,
	/* PSC_NUM_GREEN */   STR_PROGSIG_CONDVAR_NUM_GREEN,
	/* PSC_NUM_RED   */   STR_PROGSIG_CONDVAR_NUM_RED,
	/* PSC_SIGNAL_STATE*/ STR_PROGSIG_COND_SIGNAL_STATE,
	INVALID_STRING_ID
};

// TODO: These should probably lose the ORDER
static const StringID _program_comparator[] = {
	/* SGC_EQUALS */             STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS,
	/* SGC_NOT_EQUALS */         STR_ORDER_CONDITIONAL_COMPARATOR_NOT_EQUALS,
	/* SGC_LESS_THAN */          STR_ORDER_CONDITIONAL_COMPARATOR_LESS_THAN,
	/* SGC_LESS_THAN_EQUALS */   STR_ORDER_CONDITIONAL_COMPARATOR_LESS_EQUALS,
	/* SGC_MORE_THAN */          STR_ORDER_CONDITIONAL_COMPARATOR_MORE_THAN,
	/* SGC_MORE_THAN_EQUALS */   STR_ORDER_CONDITIONAL_COMPARATOR_MORE_EQUALS,
	/* SGC_IS_TRUE */            STR_ORDER_CONDITIONAL_COMPARATOR_IS_TRUE,
	/* SGC_IS_FALSE */           STR_ORDER_CONDITIONAL_COMPARATOR_IS_FALSE,
	INVALID_STRING_ID
};

static const StringID _program_sigstate[] = {
	STR_COLOUR_RED,
	STR_COLOUR_GREEN,
	INVALID_STRING_ID
};

/** Get the string for a condition */
static char *GetConditionString(SignalCondition *cond, char *buf, char *buflast, bool selected)
{
	StringID string = INVALID_STRING_ID;
	bool comparator = IsConditionComparator(cond);

	if (comparator) {
		SignalVariableCondition *cv = static_cast<SignalVariableCondition*>(cond);
		string = STR_PROGSIG_COND_COMPARE;
		SetDParam(0, _program_condvar[cond->ConditionCode()]);
		SetDParam(1, _program_comparator[cv->comparator]);
		SetDParam(2, cv->value);
	} else {
		string = _program_condvar[cond->ConditionCode()];
		if (cond->ConditionCode() == PSC_SIGNAL_STATE) {
			SignalStateCondition *sig_cond = static_cast<SignalStateCondition*>(cond);
			if (sig_cond->IsSignalValid()) {
				string = STR_PROGSIG_CONDVAR_SIGNAL_STATE_SPECIFIED;
				SetDParam(0, TileX(sig_cond->sig_tile));
				SetDParam(1, TileY(sig_cond->sig_tile));
			} else {
				string = STR_PROGSIG_CONDVAR_SIGNAL_STATE_UNSPECIFIED;
				SetDParam(0, selected ? STR_WHITE : STR_BLACK);
			}
		}
	}
	return GetString(buf, string, buflast);
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
static void DrawInstructionString(SignalInstruction *instruction, int y, bool selected, int indent, int left, int right)
{
	StringID instruction_string = INVALID_STRING_ID;

	char condstr[512];

	switch (instruction->Opcode()) {
		case PSO_FIRST:
			instruction_string = STR_PROGSIG_FIRST;
			break;

		case PSO_LAST:
			instruction_string = STR_PROGSIG_LAST;
			break;

		case PSO_IF: {
			SignalIf *if_ins = static_cast<SignalIf*>(instruction);
			GetConditionString(if_ins->condition, condstr, lastof(condstr), selected);
			SetDParamStr(0, condstr);
			instruction_string = STR_PROGSIG_IF;
			break;
		}

		case PSO_IF_ELSE:
			instruction_string = STR_PROGSIG_ELSE;
			break;

		case PSO_IF_ENDIF:
			instruction_string = STR_PROGSIG_ENDIF;
			break;

		case PSO_SET_SIGNAL: {
			instruction_string = STR_PROGSIG_SET_SIGNAL;
			SignalSet *set = static_cast<SignalSet*>(instruction);
			SetDParam(0, _program_sigstate[set->to_state]);
			break;
		}

		default: NOT_REACHED();
	}

	DrawString(left + indent * 16, right, y, instruction_string, selected ? TC_WHITE : TC_BLACK);
}

struct GuiInstruction {
	SignalInstruction *insn;
	uint indent;
};

typedef SmallVector<GuiInstruction, 4> GuiInstructionList;

class ProgramWindow: public Window {
public:
	ProgramWindow(WindowDesc *desc, SignalReference ref): Window(desc)
	{
		// this->InitNested(desc, (ref.tile << 3) | ref.track);
		this->tile = ref.tile;
		this->track = ref.track;
		this->selected_instruction = -1;

		this->CreateNestedTree(desc);
		this->vscroll = this->GetScrollbar(PROGRAM_WIDGET_SCROLLBAR);
		this->FinishInitNested((ref.tile << 3) | ref.track);

		program = GetSignalProgram(ref);
		RebuildInstructionList();
	}

	virtual void OnClick(Point pt, int widget, int click_count) OVERRIDE
	{
		switch (widget) {
			case PROGRAM_WIDGET_INSTRUCTION_LIST: {
				int sel = this->GetInstructionFromPt(pt.y);

				this->DeleteChildWindows();
				HideDropDownMenu(this);

				if (sel == -1 || this->GetOwner() != _local_company) {
					// Deselect
					this->selected_instruction = -1;
				} else {
					this->selected_instruction = sel;
				}

				this->UpdateButtonState();
			} break;

			case PROGRAM_WIDGET_INSERT: {
				DEBUG(misc, 5, "Selection is %d", this->selected_instruction);
				if (this->GetOwner() != _local_company || this->selected_instruction < 1)
					return;
				ShowDropDownMenu(this, _program_insert, -1, PROGRAM_WIDGET_INSERT, 0, 0, 0);
			} break;

			case PROGRAM_WIDGET_REMOVE: {
				SignalInstruction *ins = GetSelected();
				if (ins == NULL) return;

				uint32 p1 = 0;
				SB(p1, 0, 3, this->track);
				SB(p1, 3, 16, ins->Id());

				DoCommandP(this->tile, p1, 0, CMD_REMOVE_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
				this->RebuildInstructionList();
			} break;

			case PROGRAM_WIDGET_SET_STATE: {
				SignalInstruction *si = this->GetSelected();
				if (!si || si->Opcode() != PSO_SET_SIGNAL) return;
				SignalSet *ss = static_cast <SignalSet*>(si);

				ShowDropDownMenu(this, _program_sigstate, ss->to_state, PROGRAM_WIDGET_SET_STATE, 0, 0, 0);
			} break;

			case PROGRAM_WIDGET_COND_VARIABLE: {
				SignalInstruction *si = this->GetSelected();
				if (!si || si->Opcode() != PSO_IF) return;
				SignalIf *sif = static_cast <SignalIf*>(si);

				ShowDropDownMenu(this, _program_condvar, sif->condition->ConditionCode(), PROGRAM_WIDGET_COND_VARIABLE, 0, 0, 0);
				this->UpdateButtonState();
			} break;

			case PROGRAM_WIDGET_COND_COMPARATOR: {
				SignalInstruction *si = this->GetSelected();
				if (!si || si->Opcode() != PSO_IF) return;
				SignalIf *sif = static_cast <SignalIf*>(si);
				if (!IsConditionComparator(sif->condition)) return;
				SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(sif->condition);

				ShowDropDownMenu(this, _program_comparator, vc->comparator, PROGRAM_WIDGET_COND_COMPARATOR, 0, 0, 0);
			} break;

			case PROGRAM_WIDGET_COND_VALUE: {
				SignalInstruction *si = this->GetSelected();
				if (!si || si->Opcode() != PSO_IF) return;
				SignalIf *sif = static_cast <SignalIf*>(si);
				if (!IsConditionComparator(sif->condition)) return;
				SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(sif->condition);

				SetDParam(0, vc->value);
				//ShowQueryString(STR_JUST_INT, STR_PROGSIG_CONDITION_VALUE_CAPT, 5, 100, this, CS_NUMERAL, QSF_NONE);
				ShowQueryString(STR_JUST_INT, STR_PROGSIG_CONDITION_VALUE_CAPT, 5, this, CS_NUMERAL, QSF_NONE);
				this->UpdateButtonState();
			} break;

			case PROGRAM_WIDGET_COND_GOTO_SIGNAL: {
				SignalInstruction *si = this->GetSelected();
				if (!si || si->Opcode() != PSO_IF) return;
				SignalIf *sif = static_cast <SignalIf*>(si);
				if (sif->condition->ConditionCode() != PSC_SIGNAL_STATE) return;
				SignalStateCondition *sc = static_cast<SignalStateCondition*>(sif->condition);

				if (sc->IsSignalValid()) {
					ScrollMainWindowToTile(sc->sig_tile);
				} else {
					ShowErrorMessage(STR_ERROR_CAN_T_GOTO_UNDEFINED_SIGNAL, STR_EMPTY, WL_INFO);
				}
				// this->RaiseWidget(PROGRAM_WIDGET_COND_GOTO_SIGNAL);
			} break;

			case PROGRAM_WIDGET_COND_SET_SIGNAL: {
				this->ToggleWidgetLoweredState(PROGRAM_WIDGET_COND_SET_SIGNAL);
				this->SetWidgetDirty(PROGRAM_WIDGET_COND_SET_SIGNAL);
				if (this->IsWidgetLowered(PROGRAM_WIDGET_COND_SET_SIGNAL)) {
					SetObjectToPlaceWnd(ANIMCURSOR_BUILDSIGNALS, PAL_NONE, HT_RECT, this);
				} else {
					ResetObjectToPlace();
				}
			} break;

			case PROGRAM_WIDGET_GOTO_SIGNAL: {
				ScrollMainWindowToTile(this->tile);
				// this->RaiseWidget(PROGRAM_WIDGET_GOTO_SIGNAL);
			} break;

			case PROGRAM_WIDGET_REMOVE_PROGRAM: {
				DoCommandP(this->tile, this->track | (SPMC_REMOVE << 3), 0, CMD_SIGNAL_PROGRAM_MGMT | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
				this->RebuildInstructionList();
			} break;

			case PROGRAM_WIDGET_COPY_PROGRAM: {
				this->ToggleWidgetLoweredState(PROGRAM_WIDGET_COPY_PROGRAM);
				this->SetWidgetDirty(PROGRAM_WIDGET_COPY_PROGRAM);
				if (this->IsWidgetLowered(PROGRAM_WIDGET_COPY_PROGRAM)) {
					SetObjectToPlaceWnd(ANIMCURSOR_BUILDSIGNALS, PAL_NONE, HT_RECT, this);
				} else {
					ResetObjectToPlace();
				}
			} break;
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile1) OVERRIDE
	{
		if (this->IsWidgetLowered(PROGRAM_WIDGET_COPY_PROGRAM)) {
			//Copy program from another progsignal
			TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(tile1, TRANSPORT_RAIL, 0));
			if (trackbits & TRACK_BIT_VERT) { // N-S direction
				trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
			}
			if (trackbits & TRACK_BIT_HORZ) { // E-W direction
				trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
			}
			Track track1 = FindFirstTrack(trackbits);
			if(track1 == INVALID_TRACK) {
				return;
			}
			Trackdir td = TrackToTrackdir(track1);
			Trackdir tdr = ReverseTrackdir(td);
			if (!(HasSignalOnTrackdir(tile1, td) || HasSignalOnTrackdir(tile1, tdr)))
				return;

			if (GetSignalType(tile1, track1) != SIGTYPE_PROG) {
				ShowErrorMessage(STR_ERROR_INVALID_SIGNAL, STR_ERROR_NOT_AN_PROG_SIGNAL, WL_INFO);
				return;
			}
			if(this->tile == tile1 && this->track == track1) {
				ShowErrorMessage(STR_ERROR_INVALID_SIGNAL, STR_ERROR_CANNOT_USE_SELF, WL_INFO);
				return;
			}

			SignalProgram *sp = GetExistingSignalProgram(SignalReference(tile1, track1));
			if (!sp) {
				ShowErrorMessage(STR_ERROR_INVALID_SIGNAL, STR_ERROR_NOT_AN_EXIT_SIGNAL, WL_INFO);
				return;
			}
			DoCommandP(this->tile, this->track | (SPMC_CLONE << 3) | (track1 << 7), tile1, CMD_SIGNAL_PROGRAM_MGMT | CMD_MSG(STR_ERROR_CAN_T_INSERT_INSTRUCTION));
			ResetObjectToPlace();
			this->RaiseWidget(PROGRAM_WIDGET_COPY_PROGRAM);
			this->RebuildInstructionList();
			//OnPaint(); // this appears to cause visual artefacts
			return;
		}

		SignalInstruction *si = this->GetSelected();
		if (!si || si->Opcode() != PSO_IF) return;
		SignalIf *sif = static_cast <SignalIf*>(si);
		if (sif->condition->ConditionCode() != PSC_SIGNAL_STATE) return;

		if (!IsPlainRailTile(tile1)) {
			return;
		}

		TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(tile1, TRANSPORT_RAIL, 0));
		if (trackbits & TRACK_BIT_VERT) { // N-S direction
			trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
		}

		if (trackbits & TRACK_BIT_HORZ) { // E-W direction
			trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
		}
		Track track1 = FindFirstTrack(trackbits);
		if(track1 == INVALID_TRACK) {
			return;
		}

		Trackdir td = TrackToTrackdir(track1);
		Trackdir tdr = ReverseTrackdir(td);

		if (HasSignalOnTrackdir(tile1, td) && HasSignalOnTrackdir(tile1, tdr)) {
			ShowErrorMessage(STR_ERROR_INVALID_SIGNAL, STR_ERROR_CAN_T_DEPEND_UPON_BIDIRECTIONAL_SIGNALS, WL_INFO);
			return;
		} else if (HasSignalOnTrackdir(tile1, tdr) && !HasSignalOnTrackdir(tile1, td)) {
			td = tdr;
		}

		if (!HasSignalOnTrackdir(tile1, td)) {
			return;
		}

		//!!!!!!!!!!!!!!!
		if (!(GetSignalType(tile1, track1) == SIGTYPE_EXIT || GetSignalType(tile1, track1) == SIGTYPE_PROG)) {
		//!!!!!!!!!!!!!!!
			ShowErrorMessage(STR_ERROR_INVALID_SIGNAL, STR_ERROR_NOT_AN_EXIT_SIGNAL, WL_INFO);
			return;
		}

		uint32 p1 = 0, p2 = 0;
		SB(p1, 0, 3, this->track);
		SB(p1, 3, 16, si->Id());

		SB(p2, 0, 1, 1);
		SB(p2, 1, 4,  td);
		SB(p2, 5, 27, tile1);

		DoCommandP(this->tile, p1, p2, CMD_MODIFY_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
		ResetObjectToPlace();
		this->RaiseWidget(PROGRAM_WIDGET_COND_SET_SIGNAL);
		//OnPaint(); // this appears to cause visual artefacts
	}

	virtual void OnQueryTextFinished(char *str) OVERRIDE
	{
		if (!StrEmpty(str)) {
			SignalInstruction *si = this->GetSelected();
			if (!si || si->Opcode() != PSO_IF) return;
			SignalIf *sif = static_cast <SignalIf*>(si);
			if (!IsConditionComparator(sif->condition)) return;

			uint value = atoi(str);

			uint32 p1 = 0, p2 = 0;
			SB(p1, 0, 3, this->track);
			SB(p1, 3, 16, si->Id());

			SB(p2, 0, 1, 1);
			SB(p2, 1, 2, SCF_VALUE);
			SB(p2, 3, 27, value);

			DoCommandP(this->tile, p1, p2, CMD_MODIFY_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
		}
	}

	virtual void OnDropdownSelect(int widget, int index) OVERRIDE
	{
		SignalInstruction *ins = this->GetSelected();
		if (!ins) return;

		switch (widget) {
			case PROGRAM_WIDGET_INSERT: {
				uint64 p1 = 0;
				SB(p1, 0, 3, this->track);
				SB(p1, 3, 16, ins->Id());
				SB(p1, 19, 8, OpcodeForIndex(index));

				DoCommandP(this->tile, p1, 0, CMD_INSERT_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_INSERT_INSTRUCTION));
				this->RebuildInstructionList();
				break;
			}

			case PROGRAM_WIDGET_SET_STATE: {
				uint64 p1 = 0;
				SB(p1, 0, 3, this->track);
				SB(p1, 3, 16, ins->Id());

				DoCommandP(this->tile, p1, index, CMD_MODIFY_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
				break;
			}

			case PROGRAM_WIDGET_COND_VARIABLE: {
				uint64 p1 = 0, p2 = 0;
				SB(p1, 0, 3, this->track);
				SB(p1, 3, 16, ins->Id());

				SB(p2, 0, 1, 0);
				SB(p2, 1, 8, index);

				DoCommandP(this->tile, p1, p2, CMD_MODIFY_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
				break;
			}

			case PROGRAM_WIDGET_COND_COMPARATOR: {
				uint64 p1 = 0, p2 = 0;
				SB(p1, 0, 3, this->track);
				SB(p1, 3, 16, ins->Id());

				SB(p2, 0, 1, 1);
				SB(p2, 1, 2, SCF_COMPARATOR);
				SB(p2, 3, 27, index);

				DoCommandP(this->tile, p1, p2, CMD_MODIFY_SIGNAL_INSTRUCTION | CMD_MSG(STR_ERROR_CAN_T_MODIFY_INSTRUCTION));
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) OVERRIDE
	{
		switch (widget) {
			case PROGRAM_WIDGET_INSTRUCTION_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnResize() OVERRIDE
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, PROGRAM_WIDGET_INSTRUCTION_LIST);
	}

	virtual void OnPaint() OVERRIDE
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const OVERRIDE
	{
		if (widget != PROGRAM_WIDGET_INSTRUCTION_LIST) return;

		int y = r.top + WD_FRAMERECT_TOP;
		int line_height = this->GetWidget<NWidgetBase>(PROGRAM_WIDGET_INSTRUCTION_LIST)->resize_y;

		int no = this->vscroll->GetPosition();

		for (const GuiInstruction *i = instructions.Begin() + no, *e = instructions.End();
				i != e; ++i, no++) {
			/* Don't draw anything if it extends past the end of the window. */
			if (!this->vscroll->IsVisible(no)) break;

			DrawInstructionString(i->insn, y, no == this->selected_instruction, i->indent, r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT);
			y += line_height;
		}
	}

	virtual void OnInvalidateData(int data, bool gui_scope) OVERRIDE {
		if (gui_scope) {
			this->RebuildInstructionList();
		}
	}


	virtual void SetStringParameters(int widget) const OVERRIDE
	{
		switch (widget) {
			case PROGRAM_WIDGET_COND_VALUE: {
				SetDParam(0, 0);
				SignalInstruction *insn = this->GetSelected();
				if (!insn || insn->Opcode() != PSO_IF) return;
				SignalIf *si = static_cast<SignalIf*>(insn);
				if (!IsConditionComparator(si->condition)) return;
				SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(si->condition);
				SetDParam(0, vc->value);
			} break;
		}
	}

private:
	SignalInstruction *GetSelected() const
	{
		if (this->selected_instruction == -1
				|| this->selected_instruction >= int(this->instructions.Length()))
			return NULL;

		return this->instructions[this->selected_instruction].insn;
	}

	Owner GetOwner()
	{
		return GetTileOwner(this->tile);
	}

	int GetInstructionFromPt(int y)
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(PROGRAM_WIDGET_INSTRUCTION_LIST);
		int sel = (y - nwid->pos_y - WD_FRAMERECT_TOP) / nwid->resize_y; // Selected line

		if ((uint)sel >= this->vscroll->GetCapacity()) return -1;

		sel += this->vscroll->GetPosition();

		return (sel <= int(this->instructions.Length()) && sel >= 0) ? sel : -1;
	}

	void RebuildInstructionList()
	{
		uint old_len = this->instructions.Length();
		this->instructions.Clear();
		SignalInstruction *insn = program->first_instruction;
		uint indent = 0;

		do {
			DEBUG(misc, 5, "PSig Gui: Opcode %d", insn->Opcode());
			switch (insn->Opcode()) {
				case PSO_FIRST:
				case PSO_LAST: {
					SignalSpecial *s = static_cast<SignalSpecial*>(insn);
					GuiInstruction *gi = this->instructions.Append();
					gi->insn   = s;
					gi->indent = indent;
					insn = s->next;
					break;
				}

				case PSO_IF: {
					SignalIf *i = static_cast<SignalIf*>(insn);
					GuiInstruction *gi = this->instructions.Append();
					gi->insn   = i;
					gi->indent = indent++;
					insn = i->if_true;
					break;
				}

				case PSO_IF_ELSE: {
					SignalIf::PseudoInstruction *p = static_cast<SignalIf::PseudoInstruction*>(insn);
					GuiInstruction *gi = this->instructions.Append();
					gi->insn   = p;
					gi->indent = indent - 1;
					insn = p->block->if_false;
					break;
				}

				case PSO_IF_ENDIF: {
					SignalIf::PseudoInstruction *p = static_cast<SignalIf::PseudoInstruction*>(insn);
					GuiInstruction *gi = this->instructions.Append();
					gi->insn   = p;
					gi->indent = --indent;
					insn = p->block->after;
					break;
				}

				case PSO_SET_SIGNAL: {
					SignalSet *s = static_cast<SignalSet*>(insn);
					GuiInstruction *gi = this->instructions.Append();
					gi->insn   = s;
					gi->indent = indent;
					insn = s->next;
					break;
				}

				default: NOT_REACHED();
			}
		} while (insn);

		this->vscroll->SetCount(this->instructions.Length());
		if (this->instructions.Length() != old_len)
			selected_instruction = -1;
		UpdateButtonState();
	}

	void UpdateButtonState()
	{
		// Do not close the Signals GUI when opening the ProgrammableSignals GUI
		// ResetObjectToPlace();
		this->RaiseWidget(PROGRAM_WIDGET_INSERT);
		this->RaiseWidget(PROGRAM_WIDGET_REMOVE);
		this->RaiseWidget(PROGRAM_WIDGET_SET_STATE);
		this->RaiseWidget(PROGRAM_WIDGET_COND_VARIABLE);
		this->RaiseWidget(PROGRAM_WIDGET_COND_COMPARATOR);
		this->RaiseWidget(PROGRAM_WIDGET_COND_VALUE);
		this->RaiseWidget(PROGRAM_WIDGET_COND_GOTO_SIGNAL);

		NWidgetStacked *left_sel   = this->GetWidget<NWidgetStacked>(PROGRAM_WIDGET_SEL_TOP_LEFT);
		NWidgetStacked *middle_sel = this->GetWidget<NWidgetStacked>(PROGRAM_WIDGET_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel  = this->GetWidget<NWidgetStacked>(PROGRAM_WIDGET_SEL_TOP_RIGHT);

		// Disable all the modifier buttons - we will re-enable them if applicable
		this->DisableWidget(PROGRAM_WIDGET_SET_STATE);
		this->DisableWidget(PROGRAM_WIDGET_COND_VARIABLE);
		this->DisableWidget(PROGRAM_WIDGET_COND_COMPARATOR);
		this->DisableWidget(PROGRAM_WIDGET_COND_VALUE);
		this->DisableWidget(PROGRAM_WIDGET_COND_SET_SIGNAL);
		this->DisableWidget(PROGRAM_WIDGET_COND_GOTO_SIGNAL);

		// Don't allow modifications if don't own, or have selected invalid instruction
		if (this->GetOwner() != _local_company || this->selected_instruction < 1) {
			this->DisableWidget(PROGRAM_WIDGET_INSERT);
			this->DisableWidget(PROGRAM_WIDGET_REMOVE);
			this->SetDirty();
			return;
		} else {
			this->EnableWidget(PROGRAM_WIDGET_INSERT);
			this->EnableWidget(PROGRAM_WIDGET_REMOVE);
		}

		SignalInstruction *insn = GetSelected();
		if (!insn) return;

		switch (insn->Opcode()) {
			case PSO_IF: {
				SignalIf *i = static_cast<SignalIf*>(insn);
				left_sel->SetDisplayedPlane(DPL_COND_VARIABLE);
				middle_sel->SetDisplayedPlane(DPM_COND_COMPARATOR);
				right_sel->SetDisplayedPlane(DPR_COND_VALUE);

				this->EnableWidget(PROGRAM_WIDGET_COND_VARIABLE);
				this->GetWidget<NWidgetCore>(PROGRAM_WIDGET_COND_VARIABLE)->widget_data =
						_program_condvar[i->condition->ConditionCode()];

				if (IsConditionComparator(i->condition)) {
					SignalVariableCondition *vc = static_cast<SignalVariableCondition*>(i->condition);
					this->EnableWidget(PROGRAM_WIDGET_COND_COMPARATOR);
					this->EnableWidget(PROGRAM_WIDGET_COND_VALUE);

					this->GetWidget<NWidgetCore>(PROGRAM_WIDGET_COND_COMPARATOR)->widget_data =
						_program_comparator[vc->comparator];

				} else if (i->condition->ConditionCode() == PSC_SIGNAL_STATE) {
					this->EnableWidget(PROGRAM_WIDGET_COND_GOTO_SIGNAL);
					this->EnableWidget(PROGRAM_WIDGET_COND_SET_SIGNAL);
					middle_sel->SetDisplayedPlane(DPM_COND_GOTO_SIGNAL);
					right_sel->SetDisplayedPlane(DPR_COND_SET_SIGNAL);
				}
			} break;

			case PSO_SET_SIGNAL: {
				SignalSet *s = static_cast<SignalSet*>(insn);
				left_sel->SetDisplayedPlane(DPL_SET_STATE);
				this->SetWidgetDisabledState(PROGRAM_WIDGET_SET_STATE, false);
				this->GetWidget<NWidgetCore>(PROGRAM_WIDGET_SET_STATE)->widget_data =
						_program_sigstate[s->to_state];
			} break;

			case PSO_FIRST:
			case PSO_LAST:
			case PSO_IF_ELSE:
			case PSO_IF_ENDIF:
				// All cannot be modified
				this->DisableWidget(PROGRAM_WIDGET_REMOVE);
				break;

			default:
				NOT_REACHED();
		}

		this->SetDirty();
	}

	TileIndex tile;
	Track track;
	SignalProgram *program;
	GuiInstructionList instructions;
	int selected_instruction;
	Scrollbar *vscroll;
};

static const NWidgetPart _nested_program_widgets[] = {
	// Title bar
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, PROGRAM_WIDGET_CAPTION), SetDataTip(STR_PROGSIG_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	// Program display
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, PROGRAM_WIDGET_INSTRUCTION_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_PROGSIG_CAPTION), SetResize(1, 1), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, PROGRAM_WIDGET_SCROLLBAR),
	EndContainer(),

	// Button Bar
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, PROGRAM_WIDGET_SEL_TOP_LEFT),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, PROGRAM_WIDGET_COND_VARIABLE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_PROGSIG_COND_VARIABLE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, PROGRAM_WIDGET_SET_STATE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_PROGSIG_SIGNAL_STATE_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, PROGRAM_WIDGET_SEL_TOP_MIDDLE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, PROGRAM_WIDGET_COND_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_PROGSIG_COND_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_COND_GOTO_SIGNAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_PROGSIG_GOTO_SIGNAL, STR_PROGSIG_GOTO_SIGNAL_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, PROGRAM_WIDGET_SEL_TOP_RIGHT),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_COND_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_BLACK_COMMA, STR_PROGSIG_COND_VALUE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_COND_SET_SIGNAL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_PROGSIG_COND_SET_SIGNAL, STR_PROGSIG_COND_SET_SIGNAL_TOOLTIP), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_GREY, PROGRAM_WIDGET_GOTO_SIGNAL), SetMinimalSize(12, 12), SetDataTip(SPR_ARROW_RIGHT, STR_PROGSIG_GOTO_SIGNAL_TOOLTIP),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, PROGRAM_WIDGET_INSERT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_PROGSIG_INSERT, STR_PROGSIG_INSERT_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_REMOVE), SetMinimalSize(186, 12), SetFill(1, 0),
														SetDataTip(STR_PROGSIG_REMOVE, STR_PROGSIG_REMOVE_TOOLTIP), SetResize(1, 0),
		EndContainer(),
	EndContainer(),

	/* Third button row*/
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_REMOVE_PROGRAM), SetMinimalSize(124, 12), SetFill(1, 0), SetDataTip(STR_PROGSIG_REMOVE_PROGRAM, STR_PROGSIG_REMOVE_PROGRAM_TOOLTIP), SetResize(1, 0),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, PROGRAM_WIDGET_COPY_PROGRAM), SetMinimalSize(124, 12), SetFill(1, 0), SetDataTip(STR_PROGSIG_COPY_PROGRAM, STR_PROGSIG_COPY_PROGRAM_TOOLTIP), SetResize(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _program_desc(
	WDP_AUTO, "signal_program", 384, 100,
	WC_SIGNAL_PROGRAM, WC_BUILD_SIGNAL,
	WDF_CONSTRUCTION,
	_nested_program_widgets, lengthof(_nested_program_widgets)
);

void ShowSignalProgramWindow(SignalReference ref)
{
	uint32 window_id = (ref.tile << 3) | ref.track;
	if (BringWindowToFrontById(WC_SIGNAL_PROGRAM, window_id) != NULL) return;

	new ProgramWindow(&_program_desc, ref);
}
