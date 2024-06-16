/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dropdown.cpp Implementation of the dropdown widget. */

#include "stdafx.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "dropdown_common_type.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_gui.h"
#include "window_func.h"
#include "guitimer_func.h"
#include "zoom_func.h"

#include "widgets/dropdown_widget.h"

#include "safeguards.h"

std::unique_ptr<DropDownListItem> MakeDropDownListDividerItem()
{
	return std::make_unique<DropDownListDividerItem>(-1);
}

std::unique_ptr<DropDownListItem> MakeDropDownListStringItem(StringID str, int value, bool masked, bool shaded)
{
	return std::make_unique<DropDownListStringItem>(str, value, masked, shaded);
}

std::unique_ptr<DropDownListItem> MakeDropDownListStringItem(const std::string &str, int value, bool masked, bool shaded)
{
	return std::make_unique<DropDownListStringItem>(str, value, masked, shaded);
}

std::unique_ptr<DropDownListItem> MakeDropDownListIconItem(SpriteID sprite, PaletteID palette, StringID str, int value, bool masked, bool shaded)
{
	return std::make_unique<DropDownListIconItem>(sprite, palette, str, value, masked, shaded);
}

std::unique_ptr<DropDownListItem> MakeDropDownListIconItem(const Dimension &dim, SpriteID sprite, PaletteID palette, StringID str, int value, bool masked, bool shaded)
{
	return std::make_unique<DropDownListIconItem>(dim, sprite, palette, str, value, masked, shaded);
}

std::unique_ptr<DropDownListItem> MakeDropDownListCheckedItem(bool checked, StringID str, int value, bool masked, bool shaded)
{
	return std::make_unique<DropDownListCheckedItem>(checked, str, value, masked, shaded);
}

static constexpr NWidgetPart _nested_dropdown_menu_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_END, WID_DM_ITEMS), SetScrollbar(WID_DM_SCROLL), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_DM_SHOW_SCROLL),
			NWidget(NWID_VSCROLLBAR, COLOUR_END, WID_DM_SCROLL),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _dropdown_desc(__FILE__, __LINE__,
	WDP_MANUAL, nullptr, 0, 0,
	WC_DROPDOWN_MENU, WC_NONE,
	WDF_NO_FOCUS,
	std::begin(_nested_dropdown_menu_widgets), std::end(_nested_dropdown_menu_widgets)
);

/** Drop-down menu window */
struct DropdownWindow : Window {
	WindowToken parent_wnd_token; ///< Parent window token.
	WidgetID parent_button;       ///< Parent widget number where the window is dropped from.
	Rect wi_rect;                 ///< Rect of the button that opened the dropdown.
	DropDownList list;            ///< List with dropdown menu items.
	int selected_result;          ///< Result value of the selected item in the list.
	uint8_t click_delay = 0;      ///< Timer to delay selection.
	bool drag_mode = true;
	DropDownModeFlags mode_flags; ///< Mode flags.
	int scrolling = 0;            ///< If non-zero, auto-scroll the item list (one time).
	GUITimer scrolling_timer;     ///< Timer for auto-scroll of the item list.
	Point position;               ///< Position of the topleft corner of the window.
	Scrollbar *vscroll;
	DropDownSyncFocus sync_parent_focus; ///< Call parent window's OnFocus[Lost]().

	Dimension items_dim; ///< Calculated cropped and padded dimension for the items widget.

	/**
	 * Create a dropdown menu.
	 * @param parent        Parent window.
	 * @param list          Dropdown item list.
	 * @param selected      Initial selected result of the list.
	 * @param button        Widget of the parent window doing the dropdown.
	 * @param wi_rect       Rect of the button that opened the dropdown.
	 * @param instant_close Close the window when the mouse button is raised.
	 * @param wi_colour     Colour of the parent widget.
	 * @param persist
	 */
	DropdownWindow(Window *parent, DropDownList &&list, int selected, WidgetID button, const Rect wi_rect, DropDownModeFlags mode_flags, Colours wi_colour, DropDownSyncFocus sync_parent_focus)
			: Window(&_dropdown_desc)
			, parent_button(button)
			, wi_rect(wi_rect)
			, list(std::move(list))
			, selected_result(selected)
			, mode_flags(mode_flags)
			, sync_parent_focus(sync_parent_focus)
	{
		assert(!this->list.empty());

		this->parent_wnd_token = parent->GetWindowToken();

		this->CreateNestedTree();

		this->GetWidget<NWidgetCore>(WID_DM_ITEMS)->colour = wi_colour;
		this->GetWidget<NWidgetCore>(WID_DM_SCROLL)->colour = wi_colour;
		this->vscroll = this->GetScrollbar(WID_DM_SCROLL);
		this->UpdateSizeAndPosition(parent);

		this->FinishInitNested(0);
		CLRBITS(this->flags, WF_WHITE_BORDER);

		this->scrolling_timer  = GUITimer(MILLISECONDS_PER_TICK);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		this->Window::Close();

		Window *w2 = FindWindowByToken(this->parent_wnd_token);
		if (w2 != nullptr) {
			Point pt = _cursor.pos;
			pt.x -= w2->left;
			pt.y -= w2->top;
			w2->OnDropdownClose(pt, this->parent_button, this->selected_result, (this->mode_flags & DDMF_INSTANT_CLOSE) != 0);
		}
	}

	/**
	 * Fit dropdown list into available height, rounding to average item size. Width is adjusted if scrollbar is present.
	 * @param[in,out] desired Desired dimensions of dropdown list.
	 * @param list Dimensions of the list itself, without padding or cropping.
	 * @param available_height Available height to fit list within.
	 */
	void FitAvailableHeight(Dimension &desired, const Dimension &list, uint available_height)
	{
		if (desired.height < available_height) return;

		/* If the dropdown doesn't fully fit, we a need a dropdown. */
		uint avg_height = list.height / (uint)this->list.size();
		uint rows = std::max((available_height - WidgetDimensions::scaled.dropdownlist.Vertical()) / avg_height, 1U);

		desired.width = std::max(list.width, desired.width - NWidgetScrollbar::GetVerticalDimension().width);
		desired.height = rows * avg_height + WidgetDimensions::scaled.dropdownlist.Vertical();
	}

	/**
	 * Update size and position of window to fit dropdown list into available space.
	 */
	void UpdateSizeAndPosition(Window *parent)
	{
		Rect button_rect = this->wi_rect.Translate(parent->left, parent->top);

		/* Get the dimensions required for the list. */
		Dimension list_dim = GetDropDownListDimension(this->list);

		/* Set up dimensions for the items widget. */
		Dimension widget_dim = list_dim;
		widget_dim.width += WidgetDimensions::scaled.dropdownlist.Horizontal();
		widget_dim.height += WidgetDimensions::scaled.dropdownlist.Vertical();

		/* Width should match at least the width of the parent widget. */
		widget_dim.width = std::max<uint>(widget_dim.width, button_rect.Width());

		/* Available height below (or above, if the dropdown is placed above the widget). */
		uint available_height_below = std::max(GetMainViewBottom() - button_rect.bottom - 1, 0);
		uint available_height_above = std::max(button_rect.top - 1 - GetMainViewTop(), 0);

		/* Is it better to place the dropdown above the widget? */
		if (widget_dim.height > available_height_below && available_height_above > available_height_below) {
			FitAvailableHeight(widget_dim, list_dim, available_height_above);
			this->position.y = button_rect.top - widget_dim.height;
		} else {
			FitAvailableHeight(widget_dim, list_dim, available_height_below);
			this->position.y = button_rect.bottom + 1;
		}

		if (_current_text_dir == TD_RTL) {
			/* In case the list is wider than the parent button, the list should be right aligned to the button and overflow to the left. */
			this->position.x = button_rect.right + 1 - (int)(widget_dim.width + (list_dim.height > widget_dim.height ? NWidgetScrollbar::GetVerticalDimension().width : 0));
		} else {
			this->position.x = button_rect.left;
		}

		this->items_dim = widget_dim;
		this->GetWidget<NWidgetStacked>(WID_DM_SHOW_SCROLL)->SetDisplayedPlane(list_dim.height > widget_dim.height ? 0 : SZSP_NONE);

		/* Capacity is the average number of items visible */
		this->vscroll->SetCapacity((widget_dim.height - WidgetDimensions::scaled.dropdownlist.Vertical()) * this->list.size() / list_dim.height);
		this->vscroll->SetCount(this->list.size());

		/* If the dropdown is positioned above the parent widget, start selection at the bottom. */
		if (this->position.y < button_rect.top && list_dim.height > widget_dim.height) this->vscroll->UpdatePosition(INT_MAX);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_DM_ITEMS) size = this->items_dim;
	}

	Point OnInitialPosition([[maybe_unused]] int16_t sm_width, [[maybe_unused]] int16_t sm_height, [[maybe_unused]] int window_number) override
	{
		return this->position;
	}

	/**
	 * Find the dropdown item under the cursor.
	 * @param[out] value Selected item, if function returns \c true.
	 * @return Cursor points to a dropdown item.
	 */
	bool GetDropDownItem(int &value)
	{
		if (GetWidgetFromPos(this, _cursor.pos.x - this->left, _cursor.pos.y - this->top) < 0) return false;

		const Rect &r = this->GetWidget<NWidgetBase>(WID_DM_ITEMS)->GetCurrentRect().Shrink(WidgetDimensions::scaled.dropdownlist);
		int y     = _cursor.pos.y - this->top - r.top;
		int pos   = this->vscroll->GetPosition();

		for (const auto &item : this->list) {
			/* Skip items that are scrolled up */
			if (--pos >= 0) continue;

			int item_height = item->Height();

			if (y < item_height) {
				if (item->masked || !item->Selectable()) return false;
				value = item->result;
				return true;
			}

			y -= item_height;
		}

		return false;
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_DM_ITEMS) return;

		Colours colour = this->GetWidget<NWidgetCore>(widget)->colour;

		Rect ir = r.Shrink(WidgetDimensions::scaled.dropdownlist);
		int y = ir.top;
		int pos = this->vscroll->GetPosition();
		for (const auto &item : this->list) {
			int item_height = item->Height();

			/* Skip items that are scrolled up */
			if (--pos >= 0) continue;

			if (y + item_height - 1 <= ir.bottom) {
				Rect full{ir.left, y, ir.right, y + item_height - 1};

				bool selected = (this->selected_result == item->result) && item->Selectable();
				if (selected) GfxFillRect(full, PC_BLACK);

				item->Draw(full, full.Shrink(WidgetDimensions::scaled.dropdowntext, RectPadding::zero), selected, colour);
			}
			y += item_height;
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget != WID_DM_ITEMS) return;
		int item;
		if (this->GetDropDownItem(item)) {
			this->click_delay = 4;
			this->selected_result = item;
			this->SetDirty();
		}
	}

	virtual void OnRealtimeTick(uint delta_ms) override
	{
		if (!this->scrolling_timer.Elapsed(delta_ms)) return;
		this->scrolling_timer.SetInterval(MILLISECONDS_PER_TICK);

		if (this->scrolling != 0) {
			if (this->vscroll->UpdatePosition(this->scrolling)) this->SetDirty();

			this->scrolling = 0;
		}
	}

	virtual void OnMouseLoop() override
	{
		Window *w2 = FindWindowByToken(this->parent_wnd_token);
		if (w2 == nullptr) {
			this->Close();
			return;
		}

		if (this->click_delay != 0 && --this->click_delay == 0) {
			if ((this->mode_flags & DDMF_PERSIST) == 0) {
				if (this->sync_parent_focus & DDSF_FOCUS_PARENT_ON_SELECT) {
					SetFocusedWindow(w2);
				}

				/* Close the dropdown, so it doesn't affect new window placement. */
				this->Close();
			}

			w2->OnDropdownSelect(this->parent_button, this->selected_result);
			return;
		}

		if (this->drag_mode) {
			int item;

			if (!_left_button_clicked) {
				this->drag_mode = false;
				if (!this->GetDropDownItem(item)) {
					if ((this->mode_flags & DDMF_INSTANT_CLOSE) != 0) this->Close();
					return;
				}
				this->click_delay = 2;
			} else {
				if (_cursor.pos.y <= this->top + 2) {
					/* Cursor is above the list, set scroll up */
					this->scrolling = -1;
					return;
				} else if (_cursor.pos.y >= this->top + this->height - 2) {
					/* Cursor is below list, set scroll down */
					this->scrolling = 1;
					return;
				}

				if (!this->GetDropDownItem(item)) return;
			}

			if (this->selected_result != item) {
				this->selected_result = item;
				this->SetDirty();
			}
		}
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (this->sync_parent_focus & DDSF_NOTIFY_RECV_FOCUS) {
			Window *parent = FindWindowByToken(this->parent_wnd_token);
			if (parent != nullptr) parent->OnFocus(previously_focused_window);
		}
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (this->sync_parent_focus & DDSF_NOTIFY_LOST_FOCUS) {
			Window *parent = FindWindowByToken(this->parent_wnd_token);
			if (parent != nullptr) parent->OnFocusLost(false, newly_focused_window);
		}
	}

	void ReplaceList(DropDownList &&list)
	{
		Window *parent = FindWindowByToken(this->parent_wnd_token);
		if (parent == nullptr) return;

		this->list = std::move(list);
		this->UpdateSizeAndPosition(parent);
		this->ReInit(0, 0);
		this->InitializePositionSize(this->position.x, this->position.y, this->nested_root->smallest_x, this->nested_root->smallest_y);
		this->SetDirty();
	}
};

static DropdownWindow *GetDropDownWindowForParent(Window *parent)
{
	for (Window *w : Window::IterateFromFront()) {
		if (w->window_class != WC_DROPDOWN_MENU) continue;

		DropdownWindow *dw = dynamic_cast<DropdownWindow*>(w);
		assert(dw != nullptr);
		if (parent->GetWindowToken() == dw->parent_wnd_token) {
			return dw;
		}
	}

	return nullptr;
}

void ReplaceDropDownList(Window *parent, DropDownList &&list)
{
	DropdownWindow *ddw = GetDropDownWindowForParent(parent);
	if (ddw != nullptr) ddw->ReplaceList(std::move(list));
}

/**
 * Determine width and height required to fully display a DropDownList
 * @param list The list.
 * @return Dimension required to display the list.
 */
Dimension GetDropDownListDimension(const DropDownList &list)
{
	Dimension dim{};
	for (const auto &item : list) {
		dim.height += item->Height();
		dim.width = std::max(dim.width, item->Width());
	}
	dim.width += WidgetDimensions::scaled.dropdowntext.Horizontal();
	return dim;
}

/**
 * Show a drop down list.
 * @param w        Parent window for the list.
 * @param list     Prepopulated DropDownList.
 * @param selected The initially selected list item.
 * @param button   The widget which is passed to Window::OnDropdownSelect and OnDropdownClose.
 *                 Unless you override those functions, this should be then widget index of the dropdown button.
 * @param wi_rect  Coord of the parent drop down button, used to position the dropdown menu.
 * @param mode_flags Mode flags.
 */
void ShowDropDownListAt(Window *w, DropDownList &&list, int selected, WidgetID button, Rect wi_rect, Colours wi_colour, DropDownModeFlags mode_flags, DropDownSyncFocus sync_parent_focus)
{
	CloseWindowByClass(WC_DROPDOWN_MENU);
	new DropdownWindow(w, std::move(list), selected, button, wi_rect, mode_flags, wi_colour, sync_parent_focus);
}

/**
 * Show a drop down list.
 * @param w        Parent window for the list.
 * @param list     Prepopulated DropDownList.
 * @param selected The initially selected list item.
 * @param button   The widget within the parent window that is used to determine
 *                 the list's location.
 * @param width    Override the minimum width determined by the selected widget and list contents.
 * @param mode_flags Mode flags.
 */
void ShowDropDownList(Window *w, DropDownList &&list, int selected, WidgetID button, uint width, DropDownModeFlags mode_flags, DropDownSyncFocus sync_parent_focus)
{
	/* Our parent's button widget is used to determine where to place the drop
	 * down list window. */
	NWidgetCore *nwi = w->GetWidget<NWidgetCore>(button);
	Rect wi_rect      = nwi->GetCurrentRect();
	Colours wi_colour = nwi->colour;

	if ((nwi->type & WWT_MASK) == NWID_BUTTON_DROPDOWN) {
		nwi->disp_flags |= ND_DROPDOWN_ACTIVE;
	} else {
		nwi->SetLowered(true);
	}
	nwi->SetDirty(w);

	if (width != 0) {
		if (_current_text_dir == TD_RTL) {
			wi_rect.left = wi_rect.right + 1 - ScaleGUITrad(width);
		} else {
			wi_rect.right = wi_rect.left + ScaleGUITrad(width) - 1;
		}
	}

	ShowDropDownListAt(w, std::move(list), selected, button, wi_rect, wi_colour, mode_flags, sync_parent_focus);
}

/**
 * Show a dropdown menu window near a widget of the parent window.
 * The result code of the items is their index in the \a strings list.
 * @param w             Parent window that wants the dropdown menu.
 * @param strings       Menu list, end with #INVALID_STRING_ID
 * @param selected      Index of initial selected item.
 * @param button        Button widget number of the parent window \a w that wants the dropdown menu.
 * @param disabled_mask Bitmask for disabled items (items with their bit set are displayed, but not selectable in the dropdown list).
 * @param hidden_mask   Bitmask for hidden items (items with their bit set are not copied to the dropdown list).
 * @param width         Minimum width of the dropdown menu.
 */
void ShowDropDownMenu(Window *w, const StringID *strings, int selected, WidgetID button, uint32_t disabled_mask, uint32_t hidden_mask, uint width, DropDownSyncFocus sync_parent_focus)
{
	DropDownList list;

	for (uint i = 0; strings[i] != INVALID_STRING_ID; i++) {
		if (i >= 32 || !HasBit(hidden_mask, i)) {
			list.push_back(MakeDropDownListStringItem(strings[i], i, i < 32 && HasBit(disabled_mask, i)));
		}
	}

	if (!list.empty()) ShowDropDownList(w, std::move(list), selected, button, width, DDMF_NONE, sync_parent_focus);
}

/**
 * Delete the drop-down menu from window \a pw
 * @param pw Parent window of the drop-down menu window
 * @return Parent widget number if the drop-down was found and closed, \c -1 if the window was not found.
 */
int HideDropDownMenu(Window *pw)
{
	DropdownWindow *dw = GetDropDownWindowForParent(pw);
	if (dw != nullptr) {
		int parent_button = dw->parent_button;
		dw->Close();
		return parent_button;
	}

	return -1;
}

void GetParentWindowInfo(Window *w, WindowClass &parent_wc, WindowNumber &parent_wn)
{
	DropdownWindow *dw = dynamic_cast<DropdownWindow*>(w);
	assert(dw != nullptr);
	Window *parent = FindWindowByToken(dw->parent_wnd_token);
	if (parent != nullptr) {
		parent_wc = parent->window_class;
		parent_wn = parent->window_number;
	} else {
		parent_wc = WC_INVALID;
		parent_wn = 0;
	}
}
