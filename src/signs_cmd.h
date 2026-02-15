/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file signs_cmd.h Command definitions related to signs. */

#ifndef SIGNS_CMD_H
#define SIGNS_CMD_H

#include "command_type.h"
#include "signs_type.h"
#include "gfx_type.h"

DEF_CMD_TUPLE   (CMD_PLACE_SIGN,  CmdPlaceSign,  CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement, CmdDataT<std::string>)
DEF_CMD_TUPLE_NT(CMD_RENAME_SIGN, CmdRenameSign, CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement, CmdDataT<SignID, std::string, Colours>)
DEF_CMD_TUPLE_NT(CMD_MOVE_SIGN,   CmdMoveSign,   CMD_LOG_AUX | CMD_DEITY, CommandType::OtherManagement, CmdDataT<SignID, TileIndex>)

#endif /* SIGNS_CMD_H */
