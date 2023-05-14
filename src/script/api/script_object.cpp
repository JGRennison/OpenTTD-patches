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
#include "../../string_func_extra.h"
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

/* static */ void ScriptObject::SetLastCommand(TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	ScriptStorage *s = GetStorage();
	DEBUG(script, 6, "SetLastCommand company=%02d tile=%06x p1=%08x p2=%08x p3=" OTTD_PRINTFHEX64PAD " cmd=%d", s->root_company, tile, p1, p2, p3, cmd);
	s->last_tile = tile;
	s->last_p1 = p1;
	s->last_p2 = p2;
	s->last_p3 = p3;
	s->last_cmd = cmd & CMD_ID_MASK;
}

/* static */ bool ScriptObject::CheckLastCommand(TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	ScriptStorage *s = GetStorage();
	DEBUG(script, 6, "CheckLastCommand company=%02d tile=%06x p1=%08x p2=%08x p3=" OTTD_PRINTFHEX64PAD " cmd=%d", s->root_company, tile, p1, p2, p3, cmd);
	if (s->last_tile != tile) return false;
	if (s->last_p1 != p1) return false;
	if (s->last_p2 != p2) return false;
	if (s->last_p3 != p3) return false;
	if (s->last_cmd != (cmd & CMD_ID_MASK)) return false;
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

/* static */ void ScriptObject::SetLastCommandResultData(uint32 last_result)
{
	GetStorage()->last_result = last_result;
}

/* static */ uint32 ScriptObject::GetLastCommandResultData()
{
	return GetStorage()->last_result;
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
	/* Also store the results of various global variables */
	SetNewVehicleID(_new_vehicle_id);
	SetNewSignID(_new_sign_id);
	SetNewGroupID(_new_group_id);
	SetNewGoalID(_new_goal_id);
	SetNewStoryPageID(_new_story_page_id);
	SetNewStoryPageElementID(_new_story_page_element_id);
}

/* static */ bool ScriptObject::GetLastCommandRes()
{
	return GetStorage()->last_command_res;
}

/* static */ void ScriptObject::SetNewVehicleID(VehicleID vehicle_id)
{
	GetStorage()->new_vehicle_id = vehicle_id;
}

/* static */ VehicleID ScriptObject::GetNewVehicleID()
{
	return GetStorage()->new_vehicle_id;
}

/* static */ void ScriptObject::SetNewSignID(SignID sign_id)
{
	GetStorage()->new_sign_id = sign_id;
}

/* static */ SignID ScriptObject::GetNewSignID()
{
	return GetStorage()->new_sign_id;
}

/* static */ void ScriptObject::SetNewGroupID(GroupID group_id)
{
	GetStorage()->new_group_id = group_id;
}

/* static */ GroupID ScriptObject::GetNewGroupID()
{
	return GetStorage()->new_group_id;
}

/* static */ void ScriptObject::SetNewGoalID(GoalID goal_id)
{
	GetStorage()->new_goal_id = goal_id;
}

/* static */ GroupID ScriptObject::GetNewGoalID()
{
	return GetStorage()->new_goal_id;
}

/* static */ void ScriptObject::SetNewStoryPageID(StoryPageID story_page_id)
{
	GetStorage()->new_story_page_id = story_page_id;
}

/* static */ GroupID ScriptObject::GetNewStoryPageID()
{
	return GetStorage()->new_story_page_id;
}

/* static */ void ScriptObject::SetNewStoryPageElementID(StoryPageElementID story_page_element_id)
{
	GetStorage()->new_story_page_element_id = story_page_element_id;
}

/* static */ GroupID ScriptObject::GetNewStoryPageElementID()
{
	return GetStorage()->new_story_page_element_id;
}

/* static */ void ScriptObject::SetAllowDoCommand(bool allow)
{
	GetStorage()->allow_do_command = allow;
}

/* static */ bool ScriptObject::GetAllowDoCommand()
{
	return GetStorage()->allow_do_command;
}

/* static */ void ScriptObject::SetCompany(CompanyID company)
{
	if (GetStorage()->root_company == INVALID_OWNER) GetStorage()->root_company = company;
	GetStorage()->company = company;

	_current_company = company;
}

/* static */ CompanyID ScriptObject::GetCompany()
{
	return GetStorage()->company;
}

/* static */ CompanyID ScriptObject::GetRootCompany()
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

/* static */ void *&ScriptObject::GetLogPointer()
{
	return GetStorage()->log_data;
}

/* static */ char *ScriptObject::GetString(StringID string)
{
	char buffer[64];
	::GetString(buffer, string, lastof(buffer));
	::StrMakeValidInPlace(buffer, lastof(buffer), SVS_NONE);
	return ::stredup(buffer);
}

/* static */ void ScriptObject::SetCallbackVariable(int index, int value)
{
	if ((size_t)index >= GetStorage()->callback_value.size()) GetStorage()->callback_value.resize(index + 1);
	GetStorage()->callback_value[index] = value;
}

/* static */ int ScriptObject::GetCallbackVariable(int index)
{
	return GetStorage()->callback_value[index];
}

/* static */ bool ScriptObject::DoCommandEx(TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint cmd, const char *text, const CommandAuxiliaryBase *aux_data, Script_SuspendCallbackProc *callback)
{
	if (!ScriptObject::CanSuspend()) {
		throw Script_FatalError("You are not allowed to execute any DoCommand (even indirect) in your constructor, Save(), Load(), and any valuator.");
	}

	if (!ScriptCompanyMode::IsDeity() && !ScriptCompanyMode::IsValid()) {
		ScriptObject::SetLastError(ScriptError::ERR_PRECONDITION_INVALID_COMPANY);
		return false;
	}

	std::string text_validated;
	if (!StrEmpty(text) && (GetCommandFlags(cmd) & CMD_STR_CTRL) == 0) {
		/* The string must be valid, i.e. not contain special codes. Since some
		 * can be made with GSText, make sure the control codes are removed. */
		text_validated = text;
		::StrMakeValidInPlace(text_validated, SVS_NONE);
		text = text_validated.c_str();
	}

	/* Set the default callback to return a true/false result of the DoCommand */
	if (callback == nullptr) callback = &ScriptInstance::DoCommandReturn;

	/* Are we only interested in the estimate costs? */
	bool estimate_only = GetDoCommandMode() != nullptr && !GetDoCommandMode()();

	/* Only set p2 when the command does not come from the network. */
	if (GetCommandFlags(cmd) & CMD_CLIENT_ID && p2 == 0) p2 = UINT32_MAX;

	SCOPE_INFO_FMT([=], "ScriptObject::DoCommand: tile: %X (%d x %d), p1: 0x%X, p2: 0x%X, p3: 0x" OTTD_PRINTFHEX64 ", company: %s, cmd: 0x%X (%s), estimate_only: %d",
			tile, TileX(tile), TileY(tile), p1, p2, p3, scope_dumper().CompanyInfo(_current_company), cmd, GetCommandName(cmd), estimate_only);

	/* Store the command for command callback validation. */
	if (!estimate_only && _networking && !_generating_world) SetLastCommand(tile, p1, p2, p3, cmd);

	/* Try to perform the command. */
	CommandCost res = ::DoCommandPScript(tile, p1, p2, p3, cmd, (_networking && !_generating_world) ? ScriptObject::GetActiveInstance()->GetDoCommandCallback() : nullptr, text, false, estimate_only, aux_data);

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
	SetLastCommandResultData(res.GetResultData());
	SetLastCommandRes(true);

	if (_generating_world) {
		IncreaseDoCommandCosts(res.GetCost());
		if (callback != nullptr) {
			/* Insert return value into to stack and throw a control code that
			 * the return value in the stack should be used. */
			callback(GetActiveInstance());
			throw SQInteger(1);
		}
		return true;
	} else if (_networking) {
		/* Suspend the script till the command is really executed. */
		throw Script_Suspend(-(int)GetDoCommandDelay(), callback);
	} else if (GetActiveInstance()->GetScriptType() == ST_GS && (_pause_mode & PM_PAUSED_GAME_SCRIPT) != PM_UNPAUSED) {
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
