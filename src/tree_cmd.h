/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tree_cmd.h Command definitions related to tree tiles. */

#ifndef TREE_CMD_H
#define TREE_CMD_H

#include "command_type.h"

DEF_CMD_TUPLE(CMD_PLANT_TREE, CmdPlantTree, CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, uint8_t, bool>)

#endif /* TREE_CMD_H */
