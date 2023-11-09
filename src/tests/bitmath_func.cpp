/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bitmath_func_test.cpp Test functionality from core/bitmath_func. */

#define OPENTTD_TEST
#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/bitmath_func.hpp"

extern uint8 FindLastBit64(uint64 x);

TEST_CASE("FindLastBit tests")
{
	CHECK(FindLastBit<uint8>(0) == 0);
	CHECK(FindLastBit<uint16>(0) == 0);
	CHECK(FindLastBit<uint32>(0) == 0);
	CHECK(FindLastBit<uint64>(0) == 0);
	CHECK(FindLastBit64(0) == 0);

	for (uint i = 0; i < 8; i++) {
		uint8 t = (uint8)(1 << i);
		CHECK(FindLastBit<uint8>(t) == i);
		CHECK(FindLastBit<uint16>(t) == i);
		CHECK(FindLastBit<uint32>(t) == i);
		CHECK(FindLastBit<uint64>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 8; i < 16; i++) {
		uint16 t = (uint16)(1 << i);
		CHECK(FindLastBit<uint16>(t) == i);
		CHECK(FindLastBit<uint32>(t) == i);
		CHECK(FindLastBit<uint64>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 16; i < 32; i++) {
		uint32 t = (1 << i);
		CHECK(FindLastBit<uint32>(t) == i);
		CHECK(FindLastBit<uint64>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	for (uint i = 32; i < 64; i++) {
		uint64 t = (((uint64)1) << i);
		CHECK(FindLastBit<uint64>(t) == i);
		CHECK(FindLastBit64(t) == i);
	}

	CHECK(FindLastBit(0x42) == FindLastBit(0x40));
	CHECK(FindLastBit(0xAAAA) == FindLastBit(0x8000));
}
