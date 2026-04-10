/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file hashtable.hpp Hash table support. */

#ifndef HASHTABLE_HPP
#define HASHTABLE_HPP

#include "../3rdparty/robin_hood/robin_hood.h"

/**
 * class HashTable<Titem> - simple hash table
 *  of pointers allocated elsewhere.
 *
 *  Supports: Add/Find/Remove of Titems.
 *
 *  Your Titem must meet some extra requirements to be HashTable
 *  compliant:
 *    - its constructor/destructor (if any) must be public
 *    - if the copying of item requires an extra resource management,
 *        you must define also copy constructor
 *    - must support nested type (struct, class or typedef) Titem::Key
 *        that defines the type of key class for that item
 *    - must support public method:
 *        const Key& GetKey() const; // return the item's key object
 *
 *  In addition, the Titem::Key class must support:
 *    - nested type (struct, class or typedef) Titem::Key::HashKey
 *        that defines the hash key storage type, this may be the same as Titem::Key, and must be hashable and equality comparable
 *    - public method that get the key storage type:
 *        Titem::Key::HashKey GetHashKey() const;
 */
template <class Titem>
class HashTable {
public:
	typedef typename Titem::Key Tkey;             ///< make Titem::Key a property of HashTable
	typedef typename Tkey::HashKey THashKey;      ///< make Titem::Key::HashKey a property of HashTable

private:
	robin_hood::unordered_map<THashKey, Titem *> data;

public:
	/* default constructor */
	inline HashTable()
	{
	}

	/** item count */
	inline size_t Count() const
	{
		return this->data.size();
	}

	/** Clear all items. */
	inline void Clear()
	{
		this->data.clear();
	}

	/**
	 * Try to find an item by key.
	 * @param key The key to find.
	 * @return The found item, or \c nullptr.
	 */
	const Titem *Find(const Tkey &key) const
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) return iter->second;
		return nullptr;
	}

	/**
	 * Try to find an item by key.
	 * @param key The key to find.
	 * @return The found item, or \c nullptr.
	 */
	Titem *Find(const Tkey &key)
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) return iter->second;
		return nullptr;
	}

	/**
	 * Remove an item by key if found.
	 * @param key The key to search for.
	 * @return The popped element, or \c nullptr.
	 */
	Titem *TryPop(const Tkey &key)
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) {
			Titem *result = iter->second;
			this->data.erase(iter);
			return result;
		}
		return nullptr;
	}

	/**
	 * Remove an item by key that must exist.
	 * @param key The key to search for.
	 * @return The popped element; never \c nullptr.
	 */
	Titem &Pop(const Tkey &key)
	{
		Titem *item = TryPop(key);
		assert(item != nullptr);
		return *item;
	}

	/**
	 * Remove an item if found.
	 * @param item The item to remove.
	 * @return \c true iff the item existed.
	 */
	bool TryPop(Titem &item)
	{
		auto iter = this->data.find(item.GetKey().GetHashKey());
		if (iter != this->data.end()) {
			this->data.erase(iter);
			return true;
		}
		return false;
	}

	/**
	 * Remove an item that must exist.
	 * @param item The item to remove.
	 */
	void Pop(Titem &item)
	{
		[[maybe_unused]] bool ret = TryPop(item);
		assert(ret);
	}

	/**
	 * Add an item that may not exist.
	 * @param new_item The item to add.
	 */
	void Push(Titem &new_item)
	{
		this->data[new_item.GetKey().GetHashKey()] = &new_item;
	}
};

#endif /* HASHTABLE_HPP */
