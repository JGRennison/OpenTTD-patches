/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_base.h Base class for orders. */

#ifndef ORDER_BASE_H
#define ORDER_BASE_H

#include "order_type.h"
#include "core/pool_type.hpp"
#include "core/bitmath_func.hpp"
#include "cargo_type.h"
#include "depot_type.h"
#include "station_type.h"
#include "vehicle_type.h"
#include "date_type.h"
#include "schdispatch.h"

#include <memory>
#include <vector>

typedef Pool<Order, OrderID, 256, 64000> OrderPool;
typedef Pool<OrderList, OrderListID, 128, 64000> OrderListPool;
extern OrderPool _order_pool;
extern OrderListPool _orderlist_pool;

struct OrderExtraInfo {
	uint8 cargo_type_flags[NUM_CARGO] = {}; ///< Load/unload types for each cargo type.
	uint32 xdata = 0;                       ///< Extra arbitrary data
	uint8 xflags = 0;                       ///< Extra flags
};

/* If you change this, keep in mind that it is saved on 3 places:
 * - Load_ORDR, all the global orders
 * - Vehicle -> current_order
 * - REF_ORDER (all REFs are currently limited to 16 bits!!)
 */
struct Order : OrderPool::PoolItem<&_order_pool> {
private:
	friend const struct SaveLoad *GetVehicleDescription(VehicleType vt); ///< Saving and loading the current order of vehicles.
	friend void Load_VEHS();                                             ///< Loading of ancient vehicles.
	friend const struct SaveLoad *GetOrderDescription();                 ///< Saving and loading of orders.
	friend void Load_ORDX();                                             ///< Saving and loading of orders.
	friend void Save_ORDX();                                             ///< Saving and loading of orders.
	friend void Load_VEOX();                                             ///< Saving and loading of orders.
	friend void Save_VEOX();                                             ///< Saving and loading of orders.

	uint8 type;           ///< The type of order + non-stop flags
	uint8 flags;          ///< Load/unload types, depot order/action types.
	DestinationID dest;   ///< The destination of the order.

	CargoID refit_cargo;  ///< Refit CargoID

	uint8 occupancy;     ///< Estimate of vehicle occupancy on departure, for the current order, 0 indicates invalid, 1 - 101 indicate 0 - 100%
	int8 jump_counter;   ///< Counter for the 'jump xx% of times' option

	std::unique_ptr<OrderExtraInfo> extra; ///< Extra order info

	uint16 wait_time;    ///< How long in ticks to wait at the destination.
	uint16 travel_time;  ///< How long in ticks the journey to this destination should take.
	uint16 max_speed;    ///< How fast the vehicle may go on the way to the destination.

	void AllocExtraInfo();
	void DeAllocExtraInfo();

	inline void CheckExtraInfoAlloced()
	{
		if (!this->extra) this->AllocExtraInfo();
	}

	inline uint8 GetXFlags() const
	{
		return this->extra != nullptr ? this->extra->xflags : 0;
	}

	inline uint8 &GetXFlagsRef()
	{
		CheckExtraInfoAlloced();
		return this->extra->xflags;
	}

public:
	inline uint32 GetXData() const
	{
		return this->extra != nullptr ? this->extra->xdata : 0;
	}

	inline uint32 &GetXDataRef()
	{
		CheckExtraInfoAlloced();
		return this->extra->xdata;
	}

	Order *next;          ///< Pointer to next order. If NULL, end of list

	Order() : refit_cargo(CT_NO_REFIT), max_speed(UINT16_MAX) {}
	~Order();

	Order(uint32 packed);

	Order(const Order& other)
	{
		*this = other;
	}

	Order(Order&& other) = default;

	inline Order& operator=(Order const& other)
	{
		AssignOrder(other);
		this->next = other.next;
		this->index = other.index;
		return *this;
	}

	/**
	 * Check whether this order is of the given type.
	 * @param type the type to check against.
	 * @return true if the order matches.
	 */
	inline bool IsType(OrderType type) const { return this->GetType() == type; }

	/**
	 * Check whether this order is either of OT_LOADING or OT_LOADING_ADVANCE.
	 * @return true if the order matches.
	 */
	inline bool IsAnyLoadingType() const { return this->GetType() == OT_LOADING || this->GetType() == OT_LOADING_ADVANCE; }

	/**
	 * Get the type of order of this order.
	 * @return the order type.
	 */
	inline OrderType GetType() const { return (OrderType)GB(this->type, 0, 4); }

	void Free();

	void MakeGoToStation(StationID destination);
	void MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type = ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, OrderDepotActionFlags action = ODATF_SERVICE_ONLY, CargoID cargo = CT_NO_REFIT);
	void MakeGoToWaypoint(StationID destination);
	void MakeLoading(bool ordered);
	void MakeLeaveStation();
	void MakeDummy();
	void MakeConditional(VehicleOrderID order);
	void MakeImplicit(StationID destination);
	void MakeWaiting();
	void MakeLoadingAdvance(StationID destination);

	/**
	 * Is this a 'goto' order with a real destination?
	 * @return True if the type is either #OT_GOTO_WAYPOINT, #OT_GOTO_DEPOT or #OT_GOTO_STATION.
	 */
	inline bool IsGotoOrder() const
	{
		return IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION);
	}

	/**
	 * Gets the destination of this order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 * @return the destination of the order.
	 */
	inline DestinationID GetDestination() const { return this->dest; }

	/**
	 * Sets the destination of this order.
	 * @param destination the new destination of the order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION).
	 */
	inline void SetDestination(DestinationID destination) { this->dest = destination; }

	/**
	 * Is this order a refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a refit should happen.
	 */
	inline bool IsRefit() const { return this->refit_cargo < NUM_CARGO || this->refit_cargo == CT_AUTO_REFIT; }

	/**
	 * Is this order a auto-refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a auto-refit should happen.
	 */
	inline bool IsAutoRefit() const { return this->refit_cargo == CT_AUTO_REFIT; }

	/**
	 * Get the cargo to to refit to.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return the cargo type.
	 */
	inline CargoID GetRefitCargo() const { return this->refit_cargo; }

	void SetRefit(CargoID cargo);

	/**
	 * Update the jump_counter of this order.
	 * @param the jump chance in %.
	 * @return whether to jump or not.
	 * @pre IsType(OT_CONDITIONAL) && this->GetConditionVariable() == OCV_PERCENT.
	 */
	bool UpdateJumpCounter(uint8 percent);

	/** How must the consist be loaded? */
	inline OrderLoadFlags GetLoadType() const
	{
		OrderLoadFlags type = (OrderLoadFlags)GB(this->flags, 4, 3);
		if (type == OLFB_CARGO_TYPE_LOAD_ENCODING) type = OLFB_CARGO_TYPE_LOAD;
		return type;
	}

	/**
	 * How must the consist be loaded for this type of cargo?
	 * @pre GetLoadType() == OLFB_CARGO_TYPE_LOAD
	 * @param cargo_id The cargo type index.
	 * @return The load type for this cargo.
	 */
	inline OrderLoadFlags GetCargoLoadTypeRaw(CargoID cargo_id) const
	{
		assert(cargo_id < NUM_CARGO);
		if (!this->extra) return OLF_LOAD_IF_POSSIBLE;
		return (OrderLoadFlags) GB(this->extra->cargo_type_flags[cargo_id], 4, 4);
	}

	/**
	 * How must the consist be loaded for this type of cargo?
	 * @param cargo_id The cargo type index.
	 * @return The load type for this cargo.
	 */
	inline OrderLoadFlags GetCargoLoadType(CargoID cargo_id) const
	{
		assert(cargo_id < NUM_CARGO);
		OrderLoadFlags olf = this->GetLoadType();
		if (olf == OLFB_CARGO_TYPE_LOAD) olf = this->GetCargoLoadTypeRaw(cargo_id);
		return olf;
	}

	/** How must the consist be unloaded? */
	inline OrderUnloadFlags GetUnloadType() const
	{
		OrderUnloadFlags type = (OrderUnloadFlags)GB(this->flags, 0, 3);
		if (type == OUFB_CARGO_TYPE_UNLOAD_ENCODING) type = OUFB_CARGO_TYPE_UNLOAD;
		return type;
	}

	/**
	 * How must the consist be unloaded for this type of cargo?
	 * @pre GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD
	 * @param cargo_id The cargo type index.
	 * @return The unload type for this cargo.
	 */
	inline OrderUnloadFlags GetCargoUnloadTypeRaw(CargoID cargo_id) const
	{
		assert(cargo_id < NUM_CARGO);
		if (!this->extra) return OUF_UNLOAD_IF_POSSIBLE;
		return (OrderUnloadFlags) GB(this->extra->cargo_type_flags[cargo_id], 0, 4);
	}

	/**
	 * How must the consist be unloaded for this type of cargo?
	 * @param cargo_id The cargo type index.
	 * @return The unload type for this cargo.
	 */
	inline OrderUnloadFlags GetCargoUnloadType(CargoID cargo_id) const
	{
		assert(cargo_id < NUM_CARGO);
		OrderUnloadFlags ouf = this->GetUnloadType();
		if (ouf == OUFB_CARGO_TYPE_UNLOAD) ouf = this->GetCargoUnloadTypeRaw(cargo_id);
		return ouf;
	}

	template <typename F> CargoTypes FilterLoadUnloadTypeCargoMask(F filter_func, CargoTypes cargo_mask = ALL_CARGOTYPES)
	{
		if ((this->GetLoadType() == OLFB_CARGO_TYPE_LOAD) || (this->GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD)) {
			CargoID cargo;
			CargoTypes output_mask = cargo_mask;
			FOR_EACH_SET_BIT(cargo, cargo_mask) {
				if (!filter_func(this, cargo)) ClrBit(output_mask, cargo);
			}
			return output_mask;
		} else {
			return filter_func(this, FindFirstBit(cargo_mask)) ? cargo_mask : 0;
		}
	}

	/** At which stations must we stop? */
	inline OrderNonStopFlags GetNonStopType() const { return (OrderNonStopFlags)GB(this->type, 6, 2); }
	/** Where must we stop at the platform? */
	inline OrderStopLocation GetStopLocation() const { return (OrderStopLocation)GB(this->type, 4, 2); }
	/** What caused us going to the depot? */
	inline OrderDepotTypeFlags GetDepotOrderType() const { return (OrderDepotTypeFlags)GB(this->flags, 0, 3); }
	/** What are we going to do when in the depot. */
	inline OrderDepotActionFlags GetDepotActionType() const { return (OrderDepotActionFlags)GB(this->flags, 4, 3); }
	/** What waypoint flags? */
	inline OrderWaypointFlags GetWaypointFlags() const { return (OrderWaypointFlags)GB(this->flags, 0, 8); }
	/** What variable do we have to compare? */
	inline OrderConditionVariable GetConditionVariable() const { return (OrderConditionVariable)GB(this->dest, 11, 5); }
	/** What is the comparator to use? */
	inline OrderConditionComparator GetConditionComparator() const { return (OrderConditionComparator)GB(this->type, 5, 3); }
	/** Get the order to skip to. */
	inline VehicleOrderID GetConditionSkipToOrder() const { return this->flags; }
	/** Get the value to base the skip on. */
	inline uint16 GetConditionValue() const { return GB(this->dest, 0, 11); }

	/** Set how the consist must be loaded. */
	inline void SetLoadType(OrderLoadFlags load_type)
	{
		if (load_type == OLFB_CARGO_TYPE_LOAD) load_type = OLFB_CARGO_TYPE_LOAD_ENCODING;
		SB(this->flags, 4, 3, load_type);
	}

	/**
	 * Set how the consist must be loaded for this type of cargo.
	 * @pre GetLoadType() == OLFB_CARGO_TYPE_LOAD
	 * @param load_type The load type.
	 * @param cargo_id The cargo type index.
	 */
	inline void SetLoadType(OrderLoadFlags load_type, CargoID cargo_id)
	{
		assert(cargo_id < NUM_CARGO);
		this->CheckExtraInfoAlloced();
		SB(this->extra->cargo_type_flags[cargo_id], 4, 4, load_type);
	}

	/** Set how the consist must be unloaded. */
	inline void SetUnloadType(OrderUnloadFlags unload_type)
	{
		if (unload_type == OUFB_CARGO_TYPE_UNLOAD) unload_type = OUFB_CARGO_TYPE_UNLOAD_ENCODING;
		SB(this->flags, 0, 3, unload_type);
	}

	/**
	 * Set how the consist must be unloaded for this type of cargo.
	 * @pre GetUnloadType() == OUFB_CARGO_TYPE_UNLOAD
	 * @param unload_type The unload type.
	 * @param cargo_id The cargo type index.
	 */
	inline void SetUnloadType(OrderUnloadFlags unload_type, CargoID cargo_id)
	{
		assert(cargo_id < NUM_CARGO);
		this->CheckExtraInfoAlloced();
		SB(this->extra->cargo_type_flags[cargo_id], 0, 4, unload_type);
	}

	/** Set whether we must stop at stations or not. */
	inline void SetNonStopType(OrderNonStopFlags non_stop_type) { SB(this->type, 6, 2, non_stop_type); }
	/** Set where we must stop at the platform. */
	inline void SetStopLocation(OrderStopLocation stop_location) { SB(this->type, 4, 2, stop_location); }
	/** Set the cause to go to the depot. */
	inline void SetDepotOrderType(OrderDepotTypeFlags depot_order_type) { SB(this->flags, 0, 3, depot_order_type); }
	/** Set what we are going to do in the depot. */
	inline void SetDepotActionType(OrderDepotActionFlags depot_service_type) { SB(this->flags, 4, 3, depot_service_type); }
	/** Set waypoint flags. */
	inline void SetWaypointFlags(OrderWaypointFlags waypoint_flags) { SB(this->flags, 0, 8, waypoint_flags); }
	/** Set variable we have to compare. */
	inline void SetConditionVariable(OrderConditionVariable condition_variable) { SB(this->dest, 11, 5, condition_variable); }
	/** Set the comparator to use. */
	inline void SetConditionComparator(OrderConditionComparator condition_comparator) { SB(this->type, 5, 3, condition_comparator); }
	/** Get the order to skip to. */
	inline void SetConditionSkipToOrder(VehicleOrderID order_id) { this->flags = order_id; }
	/** Set the value to base the skip on. */
	inline void SetConditionValue(uint16 value) { SB(this->dest, 0, 11, value); }

	/* As conditional orders write their "skip to" order all over the flags, we cannot check the
	 * flags to find out if timetabling is enabled. However, as conditional orders are never
	 * autofilled we can be sure that any non-zero values for their wait_time and travel_time are
	 * explicitly set (but travel_time is actually unused for conditionals). */

	/** Does this order have an explicit wait time set? */
	inline bool IsWaitTimetabled() const { return this->IsType(OT_CONDITIONAL) ? HasBit(this->GetXFlags(), 0) : HasBit(this->flags, 3); }
	/** Does this order have an explicit travel time set? */
	inline bool IsTravelTimetabled() const { return this->IsType(OT_CONDITIONAL) ? this->travel_time > 0 : HasBit(this->flags, 7); }

	/** Get the time in ticks a vehicle should wait at the destination or 0 if it's not timetabled. */
	inline uint16 GetTimetabledWait() const { return this->IsWaitTimetabled() ? this->wait_time : 0; }
	/** Get the time in ticks a vehicle should take to reach the destination or 0 if it's not timetabled. */
	inline uint16 GetTimetabledTravel() const { return this->IsTravelTimetabled() ? this->travel_time : 0; }
	/** Get the time in ticks a vehicle will probably wait at the destination (timetabled or not). */
	inline uint16 GetWaitTime() const { return this->wait_time; }
	/** Get the time in ticks a vehicle will probably take to reach the destination (timetabled or not). */
	inline uint16 GetTravelTime() const { return this->travel_time; }

	/**
	 * Get the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @return maximum speed.
	 */
	inline uint16 GetMaxSpeed() const { return this->max_speed; }

	/** Set if the wait time is explicitly timetabled (unless the order is conditional). */
	inline void SetWaitTimetabled(bool timetabled)
	{
		if (this->IsType(OT_CONDITIONAL)) {
			SB(this->GetXFlagsRef(), 0, 1, timetabled ? 1 : 0);
		} else {
			SB(this->flags, 3, 1, timetabled ? 1 : 0);
		}
	}

	/** Set if the travel time is explicitly timetabled (unless the order is conditional). */
	inline void SetTravelTimetabled(bool timetabled) { if (!this->IsType(OT_CONDITIONAL)) SB(this->flags, 7, 1, timetabled ? 1 : 0); }

	/**
	 * Set the time in ticks to wait at the destination.
	 * @param time Time to set as wait time.
	 */
	inline void SetWaitTime(uint16 time) { this->wait_time = time;  }

	/**
	 * Set the time in ticks to take for travelling to the destination.
	 * @param time Time to set as travel time.
	 */
	inline void SetTravelTime(uint16 time) { this->travel_time = time; }

	/**
	 * Set the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @param speed Speed to be set.
	 */
	inline void SetMaxSpeed(uint16 speed) { this->max_speed = speed; }

	/** Does this order have a fixed wait time? */
	inline bool IsWaitFixed() const { return HasBit(this->GetXFlags(), 1); }

	/** Set if  the wait time is fixed */
	inline void SetWaitFixed(bool fixed)
	{
		if (!this->IsType(OT_CONDITIONAL) && fixed != IsWaitFixed()) SB(this->GetXFlagsRef(), 1, 1, fixed ? 1 : 0);
	}

	/**
	 * Get the occupancy value
	 * @return occupancy
	 */
	inline uint8 GetOccupancy() const { return this->occupancy; }

	/**
	 * Set the occupancy value
	 * @param occupancy The occupancy to set
	 */
	inline void SetOccupancy(uint8 occupancy) { this->occupancy = occupancy; }

	bool ShouldStopAtStation(const Vehicle *v, StationID station) const;
	bool CanLeaveWithCargo(bool has_cargo, CargoID cargo) const;

	TileIndex GetLocation(const Vehicle *v, bool airport = false) const;

	/** Checks if travel_time and wait_time apply to this order and if they are timetabled. */
	inline bool IsCompletelyTimetabled() const
	{
		if (!this->IsTravelTimetabled() && !this->IsType(OT_CONDITIONAL)) return false;
		if (!this->IsWaitTimetabled() && this->IsType(OT_GOTO_STATION) &&
				!(this->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
			return false;
		}
		return true;
	}

	void AssignOrder(const Order &other);
	bool Equals(const Order &other) const;

	uint32 Pack() const;
	uint16 MapOldOrder() const;
	void ConvertFromOldSavegame();
};

void InsertOrder(Vehicle *v, Order *new_o, VehicleOrderID sel_ord);
void DeleteOrder(Vehicle *v, VehicleOrderID sel_ord);

struct CargoMaskedStationIDStack {
	CargoTypes cargo_mask;
	StationIDStack station;

	CargoMaskedStationIDStack(CargoTypes cargo_mask, StationIDStack station)
			: cargo_mask(cargo_mask), station(station) {}
};

struct CargoStationIDStackSet {
private:
	CargoMaskedStationIDStack first;
	std::vector<CargoMaskedStationIDStack> more;

public:
	CargoStationIDStackSet()
			: first(ALL_CARGOTYPES, INVALID_STATION) {}

	const StationIDStack& Get(CargoID cargo) const
	{
		if (HasBit(first.cargo_mask, cargo)) return first.station;
		for (size_t i = 0; i < more.size(); i++) {
			if (HasBit(more[i].cargo_mask, cargo)) return more[i].station;
		}
		NOT_REACHED();
	}

	void FillNextStoppingStation(const Vehicle *v, const OrderList *o, const Order *first = NULL, uint hops = 0);
};

template <typename F> CargoTypes FilterCargoMask(F filter_func, CargoTypes cargo_mask = ALL_CARGOTYPES)
{
	CargoID cargo;
	CargoTypes output_mask = cargo_mask;
	FOR_EACH_SET_BIT(cargo, cargo_mask) {
		if (!filter_func(cargo)) ClrBit(output_mask, cargo);
	}
	return output_mask;
}

template <typename T, typename F> T CargoMaskValueFilter(CargoTypes &cargo_mask, F filter_func)
{
	CargoID first_cargo_id = FindFirstBit(cargo_mask);
	T value = filter_func(first_cargo_id);
	CargoTypes other_cargo_mask = cargo_mask;
	ClrBit(other_cargo_mask, first_cargo_id);
	CargoID cargo;
	FOR_EACH_SET_BIT(cargo, other_cargo_mask) {
		if (value != filter_func(cargo)) ClrBit(cargo_mask, cargo);
	}
	return value;
}

/**
 * Shared order list linking together the linked list of orders and the list
 *  of vehicles sharing this order list.
 */
struct OrderList : OrderListPool::PoolItem<&_orderlist_pool> {
private:
	friend void AfterLoadVehicles(bool part_of_load); ///< For instantiating the shared vehicle chain
	friend const struct SaveLoad *GetOrderListDescription(); ///< Saving and loading of order lists.

	StationID GetBestLoadableNext(const Vehicle *v, const Order *o1, const Order *o2) const;

	Order *first;                     ///< First order of the order list.
	VehicleOrderID num_orders;        ///< NOSAVE: How many orders there are in the list.
	VehicleOrderID num_manual_orders; ///< NOSAVE: How many manually added orders are there in the list.
	uint num_vehicles;                ///< NOSAVE: Number of vehicles that share this order list.
	Vehicle *first_shared;            ///< NOSAVE: pointer to the first vehicle in the shared order chain.

	Ticks timetable_duration;         ///< NOSAVE: Total timetabled duration of the order list.
	Ticks total_duration;             ///< NOSAVE: Total (timetabled or not) duration of the order list.

	std::vector<uint32> scheduled_dispatch;    ///< Scheduled dispatch time
	uint32 scheduled_dispatch_duration;        ///< Scheduled dispatch duration
	Date scheduled_dispatch_start_date;        ///< Scheduled dispatch start date
	uint16 scheduled_dispatch_start_full_date_fract;///< Scheduled dispatch start full date fraction;
	                                           /// this count to (DAY_TICK * _settings_game.economy.day_length_factor)
	int32 scheduled_dispatch_last_dispatch;    ///< Last vehicle dispatched offset
	int32 scheduled_dispatch_max_delay;        ///< Maximum allowed delay

public:
	/** Default constructor producing an invalid order list. */
	OrderList(VehicleOrderID num_orders = INVALID_VEH_ORDER_ID)
		: first(NULL), num_orders(num_orders), num_manual_orders(0), num_vehicles(0), first_shared(NULL),
		  timetable_duration(0), total_duration(0), scheduled_dispatch_duration(0),
		  scheduled_dispatch_start_date(-1), scheduled_dispatch_start_full_date_fract(0),
		  scheduled_dispatch_last_dispatch(0), scheduled_dispatch_max_delay(0) { }

	/**
	 * Create an order list with the given order chain for the given vehicle.
	 *  @param chain pointer to the first order of the order chain
	 *  @param v any vehicle using this orderlist
	 */
	OrderList(Order *chain, Vehicle *v) { this->Initialize(chain, v); }

	/** Destructor. Invalidates OrderList for re-usage by the pool. */
	~OrderList() {}

	void Initialize(Order *chain, Vehicle *v);

	/**
	 * Get the first order of the order chain.
	 * @return the first order of the chain.
	 */
	inline Order *GetFirstOrder() const { return this->first; }

	Order *GetOrderAt(int index) const;

	VehicleOrderID GetIndexOfOrder(const Order *order) const;

	/**
	 * Get the last order of the order chain.
	 * @return the last order of the chain.
	 */
	inline Order *GetLastOrder() const { return this->GetOrderAt(this->num_orders - 1); }

	/**
	 * Get the order after the given one or the first one, if the given one is the
	 * last one.
	 * @param curr Order to find the next one for.
	 * @return Next order.
	 */
	inline const Order *GetNext(const Order *curr) const { return (curr->next == NULL) ? this->GetFirstOrder() : curr->next; }

	/**
	 * Get number of orders in the order list.
	 * @return number of orders in the chain.
	 */
	inline VehicleOrderID GetNumOrders() const { return this->num_orders; }

	/**
	 * Get number of manually added orders in the order list.
	 * @return number of manual orders in the chain.
	 */
	inline VehicleOrderID GetNumManualOrders() const { return this->num_manual_orders; }

	CargoMaskedStationIDStack GetNextStoppingStation(const Vehicle *v, CargoTypes cargo_mask, const Order *first = NULL, uint hops = 0) const;
	const Order *GetNextDecisionNode(const Order *next, uint hops, CargoTypes &cargo_mask) const;

	void InsertOrderAt(Order *new_order, int index);
	void DeleteOrderAt(int index);
	void MoveOrder(int from, int to);

	/**
	 * Is this a shared order list?
	 * @return whether this order list is shared among multiple vehicles
	 */
	inline bool IsShared() const { return this->num_vehicles > 1; };

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *GetFirstSharedVehicle() const { return this->first_shared; }

	/**
	 * Return the number of vehicles that share this orders list
	 * @return the count of vehicles that use this shared orders list
	 */
	inline uint GetNumVehicles() const { return this->num_vehicles; }

	bool IsVehicleInSharedOrdersList(const Vehicle *v) const;
	int GetPositionInSharedOrderList(const Vehicle *v) const;

	/**
	 * Adds the given vehicle to this shared order list.
	 * @note This is supposed to be called after the vehicle has been inserted
	 *       into the shared vehicle chain.
	 * @param v vehicle to add to the list
	 */
	inline void AddVehicle(Vehicle *v) { ++this->num_vehicles; }

	void RemoveVehicle(Vehicle *v);

	bool IsCompleteTimetable() const;

	/**
	 * Gets the total duration of the vehicles timetable or INVALID_TICKS is the timetable is not complete.
	 * @return total timetable duration or INVALID_TICKS for incomplete timetables
	 */
	inline Ticks GetTimetableTotalDuration() const { return this->IsCompleteTimetable() ? this->timetable_duration : INVALID_TICKS; }

	/**
	 * Gets the known duration of the vehicles timetable even if the timetable is not complete.
	 * @return known timetable duration
	 */
	inline Ticks GetTimetableDurationIncomplete() const { return this->timetable_duration; }

	/**
	 * Gets the known duration of the vehicles orders, timetabled or not.
	 * @return  known order duration.
	 */
	inline Ticks GetTotalDuration() const { return this->total_duration; }

	/**
	 * Must be called if an order's timetable is changed to update internal book keeping.
	 * @param delta By how many ticks has the timetable duration changed
	 */
	void UpdateTimetableDuration(Ticks delta) { this->timetable_duration += delta; }

	/**
	 * Must be called if an order's timetable is changed to update internal book keeping.
	 * @param delta By how many ticks has the total duration changed
	 */
	void UpdateTotalDuration(Ticks delta) { this->total_duration += delta; }

	void FreeChain(bool keep_orderlist = false);

	void DebugCheckSanity() const;

	/**
	 * Get the vector of all scheduled dispatch slot
	 * @return  first scheduled dispatch
	 */
	inline const std::vector<uint32> &GetScheduledDispatch() { return this->scheduled_dispatch; }

	void AddScheduledDispatch(uint32 offset);
	void RemoveScheduledDispatch(uint32 offset);
	void UpdateScheduledDispatch();
	void ResetScheduledDispatch();

	/**
	 * Set the scheduled dispatch duration, in scaled tick
	 * @param  duration  New duration
	 */
	inline void SetScheduledDispatchDuration(uint32 duration) { this->scheduled_dispatch_duration = duration; }

	/**
	 * Get the scheduled dispatch duration, in scaled tick
	 * @return  scheduled dispatch duration
	 */
	inline uint32 GetScheduledDispatchDuration() { return this->scheduled_dispatch_duration; }

	/**
	 * Set the scheduled dispatch start
	 * @param  start New start date
	 * @param  fract New start full date fraction, see \c CmdScheduledDispatchSetStartDate
	 */
	inline void SetScheduledDispatchStartDate(Date start_date, uint16 start_full_date_fract)
	{
		this->scheduled_dispatch_start_date = start_date;
		this->scheduled_dispatch_start_full_date_fract = start_full_date_fract;
	}

	/**
	 * Get the scheduled dispatch start date, in absolute scaled tick
	 * @return  scheduled dispatch start date
	 */
	inline DateTicksScaled GetScheduledDispatchStartTick() { return SchdispatchConvertToScaledTick(this->scheduled_dispatch_start_date, this->scheduled_dispatch_start_full_date_fract); }

	/**
	 * Whether the scheduled dispatch setting is valid
	 * @return  scheduled dispatch start date fraction
	 */
	inline bool IsScheduledDispatchValid() { return this->scheduled_dispatch_start_date >= 0 && this->scheduled_dispatch_duration > 0; }

	/**
	 * Set the scheduled dispatch last dispatch offset, in scaled tick
	 * @param  duration  New last dispatch offset
	 */
	inline void SetScheduledDispatchLastDispatch(int32 offset) { this->scheduled_dispatch_last_dispatch = offset; }

	/**
	 * Get the scheduled dispatch last dispatch offset, in scaled tick
	 * @return  scheduled dispatch last dispatch
	 */
	inline int32 GetScheduledDispatchLastDispatch() { return this->scheduled_dispatch_last_dispatch; }

	/**
	 * Set the scheduled dispatch maximum allowed delay, in scaled tick
	 * @param  delay  New maximum allow delay
	 */
	inline void SetScheduledDispatchDelay(int32 delay) { this->scheduled_dispatch_max_delay = delay; }

	/**
	 * Get the scheduled dispatch maximum alowed delay, in scaled tick
	 * @return  scheduled dispatch last dispatch
	 */
	inline int32 GetScheduledDispatchDelay() { return this->scheduled_dispatch_max_delay; }

};

#define FOR_ALL_ORDERS_FROM(var, start) FOR_ALL_ITEMS_FROM(Order, order_index, var, start)
#define FOR_ALL_ORDERS(var) FOR_ALL_ORDERS_FROM(var, 0)


#define FOR_VEHICLE_ORDERS(v, order) for (order = (v->orders.list == NULL) ? NULL : v->orders.list->GetFirstOrder(); order != NULL; order = order->next)


#define FOR_ALL_ORDER_LISTS_FROM(var, start) FOR_ALL_ITEMS_FROM(OrderList, orderlist_index, var, start)
#define FOR_ALL_ORDER_LISTS(var) FOR_ALL_ORDER_LISTS_FROM(var, 0)

#endif /* ORDER_BASE_H */
