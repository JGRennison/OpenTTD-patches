/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file bitmap_type.hpp Bitmap functions. */

#ifndef BITMAP_TYPE_HPP
#define BITMAP_TYPE_HPP

#include "tilearea_type.h"
#include "core/bitmath_func.hpp"
#include "core/geometry_type.hpp"
#include "core/math_func.hpp"
#include <limits>
#include <vector>

/** Represents a tile area containing containing individually set tiles.
 * Each tile must be contained within the preallocated area.
 * A std::vector is used to mark which tiles are contained.
 */
class BitmapTileArea : public TileArea {
	friend struct BitmapTileIterator;

protected:
	using BlockT = uint32_t;
	static constexpr uint BLOCK_BITS = std::numeric_limits<BlockT>::digits;

	std::vector<BlockT> data;

	inline uint RowPitch() const { return CeilDivT<uint>(this->w, BLOCK_BITS); }

	inline void ResetData()
	{
		this->data.assign(this->h * this->RowPitch(), 0);
	}

	inline std::pair<uint, uint> Index(uint x, uint y) const
	{
		return { (y * this->RowPitch()) + (x / BLOCK_BITS), x % BLOCK_BITS };
	}

	inline std::pair<uint, uint> Index(TileIndex tile) const { return this->Index(TileX(tile) - TileX(this->tile), TileY(tile) - TileY(this->tile)); }

public:
	BitmapTileArea()
	{
		this->tile = INVALID_TILE;
		this->w = 0;
		this->h = 0;
	}

	BitmapTileArea(const TileArea &ta)
	{
		this->tile = ta.tile;
		this->w = ta.w;
		this->h = ta.h;
		this->ResetData();
	}

	/**
	 * Reset and clear the BitmapTileArea.
	 */
	void Reset()
	{
		this->tile = INVALID_TILE;
		this->w = 0;
		this->h = 0;
		this->data.clear();
	}

	/**
	 * Initialize the BitmapTileArea with the specified Rect.
	 * @param rect Rect to use.
	 */
	void Initialize(const Rect &r)
	{
		this->tile = TileXY(r.left, r.top);
		this->w = r.Width();
		this->h = r.Height();
		this->ResetData();
	}

	void Initialize(const TileArea &ta)
	{
		this->tile = ta.tile;
		this->w = ta.w;
		this->h = ta.h;
		this->ResetData();
	}

	/**
	 * Add a tile as part of the tile area.
	 * @param tile Tile to add.
	 */
	inline void SetTile(TileIndex tile)
	{
		assert(this->Contains(tile));
		auto pos = this->Index(tile);
		SetBit(this->data[pos.first], pos.second);
	}

	/**
	 * Clear a tile from the tile area.
	 * @param tile Tile to clear
	 */
	inline void ClrTile(TileIndex tile)
	{
		assert(this->Contains(tile));
		auto pos = this->Index(tile);
		ClrBit(this->data[pos.first], pos.second);
	}

	void SetTiles(const TileArea &area);

	/**
	 * Test if a tile is part of the tile area.
	 * @param tile Tile to check
	 */
	inline bool HasTile(TileIndex tile) const
	{
		if (!this->Contains(tile)) return false;
		auto pos = this->Index(tile);
		return HasBit(this->data[pos.first], pos.second);
	}

	inline bool operator==(const BitmapTileArea &other) const
	{
		return TileArea::operator==(other) && this->data == other.data;
	}
};

/** Iterator to iterate over all tiles belonging to a bitmaptilearea. */
class BitmapTileIterator : public TileIterator {
protected:
	BitmapTileArea::BlockT block;       ///< Current block data.
	const BitmapTileArea::BlockT *next; ///< Next block pointer.
	uint block_x;                       ///< The current 'x' position in the block iteration.
	uint y;                             ///< The current 'y' position in the rectangle.
	TileIndex row_start;                ///< Row start tile.
	uint pitch;                         ///< The row pitch.

	void advance()
	{
		while (this->block == 0) {
			this->block_x++;
			if (this->block_x == this->pitch) {
				/* Next row */
				this->y--;
				if (this->y == 0) {
					/* End */
					this->tile = INVALID_TILE;
					return;
				}
				this->block_x = 0;
				this->row_start += MapSizeX();
			}
			/* Read next block and advance next pointer */
			this->block = *this->next;
			this->next++;
		}

		uint8_t bit = FindFirstBit(this->block);
		this->block = KillFirstBit(this->block);
		this->tile = this->row_start + (this->block_x * BitmapTileArea::BLOCK_BITS) + bit;
		return;
	}

public:
	/**
	 * Construct the iterator.
	 * @param bitmap BitmapTileArea to iterate.
	 */
	BitmapTileIterator(const BitmapTileArea &bitmap) : TileIterator(), block_x(0), y(bitmap.h), row_start(bitmap.tile), pitch(bitmap.RowPitch())
	{
		if (!bitmap.data.empty()) {
			this->block = bitmap.data[0];
			this->next = bitmap.data.data() + 1;
			this->advance();
		} else {
			this->block = 0;
			this->next = nullptr;
		}
	}

	inline TileIterator& operator ++() override
	{
		assert(this->tile != INVALID_TILE);
		this->advance();
		return *this;
	}

	std::unique_ptr<TileIterator> Clone() const override
	{
		return std::make_unique<BitmapTileIterator>(*this);
	}
};

#endif /* BITMAP_TYPE_HPP */
