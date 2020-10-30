/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timetable_gui.cpp GUI for time tabling. */

#include "stdafx.h"
#include "command_func.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "string_func.h"
#include "gfx_func.h"
#include "company_func.h"
#include "date_func.h"
#include "date_gui.h"
#include "vehicle_gui.h"
#include "settings_type.h"
#include "viewport_func.h"
#include "schdispatch.h"
#include "vehiclelist.h"

#include "widgets/timetable_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

/** Container for the arrival/departure dates of a vehicle */
struct TimetableArrivalDeparture {
	Ticks arrival;   ///< The arrival time
	Ticks departure; ///< The departure time
};

/**
 * Set the timetable parameters in the format as described by the setting.
 * @param param the first DParam to fill
 * @param ticks  the number of ticks to 'draw'
 */
void SetTimetableParams(int first_param, Ticks ticks)
{
	if (_settings_client.gui.timetable_in_ticks) {
		SetDParam(first_param, STR_TIMETABLE_TICKS);
		SetDParam(first_param + 1, ticks);
	} else {
		StringID str = _settings_time.time_in_minutes ? STR_TIMETABLE_MINUTES : STR_TIMETABLE_DAYS;
		size_t ratio = DATE_UNIT_SIZE;
		size_t units = ticks / ratio;
		size_t leftover = ticks % ratio;
		if (leftover && _settings_client.gui.timetable_leftover_ticks) {
			SetDParam(first_param, STR_TIMETABLE_LEFTOVER_TICKS);
			SetDParam(first_param + 1, str);
			SetDParam(first_param + 2, units);
			SetDParam(first_param + 3, leftover);
		} else {
			SetDParam(first_param, str);
			SetDParam(first_param + 1, units);
		}
	}
}

/**
 * Check whether it is possible to determine how long the order takes.
 * @param order the order to check.
 * @param travelling whether we are interested in the travel or the wait part.
 * @return true if the travel/wait time can be used.
 */
static bool CanDetermineTimeTaken(const Order *order, bool travelling)
{
	/* Current order is conditional */
	if (order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT)) return false;
	/* No travel time and we have not already finished travelling */
	if (travelling && !order->IsTravelTimetabled()) return false;
	/* No wait time but we are loading at this timetabled station */
	if (!travelling && !order->IsWaitTimetabled() && order->IsType(OT_GOTO_STATION) &&
			!(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
		return false;
	}

	return true;
}


/**
 * Fill the table with arrivals and departures
 * @param v Vehicle which must have at least 2 orders.
 * @param start order index to start at
 * @param travelling Are we still in the travelling part of the start order
 * @param table Fill in arrival and departures including intermediate orders
 * @param offset Add this value to result and all arrivals and departures
 */
static void FillTimetableArrivalDepartureTable(const Vehicle *v, VehicleOrderID start, bool travelling, TimetableArrivalDeparture *table, Ticks offset)
{
	assert(table != nullptr);
	assert(v->GetNumOrders() >= 2);
	assert(start < v->GetNumOrders());

	Ticks sum = offset;
	VehicleOrderID i = start;
	const Order *order = v->GetOrder(i);

	/* Pre-initialize with unknown time */
	for (int i = 0; i < v->GetNumOrders(); ++i) {
		table[i].arrival = table[i].departure = INVALID_TICKS;
	}

	VehicleOrderID scheduled_dispatch_order = INVALID_VEH_ORDER_ID;
	if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) scheduled_dispatch_order = v->GetFirstWaitingLocation(true);

	/* Cyclically loop over all orders until we reach the current one again.
	 * As we may start at the current order, do a post-checking loop */
	do {
		/* Automatic orders don't influence the overall timetable;
		 * they just add some untimetabled entries, but the time till
		 * the next non-implicit order can still be known. */
		if (!order->IsType(OT_IMPLICIT)) {
			if (travelling || i != start) {
				if (!CanDetermineTimeTaken(order, true)) return;
				sum += order->GetTimetabledTravel();
				table[i].arrival = sum;
			}

			if (i == scheduled_dispatch_order && !(i == start && !travelling)) return;
			if (!CanDetermineTimeTaken(order, false)) return;
			sum += order->GetTimetabledWait();
			table[i].departure = sum;
		}

		++i;
		order = order->next;
		if (i >= v->GetNumOrders()) {
			i = 0;
			assert(order == nullptr);
			order = v->orders.list->GetFirstOrder();
		}
	} while (i != start);

	/* When loading at a scheduled station we still have to treat the
	 * travelling part of the first order. */
	if (!travelling) {
		if (!CanDetermineTimeTaken(order, true)) return;
		sum += order->GetTimetabledTravel();
		table[i].arrival = sum;
	}
}

/**
 * Callback for when a time has been chosen to start the time table
 * @param p1 The p1 parameter to send to CmdSetTimetableStart
 * @param date the actually chosen date
 */
static void ChangeTimetableStartIntl(uint32 p1, DateTicksScaled date)
{
	DateTicks date_part = date / _settings_game.economy.day_length_factor;
	uint32 sub_ticks = date % _settings_game.economy.day_length_factor;
	DoCommandP(0, p1 | (sub_ticks << 21), (Ticks)(date_part - (((DateTicks)_date * DAY_TICKS) + _date_fract)), CMD_SET_TIMETABLE_START | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
}

/**
 * Callback for when a time has been chosen to start the time table
 * @param w the window related to the setting of the date
 * @param date the actually chosen date
 */
static void ChangeTimetableStartCallback(const Window *w, DateTicksScaled date)
{
	ChangeTimetableStartIntl(w->window_number, date);
}

struct TimetableWindow : Window {
	int sel_index;
	const Vehicle *vehicle; ///< Vehicle monitored by the window.
	bool show_expected;     ///< Whether we show expected arrival or scheduled
	uint deparr_time_width; ///< The width of the departure/arrival time
	uint deparr_abbr_width; ///< The width of the departure/arrival abbreviation
	int clicked_widget;     ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	Scrollbar *vscroll;
	bool query_is_speed_query; ///< The currently open query window is a speed query and not a time query.
	bool set_start_date_all;   ///< Set start date using minutes text entry: this is a set all vehicle (ctrl-click) action
	bool change_timetable_all; ///< Set wait time or speed for all timetable entries (ctrl-click) action
	int summary_warnings = 0;  ///< NUmber of summary warnings shown

	TimetableWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			sel_index(-1),
			vehicle(Vehicle::Get(window_number)),
			show_expected(true)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_VT_SCROLLBAR);
		this->UpdateSelectionStates();
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
	}

	~TimetableWindow()
	{
		if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	/**
	 * Build the arrival-departure list for a given vehicle
	 * @param v the vehicle to make the list for
	 * @param table the table to fill
	 * @return if next arrival will be early
	 */
	static bool BuildArrivalDepartureList(const Vehicle *v, TimetableArrivalDeparture *table)
	{
		assert(HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED));

		bool travelling = (!(v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) || v->current_order.GetNonStopType() == ONSF_STOP_EVERYWHERE);
		Ticks start_time = -v->current_order_time;
		if (v->cur_timetable_order_index != INVALID_VEH_ORDER_ID && v->cur_timetable_order_index != v->cur_real_order_index) {
			/* vehicle is taking a conditional order branch, adjust start time to compensate */
			const Order *real_current_order = v->GetOrder(v->cur_real_order_index);
			const Order *real_timetable_order = v->GetOrder(v->cur_timetable_order_index);
			assert(real_timetable_order->IsType(OT_CONDITIONAL));
			start_time += (real_timetable_order->GetWaitTime() - real_current_order->GetTravelTime());
		}

		FillTimetableArrivalDepartureTable(v, v->cur_real_order_index % v->GetNumOrders(), travelling, table, start_time);

		return (travelling && v->lateness_counter < 0);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_VT_ARRIVAL_DEPARTURE_PANEL:
				SetDParamMaxValue(0, MAX_YEAR * DAYS_IN_YEAR, 0, FS_SMALL);
				this->deparr_time_width = GetStringBoundingBox(STR_JUST_DATE_TINY).width;
				SetDParamMaxValue(0, _settings_time.time_in_minutes ? 0 : MAX_YEAR * DAYS_IN_YEAR);
				this->deparr_time_width = GetStringBoundingBox(STR_JUST_DATE_WALLCLOCK_TINY).width + 4;
				this->deparr_abbr_width = max(GetStringBoundingBox(STR_TIMETABLE_ARRIVAL_ABBREVIATION).width, GetStringBoundingBox(STR_TIMETABLE_DEPARTURE_ABBREVIATION).width);
				size->width = WD_FRAMERECT_LEFT + this->deparr_abbr_width + 10 + this->deparr_time_width + WD_FRAMERECT_RIGHT;
				FALLTHROUGH;

			case WID_VT_ARRIVAL_DEPARTURE_SELECTION:
			case WID_VT_TIMETABLE_PANEL:
				resize->height = max<int>(FONT_HEIGHT_NORMAL, GetSpriteSize(SPR_LOCK).height);
				size->height = WD_FRAMERECT_TOP + 8 * resize->height + WD_FRAMERECT_BOTTOM;
				break;

			case WID_VT_SUMMARY_PANEL: {
				Dimension d = GetSpriteSize(SPR_WARNING_SIGN);
				size->height = WD_FRAMERECT_TOP + 2 * FONT_HEIGHT_NORMAL + this->summary_warnings * max<int>(d.height, FONT_HEIGHT_NORMAL) + WD_FRAMERECT_BOTTOM;
				break;
			}
		}
	}

	int GetOrderFromTimetableWndPt(int y, const Vehicle *v)
	{
		int sel = (y - this->GetWidget<NWidgetBase>(WID_VT_TIMETABLE_PANEL)->pos_y - WD_FRAMERECT_TOP) / max<int>(FONT_HEIGHT_NORMAL, GetSpriteSize(SPR_LOCK).height);

		if ((uint)sel >= this->vscroll->GetCapacity()) return INVALID_ORDER;

		sel += this->vscroll->GetPosition();

		return (sel < v->GetNumOrders() * 2 && sel >= 0) ? sel : INVALID_ORDER;
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		switch (data) {
			case VIWD_AUTOREPLACE:
				/* Autoreplace replaced the vehicle */
				this->vehicle = Vehicle::Get(this->window_number);
				break;

			case VIWD_REMOVE_ALL_ORDERS:
				/* Removed / replaced all orders (after deleting / sharing) */
				if (this->sel_index == -1) break;

				this->DeleteChildWindows();
				this->sel_index = -1;
				break;

			case VIWD_MODIFY_ORDERS:
				if (!gui_scope) break;
				this->UpdateSelectionStates();
				this->ReInit();
				break;

			default: {
				if (gui_scope) break; // only do this once; from command scope

				/* Moving an order. If one of these is INVALID_VEH_ORDER_ID, then
				 * the order is being created / removed */
				if (this->sel_index == -1) break;

				VehicleOrderID from = GB(data, 0, 16);
				VehicleOrderID to   = GB(data, 16, 16);

				if (from == to) break; // no need to change anything

				/* if from == INVALID_VEH_ORDER_ID, one order was added; if to == INVALID_VEH_ORDER_ID, one order was removed */
				uint old_num_orders = this->vehicle->GetNumOrders() - (uint)(from == INVALID_VEH_ORDER_ID) + (uint)(to == INVALID_VEH_ORDER_ID);

				VehicleOrderID selected_order = (this->sel_index + 1) / 2;
				if (selected_order == old_num_orders) selected_order = 0; // when last travel time is selected, it belongs to order 0

				bool travel = HasBit(this->sel_index, 0);

				if (from != selected_order) {
					/* Moving from preceding order? */
					selected_order -= (int)(from <= selected_order);
					/* Moving to   preceding order? */
					selected_order += (int)(to   <= selected_order);
				} else {
					/* Now we are modifying the selected order */
					if (to == INVALID_VEH_ORDER_ID) {
						/* Deleting selected order */
						this->DeleteChildWindows();
						this->sel_index = -1;
						break;
					} else {
						/* Moving selected order */
						selected_order = to;
					}
				}

				/* recompute new sel_index */
				this->sel_index = 2 * selected_order - (int)travel;
				/* travel time of first order needs special handling */
				if (this->sel_index == -1) this->sel_index = this->vehicle->GetNumOrders() * 2 - 1;
				break;
			}
		}
	}

	virtual EventState OnCTRLStateChange() OVERRIDE
	{
		this->UpdateSelectionStates();
		this->SetDirty();
		return ES_NOT_HANDLED;
	}

	void OnPaint() override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		this->vscroll->SetCount(v->GetNumOrders() * 2);

		if (v->owner == _local_company) {
			bool disable = true;
			bool wait_lockable = false;
			bool wait_locked = false;
			bool clearable_when_wait_locked = false;
			if (selected != -1) {
				const Order *order = v->GetOrder(((selected + 1) / 2) % v->GetNumOrders());
				if (selected % 2 == 1) {
					/* Travel time */
					disable = order != nullptr && (order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT));
					wait_lockable = !disable;
					wait_locked = wait_lockable && order->IsTravelFixed();
				} else {
					/* Wait time */
					if (order != nullptr) {
						if (order->IsType(OT_GOTO_WAYPOINT)) {
							disable = false;
							clearable_when_wait_locked = true;
						} else if (order->IsType(OT_CONDITIONAL)) {
							disable = true;
						} else {
							disable = (!(order->IsType(OT_GOTO_STATION) || (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_HALT))) ||
									(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION));
						}
					} else {
						disable = true;
					}
					wait_lockable = !disable;
					wait_locked = wait_lockable && order->IsWaitFixed();
				}
			}
			bool disable_speed = disable || selected % 2 != 1 || v->type == VEH_AIRCRAFT;

			this->SetWidgetDisabledState(WID_VT_CHANGE_TIME, disable || (HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE) && !wait_locked));
			this->SetWidgetDisabledState(WID_VT_CLEAR_TIME, disable || (HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE) && !(wait_locked && clearable_when_wait_locked)));
			this->SetWidgetDisabledState(WID_VT_CHANGE_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_CLEAR_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_SHARED_ORDER_LIST, !(v->IsOrderListShared() || _settings_client.gui.enable_single_veh_shared_order_gui));

			this->SetWidgetDisabledState(WID_VT_START_DATE, v->orders.list == nullptr || HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) || HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
			this->SetWidgetDisabledState(WID_VT_RESET_LATENESS, v->orders.list == nullptr);
			this->SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders.list == nullptr || HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE));
			this->SetWidgetDisabledState(WID_VT_AUTO_SEPARATION, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
			this->EnableWidget(WID_VT_AUTOMATE);
			this->EnableWidget(WID_VT_ADD_VEH_GROUP);
			this->SetWidgetDisabledState(WID_VT_LOCK_ORDER_TIME, !wait_lockable);
			this->SetWidgetLoweredState(WID_VT_LOCK_ORDER_TIME, wait_locked);
			this->SetWidgetDisabledState(WID_VT_EXTRA, disable || (selected % 2 != 0));
		} else {
			this->DisableWidget(WID_VT_START_DATE);
			this->DisableWidget(WID_VT_CHANGE_TIME);
			this->DisableWidget(WID_VT_CLEAR_TIME);
			this->DisableWidget(WID_VT_CHANGE_SPEED);
			this->DisableWidget(WID_VT_CLEAR_SPEED);
			this->DisableWidget(WID_VT_RESET_LATENESS);
			this->DisableWidget(WID_VT_AUTOFILL);
			this->DisableWidget(WID_VT_AUTOMATE);
			this->DisableWidget(WID_VT_AUTO_SEPARATION);
			this->DisableWidget(WID_VT_SHARED_ORDER_LIST);
			this->DisableWidget(WID_VT_ADD_VEH_GROUP);
			this->DisableWidget(WID_VT_LOCK_ORDER_TIME);
			this->DisableWidget(WID_VT_EXTRA);
		}

		this->SetWidgetLoweredState(WID_VT_AUTOFILL, HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE));
		this->SetWidgetLoweredState(WID_VT_AUTOMATE, HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE));
		this->SetWidgetLoweredState(WID_VT_AUTO_SEPARATION, HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION));
		this->SetWidgetLoweredState(WID_VT_SCHEDULED_DISPATCH, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));

		this->SetWidgetDisabledState(WID_VT_SCHEDULED_DISPATCH, v->orders.list == nullptr);

		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_VT_CAPTION: SetDParam(0, this->vehicle->index); break;
			case WID_VT_EXPECTED: SetDParam(0, this->show_expected ? STR_TIMETABLE_EXPECTED : STR_TIMETABLE_SCHEDULED); break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		switch (widget) {
			case WID_VT_TIMETABLE_PANEL: {
				int y = r.top + WD_FRAMERECT_TOP;
				int i = this->vscroll->GetPosition();
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = max<int>(FONT_HEIGHT_NORMAL, lock_d.height);
				VehicleOrderID order_id = (i + 1) / 2;
				bool final_order = false;

				bool rtl = _current_text_dir == TD_RTL;
				SetDParamMaxValue(0, v->GetNumOrders(), 2);
				int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + 3;
				int middle = rtl ? r.right - WD_FRAMERECT_RIGHT - index_column_width : r.left + WD_FRAMERECT_LEFT + index_column_width;

				const Order *order = v->GetOrder(order_id);
				while (order != nullptr) {
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						DrawOrderString(v, order, order_id, y, i == selected, true, r.left + WD_FRAMERECT_LEFT, middle, r.right - WD_FRAMERECT_RIGHT);

						order_id++;

						if (order_id >= v->GetNumOrders()) {
							order = v->GetOrder(0);
							final_order = true;
						} else {
							order = order->next;
						}
					} else {
						StringID string;
						TextColour colour = (i == selected) ? TC_WHITE : TC_BLACK;
						if (order->IsType(OT_CONDITIONAL)) {
							string = STR_TIMETABLE_NO_TRAVEL;
						} else if (order->IsType(OT_IMPLICIT)) {
							string = STR_TIMETABLE_NOT_TIMETABLEABLE;
							colour = ((i == selected) ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
						} else if (!order->IsTravelTimetabled()) {
							if (order->GetTravelTime() > 0) {
								SetTimetableParams(0, order->GetTravelTime());
								string = order->GetMaxSpeed() != UINT16_MAX ?
										STR_TIMETABLE_TRAVEL_FOR_SPEED_ESTIMATED  :
										STR_TIMETABLE_TRAVEL_FOR_ESTIMATED;
							} else {
								string = order->GetMaxSpeed() != UINT16_MAX ?
										STR_TIMETABLE_TRAVEL_NOT_TIMETABLED_SPEED :
										STR_TIMETABLE_TRAVEL_NOT_TIMETABLED;
							}
						} else {
							SetTimetableParams(0, order->GetTimetabledTravel());
							string = order->GetMaxSpeed() != UINT16_MAX ?
									STR_TIMETABLE_TRAVEL_FOR_SPEED : STR_TIMETABLE_TRAVEL_FOR;
						}
						SetDParam(string == STR_TIMETABLE_TRAVEL_NOT_TIMETABLED_SPEED ? 2 : 4, order->GetMaxSpeed());

						int edge = DrawString(rtl ? r.left + WD_FRAMERECT_LEFT : middle, rtl ? middle : r.right - WD_FRAMERECT_LEFT, y, string, colour);

						if (order->IsTravelFixed()) {
							Dimension lock_d = GetSpriteSize(SPR_LOCK);
							DrawPixelInfo tmp_dpi;
							if (FillDrawPixelInfo(&tmp_dpi, rtl ? r.left + WD_FRAMERECT_LEFT : middle, y, rtl ? middle : r.right - WD_FRAMERECT_LEFT, lock_d.height)) {
								DrawPixelInfo *old_dpi = _cur_dpi;
								_cur_dpi = &tmp_dpi;

								DrawSprite(SPR_LOCK, PAL_NONE, rtl ? edge - 3 - lock_d.width - (r.left + WD_FRAMERECT_LEFT) : edge + 3 - middle, 0);

								_cur_dpi = old_dpi;
							}
						}

						if (final_order) break;
					}

					i++;
					y += line_height;
				}
				break;
			}

			case WID_VT_ARRIVAL_DEPARTURE_PANEL: {
				/* Arrival and departure times are handled in an all-or-nothing approach,
				 * i.e. are only shown if we can calculate all times.
				 * Excluding order lists with only one order makes some things easier.
				 */
				Ticks total_time = v->orders.list != nullptr ? v->orders.list->GetTimetableDurationIncomplete() : 0;
				if (total_time <= 0 || v->GetNumOrders() <= 1 || !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) break;

				TimetableArrivalDeparture *arr_dep = AllocaM(TimetableArrivalDeparture, v->GetNumOrders());
				const VehicleOrderID cur_order = v->cur_real_order_index % v->GetNumOrders();

				VehicleOrderID earlyID = BuildArrivalDepartureList(v, arr_dep) ? cur_order : (VehicleOrderID)INVALID_VEH_ORDER_ID;

				int y = r.top + WD_FRAMERECT_TOP;
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = max<int>(FONT_HEIGHT_NORMAL, lock_d.height);

				bool show_late = this->show_expected && v->lateness_counter > DATE_UNIT_SIZE;
				Ticks offset = show_late ? 0 : -v->lateness_counter;

				bool rtl = _current_text_dir == TD_RTL;
				int abbr_left  = rtl ? r.right - WD_FRAMERECT_RIGHT - this->deparr_abbr_width : r.left + WD_FRAMERECT_LEFT;
				int abbr_right = rtl ? r.right - WD_FRAMERECT_RIGHT : r.left + WD_FRAMERECT_LEFT + this->deparr_abbr_width;
				int time_left  = rtl ? r.left + WD_FRAMERECT_LEFT : r.right - WD_FRAMERECT_RIGHT - this->deparr_time_width;
				int time_right = rtl ? r.left + WD_FRAMERECT_LEFT + this->deparr_time_width : r.right - WD_FRAMERECT_RIGHT;

				for (int i = this->vscroll->GetPosition(); i / 2 < v->GetNumOrders(); ++i) { // note: i is also incremented in the loop
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						if (arr_dep[i / 2].arrival != INVALID_TICKS) {
							DrawString(abbr_left, abbr_right, y, STR_TIMETABLE_ARRIVAL_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							if (this->show_expected && i / 2 == earlyID) {
								SetDParam(0, _scaled_date_ticks + arr_dep[i / 2].arrival);
								DrawString(time_left, time_right, y, STR_JUST_DATE_WALLCLOCK_TINY, TC_GREEN);
							} else {
								SetDParam(0, _scaled_date_ticks + arr_dep[i / 2].arrival + offset);
								DrawString(time_left, time_right, y, STR_JUST_DATE_WALLCLOCK_TINY,
										show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK);
							}
						}
					} else {
						if (arr_dep[i / 2].departure != INVALID_TICKS) {
							DrawString(abbr_left, abbr_right, y, STR_TIMETABLE_DEPARTURE_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							SetDParam(0, _scaled_date_ticks + arr_dep[i/2].departure + offset);
							DrawString(time_left, time_right, y, STR_JUST_DATE_WALLCLOCK_TINY,
									show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK);
						}
					}
					y += line_height;
				}
				break;
			}

			case WID_VT_SUMMARY_PANEL: {
				int y = r.top + WD_FRAMERECT_TOP;

				Ticks total_time = v->orders.list != nullptr ? v->orders.list->GetTimetableDurationIncomplete() : 0;
				if (total_time != 0) {
					SetTimetableParams(0, total_time);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, v->orders.list->IsCompleteTimetable() ? STR_TIMETABLE_TOTAL_TIME : STR_TIMETABLE_TOTAL_TIME_INCOMPLETE);
				}
				y += FONT_HEIGHT_NORMAL;

				if (v->timetable_start != 0) {
					/* We are running towards the first station so we can start the
					 * timetable at the given time. */
					SetDParam(0, STR_JUST_DATE_WALLCLOCK_TINY);
					SetDParam(1, (((DateTicksScaled) v->timetable_start) * _settings_game.economy.day_length_factor) + v->timetable_start_subticks);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_START_AT);
				} else if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
					/* We aren't running on a timetable yet, so how can we be "on time"
					 * when we aren't even "on service"/"on duty"? */
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_NOT_STARTED);
				} else if (v->lateness_counter == 0 || (!_settings_client.gui.timetable_in_ticks && v->lateness_counter / DATE_UNIT_SIZE == 0)) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TIMETABLE_STATUS_ON_TIME);
				} else {
					SetTimetableParams(0, abs(v->lateness_counter));
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, v->lateness_counter < 0 ? STR_TIMETABLE_STATUS_EARLY : STR_TIMETABLE_STATUS_LATE);
				}
				y += FONT_HEIGHT_NORMAL;

				{
					bool have_conditional = false;
					bool have_missing_wait = false;
					bool have_missing_travel = false;
					bool have_bad_full_load = false;
					bool have_non_timetabled_conditional_branch = false;

					const bool assume_timetabled = HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE) || HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE);
					for (int n = 0; n < v->GetNumOrders(); n++) {
						const Order *order = v->GetOrder(n);
						if (order->IsType(OT_CONDITIONAL)) {
							have_conditional = true;
							if (!order->IsWaitTimetabled()) have_non_timetabled_conditional_branch = true;
						} else {
							if (order->GetWaitTime() == 0 && order->IsType(OT_GOTO_STATION) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
								have_missing_wait = true;
							}
							if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
								have_missing_travel = true;
							}
						}

						if (order->IsType(OT_GOTO_STATION) && !have_bad_full_load && (assume_timetabled || order->IsWaitTimetabled())) {
							if (order->GetLoadType() & OLFB_FULL_LOAD) have_bad_full_load = true;
							if (order->GetLoadType() == OLFB_CARGO_TYPE_LOAD) {
								for (CargoID c = 0; c < NUM_CARGO; c++) {
									if (order->GetCargoLoadTypeRaw(c) & OLFB_FULL_LOAD) {
										have_bad_full_load = true;
										break;
									}
								}
							}
						}
					}

					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					const int step_height = max<int>(warning_dimensions.height, FONT_HEIGHT_NORMAL);
					const int text_offset_y = (step_height - FONT_HEIGHT_NORMAL) / 2;
					const int warning_offset_y = (step_height - warning_dimensions.height) / 2;
					const bool rtl = _current_text_dir == TD_RTL;

					int warning_count = 0;

					auto draw_info = [&](StringID text, bool warning) {
						int left = r.left + WD_FRAMERECT_LEFT;
						int right = r.right - WD_FRAMERECT_RIGHT;
						if (warning) {
							DrawSprite(SPR_WARNING_SIGN, 0, rtl ? right - warning_dimensions.width - 5 : left + 5, y + warning_offset_y);
							if (rtl) {
								right -= (warning_dimensions.width + 10);
							} else {
								left += (warning_dimensions.width + 10);
							}
						}
						DrawString(left, right, y + text_offset_y, text);
						y += step_height;
						warning_count++;
					};

					if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
						if (have_conditional) draw_info(STR_TIMETABLE_WARNING_AUTOSEP_CONDITIONAL, true);
						if (have_missing_wait || have_missing_travel) {
							if (assume_timetabled) {
								draw_info(STR_TIMETABLE_AUTOSEP_TIMETABLE_INCOMPLETE, false);
							} else {
								draw_info(STR_TIMETABLE_WARNING_AUTOSEP_MISSING_TIMINGS, true);
							}
						} else if (v->GetNumOrders() == 0) {
							draw_info(STR_TIMETABLE_AUTOSEP_TIMETABLE_INCOMPLETE, false);
						} else if (!have_conditional) {
							draw_info(v->IsOrderListShared() ? STR_TIMETABLE_AUTOSEP_OK : STR_TIMETABLE_AUTOSEP_SINGLE_VEH, false);
						}
					}
					if (have_bad_full_load) draw_info(STR_TIMETABLE_WARNING_FULL_LOAD, true);
					if (have_conditional && HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE)) draw_info(STR_TIMETABLE_WARNING_AUTOFILL_CONDITIONAL, true);
					if (total_time && have_non_timetabled_conditional_branch) draw_info(STR_TIMETABLE_NON_TIMETABLED_BRANCH, false);
					if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
						VehicleOrderID n = v->GetFirstWaitingLocation(false);
						if (n == INVALID_VEH_ORDER_ID) {
							draw_info(STR_TIMETABLE_WARNING_NO_SCHEDULED_DISPATCH_ORDER, true);
						} else if (!v->GetOrder(n)->IsWaitTimetabled()) {
							draw_info(STR_TIMETABLE_WARNING_SCHEDULED_DISPATCH_ORDER_NO_WAIT_TIME, true);
						}
					}

					if (warning_count != this->summary_warnings) {
						TimetableWindow *mutable_this = const_cast<TimetableWindow *>(this);
						mutable_this->summary_warnings = warning_count;
						mutable_this->ReInit();
					}
				}

				break;
			}
		}
	}

	static inline void ExecuteTimetableCommand(const Vehicle *v, bool bulk, uint selected, ModifyTimetableFlags mtf, uint p2, bool clear)
	{
		uint order_number = (selected + 1) / 2;
		if (order_number >= v->GetNumOrders()) order_number = 0;

		uint p1 = v->index | (mtf << 28) | (clear ? 1 << 31 : 0);
		if (bulk) {
			DoCommandP(0, p1, p2, CMD_BULK_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
		} else {
			DoCommandPEx(0, p1, p2, order_number, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), nullptr, nullptr, 0);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		const Vehicle *v = this->vehicle;

		this->clicked_widget = widget;
		this->DeleteChildWindows(WC_QUERY_STRING);

		switch (widget) {
			case WID_VT_ORDER_VIEW: // Order view button
				ShowOrdersWindow(v);
				break;

			case WID_VT_TIMETABLE_PANEL: { // Main panel.
				int selected = GetOrderFromTimetableWndPt(pt.y, v);

				/* Allow change time by double-clicking order */
				if (click_count == 2) {
					this->sel_index = selected == INVALID_ORDER ? -1 : selected;
					this->OnClick(pt, WID_VT_CHANGE_TIME, click_count);
					return;
				} else {
					this->sel_index = (selected == INVALID_ORDER || selected == this->sel_index) ? -1 : selected;
				}

				this->DeleteChildWindows();
				break;
			}

			case WID_VT_START_DATE: // Change the date that the timetable starts.
				if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					this->set_start_date_all = v->orders.list->IsCompleteTimetable() && _ctrl_pressed;
					StringID str = STR_JUST_INT;
					uint64 time = _scaled_date_ticks;
					time /= _settings_time.ticks_per_minute;
					time += _settings_time.clock_offset;
					time %= (24 * 60);
					time = (time % 60) + (((time / 60) % 24) * 100);
					SetDParam(0, time);
					ShowQueryString(str, STR_TIMETABLE_STARTING_DATE, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				} else {
					ShowSetDateWindow(this, v->index | (v->orders.list->IsCompleteTimetable() && _ctrl_pressed ? 1U << 20 : 0),
							_scaled_date_ticks, _cur_year, _cur_year + 15, ChangeTimetableStartCallback);
				}
				break;

			case WID_VT_CHANGE_TIME: { // "Wait For" button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order *order = v->GetOrder(real);
				StringID current = STR_EMPTY;

				if (order != nullptr) {
					uint time = (selected % 2 == 1) ? order->GetTravelTime() : order->GetWaitTime();
					if (!_settings_client.gui.timetable_in_ticks) time /= DATE_UNIT_SIZE;

					if (time != 0) {
						SetDParam(0, time);
						current = STR_JUST_INT;
					}
				}

				this->query_is_speed_query = false;
				this->change_timetable_all = (order != nullptr) && (selected % 2 == 0) && _ctrl_pressed;
				ShowQueryString(current, STR_TIMETABLE_CHANGE_TIME, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				break;
			}

			case WID_VT_CHANGE_SPEED: { // Change max speed button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				StringID current = STR_EMPTY;
				const Order *order = v->GetOrder(real);
				if (order != nullptr) {
					if (order->GetMaxSpeed() != UINT16_MAX) {
						SetDParam(0, ConvertKmhishSpeedToDisplaySpeed(order->GetMaxSpeed()));
						current = STR_JUST_INT;
					}
				}

				this->query_is_speed_query = true;
				this->change_timetable_all = (order != nullptr) && _ctrl_pressed;
				ShowQueryString(current, STR_TIMETABLE_CHANGE_SPEED, 31, this, CS_NUMERAL, QSF_NONE);
				break;
			}

			case WID_VT_CLEAR_TIME: { // Clear travel/waiting time.
				ExecuteTimetableCommand(v, _ctrl_pressed, this->sel_index, (this->sel_index % 2 == 1) ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, 0, true);
				break;
			}

			case WID_VT_CLEAR_SPEED: { // Clear max speed button.
				ExecuteTimetableCommand(v, _ctrl_pressed, this->sel_index, MTF_TRAVEL_SPEED, UINT16_MAX, false);
				break;
			}

			case WID_VT_LOCK_ORDER_TIME: { // Toggle order wait time lock state.
				bool locked = false;

				int selected = this->sel_index;
				VehicleOrderID order_number = (selected + 1) / 2;
				if (order_number >= v->GetNumOrders()) order_number = 0;

				const Order *order = v->GetOrder(order_number);
				if (order != nullptr) {
					locked = (selected % 2 == 1) ? order->IsTravelFixed() : order->IsWaitFixed();
				}

				ExecuteTimetableCommand(v, _ctrl_pressed, this->sel_index, ((selected % 2 == 1) ? MTF_SET_TRAVEL_FIXED : MTF_SET_WAIT_FIXED), locked ? 0 : 1, false);
				break;
			}

			case WID_VT_RESET_LATENESS: // Reset the vehicle's late counter.
				DoCommandP(0, v->index, 0, CMD_SET_VEHICLE_ON_TIME | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;

			case WID_VT_AUTOFILL: { // Autofill the timetable.
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE)) SetBit(p2, 0);
				if (_ctrl_pressed) SetBit(p2, 1);
				DoCommandP(0, v->index, p2, CMD_AUTOFILL_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_SCHEDULED_DISPATCH: {
				ShowSchdispatchWindow(v);
				break;
			}

			case WID_VT_AUTOMATE: {
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) SetBit(p2, 0);
				if (_ctrl_pressed) SetBit(p2, 1);
				DoCommandP(0, v->index, p2, CMD_AUTOMATE_TIMETABLE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_AUTO_SEPARATION: {
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) SetBit(p2, 0);
				DoCommandP(0, v->index, p2, CMD_TIMETABLE_SEPARATION | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_VT_EXPECTED:
				this->show_expected = !this->show_expected;
				break;

			case WID_VT_SHARED_ORDER_LIST:
				ShowVehicleListWindow(v);
				break;

			case WID_VT_ADD_VEH_GROUP: {
				ShowQueryString(STR_EMPTY, STR_GROUP_RENAME_CAPTION, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;
			}

			case WID_VT_EXTRA: {
				VehicleOrderID real = (this->sel_index + 1) / 2;
				if (real >= this->vehicle->GetNumOrders()) real = 0;
				const Order *order = this->vehicle->GetOrder(real);
				bool leave_type_disabled = (order == nullptr) ||
							((!(order->IsType(OT_GOTO_STATION) || (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_HALT))) ||
								(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) && !order->IsType(OT_CONDITIONAL));
				DropDownList list;
				list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_LEAVE_NORMAL, OLT_NORMAL, leave_type_disabled));
				list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_LEAVE_EARLY, OLT_LEAVE_EARLY, leave_type_disabled));
				list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_LEAVE_EARLY_FULL_ANY, OLT_LEAVE_EARLY_FULL_ANY, leave_type_disabled || !order->IsType(OT_GOTO_STATION)));
				list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_LEAVE_EARLY_FULL_ALL, OLT_LEAVE_EARLY_FULL_ALL, leave_type_disabled || !order->IsType(OT_GOTO_STATION)));
				ShowDropDownList(this, std::move(list), order != nullptr ? order->GetLeaveType() : -1, WID_VT_EXTRA);
				break;
			}
		}

		this->SetDirty();
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_VT_EXTRA: {
				ExecuteTimetableCommand(this->vehicle, false, this->sel_index, MTF_SET_LEAVE_TYPE, index, false);
			}

			default:
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		const Vehicle *v = this->vehicle;

		switch (this->clicked_widget) {
			default: NOT_REACHED();

			case WID_VT_CHANGE_SPEED:
			case WID_VT_CHANGE_TIME: {
				uint64 val = StrEmpty(str) ? 0 : strtoul(str, nullptr, 10);
				uint32 p2;
				if (this->query_is_speed_query) {
					val = ConvertDisplaySpeedToKmhishSpeed(val);
					p2 = minu(val, UINT16_MAX);
				} else {
					if (!_settings_client.gui.timetable_in_ticks) val *= DATE_UNIT_SIZE;
					p2 = val;
				}

				ExecuteTimetableCommand(v, this->change_timetable_all, this->sel_index, (this->sel_index % 2 == 1) ? (this->query_is_speed_query ? MTF_TRAVEL_SPEED : MTF_TRAVEL_TIME) : MTF_WAIT_TIME, p2, false);
				break;
			}

			case WID_VT_START_DATE: {
				if (StrEmpty(str)) break;
				char *end;
				int32 val = strtol(str, &end, 10);
				if (val >= 0 && end && *end == 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					DateTicksScaled time = MINUTES_DATE(MINUTES_DAY(CURRENT_MINUTE), hours, minutes);
					time -= _settings_time.clock_offset;

					if (time < (CURRENT_MINUTE - 60)) time += 60 * 24;
					time *= _settings_time.ticks_per_minute;
					ChangeTimetableStartIntl(v->index | (this->set_start_date_all ? 1 << 20 : 0), time);
				}
				break;
			}

			case WID_VT_ADD_VEH_GROUP: {
				DoCommandP(0, VehicleListIdentifier(VL_SINGLE_VEH, v->type, v->owner, v->index).Pack(), 0, CMD_CREATE_GROUP_FROM_LIST | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), nullptr, str);
				break;
			}
		}
	}

	void OnResize() override
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, WID_VT_TIMETABLE_PANEL, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}

	/**
	 * Update the selection state of the arrival/departure data
	 */
	void UpdateSelectionStates()
	{
		this->GetWidget<NWidgetStacked>(WID_VT_ARRIVAL_DEPARTURE_SELECTION)->SetDisplayedPlane(_settings_client.gui.timetable_arrival_departure ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_VT_EXPECTED_SELECTION)->SetDisplayedPlane(_settings_client.gui.timetable_arrival_departure ? 0 : 1);
		this->GetWidget<NWidgetStacked>(WID_VT_SEL_SHARED)->SetDisplayedPlane(this->vehicle->owner == _local_company && _ctrl_pressed ? 1 : 0);
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	virtual void OnFocusLost(Window *newly_focused_window) override
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
};

static const NWidgetPart _nested_timetable_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VT_CAPTION), SetDataTip(STR_TIMETABLE_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ORDER_VIEW), SetMinimalSize(61, 14), SetDataTip( STR_TIMETABLE_ORDER_VIEW, STR_TIMETABLE_ORDER_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_TIMETABLE_PANEL), SetMinimalSize(388, 82), SetResize(1, 10), SetDataTip(STR_NULL, STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_ARRIVAL_DEPARTURE_SELECTION),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_ARRIVAL_DEPARTURE_PANEL), SetMinimalSize(110, 0), SetFill(0, 1), SetDataTip(STR_NULL, STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VT_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_START_DATE), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_STARTING_DATE, STR_TIMETABLE_STARTING_DATE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_TIME), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CHANGE_TIME, STR_TIMETABLE_WAIT_TIME_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_TIME), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CLEAR_TIME, STR_TIMETABLE_CLEAR_TIME_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTOFILL), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_AUTOFILL, STR_TIMETABLE_AUTOFILL_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_SPEED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CHANGE_SPEED, STR_TIMETABLE_CHANGE_SPEED_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_SPEED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_CLEAR_SPEED, STR_TIMETABLE_CLEAR_SPEED_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTOMATE), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_AUTOMATE, STR_TIMETABLE_AUTOMATE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTO_SEPARATION), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_AUTO_SEPARATION, STR_TIMETABLE_AUTO_SEPARATION_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VT_EXTRA), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_EXTRA_DROP_DOWN, STR_TIMETABLE_EXTRA_DROP_DOWN_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_SCHEDULED_DISPATCH), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_SCHEDULED_DISPATCH, STR_TIMETABLE_SCHEDULED_DISPATCH_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_RESET_LATENESS), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_RESET_LATENESS, STR_TIMETABLE_RESET_LATENESS_TOOLTIP),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_EXPECTED_SELECTION),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_EXPECTED), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_BLACK_STRING, STR_TIMETABLE_EXPECTED_TOOLTIP),
					NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), SetFill(1, 1), EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_SEL_SHARED),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VT_SHARED_ORDER_LIST), SetFill(0, 1), SetDataTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ADD_VEH_GROUP), SetFill(0, 1), SetDataTip(STR_BLACK_PLUS, STR_ORDERS_NEW_GROUP_TOOLTIP),
			EndContainer(),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VT_LOCK_ORDER_TIME), SetFill(0, 1), SetDataTip(SPR_LOCK, STR_TIMETABLE_LOCK_ORDER_TIME_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY), SetFill(0, 1),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _timetable_desc(
	WDP_AUTO, "view_vehicle_timetable", 400, 130,
	WC_VEHICLE_TIMETABLE, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	_nested_timetable_widgets, lengthof(_nested_timetable_widgets)
);

/**
 * Show the timetable for a given vehicle.
 * @param v The vehicle to show the timetable for.
 */
void ShowTimetableWindow(const Vehicle *v)
{
	DeleteWindowById(WC_VEHICLE_DETAILS, v->index, false);
	DeleteWindowById(WC_VEHICLE_ORDERS, v->index, false);
	AllocateWindowDescFront<TimetableWindow>(&_timetable_desc, v->index);
}
