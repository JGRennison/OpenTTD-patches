/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file console_cmds.cpp Implementation of the console hooks. */

#include "stdafx.h"
#include "console_internal.h"
#include "debug.h"
#include "engine_func.h"
#include "landscape.h"
#include "saveload/saveload.h"
#include "network/network.h"
#include "network/network_func.h"
#include "network/network_base.h"
#include "network/network_admin.h"
#include "network/network_client.h"
#include "network/network_server.h"
#include "command_func.h"
#include "settings_func.h"
#include "fios.h"
#include "fileio_func.h"
#include "screenshot.h"
#include "genworld.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "window_func.h"
#include "date_func.h"
#include "company_func.h"
#include "gamelog.h"
#include "ai/ai.hpp"
#include "ai/ai_config.hpp"
#include "newgrf.h"
#include "newgrf_profiling.h"
#include "console_func.h"
#include "engine_base.h"
#include "road.h"
#include "rail.h"
#include "game/game.hpp"
#include "table/strings.h"
#include "aircraft.h"
#include "airport.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "waypoint_func.h"
#include "economy_func.h"
#include "town.h"
#include "industry.h"
#include "string_func_extra.h"
#include "linkgraph/linkgraphjob.h"
#include "base_media_base.h"
#include "debug_settings.h"
#include <time.h>

#include "safeguards.h"

/* scriptfile handling */
static uint _script_current_depth; ///< Depth of scripts running (used to abort execution when #ConReturn is encountered).

/** File list storage for the console, for caching the last 'ls' command. */
class ConsoleFileList : public FileList {
public:
	ConsoleFileList() : FileList()
	{
		this->file_list_valid = false;
	}

	/** Declare the file storage cache as being invalid, also clears all stored files. */
	void InvalidateFileList()
	{
		this->clear();
		this->file_list_valid = false;
	}

	/**
	 * (Re-)validate the file storage cache. Only makes a change if the storage was invalid, or if \a force_reload.
	 * @param force_reload Always reload the file storage cache.
	 */
	void ValidateFileList(bool force_reload = false)
	{
		if (force_reload || !this->file_list_valid) {
			this->BuildFileList(FT_SAVEGAME, SLO_LOAD);
			this->file_list_valid = true;
		}
	}

	bool file_list_valid; ///< If set, the file list is valid.
};

static ConsoleFileList _console_file_list; ///< File storage cache for the console.

/* console command defines */
#define DEF_CONSOLE_CMD(function) static bool function(byte argc, char *argv[])
#define DEF_CONSOLE_HOOK(function) static ConsoleHookResult function(bool echo)

/****************
 * command hooks
 ****************/

/**
 * Check network availability and inform in console about failure of detection.
 * @return Network availability.
 */
static inline bool NetworkAvailable(bool echo)
{
	if (!_network_available) {
		if (echo) IConsoleError("You cannot use this command because there is no network available.");
		return false;
	}
	return true;
}

/**
 * Check whether we are a server.
 * @return Are we a server? True when yes, false otherwise.
 */
DEF_CONSOLE_HOOK(ConHookServerOnly)
{
	if (!NetworkAvailable(echo)) return CHR_DISALLOW;

	if (!_network_server) {
		if (echo) IConsoleError("This command is only available to a network server.");
		return CHR_DISALLOW;
	}
	return CHR_ALLOW;
}

/**
 * Check whether we are a client in a network game.
 * @return Are we a client in a network game? True when yes, false otherwise.
 */
DEF_CONSOLE_HOOK(ConHookClientOnly)
{
	if (!NetworkAvailable(echo)) return CHR_DISALLOW;

	if (_network_server) {
		if (echo) IConsoleError("This command is not available to a network server.");
		return CHR_DISALLOW;
	}
	return CHR_ALLOW;
}

/**
 * Check whether we are in a multiplayer game.
 * @return True when we are client or server in a network game.
 */
DEF_CONSOLE_HOOK(ConHookNeedNetwork)
{
	if (!NetworkAvailable(echo)) return CHR_DISALLOW;

	if (!_networking || (!_network_server && !MyClient::IsConnected())) {
		if (echo) IConsoleError("Not connected. This command is only available in multiplayer.");
		return CHR_DISALLOW;
	}
	return CHR_ALLOW;
}

/**
 * Check whether we are in singleplayer mode.
 * @return True when no network is active.
 */
DEF_CONSOLE_HOOK(ConHookNoNetwork)
{
	if (_networking) {
		if (echo) IConsoleError("This command is forbidden in multiplayer.");
		return CHR_DISALLOW;
	}
	return CHR_ALLOW;
}

DEF_CONSOLE_HOOK(ConHookNewGRFDeveloperTool)
{
	if (_settings_client.gui.newgrf_developer_tools) {
		if (_game_mode == GM_MENU) {
			if (echo) IConsoleError("This command is only available in game and editor.");
			return CHR_DISALLOW;
		}
		return ConHookNoNetwork(echo);
	}
	return CHR_HIDE;
}

/**
 * Show help for the console.
 * @param str String to print in the console.
 */
static void IConsoleHelp(const char *str)
{
	IConsolePrintF(CC_WARNING, "- %s", str);
}

/**
 * Reset status of all engines.
 * @return Will always succeed.
 */
DEF_CONSOLE_CMD(ConResetEngines)
{
	if (argc == 0) {
		IConsoleHelp("Reset status data of all engines. This might solve some issues with 'lost' engines. Usage: 'resetengines'");
		return true;
	}

	StartupEngines();
	return true;
}

/**
 * Reset status of the engine pool.
 * @return Will always return true.
 * @note Resetting the pool only succeeds when there are no vehicles ingame.
 */
DEF_CONSOLE_CMD(ConResetEnginePool)
{
	if (argc == 0) {
		IConsoleHelp("Reset NewGRF allocations of engine slots. This will remove invalid engine definitions, and might make default engines available again.");
		return true;
	}

	if (_game_mode == GM_MENU) {
		IConsoleError("This command is only available in game and editor.");
		return true;
	}

	if (!EngineOverrideManager::ResetToCurrentNewGRFConfig()) {
		IConsoleError("This can only be done when there are no vehicles in the game.");
		return true;
	}

	return true;
}

#ifdef _DEBUG
/**
 * Reset a tile to bare land in debug mode.
 * param tile number.
 * @return True when the tile is reset or the help on usage was printed (0 or two parameters).
 */
DEF_CONSOLE_CMD(ConResetTile)
{
	if (argc == 0) {
		IConsoleHelp("Reset a tile to bare land. Usage: 'resettile <tile>'");
		IConsoleHelp("Tile can be either decimal (34161) or hexadecimal (0x4a5B)");
		return true;
	}

	if (argc == 2) {
		uint32 result;
		if (GetArgumentInteger(&result, argv[1])) {
			DoClearSquare((TileIndex)result);
			return true;
		}
	}

	return false;
}
#endif /* _DEBUG */

/**
 * Scroll to a tile on the map.
 * param x tile number or tile x coordinate.
 * param y optional y coordinate.
 * @note When only one argument is given it is interpreted as the tile number.
 *       When two arguments are given, they are interpreted as the tile's x
 *       and y coordinates.
 * @return True when either console help was shown or a proper amount of parameters given.
 */
DEF_CONSOLE_CMD(ConScrollToTile)
{
	switch (argc) {
		case 0:
			IConsoleHelp("Center the screen on a given tile.");
			IConsoleHelp("Usage: 'scrollto <tile>' or 'scrollto <x> <y>'");
			IConsoleHelp("Numbers can be either decimal (34161) or hexadecimal (0x4a5B).");
			return true;

		case 2: {
			uint32 result;
			if (GetArgumentInteger(&result, argv[1])) {
				if (result >= MapSize()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				ScrollMainWindowToTile((TileIndex)result);
				return true;
			}
			break;
		}

		case 3: {
			uint32 x, y;
			if (GetArgumentInteger(&x, argv[1]) && GetArgumentInteger(&y, argv[2])) {
				if (x >= MapSizeX() || y >= MapSizeY()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				ScrollMainWindowToTile(TileXY(x, y));
				return true;
			}
			break;
		}
	}

	return false;
}

/**
 * Highlight a tile on the map.
 * param x tile number or tile x coordinate.
 * param y optional y coordinate.
 * @note When only one argument is given it is intepreted as the tile number.
 *       When two arguments are given, they are interpreted as the tile's x
 *       and y coordinates.
 * @return True when either console help was shown or a proper amount of parameters given.
 */
DEF_CONSOLE_CMD(ConHighlightTile)
{
	switch (argc) {
		case 0:
			IConsoleHelp("Highlight a given tile.");
			IConsoleHelp("Usage: 'highlight_tile <tile>' or 'highlight_tile <x> <y>'");
			IConsoleHelp("Numbers can be either decimal (34161) or hexadecimal (0x4a5B).");
			return true;

		case 2: {
			uint32 result;
			if (GetArgumentInteger(&result, argv[1])) {
				if (result >= MapSize()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				SetRedErrorSquare((TileIndex)result);
				return true;
			}
			break;
		}

		case 3: {
			uint32 x, y;
			if (GetArgumentInteger(&x, argv[1]) && GetArgumentInteger(&y, argv[2])) {
				if (x >= MapSizeX() || y >= MapSizeY()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				SetRedErrorSquare(TileXY(x, y));
				return true;
			}
			break;
		}
	}

	return false;
}

/**
 * Save the map to a file.
 * param filename the filename to save the map to.
 * @return True when help was displayed or the file attempted to be saved.
 */
DEF_CONSOLE_CMD(ConSave)
{
	if (argc == 0) {
		IConsoleHelp("Save the current game. Usage: 'save <filename>'");
		return true;
	}

	if (argc == 2) {
		char *filename = str_fmt("%s.sav", argv[1]);
		IConsolePrint(CC_DEFAULT, "Saving map...");

		if (SaveOrLoad(filename, SLO_SAVE, DFT_GAME_FILE, SAVE_DIR) != SL_OK) {
			IConsolePrint(CC_ERROR, "Saving map failed");
		} else {
			IConsolePrintF(CC_DEFAULT, "Map successfully saved to %s", filename);
		}
		free(filename);
		return true;
	}

	return false;
}

/**
 * Explicitly save the configuration.
 * @return True.
 */
DEF_CONSOLE_CMD(ConSaveConfig)
{
	if (argc == 0) {
		IConsoleHelp("Saves the configuration for new games to the configuration file, typically 'openttd.cfg'.");
		IConsoleHelp("It does not save the configuration of the current game to the configuration file.");
		return true;
	}

	SaveToConfig();
	IConsolePrint(CC_DEFAULT, "Saved config.");
	return true;
}

DEF_CONSOLE_CMD(ConLoad)
{
	if (argc == 0) {
		IConsoleHelp("Load a game by name or index. Usage: 'load <file | number>'");
		return true;
	}

	if (argc != 2) return false;

	const char *file = argv[1];
	_console_file_list.ValidateFileList();
	const FiosItem *item = _console_file_list.FindItem(file);
	if (item != nullptr) {
		if (GetAbstractFileType(item->type) == FT_SAVEGAME) {
			_switch_mode = SM_LOAD_GAME;
			_file_to_saveload.SetMode(item->type);
			_file_to_saveload.SetName(FiosBrowseTo(item));
			_file_to_saveload.SetTitle(item->title);
		} else {
			IConsolePrintF(CC_ERROR, "%s: Not a savegame.", file);
		}
	} else {
		IConsolePrintF(CC_ERROR, "%s: No such file or directory.", file);
	}

	return true;
}


DEF_CONSOLE_CMD(ConRemove)
{
	if (argc == 0) {
		IConsoleHelp("Remove a savegame by name or index. Usage: 'rm <file | number>'");
		return true;
	}

	if (argc != 2) return false;

	const char *file = argv[1];
	_console_file_list.ValidateFileList();
	const FiosItem *item = _console_file_list.FindItem(file);
	if (item != nullptr) {
		if (!FiosDelete(item->name)) {
			IConsolePrintF(CC_ERROR, "%s: Failed to delete file", file);
		}
	} else {
		IConsolePrintF(CC_ERROR, "%s: No such file or directory.", file);
	}

	_console_file_list.InvalidateFileList();
	return true;
}


/* List all the files in the current dir via console */
DEF_CONSOLE_CMD(ConListFiles)
{
	if (argc == 0) {
		IConsoleHelp("List all loadable savegames and directories in the current dir via console. Usage: 'ls | dir'");
		return true;
	}

	_console_file_list.ValidateFileList(true);
	for (uint i = 0; i < _console_file_list.size(); i++) {
		IConsolePrintF(CC_DEFAULT, "%d) %s", i, _console_file_list[i].title);
	}

	return true;
}

/* Change the dir via console */
DEF_CONSOLE_CMD(ConChangeDirectory)
{
	if (argc == 0) {
		IConsoleHelp("Change the dir via console. Usage: 'cd <directory | number>'");
		return true;
	}

	if (argc != 2) return false;

	const char *file = argv[1];
	_console_file_list.ValidateFileList(true);
	const FiosItem *item = _console_file_list.FindItem(file);
	if (item != nullptr) {
		switch (item->type) {
			case FIOS_TYPE_DIR: case FIOS_TYPE_DRIVE: case FIOS_TYPE_PARENT:
				FiosBrowseTo(item);
				break;
			default: IConsolePrintF(CC_ERROR, "%s: Not a directory.", file);
		}
	} else {
		IConsolePrintF(CC_ERROR, "%s: No such file or directory.", file);
	}

	_console_file_list.InvalidateFileList();
	return true;
}

DEF_CONSOLE_CMD(ConPrintWorkingDirectory)
{
	const char *path;

	if (argc == 0) {
		IConsoleHelp("Print out the current working directory. Usage: 'pwd'");
		return true;
	}

	/* XXX - Workaround for broken file handling */
	_console_file_list.ValidateFileList(true);
	_console_file_list.InvalidateFileList();

	FiosGetDescText(&path, nullptr);
	IConsolePrint(CC_DEFAULT, path);
	return true;
}

DEF_CONSOLE_CMD(ConClearBuffer)
{
	if (argc == 0) {
		IConsoleHelp("Clear the console buffer. Usage: 'clear'");
		return true;
	}

	IConsoleClearBuffer();
	SetWindowDirty(WC_CONSOLE, 0);
	return true;
}


/**********************************
 * Network Core Console Commands
 **********************************/

static bool ConKickOrBan(const char *argv, bool ban, const char *reason)
{
	uint n;

	if (strchr(argv, '.') == nullptr && strchr(argv, ':') == nullptr) { // banning with ID
		ClientID client_id = (ClientID)atoi(argv);

		/* Don't kill the server, or the client doing the rcon. The latter can't be kicked because
		 * kicking frees closes and subsequently free the connection related instances, which we
		 * would be reading from and writing to after returning. So we would read or write data
		 * from freed memory up till the segfault triggers. */
		if (client_id == CLIENT_ID_SERVER || client_id == _redirect_console_to_client) {
			IConsolePrintF(CC_ERROR, "ERROR: You can not %s yourself!", ban ? "ban" : "kick");
			return true;
		}

		NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(client_id);
		if (ci == nullptr) {
			IConsoleError("Invalid client");
			return true;
		}

		if (!ban) {
			/* Kick only this client, not all clients with that IP */
			NetworkServerKickClient(client_id, reason);
			return true;
		}

		/* When banning, kick+ban all clients with that IP */
		n = NetworkServerKickOrBanIP(client_id, ban, reason);
	} else {
		n = NetworkServerKickOrBanIP(argv, ban, reason);
	}

	if (n == 0) {
		IConsolePrint(CC_DEFAULT, ban ? "Client not online, address added to banlist" : "Client not found");
	} else {
		IConsolePrintF(CC_DEFAULT, "%sed %u client(s)", ban ? "Bann" : "Kick", n);
	}

	return true;
}

DEF_CONSOLE_CMD(ConKick)
{
	if (argc == 0) {
		IConsoleHelp("Kick a client from a network game. Usage: 'kick <ip | client-id> [<kick-reason>]'");
		IConsoleHelp("For client-id's, see the command 'clients'");
		return true;
	}

	if (argc != 2 && argc != 3) return false;

	/* No reason supplied for kicking */
	if (argc == 2) return ConKickOrBan(argv[1], false, nullptr);

	/* Reason for kicking supplied */
	size_t kick_message_length = strlen(argv[2]);
	if (kick_message_length >= 255) {
		IConsolePrintF(CC_ERROR, "ERROR: Maximum kick message length is 254 characters. You entered " PRINTF_SIZE " characters.", kick_message_length);
		return false;
	} else {
		return ConKickOrBan(argv[1], false, argv[2]);
	}
}

DEF_CONSOLE_CMD(ConBan)
{
	if (argc == 0) {
		IConsoleHelp("Ban a client from a network game. Usage: 'ban <ip | client-id> [<ban-reason>]'");
		IConsoleHelp("For client-id's, see the command 'clients'");
		IConsoleHelp("If the client is no longer online, you can still ban their IP");
		return true;
	}

	if (argc != 2 && argc != 3) return false;

	/* No reason supplied for kicking */
	if (argc == 2) return ConKickOrBan(argv[1], true, nullptr);

	/* Reason for kicking supplied */
	size_t kick_message_length = strlen(argv[2]);
	if (kick_message_length >= 255) {
		IConsolePrintF(CC_ERROR, "ERROR: Maximum kick message length is 254 characters. You entered " PRINTF_SIZE " characters.", kick_message_length);
		return false;
	} else {
		return ConKickOrBan(argv[1], true, argv[2]);
	}
}

DEF_CONSOLE_CMD(ConUnBan)
{
	if (argc == 0) {
		IConsoleHelp("Unban a client from a network game. Usage: 'unban <ip | banlist-index>'");
		IConsoleHelp("For a list of banned IP's, see the command 'banlist'");
		return true;
	}

	if (argc != 2) return false;

	/* Try by IP. */
	uint index;
	for (index = 0; index < _network_ban_list.size(); index++) {
		if (_network_ban_list[index] == argv[1]) break;
	}

	/* Try by index. */
	if (index >= _network_ban_list.size()) {
		index = atoi(argv[1]) - 1U; // let it wrap
	}

	if (index < _network_ban_list.size()) {
		char msg[64];
		seprintf(msg, lastof(msg), "Unbanned %s", _network_ban_list[index].c_str());
		IConsolePrint(CC_DEFAULT, msg);
		_network_ban_list.erase(_network_ban_list.begin() + index);
	} else {
		IConsolePrint(CC_DEFAULT, "Invalid list index or IP not in ban-list.");
		IConsolePrint(CC_DEFAULT, "For a list of banned IP's, see the command 'banlist'");
	}

	return true;
}

DEF_CONSOLE_CMD(ConBanList)
{
	if (argc == 0) {
		IConsoleHelp("List the IP's of banned clients: Usage 'banlist'");
		return true;
	}

	IConsolePrint(CC_DEFAULT, "Banlist: ");

	uint i = 1;
	for (const auto &entry : _network_ban_list) {
		IConsolePrintF(CC_DEFAULT, "  %d) %s", i, entry.c_str());
		i++;
	}

	return true;
}

DEF_CONSOLE_CMD(ConPauseGame)
{
	if (argc == 0) {
		IConsoleHelp("Pause a network game. Usage: 'pause'");
		return true;
	}

	if ((_pause_mode & PM_PAUSED_NORMAL) == PM_UNPAUSED) {
		DoCommandP(0, PM_PAUSED_NORMAL, 1, CMD_PAUSE);
		if (!_networking) IConsolePrint(CC_DEFAULT, "Game paused.");
	} else {
		IConsolePrint(CC_DEFAULT, "Game is already paused.");
	}

	return true;
}

DEF_CONSOLE_CMD(ConUnpauseGame)
{
	if (argc == 0) {
		IConsoleHelp("Unpause a network game. Usage: 'unpause'");
		return true;
	}

	if ((_pause_mode & PM_PAUSED_NORMAL) != PM_UNPAUSED) {
		DoCommandP(0, PM_PAUSED_NORMAL, 0, CMD_PAUSE);
		if (!_networking) IConsolePrint(CC_DEFAULT, "Game unpaused.");
	} else if ((_pause_mode & PM_PAUSED_ERROR) != PM_UNPAUSED) {
		IConsolePrint(CC_DEFAULT, "Game is in error state and cannot be unpaused via console.");
	} else if (_pause_mode != PM_UNPAUSED) {
		IConsolePrint(CC_DEFAULT, "Game cannot be unpaused manually; disable pause_on_join/min_active_clients.");
	} else {
		IConsolePrint(CC_DEFAULT, "Game is already unpaused.");
	}

	return true;
}

DEF_CONSOLE_CMD(ConRcon)
{
	if (argc == 0) {
		IConsoleHelp("Remote control the server from another client. Usage: 'rcon <password> <command>'");
		IConsoleHelp("Remember to enclose the command in quotes, otherwise only the first parameter is sent");
		return true;
	}

	if (argc < 3) return false;

	if (_network_server) {
		IConsoleCmdExec(argv[2]);
	} else {
		NetworkClientSendRcon(argv[1], argv[2]);
	}
	return true;
}

DEF_CONSOLE_CMD(ConSettingsAccess)
{
	if (argc == 0) {
		IConsoleHelp("Enable changing game settings from this client. Usage: 'settings_access <password>'");
		IConsoleHelp("Send an empty password \"\" to drop access");
		return true;
	}

	if (argc < 2) return false;

	if (!_network_server) {
		NetworkClientSendSettingsPassword(argv[1]);
	}
	return true;
}

DEF_CONSOLE_CMD(ConStatus)
{
	if (argc == 0) {
		IConsoleHelp("List the status of all clients connected to the server. Usage 'status'");
		return true;
	}

	NetworkServerShowStatusToConsole();
	return true;
}

DEF_CONSOLE_CMD(ConServerInfo)
{
	if (argc == 0) {
		IConsoleHelp("List current and maximum client/company limits. Usage 'server_info'");
		IConsoleHelp("You can change these values by modifying settings 'network.max_clients', 'network.max_companies' and 'network.max_spectators'");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "Current/maximum clients:    %2d/%2d", _network_game_info.clients_on, _settings_client.network.max_clients);
	IConsolePrintF(CC_DEFAULT, "Current/maximum companies:  %2d/%2d", (int)Company::GetNumItems(), _settings_client.network.max_companies);
	IConsolePrintF(CC_DEFAULT, "Current/maximum spectators: %2d/%2d", NetworkSpectatorCount(), _settings_client.network.max_spectators);

	return true;
}

DEF_CONSOLE_CMD(ConClientNickChange)
{
	if (argc != 3) {
		IConsoleHelp("Change the nickname of a connected client. Usage: 'client_name <client-id> <new-name>'");
		IConsoleHelp("For client-id's, see the command 'clients'");
		return true;
	}

	ClientID client_id = (ClientID)atoi(argv[1]);

	if (client_id == CLIENT_ID_SERVER) {
		IConsoleError("Please use the command 'name' to change your own name!");
		return true;
	}

	if (NetworkClientInfo::GetByClientID(client_id) == nullptr) {
		IConsoleError("Invalid client");
		return true;
	}

	char *client_name = argv[2];
	StrTrimInPlace(client_name);
	if (!NetworkIsValidClientName(client_name)) {
		IConsoleError("Cannot give a client an empty name");
		return true;
	}

	if (!NetworkServerChangeClientName(client_id, client_name)) {
		IConsoleError("Cannot give a client a duplicate name");
	}

	return true;
}

DEF_CONSOLE_CMD(ConJoinCompany)
{
	if (argc < 2) {
		IConsoleHelp("Request joining another company. Usage: join <company-id> [<password>]");
		IConsoleHelp("For valid company-id see company list, use 255 for spectator");
		return true;
	}

	CompanyID company_id = (CompanyID)(atoi(argv[1]) <= MAX_COMPANIES ? atoi(argv[1]) - 1 : atoi(argv[1]));

	/* Check we have a valid company id! */
	if (!Company::IsValidID(company_id) && company_id != COMPANY_SPECTATOR) {
		IConsolePrintF(CC_ERROR, "Company does not exist. Company-id must be between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	if (NetworkClientInfo::GetByClientID(_network_own_client_id)->client_playas == company_id) {
		IConsoleError("You are already there!");
		return true;
	}

	if (company_id == COMPANY_SPECTATOR && NetworkMaxSpectatorsReached()) {
		IConsoleError("Cannot join spectators, maximum number of spectators reached.");
		return true;
	}

	if (company_id != COMPANY_SPECTATOR && !Company::IsHumanID(company_id)) {
		IConsoleError("Cannot join AI company.");
		return true;
	}

	/* Check if the company requires a password */
	if (NetworkCompanyIsPassworded(company_id) && argc < 3) {
		IConsolePrintF(CC_ERROR, "Company %d requires a password to join.", company_id + 1);
		return true;
	}

	/* non-dedicated server may just do the move! */
	if (_network_server) {
		NetworkServerDoMove(CLIENT_ID_SERVER, company_id);
	} else {
		NetworkClientRequestMove(company_id, NetworkCompanyIsPassworded(company_id) ? argv[2] : "");
	}

	return true;
}

DEF_CONSOLE_CMD(ConMoveClient)
{
	if (argc < 3) {
		IConsoleHelp("Move a client to another company. Usage: move <client-id> <company-id>");
		IConsoleHelp("For valid client-id see 'clients', for valid company-id see 'companies', use 255 for moving to spectators");
		return true;
	}

	const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID((ClientID)atoi(argv[1]));
	CompanyID company_id = (CompanyID)(atoi(argv[2]) <= MAX_COMPANIES ? atoi(argv[2]) - 1 : atoi(argv[2]));

	/* check the client exists */
	if (ci == nullptr) {
		IConsoleError("Invalid client-id, check the command 'clients' for valid client-id's.");
		return true;
	}

	if (!Company::IsValidID(company_id) && company_id != COMPANY_SPECTATOR) {
		IConsolePrintF(CC_ERROR, "Company does not exist. Company-id must be between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	if (company_id != COMPANY_SPECTATOR && !Company::IsHumanID(company_id)) {
		IConsoleError("You cannot move clients to AI companies.");
		return true;
	}

	if (ci->client_id == CLIENT_ID_SERVER && _network_dedicated) {
		IConsoleError("You cannot move the server!");
		return true;
	}

	if (ci->client_playas == company_id) {
		IConsoleError("You cannot move someone to where they already are!");
		return true;
	}

	/* we are the server, so force the update */
	NetworkServerDoMove(ci->client_id, company_id);

	return true;
}

DEF_CONSOLE_CMD(ConResetCompany)
{
	if (argc == 0) {
		IConsoleHelp("Remove an idle company from the game. Usage: 'reset_company <company-id>'");
		IConsoleHelp("For company-id's, see the list of companies from the dropdown menu. Company 1 is 1, etc.");
		return true;
	}

	if (argc != 2) return false;

	CompanyID index = (CompanyID)(atoi(argv[1]) - 1);

	/* Check valid range */
	if (!Company::IsValidID(index)) {
		IConsolePrintF(CC_ERROR, "Company does not exist. Company-id must be between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	if (!Company::IsHumanID(index)) {
		IConsoleError("Company is owned by an AI.");
		return true;
	}

	if (NetworkCompanyHasClients(index)) {
		IConsoleError("Cannot remove company: a client is connected to that company.");
		return false;
	}
	const NetworkClientInfo *ci = NetworkClientInfo::GetByClientID(CLIENT_ID_SERVER);
	if (ci->client_playas == index) {
		IConsoleError("Cannot remove company: the server is connected to that company.");
		return true;
	}

	/* It is safe to remove this company */
	DoCommandP(0, CCA_DELETE | index << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
	IConsolePrint(CC_DEFAULT, "Company deleted.");

	return true;
}

DEF_CONSOLE_CMD(ConNetworkClients)
{
	if (argc == 0) {
		IConsoleHelp("Get a list of connected clients including their ID, name, company-id, and IP. Usage: 'clients'");
		return true;
	}

	NetworkPrintClients();

	return true;
}

DEF_CONSOLE_CMD(ConNetworkReconnect)
{
	if (argc == 0) {
		IConsoleHelp("Reconnect to server to which you were connected last time. Usage: 'reconnect [<company>]'");
		IConsoleHelp("Company 255 is spectator (default, if not specified), 0 means creating new company.");
		IConsoleHelp("All others are a certain company with Company 1 being #1");
		return true;
	}

	CompanyID playas = (argc >= 2) ? (CompanyID)atoi(argv[1]) : COMPANY_SPECTATOR;
	switch (playas) {
		case 0: playas = COMPANY_NEW_COMPANY; break;
		case COMPANY_SPECTATOR: /* nothing to do */ break;
		default:
			/* From a user pov 0 is a new company, internally it's different and all
			 * companies are offset by one to ease up on users (eg companies 1-8 not 0-7) */
			if (playas < COMPANY_FIRST + 1 || playas > MAX_COMPANIES + 1) return false;
			break;
	}

	if (StrEmpty(_settings_client.network.last_host)) {
		IConsolePrint(CC_DEFAULT, "No server for reconnecting.");
		return true;
	}

	/* Don't resolve the address first, just print it directly as it comes from the config file. */
	IConsolePrintF(CC_DEFAULT, "Reconnecting to %s:%d...", _settings_client.network.last_host, _settings_client.network.last_port);

	NetworkClientConnectGame(_settings_client.network.last_host, _settings_client.network.last_port, playas);
	return true;
}

DEF_CONSOLE_CMD(ConNetworkConnect)
{
	if (argc == 0) {
		IConsoleHelp("Connect to a remote OTTD server and join the game. Usage: 'connect <ip>'");
		IConsoleHelp("IP can contain port and company: 'IP[:Port][#Company]', eg: 'server.ottd.org:443#2'");
		IConsoleHelp("Company #255 is spectator all others are a certain company with Company 1 being #1");
		return true;
	}

	if (argc < 2) return false;
	if (_networking) NetworkDisconnect(); // we are in network-mode, first close it!

	const char *port = nullptr;
	const char *company = nullptr;
	char *ip = argv[1];
	/* Default settings: default port and new company */
	uint16 rport = NETWORK_DEFAULT_PORT;
	CompanyID join_as = COMPANY_NEW_COMPANY;

	ParseGameConnectionString(&company, &port, ip);

	IConsolePrintF(CC_DEFAULT, "Connecting to %s...", ip);
	if (company != nullptr) {
		join_as = (CompanyID)atoi(company);
		IConsolePrintF(CC_DEFAULT, "    company-no: %d", join_as);

		/* From a user pov 0 is a new company, internally it's different and all
		 * companies are offset by one to ease up on users (eg companies 1-8 not 0-7) */
		if (join_as != COMPANY_SPECTATOR) {
			if (join_as > MAX_COMPANIES) return false;
			join_as--;
		}
	}
	if (port != nullptr) {
		rport = atoi(port);
		IConsolePrintF(CC_DEFAULT, "    port: %s", port);
	}

	NetworkClientConnectGame(ip, rport, join_as);

	return true;
}

/*********************************
 *  script file console commands
 *********************************/

DEF_CONSOLE_CMD(ConExec)
{
	if (argc == 0) {
		IConsoleHelp("Execute a local script file. Usage: 'exec <script> <?>'");
		return true;
	}

	if (argc < 2) return false;

	FILE *script_file = FioFOpenFile(argv[1], "r", BASE_DIR);

	if (script_file == nullptr) {
		if (argc == 2 || atoi(argv[2]) != 0) IConsoleError("script file not found");
		return true;
	}

	if (_script_current_depth == 11) {
		FioFCloseFile(script_file);
		IConsoleError("Maximum 'exec' depth reached; script A is calling script B is calling script C ... more than 10 times.");
		return true;
	}

	_script_current_depth++;
	uint script_depth = _script_current_depth;

	char cmdline[ICON_CMDLN_SIZE];
	while (fgets(cmdline, sizeof(cmdline), script_file) != nullptr) {
		/* Remove newline characters from the executing script */
		for (char *cmdptr = cmdline; *cmdptr != '\0'; cmdptr++) {
			if (*cmdptr == '\n' || *cmdptr == '\r') {
				*cmdptr = '\0';
				break;
			}
		}
		IConsoleCmdExec(cmdline);
		/* Ensure that we are still on the same depth or that we returned via 'return'. */
		assert(_script_current_depth == script_depth || _script_current_depth == script_depth - 1);

		/* The 'return' command was executed. */
		if (_script_current_depth == script_depth - 1) break;
	}

	if (ferror(script_file)) {
		IConsoleError("Encountered error while trying to read from script file");
	}

	if (_script_current_depth == script_depth) _script_current_depth--;
	FioFCloseFile(script_file);
	return true;
}

DEF_CONSOLE_CMD(ConReturn)
{
	if (argc == 0) {
		IConsoleHelp("Stop executing a running script. Usage: 'return'");
		return true;
	}

	_script_current_depth--;
	return true;
}

/*****************************
 *  default console commands
 ******************************/
extern bool CloseConsoleLogIfActive();

DEF_CONSOLE_CMD(ConScript)
{
	extern FILE *_iconsole_output_file;

	if (argc == 0) {
		IConsoleHelp("Start or stop logging console output to a file. Usage: 'script <filename>'");
		IConsoleHelp("If filename is omitted, a running log is stopped if it is active");
		return true;
	}

	if (!CloseConsoleLogIfActive()) {
		if (argc < 2) return false;

		IConsolePrintF(CC_DEFAULT, "file output started to: %s", argv[1]);
		_iconsole_output_file = fopen(argv[1], "ab");
		if (_iconsole_output_file == nullptr) IConsoleError("could not open file");
	}

	return true;
}


DEF_CONSOLE_CMD(ConEcho)
{
	if (argc == 0) {
		IConsoleHelp("Print back the first argument to the console. Usage: 'echo <arg>'");
		return true;
	}

	if (argc < 2) return false;
	IConsolePrint(CC_DEFAULT, argv[1]);
	return true;
}

DEF_CONSOLE_CMD(ConEchoC)
{
	if (argc == 0) {
		IConsoleHelp("Print back the first argument to the console in a given colour. Usage: 'echoc <colour> <arg2>'");
		return true;
	}

	if (argc < 3) return false;
	IConsolePrint((TextColour)Clamp(atoi(argv[1]), TC_BEGIN, TC_END - 1), argv[2]);
	return true;
}

DEF_CONSOLE_CMD(ConNewGame)
{
	if (argc == 0) {
		IConsoleHelp("Start a new game. Usage: 'newgame [seed]'");
		IConsoleHelp("The server can force a new game using 'newgame'; any client joined will rejoin after the server is done generating the new game.");
		return true;
	}

	StartNewGameWithoutGUI((argc == 2) ? strtoul(argv[1], nullptr, 10) : GENERATE_NEW_SEED);
	return true;
}

DEF_CONSOLE_CMD(ConRestart)
{
	if (argc == 0) {
		IConsoleHelp("Restart game. Usage: 'restart'");
		IConsoleHelp("Restarts a game. It tries to reproduce the exact same map as the game started with.");
		IConsoleHelp("However:");
		IConsoleHelp(" * restarting games started in another version might create another map due to difference in map generation");
		IConsoleHelp(" * restarting games based on scenarios, loaded games or heightmaps will start a new game based on the settings stored in the scenario/savegame");
		return true;
	}

	/* Don't copy the _newgame pointers to the real pointers, so call SwitchToMode directly */
	_settings_game.game_creation.map_x = MapLogX();
	_settings_game.game_creation.map_y = FindFirstBit(MapSizeY());
	_switch_mode = SM_RESTARTGAME;
	return true;
}

DEF_CONSOLE_CMD(ConReload)
{
	if (argc == 0) {
		IConsoleHelp("Reload game. Usage: 'reload'");
		IConsoleHelp("Reloads a game.");
		IConsoleHelp(" * if you started from a savegame / scenario / heightmap, that exact same savegame / scenario / heightmap will be loaded.");
		IConsoleHelp(" * if you started from a new game, this acts the same as 'restart'.");
		return true;
	}

	/* Don't copy the _newgame pointers to the real pointers, so call SwitchToMode directly */
	_settings_game.game_creation.map_x = MapLogX();
	_settings_game.game_creation.map_y = FindFirstBit(MapSizeY());
	_switch_mode = SM_RELOADGAME;
	return true;
}

/**
 * Print a text buffer line by line to the console. Lines are separated by '\n'.
 * @param buf The buffer to print.
 * @note All newlines are replace by '\0' characters.
 */
static void PrintLineByLine(char *buf)
{
	ProcessLineByLine(buf, [&](const char *line) {
		IConsolePrintF(CC_DEFAULT, "%s", line);
	});
}

DEF_CONSOLE_CMD(ConListAILibs)
{
	char buf[4096];
	AI::GetConsoleLibraryList(buf, lastof(buf));

	PrintLineByLine(buf);

	return true;
}

DEF_CONSOLE_CMD(ConListAI)
{
	char buf[4096];
	AI::GetConsoleList(buf, lastof(buf));

	PrintLineByLine(buf);

	return true;
}

DEF_CONSOLE_CMD(ConListGameLibs)
{
	char buf[4096];
	Game::GetConsoleLibraryList(buf, lastof(buf));

	PrintLineByLine(buf);

	return true;
}

DEF_CONSOLE_CMD(ConListGame)
{
	char buf[4096];
	Game::GetConsoleList(buf, lastof(buf));

	PrintLineByLine(buf);

	return true;
}

DEF_CONSOLE_CMD(ConStartAI)
{
	if (argc == 0 || argc > 3) {
		IConsoleHelp("Start a new AI. Usage: 'start_ai [<AI>] [<settings>]'");
		IConsoleHelp("Start a new AI. If <AI> is given, it starts that specific AI (if found).");
		IConsoleHelp("If <settings> is given, it is parsed and the AI settings are set to that.");
		return true;
	}

	if (_game_mode != GM_NORMAL) {
		IConsoleWarning("AIs can only be managed in a game.");
		return true;
	}

	if (Company::GetNumItems() == CompanyPool::MAX_SIZE) {
		IConsoleWarning("Can't start a new AI (no more free slots).");
		return true;
	}
	if (_networking && !_network_server) {
		IConsoleWarning("Only the server can start a new AI.");
		return true;
	}
	if (_networking && !_settings_game.ai.ai_in_multiplayer) {
		IConsoleWarning("AIs are not allowed in multiplayer by configuration.");
		IConsoleWarning("Switch AI -> AI in multiplayer to True.");
		return true;
	}
	if (!AI::CanStartNew()) {
		IConsoleWarning("Can't start a new AI.");
		return true;
	}

	int n = 0;
	/* Find the next free slot */
	for (const Company *c : Company::Iterate()) {
		if (c->index != n) break;
		n++;
	}

	AIConfig *config = AIConfig::GetConfig((CompanyID)n);
	if (argc >= 2) {
		config->Change(argv[1], -1, false);

		/* If the name is not found, and there is a dot in the name,
		 * try again with the assumption everything right of the dot is
		 * the version the user wants to load. */
		if (!config->HasScript()) {
			char *name = stredup(argv[1]);
			char *e = strrchr(name, '.');
			if (e != nullptr) {
				*e = '\0';
				e++;

				int version = atoi(e);
				config->Change(name, version, true);
			}
			free(name);
		}

		if (!config->HasScript()) {
			IConsoleWarning("Failed to load the specified AI");
			return true;
		}
		if (argc == 3) {
			config->StringToSettings(argv[2]);
		}
	}

	/* Start a new AI company */
	DoCommandP(0, CCA_NEW_AI | INVALID_COMPANY << 16, 0, CMD_COMPANY_CTRL);

	return true;
}

DEF_CONSOLE_CMD(ConReloadAI)
{
	if (argc != 2) {
		IConsoleHelp("Reload an AI. Usage: 'reload_ai <company-id>'");
		IConsoleHelp("Reload the AI with the given company id. For company-id's, see the list of companies from the dropdown menu. Company 1 is 1, etc.");
		return true;
	}

	if (_game_mode != GM_NORMAL) {
		IConsoleWarning("AIs can only be managed in a game.");
		return true;
	}

	if (_networking && !_network_server) {
		IConsoleWarning("Only the server can reload an AI.");
		return true;
	}

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	if (!Company::IsValidID(company_id)) {
		IConsolePrintF(CC_DEFAULT, "Unknown company. Company range is between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	/* In singleplayer mode the player can be in an AI company, after cheating or loading network save with an AI in first slot. */
	if (Company::IsHumanID(company_id) || company_id == _local_company) {
		IConsoleWarning("Company is not controlled by an AI.");
		return true;
	}

	/* First kill the company of the AI, then start a new one. This should start the current AI again */
	DoCommandP(0, CCA_DELETE | company_id << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
	DoCommandP(0, CCA_NEW_AI | company_id << 16, 0, CMD_COMPANY_CTRL);
	IConsolePrint(CC_DEFAULT, "AI reloaded.");

	return true;
}

DEF_CONSOLE_CMD(ConStopAI)
{
	if (argc != 2) {
		IConsoleHelp("Stop an AI. Usage: 'stop_ai <company-id>'");
		IConsoleHelp("Stop the AI with the given company id. For company-id's, see the list of companies from the dropdown menu. Company 1 is 1, etc.");
		return true;
	}

	if (_game_mode != GM_NORMAL) {
		IConsoleWarning("AIs can only be managed in a game.");
		return true;
	}

	if (_networking && !_network_server) {
		IConsoleWarning("Only the server can stop an AI.");
		return true;
	}

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	if (!Company::IsValidID(company_id)) {
		IConsolePrintF(CC_DEFAULT, "Unknown company. Company range is between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	/* In singleplayer mode the player can be in an AI company, after cheating or loading network save with an AI in first slot. */
	if (Company::IsHumanID(company_id) || company_id == _local_company) {
		IConsoleWarning("Company is not controlled by an AI.");
		return true;
	}

	/* Now kill the company of the AI. */
	DoCommandP(0, CCA_DELETE | company_id << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
	IConsolePrint(CC_DEFAULT, "AI stopped, company deleted.");

	return true;
}

DEF_CONSOLE_CMD(ConRescanAI)
{
	if (argc == 0) {
		IConsoleHelp("Rescan the AI dir for scripts. Usage: 'rescan_ai'");
		return true;
	}

	if (_networking && !_network_server) {
		IConsoleWarning("Only the server can rescan the AI dir for scripts.");
		return true;
	}

	AI::Rescan();

	return true;
}

DEF_CONSOLE_CMD(ConRescanGame)
{
	if (argc == 0) {
		IConsoleHelp("Rescan the Game Script dir for scripts. Usage: 'rescan_game'");
		return true;
	}

	if (_networking && !_network_server) {
		IConsoleWarning("Only the server can rescan the Game Script dir for scripts.");
		return true;
	}

	Game::Rescan();

	return true;
}

DEF_CONSOLE_CMD(ConRescanNewGRF)
{
	if (argc == 0) {
		IConsoleHelp("Rescan the data dir for NewGRFs. Usage: 'rescan_newgrf'");
		return true;
	}

	if (!RequestNewGRFScan()) {
		IConsoleWarning("NewGRF scanning is already running. Please wait until completed to run again.");
	}

	return true;
}

DEF_CONSOLE_CMD(ConGetSeed)
{
	if (argc == 0) {
		IConsoleHelp("Returns the seed used to create this game. Usage: 'getseed'");
		IConsoleHelp("The seed can be used to reproduce the exact same map as the game started with.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "Generation Seed: %u", _settings_game.game_creation.generation_seed);
	return true;
}

DEF_CONSOLE_CMD(ConGetDate)
{
	if (argc == 0) {
		IConsoleHelp("Returns the current date (year-month-day) of the game. Usage: 'getdate'");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "Date: %04d-%02d-%02d", _cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day);
	return true;
}

DEF_CONSOLE_CMD(ConGetSysDate)
{
	if (argc == 0) {
		IConsoleHelp("Returns the current date (year-month-day) of your system. Usage: 'getsysdate'");
		return true;
	}

	time_t t;
	time(&t);
	auto timeinfo = localtime(&t);
	IConsolePrintF(CC_DEFAULT, "System Date: %04d-%02d-%02d %02d:%02d:%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	return true;
}


DEF_CONSOLE_CMD(ConAlias)
{
	IConsoleAlias *alias;

	if (argc == 0) {
		IConsoleHelp("Add a new alias, or redefine the behaviour of an existing alias . Usage: 'alias <name> <command>'");
		return true;
	}

	if (argc < 3) return false;

	alias = IConsole::AliasGet(argv[1]);
	if (alias == nullptr) {
		IConsole::AliasRegister(argv[1], argv[2]);
	} else {
		alias->cmdline = argv[2];
	}
	return true;
}

DEF_CONSOLE_CMD(ConScreenShot)
{
	if (argc == 0) {
		IConsoleHelp("Create a screenshot of the game. Usage: 'screenshot [viewport | normal | big | giant | world | heightmap | minimap] [no_con] [size <width> <height>] [<filename>]'");
		IConsoleHelp("'viewport' (default) makes a screenshot of the current viewport (including menus, windows, ..), "
				"'normal' makes a screenshot of the visible area, "
				"'big' makes a zoomed-in screenshot of the visible area, "
				"'giant' makes a screenshot of the whole map using the default zoom level, "
				"'world' makes a screenshot of the whole map using the current zoom level, "
				"'heightmap' makes a heightmap screenshot of the map that can be loaded in as heightmap, "
				"'minimap' makes a top-viewed minimap screenshot of the whole world which represents one tile by one pixel. "
				"'topography' makes a top-viewed topography screenshot of the whole world which represents one tile by one pixel. "
				"'industry' makes a top-viewed industries screenshot of the whole world which represents one tile by one pixel. "
				"'no_con' hides the console to create the screenshot (only useful in combination with 'viewport'). "
				"'size' sets the width and height of the viewport to make a screenshot of (only useful in combination with 'normal' or 'big').");
		return true;
	}

	if (argc > 7) return false;

	ScreenshotType type = SC_VIEWPORT;
	uint32 width = 0;
	uint32 height = 0;
	std::string name{};
	uint32 arg_index = 1;

	if (argc > arg_index) {
		if (strcmp(argv[arg_index], "viewport") == 0) {
			type = SC_VIEWPORT;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "normal") == 0) {
			type = SC_DEFAULTZOOM;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "big") == 0) {
			type = SC_ZOOMEDIN;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "giant") == 0) {
			type = SC_WORLD;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "world") == 0) {
			type = SC_WORLD_ZOOM;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "heightmap") == 0) {
			type = SC_HEIGHTMAP;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "minimap") == 0) {
			type = SC_MINIMAP;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "topography") == 0) {
			type = SC_TOPOGRAPHY;
			arg_index += 1;
		} else if (strcmp(argv[arg_index], "industry") == 0) {
			type = SC_INDUSTRY;
			arg_index += 1;
		}
	}

	if (argc > arg_index && strcmp(argv[arg_index], "no_con") == 0) {
		if (type != SC_VIEWPORT) {
			IConsoleError("'no_con' can only be used in combination with 'viewport'");
			return true;
		}
		IConsoleClose();
		arg_index += 1;
	}

	if (argc > arg_index + 2 && strcmp(argv[arg_index], "size") == 0) {
		/* size <width> <height> */
		if (type != SC_DEFAULTZOOM && type != SC_ZOOMEDIN) {
			IConsoleError("'size' can only be used in combination with 'normal' or 'big'");
			return true;
		}
		GetArgumentInteger(&width, argv[arg_index + 1]);
		GetArgumentInteger(&height, argv[arg_index + 2]);
		arg_index += 3;
	}

	if (argc > arg_index) {
		/* Last parameter that was not one of the keywords must be the filename. */
		name = argv[arg_index];
		arg_index += 1;
	}

	if (argc > arg_index) {
		/* We have parameters we did not process; means we misunderstood any of the above. */
		return false;
	}

	MakeScreenshot(type, name, width, height);
	return true;
}

DEF_CONSOLE_CMD(ConMinimap)
{
	if (argc == 0) {
		IConsoleHelp("Create a flat image of the game minimap. Usage: 'minimap [owner] [file name]'");
		IConsoleHelp("'owner' uses the tile owner to colour the minimap image, this is the only mode at present");
		return true;
	}

	const char *name = nullptr;
	if (argc > 1) {
		if (strcmp(argv[1], "owner") != 0) {
			/* invalid mode */
			return false;
		}
	}
	if (argc > 2) {
		name = argv[2];
	}

	MakeMinimapWorldScreenshot(name);
	return true;
}

DEF_CONSOLE_CMD(ConInfoCmd)
{
	if (argc == 0) {
		IConsoleHelp("Print out debugging information about a command. Usage: 'info_cmd <cmd>'");
		return true;
	}

	if (argc < 2) return false;

	const IConsoleCmd *cmd = IConsole::CmdGet(argv[1]);
	if (cmd == nullptr) {
		IConsoleError("the given command was not found");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "command name: %s", cmd->name.c_str());
	IConsolePrintF(CC_DEFAULT, "command proc: %p", cmd->proc);

	if (cmd->hook != nullptr) IConsoleWarning("command is hooked");

	return true;
}

DEF_CONSOLE_CMD(ConDebugLevel)
{
	if (argc == 0) {
		IConsoleHelp("Get/set the default debugging level for the game. Usage: 'debug_level [<level>]'");
		IConsoleHelp("Level can be any combination of names, levels. Eg 'net=5 ms=4'. Remember to enclose it in \"'s");
		return true;
	}

	if (argc > 2) return false;

	if (argc == 1) {
		IConsolePrintF(CC_DEFAULT, "Current debug-level: '%s'", GetDebugString());
	} else {
		SetDebugString(argv[1]);
	}

	return true;
}

DEF_CONSOLE_CMD(ConExit)
{
	if (argc == 0) {
		IConsoleHelp("Exit the game. Usage: 'exit'");
		return true;
	}

	if (_game_mode == GM_NORMAL && _settings_client.gui.autosave_on_exit) DoExitSave();

	_exit_game = true;
	return true;
}

DEF_CONSOLE_CMD(ConPart)
{
	if (argc == 0) {
		IConsoleHelp("Leave the currently joined/running game (only ingame). Usage: 'part'");
		return true;
	}

	if (_game_mode != GM_NORMAL) return false;

	_switch_mode = SM_MENU;
	return true;
}

DEF_CONSOLE_CMD(ConHelp)
{
	if (argc == 2) {
		const IConsoleCmd *cmd;
		const IConsoleAlias *alias;

		cmd = IConsole::CmdGet(argv[1]);
		if (cmd != nullptr) {
			cmd->proc(0, nullptr);
			return true;
		}

		alias = IConsole::AliasGet(argv[1]);
		if (alias != nullptr) {
			cmd = IConsole::CmdGet(alias->cmdline);
			if (cmd != nullptr) {
				cmd->proc(0, nullptr);
				return true;
			}
			IConsolePrintF(CC_ERROR, "ERROR: alias is of special type, please see its execution-line: '%s'", alias->cmdline.c_str());
			return true;
		}

		IConsoleError("command not found");
		return true;
	}

	IConsolePrint(CC_WARNING, " ---- OpenTTD Console Help ---- ");
	IConsolePrint(CC_DEFAULT, " - commands: [command to list all commands: list_cmds]");
	IConsolePrint(CC_DEFAULT, " call commands with '<command> <arg2> <arg3>...'");
	IConsolePrint(CC_DEFAULT, " - to assign strings, or use them as arguments, enclose it within quotes");
	IConsolePrint(CC_DEFAULT, " like this: '<command> \"string argument with spaces\"'");
	IConsolePrint(CC_DEFAULT, " - use 'help <command>' to get specific information");
	IConsolePrint(CC_DEFAULT, " - scroll console output with shift + (up | down | pageup | pagedown)");
	IConsolePrint(CC_DEFAULT, " - scroll console input history with the up or down arrows");
	IConsolePrint(CC_DEFAULT, "");
	return true;
}

DEF_CONSOLE_CMD(ConListCommands)
{
	if (argc == 0) {
		IConsoleHelp("List all registered commands. Usage: 'list_cmds [<pre-filter>]'");
		return true;
	}

	for (auto &it : IConsole::Commands()) {
		const IConsoleCmd *cmd = &it.second;
		if (argv[1] == nullptr || cmd->name.find(argv[1]) != std::string::npos) {
			if ((_settings_client.gui.console_show_unlisted || !cmd->unlisted) && (cmd->hook == nullptr || cmd->hook(false) != CHR_HIDE)) IConsolePrintF(CC_DEFAULT, "%s", cmd->name.c_str());
		}
	}

	return true;
}

DEF_CONSOLE_CMD(ConListAliases)
{
	if (argc == 0) {
		IConsoleHelp("List all registered aliases. Usage: 'list_aliases [<pre-filter>]'");
		return true;
	}

	for (auto &it : IConsole::Aliases()) {
		const IConsoleAlias *alias = &it.second;
		if (argv[1] == nullptr || alias->name.find(argv[1]) != std::string::npos) {
			IConsolePrintF(CC_DEFAULT, "%s => %s", alias->name.c_str(), alias->cmdline.c_str());
		}
	}

	return true;
}

DEF_CONSOLE_CMD(ConCompanies)
{
	if (argc == 0) {
		IConsoleHelp("List the details of all companies in the game. Usage 'companies'");
		return true;
	}

	for (const Company *c : Company::Iterate()) {
		/* Grab the company name */
		char company_name[512];
		SetDParam(0, c->index);
		GetString(company_name, STR_COMPANY_NAME, lastof(company_name));

		const char *password_state = "";
		if (c->is_ai) {
			password_state = "AI";
		} else if (_network_server) {
				password_state = StrEmpty(_network_company_states[c->index].password) ? "unprotected" : "protected";
		}

		char colour[512];
		GetString(colour, STR_COLOUR_DARK_BLUE + _company_colours[c->index], lastof(colour));
		IConsolePrintF(CC_INFO, "#:%d(%s) Company Name: '%s'  Year Founded: %d  Money: " OTTD_PRINTF64 "  Loan: " OTTD_PRINTF64 "  Value: " OTTD_PRINTF64 "  (T:%d, R:%d, P:%d, S:%d) %s",
			c->index + 1, colour, company_name,
			c->inaugurated_year, (int64)c->money, (int64)c->current_loan, (int64)CalculateCompanyValue(c),
			c->group_all[VEH_TRAIN].num_vehicle,
			c->group_all[VEH_ROAD].num_vehicle,
			c->group_all[VEH_AIRCRAFT].num_vehicle,
			c->group_all[VEH_SHIP].num_vehicle,
			password_state);
	}

	return true;
}

DEF_CONSOLE_CMD(ConSay)
{
	if (argc == 0) {
		IConsoleHelp("Chat to your fellow players in a multiplayer game. Usage: 'say \"<msg>\"'");
		return true;
	}

	if (argc != 2) return false;

	if (!_network_server) {
		NetworkClientSendChat(NETWORK_ACTION_CHAT, DESTTYPE_BROADCAST, 0 /* param does not matter */, argv[1]);
	} else {
		bool from_admin = (_redirect_console_to_admin < INVALID_ADMIN_ID);
		NetworkServerSendChat(NETWORK_ACTION_CHAT, DESTTYPE_BROADCAST, 0, argv[1], CLIENT_ID_SERVER, from_admin);
	}

	return true;
}

DEF_CONSOLE_CMD(ConSayCompany)
{
	if (argc == 0) {
		IConsoleHelp("Chat to a certain company in a multiplayer game. Usage: 'say_company <company-no> \"<msg>\"'");
		IConsoleHelp("CompanyNo is the company that plays as company <companyno>, 1 through max_companies");
		return true;
	}

	if (argc != 3) return false;

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	if (!Company::IsValidID(company_id)) {
		IConsolePrintF(CC_DEFAULT, "Unknown company. Company range is between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	if (!_network_server) {
		NetworkClientSendChat(NETWORK_ACTION_CHAT_COMPANY, DESTTYPE_TEAM, company_id, argv[2]);
	} else {
		bool from_admin = (_redirect_console_to_admin < INVALID_ADMIN_ID);
		NetworkServerSendChat(NETWORK_ACTION_CHAT_COMPANY, DESTTYPE_TEAM, company_id, argv[2], CLIENT_ID_SERVER, from_admin);
	}

	return true;
}

DEF_CONSOLE_CMD(ConSayClient)
{
	if (argc == 0) {
		IConsoleHelp("Chat to a certain client in a multiplayer game. Usage: 'say_client <client-no> \"<msg>\"'");
		IConsoleHelp("For client-id's, see the command 'clients'");
		return true;
	}

	if (argc != 3) return false;

	if (!_network_server) {
		NetworkClientSendChat(NETWORK_ACTION_CHAT_CLIENT, DESTTYPE_CLIENT, atoi(argv[1]), argv[2]);
	} else {
		bool from_admin = (_redirect_console_to_admin < INVALID_ADMIN_ID);
		NetworkServerSendChat(NETWORK_ACTION_CHAT_CLIENT, DESTTYPE_CLIENT, atoi(argv[1]), argv[2], CLIENT_ID_SERVER, from_admin);
	}

	return true;
}

DEF_CONSOLE_CMD(ConCompanyPassword)
{
	if (argc == 0) {
		const char *helpmsg;

		if (_network_dedicated) {
			helpmsg = "Change the password of a company. Usage: 'company_pw <company-no> \"<password>\"";
		} else if (_network_server) {
			helpmsg = "Change the password of your or any other company. Usage: 'company_pw [<company-no>] \"<password>\"'";
		} else {
			helpmsg = "Change the password of your company. Usage: 'company_pw \"<password>\"'";
		}

		IConsoleHelp(helpmsg);
		IConsoleHelp("Use \"*\" to disable the password.");
		return true;
	}

	CompanyID company_id;
	const char *password;
	const char *errormsg;

	if (argc == 2) {
		company_id = _local_company;
		password = argv[1];
		errormsg = "You have to own a company to make use of this command.";
	} else if (argc == 3 && _network_server) {
		company_id = (CompanyID)(atoi(argv[1]) - 1);
		password = argv[2];
		errormsg = "You have to specify the ID of a valid human controlled company.";
	} else {
		return false;
	}

	if (!Company::IsValidHumanID(company_id)) {
		IConsoleError(errormsg);
		return false;
	}

	password = NetworkChangeCompanyPassword(company_id, password);

	if (StrEmpty(password)) {
		IConsolePrintF(CC_WARNING, "Company password cleared");
	} else {
		IConsolePrintF(CC_WARNING, "Company password changed to: %s", password);
	}

	return true;
}

DEF_CONSOLE_CMD(ConCompanyPasswordHash)
{
	if (argc == 0) {
		IConsoleHelp("Change the password hash of a company. Usage: 'company_pw_hash <company-no> \"<password_hash>\"");
		IConsoleHelp("Use \"*\" to disable the password.");
		return true;
	}

	if (argc != 3) return false;

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	const char *password = argv[2];

	if (!Company::IsValidHumanID(company_id)) {
		IConsoleError("You have to specify the ID of a valid human controlled company.");
		return false;
	}

	if (strcmp(password, "*") == 0) password = "";

	NetworkServerSetCompanyPassword(company_id, password, true);

	if (StrEmpty(password)) {
		IConsolePrintF(CC_WARNING, "Company password hash cleared");
	} else {
		IConsolePrintF(CC_WARNING, "Company password hash changed to: %s", password);
	}

	return true;
}

DEF_CONSOLE_CMD(ConCompanyPasswordHashes)
{
	if (argc == 0) {
		IConsoleHelp("List the password hashes of all companies in the game. Usage 'company_pw_hashes'");
		return true;
	}

	for (const Company *c : Company::Iterate()) {
		/* Grab the company name */
		char company_name[512];
		SetDParam(0, c->index);
		GetString(company_name, STR_COMPANY_NAME, lastof(company_name));

		char colour[512];
		GetString(colour, STR_COLOUR_DARK_BLUE + _company_colours[c->index], lastof(colour));
		IConsolePrintF(CC_INFO, "#:%d(%s) Company Name: '%s'  Hash: '%s'",
			c->index + 1, colour, company_name, _network_company_states[c->index].password);
	}

	return true;
}

/* Content downloading only is available with ZLIB */
#if defined(WITH_ZLIB)
#include "network/network_content.h"

/** Resolve a string to a content type. */
static ContentType StringToContentType(const char *str)
{
	static const char * const inv_lookup[] = { "", "base", "newgrf", "ai", "ailib", "scenario", "heightmap" };
	for (uint i = 1 /* there is no type 0 */; i < lengthof(inv_lookup); i++) {
		if (strcasecmp(str, inv_lookup[i]) == 0) return (ContentType)i;
	}
	return CONTENT_TYPE_END;
}

/** Asynchronous callback */
struct ConsoleContentCallback : public ContentCallback {
	void OnConnect(bool success)
	{
		IConsolePrintF(CC_DEFAULT, "Content server connection %s", success ? "established" : "failed");
	}

	void OnDisconnect()
	{
		IConsolePrintF(CC_DEFAULT, "Content server connection closed");
	}

	void OnDownloadComplete(ContentID cid)
	{
		IConsolePrintF(CC_DEFAULT, "Completed download of %d", cid);
	}
};

/**
 * Outputs content state information to console
 * @param ci the content info
 */
static void OutputContentState(const ContentInfo *const ci)
{
	static const char * const types[] = { "Base graphics", "NewGRF", "AI", "AI library", "Scenario", "Heightmap", "Base sound", "Base music", "Game script", "GS library" };
	static_assert(lengthof(types) == CONTENT_TYPE_END - CONTENT_TYPE_BEGIN);
	static const char * const states[] = { "Not selected", "Selected", "Dep Selected", "Installed", "Unknown" };
	static const TextColour state_to_colour[] = { CC_COMMAND, CC_INFO, CC_INFO, CC_WHITE, CC_ERROR };

	char buf[sizeof(ci->md5sum) * 2 + 1];
	md5sumToString(buf, lastof(buf), ci->md5sum);
	IConsolePrintF(state_to_colour[ci->state], "%d, %s, %s, %s, %08X, %s", ci->id, types[ci->type - 1], states[ci->state], ci->name, ci->unique_id, buf);
}

DEF_CONSOLE_CMD(ConContent)
{
	static ContentCallback *cb = nullptr;
	if (cb == nullptr) {
		cb = new ConsoleContentCallback();
		_network_content_client.AddCallback(cb);
	}

	if (argc <= 1) {
		IConsoleHelp("Query, select and download content. Usage: 'content update|upgrade|select [id]|unselect [all|id]|state [filter]|download'");
		IConsoleHelp("  update: get a new list of downloadable content; must be run first");
		IConsoleHelp("  upgrade: select all items that are upgrades");
		IConsoleHelp("  select: select a specific item given by its id. If no parameter is given, all selected content will be listed");
		IConsoleHelp("  unselect: unselect a specific item given by its id or 'all' to unselect all");
		IConsoleHelp("  state: show the download/select state of all downloadable content. Optionally give a filter string");
		IConsoleHelp("  download: download all content you've selected");
		return true;
	}

	if (strcasecmp(argv[1], "update") == 0) {
		_network_content_client.RequestContentList((argc > 2) ? StringToContentType(argv[2]) : CONTENT_TYPE_END);
		return true;
	}

	if (strcasecmp(argv[1], "upgrade") == 0) {
		_network_content_client.SelectUpgrade();
		return true;
	}

	if (strcasecmp(argv[1], "select") == 0) {
		if (argc <= 2) {
			/* List selected content */
			IConsolePrintF(CC_WHITE, "id, type, state, name");
			for (ConstContentIterator iter = _network_content_client.Begin(); iter != _network_content_client.End(); iter++) {
				if ((*iter)->state != ContentInfo::SELECTED && (*iter)->state != ContentInfo::AUTOSELECTED) continue;
				OutputContentState(*iter);
			}
		} else if (strcasecmp(argv[2], "all") == 0) {
			/* The intention of this function was that you could download
			 * everything after a filter was applied; but this never really
			 * took off. Instead, a select few people used this functionality
			 * to download every available package on BaNaNaS. This is not in
			 * the spirit of this service. Additionally, these few people were
			 * good for 70% of the consumed bandwidth of BaNaNaS. */
			IConsoleError("'select all' is no longer supported since 1.11");
		} else {
			_network_content_client.Select((ContentID)atoi(argv[2]));
		}
		return true;
	}

	if (strcasecmp(argv[1], "unselect") == 0) {
		if (argc <= 2) {
			IConsoleError("You must enter the id.");
			return false;
		}
		if (strcasecmp(argv[2], "all") == 0) {
			_network_content_client.UnselectAll();
		} else {
			_network_content_client.Unselect((ContentID)atoi(argv[2]));
		}
		return true;
	}

	if (strcasecmp(argv[1], "state") == 0) {
		IConsolePrintF(CC_WHITE, "id, type, state, name");
		for (ConstContentIterator iter = _network_content_client.Begin(); iter != _network_content_client.End(); iter++) {
			if (argc > 2 && strcasestr((*iter)->name, argv[2]) == nullptr) continue;
			OutputContentState(*iter);
		}
		return true;
	}

	if (strcasecmp(argv[1], "download") == 0) {
		uint files;
		uint bytes;
		_network_content_client.DownloadSelectedContent(files, bytes);
		IConsolePrintF(CC_DEFAULT, "Downloading %d file(s) (%d bytes)", files, bytes);
		return true;
	}

	return false;
}
#endif /* defined(WITH_ZLIB) */

DEF_CONSOLE_CMD(ConSetting)
{
	if (argc == 0) {
		IConsoleHelp("Change setting for all clients. Usage: 'setting <name> [<value>]'");
		IConsoleHelp("Omitting <value> will print out the current value of the setting.");
		return true;
	}

	if (argc == 1 || argc > 3) return false;

	if (argc == 2) {
		IConsoleGetSetting(argv[1]);
	} else {
		IConsoleSetSetting(argv[1], argv[2]);
	}

	return true;
}

DEF_CONSOLE_CMD(ConSettingNewgame)
{
	if (argc == 0) {
		IConsoleHelp("Change setting for the next game. Usage: 'setting_newgame <name> [<value>]'");
		IConsoleHelp("Omitting <value> will print out the current value of the setting.");
		return true;
	}

	if (argc == 1 || argc > 3) return false;

	if (argc == 2) {
		IConsoleGetSetting(argv[1], true);
	} else {
		IConsoleSetSetting(argv[1], argv[2], true);
	}

	return true;
}

DEF_CONSOLE_CMD(ConListSettings)
{
	if (argc == 0) {
		IConsoleHelp("List settings. Usage: 'list_settings [<pre-filter>]'");
		return true;
	}

	if (argc > 2) return false;

	IConsoleListSettings((argc == 2) ? argv[1] : nullptr);
	return true;
}

DEF_CONSOLE_CMD(ConGamelogPrint)
{
	GamelogPrintConsole();
	return true;
}

DEF_CONSOLE_CMD(ConNewGRFReload)
{
	if (argc == 0) {
		IConsoleHelp("Reloads all active NewGRFs from disk. Equivalent to reapplying NewGRFs via the settings, but without asking for confirmation. This might crash OpenTTD!");
		return true;
	}

	ReloadNewGRFData();

	extern void PostCheckNewGRFLoadWarnings();
	PostCheckNewGRFLoadWarnings();
	return true;
}

DEF_CONSOLE_CMD(ConResetBlockedHeliports)
{
	if (argc == 0) {
		IConsoleHelp("Resets heliports blocked by the improved breakdowns bug, for single-player use only.");
		return true;
	}

	unsigned int count = 0;
	for (Station *st : Station::Iterate()) {
		if (st->airport.tile == INVALID_TILE) continue;
		if (st->airport.HasHangar()) continue;
		if (!st->airport.flags) continue;

		bool occupied = false;
		for (const Aircraft *a : Aircraft::Iterate()) {
			if (a->targetairport == st->index && a->state != FLYING) {
				occupied = true;
				break;
			}
		}
		if (!occupied) {
			st->airport.flags = 0;
			count++;
			char buffer[256];
			SetDParam(0, st->index);
			GetString(buffer, STR_STATION_NAME, lastof(buffer));
			IConsolePrintF(CC_DEFAULT, "Unblocked: %s", buffer);
		}
	}

	IConsolePrintF(CC_DEFAULT, "Unblocked %u heliports", count);
	return true;
}

DEF_CONSOLE_CMD(ConMergeLinkgraphJobsAsap)
{
	if (argc == 0) {
		IConsoleHelp("Merge linkgraph jobs asap, for single-player use only.");
		return true;
	}

	for (LinkGraphJob *lgj : LinkGraphJob::Iterate()) lgj->ShiftJoinDate((((_date * DAY_TICKS) + _date_fract) - lgj->JoinDateTicks()) / DAY_TICKS);
	return true;
}

#ifdef _DEBUG
DEF_CONSOLE_CMD(ConDeleteVehicleID)
{
	if (argc == 0) {
		IConsoleHelp("Delete vehicle ID, for emergency single-player use only.");
		return true;
	}

	if (argc == 2) {
		uint32 result;
		if (GetArgumentInteger(&result, argv[1])) {
			extern void ConsoleRemoveVehicle(VehicleID id);
			ConsoleRemoveVehicle(result);
			return true;
		}
	}

	return false;
}
#endif

DEF_CONSOLE_CMD(ConGetFullDate)
{
	if (argc == 0) {
		IConsoleHelp("Returns the current full date (year-month-day, date fract, tick skip, counter) of the game. Usage: 'getfulldate'");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "Date: %04d-%02d-%02d, %i, %i", _cur_date_ymd.year, _cur_date_ymd.month + 1, _cur_date_ymd.day, _date_fract, _tick_skip_counter);
	return true;
}

DEF_CONSOLE_CMD(ConDumpCommandLog)
{
	if (argc == 0) {
		IConsoleHelp("Dump log of recently executed commands.");
		return true;
	}

	char buffer[32768];
	DumpCommandLog(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConDumpDesyncMsgLog)
{
	if (argc == 0) {
		IConsoleHelp("Dump log of desync messages.");
		return true;
	}

	char buffer[32768];
	DumpDesyncMsgLog(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConDumpInflation)
{
	if (argc == 0) {
		IConsoleHelp("Dump inflation data.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "interest_rate: %u", _economy.interest_rate);
	IConsolePrintF(CC_DEFAULT, "infl_amount: %u", _economy.infl_amount);
	IConsolePrintF(CC_DEFAULT, "infl_amount_pr: %u", _economy.infl_amount_pr);
	IConsolePrintF(CC_DEFAULT, "inflation_prices: %f", _economy.inflation_prices / 65536.0);
	IConsolePrintF(CC_DEFAULT, "inflation_payment: %f", _economy.inflation_payment / 65536.0);
	IConsolePrintF(CC_DEFAULT, "inflation ratio: %f", (double) _economy.inflation_prices / (double) _economy.inflation_payment);
	return true;
}

DEF_CONSOLE_CMD(ConDumpCpdpStats)
{
	if (argc == 0) {
		IConsoleHelp("Dump cargo packet deferred payment stats.");
		return true;
	}

	extern void DumpCargoPacketDeferredPaymentStats(char *buffer, const char *last);
	char buffer[32768];
	DumpCargoPacketDeferredPaymentStats(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConVehicleStats)
{
	if (argc == 0) {
		IConsoleHelp("Dump vehicle stats.");
		return true;
	}

	extern void DumpVehicleStats(char *buffer, const char *last);
	char buffer[32768];
	DumpVehicleStats(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConMapStats)
{
	if (argc == 0) {
		IConsoleHelp("Dump map stats.");
		return true;
	}

	extern void DumpMapStats(char *b, const char *last);
	char buffer[32768];
	DumpMapStats(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConStFlowStats)
{
	if (argc == 0) {
		IConsoleHelp("Dump station flow stats.");
		return true;
	}

	extern void DumpStationFlowStats(char *b, const char *last);
	char buffer[32768];
	DumpStationFlowStats(buffer, lastof(buffer));
	PrintLineByLine(buffer);
	return true;
}

DEF_CONSOLE_CMD(ConDumpGameEvents)
{
	if (argc == 0) {
		IConsoleHelp("Dump game events.");
		return true;
	}

	char buffer[256];
	DumpGameEventFlags(_game_events_since_load, buffer, lastof(buffer));
	IConsolePrintF(CC_DEFAULT, "Since load: %s", buffer);
	DumpGameEventFlags(_game_events_overall, buffer, lastof(buffer));
	IConsolePrintF(CC_DEFAULT, "Overall: %s", buffer);
	return true;
}

DEF_CONSOLE_CMD(ConDumpLoadDebugLog)
{
	if (argc == 0) {
		IConsoleHelp("Dump load debug log.");
		return true;
	}

	std::string dbgl = _loadgame_DBGL_data;
	PrintLineByLine(dbgl.data());
	return true;
}

DEF_CONSOLE_CMD(ConDumpLoadDebugConfig)
{
	if (argc == 0) {
		IConsoleHelp("Dump load debug config.");
		return true;
	}

	std::string dbgc = _loadgame_DBGC_data;
	PrintLineByLine(dbgc.data());
	return true;
}


DEF_CONSOLE_CMD(ConDumpLinkgraphJobs)
{
	if (argc == 0) {
		IConsoleHelp("Dump link-graph jobs.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, PRINTF_SIZE " link graph jobs", LinkGraphJob::GetNumItems());
	for (const LinkGraphJob *lgj : LinkGraphJob::Iterate()) {
		YearMonthDay start_ymd;
		ConvertDateToYMD(lgj->StartDateTicks() / DAY_TICKS, &start_ymd);
		YearMonthDay join_ymd;
		ConvertDateToYMD(lgj->JoinDateTicks() / DAY_TICKS, &join_ymd);
		IConsolePrintF(CC_DEFAULT, "  Job: %5u, nodes: %u, cost: " OTTD_PRINTF64U ", start: (%u, %4i-%02i-%02i, %i), end: (%u, %4i-%02i-%02i, %i), duration: %u",
				lgj->index, lgj->Graph().Size(), lgj->Graph().CalculateCostEstimate(),
				lgj->StartDateTicks(), start_ymd.year, start_ymd.month + 1, start_ymd.day, lgj->StartDateTicks() % DAY_TICKS,
				lgj->JoinDateTicks(), join_ymd.year, join_ymd.month + 1, join_ymd.day, lgj->JoinDateTicks() % DAY_TICKS,
				lgj->JoinDateTicks() - lgj->StartDateTicks());
	 }
	return true;
}

DEF_CONSOLE_CMD(ConDumpRoadTypes)
{
	if (argc == 0) {
		IConsoleHelp("Dump road/tram types.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "  Flags:");
	IConsolePrintF(CC_DEFAULT, "    c = catenary");
	IConsolePrintF(CC_DEFAULT, "    l = no level crossings");
	IConsolePrintF(CC_DEFAULT, "    X = no houses");
	IConsolePrintF(CC_DEFAULT, "    h = hidden");
	IConsolePrintF(CC_DEFAULT, "    T = buildable by towns");
	IConsolePrintF(CC_DEFAULT, "  Extra flags:");
	IConsolePrintF(CC_DEFAULT, "    s = not available to scripts (AI/GS)");
	IConsolePrintF(CC_DEFAULT, "    t = not modifiable by towns");

	btree::btree_map<uint32, const GRFFile *> grfs;
	for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		if (rti->label == 0) continue;
		uint32 grfid = 0;
		const GRFFile *grf = rti->grffile[ROTSG_GROUND];
		if (grf == nullptr) {
			uint32 str_grfid = GetStringGRFID(rti->strings.name);
			if (str_grfid != 0) {
				extern GRFFile *GetFileByGRFID(uint32 grfid);
				grf = GetFileByGRFID(grfid);
			}
		}
		if (grf != nullptr) {
			grfid = grf->grfid;
			grfs.insert(std::pair<uint32, const GRFFile *>(grfid, grf));
		}
		IConsolePrintF(CC_DEFAULT, "  %02u %s %c%c%c%c, Flags: %c%c%c%c%c, Extra Flags: %c%c, GRF: %08X, %s",
				(uint) rt,
				RoadTypeIsTram(rt) ? "Tram" : "Road",
				rti->label >> 24, rti->label >> 16, rti->label >> 8, rti->label,
				HasBit(rti->flags, ROTF_CATENARY)                   ? 'c' : '-',
				HasBit(rti->flags, ROTF_NO_LEVEL_CROSSING)          ? 'l' : '-',
				HasBit(rti->flags, ROTF_NO_HOUSES)                  ? 'X' : '-',
				HasBit(rti->flags, ROTF_HIDDEN)                     ? 'h' : '-',
				HasBit(rti->flags, ROTF_TOWN_BUILD)                 ? 'T' : '-',
				HasBit(rti->extra_flags, RXTF_NOT_AVAILABLE_AI_GS)  ? 's' : '-',
				HasBit(rti->extra_flags, RXTF_NO_TOWN_MODIFICATION) ? 't' : '-',
				BSWAP32(grfid),
				GetStringPtr(rti->strings.name)
		);
	}
	for (const auto &grf : grfs) {
		IConsolePrintF(CC_DEFAULT, "  GRF: %08X = %s", BSWAP32(grf.first), grf.second->filename);
	}
	return true;
}

DEF_CONSOLE_CMD(ConDumpRailTypes)
{
	if (argc == 0) {
		IConsoleHelp("Dump rail types.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "  Flags:");
	IConsolePrintF(CC_DEFAULT, "    c = catenary");
	IConsolePrintF(CC_DEFAULT, "    l = no level crossings");
	IConsolePrintF(CC_DEFAULT, "    h = hidden");
	IConsolePrintF(CC_DEFAULT, "    s = no sprite combine");
	IConsolePrintF(CC_DEFAULT, "    a = allow 90° turns");
	IConsolePrintF(CC_DEFAULT, "    d = disallow 90° turns");
	IConsolePrintF(CC_DEFAULT, "  Ctrl flags:");
	IConsolePrintF(CC_DEFAULT, "    p = signal graphics callback enabled for programmable pre-signals");
	IConsolePrintF(CC_DEFAULT, "    r = signal graphics callback restricted signal flag enabled");

	btree::btree_map<uint32, const GRFFile *> grfs;
	for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
		const RailtypeInfo *rti = GetRailTypeInfo(rt);
		if (rti->label == 0) continue;
		uint32 grfid = 0;
		const GRFFile *grf = rti->grffile[RTSG_GROUND];
		if (grf == nullptr) {
			uint32 str_grfid = GetStringGRFID(rti->strings.name);
			if (str_grfid != 0) {
				extern GRFFile *GetFileByGRFID(uint32 grfid);
				grf = GetFileByGRFID(grfid);
			}
		}
		if (grf != nullptr) {
			grfid = grf->grfid;
			grfs.insert(std::pair<uint32, const GRFFile *>(grfid, grf));
		}
		IConsolePrintF(CC_DEFAULT, "  %02u %c%c%c%c, Flags: %c%c%c%c%c%c, Ctrl Flags: %c%c%c, GRF: %08X, %s",
				(uint) rt,
				rti->label >> 24, rti->label >> 16, rti->label >> 8, rti->label,
				HasBit(rti->flags, RTF_CATENARY)            ? 'c' : '-',
				HasBit(rti->flags, RTF_NO_LEVEL_CROSSING)   ? 'l' : '-',
				HasBit(rti->flags, RTF_HIDDEN)              ? 'h' : '-',
				HasBit(rti->flags, RTF_NO_SPRITE_COMBINE)   ? 's' : '-',
				HasBit(rti->flags, RTF_ALLOW_90DEG)         ? 'a' : '-',
				HasBit(rti->flags, RTF_DISALLOW_90DEG)      ? 'd' : '-',
				HasBit(rti->ctrl_flags, RTCF_PROGSIG)       ? 'p' : '-',
				HasBit(rti->ctrl_flags, RTCF_RESTRICTEDSIG) ? 'r' : '-',
				HasBit(rti->ctrl_flags, RTCF_NOREALISTICBRAKING) ? 'b' : '-',
				BSWAP32(grfid),
				GetStringPtr(rti->strings.name)
		);
	}
	for (const auto &grf : grfs) {
		IConsolePrintF(CC_DEFAULT, "  GRF: %08X = %s", BSWAP32(grf.first), grf.second->filename);
	}
	return true;
}

DEF_CONSOLE_CMD(ConDumpBridgeTypes)
{
	if (argc == 0) {
		IConsoleHelp("Dump bridge types.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "  Ctrl flags:");
	IConsolePrintF(CC_DEFAULT, "    c = custom pillar flags");
	IConsolePrintF(CC_DEFAULT, "    i = invalid pillar flags");
	IConsolePrintF(CC_DEFAULT, "    t = not available to towns");
	IConsolePrintF(CC_DEFAULT, "    s = not available to scripts (AI/GS)");

	btree::btree_set<uint32> grfids;
	for (BridgeType bt = 0; bt < MAX_BRIDGES; bt++) {
		const BridgeSpec *spec = GetBridgeSpec(bt);
		uint32 grfid = GetStringGRFID(spec->material);
		if (grfid != 0) grfids.insert(grfid);
		IConsolePrintF(CC_DEFAULT, "  %02u Year: %7u, Min: %3u, Max: %5u, Flags: %02X, Ctrl Flags: %c%c%c%c, Pillars: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X, GRF: %08X, %s",
				(uint) bt,
				spec->avail_year,
				spec->min_length,
				spec->max_length,
				spec->flags,
				HasBit(spec->ctrl_flags, BSCF_CUSTOM_PILLAR_FLAGS) ? 'c' : '-',
				HasBit(spec->ctrl_flags, BSCF_INVALID_PILLAR_FLAGS) ? 'i' : '-',
				HasBit(spec->ctrl_flags, BSCF_NOT_AVAILABLE_TOWN) ? 't' : '-',
				HasBit(spec->ctrl_flags, BSCF_NOT_AVAILABLE_AI_GS) ? 's' : '-',
				spec->pillar_flags[0],
				spec->pillar_flags[1],
				spec->pillar_flags[2],
				spec->pillar_flags[3],
				spec->pillar_flags[4],
				spec->pillar_flags[5],
				spec->pillar_flags[6],
				spec->pillar_flags[7],
				spec->pillar_flags[8],
				spec->pillar_flags[9],
				spec->pillar_flags[10],
				spec->pillar_flags[11],
				BSWAP32(grfid),
				GetStringPtr(spec->material)
		);
	}
	for (uint32 grfid : grfids) {
		extern GRFFile *GetFileByGRFID(uint32 grfid);
		const GRFFile *grffile = GetFileByGRFID(grfid);
		IConsolePrintF(CC_DEFAULT, "  GRF: %08X = %s", BSWAP32(grfid), grffile ? grffile->filename : "????");
	}
	return true;
}

DEF_CONSOLE_CMD(ConDumpCargoTypes)
{
	if (argc == 0) {
		IConsoleHelp("Dump cargo types.");
		return true;
	}

	IConsolePrintF(CC_DEFAULT, "  Cargo classes:");
	IConsolePrintF(CC_DEFAULT, "    p = passenger");
	IConsolePrintF(CC_DEFAULT, "    m = mail");
	IConsolePrintF(CC_DEFAULT, "    x = express");
	IConsolePrintF(CC_DEFAULT, "    a = armoured");
	IConsolePrintF(CC_DEFAULT, "    b = bulk");
	IConsolePrintF(CC_DEFAULT, "    g = piece goods");
	IConsolePrintF(CC_DEFAULT, "    l = liquid");
	IConsolePrintF(CC_DEFAULT, "    r = refrigerated");
	IConsolePrintF(CC_DEFAULT, "    h = hazardous");
	IConsolePrintF(CC_DEFAULT, "    c = covered/sheltered");
	IConsolePrintF(CC_DEFAULT, "    S = special");

	btree::btree_map<uint32, const GRFFile *> grfs;
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		const CargoSpec *spec = CargoSpec::Get(i);
		if (!spec->IsValid()) continue;
		uint32 grfid = 0;
		const GRFFile *grf = spec->grffile;
		if (grf == nullptr) {
			uint32 str_grfid = GetStringGRFID(spec->name);
			if (str_grfid != 0) {
				extern GRFFile *GetFileByGRFID(uint32 grfid);
				grf = GetFileByGRFID(grfid);
			}
		}
		if (grf != nullptr) {
			grfid = grf->grfid;
			grfs.insert(std::pair<uint32, const GRFFile *>(grfid, grf));
		}
		IConsolePrintF(CC_DEFAULT, "  %02u Bit: %2u, Label: %c%c%c%c, Callback mask: 0x%02X, Cargo class: %c%c%c%c%c%c%c%c%c%c%c, GRF: %08X, %s",
				(uint) i,
				spec->bitnum,
				spec->label >> 24, spec->label >> 16, spec->label >> 8, spec->label,
				spec->callback_mask,
				(spec->classes & CC_PASSENGERS)   != 0 ? 'p' : '-',
				(spec->classes & CC_MAIL)         != 0 ? 'm' : '-',
				(spec->classes & CC_EXPRESS)      != 0 ? 'x' : '-',
				(spec->classes & CC_ARMOURED)     != 0 ? 'a' : '-',
				(spec->classes & CC_BULK)         != 0 ? 'b' : '-',
				(spec->classes & CC_PIECE_GOODS)  != 0 ? 'g' : '-',
				(spec->classes & CC_LIQUID)       != 0 ? 'l' : '-',
				(spec->classes & CC_REFRIGERATED) != 0 ? 'r' : '-',
				(spec->classes & CC_HAZARDOUS)    != 0 ? 'h' : '-',
				(spec->classes & CC_COVERED)      != 0 ? 'c' : '-',
				(spec->classes & CC_SPECIAL)      != 0 ? 'S' : '-',
				BSWAP32(grfid),
				GetStringPtr(spec->name)
		);
	}
	for (const auto &grf : grfs) {
		IConsolePrintF(CC_DEFAULT, "  GRF: %08X = %s", BSWAP32(grf.first), grf.second->filename);
	}
	return true;
}

/**
 * Dump the state of a tile on the map.
 * param x tile number or tile x coordinate.
 * param y optional y coordinate.
 * @note When only one argument is given it is interpreted as the tile number.
 *       When two arguments are given, they are interpreted as the tile's x
 *       and y coordinates.
 * @return True when either console help was shown or a proper amount of parameters given.
 */
DEF_CONSOLE_CMD(ConDumpTile)
{
	char buffer[128];

	switch (argc) {
		case 0:
			IConsoleHelp("Dump the map state of a given tile.");
			IConsoleHelp("Usage: 'dump_tile <tile>' or 'dump_tile <x> <y>'");
			IConsoleHelp("Numbers can be either decimal (34161) or hexadecimal (0x4a5B).");
			return true;

		case 2: {
			uint32 result;
			if (GetArgumentInteger(&result, argv[1])) {
				if (result >= MapSize()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				DumpTileInfo(buffer, lastof(buffer), (TileIndex)result);
				IConsolePrintF(CC_DEFAULT, "  %s", buffer);
				return true;
			}
			break;
		}

		case 3: {
			uint32 x, y;
			if (GetArgumentInteger(&x, argv[1]) && GetArgumentInteger(&y, argv[2])) {
				if (x >= MapSizeX() || y >= MapSizeY()) {
					IConsolePrint(CC_ERROR, "Tile does not exist");
					return true;
				}
				DumpTileInfo(buffer, lastof(buffer), TileXY(x, y));
				IConsolePrintF(CC_DEFAULT, "  %s", buffer);
				return true;
			}
			break;
		}
	}

	return false;
}

DEF_CONSOLE_CMD(ConCheckCaches)
{
	if (argc == 0) {
		IConsoleHelp("Debug: Check caches");
		return true;
	}

	if (argc > 2) return false;

	bool broadcast = (argc == 2 && atoi(argv[1]) > 0 && (!_networking || _network_server));
	if (broadcast) {
		DoCommandP(0, 0, 0, CMD_DESYNC_CHECK);
	} else {
		extern void CheckCaches(bool force_check, std::function<void(const char *)> log);
		CheckCaches(true, nullptr);
	}

	return true;
}

DEF_CONSOLE_CMD(ConShowTownWindow)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Show town window.  Usage: 'show_town_window <town-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL && _game_mode != GM_EDITOR) {
		return true;
	}

	TownID town_id = (TownID)(atoi(argv[1]));
	if (!Town::IsValidID(town_id)) {
		return true;
	}

	ShowTownViewWindow(town_id);

	return true;
}

DEF_CONSOLE_CMD(ConShowStationWindow)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Show station window.  Usage: 'show_station_window <station-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL && _game_mode != GM_EDITOR) {
		return true;
	}

	const BaseStation *bst = BaseStation::GetIfValid(atoi(argv[1]));
	if (bst == nullptr) return true;
	if (bst->facilities & FACIL_WAYPOINT) {
		ShowWaypointWindow(Waypoint::From(bst));
	} else {
		ShowStationViewWindow(bst->index);
	}

	return true;
}

DEF_CONSOLE_CMD(ConShowIndustryWindow)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Show industry window.  Usage: 'show_industry_window <industry-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL && _game_mode != GM_EDITOR) {
		return true;
	}

	IndustryID ind_id = (IndustryID)(atoi(argv[1]));
	if (!Industry::IsValidID(ind_id)) {
		return true;
	}

	extern void ShowIndustryViewWindow(int industry);
	ShowIndustryViewWindow(ind_id);

	return true;
}

DEF_CONSOLE_CMD(ConViewportDebug)
{
	if (argc < 1 || argc > 2) {
		IConsoleHelp("Debug: viewports flags.  Usage: 'viewport_debug [<flags>]'");
		IConsoleHelp("   1: VDF_DIRTY_BLOCK_PER_DRAW");
		IConsoleHelp("   2: VDF_DIRTY_WHOLE_VIEWPORT");
		IConsoleHelp("   4: VDF_DIRTY_BLOCK_PER_SPLIT");
		IConsoleHelp("   8: VDF_DISABLE_DRAW_SPLIT");
		IConsoleHelp("  10: VDF_SHOW_NO_LANDSCAPE_MAP_DRAW");
		IConsoleHelp("  20: VDF_DISABLE_LANDSCAPE_CACHE");
		return true;
	}

	extern uint32 _viewport_debug_flags;
	if (argc == 1) {
		IConsolePrintF(CC_DEFAULT, "Viewport debug flags: %X", _viewport_debug_flags);
	} else {
		_viewport_debug_flags = strtoul(argv[1], nullptr, 16);
	}

	return true;
}

DEF_CONSOLE_CMD(ConViewportMarkDirty)
{
	if (argc < 3 || argc > 5) {
		IConsoleHelp("Debug: Mark main viewport dirty.  Usage: 'viewport_mark_dirty <x> <y> [<w> <h>]'");
		return true;
	}

	Viewport *vp = FindWindowByClass(WC_MAIN_WINDOW)->viewport;
	uint l = strtoul(argv[1], nullptr, 0);
	uint t = strtoul(argv[2], nullptr, 0);
	uint r = std::min<uint>(l + ((argc > 3) ? strtoul(argv[3], nullptr, 0) : 1), vp->dirty_blocks_per_row);
	uint b = std::min<uint>(t + ((argc > 4) ? strtoul(argv[4], nullptr, 0) : 1), vp->dirty_blocks_per_column);
	for (uint x = l; x < r; x++) {
		for (uint y = t; y < b; y++) {
			vp->dirty_blocks[(x * vp->dirty_blocks_per_column) + y] = true;
		}
	}
	vp->is_dirty = true;

	return true;
}


DEF_CONSOLE_CMD(ConViewportMarkStationOverlayDirty)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Mark main viewport link graph overlay station links.  Usage: 'viewport_mark_dirty_st_overlay <station-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL && _game_mode != GM_EDITOR) {
		return true;
	}

	const Station *st = Station::GetIfValid(atoi(argv[1]));
	if (st == nullptr) return true;
	MarkAllViewportOverlayStationLinksDirty(st);

	return true;
}

DEF_CONSOLE_CMD(ConGfxDebug)
{
	if (argc < 1 || argc > 2) {
		IConsoleHelp("Debug: gfx flags.  Usage: 'gfx_debug [<flags>]'");
		IConsoleHelp("  1: GDF_SHOW_WINDOW_DIRTY");
		IConsoleHelp("  2: GDF_SHOW_WIDGET_DIRTY");
		IConsoleHelp("  4: GDF_SHOW_RECT_DIRTY");
		return true;
	}

	extern uint32 _gfx_debug_flags;
	if (argc == 1) {
		IConsolePrintF(CC_DEFAULT, "Gfx debug flags: %X", _gfx_debug_flags);
	} else {
		_gfx_debug_flags = strtoul(argv[1], nullptr, 16);
	}

	return true;
}

DEF_CONSOLE_CMD(ConCSleep)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Sleep.  Usage: 'csleep <milliseconds>'");
		return true;
	}

	CSleep(atoi(argv[1]));

	return true;
}

DEF_CONSOLE_CMD(ConRecalculateRoadCachedOneWayStates)
{
	if (argc == 0) {
		IConsoleHelp("Debug: Recalculate road cached one way states");
		return true;
	}

	extern void RecalculateRoadCachedOneWayStates();
	RecalculateRoadCachedOneWayStates();

	return true;
}

DEF_CONSOLE_CMD(ConMiscDebug)
{
	if (argc < 1 || argc > 2) {
		IConsoleHelp("Debug: misc flags.  Usage: 'misc_debug [<flags>]'");
		IConsoleHelp("  1: MDF_OVERHEAT_BREAKDOWN_OPEN_WIN");
		IConsoleHelp("  2: MDF_ZONING_RS_WATER_FLOOD_STATE");
		return true;
	}

	if (argc == 1) {
		IConsolePrintF(CC_DEFAULT, "Misc debug flags: %X", _misc_debug_flags);
	} else {
		_misc_debug_flags = strtoul(argv[1], nullptr, 16);
	}

	return true;
}

DEF_CONSOLE_CMD(ConDoDisaster)
{
	if (argc == 0) {
		IConsoleHelp("Debug: Do disaster");
		return true;
	}

	extern void DoDisaster();
	DoDisaster();

	return true;
}

DEF_CONSOLE_CMD(ConBankruptCompany)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Mark company as bankrupt.  Usage: 'bankrupt_company <company-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL) {
		IConsoleWarning("Companies can only be managed in a game.");
		return true;
	}

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	if (!Company::IsValidID(company_id)) {
		IConsolePrintF(CC_DEFAULT, "Unknown company. Company range is between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	Company *c = Company::Get(company_id);
	c->bankrupt_value = 42;
	c->bankrupt_asked = 1 << c->index; // Don't ask the owner
	c->bankrupt_timeout = 0;
	c->money = INT64_MIN / 2;
	IConsolePrint(CC_DEFAULT, "Company marked as bankrupt.");

	return true;
}

DEF_CONSOLE_CMD(ConDeleteCompany)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Delete company.  Usage: 'delete_company <company-id>'");
		return true;
	}

	if (_game_mode != GM_NORMAL) {
		IConsoleWarning("Companies can only be managed in a game.");
		return true;
	}

	CompanyID company_id = (CompanyID)(atoi(argv[1]) - 1);
	if (!Company::IsValidID(company_id)) {
		IConsolePrintF(CC_DEFAULT, "Unknown company. Company range is between 1 and %d.", MAX_COMPANIES);
		return true;
	}

	if (company_id == _local_company) {
		IConsoleWarning("Cannot delete current company.");
		return true;
	}

	DoCommandP(0, CCA_DELETE | company_id << 16 | CRR_MANUAL << 24, 0, CMD_COMPANY_CTRL);
	IConsolePrint(CC_DEFAULT, "Company deleted.");

	return true;
}

DEF_CONSOLE_CMD(ConNewGRFProfile)
{
	if (argc == 0) {
		IConsoleHelp("Collect performance data about NewGRF sprite requests and callbacks. Sub-commands can be abbreviated.");
		IConsoleHelp("Usage: newgrf_profile [list]");
		IConsoleHelp("  List all NewGRFs that can be profiled, and their status.");
		IConsoleHelp("Usage: newgrf_profile select <grf-num>...");
		IConsoleHelp("  Select one or more GRFs for profiling.");
		IConsoleHelp("Usage: newgrf_profile unselect <grf-num>...");
		IConsoleHelp("  Unselect one or more GRFs from profiling. Use the keyword \"all\" instead of a GRF number to unselect all. Removing an active profiler aborts data collection.");
		IConsoleHelp("Usage: newgrf_profile start [<num-days>]");
		IConsoleHelp("  Begin profiling all selected GRFs. If a number of days is provided, profiling stops after that many in-game days.");
		IConsoleHelp("Usage: newgrf_profile stop");
		IConsoleHelp("  End profiling and write the collected data to CSV files.");
		IConsoleHelp("Usage: newgrf_profile abort");
		IConsoleHelp("  End profiling and discard all collected data.");
		return true;
	}

	extern const std::vector<GRFFile *> &GetAllGRFFiles();
	const std::vector<GRFFile *> &files = GetAllGRFFiles();

	/* "list" sub-command */
	if (argc == 1 || strncasecmp(argv[1], "lis", 3) == 0) {
		IConsolePrint(CC_INFO, "Loaded GRF files:");
		int i = 1;
		for (GRFFile *grf : files) {
			auto profiler = std::find_if(_newgrf_profilers.begin(), _newgrf_profilers.end(), [&](NewGRFProfiler &pr) { return pr.grffile == grf; });
			bool selected = profiler != _newgrf_profilers.end();
			bool active = selected && profiler->active;
			TextColour tc = active ? TC_LIGHT_BLUE : selected ? TC_GREEN : CC_INFO;
			const char *statustext = active ? " (active)" : selected ? " (selected)" : "";
			IConsolePrintF(tc, "%d: [%08X] %s%s", i, BSWAP32(grf->grfid), grf->filename, statustext);
			i++;
		}
		return true;
	}

	/* "select" sub-command */
	if (strncasecmp(argv[1], "sel", 3) == 0 && argc >= 3) {
		for (size_t argnum = 2; argnum < argc; ++argnum) {
			int grfnum = atoi(argv[argnum]);
			if (grfnum < 1 || grfnum > (int)files.size()) { // safe cast, files.size() should not be larger than a few hundred in the most extreme cases
				IConsolePrintF(CC_WARNING, "GRF number %d out of range, not added.", grfnum);
				continue;
			}
			GRFFile *grf = files[grfnum - 1];
			if (std::any_of(_newgrf_profilers.begin(), _newgrf_profilers.end(), [&](NewGRFProfiler &pr) { return pr.grffile == grf; })) {
				IConsolePrintF(CC_WARNING, "GRF number %d [%08X] is already selected for profiling.", grfnum, BSWAP32(grf->grfid));
				continue;
			}
			_newgrf_profilers.emplace_back(grf);
		}
		return true;
	}

	/* "unselect" sub-command */
	if (strncasecmp(argv[1], "uns", 3) == 0 && argc >= 3) {
		for (size_t argnum = 2; argnum < argc; ++argnum) {
			if (strcasecmp(argv[argnum], "all") == 0) {
				_newgrf_profilers.clear();
				break;
			}
			int grfnum = atoi(argv[argnum]);
			if (grfnum < 1 || grfnum > (int)files.size()) {
				IConsolePrintF(CC_WARNING, "GRF number %d out of range, not removing.", grfnum);
				continue;
			}
			GRFFile *grf = files[grfnum - 1];
			auto pos = std::find_if(_newgrf_profilers.begin(), _newgrf_profilers.end(), [&](NewGRFProfiler &pr) { return pr.grffile == grf; });
			if (pos != _newgrf_profilers.end()) _newgrf_profilers.erase(pos);
		}
		return true;
	}

	/* "start" sub-command */
	if (strncasecmp(argv[1], "sta", 3) == 0) {
		std::string grfids;
		size_t started = 0;
		for (NewGRFProfiler &pr : _newgrf_profilers) {
			if (!pr.active) {
				pr.Start();
				started++;

				if (!grfids.empty()) grfids += ", ";
				char grfidstr[12]{ 0 };
				seprintf(grfidstr, lastof(grfidstr), "[%08X]", BSWAP32(pr.grffile->grfid));
				grfids += grfidstr;
			}
		}
		if (started > 0) {
			IConsolePrintF(CC_DEBUG, "Started profiling for GRFID%s %s", (started > 1) ? "s" : "", grfids.c_str());
			if (argc >= 3) {
				int days = std::max(atoi(argv[2]), 1);
				_newgrf_profile_end_date = _date + days;

				char datestrbuf[32]{ 0 };
				SetDParam(0, _newgrf_profile_end_date);
				GetString(datestrbuf, STR_JUST_DATE_ISO, lastof(datestrbuf));
				IConsolePrintF(CC_DEBUG, "Profiling will automatically stop on game date %s", datestrbuf);
			} else {
				_newgrf_profile_end_date = MAX_DAY;
			}
		} else if (_newgrf_profilers.empty()) {
			IConsolePrintF(CC_WARNING, "No GRFs selected for profiling, did not start.");
		} else {
			IConsolePrintF(CC_WARNING, "Did not start profiling for any GRFs, all selected GRFs are already profiling.");
		}
		return true;
	}

	/* "stop" sub-command */
	if (strncasecmp(argv[1], "sto", 3) == 0) {
		NewGRFProfiler::FinishAll();
		return true;
	}

	/* "abort" sub-command */
	if (strncasecmp(argv[1], "abo", 3) == 0) {
		for (NewGRFProfiler &pr : _newgrf_profilers) {
			pr.Abort();
		}
		_newgrf_profile_end_date = MAX_DAY;
		return true;
	}

	return false;
}

DEF_CONSOLE_CMD(ConRoadTypeFlagCtl)
{
	if (argc != 3) {
		IConsoleHelp("Debug: Road/tram type flag control.");
		return true;
	}

	RoadType rt = (RoadType)atoi(argv[1]);
	uint flag = atoi(argv[2]);

	if (rt >= ROADTYPE_END) return true;
	extern RoadTypeInfo _roadtypes[ROADTYPE_END];

	if (flag >= 100) {
		ToggleBit(_roadtypes[rt].extra_flags, flag - 100);
	} else {
		ToggleBit(_roadtypes[rt].flags, flag);
	}

	return true;
}

DEF_CONSOLE_CMD(ConRailTypeMapColourCtl)
{
	if (argc != 3) {
		IConsoleHelp("Debug: Rail type map colour control.");
		return true;
	}

	RailType rt = (RailType)atoi(argv[1]);
	uint8 map_colour = atoi(argv[2]);

	if (rt >= RAILTYPE_END) return true;
	extern RailtypeInfo _railtypes[RAILTYPE_END];

	_railtypes[rt].map_colour = map_colour;
	MarkAllViewportMapLandscapesDirty();

	return true;
}

DEF_CONSOLE_CMD(ConSwitchBaseset)
{
	if (argc != 2) {
		IConsoleHelp("Debug: Try to switch baseset and reload NewGRFs. Usage: 'switch_baseset <baseset-name>'");
		return true;
	}

	for (int i = 0; i < BaseGraphics::GetNumSets(); i++) {
		const GraphicsSet *basegfx = BaseGraphics::GetSet(i);
		if (argv[1] == basegfx->name) {
			extern std::string _switch_baseset;
			_switch_baseset = basegfx->name;
			_check_special_modes = true;
			return true;
		}
	}

	IConsolePrintF(CC_WARNING, "No such baseset: %s.", argv[1]);
	return 1;
}

static bool ConConditionalCommon(byte argc, char *argv[], int value, const char *value_name, const char *name)
{
	if (argc < 4) {
		IConsolePrintF(CC_WARNING, "- Execute command if %s is within the specified range. Usage: '%s <minimum> <maximum> <command...>'", value_name, name);
		return true;
	}

	int min_value = atoi(argv[1]);
	int max_value = atoi(argv[2]);

	if (value >= min_value && value <= max_value) IConsoleCmdExecTokens(argc - 3, argv + 3);

	return true;
}

DEF_CONSOLE_CMD(ConIfYear)
{
	return ConConditionalCommon(argc, argv, _cur_date_ymd.year, "the current year (in game)", "if_year");
}

DEF_CONSOLE_CMD(ConIfMonth)
{
	return ConConditionalCommon(argc, argv, _cur_date_ymd.month + 1, "the current month (in game)", "if_month");
}

DEF_CONSOLE_CMD(ConIfDay)
{
	return ConConditionalCommon(argc, argv, _cur_date_ymd.day, "the current day of the month (in game)", "if_day");
}

DEF_CONSOLE_CMD(ConIfHour)
{
	Minutes minutes = _scaled_date_ticks / _settings_time.ticks_per_minute + _settings_time.clock_offset;
	return ConConditionalCommon(argc, argv,  MINUTES_HOUR(minutes), "the current hour (in game, assuming time is in minutes)", "if_hour");
}

DEF_CONSOLE_CMD(ConIfMinute)
{
	Minutes minutes = _scaled_date_ticks / _settings_time.ticks_per_minute + _settings_time.clock_offset;
	return ConConditionalCommon(argc, argv, MINUTES_MINUTE(minutes), "the current minute (in game, assuming time is in minutes)", "if_minute");
}

DEF_CONSOLE_CMD(ConIfHourMinute)
{
	Minutes minutes = _scaled_date_ticks / _settings_time.ticks_per_minute + _settings_time.clock_offset;
	return ConConditionalCommon(argc, argv, (MINUTES_HOUR(minutes) * 100) + MINUTES_MINUTE(minutes), "the current hour and minute 0000 - 2359 (in game, assuming time is in minutes)", "if_hour_minute");
}

#ifdef _DEBUG
/******************
 *  debug commands
 ******************/

static void IConsoleDebugLibRegister()
{
	IConsole::CmdRegister("resettile",        ConResetTile);
	IConsole::AliasRegister("dbg_echo",       "echo %A; echo %B");
	IConsole::AliasRegister("dbg_echo2",      "echo %!");
}
#endif

DEF_CONSOLE_CMD(ConFramerate)
{
	extern void ConPrintFramerate(); // framerate_gui.cpp

	if (argc == 0) {
		IConsoleHelp("Show frame rate and game speed information");
		return true;
	}

	ConPrintFramerate();
	return true;
}

DEF_CONSOLE_CMD(ConFramerateWindow)
{
	extern void ShowFramerateWindow();

	if (argc == 0) {
		IConsoleHelp("Open the frame rate window");
		return true;
	}

	if (_network_dedicated) {
		IConsoleError("Can not open frame rate window on a dedicated server");
		return false;
	}

	ShowFramerateWindow();
	return true;
}

DEF_CONSOLE_CMD(ConFindNonRealisticBrakingSignal)
{
	if (argc == 0) {
		IConsoleHelp("Find next signal tile which prevents enabling of realitic braking");
		return true;
	}

	for (TileIndex t = 0; t < MapSize(); t++) {
		if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RAIL_TILE_SIGNALS) {
			uint signals = GetPresentSignals(t);
			if ((signals & 0x3) & ((signals & 0x3) - 1) || (signals & 0xC) & ((signals & 0xC) - 1)) {
				/* Signals in both directions */
				ScrollMainWindowToTile(t);
				SetRedErrorSquare(t);
				return true;
			}
			if (((signals & 0x3) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_LOWER))) ||
					((signals & 0xC) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_UPPER)))) {
				/* Banned signal types present */
				ScrollMainWindowToTile(t);
				SetRedErrorSquare(t);
				return true;
			}
		}
	}

	return true;
}

DEF_CONSOLE_CMD(ConDumpInfo)
{
	if (argc != 2) {
		IConsoleHelp("Dump debugging information.");
		IConsoleHelp("Usage: dump_info roadtypes|railtypes|cargotypes");
		IConsoleHelp("  Show information about road/tram types, rail types or cargo types.");
		return true;
	}

	if (strcasecmp(argv[1], "roadtypes") == 0) {
		ConDumpRoadTypes(argc, argv);
		return true;
	}

	if (strcasecmp(argv[1], "railtypes") == 0) {
		ConDumpRailTypes(argc, argv);
		return true;
	}

	if (strcasecmp(argv[1], "cargotypes") == 0) {
		ConDumpCargoTypes(argc, argv);
		return true;
	}

	return false;
}

/*******************************
 * console command registration
 *******************************/

void IConsoleStdLibRegister()
{
	IConsole::CmdRegister("debug_level",             ConDebugLevel);
	IConsole::CmdRegister("echo",                    ConEcho);
	IConsole::CmdRegister("echoc",                   ConEchoC);
	IConsole::CmdRegister("exec",                    ConExec);
	IConsole::CmdRegister("exit",                    ConExit);
	IConsole::CmdRegister("part",                    ConPart);
	IConsole::CmdRegister("help",                    ConHelp);
	IConsole::CmdRegister("info_cmd",                ConInfoCmd);
	IConsole::CmdRegister("list_cmds",               ConListCommands);
	IConsole::CmdRegister("list_aliases",            ConListAliases);
	IConsole::CmdRegister("newgame",                 ConNewGame);
	IConsole::CmdRegister("restart",                 ConRestart);
	IConsole::CmdRegister("reload",                  ConReload);
	IConsole::CmdRegister("getseed",                 ConGetSeed);
	IConsole::CmdRegister("getdate",                 ConGetDate);
	IConsole::CmdRegister("getsysdate",              ConGetSysDate);
	IConsole::CmdRegister("quit",                    ConExit);
	IConsole::CmdRegister("resetengines",            ConResetEngines,     ConHookNoNetwork);
	IConsole::CmdRegister("reset_enginepool",        ConResetEnginePool,  ConHookNoNetwork);
	IConsole::CmdRegister("return",                  ConReturn);
	IConsole::CmdRegister("screenshot",              ConScreenShot);
	IConsole::CmdRegister("minimap",                 ConMinimap);
	IConsole::CmdRegister("script",                  ConScript);
	IConsole::CmdRegister("scrollto",                ConScrollToTile);
	IConsole::CmdRegister("highlight_tile",          ConHighlightTile);
	IConsole::AliasRegister("scrollto_highlight",    "scrollto %+; highlight_tile %+");
	IConsole::CmdRegister("alias",                   ConAlias);
	IConsole::CmdRegister("load",                    ConLoad);
	IConsole::CmdRegister("rm",                      ConRemove);
	IConsole::CmdRegister("save",                    ConSave);
	IConsole::CmdRegister("saveconfig",              ConSaveConfig);
	IConsole::CmdRegister("ls",                      ConListFiles);
	IConsole::CmdRegister("cd",                      ConChangeDirectory);
	IConsole::CmdRegister("pwd",                     ConPrintWorkingDirectory);
	IConsole::CmdRegister("clear",                   ConClearBuffer);
	IConsole::CmdRegister("setting",                 ConSetting);
	IConsole::CmdRegister("setting_newgame",         ConSettingNewgame);
	IConsole::CmdRegister("list_settings",           ConListSettings);
	IConsole::CmdRegister("gamelog",                 ConGamelogPrint);
	IConsole::CmdRegister("rescan_newgrf",           ConRescanNewGRF);

	IConsole::AliasRegister("dir",                   "ls");
	IConsole::AliasRegister("del",                   "rm %+");
	IConsole::AliasRegister("newmap",                "newgame");
	IConsole::AliasRegister("patch",                 "setting %+");
	IConsole::AliasRegister("set",                   "setting %+");
	IConsole::AliasRegister("set_newgame",           "setting_newgame %+");
	IConsole::AliasRegister("list_patches",          "list_settings %+");
	IConsole::AliasRegister("developer",             "setting developer %+");

	IConsole::CmdRegister("list_ai_libs",            ConListAILibs);
	IConsole::CmdRegister("list_ai",                 ConListAI);
	IConsole::CmdRegister("reload_ai",               ConReloadAI);
	IConsole::CmdRegister("rescan_ai",               ConRescanAI);
	IConsole::CmdRegister("start_ai",                ConStartAI);
	IConsole::CmdRegister("stop_ai",                 ConStopAI);

	IConsole::CmdRegister("list_game",               ConListGame);
	IConsole::CmdRegister("list_game_libs",          ConListGameLibs);
	IConsole::CmdRegister("rescan_game",             ConRescanGame);

	IConsole::CmdRegister("companies",               ConCompanies);
	IConsole::AliasRegister("players",               "companies");

	/* networking functions */

/* Content downloading is only available with ZLIB */
#if defined(WITH_ZLIB)
	IConsole::CmdRegister("content",                 ConContent);
#endif /* defined(WITH_ZLIB) */

	/*** Networking commands ***/
	IConsole::CmdRegister("say",                     ConSay,              ConHookNeedNetwork);
	IConsole::CmdRegister("say_company",             ConSayCompany,       ConHookNeedNetwork);
	IConsole::AliasRegister("say_player",            "say_company %+");
	IConsole::CmdRegister("say_client",              ConSayClient,        ConHookNeedNetwork);

	IConsole::CmdRegister("connect",                 ConNetworkConnect,   ConHookClientOnly);
	IConsole::CmdRegister("clients",                 ConNetworkClients,   ConHookNeedNetwork);
	IConsole::CmdRegister("status",                  ConStatus,           ConHookServerOnly);
	IConsole::CmdRegister("server_info",             ConServerInfo,       ConHookServerOnly);
	IConsole::AliasRegister("info",                  "server_info");
	IConsole::CmdRegister("reconnect",               ConNetworkReconnect, ConHookClientOnly);
	IConsole::CmdRegister("rcon",                    ConRcon,             ConHookNeedNetwork);
	IConsole::CmdRegister("settings_access",         ConSettingsAccess,   ConHookNeedNetwork);

	IConsole::CmdRegister("join",                    ConJoinCompany,      ConHookNeedNetwork);
	IConsole::AliasRegister("spectate",              "join 255");
	IConsole::CmdRegister("move",                    ConMoveClient,       ConHookServerOnly);
	IConsole::CmdRegister("reset_company",           ConResetCompany,     ConHookServerOnly);
	IConsole::AliasRegister("clean_company",         "reset_company %A");
	IConsole::CmdRegister("client_name",             ConClientNickChange, ConHookServerOnly);
	IConsole::CmdRegister("kick",                    ConKick,             ConHookServerOnly);
	IConsole::CmdRegister("ban",                     ConBan,              ConHookServerOnly);
	IConsole::CmdRegister("unban",                   ConUnBan,            ConHookServerOnly);
	IConsole::CmdRegister("banlist",                 ConBanList,          ConHookServerOnly);

	IConsole::CmdRegister("pause",                   ConPauseGame,        ConHookServerOnly);
	IConsole::CmdRegister("unpause",                 ConUnpauseGame,      ConHookServerOnly);

	IConsole::CmdRegister("company_pw",              ConCompanyPassword,  ConHookNeedNetwork);
	IConsole::AliasRegister("company_password",      "company_pw %+");
	IConsole::CmdRegister("company_pw_hash",         ConCompanyPasswordHash, ConHookServerOnly);
	IConsole::AliasRegister("company_password_hash", "company_pw %+");
	IConsole::CmdRegister("company_pw_hashes",       ConCompanyPasswordHashes, ConHookServerOnly);
	IConsole::AliasRegister("company_password_hashes", "company_pw_hashes");

	IConsole::AliasRegister("net_frame_freq",        "setting frame_freq %+");
	IConsole::AliasRegister("net_sync_freq",         "setting sync_freq %+");
	IConsole::AliasRegister("server_pw",             "setting server_password %+");
	IConsole::AliasRegister("server_password",       "setting server_password %+");
	IConsole::AliasRegister("rcon_pw",               "setting rcon_password %+");
	IConsole::AliasRegister("rcon_password",         "setting rcon_password %+");
	IConsole::AliasRegister("settings_pw",           "setting settings_password %+");
	IConsole::AliasRegister("settings_password",     "setting settings_password %+");
	IConsole::AliasRegister("name",                  "setting client_name %+");
	IConsole::AliasRegister("server_name",           "setting server_name %+");
	IConsole::AliasRegister("server_port",           "setting server_port %+");
	IConsole::AliasRegister("server_advertise",      "setting server_advertise %+");
	IConsole::AliasRegister("max_clients",           "setting max_clients %+");
	IConsole::AliasRegister("max_companies",         "setting max_companies %+");
	IConsole::AliasRegister("max_spectators",        "setting max_spectators %+");
	IConsole::AliasRegister("max_join_time",         "setting max_join_time %+");
	IConsole::AliasRegister("pause_on_join",         "setting pause_on_join %+");
	IConsole::AliasRegister("autoclean_companies",   "setting autoclean_companies %+");
	IConsole::AliasRegister("autoclean_protected",   "setting autoclean_protected %+");
	IConsole::AliasRegister("autoclean_unprotected", "setting autoclean_unprotected %+");
	IConsole::AliasRegister("restart_game_year",     "setting restart_game_year %+");
	IConsole::AliasRegister("min_players",           "setting min_active_clients %+");
	IConsole::AliasRegister("reload_cfg",            "setting reload_cfg %+");

	/* conditionals */
	IConsole::CmdRegister("if_year",                 ConIfYear);
	IConsole::CmdRegister("if_month",                ConIfMonth);
	IConsole::CmdRegister("if_day",                  ConIfDay);
	IConsole::CmdRegister("if_hour",                 ConIfHour);
	IConsole::CmdRegister("if_minute",               ConIfMinute);
	IConsole::CmdRegister("if_hour_minute",          ConIfHourMinute);

	/* debugging stuff */
#ifdef _DEBUG
	IConsoleDebugLibRegister();
#endif
	IConsole::CmdRegister("fps",                     ConFramerate);
	IConsole::CmdRegister("fps_wnd",                 ConFramerateWindow);

	IConsole::CmdRegister("find_non_realistic_braking_signal", ConFindNonRealisticBrakingSignal);

	IConsole::CmdRegister("getfulldate",             ConGetFullDate,      nullptr, true);
	IConsole::CmdRegister("dump_command_log",        ConDumpCommandLog,   nullptr, true);
	IConsole::CmdRegister("dump_desync_msgs",        ConDumpDesyncMsgLog, nullptr, true);
	IConsole::CmdRegister("dump_inflation",          ConDumpInflation,    nullptr, true);
	IConsole::CmdRegister("dump_cpdp_stats",         ConDumpCpdpStats,    nullptr, true);
	IConsole::CmdRegister("dump_veh_stats",          ConVehicleStats,     nullptr, true);
	IConsole::CmdRegister("dump_map_stats",          ConMapStats,         nullptr, true);
	IConsole::CmdRegister("dump_st_flow_stats",      ConStFlowStats,      nullptr, true);
	IConsole::CmdRegister("dump_game_events",        ConDumpGameEvents,   nullptr, true);
	IConsole::CmdRegister("dump_load_debug_log",     ConDumpLoadDebugLog, nullptr, true);
	IConsole::CmdRegister("dump_load_debug_config",  ConDumpLoadDebugConfig, nullptr, true);
	IConsole::CmdRegister("dump_linkgraph_jobs",     ConDumpLinkgraphJobs, nullptr, true);
	IConsole::CmdRegister("dump_road_types",         ConDumpRoadTypes,    nullptr, true);
	IConsole::CmdRegister("dump_rail_types",         ConDumpRailTypes,    nullptr, true);
	IConsole::CmdRegister("dump_bridge_types",       ConDumpBridgeTypes,  nullptr, true);
	IConsole::CmdRegister("dump_cargo_types",        ConDumpCargoTypes,   nullptr, true);
	IConsole::CmdRegister("dump_tile",               ConDumpTile,         nullptr, true);
	IConsole::CmdRegister("check_caches",            ConCheckCaches,      nullptr, true);
	IConsole::CmdRegister("show_town_window",        ConShowTownWindow,   nullptr, true);
	IConsole::CmdRegister("show_station_window",     ConShowStationWindow, nullptr, true);
	IConsole::CmdRegister("show_industry_window",    ConShowIndustryWindow, nullptr, true);
	IConsole::CmdRegister("viewport_debug",          ConViewportDebug,    nullptr, true);
	IConsole::CmdRegister("viewport_mark_dirty",     ConViewportMarkDirty, nullptr, true);
	IConsole::CmdRegister("viewport_mark_dirty_st_overlay", ConViewportMarkStationOverlayDirty, nullptr, true);
	IConsole::CmdRegister("gfx_debug",               ConGfxDebug,         nullptr, true);
	IConsole::CmdRegister("csleep",                  ConCSleep,           nullptr, true);
	IConsole::CmdRegister("recalculate_road_cached_one_way_states", ConRecalculateRoadCachedOneWayStates, ConHookNoNetwork, true);
	IConsole::CmdRegister("misc_debug",              ConMiscDebug,        nullptr, true);

	/* NewGRF development stuff */
	IConsole::CmdRegister("reload_newgrfs",          ConNewGRFReload,     ConHookNewGRFDeveloperTool);
	IConsole::CmdRegister("newgrf_profile",          ConNewGRFProfile,    ConHookNewGRFDeveloperTool);
	IConsole::CmdRegister("dump_info",               ConDumpInfo);
	IConsole::CmdRegister("do_disaster",             ConDoDisaster,       ConHookNewGRFDeveloperTool, true);
	IConsole::CmdRegister("bankrupt_company",        ConBankruptCompany,  ConHookNewGRFDeveloperTool, true);
	IConsole::CmdRegister("delete_company",          ConDeleteCompany,    ConHookNewGRFDeveloperTool, true);
	IConsole::CmdRegister("road_type_flag_ctl",      ConRoadTypeFlagCtl,  ConHookNewGRFDeveloperTool, true);
	IConsole::CmdRegister("rail_type_map_colour_ctl", ConRailTypeMapColourCtl, ConHookNewGRFDeveloperTool, true);
	IConsole::CmdRegister("switch_baseset",          ConSwitchBaseset,    ConHookNewGRFDeveloperTool, true);

	/* Bug workarounds */
	IConsole::CmdRegister("jgrpp_bug_workaround_unblock_heliports", ConResetBlockedHeliports, ConHookNoNetwork, true);
	IConsole::CmdRegister("merge_linkgraph_jobs_asap", ConMergeLinkgraphJobsAsap, ConHookNoNetwork, true);

#ifdef _DEBUG
	IConsole::CmdRegister("delete_vehicle_id",       ConDeleteVehicleID,  ConHookNoNetwork, true);
#endif
}
