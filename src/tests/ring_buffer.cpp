/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ring_buffer.cpp Test functionality from core/ring_buffer.hpp */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../core/ring_buffer.hpp"

struct NonTrivialTestType {
	uint32_t value;

	NonTrivialTestType() : value(0) {}
	NonTrivialTestType(uint32_t val) : value(val) {}
	NonTrivialTestType(const NonTrivialTestType &other) : value(other.value) {}
	NonTrivialTestType& operator=(const NonTrivialTestType &other) { this->value = other.value; return *this; }

	bool operator==(uint32_t val) const { return this->value == val; }
};

struct MoveOnlyTestType {
	uint32_t value;

	MoveOnlyTestType() : value(0) {}
	MoveOnlyTestType(uint32_t val) : value(val) {}
	MoveOnlyTestType(const MoveOnlyTestType &) = delete;
	MoveOnlyTestType(MoveOnlyTestType &&other) : value(other.value) {}
	MoveOnlyTestType& operator=(const MoveOnlyTestType &) = delete;
	MoveOnlyTestType& operator=(MoveOnlyTestType &&other) { this->value = other.value; return *this; }

	bool operator==(uint32_t val) const { return this->value == val; }
};

uint32_t TestValueOf(uint32_t val) { return val; }
uint32_t TestValueOf(const NonTrivialTestType &val) { return val.value; }
uint32_t TestValueOf(const MoveOnlyTestType &val) { return val.value; }

template <typename T>
std::ostream &operator<<(std::ostream &os, const typename ring_buffer<T>::iterator &iter) {
	return os << "Position: " << std::hex << iter.debug_raw_position();
}

template <typename T>
void DumpRing(const ring_buffer<T> &ring)
{
	char buffer[1024];
	char *b = buffer;
	const char *end = buffer + 1024;
	b += snprintf(b, end - b, "Ring: Size: %u, Cap: %u, {", (uint)ring.size(), (uint)ring.capacity());
	for (const auto &it : ring) {
		b += snprintf(b, end - b, " %u,", TestValueOf(it));
	}
	b--;
	b += snprintf(b, end - b, " }");

	WARN(buffer);
}

template <typename T>
bool Matches(const ring_buffer<T> &ring, std::initializer_list<uint32_t> data)
{
	if (ring.size() != data.size()) {
		DumpRing(ring);
		return false;
	}

	auto data_iter = data.begin();
	for (const auto &it : ring) {
		if (TestValueOf(it) != *data_iter) {
			DumpRing(ring);
			return false;
		}
		++data_iter;
	}

	return true;
}

static_assert(std::is_trivially_copyable_v<uint32_t>);
static_assert(!std::is_trivially_copyable_v<NonTrivialTestType>);
static_assert(!std::is_trivially_copyable_v<MoveOnlyTestType>);

TEMPLATE_TEST_CASE("RingBuffer - basic tests", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6 });
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));

	ring.push_front(0);
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5, 6 }));

	ring.pop_back();
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5 }));

	ring.push_back(10);
	ring.push_back(11);
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5, 10, 11 }));

	ring.pop_front();
	ring.pop_front();
	CHECK(Matches(ring, { 2, 3, 4, 5, 10, 11 }));
	CHECK(ring.capacity() == 8);

	CHECK(ring[0] == 2);
	CHECK(ring[4] == 10);
}

TEMPLATE_TEST_CASE("RingBuffer - front resize", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_front(10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - front resize 2", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_front(10);
	CHECK(Matches(ring, { 10, 3, 4, 5, 6, 7, 8, 20, 21 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - back resize", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_back(10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 10 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - back resize 2", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_back(10);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 20, 21, 10 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - insert at ends no grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7 });
	CHECK(ring.size() == 7);
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.begin(), 10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());

	ring = ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6, 7 });
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7 }));
	CHECK(ring.size() == 7);
	CHECK(ring.capacity() == 8);

	iter = ring.insert(ring.end(), 10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 10 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 1);
}

TEMPLATE_TEST_CASE("RingBuffer - insert at ends shifted no grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.begin(), 10);
	CHECK(Matches(ring, { 10, 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());

	ring = ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	iter = ring.insert(ring.end(), 10);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21, 10 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 1);
}

TEMPLATE_TEST_CASE("RingBuffer - insert in middle (begin) no grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to beginning, beginning should be shifted backwards */
	TestType *pre_begin = &ring[0];
	TestType *pre_end = &ring[ring.size() - 1];
	auto iter = ring.insert(ring.begin() + 2, 10);
	CHECK(Matches(ring, { 3, 4, 10, 5, 6, 7, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 2);
	CHECK(pre_begin != &ring[0]);
	CHECK(pre_end == &ring[ring.size() - 1]);
}

TEMPLATE_TEST_CASE("RingBuffer - insert in middle (end) no grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to end, end should be shifted forwards */
	TestType *pre_begin = &ring[0];
	TestType *pre_end = &ring[ring.size() - 1];
	auto iter = ring.insert(ring.begin() + 5, 10);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 10, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 5);
	CHECK(pre_begin == &ring[0]);
	CHECK(pre_end != &ring[ring.size() - 1]);
}

TEMPLATE_TEST_CASE("RingBuffer - insert at beginning grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.begin(), 10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin());
}

TEMPLATE_TEST_CASE("RingBuffer - insert at end grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.end(), 10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 10 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.end() - 1);
}

TEMPLATE_TEST_CASE("RingBuffer - insert in middle (begin) grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to beginning */
	auto iter = ring.insert(ring.begin() + 2, 10);
	CHECK(Matches(ring, { 1, 2, 10, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin() + 2);
}

TEMPLATE_TEST_CASE("RingBuffer - insert in middle (end) grow", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to beginning */
	auto iter = ring.insert(ring.begin() + 6, 10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 10, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin() + 6);
}

TEMPLATE_TEST_CASE("RingBuffer - insert multi at start", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	auto iter = ring.insert(ring.begin(), { 1, 2 });
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());

	iter = ring.insert(ring.begin(), { 10, 11 });
	CHECK(Matches(ring, { 10, 11, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin());

	iter = ring.insert(ring.begin(), 2, 24);
	CHECK(Matches(ring, { 24, 24, 10, 11, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin());
}

TEMPLATE_TEST_CASE("RingBuffer - insert multi at end", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	auto iter = ring.insert(ring.end(), { 1, 2 });
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 1, 2 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 2);

	iter = ring.insert(ring.end(), { 10, 11 });
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 1, 2, 10, 11 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.end() - 2);

	iter = ring.insert(ring.end(), 2, 24);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8, 1, 2, 10, 11, 24, 24 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.end() - 2);
}

TEMPLATE_TEST_CASE("RingBuffer - insert multi in middle", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	auto iter = ring.insert(ring.begin() + 3, { 1, 2 });
	CHECK(Matches(ring, { 3, 4, 5, 1, 2, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 3);

	iter = ring.insert(ring.begin() + 7, { 10, 11 });
	CHECK(Matches(ring, { 3, 4, 5, 1, 2, 6, 7, 10, 11, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin() + 7);

	iter = ring.insert(ring.begin() + 2, 2, 24);
	CHECK(Matches(ring, { 3, 4, 24, 24, 5, 1, 2, 6, 7, 10, 11, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin() + 2);
}

TEMPLATE_TEST_CASE("RingBuffer - erase", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring;
	auto setup_ring = [&]() {
		ring = ring_buffer<TestType>({ 3, 4, 5, 6, 7, 8 });
		ring.push_front(2);
		ring.push_front(1);
		CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
		CHECK(ring.capacity() == 8);
	};

	setup_ring();
	TestType *expect_front = &ring[1];
	auto iter = ring.erase(ring.begin());
	CHECK(Matches(ring, { 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());
	CHECK(expect_front == &ring[0]);

	setup_ring();
	TestType *expect_back = &ring[ring.size() - 2];
	iter = ring.erase(ring.end() - 1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end());
	CHECK(expect_back == &ring[ring.size() - 1]);

	setup_ring();
	expect_front = &ring[1];
	iter = ring.erase(ring.begin() + 2);
	CHECK(Matches(ring, { 1, 2, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 2);
	CHECK(expect_front == &ring[0]);

	setup_ring();
	expect_back = &ring[ring.size() - 2];
	iter = ring.erase(ring.end() - 3);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 2);
	CHECK(expect_back == &ring[ring.size() - 1]);
}

TEMPLATE_TEST_CASE("RingBuffer - erase multi", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring;
	auto setup_ring = [&]() {
		ring = ring_buffer<TestType>({ 3, 4, 5, 6, 7, 8 });
		ring.push_front(2);
		ring.push_front(1);
		CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
		CHECK(ring.capacity() == 8);
	};

	setup_ring();
	TestType *expect_front = &ring[2];
	auto iter = ring.erase(ring.begin(), ring.begin() + 2);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());
	CHECK(expect_front == &ring[0]);

	setup_ring();
	TestType *expect_back = &ring[ring.size() - 3];
	iter = ring.erase(ring.end() - 2, ring.end());
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end());
	CHECK(expect_back == &ring[ring.size() - 1]);

	setup_ring();
	expect_front = &ring[2];
	iter = ring.erase(ring.begin() + 2, ring.begin() + 4);
	CHECK(Matches(ring, { 1, 2, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 2);
	CHECK(expect_front == &ring[0]);

	setup_ring();
	expect_back = &ring[ring.size() - 3];
	iter = ring.erase(ring.end() - 4, ring.end() - 2);
	CHECK(Matches(ring, { 1, 2, 3, 4, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 2);
	CHECK(expect_back == &ring[ring.size() - 1]);

	setup_ring();
	iter = ring.erase(ring.begin() + 1, ring.end() - 1);
	CHECK(Matches(ring, { 1, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 1);
}

TEMPLATE_TEST_CASE("RingBuffer - shrink to fit", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	ring.insert(ring.begin() + 6, 10);
	ring.insert(ring.begin() + 8, 11);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 10, 7, 11, 8 }));
	CHECK(ring.capacity() == 16);

	ring.pop_front();
	ring.pop_back();
	CHECK(Matches(ring, { 2, 3, 4, 5, 6, 10, 7, 11 }));
	CHECK(ring.capacity() == 16);

	ring.shrink_to_fit();
	CHECK(Matches(ring, { 2, 3, 4, 5, 6, 10, 7, 11 }));
	CHECK(ring.capacity() == 8);
}

TEMPLATE_TEST_CASE("RingBuffer - reserve", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	ring.reserve(12);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - resize", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	ring.resize(12);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0 }));
	CHECK(ring.capacity() == 16);
}

TEMPLATE_TEST_CASE("RingBuffer - basic move-only test", "[ring]", uint8_t, uint32_t, NonTrivialTestType, MoveOnlyTestType)
{
	TestType init[] = { 1, 2, 3, 4, 5, 6 };
	ring_buffer<TestType> ring(std::make_move_iterator(std::begin(init)), std::make_move_iterator(std::end(init)));
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));

	ring.push_front(0);
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5, 6 }));

	ring.pop_back();
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5 }));

	ring.push_back(10);
	ring.push_back(11);
	CHECK(Matches(ring, { 0, 1, 2, 3, 4, 5, 10, 11 }));

	ring.pop_front();
	ring.pop_front();
	CHECK(Matches(ring, { 2, 3, 4, 5, 10, 11 }));
	CHECK(ring.capacity() == 8);

	CHECK(ring[0] == 2);
	CHECK(ring[4] == 10);
}
