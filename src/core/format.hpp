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
 * Not directly instantiable, use format_to_buffer, format_buffer, format_buffer_sized, format_to_fixed or format_to_fixed_z.
 */
struct format_target {
protected:
	enum {
		FL_FIXED = 1,
		FL_OVERFLOW = 2,
	};

	fmt::detail::buffer<char> &target; // This can point to any subtype of fmt::basic_memory_buffer
	uint8_t flags;

	format_target(fmt::detail::buffer<char> &buffer, uint8_t flags) : target(buffer), flags(flags) {}
	~format_target() = default;

protected:
	fmt::detail::buffer<char> &GetTargetFmtBuffer() const { return this->target; }

public:
	format_target(const format_target &other) = delete;
	format_target& operator=(const format_target &other) = delete;

	inline size_t size() const noexcept;
	inline bool empty() const noexcept { return this->size() == 0; }
	void restore_size(size_t);
	inline const char *data() const noexcept;
	inline char *data() noexcept { return const_cast<char *>(const_cast<const format_target *>(this)->data()); }

	char *begin() noexcept { return this->data(); }
	char *end() noexcept { return this->data() + this->size(); }
	const char *begin() const noexcept { return this->data(); }
	const char *end() const noexcept { return this->data() + this->size(); }

	template <typename... T>
	void format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		if (has_overflowed()) return;
		fmt::detail::vformat_to(this->target, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
	}

	void vformat(fmt::string_view fmtstr, fmt::format_args args)
	{
		if (has_overflowed()) return;
		fmt::detail::vformat_to(this->target, fmtstr, args, {});
	}

	void push_back(char c)
	{
		if (has_overflowed()) return;
		this->target.push_back(c);
	}

	void append(const char* begin, const char* end)
	{
		if (has_overflowed()) return;
		this->target.append<char>(begin, end);
	}

	void append(std::string_view str) { this->append(str.data(), str.data() + str.size()); }

	template <typename F>
	void append_ptr_last_func(size_t to_reserve, F func)
	{
		if (has_overflowed()) return;
		this->target.try_reserve(this->target.size() + to_reserve);
		char *buf = this->target.data() + this->target.size();
		const char *last = this->target.data() + this->target.capacity() - 1;
		if (last > buf) {
			char *result = func(buf, last);
			this->target.try_resize(result - this->target.data());
		}
	}

	template <typename F>
	void append_span_func(size_t to_reserve, F func)
	{
		if (has_overflowed()) return;
		this->target.try_reserve(this->target.size() + to_reserve);
		const auto size = this->target.size();
		const auto capacity = this->target.capacity();
		if (size == capacity) return;
		size_t written = func(std::span<char>{this->target.data() + size, capacity - size});
		this->target.try_resize(size + written);
	}

	std::span<char> append_as_span(size_t to_append)
	{
		if (has_overflowed()) return {};
		const auto orig_size = this->target.size();
		this->target.try_resize(orig_size + to_append);
		return std::span<char>(this->target.data() + orig_size, this->target.size() - orig_size);
	}

	bool has_overflowed() const { return (this->flags & FL_OVERFLOW) != 0; }
};

/**
 * format_target subtype which outputs to an existing fmt::basic_memory_buffer/fmt::memory_buffer.
 */
struct format_to_buffer : public format_target {
	template <size_t SIZE, typename Allocator>
	format_to_buffer(fmt::basic_memory_buffer<char, SIZE, Allocator> &buffer) : format_target(buffer, 0) {}

	/** Only for internal use */
	struct format_to_buffer_internal_tag{};
	format_to_buffer(format_to_buffer_internal_tag tag, fmt::detail::buffer<char> &buffer, uint8_t flags) : format_target(buffer, 0) {}

	/** Only for specialised uses */
	fmt::detail::buffer<char> &GetTargetBuffer() const { return this->GetTargetFmtBuffer(); }
};

template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<std::is_base_of<fmt_formattable, T>::value>> {
	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx) {
		return ctx.begin();
	}

	fmt::format_context::iterator format(const T& obj, format_context &ctx) const {
		format_to_buffer output(format_to_buffer::format_to_buffer_internal_tag{}, fmt::detail::get_container(ctx.out()), 0);
		obj.fmt_format_value(output);
		return ctx.out();
	}
};

/**
 * Common functionality for format_buffer and format_buffer_size.
 */
template <size_t SIZE>
struct format_buffer_base : public format_to_buffer {
private:
	fmt::basic_memory_buffer<char, SIZE> buffer;

protected:
	format_buffer_base() : format_to_buffer(buffer) {}

public:
	char *begin() noexcept { return this->buffer.begin(); }
	char *end() noexcept { return this->buffer.end(); }
	const char *begin() const noexcept { return this->buffer.begin(); }
	const char *end() const noexcept { return this->buffer.end(); }
	size_t size() const noexcept { return this->buffer.size(); }
	bool empty() const noexcept { return this->size() == 0; }
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

/**
 * format_to_buffer subtype where the fmt::memory_buffer is built-in.
 *
 * Includes convenience wrappers to access the buffer.
 * Can be used as a fmt argument.
 */
struct format_buffer final : public format_buffer_base<fmt::inline_buffer_size> {
	format_buffer() {}
};

/**
 * format_to_buffer subtype where the fmt::memory_buffer is built-in.
 * The inline buffer size is adjustable as a template parameter.
 *
 * Includes convenience wrappers to access the buffer.
 * Can be used as a fmt argument.
 */
template <size_t SIZE>
struct format_buffer_sized final : public format_buffer_base<SIZE> {
	format_buffer_sized() {}
};

template <typename T>
struct format_by_string_view_cast : public fmt::formatter<std::string_view> {
	using underlying_type = std::string_view;
	using parent = typename fmt::formatter<std::string_view>;

	fmt::format_context::iterator format(const T &t, fmt::format_context &ctx) const
	{
		return parent::format((std::string_view)t, ctx);
	}
};

template <>
struct fmt::formatter<format_buffer, char> : public format_by_string_view_cast<format_buffer> {};

template <size_t SIZE>
struct fmt::formatter<format_buffer_sized<SIZE>, char> : public format_by_string_view_cast<format_buffer_sized<SIZE>> {};

struct format_to_fixed_base : public format_target {
	friend format_target;
private:
	/* Use an inner wrapper struct so that the fmt::detail::buffer<char> methods don't create ambiguous overloads or
	 * otherwise interfere with format_target (even with private inheritance).
	 * Casting from the inner_wrapper to the containing format_to_fixed_base is slightly ugly, but only required within grow(). */
	struct inner_wrapper final : public fmt::detail::buffer<char> {
		char * const buffer_ptr;
		const size_t buffer_size;
		char discard[32];

		void grow(size_t) override;
		void restore_size_impl(size_t size);

		inner_wrapper(char *dst, size_t size) : buffer(dst, 0, size), buffer_ptr(dst), buffer_size(size) {}
	};
	inner_wrapper inner;

protected:
	format_to_fixed_base(char *dst, size_t size, uint flags) : format_target(this->inner, flags), inner(dst, size) {}
	~format_to_fixed_base() = default;

public:
	char *data() noexcept { return this->inner.buffer_ptr; }
	const char *data() const noexcept { return this->inner.buffer_ptr; }

	size_t size() const noexcept
	{
		return (this->flags & FL_OVERFLOW) != 0 ? this->inner.buffer_size : this->inner.size();
	}

	bool empty() const noexcept { return this->size() == 0; }

	char *begin() noexcept { return this->data(); }
	char *end() noexcept { return this->data() + this->size(); }
	const char *begin() const noexcept { return this->data(); }
	const char *end() const noexcept { return this->data() + this->size(); }

	operator std::string_view() const { return std::string_view(this->data(), this->size()); }
};

/**
 * format_target subtype for writing to a fixed-size char buffer.
 *
 * Does not null-terminate.
 */
struct format_to_fixed final : public format_to_fixed_base {
	format_to_fixed(char *dst, size_t size) : format_to_fixed_base(dst, size, FL_FIXED) {}
	format_to_fixed(std::span<char> buf) : format_to_fixed_base(buf.data(), buf.size(), FL_FIXED) {}
};

/**
 * format_target subtype for writing to a fixed-size char buffer (using ptr, last semantics).
 *
 * Null-termination only occurs when the finalise method is called.
 */
struct format_to_fixed_z final : public format_to_fixed_base {
	format_to_fixed_z(char *dst, const char *last) : format_to_fixed_base(dst, last - dst, FL_FIXED) {}

	/**
	 * Add null terminator, and return pointer to end of string/null terminator.
	 */
	char *finalise()
	{
		char *terminator = this->end();
		*terminator = '\0';
		return terminator;
	}

	/**
	 * Convenience wrapper to set up a format_to_fixed_z, call format, then finalise it (adding a null-terminator).
	 * This is broadly equivalent to seprintf.
	 */
	template <typename... T>
	static char *format_to(char *dst, const char *last, fmt::format_string<T...> fmtstr, T&&... args)
	{
		format_to_fixed_z buf(dst, last);
		buf.vformat(fmtstr, fmt::make_format_args(args...));
		return buf.finalise();
	}
};

/**
 * format_target subtype for writing to a built-in fixed-size char buffer.
 *
 * Does not null-terminate.
 */
template<size_t N>
struct format_buffer_fixed final : public format_to_fixed_base {
	char strbuffer[N];

	format_buffer_fixed() : format_to_fixed_base(this->strbuffer, N, FL_FIXED) {}
};

const char *format_target::data() const noexcept
{
	if ((this->flags & FL_FIXED) != 0) {
		return static_cast<const format_to_fixed_base *>(this)->inner.buffer_ptr;
	} else {
		return this->target.data();
	}
}

size_t format_target::size() const noexcept
{
	if ((this->flags & FL_FIXED) != 0) {
		return static_cast<const format_to_fixed_base *>(this)->size();
	} else {
		return this->target.size();
	}
}

template <typename F>
struct format_lambda_wrapper : public fmt_formattable {
	F lm;

	format_lambda_wrapper(F lm) : lm(std::move(lm)) {}

	void fmt_format_value(format_target &output) const
	{
		this->lm(output);
	}
};

/**
 * Wraps a lambda of type: [...](format_target &out, args...) {}
 * as a callable of type [...](args...) which is suitable for use as an argument to fmt::format.
 */
template <typename F>
auto format_lambda(F func)
{
	return [func = std::move(func)]<typename... T>(T&&... args) -> auto {
		return format_lambda_wrapper([&](format_target &output) {
			func(output, std::forward<T>(args)...);
		});
	};
};

#endif /* FORMAT_HPP */
