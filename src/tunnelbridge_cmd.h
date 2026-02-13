/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnelbridge_cmd.h Command definitions related to tunnels and bridges. */

#ifndef TUNNELBRIDGE_CMD_H
#define TUNNELBRIDGE_CMD_H

#include "command_type.h"
#include "transport_type.h"
#include "bridge.h"

enum class BuildBridgeFlags : uint8_t {
	None                  = 0,         ///< No flag set.
	ScriptCommand         = (1U << 0), ///< This is a script command, disable functionality inappropriate for scripts.
};
DECLARE_ENUM_AS_BIT_SET(BuildBridgeFlags)

DEF_CMD_TUPLE(CMD_BUILD_BRIDGE, CmdBuildBridge, CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<TileIndex, TransportType, BridgeType, uint8_t, BuildBridgeFlags>)
DEF_CMD_TUPLE(CMD_BUILD_TUNNEL, CmdBuildTunnel,                CMD_DEITY | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<TransportType, uint8_t>)

#endif /* TUNNELBRIDGE_CMD_H */
