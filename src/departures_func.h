/* $Id: departures_func.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures_func.h Functions related to departures. */

#ifndef DEPARTURES_FUNC_H
#define DEPARTURES_FUNC_H

#include "station_base.h"
#include "core/smallvec_type.hpp"
#include "departures_type.h"

#include <vector>

DepartureList* MakeDepartureList(StationID station, const std::vector<const Vehicle *> &vehicles, DepartureType type = D_DEPARTURE,
		bool show_vehicles_via = false, bool show_pax = true, bool show_freight = true);

DateTicksScaled GetDeparturesMaxTicksAhead();

#endif /* DEPARTURES_FUNC_H */
