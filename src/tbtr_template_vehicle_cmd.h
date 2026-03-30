/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tbtr_template_vehicle_cmd.h Command definitions related to template based train replacement. */

#ifndef TBTR_TEMPLATE_VEHICLE_CMD_H
#define TBTR_TEMPLATE_VEHICLE_CMD_H

#include "cargo_type.h"
#include "command_type.h"
#include "engine_type.h"
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

DEF_CMD_TUPLE_NT (Commands::ChangeTemplateFlag,        CmdChangeFlagTemplateReplace,                          {}, CommandType::VehicleManagement,   CmdDataT<TemplateID, TemplateReplacementFlag, bool>)
DEF_CMD_TUPLE_NT (Commands::RenameTemplate,            CmdRenameTemplateReplace,                              {}, CommandType::VehicleManagement,   CmdDataT<TemplateID, std::string>)
DEF_CMD_TUPLE_NT (Commands::VirtualTrainFromTemplate,  CmdVirtualTrainFromTemplate,  CMD_CLIENT_ID | CMD_NO_TEST, CommandType::VehicleManagement,   CmdDataT<TemplateID, ClientID>)
DEF_CMD_TUPLE_NT (Commands::VirtualTrainFromTrain,     CmdVirtualTrainFromTrain,     CMD_CLIENT_ID | CMD_NO_TEST, CommandType::VehicleManagement,   CmdDataT<VehicleID, ClientID>)
DEF_CMD_TUPLE_NT (Commands::DeleteVirtualTrain,        CmdDeleteVirtualTrain,                                 {}, CommandType::VehicleManagement,   CmdDataT<VehicleID>)
DEF_CMD_TUPLE_NT (Commands::BuildVirtualRailVehicle,   CmdBuildVirtualRailVehicle,   CMD_CLIENT_ID | CMD_NO_TEST, CommandType::VehicleManagement,   CmdDataT<EngineID, CargoType, ClientID, VehicleID>)
DEF_CMD_TUPLE_NT (Commands::ReplaceTemplate,           CmdReplaceTemplateVehicle,                             {}, CommandType::VehicleManagement,   CmdDataT<TemplateID, VehicleID>)
DEF_CMD_TUPLE_NT (Commands::MoveVirtualRailVehicle,    CmdMoveVirtualRailVehicle,                             {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, VehicleID, MoveRailVehicleFlags>)
DEF_CMD_TUPLE_NT (Commands::SellVirtualVehicle,        CmdSellVirtualVehicle,                      CMD_CLIENT_ID, CommandType::VehicleManagement,   CmdDataT<VehicleID, SellVehicleFlags, ClientID>)
DEF_CMD_TUPLE_NT (Commands::CloneTemplateFromTrain,    CmdTemplateVehicleFromTrain,                           {}, CommandType::VehicleManagement,   CmdDataT<VehicleID>)
DEF_CMD_TUPLE_NT (Commands::DeleteTemplateVehicle,     CmdDeleteTemplateVehicle,                              {}, CommandType::VehicleManagement,   CmdDataT<TemplateID>)
DEF_CMD_TUPLE_NT (Commands::IssueTemplateReplacement,  CmdIssueTemplateReplacement,                           {}, CommandType::VehicleManagement,   CmdDataT<GroupID, TemplateID>)
DEF_CMD_TUPLE_NT (Commands::DeleteTemplateReplacement, CmdDeleteTemplateReplacement,                          {}, CommandType::VehicleManagement,   CmdDataT<GroupID>)
DEF_CMD_TUPLE    (Commands::CloneVehicleFromTemplate,  CmdCloneVehicleFromTemplate,                  CMD_NO_TEST, CommandType::VehicleConstruction, CmdDataT<TemplateID>) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_TUPLE_NT (Commands::TemplateReplaceVehicle,    CmdTemplateReplaceVehicle,                    CMD_NO_TEST, CommandType::VehicleConstruction, CmdDataT<VehicleID>)


#endif /* TBTR_TEMPLATE_VEHICLE_CMD_H */
