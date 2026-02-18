/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_list.cpp Implementation of ScriptList. */

#include "../../stdafx.h"
#include "script_controller.hpp"
#include "script_list.hpp"
#include "../../debug.h"
#include "../../script/squirrel.hpp"

#include "../../safeguards.h"

/** Number of bytes per item to charge to script allocation limit. */
static const size_t SCRIPT_LIST_BYTES_PER_ITEM = 64;

/**
 * Base class for any ScriptList sorter.
 */
class ScriptListSorter {
protected:
	ScriptList *list;                     ///< The list that's being sorted.
	bool has_no_more_items = true;        ///< Whether we have more items to iterate over.
	std::optional<SQInteger> item_next{}; ///< The next item we will show, or std::nullopt if there are no more items to iterate over.

	virtual void FindNext() = 0;

public:
	/**
	 * Virtual dtor, needed to mute warnings.
	 */
	virtual ~ScriptListSorter() = default;

	/**
	 * Get the first item of the sorter.
	 */
	virtual std::optional<SQInteger> Begin() = 0;

	/**
	 * Stop iterating a sorter.
	 */
	void End()
	{
		this->item_next = std::nullopt;
		this->has_no_more_items = true;
	}

	/**
	 * Get the next item of the sorter.
	 */
	std::optional<SQInteger> Next()
	{
		if (this->IsEnd()) return std::nullopt;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * See if the sorter has reached the end.
	 */
	bool IsEnd() const
	{
		return this->list->items.empty() || this->has_no_more_items;
	}

	/**
	 * Callback from the list if an item gets removed.
	 */
	void Remove(SQInteger item)
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) {
			this->FindNext();
			return;
		}
	}

	/**
	 * Callback from the list after an item gets removed.
	 */
	virtual void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) = 0;

	/**
	 * Safe btree iterators hold a pointer to the parent container's tree, so update those
	 */
	virtual void RetargetIterators() = 0;

	/**
	 * Attach the sorter to a new list. This assumes the content of the old list has been moved to
	 * the new list, too, so that we don't have to invalidate any iterators.
	 * @param target New list to attach to.
	 */
	void Retarget(ScriptList *new_list)
	{
		this->list = new_list;
		this->RetargetIterators();
	}
};

template <typename T>
void RetargetIterator(T &container, typename T::iterator &iter)
{
	if (iter.generation() > 0) {
		iter = container.lower_bound(iter.key());
	} else {
		iter = container.end();
	}
}

/**
 * Sort by value, ascending.
 */
class ScriptListSorterValueAscending final : public ScriptListSorter {
private:
	ScriptList::ScriptListValueSet::iterator value_iter;  ///< The iterator over the value list.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterValueAscending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->values.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->value_iter = this->list->values.begin();
		this->item_next = this->value_iter->second;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		if (this->value_iter == this->list->values.end()) {
			this->item_next = std::nullopt;
			this->has_no_more_items = true;
			return;
		}
		this->value_iter++;
		if (this->value_iter != this->list->values.end()) {
			this->item_next = this->value_iter->second;
		} else {
			this->item_next = std::nullopt;
		}
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		if (this->IsEnd()) return;

		/*
		 * This is to the optimise the case where the current item is removed, and the resulting iterator points to the expected next item.
		 * NB: A positive generation means that the iterator was not the end iterator, and therefore that item_next has a valid value.
		 */
		if (value_post_erase != this->list->values.end() && this->value_iter.generation() > 0 && value_post_erase->second == this->item_next) {
			this->value_iter = value_post_erase;
		}
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->values, this->value_iter);
	}
};

/**
 * Sort by value, descending.
 */
class ScriptListSorterValueDescending final : public ScriptListSorter {
private:
	/* Note: We cannot use reverse_iterator.
	 *       The iterators must only be invalidated when the element they are pointing to is removed.
	 *       This only holds for forward iterators. */
	ScriptList::ScriptListValueSet::iterator value_iter;  ///< The iterator over the value list.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterValueDescending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->values.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->value_iter = this->list->values.end();
		--this->value_iter;
		this->item_next = this->value_iter->second;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		if (this->value_iter == this->list->values.end()) {
			this->item_next = std::nullopt;
			this->has_no_more_items = true;
			return;
		}
		if (this->value_iter == this->list->values.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->value_iter = this->list->values.end();
		} else {
			this->value_iter--;
		}
		if (this->value_iter != this->list->values.end()) {
			this->item_next = this->value_iter->second;
		} else {
			this->item_next = std::nullopt;
		}
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		/* not implemented */
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->values, this->value_iter);
	}
};

/**
 * Sort by item, ascending.
 */
class ScriptListSorterItemAscending final : public ScriptListSorter {
private:
	ScriptList::ScriptListMap::iterator item_iter; ///< The iterator over the items in the map.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterItemAscending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->items.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->item_iter = this->list->items.begin();
		this->item_next = this->item_iter->first;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		this->item_iter++;
		if (this->item_iter != this->list->items.end()) this->item_next = this->item_iter->first;
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		if (this->IsEnd()) return;

		/*
		 * This is to the optimise the case where the current item is removed, and the resulting iterator points to the expected next item.
		 * NB: A positive generation means that the iterator was not the end iterator, and therefore that item_next has a valid value.
		 */
		if (post_erase != this->list->items.end() && this->item_iter.generation() > 0 && post_erase->first == this->item_next) {
			this->item_iter = post_erase;
		}
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->items, this->item_iter);
	}
};

/**
 * Sort by item, descending.
 */
class ScriptListSorterItemDescending final : public ScriptListSorter {
private:
	/* Note: We cannot use reverse_iterator.
	 *       The iterators must only be invalidated when the element they are pointing to is removed.
	 *       This only holds for forward iterators. */
	ScriptList::ScriptListMap::iterator item_iter; ///< The iterator over the items in the map.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterItemDescending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->items.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->item_iter = this->list->items.end();
		--this->item_iter;
		this->item_next = this->item_iter->first;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		if (this->item_iter == this->list->items.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->item_iter = this->list->items.end();
		} else {
			--this->item_iter;
		}
		if (this->item_iter != this->list->items.end()) this->item_next = this->item_iter->first;
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		/* not implemented */
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->items, this->item_iter);
	}
};



bool ScriptList::SaveObject(HSQUIRRELVM vm)
{
	sq_pushstring(vm, "List");
	sq_newarray(vm, 0);
	sq_pushinteger(vm, this->sorter_type);
	sq_arrayappend(vm, -2);
	sq_pushbool(vm, this->sort_ascending ? SQTrue : SQFalse);
	sq_arrayappend(vm, -2);
	sq_newtable(vm);
	for (const auto &item : this->items.unprotected_view()) {
		sq_pushinteger(vm, item.first);
		sq_pushinteger(vm, item.second);
		sq_rawset(vm, -3);
	}
	sq_arrayappend(vm, -2);
	return true;
}

bool ScriptList::LoadObject(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, -1) != OT_ARRAY) return false;
	sq_pushnull(vm);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_INTEGER) return false;
	SQInteger type;
	sq_getinteger(vm, -1, &type);
	sq_pop(vm, 2);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_BOOL) return false;
	SQBool order;
	sq_getbool(vm, -1, &order);
	sq_pop(vm, 2);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_TABLE) return false;
	sq_pushnull(vm);
	while (SQ_SUCCEEDED(sq_next(vm, -2))) {
		if (sq_gettype(vm, -2) != OT_INTEGER && sq_gettype(vm, -1) != OT_INTEGER) return false;
		SQInteger key, value;
		sq_getinteger(vm, -2, &key);
		sq_getinteger(vm, -1, &value);
		this->AddItem(key, value);
		sq_pop(vm, 2);
	}
	sq_pop(vm, 3);
	if (SQ_SUCCEEDED(sq_next(vm, -2))) return false;
	sq_pop(vm, 1);
	this->Sort(static_cast<SorterType>(type), order == SQTrue);
	return true;
}

ScriptObject *ScriptList::CloneObject()
{
	ScriptList *clone = new ScriptList();
	clone->CopyList(this);
	return clone;
}

void ScriptList::CopyList(const ScriptList *list)
{
	Squirrel::DecreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM * this->items.size());
	this->Sort(list->sorter_type, list->sort_ascending);
	this->items = list->items;
	this->values_inited = list->values_inited;
	this->values = list->values;
	Squirrel::IncreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM * this->items.size());
}

ScriptList::ScriptList()
{
	/* Default sorter */
	this->sorter_type    = SORT_BY_VALUE;
	this->sort_ascending = false;
	this->initialized    = false;
	this->values_inited  = false;
	this->modifications  = 0;
}

ScriptList::~ScriptList()
{
	if (_squirrel_allocator != nullptr) {
		Squirrel::DecreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM * this->items.size());
	}
}

bool ScriptList::HasItem(SQInteger item)
{
	return this->items.count(item) == 1;
}

void ScriptList::Clear()
{
	this->modifications++;
	Squirrel::DecreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM * this->items.size());

	this->items.clear();
	this->values.clear();
	this->values_inited = false;
	if (this->sorter != nullptr) this->sorter->End();
}

void ScriptList::AddOrSetItem(SQInteger item, SQInteger value)
{
	this->modifications++;

	auto res = this->items.insert(std::make_pair(item, value));
	if (!res.second) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(res.first, value);
		return;
	}

	Squirrel::IncreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM);
	if (this->values_inited) {
		this->values.insert(std::make_pair(value, item));
	}
}

void ScriptList::AddToItemValue(SQInteger item, SQInteger value)
{
	this->modifications++;

	auto res = this->items.insert(std::make_pair(item, value));
	if (!res.second) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(res.first, res.first->second + value);
		return;
	}

	Squirrel::IncreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM);
	if (this->values_inited) {
		this->values.insert(std::make_pair(value, item));
	}
}

void ScriptList::AddItem(SQInteger item, SQInteger value)
{
	this->modifications++;

	auto res = this->items.insert(std::make_pair(item, value));
	if (!res.second) {
		/* Key was already present, insertion did not take place */
		return;
	}

	Squirrel::IncreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM);
	if (this->values_inited) {
		this->values.insert(std::make_pair(value, item));
	}
}

ScriptList::ScriptListMap::iterator ScriptList::RemoveIter(ScriptList::ScriptListMap::iterator item_iter)
{
	SQInteger item = item_iter->first;
	SQInteger value = item_iter->second;

	if (this->initialized) this->sorter->Remove(item);

	ScriptListMap::iterator new_item_iter = this->items.erase(item_iter);
	Squirrel::DecreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM);
	if (this->values_inited) {
		ScriptListValueSet::iterator new_reverse_iter = this->values.erase(this->values.find(std::make_pair(value, item)));

		if (this->initialized) this->sorter->PostErase(item, new_item_iter, new_reverse_iter);
	} else {
		if (this->initialized) this->sorter->PostErase(item, new_item_iter, {});
	}

	return new_item_iter;
}

ScriptList::ScriptListValueSet::iterator ScriptList::RemoveValueIter(ScriptList::ScriptListValueSet::iterator value_iter)
{
	SQInteger item = value_iter->second;

	if (this->initialized) this->sorter->Remove(item);

	ScriptListMap::iterator new_item_iter = this->items.erase(this->items.find(item));
	ScriptListValueSet::iterator new_value_iter = this->values.erase(value_iter);
	Squirrel::DecreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM);

	if (this->initialized) this->sorter->PostErase(item, new_item_iter, new_value_iter);

	return new_value_iter;
}

void ScriptList::RemoveItem(SQInteger item)
{
	this->modifications++;

	ScriptListMap::iterator item_iter = this->items.find(item);
	if (item_iter == this->items.end()) return;

	this->RemoveIter(item_iter);
}

void ScriptList::InitValues()
{
	btree::btree_set<std::pair<SQInteger, SQInteger>> new_values;
	for (const auto &iter : this->items.unprotected_view()) {
		new_values.insert(std::make_pair(iter.second, iter.first));
	}
	this->values.swap(new_values);
	this->values_inited = true;
}

void ScriptList::InitSorter()
{
	if (this->sorter == nullptr) {
		switch (this->sorter_type) {
			case SORT_BY_ITEM:
				if (this->sort_ascending) {
					this->sorter = std::make_unique<ScriptListSorterItemAscending>(this);
				} else {
					this->sorter = std::make_unique<ScriptListSorterItemDescending>(this);
				}
				break;

			case SORT_BY_VALUE:
				if (this->sort_ascending) {
					this->sorter = std::make_unique<ScriptListSorterValueAscending>(this);
				} else {
					this->sorter = std::make_unique<ScriptListSorterValueDescending>(this);
				}
				break;
			default: NOT_REACHED();
		}
	}
	if (!this->values_inited && this->sorter_type == SORT_BY_VALUE) {
		this->InitValues();
	}
	this->initialized = true;
}

SQInteger ScriptList::Begin()
{
	this->InitSorter();
	return this->sorter->Begin().value_or(0);
}

SQInteger ScriptList::Next()
{
	if (!this->initialized) {
		Debug(script, 0, "Next() is invalid as Begin() is never called");
		return 0;
	}
	return this->sorter->Next().value_or(0);
}

bool ScriptList::IsEmpty()
{
	return this->items.empty();
}

bool ScriptList::IsEnd()
{
	if (!this->initialized) {
		Debug(script, 0, "IsEnd() is invalid as Begin() is never called");
		return true;
	}
	return this->sorter->IsEnd();
}

SQInteger ScriptList::Count()
{
	return this->items.size();
}

SQInteger ScriptList::GetValue(SQInteger item)
{
	ScriptListMap::iterator item_iter = this->items.find(item);
	return item_iter == this->items.end() ? 0 : item_iter->second;
}

void ScriptList::SetIterValue(ScriptListMap::iterator item_iter, SQInteger value)
{
	SQInteger value_old = item_iter->second;
	if (value_old == value) return;

	SQInteger item = item_iter->first;

	if (this->initialized && this->sorter_type == SORT_BY_VALUE) this->sorter->Remove(item);

	item_iter->second = value;

	if (this->values_inited) {
		this->values.erase(this->values.find(std::make_pair(value_old, item)));
		this->values.insert(std::make_pair(value, item));
	}
}

bool ScriptList::SetValue(SQInteger item, SQInteger value)
{
	this->modifications++;

	ScriptListMap::iterator item_iter = this->items.find(item);
	if (item_iter == this->items.end()) return false;

	this->SetIterValue(item_iter, value);

	return true;
}

void ScriptList::Sort(SorterType sorter, bool ascending)
{
	this->modifications++;

	if (sorter != SORT_BY_VALUE && sorter != SORT_BY_ITEM) return;
	if (sorter == this->sorter_type && ascending == this->sort_ascending) return;

	this->sorter.reset();
	this->sorter_type    = sorter;
	this->sort_ascending = ascending;
	this->initialized    = false;
}

void ScriptList::AddList(ScriptList *list)
{
	if (list == this) return;

	if (this->IsEmpty()) {
		/* If this is empty, we can just take the items of the other list as is. */
		this->items = list->items;
		this->values = list->values;
		this->values_inited = list->values_inited;
		if (!this->values_inited && this->initialized && this->sorter_type == SORT_BY_VALUE) {
			this->InitValues();
		}
		this->modifications++;
		Squirrel::IncreaseAllocatedSize(SCRIPT_LIST_BYTES_PER_ITEM * this->items.size());
	} else {
		for (const auto &it : list->items) {
			this->AddOrSetItem(it.first, it.second);
		}
	}
}

void ScriptList::SwapList(ScriptList *list)
{
	if (list == this) return;

	this->items.swap(list->items);
	this->values.swap(list->values);
	std::swap(this->sorter, list->sorter);
	std::swap(this->sorter_type, list->sorter_type);
	std::swap(this->sort_ascending, list->sort_ascending);
	std::swap(this->initialized, list->initialized);
	std::swap(this->values_inited, list->values_inited);
	std::swap(this->modifications, list->modifications);
	if (this->sorter != nullptr) this->sorter->Retarget(this);
	if (list->sorter != nullptr) list->sorter->Retarget(list);
}

template <class ValueFilter>
void ScriptList::RemoveItems(ValueFilter value_filter)
{
	this->modifications++;

	const size_t old_size = this->items.size();

	if (!this->initialized || this->sorter->IsEnd()) {
		/* Fast path */
		this->items.erase_count_if(this->items.begin(), this->items.size(), [value_filter](const ScriptListMap::value_type &item) -> bool {
			return value_filter(item.first, item.second);
		});
		if (this->values_inited) {
			this->values.erase_count_if(this->values.begin(), this->values.size(), [value_filter](const ScriptListValueSet::value_type &item) -> bool {
				return value_filter(item.second, item.first);
			});
			assert(this->values.size() == this->items.size());
		}

		Squirrel::DecreaseAllocatedSize((old_size - this->items.size()) * SCRIPT_LIST_BYTES_PER_ITEM);
		ScriptController::DecreaseOps(static_cast<int>(old_size / 16 + (old_size - this->items.size()) * 4));
		return;
	}

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end();) {
		if (value_filter(iter->first, iter->second)) {
			iter = this->RemoveIter(iter);
		} else {
			++iter;
		}
	}

	ScriptController::DecreaseOps(static_cast<int>(old_size / 16 + (old_size - this->items.size()) * 4));
}

void ScriptList::RemoveAboveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v > value; });
}

void ScriptList::RemoveBelowValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v < value; });
}

void ScriptList::RemoveBetweenValue(SQInteger start, SQInteger end)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v > start && v < end; });
}

void ScriptList::RemoveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v == value; });
}

template <bool KEEP_BOTTOM>
bool ScriptList::KeepTopBottomFastPath(SQInteger count)
{
	if (count * 5 <= static_cast<SQInteger>(this->items.size() * 4)) return false;

	/* Fast path: keeping <= 20% of list, and don't need to update the sorter, just create new container(s) */
	SQInteger keep = this->Count() - count;

	using ItemMap = btree::btree_map<SQInteger, SQInteger>;
	using ValueSet = btree::btree_set<std::pair<SQInteger, SQInteger>>;
	ItemMap new_items;                                 ///< The new items in the list
	ValueSet new_values;                               ///< The new items in the list, sorted by value
	auto old_items = this->items.unprotected_view();   ///< The old/current items in the list
	auto old_values = this->values.unprotected_view(); ///< The old/current items in the list, sorted by value

	switch (this->sorter_type) {
		default: NOT_REACHED();
		case SORT_BY_VALUE:
			if (this->values_inited) {
				ValueSet::const_iterator iter;
				if constexpr (KEEP_BOTTOM) {
					iter = old_values.end();
					iter.decrement_by(static_cast<ptrdiff_t>(keep));
				} else {
					iter = old_values.begin();
				}
				while (true) {
					new_values.insert(new_values.end(), *iter);
					new_items.emplace(iter->second, iter->first);
					if (--keep <= 0) break;
					++iter;
				}
			} else {
				for (const auto &iter : old_items) {
					auto to_insert = std::make_pair(iter.second, iter.first);
					if (static_cast<SQInteger>(new_values.size()) < keep) {
						new_values.insert(to_insert);
						continue;
					}

					if constexpr (KEEP_BOTTOM) {
						if (to_insert < *new_values.begin()) continue;
						new_values.erase(new_values.begin());
					} else {
						if (to_insert > *std::prev(new_values.end())) continue;
						new_values.erase(std::prev(new_values.end()));
					}

					new_values.insert(to_insert);
				}
				for (const auto &it : new_values) {
					new_items.emplace(it.second, it.first);
				}
			}
			break;

		case SORT_BY_ITEM: {
			ItemMap::const_iterator iter;
			if constexpr (KEEP_BOTTOM) {
				iter = old_items.end();
				iter.decrement_by(static_cast<ptrdiff_t>(keep));
			} else {
				iter = old_items.begin();
			}
			while (true) {
				new_items.insert(new_items.end(), *iter);
				if (--keep <= 0) break;
				++iter;
			}
			break;
		}
	}

	Squirrel::DecreaseAllocatedSize((old_items.size() - new_items.size()) * SCRIPT_LIST_BYTES_PER_ITEM);
	this->items.swap(new_items);
	this->values.swap(new_values);
	this->values_inited = !this->values.empty();

	return true;
}

void ScriptList::RemoveTop(SQInteger count)
{
	this->modifications++;

	if (!this->sort_ascending) {
		this->Sort(this->sorter_type, !this->sort_ascending);
		this->RemoveBottom(count);
		this->Sort(this->sorter_type, !this->sort_ascending);
		return;
	}
	if (this->sorter != nullptr) this->sorter->End();

	if (count <= 0) return;
	if (count >= this->Count()) {
		this->Clear();
		return;
	}

	ScriptController::DecreaseOps(count * 3);

	if (this->KeepTopBottomFastPath<true>(count)) return;

	switch (this->sorter_type) {
		default: NOT_REACHED();
		case SORT_BY_VALUE:
			if (!this->values_inited) this->InitValues();
			for (ScriptListValueSet::iterator iter = this->values.begin(); iter != this->values.end(); iter = this->values.begin()) {
				if (--count < 0) return;
				this->RemoveValueIter(iter);
			}
			break;

		case SORT_BY_ITEM:
			for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end(); iter = this->items.begin()) {
				if (--count < 0) return;
				this->RemoveIter(iter);
			}
			break;
	}
}

void ScriptList::RemoveBottom(SQInteger count)
{
	this->modifications++;

	if (!this->sort_ascending) {
		this->Sort(this->sorter_type, !this->sort_ascending);
		this->RemoveTop(count);
		this->Sort(this->sorter_type, !this->sort_ascending);
		return;
	}
	if (this->sorter != nullptr) this->sorter->End();

	if (count <= 0) return;
	if (count >= this->Count()) {
		this->Clear();
		return;
	}

	ScriptController::DecreaseOps(count * 3);

	if (this->KeepTopBottomFastPath<false>(count)) return;

	switch (this->sorter_type) {
		default: NOT_REACHED();
		case SORT_BY_VALUE:
			if (!this->values_inited) this->InitValues();
			for (ScriptListValueSet::iterator iter = this->values.end(); iter != this->values.begin(); iter = this->values.end()) {
				if (--count < 0) return;
				--iter;
				this->RemoveValueIter(iter);
			}
			break;

		case SORT_BY_ITEM:
			for (ScriptListMap::iterator iter = this->items.end(); iter != this->items.begin(); iter = this->items.end()) {
				if (--count < 0) return;
				--iter;
				this->RemoveIter(iter);
			}
			break;
	}
}

void ScriptList::RemoveList(ScriptList *list)
{
	this->modifications++;

	if (list == this) {
		this->Clear();
	} else {
		for (const auto &it : list->items.unprotected_view()) {
			this->RemoveItem(it.first);
		}
	}
}

void ScriptList::KeepAboveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v <= value; });
}

void ScriptList::KeepBelowValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v >= value; });
}

void ScriptList::KeepBetweenValue(SQInteger start, SQInteger end)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v <= start || v >= end; });
}

void ScriptList::KeepValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v != value; });
}

void ScriptList::KeepTop(SQInteger count)
{
	this->modifications++;

	this->RemoveBottom(this->Count() - count);
}

void ScriptList::KeepBottom(SQInteger count)
{
	this->modifications++;

	this->RemoveTop(this->Count() - count);
}

void ScriptList::KeepList(ScriptList *list)
{
	if (list == this) return;
	this->RemoveItems([&](const SQInteger &k, const SQInteger &) { return !list->HasItem(k); });
}

SQInteger ScriptList::_get(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	ScriptListMap::const_iterator item_iter = this->items.find(idx);
	if (item_iter == this->items.end()) return SQ_ERROR;

	sq_pushinteger(vm, item_iter->second);
	return 1;
}

SQInteger ScriptList::_set(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	/* Retrieve the return value */
	SQInteger val;
	switch (sq_gettype(vm, 3)) {
		case OT_NULL:
			this->RemoveItem(idx);
			return 0;

		case OT_BOOL: {
			SQBool v;
			sq_getbool(vm, 3, &v);
			val = v ? 1 : 0;
			break;
		}

		case OT_INTEGER:
			sq_getinteger(vm, 3, &val);
			break;

		default:
			return sq_throwerror(vm, "you can only assign integers to this list");
	}

	this->AddOrSetItem(idx, val);
	return 0;
}

SQInteger ScriptList::_nexti(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) == OT_NULL) {
		if (this->IsEmpty()) {
			sq_pushnull(vm);
			return 1;
		}
		sq_pushinteger(vm, this->Begin());
		return 1;
	}

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	SQInteger val = this->Next();
	if (this->IsEnd()) {
		sq_pushnull(vm);
		return 1;
	}

	sq_pushinteger(vm, val);
	return 1;
}

SQInteger ScriptList::Valuate(HSQUIRRELVM vm)
{
	this->modifications++;

	/* The first parameter is the instance of ScriptList. */
	int nparam = sq_gettop(vm) - 1;

	if (nparam < 1) {
		return sq_throwerror(vm, "You need to give at least a Valuator as parameter to ScriptList::Valuate");
	}

	/* Make sure the valuator function is really a function, and not any
	 * other type. It's parameter 2 for us, but for the user it's the
	 * first parameter they give. */
	SQObjectType valuator_type = sq_gettype(vm, 2);
	if (valuator_type != OT_CLOSURE && valuator_type != OT_NATIVECLOSURE) {
		return sq_throwerror(vm, "parameter 1 has an invalid type (expected function)");
	}

	/* Don't allow docommand from a Valuator, as we can't resume in
	 * mid C++-code. */
	ScriptObject::DisableDoCommandScope disabler{};

	/* Limit the total number of ops that can be consumed by a valuate operation */
	SQOpsLimiter limiter(vm, MAX_VALUATE_OPS, "valuator function");

	/* Push the function to call */
	sq_push(vm, 2);

	ScriptListMap::iterator begin;
	if (disabler.GetOriginalValue() && this->resume_item.has_value()) {
		begin = this->items.lower_bound(this->resume_item.value());
	} else {
		begin = this->items.begin();
	}

	for (ScriptListMap::iterator iter = begin; iter != this->items.end(); ++iter) {
		const auto item = iter->first;
		if (disabler.GetOriginalValue() && item != this->resume_item && ScriptController::GetOpsTillSuspend() < 0) {
			this->resume_item = item;
			/* Pop the valuator function. */
			sq_poptop(vm);
			sq_pushbool(vm, SQTrue);
			return 1;
		}

		/* Check for changing of items. */
		int previous_modification_count = this->modifications;

		/* Push the root table as instance object, this is what squirrel does for meta-functions. */
		sq_pushroottable(vm);
		/* Push all arguments for the valuator function. */
		sq_pushinteger(vm, item);
		for (int i = 0; i < nparam - 1; i++) {
			sq_push(vm, i + 3);
		}

		/* Call the function. Squirrel pops all parameters and pushes the return value. */
		if (SQ_FAILED(sq_call(vm, nparam + 1, SQTrue, SQFalse))) {
			return SQ_ERROR;
		}

		/* Retrieve the return value */
		SQInteger value;
		switch (sq_gettype(vm, -1)) {
			case OT_INTEGER: {
				sq_getinteger(vm, -1, &value);
				break;
			}

			case OT_BOOL: {
				SQBool v;
				sq_getbool(vm, -1, &v);
				value = v ? 1 : 0;
				break;
			}

			default: {
				/* Pop the valuator function and the return value. */
				sq_pop(vm, 2);

				return sq_throwerror(vm, "return value of valuator is not valid (not integer/bool)");
			}
		}

		/* Was something changed? */
		if (previous_modification_count != this->modifications) {
			/* Pop the valuator function and the return value. */
			sq_pop(vm, 2);

			return sq_throwerror(vm, "modifying valuated list outside of valuator function");
		}

		this->SetIterValue(iter, value);

		/* Pop the return value. */
		sq_poptop(vm);

		Squirrel::DecreaseOps(vm, 5);
	}

	/* Pop the valuator function from the squirrel stack. */
	sq_poptop(vm);

	this->resume_item.reset();
	sq_pushbool(vm, SQFalse);
	return 1;
}
