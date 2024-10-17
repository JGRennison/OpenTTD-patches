/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file schdispatch.h Functions related to scheduled dispatch. */

#ifndef SCHDISPATCH_H
#define SCHDISPATCH_H

#include "date_func.h"
#include "order_type.h"
#include "vehicle_type.h"
#include "settings_type.h"

void ShowSchdispatchWindow(const Vehicle *v);
void SchdispatchInvalidateWindows(const Vehicle *v);

const struct LastDispatchRecord *GetVehicleLastDispatchRecord(const Vehicle *v, uint16_t schedule_index);

/**
 * Result type for EvaluateDispatchSlotConditionalOrderGeneral.
 */
struct OrderConditionEvalResult {
	enum class Type : uint8_t {
		Certain,
		Predicted,
	};

private:
	bool result;
	Type type;

public:
	OrderConditionEvalResult(bool result, Type type) : result(result), type(type) {}
	bool GetResult() const { return this->result; }
	bool IsPredicted() const { return this->type == Type::Predicted; }
};

using GetVehicleLastDispatchRecordFunctor = std::function<const LastDispatchRecord *(uint16_t)>;
OrderConditionEvalResult EvaluateDispatchSlotConditionalOrder(const Order *order, std::span<const DispatchSchedule> schedules, StateTicks state_ticks, GetVehicleLastDispatchRecordFunctor get_vehicle_record);

#endif /* SCHDISPATCH_H */
