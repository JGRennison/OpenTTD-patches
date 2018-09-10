/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timetable_cmd.cpp Commands related to time tabling. */

#include "stdafx.h"
#include "command_func.h"
#include "company_func.h"
#include "date_func.h"
#include "date_type.h"
#include "window_func.h"
#include "vehicle_base.h"
#include "settings_type.h"
#include "cmd_helper.h"
#include "company_base.h"
#include "core/sort_func.hpp"
#include "settings_type.h"
#include "scope.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Change/update a particular timetable entry.
 * @param v            The vehicle to change the timetable of.
 * @param order_number The index of the timetable in the order list.
 * @param val          The new data of the timetable entry.
 * @param mtf          Which part of the timetable entry to change.
 * @param timetabled   If the new value is explicitly timetabled.
 * @param ignore_lock  If the change should be applied even if the value is locked.
 */
static void ChangeTimetable(Vehicle *v, VehicleOrderID order_number, uint16 val, ModifyTimetableFlags mtf, bool timetabled, bool ignore_lock = false)
{
	Order *order = v->GetOrder(order_number);
	int total_delta = 0;
	int timetable_delta = 0;

	switch (mtf) {
		case MTF_WAIT_TIME:
			if (!ignore_lock && order->IsWaitFixed()) return;
			if (!order->IsType(OT_CONDITIONAL)) {
				total_delta = val - order->GetWaitTime();
				timetable_delta = (timetabled ? val : 0) - order->GetTimetabledWait();
			}
			order->SetWaitTime(val);
			order->SetWaitTimetabled(timetabled);
			break;

		case MTF_TRAVEL_TIME:
			if (!ignore_lock && order->IsTravelFixed()) return;
			if (!order->IsType(OT_CONDITIONAL)) {
				total_delta = val - order->GetTravelTime();
				timetable_delta = (timetabled ? val : 0) - order->GetTimetabledTravel();
			}
			if (order->IsType(OT_CONDITIONAL)) assert_msg(val == order->GetTravelTime(), "%u == %u", val, order->GetTravelTime());
			order->SetTravelTime(val);
			order->SetTravelTimetabled(timetabled);
			break;

		case MTF_TRAVEL_SPEED:
			order->SetMaxSpeed(val);
			break;

		case MTF_SET_WAIT_FIXED:
			order->SetWaitFixed(val != 0);
			break;

		case MTF_SET_TRAVEL_FIXED:
			order->SetTravelFixed(val != 0);
			break;

		case MTF_SET_LEAVE_TYPE:
			order->SetLeaveType((OrderLeaveType)val);
			break;

		default:
			NOT_REACHED();
	}
	v->orders.list->UpdateTotalDuration(total_delta);
	v->orders.list->UpdateTimetableDuration(timetable_delta);

	for (v = v->FirstShared(); v != NULL; v = v->NextShared()) {
		if (v->cur_real_order_index == order_number && v->current_order.Equals(*order)) {
			switch (mtf) {
				case MTF_WAIT_TIME:
					v->current_order.SetWaitTime(val);
					v->current_order.SetWaitTimetabled(timetabled);
					break;

				case MTF_TRAVEL_TIME:
					v->current_order.SetTravelTime(val);
					v->current_order.SetTravelTimetabled(timetabled);
					break;

				case MTF_TRAVEL_SPEED:
					v->current_order.SetMaxSpeed(val);
					break;

				case MTF_SET_WAIT_FIXED:
					v->current_order.SetWaitFixed(val != 0);
					break;

				case MTF_SET_TRAVEL_FIXED:
					v->current_order.SetTravelFixed(val != 0);
					break;

				case MTF_SET_LEAVE_TYPE:
					v->current_order.SetLeaveType((OrderLeaveType)val);
					break;

				default:
					NOT_REACHED();
			}
		}
		SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
	}
}

/**
 * Change timetable data of an order.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit  0-19) - Vehicle with the orders to change.
 * - p1 = (bit 20-27) - Order index to modify.
 * - p1 = (bit 28-30) - Timetable data to change (@see ModifyTimetableFlags)
 * - p1 = (bit    31) - 0 to set timetable wait/travel time, 1 to clear it
 * @param p2 The amount of time to wait.
 * - p2 = (bit  0-15) - The data to modify as specified by p1 bits 28-29.
 *                      0 to clear times, UINT16_MAX to clear speed limit.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeTimetable(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	VehicleOrderID order_number = GB(p1, 20, 8);
	Order *order = v->GetOrder(order_number);
	if (order == NULL || order->IsType(OT_IMPLICIT)) return CMD_ERROR;

	ModifyTimetableFlags mtf = Extract<ModifyTimetableFlags, 28, 3>(p1);
	if (mtf >= MTF_END) return CMD_ERROR;

	bool clear_field = GB(p1, 31, 1) == 1;

	int wait_time   = order->GetWaitTime();
	int travel_time = order->GetTravelTime();
	int max_speed   = order->GetMaxSpeed();
	bool wait_fixed = order->IsWaitFixed();
	bool travel_fixed = order->IsTravelFixed();
	OrderLeaveType leave_type = order->GetLeaveType();
	switch (mtf) {
		case MTF_WAIT_TIME:
			wait_time = GB(p2, 0, 16);
			if (clear_field) assert(wait_time == 0);
			break;

		case MTF_TRAVEL_TIME:
			travel_time = GB(p2, 0, 16);
			if (clear_field) assert(travel_time == 0);
			break;

		case MTF_TRAVEL_SPEED:
			max_speed = GB(p2, 0, 16);
			if (max_speed == 0) max_speed = UINT16_MAX; // Disable speed limit.
			break;

		case MTF_SET_WAIT_FIXED:
			wait_fixed = GB(p2, 0, 16) != 0;
			break;

		case MTF_SET_TRAVEL_FIXED:
			travel_fixed = GB(p2, 0, 16) != 0;
			break;

		case MTF_SET_LEAVE_TYPE:
			leave_type = (OrderLeaveType)GB(p2, 0, 16);
			if (leave_type >= OLT_END) return CMD_ERROR;
			break;

		default:
			NOT_REACHED();
	}

	if (wait_time != order->GetWaitTime() || leave_type != order->GetLeaveType()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) return_cmd_error(STR_ERROR_TIMETABLE_NOT_STOPPING_HERE);
				break;

			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				break;

			case OT_CONDITIONAL:
				break;

			default: return_cmd_error(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
		}
	}

	if (travel_time != order->GetTravelTime() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	if (max_speed != order->GetMaxSpeed() && (order->IsType(OT_CONDITIONAL) || v->type == VEH_AIRCRAFT)) return CMD_ERROR;
	if (wait_fixed != order->IsWaitFixed() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	if (leave_type != order->GetLeaveType() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		switch (mtf) {
			case MTF_WAIT_TIME:
				/* Set time if changing the value or confirming an estimated time as timetabled. */
				if (wait_time != order->GetWaitTime() || (clear_field == order->IsWaitTimetabled())) {
					ChangeTimetable(v, order_number, wait_time, MTF_WAIT_TIME, !clear_field, true);
				}
				break;

			case MTF_TRAVEL_TIME:
				/* Set time if changing the value or confirming an estimated time as timetabled. */
				if (travel_time != order->GetTravelTime() || (clear_field == order->IsTravelTimetabled())) {
					ChangeTimetable(v, order_number, travel_time, MTF_TRAVEL_TIME, !clear_field, true);
				}
				break;

			case MTF_TRAVEL_SPEED:
				if (max_speed != order->GetMaxSpeed()) {
					ChangeTimetable(v, order_number, max_speed, MTF_TRAVEL_SPEED, max_speed != UINT16_MAX, true);
				}
				break;

			case MTF_SET_WAIT_FIXED:
				if (wait_fixed != order->IsWaitFixed()) {
					ChangeTimetable(v, order_number, wait_fixed ? 1 : 0, MTF_SET_WAIT_FIXED, false, true);
				}
				break;

			case MTF_SET_TRAVEL_FIXED:
				if (travel_fixed != order->IsTravelFixed()) {
					ChangeTimetable(v, order_number, travel_fixed ? 1 : 0, MTF_SET_TRAVEL_FIXED, false, true);
				}
				break;

			case MTF_SET_LEAVE_TYPE:
				if (leave_type != order->GetLeaveType()) {
					ChangeTimetable(v, order_number, leave_type, MTF_SET_LEAVE_TYPE, true);
				}
				break;

			default:
				break;
		}
	}

	return CommandCost();
}

/**
 * Change timetable data of all orders of a vehicle.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit  0-19) - Vehicle with the orders to change.
 * - p1 = (bit 20-27) - unused
 * - p1 = (bit 28-30) - Timetable data to change (@see ModifyTimetableFlags)
 * - p1 = (bit    31) - 0 to set timetable wait/travel time, 1 to clear it
 * @param p2 The amount of time to wait.
 * - p2 = (bit  0-15) - The data to modify as specified by p1 bits 28-29.
 *                      0 to clear times, UINT16_MAX to clear speed limit.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBulkChangeTimetable(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	ModifyTimetableFlags mtf = Extract<ModifyTimetableFlags, 28, 3>(p1);
	if (mtf >= MTF_END) return CMD_ERROR;

	if (v->GetNumOrders() == 0) return CMD_ERROR;

	if (flags & DC_EXEC) {
		for (VehicleOrderID order_number = 0; order_number < v->GetNumOrders(); order_number++) {
			Order *order = v->GetOrder(order_number);
			if (order == NULL || order->IsType(OT_IMPLICIT)) continue;

			uint32 new_p1 = p1;
			SB(new_p1, 20, 8, order_number);
			DoCommand(tile, new_p1, p2, flags, CMD_CHANGE_TIMETABLE);
		}
	}

	return CommandCost();
}

/**
 * Clear the lateness counter to make the vehicle on time.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit  0-19) - Vehicle with the orders to change.
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetVehicleOnTime(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle() || v->orders.list == NULL) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		v->lateness_counter = 0;
		SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
	}

	return CommandCost();
}

/**
 * Order vehicles based on their timetable. The vehicles will be sorted in order
 * they would reach the first station.
 *
 * @param ap First Vehicle pointer.
 * @param bp Second Vehicle pointer.
 * @return Comparison value.
 */
static int CDECL VehicleTimetableSorter(Vehicle * const *ap, Vehicle * const *bp)
{
	const Vehicle *a = *ap;
	const Vehicle *b = *bp;

	VehicleOrderID a_order = a->cur_real_order_index;
	VehicleOrderID b_order = b->cur_real_order_index;
	int j = (int)b_order - (int)a_order;

	/* Are we currently at an ordered station (un)loading? */
	bool a_load = (a->current_order.IsType(OT_LOADING) && a->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) || a->current_order.IsType(OT_LOADING_ADVANCE);
	bool b_load = (b->current_order.IsType(OT_LOADING) && b->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) || b->current_order.IsType(OT_LOADING_ADVANCE);

	/* If the current order is not loading at the ordered station, decrease the order index by one since we have
	 * not yet arrived at the station (and thus the timetable entry; still in the travelling of the previous one).
	 * Since the ?_order variables are unsigned the -1 will flow under and place the vehicles going to order #0 at
	 * the begin of the list with vehicles arriving at #0. */
	if (!a_load) a_order--;
	if (!b_load) b_order--;

	/* First check the order index that accounted for loading, then just the raw one. */
	int i = (int)b_order - (int)a_order;
	if (i != 0) return i;
	if (j != 0) return j;

	/* Look at the time we spent in this order; the higher, the closer to its destination. */
	i = b->current_order_time - a->current_order_time;
	if (i != 0) return i;

	/* If all else is equal, use some unique index to sort it the same way. */
	return b->unitnumber - a->unitnumber;
}

/**
 * Set the start date of the timetable.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit 0-19) - Vehicle ID.
 * - p1 = (bit 20)   - Set to 1 to set timetable start for all vehicles sharing this order
 * - p1 = (bit 21-31)- Timetable start date: sub-ticks
 * @param p2 The timetable start date.
 * @param text Not used.
 * @return The error or cost of the operation.
 */
CommandCost CmdSetTimetableStart(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	bool timetable_all = HasBit(p1, 20);
	Vehicle *v = Vehicle::GetIfValid(GB(p1, 0, 20));
	uint16 sub_ticks = GB(p1, 21, 11);
	if (v == NULL || !v->IsPrimaryVehicle() || v->orders.list == NULL) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (timetable_all && !v->orders.list->IsCompleteTimetable()) return CMD_ERROR;

	const DateTicksScaled now = _scaled_date_ticks;
	DateTicksScaled start_date_scaled = (_settings_game.economy.day_length_factor * (((DateTicks)_date * DAY_TICKS) + _date_fract + (DateTicks)(int32)p2)) + sub_ticks;

	if (flags & DC_EXEC) {
		SmallVector<Vehicle *, 8> vehs;

		if (timetable_all) {
			for (Vehicle *w = v->orders.list->GetFirstSharedVehicle(); w != NULL; w = w->NextShared()) {
				*vehs.Append() = w;
			}
		} else {
			*vehs.Append() = v;
		}

		int total_duration = v->orders.list->GetTimetableTotalDuration();
		int num_vehs = vehs.Length();

		if (num_vehs >= 2) {
			QSortT(vehs.Begin(), vehs.Length(), &VehicleTimetableSorter);
		}

		int base = vehs.FindIndex(v);

		for (Vehicle **viter = vehs.Begin(); viter != vehs.End(); viter++) {
			int idx = (viter - vehs.Begin()) - base;
			Vehicle *w = *viter;

			w->lateness_counter = 0;
			ClrBit(w->vehicle_flags, VF_TIMETABLE_STARTED);
			/* Do multiplication, then division to reduce rounding errors. */
			DateTicksScaled tt_start = start_date_scaled + ((idx * total_duration) / num_vehs);
			if (tt_start < now && idx < 0) {
				tt_start += total_duration;
			}
			w->timetable_start = tt_start / _settings_game.economy.day_length_factor;
			w->timetable_start_subticks = tt_start % _settings_game.economy.day_length_factor;
			SetWindowDirty(WC_VEHICLE_TIMETABLE, w->index);
		}

	}

	return CommandCost();
}


/**
 * Start or stop filling the timetable automatically from the time the vehicle
 * actually takes to complete it. When starting to autofill the current times
 * are cleared and the timetable will start again from scratch.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Various bitstuffed elements
 * - p2 = (bit 0) - Set to 1 to enable, 0 to disable autofill.
 * - p2 = (bit 1) - Set to 1 to preserve waiting times in non-destructive mode
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAutofillTimetable(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle() || v->orders.list == NULL) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		if (HasBit(p2, 0)) {
			/* Start autofilling the timetable, which clears the
			 * "timetable has started" bit. Times are not cleared anymore, but are
			 * overwritten when the order is reached now. */
			SetBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE);
			ClrBit(v->vehicle_flags, VF_TIMETABLE_STARTED);

			/* Overwrite waiting times only if they got longer */
			if (HasBit(p2, 1)) SetBit(v->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);

			v->timetable_start = 0;
			v->timetable_start_subticks = 0;
			v->lateness_counter = 0;
		} else {
			ClrBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE);
			ClrBit(v->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
		}

		for (Vehicle *v2 = v->FirstShared(); v2 != NULL; v2 = v2->NextShared()) {
			if (v2 != v) {
				/* Stop autofilling; only one vehicle at a time can perform autofill */
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
			}
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v2->index);
		}
	}

	return CommandCost();
}

/**
* Start or stop automatic management of timetables.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Various bitstuffed elements
 * - p2 = (bit 0) - Set to 1 to enable, 0 to disable automation.
 * - p2 = (bit 1) - Ctrl was pressed. Used when disabling to keep times.
 * @param text unused
 * @return the cost of this operation or an error
 */

CommandCost CmdAutomateTimetable(TileIndex index, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 16);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != NULL; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				/* Automated timetable. Set flags and clear current times. */
				SetBit(v2->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
				ClrBit(v2->vehicle_flags, VF_TIMETABLE_STARTED);
				v2->timetable_start = 0;
				v2->timetable_start_subticks = 0;
				v2->lateness_counter = 0;
				v2->current_loading_time = 0;
				v2->ClearSeparation();
			} else {
				/* De-automate timetable. Clear flags. */
				ClrBit(v2->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
				v2->ClearSeparation();
				if (!HasBit(p2, 1)) {
					/* Ctrl wasn't pressed, so clear all timetabled times. */
					ClrBit(v2->vehicle_flags, VF_TIMETABLE_STARTED);
					v2->timetable_start = 0;
					v2->timetable_start_subticks = 0;
					v2->lateness_counter = 0;
					v2->current_loading_time = 0;
				}
			}
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v2->index);
		}
		if (!HasBit(p2, 0) && !HasBit(p2, 1)) {
			OrderList *orders = v->orders.list;
			if (orders != NULL) {
				for (int i = 0; i < orders->GetNumOrders(); i++) {
					ChangeTimetable(v, i, 0, MTF_WAIT_TIME, false);
					ChangeTimetable(v, i, 0, MTF_TRAVEL_TIME, false);
				}
			}
		}
	}

	return CommandCost();
}

/**
 * Enable or disable auto timetable separation
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Various bitstuffed elements
 * - p2 = (bit 0) - Set to 1 to enable, 0 to disable auto separatiom.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdTimetableSeparation(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == NULL || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != NULL; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				SetBit(v2->vehicle_flags, VF_TIMETABLE_SEPARATION);
			} else {
				ClrBit(v2->vehicle_flags, VF_TIMETABLE_SEPARATION);
			}
			v2->ClearSeparation();
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v2->index);
		}
	}

	return CommandCost();
}

static inline bool IsOrderUsableForSeparation(const Order *order)
{
	if (order->IsType(OT_CONDITIONAL)) {
		// Auto separation is unlikely to useful work at all if one of these is present, so give up
		return false;
	}

	if (order->GetWaitTime() == 0 && order->IsType(OT_GOTO_STATION) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
		// non-station orders are permitted to have 0 wait times
		return false;
	}

	if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
		// 0 travel times are permitted, if explicitly timetabled
		// this is useful for depot service orders
		return false;
	}

	return true;
}

int TimeToFinishOrder(Vehicle *v, int n)
{
	int left;
	Order *order = v->GetOrder(n);
	int wait_time   = order->GetWaitTime();
	int travel_time = order->GetTravelTime();
	assert(order != NULL);
	if (!IsOrderUsableForSeparation(order)) return -1;
	if ((v->cur_real_order_index == n) && (v->last_station_visited == order->GetDestination())) {
		if (v->current_loading_time > 0) {
			left = wait_time - v->current_order_time;
		} else {
			left = wait_time;
		}
		if (left < 0) left = 0;
	} else {
		left = travel_time;
		if (v->cur_real_order_index == n) left -= v->current_order_time;
		if (left < 0) left = 0;
		left +=wait_time;
	}
	return left;
}

int SeparationBetween(Vehicle *v1, Vehicle *v2)
{
	if (v1 == v2) return -1;
	int separation = 0;
	int time;
	int n = v1->cur_real_order_index;
	while (n != v2->cur_real_order_index) {
		time = TimeToFinishOrder(v1, n);
		if (time == -1) return -1;
		separation += time;
		n++;
		if (n >= v1->GetNumOrders()) n = 0;
	}
	int time1 = TimeToFinishOrder(v1, n);
	int time2 = TimeToFinishOrder(v2, n);
	if (time1 == -1 || time2 == -1) return -1;
	time = time1 - time2;
	if (time < 0) {
		for (n = 0; n < v1->GetNumOrders(); n++) {
			Order *order = v1->GetOrder(n);
			if (!IsOrderUsableForSeparation(order)) return -1;
			time += order->GetTravelTime() + order->GetWaitTime();
		}
	}
	separation += time;
	assert(separation >= 0);
	if (separation == 0) return -1;
	return separation;
}

void UpdateSeparationOrder(Vehicle *v_start)
{
	/* First check if we have a vehicle ahead, and if not search for one. */
	if (v_start->AheadSeparation() == NULL) {
		v_start->InitSeparation();
	}
	if (v_start->AheadSeparation() == NULL) {
		return;
	}
	/* Switch positions if necessary. */
	int swaps = 0;
	bool done = false;
	while (!done) {
		done = true;
		int min_sep = SeparationBetween(v_start, v_start->AheadSeparation());
		Vehicle *v = v_start;
		do {
			if (v != v_start) {
				int tmp_sep = SeparationBetween(v_start, v);
				if (tmp_sep < min_sep && tmp_sep != -1) {
					swaps++;
					if (swaps >= 50) {
						return;
					}
					done = false;
					v_start->ClearSeparation();
					v_start->AddToSeparationBehind(v);
					break;
				}
			}
			int separation_ahead = SeparationBetween(v, v->AheadSeparation());
			int separation_behind = SeparationBetween(v->BehindSeparation(), v);
			if (separation_ahead != -1 && separation_behind != -1) {
				Company *owner = Company::GetIfValid(v->owner);
				uint8 timetable_separation_rate = owner ? owner->settings.auto_timetable_separation_rate : 100;
				int new_lateness = (separation_ahead - separation_behind) / 2;
				v->lateness_counter = (new_lateness * timetable_separation_rate +
						v->lateness_counter * (100 - timetable_separation_rate)) / 100;
			}
			v = v->AheadSeparation();
		} while (v != v_start);
	}
}

static bool IsVehicleAtFirstWaitingLocation(Vehicle *v)
{
	/* Check if we arrive at first station */
	int first_wait_index = -1;
	for (int i = 0; i < v->orders.list->GetNumOrders(); ++i) {
		Order* order = v->orders.list->GetOrderAt(i);

		if (order->IsWaitTimetabled() && !order->IsType(OT_IMPLICIT)) {
			first_wait_index = i;
			break;
		}
	}

	return v->orders.list->IsCompleteTimetable() && (v->cur_implicit_order_index == first_wait_index);
}

static DateTicksScaled GetScheduledDispatchTime(Vehicle *v)
{
	DateTicksScaled first_slot          = -1;
	const DateTicksScaled begin_time    = v->orders.list->GetScheduledDispatchStartTick();
	const int32 last_dispatched_offset  = v->orders.list->GetScheduledDispatchLastDispatch();
	const uint32 dispatch_duration      = v->orders.list->GetScheduledDispatchDuration();
	const int32 max_delay               = v->orders.list->GetScheduledDispatchDelay();

	/* Find next available slots */
	for (auto current_offset : v->orders.list->GetScheduledDispatch()) {
		while (int32(current_offset) <= last_dispatched_offset) {
			current_offset += dispatch_duration;
		}

		DateTicksScaled current_departure = begin_time + current_offset;
		while (current_departure + max_delay < _scaled_date_ticks) {
			current_departure += dispatch_duration;
		}

		if (first_slot == -1 || first_slot > current_departure) {
			first_slot = current_departure;
		}
	}

	return first_slot;
}

/**
 * Update the timetable for the vehicle.
 * @param v The vehicle to update the timetable for.
 * @param travelling Whether we just travelled or waited at a station.
 */
void UpdateVehicleTimetable(Vehicle *v, bool travelling)
{
	if (!travelling) v->current_loading_time++; // +1 because this time is one tick behind
	uint time_taken = v->current_order_time;
	uint time_loading = v->current_loading_time;

	v->current_order_time = 0;
	v->current_loading_time = 0;

	if (v->current_order.IsType(OT_IMPLICIT)) return; // no timetabling of auto orders

	if (v->cur_real_order_index >= v->GetNumOrders()) return;
	Order *real_current_order = v->GetOrder(v->cur_real_order_index);
	Order *real_timetable_order = v->cur_timetable_order_index != INVALID_VEH_ORDER_ID ? v->GetOrder(v->cur_timetable_order_index) : nullptr;

	auto guard = scope_guard([v, travelling]() {
		/* On next call, when updating waiting time, use current order even if travel field of current order isn't being updated */
		if (travelling) v->cur_timetable_order_index = v->cur_real_order_index;
	});

	VehicleOrderID first_manual_order = 0;
	for (Order *o = v->GetFirstOrder(); o != NULL && o->IsType(OT_IMPLICIT); o = o->next) {
		++first_manual_order;
	}

	bool just_started = false;

	/* Start scheduled dispatch at first opportunity */
	if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED) && HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
		if (IsVehicleAtFirstWaitingLocation(v) && travelling) {
			/* Update scheduled information */
			v->orders.list->UpdateScheduledDispatch();

			DateTicksScaled slot = GetScheduledDispatchTime(v);
			if (slot > -1) {
				v->lateness_counter = _scaled_date_ticks - slot;
				v->orders.list->SetScheduledDispatchLastDispatch(slot - v->orders.list->GetScheduledDispatchStartTick());
			}
		}
	}

	/* Start automated timetables at first opportunity */
	if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED) && HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
		v->ClearSeparation();
		SetBit(v->vehicle_flags, VF_TIMETABLE_STARTED);
		/* If the lateness is set by scheduled dispatch above, do not reset */
		if(!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) v->lateness_counter = 0;
		if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) UpdateSeparationOrder(v);
		for (v = v->FirstShared(); v != NULL; v = v->NextShared()) {
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
		}
		return;
	}

	/* This vehicle is arriving at the first destination in the timetable. */
	if (v->cur_real_order_index == first_manual_order && travelling) {
		/* If the start date hasn't been set, or it was set automatically when
		 * the vehicle last arrived at the first destination, update it to the
		 * current time. Otherwise set the late counter appropriately to when
		 * the vehicle should have arrived. */
		just_started = !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED);

		if (v->timetable_start != 0) {
			v->lateness_counter = _scaled_date_ticks - ((_settings_game.economy.day_length_factor * v->timetable_start) + v->timetable_start_subticks);
			v->timetable_start = 0;
			v->timetable_start_subticks = 0;
		}

		SetBit(v->vehicle_flags, VF_TIMETABLE_STARTED);
		SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
	}

	if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) return;
	if (real_timetable_order == nullptr) return;

	bool autofilling = HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE);
	bool is_conditional = real_timetable_order->IsType(OT_CONDITIONAL);
	bool remeasure_wait_time = !is_conditional && (!real_timetable_order->IsWaitTimetabled() ||
			(autofilling && !HasBit(v->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME)));

	if (travelling && remeasure_wait_time) {
		/* We just finished travelling and want to remeasure the loading time,
		 * so do not apply any restrictions for the loading to finish. */
		v->current_order.SetWaitTime(0);
	}

	bool travel_field = travelling;
	if (is_conditional) {
		if (travelling) {
			/* conditional orders use the wait field for the jump-taken travel time */
			travel_field = false;
		} else {
			/* doesn't make sense to update wait time for conditional orders */
			return;
		}
	} else {
		assert_msg(real_timetable_order == real_current_order, "%u, %u", v->cur_real_order_index, v->cur_timetable_order_index);
	}

	if (just_started) return;

	/* Before modifying waiting times, check whether we want to preserve bigger ones. */
	if ((travelling || time_taken > real_timetable_order->GetWaitTime() || remeasure_wait_time)) {
		/* Round the time taken up to the nearest timetable rounding factor
		 * (default: day), as this will avoid confusion for people who are
		 * timetabling in days, and can be adjusted later by people who aren't.
		 * For trains/aircraft multiple movement cycles are done in one
		 * tick. This makes it possible to leave the station and process
		 * e.g. a depot order in the same tick, causing it to not fill
		 * the timetable entry like is done for road vehicles/ships.
		 * Thus always make sure at least one tick is used between the
		 * processing of different orders when filling the timetable. */
		Company *owner = Company::GetIfValid(v->owner);
		uint rounding_factor = owner ? owner->settings.timetable_autofill_rounding : DAY_TICKS;
		uint time_to_set = CeilDiv(max(time_taken, 1U), rounding_factor) * rounding_factor;

		if (travel_field && (autofilling || !real_timetable_order->IsTravelTimetabled())) {
			ChangeTimetable(v, v->cur_timetable_order_index, time_to_set, MTF_TRAVEL_TIME, autofilling);
		} else if (!travel_field && (autofilling || !real_timetable_order->IsWaitTimetabled())) {
			ChangeTimetable(v, v->cur_timetable_order_index, time_to_set, MTF_WAIT_TIME, autofilling);
		}
	}

	if (v->cur_real_order_index == first_manual_order && travelling) {
		/* If we just started we would have returned earlier and have not reached
		 * this code. So obviously, we have completed our round: So turn autofill
		 * off again. */
		ClrBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE);
		ClrBit(v->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
	}

	if (autofilling) return;

	uint timetabled = travel_field ? real_timetable_order->GetTimetabledTravel() :
			real_timetable_order->GetTimetabledWait();

	/* Update the timetable to gradually shift order times towards the actual travel times. */
	if (timetabled != 0 && HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
		int32 new_time;
		if (travelling) {
			new_time = time_taken;
		} else {
			new_time = time_loading;
		}

		if (new_time > (int32)timetabled * 4 && travelling) {
			/* Possible jam, clear time and restart timetable for all vehicles.
			 * Otherwise we risk trains blocking 1-lane stations for long times. */
			ChangeTimetable(v, v->cur_timetable_order_index, 0, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, true);
			for (Vehicle *v2 = v->FirstShared(); v2 != NULL; v2 = v2->NextShared()) {
				v2->ClearSeparation();
				ClrBit(v2->vehicle_flags, VF_TIMETABLE_STARTED);
				SetWindowDirty(WC_VEHICLE_TIMETABLE, v2->index);
			}
			return;
		} else if (new_time >= (int32)timetabled / 2) {
			/* Compute running average, with sign conversion to avoid negative overflow. */
			if (new_time < (int32)timetabled) {
				new_time = ((int32)timetabled * 3 + new_time * 2 + 2) / 5;
			} else {
				new_time = ((int32)timetabled * 9 + new_time + 5) / 10;
			}
		} else {
			/* new time is less than hald old time, set value directly */
		}

		if (new_time < 1) new_time = 1;
		if (new_time != (int32)timetabled) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, true);
		}
	} else if (timetabled == 0 && HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
		/* Add times for orders that are not yet timetabled, even while not autofilling */
		const int32 new_time = travelling ? time_taken : time_loading;
		if (travel_field) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_TRAVEL_TIME, true);
		} else {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_WAIT_TIME, true);
		}
	}

	/* Vehicles will wait at stations if they arrive early even if they are not
	 * timetabled to wait there, so make sure the lateness counter is updated
	 * when this happens. */
	if (timetabled == 0 && (travelling || v->lateness_counter >= 0)) return;

	if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) && HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
		v->current_order_time = time_taken;
		v->current_loading_time = time_loading;
		UpdateSeparationOrder(v);
		v->current_order_time = 0;
		v->current_loading_time = 0;
	} else if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
		const bool is_first_waiting = IsVehicleAtFirstWaitingLocation(v);
		if (is_first_waiting) {
			/* Update scheduled information */
			v->orders.list->UpdateScheduledDispatch();
		}
		if (is_first_waiting && travelling) {
			DateTicksScaled slot = GetScheduledDispatchTime(v);
			if (slot > -1) {
				v->lateness_counter = _scaled_date_ticks - slot;
				v->orders.list->SetScheduledDispatchLastDispatch(slot - v->orders.list->GetScheduledDispatchStartTick());
			} else {
				v->lateness_counter -= (timetabled - time_taken);
			}
		} else {
			v->lateness_counter -= (timetabled - time_taken);
		}
	} else {
		v->lateness_counter -= (timetabled - time_taken);
	}

	/* When we are more late than this timetabled bit takes we (somewhat expensively)
	 * check how many ticks the (fully filled) timetable has. If a timetable cycle is
	 * shorter than the amount of ticks we are late we reduce the lateness by the
	 * length of a full cycle till lateness is less than the length of a timetable
	 * cycle. When the timetable isn't fully filled the cycle will be INVALID_TICKS. */
	if (v->lateness_counter > (int)timetabled) {
		Ticks cycle = v->orders.list->GetTimetableTotalDuration();
		if (cycle != INVALID_TICKS && v->lateness_counter > cycle) {
			v->lateness_counter %= cycle;
		}
	}

	for (v = v->FirstShared(); v != NULL; v = v->NextShared()) {
		SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
	}
}
