/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_cmd.h Command definitions related to settings. */

#ifndef SETTINGS_CMD_H
#define SETTINGS_CMD_H

#include "cheat_type.h"
#include "command_type.h"

/* Enable string logging for setting commands */
struct ChangeSettingCmdData final : public AutoFmtTupleCmdData<ChangeSettingCmdData, TCDF_STRINGS, std::string, int32_t> {};

DEF_CMD_TUPLE_NT(CMD_CHANGE_SETTING,         CmdChangeSetting,        CMD_SERVER, CMDT_SERVER_SETTING,  ChangeSettingCmdData)
DEF_CMD_TUPLE_NT(CMD_CHANGE_COMPANY_SETTING, CmdChangeCompanySetting,         {}, CMDT_COMPANY_SETTING, ChangeSettingCmdData)
DEF_CMD_TUPLE_NT(CMD_CHEAT_SETTING,          CmdCheatSetting,         CMD_SERVER, CMDT_CHEAT,           CmdDataT<CheatNumbers, uint32_t>)

#endif /* SETTINGS_CMD_H */
