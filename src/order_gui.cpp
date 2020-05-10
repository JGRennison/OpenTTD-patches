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
#include "vehiclelist.h"
#include "tracerestrict.h"
#include "scope.h"

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

DropDownList GetSlotDropDownList(Owner owner, TraceRestrictSlotID slot_id, int &selected);

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
		for (int i = 0; i < _sorted_standard_cargo_specs_size; i++) {
			SetDParam(0, _sorted_cargo_specs[i]->name);
			this->max_cargo_name_width = max(this->max_cargo_name_width, GetStringBoundingBox(STR_JUST_STRING).width);
		}
		this->max_cargo_dropdown_width = 0;
		for (int i = 0; this->cargo_type_order_dropdown[i] != INVALID_STRING_ID; i++) {
			SetDParam(0, this->cargo_type_order_dropdown[i]);
			this->max_cargo_dropdown_width = max(this->max_cargo_dropdown_width, GetStringBoundingBox(STR_JUST_STRING).width);
		}
	}

	/** Populate the selected entry of order dropdowns. */
	void InitDropdownSelectedTypes()
	{
		StringID tooltip = STR_CARGO_TYPE_LOAD_ORDERS_DROP_TOOLTIP + this->variant;
		const Order *order = this->vehicle->GetOrder(this->order_id);
		for (int i = 0; i < _sorted_standard_cargo_specs_size; i++) {
			const CargoSpec *cs = _sorted_cargo_specs[i];
			const CargoID cargo_id = cs->Index();
			uint8 order_type = (this->variant == CTOWV_LOAD) ? (uint8) order->GetCargoLoadTypeRaw(cargo_id) : (uint8) order->GetCargoUnloadTypeRaw(cargo_id);
			this->GetWidget<NWidgetCore>(WID_CTO_CARGO_DROPDOWN_FIRST + i)->SetDataTip(this->cargo_type_order_dropdown[order_type], tooltip);
		}
		this->set_to_all_dropdown_sel = 0;
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

		this->CreateNestedTree(desc);
		this->GetWidget<NWidgetCore>(WID_CTO_CAPTION)->SetDataTip(STR_CARGO_TYPE_ORDERS_LOAD_CAPTION + this->variant, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS);
		this->GetWidget<NWidgetCore>(WID_CTO_HEADER)->SetDataTip(STR_CARGO_TYPE_ORDERS_LOAD_TITLE + this->variant, STR_NULL);
		this->InitDropdownSelectedTypes();
		this->FinishInitNested(v->index);

		this->owner = v->owner;
	}

	~CargoTypeOrdersWindow()
	{
		if (!FocusWindowById(WC_VEHICLE_ORDERS, this->window_number)) {
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) OVERRIDE
	{
		if (widget == WID_CTO_HEADER) {
			(*size).height = max((*size).height, (uint) WD_FRAMERECT_TOP + FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM);
		} else if (WID_CTO_CARGO_LABEL_FIRST <= widget && widget <= WID_CTO_CARGO_LABEL_LAST) {
			(*size).width  = max((*size).width,  WD_FRAMERECT_LEFT + this->CARGO_ICON_WIDTH + WD_FRAMETEXT_LEFT + this->max_cargo_name_width + WD_FRAMETEXT_RIGHT + padding.width);
			(*size).height = max((*size).height, (uint) WD_FRAMERECT_TOP + FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM);
		} else if ((WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) || widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			(*size).width  = max((*size).width,  WD_DROPDOWNTEXT_LEFT + this->max_cargo_dropdown_width + WD_DROPDOWNTEXT_RIGHT + NWidgetLeaf::dropdown_dimension.width);
			(*size).height = max((*size).height, (uint) WD_DROPDOWNTEXT_TOP + FONT_HEIGHT_NORMAL + WD_DROPDOWNTEXT_BOTTOM);
		} else if (widget == WID_CTO_SET_TO_ALL_LABEL) {
			(*size).width = max((*size).width, this->max_cargo_name_width + WD_FRAMETEXT_RIGHT + padding.width);
			(*size).height = max((*size).height, (uint) WD_FRAMERECT_TOP + FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM);
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const OVERRIDE
	{
		if (WID_CTO_CARGO_LABEL_FIRST <= widget && widget <= WID_CTO_CARGO_LABEL_LAST) {
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_LABEL_FIRST];
			bool rtl = (_current_text_dir == TD_RTL);

			/* Draw cargo icon. */
			int rect_left   = rtl ? r.right - WD_FRAMETEXT_RIGHT - this->CARGO_ICON_WIDTH : r.left + WD_FRAMERECT_LEFT;
			int rect_right  = rect_left + this->CARGO_ICON_WIDTH;
			int rect_top    = r.top + WD_FRAMERECT_TOP + ((r.bottom - WD_FRAMERECT_BOTTOM - r.top - WD_FRAMERECT_TOP) - this->CARGO_ICON_HEIGHT) / 2;
			int rect_bottom = rect_top + this->CARGO_ICON_HEIGHT;
			GfxFillRect(rect_left, rect_top, rect_right, rect_bottom, PC_BLACK);
			GfxFillRect(rect_left + 1, rect_top + 1, rect_right - 1, rect_bottom - 1, cs->legend_colour);

			/* Draw cargo name */
			int text_left  = rtl ? r.left + WD_FRAMETEXT_LEFT : rect_right + WD_FRAMETEXT_LEFT;
			int text_right = rtl ? rect_left - WD_FRAMETEXT_LEFT : r.right - WD_FRAMETEXT_RIGHT;
			int text_top   = r.top + WD_FRAMERECT_TOP;
			SetDParam(0, cs->name);
			DrawString(text_left, text_right, text_top, STR_BLACK_STRING);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) OVERRIDE
	{
		if (!this->CheckOrderStillValid()) {
			delete this;
			return;
		}
		if (widget == WID_CTO_CLOSEBTN) {
			delete this;
		} else if (WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) {
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_DROPDOWN_FIRST];
			const CargoID cargo_id = cs->Index();

			ShowDropDownMenu(this, this->cargo_type_order_dropdown, this->GetOrderActionTypeForCargo(cargo_id), widget, 0, this->cargo_type_order_dropdown_hmask);
		} else if (widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			ShowDropDownMenu(this, this->cargo_type_order_dropdown, this->set_to_all_dropdown_sel, widget, 0, this->cargo_type_order_dropdown_hmask);
		}
	}

	virtual void OnDropdownSelect(int widget, int action_type) OVERRIDE
	{
		if (!this->CheckOrderStillValid()) {
			delete this;
			return;
		}
		if (WID_CTO_CARGO_DROPDOWN_FIRST <= widget && widget <= WID_CTO_CARGO_DROPDOWN_LAST) {
			const CargoSpec *cs = _sorted_cargo_specs[widget - WID_CTO_CARGO_DROPDOWN_FIRST];
			const CargoID cargo_id = cs->Index();
			uint8 order_action_type = this->GetOrderActionTypeForCargo(cargo_id);

			if (action_type == order_action_type) return;

			ModifyOrderFlags mof = (this->variant == CTOWV_LOAD) ? MOF_CARGO_TYPE_LOAD : MOF_CARGO_TYPE_UNLOAD;
			DoCommandP(this->vehicle->tile, this->vehicle->index + (this->order_id << 20), mof | (action_type << 4) | (cargo_id << 20), CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));

			this->GetWidget<NWidgetCore>(widget)->SetDataTip(this->cargo_type_order_dropdown[this->GetOrderActionTypeForCargo(cargo_id)], STR_CARGO_TYPE_LOAD_ORDERS_DROP_TOOLTIP + this->variant);
			this->SetWidgetDirty(widget);
		} else if (widget == WID_CTO_SET_TO_ALL_DROPDOWN) {
			for (int i = 0; i < _sorted_standard_cargo_specs_size; i++) {
				this->OnDropdownSelect(i + WID_CTO_CARGO_DROPDOWN_FIRST, action_type);
			}

			if (action_type != (int) this->set_to_all_dropdown_sel) {
				this->set_to_all_dropdown_sel = action_type;
				this->GetWidget<NWidgetCore>(widget)->widget_data = this->cargo_type_order_dropdown[this->set_to_all_dropdown_sel];
				this->SetWidgetDirty(widget);
			}
		}
	}

	virtual void SetStringParameters(int widget) const OVERRIDE
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

	virtual void OnFocus(Window *previously_focused_window) OVERRIDE
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	virtual void OnFocusLost(Window *newly_focused_window) OVERRIDE
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) OVERRIDE
	{
		if (!this->CheckOrderStillValid()) {
			delete this;
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
static NWidgetBase *MakeCargoTypeOrdersRows(int *biggest_index)
{
	NWidgetVertical *ver = new NWidgetVertical;

	for (int i = 0; i < _sorted_standard_cargo_specs_size; i++) {
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

/** Widgets definition of CargoTypeOrdersWindow. */
static const NWidgetPart _nested_cargo_type_orders_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CTO_CAPTION), SetDataTip(STR_NULL, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(WWT_LABEL, COLOUR_GREY, WID_CTO_HEADER), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_NULL, STR_NULL),
	EndContainer(),
	NWidgetFunction(MakeCargoTypeOrdersRows),
	NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(1, 4), SetFill(1, 0), SetResize(1, 0), EndContainer(), // SPACER
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_CTO_SET_TO_ALL_LABEL), SetPadding(0, 0, 0, WD_FRAMERECT_LEFT + 12 + WD_FRAMETEXT_LEFT), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_CARGO_TYPE_ORDERS_SET_TO_ALL_LABEL, STR_CARGO_TYPE_ORDERS_SET_TO_ALL_TOOLTIP),
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
	DeleteWindowById(desc.cls, v->index);
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
	INVALID_STRING_ID
};

static const StringID _order_goto_dropdown_aircraft[] = {
	STR_ORDER_GO_TO,
	STR_ORDER_GO_TO_NEAREST_HANGAR,
	STR_ORDER_CONDITIONAL,
	STR_ORDER_SHARE,
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
	OCV_TRAIN_IN_SLOT,
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

static const StringID _order_conditional_condition_is_fully_occupied[] = {
	STR_NULL,
	STR_NULL,
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

extern uint ConvertSpeedToDisplaySpeed(uint speed);
extern uint ConvertDisplaySpeedToSpeed(uint speed);

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
		DrawSprite(sprite, PAL_NONE, rtl ? right -     sprite_size.width : left,                     y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
		DrawSprite(sprite, PAL_NONE, rtl ? right - 2 * sprite_size.width : left + sprite_size.width, y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
	} else if (v->cur_implicit_order_index == order_index) {
		DrawSprite(sprite, PAL_NONE, rtl ? right -     sprite_size.width : left,                     y + ((int)FONT_HEIGHT_NORMAL - (int)sprite_size.height) / 2);
	}

	TextColour colour = TC_BLACK;
	if (order->IsType(OT_IMPLICIT)) {
		colour = (selected ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
	} else if (selected) {
		colour = TC_WHITE;
	}

	SetDParam(0, order_index + 1);
	DrawString(left, rtl ? right - 2 * sprite_size.width - 3 : middle, y, STR_ORDER_INDEX, colour, SA_RIGHT | SA_FORCE);

	SetDParam(7, STR_EMPTY);
	SetDParam(12, STR_EMPTY);

	/* Check range for aircraft. */
	if (v->type == VEH_AIRCRAFT && Aircraft::From(v)->GetRange() > 0 && order->IsGotoOrder()) {
		const Order *next = order->next != nullptr ? order->next : v->GetFirstOrder();
		if (GetOrderDistance(order, next, v) > Aircraft::From(v)->acache.cached_max_range_sqr) SetDParam(11, STR_ORDER_OUT_OF_RANGE);
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

			SetDParam(0, STR_ORDER_GO_TO_STATION);
			SetDParam(1, STR_ORDER_GO_TO + (v->IsGroundVehicle() ? order->GetNonStopType() : 0));
			SetDParam(2, order->GetDestination());

			if (timetable) {
				SetDParam(3, STR_EMPTY);

				if (order->GetWaitTime() > 0) {
					SetDParam(7, order->IsWaitTimetabled() ? STR_TIMETABLE_STAY_FOR : STR_TIMETABLE_STAY_FOR_ESTIMATED);
					SetTimetableParams(8, order->GetWaitTime());
				}
				timetable_wait_time_valid = true;
			} else {
				SetDParam(3, (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) ? STR_EMPTY : _station_load_types[order->IsRefit()][unload][load]);
				if (order->IsRefit()) {
					SetDParam(4, order->IsAutoRefit() ? STR_ORDER_AUTO_REFIT_ANY : CargoSpec::Get(order->GetRefitCargo())->name);
				}
				if (v->type == VEH_TRAIN && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) {
					SetDParam(7, order->GetStopLocation() + STR_ORDER_STOP_LOCATION_NEAR_END);
				}
			}
			break;
		}

		case OT_GOTO_DEPOT:
			if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
				SetDParam(0, STR_ORDER_GO_TO_NEAREST_DEPOT_FORMAT);
				if (v->type == VEH_AIRCRAFT) {
					SetDParam(2, STR_ORDER_NEAREST_HANGAR);
					SetDParam(3, STR_EMPTY);
				} else {
					SetDParam(2, STR_ORDER_NEAREST_DEPOT);
					SetDParam(3, STR_ORDER_TRAIN_DEPOT + v->type);
				}
			} else {
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
				if (!timetable && (order->GetDepotActionType() & ODATFB_HALT)) {
					SetDParam(7, STR_ORDER_STOP_ORDER);
				}

				if (!timetable && order->IsRefit()) {
					SetDParam(7, (order->GetDepotActionType() & ODATFB_HALT) ? STR_ORDER_REFIT_STOP_ORDER : STR_ORDER_REFIT_ORDER);
					SetDParam(8, CargoSpec::Get(order->GetRefitCargo())->name);
				}
			}

			if (timetable) {
				if (order->GetWaitTime() > 0) {
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
			break;
		}

		case OT_CONDITIONAL: {
			SetDParam(1, order->GetConditionSkipToOrder() + 1);
			const OrderConditionVariable ocv = order->GetConditionVariable( );
			/* handle some non-ordinary cases seperately */
			if (ocv == OCV_UNCONDITIONALLY) {
				SetDParam(0, STR_ORDER_CONDITIONAL_UNCONDITIONAL);
			} else if (ocv == OCV_PERCENT) {
				SetDParam(0, STR_CONDITIONAL_PERCENT);
				SetDParam(2, order->GetConditionValue());
			} else if (ocv == OCV_FREE_PLATFORMS) {
				SetDParam(0, STR_CONDITIONAL_FREE_PLATFORMS );
				SetDParam(2, STR_ORDER_CONDITIONAL_COMPARATOR_HAS + order->GetConditionComparator());
				SetDParam(3, order->GetConditionValue());
			} else if (ocv == OCV_SLOT_OCCUPANCY) {
				if (TraceRestrictSlot::IsValidID(order->GetXData())) {
					SetDParam(0, STR_ORDER_CONDITIONAL_SLOT);
					SetDParam(2, order->GetXData());
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_INVALID_SLOT);
					SetDParam(2, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
				}
				SetDParam(3, order->GetConditionComparator() == OCC_IS_TRUE ? STR_ORDER_CONDITIONAL_COMPARATOR_FULLY_OCCUPIED : STR_ORDER_CONDITIONAL_COMPARATOR_NOT_YET_FULLY_OCCUPIED);
			} else if (ocv == OCV_TRAIN_IN_SLOT) {
				if (TraceRestrictSlot::IsValidID(order->GetXData())) {
					SetDParam(0, STR_ORDER_CONDITIONAL_IN_SLOT);
					SetDParam(3, order->GetXData());
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_IN_INVALID_SLOT);
					SetDParam(3, STR_TRACE_RESTRICT_VARIABLE_UNDEFINED);
				}
				switch (order->GetConditionComparator()) {
					case OCC_IS_TRUE:
						SetDParam(2, STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_IN_SLOT);
						break;
					case OCC_IS_FALSE:
						SetDParam(2, STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_NOT_IN_SLOT);
						break;
					case OCC_EQUALS:
						SetDParam(2, STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_IN_ACQUIRE_SLOT);
						break;
					case OCC_NOT_EQUALS:
						SetDParam(2, STR_ORDER_CONDITIONAL_COMPARATOR_TRAIN_NOT_IN_ACQUIRE_SLOT);
						break;
					default:
						NOT_REACHED();
				}
			} else if (ocv == OCV_CARGO_LOAD_PERCENTAGE) {
				SetDParam(0, STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE_DISPLAY);
				SetDParam(2, CargoSpec::Get(order->GetConditionValue())->name);
				SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
				SetDParam(4, order->GetXData());
			} else if (ocv == OCV_CARGO_WAITING_AMOUNT) {
				if (GB(order->GetXData(), 16, 16) == 0) {
					SetDParam(0, STR_ORDER_CONDITIONAL_CARGO_WAITING_AMOUNT_DISPLAY);
					SetDParam(2, CargoSpec::Get(order->GetConditionValue())->name);
					SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
					SetDParam(4, order->GetConditionValue());
					SetDParam(5, GB(order->GetXData(), 0, 16));
				} else {
					SetDParam(0, STR_ORDER_CONDITIONAL_CARGO_WAITING_AMOUNT_VIA_DISPLAY);
					SetDParam(2, CargoSpec::Get(order->GetConditionValue())->name);
					SetDParam(3, GB(order->GetXData(), 16, 16) - 2);
					SetDParam(4, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + order->GetConditionComparator());
					SetDParam(5, order->GetConditionValue());
					SetDParam(6, GB(order->GetXData(), 0, 16));
				}
			} else {
				OrderConditionComparator occ = order->GetConditionComparator();
				bool is_cargo = ocv == OCV_CARGO_ACCEPTANCE || ocv == OCV_CARGO_WAITING;
				SetDParam(0, is_cargo ? STR_ORDER_CONDITIONAL_CARGO : (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) ? STR_ORDER_CONDITIONAL_TRUE_FALSE : STR_ORDER_CONDITIONAL_NUM);
				SetDParam(2, (ocv == OCV_CARGO_ACCEPTANCE || ocv == OCV_CARGO_WAITING || ocv == OCV_FREE_PLATFORMS)
						? STR_ORDER_CONDITIONAL_NEXT_STATION : STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + ocv);

				uint value = order->GetConditionValue();
				switch (ocv) {
					case OCV_CARGO_ACCEPTANCE:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_ACCEPTS + occ - OCC_IS_TRUE);
						SetDParam(4, CargoSpec::Get( value )->name );
						break;
					case OCV_CARGO_WAITING:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_HAS + occ - OCC_IS_TRUE);
						SetDParam(4, CargoSpec::Get( value )->name );
						break;
					case OCV_REQUIRES_SERVICE:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + occ);
						break;
					case OCV_MAX_SPEED:
						value = ConvertSpeedToDisplaySpeed(value);
						/* FALL THROUGH */
					default:
						SetDParam(3, STR_ORDER_CONDITIONAL_COMPARATOR_EQUALS + occ);
						SetDParam(4, value);
				}
			}

			if (timetable && order->GetWaitTime() > 0) {
				SetDParam(7, order->IsWaitTimetabled() ? STR_TIMETABLE_AND_TRAVEL_FOR : STR_TIMETABLE_AND_TRAVEL_FOR_ESTIMATED);
				SetTimetableParams(8, order->GetWaitTime());
			} else {
				SetDParam(7, STR_EMPTY);
			}

			break;
		}

		default: NOT_REACHED();
	}

	int edge = DrawString(rtl ? left : middle, rtl ? middle : right, y, STR_ORDER_TEXT, colour);

	if (timetable && timetable_wait_time_valid && order->GetLeaveType() == OLT_LEAVE_EARLY && edge != 0) {
		edge = DrawString(rtl ? left : edge + 3, rtl ? edge - 3 : right, y, STR_TIMETABLE_LEAVE_EARLY_ORDER, colour);
	}
	if (timetable && HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->GetFirstWaitingLocation(false) == order_index && edge != 0) {
		StringID str = order->IsWaitTimetabled() ? STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER : STR_TIMETABLE_SCHEDULED_DISPATCH_ORDER_NO_WAIT_TIME;
		edge = DrawString(rtl ? left : edge + 3, rtl ? edge - 3 : right, y, str, colour);
	}

	if (timetable && timetable_wait_time_valid && order->IsWaitFixed() && edge != 0) {
		Dimension lock_d = GetSpriteSize(SPR_LOCK);
		DrawPixelInfo tmp_dpi;
		if (FillDrawPixelInfo(&tmp_dpi, rtl ? left : middle, y, rtl ? middle - left : right - middle, lock_d.height)) {
			DrawPixelInfo *old_dpi = _cur_dpi;
			_cur_dpi = &tmp_dpi;

			DrawSprite(SPR_LOCK, PAL_NONE, rtl ? edge - 3 - lock_d.width - left : edge + 3 - middle, 0);

			_cur_dpi = old_dpi;
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
		if (v->type == VEH_ROAD && ((GetRoadTypes(tile) & RoadVehicle::From(v)->compatible_roadtypes) == 0)) {
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
struct OrdersWindow : public Window {
private:
	/** Under what reason are we using the PlaceObject functionality? */
	enum OrderPlaceObjectState {
		OPOS_NONE,
		OPOS_GOTO,
		OPOS_CONDITIONAL,
		OPOS_SHARE,
		OPOS_COND_VIA,
		OPOS_END,
	};

	/** Displayed planes of the #NWID_SELECTION widgets. */
	enum DisplayPane {
		/* WID_O_SEL_TOP_ROW_GROUNDVEHICLE */
		DP_GROUNDVEHICLE_ROW_NORMAL      = 0, ///< Display the row for normal/depot orders in the top row of the train/rv order window.
		DP_GROUNDVEHICLE_ROW_CONDITIONAL = 1, ///< Display the row for conditional orders in the top row of the train/rv order window.

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

		/* WID_O_SEL_COND_VALUE */
		DP_COND_VALUE_NUMBER = 0, ///< Display number widget
		DP_COND_VALUE_CARGO  = 1, ///< Display dropdown widget cargo types
		DP_COND_VALUE_SLOT   = 2, ///< Display dropdown widget tracerestrict slots

		/* WID_O_SEL_COND_AUX */
		DP_COND_AUX_CARGO = 0, ///< Display dropdown widget cargo types

		/* WID_O_SEL_COND_AUX2 */
		DP_COND_AUX2_VIA = 0, ///< Display via button

		DP_ROW_CONDITIONAL = 2, ///< Display the conditional order buttons in the top row of the ship/airplane order window.

		/* WID_O_SEL_BOTTOM_MIDDLE */
		DP_BOTTOM_MIDDLE_DELETE       = 0, ///< Display 'delete' in the middle button of the bottom row of the vehicle order window.
		DP_BOTTOM_MIDDLE_STOP_SHARING = 1, ///< Display 'stop sharing' in the middle button of the bottom row of the vehicle order window.

		/* WID_O_SEL_SHARED */
		DP_SHARED_LIST       = 0, ///< Display shared order list button
		DP_SHARED_VEH_GROUP  = 1, ///< Display add veh to new group button
	};

	int selected_order;
	VehicleOrderID order_over;         ///< Order over which another order is dragged, \c INVALID_VEH_ORDER_ID if none.
	OrderPlaceObjectState goto_type;
	const Vehicle *vehicle; ///< Vehicle owning the orders being displayed and manipulated.
	Scrollbar *vscroll;
	bool can_do_refit;     ///< Vehicle chain can be refitted in depot.
	bool can_do_autorefit; ///< Vehicle chain can be auto-refitted.
	int query_text_widget; ///< widget which most recently called ShowQueryString
	int current_aux_plane;
	int current_aux2_plane;

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
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_O_ORDER_LIST);
		int sel = (y - nwid->pos_y - WD_FRAMERECT_TOP) / nwid->resize_y; // Selected line in the WID_O_ORDER_LIST panel.

		if ((uint)sel >= this->vscroll->GetCapacity()) return INVALID_VEH_ORDER_ID;

		sel += this->vscroll->GetPosition();

		return (sel <= vehicle->GetNumOrders() && sel >= 0) ? sel : INVALID_VEH_ORDER_ID;
	}

	/**
	 * Determine which strings should be displayed in the conditional comparator dropdown
	 *
	 * @param order the order to evaluate
	 * @return the StringIDs to display
	 */
	static const StringID *GetComparatorStrings(const Order *order)
	{
		if (order == nullptr) return _order_conditional_condition;
		switch (order->GetConditionVariable()) {
			case OCV_FREE_PLATFORMS:
			case OCV_CARGO_WAITING:
				return _order_conditional_condition_has;

			case OCV_CARGO_ACCEPTANCE:
				return _order_conditional_condition_accepts;

			case OCV_SLOT_OCCUPANCY:
				return _order_conditional_condition_is_fully_occupied;

			case OCV_TRAIN_IN_SLOT:
				return _order_conditional_condition_is_in_slot;

			default:
				return _order_conditional_condition;
		}
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
		};
		SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, goto_place_style[type - 1], this);
		this->goto_type = type;
		this->SetWidgetDirty(WID_O_GOTO);
		this->SetWidgetDirty(WID_O_COND_AUX_VIA);
	}

	/**
	 * Handle the click on the full load button.
	 * @param load_type the way to load.
	 */
	void OrderClick_FullLoad(int load_type)
	{
		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr || (order->GetLoadType() == load_type && load_type != OLFB_CARGO_TYPE_LOAD)) return;

		if (load_type < 0) {
			load_type = order->GetLoadType() == OLF_LOAD_IF_POSSIBLE ? OLF_FULL_LOAD_ANY : OLF_LOAD_IF_POSSIBLE;
		}
		if (order->GetLoadType() != load_type) {
			DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_LOAD | (load_type << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
		}

		if (load_type == OLFB_CARGO_TYPE_LOAD) ShowCargoTypeOrdersWindow(this->vehicle, this, sel_ord, CTOWV_LOAD);
	}

	/**
	 * Handle the 'no loading' hotkey
	 */
	void OrderHotkey_NoLoad()
	{
		this->OrderClick_FullLoad(OLFB_NO_LOAD);
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
		DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_DEPOT_ACTION | (i << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
	}

	/**
	 * Handle the click on the service in nearest depot button.
	 */
	void OrderClick_NearestDepot()
	{
		Order order;
		order.next = nullptr;
		order.index = 0;
		order.MakeGoToDepot(0, ODTFB_PART_OF_ORDERS,
				(_settings_client.gui.new_nonstop || _settings_game.order.nonstop_only) && this->vehicle->IsGroundVehicle() ? ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS : ONSF_STOP_EVERYWHERE);
		order.SetDepotActionType(ODATFB_NEAREST_DEPOT);

		DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), order.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_ERROR_CAN_T_INSERT_NEW_ORDER));
	}

	/**
	 * Handle the click on the unload button.
	 */
	void OrderClick_Unload(int unload_type)
	{
		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr || (order->GetUnloadType() == unload_type && unload_type != OUFB_CARGO_TYPE_UNLOAD)) return;

		if (unload_type < 0) {
			unload_type = order->GetUnloadType() == OUF_UNLOAD_IF_POSSIBLE ? OUFB_UNLOAD : OUF_UNLOAD_IF_POSSIBLE;
		}

		if (order->GetUnloadType() != unload_type) {
			DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_UNLOAD | (unload_type << 4), CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
		}

		if (unload_type == OUFB_TRANSFER) {
			/* Transfer orders with leave empty as default */
			DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_LOAD | (OLFB_NO_LOAD << 4), CMD_MODIFY_ORDER);
			this->SetWidgetDirty(WID_O_FULL_LOAD);
		} else if(unload_type == OUFB_CARGO_TYPE_UNLOAD) {
			ShowCargoTypeOrdersWindow(this->vehicle, this, sel_ord, CTOWV_UNLOAD);
		}
	}

	/**
	 * Handle the transfer hotkey
	 */
	void OrderHotkey_Transfer()
	{
		this->OrderClick_Unload(OUFB_TRANSFER);
	}

	/**
	 * Handle the 'no unload' hotkey
	 */
	void OrderHotkey_NoUnload()
	{
		this->OrderClick_Unload(OUFB_NO_UNLOAD);
	}

	/**
	 * Handle the click on the nonstop button.
	 * @param non_stop what non-stop type to use; -1 to use the 'next' one.
	 */
	void OrderClick_Nonstop(int non_stop)
	{
		if (!this->vehicle->IsGroundVehicle()) return;

		VehicleOrderID sel_ord = this->OrderGetSel();
		const Order *order = this->vehicle->GetOrder(sel_ord);

		if (order == nullptr || order->GetNonStopType() == non_stop) return;

		/* Keypress if negative, so 'toggle' to the next */
		if (non_stop < 0) {
			non_stop = order->GetNonStopType() ^ ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS;
		}

		this->SetWidgetDirty(WID_O_NON_STOP);
		DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_NON_STOP | non_stop << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
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

	/** Cache auto-refittability of the vehicle chain. */
	void UpdateAutoRefitState()
	{
		this->can_do_refit = false;
		this->can_do_autorefit = false;
		for (const Vehicle *w = this->vehicle; w != nullptr; w = w->IsGroundVehicle() ? w->Next() : nullptr) {
			if (IsEngineRefittable(w->engine_type)) this->can_do_refit = true;
			if (HasBit(Engine::Get(w->engine_type)->info.misc_flags, EF_AUTO_REFIT)) this->can_do_autorefit = true;
		}
	}

public:
	OrdersWindow(WindowDesc *desc, const Vehicle *v) : Window(desc)
	{
		this->vehicle = v;

		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_O_SCROLLBAR);
		if (v->owner == _local_company) {
			this->GetWidget<NWidgetStacked>(WID_O_SEL_OCCUPANCY)->SetDisplayedPlane(SZSP_NONE);
			this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX)->SetDisplayedPlane(SZSP_NONE);
			this->GetWidget<NWidgetStacked>(WID_O_SEL_COND_AUX2)->SetDisplayedPlane(SZSP_NONE);
		}
		this->current_aux_plane = SZSP_NONE;
		this->current_aux2_plane = SZSP_NONE;
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
			const Order *order;
			FOR_VEHICLE_ORDERS(v, order) {
				if (order->IsType(OT_GOTO_STATION)) station_orders++;
			}

			if (station_orders < 2) this->OrderClick_Goto(OPOS_GOTO);
		}
		this->OnInvalidateData(VIWD_MODIFY_ORDERS);
	}

	~OrdersWindow()
	{
		DeleteWindowById(WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, this->window_number, false);
		DeleteWindowById(WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, this->window_number, false);
		if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_O_OCCUPANCY_LIST:
				SetDParamMaxValue(0, 100);
				size->width = WD_FRAMERECT_LEFT + GetStringBoundingBox(STR_ORDERS_OCCUPANCY_PERCENT).width + 10 + WD_FRAMERECT_RIGHT;
				/* FALL THROUGH */

			case WID_O_SEL_OCCUPANCY:
			case WID_O_ORDER_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = 6 * resize->height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;

			case WID_O_COND_VARIABLE: {
				Dimension d = {0, 0};
				for (uint i = 0; i < lengthof(_order_conditional_variable); i++) {
					if (this->vehicle->type != VEH_TRAIN && (_order_conditional_variable[i] == OCV_TRAIN_IN_SLOT || _order_conditional_variable[i] == OCV_SLOT_OCCUPANCY || _order_conditional_variable[i] == OCV_FREE_PLATFORMS)) continue;
					d = maxdim(d, GetStringBoundingBox(STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + _order_conditional_variable[i]));
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
				size->width = WD_FRAMERECT_LEFT + GetStringBoundingBox(STR_ORDERS_OCCUPANCY_PERCENT).width + 10 + WD_FRAMERECT_RIGHT;
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

				this->DeleteChildWindows();
				HideDropDownMenu(this);
				this->selected_order = -1;
				break;

			case VIWD_MODIFY_ORDERS:
				/* Some other order changes */
				break;

			default:
				if (data < 0) break;

				if (gui_scope) break; // only do this once; from command scope
				from = GB(data, 0, 8);
				to   = GB(data, 8, 8);
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
					this->DeleteChildWindows();
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

	virtual EventState OnCTRLStateChange() OVERRIDE
	{
		this->UpdateButtonState();
		return ES_NOT_HANDLED;
	}

	void UpdateButtonState()
	{
		if (this->vehicle->owner != _local_company) return; // No buttons are displayed with competitor order windows.

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

		auto aux_plane_guard = scope_guard([&]() {
			if (this->current_aux_plane != aux_sel->shown_plane) {
				this->current_aux_plane = aux_sel->shown_plane;
				this->ReInit();
			}
			if (this->current_aux2_plane != aux2_sel->shown_plane) {
				this->current_aux2_plane = aux2_sel->shown_plane;
				this->ReInit();
			}
		});

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
		} else {
			this->SetWidgetDisabledState(WID_O_FULL_LOAD, (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // full load
			this->SetWidgetDisabledState(WID_O_UNLOAD,    (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0); // unload

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
					bool is_slot_occupancy = (ocv == OCV_SLOT_OCCUPANCY || ocv == OCV_TRAIN_IN_SLOT);
					bool is_auxiliary_cargo = (ocv == OCV_CARGO_LOAD_PERCENTAGE || ocv == OCV_CARGO_WAITING_AMOUNT);

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
					} else {
						aux_sel->SetDisplayedPlane(SZSP_NONE);
					}

					if (ocv == OCV_CARGO_WAITING_AMOUNT) {
						aux2_sel->SetDisplayedPlane(DP_COND_AUX2_VIA);
					} else {
						aux2_sel->SetDisplayedPlane(SZSP_NONE);
					}

					/* Set the strings for the dropdown boxes. */
					this->GetWidget<NWidgetCore>(WID_O_COND_VARIABLE)->widget_data   = STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + ocv;
					this->GetWidget<NWidgetCore>(WID_O_COND_COMPARATOR)->widget_data = GetComparatorStrings(order)[order->GetConditionComparator()];
					this->SetWidgetDisabledState(WID_O_COND_COMPARATOR, ocv == OCV_UNCONDITIONALLY || ocv == OCV_PERCENT);
					this->SetWidgetDisabledState(WID_O_COND_VALUE, ocv == OCV_REQUIRES_SERVICE || ocv == OCV_UNCONDITIONALLY);
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
			this->SetWidgetLoweredState(WID_O_GOTO, this->goto_type != OPOS_NONE && this->goto_type != OPOS_COND_VIA);
			this->SetWidgetLoweredState(WID_O_COND_AUX_VIA, this->goto_type == OPOS_COND_VIA);
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
		}
	}

	void DrawOrderListWidget(const Rect &r) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		SetDParamMaxValue(0, this->vehicle->GetNumOrders(), 2);
		int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + 3;
		int middle = rtl ? r.right - WD_FRAMETEXT_RIGHT - index_column_width : r.left + WD_FRAMETEXT_LEFT + index_column_width;

		int y = r.top + WD_FRAMERECT_TOP;
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
					int top = (this->order_over < this->selected_order ? y : y + line_height) - WD_FRAMERECT_TOP;
					int bottom = min(top + 2, r.bottom - WD_FRAMERECT_BOTTOM);
					top = max(top - 3, r.top + WD_FRAMERECT_TOP);
					GfxFillRect(r.left + WD_FRAMETEXT_LEFT, top, r.right - WD_FRAMETEXT_RIGHT, bottom, _colour_gradient[COLOUR_GREY][7]);
					break;
				}
				y += line_height;

				i++;
				order = order->next;
			}

			/* Reset counters for drawing the orders. */
			y = r.top + WD_FRAMERECT_TOP;
			i = this->vscroll->GetPosition();
			order = this->vehicle->GetOrder(i);
		}

		/* Draw the orders. */
		while (order != nullptr) {
			/* Don't draw anything if it extends past the end of the window. */
			if (!this->vscroll->IsVisible(i)) break;

			DrawOrderString(this->vehicle, order, i, y, i == this->selected_order, false, r.left + WD_FRAMETEXT_LEFT, middle, r.right - WD_FRAMETEXT_RIGHT);
			y += line_height;

			i++;
			order = order->next;
		}

		if (this->vscroll->IsVisible(i)) {
			StringID str = this->vehicle->IsOrderListShared() ? STR_ORDERS_END_OF_SHARED_ORDERS : STR_ORDERS_END_OF_ORDERS;
			DrawString(rtl ? r.left + WD_FRAMETEXT_LEFT : middle, rtl ? middle : r.right - WD_FRAMETEXT_RIGHT, y, str, (i == this->selected_order) ? TC_WHITE : TC_BLACK);
		}
	}

	void DrawOccupancyListWidget(const Rect &r) const
	{
		int y = r.top + WD_FRAMERECT_TOP;
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
				DrawString(r.left + WD_FRAMETEXT_LEFT, r.right - WD_FRAMETEXT_RIGHT, y, STR_ORDERS_OCCUPANCY_PERCENT, (i == this->selected_order) ? TC_WHITE : TC_BLACK);
			}
			y += line_height;

			i++;
			order = order->next;
		}
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
							value = order->GetXData();
							break;

						case OCV_CARGO_WAITING_AMOUNT:
							value = GB(order->GetXData(), 0, 16);
							break;

						default:
							value = order->GetConditionValue();
							break;
					}
					if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value);
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

						DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), order.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_ERROR_CAN_T_INSERT_NEW_ORDER));
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
				this->DeleteChildWindows();
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
						DoCommandP(this->vehicle->tile, this->vehicle->index + (sel << 20),
								MOF_STOP_LOCATION | osl << 4,
								CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
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
					if (this->goto_type == OPOS_COND_VIA) ResetObjectToPlace();
					int sel;
					switch (this->goto_type) {
						case OPOS_NONE:        sel = -1; break;
						case OPOS_GOTO:        sel =  0; break;
						case OPOS_CONDITIONAL: sel =  2; break;
						case OPOS_SHARE:       sel =  3; break;
						default: NOT_REACHED();
					}
					ShowDropDownMenu(this, this->vehicle->type == VEH_AIRCRAFT ? _order_goto_dropdown_aircraft : _order_goto_dropdown, sel, WID_O_GOTO, 0, 0, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_FULL_LOAD:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_FullLoad(-1);
				} else {
					ShowDropDownMenu(this, _order_full_load_drowdown, this->vehicle->GetOrder(this->OrderGetSel())->GetLoadType(), WID_O_FULL_LOAD, 0, 0xE2 /* 1110 0010 */, 0, DDSF_LOST_FOCUS);
				}
				break;

			case WID_O_UNLOAD:
				if (this->GetWidget<NWidgetLeaf>(widget)->ButtonHit(pt)) {
					this->OrderClick_Unload(-1);
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
				TraceRestrictSlotID value = this->vehicle->GetOrder(this->OrderGetSel())->GetXData();
				DropDownList list = GetSlotDropDownList(this->vehicle->owner, value, selected);
				if (!list.empty()) ShowDropDownList(this, std::move(list), selected, WID_O_COND_SLOT, 0, true);
				break;
			}

			case WID_O_REVERSE: {
				VehicleOrderID sel_ord = this->OrderGetSel();
				const Order *order = this->vehicle->GetOrder(sel_ord);

				if (order == nullptr) break;

				DoCommandP(this->vehicle->tile, this->vehicle->index + (sel_ord << 20), MOF_WAYPOINT_FLAGS | (order->GetWaypointFlags() ^ OWF_REVERSE) << 4,
						CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				break;
			}

			case WID_O_COND_CARGO:
			case WID_O_COND_AUX_CARGO: {
				uint value = this->vehicle->GetOrder(this->OrderGetSel())->GetConditionValue();
				DropDownList list;
				for (size_t i = 0; i < _sorted_standard_cargo_specs_size; ++i) {
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
					DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VALUE_3 | NEW_STATION << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				} else {
					this->OrderClick_Goto(OPOS_COND_VIA);
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
					if (this->vehicle->type != VEH_TRAIN && (_order_conditional_variable[i] == OCV_TRAIN_IN_SLOT || _order_conditional_variable[i] == OCV_SLOT_OCCUPANCY || _order_conditional_variable[i] == OCV_FREE_PLATFORMS)) continue;
					if (ocv != _order_conditional_variable[i]) {
						if ((_order_conditional_variable[i] == OCV_TRAIN_IN_SLOT || _order_conditional_variable[i] == OCV_SLOT_OCCUPANCY) && !_settings_client.gui.show_adv_tracerestrict_features) continue;
					}
					list.emplace_back(new DropDownListStringItem(STR_ORDER_CONDITIONAL_LOAD_PERCENTAGE + _order_conditional_variable[i], _order_conditional_variable[i], false));
				}
				ShowDropDownList(this, std::move(list), ocv, WID_O_COND_VARIABLE);
				break;
			}

			case WID_O_COND_COMPARATOR: {
				const Order *o = this->vehicle->GetOrder(this->OrderGetSel());
				uint mask;
				switch (o->GetConditionVariable()) {
					case OCV_REQUIRES_SERVICE:
					case OCV_CARGO_ACCEPTANCE:
					case OCV_CARGO_WAITING:
					case OCV_SLOT_OCCUPANCY:
						mask = 0x3F;
						break;

					case OCV_TRAIN_IN_SLOT:
						mask = 0x3C;
						break;

					default:
						mask = 0xC0;
						break;
				}
				ShowDropDownMenu(this, GetComparatorStrings(o), o->GetConditionComparator(), WID_O_COND_COMPARATOR, 0, mask, 0, DDSF_LOST_FOCUS);
				break;
			}

			case WID_O_COND_VALUE: {
				const Order *order = this->vehicle->GetOrder(this->OrderGetSel());
				uint value;
				switch (order->GetConditionVariable()) {
					case OCV_CARGO_LOAD_PERCENTAGE:
						value = order->GetXData();
						break;

					case OCV_CARGO_WAITING_AMOUNT:
						value = GB(order->GetXData(), 0, 16);
						break;

					default:
						value = order->GetConditionValue();
						break;
				}
				if (order->GetConditionVariable() == OCV_MAX_SPEED) value = ConvertSpeedToDisplaySpeed(value);
				if (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) value = ConvertCargoQuantityToDisplayQuantity(order->GetConditionValue(), value);
				this->query_text_widget = widget;
				SetDParam(0, value);
				ShowQueryString(STR_JUST_INT, STR_ORDER_CONDITIONAL_VALUE_CAPT, (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) ? 12 : 6, this, CS_NUMERAL, QSF_NONE);
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
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (this->query_text_widget == WID_O_COND_VALUE && !StrEmpty(str)) {
			VehicleOrderID sel = this->OrderGetSel();
			uint value = atoi(str);

			switch (this->vehicle->GetOrder(sel)->GetConditionVariable()) {
				case OCV_MAX_SPEED:
					value = ConvertDisplaySpeedToSpeed(value);
					break;

				case OCV_PERCENT:
				case OCV_RELIABILITY:
				case OCV_LOAD_PERCENTAGE:
				case OCV_CARGO_LOAD_PERCENTAGE:
					value = Clamp(value, 0, 100);
					break;

				case OCV_CARGO_WAITING_AMOUNT:
					value = ConvertDisplayQuantityToCargoQuantity(this->vehicle->GetOrder(sel)->GetConditionValue(), value);
					break;

				default:
					break;
			}
			DoCommandP(this->vehicle->tile, this->vehicle->index + (sel << 20), MOF_COND_VALUE | Clamp(value, 0, 2047) << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
		}

		if (this->query_text_widget == WID_O_ADD_VEH_GROUP) {
			DoCommandP(0, VehicleListIdentifier(VL_SINGLE_VEH, this->vehicle->type, this->vehicle->owner, this->vehicle->index).Pack(), 0, CMD_CREATE_GROUP_FROM_LIST | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), nullptr, str);
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_O_NON_STOP:
				this->OrderClick_Nonstop(index);
				break;

			case WID_O_FULL_LOAD:
				this->OrderClick_FullLoad(index);
				break;

			case WID_O_UNLOAD:
				this->OrderClick_Unload(index);
				break;

			case WID_O_GOTO:
				switch (index) {
					case 0: this->OrderClick_Goto(OPOS_GOTO); break;
					case 1: this->OrderClick_NearestDepot(); break;
					case 2: this->OrderClick_Goto(OPOS_CONDITIONAL); break;
					case 3: this->OrderClick_Goto(OPOS_SHARE); break;
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
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VARIABLE | index << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				break;

			case WID_O_COND_COMPARATOR:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_COMPARATOR | index << 4,  CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				break;

			case WID_O_COND_CARGO:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VALUE | index << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				break;

			case WID_O_COND_AUX_CARGO:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VALUE_2 | index << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
				break;

			case WID_O_COND_SLOT:
				DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VALUE | index << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER));
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
			case OHK_SKIP:           this->OrderClick_Skip();          break;
			case OHK_DELETE:         this->OrderClick_Delete();        break;
			case OHK_GOTO:           this->OrderClick_Goto(OPOS_GOTO); break;
			case OHK_NONSTOP:        this->OrderClick_Nonstop(-1);     break;
			case OHK_FULLLOAD:       this->OrderClick_FullLoad(-1);    break;
			case OHK_UNLOAD:         this->OrderClick_Unload(-1);      break;
			case OHK_NEAREST_DEPOT:  this->OrderClick_NearestDepot();  break;
			case OHK_ALWAYS_SERVICE: this->OrderClick_Service(-1);     break;
			case OHK_TRANSFER:       this->OrderHotkey_Transfer();     break;
			case OHK_NO_UNLOAD:      this->OrderHotkey_NoUnload();     break;
			case OHK_NO_LOAD:        this->OrderHotkey_NoLoad();       break;
			default: return ES_NOT_HANDLED;
		}
		return ES_HANDLED;
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		if (this->goto_type == OPOS_GOTO) {
			const Order cmd = GetOrderCmdFromTile(this->vehicle, tile);
			if (cmd.IsType(OT_NOTHING)) return;

			if (DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), cmd.Pack(), CMD_INSERT_ORDER | CMD_MSG(STR_ERROR_CAN_T_INSERT_NEW_ORDER))) {
				/* With quick goto the Go To button stays active */
				if (!_settings_client.gui.quick_goto) ResetObjectToPlace();
			}
		} else if (this->goto_type == OPOS_COND_VIA) {
			if (IsTileType(tile, MP_STATION) || IsTileType(tile, MP_INDUSTRY)) {
				const Station *st = nullptr;

				if (IsTileType(tile, MP_STATION)) {
					st = Station::GetByTile(tile);
				} else {
					const Industry *in = Industry::GetByTile(tile);
					st = in->neutral_station;
				}
				if (st != nullptr && IsInfraUsageAllowed(this->vehicle->type, this->vehicle->owner, st->owner)) {
					if (DoCommandP(this->vehicle->tile, this->vehicle->index + (this->OrderGetSel() << 20), MOF_COND_VALUE_3 | st->index << 4, CMD_MODIFY_ORDER | CMD_MSG(STR_ERROR_CAN_T_MODIFY_THIS_ORDER))) {
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

	void OnPlaceObjectAbort() override
	{
		this->goto_type = OPOS_NONE;
		this->SetWidgetDirty(WID_O_GOTO);
		this->SetWidgetDirty(WID_O_COND_AUX_VIA);

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

	virtual void OnFocus(Window *previously_focused_window) OVERRIDE
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	virtual void OnFocusLost(Window *newly_focused_window) OVERRIDE
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
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
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(STR_ORDERS_TIMETABLE_VIEW, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
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
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX2),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_VIA), SetMinimalSize(36, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_VIA, STR_ORDER_CONDITIONAL_VIA_TOOLTIP),
				EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_VALUE),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COND_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_BLACK_COMMA, STR_ORDER_CONDITIONAL_VALUE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SLOT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
			EndContainer(),
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
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_SKIP), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_SKIP_BUTTON, STR_ORDERS_SKIP_TOOLTIP), SetResize(1, 0),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_BOTTOM_MIDDLE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_DELETE), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_TOOLTIP), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_STOP_SHARING), SetMinimalSize(124, 12), SetFill(1, 0),
														SetDataTip(STR_ORDERS_STOP_SHARING_BUTTON, STR_ORDERS_STOP_SHARING_TOOLTIP), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_GOTO), SetMinimalSize(124, 12), SetFill(1, 0),
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
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(STR_ORDERS_TIMETABLE_VIEW, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
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
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_AUX2),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_COND_AUX_VIA), SetMinimalSize(36, 12),
													SetDataTip(STR_ORDER_CONDITIONAL_VIA, STR_ORDER_CONDITIONAL_VIA_TOOLTIP),
				EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_COMPARATOR), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_COMPARATOR_TOOLTIP), SetResize(1, 0),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_COND_VALUE),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_COND_VALUE), SetMinimalSize(124, 12), SetFill(1, 0),
															SetDataTip(STR_BLACK_COMMA, STR_ORDER_CONDITIONAL_VALUE_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_CARGO), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_CARGO_TOOLTIP), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_O_COND_SLOT), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_NULL, STR_ORDER_CONDITIONAL_SLOT_TOOLTIP), SetResize(1, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),

		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_O_OCCUPANCY_TOGGLE), SetMinimalSize(36, 12), SetDataTip(STR_ORDERS_OCCUPANCY_BUTTON, STR_ORDERS_OCCUPANCY_BUTTON_TOOLTIP),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_SHARED),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_O_SHARED_ORDER_LIST), SetMinimalSize(12, 12), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_ADD_VEH_GROUP), SetMinimalSize(12, 12), SetDataTip(STR_BLACK_PLUS, STR_ORDERS_NEW_GROUP_TOOLTIP),
		EndContainer(),
	EndContainer(),

	/* Second button row. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_SKIP), SetMinimalSize(124, 12), SetFill(1, 0),
											SetDataTip(STR_ORDERS_SKIP_BUTTON, STR_ORDERS_SKIP_TOOLTIP), SetResize(1, 0),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_O_SEL_BOTTOM_MIDDLE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_DELETE), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_DELETE_BUTTON, STR_ORDERS_DELETE_TOOLTIP), SetResize(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_STOP_SHARING), SetMinimalSize(124, 12), SetFill(1, 0),
													SetDataTip(STR_ORDERS_STOP_SHARING_BUTTON, STR_ORDERS_STOP_SHARING_TOOLTIP), SetResize(1, 0),
		EndContainer(),
		NWidget(NWID_BUTTON_DROPDOWN, COLOUR_GREY, WID_O_GOTO), SetMinimalSize(124, 12), SetFill(1, 0),
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
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_O_TIMETABLE_VIEW), SetMinimalSize(61, 14), SetDataTip(STR_ORDERS_TIMETABLE_VIEW, STR_ORDERS_TIMETABLE_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_O_ORDER_LIST), SetMinimalSize(372, 72), SetDataTip(0x0, STR_ORDERS_LIST_TOOLTIP), SetResize(1, 1), SetScrollbar(WID_O_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_O_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
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
	DeleteWindowById(WC_VEHICLE_DETAILS, v->index, false);
	DeleteWindowById(WC_VEHICLE_TIMETABLE, v->index, false);
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
