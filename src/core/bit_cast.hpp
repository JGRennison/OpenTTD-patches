/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bit_cast.hpp std::bit_cast fallback */

#ifndef BIT_CAST_HPP
#define BIT_CAST_HPP

#include <type_traits>

#ifdef __cpp_lib_bit_cast

#include <bit>

#else

namespace std {
template <typename To, typename From>
constexpr To bit_cast(const From& from) noexcept
{
	static_assert(std::is_trivially_constructible_v<To>);

	To to;
	memcpy(&to, &from, sizeof(To));
	return to;
}
}

#endif

template <typename To, typename From>
constexpr To bit_cast_to_storage(const From& from) noexcept
{
	static_assert(std::is_trivially_constructible_v<To>);
	static_assert(std::is_trivially_copyable_v<From>);
	static_assert(sizeof(To) >= sizeof(From));

	To to{};
	memcpy(&to, &from, sizeof(From));
	return to;
}

template <typename To, typename From>
constexpr To bit_cast_from_storage(const From& from) noexcept
{
	static_assert(std::is_trivially_constructible_v<To>);
	static_assert(std::is_trivially_copyable_v<From>);
	static_assert(sizeof(To) <= sizeof(From));

	To to{};
	memcpy(&to, &from, sizeof(To));
	return to;
}

#endif /* BIT_CAST_HPP */
