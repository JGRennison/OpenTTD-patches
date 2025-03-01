/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_cmd.h Header file for Trace Restrict commands */

#ifndef TRACERESTRICT_CMD_H
#define TRACERESTRICT_CMD_H

#include "stdafx.h"
#include "command_type.h"
#include "tracerestrict.h"

enum TraceRestrictAlterSlotOperation : uint8_t {
	TRASO_RENAME,
	TRASO_CHANGE_MAX_OCCUPANCY,
	TRASO_SET_PUBLIC,
	TRASO_SET_PARENT_GROUP,
};

enum TraceRestrictAlterSlotGroupOperation : uint8_t {
	TRASGO_RENAME,
	TRASGO_SET_PARENT_GROUP,
};

enum TraceRestrictAlterCounterOperation : uint8_t {
	TRACO_RENAME,
	TRACO_CHANGE_VALUE,
	TRACO_SET_PUBLIC,
};

struct TraceRestrictFollowUpCmdData final : public CommandPayloadSerialisable<TraceRestrictFollowUpCmdData> {
	DynBaseCommandContainer cmd;

	TraceRestrictFollowUpCmdData() = default;
	TraceRestrictFollowUpCmdData(DynBaseCommandContainer cmd) : cmd(std::move(cmd)) {}

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	CommandCost ExecuteWithValue(uint16_t value, DoCommandFlag flags) const;
	void FormatDebugSummary(struct format_target &) const override;
};

struct TraceRestrictCreateSlotCmdData final : public CommandPayloadSerialisable<TraceRestrictCreateSlotCmdData> {
	VehicleType vehtype = VEH_INVALID;
	TraceRestrictSlotGroupID parent = INVALID_TRACE_RESTRICT_SLOT_GROUP;
	std::string name;
	std::optional<TraceRestrictFollowUpCmdData> follow_up_cmd;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(struct format_target &) const override;
};

struct TraceRestrictCreateCounterCmdData final : public CommandPayloadSerialisable<TraceRestrictCreateCounterCmdData> {
	std::string name;
	std::optional<TraceRestrictFollowUpCmdData> follow_up_cmd;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(struct format_target &) const override;
};

using TraceRestrictAlterSlotCmdData = CmdDataT<TraceRestrictSlotID, TraceRestrictAlterSlotOperation, uint32_t, std::string>;
using TraceRestrictSlotVehicleCmdData = CmdDataT<TraceRestrictSlotID, VehicleID>;
using TraceRestrictCreateSlotGroupCmdData = CmdDataT<VehicleType, TraceRestrictSlotGroupID, std::string>;
using TraceRestrictAlterSlotGroupCmdData = CmdDataT<TraceRestrictSlotGroupID, TraceRestrictAlterSlotGroupOperation, TraceRestrictSlotGroupID, std::string>;
using TraceRestrictAlterCounterCmdData = CmdDataT<TraceRestrictCounterID, TraceRestrictAlterCounterOperation, uint32_t, std::string>;

DEF_CMD_PROC    (CMD_PROGRAM_TRACERESTRICT_SIGNAL,      CmdProgramSignalTraceRestrict,                                          {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_DIRECT  (CMD_CREATE_TRACERESTRICT_SLOT,         CmdCreateTraceRestrictSlot,        TraceRestrictCreateSlotCmdData,      {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_ALTER_TRACERESTRICT_SLOT,          CmdAlterTraceRestrictSlot,         TraceRestrictAlterSlotCmdData,       {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_DELETE_TRACERESTRICT_SLOT,         CmdDeleteTraceRestrictSlot,        CmdDataT<TraceRestrictSlotID>,       {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_ADD_VEHICLE_TRACERESTRICT_SLOT,    CmdAddVehicleTraceRestrictSlot,    TraceRestrictSlotVehicleCmdData,     {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT, CmdRemoveVehicleTraceRestrictSlot, TraceRestrictSlotVehicleCmdData,     {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_CREATE_TRACERESTRICT_SLOT_GROUP,   CmdCreateTraceRestrictSlotGroup,   TraceRestrictCreateSlotGroupCmdData, {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_ALTER_TRACERESTRICT_SLOT_GROUP,    CmdAlterTraceRestrictSlotGroup,    TraceRestrictAlterSlotGroupCmdData,  {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_DELETE_TRACERESTRICT_SLOT_GROUP,   CmdDeleteTraceRestrictSlotGroup,   CmdDataT<TraceRestrictSlotGroupID>,  {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_DIRECT  (CMD_CREATE_TRACERESTRICT_COUNTER,      CmdCreateTraceRestrictCounter,     TraceRestrictCreateCounterCmdData,   {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_ALTER_TRACERESTRICT_COUNTER,       CmdAlterTraceRestrictCounter,      TraceRestrictAlterCounterCmdData,    {}, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_DELETE_TRACERESTRICT_COUNTER,      CmdDeleteTraceRestrictCounter,     CmdDataT<TraceRestrictCounterID>,    {}, CMDT_OTHER_MANAGEMENT)

#endif /* TRACERESTRICT_CMD_H */
