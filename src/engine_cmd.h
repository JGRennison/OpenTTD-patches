/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_cmd.h Command definitions related to engines. */

#ifndef ENGINE_CMD_H
#define ENGINE_CMD_H

#include "command_type.h"
#include "company_type.h"
#include "engine_type.h"

DEF_CMD_TUPLE_NT(CMD_WANT_ENGINE_PREVIEW,    CmdWantEnginePreview,            {}, CommandType::VehicleManagement, CmdDataT<EngineID>)
DEF_CMD_TUPLE_NT(CMD_ENGINE_CTRL,            CmdEngineCtrl,            CMD_DEITY, CommandType::VehicleManagement, CmdDataT<EngineID, CompanyID, bool>)
DEF_CMD_TUPLE_NT(CMD_RENAME_ENGINE,          CmdRenameEngine,         CMD_SERVER, CommandType::OtherManagement,   CmdDataT<EngineID, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_VEHICLE_VISIBILITY, CmdSetVehicleVisibility,         {}, CommandType::CompanySetting,    CmdDataT<EngineID, bool>)

#endif /* ENGINE_CMD_H */
