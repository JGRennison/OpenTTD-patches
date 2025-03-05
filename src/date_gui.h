/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file date_gui.h Functions related to the graphical selection of a date. */

#ifndef DATE_GUI_H
#define DATE_GUI_H

#include "date_type.h"
#include "window_type.h"

/**
 * Callback for when a tick has been chosen
 * @param w the window that sends the callback
 * @param tick the tick that has been chosen
 * @param callback_data callback data provided to the window
 */
typedef void SetTickCallback(const Window *w, StateTicks tick, void *callback_data);

void ShowSetDateWindow(Window *parent, int window_number, StateTicks initial_tick, EconTime::Year min_year, EconTime::Year max_year, SetTickCallback *callback, void *callback_data = nullptr,
		StringID button_text = STR_NULL, StringID button_tooltip = STR_NULL);

#endif /* DATE_GUI_H */
