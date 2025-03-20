/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_object.cpp Implementation of ScriptObject. */

#include "../../stdafx.h"
#include "../../script/squirrel.hpp"
#include "../../command_func.h"
#include "../../company_func.h"
#include "../../company_base.h"
#include "../../network/network.h"
#include "../../genworld.h"
#include "../../string_func.h"
#include "../../strings_func.h"
#include "../../scope_info.h"
#include "../../map_func.h"

#include "../script_storage.hpp"
#include "../script_instance.hpp"
#include "../script_fatalerror.hpp"
#include "script_controller.hpp"
#include "script_error.hpp"
#include "../../debug.h"

#include "../../safeguards.h"

void SimpleCountedObject::Release()
{
	int32_t res = --this->ref_count;
	assert(res >= 0);
	if (res == 0) {
		try {
			this->FinalRelease(); // may throw, for example ScriptTest/ExecMode
		} catch (...) {
			delete this;
			throw;
		}
		delete this;
	}
}

/**
 * Get the storage associated with the current ScriptInstance.
 * @return The storage.
 */
static ScriptStorage *GetStorage()
{
	return ScriptObject::GetActiveInstance()->GetStorage();
}


/* static */ ScriptInstance *ScriptObject::ActiveInstance::active = nullptr;

ScriptObject::ActiveInstance::ActiveInstance(ScriptInstance *instance) : alc_scope(instance->engine)
{
	this->last_active = ScriptObject::ActiveInstance::active;
	ScriptObject::ActiveInstance::active = instance;
}

ScriptObject::ActiveInstance::~ActiveInstance()
{
	ScriptObject::ActiveInstance::active = this->last_active;
}

/* static */ ScriptInstance *ScriptObject::GetActiveInstance()
{
	assert(ScriptObject::ActiveInstance::active != nullptr);
	return ScriptObject::ActiveInstance::active;
}


/* static */ void ScriptObject::SetDoCommandDelay(uint ticks)
{
	assert(ticks > 0);
	GetStorage()->delay = ticks;
}

/* static */ uint ScriptObject::GetDoCommandDelay()
{
	return GetStorage()->delay;
}

/* static */ void ScriptObject::SetDoCommandMode(ScriptModeProc *proc, ScriptObject *instance)
{
	GetStorage()->mode = proc;
	GetStorage()->mode_instance = instance;
}

/* static */ ScriptModeProc *ScriptObject::GetDoCommandMode()
{
	return GetStorage()->mode;
}

/* static */ ScriptObject *ScriptObject::GetDoCommandModeInstance()
{
	return GetStorage()->mode_instance;
}

/* static */ void ScriptObject::SetDoCommandAsyncMode(ScriptAsyncModeProc *proc, ScriptObject *instance)
{
	GetStorage()->async_mode = proc;
	GetStorage()->async_mode_instance = instance;
}

/* static */ ScriptAsyncModeProc *ScriptObject::GetDoCommandAsyncMode()
{
	return GetStorage()->async_mode;
}

/* static */ ScriptObject *ScriptObject::GetDoCommandAsyncModeInstance()
{
	return GetStorage()->async_mode_instance;
}

/* static */ void ScriptObject::SetLastCommand(Commands cmd, TileIndex tile, CallbackParameter cb_param)
{
	ScriptStorage *s = GetStorage();
	Debug(script, 6, "SetLastCommand company={} cmd={:X} tile={:X}, cb_param={:X}", s->root_company, cmd, tile, cb_param);
	s->last_cmd = cmd;
	s->last_tile = tile;
	s->last_cb_param = cb_param;
}

/* static */ bool ScriptObject::CheckLastCommand(Commands cmd, TileIndex tile, CallbackParameter cb_param)
{
	ScriptStorage *s = GetStorage();
	Debug(script, 6, "CheckLastCommand company={} cmd={:X} tile={:X}, cb_param={:X}", s->root_company, cmd, tile, cb_param);
	if (s->last_cmd != cmd) return false;
	if (s->last_tile != tile) return false;
	if (s->last_cb_param != cb_param) return false;

	return true;
}

/* static */ void ScriptObject::SetDoCommandCosts(Money value)
{
	GetStorage()->costs = CommandCost(INVALID_EXPENSES, value); // Expense type is never read.
}

/* static */ void ScriptObject::IncreaseDoCommandCosts(Money value)
{
	GetStorage()->costs.AddCost(value);
}

/* static */ Money ScriptObject::GetDoCommandCosts()
{
	return GetStorage()->costs.GetCost();
}

/* static */ void ScriptObject::SetLastError(ScriptErrorType last_error)
{
	GetStorage()->last_error = last_error;
}

/* static */ ScriptErrorType ScriptObject::GetLastError()
{
	return GetStorage()->last_error;
}

/* static */ void ScriptObject::SetLastCost(Money last_cost)
{
	GetStorage()->last_cost = last_cost;
}

/* static */ Money ScriptObject::GetLastCost()
{
	return GetStorage()->last_cost;
}

/* static */ void ScriptObject::SetLastCommandResultData(uint32_t last_result)
{
	auto *storage = GetStorage();
	storage->last_result = last_result;
	storage->last_result_valid = true;
}

/* static */ void ScriptObject::ClearLastCommandResultData()
{
	GetStorage()->last_result_valid = false;
}

/* static */ std::pair<uint32_t, bool> ScriptObject::GetLastCommandResultDataRaw()
{
	auto *storage = GetStorage();
	return { storage->last_result, storage->last_result_valid };
}

/* static */ void ScriptObject::SetRoadType(RoadType road_type)
{
	GetStorage()->road_type = road_type;
}

/* static */ RoadType ScriptObject::GetRoadType()
{
	return GetStorage()->road_type;
}

/* static */ void ScriptObject::SetRailType(RailType rail_type)
{
	GetStorage()->rail_type = rail_type;
}

/* static */ RailType ScriptObject::GetRailType()
{
	return GetStorage()->rail_type;
}

/* static */ void ScriptObject::SetLastCommandRes(bool res)
{
	GetStorage()->last_command_res = res;
}

/* static */ bool ScriptObject::GetLastCommandRes()
{
	return GetStorage()->last_command_res;
}

/* static */ void ScriptObject::SetAllowDoCommand(bool allow)
{
	GetStorage()->allow_do_command = allow;
}

/* static */ bool ScriptObject::GetAllowDoCommand()
{
	return GetStorage()->allow_do_command;
}

/* static */ void ScriptObject::SetCompany(::CompanyID company)
{
	if (GetStorage()->root_company == INVALID_OWNER) GetStorage()->root_company = company;
	GetStorage()->company = company;

	_current_company = company;
}

/* static */ ::CompanyID ScriptObject::GetCompany()
{
	return GetStorage()->company;
}

/* static */ ::CompanyID ScriptObject::GetRootCompany()
{
	return GetStorage()->root_company;
}

/* static */ bool ScriptObject::CanSuspend()
{
	Squirrel *squirrel = ScriptObject::GetActiveInstance()->engine;
	return GetStorage()->allow_do_command && squirrel->CanSuspend();
}

/* static */ void *&ScriptObject::GetEventPointer()
{
	return GetStorage()->event_data;
}

/* static */ ScriptLogTypes::LogData &ScriptObject::GetLogData()
{
	return GetStorage()->log_data;
}

/* static */ std::string ScriptObject::GetString(StringID string)
{
	return ::StrMakeValid(::GetString(string));
}

/* static */ void ScriptObject::SetCallbackVariable(int index, int value)
{
	if (static_cast<size_t>(index) >= GetStorage()->callback_value.size()) GetStorage()->callback_value.resize(index + 1);
	GetStorage()->callback_value[index] = value;
}

/* static */ int ScriptObject::GetCallbackVariable(int index)
{
	return GetStorage()->callback_value[index];
}

/* static */ bool ScriptObject::DoCommandImplementation(Commands cmd, TileIndex tile, CommandPayloadBase &&payload, Script_SuspendCallbackProc *callback, DoCommandIntlFlag intl_flags)
{
	if (!ScriptObject::CanSuspend()) {
		throw Script_FatalError("You are not allowed to execute any DoCommand (even indirect) in your constructor, Save(), Load(), and any valuator.");
	}

	if (!ScriptCompanyMode::IsDeity() && !ScriptCompanyMode::IsValid()) {
		ScriptObject::SetLastError(ScriptError::ERR_PRECONDITION_INVALID_COMPANY);
		return false;
	}

	if ((GetCommandFlags(cmd) & CMD_STR_CTRL) == 0) {
		/* The string must be valid, i.e. not contain special codes. Since some
		 * can be made with GSText, make sure the control codes are removed. */
		payload.SanitiseStrings(SVS_NONE);
	}

	/* Set the default callback to return a true/false result of the DoCommand */
	if (callback == nullptr) callback = &ScriptInstance::DoCommandReturn;

	/* Are we only interested in the estimate costs? */
	bool estimate_only = GetDoCommandMode() != nullptr && !GetDoCommandMode()();

	/* Should the command be executed asynchronously? */
	bool asynchronous = GetDoCommandAsyncMode() != nullptr && GetDoCommandAsyncMode()() && GetActiveInstance()->GetScriptType() == ScriptType::GS;

#if !defined(DISABLE_SCOPE_INFO)
	FunctorScopeStackRecord scope_print([=, &payload](format_target &output) {
		output.format("ScriptObject::DoCommand: tile: {}, intl_flags: 0x{:X}, company: {}, cmd: 0x{:X} {}, estimate_only: {}, payload: ",
				tile, intl_flags, CompanyInfoDumper(_current_company), cmd, GetCommandName(cmd), estimate_only);
		payload.FormatDebugSummary(output);
	});
#endif

	/* Rolling identifier for script callback identification */
	static CallbackParameter _last_cb_param = 0;
	CallbackParameter cb_param = ++_last_cb_param;

	/* Store the command for command callback validation. */
	if (!estimate_only && _networking && !_generating_world) SetLastCommand(cmd, tile, cb_param);

	/* Try to perform the command. */
	const bool use_cb = (_networking && !_generating_world && !asynchronous);
	CommandCost res = ::DoCommandPScript(cmd, tile, payload, use_cb ? ScriptObject::GetActiveInstance()->GetDoCommandCallback() : CommandCallback::None, use_cb ? cb_param : 0,
			intl_flags, estimate_only, asynchronous);

	/* We failed; set the error and bail out */
	if (res.Failed()) {
		SetLastError(ScriptError::StringToError(res.GetErrorMessage()));
		return false;
	}

	/* No error, then clear it. */
	SetLastError(ScriptError::ERR_NONE);

	/* Estimates, update the cost for the estimate and be done */
	if (estimate_only) {
		IncreaseDoCommandCosts(res.GetCost());
		return true;
	}

	/* Costs of this operation. */
	SetLastCost(res.GetCost());
	if (res.HasResultData()) {
		SetLastCommandResultData(res.GetResultData());
	} else {
		ClearLastCommandResultData();
	}
	SetLastCommandRes(true);

	if (_generating_world || asynchronous) {
		IncreaseDoCommandCosts(res.GetCost());
		if (callback != nullptr) {
			/* Insert return value into to stack and throw a control code that
			 * the return value in the stack should be used. */
			if (!_generating_world) ScriptController::DecreaseOps(100);
			callback(GetActiveInstance());
			throw SQInteger(1);
		}
		return true;
	} else if (_networking) {
		/* Suspend the script till the command is really executed. */
		throw Script_Suspend(-(int)GetDoCommandDelay(), callback);
	} else if (GetActiveInstance()->GetScriptType() == ScriptType::GS && (_pause_mode & PM_PAUSED_GAME_SCRIPT) != PM_UNPAUSED) {
		/* Game is paused due to GS, just execute as fast as possible */
		IncreaseDoCommandCosts(res.GetCost());
		ScriptController::DecreaseOps(100);
		callback(GetActiveInstance());
		throw SQInteger(1);
	} else {
		IncreaseDoCommandCosts(res.GetCost());

		/* Suspend the script player for 1+ ticks, so it simulates multiplayer. This
		 *  both avoids confusion when a developer launched the script in a
		 *  multiplayer game, but also gives time for the GUI and human player
		 *  to interact with the game. */
		throw Script_Suspend(GetDoCommandDelay(), callback);
	}

	NOT_REACHED();
}


/* static */ Randomizer ScriptObject::random_states[OWNER_END];

Randomizer &ScriptObject::GetRandomizer(Owner owner)
{
	return ScriptObject::random_states[owner];
}

void ScriptObject::InitializeRandomizers()
{
	Randomizer random = _random;
	for (Owner owner = OWNER_BEGIN; owner < OWNER_END; owner++) {
		ScriptObject::GetRandomizer(owner).SetSeed(random.Next());
	}
}

/* static */ bool ScriptObject::IsNewUniqueLogMessage(const std::string &msg)
{
	return !GetStorage()->seen_unique_log_messages.contains(msg);
}

/* static */ void ScriptObject::RegisterUniqueLogMessage(std::string &&msg)
{
	GetStorage()->seen_unique_log_messages.emplace(std::move(msg));
}
