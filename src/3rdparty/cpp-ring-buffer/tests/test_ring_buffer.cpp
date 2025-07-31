// Tests: Resizing ring buffer implementation
// https://github.com/JGRennison/cpp-ring-buffer
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Jonathan Rennison <j.g.rennison@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "../../../stdafx.h"
#include "../../catch2/catch.hpp"
#include "../ring_buffer.hpp"
#include <compare>
#include <sstream>

using jgr::ring_buffer;

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
	std::stringstream buffer;
	buffer << "Ring: Size: " << ring.size() << ", Cap: " << ring.capacity() << ", {";
	bool done_first = false;
	for (const auto &it : ring) {
		if (done_first) {
			buffer << ',';
		} else {
			done_first = true;
		}
		buffer << ' ' << TestValueOf(it);
	}
	buffer << " }";

	WARN(buffer.str());
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
	CHECK(ring[0] == 1);
	CHECK(ring.at(3) == 4);

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
	CHECK(ring.at(5) == 11);
	CHECK_THROWS_AS(ring.at(6), std::out_of_range);
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

	ring.pop_front();
	ring.resize(7);
	CHECK(Matches(ring, { 2, 3, 4, 5, 6, 7, 8 }));
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

TEMPLATE_TEST_CASE("RingBuffer - copy/move constructors and assignment", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	std::initializer_list<TestType> init{ 1, 2, 3, 4, 5, 6 };
	ring_buffer<TestType> ring(init);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));

	ring_buffer<TestType> ring2(ring.begin() + 1, ring.end() - 2);
	CHECK(Matches(ring2, { 2, 3, 4 }));

	ring_buffer<TestType> ring3(ring2);
	CHECK(Matches(ring3, { 2, 3, 4 }));
	CHECK(Matches(ring2, { 2, 3, 4 }));

	TestType *expect_front = &ring3[0];

	ring_buffer<TestType> ring4(std::move(ring3));
	CHECK(Matches(ring4, { 2, 3, 4 }));
	CHECK(Matches(ring3, {}));
	CHECK(ring3.capacity() == 0);
	CHECK(expect_front == &ring4[0]);

	ring4.insert(++ring4.begin(), { 10, 11, 12 });
	CHECK(Matches(ring4, { 2, 10, 11, 12, 3, 4 }));

	TestType *expect_rpos = &*(ring4.rbegin() + 2);
	ring2.swap(ring4);
	CHECK(Matches(ring2, { 2, 10, 11, 12, 3, 4 }));
	CHECK(Matches(ring4, { 2, 3, 4 }));
	CHECK(expect_rpos == &*(ring2.rbegin() += 2));

	ring4 = ring;
	CHECK(Matches(ring4, { 1, 2, 3, 4, 5, 6 }));
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));

	TestType *expect_back = &ring2.back();
	ring4 = std::move(ring2);
	CHECK(Matches(ring4, { 2, 10, 11, 12, 3, 4 }));
	CHECK(Matches(ring2, {}));
	CHECK(ring2.capacity() == 0);
	CHECK(expect_back == &ring4.back());
}

TEMPLATE_TEST_CASE("RingBuffer - copy reverse", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 3, 4, 5, 6 });
	ring.insert(ring.begin(), { 1, 2 });
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6 }));

	ring_buffer<TestType> ring2(ring.rbegin(), ring.rend());
	CHECK(Matches(ring2, { 6, 5, 4, 3, 2, 1 }));

	ring_buffer<TestType> ring3(ring.crbegin() += 2, ring.crend() - 1);
	CHECK(Matches(ring3, { 4, 3, 2 }));

	ring_buffer<TestType> ring4({ 10, 20, 30, 40, 50, 60 });
	ring4.insert(ring4.end() - 2, ring.rbegin(), ring.rend());
	CHECK(Matches(ring4, { 10, 20, 30, 40, 6, 5, 4, 3, 2, 1, 50, 60 }));
}

TEMPLATE_TEST_CASE("RingBuffer - equality tests", "[ring]", uint8_t, uint32_t)
{
	ring_buffer<TestType> ring1({ 1, 2, 3, 4, 5, 6 });
	ring_buffer<TestType> ring2({ 3, 4, 5, 6 });
	CHECK(ring1 != ring2);

	ring2.push_front(2);
	ring2.push_front(2);
	CHECK(ring1 != ring2);

	ring2.front() = 1;
	CHECK(Matches(ring1, { 1, 2, 3, 4, 5, 6 }));
	CHECK(Matches(ring2, { 1, 2, 3, 4, 5, 6 }));
	CHECK(ring1 == ring2);
}

TEMPLATE_TEST_CASE("RingBuffer - operator <=>", "[ring]", uint8_t, uint32_t)
{
	CHECK((ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6 }) <=> ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6 })) == std::weak_ordering::equivalent);
	CHECK((ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6 }) <=> ring_buffer<TestType>({ 2, 3, 4, 5, 6 })) == std::weak_ordering::less);
	CHECK((ring_buffer<TestType>({ 1, 2, 3 }) <=> ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6 })) == std::weak_ordering::less);
	CHECK((ring_buffer<TestType>({ 1, 2, 3, 4, 5, 6 }) <=> ring_buffer<TestType>({ 1, 2, 3 })) == std::weak_ordering::greater);
	CHECK((ring_buffer<TestType>({}) <=> ring_buffer<TestType>({ 1 })) == std::weak_ordering::less);
	CHECK((ring_buffer<TestType>({}) <=> ring_buffer<TestType>({})) == std::weak_ordering::equivalent);
}


TEMPLATE_TEST_CASE("RingBuffer - insert with ref to existing element", "[ring]", uint8_t, uint32_t, NonTrivialTestType)
{
	ring_buffer<TestType> ring({ 1 });
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.push_back(ring.back());
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);

	ring.resize(1);
	ring.shrink_to_fit();
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.push_front(ring.back());
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);

	ring.resize(1);
	ring.shrink_to_fit();
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.push_back(std::move(ring.back()));
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);

	ring.resize(1);
	ring.shrink_to_fit();
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.push_front(std::move(ring.back()));
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);

	ring.resize(1);
	ring.shrink_to_fit();
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.emplace_back(std::move(ring.back()));
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);

	ring.resize(1);
	ring.shrink_to_fit();
	CHECK(ring.capacity() == 4);
	for (size_t i = 0; i < 7; i++) {
		ring.emplace_front(std::move(ring.back()));
	}
	CHECK(Matches(ring, { 1, 1, 1, 1, 1, 1, 1, 1 }));
	CHECK(ring.capacity() == 8);
}
