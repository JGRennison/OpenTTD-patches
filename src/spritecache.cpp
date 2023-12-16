/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spritecache.cpp Caching of sprites. */

#include "stdafx.h"
#include "random_access_file_type.h"
#include "spriteloader/grf.hpp"
#include "gfx_func.h"
#include "error.h"
#include "zoom_func.h"
#include "settings_type.h"
#include "blitter/factory.hpp"
#include "core/alloc_func.hpp"
#include "core/math_func.hpp"
#include "core/mem_func.hpp"
#include "video/video_driver.hpp"
#include "scope_info.h"
#include "spritecache.h"
#include "spritecache_internal.h"

#include "table/sprites.h"
#include "table/strings.h"
#include "table/palette_convert.h"

#include "3rdparty/cpp-btree/btree_map.h"

#include <vector>
#include <algorithm>

#include "safeguards.h"

/* Default of 4MB spritecache */
uint _sprite_cache_size = 4;

size_t _spritecache_bytes_used = 0;
static uint32 _sprite_lru_counter;
static uint32 _spritecache_prune_events = 0;
static size_t _spritecache_prune_entries = 0;
static size_t _spritecache_prune_total = 0;

static std::vector<SpriteCache> _spritecache;
static SpriteDataBuffer _last_sprite_allocation;
static std::vector<std::unique_ptr<SpriteFile>> _sprite_files;

static inline SpriteCache *GetSpriteCache(uint index)
{
	return &_spritecache[index];
}

SpriteCache *AllocateSpriteCache(uint index)
{
	if (index >= _spritecache.size()) {
		_spritecache.resize(index + 1);
	}

	return GetSpriteCache(index);
}

/**
 * Get the cached SpriteFile given the name of the file.
 * @param filename The name of the file at the disk.
 * @return The SpriteFile or \c null.
 */
static SpriteFile *GetCachedSpriteFileByName(const std::string &filename)
{
	for (auto &f : _sprite_files) {
		if (f->GetFilename() == filename) {
			return f.get();
		}
	}
	return nullptr;
}

/**
 * Open/get the SpriteFile that is cached for use in the sprite cache.
 * @param filename      Name of the file at the disk.
 * @param subdir        The sub directory to search this file in.
 * @param palette_remap Whether a palette remap needs to be performed for this file.
 * @return The reference to the SpriteCache.
 */
SpriteFile &OpenCachedSpriteFile(const std::string &filename, Subdirectory subdir, bool palette_remap)
{
	SpriteFile *file = GetCachedSpriteFileByName(filename);
	if (file == nullptr) {
		file = _sprite_files.insert(std::end(_sprite_files), std::make_unique<SpriteFile>(filename, subdir, palette_remap))->get();
	} else {
		file->SeekToBegin();
	}
	return *file;
}

static void *AllocSprite(size_t mem_req);

/**
 * Skip the given amount of sprite graphics data.
 * @param type the type of sprite (compressed etc)
 * @param num the amount of sprites to skip
 * @return true if the data could be correctly skipped.
 */
bool SkipSpriteData(SpriteFile &file, byte type, uint16 num)
{
	if (type & 2) {
		file.SkipBytes(num);
	} else {
		while (num > 0) {
			int8 i = file.ReadByte();
			if (i >= 0) {
				int size = (i == 0) ? 0x80 : i;
				if (size > num) return false;
				num -= size;
				file.SkipBytes(size);
			} else {
				i = -(i >> 3);
				num -= i;
				file.ReadByte();
			}
		}
	}
	return true;
}

/* Check if the given Sprite ID exists */
bool SpriteExists(SpriteID id)
{
	if (id >= _spritecache.size()) return false;

	/* Special case for Sprite ID zero -- its position is also 0... */
	if (id == 0) return true;
	return !(GetSpriteCache(id)->file_pos == 0 && GetSpriteCache(id)->file == nullptr);
}

/**
 * Get the sprite type of a given sprite.
 * @param sprite The sprite to look at.
 * @return the type of sprite.
 */
SpriteType GetSpriteType(SpriteID sprite)
{
	if (!SpriteExists(sprite)) return SpriteType::Invalid;
	return GetSpriteCache(sprite)->GetType();
}

/**
 * Get the SpriteFile of a given sprite.
 * @param sprite The sprite to look at.
 * @return The SpriteFile.
 */
SpriteFile *GetOriginFile(SpriteID sprite)
{
	if (!SpriteExists(sprite)) return nullptr;
	return GetSpriteCache(sprite)->file;
}

/**
 * Get the GRF-local sprite id of a given sprite.
 * @param sprite The sprite to look at.
 * @return The GRF-local sprite id.
 */
uint32 GetSpriteLocalID(SpriteID sprite)
{
	if (!SpriteExists(sprite)) return 0;
	return GetSpriteCache(sprite)->id;
}

/**
 * Count the sprites which originate from a specific file in a range of SpriteIDs.
 * @param file The loaded SpriteFile.
 * @param begin First sprite in range.
 * @param end First sprite not in range.
 * @return Number of sprites.
 */
uint GetSpriteCountForFile(const std::string &filename, SpriteID begin, SpriteID end)
{
	SpriteFile *file = GetCachedSpriteFileByName(filename);
	if (file == nullptr) return 0;

	uint count = 0;
	for (SpriteID i = begin; i != end; i++) {
		if (SpriteExists(i)) {
			SpriteCache *sc = GetSpriteCache(i);
			if (sc->file == file) {
				count++;
				DEBUG(sprite, 4, "Sprite: %u", i);
			}
		}
	}
	return count;
}

/**
 * Get a reasonable (upper bound) estimate of the maximum
 * SpriteID used in OpenTTD; there will be no sprites with
 * a higher SpriteID.
 * @note It's actually the number of spritecache items.
 * @return maximum SpriteID
 */
uint GetMaxSpriteID()
{
	return (uint)_spritecache.size();
}

static bool ResizeSpriteIn(SpriteLoader::Sprite *sprite, ZoomLevel src, ZoomLevel tgt, bool dry_run)
{
	uint8 scaled_1 = ScaleByZoom(1, (ZoomLevel)(src - tgt));

	/* Check for possible memory overflow. */
	if (sprite[src].width * scaled_1 > UINT16_MAX || sprite[src].height * scaled_1 > UINT16_MAX) return false;

	sprite[tgt].width  = sprite[src].width  * scaled_1;
	sprite[tgt].height = sprite[src].height * scaled_1;
	sprite[tgt].x_offs = sprite[src].x_offs * scaled_1;
	sprite[tgt].y_offs = sprite[src].y_offs * scaled_1;
	sprite[tgt].colours = sprite[src].colours;

	if (dry_run) {
		sprite[tgt].data = nullptr;
		return true;
	}

	sprite[tgt].AllocateData(tgt, static_cast<size_t>(sprite[tgt].width) * sprite[tgt].height);

	SpriteLoader::CommonPixel *dst = sprite[tgt].data;
	for (int y = 0; y < sprite[tgt].height; y++) {
		const SpriteLoader::CommonPixel *src_ln = &sprite[src].data[y / scaled_1 * sprite[src].width];
		for (int x = 0; x < sprite[tgt].width; x++) {
			*dst = src_ln[x / scaled_1];
			dst++;
		}
	}

	return true;
}

static void ResizeSpriteOut(SpriteLoader::Sprite *sprite, ZoomLevel zoom, bool dry_run)
{
	/* Algorithm based on 32bpp_Optimized::ResizeSprite() */
	sprite[zoom].width  = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].width,  zoom);
	sprite[zoom].height = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].height, zoom);
	sprite[zoom].x_offs = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].x_offs, zoom);
	sprite[zoom].y_offs = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].y_offs, zoom);
	sprite[zoom].colours = sprite[ZOOM_LVL_NORMAL].colours;

	if (dry_run) {
		sprite[zoom].data = nullptr;
		return;
	}

	sprite[zoom].AllocateData(zoom, static_cast<size_t>(sprite[zoom].height) * sprite[zoom].width);

	SpriteLoader::CommonPixel *dst = sprite[zoom].data;
	const SpriteLoader::CommonPixel *src = sprite[zoom - 1].data;
	[[maybe_unused]] const SpriteLoader::CommonPixel *src_end = src + sprite[zoom - 1].height * sprite[zoom - 1].width;

	for (uint y = 0; y < sprite[zoom].height; y++) {
		const SpriteLoader::CommonPixel *src_ln = src + sprite[zoom - 1].width;
		assert(src_ln <= src_end);
		for (uint x = 0; x < sprite[zoom].width; x++) {
			assert(src < src_ln);
			if (src + 1 != src_ln && (src + 1)->a != 0) {
				*dst = *(src + 1);
			} else {
				*dst = *src;
			}
			dst++;
			src += 2;
		}
		src = src_ln + sprite[zoom - 1].width;
	}
}

static bool PadSingleSprite(SpriteLoader::Sprite *sprite, ZoomLevel zoom, uint pad_left, uint pad_top, uint pad_right, uint pad_bottom)
{
	uint width  = sprite->width + pad_left + pad_right;
	uint height = sprite->height + pad_top + pad_bottom;

	if (width > UINT16_MAX || height > UINT16_MAX) return false;

	if (sprite->data != nullptr) {
		/* Copy source data and reallocate sprite memory. */
		size_t sprite_size = static_cast<size_t>(sprite->width) * sprite->height;
		SpriteLoader::CommonPixel *src_data = MallocT<SpriteLoader::CommonPixel>(sprite_size);
		MemCpyT(src_data, sprite->data, sprite_size);
		sprite->AllocateData(zoom, static_cast<size_t>(width) * height);

		/* Copy with padding to destination. */
		SpriteLoader::CommonPixel *src = src_data;
		SpriteLoader::CommonPixel *data = sprite->data;
		for (uint y = 0; y < height; y++) {
			if (y < pad_top || pad_bottom + y >= height) {
				/* Top/bottom padding. */
				MemSetT(data, 0, width);
				data += width;
			} else {
				if (pad_left > 0) {
					/* Pad left. */
					MemSetT(data, 0, pad_left);
					data += pad_left;
				}

				/* Copy pixels. */
				MemCpyT(data, src, sprite->width);
				src += sprite->width;
				data += sprite->width;

				if (pad_right > 0) {
					/* Pad right. */
					MemSetT(data, 0, pad_right);
					data += pad_right;
				}
			}
		}
		free(src_data);
	}

	/* Update sprite size. */
	sprite->width   = width;
	sprite->height  = height;
	sprite->x_offs -= pad_left;
	sprite->y_offs -= pad_top;

	return true;
}

static bool PadSprites(SpriteLoader::Sprite *sprite, unsigned int sprite_avail, SpriteEncoder *encoder)
{
	/* Get minimum top left corner coordinates. */
	int min_xoffs = INT32_MAX;
	int min_yoffs = INT32_MAX;
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_SPR_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			min_xoffs = std::min(min_xoffs, ScaleByZoom(sprite[zoom].x_offs, zoom));
			min_yoffs = std::min(min_yoffs, ScaleByZoom(sprite[zoom].y_offs, zoom));
		}
	}

	/* Get maximum dimensions taking necessary padding at the top left into account. */
	int max_width  = INT32_MIN;
	int max_height = INT32_MIN;
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_SPR_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			max_width  = std::max(max_width, ScaleByZoom(sprite[zoom].width + sprite[zoom].x_offs - UnScaleByZoom(min_xoffs, zoom), zoom));
			max_height = std::max(max_height, ScaleByZoom(sprite[zoom].height + sprite[zoom].y_offs - UnScaleByZoom(min_yoffs, zoom), zoom));
		}
	}

	/* Align height and width if required to match the needs of the sprite encoder. */
	uint align = encoder->GetSpriteAlignment();
	if (align != 0) {
		max_width  = Align(max_width,  align);
		max_height = Align(max_height, align);
	}

	/* Pad sprites where needed. */
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_SPR_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			/* Scaling the sprite dimensions in the blitter is done with rounding up,
			 * so a negative padding here is not an error. */
			int pad_left   = std::max(0, sprite[zoom].x_offs - UnScaleByZoom(min_xoffs, zoom));
			int pad_top    = std::max(0, sprite[zoom].y_offs - UnScaleByZoom(min_yoffs, zoom));
			int pad_right  = std::max(0, UnScaleByZoom(max_width, zoom) - sprite[zoom].width - pad_left);
			int pad_bottom = std::max(0, UnScaleByZoom(max_height, zoom) - sprite[zoom].height - pad_top);

			if (pad_left > 0 || pad_right > 0 || pad_top > 0 || pad_bottom > 0) {
				if (!PadSingleSprite(&sprite[zoom], zoom, pad_left, pad_top, pad_right, pad_bottom)) return false;
			}
		}
	}

	return true;
}

static bool ResizeSprites(SpriteLoader::Sprite *sprite, unsigned int sprite_avail, SpriteEncoder *encoder, uint8 zoom_levels)
{
	ZoomLevel first_avail = static_cast<ZoomLevel>(FindFirstBit(sprite_avail));
	ZoomLevel first_needed = static_cast<ZoomLevel>(FindFirstBit(zoom_levels));
	ZoomLevel start = std::min(first_avail, first_needed);

	bool needed = false;
	for (ZoomLevel zoom = ZOOM_LVL_SPR_END; zoom-- > start; ) {
		if (HasBit(sprite_avail, zoom) && sprite[zoom].data != nullptr) {
			needed = false;
		} else if (HasBit(zoom_levels, zoom)) {
			needed = true;
		} else if (needed) {
			SetBit(zoom_levels, zoom);
		}
	}

	/* Create a fully zoomed image if it does not exist */
	if (first_avail != ZOOM_LVL_NORMAL) {
		if (!ResizeSpriteIn(sprite, first_avail, ZOOM_LVL_NORMAL, !HasBit(zoom_levels, ZOOM_LVL_NORMAL))) return false;
		SetBit(sprite_avail, ZOOM_LVL_NORMAL);
	}

	/* Create a zoomed image of the first required zoom if there any no sources which are equally or more zoomed in */
	if (zoom_levels != 0 && start > ZOOM_LVL_NORMAL && start < first_avail && HasBit(zoom_levels, start)) {
		if (!ResizeSpriteIn(sprite, first_avail, start, false)) return false;
		SetBit(sprite_avail, start);
	}

	/* Pad sprites to make sizes match. */
	if (!PadSprites(sprite, sprite_avail, encoder)) return false;

	/* Create other missing zoom levels */
	for (ZoomLevel zoom = ZOOM_LVL_OUT_2X; zoom != ZOOM_LVL_SPR_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			/* Check that size and offsets match the fully zoomed image. */
			assert(sprite[zoom].width  == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].width,  zoom));
			assert(sprite[zoom].height == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].height, zoom));
			assert(sprite[zoom].x_offs == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].x_offs, zoom));
			assert(sprite[zoom].y_offs == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].y_offs, zoom));
		}

		/* Zoom level is not available, or unusable, so create it */
		if (!HasBit(sprite_avail, zoom)) ResizeSpriteOut(sprite, zoom, !HasBit(zoom_levels, zoom));
	}

	return true;
}

/**
 * Load a recolour sprite into memory.
 * @param file GRF we're reading from.
 * @param num Size of the sprite in the GRF.
 * @return Sprite data.
 */
static void *ReadRecolourSprite(SpriteFile &file, uint num)
{
	/* "Normal" recolour sprites are ALWAYS 257 bytes. Then there is a small
	 * number of recolour sprites that are 17 bytes that only exist in DOS
	 * GRFs which are the same as 257 byte recolour sprites, but with the last
	 * 240 bytes zeroed.  */
	byte *dest = (byte *)AllocSprite(RECOLOUR_SPRITE_SIZE);

	auto read_data = [&](byte *targ) {
		file.ReadBlock(targ, std::min(num, RECOLOUR_SPRITE_SIZE));
		if (num > RECOLOUR_SPRITE_SIZE) {
			file.SkipBytes(num - RECOLOUR_SPRITE_SIZE);
		}
	};

	if (file.NeedsPaletteRemap()) {
		byte *dest_tmp = AllocaM(byte, RECOLOUR_SPRITE_SIZE);

		/* Only a few recolour sprites are less than 257 bytes */
		if (num < RECOLOUR_SPRITE_SIZE) memset(dest_tmp, 0, RECOLOUR_SPRITE_SIZE);
		read_data(dest_tmp);

		/* The data of index 0 is never used; "literal 00" according to the (New)GRF specs. */
		for (uint i = 1; i < RECOLOUR_SPRITE_SIZE; i++) {
			dest[i] = _palmap_w2d[dest_tmp[_palmap_d2w[i - 1] + 1]];
		}
	} else {
		read_data(dest);
	}

	return dest;
}

static const char *GetSpriteTypeName(SpriteType type)
{
	static const char * const sprite_types[] = {
		"normal",        // SpriteType::Normal
		"map generator", // SpriteType::MapGen
		"character",     // SpriteType::Font
		"recolour",      // SpriteType::Recolour
	};

	return sprite_types[static_cast<byte>(type)];
}

/**
 * Read a sprite from disk.
 * @param sc          Location of sprite.
 * @param id          Sprite number.
 * @param sprite_type Type of sprite.
 * @param allocator   Allocator function to use.
 * @param encoder     Sprite encoder to use.
 * @return Read sprite data.
 */
static void *ReadSprite(const SpriteCache *sc, SpriteID id, SpriteType sprite_type, AllocatorProc *allocator, SpriteEncoder *encoder, uint8 zoom_levels)
{
	/* Use current blitter if no other sprite encoder is given. */
	if (encoder == nullptr) {
		encoder = BlitterFactory::GetCurrentBlitter();
		if (!encoder->SupportsMissingZoomLevels()) zoom_levels = UINT8_MAX;
	} else {
		zoom_levels = UINT8_MAX;
	}
	if (encoder->NoSpriteDataRequired()) zoom_levels = 0;

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SCOPE_INFO_FMT([&], "ReadSprite: pos: " PRINTF_SIZE ", id: %u, file: (%s), type: %s", file_pos, id, file.GetSimplifiedFilename().c_str(), GetSpriteTypeName(sprite_type));

	assert(sprite_type != SpriteType::Recolour);
	assert(IsMapgenSpriteID(id) == (sprite_type == SpriteType::MapGen));
	assert(sc->GetType() == sprite_type);

	DEBUG(sprite, 9, "Load sprite %d", id);

	SpriteLoader::Sprite sprite[ZOOM_LVL_SPR_COUNT];
	uint8 sprite_avail = 0;
	sprite[ZOOM_LVL_NORMAL].type = sprite_type;

	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	if (sprite_type != SpriteType::MapGen && sc->GetHasNonPalette() && encoder->Is32BppSupported()) {
		/* Try for 32bpp sprites first. */
		sprite_avail = sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, true, sc->count, sc->flags, zoom_levels);
	}
	if (sprite_avail == 0) {
		sprite_avail = sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, false, sc->count, sc->flags, zoom_levels);
	}

	if (sprite_avail == 0) {
		if (sprite_type == SpriteType::MapGen) return nullptr;
		if (id == SPR_IMG_QUERY) usererror("Okay... something went horribly wrong. I couldn't load the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, UINT8_MAX, allocator, encoder);
	}

	if (sprite_type == SpriteType::MapGen) {
		/* Ugly hack to work around the problem that the old landscape
		 *  generator assumes that those sprites are stored uncompressed in
		 *  the memory, and they are only read directly by the code, never
		 *  send to the blitter. So do not send it to the blitter (which will
		 *  result in a data array in the format the blitter likes most), but
		 *  extract the data directly and store that as sprite.
		 * Ugly: yes. Other solution: no. Blame the original author or
		 *  something ;) The image should really have been a data-stream
		 *  (so type = 0xFF basically). */
		uint num = sprite[ZOOM_LVL_NORMAL].width * sprite[ZOOM_LVL_NORMAL].height;

		Sprite *s = (Sprite *)allocator(sizeof(*s) + num);
		s->width  = sprite[ZOOM_LVL_NORMAL].width;
		s->height = sprite[ZOOM_LVL_NORMAL].height;
		s->x_offs = sprite[ZOOM_LVL_NORMAL].x_offs;
		s->y_offs = sprite[ZOOM_LVL_NORMAL].y_offs;
		s->next = nullptr;
		s->missing_zoom_levels = 0;

		SpriteLoader::CommonPixel *src = sprite[ZOOM_LVL_NORMAL].data;
		byte *dest = s->data;
		while (num-- > 0) {
			*dest++ = src->m;
			src++;
		}

		return s;
	}

	if (!ResizeSprites(sprite, sprite_avail, encoder, zoom_levels)) {
		if (id == SPR_IMG_QUERY) usererror("Okay... something went horribly wrong. I couldn't resize the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, UINT8_MAX, allocator, encoder);
	}

	if (sprite->type == SpriteType::Font && _font_zoom != ZOOM_LVL_NORMAL) {
		/* Make ZOOM_LVL_NORMAL be ZOOM_LVL_GUI */
		sprite[ZOOM_LVL_NORMAL].width  = sprite[_font_zoom].width;
		sprite[ZOOM_LVL_NORMAL].height = sprite[_font_zoom].height;
		sprite[ZOOM_LVL_NORMAL].x_offs = sprite[_font_zoom].x_offs;
		sprite[ZOOM_LVL_NORMAL].y_offs = sprite[_font_zoom].y_offs;
		sprite[ZOOM_LVL_NORMAL].data   = sprite[_font_zoom].data;
		sprite[ZOOM_LVL_NORMAL].colours = sprite[_font_zoom].colours;
	}

	if (sprite->type == SpriteType::Normal) {
		/* Remove unwanted zoom levels before encoding */
		for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_SPR_END; zoom++) {
			if (!HasBit(zoom_levels, zoom)) sprite[zoom].data = nullptr;
		}
	}

	return encoder->Encode(sprite, allocator);
}

struct GrfSpriteOffset {
	size_t file_pos;
	uint count;
	uint16 control_flags;
};

/** Map from sprite numbers to position in the GRF file. */
static btree::btree_map<uint32, GrfSpriteOffset> _grf_sprite_offsets;

/**
 * Get the file offset for a specific sprite in the sprite section of a GRF.
 * @param id ID of the sprite to look up.
 * @return Position of the sprite in the sprite section or SIZE_MAX if no such sprite is present.
 */
size_t GetGRFSpriteOffset(uint32 id)
{
	auto iter = _grf_sprite_offsets.find(id);
	return iter != _grf_sprite_offsets.end() ? iter->second.file_pos : SIZE_MAX;
}

/**
 * Parse the sprite section of GRFs.
 * @param container_version Container version of the GRF we're currently processing.
 */
void ReadGRFSpriteOffsets(SpriteFile &file)
{
	_grf_sprite_offsets.clear();

	if (file.GetContainerVersion() >= 2) {
		/* Seek to sprite section of the GRF. */
		size_t data_offset = file.ReadDword();
		size_t old_pos = file.GetPos();
		file.SeekTo(data_offset, SEEK_CUR);

		GrfSpriteOffset offset = { 0, 0, 0 };

		/* Loop over all sprite section entries and store the file
		 * offset for each newly encountered ID. */
		uint32 id, prev_id = 0;
		while ((id = file.ReadDword()) != 0) {
			if (id != prev_id) {
				_grf_sprite_offsets[prev_id] = offset;
				offset.file_pos = file.GetPos() - 4;
				offset.count = 0;
				offset.control_flags = 0;
			}
			offset.count++;
			prev_id = id;
			uint length = file.ReadDword();
			if (length > 0) {
				byte colour = file.ReadByte() & SCC_MASK;
				length--;
				if (length > 0) {
					byte zoom = file.ReadByte();
					length--;
					if (colour != 0) {
						static const ZoomLevel zoom_lvl_map[6] = {ZOOM_LVL_OUT_4X, ZOOM_LVL_NORMAL, ZOOM_LVL_OUT_2X, ZOOM_LVL_OUT_8X, ZOOM_LVL_OUT_16X, ZOOM_LVL_OUT_32X};
						if (zoom < 6) SetBit(offset.control_flags, zoom_lvl_map[zoom] + ((colour != SCC_PAL) ? SCC_32BPP_ZOOM_START : SCC_PAL_ZOOM_START));
					}
				}
			}
			file.SkipBytes(length);
		}
		if (prev_id != 0) _grf_sprite_offsets[prev_id] = offset;

		/* Continue processing the data section. */
		file.SeekTo(old_pos, SEEK_SET);
	}
}


/**
 * Load a real or recolour sprite.
 * @param load_index Global sprite index.
 * @param file GRF to load from.
 * @param file_sprite_id Sprite number in the GRF.
 * @param container_version Container version of the GRF.
 * @return True if a valid sprite was loaded, false on any error.
 */
bool LoadNextSprite(int load_index, SpriteFile &file, uint file_sprite_id)
{
	size_t file_pos = file.GetPos();

	SCOPE_INFO_FMT([&], "LoadNextSprite: pos: " PRINTF_SIZE ", file: %s, load_index: %d, file_sprite_id: %u, container_ver: %u", file_pos, file.GetSimplifiedFilename().c_str(), load_index, file_sprite_id, file.GetContainerVersion());

	/* Read sprite header. */
	uint32 num = file.GetContainerVersion() >= 2 ? file.ReadDword() : file.ReadWord();
	if (num == 0) return false;
	byte grf_type = file.ReadByte();

	SpriteType type;
	void *data = nullptr;
	uint count = 0;
	uint16 control_flags = 0;
	if (grf_type == 0xFF) {
		/* Some NewGRF files have "empty" pseudo-sprites which are 1
		 * byte long. Catch these so the sprites won't be displayed. */
		if (num == 1) {
			file.ReadByte();
			return false;
		}
		type = SpriteType::Recolour;
		data = ReadRecolourSprite(file, num);
	} else if (file.GetContainerVersion() >= 2 && grf_type == 0xFD) {
		if (num != 4) {
			/* Invalid sprite section include, ignore. */
			file.SkipBytes(num);
			return false;
		}
		/* It is not an error if no sprite with the provided ID is found in the sprite section. */
		auto iter = _grf_sprite_offsets.find(file.ReadDword());
		if (iter != _grf_sprite_offsets.end()) {
			file_pos = iter->second.file_pos;
			count = iter->second.count;
			control_flags = iter->second.control_flags;
		} else {
			file_pos = SIZE_MAX;
		}
		type = SpriteType::Normal;
	} else {
		file.SkipBytes(7);
		type = SkipSpriteData(file, grf_type, num - 8) ? SpriteType::Normal : SpriteType::Invalid;
		/* Inline sprites are not supported for container version >= 2. */
		if (file.GetContainerVersion() >= 2) return false;
	}

	if (type == SpriteType::Invalid) return false;

	if (load_index == -1) {
		if (data != nullptr) _last_sprite_allocation.Clear();
		return false;
	}

	if (load_index >= MAX_SPRITES) {
		usererror("Tried to load too many sprites (#%d; max %d)", load_index, MAX_SPRITES);
	}

	bool is_mapgen = IsMapgenSpriteID(load_index);

	if (is_mapgen) {
		if (type != SpriteType::Normal) usererror("Uhm, would you be so kind not to load a NewGRF that changes the type of the map generator sprites?");
		type = SpriteType::MapGen;
	}

	SpriteCache *sc = AllocateSpriteCache(load_index);
	sc->file = &file;
	sc->file_pos = file_pos;
	sc->SetType(type);
	if (data != nullptr) {
		assert(data == _last_sprite_allocation.GetPtr());
		sc->Assign(std::move(_last_sprite_allocation));
	} else {
		sc->Clear();
	}
	sc->id = file_sprite_id;
	sc->count = count;
	sc->flags = control_flags;

	return true;
}


void DupSprite(SpriteID old_spr, SpriteID new_spr)
{
	SpriteCache *scnew = AllocateSpriteCache(new_spr); // may reallocate: so put it first
	SpriteCache *scold = GetSpriteCache(old_spr);

	scnew->file = scold->file;
	scnew->file_pos = scold->file_pos;
	scnew->id = scold->id;
	scnew->SetType(scold->GetType());
	scnew->flags = scold->flags;
	scnew->SetWarned(false);
}

static size_t GetSpriteCacheUsage()
{
	return _spritecache_bytes_used;
}

/**
 * Delete a single entry from the sprite cache.
 * @param item Entry to delete.
 */
static void DeleteEntryFromSpriteCache(uint item)
{
	GetSpriteCache(item)->Clear();
}

static void DeleteEntriesFromSpriteCache(size_t target)
{
	const size_t initial_in_use = GetSpriteCacheUsage();

	struct SpriteInfo {
		uint32 lru;
		SpriteID id;
		uint32 size;
		uint8 missing_zoom_levels;

		bool operator<(const SpriteInfo &other) const
		{
			return this->lru < other.lru;
		}
	};
	std::vector<SpriteInfo> candidates;
	size_t candidate_bytes = 0;

	auto push = [&](SpriteInfo info) {
		candidates.push_back(info);
		std::push_heap(candidates.begin(), candidates.end());
		candidate_bytes += info.size;
	};

	auto pop = [&]() {
		candidate_bytes -= candidates.front().size;
		std::pop_heap(candidates.begin(), candidates.end());
		candidates.pop_back();
	};

	size_t total_candidates = 0;
	SpriteID i = 0;
	for (; i != _spritecache.size() && candidate_bytes < target; i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() != SpriteType::Recolour) {
			Sprite *sp = (Sprite *)sc->GetPtr();
			while (sp != nullptr) {
				push({ sp->lru, i, sp->size, sp->missing_zoom_levels });
				total_candidates++;
				sp = sp->next;
			}
			if (candidate_bytes >= target) break;
		}
	}
	for (; i != _spritecache.size(); i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() != SpriteType::Recolour) {
			Sprite *sp = (Sprite *)sc->GetPtr();
			while (sp != nullptr) {
				total_candidates++;

				/* Only add to candidates if LRU <= current highest */
				if (sp->lru <= candidates.front().lru) {
					push({ sp->lru, i, sp->size, sp->missing_zoom_levels });
					while (!candidates.empty() && candidate_bytes - candidates.front().size >= target) {
						pop();
					}
				}
				sp = sp->next;
			}
		}
	}

	for (auto &it : candidates) {
		GetSpriteCache(it.id)->RemoveByMissingZoomLevels(it.missing_zoom_levels);
	}

	DEBUG(sprite, 3, "DeleteEntriesFromSpriteCache, deleted: " PRINTF_SIZE " of " PRINTF_SIZE ", freed: " PRINTF_SIZE ", in use: " PRINTF_SIZE " --> " PRINTF_SIZE ", delta: " PRINTF_SIZE ", requested: " PRINTF_SIZE,
			candidates.size(), total_candidates, candidate_bytes, initial_in_use, GetSpriteCacheUsage(), initial_in_use - GetSpriteCacheUsage(), target);

	_spritecache_prune_events++;
	_spritecache_prune_entries += candidates.size();
	_spritecache_prune_total += (initial_in_use - GetSpriteCacheUsage());
}

uint GetTargetSpriteSize()
{
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	return (bpp > 0 ? _sprite_cache_size * bpp / 8 : 1) * 1024 * 1024;
}

void IncreaseSpriteLRU()
{
	uint target_size = GetTargetSpriteSize();
	if (_spritecache_bytes_used > target_size) {
		DeleteEntriesFromSpriteCache(_spritecache_bytes_used - target_size + 512 * 1024);
	}

	/* Adjust all LRU values */
	if (_sprite_lru_counter >= 0xC0000000) {
		DEBUG(sprite, 5, "Fixing lru %u, inuse=" PRINTF_SIZE, _sprite_lru_counter, GetSpriteCacheUsage());

		for (SpriteID i = 0; i != _spritecache.size(); i++) {
			SpriteCache *sc = GetSpriteCache(i);
			if (sc->GetType() != SpriteType::Recolour) {
				Sprite *sp = (Sprite *)sc->GetPtr();
				while (sp != nullptr) {
					if (sp->lru > 0x80000000) {
						sp->lru -= 0x80000000;
					} else {
						sp->lru = 0;
					}
					sp = sp->next;
				}
			}
		}
		_sprite_lru_counter -= 0x80000000;
	}
}

static void *AllocSprite(size_t mem_req)
{
	assert(_last_sprite_allocation.GetPtr() == nullptr);
	_last_sprite_allocation.Allocate((uint32)mem_req);
	return _last_sprite_allocation.GetPtr();
}

/**
 * Sprite allocator simply using malloc.
 */
void *SimpleSpriteAlloc(size_t size)
{
	return MallocT<byte>(size);
}

/**
 * Handles the case when a sprite of different type is requested than is present in the SpriteCache.
 * For SpriteType::Font sprites, it is normal. In other cases, default sprite is loaded instead.
 * @param sprite ID of loaded sprite
 * @param requested requested sprite type
 * @param sc the currently known sprite cache for the requested sprite
 * @return fallback sprite
 * @note this function will do usererror() in the case the fallback sprite isn't available
 */
static void *HandleInvalidSpriteRequest(SpriteID sprite, SpriteType requested, SpriteCache *sc, AllocatorProc *allocator)
{
	SpriteType available = sc->GetType();
	if (requested == SpriteType::Font && available == SpriteType::Normal) {
		if (sc->GetPtr() == nullptr) sc->SetType(SpriteType::Font);
		return GetRawSprite(sprite, sc->GetType(), UINT8_MAX, allocator);
	}

	byte warning_level = sc->GetWarned() ? 6 : 0;
	sc->SetWarned(true);
	DEBUG(sprite, warning_level, "Tried to load %s sprite #%d as a %s sprite. Probable cause: NewGRF interference", GetSpriteTypeName(available), sprite, GetSpriteTypeName(requested));

	switch (requested) {
		case SpriteType::Normal:
			if (sprite == SPR_IMG_QUERY) usererror("Uhm, would you be so kind not to load a NewGRF that makes the 'query' sprite a non-normal sprite?");
			FALLTHROUGH;
		case SpriteType::Font:
			return GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, UINT8_MAX, allocator);
		case SpriteType::Recolour:
			if (sprite == PALETTE_TO_DARK_BLUE) usererror("Uhm, would you be so kind not to load a NewGRF that makes the 'PALETTE_TO_DARK_BLUE' sprite a non-remap sprite?");
			return GetRawSprite(PALETTE_TO_DARK_BLUE, SpriteType::Recolour, UINT8_MAX, allocator);
		case SpriteType::MapGen:
			/* this shouldn't happen, overriding of SpriteType::MapGen sprites is checked in LoadNextSprite()
			 * (the only case the check fails is when these sprites weren't even loaded...) */
		default:
			NOT_REACHED();
	}
}

/**
 * Reads a sprite (from disk or sprite cache).
 * If the sprite is not available or of wrong type, a fallback sprite is returned.
 * @param sprite Sprite to read.
 * @param type Expected sprite type.
 * @param allocator Allocator function to use. Set to nullptr to use the usual sprite cache.
 * @param encoder Sprite encoder to use. Set to nullptr to use the currently active blitter.
 * @return Sprite raw data
 */
void *GetRawSprite(SpriteID sprite, SpriteType type, uint8 zoom_levels, AllocatorProc *allocator, SpriteEncoder *encoder)
{
	assert(type != SpriteType::MapGen || IsMapgenSpriteID(sprite));
	assert(type < SpriteType::Invalid);

	if (!SpriteExists(sprite)) {
		DEBUG(sprite, 1, "Tried to load non-existing sprite #%d. Probable cause: Wrong/missing NewGRFs", sprite);

		/* SPR_IMG_QUERY is a BIG FAT RED ? */
		sprite = SPR_IMG_QUERY;
	}

	SpriteCache *sc = GetSpriteCache(sprite);

	if (sc->GetType() != type) return HandleInvalidSpriteRequest(sprite, type, sc, allocator);

	if (allocator == nullptr && encoder == nullptr) {
		/* Load sprite into/from spritecache */

		if (type != SpriteType::Normal) zoom_levels = UINT8_MAX;

		/* Load the sprite, if it is not loaded, yet */
		if (sc->GetPtr() == nullptr) {
			[[maybe_unused]] void *ptr = ReadSprite(sc, sprite, type, AllocSprite, nullptr, zoom_levels);
			assert(ptr == _last_sprite_allocation.GetPtr());
			sc->Assign(std::move(_last_sprite_allocation));
		} else if ((sc->total_missing_zoom_levels & zoom_levels) != 0) {
			[[maybe_unused]] void *ptr = ReadSprite(sc, sprite, type, AllocSprite, nullptr, sc->total_missing_zoom_levels & zoom_levels);
			assert(ptr == _last_sprite_allocation.GetPtr());
			sc->Append(std::move(_last_sprite_allocation));
		}

		if (type != SpriteType::Recolour) {
			uint8 lvls = zoom_levels;
			Sprite *sp = (Sprite *)sc->GetPtr();
			while (lvls != 0 && sp != nullptr) {
				uint8 usable = ~sp->missing_zoom_levels;
				if (usable & lvls) {
					/* Update LRU */
					sp->lru = ++_sprite_lru_counter;
					lvls &= ~usable;
				}
				sp = sp->next;
			}
		}

		return sc->GetPtr();
	} else {
		/* Do not use the spritecache, but a different allocator. */
		return ReadSprite(sc, sprite, type, allocator, encoder, UINT8_MAX);
	}
}

/**
 * Reads a sprite and finds its most representative colour.
 * @param sprite Sprite to read.
 * @param palette_id Palette for remapping colours.
 * @return if blitter supports 32bpp, average Colour.data else a palette index.
 */
uint32 GetSpriteMainColour(SpriteID sprite_id, PaletteID palette_id)
{
	if (!SpriteExists(sprite_id)) return 0;

	SpriteCache *sc = GetSpriteCache(sprite_id);
	if (sc->GetType() != SpriteType::Normal) return 0;

	const byte * const remap = (palette_id == PAL_NONE ? nullptr : GetNonSprite(GB(palette_id, 0, PALETTE_WIDTH), SpriteType::Recolour) + 1);

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SpriteLoader::Sprite sprites[ZOOM_LVL_SPR_COUNT];
	sprites[ZOOM_LVL_NORMAL].type = SpriteType::Normal;
	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	uint8 sprite_avail;
	const uint8 screen_depth = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	auto zoom_mask = [&](bool is32bpp) -> uint8 {
		return 1 << FindFirstBit(GB(sc->flags, is32bpp ? SCC_32BPP_ZOOM_START : SCC_PAL_ZOOM_START, 6));
	};

	/* Try to read the 32bpp sprite first. */
	if (screen_depth == 32 && sc->GetHasNonPalette()) {
		sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, SpriteType::Normal, true, sc->count, sc->flags, zoom_mask(true));
		if (sprite_avail != 0) {
			SpriteLoader::Sprite *sprite = &sprites[FindFirstBit(sprite_avail)];
			/* Return the average colour. */
			uint32 r = 0, g = 0, b = 0, cnt = 0;
			SpriteLoader::CommonPixel *pixel = sprite->data;
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				if (pixel->a) {
					if (remap && pixel->m) {
						const Colour c = _cur_palette.palette[remap[pixel->m]];
						if (c.a) {
							r += c.r;
							g += c.g;
							b += c.b;
							cnt++;
						}
					} else {
						r += pixel->r;
						g += pixel->g;
						b += pixel->b;
						cnt++;
					}
				}
				pixel++;
			}
			return cnt ? Colour(r / cnt, g / cnt, b / cnt).data : 0;
		}
	}

	/* No 32bpp, try 8bpp. */
	sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, SpriteType::Normal, false, sc->count, sc->flags, zoom_mask(false));
	if (sprite_avail != 0) {
		SpriteLoader::Sprite *sprite = &sprites[FindFirstBit(sprite_avail)];
		SpriteLoader::CommonPixel *pixel = sprite->data;
		if (screen_depth == 32) {
			/* Return the average colour. */
			uint32 r = 0, g = 0, b = 0, cnt = 0;
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				if (pixel->a) {
					const uint col_index = remap ? remap[pixel->m] : pixel->m;
					const Colour c = _cur_palette.palette[col_index];
					r += c.r;
					g += c.g;
					b += c.b;
					cnt++;
				}
				pixel++;
			}
			return cnt ? Colour(r / cnt, g / cnt, b / cnt).data : 0;
		} else {
			/* Return the most used indexed colour. */
			int cnt[256];
			memset(cnt, 0, sizeof(cnt));
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				cnt[remap ? remap[pixel->m] : pixel->m]++;
				pixel++;
			}
			int cnt_max = -1;
			uint32 rk = 0;
			for (uint x = 1; x < lengthof(cnt); x++) {
				if (cnt[x] > cnt_max) {
					rk = x;
					cnt_max = cnt[x];
				}
			}
			return rk;
		}
	}

	return 0;
}

void GfxInitSpriteMem()
{
	/* Reset the spritecache 'pool' */
	_spritecache.clear();
	_sprite_files.clear();
	assert(_spritecache_bytes_used == 0);
	_spritecache_prune_events = 0;
	_spritecache_prune_entries = 0;
	_spritecache_prune_total = 0;
}

/**
 * Remove all encoded sprites from the sprite cache without
 * discarding sprite location information.
 */
void GfxClearSpriteCache()
{
	/* Clear sprite ptr for all cached items */
	for (uint i = 0; i != _spritecache.size(); i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() != SpriteType::Recolour && sc->GetPtr() != nullptr) DeleteEntryFromSpriteCache(i);
	}

	VideoDriver::GetInstance()->ClearSystemSprites();
}

/**
 * Remove all encoded font sprites from the sprite cache without
 * discarding sprite location information.
 */
void GfxClearFontSpriteCache()
{
	/* Clear sprite ptr for all cached font items */
	for (uint i = 0; i != _spritecache.size(); i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() == SpriteType::Font && sc->GetPtr() != nullptr) DeleteEntryFromSpriteCache(i);
	}
}

void DumpSpriteCacheStats(char *buffer, const char *last)
{
	uint target_size = GetTargetSpriteSize();
	buffer += seprintf(buffer, last, "Sprite cache: entries: %u, size: %u, target: %u, percent used: %.1f%%\n",
			(uint)_spritecache.size(), (uint)_spritecache_bytes_used, target_size, (100.0f * _spritecache_bytes_used) / target_size);

	uint types[(uint)SpriteType::Invalid] = {};
	uint have_data = 0;
	uint have_warned = 0;
	uint have_8bpp = 0;
	uint have_32bpp = 0;

	uint depths[16] = {};
	uint have_partial_zoom = 0;
	for (const SpriteCache &entry : _spritecache) {
		if ((uint)entry.GetType() >= (uint)SpriteType::Invalid) continue;
		types[(uint)entry.GetType()]++;
		if (entry.GetPtr() != nullptr) have_data++;
		if (entry.GetHasPalette()) have_8bpp++;
		if (entry.GetHasNonPalette()) have_32bpp++;

		if (entry.GetType() == SpriteType::Normal) {
			if (entry.total_missing_zoom_levels != 0) have_partial_zoom++;
			uint depth = 0;
			const Sprite *p = (const Sprite *)entry.GetPtr();
			while (p != nullptr) {
				depth++;
				p = p->next;
			}
			if (depth < lengthof(depths)) depths[depth]++;
		}
	}
	buffer += seprintf(buffer, last, "  Normal: %u, MapGen: %u, Font: %u, Recolour: %u\n",
			types[(uint)SpriteType::Normal], types[(uint)SpriteType::MapGen], types[(uint)SpriteType::Font], types[(uint)SpriteType::Recolour]);
	buffer += seprintf(buffer, last, "  Data loaded: %u, Warned: %u, 8bpp: %u, 32bpp: %u\n",
			have_data, have_warned, have_8bpp, have_32bpp);
	buffer += seprintf(buffer, last, "  Cache prune events: %u, pruned entry total: " PRINTF_SIZE ", pruned data total: " PRINTF_SIZE "\n",
			_spritecache_prune_events, _spritecache_prune_entries, _spritecache_prune_total);
	buffer += seprintf(buffer, last, "  Normal:\n");
	buffer += seprintf(buffer, last, "    Partial zoom: %u\n", have_partial_zoom);
	for (uint i = 0; i < lengthof(depths); i++) {
		if (depths[i] > 0) buffer += seprintf(buffer, last, "    Data depth %u: %u\n", i, depths[i]);
	}
}

/* static */ ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_SPR_COUNT];
