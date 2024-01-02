/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file zoning_gui.cpp */

#include "stdafx.h"
#include "openttd.h"
#include "widgets/dropdown_func.h"
#include "widget_type.h"
#include "window_func.h"
#include "gui.h"
#include "viewport_func.h"
#include "sound_func.h"
#include "table/sprites.h"
#include "table/strings.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "core/geometry_func.hpp"
#include "core/random_func.hpp"
#include "zoning.h"

enum ZoningToolbarWidgets {
	ZTW_OUTER_DROPDOWN,
	ZTW_INNER_DROPDOWN,
	ZTW_CAPTION
};

static const StringID _zone_type_strings[] = {
	STR_ZONING_NO_ZONING,
	STR_ZONING_AUTHORITY,
	STR_ZONING_CAN_BUILD,
	STR_ZONING_STA_CATCH,
	STR_ZONING_STA_CATCH_OPEN,
	STR_ZONING_BUL_UNSER,
	STR_ZONING_IND_UNSER,
	STR_ZONING_TRACERESTRICT,
	STR_ZONING_2x2_GRID,
	STR_ZONING_3x3_GRID,
	STR_ZONING_ONE_WAY_ROAD,
	INVALID_STRING_ID
};

static const ZoningEvaluationMode _zone_type_modes[] = {
	ZEM_NOTHING,
	ZEM_AUTHORITY,
	ZEM_CAN_BUILD,
	ZEM_STA_CATCH,
	ZEM_STA_CATCH_WIN,
	ZEM_BUL_UNSER,
	ZEM_IND_UNSER,
	ZEM_TRACERESTRICT,
	ZEM_2x2_GRID,
	ZEM_3x3_GRID,
	ZEM_ONE_WAY_ROAD,
};

static ZoningEvaluationMode DropDownIndexToZoningEvaluationMode(int index)
{
	if (index < 0 || index >= (int) lengthof(_zone_type_modes)) {
		return ZEM_NOTHING;
	}
	return _zone_type_modes[index];
}

static int ZoningEvaluationModeToDropDownIndex(ZoningEvaluationMode ev_mode)
{
	for (int i = 0; i < (int) lengthof(_zone_type_modes); i++) {
		if (_zone_type_modes[i] == ev_mode) return i;
	}
	NOT_REACHED();
}

struct ZoningWindow : public Window {

	ZoningWindow(WindowDesc *desc, int window_number)
			: Window(desc)
	{
		this->InitNested(window_number);
		this->InvalidateData();
	}

	void OnPaint() override
	{
		this->DrawWidgets();
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
				ShowDropDownMenu(this, _zone_type_strings, ZoningEvaluationModeToDropDownIndex(_zoning.outer), ZTW_OUTER_DROPDOWN, 0, 0);
				break;

			case ZTW_INNER_DROPDOWN:
				ShowDropDownMenu(this, _zone_type_strings, ZoningEvaluationModeToDropDownIndex(_zoning.inner), ZTW_INNER_DROPDOWN, 0, 0);
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch(widget) {
			case ZTW_OUTER_DROPDOWN:
				SetZoningMode(false, DropDownIndexToZoningEvaluationMode(index));
				break;

			case ZTW_INNER_DROPDOWN:
				SetZoningMode(true, DropDownIndexToZoningEvaluationMode(index));
				break;
		}
		this->InvalidateData();
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
				SetDParam(0, _zone_type_strings[ZoningEvaluationModeToDropDownIndex(_zoning.outer)]);
				break;

			case ZTW_INNER_DROPDOWN:
				SetDParam(0, _zone_type_strings[ZoningEvaluationModeToDropDownIndex(_zoning.inner)]);
				break;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		const StringID *strs = nullptr;
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
			case ZTW_INNER_DROPDOWN:
				strs = _zone_type_strings;
				break;

			default:
				return;
		}
		if (strs != nullptr) {
			while (*strs != INVALID_STRING_ID) {
				*size = maxdim(*size, GetStringBoundingBox(*strs++));
			}
		}
		size->width += padding.width;
		size->height = GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.dropdowntext.Vertical();
	}
};

static const NWidgetPart _nested_zoning_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, ZTW_CAPTION), SetDataTip(STR_ZONING_TOOLBAR, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL, COLOUR_GREY), SetPIP(10, 3, 10),
			NWidget(NWID_VERTICAL, COLOUR_GREY), SetPadding(5, 0, 5, 0), SetPIP(0, 5, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_ZONING_OUTER, STR_ZONING_OUTER_INFO), SetResize(1, 0), SetPadding(1, 6, 1, 6),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_ZONING_INNER, STR_ZONING_INNER_INFO), SetResize(1, 0), SetPadding(1, 6, 1, 6),
			EndContainer(),
			NWidget(NWID_VERTICAL, COLOUR_GREY), SetPadding(5, 0, 5, 0), SetPIP(0, 5, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_OUTER_DROPDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_INNER_DROPDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static WindowDesc _zoning_desc (__FILE__, __LINE__,
	WDP_CENTER, "zoning_gui", 0, 0,
	WC_ZONING_TOOLBAR, WC_NONE,
	0,
	std::begin(_nested_zoning_widgets), std::end(_nested_zoning_widgets)
);

void ShowZoningToolbar()
{
	AllocateWindowDescFront<ZoningWindow>(&_zoning_desc, 0);
}
