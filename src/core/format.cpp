/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file format.cpp String formatting functions and helpers. */

#include "../stdafx.h"
#include "format.hpp"
#include "alloc_func.hpp"
#include "mem_func.hpp"
#include "utf8.hpp"

#include "../safeguards.h"

void format_target::append_utf8_impl(char32_t c)
{
	if (this->has_overflowed()) return;
	auto [buf, len] = EncodeUtf8(c);
	this->append(buf, buf + len);
}

void format_target::restore_size(size_t size)
{
	if ((this->flags & FL_FIXED) != 0) {
		this->flags &= ~FL_OVERFLOW;
		static_cast<format_to_fixed_base *>(this)->inner.restore_size_impl(size);
	} else {
		return this->target.try_resize(size);
	}
}

void format_to_fixed_base::inner_wrapper::grow(fmt::detail::buffer<char> &buf, size_t capacity)
{
	inner_wrapper &self = static_cast<inner_wrapper &>(buf);

	if (self.size() == self.capacity()) {
		/* Buffer is full, use the discard area for the overflow */
		self.clear();
		self.set(self.discard, sizeof(self.discard));

		char *target = reinterpret_cast<char *>(&self);
		target -= cpp_offsetof(format_to_fixed_base, inner);
		format_to_fixed_base *fixed = reinterpret_cast<format_to_fixed_base *>(target);
		fixed->flags |= FL_OVERFLOW;
	}
}

void format_to_fixed_base::inner_wrapper::restore_size_impl(size_t size)
{
	this->set(this->buffer_ptr, this->buffer_size);
	this->try_resize(size);
}

void format_detail::FmtResizeForCStr(fmt::detail::buffer<char> &buffer)
{
	buffer.try_reserve(buffer.capacity() + 1);
	assert(buffer.size() < buffer.capacity());
}

void format_to_fixed_base::growable_back_buffer::grow(fmt::detail::buffer<char> &buf, size_t requested)
{
	growable_back_buffer &self = static_cast<growable_back_buffer &>(buf);

	char *old_data = self.data();
	size_t old_capacity = self.capacity();
	size_t new_capacity = old_capacity + old_capacity / 2;
	if (requested > new_capacity) {
		new_capacity = requested;
	}

	char *new_data = MallocT<char>(new_capacity);
	MemCpyT(new_data, old_data, self.size());
	self.set(new_data, new_capacity);

	if (old_data != self.parent.inner.buffer_ptr + self.parent.inner.size()) {
		free(old_data);
	}
}

format_to_fixed_base::growable_back_buffer::~growable_back_buffer()
{
	if (this->data() == this->parent.inner.buffer_ptr + this->parent.inner.size()) {
		/* Use buffer as is */
		this->parent.inner.try_resize(this->parent.inner.size() + this->size());
	} else {
		this->parent.append(this->data(), this->data() + this->size());
		free(this->data());
	}
}
