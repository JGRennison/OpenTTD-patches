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

namespace TypeUtil {
#if !HAS_BUILTIN(__type_pack_element)
	template<size_t I, typename... T>
	struct TypeIndexer;

	template<typename T0, typename... More>
	struct TypeIndexer<0, T0, More...> {
		using type = T0;
	};

	template<typename T0, typename T1, typename... More>
	struct TypeIndexer<1, T0, T1, More...> {
		using type = T1;
	};

	template<typename T0, typename T1, typename T2, typename... More>
	struct TypeIndexer<2, T0, T1, T2, More...> {
		using type = T2;
	};

	template<typename T0, typename T1, typename T2, typename T3, typename... More>
	struct TypeIndexer<3, T0, T1, T2, T3, More...> {
		using type = T3;
	};

	template<size_t N, typename T0, typename T1, typename T2, typename T3, typename... More>
	requires (N >= 4)
	struct TypeIndexer<N, T0, T1, T2, T3, More...> : public TypeIndexer<N - 4, More...> {};
#endif
};

namespace std {
	template <typename... T>
	struct tuple_size<::TypeList<T...>> : std::integral_constant<std::size_t, sizeof...(T)> {};

	template <std::size_t I, typename... T>
	struct tuple_element<I, ::TypeList<T...>> {
#if HAS_BUILTIN(__type_pack_element)
		using type = __type_pack_element<I, T...>;
#else
		using type = typename TypeUtil::TypeIndexer<I, T...>::type;
#endif
	};
};

template <typename T>
struct MemberPtrTupleTypeAdapter {
private:
	template <typename H> struct TupleHelper;

	template <typename Tobj, typename... Targs>
	struct TupleHelper<std::tuple<Targs Tobj::*...>> {
		static constexpr size_t Count = sizeof...(Targs);
		using Object = Tobj;
		using Value = std::tuple<std::remove_cvref_t<Targs>...>;
		using Reference = std::tuple<std::remove_cvref_t<Targs> &...>;
		using ConstReference = std::tuple<const std::remove_cvref_t<Targs> &...>;
	};
	using Helper = TupleHelper<T>;

public:
	static constexpr size_t Count = Helper::Count;
	using Object = typename Helper::Object;
	using Value = typename Helper::Value;
	using Reference = typename Helper::Reference;
	using ConstReference = typename Helper::ConstReference;
};

template <typename Obj, typename T>
static constexpr auto MemberPtrsTie(Obj &object, const T &ptrs)
{
	auto handler = [&]<size_t... Tindices>(std::index_sequence<Tindices...>) -> auto {
		return std::tie(object.*std::get<Tindices>(ptrs)...);
	};
	return handler(std::make_index_sequence<MemberPtrTupleTypeAdapter<T>::Count>{});
}

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
