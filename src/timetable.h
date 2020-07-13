/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file timetable.h Functions related to time tabling. */

#ifndef TIMETABLE_H
#define TIMETABLE_H

#include "date_type.h"
#include "vehicle_type.h"
#include <vector>
#include <tuple>

void ShowTimetableWindow(const Vehicle *v);
void UpdateVehicleTimetable(Vehicle *v, bool travelling);
void SetTimetableParams(int first_param, Ticks ticks);

struct TimetableProgress {
	VehicleID id;
	int order_count;
	int order_ticks;
	int cumulative_ticks;

	bool IsValidForSeparation() const { return this->cumulative_ticks >= 0; }
	bool operator<(const TimetableProgress& other) const { return std::tie(this->order_count, this->order_ticks) < std::tie(other.order_count, other.order_ticks); }
};

std::vector<TimetableProgress> PopulateSeparationState(const Vehicle *v_start);

#endif /* TIMETABLE_H */
