/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strings_builder.h Types and functions related to struct StringBuilder. */

#ifndef STRINGS_BUILDER_H
#define STRINGS_BUILDER_H

#include "string_func.h"
#include "core/format.hpp"

/**
 * Equivalent to the std::back_insert_iterator in function, with some
 * convenience helpers for string concatenation.
 */
class StringBuilder {
	fmt::detail::buffer<char> *string;

public:
	/* Required type for this to be an output_iterator; mimics std::back_insert_iterator. */
	using value_type = void;
	using difference_type = void;
	using iterator_category = std::output_iterator_tag;
	using pointer = void;
	using reference = void;

	/**
	 * Create the builder of an external fmt::basic_memory_buffer/fmt::memory_buffer.
	 * @param buffer The buffer to write to.
	 */
	template <size_t SIZE, typename Allocator>
	StringBuilder(fmt::basic_memory_buffer<char, SIZE, Allocator> &buffer) : string(&buffer) {}

	/**
	 * Create the builder of an external format_to_buffer or subtype.
	 * @param buffer The buffer to write to.
	 */
	StringBuilder(format_to_buffer &buffer) : string(&buffer.GetTargetBuffer()) {}

	/* Required operators for this to be an output_iterator; mimics std::back_insert_iterator, which has no-ops. */
	StringBuilder &operator++() { return *this; }
	StringBuilder operator++(int) { return *this; }
	StringBuilder &operator*() { return *this; }

	/**
	 * Operator to add a character to the end of the buffer. Like the back
	 * insert iterators this also increases the position of the end of the
	 * buffer.
	 * @param value The character to add.
	 * @return Reference to this inserter.
	 */
	StringBuilder &operator=(const char value)
	{
		return this->operator+=(value);
	}

	/**
	 * Operator to add a character to the end of the buffer.
	 * @param value The character to add.
	 * @return Reference to this inserter.
	 */
	StringBuilder &operator+=(const char value)
	{
		this->string->push_back(value);
		return *this;
	}

	/**
	 * Operator to append the given string to the output buffer.
	 * @param str The string to add.
	 * @return Reference to this inserter.
	 */
	StringBuilder &operator+=(std::string_view str)
	{
		this->string->append(str.data(), str.data() + str.size());
		return *this;
	}

	/**
	 * Encode the given Utf8 character into the output buffer.
	 * @param c The character to encode.
	 */
	void Utf8Encode(char32_t c)
	{
		if (c < 0x80) {
			this->string->push_back((char)c);
		} else {
			const size_t pos = this->string->size();
			const int8_t count = Utf8CharLen(c);
			this->string->try_resize(pos + count);
			::Utf8Encode(this->string->data() + pos, c);
		}
	}

	template <typename... T>
	void Format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		fmt::detail::vformat_to(*this->string, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
	}

	/**
	 * Remove the given amount of characters from the back of the string.
	 * @param amount The amount of characters to remove.
	 */
	void RemoveElementsFromBack(size_t amount)
	{
		if (amount >= this->string->size()) {
			this->string->clear();
		} else {
			this->string->try_resize(this->string->size() - amount);
		}
	}

	/**
	 * Get the current index in the string.
	 * @return The index.
	 */
	size_t CurrentIndex()
	{
		return this->string->size();
	}

	/**
	 * Get the reference to the character at the given index.
	 * @return The reference to the character.
	 */
	char &operator[](size_t index)
	{
		return this->string->data()[index];
	}
};

#endif /* STRINGS_BUILDER_H */
