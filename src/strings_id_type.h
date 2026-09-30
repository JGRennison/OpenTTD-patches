/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file strings_id_type.h Types related to string IDs. */

#ifndef STRINGS_ID_TYPE_H
#define STRINGS_ID_TYPE_H

#include "core/strong_typedef_type.hpp"

/**
 * Numeric value that represents a string, independent of the selected language.
 */
struct StringIDTag : public StrongType::TypedefTraits<uint32_t, StrongType::Compare, StrongType::Integer> {};
using StringID = StrongType::Typedef<StringIDTag>;
static constexpr StringID STR_NULL{0};
static constexpr StringID INVALID_STRING_ID{0xFFFF}; ///< Constant representing an invalid string (16bit in case it is used in savegames)

class EncodedString;

#endif /* STRINGS_ID_TYPE_H */
