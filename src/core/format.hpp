/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file format.hpp String formatting functions and helpers. */

#ifndef FORMAT_HPP
#define FORMAT_HPP

#if defined(__GNUC__) && (__GNUC__ >= 12)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif /* __GNUC__ */

#include "../3rdparty/fmt/format.h"

#if defined(__GNUC__) && (__GNUC__ >= 12)
#pragma GCC diagnostic pop
#endif /* __GNUC__ */

#include "strong_typedef_type.hpp"

#include <type_traits>

template <typename E, typename Char>
struct fmt::formatter<E, Char, std::enable_if_t<std::is_enum<E>::value>> : fmt::formatter<typename std::underlying_type<E>::type> {
	using underlying_type = typename std::underlying_type<E>::type;
	using parent = typename fmt::formatter<underlying_type>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const E &e, format_context &ctx) const
	{
		return parent::format(underlying_type(e), ctx);
	}
};

template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<std::is_base_of<StrongTypedefBase, T>::value>> : fmt::formatter<typename T::BaseType> {
	using underlying_type = typename T::BaseType;
	using parent = typename fmt::formatter<underlying_type>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const T &t, format_context &ctx) const
	{
		return parent::format(t.base(), ctx);
	}
};

struct format_target {
protected:
	fmt::detail::buffer<char> &buffer; // This can point to any subtype of fmt::basic_memory_buffer

	format_target(fmt::detail::buffer<char> &buffer) : buffer(buffer) {}

	struct fixed_fmt_buffer final : public fmt::detail::buffer<char> {
		char discard[32];

		fixed_fmt_buffer(char *dst, size_t size) : buffer(dst, 0, size) {}
		void grow(size_t) override;
		bool has_overflowed() const { return this->data() == this->discard; }
	};

public:
	format_target(const format_target &other) = delete;
	format_target& operator=(const format_target &other) = delete;

	template <typename... T>
	void format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		fmt::detail::vformat_to(this->buffer, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
	}

	void push_back(char c)
	{
		this->buffer.push_back(c);
	}

	template <typename U>
	void append(const U* begin, const U* end)
	{
		this->buffer.append<U>(begin, end);
	}

	void append(std::string_view str) { this->append(str.begin(), str.end()); }
};

struct format_to_buffer : public format_target {
	template <size_t SIZE, typename Allocator>
	format_to_buffer(fmt::basic_memory_buffer<char, SIZE, Allocator> &buffer) : format_target(buffer) {}
};

struct format_buffer : public format_to_buffer {
	fmt::memory_buffer buffer;

	format_buffer() : format_to_buffer(buffer) {}

	char *begin() noexcept { return this->buffer.begin(); }
	char *end() noexcept { return this->buffer.end(); }
	const char *begin() const noexcept { return this->buffer.begin(); }
	const char *end() const noexcept { return this->buffer.end(); }
	size_t size() const noexcept { return this->buffer.size(); }
	size_t capacity() const noexcept { return this->buffer.capacity(); }
	char *data() noexcept { return this->buffer.data(); }
	const char *data() const noexcept { return this->buffer.data(); }

	void clear() { this->buffer.clear(); }

	std::string to_string() const { return std::string(this->buffer.data(), this->buffer.size()); }
	operator std::string_view() const { return std::string_view(this->buffer.data(), this->buffer.size()); }
};

template <>
struct fmt::formatter<format_buffer, char> : fmt::formatter<std::string_view> {
	using underlying_type = std::string_view;
	using parent = typename fmt::formatter<std::string_view>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const format_buffer &t, format_context &ctx) const
	{
		return parent::format((std::string_view)t, ctx);
	}
};

struct format_to_fixed_base : public format_target {
private:
	fixed_fmt_buffer target_buffer;
	const size_t buffer_size;

protected:
	format_to_fixed_base(char *dst, size_t size) : format_target(this->target_buffer), target_buffer(dst, size), buffer_size(size) {}

public:
	size_t written() const
	{
		return this->target_buffer.has_overflowed() ? this->buffer_size : this->target_buffer.size();
	}
};

struct format_to_fixed final : public format_to_fixed_base {
	format_to_fixed(char *dst, size_t size) : format_to_fixed_base(dst, size) {}
};

struct format_to_fixed_z final : public format_to_fixed_base {
	char *initial_dst;

	format_to_fixed_z(char *dst, const char *last) : format_to_fixed_base(dst, last - dst), initial_dst(dst) {}

	/* Add null-terminator */
	char *finalise()
	{
		size_t written = this->written();
		this->initial_dst[written] = '\0';
		return this->initial_dst + written;
	}
};

#endif /* FORMAT_HPP */
