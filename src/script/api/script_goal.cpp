/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_goal.cpp Implementation of ScriptGoal. */

#include "../../stdafx.h"
#include "script_game.hpp"
#include "script_goal.hpp"
#include "script_error.hpp"
#include "script_industry.hpp"
#include "script_map.hpp"
#include "script_town.hpp"
#include "script_story_page.hpp"
#include "../script_instance.hpp"
#include "../../goal_base.h"
#include "../../string_func.h"
#include "../../network/network_base.h"

#include "../../safeguards.h"

/* static */ bool ScriptGoal::IsValidGoal(GoalID goal_id)
{
	return ::Goal::IsValidID(goal_id);
}

/* static */ bool ScriptGoal::IsValidGoalDestination(ScriptCompany::CompanyID company, GoalType type, SQInteger destination)
{
	CompanyID c = (::CompanyID)company;
	if (company == ScriptCompany::COMPANY_INVALID) c = INVALID_COMPANY;
	StoryPage *story_page = nullptr;
	if (type == GT_STORY_PAGE && ScriptStoryPage::IsValidStoryPage((ScriptStoryPage::StoryPageID)destination)) story_page = ::StoryPage::Get((ScriptStoryPage::StoryPageID)destination);
	return (type == GT_NONE && destination == 0) ||
			(type == GT_TILE && ScriptMap::IsValidTile(destination)) ||
			(type == GT_INDUSTRY && ScriptIndustry::IsValidIndustry(destination)) ||
			(type == GT_TOWN && ScriptTown::IsValidTown(destination)) ||
			(type == GT_COMPANY && ScriptCompany::ResolveCompanyID((ScriptCompany::CompanyID)destination) != ScriptCompany::COMPANY_INVALID) ||
			(type == GT_STORY_PAGE && story_page != nullptr && (c == INVALID_COMPANY ? story_page->company == INVALID_COMPANY : story_page->company == INVALID_COMPANY || story_page->company == c));
}

/* static */ ScriptGoal::GoalID ScriptGoal::New(ScriptCompany::CompanyID company, Text *goal, GoalType type, SQInteger destination)
{
	ScriptObjectRef counter(goal);

	EnforceDeityMode(GOAL_INVALID);
	EnforcePrecondition(GOAL_INVALID, goal != nullptr);
	const std::string &text = goal->GetEncodedText();
	EnforcePreconditionEncodedText(GOAL_INVALID, text);
	EnforcePrecondition(GOAL_INVALID, company == ScriptCompany::COMPANY_INVALID || ScriptCompany::ResolveCompanyID(company) != ScriptCompany::COMPANY_INVALID);
	EnforcePrecondition(GOAL_INVALID, IsValidGoalDestination(company, type, destination));

	if (!ScriptObject::DoCommand(0, type | (company << 8), destination, CMD_CREATE_GOAL, text, &ScriptInstance::DoCommandReturnGoalID)) return GOAL_INVALID;

	/* In case of test-mode, we return GoalID 0 */
	return (ScriptGoal::GoalID)0;
}

/* static */ bool ScriptGoal::Remove(GoalID goal_id)
{
	EnforceDeityMode(false);
	EnforcePrecondition(false, IsValidGoal(goal_id));

	return ScriptObject::DoCommand(0, goal_id, 0, CMD_REMOVE_GOAL);
}

/* static */ bool ScriptGoal::SetDestination(GoalID goal_id, GoalType type, SQInteger destination)
{
	EnforceDeityMode(false);
	EnforcePrecondition(false, IsValidGoal(goal_id));
	const Goal *g = Goal::Get(goal_id);
	EnforcePrecondition(false, IsValidGoalDestination((ScriptCompany::CompanyID)g->company, type, destination));

	return ScriptObject::DoCommandEx(0, goal_id, destination, type, CMD_SET_GOAL_DESTINATION);
}

/* static */ bool ScriptGoal::SetText(GoalID goal_id, Text *goal)
{
	ScriptObjectRef counter(goal);

	EnforcePrecondition(false, IsValidGoal(goal_id));
	EnforceDeityMode(false);
	EnforcePrecondition(false, goal != nullptr);
	const std::string &text = goal->GetEncodedText();
	EnforcePreconditionEncodedText(false, text);

	return ScriptObject::DoCommand(0, goal_id, 0, CMD_SET_GOAL_TEXT, text);
}

/* static */ bool ScriptGoal::SetProgress(GoalID goal_id, Text *progress)
{
	ScriptObjectRef counter(progress);

	EnforcePrecondition(false, IsValidGoal(goal_id));
	EnforceDeityMode(false);

	return ScriptObject::DoCommand(0, goal_id, 0, CMD_SET_GOAL_PROGRESS, progress != nullptr ? progress->GetEncodedText().c_str() : "");
}

/* static */ bool ScriptGoal::SetCompleted(GoalID goal_id, bool completed)
{
	EnforcePrecondition(false, IsValidGoal(goal_id));
	EnforceDeityMode(false);

	return ScriptObject::DoCommand(0, goal_id, completed ? 1 : 0, CMD_SET_GOAL_COMPLETED);
}

/* static */ bool ScriptGoal::IsCompleted(GoalID goal_id)
{
	EnforcePrecondition(false, IsValidGoal(goal_id));
	EnforceDeityMode(false);

	const Goal *g = Goal::Get(goal_id);
	return g != nullptr && g->completed;
}

/* static */ bool ScriptGoal::DoQuestion(SQInteger uniqueid, uint32_t target, bool is_client, Text *question, QuestionType type, SQInteger buttons)
{
	ScriptObjectRef counter(question);

	EnforceDeityMode(false);
	EnforcePrecondition(false, question != nullptr);
	const std::string &text = question->GetEncodedText();
	EnforcePreconditionEncodedText(false, text);
	uint min_buttons = (type == QT_QUESTION ? 1 : 0);
	EnforcePrecondition(false, CountBits<uint64_t>(buttons) >= min_buttons && CountBits<uint64_t>(buttons) <= 3);
	EnforcePrecondition(false, buttons >= 0 && buttons < (1 << ::GOAL_QUESTION_BUTTON_COUNT));
	EnforcePrecondition(false, (int)type < ::GQT_END);
	EnforcePrecondition(false, uniqueid >= 0 && uniqueid <= UINT16_MAX);

	return ScriptObject::DoCommandEx(0, uniqueid, buttons | (type << 29) | (is_client ? (1 << 31) : 0), target, CMD_GOAL_QUESTION, text);
}

/* static */ bool ScriptGoal::Question(SQInteger uniqueid, ScriptCompany::CompanyID company, Text *question, QuestionType type, SQInteger buttons)
{
	EnforcePrecondition(false, company == ScriptCompany::COMPANY_INVALID || ScriptCompany::ResolveCompanyID(company) != ScriptCompany::COMPANY_INVALID);
	uint8_t c = company;
	if (company == ScriptCompany::COMPANY_INVALID) c = INVALID_COMPANY;

	return DoQuestion(uniqueid, c, false, question, type, buttons);
}

/* static */ bool ScriptGoal::QuestionClient(SQInteger uniqueid, ScriptClient::ClientID client, Text *question, QuestionType type, SQInteger buttons)
{
	EnforcePrecondition(false, ScriptGame::IsMultiplayer());
	EnforcePrecondition(false, ScriptClient::ResolveClientID(client) != ScriptClient::CLIENT_INVALID);
	return DoQuestion(uniqueid, client, true, question, type, buttons);
}

/* static */ bool ScriptGoal::CloseQuestion(SQInteger uniqueid)
{
	EnforceDeityMode(false);
	EnforcePrecondition(false, uniqueid >= 0 && uniqueid <= UINT16_MAX);

	return ScriptObject::DoCommand(0, uniqueid, 0, CMD_GOAL_QUESTION_ANSWER);
}
