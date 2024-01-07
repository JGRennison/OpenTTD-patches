/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spritecache.h Functions to cache sprites in memory. */

#ifndef SPRITECACHE_H
#define SPRITECACHE_H

#include "gfx_type.h"
#include "zoom_type.h"
#include "spriteloader/spriteloader.hpp"
#include "3rdparty/cpp-btree/btree_map.h"

/** Data structure describing a sprite. */
struct Sprite {
	uint32_t size;               ///< Size of the allocation for this sprite structure
	uint16_t height;             ///< Height of the sprite.
	uint16_t width;              ///< Width of the sprite.
	int16_t x_offs;              ///< Number of pixels to shift the sprite to the right.
	int16_t y_offs;              ///< Number of pixels to shift the sprite downwards.
	uint32_t lru;                ///< Sprite cache LRU of this sprite structure.
	uint8_t missing_zoom_levels; ///< Bitmask of zoom levels missing in data
	Sprite *next = nullptr;      ///< Next sprite structure, this is the only member which may be changed after the sprite has been inserted in the sprite cache
	byte data[];                 ///< Sprite data.
};

/*
 * Allow skipping sprites with zoom < ZOOM_LVL_OUT_4X, for sprite min zoom setting at 1x, if ZOOM_LVL_OUT_4X bit of present zoom levels is set.
 * Allow skipping sprites with zoom < ZOOM_LVL_OUT_2X, for sprite min zoom setting at 2x, if either ZOOM_LVL_OUT_4X or ZOOM_LVL_OUT_2X bits of present zoom levels are set.
 */
enum SpriteCacheCtrlFlags {
	SCC_PAL_ZOOM_START            =  0, ///< Start bit of present zoom levels in palette mode.
	SCC_32BPP_ZOOM_START          =  6, ///< Start bit of present zoom levels in 32bpp mode.
	SCCF_WARNED                   = 12, ///< True iff the user has been warned about incorrect use of this sprite.
};

extern uint _sprite_cache_size;

typedef void *AllocatorProc(size_t size);

void *SimpleSpriteAlloc(size_t size);
void *GetRawSprite(SpriteID sprite, SpriteType type, uint8_t zoom_levels, AllocatorProc *allocator = nullptr, SpriteEncoder *encoder = nullptr);
bool SpriteExists(SpriteID sprite);

SpriteType GetSpriteType(SpriteID sprite);
SpriteFile *GetOriginFile(SpriteID sprite);
uint32_t GetSpriteLocalID(SpriteID sprite);
uint GetSpriteCountForFile(const std::string &filename, SpriteID begin, SpriteID end);
uint GetMaxSpriteID();

inline const Sprite *GetSprite(SpriteID sprite, SpriteType type, uint8_t zoom_levels)
{
	dbg_assert(type != SpriteType::Recolour);
	return (Sprite*)GetRawSprite(sprite, type, zoom_levels);
}

inline const byte *GetNonSprite(SpriteID sprite, SpriteType type)
{
	dbg_assert(type == SpriteType::Recolour);
	return (byte*)GetRawSprite(sprite, type, UINT8_MAX);
}

void GfxInitSpriteMem();
void GfxClearSpriteCache();
void GfxClearFontSpriteCache();
void IncreaseSpriteLRU();

SpriteFile &OpenCachedSpriteFile(const std::string &filename, Subdirectory subdir, bool palette_remap);

void ReadGRFSpriteOffsets(SpriteFile &file);
size_t GetGRFSpriteOffset(uint32_t id);
bool LoadNextSprite(int load_index, SpriteFile &file, uint file_sprite_id);
bool SkipSpriteData(SpriteFile &file, byte type, uint16_t num);
void DupSprite(SpriteID old_spr, SpriteID new_spr);

uint32_t GetSpriteMainColour(SpriteID sprite_id, PaletteID palette_id);

struct SpritePointerHolder {
private:
	btree::btree_map<uint32_t, const void *> cache;

public:
	inline const Sprite *GetSprite(SpriteID sprite, SpriteType type) const
	{
		return (const Sprite*)(this->cache.find(sprite | (static_cast<uint32_t>(type) << 29))->second);
	}

	inline const byte *GetRecolourSprite(SpriteID sprite) const
	{
		return (const byte*)(this->cache.find(sprite | (static_cast<uint32_t>(SpriteType::Recolour) << 29))->second);
	}

	void Clear()
	{
		this->cache.clear();
	}

	inline void CacheSprite(SpriteID sprite, SpriteType type, ZoomLevel zoom_level)
	{
		this->cache[sprite | (static_cast<uint32_t>(type) << 29)] = GetRawSprite(sprite, type, ZoomMask(zoom_level));
	}

	inline void CacheRecolourSprite(SpriteID sprite)
	{
		this->cache[sprite | (static_cast<uint32_t>(SpriteType::Recolour) << 29)] = GetRawSprite(sprite, SpriteType::Recolour, 0);
	}
};

#endif /* SPRITECACHE_H */
