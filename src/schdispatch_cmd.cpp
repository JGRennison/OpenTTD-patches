/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file schdispatch_cmd.cpp Commands related to scheduled dispatching. */

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
#include "settings_type.h"
#include "schdispatch.h"
#include "vehicle_gui.h"

#include <algorithm>

#include "table/strings.h"

#include "safeguards.h"

/* We squeeze this amount into 14 bit of data, so we must guarantee that
   DAY_TICKS * (max_day_length_factor+1) can fit in 14-bit
   See CmdScheduledDispatchSetStartDate */
assert_compile(DAY_TICKS * 126 < 16384);

/**
 * Enable or disable scheduled dispatch
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Various bitstuffed elements
 * - p2 = (bit 0) - Set to 1 to enable, 0 to disable scheduled dispatch.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatch(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				SetBit(v2->vehicle_flags, VF_SCHEDULED_DISPATCH);
			} else {
				ClrBit(v2->vehicle_flags, VF_SCHEDULED_DISPATCH);
			}
			SetWindowDirty(WC_VEHICLE_TIMETABLE, v2->index);
			SetWindowDirty(WC_SCHDISPATCH_SLOTS, v2->index);
		}
	}

	return CommandCost();
}

/**
 * Add scheduled dispatch time offset
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Offset time to add.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAdd(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->AddScheduledDispatch(p2);
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Remove scheduled dispatch time offset
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Offset time to remove
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchRemove(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->RemoveScheduledDispatch(p2);
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch duration
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Duration, in scaled tick
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetDuration(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->SetScheduledDispatchDuration(p2);
		v->orders.list->UpdateScheduledDispatch();
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch start date
 *
 * The parameter is quite tricky. The default maximum of daylength factor is 125,
 * and with DAY_TICKS of 74 the result (maximum scaled tick per day) fits in 14 bit.
 * Vehicle index in p1 takes 20 bit, so we have 12 bit here. The MSB of the fraction is stored here.
 * The 2-bit LSB is stored in MSB of p2, which is start date. The default date is stored in int32,
 * which only have topmost bit available. However, if the date reached 31 bits, that means it is over 1,000,000 years,
 * so I think it is safe to steal another bit here.
 *
 * See also the assert_compile at the top of the file.
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 MSB of Start Full Date Fraction || Vehicle index
 * @param p2 LSB of Start Full Date Fraction || Date to add.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetStartDate(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	int32 date = (int32) GB(p2, 0, 30);
	uint16 full_date_fract = (GB(p1, 20, 12) << 2) + GB(p2, 30, 2);

	if (flags & DC_EXEC) {
		v->orders.list->SetScheduledDispatchStartDate(date, full_date_fract);
		v->orders.list->UpdateScheduledDispatch();
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch maximum allow delay
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Maximum Delay, in scaled tick
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetDelay(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->SetScheduledDispatchDelay(p2);
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Reset scheduled dispatch last dispatch vehicle time
 *
 * This is useful when the current duration is high, and the vehicle get dispatched at time in far future.
 * Thus, the last dispatch time stays high so no new vehicle are dispatched between now and that time.
 * By resetting this you set the last dispatch time to the current timetable start time,
 * allowing new vehicle to be dispatched immediately.
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchResetLastDispatch(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders.list == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders.list->SetScheduledDispatchLastDispatch(0);
		SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch slot list.
 * @param dispatch_list The offset time list, must be correctly sorted.
 */
void OrderList::SetScheduledDispatch(std::vector<uint32> dispatch_list)
{
	this->scheduled_dispatch = std::move(dispatch_list);
	assert(std::is_sorted(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end()));
	if (this->IsScheduledDispatchValid()) this->UpdateScheduledDispatch();
}

/**
 * Add new scheduled dispatch slot at offsets time.
 * @param offset The offset time to add.
 */
void OrderList::AddScheduledDispatch(uint32 offset)
{
	/* Maintain sorted list status */
	auto insert_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), offset);
	if (insert_position != this->scheduled_dispatch.end() && *insert_position == offset) {
		return;
	}
	this->scheduled_dispatch.insert(insert_position, offset);
	this->UpdateScheduledDispatch();
}

/**
 * Remove scheduled dispatch slot at offsets time.
 * @param offset The offset time to remove.
 */
void OrderList::RemoveScheduledDispatch(uint32 offset)
{
	/* Maintain sorted list status */
	auto erase_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), offset);
	if (erase_position == this->scheduled_dispatch.end() || *erase_position != offset) {
		return;
	}
	this->scheduled_dispatch.erase(erase_position);
}

/**
 * Update the scheduled dispatch start time to be the most recent possible.
 */
void OrderList::UpdateScheduledDispatch()
{
	bool update_windows = false;
	/* Most of the time this loop does not runs. It makes sure start date in in past */
	while (this->GetScheduledDispatchStartTick() > _scaled_date_ticks) {
		this->scheduled_dispatch_last_dispatch += this->GetScheduledDispatchDuration();
		SchdispatchConvertToFullDateFract(
				this->GetScheduledDispatchStartTick() - this->GetScheduledDispatchDuration(),
				&this->scheduled_dispatch_start_date, &this->scheduled_dispatch_start_full_date_fract);
		update_windows = true;
	}
	/* Most of the time this loop runs once. It makes sure the start date is as close to current time as possible. */
	while (this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration() <= _scaled_date_ticks) {
		this->scheduled_dispatch_last_dispatch -= this->GetScheduledDispatchDuration();
		SchdispatchConvertToFullDateFract(
				this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration(),
				&this->scheduled_dispatch_start_date, &this->scheduled_dispatch_start_full_date_fract);
		update_windows = true;
	}
	if (update_windows) InvalidateWindowClassesData(WC_SCHDISPATCH_SLOTS, VIWD_MODIFY_ORDERS);
}

/**
 * Reset the scheduled dispatch schedule.
 *
 * This only occurs during initialization of the scheduled dispatch for each shared order. Basically we set
 * proper default value for start time and duration
 */
void OrderList::ResetScheduledDispatch()
{
	uint32 windex = this->first_shared->index;

	Date start_date;
	uint16 start_full_date_fract;
	uint32 duration;

	if (_settings_time.time_in_minutes) {
		/* Set to 00:00 of today, and 1 day */

		DateTicksScaled val;
		val = MINUTES_DATE(MINUTES_DAY(CURRENT_MINUTE), 0, 0);
		val -= _settings_time.clock_offset;
		val *= _settings_time.ticks_per_minute;
		SchdispatchConvertToFullDateFract(val, &start_date, &start_full_date_fract);

		duration = 24 * 60 * _settings_time.ticks_per_minute;
	} else {
		/* Set Jan 1st and 365 day */
		start_date = DAYS_TILL(_cur_year);
		start_full_date_fract = 0;
		duration = 365*DAY_TICKS;
	}

	DoCommandP(0, windex, duration, CMD_SCHEDULED_DISPATCH_SET_DURATION | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));

	uint32 p1 = 0, p2 = 0;
	SB(p1, 0, 20, windex);
	SB(p1, 20, 12, GB(start_full_date_fract, 2, 12));
	SB(p2, 0, 30, start_date);
	SB(p2, 30, 2, GB(start_full_date_fract, 0, 2));

	DoCommandP(0, p1, p2, CMD_SCHEDULED_DISPATCH_SET_START_DATE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
}
