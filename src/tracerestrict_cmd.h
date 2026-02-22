/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_cmd.h Header file for Trace Restrict commands */

#ifndef TRACERESTRICT_CMD_H
#define TRACERESTRICT_CMD_H

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
	static constexpr bool HasStringSanitiser = false;

	DynBaseCommandContainer cmd;

	TraceRestrictFollowUpCmdData() = default;
	TraceRestrictFollowUpCmdData(DynBaseCommandContainer cmd) : cmd(std::move(cmd)) {}

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	CommandCost ExecuteWithValue(uint16_t value, DoCommandFlags flags) const;
	void FormatDebugSummary(struct format_target &) const;
};

struct TraceRestrictCreateSlotCmdData final : public CommandPayloadSerialisable<TraceRestrictCreateSlotCmdData> {
	VehicleType vehtype = VEH_INVALID;
	TraceRestrictSlotGroupID parent = INVALID_TRACE_RESTRICT_SLOT_GROUP;
	std::string name;
	uint32_t max_occupancy;
	std::optional<TraceRestrictFollowUpCmdData> follow_up_cmd;

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void SanitisePayloadStrings(StringValidationSettings settings);
	void FormatDebugSummary(struct format_target &) const;
};

struct TraceRestrictCreateCounterCmdData final : public CommandPayloadSerialisable<TraceRestrictCreateCounterCmdData> {
	std::string name;
	std::optional<TraceRestrictFollowUpCmdData> follow_up_cmd;

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void SanitisePayloadStrings(StringValidationSettings settings);
	void FormatDebugSummary(struct format_target &) const;
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
	void FormatDebugSummary(struct format_target &) const;
};

/* Flag values for TraceRestrictProgramSignalData::data for TRDCT_MOVE_ITEM operations */
enum class TraceRestrictProgramSignalMoveFlag : uint8_t {
	Up      = 0, ///< Move up if flag set, otherwise down.
	Shallow = 1, ///< Shallow mode.
};
using TraceRestrictProgramSignalMoveFlags = EnumBitSet<TraceRestrictProgramSignalMoveFlag, uint32_t>;

struct TraceRestrictManageSignalInnerData {
	Track track;
	TraceRestrictMgmtDoCommandType type;
	TileIndex source_tile;
	Track source_track;

	/* This must include all fields */
	auto GetRefTuple() { return std::tie(this->track, this->type, this->source_tile, this->source_track); }
};
struct TraceRestrictManageSignalData final : public TupleRefCmdData<TraceRestrictManageSignalData, TraceRestrictManageSignalInnerData> {
	void FormatDebugSummary(struct format_target &) const;
};

BaseCommandContainer<CMD_PROGRAM_TRACERESTRICT_SIGNAL> GetTraceRestrictCommandContainer(TileIndex tile, Track track, TraceRestrictDoCommandType type, uint32_t offset, uint32_t value);

DEF_CMD_TUPLE    (CMD_PROGRAM_TRACERESTRICT_SIGNAL,      CmdProgramSignalTraceRestrict,     {}, CommandType::OtherManagement, TraceRestrictProgramSignalData)
DEF_CMD_TUPLE    (CMD_MANAGE_TRACERESTRICT_SIGNAL,       CmdProgramSignalTraceRestrictMgmt, {}, CommandType::OtherManagement, TraceRestrictManageSignalData)
DEF_CMD_DIRECT_NT(CMD_CREATE_TRACERESTRICT_SLOT,         CmdCreateTraceRestrictSlot,        {}, CommandType::OtherManagement, TraceRestrictCreateSlotCmdData)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_SLOT,          CmdAlterTraceRestrictSlot,         {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotID, TraceRestrictAlterSlotOperation, uint32_t, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_SLOT,         CmdDeleteTraceRestrictSlot,        {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotID>)
DEF_CMD_TUPLE_NT (CMD_ADD_VEHICLE_TRACERESTRICT_SLOT,    CmdAddVehicleTraceRestrictSlot,    {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_REMOVE_VEHICLE_TRACERESTRICT_SLOT, CmdRemoveVehicleTraceRestrictSlot, {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_CREATE_TRACERESTRICT_SLOT_GROUP,   CmdCreateTraceRestrictSlotGroup,   {}, CommandType::OtherManagement, CmdDataT<VehicleType, TraceRestrictSlotGroupID, std::string>)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_SLOT_GROUP,    CmdAlterTraceRestrictSlotGroup,    {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotGroupID, TraceRestrictAlterSlotGroupOperation, TraceRestrictSlotGroupID, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_SLOT_GROUP,   CmdDeleteTraceRestrictSlotGroup,   {}, CommandType::OtherManagement, CmdDataT<TraceRestrictSlotGroupID>)
DEF_CMD_DIRECT_NT(CMD_CREATE_TRACERESTRICT_COUNTER,      CmdCreateTraceRestrictCounter,     {}, CommandType::OtherManagement, TraceRestrictCreateCounterCmdData)
DEF_CMD_TUPLE_NT (CMD_ALTER_TRACERESTRICT_COUNTER,       CmdAlterTraceRestrictCounter,      {}, CommandType::OtherManagement, CmdDataT<TraceRestrictCounterID, TraceRestrictAlterCounterOperation, uint32_t, std::string>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TRACERESTRICT_COUNTER,      CmdDeleteTraceRestrictCounter,     {}, CommandType::OtherManagement, CmdDataT<TraceRestrictCounterID>)

#endif /* TRACERESTRICT_CMD_H */
