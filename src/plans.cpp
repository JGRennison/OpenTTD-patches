/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans.cpp Handling of plans. */

#include "stdafx.h"
#include "plans_base.h"
#include "core/pool_func.hpp"

/** Initialize the plan-pool */
PlanPool _plan_pool("Plan");
INSTANTIATE_POOL_METHODS(Plan)

Plan *_current_plan = nullptr;
Plan *_new_plan = nullptr;
uint64_t _plan_update_counter = 0;
uint64_t _last_plan_visibility_check = 0;
bool _last_plan_visibility_check_result = false;

void PlanLine::UpdateVisualExtents()
{
	if (IsHeadless()) return;

	if (this->tiles.size() < 2) {
		this->viewport_extents = { INT_MAX, INT_MAX, INT_MAX, INT_MAX };
		return;
	}

	int min_x = INT_MAX;
	int max_x = INT_MIN;
	int min_y = INT_MAX;
	int max_y = INT_MIN;

	for (TileIndex t : this->tiles) {
		const int tile_x = TileX(t);
		const int tile_y = TileY(t);
		const int x = tile_y - tile_x;
		const int y = tile_y + tile_x;

		if (x < min_x) min_x = x;
		if (x > max_x) max_x = x;
		if (y < min_y) min_y = y;
		if (y > max_y) max_y = y;
	}

	this->viewport_extents = { (int)(min_x * TILE_SIZE * 2 * ZOOM_BASE), (int)(min_y * TILE_SIZE * ZOOM_BASE),
			(int)((max_x + 1) * TILE_SIZE * 2 * ZOOM_BASE), (int)((max_y + 1) * TILE_SIZE * ZOOM_BASE) };
}

bool Plan::ValidateNewLine()
{
	extern bool AddPlanLine(PlanID plan, TileVector tiles);

	bool ret = false;
	if (this->temp_line->tiles.size() > 1) {
		this->temp_line->MarkDirty();
		this->last_tile = this->temp_line->tiles.back();
		this->SetVisibility(true, false);
		TileVector tiles = std::move(this->temp_line->tiles);
		this->temp_line->Clear();
		ret = AddPlanLine(this->index, std::move(tiles));
	}
	return ret;
}

void UpdateAreAnyPlansVisible()
{
	_last_plan_visibility_check = _plan_update_counter;

	if (_current_plan && _current_plan->temp_line->tiles.size() > 1) {
		_last_plan_visibility_check_result = true;
		return;
	}

	for (const Plan *p : Plan::Iterate()) {
		if (!p->IsVisible()) continue;
		for (const PlanLine *pl : p->lines) {
			if (pl->visible) {
				_last_plan_visibility_check_result = true;
				return;
			}
		}
	}

	_last_plan_visibility_check_result = false;
}
