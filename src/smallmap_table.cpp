/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file smallmap_table.cpp Tables associates with smallmap GUI and related colours. */

#include "stdafx.h"
#include "smallmap_colours.h"
#include "smallmap_gui.h"

#include "safeguards.h"

/** Mapping of tile type to importance of the tile (higher number means more interesting to show). */
const EnumClassIndexContainer<std::array<uint8_t, to_underlying(TileType::End) + 1>, TileType> _tiletype_importance = {
	2, // TileType::Clear
	8, // TileType::Railway
	7, // TileType::Road
	5, // TileType::House
	2, // TileType::Trees
	9, // TileType::Station
	2, // TileType::Water
	1, // TileType::Void
	6, // TileType::Industry
	8, // TileType::TunnelBridge
	2, // TileType::Object
	0,
};

/** Colour masks for "Contour" and "Routes" modes. */
const EnumClassIndexContainer<std::array<AndOr, to_underlying(TileType::End) + 1>, TileType> _smallmap_contours_andor = {
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Clear
	AndOr{MKCOLOUR_0XX0(PC_GREY      ), MKCOLOUR_F00F}, // TileType::Railway
	AndOr{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // TileType::Road
	AndOr{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // TileType::House
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Trees
	AndOr{MKCOLOUR_XXXX(PC_LIGHT_BLUE), MKCOLOUR_0000}, // TileType::Station
	AndOr{MKCOLOUR_XXXX(PC_WATER     ), MKCOLOUR_0000}, // TileType::Water
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Void
	AndOr{MKCOLOUR_XXXX(PC_DARK_RED  ), MKCOLOUR_0000}, // TileType::Industry
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::TunnelBridge
	AndOr{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // TileType::Object
	AndOr{MKCOLOUR_0XX0(PC_GREY      ), MKCOLOUR_F00F},
};

/** Colour masks for "Vehicles", "Industry", and "Vegetation" modes. */
const EnumClassIndexContainer<std::array<AndOr, to_underlying(TileType::End) + 1>, TileType> _smallmap_vehicles_andor = {
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Clear
	AndOr{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // TileType::Railway
	AndOr{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // TileType::Road
	AndOr{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // TileType::House
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Trees
	AndOr{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F}, // TileType::Station
	AndOr{MKCOLOUR_XXXX(PC_WATER     ), MKCOLOUR_0000}, // TileType::Water
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::Void
	AndOr{MKCOLOUR_XXXX(PC_DARK_RED  ), MKCOLOUR_0000}, // TileType::Industry
	AndOr{MKCOLOUR_0000               , MKCOLOUR_FFFF}, // TileType::TunnelBridge
	AndOr{MKCOLOUR_0XX0(PC_DARK_RED  ), MKCOLOUR_F00F}, // TileType::Object
	AndOr{MKCOLOUR_0XX0(PC_BLACK     ), MKCOLOUR_F00F},
};
