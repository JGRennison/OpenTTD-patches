/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_cmd.h Command definitions related to orders. */

#ifndef ORDER_CMD_H
#define ORDER_CMD_H

#include "command_type.h"
#include "order_base.h"
#include "order_type.h"

enum class ReverseOrderOperation : uint8_t {
	Reverse,
	AppendReversed,
};

struct InsertOrderCmdData final : public CommandPayloadSerialisable<InsertOrderCmdData> {
	VehicleID veh;
	VehicleOrderID sel_ord; // This may be INVALID_VEH_ORDER_ID to append to the end of the order list
	typename TupleTypeAdapter<decltype(std::declval<Order>().GetCmdRefTuple())>::Value new_order;

	InsertOrderCmdData() = default;
	InsertOrderCmdData(VehicleID veh, VehicleOrderID sel_ord, const Order &order) :
			veh(veh), sel_ord(sel_ord), new_order(const_cast<Order &>(order).GetCmdRefTuple()) {}

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(struct format_target &) const override;
};

DEF_CMD_TUPLE_LT (CMD_MODIFY_ORDER,             CmdModifyOrder,                     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID, ModifyOrderFlags, uint16_t, CargoType, std::string>)
DEF_CMD_TUPLE_LT (CMD_SKIP_TO_ORDER,            CmdSkipToOrder,                     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (CMD_DELETE_ORDER,             CmdDeleteOrder,                     {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_DIRECT_LT(CMD_INSERT_ORDER,             CmdInsertOrder,                     {}, CMDT_ROUTE_MANAGEMENT, InsertOrderCmdData)
DEF_CMD_TUPLE_LT (CMD_ORDER_REFIT,              CmdOrderRefit,                      {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID, CargoType>)
DEF_CMD_TUPLE_LT (CMD_CLONE_ORDER,              CmdCloneOrder,                      {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<CloneOptions, VehicleID, VehicleID>)
DEF_CMD_TUPLE_LT (CMD_INSERT_ORDERS_FROM_VEH,   CmdInsertOrdersFromVehicle,         {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (CMD_MOVE_ORDER,               CmdMoveOrder,                       {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID, VehicleOrderID, uint16_t>)
DEF_CMD_TUPLE_LT (CMD_REVERSE_ORDER_LIST,       CmdReverseOrderList,                {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, ReverseOrderOperation>)
DEF_CMD_TUPLE_LT (CMD_DUPLICATE_ORDER,          CmdDuplicateOrder,                  {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (CMD_SET_ROUTE_OVERLAY_COLOUR, CmdSetRouteOverlayColour,           {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<VehicleID, Colours>)
DEF_CMD_TUPLE_NT (CMD_MASS_CHANGE_ORDER,        CmdMassChangeOrder,                 {}, CMDT_ROUTE_MANAGEMENT, CmdDataT<DestinationID, VehicleType, OrderType, CargoType, DestinationID>)
DEF_CMD_TUPLE    (CMD_CLEAR_ORDER_BACKUP,       CmdClearOrderBackup,     CMD_CLIENT_ID, CMDT_SERVER_SETTING,   CmdDataT<ClientID>)

struct BulkOrderCmdData final : public CommandPayloadSerialisable<BulkOrderCmdData> {
	VehicleID veh;
	std::vector<uint8_t> cmds;

	void Serialise(BufferSerialisationRef buffer) const override;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const override;
};

DEF_CMD_DIRECT_NT(CMD_BULK_ORDER,         CmdBulkOrder,              CMD_NO_TEST, CMDT_ROUTE_MANAGEMENT, BulkOrderCmdData)

#endif /* ORDER_CMD_H */
