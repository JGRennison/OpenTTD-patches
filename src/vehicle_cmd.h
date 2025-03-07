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
#include "vehicle_type.h"
#include "vehiclelist.h"

enum class SellVehicleFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	SellChain             = (1U << 0), ///< Sell the vehicle and all vehicles following it in the chain.
	BackupOrder           = (1U << 1), ///< Make a backup of the vehicle's order (if an engine).
	VirtualOnly           = (1U << 2), ///< Only allow command to be run on virtual trains
};
DECLARE_ENUM_AS_BIT_SET(SellVehicleFlags)

DEF_CMD_TUPLE    (CMD_BUILD_VEHICLE,              CmdBuildVehicle,              CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<EngineID, bool, CargoID, ClientID>)
DEF_CMD_TUPLE_LT (CMD_SELL_VEHICLE,               CmdSellVehicle,               CMD_CLIENT_ID, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<VehicleID, SellVehicleFlags, ClientID>)
DEF_CMD_TUPLE_LT (CMD_REFIT_VEHICLE,              CmdRefitVehicle,                         {}, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<VehicleID, CargoID, uint8_t, bool, bool, uint8_t>)
DEF_CMD_TUPLE_NT (CMD_SEND_VEHICLE_TO_DEPOT,      CmdSendVehicleToDepot,                   {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, DepotCommand, TileIndex>)
DEF_CMD_TUPLE_NT (CMD_MASS_SEND_VEHICLE_TO_DEPOT, CmdMassSendVehicleToDepot,               {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<DepotCommand, VehicleListIdentifier, CargoID>)
DEF_CMD_TUPLE_NT (CMD_CHANGE_SERVICE_INT,         CmdChangeServiceInt,                     {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, uint16_t, bool, bool>)
DEF_CMD_TUPLE_NT (CMD_RENAME_VEHICLE,             CmdRenameVehicle,                        {}, CMDT_OTHER_MANAGEMENT,     CmdDataT<VehicleID, std::string>)
DEF_CMD_TUPLE    (CMD_CLONE_VEHICLE,              CmdCloneVehicle,                CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<VehicleID, bool>) // NewGRF callbacks influence building and refitting making it impossible to correctly estimate the cost
DEF_CMD_TUPLE_LT (CMD_START_STOP_VEHICLE,         CmdStartStopVehicle,                     {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE    (CMD_MASS_START_STOP,            CmdMassStartStopVehicle,                 {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<bool, bool, VehicleListIdentifier, CargoID>)
DEF_CMD_TUPLE    (CMD_DEPOT_SELL_ALL_VEHICLES,    CmdDepotSellAllVehicles,                 {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleType>)
DEF_CMD_TUPLE    (CMD_DEPOT_MASS_AUTOREPLACE,     CmdDepotMassAutoReplace,        CMD_NO_TEST, CMDT_VEHICLE_CONSTRUCTION, CmdDataT<VehicleType>)

DEF_CMD_TUPLE_LT (CMD_TURN_ROADVEH,               CmdTurnRoadVeh,                          {}, CMDT_VEHICLE_MANAGEMENT,   CmdDataT<VehicleID>)

#endif /* VEHICLE_CMD_H */
