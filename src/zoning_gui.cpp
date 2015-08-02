
/** @file zoning_gui.cpp */

#include "stdafx.h"
#include "openttd.h"
#include "widgets/dropdown_func.h"
#include "widget_type.h"
#include "functions.h"
#include "window_func.h"
#include "gui.h"
#include "viewport_func.h"
#include "sound_func.h"
#include "variables.h"
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

const StringID _zone_types[] = { STR_ZONING_NO_ZONING, STR_ZONING_AUTHORITY, STR_ZONING_CAN_BUILD, STR_ZONING_STA_CATCH, STR_ZONING_IND_CATCH, STR_ZONING_BUL_CATCH, STR_ZONING_BUL_UNSER, STR_ZONING_IND_UNSER, INVALID_STRING_ID };

struct ZoningWindow : public Window {
	
	ZoningWindow(const WindowDesc *desc, int window_number) : Window() {
		this->InitNested(desc, window_number);	
		this->InvalidateData();	
	}
	
	virtual void OnPaint() {
		this->DrawWidgets();
	}
	
	virtual void OnClick(Point pt, int widget, int click_count) {
		switch ( widget ) {
			case ZTW_OUTER_DROPDOWN:
				ShowDropDownMenu(this, _zone_types, _zoning.outer_val, ZTW_OUTER_DROPDOWN, 0, 0);
				break;
			case ZTW_INNER_DROPDOWN:
				ShowDropDownMenu(this, _zone_types, _zoning.inner_val, ZTW_INNER_DROPDOWN, 0, 0);
				break;
		}				
	}
	
	virtual void OnDropdownSelect(int widget, int index) {
		switch(widget) {
			case ZTW_OUTER_DROPDOWN:
				_zoning.outer_val = index;
				_zoning.outer = GetEvaluationModeFromInt(_zoning.outer_val);
				break;
			case ZTW_INNER_DROPDOWN:
				_zoning.inner_val = index;
				_zoning.inner = GetEvaluationModeFromInt(_zoning.inner_val);
				break;
		}
		this->InvalidateData();
		MarkWholeScreenDirty();
	}
	
	virtual void SetStringParameters(int widget) const {
		switch ( widget ) {
			case ZTW_OUTER_DROPDOWN: SetDParam(0, _zone_types[_zoning.outer]); break;
			case ZTW_INNER_DROPDOWN: SetDParam(0, _zone_types[_zoning.inner]); break;
		}
	}
	
	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) {
		const StringID *strs = NULL;
		switch ( widget ) {
			case ZTW_OUTER_DROPDOWN: strs = _zone_types; break;
			case ZTW_INNER_DROPDOWN: strs = _zone_types; break;
		}
		if ( strs != NULL ) {
			while ( *strs != INVALID_STRING_ID ) {
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

static const WindowDesc _zoning_desc (
	WDP_CENTER, 0, 0,
	WC_ZONING_TOOLBAR, WC_NONE,
	0,
	_nested_zoning_widgets, lengthof(_nested_zoning_widgets)
);

void ShowZoningToolbar() {
	AllocateWindowDescFront<ZoningWindow>(&_zoning_desc, 0);
}
