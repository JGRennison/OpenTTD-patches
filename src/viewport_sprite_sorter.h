/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file viewport_sprite_sorter.h Types related to sprite sorting. */

#ifndef VIEWPORT_SPRITE_SORTER_H
#define VIEWPORT_SPRITE_SORTER_H

#include "core/bitmath_func.hpp"
#include "gfx_type.h"
#include <vector>

/** Parent sprite that should be drawn */
#ifdef _MSC_VER
struct __declspec(align(16)) ParentSpriteToDraw {
#else
struct __attribute__ ((aligned (16))) ParentSpriteToDraw {
#endif
	/* Block of 16B loadable in xmm register */
	int32_t xmin;                   ///< minimal world X coordinate of bounding box
	int32_t ymin;                   ///< minimal world Y coordinate of bounding box
	int32_t zmin;                   ///< minimal world Z coordinate of bounding box
	int32_t x;                      ///< screen X coordinate of sprite

	/* Second block of 16B loadable in xmm register */
	int32_t xmax;                   ///< maximal world X coordinate of bounding box
	int32_t ymax;                   ///< maximal world Y coordinate of bounding box
	int32_t zmax;                   ///< maximal world Z coordinate of bounding box
	int32_t y;                      ///< screen Y coordinate of sprite

	SpriteID image;                 ///< sprite to draw
	PaletteID pal;                  ///< palette to use
#ifdef POINTER_IS_64BIT
	int32_t sub_idx;                ///< only draw a rectangular part of the sprite (store the actual pointer elsewhere to save space in this struct)
#else
	const SubSprite *sub_ptr;       ///< only draw a rectangular part of the sprite
#endif
	uint8_t special_flags;          ///< special flags

	/* 3 bytes spare! */

	int32_t left;                   ///< minimal screen X coordinate of sprite (= x + sprite->x_offs), reference point for child sprites
	int32_t top;                    ///< minimal screen Y coordinate of sprite (= y + sprite->y_offs), reference point for child sprites

	int32_t first_child;            ///< the first child to draw.
	uint16_t width;                 ///< sprite width
	uint16_t height;                ///< sprite height, bit 15: comparison_done: used during sprite sorting: true if sprite has been compared with all other sprites

	bool IsComparisonDone() const { return HasBit(this->height, 15); }
	void SetComparisonDone(bool done) { SB(this->height, 15, 1, done ? 1 : 0); }
};
static_assert((sizeof(ParentSpriteToDraw) % 16) == 0);
static_assert(sizeof(ParentSpriteToDraw) <= 64);

typedef std::vector<ParentSpriteToDraw*> ParentSpriteToSortVector;

#ifdef POINTER_IS_64BIT
struct ParentSpriteToDrawSubSpriteHolder {
	std::vector<const SubSprite *> subsprites;

	const SubSprite *Get(const ParentSpriteToDraw *ps) const
	{
		return ps->sub_idx >= 0 ? this->subsprites[ps->sub_idx] : nullptr;
	}

	void Set(ParentSpriteToDraw *ps, const SubSprite *sub)
	{
		if (sub == nullptr) {
			ps->sub_idx = -1;
		} else {
			ps->sub_idx = (int32_t)this->subsprites.size();
			this->subsprites.push_back(sub);
		}
	}

	void Clear()
	{
		this->subsprites.clear();
	}
};
#else
struct ParentSpriteToDrawSubSpriteHolder {
	const SubSprite *Get(const ParentSpriteToDraw *ps) const { return ps->sub_ptr; }
	void Set(ParentSpriteToDraw *ps, const SubSprite *sub) { ps->sub_ptr = sub; }
	void Clear() {}
};
#endif

/** Type for method for checking whether a viewport sprite sorter exists. */
typedef bool (*VpSorterChecker)();
/** Type for the actual viewport sprite sorter. */
typedef void (*VpSpriteSorter)(ParentSpriteToSortVector *psd);

bool ViewportSortParentSpritesSpecial(ParentSpriteToDraw *ps, ParentSpriteToDraw *ps2, ParentSpriteToDraw **psd, ParentSpriteToDraw **psd2);

#ifdef WITH_SSE
bool ViewportSortParentSpritesSSE41Checker();
void ViewportSortParentSpritesSSE41(ParentSpriteToSortVector *psdv);
#endif

void InitializeSpriteSorter();

#endif /* VIEWPORT_SPRITE_SORTER_H */
