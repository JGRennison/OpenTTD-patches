/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_type.h Types related to planning. */

#ifndef PLANS_TYPE_H
#define PLANS_TYPE_H

#include "stdafx.h"
#include "core/smallvec_type.hpp"
#include "tile_type.h"

typedef uint16 PlanID;
struct PlanLine;
struct Plan;

static const PlanID INVALID_PLAN = 0xFFFF; ///< Sentinel for an invalid plan.

#endif /* PLANS_TYPE_H */
