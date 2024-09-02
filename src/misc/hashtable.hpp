/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file hashtable.hpp Hash table support. */

#ifndef HASHTABLE_HPP
#define HASHTABLE_HPP

#include "../3rdparty/robin_hood/robin_hood.h"

/**
 * class CHashTableT<Titem, Thash_bits> - simple hash table
 *  of pointers allocated elsewhere.
 *
 *  Supports: Add/Find/Remove of Titems.
 *
 * Thash_bits_ is ignored but retained for upstream compatibility
 *
 *  Your Titem must meet some extra requirements to be CHashTableT
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
template <class Titem_, int Thash_bits_ = 0>
class CHashTableT {
public:
	typedef Titem_ Titem;                         // make Titem_ visible from outside of class
	typedef typename Titem_::Key Tkey;            // make Titem_::Key a property of HashTable
	typedef typename Tkey::HashKey THashKey;      // make Titem_::Key::HashKey a property of HashTable

private:
	robin_hood::unordered_map<THashKey, Titem *> data;

public:
	/* default constructor */
	inline CHashTableT()
	{
	}

	/** item count */
	inline int Count() const
	{
		return this->data.size();
	}

	/** simple clear - forget all items - used by CSegmentCostCacheT.Flush() */
	inline void Clear()
	{
		this->data.clear();
	}

	/** const item search */
	const Titem_ *Find(const Tkey &key) const
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) return iter->second;
		return nullptr;
	}

	/** non-const item search */
	Titem_ *Find(const Tkey &key)
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) return iter->second;
		return nullptr;
	}

	/** non-const item search & optional removal (if found) */
	Titem_ *TryPop(const Tkey &key)
	{
		auto iter = this->data.find(key.GetHashKey());
		if (iter != this->data.end()) {
			Titem_ *result = iter->second;
			this->data.erase(iter);
			return result;
		}
		return nullptr;
	}

	/** non-const item search & removal */
	Titem_ &Pop(const Tkey &key)
	{
		Titem_ *item = TryPop(key);
		assert(item != nullptr);
		return *item;
	}

	/** non-const item search & optional removal (if found) */
	bool TryPop(Titem_ &item)
	{
		auto iter = this->data.find(item.GetKey().GetHashKey());
		if (iter != this->data.end()) {
			this->data.erase(iter);
			return true;
		}
		return false;
	}

	/** non-const item search & removal */
	void Pop(Titem_ &item)
	{
		[[maybe_unused]] bool ret = TryPop(item);
		assert(ret);
	}

	/** add one item - copy it from the given item */
	void Push(Titem_ &new_item)
	{
		this->data[new_item.GetKey().GetHashKey()] = &new_item;
	}
};

#endif /* HASHTABLE_HPP */
