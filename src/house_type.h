/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file house_type.h declaration of basic house types and enums */

#ifndef HOUSE_TYPE_H
#define HOUSE_TYPE_H

typedef uint16 HouseID; ///< OpenTTD ID of house types.
typedef uint16 HouseClassID; ///< Classes of houses.
typedef byte HouseVariant; ///< House variant.

struct HouseSpec;

/** Visualization contexts of town houses. */
enum HouseImageType {
	HIT_HOUSE_TILE        = 0, ///< Real house that exists on the game map (certain house tile).
	HIT_GUI_HOUSE_PREVIEW = 1, ///< GUI preview of a house.
	HIT_GUI_HOUSE_LIST    = 2, ///< Same as #HIT_GUI_HOUSE_PREVIEW but the house is being drawn in the GUI list of houses, not in the full house preview.
};

#endif /* HOUSE_TYPE_H */
