/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_depotlist.cpp Implementation of ScriptDepotList and friends. */

#include "../../stdafx.h"
#include "script_depotlist.hpp"
#include "../../depot_base.h"
#include "../../company_base.h"
#include "../../station_base.h"

#include "../../safeguards.h"

ScriptDepotList::ScriptDepotList(ScriptTile::TransportType transport_type)
{
	EnforceDeityOrCompanyModeValid_Void();
	::TileType tile_type;
	switch (transport_type) {
		default: return;

		case ScriptTile::TRANSPORT_ROAD:  tile_type = ::MP_ROAD; break;
		case ScriptTile::TRANSPORT_RAIL:  tile_type = ::MP_RAILWAY; break;
		case ScriptTile::TRANSPORT_WATER: tile_type = ::MP_WATER; break;

		case ScriptTile::TRANSPORT_AIR: {
			/* Hangars are not seen as real depots by the depot code. */
			for (const Station *st : Station::Iterate()) {
				if (st->owner == ScriptObject::GetCompany() || ScriptCompanyMode::IsDeity()) {
					for (uint i = 0; i < st->airport.GetNumHangars(); i++) {
						this->AddItem(st->airport.GetHangarTile(i));
					}
				}
			}
			return;
		}
	}

	/* Handle 'standard' depots. */
	for (const Depot *depot : Depot::Iterate()) {
		if ((::GetTileOwner(depot->xy) == ScriptObject::GetCompany() || ScriptCompanyMode::IsDeity()) && ::IsTileType(depot->xy, tile_type)) this->AddItem(depot->xy);
	}
}


/** static **/ ScriptDepotList *ScriptDepotList::GetAllDepots(ScriptTile::TransportType transport_type)
{
	ScriptDepotList *list = new ScriptDepotList();
	::TileType tile_type;
	::VehicleType veh_type;
	switch (transport_type) {
		default: return list;

		case ScriptTile::TRANSPORT_ROAD:  tile_type = ::MP_ROAD; veh_type = VEH_ROAD; break;
		case ScriptTile::TRANSPORT_RAIL:  tile_type = ::MP_RAILWAY; veh_type = VEH_TRAIN; break;
		case ScriptTile::TRANSPORT_WATER: tile_type = ::MP_WATER; veh_type = VEH_SHIP; break;

		case ScriptTile::TRANSPORT_AIR:
		{
/* Hangars are not seen as real depots by the depot code. */
			for (const Station *st : Station::Iterate()) {
				if (st->owner == ScriptObject::GetCompany() || ScriptObject::GetCompany() == OWNER_DEITY
					|| (_settings_game.economy.infrastructure_sharing[VEH_AIRCRAFT]
						&& ::Company::Get(st->owner)->settings.infra_others_buy_in_depot[VEH_AIRCRAFT])) {
					for (uint i = 0; i < st->airport.GetNumHangars(); i++) {
						list->AddItem(st->airport.GetHangarTile(i));
					}
				}
			}
			return list;
		}
	}

	/* Handle 'standard' depots. */
	for (const Depot *depot : Depot::Iterate()) {
		if ((::GetTileOwner(depot->xy) == ScriptObject::GetCompany()
			|| ScriptObject::GetCompany() == OWNER_DEITY
			|| (_settings_game.economy.infrastructure_sharing[veh_type]
				&& ::Company::Get(::GetTileOwner(depot->xy))->settings.infra_others_buy_in_depot[veh_type])) && ::IsTileType(depot->xy, tile_type)) list->AddItem(depot->xy);
	}

	return list;
}

/* static */ bool ScriptDepotList::CanBuiltInDepot(TileIndex depotTile)
{
	if (!IsDepotTile(depotTile)) return false;

	if (::GetTileOwner(depotTile) == ScriptObject::GetCompany() || ScriptObject::GetCompany() == OWNER_DEITY) return true;

	::VehicleType veh_type;
	if (IsHangarTile(depotTile)) veh_type = VEH_AIRCRAFT;
	else {
		TileType tile_type = GetTileType(depotTile);
		switch (tile_type) {
			default: return false;

			case TileType::MP_ROAD: veh_type = VEH_ROAD; break;
			case TileType::MP_RAILWAY: veh_type = VEH_TRAIN; break;
			case TileType::MP_WATER: veh_type = VEH_SHIP; break;
		}
	}

	return _settings_game.economy.infrastructure_sharing[veh_type] && ::Company::Get(::GetTileOwner(depotTile))->settings.infra_others_buy_in_depot[veh_type];
}

ScriptDepotList::ScriptDepotList()
{}
