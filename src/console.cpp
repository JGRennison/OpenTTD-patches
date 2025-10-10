/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file console.cpp Handling of the in-game console. */

#include "stdafx.h"
#include "core/string_builder.hpp"
#include "core/string_consumer.hpp"
#include "console_internal.h"
#include "network/network.h"
#include "network/network_func.h"
#include "network/network_admin.h"
#include "debug.h"
#include "console_func.h"
#include "settings_type.h"

#include "safeguards.h"

static const uint ICON_MAX_RECURSE = 10;     ///< Maximum number of recursion

/* console parser */
/* static */ IConsole::CommandList &IConsole::Commands()
{
	static IConsole::CommandList cmds;
	return cmds;
}

/* static */ IConsole::AliasList &IConsole::Aliases()
{
	static IConsole::AliasList aliases;
	return aliases;
}

std::optional<FileHandle> _iconsole_output_file;

void IConsoleInit()
{
	_iconsole_output_file = std::nullopt;
	_redirect_console_to_client = INVALID_CLIENT_ID;
	_redirect_console_to_admin  = AdminID::Invalid();

	IConsoleGUIInit();

	IConsoleStdLibRegister();
}

static void IConsoleWriteToLogFile(const std::string &string)
{
	if (_iconsole_output_file.has_value()) {
		/* if there is an console output file ... also print it there */
		try {
			fmt::print(*_iconsole_output_file, "{}{}\n", log_prefix().GetLogPrefix(), string);
		} catch (const std::system_error &) {
			_iconsole_output_file.reset();
			IConsolePrint(CC_ERROR, "Cannot write to console log file; closing the log file.");
		}
	}
}

bool CloseConsoleLogIfActive()
{
	if (_iconsole_output_file.has_value()) {
		IConsolePrint(CC_INFO, "Console log file closed.");
		_iconsole_output_file.reset();
		return true;
	}

	return false;
}

void IConsoleFree()
{
	IConsoleGUIFree();
	CloseConsoleLogIfActive();
}

/**
 * Handle the printing of text entered into the console or redirected there
 * by any other means. Text can be redirected to other clients in a network game
 * as well as to a logfile. If the network server is a dedicated server, all activities
 * are also logged. All lines to print are added to a temporary buffer which can be
 * used as a history to print them onscreen
 * @param colour_code The colour of the command.
 * @param string The message to output on the console (notice, error, etc.)
 */
void IConsolePrint(TextColour colour_code, std::string string)
{
	assert(IsValidConsoleColour(colour_code));

	if (_redirect_console_to_client != INVALID_CLIENT_ID) {
		/* Redirect the string to the client */
		NetworkServerSendRcon(_redirect_console_to_client, colour_code, string);
		return;
	}

	if (_redirect_console_to_admin != AdminID::Invalid()) {
		NetworkServerSendAdminRcon(_redirect_console_to_admin, colour_code, string);
		return;
	}

	/* Strip string of colours and invalid characters in place,
	 * and (when applicable) assign it to the console buffer */
	StrMakeValidInPlace(string, {});

	if (_network_dedicated) {
		NetworkAdminConsole("console", string);
		fmt::print("{}{}\n", log_prefix().GetLogPrefix(), string);
		fflush(stdout);
		IConsoleWriteToLogFile(string);
		return;
	}

	IConsoleWriteToLogFile(string);
	IConsoleGUIPrint(colour_code, std::move(string));
}

/**
 * Creates a copy of a string with underscores removed from it
 * @param name String to remove the underscores from.
 * @return A copy of \a name, without underscores.
 */
std::string RemoveUnderscores(std::string_view name)
{
	std::string output;
	output.reserve(name.size());
	std::copy_if(std::begin(name), std::end(name), std::back_inserter(output), [](char c) { return c != '_'; });
	return output;
}

/**
 * Register a new command to be used in the console
 * @param name name of the command that will be used
 * @param proc function that will be called upon execution of command
 */
/* static */ void IConsole::CmdRegister(std::string_view name, IConsoleCmdProc *proc, IConsoleHook *hook, bool unlisted)
{
	IConsole::Commands().try_emplace(RemoveUnderscores(name), name, proc, hook, unlisted);
}

/**
 * Find the command pointed to by its string
 * @param name command to be found
 * @return return Cmdstruct of the found command, or nullptr on failure
 */
/* static */ IConsoleCmd *IConsole::CmdGet(std::string_view name)
{
	auto item = IConsole::Commands().find(RemoveUnderscores(name));
	if (item != IConsole::Commands().end()) return &item->second;
	return nullptr;
}

/**
 * Register a an alias for an already existing command in the console
 * @param name name of the alias that will be used
 * @param cmd name of the command that 'name' will be alias of
 */
/* static */ void IConsole::AliasRegister(std::string_view name, std::string_view cmd)
{
	auto result = IConsole::Aliases().try_emplace(RemoveUnderscores(name), name, cmd);
	if (!result.second) IConsolePrint(CC_ERROR, "An alias with the name '{}' already exists.", name);
}

/**
 * Find the alias pointed to by its string
 * @param name alias to be found
 * @return return Aliasstruct of the found alias, or nullptr on failure
 */
/* static */ IConsoleAlias *IConsole::AliasGet(std::string_view name)
{
	auto item = IConsole::Aliases().find(RemoveUnderscores(name));
	if (item != IConsole::Aliases().end()) return &item->second;
	return nullptr;
}

/**
 * An alias is just another name for a command, or for more commands
 * Execute it as well.
 * @param alias is the alias of the command
 * @param tokens are the parameters given to the original command (0 is the first param)
 * @param recurse_count the number of re-entrant calls to this function
 */
static void IConsoleAliasExec(const IConsoleAlias *alias, std::span<std::string_view> tokens, uint recurse_count)
{
	Debug(console, 6, "Requested command is an alias; parsing...");

	if (recurse_count > ICON_MAX_RECURSE) {
		IConsolePrint(CC_ERROR, "Too many alias expansions, recursion limit reached.");
		return;
	}

	format_buffer buffer;
	StringBuilder builder{buffer};

	StringConsumer consumer{alias->cmdline};
	while (consumer.AnyBytesLeft()) {
		auto c = consumer.TryReadUtf8();
		if (!c.has_value()) {
			IConsolePrint(CC_ERROR, "Alias '{}' ('{}') contains malformed characters.", alias->name, alias->cmdline);
			return;
		}

		switch (*c) {
			case '\'': // ' will double for ""
				builder.PutChar('\"');
				break;

			case ';': // Cmd separator; execute previous and start new command
				IConsoleCmdExec(buffer, recurse_count);

				buffer.clear();
				break;

			case '%': // Some or all parameters
				c = consumer.ReadUtf8();
				switch (*c) {
					case '+': { // All parameters separated: "[param 1]" "[param 2]"
						for (size_t i = 0; i < tokens.size(); ++i) {
							if (i != 0) builder.PutChar(' ');
							builder.PutChar('\"');
							builder += tokens[i];
							builder.PutChar('\"');
						}
						break;
					}

					case '!': { // Merge the parameters to one: "[param 1] [param 2] [param 3...]"
						builder.PutChar('\"');
						for (size_t i = 0; i < tokens.size(); ++i) {
							if (i != 0) builder.PutChar(' ');
							builder += tokens[i];
						}
						builder.PutChar('\"');
						break;
					}

					default: { // One specific parameter: %A = [param 1] %B = [param 2] ...
						size_t param = *c - 'A';

						if (param >= tokens.size()) {
							IConsolePrint(CC_ERROR, "Too many or wrong amount of parameters passed to alias.");
							IConsolePrint(CC_HELP, "Usage of alias '{}': '{}'.", alias->name, alias->cmdline);
							return;
						}

						builder.PutChar('\"');
						builder += tokens[param];
						builder.PutChar('\"');
						break;
					}
				}
				break;

			default:
				builder.PutUtf8(*c);
				break;
		}
	}

	IConsoleCmdExec(buffer, recurse_count);
}

/**
 * Execute a given command passed to us. First chop it up into
 * individual tokens (separated by spaces), then execute it if possible
 * @param command_string string to be parsed and executed
 */
void IConsoleCmdExec(std::string_view command_string, const uint recurse_count)
{
	if (command_string.empty() || command_string[0] == '#') return; // comments

	for (char c : command_string) {
		if (!IsValidChar(c, CS_ALPHANUMERAL)) {
			IConsolePrint(CC_ERROR, "Command '{}' contains malformed characters.", command_string);
			return;
		}
	}

	Debug(console, 4, "Executing cmdline: '{}'", command_string);

	format_buffer buffer;
	StringBuilder builder{buffer};
	StringConsumer consumer{command_string};

	std::vector<std::string> tokens;
	bool found_token = false;
	bool in_quotes = false;

	/* 1. Split up commandline into tokens, separated by spaces, commands
	 * enclosed in "" are taken as one token. We can only go as far as the amount
	 * of characters in our stream or the max amount of tokens we can handle */
	while (consumer.AnyBytesLeft()) {
		auto c = consumer.TryReadUtf8();
		if (!c.has_value()) {
			IConsolePrint(CC_ERROR, "Command '{}' contains malformed characters.", command_string);
			return;
		}

		switch (*c) {
			case ' ': // Token separator
				if (!found_token) break;

				if (in_quotes) {
					builder.PutUtf8(*c);
					break;
				}

				tokens.emplace_back(buffer);
				buffer.clear();
				found_token = false;
				break;

			case '"': // Tokens enclosed in "" are one token
				in_quotes = !in_quotes;
				found_token = true;
				break;

			case '\\': // Escape character for ""
				if (consumer.ReadUtf8If('"')) {
					builder.PutUtf8('"');
					break;
				}
				[[fallthrough]];

			default: // Normal character
				builder.PutUtf8(*c);
				found_token = true;
				break;
		}
	}

	if (found_token) {
		tokens.emplace_back(buffer);
		buffer.clear();
	}

	for (size_t i = 0; i < tokens.size(); i++) {
		Debug(console, 8, "Token {} is: '{}'", i, tokens[i]);
	}

	std::vector<std::string_view> token_views;
	token_views.reserve(tokens.size());
	for (auto &token : tokens) token_views.emplace_back(token);
	IConsoleCmdExecTokens(token_views, recurse_count);
}

/**
 * Execute a given command passed to us as tokens
 * @param cmdstr string to be parsed and executed
 */
void IConsoleCmdExecTokens(std::span<std::string_view> tokens, const uint recurse_count)
{
	if (tokens.empty() || tokens[0].empty()) return; // don't execute empty commands
	/* 2. Determine type of command (cmd or alias) and execute
	 * First try commands, then aliases. Execute
	 * the found action taking into account its hooking code
	 */
	IConsoleCmd *cmd = IConsole::CmdGet(tokens[0]);
	if (cmd != nullptr) {
		ConsoleHookResult chr = (cmd->hook == nullptr ? CHR_ALLOW : cmd->hook(true));
		switch (chr) {
			case CHR_ALLOW: {
				if (!cmd->proc(tokens)) { // index started with 0
					cmd->proc({}); // if command failed, give help
				}
				return;
			}

			case CHR_DISALLOW: return;
			case CHR_HIDE: break;
		}
	}

	IConsoleAlias *alias = IConsole::AliasGet(tokens[0]);
	if (alias != nullptr) {
		IConsoleAliasExec(alias, tokens.subspan(1), recurse_count + 1);
		return;
	}

	IConsolePrint(CC_ERROR, "Command '{}' not found.", tokens[0]);
}
