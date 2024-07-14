/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file animated_tile_sl.cpp Code handling saving and loading of animated tiles */

#include "../stdafx.h"
#include "../animated_tile.h"
#include "../tile_type.h"
#include "../core/alloc_func.hpp"

#include "saveload.h"

#include "../safeguards.h"

using AnimatedTilePair = std::pair<const TileIndex, AnimatedTileInfo>;

struct AnimatedTileStructHandler : public SaveLoadStructHandler {
	NamedSaveLoadTable GetDescription() const override
	{
		static const NamedSaveLoad _anim_tiles_sub_desc[] = {
			NSLT("tile",     SLTAG(SLTAG_CUSTOM_START,     SLE_VAR(AnimatedTilePair, first, SLE_UINT32))),
			NSLT("speed",    SLTAG(SLTAG_CUSTOM_START + 1, SLE_VAR(AnimatedTilePair, second.speed, SLE_UINT8))),
		};
		return _anim_tiles_sub_desc;
	}

	void Save([[maybe_unused]] void *object) const override
	{
		uint count = 0;
		for (const auto &it : _animated_tiles) {
			if (!it.second.pending_deletion) count++;
		}
		SlSetStructListLength(count);
		for (const auto &it : _animated_tiles) {
			if (it.second.pending_deletion) continue;
			SlWriteUint32(it.first);
			SlWriteByte(it.second.speed);
		}
	}

	void Load([[maybe_unused]] void *object) const override
	{
		auto slt = this->GetLoadDescription();
		if (slt.size() != 2 || slt[0].label_tag != SLTAG_CUSTOM_START || slt[1].label_tag != SLTAG_CUSTOM_START + 1) {
			SlErrorCorrupt("Table format ANIT chunk fields not as expected");
		}

		size_t count = SlGetStructListLength(MAX_MAP_TILES);
		for (size_t i = 0; i < count; i++) {
			TileIndex tile = SlReadUint32();
			AnimatedTileInfo info = {};
			info.speed = SlReadByte();
			_animated_tiles[tile] = info;
		}
	}
};

static const NamedSaveLoad _anim_tiles_desc[] = {
	NSLT_STRUCTLIST<AnimatedTileStructHandler>("animated_tiles"),
};

/**
 * Save the ANIT chunk.
 */
static void Save_ANIT()
{
	SlSaveTableObjectChunk(_anim_tiles_desc);
}

/**
 * Load the ANIT chunk; the chunk containing the animated tiles.
 */
static void Load_ANIT()
{
	_animated_tiles.clear();

	if (SlIsTableChunk()) {
		if (SlXvIsFeatureMissing(XSLFI_ANIMATED_TILE_EXTRA, 2)) SlErrorCorrupt("Table format ANIT chunk without XSLFI_ANIMATED_TILE_EXTRA v2");
		SlLoadTableObjectChunk(_anim_tiles_desc);
		return;
	}

	/* Before version 80 we did NOT have a variable length animated tile table */
	if (IsSavegameVersionBefore(SLV_80)) {
		/* In pre version 6, we has 16bit per tile, now we have 32bit per tile, convert it ;) */
		TileIndex anim_list[256];
		SlArray(anim_list, 256, IsSavegameVersionBefore(SLV_6) ? (SLE_FILE_U16 | SLE_VAR_U32) : SLE_UINT32);

		for (int i = 0; i < 256; i++) {
			if (anim_list[i] == 0) break;
			_animated_tiles[anim_list[i]] = {};
		}
		return;
	}

	if (SlXvIsFeaturePresent(XSLFI_ANIMATED_TILE_EXTRA)) {
		uint count = (uint)SlGetFieldLength() / 5;
		for (uint i = 0; i < count; i++) {
			TileIndex tile = SlReadUint32();
			AnimatedTileInfo info = {};
			info.speed = SlReadByte();
			_animated_tiles[tile] = info;
		}
	} else {
		uint count = (uint)SlGetFieldLength() / 4;
		for (uint i = 0; i < count; i++) {
			_animated_tiles[SlReadUint32()] = {};
		}
	}
}

/**
 * "Definition" imported by the saveload code to be able to load and save
 * the animated tile table.
 */
static const ChunkHandler animated_tile_chunk_handlers[] = {
	{ 'ANIT', Save_ANIT, Load_ANIT, nullptr, nullptr, CH_TABLE },
};

extern const ChunkHandlerTable _animated_tile_chunk_handlers(animated_tile_chunk_handlers);
