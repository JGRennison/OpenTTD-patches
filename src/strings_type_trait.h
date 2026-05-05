/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file strings_type_trait.h Trait types related to string parameters. */

#ifndef STRINGS_TYPE_TRAIT_H
#define STRINGS_TYPE_TRAIT_H

/** Trait to enable auto-conversion of scoped enum to string parameter. */
template <typename enum_type>
struct is_scoped_enum_convertible_to_string_parameter {
	static constexpr bool value = false;
};

template <typename enum_type>
constexpr bool is_scoped_enum_convertible_to_string_parameter_v = is_scoped_enum_convertible_to_string_parameter<enum_type>::value;

#define DECLARE_SCOPED_ENUM_CONVERTIBLE_TO_STRING_PARAMETER(enum_type) \
	template <> struct is_scoped_enum_convertible_to_string_parameter<enum_type> { \
		static const bool value = true; \
	};

#endif /* STRINGS_TYPE_TRAIT_H */
