/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_serialisation_gui.h GUI for order serialisation and deserialisation to/from JSON. */

#include "stdafx.h"
#include "order_func.h"
#include "order_serialisation.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "widget_type.h"
#include "window_func.h"
#include "window_gui.h"
#include "core/backup_type.hpp"
#include "core/format.hpp"
#include "core/geometry_func.hpp"
#include "table/sprites.h"

#include "safeguards.h"

/** Widgets of the #OrderListImportErrorsWindow class. */
enum OrderWidgets : WidgetID {
	WID_OIE_CAPTION,                   ///< Caption of the window.
	WID_OIE_ORDER_LIST,                ///< Order list panel.
	WID_OIE_SCROLLBAR,                 ///< Order list scrollbar.
	WID_OIE_TOGGLE_NON_ERROR,          ///< Whether to show non-error orders.
	WID_OIE_TOGGLE_NON_ERROR_SEL,      ///< Selection for WID_OIE_TOGGLE_NON_ERROR_SEL.
};

/** Nested widget definition for order import errors. */
static constexpr NWidgetPart _nested_order_import_error_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_OIE_CAPTION), SetStringTip(STR_ORDER_IMPORT_ERROR_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_OIE_TOGGLE_NON_ERROR_SEL),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_OIE_TOGGLE_NON_ERROR), SetSpriteTip(SPR_LARGE_SMALL_WINDOW, STR_ORDER_IMPORT_ERROR_LIST_TOGGLE_SHOW_NON_ERRORS), SetAspect(WidgetDimensions::ASPECT_TOGGLE_SIZE),
		EndContainer(),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_OIE_ORDER_LIST), SetMinimalSize(372, 72), SetToolTip(STR_ORDERS_LIST_TOOLTIP), SetResize(1, 1), SetScrollbar(WID_OIE_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_OIE_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _order_list_import_errors_desc(__FILE__, __LINE__,
	WDP_AUTO, "view_vehicle_order_import_errors", 384, 100,
	WC_VEHICLE_ORDER_IMPORT_ERRORS, WC_VEHICLE_VIEW,
	WindowDefaultFlag::Construction,
	_nested_order_import_error_widgets
);

struct OrderListImportErrorsWindow : GeneralVehicleWindow
{
	const OrderImportErrors errs;
	Scrollbar *vscroll;
	bool show_non_error_order = false;
	OrderList saved_orders{};
	uint32_t saved_vehicle_flags{};

	OrderListImportErrorsWindow(const Vehicle *v, OrderImportErrors errs) : GeneralVehicleWindow(_order_list_import_errors_desc, v), errs(std::move(errs))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_OIE_SCROLLBAR);
		this->SaveOrders();
		this->vscroll->SetCount(this->CountRows());
		this->GetWidget<NWidgetStacked>(WID_OIE_TOGGLE_NON_ERROR_SEL)->SetDisplayedPlane(this->errs.order.empty() ? SZSP_NONE : 0);
		this->FinishInitNested(v->index);

		this->owner = v->owner;
	}

	void SaveOrders()
	{
		this->saved_vehicle_flags = this->vehicle->vehicle_flags;
		if (this->vehicle->orders != nullptr) {
			this->saved_orders = *this->vehicle->orders;
		}
	}

	size_t CountRows() const
	{
		size_t count = this->errs.global.size() + this->errs.global.size();
		if (this->errs.global.size() > 0) count++;

		for (const auto &it : this->errs.schedule) {
			count += 1 + it.second.size();
		}
		if (this->errs.schedule.size() > 0) count++;

		const VehicleOrderID order_count = this->saved_orders.GetNumOrders();
		if (this->show_non_error_order) count += order_count;
		for (const auto &it : this->errs.order) {
			if (it.first < order_count) {
				count += it.second.size();
				if (!this->show_non_error_order) count++;
			}
		}
		if (this->errs.order.size() > 0) count++;

		return count;
	}

	void DrawOrderListErrorsWidget(const Rect &r) const
	{
		const bool rtl = _current_text_dir == TD_RTL;
		Rect draw_ir = r.Shrink(WidgetDimensions::scaled.frametext, WidgetDimensions::scaled.framerect);
		Rect highlight_ir = draw_ir.Indent(WidgetDimensions::scaled.hsep_normal / 2, rtl).WithWidth(WidgetDimensions::scaled.hsep_normal, rtl);
		Rect ir = draw_ir.Indent(WidgetDimensions::scaled.hsep_normal * 2, rtl);

		SetDParamMaxValue(0, this->vehicle->GetNumOrders(), 2);
		int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + WidgetDimensions::scaled.hsep_normal;
		int middle = rtl ? ir.right - index_column_width : ir.left + index_column_width;

		int y = ir.top;
		const uint line_height = this->GetWidget<NWidgetBase>(WID_OIE_ORDER_LIST)->resize_y;

		int current_row = 0;
		VehicleOrderID order_index = 0;

		auto CheckVisibleAndIncrementRow = [&]() -> bool {
			bool res = this->vscroll->IsVisible(current_row);
			current_row++;
			return res;
		};

		auto DrawRawString = [&](std::string_view str, TextColour color = TC_BLACK, bool indented = false) -> int {
			int val;
			if (indented) {
				val = DrawString(rtl ? left : middle, rtl ? middle : ir.right, y, str, color);
			} else {
				val = DrawString(ir.left, ir.right, y, str, color);
			}
			y += line_height;
			return val;
		};

		auto DrawHighlight = [&](Colours c, ColourShade shade) -> void {
			GfxFillRect(highlight_ir.left, y, highlight_ir.right, y + line_height, GetColourGradient(c, shade));
		};

		auto DrawSectionTitle = [&](std::string_view str, TextColour color = TC_BLACK) -> void {
			if (!CheckVisibleAndIncrementRow()) return;
			int middle_height = y + line_height / 2;

			int offset = ir.right - DrawString(ir.left, ir.right, y, str, color, SA_CENTER);

			GfxFillRect(ir.left, middle_height - 1, ir.left + offset, middle_height + 1, GetColourGradient(COLOUR_BLUE, SHADE_DARK));
			GfxFillRect(ir.right - offset, middle_height - 1, ir.right, middle_height + 1, GetColourGradient(COLOUR_BLUE, SHADE_DARK));

			DrawString(ir.left, ir.right, y, str, color, SA_CENTER);

			y += line_height;
		};

		auto GetTColorFromError = [&](JsonOrderImportErrorType etype) -> TextColour {
			switch (OrderErrorTypeToColour(etype)) {
				case COLOUR_RED: return TC_RED;
				case COLOUR_ORANGE: return TC_ORANGE;
				case COLOUR_CREAM: return TC_CREAM;
				default: return TC_BLACK;
			}
		};

		if (this->errs.global.size() > 0) {
			DrawSectionTitle("[Global Errors]");

			for (const OrderImportErrors::Error &err : this->errs.global) {
				if (CheckVisibleAndIncrementRow()) {
					DrawRawString(err.msg, GetTColorFromError(err.type));
				}
			}
		}

		if (this->errs.schedule.size() > 0) {
			DrawSectionTitle("[Dispatch Errors]");

			for (const auto &[key, value] : this->errs.schedule) {
				if (CheckVisibleAndIncrementRow()) {
					DrawRawString(fmt::format("Schedule {} :", key));
				}
				for (const auto &err : value) {
					if (CheckVisibleAndIncrementRow()) {
						DrawRawString(err.msg, GetTColorFromError(err.type), true);
					}
				}
			}
		}

		if (this->errs.order.size() > 0) {
			DrawSectionTitle("[Order Errors]");

			AutoRestoreBackup(const_cast<Vehicle *>(this->vehicle)->vehicle_flags, this->saved_vehicle_flags);
			AutoRestoreBackup(const_cast<Vehicle *>(this->vehicle)->orders, const_cast<OrderList *>(&this->saved_orders));
			AutoRestoreBackup(const_cast<Vehicle *>(this->vehicle)->cur_real_order_index, INVALID_VEH_ORDER_ID);

			for (const Order *order : this->saved_orders.Orders()) {
				const bool order_has_errors = this->errs.order.contains(order_index);
				if (!this->show_non_error_order && !order_has_errors) {
					order_index++;
					continue;
				}

				if (CheckVisibleAndIncrementRow()) {
					if (order_has_errors && this->show_non_error_order) DrawHighlight(COLOUR_RED, SHADE_NORMAL);
					DrawOrderString(this->vehicle, order, order_index, y, false, false, ir.left, middle, ir.right);
					y += line_height;
				}

				if (order_has_errors) {
					const std::vector<OrderImportErrors::Error> &errors = this->errs.order.at(order_index);
					for (const OrderImportErrors::Error &e : errors) {
						if (CheckVisibleAndIncrementRow()) {
							if (this->show_non_error_order) DrawHighlight(COLOUR_RED, SHADE_NORMAL);
							DrawRawString(e.msg, GetTColorFromError(e.type), true);
						}
					}
				}

				order_index++;
			}
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_OIE_ORDER_LIST:
				DrawOrderListErrorsWidget(r);
				break;
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_OIE_ORDER_LIST, WidgetDimensions::scaled.framerect.Vertical());
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_OIE_ORDER_LIST:
				resize.height = GetCharacterHeight(FS_NORMAL);
				size.height = 6 * resize.height + padding.height;
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
		if (gui_scope) {
			this->SaveOrders();
			this->vscroll->SetCount(this->CountRows());
			this->SetDirty();
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget == WID_OIE_TOGGLE_NON_ERROR) {
			this->show_non_error_order = !this->show_non_error_order;
			this->vscroll->SetCount(this->CountRows());
			this->SetDirty();
		}
	}
};

void ShowOrderListImportErrorsWindow(const Vehicle *v, const OrderImportErrors errors)
{
	CloseWindowById(WC_VEHICLE_ORDER_IMPORT_ERRORS, v->index);
	new OrderListImportErrorsWindow(v, errors);
}
