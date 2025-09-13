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

namespace format_detail {
	template<typename T>
	concept FmtAsBase = T::fmt_as_base || false;

	template<typename T>
	concept FmtAsBaseHex = T::fmt_as_base_hex || false;

	template<typename T>
	concept FmtAsTileIndex = T::fmt_as_tile_index || false;
};

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
struct fmt::formatter<T, Char, std::enable_if_t<format_detail::FmtAsBase<T>>> : fmt::formatter<typename T::BaseType> {
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

extern fmt::format_context::iterator FmtTileIndexValueIntl(fmt::format_context &ctx, uint32_t value);

template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<format_detail::FmtAsTileIndex<T>>> : fmt::formatter<uint32_t> {
	bool use_base_fmt{};

	using underlying_type = uint32_t;
	using parent = typename fmt::formatter<underlying_type>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		auto it = ctx.begin();
		if (it == ctx.end() || *it == '}') {
			return ctx.begin();
		}
		this->use_base_fmt = true;
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const T &t, format_context &ctx) const
	{
		if (this->use_base_fmt) {
			return parent::format(t.base(), ctx);
		}
		return FmtTileIndexValueIntl(ctx, t.base());
	}
};

template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<format_detail::FmtAsBaseHex<T>>> : fmt::formatter<typename T::BaseType> {
	bool use_base_fmt{};

	using underlying_type = typename T::BaseType;
	using parent = typename fmt::formatter<underlying_type>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		auto it = ctx.begin();
		if (it == ctx.end() || *it == '}') {
			return ctx.begin();
		}
		this->use_base_fmt = true;
		return parent::parse(ctx);
	}

	fmt::format_context::iterator format(const T &t, format_context &ctx) const
	{
		if (this->use_base_fmt) {
			return parent::format(t.base(), ctx);
		} else {
			return fmt::format_to(ctx.out(), "0x{:X}", t.base());
		}
	}
};

template <typename T>
class trivial_appender {
protected:
	T *container;

public:
	constexpr trivial_appender(T &buf) : container(&buf) {}

	constexpr trivial_appender &operator=(char c)
	{
		this->container->push_back(c);
		return *this;
	}

	constexpr trivial_appender &operator*() { return *this; }
	constexpr trivial_appender &operator++() { return *this; }
	constexpr trivial_appender operator++(int) { return *this; }
};

/**
 * Base fmt format target class. Users should take by reference.
 * Not directly instantiable, use format_to_buffer, format_buffer, format_buffer_sized, format_to_fixed or format_to_fixed_z.
 */
struct format_target {
protected:
	using grow_handler = void (*)(fmt::detail::buffer<char> &buf, size_t capacity);

	struct base_buffer : public fmt::detail::buffer<char> {
		base_buffer(grow_handler grow, char *buf, size_t size = 0, size_t capacity = 0) : buffer(grow, buf, size, capacity) {}

		void set_state(char *buf, size_t capacity)
		{
			this->set(buf, capacity);
		}
	};
	base_buffer buffer; // Must be only member

	format_target(grow_handler grow, char *buf, size_t size = 0, size_t capacity = 0) : buffer(grow, buf, size, capacity) {}
	~format_target() = default;

	template <typename T>
	static T &from_buf(fmt::detail::buffer<char> &buf)
	{
		format_target *self = reinterpret_cast<format_target *>(&buf);
		return *static_cast<T *>(self);
	}

	void push_back_utf8_impl(char32_t c);

public:
	format_target(const format_target &other) = delete;
	format_target& operator=(const format_target &other) = delete;

	template <typename... T>
	void format(fmt::format_string<T...> fmtstr, T&&... args)
	{
		fmt::detail::vformat_to(this->buffer, fmt::string_view(fmtstr), fmt::make_format_args(args...), {});
	}

	void vformat(fmt::string_view fmtstr, fmt::format_args args)
	{
		fmt::detail::vformat_to(this->buffer, fmtstr, args, {});
	}

	void push_back(char c)
	{
		this->buffer.push_back(c);
	}

	void append(const char* begin, const char* end)
	{
		this->buffer.append<char>(begin, end);
	}

	void append(std::string_view str) { this->append(str.data(), str.data() + str.size()); }

	template <typename F>
	void append_ptr_last_func(size_t to_reserve, F func)
	{
		this->buffer.try_reserve(this->buffer.size() + to_reserve);
		char *buf = this->buffer.data() + this->buffer.size();
		const char *last = this->buffer.data() + this->buffer.capacity() - 1;
		if (last > buf) {
			char *result = func(buf, last);
			this->buffer.try_resize(result - this->buffer.data());
		}
	}

	template <typename F>
	void append_span_func(size_t to_reserve, F func)
	{
		this->buffer.try_reserve(this->buffer.size() + to_reserve);
		const auto size = this->buffer.size();
		const auto capacity = this->buffer.capacity();
		if (size == capacity) return;
		size_t written = func(std::span<char>{this->buffer.data() + size, capacity - size});
		this->buffer.try_resize(size + written);
	}

	std::span<char> append_as_span(size_t to_append)
	{
		const auto orig_size = this->buffer.size();
		this->buffer.try_resize(orig_size + to_append);
		return std::span<char>(this->buffer.data() + orig_size, this->buffer.size() - orig_size);
	}

	void push_back_utf8(char32_t c)
	{
		if (c < 0x80) {
			this->push_back((char)c);
		} else {
			this->push_back_utf8_impl(c);
		}
	}

	trivial_appender<format_target> back_inserter() { return trivial_appender<format_target>(*this); }

	/** Only for specialised uses */
	fmt::detail::buffer<char> &GetFmtBuffer() { return this->buffer; }
};
static_assert(sizeof(format_target) == sizeof(fmt::detail::buffer<char>));

/**
 * format_target subtype with support for read, get size, restore, etc.
 * Mainly for specialised uses.
 */
struct format_target_ctrl : public format_target {
protected:
	enum {
		FL_FIXED = 1,
		FL_OVERFLOW = 2,
	};
	uint8_t flags;

	format_target_ctrl(uint8_t flags, grow_handler grow, char *buf, size_t size = 0, size_t capacity = 0) : format_target(grow, buf, size, capacity), flags(flags) {}

public:
	inline size_t size() const noexcept;
	inline bool empty() const noexcept { return this->size() == 0; }
	void restore_size(size_t);
	inline const char *data() const noexcept;
	inline char *data() noexcept { return const_cast<char *>(const_cast<const format_target_ctrl *>(this)->data()); }

	char *begin() noexcept { return this->data(); }
	char *end() noexcept { return this->data() + this->size(); }
	const char *begin() const noexcept { return this->data(); }
	const char *end() const noexcept { return this->data() + this->size(); }

	bool has_overflowed() const noexcept { return this->flags & FL_OVERFLOW; }
};

namespace format_detail {
	template<typename T>
	concept FmtUsingFormatValueMethod = requires(const T &a) {
		{ a.fmt_format_value(std::declval<format_target &>()) };
	};
};

template <typename T, typename Char>
struct fmt::formatter<T, Char, std::enable_if_t<format_detail::FmtUsingFormatValueMethod<T>>> {
	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx) {
		return ctx.begin();
	}

	fmt::format_context::iterator format(const T& obj, format_context &ctx) const {
		fmt::detail::buffer<char> &buf = fmt::detail::get_container(ctx.out());
		obj.fmt_format_value(reinterpret_cast<format_target &>(buf));
		return ctx.out();
	}
};

namespace format_detail {
	void FmtResizeForCStr(fmt::detail::buffer<char> &buffer);
};

/**
 * Common functionality for format_buffer and format_buffer_size.
 */
struct format_buffer_base : public format_target_ctrl {
private:
	static void grow(fmt::detail::buffer<char> &, size_t);

protected:
	format_buffer_base(size_t storage_size) : format_target_ctrl(0, grow, this->local_storage(), 0, storage_size) {}

	~format_buffer_base()
	{
		this->deallocate();
	}

	char *local_storage()
	{
		return reinterpret_cast<char *>(this) + sizeof(format_buffer_base);
	}

	void deallocate()
	{
		char *data = this->buffer.data();
		if (data != this->local_storage()) free(data);
	}

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
	char &back() noexcept { return this->buffer[this->size() - 1]; }
	const char &back() const noexcept { return this->buffer[this->size() - 1]; }

	void clear() { this->buffer.clear(); }

	std::string to_string() const { return std::string(this->buffer.data(), this->buffer.size()); }
	operator std::string_view() const { return std::string_view(this->buffer.data(), this->buffer.size()); }

	/* Return a null terminated c string, this may cause the buffer to re-allocated to make room for the null terminator */
	const char *c_str()
	{
		if (this->size() == this->capacity()) format_detail::FmtResizeForCStr(this->buffer);
		*(this->end()) = '\0';
		return this->data();
	}
};

/**
 * format_target subtype with equivalent functionality to fmt::memory_buffer.
 *
 * Includes convenience wrappers to access the buffer.
 * Can be used as a fmt argument.
 */
struct format_buffer final : public format_buffer_base {
	static constexpr size_t STORAGE_SIZE = 512 - sizeof(format_buffer_base);
	alignas(alignof(void *)) char storage[STORAGE_SIZE];

	format_buffer() : format_buffer_base(STORAGE_SIZE) {}
};
static_assert(sizeof(format_buffer) == sizeof(format_buffer_base) + format_buffer::STORAGE_SIZE);

/**
 * format_target subtype with equivalent functionality to fmt::memory_buffer.
 * The inline buffer size is adjustable as a template parameter.
 *
 * Includes convenience wrappers to access the buffer.
 * Can be used as a fmt argument.
 */
template <size_t SIZE>
struct format_buffer_sized final : public format_buffer_base {
	static_assert(SIZE > 0);
	alignas(alignof(void *)) char storage[SIZE];

	format_buffer_sized() : format_buffer_base(SIZE) {}
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

struct format_to_fixed_base : public format_target_ctrl {
	friend format_target_ctrl;

private:
	char discard[31];
	char * const fixed_ptr;
	const size_t fixed_size;

	static void grow(fmt::detail::buffer<char> &, size_t);

protected:
	format_to_fixed_base(char *dst, size_t size) : format_target_ctrl(FL_FIXED, grow, dst, 0, size), fixed_ptr(dst), fixed_size(size) {}
	~format_to_fixed_base() = default;

public:
	char *data() noexcept { return this->fixed_ptr; }
	const char *data() const noexcept { return this->fixed_ptr; }

	size_t size() const noexcept
	{
		if (this->has_overflowed()) {
			return this->fixed_size;
		} else {
			return this->buffer.size();
		}
	}

	size_t fixed_capacity() const noexcept { return this->fixed_size; }
	void restore_size(size_t size);

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
	format_to_fixed(char *dst, size_t size) : format_to_fixed_base(dst, size) {}
	format_to_fixed(std::span<char> buf) : format_to_fixed_base(buf.data(), buf.size()) {}
};

/**
 * format_target subtype for writing to a fixed-size char buffer (using ptr, last semantics).
 *
 * Null-termination only occurs when the finalise method is called.
 */
struct format_to_fixed_z final : public format_to_fixed_base {
	format_to_fixed_z(char *dst, const char *last) : format_to_fixed_base(dst, last - dst) {}

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
		if (dst == last) {
			*dst = '\0';
			return dst;
		}
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
template <size_t N>
struct format_buffer_fixed final : public format_to_fixed_base {
	char strbuffer[N];

	format_buffer_fixed() : format_to_fixed_base(this->strbuffer, N) {}
};

const char *format_target_ctrl::data() const noexcept
{
	if ((this->flags & FL_FIXED) != 0) {
		return static_cast<const format_to_fixed_base *>(this)->fixed_ptr;
	} else {
		return this->buffer.data();
	}
}

size_t format_target_ctrl::size() const noexcept
{
	if ((this->flags & FL_OVERFLOW) != 0) {
		return static_cast<const format_to_fixed_base *>(this)->fixed_size;
	} else {
		return this->buffer.size();
	}
}

template <typename F>
struct format_lambda_wrapper {
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

/**
 * fmt::detail::buffer<char> implementation for a minimum overhead strictly non-growing and non-truncating fixed-size buffer.
 * Calls NOT_REACHED on overflow.
 */
struct fmt_base_fixed_non_growing final : public fmt::detail::buffer<char> {
private:
	static void grow(fmt::detail::buffer<char> &, size_t);

public:
	fmt_base_fixed_non_growing(char *buf, size_t capacity, size_t initial_size = 0) : buffer(grow, buf, initial_size, capacity) {}

	format_target &as_format_target() { return reinterpret_cast<format_target &>(*this); }
};

#endif /* FORMAT_HPP */
