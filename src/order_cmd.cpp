/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_cmd.cpp Handling of orders. */

#include "stdafx.h"
#include "debug.h"
#include "cmd_helper.h"
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
#include "cheat_type.h"
#include "viewport_func.h"
#include "order_cmd.h"
#include "vehiclelist.h"
#include "tracerestrict.h"
#include "train.h"
#include "date_func.h"

#include "table/strings.h"

#include "3rdparty/robin_hood/robin_hood.h"

#include "safeguards.h"

/* DestinationID must be at least as large as every these below, because it can
 * be any of them
 */
static_assert(sizeof(DestinationID) >= sizeof(DepotID));
static_assert(sizeof(DestinationID) >= sizeof(StationID));

OrderPool _order_pool("Order");
INSTANTIATE_POOL_METHODS(Order)
OrderListPool _orderlist_pool("OrderList");
INSTANTIATE_POOL_METHODS(OrderList)

btree::btree_map<uint32, uint32> _order_destination_refcount_map;
bool _order_destination_refcount_map_valid = false;

CommandCost CmdInsertOrderIntl(DoCommandFlag flags, Vehicle *v, VehicleOrderID sel_ord, const Order &new_order, bool allow_load_by_cargo_type);

void IntialiseOrderDestinationRefcountMap()
{
	ClearOrderDestinationRefcountMap();
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v != v->FirstShared()) continue;
		for(const Order *order : v->Orders()) {
			if (order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT)) {
				_order_destination_refcount_map[OrderDestinationRefcountMapKey(order->GetDestination(), v->owner, order->GetType(), v->type)]++;
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
		_order_destination_refcount_map[OrderDestinationRefcountMapKey(order->GetDestination(), owner, order->GetType(), type)] += delta;
	}
}

/** Clean everything up. */
Order::~Order()
{
	if (CleaningPool()) return;

	/* We can visit oil rigs and buoys that are not our own. They will be shown in
	 * the list of stations. So, we need to invalidate that window if needed. */
	if (this->IsType(OT_GOTO_STATION) || this->IsType(OT_GOTO_WAYPOINT)) {
		BaseStation *bs = BaseStation::GetIfValid(this->GetDestination());
		if (bs != nullptr && bs->owner == OWNER_NONE) InvalidateWindowClassesData(WC_STATION_LIST, 0);
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
	this->next  = nullptr;
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
void Order::MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type, OrderDepotActionFlags action, CargoID cargo)
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
bool Order::UpdateJumpCounter(byte percent, bool dry_run)
{
	const int8 jump_counter = this->GetJumpCounter();
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
	this->type = OT_WAITING;
}

void Order::MakeLoadingAdvance(StationID destination)
{
	this->type = OT_LOADING_ADVANCE;
	this->dest = destination;
}

void Order::MakeReleaseSlot()
{
	this->type = OT_RELEASE_SLOT;
	this->dest = INVALID_TRACE_RESTRICT_SLOT_ID;
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
void Order::SetRefit(CargoID cargo)
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
 * Pack this order into a 64 bits integer, or actually only
 * the type, flags and destination.
 * @return the packed representation.
 * @note unpacking is done in the constructor.
 */
uint64 Order::Pack() const
{
	return ((uint64) this->dest) << 24 | ((uint64) this->flags) << 8 | ((uint64) this->type);
}

/**
 * Pack this order into a 16 bits integer as close to the TTD
 * representation as possible.
 * @return the TTD-like packed representation.
 */
uint16 Order::MapOldOrder() const
{
	uint16 order = this->GetType();
	switch (this->type) {
		case OT_GOTO_STATION:
			if (this->GetUnloadType() & OUFB_UNLOAD) SetBit(order, 5);
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			if (this->GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) SetBit(order, 7);
			order |= GB(this->GetDestination(), 0, 8) << 8;
			break;
		case OT_GOTO_DEPOT:
			if (!(this->GetDepotOrderType() & ODTFB_PART_OF_ORDERS)) SetBit(order, 6);
			SetBit(order, 7);
			order |= GB(this->GetDestination(), 0, 8) << 8;
			break;
		case OT_LOADING:
			if (this->GetLoadType() & OLFB_FULL_LOAD) SetBit(order, 6);
			break;
	}
	return order;
}

/**
 * Create an order based on a packed representation of that order.
 * @param packed the packed representation.
 */
Order::Order(uint64 packed)
{
	this->type    = (OrderType)GB(packed,  0,  8);
	this->flags   = GB(packed,  8,  16);
	this->dest    = GB(packed, 24, 16);
	this->extra   = nullptr;
	this->next    = nullptr;
	this->refit_cargo   = CT_NO_REFIT;
	this->occupancy     = 0;
	this->wait_time     = 0;
	this->travel_time   = 0;
	this->max_speed     = UINT16_MAX;
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

void Order::SetLabelText(const char *text)
{
	assert(this->IsType(OT_LABEL) && this->GetLabelSubType() == OLST_TEXT);
	this->CheckExtraInfoAlloced();
	strecpy((char *)(this->extra->cargo_type_flags), text, (char *)(lastof(this->extra->cargo_type_flags)));
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

	if (other.extra != nullptr && (this->GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD || this->GetLoadType() == OLFB_CARGO_TYPE_LOAD
			|| (this->IsType(OT_LABEL) && this->GetLabelSubType() == OLST_TEXT)
			|| other.extra->xdata != 0 || other.extra->xdata2 != 0 || other.extra->xflags != 0 || other.extra->dispatch_index != 0)) {
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

void CargoStationIDStackSet::FillNextStoppingStation(const Vehicle *v, const OrderList *o, const Order *first, uint hops)
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

void OrderList::ReindexOrderList()
{
	this->order_index.clear();
	for (Order *o = this->first; o != nullptr; o = o->next) {
		this->order_index.push_back(o);
	}
}

bool OrderList::CheckOrderListIndexing() const
{
	uint idx = 0;
	for (Order *o = this->first; o != nullptr; o = o->next, idx++) {
		if (idx >= this->order_index.size()) return false;
		if (this->order_index[idx] != o) return false;
	}
	return idx == this->order_index.size();
}

/**
 * Recomputes everything.
 * @param chain first order in the chain
 * @param v one of vehicle that is using this orderlist
 */
void OrderList::Initialize(Order *chain, Vehicle *v)
{
	this->first = chain;
	this->first_shared = v;

	this->num_manual_orders = 0;
	this->num_vehicles = 1;
	this->timetable_duration = 0;
	this->total_duration = 0;
	this->order_index.clear();

	VehicleType type = v->type;
	Owner owner = v->owner;

	for (Order *o = this->first; o != nullptr; o = o->next) {
		if (!o->IsType(OT_IMPLICIT)) ++this->num_manual_orders;
		if (!o->IsType(OT_CONDITIONAL)) {
			this->timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
			this->total_duration += o->GetWaitTime() + o->GetTravelTime();
		}
		this->order_index.push_back(o);
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
	for (Order *o = this->first; o != nullptr; o = o->next) {
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
	Order *next;
	VehicleType type = this->GetFirstSharedVehicle()->type;
	Owner owner = this->GetFirstSharedVehicle()->owner;
	for (Order *o = this->first; o != nullptr; o = next) {
		UnregisterOrderDestination(o, type, owner);
		next = o->next;
		delete o;
	}

	if (keep_orderlist) {
		this->first = nullptr;
		this->num_manual_orders = 0;
		this->timetable_duration = 0;
		this->order_index.clear();
	} else {
		delete this;
	}
}

/**
 * Get a certain order of the order chain.
 * @param index zero-based index of the order within the chain.
 * @return the order at position index.
 */
Order *OrderList::GetOrderAt(int index) const
{
	if (index < 0 || (uint) index >= this->order_index.size()) return nullptr;
	return this->order_index[index];
}

Order *OrderList::GetOrderAtFromList(int index) const
{
	if (index < 0) return nullptr;

	Order *order = this->first;

	while (order != nullptr && index-- > 0) {
		order = order->next;
	}
	return order;
}

/**
 * Get the index of an order of the order chain, or INVALID_VEH_ORDER_ID.
 * @param order order to get the index of.
 * @return the position index of the given order, or INVALID_VEH_ORDER_ID.
 */
VehicleOrderID OrderList::GetIndexOfOrder(const Order *order) const
{
	for (VehicleOrderID index = 0; index < (VehicleOrderID)this->order_index.size(); index++) {
		if (this->order_index[index] == order) return index;
	}
	return INVALID_VEH_ORDER_ID;
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
		if (next->GetDepotActionType() & ODATFB_HALT) return nullptr;
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
			can_load_or_unload = CargoMaskValueFilter<bool>(cargo_mask, [&](CargoID cargo) {
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
 * @return A CargoMaskedStationIDStack of the cargo mask the result is valid for, and the next stopping station or INVALID_STATION.
 * @pre The vehicle is currently loading and v->last_station_visited is meaningful.
 * @note This function may draw a random number. Don't use it from the GUI.
 */
CargoMaskedStationIDStack OrderList::GetNextStoppingStation(const Vehicle *v, CargoTypes cargo_mask, const Order *first, uint hops) const
{
	static robin_hood::unordered_flat_set<const Order *> seen_conditional_branches;
	if (hops == 0) {
		seen_conditional_branches.clear();
	}

	const Order *next = first;
	if (first == nullptr) {
		next = this->GetOrderAt(v->cur_implicit_order_index);
		if (next == nullptr) {
			next = this->GetFirstOrder();
			if (next == nullptr) return CargoMaskedStationIDStack(cargo_mask, INVALID_STATION);
		} else {
			/* GetNext never returns nullptr if there is a valid station in the list.
			 * As the given "next" is already valid and a station in the list, we
			 * don't have to check for nullptr here. */
			next = this->GetNext(next);
			assert(next != nullptr);
		}
	}

	do {
		next = this->GetNextDecisionNode(next, ++hops, cargo_mask);

		/* Resolve possibly nested conditionals by estimation. */
		while (next != nullptr && next->IsType(OT_CONDITIONAL)) {
			if (!seen_conditional_branches.insert(next).second) {
				/* Already handled this branch */
				return CargoMaskedStationIDStack(cargo_mask, INVALID_STATION);
			}
			/* We return both options of conditional orders. */
			const Order *skip_to = this->GetNextDecisionNode(
					this->GetOrderAt(next->GetConditionSkipToOrder()), hops, cargo_mask);
			const Order *advance = this->GetNextDecisionNode(
					this->GetNext(next), hops, cargo_mask);
			auto seen_target = [&](const Order *target) -> bool {
				return target->IsType(OT_CONDITIONAL) && seen_conditional_branches.contains(target);
			};
			if (advance == nullptr || advance == first || skip_to == advance || seen_target(advance)) {
				next = (skip_to == first) ? nullptr : skip_to;
			} else if (skip_to == nullptr || skip_to == first || seen_target(skip_to)) {
				next = (advance == first) ? nullptr : advance;
			} else {
				CargoMaskedStationIDStack st1 = this->GetNextStoppingStation(v, cargo_mask, skip_to, hops);
				cargo_mask &= st1.cargo_mask;
				CargoMaskedStationIDStack st2 = this->GetNextStoppingStation(v, cargo_mask, advance, hops);
				st1.cargo_mask &= st2.cargo_mask;
				while (!st2.station.IsEmpty()) st1.station.Push(st2.station.Pop());
				return st1;
			}
			++hops;
		}

		if (next == nullptr) return CargoMaskedStationIDStack(cargo_mask, INVALID_STATION);

		/* Don't return a next stop if the vehicle has to unload everything. */
		if ((next->IsType(OT_GOTO_STATION) || next->IsType(OT_IMPLICIT)) &&
				next->GetDestination() == v->last_station_visited && cargo_mask != 0) {
			/* This is a cargo-specific load/unload order.
			 * Don't return a next stop if first cargo has transfer or unload set.
			 * Drop cargoes which don't match the first one. */
			bool invalid = CargoMaskValueFilter<bool>(cargo_mask, [&](CargoID cargo) {
				return ((next->GetCargoUnloadType(cargo) & (OUFB_TRANSFER | OUFB_UNLOAD)) != 0);
			});
			if (invalid) return CargoMaskedStationIDStack(cargo_mask, INVALID_STATION);
		}
	} while (next->IsType(OT_GOTO_DEPOT) || next->IsType(OT_RELEASE_SLOT) || next->IsType(OT_COUNTER) || next->IsType(OT_DUMMY) || next->IsType(OT_LABEL)
			|| (next->IsBaseStationOrder() && next->GetDestination() == v->last_station_visited));

	return CargoMaskedStationIDStack(cargo_mask, next->GetDestination());
}

/**
 * Insert a new order into the order chain.
 * @param new_order is the order to insert into the chain.
 * @param index is the position where the order is supposed to be inserted.
 */
void OrderList::InsertOrderAt(Order *new_order, int index)
{
	if (this->first == nullptr) {
		this->first = new_order;
	} else {
		if (index == 0) {
			/* Insert as first or only order */
			new_order->next = this->first;
			this->first = new_order;
		} else if (index >= this->GetNumOrders()) {
			/* index is after the last order, add it to the end */
			this->GetLastOrder()->next = new_order;
		} else {
			/* Put the new order in between */
			Order *order = this->GetOrderAt(index - 1);
			new_order->next = order->next;
			order->next = new_order;
		}
	}
	if (!new_order->IsType(OT_IMPLICIT)) ++this->num_manual_orders;
	if (!new_order->IsType(OT_CONDITIONAL)) {
		this->timetable_duration += new_order->GetTimetabledWait() + new_order->GetTimetabledTravel();
		this->total_duration += new_order->GetWaitTime() + new_order->GetTravelTime();
	}
	RegisterOrderDestination(new_order, this->GetFirstSharedVehicle()->type, this->GetFirstSharedVehicle()->owner);
	this->ReindexOrderList();

	/* We can visit oil rigs and buoys that are not our own. They will be shown in
	 * the list of stations. So, we need to invalidate that window if needed. */
	if (new_order->IsType(OT_GOTO_STATION) || new_order->IsType(OT_GOTO_WAYPOINT)) {
		BaseStation *bs = BaseStation::Get(new_order->GetDestination());
		if (bs->owner == OWNER_NONE) InvalidateWindowClassesData(WC_STATION_LIST, 0);
	}

}


/**
 * Remove an order from the order list and delete it.
 * @param index is the position of the order which is to be deleted.
 */
void OrderList::DeleteOrderAt(int index)
{
	if (index >= this->GetNumOrders()) return;

	Order *to_remove;

	if (index == 0) {
		to_remove = this->first;
		this->first = to_remove->next;
	} else {
		Order *prev = GetOrderAt(index - 1);
		to_remove = prev->next;
		prev->next = to_remove->next;
	}
	if (!to_remove->IsType(OT_IMPLICIT)) --this->num_manual_orders;
	if (!to_remove->IsType(OT_CONDITIONAL)) {
		this->timetable_duration -= (to_remove->GetTimetabledWait() + to_remove->GetTimetabledTravel());
		this->total_duration -= (to_remove->GetWaitTime() + to_remove->GetTravelTime());
	}
	UnregisterOrderDestination(to_remove, this->GetFirstSharedVehicle()->type, this->GetFirstSharedVehicle()->owner);
	delete to_remove;
	this->ReindexOrderList();
}

/**
 * Move an order to another position within the order list.
 * @param from is the zero-based position of the order to move.
 * @param to is the zero-based position where the order is moved to.
 */
void OrderList::MoveOrder(int from, int to)
{
	if (from >= this->GetNumOrders() || to >= this->GetNumOrders() || from == to) return;

	Order *moving_one;

	/* Take the moving order out of the pointer-chain */
	if (from == 0) {
		moving_one = this->first;
		this->first = moving_one->next;
	} else {
		Order *one_before = GetOrderAtFromList(from - 1);
		moving_one = one_before->next;
		one_before->next = moving_one->next;
	}

	/* Insert the moving_order again in the pointer-chain */
	if (to == 0) {
		moving_one->next = this->first;
		this->first = moving_one;
	} else {
		Order *one_before = GetOrderAtFromList(to - 1);
		moving_one->next = one_before->next;
		one_before->next = moving_one;
	}
	this->ReindexOrderList();
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
	for (VehicleOrderID index = 0; index < (VehicleOrderID)this->order_index.size(); index++) {
		const Order *o = this->order_index[index];
		/* Implicit orders are, by definition, not timetabled. */
		if (o->IsType(OT_IMPLICIT)) continue;
		if (!o->IsCompletelyTimetabled()) return false;
	}
	return true;
}

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

	DEBUG(misc, 6, "Checking OrderList %hu for sanity...", this->index);

	for (const Order *o = this->first; o != nullptr; o = o->next) {
		assert(this->order_index.size() > check_num_orders);
		assert(o == this->order_index[check_num_orders]);
		++check_num_orders;
		if (!o->IsType(OT_IMPLICIT)) ++check_num_manual_orders;
		if (!o->IsType(OT_CONDITIONAL)) {
			check_timetable_duration += o->GetTimetabledWait() + o->GetTimetabledTravel();
			check_total_duration += o->GetWaitTime() + o->GetTravelTime();
		}
	}
	assert_msg(this->GetNumOrders() == check_num_orders, "%u, %u", (uint) this->GetNumOrders(), check_num_orders);
	assert_msg(this->num_manual_orders == check_num_manual_orders, "%u, %u", this->num_manual_orders, check_num_manual_orders);
	assert_msg(this->timetable_duration == check_timetable_duration, "%u, %u", this->timetable_duration, check_timetable_duration);
	assert_msg(this->total_duration == check_total_duration, "%u, %u", this->total_duration, check_total_duration);

	for (const Vehicle *v = this->first_shared; v != nullptr; v = v->NextShared()) {
		++check_num_vehicles;
		assert_msg(v->orders == this, "%p, %p", v->orders, this);
	}
	assert_msg(this->num_vehicles == check_num_vehicles, "%u, %u", this->num_vehicles, check_num_vehicles);
	DEBUG(misc, 6, "... detected %u orders (%u manual), %u vehicles, %i timetabled, %i total",
			(uint)this->GetNumOrders(), (uint)this->num_manual_orders,
			this->num_vehicles, this->timetable_duration, this->total_duration);
	assert(this->CheckOrderListIndexing());
}

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
			(v->type == VEH_AIRCRAFT && o->IsType(OT_GOTO_DEPOT) && !(o->GetDepotActionType() & ODATFB_NEAREST_DEPOT) && o->GetDestination() != INVALID_STATION);
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
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_TOO_FEW_ORDERS);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_VOID_ORDER);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_DUPLICATE_ENTRY);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_HAS_INVALID_ENTRY);
	DeleteVehicleNews(v->index, STR_NEWS_VEHICLE_NO_DEPOT_ORDER);
	DeleteVehicleNews(v->index, STR_NEWS_PLANE_USES_TOO_SHORT_RUNWAY);
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
			if (airport && v->type == VEH_AIRCRAFT) return Station::Get(this->GetDestination())->airport.tile;
			return BaseStation::Get(this->GetDestination())->xy;

		case OT_GOTO_DEPOT:
			if (this->GetDepotActionType() & ODATFB_NEAREST_DEPOT) return INVALID_TILE;
			if (this->GetDestination() == INVALID_DEPOT) return INVALID_TILE;
			return (v->type == VEH_AIRCRAFT) ? Station::Get(this->GetDestination())->xy : Depot::Get(this->GetDestination())->xy;

		default:
			return INVALID_TILE;
	}
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
		int dist2 = GetOrderDistance(prev, cur->next == nullptr ? v->orders->GetFirstOrder() : cur->next, v, conditional_depth);
		return std::max(dist1, dist2);
	}

	TileIndex prev_tile = prev->GetLocation(v, true);
	TileIndex cur_tile = cur->GetLocation(v, true);
	if (prev_tile == INVALID_TILE || cur_tile == INVALID_TILE) return 0;
	return v->type == VEH_AIRCRAFT ? DistanceSquare(prev_tile, cur_tile) : DistanceManhattan(prev_tile, cur_tile);
}

/**
 * Add an order to the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 19) - ID of the vehicle
 * @param p2 various bitstuffed elements
 *  - p2 = (bit 0 - 15) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 * @param p3 packed order to insert
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdInsertOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleID veh          = GB(p1,  0, 20);
	VehicleOrderID sel_ord = GB(p2,  0, 16);
	Order new_order(p3);

	return CmdInsertOrderIntl(flags, Vehicle::GetIfValid(veh), sel_ord, new_order, false);
}

/**
 * Duplicate an order in the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 19) - ID of the vehicle
 * @param p2 various bitstuffed elements
 *  - p2 = (bit 0 - 15) - The order to duplicate
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDuplicateOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_id = GB(p1, 0, 20);
	VehicleOrderID sel_ord = GB(p2, 0, 16);

	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (sel_ord >= v->GetNumOrders()) return CMD_ERROR;

	const Order *src_order = v->GetOrder(sel_ord);
	if (src_order == nullptr) return CMD_ERROR;

	Order new_order;
	new_order.AssignOrder(*src_order);
	const bool wait_fixed = new_order.IsWaitFixed();
	const bool wait_timetabled = wait_fixed && new_order.IsWaitTimetabled();
	new_order.SetWaitTimetabled(false);
	new_order.SetTravelTimetabled(false);
	new_order.SetTravelTime(0);
	new_order.SetTravelFixed(false);
	CommandCost cost = CmdInsertOrderIntl(flags, v, sel_ord + 1, new_order, true);
	if (cost.Failed()) return cost;
	if (flags & DC_EXEC) {
		Order *order = v->orders->GetOrderAt(sel_ord + 1);
		order->SetRefit(new_order.GetRefitCargo());
		order->SetMaxSpeed(new_order.GetMaxSpeed());
		if (wait_fixed) {
			extern void SetOrderFixedWaitTime(Vehicle *v, VehicleOrderID order_number, uint32 wait_time, bool wait_timetabled);
			SetOrderFixedWaitTime(v, sel_ord + 1, new_order.GetWaitTime(), wait_timetabled);
		}
	}
	new_order.Free();
	return CommandCost();
}

CommandCost CmdInsertOrderIntl(DoCommandFlag flags, Vehicle *v, VehicleOrderID sel_ord, const Order &new_order, bool allow_load_by_cargo_type) {
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Check if the inserted order is to the correct destination (owner, type),
	 * and has the correct flags if any */
	switch (new_order.GetType()) {
		case OT_GOTO_STATION: {
			const Station *st = Station::GetIfValid(new_order.GetDestination());
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
				case OLF_LOAD_IF_POSSIBLE: case OLFB_FULL_LOAD: case OLF_FULL_LOAD_ANY: case OLFB_NO_LOAD: break;
				case OLFB_CARGO_TYPE_LOAD:
					if (allow_load_by_cargo_type) break;
					return CMD_ERROR;
				default: return CMD_ERROR;
			}
			switch (new_order.GetUnloadType()) {
				case OUF_UNLOAD_IF_POSSIBLE: case OUFB_UNLOAD: case OUFB_TRANSFER: case OUFB_NO_UNLOAD: break;
				case OUFB_CARGO_TYPE_UNLOAD:
					if (allow_load_by_cargo_type) break;
					return CMD_ERROR;
				default: return CMD_ERROR;
			}

			/* Filter invalid stop locations */
			switch (new_order.GetStopLocation()) {
				case OSL_PLATFORM_NEAR_END:
				case OSL_PLATFORM_MIDDLE:
				case OSL_PLATFORM_THROUGH:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					FALLTHROUGH;

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
					const Station *st = Station::GetIfValid(new_order.GetDestination());

					if (st == nullptr) return CMD_ERROR;

					CommandCost ret = CheckInfraUsageAllowed(v->type, st->owner);
					if (ret.Failed()) return ret;

					if (!CanVehicleUseStation(v, st) || !st->airport.HasHangar()) {
						return CMD_ERROR;
					}
				} else {
					const Depot *dp = Depot::GetIfValid(new_order.GetDestination());

					if (dp == nullptr) return CMD_ERROR;

					CommandCost ret = CheckInfraUsageAllowed(v->type, GetTileOwner(dp->xy), dp->xy);
					if (ret.Failed()) return ret;

					switch (v->type) {
						case VEH_TRAIN:
							if (!IsRailDepotTile(dp->xy)) return CMD_ERROR;
							break;

						case VEH_ROAD:
							if (!IsRoadDepotTile(dp->xy)) return CMD_ERROR;
							if ((GetPresentRoadTypes(dp->xy) & RoadVehicle::From(v)->compatible_roadtypes) == 0) return CMD_ERROR;
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
			if (new_order.GetDepotActionType() & ~(ODATFB_HALT | ODATFB_SELL | ODATFB_NEAREST_DEPOT)) return CMD_ERROR;
			if ((new_order.GetDepotOrderType() & ODTFB_SERVICE) && (new_order.GetDepotActionType() & ODATFB_HALT)) return CMD_ERROR;
			break;
		}

		case OT_GOTO_WAYPOINT: {
			const Waypoint *wp = Waypoint::GetIfValid(new_order.GetDestination());
			if (wp == nullptr) return CMD_ERROR;

			switch (v->type) {
				default: return CMD_ERROR;

				case VEH_TRAIN: {
					if (!(wp->facilities & FACIL_TRAIN)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_RAIL_WAYPOINT);

					CommandCost ret = CheckInfraUsageAllowed(v->type, wp->owner);
					if (ret.Failed()) return ret;
					break;
				}

				case VEH_ROAD: {
					if (!(wp->facilities & FACIL_BUS_STOP) || !(wp->facilities & FACIL_TRUCK_STOP)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_ROAD_WAYPOINT);

					CommandCost ret = CheckInfraUsageAllowed(v->type, wp->owner);
					if (ret.Failed()) return ret;
					break;
				}

				case VEH_SHIP:
					if (!(wp->facilities & FACIL_DOCK)) return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_ADD_ORDER, STR_ERROR_NO_BUOY);
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
			if (skip_to != 0 && skip_to >= v->GetNumOrders()) return CMD_ERROR; // Always allow jumping to the first (even when there is no order).
			if (new_order.GetConditionVariable() >= OCV_END) return CMD_ERROR;

			OrderConditionComparator occ = new_order.GetConditionComparator();
			if (occ >= OCC_END) return CMD_ERROR;
			switch (new_order.GetConditionVariable()) {
				case OCV_SLOT_OCCUPANCY:
				case OCV_VEH_IN_SLOT: {
					TraceRestrictSlotID slot = new_order.GetXData();
					if (slot != INVALID_TRACE_RESTRICT_SLOT_ID) {
						if (!TraceRestrictSlot::IsValidID(slot)) return CMD_ERROR;
						if (new_order.GetConditionVariable() == OCV_VEH_IN_SLOT && TraceRestrictSlot::Get(slot)->vehicle_type != v->type) return CMD_ERROR;
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

				case OCV_CARGO_LOAD_PERCENTAGE:
					if (!CargoSpec::Get(new_order.GetConditionValue())->IsValid()) return CMD_ERROR;
					if (new_order.GetXData() > 100) return CMD_ERROR;
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;

				case OCV_CARGO_WAITING_AMOUNT:
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

				case OCV_PERCENT:
					if (occ != OCC_EQUALS) return CMD_ERROR;
					/* FALL THROUGH */
				case OCV_LOAD_PERCENTAGE:
				case OCV_RELIABILITY:
					if (new_order.GetConditionValue() > 100) return CMD_ERROR;
					FALLTHROUGH;

				default:
					if (occ == OCC_IS_TRUE || occ == OCC_IS_FALSE) return CMD_ERROR;
					break;
			}
			break;
		}

		case OT_RELEASE_SLOT: {
			TraceRestrictSlotID data = new_order.GetDestination();
			if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(data);
				if (slot == nullptr || slot->vehicle_type != v->type) return CMD_ERROR;
			}
			break;
		}

		case OT_COUNTER: {
			TraceRestrictCounterID data = new_order.GetDestination();
			if (data != INVALID_TRACE_RESTRICT_COUNTER_ID) {
				const TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(data);
				if (ctr == nullptr) return CMD_ERROR;
			}
			break;
		}

		case OT_LABEL: {
			switch (new_order.GetLabelSubType()) {
				case OLST_TEXT:
					break;

				case OLST_DEPARTURES_VIA:
				case OLST_DEPARTURES_REMOVE_VIA: {
					const BaseStation *st = BaseStation::GetIfValid(new_order.GetDestination());
					if (st == nullptr) return CMD_ERROR;

					if (st->owner != OWNER_NONE) {
						CommandCost ret = CheckInfraUsageAllowed(v->type, st->owner);
						if (ret.Failed()) return ret;
					}
					break;
				}

				default:
					return CMD_ERROR;
			}
			break;
		}

		default: return CMD_ERROR;
	}

	if (sel_ord > v->GetNumOrders()) return CMD_ERROR;

	if (v->GetNumOrders() >= MAX_VEH_ORDER_ID) return_cmd_error(STR_ERROR_TOO_MANY_ORDERS);
	if (!Order::CanAllocateItem()) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	if (v->orders == nullptr && !OrderList::CanAllocateItem()) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);

	if (flags & DC_EXEC) {
		Order *new_o = new Order();
		new_o->AssignOrder(new_order);
		InsertOrder(v, new_o, sel_ord);
		CheckMarkDirtyViewportRoutePaths(v);
	}

	return CommandCost();
}

/**
 * Insert a new order but skip the validation.
 * @param v       The vehicle to insert the order to.
 * @param new_o   The new order.
 * @param sel_ord The position the order should be inserted at.
 */
void InsertOrder(Vehicle *v, Order *new_o, VehicleOrderID sel_ord)
{
	/* Create new order and link in list */
	if (v->orders == nullptr) {
		v->orders = new OrderList(new_o, v);
	} else {
		v->orders->InsertOrderAt(new_o, sel_ord);
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
			uint16 &gv_flags = u->GetGroundVehicleFlags();
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
		/* Update any possible open window of the vehicle */
		InvalidateVehicleOrder(u, INVALID_VEH_ORDER_ID | (sel_ord << 16));
	}

	/* As we insert an order, the order to skip to will be 'wrong'. */
	VehicleOrderID cur_order_id = 0;
	for (Order *order : v->Orders()) {
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
	InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
}

/**
 * Declone an order-list
 * @param *dst delete the orders of this vehicle
 * @param flags execution flags
 */
static CommandCost DecloneOrder(Vehicle *dst, DoCommandFlag flags)
{
	if (flags & DC_EXEC) {
		/* Clear scheduled dispatch flag if any */
		if (HasBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
			ClrBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH);
		}

		DeleteVehicleOrders(dst);
		InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);
		InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
		InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
		CheckMarkDirtyViewportRoutePaths(dst);
	}
	return CommandCost();
}

/**
 * Get the first cargoID that points to a valid cargo (usually 0)
 */
static CargoID GetFirstValidCargo()
{
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		if (CargoSpec::Get(i)->IsValid()) return i;
	}
	/* No cargos defined -> 'Houston, we have a problem!' */
	NOT_REACHED();
}

/**
 * Delete an order from the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 the order to delete
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdDeleteOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_id = GB(p1, 0, 20);
	VehicleOrderID sel_ord = GB(p2, 0, 16);

	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* If we did not select an order, we maybe want to de-clone the orders */
	if (sel_ord >= v->GetNumOrders()) return DecloneOrder(v, flags);

	if (v->GetOrder(sel_ord) == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		DeleteOrder(v, sel_ord);
		CheckMarkDirtyViewportRoutePaths(v);
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
		SetBit(v->vehicle_flags, VF_LOADING_FINISHED);
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

	InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
}

/**
 * Goto order of order-list.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 The ID of the vehicle which order is skipped
 * @param p2 the selected order to which we want to skip
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSkipToOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_id = GB(p1, 0, 20);
	VehicleOrderID sel_ord = GB(p2, 0, 16);

	Vehicle *v = Vehicle::GetIfValid(veh_id);

	if (v == nullptr || !v->IsPrimaryVehicle() || sel_ord == v->cur_implicit_order_index || sel_ord >= v->GetNumOrders() || v->GetNumOrders() < 2) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (flags & DC_EXEC) {
		if (v->current_order.IsAnyLoadingType()) v->LeaveStation();
		if (v->current_order.IsType(OT_WAITING)) v->HandleWaiting(true);

		if (v->type == VEH_TRAIN) {
			for (Train *u = Train::From(v); u != nullptr; u = u->Next()) {
				ClrBit(u->flags, VRF_BEYOND_PLATFORM_END);
			}
		}

		v->cur_implicit_order_index = v->cur_real_order_index = sel_ord;
		v->UpdateRealOrderIndex();
		v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;

		InvalidateVehicleOrder(v, VIWD_MODIFY_ORDERS);

		v->ClearSeparation();
		if (HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(v->vehicle_flags, VF_TIMETABLE_STARTED);

		/* We have an aircraft/ship, they have a mini-schedule, so update them all */
		if (v->type == VEH_AIRCRAFT || v->type == VEH_SHIP) DirtyVehicleListWindowForVehicle(v);
	}

	return CommandCost();
}

/**
 * Move an order inside the orderlist
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 order to move and target
 *           bit 0-15  : the order to move
 *           bit 16-31 : the target order
 * @param text unused
 * @return the cost of this operation or an error
 * @note The target order will move one place down in the orderlist
 *  if you move the order upwards else it'll move it one place down
 */
CommandCost CmdMoveOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	VehicleOrderID moving_order = GB(p2,  0, 16);
	VehicleOrderID target_order = GB(p2, 16, 16);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	/* Don't make senseless movements */
	if (moving_order >= v->GetNumOrders() || target_order >= v->GetNumOrders() ||
			moving_order == target_order || v->GetNumOrders() <= 1) return CMD_ERROR;

	Order *moving_one = v->GetOrder(moving_order);
	/* Don't move an empty order */
	if (moving_one == nullptr) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->orders->MoveOrder(moving_order, target_order);

		/* Update shared list */
		Vehicle *u = v->FirstShared();

		DeleteOrderWarnings(u);

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
			if (u->cur_real_order_index == moving_order) {
				u->cur_real_order_index = target_order;
			} else if (u->cur_real_order_index > moving_order && u->cur_real_order_index <= target_order) {
				u->cur_real_order_index--;
			} else if (u->cur_real_order_index < moving_order && u->cur_real_order_index >= target_order) {
				u->cur_real_order_index++;
			}

			if (u->cur_implicit_order_index == moving_order) {
				u->cur_implicit_order_index = target_order;
			} else if (u->cur_implicit_order_index > moving_order && u->cur_implicit_order_index <= target_order) {
				u->cur_implicit_order_index--;
			} else if (u->cur_implicit_order_index < moving_order && u->cur_implicit_order_index >= target_order) {
				u->cur_implicit_order_index++;
			}

			u->cur_timetable_order_index = INVALID_VEH_ORDER_ID;

			assert(v->orders == u->orders);
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u, moving_order | (target_order << 16));
		}

		/* As we move an order, the order to skip to will be 'wrong'. */
		for (Order *order : v->Orders()) {
			if (order->IsType(OT_CONDITIONAL)) {
				VehicleOrderID order_id = order->GetConditionSkipToOrder();
				if (order_id == moving_order) {
					order_id = target_order;
				} else if (order_id > moving_order && order_id <= target_order) {
					order_id--;
				} else if (order_id < moving_order && order_id >= target_order) {
					order_id++;
				}
				order->SetConditionSkipToOrder(order_id);
			}
		}

		/* Make sure to rebuild the whole list */
		InvalidateWindowClassesData(GetWindowClassForVehicleType(v->type), 0);
		CheckMarkDirtyViewportRoutePaths(v);
	}

	return CommandCost();
}

/**
 * Reverse an orderlist
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 subcommand
 *        0: reverse whole order list
 *        1: append reversed order list
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdReverseOrderList(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);

	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	uint order_count = v->GetNumOrders();

	switch (p2) {
		case 0: {
			if (order_count < 2) return CMD_ERROR;
			uint max_order = order_count - 1;
			for (uint i = 0; i < max_order; i++) {
				CommandCost cost = DoCommand(tile, p1, max_order | (i << 16), flags, CMD_MOVE_ORDER);
				if (cost.Failed()) return cost;
			}
			break;
		}

		case 1: {
			if (order_count < 3) return CMD_ERROR;
			uint max_order = order_count - 1;
			if (((order_count * 2) - 2) > MAX_VEH_ORDER_ID) return_cmd_error(STR_ERROR_TOO_MANY_ORDERS);
			if (!Order::CanAllocateItem(order_count - 2)) return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			for (uint i = 0; i < order_count; i++) {
				if (v->GetOrder(i)->IsType(OT_CONDITIONAL)) return CMD_ERROR;
			}
			for (uint i = 1; i < max_order; i++) {
				Order new_order;
				new_order.AssignOrder(*v->GetOrder(i));
				const bool wait_fixed = new_order.IsWaitFixed();
				const bool wait_timetabled = wait_fixed && new_order.IsWaitTimetabled();
				new_order.SetWaitTimetabled(false);
				new_order.SetTravelTimetabled(false);
				new_order.SetTravelTime(0);
				new_order.SetTravelFixed(false);
				CommandCost cost = CmdInsertOrderIntl(flags, v, order_count, new_order, true);
				if (cost.Failed()) return cost;
				if (flags & DC_EXEC) {
					Order *order = v->orders->GetOrderAt(order_count);
					order->SetRefit(new_order.GetRefitCargo());
					order->SetMaxSpeed(new_order.GetMaxSpeed());
					if (wait_fixed) {
						extern void SetOrderFixedWaitTime(Vehicle *v, VehicleOrderID order_number, uint32 wait_time, bool wait_timetabled);
						SetOrderFixedWaitTime(v, order_count, new_order.GetWaitTime(), wait_timetabled);
					}
				}
				new_order.Free();
			}
			break;
		};

		default:
			return CMD_ERROR;
	}

	return CommandCost();
}

/**
 * Modify an order in the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0  - 19) - ID of the vehicle
 * @param p2 various bitstuffed elements
 *  - p2 = (bit 0  -  7) - what data to modify (@see ModifyOrderFlags)
 *  - p2 = (bit 8  - 23) - the data to modify
 *  - p2 = (bit 24 - 31) - a CargoID for cargo type orders (MOF_CARGO_TYPE_UNLOAD or MOF_CARGO_TYPE_LOAD)
 * @param p3 various bitstuffed elements
 *  - p3 = (bit 0  - 15) - the selected order (if any). If the last order is given,
 *                         the order will be inserted before that one
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdModifyOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, uint64 p3, const char *text, const CommandAuxiliaryBase *aux_data)
{
	VehicleOrderID sel_ord = GB(p3,  0, 16);
	VehicleID veh          = GB(p1,  0, 20);
	ModifyOrderFlags mof   = Extract<ModifyOrderFlags, 0, 8>(p2);
	uint16 data            = GB(p2,  8, 16);
	CargoID cargo_id       = (mof == MOF_CARGO_TYPE_UNLOAD || mof == MOF_CARGO_TYPE_LOAD) ? (CargoID) GB(p2, 24, 8) : (CargoID) CT_INVALID;

	if (mof >= MOF_END) return CMD_ERROR;

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
				if (mof != MOF_COND_VARIABLE && mof != MOF_COND_COMPARATOR && mof != MOF_COND_VALUE && mof != MOF_COND_VALUE_2 && mof != MOF_COND_VALUE_3 && mof != MOF_COND_DESTINATION && mof != MOF_COND_STATION_ID) return CMD_ERROR;
				break;

			case OT_RELEASE_SLOT:
				if (mof != MOF_SLOT) return CMD_ERROR;
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
			if (data == order->GetNonStopType()) return CMD_ERROR;
			if (_settings_game.order.nonstop_only && !(data & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS) && v->IsGroundVehicle()) return CMD_ERROR;
			break;

		case MOF_STOP_LOCATION:
			if (v->type != VEH_TRAIN) return CMD_ERROR;
			if (data >= OSL_END) return CMD_ERROR;
			break;

		case MOF_CARGO_TYPE_UNLOAD:
			if (cargo_id >= NUM_CARGO && cargo_id != CT_INVALID) return CMD_ERROR;
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
			if (cargo_id >= NUM_CARGO && cargo_id != CT_INVALID) return CMD_ERROR;
			if (data == OLFB_CARGO_TYPE_LOAD || data == OLF_FULL_LOAD_ANY) return CMD_ERROR;
			/* FALL THROUGH */
		case MOF_LOAD:
			if (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) return CMD_ERROR;
			if ((data > OLFB_NO_LOAD && data != OLFB_CARGO_TYPE_LOAD) || data == 1) return CMD_ERROR;
			if (data == order->GetLoadType()) return CMD_ERROR;
			break;

		case MOF_DEPOT_ACTION:
			if (data >= DA_END) return CMD_ERROR;
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

				case OCV_SLOT_OCCUPANCY:
					if (data != INVALID_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(data)) return CMD_ERROR;
					break;

				case OCV_VEH_IN_SLOT:
					if (data != INVALID_TRACE_RESTRICT_SLOT_ID && !TraceRestrictSlot::IsValidID(data)) return CMD_ERROR;
					if (data != INVALID_TRACE_RESTRICT_SLOT_ID && TraceRestrictSlot::Get(data)->vehicle_type != v->type) return CMD_ERROR;
					break;

				case OCV_CARGO_ACCEPTANCE:
				case OCV_CARGO_WAITING:
					if (!(data < NUM_CARGO && CargoSpec::Get(data)->IsValid())) return CMD_ERROR;
					break;

				case OCV_CARGO_WAITING_AMOUNT:
				case OCV_COUNTER_VALUE:
				case OCV_TIME_DATE:
				case OCV_TIMETABLE:
				case OCV_DISPATCH_SLOT:
					break;

				default:
					if (data > 2047) return CMD_ERROR;
					break;
			}
			break;

		case MOF_COND_VALUE_2:
			switch (order->GetConditionVariable()) {
				case OCV_CARGO_LOAD_PERCENTAGE:
				case OCV_CARGO_WAITING_AMOUNT:
					if (!(data < NUM_CARGO && CargoSpec::Get(data)->IsValid())) return CMD_ERROR;
					break;

				case OCV_COUNTER_VALUE:
					if (data != INVALID_TRACE_RESTRICT_COUNTER_ID && !TraceRestrictCounter::IsValidID(data)) return CMD_ERROR;
					break;

				case OCV_TIME_DATE:
					if (data >= TRTDVF_END) return CMD_ERROR;
					break;

				case OCV_TIMETABLE:
					if (data >= OTCM_END) return CMD_ERROR;
					break;

				case OCV_DISPATCH_SLOT:
					if (data >= OSDSCM_END) return CMD_ERROR;
					break;

				default:
					return CMD_ERROR;
			}
			break;

		case MOF_COND_VALUE_3:
			switch (order->GetConditionVariable()) {
				case OCV_CARGO_WAITING_AMOUNT:
					if (!(data == NEW_STATION || Station::GetIfValid(data) != nullptr)) return CMD_ERROR;
					if (GB(order->GetXData2(), 0, 16) - 1 == data) return CMD_ERROR;
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
			if (data != (data & OWF_REVERSE)) return CMD_ERROR;
			break;

		case MOF_SLOT:
			if (data != INVALID_TRACE_RESTRICT_SLOT_ID) {
				const TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(data);
				if (slot == nullptr || slot->vehicle_type != v->type) return CMD_ERROR;
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

	if (flags & DC_EXEC) {
		switch (mof) {
			case MOF_NON_STOP:
				order->SetNonStopType((OrderNonStopFlags)data);
				if (data & ONSF_NO_STOP_AT_DESTINATION_STATION) {
					order->SetRefit(CT_NO_REFIT);
					order->SetLoadType(OLF_LOAD_IF_POSSIBLE);
					order->SetUnloadType(OUF_UNLOAD_IF_POSSIBLE);
					if (order->IsWaitTimetabled() || order->GetWaitTime() > 0) {
						DoCommandEx(tile, v->index | (MTF_WAIT_TIME << 28) | (1 << 31), 0, p3, flags, CMD_CHANGE_TIMETABLE);
					}
					if (order->IsScheduledDispatchOrder(false)) {
						DoCommandEx(tile, v->index | (MTF_ASSIGN_SCHEDULE << 28), -1, p3, flags, CMD_CHANGE_TIMETABLE);
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
				if (cargo_id == CT_INVALID) {
					for (CargoID i = 0; i < NUM_CARGO; i++) {
						order->SetUnloadType((OrderUnloadFlags)data, i);
					}
				} else {
					order->SetUnloadType((OrderUnloadFlags)data, cargo_id);
				}
				break;

			case MOF_LOAD:
				order->SetLoadType((OrderLoadFlags)data);
				if (data & OLFB_NO_LOAD) order->SetRefit(CT_NO_REFIT);
				break;

			case MOF_CARGO_TYPE_LOAD:
				if (cargo_id == CT_INVALID) {
					for (CargoID i = 0; i < NUM_CARGO; i++) {
						order->SetLoadType((OrderLoadFlags)data, i);
					}
				} else {
					order->SetLoadType((OrderLoadFlags)data, cargo_id);
				}
				break;

			case MOF_DEPOT_ACTION: {
				OrderDepotActionFlags base_order_action_type = order->GetDepotActionType() & ~(ODATFB_HALT | ODATFB_SELL);
				switch (data) {
					case DA_ALWAYS_GO:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type));
						break;

					case DA_SERVICE:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() | ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type));
						order->SetRefit(CT_NO_REFIT);
						break;

					case DA_STOP:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type | ODATFB_HALT));
						order->SetRefit(CT_NO_REFIT);
						break;

					case DA_SELL:
						order->SetDepotOrderType((OrderDepotTypeFlags)(order->GetDepotOrderType() & ~ODTFB_SERVICE));
						order->SetDepotActionType((OrderDepotActionFlags)(base_order_action_type | ODATFB_HALT | ODATFB_SELL));
						order->SetRefit(CT_NO_REFIT);
						break;

					default:
						NOT_REACHED();
				}
				break;
			}

			case MOF_COND_VARIABLE: {
				/* Check whether old conditional variable had a cargo as value */
				OrderConditionVariable old_condition = order->GetConditionVariable();
				bool old_var_was_cargo = (order->GetConditionVariable() == OCV_CARGO_ACCEPTANCE || order->GetConditionVariable() == OCV_CARGO_WAITING
						|| order->GetConditionVariable() == OCV_CARGO_LOAD_PERCENTAGE || order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT);
				bool old_var_was_slot = (order->GetConditionVariable() == OCV_SLOT_OCCUPANCY || order->GetConditionVariable() == OCV_VEH_IN_SLOT);
				bool old_var_was_counter = (order->GetConditionVariable() == OCV_COUNTER_VALUE);
				bool old_var_was_time = (order->GetConditionVariable() == OCV_TIME_DATE);
				bool old_var_was_tt = (order->GetConditionVariable() == OCV_TIMETABLE);
				order->SetConditionVariable((OrderConditionVariable)data);

				OrderConditionComparator occ = order->GetConditionComparator();
				switch (order->GetConditionVariable()) {
					case OCV_UNCONDITIONALLY:
						order->SetConditionComparator(OCC_EQUALS);
						order->SetConditionValue(0);
						break;

					case OCV_SLOT_OCCUPANCY:
					case OCV_VEH_IN_SLOT:
						if (!old_var_was_slot) {
							order->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID;
						} else if (order->GetConditionVariable() == OCV_VEH_IN_SLOT && order->GetXData() != INVALID_TRACE_RESTRICT_SLOT_ID && TraceRestrictSlot::Get(order->GetXData())->vehicle_type != v->type) {
							order->GetXDataRef() = INVALID_TRACE_RESTRICT_SLOT_ID;
						}
						if (old_condition != order->GetConditionVariable()) order->SetConditionComparator(OCC_IS_TRUE);
						break;

					case OCV_COUNTER_VALUE:
						if (!old_var_was_counter) order->GetXDataRef() = INVALID_TRACE_RESTRICT_COUNTER_ID << 16;
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
						if (!old_var_was_cargo) order->SetConditionValue((uint16) GetFirstValidCargo());
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						break;
					case OCV_CARGO_LOAD_PERCENTAGE:
					case OCV_CARGO_WAITING_AMOUNT:
						if (!old_var_was_cargo) order->SetConditionValue((uint16) GetFirstValidCargo());
						order->GetXDataRef() = 0;
						order->SetConditionComparator(OCC_EQUALS);
						break;
					case OCV_REQUIRES_SERVICE:
						if (old_var_was_cargo || old_var_was_slot) order->SetConditionValue(0);
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						order->SetConditionValue(0);
						break;
					case OCV_DISPATCH_SLOT:
						if (occ != OCC_IS_TRUE && occ != OCC_IS_FALSE) order->SetConditionComparator(OCC_IS_TRUE);
						order->SetConditionValue(0);
						order->GetXDataRef() = UINT16_MAX;
						break;

					case OCV_PERCENT:
						order->SetConditionComparator(OCC_EQUALS);
						/* FALL THROUGH */
					case OCV_LOAD_PERCENTAGE:
					case OCV_RELIABILITY:
						if (order->GetConditionValue() > 100) order->SetConditionValue(100);
						FALLTHROUGH;

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

					case OCV_CARGO_WAITING_AMOUNT:
					case OCV_COUNTER_VALUE:
					case OCV_DISPATCH_SLOT:
						SB(order->GetXDataRef(), 0, 16, data);
						break;

					default:
						order->SetConditionValue(data);
						break;
				}
				break;

			case MOF_COND_VALUE_2:
				switch (order->GetConditionVariable()) {
					case OCV_COUNTER_VALUE:
						SB(order->GetXDataRef(), 16, 16, data);
						break;

					default:
						order->SetConditionValue(data);
						break;
				}
				break;

			case MOF_COND_VALUE_3:
				SB(order->GetXDataRef(), 16, 16, data + 2);
				break;

			case MOF_COND_STATION_ID:
				SB(order->GetXData2Ref(), 0, 16, data + 1);
				if (order->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT && data == GB(order->GetXData(), 16, 16) - 2) {
					/* Clear via if station is set to the same ID */
					SB(order->GetXDataRef(), 16, 16, 0);
				}
				break;

			case MOF_COND_DESTINATION:
				order->SetConditionSkipToOrder(data);
				break;

			case MOF_WAYPOINT_FLAGS:
				order->SetWaypointFlags((OrderWaypointFlags)data);
				break;

			case MOF_SLOT:
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
				order->SetLabelText(text == nullptr ? "" : text);
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
						if (cargo_id == CT_INVALID) {
							for (CargoID i = 0; i < NUM_CARGO; i++) {
								u->current_order.SetUnloadType((OrderUnloadFlags)data, i);
							}
						} else {
							u->current_order.SetUnloadType((OrderUnloadFlags)data, cargo_id);
						}
						break;

					case MOF_CARGO_TYPE_LOAD:
						if (cargo_id == CT_INVALID) {
							for (CargoID i = 0; i < NUM_CARGO; i++) {
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
			InvalidateVehicleOrder(u, VIWD_MODIFY_ORDERS);
		}
		CheckMarkDirtyViewportRoutePaths(v);
	}

	return CommandCost();
}

/**
 * Check if an aircraft has enough range for an order list.
 * @param v_new Aircraft to check.
 * @param v_order Vehicle currently holding the order list.
 * @param first First order in the source order list.
 * @return True if the aircraft has enough range for the orders, false otherwise.
 */
static bool CheckAircraftOrderDistance(const Aircraft *v_new, const Vehicle *v_order, const Order *first)
{
	if (first == nullptr || v_new->acache.cached_max_range == 0) return true;

	/* Iterate over all orders to check the distance between all
	 * 'goto' orders and their respective next order (of any type). */
	for (const Order *o = first; o != nullptr; o = o->next) {
		switch (o->GetType()) {
			case OT_GOTO_STATION:
			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				/* If we don't have a next order, we've reached the end and must check the first order instead. */
				if (GetOrderDistance(o, o->next != nullptr ? o->next : first, v_order) > v_new->acache.cached_max_range_sqr) return false;
				break;

			default: break;
		}
	}

	return true;
}

static void CheckAdvanceVehicleOrdersAfterClone(Vehicle *v, DoCommandFlag flags)
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
	DoCommand(v->tile, v->index, skip_to, flags, CMD_SKIP_TO_ORDER);
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
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0-19) - destination vehicle to clone orders to
 * - p1 = (bit 30-31) - action to perform
 * @param p2 source vehicle to clone orders from, if any (none for CO_UNSHARE)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdCloneOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh_src = GB(p2, 0, 20);
	VehicleID veh_dst = GB(p1, 0, 20);

	Vehicle *dst = Vehicle::GetIfValid(veh_dst);
	if (dst == nullptr || !dst->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(dst->owner);
	if (ret.Failed()) return ret;

	switch (GB(p1, 30, 2)) {
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
					const Station *st = Station::Get(order->GetDestination());
					if (CanVehicleUseStation(src, st) && !CanVehicleUseStation(dst, st)) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, GetVehicleCannotUseStationReason(dst, st));
					}
				}
				if (OrderGoesToRoadDepot(dst, order)) {
					const Depot *dp = Depot::GetIfValid(order->GetDestination());
					if (dp != nullptr && (GetPresentRoadTypes(dp->xy) & RoadVehicle::From(dst)->compatible_roadtypes) == 0) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, RoadTypeIsTram(RoadVehicle::From(dst)->roadtype) ? STR_ERROR_NO_STOP_COMPATIBLE_TRAM_TYPE : STR_ERROR_NO_STOP_COMPATIBLE_ROAD_TYPE);
					}
				}
			}

			/* Check for aircraft range limits. */
			if (dst->type == VEH_AIRCRAFT && !CheckAircraftOrderDistance(Aircraft::From(dst), src, src->GetFirstOrder())) {
				return_cmd_error(STR_ERROR_AIRCRAFT_NOT_ENOUGH_RANGE);
			}

			if (src->orders == nullptr && !OrderList::CanAllocateItem()) {
				return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			}

			if (flags & DC_EXEC) {
				/* If the destination vehicle had a OrderList, destroy it.
				 * We reset the order indices, if the new orders are different.
				 * (We mainly do this to keep the order indices valid and in range.) */
				DeleteVehicleOrders(dst, false, ShouldResetOrderIndicesOnOrderCopy(src, dst));

				dst->orders = src->orders;

				/* Link this vehicle in the shared-list */
				dst->AddToShared(src);


				/* Set automation bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
					SetBit(dst->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				} else {
					ClrBit(dst->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				}
				/* Set auto separation bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
					SetBit(dst->vehicle_flags, VF_TIMETABLE_SEPARATION);
				} else {
					ClrBit(dst->vehicle_flags, VF_TIMETABLE_SEPARATION);
				}
				/* Set manual dispatch bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
					SetBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH);
				} else {
					ClrBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH);
				}
				ClrBit(dst->vehicle_flags, VF_AUTOFILL_TIMETABLE);
				ClrBit(dst->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);

				dst->ClearSeparation();
				if (HasBit(dst->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(dst->vehicle_flags, VF_TIMETABLE_STARTED);

				InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);
				InvalidateVehicleOrder(src, VIWD_MODIFY_ORDERS);


				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
				InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
				CheckMarkDirtyViewportRoutePaths(dst);

				CheckAdvanceVehicleOrdersAfterClone(dst, flags);
			}
			break;
		}

		case CO_COPY: {
			Vehicle *src = Vehicle::GetIfValid(veh_src);

			/* Sanity checks */
			if (src == nullptr || !src->IsPrimaryVehicle() || dst->type != src->type || dst == src) return CMD_ERROR;

			ret = CheckOwnership(src->owner);
			if (ret.Failed()) return ret;

			/* Trucks can't copy all the orders from busses (and visa versa),
			 * and neither can helicopters and aircraft. */
			for (const Order *order : src->Orders()) {
				if (OrderGoesToStation(dst, order)) {
					const Station *st = Station::Get(order->GetDestination());
					if (!CanVehicleUseStation(dst, st)) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, GetVehicleCannotUseStationReason(dst, st));
					}
				}
				if (OrderGoesToRoadDepot(dst, order)) {
					const Depot *dp = Depot::GetIfValid(order->GetDestination());
					if (dp != nullptr && (GetPresentRoadTypes(dp->xy) & RoadVehicle::From(dst)->compatible_roadtypes) == 0) {
						return CommandCost::DualErrorMessage(STR_ERROR_CAN_T_COPY_SHARE_ORDER, RoadTypeIsTram(RoadVehicle::From(dst)->roadtype) ? STR_ERROR_NO_STOP_COMPATIBLE_TRAM_TYPE : STR_ERROR_NO_STOP_COMPATIBLE_ROAD_TYPE);
					}
				}
			}

			/* Check for aircraft range limits. */
			if (dst->type == VEH_AIRCRAFT && !CheckAircraftOrderDistance(Aircraft::From(dst), src, src->GetFirstOrder())) {
				return_cmd_error(STR_ERROR_AIRCRAFT_NOT_ENOUGH_RANGE);
			}

			/* make sure there are orders available */
			if (!Order::CanAllocateItem(src->GetNumOrders()) || !OrderList::CanAllocateItem()) {
				return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
			}

			if (flags & DC_EXEC) {
				Order *first = nullptr;
				Order **order_dst;

				/* If the destination vehicle had an order list, destroy the chain but keep the OrderList.
				 * We only the order indices, if the new orders are different.
				 * (We mainly do this to keep the order indices valid and in range.) */
				DeleteVehicleOrders(dst, true, ShouldResetOrderIndicesOnOrderCopy(src, dst));

				order_dst = &first;
				for (const Order *order : src->Orders()) {
					*order_dst = new Order();
					(*order_dst)->AssignOrder(*order);
					order_dst = &(*order_dst)->next;
				}
				if (dst->orders == nullptr) {
					dst->orders = new OrderList(first, dst);
				} else {
					assert(dst->orders->GetFirstOrder() == nullptr);
					assert(!dst->orders->IsShared());
					delete dst->orders;
					assert(OrderList::CanAllocateItem());
					dst->orders = new OrderList(first, dst);
				}

				/* Copy over scheduled dispatch data */
				assert(dst->orders != nullptr);
				if (src->orders != nullptr) {
					dst->orders->GetScheduledDispatchScheduleSet() = src->orders->GetScheduledDispatchScheduleSet();
				}

				/* Set automation bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_AUTOMATE_TIMETABLE)) {
					SetBit(dst->vehicle_flags, VF_AUTOMATE_TIMETABLE);
					ClrBit(dst->vehicle_flags, VF_AUTOFILL_TIMETABLE);
					ClrBit(dst->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME);
				} else {
					ClrBit(dst->vehicle_flags, VF_AUTOMATE_TIMETABLE);
				}
				/* Set auto separation bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_TIMETABLE_SEPARATION)) {
					SetBit(dst->vehicle_flags, VF_TIMETABLE_SEPARATION);
				} else {
					ClrBit(dst->vehicle_flags, VF_TIMETABLE_SEPARATION);
				}
				/* Set manual dispatch bit if target has it. */
				if (HasBit(src->vehicle_flags, VF_SCHEDULED_DISPATCH)) {
					SetBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH);
				} else {
					ClrBit(dst->vehicle_flags, VF_SCHEDULED_DISPATCH);
				}

				InvalidateVehicleOrder(dst, VIWD_REMOVE_ALL_ORDERS);

				InvalidateWindowClassesData(GetWindowClassForVehicleType(dst->type), 0);
				InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
				CheckMarkDirtyViewportRoutePaths(dst);

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
 * Add/remove refit orders from an order
 * @param tile Not used
 * @param flags operation to perform
 * @param p1 VehicleIndex of the vehicle having the order
 * @param p2 bitmask
 *   - bit 0-7 CargoID
 *   - bit 16-31 number of order to modify
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdOrderRefit(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	VehicleID veh = GB(p1, 0, 20);
	VehicleOrderID order_number  = GB(p2, 16, 16);
	CargoID cargo = GB(p2, 0, 8);

	if (cargo >= NUM_CARGO && cargo != CT_NO_REFIT && cargo != CT_AUTO_REFIT) return CMD_ERROR;

	const Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	Order *order = v->GetOrder(order_number);
	if (order == nullptr) return CMD_ERROR;

	/* Automatic refit cargo is only supported for goto station orders. */
	if (cargo == CT_AUTO_REFIT && !order->IsType(OT_GOTO_STATION)) return CMD_ERROR;

	if (order->GetLoadType() & OLFB_NO_LOAD) return CMD_ERROR;

	if (flags & DC_EXEC) {
		order->SetRefit(cargo);

		/* Make the depot order an 'always go' order. */
		if (cargo != CT_NO_REFIT && order->IsType(OT_GOTO_DEPOT)) {
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
		CheckMarkDirtyViewportRoutePaths(v);
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
	if (v->vehstatus & VS_CRASHED) return;

	/* Do nothing for stopped vehicles if setting is '1' */
	if (_settings_client.gui.order_review_system == 1 && (v->vehstatus & VS_STOPPED)) return;

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
				const Station *st = Station::Get(order->GetDestination());

				n_st++;
				if (!CanVehicleUseStation(v, st)) {
					message = STR_NEWS_VEHICLE_HAS_INVALID_ENTRY;
				} else if (v->type == VEH_AIRCRAFT &&
							(AircraftVehInfo(v->engine_type)->subtype & AIR_FAST) &&
							(st->airport.GetFTA()->flags & AirportFTAClass::SHORT_STRIP) &&
							!_cheats.no_jetcrash.value &&
							message == INVALID_STRING_ID) {
					message = STR_NEWS_PLANE_USES_TOO_SHORT_RUNWAY;
				}
			}
			if (order->IsType(OT_GOTO_DEPOT)) {
				has_depot_order = true;
			}
		}

		/* Check if the last and the first order are the same */
		if (v->GetNumOrders() > 1) {
			const Order *last = v->GetLastOrder();

			if (v->orders->GetFirstOrder()->Equals(*last)) {
				message = STR_NEWS_VEHICLE_HAS_DUPLICATE_ENTRY;
			}
		}

		/* Do we only have 1 station in our order list? */
		if (n_st < 2 && message == INVALID_STRING_ID) message = STR_NEWS_VEHICLE_HAS_TOO_FEW_ORDERS;

#ifndef NDEBUG
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

		SetDParam(0, v->index);
		AddVehicleAdviceNewsItem(message, v->index);
	}
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
	/* Aircraft have StationIDs for depot orders and never use DepotIDs
	 * This fact is handled specially below
	 */

	/* Go through all vehicles */
	for (Vehicle *v : Vehicle::Iterate()) {
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
				if (type == OT_GOTO_STATION && o->GetConditionVariable() == OCV_CARGO_WAITING_AMOUNT) {
					if (GB(order->GetXData(), 16, 16) - 2 == destination) SB(order->GetXDataRef(), 16, 16, INVALID_STATION + 2);
				}
				if (type == OT_GOTO_STATION && ConditionVariableHasStationID(o->GetConditionVariable())) {
					if (GB(order->GetXData2(), 0, 16) - 1 == destination) SB(order->GetXData2Ref(), 0, 16, INVALID_STATION + 1);
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

	OrderBackup::RemoveOrder(type, destination, hangar);
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
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);

	if (v->IsOrderListShared()) {
		/* Remove ourself from the shared order list. */
		v->RemoveFromShared();
		v->orders = nullptr;
	} else {
		CloseWindowById(GetWindowClassForVehicleType(v->type), VehicleListIdentifier(VL_SHARED_ORDERS, v->type, v->owner, v->index).Pack());
		if (v->orders != nullptr) {
			/* Remove the orders */
			v->orders->FreeChain(keep_orderlist);
			if (!keep_orderlist) v->orders = nullptr;
		}
	}

	if (reset_order_indices) {
		v->cur_implicit_order_index = v->cur_real_order_index = 0;
		v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		if (v->current_order.IsAnyLoadingType()) {
			CancelLoadingDueToDeletedOrder(v);
		}
	}
}

/**
 * Clamp the service interval to the correct min/max. The actual min/max values
 * depend on whether it's in percent or days.
 * @param interval proposed service interval
 * @return Clamped service interval
 */
uint16 GetServiceIntervalClamped(uint interval, bool ispercent)
{
	return ispercent ? Clamp(interval, MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT) : Clamp(interval, MIN_SERVINT_DAYS, MAX_SERVINT_DAYS);
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
			case OT_GOTO_DEPOT:
			case OT_GOTO_WAYPOINT:
				return true;

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
static uint16 GetFreeStationPlatforms(StationID st_id)
{
	assert(Station::IsValidID(st_id));
	const Station *st = Station::Get(st_id);
	if (!(st->facilities & FACIL_TRAIN)) return 0;
	bool is_free;
	TileIndex t2;
	uint16 counter = 0;
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

bool EvaluateDispatchSlotConditionalOrder(const Order *order, const Vehicle *v, DateTicksScaled date_time, bool *predicted)
{
	uint schedule_index = GB(order->GetXData(), 0, 16);
	if (schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return false;
	const DispatchSchedule &sched = v->orders->GetDispatchScheduleByIndex(schedule_index);
	if (sched.GetScheduledDispatch().size() == 0) return false;

	if (predicted != nullptr) *predicted = true;

	int32 offset;
	if (order->GetConditionValue() & 2) {
		int32 last = sched.GetScheduledDispatchLastDispatch();
		if (last < 0) {
			last += sched.GetScheduledDispatchDuration() * (1 + (-last / sched.GetScheduledDispatchDuration()));
		}
		offset = last % sched.GetScheduledDispatchDuration();
	} else {
		extern DateTicksScaled GetScheduledDispatchTime(const DispatchSchedule &ds, DateTicksScaled leave_time);
		DateTicksScaled slot = GetScheduledDispatchTime(sched, _scaled_date_ticks);
		offset = (slot - sched.GetScheduledDispatchStartTick()) % sched.GetScheduledDispatchDuration();
	}

	bool value;
	if (order->GetConditionValue() & 1) {
		value = (offset == (int)sched.GetScheduledDispatch().back());
	} else {
		value = (offset == (int)sched.GetScheduledDispatch().front());
	}

	return OrderConditionCompare(order->GetConditionComparator(), value, 0);
}

static std::vector<TraceRestrictSlotID> _pco_deferred_slot_acquires;
static std::vector<TraceRestrictSlotID> _pco_deferred_slot_releases;
static btree::btree_map<TraceRestrictCounterID, int32> _pco_deferred_counter_values;
static btree::btree_map<Order *, int8> _pco_deferred_original_percent_cond;

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
	uint16 value = order->GetConditionValue();

	// OrderConditionCompare ignores the last parameter for occ == OCC_IS_TRUE or occ == OCC_IS_FALSE.
	switch (order->GetConditionVariable()) {
		case OCV_LOAD_PERCENTAGE:    skip_order = OrderConditionCompare(occ, CalcPercentVehicleFilled(v, nullptr), value); break;
		case OCV_CARGO_LOAD_PERCENTAGE: skip_order = OrderConditionCompare(occ, CalcPercentVehicleFilledOfCargo(v, (CargoType) value), order->GetXData()); break;
		case OCV_RELIABILITY:        skip_order = OrderConditionCompare(occ, ToPercent16(v->reliability),       value); break;
		case OCV_MAX_RELIABILITY:    skip_order = OrderConditionCompare(occ, ToPercent16(v->GetEngine()->reliability),   value); break;
		case OCV_MAX_SPEED:          skip_order = OrderConditionCompare(occ, v->GetDisplayMaxSpeed() * 10 / 16, value); break;
		case OCV_AGE:                skip_order = OrderConditionCompare(occ, DateToYear(v->age),                value); break;
		case OCV_REQUIRES_SERVICE:   skip_order = OrderConditionCompare(occ, v->NeedsServicing(),               value); break;
		case OCV_UNCONDITIONALLY:    skip_order = true; break;
		case OCV_CARGO_WAITING: {
			StationID next_station = GB(order->GetXData2(), 0, 16) - 1;
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, (Station::Get(next_station)->goods[value].CargoAvailableCount() > 0), value);
			break;
		}
		case OCV_CARGO_WAITING_AMOUNT: {
			StationID next_station = GB(order->GetXData2(), 0, 16) - 1;
			if (Station::IsValidID(next_station)) {
				if (GB(order->GetXData(), 16, 16) == 0) {
					skip_order = OrderConditionCompare(occ, Station::Get(next_station)->goods[value].CargoAvailableCount(), GB(order->GetXData(), 0, 16));
				} else {
					skip_order = OrderConditionCompare(occ, Station::Get(next_station)->goods[value].CargoAvailableViaCount(GB(order->GetXData(), 16, 16) - 2), GB(order->GetXData(), 0, 16));
				}
			}
			break;
		}
		case OCV_CARGO_ACCEPTANCE: {
			StationID next_station = GB(order->GetXData2(), 0, 16) - 1;
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, HasBit(Station::Get(next_station)->goods[value].status, GoodsEntry::GES_ACCEPTANCE), value);
			break;
		}
		case OCV_SLOT_OCCUPANCY: {
			TraceRestrictSlotID slot_id = order->GetXData();
			TraceRestrictSlot* slot = TraceRestrictSlot::GetIfValid(slot_id);
			if (slot != nullptr) {
				size_t count = slot->occupants.size();
				if (mode == PCO_DEFERRED) {
					if (find_index(_pco_deferred_slot_releases, slot_id) >= 0 && slot->IsOccupant(v->index)) {
						count--;
					} else if (find_index(_pco_deferred_slot_acquires, slot_id) >= 0 && !slot->IsOccupant(v->index)) {
						count++;
					}
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
			TraceRestrictSlotID slot_id = order->GetXData();
			TraceRestrictSlot* slot = TraceRestrictSlot::GetIfValid(slot_id);
			if (slot != nullptr) {
				bool occupant = slot->IsOccupant(v->index);
				if (mode == PCO_DEFERRED) {
					if (occupant && find_index(_pco_deferred_slot_releases, slot_id) >= 0) {
						occupant = false;
					} else if (!occupant && find_index(_pco_deferred_slot_acquires, slot_id) >= 0) {
						occupant = true;
					}
				}
				if (occ == OCC_EQUALS || occ == OCC_NOT_EQUALS) {
					if (!occupant && mode == PCO_EXEC) {
						occupant = slot->Occupy(v->index);
					}
					if (!occupant && mode == PCO_DEFERRED) {
						occupant = slot->OccupyDryRun(v->index);
						if (occupant) {
							include(_pco_deferred_slot_acquires, slot_id);
							container_unordered_remove(_pco_deferred_slot_releases, slot_id);
						}
					}
					occ = (occ == OCC_EQUALS) ? OCC_IS_TRUE : OCC_IS_FALSE;
				}
				skip_order = OrderConditionCompare(occ, occupant, value);
			}
			break;
		}
		case OCV_FREE_PLATFORMS: {
			StationID next_station = GB(order->GetXData2(), 0, 16) - 1;
			if (Station::IsValidID(next_station)) skip_order = OrderConditionCompare(occ, GetFreeStationPlatforms(next_station), value);
			break;
		}
		case OCV_PERCENT: {
			/* get a non-const reference to the current order */
			Order *ord = const_cast<Order *>(order);
			if (mode == PCO_DEFERRED) {
				_pco_deferred_original_percent_cond.insert({ ord, ord->GetJumpCounter() });
			}
			skip_order = ord->UpdateJumpCounter((byte)value, mode == PCO_DRY_RUN);
			break;
		}
		case OCV_REMAINING_LIFETIME: skip_order = OrderConditionCompare(occ, std::max(DateToYear(v->max_age - v->age + DAYS_IN_LEAP_YEAR - 1), 0), value); break;
		case OCV_COUNTER_VALUE: {
			const TraceRestrictCounter* ctr = TraceRestrictCounter::GetIfValid(GB(order->GetXData(), 16, 16));
			if (ctr != nullptr) {
				int32 value = ctr->value;
				if (mode == PCO_DEFERRED) {
					auto iter = _pco_deferred_counter_values.find(ctr->index);
					if (iter != _pco_deferred_counter_values.end()) value = iter->second;
				}
				skip_order = OrderConditionCompare(occ, value, GB(order->GetXData(), 0, 16));
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
			skip_order = EvaluateDispatchSlotConditionalOrder(order, v, _scaled_date_ticks, nullptr);
			break;
		}
		default: NOT_REACHED();
	}

	return skip_order ? order->GetConditionSkipToOrder() : (VehicleOrderID)INVALID_VEH_ORDER_ID;
}

/* FlushAdvanceOrderIndexDeferred must be called after calling this */
VehicleOrderID AdvanceOrderIndexDeferred(const Vehicle *v, VehicleOrderID index)
{
	int depth = 0;
	++index;

	do {
		/* Wrap around. */
		if (index >= v->GetNumOrders()) index = 0;

		Order *order = v->GetOrder(index);
		assert(order != nullptr);

		switch (order->GetType()) {
			case OT_GOTO_DEPOT:
				if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) {
					break;
				} else {
					return index;
				}

			case OT_RELEASE_SLOT:
				if (TraceRestrictSlot::IsValidID(order->GetDestination())) {
					include(_pco_deferred_slot_releases, order->GetDestination());
					container_unordered_remove(_pco_deferred_slot_acquires, order->GetDestination());
				}
				break;

			case OT_COUNTER: {
				const TraceRestrictCounter* ctr = TraceRestrictCounter::GetIfValid(order->GetDestination());
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
	} while (depth < v->GetNumOrders());

	/* Wrap around. */
	if (index >= v->GetNumOrders()) index = 0;

	return index;
}

void FlushAdvanceOrderIndexDeferred(const Vehicle *v, bool apply)
{
	if (apply) {
		for (TraceRestrictSlotID slot : _pco_deferred_slot_acquires) {
			TraceRestrictSlot::Get(slot)->Occupy(v->index);
		}
		for (TraceRestrictSlotID slot : _pco_deferred_slot_releases) {
			TraceRestrictSlot::Get(slot)->Vacate(v->index);
		}
		for (auto item : _pco_deferred_counter_values) {
			TraceRestrictCounter::Get(item.first)->UpdateValue(item.second);
		}
	} else {
		for (auto item : _pco_deferred_original_percent_cond) {
			item.first->SetJumpCounter(item.second);
		}
	}

	_pco_deferred_slot_acquires.clear();
	_pco_deferred_slot_releases.clear();
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
		v->SetDestTile(0);
		return false;
	}

	switch (order->GetType()) {
		case OT_GOTO_STATION:
			v->SetDestTile(v->GetOrderStationLocation(order->GetDestination()));
			return true;

		case OT_GOTO_DEPOT:
			if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) {
				assert(!pbs_look_ahead);
				UpdateVehicleTimetable(v, true);
				v->IncrementRealOrderIndex();
				break;
			}

			if (v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
				/* We need to search for the nearest depot (hangar). */
				ClosestDepot closestDepot = v->FindClosestDepot();

				if (closestDepot.found) {
					/* PBS reservations cannot reverse */
					if (pbs_look_ahead && closestDepot.reverse) return false;

					v->SetDestTile(closestDepot.location);
					v->current_order.SetDestination(closestDepot.destination);

					/* If there is no depot in front, reverse automatically (trains only) */
					if (v->type == VEH_TRAIN && closestDepot.reverse) DoCommand(v->tile, v->index, 0, DC_EXEC, CMD_REVERSE_TRAIN_DIRECTION);

					if (v->type == VEH_AIRCRAFT) {
						Aircraft *a = Aircraft::From(v);
						if (a->state == FLYING && a->targetairport != closestDepot.destination) {
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
					v->SetDestTile(Depot::Get(order->GetDestination())->xy);
				} else {
					Aircraft *a = Aircraft::From(v);
					DestinationID destination = a->current_order.GetDestination();
					if (a->targetairport != destination) {
						/* The aircraft is now heading for a different hangar than the next in the orders */
						a->SetDestTile(a->GetOrderStationLocation(destination));
					}
				}
				return true;
			}
			break;

		case OT_GOTO_WAYPOINT:
			v->SetDestTile(Waypoint::Get(order->GetDestination())->xy);
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
					uint16 &gv_flags = v->GetGroundVehicleFlags();
					SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
				}
			} else {
				v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
				UpdateVehicleTimetable(v, true);
				v->IncrementRealOrderIndex();
			}
			break;
		}

		case OT_RELEASE_SLOT:
			assert(!pbs_look_ahead);
			if (order->GetDestination() != INVALID_TRACE_RESTRICT_SLOT_ID) {
				TraceRestrictSlot *slot = TraceRestrictSlot::GetIfValid(order->GetDestination());
				if (slot != nullptr) slot->Vacate(v->index);
			}
			UpdateVehicleTimetable(v, true);
			v->IncrementRealOrderIndex();
			break;

		case OT_COUNTER:
			assert(!pbs_look_ahead);
			if (order->GetDestination() != INVALID_TRACE_RESTRICT_COUNTER_ID) {
				TraceRestrictCounter *ctr = TraceRestrictCounter::GetIfValid(order->GetDestination());
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
			v->SetDestTile(0);
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
		v->SetDestTile(0);
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

	ClrBit(v->vehicle_flags, VF_COND_ORDER_WAIT);

	/* Check if we've reached a 'via' destination. */
	if (((v->current_order.IsType(OT_GOTO_STATION) && (v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) ||
			(v->current_order.IsType(OT_GOTO_WAYPOINT) && !v->current_order.IsWaitTimetabled())) &&
			IsTileType(v->tile, MP_STATION) &&
			v->current_order.GetDestination() == GetStationIndex(v->tile)) {
		v->DeleteUnreachedImplicitOrders();
		/* We set the last visited station here because we do not want
		 * the train to stop at this 'via' station if the next order
		 * is a no-non-stop order; in that case not setting the last
		 * visited station will cause the vehicle to still stop. */
		v->last_station_visited = v->current_order.GetDestination();
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
		v->SetDestTile(0);
		return false;
	}

	/* If it is unchanged, keep it. */
	if (order->Equals(v->current_order) && (v->type == VEH_AIRCRAFT || v->dest_tile != 0) &&
			(v->type != VEH_SHIP || !order->IsType(OT_GOTO_STATION) || Station::Get(order->GetDestination())->HasFacilities(FACIL_DOCK))) {
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
bool Order::CanLeaveWithCargo(bool has_cargo, CargoID cargo) const
{
	return (this->GetCargoLoadType(cargo) & OLFB_NO_LOAD) == 0 || (has_cargo &&
			(this->GetCargoUnloadType(cargo) & (OUFB_UNLOAD | OUFB_TRANSFER)) == 0);
}

/**
 * Mass change the target of an order.
 * This implemented by adding a new order and if that succeeds deleting the previous one.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - The destination ID to change from
 * - p1 = (bit 16 - 18) - The vehicle type
 * - p1 = (bit 20 - 23) - The order type
 * - p1 = (bit 24 - 31) - Cargo filter
 * @param p2 various bitstuffed elements
  * - p2 = (bit  0 - 15) - The destination ID to change to
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMassChangeOrder(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	DestinationID from_dest = GB(p1, 0, 16);
	VehicleType vehtype = Extract<VehicleType, 16, 3>(p1);
	OrderType order_type = (OrderType) GB(p1, 20, 4);
	CargoID cargo_filter = GB(p1, 24, 8);
	DestinationID to_dest = GB(p2, 0, 16);

	if (flags & DC_EXEC) {
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->type == vehtype && v->IsPrimaryVehicle() && CheckOwnership(v->owner).Succeeded() && VehicleCargoFilter(v, cargo_filter)) {
				int index = 0;
				bool changed = false;

				for (Order *order : v->Orders()) {
					if (order->GetDestination() == from_dest && order->IsType(order_type) &&
							!(order_type == OT_GOTO_DEPOT && order->GetDepotActionType() & ODATFB_NEAREST_DEPOT)) {
						Order new_order;
						new_order.AssignOrder(*order);
						new_order.SetDestination(to_dest);
						const bool wait_fixed = new_order.IsWaitFixed();
						const bool wait_timetabled = wait_fixed && new_order.IsWaitTimetabled();
						new_order.SetWaitTimetabled(false);
						new_order.SetTravelTimetabled(false);
						if (CmdInsertOrderIntl(flags, v, index + 1, new_order, true).Succeeded()) {
							DoCommand(0, v->index, index, flags, CMD_DELETE_ORDER);

							order = v->orders->GetOrderAt(index);
							order->SetRefit(new_order.GetRefitCargo());
							order->SetMaxSpeed(new_order.GetMaxSpeed());
							if (wait_fixed) {
								extern void SetOrderFixedWaitTime(Vehicle *v, VehicleOrderID order_number, uint32 wait_time, bool wait_timetabled);
								SetOrderFixedWaitTime(v, index, new_order.GetWaitTime(), wait_timetabled);
							}
							changed = true;
						}

						new_order.Free();
					}
					index++;
				}
				if (changed) CheckMarkDirtyViewportRoutePaths(v);
			}
		}
	}
	return CommandCost();
}

void ShiftOrderDates(int interval)
{
	for (OrderList *orderlist : OrderList::Iterate()) {
		for (DispatchSchedule &ds : orderlist->GetScheduledDispatchScheduleSet()) {
			if (ds.GetScheduledDispatchStartDatePart() >= 0) {
				ds.SetScheduledDispatchStartDate(ds.GetScheduledDispatchStartDatePart() + interval, ds.GetScheduledDispatchStartDateFractPart());
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_ORDERS);
	SetWindowClassesDirty(WC_VEHICLE_TIMETABLE);
	SetWindowClassesDirty(WC_SCHDISPATCH_SLOTS);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
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
		"OT_RELEASE_SLOT",
		"OT_COUNTER",
		"OT_LABEL",
	};
	static_assert(lengthof(names) == OT_END);
	if (order_type < OT_END) return names[order_type];
	return "???";
}
