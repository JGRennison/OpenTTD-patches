/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file window_func.h %Window functions not directly related to making/drawing windows. */

#ifndef WINDOW_FUNC_H
#define WINDOW_FUNC_H

#include "window_type.h"
#include "company_type.h"
#include "core/geometry_type.hpp"

#include <bitset>

Window *FindWindowById(WindowClass cls, WindowNumber number);
Window *FindWindowByClass(WindowClass cls);
Window *FindWindowByToken(WindowToken token);
Window *GetMainWindow();
void ChangeWindowOwner(Owner old_owner, Owner new_owner);

void ResizeWindow(Window *w, int x, int y, bool clamp_to_screen = true);
int PositionMainToolbar(Window *w);
int PositionStatusbar(Window *w);
int PositionNewsMessage(Window *w);
int PositionNetworkChatWindow(Window *w);

int GetMainViewTop();
int GetMainViewBottom();

void InitWindowSystem();
void UnInitWindowSystem();
void ResetWindowSystem();
void SetupColoursAndInitialWindow();
void InputLoop();

void InvalidateWindowData(WindowClass cls, WindowNumber number, int data = 0, bool gui_scope = false);
void InvalidateWindowClassesData(WindowClass cls, int data = 0, bool gui_scope = false);

void CloseNonVitalWindows();
void CloseAllNonVitalWindows();
void DeleteAllMessages();
void CloseConstructionWindows();
void CloseNetworkClientWindows();
void HideVitalWindows();
void ShowVitalWindows();

/**
 * Re-initialize all windows.
 * @param zoom_changed Set if windows are being re-initialized due to a zoom level changed.
 */
void ReInitAllWindows(bool zoom_changed);

void SetWindowWidgetDirty(WindowClass cls, WindowNumber number, byte widget_index);
void SetWindowDirty(WindowClass cls, WindowNumber number);
void SetWindowClassesDirty(WindowClass cls);

void CloseWindowById(WindowClass cls, WindowNumber number, bool force = true, int data = 0);
void CloseAllWindowsById(WindowClass cls, WindowNumber number, bool force = true, int data = 0);
void CloseWindowByClass(WindowClass cls, int data = 0);

bool FocusWindowById(WindowClass cls, WindowNumber number);

inline bool HaveWindowByClass(WindowClass wc)
{
	extern std::bitset<WC_END> _present_window_types;
	return wc < WC_END && _present_window_types[wc];
}

bool EditBoxInGlobalFocus();
bool FocusedWindowIsConsole();
Point GetCaretPosition();

char *DumpWindowInfo(char *b, const char *last, const Window *w);

#endif /* WINDOW_FUNC_H */
