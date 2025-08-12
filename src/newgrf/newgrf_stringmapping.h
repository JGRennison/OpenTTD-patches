/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_stringmapping.h NewGRF string mapping definition. */

#ifndef NEWGRF_STRINGMAPPING_H
#define NEWGRF_STRINGMAPPING_H

#include "../strings_type.h"
#include "../newgrf_text_type.h"
#include "../core/bit_cast.hpp"

/**
 * Information for mapping static StringIDs.
 */
using StringIDMappingHandler = void(*)(StringID, uintptr_t);

/**
 * Record a static StringID for getting translated later.
 * @param source Source grf-local GRFStringID.
 * @param data Arbitrary data (e.g pointer), must fit into a uintptr_t.
 * @param func Function to call to set the mapping result.
 */
template <typename T, typename F>
static void AddStringForMapping(GRFStringID source, T data, F func)
{
	static_assert(sizeof(T) <= sizeof(uintptr_t));

	extern void AddStringForMappingGeneric(GRFStringID source, uintptr_t data, StringIDMappingHandler func);
	AddStringForMappingGeneric(source, bit_cast_to_storage<uintptr_t>(data), [](StringID str, uintptr_t func_data) {
		F handler;
		handler(str, bit_cast_from_storage<T>(func_data));
	});
}

void AddStringForMapping(GRFStringID source, StringID *target);
void FinaliseStringMapping();

#endif /* NEWGRF_STRINGMAPPING_H */
