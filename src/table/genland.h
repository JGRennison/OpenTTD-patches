/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file genland.h Table used to generate deserts and/or rain forests. */

#define R(x1, x2, y1, y2) {x1, y1, x2, y2}

// This default array draws a filled circle 13 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data[] = {
	R(-5,  5,   -5,  5),
	R( 6,  6,   -3,  3),
	R(-6, -6,   -3,  3),
	R(-3,  3,    6,  6),
	R(-3,  3,   -6, -6)
};

// This array draws a filled circle 19 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data_medium[] = {
	R(-3,  3,   -9, -9),
	R(-5,  5,   -8, -8),
	R(-6,  6,   -7, -7),
	R(-7,  7,   -6, -6),
	R(-8,  8,   -5, -4),
	R(-9,  9,   -3,  3),
	R(-8,  8,    4,  5),
	R(-7,  7,    6,  6),
	R(-6,  6,    7,  7),
	R(-5,  5,    8,  8),
	R(-3,  3,    9,  9)
};

// This array draws a filled circle 25 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data_large[] = {
	R( -3,  3,   -12, -12),
	R( -5,  5,   -11, -11),
	R( -7,  7,   -10, -10),
	R( -8,  8,    -9,  -9),
	R( -9,  9,    -8,  -8),
	R(-10, 10,    -7,  -6),
	R(-11, 11,    -5,  -4),
	R(-12, 12,    -3,   3),
	R(-11, 11,     4,   5),
	R(-10, 10,     6,   7),
	R( -9,  9,     8,   8),
	R( -8,  8,     9,   9),
	R( -7,  7,    10,  10),
	R( -5,  5,    11,  11),
	R( -3,  3,    12,  12)
};

// This array draws a filled circle 51 tiles in diameter.
static const Rect16 _make_desert_or_rainforest_data_extralarge[] = {
	R( -5,  5,   -25, -25),
	R( -8,  8,   -24, -24),
	R(-11, 11,   -23, -23),
	R(-12, 12,   -22, -22),
	R(-14, 14,   -21, -21),
	R(-15, 15,   -20, -20),
	R(-17, 17,   -19, -19),
	R(-18, 18,   -18, -18),
	R(-19, 19,   -17, -16),
	R(-20, 20,   -15, -15),
	R(-21, 21,   -14, -13),
	R(-22, 22,   -12, -12),
	R(-23, 23,   -11,  -9),
	R(-24, 24,    -8,  -6),
	R(-25, 25,    -5,   5),
	R(-24, 24,     6,   8),
	R(-23, 23,     9,  11),
	R(-22, 22,    12,  12),
	R(-21, 21,    13,  14),
	R(-20, 20,    15,  15),
	R(-19, 19,    16,  17),
	R(-18, 18,    18,  18),
	R(-17, 17,    19,  19),
	R(-15, 15,    20,  20),
	R(-14, 14,    21,  21),
	R(-12, 12,    22,  22),
	R(-11, 11,    23,  23),
	R( -8,  8,    24,  24),
	R( -5,  5,    25,  25)
};

#undef R
