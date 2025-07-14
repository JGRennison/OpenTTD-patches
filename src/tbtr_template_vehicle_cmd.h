/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_vehicle_cmd.h Command definitions related to template based train replacement. */

#ifndef TBTR_TEMPLATE_VEHICLE_CMD_H
#define TBTR_TEMPLATE_VEHICLE_CMD_H

#include "command_type.h"
#include "group_type.h"
#include "tbtr_template_vehicle_type.h"
#include "train_cmd.h"
#include "vehicle_cmd.h"
#include "vehicle_type.h"

enum class TemplateReplacementFlag : uint8_t {
	ReuseDepotVehicles,
	KeepRemaining,
	RefitAsTemplate,
	ReplaceOldOnly,
};

DEF_CMD_TUPLE_NT (CMD_CHANGE_TEMPLATE_FLAG,        CmdChangeFlagTemplateReplace,                          {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<TemplateID, TemplateReplacementFlag, bool>)
DEF_CMD_TUPLE_NT (CMD_RENAME_TEMPLATE,             CmdRenameTemplateReplace,                              {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<TemplateID, std::string>)
DEF_CMD_TUPLE_NT (CMD_VIRTUAL_TRAIN_FROM_TEMPLATE, CmdVirtualTrainFromTemplate,  CMD_CLIENT_ID | CMD_NO_TEST, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<TemplateID, ClientID>)
DEF_CMD_TUPLE_NT (CMD_VIRTUAL_TRAIN_FROM_TRAIN,    CmdVirtualTrainFromTrain,     CMD_CLIENT_ID | CMD_NO_TEST, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, ClientID>)
DEF_CMD_TUPLE_NT (CMD_DELETE_VIRTUAL_TRAIN,        CmdDeleteVirtualTrain,                                 {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID>)
DEF_CMD_TUPLE_NT (CMD_BUILD_VIRTUAL_RAIL_VEHICLE,  CmdBuildVirtualRailVehicle,   CMD_CLIENT_ID | CMD_NO_TEST, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<EngineID, CargoType, ClientID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_REPLACE_TEMPLATE,            CmdReplaceTemplateVehicle,                             {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<TemplateID, VehicleID>)
DEF_CMD_TUPLE_NT (CMD_MOVE_VIRTUAL_RAIL_VEHICLE,   CmdMoveVirtualRailVehicle,                             {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, VehicleID, MoveRailVehicleFlags>)
DEF_CMD_TUPLE_NT (CMD_SELL_VIRTUAL_VEHICLE,        CmdSellVirtualVehicle,                      CMD_CLIENT_ID, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, SellVehicleFlags, ClientID>)
DEF_CMD_TUPLE_NT (CMD_CLONE_TEMPLATE_FROM_TRAIN,   CmdTemplateVehicleFromTrain,                           {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TEMPLATE_VEHICLE,     CmdDeleteTemplateVehicle,                              {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<TemplateID>)
DEF_CMD_TUPLE_NT (CMD_ISSUE_TEMPLATE_REPLACEMENT,  CmdIssueTemplateReplacement,                           {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<GroupID, TemplateID>)
DEF_CMD_TUPLE_NT (CMD_DELETE_TEMPLATE_REPLACEMENT, CmdDeleteTemplateReplacement,                          {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<GroupID>)
DEF_CMD_TUPLE    (CMD_CLONE_VEHICLE_FROM_TEMPLATE, CmdCloneVehicleFromTemplate,                  CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<TemplateID>) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_TUPLE_NT (CMD_TEMPLATE_REPLACE_VEHICLE,    CmdTemplateReplaceVehicle,                    CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<VehicleID>)


#endif /* TBTR_TEMPLATE_VEHICLE_CMD_H */
