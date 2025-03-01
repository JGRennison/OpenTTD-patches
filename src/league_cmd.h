/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file league_cmd.h Command definitions related to league tables. */

#ifndef LEAGUE_CMD_H
#define LEAGUE_CMD_H

#include "command_type.h"
#include "league_type.h"

struct LeagueTableElementCmdData final : public AutoFmtTupleCmdData<LeagueTableElementCmdData, TCDF_NONE, LeagueTableID, int64_t, CompanyID, std::string, std::string, LinkType, LinkTargetID> {
	static inline constexpr const char fmt_str[] = "t: {}, r: {}, c: {}, type: {}, targ: {}";
};

using LeagueTableCmdData = CmdDataT<std::string, std::string, std::string>;
using LeagueTableElementDataCmdData = CmdDataT<LeagueTableElementID, CompanyID, std::string, LinkType, LinkTargetID>;
using LeagueTableElementScoreCmdData = CmdDataT<LeagueTableElementID, int64_t, std::string>;

DEF_CMD_TUPLE_NT(CMD_CREATE_LEAGUE_TABLE,               CmdCreateLeagueTable,             LeagueTableCmdData,              CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_CREATE_LEAGUE_TABLE_ELEMENT,       CmdCreateLeagueTableElement,      LeagueTableElementCmdData,       CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_UPDATE_LEAGUE_TABLE_ELEMENT_DATA,  CmdUpdateLeagueTableElementData,  LeagueTableElementDataCmdData,   CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_UPDATE_LEAGUE_TABLE_ELEMENT_SCORE, CmdUpdateLeagueTableElementScore, LeagueTableElementScoreCmdData,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT)
DEF_CMD_TUPLE_NT(CMD_REMOVE_LEAGUE_TABLE_ELEMENT,       CmdRemoveLeagueTableElement,      CmdDataT<LeagueTableElementID>,                 CMD_DEITY, CMDT_OTHER_MANAGEMENT)

#endif /* LEAGUE_CMD_H */
