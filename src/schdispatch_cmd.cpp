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
		}
		SetTimetableWindowsDirty(v, true);
	}

	return CommandCost();
}

/**
 * Add scheduled dispatch time offset
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Offset time to add.
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0 - 31)  - the offset for additional slots
 *  - p3 = (bit 32 - 47) - the number of additional slots to add
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAdd(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);
	uint32 offset = GB(p3, 0, 32);
	uint32 extra_slots = GB(p3, 32, 16);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (extra_slots > 512) return_cmd_error(STR_ERROR_SCHDISPATCH_TRIED_TO_ADD_TOO_MANY_SLOTS);
	if (extra_slots > 0 && offset == 0) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.AddScheduledDispatch(p2);
		for (uint i = 0; i < extra_slots; i++) {
			p2 += offset;
			if (p2 >= ds.GetScheduledDispatchDuration()) p2 -= ds.GetScheduledDispatchDuration();
			ds.AddScheduledDispatch(p2);
		}
		SetTimetableWindowsDirty(v, true);
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
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).RemoveScheduledDispatch(p2);
		SetTimetableWindowsDirty(v, true);
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
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.SetScheduledDispatchDuration(p2);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, true);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch start date
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Date to add.
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0 - 15)  - Full date fraction
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetStartDate(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	int32 date = (int32)p2;
	uint16 full_date_fract = GB(p3, 0, 16);

	if (flags & DC_EXEC) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.SetScheduledDispatchStartDate(date, full_date_fract);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, true);
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
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchDelay(p2);
		SetTimetableWindowsDirty(v, true);
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
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchLastDispatch(0);
		SetTimetableWindowsDirty(v, true);
	}

	return CommandCost();
}

/**
 * Clear scheduled dispatch schedule
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchClear(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).ClearScheduledDispatch();
		SetTimetableWindowsDirty(v, true);
	}

	return CommandCost();
}

/**
 * Add a new scheduled dispatch schedule
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Duration, in scaled tick
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0 - 31)  - Start date
 *  - p3 = (bit 32 - 47) - Full date fraction
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAddNewSchedule(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;
	if (v->orders->GetScheduledDispatchScheduleCount() >= 4096) return CMD_ERROR;

	int32 date = GB(p3, 0, 32);
	uint16 full_date_fract = GB(p3, 32, 16);

	if (flags & DC_EXEC) {
		v->orders->GetScheduledDispatchScheduleSet().emplace_back();
		DispatchSchedule &ds = v->orders->GetScheduledDispatchScheduleSet().back();
		ds.SetScheduledDispatchDuration(p2);
		ds.SetScheduledDispatchStartDate(date, full_date_fract);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, true);
	}

	return CommandCost();
}

/**
 * Remove scheduled dispatch schedule
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchRemoveSchedule(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		std::vector<DispatchSchedule> &scheds = v->orders->GetScheduledDispatchScheduleSet();
		scheds.erase(scheds.begin() + schedule_index);
		for (Order *o = v->GetFirstOrder(); o != nullptr; o = o->next) {
			int idx = o->GetDispatchScheduleIndex();
			if (idx == (int)schedule_index) {
				o->SetDispatchScheduleIndex(-1);
			} else if (idx > (int)schedule_index) {
				o->SetDispatchScheduleIndex(idx - 1);
			}
			if (o->IsType(OT_CONDITIONAL) && o->GetConditionVariable() == OCV_DISPATCH_SLOT) {
				uint16 dispatch_slot = GB(o->GetXData(), 0, 16);
				if (dispatch_slot == UINT16_MAX) {
					/* do nothing */
				} else if (dispatch_slot == schedule_index) {
					SB(o->GetXDataRef(), 0, 16, UINT16_MAX);
				} else if (dispatch_slot > schedule_index) {
					SB(o->GetXDataRef(), 0, 16, (uint16)(dispatch_slot - 1));
				}
			}
		}
		SchdispatchInvalidateWindows(v);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch slot list.
 * @param dispatch_list The offset time list, must be correctly sorted.
 */
void DispatchSchedule::SetScheduledDispatch(std::vector<uint32> dispatch_list)
{
	this->scheduled_dispatch = std::move(dispatch_list);
	assert(std::is_sorted(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end()));
	if (this->IsScheduledDispatchValid()) this->UpdateScheduledDispatch(nullptr);
}

/**
 * Add new scheduled dispatch slot at offsets time.
 * @param offset The offset time to add.
 */
void DispatchSchedule::AddScheduledDispatch(uint32 offset)
{
	/* Maintain sorted list status */
	auto insert_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), offset);
	if (insert_position != this->scheduled_dispatch.end() && *insert_position == offset) {
		return;
	}
	this->scheduled_dispatch.insert(insert_position, offset);
	this->UpdateScheduledDispatch(nullptr);
}

/**
 * Remove scheduled dispatch slot at offsets time.
 * @param offset The offset time to remove.
 */
void DispatchSchedule::RemoveScheduledDispatch(uint32 offset)
{
	/* Maintain sorted list status */
	auto erase_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), offset);
	if (erase_position == this->scheduled_dispatch.end() || *erase_position != offset) {
		return;
	}
	this->scheduled_dispatch.erase(erase_position);
}

bool DispatchSchedule::UpdateScheduledDispatchToDate(DateTicksScaled now)
{
	bool update_windows = false;
	if (this->GetScheduledDispatchStartTick() == 0) {
		int64 start = now - (now % this->GetScheduledDispatchDuration());
		SchdispatchConvertToFullDateFract(
				start,
				&this->scheduled_dispatch_start_date, &this->scheduled_dispatch_start_full_date_fract);
		int64 last_dispatch = -start;
		if (last_dispatch < INT_MIN && _settings_game.game_time.time_in_minutes) {
			/* Advance by multiples of 24 hours */
			const int64 day = 24 * 60 * _settings_game.game_time.ticks_per_minute;
			this->scheduled_dispatch_last_dispatch = last_dispatch + (CeilDivT<int64>(INT_MIN - last_dispatch, day) * day);
		} else {
			this->scheduled_dispatch_last_dispatch = ClampToI32(last_dispatch);
		}
	}
	/* Most of the time this loop does not runs. It makes sure start date in in past */
	while (this->GetScheduledDispatchStartTick() > now) {
		OverflowSafeInt32 last_dispatch = this->scheduled_dispatch_last_dispatch;
		last_dispatch += this->GetScheduledDispatchDuration();
		this->scheduled_dispatch_last_dispatch = last_dispatch;
		SchdispatchConvertToFullDateFract(
				this->GetScheduledDispatchStartTick() - this->GetScheduledDispatchDuration(),
				&this->scheduled_dispatch_start_date, &this->scheduled_dispatch_start_full_date_fract);
		update_windows = true;
	}
	/* Most of the time this loop runs once. It makes sure the start date is as close to current time as possible. */
	while (this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration() <= now) {
		OverflowSafeInt32 last_dispatch = this->scheduled_dispatch_last_dispatch;
		last_dispatch -= this->GetScheduledDispatchDuration();
		this->scheduled_dispatch_last_dispatch = last_dispatch;
		SchdispatchConvertToFullDateFract(
				this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration(),
				&this->scheduled_dispatch_start_date, &this->scheduled_dispatch_start_full_date_fract);
		update_windows = true;
	}
	return update_windows;
}

/**
 * Update the scheduled dispatch start time to be the most recent possible.
 */
void DispatchSchedule::UpdateScheduledDispatch(const Vehicle *v)
{
	if (this->UpdateScheduledDispatchToDate(_scaled_date_ticks) && v != nullptr) {
		SetTimetableWindowsDirty(v, true);
	}
}
