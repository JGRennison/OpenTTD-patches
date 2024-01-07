/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file goal.cpp Handling of goals. */

#include "stdafx.h"
#include "company_func.h"
#include "industry.h"
#include "town.h"
#include "window_func.h"
#include "goal_base.h"
#include "core/pool_func.hpp"
#include "game/game.hpp"
#include "command_func.h"
#include "company_base.h"
#include "story_base.h"
#include "string_func.h"
#include "gui.h"
#include "network/network.h"
#include "network/network_base.h"
#include "network/network_func.h"

#include "safeguards.h"


GoalID _new_goal_id;

GoalPool _goal_pool("Goal");
INSTANTIATE_POOL_METHODS(Goal)

/* static */ bool Goal::IsValidGoalDestination(CompanyID company, GoalType type, GoalTypeID dest)
{
	switch (type) {
		case GT_NONE:
			if (dest != 0) return false;
			break;

		case GT_TILE:
			if (!IsValidTile(dest)) return false;
			break;

		case GT_INDUSTRY:
			if (!Industry::IsValidID(dest)) return false;
			break;

		case GT_TOWN:
			if (!Town::IsValidID(dest)) return false;
			break;

		case GT_COMPANY:
			if (!Company::IsValidID(dest)) return false;
			break;

		case GT_STORY_PAGE: {
			if (!StoryPage::IsValidID(dest)) return false;
			CompanyID story_company = StoryPage::Get(dest)->company;
			if (company == INVALID_COMPANY ? story_company != INVALID_COMPANY : story_company != INVALID_COMPANY && story_company != company) return false;
			break;
		}

		default: return false;
	}
	return true;
}

/**
 * Create a new goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 -  7) - GoalType of destination.
 * - p1 = (bit  8 - 15) - Company for which this goal is.
 * @param p2 GoalTypeID of destination.
 * @param text Text of the goal.
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateGoal(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (!Goal::CanAllocateItem()) return CMD_ERROR;

	GoalType type = (GoalType)GB(p1, 0, 8);
	CompanyID company = (CompanyID)GB(p1, 8, 8);
	GoalTypeID dest = p2;

	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;
	if (company != INVALID_COMPANY && !Company::IsValidID(company)) return CMD_ERROR;
	if (!Goal::IsValidGoalDestination(company, type, dest)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Goal *g = new Goal();
		g->type = type;
		g->dst = dest;
		g->company = company;
		if (StrEmpty(text)) {
			g->text.clear();
		} else {
			g->text = text;
		}
		g->completed = false;

		if (g->company == INVALID_COMPANY) {
			InvalidateWindowClassesData(WC_GOALS_LIST);
		} else {
			InvalidateWindowData(WC_GOALS_LIST, g->company);
		}
		if (Goal::GetNumItems() == 1) InvalidateWindowData(WC_MAIN_TOOLBAR, 0);

		_new_goal_id = g->index;
	}

	return CommandCost();
}

/**
 * Remove a goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 GoalID to remove.
 * @param p2 unused.
 * @param text unused.
 * @return the cost of this operation or an error
 */
CommandCost CmdRemoveGoal(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (!Goal::IsValidID(p1)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Goal *g = Goal::Get(p1);
		CompanyID c = g->company;
		delete g;

		if (c == INVALID_COMPANY) {
			InvalidateWindowClassesData(WC_GOALS_LIST);
		} else {
			InvalidateWindowData(WC_GOALS_LIST, c);
		}
		if (Goal::GetNumItems() == 0) InvalidateWindowData(WC_MAIN_TOOLBAR, 0);
	}

	return CommandCost();
}

/**
 * Update goal destination of a goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 GoalID to update.
 * @param p2 GoalTypeID of destination.
 * @param p3 various bitstuffed elements
 * - p3 = (bit  0 -  7) - GoalType of destination.
 * @param p2 GoalTypeID of destination.
 * @param text unused.
 */
CommandCost CmdSetGoalDestination(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	GoalID goal = p1;
	GoalTypeID dest = p2;
	GoalType type = (GoalType)GB(p3, 0, 8);

	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (!Goal::IsValidID(goal)) return CMD_ERROR;
	Goal *g = Goal::Get(goal);
	if (!Goal::IsValidGoalDestination(g->company, type, dest)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		g->type = type;
		g->dst = dest;
	}

	return CommandCost();
}

/**
 * Update goal text of a goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 GoalID to update.
 * @param p2 unused
 * @param text Text of the goal.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetGoalText(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (!Goal::IsValidID(p1)) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Goal *g = Goal::Get(p1);
		if (StrEmpty(text)) {
			g->text.clear();
		} else {
			g->text = text;
		}

		if (g->company == INVALID_COMPANY) {
			InvalidateWindowClassesData(WC_GOALS_LIST);
		} else {
			InvalidateWindowData(WC_GOALS_LIST, g->company);
		}
	}

	return CommandCost();
}

/**
 * Update progress text of a goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 GoalID to update.
 * @param p2 unused
 * @param text Progress text of the goal.
 * @return the cost of this operation or an error
 */
CommandCost CmdSetGoalProgress(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (!Goal::IsValidID(p1)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Goal *g = Goal::Get(p1);
		if (StrEmpty(text)) {
			g->progress.clear();
		} else {
			g->progress = text;
		}

		if (g->company == INVALID_COMPANY) {
			InvalidateWindowClassesData(WC_GOALS_LIST);
		} else {
			InvalidateWindowData(WC_GOALS_LIST, g->company);
		}
	}

	return CommandCost();
}

/**
 * Update completed state of a goal.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 GoalID to update.
 * @param p2 completed state. If goal is completed, set to 1, otherwise 0.
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetGoalCompleted(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (!Goal::IsValidID(p1)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Goal *g = Goal::Get(p1);
		g->completed = p2 == 1;

		if (g->company == INVALID_COMPANY) {
			InvalidateWindowClassesData(WC_GOALS_LIST);
		} else {
			InvalidateWindowData(WC_GOALS_LIST, g->company);
		}
	}

	return CommandCost();
}

/**
 * Ask a goal related question
 * @param tile unused.
 * @param flags type of operation
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - Unique ID to use for this question.
 * @param p2 various bitstuffed elements
 * - p2 = (bit 0 - 17) - Buttons of the question.
 * - p2 = (bit 29 - 30) - Question type.
 * - p2 = (bit 31) - Question target: 0 - company, 1 - client.
 * @param p3 various bitstuffed elements
 * - p3 = (bit 0 - 31) - Company or client for which this question is.
 * @param text Text of the question.
 * @return the cost of this operation or an error
 */
CommandCost CmdGoalQuestion(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, uint64_t p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	uint16_t uniqueid = (uint16_t)GB(p1, 0, 16);
	CompanyID company = (CompanyID)GB(p3, 0, 32);
	ClientID client = (ClientID)GB(p3, 0, 32);

	static_assert(sizeof(uint32_t) >= sizeof(CompanyID));
	static_assert(sizeof(uint32_t) >= sizeof(ClientID));

	static_assert(GOAL_QUESTION_BUTTON_COUNT < 29);
	uint32_t button_mask = GB(p2, 0, GOAL_QUESTION_BUTTON_COUNT);
	byte type = GB(p2, 29, 2);
	bool is_client = HasBit(p2, 31);

	if (_current_company != OWNER_DEITY) return CMD_ERROR;
	if (StrEmpty(text)) return CMD_ERROR;
	if (is_client) {
		/* Only check during pre-flight; the client might have left between
		 * testing and executing. In that case it is fine to just ignore the
		 * fact the client is no longer here. */
		if (!(flags & DC_EXEC) && _network_server && NetworkClientInfo::GetByClientID(client) == nullptr) return CMD_ERROR;
	} else {
		if (company != INVALID_COMPANY && !Company::IsValidID(company)) return CMD_ERROR;
	}
	uint min_buttons = (type == GQT_QUESTION ? 1 : 0);
	if (CountBits(button_mask) < min_buttons || CountBits(button_mask) > 3) return CMD_ERROR;
	if (type >= GQT_END) return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (is_client) {
			if (client != _network_own_client_id) return CommandCost();
		} else {
			if (company == INVALID_COMPANY && !Company::IsValidID(_local_company)) return CommandCost();
			if (company != INVALID_COMPANY && company != _local_company) return CommandCost();
		}
		ShowGoalQuestion(uniqueid, type, button_mask, text);
	}

	return CommandCost();
}

/**
 * Reply to a goal question.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 Unique ID to use for this question.
 * @param p2 Button the company pressed
 * @param text Text of the question.
 * @return the cost of this operation or an error
 */
CommandCost CmdGoalQuestionAnswer(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	if (p1 > UINT16_MAX) return CMD_ERROR;
	if (p2 >= GOAL_QUESTION_BUTTON_COUNT) return CMD_ERROR;

	if (_current_company == OWNER_DEITY) {
		/* It has been requested to close this specific question on all clients */
		if (flags & DC_EXEC) CloseWindowById(WC_GOAL_QUESTION, p1);
		return CommandCost();
	}

	if (_networking && _local_company == _current_company) {
		/* Somebody in the same company answered the question. Close the window */
		if (flags & DC_EXEC) CloseWindowById(WC_GOAL_QUESTION, p1);
		if (!_network_server) return CommandCost();
	}

	if (flags & DC_EXEC) {
		Game::NewEvent(new ScriptEventGoalQuestionAnswer(p1, (ScriptCompany::CompanyID)(byte)_current_company, (ScriptGoal::QuestionButton)(1 << p2)));
	}

	return CommandCost();
}
