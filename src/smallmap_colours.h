/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file smallmap_colours.h Colours used by smallmap. */

#ifndef SMALLMAP_COLOURS_H
#define SMALLMAP_COLOURS_H

#include "palette_func.h"
#include "tile_type.h"
#include "core/endian_func.hpp"

#define MKCOLOUR(x)         TO_LE32(x)

#define MKCOLOUR_XXXX(x)    (MKCOLOUR(0x01010101) * (uint)(x.p))
#define MKCOLOUR_0XX0(x)    (MKCOLOUR(0x00010100) * (uint)(x.p))
#define MKCOLOUR_X00X(x)    (MKCOLOUR(0x01000001) * (uint)(x.p))

#define MKCOLOUR_XYYX(x, y) (MKCOLOUR_X00X(x) | MKCOLOUR_0XX0(y))

#define MKCOLOUR_0000       MKCOLOUR_XXXX(PixelColour{0x00})
#define MKCOLOUR_F00F       MKCOLOUR_X00X(PixelColour{0xFF})
#define MKCOLOUR_FFFF       MKCOLOUR_XXXX(PixelColour{0xFF})

/** Colour scheme of the smallmap. */
struct SmallMapColourScheme {
	std::vector<uint32_t> height_colours; ///< Cached colours for each level in a map.
	std::span<const uint32_t> height_colours_base; ///< Base table for determining the colours
	uint32_t default_colour;             ///< Default colour of the land.
};

extern SmallMapColourScheme _heightmap_schemes[];

struct AndOr {
	uint32_t mor;
	uint32_t mand;
};

inline uint32_t ApplyMask(uint32_t colour, const AndOr &mask)
{
	return (colour & mask.mand) | mask.mor;
}

extern const EnumClassIndexContainer<std::array<AndOr, to_underlying(TileType::End) + 1>, TileType> _smallmap_contours_andor;
extern const EnumClassIndexContainer<std::array<AndOr, to_underlying(TileType::End) + 1>, TileType> _smallmap_vehicles_andor;

#endif /* SMALLMAP_COLOURS_H */
