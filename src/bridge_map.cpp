/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_map.cpp Map accessor functions for bridges. */

#include "stdafx.h"
#include "landscape.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "bridge_signal_map.h"
#include "debug.h"
#include "newgrf_newsignals.h"

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
 * @param tile the bridge ramp tile to find the other bridge ramp for
 */
TileIndex GetOtherBridgeEnd(TileIndex tile)
{
	assert_tile(IsBridgeTile(tile), tile);
	return GetBridgeEnd(tile, GetTunnelBridgeDirection(tile));
}

/**
 * Get the height ('z') of a bridge.
 * @param t the bridge ramp tile to get the bridge height from
 * @return the height of the bridge.
 */
int GetBridgeHeight(TileIndex t)
{
	auto [tileh, h] = GetTileSlopeZ(t);
	Foundation f = GetBridgeFoundation(tileh, DiagDirToAxis(GetTunnelBridgeDirection(t)));

	/* one height level extra for the ramp */
	return h + 1 + ApplyFoundationToSlope(f, tileh);
}

robin_hood::unordered_flat_map<TileIndex, LongBridgeSignalStorage> _long_bridge_signal_sim_map;

SignalState GetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16_t signal)
{
	const auto it = _long_bridge_signal_sim_map.find(t);
	if (it != _long_bridge_signal_sim_map.end()) {
		const LongBridgeSignalStorage &lbss = it->second;
		uint16_t offset = signal - BRIDGE_M2_SIGNAL_STATE_COUNT;
		uint16_t slot = offset >> 6;
		uint16_t bit = offset & 0x3F;
		if (slot >= lbss.signal_red_bits.size()) return SIGNAL_STATE_GREEN;
		return GB(lbss.signal_red_bits[slot], bit, 1) ? SIGNAL_STATE_RED : SIGNAL_STATE_GREEN;
	} else {
		return SIGNAL_STATE_GREEN;
	}
}

void SetBridgeEntranceSimulatedSignalStateExtended(TileIndex t, uint16_t signal, SignalState state)
{
	LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[t];
	uint16_t offset = signal - BRIDGE_M2_SIGNAL_STATE_COUNT;
	uint16_t slot = offset >> 6;
	uint16_t bit = offset & 0x3F;
	if (slot >= lbss.signal_red_bits.size()) lbss.signal_red_bits.resize(slot + 1);
	AssignBit(lbss.signal_red_bits[slot], bit, state == SIGNAL_STATE_RED);
	_m[t].m2 |= BRIDGE_M2_SIGNAL_STATE_EXT_FLAG;
}

bool SetAllBridgeEntranceSimulatedSignalsGreenExtended(TileIndex t)
{
	bool changed = GB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_COUNT) != 0;
	SB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE, 0);
	auto it = _long_bridge_signal_sim_map.find(t);
	if (it != _long_bridge_signal_sim_map.end()) {
		LongBridgeSignalStorage &lbss = it->second;
		for (auto &it : lbss.signal_red_bits) {
			if (it != 0) {
				changed = true;
				it = 0;
			}
		}
		_m[t].m2 |= BRIDGE_M2_SIGNAL_STATE_EXT_FLAG;
	}
	return changed;
}

void SetAllBridgeEntranceSimulatedSignalsRed(TileIndex t, TileIndex other_end)
{
	_m[t].m2 |= GetBitMaskSC<uint16_t>(BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_COUNT);

	const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(t);
	const uint bridge_length = GetTunnelBridgeLength(t, other_end);
	const uint signal_count = bridge_length / simulated_wormhole_signals;

	if (signal_count <= BRIDGE_M2_SIGNAL_STATE_COUNT) return;

	_m[t].m2 |= BRIDGE_M2_SIGNAL_STATE_EXT_FLAG;
	LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[t];
	lbss.signal_red_bits.assign(CeilDiv(signal_count - BRIDGE_M2_SIGNAL_STATE_COUNT, 64), UINT64_MAX);
}

void ClearBridgeEntranceSimulatedSignalsExtended(TileIndex t)
{
	_long_bridge_signal_sim_map.erase(t);
	SB(_m[t].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE, 0);
}

void ShiftBridgeEntranceSimulatedSignalsExtended(TileIndex t, int shift, uint64_t in)
{
	if (shift > 0) {
		/* shift into array */
		LongBridgeSignalStorage *lbss = nullptr;
		auto it = _long_bridge_signal_sim_map.find(t);
		if (it != _long_bridge_signal_sim_map.end()) {
			lbss = &(it->second);
		} else if (in) {
			lbss = &(_long_bridge_signal_sim_map[t]);
		} else {
			return;
		}
		const size_t orig_size = lbss->signal_red_bits.size();
		size_t i = orig_size;
		auto insert_bits = [&](uint64_t bits, size_t pos) {
			if (bits) {
				if (pos >= lbss->signal_red_bits.size()) lbss->signal_red_bits.resize(pos + 1);
				lbss->signal_red_bits[pos] |= bits;
			}
		};
		while (i) {
			i--;
			uint64_t out = GB(lbss->signal_red_bits[i], 64 - shift, shift);
			lbss->signal_red_bits[i] <<= shift;
			insert_bits(out, i + 1);
		}
		insert_bits(in, 0);
	} else if (shift < 0) {
		/* not implemented yet */
		NOT_REACHED();
	}
}

void ClearBridgeSimulatedSignalMapping()
{
	_long_bridge_signal_sim_map.clear();
}

btree::btree_set<uint32_t> _bridge_signal_style_map;
static_assert(MAX_MAP_TILES_BITS + 4 <= 32);
static_assert(1 << 4 <= MAX_NEW_SIGNAL_STYLES + 1);

void SetBridgeSignalStyle(TileIndex t, uint8_t style)
{
	if (style == 0) {
		/* No style allocated before */
		if (!HasBit(_m[t].m3, 7)) return;

		auto iter = _bridge_signal_style_map.lower_bound(t.base() << 4);
		if (iter != _bridge_signal_style_map.end() && *iter >> 4 == t) _bridge_signal_style_map.erase(iter);
		ClrBit(_m[t].m3, 7);
	} else {
		auto iter = _bridge_signal_style_map.lower_bound(t.base() << 4);
		if (iter != _bridge_signal_style_map.end() && *iter >> 4 == t) iter = _bridge_signal_style_map.erase(iter);
		_bridge_signal_style_map.insert(iter, (t.base() << 4) | style);
		SetBit(_m[t].m3, 7);
	}
}

uint8_t GetBridgeSignalStyleExtended(TileIndex t)
{
	auto iter = _bridge_signal_style_map.lower_bound(t.base() << 4);
	if (iter != _bridge_signal_style_map.end() && *iter >> 4 == t) return (*iter) & 0xF;
	return 0;
}

void ClearBridgeSignalStyleMapping()
{
	_bridge_signal_style_map.clear();
}
