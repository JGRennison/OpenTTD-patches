/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file format.cpp String formatting functions and helpers. */

#include "../stdafx.h"
#include "format.hpp"

#include "../safeguards.h"

void format_target::restore_size(size_t size)
{
	if ((this->flags & FL_FIXED) != 0) {
		this->flags &= ~FL_OVERFLOW;
		static_cast<format_to_fixed_base *>(this)->inner.restore_size_impl(size);
	} else {
		return this->target.try_resize(size);
	}
}

void format_to_fixed_base::inner_wrapper::grow(size_t capacity)
{
	if (this->size() == this->capacity()) {
		/* Buffer is full, use the discard area for the overflow */
		this->clear();
		this->set(this->discard, sizeof(this->discard));

		char *target = reinterpret_cast<char *>(this);
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
