/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file date_gui.cpp Graphical selection of a date. */

#include "stdafx.h"
#include "strings_func.h"
#include "date_func.h"
#include "window_func.h"
#include "window_gui.h"
#include "date_gui.h"
#include "settings_type.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "string_func_extra.h"
#include "querystring_gui.h"
#include "core/format.hpp"
#include "core/geometry_func.hpp"

#include "widgets/date_widget.h"

#include "table/strings.h"

#include "safeguards.h"

struct SetDateWindowCommon : Window {
	using Window::Window;

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, int window_number) override
	{
		Point pt = { this->parent->left + this->parent->width / 2 - sm_width / 2, this->parent->top + this->parent->height / 2 - sm_height / 2 };
		return pt;
	}
};

/** Window to select a date graphically by using dropdowns */
struct SetDateWindow : SetDateWindowCommon {
	SetTickCallback *callback = nullptr; ///< Callback to call when a date has been selected
	void *callback_data = nullptr;       ///< Data provided to callback
	EconTime::YearMonthDay date{};       ///< The currently selected date
	EconTime::Year min_year{};           ///< The minimum year in the year dropdown
	EconTime::Year max_year{};           ///< The maximum year (inclusive) in the year dropdown

	/**
	 * Create the new 'set date' window
	 * @param desc the window description
	 * @param window_number number of the window
	 * @param parent the parent window, i.e. if this closes we should close too
	 * @param initial_date the initial date to show
	 * @param min_year the minimum year to show in the year dropdown
	 * @param max_year the maximum year (inclusive) to show in the year dropdown
	 * @param callback the callback to call once a date has been selected
	 * @param callback_data arbitrary data to pass to callback
	 */
	SetDateWindow(WindowDesc &desc, WindowNumber window_number, Window *parent, EconTime::Date initial_date, EconTime::Year min_year, EconTime::Year max_year,
				SetTickCallback *callback, void *callback_data, StringID button_text, StringID button_tooltip) :
			SetDateWindowCommon(desc),
			callback(callback),
			callback_data(callback_data),
			min_year(std::max(EconTime::MIN_YEAR, min_year)),
			max_year(std::min(EconTime::MAX_YEAR, max_year))
	{
		assert(this->min_year <= this->max_year);
		this->parent = parent;
		this->CreateNestedTree();
		if (button_text != STR_NULL || button_tooltip != STR_NULL) {
			NWidgetCore *btn = this->GetWidget<NWidgetCore>(WID_SD_SET_DATE);
			if (button_text != STR_NULL) btn->SetString(button_text);
			if (button_tooltip != STR_NULL) btn->SetToolTip(button_tooltip);
		}
		this->FinishInitNested(window_number);

		if (initial_date == 0) initial_date = EconTime::CurDate();
		this->date = EconTime::ConvertDateToYMD(initial_date);
		this->date.year = Clamp(this->date.year, min_year, max_year);
	}

	/**
	 * Helper function to construct the dropdown.
	 * @param widget the dropdown widget to create the dropdown for
	 */
	void ShowDateDropDown(WidgetID widget)
	{
		int selected;
		DropDownList list;

		switch (widget) {
			default: NOT_REACHED();

			case WID_SD_DAY:
				for (uint i = 0; i < 31; i++) {
					list.push_back(MakeDropDownListStringItem(STR_DAY_NUMBER_1ST + i, i + 1));
				}
				selected = this->date.day;
				break;

			case WID_SD_MONTH:
				for (uint i = 0; i < 12; i++) {
					list.push_back(MakeDropDownListStringItem(STR_MONTH_JAN + i, i));
				}
				selected = this->date.month;
				break;

			case WID_SD_YEAR:
				for (EconTime::Year i = this->min_year; i <= this->max_year; i++) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, i), i.base()));
				}
				selected = this->date.year.base();
				break;
		}

		ShowDropDownList(this, std::move(list), selected, widget);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
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
				d = maxdim(d, GetStringBoundingBox(GetString(STR_JUST_INT, GetParamMaxValue(this->max_year.base()))));
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		size = d;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_SD_DAY:   return GetString(STR_DAY_NUMBER_1ST + this->date.day - 1);
			case WID_SD_MONTH: return GetString(STR_MONTH_JAN + this->date.month);
			case WID_SD_YEAR:  return GetString(STR_JUST_INT, this->date.year);
			default: return this->Window::GetWidgetString(widget, stringid);
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
					this->callback(this, DateToStateTicks(EconTime::ConvertYMDToDate(this->date.year, this->date.month, this->date.day)), this->callback_data);
				}
				this->Close();
				break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_SD_DAY:
				this->date.day = index;
				break;

			case WID_SD_MONTH:
				this->date.month = index;
				break;

			case WID_SD_YEAR:
				this->date.year = EconTime::Year{index};
				break;
		}
		this->SetDirty();
	}
};

struct SetMinutesWindow : SetDateWindowCommon
{
	SetTickCallback *callback = nullptr; ///< Callback to call when a date has been selected
	void *callback_data = nullptr;       ///< Data provided to callback
	ClockFaceMinutes clock_face{};
	TickMinutesDayNumber base_day_number{};
	QueryString time_editbox{5 * MAX_CHAR_LENGTH, 5};
	QueryString day_number_editbox{6 * MAX_CHAR_LENGTH, 6};
	SetDateWindowFlags flags;

	/** Constructor. */
	SetMinutesWindow(WindowDesc &desc, WindowNumber window_number, Window *parent, StateTicks initial_tick,
				SetDateWindowFlags flags, SetTickCallback *callback, void *callback_data, StringID button_text, StringID button_tooltip) :
			SetDateWindowCommon(desc),
			callback(callback),
			callback_data(callback_data),
			flags(flags)
	{
		TickMinutes minutes = _settings_time.ToTickMinutes(initial_tick);
		this->clock_face = minutes.ToClockFaceMinutes();
		this->parent = parent;
		this->CreateNestedTree();
		if (this->flags.Test(SetDateWindowFlag::TextMode)) {
			this->GetWidget<NWidgetStacked>(WID_SM_TIME_SEL)->SetDisplayedPlane(1);
			this->querystrings[WID_SM_TEXT] = &this->time_editbox;

			format_buffer_sized<32> buf;
			buf.format("{:04}", this->clock_face.ClockHHMM());
			this->time_editbox.text.Assign(buf);
			this->time_editbox.text.afilter = CS_NUMERAL;
			this->time_editbox.ok_button = WID_SM_SET_DATE;
		}
		if (this->flags.Test(SetDateWindowFlag::ShowMinutesModeDayOffset)) {
			this->base_day_number = _settings_time.NowInTickMinutes().DayNumber();
			this->querystrings[WID_SM_DAY_NUM] = &this->day_number_editbox;

			format_buffer_sized<32> buf;
			buf.format("{}", minutes.DayNumber() - this->base_day_number);
			this->day_number_editbox.text.Assign(buf);
			this->day_number_editbox.text.afilter = CS_NUMERAL_SIGNED;
			this->day_number_editbox.ok_button = WID_SM_SET_DATE;
		} else {
			this->GetWidget<NWidgetStacked>(WID_SM_DAY_SEL)->SetDisplayedPlane(SZSP_NONE);
		}
		if (button_text != STR_NULL || button_tooltip != STR_NULL) {
			NWidgetCore *btn = this->GetWidget<NWidgetCore>(WID_SM_SET_DATE);
			if (button_text != STR_NULL) btn->SetString(button_text);
			if (button_tooltip != STR_NULL) btn->SetToolTip(button_tooltip);
		}
		this->FinishInitNested(window_number);

		if (this->flags.Test(SetDateWindowFlag::TextMode)) {
			this->SetFocusedWidget(WID_SM_TEXT);
		}
	}

	EventState OnKeyPress(char32_t key, uint16_t keycode) override
	{
		if (keycode == WKC_TAB && this->flags.Test(SetDateWindowFlag::ShowMinutesModeDayOffset) && this->flags.Test(SetDateWindowFlag::TextMode) && this->nested_focus != nullptr) {
			auto focus_wid = this->nested_focus->GetIndex();
			switch (focus_wid) {
				case WID_SM_TEXT:
					this->SetFocusedWidget(WID_SM_DAY_NUM);
					break;
				case WID_SM_DAY_NUM:
					this->SetFocusedWidget(WID_SM_TEXT);
					break;
				default:
					return EventState::NotHandled;
			}
			return EventState::Handled;
		} else {
			return EventState::NotHandled;
		}
	}

	/**
	 * Helper function to construct the dropdown.
	 * @param widget the dropdown widget to create the dropdown for
	 */
	void ShowDateDropDown(WidgetID widget)
	{
		int selected;
		DropDownList list;

		switch (widget) {
			default: NOT_REACHED();

			case WID_SM_MINUTE:
				for (uint i = 0; i < 60; i++) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, i), i, false));
				}
				selected = this->clock_face.ClockMinute();
				break;

			case WID_SM_HOUR:
				for (uint i = 0; i < 24; i++) {
					list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, i), i, false));
				}
				selected = this->clock_face.ClockHour();

				break;
		}

		ShowDropDownList(this, std::move(list), selected, widget);
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		Dimension d = {0, 0};
		switch (widget) {
			default: return;

			case WID_SM_MINUTE:
				for (uint i = 0; i < 60; i++) {
					d = maxdim(d, GetStringBoundingBox(GetString(STR_JUST_INT, i)));
				}
				break;

			case WID_SM_HOUR:
				for (uint i = 0; i < 24; i++) {
					d = maxdim(d, GetStringBoundingBox(GetString(STR_JUST_INT, i)));
				}
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		size = d;
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_SM_MINUTE: return GetString(STR_JUST_INT, this->clock_face.ClockMinute());
			case WID_SM_HOUR:   return GetString(STR_JUST_INT, this->clock_face.ClockHour());
			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_SM_HOUR:
			case WID_SM_MINUTE:
				ShowDateDropDown(widget);
				break;

			case WID_SM_SET_DATE: {
				if (this->flags.Test(SetDateWindowFlag::TextMode)) {
					auto result = IntFromChars<int32_t>(this->time_editbox.text.GetText());
					if (!result.has_value() || *result < 0) return;
					int hours = (*result / 100) % 24;
					int minutes = *result % 100;
					if (minutes >= 60) return;
					this->clock_face = ClockFaceMinutes::FromClockFace(hours, minutes);
				}

				TickMinutesDayNumber day_number{};
				if (this->flags.Test(SetDateWindowFlag::ShowMinutesModeDayOffset)) {
					day_number = this->base_day_number;
					auto result = IntFromChars<int32_t>(this->day_number_editbox.text.GetText());
					if (result.has_value()) day_number = day_number + *result;
				} else {
					day_number = _settings_time.NowInTickMinutes().DayNumber();
				}

				if (this->callback != nullptr) {
					this->callback(this, _settings_time.FromTickMinutes(TickMinutes::FromAbsolute(day_number, this->clock_face)), this->callback_data);
				}
				this->Close();
				break;
			}
		}
	}

	virtual void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		switch (widget) {
			case WID_SM_MINUTE:
				this->clock_face = ClockFaceMinutes::FromClockFace(this->clock_face.ClockHour(), index);
				break;

			case WID_SM_HOUR:
				this->clock_face = ClockFaceMinutes::FromClockFace(index, this->clock_face.ClockMinute());
				break;

			default:
				return;
		}

		this->SetDirty();
	}
};

/** Widgets for the date setting window. */
static constexpr std::initializer_list<NWidgetPart> _nested_set_date_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_DATE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
				NWidget(WWT_DROPDOWN, Colours::Orange, WID_SD_DAY), SetFill(1, 0), SetToolTip(STR_DATE_DAY_TOOLTIP),
				NWidget(WWT_DROPDOWN, Colours::Orange, WID_SD_MONTH), SetFill(1, 0), SetToolTip(STR_DATE_MONTH_TOOLTIP),
				NWidget(WWT_DROPDOWN, Colours::Orange, WID_SD_YEAR), SetFill(1, 0), SetToolTip(STR_DATE_YEAR_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_SD_SET_DATE), SetMinimalSize(100, 12), SetStringTip(STR_DATE_SET_DATE, STR_DATE_SET_DATE_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static constexpr NWidgetPart _nested_set_minutes_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_TIME_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_SM_TIME_SEL),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_DROPDOWN, Colours::Orange, WID_SM_HOUR), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_HOUR_TOOLTIP),
					NWidget(WWT_DROPDOWN, Colours::Orange, WID_SM_MINUTE), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_MINUTE_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, Colours::Invalid), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_TIME, STR_NULL),
					NWidget(WWT_EDITBOX, Colours::Grey, WID_SM_TEXT), SetFill(1, 0), SetToolTip(STR_DATE_HHMM_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, Colours::Invalid, WID_SM_DAY_SEL),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, Colours::Invalid), SetStringTip(STR_DATE_DAY_OFFSET, STR_NULL),
					NWidget(WWT_EDITBOX, Colours::Grey, WID_SM_DAY_NUM), SetFill(1, 0), SetToolTip(STR_DATE_DAY_OFFSET_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_SM_SET_DATE), SetMinimalSize(100, 12), SetStringTip(STR_DATE_SET_DATE, STR_DATE_SET_DATE_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

/** Description of the date setting window. */
static WindowDesc _set_date_desc(__FILE__, __LINE__,
	WindowPosition::Center, nullptr, 0, 0,
	WindowClass::SetDate, WindowClass::None,
	{},
	_nested_set_date_widgets
);

static WindowDesc _set_minutes_desc(__FILE__, __LINE__,
	WindowPosition::Center, nullptr, 0, 0,
	WindowClass::SetDate, WindowClass::None,
	{},
	_nested_set_minutes_widgets
);

/**
 * Create the new 'set date' window
 * @param window_number number for the window
 * @param parent the parent window, i.e. if this closes we should close too
 * @param initial_tick the initial tick to show
 * @param min_year the minimum year to show in the year dropdown
 * @param max_year the maximum year (inclusive) to show in the year dropdown
 * @param callback the callback to call once a date has been selected
 * @param callback_data arbitrary data to pass to callback
 */
void ShowSetDateWindow(Window *parent, int window_number, StateTicks initial_tick, EconTime::Year min_year, EconTime::Year max_year,
		SetTickCallback *callback, void *callback_data, StringID button_text, StringID button_tooltip, SetDateWindowFlags flags)
{
	CloseWindowByClass(WindowClass::SetDate);

	if (!_settings_time.time_in_minutes) {
		new SetDateWindow(_set_date_desc, window_number, parent, StateTicksToDate(initial_tick), min_year, max_year, callback, callback_data, button_text, button_tooltip);
	} else {
		new SetMinutesWindow(_set_minutes_desc, window_number, parent, initial_tick, flags, callback, callback_data, button_text, button_tooltip);
	}
}
