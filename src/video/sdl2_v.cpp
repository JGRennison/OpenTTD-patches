/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file sdl2_v.cpp Implementation of the SDL2 video driver. */

#include "../stdafx.h"
#include "../openttd.h"
#include "../gfx_func.h"
#include "../rev.h"
#include "../blitter/factory.hpp"
#include "../network/network.h"
#include "../thread.h"
#include "../progress.h"
#include "../core/random_func.hpp"
#include "../core/math_func.hpp"
#include "../core/mem_func.hpp"
#include "../core/geometry_func.hpp"
#include "../fileio_func.h"
#include "../framerate_type.h"
#include "../scope.h"
#include "sdl2_v.h"
#include <SDL.h>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#if defined(__MINGW32__)
#include "../3rdparty/mingw-std-threads/mingw.mutex.h"
#include "../3rdparty/mingw-std-threads/mingw.condition_variable.h"
#endif
#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#	include <emscripten/html5.h>
#endif

#if defined(WITH_FCITX)
#include <fcitx/frontend.h>
#include <dbus/dbus.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <unistd.h>
#endif

#include "../safeguards.h"

static FVideoDriver_SDL iFVideoDriver_SDL;

static SDL_Window *_sdl_window;
static SDL_Surface *_sdl_surface;
static SDL_Surface *_sdl_rgb_surface;
static SDL_Surface *_sdl_real_surface;

/** Whether the drawing is/may be done in a separate thread. */
static bool _draw_threaded;
/** Mutex to keep the access to the shared memory controlled. */
static std::recursive_mutex *_draw_mutex = nullptr;
/** Signal to draw the next frame. */
static std::condition_variable_any *_draw_signal = nullptr;
/** Should we keep continue drawing? */
static volatile bool _draw_continue;
static Palette _local_palette;
static SDL_Palette *_sdl_palette;

#ifdef __EMSCRIPTEN__
/** Whether we just had a window-enter event. */
static bool _cursor_new_in_window = false;
#endif

static Rect _dirty_rect;

static std::string _editing_text;

static void SetTextInputRect();

Point GetFocusedWindowCaret();
Point GetFocusedWindowTopLeft();
bool FocusedWindowIsConsole();
bool EditBoxInGlobalFocus();
void InputLoop();

#if defined(WITH_FCITX)
static bool _fcitx_mode = false;
static char _fcitx_service_name[64];
static char _fcitx_ic_name[64];
static DBusConnection *_fcitx_dbus_session_conn = nullptr;
static bool _suppress_text_event = false;

static void FcitxICMethod(const char *method)
{
	DBusMessage *msg = dbus_message_new_method_call(_fcitx_service_name, _fcitx_ic_name, "org.fcitx.Fcitx.InputContext", method);
	if (!msg) return;
	dbus_connection_send(_fcitx_dbus_session_conn, msg, NULL);
	dbus_connection_flush(_fcitx_dbus_session_conn);
	dbus_message_unref(msg);
}

static int GetXDisplayNum()
{
	const char *display = getenv("DISPLAY");
	if (!display) return 0;
	const char *colon = strchr(display, ':');
	if (!colon) return 0;
	return atoi(colon + 1);
}

static void FcitxDeinit() {
	if (_fcitx_mode) {
		FcitxICMethod("DestroyIC");
		_fcitx_mode = false;
	}
	if (_fcitx_dbus_session_conn) {
		dbus_connection_close(_fcitx_dbus_session_conn);
		_fcitx_dbus_session_conn = nullptr;
	}
}

static DBusHandlerResult FcitxDBusMessageFilter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
	if (dbus_message_is_signal(message, "org.fcitx.Fcitx.InputContext", "CommitString")) {
		DBusMessageIter iter;
		const char *text = nullptr;
		dbus_message_iter_init(message, &iter);
		dbus_message_iter_get_basic(&iter, &text);

		if (text != nullptr && EditBoxInGlobalFocus()) {
			HandleTextInput(nullptr, true);
			HandleTextInput(text);
			SetTextInputRect();
		}

		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal(message, "org.fcitx.Fcitx.InputContext", "UpdatePreedit")) {
		const char *text = nullptr;
		int32 cursor;
		if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_STRING, &text, DBUS_TYPE_INT32, &cursor, DBUS_TYPE_INVALID)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		if (text != nullptr && EditBoxInGlobalFocus()) {
			HandleTextInput(text, true, text + std::min<uint>(cursor, strlen(text)));
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void FcitxInit()
{
	DBusError err;
	dbus_error_init(&err);
	_fcitx_dbus_session_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (dbus_error_is_set(&err)) {
		dbus_error_free(&err);
		return;
	}
	dbus_connection_set_exit_on_disconnect(_fcitx_dbus_session_conn, false);
	seprintf(_fcitx_service_name, lastof(_fcitx_service_name), "org.fcitx.Fcitx-%d", GetXDisplayNum());

	auto guard = scope_guard([]() {
		if (!_fcitx_mode) FcitxDeinit();
	});

	int pid = getpid();
	int id = -1;
	uint32 enable, hk1sym, hk1state, hk2sym, hk2state;
	DBusMessage *msg = dbus_message_new_method_call(_fcitx_service_name, "/inputmethod", "org.fcitx.Fcitx.InputMethod", "CreateICv3");
	if (!msg) return;
	auto guard1 = scope_guard([&]() {
		dbus_message_unref(msg);
	});
	const char *name = "OpenTTD";
	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &name, DBUS_TYPE_INT32, &pid, DBUS_TYPE_INVALID)) return;
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(_fcitx_dbus_session_conn, msg, 100, nullptr);
	if (!reply) return;
	auto guard2 = scope_guard([&]() {
		dbus_message_unref(reply);
	});
	if (!dbus_message_get_args(reply, nullptr, DBUS_TYPE_INT32, &id, DBUS_TYPE_BOOLEAN, &enable, DBUS_TYPE_UINT32, &hk1sym, DBUS_TYPE_UINT32, &hk1state, DBUS_TYPE_UINT32, &hk2sym, DBUS_TYPE_UINT32, &hk2state, DBUS_TYPE_INVALID)) return;

	if (id < 0) return;

	seprintf(_fcitx_ic_name, lastof(_fcitx_ic_name), "/inputcontext_%d", id);
	dbus_bus_add_match(_fcitx_dbus_session_conn, "type='signal', interface='org.fcitx.Fcitx.InputContext'", nullptr);
	dbus_connection_add_filter(_fcitx_dbus_session_conn, &FcitxDBusMessageFilter, nullptr, nullptr);
	dbus_connection_flush(_fcitx_dbus_session_conn);

	uint32 caps = CAPACITY_PREEDIT;
	DBusMessage *msg2 = dbus_message_new_method_call(_fcitx_service_name, _fcitx_ic_name, "org.fcitx.Fcitx.InputContext", "SetCapacity");
	if (!msg2) return;
	auto guard3 = scope_guard([&]() {
		dbus_message_unref(msg2);
	});
	if (!dbus_message_append_args(msg2, DBUS_TYPE_UINT32, &caps, DBUS_TYPE_INVALID)) return;
	dbus_connection_send(_fcitx_dbus_session_conn, msg2, NULL);
	dbus_connection_flush(_fcitx_dbus_session_conn);

	setenv("SDL_IM_MODULE", "N/A", true);
	setenv("IBUS_ADDRESS", "/dev/null/invalid", true);

	_fcitx_mode = true;
}

static uint32 _fcitx_last_keycode = 0;
static uint32 _fcitx_last_keysym = 0;
static uint16 _last_sdl_key_mod;
static bool FcitxProcessKey()
{
	uint32 fcitx_mods = 0;
	if (_last_sdl_key_mod & KMOD_SHIFT) fcitx_mods |= FcitxKeyState_Shift;
	if (_last_sdl_key_mod & KMOD_CAPS)  fcitx_mods |= FcitxKeyState_CapsLock;
	if (_last_sdl_key_mod & KMOD_CTRL)  fcitx_mods |= FcitxKeyState_Ctrl;
	if (_last_sdl_key_mod & KMOD_ALT)   fcitx_mods |= FcitxKeyState_Alt;
	if (_last_sdl_key_mod & KMOD_NUM)   fcitx_mods |= FcitxKeyState_NumLock;
	if (_last_sdl_key_mod & KMOD_LGUI)  fcitx_mods |= FcitxKeyState_Super;
	if (_last_sdl_key_mod & KMOD_RGUI)  fcitx_mods |= FcitxKeyState_Meta;

	int type = FCITX_PRESS_KEY;
	uint32 event_time = 0;

	DBusMessage *msg = dbus_message_new_method_call(_fcitx_service_name, _fcitx_ic_name, "org.fcitx.Fcitx.InputContext", "ProcessKeyEvent");
	if (!msg) return false;
	auto guard1 = scope_guard([&]() {
		dbus_message_unref(msg);
	});
	if (!dbus_message_append_args(msg, DBUS_TYPE_UINT32, &_fcitx_last_keysym, DBUS_TYPE_UINT32, &_fcitx_last_keycode, DBUS_TYPE_UINT32, &fcitx_mods,
			DBUS_TYPE_INT32, &type, DBUS_TYPE_UINT32, &event_time, DBUS_TYPE_INVALID)) return false;
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(_fcitx_dbus_session_conn, msg, 300, nullptr);
	if (!reply) return false;
	auto guard2 = scope_guard([&]() {
		dbus_message_unref(reply);
	});
	uint32 handled = 0;
	if (!dbus_message_get_args(reply, nullptr, DBUS_TYPE_INT32, &handled, DBUS_TYPE_INVALID)) return false;
	return handled;
}

static void FcitxPoll()
{
	dbus_connection_read_write(_fcitx_dbus_session_conn, 0);
	while (dbus_connection_dispatch(_fcitx_dbus_session_conn) == DBUS_DISPATCH_DATA_REMAINS) {}
}

static void FcitxFocusChange(bool focused)
{
	FcitxICMethod(focused ? "FocusIn" : "FocusOut");
}

static void FcitxSYSWMEVENT(const SDL_SysWMEvent &event)
{
	if (_fcitx_last_keycode != 0 || _fcitx_last_keysym != 0) {
		DEBUG(misc, 0, "Passing pending keypress to Fcitx");
		FcitxProcessKey();
	}
	_fcitx_last_keycode = _fcitx_last_keysym = 0;
	if (event.msg->subsystem != SDL_SYSWM_X11) return;
	XEvent &xevent = event.msg->msg.x11.event;
	if (xevent.type == KeyPress) {
		char text[8];
		KeySym keysym = 0;
		XLookupString(&xevent.xkey, text, lengthof(text), &keysym, nullptr);
		_fcitx_last_keycode = xevent.xkey.keycode;
		_fcitx_last_keysym = keysym;
	}
}
#else
const static bool _fcitx_mode = false;
const static bool _suppress_text_event = false;
#endif

void VideoDriver_SDL::MakeDirty(int left, int top, int width, int height)
{
	Rect r = {left, top, left + width, top + height};
	_dirty_rect = BoundingRect(_dirty_rect, r);
}

static void UpdatePalette()
{
	SDL_Color pal[256];

	for (int i = 0; i != _local_palette.count_dirty; i++) {
		pal[i].r = _local_palette.palette[_local_palette.first_dirty + i].r;
		pal[i].g = _local_palette.palette[_local_palette.first_dirty + i].g;
		pal[i].b = _local_palette.palette[_local_palette.first_dirty + i].b;
		pal[i].a = 0;
	}

	SDL_SetPaletteColors(_sdl_palette, pal, _local_palette.first_dirty, _local_palette.count_dirty);
	SDL_SetSurfacePalette(_sdl_surface, _sdl_palette);
}

static void MakePalette()
{
	if (_sdl_palette == nullptr) {
		_sdl_palette = SDL_AllocPalette(256);
		if (_sdl_palette == nullptr) usererror("SDL2: Couldn't allocate palette: %s", SDL_GetError());
	}

	_cur_palette.first_dirty = 0;
	_cur_palette.count_dirty = 256;
	_local_palette = _cur_palette;
	UpdatePalette();

	if (_sdl_surface != _sdl_real_surface) {
		/* When using a shadow surface, also set our palette on the real screen. This lets SDL
		 * allocate as many colors (or approximations) as
		 * possible, instead of using only the default SDL
		 * palette. This allows us to get more colors exactly
		 * right and might allow using better approximations for
		 * other colors.
		 *
		 * Note that colors allocations are tried in-order, so
		 * this favors colors further up into the palette. Also
		 * note that if two colors from the same animation
		 * sequence are approximated using the same color, that
		 * animation will stop working.
		 *
		 * Since changing the system palette causes the colours
		 * to change right away, and allocations might
		 * drastically change, we can't use this for animation,
		 * since that could cause weird coloring between the
		 * palette change and the blitting below, so we only set
		 * the real palette during initialisation.
		 */
		SDL_SetSurfacePalette(_sdl_real_surface, _sdl_palette);
	}
}

void VideoDriver_SDL::CheckPaletteAnim()
{
	if (_cur_palette.count_dirty == 0) return;

	_local_palette = _cur_palette;
	this->MakeDirty(0, 0, _screen.width, _screen.height);
}

static void Paint()
{
	PerformanceMeasurer framerate(PFE_VIDEO);

	if (IsEmptyRect(_dirty_rect) && _cur_palette.count_dirty == 0) return;

	if (_cur_palette.count_dirty != 0) {
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();

		switch (blitter->UsePaletteAnimation()) {
			case Blitter::PALETTE_ANIMATION_VIDEO_BACKEND:
				UpdatePalette();
				break;

			case Blitter::PALETTE_ANIMATION_BLITTER:
				blitter->PaletteAnimate(_local_palette);
				break;

			case Blitter::PALETTE_ANIMATION_NONE:
				break;

			default:
				NOT_REACHED();
		}
		_cur_palette.count_dirty = 0;
	}

	SDL_Rect r = { _dirty_rect.left, _dirty_rect.top, _dirty_rect.right - _dirty_rect.left, _dirty_rect.bottom - _dirty_rect.top };

	if (_sdl_surface != _sdl_real_surface) {
		SDL_BlitSurface(_sdl_surface, &r, _sdl_real_surface, &r);
	}
	SDL_UpdateWindowSurfaceRects(_sdl_window, &r, 1);

	MemSetT(&_dirty_rect, 0);
}

static void PaintThread()
{
	/* First tell the main thread we're started */
	std::unique_lock<std::recursive_mutex> lock(*_draw_mutex);
	_draw_signal->notify_one();

	/* Now wait for the first thing to draw! */
	_draw_signal->wait(*_draw_mutex);

	while (_draw_continue) {
		/* Then just draw and wait till we stop */
		Paint();
		_draw_signal->wait(lock);
	}
}

static const Dimension default_resolutions[] = {
	{  640,  480 },
	{  800,  600 },
	{ 1024,  768 },
	{ 1152,  864 },
	{ 1280,  800 },
	{ 1280,  960 },
	{ 1280, 1024 },
	{ 1400, 1050 },
	{ 1600, 1200 },
	{ 1680, 1050 },
	{ 1920, 1200 }
};

static void FindResolutions()
{
	_resolutions.clear();

	for (int i = 0; i < SDL_GetNumDisplayModes(0); i++) {
		SDL_DisplayMode mode;
		SDL_GetDisplayMode(0, i, &mode);

		if (mode.w < 640 || mode.h < 480) continue;
		if (std::find(_resolutions.begin(), _resolutions.end(), Dimension(mode.w, mode.h)) != _resolutions.end()) continue;
		_resolutions.emplace_back(mode.w, mode.h);
	}

	/* We have found no resolutions, show the default list */
	if (_resolutions.empty()) {
		_resolutions.assign(std::begin(default_resolutions), std::end(default_resolutions));
	}

	SortResolutions();
}

static void GetAvailableVideoMode(uint *w, uint *h)
{
	/* All modes available? */
	if (!_fullscreen || _resolutions.empty()) return;

	/* Is the wanted mode among the available modes? */
	if (std::find(_resolutions.begin(), _resolutions.end(), Dimension(*w, *h)) != _resolutions.end()) return;

	/* Use the closest possible resolution */
	uint best = 0;
	uint delta = Delta(_resolutions[0].width, *w) * Delta(_resolutions[0].height, *h);
	for (uint i = 1; i != _resolutions.size(); ++i) {
		uint newdelta = Delta(_resolutions[i].width, *w) * Delta(_resolutions[i].height, *h);
		if (newdelta < delta) {
			best = i;
			delta = newdelta;
		}
	}
	*w = _resolutions[best].width;
	*h = _resolutions[best].height;
}

static uint FindStartupDisplay(uint startup_display)
{
	int num_displays = SDL_GetNumVideoDisplays();

	/* If the user indicated a valid monitor, use that. */
	if (IsInsideBS(startup_display, 0, num_displays)) return startup_display;

	/* Mouse position decides which display to use. */
	int mx, my;
	SDL_GetGlobalMouseState(&mx, &my);
	for (int display = 0; display < num_displays; ++display) {
		SDL_Rect r;
		if (SDL_GetDisplayBounds(display, &r) == 0 && IsInsideBS(mx, r.x, r.w) && IsInsideBS(my, r.y, r.h)) {
			DEBUG(driver, 1, "SDL2: Mouse is at (%d, %d), use display %d (%d, %d, %d, %d)", mx, my, display, r.x, r.y, r.w, r.h);
			return display;
		}
	}

	return 0;
}

bool VideoDriver_SDL::CreateMainWindow(uint w, uint h)
{
	if (_sdl_window != nullptr) return true;

	Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

	if (_fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN;
	}

	int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
	SDL_Rect r;
	if (SDL_GetDisplayBounds(this->startup_display, &r) == 0) {
		x = r.x + std::max(0, r.w - static_cast<int>(w)) / 2;
		y = r.y + std::max(0, r.h - static_cast<int>(h)) / 4; // decent desktops have taskbars at the bottom
	}

	char caption[50];
	seprintf(caption, lastof(caption), "OpenTTD %s", _openttd_revision);
	_sdl_window = SDL_CreateWindow(
		caption,
		x, y,
		w, h,
		flags);

	if (_sdl_window == nullptr) {
		DEBUG(driver, 0, "SDL2: Couldn't allocate a window to draw on: %s", SDL_GetError());
		return false;
	}

	std::string icon_path = FioFindFullPath(BASESET_DIR, "openttd.32.bmp");
	if (!icon_path.empty()) {
		/* Give the application an icon */
		SDL_Surface *icon = SDL_LoadBMP(icon_path.c_str());
		if (icon != nullptr) {
			/* Get the colourkey, which will be magenta */
			uint32 rgbmap = SDL_MapRGB(icon->format, 255, 0, 255);

			SDL_SetColorKey(icon, SDL_TRUE, rgbmap);
			SDL_SetWindowIcon(_sdl_window, icon);
			SDL_FreeSurface(icon);
		}
	}

	return true;
}

bool VideoDriver_SDL::CreateMainSurface(uint w, uint h, bool resize)
{
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	GetAvailableVideoMode(&w, &h);
	DEBUG(driver, 1, "SDL2: using mode %ux%ux%d", w, h, bpp);

	if (!this->CreateMainWindow(w, h)) return false;
	if (resize) SDL_SetWindowSize(_sdl_window, w, h);

	_sdl_real_surface = SDL_GetWindowSurface(_sdl_window);
	if (_sdl_real_surface == nullptr) {
		DEBUG(driver, 0, "SDL2: Couldn't get window surface: %s", SDL_GetError());
		return false;
	}

	/* Free any previously allocated rgb surface. */
	if (_sdl_rgb_surface != nullptr) {
		SDL_FreeSurface(_sdl_rgb_surface);
		_sdl_rgb_surface = nullptr;
	}

	if (bpp == 8) {
		_sdl_rgb_surface = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);

		if (_sdl_rgb_surface == nullptr) {
			DEBUG(driver, 0, "SDL2: Couldn't allocate shadow surface: %s", SDL_GetError());
			return false;
		}

		_sdl_surface = _sdl_rgb_surface;
	} else {
		_sdl_surface = _sdl_real_surface;
	}

	/* X11 doesn't appreciate it if we invalidate areas outside the window
	 * if shared memory is enabled (read: it crashes). So, as we might have
	 * gotten smaller, reset our dirty rects. GameSizeChanged() a bit lower
	 * will mark the whole screen dirty again anyway, but this time with the
	 * new dimensions. */
	MemSetT(&_dirty_rect, 0);

	_screen.width = _sdl_surface->w;
	_screen.height = _sdl_surface->h;
	_screen.pitch = _sdl_surface->pitch / (bpp / 8);
	_screen.dst_ptr = _sdl_surface->pixels;

	MakePalette();

	/* When in full screen, we will always have the mouse cursor
	 * within the window, even though SDL does not give us the
	 * appropriate event to know this. */
	if (_fullscreen) _cursor.in_window = true;

	BlitterFactory::GetCurrentBlitter()->PostResize();

	GameSizeChanged();
	return true;
}

bool VideoDriver_SDL::ClaimMousePointer()
{
	SDL_ShowCursor(0);
#ifdef __EMSCRIPTEN__
	SDL_SetRelativeMouseMode(SDL_TRUE);
#endif
	return true;
}

static void SetTextInputRect()
{
	SDL_Rect winrect;
	Point caret = GetFocusedWindowCaret();
	Point win = GetFocusedWindowTopLeft();
	winrect.x = win.x + caret.x;
	winrect.y = win.y + caret.y;
	winrect.w = 1;
	winrect.h = FONT_HEIGHT_NORMAL;

#if defined(WITH_FCITX)
	if (_fcitx_mode) {
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (!SDL_GetWindowWMInfo(_sdl_window, &info)) {
			return;
		}
		int x = 0;
		int y = 0;
		if (info.subsystem == SDL_SYSWM_X11) {
			Display *x_disp = info.info.x11.display;
			Window x_win = info.info.x11.window;
			XWindowAttributes attrib;
			XGetWindowAttributes(x_disp, x_win, &attrib);
			Window unused;
			XTranslateCoordinates(x_disp, x_win, attrib.root, 0, 0, &x, &y, &unused);
		} else {
			SDL_GetWindowPosition(_sdl_window, &x, &y);
		}
		x += winrect.x;
		y += winrect.y;
		DBusMessage *msg = dbus_message_new_method_call(_fcitx_service_name, _fcitx_ic_name, "org.fcitx.Fcitx.InputContext", "SetCursorRect");
		if (!msg) return;
		auto guard = scope_guard([&]() {
			dbus_message_unref(msg);
		});
		if (!dbus_message_append_args(msg, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INT32, &winrect.w,
				DBUS_TYPE_INT32, &winrect.h, DBUS_TYPE_INVALID)) return;
		dbus_connection_send(_fcitx_dbus_session_conn, msg, NULL);
		dbus_connection_flush(_fcitx_dbus_session_conn);
		return;
	}
#endif

	SDL_SetTextInputRect(&winrect);
}

/**
 * This is called to indicate that an edit box has gained focus, text input mode should be enabled.
 */
void VideoDriver_SDL::EditBoxGainedFocus()
{
	if (!this->edit_box_focused) {
		SDL_StartTextInput();
		this->edit_box_focused = true;
	}
	SetTextInputRect();
}

/**
 * This is called to indicate that an edit box has lost focus, text input mode should be disabled.
 */
void VideoDriver_SDL::EditBoxLostFocus()
{
	if (this->edit_box_focused) {
		if (_fcitx_mode) {
#if defined(WITH_FCITX)
			FcitxICMethod("Reset");
			FcitxICMethod("CloseIC");
#endif
		}
		SDL_StopTextInput();
		this->edit_box_focused = false;
	}
	/* Clear any marked string from the current edit box. */
	HandleTextInput(nullptr, true);
}

struct SDLVkMapping {
	SDL_Keycode vk_from;
	byte vk_count;
	byte map_to;
	bool unprintable;
};

#define AS(x, z) {x, 0, z, false}
#define AM(x, y, z, w) {x, (byte)(y - x), z, false}
#define AS_UP(x, z) {x, 0, z, true}
#define AM_UP(x, y, z, w) {x, (byte)(y - x), z, true}

static const SDLVkMapping _vk_mapping[] = {
	/* Pageup stuff + up/down */
	AS_UP(SDLK_PAGEUP,   WKC_PAGEUP),
	AS_UP(SDLK_PAGEDOWN, WKC_PAGEDOWN),
	AS_UP(SDLK_UP,     WKC_UP),
	AS_UP(SDLK_DOWN,   WKC_DOWN),
	AS_UP(SDLK_LEFT,   WKC_LEFT),
	AS_UP(SDLK_RIGHT,  WKC_RIGHT),

	AS_UP(SDLK_HOME,   WKC_HOME),
	AS_UP(SDLK_END,    WKC_END),

	AS_UP(SDLK_INSERT, WKC_INSERT),
	AS_UP(SDLK_DELETE, WKC_DELETE),

	/* Map letters & digits */
	AM(SDLK_a, SDLK_z, 'A', 'Z'),
	AM(SDLK_0, SDLK_9, '0', '9'),

	AS_UP(SDLK_ESCAPE,    WKC_ESC),
	AS_UP(SDLK_PAUSE,     WKC_PAUSE),
	AS_UP(SDLK_BACKSPACE, WKC_BACKSPACE),

	AS(SDLK_SPACE,     WKC_SPACE),
	AS(SDLK_RETURN,    WKC_RETURN),
	AS(SDLK_TAB,       WKC_TAB),

	/* Function keys */
	AM_UP(SDLK_F1, SDLK_F12, WKC_F1, WKC_F12),

	/* Numeric part. */
	AM(SDLK_KP_0, SDLK_KP_9, '0', '9'),
	AS(SDLK_KP_DIVIDE,   WKC_NUM_DIV),
	AS(SDLK_KP_MULTIPLY, WKC_NUM_MUL),
	AS(SDLK_KP_MINUS,    WKC_NUM_MINUS),
	AS(SDLK_KP_PLUS,     WKC_NUM_PLUS),
	AS(SDLK_KP_ENTER,    WKC_NUM_ENTER),
	AS(SDLK_KP_PERIOD,   WKC_NUM_DECIMAL),

	/* Other non-letter keys */
	AS(SDLK_SLASH,        WKC_SLASH),
	AS(SDLK_SEMICOLON,    WKC_SEMICOLON),
	AS(SDLK_EQUALS,       WKC_EQUALS),
	AS(SDLK_LEFTBRACKET,  WKC_L_BRACKET),
	AS(SDLK_BACKSLASH,    WKC_BACKSLASH),
	AS(SDLK_RIGHTBRACKET, WKC_R_BRACKET),

	AS(SDLK_QUOTE,   WKC_SINGLEQUOTE),
	AS(SDLK_COMMA,   WKC_COMMA),
	AS(SDLK_MINUS,   WKC_MINUS),
	AS(SDLK_PERIOD,  WKC_PERIOD),
	AS(SDLK_HASH,    WKC_HASH),
};

static uint ConvertSdlKeyIntoMy(SDL_Keysym *sym, WChar *character)
{
	const SDLVkMapping *map;
	uint key = 0;
	bool unprintable = false;

	for (map = _vk_mapping; map != endof(_vk_mapping); ++map) {
		if ((uint)(sym->sym - map->vk_from) <= map->vk_count) {
			key = sym->sym - map->vk_from + map->map_to;
			unprintable = map->unprintable;
			break;
		}
	}

	/* check scancode for BACKQUOTE key, because we want the key left of "1", not anything else (on non-US keyboards) */
	if (sym->scancode == SDL_SCANCODE_GRAVE) key = WKC_BACKQUOTE;

	/* META are the command keys on mac */
	if (sym->mod & KMOD_GUI)   key |= WKC_META;
	if (sym->mod & KMOD_SHIFT) key |= WKC_SHIFT;
	if (sym->mod & KMOD_CTRL)  key |= WKC_CTRL;
	if (sym->mod & KMOD_ALT)   key |= WKC_ALT;

	/* The mod keys have no character. Prevent '?' */
	if (sym->mod & KMOD_GUI ||
		sym->mod & KMOD_CTRL ||
		sym->mod & KMOD_ALT ||
		unprintable) {
		*character = WKC_NONE;
	} else {
		*character = sym->sym;
	}

	return key;
}

/**
 * Like ConvertSdlKeyIntoMy(), but takes an SDL_Keycode as input
 * instead of an SDL_Keysym.
 */
static uint ConvertSdlKeycodeIntoMy(SDL_Keycode kc)
{
	const SDLVkMapping *map;
	uint key = 0;

	for (map = _vk_mapping; map != endof(_vk_mapping); ++map) {
		if ((uint)(kc - map->vk_from) <= map->vk_count) {
			key = kc - map->vk_from + map->map_to;
			break;
		}
	}

	/* check scancode for BACKQUOTE key, because we want the key left
	 * of "1", not anything else (on non-US keyboards) */
	SDL_Scancode sc = SDL_GetScancodeFromKey(kc);
	if (sc == SDL_SCANCODE_GRAVE) key = WKC_BACKQUOTE;

	return key;
}

int VideoDriver_SDL::PollEvent()
{
#if defined(WITH_FCITX)
	if (_fcitx_mode) FcitxPoll();
#endif

	SDL_Event ev;
	if (!SDL_PollEvent(&ev)) return -2;

	switch (ev.type) {
		case SDL_MOUSEMOTION:
#ifdef __EMSCRIPTEN__
			if (_cursor_new_in_window) {
				/* The cursor just moved into the window; this means we don't
				 * know the absolutely position yet to move relative from.
				 * Before this time, SDL didn't know it either, and this is
				 * why we postpone it till now. Update the absolute position
				 * for this once, and work relative after. */
				_cursor.pos.x = ev.motion.x;
				_cursor.pos.y = ev.motion.y;
				_cursor.dirty = true;

				_cursor_new_in_window = false;
				SDL_SetRelativeMouseMode(SDL_TRUE);
			} else {
				_cursor.UpdateCursorPositionRelative(ev.motion.xrel, ev.motion.yrel);
			}
#else
			if (_cursor.UpdateCursorPosition(ev.motion.x, ev.motion.y, true)) {
				SDL_WarpMouseInWindow(_sdl_window, _cursor.pos.x, _cursor.pos.y);
			}
#endif
			HandleMouseEvents();
			break;

		case SDL_MOUSEWHEEL:
			if (ev.wheel.y > 0) {
				_cursor.wheel--;
			} else if (ev.wheel.y < 0) {
				_cursor.wheel++;
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
			if (_rightclick_emulate && SDL_GetModState() & KMOD_CTRL) {
				ev.button.button = SDL_BUTTON_RIGHT;
			}

			switch (ev.button.button) {
				case SDL_BUTTON_LEFT:
					_left_button_down = true;
					break;

				case SDL_BUTTON_RIGHT:
					_right_button_down = true;
					_right_button_clicked = true;
					break;

				default: break;
			}
			HandleMouseEvents();
			break;

		case SDL_MOUSEBUTTONUP:
			if (_rightclick_emulate) {
				_right_button_down = false;
				_left_button_down = false;
				_left_button_clicked = false;
			} else if (ev.button.button == SDL_BUTTON_LEFT) {
				_left_button_down = false;
				_left_button_clicked = false;
			} else if (ev.button.button == SDL_BUTTON_RIGHT) {
				_right_button_down = false;
			}
			HandleMouseEvents();
			break;

		case SDL_QUIT:
			HandleExitGameRequest();
			break;

		case SDL_KEYDOWN: // Toggle full-screen on ALT + ENTER/F
#if defined(WITH_FCITX)
			_suppress_text_event = false;
			_last_sdl_key_mod = ev.key.keysym.mod;
			if (_fcitx_mode && EditBoxInGlobalFocus() && !(FocusedWindowIsConsole() &&
					ev.key.keysym.scancode == SDL_SCANCODE_GRAVE) && (_fcitx_last_keycode != 0 || _fcitx_last_keysym != 0)) {
				if (FcitxProcessKey()) {
					/* key press handled by Fcitx */
					_suppress_text_event = true;
					_fcitx_last_keycode = _fcitx_last_keysym = 0;
					break;
				}
			}
			_fcitx_last_keycode = _fcitx_last_keysym = 0;
#endif
			if ((ev.key.keysym.mod & (KMOD_ALT | KMOD_GUI)) &&
					(ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_f)) {
				if (ev.key.repeat == 0) ToggleFullScreen(!_fullscreen);
			} else {
				WChar character;

				uint keycode = ConvertSdlKeyIntoMy(&ev.key.keysym, &character);
				// Only handle non-text keys here. Text is handled in
				// SDL_TEXTINPUT below.
				if (!this->edit_box_focused ||
					keycode == WKC_DELETE ||
					keycode == WKC_NUM_ENTER ||
					keycode == WKC_LEFT ||
					keycode == WKC_RIGHT ||
					keycode == WKC_UP ||
					keycode == WKC_DOWN ||
					keycode == WKC_HOME ||
					keycode == WKC_END ||
					keycode & WKC_META ||
					keycode & WKC_CTRL ||
					keycode & WKC_ALT ||
					(keycode >= WKC_F1 && keycode <= WKC_F12) ||
					!IsValidChar(character, CS_ALPHANUMERAL) ||
					!this->edit_box_focused) {
					HandleKeypress(keycode, character);
				}
			}
			break;

		case SDL_TEXTINPUT: {
			if (_suppress_text_event) break;
			if (!this->edit_box_focused) break;
			SDL_Keycode kc = SDL_GetKeyFromName(ev.text.text);
			uint keycode = ConvertSdlKeycodeIntoMy(kc);

			if (keycode == WKC_BACKQUOTE && FocusedWindowIsConsole()) {
				WChar character;
				Utf8Decode(&character, ev.text.text);
				HandleKeypress(keycode, character);
			} else {
				HandleTextInput(nullptr, true);
				HandleTextInput(ev.text.text);
				SetTextInputRect();
			}
			break;
		}

		case SDL_TEXTEDITING: {
			if (!EditBoxInGlobalFocus()) break;
			if (ev.edit.start == 0) {
				_editing_text = ev.edit.text;
			} else {
				_editing_text += ev.edit.text;
			}
			HandleTextInput(_editing_text.c_str(), true, _editing_text.c_str() + _editing_text.size());
			break;
		}

		case SDL_WINDOWEVENT: {
			if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) {
				// Force a redraw of the entire screen.
				this->MakeDirty(0, 0, _screen.width, _screen.height);
			} else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				int w = std::max(ev.window.data1, 64);
				int h = std::max(ev.window.data2, 64);
				CreateMainSurface(w, h, w != ev.window.data1 || h != ev.window.data2);
			} else if (ev.window.event == SDL_WINDOWEVENT_ENTER) {
				// mouse entered the window, enable cursor
				_cursor.in_window = true;
#ifdef __EMSCRIPTEN__
				/* Disable relative mouse mode for the first mouse motion,
				 * so we can pick up the absolutely position again. */
				_cursor_new_in_window = true;
				SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
			} else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
				// mouse left the window, undraw cursor
				UndrawMouseCursor();
				_cursor.in_window = false;
			} else if (ev.window.event == SDL_WINDOWEVENT_MOVED) {
				if (_fcitx_mode) SetTextInputRect();
			} else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
#if defined(WITH_FCITX)
				if (_fcitx_mode) FcitxFocusChange(true);
#endif
			} else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
#if defined(WITH_FCITX)
				if (_fcitx_mode) FcitxFocusChange(false);
#endif
			}
			break;
		}

		case SDL_SYSWMEVENT: {
#if defined(WITH_FCITX)
				if (_fcitx_mode) FcitxSYSWMEVENT(ev.syswm);
#endif
		}
	}
	return -1;
}

const char *VideoDriver_SDL::Start(const StringList &parm)
{
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 0) return "Only real blitters supported";

#if defined(WITH_FCITX)
	FcitxInit();
#endif

	/* Explicitly disable hardware acceleration. Enabling this causes
	 * UpdateWindowSurface() to update the window's texture instead of
	 * its surface. */
	SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");

	/* Just on the offchance the audio subsystem started before the video system,
	 * check whether any part of SDL has been initialised before getting here.
	 * Slightly duplicated with sound/sdl_s.cpp */
	int ret_code = 0;
	if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		ret_code = SDL_InitSubSystem(SDL_INIT_VIDEO);
	}
	if (ret_code < 0) return SDL_GetError();

	this->UpdateAutoResolution();

	FindResolutions();

	this->startup_display = FindStartupDisplay(GetDriverParamInt(parm, "display", -1));

	if (!CreateMainSurface(_cur_resolution.width, _cur_resolution.height, false)) {
		return SDL_GetError();
	}

	const char *dname = SDL_GetCurrentVideoDriver();
	DEBUG(driver, 1, "SDL2: using driver '%s'", dname);

	MarkWholeScreenDirty();

	_draw_threaded = !GetDriverParamBool(parm, "no_threads") && !GetDriverParamBool(parm, "no_thread");
	/* Wayland SDL video driver uses EGL to render the game. SDL created the
	 * EGL context from the main-thread, and with EGL you are not allowed to
	 * draw in another thread than the context was created. The function of
	 * _draw_threaded is to do exactly this: draw in another thread than the
	 * window was created, and as such, this fails on Wayland SDL video
	 * driver. So, we disable threading by default if Wayland SDL video
	 * driver is detected.
	 */
	if (strcmp(dname, "wayland") == 0) {
		_draw_threaded = false;
	}

	SDL_StopTextInput();
	this->edit_box_focused = false;

#if defined(WITH_FCITX)
	if (_fcitx_mode) SDL_EventState(SDL_SYSWMEVENT, 1);
#endif

	return nullptr;
}

void VideoDriver_SDL::Stop()
{
#if defined(WITH_FCITX)
	FcitxDeinit();
#endif
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	if (SDL_WasInit(SDL_INIT_EVERYTHING) == 0) {
		SDL_Quit(); // If there's nothing left, quit SDL
	}
}

void VideoDriver_SDL::LoopOnce()
{
	uint32 mod;
	int numkeys;
	const Uint8 *keys;
	InteractiveRandom(); // randomness

	while (PollEvent() == -1) {}
	if (_exit_game) {
#ifdef __EMSCRIPTEN__
		/* Emscripten is event-driven, and as such the main loop is inside
		 * the browser. So if _exit_game goes true, the main loop ends (the
		 * cancel call), but we still have to call the cleanup that is
		 * normally done at the end of the main loop for non-Emscripten.
		 * After that, Emscripten just halts, and the HTML shows a nice
		 * "bye, see you next time" message. */
		emscripten_cancel_main_loop();
		MainLoopCleanup();
#endif
		return;
	}

	mod = SDL_GetModState();
	keys = SDL_GetKeyboardState(&numkeys);

#if defined(_DEBUG)
	if (_shift_pressed)
#else
	/* Speedup when pressing tab, except when using ALT+TAB
	 * to switch to another application */
	if (keys[SDL_SCANCODE_TAB] && (mod & KMOD_ALT) == 0)
#endif /* defined(_DEBUG) */
	{
		if (!_networking && _game_mode != GM_MENU) _fast_forward |= 2;
	} else if (_fast_forward & 2) {
		_fast_forward = 0;
	}

	cur_ticks = std::chrono::steady_clock::now();

	/* If more than a millisecond has passed, increase the _realtime_tick. */
	if (cur_ticks - last_realtime_tick > std::chrono::milliseconds(1)) {
		auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(cur_ticks - last_realtime_tick);
		IncreaseRealtimeTick(delta.count());
		last_realtime_tick += delta;
	}

	if (cur_ticks >= next_game_tick || (_fast_forward && !_pause_mode)) {
		if (_fast_forward && !_pause_mode) {
			next_game_tick = cur_ticks + this->GetGameInterval();
		} else {
			next_game_tick += this->GetGameInterval();
			/* Avoid next_game_tick getting behind more and more if it cannot keep up. */
			if (next_game_tick < cur_ticks - ALLOWED_DRIFT * this->GetGameInterval()) next_game_tick = cur_ticks;
		}

		/* The gameloop is the part that can run asynchronously. The rest
		 * except sleeping can't. */
		if (_draw_mutex != nullptr) draw_lock.unlock();
		GameLoop();
		if (_draw_mutex != nullptr) draw_lock.lock();
		GameLoopPaletteAnimations();
	}

	/* Prevent drawing when switching mode, as windows can be removed when they should still appear. */
	if (cur_ticks >= next_draw_tick && (_switch_mode == SM_NONE || HasModalProgress())) {
		next_draw_tick += this->GetDrawInterval();
		/* Avoid next_draw_tick getting behind more and more if it cannot keep up. */
		if (next_draw_tick < cur_ticks - ALLOWED_DRIFT * this->GetDrawInterval()) next_draw_tick = cur_ticks;

		bool old_ctrl_pressed = _ctrl_pressed;
		bool old_shift_pressed = _shift_pressed;

		_ctrl_pressed  = !!(mod & KMOD_CTRL) != _invert_ctrl;
		_shift_pressed = !!(mod & KMOD_SHIFT) != _invert_shift;

		/* determine which directional keys are down */
		_dirkeys =
			(keys[SDL_SCANCODE_LEFT]  ? 1 : 0) |
			(keys[SDL_SCANCODE_UP]    ? 2 : 0) |
			(keys[SDL_SCANCODE_RIGHT] ? 4 : 0) |
			(keys[SDL_SCANCODE_DOWN]  ? 8 : 0);
		if (old_ctrl_pressed != _ctrl_pressed) HandleCtrlChanged();
		if (old_shift_pressed != _shift_pressed) HandleShiftChanged();

		InputLoop();
		UpdateWindows();
		this->CheckPaletteAnim();

		if (_draw_mutex != nullptr && !HasModalProgress()) {
			_draw_signal->notify_one();
		} else {
			Paint();
		}
	}

/* Emscripten is running an event-based mainloop; there is already some
 * downtime between each iteration, so no need to sleep. */
#ifndef __EMSCRIPTEN__
	/* If we are not in fast-forward, create some time between calls to ease up CPU usage. */
	if (!_fast_forward || _pause_mode) {
		/* See how much time there is till we have to process the next event, and try to hit that as close as possible. */
		auto next_tick = std::min(next_draw_tick, next_game_tick);
		auto now = std::chrono::steady_clock::now();

		if (next_tick > now) {
			if (_draw_mutex != nullptr) draw_lock.unlock();
			std::this_thread::sleep_for(next_tick - now);
			if (_draw_mutex != nullptr) draw_lock.lock();
		}
	}
#endif
}

void VideoDriver_SDL::MainLoop()
{
	cur_ticks = std::chrono::steady_clock::now();
	last_realtime_tick = cur_ticks;
	next_game_tick = cur_ticks;

	this->CheckPaletteAnim();

	if (_draw_threaded) {
		/* Initialise the mutex first, because that's the thing we *need*
		 * directly in the newly created thread. */
		_draw_mutex = new std::recursive_mutex();
		if (_draw_mutex == nullptr) {
			_draw_threaded = false;
		} else {
			draw_lock = std::unique_lock<std::recursive_mutex>(*_draw_mutex);
			_draw_signal = new std::condition_variable_any();
			_draw_continue = true;

			_draw_threaded = StartNewThread(&draw_thread, "ottd:draw-sdl", &PaintThread);

			/* Free the mutex if we won't be able to use it. */
			if (!_draw_threaded) {
				draw_lock.unlock();
				draw_lock.release();
				delete _draw_mutex;
				delete _draw_signal;
				_draw_mutex = nullptr;
				_draw_signal = nullptr;
			} else {
				/* Wait till the draw mutex has started itself. */
				_draw_signal->wait(*_draw_mutex);
			}
		}
	}

	DEBUG(driver, 1, "SDL2: using %sthreads", _draw_threaded ? "" : "no ");

#ifdef __EMSCRIPTEN__
	/* Run the main loop event-driven, based on RequestAnimationFrame. */
	emscripten_set_main_loop_arg(&this->EmscriptenLoop, this, 0, 1);
#else
	while (!_exit_game) {
		LoopOnce();
	}

	MainLoopCleanup();
#endif
}

void VideoDriver_SDL::MainLoopCleanup()
{
	if (_draw_mutex != nullptr) {
		_draw_continue = false;
		/* Sending signal if there is no thread blocked
		 * is very valid and results in noop */
		_draw_signal->notify_one();
		if (draw_lock.owns_lock()) draw_lock.unlock();
		draw_lock.release();
		draw_thread.join();

		delete _draw_mutex;
		delete _draw_signal;

		_draw_mutex = nullptr;
		_draw_signal = nullptr;
	}

#ifdef __EMSCRIPTEN__
	emscripten_exit_pointerlock();
	/* In effect, the game ends here. As emscripten_set_main_loop() caused
	 * the stack to be unwound, the code after MainLoop() in
	 * openttd_main() is never executed. */
	EM_ASM(if (window["openttd_syncfs"]) openttd_syncfs());
	EM_ASM(if (window["openttd_exit"]) openttd_exit());
#endif
}

bool VideoDriver_SDL::ChangeResolution(int w, int h)
{
	std::unique_lock<std::recursive_mutex> lock;
	if (_draw_mutex != nullptr) lock = std::unique_lock<std::recursive_mutex>(*_draw_mutex);

	return CreateMainSurface(w, h, true);
}

bool VideoDriver_SDL::ToggleFullscreen(bool fullscreen)
{
	std::unique_lock<std::recursive_mutex> lock;
	if (_draw_mutex != nullptr) lock = std::unique_lock<std::recursive_mutex>(*_draw_mutex);

	int w, h;

	/* Remember current window size */
	if (fullscreen) {
		SDL_GetWindowSize(_sdl_window, &w, &h);

		/* Find fullscreen window size */
		SDL_DisplayMode dm;
		if (SDL_GetCurrentDisplayMode(0, &dm) < 0) {
			DEBUG(driver, 0, "SDL_GetCurrentDisplayMode() failed: %s", SDL_GetError());
		} else {
			SDL_SetWindowSize(_sdl_window, dm.w, dm.h);
		}
	}

	DEBUG(driver, 1, "SDL2: Setting %s", fullscreen ? "fullscreen" : "windowed");
	int ret = SDL_SetWindowFullscreen(_sdl_window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
	if (ret == 0) {
		/* Switching resolution succeeded, set fullscreen value of window. */
		_fullscreen = fullscreen;
		if (!fullscreen) SDL_SetWindowSize(_sdl_window, w, h);
	} else {
		DEBUG(driver, 0, "SDL_SetWindowFullscreen() failed: %s", SDL_GetError());
	}

	return ret == 0;
}

bool VideoDriver_SDL::AfterBlitterChange()
{
	assert(BlitterFactory::GetCurrentBlitter()->GetScreenDepth() != 0);
	int w, h;
	SDL_GetWindowSize(_sdl_window, &w, &h);
	return CreateMainSurface(w, h, false);
}

void VideoDriver_SDL::AcquireBlitterLock()
{
	if (_draw_mutex != nullptr) _draw_mutex->lock();
}

void VideoDriver_SDL::ReleaseBlitterLock()
{
	if (_draw_mutex != nullptr) _draw_mutex->unlock();
}

Dimension VideoDriver_SDL::GetScreenSize() const
{
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(this->startup_display, &mode) != 0) return VideoDriver::GetScreenSize();

	return { static_cast<uint>(mode.w), static_cast<uint>(mode.h) };
}
