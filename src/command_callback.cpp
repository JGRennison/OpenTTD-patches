/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_callback.cpp Command callback table definitions. */

#include "stdafx.h"
#include "command_type.h"
#include "debug.h"

#include "company_cmd.h"
#include "group_cmd.h"
#include "order_cmd.h"
#include "rail_cmd.h"
#include "road_cmd.h"
#include "station_cmd.h"
#include "tbtr_template_vehicle_cmd.h"
#include "timetable_cmd.h"
#include "tunnelbridge_cmd.h"
#include "vehicle_cmd.h"
#include "waypoint_cmd.h"

#include "safeguards.h"

/**
 * Define a callback function for the client, after the command is finished.
 *
 * Functions of this type are called after the command is finished.
 *
 * @param result The result of the executed command
 * @param cmd Executed command ID
 * @param tile The tile of the command action
 * @param payload Command payload
 */
using GeneralCommandCallback = void(const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param);
using ResultTileCommandCallback = void(const CommandCost &result, TileIndex tile);
using ResultCommandCallback = void(const CommandCost &result);

template <typename T>
using ResultPayloadCommandCallback = void(const CommandCost &result, const T &payload);

using CommandCallbackTrampoline = bool(const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param);

template <CommandCallback Tcb> struct CommandCallbackTraits;

#define DEF_CB_GENERAL(cb_) \
GeneralCommandCallback Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		Cc ## cb_(result, cmd, tile, payload, param); \
		return true; \
	}; \
};

#define DEF_CB_RES_TILE(cb_) \
ResultTileCommandCallback Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		Cc ## cb_(result, tile); \
		return true; \
	}; \
};

#define DEF_CB_RES(cb_) \
ResultCommandCallback Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		Cc ## cb_(result); \
		return true; \
	}; \
};

#define DEF_CB_RES_PAYLOAD(cb_, cmd_) \
ResultPayloadCommandCallback<CmdPayload<cmd_>> Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		Cc ## cb_(result, static_cast<const CmdPayload<cmd_> &>(payload)); \
		return true; \
	}; \
};

template <Commands Tcmd, typename S> struct CommandCallbackTupleHelper;

template <Commands Tcmd, typename... Targs>
struct CommandCallbackTupleHelper<Tcmd, std::tuple<Targs...>> {
	using ResultTupleCommandCallback = void(const CommandCost &, typename CommandProcTupleAdapter::with_ref_params<std::remove_cvref_t<Targs>>...);
	using ResultTileTupleCommandCallback = void(const CommandCost &, TileIndex, typename CommandProcTupleAdapter::with_ref_params<std::remove_cvref_t<Targs>>...);

	static inline bool ResultExecute(ResultTupleCommandCallback *cb, Commands cmd, const CommandCost &result, const CommandPayloadBase &payload)
	{
		if (cmd != Tcmd) return false;
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			cb(result, std::get<Tindices>(static_cast<const CmdPayload<Tcmd> &>(payload).GetValues())...);
		};
		handler(std::index_sequence_for<Targs...>{});
		return true;
	}

	static inline bool ResultTileExecute(ResultTileTupleCommandCallback *cb, Commands cmd, const CommandCost &result, TileIndex tile, const CommandPayloadBase &payload)
	{
		if (cmd != Tcmd) return false;
		auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) {
			cb(result, tile, std::get<Tindices>(static_cast<const CmdPayload<Tcmd> &>(payload).GetValues())...);
		};
		handler(std::index_sequence_for<Targs...>{});
		return true;
	}
};

#define DEF_CB_RES_TUPLE(cb_, cmd_) \
namespace cmd_detail { using cc_helper_ ## cb_ = CommandCallbackTupleHelper<cmd_, std::remove_cvref_t<decltype(std::declval<CmdPayload<cmd_>>().GetValues())>>; } \
typename cmd_detail::cc_helper_ ## cb_ ::ResultTupleCommandCallback Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		return cmd_detail::cc_helper_ ## cb_ ::ResultExecute(Cc ## cb_, cmd, result, payload); \
	}; \
};

#define DEF_CB_RES_TILE_TUPLE(cb_, cmd_) \
namespace cmd_detail { using cc_helper_ ## cb_ = CommandCallbackTupleHelper<cmd_, std::remove_cvref_t<decltype(std::declval<CmdPayload<cmd_>>().GetValues())>>; } \
typename cmd_detail::cc_helper_ ## cb_ ::ResultTileTupleCommandCallback Cc ## cb_; \
template <> struct CommandCallbackTraits<CommandCallback::cb_> { \
	static constexpr CommandCallbackTrampoline *handler = [](const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param) { \
		return cmd_detail::cc_helper_ ## cb_ ::ResultTileExecute(Cc ## cb_, cmd, result, tile, payload); \
	}; \
};

DEF_CB_RES(BuildPrimaryVehicle)
DEF_CB_RES_TILE(BuildAirport)
DEF_CB_RES_TILE_TUPLE(BuildBridge, CMD_BUILD_BRIDGE)
DEF_CB_RES_TILE(PlaySound_CONSTRUCTION_WATER)
DEF_CB_RES_TILE(BuildDocks)
DEF_CB_RES_TILE(FoundTown)
DEF_CB_RES_TILE(BuildRoadTunnel)
DEF_CB_RES_TILE(BuildRailTunnel)
DEF_CB_RES_TILE(BuildWagon)
DEF_CB_RES_TILE_TUPLE(RoadDepot, CMD_BUILD_ROAD_DEPOT)
DEF_CB_RES_TILE_TUPLE(RailDepot, CMD_BUILD_TRAIN_DEPOT)
DEF_CB_RES(PlaceSign)
DEF_CB_RES_TILE(PlaySound_EXPLOSION)
DEF_CB_RES_TILE(PlaySound_CONSTRUCTION_OTHER)
DEF_CB_RES_TILE(PlaySound_CONSTRUCTION_RAIL)
DEF_CB_RES_TILE(Station)
DEF_CB_RES_TILE(Terraform)
DEF_CB_GENERAL(AI)
DEF_CB_RES(CloneVehicle)
DEF_CB_RES_TUPLE(GiveMoney, CMD_GIVE_MONEY)
DEF_CB_RES_TUPLE(CreateGroup, CMD_CREATE_GROUP)
DEF_CB_RES(FoundRandomTown)
DEF_CB_RES_TILE_TUPLE(RoadStop, CMD_BUILD_ROAD_STOP)
DEF_CB_RES_TUPLE(StartStopVehicle, CMD_START_STOP_VEHICLE)
DEF_CB_GENERAL(Game)
DEF_CB_RES(AddVehicleNewGroup)
DEF_CB_RES_PAYLOAD(InsertOrder, CMD_INSERT_ORDER)
DEF_CB_RES_TUPLE(InsertOrdersFromVehicle, CMD_INSERT_ORDERS_FROM_VEH)
DEF_CB_RES(AddPlan)
DEF_CB_RES_TUPLE(MoveStationName, CMD_MOVE_STATION_NAME)
DEF_CB_RES_TUPLE(MoveWaypointName, CMD_MOVE_WAYPOINT_NAME)
DEF_CB_RES(SetVirtualTrain)
DEF_CB_RES(VirtualTrainWagonsMoved)
DEF_CB_RES_TUPLE(DeleteVirtualTrain, CMD_SELL_VIRTUAL_VEHICLE)
DEF_CB_RES(AddVirtualEngine)
DEF_CB_RES(MoveNewVirtualEngine)
DEF_CB_RES_TUPLE(AddNewSchDispatchSchedule, CMD_SCH_DISPATCH_ADD_NEW_SCHEDULE)
DEF_CB_RES_TUPLE(SwapSchDispatchSchedules, CMD_SCH_DISPATCH_SWAP_SCHEDULES)
DEF_CB_RES_TUPLE(AdjustSchDispatch, CMD_SCH_DISPATCH_ADJUST)
DEF_CB_RES_TUPLE(AdjustSchDispatchSlot, CMD_SCH_DISPATCH_ADJUST_SLOT)
DEF_CB_RES(CreateTraceRestrictSlot)
DEF_CB_RES(CreateTraceRestrictCounter)

template <size_t... i>
inline constexpr auto MakeCommandCallbackTable(std::index_sequence<i...>) noexcept {
	return std::array<CommandCallbackTrampoline *, sizeof...(i)>{{ CommandCallbackTraits<static_cast<CommandCallback>(i + 1)>::handler... }};
}

/**
 * The master callback table
 *
 * No entry for CommandCallback::None, so length reduced by 1.
 */
static constexpr auto _command_callback_table = MakeCommandCallbackTable(std::make_index_sequence<static_cast<size_t>(CommandCallback::End) - 1>{});

void ExecuteCommandCallback(CommandCallback callback, CallbackParameter callback_param, const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload)
{
	if (callback != CommandCallback::None && to_underlying(callback) < to_underlying(CommandCallback::End)) {
		if (_command_callback_table[to_underlying(callback) - 1](result, cmd, tile, payload, callback_param)) return;
	}

	Debug(misc, 0, "Failed to execute callback: {}, {}", callback, payload);
}
