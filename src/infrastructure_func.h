/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file infrastructure_func.h Functions for access to (shared) infrastructure */

#ifndef INFRASTRUCTURE_FUNC_H
#define INFRASTRUCTURE_FUNC_H

#include "vehicle_base.h"
#include "settings_type.h"
#include "command_type.h"
#include "company_func.h"
#include "tile_map.h"

void PayStationSharingFee(Vehicle *v, const Station *st);
void PayDailyTrackSharingFee(Train *v);

bool CheckSharingChangePossible(VehicleType type, bool new_value);
void HandleSharingCompanyDeletion(Owner owner);
void UpdateAllBlockSignals(Owner owner = INVALID_OWNER);

/**
 * Check whether a vehicle of a given owner and type can use the infrastrucutre of a given company.
 * @param type        Type of vehicle we are talking about.
 * @param veh_owner   Owner of the vehicle in question.
 * @param infra_owner The owner of the infrastructure.
 * @return            True if infrastructure usage is allowed, false otherwise.
 */
static inline bool IsInfraUsageAllowed(VehicleType type, Owner veh_owner, Owner infra_owner)
{
	return infra_owner == veh_owner || infra_owner == OWNER_NONE || infra_owner == OWNER_TOWN || _settings_game.economy.infrastructure_sharing[type];
}

/**
 * Check whether a vehicle of a given owner and type can use the infrastrucutre on a given tile.
 * @param type        Type of vehicle we are talking about.
 * @param veh_owner   Owner of the vehicle in question.
 * @param tile        The tile that may or may not be used.
 * @return            True if infrastructure usage is allowed, false otherwise.
 */
static inline bool IsInfraTileUsageAllowed(VehicleType type, Owner veh_owner, TileIndex tile)
{
	return IsInfraUsageAllowed(type, veh_owner, GetTileOwner(tile));
}

/**
 * Is a vehicle owned by _current_company allowed to use the infrastructure of infra_owner?
 * If this is not allowed, this function provides the appropriate error message.
 * @see IsInfraUsageAllowed
 * @see CheckOwnership
 * @param type        Type of vehicle.
 * @param infra_owner Owner of the infrastructure.
 * @param tile        Tile of the infrastructure.
 * @return            CommandCost indicating success or failure.
 */
static inline CommandCost CheckInfraUsageAllowed(VehicleType type, Owner infra_owner, TileIndex tile = 0)
{
	if (infra_owner == OWNER_NONE || _settings_game.economy.infrastructure_sharing[type]) return CommandCost();
	return CheckOwnership(infra_owner, tile);
}

/**
 * Check whether a given company can control this vehicle.
 * Controlling a vehicle means permission to start, stop or reverse it or to make it ignore signals.
 * @param v The vehicle which may or may not be controlled.
 * @param o The company which may or may not control this vehicle.
 * @return  True if the given company is allowed to control this vehicle.
 */
static inline bool IsVehicleControlAllowed(const Vehicle *v, Owner o)
{
	return v->owner == o || (v->type == VEH_TRAIN && IsTileOwner(v->tile, o) && !v->IsChainInDepot());
}

/**
 * Check whether _current_company can control this vehicle.
 * If this is not allowed, this function provides the appropriate error message.
 * @see IsVehicleControlAllowed
 * @param v The vehicle which may or may not be controlled.
 * @return  CommandCost indicating success or failure.
 */
static inline CommandCost CheckVehicleControlAllowed(const Vehicle *v)
{
	if (v->type == VEH_TRAIN && IsTileOwner(v->tile, _current_company) && !v->IsChainInDepot()) return CommandCost();
	return CheckOwnership(v->owner);
}

/**
 * Do signal states propagate from the tracks of one owner to the other?
 * @note This function should be consistent, so if it returns true for (a, b) and (b, c),
 * it should also return true for (a, c).
 * @param o1 First track owner.
 * @param o2 Second track owner.
 * @return   True if tracks of the two owners are part of the same signal block.
 */
static inline bool IsOneSignalBlock(Owner o1, Owner o2)
{
	return o1 == o2 || _settings_game.economy.infrastructure_sharing[VEH_TRAIN];
}

#endif /* INFRASTRUCTURE_FUNC_H */
