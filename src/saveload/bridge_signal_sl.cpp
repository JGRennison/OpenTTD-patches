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
};

static void Load_XBSS()
{
	int index;
	LongBridgeSignalStorageStub stub;
	while ((index = SlIterateArray()) != -1) {
		LongBridgeSignalStorage &lbss = _long_bridge_signal_sim_map[index];
		SlObject(&stub, _long_bridge_signal_storage_stub_desc);
		lbss.signal_red_bits.resize(stub.length);
		SlArray(lbss.signal_red_bits.data(), stub.length, SLE_UINT64);
	}
}

static void RealSave_XBSS(const LongBridgeSignalStorage *lbss)
{
	LongBridgeSignalStorageStub stub;
	stub.length = (uint32)lbss->signal_red_bits.size();
	SlObject(&stub, _long_bridge_signal_storage_stub_desc);
	SlArray(const_cast<uint64*>(lbss->signal_red_bits.data()), stub.length, SLE_UINT64);
}

static void Save_XBSS()
{
	for (const auto &it : _long_bridge_signal_sim_map) {
		const LongBridgeSignalStorage &lbss = it.second;
		SlSetArrayIndex(it.first);
		SlAutolength((AutolengthProc*) RealSave_XBSS, const_cast<LongBridgeSignalStorage*>(&lbss));
	}
}

static void Load_XBST()
{
	size_t count = SlGetFieldLength() / sizeof(uint32);
	for (size_t i = 0; i < count; i++) {
		_bridge_signal_style_map.insert(SlReadUint32());
	}
}

static void Save_XBST()
{
	SlSetLength(_bridge_signal_style_map.size() * sizeof(uint32));
	for (uint32 val : _bridge_signal_style_map) {
		SlWriteUint32(val);
	}
}

extern const ChunkHandler bridge_signal_chunk_handlers[] = {
	{ 'XBSS', Save_XBSS, Load_XBSS, nullptr, nullptr, CH_SPARSE_ARRAY },
	{ 'XBST', Save_XBST, Load_XBST, nullptr, nullptr, CH_RIFF },
};

extern const ChunkHandlerTable _bridge_signal_chunk_handlers(bridge_signal_chunk_handlers);
