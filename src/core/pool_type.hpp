/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pool_type.hpp Definition of Pool, structure used to access PoolItems, and PoolItem, base structure for Vehicle, Town, and other indexed items. */

#ifndef POOL_TYPE_HPP
#define POOL_TYPE_HPP

#include "enum_type.hpp"
#include "../debug_dbg_assert.h"
#include <vector>

/** Various types of a pool. */
enum class PoolType : uint8_t {
	Normal, ///< Normal pool containing game objects.
	NetworkClient, ///< Network client pools.
	NetworkAdmin, ///< Network admin pool.
	Data, ///< NewGRF or other data, that is not reset together with normal pools.
};
using PoolTypes = EnumBitSet<PoolType, uint8_t>;
static constexpr PoolTypes PT_ALL = {PoolType::Normal, PoolType::NetworkClient, PoolType::NetworkAdmin, PoolType::Data};

typedef std::vector<struct PoolBase *> PoolVector; ///< Vector of pointers to PoolBase

/** Base class for base of all pools. */
struct PoolBase {
	const PoolType type; ///< Type of this pool.

	/**
	 * Function used to access the vector of all pools.
	 * @return pointer to vector of all pools
	 */
	static PoolVector *GetPools()
	{
		static PoolVector *pools = new PoolVector();
		return pools;
	}

	static void Clean(PoolTypes);

	/**
	 * Constructor registers this object in the pool vector.
	 * @param pt type of this pool.
	 */
	PoolBase(PoolType pt) : type(pt)
	{
		PoolBase::GetPools()->push_back(this);
	}

	virtual ~PoolBase();

	/**
	 * Virtual method that deletes all items in the pool.
	 */
	virtual void CleanPool() = 0;

private:
	/**
	 * Dummy private copy constructor to prevent compilers from
	 * copying the structure, which fails due to GetPools().
	 */
	PoolBase(const PoolBase &other);
};

struct DefaultPoolItemParam{};

template <class Titem>
struct DefaultPoolOps {
	using Tptr = Titem *;
	using Tparam_type = DefaultPoolItemParam;

	static constexpr Titem *GetPtr(Titem *ptr) { return ptr; }
	static constexpr Titem *PutPtr(Titem *ptr, DefaultPoolItemParam param) { return ptr; }
	static constexpr Titem *NullValue() { return nullptr; }
	static constexpr DefaultPoolItemParam DefaultItemParam() { return {}; }
};

/**
 * Base class for all pools.
 * @tparam Titem        Type of the class/struct that is going to be pooled
 * @tparam Tindex       Type of the index for this pool
 * @tparam Tgrowth_step Size of growths; if the pool is full increase the size by this amount
 * @tparam Tmax_size    Maximum size of the pool
 * @tparam Tpool_type   Type of this pool
 * @tparam Tcache       Whether to perform 'alloc' caching, i.e. don't actually free/malloc just reuse the memory
 * @tparam Tzero        Whether to zero the memory
 * @warning when Tcache is enabled *all* instances of this pool's item must be of the same size.
 */
template <class Titem, typename Tindex, size_t Tgrowth_step, size_t Tmax_size, PoolType Tpool_type = PoolType::Normal, bool Tcache = false, bool Tzero = true, typename Tops = DefaultPoolOps<Titem> >
struct Pool : PoolBase {
	using ParamType = typename Tops::Tparam_type;
	using PtrType = typename Tops::Tptr;

private:
	/** Some helper functions to get the maximum value of the provided index. */
	template <typename T>
	static constexpr size_t GetMaxIndexValue(T) { return std::numeric_limits<T>::max(); }
	template <typename T> requires std::is_enum_v<T>
	static constexpr size_t GetMaxIndexValue(T) { return std::numeric_limits<std::underlying_type_t<T>>::max(); }
public:
	/* Ensure the highest possible index, i.e. Tmax_size -1, is within the bounds of Tindex. */
	static_assert(Tmax_size - 1 <= GetMaxIndexValue(Tindex{}));

	static constexpr size_t MAX_SIZE = Tmax_size; ///< Make template parameter accessible from outside

	const char * const name; ///< Name of this pool

	size_t size;         ///< Current allocated size
	size_t first_free;   ///< No item with index lower than this is free (doesn't say anything about this one!)
	size_t first_unused; ///< This and all higher indexes are free (doesn't say anything about first_unused-1 !)
	size_t items;        ///< Number of used indexes (non-nullptr)
#ifdef WITH_ASSERT
	size_t checked;      ///< Number of items we checked for
#endif /* WITH_ASSERT */
	bool cleaning;       ///< True if cleaning pool (deleting all items)

	PtrType *data;       ///< Pointer to array of Tops::Tptr (by default: pointers to Titem)
	uint64_t *free_bitmap; ///< Pointer to free bitmap

	Pool(const char *name);
	void CleanPool() override;

	inline PtrType &GetRawRef(size_t index)
	{
		dbg_assert_msg(index < this->first_unused, "index: {}, first_unused: {}, name: {}", index, this->first_unused, this->name);
		return this->data[index];
	}

	inline PtrType GetRaw(size_t index)
	{
		return this->GetRawRef(index);
	}

	/**
	 * Returns Titem with given index
	 * @param index of item to get
	 * @return pointer to Titem
	 * @pre index < this->first_unused
	 */
	inline Titem *Get(size_t index)
	{
		return Tops::GetPtr(this->GetRaw(index));
	}

	/**
	 * Tests whether given index can be used to get valid (non-nullptr) Titem
	 * @param index index to examine
	 * @return true if PoolItem::Get(index) will return non-nullptr pointer
	 */
	inline bool IsValidID(size_t index)
	{
		return index < this->first_unused && this->GetRaw(index) != Tops::NullValue();
	}

	/**
	 * Tests whether we can allocate 'n' items
	 * @param n number of items we want to allocate
	 * @return true if 'n' items can be allocated
	 */
	inline bool CanAllocate(size_t n = 1)
	{
		bool ret = this->items <= Tmax_size - n;
#ifdef WITH_ASSERT
		this->checked = ret ? n : 0;
#endif /* WITH_ASSERT */
		return ret;
	}

	/**
	 * Iterator to iterate all valid T of a pool
	 * @tparam T Type of the class/struct that is going to be iterated
	 */
	template <class T>
	struct PoolIterator {
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;
		typedef size_t difference_type;
		typedef std::forward_iterator_tag iterator_category;

		explicit PoolIterator(size_t index) : index(index)
		{
			this->ValidateIndex();
		};

		bool operator==(const PoolIterator &other) const { return this->index == other.index; }
		T * operator*() const { return T::Get(this->index); }
		PoolIterator & operator++() { this->index++; this->ValidateIndex(); return *this; }

	private:
		size_t index;
		void ValidateIndex()
		{
			while (this->index < T::GetPoolSize() && !(T::IsValidID(this->index))) this->index++;
			if (this->index >= T::GetPoolSize()) this->index = T::Pool::MAX_SIZE;
		}
	};

	/*
	 * Iterable ensemble of all valid T
	 * @tparam T Type of the class/struct that is going to be iterated
	 */
	template <class T>
	struct IterateWrapper {
		size_t from;
		IterateWrapper(size_t from = 0) : from(from) {}
		PoolIterator<T> begin() { return PoolIterator<T>(this->from); }
		PoolIterator<T> end() { return PoolIterator<T>(T::Pool::MAX_SIZE); }
		bool empty() { return this->begin() == this->end(); }
	};

	/**
	 * Iterator to iterate all valid T of a pool
	 * @tparam T Type of the class/struct that is going to be iterated
	 */
	template <class T, class F>
	struct PoolIteratorFiltered {
		typedef T value_type;
		typedef T *pointer;
		typedef T &reference;
		typedef size_t difference_type;
		typedef std::forward_iterator_tag iterator_category;

		explicit PoolIteratorFiltered(size_t index, F filter) : index(index), filter(filter)
		{
			this->ValidateIndex();
		};

		bool operator==(const PoolIteratorFiltered &other) const { return this->index == other.index; }
		T * operator*() const { return T::Get(this->index); }
		PoolIteratorFiltered & operator++() { this->index++; this->ValidateIndex(); return *this; }

	private:
		size_t index;
		F filter;
		void ValidateIndex()
		{
			while (this->index < T::GetPoolSize() && !(T::IsValidID(this->index) && this->filter(this->index))) this->index++;
			if (this->index >= T::GetPoolSize()) this->index = T::Pool::MAX_SIZE;
		}
	};

	/*
	 * Iterable ensemble of all valid T
	 * @tparam T Type of the class/struct that is going to be iterated
	 */
	template <class T, class F>
	struct IterateWrapperFiltered {
		size_t from;
		F filter;
		IterateWrapperFiltered(size_t from, F filter) : from(from), filter(filter) {}
		PoolIteratorFiltered<T, F> begin() { return PoolIteratorFiltered<T, F>(this->from, this->filter); }
		PoolIteratorFiltered<T, F> end() { return PoolIteratorFiltered<T, F>(T::Pool::MAX_SIZE, this->filter); }
		bool empty() { return this->begin() == this->end(); }
	};

	/**
	 * Base class for all PoolItems
	 * @tparam Tpool The pool this item is going to be part of
	 */
	template <struct Pool<Titem, Tindex, Tgrowth_step, Tmax_size, Tpool_type, Tcache, Tzero, Tops> *Tpool>
	struct PoolItem {
		Tindex index; ///< Index of this pool item

		/** Type of the pool this item is going to be part of */
		typedef struct Pool<Titem, Tindex, Tgrowth_step, Tmax_size, Tpool_type, Tcache, Tzero, Tops> Pool;

protected:
		static inline void *NewWithParam(size_t size, ParamType param)
		{
			return Tpool->GetNew(size, param);
		}

		static inline void *NewWithParam(size_t size, size_t index, ParamType param)
		{
			return Tpool->GetNew(size, index, param);
		}

public:
		/**
		 * Allocates space for new Titem
		 * @param size size of Titem
		 * @return pointer to allocated memory
		 * @note can never fail (return nullptr), use CanAllocate() to check first!
		 */
		inline void *operator new(size_t size)
		{
			return NewWithParam(size, Tops::DefaultItemParam());
		}

		/**
		 * Marks Titem as free. Its memory is released
		 * @param p memory to free
		 * @note the item has to be allocated in the pool!
		 */
		inline void operator delete(void *p)
		{
			if (p == nullptr) return;
			Titem *pn = static_cast<Titem *>(p);
			dbg_assert_msg(pn == Tpool->Get(pn->index), "name: {}", Tpool->name);
			Tpool->FreeItem(pn->index);
		}

		/**
		 * Allocates space for new Titem with given index
		 * @param size size of Titem
		 * @param index index of item
		 * @return pointer to allocated memory
		 * @note can never fail (return nullptr), use CanAllocate() to check first!
		 * @pre index has to be unused! Else it will crash
		 */
		inline void *operator new(size_t size, size_t index)
		{
			return NewWithParam(size, index, Tops::DefaultItemParam());
		}

		/**
		 * Allocates space for new Titem at given memory address
		 * @param ptr where are we allocating the item?
		 * @return pointer to allocated memory (== ptr)
		 * @note use of this is strongly discouraged
		 * @pre the memory must not be allocated in the Pool!
		 */
		inline void *operator new(size_t, void *ptr)
		{
			for (size_t i = 0; i < Tpool->first_unused; i++) {
				/* Don't allow creating new objects over existing.
				 * Even if we called the destructor and reused this memory,
				 * we don't know whether 'size' and size of currently allocated
				 * memory are the same (because of possible inheritance).
				 * Use { size_t index = item->index; delete item; new (index) item; }
				 * instead to make sure destructor is called and no memory leaks. */
				dbg_assert_msg(ptr != Tpool->data[i], "name: {}", Tpool->name);
			}
			return ptr;
		}


		/** Helper functions so we can use PoolItem::Function() instead of _poolitem_pool.Function() */

		/**
		 * Tests whether we can allocate 'n' items
		 * @param n number of items we want to allocate
		 * @return true if 'n' items can be allocated
		 */
		static inline bool CanAllocateItem(size_t n = 1)
		{
			return Tpool->CanAllocate(n);
		}

		/**
		 * Returns current state of pool cleaning - yes or no
		 * @return true iff we are cleaning the pool now
		 */
		static inline bool CleaningPool()
		{
			return Tpool->cleaning;
		}

		/**
		 * Tests whether given index can be used to get valid (non-nullptr) Titem
		 * @param index index to examine
		 * @return true if PoolItem::Get(index) will return non-nullptr pointer
		 */
		static inline bool IsValidID(size_t index)
		{
			return Tpool->IsValidID(index);
		}

		/**
		 * Returns Titem with given index
		 * @param index of item to get
		 * @return pointer to Titem
		 * @pre index < this->first_unused
		 */
		static inline Titem *Get(size_t index)
		{
			return Tpool->Get(index);
		}

		/**
		 * Returns Titem with given index
		 * @param index of item to get
		 * @return pointer to Titem
		 * @note returns nullptr for invalid index
		 */
		static inline Titem *GetIfValid(size_t index)
		{
			return index < Tpool->first_unused ? Tpool->Get(index) : nullptr;
		}

		/**
		 * Returns first unused index. Useful when iterating over
		 * all pool items.
		 * @return first unused index
		 */
		static inline size_t GetPoolSize()
		{
			return Tpool->first_unused;
		}

		/**
		 * Returns number of valid items in the pool
		 * @return number of valid items in the pool
		 */
		static inline size_t GetNumItems()
		{
			return Tpool->items;
		}

		/**
		 * Dummy function called after destructor of each member.
		 * If you want to use it, override it in PoolItem's subclass.
		 * @param index index of deleted item
		 * @note when this function is called, PoolItem::Get(index) == nullptr.
		 * @note it's called only when !CleaningPool()
		 */
		static inline void PostDestructor([[maybe_unused]] size_t index) { }

		/**
		 * Dummy function called before a pool is about to be cleaned.
		 * If you want to use it, override it in PoolItem's subclass.
		 * @note it's called only when CleaningPool()
		 */
		static inline void PreCleanPool() { }

		/**
		 * Returns an iterable ensemble of all valid Titem
		 * @param from index of the first Titem to consider
		 * @return an iterable ensemble of all valid Titem
		 */
		static Pool::IterateWrapper<Titem> Iterate(size_t from = 0) { return Pool::IterateWrapper<Titem>(from); }
	};

private:
	static const size_t NO_FREE_ITEM = std::numeric_limits<size_t>::max(); ///< Constant to indicate we can't allocate any more items

	/**
	 * Helper struct to cache 'freed' PoolItems so we
	 * do not need to allocate them again.
	 */
	struct AllocCache {
		/** The next in our 'cache' */
		AllocCache *next;
	};

	/** Cache of freed pointers */
	AllocCache *alloc_cache;

	void *AllocateItem(size_t size, size_t index, ParamType param);
	void ResizeFor(size_t index);
	size_t FindFirstFree();

	void *GetNew(size_t size, ParamType param);
	void *GetNew(size_t size, size_t index, ParamType param);

	void FreeItem(size_t index);
};

#endif /* POOL_TYPE_HPP */
