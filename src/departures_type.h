/* $Id: departures_type.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures_type.h Types related to departures. */

#ifndef DEPARTURES_TYPE_H
#define DEPARTURES_TYPE_H

#include "station_base.h"
#include "order_base.h"
#include "vehicle_base.h"
#include "core/bitmath_func.hpp"
#include <vector>

/** Whether or not a vehicle has arrived for a departure. */
enum DepartureStatus : uint8_t {
	D_TRAVELLING = 0,
	D_ARRIVED,
	D_CANCELLED,
	D_SCHEDULED,
};

/** The type of departures. */
enum DepartureType : uint8_t {
	D_DEPARTURE = 0,
	D_ARRIVAL = 1,
};

enum DeparturesSourceMode : uint8_t {
	DSM_LIVE,
	DSM_SCHEDULE_24H,

	DSM_END
};

struct CallAt {
	StationID station;
	StateTicks scheduled_tick;

	CallAt(const StationID& s) : station(s), scheduled_tick(0) { }
	CallAt(const StationID& s, StateTicks t) : station(s), scheduled_tick(t) { }
	CallAt(const CallAt& c) : station(c.station), scheduled_tick(c.scheduled_tick) { }

	inline bool operator==(const CallAt& c) const
	{
		return this->station == c.station;
	}

	CallAt& operator=(const CallAt& c) = default;
};

struct RemoveVia {
	StationID via;
	uint calling_at_offset;
};

enum DepartureShowAs : uint8_t {
	DSA_NORMAL,
	DSA_VIA,
};

/** A scheduled departure. */
struct Departure {
	StateTicks scheduled_tick = 0;         ///< The tick this departure is scheduled to finish on (i.e. when the vehicle leaves the station)
	Ticks lateness = 0;                    ///< How delayed the departure is expected to be
	StationID via = INVALID_STATION;       ///< The station the departure should list as going via
	StationID via2 = INVALID_STATION;      ///< Secondary station the departure should list as going via
	CallAt terminus = INVALID_STATION;     ///< The station at which the vehicle will terminate following this departure
	std::vector<CallAt> calling_at;        ///< The stations both called at and unloaded at by the vehicle after this departure before it terminates
	std::vector<RemoveVia> remove_vias;    ///< Vias to remove when using smart terminus.
	DepartureStatus status{};              ///< Whether the vehicle has arrived yet for this departure
	DepartureType type{};                  ///< The type of the departure (departure or arrival)
	DepartureShowAs show_as = DSA_NORMAL;  ///< Show as type
	const Vehicle *vehicle = nullptr;      ///< The vehicle performing this departure
	const Order *order = nullptr;          ///< The order corresponding to this departure
	Ticks scheduled_waiting_time = 0;      ///< Scheduled waiting time if scheduled dispatch is used

	inline bool operator==(const Departure& d) const {
		if (this->calling_at.size() != d.calling_at.size()) return false;

		for (uint i = 0; i < this->calling_at.size(); ++i) {
			if (this->calling_at[i] != d.calling_at[i]) return false;
		}

		const Ticks timetable_unit_size = TimetableDisplayUnitSize();

		return
			(this->scheduled_tick.base() / timetable_unit_size) == (d.scheduled_tick.base() / timetable_unit_size) &&
			this->vehicle->type == d.vehicle->type &&
			this->via == d.via &&
			this->via2 == d.via2 &&
			this->type == d.type &&
			this->show_as == d.show_as;
	}

	inline Ticks EffectiveWaitingTime() const
	{
		if (this->scheduled_waiting_time > 0) {
			return this->scheduled_waiting_time;
		} else {
			return this->order->GetWaitTime();
		}
	}

	inline void ShiftTimes(StateTicksDelta delta)
	{
		this->scheduled_tick += delta;
		auto adjust_call = [&](CallAt &c) {
			if (c.scheduled_tick != 0) c.scheduled_tick += delta;
		};
		adjust_call(this->terminus);
		for (CallAt &c : this->calling_at) {
			adjust_call(c);
		}
	}
};

struct DepartureOrderDestinationDetector {
	OrderTypeMask order_type_mask = 0;
	DestinationID destination;

	bool OrderMatches(const Order *order) const
	{
		if (!(HasBit(this->order_type_mask, order->GetType()) && order->GetDestination() == this->destination)) return false;

		if (order->IsType(OT_GOTO_DEPOT) && (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) return false; // Filter out go to nearest depot orders

		return true;
	}

	bool StationMatches(StationID station) const
	{
		return HasBit(this->order_type_mask, OT_GOTO_STATION) && station == this->destination;
	}
};

struct DepartureCallingSettings {
private:
	uint8_t flags = 0;

	struct FlagBits {
		enum {
			AllowVia = 0,
			CheckShowAsViaType,
			DepartureNoLoadTest,
			ShowAllStops,
			ShowPax,
			ShowFreight,
			SmartTerminusEnabled,
		};
	};

public:
	inline bool AllowVia() const { return HasBit(this->flags, FlagBits::AllowVia); }
	inline bool CheckShowAsViaType() const { return HasBit(this->flags, FlagBits::CheckShowAsViaType); }
	inline bool DepartureNoLoadTest() const { return HasBit(this->flags, FlagBits::DepartureNoLoadTest); }
	inline bool ShowAllStops() const { return HasBit(this->flags, FlagBits::ShowAllStops); }
	inline bool ShowPax() const { return HasBit(this->flags, FlagBits::ShowPax); }
	inline bool ShowFreight() const { return HasBit(this->flags, FlagBits::ShowFreight); }
	inline bool SmartTerminusEnabled() const { return HasBit(this->flags, FlagBits::SmartTerminusEnabled); }

	inline void SetViaMode(bool allow_via, bool check_show_as_via_type)
	{
		AssignBit(this->flags, FlagBits::AllowVia, allow_via);
		AssignBit(this->flags, FlagBits::CheckShowAsViaType, check_show_as_via_type);
	}
	inline void SetDepartureNoLoadTest(bool no_test)
	{
		AssignBit(this->flags, FlagBits::DepartureNoLoadTest, no_test);
	}
	inline void SetShowAllStops(bool all_stops)
	{
		AssignBit(this->flags, FlagBits::ShowAllStops, all_stops);
	}
	inline void SetCargoFilter(bool pax, bool freight)
	{
		AssignBit(this->flags, FlagBits::ShowPax, pax);
		AssignBit(this->flags, FlagBits::ShowFreight, freight);
	}
	inline void SetSmartTerminusEnabled(bool enabled)
	{
		AssignBit(this->flags, FlagBits::SmartTerminusEnabled, enabled);
	}

	bool IsDeparture(const Order *order, const DepartureOrderDestinationDetector &source) const;
	bool IsArrival(const Order *order, const DepartureOrderDestinationDetector &source) const;
	DepartureShowAs GetShowAsType(const Order *order, DepartureType type) const;
};

typedef std::vector<std::unique_ptr<Departure>> DepartureList;

#endif /* DEPARTURES_TYPE_H */
