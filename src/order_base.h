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
#include "gfx_type.h"
#include "sl/saveload_common.h"

#include <memory>
#include <vector>
#include "3rdparty/cpp-btree/btree_map.h"

typedef Pool<Order, OrderID, 256, 0xFF0000> OrderPool;
typedef Pool<OrderList, OrderListID, 128, 64000> OrderListPool;
extern OrderPool _order_pool;
extern OrderListPool _orderlist_pool;
extern btree::btree_map<uint32_t, uint32_t> _order_destination_refcount_map;
extern bool _order_destination_refcount_map_valid;

inline uint32_t OrderDestinationRefcountMapKey(DestinationID dest, CompanyID cid, OrderType order_type, VehicleType veh_type)
{
	static_assert(sizeof(dest) == 2);
	static_assert(OT_END <= 16);
	return (((uint32_t) dest) << 16) | (((uint32_t) cid) << 8) | (((uint32_t) order_type) << 4) | ((uint32_t) veh_type);
}

template <typename F> void IterateOrderRefcountMapForDestinationID(DestinationID dest, F handler)
{
	for (auto lb = _order_destination_refcount_map.lower_bound(OrderDestinationRefcountMapKey(dest, (CompanyID) 0, (OrderType) 0, (VehicleType) 0)); lb != _order_destination_refcount_map.end(); ++lb) {
		if (GB(lb->first, 16, 16) != dest) return;
		if (lb->second && !handler((CompanyID) GB(lb->first, 8, 8), (OrderType) GB(lb->first, 4, 4), (VehicleType) GB(lb->first, 0, 4), lb->second)) return;
	}
}

void IntialiseOrderDestinationRefcountMap();
void ClearOrderDestinationRefcountMap();

/*
 * xflags bits:
 * Bit 0:    OT_CONDITIONAL and OT_GOTO_DEPOT: IsWaitTimetabled(): Depot: wait is timetabled, conditional: branch travel time
 * Bit 1:    IsWaitFixed(): Wait time fixed
 * Bits 2-3: GetLeaveType(): Order leave type
 * Bit 4:    IsTravelFixed(): Travel time fixed
 * Bits 5-7: GetRoadVehTravelDirection(): Road vehicle travel direction
 */
/*
 * xdata users:
 * OT_COUNTER: Counter operation value (not counter ID)
 * OCV_SLOT_OCCUPANCY, OCV_VEH_IN_SLOT: Trace restrict slot ID
 * OCV_COUNTER_VALUE: Bits 0-15: Counter comparison value, Bits 16-31: Counter ID
 * OCV_TIMETABLE: Timetable lateness/earliness
 * OCV_TIME_DATE: Time/date
 * OCV_CARGO_WAITING_AMOUNT: Bits 0-15: Cargo quantity comparison value, Bits 16-31: Via station ID + 2
 * OCV_CARGO_WAITING_AMOUNT_PERCENTAGE: Bits 0-15: Cargo quantity comparison value, Bits 16-31: Via station ID + 2
 * OCV_CARGO_LOAD_PERCENTAGE: Cargo percentage comparison value
 * OCV_DISPATCH_SLOT: Bits 0-15: Dispatch schedule ID
 * OCV_PERCENT: Bits 0-7: Jump counter
 */
/*
 * xdata2 users:
 * OCV_CARGO_WAITING: Bits 0-15: Station ID to test + 1
 * OCV_CARGO_ACCEPTANCE: Bits 0-15: Station ID to test + 1
 * OCV_FREE_PLATFORMS: Bits 0-15: Station ID to test + 1
 * OCV_CARGO_WAITING_AMOUNT: Bits 0-15: Station ID to test + 1
 * OCV_CARGO_WAITING_AMOUNT_PERCENTAGE: Bits 0-15: Station ID to test + 1, Bit 16: Refit mode
 */

struct OrderExtraInfo {
	uint8_t cargo_type_flags[NUM_CARGO] = {}; ///< Load/unload types for each cargo type.
	uint32_t xdata = 0;                       ///< Extra arbitrary data
	uint32_t xdata2 = 0;                      ///< Extra arbitrary data
	uint16_t dispatch_index = 0;              ///< Scheduled dispatch index + 1
	uint8_t xflags = 0;                       ///< Extra flags
	uint8_t colour = 0;                       ///< Order colour + 1
};

namespace upstream_sl {
	SaveLoadTable GetOrderDescription();
	SaveLoadTable GetOrderListDescription();
	class SlVehicleCommon;
	class SlVehicleDisaster;
}

/* If you change this, keep in mind that it is saved on 3 places:
 * - Load_ORDR, all the global orders
 * - Vehicle -> current_order
 * - REF_ORDER (all REFs are currently limited to 16 bits!!)
 */
struct Order : OrderPool::PoolItem<&_order_pool> {
private:
	friend NamedSaveLoadTable GetVehicleDescription(VehicleType vt);      ///< Saving and loading the current order of vehicles.
	friend void Load_VEHS();                                              ///< Loading of ancient vehicles.
	friend NamedSaveLoadTable GetOrderDescription();                      ///< Saving and loading of orders.
	friend struct OrderExtraDataStructHandler;                            ///< Saving and loading of orders.
	friend struct VehicleOrderExtraDataStructHandler;                     ///< Saving and loading of orders.
	friend upstream_sl::SaveLoadTable upstream_sl::GetOrderDescription(); ///< Saving and loading of orders.
	friend upstream_sl::SlVehicleCommon;
	friend upstream_sl::SlVehicleDisaster;
	friend void Load_ORDX();                                             ///< Saving and loading of orders.
	friend void Load_VEOX();                                             ///< Saving and loading of orders.

	uint16_t flags;       ///< Load/unload types, depot order/action types.
	DestinationID dest;   ///< The destination of the order.

	std::unique_ptr<OrderExtraInfo> extra; ///< Extra order info

	uint8_t type;         ///< The type of order + non-stop flags

	CargoID refit_cargo;  ///< Refit CargoID

	uint8_t occupancy;    ///< Estimate of vehicle occupancy on departure, for the current order, 0 indicates invalid, 1 - 101 indicate 0 - 100%

	TimetableTicks wait_time;    ///< How long in ticks to wait at the destination.
	TimetableTicks travel_time;  ///< How long in ticks the journey to this destination should take.
	uint16_t max_speed;          ///< How fast the vehicle may go on the way to the destination.

	void AllocExtraInfo();
	void DeAllocExtraInfo();

	inline void CheckExtraInfoAlloced()
	{
		if (!this->extra) this->AllocExtraInfo();
	}

	inline uint8_t GetXFlags() const
	{
		return this->extra != nullptr ? this->extra->xflags : 0;
	}

	inline uint8_t &GetXFlagsRef()
	{
		this->CheckExtraInfoAlloced();
		return this->extra->xflags;
	}

public:
	inline uint32_t GetXData() const
	{
		return this->extra != nullptr ? this->extra->xdata : 0;
	}

	inline uint16_t GetXDataLow() const
	{
		return (uint16_t)GB(this->GetXData(), 0, 16);
	}

	inline uint16_t GetXDataHigh() const
	{
		return (uint16_t)GB(this->GetXData(), 16, 16);
	}

	inline uint32_t &GetXDataRef()
	{
		this->CheckExtraInfoAlloced();
		return this->extra->xdata;
	}

	inline void SetXDataLow(uint16_t data)
	{
		SB(this->GetXDataRef(), 0, 16, data);
	}

	inline void SetXDataHigh(uint16_t data)
	{
		SB(this->GetXDataRef(), 16, 16, data);
	}

	inline uint32_t GetXData2() const
	{
		return this->extra != nullptr ? this->extra->xdata2 : 0;
	}

	inline uint16_t GetXData2Low() const
	{
		return (uint16_t)GB(this->GetXData2(), 0, 16);
	}

	inline uint16_t GetXData2High() const
	{
		return (uint16_t)GB(this->GetXData2(), 16, 16);
	}

	inline uint32_t &GetXData2Ref()
	{
		this->CheckExtraInfoAlloced();
		return this->extra->xdata2;
	}

	inline void SetXData2Low(uint16_t data)
	{
		SB(this->GetXData2Ref(), 0, 16, data);
	}

	inline void SetXData2High(uint16_t data)
	{
		SB(this->GetXData2Ref(), 16, 16, data);
	}

	inline uint16_t GetRawFlags() const
	{
		return this->flags;
	}

	Order *next;          ///< Pointer to next order. If nullptr, end of list

	Order() : flags(0), refit_cargo(CARGO_NO_REFIT), max_speed(UINT16_MAX) {}
	Order(uint8_t type, uint8_t flags, DestinationID dest) : flags(flags), dest(dest), type(type), refit_cargo(CARGO_NO_REFIT), occupancy(0), wait_time(0), travel_time(0), max_speed(UINT16_MAX) {}
	~Order();

	Order(uint64_t packed);

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
	void MakeGoToDepot(DepotID destination, OrderDepotTypeFlags order, OrderNonStopFlags non_stop_type = ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, OrderDepotActionFlags action = ODATF_SERVICE_ONLY, CargoID cargo = CARGO_NO_REFIT);
	void MakeGoToWaypoint(StationID destination);
	void MakeLoading(bool ordered);
	void MakeLeaveStation();
	void MakeDummy();
	void MakeConditional(VehicleOrderID order);
	void MakeImplicit(StationID destination);
	void MakeWaiting();
	void MakeLoadingAdvance(StationID destination);
	void MakeReleaseSlot();
	void MakeTryAcquireSlot();
	void MakeChangeCounter();
	void MakeLabel(OrderLabelSubType subtype);

	/**
	 * Is this a 'goto' order with a real destination?
	 * @return True if the type is either #OT_GOTO_WAYPOINT, #OT_GOTO_DEPOT or #OT_GOTO_STATION.
	 */
	inline bool IsGotoOrder() const
	{
		return IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION);
	}

	/**
	 * Is this an order with a BaseStation destination?
	 * @return True if the type is either #OT_IMPLICIT, #OT_GOTO_STATION or #OT_GOTO_WAYPOINT.
	 */
	inline bool IsBaseStationOrder() const
	{
		return IsType(OT_IMPLICIT) || IsType(OT_GOTO_STATION) || IsType(OT_GOTO_WAYPOINT);
	}

	/**
	 * Gets the destination of this order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION) || IsType(OT_SLOT) || IsType(OT_COUNTER) || IsType(OT_LABEL).
	 * @return the destination of the order.
	 */
	inline DestinationID GetDestination() const { return this->dest; }

	/**
	 * Sets the destination of this order.
	 * @param destination the new destination of the order.
	 * @pre IsType(OT_GOTO_WAYPOINT) || IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION) || IsType(OT_SLOT) || IsType(OT_COUNTER) || IsType(OT_LABEL).
	 */
	inline void SetDestination(DestinationID destination) { this->dest = destination; }

	/**
	 * Is this order a refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a refit should happen.
	 */
	inline bool IsRefit() const { return this->refit_cargo < NUM_CARGO || this->refit_cargo == CARGO_AUTO_REFIT; }

	/**
	 * Is this order a auto-refit order.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return true if a auto-refit should happen.
	 */
	inline bool IsAutoRefit() const { return this->refit_cargo == CARGO_AUTO_REFIT; }

	/**
	 * Get the cargo to to refit to.
	 * @pre IsType(OT_GOTO_DEPOT) || IsType(OT_GOTO_STATION)
	 * @return the cargo type.
	 */
	inline CargoID GetRefitCargo() const { return this->refit_cargo; }

	void SetRefit(CargoID cargo);

	/**
	 * Update the jump_counter of this order.
	 * @param percent the jump chance in %.
	 * @param dry_run whether this is a dry-run, so do not execute side-effects
	 * @return whether to jump or not.
	 * @pre IsType(OT_CONDITIONAL) && this->GetConditionVariable() == OCV_PERCENT.
	 */
	bool UpdateJumpCounter(uint8_t percent, bool dry_run);

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
			CargoTypes output_mask = cargo_mask;
			for (CargoID cargo : SetCargoBitIterator(cargo_mask)) {
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
	inline OrderDepotActionFlags GetDepotActionType() const { return (OrderDepotActionFlags)GB(this->flags, 3, 4); }
	/** Extra depot flags. */
	inline OrderDepotExtraFlags GetDepotExtraFlags() const { return (OrderDepotExtraFlags)GB(this->flags, 8, 8); }
	/** What waypoint flags? */
	inline OrderWaypointFlags GetWaypointFlags() const { return (OrderWaypointFlags)GB(this->flags, 0, 3); }
	/** What variable do we have to compare? */
	inline OrderConditionVariable GetConditionVariable() const { return (OrderConditionVariable)GB(this->dest, 11, 5); }
	/** What is the comparator to use? */
	inline OrderConditionComparator GetConditionComparator() const { return (OrderConditionComparator)GB(this->type, 5, 3); }
	/** Get the order to skip to. */
	inline VehicleOrderID GetConditionSkipToOrder() const { return this->flags; }
	/** Get the value to base the skip on. */
	inline uint16_t GetConditionValue() const { return GB(this->dest, 0, 11); }
	/** Get counter for the 'jump xx% of times' option */
	inline int8_t GetJumpCounter() const { return GB(this->GetXData(), 0, 8); }
	/** Get counter operation */
	inline uint8_t GetCounterOperation() const { return GB(this->flags, 0, 8); }
	/** Get condition station ID */
	inline StationID GetConditionStationID() const { return (StationID)(this->GetXData2Low() - 1); }
	/** Has condition via station ID */
	inline bool HasConditionViaStation() const { return this->GetXDataHigh() != 0; }
	/** Get condition via station ID */
	inline StationID GetConditionViaStationID() const { return (StationID)(this->GetXDataHigh() - 2); }
	/** Get condition dispatch scheduled ID */
	inline uint16_t GetConditionDispatchScheduleID() const { return this->GetXDataLow(); }

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
	inline void SetDepotActionType(OrderDepotActionFlags depot_service_type) { SB(this->flags, 3, 4, depot_service_type); }
	/** Set what we are going to do in the depot. */
	inline void SetDepotExtraFlags(OrderDepotExtraFlags depot_extra_flags) { SB(this->flags, 8, 8, depot_extra_flags); }
	/** Set waypoint flags. */
	inline void SetWaypointFlags(OrderWaypointFlags waypoint_flags) { SB(this->flags, 0, 3, waypoint_flags); }
	/** Set variable we have to compare. */
	inline void SetConditionVariable(OrderConditionVariable condition_variable) { SB(this->dest, 11, 5, condition_variable); }
	/** Set the comparator to use. */
	inline void SetConditionComparator(OrderConditionComparator condition_comparator) { SB(this->type, 5, 3, condition_comparator); }
	/** Get the order to skip to. */
	inline void SetConditionSkipToOrder(VehicleOrderID order_id) { this->flags = order_id; }
	/** Set the value to base the skip on. */
	inline void SetConditionValue(uint16_t value) { SB(this->dest, 0, 11, value); }
	/** Set counter for the 'jump xx% of times' option */
	inline void SetJumpCounter(int8_t jump_counter) { SB(this->GetXDataRef(), 0, 8, jump_counter); }
	/** Set counter operation */
	inline void SetCounterOperation(uint8_t op) { SB(this->flags, 0, 8, op); }
	/** Set condition station ID */
	inline void SetConditionStationID(StationID st) { this->SetXData2Low(st + 1); }
	/** Set condition via station ID */
	inline void SetConditionViaStationID(StationID st) { this->SetXDataHigh(st + 2); }
	/** Clear condition via station ID */
	inline void ClearConditionViaStation() { this->SetXDataHigh(0); }
	/** Set condition dispatch scheduled ID */
	inline void SetConditionDispatchScheduleID(uint16_t slot) { this->SetXDataLow(slot); }

	/* As conditional orders write their "skip to" order all over the flags, we cannot check the
	 * flags to find out if timetabling is enabled. However, as conditional orders are never
	 * autofilled we can be sure that any non-zero values for their wait_time and travel_time are
	 * explicitly set (but travel_time is actually unused for conditionals). */

	/* Does this order not have any associated travel or wait times */
	inline bool HasNoTimetableTimes() const { return this->IsType(OT_COUNTER) || this->IsType(OT_SLOT) || this->IsType(OT_LABEL); }

	/** Does this order have an explicit wait time set? */
	inline bool IsWaitTimetabled() const
	{
		if (this->HasNoTimetableTimes()) return true;
		return (this->IsType(OT_CONDITIONAL) || this->IsType(OT_GOTO_DEPOT)) ? HasBit(this->GetXFlags(), 0) : HasBit(this->flags, 3);
	}
	/** Does this order have an explicit travel time set? */
	inline bool IsTravelTimetabled() const
	{
		if (this->HasNoTimetableTimes()) return true;
		return this->IsType(OT_CONDITIONAL) ? this->travel_time > 0 : HasBit(this->flags, 7);
	}

	/** Get the time in ticks a vehicle should wait at the destination or 0 if it's not timetabled. */
	inline TimetableTicks GetTimetabledWait() const { return this->IsWaitTimetabled() ? this->wait_time : 0; }
	/** Get the time in ticks a vehicle should take to reach the destination or 0 if it's not timetabled. */
	inline TimetableTicks GetTimetabledTravel() const { return this->IsTravelTimetabled() ? this->travel_time : 0; }
	/** Get the time in ticks a vehicle will probably wait at the destination (timetabled or not). */
	inline TimetableTicks GetWaitTime() const { return this->wait_time; }
	/** Get the time in ticks a vehicle will probably take to reach the destination (timetabled or not). */
	inline TimetableTicks GetTravelTime() const { return this->travel_time; }

	/**
	 * Get the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @return maximum speed.
	 */
	inline uint16_t GetMaxSpeed() const { return this->max_speed; }

	/** Set if the wait time is explicitly timetabled (unless the order is conditional). */
	inline void SetWaitTimetabled(bool timetabled)
	{
		if (this->HasNoTimetableTimes()) return;
		if (this->IsType(OT_CONDITIONAL) || this->IsType(OT_GOTO_DEPOT)) {
			if (this->extra == nullptr && !timetabled) return;
			AssignBit(this->GetXFlagsRef(), 0, timetabled);
		} else {
			AssignBit(this->flags, 3, timetabled);
		}
	}

	/** Set if the travel time is explicitly timetabled (unless the order is conditional). */
	inline void SetTravelTimetabled(bool timetabled)
	{
		if (!this->IsType(OT_CONDITIONAL) && !this->HasNoTimetableTimes()) AssignBit(this->flags, 7, timetabled);
	}

	/**
	 * Set the time in ticks to wait at the destination.
	 * @param time Time to set as wait time.
	 */
	inline void SetWaitTime(TimetableTicks time) { this->wait_time = time;  }

	/**
	 * Set the time in ticks to take for travelling to the destination.
	 * @param time Time to set as travel time.
	 */
	inline void SetTravelTime(TimetableTicks time) { this->travel_time = time; }

	/**
	 * Set the maxmimum speed in km-ish/h a vehicle is allowed to reach on the way to the
	 * destination.
	 * @param speed Speed to be set.
	 */
	inline void SetMaxSpeed(uint16_t speed) { this->max_speed = speed; }

	/** Does this order have a fixed wait time? */
	inline bool IsWaitFixed() const { return HasBit(this->GetXFlags(), 1); }

	/** Set if the wait time is fixed */
	inline void SetWaitFixed(bool fixed)
	{
		if (fixed != this->IsWaitFixed()) AssignBit(this->GetXFlagsRef(), 1, fixed);
	}

	/** Does this order have a fixed travel time? */
	inline bool IsTravelFixed() const { return HasBit(this->GetXFlags(), 4); }

	/** Set if the travel time is fixed */
	inline void SetTravelFixed(bool fixed)
	{
		if (!this->IsType(OT_CONDITIONAL) && fixed != IsTravelFixed()) AssignBit(this->GetXFlagsRef(), 4, fixed);
	}

	/** Get the leave type */
	inline OrderLeaveType GetLeaveType() const { return (OrderLeaveType)GB(this->GetXFlags(), 2, 2); }

	/** Set the leave type */
	inline void SetLeaveType(OrderLeaveType leave_type)
	{
		if (leave_type != this->GetLeaveType()) SB(this->GetXFlagsRef(), 2, 2, leave_type);
	}

	/** Get the road vehicle travel direction */
	inline DiagDirection GetRoadVehTravelDirection() const { return (DiagDirection)((GB(this->GetXFlags(), 5, 3) - 1) & 0xFF); }

	/** Set the road vehicle travel direction */
	inline void SetRoadVehTravelDirection(DiagDirection dir)
	{
		if (dir != this->GetRoadVehTravelDirection()) SB(this->GetXFlagsRef(), 5, 3, (dir + 1) & 0x7);
	}

	/**
	 * Get the occupancy value
	 * @return occupancy
	 */
	inline uint8_t GetOccupancy() const { return this->occupancy; }

	/**
	 * Set the occupancy value
	 * @param occupancy The occupancy to set
	 */
	inline void SetOccupancy(uint8_t occupancy) { this->occupancy = occupancy; }

	bool UseOccupancyValueForAverage() const;

	bool ShouldStopAtStation(StationID last_station_visited, StationID station, bool waypoint) const;
	bool ShouldStopAtStation(const Vehicle *v, StationID station, bool waypoint) const;
	bool CanLeaveWithCargo(bool has_cargo, CargoID cargo) const;

	TileIndex GetLocation(const Vehicle *v, bool airport = false) const;
	TileIndex GetAuxiliaryLocation(bool secondary = false) const;

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

	inline int GetDispatchScheduleIndex() const
	{
		return this->extra != nullptr ? (int)this->extra->dispatch_index - 1 : -1;
	}

	inline void SetDispatchScheduleIndex(int schedule_index)
	{
		if (schedule_index != this->GetDispatchScheduleIndex()) {
			this->CheckExtraInfoAlloced();
			this->extra->dispatch_index = (uint16_t)(schedule_index + 1);
		}
	}

	inline bool IsScheduledDispatchOrder(bool require_wait_timetabled) const
	{
		return this->extra != nullptr && this->extra->dispatch_index > 0 && (!require_wait_timetabled || this->IsWaitTimetabled());
	}

	/** Get order colour */
	inline Colours GetColour() const
	{
		uint8_t value = this->extra != nullptr ? this->extra->colour : 0;
		return (Colours)(value - 1);
	}

	/** Set order colour */
	inline void SetColour(Colours colour)
	{
		if (colour != this->GetColour()) {
			this->CheckExtraInfoAlloced();
			this->extra->colour = ((uint8_t)colour) + 1;
		}
	}

	inline OrderSlotSubType GetSlotSubType() const
	{
		return (OrderSlotSubType)GB(this->flags, 0, 8);
	}

	inline OrderLabelSubType GetLabelSubType() const
	{
		return (OrderLabelSubType)GB(this->flags, 0, 8);
	}

	inline void SetLabelSubType(OrderLabelSubType subtype)
	{
		SB(this->flags, 0, 8, subtype);
	}

	const char *GetLabelText() const;
	void SetLabelText(const char *text);

	void AssignOrder(const Order &other);
	bool Equals(const Order &other) const;

	uint64_t Pack() const;
	uint16_t MapOldOrder() const;
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

	void FillNextStoppingStation(const Vehicle *v, const OrderList *o, const Order *first = nullptr, uint hops = 0);
};

template <typename F> CargoTypes FilterCargoMask(F filter_func, CargoTypes cargo_mask = ALL_CARGOTYPES)
{
	CargoTypes output_mask = cargo_mask;
	for (CargoID cargo : SetCargoBitIterator(cargo_mask)) {
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
	for (CargoID cargo : SetCargoBitIterator(other_cargo_mask)) {
		if (value != filter_func(cargo)) ClrBit(cargo_mask, cargo);
	}
	return value;
}

struct DispatchSlot {
	uint32_t offset;
	uint16_t flags;

	bool operator<(const DispatchSlot &other) const
	{
		return this->offset < other.offset;
	}

	/**
	 * Flag bit numbers for scheduled_dispatch_flags
	 */
	enum ScheduledDispatchSlotFlags {
		SDSF_REUSE_SLOT                           = 0,  ///< Allow this slot to be used more than once
		SDSF_FIRST_TAG                            = 8,  ///< First tag flag
		SDSF_LAST_TAG                             = 11, ///< Last tag flag
	};
};

enum ScheduledDispatchSupplementaryNameType : uint16_t {
	SDSNT_DEPARTURE_TAG                           = 0, ///< Departure slot tag
};

struct DispatchSchedule {
	static constexpr uint DEPARTURE_TAG_COUNT = 4;

private:
	friend NamedSaveLoadTable GetDispatchScheduleDescription();         ///< Saving and loading of dispatch schedules

	std::vector<DispatchSlot> scheduled_dispatch;                       ///< Scheduled dispatch slots
	StateTicks scheduled_dispatch_start_tick = -1;                      ///< Scheduled dispatch start tick
	uint32_t scheduled_dispatch_duration = 0;                           ///< Scheduled dispatch duration
	int32_t scheduled_dispatch_last_dispatch = INVALID_SCHEDULED_DISPATCH_OFFSET; ///< Last vehicle dispatched offset
	int32_t scheduled_dispatch_max_delay = 0;                           ///< Maximum allowed delay
	uint8_t scheduled_dispatch_flags = 0;                               ///< Flags

	std::string name;                                                   ///< Name of dispatch schedule
	btree::btree_map<uint32_t, std::string> supplementary_names;        ///< Supplementary name strings

	inline void CopyBasicFields(const DispatchSchedule &other)
	{
		this->scheduled_dispatch_duration              = other.scheduled_dispatch_duration;
		this->scheduled_dispatch_start_tick            = other.scheduled_dispatch_start_tick;
		this->scheduled_dispatch_last_dispatch         = other.scheduled_dispatch_last_dispatch;
		this->scheduled_dispatch_max_delay             = other.scheduled_dispatch_max_delay;
		this->scheduled_dispatch_flags                 = other.scheduled_dispatch_flags;
	}

	/**
	 * Flag bit numbers for scheduled_dispatch_flags
	 */
	enum ScheduledDispatchFlags {
		SDF_REUSE_SLOTS                           = 0,  ///< Allow each dispatch slot to be used more than once
	};

public:
	/**
	 * Get the vector of all scheduled dispatch slot
	 * @return  first scheduled dispatch
	 */
	inline const std::vector<DispatchSlot> &GetScheduledDispatch() const { return this->scheduled_dispatch; }
	inline std::vector<DispatchSlot> &GetScheduledDispatchMutable() { return this->scheduled_dispatch; }

	void SetScheduledDispatch(std::vector<DispatchSlot> dispatch_list);
	void AddScheduledDispatch(uint32_t offset);
	void RemoveScheduledDispatch(uint32_t offset);
	void AdjustScheduledDispatch(int32_t adjust);
	void ClearScheduledDispatch() { this->scheduled_dispatch.clear(); }
	bool UpdateScheduledDispatchToDate(StateTicks now);
	void UpdateScheduledDispatch(const Vehicle *v);

	/**
	 * Set the scheduled dispatch duration, in scaled tick
	 * @param  duration  New duration
	 */
	inline void SetScheduledDispatchDuration(uint32_t duration) { this->scheduled_dispatch_duration = duration; }

	/**
	 * Get the scheduled dispatch duration, in scaled tick
	 * @return  scheduled dispatch duration
	 */
	inline uint32_t GetScheduledDispatchDuration() const { return this->scheduled_dispatch_duration; }

	/**
	 * Set the scheduled dispatch start
	 * @param  start_ticks New start ticks
	 */
	inline void SetScheduledDispatchStartTick(StateTicks start_tick)
	{
		this->scheduled_dispatch_start_tick = start_tick;
	}

	/**
	 * Get the scheduled dispatch start date, in absolute scaled tick
	 * @return  scheduled dispatch start date
	 */
	inline StateTicks GetScheduledDispatchStartTick() const { return this->scheduled_dispatch_start_tick; }

	/**
	 * Whether the scheduled dispatch setting is valid
	 * @return  scheduled dispatch start date fraction
	 */
	inline bool IsScheduledDispatchValid() const { return this->scheduled_dispatch_duration > 0; }

	/**
	 * Set the scheduled dispatch last dispatch offset, in scaled tick
	 * @param  duration  New last dispatch offset
	 */
	inline void SetScheduledDispatchLastDispatch(int32_t offset) { this->scheduled_dispatch_last_dispatch = offset; }

	/**
	 * Get the scheduled dispatch last dispatch offset, in scaled tick
	 * @return  scheduled dispatch last dispatch
	 */
	inline int32_t GetScheduledDispatchLastDispatch() const { return this->scheduled_dispatch_last_dispatch; }

	/**
	 * Set the scheduled dispatch maximum allowed delay, in scaled tick
	 * @param  delay  New maximum allow delay
	 */
	inline void SetScheduledDispatchDelay(int32_t delay) { this->scheduled_dispatch_max_delay = delay; }

	/**
	 * Get whether to re-use dispatch slots
	 * @return  whether dispatch slots are re-used
	 */
	inline bool GetScheduledDispatchReuseSlots() const { return HasBit(this->scheduled_dispatch_flags, SDF_REUSE_SLOTS); }

	/**
	 * Set whether to re-use dispatch slots
	 * @param  delay  New maximum allow delay
	 */
	inline void SetScheduledDispatchReuseSlots(bool reuse_slots) { AssignBit(this->scheduled_dispatch_flags, SDF_REUSE_SLOTS, reuse_slots); }

	/**
	 * Get the scheduled dispatch maximum alowed delay, in scaled tick
	 * @return  scheduled dispatch last dispatch
	 */
	inline int32_t GetScheduledDispatchDelay() const { return this->scheduled_dispatch_max_delay; }

	inline void BorrowSchedule(DispatchSchedule &other)
	{
		this->CopyBasicFields(other);
		this->scheduled_dispatch = std::move(other.scheduled_dispatch);
	}

	inline void ReturnSchedule(DispatchSchedule &other)
	{
		other.scheduled_dispatch = std::move(this->scheduled_dispatch);
	}

	inline std::string &ScheduleName() { return this->name; }
	inline const std::string &ScheduleName() const { return this->name; }

	std::string_view GetSupplementaryName(ScheduledDispatchSupplementaryNameType name_type, uint16_t id) const;
	void SetSupplementaryName(ScheduledDispatchSupplementaryNameType name_type, uint16_t id, std::string name);
	btree::btree_map<uint32_t, std::string> &GetSupplementaryNameMap() { return this->supplementary_names; }
};

static_assert(DispatchSchedule::DEPARTURE_TAG_COUNT == 1 + (DispatchSlot::SDSF_LAST_TAG - DispatchSlot::SDSF_FIRST_TAG));

/**
 * Shared order list linking together the linked list of orders and the list
 *  of vehicles sharing this order list.
 */
struct OrderList : OrderListPool::PoolItem<&_orderlist_pool> {
private:
	friend void AfterLoadVehiclesPhase1(bool part_of_load); ///< For instantiating the shared vehicle chain
	friend NamedSaveLoadTable GetOrderListDescription(); ///< Saving and loading of order lists.
	friend upstream_sl::SaveLoadTable upstream_sl::GetOrderListDescription(); ///< Saving and loading of order lists.
	friend void Ptrs_ORDL(); ///< Saving and loading of order lists.

	void ReindexOrderList();
	Order *GetOrderAtFromList(int index) const;

	VehicleOrderID num_manual_orders; ///< NOSAVE: How many manually added orders are there in the list.
	uint num_vehicles;                ///< NOSAVE: Number of vehicles that share this order list.
	Order *first;                     ///< First order of the order list.
	std::vector<Order *> order_index; ///< NOSAVE: Vector index of order list.
	Vehicle *first_shared;            ///< NOSAVE: pointer to the first vehicle in the shared order chain.

	Ticks timetable_duration;         ///< NOSAVE: Total timetabled duration of the order list.
	Ticks total_duration;             ///< NOSAVE: Total (timetabled or not) duration of the order list.

	std::vector<DispatchSchedule> dispatch_schedules; ///< Scheduled dispatch schedules

public:
	/** Default constructor producing an invalid order list. */
	OrderList(VehicleOrderID num_orders = INVALID_VEH_ORDER_ID)
		: num_manual_orders(0), num_vehicles(0), first(nullptr), first_shared(nullptr),
		  timetable_duration(0), total_duration(0) { }

	/**
	 * Create an order list with the given order chain for the given vehicle.
	 *  @param chain pointer to the first order of the order chain
	 *  @param v any vehicle using this orderlist
	 */
	OrderList(Order *chain, Vehicle *v) { this->Initialize(chain, v); }

	/** Destructor. Invalidates OrderList for re-usage by the pool. */
	~OrderList() {}

	void Initialize(Order *chain, Vehicle *v);

	void RecalculateTimetableDuration();

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
	inline Order *GetLastOrder() const { return this->GetOrderAt(this->GetNumOrders() - 1); }

	/**
	 * Get the order after the given one or the first one, if the given one is the
	 * last one.
	 * @param curr Order to find the next one for.
	 * @return Next order.
	 */
	inline const Order *GetNext(const Order *curr) const { return (curr->next == nullptr) ? this->GetFirstOrder() : curr->next; }

	/**
	 * Get number of orders in the order list.
	 * @return number of orders in the chain.
	 */
	inline VehicleOrderID GetNumOrders() const { return static_cast<VehicleOrderID>(this->order_index.size()); }

	/**
	 * Get number of manually added orders in the order list.
	 * @return number of manual orders in the chain.
	 */
	inline VehicleOrderID GetNumManualOrders() const { return this->num_manual_orders; }

	CargoMaskedStationIDStack GetNextStoppingStation(const Vehicle *v, CargoTypes cargo_mask, const Order *first = nullptr, uint hops = 0) const;
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

	/**
	 * Adds the given vehicle to this shared order list.
	 * @note This is supposed to be called after the vehicle has been inserted
	 *       into the shared vehicle chain.
	 * @param v vehicle to add to the list
	 */
	inline void AddVehicle([[maybe_unused]] Vehicle *v) { ++this->num_vehicles; }

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

#ifdef WITH_ASSERT
	void DebugCheckSanity() const;
#endif
	bool CheckOrderListIndexing() const;

	inline std::vector<DispatchSchedule> &GetScheduledDispatchScheduleSet() { return this->dispatch_schedules; }
	inline const std::vector<DispatchSchedule> &GetScheduledDispatchScheduleSet() const { return this->dispatch_schedules; }

	inline uint GetScheduledDispatchScheduleCount() const { return (uint)this->dispatch_schedules.size(); }

	inline DispatchSchedule &GetDispatchScheduleByIndex(uint index) { return this->dispatch_schedules[index]; }
	inline const DispatchSchedule &GetDispatchScheduleByIndex(uint index) const { return this->dispatch_schedules[index]; }
};

void UpdateOrderUIOnDateChange();

#endif /* ORDER_BASE_H */
