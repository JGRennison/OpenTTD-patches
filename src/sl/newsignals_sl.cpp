/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newsignals_sl.cpp Code handling saving and loading of new signals */

#include "../stdafx.h"
#include "../newgrf_newsignals.h"

#include "saveload.h"

#include "../safeguards.h"

static void Save_NSID()
{
	SlSetLength(4 + (lengthof(_new_signal_style_mapping) * 5));
	SlWriteUint32(lengthof(_new_signal_style_mapping));
	for (const NewSignalStyleMapping &mapping : _new_signal_style_mapping) {
		SlWriteUint32(mapping.grfid);
		SlWriteByte(mapping.grf_local_id);
	}
}

static void Load_NSID()
{
	_new_signal_style_mapping.fill({});

	uint count = SlReadUint32();
	for (uint i = 0; i < count; i++) {
		NewSignalStyleMapping mapping;
		mapping.grfid = SlReadUint32();
		mapping.grf_local_id = SlReadByte();
		if (i < lengthof(_new_signal_style_mapping)) _new_signal_style_mapping[i] = mapping;
	}
}

static const ChunkHandler new_signal_chunk_handlers[] = {
	{ 'NSID', Save_NSID, Load_NSID, nullptr,   nullptr, CH_RIFF },
};

extern const ChunkHandlerTable _new_signal_chunk_handlers(new_signal_chunk_handlers);
