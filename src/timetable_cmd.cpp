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
#include "debug.h"
#include "window_func.h"
#include "vehicle_base.h"
#include "settings_type.h"
#include "cmd_helper.h"
#include "company_base.h"
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
static void ChangeTimetable(Vehicle *v, VehicleOrderID order_number, uint32_t val, ModifyTimetableFlags mtf, bool timetabled, bool ignore_lock = false)
{
	Order *order = v->GetOrder(order_number);
	assert(order != nullptr);
	if (order->HasNoTimetableTimes()) return;

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
			if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && timetabled && order->IsScheduledDispatchOrder(true)) {
				for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
					if (u->cur_implicit_order_index == order_number && order->IsBaseStationOrder() && u->last_station_visited == order->GetDestination()) {
						u->lateness_counter += timetable_delta;
					}
				}
			}
			break;

		case MTF_TRAVEL_TIME:
			if (!ignore_lock && order->IsTravelFixed()) return;
			if (!order->IsType(OT_CONDITIONAL)) {
				total_delta = val - order->GetTravelTime();
				timetable_delta = (timetabled ? val : 0) - order->GetTimetabledTravel();
			}
			if (order->IsType(OT_CONDITIONAL)) assert_msg(val == order->GetTravelTime(), "{} == {}", val, order->GetTravelTime());
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

		case MTF_ASSIGN_SCHEDULE:
			if ((int)val >= 0) {
				for (int n = 0; n < v->GetNumOrders(); n++) {
					Order *o = v->GetOrder(n);
					if (o->GetDispatchScheduleIndex() == (int)val) {
						o->SetDispatchScheduleIndex(-1);
					}
				}
			}
			order->SetDispatchScheduleIndex((int)val);
			break;

		default:
			NOT_REACHED();
	}
	v->orders->UpdateTotalDuration(total_delta);
	v->orders->UpdateTimetableDuration(timetable_delta);

	SetTimetableWindowsDirty(v, (mtf == MTF_ASSIGN_SCHEDULE) ? STWDF_SCHEDULED_DISPATCH : STWDF_NONE);

	for (v = v->FirstShared(); v != nullptr; v = v->NextShared()) {
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

				case MTF_ASSIGN_SCHEDULE:
					v->current_order.SetDispatchScheduleIndex((int)val);
					break;

				default:
					NOT_REACHED();
			}
		}
	}
}

/**
 * Change timetable data of an order.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit  0-19) - Vehicle with the orders to change.
 * - p1 = (bit 20-27) - unused
 * - p1 = (bit 28-30) - Timetable data to change (@see ModifyTimetableFlags)
 * - p1 = (bit    31) - 0 to set timetable wait/travel time, 1 to clear it
 * @param p2 The amount of time to wait.
 * - p2 =             - The data to modify as specified by p1 bits 28-30.
 *                      0 to clear times, UINT16_MAX to clear speed limit.
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0 - 15) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeTimetable(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	VehicleOrderID order_number = GB(p3,  0, 16);
	Order *order = v->GetOrder(order_number);
	if (order == nullptr || order->IsType(OT_IMPLICIT) || order->HasNoTimetableTimes()) return CMD_ERROR;

	ModifyTimetableFlags mtf = Extract<ModifyTimetableFlags, 28, 3>(p1);
	if (mtf >= MTF_END) return CMD_ERROR;

	bool clear_field = GB(p1, 31, 1) == 1;

	TimetableTicks wait_time   = order->GetWaitTime();
	TimetableTicks travel_time = order->GetTravelTime();
	int max_speed   = order->GetMaxSpeed();
	bool wait_fixed = order->IsWaitFixed();
	bool travel_fixed = order->IsTravelFixed();
	OrderLeaveType leave_type = order->GetLeaveType();
	int dispatch_index = order->GetDispatchScheduleIndex();
	switch (mtf) {
		case MTF_WAIT_TIME:
			wait_time = p2;
			if (clear_field && wait_time != 0) return CMD_ERROR;
			break;

		case MTF_TRAVEL_TIME:
			travel_time = p2;
			if (clear_field && travel_time != 0) return CMD_ERROR;
			break;

		case MTF_TRAVEL_SPEED:
			max_speed = GB(p2, 0, 16);
			if (max_speed == 0) max_speed = UINT16_MAX; // Disable speed limit.
			break;

		case MTF_SET_WAIT_FIXED:
			wait_fixed = p2 != 0;
			break;

		case MTF_SET_TRAVEL_FIXED:
			travel_fixed = p2 != 0;
			break;

		case MTF_SET_LEAVE_TYPE:
			leave_type = (OrderLeaveType)p2;
			if (leave_type >= OLT_END) return CMD_ERROR;
			break;

		case MTF_ASSIGN_SCHEDULE:
			dispatch_index = (int)p2;
			if (dispatch_index < -1 || dispatch_index >= (int)v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
			break;

		default:
			NOT_REACHED();
	}

	if (wait_time != order->GetWaitTime() || leave_type != order->GetLeaveType()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) {
					if (mtf == MTF_WAIT_TIME && clear_field) break;
					return_cmd_error(STR_ERROR_TIMETABLE_NOT_STOPPING_HERE);
				}
				break;

			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				break;

			case OT_CONDITIONAL:
				break;

			default: return_cmd_error(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
		}
	}

	if (dispatch_index != order->GetDispatchScheduleIndex()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) {
					if (mtf == MTF_ASSIGN_SCHEDULE && dispatch_index == -1) break;
					return_cmd_error(STR_ERROR_TIMETABLE_NOT_STOPPING_HERE);
				}
				break;

			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				break;

			default: return_cmd_error(STR_ERROR_TIMETABLE_ONLY_WAIT_AT_STATIONS);
		}
	}

	if (travel_time != order->GetTravelTime() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	if (travel_fixed != order->IsTravelFixed() && order->IsType(OT_CONDITIONAL)) return CMD_ERROR;
	if (max_speed != order->GetMaxSpeed() && (order->IsType(OT_CONDITIONAL) || v->type == VEH_AIRCRAFT)) return CMD_ERROR;
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

			case MTF_ASSIGN_SCHEDULE:
				if (dispatch_index != order->GetDispatchScheduleIndex()) {
					ChangeTimetable(v, order_number, dispatch_index, MTF_ASSIGN_SCHEDULE, true);
				}
				break;

			default:
				break;
		}

		/* Unbunching data is no longer valid for any vehicle in this shared order group. */
		Vehicle *u = v->FirstShared();
		for (; u != nullptr; u = u->NextShared()) {
			u->ResetDepotUnbunching();
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
CommandCost CmdBulkChangeTimetable(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	ModifyTimetableFlags mtf = Extract<ModifyTimetableFlags, 28, 3>(p1);
	if (mtf >= MTF_END) return CMD_ERROR;

	if (v->GetNumOrders() == 0) return CMD_ERROR;

	if (flags & DC_EXEC) {
		for (VehicleOrderID order_number = 0; order_number < v->GetNumOrders(); order_number++) {
			Order *order = v->GetOrder(order_number);
			if (order == nullptr || order->IsType(OT_IMPLICIT)) continue;

			// Exclude waypoints from set all wait times command
			if (Extract<ModifyTimetableFlags, 28, 3>(p1) == MTF_WAIT_TIME && GB(p1, 31, 1) == 0 && order->IsType(OT_GOTO_WAYPOINT)) continue;

			DoCommandEx(tile, p1, p2, order_number, flags, CMD_CHANGE_TIMETABLE);
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
 * - p1 = (bit  20)   - Apply to all vehicles in group.
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetVehicleOnTime(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	bool apply_to_group = HasBit(p1, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;

	/* A vehicle can't be late if its timetable hasn't started.
	 * If we're setting all vehicles in the group, we handle that below. */
	if (!apply_to_group && !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) return CommandCost(STR_ERROR_TIMETABLE_NOT_STARTED);

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		if (apply_to_group) {
			int32_t most_late = 0;
			for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
				/* A vehicle can't be late if its timetable hasn't started. */
				if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) continue;

				if (u->lateness_counter > most_late) {
					most_late = u->lateness_counter;
				}

				/* Unbunching data is no longer valid. */
				u->ResetDepotUnbunching();
			}
			if (most_late > 0) {
				for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
					/* A vehicle can't be late if its timetable hasn't started. */
					if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) continue;

					u->lateness_counter -= most_late;
					SetWindowDirty(WC_VEHICLE_TIMETABLE, u->index);
				}
			}
		} else {
			v->lateness_counter = 0;
			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
		}
	}

	return CommandCost();
}

/**
 * Order vehicles based on their timetable. The vehicles will be sorted in order
 * they would reach the first station.
 *
 * @param a First Vehicle pointer.
 * @param b Second Vehicle pointer.
 * @return Comparison value.
 */
static bool VehicleTimetableSorter(Vehicle * const &a, Vehicle * const &b)
{
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
	if (i != 0) return i < 0;
	if (j != 0) return j < 0;

	/* Look at the time we spent in this order; the higher, the closer to its destination. */
	i = b->current_order_time - a->current_order_time;
	if (i != 0) return i < 0;

	/* If all else is equal, use some unique index to sort it the same way. */
	int k = b->unitnumber - a->unitnumber;
	if (k != 0) return k < 0;

	return b->index < a->index;
}

/**
 * Set the start date of the timetable.
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Various bitstuffed elements
 * - p1 = (bit 0-19) - Vehicle ID.
 * - p1 = (bit 20)   - Set to 1 to set timetable start for all vehicles sharing this order
 * @param p3 The timetable start ticks.
 * @param text Not used.
 * @return The error or cost of the operation.
 */
CommandCost CmdSetTimetableStart(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	bool timetable_all = HasBit(p1, 20);
	Vehicle *v = Vehicle::GetIfValid(GB(p1, 0, 20));
	if (v == nullptr || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	StateTicks start_state_tick = (StateTicks)p3;

	/* Don't let a timetable start more than 15 unscaled years into the future... */
	if (start_state_tick - _state_ticks > 15 * DAY_TICKS * DAYS_IN_LEAP_YEAR) return CMD_ERROR;
	/* ...or 1 unscaled year in the past. */
	if (_state_ticks - start_state_tick > DAY_TICKS * DAYS_IN_LEAP_YEAR) return CMD_ERROR;

	if (timetable_all && !v->orders->IsCompleteTimetable()) return CommandCost(STR_ERROR_TIMETABLE_INCOMPLETE);

	if (flags & DC_EXEC) {
		std::vector<Vehicle *> vehs;

		if (timetable_all) {
			for (Vehicle *w = v->orders->GetFirstSharedVehicle(); w != nullptr; w = w->NextShared()) {
				vehs.push_back(w);
			}
			SetTimetableWindowsDirty(v);
		} else {
			vehs.push_back(v);
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
		}

		int total_duration = v->orders->GetTimetableTotalDuration();
		int num_vehs = (uint)vehs.size();

		if (num_vehs >= 2) {
			std::sort(vehs.begin(), vehs.end(), &VehicleTimetableSorter);
		}

		int idx = 0;

		for (Vehicle *w : vehs) {
			w->lateness_counter = 0;
			ClrBit(w->vehicle_flags, VF_TIMETABLE_STARTED);
			/* Do multiplication, then division to reduce rounding errors. */
			w->timetable_start = start_state_tick + ((idx * total_duration) / num_vehs);

			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();

			++idx;
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
CommandCost CmdAutofillTimetable(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;

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
			v->lateness_counter = 0;
		} else {
			ClrBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE);
			ClrBit(v->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
		}

		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (v2 != v) {
				/* Stop autofilling; only one vehicle at a time can perform autofill */
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
			}
		}
		SetTimetableWindowsDirty(v);
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
 * @param text unused
 * @return the cost of this operation or an error
 */

CommandCost CmdAutomateTimetable(TileIndex index, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				/* Automated timetable. Set flags and clear current times if also auto-separating. */
				SetBit(v2->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
				if (HasBit(v2->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
					ClrBit(v2->vehicle_flags, VF_TIMETABLE_STARTED);
					v2->timetable_start = 0;
					v2->lateness_counter = 0;
				}
				v2->ClearSeparation();
			} else {
				/* De-automate timetable. Clear flags. */
				ClrBit(v2->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(v2->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
				v2->ClearSeparation();
			}
		}
		SetTimetableWindowsDirty(v);
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
CommandCost CmdTimetableSeparation(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (HasBit(p2, 0) && (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) || v->HasUnbunchingOrder())) return CommandCost(STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE);

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				SetBit(v2->vehicle_flags, VF_TIMETABLE_SEPARATION);
			} else {
				ClrBit(v2->vehicle_flags, VF_TIMETABLE_SEPARATION);
			}
			v2->ClearSeparation();
		}
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

static inline bool IsOrderUsableForSeparation(const Order *order)
{
	if (order->HasNoTimetableTimes()) return true;

	if (order->GetWaitTime() == 0 && order->IsType(OT_GOTO_STATION) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
		// non-station orders are permitted to have 0 wait times
		return false;
	}

	if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
		// 0 travel times are permitted, if explicitly timetabled
		return false;
	}

	return true;
}

std::vector<TimetableProgress> PopulateSeparationState(const Vehicle *v_start)
{
	std::vector<TimetableProgress> out;
	if (v_start->GetNumOrders() == 0) return out;
	for (const Vehicle *v = v_start->FirstShared(); v != nullptr; v = v->NextShared()) {
		if (!HasBit(v->vehicle_flags, VF_SEPARATION_ACTIVE)) continue;
		bool separation_valid = true;
		const int n = v->cur_real_order_index;
		int cumulative_ticks = 0;
		bool vehicle_ok = true;
		int order_count = n * 2;
		for (int i = 0; i < n; i++) {
			const Order *order = v->GetOrder(i);
			if (order->IsType(OT_CONDITIONAL)) {
				vehicle_ok = false;
				break;
			}
			if (!IsOrderUsableForSeparation(order)) separation_valid = false;
			cumulative_ticks += order->GetTravelTime() + order->GetWaitTime();
		}
		if (!vehicle_ok) continue;

		const Order *order = v->GetOrder(n);
		if (order->IsType(OT_CONDITIONAL)) continue;
		if (!IsOrderUsableForSeparation(order)) separation_valid = false;
		if (order->IsType(OT_GOTO_DEPOT) && (order->GetDepotOrderType() & ODTFB_SERVICE || order->GetDepotActionType() & ODATFB_HALT)) {
			// Do not try to separate vehicles on depot service or halt orders
			separation_valid = false;
		}
		if (order->IsType(OT_SLOT) || order->IsType(OT_COUNTER) || order->IsType(OT_DUMMY) || order->IsType(OT_LABEL)) {
			// Do not try to separate vehicles on slot, change counter, or invalid orders
			separation_valid = false;
		}
		int order_ticks;
		if (order->GetType() == OT_GOTO_STATION && (v->current_order.IsType(OT_LOADING) || v->current_order.IsType(OT_LOADING_ADVANCE)) &&
				v->last_station_visited == order->GetDestination()) {
			order_count++;
			order_ticks = order->GetTravelTime() + v->current_loading_time;
			cumulative_ticks += order->GetTravelTime() + std::min(v->current_loading_time, order->GetWaitTime());
		} else {
			order_ticks = v->current_order_time;
			cumulative_ticks += std::min(v->current_order_time, order->GetTravelTime());
		}

		out.push_back({ v->index, order_count, order_ticks, separation_valid ? cumulative_ticks : -1 });
	}

	std::sort(out.begin(), out.end());

	return out;
}

void UpdateSeparationOrder(Vehicle *v_start)
{
	SetBit(v_start->vehicle_flags, VF_SEPARATION_ACTIVE);

	std::vector<TimetableProgress> progress_array = PopulateSeparationState(v_start);
	if (progress_array.size() < 2) return;

	const uint duration = v_start->orders->GetTotalDuration();
	Vehicle *v = Vehicle::Get(progress_array.back().id);
	Vehicle *v_ahead = Vehicle::Get(progress_array.front().id);
	uint behind_index = (uint)progress_array.size() - 1;
	for (uint i = 0; i < progress_array.size(); i++) {
		const TimetableProgress &info_behind = progress_array[behind_index];
		behind_index = i;
		Vehicle *v_behind = v;

		const TimetableProgress &info = progress_array[i];
		v = v_ahead;

		uint ahead_index = (i + 1 == progress_array.size()) ? 0 : i + 1;
		const TimetableProgress &info_ahead = progress_array[ahead_index];
		v_ahead = Vehicle::Get(info_ahead.id);

		if (HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED) &&
				HasBit(v_ahead->vehicle_flags, VF_TIMETABLE_STARTED) &&
				HasBit(v_behind->vehicle_flags, VF_TIMETABLE_STARTED)) {
			if (info_behind.IsValidForSeparation() && info.IsValidForSeparation() && info_ahead.IsValidForSeparation()) {
				/*
				 * The below is equivalent to:
				 * int separation_ahead = info_ahead.cumulative_ticks - info.cumulative_ticks;
				 * int separation_behind = info.cumulative_ticks - info_behind.cumulative_ticks;
				 * int separation_delta = separation_ahead - separation_behind;
				 */
				int separation_delta = info_ahead.cumulative_ticks + info_behind.cumulative_ticks - (2 * info.cumulative_ticks);

				if (i == 0) {
					separation_delta -= duration;
				} else if (ahead_index == 0) {
					separation_delta += duration;
				}

				Company *owner = Company::GetIfValid(v->owner);
				uint8_t timetable_separation_rate = owner ? owner->settings.auto_timetable_separation_rate : 100;
				int new_lateness = separation_delta / 2;
				v->lateness_counter = (new_lateness * timetable_separation_rate +
						v->lateness_counter * (100 - timetable_separation_rate)) / 100;
			}
		}
	}
}

/**
 * Get next scheduled dispatch time
 * @param ds Dispatch schedule.
 * @param leave_time Leave time.
 * @return Pair of:
 * * Dispatch time, or INVALID_STATE_TICKS
 * * Index of departure slot, or -1
 */
std::pair<StateTicks, int> GetScheduledDispatchTime(const DispatchSchedule &ds, StateTicks leave_time)
{
	const uint32_t dispatch_duration = ds.GetScheduledDispatchDuration();
	const int32_t max_delay          = ds.GetScheduledDispatchDelay();
	const StateTicks minimum         = leave_time - max_delay;
	StateTicks begin_time            = ds.GetScheduledDispatchStartTick();
	if (ds.GetScheduledDispatchReuseSlots()) {
		begin_time -= dispatch_duration;
	}

	int32_t last_dispatched_offset;
	if (ds.GetScheduledDispatchLastDispatch() == INVALID_SCHEDULED_DISPATCH_OFFSET || ds.GetScheduledDispatchReuseSlots()) {
		last_dispatched_offset = -1;
	} else {
		last_dispatched_offset = ds.GetScheduledDispatchLastDispatch();
	}

	StateTicks first_slot = INVALID_STATE_TICKS;
	int first_slot_index = -1;

	/* Find next available slots */
	int slot_idx = 0;
	for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
		int this_slot = slot_idx++;

		auto current_offset = slot.offset;
		if (current_offset >= dispatch_duration) continue;

		int32_t threshold = last_dispatched_offset;
		if (HasBit(slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) threshold--;
		if ((int32_t)current_offset <= threshold) {
			current_offset += dispatch_duration * ((threshold + dispatch_duration - current_offset) / dispatch_duration);
		}

		StateTicks current_departure = begin_time + current_offset;
		if (current_departure < minimum) {
			current_departure += dispatch_duration * ((minimum + dispatch_duration - current_departure - 1) / dispatch_duration);
		}

		if (first_slot == INVALID_STATE_TICKS || first_slot > current_departure) {
			first_slot = current_departure;
			first_slot_index = this_slot;
		}
	}

	return std::make_pair(first_slot, first_slot_index);
}

LastDispatchRecord MakeLastDispatchRecord(const DispatchSchedule &ds, StateTicks slot, int slot_index)
{
	uint8_t record_flags = 0;
	if (slot_index == 0) SetBit(record_flags, LastDispatchRecord::RF_FIRST_SLOT);
	if (slot_index == (int)(ds.GetScheduledDispatch().size() - 1)) SetBit(record_flags, LastDispatchRecord::RF_LAST_SLOT);
	const DispatchSlot &dispatch_slot = ds.GetScheduledDispatch()[slot_index];
	return {
		slot,
		dispatch_slot.offset,
		dispatch_slot.flags,
		record_flags,
	};
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
	for (Order *o = v->GetFirstOrder(); o != nullptr && o->IsType(OT_IMPLICIT); o = o->next) {
		++first_manual_order;
	}

	bool just_started = false;
	bool set_scheduled_dispatch = false;

	/* Start scheduled dispatch at first opportunity */
	if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->cur_implicit_order_index != INVALID_VEH_ORDER_ID) {
		Order *real_implicit_order = v->GetOrder(v->cur_implicit_order_index);
		if (real_implicit_order->IsScheduledDispatchOrder(true) && travelling) {
			DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(real_implicit_order->GetDispatchScheduleIndex());

			/* Update scheduled information */
			ds.UpdateScheduledDispatch(v);

			const int wait_offset = real_current_order->GetTimetabledWait();

			StateTicks slot;
			int slot_index;
			std::tie(slot, slot_index) = GetScheduledDispatchTime(ds, _state_ticks + wait_offset);

			if (slot != INVALID_STATE_TICKS) {
				just_started = !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED);
				SetBit(v->vehicle_flags, VF_TIMETABLE_STARTED);
				v->lateness_counter = (_state_ticks - slot + wait_offset).AsTicks();
				ds.SetScheduledDispatchLastDispatch((slot - ds.GetScheduledDispatchStartTick()).AsTicks());
				SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
				set_scheduled_dispatch = true;
				v->dispatch_records[static_cast<uint16_t>(real_implicit_order->GetDispatchScheduleIndex())] = MakeLastDispatchRecord(ds, slot, slot_index);
			}
		}
	}

	/* Start automated timetables at first opportunity */
	if (!HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED) && HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
		v->ClearSeparation();
		SetBit(v->vehicle_flags, VF_TIMETABLE_STARTED);
		/* If the lateness is set by scheduled dispatch above, do not reset */
		if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) v->lateness_counter = 0;
		if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) UpdateSeparationOrder(v);
		SetTimetableWindowsDirty(v);
		return;
	}

	/* This vehicle is arriving at the first destination in the timetable. */
	if (v->cur_real_order_index == first_manual_order && travelling) {
		/* If the start date hasn't been set, or it was set automatically when
		 * the vehicle last arrived at the first destination, update it to the
		 * current time. Otherwise set the late counter appropriately to when
		 * the vehicle should have arrived. */
		if (!set_scheduled_dispatch) just_started = !HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED);

		if (v->timetable_start != 0) {
			v->lateness_counter = (_state_ticks - v->timetable_start).AsTicks();
			v->timetable_start = 0;
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
		assert_msg(real_timetable_order == real_current_order, "{}, {}", v->cur_real_order_index, v->cur_timetable_order_index);
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
		uint rounding_factor = owner ? owner->settings.timetable_autofill_rounding : 0;
		if (rounding_factor == 0) rounding_factor = _settings_game.game_time.time_in_minutes ? _settings_game.game_time.ticks_per_minute : DAY_TICKS;
		uint time_to_set = CeilDiv(std::max(time_taken, 1U), rounding_factor) * rounding_factor;

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
		int32_t new_time;
		if (travelling) {
			new_time = time_taken;
			if (new_time > (int32_t)timetabled * 4 && new_time > (int32_t)timetabled + 3000 && !(real_timetable_order->IsType(OT_GOTO_DEPOT) && (real_timetable_order->GetDepotOrderType() & ODTFB_SERVICE))) {
				/* Possible jam, clear time and restart timetable for all vehicles.
				 * Otherwise we risk trains blocking 1-lane stations for long times. */
				ChangeTimetable(v, v->cur_timetable_order_index, 0, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, false);
				if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
					for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
						/* Clear VF_TIMETABLE_STARTED but do not call ClearSeparation */
						ClrBit(v2->vehicle_flags, VF_TIMETABLE_STARTED);
						v2->lateness_counter = 0;
					}
				}
				SetTimetableWindowsDirty(v);
				return;
			} else if (new_time >= (int32_t)timetabled / 2) {
				/* Compute running average, with sign conversion to avoid negative overflow.
				 * This is biased to favour negative adjustments */
				if (new_time < (int32_t)timetabled) {
					new_time = ((int32_t)timetabled * 3 + new_time * 2 + 2) / 5;
				} else {
					new_time = ((int32_t)timetabled * 9 + new_time + 5) / 10;
				}
			} else {
				/* new time is less than half the old time, set value directly */
			}
		} else {
			new_time = time_loading;
			/* Compute running average, with sign conversion to avoid negative overflow.
			 * This is biased to favour positive adjustments */
			if (new_time > (int32_t)timetabled) {
				new_time = ((int32_t)timetabled * 3 + new_time * 2 + 2) / 5;
			} else {
				new_time = ((int32_t)timetabled * 9 + new_time + 5) / 10;
			}
		}

		if (new_time < 1) new_time = 1;
		if (new_time != (int32_t)timetabled) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, travel_field ? MTF_TRAVEL_TIME : MTF_WAIT_TIME, true);
		}
	} else if (timetabled == 0 && HasBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
		/* Add times for orders that are not yet timetabled, even while not autofilling */
		const int32_t new_time = travelling ? time_taken : time_loading;
		if (travel_field) {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_TRAVEL_TIME, true);
		} else {
			ChangeTimetable(v, v->cur_timetable_order_index, new_time, MTF_WAIT_TIME, true);
		}
	}

	bool is_timetabled = travel_field ? real_timetable_order->IsTravelTimetabled() :
			real_timetable_order->IsWaitTimetabled();

	/* Vehicles will wait at stations if they arrive early even if they are not
	 * timetabled to wait there, so make sure the lateness counter is updated
	 * when this happens. */
	if (timetabled == 0 && !is_timetabled && (travelling || v->lateness_counter >= 0)) return;

	if (set_scheduled_dispatch) {
		// do nothing
	} else if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) && HasBit(v->vehicle_flags, VF_TIMETABLE_STARTED)) {
		v->current_order_time = time_taken;
		v->current_loading_time = time_loading;
		UpdateSeparationOrder(v);
		v->current_order_time = 0;
		v->current_loading_time = 0;
	} else {
		v->lateness_counter -= (timetabled - time_taken);
	}

	/* When we are more late than this timetabled bit takes we (somewhat expensively)
	 * check how many ticks the (fully filled) timetable has. If a timetable cycle is
	 * shorter than the amount of ticks we are late we reduce the lateness by the
	 * length of a full cycle till lateness is less than the length of a timetable
	 * cycle. When the timetable isn't fully filled the cycle will be INVALID_TICKS. */
	if (v->lateness_counter > (int)timetabled) {
		Ticks cycle = v->orders->GetTimetableTotalDuration();
		if (cycle != INVALID_TICKS && v->lateness_counter > cycle) {
			if (cycle == 0) {
				v->lateness_counter = 0;
			} else {
				v->lateness_counter %= cycle;
			}
		}
	}

	SetTimetableWindowsDirty(v);
}

void SetOrderFixedWaitTime(Vehicle *v, VehicleOrderID order_number, uint32_t wait_time, bool wait_timetabled, bool wait_fixed) {
	ChangeTimetable(v, order_number, wait_time, MTF_WAIT_TIME, wait_timetabled, true);
	ChangeTimetable(v, order_number, wait_fixed ? 1 : 0, MTF_SET_WAIT_FIXED, false, true);
}
