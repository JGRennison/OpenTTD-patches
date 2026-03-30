/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file plans_cmd.h Types related to planning. */

#ifndef PLANS_CMD_H
#define PLANS_CMD_H

#include "command_type.h"
#include "gfx_type.h"
#include "plans_type.h"
#include "tile_type.h"

struct PlanLineCmdData final : public CommandPayloadSerialisable<PlanLineCmdData> {
	static constexpr bool HasStringSanitiser = false;
	PlanID plan;
	std::vector<TileIndex> tiles;

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const;
};

DEF_CMD_TUPLE_NT (Commands::AddPlan,              CmdAddPlan,                         {}, CommandType::OtherManagement, EmptyCmdData)
DEF_CMD_DIRECT_NT(Commands::AddPlanLine,          CmdAddPlanLine,            CMD_NO_TEST, CommandType::OtherManagement, PlanLineCmdData)
DEF_CMD_TUPLE_NT (Commands::RemovePlan,           CmdRemovePlan,                      {}, CommandType::OtherManagement, CmdDataT<PlanID>)
DEF_CMD_TUPLE_NT (Commands::RemovePlanLine,       CmdRemovePlanLine,                  {}, CommandType::OtherManagement, CmdDataT<PlanID, uint32_t>)
DEF_CMD_TUPLE_NT (Commands::ChangePlanVisibility, CmdChangePlanVisibility,            {}, CommandType::OtherManagement, CmdDataT<PlanID, bool>)
DEF_CMD_TUPLE_NT (Commands::ChangePlanColour,     CmdChangePlanColour,                {}, CommandType::OtherManagement, CmdDataT<PlanID, Colours>)
DEF_CMD_TUPLE_NT (Commands::RenamePlan,           CmdRenamePlan,                      {}, CommandType::OtherManagement, CmdDataT<PlanID, std::string>)
DEF_CMD_TUPLE_NT (Commands::AcquireUnownedPlan,   CmdAcquireUnownedPlan,   CMD_SERVER_NS, CommandType::OtherManagement, CmdDataT<PlanID>)

#endif /* PLANS_CMD_H */
