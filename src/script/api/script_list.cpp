/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_list.cpp Implementation of ScriptList. */

#include "../../stdafx.h"
#include "script_list.hpp"
#include "script_controller.hpp"
#include "../../debug.h"
#include "../../script/squirrel.hpp"

#include "../../safeguards.h"

/**
 * Base class for any ScriptList sorter.
 */
class ScriptListSorter {
protected:
	ScriptList *list;       ///< The list that's being sorted.
	bool has_no_more_items; ///< Whether we have more items to iterate over.
	SQInteger item_next;    ///< The next item we will show.

public:
	/**
	 * Virtual dtor, needed to mute warnings.
	 */
	virtual ~ScriptListSorter() = default;

	/**
	 * Get the first item of the sorter.
	 */
	virtual SQInteger Begin() = 0;

	/**
	 * Stop iterating a sorter.
	 */
	virtual void End() = 0;

	/**
	 * Get the next item of the sorter.
	 */
	virtual SQInteger Next() = 0;

	/**
	 * See if the sorter has reached the end.
	 */
	bool IsEnd()
	{
		return this->list->items.empty() || this->has_no_more_items;
	}

	/**
	 * Callback from the list if an item gets removed.
	 */
	virtual void Remove(SQInteger item) = 0;

	/**
	 * Callback from the list after an item gets removed.
	 */
	virtual void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) = 0;

	/**
	 * Callback from the list if an item's value is changed.
	 */
	virtual void ValueChange(SQInteger item) = 0;

	/**
	 * Safe btree iterators hold a pointer to the parent container's tree, so update those
	 */
	virtual void RetargetIterators() = 0;

	/**
	 * Attach the sorter to a new list. This assumes the content of the old list has been moved to
	 * the new list, too, so that we don't have to invalidate any iterators.
	 * @param target New list to attach to.
	 */
	virtual void Retarget(ScriptList *new_list)
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
class ScriptListSorterValueAscending : public ScriptListSorter {
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

	SQInteger Begin() override
	{
		if (this->list->values.empty()) return 0;
		this->has_no_more_items = false;

		this->value_iter = this->list->values.begin();
		this->item_next = (*this->value_iter).second;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void End() override
	{
		this->has_no_more_items = true;
		this->item_next = 0;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext()
	{
		if (this->value_iter == this->list->values.end()) {
			this->has_no_more_items = true;
			return;
		}
		this->value_iter++;
		if (this->value_iter != this->list->values.end()) item_next = (*this->value_iter).second;
	}

	SQInteger Next() override
	{
		if (this->IsEnd()) return 0;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void Remove(SQInteger item) override
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) {
			FindNext();
			return;
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

	void ValueChange(SQInteger item) override
	{
		this->ScriptListSorterValueAscending::Remove(item);
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->values, this->value_iter);
	}
};

/**
 * Sort by value, descending.
 */
class ScriptListSorterValueDescending : public ScriptListSorter {
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

	SQInteger Begin() override
	{
		if (this->list->values.empty()) return 0;
		this->has_no_more_items = false;

		this->value_iter = this->list->values.end();
		--this->value_iter;
		this->item_next = this->value_iter->second;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void End() override
	{
		this->has_no_more_items = true;
		this->item_next = 0;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext()
	{
		if (this->value_iter == this->list->values.end()) {
			this->has_no_more_items = true;
			return;
		}
		if (this->value_iter == this->list->values.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->value_iter = this->list->values.end();
		} else {
			this->value_iter--;
		}
		if (this->value_iter != this->list->values.end()) item_next = this->value_iter->second;
	}

	SQInteger Next() override
	{
		if (this->IsEnd()) return 0;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void Remove(SQInteger item) override
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) {
			FindNext();
			return;
		}
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		/* not implemented */
	}

	void ValueChange(SQInteger item) override
	{
		this->ScriptListSorterValueDescending::Remove(item);
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->values, this->value_iter);
	}
};

/**
 * Sort by item, ascending.
 */
class ScriptListSorterItemAscending : public ScriptListSorter {
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

	SQInteger Begin() override
	{
		if (this->list->items.empty()) return 0;
		this->has_no_more_items = false;

		this->item_iter = this->list->items.begin();
		this->item_next = (*this->item_iter).first;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void End() override
	{
		this->has_no_more_items = true;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext()
	{
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		this->item_iter++;
		if (this->item_iter != this->list->items.end()) item_next = (*this->item_iter).first;
	}

	SQInteger Next() override
	{
		if (this->IsEnd()) return 0;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void Remove(SQInteger item) override
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) {
			FindNext();
			return;
		}
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

	void ValueChange(SQInteger item) override
	{
		/* do nothing */
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->items, this->item_iter);
	}
};

/**
 * Sort by item, descending.
 */
class ScriptListSorterItemDescending : public ScriptListSorter {
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

	SQInteger Begin() override
	{
		if (this->list->items.empty()) return 0;
		this->has_no_more_items = false;

		this->item_iter = this->list->items.end();
		--this->item_iter;
		this->item_next = (*this->item_iter).first;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void End() override
	{
		this->has_no_more_items = true;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext()
	{
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		if (this->item_iter == this->list->items.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->item_iter = this->list->items.end();
		} else {
			this->item_iter--;
		}
		if (this->item_iter != this->list->items.end()) item_next = (*this->item_iter).first;
	}

	SQInteger Next() override
	{
		if (this->IsEnd()) return 0;

		SQInteger item_current = this->item_next;
		FindNext();
		return item_current;
	}

	void Remove(SQInteger item) override
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) {
			FindNext();
			return;
		}
	}

	void PostErase(SQInteger item, ScriptList::ScriptListMap::iterator post_erase, ScriptList::ScriptListValueSet::iterator value_post_erase) override
	{
		/* not implemented */
	}

	void ValueChange(SQInteger item) override
	{
		/* do nothing */
	}

	void RetargetIterators() override
	{
		RetargetIterator(this->list->items, this->item_iter);
	}
};



ScriptList::ScriptList()
{
	/* Default sorter */
	this->sorter         = nullptr;
	this->sorter_type    = SORT_BY_VALUE;
	this->sort_ascending = false;
	this->initialized    = false;
	this->values_inited  = false;
	this->modifications  = 0;
}

ScriptList::~ScriptList()
{
	delete this->sorter;
}

bool ScriptList::HasItem(SQInteger item)
{
	return this->items.count(item) == 1;
}

void ScriptList::Clear()
{
	this->modifications++;

	this->items.clear();
	this->values.clear();
	this->values_inited = false;
	if (this->sorter != nullptr) this->sorter->End();
}

void ScriptList::AddOrSetItem(SQInteger item, SQInteger value)
{
	auto res = this->items.insert(std::make_pair(item, value));
	if (!res.second) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(res.first, value);
		return;
	}

	this->modifications++;
	if (this->values_inited) {
		this->values.insert(std::make_pair(value, item));
	}
}

void ScriptList::AddToItemValue(SQInteger item, SQInteger value)
{
	auto res = this->items.insert(std::make_pair(item, value));
	if (!res.second) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(res.first, res.first->second + value);
		return;
	}

	this->modifications++;
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
	this->values.clear();
	for (const auto &iter : this->items) {
		this->values.insert(std::make_pair(iter.second, iter.first));
	}
	this->values_inited = true;
}

void ScriptList::InitSorter()
{
	if (this->sorter == nullptr) {
		switch (this->sorter_type) {
			case SORT_BY_ITEM:
				if (this->sort_ascending) {
					this->sorter = new ScriptListSorterItemAscending(this);
				} else {
					this->sorter = new ScriptListSorterItemDescending(this);
				}
				break;

			case SORT_BY_VALUE:
				if (this->sort_ascending) {
					this->sorter = new ScriptListSorterValueAscending(this);
				} else {
					this->sorter = new ScriptListSorterValueDescending(this);
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
	return this->sorter->Begin();
}

SQInteger ScriptList::Next()
{
	if (!this->initialized) {
		DEBUG(script, 0, "Next() is invalid as Begin() is never called");
		return 0;
	}
	return this->sorter->Next();
}

bool ScriptList::IsEmpty()
{
	return this->items.empty();
}

bool ScriptList::IsEnd()
{
	if (!this->initialized) {
		DEBUG(script, 0, "IsEnd() is invalid as Begin() is never called");
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

	if (this->initialized) this->sorter->ValueChange(item);

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

	delete this->sorter;
	this->sorter = nullptr;
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
	} else {
		ScriptListMap *list_items = &list->items;
		for (auto &it : *list_items) {
			this->AddItem(it.first);
			this->SetValue(it.first, it.second);
		}
	}
}

void ScriptList::SwapList(ScriptList *list)
{
	if (list == this) return;

	this->items.swap(list->items);
	this->values.swap(list->values);
	Swap(this->sorter, list->sorter);
	Swap(this->sorter_type, list->sorter_type);
	Swap(this->sort_ascending, list->sort_ascending);
	Swap(this->initialized, list->initialized);
	Swap(this->values_inited, list->values_inited);
	Swap(this->modifications, list->modifications);
	if (this->sorter != nullptr) this->sorter->Retarget(this);
	if (list->sorter != nullptr) list->sorter->Retarget(list);
}

void ScriptList::RemoveAboveValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator next_iter, iter = this->items.begin(); iter != this->items.end(); iter = next_iter) {
		next_iter = iter; next_iter++;
		if ((*iter).second > value) this->RemoveItem((*iter).first);
	}
}

void ScriptList::RemoveBelowValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator next_iter, iter = this->items.begin(); iter != this->items.end(); iter = next_iter) {
		next_iter = iter; next_iter++;
		if ((*iter).second < value) this->RemoveItem((*iter).first);
	}
}

void ScriptList::RemoveBetweenValue(SQInteger start, SQInteger end)
{
	this->modifications++;

	for (ScriptListMap::iterator next_iter, iter = this->items.begin(); iter != this->items.end(); iter = next_iter) {
		next_iter = iter; next_iter++;
		if ((*iter).second > start && (*iter).second < end) this->RemoveItem((*iter).first);
	}
}

void ScriptList::RemoveValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator next_iter, iter = this->items.begin(); iter != this->items.end(); iter = next_iter) {
		next_iter = iter; next_iter++;
		if ((*iter).second == value) this->RemoveItem((*iter).first);
	}
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
		Clear();
	} else {
		ScriptListMap &list_items = list->items;
		for (ScriptListMap::iterator iter = list_items.begin(); iter != list_items.end(); iter++) {
			this->RemoveItem(iter->first);
		}
	}
}

void ScriptList::KeepAboveValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end();) {
		if (iter->second <= value) {
			iter = this->RemoveIter(iter);
		} else {
			++iter;
		}
	}
}

void ScriptList::KeepBelowValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end();) {
		if (iter->second >= value) {
			iter = this->RemoveIter(iter);
		} else {
			++iter;
		}
	}
}

void ScriptList::KeepBetweenValue(SQInteger start, SQInteger end)
{
	this->modifications++;

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end();) {
		if (iter->second <= start || iter->second >= end) {
			iter = this->RemoveIter(iter);
		} else {
			++iter;
		}
	}
}

void ScriptList::KeepValue(SQInteger value)
{
	this->modifications++;

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end();) {
		if (iter->second != value) {
			iter = this->RemoveIter(iter);
		} else {
			++iter;
		}
	}
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

	this->modifications++;

	ScriptList tmp;
	tmp.AddList(this);
	tmp.RemoveList(list);
	this->RemoveList(&tmp);
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
	if (sq_gettype(vm, 3) != OT_INTEGER && sq_gettype(vm, 3) != OT_NULL) {
		return sq_throwerror(vm, "you can only assign integers to this list");
	}

	SQInteger idx, val;
	sq_getinteger(vm, 2, &idx);
	if (sq_gettype(vm, 3) == OT_NULL) {
		this->RemoveItem(idx);
		return 0;
	}

	sq_getinteger(vm, 3, &val);
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
	bool backup_allow = ScriptObject::GetAllowDoCommand();
	ScriptObject::SetAllowDoCommand(false);

	/* Limit the total number of ops that can be consumed by a valuate operation */
	SQOpsLimiter limiter(vm, MAX_VALUATE_OPS, "valuator function");

	/* Push the function to call */
	sq_push(vm, 2);

	for (ScriptListMap::iterator iter = this->items.begin(); iter != this->items.end(); ++iter) {
		/* Check for changing of items. */
		int previous_modification_count = this->modifications;

		/* Push the root table as instance object, this is what squirrel does for meta-functions. */
		sq_pushroottable(vm);
		/* Push all arguments for the valuator function. */
		sq_pushinteger(vm, (*iter).first);
		for (int i = 0; i < nparam - 1; i++) {
			sq_push(vm, i + 3);
		}

		/* Call the function. Squirrel pops all parameters and pushes the return value. */
		if (SQ_FAILED(sq_call(vm, nparam + 1, SQTrue, SQTrue))) {
			ScriptObject::SetAllowDoCommand(backup_allow);
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
				/* See below for explanation. The extra pop is the return value. */
				sq_pop(vm, nparam + 4);

				ScriptObject::SetAllowDoCommand(backup_allow);
				return sq_throwerror(vm, "return value of valuator is not valid (not integer/bool)");
			}
		}

		/* Kill the script when the valuator call takes way too long.
		 * Triggered by nesting valuators, which then take billions of iterations. */
		if (ScriptController::GetOpsTillSuspend() < -1000000) {
			/* See below for explanation. The extra pop is the return value. */
			sq_pop(vm, nparam + 4);

			ScriptObject::SetAllowDoCommand(backup_allow);
			return sq_throwerror(vm, "excessive CPU usage in valuator function");
		}

		/* Was something changed? */
		if (previous_modification_count != this->modifications) {
			/* See below for explanation. The extra pop is the return value. */
			sq_pop(vm, nparam + 4);

			ScriptObject::SetAllowDoCommand(backup_allow);
			return sq_throwerror(vm, "modifying valuated list outside of valuator function");
		}

		this->SetIterValue(iter, value);

		/* Pop the return value. */
		sq_poptop(vm);

		Squirrel::DecreaseOps(vm, 5);
	}
	/* Pop from the squirrel stack:
	 * 1. The root stable (as instance object).
	 * 2. The valuator function.
	 * 3. The parameters given to this function.
	 * 4. The ScriptList instance object. */
	sq_pop(vm, nparam + 3);

	ScriptObject::SetAllowDoCommand(backup_allow);
	return 0;
}
