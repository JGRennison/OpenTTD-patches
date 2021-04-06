/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_cmd.cpp Handling of plan related commands. */

#include "stdafx.h"
#include "command_func.h"
#include "plans_base.h"
#include "plans_func.h"
#include "window_func.h"
#include "company_func.h"
#include "string_func.h"
#include "window_gui.h"
#include "table/strings.h"

/**
 * Create a new plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 owner of the plan
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdAddPlan(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!Plan::CanAllocateItem()) return_cmd_error(STR_ERROR_TOO_MANY_PLANS);
	Owner o = (Owner) p1;
	CommandCost ret = CheckOwnership(o);
	if (ret.Failed()) return ret;
	if (flags & DC_EXEC) {
		_new_plan = new Plan(o);
		if (o == _local_company) {
			_new_plan->SetVisibility(true);
			Window *w = FindWindowById(WC_PLANS, 0);
			if (w) w->InvalidateData(INVALID_PLAN, false);
		}
	}
	return CommandCost();
}

/**
 * Create a new line in a plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 plan id
 * @param p2 number of nodes
 * @param text list of tile indexes that compose the line
 * @param binary_length binary length of text
 * @return the cost of this operation or an error
 */
CommandCost CmdAddPlanLine(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, uint32 binary_length)
{
	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;
	if (p2 > (MAX_CMD_TEXT_LENGTH / sizeof(TileIndex))) return_cmd_error(STR_ERROR_TOO_MANY_NODES);
	if (!text || binary_length != p2 * 4) return CMD_ERROR;
	if (flags & DC_EXEC) {
		PlanLine *pl = p->NewLine();
		if (!pl) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_LINES);
		if (!pl->Import((const TileIndex *) text, p2)) {
			delete pl;
			p->lines.pop_back();
			return CMD_ERROR;
		}
		pl->UpdateVisualExtents();
		if (p->IsListable()) {
			pl->SetVisibility(p->visible);
			if (p->visible) pl->MarkDirty();
			Window *w = FindWindowById(WC_PLANS, 0);
			if (w) w->InvalidateData(INVALID_PLAN, false);
		}
	}
	return CommandCost();
}

/**
 * Edit the visibility of a plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 plan id
 * @param p2 visibility
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangePlanVisibility(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;
	if (flags & DC_EXEC) {
		p->visible_by_all = p2 != 0;
		Window *w = FindWindowById(WC_PLANS, 0);
		if (w) w->InvalidateData(INVALID_PLAN, false);
	}
	return CommandCost();
}

/**
 * Edit the colour of a plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 plan id
 * @param p2 colour
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdChangePlanColour(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	if (p2 >= COLOUR_END) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;
	if (flags & DC_EXEC) {
		p->colour = (Colours)p2;
		Window *w = FindWindowById(WC_PLANS, 0);
		if (w) w->InvalidateData(INVALID_PLAN, false);
		for (const PlanLine *line : p->lines) {
			if (line->visible) line->MarkDirty();
		}
		if (p->temp_line) p->temp_line->MarkDirty();
	}
	return CommandCost();
}

/**
 * Delete a plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 plan id
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemovePlan(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;
	if (flags & DC_EXEC) {
		if (p->IsListable()) {
			p->SetVisibility(false);
			Window *w = FindWindowById(WC_PLANS, 0);
			if (w) w->InvalidateData(p->index, false);
		}
		if (p == _current_plan) _current_plan = nullptr;
		delete p;
	}
	return CommandCost();
}

/**
 * Remove a line from a plan.
 * @param tile unused
 * @param flags type of operation
 * @param p1 plan id
 * @param p2 line id
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdRemovePlanLine(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;
	if (p2 >= p->lines.size()) return CMD_ERROR;
	if (flags & DC_EXEC) {
		PlanLineVector::iterator it = p->lines.begin();
		std::advance(it, p2);
		(*it)->SetVisibility(false);
		delete *it;
		p->lines.erase(it);
		if (p->IsListable()) {
			Window *w = FindWindowById(WC_PLANS, 0);
			if (w) w->InvalidateData(p->index, false);
		}
	}
	return CommandCost();
}

/**
* Give a custom name to your plan
* @param tile unused
* @param flags type of operation
* @param p1 ID of plan to name
* @param p2 unused
* @param text the new name
* @return the cost of this operation or an error
*/
CommandCost CmdRenamePlan(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (text == nullptr) return CMD_ERROR;

	Plan *p = Plan::GetIfValid(p1);
	if (p == nullptr) return CMD_ERROR;
	CommandCost ret = CheckOwnership(p->owner);
	if (ret.Failed()) return ret;

	if (Utf8StringLength(text) >= MAX_LENGTH_PLAN_NAME_CHARS) return CMD_ERROR;

	if (flags & DC_EXEC) {
		p->name = text;
		InvalidateWindowClassesData(WC_PLANS);
	}

	return CommandCost();
}
