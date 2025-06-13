/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_cmd.h Command definitions related to industries. */

#ifndef INDUSTRY_CMD_H
#define INDUSTRY_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "industry_type.h"
#include "industry.h"

DEF_CMD_TUPLE   (CMD_BUILD_INDUSTRY,           CmdBuildIndustry,                          CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<IndustryType, uint32_t, bool, uint32_t>)
DEF_CMD_TUPLE_NT(CMD_INDUSTRY_SET_FLAGS,       CmdIndustrySetFlags,        CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT,       CmdDataT<IndustryID, IndustryControlFlags>)
DEF_CMD_TUPLE_NT(CMD_INDUSTRY_SET_EXCLUSIVITY, CmdIndustrySetExclusivity,  CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT,       CmdDataT<IndustryID, Owner, bool>)
DEF_CMD_TUPLE_NT(CMD_INDUSTRY_SET_TEXT,        CmdIndustrySetText,         CMD_STR_CTRL | CMD_DEITY, CMDT_OTHER_MANAGEMENT,       CmdDataT<IndustryID, std::string>)
DEF_CMD_TUPLE_NT(CMD_INDUSTRY_SET_PRODUCTION,  CmdIndustrySetProduction,                  CMD_DEITY, CMDT_OTHER_MANAGEMENT,       CmdDataT<IndustryID, uint8_t, bool, std::string>)

#endif /* INDUSTRY_CMD_H */
