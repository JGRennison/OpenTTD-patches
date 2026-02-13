/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file object_cmd.h Command definitions related to objects. */

#ifndef OBJECT_CMD_H
#define OBJECT_CMD_H

#include "command_type.h"
#include "object_type.h"

DEF_CMD_TUPLE(CMD_BUILD_OBJECT,       CmdBuildObject,        CMD_DEITY | CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<ObjectType, uint8_t>)
DEF_CMD_TUPLE(CMD_BUILD_OBJECT_AREA,  CmdBuildObjectArea,  CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CommandType::LandscapeConstruction, CmdDataT<TileIndex, ObjectType, uint8_t, bool>)
DEF_CMD_TUPLE(CMD_PURCHASE_LAND_AREA, CmdPurchaseLandArea, CMD_NO_WATER | CMD_AUTO | CMD_NO_TEST, CommandType::LandscapeConstruction, CmdDataT<TileIndex, bool>)

#endif /* OBJECT_CMD_H */
