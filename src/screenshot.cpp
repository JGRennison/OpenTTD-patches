/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file screenshot.cpp The creation of screenshots! */

#include "stdafx.h"
#include "core/backup_type.hpp"
#include "fileio_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "screenshot.h"
#include "blitter/factory.hpp"
#include "zoom_func.h"
#include "core/endian_func.hpp"
#include "sl/saveload.h"
#include "company_base.h"
#include "company_func.h"
#include "strings_func.h"
#include "error.h"
#include "industry.h"
#include "industrytype.h"
#include "textbuf_gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "tile_map.h"
#include "landscape.h"
#include "video/video_driver.hpp"
#include "smallmap_colours.h"
#include "smallmap_gui.h"
#include "screenshot_gui.h"

#include "table/strings.h"

#include "safeguards.h"

static const char * const SCREENSHOT_NAME = "screenshot"; ///< Default filename of a saved screenshot.
static const char * const HEIGHTMAP_NAME  = "heightmap";  ///< Default filename of a saved heightmap.

std::string _screenshot_format_name;  ///< Extension of the current screenshot format (corresponds with #_cur_screenshot_format).
uint _num_screenshot_formats;         ///< Number of available screenshot formats.
uint _cur_screenshot_format;          ///< Index of the currently selected screenshot format in #_screenshot_formats.
static char _screenshot_name[128];    ///< Filename of the screenshot file.
char _full_screenshot_name[MAX_PATH]; ///< Pathname of the screenshot file.
uint _heightmap_highest_peak;         ///< When saving a heightmap, this contains the highest peak on the map.

static const char *_screenshot_aux_text_key = nullptr;
static const char *_screenshot_aux_text_value = nullptr;

void SetScreenshotAuxiliaryText(const char *key, const char *value)
{
	_screenshot_aux_text_key = key;
	_screenshot_aux_text_value = value;
}

/**
 * Callback function signature for generating lines of pixel data to be written to the screenshot file.
 * @param userdata Pointer to user data.
 * @param buf      Destination buffer.
 * @param y        Line number of the first line to write.
 * @param pitch    Number of pixels to write (1 byte for 8bpp, 4 bytes for 32bpp). @see Colour
 * @param n        Number of lines to write.
 */
typedef void ScreenshotCallback(void *userdata, void *buf, uint y, uint pitch, uint n);

/**
 * Function signature for a screenshot generation routine for one of the available formats.
 * @param name        Filename, including extension.
 * @param callb       Callback function for generating lines of pixels.
 * @param userdata    User data, passed on to \a callb.
 * @param w           Width of the image in pixels.
 * @param h           Height of the image in pixels.
 * @param pixelformat Bits per pixel (bpp), either 8 or 32.
 * @param palette     %Colour palette (for 8bpp images).
 * @return File was written successfully.
 */
typedef bool ScreenshotHandlerProc(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette);

/** Screenshot format information. */
struct ScreenshotFormat {
	const char *extension;       ///< File extension.
	ScreenshotHandlerProc *proc; ///< Function for writing the screenshot.
};

#define MKCOLOUR(x) TO_LE32X(x)

/*************************************************
 **** SCREENSHOT CODE FOR WINDOWS BITMAP (.BMP)
 *************************************************/

/** BMP File Header (stored in little endian) */
PACK(struct BitmapFileHeader {
	uint16 type;
	uint32 size;
	uint32 reserved;
	uint32 off_bits;
});
static_assert(sizeof(BitmapFileHeader) == 14);

/** BMP Info Header (stored in little endian) */
struct BitmapInfoHeader {
	uint32 size;
	int32 width, height;
	uint16 planes, bitcount;
	uint32 compression, sizeimage, xpels, ypels, clrused, clrimp;
};
static_assert(sizeof(BitmapInfoHeader) == 40);

/** Format of palette data in BMP header */
struct RgbQuad {
	byte blue, green, red, reserved;
};
static_assert(sizeof(RgbQuad) == 4);

/**
 * Generic .BMP writer
 * @param name file name including extension
 * @param callb callback used for gathering rendered image
 * @param userdata parameters forwarded to \a callb
 * @param w width in pixels
 * @param h height in pixels
 * @param pixelformat bits per pixel
 * @param palette colour palette (for 8bpp mode)
 * @return was everything ok?
 * @see ScreenshotHandlerProc
 */
static bool MakeBMPImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	uint bpp; // bytes per pixel
	switch (pixelformat) {
		case 8:  bpp = 1; break;
		/* 32bpp mode is saved as 24bpp BMP */
		case 32: bpp = 3; break;
		/* Only implemented for 8bit and 32bit images so far */
		default: return false;
	}

	FILE *f = fopen(name, "wb");
	if (f == nullptr) return false;

	/* Each scanline must be aligned on a 32bit boundary */
	uint bytewidth = Align(w * bpp, 4); // bytes per line in file

	/* Size of palette. Only present for 8bpp mode */
	uint pal_size = pixelformat == 8 ? sizeof(RgbQuad) * 256 : 0;

	/* Setup the file header */
	BitmapFileHeader bfh;
	bfh.type = TO_LE16('MB');
	bfh.size = TO_LE32(sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + pal_size + static_cast<size_t>(bytewidth) * h);
	bfh.reserved = 0;
	bfh.off_bits = TO_LE32(sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + pal_size);

	/* Setup the info header */
	BitmapInfoHeader bih;
	bih.size = TO_LE32(sizeof(BitmapInfoHeader));
	bih.width = TO_LE32(w);
	bih.height = TO_LE32(h);
	bih.planes = TO_LE16(1);
	bih.bitcount = TO_LE16(bpp * 8);
	bih.compression = 0;
	bih.sizeimage = 0;
	bih.xpels = 0;
	bih.ypels = 0;
	bih.clrused = 0;
	bih.clrimp = 0;

	/* Write file header and info header */
	if (fwrite(&bfh, sizeof(bfh), 1, f) != 1 || fwrite(&bih, sizeof(bih), 1, f) != 1) {
		fclose(f);
		return false;
	}

	if (pixelformat == 8) {
		/* Convert the palette to the windows format */
		RgbQuad rq[256];
		for (uint i = 0; i < 256; i++) {
			rq[i].red   = palette[i].r;
			rq[i].green = palette[i].g;
			rq[i].blue  = palette[i].b;
			rq[i].reserved = 0;
		}
		/* Write the palette */
		if (fwrite(rq, sizeof(rq), 1, f) != 1) {
			fclose(f);
			return false;
		}
	}

	/* Try to use 64k of memory, store between 16 and 128 lines */
	uint maxlines = Clamp(65536 / (w * pixelformat / 8), 16, 128); // number of lines per iteration

	uint8 *buff = MallocT<uint8>(maxlines * w * pixelformat / 8); // buffer which is rendered to
	uint8 *line = AllocaM(uint8, bytewidth); // one line, stored to file
	memset(line, 0, bytewidth);

	/* Start at the bottom, since bitmaps are stored bottom up */
	do {
		uint n = std::min(h, maxlines);
		h -= n;

		/* Render the pixels */
		callb(userdata, buff, h, w, n);

		/* Write each line */
		while (n-- != 0) {
			if (pixelformat == 8) {
				/* Move to 'line', leave last few pixels in line zeroed */
				memcpy(line, buff + n * w, w);
			} else {
				/* Convert from 'native' 32bpp to BMP-like 24bpp.
				 * Works for both big and little endian machines */
				Colour *src = ((Colour *)buff) + n * w;
				byte *dst = line;
				for (uint i = 0; i < w; i++) {
					dst[i * 3    ] = src[i].b;
					dst[i * 3 + 1] = src[i].g;
					dst[i * 3 + 2] = src[i].r;
				}
			}
			/* Write to file */
			if (fwrite(line, bytewidth, 1, f) != 1) {
				free(buff);
				fclose(f);
				return false;
			}
		}
	} while (h != 0);

	free(buff);
	fclose(f);

	return true;
}

/*********************************************************
 **** SCREENSHOT CODE FOR PORTABLE NETWORK GRAPHICS (.PNG)
 *********************************************************/
#if defined(WITH_PNG)
#include <png.h>

#ifdef PNG_TEXT_SUPPORTED
#include "rev.h"
#include "newgrf_config.h"
#include "ai/ai_info.hpp"
#include "company_base.h"
#include "base_media_base.h"
#endif /* PNG_TEXT_SUPPORTED */

static void PNGAPI png_my_error(png_structp png_ptr, png_const_charp message)
{
	DEBUG(misc, 0, "[libpng] error: %s - %s", message, (const char *)png_get_error_ptr(png_ptr));
	longjmp(png_jmpbuf(png_ptr), 1);
}

static void PNGAPI png_my_warning(png_structp png_ptr, png_const_charp message)
{
	DEBUG(misc, 1, "[libpng] warning: %s - %s", message, (const char *)png_get_error_ptr(png_ptr));
}

/**
 * Generic .PNG file image writer.
 * @param name        Filename, including extension.
 * @param callb       Callback function for generating lines of pixels.
 * @param userdata    User data, passed on to \a callb.
 * @param w           Width of the image in pixels.
 * @param h           Height of the image in pixels.
 * @param pixelformat Bits per pixel (bpp), either 8 or 32.
 * @param palette     %Colour palette (for 8bpp images).
 * @return File was written successfully.
 * @see ScreenshotHandlerProc
 */
static bool MakePNGImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	png_color rq[256];
	FILE *f;
	uint i, y, n;
	uint maxlines;
	uint bpp = pixelformat / 8;
	png_structp png_ptr;
	png_infop info_ptr;

	/* only implemented for 8bit and 32bit images so far. */
	if (pixelformat != 8 && pixelformat != 32) return false;

	f = fopen(name, "wb");
	if (f == nullptr) return false;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, const_cast<char *>(name), png_my_error, png_my_warning);

	if (png_ptr == nullptr) {
		fclose(f);
		return false;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp)nullptr);
		fclose(f);
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(f);
		return false;
	}

	png_init_io(png_ptr, f);

	png_set_filter(png_ptr, 0, PNG_FILTER_NONE);

	png_set_IHDR(png_ptr, info_ptr, w, h, 8, pixelformat == 8 ? PNG_COLOR_TYPE_PALETTE : PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

#ifdef PNG_TEXT_SUPPORTED
	/* Try to add some game metadata to the PNG screenshot so
	 * it's more useful for debugging and archival purposes. */
	png_text_struct text[3];
	memset(text, 0, sizeof(text));
	text[0].key = const_cast<char *>("Software");
	text[0].text = const_cast<char *>(_openttd_revision);
	text[0].text_length = strlen(_openttd_revision);
	text[0].compression = PNG_TEXT_COMPRESSION_NONE;

	char buf[8192];
	char *p = buf;
	p += seprintf(p, lastof(buf), "Graphics set: %s (%u)\n", BaseGraphics::GetUsedSet()->name.c_str(), BaseGraphics::GetUsedSet()->version);
	p = strecpy(p, "NewGRFs:\n", lastof(buf));
	for (const GRFConfig *c = _game_mode == GM_MENU ? nullptr : _grfconfig; c != nullptr; c = c->next) {
		p += seprintf(p, lastof(buf), "%08X ", BSWAP32(c->ident.grfid));
		p = md5sumToString(p, lastof(buf), c->ident.md5sum);
		p += seprintf(p, lastof(buf), " %s\n", c->filename.c_str());
	}
	p = strecpy(p, "\nCompanies:\n", lastof(buf));
	for (const Company *c : Company::Iterate()) {
		if (c->ai_info == nullptr) {
			p += seprintf(p, lastof(buf), "%2i: Human\n", (int)c->index);
		} else {
			p += seprintf(p, lastof(buf), "%2i: %s (v%d)\n", (int)c->index, c->ai_info->GetName(), c->ai_info->GetVersion());
		}
	}
	text[1].key = const_cast<char *>("Description");
	text[1].text = buf;
	text[1].text_length = p - buf;
	text[1].compression = PNG_TEXT_COMPRESSION_zTXt;
	if (_screenshot_aux_text_key && _screenshot_aux_text_value) {
		text[2].key = const_cast<char *>(_screenshot_aux_text_key);
		text[2].text = const_cast<char *>(_screenshot_aux_text_value);
		text[2].text_length = strlen(_screenshot_aux_text_value);
		text[2].compression = PNG_TEXT_COMPRESSION_zTXt;
	}
	png_set_text(png_ptr, info_ptr, text, _screenshot_aux_text_key && _screenshot_aux_text_value ? 3 : 2);
#endif /* PNG_TEXT_SUPPORTED */

	if (pixelformat == 8) {
		/* convert the palette to the .PNG format. */
		for (i = 0; i != 256; i++) {
			rq[i].red   = palette[i].r;
			rq[i].green = palette[i].g;
			rq[i].blue  = palette[i].b;
		}

		png_set_PLTE(png_ptr, info_ptr, rq, 256);
	}

	png_write_info(png_ptr, info_ptr);
	png_set_flush(png_ptr, 512);

	if (pixelformat == 32) {
		png_color_8 sig_bit;

		/* Save exact colour/alpha resolution */
		sig_bit.alpha = 0;
		sig_bit.blue  = 8;
		sig_bit.green = 8;
		sig_bit.red   = 8;
		sig_bit.gray  = 8;
		png_set_sBIT(png_ptr, info_ptr, &sig_bit);

#if TTD_ENDIAN == TTD_LITTLE_ENDIAN
		png_set_bgr(png_ptr);
		png_set_filler(png_ptr, 0, PNG_FILLER_AFTER);
#else
		png_set_filler(png_ptr, 0, PNG_FILLER_BEFORE);
#endif /* TTD_ENDIAN == TTD_LITTLE_ENDIAN */
	}

	/* use by default 64k temp memory */
	maxlines = Clamp(65536 / w, 16, 128);

	/* now generate the bitmap bits */
	void *buff = CallocT<uint8>(static_cast<size_t>(w) * maxlines * bpp); // by default generate 128 lines at a time.

	y = 0;
	do {
		/* determine # lines to write */
		n = std::min(h - y, maxlines);

		/* render the pixels into the buffer */
		callb(userdata, buff, y, w, n);
		y += n;

		/* write them to png */
		for (i = 0; i != n; i++) {
			png_write_row(png_ptr, (png_bytep)buff + i * w * bpp);
		}
	} while (y != h);

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	free(buff);
	fclose(f);
	return true;
}
#endif /* WITH_PNG */


/*************************************************
 **** SCREENSHOT CODE FOR ZSOFT PAINTBRUSH (.PCX)
 *************************************************/

/** Definition of a PCX file header. */
struct PcxHeader {
	byte manufacturer;
	byte version;
	byte rle;
	byte bpp;
	uint32 unused;
	uint16 xmax, ymax;
	uint16 hdpi, vdpi;
	byte pal_small[16 * 3];
	byte reserved;
	byte planes;
	uint16 pitch;
	uint16 cpal;
	uint16 width;
	uint16 height;
	byte filler[54];
};
static_assert(sizeof(PcxHeader) == 128);

/**
 * Generic .PCX file image writer.
 * @param name        Filename, including extension.
 * @param callb       Callback function for generating lines of pixels.
 * @param userdata    User data, passed on to \a callb.
 * @param w           Width of the image in pixels.
 * @param h           Height of the image in pixels.
 * @param pixelformat Bits per pixel (bpp), either 8 or 32.
 * @param palette     %Colour palette (for 8bpp images).
 * @return File was written successfully.
 * @see ScreenshotHandlerProc
 */
static bool MakePCXImage(const char *name, ScreenshotCallback *callb, void *userdata, uint w, uint h, int pixelformat, const Colour *palette)
{
	FILE *f;
	uint maxlines;
	uint y;
	PcxHeader pcx;
	bool success;

	if (pixelformat == 32) {
		DEBUG(misc, 0, "Can't convert a 32bpp screenshot to PCX format. Please pick another format.");
		return false;
	}
	if (pixelformat != 8 || w == 0) return false;

	f = fopen(name, "wb");
	if (f == nullptr) return false;

	memset(&pcx, 0, sizeof(pcx));

	/* setup pcx header */
	pcx.manufacturer = 10;
	pcx.version = 5;
	pcx.rle = 1;
	pcx.bpp = 8;
	pcx.xmax = TO_LE16(w - 1);
	pcx.ymax = TO_LE16(h - 1);
	pcx.hdpi = TO_LE16(320);
	pcx.vdpi = TO_LE16(320);

	pcx.planes = 1;
	pcx.cpal = TO_LE16(1);
	pcx.width = pcx.pitch = TO_LE16(w);
	pcx.height = TO_LE16(h);

	/* write pcx header */
	if (fwrite(&pcx, sizeof(pcx), 1, f) != 1) {
		fclose(f);
		return false;
	}

	/* use by default 64k temp memory */
	maxlines = Clamp(65536 / w, 16, 128);

	/* now generate the bitmap bits */
	uint8 *buff = CallocT<uint8>(static_cast<size_t>(w) * maxlines); // by default generate 128 lines at a time.

	y = 0;
	do {
		/* determine # lines to write */
		uint n = std::min(h - y, maxlines);
		uint i;

		/* render the pixels into the buffer */
		callb(userdata, buff, y, w, n);
		y += n;

		/* write them to pcx */
		for (i = 0; i != n; i++) {
			const uint8 *bufp = buff + i * w;
			byte runchar = bufp[0];
			uint runcount = 1;
			uint j;

			/* for each pixel... */
			for (j = 1; j < w; j++) {
				uint8 ch = bufp[j];

				if (ch != runchar || runcount >= 0x3f) {
					if (runcount > 1 || (runchar & 0xC0) == 0xC0) {
						if (fputc(0xC0 | runcount, f) == EOF) {
							free(buff);
							fclose(f);
							return false;
						}
					}
					if (fputc(runchar, f) == EOF) {
						free(buff);
						fclose(f);
						return false;
					}
					runcount = 0;
					runchar = ch;
				}
				runcount++;
			}

			/* write remaining bytes.. */
			if (runcount > 1 || (runchar & 0xC0) == 0xC0) {
				if (fputc(0xC0 | runcount, f) == EOF) {
					free(buff);
					fclose(f);
					return false;
				}
			}
			if (fputc(runchar, f) == EOF) {
				free(buff);
				fclose(f);
				return false;
			}
		}
	} while (y != h);

	free(buff);

	/* write 8-bit colour palette */
	if (fputc(12, f) == EOF) {
		fclose(f);
		return false;
	}

	/* Palette is word-aligned, copy it to a temporary byte array */
	byte tmp[256 * 3];

	for (uint i = 0; i < 256; i++) {
		tmp[i * 3 + 0] = palette[i].r;
		tmp[i * 3 + 1] = palette[i].g;
		tmp[i * 3 + 2] = palette[i].b;
	}
	success = fwrite(tmp, sizeof(tmp), 1, f) == 1;

	fclose(f);

	return success;
}

/*************************************************
 **** GENERIC SCREENSHOT CODE
 *************************************************/

/** Available screenshot formats. */
static const ScreenshotFormat _screenshot_formats[] = {
#if defined(WITH_PNG)
	{"png", &MakePNGImage},
#endif
	{"bmp", &MakeBMPImage},
	{"pcx", &MakePCXImage},
};

/** Get filename extension of current screenshot file format. */
const char *GetCurrentScreenshotExtension()
{
	return _screenshot_formats[_cur_screenshot_format].extension;
}

/** Initialize screenshot format information on startup, with #_screenshot_format_name filled from the loadsave code. */
void InitializeScreenshotFormats()
{
	uint j = 0;
	for (uint i = 0; i < lengthof(_screenshot_formats); i++) {
		if (_screenshot_format_name.compare(_screenshot_formats[i].extension) == 0) {
			j = i;
			break;
		}
	}
	_cur_screenshot_format = j;
	_num_screenshot_formats = lengthof(_screenshot_formats);
}

/**
 * Callback of the screenshot generator that dumps the current video buffer.
 * @see ScreenshotCallback
 */
static void CurrentScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	void *src = blitter->MoveTo(_screen.dst_ptr, 0, y);
	blitter->CopyImageToBuffer(src, buf, _screen.width, n, pitch);
}

/**
 * generate a large piece of the world
 * @param userdata Viewport area to draw
 * @param buf Videobuffer with same bitdepth as current blitter
 * @param y First line to render
 * @param pitch Pitch of the videobuffer
 * @param n Number of lines to render
 */
static void LargeWorldCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	Viewport *vp = (Viewport *)userdata;
	DrawPixelInfo dpi;
	int wx, left;

	/* We are no longer rendering to the screen */
	DrawPixelInfo old_screen = _screen;
	bool old_disable_anim = _screen_disable_anim;

	_screen.dst_ptr = buf;
	_screen.width = pitch;
	_screen.height = n;
	_screen.pitch = pitch;
	_screen_disable_anim = true;

	Backup dpi_backup(_cur_dpi, &dpi, FILE_LINE);

	dpi.dst_ptr = buf;
	dpi.height = n;
	dpi.width = vp->width;
	dpi.pitch = pitch;
	dpi.zoom = ZOOM_LVL_WORLD_SCREENSHOT;
	dpi.left = 0;
	dpi.top = y;

	/* Render viewport in blocks of 1600 pixels width */
	left = 0;
	while (vp->width - left != 0) {
		wx = std::min(vp->width - left, 1600);
		left += wx;

		ViewportDoDraw(vp,
			ScaleByZoom(left - wx - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom(y - vp->top, vp->zoom) + vp->virtual_top,
			ScaleByZoom(left - vp->left, vp->zoom) + vp->virtual_left,
			ScaleByZoom((y + n) - vp->top, vp->zoom) + vp->virtual_top,
			0
		);
	}

	dpi_backup.Restore();

	ViewportDoDrawProcessAllPending();

	/* Switch back to rendering to the screen */
	_screen = old_screen;
	_screen_disable_anim = old_disable_anim;

	ClearViewportCache(vp);
}

/**
 * Construct a pathname for a screenshot file.
 * @param default_fn Default filename.
 * @param ext        Extension to use.
 * @param crashlog   Create path for crash.png
 * @return Pathname for a screenshot file.
 */
static const char *MakeScreenshotName(const char *default_fn, const char *ext, bool crashlog = false)
{
	bool generate = StrEmpty(_screenshot_name);

	if (generate) {
		if (_game_mode == GM_EDITOR || _game_mode == GM_MENU || _local_company == COMPANY_SPECTATOR) {
			strecpy(_screenshot_name, default_fn, lastof(_screenshot_name));
		} else {
			GenerateDefaultSaveName(_screenshot_name, lastof(_screenshot_name));
		}
	}

	size_t len = strlen(_screenshot_name);

	/* Handle user-specified filenames ending in %d or # with automatic numbering */
	if (len >= 2 && _screenshot_name[len - 2] == '%' && _screenshot_name[len - 1] == 'd') {
		generate = true;
		len -= 2;
		_screenshot_name[len] = '\0';
	} else if (len >= 1 && _screenshot_name[len - 1] == '#') {
		generate = true;
		len -= 1;
		_screenshot_name[len] = '\0';
	}

	/* Add extension to screenshot file */
	seprintf(&_screenshot_name[len], lastof(_screenshot_name), ".%s", ext);

	const char *screenshot_dir = crashlog ? _personal_dir.c_str() : FiosGetScreenshotDir();

	for (uint serial = 1;; serial++) {
		if (seprintf(_full_screenshot_name, lastof(_full_screenshot_name), "%s%s", screenshot_dir, _screenshot_name) >= (int)lengthof(_full_screenshot_name)) {
			/* We need more characters than MAX_PATH -> end with error */
			_full_screenshot_name[0] = '\0';
			break;
		}
		if (!generate) break; // allow overwriting of non-automatic filenames
		if (!FileExists(_full_screenshot_name)) break;
		/* If file exists try another one with same name, but just with a higher index */
		seprintf(&_screenshot_name[len], lastof(_screenshot_name) - len, "#%u.%s", serial, ext);
	}

	return _full_screenshot_name;
}

/** Make a screenshot of the current screen. */
static bool MakeSmallScreenshot(bool crashlog)
{
	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension, crashlog), CurrentScreenCallback, nullptr, _screen.width, _screen.height,
			BlitterFactory::GetCurrentBlitter()->GetScreenDepth(), _cur_palette.palette);
}

/**
 * Configure a Viewport for rendering (a part of) the map into a screenshot.
 * @param t Screenshot type
 * @param width the width of the screenshot, or 0 for current viewport width (needs to be 0 with SC_VIEWPORT, SC_CRASHLOG, and SC_WORLD).
 * @param height the height of the screenshot, or 0 for current viewport height (needs to be 0 with SC_VIEWPORT, SC_CRASHLOG, and SC_WORLD).
 * @param[out] vp Result viewport
 */
void SetupScreenshotViewport(ScreenshotType t, Viewport *vp, uint32 width, uint32 height)
{
	switch(t) {
		case SC_VIEWPORT:
		case SC_CRASHLOG: {
			assert(width == 0 && height == 0);

			Window *w = GetMainWindow();
			vp->virtual_left   = w->viewport->virtual_left;
			vp->virtual_top    = w->viewport->virtual_top;
			vp->virtual_width  = w->viewport->virtual_width;
			vp->virtual_height = w->viewport->virtual_height;

			/* Compute pixel coordinates */
			vp->left = 0;
			vp->top = 0;
			vp->width = _screen.width;
			vp->height = _screen.height;
			vp->overlay = w->viewport->overlay;
			break;
		}
		case SC_WORLD:
		case SC_WORLD_ZOOM: {
			assert(width == 0 && height == 0);

			/* Determine world coordinates of screenshot */
			if (t == SC_WORLD_ZOOM) {
				Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
				vp->zoom =  w->viewport->zoom;
				vp->map_type = w->viewport->map_type;
			} else {
				vp->zoom = ZOOM_LVL_WORLD_SCREENSHOT;
			}

			TileIndex north_tile = _settings_game.construction.freeform_edges ? TileXY(1, 1) : TileXY(0, 0);
			TileIndex south_tile = MapSize() - 1;

			/* We need to account for a hill or high building at tile 0,0. */
			int extra_height_top = TilePixelHeight(north_tile) + 150;
			/* If there is a hill at the bottom don't create a large black area. */
			int reclaim_height_bottom = TilePixelHeight(south_tile);

			vp->virtual_left   = RemapCoords(TileX(south_tile) * TILE_SIZE, TileY(north_tile) * TILE_SIZE, 0).x;
			vp->virtual_top    = RemapCoords(TileX(north_tile) * TILE_SIZE, TileY(north_tile) * TILE_SIZE, extra_height_top).y;
			vp->virtual_width  = RemapCoords(TileX(north_tile) * TILE_SIZE, TileY(south_tile) * TILE_SIZE, 0).x                     - vp->virtual_left + 1;
			vp->virtual_height = RemapCoords(TileX(south_tile) * TILE_SIZE, TileY(south_tile) * TILE_SIZE, reclaim_height_bottom).y - vp->virtual_top  + 1;

			/* Compute pixel coordinates */
			vp->left = 0;
			vp->top = 0;
			vp->width  = UnScaleByZoom(vp->virtual_width,  vp->zoom);
			vp->height = UnScaleByZoom(vp->virtual_height, vp->zoom);
			vp->overlay = nullptr;
			break;
		}
		default: {
			vp->zoom = (t == SC_ZOOMEDIN) ? _settings_client.gui.zoom_min : ZOOM_LVL_VIEWPORT;

			Window *w = GetMainWindow();
			vp->virtual_left   = w->viewport->virtual_left;
			vp->virtual_top    = w->viewport->virtual_top;

			if (width == 0 || height == 0) {
				vp->virtual_width  = w->viewport->virtual_width;
				vp->virtual_height = w->viewport->virtual_height;
			} else {
				vp->virtual_width = width << vp->zoom;
				vp->virtual_height = height << vp->zoom;
			}

			/* Compute pixel coordinates */
			vp->left = 0;
			vp->top = 0;
			vp->width  = UnScaleByZoom(vp->virtual_width,  vp->zoom);
			vp->height = UnScaleByZoom(vp->virtual_height, vp->zoom);
			vp->overlay = nullptr;
			break;
		}
	}
	UpdateViewportSizeZoom(vp);
}

/**
 * Make a screenshot of the map.
 * @param t Screenshot type: World or viewport screenshot
 * @param width the width of the screenshot of, or 0 for current viewport width.
 * @param height the height of the screenshot of, or 0 for current viewport height.
 * @return true on success
 */
static bool MakeLargeWorldScreenshot(ScreenshotType t, uint32 width = 0, uint32 height = 0)
{
	Viewport vp;
	SetupScreenshotViewport(t, &vp, width, height);

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), LargeWorldCallback, &vp, vp.width, vp.height,
			BlitterFactory::GetCurrentBlitter()->GetScreenDepth(), _cur_palette.palette);
}

/**
 * Callback for generating a heightmap. Supports 8bpp grayscale only.
 * @param userdata Pointer to user data.
 * @param buffer   Destination buffer.
 * @param y        Line number of the first line to write.
 * @param pitch    Number of pixels to write (1 byte for 8bpp, 4 bytes for 32bpp). @see Colour
 * @param n        Number of lines to write.
 * @see ScreenshotCallback
 */
static void HeightmapCallback(void *userdata, void *buffer, uint y, uint pitch, uint n)
{
	byte *buf = (byte *)buffer;
	while (n > 0) {
		TileIndex ti = TileXY(MapMaxX(), y);
		for (uint x = MapMaxX(); true; x--) {
			*buf = 256 * TileHeight(ti) / (1 + _heightmap_highest_peak);
			buf++;
			if (x == 0) break;
			ti = TILE_ADDXY(ti, -1, 0);
		}
		y++;
		n--;
	}
}

/**
 * Make a heightmap of the current map.
 * @param filename Filename to use for saving.
 */
bool MakeHeightmapScreenshot(const char *filename)
{
	Colour palette[256];
	for (uint i = 0; i < lengthof(palette); i++) {
		palette[i].a = 0xff;
		palette[i].r = i;
		palette[i].g = i;
		palette[i].b = i;
	}

	_heightmap_highest_peak = 0;
	for (TileIndex tile = 0; tile < MapSize(); tile++) {
		uint h = TileHeight(tile);
		_heightmap_highest_peak = std::max(h, _heightmap_highest_peak);
	}

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(filename, HeightmapCallback, nullptr, MapSizeX(), MapSizeY(), 8, palette);
}

static ScreenshotType _confirmed_screenshot_type; ///< Screenshot type the current query is about to confirm.

/**
 * Callback on the confirmation window for huge screenshots.
 * @param w Window with viewport
 * @param confirmed true on confirmation
 */
static void ScreenshotConfirmationCallback(Window *w, bool confirmed)
{
	if (confirmed) MakeScreenshot(_confirmed_screenshot_type, {});
}

/**
 * Make a screenshot.
 * Ask for confirmation first if the screenshot will be huge.
 * @param t Screenshot type: World, defaultzoom, heightmap or viewport screenshot
 * @see MakeScreenshot
 */
void MakeScreenshotWithConfirm(ScreenshotType t)
{
	Viewport vp;
	SetupScreenshotViewport(t, &vp);

	bool heightmap_or_minimap = t == SC_HEIGHTMAP || t == SC_MINIMAP || t == SC_TOPOGRAPHY || t == SC_INDUSTRY;
	uint64_t width = (heightmap_or_minimap ? MapSizeX() : vp.width);
	uint64_t height = (heightmap_or_minimap ? MapSizeY() : vp.height);

	if (width * height > 8192 * 8192) {
		/* Ask for confirmation */
		_confirmed_screenshot_type = t;
		SetDParam(0, width);
		SetDParam(1, height);
		ShowQuery(STR_WARNING_SCREENSHOT_SIZE_CAPTION, STR_WARNING_SCREENSHOT_SIZE_MESSAGE, nullptr, ScreenshotConfirmationCallback);
	} else {
		/* Less than 64M pixels, just do it */
		MakeScreenshot(t, {});
	}
}

/**
 * Show a a success or failure message indicating the result of a screenshot action
 * @param ret  whether the screenshot action was successful
 */
static void ShowScreenshotResultMessage(ScreenshotType t, bool ret)
{
	if (ret) {
		if (t == SC_HEIGHTMAP) {
			SetDParamStr(0, _screenshot_name);
			SetDParam(1, _heightmap_highest_peak);
			ShowErrorMessage(STR_MESSAGE_HEIGHTMAP_SUCCESSFULLY, INVALID_STRING_ID, WL_WARNING);
		} else {
			SetDParamStr(0, _screenshot_name);
			ShowErrorMessage(STR_MESSAGE_SCREENSHOT_SUCCESSFULLY, INVALID_STRING_ID, WL_WARNING);
		}
	} else {
		ShowErrorMessage(STR_ERROR_SCREENSHOT_FAILED, INVALID_STRING_ID, WL_ERROR);
	}
}

/**
 * Make a screenshot.
 * @param t    the type of screenshot to make.
 * @param name the name to give to the screenshot.
 * @param width the width of the screenshot of, or 0 for current viewport width (only works for SC_ZOOMEDIN and SC_DEFAULTZOOM).
 * @param height the height of the screenshot of, or 0 for current viewport height (only works for SC_ZOOMEDIN and SC_DEFAULTZOOM).
 * @return true iff the screenshot was made successfully
 */
static bool RealMakeScreenshot(ScreenshotType t, std::string name, uint32 width, uint32 height)
{
	if (t == SC_VIEWPORT) {
		/* First draw the dirty parts of the screen and only then change the name
		 * of the screenshot. This way the screenshot will always show the name
		 * of the previous screenshot in the 'successful' message instead of the
		 * name of the new screenshot (or an empty name). */
		SetScreenshotWindowHidden(true);
		UndrawMouseCursor();
		DrawDirtyBlocks();
		SetScreenshotWindowHidden(false);
	}

	_screenshot_name[0] = '\0';
	if (!name.empty()) strecpy(_screenshot_name, name.c_str(), lastof(_screenshot_name));

	bool ret;
	switch (t) {
		case SC_VIEWPORT:
			ret = MakeSmallScreenshot(false);
			break;

		case SC_CRASHLOG:
			ret = MakeSmallScreenshot(true);
			break;

		case SC_ZOOMEDIN:
		case SC_DEFAULTZOOM:
			ret = MakeLargeWorldScreenshot(t, width, height);
			break;

		case SC_WORLD:
		case SC_WORLD_ZOOM:
			ret = MakeLargeWorldScreenshot(t);
			break;

		case SC_HEIGHTMAP: {
			const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
			ret = MakeHeightmapScreenshot(MakeScreenshotName(HEIGHTMAP_NAME, sf->extension));
			break;
		}

		case SC_MINIMAP:
			ret = MakeMinimapWorldScreenshot(name.empty() ? nullptr : name.c_str());
			break;

		case SC_TOPOGRAPHY:
			ret = MakeTopographyScreenshot(name.empty() ? nullptr : name.c_str());
			break;

		case SC_INDUSTRY:
			ret = MakeIndustryScreenshot(name.empty() ? nullptr : name.c_str());
			break;

		default:
			NOT_REACHED();
	}

	ShowScreenshotResultMessage(t, ret);

	return ret;
}

/**
 * Schedule making a screenshot.
 * Unconditionally take a screenshot of the requested type.
 * @param t    the type of screenshot to make.
 * @param name the name to give to the screenshot.
 * @param width the width of the screenshot of, or 0 for current viewport width (only works for SC_ZOOMEDIN and SC_DEFAULTZOOM).
 * @param height the height of the screenshot of, or 0 for current viewport height (only works for SC_ZOOMEDIN and SC_DEFAULTZOOM).
 * @return true iff the screenshot was successfully made.
 * @see MakeScreenshotWithConfirm
 */
bool MakeScreenshot(ScreenshotType t, std::string name, uint32 width, uint32 height)
{
	if (t == SC_CRASHLOG) {
		/* Video buffer might or might not be locked. */
		VideoDriver::VideoBufferLocker lock;

		return RealMakeScreenshot(t, name, width, height);
	}

	VideoDriver::GetInstance()->QueueOnMainThread([=] { // Capture by value to not break scope.
		RealMakeScreenshot(t, name, width, height);
	});

	return true;
}

/**
 * Callback for generating a smallmap screenshot.
 * @param userdata SmallMapWindow window pointer
 * @param buf Videobuffer with same bitdepth as current blitter
 * @param y First line to render
 * @param pitch Pitch of the videobuffer
 * @param n Number of lines to render
 */
static void SmallMapCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	SmallMapWindow *window = static_cast<SmallMapWindow *>(userdata);
	window->ScreenshotCallbackHandler(buf, y, pitch, n);
}

/**
 * Make a screenshot of the smallmap
 * @param width   the width of the screenshot
 * @param height  the height of the screenshot
 * @param window  a pointer to the smallmap window to use, the current mode and zoom status of the window is used for the screenshot
 * @return true iff the screenshot was made successfully
 */
bool MakeSmallMapScreenshot(unsigned int width, unsigned int height, SmallMapWindow *window)
{
	_screenshot_name[0] = '\0';
	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	bool ret = sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), SmallMapCallback, window, width, height, BlitterFactory::GetCurrentBlitter()->GetScreenDepth(), _cur_palette.palette);
	ShowScreenshotResultMessage(SC_SMALLMAP, ret);
	return ret;
}

/**
 * Return the owner of a tile to display it with in the small map in mode "Owner".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The owner of tile in the small map in mode "Owner"
 */
static Owner GetMinimapOwner(TileIndex tile)
{
	Owner o;

	if (IsTileType(tile, MP_VOID)) {
		return OWNER_END;
	} else {
		switch (GetTileType(tile)) {
		case MP_INDUSTRY: o = OWNER_DEITY;        break;
		case MP_HOUSE:    o = OWNER_TOWN;         break;
		default:          o = GetTileOwner(tile); break;
			/* FIXME: For MP_ROAD there are multiple owners.
			 * GetTileOwner returns the rail owner (level crossing) resp. the owner of ROADTYPE_ROAD (normal road),
			 * even if there are no ROADTYPE_ROAD bits on the tile.
			 */
		}

		return o;
	}
}

/**
 * Return the color value of a tile to display it with in the topography screenshot.
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The color palette value
 */
static byte GetTopographyValue(TileIndex tile)
{
	const auto tile_type = GetTileType(tile);

	if (tile_type == MP_STATION) {
		switch (GetStationType(tile)) {
			case STATION_RAIL:
				return MKCOLOUR(PC_GREY);
			case STATION_AIRPORT:
				return MKCOLOUR(PC_GREY);
			case STATION_TRUCK:
				return MKCOLOUR(PC_BLACK);
			case STATION_BUS:
				return MKCOLOUR(PC_BLACK);
			case STATION_OILRIG: // FALLTHROUGH
			case STATION_DOCK:
				return MKCOLOUR(PC_GREY);
			case STATION_BUOY:
				return MKCOLOUR(PC_WATER);
			case STATION_WAYPOINT:
				return MKCOLOUR(PC_GREY);
			case STATION_ROADWAYPOINT:
				return MKCOLOUR(PC_GREY);
			default: NOT_REACHED();
		}
	}

	if (IsBridgeAbove(tile)) {
		return MKCOLOUR(PC_DARK_GREY);
	}

	switch (tile_type) {
		case MP_TUNNELBRIDGE:
			return MKCOLOUR(PC_DARK_GREY);
		case MP_RAILWAY:
			return MKCOLOUR(PC_GREY);
		case MP_ROAD:
			return MKCOLOUR(PC_BLACK);
		case MP_HOUSE:
			return MKCOLOUR(0xB5);
		case MP_WATER:
			return MKCOLOUR(PC_WATER);
		case MP_INDUSTRY:
			return MKCOLOUR(0xA2);
		default: {
			const auto tile_z = GetTileZ(tile);
			const auto max_z = _settings_game.construction.map_height_limit;
			const auto color_index = (tile_z * 16) / max_z;

			switch (color_index) {
				case 0:
					return MKCOLOUR(0x50);
				case 1:
					return MKCOLOUR(0x51);
				case 2:
					return MKCOLOUR(0x52);
				case 3:
					return MKCOLOUR(0x53);
				case 4:
					return MKCOLOUR(0x54);
				case 5:
					return MKCOLOUR(0x55);
				case 6:
					return MKCOLOUR(0x56);
				case 7:
					return MKCOLOUR(0x57);
				case 8:
					return MKCOLOUR(0x3B);
				case 9:
					return MKCOLOUR(0x3A);
				case 10:
					return MKCOLOUR(0x39);
				case 11:
					return MKCOLOUR(0x38);
				case 12:
					return MKCOLOUR(0x37);
				case 13:
					return MKCOLOUR(0x36);
				case 14:
					return MKCOLOUR(0x35);
				case 15:
					return MKCOLOUR(0x69);
				default:
					return MKCOLOUR(0x46);
			}
		}
	}
}

/**
 * Return the color value of a tile to display it with in the industries screenshot.
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The color palette value
 */
static byte GetIndustryValue(TileIndex tile)
{
	const auto tile_type = GetTileType(tile);

	if (tile_type == MP_STATION) {
		switch (GetStationType(tile)) {
			case STATION_RAIL:
				return MKCOLOUR(PC_DARK_GREY);
			case STATION_AIRPORT:
				return MKCOLOUR(GREY_SCALE(12));
			case STATION_TRUCK:
				return MKCOLOUR(PC_GREY);
			case STATION_BUS:
				return MKCOLOUR(PC_GREY);
			case STATION_OILRIG: // FALLTHROUGH
			case STATION_DOCK:
				return MKCOLOUR(PC_GREY);
			case STATION_BUOY:
				return MKCOLOUR(PC_BLACK);
			case STATION_WAYPOINT:
				return MKCOLOUR(PC_GREY);
			case STATION_ROADWAYPOINT:
				return MKCOLOUR(PC_GREY);
			default: NOT_REACHED();
		}
	}

	if (IsBridgeAbove(tile)) {
		return MKCOLOUR(GREY_SCALE(12));
	}

	switch (tile_type) {
		case MP_TUNNELBRIDGE:
			return MKCOLOUR(GREY_SCALE(12));
		case MP_RAILWAY:
			return MKCOLOUR(PC_DARK_GREY);
		case MP_ROAD:
			return MKCOLOUR(PC_GREY);
		case MP_HOUSE:
			return MKCOLOUR(GREY_SCALE(4));
		case MP_WATER:
			return MKCOLOUR(0x12);
		case MP_INDUSTRY: {
			const IndustryType industry_type = Industry::GetByTile(tile)->type;

			return GetIndustrySpec(industry_type)->map_colour;
		}
		default:
			return MKCOLOUR(GREY_SCALE(2));
	}
}

template <typename T>
void MinimapScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n, T colorCallback)
{
	uint32 *ubuf = (uint32 *)buf;
	uint num = (pitch * n);
	for (uint i = 0; i < num; i++) {
		uint row = y + (int)(i / pitch);
		uint col = (MapSizeX() - 1) - (i % pitch);

		TileIndex tile = TileXY(col, row);
		byte val = colorCallback(tile);

		uint32 colour_buf = 0;
		colour_buf  = (_cur_palette.palette[val].b << 0);
		colour_buf |= (_cur_palette.palette[val].g << 8);
		colour_buf |= (_cur_palette.palette[val].r << 16);

		*ubuf = colour_buf;
		ubuf++;   // Skip alpha
	}
}

/**
 * Return the color value of a tile to display it with in the minimap screenshot.
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The color palette value
 */
static void MinimapScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	/* Fill with the company colours */
	byte owner_colours[OWNER_END + 1];
	for (const Company *c : Company::Iterate()) {
		owner_colours[c->index] = MKCOLOUR(_colour_gradient[c->colour][5]);
	}

	/* Fill with some special colours */
	owner_colours[OWNER_TOWN]    = PC_DARK_RED;
	owner_colours[OWNER_NONE]    = PC_GRASS_LAND;
	owner_colours[OWNER_WATER]   = PC_WATER;
	owner_colours[OWNER_DEITY]   = PC_DARK_GREY; // industry
	owner_colours[OWNER_END]     = PC_BLACK;

	MinimapScreenCallback(userdata, buf, y, pitch, n, [&](TileIndex tile) -> byte {
		return owner_colours[GetMinimapOwner(tile)];
	});
}

static void TopographyScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	MinimapScreenCallback(userdata, buf, y, pitch, n, GetTopographyValue);
}

static void IndustryScreenCallback(void *userdata, void *buf, uint y, uint pitch, uint n)
{
	MinimapScreenCallback(userdata, buf, y, pitch, n, GetIndustryValue);
}

/**
 * Make a minimap screenshot.
 */
bool MakeMinimapWorldScreenshot(const char *name)
{
	_screenshot_name[0] = '\0';
	if (name != nullptr) strecpy(_screenshot_name, name, lastof(_screenshot_name));

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), MinimapScreenCallback, nullptr, MapSizeX(), MapSizeY(), 32, _cur_palette.palette);
}

/**
 * Make a topography screenshot.
 */
bool MakeTopographyScreenshot(const char *name)
{
	_screenshot_name[0] = '\0';
	if (name != nullptr) strecpy(_screenshot_name, name, lastof(_screenshot_name));

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), TopographyScreenCallback, nullptr, MapSizeX(), MapSizeY(), 32, _cur_palette.palette);
}

/**
 * Make an industry screenshot.
 */
bool MakeIndustryScreenshot(const char *name)
{
	_screenshot_name[0] = '\0';
	if (name != nullptr) strecpy(_screenshot_name, name, lastof(_screenshot_name));

	const ScreenshotFormat *sf = _screenshot_formats + _cur_screenshot_format;
	return sf->proc(MakeScreenshotName(SCREENSHOT_NAME, sf->extension), IndustryScreenCallback, nullptr, MapSizeX(), MapSizeY(), 32, _cur_palette.palette);
}
