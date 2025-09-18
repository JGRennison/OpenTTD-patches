/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file test_bitset.cpp Test functionality from core/base_bitset_type.hpp and core/enum_type.hpp */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/base_bitset_type.hpp"
#include "../core/enum_type.hpp"

#include "../safeguards.h"

TEST_CASE("Comparison operator tests")
{
	enum class TestFlag : uint8_t {
		A,
		B,
	};

	using TestFlags = EnumBitSet<TestFlag, uint8_t>;

	CHECK(TestFlags{TestFlag::A} < TestFlags{TestFlag::B});
	CHECK(TestFlags{TestFlag::A}.base() < TestFlags{TestFlag::B}.base());
	CHECK(TestFlags{TestFlag::B} > TestFlags{TestFlag::A});
	CHECK(TestFlags{TestFlag::B}.base() > TestFlags{TestFlag::A}.base());
	CHECK(TestFlags{TestFlag::A} == TestFlags{TestFlag::A});
	CHECK(TestFlags{TestFlag::A}.base() == TestFlags{TestFlag::A}.base());
	CHECK(TestFlags{TestFlag::A} != TestFlags{TestFlag::B});
	CHECK(TestFlags{TestFlag::A}.base() != TestFlags{TestFlag::B}.base());
	CHECK((TestFlags{TestFlag::A} <=> TestFlags{TestFlag::B}) == (TestFlags{TestFlag::A}.base() <=> TestFlags{TestFlag::B}.base()));
}
