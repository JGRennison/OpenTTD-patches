/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file subsidy_cmd.h Command definitions related to subsidies. */

#ifndef SUBSIDY_CMD_H
#define SUBSIDY_CMD_H

#include "command_type.h"
#include "cargo_type.h"
#include "subsidy_base.h"

DEF_CMD_TUPLE_NT(CMD_CREATE_SUBSIDY, CmdCreateSubsidy, CMD_DEITY, CMDT_OTHER_MANAGEMENT, CmdDataT<CargoType, Source, Source>)

#endif /* SUBSIDY_CMD_H */
