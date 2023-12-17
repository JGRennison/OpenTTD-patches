/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gfx_func.h Functions related to the gfx engine. */

/**
 * @defgroup dirty Dirty
 *
 * Handles the repaint of some part of the screen.
 *
 * Some places in the code are called functions which makes something "dirty".
 * This has nothing to do with making a Tile or Window darker or less visible.
 * This term comes from memory caching and is used to define an object must
 * be repaint. If some data of an object (like a Tile, Window, Vehicle, whatever)
 * are changed which are so extensive the object must be repaint its marked
 * as "dirty". The video driver repaint this object instead of the whole screen
 * (this is btw. also possible if needed). This is used to avoid a
 * flickering of the screen by the video driver constantly repainting it.
 *
 * This whole mechanism was controlled by an rectangle defined in #_invalid_rect. This
 * rectangle defines the area on the screen which must be repaint. If a new object
 * needs to be repainted this rectangle is extended to 'catch' the object on the
 * screen. At some point (which is normally uninteresting for patch writers) this
 * rectangle is send to the video drivers method
 * VideoDriver::MakeDirty and it is truncated back to an empty rectangle. At some
 * later point (which is uninteresting, too) the video driver
 * repaints all these saved rectangle instead of the whole screen and drop the
 * rectangle information. Then a new round begins by marking objects "dirty".
 *
 * @see VideoDriver::MakeDirty
 * @see _screen
 */


#ifndef GFX_FUNC_H
#define GFX_FUNC_H

#include "gfx_type.h"
#include "strings_type.h"
#include "string_type.h"
#include <vector>

void GameLoop();

void CreateConsole();

extern byte _dirkeys;        ///< 1 = left, 2 = up, 4 = right, 8 = down
extern bool _fullscreen;
extern byte _support8bpp;
extern CursorVars _cursor;
extern bool _ctrl_pressed;   ///< Is Ctrl pressed?
extern bool _shift_pressed;  ///< Is Shift pressed?
extern bool _invert_ctrl;
extern bool _invert_shift;
extern uint16 _game_speed;
extern uint8 _milliseconds_per_tick;
extern float _ticks_per_second;

extern bool _left_button_down;
extern bool _left_button_clicked;
extern bool _right_button_down;
extern bool _right_button_clicked;

extern DrawPixelInfo _screen;
extern bool _screen_disable_anim;   ///< Disable palette animation (important for 32bpp-anim blitter during giant screenshot)

extern std::vector<Dimension> _resolutions;
extern Dimension _cur_resolution;
extern Palette _cur_palette; ///< Current palette

extern DrawPixelInfo *_cur_dpi;

void HandleToolbarHotkey(int hotkey);
void HandleKeypress(uint keycode, WChar key);
void HandleTextInput(const char *str, bool marked = false, const char *caret = nullptr, const char *insert_location = nullptr, const char *replacement_end = nullptr);
void HandleCtrlChanged();
void HandleShiftChanged();
void HandleMouseEvents();
void UpdateWindows();
void ChangeGameSpeed(bool enable_fast_forward);
void SetupTickRate();

void DrawMouseCursor();
void ScreenSizeChanged();
void GameSizeChanged();
void UndrawMouseCursor();

enum AdjustGUIZoomMode {
	AGZM_MANUAL,
	AGZM_AUTOMATIC,
	AGZM_STARTUP,
};
bool AdjustGUIZoom(AdjustGUIZoomMode mode);

/** Size of the buffer used for drawing strings. */
static const int DRAW_STRING_BUFFER = 2048;

void RedrawScreenRect(int left, int top, int right, int bottom);

Dimension GetSpriteSize(SpriteID sprid, Point *offset = nullptr, ZoomLevel zoom = ZOOM_LVL_GUI);
Dimension GetScaledSpriteSize(SpriteID sprid); /* widget.cpp */
struct SpritePointerHolder;
void DrawSpriteViewport(const SpritePointerHolder &sprite_store, const DrawPixelInfo *dpi, SpriteID img, PaletteID pal, int x, int y, const SubSprite *sub = nullptr);
void PrepareDrawSpriteViewportSpriteStore(SpritePointerHolder &sprite_store, const DrawPixelInfo *dpi, SpriteID img, PaletteID pal);
void DrawSprite(SpriteID img, PaletteID pal, int x, int y, const SubSprite *sub = nullptr, ZoomLevel zoom = ZOOM_LVL_GUI);
void DrawSpriteIgnorePadding(SpriteID img, PaletteID pal, const Rect &r, bool clicked, StringAlignment align); /* widget.cpp */
std::unique_ptr<uint32[]> DrawSpriteToRgbaBuffer(SpriteID spriteId, ZoomLevel zoom = ZOOM_LVL_GUI);

int DrawString(int left, int right, int top, std::string_view str, TextColour colour = TC_FROMSTRING, StringAlignment align = SA_LEFT, bool underline = false, FontSize fontsize = FS_NORMAL);
int DrawString(int left, int right, int top, StringID str, TextColour colour = TC_FROMSTRING, StringAlignment align = SA_LEFT, bool underline = false, FontSize fontsize = FS_NORMAL);
int DrawStringMultiLine(int left, int right, int top, int bottom, std::string_view str, TextColour colour = TC_FROMSTRING, StringAlignment align = (SA_TOP | SA_LEFT), bool underline = false, FontSize fontsize = FS_NORMAL);
int DrawStringMultiLine(int left, int right, int top, int bottom, StringID str, TextColour colour = TC_FROMSTRING, StringAlignment align = (SA_TOP | SA_LEFT), bool underline = false, FontSize fontsize = FS_NORMAL);

void DrawCharCentered(WChar c, const Rect &r, TextColour colour);

void GfxFillRect(const DrawPixelInfo *dpi, int left, int top, int right, int bottom, int colour, FillRectMode mode = FILLRECT_OPAQUE);
inline void GfxFillRect(int left, int top, int right, int bottom, int colour, FillRectMode mode = FILLRECT_OPAQUE) { GfxFillRect(_cur_dpi, left, top, right, bottom, colour, mode); }
void GfxFillPolygon(const std::vector<Point> &shape, int colour, FillRectMode mode = FILLRECT_OPAQUE, GfxFillRectModeFunctor *fill_functor = nullptr);
void GfxDrawLine(const DrawPixelInfo *dpi, int left, int top, int right, int bottom, int colour, int width = 1, int dash = 0);
inline void GfxDrawLine(int left, int top, int right, int bottom, int colour, int width = 1, int dash = 0) { GfxDrawLine(_cur_dpi, left, top, right, bottom, colour, width, dash); }
void DrawBox(const DrawPixelInfo *dpi, int x, int y, int dx1, int dy1, int dx2, int dy2, int dx3, int dy3);
void DrawRectOutline(const Rect &r, int colour, int width = 1, int dash = 0);

/* Versions of DrawString/DrawStringMultiLine that accept a Rect instead of separate left, right, top and bottom parameters. */
static inline int DrawString(const Rect &r, std::string_view str, TextColour colour = TC_FROMSTRING, StringAlignment align = SA_LEFT, bool underline = false, FontSize fontsize = FS_NORMAL)
{
	return DrawString(r.left, r.right, r.top, str, colour, align, underline, fontsize);
}

static inline int DrawString(const Rect &r, StringID str, TextColour colour = TC_FROMSTRING, StringAlignment align = SA_LEFT, bool underline = false, FontSize fontsize = FS_NORMAL)
{
	return DrawString(r.left, r.right, r.top, str, colour, align, underline, fontsize);
}

static inline int DrawStringMultiLine(const Rect &r, std::string_view str, TextColour colour = TC_FROMSTRING, StringAlignment align = (SA_TOP | SA_LEFT), bool underline = false, FontSize fontsize = FS_NORMAL)
{
	return DrawStringMultiLine(r.left, r.right, r.top, r.bottom, str, colour, align, underline, fontsize);
}

static inline int DrawStringMultiLine(const Rect &r, StringID str, TextColour colour = TC_FROMSTRING, StringAlignment align = (SA_TOP | SA_LEFT), bool underline = false, FontSize fontsize = FS_NORMAL)
{
	return DrawStringMultiLine(r.left, r.right, r.top, r.bottom, str, colour, align, underline, fontsize);
}

static inline void GfxFillRect(const Rect &r, int colour, FillRectMode mode = FILLRECT_OPAQUE)
{
	GfxFillRect(r.left, r.top, r.right, r.bottom, colour, mode);
}

Dimension GetStringBoundingBox(std::string_view str, FontSize start_fontsize = FS_NORMAL);
Dimension GetStringBoundingBox(StringID strid, FontSize start_fontsize = FS_NORMAL);
uint GetStringListWidth(const StringID *list, FontSize fontsize = FS_NORMAL);
int GetStringHeight(std::string_view str, int maxw, FontSize fontsize = FS_NORMAL);
int GetStringHeight(StringID str, int maxw);
int GetStringLineCount(StringID str, int maxw);
Dimension GetStringMultiLineBoundingBox(StringID str, const Dimension &suggestion);
Dimension GetStringMultiLineBoundingBox(std::string_view str, const Dimension &suggestion);
void LoadStringWidthTable(bool monospace = false);
Point GetCharPosInString(std::string_view str, const char *ch, FontSize start_fontsize = FS_NORMAL);
ptrdiff_t GetCharAtPosition(std::string_view str, int x, FontSize start_fontsize = FS_NORMAL);

void DrawDirtyBlocks();
void SetDirtyBlocks(int left, int top, int right, int bottom);
void SetPendingDirtyBlocks(int left, int top, int right, int bottom);
void UnsetDirtyBlocks(int left, int top, int right, int bottom);
void MarkWholeScreenDirty();

void GfxInitPalettes();
void CheckBlitter();

bool FillDrawPixelInfo(DrawPixelInfo *n, int left, int top, int width, int height);

/**
 * Determine where to draw a centred object inside a widget.
 * @param min The top or left coordinate.
 * @param max The bottom or right coordinate.
 * @param size The height or width of the object to draw.
 * @return Offset of where to start drawing the object.
 */
static inline int CenterBounds(int min, int max, int size)
{
	return (min + max - size + 1) / 2;
}

/* window.cpp */
enum DrawOverlappedWindowFlags {
	DOWF_NONE         =      0,
	DOWF_MARK_DIRTY   = 1 << 0,
	DOWF_SHOW_DEBUG   = 1 << 1,
};
DECLARE_ENUM_AS_BIT_SET(DrawOverlappedWindowFlags)
void DrawOverlappedWindowForAll(int left, int top, int right, int bottom);

void SetMouseCursorBusy(bool busy);
void SetMouseCursor(CursorID cursor, PaletteID pal);
void SetAnimatedMouseCursor(const AnimCursor *table);
void CursorTick();
void UpdateCursorSize();
void UpdateRouteStepSpriteSize();
bool ChangeResInGame(int w, int h);
void SortResolutions();
bool ToggleFullScreen(bool fs);

/* gfx.cpp */
byte GetCharacterWidth(FontSize size, WChar key);
byte GetDigitWidth(FontSize size = FS_NORMAL);
void GetBroadestDigit(uint *front, uint *next, FontSize size = FS_NORMAL);
uint64 GetBroadestDigitsValue(uint count, FontSize size = FS_NORMAL);

extern int font_height_cache[FS_END];

/**
 * Get height of a character for a given font size.
 * @param size Font size to get height of
 * @return     Height of characters in the given font (pixels)
 */
inline int GetCharacterHeight(FontSize size)
{
	return font_height_cache[size];
}

TextColour GetContrastColour(uint8 background, uint8 threshold = 128);

/**
 * All 16 colour gradients
 * 8 colours per gradient from darkest (0) to lightest (7)
 */
extern byte _colour_gradient[COLOUR_END][8];
extern byte _colour_value[COLOUR_END];

/**
 * Return the colour for a particular greyscale level.
 * @param level Intensity, 0 = black, 15 = white
 * @return colour
 */
#define GREY_SCALE(level) (level)

static const uint8 PC_BLACK              = GREY_SCALE(1);  ///< Black palette colour.
static const uint8 PC_DARK_GREY          = GREY_SCALE(6);  ///< Dark grey palette colour.
static const uint8 PC_GREY               = GREY_SCALE(10); ///< Grey palette colour.
static const uint8 PC_WHITE              = GREY_SCALE(15); ///< White palette colour.

static const uint8 PC_VERY_DARK_RED      = 0xB2;           ///< Almost-black red palette colour.
static const uint8 PC_DARK_RED           = 0xB4;           ///< Dark red palette colour.
static const uint8 PC_RED                = 0xB8;           ///< Red palette colour.

static const uint8 PC_VERY_DARK_BROWN    = 0x56;           ///< Almost-black brown palette colour.

static const uint8 PC_ORANGE             = 0xC2;           ///< Orange palette colour.

static const uint8 PC_YELLOW             = 0xBF;           ///< Yellow palette colour.
static const uint8 PC_LIGHT_YELLOW       = 0x44;           ///< Light yellow palette colour.
static const uint8 PC_VERY_LIGHT_YELLOW  = 0x45;           ///< Almost-white yellow palette colour.

static const uint8 PC_GREEN              = 0xD0;           ///< Green palette colour.

static const uint8 PC_VERY_DARK_BLUE     = 0x9A;           ///< Almost-black blue palette colour.
static const uint8 PC_DARK_BLUE          = 0x9D;           ///< Dark blue palette colour.
static const uint8 PC_LIGHT_BLUE         = 0x98;           ///< Light blue palette colour.

#endif /* GFX_FUNC_H */
