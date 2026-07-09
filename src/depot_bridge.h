/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file depot_bridge.h Functions related to bridging over depots. */

#ifndef DEPOT_BRIDGE_H
#define DEPOT_BRIDGE_H

#include "bridge.h"
#include "direction_type.h"
#include "transport_type.h"

CommandCost IsDepotBridgeAboveOK(TileIndex tile, TransportType depot_transport_type, DiagDirection dir, const BridgeAboveInfo &bridge_above);

CommandCost IsExistingDepotBridgeAboveOK(TileIndex tile, const BridgeAboveInfo &bridge_above);

#endif /* DEPOT_BRIDGE_H */
