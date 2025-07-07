/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tracerestrict_id_type.h Trace Restrict pool ID types */

#ifndef TRACERESTRICT_ID_TYPE_H
#define TRACERESTRICT_ID_TYPE_H

#include "core/pool_type.hpp"

/** Program pool ID type. */
using TraceRestrictProgramID = PoolID<uint32_t, struct TraceRestrictProgramIDTag, 256000, 0xFFFFFFFF>;

/** Slot pool ID type. */
typedef uint16_t TraceRestrictSlotID;

/** Slot group pool ID type. */
typedef uint16_t TraceRestrictSlotGroupID;

/** Counter pool ID type. */
typedef uint16_t TraceRestrictCounterID;

#endif /* TRACERESTRICT_ID_TYPE_H */
