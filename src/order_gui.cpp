/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_gui.cpp GUI related to orders. */

#include "stdafx.h"
#include "command_func.h"
#include "viewport_func.h"
#include "depot_map.h"
#include "roadveh.h"
#include "timetable.h"
#include "strings_func.h"
#include "company_func.h"
#include "widgets/dropdown_type.h"
#include "widgets/dropdown_func.h"
#include "textbuf_gui.h"
#include "string_func.h"
#include "tilehighlight_func.h"
#include "network/network.h"
#include "station_base.h"
#include "industry.h"
#include "waypoint_base.h"
#include "core/geometry_func.hpp"
#include "infrastructure_func.h"
#include "hotkeys.h"
#include "aircraft.h"
#include "engine_func.h"
#include "vehicle_func.h"
#include "vehiclelist.h"
#include "error.h"
#include "tracerestrict.h"
#include "scope.h"
#include "core/backup_type.hpp"

#include "widgets/order_widget.h"

#include "safeguards.h"

enum CargoTypeOrdersWindowVariant {
	CTOWV_LOAD   = 0,
	CTOWV_UNLOAD = 1,
};

/** Cargo type orders strings for load dropdowns. */
static const StringID _cargo_type_load_order_drowdown[] = {
	STR_ORDER_DROP_LOAD_IF_POSSIBLE,      // OLF_LOAD_IF_POSSIBLE
	STR_EMPTY,
	STR_CARGO_TYPE_ORDERS_DROP_FULL_LOAD, // OLFB_FULL_LOAD
	STR_EMPTY,
	STR_ORDER_DROP_NO_LOADING,            // OLFB_NO_LOAD
	INVALID_STRING_ID
};
static const uint32 _cargo_type_load_order_drowdown_hidden_mask = 0xA; // 01010

/** Cargo type orders strings for unload dropdowns. */
static const StringID _cargo_type_unload_order_drowdown[] = {
	STR_ORDER_DROP_UNLOAD_IF_ACCEPTED, // OUF_UNLOAD_IF_POSSIBLE
	STR_ORDER_DROP_UNLOAD,             // OUFB_UNLOAD
	STR_ORDER_DROP_TRANSFER,           // OUFB_TRANSFER
	STR_EMPTY,
	STR_ORDER_DROP_NO_UNLOADING,       // OUFB_NO_UNLOAD
	INVALID_STRING_ID
};
static const uint32 _cargo_type_unload_order_drowdown_hidden_mask = 0x8; // 01000

DropDownList GetSlotDropDownList(Owner owner, TraceRestrictSlotID slot_id, int &selected, VehicleType vehtype, bool show_other_types);
DropDownList GetCounterDropDownList(Owner owner, TraceRestrictCounterID ctr_id, int &selected);

static bool ModifyOrder(const Vehicle *v, VehicleOrderID order_id, uint32 p2, bool error_msg = true, const char *text = nullptr)
{
	return DoCommandPEx(v->tile, v->index, p2, order_id, CMD_MODIFY_ORDER | (error_msg ? CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER) : 0), nullptr, text, nullptr);
}

struct CargoTypeOrdersWindow : public Window {
private:
	CargoTypeOrdersWindowVariant variant;

	const Vehicle *vehicle;  ///< Vehicle owning the orders being displayed and manipulated.
	VehicleOrderID order_id; ///< Index of the order concerned by this window.

	VehicleOrderID order_count; ///< Count of the orders of the vehicle owning this window
	const Order *order;         ///< Order pointer at construction time;

	static const uint8 CARGO_ICON_WIDTH  = 12;
	static const uint8 CARGO_ICON_HEIGHT =  8;

	const StringID *cargo_type_order_dropdown; ///< Strings used to populate order dropdowns.
	uint32 cargo_type_order_dropdown_hmask;    ///< Hidden mask for order dropdowns.

	uint max_cargo_name_width;     ///< Greatest width of cargo names.
	uint max_cargo_dropdown_width; ///< Greatest width of order names.

	uint set_to_all_dropdown_sel;     ///< Selected entry for the 'set to all' dropdown

	/**
	 * Initialize \c max_cargo_name_width and \c max_cargo_dropdown_width.
	 * @post \c max_cargo_name_width
	 * @post \c max_cargo_dropdown_width
	 */
	void InitMaxWidgetWidth()
	{
		this->max_cargo_name_width = 0;
		for (int i = 0; i < (int)_sorted_standard_cargo_specs.size(); i++) {
			SetDParam(0, _sorted_cargo_specs[i]->name);
			this->max_cargo_name_width = std::max(this->max_cargo_name_width, GetStringBoundingBox(STR_JUST_STRING).width);
		}
		this->max_cargo_dropdown_width = 0;
		for (int i = 0; this->cargo_type_order_dropdown[i] != INVALID_STRING_ID; i++) {
			SetDParam(0, this->cargo_type_order_dropdown[i]);
			this->max_cargo_dropdown_width = std::max(this->max_cargo_dropdown_width, GetStringBoundingBox(STR_JUST_STRING).width);
		}
	}

	/** Populate the selected entry of order dropdowns. */
	void InitDropdownSelectedTypes()
	{
		StringID tooltip = STR_CARGO_TYPE_LOAD_ORDERS_DROP_TOOLTIP + this->variant;
		const Order *order = this->vehicle->GetOrder(this->order_id);
		for (int i = 0; i < (int)_sorted_standard_cargo_specs.size(); i++) {
			const CargoSpec *cs = _sorted_cargo_specs[i];
			const CargoID cargo_id = cs->Index();
			uint8 order_type = (this->variant == CTOWV_LOAD) ? (uint8) order->GetCargoLoadTypeRaw(cargo_id) : (uint8) order->GetCargoUnloadTypeRaw(cargo_id);
			this->GetWidget<NWidgetCore>(WID_CTO_CARGO_DROPDOWN_FIRST + i)->SetDataTip(this->cargo_type_order_dropdown[order_type], tooltip);
		}
		this->GetWidget<NWidgetCore>(WID_CTO_SET_TO_ALL_DROPDOWN)->widget_data = this->cargo_type_order_dropdown[this->set_to_all_dropdown_sel];
	}

	/**
	 * Returns the load/unload type of this order for the specified cargo.
	 * @param cargo_id The cargo index for wich we want the load/unload type.
	 * @return an OrderLoadFlags if \c load_variant = true, an OrderUnloadFlags otherwise.
	 */
	uint8 GetOrderActionTypeForCargo(CargoID cargo_id)
	{
		const Order *order = this->vehicle->GetOrder(this->order_id);
		return (this->variant == CTOWV_LOAD) ? (uint8) order->GetCargoLoadTypeRaw(cargo_id) : (uint8) order->GetCargoUnloadTypeRaw(cargo_id);
	}

	bool CheckOrderStillValid() const
	{
		if (this->vehicle->GetNumOrders() != this->order_count) return false;
		if (this->vehicle->GetOrder(this->order_id) != this->order) return false;
		return true;
	}

public:
	/**
	 * Instantiate a new CargoTypeOrdersWindow.
	 * @param desc The window description.
	 * @param v The vehicle the order belongs to.
	 * @param order_id Which order to display/edit.
	 * @param variant Which aspect of the order to display/edit: load or unload.
	 * @pre \c v != nullptr
	 */
	CargoTypeOrdersWindow(WindowDesc *desc, const Vehicle *v, VehicleOrderID order_id, CargoTypeOrdersWindowVariant variant) : Window(desc)
	{
		this->variant = variant;
		this->cargo_type_order_dropdown = (this->variant == CTOWV_LOAD) ? _cargo_type_load_order_drowdown : _cargo_type_unload_order_drowdown;
		this->cargo_type_order_dropdown_hmask = (this->variant == CTOWV_LOAD) ? _cargo_type_load_order_drowdown_hidden_mask : _cargo_type_unload_order_drowdown_hidden_mask;
		this->InitMaxWidgetWidth();

		this->vehicle = v;
		this->order_id = order_id;
		this->order_count = v->GetNumOrders();
		this->order = v->GetOrder(order_id);
		this->set_to_all_dropdown_sel = 0;

		this->CreateNestedTree(desc);
		this->GetWidget<NWidgetCore>(WID_CTO_CAPTION)->SetDataTip(STR_CARGO_TYPE_ORDERS_LOAD_CAPTION + this->variant, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS);
		this->GetWidget<NWidgetCore>(WID_CTO_HEADER)->SetDataTip(STR_CARGO_TYPE_ORDERS_LOAD_TITLE + this->variant, STR_NULL);
		this->GetWidget<NWidgetStacked>(WID_CTO_SELECT)->SetDisplayedPlane((_sorted_standard_cargo_specs.size() >= 32) ? 0 : SZSP_NONE);
		this->InitDropdownSelectedTypes();
		this->FinishInitNested(v->index);

		this->owner = v->owner;
	}

	void Close() override
	{
		if (!FocusWindowById(WC_VEHICLE_ORDERS, this->window_number)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
		this->Window::Close();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (widget == WID_CTO_HEADER) {
			(*size).height = std::max((*size).height, (uint) FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.framerect.Vertical());
		} else if (WID_CTO_CARGO_LABEL_FIRST <= widget && widget <= WID_CTO_CARGO_LABEL_LAST) {
			(*size).width  = std::max((*size).width, WidgetDimensions::scaled.framerect.left + this->CARGO_ICON_WIDTH + WidgetDimensions::scaled.framerect.Horizontal() + this->max_cargo_name_width + padding.width);
			(*size).height = std::max((*size).height, (uint) FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.framerect.Vertical());
		} else if ((WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) || widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			(*size).width  = std::max((*size).width, WidgetDimensions::scaled.dropdowntext.Horizontal() + this->max_cargo_dropdown_width + NWidgetLeaf::GetDropdownBoxDimension().width);
			(*size).height = std::max((*size).height, (uint) WidgetDimensions::scaled.dropdowntext.Vertical() + FONT_HEIGHT_NORMAL);
		} else if (widget == WID_CTO_SET_TO_ALL_LABEL) {
			(*size).width = std::max((*size).width, this->max_cargo_name_width + WidgetDimensions::scaled.framerect.right + padding.width);
			(*size).height = std::max((*size).height, (uint) FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.framerect.Vertical());
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		if (WID_CTO_CARGO_LABEL_FIRST <= widget && widget <= WID_CTO_CARGO_LABEL_LAST) {
			Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_LABEL_FIRST];
			bool rtl = (_current_text_dir == TD_RTL);

			/* Draw cargo icon. */
			int rect_left   = rtl ? ir.right - this->CARGO_ICON_WIDTH : ir.left;
			int rect_right  = rect_left + this->CARGO_ICON_WIDTH;
			int rect_top    = ir.top + ((ir.bottom - ir.top) - this->CARGO_ICON_HEIGHT) / 2;
			int rect_bottom = rect_top + this->CARGO_ICON_HEIGHT;
			GfxFillRect(rect_left, rect_top, rect_right, rect_bottom, PC_BLACK);
			GfxFillRect(rect_left + 1, rect_top + 1, rect_right - 1, rect_bottom - 1, cs->legend_colour);

			/* Draw cargo name */
			int text_left  = rtl ? ir.left : rect_right + WidgetDimensions::scaled.framerect.left;
			int text_right = rtl ? rect_left - WidgetDimensions::scaled.framerect.left : ir.right;
			int text_top   = ir.top;
			SetDParam(0, cs->name);
			DrawString(text_left, text_right, text_top, STR_JUST_STRING, TC_BLACK);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		if (!this->CheckOrderStillValid()) {
			this->Close();
			return;
		}
		if (widget == WID_CTO_CLOSEBTN) {
			this->Close();
		} else if (WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) {
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_DROPDOWN_FIRST];
			const CargoID cargo_id = cs->Index();

			ShowDropDownMenu(this, this->cargo_type_order_dropdown, this->GetOrderActionTypeForCargo(cargo_id), widget, 0, this->cargo_type_order_dropdown_hmask);
		} else if (widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			ShowDropDownMenu(this, this->cargo_type_order_dropdown, this->set_to_all_dropdown_sel, widget, 0, this->cargo_type_order_dropdown_hmask);
		}
	}

	virtual void OnDropdownSelect(int widget, int action_type) override
	{
		if (!this->CheckOrderStillValid()) {
			this->Close();
			return;
		}
		ModifyOrderFlags mof = (this->variant == CTOWV_LOAD) ? MOF_CARGO_TYPE_LOAD : MOF_CARGO_TYPE_UNLOAD;
		if (WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) {
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_DROPDOWN_FIRST];
			const CargoID cargo_id = cs->Index();
			uint8 order_action_type = this->GetOrderActionTypeForCargo(cargo_id);

			if (action_type == order_action_type) return;

			ModifyOrder(this->vehicle, this->order_id, mof | (action_type << 8) | (cargo_id << 24));

			this->GetWidget<NWidgetCore>(widget)->SetDataTip(this->cargo_type_order_dropdown[this->GetOrderActionTypeForCargo(cargo_id)], STR_CARGO_TYPE_LOAD_ORDERS_DROP_TOOLTIP + this->variant);
			this->SetWidgetDirty(widget);
		} else if (widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			ModifyOrder(this->vehicle, this->order_id, mof | (action_type << 8) | (CT_INVALID << 24));

			for (int i = 0; i < (int)_sorted_standard_cargo_specs.size(); i++) {
				const CargoSpec *cs = _sorted_cargo_specs[i];
				const CargoID cargo_id = cs->Index();
				if (action_type != this->GetOrderActionTypeForCargo(cargo_id)) {
					this->GetWidget<NWidgetCore>(i + WID_CTO_CARGO_DROPDOWN_FIRST)->SetDataTip(this->cargo_type_order_dropdown[this->GetOrderActionTypeForCargo(cargo_id)], STR_CARGO_TYPE_LOAD_ORDERS_DROP_TOOLTIP + this->variant);
					this->SetWidgetDirty(i + WID_CTO_CARGO_DROPDOWN_FIRST);
				}
			}

			if (action_type != (int) this->set_to_all_dropdown_sel) {
				this->set_to_all_dropdown_sel = action_type;
				this->GetWidget<NWidgetCore>(widget)->widget_data = this->cargo_type_order_dropdown[this->set_to_all_dropdown_sel];
				this->SetWidgetDirty(widget);
			}
		}
	}

	virtual void SetStringParameters(int widget) const override
	{
		if (!this->CheckOrderStillValid()) {
			return;
		}
		if (widget == WID_CTO_CAPTION) {
			SetDParam(0, this->vehicle->index);
			SetDParam(1, this->order_id + 1);
			SetDParam(2, this->vehicle->GetOrder(this->order_id)->GetDestination());
		}
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!this->CheckOrderStillValid()) {
			this->Close();
			return;
		}
		if (gui_scope) {
			this->InitDropdownSelectedTypes();
			this->SetDirty();
		}
	}
};

/**
 * Make a list of panel for each available cargo type.
 * Each panel contains a label to display the cargo name.
 * @param biggest_index Storage for collecting the biggest index used in the returned tree
 * @return A vertical container of cargo type orders rows.
 * @post \c *biggest_index contains the largest used index in the tree.
 */
static NWidgetBase *MakeCargoTypeOrdersRows(int *biggest_index, bool right)
{

	NWidgetVertical *ver = new NWidgetVertical;

	const bool dual_column = (_sorted_standard_cargo_specs.size() >= 32);
	if (right && !dual_column) return ver;

	const int increment = dual_column ? 2 : 1;

	for (int i = (right ? 1 : 0); i < (int)_sorted_standard_cargo_specs.size(); i += increment) {
		/* Cargo row */
		NWidgetBackground *panel = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, WID_CTO_CARGO_ROW_FIRST + i);
		ver->Add(panel);
		NWidgetHorizontal *horiz = new NWidgetHorizontal;
		panel->Add(horiz);
		/* Cargo label */
		NWidgetBackground *label = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, WID_CTO_CARGO_LABEL_FIRST + i);
		label->SetFill(1, 0);
		label->SetResize(1, 0);
		horiz->Add(label);
		/* Orders dropdown */
		NWidgetLeaf *dropdown = new NWidgetLeaf(WWT_DROPDOWN, COLOUR_GREY, WID_CTO_CARGO_DROPDOWN_FIRST + i, STR_NULL, STR_EMPTY);
		dropdown->SetFill(1, 0);
		dropdown->SetResize(1, 0);
		horiz->Add(dropdown);
	}

	*biggest_index = WID_CTO_CARGO_DROPDOWN_LAST;
	return ver;
}

static NWidgetBase *MakeCargoTypeOrdersRowsLeft(int *biggest_index)
{
	return MakeCargoTypeOrdersRows(biggest_index, false);
}

static NWidgetBase *MakeCargoTypeOrdersRowsRight(int *biggest_index)
{
	return MakeCargoTypeOrdersRows(biggest_index, true);
}

/** Widgets definition of CargoTypeOrdersWindow. */
static const NWidgetPart _nested_cargo_type_orders_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CTO_CAPTION), SetDataTip(STR_NULL, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(WWT_LABEL, COLOUR_GREY, WID_CTO_HEADER), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_NULL, STR_NULL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL),
			NWidgetFunction(MakeCargoTypeOrdersRowsLeft),
			NWidget(NWID_SELECTION, COLOUR_GREY, WID_CTO_SELECT),
				NWidgetFunction(MakeCargoTypeOrdersRowsRight),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(1, 4), SetFill(1, 0), SetResize(1, 0), EndContainer(), // SPACER
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_CTO_SET_TO_ALL_LABEL), SetPadding(0, 0, 0, 12 + WidgetDimensions::unscaled.framerect.Horizontal()), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_CARGO_TYPE_ORDERS_SET_TO_ALL_LABEL, STR_CARGO_TYPE_ORDERS_SET_TO_ALL_TOOLTIP),
		EndContainer(),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_CTO_SET_TO_ALL_DROPDOWN), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_NULL, STR_CARGO_TYPE_ORDERS_SET_TO_ALL_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_CTO_CLOSEBTN), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_CARGO_TYPE_ORDERS_CLOSE_BUTTON, STR_TOOLTIP_CLOSE_WINDOW),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/** Window description for the 'load' variant of CargoTypeOrdersWindow. */
static WindowDesc _cargo_type_load_orders_widgets (
	WDP_AUTO, "view_cargo_type_load_order", 195, 186,
	WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, WC_VEHICLE_ORDERS,
	WDF_CONSTRUCTION,
	_nested_cargo_type_orders_widgets, lengthof(_nested_cargo_type_orders_widgets)
);

/** Window description for the 'unload' variant of CargoTypeOrdersWindow. */
static WindowDesc _cargo_type_unload_orders_widgets (
	WDP_AUTO, "view_cargo_type_unload_order", 195, 186,
	WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, WC_VEHICLE_ORDERS,
	WDF_CONSTRUCTION,
	_nested_cargo_type_orders_widgets, lengthof(_nested_cargo_type_orders_widgets)
);

/**
 * Show the CargoTypeOrdersWindow for an order.
 * @param v The vehicle the order belongs to.
 * @param parent The parent window.
 * @param order_id Which order to display/edit.
 * @param variant Which aspect of the order to display/edit: load or unload.
 * @pre \c v != nullptr
 */
void ShowCargoTypeOrdersWindow(const Vehicle *v, Window *parent, VehicleOrderID order_id, CargoTypeOrdersWindowVariant variant)
{
	WindowDesc &desc = (variant == CTOWV_LOAD) ? _cargo_type_load_orders_widgets : _cargo_type_unload_orders_widgets;
	CloseWindowById(desc.cls, v->index);
	CargoTypeOrdersWindow *w = new CargoTypeOrdersWindow(&desc, v, order_id, variant);
	w->parent = parent;
}


/** Order load types that could be given to station orders. */
static const StringID _station_load_types[][9][9] = {
	{
		/* No refitting. */
		{
			STR_EMPTY,
			INVALID_STRING_ID,
			STR_ORDER_FULL_LOAD,
			STR_ORDER_FULL_LOAD_ANY,
			STR_ORDER_NO_LOAD,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_LOAD,
		}, {
			STR_ORDER_UNLOAD,
			INVALID_STRING_ID,
			STR_ORDER_UNLOAD_FULL_LOAD,
			STR_ORDER_UNLOAD_FULL_LOAD_ANY,
			STR_ORDER_UNLOAD_NO_LOAD,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_UNLOAD_CARGO_TYPE_LOAD,
		}, {
			STR_ORDER_TRANSFER,
			INVALID_STRING_ID,
			STR_ORDER_TRANSFER_FULL_LOAD,
			STR_ORDER_TRANSFER_FULL_LOAD_ANY,
			STR_ORDER_TRANSFER_NO_LOAD,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_TRANSFER_CARGO_TYPE_LOAD,
		}, {
			/* Unload and transfer do not work together. */
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			STR_ORDER_NO_UNLOAD,
			INVALID_STRING_ID,
			STR_ORDER_NO_UNLOAD_FULL_LOAD,
			STR_ORDER_NO_UNLOAD_FULL_LOAD_ANY,
			STR_ORDER_NO_UNLOAD_NO_LOAD,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_NO_UNLOAD_CARGO_TYPE_LOAD,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			STR_ORDER_CARGO_TYPE_UNLOAD,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_UNLOAD_FULL_LOAD,
			STR_ORDER_CARGO_TYPE_UNLOAD_FULL_LOAD_ANY,
			STR_ORDER_CARGO_TYPE_UNLOAD_NO_LOAD,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_UNLOAD_CARGO_TYPE_LOAD,
		}
	}, {
		/* With auto-refitting. No loading and auto-refitting do not work together. */
		{
			STR_ORDER_AUTO_REFIT,
			INVALID_STRING_ID,
			STR_ORDER_FULL_LOAD_REFIT,
			STR_ORDER_FULL_LOAD_ANY_REFIT,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_LOAD_REFIT,
		}, {
			STR_ORDER_UNLOAD_REFIT,
			INVALID_STRING_ID,
			STR_ORDER_UNLOAD_FULL_LOAD_REFIT,
			STR_ORDER_UNLOAD_FULL_LOAD_ANY_REFIT,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_UNLOAD_CARGO_TYPE_LOAD_REFIT,
		}, {
			STR_ORDER_TRANSFER_REFIT,
			INVALID_STRING_ID,
			STR_ORDER_TRANSFER_FULL_LOAD_REFIT,
			STR_ORDER_TRANSFER_FULL_LOAD_ANY_REFIT,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_TRANSFER_CARGO_TYPE_LOAD_REFIT,
		}, {
			/* Unload and transfer do not work together. */
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			STR_ORDER_NO_UNLOAD_REFIT,
			INVALID_STRING_ID,
			STR_ORDER_NO_UNLOAD_FULL_LOAD_REFIT,
			STR_ORDER_NO_UNLOAD_FULL_LOAD_ANY_REFIT,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_NO_UNLOAD_CARGO_TYPE_LOAD_REFIT,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
			INVALID_STRING_ID, INVALID_STRING_ID, INVALID_STRING_ID,
		}, {
			STR_ORDER_CARGO_TYPE_UNLOAD_REFIT,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_UNLOAD_FULL_LOAD_REFIT,
			STR_ORDER_CARGO_TYPE_UNLOAD_FULL_LOAD_ANY_REFIT,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			INVALID_STRING_ID,
			STR_ORDER_CARGO_TYPE_UNLOAD_CARGO_TYPE_LOAD_REFIT,
		}
	}
};

static const StringID _order_non_stop_drowdown[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_NON_STOP_TO,
	STR_ORDER_GO_VIA,
	STR_ORDER_GO_NON_STOP_VIA,
	INVALID_STRING_ID
};

static const StringID _order_full_load_drowdown[] = {
	STR_ORDER_DROP_LOAD_IF_POSSIBLE,
	STR_EMPTY,
	STR_ORDER_DROP_FULL_LOAD_ALL,
	STR_ORDER_DROP_FULL_LOAD_ANY,
	STR_ORDER_DROP_NO_LOADING,
	STR_EMPTY,
	STR_EMPTY,
	STR_EMPTY,
	STR_ORDER_DROP_CARGO_TYPE_LOAD,
	INVALID_STRING_ID
};

static const StringID _order_unload_drowdown[] = {
	STR_ORDER_DROP_UNLOAD_IF_ACCEPTED,
	STR_ORDER_DROP_UNLOAD,
	STR_ORDER_DROP_TRANSFER,
	STR_EMPTY,
	STR_ORDER_DROP_NO_UNLOADING,
	STR_EMPTY,
	STR_EMPTY,
	STR_EMPTY,
	STR_ORDER_DROP_CARGO_TYPE_UNLOAD,
	INVALID_STRING_ID
};

static const StringID _order_goto_dropdown[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_TO_NEAREST_DEPOT,
	STR_ORDER_CONDITIONAL,
	STR_ORDER_SHARE,
	STR_ORDER_RELEASE_SLOT_BUTTON,
	STR_ORDER_CHANGE_COUNTER_BUTTON,
	STR_ORDER_LABEL_TEXT_BUTTON,
	STR_ORDER_LABEL_DEPARTURES_VIA_BUTTON,
	INVALID_STRING_ID
};

static const StringID _order_goto_dropdown_aircraft[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_TO_NEAREST_HANGAR,
	STR_ORDER_CONDITIONAL,
	STR_ORDER_SHARE,
	STR_ORDER_RELEASE_SLOT_BUTTON,
	STR_ORDER_CHANGE_COUNTER_BUTTON,
	STR_ORDER_LABEL_TEXT_BUTTON,
	STR_ORDER_LABEL_DEPARTURES_VIA_BUTTON,
	INVALID_STRING_ID
};

static const StringID _order_manage_list_dropdown[] = {
	STR_ORDER_REVERSE_ORDER_LIST,
	STR_ORDER_APPEND_REVERSED_ORDER_LIST,
	INVALID_STRING_ID
};

/** Variables for conditional orders; this defines the order of appearance in the dropdown box */
static const OrderConditionVariable _order_conditional_variable[] = {
	OCV_LOAD_PERCENTAGE,
	OCV_CARGO_LOAD_PERCENTAGE,
	OCV_RELIABILITY,
	OCV_MAX_RELIABILITY,
	OCV_MAX_SPEED,
	OCV_AGE,
	OCV_REMAINING_LIFETIME,
	OCV_REQUIRES_SERVICE,
	OCV_CARGO_WAITING,
	OCV_CARGO_WAITING_AMOUNT,
	OCV_CARGO_ACCEPTANCE,
	OCV_FREE_PLATFORMS,
	OCV_SLOT_OCCUPANCY,
	OCV_VEH_IN_SLOT,
	OCV_COUNTER_VALUE,
	OCV_TIME_DATE,
	OCV_TIMETABLE,
	OCV_DISPATCH_SLOT,
	OCV_PERCENT,
	OCV_UNCONDITIONALLY,
};

static const StringID _order_conditional_condition[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_NOT_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_LESS_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_LESS_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_MORE_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_MORE_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_IS_TRUE,
	STR_ORDER_CONDITIONAL_COMPARATOR_IS_FALSE,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_has[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_NO,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_LESS_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_LESS_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_MORE_THAN,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_MORE_EQUALS,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS,
	STR_ORDER_CONDITIONAL_COMPARATOR_HAS_NO,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_accepts[] = {
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_ACCEPTS,
	STR_ORDER_CONDITIONAL_COMPARATOR_DOES_NOT_ACCEPT,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_occupancy[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_OCCUPANCY_EMPTY,
	STR_ORDER_CONDITIONAL_COMPARATOR_OCCUPANCY_NOT_EMPTY,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_FULLY_OCCUPIED,
	STR_ORDER_CONDITIONAL_COMPARATOR_NOT_YET_FULLY_OCCUPIED,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_is_in_slot[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_IN_ACQUIRE_SLOT,
	STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_NOT_IN_ACQUIRE_SLOT,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_IN_SLOT,
	STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_NOT_IN_SLOT,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_is_in_slot_non_train[] = {
	STR_ORDER_CONDITIONAL_COMPARATOR_VEHICLE_IN_ACQUIRE_SLOT,
	STR_ORDER_CONDITIONAL_COMPARATOR_VEHICLE_NOT_IN_ACQUIRE_SLOT,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_VEHICLE_IN_SLOT,
	STR_ORDER_CONDITIONAL_COMPARATOR_VEHICLE_NOT_IN_SLOT,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_dispatch_slot_first[] = {
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_FIRST,
	STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_NOT_FIRST,
	INVALID_STRING_ID,
};

static const StringID _order_conditional_condition_dispatch_slot_last[] = {
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_NULL,
	STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_LAST,
	STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_NOT_LAST,
	INVALID_STRING_ID,
};

extern uint ConvertSpeedToDisplaySpeed(uint speed, VehicleType type);
extern uint ConvertDisplaySpeedToSpeed(uint speed, VehicleType type);

static const StringID _order_depot_action_dropdown[] = {
	STR_ORDER_DROP_GO_ALWAYS_DEPOT,
	STR_ORDER_DROP_SERVICE_DEPOT,
	STR_ORDER_DROP_HALT_DEPOT,
	STR_ORDER_DROP_SELL_DEPOT,
	INVALID_STRING_ID
};

static int DepotActionStringIndex(const Order *order)
{
	if (order->GetDepotActionType() & ODATFB_SELL) {
		return DA_SELL;
	} else if (order->GetDepotActionType() & ODATFB_HALT) {
		return DA_STOP;
	} else if (order->GetDepotOrderType() & ODTFB_SERVICE) {
		return DA_SERVICE;
	} else {
		return DA_ALWAYS_GO;
	}
}

static const StringID _order_refit_action_dropdown[] = {
	STR_ORDER_DROP_REFIT_AUTO,
	STR_ORDER_DROP_REFIT_AUTO_ANY,
	INVALID_STRING_ID
};

static const StringID _order_time_date_dropdown[] = {
	STR_TRACE_RESTRICT_TIME_MINUTE,
	STR_TRACE_RESTRICT_TIME_HOUR,
	STR_TRACE_RESTRICT_TIME_HOUR_MINUTE,
	STR_TRACE_RESTRICT_TIME_DAY,
	STR_TRACE_RESTRICT_TIME_MONTH,
	INVALID_STRING_ID
};

static const StringID _order_timetable_dropdown[] = {
	STR_TRACE_RESTRICT_TIMETABLE_LATENESS,
	STR_TRACE_RESTRICT_TIMETABLE_EARLINESS,
	INVALID_STRING_ID
};

static const StringID _order_dispatch_slot_dropdown[] = {
	STR_TRACE_RESTRICT_DISPATCH_SLOT_NEXT,
	STR_TRACE_RESTRICT_DISPATCH_SLOT_LAST,
	INVALID_STRING_ID
};

StringID OrderStringForVariable(const Vehicle *v, OrderConditionVariable ocv)
{
	if (ocv == OCV_VEH_IN_SLOT && v->type != VEH_TRAIN) return STR_ORDER_CONDITIONAL_VEHICLE_IN_SLOT;
	return STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + ocv;
}

/**
 * Draws an order in order or timetable GUI
 * @param v Vehicle the order belongs to
 * @param order The order to draw
 * @param order_index Index of the order in the orders of the vehicle
 * @param y Y position for drawing
 * @param selected True, if the order is selected
 * @param timetable True, when drawing in the timetable GUI
 * @param left Left border for text drawing
 * @param middle X position between order index and order text
 * @param right Right border for text drawing
 */
void DrawOrderString(const Vehicle *v, const Order *order, int order_index, int y, bool selected, bool timetable, int left, int middle, int right)
{
	bool rtl = _current_text_dir == TD_RTL;

	SpriteID sprite = rtl ? SPR_ARROW_LEFT : SPR_ARROW_RIGHT;
	Dimension sprite_size = GetSpriteSize(sprite);
	if (v->cur_real_order_index == order_index) {
		/* Draw two arrows before the next real order. */
		DrawSprite(sprite, PAL_NONE, rtl ? right -     sprite_size.width : left,                     y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
		DrawSprite(sprite, PAL_NONE, rtl ? right - 2 * sprite_size.width : left + sprite_size.width, y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
	} else if (v->cur_implicit_order_index == order_index) {
		/* Draw one arrow before the next implicit order; the next real order will still get two arrows. */
		DrawSprite(sprite, PAL_NONE, rtl ? right -     sprite_size.width : left,                     y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
	}

	TextColour colour = TC_BLACK;
	if (order->IsType(OT_IMPLICIT)) {
		colour = (selected ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
	} else {
		if (selected) {
			colour = TC_WHITE;
		} else {
			Colours order_colour = order->GetColour();
			if (order_colour != INVALID_COLOUR) colour = TC_IS_PALETTE_COLOUR | (TextColour)_colour_value[order_colour];
		}
	}

	SetDParam(0, order_index + 1);
	DrawString(left, rtl ? right - 2 * sprite_size.width - 3 : middle, y, STR_ORDER_INDEX, colour, SA_RIGHT | SA_FORCE);

	SetDParam(7, STR_EMPTY);
	SetDParam(10, STR_EMPTY);

	/* Check range for aircraft. */
	if (v->type == VEH_AIRCRAFT && Aircraft::From(v)->GetRange() > 0 && order->IsGotoOrder()) {
		const Order *next = order->next != nullptr ? order->next : v->GetFirstOrder();
		if (GetOrderDistance(order, next, v) > Aircraft::From(v)->acache.cached_max_range_sqr) SetDParam(10, STR_ORDER_OUT_OF_RANGE);
	}

	bool timetable_wait_time_valid = false;

	switch (order->GetType()) {
		case OT_DUMMY:
			SetDParam(0, STR_INVALID_ORDER);
			SetDParam(1, order->GetDestination());
			break;

		case OT_IMPLICIT:
			SetDParam(0, STR_ORDER_GO_TO_STATION);
			SetDParam(1, STR_ORDER_GO_TO);
			SetDParam(2, order->GetDestination());
			SetDParam(3, timetable ? STR_EMPTY : STR_ORDER_IMPLICIT);
			break;

		case OT_GOTO_STATION: {
			OrderLoadFlags load = order->GetLoadType();
			OrderUnloadFlags unload = order->GetUnloadType();
			bool valid_station = CanVehicleUseStation(v, Station::Get(order->GetDestination()));

			SetDParam(0, valid_station ? STR_ORDER_GO_TO_STATION : STR_ORDER_GO_TO_STATION_CAN_T_USE_STATION);
			SetDParam(1, STR_ORDER_GO_TO + (v->IsGroundVehicle() ? order->GetNonStopType() : 0));
			SetDParam(2, order->GetDestination());

			if (timetable) {
				/* Show only wait time in the timetable window. */
				SetDParam(3, STR_EMPTY);

				if (order->GetWaitTime() > 0 || order->IsWaitTimetabled()) {
					SetDParam(7, order->IsWaitTimetabled() ? STR_TIMETABLE_STAY_FOR : STR_TIMETABLE_STAY_FOR_ESTIMATED);
					SetTimetableParams(8, order->GetWaitTime());
				}
				timetable_wait_time_valid = true;
			} else {
				/* Show non-stop, refit and stop location only in the order window. */
				SetDParam(3, (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) ? STR_EMPTY : _station_load_types[order->IsRefit()][unload][load]);
				if (order->IsRefit()) {
					SetDParam(4, order->IsAutoRefit() ? STR_ORDER_AUTO_REFIT_ANY : CargoSpec::Get(order->GetRefitCargo())->name);
				}
				if (v->type == VEH_TRAIN && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) {
					/* Only show the stopping location if other than the default chosen by the player. */
					if (!_settings_client.gui.hide_default_stop_location || order->GetStopLocation() != (OrderStopLocation)(_settings_client.gui.stop_location)) {
						SetDParam(7, order->GetStopLocation() + STR_ORDER_STOP_LOCATION_NEAR_END);
					} else {
						SetDParam(7, STR_EMPTY);
					}
				}
				if (v->type == VEH_ROAD && order->GetRoadVehTravelDirection() != INVALID_DIAGDIR && _settings_game.pf.pathfinder_for_roadvehs == VPF_YAPF) {
					SetDParam(7, order->GetRoadVehTravelDirection() + STR_ORDER_RV_DIR_NE);
				}
			}
			break;
		}

		case OT_GOTO_DEPOT:
			if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
				/* Going to the nearest depot. */
				SetDParam(0, STR_ORDER_GO_TO_NEAREST_DEPOT_FORMAT);
				if (v->type == VEH_AIRCRAFT) {
					SetDParam(2, STR_ORDER_NEAREST_HANGAR);
					SetDParam(3, STR_EMPTY);
				} else {
					SetDParam(2, STR_ORDER_NEAREST_DEPOT);
					SetDParam(3, STR_ORDER_TRAIN_DEPOT + v->type);
				}
			} else {
				/* Going to a specific depot. */
				SetDParam(0, STR_ORDER_GO_TO_DEPOT_FORMAT);
				SetDParam(2, v->type);
				SetDParam(3, order->GetDestination());
			}

			if (order->GetDepotOrderType() & ODTFB_SERVICE) {
				SetDParam(1, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_ORDER_SERVICE_NON_STOP_AT : STR_ORDER_SERVICE_AT);
			} else {
				SetDParam(1, (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_ORDER_GO_NON_STOP_TO : STR_ORDER_GO_TO);
			}

			if (!timetable && (order->GetDepotActionType() & ODATFB_SELL)) {
				SetDParam(7, STR_ORDER_SELL_ORDER);
			} else {
				/* Do not show stopping in the depot in the timetable window. */
				if (!timetable && (order->GetDepotActionType() & ODATFB_HALT)) {
					SetDParam(7, STR_ORDER_STOP_ORDER);
				}

				/* Do not show refitting in the depot in the timetable window. */
				if (!timetable && order->IsRefit()) {
					SetDParam(7, (order->GetDepotActionType() & ODATFB_HALT) ? STR_ORDER_REFIT_STOP_ORDER : STR_ORDER_REFIT_ORDER);
					SetDParam(8, CargoSpec::Get(order->GetRefitCargo())->name);
				}
			}

			if (timetable) {
				if (order->GetWaitTime() > 0 || order->IsWaitTimetabled()) {
					SetDParam(7, order->IsWaitTimetabled() ? STR_TIMETABLE_STAY_FOR : STR_TIMETABLE_STAY_FOR_ESTIMATED);
					SetTimetableParams(8, order->GetWaitTime());
				}
				timetable_wait_time_valid = !(order->GetDepotActionType() & ODATFB_HALT);
			}
			break;

		case OT_GOTO_WAYPOINT: {
			StringID str = (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) ? STR_ORDER_GO_NON_STOP_TO_WAYPOINT : STR_ORDER_GO_TO_WAYPOINT;
			if (order->GetWaypointFlags() & OWF_REVERSE) str += STR_ORDER_GO_TO_WAYPOINT_REVERSE - STR_ORDER_GO_TO_WAYPOINT;
			SetDParam(0, str);
			SetDParam(1, order->GetDestination());
			if (timetable && order->IsWaitTimetabled()) {
				SetDParam(7, STR_TIMETABLE_STAY_FOR);
				SetTimetableParams(8, order->GetWaitTime());
				timetable_wait_time_valid = true;
			}
			if (!timetable && v->type == VEH_ROAD && order->GetRoadVehTravelDirection() != INVALID_DIAGDIR && _settings_game.pf.pathfinder_for_roadvehs == VPF_YAPF) {
				SetDParam(7, order->GetRoadVehTravelDirection() + STR_ORDER_RV_DIR_NE);
			}
			break;
		}

		case OT_CONDITIONAL: {
			auto set_station_id = [&order](uint index, StringParameters &sp = _global_string_params) {
				const Station *st = Station::GetIfValid(GB(order->GetXData2(), 0, 16) - 1);
				if (st == nullptr) {
					sp.SetParam(index, STR_ORDER_CONDITIONAL_UNDEFINED_STATION);
				} else {
					sp.SetParam(index, STR_JUST_STATION);
					sp.SetParam(index + 1, st->index);
				}
			};

			SetDParam(1, order->GetConditionSkipToOrder() + 1);
			const OrderConditionVariable ocv = order->GetConditionVariable();
			/* handle some non-ordinary cases seperately */
			if (ocv == OCV_UNCONDITIONALLY) {
				SetDParam(0, STR_ORDER_CONDITIONAL_UNCONDITIONAL);
			} else if (ocv == OCV_PERCENT) {
				SetDParam(0, STR_ORDER_CONDITIONAL_PERCENT_DISPLAY);
				SetDParam(2, order->GetConditionValue());
			} else if (ocv == OCV_FREE_PLATFORMS) {
				SetDParam(0, STR_ORDER_CONDITIONAL_FREE_PLATFORMS_DISPLAY);
				set_station_id(2);
				SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_HAS + order->GetConditionComparator());
				SetDParam(5, order->GetConditionValue());
			} else if (ocv == OCV_SLOT_OCCUPANCY) {
				if (TraceRestrictSlot::IsValidID(order->GetXData())) {
					SetDParam(0, STR_ORDER_CONDITIONAL_SLOT);
					SetDParam(2, order->GetXData());
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_INVALID_SLOT);
					SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
				}
				switch (order->GetConditionComparator()) {
					case OCC_IS_TRUE:
					case OCC_IS_FALSE:
					case OCC_EQUALS:
					case OCC_NOT_EQUALS: {
						SetDParam(3, _order_conditional_condition_occupancy[order->GetConditionComparator()]);
						break;
					}
					default:
						NOT_REACHED();
				}
			} else if (ocv == OCV_VEH_IN_SLOT) {
				if (TraceRestrictSlot::IsValidID(order->GetXData())) {
					SetDParam(0, STR_ORDER_CONDITIONAL_IN_SLOT);
					SetDParam(3, order->GetXData());
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_IN_INVALID_SLOT);
					SetDParam(3, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
				}
				switch (order->GetConditionComparator()) {
					case OCC_IS_TRUE:
					case OCC_IS_FALSE:
					case OCC_EQUALS:
					case OCC_NOT_EQUALS: {
						const StringID *strs = v->type == VEH_TRAIN ? _order_conditional_condition_is_in_slot : _order_conditional_condition_is_in_slot_non_train;
						SetDParam(2, strs[order->GetConditionComparator()]);
						break;
					}
					default:
						NOT_REACHED();
				}
			} else if (ocv == OCV_CARGO_LOAD_PERCENTAGE) {
				SetDParam(0, STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE_DISPLAY);
				SetDParam(2, CargoSpec::Get(order->GetConditionValue())->name);
				SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
				SetDParam(4, order->GetXData());
			} else if (ocv == OCV_CARGO_WAITING_AMOUNT) {
				char buf[512] = "";
				ArrayStringParameters<10> tmp_params;
				StringID substr;

				tmp_params.SetParam(0, order->GetConditionSkipToOrder() + 1);
				tmp_params.SetParam(1, CargoSpec::Get(order->GetConditionValue())->name);
				set_station_id(2, tmp_params);

				if (GB(order->GetXData(), 16, 16) == 0) {
					substr = STR_ORDER_CONDITIONAL_CARGO_WAITING_AMOUNT_DISPLAY;
					tmp_params.SetParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
					tmp_params.SetParam(5, order->GetConditionValue());
					tmp_params.SetParam(6, GB(order->GetXData(), 0, 16));
				} else {
					substr = STR_ORDER_CONDITIONAL_CARGO_WAITING_AMOUNT_VIA_DISPLAY;
					const Station *via_st = Station::GetIfValid(GB(order->GetXData(), 16, 16) - 2);
					if (via_st == nullptr) {
						tmp_params.SetParam(4, STR_ORDER_CONDITIONAL_UNDEFINED_STATION);
					} else {
						tmp_params.SetParam(4, STR_JUST_STATION);
						tmp_params.SetParam(5, via_st->index);
					}
					tmp_params.SetParam(6, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
					tmp_params.SetParam(7, order->GetConditionValue());
					tmp_params.SetParam(8, GB(order->GetXData(), 0, 16));
				}
				char *end = GetStringWithArgs(buf, substr, tmp_params, lastof(buf));
				_temp_special_strings[0].assign(buf, end);
				SetDParam(0, SPECSTR_TEMP_START);
			} else if (ocv == OCV_COUNTER_VALUE) {
				if (TraceRestrictCounter::IsValidID(GB(order->GetXData(), 16, 16))) {
					SetDParam(0, STR_ORDER_CONDITIONAL_COUNTER);
					SetDParam(2, GB(order->GetXData(), 16, 16));
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_INVALID_COUNTER);
					SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
				}
				SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
				SetDParam(4, GB(order->GetXData(), 0, 16));
			} else if (ocv == OCV_TIME_DATE) {
				SetDParam(0, (order->GetConditionValue() == TRTDVF_HOUR_MINUTE) ? STR_ORDER_CONDITIONAL_TIME_HHMM : STR_ORDER_CONDITIONAL_NUM);
				SetDParam(2, STR_TRACE_RESTRICT_TIME_MINUTE_ITEM + order->GetConditionValue());
				SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
				SetDParam(4, order->GetXData());
			} else if (ocv == OCV_TIMETABLE) {
				SetDParam(0, STR_ORDER_CONDITIONAL_TIMETABLE);
				SetDParam(2, STR_TRACE_RESTRICT_TIMETABLE_LATENESS + order->GetConditionValue());
				SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
				SetDParam(4, order->GetXData());
			} else if (ocv == OCV_DISPATCH_SLOT) {
				SetDParam(0, STR_ORDER_CONDITIONAL_DISPATCH_SLOT_DISPLAY);
				if (GB(order->GetXData(), 0, 16) != UINT16_MAX) {
					const DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(GB(order->GetXData(), 0, 16));
					if (ds.ScheduleName().empty()) {
						char buf[256];
						auto tmp_params = MakeParameters(GB(order->GetXData(), 0, 16) + 1);
						char *end = GetStringWithArgs(buf, STR_TIMETABLE_ASSIGN_SCHEDULE_ID, tmp_params, lastof(buf));
						_temp_special_strings[0].assign(buf, end);
					} else {
						_temp_special_strings[0] = ds.ScheduleName();
					}
					SetDParam(2, SPECSTR_TEMP_START);
				} else {
					SetDParam(2, STR_TIMETABLE_ASSIGN_SCHEDULE_NONE);
				}
				SetDParam(3, STR_TRACE_RESTRICT_DISPATCH_SLOT_NEXT + (order->GetConditionValue() / 2));
				SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_FIRST + ((order->GetConditionComparator() == OCC_IS_FALSE) ? 1 : 0) +
						((order->GetConditionValue() % 2) ? 2 : 0));
			} else {
				OrderConditionComparator occ = order->GetConditionComparator();
				SetDParam(0, (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) ? STR_ORDER_CONDITIONAL_TRUE_FALSE : STR_ORDER_CONDITIONAL_NUM);
				SetDParam(2, (ocv == OCV_CARGO_ACCEPTANCE || ocv == OCV_CARGO_WAITING || ocv == OCV_FREE_PLATFORMS)
						? STR_ORDER_CONDITIONAL_NEXT_STATION : OrderStringForVariable(v, ocv));

				uint value = order->GetConditionValue();
				switch (ocv) {
					case OCV_CARGO_ACCEPTANCE:
						SetDParam(0, STR_ORDER_CONDITIONAL_CARGO_ACCEPTANCE);
						set_station_id(2);
						SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_ACCEPTS + occ - OCC_IS_TRUE);
						SetDParam(5, CargoSpec::Get(value)->name);
						break;
					case OCV_CARGO_WAITING:
						SetDParam(0, STR_ORDER_CONDITIONAL_CARGO_WAITING_DISPLAY);
						set_station_id(2);
						SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_HAS + occ - OCC_IS_TRUE);
						SetDParam(5, CargoSpec::Get(value)->name);
						break;
					case OCV_REQUIRES_SERVICE:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + occ);
						break;
					case OCV_MAX_SPEED:
						value = ConvertSpeedToDisplaySpeed(value, v->type);
						/* FALL THROUGH */
					default:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + occ);
						SetDParam(4, value);
				}
			}

			if (timetable && (order->IsWaitTimetabled() || order->GetWaitTime() > 0)) {
				SetDParam(7, order->IsWaitTimetabled() ? STR_TIMETABLE_AND_TRAVEL_FOR : STR_TIMETABLE_AND_TRAVEL_FOR_ESTIMATED);
				SetTimetableParams(8, order->GetWaitTime());
			} else {
				SetDParam(7, STR_EMPTY);
			}

			break;
		}

		case OT_RELEASE_SLOT:
			SetDParam(0, STR_ORDER_RELEASE_SLOT);
			if (order->GetDestination() == INVALID_TRACE_RESTRICT_SLOT_ID) {
				SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
			} else {
				SetDParam(1, STR_TRACE_RESTRICT_SLOT_NAME);
				SetDParam(2, order->GetDestination());
			}
			break;

		case OT_COUNTER:
			switch (static_cast<TraceRestrictCounterCondOpField>(order->GetCounterOperation())) {
				case TRCCOF_INCREASE:
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_INCREASE_ITEM);
					break;

				case TRCCOF_DECREASE:
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_DECREASE_ITEM);
					break;

				case TRCCOF_SET:
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_SET_ITEM);
					break;

				default:
					NOT_REACHED();
					break;
			}
			if (order->GetDestination() == INVALID_TRACE_RESTRICT_COUNTER_ID) {
				SetDParam(1, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
			} else {
				SetDParam(1, STR_TRACE_RESTRICT_COUNTER_NAME);
				SetDParam(2, order->GetDestination());
			}
			SetDParam(3, order->GetXData());
			break;

		case OT_LABEL: {
			auto show_destination_subtype = [&](uint offset) {
				if (Waypoint::IsValidID(order->GetDestination())) {
					SetDParam(offset, STR_WAYPOINT_NAME);
				} else {
					SetDParam(offset, STR_STATION_NAME);
				}
				SetDParam(offset + 1, order->GetDestination());
			};
			switch (order->GetLabelSubType()) {
				case OLST_TEXT: {
					SetDParam(0, STR_ORDER_LABEL_TEXT);
					const char *text = order->GetLabelText();
					SetDParamStr(1, StrEmpty(text) ? "" : text);
					break;
				}

				case OLST_DEPARTURES_VIA:
					SetDParam(0, STR_ORDER_LABEL_DEPARTURES_VIA);
					SetDParam(1, STR_ORDER_LABEL_DEPARTURES_SHOW_AS_VIA);
					show_destination_subtype(2);
					break;

				case OLST_DEPARTURES_REMOVE_VIA:
					SetDParam(0, STR_ORDER_LABEL_DEPARTURES_VIA);
					SetDParam(1, STR_ORDER_LABEL_DEPARTURES_REMOVE_VIA);
					show_destination_subtype(2);
					break;

				default:
					SetDParam(0, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED_RED);
					break;
			}
			break;
		}

		default: NOT_REACHED();
	}

	int edge = DrawString(rtl ? left : middle, rtl ? middle : right, y, STR_ORDER_TEXT, colour);

	if (timetable && timetable_wait_time_valid && order->GetLeaveType() != OLT_NORMAL && edge != 0) {
		edge = DrawString(rtl ? left : edge + 3, rtl ? edge - 3 : right, y, STR_TIMETABLE_LEAVE_EARLY_ORDER + order->GetLeaveType() - OLT_LEAVE_EARLY, colour);
	}
	if (timetable && HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && order->IsScheduledDispatchOrder(false) && edge != 0) {
		StringID str = order->IsWaitTimetabled() ? STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER : STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER_NO_WAIT_TIME;
		const DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(order->GetDispatchScheduleIndex());
		if (!ds.ScheduleName().empty()) {
			SetDParam(0, STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER_NAMED_SCHEDULE);
			SetDParamStr(1, ds.ScheduleName().c_str());
		} else {
			SetDParam(0, v->orders->GetScheduledDispatchScheduleCount() > 1 ? STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER_SCHEDULE_INDEX : STR_EMPTY);
			SetDParam(1, order->GetDispatchScheduleIndex() + 1);
		}
		edge = DrawString(rtl ? left : edge + 3, rtl ? edge - 3 : right, y, str, colour);
	}

	if (timetable && (timetable_wait_time_valid || order->IsType(OT_CONDITIONAL)) && order->IsWaitFixed() && edge != 0) {
		Dimension lock_d = GetSpriteSize(SPR_LOCK);
		DrawPixelInfo tmp_dpi;
		if (FillDrawPixelInfo(&tmp_dpi, rtl ? left : middle, y, rtl ? middle - left : right - middle, lock_d.height)) {
			AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);

			DrawSprite(SPR_LOCK, PAL_NONE, rtl ? edge - 3 - lock_d.width - left : edge + 3 - middle, 0);
		}
	}
}

/**
 * Get the order command a vehicle can do in a given tile.
 * @param v Vehicle involved.
 * @param tile Tile being queried.
 * @return The order associated to vehicle v in given tile (or empty order if vehicle can do nothing in the tile).
 */
static Order GetOrderCmdFromTile(const Vehicle *v, TileIndex tile)
{
	/* Hack-ish; unpack order 0, so everything gets initialised with either zero
	 * or a suitable default value for the variable. Then also override the index
	 * as it is not coming from a pool, so would be initialised. */
	Order order(0);
	order.index = 0;

	/* check depot first */
	if (IsDepotTypeTile(tile, (TransportType)(uint)v->type) && IsInfraTileUsageAllowed(v->type, v->owner, tile)) {
		if (v->type == VEH_ROAD && ((GetPresentRoadTypes(tile) & RoadVehicle::From(v)->compatible_roadtypes) == 0)) {
			order.Free();
			return order;
		}
		order.MakeGoToDepot(v->type == VEH_AIRCRAFT ? GetStationIndex(tile) : GetDepotIndex(tile),
				ODTFB_PART_OF_ORDERS,
				((_settings_client.gui.new_nonstop || _settings_game.order.nonstop_only) && v->IsGroundVehicle()) ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);

		if (_ctrl_pressed) order.SetDepotOrderType((OrderDepotTypeFlags)(order.GetDepotOrderType() ^ ODTFB_SERVICE));

		return order;
	}

	/* check rail waypoint */
	if (IsRailWaypointTile(tile) &&
			v->type == VEH_TRAIN &&
			IsInfraTileUsageAllowed(VEH_TRAIN, v->owner, tile)) {
		order.MakeGoToWaypoint(GetStationIndex(tile));
		if (_settings_client.gui.new_nonstop != _ctrl_pressed || _settings_game.order.nonstop_only) order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
		return order;
	}

	/* check road waypoint */
	if (IsRoadWaypointTile(tile) &&
			v->type == VEH_ROAD &&
			IsInfraTileUsageAllowed(VEH_ROAD, v->owner, tile)) {
		order.MakeGoToWaypoint(GetStationIndex(tile));
		if (_settings_client.gui.new_nonstop != _ctrl_pressed || _settings_game.order.nonstop_only) order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
		return order;
	}

	/* check buoy (no ownership) */
	if (IsBuoyTile(tile) && v->type == VEH_SHIP) {
		order.MakeGoToWaypoint(GetStationIndex(tile));
		return order;
	}

	/* check for station or industry with neutral station */
	if (IsTileType(tile, MP_STATION) || IsTileType(tile, MP_INDUSTRY)) {
		const Station *st = nullptr;

		if (IsTileType(tile, MP_STATION)) {
			st = Station::GetByTile(tile);
		} else {
			const Industry *in = Industry::GetByTile(tile);
			st = in->neutral_station;
		}
		if (st != nullptr && IsInfraUsageAllowed(v->type, v->owner, st->owner)) {
			byte facil;
			switch (v->type) {
				case VEH_SHIP:     facil = FACIL_DOCK;    break;
				case VEH_TRAIN:    facil = FACIL_TRAIN;   break;
				case VEH_AIRCRAFT: facil = FACIL_AIRPORT; break;
				case VEH_ROAD:     facil = FACIL_BUS_STOP | FACIL_TRUCK_STOP; break;
				default: NOT_REACHED();
			}
			if (st->facilities & facil) {
				order.MakeGoToStation(st->index);
				if (_ctrl_pressed) order.SetLoadType(OLF_FULL_LOAD_ANY);
				if ((_settings_client.gui.new_nonstop || _settings_game.order.nonstop_only) && v->IsGroundVehicle()) order.SetNonStopType(ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
				order.SetStopLocation(v->type == VEH_TRAIN ? (OrderStopLocation)(_settings_client.gui.stop_location) : OSL_PLATFORM_FAR_END);
				return order;
			}
		}
	}

	/* not found */
	order.Free();
	return order;
}

/** Hotkeys for order window. */
enum {
	OHK_SKIP,
	OHK_DELETE,
	OHK_GOTO,
	OHK_NONSTOP,
	OHK_VIA,
	OHK_FULLLOAD,
	OHK_UNLOAD,
	OHK_NEAREST_DEPOT,
	OHK_ALWAYS_SERVICE,
	OHK_TRANSFER,
	OHK_NO_UNLOAD,
	OHK_NO_LOAD,
};

/**
 * %Order window code for all vehicles.
 *
 * At the bottom of the window two button rows are located for changing the orders of the vehicle.
 *
 * \section top-row Top row
 * The top-row is for manipulating an individual order. What row is displayed depends on the type of vehicle, and whether or not you are the owner of the vehicle.
 *
 * The top-row buttons of one of your trains or road vehicles is one of the following three cases:
 * \verbatim
 * +-----------------+-----------------+-----------------+-----------------+
 * |    NON-STOP     |    FULL_LOAD    |     UNLOAD      |      REFIT      | (normal)
 * +-----------------+-----+-----------+-----------+-----+-----------------+
 * |       COND_VAR        |    COND_COMPARATOR    |      COND_VALUE       | (for conditional orders)
 * +-----------------+-----+-----------+-----------+-----+-----------------+
 * |    NON-STOP     |      REFIT      |     SERVICE     |     (empty)     | (for depot orders)
 * +-----------------+-----------------+-----------------+-----------------+
 * \endverbatim
 *
 * Airplanes and ships have one of the following three top-row button rows:
 * \verbatim
 * +-----------------+-----------------+-----------------+
 * |    FULL_LOAD    |     UNLOAD      |      REFIT      | (normal)
 * +-----------------+-----------------+-----------------+
 * |    COND_VAR     | COND_COMPARATOR |   COND_VALUE    | (for conditional orders)
 * +-----------------+--------+--------+-----------------+
 * |            REFIT         |          SERVICE         | (for depot order)
 * +--------------------------+--------------------------+
 * \endverbatim
 *
 * \section bottom-row Bottom row
 * The second row (the bottom row) is for manipulating the list of orders:
 * \verbatim
 * +-----------------+-----------------+-----------------+
 * |      SKIP       |     DELETE      |      GOTO       |
 * +-----------------+-----------------+-----------------+
 * \endverbatim
 *
 * For vehicles of other companies, both button rows are not displayed.
 */
struct OrdersWindow : public GeneralVehicleWindow {
private:
	/** Under what reason are we using the PlaceObject functionality? */
	enum OrderPlaceObjectState {
		OPOS_NONE,
		OPOS_GOTO,
		OPOS_CONDITIONAL,
		OPOS_SHARE,
		OPOS_COND_VIA,
		OPOS_COND_STATION,
		OPOS_CONDITIONAL_RETARGET,
		OPOS_DEPARTURE_VIA,
		OPOS_END,
	};

	/** Displayed planes of the #NWID_SELECTION widgets. */
	enum DisplayPane {
		/* WID_O_SEL_TOP_ROW_GROUNDVEHICLE */
		DP_GROUNDVEHICLE_ROW_NORMAL      = 0, ///< Display the row for normal/depot orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_CONDITIONAL = 1, ///< Display the row for conditional orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_SLOT        = 2, ///< Display the row for release slot orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_COUNTER     = 3, ///< Display the row for change counter orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_TEXT_LABEL  = 4, ///< Display the row for text label orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_DEPARTURES  = 5, ///< Display the row for departure via label orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_EMPTY       = 6, ///< Display the row for no buttons in the top row of the train/rv order window.

		/* WID_O_SEL_TOP_LEFT */
		DP_LEFT_LOAD       = 0, ///< Display 'load' in the left button of the top row of the train/rv order window.
		DP_LEFT_REFIT      = 1, ///< Display 'refit' in the left button of the top row of the train/rv order window.
		DP_LEFT_REVERSE    = 2, ///< Display 'reverse' in the left button of the top row of the train/rv order window.

		/* WID_O_SEL_TOP_MIDDLE */
		DP_MIDDLE_UNLOAD   = 0, ///< Display 'unload' in the middle button of the top row of the train/rv order window.
		DP_MIDDLE_SERVICE  = 1, ///< Display 'service' in the middle button of the top row of the train/rv order window.

		/* WID_O_SEL_TOP_RIGHT */
		DP_RIGHT_EMPTY     = 0, ///< Display an empty panel in the right button of the top row of the train/rv order window.
		DP_RIGHT_REFIT     = 1, ///< Display 'refit' in the right button of the top  row of the train/rv order window.

		/* WID_O_SEL_TOP_ROW */
		DP_ROW_LOAD        = 0, ///< Display 'load' / 'unload' / 'refit' buttons in the top row of the ship/airplane order window.
		DP_ROW_DEPOT       = 1, ///< Display 'refit' / 'service' buttons in the top row of the ship/airplane order window.
		DP_ROW_CONDITIONAL = 2, ///< Display the conditional order buttons in the top row of the ship/airplane order window.
		DP_ROW_SLOT        = 3, ///< Display the release slot buttons in the top row of the ship/airplane order window.
		DP_ROW_COUNTER     = 4, ///< Display the change counter buttons in the top row of the ship/airplane order window.
		DP_ROW_TEXT_LABEL  = 5, ///< Display the text label buttons in the top row of the ship/airplane order window.
		DP_ROW_DEPARTURES  = 6, ///< Display the row for departure via label orders in the top row of the ship/airplane order window.
		DP_ROW_EMPTY       = 7, ///< Display no buttons in the top row of the ship/airplane order window.

		/* WID_O_SEL_COND_VALUE */
		DP_COND_VALUE_NUMBER = 0, ///< Display number widget
		DP_COND_VALUE_CARGO  = 1, ///< Display dropdown widget cargo types
		DP_COND_VALUE_SLOT   = 2, ///< Display dropdown widget tracerestrict slots

		/* WID_O_SEL_COND_AUX */
		DP_COND_AUX_CARGO = 0, ///< Display dropdown widget cargo types
		DP_COND_TIME_DATE = 1, ///< Display dropdown for current time/date field
		DP_COND_TIMETABLE = 2, ///< Display dropdown for timetable field
		DP_COND_COUNTER = 3,   ///< Display dropdown widget counters
		DP_COND_SCHED_SELECT = 4, ///< Display dropdown for scheduled dispatch schedule selection

		/* WID_O_SEL_COND_AUX2 */
		DP_COND_AUX2_VIA = 0, ///< Display via button
		DP_COND_AUX2_SCHED_TEST = 1, ///< Display dropdown for scheduled dispatch test selection

		/* WID_O_SEL_COND_AUX3 */
		DP_COND_AUX3_STATION = 0, ///< Display station button

		/* WID_O_SEL_BOTTOM_MIDDLE */
		DP_BOTTOM_MIDDLE_DELETE       = 0, ///< Display 'delete' in the middle button of the bottom row of the vehicle order window.
		DP_BOTTOM_MIDDLE_STOP_SHARING = 1, ///< Display 'stop sharing' in the middle button of the bottom row of the vehicle order window.

		/* WID_O_SEL_SHARED */
		DP_SHARED_LIST       = 0, ///< Display shared order list button
		DP_SHARED_VEH_GROUP  = 1, ///< Display add veh to new group button

		/* WID_O_SEL_MGMT */
		DP_MGMT_BTN          = 0, ///< Display order management button
		DP_MGMT_LIST_BTN     = 1, ///< Display order list management button
	};

	int selected_order;
	VehicleOrderID order_over;         ///< Order over which another order is dragged, \c INVALID_VEH_ORDER_ID if none.
	OrderPlaceObjectState goto_type;
	Scrollbar *vscroll;
	bool can_do_refit;     ///< Vehicle chain can be refitted in depot.
	bool can_do_autorefit; ///< Vehicle chain can be auto-refitted.
	int query_text_widget; ///< widget which most recently called ShowQueryString
	int current_aux_plane;
	int current_aux2_plane;
	int current_aux3_plane;
	int current_mgmt_plane;

	/**
	 * Return the memorised selected order.
	 * @return the memorised order if it is a valid one
	 *  else return the number of orders
	 */
	VehicleOrderID OrderGetSel() const
	{
		int num = this->selected_order;
		return (num >= 0 && num < vehicle->GetNumOrders()) ? num : vehicle->GetNumOrders();
	}

	/**
	 * Calculate the selected order.
	 * The calculation is based on the relative (to the window) y click position and
	 *  the position of the scrollbar.
	 *
	 * @param y Y-value of the click relative to the window origin
	 * @return The selected order if the order is valid, else return \c INVALID_VEH_ORDER_ID.
	 */
	VehicleOrderID GetOrderFromPt(int y)
	{
		int sel = this->vscroll->GetScrolledRowFromWidget(y, this, WID_O_ORDER_LIST, WidgetDimensions::scaled.framerect.top);
		if (sel == INT_MAX) return INVALID_VEH_ORDER_ID;
		/* One past the orders is the 'End of Orders' line. */
		assert(IsInsideBS(sel, 0, vehicle->GetNumOrders() + 1));
		return sel;
	}

	/**
	 * Determine which strings should be displayed in the conditional comparator dropdown
	 *
	 * @param order the order to evaluate
	 * @return the StringIDs to display
	 */
	static const StringID *GetComparatorStrings(const Vehicle *v, const Order *order)
	{
		if (order == nullptr) return _order_conditional_condition;
		switch (order->GetConditionVariable()) {
			case OCV_FREE_PLATFORMS:
			case OCV_CARGO_WAITING:
				return _order_conditional_condition_has;

			case OCV_CARGO_ACCEPTANCE:
				return _order_conditional_condition_accepts;

			case OCV_SLOT_OCCUPANCY:
				return _order_conditional_condition_occupancy;

			case OCV_VEH_IN_SLOT:
				return v->type == VEH_TRAIN ? _order_conditional_condition_is_in_slot : _order_conditional_condition_is_in_slot_non_train;

			case OCV_DISPATCH_SLOT:
				return (order->GetConditionValue() % 2) == 0 ? _order_conditional_condition_dispatch_slot_first : _order_conditional_condition_dispatch_slot_last;

			default:
				return _order_conditional_condition;
		}
	}

	bool InsertNewOrder(uint64 order_pack)
	{
		return DoCommandPEx(this->vehicle->tile, this->vehicle->index, this->OrderGetSel(), order_pack, CMD_INSERT_ORDER | CMD_MSG(STR_ERROR_CAN_T_INSERT_NEW_ORDER), nullptr, nullptr, 0);
	}

	bool ModifyOrder(VehicleOrderID sel_ord, uint32 p2, bool error_msg = true, const char *text = nullptr)
	{
		return ::ModifyOrder(this->vehicle, sel_ord, p2, error_msg, text);
	}

	/**
	 * Handle the click on the goto button.
	 */
	void OrderClick_Goto(OrderPlaceObjectState type)
	{
		assert(type > OPOS_NONE && type < OPOS_END);

		static const HighLightStyle goto_place_style[OPOS_END - 1] = {
			HT_RECT | HT_VEHICLE, // OPOS_GOTO
			HT_NONE,              // OPOS_CONDITIONAL
			HT_VEHICLE,           // OPOS_SHARE
			HT_RECT,              // OPOS_COND_VIA
			HT_RECT,              // OPOS_COND_STATION
			HT_NONE,              // OPOS_CONDITIONAL_RETARGET
			HT_RECT,              // OPOS_DEPARTURE_VIA
		};
		SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, goto_place_style[type - 1], this);
		this->goto_type = type;
		this->SetWidgetDirty(WID_O_GOTO);
		this->SetWidgetDirty(WID_O_COND_AUX_VIA);
		this->SetWidgetDirty(WID_O_COND_AUX_STATION);
		this->SetWidgetDirty(WID_O_MGMT_BTN);
	}

	/**
	 * Handle the click on the full load button.
	 * @param load_type Load flag to apply. If matches existing load type, toggles to default of 'load if possible'.
	 * @param toggle If we toggle or not (used for hotkey behavior)
	 */
	void OrderClick_FullLoad(OrderLoadFlags load_type, bool toggle = false)
	{
		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr) return;

		if (toggle && order->GetLoadType() == load_type) {
			load_type = OLF_LOAD_IF_POSSIBLE; // reset to 'default'
		}
		if (order->GetLoadType() != load_type) {
			this->ModifyOrder(sel_ord, MOF_LOAD | (load_type << 8));
		}

		if (load_type == OLFB_CARGO_TYPE_LOAD) ShowCargoTypeOrdersWindow(this->vehicle, this, sel_ord, CTOWV_LOAD);
	}

	/**
	 * Handle the click on the service.
	 */
	void OrderClick_Service(int i)
	{
		VehicleOrderID sel_ord = this->OrderGetSel();

		if (i < 0) {
			const Order *order = this->vehicle->GetOrder(sel_ord);
			if (order == nullptr) return;
			i = (order->GetDepotOrderType() & ODTFB_SERVICE) ? DA_ALWAYS_GO : DA_SERVICE;
		}
		this->ModifyOrder(sel_ord, MOF_DEPOT_ACTION | (i << 8));
	}

	/**
	 * Handle the click on the service in nearest depot button.
	 */
	void OrderClick_NearestDepot()
	{
		Order order;
		order.next = nullptr;
		order.index = 0;
		order.MakeGoToDepot(INVALID_DEPOT, ODTFB_PART_OF_ORDERS,
				(_settings_client.gui.new_nonstop || _settings_game.order.nonstop_only) && this->vehicle->IsGroundVehicle() ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);
		order.SetDepotActionType(ODATFB_NEAREST_DEPOT);

		this->InsertNewOrder(order.Pack());
	}

	/**
	 * Handle the click on the release slot button.
	 */
	void OrderClick_ReleaseSlot()
	{
		Order order;
		order.next = nullptr;
		order.index = 0;
		order.MakeReleaseSlot();

		this->InsertNewOrder(order.Pack());
	}

	/**
	 * Handle the click on the change counter button.
	 */
	void OrderClick_ChangeCounter()
	{
		Order order;
		order.next = nullptr;
		order.index = 0;
		order.MakeChangeCounter();

		this->InsertNewOrder(order.Pack());
	}

	/**
	 * Handle the click on the text label button.
	 */
	void OrderClick_TextLabel()
	{
		Order order;
		order.next = nullptr;
		order.index = 0;
		order.MakeLabel(OLST_TEXT);

		this->InsertNewOrder(order.Pack());
	}

	/**
	 * Handle the click on the unload button.
	 * @param unload_type Unload flag to apply. If matches existing unload type, toggles to default of 'unload if possible'.
	 * @param toggle If we toggle or not (used for hotkey behavior)
	 */
	void OrderClick_Unload(OrderUnloadFlags unload_type, bool toggle = false)
	{
		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr) return;

		if (toggle && order->GetUnloadType() == unload_type) {
			unload_type = OUF_UNLOAD_IF_POSSIBLE;
		}
		if (order->GetUnloadType() == unload_type && unload_type != OUFB_CARGO_TYPE_UNLOAD) return; // If we still match, do nothing

		if (order->GetUnloadType() != unload_type) {
			this->ModifyOrder(sel_ord, MOF_UNLOAD | (unload_type << 8));
		}

		if (unload_type == OUFB_TRANSFER || unload_type == OUFB_UNLOAD) {
			/* Transfer and unload orders with leave empty as default */
			this->ModifyOrder(sel_ord, MOF_LOAD | (OLFB_NO_LOAD << 8), false);
			this->SetWidgetDirty(WID_O_FULL_LOAD);
		} else if (unload_type == OUFB_CARGO_TYPE_UNLOAD) {
			ShowCargoTypeOrdersWindow(this->vehicle, this, sel_ord, CTOWV_UNLOAD);
		}
	}

	/**
	 * Handle the click on the nonstop button.
	 * @param non_stop what non-stop type to use; -1 to use the 'next' one, -2 to toggle the via state.
	 */
	void OrderClick_Nonstop(int non_stop)
	{
		if (!this->vehicle->IsGroundVehicle()) return;

		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr || order->GetNonStopType() == non_stop) return;

		/* Keypress if negative, so 'toggle' to the next */
		if (non_stop == -1) {
			non_stop = order->GetNonStopType() ^ ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
		} else if (non_stop == -2) {
			if (!order->IsType(OT_GOTO_STATION)) return;
			non_stop = order->GetNonStopType() ^ ONSF_NO_STOP_AT_DESTINATION_STATION;
		}

		this->SetWidgetDirty(WID_O_NON_STOP);
		this->ModifyOrder(sel_ord, MOF_NON_STOP | non_stop << 8);
	}

	/**
	 * Handle the click on the skip button.
	 * If ctrl is pressed, skip to selected order, else skip to current order + 1
	 */
	void OrderClick_Skip()
	{
		/* Don't skip when there's nothing to skip */
		if (_ctrl_pressed && this->vehicle->cur_implicit_order_index == this->OrderGetSel()) return;
		if (this->vehicle->GetNumOrders() <= 1) return;

		DoCommandP(this->vehicle->tile, this->vehicle->index, _ctrl_pressed ? this->OrderGetSel() : ((this->vehicle->cur_implicit_order_index + 1) % this->vehicle->GetNumOrders()),
				CMD_SKIP_TO_ORDER | CMD_MSG(_ctrl_pressed ? STR_ERROR_CAN_T_SKIP_TO_ORDER : STR_ERROR_CAN_T_SKIP_ORDER));
	}

	/**
	 * Handle the click on the delete button.
	 */
	void OrderClick_Delete()
	{
		/* When networking, move one order lower */
		int selected = this->selected_order + (int)_networking;

		if (DoCommandP(this->vehicle->tile, this->vehicle->index, this->OrderGetSel(), CMD_DELETE_ORDER | CMD_MSG(STR_ERROR_CAN_T_DELETE_THIS_ORDER))) {
			this->selected_order = selected >= this->vehicle->GetNumOrders() ? -1 : selected;
			this->UpdateButtonState();
		}
	}

	/**
	 * Handle the click on the 'stop sharing' button.
	 * If 'End of Shared Orders' isn't selected, do nothing. If Ctrl is pressed, call OrderClick_Delete and exit.
	 * To stop sharing this vehicle order list, we copy the orders of a vehicle that share this order list. That way we
	 * exit the group of shared vehicles while keeping the same order list.
	 */
	void OrderClick_StopSharing()
	{
		/* Don't try to stop sharing orders if 'End of Shared Orders' isn't selected. */
		if (!this->vehicle->IsOrderListShared() || this->selected_order != this->vehicle->GetNumOrders()) return;
		/* If Ctrl is pressed, delete the order list as if we clicked the 'Delete' button. */
		if (_ctrl_pressed) {
			this->OrderClick_Delete();
			return;
		}

		/* Get another vehicle that share orders with this vehicle. */
		Vehicle *other_shared = (this->vehicle->FirstShared() == this->vehicle) ? this->vehicle->NextShared() : this->vehicle->PreviousShared();
		/* Copy the order list of the other vehicle. */
		if (DoCommandP(this->vehicle->tile, this->vehicle->index | CO_COPY << 30, other_shared->index, CMD_CLONE_ORDER | CMD_MSG(STR_ERROR_CAN_T_STOP_SHARING_ORDER_LIST))) {
			this->UpdateButtonState();
		}
	}

	/**
	 * Handle the click on the refit button.
	 * If ctrl is pressed, cancel refitting, else show the refit window.
	 * @param i Selected refit command.
	 * @param auto_refit Select refit for auto-refitting.
	 */
	void OrderClick_Refit(int i, bool auto_refit)
	{
		if (_ctrl_pressed) {
			/* Cancel refitting */
			DoCommandP(this->vehicle->tile, this->vehicle->index, (this->OrderGetSel() << 16) | (CT_NO_REFIT << 8) | CT_NO_REFIT, CMD_ORDER_REFIT);
		} else {
			if (i == 1) { // Auto-refit to available cargo type.
				DoCommandP(this->vehicle->tile, this->vehicle->index, (this->OrderGetSel() << 16) | CT_AUTO_REFIT, CMD_ORDER_REFIT);
			} else {
				ShowVehicleRefitWindow(this->vehicle, this->OrderGetSel(), this, auto_refit);
			}
		}
	}

	/**
	 * Handle the click on the reverse order list button.
	 */
	void OrderClick_ReverseOrderList(uint subcommand)
	{
		DoCommandP(this->vehicle->tile, this->vehicle->index, subcommand, CMD_REVERSE_ORDER_LIST | CMD_MSG(STR_ERROR_CAN_T_MOVE_THIS_ORDER));
	}

	/** Cache auto-refittability of the vehicle chain. */
	void UpdateAutoRefitState()
	{
		this->can_do_refit = false;
		this->can_do_autorefit = false;
		for (const Vehicle *w = this->vehicle; w != nullptr; w = w->IsArticulatedCallbackVehicleType() ? w->Next() : nullptr) {
			if (IsEngineRefittable(w->engine_type)) this->can_do_refit = true;
			if (HasBit(Engine::Get(w->engine_type)->info.misc_flags, EF_AUTO_REFIT)) this->can_do_autorefit = true;
		}
	}

	int GetOrderManagementPlane() const
	{
		return this->selected_order == this->vehicle->GetNumOrders() ? DP_MGMT_LIST_BTN : DP_MGMT_BTN;
	}

public:
	OrdersWindow(WindowDesc *desc, const Vehicle *v) : GeneralVehicleWindow(desc, v)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_O_SCROLLBAR);
		this->GetWidget<NWidgetStacked>(WID_O_SEL_OCCUPANCY)->SetDisplayedPlane(_settings_client.gui.show_order_occupancy_by_default ? 0 : SZSP_NONE);
		this->SetWidgetLoweredState(WID_O_OCCUPANCY_TOGGLE, _settings_client.gui.show_order_occupancy_by_default);
		this->current_aux_plane = SZSP_NONE;
		this->current_aux2_plane = SZSP_NONE;
		this->current_aux3_plane = SZSP_NONE;
		this->current_mgmt_plane = this->GetOrderManagementPlane();
		if (v->owner == _local_company) {
			NWidgetStacked *aux_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX);
			NWidgetStacked *aux2_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX2);
			NWidgetStacked *aux3_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX3);
			aux_sel->independent_planes = true;
			aux2_sel->independent_planes = true;
			aux3_sel->independent_planes = true;
			aux_sel->SetDisplayedPlane(this->current_aux_plane);
			aux2_sel->SetDisplayedPlane(this->current_aux2_plane);
			aux3_sel->SetDisplayedPlane(this->current_aux3_plane);
			this->GetWidget<NWidgetStacked>(WID_O_SEL_MGMT)->SetDisplayedPlane(this->current_mgmt_plane);
		}
		this->FinishInitNested(v->index);
		if (v->owner == _local_company) {
			this->DisableWidget(WID_O_EMPTY);
		}

		this->selected_order = -1;
		this->order_over = INVALID_VEH_ORDER_ID;
		this->goto_type = OPOS_NONE;
		this->owner = v->owner;

		this->UpdateAutoRefitState();

		if (_settings_client.gui.quick_goto && v->owner == _local_company) {
			/* If there are less than 2 station, make Go To active. */
			int station_orders = 0;
			for(const Order *order : v->Orders()) {
				if (order->IsType(OT_GOTO_STATION)) station_orders++;
			}

			if (station_orders < 2) this->OrderClick_Goto(OPOS_GOTO);
		}
		this->OnInvalidateData(VIWD_MODIFY_ORDERS);
	}

	void Close() override
	{
		CloseWindowById(WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, this->window_number, false);
		CloseWindowById(WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, this->window_number, false);
		if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
		this->GeneralVehicleWindow::Close();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_O_OCCUPANCY_LIST:
				SetDParamMaxValue(0, 100);
				size->width = GetStringBoundingBox(STR_ORDERS_OCCUPANCY_PERCENT).width + 10 + WidgetDimensions::unscaled.framerect.Horizontal();
				/* FALL THROUGH */

			case WID_O_SEL_OCCUPANCY:
			case WID_O_ORDER_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + padding.height;
				break;

			case WID_O_COND_VARIABLE: {
				Dimension d = {0, 0};
				for (uint i = 0; i < lengthof(_order_conditional_variable); i++) {
					if (this->vehicle->type != VEH_TRAIN && _order_conditional_variable[i] == OCV_FREE_PLATFORMS) {
						continue;
					}
					d = maxdim(d, GetStringBoundingBox(OrderStringForVariable(this->vehicle, _order_conditional_variable[i])));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_O_COND_COMPARATOR: {
				Dimension d = {0, 0};
				for (int i = 0; _order_conditional_condition[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(_order_conditional_condition[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_O_OCCUPANCY_TOGGLE:
				SetDParamMaxValue(0, 100);
				size->width = GetStringBoundingBox(STR_ORDERS_OCCUPANCY_PERCENT).width + 10 + WidgetDimensions::unscaled.framerect.Horizontal();
				break;

			case WID_O_TIMETABLE_VIEW: {
				Dimension d = GetStringBoundingBox(STR_ORDERS_TIMETABLE_VIEW);
				Dimension spr_d = GetSpriteSize(SPR_WARNING_SIGN);
				d.width += spr_d.width + WidgetDimensions::scaled.hsep_normal;
				d.height = std::max(d.height, spr_d.height);
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_O_SHARED_ORDER_LIST:
			case WID_O_ADD_VEH_GROUP:
				size->width = std::max(size->width, NWidgetLeaf::GetResizeBoxDimension().width);
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		VehicleOrderID from = INVALID_VEH_ORDER_ID;
		VehicleOrderID to   = INVALID_VEH_ORDER_ID;

		switch (data) {
			case VIWD_AUTOREPLACE:
				/* Autoreplace replaced the vehicle */
				this->vehicle = Vehicle::Get(this->window_number);
				FALLTHROUGH;

			case VIWD_CONSIST_CHANGED:
				/* Vehicle composition was changed. */
				this->UpdateAutoRefitState();
				break;

			case VIWD_REMOVE_ALL_ORDERS:
				/* Removed / replaced all orders (after deleting / sharing) */
				if (this->selected_order == -1) break;

				this->CloseChildWindows();
				HideDropDownMenu(this);
				this->selected_order = -1;
				break;

			case VIWD_MODIFY_ORDERS:
				/* Some other order changes */
				break;

			default:
				if (gui_scope) break; // only do this once; from command scope
				from = GB(data, 0, 16);
				to   = GB(data, 16, 16);
				/* Moving an order. If one of these is INVALID_VEH_ORDER_ID, then
				 * the order is being created / removed */
				if (this->selected_order == -1) break;

				if (from == to) break; // no need to change anything

				if (from != this->selected_order) {
					/* Moving from preceding order? */
					this->selected_order -= (int)(from <= this->selected_order);
					/* Moving to   preceding order? */
					this->selected_order += (int)(to   <= this->selected_order);
					break;
				}

				/* Now we are modifying the selected order */
				if (to == INVALID_VEH_ORDER_ID) {
					/* Deleting selected order */
					this->CloseChildWindows();
					HideDropDownMenu(this);
					this->selected_order = -1;
					break;
				}

				/* Moving selected order */
				this->selected_order = to;
				break;
		}

		this->vscroll->SetCount(this->vehicle->GetNumOrders() + 1);
		if (gui_scope) {
			this->UpdateButtonState();
			InvalidateWindowClassesData(WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, 0);
			InvalidateWindowClassesData(WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, 0);
		}

		/* Scroll to the new order. */
		if (from == INVALID_VEH_ORDER_ID && to != INVALID_VEH_ORDER_ID && !this->vscroll->IsVisible(to)) {
			this->vscroll->ScrollTowards(to);
		}
	}

	virtual EventState OnCTRLStateChange() override
	{
		this->UpdateButtonState();
		return ES_NOT_HANDLED;
	}

	void UpdateButtonState()
	{
		if (this->vehicle->owner != _local_company) {
			this->GetWidget<NWidgetStacked>(WID_O_SEL_OCCUPANCY)->SetDisplayedPlane(IsWidgetLowered(WID_O_OCCUPANCY_TOGGLE) ? 0 : SZSP_NONE);
			return; // No buttons are displayed with competitor order windows.
		}

		bool shared_orders = this->vehicle->IsOrderListShared();
		VehicleOrderID sel = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel);

		/* Second row. */
		/* skip */
		this->SetWidgetDisabledState(WID_O_SKIP, this->vehicle->GetNumOrders() <= 1);

		/* delete / stop sharing */
		NWidgetStacked *delete_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_BOTTOM_MIDDLE);
		if (shared_orders && this->selected_order == this->vehicle->GetNumOrders()) {
			/* The 'End of Shared Orders' order is selected, show the 'stop sharing' button. */
			delete_sel->SetDisplayedPlane(DP_BOTTOM_MIDDLE_STOP_SHARING);
		} else {
			/* The 'End of Shared Orders' order isn't selected, show the 'delete' button. */
			delete_sel->SetDisplayedPlane(DP_BOTTOM_MIDDLE_DELETE);
			this->SetWidgetDisabledState(WID_O_DELETE,
				(uint)this->vehicle->GetNumOrders() + ((shared_orders || this->vehicle->GetNumOrders() != 0) ? 1 : 0) <= (uint)this->selected_order);

			/* Set the tooltip of the 'delete' button depending on whether the
			 * 'End of Orders' order or a regular order is selected. */
			NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_O_DELETE);
			if (this->selected_order == this->vehicle->GetNumOrders()) {
				nwi->SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_ALL_TOOLTIP);
			} else {
				nwi->SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_TOOLTIP);
			}
		}

		/* First row. */
		this->RaiseWidget(WID_O_FULL_LOAD);
		this->RaiseWidget(WID_O_UNLOAD);
		this->RaiseWidget(WID_O_SERVICE);

		/* Selection widgets. */
		/* Train or road vehicle. */
		NWidgetStacked *train_row_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_TOP_ROW_GROUNDVEHICLE);
		NWidgetStacked *left_sel      = this->GetWidget<NWidgetStacked>(WID_O_SEL_TOP_LEFT);
		NWidgetStacked *middle_sel    = this->GetWidget<NWidgetStacked>(WID_O_SEL_TOP_MIDDLE);
		NWidgetStacked *right_sel     = this->GetWidget<NWidgetStacked>(WID_O_SEL_TOP_RIGHT);
		/* Ship or airplane. */
		NWidgetStacked *row_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_TOP_ROW);
		assert(row_sel != nullptr || (train_row_sel != nullptr && left_sel != nullptr && middle_sel != nullptr && right_sel != nullptr));

		NWidgetStacked *aux_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX);
		NWidgetStacked *aux2_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX2);
		NWidgetStacked *aux3_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX3);
		NWidgetStacked *mgmt_sel = this->GetWidget<NWidgetStacked>(WID_O_SEL_MGMT);
		mgmt_sel->SetDisplayedPlane(this->GetOrderManagementPlane());

		auto aux_plane_guard = scope_guard([&]() {
			bool reinit = false;
			if (this->current_aux_plane != aux_sel->shown_plane) {
				this->current_aux_plane = aux_sel->shown_plane;
				reinit = true;
			}
			if (this->current_aux2_plane != aux2_sel->shown_plane) {
				this->current_aux2_plane = aux2_sel->shown_plane;
				reinit = true;
			}
			if (this->current_aux3_plane != aux3_sel->shown_plane) {
				this->current_aux3_plane = aux3_sel->shown_plane;
				reinit = true;
			}
			if ((this->current_mgmt_plane == SZSP_NONE) != (mgmt_sel->shown_plane == SZSP_NONE)) {
				this->current_mgmt_plane = mgmt_sel->shown_plane;
				reinit = true;
			} else if (this->current_mgmt_plane != mgmt_sel->shown_plane) {
				this->current_mgmt_plane = mgmt_sel->shown_plane;
			}
			if (reinit) this->ReInit();
		});

		aux_sel->SetDisplayedPlane(SZSP_NONE);
		aux2_sel->SetDisplayedPlane(SZSP_NONE);
		aux3_sel->SetDisplayedPlane(SZSP_NONE);

		if (order == nullptr) {
			if (row_sel != nullptr) {
				row_sel->SetDisplayedPlane(DP_ROW_LOAD);
			} else {
				train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_NORMAL);
				left_sel->SetDisplayedPlane(DP_LEFT_LOAD);
				middle_sel->SetDisplayedPlane(DP_MIDDLE_UNLOAD);
				right_sel->SetDisplayedPlane(DP_RIGHT_EMPTY);
				this->DisableWidget(WID_O_NON_STOP);
				this->RaiseWidget(WID_O_NON_STOP);
			}
			this->DisableWidget(WID_O_FULL_LOAD);
			this->DisableWidget(WID_O_UNLOAD);
			this->DisableWidget(WID_O_REFIT_DROPDOWN);
			this->DisableWidget(WID_O_MGMT_BTN);
		} else {
			this->SetWidgetDisabledState(WID_O_FULL_LOAD, (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // full load
			this->SetWidgetDisabledState(WID_O_UNLOAD,    (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // unload
			this->EnableWidget(WID_O_MGMT_BTN);

			switch (order->GetType()) {
				case OT_GOTO_STATION:
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_LOAD);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_NORMAL);
						left_sel->SetDisplayedPlane(DP_LEFT_LOAD);
						middle_sel->SetDisplayedPlane(DP_MIDDLE_UNLOAD);
						right_sel->SetDisplayedPlane(DP_RIGHT_REFIT);
						this->EnableWidget(WID_O_NON_STOP);
						this->SetWidgetLoweredState(WID_O_NON_STOP, order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
					}
					this->SetWidgetLoweredState(WID_O_FULL_LOAD, order->GetLoadType() == OLF_FULL_LOAD_ANY);
					this->SetWidgetLoweredState(WID_O_UNLOAD, order->GetUnloadType() == OUFB_UNLOAD);

					/* Can only do refitting when stopping at the destination and loading cargo.
					 * Also enable the button if a refit is already set to allow clearing it. */
					this->SetWidgetDisabledState(WID_O_REFIT_DROPDOWN,
							order->GetLoadType() == OLFB_NO_LOAD || (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) ||
							((!this->can_do_refit || !this->can_do_autorefit) && !order->IsRefit()));

					break;

				case OT_GOTO_WAYPOINT:
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_LOAD);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_NORMAL);
						left_sel->SetDisplayedPlane(DP_LEFT_REVERSE);
						middle_sel->SetDisplayedPlane(DP_MIDDLE_UNLOAD);
						right_sel->SetDisplayedPlane(DP_RIGHT_EMPTY);
						this->EnableWidget(WID_O_NON_STOP);
						this->SetWidgetLoweredState(WID_O_NON_STOP, order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
						this->EnableWidget(WID_O_REVERSE);
						this->SetWidgetLoweredState(WID_O_REVERSE, order->GetWaypointFlags() & OWF_REVERSE);
					}
					this->DisableWidget(WID_O_UNLOAD);
					this->DisableWidget(WID_O_REFIT_DROPDOWN);
					break;

				case OT_GOTO_DEPOT:
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_DEPOT);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_NORMAL);
						left_sel->SetDisplayedPlane(DP_LEFT_REFIT);
						middle_sel->SetDisplayedPlane(DP_MIDDLE_SERVICE);
						right_sel->SetDisplayedPlane(DP_RIGHT_EMPTY);
						this->EnableWidget(WID_O_NON_STOP);
						this->SetWidgetLoweredState(WID_O_NON_STOP, order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS);
					}
					/* Disable refit button if the order is no 'always go' order.
					 * However, keep the service button enabled for refit-orders to allow clearing refits (without knowing about ctrl). */
					this->SetWidgetDisabledState(WID_O_REFIT,
							(order->GetDepotOrderType() & ODTFB_SERVICE) || (order->GetDepotActionType() & ODATFB_HALT) ||
							(!this->can_do_refit && !order->IsRefit()));
					this->SetWidgetLoweredState(WID_O_SERVICE, order->GetDepotOrderType() & ODTFB_SERVICE);
					break;

				case OT_CONDITIONAL: {
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_CONDITIONAL);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_CONDITIONAL);
					}

					OrderConditionVariable ocv = (order == nullptr) ? OCV_LOAD_PERCENTAGE : order->GetConditionVariable();
					bool is_cargo = (ocv == OCV_CARGO_ACCEPTANCE || ocv == OCV_CARGO_WAITING);
					bool is_slot_occupancy = (ocv == OCV_SLOT_OCCUPANCY || ocv == OCV_VEH_IN_SLOT);
					bool is_auxiliary_cargo = (ocv == OCV_CARGO_LOAD_PERCENTAGE || ocv == OCV_CARGO_WAITING_AMOUNT);
					bool is_counter = (ocv == OCV_COUNTER_VALUE);
					bool is_time_date = (ocv == OCV_TIME_DATE);
					bool is_timetable = (ocv == OCV_TIMETABLE);
					bool is_sched_dispatch = (ocv == OCV_DISPATCH_SLOT);

					if (is_cargo) {
						if (order == nullptr || !CargoSpec::Get(order->GetConditionValue())->IsValid()) {
							this->GetWidget<NWidgetCore>(WID_O_COND_CARGO)->widget_data = STR_NEWGRF_INVALID_CARGO;
						} else {
							this->GetWidget<NWidgetCore>(WID_O_COND_CARGO)->widget_data = CargoSpec::Get(order->GetConditionValue())->name;
						}
						this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_VALUE)->SetDisplayedPlane(DP_COND_VALUE_CARGO);
					} else if (is_slot_occupancy) {
						TraceRestrictSlotID slot_id = (order != nullptr && TraceRestrictSlot::IsValidID(order->GetXData()) ? order->GetXData() : INVALID_TRACE_RESTRICT_SLOT_ID);

						this->GetWidget<NWidgetCore>(WID_O_COND_SLOT)->widget_data = (slot_id != INVALID_TRACE_RESTRICT_SLOT_ID) ? STR_TRACE_RESTRICT_SLOT_NAME : STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
						this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_VALUE)->SetDisplayedPlane(DP_COND_VALUE_SLOT);
					} else if (is_sched_dispatch) {
						this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_VALUE)->SetDisplayedPlane(SZSP_NONE);
					} else {
						this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_VALUE)->SetDisplayedPlane(DP_COND_VALUE_NUMBER);
					}

					if (is_auxiliary_cargo) {
						if (order == nullptr || !CargoSpec::Get(order->GetConditionValue())->IsValid()) {
							this->GetWidget<NWidgetCore>(WID_O_COND_AUX_CARGO)->widget_data = STR_NEWGRF_INVALID_CARGO;
						} else {
							this->GetWidget<NWidgetCore>(WID_O_COND_AUX_CARGO)->widget_data = CargoSpec::Get(order->GetConditionValue())->name;
						}
						aux_sel->SetDisplayedPlane(DP_COND_AUX_CARGO);
					} else if (is_counter) {
						TraceRestrictCounterID ctr_id = (order != nullptr && TraceRestrictCounter::IsValidID(GB(order->GetXData(), 16, 16)) ? GB(order->GetXData(), 16, 16) : INVALID_TRACE_RESTRICT_COUNTER_ID);

						this->GetWidget<NWidgetCore>(WID_O_COND_COUNTER)->widget_data = (ctr_id != INVALID_TRACE_RESTRICT_COUNTER_ID) ? STR_TRACE_RESTRICT_COUNTER_NAME : STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
						aux_sel->SetDisplayedPlane(DP_COND_COUNTER);
					} else if (is_time_date) {
						this->GetWidget<NWidgetCore>(WID_O_COND_TIME_DATE)->widget_data = STR_TRACE_RESTRICT_TIME_MINUTE_ITEM + order->GetConditionValue();
						aux_sel->SetDisplayedPlane(DP_COND_TIME_DATE);
					} else if (is_timetable) {
						this->GetWidget<NWidgetCore>(WID_O_COND_TIMETABLE)->widget_data = STR_TRACE_RESTRICT_TIMETABLE_LATENESS + order->GetConditionValue();
						aux_sel->SetDisplayedPlane(DP_COND_TIMETABLE);
					} else if (is_sched_dispatch) {
						this->GetWidget<NWidgetCore>(WID_O_COND_SCHED_SELECT)->widget_data = STR_JUST_STRING1;
						aux_sel->SetDisplayedPlane(DP_COND_SCHED_SELECT);
					} else {
						aux_sel->SetDisplayedPlane(SZSP_NONE);
					}

					if (ocv == OCV_CARGO_WAITING_AMOUNT) {
						aux2_sel->SetDisplayedPlane(DP_COND_AUX2_VIA);
					} else if (is_sched_dispatch) {
						this->GetWidget<NWidgetCore>(WID_O_COND_SCHED_TEST)->widget_data = STR_TRACE_RESTRICT_DISPATCH_SLOT_SHORT_NEXT + (order->GetConditionValue() / 2);
						aux2_sel->SetDisplayedPlane(DP_COND_AUX2_SCHED_TEST);
					} else {
						aux2_sel->SetDisplayedPlane(SZSP_NONE);
					}

					if (ConditionVariableHasStationID(ocv)) {
						aux3_sel->SetDisplayedPlane(DP_COND_AUX3_STATION);
					} else {
						aux3_sel->SetDisplayedPlane(SZSP_NONE);
					}

					/* Set the strings for the dropdown boxes. */
					this->GetWidget<NWidgetCore>(WID_O_COND_VARIABLE)->widget_data   = OrderStringForVariable(this->vehicle, ocv);
					this->GetWidget<NWidgetCore>(WID_O_COND_COMPARATOR)->widget_data = GetComparatorStrings(this->vehicle, order)[order->GetConditionComparator()];
					this->GetWidget<NWidgetCore>(WID_O_COND_VALUE)->widget_data = (ocv == OCV_TIME_DATE && order->GetConditionValue() == TRTDVF_HOUR_MINUTE) ? STR_JUST_TIME_HHMM : STR_JUST_COMMA;
					this->SetWidgetDisabledState(WID_O_COND_COMPARATOR, ocv == OCV_UNCONDITIONALLY || ocv == OCV_PERCENT);
					this->SetWidgetDisabledState(WID_O_COND_VALUE, ocv == OCV_REQUIRES_SERVICE || ocv == OCV_UNCONDITIONALLY);
					break;
				}

				case OT_RELEASE_SLOT: {
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_SLOT);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_SLOT);
					}

					TraceRestrictSlotID slot_id = (order != nullptr && TraceRestrictSlot::IsValidID(order->GetDestination()) ? order->GetDestination() : INVALID_TRACE_RESTRICT_SLOT_ID);

					this->GetWidget<NWidgetCore>(WID_O_RELEASE_SLOT)->widget_data = (slot_id != INVALID_TRACE_RESTRICT_SLOT_ID) ? STR_TRACE_RESTRICT_SLOT_NAME : STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
					break;
				}

				case OT_COUNTER: {
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_COUNTER);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_COUNTER);
					}

					TraceRestrictCounterID ctr_id = (order != nullptr && TraceRestrictCounter::IsValidID(order->GetDestination()) ? order->GetDestination() : INVALID_TRACE_RESTRICT_COUNTER_ID);

					this->GetWidget<NWidgetCore>(WID_O_CHANGE_COUNTER)->widget_data = (ctr_id != INVALID_TRACE_RESTRICT_COUNTER_ID) ? STR_TRACE_RESTRICT_COUNTER_NAME : STR_TRACE_RESTRICT_VARIABLE_UNDEFINED;
					break;
				}

				case OT_LABEL: {
					std::pair<int, int> sections = { DP_ROW_EMPTY, DP_GROUNDVEHICLE_ROW_EMPTY };
					if (order->GetLabelSubType() == OLST_TEXT) {
						sections = { DP_ROW_TEXT_LABEL, DP_GROUNDVEHICLE_ROW_TEXT_LABEL };
					} else if (IsDeparturesOrderLabelSubType(order->GetLabelSubType())) {
						sections = { DP_ROW_DEPARTURES, DP_GROUNDVEHICLE_ROW_DEPARTURES };
					}
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(sections.first);
					} else {
						train_row_sel->SetDisplayedPlane(sections.second);
					}
					break;
				}

				default: // every other order
					if (row_sel != nullptr) {
						row_sel->SetDisplayedPlane(DP_ROW_LOAD);
					} else {
						train_row_sel->SetDisplayedPlane(DP_GROUNDVEHICLE_ROW_NORMAL);
						left_sel->SetDisplayedPlane(DP_LEFT_LOAD);
						middle_sel->SetDisplayedPlane(DP_MIDDLE_UNLOAD);
						right_sel->SetDisplayedPlane(DP_RIGHT_EMPTY);
						this->DisableWidget(WID_O_NON_STOP);
					}
					this->DisableWidget(WID_O_FULL_LOAD);
					this->DisableWidget(WID_O_UNLOAD);
					this->DisableWidget(WID_O_REFIT_DROPDOWN);
					break;
			}
		}

		this->GetWidget<NWidgetStacked>(WID_O_SEL_SHARED)->SetDisplayedPlane(_ctrl_pressed ? DP_SHARED_VEH_GROUP : DP_SHARED_LIST);

		/* Disable list of vehicles with the same shared orders if there is no list */
		this->SetWidgetDisabledState(WID_O_SHARED_ORDER_LIST, !(shared_orders || _settings_client.gui.enable_single_veh_shared_order_gui));

		this->GetWidget<NWidgetStacked>(WID_O_SEL_OCCUPANCY)->SetDisplayedPlane(IsWidgetLowered(WID_O_OCCUPANCY_TOGGLE) ? 0 : SZSP_NONE);

		this->SetDirty();
	}

	void OnPaint() override
	{
		if (this->vehicle->owner != _local_company) {
			this->selected_order = -1; // Disable selection any selected row at a competitor order window.
		} else {
			this->SetWidgetLoweredState(WID_O_GOTO, this->goto_type != OPOS_NONE && this->goto_type != OPOS_COND_VIA
					&& this->goto_type != OPOS_COND_STATION && this->goto_type != OPOS_CONDITIONAL_RETARGET);
			this->SetWidgetLoweredState(WID_O_COND_AUX_VIA, this->goto_type == OPOS_COND_VIA);
			this->SetWidgetLoweredState(WID_O_COND_AUX_STATION, this->goto_type == OPOS_COND_STATION);
			this->SetWidgetLoweredState(WID_O_MGMT_BTN, this->goto_type == OPOS_CONDITIONAL_RETARGET);
		}
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_O_ORDER_LIST:
				DrawOrderListWidget(r);
				break;

			case WID_O_OCCUPANCY_LIST:
				DrawOccupancyListWidget(r);
				break;

			case WID_O_TIMETABLE_VIEW:
				DrawTimetableButtonWidget(r);
				break;
		}
	}

	void DrawOrderListWidget(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.frametext, WidgetDimensions::scaled.framerect);
		bool rtl = _current_text_dir == TD_RTL;
		SetDParamMaxValue(0, this->vehicle->GetNumOrders(), 2);
		int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + WidgetDimensions::scaled.hsep_normal;
		int middle = rtl ? ir.right - index_column_width : ir.left + index_column_width;

		int y = ir.top;
		int line_height = this->GetWidget<NWidgetBase>(WID_O_ORDER_LIST)->resize_y;

		int i = this->vscroll->GetPosition();
		const Order *order = this->vehicle->GetOrder(i);
		/* First draw the highlighting underground if it exists. */
		if (this->order_over != INVALID_VEH_ORDER_ID) {
			while (order != nullptr) {
				/* Don't draw anything if it extends past the end of the window. */
				if (!this->vscroll->IsVisible(i)) break;

				if (i != this->selected_order && i == this->order_over) {
					/* Highlight dragged order destination. */
					int top = (this->order_over < this->selected_order ? y : y + line_height) - WidgetDimensions::scaled.framerect.top;
					int bottom = std::min(top + 2, ir.bottom);
					top = std::max(top - 3, ir.top);
					GfxFillRect(ir.left, top, ir.right, bottom, _colour_gradient[COLOUR_GREY][7]);
					break;
				}
				y += line_height;

				i++;
				order = order->next;
			}

			/* Reset counters for drawing the orders. */
			y = ir.top;
			i = this->vscroll->GetPosition();
			order = this->vehicle->GetOrder(i);
		}

		/* Draw the orders. */
		while (order != nullptr) {
			/* Don't draw anything if it extends past the end of the window. */
			if (!this->vscroll->IsVisible(i)) break;

			DrawOrderString(this->vehicle, order, i, y, i == this->selected_order, false, ir.left, middle, ir.right);
			y += line_height;

			i++;
			order = order->next;
		}

		if (this->vscroll->IsVisible(i)) {
			StringID str = this->vehicle->IsOrderListShared() ? STR_ORDERS_END_OF_SHARED_ORDERS : STR_ORDERS_END_OF_ORDERS;
			DrawString(rtl ? ir.left : middle, rtl ? middle : ir.right, y, str, (i == this->selected_order) ? TC_WHITE : TC_BLACK);
		}
	}

	void DrawOccupancyListWidget(const Rect &r) const
	{
		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int y = ir.top;
		int line_height = this->GetWidget<NWidgetBase>(WID_O_ORDER_LIST)->resize_y;

		int i = this->vscroll->GetPosition();
		const Order *order = this->vehicle->GetOrder(i);
		/* Draw the orders. */
		while (order != nullptr) {
			/* Don't draw anything if it extends past the end of the window. */
			if (!this->vscroll->IsVisible(i)) break;

			uint8 occupancy = order->GetOccupancy();
			if (occupancy > 0) {
				SetDParam(0, occupancy - 1);
				TextColour colour;
				if (order->UseOccupancyValueForAverage()) {
					colour = (i == this->selected_order) ? TC_WHITE : TC_BLACK;
				} else {
					colour = ((i == this->selected_order) ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
				}
				DrawString(ir.left, ir.right, y, STR_ORDERS_OCCUPANCY_PERCENT, colour);
			}
			y += line_height;

			i++;
			order = order->next;
		}
	}

	void DrawTimetableButtonWidget(const Rect &r) const
	{
		const bool rtl = _current_text_dir == TD_RTL;
		bool clicked = this->GetWidget<NWidgetCore>(WID_O_TIMETABLE_VIEW)->IsLowered();
		Dimension d = GetStringBoundingBox(STR_ORDERS_TIMETABLE_VIEW);

		int left = r.left + clicked;
		int right = r.right + clicked;

		extern void ProcessTimetableWarnings(const Vehicle *v, std::function<void(StringID, bool)> handler);

		bool show_warning = false;
		ProcessTimetableWarnings(this->vehicle, [&](StringID text, bool warning) {
			if (warning) show_warning = true;
		});

		if (show_warning) {
			const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
			int spr_offset = std::max(0, ((int)(r.bottom - r.top + 1) - (int)warning_dimensions.height) / 2); // Offset for rendering the sprite vertically centered
			DrawSprite(SPR_WARNING_SIGN, 0, rtl ? right - warning_dimensions.width - 2 : left + 2, r.top + spr_offset);
			if (rtl) {
				right -= warning_dimensions.width;
			} else {
				left += warning_dimensions.width;
			}
		}
		int offset = std::max(0, ((int)(r.bottom - r.top + 1) - (int)d.height) / 2); // Offset for rendering the text vertically centered
		DrawString(left, right, r.top + offset + clicked, STR_ORDERS_TIMETABLE_VIEW, TC_FROMSTRING, SA_HOR_CENTER);
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_O_COND_VALUE: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_CONDITIONAL)) {
					uint value;
					switch (order->GetConditionVariable()) {
						case OCV_CARGO_LOAD_PERCENTAGE:
						case OCV_TIME_DATE:
							value = order->GetXData();
							break;

						case OCV_TIMETABLE:
							value = order->GetXData();
							if (!_settings_client.gui.timetable_in_ticks) value /= DATE_UNIT_SIZE;
							break;

						case OCV_CARGO_WAITING_AMOUNT:
						case OCV_COUNTER_VALUE:
							value = GB(order->GetXData(), 0, 16);
							break;

						default:
							value = order->GetConditionValue();
							break;
					}
					if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value, this->vehicle->type);
					if (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) value = ConvertCargoQuantityToDisplayQuantity(order->GetConditionValue(), value);
					SetDParam(0, value);
				}
				break;
			}

			case WID_O_COND_SLOT: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_CONDITIONAL)) {
					TraceRestrictSlotID value = order->GetXData();
					SetDParam(0, value);
				}
				break;
			}

			case WID_O_COND_COUNTER: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_CONDITIONAL)) {
					TraceRestrictCounterID value = GB(order->GetXData(), 16, 16);
					SetDParam(0, value);
				}
				break;
			}

			case WID_O_COND_SCHED_SELECT: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				uint schedule_index = GB(order->GetXData(), 0, 16);
				if (order != nullptr && order->IsType(OT_CONDITIONAL) && order->GetConditionVariable() == OCV_DISPATCH_SLOT && schedule_index != UINT16_MAX) {
					if (schedule_index < this->vehicle->orders->GetScheduledDispatchScheduleCount()) {
						const DispatchSchedule &ds = this->vehicle->orders->GetDispatchScheduleByIndex(schedule_index);
						if (!ds.ScheduleName().empty()) {
							SetDParam(0, STR_JUST_RAW_STRING);
							SetDParamStr(1, ds.ScheduleName().c_str());
							break;
						}
					}
					SetDParam(0, STR_TIMETABLE_ASSIGN_SCHEDULE_ID);
					SetDParam(1, schedule_index + 1);
				} else {
					SetDParam(0, STR_TIMETABLE_ASSIGN_SCHEDULE_NONE);
				}
				break;
			}

			case WID_O_CAPTION:
				SetDParam(0, this->vehicle->index);
				break;

			case WID_O_OCCUPANCY_TOGGLE:
				const_cast<Vehicle *>(this->vehicle)->RecalculateOrderOccupancyAverage();
				if (this->vehicle->order_occupancy_average >= 16) {
					SetDParam(0, STR_JUST_INT);
					SetDParam(1, this->vehicle->order_occupancy_average - 16);
				} else {
					SetDParam(0, STR_EMPTY);
					SetDParam(1, 0);
				}
				break;

			case WID_O_RELEASE_SLOT: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_RELEASE_SLOT)) {
					TraceRestrictSlotID value = order->GetDestination();
					SetDParam(0, value);
				}
				break;
			}

			case WID_O_COUNTER_OP: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_COUNTER)) {
					SetDParam(0, STR_TRACE_RESTRICT_COUNTER_INCREASE + order->GetCounterOperation());
				} else {
					SetDParam(0, STR_EMPTY);
				}
				break;
			}

			case WID_O_CHANGE_COUNTER: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_COUNTER)) {
					TraceRestrictCounterID value = order->GetDestination();
					SetDParam(0, value);
				}
				break;
			}

			case WID_O_COUNTER_VALUE: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_COUNTER)) {
					SetDParam(0, order->GetXData());
				}
				break;
			}

			case WID_O_DEPARTURE_VIA_TYPE: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);

				if (order != nullptr && order->IsType(OT_LABEL) && IsDeparturesOrderLabelSubType(order->GetLabelSubType())) {
					switch (order->GetLabelSubType()) {
						case OLST_DEPARTURES_VIA:
							SetDParam(0, STR_ORDER_LABEL_DEPARTURES_SHOW_AS_VIA);
							break;

						case OLST_DEPARTURES_REMOVE_VIA:
							SetDParam(0, STR_ORDER_LABEL_DEPARTURES_REMOVE_VIA_SHORT);
							break;

						default:
							SetDParam(0, STR_EMPTY);
							break;
					}
				} else {
					SetDParam(0, STR_EMPTY);
				}
				break;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_O_ORDER_LIST: {
				if (this->goto_type == OPOS_CONDITIONAL) {
					VehicleOrderID order_id = this->GetOrderFromPt(_cursor.pos.y - this->top);
					if (order_id != INVALID_VEH_ORDER_ID) {
						Order order;
						order.next = nullptr;
						order.index = 0;
						order.MakeConditional(order_id);

						this->InsertNewOrder(order.Pack());
					}
					ResetObjectToPlace();
					break;
				}
				if (this->goto_type == OPOS_CONDITIONAL_RETARGET) {
					VehicleOrderID order_id = this->GetOrderFromPt(_cursor.pos.y - this->top);
					if (order_id != INVALID_VEH_ORDER_ID) {
						this->ModifyOrder(this->OrderGetSel(), MOF_COND_DESTINATION | (order_id << 8));
					}
					ResetObjectToPlace();
					break;
				}

				VehicleOrderID sel = this->GetOrderFromPt(pt.y);

				if (_ctrl_pressed && sel < this->vehicle->GetNumOrders()) {
					TileIndex xy = this->vehicle->GetOrder(sel)->GetLocation(this->vehicle);
					if (xy != INVALID_TILE) ScrollMainWindowToTile(xy);
					return;
				}

				/* This order won't be selected any more, close all child windows and dropdowns */
				this->CloseChildWindows();
				HideDropDownMenu(this);

				if (sel == INVALID_VEH_ORDER_ID || this->vehicle->owner != _local_company) {
					/* Deselect clicked order */
					this->selected_order = -1;
				} else if (sel == this->selected_order) {
					if (this->vehicle->type == VEH_TRAIN && sel < this->vehicle->GetNumOrders()) {
						int osl = ((this->vehicle->GetOrder(sel)->GetStopLocation() + 1) % OSL_END);
						if (osl == OSL_PLATFORM_THROUGH && !_settings_client.gui.show_adv_load_mode_features) {
							osl = OSL_PLATFORM_NEAR_END;
						}
						if (osl == OSL_PLATFORM_THROUGH) {
							for (const Vehicle *u = this->vehicle; u != nullptr; u = u->Next()) {
								/* Passengers may not be through-loaded */
								if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CC_PASSENGERS)) {
									osl = OSL_PLATFORM_NEAR_END;
									break;
								}
							}
						}
						this->ModifyOrder(sel, MOF_STOP_LOCATION | osl << 8);
					}
					if (this->vehicle->type == VEH_ROAD && sel < this->vehicle->GetNumOrders() && _settings_game.pf.pathfinder_for_roadvehs == VPF_YAPF) {
						DiagDirection current = this->vehicle->GetOrder(sel)->GetRoadVehTravelDirection();
						if (_settings_client.gui.show_adv_load_mode_features || current != INVALID_DIAGDIR) {
							uint dir = (current + 1) & 0xFF;
							if (dir >= DIAGDIR_END) dir = INVALID_DIAGDIR;
							this->ModifyOrder(sel, MOF_RV_TRAVEL_DIR | dir << 8);
						}
					}
				} else {
					/* Select clicked order */
					this->selected_order = sel;

					if (this->vehicle->owner == _local_company) {
						/* Activate drag and drop */
						SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
					}
				}

				this->UpdateButtonState();
				break;
			}

			case WID_O_SKIP:
				this->OrderClick_Skip();
				break;

			case WID_O_MGMT_LIST_BTN: {
				uint disabled_mask = (this->vehicle->GetNumOrders() < 2 ? 1 : 0) | (this->vehicle->GetNumOrders() < 3 ? 2 : 0);
				uint order_count = this->vehicle->GetNumOrders();
				for (uint i = 0; i < order_count; i++) {
					if (this->vehicle->GetOrder(i)->IsType(OT_CONDITIONAL)) {
						disabled_mask |= 2;
						break;
					}
				}
				ShowDropDownMenu(this, _order_manage_list_dropdown, -1, widget, disabled_mask, 0, 0, DDSF_LOST_FOCUS);
				break;
			}

			case WID_O_MGMT_BTN: {
				VehicleOrderID sel = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel);
				if (order == nullptr) break;

				DropDownList list;
				list.emplace_back(new DropDownListStringItem(STR_ORDER_DUPLICATE_ORDER, 0, false));
				if (order->IsType(OT_CONDITIONAL)) list.emplace_back(new DropDownListStringItem(STR_ORDER_CHANGE_JUMP_TARGET, 1, false));
				if (!order->IsType(OT_IMPLICIT)) {
					list.emplace_back(new DropDownListItem(-1, false));
					list.emplace_back(new DropDownListStringItem(STR_COLOUR_DEFAULT, 0x100 + INVALID_COLOUR, false));
					auto add_colour = [&](Colours colour) {
						list.emplace_back(new DropDownListStringItem(STR_COLOUR_DARK_BLUE + colour, 0x100 + colour, false));
					};
					add_colour(COLOUR_YELLOW);
					add_colour(COLOUR_LIGHT_BLUE);
					add_colour(COLOUR_GREEN);
					add_colour(COLOUR_ORANGE);
					add_colour(COLOUR_PINK);
				}
				ShowDropDownList(this, std::move(list), 0x100 + order->GetColour(), widget, 0, false, DDSF_LOST_FOCUS);
				break;
			}

			case WID_O_DELETE:
				this->OrderClick_Delete();
				break;

			case WID_O_STOP_SHARING:
				this->OrderClick_StopSharing();
				break;

			case WID_O_NON_STOP:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_Nonstop(-1);
				} else {
					const Order *o = this->vehicle->GetOrder(this->OrderGetSel());
					ShowDropDownMenu(this, _order_non_stop_drowdown, o->GetNonStopType(), WID_O_NON_STOP, _settings_game.order.nonstop_only ? 5 : 0,
							o->IsType(OT_GOTO_STATION) ? 0 : (o->IsType(OT_GOTO_WAYPOINT) ? 3 : 12), 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_GOTO:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					if (this->goto_type != OPOS_NONE) {
						ResetObjectToPlace();
					} else {
						this->OrderClick_Goto(OPOS_GOTO);
					}
				} else {
					if (this->goto_type == OPOS_COND_VIA || this->goto_type == OPOS_COND_STATION) ResetObjectToPlace();
					int sel;
					switch (this->goto_type) {
						case OPOS_NONE:        sel = -1; break;
						case OPOS_GOTO:        sel =  0; break;
						case OPOS_CONDITIONAL: sel =  2; break;
						case OPOS_SHARE:       sel =  3; break;
						case OPOS_CONDITIONAL_RETARGET: sel = -1; break;
						case OPOS_DEPARTURE_VIA:        sel =  7; break;
						default: NOT_REACHED();
					}
					uint32 hidden_mask = 0;
					if (_settings_client.gui.show_adv_tracerestrict_features) {
						bool have_counters = false;
						for (const TraceRestrictCounter *ctr : TraceRestrictCounter::Iterate()) {
							if (ctr->owner == this->vehicle->owner) {
								have_counters = true;
								break;
							}
						}
						if (!have_counters) {
							// Owner has no counters, don't both showing the menu item
							hidden_mask |= 0x20;
						}
					} else {
						hidden_mask |= 0x30;
					}
					ShowDropDownMenu(this, this->vehicle->type == VEH_AIRCRAFT ? _order_goto_dropdown_aircraft : _order_goto_dropdown, sel, WID_O_GOTO,
							0, hidden_mask, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_FULL_LOAD:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_FullLoad(OLF_FULL_LOAD_ANY, true);
				} else {
					ShowDropDownMenu(this, _order_full_load_drowdown, this->vehicle->GetOrder(this->OrderGetSel())->GetLoadType(), WID_O_FULL_LOAD, 0, 0xE2 /* 1110 0010 */, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_UNLOAD:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_Unload(OUFB_UNLOAD, true);
				} else {
					ShowDropDownMenu(this, _order_unload_drowdown, this->vehicle->GetOrder(this->OrderGetSel())->GetUnloadType(), WID_O_UNLOAD, 0, 0xE8 /* 1110 1000 */, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_REFIT:
				this->OrderClick_Refit(0, false);
				break;

			case WID_O_SERVICE:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_Service(-1);
				} else {
					ShowDropDownMenu(this, _order_depot_action_dropdown, DepotActionStringIndex(this->vehicle->GetOrder(this->OrderGetSel())),
							WID_O_SERVICE, 0, _settings_client.gui.show_depot_sell_gui ? 0 : (1 << DA_SELL), 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_REFIT_DROPDOWN:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_Refit(0, true);
				} else {
					ShowDropDownMenu(this, _order_refit_action_dropdown, 0, WID_O_REFIT_DROPDOWN, 0, 0, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_COND_SLOT: {
				int selected;
				const Order *order = this->vehicle->GetOrder(this->OrderGetSel());
				TraceRestrictSlotID value = order->GetXData();
				DropDownList list = GetSlotDropDownList(this->vehicle->owner, value, selected, this->vehicle->type, order->GetConditionVariable() == OCV_SLOT_OCCUPANCY);
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_COND_SLOT, 0);
				break;
			}

			case WID_O_COND_COUNTER: {
				int selected;
				TraceRestrictCounterID value = GB(this->vehicle->GetOrder(this->OrderGetSel())->GetXData(), 16, 16);
				DropDownList list = GetCounterDropDownList(this->vehicle->owner, value, selected);
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_COND_COUNTER, 0);
				break;
			}

			case WID_O_COND_TIME_DATE: {
				ShowDropDownMenu(this, _order_time_date_dropdown, this->vehicle->GetOrder(this->OrderGetSel())->GetConditionValue(),
						WID_O_COND_TIME_DATE, _settings_game.game_time.time_in_minutes ? 0 : 7, 0);
				break;
			}

			case WID_O_COND_TIMETABLE: {
				ShowDropDownMenu(this, _order_timetable_dropdown, this->vehicle->GetOrder(this->OrderGetSel())->GetConditionValue(),
						WID_O_COND_TIMETABLE, 0, 0);
				break;
			}

			case WID_O_COND_SCHED_SELECT: {
				int selected = GB(this->vehicle->GetOrder(this->OrderGetSel())->GetXData(), 0, 16);
				if (selected == UINT16_MAX) selected = -1;

				uint count = this->vehicle->orders->GetScheduledDispatchScheduleCount();
				DropDownList list;
				for (uint i = 0; i < count; ++i) {
					const DispatchSchedule &ds = this->vehicle->orders->GetDispatchScheduleByIndex(i);
					if (ds.ScheduleName().empty()) {
						SetDParam(0, i + 1);
						list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_ASSIGN_SCHEDULE_ID, i, false));
					} else {
						list.emplace_back(new DropDownListStringItem(ds.ScheduleName(), i, false));
					}
				}
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_COND_SCHED_SELECT, 0);
				break;
			}

			case WID_O_COND_SCHED_TEST: {
				ShowDropDownMenu(this, _order_dispatch_slot_dropdown, this->vehicle->GetOrder(this->OrderGetSel())->GetConditionValue() / 2,
						WID_O_COND_SCHED_TEST, 0, 0);
				break;
			}

			case WID_O_REVERSE: {
				VehicleOrderID sel_ord = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel_ord);

				if (order == nullptr) break;

				this->ModifyOrder(sel_ord, MOF_WAYPOINT_FLAGS | (order->GetWaypointFlags() ^ OWF_REVERSE) << 8);
				break;
			}

			case WID_O_COND_CARGO:
			case WID_O_COND_AUX_CARGO: {
				uint value = this->vehicle->GetOrder(this->OrderGetSel())->GetConditionValue();
				DropDownList list;
				for (size_t i = 0; i < _sorted_standard_cargo_specs.size(); ++i) {
					const CargoSpec *cs = _sorted_cargo_specs[i];
					list.emplace_back(new DropDownListStringItem(cs->name, cs->Index(), false));
				}
				if (!list.empty()) ShowDropDownList(this, std::move(list), value, widget, 0);
				break;
			}

			case WID_O_COND_AUX_VIA: {
				if (this->goto_type != OPOS_NONE) {
					ResetObjectToPlace();
				} else if (GB(this->vehicle->GetOrder(this->OrderGetSel())->GetXData(), 16, 16) != 0) {
					this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_3 | NEW_STATION << 8);
				} else {
					this->OrderClick_Goto(OPOS_COND_VIA);
				}
				break;
			}

			case WID_O_COND_AUX_STATION: {
				if (this->goto_type != OPOS_NONE) {
					ResetObjectToPlace();
				} else {
					this->OrderClick_Goto(OPOS_COND_STATION);
				}
				break;
			}

			case WID_O_TIMETABLE_VIEW:
				ShowTimetableWindow(this->vehicle);
				break;

			case WID_O_COND_VARIABLE: {
				const OrderConditionVariable ocv = this->vehicle->GetOrder(this->OrderGetSel())->GetConditionVariable();
				DropDownList list;
				for (uint i = 0; i < lengthof(_order_conditional_variable); i++) {
					if (this->vehicle->type != VEH_TRAIN && _order_conditional_variable[i] == OCV_FREE_PLATFORMS) {
						continue;
					}
					if (ocv != _order_conditional_variable[i]) {
						if ((_order_conditional_variable[i] == OCV_VEH_IN_SLOT || _order_conditional_variable[i] == OCV_SLOT_OCCUPANCY ||
								_order_conditional_variable[i] == OCV_COUNTER_VALUE) && !_settings_client.gui.show_adv_tracerestrict_features) {
							continue;
						}
						if ((_order_conditional_variable[i] == OCV_DISPATCH_SLOT) && this->vehicle->orders->GetScheduledDispatchScheduleCount() == 0) {
							continue;
						}
					}
					list.emplace_back(new DropDownListStringItem(OrderStringForVariable(this->vehicle, _order_conditional_variable[i]), _order_conditional_variable[i], false));
				}
				ShowDropDownList(this, std::move(list), ocv, WID_O_COND_VARIABLE);
				break;
			}

			case WID_O_COND_COMPARATOR: {
				const Order *o = this->vehicle->GetOrder(this->OrderGetSel());
				if (o->GetConditionVariable() == OCV_DISPATCH_SLOT) {
					DropDownList list;
					list.emplace_back(new DropDownListStringItem(STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_FIRST, 0x100, false));
					list.emplace_back(new DropDownListStringItem(STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_NOT_FIRST, 0x101, false));
					list.emplace_back(new DropDownListStringItem(STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_LAST, 0x102, false));
					list.emplace_back(new DropDownListStringItem(STR_ORDER_CONDITIONAL_COMPARATOR_DISPATCH_SLOT_IS_NOT_LAST, 0x103, false));
					int selected = 0x100 + ((o->GetConditionValue() % 2) * 2) + ((o->GetConditionComparator() == OCC_IS_FALSE) ? 1 : 0);
					ShowDropDownList(this, std::move(list), selected, WID_O_COND_COMPARATOR, 0);
					break;
				}
				uint mask;
				switch (o->GetConditionVariable()) {
					case OCV_REQUIRES_SERVICE:
					case OCV_CARGO_ACCEPTANCE:
					case OCV_CARGO_WAITING:
						mask = 0x3F;
						break;

					case OCV_VEH_IN_SLOT:
					case OCV_SLOT_OCCUPANCY:
						mask = 0x3C;
						break;

					case OCV_TIMETABLE:
						mask = 0xC3;
						break;

					default:
						mask = 0xC0;
						break;
				}
				ShowDropDownMenu(this, GetComparatorStrings(this->vehicle, o), o->GetConditionComparator(), WID_O_COND_COMPARATOR, 0, mask, 0, DDSF_LOST_FOCUS);
				break;
			}

			case WID_O_COND_VALUE: {
				const Order *order = this->vehicle->GetOrder(this->OrderGetSel());
				uint value;
				CharSetFilter charset_filter = CS_NUMERAL;
				switch (order->GetConditionVariable()) {
					case OCV_CARGO_LOAD_PERCENTAGE:
					case OCV_TIME_DATE:
						value = order->GetXData();
						break;

					case OCV_TIMETABLE:
						value = order->GetXData();
						if (!_settings_client.gui.timetable_in_ticks) {
							value /= DATE_UNIT_SIZE;
							charset_filter = CS_NUMERAL_DECIMAL;
						}
						break;

					case OCV_CARGO_WAITING_AMOUNT:
					case OCV_COUNTER_VALUE:
						value = GB(order->GetXData(), 0, 16);
						break;

					default:
						value = order->GetConditionValue();
						break;
				}
				if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value, this->vehicle->type);
				if (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) value = ConvertCargoQuantityToDisplayQuantity(order->GetConditionValue(), value);
				this->query_text_widget = widget;
				SetDParam(0, value);
				ShowQueryString(STR_JUST_INT, STR_ORDER_CONDITIONAL_VALUE_CAPT, (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) ? 12 : 6, this, charset_filter, QSF_NONE);
				break;
			}

			case WID_O_SHARED_ORDER_LIST:
				ShowVehicleListWindow(this->vehicle);
				break;

			case WID_O_ADD_VEH_GROUP: {
				this->query_text_widget = WID_O_ADD_VEH_GROUP;
				ShowQueryString(STR_EMPTY, STR_GROUP_RENAME_CAPTION, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;
			}

			case WID_O_OCCUPANCY_TOGGLE:
				ToggleWidgetLoweredState(WID_O_OCCUPANCY_TOGGLE);
				this->UpdateButtonState();
				this->ReInit();
				break;

			case WID_O_RELEASE_SLOT: {
				int selected;
				TraceRestrictSlotID value = this->vehicle->GetOrder(this->OrderGetSel())->GetDestination();
				DropDownList list = GetSlotDropDownList(this->vehicle->owner, value, selected, this->vehicle->type, false);
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_RELEASE_SLOT, 0);
				break;
			}

			case WID_O_COUNTER_OP: {
				DropDownList list;
				list.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_COUNTER_INCREASE, 0, false));
				list.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_COUNTER_DECREASE, 1, false));
				list.emplace_back(new DropDownListStringItem(STR_TRACE_RESTRICT_COUNTER_SET, 2, false));
				int selected = this->vehicle->GetOrder(this->OrderGetSel())->GetCounterOperation();
				ShowDropDownList(this, std::move(list), selected, WID_O_COUNTER_OP, 0);
				break;
			}

			case WID_O_CHANGE_COUNTER: {
				int selected;
				TraceRestrictCounterID value = this->vehicle->GetOrder(this->OrderGetSel())->GetDestination();
				DropDownList list = GetCounterDropDownList(this->vehicle->owner, value, selected);
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_CHANGE_COUNTER, 0);
				break;
			}

			case WID_O_COUNTER_VALUE: {
				const Order *order = this->vehicle->GetOrder(this->OrderGetSel());
				this->query_text_widget = widget;
				SetDParam(0, order->GetXData());
				ShowQueryString(STR_JUST_INT, STR_TRACE_RESTRICT_VALUE_CAPTION, 10, this, CS_NUMERAL, QSF_NONE);
				break;
			}

			case WID_O_TEXT_LABEL: {
				const Order *order = this->vehicle->GetOrder(this->OrderGetSel());
				this->query_text_widget = widget;
				SetDParamStr(0, order->GetLabelText());
				ShowQueryString(STR_JUST_RAW_STRING, STR_ORDER_LABEL_TEXT_CAPTION, NUM_CARGO - 1, this, CS_ALPHANUMERAL, QSF_NONE);
				break;
			}

			case WID_O_DEPARTURE_VIA_TYPE: {
				DropDownList list;
				list.emplace_back(new DropDownListStringItem(STR_ORDER_LABEL_DEPARTURES_SHOW_AS_VIA, OLST_DEPARTURES_VIA, false));
				list.emplace_back(new DropDownListStringItem(STR_ORDER_LABEL_DEPARTURES_REMOVE_VIA, OLST_DEPARTURES_REMOVE_VIA, false));
				int selected = this->vehicle->GetOrder(this->OrderGetSel())->GetLabelSubType();
				ShowDropDownList(this, std::move(list), selected, WID_O_DEPARTURE_VIA_TYPE, 0);
				break;
			}
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (this->query_text_widget == WID_O_COND_VALUE && !StrEmpty(str)) {
			VehicleOrderID sel = this->OrderGetSel();
			uint value = atoi(str);

			switch (this->vehicle->GetOrder(sel)->GetConditionVariable()) {
				case OCV_MAX_SPEED:
					value = Clamp(ConvertDisplaySpeedToSpeed(value, this->vehicle->type), 0, 2047);
					break;

				case OCV_PERCENT:
				case OCV_RELIABILITY:
				case OCV_LOAD_PERCENTAGE:
				case OCV_CARGO_LOAD_PERCENTAGE:
					value = Clamp(value, 0, 100);
					break;

				case OCV_CARGO_WAITING_AMOUNT:
					value = Clamp(ConvertDisplayQuantityToCargoQuantity(this->vehicle->GetOrder(sel)->GetConditionValue(), value), 0, 0xFFFF);
					break;

				case OCV_COUNTER_VALUE:
				case OCV_TIME_DATE:
					value = Clamp(value, 0, 0xFFFF);
					break;

				case OCV_TIMETABLE: {
					value = Clamp(ParseTimetableDuration(str), 0, 0xFFFF);
					break;
				}

				default:
					value = Clamp(value, 0, 2047);
					break;
			}
			this->ModifyOrder(sel, MOF_COND_VALUE | value << 8);
		}

		if (this->query_text_widget == WID_O_COUNTER_VALUE && !StrEmpty(str)) {
			VehicleOrderID sel = this->OrderGetSel();
			uint value = Clamp(atoi(str), 0, 0xFFFF);
			this->ModifyOrder(sel, MOF_COUNTER_VALUE | value << 8);
		}

		if (this->query_text_widget == WID_O_ADD_VEH_GROUP) {
			DoCommandP(0, VehicleListIdentifier(VL_SINGLE_VEH, this->vehicle->type, this->vehicle->owner, this->vehicle->index).Pack(), CF_ANY, CMD_CREATE_GROUP_FROM_LIST | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), nullptr, str);
		}

		if (this->query_text_widget == WID_O_TEXT_LABEL && str != nullptr) {
			this->ModifyOrder(this->OrderGetSel(), MOF_LABEL_TEXT, true, str);
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_O_NON_STOP:
				this->OrderClick_Nonstop(index);
				break;

			case WID_O_FULL_LOAD:
				this->OrderClick_FullLoad((OrderLoadFlags)index);
				break;

			case WID_O_UNLOAD:
				this->OrderClick_Unload((OrderUnloadFlags)index);
				break;

			case WID_O_GOTO:
				switch (index) {
					case 0: this->OrderClick_Goto(OPOS_GOTO); break;
					case 1: this->OrderClick_NearestDepot(); break;
					case 2: this->OrderClick_Goto(OPOS_CONDITIONAL); break;
					case 3: this->OrderClick_Goto(OPOS_SHARE); break;
					case 4: this->OrderClick_ReleaseSlot(); break;
					case 5: this->OrderClick_ChangeCounter(); break;
					case 6: this->OrderClick_TextLabel(); break;
					case 7: this->OrderClick_Goto(OPOS_DEPARTURE_VIA); break;
					default: NOT_REACHED();
				}
				break;

			case WID_O_SERVICE:
				this->OrderClick_Service(index);
				break;

			case WID_O_REFIT_DROPDOWN:
				this->OrderClick_Refit(index, true);
				break;

			case WID_O_COND_VARIABLE:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VARIABLE | index << 8);
				break;

			case WID_O_COND_COMPARATOR:
				if (index >= 0x100) {
					const Order *o = this->vehicle->GetOrder(this->OrderGetSel());
					if (o == nullptr || o->GetConditionVariable() != OCV_DISPATCH_SLOT) return;
					this->ModifyOrder(this->OrderGetSel(), MOF_COND_COMPARATOR | ((index & 1) ? OCC_IS_FALSE : OCC_IS_TRUE) << 8);
					this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | ((o->GetConditionValue() & 2) | ((index & 2) >> 1)) << 8);
				} else {
					this->ModifyOrder(this->OrderGetSel(), MOF_COND_COMPARATOR | index << 8);
				}
				break;

			case WID_O_COND_CARGO:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE | index << 8);
				break;

			case WID_O_COND_AUX_CARGO:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | index << 8);
				break;

			case WID_O_COND_SLOT:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE | index << 8);
				break;

			case WID_O_COND_COUNTER:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | index << 8);
				break;

			case WID_O_COND_TIME_DATE:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | index << 8);
				break;

			case WID_O_COND_TIMETABLE:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | index << 8);
				break;

			case WID_O_COND_SCHED_SELECT:
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE | index << 8);
				break;

			case WID_O_COND_SCHED_TEST: {
				const Order *o = this->vehicle->GetOrder(this->OrderGetSel());
				if (o == nullptr) return;
				index = (index * 2) | (o->GetConditionValue() & 1);
				this->ModifyOrder(this->OrderGetSel(), MOF_COND_VALUE_2 | index << 8);
				break;
			}

			case WID_O_RELEASE_SLOT:
				this->ModifyOrder(this->OrderGetSel(), MOF_SLOT | index << 8);
				break;

			case WID_O_COUNTER_OP:
				this->ModifyOrder(this->OrderGetSel(), MOF_COUNTER_OP | index << 8);
				break;

			case WID_O_CHANGE_COUNTER:
				this->ModifyOrder(this->OrderGetSel(), MOF_COUNTER_ID | index << 8);
				break;

			case WID_O_DEPARTURE_VIA_TYPE:
				this->ModifyOrder(this->OrderGetSel(), MOF_DEPARTURES_SUBTYPE | index << 8);
				break;

			case WID_O_MGMT_LIST_BTN:
				switch (index) {
					case 0: this->OrderClick_ReverseOrderList(0); break;
					case 1: this->OrderClick_ReverseOrderList(1); break;
					default: NOT_REACHED();
				}
				break;

			case WID_O_MGMT_BTN:
				if (this->goto_type == OPOS_CONDITIONAL_RETARGET) {
					ResetObjectToPlace();
					break;
				}
				if (index >= 0x100 && index <= 0x100 + INVALID_COLOUR) {
					this->ModifyOrder(this->OrderGetSel(), MOF_COLOUR | (index & 0xFF) << 8);
					break;
				}
				switch (index) {
					case 0:
						DoCommandP(this->vehicle->tile, this->vehicle->index, this->OrderGetSel(), CMD_DUPLICATE_ORDER | CMD_MSG(STR_ERROR_CAN_T_INSERT_NEW_ORDER));
						break;

					case 1:
						this->OrderClick_Goto(OPOS_CONDITIONAL_RETARGET);
						break;

					default:
						NOT_REACHED();
				}
				break;
		}
	}

	void OnDragDrop(Point pt, int widget) override
	{
		switch (widget) {
			case WID_O_ORDER_LIST: {
				VehicleOrderID from_order = this->OrderGetSel();
				VehicleOrderID to_order = this->GetOrderFromPt(pt.y);

				if (!(from_order == to_order || from_order == INVALID_VEH_ORDER_ID || from_order > this->vehicle->GetNumOrders() || to_order == INVALID_VEH_ORDER_ID || to_order > this->vehicle->GetNumOrders()) &&
						DoCommandP(this->vehicle->tile, this->vehicle->index, from_order | (to_order << 16), CMD_MOVE_ORDER | CMD_MSG(STR_ERROR_CAN_T_MOVE_THIS_ORDER))) {
					this->selected_order = -1;
					this->UpdateButtonState();
				}
				break;
			}

			case WID_O_DELETE:
				this->OrderClick_Delete();
				break;

			case WID_O_STOP_SHARING:
				this->OrderClick_StopSharing();
				break;
		}

		ResetObjectToPlace();

		if (this->order_over != INVALID_VEH_ORDER_ID) {
			/* End of drag-and-drop, hide dragged order destination highlight. */
			this->order_over = INVALID_VEH_ORDER_ID;
			this->SetWidgetDirty(WID_O_ORDER_LIST);
		}
	}

	EventState OnHotkey(int hotkey) override
	{
		if (this->vehicle->owner != _local_company) return ES_NOT_HANDLED;

		switch (hotkey) {
			case OHK_SKIP:           this->OrderClick_Skip(); break;
			case OHK_DELETE:         this->OrderClick_Delete(); break;
			case OHK_GOTO:           this->OrderClick_Goto(OPOS_GOTO); break;
			case OHK_NONSTOP:        this->OrderClick_Nonstop(-1); break;
			case OHK_VIA:            this->OrderClick_Nonstop(-2); break;
			case OHK_FULLLOAD:       this->OrderClick_FullLoad(OLF_FULL_LOAD_ANY, true); break;
			case OHK_UNLOAD:         this->OrderClick_Unload(OUFB_UNLOAD, true); break;
			case OHK_NEAREST_DEPOT:  this->OrderClick_NearestDepot(); break;
			case OHK_ALWAYS_SERVICE: this->OrderClick_Service(-1); break;
			case OHK_TRANSFER:       this->OrderClick_Unload(OUFB_TRANSFER, true); break;
			case OHK_NO_UNLOAD:      this->OrderClick_Unload(OUFB_NO_UNLOAD, true); break;
			case OHK_NO_LOAD:        this->OrderClick_FullLoad(OLFB_NO_LOAD, true); break;
			default: return ES_NOT_HANDLED;
		}
		return ES_HANDLED;
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		if (this->goto_type == OPOS_GOTO) {
			const Order cmd = GetOrderCmdFromTile(this->vehicle, tile);
			if (cmd.IsType(OT_NOTHING)) return;

			if (this->InsertNewOrder(cmd.Pack())) {
				/* With quick goto the Go To button stays active */
				if (!_settings_client.gui.quick_goto) ResetObjectToPlace();
			}
		} else if (this->goto_type == OPOS_COND_VIA || this->goto_type == OPOS_COND_STATION) {
			if (IsTileType(tile, MP_STATION) || IsTileType(tile, MP_INDUSTRY)) {
				const Station *st = nullptr;

				if (IsTileType(tile, MP_STATION)) {
					st = Station::GetByTile(tile);
				} else {
					const Industry *in = Industry::GetByTile(tile);
					st = in->neutral_station;
				}
				if (st != nullptr && IsInfraUsageAllowed(this->vehicle->type, this->vehicle->owner, st->owner)) {
					if (this->ModifyOrder(this->OrderGetSel(), (this->goto_type == OPOS_COND_VIA ? MOF_COND_VALUE_3 : MOF_COND_STATION_ID) | st->index << 8)) {
						ResetObjectToPlace();
					}
				}
			}
		} else if (this->goto_type == OPOS_DEPARTURE_VIA) {
			if (IsTileType(tile, MP_STATION) || IsTileType(tile, MP_INDUSTRY)) {
				const BaseStation *st = nullptr;

				if (IsTileType(tile, MP_STATION)) {
					st = BaseStation::GetByTile(tile);
				} else {
					const Industry *in = Industry::GetByTile(tile);
					st = in->neutral_station;
				}
				if (st != nullptr && IsInfraUsageAllowed(this->vehicle->type, this->vehicle->owner, st->owner)) {
					Order order;
					order.next = nullptr;
					order.index = 0;
					order.MakeLabel(OLST_DEPARTURES_VIA);
					order.SetDestination(st->index);

					if (this->InsertNewOrder(order.Pack())) {
						ResetObjectToPlace();
					}
				}
			}
		}
	}

	bool OnVehicleSelect(const Vehicle *v) override
	{
		/* v is vehicle getting orders. Only copy/clone orders if vehicle doesn't have any orders yet.
		 * We disallow copying orders of other vehicles if we already have at least one order entry
		 * ourself as it easily copies orders of vehicles within a station when we mean the station.
		 * Obviously if you press CTRL on a non-empty orders vehicle you know what you are doing
		 * TODO: give a warning message */
		bool share_order = _ctrl_pressed || this->goto_type == OPOS_SHARE;
		if (this->vehicle->GetNumOrders() != 0 && !share_order) return false;

		if (DoCommandP(this->vehicle->tile, this->vehicle->index | (share_order ? CO_SHARE : CO_COPY) << 30, v->index,
				share_order ? CMD_CLONE_ORDER | CMD_MSG(STR_ERROR_CAN_T_SHARE_ORDER_LIST) : CMD_CLONE_ORDER | CMD_MSG(STR_ERROR_CAN_T_COPY_ORDER_LIST))) {
			this->selected_order = -1;
			ResetObjectToPlace();
		}
		return true;
	}

	/**
	 * Clones an order list from a vehicle list.  If this doesn't make sense (because not all vehicles in the list have the same orders), then it displays an error.
	 * @return This always returns true, which indicates that the contextual action handled the mouse click.
	 *         Note that it's correct behaviour to always handle the click even though an error is displayed,
	 *         because users aren't going to expect the default action to be performed just because they overlooked that cloning doesn't make sense.
	 */
	bool OnVehicleSelect(VehicleList::const_iterator begin, VehicleList::const_iterator end) override
	{
		bool share_order = _ctrl_pressed || this->goto_type == OPOS_SHARE;
		if (this->vehicle->GetNumOrders() != 0 && !share_order) return false;

		if (!share_order) {
			/* If CTRL is not pressed: If all the vehicles in this list have the same orders, then copy orders */
			if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
				return VehiclesHaveSameOrderList(v1, v2);
			})) {
				OnVehicleSelect(*begin);
			} else {
				ShowErrorMessage(STR_ERROR_CAN_T_COPY_ORDER_LIST, STR_ERROR_CAN_T_COPY_ORDER_VEHICLE_LIST, WL_INFO);
			}
		} else {
			/* If CTRL is pressed: If all the vehicles in this list share orders, then copy orders */
			if (AllEqual(begin, end, [](const Vehicle *v1, const Vehicle *v2) {
				return v1->FirstShared() == v2->FirstShared();
			})) {
				OnVehicleSelect(*begin);
			} else {
				ShowErrorMessage(STR_ERROR_CAN_T_SHARE_ORDER_LIST, STR_ERROR_CAN_T_SHARE_ORDER_VEHICLE_LIST, WL_INFO);
			}
		}

		return true;
	}

	void OnPlaceObjectAbort() override
	{
		this->goto_type = OPOS_NONE;
		this->SetWidgetDirty(WID_O_GOTO);
		this->SetWidgetDirty(WID_O_COND_AUX_VIA);
		this->SetWidgetDirty(WID_O_COND_AUX_STATION);
		this->SetWidgetDirty(WID_O_MGMT_BTN);

		/* Remove drag highlighting if it exists. */
		if (this->order_over != INVALID_VEH_ORDER_ID) {
			this->order_over = INVALID_VEH_ORDER_ID;
			this->SetWidgetDirty(WID_O_ORDER_LIST);
		}
	}

	void OnMouseDrag(Point pt, int widget) override
	{
		if (this->selected_order != -1 && widget == WID_O_ORDER_LIST) {
			/* An order is dragged.. */
			VehicleOrderID from_order = this->OrderGetSel();
			VehicleOrderID to_order = this->GetOrderFromPt(pt.y);
			uint num_orders = this->vehicle->GetNumOrders();

			if (from_order != INVALID_VEH_ORDER_ID && from_order <= num_orders) {
				if (to_order != INVALID_VEH_ORDER_ID && to_order <= num_orders) { // ..over an existing order.
					this->order_over = to_order;
					this->SetWidgetDirty(widget);
				} else if (from_order != to_order && this->order_over != INVALID_VEH_ORDER_ID) { // ..outside of the order list.
					this->order_over = INVALID_VEH_ORDER_ID;
					this->SetWidgetDirty(widget);
				}
			}
		}
	}

	void OnResize() override
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, WID_O_ORDER_LIST);
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_O_SHARED_ORDER_LIST: {
				if (this->vehicle->owner == _local_company) {
					SetDParam(0, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP);
					GuiShowTooltips(this, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP_EXTRA, close_cond, 1);
					return true;
				}
				return false;
			}
			default:
				return false;
		}
	}

	const Vehicle *GetVehicle()
	{
		return this->vehicle;
	}

	static HotkeyList hotkeys;
};

static Hotkey order_hotkeys[] = {
	Hotkey('D', "skip", OHK_SKIP),
	Hotkey('F', "delete", OHK_DELETE),
	Hotkey('G', "goto", OHK_GOTO),
	Hotkey('H', "nonstop", OHK_NONSTOP),
	Hotkey((uint16)0, "via", OHK_VIA),
	Hotkey('J', "fullload", OHK_FULLLOAD),
	Hotkey('K', "unload", OHK_UNLOAD),
	Hotkey((uint16)0, "nearest_depot", OHK_NEAREST_DEPOT),
	Hotkey((uint16)0, "always_service", OHK_ALWAYS_SERVICE),
	Hotkey((uint16)0, "transfer", OHK_TRANSFER),
	Hotkey((uint16)0, "no_unload", OHK_NO_UNLOAD),
	Hotkey((uint16)0, "no_load", OHK_NO_LOAD),
	HOTKEY_LIST_END
};
HotkeyList OrdersWindow::hotkeys("order", order_hotkeys);

/** Nested widget definition for "your" train orders. */
static const NWidgetPart _nested_orders_train_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_O_CAPTION), SetDataTip(STR_ORDERS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(0x0, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_O_ORDER_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_ORDERS_LIST_TOOLTIP), SetResize(1, 1), SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_OCCUPANCY),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_O_OCCUPANCY_LIST), SetMinimalSize(50, 0), SetFill(0, 1), SetDataTip(STR_NULL, STR_ORDERS_OCCUPANCY_LIST_TOOLTIP),
															SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_O_SCROLLBAR),
	EndContainer(),

	/* First button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_TOP_ROW_GROUNDVEHICLE),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_NON_STOP), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_NON_STOP, STR_ORDER_TOOLTIP_NON_STOP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_TOP_LEFT),
					NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_FULL_LOAD), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_TOGGLE_FULL_LOAD, STR_ORDER_TOOLTIP_FULL_LOAD), SetResize(1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_REFIT), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_REFIT, STR_ORDER_REFIT_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_REVERSE), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_REVERSE, STR_ORDER_REVERSE_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_TOP_MIDDLE),
					NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_UNLOAD), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_TOGGLE_UNLOAD, STR_ORDER_TOOLTIP_UNLOAD), SetResize(1, 0),
					NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_SERVICE), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_SERVICE, STR_ORDER_SERVICE_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_TOP_RIGHT),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_EMPTY), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_REFIT, STR_ORDER_REFIT_TOOLTIP), SetResize(1, 0),
					NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_REFIT_DROPDOWN), SetMinimalSize(93, 12), SetFill(1, 0),
															SetDataTip(STR_ORDER_REFIT_AUTO, STR_ORDER_REFIT_AUTO_TOOLTIP), SetResize(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_VARIABLE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_VARIABLE_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_AUX_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_TIME_DATE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_TIME_DATE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_TIMETABLE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_TIMETABLE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COUNTER), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COUNTER_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SCHED_SELECT), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SCHED_SELECT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX3),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_STATION), SetMinimalSize(72, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_STATION, STR_ORDER_CONDITIONAL_STATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX2),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_VIA), SetMinimalSize(36, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_VIA, STR_ORDER_CONDITIONAL_VIA_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SCHED_TEST), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SCHED_TEST_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_VALUE),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COND_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_JUST_COMMA, STR_ORDER_CONDITIONAL_VALUE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SLOT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_RELEASE_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_ORDER_RELEASE_SLOT_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COUNTER_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_STRING, STR_TRACE_RESTRICT_COUNTER_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_CHANGE_COUNTER), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_ORDER_CHANGE_COUNTER_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COUNTER_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_COMMA, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_TEXT_LABEL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_ORDER_LABEL_TEXT_BUTTON, STR_ORDER_LABEL_TEXT_BUTTON_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_DEPARTURE_VIA_TYPE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_STRING, STR_ORDER_LABEL_DEPARTURES_VIA_TYPE_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		EndContainer(),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_OCCUPANCY_TOGGLE), SetMinimalSize(36, 12), SetDataTip(STR_ORDERS_OCCUPANCY_BUTTON, STR_ORDERS_OCCUPANCY_BUTTON_TOOLTIP),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_SHARED),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_O_SHARED_ORDER_LIST), SetMinimalSize(12, 12), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_ADD_VEH_GROUP), SetMinimalSize(12, 12), SetDataTip(STR_BLACK_PLUS, STR_ORDERS_NEW_GROUP_TOOLTIP),
		EndContainer(),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_MGMT),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_MGMT_BTN), SetMinimalSize(100, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_MANAGE_ORDER, STR_ORDERS_MANAGE_ORDER_TOOLTIP), SetResize(1, 0), SetAlignment(SA_TOP | SA_LEFT),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_MGMT_LIST_BTN), SetMinimalSize(100, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_MANAGE_LIST, STR_ORDERS_MANAGE_LIST_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_SKIP), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_SKIP_BUTTON, STR_ORDERS_SKIP_TOOLTIP), SetResize(1, 0),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_BOTTOM_MIDDLE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_DELETE), SetMinimalSize(100, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_STOP_SHARING), SetMinimalSize(100, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_STOP_SHARING_BUTTON, STR_ORDERS_STOP_SHARING_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_GOTO), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_GO_TO_BUTTON, STR_ORDERS_GO_TO_TOOLTIP), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _orders_train_desc(
	WDP_AUTO, "view_vehicle_orders_train", 384, 100,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	_nested_orders_train_widgets, lengthof(_nested_orders_train_widgets),
	&OrdersWindow::hotkeys
);

/** Nested widget definition for "your" orders (non-train). */
static const NWidgetPart _nested_orders_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_O_CAPTION), SetDataTip(STR_ORDERS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(0x0, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_O_ORDER_LIST), SetMinimalSize(372, 62), SetDataTip(0x0, STR_ORDERS_LIST_TOOLTIP), SetResize(1, 1), SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_OCCUPANCY),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_O_OCCUPANCY_LIST), SetMinimalSize(50, 0), SetFill(0, 1), SetDataTip(STR_NULL, STR_ORDERS_OCCUPANCY_LIST_TOOLTIP),
															SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_O_SCROLLBAR),
	EndContainer(),

	/* First button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_TOP_ROW),
			/* Load + unload + refit buttons. */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_FULL_LOAD), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDER_TOGGLE_FULL_LOAD, STR_ORDER_TOOLTIP_FULL_LOAD), SetResize(1, 0),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_UNLOAD), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDER_TOGGLE_UNLOAD, STR_ORDER_TOOLTIP_UNLOAD), SetResize(1, 0),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_REFIT_DROPDOWN), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDER_REFIT_AUTO, STR_ORDER_REFIT_AUTO_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			/* Refit + service buttons. */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_REFIT), SetMinimalSize(186, 12), SetFill(1, 0),
													SetDataTip(STR_ORDER_REFIT, STR_ORDER_REFIT_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_SERVICE), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDER_SERVICE, STR_ORDER_SERVICE_TOOLTIP), SetResize(1, 0),
			EndContainer(),

			/* Buttons for setting a condition. */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_VARIABLE), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_VARIABLE_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_AUX_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_TIME_DATE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_TIME_DATE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_TIMETABLE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_TIMETABLE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COUNTER), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COUNTER_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SCHED_SELECT), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SCHED_SELECT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX3),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_STATION), SetMinimalSize(72, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_STATION, STR_ORDER_CONDITIONAL_STATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX2),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_VIA), SetMinimalSize(36, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_VIA, STR_ORDER_CONDITIONAL_VIA_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SCHED_TEST), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SCHED_TEST_TOOLTIP), SetResize(1, 0),
				EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_VALUE),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COND_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_JUST_COMMA, STR_ORDER_CONDITIONAL_VALUE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SLOT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
			EndContainer(),

			/* Buttons for releasing a slot. */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_RELEASE_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_ORDER_RELEASE_SLOT_TOOLTIP), SetResize(1, 0),
			EndContainer(),

			/* Buttons for changing a counter. */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COUNTER_OP), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_STRING, STR_TRACE_RESTRICT_COUNTER_OP_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_CHANGE_COUNTER), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_NULL, STR_ORDER_CHANGE_COUNTER_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COUNTER_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_COMMA, STR_TRACE_RESTRICT_COND_VALUE_TOOLTIP), SetResize(1, 0),
			EndContainer(),

			/* Buttons for changing a text label */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_TEXT_LABEL), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_ORDER_LABEL_TEXT_BUTTON, STR_ORDER_LABEL_TEXT_BUTTON_TOOLTIP), SetResize(1, 0),
			EndContainer(),

			/* Buttons for changing a departure board via order */
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_DEPARTURE_VIA_TYPE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_JUST_STRING, STR_ORDER_LABEL_DEPARTURES_VIA_TYPE_TOOLTIP), SetResize(1, 0),
			EndContainer(),

			/* No buttons */
			NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0), SetResize(1, 0), EndContainer(),
		EndContainer(),

		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_OCCUPANCY_TOGGLE), SetMinimalSize(36, 12), SetDataTip(STR_ORDERS_OCCUPANCY_BUTTON, STR_ORDERS_OCCUPANCY_BUTTON_TOOLTIP),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_SHARED),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_O_SHARED_ORDER_LIST), SetMinimalSize(12, 12), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_ADD_VEH_GROUP), SetMinimalSize(12, 12), SetDataTip(STR_BLACK_PLUS, STR_ORDERS_NEW_GROUP_TOOLTIP),
		EndContainer(),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_MGMT),
			NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_MGMT_BTN), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_MANAGE_ORDER, STR_ORDERS_MANAGE_ORDER_TOOLTIP), SetResize(1, 0), SetAlignment(SA_TOP | SA_LEFT),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_MGMT_LIST_BTN), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_MANAGE_LIST, STR_ORDERS_MANAGE_LIST_TOOLTIP), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_SKIP), SetMinimalSize(100, 12), SetFill(1, 0),
												SetDataTip(STR_ORDERS_SKIP_BUTTON, STR_ORDERS_SKIP_TOOLTIP), SetResize(1, 0),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_BOTTOM_MIDDLE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_DELETE), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_TOOLTIP), SetResize(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_STOP_SHARING), SetMinimalSize(100, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_STOP_SHARING_BUTTON, STR_ORDERS_STOP_SHARING_TOOLTIP), SetResize(1, 0),
		EndContainer(),
		NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_GOTO), SetMinimalSize(100, 12), SetFill(1, 0),
											SetDataTip(STR_ORDERS_GO_TO_BUTTON, STR_ORDERS_GO_TO_TOOLTIP), SetResize(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _orders_desc(
	WDP_AUTO, "view_vehicle_orders", 384, 100,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	_nested_orders_widgets, lengthof(_nested_orders_widgets),
	&OrdersWindow::hotkeys
);

/** Nested widget definition for competitor orders. */
static const NWidgetPart _nested_other_orders_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_O_CAPTION), SetDataTip(STR_ORDERS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(0x0, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_O_ORDER_LIST), SetMinimalSize(372, 72), SetDataTip(0x0, STR_ORDERS_LIST_TOOLTIP), SetResize(1, 1), SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_OCCUPANCY),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_O_OCCUPANCY_LIST), SetMinimalSize(50, 0), SetFill(0, 1), SetDataTip(STR_NULL, STR_ORDERS_OCCUPANCY_LIST_TOOLTIP),
															SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_O_SCROLLBAR),
	EndContainer(),

	/* First button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_OCCUPANCY_TOGGLE), SetMinimalSize(36, 12), SetDataTip(STR_ORDERS_OCCUPANCY_BUTTON, STR_ORDERS_OCCUPANCY_BUTTON_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_O_SHARED_ORDER_LIST), SetMinimalSize(12, 12), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _other_orders_desc(
	WDP_AUTO, "view_vehicle_orders_competitor", 384, 86,
	WC_VEHICLE_ORDERS, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	_nested_other_orders_widgets, lengthof(_nested_other_orders_widgets),
	&OrdersWindow::hotkeys
);

void ShowOrdersWindow(const Vehicle *v)
{
	CloseWindowById(WC_VEHICLE_DETAILS, v->index, false);
	CloseWindowById(WC_VEHICLE_TIMETABLE, v->index, false);
	if (BringWindowToFrontById(WC_VEHICLE_ORDERS, v->index) != nullptr) return;

	/* Using a different WindowDescs for _local_company causes problems.
	 * Due to this we have to close order windows in ChangeWindowOwner/DeleteCompanyWindows,
	 * because we cannot change switch the WindowDescs and keeping the old WindowDesc results
	 * in crashed due to missing widges.
	 * TODO Rewrite the order GUI to not use different WindowDescs.
	 */
	if (v->owner != _local_company) {
		new OrdersWindow(&_other_orders_desc, v);
	} else {
		new OrdersWindow(v->IsGroundVehicle() ? &_orders_train_desc : &_orders_desc, v);
	}
}
