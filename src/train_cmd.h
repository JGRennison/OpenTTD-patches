/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_cmd.h Command definitions related to trains. */

#ifndef TRAIN_CMD_H
#define TRAIN_CMD_H

#include "command_type.h"
#include "vehicle_type.h"

enum class MoveRailVehicleFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	MoveChain             = (1U << 0), ///< Move all vehicles following the source vehicle
	Virtual               = (1U << 1), ///< This is a virtual vehicle (for creating TemplateVehicles)
	NewHead               = (1U << 2), ///< When moving a head vehicle, always reset the head state
};
DECLARE_ENUM_AS_BIT_SET(MoveRailVehicleFlags)

DEF_CMD_TUPLE_LT (CMD_MOVE_RAIL_VEHICLE,           CmdMoveRailVehicle,           {}, CommandType::VehicleConstruction, CmdDataT<VehicleID, VehicleID, MoveRailVehicleFlags>)
DEF_CMD_TUPLE_LT (CMD_FORCE_TRAIN_PROCEED,         CmdForceTrainProceed,         {}, CommandType::VehicleManagement,   CmdDataT<VehicleID>)
DEF_CMD_TUPLE_LT (CMD_REVERSE_TRAIN_DIRECTION,     CmdReverseTrainDirection,     {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, bool>)
DEF_CMD_TUPLE_LT (CMD_SET_TRAIN_SPEED_RESTRICTION, CmdSetTrainSpeedRestriction,  {}, CommandType::VehicleManagement,   CmdDataT<VehicleID, uint16_t>)

#endif /* TRAIN_CMD_H */
