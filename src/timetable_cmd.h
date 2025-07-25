/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timetable_cmd.h Command definitions related to timetables. */

#ifndef TIMETABLE_CMD_H
#define TIMETABLE_CMD_H

#include "command_type.h"
#include "date_type.h"
#include "order_type.h"

/**
 * Enumeration for the data to set in #CmdChangeTimetable.
 */
enum ModifyTimetableFlags : uint8_t {
	MTF_WAIT_TIME,       ///< Set wait time.
	MTF_TRAVEL_TIME,     ///< Set travel time.
	MTF_TRAVEL_SPEED,    ///< Set max travel speed.
	MTF_SET_WAIT_FIXED,  ///< Set wait time fixed flag state.
	MTF_SET_TRAVEL_FIXED,///< Set travel time fixed flag state.
	MTF_SET_LEAVE_TYPE,  ///< Passes an OrderLeaveType.
	MTF_ASSIGN_SCHEDULE, ///< Assign a dispatch schedule.
	MTF_END
};

/**
 * Control flags for #CmdChangeTimetable.
 */
enum ModifyTimetableCtrlFlags : uint8_t {
	MTCF_NONE        = 0,      ///< No flags set
	MTCF_CLEAR_FIELD = 1 << 0, ///< Clear field
};
DECLARE_ENUM_AS_BIT_SET(ModifyTimetableCtrlFlags)

DEF_CMD_TUPLE_NT(CMD_CHANGE_TIMETABLE,                 CmdChangeTimetable,               {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID, ModifyTimetableFlags, uint32_t, ModifyTimetableCtrlFlags>)
DEF_CMD_TUPLE_NT(CMD_BULK_CHANGE_TIMETABLE,            CmdBulkChangeTimetable,           {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, ModifyTimetableFlags, uint32_t, ModifyTimetableCtrlFlags>)
DEF_CMD_TUPLE_NT(CMD_SET_VEHICLE_ON_TIME,              CmdSetVehicleOnTime,              {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_AUTOFILL_TIMETABLE,               CmdAutofillTimetable,             {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool, bool>)
DEF_CMD_TUPLE_NT(CMD_AUTOMATE_TIMETABLE,               CmdAutomateTimetable,             {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_TIMETABLE_SEPARATION,             CmdTimetableSeparation,           {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_SET_TIMETABLE_START,              CmdSetTimetableStart,             {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool, StateTicks>)

DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH,                     CmdSchDispatch,                   {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_ADD,                 CmdSchDispatchAdd,                {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_REMOVE,              CmdSchDispatchRemove,             {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SET_DURATION,        CmdSchDispatchSetDuration,        {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SET_START_DATE,      CmdSchDispatchSetStartDate,       {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, StateTicks>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SET_DELAY,           CmdSchDispatchSetDelay,           {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SET_REUSE_SLOTS,     CmdSchDispatchSetReuseSlots,      {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, bool>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_RESET_LAST_DISPATCH, CmdSchDispatchResetLastDispatch,  {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_CLEAR,               CmdSchDispatchClear,              {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_ADD_NEW_SCHEDULE,    CmdSchDispatchAddNewSchedule,     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, StateTicks, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_REMOVE_SCHEDULE,     CmdSchDispatchRemoveSchedule,     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_RENAME_SCHEDULE,     CmdSchDispatchRenameSchedule,     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, std::string>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_DUPLICATE_SCHEDULE,  CmdSchDispatchDuplicateSchedule,  {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_APPEND_VEH_SCHEDULE, CmdSchDispatchAppendVehSchedules, {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleID>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_ADJUST,              CmdSchDispatchAdjust,             {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, int32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_ADJUST_SLOT,         CmdSchDispatchAdjustSlot,         {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t, int32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SWAP_SCHEDULES,      CmdSchDispatchSwapSchedules,      {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_SET_SLOT_FLAGS,      CmdSchDispatchSetSlotFlags,       {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint32_t, uint16_t, uint16_t>)
DEF_CMD_TUPLE_NT(CMD_SCH_DISPATCH_RENAME_TAG,          CmdSchDispatchRenameTag,          {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, uint32_t, uint16_t, std::string>)

struct SchDispatchBulkAddCmdData final : public CommandPayloadSerialisable<SchDispatchBulkAddCmdData> {
	VehicleID veh;
	uint32_t schedule_index;
	std::vector<std::pair<uint32_t, uint16_t>> slots;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const override;
};

DEF_CMD_DIRECT_NT(CMD_SCH_DISPATCH_BULK_ADD,           CmdSchDispatchBulkAdd,            {}, CMDT_ROUTE_MANAGEMENT, SchDispatchBulkAddCmdData)

#endif /* TIMETABLE_CMD_H */
