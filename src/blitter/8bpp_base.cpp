/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file 8bpp_base.cpp Implementation of the base for all 8 bpp blitters. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "8bpp_base.hpp"
#include "common.hpp"

#include "../safeguards.h"

void Blitter_8bppBase::DrawColourMappingRect(void *dst, int width, int height, PaletteID pal)
{
	const uint8_t *ctab = GetNonSprite(pal, SpriteType::Recolour) + 1;
	const int screen_pitch = this->GetScreenPitch();

	do {
		for (int i = 0; i != width; i++) *((uint8_t *)dst + i) = ctab[((uint8_t *)dst)[i]];
		dst = (uint8_t *)dst + screen_pitch;
	} while (--height);
}

void *Blitter_8bppBase::MoveTo(void *video, int x, int y)
{
	return (uint8_t *)video + x + y * this->GetScreenPitch();
}

void Blitter_8bppBase::SetPixel(void *video, int x, int y, uint8_t colour)
{
	*((uint8_t *)video + x + y * this->GetScreenPitch()) = colour;
}

void Blitter_8bppBase::SetPixel32(void *video, int x, int y, uint8_t colour, uint32_t colour32)
{
	this->Blitter_8bppBase::SetPixel(video, x, y, colour);
}

void Blitter_8bppBase::DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8_t colour, int width, int dash)
{
	const int screen_pitch = this->GetScreenPitch();
	this->DrawLineGeneric(x, y, x2, y2, screen_width, screen_height, width, dash, [=](int x, int y) {
		*((uint8_t *)video + x + y * screen_pitch) = colour;
	});
}

void Blitter_8bppBase::SetRect(void *video, int x, int y, const uint8_t *colours, uint lines, uint width, uint pitch)
{
	const int screen_pitch = this->GetScreenPitch();
	uint8_t *dst = (uint8_t *)video + x + y * screen_pitch;
	do {
		memcpy(dst, colours, width * sizeof(uint8_t));
		dst += screen_pitch;
		colours += pitch;
	} while (--lines);
}

void Blitter_8bppBase::SetRectNoD7(void *video, int x, int y, const uint8_t *colours, uint lines, uint width, uint pitch)
{
	const int screen_pitch = this->GetScreenPitch();
	uint8_t *dst = (uint8_t *)video + x + y * screen_pitch;
	do {
		for (size_t i = 0; i < width; i++) {
			if (colours[i] != 0xD7) dst[i] = colours[i];
		}
		dst += screen_pitch;
		colours += pitch;
	} while (--lines);
}

void Blitter_8bppBase::DrawRect(void *video, int width, int height, uint8_t colour)
{
	const int screen_pitch = this->GetScreenPitch();
	do {
		memset(video, colour, width);
		video = (uint8_t *)video + screen_pitch;
	} while (--height);
}

void Blitter_8bppBase::DrawRectAt(void *video, int x, int y, int width, int height, uint8_t colour)
{
	this->Blitter_8bppBase::DrawRect((uint8_t *)video + x + y * this->GetScreenPitch(), width, height, colour);
}

void Blitter_8bppBase::CopyFromBuffer(void *video, const void *src, int width, int height)
{
	uint8_t *dst = (uint8_t *)video;
	const uint8_t *usrc = (const uint8_t *)src;
	const int screen_pitch = this->GetScreenPitch();

	for (; height > 0; height--) {
		memcpy(dst, usrc, width * sizeof(uint8_t));
		usrc += width;
		dst += screen_pitch;
	}
}

void Blitter_8bppBase::CopyToBuffer(const void *video, void *dst, int width, int height)
{
	uint8_t *udst = (uint8_t *)dst;
	const uint8_t *src = (const uint8_t *)video;
	const int screen_pitch = this->GetScreenPitch();

	for (; height > 0; height--) {
		memcpy(udst, src, width * sizeof(uint8_t));
		src += screen_pitch;
		udst += width;
	}
}

void Blitter_8bppBase::CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch)
{
	uint8_t *udst = (uint8_t *)dst;
	const uint8_t *src = (const uint8_t *)video;
	const int screen_pitch = this->GetScreenPitch();

	for (; height > 0; height--) {
		memcpy(udst, src, width * sizeof(uint8_t));
		src += screen_pitch;
		udst += dst_pitch;
	}
}

void Blitter_8bppBase::ScrollBuffer(void *video, int left, int top, int width, int height, int scroll_x, int scroll_y)
{
	const uint8_t *src;
	uint8_t *dst;
	const int screen_pitch = this->GetScreenPitch();

	if (scroll_y > 0) {
		/* Calculate pointers */
		dst = (uint8_t *)video + left + (top + height - 1) * screen_pitch;
		src = dst - scroll_y * this->GetScreenPitch();

		/* Decrease height and increase top */
		top += scroll_y;
		height -= scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		} else {
			src -= scroll_x;
			width += scroll_x;
		}

		for (int h = height; h > 0; h--) {
			memcpy(dst, src, width * sizeof(uint8_t));
			src -= screen_pitch;
			dst -= screen_pitch;
		}
	} else {
		/* Calculate pointers */
		dst = (uint8_t *)video + left + top * screen_pitch;
		src = dst - scroll_y * screen_pitch;

		/* Decrease height. (scroll_y is <=0). */
		height += scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		} else {
			src -= scroll_x;
			width += scroll_x;
		}

		/* the y-displacement may be 0 therefore we have to use memmove,
		 * because source and destination may overlap */
		for (int h = height; h > 0; h--) {
			memmove(dst, src, width * sizeof(uint8_t));
			src += screen_pitch;
			dst += screen_pitch;
		}
	}
}

size_t Blitter_8bppBase::BufferSize(uint width, uint height)
{
	return static_cast<size_t>(width) * height;
}

void Blitter_8bppBase::PaletteAnimate(const Palette &)
{
	/* Video backend takes care of the palette animation */
}

Blitter::PaletteAnimation Blitter_8bppBase::UsePaletteAnimation()
{
	return Blitter::PALETTE_ANIMATION_VIDEO_BACKEND;
}
