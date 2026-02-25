/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file type_util.hpp Type utilities */

#ifndef TYPE_UTIL_HPP
#define TYPE_UTIL_HPP

#include <tuple>
#include <type_traits>

/**
 * Non-instantiable generic class to hold type parameters.
 */
template <typename... T>
struct TypeList {
	static constexpr size_t Size = sizeof...(T);

private:
	TypeList() = default;
};

template <typename T>
struct TupleTypeAdapter {
private:
	template <typename H> struct TupleHelper;

	template <typename... Targs>
	struct TupleHelper<std::tuple<Targs...>> {
		using Value = std::tuple<std::remove_cvref_t<Targs>...>;
		using Reference = std::tuple<std::remove_cvref_t<Targs> &...>;
		using ConstReference = std::tuple<const std::remove_cvref_t<Targs> &...>;
	};
	using Helper = TupleHelper<T>;

public:
	using Value = typename Helper::Value;
	using Reference = typename Helper::Reference;
	using ConstReference = typename Helper::ConstReference;
};

namespace TypesDetail {
	template <typename TFind, typename... T>
	constexpr size_t GetTypePackIndexIgnoreCvRefOrSize()
	{
		constexpr size_t count = sizeof...(T);
		constexpr bool found[count] = { std::is_same_v<std::remove_cvref_t<T>, TFind> ... };
		size_t n = count;
		for (size_t i = 0; i < count; ++i) {
			if (found[i]) {
				if (n < count) return count; // more than one TFind found
				n = i;
			}
		}
		return n;
	}
}

/**
 * Returns the index of type TFind in typename pack T..., ignoring all cvref qualifiers.
 * static_asserts unless exactly one instance of TFind is found.
 */
template <typename TFind, typename... T>
constexpr size_t GetTypePackIndexIgnoreCvRef()
{
	constexpr size_t result = TypesDetail::GetTypePackIndexIgnoreCvRefOrSize<TFind, T...>();
	static_assert(result < sizeof...(T));
	return result;
}

namespace TypesDetail {
	template <typename TFind, typename H> struct GetTypeListIndexIgnoreCvRefHelper;

	template <typename TFind, typename... Targs>
	struct GetTypeListIndexIgnoreCvRefHelper<TFind, TypeList<Targs...>> {
		static constexpr size_t Get()
		{
			return GetTypePackIndexIgnoreCvRef<TFind, Targs...>();
		}
	};
}

/**
 * Returns the index of type TFind in TypeList type T, ignoring all cvref qualifiers.
 * static_asserts unless exactly one instance of TFind is found.
 */
template <typename TFind, typename T>
constexpr size_t GetTypeListIndexIgnoreCvRef()
{
	using Helper = typename TypesDetail::GetTypeListIndexIgnoreCvRefHelper<TFind, std::remove_cvref_t<T>>;
	return Helper::Get();
}

#endif /* TYPE_UTIL_HPP */
