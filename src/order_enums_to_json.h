/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file order_enums_to_json.h Maps conversions between export-relevant order enums and json. */

#ifndef ORDER_ENUMS_TO_JSON
#define ORDER_ENUMS_TO_JSON

#include "order_type.h"
#include "direction_type.h"
#include "gfx_type.h"

#include "3rdparty/nlohmann/json.hpp"

NLOHMANN_JSON_SERIALIZE_ENUM(OrderNonStopFlags, {
	{ONSF_END, nullptr},
	{ONSF_STOP_EVERYWHERE, "go-to" },
	{ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, "go-nonstop-to"},
	{ONSF_NO_STOP_AT_DESTINATION_STATION, "go-via"},
	{ONSF_NO_STOP_AT_ANY_STATION, "go-nonstop-via"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderStopLocation, {
	{OSL_END, nullptr},
	{OSL_PLATFORM_NEAR_END, "near-end"},
	{OSL_PLATFORM_MIDDLE, "middle"},
	{OSL_PLATFORM_FAR_END, "far-end"},
	{OSL_PLATFORM_THROUGH, "through"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLabelSubType, {
	{static_cast<OrderLabelSubType>(-1), nullptr},
	{OLST_TEXT, "text"},
	{OLST_DEPARTURES_VIA, "show-departure-via"},
	{OLST_DEPARTURES_REMOVE_VIA, "rem-departure-via"},
	{OLST_ERROR, "error"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLabelError, {
	{static_cast<OrderLabelError>(-1), nullptr},
	{OrderLabelError::Default, "default"},
	{OrderLabelError::ParseError, "parse-error"}
})

/* Temporary ordertypes omitted */
NLOHMANN_JSON_SERIALIZE_ENUM(OrderType, {
	{OT_NOTHING, nullptr},
	{OT_GOTO_STATION, "go-to-station"},
	{OT_GOTO_DEPOT, "go-to-depot"},
	{OT_GOTO_WAYPOINT, "go-to-waypoint"},
	{OT_CONDITIONAL, "conditional"},
	{OT_IMPLICIT, "implicit"},
	{OT_SLOT, "slot"},
	{OT_SLOT_GROUP, "slot-group"},
	{OT_COUNTER, "counter"},
	{OT_LABEL, "label"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderDepotAction, {
	{DA_END, nullptr},
	{DA_SERVICE, "service-only"},
	{DA_STOP, "stop"},
	{DA_SELL, "sell"},
	{DA_UNBUNCH, "unbunch"},
	{DA_ALWAYS_GO, "always-go"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLoadFlags, {
	{static_cast<OrderLoadFlags>(-1), nullptr},
	{OLF_LOAD_IF_POSSIBLE, "normal"},
	{OLFB_FULL_LOAD, "full-load"},
	{OLF_FULL_LOAD_ANY, "full-load-any"},
	{OLFB_NO_LOAD, "no-load"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLeaveType, {
	{OLT_END, nullptr},
	{OLT_NORMAL, "normal"},
	{OLT_LEAVE_EARLY, "leave-early"},
	{OLT_LEAVE_EARLY_FULL_ANY, "leave-early-if-any-cargo-full"},
	{OLT_LEAVE_EARLY_FULL_ALL, "leave-early-if-all-cargo-full"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderUnloadFlags, {
	{static_cast<OrderUnloadFlags>(-1), nullptr},
	{OUF_UNLOAD_IF_POSSIBLE, "normal"},
	{OUFB_UNLOAD, "unload"},
	{OUFB_UNLOAD, "unload-and-leave-empty"}, // Import only
	{OUFB_TRANSFER, "transfer"},
	{OUFB_NO_UNLOAD, "no-unload"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(DiagDirection, {
	{INVALID_DIAGDIR, nullptr},
	{DIAGDIR_NE, "north-east"},
	{DIAGDIR_SE, "south-east"},
	{DIAGDIR_NW, "north-west"},
	{DIAGDIR_SW, "south-west"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(Colours, {
	{INVALID_COLOUR, nullptr},
	{COLOUR_DARK_BLUE, "dark-blue"},
	{COLOUR_PALE_GREEN, "pale-green"},
	{COLOUR_PINK, "pink"},
	{COLOUR_YELLOW, "yellow"},
	{COLOUR_RED, "red"},
	{COLOUR_LIGHT_BLUE, "light-blue"},
	{COLOUR_GREEN, "green"},
	{COLOUR_DARK_GREEN, "dark-green"},
	{COLOUR_BLUE, "blue"},
	{COLOUR_CREAM, "cream"},
	{COLOUR_MAUVE, "mauve"},
	{COLOUR_PURPLE, "purple"},
	{COLOUR_ORANGE, "orange"},
	{COLOUR_BROWN, "brown"},
	{COLOUR_GREY, "grey"},
	{COLOUR_WHITE, "white"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(VehicleType, {
	{VEH_END, nullptr},
	{VEH_TRAIN, "train"},
	{VEH_ROAD, "road"},
	{VEH_SHIP, "ship"},
	{VEH_AIRCRAFT, "aircraft"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderConditionVariable, {
	{OCV_END, nullptr},
	{OCV_LOAD_PERCENTAGE, "load-percentage"},
	{OCV_RELIABILITY, "reliability"},
	{OCV_MAX_SPEED, "max-speed"},
	{OCV_AGE, "age"},
	{OCV_REQUIRES_SERVICE, "requires-service"},
	{OCV_UNCONDITIONALLY, "always"},
	{OCV_REMAINING_LIFETIME, "remaining-lifetime"},
	{OCV_MAX_RELIABILITY, "max-reliability" },
	{OCV_CARGO_WAITING, "cargo-waiting" },
	{OCV_CARGO_ACCEPTANCE, "cargo-acceptance" },
	{OCV_FREE_PLATFORMS, "free-platforms" },
	{OCV_PERCENT, "percent-of-times" },
	{OCV_SLOT_OCCUPANCY, "slot-occupancy" },
	{OCV_VEH_IN_SLOT, "vehicle-in-slot" },
	{OCV_CARGO_LOAD_PERCENTAGE, "cargo-load-percentage" },
	{OCV_CARGO_WAITING_AMOUNT, "cargo-waiting-amount" },
	{OCV_COUNTER_VALUE, "counter-value" },
	{OCV_TIME_DATE, "time-date" },
	{OCV_TIMETABLE, "timetable" },
	{OCV_DISPATCH_SLOT, "dispatch-slot" },
	{OCV_CARGO_WAITING_AMOUNT_PERCENTAGE, "cargo-waiting-amount-percentage" },
	{OCV_VEH_IN_SLOT_GROUP, "vehicle-in-slot-group" }
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderConditionComparator, {
	{OCC_END, nullptr},
	{OCC_EQUALS, "=="},
	{OCC_NOT_EQUALS, "!="},
	{OCC_LESS_THAN, "<"},
	{OCC_LESS_EQUALS, "<="},
	{OCC_MORE_THAN, ">"},
	{OCC_MORE_EQUALS, ">="},
	{OCC_IS_TRUE, "true"},
	{OCC_IS_FALSE, "false"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderSlotSubType, {
	{static_cast<OrderSlotSubType>(-1), nullptr},
	{OSST_RELEASE, "release"},
	{OSST_TRY_ACQUIRE, "try-acquire"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderDispatchConditionSources, {
	{static_cast<OrderDispatchConditionSources>(-1), nullptr},
	{ODCS_NEXT, "next"},
	{ODCS_LAST, "last"},
	{ODCS_VEH,  "vehicle"}
})

#endif
