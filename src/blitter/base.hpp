/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file base.hpp Base for all blitters. */

#ifndef BLITTER_BASE_HPP
#define BLITTER_BASE_HPP

#include "../spritecache.h"
#include "../spriteloader/spriteloader.hpp"
#include "../core/math_func.hpp"

#include <utility>

/** The modes of blitting we can do. */
enum BlitterMode {
	BM_NORMAL,       ///< Perform the simple blitting.
	BM_COLOUR_REMAP, ///< Perform a colour remapping.
	BM_TRANSPARENT,  ///< Perform transparency colour remapping.
	BM_CRASH_REMAP,  ///< Perform a crash remapping.
	BM_BLACK_REMAP,  ///< Perform remapping to a completely blackened sprite
};

/** Helper for using specialised functions designed to prevent whenever it's possible things like:
 *  - IO (reading video buffer),
 *  - calculations (alpha blending),
 *  - heavy branching (remap lookups and animation buffer handling).
 */
enum BlitterSpriteFlags {
	SF_NONE        = 0,
	SF_TRANSLUCENT = 1 << 1, ///< The sprite has at least 1 translucent pixel.
	SF_NO_REMAP    = 1 << 2, ///< The sprite has no remappable colour pixel.
	SF_NO_ANIM     = 1 << 3, ///< The sprite has no palette animated pixel.
};
DECLARE_ENUM_AS_BIT_SET(BlitterSpriteFlags);

/**
 * How all blitters should look like. Extend this class to make your own.
 */
class Blitter {
public:
	/** Parameters related to blitting. */
	struct BlitterParams {
		const void *sprite; ///< Pointer to the sprite how ever the encoder stored it
		const byte *remap;  ///< XXX -- Temporary storage for remap array

		int skip_left;      ///< How much pixels of the source to skip on the left (based on zoom of dst)
		int skip_top;       ///< How much pixels of the source to skip on the top (based on zoom of dst)
		int width;          ///< The width in pixels that needs to be drawn to dst
		int height;         ///< The height in pixels that needs to be drawn to dst
		int sprite_width;   ///< Real width of the sprite
		int sprite_height;  ///< Real height of the sprite
		int left;           ///< The left offset in the 'dst' in pixels to start drawing
		int top;            ///< The top offset in the 'dst' in pixels to start drawing

		void *dst;          ///< Destination buffer
		int pitch;          ///< The pitch of the destination buffer
	};

	/** Types of palette animation. */
	enum PaletteAnimation {
		PALETTE_ANIMATION_NONE,           ///< No palette animation
		PALETTE_ANIMATION_VIDEO_BACKEND,  ///< Palette animation should be done by video backend (8bpp only!)
		PALETTE_ANIMATION_BLITTER,        ///< The blitter takes care of the palette animation
	};

	/**
	 * Get the screen depth this blitter works for.
	 *  This is either: 8, 16, 24 or 32.
	 */
	virtual uint8 GetScreenDepth() = 0;

	/**
	 * Draw an image to the screen, given an amount of params defined above.
	 */
	virtual void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom) = 0;

	/**
	 * Draw a colourtable to the screen. This is: the colour of the screen is read
	 *  and is looked-up in the palette to match a new colour, which then is put
	 *  on the screen again.
	 * @param dst the destination pointer (video-buffer).
	 * @param width the width of the buffer.
	 * @param height the height of the buffer.
	 * @param pal the palette to use.
	 */
	virtual void DrawColourMappingRect(void *dst, int width, int height, PaletteID pal) = 0;

	/**
	 * Convert a sprite from the loader to our own format.
	 */
	virtual Sprite *Encode(const SpriteLoader::Sprite *sprite, AllocatorProc *allocator) = 0;

	/**
	 * Move the destination pointer the requested amount x and y, keeping in mind
	 *  any pitch and bpp of the renderer.
	 * @param video The destination pointer (video-buffer) to scroll.
	 * @param x How much you want to scroll to the right.
	 * @param y How much you want to scroll to the bottom.
	 * @return A new destination pointer moved the the requested place.
	 */
	virtual void *MoveTo(void *video, int x, int y) = 0;

	/**
	 * Draw a pixel with a given colour on the video-buffer.
	 * @param video The destination pointer (video-buffer).
	 * @param x The x position within video-buffer.
	 * @param y The y position within video-buffer.
	 * @param colour A 8bpp mapping colour.
	 */
	virtual void SetPixel(void *video, int x, int y, uint8 colour) = 0;

	/**
	 * Draw a sequence of pixels on the video-buffer.
	 * @param video The destination pointer (video-buffer).
	 * @param x The x position within video-buffer.
	 * @param y The y position within video-buffer.
	 * @param colours A 8bpp colour mapping buffer.
	 * @param width The length of the line.
	 */
	virtual void SetLine(void *video, int x, int y, uint8 *colours, uint width) = 0;

	/**
	 * Draw a sequence of pixels on the video-buffer (no LookupColourInPalette).
	 * @param video The destination pointer (video-buffer).
	 * @param x The x position within video-buffer.
	 * @param y The y position within video-buffer.
	 * @param colours A 32bpp colour buffer.
	 * @param width The length of the line.
	 */
	virtual void SetLine32(void *video, int x, int y, uint32 *colours, uint width) { NOT_REACHED(); };

	/**
	 * Make a single horizontal line in a single colour on the video-buffer.
	 * @param video The destination pointer (video-buffer).
	 * @param width The length of the line.
	 * @param height The height of the line.
	 * @param colour A 8bpp mapping colour.
	 */
	virtual void DrawRect(void *video, int width, int height, uint8 colour) = 0;

	/**
	 * Draw a line with a given colour.
	 * @param video The destination pointer (video-buffer).
	 * @param x The x coordinate from where the line starts.
	 * @param y The y coordinate from where the line starts.
	 * @param x2 The x coordinate to where the line goes.
	 * @param y2 The y coordinate to where the lines goes.
	 * @param screen_width The width of the screen you are drawing in (to avoid buffer-overflows).
	 * @param screen_height The height of the screen you are drawing in (to avoid buffer-overflows).
	 * @param colour A 8bpp mapping colour.
	 * @param width Line width.
	 * @param dash Length of dashes for dashed lines. 0 means solid line.
	 */
	virtual void DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour, int width, int dash = 0) = 0;

	/**
	 * Copy from a buffer to the screen.
	 * @param video The destination pointer (video-buffer).
	 * @param src The buffer from which the data will be read.
	 * @param width The width of the buffer.
	 * @param height The height of the buffer.
	 * @note You can not do anything with the content of the buffer, as the blitter can store non-pixel data in it too!
	 */
	virtual void CopyFromBuffer(void *video, const void *src, int width, int height) = 0;

	/**
	 * Copy from the screen to a buffer.
	 * @param video The destination pointer (video-buffer).
	 * @param dst The buffer in which the data will be stored.
	 * @param width The width of the buffer.
	 * @param height The height of the buffer.
	 * @note You can not do anything with the content of the buffer, as the blitter can store non-pixel data in it too!
	 */
	virtual void CopyToBuffer(const void *video, void *dst, int width, int height) = 0;

	/**
	 * Copy from the screen to a buffer in a palette format for 8bpp and RGBA format for 32bpp.
	 * @param video The destination pointer (video-buffer).
	 * @param dst The buffer in which the data will be stored.
	 * @param width The width of the buffer.
	 * @param height The height of the buffer.
	 * @param dst_pitch The pitch (byte per line) of the destination buffer.
	 */
	virtual void CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch) = 0;

	/**
	 * Scroll the videobuffer some 'x' and 'y' value.
	 * @param video The buffer to scroll into.
	 * @param left The left value of the screen to scroll.
	 * @param top The top value of the screen to scroll.
	 * @param width The width of the screen to scroll.
	 * @param height The height of the screen to scroll.
	 * @param scroll_x How much to scroll in X.
	 * @param scroll_y How much to scroll in Y.
	 */
	virtual void ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y) = 0;

	/**
	 * Calculate how much memory there is needed for an image of this size in the video-buffer.
	 * @param width The width of the buffer-to-be.
	 * @param height The height of the buffer-to-be.
	 * @return The size needed for the buffer.
	 */
	virtual int BufferSize(int width, int height) = 0;

	/**
	 * Called when the 8bpp palette is changed; you should redraw all pixels on the screen that
	 *  are equal to the 8bpp palette indexes 'first_dirty' to 'first_dirty + count_dirty'.
	 * @param palette The new palette.
	 */
	virtual void PaletteAnimate(const Palette &palette) = 0;

	/**
	 * Check if the blitter uses palette animation at all.
	 * @return True if it uses palette animation.
	 */
	virtual Blitter::PaletteAnimation UsePaletteAnimation() = 0;

	/**
	 * Get the name of the blitter, the same as the Factory-instance returns.
	 */
	virtual const char *GetName() = 0;

	/**
	 * Get how many bytes are needed to store a pixel.
	 */
	virtual int GetBytesPerPixel() = 0;

	/**
	 * Post resize event
	 */
	virtual void PostResize() { };

	virtual ~Blitter() { }

	template <typename SetPixelT> void DrawLineGeneric(int x, int y, int x2, int y2, int screen_width, int screen_height, int width, int dash, SetPixelT set_pixel);
};

template <typename SetPixelT>
void Blitter::DrawLineGeneric(int x, int y, int x2, int y2, int screen_width, int screen_height, int width, int dash, SetPixelT set_pixel)
{
	int dy;
	int dx;
	int stepx;
	int stepy;

	dy = (y2 - y) * 2;
	if (dy < 0) {
		dy = -dy;
		stepy = -1;
	} else {
		stepy = 1;
	}

	dx = (x2 - x) * 2;
	if (dx < 0) {
		dx = -dx;
		stepx = -1;
	} else {
		stepx = 1;
	}

	if (dx == 0 && dy == 0) {
		/* The algorithm below cannot handle this special case; make it work at least for line width 1 */
		if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) set_pixel(x, y);
		return;
	}

	int frac_diff = width * max(dx, dy);
	if (width > 1) {
		/* compute frac_diff = width * sqrt(dx*dx + dy*dy)
		 * Start interval:
		 *    max(dx, dy) <= sqrt(dx*dx + dy*dy) <= sqrt(2) * max(dx, dy) <= 3/2 * max(dx, dy) */
		int64 frac_sq = ((int64) width) * ((int64) width) * (((int64) dx) * ((int64) dx) + ((int64) dy) * ((int64) dy));
		int frac_max = 3 * frac_diff / 2;
		while (frac_diff < frac_max) {
			int frac_test = (frac_diff + frac_max) / 2;
			if (((int64) frac_test) * ((int64) frac_test) < frac_sq) {
				frac_diff = frac_test + 1;
			} else {
				frac_max = frac_test - 1;
			}
		}
	}

	int gap = dash;
	if (dash == 0) dash = 1;
	int dash_count = 0;
	if (dx > dy) {
		if (stepx < 0) {
			std::swap(x, x2);
			std::swap(y, y2);
			stepy = -stepy;
		}
		if (x2 < 0 || x >= screen_width) return;

		int y_low     = y;
		int y_high    = y;
		int frac_low  = dy - frac_diff / 2;
		int frac_high = dy + frac_diff / 2;

		while (frac_low < -(dx / 2)) {
			frac_low += dx;
			y_low -= stepy;
		}
		while (frac_high >= dx / 2) {
			frac_high -= dx;
			y_high += stepy;
		}

		if (x < 0) {
			dash_count = (-x) % (dash + gap);
			auto adjust_frac = [&](int64 frac, int &y_bound) -> int {
				frac -= ((int64) dy) * ((int64) x);
				if (frac >= 0) {
					int quotient = frac / dx;
					int remainder = frac % dx;
					y_bound += (1 + quotient) * stepy;
					frac = remainder - dx;
				}
				return frac;
			};
			frac_low = adjust_frac(frac_low, y_low);
			frac_high = adjust_frac(frac_high, y_high);
			x = 0;
		}
		x2++;
		if (x2 > screen_width) {
			x2 = screen_width;
		}

		while (x != x2) {
			if (dash_count < dash) {
				for (int y = y_low; y != y_high; y += stepy) {
					if (y >= 0 && y < screen_height) set_pixel(x, y);
				}
			}
			if (frac_low >= 0) {
				y_low += stepy;
				frac_low -= dx;
			}
			if (frac_high >= 0) {
				y_high += stepy;
				frac_high -= dx;
			}
			x++;
			frac_low += dy;
			frac_high += dy;
			if (++dash_count >= dash + gap) dash_count = 0;
		}
	} else {
		if (stepy < 0) {
			std::swap(x, x2);
			std::swap(y, y2);
			stepx = -stepx;
		}
		if (y2 < 0 || y >= screen_height) return;

		int x_low     = x;
		int x_high    = x;
		int frac_low  = dx - frac_diff / 2;
		int frac_high = dx + frac_diff / 2;

		while (frac_low < -(dy / 2)) {
			frac_low += dy;
			x_low -= stepx;
		}
		while (frac_high >= dy / 2) {
			frac_high -= dy;
			x_high += stepx;
		}

		if (y < 0) {
			dash_count = (-y) % (dash + gap);
			auto adjust_frac = [&](int64 frac, int &x_bound) -> int {
				frac -= ((int64) dx) * ((int64) y);
				if (frac >= 0) {
					int quotient = frac / dy;
					int remainder = frac % dy;
					x_bound += (1 + quotient) * stepx;
					frac = remainder - dy;
				}
				return frac;
			};
			frac_low = adjust_frac(frac_low, x_low);
			frac_high = adjust_frac(frac_high, x_high);
			y = 0;
		}
		y2++;
		if (y2 > screen_height) {
			y2 = screen_height;
		}

		while (y != y2) {
			if (dash_count < dash) {
				for (int x = x_low; x != x_high; x += stepx) {
					if (x >= 0 && x < screen_width) set_pixel(x, y);
				}
			}
			if (frac_low >= 0) {
				x_low += stepx;
				frac_low -= dy;
			}
			if (frac_high >= 0) {
				x_high += stepx;
				frac_high -= dy;
			}
			y++;
			frac_low += dx;
			frac_high += dx;
			if (++dash_count >= dash + gap) dash_count = 0;
		}
	}
}

#endif /* BLITTER_BASE_HPP */
