/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tunnel_sl.cpp Code handling saving and loading of tunnels */

#include "../stdafx.h"
#include "../tunnel_base.h"

#include "saveload.h"

#include "../safeguards.h"


static const SaveLoad _tunnel_desc[] = {
	 SLE_CONDVAR(Tunnel, tile_n,           SLE_UINT32,             SL_MIN_VERSION, SL_MAX_VERSION),
	 SLE_CONDVAR(Tunnel, tile_s,           SLE_UINT32,             SL_MIN_VERSION, SL_MAX_VERSION),
	 SLE_CONDVAR(Tunnel, height,            SLE_UINT8,             SL_MIN_VERSION, SL_MAX_VERSION),
	 SLE_CONDVAR(Tunnel, is_chunnel,         SLE_BOOL,             SL_MIN_VERSION, SL_MAX_VERSION),
	SLE_CONDVAR_X(Tunnel, style,            SLE_UINT8,             SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEW_SIGNAL_STYLES)),
};

static void Save_TUNN()
{
	for (Tunnel *tunnel : Tunnel::Iterate()) {
		SlSetArrayIndex(tunnel->index);
		SlObject(tunnel, _tunnel_desc);
	}
}

static void Load_TUNN()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		Tunnel *tunnel = new (index) Tunnel();
		SlObject(tunnel, _tunnel_desc);
		tunnel->UpdateIndexes();
	}
}


extern const ChunkHandler tunnel_chunk_handlers[] = {
	{ 'TUNN', Save_TUNN, Load_TUNN, nullptr, nullptr, CH_ARRAY },
};

extern const ChunkHandlerTable _tunnel_chunk_handlers(tunnel_chunk_handlers);
