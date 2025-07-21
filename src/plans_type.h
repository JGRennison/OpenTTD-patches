/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_type.h Types related to planning. */

#ifndef PLANS_TYPE_H
#define PLANS_TYPE_H

#include "core/pool_id_type.hpp"

struct PlanIDTag : public PoolIDTraits<uint16_t, 64000, 0xFFFF> {};
using PlanID = PoolID<PlanIDTag>;
static constexpr PlanID INVALID_PLAN = PlanID::Invalid(); ///< Sentinel for an invalid plan.

struct PlanLine;
struct Plan;

static const uint MAX_LENGTH_PLAN_NAME_CHARS = 128; ///< The maximum length of a plan name in characters including '\0'

#endif /* PLANS_TYPE_H */
