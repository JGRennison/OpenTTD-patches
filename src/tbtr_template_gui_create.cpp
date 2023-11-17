/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_gui_create.cpp Template-based train replacement: template creation GUI. */

#include "stdafx.h"

#include "gfx_func.h"
#include "direction_type.h"

#include "strings_func.h"
#include "window_func.h"
#include "company_func.h"
#include "window_gui.h"
#include "settings_func.h"
#include "core/geometry_func.hpp"
#include "table/sprites.h"
#include "table/strings.h"
#include "viewport_func.h"
#include "window_func.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "depot_base.h"
#include "vehicle_gui.h"
#include "spritecache.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "window_gui.h"
#include "vehiclelist.h"
#include "order_backup.h"
#include "group.h"
#include "company_base.h"
#include "train.h"
#include "newgrf_debug.h"
#include "core/backup_type.hpp"

#include "tbtr_template_gui_create.h"
#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"

#include "safeguards.h"

class TemplateReplaceWindow;

// some space in front of the virtual train in the matrix
uint16 TRAIN_FRONT_SPACE = 16;

enum TemplateReplaceCreateWindowWidgets {
	TCW_CAPTION,
	TCW_NEW_TMPL_PANEL,
	TCW_INFO_PANEL,
	TCW_SCROLLBAR_H_NEW_TMPL,
	TCW_SCROLLBAR_V_NEW_TMPL,
	TCW_SELL_TMPL,
	TCW_NEW,
	TCW_OK,
	TCW_CANCEL,
	TCW_REFIT,
	TCW_CLONE,
};

static const NWidgetPart _widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, TCW_CAPTION), SetDataTip(STR_TMPL_CREATEGUI_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEBUGBOX, COLOUR_GREY),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_GREY, TCW_NEW_TMPL_PANEL), SetMinimalSize(250, 30), SetResize(1, 0), SetScrollbar(TCW_SCROLLBAR_H_NEW_TMPL), SetDataTip(STR_NULL, STR_DEPOT_TRAIN_LIST_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, TCW_INFO_PANEL), SetMinimalSize(250, 100), SetResize(1, 1), SetScrollbar(TCW_SCROLLBAR_V_NEW_TMPL), EndContainer(),
			NWidget(NWID_HSCROLLBAR, COLOUR_GREY, TCW_SCROLLBAR_H_NEW_TMPL),
		EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_GREY, TCW_SELL_TMPL), SetMinimalSize(40, 40), SetDataTip(0x0, STR_NULL), SetResize(0, 1), SetFill(0, 1),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, TCW_SCROLLBAR_V_NEW_TMPL),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_OK),     SetMinimalSize(52, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_CONFIRM,          STR_TMPL_CONFIRM),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_NEW),    SetMinimalSize(52, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_NEW,              STR_TMPL_NEW),
		NWidget(WWT_TEXTBTN,    COLOUR_GREY, TCW_CLONE),  SetMinimalSize(52, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_CREATE_CLONE_VEH, STR_TMPL_CREATE_CLONE_VEH),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_REFIT),  SetMinimalSize(52, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_REFIT,            STR_TMPL_REFIT),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, TCW_CANCEL), SetMinimalSize(52, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_CANCEL,           STR_TMPL_CANCEL),
		NWidget(WWT_RESIZEBOX,  COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _template_create_window_desc(
	WDP_AUTO,                       // window position
	"template create window",       // const char* ini_key
	456, 100,                       // window size
	WC_CREATE_TEMPLATE,             // window class
	WC_TEMPLATEGUI_MAIN,            // parent window class
	WDF_CONSTRUCTION,               // window flags
	_widgets, lengthof(_widgets)    // widgets + num widgets
);

void ShowTemplateTrainBuildVehicleWindow(Train **virtual_train);

static void TrainDepotMoveVehicle(const Vehicle *wagon, VehicleID sel, const Vehicle *head)
{
	const Vehicle *v = Vehicle::Get(sel);

	if (v == wagon) return;

	if (wagon == nullptr) {
		if (head != nullptr) wagon = head->Last();
	} else {
		wagon = wagon->Previous();
		if (wagon == nullptr) return;
	}

	if (wagon == v) return;

	DoCommandP(v->tile, v->index | ((_ctrl_pressed ? 1 : 0) << 20) | (1 << 21) , wagon == nullptr ? INVALID_VEHICLE : wagon->index,
			CMD_MOVE_VIRTUAL_RAIL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_MOVE_VEHICLE), CcVirtualTrainWagonsMoved);
}

class TemplateCreateWindow : public Window {
private:
	Scrollbar *hscroll;
	Scrollbar *vscroll;
	Train* virtual_train;
	bool *create_window_open;         /// used to notify main window of progress (dummy way of disabling 'delete' while editing a template)
	VehicleID sel;
	VehicleID vehicle_over;
	bool sell_hovered;                ///< A vehicle is being dragged/hovered over the sell button.
	uint32 template_index;
	btree::btree_set<VehicleID> pending_deletions; ///< Vehicle IDs where deletion is in progress

public:
	TemplateCreateWindow(WindowDesc* _wdesc, TemplateVehicle *to_edit, bool *window_open) : Window(_wdesc)
	{
		this->CreateNestedTree(_wdesc != nullptr);
		this->hscroll = this->GetScrollbar(TCW_SCROLLBAR_H_NEW_TMPL);
		this->vscroll = this->GetScrollbar(TCW_SCROLLBAR_V_NEW_TMPL);
		this->FinishInitNested(VEH_TRAIN);
		/* a sprite */
		this->GetWidget<NWidgetCore>(TCW_SELL_TMPL)->widget_data = SPR_SELL_TRAIN;

		this->owner = _local_company;

		this->create_window_open = window_open;
		this->template_index = (to_edit != nullptr) ? to_edit->index : INVALID_VEHICLE;

		this->sel = INVALID_VEHICLE;
		this->vehicle_over = INVALID_VEHICLE;
		this->sell_hovered = false;

		if (to_edit != nullptr) {
			DoCommandP(0, to_edit->index, 0, CMD_VIRTUAL_TRAIN_FROM_TEMPLATE_VEHICLE | CMD_MSG(STR_TMPL_CANT_CREATE), CcSetVirtualTrain);
		}

		this->resize.step_height = 1;

		UpdateButtonState();
	}

	void Close() override
	{
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
			virtual_train = nullptr;
		}

		/* more cleanup */
		*create_window_open = false;
		CloseWindowById(WC_BUILD_VIRTUAL_TRAIN, this->window_number);
		InvalidateWindowClassesData(WC_TEMPLATEGUI_MAIN);
		this->Window::Close();
	}

	void SetVirtualTrain(Train* const train)
	{
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
		}

		virtual_train = train;
		if (virtual_train != nullptr) {
			assert(HasBit(virtual_train->subtype, GVSF_VIRTUAL));
		}
		UpdateButtonState();
	}

	virtual void OnResize() override
	{
		NWidgetCore *template_panel = this->GetWidget<NWidgetCore>(TCW_NEW_TMPL_PANEL);
		this->hscroll->SetCapacity(template_panel->current_x);

		NWidgetCore *info_panel = this->GetWidget<NWidgetCore>(TCW_INFO_PANEL);
		this->vscroll->SetCapacity(info_panel->current_y);
	}


	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if(!gui_scope) return;

		if (this->template_index != INVALID_VEHICLE) {
			if (TemplateVehicle::GetIfValid(this->template_index) == nullptr) {
				this->Close();
				return;
			}
		}
		this->SetDirty();
		UpdateButtonState();
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		switch(widget) {
			case TCW_NEW_TMPL_PANEL: {
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(TCW_NEW_TMPL_PANEL);
				ClickedOnVehiclePanel(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				break;
			}
			case TCW_NEW: {
				ShowTemplateTrainBuildVehicleWindow(&virtual_train);
				break;
			}
			case TCW_CLONE: {
				this->SetWidgetDirty(TCW_CLONE);
				this->ToggleWidgetLoweredState(TCW_CLONE);
				if (this->IsWidgetLowered(TCW_CLONE)) {
					SetObjectToPlaceWnd(SPR_CURSOR_CLONE_TRAIN, PAL_NONE, HT_VEHICLE, this);
				} else {
					ResetObjectToPlace();
				}
				break;
			}
			case TCW_OK: {
				if (virtual_train != nullptr) {
					DoCommandP(0, this->template_index, virtual_train->index, CMD_REPLACE_TEMPLATE_VEHICLE);
				} else if (this->template_index != INVALID_VEHICLE) {
					DoCommandP(0, this->template_index, 0, CMD_DELETE_TEMPLATE_VEHICLE);
				}
				this->Close();
				break;
			}
			case TCW_CANCEL: {
				this->Close();
				break;
			}
			case TCW_REFIT: {
				if (virtual_train != nullptr) {
					ShowVehicleRefitWindow(virtual_train, INVALID_VEH_ORDER_ID, this, false, true);
				}
				break;
			}
		}
	}

	virtual bool OnVehicleSelect(const Vehicle *v) override
	{
		// throw away the current virtual train
		if (virtual_train != nullptr) {
			DoCommandP(0, virtual_train->index, 0, CMD_DELETE_VIRTUAL_TRAIN);
			virtual_train = nullptr;
		}

		// create a new one
		DoCommandP(0, v->index, 0, CMD_VIRTUAL_TRAIN_FROM_TRAIN | CMD_MSG(STR_TMPL_CANT_CREATE), CcSetVirtualTrain);
		this->ToggleWidgetLoweredState(TCW_CLONE);
		ResetObjectToPlace();
		this->SetDirty();

		return true;
	}

	virtual void OnPlaceObjectAbort() override
	{
		this->sel = INVALID_VEHICLE;
		this->vehicle_over = INVALID_VEHICLE;
		this->RaiseButtons();
		this->SetDirty();
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		switch(widget) {
			case TCW_NEW_TMPL_PANEL: {
				if (this->virtual_train) {
					DrawTrainImage(virtual_train, r.Shrink(TRAIN_FRONT_SPACE, 2, 25, 0), this->sel, EIT_IN_DEPOT, this->hscroll->GetPosition(), this->vehicle_over);
					SetDParam(0, CeilDiv(virtual_train->gcache.cached_total_length * 10, TILE_SIZE));
					SetDParam(1, 1);
					DrawString(r.left, r.right, r.top, STR_JUST_DECIMAL, TC_BLACK, SA_RIGHT, false, FS_SMALL);
				}
				break;
			}
			case TCW_INFO_PANEL: {
				if (this->virtual_train) {
					DrawPixelInfo tmp_dpi;

					if (!FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left, r.bottom - r.top)) break;

					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);

					int y = ScaleGUITrad(4) - this->vscroll->GetPosition();
					int left = ScaleGUITrad(4);
					int right = (r.right - r.left) - ScaleGUITrad(4);
					bool buildable = true;
					Money buy_cost = 0;
					RailTypes types = static_cast<RailTypes>(UINT64_MAX);
					for (Train *train = this->virtual_train; train != nullptr; train = train->GetNextUnit()) {
						const Engine *e = Engine::Get(train->engine_type);
						if (!IsEngineBuildable(train->engine_type, VEH_TRAIN, train->owner)) {
							buildable = false;
						} else {
							types &= (GetRailTypeInfo(e->u.rail.railtype))->compatible_railtypes;
						}
						buy_cost += e->GetCost();
					}
					if (!buildable) {
						DrawString(left, right, y, STR_TMPL_WARNING_VEH_UNAVAILABLE);
						y += FONT_HEIGHT_NORMAL;
					} else if (types == RAILTYPES_NONE) {
						DrawString(left, right, y, STR_TMPL_WARNING_VEH_NO_COMPATIBLE_RAIL_TYPE);
						y += FONT_HEIGHT_NORMAL;
					}

					SetDParam(0, STR_TMPL_TEMPLATE_OVR_VALUE_LTBLUE);
					SetDParam(1, buy_cost);
					SetDParam(2, STR_TMPL_TEMPLATE_OVR_RUNNING_COST);
					SetDParam(3, this->virtual_train->GetDisplayRunningCost());
					DrawString(left, right, y, STR_TMPL_TEMPLATE_OVR_MULTIPLE);
					y += FONT_HEIGHT_NORMAL;

					/* Draw vehicle performance info */
					const bool original_acceleration = (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ||
							GetRailTypeInfo(this->virtual_train->railtype)->acceleration_type == 2);
					const GroundVehicleCache *gcache = this->virtual_train->GetGroundVehicleCache();
					SetDParam(2, this->virtual_train->GetDisplayMaxSpeed());
					SetDParam(1, gcache->cached_power);
					SetDParam(0, gcache->cached_weight);
					SetDParam(3, gcache->cached_max_te);
					DrawString(left, right, y, original_acceleration ? STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED : STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE);
					uint32 full_cargo_weight = 0;
					for (Train *train = this->virtual_train; train != nullptr; train = train->Next()) {
						full_cargo_weight += train->GetCargoWeight(train->cargo_cap);
					}
					if (full_cargo_weight > 0 || _settings_client.gui.show_train_weight_ratios_in_details) {
						y += FONT_HEIGHT_NORMAL;
						uint full_weight = gcache->cached_weight + full_cargo_weight;
						SetDParam(0, full_weight);
						if (_settings_client.gui.show_train_weight_ratios_in_details) {
							SetDParam(1, STR_VEHICLE_INFO_WEIGHT_RATIOS);
							SetDParam(2, STR_VEHICLE_INFO_POWER_WEIGHT_RATIO);
							SetDParam(3, (100 * this->virtual_train->gcache.cached_power) / std::max<uint>(1, full_weight));
							SetDParam(4, this->virtual_train->GetAccelerationType() == 2 ? STR_EMPTY : STR_VEHICLE_INFO_TE_WEIGHT_RATIO);
							SetDParam(5, (100 * this->virtual_train->gcache.cached_max_te) / std::max<uint>(1, full_weight));
						} else {
							SetDParam(1, STR_EMPTY);
						}
						DrawString(left, right, y, STR_VEHICLE_INFO_FULL_WEIGHT_WITH_RATIOS);
					}
					if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
						y += FONT_HEIGHT_NORMAL;
						SetDParam(0, GetTrainEstimatedMaxAchievableSpeed(this->virtual_train, gcache->cached_weight + full_cargo_weight, this->virtual_train->GetDisplayMaxSpeed()));
						DrawString(left, right, y, STR_VEHICLE_INFO_MAX_SPEED_LOADED);
					}
					/* Draw cargo summary */
					CargoArray cargo_caps{};
					for (const Train *tmp = this->virtual_train; tmp != nullptr; tmp = tmp->Next()) {
						cargo_caps[tmp->cargo_type] += tmp->cargo_cap;
					}
					y += FONT_HEIGHT_NORMAL * 2;
					for (CargoID i = 0; i < NUM_CARGO; ++i) {
						if (cargo_caps[i] > 0) {
							SetDParam(0, i);
							SetDParam(1, cargo_caps[i]);
							DrawString(left, right, y, STR_TMPL_CARGO_SUMMARY, TC_LIGHT_BLUE, SA_LEFT);
							y += FONT_HEIGHT_NORMAL;
						}
					}
				}
				break;
			}
			default:
				break;
		}
	}

	virtual bool OnRightClick(Point pt, int widget) override
	{
		if (widget != TCW_NEW_TMPL_PANEL) return false;

		GetDepotVehiclePtData gdvp = { nullptr, nullptr };
		const Vehicle *v = nullptr;
		NWidgetBase *nwi = this->GetWidget<NWidgetBase>(TCW_NEW_TMPL_PANEL);
		DepotGUIAction mode = this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, &gdvp);
		v = gdvp.wagon;

		if (v == nullptr || mode != MODE_DRAG_VEHICLE) return false;

		CargoArray capacity{}, loaded{};

		/* Display info for single (articulated) vehicle, or for whole chain starting with selected vehicle */
		bool whole_chain = _ctrl_pressed;

		/* loop through vehicle chain and collect cargoes */
		uint num = 0;
		for (const Vehicle *w = v; w != nullptr; w = w->Next()) {
			if (w->cargo_cap > 0 && w->cargo_type < NUM_CARGO) {
				capacity[w->cargo_type] += w->cargo_cap;
				loaded  [w->cargo_type] += w->cargo.StoredCount();
			}

			if (w->type == VEH_TRAIN && !w->HasArticulatedPart()) {
				num++;
				if (!whole_chain) break;
			}
		}

		/* Build tooltipstring */
		std::string details;

		for (CargoID cargo_type = 0; cargo_type < NUM_CARGO; cargo_type++) {
			if (capacity[cargo_type] == 0) continue;

			SetDParam(0, cargo_type);           // {CARGO} #1
			SetDParam(1, loaded[cargo_type]);   // {CARGO} #2
			SetDParam(2, cargo_type);           // {SHORTCARGO} #1
			SetDParam(3, capacity[cargo_type]); // {SHORTCARGO} #2
			details = GetString(STR_DEPOT_VEHICLE_TOOLTIP_CARGO);
		}

		/* Show tooltip window */
		SetDParam(0, whole_chain ? num : v->engine_type);
		SetDParamStr(1, std::move(details));
		GuiShowTooltips(this, whole_chain ? STR_DEPOT_VEHICLE_TOOLTIP_CHAIN : STR_DEPOT_VEHICLE_TOOLTIP, TCC_RIGHT_CLICK, 2);

		return true;
	}

	virtual void OnDragDrop(Point pt, int widget) override
	{
		switch (widget) {
			case TCW_NEW_TMPL_PANEL: {
				const Vehicle *v = nullptr;
				VehicleID sel = this->sel;

				this->sel = INVALID_VEHICLE;
				this->SetDirty();

				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(TCW_NEW_TMPL_PANEL);
				GetDepotVehiclePtData gdvp = { nullptr, nullptr };

				if (this->GetVehicleFromDepotWndPt(pt.x - nwi->pos_x, pt.y - nwi->pos_y, &v, &gdvp) == MODE_DRAG_VEHICLE && sel != INVALID_VEHICLE) {
					if (gdvp.wagon != nullptr && gdvp.wagon->index == sel && _ctrl_pressed) {
						DoCommandP(Vehicle::Get(sel)->tile, Vehicle::Get(sel)->index, true,
								CMD_REVERSE_TRAIN_DIRECTION | CMD_MSG(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE), CcVirtualTrainWagonsMoved);
					} else if (gdvp.wagon == nullptr || gdvp.wagon->index != sel) {
						this->vehicle_over = INVALID_VEHICLE;
						TrainDepotMoveVehicle(gdvp.wagon, sel, gdvp.head);
					}
				}
				break;
			}
			case TCW_SELL_TMPL: {
				if (this->IsWidgetDisabled(widget)) return;
				if (this->sel == INVALID_VEHICLE) return;

				int sell_cmd = (_ctrl_pressed) ? 1 : 0;

				Train* train_to_delete = Train::Get(this->sel);

				this->pending_deletions.insert(this->sel);

				if (virtual_train == train_to_delete) {
					if (_ctrl_pressed) {
						virtual_train = nullptr;
					} else {
						this->RearrangeVirtualTrain();
					}
				}

				DoCommandP(0, this->sel | (sell_cmd << 20) | (1 << 21), 0, CMD_SELL_VIRTUAL_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_SELL_TRAIN), CcDeleteVirtualTrain);

				this->sel = INVALID_VEHICLE;

				this->SetDirty();
				UpdateButtonState();
				break;
			}
			default:
				this->sel = INVALID_VEHICLE;
				this->SetDirty();
				break;
		}
		this->sell_hovered = false;
		_cursor.vehchain = false;
		this->sel = INVALID_VEHICLE;
		this->SetDirty();
	}

	virtual void OnMouseDrag(Point pt, int widget) override
	{
		if (this->sel == INVALID_VEHICLE) return;

		bool is_sell_widget = widget == TCW_SELL_TMPL;
		if (is_sell_widget != this->sell_hovered) {
			this->sell_hovered = is_sell_widget;
			this->SetWidgetLoweredState(TCW_SELL_TMPL, is_sell_widget);
			this->SetWidgetDirty(TCW_SELL_TMPL);
		}

		/* A rail vehicle is dragged.. */
		if (widget != TCW_NEW_TMPL_PANEL) { // ..outside of the depot matrix.
			if (this->vehicle_over != INVALID_VEHICLE) {
				this->vehicle_over = INVALID_VEHICLE;
				this->SetWidgetDirty(TCW_NEW_TMPL_PANEL);
			}
			return;
		}

		NWidgetBase *matrix = this->GetWidget<NWidgetBase>(widget);
		const Vehicle *v = nullptr;
		GetDepotVehiclePtData gdvp = {nullptr, nullptr};

		if (this->GetVehicleFromDepotWndPt(pt.x - matrix->pos_x, pt.y - matrix->pos_y, &v, &gdvp) != MODE_DRAG_VEHICLE) return;
		VehicleID new_vehicle_over = INVALID_VEHICLE;
		if (gdvp.head != nullptr) {
			if (gdvp.wagon == nullptr && gdvp.head->Last()->index != this->sel) { // ..at the end of the train.
				/* NOTE: As a wagon can't be moved at the begin of a train, head index isn't used to mark a drag-and-drop
				 * destination inside a train. This head index is then used to indicate that a wagon is inserted at
				 * the end of the train.
				 */
				new_vehicle_over = gdvp.head->index;
			} else if (gdvp.wagon != nullptr && gdvp.head != gdvp.wagon &&
					gdvp.wagon->index != this->sel &&
					gdvp.wagon->Previous()->index != this->sel) { // ..over an existing wagon.
				new_vehicle_over = gdvp.wagon->index;
			}
		}
		if (this->vehicle_over == new_vehicle_over) return;

		this->vehicle_over = new_vehicle_over;
		this->SetWidgetDirty(widget);
	}

	virtual void OnPaint() override
	{
		uint min_width = 32;
		uint width = 0;
		uint height = ScaleGUITrad(8) + (3 * FONT_HEIGHT_NORMAL);
		CargoArray cargo_caps{};

		if (virtual_train != nullptr) {
			bool buildable = true;
			uint32 full_cargo_weight = 0;
			for (Train *train = virtual_train; train != nullptr; train = train->GetNextUnit()) {
				width += train->GetDisplayImageWidth();
				cargo_caps[train->cargo_type] += train->cargo_cap;
				if (!IsEngineBuildable(train->engine_type, VEH_TRAIN, train->owner)) buildable = false;
				full_cargo_weight += train->GetCargoWeight(train->cargo_cap);
			}
			if (!buildable) height += FONT_HEIGHT_NORMAL;
			if (full_cargo_weight > 0 || _settings_client.gui.show_train_weight_ratios_in_details) height += FONT_HEIGHT_NORMAL;
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) height += FONT_HEIGHT_NORMAL;

			for (CargoID i = 0; i < NUM_CARGO; ++i) {
				if (cargo_caps[i] > 0) {
					height += FONT_HEIGHT_NORMAL;
				}
			}
		}

		min_width = std::max(min_width, width);
		this->hscroll->SetCount(min_width + 50);

		this->vscroll->SetCount(height);

		this->DrawWidgets();
	}

	struct GetDepotVehiclePtData {
		const Vehicle *head;
		const Vehicle *wagon;
	};

	enum DepotGUIAction {
		MODE_ERROR,
		MODE_DRAG_VEHICLE,
		MODE_SHOW_VEHICLE,
		MODE_START_STOP,
	};

	uint count_width;
	uint header_width;

	DepotGUIAction GetVehicleFromDepotWndPt(int x, int y, const Vehicle **veh, GetDepotVehiclePtData *d) const
	{
		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(TCW_NEW_TMPL_PANEL);
		/* In case of RTL the widgets are swapped as a whole */
		if (_current_text_dir == TD_RTL) x = matrix_widget->current_x - x;

		x -= TRAIN_FRONT_SPACE;

		uint xm = x;

		bool wagon = false;

		x += this->hscroll->GetPosition();
		const Train *v = virtual_train;
		d->head = d->wagon = v;

		if (xm <= this->header_width) {
			if (wagon) return MODE_ERROR;

			return MODE_SHOW_VEHICLE;
		}

		/* Account for the header */
		x -= this->header_width;

		/* find the vehicle in this row that was clicked */
		for (; v != nullptr; v = v->Next()) {
			x -= v->GetDisplayImageWidth();
			if (x < 0) break;
		}

		d->wagon = (v != nullptr ? v->GetFirstEnginePart() : nullptr);

		return MODE_DRAG_VEHICLE;
	}

	void ClickedOnVehiclePanel(int x, int y)
	{
		GetDepotVehiclePtData gdvp = { nullptr, nullptr };
		const Vehicle *v = nullptr;
		this->GetVehicleFromDepotWndPt(x, y, &v, &gdvp);

		v = gdvp.wagon;

		if (v != nullptr && VehicleClicked(v)) return;
		VehicleID sel = this->sel;

		if (sel != INVALID_VEHICLE) {
			this->sel = INVALID_VEHICLE;
			TrainDepotMoveVehicle(v, sel, gdvp.head);
		} else if (v != nullptr) {
			SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
			SetMouseCursorVehicle(v, EIT_PURCHASE);
			_cursor.vehchain = _ctrl_pressed;

			this->sel = v->index;
			this->SetDirty();
		}
	}

	EventState OnCTRLStateChange() override
	{
		if (this->sel != INVALID_VEHICLE) {
			_cursor.vehchain = _ctrl_pressed;
			this->SetWidgetDirty(TCW_NEW_TMPL_PANEL);
			return ES_HANDLED;
		}

		return ES_NOT_HANDLED;
	}

	void VirtualVehicleDeleted(VehicleID id)
	{
		this->pending_deletions.erase(id);
		this->RearrangeVirtualTrain();
	}

	void RearrangeVirtualTrain()
	{
		if (!this->virtual_train) return;
		this->virtual_train = this->virtual_train->First();
		assert(HasBit(this->virtual_train->subtype, GVSF_VIRTUAL));
		for (; this->virtual_train != nullptr; this->virtual_train = this->virtual_train->GetNextUnit()) {
			if (this->pending_deletions.count(this->virtual_train->index) == 0) break;
		}
	}


	void UpdateButtonState()
	{
		this->SetWidgetDisabledState(TCW_REFIT, virtual_train == nullptr);
	}

	bool IsNewGRFInspectable() const override
	{
		return true;
	}

	void ShowNewGRFInspectWindow() const override
	{
		if (this->virtual_train != nullptr) {
			::ShowNewGRFInspectWindow(GetGrfSpecFeature(VEH_TRAIN), this->virtual_train->index);
		}
	}
};

void ShowTemplateCreateWindow(TemplateVehicle *to_edit, bool *create_window_open)
{
	if (BringWindowToFrontById(WC_CREATE_TEMPLATE, VEH_TRAIN) != nullptr) return;
	new TemplateCreateWindow(&_template_create_window_desc, to_edit, create_window_open);
}

void CcSetVirtualTrain(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_CREATE_TEMPLATE, 0);
	if (window) {
		Train* train = Train::From(Vehicle::Get(_new_vehicle_id));
		((TemplateCreateWindow*)window)->SetVirtualTrain(train);
		window->InvalidateData();
	}
}

void CcVirtualTrainWagonsMoved(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_CREATE_TEMPLATE, 0);
	if (window) {
		((TemplateCreateWindow*)window)->RearrangeVirtualTrain();
		window->InvalidateData();
	}
}

void CcDeleteVirtualTrain(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_CREATE_TEMPLATE, 0);
	if (window) {
		((TemplateCreateWindow*)window)->VirtualVehicleDeleted(GB(p1, 0, 20));
		window->InvalidateData();
	}
}
