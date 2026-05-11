/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
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
	{OrderStopLocation::End, nullptr},
	{OrderStopLocation::NearEnd, "near-end"},
	{OrderStopLocation::Middle, "middle"},
	{OrderStopLocation::FarEnd, "far-end"},
	{OrderStopLocation::Through, "through"}
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

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLoadType, {
	{static_cast<OrderLoadType>(-1), nullptr},
	{OrderLoadType::LoadIfPossible, "normal"},
	{OrderLoadType::FullLoad, "full-load"},
	{OrderLoadType::FullLoadAny, "full-load-any"},
	{OrderLoadType::NoLoad, "no-load"}
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
	{Colours::Invalid, nullptr},
	{Colours::DarkBlue, "dark-blue"},
	{Colours::PaleGreen, "pale-green"},
	{Colours::Pink, "pink"},
	{Colours::Yellow, "yellow"},
	{Colours::Red, "red"},
	{Colours::LightBlue, "light-blue"},
	{Colours::Green, "green"},
	{Colours::DarkGreen, "dark-green"},
	{Colours::Blue, "blue"},
	{Colours::Cream, "cream"},
	{Colours::Mauve, "mauve"},
	{Colours::Purple, "purple"},
	{Colours::Orange, "orange"},
	{Colours::Brown, "brown"},
	{Colours::Grey, "grey"},
	{Colours::White, "white"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(VehicleType, {
	{VehicleType::End, nullptr},
	{VehicleType::Train, "train"},
	{VehicleType::Road, "road"},
	{VehicleType::Ship, "ship"},
	{VehicleType::Aircraft, "aircraft"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderConditionVariable, {
	{OrderConditionVariable::End, nullptr},
	{OrderConditionVariable::LoadPercentage, "load-percentage"},
	{OrderConditionVariable::Reliability, "reliability"},
	{OrderConditionVariable::MaxSpeed, "max-speed"},
	{OrderConditionVariable::Age, "age"},
	{OrderConditionVariable::RequiresService, "requires-service"},
	{OrderConditionVariable::Unconditionally, "always"},
	{OrderConditionVariable::RemainingLifetime, "remaining-lifetime"},
	{OrderConditionVariable::MaxReliability, "max-reliability" },
	{OrderConditionVariable::CargoWaiting, "cargo-waiting" },
	{OrderConditionVariable::CargoAcceptance, "cargo-acceptance" },
	{OrderConditionVariable::FreePlatforms, "free-platforms" },
	{OrderConditionVariable::Percent, "percent-of-times" },
	{OrderConditionVariable::SlotOccupancy, "slot-occupancy" },
	{OrderConditionVariable::VehicleInSlot, "vehicle-in-slot" },
	{OrderConditionVariable::CargoLoadPercentage, "cargo-load-percentage" },
	{OrderConditionVariable::CargoWaitingAmount, "cargo-waiting-amount" },
	{OrderConditionVariable::CounterValue, "counter-value" },
	{OrderConditionVariable::TimeDate, "time-date" },
	{OrderConditionVariable::Timetable, "timetable" },
	{OrderConditionVariable::DispatchSlot, "dispatch-slot" },
	{OrderConditionVariable::CargoWaitingAmountPercentage, "cargo-waiting-amount-percentage" },
	{OrderConditionVariable::VehicleInSlotGroup, "vehicle-in-slot-group" }
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderConditionComparator, {
	{OrderConditionComparator::End, nullptr},
	{OrderConditionComparator::Equal, "=="},
	{OrderConditionComparator::NotEqual, "!="},
	{OrderConditionComparator::LessThan, "<"},
	{OrderConditionComparator::LessThanOrEqual, "<="},
	{OrderConditionComparator::MoreThan, ">"},
	{OrderConditionComparator::MoreThanOrEqual, ">="},
	{OrderConditionComparator::IsTrue, "true"},
	{OrderConditionComparator::IsFalse, "false"}
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
