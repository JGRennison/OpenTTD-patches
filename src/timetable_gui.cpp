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
#include "tracerestrict.h"
#include "core/backup_type.hpp"

#include "widgets/timetable_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"

enum TimetableArrivalDepartureFlags {
	TADF_ARRIVAL_PREDICTED,
	TADF_DEPARTURE_PREDICTED,
	TADF_ARRIVAL_NO_OFFSET,
	TADF_DEPARTURE_NO_OFFSET,
	TADF_REACHED,
};

/** Container for the arrival/departure dates of a vehicle */
struct TimetableArrivalDeparture {
	Ticks arrival;   ///< The arrival time
	Ticks departure; ///< The departure time
	uint flags;
};

/**
 * Set the timetable parameters in the format as described by the setting.
 * @param param the first DParam to fill
 * @param ticks  the number of ticks to 'draw'
 * @param long_mode long output format
 */
void SetTimetableParams(int first_param, Ticks ticks, bool long_mode)
{
	SetDParam(first_param, long_mode ? STR_JUST_TT_TICKS_LONG : STR_JUST_TT_TICKS);
	SetDParam(first_param + 1, ticks);
}

Ticks ParseTimetableDuration(const char *str)
{
	if (StrEmpty(str)) return 0;

	if (_settings_client.gui.timetable_in_ticks) {
		return strtoul(str, nullptr, 10);
	}

	char tmp_buffer[64];
	strecpy(tmp_buffer, str, lastof(tmp_buffer));
	str_replace_wchar(tmp_buffer, lastof(tmp_buffer), GetDecimalSeparatorChar(), '.');
	return atof(tmp_buffer) * DATE_UNIT_SIZE;
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

	/* Pre-initialize with unknown time */
	for (int i = 0; i < v->GetNumOrders(); ++i) {
		table[i].arrival = table[i].departure = INVALID_TICKS;
		table[i].flags = 0;
	}

	Ticks sum = offset;
	VehicleOrderID i = start;
	const Order *order = v->GetOrder(i);

	bool predicted = false;
	bool no_offset = false;
	bool skip_travel = false;

	/* Cyclically loop over all orders until we reach the current one again.
	 * As we may start at the current order, do a post-checking loop */
	do {
		if (HasBit(table[i].flags, TADF_REACHED)) break;
		SetBit(table[i].flags, TADF_REACHED);

		bool skip = order->IsType(OT_IMPLICIT);

		if (order->IsType(OT_CONDITIONAL)) {
			bool jump = false;
			switch (order->GetConditionVariable()) {
				case OCV_UNCONDITIONALLY: {
					jump = true;
					break;
				}

				case OCV_TIME_DATE: {
					predicted = true;
					DateTicksScaled time = _scaled_date_ticks + sum;
					if (!no_offset) time -= v->lateness_counter;
					int value = GetTraceRestrictTimeDateValueFromDate(static_cast<TraceRestrictTimeDateValueField>(order->GetConditionValue()), time);
					jump = OrderConditionCompare(order->GetConditionComparator(), value, order->GetXData());
					break;
				}

				case OCV_DISPATCH_SLOT: {
					DateTicksScaled time = _scaled_date_ticks + sum;
					if (!no_offset) time -= v->lateness_counter;
					extern bool EvaluateDispatchSlotConditionalOrder(const Order *order, const Vehicle *v, DateTicksScaled date_time, bool *predicted);
					jump = EvaluateDispatchSlotConditionalOrder(order, v, time, &predicted);
					break;
				}

				default:
					return;
			}
			if (jump) {
				if (!order->IsWaitTimetabled()) return;
				sum += order->GetTimetabledWait();
				i = order->GetConditionSkipToOrder();
				order = v->GetOrder(i);
				skip_travel = true;
				continue;
			} else {
				skip = true;
			}
		}

		/* Automatic orders don't influence the overall timetable;
		 * they just add some untimetabled entries, but the time till
		 * the next non-implicit order can still be known. */
		if (!skip) {
			if (travelling || i != start) {
				if (!skip_travel) {
					if (!CanDetermineTimeTaken(order, true)) return;
					sum += order->GetTimetabledTravel();
				}
				table[i].arrival = sum;
				if (predicted) SetBit(table[i].flags, TADF_ARRIVAL_PREDICTED);
				if (no_offset) SetBit(table[i].flags, TADF_ARRIVAL_NO_OFFSET);
			}

			if (order->IsScheduledDispatchOrder(true) && !(i == start && !travelling)) {
				if (!no_offset) sum -= v->lateness_counter;
				extern DateTicksScaled GetScheduledDispatchTime(const DispatchSchedule &ds, DateTicksScaled leave_time);
				DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(order->GetDispatchScheduleIndex());
				DispatchSchedule predicted_ds;
				predicted_ds.BorrowSchedule(ds);
				predicted_ds.UpdateScheduledDispatchToDate(_scaled_date_ticks + sum);
				DateTicksScaled slot = GetScheduledDispatchTime(predicted_ds, _scaled_date_ticks + sum + order->GetTimetabledWait());
				predicted_ds.ReturnSchedule(ds);
				if (slot <= -1) return;
				sum = slot - _scaled_date_ticks;
				predicted = true;
				no_offset = true;
			} else {
				if (!CanDetermineTimeTaken(order, false)) return;
				sum += order->GetTimetabledWait();
			}
			table[i].departure = sum;
			if (predicted) SetBit(table[i].flags, TADF_DEPARTURE_PREDICTED);
			if (predicted) SetBit(table[i].flags, TADF_DEPARTURE_NO_OFFSET);
		}

		skip_travel = false;

		++i;
		order = order->next;
		if (i >= v->GetNumOrders()) {
			i = 0;
			assert(order == nullptr);
			order = v->orders->GetFirstOrder();
		}
	} while (i != start);

	/* When loading at a scheduled station we still have to treat the
	 * travelling part of the first order. */
	if (!travelling && table[i].arrival == INVALID_TICKS) {
		if (!CanDetermineTimeTaken(order, true)) return;
		sum += order->GetTimetabledTravel();
		table[i].arrival = sum;
		if (predicted) SetBit(table[i].flags, TADF_ARRIVAL_PREDICTED);
		if (no_offset) SetBit(table[i].flags, TADF_ARRIVAL_NO_OFFSET);
	}
}

/**
 * Callback for when a time has been chosen to start the time table
 * @param p1 The p1 parameter to send to CmdSetTimetableStart
 * @param date the actually chosen date
 */
static void ChangeTimetableStartIntl(uint32 p1, DateTicksScaled date)
{
	DateTicks date_part;
	uint16 sub_ticks;
	std::tie(date_part, sub_ticks) = ScaledDateTicksToDateTicksAndSubTicks(date);
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

void ProcessTimetableWarnings(const Vehicle *v, std::function<void(StringID, bool)> handler)
{
	Ticks total_time = v->orders != nullptr ? v->orders->GetTimetableDurationIncomplete() : 0;

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

	if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
		if (have_conditional) handler(STR_TIMETABLE_WARNING_AUTOSEP_CONDITIONAL, true);
		if (have_missing_wait || have_missing_travel) {
			if (assume_timetabled) {
				handler(STR_TIMETABLE_AUTOSEP_TIMETABLE_INCOMPLETE, false);
			} else {
				handler(STR_TIMETABLE_WARNING_AUTOSEP_MISSING_TIMINGS, true);
				handler(STR_TIMETABLE_FILL_TIMETABLE_SUGGESTION, false);
				handler(STR_TIMETABLE_FILL_TIMETABLE_SUGGESTION_2, false);
			}
		} else if (v->GetNumOrders() == 0) {
			handler(STR_TIMETABLE_AUTOSEP_TIMETABLE_INCOMPLETE, false);
		} else if (!have_conditional) {
			handler(v->IsOrderListShared() ? STR_TIMETABLE_AUTOSEP_OK : STR_TIMETABLE_AUTOSEP_SINGLE_VEH, false);
		}
	}
	if (have_bad_full_load) handler(STR_TIMETABLE_WARNING_FULL_LOAD, true);
	if (have_conditional && HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE)) handler(STR_TIMETABLE_WARNING_AUTOFILL_CONDITIONAL, true);
	if (total_time && have_non_timetabled_conditional_branch) handler(STR_TIMETABLE_NON_TIMETABLED_BRANCH, false);
	if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->orders != nullptr) {
		auto sd_warning = [&](int schedule_index, StringID str) {
			if (v->orders->GetScheduledDispatchScheduleCount() > 1) {
				SetDParam(0, schedule_index + 1);
				SetDParam(1, str);
				handler(STR_TIMETABLE_WARNING_SCHEDULE_ID, true);
			} else {
				handler(str, true);
			}
		};
		std::vector<bool> seen_sched_dispatch_orders(v->orders->GetScheduledDispatchScheduleCount());

		for (int n = 0; n < v->GetNumOrders(); n++) {
			const Order *order = v->GetOrder(n);
			int schedule_index = order->GetDispatchScheduleIndex();
			if (schedule_index >= 0) {
				seen_sched_dispatch_orders[schedule_index] = true;
				if (!order->IsWaitTimetabled()) {
					sd_warning(schedule_index, STR_TIMETABLE_WARNING_SCHEDULED_DISPATCH_ORDER_NO_WAIT_TIME);
				}
			}
		}
		for (uint i = 0; i < seen_sched_dispatch_orders.size(); i++) {
			if (!seen_sched_dispatch_orders[i]) sd_warning(i, STR_TIMETABLE_WARNING_NO_SCHEDULED_DISPATCH_ORDER_ASSIGNED);
		}
	}
}


struct TimetableWindow : GeneralVehicleWindow {
	int sel_index;
	bool show_expected;     ///< Whether we show expected arrival or scheduled
	uint deparr_time_width; ///< The width of the departure/arrival time
	uint deparr_abbr_width; ///< The width of the departure/arrival abbreviation
	int clicked_widget;     ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	Scrollbar *vscroll;
	bool query_is_speed_query; ///< The currently open query window is a speed query and not a time query.
	bool set_start_date_all;   ///< Set start date using minutes text entry: this is a set all vehicle (ctrl-click) action
	bool change_timetable_all; ///< Set wait time or speed for all timetable entries (ctrl-click) action
	int summary_warnings = 0;  ///< NUmber of summary warnings shown

	enum {
		MAX_SUMMARY_WARNINGS = 10,
	};

	TimetableWindow(WindowDesc *desc, WindowNumber window_number) :
			GeneralVehicleWindow(desc, Vehicle::Get(window_number)),
			sel_index(-1),
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
			MarkDirtyFocusedRoutePaths(this->vehicle);
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
		Ticks start_time = -(Ticks)v->current_order_time;
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
				SetDParamMaxValue(0, _settings_time.time_in_minutes ? 0 : MAX_YEAR * DAYS_IN_YEAR);
				this->deparr_time_width = GetStringBoundingBox(STR_JUST_DATE_WALLCLOCK_TINY).width + 4;
				this->deparr_abbr_width = std::max(GetStringBoundingBox(STR_TIMETABLE_ARRIVAL_ABBREVIATION).width, GetStringBoundingBox(STR_TIMETABLE_DEPARTURE_ABBREVIATION).width);
				size->width = this->deparr_abbr_width + WidgetDimensions::scaled.hsep_wide + this->deparr_time_width + padding.width;
				FALLTHROUGH;

			case WID_VT_ARRIVAL_DEPARTURE_SELECTION:
			case WID_VT_TIMETABLE_PANEL:
				resize->height = std::max<int>(FONT_HEIGHT_NORMAL, GetSpriteSize(SPR_LOCK).height);
				size->height = 8 * resize->height + padding.height;
				break;

			case WID_VT_SUMMARY_PANEL: {
				Dimension d = GetSpriteSize(SPR_WARNING_SIGN);
				size->height = 2 * FONT_HEIGHT_NORMAL + std::min<int>(MAX_SUMMARY_WARNINGS, this->summary_warnings) * std::max<int>(d.height, FONT_HEIGHT_NORMAL) + padding.height;
				break;
			}
		}
	}

	int GetOrderFromTimetableWndPt(int y, const Vehicle *v)
	{
		int sel = (y - this->GetWidget<NWidgetBase>(WID_VT_TIMETABLE_PANEL)->pos_y - WidgetDimensions::scaled.framerect.top) / std::max<int>(FONT_HEIGHT_NORMAL, GetSpriteSize(SPR_LOCK).height);

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

	virtual EventState OnCTRLStateChange() override
	{
		this->UpdateSelectionStates();
		this->SetDirty();
		return ES_NOT_HANDLED;
	}

	void SetButtonDisabledStates()
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		this->vscroll->SetCount(v->GetNumOrders() * 2);

		if (v->owner == _local_company) {
			bool disable = true;
			bool disable_time = true;
			bool wait_lockable = false;
			bool wait_locked = false;
			bool clearable_when_wait_locked = false;
			if (selected != -1) {
				const Order *order = v->GetOrder(((selected + 1) / 2) % v->GetNumOrders());
				if (selected % 2 != 0) {
					/* Travel time */
					disable = order != nullptr && (order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT) || order->HasNoTimetableTimes());
					disable_time = disable;
					wait_lockable = !disable;
					wait_locked = wait_lockable && order->IsTravelFixed();
				} else {
					/* Wait time */
					if (order != nullptr) {
						if (order->IsType(OT_GOTO_WAYPOINT)) {
							disable = false;
							disable_time = false;
							clearable_when_wait_locked = true;
						} else if (order->IsType(OT_CONDITIONAL)) {
							disable = true;
							disable_time = false;
							clearable_when_wait_locked = true;
						} else {
							disable = (!(order->IsType(OT_GOTO_STATION) || (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_HALT))) ||
									(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION));
							disable_time = disable;
						}
					} else {
						disable = true;
						disable_time = true;
					}
					wait_lockable = !disable_time;
					wait_locked = wait_lockable && order->IsWaitFixed();
				}
			}
			bool disable_speed = disable || selected % 2 == 0 || v->type == VEH_AIRCRAFT;

			this->SetWidgetDisabledState(WID_VT_CHANGE_TIME, disable_time || (HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE) && !wait_locked));
			this->SetWidgetDisabledState(WID_VT_CLEAR_TIME, disable_time || (HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE) && !(wait_locked && clearable_when_wait_locked)));
			this->SetWidgetDisabledState(WID_VT_CHANGE_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_CLEAR_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_SHARED_ORDER_LIST, !(v->IsOrderListShared() || _settings_client.gui.enable_single_veh_shared_order_gui));

			this->SetWidgetDisabledState(WID_VT_START_DATE, v->orders == nullptr || HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) || HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
			this->SetWidgetDisabledState(WID_VT_RESET_LATENESS, v->orders == nullptr);
			this->SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders == nullptr || HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE));
			this->SetWidgetDisabledState(WID_VT_AUTO_SEPARATION, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
			this->EnableWidget(WID_VT_AUTOMATE);
			this->EnableWidget(WID_VT_ADD_VEH_GROUP);
			this->SetWidgetDisabledState(WID_VT_LOCK_ORDER_TIME, !wait_lockable);
			this->SetWidgetLoweredState(WID_VT_LOCK_ORDER_TIME, wait_locked);
			this->SetWidgetDisabledState(WID_VT_EXTRA, disable || (selected % 2 != 0));
			this->SetWidgetDisabledState(WID_VT_ASSIGN_SCHEDULE, disable || (selected % 2 != 0) || !HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
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
			this->DisableWidget(WID_VT_ASSIGN_SCHEDULE);
		}

		this->SetWidgetLoweredState(WID_VT_AUTOFILL, HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE));
		this->SetWidgetLoweredState(WID_VT_AUTOMATE, HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE));
		this->SetWidgetLoweredState(WID_VT_AUTO_SEPARATION, HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION));
		this->SetWidgetLoweredState(WID_VT_SCHEDULED_DISPATCH, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
		this->SetWidgetLoweredState(WID_VT_SCHEDULED_DISPATCH, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));

		this->SetWidgetDisabledState(WID_VT_SCHEDULED_DISPATCH, v->orders == nullptr);
		this->GetWidget<NWidgetStacked>(WID_VT_START_DATE_SELECTION)->SetDisplayedPlane(HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) ? 1 : 0);
	}

	void OnPaint() override
	{
		this->SetButtonDisabledStates();
		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_VT_CAPTION: SetDParam(0, this->vehicle->index); break;
			case WID_VT_EXPECTED: SetDParam(0, this->show_expected ? STR_TIMETABLE_EXPECTED : STR_TIMETABLE_SCHEDULED); break;
		}
	}

	bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_VT_CHANGE_TIME: {
				GuiShowTooltips(this, STR_TIMETABLE_WAIT_TIME_TOOLTIP, 0, nullptr, close_cond);
				return true;
			}
			case WID_VT_CLEAR_TIME: {
				GuiShowTooltips(this, STR_TIMETABLE_CLEAR_TIME_TOOLTIP, 0, nullptr, close_cond);
				return true;
			}
			case WID_VT_CHANGE_SPEED: {
				GuiShowTooltips(this, STR_TIMETABLE_CHANGE_SPEED_TOOLTIP, 0, nullptr, close_cond);
				return true;
			}
			case WID_VT_CLEAR_SPEED: {
				GuiShowTooltips(this, STR_TIMETABLE_CLEAR_SPEED_TOOLTIP, 0, nullptr, close_cond);
				return true;
			}
			default:
				return false;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		switch (widget) {
			case WID_VT_TIMETABLE_PANEL: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				int i = this->vscroll->GetPosition();
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = std::max<int>(FONT_HEIGHT_NORMAL, lock_d.height);
				VehicleOrderID order_id = (i + 1) / 2;
				bool final_order = false;

				bool rtl = _current_text_dir == TD_RTL;
				SetDParamMaxValue(0, v->GetNumOrders(), 2);
				int index_column_width = GetStringBoundingBox(STR_ORDER_INDEX).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + WidgetDimensions::scaled.hsep_normal;
				int middle = rtl ? tr.right - index_column_width : tr.left + index_column_width;

				const Order *order = v->GetOrder(order_id);
				while (order != nullptr) {
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						DrawOrderString(v, order, order_id, tr.top, i == selected, true, tr.left, middle, tr.right);

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
						if (order->IsType(OT_CONDITIONAL) || order->HasNoTimetableTimes()) {
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
						SetDParam(2, PackVelocity(order->GetMaxSpeed(), v->type));

						int edge = DrawString(rtl ? tr.left : middle, rtl ? middle : tr.right, tr.top, string, colour);

						if (order->IsTravelFixed()) {
							Dimension lock_d = GetSpriteSize(SPR_LOCK);
							DrawPixelInfo tmp_dpi;
							if (FillDrawPixelInfo(&tmp_dpi, rtl ? tr.left : middle, tr.top, rtl ? middle : tr.right, lock_d.height)) {
								AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);

								DrawSprite(SPR_LOCK, PAL_NONE, rtl ? edge - 3 - lock_d.width - tr.left : edge + 3 - middle, 0);
							}
						}

						if (final_order) break;
					}

					i++;
					tr.top += line_height;
				}
				break;
			}

			case WID_VT_ARRIVAL_DEPARTURE_PANEL: {
				/* Arrival and departure times are handled in an all-or-nothing approach,
				 * i.e. are only shown if we can calculate all times.
				 * Excluding order lists with only one order makes some things easier.
				 */
				Ticks total_time = v->orders != nullptr ? v->orders->GetTimetableDurationIncomplete() : 0;
				if (total_time <= 0 || v->GetNumOrders() <= 1 || !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) break;

				TimetableArrivalDeparture *arr_dep = AllocaM(TimetableArrivalDeparture, v->GetNumOrders());
				const VehicleOrderID cur_order = v->cur_real_order_index % v->GetNumOrders();

				VehicleOrderID earlyID = BuildArrivalDepartureList(v, arr_dep) ? cur_order : (VehicleOrderID)INVALID_VEH_ORDER_ID;

				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = std::max<int>(FONT_HEIGHT_NORMAL, lock_d.height);

				bool show_late = this->show_expected && v->lateness_counter > DATE_UNIT_SIZE;
				Ticks offset = show_late ? 0 : -v->lateness_counter;

				bool rtl = _current_text_dir == TD_RTL;
				Rect abbr = tr.WithWidth(this->deparr_abbr_width, rtl);
				Rect time = tr.WithWidth(this->deparr_time_width, !rtl);

				for (int i = this->vscroll->GetPosition(); i / 2 < v->GetNumOrders(); ++i) { // note: i is also incremented in the loop
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						if (arr_dep[i / 2].arrival != INVALID_TICKS) {
							DrawString(abbr.left, abbr.right, tr.top, STR_TIMETABLE_ARRIVAL_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							if (this->show_expected && i / 2 == earlyID) {
								SetDParam(0, _scaled_date_ticks + arr_dep[i / 2].arrival);
								DrawString(time.left, time.right, tr.top, STR_JUST_DATE_WALLCLOCK_TINY, TC_GREEN);
							} else {
								SetDParam(0, _scaled_date_ticks + arr_dep[i / 2].arrival + (HasBit(arr_dep[i / 2].flags, TADF_ARRIVAL_NO_OFFSET) ? 0 : offset));
								DrawString(time.left, time.right, tr.top, STR_JUST_DATE_WALLCLOCK_TINY,
										HasBit(arr_dep[i / 2].flags, TADF_ARRIVAL_PREDICTED) ? (TextColour)(TC_IS_PALETTE_COLOUR | TC_NO_SHADE | 4) : (show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK));
							}
						}
					} else {
						if (arr_dep[i / 2].departure != INVALID_TICKS) {
							DrawString(abbr.left, abbr.right, tr.top, STR_TIMETABLE_DEPARTURE_ABBREVIATION, i == selected ? TC_WHITE : TC_BLACK);
							SetDParam(0, _scaled_date_ticks + arr_dep[i/2].departure + (HasBit(arr_dep[i / 2].flags, TADF_DEPARTURE_NO_OFFSET) ? 0 : offset));
							DrawString(time.left, time.right, tr.top, STR_JUST_DATE_WALLCLOCK_TINY,
									HasBit(arr_dep[i / 2].flags, TADF_DEPARTURE_PREDICTED) ? (TextColour)(TC_IS_PALETTE_COLOUR | TC_NO_SHADE | 4) : (show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK));
						}
					}
					tr.top += line_height;
				}
				break;
			}

			case WID_VT_SUMMARY_PANEL: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

				Ticks total_time = v->orders != nullptr ? v->orders->GetTimetableDurationIncomplete() : 0;
				if (total_time != 0) {
					SetTimetableParams(0, total_time);
					DrawString(tr, v->orders->IsCompleteTimetable() ? STR_TIMETABLE_TOTAL_TIME : STR_TIMETABLE_TOTAL_TIME_INCOMPLETE);
				}
				tr.top += FONT_HEIGHT_NORMAL;

				if (v->timetable_start != 0) {
					/* We are running towards the first station so we can start the
					 * timetable at the given time. */
					SetDParam(0, STR_JUST_DATE_WALLCLOCK_TINY);
					SetDParam(1, DateTicksToScaledDateTicks(v->timetable_start) + v->timetable_start_subticks);
					DrawString(tr, STR_TIMETABLE_STATUS_START_AT);
				} else if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
					/* We aren't running on a timetable yet, so how can we be "on time"
					 * when we aren't even "on service"/"on duty"? */
					DrawString(tr, STR_TIMETABLE_STATUS_NOT_STARTED);
				} else if (v->lateness_counter == 0 || (!_settings_client.gui.timetable_in_ticks && v->lateness_counter / DATE_UNIT_SIZE == 0)) {
					DrawString(tr, STR_TIMETABLE_STATUS_ON_TIME);
				} else {
					SetTimetableParams(0, abs(v->lateness_counter));
					DrawString(tr, v->lateness_counter < 0 ? STR_TIMETABLE_STATUS_EARLY : STR_TIMETABLE_STATUS_LATE);
				}
				tr.top += FONT_HEIGHT_NORMAL;

				{
					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					const int step_height = std::max<int>(warning_dimensions.height, FONT_HEIGHT_NORMAL);
					const int text_offset_y = (step_height - FONT_HEIGHT_NORMAL) / 2;
					const int warning_offset_y = (step_height - warning_dimensions.height) / 2;
					const bool rtl = _current_text_dir == TD_RTL;

					auto draw_warning = [&](StringID text, bool warning) {
						int left = tr.left;
						int right = tr.right;
						if (warning) {
							DrawSprite(SPR_WARNING_SIGN, 0, rtl ? right - warning_dimensions.width - 5 : left + 5, tr.top + warning_offset_y);
							if (rtl) {
								right -= (warning_dimensions.width + 10);
							} else {
								left += (warning_dimensions.width + 10);
							}
						}
						DrawString(left, right, tr.top + text_offset_y, text);
						tr.top += step_height;
					};

					int warning_count = 0;
					int warning_limit = this->summary_warnings > MAX_SUMMARY_WARNINGS ? MAX_SUMMARY_WARNINGS - 1 : std::min<int>(MAX_SUMMARY_WARNINGS, this->summary_warnings);

					ProcessTimetableWarnings(v, [&](StringID text, bool warning) {
						if (warning_count < warning_limit) draw_warning(text, warning);
						warning_count++;
					});
					if (warning_count > warning_limit) {
						SetDParam(0, warning_count - warning_limit);
						draw_warning(STR_TIMETABLE_WARNINGS_OMITTED, true);
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
					this->SetButtonDisabledStates();
					if (!this->IsWidgetDisabled(WID_VT_CHANGE_TIME)) {
						this->OnClick(pt, WID_VT_CHANGE_TIME, click_count);
					}
					return;
				} else {
					this->sel_index = (selected == INVALID_ORDER || selected == this->sel_index) ? -1 : selected;
				}

				this->DeleteChildWindows();
				break;
			}

			case WID_VT_START_DATE: // Change the date that the timetable starts.
				if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					this->set_start_date_all = v->orders->IsCompleteTimetable() && _ctrl_pressed;
					StringID str = STR_JUST_INT;
					uint64 time = _scaled_date_ticks;
					time /= _settings_time.ticks_per_minute;
					time += _settings_time.clock_offset;
					time %= (24 * 60);
					time = (time % 60) + (((time / 60) % 24) * 100);
					SetDParam(0, time);
					ShowQueryString(str, STR_TIMETABLE_STARTING_DATE, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				} else {
					ShowSetDateWindow(this, v->index | (v->orders->IsCompleteTimetable() && _ctrl_pressed ? 1U << 20 : 0),
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
					uint time = (selected % 2 != 0) ? order->GetTravelTime() : order->GetWaitTime();
					if (!_settings_client.gui.timetable_in_ticks) time /= DATE_UNIT_SIZE;

					if (time != 0) {
						SetDParam(0, time);
						current = STR_JUST_INT;
					}
				}

				this->query_is_speed_query = false;
				this->change_timetable_all = (order != nullptr) && (selected % 2 == 0) && _ctrl_pressed;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				ShowQueryString(current, STR_TIMETABLE_CHANGE_TIME, 31, this, charset_filter, QSF_ACCEPT_UNCHANGED);
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
						SetDParam(0, ConvertKmhishSpeedToDisplaySpeed(order->GetMaxSpeed(), v->type));
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
				DoCommandP(0, v->index | (_ctrl_pressed ? 1 << 20 : 0), 0, CMD_SET_VEHICLE_ON_TIME | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
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

			case WID_VT_ASSIGN_SCHEDULE: {
				VehicleOrderID real = (this->sel_index + 1) / 2;
				if (real >= this->vehicle->GetNumOrders()) real = 0;
				const Order *order = this->vehicle->GetOrder(real);
				DropDownList list;
				list.emplace_back(new DropDownListStringItem(STR_TIMETABLE_ASSIGN_SCHEDULE_NONE, -1, false));

				for (uint i = 0; i < v->orders->GetScheduledDispatchScheduleCount(); i++) {
					const DispatchSchedule &ds = this->vehicle->orders->GetDispatchScheduleByIndex(i);
					if (ds.ScheduleName().empty()) {
						DropDownListParamStringItem *item = new DropDownListParamStringItem(STR_TIMETABLE_ASSIGN_SCHEDULE_ID, i, false);
						item->SetParam(0, i + 1);
						list.emplace_back(item);
					} else {
						DropDownListCharStringItem *item = new DropDownListCharStringItem(ds.ScheduleName(), i, false);
						list.emplace_back(item);
					}
				}
				ShowDropDownList(this, std::move(list), order->GetDispatchScheduleIndex(), WID_VT_ASSIGN_SCHEDULE);
				break;
			}
		}

		this->SetDirty();
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_VT_EXTRA:
				ExecuteTimetableCommand(this->vehicle, false, this->sel_index, MTF_SET_LEAVE_TYPE, index, false);
				break;

			case WID_VT_ASSIGN_SCHEDULE:
				ExecuteTimetableCommand(this->vehicle, false, this->sel_index, MTF_ASSIGN_SCHEDULE, index, false);
				break;

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
				uint32 p2;
				if (this->query_is_speed_query) {
					uint64 display_speed = StrEmpty(str) ? 0 : strtoul(str, nullptr, 10);
					uint64 val = ConvertDisplaySpeedToKmhishSpeed(display_speed, v->type);
					p2 = std::min<uint>(val, UINT16_MAX);
				} else {
					p2 = ParseTimetableDuration(str);
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
				DoCommandP(0, VehicleListIdentifier(VL_SINGLE_VEH, v->type, v->owner, v->index).Pack(), CF_ANY, CMD_CREATE_GROUP_FROM_LIST | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), nullptr, str);
				break;
			}
		}
	}

	void OnResize() override
	{
		/* Update the scroll bar */
		this->vscroll->SetCapacityFromWidget(this, WID_VT_TIMETABLE_PANEL, WidgetDimensions::scaled.framerect.Vertical());
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
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	virtual void OnFocusLost(Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
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
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_START_DATE_SELECTION),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_START_DATE), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_STARTING_DATE, STR_TIMETABLE_STARTING_DATE_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VT_ASSIGN_SCHEDULE), SetResize(1, 0), SetFill(1, 1), SetDataTip(STR_TIMETABLE_ASSIGN_SCHEDULE_DROP_DOWN, STR_TIMETABLE_ASSIGN_SCHEDULE_DROP_DOWN_TOOLTIP),
				EndContainer(),
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

void SetTimetableWindowsDirty(const Vehicle *v, SetTimetableWindowsDirtyFlags flags)
{
	v = v->FirstShared();
	for (Window *w : Window::IterateFromBack()) {
		if (w->window_class == WC_VEHICLE_TIMETABLE ||
				((flags & STWDF_SCHEDULED_DISPATCH) && w->window_class == WC_SCHDISPATCH_SLOTS) ||
				((flags & STWDF_ORDERS) && w->window_class == WC_VEHICLE_ORDERS)) {
			if (static_cast<GeneralVehicleWindow *>(w)->vehicle->FirstShared() == v) w->SetDirty();
		}
	}
}
