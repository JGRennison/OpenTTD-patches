/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file money_type.h Money type definition. */

#ifndef MONEY_TYPE_H
#define MONEY_TYPE_H

#include "core/overflowsafe_type.hpp"

typedef OverflowSafeInt64 Money;

enum class Price : uint8_t;

#endif /* MONEY_TYPE_H */
