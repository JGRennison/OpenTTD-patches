/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file terraform_cmd.h Command definitions related to terraforming. */

#ifndef TERRAFORM_CMD_H
#define TERRAFORM_CMD_H

#include "command_type.h"
#include "map_type.h"
#include "slope_type.h"

DEF_CMD_TUPLE(CMD_TERRAFORM_LAND, CmdTerraformLand,               CMD_ALL_TILES | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<Slope, bool>)
DEF_CMD_TUPLE(CMD_LEVEL_LAND,     CmdLevelLand,     CMD_ALL_TILES | CMD_NO_TEST | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, bool, LevelMode>) // test run might clear tiles multiple times, in execution that only happens once

#endif /* TERRAFORM_CMD_H */
