/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file autoreplace_cmd.h Command definitions related to autoreplace. */

#ifndef AUTOREPLACE_CMD_H
#define AUTOREPLACE_CMD_H

#include "command_type.h"
#include "vehicle_type.h"
#include "engine_type.h"
#include "group_type.h"

DEF_CMD_TUPLE_NT(CMD_AUTOREPLACE_VEHICLE, CmdAutoreplaceVehicle, {}, CommandType::VehicleManagement, CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_SET_AUTOREPLACE,     CmdSetAutoReplace,     {}, CommandType::VehicleManagement, CmdDataT<GroupID, EngineID, EngineID, bool>)

#endif /* AUTOREPLACE_CMD_H */
