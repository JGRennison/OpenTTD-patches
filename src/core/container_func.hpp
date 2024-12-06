/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file container_func.hpp Some simple functions to help with accessing containers. */

#ifndef CONTAINER_FUNC_HPP
#define CONTAINER_FUNC_HPP

#include <iterator>
#include <algorithm>

/**
 * Helper function to append an item to a container if it is not already contained.
 * The container must have a \c emplace_back function.
 * Consider using std::set, std::unordered_set or std::flat_set in new code.
 *
 * @param container A reference to the container to be extended
 * @param item Reference to the item to be copy-constructed if not found
 *
 * @return Whether the item was already present
 */
template <typename Container>
inline bool include(Container &container, typename Container::const_reference &item)
{
	const bool is_member = std::ranges::find(container, item) != container.end();
	if (!is_member) container.emplace_back(item);
	return is_member;
}

/**
 * Helper function to get the index of an item
 * Consider using std::set, std::unordered_set or std::flat_set in new code.
 *
 * @param container A reference to the container to be searched.
 * @param item Reference to the item to be search for
 *
 * @return Index of element if found, otherwise -1
 */
template <typename Container>
int find_index(Container const &container, typename Container::const_reference item)
{
	auto const it = std::ranges::find(container, item);
	if (it != container.end()) return std::distance(container.begin(), it);

	return -1;
}

/**
 * Move elements between first and last to a new position, rotating elements in between as necessary.
 * @param first Iterator to first element to move.
 * @param last Iterator to (end-of) last element to move.
 * @param position Iterator to where range should be moved to.
 * @returns Iterators to first and last after being moved.
 */
template <typename TIter>
auto Slide(TIter first, TIter last, TIter position) -> std::pair<TIter, TIter>
{
	if (last < position) return { std::rotate(first, last, position), position };
	if (position < first) return { position, std::rotate(position, first, last) };
	return { first, last };
}

template <bool ONCE, typename C, typename UP>
uint container_unordered_remove_if_generic(C &container, UP predicate)
{
	uint removecount = 0;
	for (auto it = container.begin(); it != container.end();) {
		if (predicate(*it)) {
			removecount++;
			if (std::next(it) != container.end()) {
				*it = std::move(container.back());
				container.pop_back();
			} else {
				container.pop_back();
				break;
			}
			if (ONCE) break;
		} else {
			++it;
		}
	}
	return removecount;
}

template <typename C, typename UP>
uint container_unordered_remove_if(C &container, UP predicate)
{
	return container_unordered_remove_if_generic<false>(container, predicate);
}

template <typename C, typename UP>
uint container_unordered_remove_once_if(C &container, UP predicate)
{
	return container_unordered_remove_if_generic<true>(container, predicate);
}

template <bool ONCE, typename C, typename V>
unsigned int container_unordered_remove_generic(C &container, const V &value)
{
	return container_unordered_remove_if_generic<ONCE>(container, [&](const typename C::value_type &v) {
		return v == value;
	});
}

template <typename C, typename V>
uint container_unordered_remove(C &container, const V &value)
{
	return container_unordered_remove_generic<false>(container, value);
}

template <typename C, typename V>
uint container_unordered_remove_once(C &container, const V &value)
{
	return container_unordered_remove_generic<true>(container, value);
}

template <typename T>
bool multimaps_equalivalent(const T &a, const T&b)
{
	if (a.size() != b.size()) return false;

	for (auto it_a = a.begin(); it_a != a.end();) {
		const auto start_a = it_a;
		const auto key = start_a->first;
		size_t distance_a = 0;
		do {
			++it_a;
			++distance_a;
		} while (it_a != a.end() && it_a->first == key);

		const auto start_b = b.lower_bound(key);
		size_t distance_b = 0;
		for (auto it_b = start_b; it_b != b.end() && it_b->first == key; ++it_b) {
			++distance_b;
		}

		if (distance_a != distance_b) return false;

		if (!std::is_permutation(start_a, it_a, start_b)) {
			return false;
		}
	}

	return true;
}

#endif /* CONTAINER_FUNC_HPP */
