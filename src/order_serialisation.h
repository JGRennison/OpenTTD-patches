/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_serialisation.h Handling of order serialisation and deserialisation to/from JSON. */

#ifndef ORDER_SERIALISATION_H
#define ORDER_SERIALISATION_H

#include "order_type.h"
#include "vehicle_type.h"
#include "gfx_type.h"

#include "3rdparty/robin_hood/robin_hood.h"

#include <string_view>

enum JsonOrderImportErrorType : uint8_t {
	JOIET_OK,           ///< Used to suppress errors / check no error occured
	JOIET_MINOR,        ///< A cosmetic attribute of the order was malformed
	JOIET_MAJOR,        ///< An important part of the order was malformed, but it was not strictly required for creating the order
	JOIET_CRITICAL,     ///< Makes building an order completely impossible, in these cases the order is replaced by a label
};

struct OrderImportErrors {
	struct Error {
		std::string msg;
		JsonOrderImportErrorType type;
	};

	std::vector<Error> global;
	robin_hood::unordered_map<VehicleOrderID, std::vector<Error>> order;
	robin_hood::unordered_map<int, std::vector<Error>> schedule;

	bool HasErrors() const;
};

OrderImportErrors ImportJsonOrderList(const Vehicle *veh, std::string_view json_str, VehicleOrderID insert_index = INVALID_VEH_ORDER_ID, bool reverse_orders = false);
std::string OrderListToJSONString(const OrderList *ol);

Colours OrderErrorTypeToColour(JsonOrderImportErrorType error_type);
void ShowOrderListImportErrorsWindow(const Vehicle *v, OrderImportErrors &&errors);

#endif /* ORDER_SERIALISATION_H */
