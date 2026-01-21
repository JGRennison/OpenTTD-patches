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
#include "string_func_extra.h"
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
#include "scope.h"
#include "timetable_cmd.h"
#include "group_cmd.h"
#include "core/backup_type.hpp"
#include "core/string_consumer.hpp"

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
 * Create the timetable parameters in the format as described by the setting.
 * @param ticks  the number of ticks to 'draw'
 * @param long_mode long output format
 */
std::pair<struct StringParameter, struct StringParameter> GetTimetableParameters(Ticks ticks, bool long_mode)
{
	return { long_mode ? STR_JUST_TT_TICKS_LONG : STR_JUST_TT_TICKS, ticks };
}

Ticks ParseTimetableDuration(std::string_view str)
{
	if (str.empty()) return 0;

	if (_settings_client.gui.timetable_in_ticks) {
		return IntFromChars<int32_t>(str, true).value_or(0);
	}

	format_buffer_sized<64> tmp_buffer;
	str_replace_wchar(tmp_buffer, str, GetDecimalSeparatorChar(), '.');
	return atof(tmp_buffer.c_str()) * TimetableDisplayUnitSize();
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
	for (VehicleOrderID i = 0; i < v->GetNumOrders(); ++i) {
		table[i].arrival = table[i].departure = INVALID_TICKS;
		table[i].flags = 0;
	}

	Ticks sum = offset;
	VehicleOrderID i = start;
	const Order *order = v->GetOrder(i);

	bool predicted = false;
	bool no_offset = false;
	bool skip_travel = false;
	bool reached_depot = false;

	btree::btree_map<uint, LastDispatchRecord> dispatch_records;

	/* Backup all DispatchSchedule positions for this order list, so that positions can be modified
	 * during timetable traversal to allow conditional order prediction. */
	const uint schedule_count = v->orders->GetScheduledDispatchScheduleCount();
	auto schedule_position_backups = std::make_unique<DispatchSchedule::PositionBackup[]>(schedule_count);
	for (uint i = 0; i < schedule_count; i++) {
		schedule_position_backups[i] = v->orders->GetDispatchScheduleByIndex(i).BackupPosition();
	}
	auto guard = scope_guard([&]() {
		for (uint i = 0; i < schedule_count; i++) {
			v->orders->GetDispatchScheduleByIndex(i).RestorePosition(schedule_position_backups[i]);
		}
	});

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
					StateTicks time = _state_ticks + sum;
					if (!no_offset) time -= v->lateness_counter;
					int value = GetTraceRestrictTimeDateValueFromStateTicks(static_cast<TraceRestrictTimeDateValueField>(order->GetConditionValue()), time);
					jump = OrderConditionCompare(order->GetConditionComparator(), value, order->GetXData());
					break;
				}

				case OCV_DISPATCH_SLOT: {
					StateTicks time = _state_ticks + sum;
					if (!no_offset) time -= v->lateness_counter;

					auto get_vehicle_records = [&](uint16_t schedule_index) -> const LastDispatchRecord * {
						auto record = dispatch_records.find(schedule_index);
						if (record != dispatch_records.end()) {
							/* dispatch_records contains a last dispatch entry, use that instead of the one stored in the vehicle */
							return &(record->second);
						} else {
							return GetVehicleLastDispatchRecord(v, schedule_index);
						}
					};
					OrderConditionEvalResult result = EvaluateDispatchSlotConditionalOrder(order, v->orders->GetScheduledDispatchScheduleSet(), time, get_vehicle_records);
					if (result.IsPredicted()) predicted = true;
					jump = result.GetResult();
					break;
				}

				case OCV_REQUIRES_SERVICE: {
					bool requires_service = reached_depot ? false : v->NeedsServicing();
					jump = OrderConditionCompare(order->GetConditionComparator(), requires_service, order->GetConditionValue());
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
		} else if (order->IsType(OT_GOTO_DEPOT)) {
			reached_depot = true;
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

			if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && order->IsScheduledDispatchOrder(true) && !(i == start && !travelling)) {
				if (!no_offset) sum -= v->lateness_counter;
				DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(order->GetDispatchScheduleIndex());
				ds.UpdateScheduledDispatchToDate(_state_ticks + sum);

				StateTicks slot;
				int slot_index;
				std::tie(slot, slot_index) = GetScheduledDispatchTime(ds, _state_ticks + sum + order->GetTimetabledWait());

				if (slot == INVALID_STATE_TICKS) return;
				sum = (slot - _state_ticks).AsTicks();
				predicted = true;
				no_offset = true;

				ds.SetScheduledDispatchLastDispatch((slot - ds.GetScheduledDispatchStartTick()).AsTicks());

				extern LastDispatchRecord MakeLastDispatchRecord(const DispatchSchedule &ds, StateTicks slot, int slot_index);
				dispatch_records[order->GetDispatchScheduleIndex()] = MakeLastDispatchRecord(ds, slot, slot_index);
			} else {
				if (!CanDetermineTimeTaken(order, false)) return;
				sum += order->GetTimetabledWait();
			}
			table[i].departure = sum;
			if (predicted) SetBit(table[i].flags, TADF_DEPARTURE_PREDICTED);
			if (no_offset) SetBit(table[i].flags, TADF_DEPARTURE_NO_OFFSET);
		}

		skip_travel = false;

		v->orders->AdvanceOrderWithIndex(order, i);
	} while (i != start);

	/* When loading at a scheduled station we still have to treat the
	 * travelling part of the first order. */
	if (!travelling && table[i].arrival == INVALID_TICKS) {
		if (!skip_travel) {
			if (!CanDetermineTimeTaken(order, true)) return;
			sum += order->GetTimetabledTravel();
		}
		table[i].arrival = sum;
		if (predicted) SetBit(table[i].flags, TADF_ARRIVAL_PREDICTED);
		if (no_offset) SetBit(table[i].flags, TADF_ARRIVAL_NO_OFFSET);
	}
}

/**
 * Callback for when a time has been chosen to start the time table
 * @param w the window related to the setting of the date
 * @param tick the actually chosen tick
 * @param callback_data callback data
 */
static void ChangeTimetableStartCallback(const Window *w, StateTicks tick, void *callback_data)
{
	Command<CMD_SET_TIMETABLE_START>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, w->window_number, reinterpret_cast<uintptr_t>(callback_data) != 0, tick);
}

void ProcessTimetableWarnings(const Vehicle *v, std::function<void(std::string_view, bool)> handler_func, bool no_text = false)
{
	Ticks total_time = v->orders != nullptr ? v->orders->GetTimetableDurationIncomplete() : 0;

	auto handler = [&](StringID str, bool warning) {
		if (no_text) {
			handler_func({}, warning);
		} else {
			handler_func(GetString(str), warning);
		}
	};

	bool have_conditional = false;
	bool have_missing_wait = false;
	bool have_missing_travel = false;
	bool have_bad_full_load = false;
	bool have_non_timetabled_conditional_branch = false;
	bool have_autoseparate_bad_non_stop_type = false;

	const bool assume_timetabled = v->vehicle_flags.Test(VehicleFlag::AutofillTimetable) || v->vehicle_flags.Test(VehicleFlag::AutomateTimetable);
	for (const Order *order : v->Orders()) {
		if (order->IsType(OT_CONDITIONAL)) {
			have_conditional = true;
			if (!order->IsWaitTimetabled()) have_non_timetabled_conditional_branch = true;
		} else {
			if (order->GetWaitTime() == 0 && !order->IsWaitTimetabled() && order->IsType(OT_GOTO_STATION) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
				have_missing_wait = true;
			}
			if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
				have_missing_travel = true;
			}
		}

		if (order->IsType(OT_GOTO_STATION) && !have_bad_full_load && (assume_timetabled || order->IsWaitTimetabled())) {
			if (order->GetLoadType() & OLFB_FULL_LOAD) have_bad_full_load = true;
			if (order->GetLoadType() == OLFB_CARGO_TYPE_LOAD) {
				for (CargoType c = 0; c < NUM_CARGO; c++) {
					if (order->GetCargoLoadTypeRaw(c) & OLFB_FULL_LOAD) {
						have_bad_full_load = true;
						break;
					}
				}
			}
		}

		if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) && !have_autoseparate_bad_non_stop_type && v->IsGroundVehicle()) {
			if (order->IsType(OT_IMPLICIT)) {
				have_autoseparate_bad_non_stop_type = true;
			} else if (order->IsGotoOrder() && (order->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) == 0) {
				have_autoseparate_bad_non_stop_type = true;
			}
		}
	}

	if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
		auto no_set_waiting_orders = [&]() -> bool {
			for (const Order *o : v->Orders()) {
				if (o->IsType(OT_IMPLICIT) || o->HasNoTimetableTimes()) continue;

				if (o->IsWaitTimetabled() && o->GetWaitTime() > 0 && ((o->IsType(OT_GOTO_STATION) && !(o->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) ||
						(o->IsType(OT_GOTO_WAYPOINT) && v->type == VEH_TRAIN))) {
					return false;
				}
			}
			return true;
		};

		if (have_conditional) handler(STR_TIMETABLE_WARNING_AUTOSEP_CONDITIONAL, true);
		if (have_autoseparate_bad_non_stop_type) handler(STR_TIMETABLE_WARNING_AUTOSEP_WRONG_STOP_TYPE, true);
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
		} else if (no_set_waiting_orders()) {
			handler(STR_TIMETABLE_AUTOSEP_NO_WAIT_TIME_SET, true);
		} else if (!have_conditional) {
			handler(v->IsOrderListShared() ? STR_TIMETABLE_AUTOSEP_OK : STR_TIMETABLE_AUTOSEP_SINGLE_VEH, false);
		}
	}
	if (have_bad_full_load) handler(STR_TIMETABLE_WARNING_FULL_LOAD, true);
	if (have_conditional && v->vehicle_flags.Test(VehicleFlag::AutofillTimetable)) handler(STR_TIMETABLE_WARNING_AUTOFILL_CONDITIONAL, true);
	if (total_time && have_non_timetabled_conditional_branch) handler(STR_TIMETABLE_NON_TIMETABLED_BRANCH, false);
	if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && v->orders != nullptr) {
		auto sd_warning = [&](int schedule_index, StringID str) {
			if (no_text) {
				handler_func({}, true);
			} else if (v->orders->GetScheduledDispatchScheduleCount() > 1) {
				handler_func(GetString(STR_TIMETABLE_WARNING_SCHEDULE_ID, schedule_index + 1, str), true);
			} else {
				handler_func(GetString(str), true);
			}
		};
		std::vector<bool> seen_sched_dispatch_orders(v->orders->GetScheduledDispatchScheduleCount());

		for (const Order *order : v->Orders()) {
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

bool HaveTimetableWarnings(const Vehicle *v)
{
	bool show_warning = false;
	ProcessTimetableWarnings(v, [&](std::string_view text, bool warning) {
		if (warning) show_warning = true;
	}, true);
	return show_warning;
}

struct TimetableWindow : GeneralVehicleWindow {
	int sel_index = -1;
	bool show_expected = true;         ///< Whether we show expected arrival or scheduled
	uint deparr_time_width = 0;        ///< The width of the departure/arrival time
	uint deparr_abbr_width = 0;        ///< The width of the departure/arrival abbreviation
	int clicked_widget = -1;           ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	Scrollbar *vscroll = nullptr;
	bool query_is_speed_query = false; ///< The currently open query window is a speed query and not a time query.
	bool set_start_date_all = false;   ///< Set start date using minutes text entry: this is a set all vehicle (ctrl-click) action
	bool change_timetable_all = false; ///< Set wait time or speed for all timetable entries (ctrl-click) action
	int summary_warnings = 0;          ///< Number of summary warnings shown

	enum {
		MAX_SUMMARY_WARNINGS = 10,
	};

	TimetableWindow(WindowDesc &desc, WindowNumber window_number) :
			GeneralVehicleWindow(desc, Vehicle::Get(window_number))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_VT_SCROLLBAR);
		this->UpdateSelectionStates();
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
	}

	void Close(int data = 0) override
	{
		FocusWindowById(WC_VEHICLE_VIEW, this->window_number);
		this->GeneralVehicleWindow::Close();
	}

	/**
	 * Build the arrival-departure list for a given vehicle
	 * @param v the vehicle to make the list for
	 * @param table the table to fill
	 * @return if next arrival will be early
	 */
	static bool BuildArrivalDepartureList(const Vehicle *v, TimetableArrivalDeparture *table)
	{
		assert(v->vehicle_flags.Test(VehicleFlag::TimetableStarted));

		bool travelling = (!(v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) || v->current_order.GetNonStopType() == ONSF_STOP_EVERYWHERE);
		Ticks start_time = -(Ticks)v->current_order_time;
		if (v->cur_timetable_order_index != INVALID_VEH_ORDER_ID && v->cur_timetable_order_index != v->cur_real_order_index) {
			/* vehicle is taking a conditional order branch, adjust start time to compensate */
			const Order *real_current_order = v->GetOrder(v->cur_real_order_index);
			const Order *real_timetable_order = v->GetOrder(v->cur_timetable_order_index);
			assert(real_timetable_order->IsType(OT_CONDITIONAL));
			start_time += real_timetable_order->GetWaitTime(); // NB: wait and travel times are unsigned
			start_time -= real_current_order->GetTravelTime();
		}

		FillTimetableArrivalDepartureTable(v, v->cur_real_order_index % v->GetNumOrders(), travelling, table, start_time);

		return (travelling && v->lateness_counter < 0);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_VT_ARRIVAL_DEPARTURE_PANEL: {
				int64_t param;
				if (_settings_time.time_in_minutes) {
					param = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(GetBroadestHourDigitsValue(), (int)GetParamMaxDigits(2))).base();
				} else if (EconTime::UsingWallclockUnits()) {
					param = _state_ticks.base() + (TICKS_PER_SECOND * 9999);
				} else {
					param = EconTime::MAX_YEAR.base() * DAYS_IN_YEAR;
				}

				format_buffer buf;
				auto get_width = [&](StringID str) {
					buf.clear();
					AppendStringInPlace(buf, str, param);
					return GetStringBoundingBox(buf).width;
				};
				this->deparr_time_width = get_width(STR_JUST_TT_TIME);
				this->deparr_abbr_width = std::max(get_width(STR_TIMETABLE_ARRIVAL_ABBREVIATION), get_width(STR_TIMETABLE_DEPARTURE_ABBREVIATION));
				size.width = this->deparr_abbr_width + this->deparr_time_width + padding.width;
				[[fallthrough]];
			}

			case WID_VT_TIMETABLE_PANEL:
				fill.height = resize.height = std::max<int>(GetCharacterHeight(FS_NORMAL), GetSpriteSize(SPR_LOCK).height);
				size.height = 8 * resize.height + padding.height;
				break;

			case WID_VT_SUMMARY_PANEL: {
				Dimension d = GetSpriteSize(SPR_WARNING_SIGN);
				size.height = 2 * GetCharacterHeight(FS_NORMAL) + std::min<int>(MAX_SUMMARY_WARNINGS, this->summary_warnings) * std::max<int>(d.height, GetCharacterHeight(FS_NORMAL)) + padding.height;
				break;
			}
		}
	}

	int GetOrderFromTimetableWndPt(int y, [[maybe_unused]] const Vehicle *v)
	{
		int32_t sel = this->vscroll->GetScrolledRowFromWidget(y, this, WID_VT_TIMETABLE_PANEL, WidgetDimensions::scaled.framerect.top);
		if (sel == INT32_MAX) return sel;
		assert(IsInsideBS(sel, 0, v->GetNumOrders() * 2));
		return sel;
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		switch (data) {
			case VIWD_AUTOREPLACE:
				/* Autoreplace replaced the vehicle */
				this->vehicle = Vehicle::Get(this->window_number);
				break;

			case VIWD_REMOVE_ALL_ORDERS:
				/* Removed / replaced all orders (after deleting / sharing) */
				if (this->sel_index == -1) break;

				this->CloseChildWindows();
				this->sel_index = -1;
				break;

			case VIWD_MODIFY_ORDERS:
				if (!gui_scope) break;
				this->UpdateSelectionStates();
				this->ReInit();
				break;

			default: {
				if (gui_scope) break; // only do this once; from command scope
				this->OnOrderMove(GB(data, 0, 16), GB(data, 16, 16), 1);
			}
		}
	}

	void OnOrderMove(VehicleOrderID from, VehicleOrderID to, uint16_t count)
	{
		/* Moving an order. If one of these is INVALID_VEH_ORDER_ID, then
		 * the order is being created / removed */
		if (this->sel_index == -1) return;

		if (from == to || count == 0) return; // no need to change anything

		/* if from == INVALID_VEH_ORDER_ID, one order was added; if to == INVALID_VEH_ORDER_ID, one order was removed */
		uint old_num_orders = this->vehicle->GetNumOrders() - (uint)(from == INVALID_VEH_ORDER_ID) + (uint)(to == INVALID_VEH_ORDER_ID);

		VehicleOrderID selected_order = (this->sel_index + 1) / 2;
		if (selected_order == old_num_orders) selected_order = 0; // when last travel time is selected, it belongs to order 0

		bool travel = HasBit(this->sel_index, 0);

		if (selected_order < from || selected_order >= from + count) {
			/* Moving from preceding order? */
			if (from < selected_order) selected_order -= count;
			/* Moving to   preceding order? */
			if (to <= selected_order) selected_order += count;
		} else {
			/* Now we are modifying the selected order */
			if (to == INVALID_VEH_ORDER_ID) {
				/* Deleting selected order */
				this->CloseChildWindows();
				this->sel_index = -1;
				return;
			} else {
				/* Moving selected order */
				selected_order = to;
			}
		}

		/* recompute new sel_index */
		this->sel_index = 2 * selected_order - (int)travel;
		/* travel time of first order needs special handling */
		if (this->sel_index == -1) this->sel_index = this->vehicle->GetNumOrders() * 2 - 1;
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
		const VehicleOrderID order_count = v->GetNumOrders();
		if (this->sel_index != -1 && order_count == 0) {
			this->sel_index = -1;
		}
		int selected = this->sel_index;

		this->vscroll->SetCount(order_count * 2);

		if (v->owner == _local_company) {
			bool disable = true;
			bool disable_time = true;
			bool wait_lockable = false;
			bool wait_locked = false;
			bool clearable_when_wait_locked = false;
			if (selected != -1) {
				VehicleOrderID order_number = (selected + 1) / 2;
				if (order_number >= order_count) order_number = 0;
				const Order *order = v->GetOrder(order_number);
				if (order != nullptr) {
					if (selected % 2 != 0) {
						/* Travel time */
						disable = order->IsType(OT_CONDITIONAL) || order->IsType(OT_IMPLICIT) || order->HasNoTimetableTimes();
						disable_time = disable;
						wait_lockable = !disable;
						wait_locked = wait_lockable && order->IsTravelFixed();
					} else {
						/* Wait time */
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
						wait_lockable = !disable_time;
						wait_locked = wait_lockable && order->IsWaitFixed();
					}
				}
			}
			bool disable_speed = disable || selected % 2 == 0 || v->type == VEH_AIRCRAFT;

			this->SetWidgetDisabledState(WID_VT_CHANGE_TIME, disable_time || (v->vehicle_flags.Test(VehicleFlag::AutomateTimetable) && !wait_locked));
			this->SetWidgetDisabledState(WID_VT_CLEAR_TIME, disable_time || (v->vehicle_flags.Test(VehicleFlag::AutomateTimetable) && !(wait_locked && clearable_when_wait_locked)));
			this->SetWidgetDisabledState(WID_VT_CHANGE_SPEED, disable_speed);
			this->SetWidgetDisabledState(WID_VT_CLEAR_SPEED, disable_speed);

			this->SetWidgetDisabledState(WID_VT_START_DATE, v->orders == nullptr || v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) || v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch));
			this->SetWidgetDisabledState(WID_VT_RESET_LATENESS, v->orders == nullptr);
			this->SetWidgetDisabledState(WID_VT_AUTOFILL, v->orders == nullptr || v->vehicle_flags.Test(VehicleFlag::AutomateTimetable));
			this->SetWidgetDisabledState(WID_VT_AUTO_SEPARATION, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) || v->HasUnbunchingOrder());
			this->EnableWidget(WID_VT_AUTOMATE);
			this->EnableWidget(WID_VT_ADD_VEH_GROUP);
			this->SetWidgetDisabledState(WID_VT_LOCK_ORDER_TIME, !wait_lockable);
			this->SetWidgetLoweredState(WID_VT_LOCK_ORDER_TIME, wait_locked);
			this->SetWidgetDisabledState(WID_VT_EXTRA, disable || (selected % 2 != 0));
			this->SetWidgetDisabledState(WID_VT_ASSIGN_SCHEDULE, disable || (selected % 2 != 0) || !v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch));
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
			this->DisableWidget(WID_VT_ADD_VEH_GROUP);
			this->DisableWidget(WID_VT_LOCK_ORDER_TIME);
			this->DisableWidget(WID_VT_EXTRA);
			this->DisableWidget(WID_VT_ASSIGN_SCHEDULE);
		}

		this->SetWidgetDisabledState(WID_VT_SHARED_ORDER_LIST, !(v->IsOrderListShared() || _settings_client.gui.enable_single_veh_shared_order_gui));

		this->SetWidgetLoweredState(WID_VT_AUTOFILL, v->vehicle_flags.Test(VehicleFlag::AutofillTimetable));
		this->SetWidgetLoweredState(WID_VT_AUTOMATE, v->vehicle_flags.Test(VehicleFlag::AutomateTimetable));
		this->SetWidgetLoweredState(WID_VT_AUTO_SEPARATION, v->vehicle_flags.Test(VehicleFlag::TimetableSeparation));
		this->SetWidgetLoweredState(WID_VT_SCHEDULED_DISPATCH, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch));
		this->SetWidgetLoweredState(WID_VT_SCHEDULED_DISPATCH, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch));

		this->SetWidgetDisabledState(WID_VT_SCHEDULED_DISPATCH, v->orders == nullptr);
		this->GetWidget<NWidgetStacked>(WID_VT_START_DATE_SELECTION)->SetDisplayedPlane(v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) ? 1 : 0);
	}

	void OnPaint() override
	{
		this->SetButtonDisabledStates();
		this->DrawWidgets();
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_VT_CAPTION: return GetString(STR_TIMETABLE_TITLE, this->vehicle->index);
			case WID_VT_EXPECTED: return GetString(this->show_expected ? STR_TIMETABLE_EXPECTED : STR_TIMETABLE_SCHEDULED);
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	bool OnTooltip(Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_VT_CHANGE_TIME: {
				GuiShowTooltips(this, GetEncodedString(STR_TIMETABLE_WAIT_TIME_TOOLTIP), close_cond);
				return true;
			}
			case WID_VT_CLEAR_TIME: {
				GuiShowTooltips(this, GetEncodedString(STR_TIMETABLE_CLEAR_TIME_TOOLTIP), close_cond);
				return true;
			}
			case WID_VT_CHANGE_SPEED: {
				GuiShowTooltips(this, GetEncodedString(STR_TIMETABLE_CHANGE_SPEED_TOOLTIP), close_cond);
				return true;
			}
			case WID_VT_CLEAR_SPEED: {
				GuiShowTooltips(this, GetEncodedString(STR_TIMETABLE_CLEAR_SPEED_TOOLTIP), close_cond);
				return true;
			}
			case WID_VT_SHARED_ORDER_LIST: {
				if (this->vehicle->owner == _local_company) {
					GuiShowTooltips(this, GetEncodedString(STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP_EXTRA, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP), close_cond);
					return true;
				}
				return false;
			}
			case WID_VT_AUTO_SEPARATION: {
				if (this->vehicle->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
					GuiShowTooltips(this, GetEncodedString(STR_TOOLTIP_SEPARATION_CANNOT_ENABLE, STR_TIMETABLE_AUTO_SEPARATION_TOOLTIP, STR_CANNOT_ENABLE_BECAUSE_SCHED_DISPATCH), close_cond);
				} else if (this->vehicle->HasUnbunchingOrder()) {
					GuiShowTooltips(this, GetEncodedString(STR_TOOLTIP_SEPARATION_CANNOT_ENABLE, STR_TIMETABLE_AUTO_SEPARATION_TOOLTIP, STR_CANNOT_ENABLE_BECAUSE_UNBUNCHING), close_cond);
				} else {
					GuiShowTooltips(this, GetEncodedString(STR_TIMETABLE_AUTO_SEPARATION_TOOLTIP), close_cond);
				}
				return true;
			}

			default:
				return false;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Vehicle *v = this->vehicle;
		int selected = this->sel_index;

		switch (widget) {
			case WID_VT_TIMETABLE_PANEL: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				int i = this->vscroll->GetPosition();
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = std::max<int>(GetCharacterHeight(FS_NORMAL), lock_d.height);
				VehicleOrderID order_id = (i + 1) / 2;
				bool final_order = false;

				format_buffer buffer;

				bool rtl = _current_text_dir == TD_RTL;
				int index_column_width = GetStringBoundingBox(GetStringInPlace(buffer, STR_ORDER_INDEX, GetParamMaxValue(v->GetNumOrders(), 2))).width + 2 * GetSpriteSize(rtl ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT).width + WidgetDimensions::scaled.hsep_normal;
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
							order = v->orders->GetNext(order);
						}
					} else {
						TextColour colour = (i == selected) ? TC_WHITE : TC_BLACK;
						if (order->IsType(OT_CONDITIONAL) || order->HasNoTimetableTimes()) {
							GetStringInPlace(buffer, STR_TIMETABLE_NO_TRAVEL);
						} else if (order->IsType(OT_IMPLICIT)) {
							GetStringInPlace(buffer, STR_TIMETABLE_NOT_TIMETABLEABLE);
							colour = ((i == selected) ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
						} else if (!order->IsTravelTimetabled()) {
							if (order->GetTravelTime() > 0) {
								auto [str, value] = GetTimetableParameters(order->GetTravelTime());
								if (order->GetMaxSpeed() != UINT16_MAX) {
									GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_FOR_SPEED_ESTIMATED, str, value, PackVelocity(order->GetMaxSpeed(), v->type));
								} else {
									GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_FOR_ESTIMATED, str, value);
								}
							} else {
								if (order->GetMaxSpeed() != UINT16_MAX) {
									GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_NOT_TIMETABLED_SPEED, PackVelocity(order->GetMaxSpeed(), v->type));
								} else {
									GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_NOT_TIMETABLED);
								}
							}
						} else {
							auto [str, value] = GetTimetableParameters(order->GetTimetabledTravel());
							if (order->GetMaxSpeed() != UINT16_MAX) {
								GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_FOR_SPEED, str, value, PackVelocity(order->GetMaxSpeed(), v->type));
							} else {
								GetStringInPlace(buffer, STR_TIMETABLE_TRAVEL_FOR, str, value);
							}
						}

						int edge = DrawString(rtl ? tr.left : middle, rtl ? middle : tr.right, tr.top, buffer, colour);

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
				if (total_time <= 0 || v->GetNumOrders() <= 1 || !v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) break;

				std::unique_ptr<TimetableArrivalDeparture[]> arr_dep = std::make_unique<TimetableArrivalDeparture[]>(v->GetNumOrders());
				const VehicleOrderID cur_order = v->cur_real_order_index % v->GetNumOrders();

				VehicleOrderID earlyID = BuildArrivalDepartureList(v, arr_dep.get()) ? cur_order : (VehicleOrderID)INVALID_VEH_ORDER_ID;

				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				Dimension lock_d = GetSpriteSize(SPR_LOCK);
				int line_height = std::max<int>(GetCharacterHeight(FS_NORMAL), lock_d.height);

				const Ticks timetable_unit_size = TimetableDisplayUnitSize();
				bool show_late = this->show_expected && v->lateness_counter >= timetable_unit_size;
				Ticks offset = show_late ? 0 : -v->lateness_counter;

				bool rtl = _current_text_dir == TD_RTL;
				Rect abbr = tr.WithWidth(this->deparr_abbr_width, rtl);
				Rect time = tr.WithWidth(this->deparr_time_width, !rtl);

				format_buffer buffer;
				auto draw_time = [&]<typename... T>(TextColour colour, StringID str, T&&... params) {
					DrawString(time.left, time.right, tr.top, GetStringInPlace(buffer, str, std::forward<T>(params)...), colour);
				};

				std::string arrival_abbr = GetString(STR_TIMETABLE_ARRIVAL_ABBREVIATION);
				std::string departure_abbr = GetString(STR_TIMETABLE_DEPARTURE_ABBREVIATION);

				for (int i = this->vscroll->GetPosition(); i / 2 < v->GetNumOrders(); ++i) { // note: i is also incremented in the loop
					/* Don't draw anything if it extends past the end of the window. */
					if (!this->vscroll->IsVisible(i)) break;

					if (i % 2 == 0) {
						if (arr_dep[i / 2].arrival != INVALID_TICKS) {
							DrawString(abbr.left, abbr.right, tr.top, arrival_abbr, i == selected ? TC_WHITE : TC_BLACK);
							if (this->show_expected && i / 2 == earlyID) {
								draw_time(TC_GREEN, STR_JUST_TT_TIME, _state_ticks + arr_dep[i / 2].arrival);
							} else {
								draw_time(HasBit(arr_dep[i / 2].flags, TADF_ARRIVAL_PREDICTED) ? (TextColour)(TC_IS_PALETTE_COLOUR | TC_NO_SHADE | 4) : (show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK),
										STR_JUST_TT_TIME,
										_state_ticks + arr_dep[i / 2].arrival + (HasBit(arr_dep[i / 2].flags, TADF_ARRIVAL_NO_OFFSET) ? 0 : offset));
							}
						}
					} else {
						if (arr_dep[i / 2].departure != INVALID_TICKS) {
							DrawString(abbr.left, abbr.right, tr.top, departure_abbr, i == selected ? TC_WHITE : TC_BLACK);
							draw_time(HasBit(arr_dep[i / 2].flags, TADF_DEPARTURE_PREDICTED) ? (TextColour)(TC_IS_PALETTE_COLOUR | TC_NO_SHADE | 4) : (show_late ? TC_RED : i == selected ? TC_WHITE : TC_BLACK),
									STR_JUST_TT_TIME,
									_state_ticks + arr_dep[i/2].departure + (HasBit(arr_dep[i / 2].flags, TADF_DEPARTURE_NO_OFFSET) ? 0 : offset));
						}
					}
					tr.top += line_height;
				}
				break;
			}

			case WID_VT_SUMMARY_PANEL: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

				format_buffer buffer;
				auto draw = [&]<typename... T>(StringID str, T&&... params) {
					DrawString(tr, GetStringInPlace(buffer, str, std::forward<T>(params)...));
				};

				Ticks total_time = v->orders != nullptr ? v->orders->GetTimetableDurationIncomplete() : 0;
				if (total_time != 0) {
					auto params = GetTimetableParameters(total_time, true);
					StringID str;
					if (!v->orders->IsCompleteTimetable()) {
						str = STR_TIMETABLE_TOTAL_TIME_INCOMPLETE;
					} else if (!_settings_client.gui.timetable_in_ticks && _settings_client.gui.timetable_leftover_time == TLT_OFF && total_time % TimetableDisplayUnitSize() != 0) {
						str = STR_TIMETABLE_APPROX_TIME;
					} else {
						str = STR_TIMETABLE_TOTAL_TIME;
					}
					draw(str, params.first, params.second);
				}
				tr.top += GetCharacterHeight(FS_NORMAL);

				if (v->timetable_start != 0) {
					/* We are running towards the first station so we can start the
					 * timetable at the given time. */
					if (EconTime::UsingWallclockUnits() && !_settings_time.time_in_minutes) {
						draw(STR_TIMETABLE_STATUS_START_IN_SECONDS, (v->timetable_start - _state_ticks) / TICKS_PER_SECOND);
					} else {
						draw(STR_TIMETABLE_STATUS_START_AT_DATE, STR_JUST_TT_TIME, v->timetable_start);
					}
				} else if (!v->vehicle_flags.Test(VehicleFlag::TimetableStarted)) {
					/* We aren't running on a timetable yet, so how can we be "on time"
					 * when we aren't even "on service"/"on duty"? */
					draw(STR_TIMETABLE_STATUS_NOT_STARTED);
				} else if (v->lateness_counter == 0 || (!_settings_client.gui.timetable_in_ticks && abs(v->lateness_counter) < TimetableDisplayUnitSize())) {
					draw(STR_TIMETABLE_STATUS_ON_TIME);
				} else {
					auto params = GetTimetableParameters(abs(v->lateness_counter), true);
					draw(v->lateness_counter < 0 ? STR_TIMETABLE_STATUS_EARLY : STR_TIMETABLE_STATUS_LATE, params.first, params.second);
				}
				tr.top += GetCharacterHeight(FS_NORMAL);

				{
					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					const int step_height = std::max<int>(warning_dimensions.height, GetCharacterHeight(FS_NORMAL));
					const int text_offset_y = (step_height - GetCharacterHeight(FS_NORMAL)) / 2;
					const int warning_offset_y = (step_height - warning_dimensions.height) / 2;
					const bool rtl = _current_text_dir == TD_RTL;

					auto draw_warning = [&](std::string_view text, bool warning) {
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

					ProcessTimetableWarnings(v, [&](std::string_view text, bool warning) {
						if (warning_count < warning_limit) draw_warning(text, warning);
						warning_count++;
					});
					if (warning_count > warning_limit) {
						draw_warning(GetStringInPlace(buffer, STR_TIMETABLE_WARNINGS_OMITTED, warning_count - warning_limit), true);
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

	static inline void ExecuteTimetableCommand(const Vehicle *v, bool bulk, uint selected, ModifyTimetableFlags mtf, uint32_t data, bool clear)
	{
		uint order_number = (selected + 1) / 2;
		if (order_number >= v->GetNumOrders()) order_number = 0;

		if (bulk) {
			Command<CMD_BULK_CHANGE_TIMETABLE>::Post(v->index, mtf, data, clear ? MTCF_CLEAR_FIELD : MTCF_NONE);
		} else {
			Command<CMD_CHANGE_TIMETABLE>::Post(v->index, order_number, mtf, data, clear ? MTCF_CLEAR_FIELD : MTCF_NONE);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		const Vehicle *v = this->vehicle;

		this->clicked_widget = widget;
		this->CloseChildWindows(WC_QUERY_STRING);

		switch (widget) {
			case WID_VT_ORDER_VIEW: // Order view button
				ShowOrdersWindow(v);
				return;

			case WID_VT_TIMETABLE_PANEL: { // Main panel.
				int selected = GetOrderFromTimetableWndPt(pt.y, v);

				/* Allow change time by double-clicking order */
				if (click_count == 2) {
					this->sel_index = (selected == OrderID::Invalid()) ? -1 : selected;
					this->SetButtonDisabledStates();
					if (!this->IsWidgetDisabled(WID_VT_CHANGE_TIME)) {
						this->OnClick(pt, WID_VT_CHANGE_TIME, click_count);
					}
					return;
				} else {
					this->sel_index = (selected == INT32_MAX || selected == this->sel_index) ? -1 : selected;
				}

				this->CloseChildWindows();
				break;
			}

			case WID_VT_START_DATE: { // Change the date that the timetable starts.
				bool set_all = _ctrl_pressed && v->orders->IsCompleteTimetable();
				if (EconTime::UsingWallclockUnits() && !_settings_time.time_in_minutes) {
					this->set_start_date_all = set_all;
					ShowQueryString({}, STR_TIMETABLE_START_SECONDS_QUERY, 6, this, CS_NUMERAL, QueryStringFlag::AcceptUnchanged);
				} else if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					this->set_start_date_all = set_all;
					ShowQueryString(GetString(STR_JUST_INT, _settings_time.NowInTickMinutes().ClockHHMM()), STR_TIMETABLE_START, 31, this, CS_NUMERAL, QueryStringFlag::AcceptUnchanged);
				} else {
					ShowSetDateWindow(this, v->index.base(), _state_ticks, EconTime::CurYear(), EconTime::CurYear() + 15,
							ChangeTimetableStartCallback, reinterpret_cast<void *>(static_cast<uintptr_t>(set_all ? 1 : 0)));
				}
				break;
			}

			case WID_VT_CHANGE_TIME: { // "Wait For" button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				const Order *order = v->GetOrder(real);
				std::string current;

				if (order != nullptr) {
					uint time = (selected % 2 != 0) ? order->GetTravelTime() : order->GetWaitTime();
					if (!_settings_client.gui.timetable_in_ticks) time /= TimetableDisplayUnitSize();

					if (time != 0) {
						current = GetString(STR_JUST_INT, time);
					}
				}

				this->query_is_speed_query = false;
				this->change_timetable_all = (order != nullptr) && (selected % 2 == 0) && _ctrl_pressed;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				ShowQueryString(current, STR_TIMETABLE_CHANGE_TIME_QUERY, 31, this, charset_filter, QueryStringFlag::AcceptUnchanged);
				break;
			}

			case WID_VT_CHANGE_SPEED: { // Change max speed button.
				int selected = this->sel_index;
				VehicleOrderID real = (selected + 1) / 2;

				if (real >= v->GetNumOrders()) real = 0;

				std::string current;
				const Order *order = v->GetOrder(real);
				if (order != nullptr) {
					if (order->GetMaxSpeed() != UINT16_MAX) {
						current = GetString(STR_JUST_INT, ConvertKmhishSpeedToDisplaySpeed(order->GetMaxSpeed(), v->type));
					}
				}

				this->query_is_speed_query = true;
				this->change_timetable_all = (order != nullptr) && _ctrl_pressed;
				ShowQueryString(current, STR_TIMETABLE_CHANGE_SPEED_QUERY, 31, this, CS_NUMERAL, {});
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
				Command<CMD_SET_VEHICLE_ON_TIME>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, _ctrl_pressed);
				break;

			case WID_VT_AUTOFILL: { // Autofill the timetable.
				Command<CMD_AUTOFILL_TIMETABLE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, !v->vehicle_flags.Test(VehicleFlag::AutofillTimetable), _ctrl_pressed);
				break;
			}

			case WID_VT_SCHEDULED_DISPATCH: {
				ShowSchdispatchWindow(v);
				break;
			}

			case WID_VT_AUTOMATE: {
				Command<CMD_AUTOMATE_TIMETABLE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, !v->vehicle_flags.Test(VehicleFlag::AutomateTimetable));
				break;
			}

			case WID_VT_AUTO_SEPARATION: {
				Command<CMD_TIMETABLE_SEPARATION>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, !v->vehicle_flags.Test(VehicleFlag::TimetableSeparation));
				break;
			}

			case WID_VT_EXPECTED:
				this->show_expected = !this->show_expected;
				break;

			case WID_VT_SHARED_ORDER_LIST:
				ShowVehicleListWindow(v);
				break;

			case WID_VT_ADD_VEH_GROUP: {
				ShowQueryString({}, STR_GROUP_RENAME_CAPTION, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;
			}

			case WID_VT_EXTRA: {
				VehicleOrderID real = (this->sel_index + 1) / 2;
				if (real >= this->vehicle->GetNumOrders()) real = 0;
				const Order *order = this->vehicle->GetOrder(real);
				bool leave_type_disabled = (order == nullptr) ||
							((!(order->IsType(OT_GOTO_STATION) || (order->IsType(OT_GOTO_DEPOT) && !(order->GetDepotActionType() & ODATFB_HALT))) ||
								(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) && !order->IsType(OT_CONDITIONAL));
				OrderLeaveType current = order != nullptr ? order->GetLeaveType() : OLT_END;
				DropDownList list;
				list.emplace_back(MakeDropDownListCheckedItem(current == OLT_NORMAL, STR_TIMETABLE_LEAVE_NORMAL, OLT_NORMAL, leave_type_disabled));
				list.emplace_back(MakeDropDownListCheckedItem(current == OLT_LEAVE_EARLY, STR_TIMETABLE_LEAVE_EARLY, OLT_LEAVE_EARLY, leave_type_disabled));
				list.emplace_back(MakeDropDownListCheckedItem(current == OLT_LEAVE_EARLY_FULL_ANY, STR_TIMETABLE_LEAVE_EARLY_FULL_ANY, OLT_LEAVE_EARLY_FULL_ANY, leave_type_disabled || !order->IsType(OT_GOTO_STATION)));
				list.emplace_back(MakeDropDownListCheckedItem(current == OLT_LEAVE_EARLY_FULL_ALL, STR_TIMETABLE_LEAVE_EARLY_FULL_ALL, OLT_LEAVE_EARLY_FULL_ALL, leave_type_disabled || !order->IsType(OT_GOTO_STATION)));
				ShowDropDownList(this, std::move(list), -1, widget, 0, DDMF_NONE, DDSF_SHARED);
				break;
			}

			case WID_VT_ASSIGN_SCHEDULE: {
				VehicleOrderID real = (this->sel_index + 1) / 2;
				if (real >= this->vehicle->GetNumOrders()) real = 0;
				const Order *order = this->vehicle->GetOrder(real);
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_TIMETABLE_ASSIGN_SCHEDULE_NONE, -1, false));

				for (uint i = 0; i < v->orders->GetScheduledDispatchScheduleCount(); i++) {
					const DispatchSchedule &ds = this->vehicle->orders->GetDispatchScheduleByIndex(i);
					if (ds.ScheduleName().empty()) {
						list.push_back(MakeDropDownListStringItem(GetString(STR_TIMETABLE_ASSIGN_SCHEDULE_ID, i + 1), i, false));
					} else {
						list.push_back(MakeDropDownListStringItem(std::string{ds.ScheduleName()}, i, false));
					}
				}
				ShowDropDownList(this, std::move(list), order->GetDispatchScheduleIndex(), WID_VT_ASSIGN_SCHEDULE, 0, DDMF_NONE, DDSF_SHARED);
				break;
			}
		}

		this->SetDirty();
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
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

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;

		const Vehicle *v = this->vehicle;

		switch (this->clicked_widget) {
			default: NOT_REACHED();

			case WID_VT_CHANGE_SPEED:
			case WID_VT_CHANGE_TIME: {
				uint32_t p2;
				if (this->query_is_speed_query) {
					uint64_t display_speed = 0;
					if (!str->empty()) {
						auto try_value = ParseInteger<uint64_t>(*str, 10, true);
						if (!try_value.has_value()) return;
						display_speed = *try_value;
					}
					uint64_t val = ConvertDisplaySpeedToKmhishSpeed(display_speed, v->type);
					p2 = std::min<uint>(val, UINT16_MAX);
				} else {
					p2 = ParseTimetableDuration(*str);
				}

				ExecuteTimetableCommand(v, this->change_timetable_all, this->sel_index, (this->sel_index % 2 == 1) ? (this->query_is_speed_query ? MTF_TRAVEL_SPEED : MTF_TRAVEL_TIME) : MTF_WAIT_TIME, p2, false);
				break;
			}

			case WID_VT_START_DATE: {
				if (str->empty()) break;
				auto result = IntFromChars<int32_t>(*str);
				if (!result.has_value()) break;
				int32_t val = *result;
				if (EconTime::UsingWallclockUnits() && !_settings_time.time_in_minutes) {
					Command<CMD_SET_TIMETABLE_START>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, this->set_start_date_all, _state_ticks + (val * TICKS_PER_SECOND));
					break;
				}
				if (val >= 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					const TickMinutes now = _settings_time.NowInTickMinutes();
					TickMinutes time = now.ToSameDayClockTime(hours, minutes);

					if (time < (now - 60)) time += TickMinutes{60 * 24};

					Command<CMD_SET_TIMETABLE_START>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, this->set_start_date_all, _settings_time.FromTickMinutes(time));
				}
				break;
			}

			case WID_VT_ADD_VEH_GROUP: {
				Command<CMD_CREATE_GROUP_FROM_LIST>::Post(STR_ERROR_GROUP_CAN_T_CREATE, VehicleListIdentifier(VL_SINGLE_VEH, v->type, v->owner, v->index), CargoFilterCriteria::CF_ANY, *str);
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

	const Vehicle *GetVehicle()
	{
		return this->vehicle;
	}
};

void InvalidateTimetableListWindowOnOrderMove(VehicleID veh, VehicleOrderID from, VehicleOrderID to, uint16_t count)
{
	TimetableWindow *w = dynamic_cast<TimetableWindow *>(FindWindowById(WC_VEHICLE_TIMETABLE, veh));
	if (w != nullptr) {
		w->SetDirty();
		w->OnOrderMove(from, to, count);
	}
}

static constexpr NWidgetPart _nested_timetable_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VT_CAPTION),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ORDER_VIEW), SetMinimalSize(61, 14), SetStringTip(STR_TIMETABLE_ORDER_VIEW, STR_TIMETABLE_ORDER_VIEW_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_TIMETABLE_PANEL), SetMinimalSize(388, 82), SetResize(1, 10), SetToolTip(STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_ARRIVAL_DEPARTURE_SELECTION),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_ARRIVAL_DEPARTURE_PANEL), SetMinimalSize(110, 0), SetFill(0, 1), SetToolTip(STR_TIMETABLE_TOOLTIP), SetScrollbar(WID_VT_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VT_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VT_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_START_DATE_SELECTION),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_START_DATE), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_START, STR_TIMETABLE_START_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VT_ASSIGN_SCHEDULE), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_ASSIGN_SCHEDULE_DROP_DOWN, STR_TIMETABLE_ASSIGN_SCHEDULE_DROP_DOWN_TOOLTIP),
				EndContainer(),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_TIME), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_CHANGE_TIME, STR_TIMETABLE_WAIT_TIME_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_TIME), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_CLEAR_TIME, STR_TIMETABLE_CLEAR_TIME_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTOFILL), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_AUTOFILL, STR_TIMETABLE_AUTOFILL_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CHANGE_SPEED), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_CHANGE_SPEED, STR_TIMETABLE_CHANGE_SPEED_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_CLEAR_SPEED), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_CLEAR_SPEED, STR_TIMETABLE_CLEAR_SPEED_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTOMATE), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_AUTOMATE, STR_TIMETABLE_AUTOMATE_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_AUTO_SEPARATION), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_AUTO_SEPARATION, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VT_EXTRA), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_EXTRA_DROP_DOWN, STR_TIMETABLE_EXTRA_DROP_DOWN_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_SCHEDULED_DISPATCH), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_SCHEDULED_DISPATCH, STR_TIMETABLE_SCHEDULED_DISPATCH_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_RESET_LATENESS), SetResize(1, 0), SetFill(1, 1), SetStringTip(STR_TIMETABLE_RESET_LATENESS, STR_TIMETABLE_RESET_LATENESS_TOOLTIP),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_EXPECTED_SELECTION),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_EXPECTED), SetResize(1, 0), SetFill(1, 1), SetToolTip(STR_TIMETABLE_EXPECTED_TOOLTIP),
					NWidget(WWT_PANEL, COLOUR_GREY), SetResize(1, 0), SetFill(1, 1), EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL, NWidContainerFlag::EqualSize),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VT_SEL_SHARED),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VT_SHARED_ORDER_LIST), SetAspect(1), SetFill(0, 1), SetSpriteTip(SPR_SHARED_ORDERS_ICON, STR_ORDERS_VEH_WITH_SHARED_ORDERS_LIST_TOOLTIP),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VT_ADD_VEH_GROUP), SetFill(0, 1), SetStringTip(STR_BLACK_PLUS, STR_ORDERS_NEW_GROUP_TOOLTIP),
			EndContainer(),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VT_LOCK_ORDER_TIME), SetFill(0, 1), SetSpriteTip(SPR_LOCK, STR_TIMETABLE_LOCK_ORDER_TIME_TOOLTIP),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY), SetFill(0, 1),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _timetable_desc(__FILE__, __LINE__,
	WDP_AUTO, "view_vehicle_timetable", 400, 130,
	WC_VEHICLE_TIMETABLE, WC_VEHICLE_VIEW,
	WindowDefaultFlag::Construction,
	_nested_timetable_widgets
);

/**
 * Show the timetable for a given vehicle.
 * @param v The vehicle to show the timetable for.
 */
void ShowTimetableWindow(const Vehicle *v)
{
	CloseWindowById(WC_VEHICLE_DETAILS, v->index, false);
	CloseWindowById(WC_VEHICLE_ORDERS, v->index, false);
	AllocateWindowDescFront<TimetableWindow>(_timetable_desc, v->index);
}

void SetTimetableWindowsDirty(const Vehicle *v, SetTimetableWindowsDirtyFlags flags)
{
	if (_pause_mode.Any()) InvalidateWindowClassesData(WC_DEPARTURES_BOARD);

	if (!(HaveWindowByClass(WC_VEHICLE_TIMETABLE) ||
			((flags & STWDF_SCHEDULED_DISPATCH) && HaveWindowByClass(WC_SCHDISPATCH_SLOTS)) ||
			((flags & STWDF_ORDERS) && HaveWindowByClass(WC_VEHICLE_ORDERS)))) {
		return;
	}

	v = v->FirstShared();
	for (Window *w : Window::Iterate()) {
		if (w->window_class == WC_VEHICLE_TIMETABLE ||
				((flags & STWDF_SCHEDULED_DISPATCH) && w->window_class == WC_SCHDISPATCH_SLOTS) ||
				((flags & STWDF_ORDERS) && w->window_class == WC_VEHICLE_ORDERS)) {
			if (static_cast<GeneralVehicleWindow *>(w)->vehicle->FirstShared() == v) w->SetDirty();
		}
	}
}
