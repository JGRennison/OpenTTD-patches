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

std::ostream &operator<<(std::ostream &os, const ring_buffer<uint32_t>::iterator &iter) {
	return os << "Position: " << std::hex << iter.debug_raw_position();
}

void DumpRing(const ring_buffer<uint32_t> &ring)
{
	char buffer[1024];
	char *b = buffer;
	const char *end = buffer + 1024;
	b += snprintf(b, end - b, "Ring: Size: %u, Cap: %u, {", (uint)ring.size(), (uint)ring.capacity());
	for (uint32_t it : ring) {
		b += snprintf(b, end - b, " %u,", it);
	}
	b--;
	b += snprintf(b, end - b, " }");

	WARN(buffer);
}

bool Matches(const ring_buffer<uint32_t> &ring, std::initializer_list<uint32_t> data)
{
	if (ring.size() != data.size()) {
		DumpRing(ring);
		return false;
	}

	auto data_iter = data.begin();
	for (uint32_t it : ring) {
		if (it != *data_iter) {
			DumpRing(ring);
			return false;
		}
		++data_iter;
	}

	return true;
}

TEST_CASE("RingBuffer - basic tests")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6 });
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

TEST_CASE("RingBuffer - front resize")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_front(10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEST_CASE("RingBuffer - front resize 2")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - back resize")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);

	ring.push_back(10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 10 }));
	CHECK(ring.size() == 9);
	CHECK(ring.capacity() == 16);
}

TEST_CASE("RingBuffer - back resize 2")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - insert at ends no grow")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7 });
	CHECK(ring.size() == 7);
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.begin(), 10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());

	ring = ring_buffer<uint32_t>({ 1, 2, 3, 4, 5, 6, 7 });
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7 }));
	CHECK(ring.size() == 7);
	CHECK(ring.capacity() == 8);

	iter = ring.insert(ring.end(), 10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 10 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.end() - 1);
}

TEST_CASE("RingBuffer - insert at ends shifted no grow")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7 });
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

	ring = ring_buffer<uint32_t>({ 1, 2, 3, 4, 5, 6, 7 });
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

TEST_CASE("RingBuffer - insert in middle (begin) no grow")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to beginning, beginning should be shifted backwards */
	uint32_t *pre_begin = &ring[0];
	uint32_t *pre_end = &ring[ring.size() - 1];
	auto iter = ring.insert(ring.begin() + 2, 10);
	CHECK(Matches(ring, { 3, 4, 10, 5, 6, 7, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 2);
	CHECK(pre_begin != &ring[0]);
	CHECK(pre_end == &ring[ring.size() - 1]);
}

TEST_CASE("RingBuffer - insert in middle (end) no grow")
{
	ring_buffer<uint32_t> ring({ 1, 2, 3, 4, 5, 6, 7 });
	ring.pop_front();
	ring.pop_front();
	ring.push_back(20);
	ring.push_back(21);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 20, 21 }));
	CHECK(ring.capacity() == 8);

	/* Insert closer to end, end should be shifted forwards */
	uint32_t *pre_begin = &ring[0];
	uint32_t *pre_end = &ring[ring.size() - 1];
	auto iter = ring.insert(ring.begin() + 5, 10);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 10, 20, 21 }));
	CHECK(ring.size() == 8);
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin() + 5);
	CHECK(pre_begin == &ring[0]);
	CHECK(pre_end != &ring[ring.size() - 1]);
}

TEST_CASE("RingBuffer - insert at beginning grow")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.begin(), 10);
	CHECK(Matches(ring, { 10, 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.begin());
}

TEST_CASE("RingBuffer - insert at end grow")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	auto iter = ring.insert(ring.end(), 10);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 10 }));
	CHECK(ring.capacity() == 16);
	CHECK(iter == ring.end() - 1);
}

TEST_CASE("RingBuffer - insert in middle (begin) grow")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - insert in middle (end) grow")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - insert multi at start")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - insert multi at end")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - insert multi in middle")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - erase")
{
	ring_buffer<uint32_t> ring;
	auto setup_ring = [&]() {
		ring = ring_buffer<uint32_t>({ 3, 4, 5, 6, 7, 8 });
		ring.push_front(2);
		ring.push_front(1);
		CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
		CHECK(ring.capacity() == 8);
	};

	setup_ring();
	uint32_t *expect_front = &ring[1];
	auto iter = ring.erase(ring.begin());
	CHECK(Matches(ring, { 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());
	CHECK(expect_front == &ring[0]);

	setup_ring();
	uint32_t *expect_back = &ring[ring.size() - 2];
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

TEST_CASE("RingBuffer - erase multi")
{
	ring_buffer<uint32_t> ring;
	auto setup_ring = [&]() {
		ring = ring_buffer<uint32_t>({ 3, 4, 5, 6, 7, 8 });
		ring.push_front(2);
		ring.push_front(1);
		CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
		CHECK(ring.capacity() == 8);
	};

	setup_ring();
	uint32_t *expect_front = &ring[2];
	auto iter = ring.erase(ring.begin(), ring.begin() + 2);
	CHECK(Matches(ring, { 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);
	CHECK(iter == ring.begin());
	CHECK(expect_front == &ring[0]);

	setup_ring();
	uint32_t *expect_back = &ring[ring.size() - 3];
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

TEST_CASE("RingBuffer - shrink to fit")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
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

TEST_CASE("RingBuffer - reserve")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	ring.reserve(12);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 16);
}

TEST_CASE("RingBuffer - resize")
{
	ring_buffer<uint32_t> ring({ 3, 4, 5, 6, 7, 8 });
	ring.push_front(2);
	ring.push_front(1);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8 }));
	CHECK(ring.capacity() == 8);

	ring.resize(12);
	CHECK(Matches(ring, { 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0 }));
	CHECK(ring.capacity() == 16);
}
