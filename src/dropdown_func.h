/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dropdown_func.h Functions related to the drop down widget. */

#ifndef DROPDOWN_FUNC_H
#define DROPDOWN_FUNC_H

#include "window_gui.h"
#include "dropdown_type.h"

/* Show drop down menu containing a fixed list of strings */
void ShowDropDownMenu(Window *w, std::span<const StringID> strings, int selected, WidgetID button, uint32_t disabled_mask, uint32_t hidden_mask, uint width = 0, DropDownSyncFocus sync_parent_focus = DDSF_NONE);

/* Hide drop down menu of a parent window */
int HideDropDownMenu(Window *pw);

void GetParentWindowInfo(Window *w, WindowClass &parent_wc, WindowNumber &parent_wn);

/* Helper functions for commonly used drop down list items. */
std::unique_ptr<DropDownListItem> MakeDropDownListDividerItem();
std::unique_ptr<DropDownListItem> MakeDropDownListStringItem(StringID str, int value, bool masked = false, bool shaded = false);
std::unique_ptr<DropDownListItem> MakeDropDownListStringItem(const std::string &str, int value, bool masked = false, bool shaded = false);
std::unique_ptr<DropDownListItem> MakeDropDownListIconItem(SpriteID sprite, PaletteID palette, StringID str, int value, bool masked = false, bool shaded = false);
std::unique_ptr<DropDownListItem> MakeDropDownListIconItem(const Dimension &dim, SpriteID sprite, PaletteID palette, StringID str, int value, bool masked = false, bool shaded = false);
std::unique_ptr<DropDownListItem> MakeDropDownListCheckedItem(bool checked, StringID str, int value, bool masked = false, bool shaded = false);

#endif /* DROPDOWN_FUNC_H */
