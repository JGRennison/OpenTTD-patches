/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_cmd.cpp Handling of orders. */

#include "stdafx.h"
#include "debug.h"
#include "command_func.h"
#include "company_func.h"
#include "news_func.h"
#include "strings_func.h"
#include "timetable.h"
#include "station_base.h"
#include "station_map.h"
#include "station_func.h"
#include "map_func.h"
#include "cargotype.h"
#include "vehicle_func.h"
#include "depot_base.h"
#include "core/bitmath_func.hpp"
#include "core/container_func.hpp"
#include "core/pool_func.hpp"
#include "core/random_func.hpp"
#include "aircraft.h"
#include "roadveh.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "company_base.h"
#include "infrastructure_func.h"
#include "order_backup.h"
#include "order_bulk.h"
#include "order_cmd.h"
#include "cheat_type.h"
#include "viewport_func.h"
#include "order_dest_func.h"
#include "vehiclelist.h"
#include "tracerestrict.h"
#include "train.h"
#include "date_func.h"
#include "schdispatch.h"
#include "timetable_cmd.h"
#include "train_cmd.h"

#include "table/strings.h"

#include <limits>
#include <ranges>
#include <vector>

#include "safeguards.h"

/* DestinationID must be at least as large as every these below, because it can
 * be any of them
 */
static_assert(sizeof(DestinationID) >= sizeof(DepotID));
static_assert(sizeof(DestinationID) >= sizeof(StationID));
static_assert(sizeof(DestinationID) >= sizeof(TraceRestrictSlotID));

/* OrderTypeMask must be large enough for all order types */
static_assert(std::numeric_limits<OrderTypeMask>::digits >= OT_END);

OrderPool _order_pool("Order");
INSTANTIATE_POOL_METHODS(Order)
OrderListPool _orderlist_pool("OrderList");
INSTANTIATE_POOL_METHODS(OrderList)

btree::btree_map<uint32_t, uint32_t> _order_destination_refcount_map;
bool _order_destination_refcount_map_valid = false;

enum class CmdInsertOrderIntlFlag : uint8_t {
	AllowLoadByCargoType,   ///< Allow load by cargo type
	AllowDuplicateUnbunch,  ///< Allow duplicate unbunch orders
	NoUnbunchChecks,        ///< Do not perform unbunch checks
	NoConditionTargetCheck, ///< Do not check target of conditional jump orders
};
using CmdInsertOrderIntlFlags = EnumBitSet<CmdInsertOrderIntlFlag, uint8_t>;

static CommandCost CmdInsertOrderIntl(DoCommandFlags flags, Vehicle *v, VehicleOrderID sel_ord, const Order &new_order, CmdInsertOrderIntlFlags insert_flags);

void IntialiseOrderDestinationRefcountMap()
{
	ClearOrderDestinationRefcountMap();
	for (const Vehicle *v : Vehicle::IterateFrontOnly()) {
		if (v != v->FirstShared()) continue;
		for (const Order *order : v->Orders()) {
			if (order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT)) {
				_order_destination_refcount_map[OrderDestinationRefcountMapKey(order->GetDestination().ToStationID(), v->owner, order->GetType(), v->type)]++;
			}
		}
	}
	_order_destination_refcount_map_valid = true;
}

void ClearOrderDestinationRefcountMap()
{
	_order_destination_refcount_map.clear();
	_order_destination_refcount_map_valid = false;
}

void UpdateOrderDestinationRefcount(const Order *order, VehicleType type, Owner owner, int delta)
{
	if (order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT)) {
		_order_destination_refcount_map[OrderDestinationRefcountMapKey(order->GetDestination().ToStationID(), owner, order->GetType(), type)] += delta;
	}
}

void Order::InvalidateGuiOnRemove()
{
	/* We can visit oil rigs and buoys that are not our own. They will be shown in
	 * the list of stations. So, we need to invalidate that window if needed. */
	if (this->IsType(OT_GOTO_STATION) || this->IsType(OT_GOTO_WAYPOINT)) {
		BaseStation *bs = BaseStation::GetIfValid(this->GetDestination().ToStationID());
		if (bs != nullptr && bs->owner == OWNER_NONE) InvalidateWindowClassesData(WC_STATION_LIST);
	}
}

/**
 * 'Free' the order
 * @note ONLY use on "current_order" vehicle orders!
 */
void Order::Free()
{
	this->type  = OT_NOTHING;
	this->flags = 0;
	this->dest  = 0;
	DeAllocExtraInfo();
}

/**
 * Makes this order a Go To Station order.
 * @param destination the station to go to.
 */
void Order::MakeGoToStation(StationID destination)
{
	this->type = OT_GOTO_STATION;
	this->flags = 0;
	this->dest = destination;
}

/**
 * Makes this order a Go To Depot order.
 * @param destination   the depot to go to.
 * @param order         is this order a 'default' order, or an overridden vehicle order?
 * @param non_stop_type how to get to the depot?
 * @param action        what to do in the depot?
 * @param cargo         the cargo type to change to.
 */
void Order::MakeGoToDepot(DestinationID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type, OrderDepotActionFlags action, CargoType cargo)
{
	this->type = OT_GOTO_DEPOT;
	this->SetDepotOrderType(order);
	this->SetDepotActionType(action);
	this->SetNonStopType(non_stop_type);
	this->dest = destination;
	this->SetRefit(cargo);
}

/**
 * Makes this order a Go To Waypoint order.
 * @param destination the waypoint to go to.
 */
void Order::MakeGoToWaypoint(StationID destination)
{
	this->type = OT_GOTO_WAYPOINT;
	this->flags = 0;
	this->dest = destination;
}

/**
 * Makes this order a Loading order.
 * @param ordered is this an ordered stop?
 */
void Order::MakeLoading(bool ordered)
{
	this->type = OT_LOADING;
	if (!ordered) this->flags = 0;
}

/**
 * Update the jump counter, for percent probability
 * conditional orders
 *
 * Not that jump_counter is signed and may become
 * negative when a jump has been taken
 *
 * @param percent the jump chance in %.
 * @param dry_run whether this is a dry-run, so do not execute side-effects
 *
 * @return true if the jump should be taken
 */
bool Order::UpdateJumpCounter(uint8_t percent, bool dry_run)
{
	const int8_t jump_counter = this->GetJumpCounter();
	if (dry_run) return jump_counter >= 0;
	if (jump_counter >= 0) {
		this->SetJumpCounter(jump_counter + (percent - 100));
		return true;
	}
	this->SetJumpCounter(jump_counter + percent);
	return false;
}

/**
 * Makes this order a Leave Station order.
 */
void Order::MakeLeaveStation()
{
	this->type = OT_LEAVESTATION;
	this->flags = 0;
}

/**
 * Makes this order a Dummy order.
 */
void Order::MakeDummy()
{
	this->type = OT_DUMMY;
	this->flags = 0;
}

/**
 * Makes this order an conditional order.
 * @param order the order to jump to.
 */
void Order::MakeConditional(VehicleOrderID order)
{
	this->type = OT_CONDITIONAL;
	this->flags = order;
	this->dest = 0;
}

/**
 * Makes this order an implicit order.
 * @param destination the station to go to.
 */
void Order::MakeImplicit(StationID destination)
{
	this->type = OT_IMPLICIT;
	this->dest = destination;
}

void Order::MakeWaiting()
{
	const bool wait_timetabled = this->IsWaitTimetabled();
	this->type = OT_WAITING;
	this->SetWaitTimetabled(wait_timetabled);
}

void Order::MakeLoadingAdvance(StationID destination)
{
	this->type = OT_LOADING_ADVANCE;
	this->dest = destination;
}

void Order::MakeReleaseSlot()
{
	this->type = OT_SLOT;
	this->dest = INVALID_TRACE_RESTRICT_SLOT_ID;
	this->flags = OSST_RELEASE;
}

void Order::MakeTryAcquireSlot()
{
	this->type = OT_SLOT;
	this->dest = INVALID_TRACE_RESTRICT_SLOT_ID;
	this->flags = OSST_TRY_ACQUIRE;
}

void Order::MakeReleaseSlotGroup()
{
	this->type = OT_SLOT_GROUP;
	this->dest = INVALID_TRACE_RESTRICT_SLOT_ID;
	this->flags = OSGST_RELEASE;
}

void Order::MakeChangeCounter()
{
	this->type = OT_COUNTER;
	this->dest = INVALID_TRACE_RESTRICT_COUNTER_ID;
	this->flags = 0;
}

void Order::MakeLabel(OrderLabelSubType subtype)
{
	this->type = OT_LABEL;
	this->flags = subtype;
}

/**
 * Make this depot/station order also a refit order.
 * @param cargo   the cargo type to change to.
 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
 */
void Order::SetRefit(CargoType cargo)
{
	this->refit_cargo = cargo;
}

/**
 * Does this order have the same type, flags and destination?
 * @param other the second order to compare to.
 * @return true if the type, flags and destination match.
 */
bool Order::Equals(const Order &other) const
{
	/* In case of go to nearest depot orders we need "only" compare the flags
	 * with the other and not the nearest depot order bit or the actual
	 * destination because those get clear/filled in during the order
	 * evaluation. If we do not do this the order will continuously be seen as
	 * a different order and it will try to find a "nearest depot" every tick. */
	if ((this->IsType(OT_GOTO_DEPOT) && this->type == other.type) &&
			((this->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0 ||
			 (other.GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0)) {
		return this->GetDepotOrderType() == other.GetDepotOrderType() &&
				(this->GetDepotActionType() & ~ODATFB_NEAREST_DEPOT) == (other.GetDepotActionType() & ~ODATFB_NEAREST_DEPOT);
	}

	return this->type == other.type && this->flags == other.flags && this->dest == other.dest;
}

/**
 * Is this order derived from another order.
 * @param other the second order to compare to.
 * @return true the order is derived from the other order.
 */
bool Order::IsDerivedFrom(const Order &other) const
{
	if (this->IsAnyLoadingType() || this->IsType(OT_GOTO_STATION)) {
		return other.IsType(OT_GOTO_STATION) && this->dest == other.dest;
	}

	return this->Equals(other);
}

/**
 * Pack this order into a 16 bits integer as close to the TTD
 * representation as possible.
 * @return the TTD-like packed representation.
 */
uint16_t Order::MapOldOrder() const
{
	uint16_t order = this->GetType();
	switch (this->GetType()) {
		case OT_GOTO_STATION:
			if (this->GetUnloadType() & OUFB_UNLOAD) SetBit(order, 5);
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			if (this->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) SetBit(order, 7);
			order |= GB(this->GetDestination().value, 0, 8) << 8;
			break;
		case OT_GOTO_DEPOT:
			if (!(this->GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) SetBit(order, 6);
			SetBit(order, 7);
			order |= GB(this->GetDestination().value, 0, 8) << 8;
			break;
		case OT_LOADING:
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			/* If both "no load" and "no unload" are set, return nothing order instead */
			if ((this->GetLoadType() & OLFB_NO_LOAD) && (this->GetUnloadType() & OUFB_NO_UNLOAD)) {
				order = OT_NOTHING;
			}
			break;
		default:
			break;
	}
	return order;
}

/**
 *
 * Updates the widgets of a vehicle which contains the order-data
 *
 */
void InvalidateVehicleOrder(const Vehicle *v, int data)
{
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);
	SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);

	if (data != 0) {
		/* Calls SetDirty() too */
		InvalidateWindowData(WC_VEHICLE_ORDERS,    v->index, data);
		InvalidateWindowData(WC_VEHICLE_TIMETABLE, v->index, data);
		return;
	}

	SetWindowDirty(WC_VEHICLE_ORDERS,    v->index);
	SetWindowDirty(WC_VEHICLE_TIMETABLE, v->index);
}

/**
 *
 * Updates the widgets of a vehicle which contains the order-data
 *
 */
void InvalidateVehicleOrderOnMove(const Vehicle *v, VehicleOrderID from, VehicleOrderID to, uint16_t count)
{
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);
	SetWindowDirty(WC_SCHDISPATCH_SLOTS, v->index);

	extern void InvalidateOrderListWindowOnOrderMove(VehicleID veh, VehicleOrderID from, VehicleOrderID to, uint16_t count);
	extern void InvalidateTimetableListWindowOnOrderMove(VehicleID veh, VehicleOrderID from, VehicleOrderID to, uint16_t count);
	InvalidateOrderListWindowOnOrderMove(v->index, from, to, count);
	InvalidateTimetableListWindowOnOrderMove(v->index, from, to, count);
}

const char *Order::GetLabelText() const
{
	assert(this->IsType(OT_LABEL) && this->GetLabelSubType() == OLST_TEXT);
	if (this->extra == nullptr) return "";
	const char *text = (const char *)(this->extra->cargo_type_flags);
	if (ttd_strnlen(text, lengthof(this->extra->cargo_type_flags)) == lengthof(this->extra->cargo_type_flags)) {
		/* Not null terminated, give up */
		return "";
	}
	return text;
}

void Order::SetLabelText(std::string_view text)
{
	assert(this->IsType(OT_LABEL) && this->GetLabelSubType() == OLST_TEXT);
	this->CheckExtraInfoAlloced();
	strecpy(std::span((char *)(this->extra->cargo_type_flags), lengthof(this->extra->cargo_type_flags)), text);
}

/**
 *
 * Assign data to an order (from another order)
 *   This function makes sure that the index is maintained correctly
 * @param other the data to copy (except next pointer).
 *
 */
void Order::AssignOrder(const Order &other)
{
	this->type  = other.type;
	this->flags = other.flags;
	this->dest  = other.dest;

	this->refit_cargo   = other.refit_cargo;

	this->wait_time   = other.wait_time;

	this->travel_time = other.travel_time;
	this->max_speed   = other.max_speed;

	this->occupancy = other.occupancy;

	if (other.extra != nullptr && (this->GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD || this->GetLoadType() == OLFB_CARGO_TYPE_LOAD
			|| (this->IsType(OT_LABEL) && this->GetLabelSubType() == OLST_TEXT)
			|| other.extra->xdata != 0 || other.extra->xdata2 != 0 || other.extra->xflags != 0 || other.extra->dispatch_index != 0 || other.extra->colour != 0)) {
		this->AllocExtraInfo();
		*(this->extra) = *(other.extra);
	} else {
		this->DeAllocExtraInfo();
	}
}

void Order::AllocExtraInfo()
{
	if (!this->extra) {
		this->extra.reset(new OrderExtraInfo());
	}
}

void Order::DeAllocExtraInfo()
{
	this->extra.reset();
}

void CargoStationIDVectorSet::FillNextStoppingStation(const Vehicle *v, const OrderList *o, const Order *first, uint hops)
{
	this->more.clear();
	this->first = o->GetNextStoppingStation(v, ALL_CARGOTYPES, first, hops);
	if (this->first.cargo_mask != ALL_CARGOTYPES) {
		CargoTypes have_cargoes = this->first.cargo_mask;
		do {
			this->more.push_back(o->GetNextStoppingStation(v, ~have_cargoes, first, hops));
			have_cargoes |= this->more.back().cargo_mask;
		} while (have_cargoes != ALL_CARGOTYPES);
	}
}

/**
 * Recomputes everything.
 * @param v one of vehicle that is using this orderlist
 */
void OrderList::Initialize(Vehicle *v)
{
	this->first_shared = v;

	this->num_manual_orders = 0;
	this->num_vehicles = 1;
	this->timetable_duration = 0;
	this->total_duration = 0;

	VehicleType type = v->type;
	Owner owner = v->owner;

	for (const Order *o : this->Orders()) {
		if (!o->IsType(OT_IMPLICIT)) ++this->num_manual_orders;
		if (!o->IsType(OT_CONDITIONAL)) {
			this->timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
			this->total_duration += o->GetWaitTime() + o->GetTravelTime();
		}
		RegisterOrderDestination(o, type, owner);
	}

	for (Vehicle *u = this->first_shared->PreviousShared(); u != nullptr; u = u->PreviousShared()) {
		++this->num_vehicles;
		this->first_shared = u;
	}

	for (const Vehicle *u = v->NextShared(); u != nullptr; u = u->NextShared()) ++this->num_vehicles;
}

/**
 * Recomputes Timetable duration.
 * Split out into a separate function so it can be used by afterload.
 */
void OrderList::RecalculateTimetableDuration()
{
	this->timetable_duration = 0;
	for (const Order *o : this->Orders()) {
		if (!o->IsType(OT_CONDITIONAL)) {
			this->timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
		}
	}
}

/**
 * Free a complete order chain.
 * @param keep_orderlist If this is true only delete the orders, otherwise also delete the OrderList.
 * @note do not use on "current_order" vehicle orders!
 */
void OrderList::FreeChain(bool keep_orderlist)
{
	VehicleType type = this->GetFirstSharedVehicle()->type;
	Owner owner = this->GetFirstSharedVehicle()->owner;
	for (Order *o : this->Orders()) {
		UnregisterOrderDestination(o, type, owner);
		if (!CleaningPool()) o->InvalidateGuiOnRemove();
	}
	this->orders.clear();

	if (keep_orderlist) {
		this->num_manual_orders = 0;
		this->timetable_duration = 0;
		this->total_duration = 0;
	} else {
		delete this;
	}
}

/**
 * Get the next order which will make the given vehicle stop at a station
 * or refit at a depot or evaluate a non-trivial condition.
 * @param next The order to start looking at.
 * @param hops The number of orders we have already looked at.
 * @param cargo_mask The bit set of cargoes that the we are looking at, this may be reduced to indicate the set of cargoes that the result is valid for. This may be 0 to ignore cargo types entirely.
 * @return Either of
 *         \li a station order
 *         \li a refitting depot order
 *         \li a non-trivial conditional order
 *         \li nullptr  if the vehicle won't stop anymore.
 */
const Order *OrderList::GetNextDecisionNode(const Order *next, uint hops, CargoTypes &cargo_mask) const
{
	if (hops > std::min<uint>(64, this->GetNumOrders()) || next == nullptr) return nullptr;

	if (next->IsType(OT_CONDITIONAL)) {
		if (next->GetConditionVariable() != OCV_UNCONDITIONALLY) return next;

		/* We can evaluate trivial conditions right away. They're conceptually
		 * the same as regular order progression. */
		return this->GetNextDecisionNode(
				this->GetOrderAt(next->GetConditionSkipToOrder()),
				hops + 1, cargo_mask);
	}

	if (next->IsType(OT_GOTO_DEPOT)) {
		if ((next->GetDepotActionType() & ODATFB_HALT) != 0) return nullptr;
		if (next->IsRefit()) return next;
	}

	bool can_load_or_unload = false;
	if ((next->IsType(OT_GOTO_STATION) || next->IsType(OT_IMPLICIT)) &&
			(next->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) {
		if (cargo_mask == 0) {
			can_load_or_unload = true;
		} else if (next->GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD || next->GetLoadType() == OLFB_CARGO_TYPE_LOAD) {
			/* This is a cargo-specific load/unload order.
			 * If the first cargo is both a no-load and no-unload order, skip it.
			 * Drop cargoes which don't match the first one. */
			can_load_or_unload = CargoMaskValueFilter<bool>(cargo_mask, [&](CargoType cargo) {
				return ((next->GetCargoLoadType(cargo) & OLFB_NO_LOAD) == 0 || (next->GetCargoUnloadType(cargo) & OUFB_NO_UNLOAD) == 0);
			});
		} else if ((next->GetLoadType() & OLFB_NO_LOAD) == 0 || (next->GetUnloadType() & OUFB_NO_UNLOAD) == 0) {
			can_load_or_unload = true;
		}
	}

	if (!can_load_or_unload) {
		return this->GetNextDecisionNode(this->GetNext(next), hops + 1, cargo_mask);
	}

	return next;
}

/**
 * Recursively determine the next deterministic station to stop at.
 * @param v The vehicle we're looking at.
 * @param CargoTypes cargo_mask Bit-set of the cargo IDs of interest. This may be 0 to ignore cargo types entirely.
 * @param first Order to start searching at or nullptr to start at cur_implicit_order_index + 1.
 * @param hops Number of orders we have already looked at.
 * @return A CargoMaskedStationIDVector of the cargo mask the result is valid for, and the next stopping station or StationID::Invalid().
 * @pre The vehicle is currently loading and v->last_station_visited is meaningful.
 * @note This function may draw a random number. Don't use it from the GUI.
 */
CargoMaskedStationIDVector OrderList::GetNextStoppingStation(const Vehicle *v, CargoTypes cargo_mask, const Order *first, uint hops) const
{
	static std::vector<bool> seen_orders_container;
	if (hops == 0) {
		if (this->GetNumOrders() == 0) return CargoMaskedStationIDVector(cargo_mask); // No orders at all
		seen_orders_container.assign(this->GetNumOrders(), false);
	}

	const Order *next = first;
	if (first == nullptr) {
		next = this->GetOrderAt(v->cur_implicit_order_index);
		if (next == nullptr) {
			next = this->GetFirstOrder();
			if (next == nullptr) return CargoMaskedStationIDVector(cargo_mask);
		} else {
			/* GetNext never returns nullptr if there is a valid station in the list.
			 * As the given "next" is already valid and a station in the list, we
			 * don't have to check for nullptr here. */
			next = this->GetNext(next);
			assert(next != nullptr);
		}
	}

	const std::span<Order> order_span = v->orders->GetOrderVector();
	auto seen_order = [&](const Order *o) -> std::vector<bool>::reference { return seen_orders_container[o - order_span.data()]; };

	do {
		if (seen_order(next)) return CargoMaskedStationIDVector(cargo_mask); // Already handled

		const Order *decision_node = this->GetNextDecisionNode(next, ++hops, cargo_mask);

		if (decision_node == nullptr || seen_order(decision_node)) return CargoMaskedStationIDVector(cargo_mask); // Invalid or already handled

		seen_order(next) = true;
		seen_order(decision_node) = true;

		next = decision_node;

		/* Resolve possibly nested conditionals by estimation. */
		while (next->IsType(OT_CONDITIONAL)) {
			/* We return both options of conditional orders. */
			const Order *skip_to = &(order_span[next->GetConditionSkipToOrder()]);
			if (!seen_order(skip_to)) skip_to = this->GetNextDecisionNode(skip_to, hops, cargo_mask);
			const Order *advance = this->GetNext(next);
			if (!seen_order(advance)) advance = this->GetNextDecisionNode(advance, hops, cargo_mask);

			if (advance == nullptr || advance == first || skip_to == advance || seen_order(advance)) {
				next = (skip_to == first) ? nullptr : skip_to;
			} else if (skip_to == nullptr || skip_to == first || seen_order(skip_to)) {
				next = (advance == first) ? nullptr : advance;
			} else {
				CargoMaskedStationIDVector st1 = this->GetNextStoppingStation(v, cargo_mask, skip_to, hops);
				cargo_mask &= st1.cargo_mask;
				CargoMaskedStationIDVector st2 = this->GetNextStoppingStation(v, cargo_mask, advance, hops);
				st1.cargo_mask &= st2.cargo_mask;
				st1.station.insert(st1.station.end(), st2.station.begin(), st2.station.end());
				return st1;
			}

			if (next == nullptr || seen_order(next)) return CargoMaskedStationIDVector(cargo_mask);
			++hops;
		}

		/* Don't return a next stop if the vehicle has to unload everything. */
		if ((next->IsType(OT_GOTO_STATION) || next->IsType(OT_IMPLICIT)) &&
				next->GetDestination() == v->last_station_visited && cargo_mask != 0) {
			/* This is a cargo-specific load/unload order.
			 * Don't return a next stop if first cargo has transfer or unload set.
			 * Drop cargoes which don't match the first one. */
			bool invalid = CargoMaskValueFilter<bool>(cargo_mask, [&](CargoType cargo) {
				return ((next->GetCargoUnloadType(cargo) & (OUFB_TRANSFER | OUFB_UNLOAD)) != 0);
			});
			if (invalid) return CargoMaskedStationIDVector(cargo_mask);
		}
	} while (next->IsType(OT_GOTO_DEPOT) || next->IsSlotCounterOrder() || next->IsType(OT_DUMMY) || next->IsType(OT_LABEL)
			|| (next->IsBaseStationOrder() && next->GetDestination() == v->last_station_visited));

	return CargoMaskedStationIDVector(cargo_mask, { next->GetDestination().ToStationID() });
}

/**
 * Insert a new order into the order chain.
 * @param ins_order is the order to insert into the chain.
 * @param index is the position where the order is supposed to be inserted.
 */
void OrderList::InsertOrderAt(Order &&ins_order, VehicleOrderID index)
{
	if (index >= this->orders.size()) {
		index = (VehicleOrderID)this->orders.size();
	}

	Order *new_order = &*this->orders.emplace(this->orders.begin() + index, std::move(ins_order));

	if (!new_order->IsType(OT_IMPLICIT)) ++this->num_manual_orders;
	if (!new_order->IsType(OT_CONDITIONAL)) {
		this->timetable_duration += new_order->GetTimetabledWait() + new_order->GetTimetabledTravel();
		this->total_duration += new_order->GetWaitTime() + new_order->GetTravelTime();
	}
	RegisterOrderDestination(new_order, this->GetFirstSharedVehicle()->type, this->GetFirstSharedVehicle()->owner);

	/* We can visit oil rigs and buoys that are not our own. They will be shown in
	 * the list of stations. So, we need to invalidate that window if needed. */
	if (new_order->IsType(OT_GOTO_STATION) || new_order->IsType(OT_GOTO_WAYPOINT)) {
		BaseStation *bs = BaseStation::Get(new_order->GetDestination().ToStationID());
		if (bs->owner == OWNER_NONE) InvalidateWindowClassesData(WC_STATION_LIST);
	}

}


/**
 * Remove an order from the order list and delete it.
 * @param index is the position of the order which is to be deleted.
 */
void OrderList::DeleteOrderAt(VehicleOrderID index)
{
	if (index >= this->GetNumOrders()) return;

	Order *to_remove = &(this->orders[index]);

	if (!to_remove->IsType(OT_IMPLICIT)) --this->num_manual_orders;
	if (!to_remove->IsType(OT_CONDITIONAL)) {
		this->timetable_duration -= (to_remove->GetTimetabledWait() + to_remove->GetTimetabledTravel());
		this->total_duration -= (to_remove->GetWaitTime() + to_remove->GetTravelTime());
	}
	UnregisterOrderDestination(to_remove, this->GetFirstSharedVehicle()->type, this->GetFirstSharedVehicle()->owner);

	to_remove->InvalidateGuiOnRemove();

	this->orders.erase(this->orders.begin() + index);
}

/**
 * Move an order to another position within the order list.
 * @param from is the zero-based position of the orders to move.
 * @param to is the zero-based position where the orders are moved to.
 * @param count is the number of orders to move
 */
void OrderList::MoveOrders(VehicleOrderID from, VehicleOrderID to, uint16_t count)
{
	if (count == 0 || from >= this->GetNumOrders() || to >= this->GetNumOrders() || from == to) return;

	if (from < to) {
		if (to < from + count) return;
		/* Rotate from towards end */
		const auto it = this->orders.begin();
		std::rotate(it + from, it + from + count, it + to + count);
	} else {
		if (from + count > this->GetNumOrders()) return;
		/* Rotate from towards begin */
		const auto it = this->orders.begin();
		std::rotate(it + to, it + from, it + from + count);
	}
}

/**
 * Removes the vehicle from the shared order list.
 * @note This is supposed to be called when the vehicle is still in the chain
 * @param v vehicle to remove from the list
 */
void OrderList::RemoveVehicle(Vehicle *v)
{
	--this->num_vehicles;
	if (v == this->first_shared) this->first_shared = v->NextShared();
}

/**
 * Checks whether all orders of the list have a filled timetable.
 * @return whether all orders have a filled timetable.
 */
bool OrderList::IsCompleteTimetable() const
{
	for (const Order *o : this->Orders()) {
		/* Implicit orders are, by definition, not timetabled. */
		if (o->IsType(OT_IMPLICIT)) continue;
		if (!o->IsCompletelyTimetabled()) return false;
	}
	return true;
}

#ifdef WITH_ASSERT
/**
 * Checks for internal consistency of order list. Triggers assertion if something is wrong.
 */
void OrderList::DebugCheckSanity() const
{
	VehicleOrderID check_num_orders = 0;
	VehicleOrderID check_num_manual_orders = 0;
	uint check_num_vehicles = 0;
	Ticks check_timetable_duration = 0;
	Ticks check_total_duration = 0;

	Debug(misc, 6, "Checking OrderList {} for sanity...", this->index);

	for (const Order *o : this->Orders()) {
		++check_num_orders;
		if (!o->IsType(OT_IMPLICIT)) ++check_num_manual_orders;
		if (!o->IsType(OT_CONDITIONAL)) {
			check_timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
			check_total_duration += o->GetWaitTime() + o->GetTravelTime();
		}
	}
	assert_msg(this->GetNumOrders() == check_num_orders, "{}, {}", this->GetNumOrders(), check_num_orders);
	assert_msg(this->num_manual_orders == check_num_manual_orders, "{}, {}", this->num_manual_orders, check_num_manual_orders);
	assert_msg(this->timetable_duration == check_timetable_duration, "{}, {}", this->timetable_duration, check_timetable_duration);
	assert_msg(this->total_duration == check_total_duration, "{}, {}", this->total_duration, check_total_duration);

	for (const Vehicle *v = this->first_shared; v != nullptr; v = v->NextShared()) {
		++check_num_vehicles;
		assert_msg(v->orders == this, "{}, {}", fmt::ptr(v->orders), fmt::ptr(this));
	}
	assert_msg(this->num_vehicles == check_num_vehicles, "{}, {}", this->num_vehicles, check_num_vehicles);
	Debug(misc, 6, "... detected {} orders ({} manual), {} vehicles, {} timetabled, {} total",
			this->GetNumOrders(), this->num_manual_orders,
			this->num_vehicles, this->timetable_duration, this->total_duration);
}
#endif

/**
 * Checks whether the order goes to a station or not, i.e. whether the
 * destination is a station
 * @param v the vehicle to check for
 * @param o the order to check
 * @return true if the destination is a station
 */
static inline bool OrderGoesToStation(const Vehicle *v, const Order *o)
{
	return o->IsType(OT_GOTO_STATION) ||
			(v->type == VEH_AIRCRAFT && o->IsType(OT_GOTO_DEPOT) && !(o->GetDepotActionType() & ODATFB_NEAREST_DEPOT) && o->GetDestination() != StationID::Invalid());
}

/**
 * Checks whether the order goes to a road depot
 * @param v the vehicle to check for
 * @param o the order to check
 * @return true if the destination is a road depot
 */
static inline bool OrderGoesToRoadDepot(const Vehicle *v, const Order *o)
{
	return (v->type == VEH_ROAD) && o->IsType(OT_GOTO_DEPOT) && !(o->GetDepotActionType() & ODATFB_NEAREST_DEPOT);
}

/**
 * Delete all news items regarding defective orders about a vehicle
 * This could kill still valid warnings (for example about void order when just
 * another order gets added), but assume the company will notice the problems,
 * when they're changing the orders.
 */
static void DeleteOrderWarnings(const Vehicle *v)
{
	DeleteVehicleNews(v->index, AdviceType::Order);
}

/**
 * Returns a tile somewhat representing the order destination (not suitable for pathfinding).
 * @param v The vehicle to get the location for.
 * @param airport Get the airport tile and not the station location for aircraft.
 * @return destination of order, or INVALID_TILE if none.
 */
TileIndex Order::GetLocation(const Vehicle *v, bool airport) const
{
	switch (this->GetType()) {
		case OT_GOTO_WAYPOINT:
		case OT_GOTO_STATION:
		case OT_IMPLICIT:
			if (airport && v->type == VEH_AIRCRAFT) return Station::Get(this->GetDestination().ToStationID())->airport.tile;
			return BaseStation::Get(this->GetDestination().ToStationID())->xy;

		case OT_GOTO_DEPOT:
			if (this->GetDepotActionType() & ODATFB_NEAREST_DEPOT) return INVALID_TILE;
			if (this->GetDestination() == DepotID::Invalid()) return INVALID_TILE;
			return (v->type == VEH_AIRCRAFT) ? Station::Get(this->GetDestination().ToStationID())->xy : Depot::Get(this->GetDestination().ToDepotID())->xy;

		default:
			return INVALID_TILE;
	}
}

/**
 * Returns a tile somewhat representing the order's auxiliary location (not related to vehicle movement).
 * @param secondary Whether to return a second auxiliary location, if available.
 * @return auxiliary location of order, or INVALID_TILE if none.
 */
TileIndex Order::GetAuxiliaryLocation(bool secondary) const
{
	if (this->IsType(OT_CONDITIONAL)) {
		if (secondary && ConditionVariableTestsCargoWaitingAmount(this->GetConditionVariable()) && this->HasConditionViaStation()) {
			const Station *st = Station::GetIfValid(this->GetConditionViaStationID());
			if (st != nullptr) return st->xy;
		}
		if (ConditionVariableHasStationID(this->GetConditionVariable())) {
			const Station *st = Station::GetIfValid(this->GetConditionStationID());
			if (st != nullptr) return st->xy;
		}
	}
	if (this->IsType(OT_LABEL) && IsDestinationOrderLabelSubType(this->GetLabelSubType())) {
		const BaseStation *st = BaseStation::GetIfValid(this->GetDestination().ToStationID());
		if (st != nullptr) return st->xy;
	}
	return INVALID_TILE;
}

/**
 * Get the distance between two orders of a vehicle. Conditional orders are resolved
 * and the bigger distance of the two order branches is returned.
 * @param prev Origin order.
 * @param cur Destination order.
 * @param v The vehicle to get the distance for.
 * @param conditional_depth Internal param for resolving conditional orders.
 * @return Maximum distance between the two orders.
 */
uint GetOrderDistance(const Order *prev, const Order *cur, const Vehicle *v, int conditional_depth)
{
	if (cur->IsType(OT_CONDITIONAL)) {
		if (conditional_depth > std::min<int>(64, v->GetNumOrders())) return 0;

		conditional_depth++;

		int dist1 = GetOrderDistance(prev, v->GetOrder(cur->GetConditionSkipToOrder()), v, conditional_depth);
		int dist2 = GetOrderDistance(prev, v->orders->GetNext(cur), v, conditional_depth);
		return std::max(dist1, dist2);
	}

	TileIndex prev_tile = prev->GetLocation(v, true);
	TileIndex cur_tile = cur->GetLocation(v, true);
	if (prev_tile == INVALID_TILE || cur_tile == INVALID_TILE) return 0;
	return v->type == VEH_AIRCRAFT ? DistanceSquare(prev_tile, cur_tile) : DistanceManhattan(prev_tile, cur_tile);
}

/**
 * Add an order to the orderlist of a vehicle.
 * @return the cost of this operation or an error
 */
CommandCost CmdInsertOrder(DoCommandFlags flags, const InsertOrderCmdData &data)
{
	Order new_order{};
	MemberPtrsTie(new_order, Order::GetCmdRefFields()) = data.new_order;

	return CmdInsertOrderIntl(flags, Vehicle::GetIfValid(data.veh), data.sel_ord, new_order, {});
}

/**
 * Duplicate an order in the orderlist of a vehicle.
 * @param flags operation to perform
 * @param veh_id ID of the vehicle
 * @param sel_ord The order to duplicate
 * @return the cost of this operation or an error
 */
CommandCost CmdDuplicateOrder(DoCommandFlags flags, VehicleID veh_id, VehicleOrderID sel_ord)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (sel_ord >= v->GetNumOrders()) return CMD_ERROR;

	const Order *src_order = v->GetOrder(sel_ord);
	if (src_order == nullptr) return CMD_ERROR;

	Order new_order(*src_order);
	new_order.SetTravelTimetabled(false);
	new_order.SetTravelTime(0);
	new_order.SetTravelFixed(false);
	new_order.SetDispatchScheduleIndex(-1);
	CommandCost cost = CmdInsertOrderIntl(flags, v, sel_ord + 1, new_order, CmdInsertOrderIntlFlag::AllowLoadByCargoType);
	if (cost.Failed()) return cost;
	return CommandCost();
}

/**
 * Edit the colour of the vehicle's route overlay.
 * @param flags type of operation
 * @param veh_id ID of the vehicle
 * @param colour colour
 * @return the cost of this operation or an error
 */
CommandCost CmdSetRouteOverlayColour(DoCommandFlags flags, VehicleID veh_id, Colours colour)
{
	if (colour >= COLOUR_END) return CMD_ERROR;

	Vehicle *v = Vehicle::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle() || v->orders == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->SetRouteOverlayColour(colour);
	}
	return CommandCost();
}

static CommandCost PreInsertOrderCheck(Vehicle *v, const Order &new_order, CmdInsertOrderIntlFlags insert_flags)
{
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Check if the inserted order is to the correct destination (owner, type),
	 * and has the correct flags if any */
	switch (new_order.GetType()) {
		case OT_GOTO_STATION: {
			const Station *st = Station::GetIfValid(new_order.GetDestination().ToStationID());
			if (st == nullptr) return CMD_ERROR;

			if (st->owner != OWNER_NONE) {
				CommandCost ret = CheckInfraUsageAllowed(v->type, st->owner);
				if (ret.Failed()) return ret;
			}

			if (!CanVehicleUseStation(v, st)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, GetVehicleCannotUseStationReason(v, st));
			for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
				if (!CanVehicleUseStation(u, st)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER_SHARED, GetVehicleCannotUseStationReason(u, st));
			}

			/* Non stop only allowed for ground vehicles. */
			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && !v->IsGroundVehicle()) return CMD_ERROR;
			if (_settings_game.order.nonstop_only && !(new_order.GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) && v->IsGroundVehicle()) return CMD_ERROR;

			/* Filter invalid load/unload types. */
			switch (new_order.GetLoadType()) {
				case OLFB_CARGO_TYPE_LOAD:
					if (insert_flags.Test(CmdInsertOrderIntlFlag::AllowLoadByCargoType)) break;
					return CMD_ERROR;

				case OLF_LOAD_IF_POSSIBLE:
				case OLFB_NO_LOAD:
					break;

				case OLFB_FULL_LOAD:
				case OLF_FULL_LOAD_ANY:
					if (!insert_flags.Test(CmdInsertOrderIntlFlag::NoUnbunchChecks) && v->HasUnbunchingOrder()) return CommandCost(STR_ERROR_UNBUNCHING_NO_FULL_LOAD);
					break;

				default:
					return CMD_ERROR;
			}
			switch (new_order.GetUnloadType()) {
				case OUF_UNLOAD_IF_POSSIBLE: case OUFB_UNLOAD: case OUFB_TRANSFER: case OUFB_NO_UNLOAD: break;
				case OUFB_CARGO_TYPE_UNLOAD:
					if (insert_flags.Test(CmdInsertOrderIntlFlag::AllowLoadByCargoType)) break;
					return CMD_ERROR;
				default: return CMD_ERROR;
			}

			/* Filter invalid stop locations */
			switch (new_order.GetStopLocation()) {
				case OSL_PLATFORM_NEAR_END:
				case OSL_PLATFORM_MIDDLE:
				case OSL_PLATFORM_THROUGH:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					[[fallthrough]];

				case OSL_PLATFORM_FAR_END:
					break;

				default:
					return CMD_ERROR;
			}

			break;
		}

		case OT_GOTO_DEPOT: {
			if ((new_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) == 0) {
				if (v->type == VEH_AIRCRAFT) {
					const Station *st = Station::GetIfValid(new_order.GetDestination().ToStationID());

					if (st == nullptr) return CMD_ERROR;

					CommandCost ret = CheckInfraUsageAllowed(v->type, st->owner);
					if (ret.Failed()) return ret;

					if (!CanVehicleUseStation(v, st) || !st->airport.HasHangar()) {
						return CMD_ERROR;
					}
				} else {
					const Depot *dp = Depot::GetIfValid(new_order.GetDestination().ToDepotID());

					if (dp == nullptr) return CMD_ERROR;

					CommandCost ret = CheckInfraUsageAllowed(v->type, GetTileOwner(dp->xy), dp->xy);
					if (ret.Failed()) return ret;

					switch (v->type) {
						case VEH_TRAIN:
							if (!IsRailDepotTile(dp->xy)) return CMD_ERROR;
							break;

						case VEH_ROAD:
							if (!IsRoadDepotTile(dp->xy)) return CMD_ERROR;
							if ((GetPresentRoadTypes(dp->xy) & RoadVehicle::From(v)->compatible_roadtypes).None()) return CMD_ERROR;
							break;

						case VEH_SHIP:
							if (!IsShipDepotTile(dp->xy)) return CMD_ERROR;
							break;

						default: return CMD_ERROR;
					}
				}
			}

			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && !v->IsGroundVehicle()) return CMD_ERROR;
			if (_settings_game.order.nonstop_only && !(new_order.GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) && v->IsGroundVehicle()) return CMD_ERROR;
			if (new_order.GetDepotOrderType() & ~(ODTFB_PART_OF_ORDERS | ((new_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) != 0 ? ODTFB_SERVICE : 0))) return CMD_ERROR;
			if (new_order.GetDepotActionType() & ~(ODATFB_HALT | ODATFB_SELL | ODATFB_NEAREST_DEPOT | ODATFB_UNBUNCH)) return CMD_ERROR;

			/* Vehicles cannot have a "service if needed" order that also has a depot action. */
			if ((new_order.GetDepotOrderType() & ODTFB_SERVICE) && (new_order.GetDepotActionType() & (ODATFB_HALT | ODATFB_UNBUNCH))) return CMD_ERROR;

			/* Check if we're allowed to have a new unbunching order. */
			if ((new_order.GetDepotActionType() & ODATFB_UNBUNCH)) {
				if (v->HasFullLoadOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_UNBUNCHING_NO_UNBUNCHING_FULL_LOAD);
				if (!insert_flags.Test(CmdInsertOrderIntlFlag::AllowDuplicateUnbunch) && !insert_flags.Test(CmdInsertOrderIntlFlag::NoUnbunchChecks) && v->HasUnbunchingOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_UNBUNCHING_ONLY_ONE_ALLOWED);
				if (v->HasConditionalOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_UNBUNCHING_NO_UNBUNCHING_CONDITIONAL);
			}
			break;
		}

		case OT_GOTO_WAYPOINT: {
			const Waypoint *wp = Waypoint::GetIfValid(new_order.GetDestination().ToStationID());
			if (wp == nullptr) return CMD_ERROR;

			switch (v->type) {
				default: return CMD_ERROR;

				case VEH_TRAIN: {
					if (!wp->facilities.Test(StationFacility::Train)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_RAIL_WAYPOINT);

					CommandCost ret = CheckInfraUsageAllowed(v->type, wp->owner);
					if (ret.Failed()) return ret;
					break;
				}

				case VEH_ROAD: {
					if (!wp->facilities.Test(StationFacility::BusStop) && !wp->facilities.Test(StationFacility::TruckStop)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_ROAD_WAYPOINT);

					CommandCost ret = CheckInfraUsageAllowed(v->type, wp->owner);
					if (ret.Failed()) return ret;
					break;
				}

				case VEH_SHIP:
					if (!wp->facilities.Test(StationFacility::Dock)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_BUOY);
					if (wp->owner != OWNER_NONE) {
						CommandCost ret = CheckInfraUsageAllowed(v->type, wp->owner);
						if (ret.Failed()) return ret;
					}
					break;
			}

			/* Order flags can be any of the following for waypoints:
			 * [non-stop]
			 * non-stop orders (if any) are only valid for trains/RVs */
			if (new_order.GetNonStopType() != ONSF_STOP_EVERYWHERE && !v->IsGroundVehicle()) return CMD_ERROR;
			if (_settings_game.order.nonstop_only && !(new_order.GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) && v->IsGroundVehicle()) return CMD_ERROR;
			break;
		}

		case OT_CONDITIONAL: {
			VehicleOrderID skip_to = new_order.GetConditionSkipToOrder();
			if (skip_to != 0 && skip_to >= v->GetNumOrders() && !insert_flags.Test(CmdInsertOrderIntlFlag::NoConditionTargetCheck)) return CMD_ERROR; // Always allow jumping to the first (even when there is no order).
			if (new_order.GetConditionVariable() >= OCV_END) return CMD_ERROR;
			if (!insert_flags.Test(CmdInsertOrderIntlFlag::NoUnbunchChecks) && v->HasUnbunchingOrder()) return CommandCost(STR_ERROR_UNBUNCHING_NO_CONDITIONAL);

			OrderConditionComparator occ = new_order.GetConditionComparator();
			if (occ >= OCC_END) return CMD_ERROR;
			switch (new_order.GetConditionVariable()) {
				case OCV_SLOT_OCCUPANCY:
				case OCV_VEH_IN_SLOT: {
					TraceRestrictSlotID slot{new_order.GetXDataLow()};
					if (slot != INVALID_TRACE_RESTRICT_SLOT_ID) {
						const TraceRestrictSlot *trslot = TraceRestrictSlot::GetIfValid(slot);
						if (trslot == nullptr) return CMD_ERROR;
						if (new_order.GetConditionVariable() == OCV_VEH_IN_SLOT && trslot->vehicle_type != v->type) return CMD_ERROR;
						if (!trslot->IsUsableByOwner(v->owner)) return CMD_ERROR;
					}
					switch (occ) {
						case OCC_IS_TRUE:
						case OCC_IS_FALSE:
						case OCC_EQUALS:
						case OCC_NOT_EQUALS:
							break;

						default:
							return CMD_ERROR;
					}
					break;
				}

				case OCV_VEH_IN_SLOT_GROUP: {
					TraceRestrictSlotGroupID slot_group{new_order.GetXDataLow()};
					if (slot_group != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
						const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(slot_group);
						if (sg == nullptr || sg->vehicle_type != v->type) return CMD_ERROR;
						if (!sg->CompanyCanReferenceSlotGroup(v->owner)) return CMD_ERROR;
					}
					switch (occ) {
						case OCC_IS_TRUE:
						case OCC_IS_FALSE:
							break;

						default:
							return CMD_ERROR;
					}
					break;
				}

				case OCV_CARGO_LOAD_PERCENTAGE:
					if (!CargoSpec::Get(new_order.GetConditionValue())->IsValid()) return CMD_ERROR;
					if (new_order.GetXData() > 100) return CMD_ERROR;
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_CARGO_WAITING_AMOUNT:
				case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
					if (!CargoSpec::Get(new_order.GetConditionValue())->IsValid()) return CMD_ERROR;
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_CARGO_WAITING:
				case OCV_CARGO_ACCEPTANCE:
					if (!CargoSpec::Get(new_order.GetConditionValue())->IsValid()) return CMD_ERROR;
					/* FALL THROUGH */

				case OCV_REQUIRES_SERVICE:
					if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_UNCONDITIONALLY:
					if (occ != OCC_EQUALS) return CMD_ERROR;
					if (new_order.GetConditionValue() != 0) return CMD_ERROR;
					break;

				case OCV_FREE_PLATFORMS:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_DISPATCH_SLOT: {
					if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) return CMD_ERROR;
					uint submode = GB(new_order.GetConditionValue(), ODCB_SRC_START, ODCB_SRC_COUNT);
					if (submode < ODCS_BEGIN || submode >= ODCS_END) return CMD_ERROR;
					break;
				}

				case OCV_PERCENT:
					if (occ != OCC_EQUALS) return CMD_ERROR;
					[[fallthrough]];
				case OCV_LOAD_PERCENTAGE:
				case OCV_RELIABILITY:
					if (new_order.GetConditionValue() > 100) return CMD_ERROR;
					[[fallthrough]];

				default:
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;
			}
			break;
		}

		case OT_SLOT: {
			TraceRestrictSlotID data = new_order.GetDestination().ToSlotID();
			if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(data);
				if (slot == nullptr || slot->vehicle_type != v->type) return CMD_ERROR;
				if (!slot->IsUsableByOwner(v->owner)) return CMD_ERROR;
			}
			switch (new_order.GetSlotSubType()) {
				case OSST_RELEASE:
				case OSST_TRY_ACQUIRE:
					break;

				default:
					return CMD_ERROR;
			}
			break;
		}

		case OT_SLOT_GROUP: {
			TraceRestrictSlotGroupID data = new_order.GetDestination().ToSlotGroupID();
			if (data != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
				const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(data);
				if (sg == nullptr || sg->vehicle_type != v->type) return CMD_ERROR;
				if (!sg->CompanyCanReferenceSlotGroup(v->owner)) return CMD_ERROR;
			}
			switch (new_order.GetSlotGroupSubType()) {
				case OSGST_RELEASE:
					break;

				default:
					return CMD_ERROR;
			}
			break;
		}

		case OT_COUNTER: {
			TraceRestrictCounterID data = new_order.GetDestination().ToCounterID();
			if (data != INVALID_TRACE_RESTRICT_COUNTER_ID) {
				const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(data);
				if (ctr == nullptr) return CMD_ERROR;
				if (!ctr->IsUsableByOwner(v->owner)) return CMD_ERROR;
			}
			break;
		}

		case OT_LABEL: {
			switch (new_order.GetLabelSubType()) {
				case OLST_TEXT:
					break;

				case OLST_DEPARTURES_VIA:
				case OLST_DEPARTURES_REMOVE_VIA: {
					const BaseStation *st = BaseStation::GetIfValid(new_order.GetDestination().ToStationID());
					if (st == nullptr) return CMD_ERROR;

					if (st->owner != OWNER_NONE) {
						CommandCost ret = CheckInfraUsageAllowed(v->type, st->owner);
						if (ret.Failed()) return ret;
					}
					break;
				}

				case OLST_ERROR:
					switch (new_order.GetLabelError()) {
						case OrderLabelError::Default:
						case OrderLabelError::ParseError:
							break;
						default:
							return CMD_ERROR;
					}
					break;

				default:
					return CMD_ERROR;
			}
			break;
		}

		default: return CMD_ERROR;
	}

	return CommandCost();
}

static CommandCost CmdInsertOrderIntl(DoCommandFlags flags, Vehicle *v, VehicleOrderID sel_ord, const Order &new_order, CmdInsertOrderIntlFlags insert_flags)
{
	CommandCost ret = PreInsertOrderCheck(v, new_order, insert_flags);
	if (ret.Failed()) return ret;

	if (sel_ord == INVALID_VEH_ORDER_ID) sel_ord = v->GetNumOrders(); // Append to end of list

	if (sel_ord > v->GetNumOrders()) return CMD_ERROR;

	if (v->GetNumOrders() >= MAX_VEH_ORDER_ID) return CommandCost(STR_ERROR_TOO_MANY_ORDERS);
	if (v->orders == nullptr && !OrderList::CanAllocateItem()) return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);

	if (flags.Test(DoCommandFlag::Execute)) {
		InsertOrder(v, Order(new_order), sel_ord);
	}

	CommandCost cost;
	cost.SetResultData(sel_ord);
	return cost;
}

/**
 * Insert a new order but skip the validation.
 * @param v       The vehicle to insert the order to.
 * @param new_o   The new order.
 * @param sel_ord The position the order should be inserted at.
 */
void InsertOrder(Vehicle *v, Order &&new_o, VehicleOrderID sel_ord)
{
	/* Create new order and link in list */
	if (v->orders == nullptr) {
		v->orders = new OrderList(std::move(new_o), v);
	} else {
		v->orders->InsertOrderAt(std::move(new_o), sel_ord);
	}

	Vehicle *u = v->FirstShared();
	DeleteOrderWarnings(u);
	for (; u != nullptr; u = u->NextShared()) {
		assert(v->orders == u->orders);

		/* If there is added an order before the current one, we need
		 * to update the selected order. We do not change implicit/real order indices though.
		 * If the new order is between the current implicit order and real order, the implicit order will
		 * later skip the inserted order. */
		if (sel_ord <= u->cur_real_order_index) {
			uint cur = u->cur_real_order_index + 1;
			/* Check if we don't go out of bound */
			if (cur < u->GetNumOrders()) {
				u->cur_real_order_index = cur;
			}
		}
		if (sel_ord == u->cur_implicit_order_index && u->IsGroundVehicle()) {
			/* We are inserting an order just before the current implicit order.
			 * We do not know whether we will reach current implicit or the newly inserted order first.
			 * So, disable creation of implicit orders until we are on track again. */
			uint16_t &gv_flags = u->GetGroundVehicleFlags();
			SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
		}
		if (sel_ord <= u->cur_implicit_order_index) {
			uint cur = u->cur_implicit_order_index + 1;
			/* Check if we don't go out of bound */
			if (cur < u->GetNumOrders()) {
				u->cur_implicit_order_index = cur;
			}
		}

		if (u->cur_timetable_order_index != INVALID_VEH_ORDER_ID && sel_ord <= u->cur_timetable_order_index) {
			uint cur = u->cur_timetable_order_index + 1;
			/* Check if we don't go out of bound */
			if (cur < u->GetNumOrders()) {
				u->cur_timetable_order_index = cur;
			}
		}

		/* Unbunching data is no longer valid. */
		u->ResetDepotUnbunching();

		/* Update any possible open window of the vehicle */
		InvalidateVehicleOrder(u, INVALID_VEH_ORDER_ID | (sel_ord << 16));
	}

	/* As we insert an order, the order to skip to will be 'wrong'. */
	VehicleOrderID cur_order_id = 0;
	if (sel_ord + 1 == v->GetNumOrders() && sel_ord > 0) {
		/* Avoid scanning whole order list for inserts at the end. */
		cur_order_id = sel_ord - 1;
	}
	for (Order *order : v->Orders(cur_order_id)) {
		if (order->IsType(OT_CONDITIONAL)) {
			VehicleOrderID order_id = order->GetConditionSkipToOrder();
			if (order_id >= sel_ord) {
				order->SetConditionSkipToOrder(order_id + 1);
			}
			if (order_id == cur_order_id) {
				order->SetConditionSkipToOrder((order_id + 1) % v->GetNumOrders());
			}
		}
		cur_order_id++;
	}

	/* Make sure to rebuild the whole list */
	InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type));
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
}

/**
 * Declone an order-list
 * @param *dst delete the orders of this vehicle
 * @param flags execution flags
 */
static CommandCost DecloneOrder(Vehicle *dst, DoCommandFlags flags)
{
	if (flags.Test(DoCommandFlag::Execute)) {
		/* Clear scheduled dispatch flag if any */
		if (dst->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
			dst->vehicle_flags.Reset(VehicleFlag::ScheduledDispatch);
		}

		DeleteVehicleOrders(dst);
		InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);
		InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type));
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
	}
	return CommandCost();
}

/**
 * Get the first cargoID that points to a valid cargo (usually 0)
 */
static CargoType GetFirstValidCargo()
{
	for (CargoType i = 0; i < NUM_CARGO; i++) {
		if (CargoSpec::Get(i)->IsValid()) return i;
	}
	/* No cargos defined -> 'Houston, we have a problem!' */
	NOT_REACHED();
}

/**
 * Delete an order from the orderlist of a vehicle.
 * @param flags operation to perform
 * @param veh_id the ID of the vehicle
 * @param sel_ord the order to delete
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteOrder(DoCommandFlags flags, VehicleID veh_id, VehicleOrderID sel_ord)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* If we did not select an order, we maybe want to de-clone the orders */
	if (sel_ord >= v->GetNumOrders()) return DecloneOrder(v, flags);

	if (v->GetOrder(sel_ord) == nullptr) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		DeleteOrder(v, sel_ord);
	}
	return CommandCost();
}

/**
 * Cancel the current loading order of the vehicle as the order was deleted.
 * @param v the vehicle
 */
static void CancelLoadingDueToDeletedOrder(Vehicle *v)
{
	if (v->current_order.IsType(OT_LOADING_ADVANCE)) {
		v->vehicle_flags.Set(VehicleFlag::LoadingFinished);
		return;
	}

	assert(v->current_order.IsType(OT_LOADING));
	/* NON-stop flag is misused to see if a train is in a station that is
	 * on its order list or not */
	v->current_order.SetNonStopType(ONSF_STOP_EVERYWHERE);
	/* When full loading, "cancel" that order so the vehicle doesn't
	 * stay indefinitely at this station anymore. */
	if (v->current_order.GetLoadType() & OLFB_FULL_LOAD) v->current_order.SetLoadType(OLF_LOAD_IF_POSSIBLE);
}

/**
 * Delete an order but skip the parameter validation.
 * @param v       The vehicle to delete the order from.
 * @param sel_ord The id of the order to be deleted.
 */
void DeleteOrder(Vehicle *v, VehicleOrderID sel_ord)
{
	v->orders->DeleteOrderAt(sel_ord);

	Vehicle *u = v->FirstShared();
	DeleteOrderWarnings(u);
	for (; u != nullptr; u = u->NextShared()) {
		assert(v->orders == u->orders);

		if (sel_ord == u->cur_real_order_index && u->current_order.IsAnyLoadingType()) {
			CancelLoadingDueToDeletedOrder(u);
		}

		if (sel_ord < u->cur_real_order_index) {
			u->cur_real_order_index--;
		} else if (sel_ord == u->cur_real_order_index) {
			u->UpdateRealOrderIndex();
		}

		if (sel_ord < u->cur_implicit_order_index) {
			u->cur_implicit_order_index--;
		} else if (sel_ord == u->cur_implicit_order_index) {
			/* Make sure the index is valid */
			if (u->cur_implicit_order_index >= u->GetNumOrders()) u->cur_implicit_order_index = 0;

			/* Skip non-implicit orders for the implicit-order-index (e.g. if the current implicit order was deleted */
			while (u->cur_implicit_order_index != u->cur_real_order_index && !u->GetOrder(u->cur_implicit_order_index)->IsType(OT_IMPLICIT)) {
				u->cur_implicit_order_index++;
				if (u->cur_implicit_order_index >= u->GetNumOrders()) u->cur_implicit_order_index = 0;
			}
		}
		/* Unbunching data is no longer valid. */
		u->ResetDepotUnbunching();

		if (u->cur_timetable_order_index != INVALID_VEH_ORDER_ID) {
			if (sel_ord < u->cur_timetable_order_index) {
				u->cur_timetable_order_index--;
			} else if (sel_ord == u->cur_timetable_order_index) {
				u->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
			}
		}

		/* Update any possible open window of the vehicle */
		InvalidateVehicleOrder(u, sel_ord | (INVALID_VEH_ORDER_ID << 16));
	}

	/* As we delete an order, the order to skip to will be 'wrong'. */
	VehicleOrderID cur_order_id = 0;
	for (Order *order : v->Orders()) {
		if (order->IsType(OT_CONDITIONAL)) {
			VehicleOrderID order_id = order->GetConditionSkipToOrder();
			if (order_id >= sel_ord) {
				order_id = std::max(order_id - 1, 0);
			}
			if (order_id == cur_order_id) {
				order_id = (order_id + 1) % v->GetNumOrders();
			}
			order->SetConditionSkipToOrder(order_id);
		}
		cur_order_id++;
	}

	InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type));
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
}

/**
 * Goto order of order-list.
 * @param flags operation to perform
 * @param veh_id The ID of the vehicle which order is skipped
 * @param sel_ord the selected order to which we want to skip
 * @return the cost of this operation or an error
 */
CommandCost CmdSkipToOrder(DoCommandFlags flags, VehicleID veh_id, VehicleOrderID sel_ord)
{
	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle() || sel_ord == v->cur_implicit_order_index || sel_ord >= v->GetNumOrders() || v->GetNumOrders() < 2) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		if (v->current_order.IsAnyLoadingType()) v->LeaveStation();
		if (v->current_order.IsType(OT_WAITING)) v->HandleWaiting(true);

		if (v->type == VEH_TRAIN) {
			for (Train *u = Train::From(v); u != nullptr; u = u->Next()) {
				u->flags.Reset(VehicleRailFlag::BeyondPlatformEnd);
			}
		}

		v->cur_implicit_order_index = v->cur_real_order_index = sel_ord;
		v->UpdateRealOrderIndex();
		v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;

		/* Unbunching data is no longer valid. */
		v->ResetDepotUnbunching();

		InvalidateVehicleOrder(v, VIWD_MODIFY_ORDERS);

		v->ClearSeparation();
		if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) v->vehicle_flags.Reset(VehicleFlag::TimetableStarted);

		/* We have an aircraft/ship, they have a mini-schedule, so update them all */
		if (v->type == VEH_AIRCRAFT || v->type == VEH_SHIP) DirtyVehicleListWindowForVehicle(v);
	}

	return CommandCost();
}

/**
 * Move an order inside the orderlist
 * @param flags operation to perform
 * @param veh the ID of the vehicle
 * @param moving_order the order to move
 * @param target_order the target order
 * @param count the number of orders to move
 * @return the cost of this operation or an error
 * @note The target order will move one place down in the orderlist
 *  if you move the order upwards else it'll move it one place down
 */
CommandCost CmdMoveOrder(DoCommandFlags flags, VehicleID veh, VehicleOrderID moving_order, VehicleOrderID target_order, uint16_t count)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	const VehicleOrderID order_count = v->GetNumOrders();

	/* Don't make senseless movements */
	if (count == 0 || order_count <= 1 || moving_order >= order_count || target_order >= order_count || moving_order == target_order) return CMD_ERROR;

	if (moving_order < target_order) {
		if (target_order < moving_order + count) return CMD_ERROR;
	} else {
		if (moving_order + count > order_count) return CMD_ERROR;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		v->orders->MoveOrders(moving_order, target_order, count);

		/* Update shared list */
		Vehicle *u = v->FirstShared();

		DeleteOrderWarnings(u);

		auto adjust_order_idx = [&](VehicleOrderID idx) -> VehicleOrderID {
			if (idx >= order_count) return idx;
			if (idx >= moving_order && idx < moving_order + count) return target_order + (idx - moving_order);
			if (idx > moving_order && idx <= target_order) return idx - count;
			if (idx < moving_order && idx >= target_order) return idx + count;
			return idx;
		};

		for (; u != nullptr; u = u->NextShared()) {
			/* Update the current order.
			 * There are multiple ways to move orders, which result in cur_implicit_order_index
			 * and cur_real_order_index to not longer make any sense. E.g. moving another
			 * real order between them.
			 *
			 * Basically one could choose to preserve either of them, but not both.
			 * While both ways are suitable in this or that case from a human point of view, neither
			 * of them makes really sense.
			 * However, from an AI point of view, preserving cur_real_order_index is the most
			 * predictable and transparent behaviour.
			 *
			 * With that decision it basically does not matter what we do to cur_implicit_order_index.
			 * If we change orders between the implicit- and real-index, the implicit orders are mostly likely
			 * completely out-dated anyway. So, keep it simple and just keep cur_implicit_order_index as well.
			 * The worst which can happen is that a lot of implicit orders are removed when reaching current_order.
			 */
			u->cur_real_order_index = adjust_order_idx(u->cur_real_order_index);
			u->cur_implicit_order_index = adjust_order_idx(u->cur_implicit_order_index);

			/* Unbunching data is no longer valid. */
			u->ResetDepotUnbunching();


			u->cur_timetable_order_index = INVALID_VEH_ORDER_ID;

			assert(v->orders == u->orders);
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrderOnMove(u, moving_order, target_order, count);
		}

		/* As we move an order, the order to skip to will be 'wrong'. */
		for (Order *order : v->Orders()) {
			if (order->IsType(OT_CONDITIONAL)) {
				order->SetConditionSkipToOrder(adjust_order_idx(order->GetConditionSkipToOrder()));
			}
		}

		/* Make sure to rebuild the whole list */
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type));
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
	}

	return CommandCost();
}

static void AdjustTravelAfterOrderReverse(std::span<Order> orders)
{
	auto is_usable = [](const Order &o) -> bool {
		if (o.HasNoTimetableTimes()) return false;
		if (o.IsType(OT_CONDITIONAL)) return false;
		if (o.IsType(OT_GOTO_DEPOT) && (o.GetDepotOrderType() & ODTFB_SERVICE)) return false;
		return true;
	};

	struct TravelInfo {
		TimetableTicks travel_time;
		bool travel_timetabled;
		bool travel_fixed;
		uint16_t max_speed;

		void Read(const Order &o)
		{
			this->travel_time = o.GetTravelTime();
			this->travel_timetabled = o.IsTravelTimetabled();
			this->travel_fixed = o.IsTravelFixed();
			this->max_speed = o.GetMaxSpeed();
		}

		void Write(Order &o) const
		{
			o.SetTravelTime(this->travel_time);
			o.SetTravelTimetabled(this->travel_timetabled);
			o.SetTravelFixed(this->travel_fixed);
			o.SetMaxSpeed(this->max_speed);
		}
	};

	TravelInfo tail_info{};
	const Order *tail = nullptr;
	Order *head = nullptr;
	/* Iterate from tail to head, shuffle travel info by one place in head to tail direction. */
	for (Order &o : std::views::reverse(orders)) {
		if (!is_usable(o)) continue;
		if (tail == nullptr) {
			tail = &o;
			tail_info.Read(o);
		}
		if (head != nullptr) {
			TravelInfo info{};
			info.Read(o);
			info.Write(*head);
		}
		head = &o;
	}
	if (head != nullptr && head != tail) {
		/* tail_info contains that state of tail before it was overwritten, write it to the end head (first usable order). */
		tail_info.Write(*head);
	}
}

/**
 * Reverse an orderlist
 * @param flags operation to perform
 * @param veh the ID of the vehicle
 * @param op operation to perform
 * @return the cost of this operation or an error
 */
CommandCost CmdReverseOrderList(DoCommandFlags flags, VehicleID veh, ReverseOrderOperation op)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	switch (op) {
		case ReverseOrderOperation::Reverse: {
			VehicleOrderID order_count = v->GetNumOrders();
			if (order_count < 2) return CMD_ERROR;
			if (flags.Test(DoCommandFlag::Execute)) {
				auto map_order_id = [&](VehicleOrderID idx) -> VehicleOrderID {
					if (idx == INVALID_VEH_ORDER_ID) return idx;
					return (order_count - 1) - idx;
				};

				std::vector<Order> &orders = v->orders->GetOrderVector();
				std::reverse(orders.begin(), orders.end());
				AdjustTravelAfterOrderReverse(orders);

				/* As we move an order, the order to skip to will be 'wrong'. */
				for (Order &order : orders) {
					if (order.IsType(OT_CONDITIONAL)) {
						order.SetConditionSkipToOrder(map_order_id(order.GetConditionSkipToOrder()));
					}
				}

				/* Update shared list */
				Vehicle *u = v->FirstShared();
				DeleteOrderWarnings(u);
				for (; u != nullptr; u = u->NextShared()) {
					u->cur_real_order_index = map_order_id(u->cur_real_order_index);
					u->cur_implicit_order_index = map_order_id(u->cur_implicit_order_index);
					u->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
					u->ResetDepotUnbunching();
					InvalidateVehicleOrder(u, VIWD_REMOVE_ALL_ORDERS); // All orders have moved/been modified, deselect
				}
			}
			break;
		}

		case ReverseOrderOperation::AppendReversed: {
			const uint order_count = v->GetNumOrders();
			if (order_count < 3) return CMD_ERROR;
			const uint max_order = order_count - 1;
			if (((order_count * 2) - 2) > MAX_VEH_ORDER_ID) return CommandCost(STR_ERROR_TOO_MANY_ORDERS);
			for (uint i = 0; i < order_count; i++) {
				const Order *o = v->GetOrder(i);
				if (o->IsType(OT_CONDITIONAL)) return CMD_ERROR;
				if (i >= 1 && i < max_order && o->IsType(OT_GOTO_DEPOT) && o->GetDepotActionType() & ODATFB_UNBUNCH) {
					return CommandCost(STR_ERROR_UNBUNCHING_ONLY_ONE_ALLOWED);
				}
			}
			for (uint i = max_order - 1; i >= 1; i--) {
				Order new_order(*v->GetOrder(i));
				CommandCost ret = PreInsertOrderCheck(v, new_order, {CmdInsertOrderIntlFlag::AllowLoadByCargoType, CmdInsertOrderIntlFlag::NoUnbunchChecks});
				if (ret.Failed()) return ret;

				if (flags.Test(DoCommandFlag::Execute)) v->orders->InsertOrderAt(std::move(new_order), v->GetNumOrders());
			}
			if (flags.Test(DoCommandFlag::Execute)) {
				std::vector<Order> &orders = v->orders->GetOrderVector();
				AdjustTravelAfterOrderReverse(std::span<Order>(orders).subspan(order_count));

				Vehicle *u = v->FirstShared();
				DeleteOrderWarnings(u);
				for (; u != nullptr; u = u->NextShared()) {
					u->ResetDepotUnbunching();
					InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);
				}
			}
			break;
		}

		default:
			return CMD_ERROR;
	}

	return CommandCost();
}

/**
 * Modify an order in the orderlist of a vehicle.
 * @param flags operation to perform
 * @param veh ID of the vehicle
 * @param sel_ord the selected order (if any). If the last order is given,
 *                the order will be inserted before that one
 *                the maximum vehicle order id is 254.
 * @param mof what data to modify (@see ModifyOrderFlags)
 * @param data the data to modify
 * @param cargo for MOF_CARGO_TYPE_UNLOAD, MOF_CARGO_TYPE_LOAD
 * @param text for MOF_LABEL_TEXT
 * @return the cost of this operation or an error
 */
CommandCost CmdModifyOrder(DoCommandFlags flags, VehicleID veh, VehicleOrderID sel_ord, ModifyOrderFlags mof, uint16_t data, CargoType cargo_id, const std::string &text)
{
	if (mof >= MOF_END) return CMD_ERROR;

	if (mof != MOF_LABEL_TEXT && !text.empty()) return CMD_ERROR;

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Is it a valid order? */
	if (sel_ord >= v->GetNumOrders()) return CMD_ERROR;

	Order *order = v->GetOrder(sel_ord);
	assert(order != nullptr);
	if (mof == MOF_COLOUR) {
		if (order->GetType() == OT_IMPLICIT) return CMD_ERROR;
	} else {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
				if (mof != MOF_NON_STOP && mof != MOF_STOP_LOCATION && mof != MOF_UNLOAD && mof != MOF_LOAD && mof != MOF_CARGO_TYPE_UNLOAD && mof != MOF_CARGO_TYPE_LOAD && mof != MOF_RV_TRAVEL_DIR) return CMD_ERROR;
				break;

			case OT_GOTO_DEPOT:
				if (mof != MOF_NON_STOP && mof != MOF_DEPOT_ACTION) return CMD_ERROR;
				break;

			case OT_GOTO_WAYPOINT:
				if (mof != MOF_NON_STOP && mof != MOF_WAYPOINT_FLAGS && mof != MOF_RV_TRAVEL_DIR) return CMD_ERROR;
				break;

			case OT_CONDITIONAL:
				if (mof != MOF_COND_VARIABLE && mof != MOF_COND_COMPARATOR && mof != MOF_COND_VALUE && mof != MOF_COND_VALUE_2 && mof != MOF_COND_VALUE_3 && mof != MOF_COND_VALUE_4 && mof != MOF_COND_DESTINATION && mof != MOF_COND_STATION_ID) return CMD_ERROR;
				break;

			case OT_SLOT:
				if (mof != MOF_SLOT) return CMD_ERROR;
				break;

			case OT_SLOT_GROUP:
				if (mof != MOF_SLOT_GROUP) return CMD_ERROR;
				break;

			case OT_COUNTER:
				if (mof != MOF_COUNTER_ID && mof != MOF_COUNTER_OP && mof != MOF_COUNTER_VALUE) return CMD_ERROR;
				break;

			case OT_LABEL:
				if (order->GetLabelSubType() == OLST_TEXT) {
					if (mof != MOF_LABEL_TEXT) return CMD_ERROR;
				} else if (IsDeparturesOrderLabelSubType(order->GetLabelSubType())) {
					if (mof != MOF_DEPARTURES_SUBTYPE) return CMD_ERROR;
				} else {
					return CMD_ERROR;
				}
				break;

			default:
				return CMD_ERROR;
		}
	}

	switch (mof) {
		default: NOT_REACHED();

		case MOF_NON_STOP:
			if (!v->IsGroundVehicle()) return CMD_ERROR;
			if (data >= ONSF_END) return CMD_ERROR;
			if ((data & ONSF_NO_STOP_AT_DESTINATION_STATION) && order->IsType(OT_GOTO_DEPOT)) return CMD_ERROR;
			if (data == order->GetNonStopType()) return CommandCost();
			if (_settings_game.order.nonstop_only && !(data & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) && v->IsGroundVehicle()) return CMD_ERROR;
			break;

		case MOF_STOP_LOCATION:
			if (v->type != VEH_TRAIN) return CMD_ERROR;
			if (data >= OSL_END) return CMD_ERROR;
			break;

		case MOF_CARGO_TYPE_UNLOAD:
			if (cargo_id >= NUM_CARGO && cargo_id != INVALID_CARGO) return CMD_ERROR;
			if (data == OUFB_CARGO_TYPE_UNLOAD) return CMD_ERROR;
			/* FALL THROUGH */
		case MOF_UNLOAD:
			if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) return CMD_ERROR;
			if ((data & ~(OUFB_UNLOAD | OUFB_TRANSFER | OUFB_NO_UNLOAD | OUFB_CARGO_TYPE_UNLOAD)) != 0) return CMD_ERROR;
			/* Unload and no-unload are mutual exclusive and so are transfer and no unload. */
			if (data != 0 && (data & OUFB_CARGO_TYPE_UNLOAD) == 0 && ((data & (OUFB_UNLOAD | OUFB_TRANSFER)) != 0) == ((data & OUFB_NO_UNLOAD) != 0)) return CMD_ERROR;
			/* Cargo-type-unload exclude all the other flags. */
			if ((data & OUFB_CARGO_TYPE_UNLOAD) != 0 && data != OUFB_CARGO_TYPE_UNLOAD) return CMD_ERROR;
			if (data == order->GetUnloadType()) return CMD_ERROR;
			break;

		case MOF_CARGO_TYPE_LOAD:
			if (cargo_id >= NUM_CARGO && cargo_id != INVALID_CARGO) return CMD_ERROR;
			if (data == OLFB_CARGO_TYPE_LOAD || data == OLF_FULL_LOAD_ANY) return CMD_ERROR;
			/* FALL THROUGH */
		case MOF_LOAD:
			if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) return CMD_ERROR;
			if ((data > OLFB_NO_LOAD && data != OLFB_CARGO_TYPE_LOAD) || data == 1) return CMD_ERROR;
			if (data == order->GetLoadType()) return CommandCost();
			if ((data & (OLFB_FULL_LOAD | OLF_FULL_LOAD_ANY)) && v->HasUnbunchingOrder()) return CommandCost(STR_ERROR_UNBUNCHING_NO_FULL_LOAD);
			break;

		case MOF_DEPOT_ACTION:
			if (data >= DA_END) return CMD_ERROR;

			/* Check if we are allowed to add unbunching. We are always allowed to remove it. */
			if (data == DA_UNBUNCH) {
				/* Only one unbunching order is allowed in a vehicle's orders. If this order already has an unbunching action, no error is needed. */
				if (v->HasUnbunchingOrder() && !(order->GetDepotActionType() & ODATFB_UNBUNCH)) return CommandCost(STR_ERROR_UNBUNCHING_ONLY_ONE_ALLOWED);

				if (v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) return CommandCost(STR_ERROR_UNBUNCHING_NO_UNBUNCHING_SCHED_DISPATCH);
				if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) return CommandCost(STR_ERROR_UNBUNCHING_NO_UNBUNCHING_AUTO_SEPARATION);

				/* We don't allow unbunching if the vehicle has a conditional order. */
				if (v->HasConditionalOrder()) return CommandCost(STR_ERROR_UNBUNCHING_NO_UNBUNCHING_CONDITIONAL);
				/* We don't allow unbunching if the vehicle has a full load order. */
				if (v->HasFullLoadOrder()) return CommandCost(STR_ERROR_UNBUNCHING_NO_UNBUNCHING_FULL_LOAD);
			}
			break;

		case MOF_COND_VARIABLE:
			if (data == OCV_FREE_PLATFORMS && v->type != VEH_TRAIN) return CMD_ERROR;
			if (data >= OCV_END) return CMD_ERROR;
			break;

		case MOF_COND_COMPARATOR:
			if (data >= OCC_END) return CMD_ERROR;
			switch (order->GetConditionVariable()) {
				case OCV_UNCONDITIONALLY:
				case OCV_PERCENT:
					return CMD_ERROR;

				case OCV_REQUIRES_SERVICE:
				case OCV_CARGO_ACCEPTANCE:
				case OCV_CARGO_WAITING:
				case OCV_DISPATCH_SLOT:
					if (data != OCC_IS_TRUE && data != OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_SLOT_OCCUPANCY:
					if (data != OCC_IS_TRUE && data != OCC_IS_FALSE && data != OCC_EQUALS && data != OCC_NOT_EQUALS) return CMD_ERROR;
					break;

				case OCV_VEH_IN_SLOT: {
					if (data != OCC_IS_TRUE && data != OCC_IS_FALSE && data != OCC_EQUALS && data != OCC_NOT_EQUALS) return CMD_ERROR;
					const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(order->GetXData());
					if (slot != nullptr && slot->vehicle_type != v->type) return CMD_ERROR;
					break;
				}

				case OCV_VEH_IN_SLOT_GROUP: {
					if (data != OCC_IS_TRUE && data != OCC_IS_FALSE) return CMD_ERROR;
					const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(order->GetXData());
					if (sg != nullptr && sg->vehicle_type != v->type) return CMD_ERROR;
					break;
				}

				case OCV_TIMETABLE:
					if (data == OCC_IS_TRUE || data == OCC_IS_FALSE || data == OCC_EQUALS || data == OCC_NOT_EQUALS) return CMD_ERROR;
					break;

				default:
					if (data == OCC_IS_TRUE || data == OCC_IS_FALSE) return CMD_ERROR;
					break;
			}
			break;

		case MOF_COND_VALUE:
			switch (order->GetConditionVariable()) {
				case OCV_UNCONDITIONALLY:
				case OCV_REQUIRES_SERVICE:
					return CMD_ERROR;

				case OCV_LOAD_PERCENTAGE:
				case OCV_RELIABILITY:
				case OCV_PERCENT:
				case OCV_CARGO_LOAD_PERCENTAGE:
					if (data > 100) return CMD_ERROR;
					break;

				case OCV_SLOT_OCCUPANCY: {
					if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
						const TraceRestrictSlot *trslot = TraceRestrictSlot::GetIfValid(data);
						if (trslot == nullptr) return CMD_ERROR;
						if (!trslot->IsUsableByOwner(v->owner)) return CMD_ERROR;
					}
					break;
				}

				case OCV_VEH_IN_SLOT:
					if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
						const TraceRestrictSlot *trslot = TraceRestrictSlot::GetIfValid(data);
						if (trslot == nullptr) return CMD_ERROR;
						if (trslot->vehicle_type != v->type) return CMD_ERROR;
						if (!trslot->IsUsableByOwner(v->owner)) return CMD_ERROR;
					}
					break;

				case OCV_VEH_IN_SLOT_GROUP:
					if (data != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
						const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(data);
						if (sg == nullptr || sg->vehicle_type != v->type) return CMD_ERROR;
						if (!sg->CompanyCanReferenceSlotGroup(v->owner)) return CMD_ERROR;
					}
					break;

				case OCV_CARGO_ACCEPTANCE:
				case OCV_CARGO_WAITING:
					if (!(data < NUM_CARGO && CargoSpec::Get(data)->IsValid())) return CMD_ERROR;
					break;

				case OCV_CARGO_WAITING_AMOUNT:
				case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
				case OCV_COUNTER_VALUE:
				case OCV_TIME_DATE:
				case OCV_TIMETABLE:
					break;

				case OCV_DISPATCH_SLOT: {
					uint submode = GB(data, ODCB_SRC_START, ODCB_SRC_COUNT);
					if (submode < ODCS_BEGIN || submode >= ODCS_END) return CMD_ERROR;
					break;
				}

				default:
					if (data > 2047) return CMD_ERROR;
					break;
			}
			break;

		case MOF_COND_VALUE_2:
			switch (order->GetConditionVariable()) {
				case OCV_CARGO_LOAD_PERCENTAGE:
				case OCV_CARGO_WAITING_AMOUNT:
				case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
					if (!(data < NUM_CARGO && CargoSpec::Get(data)->IsValid())) return CMD_ERROR;
					break;

				case OCV_COUNTER_VALUE:
					if (data != INVALID_TRACE_RESTRICT_COUNTER_ID) {
						const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(data);
						if (ctr == nullptr) return CMD_ERROR;
						if (!ctr->IsUsableByOwner(v->owner)) return CMD_ERROR;
					}
					break;

				case OCV_TIME_DATE:
					if (data >= TRTDVF_END) return CMD_ERROR;
					break;

				case OCV_TIMETABLE:
					if (data >= OTCM_END) return CMD_ERROR;
					break;

				case OCV_DISPATCH_SLOT:
					if (data != UINT16_MAX && data >= v->orders->GetScheduledDispatchScheduleCount()) {
						return CMD_ERROR;
					}
					break;

				default:
					return CMD_ERROR;
			}
			break;

		case MOF_COND_VALUE_3:
			switch (order->GetConditionVariable()) {
				case OCV_CARGO_WAITING_AMOUNT:
				case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
					if (!(data == ORDER_NO_VIA_STATION || Station::GetIfValid(data) != nullptr)) return CMD_ERROR;
					if (order->GetConditionStationID() == data) return CMD_ERROR;
					break;

				case OCV_DISPATCH_SLOT: {
					if (GB(order->GetConditionValue(), ODCB_MODE_START, ODCB_MODE_COUNT) != OCDM_ROUTE_ID) return CMD_ERROR;
					if (data >= 0x100) return CMD_ERROR;
					break;
				}

				default:
					return CMD_ERROR;
			}
			break;

		case MOF_COND_VALUE_4:
			switch (order->GetConditionVariable()) {
				case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
					if (data > 1) return CMD_ERROR;
					break;

				default:
					return CMD_ERROR;
			}
			break;

		case MOF_COND_STATION_ID:
			if (ConditionVariableHasStationID(order->GetConditionVariable())) {
				if (Station::GetIfValid(data) == nullptr) return CMD_ERROR;
			} else {
				return CMD_ERROR;
			}
			break;

		case MOF_COND_DESTINATION:
			if (data >= v->GetNumOrders() || data == sel_ord) return CMD_ERROR;
			break;

		case MOF_WAYPOINT_FLAGS:
			if (data != (data & OrderWaypointFlags(OrderWaypointFlag::Reverse).base())) return CMD_ERROR;
			break;

		case MOF_SLOT:
			if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(data);
				if (slot == nullptr || slot->vehicle_type != v->type) return CMD_ERROR;
				if (!slot->IsUsableByOwner(v->owner)) return CMD_ERROR;
			}
			break;

		case MOF_SLOT_GROUP:
			if (data != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
				const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(data);
				if (sg == nullptr || sg->vehicle_type != v->type) return CMD_ERROR;
				if (!sg->CompanyCanReferenceSlotGroup(v->owner)) return CMD_ERROR;
			}
			break;

		case MOF_RV_TRAVEL_DIR:
			if (v->type != VEH_ROAD) return CMD_ERROR;
			if (data >= DIAGDIR_END && data != INVALID_DIAGDIR) return CMD_ERROR;
			break;

		case MOF_COUNTER_ID:
			if (data != INVALID_TRACE_RESTRICT_COUNTER_ID) {
				const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(data);
				if (ctr == nullptr) return CMD_ERROR;
				if (!ctr->IsUsableByOwner(v->owner)) return CMD_ERROR;
			}
			break;

		case MOF_COUNTER_OP:
			if (data != TRCCOF_INCREASE && data != TRCCOF_DECREASE && data != TRCCOF_SET) {
				return CMD_ERROR;
			}
			break;

		case MOF_COUNTER_VALUE:
			break;

		case MOF_COLOUR:
			if (data >= COLOUR_END && data != INVALID_COLOUR) {
				return CMD_ERROR;
			}
			break;

		case MOF_LABEL_TEXT:
			break;

		case MOF_DEPARTURES_SUBTYPE:
			if (!IsDeparturesOrderLabelSubType(static_cast<OrderLabelSubType>(data))) {
				return CMD_ERROR;
			}
			break;
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		switch (mof) {
			case MOF_NON_STOP:
				order->SetNonStopType((OrderNonStopFlags)data);
				if ((data & ONSF_NO_STOP_AT_DESTINATION_STATION) && order->IsType(OT_GOTO_STATION)) {
					order->SetRefit(CARGO_NO_REFIT);
					order->SetLoadType(OLF_LOAD_IF_POSSIBLE);
					order->SetUnloadType(OUF_UNLOAD_IF_POSSIBLE);
					if (order->IsWaitTimetabled() || order->GetWaitTime() > 0) {
						Command<CMD_CHANGE_TIMETABLE>::Do(flags, v->index, sel_ord, MTF_WAIT_TIME, 0, MTCF_CLEAR_FIELD);
					}
					if (order->IsScheduledDispatchOrder(false)) {
						Command<CMD_CHANGE_TIMETABLE>::Do(flags, v->index, sel_ord, MTF_ASSIGN_SCHEDULE, -1, MTCF_NONE);
					}
				}
				break;

			case MOF_STOP_LOCATION:
				order->SetStopLocation((OrderStopLocation)data);
				break;

			case MOF_UNLOAD:
				order->SetUnloadType((OrderUnloadFlags)data);
				break;

			case MOF_CARGO_TYPE_UNLOAD:
				if (cargo_id == INVALID_CARGO) {
					for (CargoType i = 0; i < NUM_CARGO; i++) {
						order->SetUnloadType((OrderUnloadFlags)data, i);
					}
				} else {
					order->SetUnloadType((OrderUnloadFlags)data, cargo_id);
				}
				break;

			case MOF_LOAD:
				order->SetLoadType((OrderLoadFlags)data);
				if (data & OLFB_NO_LOAD) order->SetRefit(CARGO_NO_REFIT);
				break;

			case MOF_CARGO_TYPE_LOAD:
				if (cargo_id == INVALID_CARGO) {
					for (CargoType i = 0; i < NUM_CARGO; i++) {
						order->SetLoadType((OrderLoadFlags)data, i);
					}
				} else {
					order->SetLoadType((OrderLoadFlags)data, cargo_id);
				}
				break;

			case MOF_DEPOT_ACTION: {
				OrderDepotActionFlags base_order_action_type = order->GetDepotActionType() & ~(ODATFB_HALT | ODATFB_SELL | ODATFB_UNBUNCH);
				switch (data) {
					case DA_ALWAYS_GO:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type));
						break;

					case DA_SERVICE:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() | ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type));
						order->SetRefit(CARGO_NO_REFIT);
						break;

					case DA_STOP:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type | ODATFB_HALT));
						order->SetRefit(CARGO_NO_REFIT);
						break;

					case DA_SELL:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type | ODATFB_HALT | ODATFB_SELL));
						order->SetRefit(CARGO_NO_REFIT);
						break;

					case DA_UNBUNCH:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type | ODATFB_UNBUNCH));
						break;

					default:
						NOT_REACHED();
				}
				break;
			}

			case MOF_COND_VARIABLE: {
				/* Check whether old conditional variable had a cargo as value */
				const OrderConditionVariable old_condition = order->GetConditionVariable();
				const OrderConditionVariable new_condition = (OrderConditionVariable)data;
				bool old_var_was_cargo = (order->GetConditionVariable() == OCV_CARGO_ACCEPTANCE || order->GetConditionVariable() == OCV_CARGO_WAITING
						|| order->GetConditionVariable() == OCV_CARGO_LOAD_PERCENTAGE || order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT
						|| order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT_PERCENTAGE);
				bool old_var_was_slot = (order->GetConditionVariable() == OCV_SLOT_OCCUPANCY || order->GetConditionVariable() == OCV_VEH_IN_SLOT);
				bool old_var_was_slot_group = (order->GetConditionVariable() == OCV_VEH_IN_SLOT_GROUP);
				bool old_var_was_counter = (order->GetConditionVariable() == OCV_COUNTER_VALUE);
				bool old_var_was_time = (order->GetConditionVariable() == OCV_TIME_DATE);
				bool old_var_was_tt = (order->GetConditionVariable() == OCV_TIMETABLE);

				order->SetConditionVariable(new_condition);

				if (ConditionVariableHasStationID(new_condition) && !ConditionVariableHasStationID(old_condition)) {
					order->SetConditionStationID(StationID::Invalid());
				}
				OrderConditionComparator occ = order->GetConditionComparator();
				switch (new_condition) {
					case OCV_UNCONDITIONALLY:
						order->SetConditionComparator(OCC_EQUALS);
						order->SetConditionValue(0);
						break;

					case OCV_SLOT_OCCUPANCY:
					case OCV_VEH_IN_SLOT:
						if (!old_var_was_slot) {
							order->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID.base();
						} else if (order->GetConditionVariable() == OCV_VEH_IN_SLOT && order->GetXData() != INVALID_TRACE_RESTRICT_SLOT_ID && TraceRestrictSlot::Get(order->GetXData())->vehicle_type != v->type) {
							order->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID.base();
						}
						if (old_condition != order->GetConditionVariable()) order->SetConditionComparator(OCC_IS_TRUE);
						break;

					case OCV_VEH_IN_SLOT_GROUP:
						if (!old_var_was_slot_group) {
							order->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_GROUP.base();
						}
						if (old_condition != order->GetConditionVariable()) order->SetConditionComparator(OCC_IS_TRUE);
						break;

					case OCV_COUNTER_VALUE:
						if (!old_var_was_counter) order->GetXDataRef() = INVALID_TRACE_RESTRICT_COUNTER_ID.base() << 16;
						if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) order->SetConditionComparator(OCC_EQUALS);
						break;

					case OCV_TIME_DATE:
						if (!old_var_was_time) {
							order->SetConditionValue(0);
							order->GetXDataRef() = 0;
						}
						if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) order->SetConditionComparator(OCC_EQUALS);
						break;

					case OCV_TIMETABLE:
						if (!old_var_was_tt) {
							order->SetConditionValue(0);
							order->GetXDataRef() = 0;
						}
						if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE || occ == OCC_EQUALS || occ == OCC_NOT_EQUALS) order->SetConditionComparator(OCC_LESS_THAN);
						break;

					case OCV_CARGO_ACCEPTANCE:
					case OCV_CARGO_WAITING:
						if (!old_var_was_cargo) order->SetConditionValue((uint16_t) GetFirstValidCargo());
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						break;
					case OCV_CARGO_LOAD_PERCENTAGE:
						if (!old_var_was_cargo) order->SetConditionValue((uint16_t) GetFirstValidCargo());
						order->GetXDataRef() = 0;
						order->SetConditionComparator(OCC_EQUALS);
						break;
					case OCV_CARGO_WAITING_AMOUNT:
					case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
						if (!old_var_was_cargo) order->SetConditionValue((uint16_t) GetFirstValidCargo());
						if (!ConditionVariableTestsCargoWaitingAmount(old_condition)) order->ClearConditionViaStation();
						order->SetXDataLow(0);
						order->SetXData2High(0);
						order->SetConditionComparator(OCC_EQUALS);
						break;
					case OCV_REQUIRES_SERVICE:
						if (old_var_was_cargo || old_var_was_slot) order->SetConditionValue(0);
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						order->SetConditionValue(0);
						break;
					case OCV_DISPATCH_SLOT:
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						order->SetConditionValue(ODCS_VEH << ODCB_SRC_START);
						order->GetXDataRef() = UINT16_MAX;
						break;

					case OCV_PERCENT:
						order->SetConditionComparator(OCC_EQUALS);
						/* FALL THROUGH */
					case OCV_LOAD_PERCENTAGE:
					case OCV_RELIABILITY:
						if (order->GetConditionValue() > 100) order->SetConditionValue(100);
						[[fallthrough]];

					default:
						if (old_var_was_cargo || old_var_was_slot || old_var_was_counter || old_var_was_time || old_var_was_tt) order->SetConditionValue(0);
						if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) order->SetConditionComparator(OCC_EQUALS);
						break;
				}
				break;
			}

			case MOF_COND_COMPARATOR:
				order->SetConditionComparator((OrderConditionComparator)data);
				break;

			case MOF_COND_VALUE:
				switch (order->GetConditionVariable()) {
					case OCV_SLOT_OCCUPANCY:
					case OCV_CARGO_LOAD_PERCENTAGE:
					case OCV_TIME_DATE:
					case OCV_TIMETABLE:
						order->GetXDataRef() = data;
						break;

					case OCV_VEH_IN_SLOT:
						order->GetXDataRef() = data;
						if (data != INVALID_TRACE_RESTRICT_SLOT_ID && TraceRestrictSlot::Get(data)->vehicle_type != v->type) {
							if (order->GetConditionComparator() == OCC_EQUALS) order->SetConditionComparator(OCC_IS_TRUE);
							if (order->GetConditionComparator() == OCC_NOT_EQUALS) order->SetConditionComparator(OCC_IS_FALSE);
						}
						break;

					case OCV_VEH_IN_SLOT_GROUP:
						order->GetXDataRef() = data;
						break;

					case OCV_CARGO_WAITING_AMOUNT:
					case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
					case OCV_COUNTER_VALUE:
						order->SetXDataLow(data);
						break;

					case OCV_DISPATCH_SLOT:
						order->SetConditionValue(data);
						if (GB(data, ODCB_MODE_START, ODCB_MODE_COUNT) != OCDM_ROUTE_ID && order->GetXData2Low() != 0) {
							/* Clear any route ID when changing mode */
							order->SetXData2Low(0);
						}
						break;

					default:
						order->SetConditionValue(data);
						break;
				}
				break;

			case MOF_COND_VALUE_2:
				switch (order->GetConditionVariable()) {
					case OCV_COUNTER_VALUE:
						order->SetXDataHigh(data);
						break;

					case OCV_DISPATCH_SLOT:
						order->SetConditionDispatchScheduleID(data);
						if (GB(order->GetConditionValue(), ODCB_MODE_START, ODCB_MODE_COUNT) == OCDM_ROUTE_ID && order->GetXData2Low() != 0) {
							/* Clear any route ID when changing schedule */
							order->SetXData2Low(0);
						}
						break;

					default:
						order->SetConditionValue(data);
						break;
				}
				break;

			case MOF_COND_VALUE_3:
				switch (order->GetConditionVariable()) {
					case OCV_CARGO_WAITING_AMOUNT:
					case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE:
						order->SetConditionViaStationID(StationID(data));
						break;

					case OCV_DISPATCH_SLOT:
						order->SetXData2Low(data);
						break;

					default:
						NOT_REACHED();
				}
				break;

			case MOF_COND_VALUE_4:
				SB(order->GetXData2Ref(), 16, 1, data);
				break;

			case MOF_COND_STATION_ID:
				order->SetConditionStationID(StationID(data));
				if (ConditionVariableTestsCargoWaitingAmount(order->GetConditionVariable()) && data == order->GetConditionViaStationID()) {
					/* Clear via if station is set to the same ID */
					order->ClearConditionViaStation();
				}
				break;

			case MOF_COND_DESTINATION:
				order->SetConditionSkipToOrder(data);
				break;

			case MOF_WAYPOINT_FLAGS:
				order->SetWaypointFlags(static_cast<OrderWaypointFlags>(data));
				break;

			case MOF_SLOT:
			case MOF_SLOT_GROUP:
			case MOF_COUNTER_ID:
				order->SetDestination(data);
				break;

			case MOF_RV_TRAVEL_DIR:
				order->SetRoadVehTravelDirection((DiagDirection)data);
				break;

			case MOF_COUNTER_OP:
				order->SetCounterOperation(data);
				break;

			case MOF_COUNTER_VALUE:
				order->GetXDataRef() = data;
				break;

			case MOF_COLOUR:
				order->SetColour((Colours)data);
				break;

			case MOF_LABEL_TEXT:
				order->SetLabelText(text);
				break;

			case MOF_DEPARTURES_SUBTYPE:
				order->SetLabelSubType(static_cast<OrderLabelSubType>(data));
				break;

			default: NOT_REACHED();
		}

		/* Update the windows and full load flags, also for vehicles that share the same order list */
		Vehicle *u = v->FirstShared();
		DeleteOrderWarnings(u);
		for (; u != nullptr; u = u->NextShared()) {
			/* Toggle u->current_order "Full load" flag if it changed.
			 * However, as the same flag is used for depot orders, check
			 * whether we are not going to a depot as there are three
			 * cases where the full load flag can be active and only
			 * one case where the flag is used for depot orders. In the
			 * other cases for the OrderType the flags are not used,
			 * so do not care and those orders should not be active
			 * when this function is called.
			 */
			if (sel_ord == u->cur_real_order_index &&
					(u->current_order.IsType(OT_GOTO_STATION) || u->current_order.IsAnyLoadingType())) {
				if (u->current_order.GetLoadType() != order->GetLoadType()) {
					u->current_order.SetLoadType(order->GetLoadType());
				}
				if (u->current_order.GetUnloadType() != order->GetUnloadType()) {
					u->current_order.SetUnloadType(order->GetUnloadType());
				}
				switch (mof) {
					case MOF_CARGO_TYPE_UNLOAD:
						if (cargo_id == INVALID_CARGO) {
							for (CargoType i = 0; i < NUM_CARGO; i++) {
								u->current_order.SetUnloadType((OrderUnloadFlags)data, i);
							}
						} else {
							u->current_order.SetUnloadType((OrderUnloadFlags)data, cargo_id);
						}
						break;

					case MOF_CARGO_TYPE_LOAD:
						if (cargo_id == INVALID_CARGO) {
							for (CargoType i = 0; i < NUM_CARGO; i++) {
								u->current_order.SetLoadType((OrderLoadFlags)data, i);
							}
						} else {
							u->current_order.SetLoadType((OrderLoadFlags)data, cargo_id);
						}
						break;

					default:
						break;
				}
			}
			if (mof == MOF_RV_TRAVEL_DIR && sel_ord == u->cur_real_order_index &&
					(u->current_order.IsType(OT_GOTO_STATION) || u->current_order.IsType(OT_GOTO_WAYPOINT))) {
				u->current_order.SetRoadVehTravelDirection((DiagDirection)data);
			}

			/* Unbunching data is no longer valid. */
			u->ResetDepotUnbunching();

			InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);
		}
	}

	return CommandCost();
}

/**
 * Check if an aircraft has enough range for an order list.
 * @param v_new Aircraft to check.
 * @param v_order Vehicle currently holding the order list.
 * @return True if the aircraft has enough range for the orders, false otherwise.
 */
static bool CheckAircraftOrderDistance(const Aircraft *v_new, const Vehicle *v_order)
{
	if (v_order->orders == nullptr || v_new->acache.cached_max_range == 0) return true;

	/* Iterate over all orders to check the distance between all
	 * 'goto' orders and their respective next order (of any type). */
	for (const Order *o : v_order->orders->Orders()) {
		switch (o->GetType()) {
			case OT_GOTO_STATION:
			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				/* If we don't have a next order, we've reached the end and must check the first order instead. */
				if (GetOrderDistance(o, v_order->orders->GetNext(o), v_order) > v_new->acache.cached_max_range_sqr) return false;
				break;

			default: break;
		}
	}

	return true;
}

static void CheckAdvanceVehicleOrdersAfterClone(Vehicle *v, DoCommandFlags flags)
{
	const Company *owner = Company::GetIfValid(v->owner);
	if (!owner || !owner->settings.advance_order_on_clone || !v->IsInDepot() || !IsDepotTile(v->tile)) return;

	std::vector<VehicleOrderID> target_orders;

	const int order_count = v->GetNumOrders();
	if (v->type == VEH_AIRCRAFT) {
		for (VehicleOrderID idx = 0; idx < order_count; idx++) {
			const Order *o = v->GetOrder(idx);
			if (o->IsType(OT_GOTO_STATION) && o->GetDestination() == GetStationIndex(v->tile)) {
				target_orders.push_back(idx);
			}
		}
	} else if (GetDepotVehicleType(v->tile) == v->type) {
		for (VehicleOrderID idx = 0; idx < order_count; idx++) {
			const Order *o = v->GetOrder(idx);
			if (o->IsType(OT_GOTO_DEPOT) && o->GetDestination() == GetDepotIndex(v->tile)) {
				target_orders.push_back(idx + 1 < order_count ? idx + 1 : 0);
			}
		}
	}
	if (target_orders.empty()) return;

	VehicleOrderID skip_to = target_orders[v->unitnumber % target_orders.size()];
	Command<CMD_SKIP_TO_ORDER>::Do(flags, v->index, skip_to);
}

static bool ShouldResetOrderIndicesOnOrderCopy(const Vehicle *src, const Vehicle *dst)
{
	const int num_orders = src->GetNumOrders();
	if (dst->GetNumOrders() != num_orders) return true;

	for (int i = 0; i < num_orders; i++) {
		if (!src->GetOrder(i)->Equals(*dst->GetOrder(i))) return true;
	}
	return false;
}

/**
 * Clone/share/copy an order-list of another vehicle.
 * @param flags operation to perform
 * @param action action to perform
 * @param veh_dst destination vehicle to clone orders to
 * @param veh_src source vehicle to clone orders from, if any (none for CO_UNSHARE)
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneOrder(DoCommandFlags flags, CloneOptions action, VehicleID veh_dst, VehicleID veh_src)
{
	Vehicle *dst = Vehicle::GetIfValid(veh_dst);
	if (dst == nullptr || !dst->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(dst->owner);
	if (ret.Failed()) return ret;

	switch (action) {
		case CO_SHARE: {
			Vehicle *src = Vehicle::GetIfValid(veh_src);

			/* Sanity checks */
			if (src == nullptr || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src) return CMD_ERROR;

			ret = CheckOwnership(src->owner);
			if (ret.Failed()) return ret;

			/* Trucks can't share orders with busses (and visa versa) */
			if (src->type == VEH_ROAD && RoadVehicle::From(src)->IsBus() != RoadVehicle::From(dst)->IsBus()) {
				return CMD_ERROR;
			}

			/* Is the vehicle already in the shared list? */
			if (src->FirstShared() == dst->FirstShared()) return CMD_ERROR;

			for (const Order *order : src->Orders()) {
				if (OrderGoesToStation(dst, order)) {
					/* Allow copying unreachable destinations if they were already unreachable for the source.
					 * This is basically to allow cloning / autorenewing / autoreplacing vehicles, while the stations
					 * are temporarily invalid due to reconstruction. */
					const Station *st = Station::Get(order->GetDestination().ToStationID());
					if (CanVehicleUseStation(src, st) && !CanVehicleUseStation(dst, st)) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, GetVehicleCannotUseStationReason(dst, st));
					}
				}
				if (OrderGoesToRoadDepot(dst, order)) {
					const Depot *dp = Depot::GetIfValid(order->GetDestination().ToDepotID());
					if (dp != nullptr && (GetPresentRoadTypes(dp->xy) & RoadVehicle::From(dst)->compatible_roadtypes).None()) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, RoadTypeIsTram(RoadVehicle::From(dst)->roadtype) ? STR_ERROR_NO_STOP_COMPATIBLE_TRAM_TYPE : STR_ERROR_NO_STOP_COMPATIBLE_ROAD_TYPE);
					}
				}
			}

			/* Check for aircraft range limits. */
			if (dst->type == VEH_AIRCRAFT && !CheckAircraftOrderDistance(Aircraft::From(dst), src)) {
				return CommandCost(STR_ERROR_AIRCRAFT_NOT_ENOUGH_RANGE);
			}

			if (src->orders == nullptr && !OrderList::CanAllocateItem()) {
				return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			}

			if (flags.Test(DoCommandFlag::Execute)) {
				/* If the destination vehicle had a OrderList, destroy it.
				 * We reset the order indices, if the new orders are different.
				 * (We mainly do this to keep the order indices valid and in range.) */
				DeleteVehicleOrders(dst, false, ShouldResetOrderIndicesOnOrderCopy(src, dst));
				dst->dispatch_records.clear();

				dst->orders = src->orders;

				/* Link this vehicle in the shared-list */
				dst->AddToShared(src);


				/* Set automation bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::AutomateTimetable)) {
					dst->vehicle_flags.Set(VehicleFlag::AutomateTimetable);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::AutomateTimetable);
				}
				/* Set auto separation bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
					dst->vehicle_flags.Set(VehicleFlag::TimetableSeparation);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::TimetableSeparation);
				}
				/* Set manual dispatch bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
					dst->vehicle_flags.Set(VehicleFlag::ScheduledDispatch);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::ScheduledDispatch);
				}
				dst->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
				dst->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);

				dst->ClearSeparation();
				if (dst->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) dst->vehicle_flags.Reset(VehicleFlag::TimetableStarted);

				InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);
				InvalidateVehicleOrder(src, VIWD_MODIFY_ORDERS);

				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type));
				InvalidateWindowClassesData(WC_DEPARTURES_BOARD);

				CheckAdvanceVehicleOrdersAfterClone(dst, flags);
			}
			break;
		}

		case CO_COPY: {
			Vehicle *src = Vehicle::GetIfValid(veh_src);

			/* Sanity checks */
			if (src == nullptr || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src) return CMD_ERROR;

			if (!_settings_game.economy.infrastructure_sharing[src->type]) {
				CommandCost ret = CheckOwnership(src->owner);
				if (ret.Failed()) return ret;
			}

			/* Trucks can't copy all the orders from busses (and visa versa),
			 * and neither can helicopters and aircraft. */
			for (const Order *order : src->Orders()) {
				if (OrderGoesToStation(dst, order)) {
					const Station *st = Station::Get(order->GetDestination().ToStationID());
					if (!CanVehicleUseStation(dst, st)) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, GetVehicleCannotUseStationReason(dst, st));
					}
				}
				if (OrderGoesToRoadDepot(dst, order)) {
					const Depot *dp = Depot::GetIfValid(order->GetDestination().ToDepotID());
					if (dp != nullptr && (GetPresentRoadTypes(dp->xy) & RoadVehicle::From(dst)->compatible_roadtypes).None()) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, RoadTypeIsTram(RoadVehicle::From(dst)->roadtype) ? STR_ERROR_NO_STOP_COMPATIBLE_TRAM_TYPE : STR_ERROR_NO_STOP_COMPATIBLE_ROAD_TYPE);
					}
				}
			}

			/* Check for aircraft range limits. */
			if (dst->type == VEH_AIRCRAFT && !CheckAircraftOrderDistance(Aircraft::From(dst), src)) {
				return CommandCost(STR_ERROR_AIRCRAFT_NOT_ENOUGH_RANGE);
			}

			/* make sure there are orders available */
			if (!OrderList::CanAllocateItem()) {
				return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			}

			if (flags.Test(DoCommandFlag::Execute)) {
				/* If the destination vehicle had an order list, destroy the chain but keep the OrderList.
				 * We only the order indices, if the new orders are different.
				 * (We mainly do this to keep the order indices valid and in range.) */
				DeleteVehicleOrders(dst, true, ShouldResetOrderIndicesOnOrderCopy(src, dst));
				dst->dispatch_records.clear();

				std::vector<Order> dst_orders;
				for (const Order *order : src->Orders()) {
					dst_orders.emplace_back(*order); // clone order
					TraceRestrictRemoveNonOwnedReferencesFromOrder(&dst_orders.back(), dst->owner);
				}
				if (dst->orders != nullptr) {
					assert(dst->orders->GetNumOrders() == 0);
					assert(!dst->orders->IsShared());
					delete dst->orders;
					dst->orders = nullptr;
				}
				assert(OrderList::CanAllocateItem());
				dst->orders = new OrderList(std::move(dst_orders), dst);

				/* Copy over scheduled dispatch data */
				assert(dst->orders != nullptr);
				if (src->orders != nullptr) {
					dst->orders->GetScheduledDispatchScheduleSet() = src->orders->GetScheduledDispatchScheduleSet();
					for (DispatchSchedule &ds : dst->orders->GetScheduledDispatchScheduleSet()) {
						ds.ResetStateAfterClone();
					}
				}

				/* Set automation bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::AutomateTimetable)) {
					dst->vehicle_flags.Set(VehicleFlag::AutomateTimetable);
					dst->vehicle_flags.Reset(VehicleFlag::AutofillTimetable);
					dst->vehicle_flags.Reset(VehicleFlag::AutofillPreserveWaitTime);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::AutomateTimetable);
				}
				/* Set auto separation bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
					dst->vehicle_flags.Set(VehicleFlag::TimetableSeparation);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::TimetableSeparation);
				}
				/* Set manual dispatch bit if target has it. */
				if (src->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)) {
					dst->vehicle_flags.Set(VehicleFlag::ScheduledDispatch);
				} else {
					dst->vehicle_flags.Reset(VehicleFlag::ScheduledDispatch);
				}

				InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);

				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type));
				InvalidateWindowClassesData(WC_DEPARTURES_BOARD);

				CheckAdvanceVehicleOrdersAfterClone(dst, flags);
			}
			break;
		}

		case CO_UNSHARE: return DecloneOrder(dst, flags);
		default: return CMD_ERROR;
	}

	return CommandCost();
}

/**
 * Clone/share/copy an order-list of another vehicle.
 * @param flags operation to perform
 * @param veh_dst destination vehicle to copy orders to
 * @param veh_src source vehicle to copy orders from
 * @param insert_pos position to insert orders
 * @return the cost of this operation or an error
 */
CommandCost CmdInsertOrdersFromVehicle(DoCommandFlags flags, VehicleID veh_dst, VehicleID veh_src, VehicleOrderID insert_pos)
{
	Vehicle *dst = Vehicle::GetIfValid(veh_dst);
	if (dst == nullptr || !dst->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(dst->owner);
	if (ret.Failed()) return ret;

	const Vehicle *src = Vehicle::GetIfValid(veh_src);

	/* Sanity checks */
	if (src == nullptr || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src || src->FirstShared() == dst->FirstShared()) return CMD_ERROR;

	if (insert_pos > dst->GetNumOrders()) return CMD_ERROR;

	if (!_settings_game.economy.infrastructure_sharing[src->type]) {
		CommandCost ret = CheckOwnership(src->owner);
		if (ret.Failed()) return ret;
	}

	if (src->orders == nullptr) return CommandCost();

	/* make sure there are orders available */
	if (dst->orders == nullptr && !OrderList::CanAllocateItem()) {
		return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	}

	if (src->GetNumOrders() + dst->GetNumOrders() > MAX_VEH_ORDER_ID) return CommandCost(STR_ERROR_TOO_MANY_ORDERS);

	if (dst->HasUnbunchingOrder()) {
		if (src->HasFullLoadOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, STR_ERROR_UNBUNCHING_NO_UNBUNCHING_FULL_LOAD);
		if (src->HasUnbunchingOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, STR_ERROR_UNBUNCHING_ONLY_ONE_ALLOWED);
		if (src->HasConditionalOrder()) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, STR_ERROR_UNBUNCHING_NO_UNBUNCHING_CONDITIONAL);
	}

	const VehicleOrderID new_orders_start = dst->GetNumOrders();
	const uint existing_schedule_count = (dst->orders != nullptr) ? dst->orders->GetScheduledDispatchScheduleCount() : 0;

	/* Copy over scheduled dispatch data */
	if (flags.Test(DoCommandFlag::Execute)) {
		if (dst->orders == nullptr) {
			dst->orders = new OrderList(nullptr, dst);
		}

		const std::vector<DispatchSchedule> &src_scheds = src->orders->GetScheduledDispatchScheduleSet();
		std::vector<DispatchSchedule> &dst_scheds = dst->orders->GetScheduledDispatchScheduleSet();
		dst_scheds.reserve(dst_scheds.size() + src_scheds.size());
		for (const DispatchSchedule &src_ds : src_scheds) {
			dst_scheds.push_back(src_ds);
			dst_scheds.back().ResetStateAfterClone();
		}
	}

	for (const Order *src_order : src->Orders()) {
		/* Clone order and link in list */
		Order order(*src_order);
		TraceRestrictRemoveNonOwnedReferencesFromOrder(&order, dst->owner);
		if (order.IsType(OT_CONDITIONAL)) {
			order.SetConditionSkipToOrder(order.GetConditionSkipToOrder() + new_orders_start);
			if (order.GetConditionVariable() == OCV_DISPATCH_SLOT && order.GetConditionDispatchScheduleID() != UINT16_MAX) {
				order.SetConditionDispatchScheduleID(static_cast<uint16_t>(order.GetConditionDispatchScheduleID() + existing_schedule_count));
			}
		}
		if (order.GetDispatchScheduleIndex() >= 0) {
			order.SetDispatchScheduleIndex(order.GetDispatchScheduleIndex() + existing_schedule_count);
		}

		CommandCost ret = PreInsertOrderCheck(dst, order, {CmdInsertOrderIntlFlag::AllowLoadByCargoType, CmdInsertOrderIntlFlag::NoUnbunchChecks, CmdInsertOrderIntlFlag::NoConditionTargetCheck});
		if (ret.Failed()) {
			if (flags.Test(DoCommandFlag::Execute)) {
				/* Remove partially inserted orders */
				while (dst->GetNumOrders() > new_orders_start) {
					dst->orders->DeleteOrderAt(dst->GetNumOrders() - 1);
				}
			}
			return ret;
		}

		if (flags.Test(DoCommandFlag::Execute)) dst->orders->InsertOrderAt(std::move(order), dst->GetNumOrders());
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		CmdMoveOrder(flags, veh_dst, new_orders_start, insert_pos, dst->GetNumOrders() - new_orders_start);

		for (Vehicle *u = dst->FirstShared(); u != nullptr; u = u->NextShared()) {
			u->ResetDepotUnbunching();

			InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);
		}
		InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type));
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
	}

	return CommandCost();
}

/**
 * Add/remove refit orders from an order
 * @param flags operation to perform
 * @param veh VehicleIndex of the vehicle having the order
 * @param order_number number of order to modify
 * @param cargo CargoType
 * @return the cost of this operation or an error
 */
CommandCost CmdOrderRefit(DoCommandFlags flags, VehicleID veh, VehicleOrderID order_number, CargoType cargo)
{
	if (cargo >= NUM_CARGO && cargo != CARGO_NO_REFIT && cargo != CARGO_AUTO_REFIT) return CMD_ERROR;

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	Order *order = v->GetOrder(order_number);
	if (order == nullptr) return CMD_ERROR;

	/* Automatic refit cargo is only supported for goto station orders. */
	if (cargo == CARGO_AUTO_REFIT && !order->IsType(OT_GOTO_STATION)) return CMD_ERROR;

	if (order->GetLoadType() & OLFB_NO_LOAD) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		order->SetRefit(cargo);

		/* Make the depot order an 'always go' order. */
		if (cargo != CARGO_NO_REFIT && order->IsType(OT_GOTO_DEPOT)) {
			order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
			order->SetDepotActionType((OrderDepotActionFlags)(order->GetDepotActionType() & ~(ODATFB_HALT | ODATFB_SELL)));
		}

		for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);

			/* If the vehicle already got the current depot set as current order, then update current order as well */
			if (u->cur_real_order_index == order_number && (u->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) {
				u->current_order.SetRefit(cargo);
			}
		}
	}

	return CommandCost();
}


/**
 *
 * Check the orders of a vehicle, to see if there are invalid orders and stuff
 *
 */
void CheckOrders(const Vehicle *v)
{
	/* Does the user wants us to check things? */
	if (_settings_client.gui.order_review_system == 0) return;

	/* Do nothing for crashed vehicles */
	if (v->vehstatus.Test(VehState::Crashed)) return;

	/* Do nothing for stopped vehicles if setting is '1' */
	if (_settings_client.gui.order_review_system == 1 && v->vehstatus.Test(VehState::Stopped)) return;

	/* do nothing we we're not the first vehicle in a share-chain */
	if (v->FirstShared() != v) return;

	/* Only check every 20 days, so that we don't flood the message log */
	/* The check is skipped entirely in case the current vehicle is virtual (a.k.a a 'template train') */
	if (v->owner == _local_company && v->day_counter % 20 == 0 && !HasBit(v->subtype, GVSF_VIRTUAL)) {
		StringID message = INVALID_STRING_ID;

		/* Check the order list */
		int n_st = 0;
		bool has_depot_order = false;

		for (const Order *order : v->Orders()) {
			/* Dummy order? */
			if (order->IsType(OT_DUMMY)) {
				message = STR_NEWS_VEHICLE_HAS_VOID_ORDER;
				break;
			}
			/* Does station have a load-bay for this vehicle? */
			if (order->IsType(OT_GOTO_STATION)) {
				const Station *st = Station::Get(order->GetDestination().ToStationID());

				n_st++;
				if (!CanVehicleUseStation(v, st)) {
					message = STR_NEWS_VEHICLE_HAS_INVALID_ENTRY;
				} else if (v->type == VEH_AIRCRAFT &&
							(AircraftVehInfo(v->engine_type)->subtype & AIR_FAST) &&
							st->airport.GetFTA()->flags.Test(AirportFTAClass::Flag::ShortStrip) &&
							!_cheats.no_jetcrash.value &&
							message == INVALID_STRING_ID) {
					message = STR_NEWS_PLANE_USES_TOO_SHORT_RUNWAY;
				}
			}
			if (order->IsType(OT_GOTO_DEPOT)) {
				has_depot_order = true;
			}
		}

		/* Check if the last and the first order are the same, and the first order is a go to order */
		if (v->GetNumOrders() > 1 && v->orders->GetFirstOrder()->IsGotoOrder()) {
			const Order *last = v->GetLastOrder();

			if (v->orders->GetFirstOrder()->Equals(*last)) {
				message = STR_NEWS_VEHICLE_HAS_DUPLICATE_ENTRY;
			}
		}

		/* Do we only have 1 station in our order list? */
		if (n_st < 2 && message == INVALID_STRING_ID) message = STR_NEWS_VEHICLE_HAS_TOO_FEW_ORDERS;

#ifdef WITH_ASSERT
		if (v->orders != nullptr) v->orders->DebugCheckSanity();
#endif

		if (message == INVALID_STRING_ID && !has_depot_order && v->type != VEH_AIRCRAFT) {
			if (_settings_client.gui.no_depot_order_warn == 1 ||
					(_settings_client.gui.no_depot_order_warn == 2 && _settings_game.difficulty.vehicle_breakdowns != 0)) {
				message = STR_NEWS_VEHICLE_NO_DEPOT_ORDER;
			}
		}

		/* We don't have a problem */
		if (message == INVALID_STRING_ID) return;

		AddVehicleAdviceNewsItem(AdviceType::Order, GetEncodedString(message, v->index), v->index);
	}
}

static bool _remove_order_from_all_vehicles_batch = false;
std::vector<uint32_t> _remove_order_from_all_vehicles_depots;

static bool IsBatchRemoveOrderDepotRemoved(DestinationID destination)
{
	if (static_cast<size_t>(destination.base() / 32) >= _remove_order_from_all_vehicles_depots.size()) return false;

	return HasBit(_remove_order_from_all_vehicles_depots[destination.base() / 32], destination.base() % 32);
}

void StartRemoveOrderFromAllVehiclesBatch()
{
	assert(_remove_order_from_all_vehicles_batch == false);
	_remove_order_from_all_vehicles_batch = true;
}

void StopRemoveOrderFromAllVehiclesBatch()
{
	assert(_remove_order_from_all_vehicles_batch == true);
	_remove_order_from_all_vehicles_batch = false;

	/* Go through all vehicles */
	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		if (v->type == VEH_AIRCRAFT) continue;

		Order *order = &v->current_order;
		if (order->IsType(OT_GOTO_DEPOT) && IsBatchRemoveOrderDepotRemoved(order->GetDestination())) {
			order->MakeDummy();
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		}

		/* order list */
		if (v->FirstShared() != v) continue;

		RemoveVehicleOrdersIf(v, [&](const Order *o) {
			OrderType ot = o->GetType();
			if (ot != OT_GOTO_DEPOT) return false;
			if ((o->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) return false;
			return IsBatchRemoveOrderDepotRemoved(o->GetDestination());
		});
	}

	_remove_order_from_all_vehicles_depots.clear();
}

/**
 * Removes an order from all vehicles. Triggers when, say, a station is removed.
 * @param type The type of the order (OT_GOTO_[STATION|DEPOT|WAYPOINT]).
 * @param destination The destination. Can be a StationID, DepotID or WaypointID.
 * @param hangar Only used for airports in the destination.
 *               When false, remove airport and hangar orders.
 *               When true, remove either airport or hangar order.
 */
void RemoveOrderFromAllVehicles(OrderType type, DestinationID destination, bool hangar)
{
	if (destination == ((type == OT_GOTO_DEPOT) ? (DestinationID)DepotID::Invalid() : (DestinationID)StationID::Invalid())) return;

	OrderBackup::RemoveOrder(type, destination, hangar);

	if (_remove_order_from_all_vehicles_batch && type == OT_GOTO_DEPOT && !hangar) {
		std::vector<uint32_t> &ids = _remove_order_from_all_vehicles_depots;
		uint32_t word_idx = destination.base() / 32;
		if (word_idx >= ids.size()) ids.resize(word_idx + 1);
		SetBit(ids[word_idx], destination.base() % 32);
		return;
	}

	/* Aircraft have StationIDs for depot orders and never use DepotIDs
	 * This fact is handled specially below
	 */

	/* Go through all vehicles */
	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		Order *order = &v->current_order;
		if ((v->type == VEH_AIRCRAFT && order->IsType(OT_GOTO_DEPOT) && !hangar ? OT_GOTO_STATION : order->GetType()) == type &&
				(!hangar || v->type == VEH_AIRCRAFT) && order->GetDestination() == destination) {
			order->MakeDummy();
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		}

		/* order list */
		if (v->FirstShared() != v) continue;

		RemoveVehicleOrdersIf(v, [&](const Order *o) {
			OrderType ot = o->GetType();
			if (ot == OT_CONDITIONAL) {
				if (type == OT_GOTO_STATION && ConditionVariableTestsCargoWaitingAmount(o->GetConditionVariable())) {
					if (order->GetConditionViaStationID() == destination) order->SetConditionViaStationID(StationID::Invalid());
				}
				if (type == OT_GOTO_STATION && ConditionVariableHasStationID(o->GetConditionVariable())) {
					if (order->GetConditionStationID() == destination) order->SetConditionStationID(StationID::Invalid());
				}
				return false;
			}
			if (ot == OT_GOTO_DEPOT && (o->GetDepotActionType() & ODATFB_NEAREST_DEPOT) != 0) return false;
			if (ot == OT_GOTO_DEPOT && hangar && v->type != VEH_AIRCRAFT) return false; // Not an aircraft? Can't have a hangar order.
			if (ot == OT_IMPLICIT || (v->type == VEH_AIRCRAFT && ot == OT_GOTO_DEPOT && !hangar)) ot = OT_GOTO_STATION;
			if (ot == OT_LABEL && IsDestinationOrderLabelSubType(o->GetLabelSubType()) && (type == OT_GOTO_STATION || type == OT_GOTO_WAYPOINT) && o->GetDestination() == destination) return true;
			return (ot == type && o->GetDestination() == destination);
		});
	}
}

/**
 * Checks if a vehicle has a depot in its order list.
 * @return True iff at least one order is a depot order.
 */
bool Vehicle::HasDepotOrder() const
{
	for (const Order *order : this->Orders()) {
		if (order->IsType(OT_GOTO_DEPOT)) return true;
	}

	return false;
}

/**
 * Delete all orders from a vehicle
 * @param v                   Vehicle whose orders to reset
 * @param keep_orderlist      If true, do not free the order list, only empty it.
 * @param reset_order_indices If true, reset cur_implicit_order_index and cur_real_order_index
 *                            and cancel the current full load order (if the vehicle is loading).
 *                            If false, _you_ have to make sure the order indices are valid after
 *                            your messing with them!
 */
void DeleteVehicleOrders(Vehicle *v, bool keep_orderlist, bool reset_order_indices)
{
	DeleteOrderWarnings(v);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD);

	extern void UpdateDeparturesWindowVehicleFilter(const OrderList *order_list, bool remove);

	if (v->IsOrderListShared()) {
		/* Remove ourself from the shared order list. */
		UpdateDeparturesWindowVehicleFilter(v->orders, false);
		v->RemoveFromShared();
		v->orders = nullptr;
	} else {
		CloseWindowById(GetWindowClassForVehicleType(v->type), VehicleListIdentifier(VL_SHARED_ORDERS, v->type, v->owner, v->index).ToWindowNumber());
		if (v->orders != nullptr) {
			/* Remove the orders */
			if (!keep_orderlist) UpdateDeparturesWindowVehicleFilter(v->orders, true);
			v->orders->FreeChain(keep_orderlist);
			if (!keep_orderlist) v->orders = nullptr;
		}
	}

	/* Unbunching data is no longer valid. */
	v->ResetDepotUnbunching();

	if (reset_order_indices) {
		v->cur_implicit_order_index = v->cur_real_order_index = 0;
		v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		if (v->current_order.IsAnyLoadingType()) {
			CancelLoadingDueToDeletedOrder(v);
		}
	}
}

/**
 * Delete all orders from a vehicle, without unsharing orders or freeing order lists
 * @param v                   Vehicle whose orders to reset
 * @param reset_order_indices If true, reset cur_implicit_order_index and cur_real_order_index
 *                            and cancel the current full load order (if the vehicle is loading).
 *                            If false, _you_ have to make sure the order indices are valid after
 *                            your messing with them!
 */
static void ClearVehicleOrders(Vehicle *v, bool reset_order_indices = true)
{
	if (v->orders == nullptr) return;

	DeleteOrderWarnings(v->FirstShared());
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
	v->orders->FreeChain(true);

	for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
		/* Unbunching data is no longer valid. */
		u->ResetDepotUnbunching();

		if (reset_order_indices) {
			u->cur_implicit_order_index = u->cur_real_order_index = 0;
			u->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
			if (u->current_order.IsAnyLoadingType()) {
				CancelLoadingDueToDeletedOrder(u);
			}
		}
	}
}

/**
 * Clamp the service interval to the correct min/max. The actual min/max values
 * depend on whether it's in days, minutes, or percent.
 * @param interval The proposed service interval.
 * @param ispercent Whether the interval is a percent.
 * @return The service interval clamped to use the chosen units.
 */
uint16_t GetServiceIntervalClamped(int interval, bool ispercent)
{
	/* Service intervals are in percents. */
	if (ispercent) return Clamp(interval, MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT);

	/* Service intervals are in minutes. */
	if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) return Clamp(interval, MIN_SERVINT_MINUTES, MAX_SERVINT_MINUTES);

	/* Service intervals are in days. */
	return Clamp(interval, MIN_SERVINT_DAYS, MAX_SERVINT_DAYS);
}

/**
 *
 * Check if a vehicle has any valid orders
 *
 * @return false if there are no valid orders
 * @note Conditional orders are not considered valid destination orders
 *
 */
static bool CheckForValidOrders(const Vehicle *v)
{
	for (const Order *order : v->Orders()) {
		switch (order->GetType()) {
			case OT_GOTO_STATION:
			case OT_GOTO_WAYPOINT:
				return true;

			case OT_GOTO_DEPOT:
				if ((order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) == 0) return true;
				break;

			default:
				break;
		}
	}

	return false;
}

/**
 * Compare the variable and value based on the given comparator.
 */
bool OrderConditionCompare(OrderConditionComparator occ, int variable, int value)
{
	switch (occ) {
		case OCC_EQUALS:      return variable == value;
		case OCC_NOT_EQUALS:  return variable != value;
		case OCC_LESS_THAN:   return variable <  value;
		case OCC_LESS_EQUALS: return variable <= value;
		case OCC_MORE_THAN:   return variable >  value;
		case OCC_MORE_EQUALS: return variable >= value;
		case OCC_IS_TRUE:     return variable != 0;
		case OCC_IS_FALSE:    return variable == 0;
		default: NOT_REACHED();
	}
}

/* Get the number of free (train) platforms in a station.
 * @param st_id The StationID of the station.
 * @return The number of free train platforms.
 */
static uint16_t GetFreeStationPlatforms(StationID st_id)
{
	assert(Station::IsValidID(st_id));
	const Station *st = Station::Get(st_id);
	if (!st->facilities.Test(StationFacility::Train)) return 0;
	bool is_free;
	TileIndex t2;
	uint16_t counter = 0;
	for (TileIndex t1 : st->train_station) {
		if (st->TileBelongsToRailStation(t1)) {
			/* We only proceed if this tile is a track tile and the north(-east/-west) end of the platform */
			if (IsCompatibleTrainStationTile(t1 + TileOffsByDiagDir(GetRailStationAxis(t1) == AXIS_X ? DIAGDIR_NE : DIAGDIR_NW), t1) || IsStationTileBlocked(t1)) continue;
			is_free = true;
			t2 = t1;
			do {
				if (GetStationReservationTrackBits(t2)) {
					is_free = false;
					break;
				}
				t2 += TileOffsByDiagDir(GetRailStationAxis(t1) == AXIS_X ? DIAGDIR_SW : DIAGDIR_SE);
			} while (IsCompatibleTrainStationTile(t2, t1));
			if (is_free) counter++;
		}
	}
	return counter;
}

bool EvaluateDispatchSlotConditionalOrderVehicleRecord(const Order *order, const LastDispatchRecord &record)
{
	bool value = false;
	switch ((OrderDispatchConditionModes)GB(order->GetConditionValue(), ODCB_MODE_START, ODCB_MODE_COUNT)) {
		case ODCM_FIRST_LAST:
			if (HasBit(order->GetConditionValue(), ODFLCB_LAST_SLOT)) {
				value = HasBit(record.record_flags, LastDispatchRecord::RF_LAST_SLOT);
			} else {
				value = HasBit(record.record_flags, LastDispatchRecord::RF_FIRST_SLOT);
			}
			break;

		case OCDM_TAG: {
			uint8_t tag = (uint8_t)GB(order->GetConditionValue(), ODFLCB_TAG_START, ODFLCB_TAG_COUNT);
			value = HasBit(record.slot_flags, DispatchSlot::SDSF_FIRST_TAG + tag);
			break;
		}

		case OCDM_ROUTE_ID: {
			uint16_t route_id = order->GetXData2Low();
			value = (record.route_id == route_id);
			break;
		}
	}

	return OrderConditionCompare(order->GetConditionComparator(), value ? 1 : 0, 0);
}

const LastDispatchRecord *GetVehicleLastDispatchRecord(const Vehicle *v, uint16_t schedule_index)
{
	auto iter = v->dispatch_records.find(schedule_index);
	if (iter != v->dispatch_records.end()) return &(iter->second);

	return nullptr;
}

OrderConditionEvalResult EvaluateDispatchSlotConditionalOrder(const Order *order, std::span<const DispatchSchedule> schedules, StateTicks state_ticks, GetVehicleLastDispatchRecordFunctor get_vehicle_record)
{
	OrderConditionEvalResult::Type result_type = OrderConditionEvalResult::Type::Certain;

	uint16_t schedule_index = order->GetConditionDispatchScheduleID();
	if (schedule_index >= schedules.size()) return OrderConditionEvalResult(false, result_type);
	const DispatchSchedule &sched = schedules[schedule_index];

	const OrderDispatchConditionSources src = (OrderDispatchConditionSources)GB(order->GetConditionValue(), ODCB_SRC_START, ODCB_SRC_COUNT);
	if (src == ODCS_VEH) {
		/* Don't set predicted for vehicle record tests */

		const LastDispatchRecord *record = get_vehicle_record(schedule_index);
		if (record == nullptr) return OrderConditionEvalResult(OrderConditionCompare(order->GetConditionComparator(), 0, 0), result_type);

		return OrderConditionEvalResult(EvaluateDispatchSlotConditionalOrderVehicleRecord(order, *record), result_type);
	}

	if (sched.GetScheduledDispatch().empty()) return OrderConditionEvalResult(false, result_type);

	result_type = OrderConditionEvalResult::Type::Predicted;

	int32_t offset;
	switch (src) {
		case ODCS_LAST: {
			int32_t last = sched.GetScheduledDispatchLastDispatch();
			if (last == INVALID_SCHEDULED_DISPATCH_OFFSET) {
				/* No last dispatched */
				return OrderConditionEvalResult(OrderConditionCompare(order->GetConditionComparator(), 0, 0), result_type);
			}
			if (last < 0) {
				last += sched.GetScheduledDispatchDuration() * (1 + (-last / sched.GetScheduledDispatchDuration()));
			}
			offset = last % sched.GetScheduledDispatchDuration();
			break;
		}

		case ODCS_NEXT: {
			StateTicks slot = GetScheduledDispatchTime(sched, state_ticks).first;
			if (slot == INVALID_STATE_TICKS) {
				/* No next dispatch */
				return OrderConditionEvalResult(OrderConditionCompare(order->GetConditionComparator(), 0, 0), result_type);
			}
			offset = (slot - sched.GetScheduledDispatchStartTick()).base() % sched.GetScheduledDispatchDuration();
			break;
		}

		default:
			NOT_REACHED();
	}

	bool value = false;
	switch ((OrderDispatchConditionModes)GB(order->GetConditionValue(), ODCB_MODE_START, ODCB_MODE_COUNT)) {
		case ODCM_FIRST_LAST:
			if (HasBit(order->GetConditionValue(), ODFLCB_LAST_SLOT)) {
				value = (offset == (int32_t)sched.GetScheduledDispatch().back().offset);
			} else {
				value = (offset == (int32_t)sched.GetScheduledDispatch().front().offset);
			}
			break;

		case OCDM_TAG: {
			uint8_t tag = (uint8_t)GB(order->GetConditionValue(), ODFLCB_TAG_START, ODFLCB_TAG_COUNT);
			for (const DispatchSlot &slot : sched.GetScheduledDispatch()) {
				if (offset == (int32_t)slot.offset) {
					value = HasBit(slot.flags, DispatchSlot::SDSF_FIRST_TAG + tag);
					break;
				}
			}
			break;
		}

		case OCDM_ROUTE_ID: {
			uint16_t route_id = order->GetXData2Low();
			for (const DispatchSlot &slot : sched.GetScheduledDispatch()) {
				if (offset == (int32_t)slot.offset) {
					value = (slot.route_id == route_id);
					break;
				}
			}
			break;
		}
	}

	return OrderConditionEvalResult(OrderConditionCompare(order->GetConditionComparator(), value ? 1 : 0, 0), result_type);
}

static TraceRestrictVehicleTemporarySlotMembershipState _pco_deferred_slot_membership;
static btree::btree_map<TraceRestrictCounterID, int32_t> _pco_deferred_counter_values;
static btree::btree_map<Order *, int8_t> _pco_deferred_original_percent_cond;

static bool ExecuteVehicleInSlotOrderCondition(const Vehicle *v, TraceRestrictSlot *slot, ProcessConditionalOrderMode mode, bool acquire)
{
	bool occupant;
	if (mode == PCO_DEFERRED && _pco_deferred_slot_membership.IsValid()) {
		occupant = _pco_deferred_slot_membership.IsInSlot(slot->index);
	} else {
		occupant = slot->IsOccupant(v->index);
	}
	if (acquire) {
		if (!occupant && mode == PCO_EXEC) {
			occupant = slot->Occupy(v);
		}
		if (!occupant && mode == PCO_DEFERRED) {
			occupant = slot->OccupyDryRun(v->index);
			if (occupant) {
				_pco_deferred_slot_membership.Initialise(v);
				_pco_deferred_slot_membership.AddSlot(slot->index);
			}
		}
	}
	return occupant;
}

/**
 * Process a conditional order and determine the next order.
 * @param order the order the vehicle currently has
 * @param v the vehicle to update
 * @param mode whether this is a dry-run so do not execute side-effects, or if side-effects are deferred
 * @return index of next order to jump to, or INVALID_VEH_ORDER_ID to use the next order
 */
VehicleOrderID ProcessConditionalOrder(const Order *order, const Vehicle *v, ProcessConditionalOrderMode mode)
{
	if (order->GetType() != OT_CONDITIONAL) return INVALID_VEH_ORDER_ID;

	bool skip_order = false;
	OrderConditionComparator occ = order->GetConditionComparator();
	uint16_t value = order->GetConditionValue();

	// OrderConditionCompare ignores the last parameter for occ == OCC_IS_TRUE or occ == OCC_IS_FALSE.
	switch (order->GetConditionVariable()) {
		case OCV_LOAD_PERCENTAGE:    skip_order = OrderConditionCompare(occ, CalcPercentVehicleFilled(v, nullptr), value); break;
		case OCV_CARGO_LOAD_PERCENTAGE: skip_order = OrderConditionCompare(occ, CalcPercentVehicleFilledOfCargo(v, (CargoType)value), order->GetXData()); break;
		case OCV_RELIABILITY:        skip_order = OrderConditionCompare(occ, ToPercent16(v->reliability),       value); break;
		case OCV_MAX_RELIABILITY:    skip_order = OrderConditionCompare(occ, ToPercent16(v->GetEngine()->reliability),   value); break;
		case OCV_MAX_SPEED:          skip_order = OrderConditionCompare(occ, v->GetDisplayMaxSpeed() * 10 / 16, value); break;
		case OCV_AGE:                skip_order = OrderConditionCompare(occ, DateDeltaToYearDelta(v->age).base(), value); break;
		case OCV_REQUIRES_SERVICE:   skip_order = OrderConditionCompare(occ, v->NeedsServicing(),               value); break;
		case OCV_UNCONDITIONALLY:    skip_order = true; break;
		case OCV_CARGO_WAITING: {
			StationID next_station = order->GetConditionStationID();
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, (Station::Get(next_station)->goods[value].CargoAvailableCount() > 0), value);
			break;
		}
		case OCV_CARGO_WAITING_AMOUNT: {
			StationID next_station = order->GetConditionStationID();
			if (Station::IsValidID(next_station)) {
				if (!order->HasConditionViaStation()) {
					skip_order = OrderConditionCompare(occ, Station::Get(next_station)->goods[value].CargoAvailableCount(), order->GetXDataLow());
				} else {
					skip_order = OrderConditionCompare(occ, Station::Get(next_station)->goods[value].CargoAvailableViaCount(order->GetConditionViaStationID()), order->GetXDataLow());
				}
			}
			break;
		}
		case OCV_CARGO_WAITING_AMOUNT_PERCENTAGE: {
			StationID next_station = order->GetConditionStationID();
			if (Station::IsValidID(next_station)) {
				const bool refit_mode = HasBit(order->GetXData2(), 16);
				const CargoType cargo = static_cast<CargoType>(value);
				uint32_t waiting;
				if (!order->HasConditionViaStation()) {
					waiting = Station::Get(next_station)->goods[cargo].CargoAvailableCount();
				} else {
					waiting = Station::Get(next_station)->goods[cargo].CargoAvailableViaCount(order->GetConditionViaStationID());
				}

				uint32_t veh_capacity = 0;
				for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
					if (u->cargo_type == cargo) {
						veh_capacity += u->cargo_cap;
					} else if (refit_mode) {
						const Engine *e = Engine::Get(u->engine_type);
						if (!HasBit(e->info.refit_mask, cargo)) {
							continue;
						}

						/* Back up the vehicle's cargo type */
						const CargoType temp_cid = u->cargo_type;
						const uint8_t temp_subtype = u->cargo_subtype;

						const_cast<Vehicle *>(u)->cargo_type = value;
						if (e->refit_capacity_values == nullptr || !(e->callbacks_used & SGCU_REFIT_CB_ALL_CARGOES) || cargo == e->GetDefaultCargoType() || (e->type == VEH_AIRCRAFT && IsCargoInClass(cargo, CargoClass::Passengers))) {
							/* This can be omitted when the refit capacity values are already determined, and the capacity is definitely from the refit callback */
							const_cast<Vehicle *>(u)->cargo_subtype = GetBestFittingSubType(u, const_cast<Vehicle *>(u), cargo);
						}

						veh_capacity += e->DetermineCapacity(u, nullptr); // special mail handling for aircraft is not required here

						/* Restore the original cargo type */
						const_cast<Vehicle *>(u)->cargo_type = temp_cid;
						const_cast<Vehicle *>(u)->cargo_subtype = temp_subtype;
					}
				}
				uint32_t percentage = order->GetXDataLow();
				uint32_t threshold = static_cast<uint32_t>(((uint64_t)veh_capacity * percentage) / 100);

				skip_order = OrderConditionCompare(occ, waiting, threshold);
			}
			break;
		}
		case OCV_CARGO_ACCEPTANCE: {
			StationID next_station = order->GetConditionStationID();
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, Station::Get(next_station)->goods[value].status.Test(GoodsEntry::State::Acceptance), value);
			break;
		}
		case OCV_SLOT_OCCUPANCY: {
			TraceRestrictSlotID slot_id{order->GetXDataLow()};
			TraceRestrictSlot* slot = TraceRestrictSlot::GetIfValid(slot_id);
			if (slot != nullptr) {
				size_t count = slot->occupants.size();
				if (mode == PCO_DEFERRED && _pco_deferred_slot_membership.IsValid()) {
					count += _pco_deferred_slot_membership.GetSlotOccupancyDelta(slot_id);
				}
				bool result;
				if (occ == OCC_EQUALS || occ == OCC_NOT_EQUALS) {
					occ = (occ == OCC_EQUALS) ? OCC_IS_TRUE : OCC_IS_FALSE;
					result = (count == 0);
				} else {
					result = (count >= slot->max_occupancy);
				}
				skip_order = OrderConditionCompare(occ, result, value);
			}
			break;
		}
		case OCV_VEH_IN_SLOT: {
			TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(order->GetXData());
			if (slot != nullptr) {
				bool acquire = false;
				if (occ == OCC_EQUALS || occ == OCC_NOT_EQUALS) {
					acquire = true;
					occ = (occ == OCC_EQUALS) ? OCC_IS_TRUE : OCC_IS_FALSE;
				}
				bool occupant = ExecuteVehicleInSlotOrderCondition(v, slot, mode, acquire);
				skip_order = OrderConditionCompare(occ, occupant, value);
			}
			break;
		}
		case OCV_VEH_IN_SLOT_GROUP: {
			TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(order->GetXData());
			if (sg != nullptr) {
				bool occupant = false;
				if (mode == PCO_EXEC) {
					/* Use vehicle slot membership */
					occupant = TraceRestrictIsVehicleInSlotGroup(sg, v->owner, v);
				} else {
					/* Slow(er) path */
					bool check_owner = (sg->owner != v->owner);
					for (TraceRestrictSlotID slot_id : sg->contained_slots) {
						TraceRestrictSlot *slot = TraceRestrictSlot::Get(slot_id);
						if (check_owner && !slot->flags.Test(TraceRestrictSlot::Flag::Public)) {
							continue;
						}
						if (ExecuteVehicleInSlotOrderCondition(v, slot, mode, false)) {
							occupant = true;
							break;
						}
					}
				}
				skip_order = OrderConditionCompare(occ, occupant, value);
			}
			break;
		}
		case OCV_FREE_PLATFORMS: {
			StationID next_station = order->GetConditionStationID();
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, GetFreeStationPlatforms(next_station), value);
			break;
		}
		case OCV_PERCENT: {
			/* get a non-const reference to the current order */
			Order *ord = const_cast<Order *>(order);
			if (mode == PCO_DEFERRED) {
				_pco_deferred_original_percent_cond.insert({ ord, ord->GetJumpCounter() });
			}
			skip_order = ord->UpdateJumpCounter((uint8_t)value, mode == PCO_DRY_RUN);
			break;
		}
		case OCV_REMAINING_LIFETIME: skip_order = OrderConditionCompare(occ, std::max(DateDeltaToYearDelta(v->max_age - v->age + DAYS_IN_LEAP_YEAR - 1).base(), 0), value); break;
		case OCV_COUNTER_VALUE: {
			const TraceRestrictCounter* ctr = TraceRestrictCounter::GetIfValid(order->GetXDataHigh());
			if (ctr != nullptr) {
				int32_t value = ctr->value;
				if (mode == PCO_DEFERRED) {
					auto iter = _pco_deferred_counter_values.find(ctr->index);
					if (iter != _pco_deferred_counter_values.end()) value = iter->second;
				}
				skip_order = OrderConditionCompare(occ, value, order->GetXDataLow());
			}
			break;
		}
		case OCV_TIME_DATE: {
			skip_order = OrderConditionCompare(occ, GetTraceRestrictTimeDateValue(static_cast<TraceRestrictTimeDateValueField>(value)), order->GetXData());
			break;
		}
		case OCV_TIMETABLE: {
			int tt_value = 0;
			switch (static_cast<OrderTimetableConditionMode>(value)) {
				case OTCM_LATENESS:
					tt_value = v->lateness_counter;
					break;

				case OTCM_EARLINESS:
					tt_value = -v->lateness_counter;
					break;

				default:
					break;
			}
			skip_order = OrderConditionCompare(occ, tt_value, order->GetXData());
			break;
		}
		case OCV_DISPATCH_SLOT: {
			auto get_vehicle_records = [&](uint16_t schedule_index) -> const LastDispatchRecord * {
				return GetVehicleLastDispatchRecord(v, schedule_index);
			};
			skip_order = EvaluateDispatchSlotConditionalOrder(order, v->orders->GetScheduledDispatchScheduleSet(), _state_ticks, get_vehicle_records).GetResult();
			break;
		}
		default: NOT_REACHED();
	}

	return skip_order ? order->GetConditionSkipToOrder() : (VehicleOrderID)INVALID_VEH_ORDER_ID;
}

/* FlushAdvanceOrderIndexDeferred must be called after calling this */
VehicleOrderID AdvanceOrderIndexDeferred(const Vehicle *v, VehicleOrderID index)
{
	const auto num_orders = v->GetNumOrders();
	const uint max_depth = std::min<uint>(32, num_orders);
	uint depth = 0;

	do {
		/* Wrap around. */
		if (index >= num_orders) index = 0;

		const Order *order = v->GetOrder(index);
		assert(order != nullptr);

		switch (order->GetType()) {
			case OT_GOTO_DEPOT:
				if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) {
					break;
				} else {
					return index;
				}

			case OT_SLOT:
				if (TraceRestrictSlot::IsValidID(order->GetDestination().base())) {
					switch (order->GetSlotSubType()) {
						case OSST_RELEASE:
							_pco_deferred_slot_membership.Initialise(v);
							_pco_deferred_slot_membership.RemoveSlot(order->GetDestination().ToSlotID());
							break;
						case OSST_TRY_ACQUIRE:
							ExecuteVehicleInSlotOrderCondition(v, TraceRestrictSlot::Get(order->GetDestination().base()), PCO_DEFERRED, true);
							break;
					}
				}
				break;

			case OT_SLOT_GROUP:
				switch (order->GetSlotGroupSubType()) {
					case OSGST_RELEASE: {
						const TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(order->GetDestination().base());
						if (sg != nullptr) {
							_pco_deferred_slot_membership.Initialise(v);
							bool check_owner = (sg->owner != v->owner);
							for (TraceRestrictSlotID slot_id : sg->contained_slots) {
								if (check_owner && !TraceRestrictSlot::Get(slot_id)->flags.Test(TraceRestrictSlot::Flag::Public)) {
									continue;
								}
								_pco_deferred_slot_membership.RemoveSlot(slot_id);
							}
						}
						break;
					}
				}
				break;

			case OT_COUNTER: {
				const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(order->GetDestination().base());
				if (ctr != nullptr) {
					auto result = _pco_deferred_counter_values.insert(std::make_pair(ctr->index, ctr->value));
					result.first->second = TraceRestrictCounter::ApplyValue(result.first->second, static_cast<TraceRestrictCounterCondOpField>(order->GetCounterOperation()), order->GetXData());
				}
				break;
			}

			case OT_CONDITIONAL: {
				VehicleOrderID next = ProcessConditionalOrder(order, v, PCO_DEFERRED);
				if (next != INVALID_VEH_ORDER_ID) {
					depth++;
					index = next;
					/* Don't increment next, so no break here. */
					continue;
				}
				break;
			}

			case OT_DUMMY:
			case OT_LABEL:
				break;

			default:
				return index;
		}
		/* Don't increment inside the while because otherwise conditional
		 * orders can lead to an infinite loop. */
		++index;
		depth++;
	} while (depth < max_depth);

	/* Wrap around. */
	if (index >= num_orders) index = 0;

	return index;
}

void FlushAdvanceOrderIndexDeferred(const Vehicle *v, bool apply)
{
	if (apply) {
		_pco_deferred_slot_membership.ApplyToVehicle();
		for (auto item : _pco_deferred_counter_values) {
			TraceRestrictCounter::Get(item.first)->UpdateValue(item.second);
		}
	} else {
		for (auto item : _pco_deferred_original_percent_cond) {
			item.first->SetJumpCounter(item.second);
		}
	}

	_pco_deferred_slot_membership.Clear();
	_pco_deferred_counter_values.clear();
	_pco_deferred_original_percent_cond.clear();
}

/**
 * Update the vehicle's destination tile from an order.
 * @param order the order the vehicle currently has
 * @param v the vehicle to update
 * @param conditional_depth the depth (amount of steps) to go with conditional orders. This to prevent infinite loops.
 * @param pbs_look_ahead Whether we are forecasting orders for pbs reservations in advance. If true, the order indices must not be modified.
 */
bool UpdateOrderDest(Vehicle *v, const Order *order, int conditional_depth, bool pbs_look_ahead)
{
	if (conditional_depth > std::min<int>(64, v->GetNumOrders())) {
		v->current_order.Free();
		v->SetDestTile(INVALID_TILE);
		return false;
	}

	switch (order->GetType()) {
		case OT_GOTO_STATION:
			v->SetDestTile(v->GetOrderStationLocation(order->GetDestination().ToStationID()));
			return true;

		case OT_GOTO_DEPOT:
			if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) {
				assert(!pbs_look_ahead);
				UpdateVehicleTimetable(v, true);
				v->IncrementRealOrderIndex();
				break;
			}

			if (v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
				/* If the vehicle can't find its destination, delay its next search.
				 * In case many vehicles are in this state, use the vehicle index to spread out pathfinder calls. */
				if (v->dest_tile == INVALID_TILE && (_state_ticks.base() & 0x3F) != (v->index.base() & 0x3F)) break;

				/* We need to search for the nearest depot (hangar). */
				ClosestDepot closest_depot = v->FindClosestDepot();

				if (closest_depot.found) {
					/* PBS reservations cannot reverse */
					if (pbs_look_ahead && closest_depot.reverse) return false;

					v->SetDestTile(closest_depot.location);
					v->current_order.SetDestination(closest_depot.destination);

					/* If there is no depot in front, reverse automatically (trains only) */
					if (v->type == VEH_TRAIN && closest_depot.reverse) Command<CMD_REVERSE_TRAIN_DIRECTION>::Do(DoCommandFlag::Execute, v->index, false);

					if (v->type == VEH_AIRCRAFT) {
						Aircraft *a = Aircraft::From(v);
						if (a->state == FLYING && a->targetairport != closest_depot.destination) {
							/* The aircraft is now heading for a different hangar than the next in the orders */
							AircraftNextAirportPos_and_Order(a);
						}
					}
					return true;
				}

				/* If there is no depot, we cannot help PBS either. */
				if (pbs_look_ahead) return false;

				UpdateVehicleTimetable(v, true);
				v->IncrementRealOrderIndex();
			} else {
				if (v->type != VEH_AIRCRAFT) {
					v->SetDestTile(Depot::Get(order->GetDestination().ToStationID())->xy);
				} else {
					Aircraft *a = Aircraft::From(v);
					DestinationID destination = a->current_order.GetDestination();
					if (a->targetairport != destination) {
						/* The aircraft is now heading for a different hangar than the next in the orders */
						a->SetDestTile(a->GetOrderStationLocation(destination.ToStationID()));
					}
				}
				return true;
			}
			break;

		case OT_GOTO_WAYPOINT:
			v->SetDestTile(Waypoint::Get(order->GetDestination().ToStationID())->xy);
			return true;

		case OT_CONDITIONAL: {
			assert(!pbs_look_ahead);
			VehicleOrderID next_order = ProcessConditionalOrder(order, v);
			if (next_order != INVALID_VEH_ORDER_ID) {
				/* Jump to next_order. cur_implicit_order_index becomes exactly that order,
				 * cur_real_order_index might come after next_order. */
				UpdateVehicleTimetable(v, false);
				v->cur_implicit_order_index = v->cur_real_order_index = next_order;
				v->UpdateRealOrderIndex();
				v->cur_timetable_order_index = v->GetIndexOfOrder(order);

				/* Disable creation of implicit orders.
				 * When inserting them we do not know that we would have to make the conditional orders point to them. */
				if (v->IsGroundVehicle()) {
					uint16_t &gv_flags = v->GetGroundVehicleFlags();
					SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
				}
			} else {
				v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
				UpdateVehicleTimetable(v, true);
				v->IncrementRealOrderIndex();
			}
			break;
		}

		case OT_SLOT:
			assert(!pbs_look_ahead);
			if (order->GetDestination().base() != INVALID_TRACE_RESTRICT_SLOT_ID) {
				TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(order->GetDestination().base());
				if (slot != nullptr) {
					switch (order->GetSlotSubType()) {
						case OSST_RELEASE:
							slot->Vacate(v);
							break;
						case OSST_TRY_ACQUIRE:
							slot->Occupy(v);
							break;
					}
				}
			}
			UpdateVehicleTimetable(v, true);
			v->IncrementRealOrderIndex();
			break;

		case OT_SLOT_GROUP:
			assert(!pbs_look_ahead);
			if (order->GetDestination().base() != INVALID_TRACE_RESTRICT_SLOT_GROUP) {
				TraceRestrictSlotGroup *sg = TraceRestrictSlotGroup::GetIfValid(order->GetDestination().base());
				if (sg != nullptr) {
					switch (order->GetSlotGroupSubType()) {
						case OSGST_RELEASE:
							TraceRestrictVacateSlotGroup(sg, v->owner, v);
							break;
					}
				}
			}
			UpdateVehicleTimetable(v, true);
			v->IncrementRealOrderIndex();
			break;

		case OT_COUNTER:
			assert(!pbs_look_ahead);
			if (order->GetDestination().base() != INVALID_TRACE_RESTRICT_COUNTER_ID) {
				TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(order->GetDestination().base());
				if (ctr != nullptr) {
					ctr->ApplyUpdate(static_cast<TraceRestrictCounterCondOpField>(order->GetCounterOperation()), order->GetXData());
				}
			}
			UpdateVehicleTimetable(v, true);
			v->IncrementRealOrderIndex();
			break;

		case OT_DUMMY:
		case OT_LABEL:
			assert(!pbs_look_ahead);
			UpdateVehicleTimetable(v, true);
			v->IncrementRealOrderIndex();
			break;

		default:
			v->SetDestTile(INVALID_TILE);
			return false;
	}

	assert(v->cur_implicit_order_index < v->GetNumOrders());
	assert(v->cur_real_order_index < v->GetNumOrders());

	/* Get the current order */
	order = v->GetOrder(v->cur_real_order_index);
	if (order != nullptr && order->IsType(OT_IMPLICIT)) {
		assert(v->GetNumManualOrders() == 0);
		order = nullptr;
	}

	if (order == nullptr) {
		v->current_order.Free();
		v->SetDestTile(INVALID_TILE);
		return false;
	}

	v->current_order = *order;
	return UpdateOrderDest(v, order, conditional_depth + 1, pbs_look_ahead);
}

/**
 * Handle the orders of a vehicle and determine the next place
 * to go to if needed.
 * @param v the vehicle to do this for.
 * @return true *if* the vehicle is eligible for reversing
 *              (basically only when leaving a station).
 */
bool ProcessOrders(Vehicle *v)
{
	switch (v->current_order.GetType()) {
		case OT_GOTO_DEPOT:
			/* Let a depot order in the orderlist interrupt. */
			if (!(v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) return false;
			break;

		case OT_LOADING:
			return false;

		case OT_LOADING_ADVANCE:
			return false;

		case OT_WAITING:
			return false;

		case OT_LEAVESTATION:
			if (v->type != VEH_AIRCRAFT) return false;
			break;

		default: break;
	}

	/**
	 * Reversing because of order change is allowed only just after leaving a
	 * station (and the difficulty setting to allowed, of course)
	 * this can be detected because only after OT_LEAVESTATION, current_order
	 * will be reset to nothing. (That also happens if no order, but in that case
	 * it won't hit the point in code where may_reverse is checked)
	 */
	bool may_reverse = v->current_order.IsType(OT_NOTHING);

	v->vehicle_flags.Reset(VehicleFlag::ConditionalOrderWait);

	/* Check if we've reached a 'via' destination. */
	if (((v->current_order.IsType(OT_GOTO_STATION) && (v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) ||
			(v->current_order.IsType(OT_GOTO_WAYPOINT) && (!v->current_order.IsWaitTimetabled() || v->type != VEH_TRAIN))) &&
			IsTileType(v->tile, MP_STATION) &&
			v->current_order.GetDestination() == GetStationIndex(v->tile)) {
		v->DeleteUnreachedImplicitOrders();
		/* We set the last visited station here because we do not want
		 * the train to stop at this 'via' station if the next order
		 * is a no-non-stop order; in that case not setting the last
		 * visited station will cause the vehicle to still stop. */
		v->last_station_visited = v->current_order.GetDestination().ToStationID();
		UpdateVehicleTimetable(v, true);
		v->IncrementImplicitOrderIndex();
	}

	/* Get the current order */
	assert(v->cur_implicit_order_index == 0 || v->cur_implicit_order_index < v->GetNumOrders());
	v->UpdateRealOrderIndex();

	const Order *order = v->GetOrder(v->cur_real_order_index);
	if (order != nullptr && order->IsType(OT_IMPLICIT)) {
		assert(v->GetNumManualOrders() == 0);
		order = nullptr;
	}

	/* If no order, do nothing. */
	if (order == nullptr || (v->type == VEH_AIRCRAFT && !CheckForValidOrders(v))) {
		if (v->type == VEH_AIRCRAFT) {
			/* Aircraft do something vastly different here, so handle separately */
			HandleMissingAircraftOrders(Aircraft::From(v));
			return false;
		}

		v->current_order.Free();
		v->SetDestTile(INVALID_TILE);
		return false;
	}

	/* If it is unchanged, keep it. */
	if (order->Equals(v->current_order) && (v->type == VEH_AIRCRAFT || v->dest_tile != INVALID_TILE) &&
			(v->type != VEH_SHIP || !order->IsType(OT_GOTO_STATION) || Station::Get(order->GetDestination().ToStationID())->facilities.Test(StationFacility::Dock))) {
		return false;
	}

	/* Otherwise set it, and determine the destination tile. */
	v->current_order = *order;

	InvalidateVehicleOrder(v, VIWD_MODIFY_ORDERS);
	switch (v->type) {
		default:
			NOT_REACHED();

		case VEH_ROAD:
		case VEH_TRAIN:
			break;

		case VEH_AIRCRAFT:
		case VEH_SHIP:
			DirtyVehicleListWindowForVehicle(v);
			break;
	}

	return UpdateOrderDest(v, order) && may_reverse;
}

bool Order::UseOccupancyValueForAverage() const
{
	if (this->GetOccupancy() == 0) return false;
	if (this->GetOccupancy() > 1) return true;

	if (this->IsType(OT_GOTO_STATION)) {
		OrderUnloadFlags unload_type = this->GetUnloadType();
		if ((unload_type == OUFB_TRANSFER || unload_type == OUFB_UNLOAD) && this->GetLoadType() == OLFB_NO_LOAD) return false;
	}

	return true;
}

/**
 * Check whether the given vehicle should stop at the given station
 * based on this order and the non-stop settings.
 * @param last_station_visited the last visited station.
 * @param station the station to stop at.
 * @param waypoint if station is a waypoint.
 * @return true if the vehicle should stop.
 */
bool Order::ShouldStopAtStation(StationID last_station_visited, StationID station, bool waypoint) const
{
	if (waypoint) return this->IsType(OT_GOTO_WAYPOINT) && this->dest == station && this->IsWaitTimetabled();
	if (this->IsType(OT_LOADING_ADVANCE) && this->dest == station) return true;
	bool is_dest_station = this->IsType(OT_GOTO_STATION) && this->dest == station;

	return (!this->IsType(OT_GOTO_DEPOT) || (this->GetDepotOrderType() & ODTFB_PART_OF_ORDERS) != 0) &&
			(last_station_visited != station) && // Do stop only when we've not just been there
			/* Finally do stop when there is no non-stop flag set for this type of station. */
			!(this->GetNonStopType() & (is_dest_station ? ONSF_NO_STOP_AT_DESTINATION_STATION : ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS));
}

/**
 * Check whether the given vehicle should stop at the given station
 * based on this order and the non-stop settings.
 * @param v       the vehicle that might be stopping.
 * @param station the station to stop at.
 * @param waypoint if station is a waypoint.
 * @return true if the vehicle should stop.
 */
bool Order::ShouldStopAtStation(const Vehicle *v, StationID station, bool waypoint) const
{
	return this->ShouldStopAtStation(v->last_station_visited, station, waypoint);
}

/**
 * A vehicle can leave the current station with cargo if:
 * 1. it can load cargo here OR
 * 2a. it could leave the last station with cargo AND
 * 2b. it doesn't have to unload all cargo here.
 */
bool Order::CanLeaveWithCargo(bool has_cargo, CargoType cargo) const
{
	return (this->GetCargoLoadType(cargo) & OLFB_NO_LOAD) == 0 || (has_cargo &&
			(this->GetCargoUnloadType(cargo) & (OUFB_UNLOAD | OUFB_TRANSFER)) == 0);
}

/**
 * Mass change the target of an order.
 * This implemented by adding a new order and if that succeeds deleting the previous one.
 * @param flags operation to perform
 * @param from_dest The destination ID to change from
 * @param vehtype The vehicle type
 * @param order_type The order type
 * @param cargo_filter Cargo filter
 * @param to_dest The destination ID to change to
 * @return the cost of this operation or an error
 */
CommandCost CmdMassChangeOrder(DoCommandFlags flags, DestinationID from_dest, VehicleType vehtype, OrderType order_type, CargoType cargo_filter, DestinationID to_dest)
{
	if (flags.Test(DoCommandFlag::Execute)) {
		for (Vehicle *v : Vehicle::IterateTypeFrontOnly(vehtype)) {
			if (v->IsPrimaryVehicle() && CheckOwnership(v->owner).Succeeded() && VehicleCargoFilter(v, cargo_filter)) {
				uint index = 0;
				for (const Order *order : v->Orders()) {
					if (order->GetDestination() == from_dest && order->IsType(order_type) &&
							!(order_type == OT_GOTO_DEPOT && order->GetDepotActionType() & ODATFB_NEAREST_DEPOT)) {
						Order new_order(*order);
						new_order.SetDestination(to_dest);
						if (CmdInsertOrderIntl(flags, v, index + 1, new_order, {CmdInsertOrderIntlFlag::AllowLoadByCargoType, CmdInsertOrderIntlFlag::AllowDuplicateUnbunch}).Succeeded()) {
							Command<CMD_DELETE_ORDER>::Do(flags, v->index, index);
						}
					}
					index++;
				}
			}
		}
	}
	return CommandCost();
}

void UpdateOrderUIOnDateChange()
{
	SetWindowClassesDirty(WC_VEHICLE_ORDERS);
	SetWindowClassesDirty(WC_VEHICLE_TIMETABLE);
	SetWindowClassesDirty(WC_SCHDISPATCH_SLOTS);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD);
}

const char *GetOrderTypeName(OrderType order_type)
{
	static const char *names[] = {
		"OT_NOTHING",
		"OT_GOTO_STATION",
		"OT_GOTO_DEPOT",
		"OT_LOADING",
		"OT_LEAVESTATION",
		"OT_DUMMY",
		"OT_GOTO_WAYPOINT",
		"OT_CONDITIONAL",
		"OT_IMPLICIT",
		"OT_WAITING",
		"OT_LOADING_ADVANCE",
		"OT_SLOT",
		"OT_COUNTER",
		"OT_LABEL",
		"OT_SLOT_GROUP",
	};
	static_assert(lengthof(names) == OT_END);
	if (order_type < OT_END) return names[order_type];
	return "???";
}

void InsertOrderCmdData::SerialisePayload(BufferSerialisationRef buffer) const
{
	buffer.Send_generic(this->veh);
	buffer.Send_generic(this->sel_ord);
	buffer.Send_generic(this->new_order);
}

bool InsertOrderCmdData::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_generic(this->veh, default_string_validation);
	buffer.Recv_generic(this->sel_ord, default_string_validation);
	buffer.Recv_generic(this->new_order, default_string_validation);

	return true;
}

void InsertOrderCmdData::FormatDebugSummary(format_target &output) const
{
	output.format("{}, {}, order: ", this->veh, this->sel_ord);

	auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
		output.format(Order::CMD_TUPLE_FMT, std::get<Tindices>(this->new_order)...);
	};
	handler(std::make_index_sequence<std::tuple_size_v<decltype(this->new_order)>>{});
}

void BulkOrderCmdData::SerialisePayload(BufferSerialisationRef buffer) const
{
	buffer.Send_generic(this->veh);
	buffer.Send_buffer(this->cmds);
}

bool BulkOrderCmdData::Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation)
{
	buffer.Recv_generic(this->veh, default_string_validation);
	this->cmds = buffer.Recv_buffer();
	if (this->cmds.size() > BULK_ORDER_MAX_CMD_SIZE) return false;
	return true;
}

void BulkOrderCmdData::FormatDebugSummary(format_target &output) const
{
	output.format("{}, {} command bytes", this->veh, this->cmds.size());
}

/**
 * Bulk order operation
 * @param flags Operation to perform.
 * @param cmd_data Command data.
 * @return the cost of this operation or an error
 */
CommandCost CmdBulkOrder(DoCommandFlags flags, const BulkOrderCmdData &cmd_data)
{
	Vehicle *v = Vehicle::GetIfValid(cmd_data.veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) {
		InvalidateWindowData(WC_VEHICLE_ORDER_IMPORT_ERRORS, v->index);

		if (v->orders == nullptr) {
			if (!OrderList::CanAllocateItem()) return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			v->orders = new OrderList(nullptr, v);
		}

		VehicleOrderID insert_pos = INVALID_VEH_ORDER_ID;
		VehicleOrderID modify_pos = INVALID_VEH_ORDER_ID;
		CommandCost last_result = CommandCost();

		uint active_schedule_id = UINT_MAX;
		DispatchSchedule *active_schedule = nullptr;
		uint32_t active_schedule_after_end = 0;
		bool update_active_schedule = false;

		bool dirty_dispatch_windows = false;

		auto flush_active_schedule = [&]() {
			if (update_active_schedule) {
				active_schedule->UpdateScheduledDispatch(nullptr);
				update_active_schedule = false;
				dirty_dispatch_windows = true;
			}
		};

		auto create_error_order = [&]() {
			Order error_order;
			error_order.MakeLabel(OLST_ERROR);
			error_order.SetColour(COLOUR_RED);
			error_order.SetLabelError(OrderLabelError::ParseError);

			if (modify_pos != INVALID_VEH_ORDER_ID) {
				DeleteOrder(v, modify_pos);
				InsertOrder(v, std::move(error_order), modify_pos);
				modify_pos = INVALID_VEH_ORDER_ID;
			} else {
				if (v->GetNumOrders() < MAX_VEH_ORDER_ID) InsertOrder(v, std::move(error_order), insert_pos);
			}
			last_result = CommandCost();
		};

		DeserialisationBuffer buf(cmd_data.cmds.data(), cmd_data.cmds.size());
		while (buf.CanDeserialiseBytes(1, false)) {
			const BulkOrderOp op = static_cast<BulkOrderOp>(buf.Recv_uint8());
			switch (op) {
				case BulkOrderOp::ClearOrders:
					ClearVehicleOrders(v);
					insert_pos = INVALID_VEH_ORDER_ID;
					modify_pos = INVALID_VEH_ORDER_ID;
					break;

				case BulkOrderOp::Insert: {
					Order new_order{};
					buf.Recv_generic_member_ptrs(new_order, Order::GetCmdRefFields());
					if (buf.error) return CMD_ERROR;
					last_result = CmdInsertOrderIntl(flags, v, insert_pos, new_order, {});
					auto result_pos = last_result.GetResultData<VehicleOrderID>();
					if (last_result.Succeeded() && result_pos.has_value()) {
						modify_pos = *result_pos;
						if (insert_pos != INVALID_VEH_ORDER_ID) insert_pos++;
					} else {
						modify_pos = INVALID_VEH_ORDER_ID;
					}
					break;
				}

				case BulkOrderOp::Modify: {
					ModifyOrderFlags mof;
					uint16_t data;
					CargoType cargo_id;
					std::string text;
					buf.Recv_generic_seq({}, mof, data, cargo_id, text);
					if (buf.error) return CMD_ERROR;
					if (modify_pos != INVALID_VEH_ORDER_ID) {
						last_result = CmdModifyOrder(flags, cmd_data.veh, modify_pos, mof, data, cargo_id, text);
					}
					break;
				}

				case BulkOrderOp::Refit: {
					CargoType cargo;
					buf.Recv_generic(cargo, {});
					if (buf.error) return CMD_ERROR;
					if (modify_pos != INVALID_VEH_ORDER_ID) {
						last_result = CmdOrderRefit(flags, cmd_data.veh, modify_pos, cargo);
					}
					break;
				}

				case BulkOrderOp::Timetable: {
					ModifyTimetableFlags mtf;
					uint32_t data;
					ModifyTimetableCtrlFlags ctrl_flags;
					buf.Recv_generic_seq({}, mtf, data, ctrl_flags);
					if (buf.error) return CMD_ERROR;
					if (modify_pos != INVALID_VEH_ORDER_ID) {
						last_result = CmdChangeTimetable(flags, cmd_data.veh, modify_pos, mtf, data, ctrl_flags);
					}
					break;
				}

				case BulkOrderOp::InsertFail:
					modify_pos = INVALID_VEH_ORDER_ID;
					create_error_order();
					break;

				case BulkOrderOp::ReplaceWithFail:
					if (modify_pos != INVALID_VEH_ORDER_ID) {
						create_error_order();
					}
					break;

				case BulkOrderOp::ReplaceOnFail:
					if (last_result.Failed()) {
						create_error_order();
					}
					break;

				case BulkOrderOp::SeekTo:
					buf.Recv_generic_integer(insert_pos);
					modify_pos = insert_pos;
					if (insert_pos > v->GetNumOrders()) insert_pos = INVALID_VEH_ORDER_ID;
					if (modify_pos >= v->GetNumOrders()) modify_pos = INVALID_VEH_ORDER_ID;
					last_result = CommandCost();
					break;

				case BulkOrderOp::Move: {
					VehicleOrderID from;
					VehicleOrderID to;
					uint16_t count;
					buf.Recv_generic_seq({}, from, to, count);
					if (buf.error) return CMD_ERROR;
					if (count == INVALID_VEH_ORDER_ID) count = v->GetNumOrders() - from;
					last_result = CmdMoveOrder(flags, cmd_data.veh, from, to, count);
					insert_pos = INVALID_VEH_ORDER_ID;
					modify_pos = INVALID_VEH_ORDER_ID;
					break;
				}

				case BulkOrderOp::AdjustTravelAfterReverse: {
					VehicleOrderID start;
					uint16_t count;
					buf.Recv_generic_seq({}, start, count);
					if (buf.error) return CMD_ERROR;
					if (count == INVALID_VEH_ORDER_ID) count = v->GetNumOrders() - start;
					if (start < v->GetNumOrders() && start + count <= v->GetNumOrders() && v->GetNumOrders() > 1) {
						std::vector<Order> &orders = v->orders->GetOrderVector();
						AdjustTravelAfterOrderReverse(std::span<Order>(orders).subspan(start, count));
						for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
							InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);
						}
					}
					insert_pos = INVALID_VEH_ORDER_ID;
					modify_pos = INVALID_VEH_ORDER_ID;
					break;
				}

				case BulkOrderOp::SetRouteOverlayColour: {
					Colours colour;
					buf.Recv_generic_integer(colour);
					if (buf.error) return CMD_ERROR;
					CmdSetRouteOverlayColour(flags, cmd_data.veh, colour);
					break;
				}

				/* Scheduled dispatch opcodes follow */

				case BulkOrderOp::ClearSchedules:
					for (Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
						u->dispatch_records.clear();
					}
					if (v->GetNumOrders() == 0) {
						/* No orders, fast path */
						v->orders->GetScheduledDispatchScheduleSet().clear();
						SchdispatchInvalidateWindows(v);
					} else {
						/* Delete schedules individually, perform order updates */
						for (uint i = v->orders->GetScheduledDispatchScheduleCount(); i > 0; i--) {
							CmdSchDispatchRemoveSchedule(flags, cmd_data.veh, i - 1);
						}
					}
					break;

				case BulkOrderOp::AppendSchedule: {
					flush_active_schedule();

					StateTicks start_tick;
					uint32_t duration;
					buf.Recv_generic_seq({}, start_tick, duration);
					if (buf.error) return CMD_ERROR;
					if (CmdSchDispatchAddNewSchedule(flags, cmd_data.veh, start_tick, duration).Succeeded()) {
						active_schedule_id = v->orders->GetScheduledDispatchScheduleCount() - 1;
						active_schedule = &v->orders->GetDispatchScheduleByIndex(active_schedule_id);
						active_schedule_after_end = 0;
					} else {
						active_schedule_id = UINT_MAX;
						active_schedule = nullptr;
					}
					break;
				}

				case BulkOrderOp::SelectSchedule: {
					flush_active_schedule();

					buf.Recv_generic_seq({}, active_schedule_id);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id >= v->orders->GetScheduledDispatchScheduleCount()) {
						active_schedule_id = UINT_MAX;
						active_schedule = nullptr;
					} else {
						active_schedule = &v->orders->GetDispatchScheduleByIndex(active_schedule_id);
						const std::vector<DispatchSlot> &dslist = active_schedule->GetScheduledDispatch();
						active_schedule_after_end = dslist.empty() ? 0 : dslist.back().offset + 1;
					}
					break;
				}

				case BulkOrderOp::SetDispatchEnabled: {
					bool enabled;
					buf.Recv_generic_seq({}, enabled);
					if (buf.error) return CMD_ERROR;
					CmdSchDispatch(flags, cmd_data.veh, enabled);
					break;
				}

				case BulkOrderOp::RenameSchedule: {
					std::string text;
					buf.Recv_generic_seq({}, text);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						CmdSchDispatchRenameSchedule(flags, cmd_data.veh, active_schedule_id, text);
					}
					break;
				}

				case BulkOrderOp::RenameScheduleTag: {
					uint16_t tag_id;
					std::string text;
					buf.Recv_generic_seq({}, tag_id, text);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						CmdSchDispatchRenameTag(flags, cmd_data.veh, active_schedule_id, tag_id, text);
					}
					break;
				}

				case BulkOrderOp::EditScheduleRoute: {
					DispatchSlotRouteID route_id;
					std::string text;
					buf.Recv_generic_seq({}, route_id, text);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						CmdSchDispatchEditRoute(flags, cmd_data.veh, active_schedule_id, route_id, text);
					}
					break;
				}

				case BulkOrderOp::SetScheduleMaxDelay: {
					uint32_t delay;
					buf.Recv_generic_seq({}, delay);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						active_schedule->SetScheduledDispatchDelay(delay);
						dirty_dispatch_windows = true;
					}
					break;
				}

				case BulkOrderOp::SetScheduleReuseSlots: {
					bool reuse;
					buf.Recv_generic_seq({}, reuse);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						active_schedule->SetScheduledDispatchReuseSlots(reuse);
						dirty_dispatch_windows = true;
					}
					break;
				}

				case BulkOrderOp::AddScheduleSlot:
				case BulkOrderOp::AddScheduleSlotWithFlags: {
					uint32_t offset;
					uint16_t flags = 0;
					DispatchSlotRouteID route_id = 0;
					buf.Recv_generic_seq({}, offset);
					if (op == BulkOrderOp::AddScheduleSlotWithFlags) buf.Recv_generic_seq({}, flags, route_id);
					if (buf.error) return CMD_ERROR;
					if (active_schedule_id != UINT_MAX) {
						std::vector<DispatchSlot> &dslist = active_schedule->GetScheduledDispatchMutable();
						if (offset >= active_schedule_after_end) {
							/* Append to end, fast path */
							dslist.push_back({ offset, flags, route_id });
							active_schedule_after_end = offset + 1;
						} else {
							auto insert_position = std::lower_bound(dslist.begin(), dslist.end(), DispatchSlot{ offset, 0 });
							if (insert_position != dslist.end() && insert_position->offset == offset) {
								insert_position->flags = flags;
								insert_position->route_id = route_id;
							} else {
								dslist.insert(insert_position, { offset, flags, route_id });
							}
						}
						update_active_schedule = true;
					}
					break;
				}

				default:
					return CMD_ERROR;
			}
		}

		flush_active_schedule();
		if (dirty_dispatch_windows) {
			SetTimetableWindowsDirty(v, STWDF_SCHEDULED_DISPATCH);
		}

		if (buf.error) return CMD_ERROR;
	}

	return CommandCost();
}
