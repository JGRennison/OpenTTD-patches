/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_cmd.h Functions related to order commands. */

#ifndef ORDER_CMD_H
#define ORDER_CMD_H

#include "order_base.h"
#include "order_func.h"
#include "vehicle_base.h"

/**
 * Removes all orders from a vehicle for which order_predicate returns true.
 * Handles timetable updating, removing implicit orders correctly, etc.
 * @param v The vehicle.
 * @param order_predicate Functor with signature: bool (const Order *)
 */
template <typename F> void RemoveVehicleOrdersIf(Vehicle * const v, F order_predicate) {
	/* Clear the order from the order-list */
	Order *order;
	int id = -1;
	FOR_VEHICLE_ORDERS(v, order) {
		id++;
restart:

		if (order_predicate(const_cast<const Order *>(order))) {
			/* We want to clear implicit orders, but we don't want to make them
			 * dummy orders. They should just vanish. Also check the actual order
			 * type as ot is currently OT_GOTO_STATION. */
			if (order->IsType(OT_IMPLICIT)) {
				order = order->next; // DeleteOrder() invalidates current order
				DeleteOrder(v, id);
				if (order != NULL) goto restart;
				break;
			}

			/* Clear wait time */
			if (!order->IsType(OT_CONDITIONAL)) v->orders.list->UpdateTotalDuration(-order->GetWaitTime());
			if (order->IsWaitTimetabled()) {
				if (!order->IsType(OT_CONDITIONAL)) v->orders.list->UpdateTimetableDuration(-order->GetTimetabledWait());
				order->SetWaitTimetabled(false);
			}
			order->SetWaitTime(0);

			/* Clear order, preserving travel time */
			bool travel_timetabled = order->IsTravelTimetabled();
			order->MakeDummy();
			order->SetTravelTimetabled(travel_timetabled);

			for (const Vehicle *w = v->FirstShared(); w != NULL; w = w->NextShared()) {
				/* In GUI, simulate by removing the order and adding it back */
				InvalidateVehicleOrder(w, id | (INVALID_VEH_ORDER_ID << 8));
				InvalidateVehicleOrder(w, (INVALID_VEH_ORDER_ID << 8) | id);
			}
		}
	}
}

#endif /* ORDER_CMD_H */
