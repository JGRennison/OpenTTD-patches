/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file animated_tile_sl.cpp Code handling saving and loading of animated tiles */

#include "../stdafx.h"

#include "saveload.h"
#include "compat/animated_tile_sl_compat.h"

#include "../tile_type.h"
#include "../animated_tile.h"
#include "../core/alloc_func.hpp"

#include "../safeguards.h"

namespace upstream_sl {

static std::vector <TileIndex> _tmp_animated_tiles;

static const SaveLoad _animated_tile_desc[] = {
	 SLEG_VECTOR("tiles", _tmp_animated_tiles, SLE_UINT32),
};

struct ANITChunkHandler : ChunkHandler {
	ANITChunkHandler() : ChunkHandler('ANIT', CH_TABLE) {}

	void Save() const override
	{
		// removed
		NOT_REACHED();
	}

	void Load() const override
	{
		/* Before version 80 we did NOT have a variable length animated tile table */
		if (IsSavegameVersionBefore(SLV_80)) {
			/* In pre version 6, we has 16bit per tile, now we have 32bit per tile, convert it ;) */
			TileIndex anim_list[256];
			SlCopy(anim_list, 256, IsSavegameVersionBefore(SLV_6) ? (SLE_FILE_U16 | SLE_VAR_U32) : SLE_UINT32);

			for (int i = 0; i < 256; i++) {
				if (anim_list[i] == 0) break;
				_animated_tiles[anim_list[i]] = {};
			}
			return;
		}

		if (IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY)) {
			size_t count = SlGetFieldLength() / sizeof(uint32_t);
			_animated_tiles.clear();
			for (uint i = 0; i < count; i++) {
				_animated_tiles[TileIndex(SlReadUint32())] = {};
			}
			return;
		}

		const std::vector<SaveLoad> slt = SlCompatTableHeader(_animated_tile_desc, _animated_tile_sl_compat);

		if (SlIterateArray() == -1) return;
		SlGlobList(slt);
		if (SlIterateArray() != -1) SlErrorCorrupt("Too many ANIT entries");

		for (TileIndex t : _tmp_animated_tiles) {
			_animated_tiles[t] = {};
		}
		_tmp_animated_tiles.clear();
	}
};


static const ANITChunkHandler ANIT;
static const ChunkHandlerRef animated_tile_chunk_handlers[] = {
	ANIT,
};

extern const ChunkHandlerTable _animated_tile_chunk_handlers(animated_tile_chunk_handlers);

}
