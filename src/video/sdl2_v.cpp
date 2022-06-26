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
#include <algorithm>
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

static std::string _editing_text;

static void SetTextInputRect();

bool IsWindowFocused();
Point GetFocusedWindowCaret();
Point GetFocusedWindowTopLeft();
bool FocusedWindowIsConsole();
bool EditBoxInGlobalFocus();
void InputLoop();

#if defined(WITH_FCITX)
static SDL_Window *_fcitx_sdl_window;
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

void VideoDriver_SDL_Base::MakeDirty(int left, int top, int width, int height)
{
	Rect r = {left, top, left + width, top + height};
	this->dirty_rect = BoundingRect(this->dirty_rect, r);
}

void VideoDriver_SDL_Base::CheckPaletteAnim()
{
	if (_cur_palette.count_dirty == 0) return;

	this->local_palette = _cur_palette;
	_cur_palette.count_dirty = 0;
	this->MakeDirty(0, 0, _screen.width, _screen.height);
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

void VideoDriver_SDL_Base::ClientSizeChanged(int w, int h, bool force)
{
	/* Allocate backing store of the new size. */
	if (this->AllocateBackingStore(w, h, force)) {
		/* Mark all palette colours dirty. */
		_cur_palette.first_dirty = 0;
		_cur_palette.count_dirty = 256;
		this->local_palette = _cur_palette;

		BlitterFactory::GetCurrentBlitter()->PostResize();

		GameSizeChanged();
	}
}

bool VideoDriver_SDL_Base::CreateMainWindow(uint w, uint h, uint flags)
{
	if (this->sdl_window != nullptr) return true;

	flags |= SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

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
	this->sdl_window = SDL_CreateWindow(
		caption,
		x, y,
		w, h,
		flags);
#if defined(WITH_FCITX)
	_fcitx_sdl_window = this->sdl_window;
#endif

	if (this->sdl_window == nullptr) {
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
			SDL_SetWindowIcon(this->sdl_window, icon);
			SDL_FreeSurface(icon);
		}
	}

	return true;
}

bool VideoDriver_SDL_Base::CreateMainSurface(uint w, uint h, bool resize)
{
	GetAvailableVideoMode(&w, &h);
	DEBUG(driver, 1, "SDL2: using mode %ux%u", w, h);

	if (!this->CreateMainWindow(w, h)) return false;
	if (resize) SDL_SetWindowSize(this->sdl_window, w, h);
	this->ClientSizeChanged(w, h, true);

	/* When in full screen, we will always have the mouse cursor
	 * within the window, even though SDL does not give us the
	 * appropriate event to know this. */
	if (_fullscreen) _cursor.in_window = true;

	return true;
}

bool VideoDriver_SDL_Base::ClaimMousePointer()
{
	/* Emscripten never claims the pointer, so we do not need to change the cursor visibility. */
#ifndef __EMSCRIPTEN__
	SDL_ShowCursor(0);
#endif
	return true;
}

static void SetTextInputRect()
{
	if (!IsWindowFocused()) return;

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
		if (!SDL_GetWindowWMInfo(_fcitx_sdl_window, &info)) {
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
			SDL_GetWindowPosition(_fcitx_sdl_window, &x, &y);
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
void VideoDriver_SDL_Base::EditBoxGainedFocus()
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
void VideoDriver_SDL_Base::EditBoxLostFocus()
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

std::vector<int> VideoDriver_SDL_Base::GetListOfMonitorRefreshRates()
{
	std::vector<int> rates = {};
	for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
		SDL_DisplayMode mode = {};
		if (SDL_GetDisplayMode(i, 0, &mode) != 0) continue;
		if (mode.refresh_rate != 0) rates.push_back(mode.refresh_rate);
	}
	return rates;
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

bool VideoDriver_SDL_Base::PollEvent()
{
#if defined(WITH_FCITX)
	if (_fcitx_mode) FcitxPoll();
#endif

	SDL_Event ev;
	if (!SDL_PollEvent(&ev)) return false;

	switch (ev.type) {
		case SDL_MOUSEMOTION:
			if (_cursor.UpdateCursorPosition(ev.motion.x, ev.motion.y, true)) {
				SDL_WarpMouseInWindow(this->sdl_window, _cursor.pos.x, _cursor.pos.y);
			}
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
				/* Ensure pointer lock will not occur. */
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

	return true;
}

static const char *InitializeSDL()
{
#if defined(WITH_FCITX)
	FcitxInit();
#endif

	/* Explicitly disable hardware acceleration. Enabling this causes
	 * UpdateWindowSurface() to update the window's texture instead of
	 * its surface. */
	SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
#ifndef __EMSCRIPTEN__
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");
#endif

	/* Check if the video-driver is already initialized. */
	if (SDL_WasInit(SDL_INIT_VIDEO) != 0) return nullptr;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) return SDL_GetError();
	return nullptr;
}

const char *VideoDriver_SDL_Base::Initialize()
{
	this->UpdateAutoResolution();

	const char *error = InitializeSDL();
	if (error != nullptr) return error;

	FindResolutions();
	DEBUG(driver, 2, "Resolution for display: %ux%u", _cur_resolution.width, _cur_resolution.height);

	return nullptr;
}

const char *VideoDriver_SDL_Base::Start(const StringList &param)
{
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 0) return "Only real blitters supported";

	const char *error = this->Initialize();
	if (error != nullptr) return error;

	this->startup_display = FindStartupDisplay(GetDriverParamInt(param, "display", -1));

	if (!CreateMainSurface(_cur_resolution.width, _cur_resolution.height, false)) {
		return SDL_GetError();
	}

	const char *dname = SDL_GetCurrentVideoDriver();
	DEBUG(driver, 1, "SDL2: using driver '%s'", dname);

	this->driver_info = this->GetName();
	this->driver_info += " (";
	this->driver_info += dname;
	this->driver_info += ")";

	MarkWholeScreenDirty();

	SDL_StopTextInput();
	this->edit_box_focused = false;

#if defined(WITH_FCITX)
	if (_fcitx_mode) SDL_EventState(SDL_SYSWMEVENT, 1);
#endif
#ifdef __EMSCRIPTEN__
	this->is_game_threaded = false;
#else
	this->is_game_threaded = !GetDriverParamBool(param, "no_threads") && !GetDriverParamBool(param, "no_thread");
#endif

	return nullptr;
}

void VideoDriver_SDL_Base::Stop()
{
#if defined(WITH_FCITX)
	FcitxDeinit();
#endif
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	if (SDL_WasInit(SDL_INIT_EVERYTHING) == 0) {
		SDL_Quit(); // If there's nothing left, quit SDL
	}
}

void VideoDriver_SDL_Base::InputLoop()
{
	uint32 mod = SDL_GetModState();
	const Uint8 *keys = SDL_GetKeyboardState(nullptr);

	bool old_ctrl_pressed = _ctrl_pressed;
	bool old_shift_pressed = _shift_pressed;

	_ctrl_pressed  = !!(mod & KMOD_CTRL) != _invert_ctrl;
	_shift_pressed = !!(mod & KMOD_SHIFT) != _invert_shift;

#if defined(_DEBUG)
	this->fast_forward_key_pressed = _shift_pressed;
#else
	/* Speedup when pressing tab, except when using ALT+TAB
	 * to switch to another application. */
	this->fast_forward_key_pressed = keys[SDL_SCANCODE_TAB] && (mod & KMOD_ALT) == 0;
#endif /* defined(_DEBUG) */

	/* Determine which directional keys are down. */
	_dirkeys =
		(keys[SDL_SCANCODE_LEFT]  ? 1 : 0) |
		(keys[SDL_SCANCODE_UP]    ? 2 : 0) |
		(keys[SDL_SCANCODE_RIGHT] ? 4 : 0) |
		(keys[SDL_SCANCODE_DOWN]  ? 8 : 0);

	if (old_ctrl_pressed != _ctrl_pressed) HandleCtrlChanged();
	if (old_shift_pressed != _shift_pressed) HandleShiftChanged();
}

void VideoDriver_SDL_Base::LoopOnce()
{
	if (_exit_game) {
#ifdef __EMSCRIPTEN__
		/* Emscripten is event-driven, and as such the main loop is inside
		 * the browser. So if _exit_game goes true, the main loop ends (the
		 * cancel call), but we still have to call the cleanup that is
		 * normally done at the end of the main loop for non-Emscripten.
		 * After that, Emscripten just halts, and the HTML shows a nice
		 * "bye, see you next time" message. */
		emscripten_cancel_main_loop();
		emscripten_exit_pointerlock();
		/* In effect, the game ends here. As emscripten_set_main_loop() caused
		 * the stack to be unwound, the code after MainLoop() in
		 * openttd_main() is never executed. */
		EM_ASM(if (window["openttd_syncfs"]) openttd_syncfs());
		EM_ASM(if (window["openttd_exit"]) openttd_exit());
#endif
		return;
	}

	this->Tick();

/* Emscripten is running an event-based mainloop; there is already some
 * downtime between each iteration, so no need to sleep. */
#ifndef __EMSCRIPTEN__
	this->SleepTillNextTick();
#endif
}

void VideoDriver_SDL_Base::MainLoop()
{
#ifdef __EMSCRIPTEN__
	/* Run the main loop event-driven, based on RequestAnimationFrame. */
	emscripten_set_main_loop_arg(&this->EmscriptenLoop, this, 0, 1);
#else
	this->StartGameThread();

	while (!_exit_game) {
		LoopOnce();
	}

	this->StopGameThread();
#endif
}

bool VideoDriver_SDL_Base::ChangeResolution(int w, int h)
{
	return CreateMainSurface(w, h, true);
}

bool VideoDriver_SDL_Base::ToggleFullscreen(bool fullscreen)
{
	int w, h;

	/* Remember current window size */
	if (fullscreen) {
		SDL_GetWindowSize(this->sdl_window, &w, &h);

		/* Find fullscreen window size */
		SDL_DisplayMode dm;
		if (SDL_GetCurrentDisplayMode(0, &dm) < 0) {
			DEBUG(driver, 0, "SDL_GetCurrentDisplayMode() failed: %s", SDL_GetError());
		} else {
			SDL_SetWindowSize(this->sdl_window, dm.w, dm.h);
		}
	}

	DEBUG(driver, 1, "SDL2: Setting %s", fullscreen ? "fullscreen" : "windowed");
	int ret = SDL_SetWindowFullscreen(this->sdl_window, fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
	if (ret == 0) {
		/* Switching resolution succeeded, set fullscreen value of window. */
		_fullscreen = fullscreen;
		if (!fullscreen) SDL_SetWindowSize(this->sdl_window, w, h);
	} else {
		DEBUG(driver, 0, "SDL_SetWindowFullscreen() failed: %s", SDL_GetError());
	}

	this->InvalidateGameOptionsWindow();
	return ret == 0;
}

bool VideoDriver_SDL_Base::AfterBlitterChange()
{
	assert(BlitterFactory::GetCurrentBlitter()->GetScreenDepth() != 0);
	int w, h;
	SDL_GetWindowSize(this->sdl_window, &w, &h);
	return CreateMainSurface(w, h, false);
}

Dimension VideoDriver_SDL_Base::GetScreenSize() const
{
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(this->startup_display, &mode) != 0) return VideoDriver::GetScreenSize();

	return { static_cast<uint>(mode.w), static_cast<uint>(mode.h) };
}

bool VideoDriver_SDL_Base::LockVideoBuffer()
{
	if (this->buffer_locked) return false;
	this->buffer_locked = true;

	_screen.dst_ptr = this->GetVideoPointer();
	assert(_screen.dst_ptr != nullptr);

	return true;
}

void VideoDriver_SDL_Base::UnlockVideoBuffer()
{
	if (_screen.dst_ptr != nullptr) {
		/* Hand video buffer back to the drawing backend. */
		this->ReleaseVideoPointer();
		_screen.dst_ptr = nullptr;
	}

	this->buffer_locked = false;
}
