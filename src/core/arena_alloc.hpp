/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file arena_alloc.hpp Arena allocator. */

#ifndef ARENA_ALLOC_HPP
#define ARENA_ALLOC_HPP

#include <vector>

/**
 * Custom arena allocator for uniform-size allocations.
 */
template <size_t SIZE, uint N_PER_CHUNK>
class UniformArenaAllocator {
	static_assert(SIZE >= sizeof(void *));

	std::vector<void *> used_blocks;

	void *current_block = nullptr;
	void *last_freed = nullptr;
	size_t next_position = 0;

	void NewBlock()
	{
		current_block = malloc(SIZE * N_PER_CHUNK);
		assert(current_block != nullptr);
		next_position = 0;
		used_blocks.push_back(current_block);
	}

public:
	UniformArenaAllocator() = default;
	UniformArenaAllocator(const UniformArenaAllocator &other) = delete;
	UniformArenaAllocator& operator=(const UniformArenaAllocator &other) = delete;

	~UniformArenaAllocator()
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
	}

	void *Allocate() {
		if (last_freed) {
			void *ptr = last_freed;
			last_freed = *reinterpret_cast<void**>(ptr);
			return ptr;
		} else {
			if (current_block == nullptr || next_position == N_PER_CHUNK) {
				NewBlock();
			}
			void *out = reinterpret_cast<char *>(current_block) + (SIZE * next_position);
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
};

#endif /* ARENA_ALLOC_HPP */
