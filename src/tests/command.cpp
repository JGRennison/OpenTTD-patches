/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file command.cpp Test command serialisation. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"
#include "../core/serialisation.hpp"

#include "../core/format.hpp"

#include "../company_cmd.h"
#include "../misc_cmd.h"
#include "../plans_cmd.h"
#include "../vehicle_cmd.h"

#include "../3rdparty/fmt/ranges.h"
#include "../3rdparty/fmt/std.h"

template <Commands Cmd, typename F, typename E = std::monostate>
bool TestGeneralCommandPayload(const CmdPayload<Cmd> &src_payload, F payload_checker, E payload_equality = {})
{
	static constexpr TileIndex TEST_TILE{0x12345678};
	static constexpr StringID TEST_ERROR{0xcafe};

	using PayloadType = CmdPayload<Cmd>;
	const DynBaseCommandContainer src_cmd(Cmd, TEST_ERROR, TEST_TILE, src_payload.Clone());

	std::vector<uint8_t> data;
	BufferSerialisationRef buffer(data);
	src_cmd.Serialise(buffer);
	CHECK(data.size() >= 10); // Commands, error message, tile, payload size

	const uint16_t payload_size = data[8] + (data[9] << 8);
	CHECK(data.size() == payload_size + 10u);

	std::span<const uint8_t> payload_data(data.begin() + 10, data.end());
	if (!payload_checker(payload_data)) return false;

	DeserialisationBuffer deserialise(data.data(), data.size());
	DynBaseCommandContainer recv_cmd{};
	const char *err = recv_cmd.Deserialise(deserialise);
	if (err != nullptr) {
		WARN(fmt::format("DynBaseCommandContainer.Deserialise: error: {}", err));
		return false;
	}
	bool bytes_left = deserialise.CanRecvBytes(1, false);
	if (deserialise.error || bytes_left) {
		WARN(fmt::format("Deserialise: error: {}, bytes left: {}", deserialise.error, bytes_left));
		return false;
	}

	CHECK(recv_cmd.cmd == src_cmd.cmd);
	CHECK(recv_cmd.error_msg == src_cmd.error_msg);
	CHECK(recv_cmd.tile == src_cmd.tile);
	CHECK(recv_cmd.payload != nullptr);
	CHECK(recv_cmd.payload->IsType<PayloadType>());

	const PayloadType *recv_payload = recv_cmd.payload->AsType<CmdPayload<Cmd>>();

	if constexpr (std::is_same_v<E, std::monostate>) {
		CHECK(*recv_payload == src_payload);
	} else {
		CHECK(payload_equality(*recv_payload, src_payload));
	}

	return true;
}

struct PayloadChecker {
	std::initializer_list<uint8_t> expected;

	bool operator()(std::span<const uint8_t> payload_data) const
	{
		if (payload_data.size() != expected.size() || !std::equal(payload_data.begin(), payload_data.end(), expected.begin(), expected.end())) {
			WARN(fmt::format("Serialise: {:X} != {:X}", fmt::join(payload_data, ", "), fmt::join(expected, ", ")));
			return false;
		}
		return true;
	}
};

template <Commands Cmd>
bool TestCommandPayload(const typename CmdPayload<Cmd>::Tuple &values, std::initializer_list<uint8_t> expected)
{
	return TestGeneralCommandPayload<Cmd>(CmdPayload<Cmd>(values), PayloadChecker{expected});
}

TEST_CASE("CmdDataT simple tests")
{
	CHECK(CmdPayload<CMD_REMOVE_PLAN>::Make(PlanID{5}) == CmdPayload<CMD_REMOVE_PLAN>::Make(PlanID{5}));
	CHECK(CmdPayload<CMD_REMOVE_PLAN>::Make(PlanID{5}) != CmdPayload<CMD_REMOVE_PLAN>::Make(PlanID{6}));

	CHECK(TestCommandPayload<CMD_ADD_PLAN>({}, {}));
	CHECK(TestCommandPayload<CMD_REMOVE_PLAN>({ PlanID{5} }, { 5, 0 }));
	CHECK(TestCommandPayload<CMD_RENAME_PLAN>({ PlanID{6}, "abc" }, { 6, 0, 0x61, 0x62, 0x63, 0 }));
	CHECK(TestCommandPayload<CMD_CHANGE_PLAN_VISIBILITY>({ PlanID{7}, true }, { 7, 0, 1 }));
	CHECK(TestCommandPayload<CMD_START_STOP_VEHICLE>({ VehicleID{8}, true }, { 8, 1 }));
	CHECK(TestCommandPayload<CMD_MONEY_CHEAT>({ 0 }, { 0 }));
	CHECK(TestCommandPayload<CMD_MONEY_CHEAT>({ -1 }, { 1 }));
	CHECK(TestCommandPayload<CMD_MONEY_CHEAT>({ 1 }, { 2 }));
	CHECK(TestCommandPayload<CMD_MONEY_CHEAT>({ 1000000 }, { 0xDE, 0x84, 0x80 }));
}

TEST_CASE("TupleRefCmdData tests")
{
	CmdCompanyCtrlData payload = CmdCompanyCtrlData::Make(CCA_NEW_AI, CompanyID{2}, CompanyRemoveReason{3}, {}, CompanyID{5});
	CHECK(TestGeneralCommandPayload<CMD_COMPANY_CTRL>(payload, PayloadChecker{{ 1, 2, 3, 0, 5 }}));

	CmdCompanyCtrlData payload2 = payload;
	SetPreCheckedCommandPayloadClientID(CMD_COMPANY_CTRL, payload2, ClientID{4});
	CHECK(payload2 != payload);
	CHECK(TestGeneralCommandPayload<CMD_COMPANY_CTRL>(payload2, PayloadChecker{{ 1, 2, 3, 4, 5 }}));
}

