/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_type.h Types related to orders. */

#ifndef ORDER_TYPE_H
#define ORDER_TYPE_H

#include "core/enum_type.hpp"

typedef uint16_t VehicleOrderID;  ///< The index of an order within its current vehicle (not pool related)
typedef uint32_t OrderID;
typedef uint16_t OrderListID;
typedef uint16_t DestinationID;
typedef uint32_t TimetableTicks;

/** Invalid vehicle order index (sentinel) */
static const VehicleOrderID INVALID_VEH_ORDER_ID = 0xFFFF;
/** Last valid VehicleOrderID. */
static const VehicleOrderID MAX_VEH_ORDER_ID     = INVALID_VEH_ORDER_ID - 1;

/** Invalid order (sentinel) */
static const OrderID INVALID_ORDER = 0xFFFFFF;

/**
 * Maximum number of orders in implicit-only lists before we start searching
 * harder for duplicates.
 */
static const uint IMPLICIT_ORDER_ONLY_CAP = 32;

/** Invalid scheduled dispatch offset from current schedule */
static const int32_t INVALID_SCHEDULED_DISPATCH_OFFSET = INT32_MIN;

/** Order types. It needs to be 8bits, because we save and load it as such */
enum OrderType : uint8_t {
	OT_BEGIN         = 0,
	OT_NOTHING       = 0,
	OT_GOTO_STATION  = 1,
	OT_GOTO_DEPOT    = 2,
	OT_LOADING       = 3,
	OT_LEAVESTATION  = 4,
	OT_DUMMY         = 5,
	OT_GOTO_WAYPOINT = 6,
	OT_CONDITIONAL   = 7,
	OT_IMPLICIT      = 8,
	OT_WAITING       = 9,
	OT_LOADING_ADVANCE = 10,
	OT_SLOT          = 11,
	OT_COUNTER       = 12,
	OT_LABEL         = 13,
	OT_END
};

using OrderTypeMask = uint16_t;

enum OrderSlotSubType : uint8_t {
	OSST_RELEASE               = 0,
	OSST_TRY_ACQUIRE           = 1,
};

enum OrderLabelSubType : uint8_t {
	OLST_TEXT                  = 0,
	OLST_DEPARTURES_VIA        = 1,
	OLST_DEPARTURES_REMOVE_VIA = 2,
};

inline bool IsDestinationOrderLabelSubType(OrderLabelSubType subtype)
{
	return subtype == OLST_DEPARTURES_VIA || subtype == OLST_DEPARTURES_REMOVE_VIA;
}

inline bool IsDeparturesOrderLabelSubType(OrderLabelSubType subtype)
{
	return subtype == OLST_DEPARTURES_VIA || subtype == OLST_DEPARTURES_REMOVE_VIA;
}

/**
 * Flags related to the unloading order.
 */
enum OrderUnloadFlags {
	OUF_UNLOAD_IF_POSSIBLE = 0,      ///< Unload all cargo that the station accepts.
	OUFB_UNLOAD            = 1 << 0, ///< Force unloading all cargo onto the platform, possibly not getting paid.
	OUFB_TRANSFER          = 1 << 1, ///< Transfer all cargo onto the platform.
	OUFB_NO_UNLOAD         = 1 << 2, ///< Totally no unloading will be done.
	OUFB_CARGO_TYPE_UNLOAD = 1 << 3, ///< Unload actions are defined per cargo type.
	OUFB_CARGO_TYPE_UNLOAD_ENCODING = (1 << 0) | (1 << 2), ///< Raw encoding of OUFB_CARGO_TYPE_UNLOAD
};

/**
 * Flags related to the loading order.
 */
enum OrderLoadFlags {
	OLF_LOAD_IF_POSSIBLE = 0,      ///< Load as long as there is cargo that fits in the train.
	OLFB_FULL_LOAD       = 1 << 1, ///< Full load all cargoes of the consist.
	OLF_FULL_LOAD_ANY    = 3,      ///< Full load a single cargo of the consist.
	OLFB_NO_LOAD         = 4,      ///< Do not load anything.
	OLFB_CARGO_TYPE_LOAD = 1 << 3, ///< Load actions are defined per cargo type.
	OLFB_CARGO_TYPE_LOAD_ENCODING = (1 << 1) | 4, ///< Raw encoding of OLFB_CARGO_TYPE_LOAD
};

/**
 * Non-stop order flags.
 */
enum OrderNonStopFlags {
	ONSF_STOP_EVERYWHERE                  = 0, ///< The vehicle will stop at any station it passes and the destination.
	ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS = 1, ///< The vehicle will not stop at any stations it passes except the destination.
	ONSF_NO_STOP_AT_DESTINATION_STATION   = 2, ///< The vehicle will stop at any station it passes except the destination.
	ONSF_NO_STOP_AT_ANY_STATION           = 3, ///< The vehicle will not stop at any stations it passes including the destination.
	ONSF_END
};

/**
 * Where to stop the trains.
 */
enum OrderStopLocation {
	OSL_PLATFORM_NEAR_END = 0, ///< Stop at the near end of the platform
	OSL_PLATFORM_MIDDLE   = 1, ///< Stop at the middle of the platform
	OSL_PLATFORM_FAR_END  = 2, ///< Stop at the far end of the platform
	OSL_PLATFORM_THROUGH  = 3, ///< Load/unload through the platform
	OSL_END
};

/**
 * Reasons that could cause us to go to the depot.
 */
enum OrderDepotTypeFlags {
	ODTF_MANUAL          = 0,      ///< Manually initiated order.
	ODTFB_SERVICE        = 1 << 0, ///< This depot order is because of the servicing limit.
	ODTFB_PART_OF_ORDERS = 1 << 1, ///< This depot order is because of a regular order.
	ODTFB_BREAKDOWN      = 1 << 2, ///< This depot order is because of a breakdown.
};

/**
 * Actions that can be performed when the vehicle enters the depot.
 */
enum OrderDepotActionFlags {
	ODATF_SERVICE_ONLY   = 0,      ///< Only service the vehicle.
	ODATFB_HALT          = 1 << 0, ///< Service the vehicle and then halt it.
	ODATFB_NEAREST_DEPOT = 1 << 1, ///< Send the vehicle to the nearest depot.
	ODATFB_SELL          = 1 << 2, ///< Sell the vehicle on arrival at the depot.
	ODATFB_UNBUNCH       = 1 << 3, ///< Service the vehicle and then unbunch it.
};
DECLARE_ENUM_AS_BIT_SET(OrderDepotActionFlags)

/**
 * Extra depot flags.
 */
enum OrderDepotExtraFlags {
	ODEF_NONE           = 0,      ///< No flags.
	ODEFB_SPECIFIC      = 1 << 0, ///< This order is for a specific depot.
};
DECLARE_ENUM_AS_BIT_SET(OrderDepotExtraFlags)

/**
 * Flags for go to waypoint orders
 */
enum OrderWaypointFlags {
	OWF_DEFAULT          = 0,      ///< Default waypoint behaviour
	OWF_REVERSE          = 1 << 0, ///< Reverse train at the waypoint
};
DECLARE_ENUM_AS_BIT_SET(OrderWaypointFlags)

/**
 * Variables (of a vehicle) to 'cause' skipping on.
 */
enum OrderConditionVariable {
	OCV_LOAD_PERCENTAGE,    ///< Skip based on the amount of load
	OCV_RELIABILITY,        ///< Skip based on the reliability
	OCV_MAX_SPEED,          ///< Skip based on the maximum speed
	OCV_AGE,                ///< Skip based on the age
	OCV_REQUIRES_SERVICE,   ///< Skip when the vehicle requires service
	OCV_UNCONDITIONALLY,    ///< Always skip
	OCV_REMAINING_LIFETIME, ///< Skip based on the remaining lifetime
	OCV_MAX_RELIABILITY,    ///< Skip based on the maximum reliability
	OCV_CARGO_WAITING,      ///< Skip if specified cargo is waiting at station
	OCV_CARGO_ACCEPTANCE,   ///< Skip if specified cargo is accepted at station
	OCV_FREE_PLATFORMS,     ///< Skip based on free platforms at station
	OCV_PERCENT,            ///< Skip xx percent of times
	OCV_SLOT_OCCUPANCY,     ///< Test if vehicle slot is fully occupied, or empty
	OCV_VEH_IN_SLOT,        ///< Test if vehicle is in slot
	OCV_CARGO_LOAD_PERCENTAGE, ///< Skip based on the amount of load of a specific cargo
	OCV_CARGO_WAITING_AMOUNT,  ///< Skip based on the amount of a specific cargo waiting at station
	OCV_COUNTER_VALUE,      ///< Skip based on counter value
	OCV_TIME_DATE,          ///< Skip based on current time/date
	OCV_TIMETABLE,          ///< Skip based on timetable state
	OCV_DISPATCH_SLOT,      ///< Skip based on scheduled dispatch slot state
	OCV_CARGO_WAITING_AMOUNT_PERCENTAGE, ///< Skip based on the amount of a specific cargo waiting at station, relative to the vehicle capacity
	OCV_END
};

inline bool ConditionVariableHasStationID(OrderConditionVariable ocv)
{
	return ocv == OCV_CARGO_WAITING || ocv == OCV_CARGO_ACCEPTANCE || ocv == OCV_FREE_PLATFORMS || ocv == OCV_CARGO_WAITING_AMOUNT || ocv == OCV_CARGO_WAITING_AMOUNT_PERCENTAGE;
}

inline bool ConditionVariableTestsCargoWaitingAmount(OrderConditionVariable ocv)
{
	return ocv == OCV_CARGO_WAITING_AMOUNT || ocv == OCV_CARGO_WAITING_AMOUNT_PERCENTAGE;
}

/**
 * Comparator for the skip reasoning.
 */
enum OrderConditionComparator {
	OCC_EQUALS,      ///< Skip if both values are equal
	OCC_NOT_EQUALS,  ///< Skip if both values are not equal
	OCC_LESS_THAN,   ///< Skip if the value is less than the limit
	OCC_LESS_EQUALS, ///< Skip if the value is less or equal to the limit
	OCC_MORE_THAN,   ///< Skip if the value is more than the limit
	OCC_MORE_EQUALS, ///< Skip if the value is more or equal to the limit
	OCC_IS_TRUE,     ///< Skip if the variable is true
	OCC_IS_FALSE,    ///< Skip if the variable is false
	OCC_END
};


/**
 * Enumeration for the data to set in #CmdModifyOrder.
 */
enum ModifyOrderFlags : uint8_t {
	MOF_NON_STOP,        ///< Passes an OrderNonStopFlags.
	MOF_STOP_LOCATION,   ///< Passes an OrderStopLocation.
	MOF_UNLOAD,          ///< Passes an OrderUnloadType.
	MOF_LOAD,            ///< Passes an OrderLoadType
	MOF_DEPOT_ACTION,    ///< Selects the OrderDepotAction
	MOF_COND_VARIABLE,   ///< A conditional variable changes.
	MOF_COND_COMPARATOR, ///< A comparator changes.
	MOF_COND_VALUE,      ///< The value to set the condition to.
	MOF_COND_VALUE_2,    ///< The secondary value to set the condition to.
	MOF_COND_VALUE_3,    ///< The tertiary value to set the condition to.
	MOF_COND_VALUE_4,    ///< The quaternary value to set the condition to.
	MOF_COND_STATION_ID, ///< The station ID to set the condition to.
	MOF_COND_DESTINATION,///< Change the destination of a conditional order.
	MOF_WAYPOINT_FLAGS,  ///< Change the waypoint flags
	MOF_CARGO_TYPE_UNLOAD, ///< Passes an OrderUnloadType and a CargoID.
	MOF_CARGO_TYPE_LOAD,   ///< Passes an OrderLoadType and a CargoID.
	MOF_SLOT,            ///< Change the slot value
	MOF_RV_TRAVEL_DIR,   ///< Change the road vehicle travel direction.
	MOF_COUNTER_ID,      ///< Change the counter ID
	MOF_COUNTER_OP,      ///< Change the counter operation
	MOF_COUNTER_VALUE,   ///< Change the counter value
	MOF_COLOUR,          ///< Change the colour value
	MOF_LABEL_TEXT,      ///< Change the label text value
	MOF_DEPARTURES_SUBTYPE, ///< Change the label departures subtype
	MOF_END
};
template <> struct EnumPropsT<ModifyOrderFlags> : MakeEnumPropsT<ModifyOrderFlags, uint8_t, MOF_NON_STOP, MOF_END, MOF_END, 8> {};

/**
 * Depot action to switch to when doing a #MOF_DEPOT_ACTION.
 */
enum OrderDepotAction {
	DA_ALWAYS_GO, ///< Always go to the depot
	DA_SERVICE,   ///< Service only if needed
	DA_STOP,      ///< Go to the depot and stop there
	DA_UNBUNCH,   ///< Go to the depot and unbunch
	DA_SELL,      ///< Go to the depot and sell vehicle
	DA_END
};

/**
 * When to leave the station/waiting point.
 */
enum OrderLeaveType {
	OLT_NORMAL               = 0, ///< Leave when timetabled
	OLT_LEAVE_EARLY          = 1, ///< Leave as soon as possible
	OLT_LEAVE_EARLY_FULL_ANY = 2, ///< Leave as soon as possible, if any cargoes fully loaded
	OLT_LEAVE_EARLY_FULL_ALL = 3, ///< Leave as soon as possible, if all cargoes fully loaded
	OLT_END
};

enum OrderTimetableConditionMode {
	OTCM_LATENESS            = 0, ///< Test timetable lateness
	OTCM_EARLINESS           = 1, ///< Test timetable earliness
	OTCM_END
};

/**
 * Condition value field for OCV_DISPATCH_SLOT
 *  0                   1
 *  0 1 2 3 4 5 6 7 8 9 0
 * +-+-+-+-+-+-+-+-+-+-+-+
 * | |Src|         |Mode |
 * | |   |         |     |
 * +-+-+-+-+-+-+-+-+-+-+-+
 *
 * Mode = ODCM_FIRST_LAST
 *  0                   1
 *  0 1 2 3 4 5 6 7 8 9 0
 * +-+-+-+-+-+-+-+-+-+-+-+
 * |X|Src|         |Mode |
 * | |   |         |     |
 * +-+-+-+-+-+-+-+-+-+-+-+
 *  |
 * First/last slot bit
 *
 * Mode = OCDM_TAG
 *  0                   1
 *  0 1 2 3 4 5 6 7 8 9 0
 * +-+-+-+-+-+-+-+-+-+-+-+
 * | |Src| |Tag|   |Mode |
 * | |   | |   |   |     |
 * +-+-+-+-+-+-+-+-+-+-+-+
 *           |
 *           Slot tag
*/

enum OrderDispatchConditionBits {
	ODCB_SRC_START           = 1,
	ODCB_SRC_COUNT           = 2,
	ODCB_MODE_START          = 8,
	ODCB_MODE_COUNT          = 3,
};

enum OrderDispatchConditionSources : uint8_t {
	ODCS_BEGIN               = 0,
	ODCS_NEXT                = 0,
	ODCS_LAST                = 1,
	ODCS_VEH                 = 2,
	ODCS_END,
};

enum OrderDispatchConditionModes : uint8_t {
	ODCM_FIRST_LAST          = 0,
	OCDM_TAG                 = 1,
};

enum OrderDispatchFirstLastConditionBits {
	ODFLCB_LAST_SLOT         = 0,
};

enum OrderDispatchTagConditionBits {
	ODFLCB_TAG_START         = 4,
	ODFLCB_TAG_COUNT         = 2,
};

/**
 * Enumeration for the data to set in #CmdChangeTimetable.
 */
enum ModifyTimetableFlags : uint8_t {
	MTF_WAIT_TIME,    ///< Set wait time.
	MTF_TRAVEL_TIME,  ///< Set travel time.
	MTF_TRAVEL_SPEED, ///< Set max travel speed.
	MTF_SET_WAIT_FIXED,///< Set wait time fixed flag state.
	MTF_SET_TRAVEL_FIXED,///< Set travel time fixed flag state.
	MTF_SET_LEAVE_TYPE,///< Passes an OrderLeaveType.
	MTF_ASSIGN_SCHEDULE, ///< Assign a dispatch schedule.
	MTF_END
};
template <> struct EnumPropsT<ModifyTimetableFlags> : MakeEnumPropsT<ModifyTimetableFlags, uint8_t, MTF_WAIT_TIME, MTF_END, MTF_END, 3> {};


/** Clone actions. */
enum CloneOptions : uint8_t {
	CO_SHARE   = 0,
	CO_COPY    = 1,
	CO_UNSHARE = 2
};

struct Order;
struct OrderList;

#endif /* ORDER_TYPE_H */
