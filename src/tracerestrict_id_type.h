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
using TraceRestrictSlotID = PoolID<uint16_t, struct TraceRestrictSlotIDTag, 0xFFF0, 0xFFFF>;

/** Slot group pool ID type. */
using TraceRestrictSlotGroupID = PoolID<uint16_t, struct TraceRestrictSlotGroupIDTag, 0xFFF0, 0xFFFF>;

/** Counter pool ID type. */
using TraceRestrictCounterID = PoolID<uint16_t, struct TraceRestrictCounterIDTag, 0xFFF0, 0xFFFF>;

#endif /* TRACERESTRICT_ID_TYPE_H */
