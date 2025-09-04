/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file pool_id_type.hpp Definition of PoolID type. */

#ifndef POOL_ID_TYPE_HPP
#define POOL_ID_TYPE_HPP

/** Non-templated base for #PoolID for use with type trait queries. */
struct PoolIDBase {};

/**
 * Templated helper for PoolID.
 *
 * Example usage:
 *
 *   struct MyTypeTag : public PoolIDTraits<int, 16, 0xFF> {};
 *   using MyType = PoolID<MyTypeTag>;
 *
 * @tparam TBaseType Type of the derived class (i.e. the concrete usage of this class).
 * @tparam TEnd The PoolID at the end of the pool (equivalent to size).
 * @tparam TInvalid The PoolID denoting an invalid value.
 */
template <typename TBaseType, TBaseType TEnd, TBaseType TInvalid>
struct EMPTY_BASES PoolIDTraits {
	using BaseType = TBaseType;

protected:
	static constexpr BaseType End{TEnd};
	static constexpr BaseType Invalid{TInvalid};
};

/**
 * Templated helper to make a PoolID a single POD value.
 *
 * Example usage:
 *
 *   struct MyTypeTag : public PoolIDTraits<int, 16, 0xFF> {};
 *   using MyType = PoolID<MyTypeTag>;
 *
 * @tparam TBaseType Type of the derived class (i.e. the concrete usage of this class).
 * @tparam TTag An unique struct to keep types of the same TBaseType distinct.
 * @tparam TEnd The PoolID at the end of the pool (equivalent to size).
 * @tparam TInvalid The PoolID denoting an invalid value.
 */
template <typename TTag>
struct EMPTY_BASES PoolID : TTag, PoolIDBase {
	static inline constexpr bool fmt_as_base = true;
	static inline constexpr bool serialisation_as_base = true;
	static inline constexpr bool saveload_primitive_type = true;
	static inline constexpr bool string_parameter_as_base = true;
	static inline constexpr bool script_stack_value_as_base = true;
	static inline constexpr bool integer_type_hint = true;
	static inline constexpr bool hash_as_base = true;

	using BaseType = TTag::BaseType;
	using TagType = TTag;

	constexpr PoolID() = default;
	constexpr PoolID(const PoolID &) = default;
	constexpr PoolID(PoolID &&) = default;

	explicit constexpr PoolID(const BaseType &value) : value(value) {}

	constexpr PoolID &operator =(const PoolID &rhs) = default;
	constexpr PoolID &operator =(PoolID &&rhs) = default;

	/* Only allow conversion to BaseType via method. */
	constexpr BaseType base() const noexcept { return this->value; }
	constexpr BaseType &edit_base() { return this->value; }

	static constexpr PoolID Begin() { return PoolID{}; }
	static constexpr PoolID End() { return PoolID{TTag::End}; }
	static constexpr PoolID Invalid() { return PoolID{TTag::Invalid}; }

	constexpr auto operator++() { ++this->value; return this; }
	constexpr auto operator+(const std::integral auto &val) const { return this->value + val; }
	constexpr auto operator-(const std::integral auto &val) const { return this->value - val; }
	constexpr auto operator%(const std::integral auto &val) const { return this->value % val; }

	constexpr bool operator==(const PoolID<TTag> &rhs) const { return this->value == rhs.value; }
	constexpr auto operator<=>(const PoolID<TTag> &rhs) const { return this->value <=> rhs.value; }

	constexpr bool operator==(const size_t &rhs) const { return this->value == rhs; }
	constexpr auto operator<=>(const size_t &rhs) const { return this->value <=> rhs; }
private:
	/* Do not explicitly initialize. */
	BaseType value;
};

template <typename T> requires std::is_base_of_v<PoolIDBase, T>
constexpr auto operator+(const std::integral auto &val, const T &pool_id) { return pool_id + val; }
template <typename Te, typename Tp> requires std::is_enum_v<Te> && std::is_base_of_v<PoolIDBase, Tp>
constexpr auto operator+(const Te &val, const Tp &pool_id) { return pool_id + to_underlying(val); }

#endif /* POOL_ID_TYPE_HPP */
