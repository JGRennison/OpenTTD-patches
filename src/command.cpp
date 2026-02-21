/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command.cpp Handling of commands. */

#include "stdafx.h"
#include "landscape.h"
#include "error.h"
#include "gui.h"
#include "command_func.h"
#include "command_settings_type.h"
#include "command_table.h"
#include "network/network_type.h"
#include "network/network.h"
#include "genworld.h"
#include "strings_func.h"
#include "texteff.hpp"
#include "town.h"
#include "date_func.h"
#include "company_func.h"
#include "company_base.h"
#include "signal_func.h"
#include "core/backup_type.hpp"
#include "object_base.h"
#include "newgrf_text.h"
#include "string_func.h"
#include "scope.h"
#include "scope_info.h"
#include "core/random_func.hpp"
#include "settings_func.h"
#include "signal_func.h"
#include "debug_settings.h"
#include "debug_desync.h"
#include "order_backup.h"
#include "core/checksum_func.hpp"
#include "3rdparty/cpp-ring-buffer/ring_buffer.hpp"
#include <array>

#include "table/strings.h"

#include "safeguards.h"

ClientID _cmd_client_id = INVALID_CLIENT_ID;

/**
 * List of flags for a command log entry
 */
enum CommandLogEntryFlag : uint16_t {
	CLEF_NONE                =  0x00, ///< no flag is set
	CLEF_CMD_FAILED          =  0x01, ///< command failed
	CLEF_GENERATING_WORLD    =  0x02, ///< generating world
	CLEF_NETWORK             =  0x04, ///< network command
	CLEF_ESTIMATE_ONLY       =  0x08, ///< estimate only
	CLEF_ONLY_SENDING        =  0x10, ///< only sending
	CLEF_MY_CMD              =  0x20, ///< locally generated command
	CLEF_SCRIPT              =  0x40, ///< command run by AI/game script
	CLEF_SCRIPT_ASYNC        =  0x80, ///< command run by AI/game script - asynchronous
	CLEF_TWICE               = 0x100, ///< command logged twice (only sending and execution)
	CLEF_RANDOM              = 0x200, ///< command changed random seed
	CLEF_ORDER_BACKUP        = 0x400, ///< command changed order backups
};
DECLARE_ENUM_AS_BIT_SET(CommandLogEntryFlag)

extern uint32_t _frame_counter;

struct CommandLogEntry {
	EconTime::Date date;
	EconTime::DateFract date_fract;
	uint8_t tick_skip_counter;
	uint32_t frame_counter;

	CompanyID current_company;
	CompanyID local_company;
	ClientID client_id;

	CommandLogEntryFlag log_flags;

	Commands cmd;
	TileIndex tile;
	std::string summary;

	CommandLogEntry() { }

	CommandLogEntry(TileIndex tile, Commands cmd, CommandLogEntryFlag log_flags, std::string &&summary) :
			date(EconTime::CurDate()), date_fract(EconTime::CurDateFract()), tick_skip_counter(TickSkipCounter()), frame_counter(_frame_counter),
			current_company(_current_company), local_company(_local_company), client_id(_cmd_client_id),
			log_flags(log_flags),
			cmd(cmd), tile(tile), summary(std::move(summary)) {}
};

struct CommandLog {
	std::array<CommandLogEntry, 256> log;
	unsigned int count = 0;
	unsigned int next = 0;

	void Reset()
	{
		this->count = 0;
		this->next = 0;
	}
};

static CommandLog _command_log;
static CommandLog _command_log_aux;

struct CommandQueueItem {
	DynCommandContainer cmd;
	CompanyID company;
	DoCommandIntlFlag intl_flags;
};
static jgr::ring_buffer<CommandQueueItem> _command_queue;

void ClearCommandLog()
{
	_command_log.Reset();
	_command_log_aux.Reset();
}

static void DumpSubCommandLogEntry(format_target &buffer, const CommandLogEntry &entry)
{
	const CommandInfo &cmd_info = _command_proc_table[entry.cmd];

	auto fc = [&](CommandLogEntryFlag flag, char c) -> char {
		return entry.log_flags & flag ? c : '-';
	};

	auto script_fc = [&]() -> char {
		if (!(entry.log_flags & CLEF_SCRIPT)) return '-';
		return (entry.log_flags & CLEF_SCRIPT_ASYNC) ? 'A' : 'a';
	};

	EconTime::YearMonthDay ymd = EconTime::ConvertDateToYMD(entry.date);
	buffer.format("{:4}-{:02}-{:02}, {:2}, {:3}", ymd.year.base(), ymd.month + 1, ymd.day, entry.date_fract, entry.tick_skip_counter);
	if (_networking) {
		buffer.format(", {:08X}", entry.frame_counter);
	}
	buffer.format(" | {}{}{}{}{}{}{}{}{}{} | ",
			fc(CLEF_ORDER_BACKUP, 'o'), fc(CLEF_RANDOM, 'r'), fc(CLEF_TWICE, '2'),
			script_fc(), fc(CLEF_MY_CMD, 'm'), fc(CLEF_ONLY_SENDING, 's'),
			fc(CLEF_ESTIMATE_ONLY, 'e'), fc(CLEF_NETWORK, 'n'), fc(CLEF_GENERATING_WORLD, 'g'), fc(CLEF_CMD_FAILED, 'f')
			);
	buffer.format("cc: {:3}, lc: {:3}", entry.current_company, entry.local_company);
	if (_network_server) {
		buffer.format(", client: {:4}", entry.client_id);
	}
	if (entry.tile != 0 || !(cmd_info.intl_flags & CIF_NO_OUTPUT_TILE)) {
		buffer.format(" | {:{}} x {:{}} | ", TileX(entry.tile), Map::DigitsX(), TileY(entry.tile), Map::DigitsY());
	} else {
		buffer.format(" |{:{}}| ", "", Map::DigitsX() + Map::DigitsY() + 5);
	}
	buffer.format("cmd: {:03X} {:<34} |", entry.cmd, cmd_info.name);

	if (!entry.summary.empty()) {
		buffer.push_back(' ');
		buffer.append(entry.summary);
	}
}

static void DumpSubCommandLog(format_target &buffer, const CommandLog &cmd_log, const unsigned int count)
{
	unsigned int log_index = cmd_log.next;
	for (unsigned int i = 0 ; i < count; i++) {
		if (log_index > 0) {
			log_index--;
		} else {
			log_index = (uint)cmd_log.log.size() - 1;
		}

		buffer.format(" {:3} | ", i);

		const CommandLogEntry &entry = cmd_log.log[log_index];
		DumpSubCommandLogEntry(buffer, entry);

		buffer.push_back('\n');
	}
}

void DumpCommandLog(format_target &buffer)
{
	const unsigned int count = std::min<unsigned int>(_command_log.count, 256);
	buffer.format("Command Log:\n Showing most recent {} of {} commands\n", count, _command_log.count);
	DumpSubCommandLog(buffer, _command_log, count);

	if (_command_log_aux.count > 0) {
		const unsigned int aux_count = std::min<unsigned int>(_command_log_aux.count, 32);
		buffer.format("\n Showing most recent {} of {} commands (aux log)\n", aux_count, _command_log_aux.count);
		DumpSubCommandLog(buffer, _command_log_aux, aux_count);
	}
}

/**
 * This returns the flags which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The flags for this command
 */
CommandFlags GetCommandFlags(Commands cmd)
{
	assert(IsValidCommand(cmd));

	return _command_proc_table[cmd].flags;
}

/**
 * This returns the name which belongs to the given command.
 *
 * @param cmd The integer value of the command
 * @return The name for this command
 */
std::string_view GetCommandName(Commands cmd)
{
	if (!IsValidCommand(cmd)) return "????"; // This can be reached in error/crash log paths when IsValidCommand checks fail

	return _command_proc_table[cmd].name;
}

/**
 * Returns whether the command is allowed while the game is paused.
 * @param cmd The command to check.
 * @return True if the command is allowed while paused, false otherwise.
 */
bool IsCommandAllowedWhilePaused(Commands cmd)
{
	/* Lookup table for the command types that are allowed for a given pause level setting. */
	static constexpr CommandPauseLevel command_type_lookup[] = {
		CommandPauseLevel::AllActions, // CommandType::LandscapeConstruction
		CommandPauseLevel::NoLandscaping, // CommandType::VehicleConstruction
		CommandPauseLevel::NoLandscaping, // CommandType::MoneyManagement
		CommandPauseLevel::NoConstruction, // CommandType::VehicleManagement
		CommandPauseLevel::NoConstruction, // CommandType::RouteManagement
		CommandPauseLevel::NoConstruction, // CommandType::OtherManagement
		CommandPauseLevel::NoActions, // CommandType::CompanySetting
		CommandPauseLevel::NoActions, // CommandType::ServerSetting
		CommandPauseLevel::NoActions, // CommandType::Cheat
	};
	static_assert(std::size(command_type_lookup) == to_underlying(CommandType::End));

	assert(IsValidCommand(cmd));
	return _game_mode == GM_EDITOR || command_type_lookup[to_underlying(_command_proc_table[cmd].type)] <= _settings_game.construction.command_pause_level;
}

bool IsCorrectCommandPayloadType(Commands cmd, const CommandPayloadBase &payload)
{
	assert(IsValidCommand(cmd));
	return typeid(payload) == _command_proc_table[cmd].payload_type_info;
}

static int _docommand_recursive = 0;

/**
 * This function executes a given command with the parameters from the command payload.
 * Depending on the flags parameter it executes or tests a command.
 *
 * @param cmd The command-id to execute (a value of the CMD_* enums)
 * @param tile The tile to apply the command on
 * @param payload Command payload
 * @param flags Flags for the command and how to execute the command
 * @param intl_flags Internal flags for the command and how to execute the command

 * @return the cost
 */
CommandCost DoCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, DoCommandFlags flags, DoCommandIntlFlag intl_flags)
{
#if !defined(DISABLE_SCOPE_INFO)
	FunctorScopeStackRecord scope_print([=, &payload](format_target &output) {
		output.format("DoCommand: tile: {}, flags: 0x{:X}, intl_flags: 0x{:X}, company: {}, cmd: 0x{:X} {}, payload: ",
				tile, flags, intl_flags, CompanyInfoDumper(_current_company), cmd, GetCommandName(cmd));
		payload.FormatDebugSummary(output);
	});
#endif

	assert(IsValidCommand(cmd));

	if ((intl_flags & DCIF_TYPE_CHECKED) == 0) {
		if (!IsCorrectCommandPayloadType(cmd, payload)) return CMD_ERROR;
		intl_flags |= DCIF_TYPE_CHECKED;
	}

	CommandCost res;

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= Map::Size() || (!IsValidTile(tile) && !flags.Test(DoCommandFlag::AllTiles)))) return CMD_ERROR;

	const CommandInfo &command = _command_proc_table[cmd];

	_docommand_recursive++;

	/* only execute the test call if it's toplevel, or we're not execing. */
	if (_docommand_recursive == 1 || !flags.Test(DoCommandFlag::Execute)) {
		if (_docommand_recursive == 1) _cleared_object_areas.clear();
		SetTownRatingTestMode(true);
		res = command.exec({ tile, DoCommandFlags{flags}.Reset(DoCommandFlag::Execute), payload });
		SetTownRatingTestMode(false);
		if (res.Failed()) {
			_docommand_recursive--;
			return res;
		}

		if (_docommand_recursive == 1 &&
				!flags.Test(DoCommandFlag::QueryCost) &&
				!flags.Test(DoCommandFlag::Bankrupt) &&
				!CheckCompanyHasMoney(res)) { // CheckCompanyHasMoney() modifies 'res' to an error if it fails.
			_docommand_recursive--;
			return res;
		}

		if (!flags.Test(DoCommandFlag::Execute)) {
			_docommand_recursive--;
			return res;
		}
	}

	/* Execute the command here. All cost-relevant functions set the expenses type
	 * themselves to the cost object at some point */
	if (_docommand_recursive == 1) _cleared_object_areas.clear();
	res = command.exec({ tile, flags, payload });
	if (res.Failed()) {
		_docommand_recursive--;
		return res;
	}

	/* if toplevel, subtract the money. */
	if (--_docommand_recursive == 0 && !flags.Test(DoCommandFlag::Bankrupt)) {
		SubtractMoneyFromCompany(res);
	}

	return res;
}

static void DebugLogCommandLogEntry(const CommandLogEntry &entry)
{
	if (GetDebugLevel(DebugLevelID::command) <= 0) return;

	format_buffer buffer;
	DumpSubCommandLogEntry(buffer, entry);
	debug_print(DebugLevelID::command, 1, buffer);
}

static void AppendCommandLogEntry(const CommandCost &res, TileIndex tile, Commands cmd, CommandLogEntryFlag log_flags, const CommandPayloadBase &payload)
{
	if (res.Failed()) log_flags |= CLEF_CMD_FAILED;
	if (_generating_world) log_flags |= CLEF_GENERATING_WORLD;

	CommandLog &cmd_log = GetCommandFlags(cmd).Test(CommandFlag::LogAux) ? _command_log_aux : _command_log;

	format_buffer summary;
	payload.FormatDebugSummary(summary);
	if (res.HasAnyResultData()) {
		summary.format(" --> {}", res.GetUntypedResultData());
	}

	if (_networking && cmd_log.count > 0) {
		CommandLogEntry &current = cmd_log.log[(cmd_log.next - 1) % cmd_log.log.size()];
		if (current.log_flags & CLEF_ONLY_SENDING &&
				current.tile == tile &&
				current.cmd == cmd &&
				((current.log_flags ^ log_flags) & ~(CLEF_SCRIPT | CLEF_MY_CMD | CLEF_NETWORK)) == CLEF_ONLY_SENDING &&
				current.date == EconTime::CurDate() && current.date_fract == EconTime::CurDateFract() &&
				current.tick_skip_counter == TickSkipCounter() &&
				current.frame_counter == _frame_counter &&
				current.current_company == _current_company &&
				current.local_company == _local_company &&
				current.summary == (std::string_view)summary) {
			current.log_flags |= log_flags | CLEF_TWICE;
			current.log_flags &= ~CLEF_ONLY_SENDING;
			DebugLogCommandLogEntry(current);
			return;
		}
	}

	cmd_log.log[cmd_log.next] = CommandLogEntry(tile, cmd, log_flags, summary.to_string());
	DebugLogCommandLogEntry(cmd_log.log[cmd_log.next]);
	cmd_log.next = (cmd_log.next + 1) % cmd_log.log.size();
	cmd_log.count++;
}

/**
 * Toplevel network safe docommand function for the current company. Must not be called recursively.
 * The callback is called when the command succeeded or failed.
 *
 * @param cmd The command-id to execute (a value of the CMD_* enums)
 * @param tile The tile to apply the command on
 * @param orig_payload Command payload
 * @param error_msg Error message string ID
 * @param callback A callback function to call after the command is finished
 * @param callback_param An arbitrary parameter associated with the callback
 * @param intl_flags Internal flags for the command and how to execute the command
 *
 * @return \c true if the command succeeded, else \c false.
 */
bool DoCommandPImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &orig_payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags)
{
#if !defined(DISABLE_SCOPE_INFO)
	FunctorScopeStackRecord scope_print([=, &orig_payload](format_target &output) {
		output.format("DoCommandP: tile: {}, intl_flags: 0x{:X}, company: {}, cmd: 0x{:X} {}, payload: ",
				tile, intl_flags, CompanyInfoDumper(_current_company), cmd, GetCommandName(cmd));
		orig_payload.FormatDebugSummary(output);
	});
#endif

	assert(IsValidCommand(cmd));

	if ((intl_flags & DCIF_TYPE_CHECKED) == 0) {
		if (!IsCorrectCommandPayloadType(cmd, orig_payload)) return false;
		intl_flags |= DCIF_TYPE_CHECKED;
	}

	/* Cost estimation is generally only done when the
	 * local user presses shift while doing something.
	 * However, in case of incoming network commands,
	 * map generation or the pause button we do want
	 * to execute. */
	bool estimate_only = _shift_pressed && IsLocalCompany() &&
			!_generating_world &&
			!(intl_flags & DCIF_NETWORK_COMMAND) &&
			!(intl_flags & DCIF_NO_ESTIMATE) &&
			!GetCommandFlags(cmd).Test(CommandFlag::NoEst);

	/* We're only sending the command, so don't do
	 * fancy things for 'success'. */
	bool only_sending = _networking && !(intl_flags & DCIF_NETWORK_COMMAND);

	/* Where to show the message? */

	int x = TileX(tile) * TILE_SIZE;
	int y = TileY(tile) * TILE_SIZE;

	if (_pause_mode.Any() && !IsCommandAllowedWhilePaused(cmd) && !estimate_only) {
		ShowErrorMessage(GetEncodedString(error_msg), GetEncodedString(STR_ERROR_NOT_ALLOWED_WHILE_PAUSED), WL_INFO, x, y);
		return false;
	}

	std::unique_ptr<CommandPayloadBase> modified_payload;
	const CommandPayloadBase *use_payload = &orig_payload;

	/* Only set client ID when the command does not come from the network. */
	if (!(intl_flags & DCIF_NETWORK_COMMAND) && GetCommandFlags(cmd).Test(CommandFlag::ClientID)) {
		modified_payload = orig_payload.Clone();
		assert(IsCorrectCommandPayloadType(cmd, *modified_payload));
		SetPreCheckedCommandPayloadClientID(cmd, *modified_payload, CLIENT_ID_SERVER);
		use_payload = modified_payload.get();
	}

	GameRandomSeedChecker random_state;
	uint order_backup_update_counter = OrderBackup::GetUpdateCounter();

	CommandCost res = DoCommandPInternal(cmd, tile, *use_payload, error_msg, callback, callback_param, intl_flags, estimate_only);

	CommandLogEntryFlag log_flags;
	log_flags = CLEF_NONE;
	if (estimate_only) log_flags |= CLEF_ESTIMATE_ONLY;
	if (only_sending) log_flags |= CLEF_ONLY_SENDING;
	if (!(intl_flags & DCIF_NOT_MY_CMD)) log_flags |= CLEF_MY_CMD;
	if (!random_state.Check()) log_flags |= CLEF_RANDOM;
	if (order_backup_update_counter != OrderBackup::GetUpdateCounter()) log_flags |= CLEF_ORDER_BACKUP;
	if (intl_flags & DCIF_NETWORK_COMMAND) log_flags |= CLEF_NETWORK;
	AppendCommandLogEntry(res, tile, cmd, log_flags, *use_payload);

	if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_POST_COMMAND)) && !GetCommandFlags(cmd).Test(CommandFlag::LogAux)) {
		CheckCachesFlags flags = CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG;
		if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		CheckCaches(true, nullptr, flags);
	}

	if (res.Failed()) {
		/* Only show the error when it's for us. */
		if (estimate_only || (IsLocalCompany() && error_msg != 0 && !(intl_flags & DCIF_NOT_MY_CMD))) {
			ShowErrorMessage(GetEncodedString(error_msg), x, y, res);
		}
	} else if (estimate_only) {
		ShowEstimatedCostOrIncome(res.GetCost(), x, y);
	} else if (!only_sending && tile != 0 && IsLocalCompany() && _game_mode != GM_EDITOR && HasBit(_extra_display_opt, XDO_SHOW_MONEY_TEXT_EFFECTS)) {
		/* Only show the cost animation when we did actually
		 * execute the command, i.e. we're not sending it to
		 * the server, when it has cost the local company
		 * something. Furthermore in the editor there is no
		 * concept of cost, so don't show it there either. */
		ShowCostOrIncomeAnimation(x, y, GetSlopePixelZ(x, y), res.GetCost());
	}

	if (!estimate_only && !only_sending && callback != CommandCallback::None) {
		extern void ExecuteCommandCallback(CommandCallback callback, CallbackParameter callback_param, const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload);
		ExecuteCommandCallback(callback, callback_param, res, cmd, tile, *use_payload);
	}

	return res.Succeeded();
}

CommandCost DoCommandPScript(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only, bool asynchronous)
{
	GameRandomSeedChecker random_state;
	uint order_backup_update_counter = OrderBackup::GetUpdateCounter();

	CommandCost res = DoCommandPInternal(cmd, tile, payload, static_cast<StringID>(0), callback, callback_param, intl_flags | DCIF_NOT_MY_CMD, estimate_only);

	CommandLogEntryFlag log_flags;
	log_flags = CLEF_SCRIPT;
	if (asynchronous) log_flags |= CLEF_SCRIPT_ASYNC;
	if (estimate_only) log_flags |= CLEF_ESTIMATE_ONLY;
	if (_networking) log_flags |= CLEF_ONLY_SENDING;
	if (!random_state.Check()) log_flags |= CLEF_RANDOM;
	if (order_backup_update_counter != OrderBackup::GetUpdateCounter()) log_flags |= CLEF_ORDER_BACKUP;
	AppendCommandLogEntry(res, tile, cmd, log_flags, payload);

	if (unlikely(HasChickenBit(DCBF_DESYNC_CHECK_POST_COMMAND)) && !GetCommandFlags(cmd).Test(CommandFlag::LogAux)) {
		CheckCachesFlags flags = CHECK_CACHE_ALL | CHECK_CACHE_EMIT_LOG;
		if (HasChickenBit(DCBF_DESYNC_CHECK_NO_GENERAL)) flags &= ~CHECK_CACHE_GENERAL;
		CheckCaches(true, nullptr, flags);
	}

	return res;
}

void ExecuteCommandQueue()
{
	while (!_command_queue.empty()) {
		if (_network_client_commands_sent >= 2) break; // Too many network client commands sent this tick already
		Backup<CompanyID> cur_company(_current_company, FILE_LINE);
		cur_company.Change(_command_queue.front().company);
		DoCommandPContainer(_command_queue.front().cmd, _command_queue.front().intl_flags);
		cur_company.Restore();
		_command_queue.pop_front();
	}
}

void ClearCommandQueue()
{
	_command_queue.clear();
}

void EnqueueDoCommandPImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags)
{
	/* Do not execute immediately if we are already in DoCommand context or there have already been multiple client commands sent this tick. */
	if (_docommand_recursive == 0 || _network_client_commands_sent >= 2) {
		DoCommandPImplementation(cmd, tile, payload, error_msg, callback, callback_param, intl_flags);
	} else {
		CommandQueueItem &item = _command_queue.emplace_back();
		item.cmd = DynCommandContainer(cmd, error_msg, tile, payload.Clone(), callback, callback_param);
		item.company = _current_company;
		item.intl_flags = intl_flags;
	}
}


/**
 * Helper function for the toplevel network safe docommand function for the current company.
 *
 * @param cmd The command to execute (a CMD_* value)
 * @param tile The tile to perform a command on
 * @param payload Command payload
 * @param error_msg Error message string
 * @param callback A callback function to call after the command is finished
 * @param intl_flags Internal flags
 * @param estimate_only whether to give only the estimate or also execute the command
 * @return the command cost of this function.
 */
CommandCost DoCommandPInternal(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, DoCommandIntlFlag intl_flags, bool estimate_only)
{
	/* Prevent recursion; it gives a mess over the network */
	assert(_docommand_recursive == 0);
	_docommand_recursive = 1;
	auto guard = scope_guard([]() {
		_docommand_recursive = 0;
	});

	assert(IsValidCommand(cmd));

	/* Get pointer to command handler */
	const CommandInfo &command = _command_proc_table[cmd];
	/* Shouldn't happen, but you never know when someone adds
	 * NULLs to the _command_proc_table. */
	assert(command.exec != nullptr);

	if ((intl_flags & DCIF_TYPE_CHECKED) == 0) {
		if (!IsCorrectCommandPayloadType(cmd, payload)) return CMD_ERROR;
		intl_flags |= DCIF_TYPE_CHECKED;
	}

	/* Command flags are used internally */
	CommandFlags cmd_flags = GetCommandFlags(cmd);
	/* Flags get send to the DoCommand */
	DoCommandFlags flags = CommandFlagsToDCFlags(cmd_flags);

	/* Do not even think about executing out-of-bounds tile-commands */
	if (tile != 0 && (tile >= Map::Size() || (!IsValidTile(tile) && !cmd_flags.Test(CommandFlag::AllTiles)))) return CMD_ERROR;

	/* Always execute server and spectator commands as spectator */
	bool exec_as_spectator = cmd_flags.Any({CommandFlag::Spectator, CommandFlag::Server});

	/* If the company isn't valid it may only do server command or start a new company!
	 * The server will ditch any server commands a client sends to it, so effectively
	 * this guards the server from executing functions for an invalid company. */
	if (_game_mode == GM_NORMAL && !exec_as_spectator && !Company::IsValidID(_current_company) && !(_current_company == OWNER_DEITY && cmd_flags.Test(CommandFlag::Deity))) {
		return CMD_ERROR;
	}

	Backup<CompanyID> cur_company(_current_company, FILE_LINE);
	if (exec_as_spectator) cur_company.Change(COMPANY_SPECTATOR);

	bool test_and_exec_can_differ = cmd_flags.Test(CommandFlag::NoTest) || HasChickenBit(DCBF_CMD_NO_TEST_ALL);

	GameRandomSeedChecker random_state;

	/* Test the command. */
	_cleared_object_areas.clear();
	SetTownRatingTestMode(true);
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_TESTMODE);
	CommandCost res = command.exec({ tile, flags, payload });
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_TESTMODE);
	SetTownRatingTestMode(false);

	if (!random_state.Check()) {
		format_buffer buffer;
		buffer.format("Random seed changed in test command: company: {:02x}; tile: {:06x} ({} x {}); cmd: {:03x}; {}; payload: ",
				_current_company, tile, TileX(tile), TileY(tile), cmd, GetCommandName(cmd));
		payload.FormatDebugSummary(buffer);
		Debug(desync, 0, "msg: {}; {}", debug_date_dumper().HexDate(), buffer);
		LogDesyncMsg(buffer.to_string());
	}

	/* Make sure we're not messing things up here. */
	assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());

	auto log_desync_cmd = [&](const char *prefix) {
		if (GetDebugLevel(DebugLevelID::desync) >= 1) {
			std::string aux_str;
			{
				std::vector<uint8_t> buffer;
				BufferSerialisationRef serialiser(buffer, SHRT_MAX);
				payload.Serialise(serialiser);
				aux_str = FormatArrayAsHex(buffer, false);
			}

			Debug(desync, 1, "{}: {}; company: {:02x}; tile: {:06x} ({} x {}); cmd: {:03x}; <{}> ({})",
					prefix, debug_date_dumper().HexDate(), _current_company, tile, TileX(tile), TileY(tile),
					cmd, aux_str, GetCommandName(cmd));
		}
	};

	/* If the command fails, we're doing an estimate
	 * or the player does not have enough money
	 * (unless it's a command where the test and
	 * execution phase might return different costs)
	 * we bail out here. */
	if (res.Failed() || estimate_only ||
			(!test_and_exec_can_differ && !CheckCompanyHasMoney(res))) {
		if (!_networking || _generating_world || (intl_flags & DCIF_NETWORK_COMMAND) != 0) {
			/* Log the failed command as well. Just to be able to be find
			 * causes of desyncs due to bad command test implementations. */
			log_desync_cmd("cmdf");
		}
		cur_company.Restore();
		return res;
	}

	/*
	 * If we are in network, and the command is not from the network
	 * send it to the command-queue and abort execution
	 */
	if (_networking && !_generating_world && !(intl_flags & DCIF_NETWORK_COMMAND)) {
		/* Payload is already checked as being of the correct type */
		extern void NetworkSendCommandImplementation(Commands cmd, TileIndex tile, const CommandPayloadBase &payload, StringID error_msg, CommandCallback callback, CallbackParameter callback_param, CompanyID company);
		NetworkSendCommandImplementation(cmd, tile, payload, error_msg, callback, callback_param, _current_company);
		cur_company.Restore();

		/* Don't return anything special here; no error, no costs.
		 * This way it's not handled by DoCommand and only the
		 * actual execution of the command causes messages. Also
		 * reset the storages as we've not executed the command. */
		return CommandCost();
	}
	log_desync_cmd("cmd");

	/* Actually try and execute the command. If no cost-type is given
	 * use the construction one */
	_cleared_object_areas.clear();
	BasePersistentStorageArray::SwitchMode(PSM_ENTER_COMMAND);
	CommandCost res2 = command.exec({ tile, flags | DoCommandFlag::Execute, payload });
	BasePersistentStorageArray::SwitchMode(PSM_LEAVE_COMMAND);

	if (cmd == CMD_COMPANY_CTRL) {
		cur_company.Trash();
		/* We are a new company                  -> Switch to new local company.
		 * We were closed down                   -> Switch to spectator
		 * Some other company opened/closed down -> The outside function will switch back */
		_current_company = _local_company;
	} else {
		/* Make sure nothing bad happened, like changing the current company. */
		assert(exec_as_spectator ? _current_company == COMPANY_SPECTATOR : cur_company.Verify());
		cur_company.Restore();
	}

	/* If the test and execution can differ we have to check the
	 * return of the command. Otherwise we can check whether the
	 * test and execution have yielded the same result,
	 * i.e. cost and error state are the same. */
	if (!test_and_exec_can_differ) {
		assert_msg(res.GetCost() == res2.GetCost() && res.Failed() == res2.Failed(),
				"Command: cmd: 0x{:X} ({}), Test: {}, Exec: {}", cmd, GetCommandName(cmd),
				res.SummaryMessage(error_msg), res2.SummaryMessage(error_msg)); // sanity check
	} else if (res2.Failed()) {
		return res2;
	}

	/* If we're needing more money and we haven't done
	 * anything yet, ask for the money! */
	if (res2.GetAdditionalCashRequired() != 0 && res2.GetCost() == 0) {
		/* It could happen we removed rail, thus gained money, and deleted something else.
		 * So make sure the signal buffer is empty even in this case */
		UpdateSignalsInBuffer();
		if (_extra_aspects > 0) FlushDeferredAspectUpdates();
		return CommandCostWithParam(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY, res2.GetAdditionalCashRequired());
	}

	/* update last build coordinate of company. */
	if (tile != 0) {
		Company *c = Company::GetIfValid(_current_company);
		if (c != nullptr) c->last_build_coordinate = tile;
	}

	SubtractMoneyFromCompany(res2);
	if (_networking) UpdateStateChecksum(res2.GetCost());

	/* update signals if needed */
	UpdateSignalsInBuffer();
	if (_extra_aspects > 0) FlushDeferredAspectUpdates();

	/* Record if there was a command issues during pause; ignore pause/other setting related changes. */
	if (_pause_mode.Any() && command.type != CommandType::ServerSetting) _pause_mode.Set(PauseMode::CommandDuringPause);

	return res2;
}

CommandLargeResultBase::~CommandLargeResultBase() {}

CommandCost::CommandCost(const CommandCost &other)
{
	*this = other;
}

CommandCost &CommandCost::operator=(const CommandCost &other)
{
	this->cost = other.cost;
	this->expense_type = other.expense_type;
	this->flags = other.flags;
	this->owner = other.owner;
	this->message = other.message;
	if (other.GetInlineType() == CommandCostInlineType::AuxiliaryData) {
		this->inl.aux_data = new CommandCostAuxiliaryData(*other.inl.aux_data);
	} else {
		this->inl = other.inl;
	}
	return *this;
}

/**
 * Set the encoded message string. If set, this is used by the error message window instead of the error StringID,
 * to allow more information to be displayed to the local player.
 * @note Do not set an encoded message if the error is not for the local player, as it will never be seen.
 * @param message EncodedString message to set.
 */
void CommandCost::SetEncodedMessage(EncodedString &&message)
{
	if (this->GetInlineType() != CommandCostInlineType::AuxiliaryData) {
		this->AllocAuxData();
	}

	this->inl.aux_data->encoded_message = std::move(message);
}

/**
 * Get the last encoded error message.
 * @returns Reference to the encoded message.
 */
EncodedString &CommandCost::GetEncodedMessage()
{
	static EncodedString empty;

	if (this->GetInlineType() == CommandCostInlineType::AuxiliaryData) {
		return this->inl.aux_data->encoded_message;
	} else {
		empty.clear();
		return empty;
	}
}

/**
 * Adds the cost of the given command return value to this cost.
 * Also takes a possible error message when it is set.
 * @param ret The command to add the cost of.
 */
void CommandCost::AddCost(CommandCost &&ret)
{
	this->AddCost(ret.cost);
	if (this->Succeeded() && !ret.Succeeded()) {
		this->message = ret.message;
		this->flags &= ~CCIF_SUCCESS;
		if (ret.GetInlineType() == CommandCostInlineType::AuxiliaryData && !ret.inl.aux_data->encoded_message.empty()) {
			this->SetEncodedMessage(std::move(ret.inl.aux_data->encoded_message));
		}
	}
}

std::string CommandCost::SummaryMessage(StringID cmd_msg) const
{
	if (this->Succeeded()) {
		return fmt::format("Success: cost: {}", (int64_t) this->GetCost());
	} else {
		format_buffer buf;
		buf.format("Failed: cost: {}", (int64_t) this->GetCost());
		if (cmd_msg != 0) {
			buf.push_back(' ');
			AppendStringInPlace(buf, cmd_msg);
		}
		if (this->message != INVALID_STRING_ID) {
			buf.push_back(' ');
			AppendStringInPlace(buf, this->message);
		}

		return buf.to_string();
	}
}

void CommandCost::AllocAuxData()
{
	CommandCostAuxiliaryData *aux_data = new CommandCostAuxiliaryData();
	switch (this->GetInlineType()) {
		case CommandCostInlineType::None:
			break;

		case CommandCostInlineType::AuxiliaryData:
			NOT_REACHED();

		case CommandCostInlineType::ExtraMsg:
			aux_data->extra_message = this->inl.extra_message;
			break;

		case CommandCostInlineType::Tile:
			aux_data->tile = TileIndex(this->inl.tile);
			break;

		case CommandCostInlineType::Result:
			aux_data->result = this->inl.result;
			break;

		case CommandCostInlineType::AdditionalCash:
			aux_data->additional_cash_required = this->inl.additional_cash_required;
			break;
	}
	this->inl.aux_data = aux_data;
	this->SetInlineType(CommandCostInlineType::AuxiliaryData);
}

bool CommandCost::AddInlineData(CommandCostInlineType inl_type)
{
	const CommandCostInlineType current = this->GetInlineType();
	if (current == CommandCostInlineType::AuxiliaryData) return true;
	if (current == inl_type) return false;
	if (current == CommandCostInlineType::None) {
		this->SetInlineType(inl_type);
		return false;
	}
	this->AllocAuxData();
	return true;
}

void CommandCost::SetTile(TileIndex tile)
{
	if (tile == this->GetTile()) return;

	if (this->AddInlineData(CommandCostInlineType::Tile)) {
		this->inl.aux_data->tile = tile;
	} else {
		this->inl.tile = tile.base();
	}
}

void CommandCost::SetAdditionalCashRequired(Money cash)
{
	if (cash == this->GetAdditionalCashRequired()) return;

	if (this->AddInlineData(CommandCostInlineType::AdditionalCash)) {
		this->inl.aux_data->additional_cash_required = cash;
	} else {
		this->inl.additional_cash_required = static_cast<int64_t>(cash);
	}
}

void CommandCost::SetResultDataWithType(CommandResultData result)
{
	this->flags |= CCIF_VALID_RESULT;

	if (this->AddInlineData(CommandCostInlineType::Result)) {
		this->inl.aux_data->result = result;
	} else {
		this->inl.result = result;
	}
}

void CommandCost::SetLargeResult(std::shared_ptr<const CommandLargeResultBase> large_result)
{
	if (this->GetInlineType() != CommandCostInlineType::AuxiliaryData) {
		this->AllocAuxData();
	}

	this->inl.aux_data->large_result = std::move(large_result);
}

template <typename T>
void SerialisePayload(BufferSerialisationRef buffer, const T &payload)
{
	size_t payload_pos = buffer.GetSendOffset();
	buffer.Send_uint16(0);
	payload.Serialise(buffer);
	buffer.SendAtOffset_uint16(payload_pos, (uint16_t)(buffer.GetSendOffset() - payload_pos - 2));
}

void SerialisedBaseCommandContainer::Serialise(BufferSerialisationRef buffer) const
{
	buffer.Send_uint16(this->cmd);
	buffer.Send_uint16(this->error_msg);
	buffer.Send_uint32(this->tile.base());
	SerialisePayload(buffer, this->payload);
}

void DynBaseCommandContainer::Serialise(BufferSerialisationRef buffer) const
{
	buffer.Send_uint16(this->cmd);
	buffer.Send_uint16(this->error_msg);
	buffer.Send_uint32(this->tile.base());
	SerialisePayload(buffer, *this->payload);
}

const char *DynBaseCommandContainer::Deserialise(DeserialisationBuffer &buffer)
{
	this->cmd = static_cast<Commands>(buffer.Recv_uint16());
	if (!IsValidCommand(this->cmd)) return "invalid command";
	if (GetCommandFlags(this->cmd).Test(CommandFlag::Offline)) return "single-player only command";

	this->error_msg = buffer.Recv_uint16();
	this->tile = TileIndex(buffer.Recv_uint32());

	StringValidationSettings default_settings = (!_network_server && GetCommandFlags(this->cmd).Test(CommandFlag::StrCtrl)) ? StringValidationSettings{StringValidationSetting::AllowControlCode, StringValidationSetting::ReplaceWithQuestionMark} : StringValidationSetting::ReplaceWithQuestionMark;

	uint16_t payload_size = buffer.Recv_uint16();
	size_t expected_offset = buffer.GetDeserialisationPosition() + payload_size;
	this->payload = _command_proc_table[this->cmd].payload_deserialiser(buffer, default_settings);
	if (this->payload == nullptr || expected_offset != buffer.GetDeserialisationPosition()) return "failed to deserialise command payload";

	return nullptr;
}

/**
 * Return an error status, with string and parameter.
 * @param str StringID of error.
 * @param value Single parameter for error.
 * @returns CommandCost representing the error.
 */
CommandCost CommandCostWithParam(StringID str, uint64_t value)
{
	CommandCost error = CommandCost(str);
	if (IsLocalCompany()) {
		error.SetEncodedMessage(GetEncodedString(str, value));
	}
	return error;
}
