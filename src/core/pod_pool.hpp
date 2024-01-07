/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pod_pool.hpp A pool-type container for POD data. */

#ifndef POD_POOL_HPP
#define POD_POOL_HPP

#include <vector>

/**
 * Pool-type container for POD data..
 */
template <typename PTR, size_t SIZE, uint N_PER_CHUNK, typename IDX = uint32_t>
class PodPool {
	static_assert(SIZE >= sizeof(IDX));

	std::vector<void *> used_blocks;

	void *current_block = nullptr;
	IDX last_freed = (IDX)(-1);
	IDX next_position = 0;

	void NewBlock()
	{
		current_block = malloc(SIZE * N_PER_CHUNK);
		assert(current_block != nullptr);
		next_position = 0;
		used_blocks.push_back(current_block);
	}

public:
	PodPool() = default;
	PodPool(const PodPool &other) = delete;
	PodPool& operator=(const PodPool &other) = delete;

	PodPool(PodPool &&other)
	{
		*this = std::move(other);
	}

	PodPool& operator=(PodPool &&other)
	{
		this->used_blocks = std::move(other.used_blocks);
		this->current_block = other.current_block;
		this->last_freed = other.last_freed;
		this->next_position = other.next_position;
		other.used_blocks.clear();
		other.current_block = nullptr;
		other.last_freed = (IDX)(-1);
		other.next_position = 0;
		return *this;
	}

	~PodPool()
	{
		this->Clear();
	}

	void Clear()
	{
		this->current_block = nullptr;
		this->last_freed = (IDX)(-1);
		this->next_position = 0;
		for (void *block : this->used_blocks) {
			free(block);
		}
		this->used_blocks.clear();
	}

	inline PTR operator[](IDX idx) { return reinterpret_cast<PTR>(reinterpret_cast<char *>(this->used_blocks[idx / N_PER_CHUNK]) + (SIZE * (idx % N_PER_CHUNK))); }

	std::pair<IDX, PTR> Allocate() {
		if (this->last_freed != (IDX)(-1)) {
			IDX idx = this->last_freed;
			PTR item = (*this)[idx];
			this->last_freed = *reinterpret_cast<IDX*>(item);
			return std::make_pair(idx, item);
		} else {
			if (current_block == nullptr || next_position == N_PER_CHUNK) {
				NewBlock();
			}
			IDX idx = this->next_position + ((IDX)(this->used_blocks.size() - 1) * N_PER_CHUNK);
			PTR item = reinterpret_cast<PTR>(reinterpret_cast<char *>(current_block) + (SIZE * next_position));
			this->next_position++;
			return std::make_pair(idx, item);
		}
	}

	void Free(IDX idx) {
		this->Free(idx, (*this)[idx]);
	}

	void Free(IDX idx, PTR item) {
		dbg_assert(current_block != nullptr);

		*reinterpret_cast<IDX*>(item) = this->last_freed;
		this->last_freed = idx;
	}
};

#endif /* POD_POOL_HPP */
