/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_cmd.h Types related to planning. */

#ifndef PLANS_CMD_H
#define PLANS_CMD_H

#include "command_type.h"
#include "gfx_type.h"
#include "plans_type.h"
#include "tile_type.h"

struct PlanLineCmdData final : public CommandPayloadSerialisable<PlanLineCmdData> {
	PlanID plan;
	std::vector<TileIndex> tiles;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const override;
};

DEF_CMD_TUPLE_NT (CMD_ADD_PLAN,               CmdAddPlan,                         {}, CMDT_OTHER_MANAGEMENT, EmptyCmdData)
DEF_CMD_DIRECT_NT(CMD_ADD_PLAN_LINE,          CmdAddPlanLine,            CMD_NO_TEST, CMDT_OTHER_MANAGEMENT, PlanLineCmdData)
DEF_CMD_TUPLE_NT (CMD_REMOVE_PLAN,            CmdRemovePlan,                      {}, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID>)
DEF_CMD_TUPLE_NT (CMD_REMOVE_PLAN_LINE,       CmdRemovePlanLine,                  {}, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID, uint32_t>)
DEF_CMD_TUPLE_NT (CMD_CHANGE_PLAN_VISIBILITY, CmdChangePlanVisibility,            {}, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID, bool>)
DEF_CMD_TUPLE_NT (CMD_CHANGE_PLAN_COLOUR,     CmdChangePlanColour,                {}, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID, Colours>)
DEF_CMD_TUPLE_NT (CMD_RENAME_PLAN,            CmdRenamePlan,                      {}, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID, std::string>)
DEF_CMD_TUPLE_NT (CMD_ACQUIRE_UNOWNED_PLAN,   CmdAcquireUnownedPlan,   CMD_SERVER_NS, CMDT_OTHER_MANAGEMENT, CmdDataT<PlanID>)

#endif /* PLANS_CMD_H */
