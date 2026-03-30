/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file league_cmd.h Command definitions related to league tables. */

#ifndef LEAGUE_CMD_H
#define LEAGUE_CMD_H

#include "command_type.h"
#include "league_type.h"

struct LeagueTableElementCmdData final : public AutoFmtTupleCmdData<LeagueTableElementCmdData, TCDF_NONE, LeagueTableID, int64_t, CompanyID, EncodedString, EncodedString, LinkType, LinkTargetID> {
	static inline constexpr const char fmt_str[] = "t: {}, r: {}, c: {}, type: {}, targ: {}";
};

DEF_CMD_TUPLE_NT(Commands::CreateLeagueTable,              CmdCreateLeagueTable,             CMD_STR_CTRL | CMD_DEITY, CommandType::OtherManagement, CmdDataT<EncodedString, EncodedString, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::CreateLeagueTableElement,       CmdCreateLeagueTableElement,      CMD_STR_CTRL | CMD_DEITY, CommandType::OtherManagement, LeagueTableElementCmdData)
DEF_CMD_TUPLE_NT(Commands::UpdateLeagueTableElementData,   CmdUpdateLeagueTableElementData,  CMD_STR_CTRL | CMD_DEITY, CommandType::OtherManagement, CmdDataT<LeagueTableElementID, CompanyID, EncodedString, LinkType, LinkTargetID>)
DEF_CMD_TUPLE_NT(Commands::UpdateLeagueTableElementScore,  CmdUpdateLeagueTableElementScore, CMD_STR_CTRL | CMD_DEITY, CommandType::OtherManagement, CmdDataT<LeagueTableElementID, int64_t, EncodedString>)
DEF_CMD_TUPLE_NT(Commands::RemoveLeagueTableElement,       CmdRemoveLeagueTableElement,                     CMD_DEITY, CommandType::OtherManagement, CmdDataT<LeagueTableElementID>)

#endif /* LEAGUE_CMD_H */
