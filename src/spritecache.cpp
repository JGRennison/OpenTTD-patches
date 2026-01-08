/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spritecache.cpp Caching of sprites. */

#include "stdafx.h"
#include "core/alloc_func.hpp"
#include "random_access_file_type.h"
#include "spriteloader/grf.hpp"
#include "spriteloader/makeindexed.h"
#include "gfx_func.h"
#include "error.h"
#include "error_func.h"
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
#include "blitter/32bpp_base.hpp"

#include "table/sprites.h"
#include "table/strings.h"
#include "table/palette_convert.h"

#include "3rdparty/cpp-btree/btree_map.h"

#include <vector>
#include <algorithm>
#include <optional>

#include "safeguards.h"

/* Default of 4MB spritecache */
uint _sprite_cache_size = 4;

size_t _spritecache_bytes_used = 0;
static uint32_t _sprite_lru_counter;
static uint32_t _spritecache_prune_events = 0;
static size_t _spritecache_prune_entries = 0;
static size_t _spritecache_prune_total = 0;

static std::vector<SpriteCache> _spritecache;
static std::vector<std::unique_ptr<SpriteFile>> _sprite_files;
static RecolourSpriteCache _recolour_cache;

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
 * Get the list of cached SpriteFiles.
 * @return Read-only list of cache SpriteFiles.
 */
std::span<const std::unique_ptr<SpriteFile>> GetCachedSpriteFiles()
{
	return _sprite_files;
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

/**
 * Skip the given amount of sprite graphics data.
 * @param type the type of sprite (compressed etc)
 * @param num the amount of sprites to skip
 * @return true if the data could be correctly skipped.
 */
bool SkipSpriteData(SpriteFile &file, uint8_t type, uint16_t num)
{
	if (type & 2) {
		file.SkipBytes(num);
	} else {
		while (num > 0) {
			int8_t i = file.ReadByte();
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
uint32_t GetSpriteLocalID(SpriteID sprite)
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
				Debug(sprite, 4, "Sprite: {}", i);
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

static bool ResizeSpriteIn(SpriteLoader::SpriteCollection &sprite, ZoomLevel src, ZoomLevel tgt, bool dry_run)
{
	uint8_t scaled_1 = ScaleByZoom(1, (ZoomLevel)(src - tgt));
	const auto &src_sprite = sprite[src];
	auto &dest_sprite = sprite[tgt];

	/* Check for possible memory overflow. */
	if (src_sprite.width * scaled_1 > UINT16_MAX || src_sprite.height * scaled_1 > UINT16_MAX) return false;

	dest_sprite.width  = src_sprite.width  * scaled_1;
	dest_sprite.height = src_sprite.height * scaled_1;
	dest_sprite.x_offs = src_sprite.x_offs * scaled_1;
	dest_sprite.y_offs = src_sprite.y_offs * scaled_1;
	dest_sprite.colours = src_sprite.colours;

	if (dry_run) {
		dest_sprite.data = nullptr;
		return true;
	}

	dest_sprite.AllocateData(tgt, static_cast<size_t>(dest_sprite.width) * dest_sprite.height);

	SpriteLoader::CommonPixel *dst = dest_sprite.data;
	for (int y = 0; y < dest_sprite.height; y++) {
		const SpriteLoader::CommonPixel *src_ln = &src_sprite.data[y / scaled_1 * src_sprite.width];
		for (int x = 0; x < dest_sprite.width; x++) {
			*dst = src_ln[x / scaled_1];
			dst++;
		}
	}

	return true;
}

static void ResizeSpriteOut(SpriteLoader::SpriteCollection &sprite, ZoomLevel zoom, bool dry_run)
{
	/* Algorithm based on 32bpp_Optimized::ResizeSprite() */
	const auto &root_sprite = sprite.Root();
	auto &dest_sprite = sprite[zoom];
	dest_sprite.width  = UnScaleByZoom(root_sprite.width,  zoom);
	dest_sprite.height = UnScaleByZoom(root_sprite.height, zoom);
	dest_sprite.x_offs = UnScaleByZoom(root_sprite.x_offs, zoom);
	dest_sprite.y_offs = UnScaleByZoom(root_sprite.y_offs, zoom);
	dest_sprite.colours = root_sprite.colours;

	if (dry_run) {
		dest_sprite.data = nullptr;
		return;
	}

	dest_sprite.AllocateData(zoom, static_cast<size_t>(dest_sprite.height) * dest_sprite.width);

	SpriteLoader::CommonPixel *dst = dest_sprite.data;
	auto &src_sprite = sprite[zoom - 1];
	const SpriteLoader::CommonPixel *src = src_sprite.data;
	[[maybe_unused]] const SpriteLoader::CommonPixel *src_end = src + src_sprite.height * src_sprite.width;

	for (uint y = 0; y < sprite[zoom].height; y++) {
		const SpriteLoader::CommonPixel *src_ln = src + src_sprite.width;
		assert(src_ln <= src_end);
		for (uint x = 0; x < dest_sprite.width; x++) {
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
		std::unique_ptr<SpriteLoader::CommonPixel[]> src_data(new SpriteLoader::CommonPixel[sprite_size]);
		MemCpyT(src_data.get(), sprite->data, sprite_size);
		sprite->AllocateData(zoom, static_cast<size_t>(width) * height);

		/* Copy with padding to destination. */
		SpriteLoader::CommonPixel *src = src_data.get();
		SpriteLoader::CommonPixel *data = sprite->data;
		for (uint y = 0; y < height; y++) {
			if (y < pad_top || pad_bottom + y >= height) {
				/* Top/bottom padding. */
				std::fill_n(data, width, SpriteLoader::CommonPixel{});
				data += width;
			} else {
				if (pad_left > 0) {
					/* Pad left. */
					std::fill_n(data, pad_left, SpriteLoader::CommonPixel{});
					data += pad_left;
				}

				/* Copy pixels. */
				std::copy_n(src, sprite->width, data);
				src += sprite->width;
				data += sprite->width;

				if (pad_right > 0) {
					/* Pad right. */
					std::fill_n(data, pad_right, SpriteLoader::CommonPixel{});
					data += pad_right;
				}
			}
		}
	}

	/* Update sprite size. */
	sprite->width   = width;
	sprite->height  = height;
	sprite->x_offs -= pad_left;
	sprite->y_offs -= pad_top;

	return true;
}

static bool PadSprites(SpriteLoader::SpriteCollection &sprite, LowZoomLevels sprite_avail, SpriteEncoder *encoder)
{
	/* Get minimum top left corner coordinates. */
	int min_xoffs = INT32_MAX;
	int min_yoffs = INT32_MAX;
	for (ZoomLevel zoom = ZoomLevel::Begin; zoom != ZoomLevel::SpriteEnd; zoom++) {
		if (sprite_avail.Test(zoom)) {
			min_xoffs = std::min(min_xoffs, ScaleByZoom(sprite[zoom].x_offs, zoom));
			min_yoffs = std::min(min_yoffs, ScaleByZoom(sprite[zoom].y_offs, zoom));
		}
	}

	/* Get maximum dimensions taking necessary padding at the top left into account. */
	int max_width  = INT32_MIN;
	int max_height = INT32_MIN;
	for (ZoomLevel zoom = ZoomLevel::Begin; zoom != ZoomLevel::SpriteEnd; zoom++) {
		if (sprite_avail.Test(zoom)) {
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
	for (ZoomLevel zoom = ZoomLevel::Begin; zoom != ZoomLevel::SpriteEnd; zoom++) {
		if (sprite_avail.Test(zoom)) {
			auto &cur_sprite = sprite[zoom];
			/* Scaling the sprite dimensions in the blitter is done with rounding up,
			 * so a negative padding here is not an error. */
			int pad_left   = std::max(0, cur_sprite.x_offs - UnScaleByZoom(min_xoffs, zoom));
			int pad_top    = std::max(0, cur_sprite.y_offs - UnScaleByZoom(min_yoffs, zoom));
			int pad_right  = std::max(0, UnScaleByZoom(max_width, zoom) - cur_sprite.width - pad_left);
			int pad_bottom = std::max(0, UnScaleByZoom(max_height, zoom) - cur_sprite.height - pad_top);

			if (pad_left > 0 || pad_right > 0 || pad_top > 0 || pad_bottom > 0) {
				if (!PadSingleSprite(&cur_sprite, zoom, pad_left, pad_top, pad_right, pad_bottom)) return false;
			}
		}
	}

	return true;
}

static bool ResizeSprites(SpriteLoader::SpriteCollection &sprite, LowZoomLevels sprite_avail, SpriteEncoder *encoder, LowZoomLevels zoom_levels)
{
	ZoomLevel first_avail = sprite_avail.FindFirstBit();
	ZoomLevel first_needed = zoom_levels.FindFirstBit();

	/* Upscale to desired sprite_min_zoom if provided sprite only had zoomed in versions. */
	if (first_avail < _settings_client.gui.sprite_zoom_min) {
		const unsigned int below_min_zoom_mask = (1 << to_underlying(_settings_client.gui.sprite_zoom_min)) - 1;
		if ((zoom_levels.base() & below_min_zoom_mask) != 0 && !sprite_avail.Test(_settings_client.gui.sprite_zoom_min)) {
			if (!sprite_avail.Test(ZoomLevel::In2x)) ResizeSpriteOut(sprite, ZoomLevel::In2x, false);
			if (_settings_client.gui.sprite_zoom_min == ZoomLevel::Normal) {
				if (first_avail != ZoomLevel::Min) {
					/* Ensure dimensions of ZoomLevel::Min are set if the first available sprite level was ZoomLevel::In2x */
					if (!ResizeSpriteIn(sprite, first_avail, ZoomLevel::Min, true)) return false;
				}
				ResizeSpriteOut(sprite, ZoomLevel::Normal, false);
			}
			sprite_avail.edit_base() &= ~below_min_zoom_mask;
			sprite_avail.Set(_settings_client.gui.sprite_zoom_min);
			first_avail = _settings_client.gui.sprite_zoom_min;
		}
	}

	ZoomLevel start = std::min(first_avail, first_needed);

	bool needed = false;
	for (ZoomLevel zoom = ZoomLevel::SpriteEnd; zoom-- > start; ) {
		if (sprite_avail.Test(zoom) && sprite[zoom].data != nullptr) {
			needed = false;
		} else if (zoom_levels.Test(zoom)) {
			needed = true;
		} else if (needed) {
			zoom_levels.Set(zoom);
		}
	}

	/* Create a fully zoomed image if it does not exist */
	if (first_avail != ZoomLevel::Min) {
		if (!ResizeSpriteIn(sprite, first_avail, ZoomLevel::Min, !zoom_levels.Test(ZoomLevel::Min))) return false;
		sprite_avail.Set(ZoomLevel::Min);
	}

	/* Create a zoomed image of the first required zoom if there any no sources which are equally or more zoomed in */
	if (zoom_levels.Any() && start > ZoomLevel::Min && start < first_avail && zoom_levels.Test(start)) {
		if (!ResizeSpriteIn(sprite, first_avail, start, false)) return false;
		sprite_avail.Set(start);
	}

	/* Pad sprites to make sizes match. */
	if (!PadSprites(sprite, sprite_avail, encoder)) return false;

	/* Create other missing zoom levels */
	for (ZoomLevel zoom = ZoomLevel::In2x; zoom != ZoomLevel::SpriteEnd; zoom++) {
		if (sprite_avail.Test(zoom)) {
			/* Check that size and offsets match the fully zoomed image. */
			[[maybe_unused]] const auto &root_sprite = sprite[ZoomLevel::Min];
			[[maybe_unused]] const auto &dest_sprite = sprite[zoom];
			assert(dest_sprite.width  == UnScaleByZoom(root_sprite.width,  zoom));
			assert(dest_sprite.height == UnScaleByZoom(root_sprite.height, zoom));
			assert(dest_sprite.x_offs == UnScaleByZoom(root_sprite.x_offs, zoom));
			assert(dest_sprite.y_offs == UnScaleByZoom(root_sprite.y_offs, zoom));
		}

		/* Zoom level is not available, or unusable, so create it */
		if (!sprite_avail.Test(zoom)) ResizeSpriteOut(sprite, zoom, !zoom_levels.Test(zoom));
	}

	return true;
}

/**
 * Load a recolour sprite into memory.
 * @param file GRF we're reading from.
 * @param num Size of the sprite in the GRF, must be >= 1.
 * @param buffer Output buffer to write data to.
 * @return Sprite data.
 */
static void ReadRecolourSprite(SpriteFile &file, uint num, std::span<uint8_t, RECOLOUR_SPRITE_SIZE> buffer)
{
	/* "Normal" recolour sprites are ALWAYS 257 bytes. Then there is a small
	 * number of recolour sprites that are 17 bytes that only exist in DOS
	 * GRFs which are the same as 257 byte recolour sprites, but with the last
	 * 240 bytes zeroed.  */
	uint8_t *dest = buffer.data();

	/* The first byte of the recolour sprite is never used, so just skip it */
	file.ReadByte();
	num--;

	auto read_data = [&](uint8_t *targ) {
		file.ReadBlock(targ, std::min(num, RECOLOUR_SPRITE_SIZE));
		if (num > RECOLOUR_SPRITE_SIZE) {
			file.SkipBytes(num - RECOLOUR_SPRITE_SIZE);
		} else if (num < RECOLOUR_SPRITE_SIZE) {
			/* Only a few recolour sprites are less than 257 bytes */
			memset(targ + num, 0, RECOLOUR_SPRITE_SIZE - num);
		}
	};

	if (file.NeedsPaletteRemap()) {
		uint8_t dest_tmp[RECOLOUR_SPRITE_SIZE];
		read_data(dest_tmp);

		for (uint i = 0; i < RECOLOUR_SPRITE_SIZE; i++) {
			dest[i] = _palmap_w2d[dest_tmp[_palmap_d2w[i]]];
		}
	} else {
		read_data(dest);
	}
}

static const char *GetSpriteTypeName(SpriteType type)
{
	static const char * const sprite_types[] = {
		"normal",        // SpriteType::Normal
		"map generator", // SpriteType::MapGen
		"character",     // SpriteType::Font
		"recolour",      // SpriteType::Recolour
	};

	return sprite_types[static_cast<uint8_t>(type)];
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
static void *ReadSprite(const SpriteCache *sc, SpriteID id, SpriteType sprite_type, SpriteAllocator &allocator, SpriteEncoder *encoder, LowZoomLevels zoom_levels)
{
	/* Use current blitter if no other sprite encoder is given. */
	if (encoder == nullptr) {
		encoder = BlitterFactory::GetCurrentBlitter();
		if (!encoder->SupportsMissingZoomLevels()) zoom_levels = LOW_ZOOM_ALL_BITS;
	} else {
		zoom_levels = LOW_ZOOM_ALL_BITS;
	}
	if (encoder->NoSpriteDataRequired()) zoom_levels = {};

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SCOPE_INFO_FMT([&], "ReadSprite: pos: {}, id: {}, file: ({}), type: {}", file_pos, id, file.GetSimplifiedFilename(), GetSpriteTypeName(sprite_type));

	assert(sprite_type != SpriteType::Recolour);
	assert(IsMapgenSpriteID(id) == (sprite_type == SpriteType::MapGen));
	assert(sc->GetType() == sprite_type);

	Debug(sprite, 9, "Load sprite {}", id);

	SpriteLoader::SpriteCollection sprite;
	SpriteLoaderResult load_result{};

	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	if (sprite_type != SpriteType::MapGen && sc->GetHasNonPalette() && encoder->Is32BppSupported()) {
		/* Try for 32bpp sprites first. */
		load_result = sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, true, sc->count, sc->flags, zoom_levels);
	}
	if (load_result.loaded_sprites.None()) {
		load_result.Apply(sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, false, sc->count, sc->flags, zoom_levels));
		if (sprite_type == SpriteType::Normal && load_result.avail_32bpp.Any() && !encoder->Is32BppSupported() && load_result.loaded_sprites.None()) {
			/* No 8bpp available, try converting from 32bpp. */
			SpriteLoaderMakeIndexed make_indexed(sprite_loader);
			load_result = make_indexed.LoadSprite(sprite, file, file_pos, sprite_type, true, sc->count, sc->flags, zoom_levels);
		}
	}

	if (load_result.loaded_sprites.None()) {
		if (sprite_type == SpriteType::MapGen) return nullptr;
		if (id == SPR_IMG_QUERY) UserError("Okay... something went horribly wrong. I couldn't load the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, LOW_ZOOM_ALL_BITS, &allocator, encoder);
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
		const auto &root_sprite = sprite.Root();
		uint num = root_sprite.width * root_sprite.height;

		Sprite *s = allocator.Allocate<Sprite>(sizeof(*s) + num);
		s->width  = root_sprite.width;
		s->height = root_sprite.height;
		s->x_offs = root_sprite.x_offs;
		s->y_offs = root_sprite.y_offs;
		s->next = nullptr;
		s->missing_zoom_levels = {};

		SpriteLoader::CommonPixel *src = root_sprite.data;
		uint8_t *dest = s->data;
		while (num-- > 0) {
			*dest++ = src->m;
			src++;
		}

		return s;
	}

	if (!ResizeSprites(sprite, load_result.loaded_sprites, encoder, zoom_levels)) {
		if (id == SPR_IMG_QUERY) UserError("Okay... something went horribly wrong. I couldn't resize the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, LOW_ZOOM_ALL_BITS, &allocator, encoder);
	}

	if (sprite_type == SpriteType::Font && _font_zoom != ZoomLevel::Min) {
		/* Make ZoomLevel::Min be ZOOM_LVL_GUI */
		sprite[ZoomLevel::Min] = sprite[_font_zoom];
	}

	if (sprite_type == SpriteType::Normal) {
		/* Remove unwanted zoom levels before encoding */
		for (ZoomLevel zoom = ZoomLevel::Begin; zoom != ZoomLevel::SpriteEnd; zoom++) {
			if (!zoom_levels.Test(zoom)) sprite[zoom].data = nullptr;
		}
	}

	return encoder->Encode(sprite_type, sprite, allocator);
}

struct GrfSpriteOffset {
	size_t file_pos;
	uint count;
	uint16_t control_flags;
};

/** Map from sprite numbers to position in the GRF file. */
static robin_hood::unordered_flat_map<uint32_t, GrfSpriteOffset> _grf_sprite_offsets;

/**
 * Get the file offset for a specific sprite in the sprite section of a GRF.
 * @param id ID of the sprite to look up.
 * @return Position of the sprite in the sprite section or SIZE_MAX if no such sprite is present.
 */
size_t GetGRFSpriteOffset(uint32_t id)
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
		SpriteID id, prev_id = 0;
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
				SpriteComponents colour{file.ReadByte()};
				length--;
				if (length > 0) {
					uint8_t zoom = file.ReadByte();
					length--;
					if (colour.Any()) {
						static const ZoomLevel zoom_lvl_map[6] = {ZoomLevel::Normal, ZoomLevel::In4x, ZoomLevel::In2x, ZoomLevel::Out2x, ZoomLevel::Out4x, ZoomLevel::Out8x};
						if (zoom < 6) SetBit(offset.control_flags, static_cast<uint>(zoom_lvl_map[zoom]) + static_cast<uint>((colour != SpriteComponent::Palette) ? SCC_32BPP_ZOOM_START : SCC_PAL_ZOOM_START));
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
bool LoadNextSprite(SpriteID load_index, SpriteFile &file, uint file_sprite_id)
{
	size_t file_pos = file.GetPos();

	SCOPE_INFO_FMT([&], "LoadNextSprite: pos: {}, file: {}, load_index: {}, file_sprite_id: {}, container_ver: {}", file_pos, file.GetSimplifiedFilename(), load_index, file_sprite_id, file.GetContainerVersion());

	/* Read sprite header. */
	uint32_t num = file.GetContainerVersion() >= 2 ? file.ReadDword() : file.ReadWord();
	if (num == 0) return false;
	uint8_t grf_type = file.ReadByte();

	CacheSpriteAllocator allocator;

	SpriteType type;
	void *data = nullptr;
	uint count = 0;
	uint16_t control_flags = 0;
	if (grf_type == 0xFF) {
		/* Some NewGRF files have "empty" pseudo-sprites which are 1
		 * byte long. Catch these so the sprites won't be displayed. */
		if (num == 1) {
			file.ReadByte();
			return false;
		}
		type = SpriteType::Recolour;
		auto &buffer = _recolour_cache.GetBuffer();
		ReadRecolourSprite(file, num, buffer);
		data = _recolour_cache.GetCachePtr();
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

	if (load_index == INVALID_SPRITE_ID) {
		return false;
	}

	if (load_index >= MAX_SPRITES) {
		UserError("Tried to load too many sprites (#{}; max {})", load_index, MAX_SPRITES);
	}

	bool is_mapgen = IsMapgenSpriteID(load_index);

	if (is_mapgen) {
		if (type != SpriteType::Normal) UserError("Uhm, would you be so kind not to load a NewGRF that changes the type of the map generator sprites?");
		type = SpriteType::MapGen;
	}

	SpriteCache *sc = AllocateSpriteCache(load_index);
	sc->Clear(); // Clear existing entry before changing type field
	sc->file = &file;
	sc->file_pos = file_pos;
	sc->SetType(type);
	if (data != nullptr) {
		if (type == SpriteType::Recolour) {
			sc->AssignRecolourSpriteData(data);
		} else {
			assert(data == allocator.last_sprite_allocation.GetPtr());
			sc->Assign(std::move(allocator.last_sprite_allocation));
		}
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
		uint32_t lru;
		SpriteID id;
		uint32_t size;
		LowZoomLevels missing_zoom_levels;

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

	Debug(sprite, 3, "DeleteEntriesFromSpriteCache, deleted: {} of {}, freed: {}, in use: {} --> {}, delta: {}, requested: {}",
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
		Debug(sprite, 5, "Fixing lru {}, inuse={}", _sprite_lru_counter, GetSpriteCacheUsage());

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

void *CacheSpriteAllocator::AllocatePtr(size_t mem_req)
{
	assert(this->last_sprite_allocation.GetPtr() == nullptr);
	this->last_sprite_allocation.Allocate((uint32_t)mem_req);
	return this->last_sprite_allocation.GetPtr();
}

void *UniquePtrSpriteAllocator::AllocatePtr(size_t size)
{
	this->data = std::make_unique<std::byte[]>(size);
	return this->data.get();
}

/**
 * Handles the case when a sprite of different type is requested than is present in the SpriteCache.
 * For SpriteType::Font sprites, it is normal. In other cases, default sprite is loaded instead.
 * @param sprite ID of loaded sprite
 * @param requested requested sprite type
 * @param sc the currently known sprite cache for the requested sprite
 * @return fallback sprite
 * @note this function will do UserError() in the case the fallback sprite isn't available
 */
static void *HandleInvalidSpriteRequest(SpriteID sprite, SpriteType requested, SpriteCache *sc, SpriteAllocator *allocator)
{
	SpriteType available = sc->GetType();
	if (requested == SpriteType::Font && available == SpriteType::Normal) {
		if (sc->GetPtr() == nullptr) sc->SetType(SpriteType::Font);
		return GetRawSprite(sprite, sc->GetType(), LOW_ZOOM_ALL_BITS, allocator);
	}

	uint8_t warning_level = sc->GetWarned() ? 6 : 0;
	sc->SetWarned(true);
	Debug(sprite, warning_level, "Tried to load {} sprite #{} as a {} sprite. Probable cause: NewGRF interference", GetSpriteTypeName(available), sprite, GetSpriteTypeName(requested));

	switch (requested) {
		case SpriteType::Normal:
			if (sprite == SPR_IMG_QUERY) UserError("Uhm, would you be so kind not to load a NewGRF that makes the 'query' sprite a non-normal sprite?");
			[[fallthrough]];
		case SpriteType::Font:
			return GetRawSprite(SPR_IMG_QUERY, SpriteType::Normal, LOW_ZOOM_ALL_BITS, allocator);
		case SpriteType::Recolour:
			if (sprite == PALETTE_TO_DARK_BLUE) UserError("Uhm, would you be so kind not to load a NewGRF that makes the 'PALETTE_TO_DARK_BLUE' sprite a non-remap sprite?");
			return GetRawSprite(PALETTE_TO_DARK_BLUE, SpriteType::Recolour, LOW_ZOOM_ALL_BITS, allocator);
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
void *GetRawSprite(SpriteID sprite, SpriteType type, LowZoomLevels zoom_levels, SpriteAllocator *allocator, SpriteEncoder *encoder)
{
	assert(type != SpriteType::MapGen || IsMapgenSpriteID(sprite));
	assert(type < SpriteType::Invalid);

	if (!SpriteExists(sprite)) {
		Debug(sprite, 1, "Tried to load non-existing sprite #{}. Probable cause: Wrong/missing NewGRFs", sprite);

		/* SPR_IMG_QUERY is a BIG FAT RED ? */
		sprite = SPR_IMG_QUERY;
	}

	SpriteCache *sc = GetSpriteCache(sprite);

	if (sc->GetType() != type) return HandleInvalidSpriteRequest(sprite, type, sc, allocator);

	if (allocator == nullptr && encoder == nullptr) {
		/* Load sprite into/from spritecache */
		CacheSpriteAllocator cache_allocator;

		if (type != SpriteType::Normal) zoom_levels = LOW_ZOOM_ALL_BITS;

		/* Load the sprite, if it is not loaded, yet */
		if (sc->GetPtr() == nullptr) {
			[[maybe_unused]] void *ptr = ReadSprite(sc, sprite, type, cache_allocator, nullptr, zoom_levels);
			assert(ptr == cache_allocator.last_sprite_allocation.GetPtr());
			sc->Assign(std::move(cache_allocator.last_sprite_allocation));
		} else if ((sc->total_missing_zoom_levels & zoom_levels).Any()) {
			[[maybe_unused]] void *ptr = ReadSprite(sc, sprite, type, cache_allocator, nullptr, sc->total_missing_zoom_levels & zoom_levels);
			assert(ptr == cache_allocator.last_sprite_allocation.GetPtr());
			sc->Append(std::move(cache_allocator.last_sprite_allocation));
		}

		if (type != SpriteType::Recolour) {
			uint8_t lvls = zoom_levels.base();
			Sprite *sp = (Sprite *)sc->GetPtr();
			while (lvls != 0 && sp != nullptr) {
				uint8_t usable = ~sp->missing_zoom_levels.base();
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
		return ReadSprite(sc, sprite, type, *allocator, encoder, LOW_ZOOM_ALL_BITS);
	}
}

#if !defined(DEDICATED)
/**
 * Reads a sprite and finds its most representative colour.
 * @param sprite Sprite to read.
 * @param palette_id Palette for remapping colours.
 * @return if blitter supports 32bpp, average Colour.data else a palette index.
 */
uint32_t GetSpriteMainColour(SpriteID sprite_id, PaletteID palette_id)
{
	if (!SpriteExists(sprite_id)) return 0;

	SpriteCache *sc = GetSpriteCache(sprite_id);
	if (sc->GetType() != SpriteType::Normal) return 0;

	const uint8_t * const remap = (palette_id == PAL_NONE ? nullptr : GetNonSprite(GB(palette_id, 0, PALETTE_WIDTH), SpriteType::Recolour));

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SpriteLoader::SpriteCollection sprites;
	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	const uint8_t screen_depth = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	auto zoom_mask = [&](bool is32bpp) -> LowZoomLevels {
		return static_cast<LowZoomLevels>(1 << FindFirstBit(GB(sc->flags, is32bpp ? SCC_32BPP_ZOOM_START : SCC_PAL_ZOOM_START, 6)));
	};

	auto check_32bpp = [&]() -> std::optional<uint32_t> {
		LowZoomLevels sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, SpriteType::Normal, true, sc->count, sc->flags, zoom_mask(true)).loaded_sprites;
		if (sprite_avail.Any()) {
			SpriteLoader::Sprite *sprite = &sprites[sprite_avail.FindFirstBit()];
			/* Return the average colour. */
			uint32_t r = 0, g = 0, b = 0, cnt = 0;
			SpriteLoader::CommonPixel *pixel = sprite->data;
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				if (pixel->a != 0) {
					if (pixel->m != 0) {
						uint8_t m = pixel->m;
						if (remap != nullptr) m = remap[m];

						/* Get brightest value */
						uint8_t rgb_max = std::max({pixel->r, pixel->g, pixel->b});

						/* Black pixel (8bpp or old 32bpp image), so use default value */
						if (rgb_max == 0) rgb_max = DEFAULT_BRIGHTNESS;

						/* Convert the mapping channel to a RGB value */
						const Colour c = AdjustBrightness(_cur_palette.palette[m], rgb_max);

						if (c.a != 0) {
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
		return std::nullopt;
	};

	/* 32bpp screen: Try to read the 32bpp sprite first. */
	if (screen_depth == 32 && sc->GetHasNonPalette()) {
		auto result = check_32bpp();
		if (result.has_value()) return *result;
	}

	/* No 32bpp, try 8bpp. */
	LowZoomLevels sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, SpriteType::Normal, false, sc->count, sc->flags, zoom_mask(false)).loaded_sprites;
	if (sprite_avail.Any()) {
		SpriteLoader::Sprite *sprite = &sprites[sprite_avail.FindFirstBit()];
		SpriteLoader::CommonPixel *pixel = sprite->data;
		if (screen_depth == 32) {
			/* Return the average colour. */
			uint32_t r = 0, g = 0, b = 0, cnt = 0;
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				if (pixel->a != 0) {
					const uint col_index = remap != nullptr ? remap[pixel->m] : pixel->m;
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
			std::array<uint, 256> counts{};
			for (uint x = sprite->width * sprite->height; x != 0; x--) {
				if (pixel->a != 0) {
					counts[remap != nullptr ? remap[pixel->m] : pixel->m]++;
				}
				pixel++;
			}
			return std::max_element(counts.begin(), counts.end()) - counts.begin();
		}
	}

	/* 8bpp screen: As a fallback, try to read the 32bpp sprite, and then convert the average colour to an 8bpp index. */
	if (screen_depth != 32 && sc->GetHasNonPalette()) {
		auto result = check_32bpp();
		if (result.has_value()) return GetNearestColourIndex(Colour(*result));
	}

	return 0;
}
#endif /* !DEDICATED */

void GfxInitSpriteMem()
{
	/* Reset the spritecache 'pool' */
	_spritecache.clear();
	_sprite_files.clear();
	_recolour_cache.Clear();
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

void GfxClearSpriteCacheLoadIndex()
{
	_recolour_cache.ClearIndex();
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

void DumpSpriteCacheStats(format_target &buffer)
{
	uint target_size = GetTargetSpriteSize();
	buffer.format("Sprite cache: entries: {}, size: {}, target: {}, percent used: {:.1f}%\n",
			_spritecache.size(), _spritecache_bytes_used, target_size, (100.0f * _spritecache_bytes_used) / target_size);

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

		if (entry.GetType() == SpriteType::Recolour) continue;

		if (entry.GetPtr() != nullptr) have_data++;
		if (entry.GetHasPalette()) have_8bpp++;
		if (entry.GetHasNonPalette()) have_32bpp++;

		if (entry.GetType() == SpriteType::Normal) {
			if (entry.total_missing_zoom_levels.Any()) have_partial_zoom++;
			uint depth = 0;
			const Sprite *p = (const Sprite *)entry.GetPtr();
			while (p != nullptr) {
				depth++;
				p = p->next;
			}
			if (depth < lengthof(depths)) depths[depth]++;
		}
	}
	buffer.format("  Normal: {}, MapGen: {}, Font: {}, Recolour: {}\n",
			types[(uint)SpriteType::Normal], types[(uint)SpriteType::MapGen], types[(uint)SpriteType::Font], types[(uint)SpriteType::Recolour]);
	buffer.format("  Data loaded: {}, Recolour loaded: {}, Warned: {}, 8bpp: {}, 32bpp: {}\n",
			have_data, _recolour_cache.GetAllocationCount(), have_warned, have_8bpp, have_32bpp);
	buffer.format("  Cache prune events: {}, pruned entry total: {}, pruned data total: {}\n",
			_spritecache_prune_events, _spritecache_prune_entries, _spritecache_prune_total);
	buffer.append("  Normal:\n");
	buffer.format("    Partial zoom: {}\n", have_partial_zoom);
	for (uint i = 0; i < lengthof(depths); i++) {
		if (depths[i] > 0) buffer.format("    Data depth {}: {}\n", i, depths[i]);
	}
}

/* static */ SpriteCollMap<ReusableBuffer<SpriteLoader::CommonPixel>> SpriteLoader::Sprite::buffer;
