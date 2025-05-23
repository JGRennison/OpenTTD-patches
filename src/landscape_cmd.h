/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file landscape_cmd.h Command definitions related to landscape (slopes etc.). */

#ifndef LANDSCAPE_CMD_H
#define LANDSCAPE_CMD_H

#include "command_type.h"
#include "tile_type.h"

DEF_CMD_TUPLE(CMD_LANDSCAPE_CLEAR, CmdLandscapeClear,   CMD_DEITY, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<>)
DEF_CMD_TUPLE(CMD_CLEAR_AREA,      CmdClearArea,      CMD_NO_TEST, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, bool>) // destroying multi-tile houses makes town rating differ between test and execution

#endif /* LANDSCAPE_CMD_H */
