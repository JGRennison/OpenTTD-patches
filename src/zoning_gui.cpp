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
#include "debug_settings.h"

#include <initializer_list>

enum ZoningToolbarWidgets {
	ZTW_OUTER_DROPDOWN,
	ZTW_INNER_DROPDOWN,
	ZTW_CAPTION
};

struct ZoningModeInfo {
	ZoningEvaluationMode mode;
	StringID str;
	const char *param;
	bool debug;

	ZoningModeInfo(ZoningEvaluationMode mode, StringID str) : mode(mode), str(str), param(""), debug(false) {}
	ZoningModeInfo(ZoningEvaluationMode mode, const char *param, bool debug = true) : mode(mode), str(STR_JUST_RAW_STRING), param(param), debug(debug) {}
};

static const std::initializer_list<ZoningModeInfo> _zone_modes = {
	ZoningModeInfo(ZEM_NOTHING,          STR_ZONING_NO_ZONING),
	ZoningModeInfo(ZEM_AUTHORITY,        STR_ZONING_AUTHORITY),
	ZoningModeInfo(ZEM_CAN_BUILD,        STR_ZONING_CAN_BUILD),
	ZoningModeInfo(ZEM_STA_CATCH,        STR_ZONING_STA_CATCH),
	ZoningModeInfo(ZEM_STA_CATCH_WIN,    STR_ZONING_STA_CATCH_OPEN),
	ZoningModeInfo(ZEM_BUL_UNSER,        STR_ZONING_BUL_UNSER),
	ZoningModeInfo(ZEM_IND_UNSER,        STR_ZONING_IND_UNSER),
	ZoningModeInfo(ZEM_TRACERESTRICT,    STR_ZONING_TRACERESTRICT),
	ZoningModeInfo(ZEM_2x2_GRID,         STR_ZONING_2x2_GRID),
	ZoningModeInfo(ZEM_3x3_GRID,         STR_ZONING_3x3_GRID),
	ZoningModeInfo(ZEM_ONE_WAY_ROAD,     STR_ZONING_ONE_WAY_ROAD),

	ZoningModeInfo(ZEM_DBG_WATER_FLOOD,   "Debug: Flooding"),
	ZoningModeInfo(ZEM_DBG_WATER_REGION,  "Debug: Water regions"),
	ZoningModeInfo(ZEM_DBG_TROPIC_ZONE,   "Debug: Tropic zones"),
	ZoningModeInfo(ZEM_DBG_ANIMATED_TILE, "Debug: Animated tiles"),
};

static const ZoningModeInfo &ZoningEvaluationModeToInfo(ZoningEvaluationMode ev_mode)
{
	for (const ZoningModeInfo &info : _zone_modes) {
		if (info.mode == ev_mode) return info;
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

	static inline bool IsDebugEnabled()
	{
		return HasBit(_misc_debug_flags, MDF_ZONING_DEBUG_MODES);
	}

	void OnPaint() override
	{
		this->DrawWidgets();
	}

	void ShowZoningDropDown(WidgetID widget, ZoningEvaluationMode current)
	{
		DropDownList list;
		for (const ZoningModeInfo &info : _zone_modes) {
			if (info.debug && !IsDebugEnabled()) continue;
			SetDParamStr(0, info.param);
			list.push_back(std::make_unique<DropDownListStringItem>(info.str, info.mode, false));
		}
		ShowDropDownList(this, std::move(list), current, widget);
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
				this->ShowZoningDropDown(ZTW_OUTER_DROPDOWN, _zoning.outer);
				break;

			case ZTW_INNER_DROPDOWN:
				this->ShowZoningDropDown(ZTW_INNER_DROPDOWN, _zoning.inner);
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch(widget) {
			case ZTW_OUTER_DROPDOWN:
				SetZoningMode(false, (ZoningEvaluationMode)index);
				break;

			case ZTW_INNER_DROPDOWN:
				SetZoningMode(true, (ZoningEvaluationMode)index);
				break;
		}
		this->InvalidateData();
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
			case ZTW_INNER_DROPDOWN: {
				const ZoningModeInfo &info = ZoningEvaluationModeToInfo(widget == ZTW_OUTER_DROPDOWN ? _zoning.outer : _zoning.inner);
				SetDParam(0, info.str);
				SetDParamStr(1, info.param);
				break;
			}
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case ZTW_OUTER_DROPDOWN:
			case ZTW_INNER_DROPDOWN:
				for (const ZoningModeInfo &info : _zone_modes) {
					SetDParamStr(0, info.param);
					*size = maxdim(*size, GetStringBoundingBox(info.str));
				}
				break;

			default:
				return;
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
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_OUTER_DROPDOWN), SetDataTip(STR_JUST_STRING1, STR_NULL), SetFill(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, ZTW_INNER_DROPDOWN), SetDataTip(STR_JUST_STRING1, STR_NULL), SetFill(1, 0),
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
