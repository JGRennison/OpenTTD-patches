/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bitmath_func.cpp Test functionality from core/bitmath_func. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/bitmath_func.hpp"

TEST_CASE("FindLastBit tests")
{
	CHECK(FindLastBit<uint8_t>(0) == 0);
	CHECK(FindLastBit<uint16_t>(0) == 0);
	CHECK(FindLastBit<uint32_t>(0) == 0);
	CHECK(FindLastBit<uint64_t>(0) == 0);

	for (uint i = 0; i < 8; i++) {
		uint8_t t = (uint8_t)(1 << i);
		CHECK(FindLastBit<uint8_t>(t) == i);
		CHECK(FindLastBit<uint16_t>(t) == i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
	}

	for (uint i = 8; i < 16; i++) {
		uint16_t t = (uint16_t)(1 << i);
		CHECK(FindLastBit<uint16_t>(t) == i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
	}

	for (uint i = 16; i < 32; i++) {
		uint32_t t = (1 << i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
	}

	for (uint i = 32; i < 64; i++) {
		uint64_t t = (((uint64_t)1) << i);
		CHECK(FindLastBit<uint64_t>(t) == i);
	}

	CHECK(FindLastBit(0x42U) == FindLastBit(0x40U));
	CHECK(FindLastBit(0xAAAAU) == FindLastBit(0x8000U));
}

TEST_CASE("SetBitIterator tests")
{
	auto test_case = [&](auto input, std::initializer_list<uint> expected) {
		auto iter = expected.begin();
		for (auto bit : SetBitIterator(input)) {
			if (iter == expected.end()) return false;
			if (bit != *iter) return false;
			++iter;
		}
		return iter == expected.end();
	};
	CHECK(test_case(0, {}));
	CHECK(test_case(1, { 0 }));
	CHECK(test_case(42, { 1, 3, 5 }));
	CHECK(test_case(0x8080FFFFU, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 23, 31 }));
	CHECK(test_case(INT32_MIN, { 31 }));
	CHECK(test_case(INT64_MIN, { 63 }));
}

TEST_CASE("GetBitMaskSC tests")
{
	CHECK(GetBitMaskSC<uint>(4, 4) == 0xF0);
	CHECK(GetBitMaskSC<uint>(28, 4) == 0xF0000000);
	CHECK(GetBitMaskSC<uint8_t>(7, 1) == 0x80);
	CHECK(GetBitMaskSC<uint8_t>(0, 1) == 1);
	CHECK(GetBitMaskSC<uint8_t>(0, 0) == 0);
	CHECK(GetBitMaskSC<uint8_t>(7, 0) == 0);
}

TEST_CASE("GetBitMaskFL tests")
{
	CHECK(GetBitMaskFL<uint>(4, 7) == 0xF0);
	CHECK(GetBitMaskFL<uint>(28, 31) == 0xF0000000);
	CHECK(GetBitMaskFL<uint8_t>(7, 7) == 0x80);
	CHECK(GetBitMaskFL<uint8_t>(0, 0) == 1);
	CHECK(GetBitMaskFL<uint8_t>(3, 4) == 0x18);
}
