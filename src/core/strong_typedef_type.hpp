/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file strong_typedef_type.hpp Type (helpers) for making a strong typedef that is a distinct type. */

#ifndef STRONG_TYPEDEF_TYPE_HPP
#define STRONG_TYPEDEF_TYPE_HPP

/** Non-templated base for #StrongType::Typedef for use with type trait queries. */
struct StrongTypedefBase {};

namespace StrongType {
	/**
	 * Mix-in which makes the new Typedef comparable with itself and its base type.
	 */
	struct Compare {
		template <typename TType, typename TBaseType>
		struct mixin {
			friend constexpr bool operator ==(const TType &lhs, const TType &rhs) { return lhs.value == rhs.value; }
			friend constexpr bool operator ==(const TType &lhs, const TBaseType &rhs) { return lhs.value == rhs; }

			friend constexpr bool operator !=(const TType &lhs, const TType &rhs) { return lhs.value != rhs.value; }
			friend constexpr bool operator !=(const TType &lhs, const TBaseType &rhs) { return lhs.value != rhs; }

			friend constexpr bool operator <=(const TType &lhs, const TType &rhs) { return lhs.value <= rhs.value; }
			friend constexpr bool operator <=(const TType &lhs, const TBaseType &rhs) { return lhs.value <= rhs; }

			friend constexpr bool operator <(const TType &lhs, const TType &rhs) { return lhs.value < rhs.value; }
			friend constexpr bool operator <(const TType &lhs, const TBaseType &rhs) { return lhs.value < rhs; }

			friend constexpr bool operator >=(const TType &lhs, const TType &rhs) { return lhs.value >= rhs.value; }
			friend constexpr bool operator >=(const TType &lhs, const TBaseType &rhs) { return lhs.value >= rhs; }

			friend constexpr bool operator >(const TType &lhs, const TType &rhs) { return lhs.value > rhs.value; }
			friend constexpr bool operator >(const TType &lhs, const TBaseType &rhs) { return lhs.value > rhs; }

			friend constexpr auto operator <=>(const TType &lhs, const TType &rhs) { return lhs.value <=> rhs.value; }
			friend constexpr auto operator <=>(const TType &lhs, const TBaseType &rhs) { return lhs.value <=> rhs; }
		};
	};

	/**
	 * Mix-in which makes the new Typedef behave more like an integer. This means you can add and subtract from it.
	 *
	 * Operators like divide, multiply and module are explicitly denied, as that often makes little sense for the
	 * new type. If you want to do these actions on the new Typedef, you are better off first casting it to the
	 * base type.
	 */
	struct Integer {
		template <typename TType, typename TBaseType>
		struct mixin {
			friend constexpr TType &operator ++(TType &lhs) { lhs.value++; return lhs; }
			friend constexpr TType &operator --(TType &lhs) { lhs.value--; return lhs; }
			friend constexpr TType operator ++(TType &lhs, int) { TType res = lhs; lhs.value++; return res; }
			friend constexpr TType operator --(TType &lhs, int) { TType res = lhs; lhs.value--; return res; }

			friend constexpr TType &operator +=(TType &lhs, const TType &rhs) { lhs.value += rhs.value; return lhs; }
			friend constexpr TType operator +(const TType &lhs, const TType &rhs) { return TType(lhs.value + rhs.value); }
			friend constexpr TType operator +(const TType &lhs, const TBaseType &rhs) { return TType(lhs.value + rhs); }

			friend constexpr TType &operator -=(TType &lhs, const TType &rhs) { lhs.value -= rhs.value; return lhs; }
			friend constexpr TType operator -(const TType &lhs, const TType &rhs) { return TType(lhs.value - rhs.value); }
			friend constexpr TType operator -(const TType &lhs, const TBaseType &rhs) { return TType(lhs.value - rhs); }

			/* For most new types, the rest of the operators make no sense. For example,
			 * what does it actually mean to multiply a Year with a value. Or to do a
			 * bitwise OR on a Date. Or to divide a TileIndex by 2. Conceptually, they
			 * don't really mean anything. So force the user to first cast it to the
			 * base type, so the operation no longer returns the new Typedef. */

			constexpr TType &operator *=(const TType &rhs) = delete;
			constexpr TType operator *(const TType &rhs) = delete;
			constexpr TType operator *(const TBaseType &rhs) = delete;

			constexpr TType &operator /=(const TType &rhs) = delete;
			constexpr TType operator /(const TType &rhs) = delete;
			constexpr TType operator /(const TBaseType &rhs) = delete;

			constexpr TType &operator %=(const TType &rhs) = delete;
			constexpr TType operator %(const TType &rhs) = delete;
			constexpr TType operator %(const TBaseType &rhs) = delete;

			constexpr TType &operator &=(const TType &rhs) = delete;
			constexpr TType operator &(const TType &rhs) = delete;
			constexpr TType operator &(const TBaseType &rhs) = delete;

			constexpr TType &operator |=(const TType &rhs) = delete;
			constexpr TType operator |(const TType &rhs) = delete;
			constexpr TType operator |(const TBaseType &rhs) = delete;

			constexpr TType &operator ^=(const TType &rhs) = delete;
			constexpr TType operator ^(const TType &rhs) = delete;
			constexpr TType operator ^(const TBaseType &rhs) = delete;

			constexpr TType &operator <<=(const TType &rhs) = delete;
			constexpr TType operator <<(const TType &rhs) = delete;
			constexpr TType operator <<(const TBaseType &rhs) = delete;

			constexpr TType &operator >>=(const TType &rhs) = delete;
			constexpr TType operator >>(const TType &rhs) = delete;
			constexpr TType operator >>(const TBaseType &rhs) = delete;

			constexpr TType operator ~() = delete;
			constexpr TType operator -() = delete;
		};
	};

	/**
	 * Mix-in which makes the new Typedef behave more like an integer. This means you can add and subtract from it.
	 *
	 * Operators like divide, multiply and module are permitted.
	 */
	struct IntegerScalable {
		template <typename TType, typename TBaseType>
		struct mixin {
			friend constexpr TType &operator ++(TType &lhs) { lhs.value++; return lhs; }
			friend constexpr TType &operator --(TType &lhs) { lhs.value--; return lhs; }
			friend constexpr TType operator ++(TType &lhs, int) { TType res = lhs; lhs.value++; return res; }
			friend constexpr TType operator --(TType &lhs, int) { TType res = lhs; lhs.value--; return res; }

			friend constexpr TType &operator +=(TType &lhs, const TType &rhs) { lhs.value += rhs.value; return lhs; }
			friend constexpr TType operator +(const TType &lhs, const TType &rhs) { return TType{ lhs.value + rhs.value }; }
			friend constexpr TType operator +(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value + rhs }; }

			friend constexpr TType &operator -=(TType &lhs, const TType &rhs) { lhs.value -= rhs.value; return lhs; }
			friend constexpr TType operator -(const TType &lhs, const TType &rhs) { return TType{ lhs.value - rhs.value }; }
			friend constexpr TType operator -(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value - rhs }; }

			friend constexpr TType &operator *=(TType &lhs, const TType &rhs) { lhs.value *= rhs.value; return lhs; }
			friend constexpr TType operator *(const TType &lhs, const TType &rhs) { return TType{ lhs.value * rhs.value }; }
			friend constexpr TType operator *(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value * rhs }; }

			friend constexpr TType &operator /=(TType &lhs, const TType &rhs) { lhs.value /= rhs.value; return lhs; }
			friend constexpr TType operator /(const TType &lhs, const TType &rhs) { return TType{ lhs.value / rhs.value }; }
			friend constexpr TType operator /(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value / rhs }; }

			friend constexpr TType &operator %=(TType &lhs, const TType &rhs) { lhs.value %= rhs.value; return lhs; }
			friend constexpr TType operator %(const TType &lhs, const TType &rhs) { return TType{ lhs.value % rhs.value }; }
			friend constexpr TType operator %(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value % rhs }; }

			friend constexpr TType operator -(const TType &lhs) { return TType{ -lhs.value }; }

			/* For most new types, the rest of the operators make no sense. */

			constexpr TType &operator &=(const TType &rhs) = delete;
			constexpr TType operator &(const TType &rhs) = delete;
			constexpr TType operator &(const TBaseType &rhs) = delete;

			constexpr TType &operator |=(const TType &rhs) = delete;
			constexpr TType operator |(const TType &rhs) = delete;
			constexpr TType operator |(const TBaseType &rhs) = delete;

			constexpr TType &operator ^=(const TType &rhs) = delete;
			constexpr TType operator ^(const TType &rhs) = delete;
			constexpr TType operator ^(const TBaseType &rhs) = delete;

			constexpr TType &operator <<=(const TType &rhs) = delete;
			constexpr TType operator <<(const TType &rhs) = delete;
			constexpr TType operator <<(const TBaseType &rhs) = delete;

			constexpr TType &operator >>=(const TType &rhs) = delete;
			constexpr TType operator >>(const TType &rhs) = delete;
			constexpr TType operator >>(const TBaseType &rhs) = delete;

			constexpr TType operator ~() = delete;
		};
	};

	/**
	 * Mix-in which makes the new Typedef behave more like an integer. This means you can add and subtract from it.
	 *
	 * Operators like divide, multiply and module are explicitly denied, as that often makes little sense for the
	 * new type. If you want to do these actions on the new Typedef, you are better off first casting it to the
	 * base type.
	 *
	 * Subtracting the new Typedef from another new Typedef produces TDeltaType, instead of another new Typedef.
	 * e.g. subtracting an absolute time from another absolute time should produce a duration, not another absolute time.
	 * Adding a new Typedef to another new Typedef is not allowed.
	 * TDeltaType should be another StrongType::Typedef.
	 */
	template <typename TDeltaType>
	struct IntegerDelta {
		template <typename TType, typename TBaseType>
		struct mixin {
			friend constexpr TType &operator ++(TType &lhs) { lhs.value++; return lhs; }
			friend constexpr TType &operator --(TType &lhs) { lhs.value--; return lhs; }
			friend constexpr TType operator ++(TType &lhs, int) { TType res = lhs; lhs.value++; return res; }
			friend constexpr TType operator --(TType &lhs, int) { TType res = lhs; lhs.value--; return res; }

			template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
			friend constexpr TType &operator +=(TType &lhs, const T &rhs) = delete;
			template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
			friend constexpr TType &operator +(const TType &lhs, const T &rhs) = delete;

			friend constexpr TType &operator +=(TType &lhs, const TDeltaType &rhs) { lhs.value += rhs.value; return lhs; }
			friend constexpr TType &operator +=(TType &lhs, const TBaseType &rhs) { lhs.value += rhs; return lhs; }
			friend constexpr TType operator +(const TType &lhs, const TDeltaType &rhs) { return TType{ lhs.value + rhs.value }; }
			friend constexpr TType operator +(const TDeltaType &lhs, const TType &rhs) { return TType{ lhs.value + rhs.value }; }
			friend constexpr TType operator +(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value + rhs }; }
			friend constexpr TType operator +(const TBaseType &lhs, const TType &rhs) { return TType{ lhs + rhs.value }; }

			template <class T, typename std::enable_if<std::is_same<T, TType>::value>::type>
			friend constexpr TType &operator -=(TType &lhs, const T &rhs) = delete;

			friend constexpr TType &operator -=(TType &lhs, const TDeltaType &rhs) { lhs.value -= rhs.value; return lhs; }
			friend constexpr TType &operator -=(TType &lhs, const TBaseType &rhs) { lhs.value -= rhs; return lhs; }
			friend constexpr TDeltaType operator -(const TType &lhs, const TType &rhs) { return TDeltaType{ lhs.value - rhs.value }; }
			friend constexpr TType operator -(const TType &lhs, const TDeltaType &rhs) { return TType{ lhs.value - rhs.value }; }
			friend constexpr TType operator -(const TType &lhs, const TBaseType &rhs) { return TType{ lhs.value - rhs }; }

			constexpr TDeltaType AsDelta() const { return TDeltaType{ static_cast<const TType &>(*this).value }; }

			/* For most new types, the rest of the operators make no sense. For example,
			 * what does it actually mean to multiply a Year with a value. Or to do a
			 * bitwise OR on a Date. Or to divide a TileIndex by 2. Conceptually, they
			 * don't really mean anything. So force the user to first cast it to the
			 * base type, so the operation no longer returns the new Typedef. */

			constexpr TType &operator *=(const TType &rhs) = delete;
			constexpr TType operator *(const TType &rhs) = delete;
			constexpr TType operator *(const TBaseType &rhs) = delete;

			constexpr TType &operator /=(const TType &rhs) = delete;
			constexpr TType operator /(const TType &rhs) = delete;
			constexpr TType operator /(const TBaseType &rhs) = delete;

			constexpr TType &operator %=(const TType &rhs) = delete;
			constexpr TType operator %(const TType &rhs) = delete;
			constexpr TType operator %(const TBaseType &rhs) = delete;

			constexpr TType &operator &=(const TType &rhs) = delete;
			constexpr TType operator &(const TType &rhs) = delete;
			constexpr TType operator &(const TBaseType &rhs) = delete;

			constexpr TType &operator |=(const TType &rhs) = delete;
			constexpr TType operator |(const TType &rhs) = delete;
			constexpr TType operator |(const TBaseType &rhs) = delete;

			constexpr TType &operator ^=(const TType &rhs) = delete;
			constexpr TType operator ^(const TType &rhs) = delete;
			constexpr TType operator ^(const TBaseType &rhs) = delete;

			constexpr TType &operator <<=(const TType &rhs) = delete;
			constexpr TType operator <<(const TType &rhs) = delete;
			constexpr TType operator <<(const TBaseType &rhs) = delete;

			constexpr TType &operator >>=(const TType &rhs) = delete;
			constexpr TType operator >>(const TType &rhs) = delete;
			constexpr TType operator >>(const TBaseType &rhs) = delete;

			constexpr TType operator ~() = delete;
			constexpr TType operator -() = delete;
		};
	};

	/**
	 * Mix-in which makes the new Typedef compatible with another type (which is not the base type).
	 *
	 * @note The base type of the new Typedef will be cast to the other type; so make sure they are compatible.
	 *
	 * @tparam TCompatibleType The other type to be compatible with.
	 */
	template <typename TCompatibleType>
	struct Compatible {
		template <typename TType, typename TBaseType>
		struct mixin {
			friend constexpr bool operator ==(const TType &lhs, TCompatibleType rhs) { return lhs.value == static_cast<TBaseType>(rhs); }
			friend constexpr bool operator !=(const TType &lhs, TCompatibleType rhs) { return lhs.value != static_cast<TBaseType>(rhs); }

			friend constexpr bool operator <=(const TType &lhs, TCompatibleType rhs) { return lhs.value <= static_cast<TBaseType>(rhs); }
			friend constexpr bool operator <(const TType &lhs, TCompatibleType rhs) { return lhs.value < static_cast<TBaseType>(rhs); }
			friend constexpr bool operator >=(const TType &lhs, TCompatibleType rhs) { return lhs.value >= static_cast<TBaseType>(rhs); }
			friend constexpr bool operator >(const TType &lhs, TCompatibleType rhs) { return lhs.value > static_cast<TBaseType>(rhs); }

			friend constexpr TType operator +(const TType &lhs, TCompatibleType rhs) { return TType(lhs.value + rhs); }
			friend constexpr TType operator -(const TType &lhs, TCompatibleType rhs) { return TType(lhs.value - rhs); }
		};
	};

	namespace detail {
		template <typename... TProperties>
		struct Properties {};
	}

	/**
	 * Templated type trait definition helper to be used with Strongtype::Typedef.
	 *
	 * @tparam TBaseType Type of the derived class (i.e. the concrete usage of this class).
	 * @tparam TProperties A list of mixins to add to the class.
	 */
	template <typename TBaseType, typename... TProperties>
	struct TypedefTraits {
		using BaseType = TBaseType;
		using Properties = detail::Properties<TProperties...>;
	};

	namespace detail {
		template <typename TParent, typename TTraits, typename TProperties> struct EMPTY_BASES TypedefTraitHelper;

		template <typename TParent, typename TTraits, typename... TProperties>
		struct EMPTY_BASES TypedefTraitHelper<TParent, TTraits, detail::Properties<TProperties...>> : public TProperties::template mixin<TParent, typename TTraits::BaseType>... {};
	}
}

template <typename TTraits>
struct EMPTY_BASES ST : public StrongTypedefBase, public fmt_format_as_base, public StrongType::detail::TypedefTraitHelper<ST<TTraits>, TTraits, typename TTraits::Properties> {
	using BaseType = typename TTraits::BaseType;

	constexpr ST() = default;
	constexpr ST(const ST &) = default;
	constexpr ST(ST &&) = default;

	explicit constexpr ST(const BaseType &value) : value(value) {}

	constexpr ST &operator =(const ST &rhs) { this->value = rhs.value; return *this; }
	constexpr ST &operator =(ST &&rhs) { this->value = std::move(rhs.value); return *this; }

	/* Only allow conversion to BaseType via method. */
	constexpr BaseType base() const { return this->value; }
	constexpr BaseType &edit_base() { return this->value; }

	/* Only allow TProperties classes access to the internal value. Everyone else needs to call .base(). */
	friend struct StrongType::Compare;
	friend struct StrongType::Integer;
	friend struct StrongType::IntegerScalable;
	template <typename TDeltaType> friend struct StrongType::IntegerDelta;
	template <typename TCompatibleType> friend struct StrongType::Compatible;

/* GCC / MSVC don't pick up on the "friend struct" above, where CLang does.
* As in our CI we compile for all three targets, it is sufficient to have one
* that errors on this; but nobody should be using "value" directly. Instead,
* use base() to convert to the base type. */
#ifdef __clang__
protected:
#endif /* __clang__ */
	BaseType value{};
};

template <typename TTraits>
struct EMPTY_BASES STRef : public StrongTypedefBase, public fmt_format_as_base, public StrongType::detail::TypedefTraitHelper<STRef<TTraits>, TTraits, typename TTraits::Properties> {
	using ValueType = ST<TTraits>;
	using BaseType = typename TTraits::BaseType;

	constexpr STRef(STRef &) = default;
	constexpr STRef(ValueType &target) : value(target.edit_base()) {}

	explicit constexpr STRef(BaseType &value) : value(value) {}

	constexpr STRef &operator =(const STRef &rhs) { this->value = rhs.value; return *this; }
	constexpr STRef &operator =(STRef &&rhs) { this->value = std::move(rhs.value); return *this; }
	constexpr STRef &operator =(const ValueType &rhs) { this->value = rhs.base(); return *this; }
	constexpr STRef &operator =(ValueType &&rhs) { this->value = std::move(rhs.edit_base()); return *this; }

	constexpr operator ValueType() const { return ValueType{this->value}; }

	/* Only allow conversion to BaseType via method. */
	constexpr BaseType base() const { return this->value; }
	constexpr BaseType &edit_base() { return this->value; }

	/* Only allow TProperties classes access to the internal value. Everyone else needs to call .base(). */
	friend struct StrongType::Compare;
	friend struct StrongType::Integer;
	friend struct StrongType::IntegerScalable;
	template <typename TDeltaType> friend struct StrongType::IntegerDelta;
	template <typename TCompatibleType> friend struct StrongType::Compatible;

/* GCC / MSVC don't pick up on the "friend struct" above, where CLang does.
* As in our CI we compile for all three targets, it is sufficient to have one
* that errors on this; but nobody should be using "value" directly. Instead,
* use base() to convert to the base type. */
#ifdef __clang__
protected:
#endif /* __clang__ */
	BaseType &value;
};

namespace StrongType {
	/**
	 * Templated helper to make a type-safe 'typedef' representing a single POD value.
	 * A normal 'typedef' is not distinct from its base type and will be treated as
	 * identical in many contexts. This class provides a distinct type that can still
	 * be assigned from and compared to values of its base type.
	 *
	 * Example usage:
	 *
	 *   struct MyTypeTag : public StrongType::TypedefTraits<int, StrongType::Compare, StrongType::Integer> {};
	 *   using MyType = StrongType::Typedef<MyTypeTag>;
	 *
	 * @tparam TTraits A type inheriting from StrongType::TypedefTraits<TBaseType, TProperties...>
	 * @tparam TTag An unique struct to keep types of the same TBaseType distinct.
	 * @tparam TProperties A list of mixins to add to the class.
	 */
	template <typename TTraits>
	using Typedef = ST<TTraits>;

	/**
	 * Templated helper to make a type-safe reference wrapper type over the base type.
	 * A normal 'typedef' is not distinct from its base type and will be treated as
	 * identical in many contexts. This class provides a distinct type that can still
	 * be assigned from and compared to values of its base type.
	 *
	 * Example usage:
	 *
	 *   struct MyTypeTag : public StrongType::TypedefTraits<int, StrongType::Compare, StrongType::Integer> {};
	 *   using MyType = StrongType::Typedef<MyTypeTag>;
	 *   using MyTypeBaseRef = StrongType::BaseRefTypedef<MyTypeTag>;
	 *
	 * @tparam TTypedef Corresponding StrongType::Typedef.
	 * @tparam TProperties A list of mixins to add to the class.
	 */
	template <typename TTraits>
	using BaseRefTypedef = STRef<TTraits>;
}

#endif /* STRONG_TYPEDEF_TYPE_HPP */
