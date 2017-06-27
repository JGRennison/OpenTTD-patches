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

/** Whether or not a vehicle has arrived for a departure. */
typedef enum {
	D_TRAVELLING = 0,
	D_ARRIVED = 1,
	D_CANCELLED = 2,
} DepartureStatus;

/** The type of departures. */
typedef enum {
	D_DEPARTURE = 0,
	D_ARRIVAL = 1,
} DepartureType;

typedef struct CallAt {
	StationID station;
	DateTicks scheduled_date;

	CallAt(const StationID& s) : station(s), scheduled_date(0) { }
	CallAt(const StationID& s, const DateTicks& t) : station(s), scheduled_date(t) { }
	CallAt(const CallAt& c) : station(c.station), scheduled_date(c.scheduled_date) { }

	inline bool operator==(const CallAt& c) const {
		return this->station == c.station;
	}

	inline bool operator!=(const CallAt& c) const {
		return this->station != c.station;
	}

	inline bool operator>=(const CallAt& c) const {
		return this->station == c.station &&
				this->scheduled_date != 0 &&
				c.scheduled_date != 0 &&
				this->scheduled_date >= c.scheduled_date;
	}

	CallAt& operator=(const CallAt& c) {
		this->station = c.station;
		this->scheduled_date = c.scheduled_date;
		return *this;
	}

	inline bool operator==(StationID s) const {
		return this->station == s;
	}
} CallAt;

/** A scheduled departure. */
typedef struct Departure {
	DateTicksScaled scheduled_date;        ///< The date this departure is scheduled to finish on (i.e. when the vehicle leaves the station)
	Ticks lateness;                        ///< How delayed the departure is expected to be
	CallAt terminus;                       ///< The station at which the vehicle will terminate following this departure
	StationID via;                         ///< The station the departure should list as going via
	SmallVector<CallAt, 32> calling_at;    ///< The stations both called at and unloaded at by the vehicle after this departure before it terminates
	DepartureStatus status;                ///< Whether the vehicle has arrived yet for this departure
	DepartureType type;                    ///< The type of the departure (departure or arrival)
	const Vehicle *vehicle;                ///< The vehicle performing this departure
	const Order *order;                    ///< The order corresponding to this departure
	uint scheduled_waiting_time;           ///< Scheduled waiting time if scheduled dispatch is used
	Departure() : terminus(INVALID_STATION), via(INVALID_STATION), calling_at(), vehicle(NULL) { }
	~Departure()
	{
		calling_at.Reset();
	}

	inline bool operator==(const Departure& d) const {
		if (this->calling_at.Length() != d.calling_at.Length()) return false;

		for (uint i = 0; i < this->calling_at.Length(); ++i) {
			if (*(this->calling_at.Get(i)) != *(d.calling_at.Get(i))) return false;
		}

		return
			(this->scheduled_date / DATE_UNIT_SIZE) == (d.scheduled_date / DATE_UNIT_SIZE) &&
			this->vehicle->type == d.vehicle->type &&
			this->via == d.via &&
			this->type == d.type
			;
	}
} Departure;

typedef SmallVector<Departure*, 32> DepartureList;

#endif /* DEPARTURES_TYPE_H */
