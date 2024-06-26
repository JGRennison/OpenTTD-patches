/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file clear_func.h Functions related to clear (MP_CLEAR) land. */

#ifndef CLEAR_FUNC_H
#define CLEAR_FUNC_H

#include "tile_cmd.h"

void DrawHillyLandTile(const TileInfo *ti);
void DrawClearLandTile(const TileInfo *ti, uint8_t set);

SpriteID GetSpriteIDForClearLand(const Slope slope, uint8_t set);
SpriteID GetSpriteIDForHillyLand(const Slope slope, const uint rough_index);
SpriteID GetSpriteIDForRocks(const Slope slope, const uint tile_hash);
SpriteID GetSpriteIDForFields(const Slope slope, const uint field_type);
SpriteID GetSpriteIDForSnowDesert(const Slope slope, const uint density);

#endif /* CLEAR_FUNC_H */
