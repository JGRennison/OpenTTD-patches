/* $Id: trafficlight.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file trafficlight.h variables used for handling trafficlights. */


 /**
  * Used for synchronising traffic light signals.
  * Number below is how far we look into the _tl_check_offsets array when
  * placing trafficlights, based on _settings_game.construction.max_tlc_distance.
  */
static const uint8 _tlc_distance[5] = {
0,  ///< no synchronizing.
8,  ///< adjecant tiles only.
24, ///< 2 tiles away.
48, ///< 3 tiles away.
80  ///< 4 tiles away.
};

/** TileDiffs for the adjacent tiles and those a little further away. */
static const TileIndexDiffC _tl_check_offsets[80] = {
	/* Tiles next to this tile (8 tiles). */
	{-1, -1},
	{ 0, -1},
	{ 1, -1},
	{ 1,  0},
	{ 1,  1},
	{ 0,  1},
	{-1,  1},
	{-1,  0},
	/* Tiles two tiles away from this tile (16 tiles). */
	{-2, -2},
	{-1, -2},
	{ 0, -2},
	{ 1, -2},
	{ 2, -2},
	{ 2, -1},
	{ 2,  0},
	{ 2,  1},
	{ 2,  2},
	{ 1,  2},
	{ 0,  2},
	{-1,  2},
	{-2,  2},
	{-2,  1},
	{-2,  0},
	{-2, -1},
	/* Tiles three tiles away from this tile (24 tiles). */
	{-3, -3},
	{-3, -2},
	{-3, -1},
	{-3,  0},
	{-3,  1},
	{-3,  2},
	{-3,  3},
	{-2,  3},
	{-1,  3},
	{ 0,  3},
	{ 1,  3},
	{ 2,  3},
	{ 3,  3},
	{ 3,  2},
	{ 3,  1},
	{ 3,  0},
	{ 3, -1},
	{ 3, -2},
	{ 3, -3},
	{ 2, -3},
	{ 1, -3},
	{ 0, -3},
	{-1, -3},
	{-2, -3},
	/* Tiles four tiles away from this tile (32 tiles). */
	{-4, -4},
	{-3, -4},
	{-2, -4},
	{-1, -4},
	{ 0, -4},
	{ 1, -4},
	{ 2, -4},
	{ 3, -4},
	{ 4, -4},
	{ 4, -3},
	{ 4, -2},
	{ 4, -1},
	{ 4,  0},
	{ 4,  1},
	{ 4,  2},
	{ 4,  3},
	{ 4,  4},
	{ 3,  4},
	{ 2,  4},
	{ 1,  4},
	{ 0,  4},
	{-1,  4},
	{-2,  4},
	{-3,  4},
	{-4,  4},
	{-4,  3},
	{-4,  2},
	{-4,  1},
	{-4,  0},
	{-4, -1},
	{-4, -2},
	{-4, -3}
};

/**
 * Drawing offsets for the traffic light posts [roadside (left, right)][direction (SW, SE, NW, NE)].
 */
static const Point _tl_offsets[2][4] = {
	{{15, 1}, {14, 15}, {1, 0}, {0, 14}},  // Left side driving.
	{{15, 14}, {1, 15}, {14, 0}, {0, 1}}   // Right side driving.
};

/**
 * Sprites needed for the various states of a TL crossing [state][direction].
 */
static const SpriteID _tls_to_sprites[7][4] = {
	{SPR_TL_SW_NONE,       SPR_TL_SE_NONE,       SPR_TL_NW_NONE,       SPR_TL_NE_NONE},
	{SPR_TL_SW_GREEN,      SPR_TL_SE_RED,        SPR_TL_NW_RED,        SPR_TL_NE_GREEN},
	{SPR_TL_SW_YELLOW,     SPR_TL_SE_RED,        SPR_TL_NW_RED,        SPR_TL_NE_YELLOW},
	{SPR_TL_SW_RED,        SPR_TL_SE_RED_YELLOW, SPR_TL_NW_RED_YELLOW, SPR_TL_NE_RED},
	{SPR_TL_SW_RED,        SPR_TL_SE_GREEN,      SPR_TL_NW_GREEN,      SPR_TL_NE_RED},
	{SPR_TL_SW_RED,        SPR_TL_SE_YELLOW,     SPR_TL_NW_YELLOW,     SPR_TL_NE_RED},
	{SPR_TL_SW_RED_YELLOW, SPR_TL_SE_RED,        SPR_TL_NW_RED,        SPR_TL_NE_RED_YELLOW}
};