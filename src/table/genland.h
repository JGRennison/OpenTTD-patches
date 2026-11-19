/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file genland.h Table used to generate deserts and/or rainforests. */

// This default array draws a filled circle 13 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data[] = {
	{-5,  5,   -5,  5},
	{ 6,  6,   -3,  3},
	{-6, -6,   -3,  3},
	{-3,  3,    6,  6},
	{-3,  3,   -6, -6}
};

// This array draws a filled circle 19 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data_medium[] = {
	{-3,  3,   -9, -9},
	{-5,  5,   -8, -8},
	{-6,  6,   -7, -7},
	{-7,  7,   -6, -6},
	{-8,  8,   -5, -4},
	{-9,  9,   -3,  3},
	{-8,  8,    4,  5},
	{-7,  7,    6,  6},
	{-6,  6,    7,  7},
	{-5,  5,    8,  8},
	{-3,  3,    9,  9}
};

// This array draws a filled circle 25 tiles in diameter
static const Rect16 _make_desert_or_rainforest_data_large[] = {
	{ -3,  3,   -12, -12},
	{ -5,  5,   -11, -11},
	{ -7,  7,   -10, -10},
	{ -8,  8,    -9,  -9},
	{ -9,  9,    -8,  -8},
	{-10, 10,    -7,  -6},
	{-11, 11,    -5,  -4},
	{-12, 12,    -3,   3},
	{-11, 11,     4,   5},
	{-10, 10,     6,   7},
	{ -9,  9,     8,   8},
	{ -8,  8,     9,   9},
	{ -7,  7,    10,  10},
	{ -5,  5,    11,  11},
	{ -3,  3,    12,  12}
};

// This array draws a filled circle 51 tiles in diameter.
static const Rect16 _make_desert_or_rainforest_data_extralarge[] = {
	{ -5,  5,   -25, -25},
	{ -8,  8,   -24, -24},
	{-11, 11,   -23, -23},
	{-12, 12,   -22, -22},
	{-14, 14,   -21, -21},
	{-15, 15,   -20, -20},
	{-17, 17,   -19, -19},
	{-18, 18,   -18, -18},
	{-19, 19,   -17, -16},
	{-20, 20,   -15, -15},
	{-21, 21,   -14, -13},
	{-22, 22,   -12, -12},
	{-23, 23,   -11,  -9},
	{-24, 24,    -8,  -6},
	{-25, 25,    -5,   5},
	{-24, 24,     6,   8},
	{-23, 23,     9,  11},
	{-22, 22,    12,  12},
	{-21, 21,    13,  14},
	{-20, 20,    15,  15},
	{-19, 19,    16,  17},
	{-18, 18,    18,  18},
	{-17, 17,    19,  19},
	{-15, 15,    20,  20},
	{-14, 14,    21,  21},
	{-12, 12,    22,  22},
	{-11, 11,    23,  23},
	{ -8,  8,    24,  24},
	{ -5,  5,    25,  25}
};
