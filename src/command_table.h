/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_table.h Command table types. */

#ifndef COMMAND_TABLE_H
#define COMMAND_TABLE_H

#include "command_type.h"
#include "core/enum_type.hpp"

#include <array>
#include <typeinfo>

using CommandExecTrampoline = CommandCost(const CommandExecData &);

enum CommandIntlFlags : uint8_t {
	CIF_NONE                = 0x0, ///< no flag is set
	CIF_NO_OUTPUT_TILE      = 0x1, ///< command does not take a tile at the output side (omit when logging)
};
DECLARE_ENUM_AS_BIT_SET(CommandIntlFlags)

struct CommandInfo {
	CommandExecTrampoline *exec;                      ///< Command proc exec trampoline function
	CommandPayloadDeserialiser *payload_deserialiser; ///< Command payload deserialiser
	const CommandPayloadBase::Operations &operations; ///< Command payload operations
	const char *name;                                 ///< A human readable name for the procedure
	CommandFlags flags;                               ///< The (command) flags to that apply to this command
	CommandType type;                                 ///< The type of command
	CommandIntlFlags intl_flags;                      ///< Internal flags
};

extern const std::array<CommandInfo, to_underlying(CMD_END)> _command_proc_table;

#endif /* COMMAND_TABLE_H */
