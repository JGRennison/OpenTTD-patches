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

template <Commands cmd>
CommandCost DoCommand(TileIndex tile, const typename CommandTraits<cmd>::PayloadType &payload, DoCommandFlag flags, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandImplementation(cmd, tile, payload, flags, intl_flags | DCIF_TYPE_CHECKED);
}

inline CommandCost DoCommandContainer(const DynBaseCommandContainer &container, DoCommandFlag flags)
{
	return DoCommandImplementation(container.cmd, container.tile, *container.payload, flags, DCIF_NONE);
}

template <typename T>
inline CommandCost DoCommandContainer(const BaseCommandContainer<T> &container, DoCommandFlag flags)
{
	return DoCommandImplementation(container.cmd, container.tile, container.payload, flags, DCIF_NONE);
}

inline CommandCost DoCommandEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, DoCommandFlag flags, uint32_t cmd, const char *text = nullptr)
{
	BaseCommandContainer<P123CmdData> cont = NewBaseCommandContainerBasic(tile, p1, p2, cmd);
	cont.payload.p3 = p3;
	if (text != nullptr) cont.payload.text = text;
	return DoCommandContainer(cont, flags);
}

inline CommandCost DoCommandOld(TileIndex tile, uint32_t p1, uint32_t p2, DoCommandFlag flags, uint32_t cmd, const char *text = nullptr)
{
	return DoCommandEx(tile, p1, p2, 0, flags, cmd, text);
}

/* DoCommandP and variants */

bool DoCommandPImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags);

inline bool DoCommandPContainer(const DynCommandContainer &container, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(container.command.cmd, container.command.tile, *container.command.payload, container.command.error_msg, container.callback, container.callback_param, intl_flags);
}

template <typename T>
inline bool DoCommandPContainer(const CommandContainer<T> &container, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(container.cmd, container.tile, container.payload, container.error_msg, container.callback, container.callback_param, intl_flags);
}

inline bool DoCommandPEx(TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd, CommandCallback callback = CommandCallback::None, const char *text = nullptr)
{
	CommandContainer<P123CmdData> cont = NewCommandContainerBasic(tile, p1, p2, cmd, callback);
	cont.payload.p3 = p3;
	if (text != nullptr) cont.payload.text = text;
	return DoCommandPContainer(cont);
}

inline bool DoCommandPOld(TileIndex tile, uint32_t p1, uint32_t p2, uint32_t cmd, CommandCallback callback = CommandCallback::None, const char *text = nullptr)
{
	return DoCommandPEx(tile, p1, p2, 0, cmd, callback, text);
}

template <Commands cmd>
bool DoCommandP(TileIndex tile, const typename CommandTraits<cmd>::PayloadType &payload, StringID error_msg, CommandCallback callback = CommandCallback::None, CallbackParameter callback_param = 0, DoCommandIntlFlag intl_flags = DCIF_NONE)
{
	return DoCommandPImplementation(cmd, tile, payload, error_msg, callback, callback_param, intl_flags | DCIF_TYPE_CHECKED);
}

template <Commands TCmd, typename T> struct DoCommandHelper;
template <Commands TCmd, typename T> struct DoCommandHelperNoTile;

template <Commands Tcmd, typename... Targs>
struct DoCommandHelper<Tcmd, std::tuple<Targs...>> {
	using PayloadType = typename CommandTraits<Tcmd>::PayloadType;

	static inline CommandCost Do(DoCommandFlag flags, TileIndex tile, Targs... args)
	{
		return DoCommand<Tcmd>(tile, PayloadType::Make(std::forward<Targs>(args)...), flags);
	}

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
struct DoCommandHelperNoTile<Tcmd, std::tuple<Targs...>> {
	using PayloadType = typename CommandTraits<Tcmd>::PayloadType;

	static inline CommandCost Do(DoCommandFlag flags, Targs... args)
	{
		return DoCommand<Tcmd>(0, PayloadType::Make(std::forward<Targs>(args)...), flags);
	}

	static inline bool Post(Targs... args)
	{
		return DoCommandP<Tcmd>(0, PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, CommandCallback::None);
	}

	static inline bool Post(StringID error_msg, Targs... args)
	{
		return DoCommandP<Tcmd>(0, PayloadType::Make(std::forward<Targs>(args)...), error_msg, CommandCallback::None);
	}

	static inline bool Post(CommandCallback callback, Targs... args)
	{
		return DoCommandP<Tcmd>(0, PayloadType::Make(std::forward<Targs>(args)...), (StringID)0, callback);
	}

	static inline bool Post(StringID error_msg, CommandCallback callback, Targs... args)
	{
		return DoCommandP<Tcmd>(0, PayloadType::Make(std::forward<Targs>(args)...), error_msg, callback);
	}
};

template <Commands Tcmd>
using Command = std::conditional_t<::CommandTraits<Tcmd>::no_tile,
		DoCommandHelperNoTile<Tcmd, typename ::CommandTraits<Tcmd>::PayloadType::Tuple>,
		DoCommandHelper<Tcmd, typename ::CommandTraits<Tcmd>::PayloadType::Tuple>>;

/* Other command functions */

CommandCost DoCommandPScript(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only, bool asynchronous);
CommandCost DoCommandPInternal(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only);

template <Commands Tcmd>
void NetworkSendCommand(TileIndex tile, const typename CommandTraits<Tcmd>::PayloadType &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company)
{
	extern void NetworkSendCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company);
	return NetworkSendCommandImplementation(Tcmd, tile, payload, error_msg, callback, callback_param, company);
}

extern Money _additional_cash_required;

inline bool IsValidCommand(Commands cmd) { return cmd < CMD_END; }
CommandFlags GetCommandFlags(Commands cmd);
const char *GetCommandName(Commands cmd);
bool IsCommandAllowedWhilePaused(Commands cmd);

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
void EnqueueDoCommandP(DynCommandContainer container, DoCommandIntlFlag intl_flags = DCIF_NONE);

#endif /* COMMAND_FUNC_H */
