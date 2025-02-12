/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_bridge.cpp Implementation of ScriptBridge. */

#include "../../stdafx.h"
#include "script_bridge.hpp"
#include "script_rail.hpp"
#include "../script_instance.hpp"
#include "../../bridge_map.h"
#include "../../strings_func.h"
#include "../../date_func.h"
#include "table/strings.h"

#include "../../safeguards.h"

/* static */ bool ScriptBridge::IsValidBridge(BridgeID bridge_id)
{
	if (bridge_id >= MAX_BRIDGES) return false;
	const BridgeSpec *b = ::GetBridgeSpec(bridge_id);
	return b->avail_year <= CalTime::CurYear() && !HasBit(b->ctrl_flags, BSCF_NOT_AVAILABLE_AI_GS);
}

/* static */ bool ScriptBridge::IsBridgeTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;
	return ::IsBridgeTile(tile);
}

/* static */ BridgeID ScriptBridge::GetBridgeID(TileIndex tile)
{
	if (!IsBridgeTile(tile)) return (BridgeID)-1;
	return (BridgeID)::GetBridgeType(tile);
}

/**
 * Helper function to connect a just built bridge to nearby roads.
 * @param instance The script instance we have to built the road for.
 */
static void _DoCommandReturnBuildBridge2(class ScriptInstance *instance)
{
	if (!ScriptBridge::_BuildBridgeRoad2()) {
		ScriptInstance::DoCommandReturn(instance);
		return;
	}

	/* This can never happen, as in test-mode this callback is never executed,
	 *  and in execute-mode, the other callback is called. */
	NOT_REACHED();
}

/**
 * Helper function to connect a just built bridge to nearby roads.
 * @param instance The script instance we have to built the road for.
 */
static void _DoCommandReturnBuildBridge1(class ScriptInstance *instance)
{
	if (!ScriptBridge::_BuildBridgeRoad1()) {
		ScriptInstance::DoCommandReturn(instance);
		return;
	}

	/* This can never happen, as in test-mode this callback is never executed,
	 *  and in execute-mode, the other callback is called. */
	NOT_REACHED();
}

/* static */ bool ScriptBridge::BuildBridge(ScriptVehicle::VehicleType vehicle_type, BridgeID bridge_id, TileIndex start, TileIndex end)
{
	EnforceDeityOrCompanyModeValid(false);
	EnforcePrecondition(false, start != end);
	EnforcePrecondition(false, ::IsValidTile(start) && ::IsValidTile(end));
	EnforcePrecondition(false, TileX(start) == TileX(end) || TileY(start) == TileY(end));
	EnforcePrecondition(false, vehicle_type == ScriptVehicle::VT_ROAD || vehicle_type == ScriptVehicle::VT_RAIL || vehicle_type == ScriptVehicle::VT_WATER);
	EnforcePrecondition(false, vehicle_type != ScriptVehicle::VT_RAIL || ScriptRail::IsRailTypeAvailable(ScriptRail::GetCurrentRailType()));
	EnforcePrecondition(false, vehicle_type != ScriptVehicle::VT_ROAD || ScriptRoad::IsRoadTypeAvailable(ScriptRoad::GetCurrentRoadType()));
	EnforcePrecondition(false, ScriptCompanyMode::IsValid() || vehicle_type == ScriptVehicle::VT_ROAD);

	uint type = (1 << 17);
	switch (vehicle_type) {
		case ScriptVehicle::VT_ROAD:
			type |= (TRANSPORT_ROAD << 15);
			type |= (ScriptRoad::GetCurrentRoadType() << 8);
			break;
		case ScriptVehicle::VT_RAIL:
			type |= (TRANSPORT_RAIL << 15);
			type |= (ScriptRail::GetCurrentRailType() << 8);
			break;
		case ScriptVehicle::VT_WATER:
			type |= (TRANSPORT_WATER << 15);
			break;
		default: NOT_REACHED();
	}

	/* For rail and water we do nothing special */
	if (vehicle_type == ScriptVehicle::VT_RAIL || vehicle_type == ScriptVehicle::VT_WATER) {
		return ScriptObject::DoCommand(end, start, type | bridge_id, CMD_BUILD_BRIDGE);
	}

	ScriptObject::SetCallbackVariable(0, start);
	ScriptObject::SetCallbackVariable(1, end);
	return ScriptObject::DoCommand(end, start, type | bridge_id, CMD_BUILD_BRIDGE, nullptr, &::_DoCommandReturnBuildBridge1);
}

/* static */ bool ScriptBridge::_BuildBridgeRoad1()
{
	EnforceDeityOrCompanyModeValid(false);

	/* Build the piece of road on the 'start' side of the bridge */
	TileIndex end = ScriptObject::GetCallbackVariable(0);
	TileIndex start = ScriptObject::GetCallbackVariable(1);

	DiagDirection dir_1 = ::DiagdirBetweenTiles(end, start);
	DiagDirection dir_2 = ::ReverseDiagDir(dir_1);

	return ScriptObject::DoCommand(start + ::TileOffsByDiagDir(dir_1), ::DiagDirToRoadBits(dir_2) | (ScriptRoad::GetCurrentRoadType() << 4) | (1 << 13), INVALID_TOWN, CMD_BUILD_ROAD, nullptr, &::_DoCommandReturnBuildBridge2);
}

/* static */ bool ScriptBridge::_BuildBridgeRoad2()
{
	EnforceDeityOrCompanyModeValid(false);

	/* Build the piece of road on the 'end' side of the bridge */
	TileIndex end = ScriptObject::GetCallbackVariable(0);
	TileIndex start = ScriptObject::GetCallbackVariable(1);

	DiagDirection dir_1 = ::DiagdirBetweenTiles(end, start);
	DiagDirection dir_2 = ::ReverseDiagDir(dir_1);

	return ScriptObject::DoCommand(end + ::TileOffsByDiagDir(dir_2), ::DiagDirToRoadBits(dir_1) | (ScriptRoad::GetCurrentRoadType() << 4) | (1 << 13), INVALID_TOWN, CMD_BUILD_ROAD);
}

/* static */ bool ScriptBridge::RemoveBridge(TileIndex tile)
{
	EnforceCompanyModeValid(false);
	EnforcePrecondition(false, IsBridgeTile(tile));
	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ std::optional<std::string> ScriptBridge::GetName(BridgeID bridge_id, ScriptVehicle::VehicleType vehicle_type)
{
	EnforcePrecondition(std::nullopt, vehicle_type == ScriptVehicle::VT_ROAD || vehicle_type == ScriptVehicle::VT_RAIL || vehicle_type == ScriptVehicle::VT_WATER);
	if (!IsValidBridge(bridge_id)) return std::nullopt;

	return GetString(vehicle_type == ScriptVehicle::VT_WATER ? STR_LAI_BRIDGE_DESCRIPTION_AQUEDUCT : ::GetBridgeSpec(bridge_id)->transport_name[vehicle_type]);
}

/* static */ SQInteger ScriptBridge::GetMaxSpeed(BridgeID bridge_id)
{
	if (!IsValidBridge(bridge_id)) return -1;

	return ::GetBridgeSpec(bridge_id)->speed; // km-ish/h
}

/* static */ Money ScriptBridge::GetPrice(BridgeID bridge_id, SQInteger length)
{
	if (!IsValidBridge(bridge_id)) return -1;

	length = Clamp<SQInteger>(length, 0, INT32_MAX);

	return ::CalcBridgeLenCostFactor(length) * _price[PR_BUILD_BRIDGE] * ::GetBridgeSpec(bridge_id)->price >> 8;
}

/* static */ SQInteger ScriptBridge::GetMaxLength(BridgeID bridge_id)
{
	if (!IsValidBridge(bridge_id)) return -1;

	return std::min<SQInteger>(::GetBridgeSpec(bridge_id)->max_length, _settings_game.construction.max_bridge_length) + 2;
}

/* static */ SQInteger ScriptBridge::GetMinLength(BridgeID bridge_id)
{
	if (!IsValidBridge(bridge_id)) return -1;

	return static_cast<SQInteger>(::GetBridgeSpec(bridge_id)->min_length) + 2;
}

/* static */ TileIndex ScriptBridge::GetOtherBridgeEnd(TileIndex tile)
{
	if (!::IsValidTile(tile)) return INVALID_TILE;
	if (!IsBridgeTile(tile)) return INVALID_TILE;

	return ::GetOtherBridgeEnd(tile);
}
