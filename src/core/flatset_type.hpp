/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file flatset_type.hpp Flat set container implementation. */

#ifndef FLATSET_TYPE_HPP
#define FLATSET_TYPE_HPP

#include <ranges>
#include <vector>

/**
 * Flat set implementation that uses a sorted vector for storage.
 * This is subset of functionality implemented by std::flat_set in c++23.
 * @tparam Tkey key type.
 * @tparam Tcompare key comparator.
 */
template <class Tkey, class Tcompare = std::less<>>
class FlatSet {
	std::vector<Tkey> contents; ///< Sorted vector. of values.

	void sort_initial_values()
	{
		std::sort(this->contents.begin(), this->contents.end(), Tcompare{});
		this->contents.erase(std::unique(this->contents.begin(), this->contents.end()), this->contents.end());
	}

public:
	using const_iterator = std::vector<Tkey>::const_iterator;

	FlatSet() = default;
	FlatSet(const FlatSet &) = default;
	FlatSet(FlatSet &&) = default;

	FlatSet(std::initializer_list<Tkey> init) : contents(init)
	{
		this->sort_initial_values();
	}

	template <typename InputIt>
	FlatSet(InputIt first, InputIt last) : contents(first, last)
	{
		this->sort_initial_values();
	}

	FlatSet &operator =(const FlatSet &rhs) = default;
	FlatSet &operator =(FlatSet &&rhs) = default;

	/**
	 * Insert a key into the set, if it does not already exist.
	 * @param key Key to insert.
	 * @return A pair consisting of an iterator to the inserted element (or to the element that prevented the
	 *         insertion), and a bool value to true iff the insertion took place.
	 */
	std::pair<const_iterator, bool> insert(const Tkey &key)
	{
		auto it = std::ranges::lower_bound(this->contents, key, Tcompare{});
		if (it == std::end(this->contents) || *it != key) return {this->contents.emplace(it, key), true};
		return {it, false};
	}

	/**
	 * Erase a key from the set.
	 * @param key Key to erase.
	 * @return number of elements removed.
	 */
	size_t erase(const Tkey &key)
	{
		auto it = std::ranges::lower_bound(this->contents, key, Tcompare{});
		if (it == std::end(this->contents) || *it != key) return 0;

		this->contents.erase(it);
		return 1;
	}

	/**
	 * Test if a key exists in the set.
	 * @param key Key to test.
	 * @return true iff the key exists in the set.
	 */
	bool contains(const Tkey &key) const
	{
		return std::ranges::binary_search(this->contents, key, Tcompare{});
	}

	const_iterator find(const Tkey &key) const
	{
		auto it = std::ranges::lower_bound(this->contents, key, Tcompare{});
		if (it != this->end() && *it != key) it = this->end();
		return it;
	}

	const_iterator begin() const { return std::cbegin(this->contents); }
	const_iterator end() const { return std::cend(this->contents); }

	const_iterator cbegin() const { return std::cbegin(this->contents); }
	const_iterator cend() const { return std::cend(this->contents); }

	size_t size() const { return std::size(this->contents); }
	const Tkey *data() const { return this->contents.data(); }
	bool empty() const { return this->contents.empty(); }

	void clear() { this->contents.clear(); }

	auto operator<=>(const FlatSet<Tkey, Tcompare> &) const = default;
};

#endif /* FLATSET_TYPE_HPP */
