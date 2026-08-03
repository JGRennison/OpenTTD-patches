/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file command_type_fwd_declare.h Types related to commands. */

#ifndef COMMAND_TYPE_FWD_DECLARE_H
#define COMMAND_TYPE_FWD_DECLARE_H

#include "core/enum_type.hpp"

class CommandCost;

enum class DoCommandFlag : uint8_t;
using DoCommandFlags = EnumBitSet<DoCommandFlag, uint16_t>;

#endif /* COMMAND_TYPE_FWD_DECLARE_H */
