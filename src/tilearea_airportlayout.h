/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tilearea_airportlayout.h Class for iterating an airport layout on the map. */

#ifndef TILEAREA_AIRPORTLAYOUT_HPP
#define TILEAREA_AIRPORTLAYOUT_HPP

#include "newgrf_airport.h"

/** Iterator to iterate over all tiles belonging to an airport spec. */
class AirportTileTableIterator : public TileIterator {
private:
	std::span<const AirportTileTable> att; ///< The offsets.
	TileIndex base_tile; ///< The tile we base the offsets off.
	std::span<const AirportTileTable>::iterator iter;
	bool include_special; ///< Include special tiles.

public:
	/**
	 * Construct the iterator.
	 * @param att The TileTable we want to iterate over.
	 * @param base_tile The basetile for all offsets.
	 */
	AirportTileTableIterator(std::span<const AirportTileTable> att, TileIndex base_tile, bool include_special)
		: TileIterator(base_tile + ToTileIndexDiff(att.front().ti))
		, att(att), base_tile(base_tile), iter(att.begin()), include_special(include_special)
	{
	}

	inline AirportTileTableIterator& operator ++()
	{
		do {
			++this->iter;
			if (this->iter == std::end(att)) {
				this->tile = INVALID_TILE;
			} else {
				this->tile = this->base_tile + ToTileIndexDiff(this->iter->ti);
			}
		} while (this->tile != INVALID_TILE && !this->include_special && this->GetAirportGfx() == AIRPORTGFX_CLEARTILE_SPECIALCHECK);
		return *this;
	}

	/**
	 * Get the AirportGfx for the current tile.
	 * @return The identifier of the graphics for this tile.
	 */
	AirportGfx GetAirportGfx() const
	{
		return this->iter->gfx;
	}
};

#endif /* TILEAREA_AIRPORTLAYOUT_HPP */
