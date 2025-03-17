/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_func.h Functions related to commands. */

#ifndef COMMAND_FUNC_H
#define COMMAND_FUNC_H

#include "command_type.h"
#include "company_type.h"

/* DoCommand and variants */

CommandCost DoCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, DoCommandFlag flags, DoCommandIntlFlag intl_flags);

/* Note that output_no_tile is used here instead of input_no_tile, because a tile index used only for error messages is not useful */
template <Commands cmd, typename = typename std::enable_if<!CommandTraits<cmd>::output_no_tile>>
CommandCost DoCommand(TileIndex tile, const CmdPayload<cmd> &payload, DoCommandFlag flags, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandImplementation(cmd, tile, payload, flags, intl_flags | DCIF_TYPE_CHECKED);
}

template <Commands cmd, typename = typename std::enable_if<CommandTraits<cmd>::output_no_tile>>
CommandCost DoCommand(const CmdPayload<cmd> &payload, DoCommandFlag flags, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandImplementation(cmd, TileIndex{0}, payload, flags, intl_flags | DCIF_TYPE_CHECKED);
}

inline CommandCost DoCommandContainer(const DynBaseCommandContainer &container, DoCommandFlag flags)
{
	return DoCommandImplementation(container.cmd, container.tile, *container.payload, flags, DCIF_NONE);
}

template <Commands cmd>
inline CommandCost DoCommandContainer(const BaseCommandContainer<cmd> &container, DoCommandFlag flags)
{
	return DoCommandImplementation(cmd, container.tile, container.payload, flags, DCIF_TYPE_CHECKED);
}

/* DoCommandP and variants */

bool DoCommandPImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags);

inline bool DoCommandPContainer(const DynCommandContainer &container, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(container.command.cmd, container.command.tile, *container.command.payload, container.command.error_msg, container.callback, container.callback_param, intl_flags);
}

template <Commands cmd>
inline bool DoCommandPContainer(const CommandContainer<cmd> &container, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(cmd, container.tile, container.payload, container.error_msg, container.callback, container.callback_param, intl_flags | DCIF_TYPE_CHECKED);
}

template <Commands cmd, typename = typename std::enable_if<!CommandTraits<cmd>::input_no_tile>>
bool DoCommandP(TileIndex tile, const CmdPayload<cmd> &payload, StringID error_msg, CommandCallback callback = CommandCallback::None, CallbackParameter callback_param = 0, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(cmd, tile, payload, error_msg, callback, callback_param, intl_flags | DCIF_TYPE_CHECKED);
}

template <Commands cmd, typename = typename std::enable_if<CommandTraits<cmd>::input_no_tile>>
bool DoCommandP(const CmdPayload<cmd> &payload, StringID error_msg, CommandCallback callback = CommandCallback::None, CallbackParameter callback_param = 0, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(cmd, TileIndex{0}, payload, error_msg, callback, callback_param, intl_flags | DCIF_TYPE_CHECKED);
}

template <Commands TCmd, typename T> struct DoCommandHelper;
template <Commands TCmd, typename T> struct DoCommandHelperNoTile;

template <Commands Tcmd, typename... Targs>
struct DoCommandHelper<Tcmd, std::tuple<Targs...>> {
	static inline CommandCost Do(DoCommandFlag flags, TileIndex tile, Targs... args)
	{
		return DoCommand<Tcmd>(tile, CmdPayload<Tcmd>::Make(std::forward<Targs>(args)...), flags);
	}
};

template <Commands Tcmd, typename... Targs>
struct DoCommandHelperNoTile<Tcmd, std::tuple<Targs...>> {
	static inline CommandCost Do(DoCommandFlag flags, Targs... args)
	{
		return DoCommand<Tcmd>(CmdPayload<Tcmd>::Make(std::forward<Targs>(args)...), flags);
	}
};

template <Commands TCmd, typename T> struct DoCommandPHelper;
template <Commands TCmd, typename T> struct DoCommandPHelperNoTile;

template <Commands Tcmd, typename... Targs>
struct DoCommandPHelper<Tcmd, std::tuple<Targs...>> {
	using PayloadType = CmdPayload<Tcmd>;

	static inline bool Post(TileIndex tile, Targs... args)
	{
		return DoCommandP<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, CommandCallback::None);
	}

	static inline bool Post(StringID error_msg, TileIndex tile, Targs... args)
	{
		return DoCommandP<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), error_msg, CommandCallback::None);
	}

	static inline bool Post(CommandCallback callback, TileIndex tile, Targs... args)
	{
		return DoCommandP<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, callback);
	}

	static inline bool Post(StringID error_msg, CommandCallback callback, TileIndex tile, Targs... args)
	{
		return DoCommandP<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), error_msg, callback);
	}
};

template <Commands Tcmd, typename... Targs>
struct DoCommandPHelperNoTile<Tcmd, std::tuple<Targs...>> {
	using PayloadType = CmdPayload<Tcmd>;

	static inline bool Post(Targs... args)
	{
		return DoCommandP<Tcmd>(PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, CommandCallback::None);
	}

	static inline bool Post(StringID error_msg, Targs... args)
	{
		return DoCommandP<Tcmd>(PayloadType::Make(std::forward<Targs>(args)...), error_msg, CommandCallback::None);
	}

	static inline bool Post(CommandCallback callback, Targs... args)
	{
		return DoCommandP<Tcmd>(PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, callback);
	}

	static inline bool Post(StringID error_msg, CommandCallback callback, Targs... args)
	{
		return DoCommandP<Tcmd>(PayloadType::Make(std::forward<Targs>(args)...), error_msg, callback);
	}
};

template <Commands Tcmd>
struct Command :
		public std::conditional_t<CommandTraits<Tcmd>::output_no_tile,
			DoCommandHelperNoTile<Tcmd, typename CmdPayload<Tcmd>::Tuple>,
			DoCommandHelper<Tcmd, typename CmdPayload<Tcmd>::Tuple>>,
		public std::conditional_t<CommandTraits<Tcmd>::input_no_tile,
			DoCommandPHelperNoTile<Tcmd, typename CmdPayload<Tcmd>::Tuple>,
			DoCommandPHelper<Tcmd, typename CmdPayload<Tcmd>::Tuple>> {};

/* Other command functions */

CommandCost DoCommandPScript(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only, bool asynchronous);
CommandCost DoCommandPInternal(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only);

template <Commands Tcmd>
void NetworkSendCommand(TileIndex tile, const CmdPayload<Tcmd> &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company)
{
	extern void NetworkSendCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company);
	return NetworkSendCommandImplementation(Tcmd, tile, payload, error_msg, callback, callback_param, company);
}

inline bool IsValidCommand(Commands cmd) { return cmd < CMD_END; }
CommandFlags GetCommandFlags(Commands cmd);
const char *GetCommandName(Commands cmd);
bool IsCommandAllowedWhilePaused(Commands cmd);
bool IsCorrectCommandPayloadType(Commands cmd, const CommandPayloadBase *payload);

template <Commands Tcmd>
constexpr CommandFlags GetCommandFlags()
{
	return CommandTraits<Tcmd>::flags;
}

/**
 * Extracts the DC flags needed for DoCommand from the flags returned by GetCommandFlags
 * @param cmd_flags Flags from GetCommandFlags
 * @return flags for DoCommand
 */
inline DoCommandFlag CommandFlagsToDCFlags(CommandFlags cmd_flags)
{
	DoCommandFlag flags = DC_NONE;
	if (cmd_flags & CMD_NO_WATER) flags |= DC_NO_WATER;
	if (cmd_flags & CMD_AUTO) flags |= DC_AUTO;
	if (cmd_flags & CMD_ALL_TILES) flags |= DC_ALL_TILES;
	return flags;
}

void ExecuteCommandQueue();
void ClearCommandQueue();

template <Commands Tcmd>
void EnqueueDoCommandP(TileIndex tile, const CmdPayload<Tcmd> &payload, StringID error_msg, CommandCallback callback = CommandCallback::None, CallbackParameter callback_param = 0, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	extern void EnqueueDoCommandPImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags);
	return EnqueueDoCommandPImplementation(Tcmd, tile, payload, error_msg, callback, callback_param, intl_flags | DCIF_TYPE_CHECKED);
}

#endif /* COMMAND_FUNC_H */
