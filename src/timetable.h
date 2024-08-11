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
void SetTimetableParams(int first_param, Ticks ticks, bool long_mode = false);
Ticks ParseTimetableDuration(const char *str);

enum SetTimetableWindowsDirtyFlags {
	STWDF_NONE                       = 0,
	STWDF_SCHEDULED_DISPATCH         = 1 << 0,
	STWDF_ORDERS                     = 1 << 1,
};
DECLARE_ENUM_AS_BIT_SET(SetTimetableWindowsDirtyFlags)
void SetTimetableWindowsDirty(const Vehicle *v, SetTimetableWindowsDirtyFlags flags = STWDF_NONE);

struct TimetableProgress {
	VehicleID id;
	int order_count;
	int order_ticks;
	int cumulative_ticks;

	bool IsValidForSeparation() const { return this->cumulative_ticks >= 0; }
	bool operator<(const TimetableProgress& other) const { return std::tie(this->order_count, this->order_ticks, this->id) < std::tie(other.order_count, other.order_ticks, other.id); }
};

std::vector<TimetableProgress> PopulateSeparationState(const Vehicle *v_start);

struct DispatchSchedule;
std::pair<StateTicks, int> GetScheduledDispatchTime(const DispatchSchedule &ds, StateTicks leave_time);

#endif /* TIMETABLE_H */
