/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tinystring_type.hpp Functions related to the allocation of memory */

#ifndef TINYSTRING_TYPE_HPP
#define TINYSTRING_TYPE_HPP

#include "../string_func.h"
#include <cstddef>
#include <cstring>
#include <string>

/**
 * An SLE_STR compatible tiny c-string wrapper class.
 *
 * This is intended for the cases where the string is almost always empty,
 * and the space overhead of std::string is undesirable.
 */
class TinyString {
	char *storage = nullptr;

public:
	inline TinyString()
	{
		static_assert(offsetof(TinyString, storage) == 0, "offsetof(TinyString, storage) must be 0");
	}

	inline TinyString(const TinyString &other)
	{
		if (other.storage != nullptr) this->storage = stredup(other.storage);
	}

	inline TinyString(TinyString &&other) noexcept
	{
		this->storage = other.storage;
		other.storage = nullptr;
	}

	inline TinyString(const std::string &other) noexcept
	{
		if (!other.empty()) this->storage = stredup(other.c_str());
	}

	inline TinyString(const char *str) noexcept
	{
		if (str != nullptr && *str != 0) this->storage = stredup(str);
	}

	inline void clear()
	{
		if (this->storage != nullptr) {
			free(this->storage);
			this->storage = nullptr;
		}
	}

	inline ~TinyString()
	{
		if (this->storage != nullptr) free(this->storage);
	}

	inline TinyString &operator=(const TinyString &other)
	{
		this->clear();
		if (other.storage != nullptr) this->storage = stredup(other.storage);
		return *this;
	}

	inline TinyString &operator=(TinyString &&other) noexcept
	{
		this->clear();
		if (other.storage != nullptr) {
			this->storage = other.storage;
			other.storage = nullptr;
		}
		return *this;
	}

	inline TinyString &operator=(const std::string &other)
	{
		this->clear();
		if (!other.empty()) this->storage = stredup(other.c_str());
		return *this;
	}

	inline TinyString &operator=(const char *str)
	{
		this->clear();
		if (str != nullptr && *str != 0) this->storage = stredup(str);
		return *this;
	}

	inline bool operator==(const char *str) const
	{
		if (str == nullptr) return this->empty();
		if (this->storage == nullptr) return *str == 0;
		return strcmp(this->storage, str) == 0;
	}

	inline bool empty() const { return this->storage == nullptr || *this->storage == 0; }
	inline const char *c_str() const { return this->storage; }
	inline const char *data() const { return this->storage; }
};


#endif /* TINYSTRING_TYPE_HPP */
