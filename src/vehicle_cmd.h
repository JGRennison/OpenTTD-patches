/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_cmd.h Command definitions for vehicles. */

#ifndef VEHICLE_CMD_H
#define VEHICLE_CMD_H

#include "command_type.h"
#include "engine_type.h"
#include "vehicle_type.h"
#include "vehiclelist.h"

enum class SellVehicleFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	SellChain             = (1U << 0), ///< Sell the vehicle and all vehicles following it in the chain.
	BackupOrder           = (1U << 1), ///< Make a backup of the vehicle's order (if an engine).
	VirtualOnly           = (1U << 2), ///< Only allow command to be run on virtual trains
};
DECLARE_ENUM_AS_BIT_SET(SellVehicleFlags)

DEF_CMD_TUPLE    (CMD_BUILD_VEHICLE,              CmdBuildVehicle,              CMD_CLIENT_ID, CommandType::VehicleConstruction, CmdDataT<EngineID, bool, CargoType, ClientID>)
DEF_CMD_TUPLE_LT (CMD_SELL_VEHICLE,               CmdSellVehicle,               CMD_CLIENT_ID, CommandType::VehicleConstruction, CmdDataT<VehicleID, SellVehicleFlags, ClientID>)
DEF_CMD_TUPLE_LT (CMD_REFIT_VEHICLE,              CmdRefitVehicle,                         {}, CommandType::VehicleConstruction, CmdDataT<VehicleID, CargoType, uint8_t, bool, bool, uint8_t>)
DEF_CMD_TUPLE_NT (CMD_SEND_VEHICLE_TO_DEPOT,      CmdSendVehicleToDepot,                   {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, DepotCommandFlags, TileIndex>)
DEF_CMD_TUPLE_NT (CMD_MASS_SEND_VEHICLE_TO_DEPOT, CmdMassSendVehicleToDepot,               {}, CommandType::VehicleManagement,   CmdDataT<DepotCommandFlags, VehicleListIdentifier, CargoType>)
DEF_CMD_TUPLE_NT (CMD_CHANGE_SERVICE_INT,         CmdChangeServiceInt,                     {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, uint16_t, bool, bool>)
DEF_CMD_TUPLE_NT (CMD_RENAME_VEHICLE,             CmdRenameVehicle,                        {}, CommandType::OtherManagement,     CmdDataT<VehicleID, std::string>)
DEF_CMD_TUPLE    (CMD_CLONE_VEHICLE,              CmdCloneVehicle,                CMD_NO_TEST, CommandType::VehicleConstruction, CmdDataT<VehicleID, bool>) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_TUPLE_LT (CMD_START_STOP_VEHICLE,         CmdStartStopVehicle,                     {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE    (CMD_MASS_START_STOP,            CmdMassStartStopVehicle,                 {}, CommandType::VehicleManagement,   CmdDataT<bool, bool, VehicleListIdentifier, CargoType>)
DEF_CMD_TUPLE    (CMD_DEPOT_SELL_ALL_VEHICLES,    CmdDepotSellAllVehicles,                 {}, CommandType::VehicleManagement,   CmdDataT<VehicleType>)
DEF_CMD_TUPLE    (CMD_DEPOT_MASS_AUTOREPLACE,     CmdDepotMassAutoReplace,        CMD_NO_TEST, CommandType::VehicleConstruction, CmdDataT<VehicleType>)

DEF_CMD_TUPLE_LT (CMD_TURN_ROADVEH,               CmdTurnRoadVeh,                          {}, CommandType::VehicleManagement,   CmdDataT<VehicleID>)

#endif /* VEHICLE_CMD_H */
