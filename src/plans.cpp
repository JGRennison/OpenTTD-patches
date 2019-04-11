/* $Id$ */

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
