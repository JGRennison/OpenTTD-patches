/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file spritecache_internal.h Internal functions to cache sprites in memory. */

#ifndef SPRITECACHE_INTERNAL_H
#define SPRITECACHE_INTERNAL_H

#include "stdafx.h"

#include "core/math_func.hpp"
#include "gfx_type.h"
#include "spriteloader/spriteloader.hpp"

#include "table/sprites.h"

/* These declarations are internal to spritecache but need to be exposed for unit-tests. */

extern size_t _spritecache_bytes_used;

static const uint RECOLOUR_SPRITE_SIZE = 257;

struct SpriteCache;

class SpriteDataBuffer {
	friend SpriteCache;

	std::unique_ptr<void, FreeDeleter> ptr;
	uint32_t size = 0;

public:
	void *GetPtr() { return this->ptr.get(); }
	uint32_t GetSize() { return this->size; }

	void Allocate(uint32_t size)
	{
		this->ptr.reset(MallocT<uint8_t>(size));
		this->size = size;
	}

	void Clear()
	{
		this->ptr.reset();
		this->size = 0;
	}
};

struct SpriteCache {
	SpriteFile *file;    ///< The file the sprite in this entry can be found in.
	size_t file_pos;

private:
	std::unique_ptr<void, NoOpDeleter> ptr;

public:
	uint32_t id;
	uint count;

	SpriteType type;     ///< In some cases a single sprite is misused by two NewGRFs. Once as real sprite and once as recolour sprite. If the recolour sprite gets into the cache it might be drawn as real sprite which causes enormous trouble.
	uint8_t total_missing_zoom_levels = 0; ///< Zoom levels missing entirely
	uint16_t flags;      ///< Control flags, see SpriteCacheCtrlFlags

	void *GetPtr() { return this->ptr.get(); }
	const void *GetPtr() const { return this->ptr.get(); }

	SpriteType GetType() const { return this->type; }
	void SetType(SpriteType type) { this->type = type; }
	bool GetWarned() const { return HasBit(this->flags, SCCF_WARNED); }
	void SetWarned(bool warned) { AssignBit(this->flags, SCCF_WARNED, warned); }
	bool GetHasPalette() const { return GB(this->flags, SCC_PAL_ZOOM_START, 6) != 0; }
	bool GetHasNonPalette() const { return GB(this->flags, SCC_32BPP_ZOOM_START, 6) != 0; }

private:
	void Deallocate()
	{
		if (!this->ptr) return;

		if (this->GetType() == SpriteType::Recolour) {
			_spritecache_bytes_used -= RECOLOUR_SPRITE_SIZE;
			free(this->ptr.release());
			return;
		}

		Sprite *p = (Sprite *)this->ptr.release();
		while (p != nullptr) {
			Sprite *next = p->next;
			_spritecache_bytes_used -= p->size;
			free(p);
			p = next;
		}
	}

	Sprite *GetSpritePtr() { return (Sprite *)this->ptr.get(); }

public:
	void Clear()
	{
		this->Deallocate();
		this->total_missing_zoom_levels = 0;
	}

	void RemoveByMissingZoomLevels(uint8_t lvls)
	{
		Sprite *base = this->GetSpritePtr();
		if (base == nullptr) {
			return;
		}
		if (base->missing_zoom_levels == lvls) {
			/* erase top level entry */
			this->ptr.reset(base->next);
			_spritecache_bytes_used -= base->size;
			free(base);
			base = this->GetSpritePtr();
		}
		if (base == nullptr) {
			this->total_missing_zoom_levels = 0;
			return;
		}
		this->total_missing_zoom_levels = base->missing_zoom_levels;
		Sprite *sp = base;
		while (sp != nullptr) {
			this->total_missing_zoom_levels &= sp->missing_zoom_levels;

			if (sp->next != nullptr && sp->next->missing_zoom_levels == lvls) {
				/* found entry to erase */
				_spritecache_bytes_used -= sp->next->size;
				Sprite *new_next = sp->next->next;
				free(sp->next);
				sp->next = new_next;
			}

			sp = sp->next;
		}
	}

	void Assign(SpriteDataBuffer &&other)
	{
		this->Clear();
		if (!other.ptr) return;

		this->ptr.reset(other.ptr.release());
		if (this->GetType() == SpriteType::Recolour) {
			_spritecache_bytes_used += RECOLOUR_SPRITE_SIZE;
		} else {
			this->GetSpritePtr()->size = other.size;
			_spritecache_bytes_used += other.size;
			if (this->GetType() == SpriteType::Normal) {
				this->total_missing_zoom_levels = this->GetSpritePtr()->missing_zoom_levels;
			}
		}
		other.size = 0;
	}

	void Append(SpriteDataBuffer &&other)
	{
		assert(this->GetType() == SpriteType::Normal);

		if (!this->ptr || this->total_missing_zoom_levels == UINT8_MAX) {
			/* Top level has no data or no zoom levels at all, it's safe to replace it because it cannot be cached for a render job */
			this->Assign(std::move(other));
			return;
		}

		Sprite *sp = (Sprite *)other.ptr.release();
		if (sp == nullptr) return;

		sp->size = other.size;

		Sprite *p = this->GetSpritePtr();
		while (p->next != nullptr) {
			p = p->next;
		}
		p->next = sp;
		this->total_missing_zoom_levels &= sp->missing_zoom_levels;
		_spritecache_bytes_used += other.size;
		other.size = 0;
	}

	~SpriteCache()
	{
		this->Clear();
	}

	SpriteCache() = default;
	SpriteCache(const SpriteCache &other) = delete;
	SpriteCache(SpriteCache &&other) = default;
	SpriteCache& operator=(const SpriteCache &other) = delete;
	SpriteCache& operator=(SpriteCache &&other) = default;
};

/** SpriteAllocator that allocates memory from the sprite cache. */
struct CacheSpriteAllocator final : public SpriteAllocator {
	SpriteDataBuffer last_sprite_allocation;

protected:
	void *AllocatePtr(size_t size) override;
};

inline bool IsMapgenSpriteID(SpriteID sprite)
{
	return IsInsideMM(sprite, SPR_MAPGEN_BEGIN, SPR_MAPGEN_END);
}

SpriteCache *AllocateSpriteCache(uint index);

#endif /* SPRITECACHE_INTERNAL_H */
