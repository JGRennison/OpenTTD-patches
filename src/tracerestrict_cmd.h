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

enum TraceRestrictDoCommandType : uint8_t {
	TRDCT_INSERT_ITEM,                       ///< insert new instruction before offset field as given value
	TRDCT_MODIFY_ITEM,                       ///< modify instruction at offset field to given value
	TRDCT_MODIFY_DUAL_ITEM,                  ///< modify second item of dual-part instruction at offset field to given value
	TRDCT_REMOVE_ITEM,                       ///< remove instruction at offset field
	TRDCT_SHALLOW_REMOVE_ITEM,               ///< shallow remove instruction at offset field, does not delete contents of block
	TRDCT_MOVE_ITEM,                         ///< move instruction or block at offset field
	TRDCT_DUPLICATE_ITEM,                    ///< duplicate instruction/block at offset field
	TRDCT_SET_TEXT,                          ///< set text for label instruction
};

const char *GetTraceRestrictDoCommandTypeName(TraceRestrictDoCommandType type);

enum TraceRestrictMgmtDoCommandType : uint8_t {
	TRMDCT_PROG_COPY,                        ///< copy program operation
	TRMDCT_PROG_COPY_APPEND,                 ///< copy and append program operation
	TRMDCT_PROG_SHARE,                       ///< share program operation
	TRMDCT_PROG_SHARE_IF_UNMAPPED,           ///< share program operation (if unmapped)
	TRMDCT_PROG_UNSHARE,                     ///< unshare program (copy as a new program)
	TRMDCT_PROG_RESET,                       ///< reset program state of signal
};

const char *GetTraceRestrictMgmtDoCommandTypeName(TraceRestrictMgmtDoCommandType type);

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
	void SanitiseStrings(StringValidationSettings settings) override;
	void FormatDebugSummary(struct format_target &) const override;
};

struct TraceRestrictCreateCounterCmdData final : public CommandPayloadSerialisable<TraceRestrictCreateCounterCmdData> {
	std::string name;
	std::optional<TraceRestrictFollowUpCmdData> follow_up_cmd;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void SanitiseStrings(StringValidationSettings settings) override;
	void FormatDebugSummary(struct format_target &) const override;
};

struct TraceRestrictProgramSignalInnerData {
	Track track;
	TraceRestrictDoCommandType type;
	uint32_t offset;
	uint32_t data;
	std::string name;

	/* This must include all fields */
	auto GetRefTuple() { return std::tie(this->track, this->type, this->offset, this->data, this->name); }
};
struct TraceRestrictProgramSignalData final : public TupleRefCmdData<TraceRestrictProgramSignalData, TraceRestrictProgramSignalInnerData> {
	void FormatDebugSummary(struct format_target &) const override;
};

struct TraceRestrictManageSignalInnerData {
	Track track;
	TraceRestrictMgmtDoCommandType type;
	TileIndex source_tile;
	Track source_track;

	/* This must include all fields */
	auto GetRefTuple() { return std::tie(this->track, this->type, this->source_tile, this->source_track); }
};
struct TraceRestrictManageSignalData final : public TupleRefCmdData<TraceRestrictManageSignalData, TraceRestrictManageSignalInnerData> {
	void FormatDebugSummary(struct format_target &) const override;
};

BaseCommandContainer<CMD_PROGRAM_TRACERESTRICT_SIGNAL> GetTraceRestrictCommandContainer(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32_t offset, uint32_t value);

DEF_CMD_TUPLE    (CMD_PROGRAM_TRACERESTRICT_SIGNAL,      CmdProgramSignalTraceRestrict,     {}, CMDT_OTHER_MANAGEMENT, TraceRestrictProgramSignalData)
DEF_CMD_TUPLE    (CMD_MANAGE_TRACERESTRICT_SIGNAL,       CmdProgramSignalTraceRestrictMgmt, {}, CMDT_OTHER_MANAGEMENT, TraceRestrictManageSignalData)
DEF_CMD_DIRECT_NT(CMD_CREATE_TRACERESTRICT_SLOT,         CmdCreateTraceRestrictSlot,        {}, CMDT_OTHER_MANAGEMENT, TraceRestrictCreateSlotCmdData)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_SLOT,          CmdAlterTraceRestrictSlot,         {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotID, TraceRestrictAlterSlotOperation, uint32_t, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_SLOT,         CmdDeleteTraceRestrictSlot,        {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotID>)
DEF_CMD_TUPLE_NT (CMD_ADD_VEHICLE_TRACERESTRICT_SLOT,    CmdAddVehicleTraceRestrictSlot,    {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT, CmdRemoveVehicleTraceRestrictSlot, {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_CREATE_TRACERESTRICT_SLOT_GROUP,   CmdCreateTraceRestrictSlotGroup,   {}, CMDT_OTHER_MANAGEMENT, CmdDataT<VehicleType, TraceRestrictSlotGroupID, std::string>)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_SLOT_GROUP,    CmdAlterTraceRestrictSlotGroup,    {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotGroupID, TraceRestrictAlterSlotGroupOperation, TraceRestrictSlotGroupID, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_SLOT_GROUP,   CmdDeleteTraceRestrictSlotGroup,   {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictSlotGroupID>)
DEF_CMD_DIRECT_NT(CMD_CREATE_TRACERESTRICT_COUNTER,      CmdCreateTraceRestrictCounter,     {}, CMDT_OTHER_MANAGEMENT, TraceRestrictCreateCounterCmdData)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_COUNTER,       CmdAlterTraceRestrictCounter,      {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictCounterID, TraceRestrictAlterCounterOperation, uint32_t, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_COUNTER,      CmdDeleteTraceRestrictCounter,     {}, CMDT_OTHER_MANAGEMENT, CmdDataT<TraceRestrictCounterID>)

#endif /* TRACERESTRICT_CMD_H */
