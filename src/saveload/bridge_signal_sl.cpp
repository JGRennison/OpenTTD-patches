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

/** stub save header struct */
struct LongBridgeSignalStorageStub {
	uint32 length;
};

static const SaveLoad _long_bridge_signal_storage_stub_desc[] = {
	SLE_VAR(LongBridgeSignalStorageStub, length, SLE_UINT32),
	SLE_END()
};

static void Load_XBSS()
{
	int index;
	LongBridgeSignalStorageStub stub;
	while ((index = SlIterateArray()) != -1) {
		LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[index];
		SlObject(&stub, _long_bridge_signal_storage_stub_desc);
		lbss.signal_red_bits.resize(stub.length);
		SlArray(&(lbss.signal_red_bits[0]), stub.length, SLE_UINT64);
	}
}

static void RealSave_XBSS(const LongBridgeSignalStorage *lbss)
{
	LongBridgeSignalStorageStub stub;
	assert(lbss->signal_red_bits.size() <= std::numeric_limits<decltype(stub.length)>::max());
	stub.length = static_cast<decltype(stub.length)>(lbss->signal_red_bits.size());
	SlObject(&stub, _long_bridge_signal_storage_stub_desc);
	SlArray(const_cast<uint64*>(&(lbss->signal_red_bits[0])), stub.length, SLE_UINT64);
}

static void Save_XBSS()
{
	for (const auto &it : _long_bridge_signal_sim_map) {
		const LongBridgeSignalStorage &lbss = it.second;
		SlSetArrayIndex(it.first);
		SlAutolength((AutolengthProc*) RealSave_XBSS, const_cast<LongBridgeSignalStorage*>(&lbss));
	}
}

extern const ChunkHandler _bridge_signal_chunk_handlers[] = {
	{ 'XBSS', Save_XBSS, Load_XBSS, nullptr, nullptr, CH_SPARSE_ARRAY | CH_LAST},
};
