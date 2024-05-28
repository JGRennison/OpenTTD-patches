/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file screenshot_gui.cpp GUI functions related to screenshots. */

#include "stdafx.h"
#include "window_func.h"
#include "window_gui.h"
#include "screenshot.h"

#include "widgets/screenshot_widget.h"

#include "table/strings.h"

#include "safeguards.h"

struct ScreenshotWindow : Window {
	ScreenshotWindow(WindowDesc *desc) : Window(desc)
	{
		this->CreateNestedTree();
		this->FinishInitNested();
	}

	void OnPaint() override
	{
		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		ScreenshotType st;
		switch (widget) {
			default: return;
			case WID_SC_TAKE:             st = SC_VIEWPORT;    break;
			case WID_SC_TAKE_ZOOMIN:      st = SC_ZOOMEDIN;    break;
			case WID_SC_TAKE_DEFAULTZOOM: st = SC_DEFAULTZOOM; break;
			case WID_SC_TAKE_WORLD:       st = SC_WORLD;       break;
			case WID_SC_TAKE_WORLD_ZOOM:  st = SC_WORLD_ZOOM;  break;
			case WID_SC_TAKE_HEIGHTMAP:   st = SC_HEIGHTMAP;   break;
			case WID_SC_TAKE_MINIMAP:     st = SC_MINIMAP;     break;
			case WID_SC_TAKE_TOPOGRAPHY:  st = SC_TOPOGRAPHY;  break;
			case WID_SC_TAKE_INDUSTRY:    st = SC_INDUSTRY;    break;
		}
		MakeScreenshotWithConfirm(st);
	}
};

static constexpr NWidgetPart _nested_screenshot[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_SCREENSHOT_CAPTION, 0),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_VERTICAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_ZOOMIN), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_ZOOMIN_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_DEFAULTZOOM), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_DEFAULTZOOM_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_WORLD), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_WORLD_SCREENSHOT_DEFAULT_ZOOM, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_WORLD_ZOOM), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_WORLD_SCREENSHOT_CURRENT_ZOOM, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_HEIGHTMAP), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_HEIGHTMAP_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_MINIMAP), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_MINIMAP_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_TOPOGRAPHY), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_TOPOGRAPHY_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SC_TAKE_INDUSTRY), SetFill(1, 1), SetDataTip(STR_SCREENSHOT_INDUSTRY_SCREENSHOT, 0), SetMinimalTextLines(2, 0),
	EndContainer(),
};

static WindowDesc _screenshot_window_desc(__FILE__, __LINE__,
	WDP_AUTO, "take_a_screenshot", 200, 100,
	WC_SCREENSHOT, WC_NONE,
	0,
	std::begin(_nested_screenshot), std::end(_nested_screenshot)
);

void ShowScreenshotWindow()
{
	CloseWindowById(WC_SCREENSHOT, 0);
	new ScreenshotWindow(&_screenshot_window_desc);
}

void SetScreenshotWindowHidden(bool hidden)
{
	ScreenshotWindow *scw = (ScreenshotWindow *) FindWindowById(WC_SCREENSHOT, 0);
	if (scw != nullptr) {
		if (hidden) {
			scw->SetDirtyAsBlocks();
			SetBit(scw->left, 30);
		} else {
			ClrBit(scw->left, 30);
			scw->SetDirtyAsBlocks();
		}
	}
}
