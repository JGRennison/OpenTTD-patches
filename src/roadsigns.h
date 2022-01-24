/* $Id: yieldsign.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file roadsign.h variables used for handling yield sign. */

/**
 * Drawing offsets for the yield signs posts [roadside (left, right)][direction (SW, SE, NW, NE)].
 */
static const Point _ys_offsets[2][4] = {
	{{15, 1}, {14, 15}, {1, 0}, {0, 14}},  // Left side driving.
	{{15, 14}, {1, 15}, {14, 0}, {0, 1}}   // Right side driving.
};

// Street crossing
static const Point _sc_offsets[2][4] = {

	{{14, 14}, {1, 14}, {14, 1}, {1, 1}}, // Left side driving.
	{{14, 1}, {14, 14}, {1, 1}, {1, 14}}, // Right side driving.
};

// Hydrants
static const Point _hydrant_offsets[2][2] = {
	{{5, 1}, {1, 5}},
	{{5, 14}, {14, 5}},
};

/**
 * Sprites needed for the various states of a Yield Sign crossing [direction].
 */
static const SpriteID _ys_to_sprites[4] = { SPR_YS_SW,       SPR_YS_SE,       SPR_YS_NW,       SPR_YS_NE };

/**
 * Sprites needed for the various states of a Stop Sign crossing [direction].
 */
static const SpriteID _ss_to_sprites[4] = { SPR_SS_SW,       SPR_SS_SE,       SPR_SS_NW,       SPR_SS_NE };


enum RoadSignDirection {
	ROAD_SIGN_DIRECTION_NONE = -1,
	ROAD_SIGN_DIRECTION_BEGIN = 0,
	ROAD_SIGN_DIRECTION_SW = ROAD_SIGN_DIRECTION_BEGIN,
	ROAD_SIGN_DIRECTION_SE = 1,
	ROAD_SIGN_DIRECTION_NW = 2,
	ROAD_SIGN_DIRECTION_NE = 3,
	ROAD_SIGN_DIRECTION_END = ROAD_SIGN_DIRECTION_NE
};