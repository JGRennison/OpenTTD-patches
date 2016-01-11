/* $Id$ */

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
	ZTW_OUTER = 4,
	ZTW_OUTER_DROPDOWN,
	ZTW_INNER,
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

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
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

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch(widget) {
			case ZTW_OUTER_DROPDOWN:
				_zoning.outer = DropDownIndexToZoningEvaluationMode(index);
				break;

			case ZTW_INNER_DROPDOWN:
				_zoning.inner = DropDownIndexToZoningEvaluationMode(index);
				break;
		}
		this->InvalidateData();
		MarkWholeScreenDirty();
	}

	virtual void SetStringParameters(int widget) const
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

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		const StringID *strs = NULL;
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
			case ZTW_INNER_DROPDOWN:
				strs = _zone_type_strings;
				break;
		}
		if (strs != NULL) {
			while (*strs != INVALID_STRING_ID) {
				*size = maxdim(*size, GetStringBoundingBox(*strs++));
			}
		}
		size->width += padding.width;
		size->height = FONT_HEIGHT_NORMAL + WD_DROPDOWNTEXT_TOP + WD_DROPDOWNTEXT_BOTTOM;
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
			NWidget(NWID_VERTICAL, COLOUR_GREY), SetPadding(5, 0, 5, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_ZONING_OUTER, STR_NULL), SetResize(1, 0), SetPadding(1, 6, 1, 6),
				NWidget(WWT_TEXT, COLOUR_GREY, ZTW_OUTER),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_ZONING_INNER, STR_NULL), SetResize(1, 0), SetPadding(1, 6, 1, 6),
				NWidget(WWT_TEXT, COLOUR_GREY, ZTW_INNER),
			EndContainer(),
			NWidget(NWID_VERTICAL, COLOUR_GREY), SetPadding(5, 0, 5, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_OUTER_DROPDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_INNER_DROPDOWN), SetDataTip(STR_JUST_STRING, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static WindowDesc _zoning_desc (
	WDP_CENTER, "zoning_gui", 0, 0,
	WC_ZONING_TOOLBAR, WC_NONE,
	0,
	_nested_zoning_widgets, lengthof(_nested_zoning_widgets)
);

void ShowZoningToolbar()
{
	AllocateWindowDescFront<ZoningWindow>(&_zoning_desc, 0);
}
