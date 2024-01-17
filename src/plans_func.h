/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_func.h Functions related to plans. */

#ifndef PLANS_FUNC_H
#define PLANS_FUNC_H

#include "plans_type.h"

extern Plan *_new_plan;
extern Plan *_current_plan;
extern uint64_t _plan_update_counter;
extern uint64_t _last_plan_visibility_check;
extern bool _last_plan_visibility_check_result;

void ShowPlansWindow();
void UpdateAreAnyPlansVisible();

inline bool AreAnyPlansVisible()
{
	if (_plan_update_counter != _last_plan_visibility_check) UpdateAreAnyPlansVisible();
	return _last_plan_visibility_check_result;
}

inline void InvalidatePlanCaches()
{
	_plan_update_counter++;
}

#endif /* PLANS_FUNC_H */
