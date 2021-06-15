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

#include "table/sprites.h"
#include "table/strings.h"
#include "table/palette_convert.h"

#include "3rdparty/cpp-btree/btree_map.h"

#include <vector>
#include <algorithm>

#include "safeguards.h"

/* Default of 4MB spritecache */
uint _sprite_cache_size = 4;

static size_t _spritecache_bytes_used = 0;
static uint32 _sprite_lru_counter;

PACK_N(class SpriteDataBuffer {
	void *ptr = nullptr;
	uint32 size = 0;

public:
	void *GetPtr() { return this->ptr; }
	uint32 GetSize() { return this->size; }

	void Allocate(uint32 size)
	{
		_spritecache_bytes_used -= this->size;
		free(this->ptr);
		this->ptr = MallocT<byte>(size);
		this->size = size;
		_spritecache_bytes_used += this->size;
	}

	void Clear()
	{
		_spritecache_bytes_used -= this->size;
		free(this->ptr);
		this->ptr = nullptr;
		this->size = 0;
	}

	SpriteDataBuffer() {}

	SpriteDataBuffer(uint32 size) { this->Allocate(size); }

	SpriteDataBuffer(SpriteDataBuffer &&other) noexcept
	{
		*this = std::move(other);
	}

	~SpriteDataBuffer()
	{
		this->Clear();
	}

	SpriteDataBuffer& operator=(SpriteDataBuffer &&other) noexcept
	{
		this->Clear();
		this->ptr = other.ptr;
		this->size = other.size;
		other.ptr = nullptr;
		other.size = 0;
		return *this;
	}
}, 4);

PACK_N(struct SpriteCache {
	SpriteFile *file;    ///< The file the sprite in this entry can be found in.
	size_t file_pos;
	SpriteDataBuffer buffer;
	uint32 id;
	uint32 lru;
	uint count;

	SpriteType type;     ///< In some cases a single sprite is misused by two NewGRFs. Once as real sprite and once as recolour sprite. If the recolour sprite gets into the cache it might be drawn as real sprite which causes enormous trouble.

	/**
	 * Bit      0:  warned           True iff the user has been warned about incorrect use of this sprite.
	 * Bit      1:  has_non_palette  True iff there is at least one non-paletter sprite present (such that 32bpp mode can be used).
	 */
	byte flags;

	void *GetPtr() { return this->buffer.GetPtr(); }

	SpriteType GetType() const { return this->type; }
	void SetType(SpriteType type) { this->type = type; }
	bool GetWarned() const { return HasBit(this->flags, 0); }
	void SetWarned(bool warned) { SB(this->flags, 0, 1, warned ? 1 : 0); }
	bool GetHasNonPalette() const { return HasBit(this->flags, 1); }
	void SetHasNonPalette(bool non_palette) { SB(this->flags, 1, 1, non_palette ? 1 : 0); }
}, 4);

static std::vector<SpriteCache> _spritecache;
static SpriteDataBuffer _last_sprite_allocation;
static std::vector<std::unique_ptr<SpriteFile>> _sprite_files;

static inline SpriteCache *GetSpriteCache(uint index)
{
	return &_spritecache[index];
}

static inline bool IsMapgenSpriteID(SpriteID sprite)
{
	return IsInsideMM(sprite, 4845, 4882);
}

static SpriteCache *AllocateSpriteCache(uint index)
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
static SpriteFile *GetCachedSpriteFileByName(const std::string &filename) {
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
		file = _sprite_files.emplace_back(new SpriteFile(filename, subdir, palette_remap)).get();
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
	if (!SpriteExists(sprite)) return ST_INVALID;
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
			if (sc->file == file) count++;
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

static bool ResizeSpriteIn(SpriteLoader::Sprite *sprite, ZoomLevel src, ZoomLevel tgt)
{
	uint8 scaled_1 = ScaleByZoom(1, (ZoomLevel)(src - tgt));

	/* Check for possible memory overflow. */
	if (sprite[src].width * scaled_1 > UINT16_MAX || sprite[src].height * scaled_1 > UINT16_MAX) return false;

	sprite[tgt].width  = sprite[src].width  * scaled_1;
	sprite[tgt].height = sprite[src].height * scaled_1;
	sprite[tgt].x_offs = sprite[src].x_offs * scaled_1;
	sprite[tgt].y_offs = sprite[src].y_offs * scaled_1;
	sprite[tgt].colours = sprite[src].colours;

	sprite[tgt].AllocateData(tgt, sprite[tgt].width * sprite[tgt].height);

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

static void ResizeSpriteOut(SpriteLoader::Sprite *sprite, ZoomLevel zoom)
{
	/* Algorithm based on 32bpp_Optimized::ResizeSprite() */
	sprite[zoom].width  = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].width,  zoom);
	sprite[zoom].height = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].height, zoom);
	sprite[zoom].x_offs = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].x_offs, zoom);
	sprite[zoom].y_offs = UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].y_offs, zoom);
	sprite[zoom].colours = sprite[ZOOM_LVL_NORMAL].colours;

	sprite[zoom].AllocateData(zoom, sprite[zoom].height * sprite[zoom].width);

	SpriteLoader::CommonPixel *dst = sprite[zoom].data;
	const SpriteLoader::CommonPixel *src = sprite[zoom - 1].data;
#ifdef WITH_ASSERT
	const SpriteLoader::CommonPixel *src_end = src + sprite[zoom - 1].height * sprite[zoom - 1].width;
#endif

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

	/* Copy source data and reallocate sprite memory. */
	SpriteLoader::CommonPixel *src_data = MallocT<SpriteLoader::CommonPixel>(sprite->width * sprite->height);
	MemCpyT(src_data, sprite->data, sprite->width * sprite->height);
	sprite->AllocateData(zoom, width * height);

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
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			min_xoffs = std::min(min_xoffs, ScaleByZoom(sprite[zoom].x_offs, zoom));
			min_yoffs = std::min(min_yoffs, ScaleByZoom(sprite[zoom].y_offs, zoom));
		}
	}

	/* Get maximum dimensions taking necessary padding at the top left into account. */
	int max_width  = INT32_MIN;
	int max_height = INT32_MIN;
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_END; zoom++) {
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
	for (ZoomLevel zoom = ZOOM_LVL_BEGIN; zoom != ZOOM_LVL_END; zoom++) {
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

static bool ResizeSprites(SpriteLoader::Sprite *sprite, unsigned int sprite_avail, SpriteEncoder *encoder)
{
	/* Create a fully zoomed image if it does not exist */
	ZoomLevel first_avail = static_cast<ZoomLevel>(FIND_FIRST_BIT(sprite_avail));
	if (first_avail != ZOOM_LVL_NORMAL) {
		if (!ResizeSpriteIn(sprite, first_avail, ZOOM_LVL_NORMAL)) return false;
		SetBit(sprite_avail, ZOOM_LVL_NORMAL);
	}

	/* Pad sprites to make sizes match. */
	if (!PadSprites(sprite, sprite_avail, encoder)) return false;

	/* Create other missing zoom levels */
	for (ZoomLevel zoom = ZOOM_LVL_OUT_2X; zoom != ZOOM_LVL_END; zoom++) {
		if (HasBit(sprite_avail, zoom)) {
			/* Check that size and offsets match the fully zoomed image. */
			assert(sprite[zoom].width  == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].width,  zoom));
			assert(sprite[zoom].height == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].height, zoom));
			assert(sprite[zoom].x_offs == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].x_offs, zoom));
			assert(sprite[zoom].y_offs == UnScaleByZoom(sprite[ZOOM_LVL_NORMAL].y_offs, zoom));
		}

		/* Zoom level is not available, or unusable, so create it */
		if (!HasBit(sprite_avail, zoom)) ResizeSpriteOut(sprite, zoom);
	}

	return  true;
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
	static const uint RECOLOUR_SPRITE_SIZE = 257;
	byte *dest = (byte *)AllocSprite(std::max(RECOLOUR_SPRITE_SIZE, num));

	if (file.NeedsPaletteRemap()) {
		byte *dest_tmp = AllocaM(byte, std::max(RECOLOUR_SPRITE_SIZE, num));

		/* Only a few recolour sprites are less than 257 bytes */
		if (num < RECOLOUR_SPRITE_SIZE) memset(dest_tmp, 0, RECOLOUR_SPRITE_SIZE);
		file.ReadBlock(dest_tmp, num);

		/* The data of index 0 is never used; "literal 00" according to the (New)GRF specs. */
		for (uint i = 1; i < RECOLOUR_SPRITE_SIZE; i++) {
			dest[i] = _palmap_w2d[dest_tmp[_palmap_d2w[i - 1] + 1]];
		}
	} else {
		file.ReadBlock(dest, num);
	}

	return dest;
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
static void *ReadSprite(const SpriteCache *sc, SpriteID id, SpriteType sprite_type, AllocatorProc *allocator, SpriteEncoder *encoder)
{
	/* Use current blitter if no other sprite encoder is given. */
	if (encoder == nullptr) encoder = BlitterFactory::GetCurrentBlitter();

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SCOPE_INFO_FMT([&], "ReadSprite: pos: " PRINTF_SIZE ", id: %u, file: (%s), type: %u", file_pos, id, file.GetSimplifiedFilename().c_str(), sprite_type);

	assert(sprite_type != ST_RECOLOUR);
	assert(IsMapgenSpriteID(id) == (sprite_type == ST_MAPGEN));
	assert(sc->GetType() == sprite_type);

	DEBUG(sprite, 9, "Load sprite %d", id);

	SpriteLoader::Sprite sprite[ZOOM_LVL_COUNT];
	uint8 sprite_avail = 0;
	sprite[ZOOM_LVL_NORMAL].type = sprite_type;

	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	if (sprite_type != ST_MAPGEN && sc->GetHasNonPalette() && encoder->Is32BppSupported()) {
		/* Try for 32bpp sprites first. */
		sprite_avail = sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, true, sc->count);
	}
	if (sprite_avail == 0) {
		sprite_avail = sprite_loader.LoadSprite(sprite, file, file_pos, sprite_type, false, sc->count);
	}

	if (sprite_avail == 0) {
		if (sprite_type == ST_MAPGEN) return nullptr;
		if (id == SPR_IMG_QUERY) usererror("Okay... something went horribly wrong. I couldn't load the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, ST_NORMAL, allocator, encoder);
	}

	if (sprite_type == ST_MAPGEN) {
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

		SpriteLoader::CommonPixel *src = sprite[ZOOM_LVL_NORMAL].data;
		byte *dest = s->data;
		while (num-- > 0) {
			*dest++ = src->m;
			src++;
		}

		return s;
	}

	if (!ResizeSprites(sprite, sprite_avail, encoder)) {
		if (id == SPR_IMG_QUERY) usererror("Okay... something went horribly wrong. I couldn't resize the fallback sprite. What should I do?");
		return (void*)GetRawSprite(SPR_IMG_QUERY, ST_NORMAL, allocator, encoder);
	}

	if (sprite->type == ST_FONT && ZOOM_LVL_FONT != ZOOM_LVL_NORMAL) {
		/* Make ZOOM_LVL_NORMAL be ZOOM_LVL_FONT */
		sprite[ZOOM_LVL_NORMAL].width  = sprite[ZOOM_LVL_FONT].width;
		sprite[ZOOM_LVL_NORMAL].height = sprite[ZOOM_LVL_FONT].height;
		sprite[ZOOM_LVL_NORMAL].x_offs = sprite[ZOOM_LVL_FONT].x_offs;
		sprite[ZOOM_LVL_NORMAL].y_offs = sprite[ZOOM_LVL_FONT].y_offs;
		sprite[ZOOM_LVL_NORMAL].data   = sprite[ZOOM_LVL_FONT].data;
		sprite[ZOOM_LVL_NORMAL].colours = sprite[ZOOM_LVL_FONT].colours;
	}

	return encoder->Encode(sprite, allocator);
}

struct GrfSpriteOffset {
	size_t file_pos;
	uint count;
	bool has_non_palette;
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
				offset.has_non_palette = false;
			}
			offset.count++;
			prev_id = id;
			uint length = file.ReadDword();
			if (length > 0) {
				if ((file.ReadByte() & SCC_MASK) != SCC_PAL) offset.has_non_palette = true;
				length--;
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
	bool has_non_palette = false;
	if (grf_type == 0xFF) {
		/* Some NewGRF files have "empty" pseudo-sprites which are 1
		 * byte long. Catch these so the sprites won't be displayed. */
		if (num == 1) {
			file.ReadByte();
			return false;
		}
		type = ST_RECOLOUR;
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
			has_non_palette = iter->second.has_non_palette;
		} else {
			file_pos = SIZE_MAX;
		}
		type = ST_NORMAL;
	} else {
		file.SkipBytes(7);
		type = SkipSpriteData(file, grf_type, num - 8) ? ST_NORMAL : ST_INVALID;
		/* Inline sprites are not supported for container version >= 2. */
		if (file.GetContainerVersion() >= 2) return false;
	}

	if (type == ST_INVALID) return false;

	if (load_index >= MAX_SPRITES) {
		usererror("Tried to load too many sprites (#%d; max %d)", load_index, MAX_SPRITES);
	}

	bool is_mapgen = IsMapgenSpriteID(load_index);

	if (is_mapgen) {
		if (type != ST_NORMAL) usererror("Uhm, would you be so kind not to load a NewGRF that changes the type of the map generator sprites?");
		type = ST_MAPGEN;
	}

	SpriteCache *sc = AllocateSpriteCache(load_index);
	sc->file = &file;
	sc->file_pos = file_pos;
	if (data != nullptr) {
		assert(data == _last_sprite_allocation.GetPtr());
		sc->buffer = std::move(_last_sprite_allocation);
	}
	sc->lru = 0;
	sc->id = file_sprite_id;
	sc->count = count;
	sc->SetType(type);
	sc->flags = 0;
	if (has_non_palette) sc->SetHasNonPalette(true);

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
	GetSpriteCache(item)->buffer.Clear();
}

static void DeleteEntriesFromSpriteCache(size_t target)
{
	const size_t initial_in_use = GetSpriteCacheUsage();

	struct SpriteInfo {
		uint32 lru;
		SpriteID id;
		uint32 size;

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

	SpriteID i = 0;
	for (; i != _spritecache.size() && candidate_bytes < target; i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() != ST_RECOLOUR && sc->GetPtr() != nullptr) {
			push({ sc->lru, i, sc->buffer.GetSize() });
			if (candidate_bytes >= target) break;
		}
	}
	for (; i != _spritecache.size(); i++) {
		SpriteCache *sc = GetSpriteCache(i);
		if (sc->GetType() != ST_RECOLOUR && sc->GetPtr() != nullptr && sc->lru <= candidates.front().lru) {
			push({ sc->lru, i, sc->buffer.GetSize() });
			while (!candidates.empty() && candidate_bytes - candidates.front().size >= target) {
				pop();
			}
		}
	}

	for (auto &it : candidates) {
		DeleteEntryFromSpriteCache(it.id);
	}

	DEBUG(sprite, 3, "DeleteEntriesFromSpriteCache, deleted: " PRINTF_SIZE ", freed: " PRINTF_SIZE ", in use: " PRINTF_SIZE " --> " PRINTF_SIZE ", delta: " PRINTF_SIZE ", requested: " PRINTF_SIZE,
			candidates.size(), candidate_bytes, initial_in_use, GetSpriteCacheUsage(), initial_in_use - GetSpriteCacheUsage(), target);
}

void IncreaseSpriteLRU()
{
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	uint target_size = (bpp > 0 ? _sprite_cache_size * bpp / 8 : 1) * 1024 * 1024;
	if (_spritecache_bytes_used > target_size) {
		DeleteEntriesFromSpriteCache(_spritecache_bytes_used - target_size + 512 * 1024);
	}

	/* Increase all LRU values */
	if (_sprite_lru_counter >= 0xC0000000) {
		SpriteID i;

		DEBUG(sprite, 3, "Fixing lru %u, inuse=" PRINTF_SIZE, _sprite_lru_counter, GetSpriteCacheUsage());

		for (i = 0; i != _spritecache.size(); i++) {
			SpriteCache *sc = GetSpriteCache(i);
			if (sc->GetPtr() != nullptr) {
				if (sc->lru > 0x80000000) {
					sc->lru -= 0x80000000;
				} else {
					sc->lru = 0;
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
 * For ST_FONT sprites, it is normal. In other cases, default sprite is loaded instead.
 * @param sprite ID of loaded sprite
 * @param requested requested sprite type
 * @param sc the currently known sprite cache for the requested sprite
 * @return fallback sprite
 * @note this function will do usererror() in the case the fallback sprite isn't available
 */
static void *HandleInvalidSpriteRequest(SpriteID sprite, SpriteType requested, SpriteCache *sc, AllocatorProc *allocator)
{
	static const char * const sprite_types[] = {
		"normal",        // ST_NORMAL
		"map generator", // ST_MAPGEN
		"character",     // ST_FONT
		"recolour",      // ST_RECOLOUR
	};

	SpriteType available = sc->GetType();
	if (requested == ST_FONT && available == ST_NORMAL) {
		if (sc->GetPtr() == nullptr) sc->SetType(ST_FONT);
		return GetRawSprite(sprite, sc->GetType(), allocator);
	}

	byte warning_level = sc->GetWarned() ? 6 : 0;
	sc->SetWarned(true);
	DEBUG(sprite, warning_level, "Tried to load %s sprite #%d as a %s sprite. Probable cause: NewGRF interference", sprite_types[available], sprite, sprite_types[requested]);

	switch (requested) {
		case ST_NORMAL:
			if (sprite == SPR_IMG_QUERY) usererror("Uhm, would you be so kind not to load a NewGRF that makes the 'query' sprite a non-normal sprite?");
			FALLTHROUGH;
		case ST_FONT:
			return GetRawSprite(SPR_IMG_QUERY, ST_NORMAL, allocator);
		case ST_RECOLOUR:
			if (sprite == PALETTE_TO_DARK_BLUE) usererror("Uhm, would you be so kind not to load a NewGRF that makes the 'PALETTE_TO_DARK_BLUE' sprite a non-remap sprite?");
			return GetRawSprite(PALETTE_TO_DARK_BLUE, ST_RECOLOUR, allocator);
		case ST_MAPGEN:
			/* this shouldn't happen, overriding of ST_MAPGEN sprites is checked in LoadNextSprite()
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
void *GetRawSprite(SpriteID sprite, SpriteType type, AllocatorProc *allocator, SpriteEncoder *encoder)
{
	assert(type != ST_MAPGEN || IsMapgenSpriteID(sprite));
	assert(type < ST_INVALID);

	if (!SpriteExists(sprite)) {
		DEBUG(sprite, 1, "Tried to load non-existing sprite #%d. Probable cause: Wrong/missing NewGRFs", sprite);

		/* SPR_IMG_QUERY is a BIG FAT RED ? */
		sprite = SPR_IMG_QUERY;
	}

	SpriteCache *sc = GetSpriteCache(sprite);

	if (sc->GetType() != type) return HandleInvalidSpriteRequest(sprite, type, sc, allocator);

	if (allocator == nullptr && encoder == nullptr) {
		/* Load sprite into/from spritecache */

		/* Update LRU */
		sc->lru = ++_sprite_lru_counter;

		/* Load the sprite, if it is not loaded, yet */
		if (sc->GetPtr() == nullptr) {
			void *ptr = ReadSprite(sc, sprite, type, AllocSprite, nullptr);
			assert(ptr == _last_sprite_allocation.GetPtr());
			sc->buffer = std::move(_last_sprite_allocation);
		}

		return sc->GetPtr();
	} else {
		/* Do not use the spritecache, but a different allocator. */
		return ReadSprite(sc, sprite, type, allocator, encoder);
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
	if (sc->GetType() != ST_NORMAL) return 0;

	const byte * const remap = (palette_id == PAL_NONE ? nullptr : GetNonSprite(GB(palette_id, 0, PALETTE_WIDTH), ST_RECOLOUR) + 1);

	SpriteFile &file = *sc->file;
	size_t file_pos = sc->file_pos;

	SpriteLoader::Sprite sprites[ZOOM_LVL_COUNT];
	sprites[ZOOM_LVL_NORMAL].type = ST_NORMAL;
	SpriteLoaderGrf sprite_loader(file.GetContainerVersion());
	uint8 sprite_avail;
	const uint8 screen_depth = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	/* Try to read the 32bpp sprite first. */
	if (screen_depth == 32 && sc->GetHasNonPalette()) {
		sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, ST_NORMAL, true, sc->count);
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
	sprite_avail = sprite_loader.LoadSprite(sprites, file, file_pos, ST_NORMAL, false, sc->count);
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
		if (sc->GetType() != ST_RECOLOUR && sc->GetPtr() != nullptr) DeleteEntryFromSpriteCache(i);
	}

	VideoDriver::GetInstance()->ClearSystemSprites();
}

/* static */ ReusableBuffer<SpriteLoader::CommonPixel> SpriteLoader::Sprite::buffer[ZOOM_LVL_COUNT];
