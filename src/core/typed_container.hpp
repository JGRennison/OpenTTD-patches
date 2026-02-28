/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file typed_container.hpp Wrappers for typed index containers. */

#ifndef TYPED_CONTAINER_HPP
#define TYPED_CONTAINER_HPP

/**
 * List of policies which can be applied to TypedIndexContainer.
 */
enum class TypedIndexContainerPolicy {
	AllowInteger, ///< Allow integer (size_t) indexing.
};

/**
 * A sort-of mixin that implements 'at(pos)' and 'operator[](pos)' only for a specific type.
 * The type must have a suitable '.base()' method.
 * This to prevent accidental use of the wrong index type, and to prevent having to call '.base()'.
 */
template <typename Container, typename Index, TypedIndexContainerPolicy... Policies>
class TypedIndexContainer : public Container {
	static constexpr bool INTEGER_ALLOWED = ((Policies == TypedIndexContainerPolicy::AllowInteger) || ...);

public:
	constexpr Container::reference at(size_t pos) { static_assert(INTEGER_ALLOWED); return this->Container::at(pos); }
	constexpr Container::reference at(const Index &pos) { return this->Container::at(pos.base()); }

	constexpr Container::const_reference at(size_t pos) const { static_assert(INTEGER_ALLOWED); return this->Container::at(pos); }
	constexpr Container::const_reference at(const Index &pos) const { return this->Container::at(pos.base()); }

	constexpr Container::reference operator[](size_t pos) { static_assert(INTEGER_ALLOWED); return this->Container::operator[](pos); }
	constexpr Container::reference operator[](const Index &pos) { return this->Container::operator[](pos.base()); }

	constexpr Container::const_reference operator[](size_t pos) const { static_assert(INTEGER_ALLOWED); return this->Container::operator[](pos); }
	constexpr Container::const_reference operator[](const Index &pos) const { return this->Container::operator[](pos.base()); }
};

#endif /* TYPED_CONTAINER_HPP */
