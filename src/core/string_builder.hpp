/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file string_builder.hpp Types and functions related to struct StringBuilder. */

#ifndef STRING_BUILDER_HPP
#define STRING_BUILDER_HPP

#include "format.hpp"
#include <array>
#include <charconv>

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

	using size_type = std::string_view::size_type;

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

	/**
	 * Only for internal use by AppendStringWithArgsInPlaceFixed.
	 */
	StringBuilder(format_to_fixed_base::growable_back_buffer &buffer) : string(&buffer) {}

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

	void PutBuffer(std::span<const char> str) { this->string->append(str.data(), str.data() + str.size()); }

	/**
	 * Append string.
	 */
	void Put(std::string_view str) { this->string->append(str.data(), str.data() + str.size()); }

	/**
	 * Append binary uint8.
	 */
	void PutUint8(uint8_t value) { this->string->push_back((char)value); }

	/**
	 * Append binary int8.
	 */
	void PutSint8(int8_t value) { this->string->push_back((char)value); }

	void PutUint16LE(uint16_t value);
	void PutSint16LE(int16_t value);
	void PutUint32LE(uint32_t value);
	void PutSint32LE(int32_t value);
	void PutUint64LE(uint64_t value);
	void PutSint64LE(int64_t value);

	inline void PutChar(char c) { this->string->push_back((char)c); }

private:
	void PutUtf8Impl(char32_t c);

public:
	/**
	 * Append UTF.8 char.
	 */
	inline void PutUtf8(char32_t c)
	{
		if (c < 0x80) {
			this->PutChar((char)c);
		} else {
			this->PutUtf8Impl(c);
		}
	}

	/**
	 * Append integer 'value' in given number 'base'.
	 */
	template <class T>
	void PutIntegerBase(T value, int base)
	{
		std::array<char, 32> buf;
		auto result = std::to_chars(buf.data(), buf.data() + buf.size(), value, base);
		if (result.ec != std::errc{}) return;
		size_type len = result.ptr - buf.data();
		this->PutBuffer({buf.data(), len});
	}

	/**
	 * Encode the given Utf8 character into the output buffer.
	 * @param c The character to encode.
	 */
	inline void Utf8Encode(char32_t c)
	{
		this->PutUtf8(c);
	}

	template <typename... T>
	void Format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		fmt::detail::vformat_to(*this->string, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
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

#endif /* STRING_BUILDER_HPP */
