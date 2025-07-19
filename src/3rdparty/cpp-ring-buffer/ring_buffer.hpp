// Resizing ring buffer implementation
// https://github.com/JGRennison/cpp-ring-buffer
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Jonathan Rennison <j.g.rennison@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef JGR_RING_BUFFER_HPP
#define JGR_RING_BUFFER_HPP

#include <string.h>
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <iterator>
#include <memory>
#include <type_traits>

namespace jgr {

#if defined(_MSC_VER) && _MSC_VER >= 1929
#	define JGR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#elif defined(__has_cpp_attribute) && __has_cpp_attribute(no_unique_address)
#	define JGR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#	define JGR_NO_UNIQUE_ADDRESS
#endif

/**
 * Self-resizing ring-buffer
 *
 * Insertion of an item invalidates existing iterators.
 * Erasing an item which is not at the front or the back invalidates existing iterators.
 */
template <class T, class Allocator = std::allocator<T>>
class ring_buffer {
	using Storage = T;
	static constexpr size_t MAX_SIZE = 1U << 31;

	Storage *data = nullptr;
	uint32_t head = 0;
	uint32_t count = 0;
	uint32_t mask = (uint32_t)-1;
	JGR_NO_UNIQUE_ADDRESS Allocator allocator;

	static uint8_t find_last_bit(uint32_t value)
	{
		return std::countl_zero<uint32_t>(1) - std::countl_zero<uint32_t>(value);
	}

	static uint32_t round_up_size(size_t size)
	{
		if (size <= 4) return 4;
#ifdef WITH_FULL_ASSERTS
		if (size > MAX_SIZE) throw std::length_error("jgr::ring_buffer: maximum size exceeded");
#endif
		uint8_t bit = find_last_bit((uint32_t)size - 1);
		return 1U << (bit + 1);
	}

	class ring_buffer_iterator_base {
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

	void allocate_storage(uint32_t cap)
	{
		this->data = this->allocator.allocate(cap);
		this->mask = cap - 1;
	}

	void deallocate_storage()
	{
		if (this->data != nullptr) {
			this->allocator.deallocate(this->data, this->mask + 1);
		}
	}

	void replace_storage(Storage *data, uint32_t cap)
	{
		this->deallocate_storage();
		this->data = data;
		this->mask = cap - 1;
	}

	void move_assign_data(ring_buffer &&other) noexcept
	{
		this->data = other.data;
		this->head = other.head;
		this->count = other.count;
		this->mask = other.mask;
		other.data = nullptr;
		other.head = 0;
		other.count = 0;
		other.mask = (uint32_t)-1;
	}

public:
	template <class V, bool REVERSE>
	class ring_buffer_iterator : public ring_buffer_iterator_base {
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
	using allocator_type = Allocator;
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

	template <typename InputIt>
	void construct_from(size_t size, InputIt first, InputIt last)
	{
		uint32_t cap = round_up_size(size);
		this->allocate_storage(cap);
		this->head = 0;
		this->count = (uint32_t)size;
		Storage *ptr = this->data;
		for (auto iter = first; iter != last; ++iter) {
			new (ptr) T(*iter);
			ptr++;
		}
	}

	void copy_data_from(const ring_buffer &other)
	{
		this->head = 0;
		this->count = other.count;
		if constexpr (std::is_trivially_copyable_v<T>) {
			other.memcpy_to(this->data);
		} else {
			Storage *ptr = this->data;
			for (const T &item : other) {
				new (ptr) T(item);
				ptr++;
			}
		}
	}

	void copy_assign_from(const ring_buffer &other)
	{
		if (!other.empty()) {
			if (other.size() > this->capacity()) {
				uint32_t cap = round_up_size(other.size());
				this->deallocate_storage();
				this->allocate_storage(cap);
			}
			this->copy_data_from(other);
		}
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

	ring_buffer() noexcept(noexcept(Allocator())) : allocator(Allocator()) {}

	ring_buffer(const ring_buffer &other) : allocator(std::allocator_traits<Allocator>::select_on_container_copy_construction(other.allocator))
	{
		if (!other.empty()) {
			this->allocate_storage(round_up_size(other.size()));
			this->copy_data_from(other);
		}
	}

	ring_buffer(const ring_buffer &other, const Allocator &alloc) : allocator(alloc)
	{
		if (!other.empty()) {
			this->allocate_storage(round_up_size(other.size()));
			this->copy_data_from(other);
		}
	}

	ring_buffer(ring_buffer &&other) noexcept : allocator(std::move(other.allocator))
	{
		this->move_assign_data(std::move(other));
	}

	ring_buffer(ring_buffer &&other, const Allocator &alloc) : allocator(alloc)
	{
		if (alloc == other.allocator) {
			this->move_assign_data(std::move(other));
		} else if (!other.empty()) {
			this->allocate_storage(round_up_size(other.size()));
			this->copy_data_from(other);
		}
	}

	ring_buffer(std::initializer_list<T> init, const Allocator &alloc = Allocator()) : allocator(alloc)
	{
		if (init.size() > 0) {
			this->construct_from(init.size(), init.begin(), init.end());
		}
	}

	template <typename InputIt, typename = std::enable_if_t<std::is_convertible<typename std::iterator_traits<InputIt>::iterator_category, std::input_iterator_tag>::value>>
	ring_buffer(InputIt first, InputIt last, const Allocator &alloc = Allocator()) : allocator(alloc)
	{
		if (first != last) {
			this->construct_from(std::distance(first, last), first, last);
		}
	}

	ring_buffer& operator =(const ring_buffer &other)
	{
		if (&other != this) {
			this->clear();
			if constexpr (std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value) {
				if (this->allocator != other.allocator) this->replace_storage(nullptr, 0); // Do not re-use existing storage if allocators do not match
				this->allocator = other.allocator;
			}
			this->copy_assign_from(other);
		}
		return *this;
	}

	ring_buffer& operator =(ring_buffer &&other) noexcept(std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value || std::allocator_traits<Allocator>::is_always_equal::value)
	{
		if (&other != this) {
			this->clear();
			if constexpr (std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value) {
				this->allocator = other.allocator;
			} else if (this->allocator != other.allocator) {
				// Do not move existing storage if allocators do not match
				this->copy_assign_from(other);
				return *this;
			}
			this->deallocate_storage();
			this->move_assign_data(std::move(other));
		}
		return *this;
	}

	~ring_buffer()
	{
		for (T &item : *this) {
			item.~T();
		}
		this->deallocate_storage();
	}

	void swap(ring_buffer &other) noexcept
	{
		std::swap(this->data, other.data);
		std::swap(this->head, other.head);
		std::swap(this->count, other.count);
		std::swap(this->mask, other.mask);
		if constexpr (std::allocator_traits<Allocator>::propagate_on_container_swap::value) {
			std::swap(this->allocator, other.allocator);
		}
	}

	friend bool operator ==(const ring_buffer &a, const ring_buffer &b)
	{
		if (a.count != b.count) return false;
		if (a.empty()) return true;

		auto other_iter = b.begin();
		for (const T &item : a) {
			if (item != *other_iter) return false;
			++other_iter;
		}
		return true;
	}

	friend auto operator <=>(const ring_buffer &a, const ring_buffer &b)
	{
		return std::lexicographical_compare_three_way(a.begin(), a.end(), b.begin(), b.end());
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

	allocator_type get_allocator() const noexcept
	{
		return this->allocator;
	}

	constexpr size_t max_size() const
	{
		return MAX_SIZE;
	}

private:
	Storage *memcpy_to(Storage *target, uint32_t start_pos, uint32_t end_pos) const
	{
		if (start_pos == end_pos) return target;

		const Storage *start_ptr = static_cast<Storage *>(this->raw_ptr_at_pos(start_pos));
		const Storage *end_ptr = static_cast<Storage *>(this->raw_ptr_at_pos(end_pos));
		if (end_ptr <= start_ptr) {
			/* Copy in two chunks due to wrap */

			const Storage *buffer_end = this->data + this->capacity();
			memcpy(target, start_ptr, (buffer_end - start_ptr) * sizeof(T));
			target += buffer_end - start_ptr;

			memcpy(target, this->data, (end_ptr - this->data) * sizeof(T));
			target += end_ptr - this->data;
		} else {
			/* Copy in one chunk */
			memcpy(target, start_ptr, (end_ptr - start_ptr) * sizeof(T));
			target += end_ptr - start_ptr;
		}
		return target;
	}

	Storage *memcpy_to(Storage *target) const
	{
		return this->memcpy_to(target, this->head, this->head + this->count);
	}

	void reallocate(size_t new_cap)
	{
		const uint32_t cap = round_up_size(new_cap);
		Storage *new_buf = this->allocator.allocate(cap);
		if constexpr (std::is_trivially_copyable_v<T>) {
			this->memcpy_to(new_buf);
		} else {
			Storage *pos = new_buf;
			for (T &item : *this) {
				new (pos) T(std::move(item));
				item.~T();
				pos++;
			}
		}
		this->replace_storage(new_buf, cap);
		this->head = 0;
	}

	void *raw_ptr_at_pos(uint32_t idx) const
	{
		return this->data + (idx & this->mask);
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
	uint32_t setup_insert(uint32_t pos, size_t num)
	{
		if (this->count + num > this->capacity()) {
			/* grow container */
			const uint32_t cap = round_up_size(this->count + num);
			Storage *new_buf = this->allocator.allocate(cap);
			if constexpr (std::is_trivially_copyable_v<T>) {
				Storage *insert_gap = this->memcpy_to(new_buf, this->head, pos);
				this->memcpy_to(insert_gap + num, pos, this->head + this->count);
			} else {
				Storage *write_to = new_buf;
				const uint32_t end = this->head + this->count;
				for (uint32_t idx = this->head; idx != end; idx++) {
					if (idx == pos) {
						/* gap for inserted items */
						write_to += num;
					}
					T &item = *this->ptr_at_pos(idx);
					new (write_to) T(std::move(item));
					item.~T();
					write_to++;
				}
			}
			uint32_t res = pos - this->head;
			this->replace_storage(new_buf, cap);
			this->head = 0;
			this->count += num;
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

		const uint32_t new_pos_start = this->setup_insert(pos.pos, count);
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

		const uint32_t new_pos_start = this->setup_insert(pos.pos, std::distance(first, last));
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

		this->reallocate(new_cap);
	}

	void resize(size_t new_size)
	{
		if (new_size < this->size()) {
			for (uint32_t i = (uint32_t)new_size; i != this->count; i++) {
				this->ptr_at_offset(i)->~T();
			}
		} else if (new_size > this->size()) {
			if (new_size > this->capacity()) {
				this->reallocate(new_size);
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
			this->replace_storage(nullptr, 0);
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

	T &at(size_t index)
	{
		if (index >= this->size()) throw std::out_of_range("jgr::ring_buffer::at: index out of range");
		return *this->ptr_at_offset((uint32_t)index);
	}

	const T &at(size_t index) const
	{
		if (index >= this->size()) throw std::out_of_range("jgr::ring_buffer::at: index out of range");
		return *this->ptr_at_offset((uint32_t)index);
	}
};

}

#undef JGR_NO_UNIQUE_ADDRESS

#endif /* JGR_RING_BUFFER_HPP */
