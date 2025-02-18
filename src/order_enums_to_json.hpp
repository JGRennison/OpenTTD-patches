/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file order_enums_to_json.hpp Maps conversions between export-relevant order enums and json. */

#ifndef ORDER_ENUMS_TO_JSON
#define ORDER_ENUMS_TO_JSON

#include "order_type.h"

#include "3rdparty/nlohmann/json.hpp"

NLOHMANN_JSON_SERIALIZE_ENUM(OrderNonStopFlags, {
	{ONSF_END,nullptr},
	{ONSF_STOP_EVERYWHERE, "go-to" },
	{ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, "go-nonstop-to"},
	{ONSF_NO_STOP_AT_DESTINATION_STATION, "go-via"},
	{ONSF_NO_STOP_AT_ANY_STATION, "go-nonstop-via"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderStopLocation, {
	{OSL_END,nullptr},
	{OSL_PLATFORM_NEAR_END,"near-end"},
	{OSL_PLATFORM_MIDDLE,"middle"},
	{OSL_PLATFORM_FAR_END,"far-end"},
	{OSL_PLATFORM_THROUGH,"through"}
})


NLOHMANN_JSON_SERIALIZE_ENUM(OrderWaypointFlags, {
	{OWF_DEFAULT,"default"},
	{OWF_REVERSE,"reverse"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(OrderLabelSubType, {
	{OLST_END,nullptr},
	{OLST_TEXT,"text"},
	{OLST_DEPARTURES_VIA,"show-departure-via"},
	{OLST_DEPARTURES_REMOVE_VIA,"rem-departure-via"}
})

// Temporary ordertypes omitted
NLOHMANN_JSON_SERIALIZE_ENUM(OrderType, {
	{OT_NOTHING,nullptr},
	{OT_GOTO_STATION,"go-to-station"},
	{OT_GOTO_DEPOT,"go-to-depot"},
	{OT_GOTO_WAYPOINT,"go-to-waypoint"},
	{OT_CONDITIONAL,"conditional"},
	{OT_IMPLICIT,"implicit"},
	{OT_SLOT,"slot"},
	{OT_COUNTER,"counter"},
	{OT_LABEL,"label"}
})

/*
* ODATFB_NEAREST_DEPOT is not treated as a part of the export - relevant data in this context.
* It is the only entry in OrderDepotActionFlags that justifies the enum being declared as a bitset
* and it is therefore the only element that can appear more then once, as such it will be treated separately
*/
NLOHMANN_JSON_SERIALIZE_ENUM(OrderDepotActionFlags, {

	{ODATF_SERVICE_ONLY,"service-only"},
	{ODATFB_HALT,"stop"},
	{ODATFB_SELL,"sell"},
	{ODATFB_UNBUNCH,"unbunch"},

})

#endif
