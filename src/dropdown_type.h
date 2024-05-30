/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dropdown_type.h Types related to the drop down widget. */

#ifndef DROPDOWN_TYPE_H
#define DROPDOWN_TYPE_H

#include "window_type.h"
#include "gfx_func.h"
#include "gfx_type.h"
#include "window_gui.h"
#include <vector>

enum DropDownSyncFocus {
	DDSF_NONE                   = 0,
	DDSF_NOTIFY_RECV_FOCUS      = 1 << 0,
	DDSF_NOTIFY_LOST_FOCUS      = 1 << 1,
	DDSF_FOCUS_PARENT_ON_SELECT = 1 << 2,

	DDSF_SHARED                 = DDSF_NOTIFY_RECV_FOCUS | DDSF_FOCUS_PARENT_ON_SELECT,
};
DECLARE_ENUM_AS_BIT_SET(DropDownSyncFocus)

/**
 * Base list item class from which others are derived.
 */
class DropDownListItem {
public:
	int result; ///< Result value to return to window on selection.
	bool masked; ///< Masked and unselectable item.
	bool shaded; ///< Shaded item, affects text colour.
	TextColour colour_flags = TC_BEGIN;

	explicit DropDownListItem(int result, bool masked = false, bool shaded = false) : result(result), masked(masked), shaded(shaded) {}
	virtual ~DropDownListItem() = default;

	virtual bool Selectable() const { return true; }
	virtual uint Height() const { return 0; }
	virtual uint Width() const { return 0; }

	virtual void Draw(const Rect &full, const Rect &, bool, Colours bg_colour) const
	{
		if (this->masked) GfxFillRect(full, GetColourGradient(bg_colour, SHADE_LIGHT), FILLRECT_CHECKER);
	}

	TextColour GetColour(bool sel) const
	{
		if (this->shaded) return (sel ? TC_SILVER : TC_GREY) | TC_NO_SHADE;
		return (sel ? TC_WHITE : TC_BLACK) | this->colour_flags;
	}
};

/**
 * Drop down unselectable component.
 * @tparam TBase Base component.
 */
template<class TBase>
class DropDownUnselectable : public TBase {
public:
	template <typename... Args>
	explicit DropDownUnselectable(Args&&... args) : TBase(std::forward<Args>(args)...) {}

	bool Selectable() const override { return false; }
};

/**
 * A drop down list is a collection of drop down list items.
 */
typedef std::vector<std::unique_ptr<const DropDownListItem>> DropDownList;

enum DropDownModeFlags : uint8_t {
	DDMF_NONE              = 0,
	DDMF_INSTANT_CLOSE     = 1 << 0, ///< Close the window when the mouse button is raised.
	DDMF_PERSIST           = 1 << 1, ///< Dropdown menu will persist.
};
DECLARE_ENUM_AS_BIT_SET(DropDownModeFlags)

void ShowDropDownListAt(Window *w, DropDownList &&list, int selected, WidgetID button, Rect wi_rect, Colours wi_colour, DropDownModeFlags mode_flags = DDMF_NONE, DropDownSyncFocus sync_parent_focus = DDSF_NONE);

void ShowDropDownList(Window *w, DropDownList &&list, int selected, WidgetID button, uint width = 0, DropDownModeFlags mode_flags = DDMF_NONE, DropDownSyncFocus sync_parent_focus = DDSF_NONE);

Dimension GetDropDownListDimension(const DropDownList &list);

void ReplaceDropDownList(Window *parent, DropDownList &&list);

#endif /* DROPDOWN_TYPE_H */
