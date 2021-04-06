/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file smallmap_colours.h Colours used by smallmap. */

#ifndef SMALLMAP_COLOURS_H
#define SMALLMAP_COLOURS_H

#include "core/endian_func.hpp"

static const uint8 PC_ROUGH_LAND      = 0x52; ///< Dark green palette colour for rough land.
static const uint8 PC_GRASS_LAND      = 0x54; ///< Dark green palette colour for grass land.
static const uint8 PC_BARE_LAND       = 0x37; ///< Brown palette colour for bare land.
static const uint8 PC_RAINFOREST      = 0x5C; ///< Pale green palette colour for rainforest.
static const uint8 PC_FIELDS          = 0x25; ///< Light brown palette colour for fields.
static const uint8 PC_TREES           = 0x57; ///< Green palette colour for trees.
static const uint8 PC_WATER           = 0xC9; ///< Dark blue palette colour for water.

#define MKCOLOUR(x)         TO_LE32X(x)

#define MKCOLOUR_XXXX(x)    (MKCOLOUR(0x01010101) * (uint)(x))
#define MKCOLOUR_X0X0(x)    (MKCOLOUR(0x01000100) * (uint)(x))
#define MKCOLOUR_0X0X(x)    (MKCOLOUR(0x00010001) * (uint)(x))
#define MKCOLOUR_0XX0(x)    (MKCOLOUR(0x00010100) * (uint)(x))
#define MKCOLOUR_X00X(x)    (MKCOLOUR(0x01000001) * (uint)(x))

#define MKCOLOUR_XYXY(x, y) (MKCOLOUR_X0X0(x) | MKCOLOUR_0X0X(y))
#define MKCOLOUR_XYYX(x, y) (MKCOLOUR_X00X(x) | MKCOLOUR_0XX0(y))

#define MKCOLOUR_0000       MKCOLOUR_XXXX(0x00)
#define MKCOLOUR_0FF0       MKCOLOUR_0XX0(0xFF)
#define MKCOLOUR_F00F       MKCOLOUR_X00X(0xFF)
#define MKCOLOUR_FFFF       MKCOLOUR_XXXX(0xFF)

#include "table/heightmap_colours.h"
#include "table/darklight_colours.h"

/** Colour scheme of the smallmap. */
struct SmallMapColourScheme {
	uint32 *height_colours;            ///< Cached colours for each level in a map.
	const uint32 *height_colours_base; ///< Base table for determining the colours
	size_t colour_count;               ///< The number of colours.
	uint32 default_colour;             ///< Default colour of the land.
};

extern SmallMapColourScheme _heightmap_schemes[];

struct AndOr {
	uint32 mor;
	uint32 mand;
};

static inline uint32 ApplyMask(uint32 colour, const AndOr *mask)
{
	return (colour & mask->mand) | mask->mor;
}

/** Colour masks for "Contour" and "Routes" modes. */
static const AndOr _smallmap_contours_andor[] = {
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_CLEAR
	{MKCOLOUR_0XX0(PC_GREY      ), MKCOLOUR_F00F}, // MP_RAILWAY
	{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // MP_ROAD
	{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // MP_HOUSE
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_TREES
	{MKCOLOUR_XXXX(PC_LIGHT_BLUE), MKCOLOUR_0000}, // MP_STATION
	{MKCOLOUR_XXXX(PC_WATER     ), MKCOLOUR_0000}, // MP_WATER
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_VOID
	{MKCOLOUR_XXXX(PC_DARK_RED  ), MKCOLOUR_0000}, // MP_INDUSTRY
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_TUNNELBRIDGE
	{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // MP_OBJECT
	{MKCOLOUR_0XX0(PC_GREY      ), MKCOLOUR_F00F},
};

/** Colour masks for "Vehicles", "Industry", and "Vegetation" modes. */
static const AndOr _smallmap_vehicles_andor[] = {
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_CLEAR
	{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // MP_RAILWAY
	{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // MP_ROAD
	{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // MP_HOUSE
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_TREES
	{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // MP_STATION
	{MKCOLOUR_XXXX(PC_WATER     ), MKCOLOUR_0000}, // MP_WATER
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_VOID
	{MKCOLOUR_XXXX(PC_DARK_RED  ), MKCOLOUR_0000}, // MP_INDUSTRY
	{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // MP_TUNNELBRIDGE
	{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // MP_OBJECT
	{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F},
};

static const uint32 _vegetation_clear_bits[] = {
	MKCOLOUR_XXXX(PC_GRASS_LAND), ///< full grass
	MKCOLOUR_XXXX(PC_ROUGH_LAND), ///< rough land
	MKCOLOUR_XXXX(PC_GREY),       ///< rocks
	MKCOLOUR_XXXX(PC_FIELDS),     ///< fields
	MKCOLOUR_XXXX(PC_LIGHT_BLUE), ///< snow
	MKCOLOUR_XXXX(PC_ORANGE),     ///< desert
	MKCOLOUR_XXXX(PC_GRASS_LAND), ///< unused
	MKCOLOUR_XXXX(PC_GRASS_LAND), ///< unused
};

#endif /* SMALLMAP_COLOURS_H */
