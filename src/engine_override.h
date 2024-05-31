/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_override.h Engine override functionality. */

#ifndef ENGINE_OVERRIDE_H
#define ENGINE_OVERRIDE_H

#include "engine_type.h"
#include "vehicle_type.h"

#include "3rdparty/robin_hood/robin_hood.h"
#include <vector>

struct EngineIDMapping {
	uint32_t grfid;          ///< The GRF ID of the file the entity belongs to
	uint16_t internal_id;    ///< The internal ID within the GRF file
	VehicleType type;        ///< The engine type
	uint8_t  substitute_id;  ///< The (original) entity ID to use if this GRF is not available (currently not used)
};

/**
 * Stores the mapping of EngineID to the internal id of newgrfs.
 * Note: This is not part of Engine, as the data in the EngineOverrideManager and the engine pool get resetted in different cases.
 */
struct EngineOverrideManager : std::vector<EngineIDMapping> {
	static const uint NUM_DEFAULT_ENGINES; ///< Number of default entries

private:
	static uint64_t HashKey(VehicleType type, uint16_t grf_local_id, uint32_t grfid)
	{
		return grfid | (static_cast<uint64_t>(grf_local_id) << 32) | (static_cast<uint64_t>(type) << 48);
	}

	static uint64_t HashKey(const EngineIDMapping &eid)
	{
		return HashKey(eid.type, eid.internal_id, eid.grfid);
	}

	robin_hood::unordered_map<uint64_t, EngineID> mapping_index;

public:
	void ResetToDefaultMapping();
	EngineID GetID(VehicleType type, uint16_t grf_local_id, uint32_t grfid);

	void AddToIndex(EngineID id);
	void RemoveFromIndex(EngineID id);
	void ReIndex();

	static bool ResetToCurrentNewGRFConfig();
};

extern EngineOverrideManager _engine_mngr;

#endif /* ENGINE_BASE_H */
