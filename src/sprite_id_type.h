/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file sprite_id_type.h Types related to sprite IDs. */

#ifndef SPRITE_ID_TYPE_H
#define SPRITE_ID_TYPE_H

typedef uint32_t SpriteID;  ///< The number of a sprite, without mapping bits and colourtables
typedef uint32_t PaletteID; ///< The number of the palette
typedef uint32_t CursorID;  ///< The number of the cursor (sprite)

/** Combination of a palette sprite and a 'real' sprite */
struct PalSpriteID {
	SpriteID sprite{};  ///< The 'real' sprite
	PaletteID pal{};    ///< The palette (use \c PAL_NONE) if not needed)

	auto operator<=>(const PalSpriteID&) const = default;
};

#endif /* SPRITE_ID_TYPE_H */
