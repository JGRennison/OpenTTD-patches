/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file palette.cpp Handling of palettes. */

#include "stdafx.h"
#include "blitter/base.hpp"
#include "blitter/factory.hpp"
#include "fileio_func.h"
#include "gfx_type.h"
#include "landscape_type.h"
#include "palette_func.h"
#include "settings_type.h"
#include "thread.h"
#include "core/mem_func.hpp"

#include "table/palettes.h"

#include "safeguards.h"

Palette _cur_palette;
std::mutex _cur_palette_mutex;

uint8_t _colour_value[COLOUR_END] = {
	133, // COLOUR_DARK_BLUE
	 99, // COLOUR_PALE_GREEN,
	 48, // COLOUR_PINK,
	 68, // COLOUR_YELLOW,
	184, // COLOUR_RED,
	152, // COLOUR_LIGHT_BLUE,
	209, // COLOUR_GREEN,
	 95, // COLOUR_DARK_GREEN,
	150, // COLOUR_BLUE,
	 79, // COLOUR_CREAM,
	134, // COLOUR_MAUVE,
	174, // COLOUR_PURPLE,
	195, // COLOUR_ORANGE,
	116, // COLOUR_BROWN,
	  6, // COLOUR_GREY,
	 15, // COLOUR_WHITE,
};

Colour _water_palette[10];

/**
 * PALETTE_BITS reduces the bits-per-channel of 32bpp graphics data to allow faster palette lookups from
 * a smaller lookup table.
 *
 * 6 bpc is chosen as this results in a palette lookup table of 256KiB with adequate fidelty.
 * In contrast, a 5 bpc lookup table would be 32KiB, and 7 bpc would be 2MiB.
 *
 * Values in the table are filled as they are first encountered -- larger lookup table means more colour
 * distance calculations, and is therefore slower.
 */
const uint PALETTE_BITS = 6;
const uint PALETTE_SHIFT = 8 - PALETTE_BITS;
const uint PALETTE_BITS_MASK = ((1U << PALETTE_BITS) - 1) << PALETTE_SHIFT;
const uint PALETTE_BITS_OR = (1U << (PALETTE_SHIFT - 1));

/* Palette and reshade lookup table. */
using PaletteLookup = std::array<uint8_t, 1U << (PALETTE_BITS * 3)>;
static PaletteLookup _palette_lookup{};

using ReshadeLookup = std::array<uint8_t, 1U << PALETTE_BITS>;
static ReshadeLookup _reshade_lookup{};

/**
 * Reduce bits per channel to PALETTE_BITS, and place value in the middle of the reduced range.
 * This is to counteract the information lost between bright and dark pixels, e.g if PALETTE_BITS was 2:
 *    0 -  63 ->  32
 *   64 - 127 ->  96
 *  128 - 191 -> 160
 *  192 - 255 -> 224
 * @param c 8 bit colour component.
 * @returns Colour component reduced to PALETTE_BITS.
 */
inline uint CrunchColour(uint c)
{
	return (c & PALETTE_BITS_MASK) | PALETTE_BITS_OR;
}

/**
 * Calculate distance between two colours.
 * @param col1 First colour.
 * @param r2 Red component of second colour.
 * @param g2 Green component of second colour.
 * @param b2 Blue component of second colour.
 * @returns Euclidean distance between first and second colour.
 */
static uint CalculateColourDistance(const Colour &col1, int r2, int g2, int b2)
{
	/* Euclidean colour distance for sRGB based on https://en.wikipedia.org/wiki/Color_difference#sRGB */
	int r = (int)col1.r - (int)r2;
	int g = (int)col1.g - (int)g2;
	int b = (int)col1.b - (int)b2;

	int avgr = (col1.r + r2) / 2;
	return ((2 + (avgr / 256.0)) * r * r) + (4 * g * g) + ((2 + ((255 - avgr) / 256.0)) * b * b);
}

/* Palette indexes for conversion. See docs/palettes/palette_key.png */
const uint8_t PALETTE_INDEX_CC_START = 198; ///< Palette index of start of company colour remap area.
const uint8_t PALETTE_INDEX_CC_END = PALETTE_INDEX_CC_START + 8; ///< Palette index of end of company colour remap area.
const uint8_t PALETTE_INDEX_START = 1; ///< Palette index of start of defined palette.
const uint8_t PALETTE_INDEX_END = 215; ///< Palette index of end of defined palette.

/**
 * Find nearest colour palette index for a 32bpp pixel.
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 * @returns palette index of nearest colour.
 */
static uint8_t FindNearestColourIndex(uint8_t r, uint8_t g, uint8_t b)
{
	r = CrunchColour(r);
	g = CrunchColour(g);
	b = CrunchColour(b);

	uint best_index = 0;
	uint best_distance = UINT32_MAX;

	for (uint i = PALETTE_INDEX_START; i < PALETTE_INDEX_CC_START; i++) {
		if (uint distance = CalculateColourDistance(_palette.palette[i], r, g, b); distance < best_distance) {
			best_index = i;
			best_distance = distance;
		}
	}
	/* There's a hole in the palette reserved for company colour remaps. */
	for (uint i = PALETTE_INDEX_CC_END; i < PALETTE_INDEX_END; i++) {
		if (uint distance = CalculateColourDistance(_palette.palette[i], r, g, b); distance < best_distance) {
			best_index = i;
			best_distance = distance;
		}
	}
	return best_index;
}

/**
 * Find nearest company colour palette index for a brightness level.
 * @param pixel Pixel to find.
 * @returns palette index of nearest colour.
 */
static uint8_t FindNearestColourReshadeIndex(uint8_t b)
{
	b = CrunchColour(b);

	uint best_index = 0;
	uint best_distance = UINT32_MAX;

	for (uint i = PALETTE_INDEX_CC_START; i < PALETTE_INDEX_CC_END; i++) {
		if (uint distance = CalculateColourDistance(_palette.palette[i], b, b, b); distance < best_distance) {
			best_index = i;
			best_distance = distance;
		}
	}
	return best_index;
}

/**
 * Get nearest colour palette index from an RGB colour.
 * A search is performed if this colour is not already in the lookup table.
 * @param r Red component.
 * @param g Green component.
 * @param b Blue component.
 * @returns nearest colour palette index.
 */
uint8_t GetNearestColourIndex(uint8_t r, uint8_t g, uint8_t b)
{
	uint32_t key = (r >> PALETTE_SHIFT) | (g >> PALETTE_SHIFT) << PALETTE_BITS | (b >> PALETTE_SHIFT) << (PALETTE_BITS * 2);
	if (_palette_lookup[key] == 0) _palette_lookup[key] = FindNearestColourIndex(r, g, b);
	return _palette_lookup[key];
}

/**
 * Get nearest colour palette index from a brightness level.
 * A search is performed if this brightness level is not already in the lookup table.
 * @param b Brightness component.
 * @returns nearest colour palette index.
 */
uint8_t GetNearestColourReshadeIndex(uint8_t b)
{
	uint32_t key = (b >> PALETTE_SHIFT);
	if (_reshade_lookup[key] == 0) _reshade_lookup[key] = FindNearestColourReshadeIndex(b);
	return _reshade_lookup[key];
}

/**
 * Adjust brightness of colour.
 * @param colour Colour to adjust.
 * @param brightness Brightness to apply to colour.
 * @returns Adjusted colour.
 */
Colour ReallyAdjustBrightness(Colour colour, int brightness)
{
	if (brightness == DEFAULT_BRIGHTNESS) return colour;

	uint64_t combined = (static_cast<uint64_t>(colour.r) << 32) | (static_cast<uint64_t>(colour.g) << 16) | static_cast<uint64_t>(colour.b);
	combined *= brightness;

	uint16_t r = GB(combined, 39, 9);
	uint16_t g = GB(combined, 23, 9);
	uint16_t b = GB(combined, 7, 9);

	if ((combined & 0x800080008000L) == 0L) {
		return Colour(r, g, b, colour.a);
	}

	uint16_t ob = 0;
	/* Sum overbright */
	if (r > 255) ob += r - 255;
	if (g > 255) ob += g - 255;
	if (b > 255) ob += b - 255;

	/* Reduce overbright strength */
	ob /= 2;
	return Colour(
		r >= 255 ? 255 : std::min(r + ob * (255 - r) / 256, 255),
		g >= 255 ? 255 : std::min(g + ob * (255 - g) / 256, 255),
		b >= 255 ? 255 : std::min(b + ob * (255 - b) / 256, 255),
		colour.a);
}

void DoPaletteAnimations();

void GfxInitPalettes()
{
	MemCpyT<Colour>(_water_palette, (_settings_game.game_creation.landscape == LT_TOYLAND) ? _extra_palette_values.dark_water_toyland : _extra_palette_values.dark_water, 5);
	const Colour *s = (_settings_game.game_creation.landscape == LT_TOYLAND) ? _extra_palette_values.glitter_water_toyland : _extra_palette_values.glitter_water;
	for (int i = 0; i < 5; i++) {
		_water_palette[i + 5] = s[i * 3];
	}

	std::lock_guard<std::mutex> lock_state(_cur_palette_mutex);
	memcpy(&_cur_palette, &_palette, sizeof(_cur_palette));
	DoPaletteAnimations();
}

#define EXTR(p, q) (((uint16_t)(palette_animation_counter * (p)) * (q)) >> 16)
#define EXTR2(p, q) (((uint16_t)(~palette_animation_counter * (p)) * (q)) >> 16)

void DoPaletteAnimations()
{
	/* Animation counter for the palette animation. */
	static int palette_animation_counter = 0;
	palette_animation_counter += 8;

	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	const Colour *s;
	const ExtraPaletteValues *ev = &_extra_palette_values;
	Colour old_val[PALETTE_ANIM_SIZE];
	const uint old_tc = palette_animation_counter;
	uint j;

	if (blitter != nullptr && blitter->UsePaletteAnimation() == Blitter::PaletteAnimation::None) {
		palette_animation_counter = 0;
	}

	Colour *palette_pos = &_cur_palette.palette[PALETTE_ANIM_START];  // Points to where animations are taking place on the palette
	/* Makes a copy of the current animation palette in old_val,
	 * so the work on the current palette could be compared, see if there has been any changes */
	memcpy(old_val, palette_pos, sizeof(old_val));

	/* Fizzy Drink bubbles animation */
	s = ev->fizzy_drink;
	j = EXTR2(512, EPV_CYCLES_FIZZY_DRINK);
	for (uint i = 0; i != EPV_CYCLES_FIZZY_DRINK; i++) {
		*palette_pos++ = s[j];
		j++;
		if (j == EPV_CYCLES_FIZZY_DRINK) j = 0;
	}

	/* Oil refinery fire animation */
	s = ev->oil_refinery;
	j = EXTR2(512, EPV_CYCLES_OIL_REFINERY);
	for (uint i = 0; i != EPV_CYCLES_OIL_REFINERY; i++) {
		*palette_pos++ = s[j];
		j++;
		if (j == EPV_CYCLES_OIL_REFINERY) j = 0;
	}

	/* Radio tower blinking */
	{
		uint8_t i = (palette_animation_counter >> 1) & 0x7F;
		uint8_t v;

		if (i < 0x3f) {
			v = 255;
		} else if (i < 0x4A || i >= 0x75) {
			v = 128;
		} else {
			v = 20;
		}
		palette_pos->r = v;
		palette_pos->g = 0;
		palette_pos->b = 0;
		palette_pos++;

		i ^= 0x40;
		if (i < 0x3f) {
			v = 255;
		} else if (i < 0x4A || i >= 0x75) {
			v = 128;
		} else {
			v = 20;
		}
		palette_pos->r = v;
		palette_pos->g = 0;
		palette_pos->b = 0;
		palette_pos++;
	}

	/* Handle lighthouse and stadium animation */
	s = ev->lighthouse;
	j = EXTR(256, EPV_CYCLES_LIGHTHOUSE);
	for (uint i = 0; i != EPV_CYCLES_LIGHTHOUSE; i++) {
		*palette_pos++ = s[j];
		j++;
		if (j == EPV_CYCLES_LIGHTHOUSE) j = 0;
	}

	/* Dark blue water */
	s = (_settings_game.game_creation.landscape == LT_TOYLAND) ? ev->dark_water_toyland : ev->dark_water;
	j = EXTR(320, EPV_CYCLES_DARK_WATER);
	for (uint i = 0; i != EPV_CYCLES_DARK_WATER; i++) {
		*palette_pos++ = s[j];
		j++;
		if (j == EPV_CYCLES_DARK_WATER) j = 0;
	}

	/* Glittery water */
	s = (_settings_game.game_creation.landscape == LT_TOYLAND) ? ev->glitter_water_toyland : ev->glitter_water;
	j = EXTR(128, EPV_CYCLES_GLITTER_WATER);
	for (uint i = 0; i != EPV_CYCLES_GLITTER_WATER / 3; i++) {
		*palette_pos++ = s[j];
		j += 3;
		if (j >= EPV_CYCLES_GLITTER_WATER) j -= EPV_CYCLES_GLITTER_WATER;
	}

	if (blitter != nullptr && blitter->UsePaletteAnimation() == Blitter::PaletteAnimation::None) {
		palette_animation_counter = old_tc;
	} else if (_cur_palette.count_dirty == 0 && memcmp(old_val, &_cur_palette.palette[PALETTE_ANIM_START], sizeof(old_val)) != 0) {
		/* Did we changed anything on the palette? Seems so.  Mark it as dirty */
		_cur_palette.first_dirty = PALETTE_ANIM_START;
		_cur_palette.count_dirty = PALETTE_ANIM_SIZE;
	}
}

/**
 * Determine a contrasty text colour for a coloured background.
 * @param background Background colour.
 * @param threshold Background colour brightness threshold below which the background is considered dark and TC_WHITE is returned, range: 0 - 255, default 128.
 * @return TC_BLACK or TC_WHITE depending on what gives a better contrast.
 */
TextColour GetContrastColour(uint8_t background, uint8_t threshold)
{
	Colour c = _cur_palette.palette[background];
	/* Compute brightness according to http://www.w3.org/TR/AERT#color-contrast.
	 * The following formula computes 1000 * brightness^2, with brightness being in range 0 to 255. */
	uint sq1000_brightness = c.r * c.r * 299 + c.g * c.g * 587 + c.b * c.b * 114;
	/* Compare with threshold brightness which defaults to 128 (50%) */
	return sq1000_brightness < ((uint) threshold) * ((uint) threshold) * 1000 ? TC_WHITE : TC_BLACK;
}

/**
 * Lookup table of colour shades for all 16 colour gradients.
 * 8 colours per gradient from darkest (0) to lightest (7)
 */
struct ColourGradients
{
	using ColourGradient = std::array<uint8_t, SHADE_END>;

	static inline std::array<ColourGradient, COLOUR_END> gradient{};
};

/**
 * Get colour gradient palette index.
 * @param colour Colour.
 * @param shade Shade level from 1 to 7.
 * @returns palette index of colour.
 */
uint8_t GetColourGradient(Colours colour, ColourShade shade)
{
	return ColourGradients::gradient[colour % COLOUR_END][shade % SHADE_END];
}

/**
 * Set colour gradient palette index.
 * @param colour Colour.
 * @param shade Shade level from 1 to 7.
 * @param palette_index Palette index to set.
 */
void SetColourGradient(Colours colour, ColourShade shade, uint8_t palette_index)
{
	assert(colour < COLOUR_END);
	assert(shade < SHADE_END);
	ColourGradients::gradient[colour % COLOUR_END][shade % SHADE_END] = palette_index;
}
