/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_gui_main.cpp Template-based train replacement: main GUI. */

#include "stdafx.h"
#include "autoreplace_func.h"
#include "command_func.h"
#include "company_func.h"
#include "core/backup_type.hpp"
#include "core/geometry_func.hpp"
#include "core/pool_func.hpp"
#include "engine_base.h"
#include "engine_func.h"
#include "engine_gui.h"
#include "gfx_type.h"
#include "group.h"
#include "network/network.h"
#include "newgrf_engine.h"
#include "rail_gui.h"
#include "rail.h"
#include "settings_func.h"
#include "spritecache.h"
#include "strings_func.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "textbuf_gui.h"
#include "tilehighlight_func.h"
#include "train.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "vehicle_gui_base.h"
#include "vehicle_gui.h"
#include "viewport_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "zoom_func.h"
#include "group_gui_list.h"

#include "tbtr_template_gui_main.h"
#include "tbtr_template_gui_create.h"
#include "tbtr_template_vehicle.h"

#include <iostream>
#include <stdio.h>

#include "safeguards.h"

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

	TRW_WIDGET_TMPL_CONFIG_HEADER,

	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP,
	TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY,
	TRW_WIDGET_TMPL_BUTTONS_CONFIG_RIGHTPANEL,

	TRW_WIDGET_TMPL_BUTTONS_DEFINE,
	TRW_WIDGET_TMPL_BUTTONS_EDIT,
	TRW_WIDGET_TMPL_BUTTONS_CLONE,
	TRW_WIDGET_TMPL_BUTTONS_DELETE,
	TRW_WIDGET_TMPL_BUTTONS_RENAME,

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

static constexpr NWidgetPart _widgets[] = {
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
			NWidget(WWT_MATRIX, COLOUR_GREY, TRW_WIDGET_TOP_MATRIX), SetMinimalSize(216, 0), SetFill(1, 1), SetDataTip(0x1, STR_NULL), SetResize(1, 0), SetScrollbar(TRW_WIDGET_TOP_SCROLLBAR),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TRW_WIDGET_TOP_SCROLLBAR),
		EndContainer(),
	EndContainer(),
	// Template Display
	NWidget(NWID_VERTICAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, TRW_WIDGET_INSET_TEMPLATES), SetPadding(2, 2, 2, 2), SetResize(1, 0), SetDataTip(STR_TMPL_AVAILABLE_TEMPLATES, STR_NULL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, TRW_WIDGET_BOTTOM_MATRIX), SetMinimalSize(216, 0), SetFill(1, 1), SetDataTip(0x1, STR_NULL), SetResize(1, 1), SetScrollbar(TRW_WIDGET_MIDDLE_SCROLLBAR),
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
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL),
			// Config header
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_CONFIG_HEADER), SetMinimalSize(0, 12), SetFill(1, 0), SetResize(1, 0), EndContainer(),
			// Config buttons
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE), SetMinimalSize(100, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_SET_REFIT_AS_TEMPLATE, STR_TMPL_SET_REFIT_AS_TEMPLATE_TIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING), SetMinimalSize(100, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_SET_REFIT_AS_INCOMING, STR_TMPL_SET_REFIT_AS_INCOMING_TIP),
				NWidget(NWID_SPACER), SetFill(0, 0), SetMinimalSize(2, 0), SetResize(0, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE), SetMinimalSize(100, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_SET_USEDEPOT, STR_TMPL_SET_USEDEPOT_TIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP), SetMinimalSize(100, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_SET_KEEPREMAINDERS, STR_TMPL_SET_KEEPREMAINDERS_TIP),
				NWidget(NWID_SPACER), SetFill(0, 0), SetMinimalSize(2, 0), SetResize(0, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY), SetMinimalSize(100, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_SET_OLD_ONLY, STR_TMPL_SET_OLD_ONLY_TIP),
				NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CONFIG_RIGHTPANEL), SetMinimalSize(12, 12), SetFill(0, 0), SetResize(0, 0), EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 0), SetMinimalSize(0, 2), SetResize(1, 0),
			// Edit buttons
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_DEFINE), SetMinimalSize(75, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_DEFINE_TEMPLATE, STR_TMPL_DEFINE_TEMPLATE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_EDIT), SetMinimalSize(75, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_EDIT_TEMPLATE, STR_TMPL_EDIT_TEMPLATE),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_CLONE), SetMinimalSize(75, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_CREATE_CLONE_VEH, STR_TMPL_CREATE_CLONE_VEH),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_DELETE), SetMinimalSize(75, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TMPL_DELETE_TEMPLATE, STR_TMPL_DELETE_TEMPLATE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_RENAME), SetMinimalSize(75, 12), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_BUTTON_RENAME, STR_TMPL_RENAME_TEMPLATE),
				NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TMPL_BUTTONS_EDIT_RIGHTPANEL), SetMinimalSize(12, 12), SetFill(0, 0), SetResize(0, 0), EndContainer(),
			EndContainer(),
		EndContainer(),
		// Start/Stop buttons
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_START), SetMinimalSize(150, 12), SetDataTip(STR_TMPL_RPL_START, STR_TMPL_RPL_START_TOOLTIP),
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TRAIN_FLUFF_LEFT), SetMinimalSize(15, 12), EndContainer(),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN), SetMinimalSize(150, 12), SetDataTip(0x0, STR_REPLACE_HELP_RAILTYPE), SetResize(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, TRW_WIDGET_TRAIN_FLUFF_RIGHT), SetMinimalSize(16, 12), EndContainer(),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TRW_WIDGET_STOP), SetMinimalSize(150, 12), SetDataTip(STR_TMPL_RPL_STOP, STR_TMPL_RPL_STOP_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _replace_rail_vehicle_desc(__FILE__, __LINE__,
	WDP_AUTO,
	"template replace window",
	456, 156,
	WC_TEMPLATEGUI_MAIN,
	WC_NONE,                     // parent window class
	WDF_CONSTRUCTION,
	std::begin(_widgets), std::end(_widgets)
);

enum {
	TRW_LEFT_OFFSET = 36,
	TRW_RIGHT_OFFSET = 30,
	TRW_GAP = 10,
};

class TemplateReplaceWindow : public Window {
private:

	GUIGroupList groups;          ///< List of groups

	std::vector<int> indents; ///< Indentation levels

	int bottom_matrix_item_size = 0;

	int details_height;           ///< Minimal needed height of the details panels (found so far).
	RailType sel_railtype;        ///< Type of rail tracks selected.
	Scrollbar *vscroll[3];
	// listing/sorting continued
	GUITemplateList templates;
	GUITemplateList::SortFunction **template_sorter_funcs;

	int selected_template_index;
	int selected_group_index;

	bool editInProgress;

	uint buy_cost_width = 0;
	uint refit_text_width = 0;
	uint depot_text_width = 0;
	uint remainder_text_width = 0;
	uint old_text_width = 0;

public:
	TemplateReplaceWindow(WindowDesc *wdesc) : Window(wdesc)
	{
		// listing/sorting
		templates.SetSortFuncs(this->template_sorter_funcs);

		this->sel_railtype = INVALID_RAILTYPE;

		this->details_height = 10 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical();

		this->CreateNestedTree();
		this->vscroll[0] = this->GetScrollbar(TRW_WIDGET_TOP_SCROLLBAR);
		this->vscroll[1] = this->GetScrollbar(TRW_WIDGET_MIDDLE_SCROLLBAR);
		this->vscroll[2] = this->GetScrollbar(TRW_WIDGET_BOTTOM_SCROLLBAR);
		this->FinishInitNested(VEH_TRAIN);

		this->owner = _local_company;

		this->groups.ForceRebuild();
		this->groups.NeedResort();
		this->BuildGroupList();

		this->selected_template_index = -1;
		this->selected_group_index = -1;

		this->UpdateButtonState();

		this->editInProgress = false;

		this->templates.ForceRebuild();

		this->templates.ForceRebuild();
		this->BuildTemplateGuiList();
	}

	void Close(int data = 0) override {
		CloseWindowById(WC_CREATE_TEMPLATE, this->window_number);
		this->Window::Close();
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case TRW_WIDGET_TOP_MATRIX:
				resize->height = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical();
				size->height = 8 * resize->height;
				break;
			case TRW_WIDGET_BOTTOM_MATRIX: {
				int base_resize = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical();
				int target_resize = WidgetDimensions::scaled.matrix.top + GetCharacterHeight(FS_NORMAL) + ScaleGUITrad(GetVehicleHeight(VEH_TRAIN));
				this->bottom_matrix_item_size = resize->height = CeilT<int>(target_resize, base_resize);
				size->height = 4 * resize->height;

				int gap = ScaleGUITrad(TRW_GAP);

				SetDParamMaxDigits(0, 8);
				this->buy_cost_width = GetStringBoundingBox(STR_TMPL_TEMPLATE_OVR_VALUE).width + gap;

				this->refit_text_width = maxdim(GetStringBoundingBox(STR_TMPL_CONFIG_REFIT_AS_TEMPLATE), GetStringBoundingBox(STR_TMPL_CONFIG_REFIT_AS_INCOMING)).width;
				this->depot_text_width = GetStringBoundingBox(STR_TMPL_CONFIG_USEDEPOT).width + gap;
				this->remainder_text_width = GetStringBoundingBox(STR_TMPL_CONFIG_KEEPREMAINDERS).width + gap;
				this->old_text_width = GetStringBoundingBox(STR_TMPL_CONFIG_OLD_ONLY).width + gap;

				/* use buy cost width as nominal width for name field */
				uint left_side = ScaleGUITrad(TRW_LEFT_OFFSET) + this->buy_cost_width * 2;
				uint right_side = this->refit_text_width + this->depot_text_width + this->remainder_text_width + this->old_text_width + ScaleGUITrad(TRW_RIGHT_OFFSET);
				size->width = std::max(size->width, left_side + gap + right_side);
				break;
			}
			case TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN: {
				Dimension d = GetStringBoundingBox(STR_REPLACE_ALL_RAILTYPE);
				for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
					const RailTypeInfo *rti = GetRailTypeInfo(rt);
					// Skip rail type if it has no label
					if (rti->label == 0) continue;
					d = maxdim(d, GetStringBoundingBox(rti->strings.replace_text));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case TRW_WIDGET_TMPL_CONFIG_HEADER:
				size->height = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical();
				break;
			case TRW_WIDGET_TMPL_BUTTONS_CONFIG_RIGHTPANEL:
			case TRW_WIDGET_TMPL_BUTTONS_EDIT_RIGHTPANEL:
				size->width = std::max(size->width, NWidgetLeaf::GetResizeBoxDimension().width);
				break;
		}
	}

	virtual void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case TRW_CAPTION:
				SetDParam(0, STR_TMPL_RPL_TITLE);
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, WidgetID widget) const override
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
			case TRW_WIDGET_TMPL_CONFIG_HEADER: {
				auto draw_label = [&](int widget_1, int widget_2, StringID str) {
					Rect lr = this->GetWidget<NWidgetBase>(widget_1)->GetCurrentRect();
					if (widget_2 != 0) lr = BoundingRect(lr, this->GetWidget<NWidgetBase>(widget_2)->GetCurrentRect());
					DrawString(lr.left, lr.right, r.top + WidgetDimensions::scaled.framerect.top, str, TC_FROMSTRING, SA_CENTER);
				};
				draw_label(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING, STR_TMPL_SECTION_REFIT);
				draw_label(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP, STR_TMPL_SECTION_DEPOT_VEHICLES);
				draw_label(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY, 0, STR_TMPL_SECTION_WHEN);
				break;
			}
		}
	}

	virtual void OnPaint() override
	{
		this->BuildGroupList();
		this->BuildTemplateGuiList();

		/* sets the colour of that art thing */
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_FLUFF_LEFT)->colour  = _company_colours[_local_company];
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_FLUFF_RIGHT)->colour = _company_colours[_local_company];

		/* Show the selected railtype in the pulldown menu */
		this->GetWidget<NWidgetCore>(TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN)->widget_data = (this->sel_railtype == INVALID_RAILTYPE) ? STR_REPLACE_ALL_RAILTYPE : GetRailTypeInfo(this->sel_railtype)->strings.replace_text;

		if ((this->selected_template_index < 0) || (this->selected_template_index >= (int)this->templates.size())) {
			this->vscroll[2]->SetCount(24);
			this->SetWidgetsLoweredState(false,
					TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING,
					TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE, TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP,
					TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY);
		} else {
			const TemplateVehicle *tmp = this->templates[this->selected_template_index];
			uint height = ScaleGUITrad(8) + (3 * GetCharacterHeight(FS_NORMAL));
			CargoArray cargo_caps{};
			uint count_columns = 0;
			uint max_columns = 2;

			if (tmp->full_weight > tmp->empty_weight || _settings_client.gui.show_train_weight_ratios_in_details) height += GetCharacterHeight(FS_NORMAL);
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) height += GetCharacterHeight(FS_NORMAL);

			for (const TemplateVehicle *u = tmp; u != nullptr; u = u->Next()) {
				cargo_caps[u->cargo_type] += u->cargo_cap;
			}

			for (CargoID i = 0; i < NUM_CARGO; ++i) {
				if (cargo_caps[i] > 0) {
					if (count_columns % max_columns == 0) {
						height += GetCharacterHeight(FS_NORMAL);
					}

					++count_columns;
				}
			}

			this->vscroll[2]->SetCount(height);

			this->SetWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE, tmp->IsSetRefitAsTemplate());
			this->SetWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING, !tmp->IsSetRefitAsTemplate());
			this->SetWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE, tmp->IsSetReuseDepotVehicles());
			this->SetWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP, tmp->IsSetKeepRemainingVehicles());
			this->SetWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY, tmp->IsReplaceOldOnly());
		}

		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (this->editInProgress) return;

		this->BuildGroupList();
		this->BuildTemplateGuiList();

		switch (widget) {
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size())) {
					uint32_t template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_REUSE_DEPOT_VEHICLES, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size())) {
					uint32_t template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, 0, CMD_TOGGLE_KEEP_REMAINING_VEHICLES, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE:
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size())) {
					uint32_t template_index = ((this->templates)[selected_template_index])->index;

					DoCommandP(0, template_index, (widget == TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE) ? 1 : 0, CMD_SET_REFIT_AS_TEMPLATE, nullptr);
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size())) {
					uint32_t template_index = ((this->templates)[selected_template_index])->index;

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
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size())) {
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
					SetObjectToPlaceWnd(SPR_CURSOR_CLONE_TRAIN, PAL_NONE, HT_VEHICLE, this);
				} else {
					ResetObjectToPlace();
				}
				break;
			}
			case TRW_WIDGET_TMPL_BUTTONS_DELETE:
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size()) && !editInProgress) {

					uint32_t template_index = ((this->templates)[selected_template_index])->index;

					bool succeeded = DoCommandP(0, template_index, 0, CMD_DELETE_TEMPLATE_VEHICLE, nullptr);

					if (succeeded) {
						this->templates.ForceRebuild();
						selected_template_index = -1;
					}
				}
				break;
			case TRW_WIDGET_TMPL_BUTTONS_RENAME:
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size()) && !editInProgress) {
					const TemplateVehicle *tmp = this->templates[this->selected_template_index];
					SetDParamStr(0, tmp->name);
					ShowQueryString(STR_JUST_RAW_STRING, STR_TMPL_RENAME_TEMPLATE, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				}
				break;
			case TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN: // Railtype selection dropdown menu
				ShowDropDownList(this, GetRailTypeDropDownList(true, true), this->sel_railtype, TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN);
				break;
			case TRW_WIDGET_TOP_MATRIX: {
				uint16_t newindex = (uint16_t)((pt.y - this->GetWidget<NWidgetBase>(TRW_WIDGET_TOP_MATRIX)->pos_y) / (GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical()) ) + this->vscroll[0]->GetPosition();
				if (newindex == this->selected_group_index || newindex >= this->groups.size()) {
					this->selected_group_index = -1;
				} else if (newindex < this->groups.size()) {
					this->selected_group_index = newindex;
				}
				this->UpdateButtonState();
				break;
			}
			case TRW_WIDGET_BOTTOM_MATRIX: {
				uint16_t newindex = (uint16_t)((pt.y - this->GetWidget<NWidgetBase>(TRW_WIDGET_BOTTOM_MATRIX)->pos_y) / this->bottom_matrix_item_size) + this->vscroll[1]->GetPosition();
				if (newindex == this->selected_template_index || newindex >= templates.size()) {
					this->selected_template_index = -1;
				} else if (newindex < templates.size()) {
					const TemplateVehicle *tmp = this->templates[newindex];
					if (tmp != nullptr && TemplateVehicleClicked(tmp)) return;
					this->selected_template_index = newindex;
				}
				this->UpdateButtonState();
				break;
			}
			case TRW_WIDGET_START: {
				if ((this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size()) &&
						(this->selected_group_index >= 0) && (this->selected_group_index < (int)this->groups.size())) {
					uint32_t tv_index = ((this->templates)[selected_template_index])->index;
					int current_group_index = (this->groups)[this->selected_group_index]->index;

					DoCommandP(0, current_group_index, tv_index, CMD_ISSUE_TEMPLATE_REPLACEMENT, nullptr);
					this->UpdateButtonState();
				}
				break;
			}
			case TRW_WIDGET_STOP:
				if ((this->selected_group_index < 0) || (this->selected_group_index >= (int)this->groups.size())) {
					return;
				}

				int current_group_index = (this->groups)[this->selected_group_index]->index;

				DoCommandP(0, current_group_index, 0, CMD_DELETE_TEMPLATE_REPLACEMENT, nullptr);
				this->UpdateButtonState();
				break;
		}
		this->SetDirty();
	}

	virtual bool OnVehicleSelect(const Vehicle *v) override
	{
		bool succeeded = DoCommandP(0, v->index, 0, CMD_CLONE_TEMPLATE_VEHICLE_FROM_TRAIN | CMD_MSG(STR_TMPL_CANT_CREATE), nullptr);

		if (!succeeded)	return false;

		this->templates.ForceRebuild();
		this->ToggleWidgetLoweredState(TRW_WIDGET_TMPL_BUTTONS_CLONE);
		ResetObjectToPlace();
		this->SetDirty();

		return true;
	}

	virtual void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	virtual void OnDropdownSelect(WidgetID widget, int index) override
	{
		RailType temp = (RailType) index;
		if (temp == this->sel_railtype) return; // we didn't select a new one. No need to change anything
		this->sel_railtype = temp;
		/* Reset scrollbar positions */
		this->vscroll[0]->SetPosition(0);
		this->vscroll[1]->SetPosition(0);
		this->templates.ForceRebuild();
		this->SetDirty();
	}

	virtual void OnResize() override
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

	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		this->groups.ForceRebuild();
		this->templates.ForceRebuild();
		this->UpdateButtonState();
		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str != nullptr && (this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size()) && !editInProgress) {
			const TemplateVehicle *tmp = this->templates[this->selected_template_index];
			DoCommandP(0, tmp->index, 0, CMD_RENAME_TMPL_REPLACE | CMD_MSG(STR_TMPL_CANT_RENAME), nullptr, str);
		}
	}

	/** For a given group (id) find the template that is issued for template replacement for this group and return this template's index
	 *  from the gui list */
	int FindTemplateIndex(TemplateID tid) const
	{
		if (tid == INVALID_TEMPLATE) return -1;

		for (uint32_t i = 0; i < this->templates.size(); ++i) {
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

	void BuildGroupList()
	{
		if (!this->groups.NeedRebuild()) return;

		this->groups.clear();
		this->indents.clear();

		GUIGroupList list;

		for (const Group *g : Group::Iterate()) {
			if (g->owner == this->owner && g->vehicle_type == VEH_TRAIN) {
				list.push_back(g);
			}
		}

		list.ForceResort();
		SortGUIGroupList(list);

		AddParents(&list, INVALID_GROUP, 0);

		this->groups.shrink_to_fit();
		this->groups.RebuildDone();
		this->vscroll[0]->SetCount((uint)groups.size());
	}

	void BuildTemplateGuiList()
	{
		if (!this->templates.NeedRebuild()) return;

		::BuildTemplateGuiList(&this->templates, this->vscroll[1], this->owner, this->sel_railtype);
	}

	void DrawAllGroupsFunction(const Rect &r) const
	{
		int left = r.left + WidgetDimensions::scaled.matrix.left;
		int right = r.right - WidgetDimensions::scaled.matrix.right;
		int y = r.top;
		int max = std::min<int>(this->vscroll[0]->GetPosition() + this->vscroll[0]->GetCapacity(), (int)this->groups.size());

		bool rtl = _current_text_dir == TD_RTL;

		/* Then treat all groups defined by/for the current company */
		for (int i = this->vscroll[0]->GetPosition(); i < max; ++i) {
			const Group *g = (this->groups)[i];
			GroupID g_id = g->index;

			/* Fill the background of the current cell in a darker tone for the currently selected template */
			if (this->selected_group_index == i) {
				GfxFillRect(r.left + 1, y, r.right, y + GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical(), GetColourGradient(COLOUR_GREY, SHADE_DARK));
			}

			int text_y = y + WidgetDimensions::scaled.matrix.top;

			auto draw_text = [&](int left, int right, StringID str, TextColour colour, StringAlignment align) {
				if (rtl) {
					DrawString(r.left + (r.right - right), r.right - (left - r.left), text_y, str, colour, align);
				} else {
					DrawString(left, right, text_y, str, colour, align);
				}
			};

			int col1 = left + (2 * left + right) / 3;
			int col2 = left + (left + 2 * right) / 3;

			SetDParam(0, g_id);
			StringID str = STR_GROUP_NAME;
			draw_text(left + ScaleGUITrad(4 + this->indents[i] * 10), col1 - ScaleGUITrad(4), str, TC_BLACK, SA_LEFT);

			const TemplateID tid = GetTemplateIDByGroupIDRecursive(g_id);
			const TemplateID tid_self = GetTemplateIDByGroupID(g_id);

			/* Draw the template in use for this group, if there is one */
			int template_in_use = FindTemplateIndex(tid);
			if (tid != INVALID_TEMPLATE && tid_self == INVALID_TEMPLATE) {
				draw_text(col1 + ScaleGUITrad(4), col2 - ScaleGUITrad(4), STR_TMP_TEMPLATE_FROM_PARENT_GROUP, TC_SILVER, SA_HOR_CENTER);
			} else if (template_in_use >= 0) {
				const TemplateVehicle *tv = TemplateVehicle::Get(tid);
				SetDParam(1, template_in_use);
				if (tv->name.empty()) {
					SetDParam(0, STR_JUST_INT);
				} else {
					SetDParam(0, STR_TMPL_NAME);
					SetDParamStr(2, tv->name);
				}
				draw_text(col1 + ScaleGUITrad(4), col2 - ScaleGUITrad(4), STR_TMPL_GROUP_USES_TEMPLATE, TC_BLACK, SA_HOR_CENTER);
			} else if (tid != INVALID_TEMPLATE) { /* If there isn't a template applied from the current group, check if there is one for another rail type */
				draw_text(col1 + ScaleGUITrad(4), col2 - ScaleGUITrad(4), STR_TMPL_TMPLRPL_EX_DIFF_RAILTYPE, TC_SILVER, SA_HOR_CENTER);
			}

			/* Draw the number of trains that still need to be treated by the currently selected template replacement */
			if (tid != INVALID_TEMPLATE) {
				const TemplateVehicle *tv = TemplateVehicle::Get(tid);
				const uint num_trains = CountsTrainsNeedingTemplateReplacement(g_id, tv);
				SetDParam(0, num_trains > 0 ? TC_ORANGE : TC_GREY);
				SetDParam(1, num_trains);
				draw_text(col2 + ScaleGUITrad(4), right - ScaleGUITrad(4), STR_TMPL_NUM_TRAINS_NEED_RPL, num_trains > 0 ? TC_BLACK : TC_GREY, SA_RIGHT);
			}

			y += GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical();
		}
	}

	void DrawTemplateList(const Rect &r) const
	{
		if (!_template_vehicle_images_valid) UpdateAllTemplateVehicleImages();

		const_cast<TemplateReplaceWindow *>(this)->BuildTemplateGuiList();

		int y = r.top;

		Scrollbar *draw_vscroll = vscroll[1];
		uint max = std::min<uint>(draw_vscroll->GetPosition() + draw_vscroll->GetCapacity(), (uint)this->templates.size());

		bool rtl = _current_text_dir == TD_RTL;

		const TemplateVehicle *v;
		for (uint i = draw_vscroll->GetPosition(); i < max; ++i) {
			v = (this->templates)[i];

			/* Fill the background of the current cell in a darker tone for the currently selected template */
			if (this->selected_template_index == (int32_t) i) {
				GfxFillRect(r.left + 1, y, r.right, y + this->bottom_matrix_item_size, GetColourGradient(COLOUR_GREY, SHADE_DARK));
			}

			/* Draw the template */
			DrawTemplate(v, r.left + ScaleGUITrad(rtl ? TRW_RIGHT_OFFSET : TRW_LEFT_OFFSET), r.right - ScaleGUITrad(rtl ? TRW_LEFT_OFFSET : TRW_RIGHT_OFFSET), y, ScaleGUITrad(15));

			auto draw_text_across = [&](int left_offset, int right_offset, int y_offset, StringID str, TextColour colour, StringAlignment align, FontSize fontsize = FS_NORMAL) {
				DrawString(r.left + (rtl ? right_offset : left_offset), r.right - (rtl ? left_offset : right_offset), y + y_offset, str, colour, align, false, fontsize);
			};

			auto draw_text_left = [&](int left_offset, int left_offset_end, int y_offset, StringID str, TextColour colour, StringAlignment align, FontSize fontsize = FS_NORMAL) {
				int left = (rtl ? (r.right - left_offset_end) : (r.left + left_offset));
				DrawString(left, left + (left_offset_end - left_offset), y + y_offset, str, colour, align, false, fontsize);
			};

			auto draw_text_right = [&](int right_offset, int right_offset_end, int y_offset, StringID str, TextColour colour, StringAlignment align, FontSize fontsize = FS_NORMAL) {
				int left = (rtl ? (r.left + right_offset_end) : (r.right - right_offset));
				DrawString(left, left + (right_offset - right_offset_end), y + y_offset, str, colour, align, false, fontsize);
			};

			/* Draw a notification string for chains that are not runnable */
			if (v->IsFreeWagonChain()) {
				draw_text_across(0, ScaleGUITrad(TRW_RIGHT_OFFSET), ScaleGUITrad(2), STR_TMPL_WARNING_FREE_WAGON, TC_RED, SA_RIGHT);
			}

			bool buildable = true;
			RailTypes types = static_cast<RailTypes>(UINT64_MAX);
			for (const TemplateVehicle *u = v; u != nullptr; u = u->GetNextUnit()) {
				if (!IsEngineBuildable(u->engine_type, VEH_TRAIN, u->owner)) {
					buildable = false;
					break;
				} else {
					types &= (GetRailTypeInfo(Engine::Get(u->engine_type)->u.rail.railtype))->compatible_railtypes;
				}
			}
			/* Draw a notification string for chains that are not buildable */
			if (!buildable) {
				draw_text_across(0, ScaleGUITrad(TRW_RIGHT_OFFSET), ScaleGUITrad(2), STR_TMPL_WARNING_VEH_UNAVAILABLE, TC_RED, SA_CENTER);
			} else if (types == RAILTYPES_NONE) {
				draw_text_across(0, ScaleGUITrad(TRW_RIGHT_OFFSET), ScaleGUITrad(2), STR_TMPL_WARNING_VEH_NO_COMPATIBLE_RAIL_TYPE, TC_RED, SA_CENTER);
			}

			/* Draw the template's length in tile-units */
			SetDParam(0, v->GetRealLength());
			SetDParam(1, 1);
			draw_text_across(0, ScaleGUITrad(4), ScaleGUITrad(2), STR_JUST_DECIMAL, TC_BLACK, SA_RIGHT, FS_SMALL);

			int bottom_edge = this->bottom_matrix_item_size - GetCharacterHeight(FS_NORMAL) - WidgetDimensions::scaled.framerect.bottom;

			/* Buying cost */
			SetDParam(0, CalculateOverallTemplateCost(v));
			draw_text_left(ScaleGUITrad(TRW_LEFT_OFFSET), ScaleGUITrad(TRW_LEFT_OFFSET) + this->buy_cost_width, bottom_edge, STR_TMPL_TEMPLATE_OVR_VALUE, TC_BLUE, SA_LEFT);

			/* Index of current template vehicle in the list of all templates for its company */
			SetDParam(0, i);
			draw_text_left(ScaleGUITrad(5), ScaleGUITrad(25), ScaleGUITrad(2), STR_JUST_INT, TC_BLACK, SA_RIGHT);

			/* Draw whether the current template is in use by any group */
			if (v->NumGroupsUsingTemplate() > 0) {
				draw_text_across(ScaleGUITrad(TRW_LEFT_OFFSET), 0, ScaleGUITrad(2), STR_TMP_TEMPLATE_IN_USE, TC_GREEN, SA_LEFT);
			}

			/* Draw information about template configuration settings */

			int r_offset = ScaleGUITrad(TRW_LEFT_OFFSET);

			TextColour color;
			color = v->IsReplaceOldOnly() ? TC_LIGHT_BLUE : TC_GREY;
			draw_text_right(r_offset + this->old_text_width, r_offset, bottom_edge, STR_TMPL_CONFIG_OLD_ONLY, color, SA_RIGHT);
			r_offset += this->old_text_width;

			color = v->IsSetKeepRemainingVehicles() ? TC_LIGHT_BLUE : TC_GREY;
			draw_text_right(r_offset + this->remainder_text_width, r_offset, bottom_edge, STR_TMPL_CONFIG_KEEPREMAINDERS, color, SA_RIGHT);
			r_offset += this->remainder_text_width;

			color = v->IsSetReuseDepotVehicles() ? TC_LIGHT_BLUE : TC_GREY;
			draw_text_right(r_offset + this->depot_text_width, r_offset, bottom_edge, STR_TMPL_CONFIG_USEDEPOT, color, SA_RIGHT);
			r_offset += this->depot_text_width;

			draw_text_right(r_offset + this->refit_text_width, r_offset, bottom_edge, v->IsSetRefitAsTemplate() ? STR_TMPL_CONFIG_REFIT_AS_TEMPLATE : STR_TMPL_CONFIG_REFIT_AS_INCOMING, TC_FROMSTRING, SA_LEFT);
			r_offset += this->refit_text_width;

			if (!v->name.empty()) {
				SetDParamStr(0, v->name);
				draw_text_across(ScaleGUITrad(TRW_LEFT_OFFSET) + this->buy_cost_width, r_offset + ScaleGUITrad(TRW_GAP), bottom_edge, STR_JUST_RAW_STRING, TC_BLACK, SA_LEFT);
			}

			y += this->bottom_matrix_item_size;
		}
	}

	void DrawTemplateInfo(const Rect &r) const
	{
		if ((this->selected_template_index < 0) || (this->selected_template_index >= (int)this->templates.size())) {
			return;
		}

		DrawPixelInfo tmp_dpi;

		if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left, r.bottom - r.top)) {
			return;
		}

		AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);

		const TemplateVehicle *tmp = this->templates[this->selected_template_index];

		int top = ScaleGUITrad(4) - this->vscroll[2]->GetPosition();
		int left = ScaleGUITrad(8);
		int right = (r.right - r.left) - left;

		SetDParam(0, CalculateOverallTemplateDisplayRunningCost(tmp));
		DrawString(left, right, top, STR_TMPL_TEMPLATE_OVR_RUNNING_COST);
		top += GetCharacterHeight(FS_NORMAL);

		/* Draw vehicle performance info */
		const bool original_acceleration = (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ||
				GetRailTypeInfo(tmp->railtype)->acceleration_type == 2);
		SetDParam(2, tmp->max_speed);
		SetDParam(1, tmp->power);
		SetDParam(0, tmp->empty_weight);
		SetDParam(3, tmp->max_te / 1000);
		DrawString(left, right, top, original_acceleration ? STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED : STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE);

		if (tmp->full_weight > tmp->empty_weight || _settings_client.gui.show_train_weight_ratios_in_details) {
			top += GetCharacterHeight(FS_NORMAL);
			SetDParam(0, tmp->full_weight);
			if (_settings_client.gui.show_train_weight_ratios_in_details) {
				SetDParam(1, STR_VEHICLE_INFO_WEIGHT_RATIOS);
				SetDParam(2, STR_VEHICLE_INFO_POWER_WEIGHT_RATIO);
				SetDParam(3, (100 * tmp->power) / std::max<uint>(1, tmp->full_weight));
				SetDParam(4, GetRailTypeInfo(tmp->railtype)->acceleration_type == 2 ? STR_EMPTY : STR_VEHICLE_INFO_TE_WEIGHT_RATIO);
				SetDParam(5, (100 * tmp->max_te) / std::max<uint>(1, tmp->full_weight));
			} else {
				SetDParam(1, STR_EMPTY);
			}
			DrawString(8, right, top, STR_VEHICLE_INFO_FULL_WEIGHT_WITH_RATIOS);
		}
		if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
			top += GetCharacterHeight(FS_NORMAL);
			SetDParam(0, GetTemplateVehicleEstimatedMaxAchievableSpeed(tmp, tmp->full_weight, tmp->max_speed));
			DrawString(8, right, top, STR_VEHICLE_INFO_MAX_SPEED_LOADED);
		}

		/* Draw cargo summary */
		top += GetCharacterHeight(FS_NORMAL) * 2;
		int count_columns = 0;
		int max_columns = 2;

		CargoArray cargo_caps{};
		for (; tmp != nullptr; tmp = tmp->Next()) {
			cargo_caps[tmp->cargo_type] += tmp->cargo_cap;
		}
		int x = 0;
		int step = ScaleGUITrad(250);
		bool rtl = _current_text_dir == TD_RTL;
		for (CargoID i = 0; i < NUM_CARGO; ++i) {
			if (cargo_caps[i] > 0) {
				count_columns++;
				SetDParam(0, i);
				SetDParam(1, cargo_caps[i]);
				SetDParam(2, _settings_game.vehicle.freight_trains);
				int pos = rtl ? right - step - x : left + x;
				DrawString(pos, pos + step, top, FreightWagonMult(i) > 1 ? STR_TMPL_CARGO_SUMMARY_MULTI : STR_TMPL_CARGO_SUMMARY, TC_LIGHT_BLUE, SA_LEFT);
				x += step;
				if (count_columns % max_columns == 0) {
					x = 0;
					top += GetCharacterHeight(FS_NORMAL);
				}
			}
		}
	}

	void UpdateButtonState()
	{
		this->BuildGroupList();
		this->BuildTemplateGuiList();

		bool selected_ok = (this->selected_template_index >= 0) && (this->selected_template_index < (int)this->templates.size());
		bool group_ok = (this->selected_group_index >= 0) && (this->selected_group_index < (int)this->groups.size());

		GroupID g_id = -1;
		if (group_ok) {
			const Group *g = (this->groups)[this->selected_group_index];
			g_id = g->index;
		}

		const TemplateID tid = GetTemplateIDByGroupID(g_id);
		const bool disable_selection_buttons = this->editInProgress || !selected_ok;

		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_EDIT, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_DELETE, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_TEMPLATE, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REFIT_AS_INCOMING, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_REUSE, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_KEEP, disable_selection_buttons);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CONFIGTMPL_OLD_ONLY, disable_selection_buttons);

		this->SetWidgetDisabledState(TRW_WIDGET_START, this->editInProgress || !(selected_ok && group_ok && FindTemplateIndex(tid) != this->selected_template_index));
		this->SetWidgetDisabledState(TRW_WIDGET_STOP, this->editInProgress || !(group_ok && tid != INVALID_TEMPLATE));

		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_DEFINE, this->editInProgress);
		this->SetWidgetDisabledState(TRW_WIDGET_TMPL_BUTTONS_CLONE, this->editInProgress);
		this->SetWidgetDisabledState(TRW_WIDGET_TRAIN_RAILTYPE_DROPDOWN, this->editInProgress);
	}
};

void ShowTemplateReplaceWindow()
{
	if (BringWindowToFrontById(WC_TEMPLATEGUI_MAIN, 0) == nullptr) {
		new TemplateReplaceWindow(&_replace_rail_vehicle_desc);
	}
}

/**
 * Dispatch a "template vehicle selected" event if any window waits for it.
 * @param v selected vehicle;
 * @return did any window accept vehicle selection?
 */
bool TemplateVehicleClicked(const TemplateVehicle *v)
{
	assert(v != nullptr);
	if (!(_thd.place_mode & HT_VEHICLE)) return false;

	v = v->First();
	if (!v->IsPrimaryVehicle()) return false;

	return _thd.GetCallbackWnd()->OnTemplateVehicleSelect(v);
}
