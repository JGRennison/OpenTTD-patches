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
#include "company_base.h"
#include "settings_type.h"
#include "schdispatch.h"
#include "vehicle_gui.h"
#include "timetable_cmd.h"
#include "3rdparty/nlohmann/json.hpp"

#include <algorithm>

#include "table/strings.h"

#include "safeguards.h"

/**
 * Enable or disable scheduled dispatch
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param enable Whether to enable scheduled dispatch.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatch(DoCommandFlags flags, VehicleID veh, bool enable)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (enable && (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) || v->HasUnbunchingOrder())) return CommandCost(STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE);

	if (flags.Test(DoCommandFlag::Execute)) {
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			AssignBit(v2->vehicle_flags, VF_SCHEDULED_DISPATCH, enable);
		}
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Add scheduled dispatch time offset
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param schedule_index Schedule index.
 * @param time Time to add.
 * @param offset The offset for additional slots
 * @param extra_slots The number of additional slots to add
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchAdd(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t time, uint32_t offset, uint32_t extra_slots)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (extra_slots > 512) return CommandCost(STR_ERROR_SCHDISPATCH_TRIED_TO_ADD_TOO_MANY_SLOTS);
	if (extra_slots > 0 && offset == 0) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.AddScheduledDispatch(time);
		for (uint i = 0; i < extra_slots; i++) {
			time += offset;
			if (time >= ds.GetScheduledDispatchDuration()) time -= ds.GetScheduledDispatchDuration();
			ds.AddScheduledDispatch(time);
		}
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Remove scheduled dispatch time offset
 * @param flags Operation to perform.
 * @param veh Vehicle index.
 * @param schedule_index Schedule index.
 * @param time Time to remove.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchRemove(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t time)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).RemoveScheduledDispatch(time);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch duration
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param duration Duration, in scaled tick
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSetDuration(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t duration)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || duration == 0) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.SetScheduledDispatchDuration(duration);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch start date
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param start_tick Start tick.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSetStartDate(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, StateTicks start_tick)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.SetScheduledDispatchStartTick(start_tick);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch maximum allow delay
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param max_delay Maximum Delay, in scaled tick
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSetDelay(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t max_delay)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchDelay(max_delay);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch maximum allow delay
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param re_use_slots Whether to re-use slots
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSetReuseSlots(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, bool re_use_slots)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchReuseSlots(re_use_slots);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchResetLastDispatch(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Clear scheduled dispatch schedule
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchClear(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).ClearScheduledDispatch();
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Add a new scheduled dispatch schedule
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param start_tick Start tick
 * @param duration Duration, in scaled tick
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchAddNewSchedule(DoCommandFlags flags, VehicleID veh, StateTicks start_tick, uint32_t duration)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || duration == 0) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;
	if (v->orders->GetScheduledDispatchScheduleCount() >= 4096) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetScheduledDispatchScheduleSet().emplace_back();
		DispatchSchedule &ds = v->orders->GetScheduledDispatchScheduleSet().back();
		ds.SetScheduledDispatchDuration(duration);
		ds.SetScheduledDispatchStartTick(start_tick);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Remove scheduled dispatch schedule
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchRemoveSchedule(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		std::vector<DispatchSchedule> &scheds = v->orders->GetScheduledDispatchScheduleSet();
		scheds.erase(scheds.begin() + schedule_index);
		for (Order *o : v->Orders()) {
			int idx = o->GetDispatchScheduleIndex();
			if (idx == (int)schedule_index) {
				o->SetDispatchScheduleIndex(-1);
			} else if (idx > (int)schedule_index) {
				o->SetDispatchScheduleIndex(idx - 1);
			}
			if (o->IsType(OT_CONDITIONAL) && o->GetConditionVariable() == OCV_DISPATCH_SLOT) {
				uint16_t order_schedule = o->GetConditionDispatchScheduleID();
				if (order_schedule == UINT16_MAX) {
					/* do nothing */
				} else if (order_schedule == schedule_index) {
					o->SetConditionDispatchScheduleID(UINT16_MAX);
				} else if (order_schedule > schedule_index) {
					o->SetConditionDispatchScheduleID((uint16_t)(order_schedule - 1));
				}
			}
		}
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (v2->dispatch_records.empty()) continue;

			btree::btree_map<uint16_t, LastDispatchRecord> new_records;
			for (auto &iter : v2->dispatch_records) {
				if (iter.first < schedule_index) {
					new_records[iter.first] = std::move(iter.second);
				} else if (iter.first > schedule_index) {
					new_records[iter.first - 1] = std::move(iter.second);
				}
			}
			v2->dispatch_records = std::move(new_records);
		}
		SchdispatchInvalidateWindows(v);
	}

	return CommandCost();
}

/**
 * Rename scheduled dispatch schedule
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchRenameSchedule(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, const std::string &name)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	bool reset = name.empty();

	if (!reset) {
		if (Utf8StringLength(name) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		if (reset) {
			v->orders->GetDispatchScheduleByIndex(schedule_index).ScheduleName().clear();
		} else {
			v->orders->GetDispatchScheduleByIndex(schedule_index).ScheduleName() = name;
		}
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH | STWDF_ORDERS);
	}

	return CommandCost();
}

/**
 * Rename scheduled dispatch departure tag
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param tag_id Tag ID
 * @param name name
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchRenameTag(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint16_t tag_id, const std::string &name)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
	if (tag_id >= DispatchSchedule::DEPARTURE_TAG_COUNT) return CMD_ERROR;

	if (Utf8StringLength(name) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetSupplementaryName(SDSNT_DEPARTURE_TAG, tag_id, name);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH | STWDF_ORDERS);
	}

	return CommandCost();
}

/**
 * Duplicate scheduled dispatch schedule
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchDuplicateSchedule(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;
	if (v->orders->GetScheduledDispatchScheduleCount() >= 4096) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		DispatchSchedule &ds = v->orders->GetScheduledDispatchScheduleSet().emplace_back(v->orders->GetDispatchScheduleByIndex(schedule_index));
		ds.SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Append scheduled dispatch schedules from another vehicle
 *
 * @param flags Operation to perform.
 * @param dst_veh Vehicle index to append to
 * @param src_veh Vehicle index to copy from
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchAppendVehSchedules(DoCommandFlags flags, VehicleID dst_veh, VehicleID src_veh)
{
	Vehicle *v1 = Vehicle::GetIfValid(dst_veh);
	if (v1 == nullptr || !v1->IsPrimaryVehicle()) return CMD_ERROR;

	const Vehicle *v2 = Vehicle::GetIfValid(src_veh);
	if (v2 == nullptr || !v2->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v1->owner);
	if (ret.Failed()) return ret;

	if (v1->orders == nullptr || v2->orders == nullptr || v1->orders == v2->orders) return CMD_ERROR;

	if (v1->orders->GetScheduledDispatchScheduleCount() + v2->orders->GetScheduledDispatchScheduleCount() > 4096) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		for (uint i = 0; i < v2->orders->GetScheduledDispatchScheduleCount(); i++) {
			DispatchSchedule &ds = v1->orders->GetScheduledDispatchScheduleSet().emplace_back(v2->orders->GetDispatchScheduleByIndex(i));
			ds.SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
			ds.UpdateScheduledDispatch(nullptr);
		}
		SetTimetableWindowsDirty(v1, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Adjust scheduled dispatch time offsets
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param adjustment Signed adjustment
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchAdjust(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, int32_t adjustment)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
	if (abs(adjustment) >= (int)ds.GetScheduledDispatchDuration()) return CommandCost(STR_ERROR_SCHDISPATCH_ADJUSTMENT_TOO_LARGE);

	if (flags.Test(DoCommandFlag::Execute)) {
		ds.AdjustScheduledDispatch(adjustment);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Adjust scheduled dispatch time offset of a single departure slot
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param offset Slot offset.
 * @param adjustment Signed adjustment.
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchAdjustSlot(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t offset, int32_t adjustment)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
	if (abs(adjustment) >= (int)ds.GetScheduledDispatchDuration()) return CommandCost(STR_ERROR_SCHDISPATCH_ADJUSTMENT_TOO_LARGE);

	uint32_t new_offset = ds.AdjustScheduledDispatchOffset(offset, adjustment);
	for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
		if (slot.offset == new_offset) return CommandCost(STR_ERROR_SCHDISPATCH_SLOT_ALREADY_EXISTS_AT_ADJUSTED_TIME);
	}

	for (DispatchSlot &slot : ds.GetScheduledDispatchMutable()) {
		if (slot.offset == offset) {
			if (flags.Test(DoCommandFlag::Execute)) {
				slot.offset = new_offset;
				ds.ResortDispatchOffsets();
				ds.UpdateScheduledDispatch(nullptr);
				SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
			}
			CommandCost cost;
			cost.SetResultData(new_offset);
			return cost;
		}
	}

	return CMD_ERROR;
}

/**
 * Swap two schedules in dispatch schedule list
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index_1 Schedule index.
 * @param schedule_index_2 Schedule index.
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSwapSchedules(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index_1, uint32_t schedule_index_2)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index_1 == schedule_index_2) return CMD_ERROR;
	if (schedule_index_1 >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
	if (schedule_index_2 >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		std::swap(v->orders->GetDispatchScheduleByIndex(schedule_index_1), v->orders->GetDispatchScheduleByIndex(schedule_index_2));
		for (Order *o : v->Orders()) {
			int idx = o->GetDispatchScheduleIndex();
			if (idx == (int)schedule_index_1) {
				o->SetDispatchScheduleIndex((int)schedule_index_2);
			} else if (idx == (int)schedule_index_2) {
				o->SetDispatchScheduleIndex((int)schedule_index_1);
			}
			if (o->IsType(OT_CONDITIONAL) && o->GetConditionVariable() == OCV_DISPATCH_SLOT) {
				uint16_t order_schedule = o->GetConditionDispatchScheduleID();
				if (order_schedule == schedule_index_1) {
					o->SetConditionDispatchScheduleID(schedule_index_2);
				} else if (order_schedule == schedule_index_2) {
					o->SetConditionDispatchScheduleID(schedule_index_1);
				}
			}
		}
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (v2->dispatch_records.empty()) continue;

			auto iter_1 = v2->dispatch_records.find(static_cast<uint16_t>(schedule_index_1));
			auto iter_2 = v2->dispatch_records.find(static_cast<uint16_t>(schedule_index_2));
			if (iter_1 != v2->dispatch_records.end() && iter_2 != v2->dispatch_records.end()) {
				std::swap(iter_1->second, iter_2->second);
			} else if (iter_1 != v2->dispatch_records.end()) {
				LastDispatchRecord r = std::move(iter_1->second);
				v2->dispatch_records.erase(iter_1);
				v2->dispatch_records[static_cast<uint16_t>(schedule_index_2)] = std::move(r);
			} else if (iter_2 != v2->dispatch_records.end()) {
				LastDispatchRecord r = std::move(iter_2->second);
				v2->dispatch_records.erase(iter_2);
				v2->dispatch_records[static_cast<uint16_t>(schedule_index_1)] = std::move(r);
			}
		}
		SchdispatchInvalidateWindows(v);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Add scheduled dispatch time offset
 *
 * @param flags Operation to perform.
 * @param veh Vehicle index
 * @param schedule_index Schedule index.
 * @param offset Slot offset.
 * @param values flag values
 * @param mask flag mask
 * @return the cost of this operation or an error
 */
CommandCost CmdSchDispatchSetSlotFlags(DoCommandFlags flags, VehicleID veh, uint32_t schedule_index, uint32_t offset, uint16_t values, uint16_t mask)
{
	const uint16_t permitted_mask = GetBitMaskSC<uint16_t>(DispatchSlot::SDSF_REUSE_SLOT, 1) | GetBitMaskFL<uint16_t>(DispatchSlot::SDSF_FIRST_TAG, DispatchSlot::SDSF_LAST_TAG);
	if ((mask & permitted_mask) != mask) return CMD_ERROR;
	if ((values & (~mask)) != 0) return CMD_ERROR;

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
	for (DispatchSlot &slot : ds.GetScheduledDispatchMutable()) {
		if (slot.offset == offset) {
			if (flags.Test(DoCommandFlag::Execute)) {
				slot.flags &= ~mask;
				slot.flags |= values;
				SchdispatchInvalidateWindows(v);
				SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
			}
			return CommandCost();
		}
	}

	return CMD_ERROR;
}

/**
 * Set scheduled dispatch slot list.
 * @param dispatch_list The offset time list, must be correctly sorted.
 */
void DispatchSchedule::SetScheduledDispatch(std::vector<DispatchSlot> dispatch_list)
{
	this->scheduled_dispatch = std::move(dispatch_list);
	assert(std::is_sorted(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end()));
	if (this->IsScheduledDispatchValid()) this->UpdateScheduledDispatch(nullptr);
}

/**
 * Add new scheduled dispatch slot at offsets time.
 * @param offset The offset time to add.
 */
void DispatchSchedule::AddScheduledDispatch(uint32_t offset)
{
	/* Maintain sorted list status */
	auto insert_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), DispatchSlot{ offset, 0 });
	if (insert_position != this->scheduled_dispatch.end() && insert_position->offset == offset) {
		return;
	}
	this->scheduled_dispatch.insert(insert_position, { offset, 0 });
}

/**
 * Remove scheduled dispatch slot at offsets time.
 * @param offset The offset time to remove.
 */
void DispatchSchedule::RemoveScheduledDispatch(uint32_t offset)
{
	/* Maintain sorted list status */
	auto erase_position = std::lower_bound(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end(), DispatchSlot{ offset, 0 });
	if (erase_position == this->scheduled_dispatch.end() || erase_position->offset != offset) {
		return;
	}
	this->scheduled_dispatch.erase(erase_position);
}

/**
 * Adjust a scheduled dispatch slot by a time adjustment.
 * @param offset The time slot to adjust.
 * @param adjust The time adjustment to add to the time slot.
 * @return the adjusted time slot.
 */
uint32_t DispatchSchedule::AdjustScheduledDispatchOffset(uint32_t offset, int32_t adjust)
{
	int32_t t = (int32_t)offset + adjust;
	if (t < 0) t += this->GetScheduledDispatchDuration();
	if (t >= (int32_t)this->GetScheduledDispatchDuration()) t -= (int32_t)this->GetScheduledDispatchDuration();
	return (uint32_t)t;
}

void DispatchSchedule::ResortDispatchOffsets()
{
	std::sort(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end());
}

/**
 * Adjust all scheduled dispatch slots by time adjustment.
 * @param adjust The time adjustment to add to each time slot.
 */
void DispatchSchedule::AdjustScheduledDispatch(int32_t adjust)
{
	for (DispatchSlot &slot : this->scheduled_dispatch) {
		slot.offset = this->AdjustScheduledDispatchOffset(slot.offset, adjust);
	}
	this->ResortDispatchOffsets();
}

bool DispatchSchedule::UpdateScheduledDispatchToDate(StateTicks now)
{
	bool update_windows = false;
	if (this->GetScheduledDispatchStartTick() == 0) {
		StateTicks start = now - (now.base() % this->GetScheduledDispatchDuration());
		this->SetScheduledDispatchStartTick(start);
		int64_t last_dispatch = -(start.base());
		if (last_dispatch < INT_MIN && _settings_game.game_time.time_in_minutes) {
			/* Advance by multiples of 24 hours */
			const int64_t day = 24 * 60 * _settings_game.game_time.ticks_per_minute;
			this->scheduled_dispatch_last_dispatch = last_dispatch + (CeilDivT<int64_t>(INT_MIN - last_dispatch, day) * day);
		} else {
			this->scheduled_dispatch_last_dispatch = ClampTo<int32_t>(last_dispatch);
		}
	}
	/* Most of the time this loop does not run. It makes sure start date in in past */
	while (this->GetScheduledDispatchStartTick() > now) {
		OverflowSafeInt32 last_dispatch = this->scheduled_dispatch_last_dispatch;
		if (last_dispatch != INVALID_SCHEDULED_DISPATCH_OFFSET) {
			last_dispatch += this->GetScheduledDispatchDuration();
			this->scheduled_dispatch_last_dispatch = last_dispatch;
		}
		this->SetScheduledDispatchStartTick(this->GetScheduledDispatchStartTick() - this->GetScheduledDispatchDuration());
		update_windows = true;
	}
	/* Most of the time this loop runs once. It makes sure the start date is as close to current time as possible. */
	while (this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration() <= now) {
		OverflowSafeInt32 last_dispatch = this->scheduled_dispatch_last_dispatch;
		if (last_dispatch != INVALID_SCHEDULED_DISPATCH_OFFSET) {
			last_dispatch -= this->GetScheduledDispatchDuration();
			this->scheduled_dispatch_last_dispatch = last_dispatch;
		}
		this->SetScheduledDispatchStartTick(this->GetScheduledDispatchStartTick() + this->GetScheduledDispatchDuration());
		update_windows = true;
	}
	return update_windows;
}

/**
 * Update the scheduled dispatch start time to be the most recent possible.
 */
void DispatchSchedule::UpdateScheduledDispatch(const Vehicle *v)
{
	if (this->UpdateScheduledDispatchToDate(_state_ticks) && v != nullptr) {
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}
}

std::string DispatchSchedule::ToJSONString()
{

	nlohmann::ordered_json json;

	for (int i = 0; i < DispatchSchedule::DEPARTURE_TAG_COUNT; i++) {

		std::string_view rename = this->GetSupplementaryName(SDSNT_DEPARTURE_TAG, i);

		if (!rename.empty()) {

			json["renamed-tags"][std::to_string(i + 1)] = rename;

		}
	}

	for (auto & SD_slot : this->GetScheduledDispatch()) {

		std::string stringOffset = std::to_string(SD_slot.offset);

		auto &slotJson = json["slots"][stringOffset];

		if (HasBit(SD_slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) {
			slotJson["re-use-slot"] = true;
		}
		int ctr = 0;
		for (int i = 0; i <= (DispatchSlot::SDSF_LAST_TAG - DispatchSlot::SDSF_FIRST_TAG); i++) {
			if (HasBit(SD_slot.flags, DispatchSlot::SDSF_FIRST_TAG + i)) {
				slotJson["tags"][ctr++] = std::to_string(i + 1);
			}
		}
	}

	if (!this->ScheduleName().empty()) {
		json["name"] = this->ScheduleName();
	}

	if (this->GetScheduledDispatchDuration() != _settings_client.company.default_sched_dispatch_duration ) {
		json["duration"] = this->GetScheduledDispatchDuration();
	}

	if (this->GetScheduledDispatchDelay() != 0) {
		json["delay"] = this->GetScheduledDispatchDelay();
	}

	if (this->GetScheduledDispatchReuseSlots()) {
		json["re-use-all-slots"] = true;
	}

	return json.dump();
}

static inline uint32_t SupplementaryNameKey(ScheduledDispatchSupplementaryNameType name_type, uint16_t id)
{
	return (static_cast<uint32_t>(name_type) << 16) | id;
}

std::string_view DispatchSchedule::GetSupplementaryName(ScheduledDispatchSupplementaryNameType name_type, uint16_t id) const
{
	auto iter = this->supplementary_names.find(SupplementaryNameKey(name_type, id));
	if (iter == this->supplementary_names.end()) return {};
	return iter->second;
}

void DispatchSchedule::SetSupplementaryName(ScheduledDispatchSupplementaryNameType name_type, uint16_t id, std::string name)
{
	uint32_t key = SupplementaryNameKey(name_type, id);
	if (name.empty()) {
		this->supplementary_names.erase(key);
	} else {
		this->supplementary_names[key] = std::move(name);
	}
}
