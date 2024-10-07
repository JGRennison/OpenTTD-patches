/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file arena_alloc.hpp Arena allocator. */

#ifndef ARENA_ALLOC_HPP
#define ARENA_ALLOC_HPP

#include <type_traits>
#include <vector>

/**
 * Custom bump allocator for uniform-size allocations, no free support.
 */
template <size_t SIZE, uint N_PER_CHUNK>
class UniformBumpAllocator {
	static_assert(SIZE > 0);
	static_assert(N_PER_CHUNK > 0);
	std::vector<char *> used_blocks;

	char *next_ptr = nullptr;
	char *end_ptr = nullptr;

	void NewBlock()
	{
		this->next_ptr = static_cast<char *>(malloc(SIZE * N_PER_CHUNK));
		assert(this->next_ptr != nullptr);
		this->end_ptr = this->next_ptr + (SIZE * N_PER_CHUNK);
		this->used_blocks.push_back(this->next_ptr);
	}

public:
	UniformBumpAllocator() = default;
	UniformBumpAllocator(const UniformBumpAllocator &other) = delete;
	UniformBumpAllocator& operator=(const UniformBumpAllocator &other) = delete;

	~UniformBumpAllocator()
	{
		this->ClearArena();
	}

	void ClearArena()
	{
		this->next_ptr = nullptr;
		this->end_ptr = nullptr;
		for (char *block : this->used_blocks) {
			free(block);
		}
		this->used_blocks.clear();
	}

	void *Allocate()
	{
		if (this->next_ptr == this->end_ptr) {
			this->NewBlock();
		}
		void *out = this->next_ptr;
		this->next_ptr += SIZE;
		return out;
	}

	template <typename F>
	void IterateAllocations(F handler) const
	{
		for (char *block : this->used_blocks) {
			for (size_t i = 0; i < N_PER_CHUNK; i++) {
				if (block == this->end_ptr) return;
				handler(block);
				block += SIZE;
			}
		}
	}

	void *GetAllocationAt(size_t index) const
	{
		return this->used_blocks[index / N_PER_CHUNK] + (SIZE * (index % N_PER_CHUNK));
	}

	size_t AllocationCount() const
	{
		if (this->used_blocks.empty()) return 0;
		return ((this->used_blocks.size() - 1) * N_PER_CHUNK) + ((this->next_ptr - this->used_blocks.back()) / SIZE);
	}
};

/**
 * Container based on a bump allocator.
 * Allocated items are never moved.
 * Allocated items are only freed/destructed when the entire container
 * is freed using clear() or the container is destructed.
 */
template <typename T, uint N_PER_CHUNK>
class BumpAllocContainer {
	UniformBumpAllocator<sizeof(T), N_PER_CHUNK> base_allocator;

	void DestructItems()
	{
		if constexpr (!std::is_trivially_destructible<T>{}) {
			/* Call all destructors if not trivially destructable */
			this->Iterate([](T *ptr) {
				ptr->~T();
			});
		}
	}

public:
	~BumpAllocContainer()
	{
		this->DestructItems();
	}

	void clear()
	{
		this->DestructItems();
		this->base_allocator.ClearArena();
	}

	size_t size() const
	{
		return this->base_allocator.AllocationCount();
	}

	template <typename... Args>
	T *New(Args&&... args)
	{
		return new (this->base_allocator.Allocate()) T(std::forward<Args>(args)...);
	}

	T *Get(size_t index) const
	{
		return static_cast<T *>(this->base_allocator.GetAllocationAt(index));
	}

	template <typename F>
	void Iterate(F handler) const
	{
		this->base_allocator.IterateAllocations([&](void *ptr) {
			handler(static_cast<T *>(ptr));
		});
	}
};

/**
 * Custom arena allocator for uniform-size allocations.
 */
template <size_t SIZE, uint N_PER_CHUNK>
class UniformArenaAllocator {
	static_assert(SIZE >= sizeof(void *));

	UniformBumpAllocator<SIZE, N_PER_CHUNK> base_allocator;
	void *last_freed = nullptr;

public:
	UniformArenaAllocator() = default;
	UniformArenaAllocator(const UniformArenaAllocator &other) = delete;
	UniformArenaAllocator& operator=(const UniformArenaAllocator &other) = delete;

	void ClearArena()
	{
		this->base_allocator.ClearArena();
		this->last_freed = nullptr;
	}

	void *Allocate()
	{
		if (last_freed != nullptr) {
			void *ptr = last_freed;
			last_freed = *reinterpret_cast<void**>(ptr);
			return ptr;
		} else {
			return this->base_allocator.Allocate();
		}
	}

	void Free(void *ptr)
	{
		if (ptr == nullptr) return;

		*reinterpret_cast<void**>(ptr) = this->last_freed;
		this->last_freed = ptr;
	}
};

#endif /* ARENA_ALLOC_HPP */
