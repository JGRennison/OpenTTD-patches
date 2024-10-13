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


static const NamedSaveLoad _tunnel_desc[] = {
	NSL("tile_n",     SLE_VAR(Tunnel, tile_n, SLE_UINT32)),
	NSL("tile_s",     SLE_VAR(Tunnel, tile_s, SLE_UINT32)),
	NSL("height",     SLE_VAR(Tunnel, height, SLE_UINT8)),
	NSL("is_chunnel", SLE_VAR(Tunnel, is_chunnel, SLE_BOOL)),
	NSL("style",      SLE_CONDVAR_X(Tunnel, style_n, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEW_SIGNAL_STYLES, 1, 4))),
	NSL("style_n",    SLE_CONDVAR_X(Tunnel, style_n, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEW_SIGNAL_STYLES, 5))),
	NSL("style_s",    SLE_CONDVAR_X(Tunnel, style_s, SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_NEW_SIGNAL_STYLES, 5))),
};

static void Save_TUNN()
{
	SaveLoadTableData slt = SlTableHeader(_tunnel_desc);

	for (Tunnel *tunnel : Tunnel::Iterate()) {
		SlSetArrayIndex(tunnel->index);
		SlObjectSaveFiltered(tunnel, slt);
	}
}

static void Load_TUNN()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_tunnel_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		Tunnel *tunnel = new (index) Tunnel();
		SlObjectLoadFiltered(tunnel, slt);
		tunnel->UpdateIndexes();
	}
}


extern const ChunkHandler tunnel_chunk_handlers[] = {
	{ 'TUNN', Save_TUNN, Load_TUNN, nullptr, nullptr, CH_TABLE },
};

extern const ChunkHandlerTable _tunnel_chunk_handlers(tunnel_chunk_handlers);
