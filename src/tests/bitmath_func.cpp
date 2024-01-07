/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bitmath_func_test.cpp Test functionality from core/bitmath_func. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/bitmath_func.hpp"

extern uint8_t FindLastBit64(uint64_t x);

TEST_CASE("FindLastBit tests")
{
	CHECK(FindLastBit<uint8_t>(0) == 0);
	CHECK(FindLastBit<uint16_t>(0) == 0);
	CHECK(FindLastBit<uint32_t>(0) == 0);
	CHECK(FindLastBit<uint64_t>(0) == 0);
	CHECK(FindLastBit64(0) == 0);

	for (uint i = 0; i < 8; i++) {
		uint8_t t = (uint8_t)(1 << i);
		CHECK(FindLastBit<uint8_t>(t) == i);
		CHECK(FindLastBit<uint16_t>(t) == i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 8; i < 16; i++) {
		uint16_t t = (uint16_t)(1 << i);
		CHECK(FindLastBit<uint16_t>(t) == i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 16; i < 32; i++) {
		uint32_t t = (1 << i);
		CHECK(FindLastBit<uint32_t>(t) == i);
		CHECK(FindLastBit<uint64_t>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 32; i < 64; i++) {
		uint64_t t = (((uint64_t)1) << i);
		CHECK(FindLastBit<uint64_t>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	CHECK(FindLastBit(0x42) == FindLastBit(0x40));
	CHECK(FindLastBit(0xAAAA) == FindLastBit(0x8000));
}
