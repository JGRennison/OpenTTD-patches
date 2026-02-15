/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_map.cpp Complex road accessors. */

#include "stdafx.h"
#include "station_map.h"
#include "tunnelbridge_map.h"

#include "safeguards.h"


/**
 * Returns the RoadBits on an arbitrary tile
 * Special behaviour:
 * - road depots: entrance is treated as road piece
 * - road tunnels: entrance is treated as road piece
 * - bridge ramps: start of the ramp is treated as road piece
 * - bridge middle parts: bridge itself is ignored
 *
 * If straight_tunnel_bridge_entrance is set a ROAD_X or ROAD_Y
 * for bridge ramps and tunnel entrances is returned depending
 * on the orientation of the tunnel or bridge.
 * @param tile the tile to get the road bits for
 * @param rt   the road type to get the road bits form
 * @param straight_tunnel_bridge_entrance whether to return straight road bits for tunnels/bridges.
 * @return the road bits of the given tile
 */
RoadBits GetAnyRoadBits(TileIndex tile, RoadTramType rtt, bool straight_tunnel_bridge_entrance)
{
	if (!MayHaveRoad(tile) || !HasTileRoadType(tile, rtt)) return ROAD_NONE;

	switch (GetTileType(tile)) {
		case MP_ROAD:
			switch (GetRoadTileType(tile)) {
				default:
				case RoadTileType::Normal:   return GetRoadBits(tile, rtt);
				case RoadTileType::Crossing: return GetCrossingRoadBits(tile);
				case RoadTileType::Depot:    return DiagDirToRoadBits(GetRoadDepotDirection(tile));
			}

		case MP_STATION:
			dbg_assert(IsAnyRoadStopTile(tile)); // ensured by MayHaveRoad
			if (IsDriveThroughStopTile(tile)) return AxisToRoadBits(GetDriveThroughStopAxis(tile));
			return DiagDirToRoadBits(GetBayRoadStopDir(tile));

		case MP_TUNNELBRIDGE:
			dbg_assert(GetTunnelBridgeTransportType(tile) == TRANSPORT_ROAD); // ensured by MayHaveRoad
			if (IsRoadCustomBridgeHeadTile(tile)) return GetCustomBridgeHeadRoadBits(tile, rtt);
			return straight_tunnel_bridge_entrance ?
					AxisToRoadBits(DiagDirToAxis(GetTunnelBridgeDirection(tile))) :
					DiagDirToRoadBits(ReverseDiagDir(GetTunnelBridgeDirection(tile)));

		default: return ROAD_NONE;
	}
}
