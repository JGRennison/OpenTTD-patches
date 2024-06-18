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
	std::unique_ptr<uint8_t, FreeDeleter> data;
	uint32_t head = 0;
	uint32_t count = 0;
	uint32_t mask = (uint32_t)-1;

	static uint32_t round_up_size(uint32_t size)
	{
		if (size <= 4) return 4;
		uint8_t bit = FindLastBit(size - 1);
		return 1 << (bit + 1);
	}

	class ring_buffer_iterator_base
	{
		friend class ring_buffer;

	protected:
		const ring_buffer *ring = nullptr;
		uint32_t pos = 0;

		ring_buffer_iterator_base() {}

		ring_buffer_iterator_base(const ring_buffer *ring, uint32_t pos)
				: ring(ring), pos(pos) {}

	public:
		uint32_t debug_raw_position() const { return this->pos; }
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
		ring_buffer_iterator(const ring_buffer *ring, uint32_t pos)
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
				this->pos -= (uint32_t)delta;
			} else {
				this->pos += (uint32_t)delta;
			}
		}

	public:
		using difference_type = std::ptrdiff_t;
		using value_type = T;
		using reference = V &;
		using pointer = V *;
		using iterator_category = std::bidirectional_iterator_tag;

		reference operator *() const
		{
			return *this->ring->ptr_at_pos(this->pos);
		}

		pointer operator ->() const
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

		friend bool operator==(const ring_buffer_iterator &a, const ring_buffer_iterator &b) noexcept {
			return (a.ring == b.ring) && (a.pos == b.pos);
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
				return (int32_t)(other.pos - this->pos);
			} else {
				return (int32_t)(this->pos - other.pos);
			}
		}
	};

	using difference_type = std::ptrdiff_t;
	using size_type = size_t;
	using value_type = T;
	using reference = T &;
	using const_reference = const T &;
	typedef ring_buffer_iterator<T, false> iterator;
	typedef ring_buffer_iterator<const T, false> const_iterator;
	typedef ring_buffer_iterator<T, true> reverse_iterator;
	typedef ring_buffer_iterator<const T, true> const_reverse_iterator;

private:
	static inline bool iter_equal(const ring_buffer_iterator_base &a, const ring_buffer_iterator_base &b) noexcept
	{
		return (a.ring == b.ring) && (a.pos == b.pos);
	}

public:
	friend bool operator==(const const_iterator &a, const iterator &b) noexcept
	{
		return ring_buffer::iter_equal(a, b);
	}

	friend bool operator==(const const_reverse_iterator &a, const reverse_iterator &b) noexcept
	{
		return ring_buffer::iter_equal(a, b);
	}

	ring_buffer() = default;

	template <typename U>
	void construct_from(const U &other)
	{
		uint32_t cap = round_up_size((uint32_t)other.size());
		this->data.reset(MallocT<uint8_t>(cap * sizeof(T)));
		this->mask = cap - 1;
		this->head = 0;
		this->count = (uint32_t)other.size();
		uint8_t *ptr = this->data.get();
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

	template <typename InputIt, typename = std::enable_if_t<std::is_convertible<typename std::iterator_traits<InputIt>::iterator_category, std::input_iterator_tag>::value>>
	ring_buffer(InputIt first, InputIt last)
	{
		if (first == last) return;

		uint32_t size = (uint32_t)std::distance(first, last);
		uint32_t cap = round_up_size(size);
		this->data.reset(MallocT<uint8_t>(cap * sizeof(T)));
		this->mask = cap - 1;
		this->head = 0;
		this->count = size;
		uint8_t *ptr = this->data.get();
		for (auto iter = first; iter != last; ++iter) {
			new (ptr) T(*iter);
			ptr += sizeof(T);
		}
	}

	ring_buffer& operator =(const ring_buffer &other)
	{
		if (&other != this) {
			this->clear();
			if (!other.empty()) {
				if (other.size() > this->capacity()) {
					uint32_t cap = round_up_size(other.count);
					this->data.reset(MallocT<uint8_t>(cap * sizeof(T)));
					this->mask = cap - 1;
				}
				this->head = 0;
				this->count = other.count;
				if constexpr (std::is_trivially_copyable_v<T>) {
					other.memcpy_to(this->data.get());
				} else {
					uint8_t *ptr = this->data.get();
					for (const T &item : other) {
						new (ptr) T(item);
						ptr += sizeof(T);
					}
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
	uint8_t *memcpy_to(uint8_t *target, uint32_t start_pos, uint32_t end_pos) const
	{
		if (start_pos == end_pos) return target;

		const uint8_t *start_ptr = static_cast<const uint8_t *>(this->raw_ptr_at_pos(start_pos));
		const uint8_t *end_ptr = static_cast<const uint8_t *>(this->raw_ptr_at_pos(end_pos));
		if (end_ptr <= start_ptr) {
			/* Copy in two chunks due to wrap */

			const uint8_t *buffer_end = this->data.get() + (this->capacity() * sizeof(T));
			memcpy(target, start_ptr, buffer_end - start_ptr);
			target += buffer_end - start_ptr;

			memcpy(target, this->data.get(), end_ptr - this->data.get());
			target += end_ptr - this->data.get();
		} else {
			/* Copy in one chunk */
			memcpy(target, start_ptr, end_ptr - start_ptr);
			target += end_ptr - start_ptr;
		}
		return target;
	}

	uint8_t *memcpy_to(uint8_t *target) const
	{
		return this->memcpy_to(target, this->head, this->head + this->count);
	}

	void reallocate(uint32_t new_cap)
	{
		const uint32_t cap = round_up_size(new_cap);
		uint8_t *new_buf = MallocT<uint8_t>(cap * sizeof(T));
		if constexpr (std::is_trivially_copyable_v<T>) {
			this->memcpy_to(new_buf);
		} else {
			uint8_t *pos = new_buf;
			for (T &item : *this) {
				new (pos) T(std::move(item));
				item.~T();
				pos += sizeof(T);
			}
		}
		this->mask = cap - 1;
		this->head = 0;
		this->data.reset(new_buf);
	}

	void *raw_ptr_at_pos(uint32_t idx) const
	{
		return this->data.get() + (sizeof(T) * (idx & this->mask));
	}

	void *raw_ptr_at_offset(uint32_t idx) const
	{
		return this->raw_ptr_at_pos(this->head + idx);
	}

	T *ptr_at_pos(uint32_t idx) const
	{
		return static_cast<T *>(this->raw_ptr_at_pos(idx));
	}

	T *ptr_at_offset(uint32_t idx) const
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
	uint32_t setup_insert(uint32_t pos, uint32_t num)
	{
		if (this->count + num > (uint32_t)this->capacity()) {
			/* grow container */
			const uint32_t cap = round_up_size(this->count + num);
			uint8_t *new_buf = MallocT<uint8_t>(cap * sizeof(T));
			if constexpr (std::is_trivially_copyable_v<T>) {
				uint8_t *insert_gap = this->memcpy_to(new_buf, this->head, pos);
				this->memcpy_to(insert_gap + (num * sizeof(T)), pos, this->head + this->count);
			} else {
				uint8_t *write_to = new_buf;
				const uint32_t end = this->head + this->count;
				for (uint32_t idx = this->head; idx != end; idx++) {
					if (idx == pos) {
						/* gap for inserted items */
						write_to += num * sizeof(T);
					}
					T &item = *this->ptr_at_pos(idx);
					new (write_to) T(std::move(item));
					item.~T();
					write_to += sizeof(T);
				}
			}
			uint32_t res = pos - this->head;
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
				const uint32_t new_head = this->head - num;
				const uint32_t insert_start = pos - num;
				for (uint32_t idx = new_head; idx != this->head; idx++) {
					/* Move construct to move backwards into uninitialised region */
					new (this->raw_ptr_at_pos(idx)) T(std::move(*(this->ptr_at_pos(idx + num))));
				}
				for (uint32_t idx = this->head; idx != insert_start; idx++) {
					/* Move assign to move backwards in initialised region */
					*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx + num));
				}
				for (uint32_t idx = insert_start; idx != pos; idx++) {
					/* Destruct to leave space for inserts */
					this->ptr_at_pos(idx)->~T();
				}
				this->head = new_head;
				this->count += num;
				return insert_start;
			} else {
				/* closer to the end, shuffle those forwards */
				const uint32_t last_inserted = pos + num - 1;
				const uint32_t last = this->head + this->count - 1;
				const uint32_t new_last = last + num;
				for (uint32_t idx = new_last; idx != last; idx--) {
					/* Move construct to move forwards into uninitialised region */
					new (this->raw_ptr_at_pos(idx)) T(std::move(*(this->ptr_at_pos(idx - num))));
				}
				for (uint32_t idx = last; idx != last_inserted; idx--) {
					/* Move assign to move forwards in initialised region */
					*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx - num));
				}
				for (uint32_t idx = last_inserted; idx != pos; idx--) {
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

		uint32_t new_pos = this->setup_insert(pos.pos, 1);
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

		const uint32_t new_pos_start = this->setup_insert(pos.pos, (uint32_t)count);
		uint32_t new_pos = new_pos_start;
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

		const uint32_t new_pos_start = this->setup_insert(pos.pos, (uint32_t)std::distance(first, last));
		uint32_t new_pos = new_pos_start;
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

private:
	uint32_t do_erase(uint32_t pos, uint32_t num)
	{
		if (pos == this->head) {
			/* erase from beginning */
			for (uint32_t i = 0; i < num; i++) {
				this->ptr_at_pos(pos + i)->~T();
			}
			this->head += num;
			this->count -= num;
			return this->head;
		} else if (pos + num == this->head + this->count) {
			/* erase from end */
			for (uint32_t i = 0; i < num; i++) {
				this->ptr_at_pos(pos + i)->~T();
			}
			this->count -= num;
			return pos;
		} else if (pos - this->head < this->head + this->count - (pos + num)) {
			/* closer to the beginning, shuffle beginning forwards to fill gap */
			const uint32_t new_head = this->head + num;
			const uint32_t erase_end = pos + num;
			for (uint32_t idx = erase_end - 1; idx != new_head - 1; idx--) {
				*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx - num));
			}
			for (uint32_t idx = new_head - 1; idx != this->head - 1; idx--) {
				this->ptr_at_pos(idx)->~T();
			}
			this->head = new_head;
			this->count -= num;
			return pos + num;
		} else {
			/* closer to the end, shuffle end backwards to fill gap */
			const uint32_t current_end = this->head + this->count;
			const uint32_t new_end = current_end - num;
			for (uint32_t idx = pos; idx != new_end; idx++) {
				*this->ptr_at_pos(idx) = std::move(*this->ptr_at_pos(idx + num));
			}
			for (uint32_t idx = new_end; idx != current_end; idx++) {
				this->ptr_at_pos(idx)->~T();
			}
			this->count -= num;
			return pos;
		}
	}

public:
	iterator erase(ring_buffer_iterator_base pos)
	{
		dbg_assert(pos.ring == this);

		return iterator(this, this->do_erase(pos.pos, 1));
	}

	iterator erase(ring_buffer_iterator_base first, ring_buffer_iterator_base last)
	{
		if (first.ring == last.ring && first.pos == last.pos) return last;

		dbg_assert(first.ring == this && last.ring == this);

		return iterator(this, this->do_erase(first.pos, last.pos - first.pos));
	}

	void reserve(size_t new_cap)
	{
		if (new_cap <= this->capacity()) return;

		this->reallocate((uint32_t)new_cap);
	}

	void resize(size_t new_size)
	{
		if (new_size < this->size()) {
			for (uint32_t i = (uint32_t)new_size; i != this->count; i++) {
				this->ptr_at_offset(i)->~T();
			}
		} else if (new_size > this->size()) {
			if (new_size > this->capacity()) {
				this->reallocate((uint32_t)new_size);
			}
			for (uint32_t i = this->count; i != (uint32_t)new_size; i++) {
				new (this->raw_ptr_at_offset(i)) T();
			}
		}
		this->count = (uint32_t)new_size;
	}

	void shrink_to_fit()
	{
		if (this->empty()) {
			this->clear();
			this->data.reset();
			this->mask = (uint32_t)-1;
		} else if (round_up_size(this->count) < this->capacity()) {
			this->reallocate(this->count);
		}
	}

	T &operator[](size_t index)
	{
		return *this->ptr_at_offset((uint32_t)index);
	}

	const T &operator[](size_t index) const
	{
		return *this->ptr_at_offset((uint32_t)index);
	}
};

#endif /* RING_BUFFER_HPP */
