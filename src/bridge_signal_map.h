/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_signal_map.h Map accessor functions for bridge signal simulation. */

#ifndef BRIDGE_SIGNAL_MAP_H
#define BRIDGE_SIGNAL_MAP_H

#include "tile_type.h"
#include "map_func.h"
#include "signal_type.h"
#include "core/bitmath_func.hpp"

#include <vector>
#include <unordered_map>

struct LongBridgeSignalStorage {
	std::vector<uint64> signal_red_bits;
};

extern std::unordered_map<TileIndex, LongBridgeSignalStorage> _long_bridge_signal_sim_map;

SignalState GetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16 signal);

enum {
	BRIDGE_M2_SIGNAL_STATE_COUNT      = 11,
	BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE = 12,
	BRIDGE_M2_SIGNAL_STATE_OFFSET     = 4,
	BRIDGE_M2_SIGNAL_STATE_EXT_FLAG   = 0x8000,
};

static inline SignalState GetBridgeEntranceSimulatedSignalState(TileIndex t, uint16 signal)
{
	if (signal < BRIDGE_M2_SIGNAL_STATE_COUNT) {
		return GB(_m[t].m2, signal + BRIDGE_M2_SIGNAL_STATE_OFFSET, 1) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
	} else {
		return GetBridgeEntranceSimulatedSignalStateExtended(t, signal);
	}
}

void SetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16 signal, SignalState state);

static inline void SetBridgeEntranceSimulatedSignalState(TileIndex t, uint16 signal, SignalState state)
{
	if (signal < BRIDGE_M2_SIGNAL_STATE_COUNT) {
		SB(_m[t].m2, signal + BRIDGE_M2_SIGNAL_STATE_OFFSET, 1, (state == SIGNAL_STATE_RED) ? 1 : 0);
	} else {
		SetBridgeEntranceSimulatedSignalStateExtended(t, signal, state);
	}
}

void SetAllBridgeEntranceSimulatedSignalsGreenExtended(TileIndex t);

static inline void SetAllBridgeEntranceSimulatedSignalsGreen(TileIndex t)
{
	if (_m[t].m2 & BRIDGE_M2_SIGNAL_STATE_EXT_FLAG) {
		SetAllBridgeEntranceSimulatedSignalsGreenExtended(t);
	} else {
		SB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE, 0);
	}
}

void ClearBridgeEntranceSimulatedSignalsExtended(TileIndex t);

static inline void ClearBridgeEntranceSimulatedSignals(TileIndex t)
{
	if (_m[t].m2 & BRIDGE_M2_SIGNAL_STATE_EXT_FLAG) {
		ClearBridgeEntranceSimulatedSignalsExtended(t);
	} else {
		SB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE, 0);
	}
}

void ClearBridgeSimulatedSignalMapping();

#endif /* BRIDGE_SIGNAL_MAP_H */
