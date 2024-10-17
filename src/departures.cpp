/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures.cpp Scheduled departures from a station. */

#include "stdafx.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "window_gui.h"
#include "timetable.h"
#include "vehiclelist.h"
#include "company_base.h"
#include "date_func.h"
#include "departures_gui.h"
#include "station_base.h"
#include "vehicle_gui_base.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "order_base.h"
#include "settings_type.h"
#include "date_type.h"
#include "company_type.h"
#include "cargo_type.h"
#include "departures_func.h"
#include "departures_type.h"
#include "schdispatch.h"
#include "tracerestrict.h"
#include "scope.h"
#include "3rdparty/cpp-btree/btree_set.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include <vector>
#include <algorithm>

static constexpr Ticks INVALID_DEPARTURE_TICKS = INT32_MIN;

/* A cache of used departure time for scheduled dispatch in departure time calculation */
using ScheduledDispatchCache = btree::btree_map<const DispatchSchedule *, btree::btree_set<StateTicks>>;
using ScheduledDispatchVehicleRecords = btree::btree_map<std::pair<uint, VehicleID>, LastDispatchRecord>;

CallAtTargetID CallAtTargetID::FromOrder(const Order *order)
{
	uint32_t id = order->GetDestination();
	if (order->IsType(OT_GOTO_DEPOT)) id |= DEPOT_TAG;
	return CallAtTargetID(id);
}

struct ArrivalHistoryEntry {
	const Order *order;
	Ticks offset;
};

/** A scheduled order. */
struct OrderDate {
	const Order *order;     ///< The order
	const Vehicle *v;       ///< The vehicle carrying out the order
	Ticks expected_tick;    ///< The tick on which the order is expected to complete
	Ticks lateness;         ///< How late this order is expected to finish
	DepartureStatus status; ///< Whether the vehicle has arrived to carry out the order yet
	bool have_veh_dispatch_conditionals; ///< Whether vehicle dispatch conditionals are present
	bool arrivals_complete;       ///< arrival history is complete
	Ticks scheduled_waiting_time; ///< Scheduled waiting time if scheduled dispatch is used
	ScheduledDispatchVehicleRecords dispatch_records; ///< Dispatch records for this vehicle
	std::vector<ArrivalHistoryEntry> arrival_history;

	inline Ticks EffectiveWaitingTime() const
	{
		if (this->scheduled_waiting_time > 0) {
			return this->scheduled_waiting_time;
		} else {
			return this->order->GetWaitTime();
		}
	}

	Ticks GetQueueTick(DepartureType type) const
	{
		Ticks tick = this->expected_tick - this->lateness;
		if (type == D_ARRIVAL) {
			tick -= this->EffectiveWaitingTime();
		}
		return tick;
	}
};

struct OrderDateQueueItem {
	uint order_data_index;
	Ticks tick;

	bool operator<(const OrderDateQueueItem &other) const
	{
		/* Sort in opposite order */
		return std::tie(this->tick, this->order_data_index) > std::tie(other.tick, other.order_data_index);
	}
};

inline bool IsStationOrderWithWait(const Order *order)
{
	return ((order->GetWaitTime() != 0 || order->IsWaitTimetabled()) && !(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION));
}

template <typename LOAD_FILTER>
bool IsArrivalDepartureTest(DepartureCallingSettings settings, const Order *order, LOAD_FILTER load_filter)
{
	if (order->GetType() == OT_GOTO_STATION) {
		if (!settings.DepartureNoLoadTest() && !load_filter(order)) return false;
		return settings.AllowVia() || IsStationOrderWithWait(order);
	} else if (order->GetType() == OT_GOTO_WAYPOINT) {
		if (settings.AllowVia()) return true;
		return (order->GetWaitTime() != 0 || order->IsWaitTimetabled());
	} else {
		return true;
	}
}

static bool DepartureLoadFilter(const Order *order)
{
	return order->GetLoadType() != OLFB_NO_LOAD;
}

static bool ArrivalLoadFilter(const Order *order)
{
	return order->GetUnloadType() != OUFB_NO_UNLOAD;
}

bool DepartureCallingSettings::IsDeparture(const Order *order, const DepartureOrderDestinationDetector &source) const
{
	if (!source.OrderMatches(order)) return false;
	return IsArrivalDepartureTest(*this, order, DepartureLoadFilter);
}

bool DepartureCallingSettings::IsArrival(const Order *order, const DepartureOrderDestinationDetector &source) const
{
	if (!source.OrderMatches(order)) return false;
	return IsArrivalDepartureTest(*this, order, ArrivalLoadFilter);
}

DepartureShowAs DepartureCallingSettings::GetShowAsType(const Order *order, DepartureType type) const
{
	if (this->CheckShowAsViaType() && order->IsType(OT_GOTO_STATION) && !IsStationOrderWithWait(order)) return DSA_VIA;
	if (order->IsType(OT_GOTO_WAYPOINT)) return order->IsWaitTimetabled() ? DSA_NO_LOAD : DSA_VIA;
	if (order->IsType(OT_GOTO_DEPOT)) return DSA_NO_LOAD;
	if (order->IsType(OT_GOTO_STATION)) {
		if (type == D_DEPARTURE && !DepartureLoadFilter(order)) return DSA_NO_LOAD;
		if (type == D_ARRIVAL && !ArrivalLoadFilter(order)) return DSA_NO_LOAD;
	}
	return DSA_NORMAL;
}

static uint8_t GetNonScheduleDepartureConditionalOrderMode(const Order *order, const Vehicle *v, StateTicks eval_tick)
{
	if (order->GetConditionVariable() == OCV_UNCONDITIONALLY) return 1;
	if (order->GetConditionVariable() == OCV_TIME_DATE) {
		int value = GetTraceRestrictTimeDateValueFromStateTicks(static_cast<TraceRestrictTimeDateValueField>(order->GetConditionValue()), eval_tick);
		return OrderConditionCompare(order->GetConditionComparator(), value, order->GetXData()) ? 1 : 2;
	}

	return _settings_client.gui.departure_conditionals;
}

static uint8_t GetDepartureConditionalOrderMode(const Order *order, const Vehicle *v, StateTicks eval_tick, const ScheduledDispatchVehicleRecords &records)
{
	if (order->GetConditionVariable() == OCV_DISPATCH_SLOT) {
		auto get_vehicle_records = [&](uint16_t schedule_index) -> const LastDispatchRecord * {
			auto record = records.find(std::make_pair(schedule_index, v->index));
			if (record != records.end()) {
				/* ScheduledDispatchVehicleRecords contains a last dispatch entry, use that instead of the one stored in the vehicle */
				return &(record->second);
			} else {
				return GetVehicleLastDispatchRecord(v, schedule_index);
			}
		};
		return EvaluateDispatchSlotConditionalOrder(order, v->orders->GetScheduledDispatchScheduleSet(), eval_tick, get_vehicle_records).GetResult() ? 1 : 2;
	} else {
		return GetNonScheduleDepartureConditionalOrderMode(order, v, eval_tick);
	}
}

static bool VehicleSetNextDepartureTime(Ticks *previous_departure, Ticks *waiting_time, const StateTicks state_ticks_base,
		const Vehicle *v, const Order *order, bool arrived_at_timing_point, ScheduledDispatchCache &dept_schedule_last, ScheduledDispatchVehicleRecords &records)
{
	if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
		auto is_current_implicit_order = [&v](const Order *o) -> bool {
			if (v->cur_implicit_order_index >= v->orders->GetNumOrders()) return false;
			return v->orders->GetOrderAt(v->cur_implicit_order_index) == o;
		};

		/* This condition means that we want departure time for the dispatch order */
		/* but not if the vehicle has arrived at the dispatch order because the timetable is already shifted */
		if (order->IsScheduledDispatchOrder(true) && !(arrived_at_timing_point && is_current_implicit_order(order))) {
			const DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(order->GetDispatchScheduleIndex());

			StateTicks actual_departure         = INT64_MAX;
			int actual_slot_index               = -1;
			const StateTicks begin_time         = ds.GetScheduledDispatchStartTick();
			const uint32_t dispatch_duration    = ds.GetScheduledDispatchDuration();
			const int32_t max_delay             = ds.GetScheduledDispatchDelay();

			/* Earliest possible departure according to the schedule */
			StateTicks earliest_departure = begin_time;
			if (ds.GetScheduledDispatchLastDispatch() != INVALID_SCHEDULED_DISPATCH_OFFSET) {
				earliest_departure += ds.GetScheduledDispatchLastDispatch();
			} else {
				earliest_departure--;
			}

			/* Earliest possible departure according to vehicle current timetable */
			const StateTicks ready_to_depart_time = state_ticks_base + *previous_departure + order->GetTravelTime() + order->GetTimetabledWait();
			if (earliest_departure + max_delay < ready_to_depart_time) {
				earliest_departure = ready_to_depart_time - max_delay - 1;
				/* -1 because this number is actually a moment before actual departure */
			}

			btree::btree_set<StateTicks> &slot_cache = dept_schedule_last[&ds];

			/* Find next available slots */
			int slot_idx = 0;
			for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
				int this_slot = slot_idx++;

				if (slot.offset >= dispatch_duration) continue;

				StateTicks current_departure = begin_time + slot.offset;
				while (current_departure <= earliest_departure) {
					current_departure += dispatch_duration;
				}

				/* Make sure the slots has not already been used previously in this departure board calculation */
				while (slot_cache.count(current_departure) > 0) {
					if (HasBit(slot.flags, DispatchSlot::SDSF_REUSE_SLOT)) {
						/* Allow re-use of this slot if it's the last seen */
						if (*slot_cache.rbegin() == current_departure) break;
					}
					current_departure += dispatch_duration;
				}

				if (actual_departure > current_departure) {
					actual_departure = current_departure;
					actual_slot_index = this_slot;
				}
			}

			if (actual_departure == INT64_MAX) {
				/* Failed to find a dispatch slot for this departure at all, the schedule is invalid/empty.
				 * Just treat it as a non-dispatch order. */
				*previous_departure += order->GetTravelTime() + order->GetWaitTime();
				*waiting_time = 0;
				return false;
			}

			*waiting_time = (actual_departure - state_ticks_base).AsTicks() - *previous_departure - order->GetTravelTime();
			*previous_departure = (actual_departure - state_ticks_base).AsTicks();
			if (!ds.GetScheduledDispatchReuseSlots()) {
				slot_cache.insert(actual_departure);
			}

			extern LastDispatchRecord MakeLastDispatchRecord(const DispatchSchedule &ds, StateTicks slot, int slot_index);
			records[std::make_pair((uint)order->GetDispatchScheduleIndex(), v->index)] = MakeLastDispatchRecord(ds, actual_departure, actual_slot_index);

			/* Return true means that vehicle lateness should be clear from this point onward */
			return true;
		}

		/* This is special case for proper calculation of arrival time. */
		if (arrived_at_timing_point && v->cur_implicit_order_index < v->orders->GetNumOrders() && v->orders->GetOrderAt(v->cur_implicit_order_index)->IsScheduledDispatchOrder(true)) {
			*previous_departure += order->GetTravelTime() + order->GetWaitTime();
			*waiting_time = -v->lateness_counter + order->GetWaitTime();
			return false;
		}
	} /* if vehicle is on scheduled dispatch */

	/* Not using schedule for this departure time */
	*previous_departure += order->GetTravelTime() + order->GetWaitTime();
	*waiting_time = 0;
	return false;
}

static void ScheduledDispatchDepartureLocalFix(DepartureList &departure_list)
{
	/* Separate departure by each shared order group */
	btree::btree_map<uint32_t, std::vector<Departure *>> separated_departure;
	for (auto &departure : departure_list) {
		separated_departure[departure->vehicle->orders->index].push_back(departure.get());
	}

	for (auto& pair : separated_departure) {
		std::vector<Departure *> &d_list = pair.second;

		/* If the group is scheduled dispatch, then */
		if (HasBit(d_list[0]->vehicle->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
			/* Separate departure time and sort them ascendently */
			std::vector<StateTicks> departure_time_list;
			for (const auto& d : d_list) {
				departure_time_list.push_back(d->scheduled_tick);
			}
			std::sort(departure_time_list.begin(), departure_time_list.end());

			/* Sort the departure list by arrival time */
			std::sort(d_list.begin(), d_list.end(), [](const Departure * const &a, const Departure * const &b) -> bool {
				StateTicks arr_a = a->scheduled_tick - a->EffectiveWaitingTime();
				StateTicks arr_b = b->scheduled_tick - b->EffectiveWaitingTime();
				return arr_a < arr_b;
			});

			/* Re-assign them sequentially */
			for (size_t i = 0; i < d_list.size(); i++) {
				const StateTicks arrival = d_list[i]->scheduled_tick - d_list[i]->EffectiveWaitingTime();
				d_list[i]->scheduled_waiting_time = (departure_time_list[i] - arrival).AsTicks();
				d_list[i]->scheduled_tick = departure_time_list[i];

				if (d_list[i]->scheduled_waiting_time == (Ticks)d_list[i]->order->GetWaitTime()) {
					d_list[i]->scheduled_waiting_time = 0;
				}
			}
		}
	}

	/* Re-sort the departure list */
	std::sort(departure_list.begin(), departure_list.end(), [](std::unique_ptr<Departure> &a, std::unique_ptr<Departure> &b) -> bool {
		return a->scheduled_tick < b->scheduled_tick;
	});
}

static void ScheduledDispatchSmartTerminusDetection(DepartureList &departure_list, Ticks loop_duration = 0)
{
	btree::btree_map<CallAtTargetID, StateTicks> earliest_seen;

	auto check_departure = [&](Departure *d) {
		size_t calling_at_size = d->calling_at.size();

		/* If the terminus has already been moved back, find the right starting offset */
		while (calling_at_size >= 2) {
			if (d->terminus == d->calling_at[calling_at_size - 1]) break;
			calling_at_size--;
		}

		while (calling_at_size >= 2) {
			if (d->terminus.scheduled_tick != 0) {
				auto iter = earliest_seen.find(d->terminus.target);
				if (iter != earliest_seen.end() && iter->second <= d->terminus.scheduled_tick) {
					/* Terminus can be reached at same or earlier time on a later vehicle */
					calling_at_size--;
					size_t new_terminus_offset = calling_at_size - 1;
					d->terminus = d->calling_at[new_terminus_offset];

					auto remove_via = [&](StationID st) {
						if (d->via2 == st) d->via2 = INVALID_STATION;
						if (d->via == st) {
							d->via = d->via2;
							d->via2 = INVALID_STATION;
						}
					};
					if (d->terminus.target.IsStationID()) {
						remove_via(d->terminus.target.GetStationID());
					}
					for (const RemoveVia &rv : d->remove_vias) {
						if (rv.calling_at_offset == new_terminus_offset) {
							remove_via(rv.via);
						}
					}
					continue; // Try again with new terminus
				}
			}
			break;
		}

		for (const CallAt &c : d->calling_at) {
			if (c.scheduled_tick != 0) {
				StateTicks &seen = earliest_seen[c.target];
				if (seen == 0 || c.scheduled_tick < seen) seen = c.scheduled_tick;
			}
		}
	};

	for (auto iter = departure_list.rbegin(); iter != departure_list.rend(); ++iter) {
		Departure *d = iter->get();
		if (d->show_as != DSA_NORMAL) continue;

		check_departure(d);
	}

	if (loop_duration > 0) {
		/* Second pass: offset all earliest seen by the loop duration, and run through again.
		 * This is so that departures at the end can be compared with departures at the start of the next schedule period/day. */
		for (auto &it : earliest_seen) {
			it.second += loop_duration;
		}

		for (auto iter = departure_list.rbegin(); iter != departure_list.rend(); ++iter) {
			Departure *d = iter->get();
			if (d->show_as != DSA_NORMAL) continue;

			check_departure(d);
		}
	}
}

static bool IsVehicleUsableForDepartures(const Vehicle *v, DepartureCallingSettings calling_settings)
{
	if (v->GetNumOrders() == 0) return false;
	if (calling_settings.ShowPax() != calling_settings.ShowFreight()) {
		bool carries_passengers = false;

		const Vehicle *u = v;
		while (u != nullptr) {
			if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CC_PASSENGERS)) {
				carries_passengers = true;
				break;
			}
			u = u->Next();
		}

		if (carries_passengers != calling_settings.ShowPax()) {
			return false;
		}
	}

	/* If the vehicle is stopped in a depot, ignore it. */
	if (v->IsStoppedInDepot()) {
		return false;
	}

	return true;
}

static void GetDepartureCandidateOrderDatesFromVehicle(std::vector<OrderDate> &next_orders, const Vehicle *v, const DepartureOrderDestinationDetector &source, const DepartureType type,
		DepartureCallingSettings calling_settings, const Ticks max_ticks, ScheduledDispatchCache &schdispatch_last_planned_dispatch)
{
	if (!IsVehicleUsableForDepartures(v, calling_settings)) return;

	ScheduledDispatchVehicleRecords dispatch_records;
	std::vector<ArrivalHistoryEntry> arrival_history;

	const StateTicks state_ticks_base = _state_ticks;

	const Order *order = v->GetOrder(v->cur_implicit_order_index % v->GetNumOrders());
	if (order == nullptr) return;
	Ticks start_ticks = -((Ticks)v->current_order_time);
	if (v->cur_timetable_order_index != INVALID_VEH_ORDER_ID && v->cur_timetable_order_index != v->cur_real_order_index) {
		/* vehicle is taking a conditional order branch, adjust start time to compensate */
		const Order *real_current_order = v->GetOrder(v->cur_real_order_index);
		const Order *real_timetable_order = v->GetOrder(v->cur_timetable_order_index);
		if (real_timetable_order->IsType(OT_CONDITIONAL)) {
			start_ticks += (real_timetable_order->GetWaitTime() - real_current_order->GetTravelTime());
		} else {
			/* This can also occur with implicit orders, when there are no real orders, do nothing */
		}
	}
	DepartureStatus status = D_TRAVELLING;
	bool should_reset_lateness = false;
	Ticks waiting_time = 0;

	/* If the vehicle is heading for a depot to stop there, then its departures are cancelled. */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDepotActionType() & ODATFB_HALT) {
		status = D_CANCELLED;
	}

	bool require_travel_time = true;
	if (v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) {
		/* Account for the vehicle having reached the current order and being in the loading phase. */
		status = D_ARRIVED;
		start_ticks -= order->GetTravelTime() + ((v->lateness_counter < 0) ? v->lateness_counter : 0);
		require_travel_time = false;
	}

	bool have_veh_dispatch_conditionals = false;
	for (const Order *order : v->Orders()) {
		if (order->IsType(OT_CONDITIONAL) && order->GetConditionVariable() == OCV_DISPATCH_SLOT && GB(order->GetConditionValue(), ODCB_SRC_START, ODCB_SRC_COUNT) == ODCS_VEH) {
			have_veh_dispatch_conditionals = true;
		}
	}

	/* Loop through the vehicle's orders until we've found a suitable order or we've determined that no such order exists. */
	/* We only need to consider each order at most once. */
	for (int i = v->GetNumOrders() * (have_veh_dispatch_conditionals ? 8 : 1); i > 0; --i) {
		if (VehicleSetNextDepartureTime(&start_ticks, &waiting_time, state_ticks_base, v, order, status == D_ARRIVED, schdispatch_last_planned_dispatch, dispatch_records)) {
			should_reset_lateness = true;
		}

		/* If the order is a conditional branch, handle it. */
		if (order->IsType(OT_CONDITIONAL)) {
			switch (GetDepartureConditionalOrderMode(order, v, state_ticks_base + start_ticks, dispatch_records)) {
					case 0: {
						/* Give up */
						break;
					}
					case 1: {
						/* Take the branch */
						if (status != D_CANCELLED) {
							status = D_TRAVELLING;
						}
						order = v->GetOrder(order->GetConditionSkipToOrder());
						if (order == nullptr) {
							break;
						}

						start_ticks -= order->GetTravelTime();
						require_travel_time = false;
						continue;
					}
					case 2: {
						/* Do not take the branch */
						if (status != D_CANCELLED) {
							status = D_TRAVELLING;
						}
						start_ticks -= order->GetWaitTime(); /* Added previously in VehicleSetNextDepartureTime */
						order = v->orders->GetNext(order);
						require_travel_time = true;
						continue;
					}
			}
			break;
		}

		/* If the scheduled departure date is too far in the future, stop. */
		if (start_ticks - v->lateness_counter > max_ticks) {
			break;
		}

		/* If an order has a 0 travel time, and it's not explictly set, then stop. */
		if (require_travel_time && order->GetTravelTime() == 0 && !order->IsTravelTimetabled() && !order->IsType(OT_IMPLICIT)) {
			break;
		}

		/* If the vehicle will be stopping at and loading from this station, and its wait time is not zero, then it is a departure. */
		/* If the vehicle will be stopping at and unloading at this station, and its wait time is not zero, then it is an arrival. */
		if ((type == D_DEPARTURE && calling_settings.IsDeparture(order, source)) ||
				(type == D_ARRIVAL && calling_settings.IsArrival(order, source))) {
			/* If the departure was scheduled to have already begun and has been cancelled, do not show it. */
			if (start_ticks < 0 && status == D_CANCELLED) {
				break;
			}

			OrderDate od{};
			od.order = order;
			od.v = v;
			/* We store the expected date for now, so that vehicles will be shown in order of expected time. */
			od.expected_tick = start_ticks;
			od.lateness = v->lateness_counter > 0 ? v->lateness_counter : 0;
			od.status = status;
			od.have_veh_dispatch_conditionals = have_veh_dispatch_conditionals;
			od.scheduled_waiting_time = waiting_time;
			od.dispatch_records = std::move(dispatch_records);
			od.arrivals_complete = false;
			od.arrival_history = std::move(arrival_history);

			/* Reset lateness if timing is from scheduled dispatch */
			if (should_reset_lateness) {
				od.lateness = 0;
			}

			/* If we are early, use the scheduled date as the expected date. We also take lateness to be zero. */
			if (!should_reset_lateness && v->lateness_counter < 0 && !(v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING))) {
				od.expected_tick -= v->lateness_counter;
			}

			next_orders.push_back(std::move(od));

			/* We're done with this vehicle. */
			break;
		} else {
			if (type == D_ARRIVAL) {
				arrival_history.push_back({ order, start_ticks });
			}

			/* Go to the next order in the list. */
			if (status != D_CANCELLED) {
				status = D_TRAVELLING;
			}
			order = v->orders->GetNext(order);
			require_travel_time = true;
		}
	}
}

static bool IsCallingPointTargetOrder(const Order *order)
{
	if ((order->IsType(OT_GOTO_STATION) || order->IsType(OT_IMPLICIT)) && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) return true;
	if (order->IsType(OT_GOTO_WAYPOINT) && order->IsWaitTimetabled()) return true;
	if (order->IsType(OT_GOTO_DEPOT) && ((order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) == 0) && (order->IsWaitTimetabled() || (order->GetDepotActionType() & ODATFB_HALT) != 0)) return true;
	return false;
}

struct DepartureViaTerminusState {
	/* We keep track of potential via stations along the way. If we call at a station immediately after going via it, then it is the via station. */
	StationID candidate_via = INVALID_STATION;
	StationID pending_via = INVALID_STATION;
	StationID pending_via2 = INVALID_STATION;

	/* We only need to consider each order at most once. */
	bool found_terminus = false;

	bool CheckOrder(const Vehicle *v, Departure *d, const Order *order, DepartureOrderDestinationDetector source, DepartureCallingSettings calling_settings);
	bool HandleCallingPoint(Departure *d, const Order *order, CallAt c, DepartureCallingSettings calling_settings);
};

/**
 * Check the order terminus and via states.
 * @return true to stop at this order
 */
bool DepartureViaTerminusState::CheckOrder(const Vehicle *v, Departure *d, const Order *order, DepartureOrderDestinationDetector source, DepartureCallingSettings calling_settings)
{
	/* If we reach the original station again, then use it as the terminus. */
	if (order->GetType() == OT_GOTO_STATION &&
			source.OrderMatches(order) &&
			(order->GetUnloadType() != OUFB_NO_UNLOAD || calling_settings.ShowAllStops()) &&
			(((order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) || ((d->order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) != 0))) {
		/* If we're not calling anywhere, then skip this departure. */
		this->found_terminus = (d->calling_at.size() > 0);
		return true;
	} else if (order->GetType() == OT_GOTO_WAYPOINT && source.OrderMatches(order)) {
		/* If we're not calling anywhere, then skip this departure. */
		this->found_terminus = (d->calling_at.size() > 0);
		return true;
	}

	/* Check if we're going via this station. */
	if ((order->GetNonStopType() == ONSF_NO_STOP_AT_ANY_STATION ||
			order->GetNonStopType() == ONSF_NO_STOP_AT_DESTINATION_STATION) &&
			order->GetType() == OT_GOTO_STATION &&
			d->via == INVALID_STATION) {
		this->candidate_via = (StationID)order->GetDestination();
	}

	if (order->GetType() == OT_LABEL && order->GetLabelSubType() == OLST_DEPARTURES_VIA && d->via == INVALID_STATION && this->pending_via == INVALID_STATION) {
		this->pending_via = (StationID)order->GetDestination();
		const Order *next = v->orders->GetNext(order);
		if (next->GetType() == OT_LABEL && next->GetLabelSubType() == OLST_DEPARTURES_VIA && (StationID)next->GetDestination() != this->pending_via) {
			this->pending_via2 = (StationID)next->GetDestination();
		}
	}

	if (order->GetType() == OT_LABEL && order->GetLabelSubType() == OLST_DEPARTURES_REMOVE_VIA && !d->calling_at.empty()) {
		d->remove_vias.push_back({ (StationID)order->GetDestination(), (uint)(d->calling_at.size() - 1) });
	}

	return false;
}

bool DepartureViaTerminusState::HandleCallingPoint(Departure *d, const Order *order, CallAt c, DepartureCallingSettings calling_settings)
{
	if (!IsCallingPointTargetOrder(order)) return false;

	if (order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_GOTO_DEPOT)) {
		if (!calling_settings.ShowAllStops()) return false;
	} else {
		if (!calling_settings.ShowAllStops() && order->GetUnloadType() == OUFB_NO_UNLOAD) return false;
	}

	/* If this order's station is already in the calling, then the previous called at station is the terminus. */
	if (std::find(d->calling_at.begin(), d->calling_at.end(), c) != d->calling_at.end()) {
		this->found_terminus = true;
		return true;
	}

	d->terminus = c;
	d->calling_at.push_back(c);

	if (order->IsType(OT_GOTO_DEPOT)) return (order->GetDepotActionType() & ODATFB_HALT) != 0;

	/* Add the station to the calling at list and make it the candidate terminus. */
	if (d->via == INVALID_STATION && pending_via != INVALID_STATION) {
		d->via = this->pending_via;
		d->via2 = this->pending_via2;
	}
	if (d->via == INVALID_STATION && this->candidate_via == (StationID)order->GetDestination()) {
		d->via = (StationID)order->GetDestination();
	}

	/* If we unload all at this station and departure load tests are not disabled, then it is the terminus. */
	if (order->GetType() == OT_GOTO_STATION && order->GetUnloadType() == OUFB_UNLOAD && !calling_settings.DepartureNoLoadTest()) {
		if (d->calling_at.size() > 0) {
			this->found_terminus = true;
		}
		return true;
	}

	return false;
}

/**
 * Process arrival history, returns true if a valid arrival was found.
 * @param d Departure.
 * @param arrival_history Arrival history up to but not including the source order, offset field has arbitrary base, and refers to order departure time.
 * @param arrival_tick Arrival time at the source order, in the same arbitrary base as arrival_history.
 * @param source Source order detector.
 * @param calling_settings Calling at settings.
 * @return true is an arrival was found.
 */
static bool ProcessArrivalHistory(Departure *d, std::span<ArrivalHistoryEntry> arrival_history, Ticks arrival_tick, DepartureOrderDestinationDetector source, DepartureCallingSettings calling_settings)
{
	/* Not that d->scheduled_tick is an arrival time, not a departure time as in arrival_history.
	 * arrival_offset is thus usable to transform either arrival or departure times in the arrival_history timebase to StateTicks. */
	const StateTicks arrival_offset = d->scheduled_tick - arrival_tick;

	std::vector<std::pair<StationID, uint>> possible_origins;

	for (uint i = 0; i < (uint)arrival_history.size(); i++) {
		const Order *o = arrival_history[i].order;

		if (IsCallingPointTargetOrder(o)) {
			if (source.StationMatches(o->GetDestination())) {
				/* Same as source order, remove all possible origins */
				possible_origins.clear();
			} else if (!calling_settings.ShowAllStops() && o->IsType(OT_GOTO_STATION) && o->GetLoadType() == OLFB_NO_LOAD && (o->GetUnloadType() & (OUFB_TRANSFER | OUFB_UNLOAD)) != 0) {
				/* All cargo unloaded, remove all possible origins */
				possible_origins.clear();
			} else {
				/* Remove all possible origins of this station */
				for (auto &item : possible_origins) {
					if (item.first == o->GetDestination()) {
						item.first = INVALID_STATION;
					}
				}

				if (o->IsType(OT_GOTO_WAYPOINT) || o->IsType(OT_GOTO_DEPOT)) {
					if (calling_settings.ShowAllStops()) possible_origins.push_back({ o->GetDestination(), i });
				} else {
					if (calling_settings.ShowAllStops() || o->GetLoadType() != OLFB_NO_LOAD) possible_origins.push_back({ o->GetDestination(), i });
				}
			}
		}
	}

	ArrivalHistoryEntry origin = { nullptr, 0 };
	uint origin_arrival_history_index = 0;
	for (const auto &item : possible_origins) {
		if (item.first != INVALID_STATION) {
			origin_arrival_history_index = item.second;
			origin = arrival_history[item.second];
			break;
		}
	}
	possible_origins.clear();
	if (origin.order != nullptr) {
		bool check_no_load_mode = false;
		if (calling_settings.ShowAllStops() && d->show_as == DSA_NORMAL) {
			check_no_load_mode = true;
			d->show_as = DSA_NO_LOAD;
		}
		auto check_order = [&](const Order *o) {
			if (check_no_load_mode && o->IsType(OT_GOTO_STATION) && o->GetLoadType() != OLFB_NO_LOAD) {
				d->show_as = DSA_NORMAL;
				check_no_load_mode = false;
			}
		};
		check_order(origin.order);

		auto make_call_at = [&](const ArrivalHistoryEntry &entry) -> CallAt {
			if (entry.offset == INVALID_DEPARTURE_TICKS) {
				return CallAt(entry.order);
			} else {
				return CallAt(entry.order, entry.offset + arrival_offset);
			}
		};

		for (uint i = origin_arrival_history_index + 1; i < (uint)arrival_history.size(); i++) {
			const Order *o = arrival_history[i].order;
			if (IsCallingPointTargetOrder(o)) {
				check_order(o);
				if (o->IsType(OT_GOTO_STATION) && (o->GetLoadType() != OLFB_NO_LOAD || calling_settings.ShowAllStops())) {
					d->calling_at.push_back(make_call_at(arrival_history[i]));
				} else if ((o->IsType(OT_GOTO_WAYPOINT) || o->IsType(OT_GOTO_DEPOT))&& calling_settings.ShowAllStops()) {
					d->calling_at.push_back(make_call_at(arrival_history[i]));
				}
			}
		}

		d->terminus = make_call_at(origin);

		return true;
	}

	return false;
}

/**
 * Compute an up-to-date list of departures for a station.
 * @param source the station/etc to compute the departures of
 * @param vehicles set of all the vehicles stopping at this station, of all vehicles types that we are interested in
 * @param type the type of departures to get (departures or arrivals)
 * @param calling_settings departure calling settings
 * @return a list of departures, which is empty if an error occurred
 */
static DepartureList MakeDepartureListLiveMode(DepartureOrderDestinationDetector source, const std::vector<const Vehicle *> &vehicles, DepartureType type, DepartureCallingSettings calling_settings)
{
	/* This function is the meat of the departure boards functionality. */
	/* As an overview, it works by repeatedly considering the best possible next departure to show. */
	/* By best possible we mean the one expected to arrive at the station first. */
	/* However, we do not consider departures whose scheduled time is too far in the future, even if they are expected before some delayed ones. */
	/* This code can probably be made more efficient. I haven't done so in order to keep both its (relative) simplicity and my (relative) sanity. */
	/* Having written that, it's not exactly slow at the moment. */

	if (!calling_settings.ShowPax() && !calling_settings.ShowFreight()) return {};

	/* The list of departures which will be returned as a result. */
	std::vector<std::unique_ptr<Departure>> result;

	/* A list of the next scheduled orders to be considered for inclusion in the departure list. */
	std::vector<OrderDate> next_orders;

	/* The maximum possible date for departures to be scheduled to occur. */
	const Ticks max_ticks = GetDeparturesMaxTicksAhead();

	const StateTicks state_ticks_base = _state_ticks;

	/* Cache for scheduled departure time */
	ScheduledDispatchCache schdispatch_last_planned_dispatch;

	/* Get the first order for each vehicle for the station we're interested in that doesn't have No Loading set. */
	/* We find the least order while we're at it. */
	for (const Vehicle *v : vehicles) {
		GetDepartureCandidateOrderDatesFromVehicle(next_orders, v, source, type, calling_settings, max_ticks, schdispatch_last_planned_dispatch);
	}

	/* No suitable orders found? Then stop. */
	if (next_orders.size() == 0) {
		return result;
	}

	/* Priority queue/heap */
	std::vector<OrderDateQueueItem> order_queue;
	order_queue.reserve(next_orders.size());
	for (uint i = 0; i < (uint)next_orders.size(); i++) {
		order_queue.push_back({ i, next_orders[i].GetQueueTick(type) });
	}
	std::make_heap(order_queue.begin(), order_queue.end());

	/* We now find as many departures as we can. It's a little involved so I'll try to explain each major step. */
	/* The countdown from 10000 is a safeguard just in case something nasty happens. 10000 seemed large enough. */
	for (int i = 10000; i > 0; --i) {
		/* I should probably try to convince you that this loop always terminates regardless of the safeguard. */
		/* 1. next_orders contains at least one element. */
		/* 2. The loop terminates if result->size() exceeds a fixed (for this loop) value, or if the least order's scheduled date is later than max_ticks. */
		/*    (We ignore the case that the least order's scheduled date has overflown, as it is a relative rather than absolute date.) */
		/* 3. Every time we loop round, either result->size() will have increased -OR- we will have increased the expected_tick of one of the elements of next_orders. */
		/* 4. Therefore the loop must eventually terminate. */

		/* First, we check if we can stop looking for departures yet. */
		if (result.size() >= _settings_client.gui.max_departures || order_queue.empty()) break;

		/* The best candidate for the next departure is at the top of the next_orders queue, store it in least_item, lod. */
		OrderDateQueueItem least_item = order_queue.front();
		std::pop_heap(order_queue.begin(), order_queue.end());
		order_queue.pop_back();

		OrderDate &lod = next_orders[least_item.order_data_index];

		if (lod.expected_tick - lod.lateness > max_ticks) break;

		/* We already know the least order and that it's a suitable departure, so make it into a departure. */
		std::unique_ptr<Departure> departure_ptr = std::make_unique<Departure>();
		Departure *d = departure_ptr.get();
		d->scheduled_tick = state_ticks_base + lod.expected_tick - lod.lateness;
		d->lateness = lod.lateness;
		d->status = lod.status;
		d->vehicle = lod.v;
		d->type = type;
		d->show_as = calling_settings.GetShowAsType(lod.order, type);
		d->order = lod.order;
		d->scheduled_waiting_time = lod.scheduled_waiting_time;

		ScheduledDispatchVehicleRecords dispatch_records = lod.dispatch_records;

		/* We'll be going through the order list later, so we need a separate variable for it. */
		const Order *order = lod.order;

		const uint order_iteration_limit = lod.v->GetNumOrders() * (lod.have_veh_dispatch_conditionals ? 8 : 1);

		if (type == D_DEPARTURE) {
			/* Computing departures: */
			/* We want to find out where it will terminate, making a list of the stations it calls at along the way. */
			/* We only count stations where unloading happens as being called at - i.e. pickup-only stations are ignored. */
			/* Where the vehicle terminates is defined as the last unique station called at by the vehicle from the current order. */

			/* If the vehicle loops round to the current order without a terminus being found, then it terminates upon reaching its current order again. */

			/* We also determine which station this departure is going via, if any. */
			/* A departure goes via a station if it is the first station for which the vehicle has an order to go via or non-stop via. */
			/* Multiple departures on the same journey may go via different stations. That a departure can go via at most one station is intentional. */

			DepartureViaTerminusState via_state{};

			/* Go through the order list, looping if necessary, to find a terminus. */
			/* Get the next order, which may be the vehicle's first order. */
			order = lod.v->orders->GetNext(order);
			StateTicks departure_tick = d->scheduled_tick;
			bool travel_time_required = true;
			CallAt c = CallAt(order, departure_tick);
			for (uint i = order_iteration_limit; i > 0; --i) {
				/* If we reach the order at which the departure occurs again, then use the departure station as the terminus. */
				if (order == lod.order) {
					/* If we're not calling anywhere, then skip this departure. */
					via_state.found_terminus = (d->calling_at.size() > 0);
					break;
				}

				/* If the order is a conditional branch, handle it. */
				if (order->IsType(OT_CONDITIONAL)) {
					switch (GetDepartureConditionalOrderMode(order, lod.v, departure_tick, dispatch_records)) {
							case 0: {
								/* Give up */
								break;
							}
							case 1: {
								/* Take the branch */
								const Order *target = lod.v->GetOrder(order->GetConditionSkipToOrder());
								if (target == nullptr) {
									break;
								}
								departure_tick += order->GetWaitTime() - target->GetTravelTime();
								if (order->GetWaitTime() == 0 && !order->IsWaitTimetabled() && !target->HasNoTimetableTimes()) {
									c.scheduled_tick = 0;
								}
								order = target;
								travel_time_required = false;
								continue;
							}
							case 2: {
								/* Do not take the branch */
								order = lod.v->orders->GetNext(order);
								continue;
							}
					}
					break;
				}

				if (via_state.CheckOrder(lod.v, d, order, source, calling_settings)) break;

				departure_tick += order->GetTravelTime();
				if (travel_time_required && order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
					c.scheduled_tick = 0;
				}
				if (c.scheduled_tick != 0) c.scheduled_tick = departure_tick;

				c.target = CallAtTargetID::FromOrder(order);

				/* We're not interested in this order any further if we're not calling at it. */
				if (via_state.HandleCallingPoint(d, order, c, calling_settings)) break;

				departure_tick += order->GetWaitTime();

				/* Get the next order, which may be the vehicle's first order. */
				order = lod.v->orders->GetNext(order);
				travel_time_required = true;
			}

			if (via_state.found_terminus) {
				/* Add the departure to the result list. */
				bool duplicate = false;

				if (_settings_client.gui.departure_merge_identical) {
					for (uint i = 0; i < result.size(); ++i) {
						if (*d == *(result[i])) {
							duplicate = true;
							break;
						}
					}
				}

				if (!duplicate) {
					result.push_back(std::move(departure_ptr));

					/* If the vehicle is expected to be late, we want to know what time it will arrive rather than depart. */
					/* This is done because it looked silly to me to have a vehicle not be expected for another few days, yet it be at the same time pulling into the station. */
					if (d->status != D_ARRIVED && d->lateness > 0) {
						d->lateness = std::max<Ticks>(0, d->lateness - lod.order->GetWaitTime());
					}
				}
			}
		} else {
			/* Computing arrivals: */
			/* First we need to find the origin of the order. This is somewhat like finding a terminus, but a little more involved since order lists are singly linked. */
			/* The next stage is simpler. We just need to add all the stations called at on the way to the current station. */
			/* Again, we define a station as being called at if the vehicle loads from it. */

			/* However, the very first thing we do is use the arrival time as the scheduled time instead of the departure time. */
			d->scheduled_tick -= d->EffectiveWaitingTime();

			/* Project back the arrival history if the vehicle is already part way along the route, this stops at conditional jumps or jump targets */
			if (!lod.arrivals_complete) {
				ArrivalHistoryEntry existing_history_start = lod.arrival_history.empty() ? ArrivalHistoryEntry{ lod.order, lod.expected_tick } : lod.arrival_history.front();
				OrderID existing_history_start_idx = 0;
				OrderID arrival_idx = 0;
				for (OrderID i = 0; i < lod.v->GetNumOrders(); i++) {
					const Order *o = lod.v->GetOrder(i);
					if (o == existing_history_start.order) existing_history_start_idx = i;
					if (o == lod.order) arrival_idx = i;
				}

				OrderID predict_history_starting_from = arrival_idx + 1;
				if (predict_history_starting_from >= lod.v->GetNumOrders()) predict_history_starting_from = 0;

				for (OrderID i = 0; i < lod.v->GetNumOrders(); i++) {
					const Order *o = lod.v->GetOrder(i);
					if (o->IsType(OT_CONDITIONAL)) {
						auto stop_prediction_at = [&](OrderID target) {
							if (target < lod.v->GetNumOrders()) {
								if (predict_history_starting_from > existing_history_start_idx) {
									/* Prediction range is cut into two sections by the end of the order list */
									if (target > predict_history_starting_from || target < existing_history_start_idx) predict_history_starting_from = target;
								} else {
									/* Prediction range is in the middle of the order list */
									if (target > predict_history_starting_from && target < existing_history_start_idx) predict_history_starting_from = target;
								}
							}
						};
						stop_prediction_at(i);
						stop_prediction_at(order->GetConditionSkipToOrder());
					}
				}

				std::vector<ArrivalHistoryEntry> new_history;
				Ticks cumul = 0;
				for (const Order *o = lod.v->GetOrder(predict_history_starting_from); o != existing_history_start.order; o = lod.v->orders->GetNext(o)) {
					if ((o->GetTravelTime() == 0 && !o->IsTravelTimetabled()) || o->IsScheduledDispatchOrder(true)) {
						if (!new_history.empty()) new_history.back().offset = INVALID_DEPARTURE_TICKS; // Signal to not use times for orders before this in the history
					}

					cumul += o->GetTravelTime() + o->GetWaitTime();

					if (IsCallingPointTargetOrder(o)) {
						new_history.push_back({ o, cumul });
					}
				}
				cumul += existing_history_start.order->GetTravelTime();
				if (existing_history_start.order == lod.order) {
					cumul += d->EffectiveWaitingTime();
				} else {
					cumul += existing_history_start.order->GetWaitTime();
				}

				/* Iterate in reverse order, to fill in times properly */
				size_t idx = new_history.size();
				while (idx > 0) {
					ArrivalHistoryEntry &entry = new_history[idx - 1];
					if (entry.offset == INVALID_DEPARTURE_TICKS) break;

					entry.offset = existing_history_start.offset - (cumul - entry.offset);

					idx--;
				}
				while (idx > 0) {
					new_history[idx - 1].offset = INVALID_DEPARTURE_TICKS;
					idx--;
				}

				new_history.insert(new_history.end(), lod.arrival_history.begin(), lod.arrival_history.end());
				lod.arrival_history = std::move(new_history);
			}

			if (ProcessArrivalHistory(d, lod.arrival_history, lod.expected_tick - d->EffectiveWaitingTime(), source, calling_settings)) {
				bool duplicate = false;

				if (_settings_client.gui.departure_merge_identical) {
					for (uint i = 0; i < result.size(); ++i) {
						if (*d == *(result[i])) {
							duplicate = true;
							break;
						}
					}
				}

				if (!duplicate) {
					result.push_back(std::move(departure_ptr));
				}
			}

			/* A new arrival history following on from this will be filled in below */
			lod.arrival_history.clear();
			lod.arrivals_complete = true;
		}

		/* Save on pointer dereferences in the coming loop. */
		order = lod.order;

		/* Now we find the next suitable order for being a departure for this vehicle. */
		/* We do this in a similar way to finding the first suitable order for the vehicle. */

		/* Go to the next order so we don't add the current order again. */
		order = lod.v->orders->GetNext(order);
		if (VehicleSetNextDepartureTime(&lod.expected_tick, &lod.scheduled_waiting_time, state_ticks_base, lod.v, order, false, schdispatch_last_planned_dispatch, dispatch_records)) {
			lod.lateness = 0;
		}

		/* Go through the order list to find the next candidate departure. */
		/* We only need to consider each order at most once. */
		bool found_next_order = false;
		bool require_travel_time = true;
		for (uint i = order_iteration_limit; i > 0; --i) {
			/* If the order is a conditional branch, handle it. */
			if (order->IsType(OT_CONDITIONAL)) {
				switch (GetDepartureConditionalOrderMode(order, lod.v, state_ticks_base + lod.expected_tick, dispatch_records)) {
						case 0: {
							/* Give up */
							break;
						}
						case 1: {
							/* Take the branch */
							const Order *target = lod.v->GetOrder(order->GetConditionSkipToOrder());
							if (target == nullptr) {
								break;
							}
							if (order->GetWaitTime() == 0 && !order->IsWaitTimetabled() && !target->HasNoTimetableTimes() && !target->IsType(OT_CONDITIONAL)) {
								break;
							}
							order = target;

							lod.expected_tick -= order->GetTravelTime(); /* Added in next VehicleSetNextDepartureTime */
							if (VehicleSetNextDepartureTime(&lod.expected_tick, &lod.scheduled_waiting_time, state_ticks_base, lod.v, order, false, schdispatch_last_planned_dispatch, dispatch_records)) {
								lod.lateness = 0;
							}
							require_travel_time = false;
							continue;
						}
						case 2: {
							/* Do not take the branch */
							lod.expected_tick -= order->GetWaitTime(); /* Added previously in VehicleSetNextDepartureTime */
							order = lod.v->orders->GetNext(order);
							if (VehicleSetNextDepartureTime(&lod.expected_tick, &lod.scheduled_waiting_time, state_ticks_base, lod.v, order, false, schdispatch_last_planned_dispatch, dispatch_records)) {
								lod.lateness = 0;
							}
							require_travel_time = true;
							continue;
						}
				}
				break;
			}

			/* If an order has a 0 travel time, and it's not explictly set, then stop. */
			if (require_travel_time && order->GetTravelTime() == 0 && !order->IsTravelTimetabled() && !order->IsType(OT_IMPLICIT)) {
				break;
			}

			/* If the departure is scheduled to be too late, then stop. */
			if (lod.expected_tick - lod.lateness > max_ticks) {
				break;
			}

			/* If the order loads from this station (or unloads if we're computing arrivals) and has a wait time set, then it is suitable for being a departure. */
			if ((type == D_DEPARTURE && calling_settings.IsDeparture(order, source)) ||
					(type == D_ARRIVAL && calling_settings.IsArrival(order, source))) {
				lod.order = order;
				found_next_order = true;
				break;
			} else {
				if (type == D_ARRIVAL) {
					lod.arrival_history.push_back({ order, lod.expected_tick });
				}
			}

			order = lod.v->orders->GetNext(order);
			if (VehicleSetNextDepartureTime(&lod.expected_tick, &lod.scheduled_waiting_time, state_ticks_base, lod.v, order, false, schdispatch_last_planned_dispatch, dispatch_records)) {
				lod.lateness = 0;
			}
			require_travel_time = true;
		}

		/* The vehicle can't possibly have arrived at its next candidate departure yet. */
		if (lod.status == D_ARRIVED) {
			lod.status = D_TRAVELLING;
		}

		/* If we found a suitable order for being a departure, add it back to the queue, otherwise then we can ignore this vehicle from now on. */
		if (found_next_order) {
			least_item.tick = lod.GetQueueTick(type);
			order_queue.push_back(least_item);
			std::push_heap(order_queue.begin(), order_queue.end());
		}
	}

	if (type == D_DEPARTURE) {
		ScheduledDispatchDepartureLocalFix(result);
		if (calling_settings.SmartTerminusEnabled()) {
			ScheduledDispatchSmartTerminusDetection(result);
		}
	}

	/* Done. Phew! */
	return result;
}

Ticks GetDeparturesMaxTicksAhead()
{
	if (_settings_time.time_in_minutes) {
		return _settings_client.gui.max_departure_time_minutes * _settings_time.ticks_per_minute;
	} else {
		return _settings_client.gui.max_departure_time * DAY_TICKS * DayLengthFactor();
	}
}

struct DepartureListScheduleModeSlotEvaluator {
	struct DispatchScheduleAnno {
		DispatchSchedule::PositionBackup original_position_backup;
		uint repetition = 0;
		bool usable = false;
	};

	DepartureList &result;
	const Vehicle *v;
	const Order *start_order;
	DispatchSchedule &ds;
	DispatchScheduleAnno &anno;
	const uint schedule_index;
	const DepartureOrderDestinationDetector &source;
	DepartureType type;
	DepartureCallingSettings calling_settings;
	std::vector<ArrivalHistoryEntry> &arrival_history;

	StateTicks slot{};
	uint slot_index{};
	bool departure_dependant_condition_found = false;

	void EvaluateSlots();

private:
	inline bool IsDepartureDependantConditionVariable(OrderConditionVariable ocv) const { return ocv == OCV_DISPATCH_SLOT || ocv == OCV_TIME_DATE; }

	uint8_t EvaluateConditionalOrder(const Order *order, StateTicks eval_tick);
	void EvaluateFromSourceOrder(const Order *source_order, StateTicks departure_tick);
	void EvaluateSlotIndex(uint slot_index);
};

uint8_t DepartureListScheduleModeSlotEvaluator::EvaluateConditionalOrder(const Order *order, StateTicks eval_tick) {
	if (order->GetConditionVariable() == OCV_TIME_DATE) {
		TraceRestrictTimeDateValueField field = static_cast<TraceRestrictTimeDateValueField>(order->GetConditionValue());
		if (field != TRTDVF_MINUTE && field != TRTDVF_HOUR && field != TRTDVF_HOUR_MINUTE) {
			/* No reasonable way to handle this with a minutes schedule, give up */
			return 0;
		}
	}
	if (order->GetConditionVariable() == OCV_DISPATCH_SLOT) {
		LastDispatchRecord record{};

		auto get_vehicle_records = [&](uint16_t schedule_index) -> const LastDispatchRecord * {
			if (schedule_index == this->schedule_index) {
				extern LastDispatchRecord MakeLastDispatchRecord(const DispatchSchedule &ds, StateTicks slot, int slot_index);
				record = MakeLastDispatchRecord(this->ds, this->slot, this->slot_index);
				return &record;
			} else {
				/* Testing a different schedule index, handle as if there is no record */
				return nullptr;
			}
		};

		return EvaluateDispatchSlotConditionalOrder(order, this->v->orders->GetScheduledDispatchScheduleSet(), eval_tick, get_vehicle_records).GetResult() ? 1 : 2;
	} else {
		return GetNonScheduleDepartureConditionalOrderMode(order, this->v, eval_tick);
	}
}

void DepartureListScheduleModeSlotEvaluator::EvaluateFromSourceOrder(const Order *source_order, StateTicks departure_tick)
{
	Departure d{};
	d.scheduled_tick = departure_tick;
	d.lateness = 0;
	d.status = D_SCHEDULED;
	d.vehicle = this->v;
	d.type = this->type;
	d.show_as = this->calling_settings.GetShowAsType(source_order, type);
	d.order = source_order;
	d.scheduled_waiting_time = 0;

	/* We'll be going through the order list later, so we need a separate variable for it. */
	const Order *order = source_order;

	const uint order_iteration_limit = this->v->GetNumOrders();

	if (type == D_DEPARTURE) {
		/* Computing departures: */
		DepartureViaTerminusState via_state{};

		order = this->v->orders->GetNext(order);
		bool travel_time_required = true;
		CallAt c = CallAt(order, departure_tick);
		for (uint i = order_iteration_limit; i > 0; --i) {
			/* If we reach the order at which the departure occurs again, then use the departure station as the terminus. */
			if (order == source_order) {
				/* If we're not calling anywhere, then skip this departure. */
				via_state.found_terminus = (d.calling_at.size() > 0);
				break;
			}

			/* If the order is a conditional branch, handle it. */
			if (order->IsType(OT_CONDITIONAL)) {
				if (this->IsDepartureDependantConditionVariable(order->GetConditionVariable())) this->departure_dependant_condition_found = true;
				switch (this->EvaluateConditionalOrder(order, departure_tick)) {
						case 0: {
							/* Give up */
							break;
						}
						case 1: {
							/* Take the branch */
							const Order *target = this->v->GetOrder(order->GetConditionSkipToOrder());
							if (target == nullptr) {
								break;
							}
							departure_tick += order->GetWaitTime() - target->GetTravelTime();
							if (order->GetWaitTime() == 0 && !order->IsWaitTimetabled() && !target->HasNoTimetableTimes() && !target->IsType(OT_CONDITIONAL)) {
								c.scheduled_tick = 0;
							}
							order = target;
							travel_time_required = false;
							continue;
						}
						case 2: {
							/* Do not take the branch */
							order = this->v->orders->GetNext(order);
							continue;
						}
				}
				break;
			}

			if (via_state.CheckOrder(this->v, &d, order, this->source, this->calling_settings)) break;

			departure_tick += order->GetTravelTime();
			if (travel_time_required && order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
				c.scheduled_tick = 0;
			}
			if (c.scheduled_tick != 0) c.scheduled_tick = departure_tick;
			c.target = CallAtTargetID::FromOrder(order);

			/* We're not interested in this order any further if we're not calling at it. */
			if (via_state.HandleCallingPoint(&d, order, c, this->calling_settings)) break;

			departure_tick += order->GetWaitTime();

			if (order->IsScheduledDispatchOrder(true)) {
				if (d.calling_at.size() > 0) {
					via_state.found_terminus = true;
				}
				break;
			}

			/* Get the next order, which may be the vehicle's first order. */
			order = this->v->orders->GetNext(order);
			travel_time_required = true;
		}

		if (via_state.found_terminus) {
			/* Add the departure to the result list. */
			this->result.push_back(std::make_unique<Departure>(std::move(d)));
		}
	} else {
		/* Computing arrivals: */

		if (ProcessArrivalHistory(&d, this->arrival_history, (departure_tick - this->slot).AsTicks(), this->source, this->calling_settings)) {
			this->result.push_back(std::make_unique<Departure>(std::move(d)));
		}
	}
}

void DepartureListScheduleModeSlotEvaluator::EvaluateSlotIndex(uint slot_index)
{
	this->slot_index = slot_index;
	this->slot = this->ds.GetScheduledDispatchStartTick() + this->ds.GetScheduledDispatch()[slot_index].offset;
	StateTicks departure_tick = this->slot;
	this->arrival_history.clear();

	/* The original last dispatch time will be restored in MakeDepartureListScheduleMode */
	this->ds.SetScheduledDispatchLastDispatch(ds.GetScheduledDispatch()[slot_index].offset);
	auto guard = scope_guard([&]() {
		this->ds.SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
	});

	if (type == D_DEPARTURE && calling_settings.IsDeparture(this->start_order, this->source)) {
		this->EvaluateFromSourceOrder(this->start_order, departure_tick);
		return;
	}
	if (type == D_ARRIVAL) {
		this->arrival_history.push_back({ this->start_order, (departure_tick - this->slot).AsTicks() });
	}

	const Order *order = this->v->orders->GetNext(this->start_order);
	bool require_travel_time = true;

	/* Loop through the vehicle's orders until we've found a suitable order or we've determined that no such order exists. */
	/* We only need to consider each order at most once. */
	for (int i = this->v->GetNumOrders(); i > 0; --i) {
		departure_tick += order->GetTravelTime();

		if (type == D_ARRIVAL && this->calling_settings.IsArrival(order, this->source)) {
			this->EvaluateFromSourceOrder(order, departure_tick);
			break;
		}

		departure_tick += order->GetWaitTime();

		if (order->IsScheduledDispatchOrder(true)) {
			break;
		}

		if (type == D_DEPARTURE && this->calling_settings.IsDeparture(order, this->source)) {
			this->EvaluateFromSourceOrder(order, departure_tick);
			break;
		}

		/* If the order is a conditional branch, handle it. */
		if (order->IsType(OT_CONDITIONAL)) {
			if (this->IsDepartureDependantConditionVariable(order->GetConditionVariable())) this->departure_dependant_condition_found = true;
			switch (this->EvaluateConditionalOrder(order, departure_tick)) {
				case 0: {
					/* Give up */
					break;
				}
				case 1: {
					/* Take the branch */
					const Order *target = this->v->GetOrder(order->GetConditionSkipToOrder());
					if (target == nullptr) {
						break;
					}
					departure_tick -= target->GetTravelTime();
					if (order->GetWaitTime() == 0 && !order->IsWaitTimetabled() && !target->HasNoTimetableTimes() && !target->IsType(OT_CONDITIONAL)) {
						break; // Branch travel time required but not present, stop
					}
					order = target;
					require_travel_time = false;
					continue;
				}
				case 2: {
					/* Do not take the branch */
					departure_tick -= order->GetWaitTime(); /* Added previously above */
					order = this->v->orders->GetNext(order);
					require_travel_time = true;
					continue;
				}
			}
			break;
		}

		/* If an order has a 0 travel time, and it's not explictly set, then stop. */
		if (require_travel_time && order->GetTravelTime() == 0 && !order->IsTravelTimetabled() && !order->IsType(OT_IMPLICIT)) {
			break;
		}

		if (type == D_ARRIVAL) {
			this->arrival_history.push_back({ order, (departure_tick - this->slot).AsTicks() });
		}

		order = this->v->orders->GetNext(order);
		require_travel_time = true;
	}
}

void DepartureListScheduleModeSlotEvaluator::EvaluateSlots()
{
	const size_t start_number_departures = this->result.size();
	this->departure_dependant_condition_found = false;
	this->EvaluateSlotIndex(0);
	const auto &slots = this->ds.GetScheduledDispatch();
	if (this->departure_dependant_condition_found) {
		/* Need to evaluate every slot individually */
		for (uint i = 1; i < (uint)slots.size(); i++) {
			this->EvaluateSlotIndex(i);
		}

		if (this->anno.repetition > 1) {
			const auto dispatch_start_tick = this->ds.GetScheduledDispatchStartTick();
			auto guard = scope_guard([&]() {
				this->ds.SetScheduledDispatchStartTick(dispatch_start_tick);
			});
			for (uint i = 1; i < this->anno.repetition; i++) {
				this->ds.SetScheduledDispatchStartTick(this->ds.GetScheduledDispatchStartTick() + this->ds.GetScheduledDispatchDuration());
				for (uint j = 0; j < (uint)slots.size(); j++) {
					this->EvaluateSlotIndex(j);
				}
			}
		}
	} else {
		/* Trivially repeat found departures */
		const size_t done_first_slot_departures = this->result.size();
		if (done_first_slot_departures == start_number_departures) return;
		const uint32_t first_offset = slots[0].offset;
		for (size_t i = 1; i < slots.size(); i++) {
			for (size_t j = start_number_departures; j != done_first_slot_departures; j++) {
				std::unique_ptr<Departure> d = std::make_unique<Departure>(*this->result[j]); // Clone departure
				d->ShiftTimes(slots[i].offset - first_offset);
				this->result.push_back(std::move(d));
			}
		}
		const size_t done_schedule_departures = this->result.size();
		for (uint i = 1; i < this->anno.repetition; i++) {
			for (size_t j = start_number_departures; j != done_schedule_departures; j++) {
				std::unique_ptr<Departure> d = std::make_unique<Departure>(*this->result[j]); // Clone departure
				d->ShiftTimes(this->ds.GetScheduledDispatchDuration() * i);
				this->result.push_back(std::move(d));
			}
		}
	}
}

static DepartureList MakeDepartureListScheduleMode(DepartureOrderDestinationDetector source, const std::vector<const Vehicle *> &vehicles, DepartureType type,
		DepartureCallingSettings calling_settings, const StateTicks start_tick, const StateTicks end_tick, const uint max_departure_slots_per_schedule)
{
	const Ticks tick_duration = (end_tick - start_tick).AsTicks();

	std::vector<std::unique_ptr<Departure>> result;
	std::vector<ArrivalHistoryEntry> arrival_history;

	for (const Vehicle *veh : vehicles) {
		if (!HasBit(veh->vehicle_flags, VF_SCHEDULED_DISPATCH)) continue;

		const Vehicle *v = nullptr;
		for (const Vehicle *u = veh->FirstShared(); u != nullptr; u = u->NextShared()) {
			if (IsVehicleUsableForDepartures(u, calling_settings)) {
				v = u;
				break;
			}
		}
		if (v == nullptr) continue;

		std::vector<DepartureListScheduleModeSlotEvaluator::DispatchScheduleAnno> schedule_anno;
		schedule_anno.resize(v->orders->GetScheduledDispatchScheduleCount());
		for (uint i = 0; i < v->orders->GetScheduledDispatchScheduleCount(); i++) {
			/* This is mutable so that parts can be backed up, modified and restored later */
			DispatchSchedule &ds = const_cast<Vehicle *>(v)->orders->GetDispatchScheduleByIndex(i);
			DepartureListScheduleModeSlotEvaluator::DispatchScheduleAnno &anno = schedule_anno[i];

			anno.original_position_backup = ds.BackupPosition();

			const uint32_t duration = ds.GetScheduledDispatchDuration();
			if (duration < _settings_time.ticks_per_minute || duration > (uint)tick_duration) continue; // Duration is obviously out of range
			if (tick_duration % duration != 0) continue; // Duration does not evenly fit into range
			const uint slot_count = (uint)ds.GetScheduledDispatch().size();
			if (slot_count == 0) continue; // No departure slots

			anno.repetition = tick_duration / ds.GetScheduledDispatchDuration();

			if (anno.repetition * slot_count > max_departure_slots_per_schedule) continue;

			StateTicks dispatch_tick = ds.GetScheduledDispatchStartTick();
			if (dispatch_tick < start_tick) {
				dispatch_tick += CeilDivT<StateTicksDelta>(start_tick - dispatch_tick, duration).AsTicks() * duration;
			}
			if (dispatch_tick > start_tick) {
				StateTicksDelta delta = (dispatch_tick - start_tick);
				dispatch_tick -= (delta / duration).AsTicksT<uint>() * duration;
			}

			ds.SetScheduledDispatchStartTick(dispatch_tick);
			ds.SetScheduledDispatchLastDispatch(INVALID_SCHEDULED_DISPATCH_OFFSET);
			anno.usable = true;
		}

		auto guard = scope_guard([&]() {
			for (uint i = 0; i < v->orders->GetScheduledDispatchScheduleCount(); i++) {
				/* Restore backup */
				DispatchSchedule &ds = const_cast<Vehicle *>(v)->orders->GetDispatchScheduleByIndex(i);
				const DepartureListScheduleModeSlotEvaluator::DispatchScheduleAnno &anno = schedule_anno[i];
				ds.RestorePosition(anno.original_position_backup);
			}
		});

		for (const Order *start_order : v->Orders()) {
			if (start_order->IsScheduledDispatchOrder(true)) {
				const uint schedule_index = start_order->GetDispatchScheduleIndex();
				DepartureListScheduleModeSlotEvaluator::DispatchScheduleAnno &anno = schedule_anno[schedule_index];
				if (!anno.usable) continue;

				DispatchSchedule &ds = const_cast<Vehicle *>(v)->orders->GetDispatchScheduleByIndex(schedule_index);
				DepartureListScheduleModeSlotEvaluator evaluator{
					result, v, start_order, ds, anno, schedule_index, source, type, calling_settings, arrival_history
				};
				evaluator.EvaluateSlots();
			}
		}
	}

	for (std::unique_ptr<Departure> &d : result) {
		StateTicks new_tick = d->scheduled_tick;
		if (new_tick < start_tick) {
			new_tick += CeilDivT<StateTicksDelta>(start_tick - new_tick, tick_duration).AsTicks() * tick_duration;
		}
		if (new_tick > start_tick) {
			StateTicksDelta delta = (new_tick - start_tick);
			new_tick -= (delta / tick_duration).AsTicksT<uint>() * tick_duration;
		}
		if (new_tick != d->scheduled_tick) {
			d->ShiftTimes(new_tick - d->scheduled_tick);
		}
	}

	std::sort(result.begin(), result.end(), [](std::unique_ptr<Departure> &a, std::unique_ptr<Departure> &b) -> bool {
		if (a->scheduled_tick == b->scheduled_tick) {
			return std::tie(a->terminus.target, a->terminus.scheduled_tick, a->vehicle->index)
					< std::tie(b->terminus.target, b->terminus.scheduled_tick, b->vehicle->index);
		}
		return a->scheduled_tick < b->scheduled_tick;
	});

	if (type == D_DEPARTURE && calling_settings.SmartTerminusEnabled()) {
		ScheduledDispatchSmartTerminusDetection(result, tick_duration);
	}

	return result;
}

/**
 * Compute an up-to-date list of departures for a station.
 * @param source_mode the departure source mode to use
 * @param source the station/etc to compute the departures of
 * @param vehicles set of all the vehicles stopping at this station, of all vehicles types that we are interested in
 * @param type the type of departures to get (departures or arrivals)
 * @param calling_settings departure calling settings
 * @return a list of departures, which is empty if an error occurred
 */
DepartureList MakeDepartureList(DeparturesSourceMode source_mode, DepartureOrderDestinationDetector source, const std::vector<const Vehicle *> &vehicles,
		DepartureType type, DepartureCallingSettings calling_settings)
{
	switch (source_mode) {
		case DSM_LIVE:
			return MakeDepartureListLiveMode(source, vehicles, type, calling_settings);

		case DSM_SCHEDULE_24H: {
			if (!_settings_time.time_in_minutes) return {};
			TickMinutes start = _settings_time.NowInTickMinutes().ToSameDayClockTime(0, 0);
			StateTicks start_tick = _settings_time.FromTickMinutes(start);
			StateTicks end_tick = _settings_time.FromTickMinutes(start + (24 * 60));

			/* Set maximum to 90 departures per hour per dispatch schedule, to prevent excessive numbers of departures */
			return MakeDepartureListScheduleMode(source, vehicles, type, calling_settings, start_tick, end_tick, 90 * 24);
		}

		default:
			NOT_REACHED();
	}
}
