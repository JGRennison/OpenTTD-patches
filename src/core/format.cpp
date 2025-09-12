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

void format_target::push_back_utf8_impl(char32_t c)
{
	auto [buf, len] = EncodeUtf8(c);
	this->append(buf, buf + len);
}

void format_target_ctrl::restore_size(size_t size)
{
	if ((this->flags & FL_FIXED) != 0) {
		static_cast<format_to_fixed_base *>(this)->restore_size(size);
	} else {
		return this->buffer.try_resize(size);
	}
}

void format_buffer_base::grow(fmt::detail::buffer<char> &buf, size_t capacity)
{
	format_buffer_base &self = from_buf<format_buffer_base>(buf);

	size_t old_capacity = buf.capacity();
	size_t new_capacity = old_capacity + old_capacity / 2;
	if (capacity > new_capacity) {
		new_capacity = capacity;
	}
	char *old_data = buf.data();
	char *new_data = MallocT<char>(new_capacity);
	builtin_assume(buf.size() <= new_capacity);
	MemCpyT(new_data, old_data, buf.size());
	self.buffer.set_state(new_data, new_capacity);

	if (old_data != self.local_storage()) free(old_data);
}

void format_to_fixed_base::grow(fmt::detail::buffer<char> &buf, size_t capacity)
{
	format_to_fixed_base &self = from_buf<format_to_fixed_base>(buf);

	if (self.buffer.size() == self.buffer.capacity()) {
		/* Buffer is full, use the discard area for the overflow */
		self.buffer.clear();
		self.buffer.set_state(self.discard, sizeof(self.discard));

		self.flags |= FL_OVERFLOW;
	}
}

void format_to_fixed_base::restore_size(size_t size)
{
	this->flags &= ~FL_OVERFLOW;
	this->buffer.set_state(this->fixed_ptr, this->fixed_size);
	this->buffer.try_resize(size);
}

void format_detail::FmtResizeForCStr(fmt::detail::buffer<char> &buffer)
{
	buffer.try_reserve(buffer.capacity() + 1);
	assert(buffer.size() < buffer.capacity());
}

void fmt_base_fixed_non_growing::grow(fmt::detail::buffer<char> &buf, size_t capacity)
{
	if (buf.size() == buf.capacity()) {
		/* Buffer is full, can't proceed from here. */
		NOT_REACHED();
	}
}
