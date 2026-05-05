/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file window_type_trait.h Trait types related to window types. */

#ifndef WINDOW_TYPE_TRAIT_H
#define WINDOW_TYPE_TRAIT_H

/** Trait to enable auto-conversion to window number. */
template <typename enum_type>
struct is_convertible_to_window_number {
	static constexpr bool value = false;
};

template <typename enum_type>
constexpr bool is_convertible_to_window_number_v = is_convertible_to_window_number<enum_type>::value;

#define DECLARE_CONVERTIBLE_TO_WINDOW_NUMBER(enum_type) \
	template <> struct is_convertible_to_window_number<enum_type> { \
		static const bool value = true; \
	};

/** Trait to enable auto-conversion to window invalidation data. */
template <typename enum_type>
struct is_convertible_to_window_invalidation_data {
	static constexpr bool value = false;
};

template <typename enum_type>
constexpr bool is_convertible_to_window_invalidation_data_v = is_convertible_to_window_invalidation_data<enum_type>::value;

#define DECLARE_CONVERTIBLE_TO_WINDOW_INVALIDATION_DATA(enum_type) \
	template <> struct is_convertible_to_window_invalidation_data<enum_type> { \
		static const bool value = true; \
	};

#endif /* WINDOW_TYPE_TRAIT_H */
