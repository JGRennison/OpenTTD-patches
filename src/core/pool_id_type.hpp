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
 * Templated helper to make a PoolID a single POD value.
 *
 * Example usage:
 *
 *   using MyType = PoolID<int, struct MyTypeTag, 16, 0xFF>;
 *
 * @tparam TBaseType Type of the derived class (i.e. the concrete usage of this class).
 * @tparam TTag An unique struct to keep types of the same TBaseType distinct.
 * @tparam TEnd The PoolID at the end of the pool (equivalent to size).
 * @tparam TInvalid The PoolID denoting an invalid value.
 */
template <typename TBaseType, typename TTag, TBaseType TEnd, TBaseType TInvalid>
struct EMPTY_BASES PoolID : PoolIDBase {
	static inline constexpr bool fmt_as_base = true;
	static inline constexpr bool serialisation_as_base = true;
	static inline constexpr bool saveload_primitive_type = true;
	static inline constexpr bool string_parameter_as_base = true;
	static inline constexpr bool script_stack_value_as_base = true;
	static inline constexpr bool integer_type_hint = true;
	static inline constexpr bool hash_as_base = true;

	using BaseType = TBaseType;

	constexpr PoolID() = default;
	constexpr PoolID(const PoolID &) = default;
	constexpr PoolID(PoolID &&) = default;

	explicit constexpr PoolID(const TBaseType &value) : value(value) {}

	constexpr PoolID &operator =(const PoolID &rhs) { this->value = rhs.value; return *this; }
	constexpr PoolID &operator =(PoolID &&rhs) { this->value = std::move(rhs.value); return *this; }

	/* Only allow conversion to BaseType via method. */
	constexpr TBaseType base() const noexcept { return this->value; }
	constexpr TBaseType &edit_base() { return this->value; }

	static constexpr PoolID Begin() { return PoolID{}; }
	static constexpr PoolID End() { return PoolID{static_cast<TBaseType>(TEnd)}; }
	static constexpr PoolID Invalid() { return PoolID{static_cast<TBaseType>(TInvalid)}; }

	constexpr auto operator++() { ++this->value; return this; }
	constexpr auto operator+(const std::integral auto &val) const { return this->value + val; }

	constexpr bool operator==(const PoolID<TBaseType, TTag, TEnd, TInvalid> &rhs) const { return this->value == rhs.value; }
	constexpr auto operator<=>(const PoolID<TBaseType, TTag, TEnd, TInvalid> &rhs) const { return this->value <=> rhs.value; }

	constexpr bool operator==(const size_t &rhs) const { return this->value == rhs; }
	constexpr auto operator<=>(const size_t &rhs) const { return this->value <=> rhs; }
private:
	/* Do not explicitly initialize. */
	TBaseType value;
};

#endif /* POOL_ID_TYPE_HPP */
