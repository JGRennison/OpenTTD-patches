/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.cpp Map accessor functions for bridges. */

#include "stdafx.h"
#include "landscape.h"
#include "tunnelbridge_map.h"
#include "bridge_signal_map.h"
#include "debug.h"

#include "safeguards.h"


/**
 * Finds the end of a bridge in the specified direction starting at a middle tile
 * @param tile the bridge tile to find the bridge ramp for
 * @param dir  the direction to search in
 */
static TileIndex GetBridgeEnd(TileIndex tile, DiagDirection dir)
{
	TileIndexDiff delta = TileOffsByDiagDir(dir);

	dir = ReverseDiagDir(dir);
	do {
		tile += delta;
	} while (!IsBridgeTile(tile) || GetTunnelBridgeDirection(tile) != dir);

	return tile;
}


/**
 * Finds the northern end of a bridge starting at a middle tile
 * @param t the bridge tile to find the bridge ramp for
 */
TileIndex GetNorthernBridgeEnd(TileIndex t)
{
	return GetBridgeEnd(t, ReverseDiagDir(AxisToDiagDir(GetBridgeAxis(t))));
}


/**
 * Finds the southern end of a bridge starting at a middle tile
 * @param t the bridge tile to find the bridge ramp for
 */
TileIndex GetSouthernBridgeEnd(TileIndex t)
{
	return GetBridgeEnd(t, AxisToDiagDir(GetBridgeAxis(t)));
}


/**
 * Starting at one bridge end finds the other bridge end
 * @param t the bridge ramp tile to find the other bridge ramp for
 */
TileIndex GetOtherBridgeEnd(TileIndex tile)
{
	assert(IsBridgeTile(tile));
	return GetBridgeEnd(tile, GetTunnelBridgeDirection(tile));
}

/**
 * Get the height ('z') of a bridge.
 * @param tile the bridge ramp tile to get the bridge height from
 * @return the height of the bridge.
 */
int GetBridgeHeight(TileIndex t)
{
	int h;
	Slope tileh = GetTileSlope(t, &h);
	Foundation f = GetBridgeFoundation(tileh, DiagDirToAxis(GetTunnelBridgeDirection(t)));

	/* one height level extra for the ramp */
	return h + 1 + ApplyFoundationToSlope(f, &tileh);
}

std::unordered_map<TileIndex, LongBridgeSignalStorage> _long_bridge_signal_sim_map;

SignalState GetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16 signal)
{
	const auto it = _long_bridge_signal_sim_map.find(t);
	if (it != _long_bridge_signal_sim_map.end()) {
		const LongBridgeSignalStorage &lbss = it->second;
		uint16 offset = signal - 15;
		uint16 slot = offset >> 6;
		uint16 bit = offset & 0x3F;
		if (slot >= lbss.signal_red_bits.size()) return SIGNAL_STATE_GREEN;
		return GB(lbss.signal_red_bits[slot], bit, 1) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
	} else {
		return SIGNAL_STATE_GREEN;
	}
}

void SetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16 signal, SignalState state)
{
	LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[t];
	uint16 offset = signal - 15;
	uint16 slot = offset >> 6;
	uint16 bit = offset & 0x3F;
	if (slot >= lbss.signal_red_bits.size()) lbss.signal_red_bits.resize(slot + 1);
	SB(lbss.signal_red_bits[slot], bit, 1, (uint64) ((state == SIGNAL_STATE_RED) ? 1 : 0));
	_m[t].m2 |= 0x8000;
}

void SetAllBridgeEntranceSimulatedSignalsGreenExtended(TileIndex t)
{
	auto it = _long_bridge_signal_sim_map.find(t);
	if (it != _long_bridge_signal_sim_map.end()) {
		LongBridgeSignalStorage &lbss = it->second;
		for (auto &it : lbss.signal_red_bits) {
			it = 0;
		}
		_m[t].m2 = 0x8000;
	} else {
		_m[t].m2 = 0;
	}
}

void ClearBridgeEntranceSimulatedSignalsExtended(TileIndex t)
{
	_long_bridge_signal_sim_map.erase(t);
	_m[t].m2 = 0;
}

void ClearBridgeSimulatedSignalMapping()
{
	_long_bridge_signal_sim_map.clear();
}
