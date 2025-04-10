/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file viewport_cmd.h Command definitions related to viewports. */

#ifndef VIEWPORT_CMD_H
#define VIEWPORT_CMD_H

#include "command_type.h"
#include "viewport_type.h"

DEF_CMD_TUPLE(CMD_SCROLL_VIEWPORT, CmdScrollViewport, CMD_DEITY | CMD_LOG_AUX, CMDT_OTHER_MANAGEMENT, CmdDataT<ViewportScrollTarget, uint32_t>)

#endif /* VIEWPORT_CMD_H */



