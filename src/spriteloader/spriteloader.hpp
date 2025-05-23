/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spriteloader.hpp Base for loading sprites. */

#ifndef SPRITELOADER_HPP
#define SPRITELOADER_HPP

#include "../core/alloc_type.hpp"
#include "../core/enum_type.hpp"
#include "../gfx_type.h"
#include "sprite_file_type.hpp"
#include <array>

struct Sprite;

/** The different colour components a sprite can have. */
enum SpriteColourComponent : uint8_t {
	SCC_RGB   = 1 << 0, ///< Sprite has RGB.
	SCC_ALPHA = 1 << 1, ///< Sprite has alpha.
	SCC_PAL   = 1 << 2, ///< Sprite has palette data.
	SCC_MASK  = SCC_RGB | SCC_ALPHA | SCC_PAL, ///< Mask of valid colour bits.
};
DECLARE_ENUM_AS_BIT_SET(SpriteColourComponent)

struct SpriteLoaderResult {
	uint8_t loaded_sprites = 0;  ///< Bit mask of the zoom levels successfully loaded or 0 if no sprite could be loaded.
	uint8_t avail_8bpp = 0;
	uint8_t avail_32bpp = 0;

	void Apply(const SpriteLoaderResult &other)
	{
		this->loaded_sprites |= other.loaded_sprites;
		this->avail_8bpp |= other.avail_8bpp;
		this->avail_32bpp |= other.avail_32bpp;
	}
};

/** Interface for the loader of our sprites. */
class SpriteLoader {
public:
	/** Definition of a common pixel in OpenTTD's realm. */
	struct CommonPixel {
		uint8_t r;  ///< Red-channel
		uint8_t g;  ///< Green-channel
		uint8_t b;  ///< Blue-channel
		uint8_t a;  ///< Alpha-channel
		uint8_t m;  ///< Remap-channel
	};

	/**
	 * Structure for passing information from the sprite loader to the blitter.
	 * You can only use this struct once at a time when using AllocateData to
	 * allocate the memory as that will always return the same memory address.
	 * This to prevent thousands of malloc + frees just to load a sprite.
	 */
	struct Sprite {
		uint16_t height;                 ///< Height of the sprite
		uint16_t width;                  ///< Width of the sprite
		int16_t x_offs;                  ///< The x-offset of where the sprite will be drawn
		int16_t y_offs;                  ///< The y-offset of where the sprite will be drawn
		SpriteType type;                 ///< The sprite type
		SpriteColourComponent colours;   ///< The colour components of the sprite with useful information.
		SpriteLoader::CommonPixel *data; ///< The sprite itself

		/**
		 * Allocate the sprite data of this sprite.
		 * @param zoom Zoom level to allocate the data for.
		 * @param size the minimum size of the data field.
		 */
		void AllocateData(ZoomLevel zoom, size_t size) { this->data = Sprite::buffer[zoom].ZeroAllocate(size); }
	private:
		/** Allocated memory to pass sprite data around */
		static ReusableBuffer<SpriteLoader::CommonPixel> buffer[ZOOM_LVL_SPR_COUNT];
	};

	/**
	 * Type defining a collection of sprites, one for each zoom level.
	 */
	using SpriteCollection = std::array<Sprite, ZOOM_LVL_SPR_COUNT>;

	/**
	 * Load a sprite from the disk and return a sprite struct which is the same for all loaders.
	 * @param[out] sprite The sprites to fill with data.
	 * @param file_slot   The file "descriptor" of the file we read from.
	 * @param file_pos    The position within the file the image begins.
	 * @param sprite_type The type of sprite we're trying to load.
	 * @param load_32bpp  True if 32bpp sprites should be loaded, false for a 8bpp sprite.
	 * @param control_flags Control flags, see SpriteCacheCtrlFlags.
	 * @return SpriteLoaderResult. loaded_sprites field is a bit mask of the zoom levels successfully loaded or 0 if no sprite could be loaded.
	 */
	virtual SpriteLoaderResult LoadSprite(SpriteLoader::SpriteCollection &sprite, SpriteFile &file, size_t file_pos, SpriteType sprite_type, bool load_32bpp, uint count, uint16_t control_flags, uint8_t zoom_levels) = 0;

	virtual ~SpriteLoader() = default;
};

/** Interface for something that can allocate memory for a sprite. */
class SpriteAllocator {
public:
	virtual ~SpriteAllocator() = default;

	/**
	 * Allocate memory for a sprite.
	 * @tparam T Type to return memory as.
	 * @param size Size of memory to allocate in bytes.
	 * @return Pointer to allocated memory.
	 */
	template <typename T>
	T *Allocate(size_t size)
	{
		return static_cast<T *>(this->AllocatePtr(size));
	}

protected:
	/**
	 * Allocate memory for a sprite.
	 * @param size Size of memory to allocate.
	 * @return Pointer to allocated memory.
	 */
	virtual void *AllocatePtr(size_t size) = 0;
};

/** Interface for something that can encode a sprite. */
class SpriteEncoder {
	bool supports_missing_zoom_levels = false;
	bool supports_32bpp = false;
	bool no_data_required = false;

protected:
	inline void SetSupportsMissingZoomLevels(bool supported)
	{
		this->supports_missing_zoom_levels = supported;
	}

	inline void SetIs32BppSupported(bool supported)
	{
		this->supports_32bpp = supported;
	}

	inline void SetNoSpriteDataRequired(bool not_required)
	{
		this->no_data_required = not_required;
	}

public:

	virtual ~SpriteEncoder() = default;

	inline bool SupportsMissingZoomLevels() const
	{
		return this->supports_missing_zoom_levels;
	}

	inline bool NoSpriteDataRequired() const
	{
		return this->no_data_required;
	}

	/**
	 * Can the sprite encoder make use of RGBA sprites?
	 */
	inline bool Is32BppSupported() const
	{
		return this->supports_32bpp;
	}

	/**
	 * Convert a sprite from the loader to our own format.
	 */
	virtual Sprite *Encode(const SpriteLoader::SpriteCollection &sprite, SpriteAllocator &allocator) = 0;

	/**
	 * Get the value which the height and width on a sprite have to be aligned by.
	 * @return The needed alignment or 0 if any alignment is accepted.
	 */
	virtual uint GetSpriteAlignment()
	{
		return 0;
	}
};
#endif /* SPRITELOADER_HPP */
