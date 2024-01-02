/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file date_gui.cpp Graphical selection of a date. */

#include "stdafx.h"
#include "strings_func.h"
#include "date_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "date_gui.h"
#include "core/geometry_func.hpp"
#include "settings_type.h"

#include "widgets/dropdown_type.h"
#include "widgets/date_widget.h"

#include "safeguards.h"


/** Window to select a date graphically by using dropdowns */
struct SetDateWindow : Window {
	SetDateCallback *callback; ///< Callback to call when a date has been selected
	YearMonthDay date; ///< The currently selected date
	Year min_year;     ///< The minimum year in the year dropdown
	Year max_year;     ///< The maximum year (inclusive) in the year dropdown

	/**
	 * Create the new 'set date' window
	 * @param desc the window description
	 * @param window_number number of the window
	 * @param parent the parent window, i.e. if this closes we should close too
	 * @param initial_date the initial date to show
	 * @param min_year the minimum year to show in the year dropdown
	 * @param max_year the maximum year (inclusive) to show in the year dropdown
	 * @param callback the callback to call once a date has been selected
	 */
	SetDateWindow(WindowDesc *desc, WindowNumber window_number, Window *parent, Date initial_date, Year min_year, Year max_year,
				SetDateCallback *callback, StringID button_text, StringID button_tooltip) :
			Window(desc),
			callback(callback),
			min_year(std::max(MIN_YEAR, min_year)),
			max_year(std::min(MAX_YEAR, max_year))
	{
		assert(this->min_year <= this->max_year);
		this->parent = parent;
		this->CreateNestedTree();
		if (button_text != STR_NULL || button_tooltip != STR_NULL) {
			NWidgetCore *btn = this->GetWidget<NWidgetCore>(WID_SD_SET_DATE);
			if (button_text != STR_NULL) btn->widget_data = button_text;
			if (button_tooltip != STR_NULL) btn->tool_tip = button_tooltip;
		}
		this->FinishInitNested(window_number);

		if (initial_date == 0) initial_date = _date;
		this->date = ConvertDateToYMD(initial_date);
		this->date.year = Clamp(this->date.year, min_year, max_year);
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		Point pt = { this->parent->left + this->parent->width / 2 - sm_width / 2, this->parent->top + this->parent->height / 2 - sm_height / 2 };
		return pt;
	}

	/**
	 * Helper function to construct the dropdown.
	 * @param widget the dropdown widget to create the dropdown for
	 */
	virtual void ShowDateDropDown(WidgetID widget)
	{
		int selected;
		DropDownList list;

		switch (widget) {
			default: NOT_REACHED();

			case WID_SD_DAY:
				for (uint i = 0; i < 31; i++) {
					list.push_back(std::make_unique<DropDownListStringItem>(STR_DAY_NUMBER_1ST + i, i + 1, false));
				}
				selected = this->date.day;
				break;

			case WID_SD_MONTH:
				for (uint i = 0; i < 12; i++) {
					list.push_back(std::make_unique<DropDownListStringItem>(STR_MONTH_JAN + i, i, false));
				}
				selected = this->date.month;
				break;

			case WID_SD_YEAR:
				for (Year i = this->min_year; i <= this->max_year; i++) {
					SetDParam(0, i);
					list.push_back(std::make_unique<DropDownListStringItem>(STR_JUST_INT, i, false));
				}
				selected = this->date.year;
				break;
		}

		ShowDropDownList(this, std::move(list), selected, widget);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		Dimension d = {0, 0};
		switch (widget) {
			default: return;

			case WID_SD_DAY:
				for (uint i = 0; i < 31; i++) {
					d = maxdim(d, GetStringBoundingBox(STR_DAY_NUMBER_1ST + i));
				}
				break;

			case WID_SD_MONTH:
				for (uint i = 0; i < 12; i++) {
					d = maxdim(d, GetStringBoundingBox(STR_MONTH_JAN + i));
				}
				break;

			case WID_SD_YEAR:
				SetDParamMaxValue(0, this->max_year);
				d = maxdim(d, GetStringBoundingBox(STR_JUST_INT));
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		*size = d;
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_SD_DAY:   SetDParam(0, this->date.day - 1 + STR_DAY_NUMBER_1ST); break;
			case WID_SD_MONTH: SetDParam(0, this->date.month + STR_MONTH_JAN); break;
			case WID_SD_YEAR:  SetDParam(0, this->date.year); break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_SD_DAY:
			case WID_SD_MONTH:
			case WID_SD_YEAR:
				ShowDateDropDown(widget);
				break;
			case WID_SD_SET_DATE:
				if (this->callback != nullptr) {
					this->callback(this, DateToScaledDateTicks(ConvertYMDToDate(this->date.year, this->date.month, this->date.day)));
				}
				this->Close();
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch (widget) {
			case WID_SD_DAY:
				this->date.day = index;
				break;

			case WID_SD_MONTH:
				this->date.month = index;
				break;

			case WID_SD_YEAR:
				this->date.year = index;
				break;
		}
		this->SetDirty();
	}
};

struct SetMinutesWindow : SetDateWindow
{
	TickMinutes minutes;

	/** Constructor. */
	SetMinutesWindow(WindowDesc *desc, WindowNumber window_number, Window *parent, DateTicksScaled initial_date, Year min_year, Year max_year,
				SetDateCallback *callback, StringID button_text, StringID button_tooltip) :
			SetDateWindow(desc, window_number, parent, 0, min_year, max_year, callback, button_text, button_tooltip),
			minutes(_settings_time.ToTickMinutes(initial_date))
	{
	}

	/**
	 * Helper function to construct the dropdown.
	 * @param widget the dropdown widget to create the dropdown for
	 */
	virtual void ShowDateDropDown(WidgetID widget) override
	{
		int selected;
		DropDownList list;

		switch (widget) {
			default: NOT_REACHED();

			case WID_SD_DAY:
				for (uint i = 0; i < 60; i++) {
					SetDParam(0, i);
					list.emplace_back(new DropDownListStringItem(STR_JUST_INT, i, false));
				}
				selected = this->minutes.ClockMinute();
				break;

			case WID_SD_MONTH:
				for (uint i = 0; i < 24; i++) {
					SetDParam(0, i);
					list.emplace_back(new DropDownListStringItem(STR_JUST_INT, i, false));
				}
				selected = this->minutes.ClockHour();

				break;
		}

		ShowDropDownList(this, std::move(list), selected, widget);
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		Dimension d = {0, 0};
		switch (widget) {
			default: return;

			case WID_SD_DAY:
				for (uint i = 0; i < 60; i++) {
					SetDParam(0, i);
					d = maxdim(d, GetStringBoundingBox(STR_JUST_INT));
				}
				break;

			case WID_SD_MONTH:
				for (uint i = 0; i < 24; i++) {
					SetDParam(0, i);
					d = maxdim(d, GetStringBoundingBox(STR_JUST_INT));
				}
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		*size = d;
	}

	virtual void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_SD_DAY:   SetDParam(0, this->minutes.ClockMinute()); break;
			case WID_SD_MONTH: SetDParam(0, this->minutes.ClockHour()); break;
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_SD_DAY:
			case WID_SD_MONTH:
			case WID_SD_YEAR:
				ShowDateDropDown(widget);
				break;

			case WID_SD_SET_DATE:
				if (this->callback != nullptr) {
					this->callback(this, _settings_time.FromTickMinutes(this->minutes));
				}
				this->Close();
				break;
		}
	}

	virtual void OnDropdownSelect(WidgetID widget, int index) override
	{
		const TickMinutes now = _settings_time.NowInTickMinutes();
		TickMinutes current = 0;
		switch (widget) {
			case WID_SD_DAY:
				current = now.ToSameDayClockTime(now.ClockHour(), index);
				break;

			case WID_SD_MONTH:
				current = now.ToSameDayClockTime(index, now.ClockMinute());
				break;

			default:
				return;
		}

		if (current < (now - 60)) current += 60 * 24;
		this->minutes = current;

		this->SetDirty();
	}
};

/** Widgets for the date setting window. */
static const NWidgetPart _nested_set_date_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_DATE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(6, 6, 6),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SD_DAY), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_DATE_DAY_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SD_MONTH), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_DATE_MONTH_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SD_YEAR), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_YEAR_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_SD_SET_DATE), SetMinimalSize(100, 12), SetDataTip(STR_DATE_SET_DATE, STR_DATE_SET_DATE_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static const NWidgetPart _nested_set_minutes_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_TIME_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(6, 6, 6),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SD_MONTH), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_HOUR_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SD_DAY), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_MINUTE_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_SD_SET_DATE), SetMinimalSize(100, 12), SetDataTip(STR_DATE_SET_DATE, STR_DATE_SET_DATE_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

/** Description of the date setting window. */
static WindowDesc _set_date_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_SET_DATE, WC_NONE,
	0,
	std::begin(_nested_set_date_widgets), std::end(_nested_set_date_widgets)
);

static WindowDesc _set_minutes_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_SET_DATE, WC_NONE,
	0,
	std::begin(_nested_set_minutes_widgets), std::end(_nested_set_minutes_widgets)
);

/**
 * Create the new 'set date' window
 * @param window_number number for the window
 * @param parent the parent window, i.e. if this closes we should close too
 * @param initial_date the initial date to show
 * @param min_year the minimum year to show in the year dropdown
 * @param max_year the maximum year (inclusive) to show in the year dropdown
 * @param callback the callback to call once a date has been selected
 */
void ShowSetDateWindow(Window *parent, int window_number, DateTicksScaled initial_date, Year min_year, Year max_year,
		SetDateCallback *callback, StringID button_text, StringID button_tooltip)
{
	CloseWindowByClass(WC_SET_DATE);

	if (!_settings_time.time_in_minutes) {
		new SetDateWindow(&_set_date_desc, window_number, parent, ScaledDateTicksToDate(initial_date), min_year, max_year, callback, button_text, button_tooltip);
	} else {
		new SetMinutesWindow(&_set_minutes_desc, window_number, parent,
				initial_date + (_settings_game.economy.day_length_factor * (_settings_time.clock_offset * _settings_time.ticks_per_minute)),
				min_year, max_year, callback, button_text, button_tooltip);
	}
}
