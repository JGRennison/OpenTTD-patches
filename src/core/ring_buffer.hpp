/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ring_buffer.hpp Resizing ring buffer implementation. */

#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include "alloc_type.hpp"
#include "bitmath_func.hpp"

#include <iterator>
#include <type_traits>

/**
 * Self-resizing ring-buffer
 *
 * Insertion of an item invalidates existing iterators.
 * Erasing an item which is not at the front or the back invalidates existing iterators.
 */
template <class T>
class ring_buffer
{
	std::unique_ptr<byte, FreeDeleter> data;
	uint32 head = 0;
	uint32 count = 0;
	uint32 mask = (uint32)-1;

	static uint32 round_up_size(uint32 size)
	{
		if (size <= 4) return 4;
		uint8 bit = FindLastBit(size - 1);
		return 1 << (bit + 1);
	}

	class ring_buffer_iterator_base
	{
		friend class ring_buffer;

	protected:
		const ring_buffer *ring = nullptr;
		uint32 pos = 0;

		ring_buffer_iterator_base() {}

		ring_buffer_iterator_base(const ring_buffer *ring, uint32 pos)
				: ring(ring), pos(pos) {}

	public:
		uint32 debug_raw_position() const { return this->pos; }
	};

public:
	template <class V, bool REVERSE>
	class ring_buffer_iterator : public ring_buffer_iterator_base
	{
		friend class ring_buffer;

	public:
		ring_buffer_iterator()
				: ring_buffer_iterator_base() {}

		ring_buffer_iterator(const ring_buffer_iterator_base &other)
				: ring_buffer_iterator_base(other.ring, other.pos) {}

	private:
		ring_buffer_iterator(const ring_buffer *ring, uint32 pos)
				: ring_buffer_iterator_base(ring, pos) {}

		void next()
		{
			if (REVERSE) {
				--this->pos;
			} else {
				++this->pos;
			}
		}

		void prev()
		{
			if (REVERSE) {
				++this->pos;
			} else {
				--this->pos;
			}
		}

		void move(std::ptrdiff_t delta)
		{
			if (REVERSE) {
				this->pos -= (uint32)delta;
			} else {
				this->pos += (uint32)delta;
			}
		}

	public:
		using difference_type = std::ptrdiff_t;
		using value_type = T;
		using reference = V &;
		using const_reference = const V &;
		using pointer = V *;
		using const_pointer = const V *;
		using iterator_category = std::bidirectional_iterator_tag;

		reference operator *()
		{
			return *this->ring->ptr_at_pos(this->pos);
		}

		const_reference operator *() const
		{
			return *this->ring->ptr_at_pos(this->pos);
		}

		pointer operator ->()
		{
			return this->ring->ptr_at_pos(this->pos);
		}

		const_pointer operator ->() const
		{
			return this->ring->ptr_at_pos(this->pos);
		}

		/* Increment operator (postfix) */
		ring_buffer_iterator operator ++(int)
		{
			ring_buffer_iterator tmp = *this;
			this->next();
			return tmp;
		}

		/* Increment operator (prefix) */
		ring_buffer_iterator &operator ++()
		{
			this->next();
			return *this;
		}

		/* Decrement operator (postfix) */
		ring_buffer_iterator operator --(int)
		{
			ring_buffer_iterator tmp = *this;
			this->prev();
			return tmp;
		}

		/* Decrement operator (prefix) */
		ring_buffer_iterator &operator --()
		{
			this->prev();
			return *this;
		}

		bool operator ==(const ring_buffer_iterator_base &other) const
		{
			return (this->ring == other.ring) && (this->pos == other.pos);
		}

		bool operator !=(const ring_buffer_iterator_base &other) const
		{
			return !operator ==(other);
		}

		ring_buffer_iterator operator +(std::ptrdiff_t delta) const
		{
			ring_buffer_iterator tmp = *this;
			tmp += delta;
			return tmp;
		}

		ring_buffer_iterator &operator +=(std::ptrdiff_t delta)
		{
			this->move(delta);
			return *this;
		}

		ring_buffer_iterator operator -(std::ptrdiff_t delta) const
		{
			ring_buffer_iterator tmp = *this;
			tmp -= delta;
			return tmp;
		}

		ring_buffer_iterator &operator -=(std::ptrdiff_t delta)
		{
			this->move(-delta);
			return *this;
		}

		std::ptrdiff_t operator -(const ring_buffer_iterator &other) const
		{
			dbg_assert(this->ring == other.ring);
			if (REVERSE) {
				return (int32)(other.pos - this->pos);
			} else {
				return (int32)(this->pos - other.pos);
			}
		}
	};

	typedef ring_buffer_iterator<T, false> iterator;
	typedef ring_buffer_iterator<const T, false> const_iterator;
	typedef ring_buffer_iterator<T, true> reverse_iterator;
	typedef ring_buffer_iterator<const T, true> const_reverse_iterator;

	ring_buffer() = default;

	template <typename U>
	void construct_from(const U &other)
	{
		uint32 cap = round_up_size((uint32)other.size());
		this->data.reset(MallocT<byte>(cap * sizeof(T)));
		this->mask = cap - 1;
		this->head = 0;
		this->count = (uint32)other.size();
		byte *ptr = this->data.get();
		for (const T &item : other) {
			new (ptr) T(item);
			ptr += sizeof(T);
		}
	}

	ring_buffer(const ring_buffer &other)
	{
		if (!other.empty()) {
			this->construct_from(other);
		}
	}

	ring_buffer(ring_buffer &&other) noexcept
	{
		std::swap(this->data, other.data);
		std::swap(this->head, other.head);
		std::swap(this->count, other.count);
		std::swap(this->mask, other.mask);
	}

	ring_buffer(std::initializer_list<T> init)
	{
		if (init.size() > 0) {
			this->construct_from(init);
		}
	}

	ring_buffer& operator =(const ring_buffer &other)
	{
		if (&other != this) {
			this->clear();
			if (!other.empty()) {
				if (other.size() > this->capacity()) {
					uint32 cap = round_up_size(other.count);
					this->data.reset(MallocT<byte>(cap * sizeof(T)));
					this->mask = cap - 1;
				}
				this->head = 0;
				this->count = other.count;
				byte *ptr = this->data.get();
				for (const T &item : other) {
					new (ptr) T(item);
					ptr += sizeof(T);
				}
			}
		}
		return *this;
	}

	ring_buffer& operator =(ring_buffer &&other) noexcept
	{
		if (&other != this) {
			std::swap(this->data, other.data);
			std::swap(this->head, other.head);
			std::swap(this->count, other.count);
			std::swap(this->mask, other.mask);
		}
		return *this;
	}

	~ring_buffer()
	{
		for (T &item : *this) {
			item.~T();
		}
	}

	void swap(ring_buffer &other) noexcept
	{
		std::swap(this->data, other.data);
		std::swap(this->head, other.head);
		std::swap(this->count, other.count);
		std::swap(this->mask, other.mask);
	}

	bool operator ==(const ring_buffer& other) const
	{
		if (this->count != other.count) return false;
		if (this->empty()) return true;

		auto other_iter = other.begin();
		for (const T &item : *this) {
			if (item != *other_iter) return false;
			++other_iter;
		}
		return true;
	}

	bool operator != (const ring_buffer &other) const
	{
		return !operator ==(other);
	}

	size_t size() const
	{
		return this->count;
	}

	bool empty() const
	{
		return this->count == 0;
	}

	size_t capacity() const
	{
		return this->mask + 1;
	};

	void clear()
	{
		for (const T &item : *this) {
			item.~T();
		}
		this->count = 0;
		this->head = 0;
	}

private:
	void reallocate(uint32 new_cap)
	{
		const uint32 cap = round_up_size(new_cap);
		byte *new_buf = MallocT<byte>(cap * sizeof(T));
		byte *pos = new_buf;
		for (T &item : *this) {
			new (pos) T(std::move(item));
			item.~T();
			pos += sizeof(T);
		}
		this->mask = cap - 1;
		this->head = 0;
		this->data.reset(new_buf);
	}

	void *raw_ptr_at_pos(uint32 idx) const
	{
		return this->data.get() + (sizeof(T) * (idx & this->mask));
	}

	void *raw_ptr_at_offset(uint32 idx) const
	{
		return this->raw_ptr_at_pos(this->head + idx);
	}

	T *ptr_at_pos(uint32 idx) const
	{
		return static_cast<T *>(this->raw_ptr_at_pos(idx));
	}

	T *ptr_at_offset(uint32 idx) const
	{
		return static_cast<T *>(this->raw_ptr_at_offset(idx));
	}

	void *new_back_ptr()
	{
		if (this->count == this->capacity()) this->reallocate(this->count + 1);
		this->count++;
		return this->raw_ptr_at_offset(this->count - 1);
	}

	void *new_front_ptr()
	{
		if (this->count == this->capacity()) this->reallocate(this->count + 1);
		this->count++;
		this->head--;
		return this->raw_ptr_at_offset(0);
	}

public:
	void push_back(const T &item)
	{
		new (this->new_back_ptr()) T(item);
	}

	void push_back(T &&item)
	{
		new (this->new_back_ptr()) T(std::move(item));
	}

	template <typename... Args>
	T &emplace_back(Args&&... args)
	{
		void *ptr = this->new_back_ptr();
		return *(new (ptr) T(std::forward<Args>(args)...));
	}

	void push_front(const T &item)
	{
		new (this->new_front_ptr()) T(item);
	}

	void push_front(T &&item)
	{
		new (this->new_front_ptr()) T(std::move(item));
	}

	template <typename... Args>
	T &emplace_front(Args&&... args)
	{
		void *ptr = this->new_front_ptr();
		return *(new (ptr) T(std::forward<Args>(args)...));
	}

	void pop_back()
	{
		this->count--;
		this->ptr_at_offset(this->count)->~T();
	}

	void pop_front()
	{
		this->ptr_at_offset(0)->~T();
		this->head++;
		this->count--;
	}

	iterator begin()
	{
		return iterator(this, this->head);
	}

	const_iterator begin() const
	{
		return const_iterator(this, this->head);
	}

	const_iterator cbegin() const
	{
		return const_iterator(this, this->head);
	}

	iterator end()
	{
		return iterator(this, this->head + this->count);
	}

	const_iterator end() const
	{
		return const_iterator(this, this->head + this->count);
	}

	const_iterator cend() const
	{
		return const_iterator(this, this->head + this->count);
	}

	reverse_iterator rbegin()
	{
		return reverse_iterator(this, this->head + this->count - 1);
	}

	const_reverse_iterator rbegin() const
	{
		return const_reverse_iterator(this, this->head + this->count - 1);
	}

	const_reverse_iterator crbegin() const
	{
		return const_reverse_iterator(this, this->head + this->count - 1);
	}

	reverse_iterator rend()
	{
		return reverse_iterator(this, this->head - 1);
	}

	const_reverse_iterator rend() const
	{
		return const_reverse_iterator(this, this->head - 1);
	}

	const_reverse_iterator crend() const
	{
		return const_reverse_iterator(this, this->head - 1);
	}

	T &front()
	{
		return *this->ptr_at_offset(0);
	}

	const T &front() const
	{
		return *this->ptr_at_offset(0);
	}

	T &back()
	{
		return *this->ptr_at_offset(this->count - 1);
	}

	const T &back() const
	{
		return *this->ptr_at_offset(this->count - 1);
	}

private:
	uint32 setup_insert(uint32 pos, uint32 num)
	{
		if (this->count + num > (uint32)this->capacity()) {
			/* grow container */
			const uint32 cap = round_up_size(this->count + num);
			byte *new_buf = MallocT<byte>(cap * sizeof(T));
			byte *write_to = new_buf;
			const uint32 end = this->head + this->count;
			for (uint32 idx = this->head; idx != end; idx++) {
				if (idx == pos) {
					/* gap for inserted items */
					write_to += num * sizeof(T);
				}
				T &item = *this->ptr_at_pos(idx);
				new (write_to) T(std::move(item));
				item.~T();
				write_to += sizeof(T);
			}
			uint32 res = pos - this->head;
			this->mask = cap - 1;
			this->head = 0;
			this->count += num;
			this->data.reset(new_buf);
			return res;
		} else if (pos == this->head) {
			/* front */
			this->count += num;
			this->head -= num;
			return this->head;
		} else if (pos == this->head + this->count) {
			/* back */
			this->count += num;
			return pos;
		} else {
			/* middle, move data */
			if (pos - this->head < (this->count / 2)) {
				/* closer to the beginning, shuffle those backwards */
				const uint32 new_head = this->head - num;
				const uint32 insert_start = pos - num;
				for (uint32 idx = new_head; idx != this->head; idx++) {
					/* Move construct to move backwards into uninitialised region */
					new (this->raw_ptr_at_pos(idx)) T(std::move(*(this->ptr_at_pos(idx + num))));
				}
				for (uint32 idx = this->head; idx != insert_start; idx++) {
					/* Move assign to move backwards in initialised region */
					*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx + num));
				}
				for (uint32 idx = insert_start; idx != pos; idx++) {
					/* Destruct to leave space for inserts */
					this->ptr_at_pos(idx)->~T();
				}
				this->head = new_head;
				this->count += num;
				return insert_start;
			} else {
				/* closer to the end, shuffle those forwards */
				const uint32 last_inserted = pos + num - 1;
				const uint32 last = this->head + this->count - 1;
				const uint32 new_last = last + num;
				for (uint32 idx = new_last; idx != last; idx--) {
					/* Move construct to move forwards into uninitialised region */
					new (this->raw_ptr_at_pos(idx)) T(std::move(*(this->ptr_at_pos(idx - num))));
				}
				for (uint32 idx = last; idx != last_inserted; idx--) {
					/* Move assign to move forwards in initialised region */
					*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx - num));
				}
				for (uint32 idx = last_inserted; idx != pos; idx--) {
					/* Destruct to leave space for inserts */
					this->ptr_at_pos(idx)->~T();
				}
				this->count += num;
				return pos;
			}
		}
	}

public:
	template <typename... Args>
	iterator emplace(ring_buffer_iterator_base pos, Args&&... args)
	{
		dbg_assert(pos.ring == this);

		uint32 new_pos = this->setup_insert(pos.pos, 1);
		new (this->raw_ptr_at_pos(new_pos)) T(std::forward<Args>(args)...);
		return iterator(this, new_pos);
	}

	iterator insert(ring_buffer_iterator_base pos, const T& value)
	{
		return this->emplace(pos, value);
	}

	iterator insert(ring_buffer_iterator_base pos, T&& value)
	{
		return this->emplace(pos, std::move(value));
	}

	iterator insert(ring_buffer_iterator_base pos, size_t count, const T& value)
	{
		if (count == 0) return iterator(pos);

		dbg_assert(pos.ring == this);

		const uint32 new_pos_start = this->setup_insert(pos.pos, count);
		uint32 new_pos = new_pos_start;
		for (size_t i = 0; i != count; i++) {
			new (this->raw_ptr_at_pos(new_pos)) T(value);
			++new_pos;
		}
		return iterator(this, new_pos_start);
	}

	template <typename InputIt, typename = std::enable_if_t<std::is_convertible<typename std::iterator_traits<InputIt>::iterator_category, std::input_iterator_tag>::value>>
	iterator insert(ring_buffer_iterator_base pos, InputIt first, InputIt last)
	{
		if (first == last) return iterator(pos);

		dbg_assert(pos.ring == this);

		const uint32 new_pos_start = this->setup_insert(pos.pos, (uint32)std::distance(first, last));
		uint32 new_pos = new_pos_start;
		for (auto iter = first; iter != last; ++iter) {
			new (this->raw_ptr_at_pos(new_pos)) T(*iter);
			++new_pos;
		}
		return iterator(this, new_pos_start);
	}

	iterator insert(ring_buffer_iterator_base pos, std::initializer_list<T> values)
	{
		return this->insert(pos, values.begin(), values.end());
	}

	void reserve(size_t new_cap)
	{
		if (new_cap <= this->capacity()) return;

		this->reallocate((uint32)new_cap);
	}

	void resize(size_t new_size)
	{
		if (new_size < this->size()) {
			for (uint32 i = (uint32)new_size; i != this->count; i++) {
				this->ptr_at_offset(i)->~T();
			}
		} else if (new_size > this->size()) {
			if (new_size > this->capacity()) {
				this->reallocate((uint32)new_size);
			}
			for (uint32 i = this->count; i != (uint32)new_size; i++) {
				new (this->raw_ptr_at_offset(i)) T();
			}
		}
		this->count = (uint32)new_size;
	}

	void shrink_to_fit()
	{
		if (this->empty()) {
			this->clear();
			this->data.reset();
			this->mask = (uint32)-1;
		} else if (round_up_size(this->count) < this->capacity()) {
			this->reallocate(this->count);
		}
	}

	T &operator[](size_t index)
	{
		return *this->ptr_at_offset((uint32)index);
	}

	const T &operator[](size_t index) const
	{
		return *this->ptr_at_offset((uint32)index);
	}
};

#endif /* RING_BUFFER_HPP */
