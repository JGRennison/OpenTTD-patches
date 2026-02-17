/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file maintenance_func.h Functions related to infrastructure maintenance. */

#ifndef MAINTENANCE_FUNC_H
#define MAINTENANCE_FUNC_H

#include "settings_type.h"
#include "core/math_func.hpp"

/**
 * Calculates the scaling factor to use for maintenance costs.
 * @param total_num Number of infrastructure items.
 * @param linear_scale Scaling factor to use in linear mode.
 * @return Scaling factor.
 */
inline uint32_t GetMaintenanceCostScale(uint32_t total_num, uint32_t linear_scale)
{
	if (_settings_game.economy.linear_maintenance) return linear_scale;
	return 1 + IntSqrt(total_num);
}

#endif /* MAINTENANCE_FUNC_H */
