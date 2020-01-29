/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file schdispatch_gui.cpp GUI code for Scheduled Dispatch */

#include "stdafx.h"
#include "command_func.h"
#include "gui.h"
#include "window_gui.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "string_func.h"
#include "spritecache.h"
#include "gfx_func.h"
#include "company_func.h"
#include "date_func.h"
#include "date_gui.h"
#include "vehicle_gui.h"
#include "settings_type.h"
#include "viewport_func.h"
#include "zoom_func.h"

#include <vector>
#include <algorithm>

#include "table/strings.h"
#include "table/string_colours.h"
#include "table/sprites.h"

#include "safeguards.h"

enum SchdispatchWidgets {
	WID_SCHDISPATCH_CAPTION,         ///< Caption of window.
	WID_SCHDISPATCH_MATRIX,          ///< Matrix of vehicles.
	WID_SCHDISPATCH_V_SCROLL,        ///< Vertical scrollbar.
    WID_SCHDISPATCH_SUMMARY_PANEL,   ///< Summary panel

	WID_SCHDISPATCH_ENABLED,         ///< Enable button.
	WID_SCHDISPATCH_ADD,             ///< Add Departure Time button
	WID_SCHDISPATCH_SET_DURATION,    ///< Duration button
	WID_SCHDISPATCH_SET_START_DATE,  ///< Start Date button
	WID_SCHDISPATCH_SET_DELAY,       ///< Delat button
	WID_SCHDISPATCH_RESET_DISPATCH,  ///< Reset dispatch button
};

/**
 * Callback for when a time has been chosen to start the schedule
 * @param windex The windows index
 * @param date the actually chosen date
 */
static void SetScheduleStartDateIntl(uint32 windex, DateTicksScaled date)
{
	Date start_date;
	uint16 start_full_date_fract;
	SchdispatchConvertToFullDateFract(date, &start_date, &start_full_date_fract);

	uint32 p1 = 0, p2 = 0;
	SB(p1, 0, 20, windex);
	SB(p1, 20, 12, GB(start_full_date_fract, 2, 12));
	SB(p2, 0, 30, start_date);
	SB(p2, 30, 2, GB(start_full_date_fract, 0, 2));

	DoCommandP(0, p1, p2, CMD_SCHEDULED_DISPATCH_SET_START_DATE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
}

/**
 * Callback for when a time has been chosen to start the schedule
 * @param window the window related to the setting of the date
 * @param date the actually chosen date
 */
static void SetScheduleStartDateCallback(const Window *w, DateTicksScaled date)
{
	SetScheduleStartDateIntl(w->window_number, date);
}

/**
 * Callback for when a time has been chosen to add to the schedule
 * @param p1 The p1 parameter to send to CmdScheduledDispatchAdd
 * @param date the actually chosen date
 */
static void ScheduleAddIntl(uint32 p1, DateTicksScaled date)
{
	VehicleID veh = GB(p1, 0, 20);
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle()) return;

	/* Make sure the time is the closest future to the timetable start */
	DateTicksScaled start_tick = v->orders.list->GetScheduledDispatchStartTick();
	while (date > start_tick) date -= v->orders.list->GetScheduledDispatchDuration();
	while (date < start_tick) date += v->orders.list->GetScheduledDispatchDuration();

	DoCommandP(0, v->index, (uint32)(date - start_tick), CMD_SCHEDULED_DISPATCH_ADD | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
}

/**
 * Callback for when a time has been chosen to add to the schedule
 * @param window the window related to the setting of the date
 * @param date the actually chosen date
 */
static void ScheduleAddCallback(const Window *w, DateTicksScaled date)
{
	ScheduleAddIntl(w->window_number, date);
}

/**
 * Calculate the maximum number of vehicle required to run this timetable according to the dispatch schedule
 * @param timetable_duration  timetable duration in scaled tick
 * @param schedule_duration  scheduled dispatch duration in scaled tick
 * @param offsets list of all dispatch offsets in the schedule
 * @return maxinum number of vehicle required
 */
static int CalculateMaxRequiredVehicle(Ticks timetable_duration, uint32 schedule_duration, std::vector<uint32> offsets)
{
	if (timetable_duration == INVALID_TICKS) return -1;
	if (offsets.size() == 0) return -1;

	/* Number of time required to ensure all vehicle are counted */
	int required_loop = CeilDiv(timetable_duration, schedule_duration) + 1;

	/* Create indice array to count maximum overlapping range */
	std::vector<std::pair<uint32, int>> indices;
	for (int i = 0; i < required_loop; i++) {
		for (uint32 offset : offsets) {
			indices.push_back(std::make_pair(i * schedule_duration + offset, 1));
			indices.push_back(std::make_pair(i * schedule_duration + offset + timetable_duration, -1));
		}
	}
	std::sort(indices.begin(), indices.end());
	int current_count = 0;
	int vehicle_count = 0;
	for (const auto& inc : indices) {
		current_count += inc.second;
		if (current_count > vehicle_count) vehicle_count = current_count;
	}
	return vehicle_count;
}

struct SchdispatchWindow : Window {
	const Vehicle *vehicle; ///< Vehicle monitored by the window.
	int clicked_widget;     ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	Scrollbar *vscroll;     ///< Verticle scrollbar
	uint num_columns;       ///< Number of columns.

	uint item_count = 0;     ///< Number of scheduled item
	bool last_departure_future; ///< True if last departure is currently displayed in the future

	SchdispatchWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			vehicle(Vehicle::Get(window_number))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SCHDISPATCH_V_SCROLL);
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
	}

	~SchdispatchWindow()
	{
		if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	uint base_width;
	uint header_width;
	uint flag_width;
	uint flag_height;

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: {
				uint min_height = 0;

				SetDParamMaxValue(0, _settings_client.gui.time_in_minutes ? 0 : MAX_YEAR * DAYS_IN_YEAR);
				Dimension unumber = GetStringBoundingBox(STR_JUST_DATE_WALLCLOCK_TINY);
				const Sprite *spr = GetSprite(SPR_FLAG_VEH_STOPPED, ST_NORMAL);
				this->flag_width  = UnScaleGUI(spr->width) + WD_FRAMERECT_RIGHT;
				this->flag_height = UnScaleGUI(spr->height);

				min_height = max<uint>(unumber.height + WD_MATRIX_TOP, UnScaleGUI(spr->height));
				this->header_width = this->flag_width + WD_FRAMERECT_LEFT;
				this->base_width = unumber.width + this->header_width + 4;

				resize->height = min_height;
				resize->width = base_width;
				size->width = resize->width * 3;
				size->height = resize->height * 3;

				fill->width = resize->width;
				fill->height = resize->height;
				break;
			}

			case WID_SCHDISPATCH_SUMMARY_PANEL:
				size->height = WD_FRAMERECT_TOP + 4 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	/**
	 * Set proper item_count to number of offsets in the schedule.
	 */
	void CountItem()
	{
		this->item_count = 0;
		if (this->vehicle->orders.list != nullptr) {
			this->item_count = this->vehicle->orders.list->GetScheduledDispatch().size();
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		switch (data) {
			case VIWD_MODIFY_ORDERS:
				if (!gui_scope) break;
				this->ReInit();
				break;
		}
	}

	virtual void OnPaint() override
	{
		const Vehicle *v = this->vehicle;
		CountItem();

		if (v->owner == _local_company) {
			this->SetWidgetDisabledState(WID_SCHDISPATCH_ENABLED, HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION));
			this->SetWidgetDisabledState(WID_SCHDISPATCH_ADD, !HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->orders.list != nullptr);
			this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_DURATION, !HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->orders.list != nullptr);
			this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_START_DATE, !HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->orders.list != nullptr);
		} else {
			this->DisableWidget(WID_SCHDISPATCH_ENABLED);
			this->DisableWidget(WID_SCHDISPATCH_ADD);
			this->DisableWidget(WID_SCHDISPATCH_SET_DURATION);
			this->DisableWidget(WID_SCHDISPATCH_SET_START_DATE);
		}

		this->vscroll->SetCount(CeilDiv(this->item_count, this->num_columns));

		this->SetWidgetLoweredState(WID_SCHDISPATCH_ENABLED, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
		this->DrawWidgets();
	}

	virtual void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_SCHDISPATCH_CAPTION: SetDParam(0, this->vehicle->index); break;
		}
	}

	/**
	 * Draw a time in the box with the top left corner at x,y.
	 * @param time  Time to draw.
	 * @param left  Left side of the box to draw in.
	 * @param right Right side of the box to draw in.
	 * @param y     Top of the box to draw in.
	 */
	void DrawScheduledTime(const DateTicksScaled time, int left, int right, int y, TextColour colour) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		uint diff_x, diff_y;
		diff_x = this->flag_width + WD_FRAMERECT_LEFT;
		diff_y = (this->resize.step_height - this->flag_height) / 2 - 2;

		int text_left  = rtl ? right - this->base_width - 1 : left + diff_x;
		int text_right = rtl ? right - diff_x : left + this->base_width - 1;

		DrawSprite(SPR_FLAG_VEH_STOPPED, PAL_NONE, rtl ? right - this->flag_width : left + WD_FRAMERECT_LEFT, y + diff_y);

		SetDParam(0, time);
		DrawString(text_left, text_right, y + 2, STR_JUST_DATE_WALLCLOCK_TINY, colour);
	}

	virtual void OnGameTick() override
	{
		const Vehicle *v = this->vehicle;
		if (HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) && v->orders.list != nullptr) {
			if (((v->orders.list->GetScheduledDispatchStartTick() + v->orders.list->GetScheduledDispatchLastDispatch()) > _scaled_date_ticks) != this->last_departure_future) {
				SetWidgetDirty(WID_SCHDISPATCH_SUMMARY_PANEL);
			}
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		const Vehicle *v = this->vehicle;

		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: {
				/* If order is not initialized, don't draw */
				if (v->orders.list == nullptr) break;

				bool rtl = _current_text_dir == TD_RTL;

				/* Set the row and number of boxes in each row based on the number of boxes drawn in the matrix */
				const NWidgetCore *wid = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
				uint16 rows_in_display = wid->current_y / wid->resize_y;

				uint16 num = this->vscroll->GetPosition() * this->num_columns;
				int maxval = min(this->item_count, num + (rows_in_display * this->num_columns));
				int y;

				auto current_schedule = v->orders.list->GetScheduledDispatch().begin();
				DateTicksScaled start_tick = v->orders.list->GetScheduledDispatchStartTick();
				DateTicksScaled end_tick = v->orders.list->GetScheduledDispatchStartTick() + v->orders.list->GetScheduledDispatchDuration();

				for (y = r.top + 1; num < maxval; y += this->resize.step_height) { /* Draw the rows */
					for (byte i = 0; i < this->num_columns && num < maxval; i++, num++) {
						/* Draw all departure time in the current row */
						if (current_schedule != v->orders.list->GetScheduledDispatch().end()) {
							int x = r.left + (rtl ? (this->num_columns - i - 1) : i) * this->resize.step_width;
							DateTicksScaled draw_time = start_tick + *current_schedule;
							this->DrawScheduledTime(draw_time, x, x + this->resize.step_width - 1, y, draw_time >= end_tick ? TC_RED : TC_BLACK);
							current_schedule++;
						} else {
							break;
						}
					}
				}
				break;
			}

			case WID_SCHDISPATCH_SUMMARY_PANEL: {

				int y = r.top + WD_FRAMERECT_TOP;

				if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) || v->orders.list == nullptr) {
					y += FONT_HEIGHT_NORMAL;
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_SCHDISPATCH_SUMMARY_NOT_ENABLED);
				} else {

					const DateTicksScaled last_departure = v->orders.list->GetScheduledDispatchStartTick() + v->orders.list->GetScheduledDispatchLastDispatch();
					SetDParam(0, last_departure);
					const_cast<SchdispatchWindow*>(this)->last_departure_future = (last_departure > _scaled_date_ticks);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y,
							this->last_departure_future ? STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_FUTURE : STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_PAST);
					y += FONT_HEIGHT_NORMAL;

					bool have_conditional = false;
					for (int n = 0; n < v->GetNumOrders(); n++) {
						const Order *order = v->GetOrder(n);
						if (order->IsType(OT_CONDITIONAL)) {
							have_conditional = true;
						}
					}
					if (!have_conditional) {
						const int required_vehicle = CalculateMaxRequiredVehicle(v->orders.list->GetTimetableTotalDuration(), v->orders.list->GetScheduledDispatchDuration(), v->orders.list->GetScheduledDispatch());
						if (required_vehicle > 0) {
							SetDParam(0, required_vehicle);
							DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_SCHDISPATCH_SUMMARY_L1);
						}
					}
					y += FONT_HEIGHT_NORMAL;

					SetTimetableParams(0, v->orders.list->GetScheduledDispatchDuration());
					SetDParam(4, v->orders.list->GetScheduledDispatchStartTick());
					SetDParam(5, v->orders.list->GetScheduledDispatchStartTick() + v->orders.list->GetScheduledDispatchDuration());
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_SCHDISPATCH_SUMMARY_L2);
					y += FONT_HEIGHT_NORMAL;

					SetTimetableParams(0, v->orders.list->GetScheduledDispatchDelay());
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_SCHDISPATCH_SUMMARY_L3);
				}

				break;
			}
		}
	}

	/**
	 * Handle click in the departure time matrix.
	 * @param x Horizontal position in the matrix widget in pixels.
	 * @param y Vertical position in the matrix widget in pixels.
	 */
	void TimeClick(int x, int y)
	{
		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
		/* In case of RTL the widgets are swapped as a whole */
		if (_current_text_dir == TD_RTL) x = matrix_widget->current_x - x;

		uint xt = 0, xm = 0;
		xt = x / this->resize.step_width;
		xm = x % this->resize.step_width;
		if (xt >= this->num_columns) return;

		uint row = y / this->resize.step_height;
		if (row >= this->vscroll->GetCapacity()) return;

		uint pos = ((row + this->vscroll->GetPosition()) * this->num_columns) + xt;

		if (pos >= this->item_count) return;

		if (xm <= this->header_width) {
			DoCommandP(0, this->vehicle->index, this->vehicle->orders.list->GetScheduledDispatch()[pos], CMD_SCHEDULED_DISPATCH_REMOVE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		const Vehicle *v = this->vehicle;

		this->clicked_widget = widget;
		this->DeleteChildWindows(WC_QUERY_STRING);

		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: { /* List */
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_SCHDISPATCH_MATRIX);
				this->TimeClick(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				break;
			}

			case WID_SCHDISPATCH_ENABLED: {
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) SetBit(p2, 0);

				if (!v->orders.list->IsScheduledDispatchValid()) v->orders.list->ResetScheduledDispatch();
				DoCommandP(0, v->index, p2, CMD_SCHEDULED_DISPATCH | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_SCHDISPATCH_ADD: {
				if (_settings_client.gui.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					ShowQueryString(STR_EMPTY, STR_SCHDISPATCH_ADD_CAPTION, 31, this, CS_NUMERAL, QSF_NONE);
				} else {
					ShowSetDateWindow(this, v->index, _scaled_date_ticks, _cur_year, _cur_year + 15, ScheduleAddCallback);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				SetDParam(0, RoundDivSU(v->orders.list->GetScheduledDispatchDuration(), _settings_client.gui.ticks_per_minute ? _settings_client.gui.ticks_per_minute : DAY_TICKS));
				ShowQueryString(STR_JUST_INT, _settings_client.gui.time_in_minutes ? STR_SCHDISPATCH_DURATION_CAPTION_MINUTE : STR_SCHDISPATCH_DURATION_CAPTION_DAY, 31, this, CS_NUMERAL, QSF_NONE);
				break;
			}

			case WID_SCHDISPATCH_SET_START_DATE: {
				if (_settings_client.gui.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					uint64 time = _scaled_date_ticks;
					time /= _settings_client.gui.ticks_per_minute;
					time += _settings_client.gui.clock_offset;
					time %= (24 * 60);
					time = (time % 60) + (((time / 60) % 24) * 100);
					SetDParam(0, time);
					ShowQueryString(STR_JUST_INT, STR_SCHDISPATCH_START_CAPTION_MINUTE, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				} else {
					ShowSetDateWindow(this, v->index, _scaled_date_ticks, _cur_year, _cur_year + 15, SetScheduleStartDateCallback);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				SetDParam(0, RoundDivSU(v->orders.list->GetScheduledDispatchDelay(), _settings_client.gui.ticks_per_minute ? _settings_client.gui.ticks_per_minute : DAY_TICKS));
				ShowQueryString(STR_JUST_INT, _settings_client.gui.time_in_minutes ? STR_SCHDISPATCH_DELAY_CAPTION_MINUTE : STR_SCHDISPATCH_DELAY_CAPTION_DAY, 31, this, CS_NUMERAL, QSF_NONE);
				break;
			}

			case WID_SCHDISPATCH_RESET_DISPATCH: {
				DoCommandP(0, v->index, 0, CMD_SCHEDULED_DISPATCH_RESET_LAST_DISPATCH | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}
		}

		this->SetDirty();
	}

	virtual void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;
		const Vehicle *v = this->vehicle;

		switch (this->clicked_widget) {
			default: NOT_REACHED();

			case WID_SCHDISPATCH_ADD: {
				char *end;
				int32 val = StrEmpty(str) ? -1 : strtoul(str, &end, 10);

				if (val >= 0 && end && *end == 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					DateTicksScaled slot = MINUTES_DATE(MINUTES_DAY(CURRENT_MINUTE), hours, minutes);
					slot -= _settings_client.gui.clock_offset;
					slot *= _settings_client.gui.ticks_per_minute;
					ScheduleAddIntl(v->index, slot);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_START_DATE: {
				char *end;
				int32 val = StrEmpty(str) ? -1 : strtoul(str, &end, 10);

				if (val >= 0 && end && *end == 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					DateTicksScaled start = MINUTES_DATE(MINUTES_DAY(CURRENT_MINUTE), hours, minutes);
					start -= _settings_client.gui.clock_offset;
					start *= _settings_client.gui.ticks_per_minute;
					SetScheduleStartDateIntl(v->index, start);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				int32 val = StrEmpty(str) ? 0 : strtoul(str, nullptr, 10);

				if (val > 0) {
					if (_settings_client.gui.time_in_minutes) {
						val *= _settings_client.gui.ticks_per_minute;
					} else {
						val *= DAY_TICKS;
					}

					DoCommandP(0, v->index, val, CMD_SCHEDULED_DISPATCH_SET_DURATION | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				char *end;
				int32 val = StrEmpty(str) ? -1 : strtoul(str, &end, 10);

				if (val >= 0 && end && *end == 0) {
					if (_settings_client.gui.time_in_minutes) {
						val *= _settings_client.gui.ticks_per_minute;
					} else {
						val *= DAY_TICKS;
					}

					DoCommandP(0, v->index, val, CMD_SCHEDULED_DISPATCH_SET_DELAY | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				}
				break;
			}
		}

		this->SetDirty();
	}

	virtual void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SCHDISPATCH_MATRIX);
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
		this->num_columns = nwi->current_x / nwi->resize_x;
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			MarkAllRoutePathsDirty(this->vehicle);
			MarkAllRouteStepsDirty(this->vehicle);
		}
	}

	const Vehicle *GetVehicle()
	{
		return this->vehicle;
	}
};

static const NWidgetPart _nested_schdispatch_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCHDISPATCH_CAPTION), SetDataTip(STR_SCHDISPATCH_CAPTION, STR_NULL),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_SCHDISPATCH_MATRIX), SetResize(1, 1), SetScrollbar(WID_SCHDISPATCH_V_SCROLL),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SCHDISPATCH_V_SCROLL),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_SCHDISPATCH_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(NWID_VERTICAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ENABLED), SetDataTip(STR_SCHDISPATCH_ENABLED, STR_SCHDISPATCH_ENABLED_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADD), SetDataTip(STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
		EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DURATION), SetDataTip(STR_SCHDISPATCH_DURATION, STR_SCHDISPATCH_DURATION_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_START_DATE), SetDataTip(STR_SCHDISPATCH_START, STR_SCHDISPATCH_START_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
		EndContainer(),
		NWidget(NWID_VERTICAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DELAY), SetDataTip(STR_SCHDISPATCH_DELAY, STR_SCHDISPATCH_DELAY_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_RESET_DISPATCH), SetDataTip(STR_SCHDISPATCH_RESET_LAST_DISPATCH, STR_SCHDISPATCH_RESET_LAST_DISPATCH_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _schdispatch_desc(
	WDP_AUTO, "scheduled_dispatch_slots", 400, 130,
	WC_SCHDISPATCH_SLOTS, WC_VEHICLE_TIMETABLE,
	WDF_CONSTRUCTION,
	_nested_schdispatch_widgets, lengthof(_nested_schdispatch_widgets)
);

/**
 * Show the slot dispatching slots
 * @param v The vehicle to show the slot dispatching slots for
 */
void ShowSchdispatchWindow(const Vehicle *v)
{
	AllocateWindowDescFront<SchdispatchWindow>(&_schdispatch_desc, v->index);
}
