/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures.cpp Scheduled departures from a station. */

#include "stdafx.h"
#include "debug.h"
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
#include "core/smallvec_type.hpp"
#include "date_type.h"
#include "company_type.h"
#include "cargo_type.h"
#include "departures_func.h"
#include "departures_type.h"

#include <map>
#include <set>
#include <vector>
#include <algorithm>

/* A cache of used departure time for scheduled dispatch in departure time calculation */
typedef std::map<uint32, std::set<DateTicksScaled>> schdispatch_cache_t;

/** A scheduled order. */
typedef struct OrderDate
{
	const Order *order;     ///< The order
	const Vehicle *v;       ///< The vehicle carrying out the order
	DateTicks expected_date;///< The date on which the order is expected to complete
	Ticks lateness;         ///< How late this order is expected to finish
	DepartureStatus status; ///< Whether the vehicle has arrived to carry out the order yet
	uint scheduled_waiting_time; ///< Scheduled waiting time if scheduled dispatch is used
} OrderDate;

static bool IsDeparture(const Order *order, StationID station) {
	return (order->GetType() == OT_GOTO_STATION &&
			(StationID)order->GetDestination() == station &&
			(order->GetLoadType() != OLFB_NO_LOAD ||
			_settings_client.gui.departure_show_all_stops) &&
			(order->GetWaitTime() != 0 || order->IsWaitTimetabled()) &&
			!(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION));
}

static bool IsVia(const Order *order, StationID station) {
	return ((order->GetType() == OT_GOTO_STATION ||
			order->GetType() == OT_GOTO_WAYPOINT) &&
			(StationID)order->GetDestination() == station &&
			(order->GetNonStopType() == ONSF_NO_STOP_AT_ANY_STATION ||
			order->GetNonStopType() == ONSF_NO_STOP_AT_DESTINATION_STATION));
}

static bool IsArrival(const Order *order, StationID station) {
	return (order->GetType() == OT_GOTO_STATION &&
			(StationID)order->GetDestination() == station &&
			(order->GetUnloadType() != OUFB_NO_UNLOAD ||
			_settings_client.gui.departure_show_all_stops) &&
			(order->GetWaitTime() != 0 || order->IsWaitTimetabled()) &&
			!(order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION));
}

static inline bool VehicleSetNextDepartureTime(DateTicks *previous_departure, uint *waiting_time, const DateTicksScaled date_only_scaled, const Vehicle *v, const Order *order, bool arrived_at_timing_point, schdispatch_cache_t &dept_schedule_last)
{
	if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
		/* Loop over all order to find the first waiting order */
		for (int j = 0; j < v->orders.list->GetNumOrders(); ++j) {
			Order* iterating_order = v->orders.list->GetOrderAt(j);

			if (iterating_order->IsWaitTimetabled() && !iterating_order->IsType(OT_IMPLICIT)) {
				/* This condition means that we want departure time for the first order */
				/* but not if the vehicle has arrived at the first order because the timetable is already shifted */
				if (iterating_order == order && !(arrived_at_timing_point && v->cur_implicit_order_index == j)) {
					DateTicksScaled actual_departure    = -1;
					const DateTicksScaled begin_time    = v->orders.list->GetScheduledDispatchStartTick();
					const uint32 dispatch_duration      = v->orders.list->GetScheduledDispatchDuration();
					const int32 max_delay               = v->orders.list->GetScheduledDispatchDelay();

					/* Earliest possible departure according to schedue */
					DateTicksScaled earliest_departure = begin_time + v->orders.list->GetScheduledDispatchLastDispatch();

					/* Earliest possible departure according to vehicle current timetable */
					if (earliest_departure + max_delay < date_only_scaled + *previous_departure + order->GetTravelTime()) {
						earliest_departure = date_only_scaled + *previous_departure + order->GetTravelTime() - max_delay - 1;
						/* -1 because this number is actually a moment before actual departure */
					}

					/* Find next available slots */
					for (auto current_offset : v->orders.list->GetScheduledDispatch()) {
						if (current_offset >= dispatch_duration) continue;
						DateTicksScaled current_departure = begin_time + current_offset;
						while (current_departure <= earliest_departure) {
							current_departure += dispatch_duration;
						}

						/* Make sure the slots has not already been used previously in this departure board calculation */
						while (dept_schedule_last[v->orders.list->index].count(current_departure) > 0) {
							current_departure += dispatch_duration;
						}

						if (actual_departure == -1 || actual_departure > current_departure) {
							actual_departure = current_departure;
						}
					}

					*waiting_time = order->GetWaitTime() + actual_departure - date_only_scaled - *previous_departure - order->GetTravelTime();
					*previous_departure = actual_departure - date_only_scaled + order->GetWaitTime();
					dept_schedule_last[v->orders.list->index].insert(actual_departure);

					/* Return true means that vehicle lateness should be clear from this point onward */
					return true;
				}

				/* This is special case for proper calculation of arrival time. */
				if (arrived_at_timing_point && v->cur_implicit_order_index == j) {
					*previous_departure += order->GetTravelTime() + order->GetWaitTime();
					*waiting_time = -v->lateness_counter + order->GetWaitTime();
					return false;
				}
				break;
			} /* if it is first waiting order */
		} /* for in order list */
	} /* if vehicle is on scheduled dispatch */

	/* Not using schedule for this departure time */
	*previous_departure += order->GetTravelTime() + order->GetWaitTime();
	*waiting_time = 0;
	return false;
}

static void ScheduledDispatchDepartureLocalFix(DepartureList *departure_list)
{
	/* Seperate departure by each shared order group */
	std::map<uint32, std::vector<Departure*>> separated_departure;
	for (Departure* departure : *departure_list) {
		separated_departure[departure->vehicle->orders.list->index].push_back(departure);
	}

	for (auto& pair : separated_departure) {
		auto d_list = pair.second;

		/* If the group is scheduled dispatch, then */
		if (HasBit(d_list[0]->vehicle->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
			/* Separate departure time and sort them ascendently */
			std::vector<DateTicksScaled> departure_time_list;
			for (const auto& d : d_list) {
				departure_time_list.push_back(d->scheduled_date);
			}
			std::sort(departure_time_list.begin(), departure_time_list.end());

			/* Sort the departure list by arrival time */
			std::sort(d_list.begin(), d_list.end(), [](const Departure * const &a, const Departure * const &b) -> bool {
				DateTicksScaled arr_a = a->scheduled_date - (a->scheduled_waiting_time > 0 ? a->scheduled_waiting_time : a->order->GetWaitTime());
				DateTicksScaled arr_b = b->scheduled_date - (b->scheduled_waiting_time > 0 ? b->scheduled_waiting_time : b->order->GetWaitTime());
				return arr_a < arr_b;
			});

			/* Re-assign them sequentially */
			for (size_t i = 0; i < d_list.size(); i++) {
				const DateTicksScaled arrival = d_list[i]->scheduled_date - (d_list[i]->scheduled_waiting_time > 0 ? d_list[i]->scheduled_waiting_time : d_list[i]->order->GetWaitTime());
				d_list[i]->scheduled_waiting_time = departure_time_list[i] - arrival;
				d_list[i]->scheduled_date = departure_time_list[i];

				if (d_list[i]->scheduled_waiting_time == d_list[i]->order->GetWaitTime()) {
					d_list[i]->scheduled_waiting_time = 0;
				}
			}
		}
	}

	/* Re-sort the departure list */
	std::sort(departure_list->begin(), departure_list->end(), [](Departure * const &a, Departure * const &b) -> bool {
		return a->scheduled_date < b->scheduled_date;
	});
}

/**
 * Compute an up-to-date list of departures for a station.
 * @param station the station to compute the departures of
 * @param vehicles set of all the vehicles stopping at this station, of all vehicles types that we are interested in
 * @param type the type of departures to get (departures or arrivals)
 * @param show_vehicles_via whether to include vehicles that have this station in their orders but do not stop at it
 * @param show_pax whether to include passenger vehicles
 * @param show_freight whether to include freight vehicles
 * @return a list of departures, which is empty if an error occurred
 */
DepartureList* MakeDepartureList(StationID station, const std::vector<const Vehicle *> &vehicles, DepartureType type, bool show_vehicles_via, bool show_pax, bool show_freight)
{
	/* This function is the meat of the departure boards functionality. */
	/* As an overview, it works by repeatedly considering the best possible next departure to show. */
	/* By best possible we mean the one expected to arrive at the station first. */
	/* However, we do not consider departures whose scheduled time is too far in the future, even if they are expected before some delayed ones. */
	/* This code can probably be made more efficient. I haven't done so in order to keep both its (relative) simplicity and my (relative) sanity. */
	/* Having written that, it's not exactly slow at the moment. */

	/* The list of departures which will be returned as a result. */
	std::vector<Departure*> *result = new std::vector<Departure*>();

	if (!show_pax && !show_freight) return result;

	/* A list of the next scheduled orders to be considered for inclusion in the departure list. */
	std::vector<OrderDate*> next_orders;

	/* The maximum possible date for departures to be scheduled to occur. */
	DateTicksScaled max_date = _settings_client.gui.max_departure_time * DAY_TICKS * _settings_game.economy.day_length_factor;

	DateTicksScaled date_only_scaled = ((DateTicksScaled)_date * DAY_TICKS * _settings_game.economy.day_length_factor);
	DateTicksScaled date_fract_scaled = ((DateTicksScaled)_date_fract * _settings_game.economy.day_length_factor) + _tick_skip_counter;

	/* The scheduled order in next_orders with the earliest expected_date field. */
	OrderDate *least_order = nullptr;

	/* Cache for scheduled departure time */
	schdispatch_cache_t schdispatch_last_planned_dispatch;

	{
		/* Get the first order for each vehicle for the station we're interested in that doesn't have No Loading set. */
		/* We find the least order while we're at it. */
		for (const Vehicle *v : vehicles) {
			if (show_pax != show_freight) {
				bool carries_passengers = false;

				const Vehicle *u = v;
				while (u != nullptr) {
					if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CC_PASSENGERS)) {
						carries_passengers = true;
						break;
					}
					u = u->Next();
				}

				if (carries_passengers != show_pax) {
					continue;
				}
			}

			const Order *order = v->GetOrder(v->cur_implicit_order_index % v->GetNumOrders());
			DateTicks start_date = date_fract_scaled - v->current_order_time;
			if (v->cur_timetable_order_index != INVALID_VEH_ORDER_ID && v->cur_timetable_order_index != v->cur_real_order_index) {
				/* vehicle is taking a conditional order branch, adjust start time to compensate */
				const Order *real_current_order = v->GetOrder(v->cur_real_order_index);
				const Order *real_timetable_order = v->GetOrder(v->cur_timetable_order_index);
				assert(real_timetable_order->IsType(OT_CONDITIONAL));
				start_date += (real_timetable_order->GetWaitTime() - real_current_order->GetTravelTime());
			}
			DepartureStatus status = D_TRAVELLING;
			bool should_reset_lateness = false;
			uint waiting_time = 0;

			/* If the vehicle is stopped in a depot, ignore it. */
			if (v->IsStoppedInDepot()) {
				continue;
			}

			/* If the vehicle is heading for a depot to stop there, then its departures are cancelled. */
			if (v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDepotActionType() & ODATFB_HALT) {
				status = D_CANCELLED;
			}

			if (v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) {
				/* Account for the vehicle having reached the current order and being in the loading phase. */
				status = D_ARRIVED;
				start_date -= order->GetTravelTime() + ((v->lateness_counter < 0) ? v->lateness_counter : 0);
			}

			/* Loop through the vehicle's orders until we've found a suitable order or we've determined that no such order exists. */
			/* We only need to consider each order at most once. */
			for (int i = v->GetNumOrders(); i > 0; --i) {
				if (VehicleSetNextDepartureTime(&start_date, &waiting_time, date_only_scaled, v, order, status == D_ARRIVED, schdispatch_last_planned_dispatch)) {
					should_reset_lateness = true;
				}

				/* If the scheduled departure date is too far in the future, stop. */
				if (start_date - v->lateness_counter > max_date) {
					break;
				}

				/* If the order is a conditional branch, handle it. */
				if (order->IsType(OT_CONDITIONAL)) {
					switch(_settings_client.gui.departure_conditionals) {
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

								start_date -= order->GetTravelTime();

								continue;
							}
							case 2: {
								/* Do not take the branch */
								if (status != D_CANCELLED) {
									status = D_TRAVELLING;
								}
								order = (order->next == nullptr) ? v->GetFirstOrder() : order->next;
								continue;
							}
					}
				}

				/* Skip it if it's an automatic order. */
				if (order->IsType(OT_IMPLICIT)) {
					order = (order->next == nullptr) ? v->GetFirstOrder() : order->next;
					continue;
				}

				/* If an order has a 0 travel time, and it's not explictly set, then stop. */
				if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
					break;
				}

				/* If the vehicle will be stopping at and loading from this station, and its wait time is not zero, then it is a departure. */
				/* If the vehicle will be stopping at and unloading at this station, and its wait time is not zero, then it is an arrival. */
				if ((type == D_DEPARTURE && IsDeparture(order, station)) ||
						(type == D_DEPARTURE && show_vehicles_via && IsVia(order, station)) ||
						(type == D_ARRIVAL && IsArrival(order, station))) {
					/* If the departure was scheduled to have already begun and has been cancelled, do not show it. */
					if (start_date < 0 && status == D_CANCELLED) {
						break;
					}

					OrderDate *od = new OrderDate();
					od->order = order;
					od->v = v;
					/* We store the expected date for now, so that vehicles will be shown in order of expected time. */
					od->expected_date = start_date;
					od->lateness = v->lateness_counter > 0 ? v->lateness_counter : 0;
					od->status = status;
					od->scheduled_waiting_time = waiting_time;

					/* Reset lateness if timing is from scheduled dispatch */
					if (should_reset_lateness) {
						od->lateness = 0;
					}

					/* If we are early, use the scheduled date as the expected date. We also take lateness to be zero. */
					if (!should_reset_lateness && v->lateness_counter < 0 && !(v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING))) {
						od->expected_date -= v->lateness_counter;
					}

					/* Update least_order if this is the current least order. */
					if (least_order == nullptr) {
						least_order = od;
					} else if (int(least_order->expected_date - least_order->lateness - (type == D_ARRIVAL ? (least_order->scheduled_waiting_time > 0 ? least_order->scheduled_waiting_time : least_order->order->GetWaitTime()) : 0)) > int(od->expected_date - od->lateness - (type == D_ARRIVAL ? (od->scheduled_waiting_time > 0 ? od->scheduled_waiting_time : od->order->GetWaitTime()) : 0))) {
						/* Somehow my compiler perform an unsigned comparition above so integer cast is required */
						least_order = od;
					}

					next_orders.push_back(od);

					/* We're done with this vehicle. */
					break;
				} else {
					/* Go to the next order in the list. */
					if (status != D_CANCELLED) {
						status = D_TRAVELLING;
					}
					order = (order->next == nullptr) ? v->GetFirstOrder() : order->next;
				}
			}
		}
	}

	/* No suitable orders found? Then stop. */
	if (next_orders.size() == 0) {
		return result;
	}

	/* We now find as many departures as we can. It's a little involved so I'll try to explain each major step. */
	/* The countdown from 10000 is a safeguard just in case something nasty happens. 10000 seemed large enough. */
	for(int i = 10000; i > 0; --i) {
		/* I should probably try to convince you that this loop always terminates regardless of the safeguard. */
		/* 1. next_orders contains at least one element. */
		/* 2. The loop terminates if result->size() exceeds a fixed (for this loop) value, or if the least order's scheduled date is later than max_date. */
		/*    (We ignore the case that the least order's scheduled date has overflown, as it is a relative rather than absolute date.) */
		/* 3. Every time we loop round, either result->size() will have increased -OR- we will have increased the expected_date of one of the elements of next_orders. */
		/* 4. Therefore the loop must eventually terminate. */

		/* least_order is the best candidate for the next departure. */

		/* First, we check if we can stop looking for departures yet. */
		if (result->size() >= _settings_client.gui.max_departures ||
				least_order->expected_date - least_order->lateness > max_date) {
			break;
		}

		/* We already know the least order and that it's a suitable departure, so make it into a departure. */
		Departure *d = new Departure();
		d->scheduled_date = date_only_scaled + least_order->expected_date - least_order->lateness;
		d->lateness = least_order->lateness;
		d->status = least_order->status;
		d->vehicle = least_order->v;
		d->type = type;
		d->order = least_order->order;
		d->scheduled_waiting_time = least_order->scheduled_waiting_time;

		/* We'll be going through the order list later, so we need a separate variable for it. */
		const Order *order = least_order->order;

		if (type == D_DEPARTURE) {
			/* Computing departures: */
			/* We want to find out where it will terminate, making a list of the stations it calls at along the way. */
			/* We only count stations where unloading happens as being called at - i.e. pickup-only stations are ignored. */
			/* Where the vehicle terminates is defined as the last unique station called at by the vehicle from the current order. */

			/* If the vehicle loops round to the current order without a terminus being found, then it terminates upon reaching its current order again. */

			/* We also determine which station this departure is going via, if any. */
			/* A departure goes via a station if it is the first station for which the vehicle has an order to go via or non-stop via. */
			/* Multiple departures on the same journey may go via different stations. That a departure can go via at most one station is intentional. */

			/* We keep track of potential via stations along the way. If we call at a station immediately after going via it, then it is the via station. */
			StationID candidate_via = INVALID_STATION;

			/* Go through the order list, looping if necessary, to find a terminus. */
			/* Get the next order, which may be the vehicle's first order. */
			order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
			/* We only need to consider each order at most once. */
			bool found_terminus = false;
			CallAt c = CallAt((StationID)order->GetDestination(), d->scheduled_date);
			for (int i = least_order->v->GetNumOrders(); i > 0; --i) {
				/* If we reach the order at which the departure occurs again, then use the departure station as the terminus. */
				if (order == least_order->order) {
					/* If we're not calling anywhere, then skip this departure. */
					found_terminus = (d->calling_at.size() > 0);
					break;
				}

				/* If the order is a conditional branch, handle it. */
				if (order->IsType(OT_CONDITIONAL)) {
					switch(_settings_client.gui.departure_conditionals) {
							case 0: {
								/* Give up */
								break;
							}
							case 1: {
								/* Take the branch */
								order = least_order->v->GetOrder(order->GetConditionSkipToOrder());
								if (order == nullptr) {
									break;
								}
								continue;
							}
							case 2: {
								/* Do not take the branch */
								order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
								continue;
							}
					}
				}

				/* If we reach the original station again, then use it as the terminus. */
				if (order->GetType() == OT_GOTO_STATION &&
						(StationID)order->GetDestination() == station &&
						(order->GetUnloadType() != OUFB_NO_UNLOAD ||
						_settings_client.gui.departure_show_all_stops) &&
						order->GetNonStopType() != ONSF_NO_STOP_AT_ANY_STATION &&
						order->GetNonStopType() != ONSF_NO_STOP_AT_DESTINATION_STATION) {
					/* If we're not calling anywhere, then skip this departure. */
					found_terminus = (d->calling_at.size() > 0);
					break;
				}

				/* Check if we're going via this station. */
				if ((order->GetNonStopType() == ONSF_NO_STOP_AT_ANY_STATION ||
						order->GetNonStopType() == ONSF_NO_STOP_AT_DESTINATION_STATION) &&
						order->GetType() == OT_GOTO_STATION &&
						d->via == INVALID_STATION) {
					candidate_via = (StationID)order->GetDestination();
				}

				if (c.scheduled_date != 0 && (order->GetTravelTime() != 0 || order->IsTravelTimetabled())) {
					c.scheduled_date += order->GetTravelTime(); /* TODO smart terminal may not work correctly */
				} else {
					c.scheduled_date = 0;
				}

				c.station = (StationID)order->GetDestination();

				/* We're not interested in this order any further if we're not calling at it. */
				if ((order->GetUnloadType() == OUFB_NO_UNLOAD &&
						!_settings_client.gui.departure_show_all_stops) ||
						(order->GetType() != OT_GOTO_STATION &&
						order->GetType() != OT_IMPLICIT) ||
						order->GetNonStopType() == ONSF_NO_STOP_AT_ANY_STATION ||
						order->GetNonStopType() == ONSF_NO_STOP_AT_DESTINATION_STATION) {
					c.scheduled_date += order->GetWaitTime();
					order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
					continue;
				}

				/* If this order's station is already in the calling, then the previous called at station is the terminus. */
				if (std::find(d->calling_at.begin(), d->calling_at.end(), c) != d->calling_at.end()) {
					found_terminus = true;
					break;
				}

				/* If appropriate, add the station to the calling at list and make it the candidate terminus. */
				if ((order->GetType() == OT_GOTO_STATION ||
						order->GetType() == OT_IMPLICIT) &&
						order->GetNonStopType() != ONSF_NO_STOP_AT_ANY_STATION &&
						order->GetNonStopType() != ONSF_NO_STOP_AT_DESTINATION_STATION) {
					if (d->via == INVALID_STATION && candidate_via == (StationID)order->GetDestination()) {
						d->via = (StationID)order->GetDestination();
					}
					d->terminus = c;
					d->calling_at.push_back(c);
				}

				/* If we unload all at this station, then it is the terminus. */
				if (order->GetType() == OT_GOTO_STATION &&
						order->GetUnloadType() == OUFB_UNLOAD) {
					if (d->calling_at.size() > 0) {
						found_terminus = true;
					}
					break;
				}

				c.scheduled_date += order->GetWaitTime();

				/* Get the next order, which may be the vehicle's first order. */
				order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
			}

			if (found_terminus) {
				/* Add the departure to the result list. */
				bool duplicate = false;

				if (_settings_client.gui.departure_merge_identical) {
					for (uint i = 0; i < result->size(); ++i) {
						if (*d == *((*result)[i])) {
							duplicate = true;
							break;
						}
					}
				}

				if (!duplicate) {
					result->push_back(d);

					if (_settings_client.gui.departure_smart_terminus && type == D_DEPARTURE) {
						for (uint i = 0; i < result->size() - 1; ++i) {
							Departure *d_first = (*result)[i];
							uint k = d_first->calling_at.size() - 2;
							for (uint j = d->calling_at.size(); j > 0; --j) {
								CallAt c = CallAt(d->calling_at[j - 1]);

								if (d_first->terminus >= c && d_first->calling_at.size() >= 2) {
									d_first->terminus = CallAt(d_first->calling_at[k]);

									if (k == 0) break;

									k--;
								}
							}
						}
					}

					/* If the vehicle is expected to be late, we want to know what time it will arrive rather than depart. */
					/* This is done because it looked silly to me to have a vehicle not be expected for another few days, yet it be at the same time pulling into the station. */
					if (d->status != D_ARRIVED &&
							d->lateness > 0) {
						d->lateness -= least_order->order->GetWaitTime();
					}
				}
			}
		} else {
			/* Computing arrivals: */
			/* First we need to find the origin of the order. This is somewhat like finding a terminus, but a little more involved since order lists are singly linked. */
			/* The next stage is simpler. We just need to add all the stations called at on the way to the current station. */
			/* Again, we define a station as being called at if the vehicle loads from it. */

			/* However, the very first thing we do is use the arrival time as the scheduled time instead of the departure time. */
			d->scheduled_date -= d->scheduled_waiting_time > 0 ? d->scheduled_waiting_time : order->GetWaitTime();

			const Order *candidate_origin = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
			bool found_origin = false;

			while (candidate_origin != least_order->order) {
				if ((candidate_origin->GetLoadType() != OLFB_NO_LOAD ||
						_settings_client.gui.departure_show_all_stops) &&
						(candidate_origin->GetType() == OT_GOTO_STATION ||
						candidate_origin->GetType() == OT_IMPLICIT) &&
						candidate_origin->GetDestination() != station) {
					const Order *o = (candidate_origin->next == nullptr) ? least_order->v->GetFirstOrder() : candidate_origin->next;
					bool found_collision = false;

					/* Check if the candidate origin's destination appears again before the original order or the station does. */
					while (o != least_order->order) {
						if (o->GetUnloadType() == OUFB_UNLOAD) {
							found_collision = true;
							break;
						}

						if ((o->GetType() == OT_GOTO_STATION ||
								o->GetType() == OT_IMPLICIT) &&
								(o->GetDestination() == candidate_origin->GetDestination() ||
								o->GetDestination() == station)) {
							found_collision = true;
							break;
						}

						o = (o->next == nullptr) ? least_order->v->GetFirstOrder() : o->next;
					}

					/* If it doesn't, then we have found the origin. */
					if (!found_collision) {
						found_origin = true;
						break;
					}
				}

				candidate_origin = (candidate_origin->next == nullptr) ? least_order->v->GetFirstOrder() : candidate_origin->next;
			}

			order = (candidate_origin->next == nullptr) ? least_order->v->GetFirstOrder() : candidate_origin->next;

			while (order != least_order->order) {
				if (order->GetType() == OT_GOTO_STATION &&
						(order->GetLoadType() != OLFB_NO_LOAD ||
						_settings_client.gui.departure_show_all_stops)) {
					d->calling_at.push_back(CallAt((StationID)order->GetDestination()));
				}

				order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
			}

			d->terminus = CallAt((StationID)candidate_origin->GetDestination());

			if (found_origin) {
				bool duplicate = false;

				if (_settings_client.gui.departure_merge_identical) {
					for (uint i = 0; i < result->size(); ++i) {
						if (*d == *((*result)[i])) {
							duplicate = true;
							break;
						}
					}
				}

				if (!duplicate) {
					result->push_back(d);
				}
			}
		}

		/* Save on pointer dereferences in the coming loop. */
		order = least_order->order;

		/* Now we find the next suitable order for being a departure for this vehicle. */
		/* We do this in a similar way to finding the first suitable order for the vehicle. */

		/* Go to the next order so we don't add the current order again. */
		order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
		if (VehicleSetNextDepartureTime(&least_order->expected_date, &least_order->scheduled_waiting_time, date_only_scaled, least_order->v, order, false, schdispatch_last_planned_dispatch)) {
			least_order->lateness = 0;
		}

		/* Go through the order list to find the next candidate departure. */
		/* We only need to consider each order at most once. */
		bool found_next_order = false;
		for (int i = least_order->v->GetNumOrders(); i > 0; --i) {
			/* If the order is a conditional branch, handle it. */
			if (order->IsType(OT_CONDITIONAL)) {
				switch(_settings_client.gui.departure_conditionals) {
						case 0: {
							/* Give up */
							break;
						}
						case 1: {
							/* Take the branch */
							order = least_order->v->GetOrder(order->GetConditionSkipToOrder());
							if (order == nullptr) {
								break;
							}

							if (VehicleSetNextDepartureTime(&least_order->expected_date, &least_order->scheduled_waiting_time, date_only_scaled, least_order->v, order, false, schdispatch_last_planned_dispatch)) {
								least_order->lateness = 0;
							}

							continue;
						}
						case 2: {
							/* Do not take the branch */
							order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
							if (VehicleSetNextDepartureTime(&least_order->expected_date, &least_order->scheduled_waiting_time, date_only_scaled, least_order->v, order, false, schdispatch_last_planned_dispatch)) {
								least_order->lateness = 0;
							}
							continue;
						}
				}
			}

			/* Skip it if it's an automatic order. */
			if (order->IsType(OT_IMPLICIT)) {
				order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
				continue;
			}

			/* If an order has a 0 travel time, and it's not explictly set, then stop. */
			if (order->GetTravelTime() == 0 && !order->IsTravelTimetabled()) {
				break;
			}

			/* If the departure is scheduled to be too late, then stop. */
			if (least_order->expected_date - least_order->lateness > max_date) {
				break;
			}

			/* If the order loads from this station (or unloads if we're computing arrivals) and has a wait time set, then it is suitable for being a departure. */
			if ((type == D_DEPARTURE && IsDeparture(order, station)) ||
						(type == D_DEPARTURE && show_vehicles_via && IsVia(order, station)) ||
						(type == D_ARRIVAL && IsArrival(order, station))) {
				least_order->order = order;
				found_next_order = true;
				break;
			}

			order = (order->next == nullptr) ? least_order->v->GetFirstOrder() : order->next;
			if (VehicleSetNextDepartureTime(&least_order->expected_date, &least_order->scheduled_waiting_time, date_only_scaled, least_order->v, order, false, schdispatch_last_planned_dispatch)) {
				least_order->lateness = 0;
			}
		}

		/* If we didn't find a suitable order for being a departure, then we can ignore this vehicle from now on. */
		if (!found_next_order) {
			/* Make sure we don't try to get departures out of this order. */
			/* This is cheaper than deleting it from next_orders. */
			/* If we ever get to a state where _date * DAY_TICKS is close to INT_MAX, then we'll have other problems anyway as departures' scheduled dates will wrap around. */
			least_order->expected_date = INT32_MAX;
		}

		/* The vehicle can't possibly have arrived at its next candidate departure yet. */
		if (least_order->status == D_ARRIVED) {
			least_order->status = D_TRAVELLING;
		}

		/* Find the new least order. */
		for (uint i = 0; i < next_orders.size(); ++i) {
			OrderDate *od = next_orders[i];

			DateTicks lod = least_order->expected_date - least_order->lateness;
			DateTicks odd = od->expected_date - od->lateness;

			if (type == D_ARRIVAL) {
				lod -= least_order->scheduled_waiting_time > 0 ? least_order->scheduled_waiting_time : least_order->order->GetWaitTime();
				odd -= od->scheduled_waiting_time > 0 ? od->scheduled_waiting_time : od->order->GetWaitTime();
			}

			if (lod > odd && od->expected_date - od->lateness < max_date) {
				least_order = od;
			}
		}
	}

	/* Avoid leaking OrderDate structs */
	for (uint i = 0; i < next_orders.size(); ++i) {
		OrderDate *od = next_orders[i];
		delete od;
	}

	if (type == D_DEPARTURE) ScheduledDispatchDepartureLocalFix(result);

	/* Done. Phew! */
	return result;
}
