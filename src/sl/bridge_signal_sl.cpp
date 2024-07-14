/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bridge_signal_sl.cpp Code handling saving and loading of data for signal on bridges */

#include "../stdafx.h"
#include "../bridge_signal_map.h"
#include "saveload.h"
#include <vector>

static const NamedSaveLoad _long_bridge_signal_storage_desc[] = {
	NSL("signal_red_bits", SLE_VARVEC(LongBridgeSignalStorage, signal_red_bits, SLE_UINT64)),
};

static void Load_XBSS()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_long_bridge_signal_storage_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[index];
		SlObjectLoadFiltered(&lbss, slt);
	}
}

static void Save_XBSS()
{
	SaveLoadTableData slt = SlTableHeader(_long_bridge_signal_storage_desc);

	for (auto &it : _long_bridge_signal_sim_map) {
		LongBridgeSignalStorage &lbss = it.second;
		SlSetArrayIndex(it.first);
		SlObjectSaveFiltered(&lbss, slt);
	}
}

static void Load_XBST()
{
	size_t count = SlGetFieldLength() / sizeof(uint32_t);
	for (size_t i = 0; i < count; i++) {
		_bridge_signal_style_map.insert(SlReadUint32());
	}
}

static void Save_XBST()
{
	SlSetLength(_bridge_signal_style_map.size() * sizeof(uint32_t));
	for (uint32_t val : _bridge_signal_style_map) {
		SlWriteUint32(val);
	}
}

extern const ChunkHandler bridge_signal_chunk_handlers[] = {
	{ 'XBSS', Save_XBSS, Load_XBSS, nullptr, nullptr, CH_SPARSE_TABLE },
	{ 'XBST', Save_XBST, Load_XBST, nullptr, nullptr, CH_RIFF },
};

extern const ChunkHandlerTable _bridge_signal_chunk_handlers(bridge_signal_chunk_handlers);
