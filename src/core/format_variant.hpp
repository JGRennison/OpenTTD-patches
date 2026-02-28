/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file format_variant.hpp Custom formatter for std::variant. */

/* The std::variant in fmt is not very useful as it doesn't indicate which value is being formatted.
 * Use our own variant formatter. */

#ifndef FORMAT_VARIANT_HPP
#define FORMAT_VARIANT_HPP

#include "format.hpp"
#include <variant>

constexpr inline std::string_view format_as(std::monostate) { return "monostate"; }

template <typename Char, typename... T>
struct fmt::formatter<std::variant<T...>, Char> : fmt::formatter<uint32_t> {
	bool use_base_fmt{};

	using underlying_type = uint32_t;
	using parent = typename fmt::formatter<underlying_type>;

	constexpr fmt::format_parse_context::iterator parse(fmt::format_parse_context &ctx)
	{
		return ctx.begin();
	}

	fmt::format_context::iterator format(const std::variant<T...> &t, format_context &ctx) const
	{
		if (t.valueless_by_exception()) return fmt::format_to(ctx.out(), "<invalid>");
		return std::visit([&](const auto& v) -> fmt::format_context::iterator {
			if constexpr (requires { format_as(v); }) {
				return fmt::format_to(ctx.out(), "({}: {})", t.index(), format_as(v));
			} else {
				return fmt::format_to(ctx.out(), "({}: {})", t.index(), v);
			}
		}, t);
	}
};

#endif /* FORMAT_VARIANT_HPP */
