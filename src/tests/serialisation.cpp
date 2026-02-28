/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file serialisation.cpp Test functionality from core/serialisation. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"
#include "../core/serialisation.hpp"
#include "../core/format_variant.hpp"

#include "../core/format.hpp"
#include "../3rdparty/fmt/ranges.h"
#include "../3rdparty/fmt/std.h"

template <typename T>
bool TestGenericSerialisation(T value, std::initializer_list<uint8_t> expected)
{
	std::vector<uint8_t> data;
	BufferSerialisationRef buffer(data);
	buffer.Send_generic(value);
	if (!std::equal(data.begin(), data.end(), expected.begin(), expected.end())) {
		WARN(fmt::format("Serialise: {:X} != {:X}", fmt::join(data, ", "), fmt::join(expected, ", ")));
		return false;
	}

	DeserialisationBuffer deserialise(data.data(), data.size());
	T recv{};
	deserialise.Recv_generic(recv);
	bool bytes_left = deserialise.CanRecvBytes(1, false);
	if (deserialise.error || bytes_left || value != recv) {
		WARN(fmt::format("Deserialise: error: {}, bytes left: {}, {} --> {}", deserialise.error, bytes_left, value, recv));
		return false;
	}

	return true;
}

TEST_CASE("Generic integer")
{
	CHECK(TestGenericSerialisation<uint8_t>(0, { 0 }));
	CHECK(TestGenericSerialisation<uint8_t>(0xFF, { 0xFF }));
	CHECK(TestGenericSerialisation<int8_t>(-1, { 0xFF }));
	CHECK(TestGenericSerialisation<int8_t>(42, { 42 }));
	CHECK(TestGenericSerialisation<uint16_t>(0, { 0, 0 }));
	CHECK(TestGenericSerialisation<uint16_t>(0xFE, { 0xFE, 0 }));
	CHECK(TestGenericSerialisation<uint16_t>(0xFEDC, { 0xDC, 0xFE }));
	CHECK(TestGenericSerialisation<int16_t>(-2, { 0xFE, 0xFF }));
	CHECK(TestGenericSerialisation<int16_t>(42, { 42, 0 }));
	CHECK(TestGenericSerialisation<uint32_t>(0, { 0 }));
	CHECK(TestGenericSerialisation<uint32_t>(42, { 42 }));
	CHECK(TestGenericSerialisation<uint32_t>(128, { 0x80, 0x80 }));
	CHECK(TestGenericSerialisation<uint32_t>(UINT32_MAX, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFF }));
	CHECK(TestGenericSerialisation<int32_t>(0, { 0 }));
	CHECK(TestGenericSerialisation<int32_t>(42, { 0x54 }));
	CHECK(TestGenericSerialisation<int32_t>(-42, { 0x53 }));
	CHECK(TestGenericSerialisation<int32_t>(INT32_MAX, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFE }));
	CHECK(TestGenericSerialisation<int32_t>(INT32_MIN, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFF }));
	CHECK(TestGenericSerialisation<uint64_t>(0, { 0 }));
	CHECK(TestGenericSerialisation<uint64_t>(42, { 42 }));
	CHECK(TestGenericSerialisation<uint64_t>(128, { 0x80, 0x80 }));
	CHECK(TestGenericSerialisation<uint64_t>(UINT32_MAX, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFF }));
	CHECK(TestGenericSerialisation<uint64_t>(UINT64_MAX, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }));
	CHECK(TestGenericSerialisation<int64_t>(0, { 0 }));
	CHECK(TestGenericSerialisation<int64_t>(42, { 0x54 }));
	CHECK(TestGenericSerialisation<int64_t>(-42, { 0x53 }));
	CHECK(TestGenericSerialisation<int64_t>(INT32_MAX, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFE }));
	CHECK(TestGenericSerialisation<int64_t>(INT32_MIN, { 0xF0, 0xFF, 0xFF, 0xFF, 0xFF }));
	CHECK(TestGenericSerialisation<int64_t>(INT64_MAX, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE }));
	CHECK(TestGenericSerialisation<int64_t>(INT64_MIN, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }));
}

TEST_CASE("std::variant")
{
	CHECK(TestGenericSerialisation<std::variant<std::monostate, uint8_t>>(std::monostate{}, { 0 }));
	CHECK(TestGenericSerialisation<std::variant<std::monostate, uint8_t>>((uint8_t)0, { 1, 0 }));
	CHECK(TestGenericSerialisation<std::variant<uint8_t, std::string>>((uint8_t)42, { 0, 42 }));
	CHECK(TestGenericSerialisation<std::variant<uint8_t, std::string>>("ABCD", { 1, 0x41, 0x42, 0x43, 0x44, 0 }));
}
