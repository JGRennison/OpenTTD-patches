/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file alloc_func.hpp Functions related to the allocation of memory */

#ifndef ALLOC_FUNC_HPP
#define ALLOC_FUNC_HPP

/*
 * Functions to exit badly with an error message.
 * It has to be linked so the error messages are not
 * duplicated in each object file making the final
 * binary needlessly large.
 */

[[noreturn]] void MallocError(size_t size);
[[noreturn]] void ReallocError(size_t size);

/**
 * Checks whether allocating memory would overflow size_t.
 *
 * @param element_size Size of the structure to allocate.
 * @param num_elements Number of elements to allocate.
 */
inline void CheckAllocationConstraints(size_t element_size, size_t num_elements)
{
	if (num_elements > SIZE_MAX / element_size) MallocError(SIZE_MAX);
}

/**
 * Checks whether allocating memory would overflow size_t.
 *
 * @tparam T Structure to allocate.
 * @param num_elements Number of elements to allocate.
 */
template <typename T>
inline void CheckAllocationConstraints(size_t num_elements)
{
	CheckAllocationConstraints(sizeof(T), num_elements);
}

/**
 * Simplified allocation function that allocates the specified number of
 * elements of the given type. It also explicitly casts it to the requested
 * type.
 * @note throws an error when there is no memory anymore.
 * @note the memory contains garbage data (i.e. possibly non-zero values).
 * @tparam T the type of the variable(s) to allocation.
 * @param num_elements the number of elements to allocate of the given type.
 * @return nullptr when num_elements == 0, non-nullptr otherwise.
 */
template <typename T>
inline T *MallocT(size_t num_elements)
{
	/*
	 * MorphOS cannot handle 0 elements allocations, or rather that always
	 * returns nullptr. So we do that for *all* allocations, thus causing it
	 * to behave the same on all OSes.
	 */
	if (num_elements == 0) return nullptr;

	/* Ensure the size does not overflow. */
	CheckAllocationConstraints<T>(num_elements);

	T *t_ptr = (T*)malloc(num_elements * sizeof(T));
	if (t_ptr == nullptr) MallocError(num_elements * sizeof(T));
	return t_ptr;
}

/**
 * Simplified allocation function that allocates the specified number of
 * elements of the given type. It also explicitly casts it to the requested
 * type.
 * @note throws an error when there is no memory anymore.
 * @note the memory contains all zero values.
 * @tparam T the type of the variable(s) to allocation.
 * @param num_elements the number of elements to allocate of the given type.
 * @return nullptr when num_elements == 0, non-nullptr otherwise.
 */
template <typename T>
inline T *CallocT(size_t num_elements)
{
	/*
	 * MorphOS cannot handle 0 elements allocations, or rather that always
	 * returns nullptr. So we do that for *all* allocations, thus causing it
	 * to behave the same on all OSes.
	 */
	if (num_elements == 0) return nullptr;

	T *t_ptr = (T*)calloc(num_elements, sizeof(T));
	if (t_ptr == nullptr) MallocError(num_elements * sizeof(T));
	return t_ptr;
}

/**
 * Simplified reallocation function that allocates the specified number of
 * elements of the given type. It also explicitly casts it to the requested
 * type. It extends/shrinks the memory allocation given in t_ptr.
 * @note throws an error when there is no memory anymore.
 * @note the pointer to the data may change, but the data will remain valid.
 * @tparam T the type of the variable(s) to allocation.
 * @param t_ptr the previous allocation to extend/shrink.
 * @param num_elements the number of elements to allocate of the given type.
 * @return nullptr when num_elements == 0, non-nullptr otherwise.
 */
template <typename T>
inline T *ReallocT(T *t_ptr, size_t num_elements)
{
	/*
	 * MorphOS cannot handle 0 elements allocations, or rather that always
	 * returns nullptr. So we do that for *all* allocations, thus causing it
	 * to behave the same on all OSes.
	 */
	if (num_elements == 0) {
		free(t_ptr);
		return nullptr;
	}

	/* Ensure the size does not overflow. */
	CheckAllocationConstraints<T>(num_elements);

	t_ptr = (T*)realloc(static_cast<void *>(t_ptr), num_elements * sizeof(T));
	if (t_ptr == nullptr) ReallocError(num_elements * sizeof(T));
	return t_ptr;
}

/**
 * Temporary buffer for elements of type T.
 * Up to MAX_N elements are stored inline (on the stack).
 * Requests of more than MAX_N elements use the heap/malloc.
 * This has a similar use-case to alloca, but without the potentially problematic stack use.
 */
template <typename T, size_t MAX_N>
struct TempBufferT {
private:
	T *ptr;
	T storage[MAX_N];

	void init(size_t num_elements)
	{
		if (num_elements > MAX_N) {
			this->ptr = MallocT<T>(num_elements);
		} else {
			this->ptr = this->storage;
		}
	}

public:
	/** Construct with storage for num_elements elements, the elements are uninitialised. */
	TempBufferT(size_t num_elements)
	{
		init(num_elements);
	}

	/** Construct with storage for num_elements elements, the elements are uninitialised. */
	TempBufferT(size_t num_elements, const T &init_value)
	{
		init(num_elements);
		std::fill_n(this->ptr, num_elements, init_value);
	}

	~TempBufferT()
	{
		if (this->ptr != this->storage) free(this->ptr);
	}

	TempBufferT(const TempBufferT &other) = delete;
	TempBufferT& operator=(const TempBufferT &other) = delete;

	T *get() noexcept { return this->ptr; }
	const T *get() const noexcept { return this->ptr; }

	operator T* () noexcept { return this->get(); }
	operator const T* () const noexcept { return this->get(); }
};

template <typename T, size_t MAX_SIZE>
using TempBufferSTParent = TempBufferT<T, std::max<size_t>(MAX_SIZE / sizeof(T), 1)>;

/**
 * Temporary buffer for elements of type T.
 *
 * This is a wrapper for TempBufferT which calculates the number of elements from
 * a maximum total size. Default: 256 bytes.
 * The minimum MAX_N is 1, even if sizeof(T) > MAX_SIZE.
 */
template <typename T, size_t MAX_SIZE = 256>
struct TempBufferST : public TempBufferSTParent<T, MAX_SIZE> {
	using TempBufferSTParent<T, MAX_SIZE>::TempBufferSTParent;
};

#endif /* ALLOC_FUNC_HPP */
