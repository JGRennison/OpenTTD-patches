/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file timetable_cmd.h Command definitions related to timetables. */

#ifndef TIMETABLE_CMD_H
#define TIMETABLE_CMD_H

#include "command_type.h"
#include "date_type.h"
#include "order_type.h"
#include "vehicle_type.h"

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

struct ScheduledDispatchSlotSet {
	static constexpr bool command_payload_as_ref = true;

	static constexpr size_t MAX_SLOTS = 512;

	std::vector<uint32_t> slots;

	void Serialise(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);

	bool IsValid() const;

	void fmt_format_value(struct format_target &) const;
};

struct ScheduledDispatchAdjustSlotResult : public CommandLargeResultBase {
	struct Change {
		uint32_t old_slot;
		uint32_t new_slot;
	};
	std::vector<Change> changes;
};

DEF_CMD_TUPLE_NT(Commands::ChangeTimetable,                  CmdChangeTimetable,               {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID, ModifyTimetableFlags, uint32_t, ModifyTimetableCtrlFlags>)
DEF_CMD_TUPLE_NT(Commands::BulkChangeTimetable,              CmdBulkChangeTimetable,           {}, CommandType::RouteManagement, CmdDataT<VehicleID, ModifyTimetableFlags, uint32_t, ModifyTimetableCtrlFlags>)
DEF_CMD_TUPLE_NT(Commands::SetVehicleOnTime,                 CmdSetVehicleOnTime,              {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(Commands::AutofillTimetable,                CmdAutofillTimetable,             {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool, bool>)
DEF_CMD_TUPLE_NT(Commands::AutomateTimetable,                CmdAutomateTimetable,             {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(Commands::TimetableSeparation,              CmdTimetableSeparation,           {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(Commands::SetTimetableStart,                CmdSetTimetableStart,             {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool, StateTicks>)

DEF_CMD_TUPLE_NT(Commands::SchDispatchSetEnabled,            CmdSchDispatchSetEnabled,         {}, CommandType::RouteManagement, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchAdd,                   CmdSchDispatchAdd,                {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint32_t, uint32_t, uint32_t, uint16_t, DispatchSlotRouteID>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchRemove,                CmdSchDispatchRemove,             {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetDuration,           CmdSchDispatchSetDuration,        {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetStartDate,          CmdSchDispatchSetStartDate,       {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, StateTicks>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetDelay,              CmdSchDispatchSetDelay,           {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetReuseSlots,         CmdSchDispatchSetReuseSlots,      {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, bool>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchResetLastDispatch,     CmdSchDispatchResetLastDispatch,  {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchClear,                 CmdSchDispatchClear,              {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchAddNewSchedule,        CmdSchDispatchAddNewSchedule,     {}, CommandType::RouteManagement, CmdDataT<VehicleID, StateTicks, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchRemoveSchedule,        CmdSchDispatchRemoveSchedule,     {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchRenameSchedule,        CmdSchDispatchRenameSchedule,     {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, std::string>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchDuplicateSchedule,     CmdSchDispatchDuplicateSchedule,  {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchAppendVehicleSchedule, CmdSchDispatchAppendVehSchedules, {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleID>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchAdjust,                CmdSchDispatchAdjust,             {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, int32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchAdjustSlot,            CmdSchDispatchAdjustSlot,         {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, ScheduledDispatchSlotSet, int32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSwapSchedules,         CmdSchDispatchSwapSchedules,      {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetSlotFlags,          CmdSchDispatchSetSlotFlags,       {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, ScheduledDispatchSlotSet, uint16_t, uint16_t>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchSetSlotRoute,          CmdSchDispatchSetSlotRoute,       {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, ScheduledDispatchSlotSet, DispatchSlotRouteID>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchRenameTag,             CmdSchDispatchRenameTag,          {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, uint16_t, std::string>)
DEF_CMD_TUPLE_NT(Commands::SchDispatchEditRoute,             CmdSchDispatchEditRoute,          {}, CommandType::RouteManagement, CmdDataT<VehicleID, uint32_t, DispatchSlotRouteID, std::string>)

#endif /* TIMETABLE_CMD_H */
