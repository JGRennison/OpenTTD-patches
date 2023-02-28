/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dyn_arena_alloc.hpp Dynamic chunk-size arena allocator. */

#ifndef DYN_ARENA_ALLOC_HPP
#define DYN_ARENA_ALLOC_HPP

#include <vector>

/**
 * Custom arena allocator for uniform-size allocations of a variable size.
 * The allocation and chunk sizes may only be changed when the arena is empty.
 */
class DynUniformArenaAllocator {
	std::vector<void *> used_blocks;

	void *current_block = nullptr;
	void *last_freed = nullptr;
	size_t next_position = 0;

	size_t item_size = 0;
	size_t items_per_chunk = 0;

	void NewBlock()
	{
		current_block = malloc(item_size * items_per_chunk);
		assert(current_block != nullptr);
		next_position = 0;
		used_blocks.push_back(current_block);
	}

public:
	DynUniformArenaAllocator() = default;
	DynUniformArenaAllocator(const DynUniformArenaAllocator &other) = delete;
	DynUniformArenaAllocator& operator=(const DynUniformArenaAllocator &other) = delete;

	~DynUniformArenaAllocator()
	{
		EmptyArena();
	}

	void EmptyArena()
	{
		current_block = nullptr;
		last_freed = nullptr;
		next_position = 0;
		for (void *block : used_blocks) {
			free(block);
		}
		used_blocks.clear();
	}

	void ResetArena()
	{
		EmptyArena();
		item_size = 0;
		items_per_chunk = 0;
	}

	void *Allocate() {
		assert(item_size != 0);
		if (last_freed) {
			void *ptr = last_freed;
			last_freed = *reinterpret_cast<void**>(ptr);
			return ptr;
		} else {
			if (current_block == nullptr || next_position == items_per_chunk) {
				NewBlock();
			}
			void *out = reinterpret_cast<char *>(current_block) + (item_size * next_position);
			next_position++;
			return out;
		}
	}

	void Free(void *ptr) {
		if (!ptr) return;
		assert(current_block != nullptr);

		*reinterpret_cast<void**>(ptr) = last_freed;
		last_freed = ptr;
	}

	void SetParameters(size_t item_size, size_t items_per_chunk)
	{
		if (item_size < sizeof(void *)) item_size = sizeof(void *);
		if (this->item_size == item_size && this->items_per_chunk == items_per_chunk) return;

		assert(current_block == nullptr);
		this->item_size = item_size;
		this->items_per_chunk = items_per_chunk;
	}
};

#endif /* DYN_ARENA_ALLOC_HPP */
