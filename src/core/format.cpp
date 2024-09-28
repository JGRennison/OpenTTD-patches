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

void format_target::fixed_fmt_buffer::grow(size_t capacity)
{
	if (this->size() == this->capacity()) {
		/* Buffer is full, use the discard area for the overflow */
		this->clear();
		this->set(this->discard, sizeof(this->discard));
	}
}
