/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command_table.cpp Command table definition. */

#define CMD_DEFINE

#include "stdafx.h"
#include "command_serialisation.h"
#include "command_table.h"
#include "3rdparty/fmt/std.h"

#include "autoreplace_cmd.h"
#include "company_cmd.h"
#include "depot_cmd.h"
#include "engine_cmd.h"
#include "goal_cmd.h"
#include "group_cmd.h"
#include "industry_cmd.h"
#include "landscape_cmd.h"
#include "league_cmd.h"
#include "misc_cmd.h"
#include "news_cmd.h"
#include "object_cmd.h"
#include "order_cmd.h"
#include "plans_cmd.h"
#include "programmable_signals_cmd.h"
#include "rail_cmd.h"
#include "road_cmd.h"
#include "settings_cmd.h"
#include "signs_cmd.h"
#include "station_cmd.h"
#include "story_cmd.h"
#include "subsidy_cmd.h"
#include "tbtr_template_vehicle_cmd.h"
#include "terraform_cmd.h"
#include "timetable_cmd.h"
#include "town_cmd.h"
#include "tracerestrict_cmd.h"
#include "train_cmd.h"
#include "tree_cmd.h"
#include "tunnelbridge_cmd.h"
#include "vehicle_cmd.h"
#include "viewport_cmd.h"
#include "water_cmd.h"
#include "waypoint_cmd.h"

template <typename T, CommandProcDirect<T> proc, bool no_tile>
static constexpr CommandExecTrampoline *MakeTrampoline()
{
	return [](const CommandExecData &exec_data) -> CommandCost
	{
		const T &data = static_cast<const T &>(exec_data.payload);
		return proc(exec_data.flags, exec_data.tile, data);
	};
}

template <typename T, CommandProcDirectNoTile<T> proc, bool no_tile>
static constexpr CommandExecTrampoline *MakeTrampoline()
{
	return [](const CommandExecData &exec_data) -> CommandCost
	{
		const T &data = static_cast<const T &>(exec_data.payload);
		return proc(exec_data.flags, data);
	};
}

template <bool no_tile, typename F, typename T, size_t... Tindices>
CommandCost CommandExecTrampolineTuple(F proc, TileIndex tile, DoCommandFlags flags, const T &payload, std::index_sequence<Tindices...>)
{
	if constexpr (no_tile) {
		return proc(flags, std::get<Tindices>(payload.GetValues())...);
	} else {
		return proc(flags, tile, std::get<Tindices>(payload.GetValues())...);
	}
}

template <typename T, auto &proc, bool no_tile, typename = std::enable_if_t<PayloadHasBaseTupleCmdDataTag<T>>>
static constexpr CommandExecTrampoline *MakeTrampoline()
{
	return [](const CommandExecData &exec_data) -> CommandCost
	{
		const T &data = static_cast<const T &>(exec_data.payload);
		return CommandExecTrampolineTuple<no_tile>(proc, exec_data.tile, exec_data.flags, data, std::make_index_sequence<std::tuple_size_v<typename T::Tuple>>{});
	};
}

template <typename T>
static constexpr CommandPayloadDeserialiser *MakePayloadDeserialiser()
{
	return [](DeserialisationBuffer &buffer, StringValidationSettings default_string_validation) -> std::unique_ptr<CommandPayloadBase>
	{
		auto payload = std::make_unique<T>();
		if (!payload->Deserialise(buffer, default_string_validation)) payload = nullptr;
		return payload;
	};
}

/* Helpers to generate the master command table from the command traits. */
template <typename T, typename H>
inline constexpr CommandInfo CommandFromTrait() noexcept
{
	using Payload = typename T::PayloadType;
	static_assert(std::is_final_v<Payload>);
	return { MakeTrampoline<Payload, H::proc, T::output_no_tile>(), MakePayloadDeserialiser<Payload>(), typeid(Payload), H::name, T::flags, T::type, T::output_no_tile ? CIF_NO_OUTPUT_TILE : CIF_NONE };
};

template <typename T, T... i>
inline constexpr auto MakeCommandsFromTraits(std::integer_sequence<T, i...>) noexcept {
	return std::array<CommandInfo, sizeof...(i)>{{ CommandFromTrait<CommandTraits<static_cast<Commands>(i)>, CommandHandlerTraits<static_cast<Commands>(i)>>()... }};
}

/**
 * The master command table
 *
 * This table contains the CommandInfo for all possible commands.
 */
const std::array<CommandInfo, to_underlying(CMD_END)> _command_proc_table = MakeCommandsFromTraits(std::make_integer_sequence<std::underlying_type_t<Commands>, CMD_END>{});

/**
 * Set client ID for this command payload using the field returned by Payload::GetClientIDField().
 * This provided payload must have already been type-checked as valid for cmd.
 * Not many commands set CMD_CLIENT_ID so a series of ifs is not too onerous.
 */
void SetPreCheckedCommandPayloadClientID(Commands cmd, CommandPayloadBase &payload, ClientID client_id)
{
	static_assert(INVALID_CLIENT_ID == (ClientID)0);

	auto cmd_check = [&]<Commands Tcmd>() -> bool {
		if constexpr (CommandTraits<Tcmd>::flags.Test(CommandFlag::ClientID)) {
			if (cmd == Tcmd) {
				SetCommandPayloadClientID(static_cast<CmdPayload<Tcmd> &>(payload), client_id);
				return true;
			}
		}
		return false;
	};

	using Tseq = std::underlying_type_t<Commands>;
	auto cmd_loop = [&]<Tseq... Tindices>(std::integer_sequence<Tseq, Tindices...>) {
		(cmd_check.template operator()<static_cast<Commands>(Tindices)>() || ...);
	};
	cmd_loop(std::make_integer_sequence<Tseq, static_cast<Tseq>(CMD_END)>{});
}
