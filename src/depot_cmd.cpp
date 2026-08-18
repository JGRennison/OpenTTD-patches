/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_cmd.cpp %Command Handling for depots. */

#include "stdafx.h"
#include "command_func.h"
#include "depot_base.h"
#include "depot_bridge.h"
#include "depot_cmd.h"
#include "company_func.h"
#include "string_func.h"
#include "town.h"
#include "vehicle_gui.h"
#include "vehiclelist.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Check whether the given name is globally unique amongst depots.
 * @param name The name to check.
 * @return True if there is no depot with the given name.
 */
static bool IsUniqueDepotName(std::string_view name)
{
	for (const Depot *d : Depot::Iterate()) {
		if (!d->name.empty() && d->name == name) return false;
	}

	return true;
}

/**
 * Rename a depot.
 * @param flags type of operation
 * @param depot_id id of depot
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameDepot(DoCommandFlags flags, DepotID depot_id, const std::string &text)
{
	Depot *d = Depot::GetIfValid(depot_id);
	if (d == nullptr) return CMD_ERROR;

	CommandCost ret = CheckTileOwnership(d->xy);
	if (ret.Failed()) return ret;

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_DEPOT_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueDepotName(text)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		if (reset) {
			d->name.clear();
			MakeDefaultName(d);
		} else {
			d->name = text;
		}

		/* Update the orders and depot */
		SetWindowClassesDirty(WindowClass::VehicleOrders);
		SetWindowDirty(WindowClass::VehicleDepot, d->xy.base());

		/* Update the depot list */
		VehicleType vt = GetDepotVehicleType(d->xy);
		SetWindowDirty(GetWindowClassForVehicleType(vt), VehicleListIdentifier(VehicleListType::Depot, vt, GetTileOwner(d->xy), d->index).ToWindowNumber());
	}
	return CommandCost();
}

CommandCost IsDepotBridgeAboveOK(TileIndex tile, TransportType depot_transport_type, DiagDirection dir, const BridgeAboveInfo &bridge_above)
{
	extern int GetBridgeTooLowHeightDifference(TileIndex tile, int height_clearance, int bridge_height);

	const int too_low = GetBridgeTooLowHeightDifference(tile, MINIMAL_DEPOT_BRIDGE_HEIGHT, bridge_above.height);
	if (too_low > 0) return CommandCostWithParam(STR_ERROR_BRIDGE_TOO_LOW_FOR_TRAIN_DEPOT + to_underlying(depot_transport_type), too_low);

	if (GetBridgeTilePillarFlags(tile, bridge_above) == 0) {
		return CommandCost();
	} else {
		return CommandCost(STR_ERROR_BRIDGE_PILLARS_OBSTRUCT_TRAIN_DEPOT + to_underlying(depot_transport_type));
	}
}

CommandCost IsExistingDepotBridgeAboveOK(TileIndex tile, const BridgeAboveInfo &bridge_above)
{
	TransportType depot_transport_type{};
	DiagDirection dir{};
	switch (GetTileType(tile)) {
		case TileType::Railway:
			depot_transport_type = TransportType::Rail;
			dir = GetRailDepotDirection(tile);
			break;

		case TileType::Road:
			depot_transport_type = TransportType::Road;
			dir = GetRoadDepotDirection(tile);
			break;

		case TileType::Water:
			depot_transport_type = TransportType::Water;
			dir = GetShipDepotDirection(tile);
			break;

		default:
			return CommandCost(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST);
	}
	return IsDepotBridgeAboveOK(tile, depot_transport_type, dir, bridge_above);
}
