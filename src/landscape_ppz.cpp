/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file landscape_ppz.cpp Functions related to landscape partial pixel z. */

#include "stdafx.h"
#include "slope_func.h"

#include "safeguards.h"

/**
 * Determines height at given coordinate of a slope.
 *
 * At the northern corner (0, 0) the result is always a multiple of TILE_HEIGHT.
 * When the height is a fractional Z, then the height is rounded down. For example,
 * when at the height is 0 at x = 0 and the height is 8 at x = 16 (actually x = 0
 * of the next tile), then height is 0 at x = 1, 1 at x = 2, and 7 at x = 15.
 * @param x x coordinate (value from 0 to 15)
 * @param y y coordinate (value from 0 to 15)
 * @param corners slope to examine
 * @return height of given point of given slope
 */
uint GetPartialPixelZ(int x, int y, Slope corners)
{
	if (IsHalftileSlope(corners)) {
		/* A foundation is placed on half the tile at a specific corner. This means that,
		 * depending on the corner, that one half of the tile is at the maximum height. */
		switch (GetHalftileSlopeCorner(corners)) {
			case CORNER_W:
				if (x > y) return GetSlopeMaxPixelZ(corners);
				break;

			case CORNER_S:
				if (x + y >= (int)TILE_SIZE) return GetSlopeMaxPixelZ(corners);
				break;

			case CORNER_E:
				if (x <= y) return GetSlopeMaxPixelZ(corners);
				break;

			case CORNER_N:
				if (x + y < (int)TILE_SIZE) return GetSlopeMaxPixelZ(corners);
				break;

			default: NOT_REACHED();
		}
	}

	switch (RemoveHalftileSlope(corners)) {
		case SLOPE_FLAT: return 0;

		/* One corner is up.*/
		case SLOPE_N: return x + y <= (int)TILE_SIZE ? (TILE_SIZE - x - y)     >> 1 : 0;
		case SLOPE_E: return y >= x                  ? (1 + y - x)             >> 1 : 0;
		case SLOPE_S: return x + y >= (int)TILE_SIZE ? (1 + x + y - TILE_SIZE) >> 1 : 0;
		case SLOPE_W: return x >= y                  ? (x - y)                 >> 1 : 0;

		/* Two corners next to eachother are up. */
		case SLOPE_NE: return (TILE_SIZE - x) >> 1;
		case SLOPE_SE: return (y + 1) >> 1;
		case SLOPE_SW: return (x + 1) >> 1;
		case SLOPE_NW: return (TILE_SIZE - y) >> 1;

		/* Three corners are up on the same level. */
		case SLOPE_ENW: return x + y >= (int)TILE_SIZE ? TILE_HEIGHT - ((1 + x + y - TILE_SIZE) >> 1) : TILE_HEIGHT;
		case SLOPE_SEN: return y < x                   ? TILE_HEIGHT - ((x - y)                 >> 1) : TILE_HEIGHT;
		case SLOPE_WSE: return x + y <= (int)TILE_SIZE ? TILE_HEIGHT - ((TILE_SIZE - x - y)     >> 1) : TILE_HEIGHT;
		case SLOPE_NWS: return x < y                   ? TILE_HEIGHT - ((1 + y - x)             >> 1) : TILE_HEIGHT;

		/* Two corners at opposite sides are up. */
		case SLOPE_NS: return x + y < (int)TILE_SIZE ? (TILE_SIZE - x - y) >> 1 : (1 + x + y - TILE_SIZE) >> 1;
		case SLOPE_EW: return x >= y ? (x - y) >> 1 : (1 + y - x) >> 1;

		/* Very special cases. */
		case SLOPE_ELEVATED: return TILE_HEIGHT;

		/* Steep slopes. The top is at 2 * TILE_HEIGHT. */
		case SLOPE_STEEP_N: return (TILE_SIZE - x + TILE_SIZE - y) >> 1;
		case SLOPE_STEEP_E: return (TILE_SIZE + 1 + y - x) >> 1;
		case SLOPE_STEEP_S: return (1 + x + y) >> 1;
		case SLOPE_STEEP_W: return (TILE_SIZE + x - y) >> 1;

		default: NOT_REACHED();
	}
}
