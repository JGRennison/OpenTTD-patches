/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_cmd.h Command definitions related to engine groups. */

#ifndef GROUP_CMD_H
#define GROUP_CMD_H

#include "command_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "vehiclelist.h"

enum Colours : uint8_t;
enum class GroupFlag : uint8_t;

/** Action for \c CmdAlterGroup. */
enum class AlterGroupMode : uint8_t {
	Rename,    ///< Change group name.
	SetParent, ///< Change group parent.
};

DEF_CMD_TUPLE_NT(CMD_CREATE_GROUP,              CmdCreateGroup,             {}, CommandType::RouteManagement, CmdDataT<VehicleType, GroupID>)
DEF_CMD_TUPLE_NT(CMD_DELETE_GROUP,              CmdDeleteGroup,             {}, CommandType::RouteManagement, CmdDataT<GroupID>)
DEF_CMD_TUPLE_NT(CMD_ALTER_GROUP,               CmdAlterGroup,              {}, CommandType::OtherManagement, CmdDataT<AlterGroupMode, GroupID, GroupID, std::string>)
DEF_CMD_TUPLE_NT(CMD_ADD_VEHICLE_GROUP,         CmdAddVehicleGroup,         {}, CommandType::RouteManagement, CmdDataT<GroupID, VehicleID, bool>)
DEF_CMD_TUPLE_NT(CMD_ADD_SHARED_VEHICLE_GROUP,  CmdAddSharedVehicleGroup,   {}, CommandType::RouteManagement, CmdDataT<GroupID, VehicleType>)
DEF_CMD_TUPLE_NT(CMD_REMOVE_ALL_VEHICLES_GROUP, CmdRemoveAllVehiclesGroup,  {}, CommandType::RouteManagement, CmdDataT<GroupID>)
DEF_CMD_TUPLE_NT(CMD_SET_GROUP_FLAG,            CmdSetGroupFlag,            {}, CommandType::RouteManagement, CmdDataT<GroupID, GroupFlag, bool, bool>)
DEF_CMD_TUPLE_NT(CMD_SET_GROUP_LIVERY,          CmdSetGroupLivery,          {}, CommandType::RouteManagement, CmdDataT<GroupID, bool, Colours>)
DEF_CMD_TUPLE_NT(CMD_CREATE_GROUP_FROM_LIST,    CmdCreateGroupFromList,     {}, CommandType::OtherManagement, CmdDataT<VehicleListIdentifier, CargoType, std::string>)

#endif /* GROUP_CMD_H */
