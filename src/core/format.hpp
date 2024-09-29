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

/**
 * Base fmt format target class. Users should take by reference.
 * Not directly instatiable, use format_to_buffer, format_buffer, format_to_fixed or format_to_fixed_z.
 */
struct format_target {
protected:
	enum {
		FL_FIXED = 1,
		FL_FIXED_Z = 2,
		FL_OVERFLOW = 4,
	};

	fmt::detail::buffer<char> &target; // This can point to any subtype of fmt::basic_memory_buffer
	uint8_t flags;

	format_target(fmt::detail::buffer<char> &buffer, uint8_t flags) : target(buffer), flags(flags) {}
	~format_target() = default;

public:
	format_target(const format_target &other) = delete;
	format_target& operator=(const format_target &other) = delete;

	size_t get_position() const;
	void restore_position(size_t);
	inline const char *data() const;
	inline char *data() { return const_cast<char *>(const_cast<const format_target *>(this)->data()); }

	template <typename... T>
	void format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		if (has_overflowed()) return;
		fmt::detail::vformat_to(this->target, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
	}

	void push_back(char c)
	{
		if (has_overflowed()) return;
		this->target.push_back(c);
	}

	template <typename U>
	void append(const U* begin, const U* end)
	{
		if (has_overflowed()) return;
		this->target.append<U>(begin, end);
	}

	void append(std::string_view str) { this->append(str.begin(), str.end()); }

	template <typename F>
	void append_ptr_last_func(size_t to_reserve, F func)
	{
		this->target.try_reserve(this->target.size() + to_reserve);
		char *buf = this->target.data() + this->target.size();
		const char *last = this->target.data() + this->target.capacity() - 1;
		if (last > buf) {
			char *result = func(buf, last);
			this->target.try_resize(result - this->target.data());
		}
	}

	bool has_overflowed() const { return (this->flags & FL_OVERFLOW) != 0; }
	bool is_fixed_z() const { return (this->flags & FL_FIXED_Z) != 0; }
};

/**
 * format_target subtype which outputs to an existing fmt::basic_memory_buffer/fmt::memory_buffer.
 */
struct format_to_buffer : public format_target {
	template <size_t SIZE, typename Allocator>
	format_to_buffer(fmt::basic_memory_buffer<char, SIZE, Allocator> &buffer) : format_target(buffer, 0) {}
};

/**
 * format_to_buffer subtype where the fmt::memory_buffer is built-in.
 *
 * Includes convenience wrappers to access the buffer.
 * Can be used as a fmt argument.
 */
struct format_buffer final : public format_to_buffer {
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

	/* Return a null terminated c string, this may cause the buffer to re-allocated to make room for the null terminator */
	const char *c_str()
	{
		if (this->size() == this->capacity()) this->buffer.try_reserve(this->capacity() + 1);
		*(this->end()) = '\0';
		return this->data();
	}
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

/*
 * The destructors of buffer and format_target are both protected,
 * so not having a virtual destructor is safe.
 * gcc (not clang) still complains though.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif /* __GNUC__ */

struct format_to_fixed_base : private fmt::detail::buffer<char>, public format_target {
	friend format_target;
private:
	char * const buffer_ptr;
	const size_t buffer_size;
	char discard[32];

	fmt::detail::buffer<char> &base_buffer() { return *static_cast<fmt::detail::buffer<char> *>(this); }
	const fmt::detail::buffer<char> &base_buffer() const { return *static_cast<const fmt::detail::buffer<char> *>(this); }

	void grow(size_t) override;
	void restore_position_impl(size_t size);

protected:
	format_to_fixed_base(char *dst, size_t size, uint flags) : buffer(dst, 0, size), format_target(this->base_buffer(), flags), buffer_ptr(dst), buffer_size(size) {}
	~format_to_fixed_base() = default;

public:
	size_t written() const
	{
		return (this->flags & FL_OVERFLOW) != 0 ? this->buffer_size : this->base_buffer().size();
	}
};

/**
 * format_target subtype for writing to a fixed-size char buffer.
 *
 * Does not null-terminate.
 */
struct format_to_fixed final : public format_to_fixed_base {
	format_to_fixed(char *dst, size_t size) : format_to_fixed_base(dst, size, FL_FIXED) {}
};

/**
 * format_target subtype for writing to a fixed-size char buffer (using ptr, last semantics).
 *
 * Null-termination only occurs when the finalise method is called.
 */
struct format_to_fixed_z final : public format_to_fixed_base {
	char *initial_dst;

	format_to_fixed_z(char *dst, const char *last) : format_to_fixed_base(dst, last - dst, FL_FIXED | FL_FIXED_Z), initial_dst(dst) {}

	/**
	 * Add null terminator, and return pointer to end of string/null terminator.
	 */
	char *finalise()
	{
		size_t written = this->written();
		this->initial_dst[written] = '\0';
		return this->initial_dst + written;
	}

	/**
	 * If src is an instance of format_to_fixed_z, cast to format_to_fixed_z *, otherwise return nullptr.
	 */
	static inline format_to_fixed_z *from_format_target(format_target &src)
	{
		return src.is_fixed_z() ? static_cast<format_to_fixed_z *>(&src) : nullptr;
	}
};

const char *format_target::data() const
{
	if ((this->flags & FL_FIXED) != 0) {
		return static_cast<const format_to_fixed_base *>(this)->buffer_ptr;
	} else {
		return this->target.data();
	}
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif /* __GNUC__ */

#endif /* FORMAT_HPP */
