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
CommandCost CmdScheduledDispatch(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (HasBit(p2, 0) && (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION) || v->HasUnbunchingOrder())) return CommandCost(STR_ERROR_SEPARATION_MUTUALLY_EXCLUSIVE);

	if (flags & DC_EXEC) {
		for (Vehicle *v2 = v->FirstShared(); v2 != nullptr; v2 = v2->NextShared()) {
			if (HasBit(p2, 0)) {
				SetBit(v2->vehicle_flags, VF_SCHEDULED_DISPATCH);
			} else {
				ClrBit(v2->vehicle_flags, VF_SCHEDULED_DISPATCH);
			}
		}
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchAdd(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);
	uint32_t offset = GB(p3, 0, 32);
	uint32_t extra_slots = GB(p3, 32, 16);

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
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchRemove(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchSetDuration(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || p2 == 0) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
		ds.SetScheduledDispatchDuration(p2);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch start date
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Unused.
 * @param p3 Start tick
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetStartDate(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
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
		ds.SetScheduledDispatchStartTick((StateTicks)p3);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchSetDelay(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Set scheduled dispatch maximum allow delay
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Whether to re-use slots
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetReuseSlots(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchReuseSlots(p2 != 0);
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
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchResetLastDispatch(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchClear(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
 * @param p3 Start tick
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAddNewSchedule(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || p2 == 0) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;
	if (v->orders->GetScheduledDispatchScheduleCount() >= 4096) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->GetScheduledDispatchScheduleSet().emplace_back();
		DispatchSchedule &ds = v->orders->GetScheduledDispatchScheduleSet().back();
		ds.SetScheduledDispatchDuration(p2);
		ds.SetScheduledDispatchStartTick((StateTicks)p3);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
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
CommandCost CmdScheduledDispatchRemoveSchedule(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
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
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchRenameSchedule(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	bool reset = StrEmpty(text);

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		if (reset) {
			v->orders->GetDispatchScheduleByIndex(schedule_index).ScheduleName().clear();
		} else {
			v->orders->GetDispatchScheduleByIndex(schedule_index).ScheduleName() = text;
		}
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH | STWDF_ORDERS);
	}

	return CommandCost();
}

/**
 * Rename scheduled dispatch departure tag
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Tag ID
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchRenameTag(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
	if (p2 >= DispatchSchedule::DEPARTURE_TAG_COUNT) return CMD_ERROR;

	std::string name;
	if (!StrEmpty(text)) {
		if (Utf8StringLength(text) >= MAX_LENGTH_VEHICLE_NAME_CHARS) return CMD_ERROR;
		name = text;
	}

	if (flags & DC_EXEC) {
		v->orders->GetDispatchScheduleByIndex(schedule_index).SetSupplementaryName(SDSNT_DEPARTURE_TAG, static_cast<uint16_t>(p2), std::move(name));
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH | STWDF_ORDERS);
	}

	return CommandCost();
}

/**
 * Duplicate scheduled dispatch schedule
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Not used
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchDuplicateSchedule(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;
	if (v->orders->GetScheduledDispatchScheduleCount() >= 4096) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
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
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index to append to
 * @param p2 Vehicle index to copy from
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAppendVehicleSchedules(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh1 = GB(p1, 0, 20);
	VehicleID veh2 = GB(p2, 0, 20);

	Vehicle *v1 = Vehicle::GetIfValid(veh1);
	if (v1 == nullptr || !v1->IsPrimaryVehicle()) return CMD_ERROR;

	const Vehicle *v2 = Vehicle::GetIfValid(veh2);
	if (v2 == nullptr || !v2->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v1->owner);
	if (ret.Failed()) return ret;

	if (v1->orders == nullptr || v2->orders == nullptr || v1->orders == v2->orders) return CMD_ERROR;

	if (v1->orders->GetScheduledDispatchScheduleCount() + v2->orders->GetScheduledDispatchScheduleCount() > 4096) return CMD_ERROR;

	if (flags & DC_EXEC) {
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
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 Signed adjustment
 * @param text name
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchAdjust(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);
	int32_t adjustment = p2;

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);
	if (abs(adjustment) >= (int)ds.GetScheduledDispatchDuration()) return CommandCost(STR_ERROR_SCHDISPATCH_ADJUSTMENT_TOO_LARGE);

	if (flags & DC_EXEC) {
		ds.AdjustScheduledDispatch(adjustment);
		ds.UpdateScheduledDispatch(nullptr);
		SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
	}

	return CommandCost();
}

/**
 * Swap two schedules in dispatch schedule list
 *
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index
 * @param p2 various bitstuffed elements
 *  - p2 = (bit 0 - 15)  - Schedule index 1
 *  - p2 = (bit 16 - 31) - Schedule index 2
 * @param unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSwapSchedules(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index_1 = GB(p2, 0, 16);
	uint schedule_index_2 = GB(p2, 16, 16);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (v->orders == nullptr) return CMD_ERROR;

	if (schedule_index_1 == schedule_index_2) return CMD_ERROR;
	if (schedule_index_1 >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;
	if (schedule_index_2 >= v->orders->GetScheduledDispatchScheduleCount()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		std::swap(v->orders->GetDispatchScheduleByIndex(schedule_index_1), v->orders->GetDispatchScheduleByIndex(schedule_index_2));
		for (Order *o = v->GetFirstOrder(); o != nullptr; o = o->next) {
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
 * @param tile Not used.
 * @param flags Operation to perform.
 * @param p1 Vehicle index.
 * @param p2 Slot offset.
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0 - 15)  - flag values
 *  - p3 = (bit 16 - 31) - flag mask
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdScheduledDispatchSetSlotFlags(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);
	uint32_t offset = p2;
	uint16_t values = (uint16_t)GB(p3, 0, 16);
	uint16_t mask = (uint16_t)GB(p3, 16, 16);

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
			if (flags & DC_EXEC) {
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
	this->UpdateScheduledDispatch(nullptr);
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
 * Adjust all scheduled dispatch slots by time adjustment.
 * @param adjust The time adjustment to add to each time slot.
 */
void DispatchSchedule::AdjustScheduledDispatch(int32_t adjust)
{
	for (DispatchSlot &slot : this->scheduled_dispatch) {
		int32_t t = (int32_t)slot.offset + adjust;
		if (t < 0) t += GetScheduledDispatchDuration();
		if (t >= (int32_t)GetScheduledDispatchDuration()) t -= (int32_t)GetScheduledDispatchDuration();
		slot.offset = (uint32_t)t;
	}
	std::sort(this->scheduled_dispatch.begin(), this->scheduled_dispatch.end());
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
