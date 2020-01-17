/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_gui_main.cpp Template-based train replacement: main GUI. */

#include "stdafx.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "newgrf_engine.h"
#include "group.h"
#include "rail.h"
#include "strings_func.h"
#include "window_func.h"
#include "autoreplace_func.h"
#include "company_func.h"
#include "engine_base.h"
#include "window_gui.h"
#include "viewport_func.h"
#include "tilehighlight_func.h"
#include "engine_gui.h"
#include "settings_func.h"
#include "core/geometry_func.hpp"
#include "rail_gui.h"
#include "network/network.h"
#include "zoom_func.h"

#include "table/sprites.h"
#include "table/strings.h"

// test creating pool -> creating vehicles
#include "core/pool_func.hpp"

#include "vehicle_gui_base.h"
#include "vehicle_base.h"
#include "train.h"
#include "vehicle_func.h"

#include "gfx_type.h"

#include "engine_func.h"

// drawing the vehicle length based on occupied tiles
#include "spritecache.h"

#include "tbtr_template_gui_main.h"
#include "tbtr_template_gui_create.h"
#include "tbtr_template_vehicle.h"

#include <iostream>
#include <stdio.h>

#include "safeguards.h"


typedef GUIList<const Group*> GUIGroupList;

enum TemplateReplaceWindowWidgets {
	TRW_CAPTION,

	TRW_WIDGET_INSET_GROUPS,
	TRW_WIDGET_TOP_MATRIX,
	TRW_WIDGET_TOP_SCROLLBAR,

	TRW_WIDGET_INSET_TEMPLATES,
	TRW_WIDGET_BOTTOM_MATRIX,
	TRW_WIDGET_MIDDLE_SCROLLBAR,
	TRW_WIDGET_BOTTOM_SCROLLBAR,

	TRW_WIDGET_TMPL_INFO_INSET,
	TRW_WIDGET_TMPL_INFO_PANEL,

	TRW_WIDGET_TMPL_PRE_BUTTON_FLUFF,

	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY,
	TRW_WIDGET_TMPL_BUTTONS_CONFIG_RIGHTPANEL,

	TRW_WIDGET_TMPL_BUTTONS_DEFINE,
	TRW_WIDGET_TMPL_BUTTONS_EDIT,
	TRW_WIDGET_TMPL_BUTTONS_CLONE,
	TRW_WIDGET_TMPL_BUTTONS_DELETE,

	TRW_WIDGET_TMPL_BUTTONS_EDIT_RIGHTPANEL,

	TRW_WIDGET_TITLE_INFO_GROUP,
	TRW_WIDGET_TITLE_INFO_TEMPLATE,

	TRW_WIDGET_INFO_GROUP,
	TRW_WIDGET_INFO_TEMPLATE,

	TRW_WIDGET_TMPL_BUTTONS_SPACER,

	TRW_WIDGET_START,
	TRW_WIDGET_TRAIN_FLUFF_LEFT,
	TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN,
	TRW_WIDGET_TRAIN_FLUFF_RIGHT,
	TRW_WIDGET_STOP,

	TRW_WIDGET_SEL_TMPL_DISPLAY_CREATE,
};

static const NWidgetPart _widgets[] = {
	// Title bar
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, TRW_CAPTION), SetDataTip(STR_TMPL_RPL_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	//Top Matrix
	NWidget(NWID_VERTICAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, TRW_WIDGET_INSET_GROUPS), SetPadding(2, 2, 2, 2), SetResize(1, 0), SetDataTip(STR_TMPL_MAINGUI_DEFINEDGROUPS, STR_NULL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, TRW_WIDGET_TOP_MATRIX), SetMinimalSize(216, 0), SetFill(1, 1), SetDataTip(0x1, STR_REPLACE_HELP_LEFT_ARRAY), SetResize(1, 0), SetScrollbar(TRW_WIDGET_TOP_SCROLLBAR),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TRW_WIDGET_TOP_SCROLLBAR),
		EndContainer(),
	EndContainer(),
	// Template Display
	NWidget(NWID_VERTICAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, TRW_WIDGET_INSET_TEMPLATES), SetPadding(2, 2, 2, 2), SetResize(1, 0), SetDataTip(STR_TMPL_AVAILABLE_TEMPLATES, STR_NULL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, TRW_WIDGET_BOTTOM_MATRIX), SetMinimalSize(216, 0), SetFill(1, 1), SetDataTip(0x1, STR_REPLACE_HELP_RIGHT_ARRAY), SetResize(1, 1), SetScrollbar(TRW_WIDGET_MIDDLE_SCROLLBAR),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TRW_WIDGET_MIDDLE_SCROLLBAR),
		EndContainer(),
	EndContainer(),
	// Info Area
	NWidget(NWID_VERTICAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, TRW_WIDGET_TMPL_INFO_INSET), SetPadding(2, 2, 2, 2), SetResize(1, 0), SetDataTip(STR_TMPL_TEMPLATE_INFO, STR_NULL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_INFO_PANEL), SetMinimalSize(216,120), SetResize(1,0), SetScrollbar(TRW_WIDGET_BOTTOM_SCROLLBAR), EndContainer(),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TRW_WIDGET_BOTTOM_SCROLLBAR),
		EndContainer(),
	EndContainer(),
	// Control Area
	NWidget(NWID_VERTICAL),
		// Spacing
		NWidget(WWT_INSET, COLOUR_GREY, TRW_WIDGET_TMPL_PRE_BUTTON_FLUFF), SetMinimalSize(139, 12), SetResize(1,0), EndContainer(),
		// Config buttons
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE), SetMinimalSize(150,12), SetResize(0,0), SetDataTip(STR_TMPL_SET_USEDEPOT, STR_TMPL_SET_USEDEPOT_TIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP), SetMinimalSize(150,12), SetResize(0,0), SetDataTip(STR_TMPL_SET_KEEPREMAINDERS, STR_TMPL_SET_KEEPREMAINDERS_TIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT), SetMinimalSize(150,12), SetResize(0,0), SetDataTip(STR_TMPL_SET_REFIT, STR_TMPL_SET_REFIT_TIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY), SetMinimalSize(150,12), SetResize(0,0), SetDataTip(STR_TMPL_SET_OLD_ONLY, STR_TMPL_SET_OLD_ONLY_TIP),
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIG_RIGHTPANEL), SetMinimalSize(12,12), SetResize(1,0), EndContainer(),
		EndContainer(),
		// Edit buttons
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_DEFINE), SetMinimalSize(75,12), SetResize(0,0), SetDataTip(STR_TMPL_DEFINE_TEMPLATE, STR_TMPL_DEFINE_TEMPLATE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_EDIT), SetMinimalSize(75,12), SetResize(0,0), SetDataTip(STR_TMPL_EDIT_TEMPLATE, STR_TMPL_EDIT_TEMPLATE),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CLONE), SetMinimalSize(75,12), SetResize(0,0), SetDataTip(STR_TMPL_CREATE_CLONE_VEH, STR_TMPL_CREATE_CLONE_VEH),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_DELETE), SetMinimalSize(75,12), SetResize(0,0), SetDataTip(STR_TMPL_DELETE_TEMPLATE, STR_TMPL_DELETE_TEMPLATE),
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_EDIT_RIGHTPANEL), SetMinimalSize(50,12), SetResize(1,0), EndContainer(),
		EndContainer(),
	EndContainer(),
	// Start/Stop buttons
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_START), SetMinimalSize(150, 12), SetDataTip(STR_TMPL_RPL_START, STR_REPLACE_ENGINE_WAGON_SELECT_HELP),
		NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TRAIN_FLUFF_LEFT), SetMinimalSize(15, 12), EndContainer(),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(0x0, STR_REPLACE_HELP_RAILTYPE), SetResize(1, 0),
		NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TRAIN_FLUFF_RIGHT), SetMinimalSize(16, 12), EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_STOP), SetMinimalSize(150, 12), SetDataTip(STR_TMPL_RPL_STOP, STR_REPLACE_REMOVE_WAGON_HELP),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _replace_rail_vehicle_desc(
	WDP_AUTO,
	"template replace window",
	456, 156,
	WC_TEMPLATEGUI_MAIN,
	WC_NONE,                     // parent window class
	WDF_CONSTRUCTION,
	_widgets, lengthof(_widgets)
);

class TemplateReplaceWindow : public Window {
private:

	GUIGroupList groups;          ///< List of groups
	uint unitnumber_digits;

	std::vector<int> indents; ///< Indentation levels

	short matrixContentLeftMargin;
	int bottom_matrix_item_size = 0;

	int details_height;           ///< Minimal needed height of the details panels (found so far).
	RailType sel_railtype;        ///< Type of rail tracks selected.
	Scrollbar *vscroll[3];
	// listing/sorting continued
	GUITemplateList templates;
	GUITemplateList::SortFunction **template_sorter_funcs;

	short selected_template_index;
	short selected_group_index;

	bool editInProgress;

public:
	TemplateReplaceWindow(WindowDesc *wdesc, uint unitnumber_digits) : Window(wdesc)
	{
		// listing/sorting
		templates.SetSortFuncs(this->template_sorter_funcs);

		// From BaseVehicleListWindow
		this->unitnumber_digits = unitnumber_digits;

		this->sel_railtype = INVALID_RAILTYPE;

		this->details_height = 10 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		this->CreateNestedTree(wdesc != nullptr);
		this->vscroll[0] = this->GetScrollbar(TRW_WIDGET_TOP_SCROLLBAR);
		this->vscroll[1] = this->GetScrollbar(TRW_WIDGET_MIDDLE_SCROLLBAR);
		this->vscroll[2] = this->GetScrollbar(TRW_WIDGET_BOTTOM_SCROLLBAR);
		this->FinishInitNested(VEH_TRAIN);

		this->owner = _local_company;

		this->groups.ForceRebuild();
		this->groups.NeedResort();
		this->BuildGroupList(_local_company);

		this->matrixContentLeftMargin = 40;
		this->selected_template_index = -1;
		this->selected_group_index = -1;

		this->UpdateButtonState();

		this->editInProgress = false;

		this->templates.ForceRebuild();

		BuildTemplateGuiList(&this->templates, this->vscroll[1], this->owner, this->sel_railtype);
	}

	~TemplateReplaceWindow() {
		DeleteWindowById(WC_CREATE_TEMPLATE, this->window_number);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case TRW_WIDGET_TOP_MATRIX:
				resize->height = WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + WD_MATRIX_BOTTOM;
				size->height = 8 * resize->height;
				break;
			case TRW_WIDGET_BOTTOM_MATRIX: {
				int base_resize = WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + WD_MATRIX_BOTTOM;
				int target_resize = WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + ScaleGUITrad(GetVehicleHeight(VEH_TRAIN));
				this->bottom_matrix_item_size = resize->height = CeilT<int>(target_resize, base_resize);
				size->height = 4 * resize->height;
				break;
			}
			case TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN: {
				Dimension d = GetStringBoundingBox(STR_REPLACE_ALL_RAILTYPE);
				for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
					const RailtypeInfo *rti = GetRailTypeInfo(rt);
					// Skip rail type if it has no label
					if (rti->label == 0) continue;
					d = maxdim(d, GetStringBoundingBox(rti->strings.replace_text));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case TRW_CAPTION:
				SetDParam(0, STR_TMPL_RPL_TITLE);
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case TRW_WIDGET_TOP_MATRIX: {
				DrawAllGroupsFunction(r);
				break;
			}
			case TRW_WIDGET_BOTTOM_MATRIX: {
				DrawTemplateList(r);
				break;
			}
			case TRW_WIDGET_TMPL_INFO_PANEL: {
				DrawTemplateInfo(r);
				break;
			}
		}
	}

	virtual void OnPaint()
	{
		BuildTemplateGuiList(&this->templates, this->vscroll[1], this->owner, this->sel_railtype);

		this->BuildGroupList(_local_company);

		/* sets the colour of that art thing */
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_FLUFF_LEFT)->colour  = _company_colours[_local_company];
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_FLUFF_RIGHT)->colour = _company_colours[_local_company];

		/* Show the selected railtype in the pulldown menu */
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN)->widget_data = (this->sel_railtype == INVALID_RAILTYPE) ? STR_REPLACE_ALL_RAILTYPE : GetRailTypeInfo(this->sel_railtype)->strings.replace_text;

		if ((this->selected_template_index < 0) || (this->selected_template_index >= (short)this->templates.size())) {
			this->vscroll[2]->SetCount(24);
		} else {
			const TemplateVehicle *tmp = this->templates[this->selected_template_index];
			uint min_height = 30;
			uint height = 30;
			CargoArray cargo_caps;
			short count_columns = 0;
			short max_columns = 2;

			for (; tmp != nullptr; tmp = tmp->Next()) {
				cargo_caps[tmp->cargo_type] += tmp->cargo_cap;
			}

			for (CargoID i = 0; i < NUM_CARGO; ++i) {
				if (cargo_caps[i] > 0) {
					if (count_columns % max_columns == 0) {
						height += FONT_HEIGHT_NORMAL;
					}

					++count_columns;
				}
			}

			min_height = max(min_height, height);
			this->vscroll[2]->SetCount(min_height);
		}

		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (this->editInProgress) return;

		switch (widget) {
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size())) {
					uint32 template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_REUSE_DEPOT_VEHICLES, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size())) {
					uint32 template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_KEEP_REMAINING_VEHICLES, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size())) {
					uint32 template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_REFIT_AS_TEMPLATE, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size())) {
					uint32 template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_TMPL_REPLACE_OLD_ONLY, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_DEFINE: {
				editInProgress = true;
				ShowTemplateCreateWindow(nullptr, &editInProgress);
				UpdateButtonState();
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_EDIT: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size())) {
					editInProgress = true;
					TemplateVehicle *sel = TemplateVehicle::Get(((this->templates)[selected_template_index])->index);
					ShowTemplateCreateWindow(sel, &editInProgress);
					UpdateButtonState();
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CLONE: {
				this->SetWidgetDirty(TRW_WIDGET_TMPL_BUTTONS_CLONE);
				this->ToggleWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CLONE);

				if (this->IsWidgetLowered(TRW_WIDGET_TMPL_BUTTONS_CLONE)) {
					static const CursorID clone_icon =	SPR_CURSOR_CLONE_TRAIN;
					SetObjectToPlaceWnd(clone_icon, PAL_NONE, HT_VEHICLE, this);
				} else {
					ResetObjectToPlace();
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_DELETE:
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size()) && !editInProgress) {

					uint32 template_index = ((this->templates)[selected_template_index])->index;

					bool succeeded = DoCommandP(0, template_index, 0, CMD_DELETE_TEMPLATE_VEHICLE, nullptr);

					if (succeeded) {
						BuildTemplateGuiList(&this->templates, this->vscroll[1], this->owner, this->sel_railtype);
						selected_template_index = -1;
					}
				}
				break;
			case TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN: // Railtype selection dropdown menu
				ShowDropDownList(this, GetRailTypeDropDownList(true, true), this->sel_railtype, TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN);
				break;
			case TRW_WIDGET_TOP_MATRIX: {
				uint16 newindex = (uint16)((pt.y - this->nested_array[TRW_WIDGET_TOP_MATRIX]->pos_y) / (WD_MATRIX_TOP + FONT_HEIGHT_NORMAL+ WD_MATRIX_BOTTOM) ) + this->vscroll[0]->GetPosition();
				if (newindex == this->selected_group_index || newindex >= this->groups.size()) {
					this->selected_group_index = -1;
				} else if (newindex < this->groups.size()) {
					this->selected_group_index = newindex;
				}
				this->UpdateButtonState();
				break;
			}
			case TRW_WIDGET_BOTTOM_MATRIX: {
				uint16 newindex = (uint16)((pt.y - this->nested_array[TRW_WIDGET_BOTTOM_MATRIX]->pos_y) / this->bottom_matrix_item_size) + this->vscroll[1]->GetPosition();
				if (newindex == this->selected_template_index || newindex >= templates.size()) {
					this->selected_template_index = -1;
				} else if (newindex < templates.size()) {
					this->selected_template_index = newindex;
				}
				this->UpdateButtonState();
				break;
			}
			case TRW_WIDGET_START: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size()) &&
						(this->selected_group_index >= 0) && (this->selected_group_index < (short)this->groups.size())) {
					uint32 tv_index = ((this->templates)[selected_template_index])->index;
					int current_group_index = (this->groups)[this->selected_group_index]->index;

					DoCommandP(0, current_group_index, tv_index, CMD_ISSUE_TEMPLATE_REPLACEMENT, nullptr);
					this->UpdateButtonState();
				}
				break;
			}
			case TRW_WIDGET_STOP:
				if ((this->selected_group_index < 0) || (this->selected_group_index >= (short)this->groups.size())) {
					return;
				}

				int current_group_index = (this->groups)[this->selected_group_index]->index;

				DoCommandP(0, current_group_index, 0, CMD_DELETE_TEMPLATE_REPLACEMENT, nullptr);
				this->UpdateButtonState();
				break;
		}
		this->SetDirty();
	}

	virtual bool OnVehicleSelect(const Vehicle *v)
	{
		bool succeeded = DoCommandP(0, v->index, 0, CMD_CLONE_TEMPLATE_VEHICLE_FROM_TRAIN | CMD_MSG(STR_TMPL_CANT_CREATE), nullptr);

		if (!succeeded)	return false;

		BuildTemplateGuiList(&this->templates, vscroll[1], _local_company, this->sel_railtype);
		this->ToggleWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CLONE);
		ResetObjectToPlace();
		this->SetDirty();

		return true;
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		RailType temp = (RailType) index;
		if (temp == this->sel_railtype) return; // we didn't select a new one. No need to change anything
		this->sel_railtype = temp;
		/* Reset scrollbar positions */
		this->vscroll[0]->SetPosition(0);
		this->vscroll[1]->SetPosition(0);
		BuildTemplateGuiList(&this->templates, this->vscroll[1], this->owner, this->sel_railtype);
		this->SetDirty();
	}

	virtual void OnResize()
	{
		/* Top Matrix */
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(TRW_WIDGET_TOP_MATRIX);
		this->vscroll[0]->SetCapacityFromWidget(this, TRW_WIDGET_TOP_MATRIX);
		nwi->widget_data = (this->vscroll[0]->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
		/* Bottom Matrix */
		NWidgetCore *nwi2 = this->GetWidget<NWidgetCore>(TRW_WIDGET_BOTTOM_MATRIX);
		this->vscroll[1]->SetCapacityFromWidget(this, TRW_WIDGET_BOTTOM_MATRIX);
		nwi2->widget_data = (this->vscroll[1]->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
		/* Info panel */
		NWidgetCore *nwi3 = this->GetWidget<NWidgetCore>(TRW_WIDGET_TMPL_INFO_PANEL);
		this->vscroll[2]->SetCapacity(nwi3->current_y);
	}

	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		this->groups.ForceRebuild();
		this->templates.ForceRebuild();
		this->UpdateButtonState();
		this->SetDirty();
	}

	/** For a given group (id) find the template that is issued for template replacement for this group and return this template's index
	 *  from the gui list */
	short FindTemplateIndex(TemplateID tid) const
	{
		if (tid == INVALID_TEMPLATE) return -1;

		for (uint32 i = 0; i < this->templates.size(); ++i) {
			if (templates[i]->index == tid) {
				return i;
			}
		}
		return -1;
	}

	void AddParents(GUIGroupList *source, GroupID parent, int indent)
	{
		for (const Group *g : *source) {
			if (g->parent == parent) {
				this->groups.push_back(g);
				this->indents.push_back(indent);
				AddParents(source, g->index, indent + 1);
			}
		}
	}

	void BuildGroupList(Owner owner)
	{
		if (!this->groups.NeedRebuild()) return;

		this->groups.clear();
		this->indents.clear();

		GUIGroupList list;

		for (const Group *g : Group::Iterate()) {
			if (g->owner == owner && g->vehicle_type == VEH_TRAIN) {
				list.push_back(g);
			}
		}

		list.ForceResort();
		extern bool GroupNameSorter(const Group * const &a, const Group * const &b);
		list.Sort(&GroupNameSorter);

		AddParents(&list, INVALID_GROUP, 0);

		this->groups.shrink_to_fit();
		this->groups.RebuildDone();
		this->vscroll[0]->SetCount(groups.size());
	}

	void DrawAllGroupsFunction(const Rect &r) const
	{
		int left = r.left + WD_MATRIX_LEFT;
		int right = r.right - WD_MATRIX_RIGHT;
		int y = r.top;
		int max = min(this->vscroll[0]->GetPosition() + this->vscroll[0]->GetCapacity(), this->groups.size());

		/* Then treat all groups defined by/for the current company */
		for (int i = this->vscroll[0]->GetPosition(); i < max; ++i) {
			const Group *g = (this->groups)[i];
			short g_id = g->index;

			/* Fill the background of the current cell in a darker tone for the currently selected template */
			if (this->selected_group_index == i) {
				GfxFillRect(r.left + 1, y, r.right, y + WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + WD_MATRIX_BOTTOM, _colour_gradient[COLOUR_GREY][3]);
			}

			int text_y = y + WD_MATRIX_TOP;

			SetDParam(0, g_id);
			StringID str = STR_GROUP_NAME;
			DrawString(left + ScaleGUITrad(30 + this->indents[i] * 10), right, text_y, str, TC_BLACK);

			const TemplateID tid = GetTemplateIDByGroupIDRecursive(g_id);
			const TemplateID tid_self = GetTemplateIDByGroupID(g_id);

			/* Draw the template in use for this group, if there is one */
			short template_in_use = FindTemplateIndex(tid);
			if (tid != INVALID_TEMPLATE && tid_self == INVALID_TEMPLATE) {
				DrawString (left, right, text_y, STR_TMP_TEMPLATE_FROM_PARENT_GROUP, TC_SILVER, SA_HOR_CENTER);
			} else if (template_in_use >= 0) {
				SetDParam(0, template_in_use);
				DrawString (left, right, text_y, STR_TMPL_GROUP_USES_TEMPLATE, TC_BLACK, SA_HOR_CENTER);
			} else if (tid != INVALID_TEMPLATE) { /* If there isn't a template applied from the current group, check if there is one for another rail type */
				DrawString (left, right, text_y, STR_TMPL_TMPLRPL_EX_DIFF_RAILTYPE, TC_SILVER, SA_HOR_CENTER);
			}

			/* Draw the number of trains that still need to be treated by the currently selected template replacement */
			if (tid != INVALID_TEMPLATE) {
				const TemplateVehicle *tv = TemplateVehicle::Get(tid);
				const int num_trains = NumTrainsNeedTemplateReplacement(g_id, tv);
				// Draw number
				SetDParam(0, num_trains);
				int inner_right = DrawString(left, right - ScaleGUITrad(4), text_y, STR_JUST_INT, num_trains ? TC_ORANGE : TC_GREY, SA_RIGHT);
				// Draw text
				DrawString(left, inner_right - ScaleFontTrad(4), text_y, STR_TMPL_NUM_TRAINS_NEED_RPL, num_trains ? TC_BLACK : TC_GREY, SA_RIGHT);
			}

			y += WD_MATRIX_TOP + FONT_HEIGHT_NORMAL + WD_MATRIX_BOTTOM;
		}
	}

	void DrawTemplateList(const Rect &r) const
	{
		int left = r.left;
		int right = r.right;
		int y = r.top;

		Scrollbar *draw_vscroll = vscroll[1];
		uint max = min(draw_vscroll->GetPosition() + draw_vscroll->GetCapacity(), this->templates.size());

		const TemplateVehicle *v;
		for (uint i = draw_vscroll->GetPosition(); i < max; ++i) {
			v = (this->templates)[i];

			/* Fill the background of the current cell in a darker tone for the currently selected template */
			if (this->selected_template_index == (int32) i) {
				GfxFillRect(left + 1, y, right, y + this->bottom_matrix_item_size, _colour_gradient[COLOUR_GREY][3]);
			}

			/* Draw the template */
			DrawTemplate(v, left + ScaleGUITrad(36), right - ScaleGUITrad(24), y);

			/* Draw a notification string for chains that are not runnable */
			if (v->IsFreeWagonChain()) {
				DrawString(left, right - ScaleGUITrad(24), y + ScaleGUITrad(2), STR_TMPL_WARNING_FREE_WAGON, TC_RED, SA_RIGHT);
			}

			bool buildable = true;
			for (const TemplateVehicle *u = v; u != nullptr; u = u->GetNextUnit()) {
				if (!IsEngineBuildable(u->engine_type, VEH_TRAIN, u->owner)) {
					buildable = false;
					break;
				}
			}
			/* Draw a notification string for chains that are not buildable */
			if (!buildable) {
				DrawString(left, right - ScaleGUITrad(24), y + ScaleGUITrad(2), STR_TMPL_WARNING_VEH_UNAVAILABLE, TC_RED, SA_CENTER);
			}

			/* Draw the template's length in tile-units */
			SetDParam(0, v->GetRealLength());
			SetDParam(1, 1);
			DrawString(left, right - ScaleGUITrad(4), y + ScaleGUITrad(2), STR_TINY_BLACK_DECIMAL, TC_BLACK, SA_RIGHT);

			int bottom_edge = y + this->bottom_matrix_item_size - FONT_HEIGHT_NORMAL - WD_FRAMERECT_BOTTOM;

			/* Buying cost */
			SetDParam(0, CalculateOverallTemplateCost(v));
			DrawString(left + ScaleGUITrad(35), right, bottom_edge, STR_TMPL_TEMPLATE_OVR_VALUE_notinyfont, TC_BLUE, SA_LEFT);

			/* Index of current template vehicle in the list of all templates for its company */
			SetDParam(0, i);
			DrawString(left + ScaleGUITrad(5), left + ScaleGUITrad(25), y + ScaleGUITrad(2), STR_BLACK_INT, TC_BLACK, SA_RIGHT);

			/* Draw whether the current template is in use by any group */
			if (v->NumGroupsUsingTemplate() > 0) {
				DrawString(left + ScaleGUITrad(35), right, bottom_edge - FONT_HEIGHT_NORMAL - WD_FRAMERECT_BOTTOM,
						STR_TMP_TEMPLATE_IN_USE, TC_GREEN, SA_LEFT);
			}

			/* Draw information about template configuration settings */
			TextColour color;

			color = v->IsSetReuseDepotVehicles() ? TC_LIGHT_BLUE : TC_GREY;
			DrawString(right - ScaleFontTrad(300), right, bottom_edge, STR_TMPL_CONFIG_USEDEPOT, color, SA_LEFT);

			color = v->IsSetKeepRemainingVehicles() ? TC_LIGHT_BLUE : TC_GREY;
			DrawString(right - ScaleFontTrad(225), right, bottom_edge, STR_TMPL_CONFIG_KEEPREMAINDERS, color, SA_LEFT);

			color = v->IsSetRefitAsTemplate() ? TC_LIGHT_BLUE : TC_GREY;
			DrawString(right - ScaleFontTrad(150), right, bottom_edge, STR_TMPL_CONFIG_REFIT, color, SA_LEFT);

			color = v->IsReplaceOldOnly() ? TC_LIGHT_BLUE : TC_GREY;
			DrawString(right - ScaleFontTrad(75), right, bottom_edge, STR_TMPL_CONFIG_OLD_ONLY, color, SA_LEFT);

			y += this->bottom_matrix_item_size;
		}
	}

	void DrawTemplateInfo(const Rect &r) const
	{
		if ((this->selected_template_index < 0) || (this->selected_template_index >= (short)this->templates.size())) {
			return;
		}

		DrawPixelInfo tmp_dpi, *old_dpi;

		if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left, r.bottom - r.top)) {
			return;
		}

		old_dpi = _cur_dpi;
		_cur_dpi = &tmp_dpi;

		const TemplateVehicle *tmp = this->templates[this->selected_template_index];

		/* Draw vehicle performance info */
		SetDParam(2, tmp->max_speed);
		SetDParam(1, tmp->power);
		SetDParam(0, tmp->weight);
		SetDParam(3, tmp->max_te);
		DrawString(8, r.right, ScaleGUITrad(4) - this->vscroll[2]->GetPosition(), STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE);

		/* Draw cargo summary */
		short top = ScaleGUITrad(30) - this->vscroll[2]->GetPosition();
		short left = ScaleGUITrad(8);
		short count_columns = 0;
		short max_columns = 2;

		CargoArray cargo_caps;
		for (; tmp != nullptr; tmp = tmp->Next()) {
			cargo_caps[tmp->cargo_type] += tmp->cargo_cap;
		}
		int x = left;
		for (CargoID i = 0; i < NUM_CARGO; ++i) {
			if (cargo_caps[i] > 0) {
				count_columns++;
				SetDParam(0, i);
				SetDParam(1, cargo_caps[i]);
				SetDParam(2, _settings_game.vehicle.freight_trains);
				DrawString(x, r.right, top, FreightWagonMult(i) > 1 ? STR_TMPL_CARGO_SUMMARY_MULTI : STR_TMPL_CARGO_SUMMARY, TC_LIGHT_BLUE, SA_LEFT);
				x += ScaleGUITrad(250);
				if (count_columns % max_columns == 0) {
					x = left;
					top += FONT_HEIGHT_NORMAL;
				}
			}
		}

		_cur_dpi = old_dpi;
	}

	void UpdateButtonState()
	{
		bool selected_ok = (this->selected_template_index >= 0) && (this->selected_template_index < (short)this->templates.size());
		bool group_ok = (this->selected_group_index >= 0) && (this->selected_group_index < (short)this->groups.size());

		short g_id = -1;
		if (group_ok) {
			const Group *g = (this->groups)[this->selected_group_index];
			g_id = g->index;
		}

		const TemplateID tid = GetTemplateIDByGroupID(g_id);

		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_EDIT, this->editInProgress || !selected_ok);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_DELETE, this->editInProgress || !selected_ok);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE, this->editInProgress || !selected_ok);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP, this->editInProgress ||!selected_ok);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT, this->editInProgress ||!selected_ok);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY, this->editInProgress ||!selected_ok);

		this->SetWidgetDisabledState(TRW_WIDGET_START, this->editInProgress || !(selected_ok && group_ok && FindTemplateIndex(tid) != this->selected_template_index));
		this->SetWidgetDisabledState(TRW_WIDGET_STOP, this->editInProgress || !(group_ok && tid != INVALID_TEMPLATE));

		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_DEFINE, this->editInProgress);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CLONE, this->editInProgress);
		this->SetWidgetDisabledState(TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN, this->editInProgress);
	}
};

void ShowTemplateReplaceWindow(uint unitnumber_digits)
{
	new TemplateReplaceWindow(&_replace_rail_vehicle_desc, unitnumber_digits);
}
