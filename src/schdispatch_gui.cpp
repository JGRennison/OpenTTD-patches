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
#include "company_base.h"
#include "company_func.h"
#include "date_func.h"
#include "date_gui.h"
#include "vehicle_gui.h"
#include "settings_type.h"
#include "viewport_func.h"
#include "zoom_func.h"
#include "core/geometry_func.hpp"
#include "tilehighlight_func.h"

#include <vector>
#include <algorithm>

#include "table/strings.h"
#include "table/string_colours.h"
#include "table/sprites.h"

#include "safeguards.h"

enum SchdispatchWidgets {
	WID_SCHDISPATCH_CAPTION,         ///< Caption of window.
	WID_SCHDISPATCH_RENAME,          ///< Rename button.
	WID_SCHDISPATCH_MOVE_LEFT,       ///< Move current schedule left (-1).
	WID_SCHDISPATCH_MOVE_RIGHT,      ///< Move current schedule right (+1).
	WID_SCHDISPATCH_MATRIX,          ///< Matrix of vehicles.
	WID_SCHDISPATCH_V_SCROLL,        ///< Vertical scrollbar.
	WID_SCHDISPATCH_SUMMARY_PANEL,   ///< Summary panel

	WID_SCHDISPATCH_ENABLED,         ///< Enable button.
	WID_SCHDISPATCH_HEADER,          ///< Header text.
	WID_SCHDISPATCH_PREV,            ///< Previous schedule.
	WID_SCHDISPATCH_NEXT,            ///< Next schedule.
	WID_SCHDISPATCH_ADD_SCHEDULE,    ///< Add schedule.

	WID_SCHDISPATCH_ADD,             ///< Add Departure Time button
	WID_SCHDISPATCH_SET_DURATION,    ///< Duration button
	WID_SCHDISPATCH_SET_START_DATE,  ///< Start Date button
	WID_SCHDISPATCH_SET_DELAY,       ///< Delay button
	WID_SCHDISPATCH_MANAGEMENT,      ///< Management button
	WID_SCHDISPATCH_ADJUST,          ///< Adjust departure times
};

/**
 * Callback for when a time has been chosen to start the schedule
 * @param p1 The p1 parameter to send to CmdScheduledDispatchSetStartDate
 * @param date the actually chosen date
 */
static void SetScheduleStartDateIntl(uint32 p1, DateTicksScaled date)
{
	DoCommandPEx(0, p1, 0, (uint64)date.base(), CMD_SCHEDULED_DISPATCH_SET_START_DATE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), nullptr, nullptr, 0);
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
static void ScheduleAddIntl(uint32 p1, DateTicksScaled date, uint extra_slots, uint offset, bool wrap_mode = false)
{
	VehicleID veh = GB(p1, 0, 20);
	uint schedule_index = GB(p1, 20, 12);
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return;

	const DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);

	/* Make sure the time is the closest future to the timetable start */
	DateTicksScaled start_tick = ds.GetScheduledDispatchStartTick();
	uint32 duration = ds.GetScheduledDispatchDuration();
	while (date > start_tick) date -= duration;
	while (date < start_tick) date += duration;

	if (extra_slots > 0 && offset > 0 && !wrap_mode) {
		DateTicksScaled end_tick = start_tick + duration;
		int64 max_extra_slots = (end_tick - 1 - date).base() / offset;
		if (max_extra_slots < extra_slots) extra_slots = static_cast<uint>(std::max<int64>(0, max_extra_slots));
		extra_slots = std::min<uint>(extra_slots, UINT16_MAX);
	}

	DoCommandPEx(0, p1, (uint32)(date - start_tick).base(), (((uint64)extra_slots) << 32) | offset, CMD_SCHEDULED_DISPATCH_ADD | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), nullptr, nullptr, 0);
}

/**
 * Callback for when a time has been chosen to add to the schedule
 * @param window the window related to the setting of the date
 * @param date the actually chosen date
 */
static void ScheduleAddCallback(const Window *w, DateTicksScaled date)
{
	ScheduleAddIntl(w->window_number, date, 0, 0);
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
			if (offset >= schedule_duration) continue;
			indices.push_back(std::make_pair(i * schedule_duration + offset, 1));
			indices.push_back(std::make_pair(i * schedule_duration + offset + timetable_duration, -1));
		}
	}
	if (indices.empty()) return -1;
	std::sort(indices.begin(), indices.end());
	int current_count = 0;
	int vehicle_count = 0;
	for (const auto& inc : indices) {
		current_count += inc.second;
		if (current_count > vehicle_count) vehicle_count = current_count;
	}
	return vehicle_count;
}

static void AddNewScheduledDispatchSchedule(VehicleID vindex)
{
	DateTicksScaled start_tick;
	uint32 duration;

	const Company *c = Company::GetIfValid(_local_company);
	if (c != nullptr && c->settings.default_sched_dispatch_duration != 0) {
		/* Use duration from setting, set start time to be integer multiple of duration */

		const TickMinutes now = _settings_time.NowInTickMinutes();
		start_tick = _settings_time.FromTickMinutes(now - (now.base() % c->settings.default_sched_dispatch_duration));

		duration = c->settings.default_sched_dispatch_duration * _settings_time.ticks_per_minute;
	} else if (_settings_time.time_in_minutes) {
		/* Set to 00:00 of today, and 1 day */

		start_tick = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, 0));

		duration = 24 * 60 * _settings_time.ticks_per_minute;
	} else {
		/* Set Jan 1st and 365 day */
		start_tick = DateToScaledDateTicks(DateAtStartOfYear(_cur_year));
		duration = 365 * DAY_TICKS;
	}

	DoCommandPEx(0, vindex, duration, (uint64)start_tick.base(), CMD_SCHEDULED_DISPATCH_ADD_NEW_SCHEDULE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), CcAddNewSchDispatchSchedule, nullptr, 0);
}

struct SchdispatchWindow : GeneralVehicleWindow {
	int schedule_index;
	int clicked_widget;     ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	Scrollbar *vscroll;     ///< Verticle scrollbar
	uint num_columns;       ///< Number of columns.

	uint item_count = 0;     ///< Number of scheduled item
	bool last_departure_future; ///< True if last departure is currently displayed in the future
	uint warning_count = 0;
	bool no_order_warning_pad = false;

	SchdispatchWindow(WindowDesc *desc, WindowNumber window_number) :
			GeneralVehicleWindow(desc, Vehicle::Get(window_number))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SCHDISPATCH_V_SCROLL);
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
		this->schedule_index = -1;
		this->AutoSelectSchedule();
	}

	void Close(int data = 0) override
	{
		if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
		this->GeneralVehicleWindow::Close();
	}

	uint base_width;
	uint header_width;
	uint flag_width;
	uint flag_height;

	enum ManagementDropdown {
		SCH_MD_RESET_LAST_DISPATCHED,
		SCH_MD_CLEAR_SCHEDULE,
		SCH_MD_REMOVE_SCHEDULE,
		SCH_MD_DUPLICATE_SCHEDULE,
		SCH_MD_APPEND_VEHICLE_SCHEDULES,
	};

	bool IsScheduleSelected() const
	{
		return this->vehicle->orders != nullptr && this->schedule_index >= 0 && (uint)this->schedule_index < this->vehicle->orders->GetScheduledDispatchScheduleCount();
	}

	void AutoSelectSchedule()
	{
		if (!this->IsScheduleSelected()) {
			if (this->vehicle->orders != nullptr && this->vehicle->orders->GetScheduledDispatchScheduleCount() > 0) {
				this->schedule_index = Clamp<int>(this->schedule_index, 0, this->vehicle->orders->GetScheduledDispatchScheduleCount() - 1);
			} else {
				this->schedule_index = -1;
			}
		}
	}

	const DispatchSchedule &GetSelectedSchedule() const
	{
		return this->vehicle->orders->GetDispatchScheduleByIndex(this->schedule_index);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: {
				uint min_height = 0;

				SetDParamMaxValue(0, _settings_time.time_in_minutes ? 0 : MAX_YEAR * DAYS_IN_YEAR);
				Dimension unumber = GetStringBoundingBox(STR_JUST_DATE_WALLCLOCK_TINY);
				const Sprite *spr = GetSprite(SPR_FLAG_VEH_STOPPED, SpriteType::Normal, ZoomMask(ZOOM_LVL_GUI));
				this->flag_width  = UnScaleGUI(spr->width) + WidgetDimensions::scaled.framerect.right;
				this->flag_height = UnScaleGUI(spr->height);

				min_height = std::max<uint>(unumber.height + WidgetDimensions::scaled.matrix.top, UnScaleGUI(spr->height));
				this->header_width = this->flag_width + WidgetDimensions::scaled.framerect.left;
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
				size->height = 6 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical();
				uint warning_count = this->warning_count;
				if (this->no_order_warning_pad) {
					warning_count++;
					size->height -= GetCharacterHeight(FS_NORMAL);
				}
				if (warning_count > 0) {
					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					size->height += warning_count * std::max<int>(warning_dimensions.height, GetCharacterHeight(FS_NORMAL));
				}
				break;
		}
	}

	/**
	 * Set proper item_count to number of offsets in the schedule.
	 */
	void CountItem()
	{
		this->item_count = 0;
		if (this->IsScheduleSelected()) {
			this->item_count = (uint)this->GetSelectedSchedule().GetScheduledDispatch().size();
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
				this->AutoSelectSchedule();
				this->ReInit();
				break;
		}
	}

	virtual void OnPaint() override
	{
		const Vehicle *v = this->vehicle;
		CountItem();

		bool unusable = (v->owner != _local_company) || (v->orders == nullptr);

		this->SetWidgetDisabledState(WID_SCHDISPATCH_ENABLED, unusable || HasBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION));

		this->SetWidgetDisabledState(WID_SCHDISPATCH_RENAME, unusable || v->orders->GetScheduledDispatchScheduleCount() == 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_PREV, v->orders == nullptr || this->schedule_index <= 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_NEXT, v->orders == nullptr || this->schedule_index >= (int)(v->orders->GetScheduledDispatchScheduleCount() - 1));
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MOVE_LEFT, v->orders == nullptr || this->schedule_index <= 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MOVE_RIGHT, v->orders == nullptr || this->schedule_index >= (int)(v->orders->GetScheduledDispatchScheduleCount() - 1));
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADD_SCHEDULE, unusable || v->orders->GetScheduledDispatchScheduleCount() >= 4096);

		bool disabled = unusable || !HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)  || !this->IsScheduleSelected();
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADD, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_DURATION, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_START_DATE, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_DELAY, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MANAGEMENT, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADJUST, disabled || this->GetSelectedSchedule().GetScheduledDispatch().empty());

		this->vscroll->SetCount(CeilDiv(this->item_count, this->num_columns));

		this->SetWidgetLoweredState(WID_SCHDISPATCH_ENABLED, HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH));
		this->DrawWidgets();
	}

	virtual void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_SCHDISPATCH_CAPTION:
				SetDParam(0, this->vehicle->index);
				break;

			case WID_SCHDISPATCH_HEADER:
				if (this->IsScheduleSelected()) {
					const DispatchSchedule &ds = this->GetSelectedSchedule();
					if (ds.ScheduleName().empty()) {
						SetDParam(0, STR_SCHDISPATCH_SCHEDULE_ID);
						SetDParam(1, this->schedule_index + 1);
						SetDParam(2, this->vehicle->orders->GetScheduledDispatchScheduleCount());
					} else {
						SetDParam(0, STR_SCHDISPATCH_NAMED_SCHEDULE_ID);
						SetDParamStr(1, ds.ScheduleName().c_str());
						SetDParam(2, this->schedule_index + 1);
						SetDParam(3, this->vehicle->orders->GetScheduledDispatchScheduleCount());
					}
				} else {
					SetDParam(0, STR_SCHDISPATCH_NO_SCHEDULES);
				}
				break;
		}
	}

	virtual bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_ADD: {
				if (_settings_time.time_in_minutes) {
					SetDParam(0, STR_SCHDISPATCH_ADD_TOOLTIP);
					GuiShowTooltips(this, STR_SCHDISPATCH_ADD_TOOLTIP_EXTRA, close_cond, 1);
					return true;
				}
				break;
			}

			case WID_SCHDISPATCH_MANAGEMENT: {
				_temp_special_strings[0] = GetString(STR_SCHDISPATCH_RESET_LAST_DISPATCH_TOOLTIP);
				auto add_suffix = [&](StringID str) {
					SetDParam(0, str);
					_temp_special_strings[0] += GetString(STR_SCHDISPATCH_MANAGE_TOOLTIP_SUFFIX);
				};
				add_suffix(STR_SCHDISPATCH_CLEAR_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_REMOVE_SCHEDULE_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_DUPLICATE_SCHEDULE_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_APPEND_VEHICLE_SCHEDULES_TOOLTIP);
				GuiShowTooltips(this, SPECSTR_TEMP_START, close_cond);
				return true;
			}

			default:
				break;
		}

		return false;
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
		diff_x = this->flag_width + WidgetDimensions::scaled.framerect.left;
		diff_y = (this->resize.step_height - this->flag_height) / 2 - 2;

		int text_left  = rtl ? right - this->base_width - 1 : left + diff_x;
		int text_right = rtl ? right - diff_x : left + this->base_width - 1;

		DrawSprite(SPR_FLAG_VEH_STOPPED, PAL_NONE, rtl ? right - this->flag_width : left + WidgetDimensions::scaled.framerect.left, y + diff_y);

		SetDParam(0, time);
		DrawString(text_left, text_right, y + 2, STR_JUST_DATE_WALLCLOCK_TINY, colour);
	}

	virtual void OnGameTick() override
	{
		if (HasBit(this->vehicle->vehicle_flags, VF_SCHEDULED_DISPATCH) && this->IsScheduleSelected()) {
			const DispatchSchedule &ds = this->GetSelectedSchedule();
			if (((ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchLastDispatch()) > _scaled_date_ticks) != this->last_departure_future) {
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
				if (!this->IsScheduleSelected()) break;

				bool rtl = _current_text_dir == TD_RTL;

				/* Set the row and number of boxes in each row based on the number of boxes drawn in the matrix */
				const NWidgetCore *wid = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
				const uint16 rows_in_display = wid->current_y / wid->resize_y;

				const DispatchSchedule &ds = this->GetSelectedSchedule();

				uint num = this->vscroll->GetPosition() * this->num_columns;
				if (num >= ds.GetScheduledDispatch().size()) break;

				const uint maxval = std::min<uint>(this->item_count, num + (rows_in_display * this->num_columns));

				auto current_schedule = ds.GetScheduledDispatch().begin() + num;
				const DateTicksScaled start_tick = ds.GetScheduledDispatchStartTick();
				const DateTicksScaled end_tick = ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchDuration();

				for (int y = r.top + 1; num < maxval; y += this->resize.step_height) { /* Draw the rows */
					for (uint i = 0; i < this->num_columns && num < maxval; i++, num++) {
						/* Draw all departure time in the current row */
						if (current_schedule != ds.GetScheduledDispatch().end()) {
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
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				int y = ir.top;

				if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH) || !this->IsScheduleSelected()) {
					y += GetCharacterHeight(FS_NORMAL);
					DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_NOT_ENABLED);
				} else {
					const DispatchSchedule &ds = this->GetSelectedSchedule();

					auto draw_warning = [&](StringID text) {
						const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
						int step_height = std::max<int>(warning_dimensions.height, GetCharacterHeight(FS_NORMAL));
						int left = ir.left;
						int right = ir.right;
						const bool rtl = (_current_text_dir == TD_RTL);
						DrawSprite(SPR_WARNING_SIGN, 0, rtl ? right - warning_dimensions.width - 5 : left + 5, y + (step_height - warning_dimensions.height) / 2);
						if (rtl) {
							right -= (warning_dimensions.width + 10);
						} else {
							left += (warning_dimensions.width + 10);
						}
						DrawString(left, right, y + (step_height - GetCharacterHeight(FS_NORMAL)) / 2, text);
						y += step_height;
					};

					bool have_conditional = false;
					int schedule_order_index = -1;
					for (int n = 0; n < v->GetNumOrders(); n++) {
						const Order *order = v->GetOrder(n);
						if (order->IsType(OT_CONDITIONAL)) {
							have_conditional = true;
						}
						if (order->GetDispatchScheduleIndex() == this->schedule_index) {
							schedule_order_index = n;
						}
					}
					bool no_order_warning_pad = false;
					if (schedule_order_index < 0) {
						draw_warning(STR_SCHDISPATCH_NOT_ASSIGNED_TO_ORDER);
						no_order_warning_pad = true;
					} else {
						const Order *order = v->GetOrder(schedule_order_index);
						SetDParam(0, schedule_order_index + 1);

						switch (order->GetType()) {
							case OT_GOTO_STATION:
								SetDParam(1, STR_STATION_NAME);
								SetDParam(2, order->GetDestination());
								break;

							case OT_GOTO_WAYPOINT:
								SetDParam(1, STR_WAYPOINT_NAME);
								SetDParam(2, order->GetDestination());
								break;

							case OT_GOTO_DEPOT:
								if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
									if (v->type == VEH_AIRCRAFT) {
										SetDParam(1, STR_ORDER_GO_TO_NEAREST_HANGAR);
									} else {
										SetDParam(1, STR_ORDER_GO_TO_NEAREST_DEPOT);
									}
								} else {
									SetDParam(1, STR_DEPOT_NAME);
									SetDParam(2, v->type);
									SetDParam(3, order->GetDestination());
								}
								break;

							default:
								SetDParam(1, STR_INVALID_ORDER);
								break;
						}

						DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_ASSIGNED_TO_ORDER);
						y += GetCharacterHeight(FS_NORMAL);
					}

					const DateTicksScaled last_departure = ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchLastDispatch();
					SetDParam(0, last_departure);
					const_cast<SchdispatchWindow*>(this)->last_departure_future = (last_departure > _scaled_date_ticks);
					DrawString(ir.left, ir.right, y,
							this->last_departure_future ? STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_FUTURE : STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_PAST);
					y += GetCharacterHeight(FS_NORMAL);

					if (!have_conditional) {
						const int required_vehicle = CalculateMaxRequiredVehicle(v->orders->GetTimetableTotalDuration(), ds.GetScheduledDispatchDuration(), ds.GetScheduledDispatch());
						if (required_vehicle > 0) {
							SetDParam(0, required_vehicle);
							DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_L1);
						}
					}
					y += GetCharacterHeight(FS_NORMAL);

					SetTimetableParams(0, ds.GetScheduledDispatchDuration(), true);
					DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_L2);
					y += GetCharacterHeight(FS_NORMAL);

					SetDParam(0, ds.GetScheduledDispatchStartTick());
					SetDParam(1, ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchDuration());
					DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_L3);
					y += GetCharacterHeight(FS_NORMAL);

					SetTimetableParams(0, ds.GetScheduledDispatchDelay());
					DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_L4);
					y += GetCharacterHeight(FS_NORMAL);

					uint32 duration = ds.GetScheduledDispatchDuration();
					uint warnings = 0;
					for (uint32 slot : ds.GetScheduledDispatch()) {
						if (slot >= duration) {
							draw_warning(STR_SCHDISPATCH_SLOT_OUTSIDE_SCHEDULE);
							warnings++;
							break;
						}
					}

					if (warnings != this->warning_count || no_order_warning_pad != this->no_order_warning_pad) {
						SchdispatchWindow *mutable_this = const_cast<SchdispatchWindow *>(this);
						mutable_this->warning_count = warnings;
						mutable_this->no_order_warning_pad = no_order_warning_pad;
						mutable_this->ReInit();
					}
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
		if (!this->IsScheduleSelected()) return;

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

		const DispatchSchedule &ds = this->GetSelectedSchedule();

		if (pos >= this->item_count || pos >= ds.GetScheduledDispatch().size()) return;

		if (xm <= this->header_width) {
			DoCommandP(0, this->vehicle->index | (this->schedule_index << 20), ds.GetScheduledDispatch()[pos], CMD_SCHEDULED_DISPATCH_REMOVE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
		}
	}

	int32 ProcessDurationForQueryString(int32 duration) const
	{
		if (!_settings_client.gui.timetable_in_ticks) duration = RoundDivSU(duration, DATE_UNIT_SIZE);
		return duration;
	}

	int GetQueryStringCaptionOffset() const
	{
		if (_settings_client.gui.timetable_in_ticks) return 2;
		if (_settings_time.time_in_minutes) return 0;
		return 1;
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		const Vehicle *v = this->vehicle;

		this->clicked_widget = widget;
		this->CloseChildWindows(WC_QUERY_STRING);

		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: { /* List */
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_SCHDISPATCH_MATRIX);
				this->TimeClick(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				break;
			}

			case WID_SCHDISPATCH_ENABLED: {
				uint32 p2 = 0;
				if (!HasBit(v->vehicle_flags, VF_SCHEDULED_DISPATCH)) SetBit(p2, 0);

				DoCommandP(0, v->index, p2, CMD_SCHEDULED_DISPATCH | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				if (HasBit(p2, 0) && this->vehicle->orders != nullptr && this->vehicle->orders->GetScheduledDispatchScheduleCount() == 0) {
					AddNewScheduledDispatchSchedule(v->index);
				}
				break;
			}

			case WID_SCHDISPATCH_ADD: {
				if (!this->IsScheduleSelected()) break;
				if (_settings_time.time_in_minutes && _ctrl_pressed) {
					void ShowScheduledDispatchAddSlotsWindow(SchdispatchWindow *parent, int window_number);
					ShowScheduledDispatchAddSlotsWindow(this, v->index);
				} else if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					ShowQueryString(STR_EMPTY, STR_SCHDISPATCH_ADD_CAPTION, 31, this, CS_NUMERAL, QSF_NONE);
				} else {
					ShowSetDateWindow(this, v->index | (this->schedule_index << 20), _scaled_date_ticks, _cur_year, _cur_year + 15, ScheduleAddCallback, STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				SetDParam(0, ProcessDurationForQueryString(this->GetSelectedSchedule().GetScheduledDispatchDuration()));
				ShowQueryString(STR_JUST_INT, STR_SCHDISPATCH_DURATION_CAPTION_MINUTE + this->GetQueryStringCaptionOffset(), 31, this, charset_filter, QSF_NONE);
				break;
			}

			case WID_SCHDISPATCH_SET_START_DATE: {
				if (!this->IsScheduleSelected()) break;
				if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					SetDParam(0, _settings_time.NowInTickMinutes().ClockHHMM());
					ShowQueryString(STR_JUST_INT, STR_SCHDISPATCH_START_CAPTION_MINUTE, 31, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
				} else {
					ShowSetDateWindow(this, v->index | (this->schedule_index << 20), _scaled_date_ticks, _cur_year, _cur_year + 15, SetScheduleStartDateCallback, STR_SCHDISPATCH_SET_START, STR_SCHDISPATCH_START_TOOLTIP);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				SetDParam(0, ProcessDurationForQueryString(this->GetSelectedSchedule().GetScheduledDispatchDelay()));
				ShowQueryString(STR_JUST_INT, STR_SCHDISPATCH_DELAY_CAPTION_MINUTE + this->GetQueryStringCaptionOffset(), 31, this, charset_filter, QSF_NONE);
				break;
			}

			case WID_SCHDISPATCH_MANAGEMENT: {
				if (!this->IsScheduleSelected()) break;
				DropDownList list;
				auto add_item = [&](StringID string, int result) {
					std::unique_ptr<DropDownListStringItem> item(new DropDownListStringItem(string, result, false));
					item->SetColourFlags(TC_FORCED);
					list.emplace_back(std::move(item));
				};
				add_item(STR_SCHDISPATCH_RESET_LAST_DISPATCH, SCH_MD_RESET_LAST_DISPATCHED);
				add_item(STR_SCHDISPATCH_CLEAR, SCH_MD_CLEAR_SCHEDULE);
				add_item(STR_SCHDISPATCH_REMOVE_SCHEDULE, SCH_MD_REMOVE_SCHEDULE);
				add_item(STR_SCHDISPATCH_DUPLICATE_SCHEDULE, SCH_MD_DUPLICATE_SCHEDULE);
				add_item(STR_SCHDISPATCH_APPEND_VEHICLE_SCHEDULES, SCH_MD_APPEND_VEHICLE_SCHEDULES);
				ShowDropDownList(this, std::move(list), -1, WID_SCHDISPATCH_MANAGEMENT);
				break;
			}

			case WID_SCHDISPATCH_PREV:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index > 0) this->schedule_index--;
				this->ReInit();
				break;

			case WID_SCHDISPATCH_NEXT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index < (int)(this->vehicle->orders->GetScheduledDispatchScheduleCount() - 1)) this->schedule_index++;
				this->ReInit();
				break;

			case WID_SCHDISPATCH_ADD_SCHEDULE:
				AddNewScheduledDispatchSchedule(this->vehicle->index);
				break;

			case WID_SCHDISPATCH_RENAME:
				if (!this->IsScheduleSelected()) break;
				SetDParamStr(0, this->GetSelectedSchedule().ScheduleName().c_str());
				ShowQueryString(STR_JUST_RAW_STRING, STR_SCHDISPATCH_RENAME_SCHEDULE_CAPTION,
						MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_SCHDISPATCH_ADJUST: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL_SIGNED : CS_NUMERAL_DECIMAL_SIGNED;
				SetDParam(0, 0);
				ShowQueryString(STR_JUST_INT, STR_SCHDISPATCH_ADJUST_CAPTION_MINUTE + this->GetQueryStringCaptionOffset(), 31, this, charset_filter, QSF_NONE);
				break;
			}

			case WID_SCHDISPATCH_MOVE_LEFT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index > 0) {
					DoCommandP(0, this->vehicle->index, (this->schedule_index - 1) | (this->schedule_index << 16), CMD_SCHEDULED_DISPATCH_SWAP_SCHEDULES | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), CcSwapSchDispatchSchedules);
				}
				break;

			case WID_SCHDISPATCH_MOVE_RIGHT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index < (int)(this->vehicle->orders->GetScheduledDispatchScheduleCount() - 1)) {
					DoCommandP(0, this->vehicle->index, (this->schedule_index + 1) | (this->schedule_index << 16), CMD_SCHEDULED_DISPATCH_SWAP_SCHEDULES | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE), CcSwapSchDispatchSchedules);
				}
				break;
		}

		this->SetDirty();
	}

	static void ClearScheduleCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			SchdispatchWindow *w = (SchdispatchWindow*)win;
			if (w->IsScheduleSelected()) {
				DoCommandP(0, w->vehicle->index | (w->schedule_index << 20), 0, CMD_SCHEDULED_DISPATCH_CLEAR | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
			}
		}
	}

	static void RemoveScheduleCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			SchdispatchWindow *w = (SchdispatchWindow*)win;
			if (w->IsScheduleSelected()) {
				DoCommandP(0, w->vehicle->index | (w->schedule_index << 20), 0, CMD_SCHEDULED_DISPATCH_REMOVE_SCHEDULE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
			}
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_MANAGEMENT: {
				if (!this->IsScheduleSelected()) break;
				switch((ManagementDropdown)index) {
					case SCH_MD_RESET_LAST_DISPATCHED:
						DoCommandP(0, this->vehicle->index | (this->schedule_index << 20), 0, CMD_SCHEDULED_DISPATCH_RESET_LAST_DISPATCH | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
						break;

					case SCH_MD_CLEAR_SCHEDULE:
						if (this->GetSelectedSchedule().GetScheduledDispatch().empty()) return;
						SetDParam(0, (uint)this->GetSelectedSchedule().GetScheduledDispatch().size());
						ShowQuery(STR_SCHDISPATCH_QUERY_CLEAR_SCHEDULE_CAPTION, STR_SCHDISPATCH_QUERY_CLEAR_SCHEDULE_TEXT, this, ClearScheduleCallback);

						break;

					case SCH_MD_REMOVE_SCHEDULE:
						SetDParam(0, (uint)this->GetSelectedSchedule().GetScheduledDispatch().size());
						ShowQuery(STR_SCHDISPATCH_QUERY_REMOVE_SCHEDULE_CAPTION, STR_SCHDISPATCH_QUERY_REMOVE_SCHEDULE_TEXT, this, RemoveScheduleCallback);
						break;

					case SCH_MD_DUPLICATE_SCHEDULE:
						DoCommandP(0, this->vehicle->index | (this->schedule_index << 20), 0, CMD_SCHEDULED_DISPATCH_DUPLICATE_SCHEDULE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
						break;

					case SCH_MD_APPEND_VEHICLE_SCHEDULES: {
						static const CursorID clone_icons[] = {
							SPR_CURSOR_CLONE_TRAIN, SPR_CURSOR_CLONE_ROADVEH,
							SPR_CURSOR_CLONE_SHIP, SPR_CURSOR_CLONE_AIRPLANE
						};
						SetObjectToPlaceWnd(clone_icons[this->vehicle->type], PAL_NONE, HT_VEHICLE, this);
						break;
					}
				}
			}

			default:
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;
		const Vehicle *v = this->vehicle;

		switch (this->clicked_widget) {
			default: NOT_REACHED();

			case WID_SCHDISPATCH_ADD: {
				if (!this->IsScheduleSelected()) break;

				if (StrEmpty(str)) break;

				char *end;
				int32 val = std::strtoul(str, &end, 10);
				if (val >= 0 && end != nullptr && *end == 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					DateTicksScaled slot = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(hours, minutes));
					ScheduleAddIntl(v->index | (this->schedule_index << 20), slot, 0, 0);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_START_DATE: {
				if (!this->IsScheduleSelected()) break;

				if (StrEmpty(str)) break;

				char *end;
				int32 val = std::strtoul(str, &end, 10);
				if (val >= 0 && end != nullptr && *end == 0) {
					uint minutes = (val % 100) % 60;
					uint hours = (val / 100) % 24;
					DateTicksScaled start = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(hours, minutes));
					SetScheduleStartDateIntl(v->index | (this->schedule_index << 20), start);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				if (!this->IsScheduleSelected()) break;
				Ticks val = ParseTimetableDuration(str);

				if (val > 0) {
					DoCommandP(0, v->index | (this->schedule_index << 20), val, CMD_SCHEDULED_DISPATCH_SET_DURATION | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				if (!this->IsScheduleSelected()) break;

				if (StrEmpty(str)) break;

				DoCommandP(0, v->index | (this->schedule_index << 20), ParseTimetableDuration(str), CMD_SCHEDULED_DISPATCH_SET_DELAY | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
				break;
			}

			case WID_SCHDISPATCH_RENAME: {
				if (str == nullptr) return;

				DoCommandP(0, v->index | (this->schedule_index << 20), 0, CMD_SCHEDULED_DISPATCH_RENAME_SCHEDULE | CMD_MSG(STR_ERROR_CAN_T_RENAME_SCHEDULE), nullptr, str);
				break;
			}

			case WID_SCHDISPATCH_ADJUST: {
				if (!this->IsScheduleSelected()) break;
				Ticks val = ParseTimetableDuration(str);

				if (val != 0) {
					DoCommandP(0, v->index | (this->schedule_index << 20), val, CMD_SCHEDULED_DISPATCH_ADJUST | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
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
			MarkDirtyFocusedRoutePaths(this->vehicle);
		}
	}

	bool OnVehicleSelect(const Vehicle *v) override
	{
		if (v->orders == nullptr || v->orders->GetScheduledDispatchScheduleCount() == 0) return false;

		DoCommandP(0, this->vehicle->index, v->index, CMD_SCHEDULED_DISPATCH_APPEND_VEHICLE_SCHEDULE | CMD_MSG(STR_ERROR_CAN_T_TIMETABLE_VEHICLE));
		ResetObjectToPlace();
		return true;
	}

	const Vehicle *GetVehicle()
	{
		return this->vehicle;
	}

	void AddMultipleDepartureSlots(uint start, uint step, uint end)
	{
		bool wrap_mode = false;
		if (end < start) {
			const DispatchSchedule &ds = this->GetSelectedSchedule();
			if (ds.GetScheduledDispatchDuration() == (1440 * _settings_time.ticks_per_minute)) {
				/* 24 hour timetabling */
				end += 1440;
				wrap_mode = true;
			}
		}
		if (end < start || step == 0 || !this->IsScheduleSelected()) return;

		DateTicksScaled slot = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, start));
		ScheduleAddIntl(this->vehicle->index | (this->schedule_index << 20), slot, (end - start) / step, step * _settings_time.ticks_per_minute, wrap_mode);
	}
};

void CcAddNewSchDispatchSchedule(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow*>(FindWindowById(WC_SCHDISPATCH_SLOTS, p1));
	if (w != nullptr) {
		w->schedule_index = INT_MAX;
		w->AutoSelectSchedule();
		w->ReInit();
	}
}

void CcSwapSchDispatchSchedules(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow*>(FindWindowById(WC_SCHDISPATCH_SLOTS, p1));
	if (w != nullptr) {
		w->schedule_index = GB(p2, 0, 16);
		w->AutoSelectSchedule();
		w->ReInit();
	}
}

static const NWidgetPart _nested_schdispatch_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_RENAME), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_SCHDISPATCH_RENAME_SCHEDULE_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_MOVE_LEFT), SetMinimalSize(12, 14), SetDataTip(SPR_ARROW_LEFT, STR_SCHDISPATCH_MOVE_SCHEDULE),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_MOVE_RIGHT), SetMinimalSize(12, 14), SetDataTip(SPR_ARROW_RIGHT, STR_SCHDISPATCH_MOVE_SCHEDULE),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCHDISPATCH_CAPTION), SetDataTip(STR_SCHDISPATCH_CAPTION, STR_NULL),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ENABLED), SetDataTip(STR_SCHDISPATCH_ENABLED, STR_SCHDISPATCH_ENABLED_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			NWidget(WWT_TEXT, COLOUR_GREY, WID_SCHDISPATCH_HEADER), SetAlignment(SA_CENTER), SetDataTip(STR_JUST_STRING3, STR_NULL), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_PREV), SetDataTip(STR_SCHDISPATCH_PREV_SCHEDULE, STR_SCHDISPATCH_PREV_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_NEXT), SetDataTip(STR_SCHDISPATCH_NEXT_SCHEDULE, STR_SCHDISPATCH_NEXT_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADD_SCHEDULE), SetDataTip(STR_SCHDISPATCH_ADD_SCHEDULE, STR_SCHDISPATCH_ADD_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, WID_SCHDISPATCH_MATRIX), SetResize(1, 1), SetScrollbar(WID_SCHDISPATCH_V_SCROLL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SCHDISPATCH_V_SCROLL),
		EndContainer(),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_SCHDISPATCH_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADD), SetDataTip(STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADJUST), SetDataTip(STR_SCHDISPATCH_ADJUST, STR_SCHDISPATCH_ADJUST_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DURATION), SetDataTip(STR_SCHDISPATCH_DURATION, STR_SCHDISPATCH_DURATION_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_START_DATE), SetDataTip(STR_SCHDISPATCH_START, STR_SCHDISPATCH_START_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
			NWidget(NWID_VERTICAL, NC_EQUALSIZE),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DELAY), SetDataTip(STR_SCHDISPATCH_DELAY, STR_SCHDISPATCH_DELAY_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCHDISPATCH_MANAGEMENT), SetDataTip(STR_SCHDISPATCH_MANAGE, STR_NULL), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _schdispatch_desc(__FILE__, __LINE__,
	WDP_AUTO, "scheduled_dispatch_slots", 400, 130,
	WC_SCHDISPATCH_SLOTS, WC_VEHICLE_TIMETABLE,
	WDF_CONSTRUCTION,
	std::begin(_nested_schdispatch_widgets), std::end(_nested_schdispatch_widgets)
);

/**
 * Show the slot dispatching slots
 * @param v The vehicle to show the slot dispatching slots for
 */
void ShowSchdispatchWindow(const Vehicle *v)
{
	AllocateWindowDescFront<SchdispatchWindow>(&_schdispatch_desc, v->index);
}

enum ScheduledDispatchAddSlotsWindowWidgets {
	WID_SCHDISPATCH_ADD_SLOT_START_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_START_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_END_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_END_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON,
	WID_SCHDISPATCH_ADD_SLOT_START_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_END_TEXT,
};

struct ScheduledDispatchAddSlotsWindow : Window {
	ClockFaceMinutes start;
	ClockFaceMinutes step;
	ClockFaceMinutes end;

	ScheduledDispatchAddSlotsWindow(WindowDesc *desc, WindowNumber window_number, SchdispatchWindow *parent) :
			Window(desc)
	{
		this->start = _settings_time.NowInTickMinutes().ToClockFaceMinutes();
		this->step = 30;
		this->end = this->start + 60;
		this->parent = parent;
		this->CreateNestedTree();
		this->FinishInitNested(window_number);
	}

	Point OnInitialPosition(int16 sm_width, int16 sm_height, int window_number) override
	{
		Point pt = { this->parent->left + this->parent->width / 2 - sm_width / 2, this->parent->top + this->parent->height / 2 - sm_height / 2 };
		return pt;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		Dimension d = {0, 0};
		switch (widget) {
			default: return;

			case WID_SCHDISPATCH_ADD_SLOT_START_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_END_TEXT:
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_START));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END));
				break;

			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:
				for (uint i = 0; i < 24; i++) {
					SetDParam(0, i);
					d = maxdim(d, GetStringBoundingBox(STR_JUST_INT));
				}
				break;

			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:
				for (uint i = 0; i < 60; i++) {
					SetDParam(0, i);
					d = maxdim(d, GetStringBoundingBox(STR_JUST_INT));
				}
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		*size = d;
	}

	virtual void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:   SetDParam(0, start.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE: SetDParam(0, start.ClockMinute()); break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:    SetDParam(0, step.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:  SetDParam(0, step.ClockMinute()); break;
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:     SetDParam(0, end.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:   SetDParam(0, end.ClockMinute()); break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		auto handle_hours_dropdown = [&](ClockFaceMinutes current) {
			DropDownList list;
			for (uint i = 0; i < 24; i++) {
				SetDParam(0, i);
				list.emplace_back(new DropDownListStringItem(STR_JUST_INT, i, false));
			}
			ShowDropDownList(this, std::move(list), current.ClockHour(), widget);
		};

		auto handle_minutes_dropdown = [&](ClockFaceMinutes current) {
			DropDownList list;
			for (uint i = 0; i < 60; i++) {
				SetDParam(0, i);
				list.emplace_back(new DropDownListStringItem(STR_JUST_INT, i, false));
			}
			ShowDropDownList(this, std::move(list), current.ClockMinute(), widget);
		};

		switch (widget) {
			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:
				handle_hours_dropdown(this->start);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE:
				handle_minutes_dropdown(this->start);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:
				handle_hours_dropdown(this->step);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:
				handle_minutes_dropdown(this->step);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:
				handle_hours_dropdown(this->end);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:
				handle_minutes_dropdown(this->end);
				break;

			case WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON:
				static_cast<SchdispatchWindow *>(this->parent)->AddMultipleDepartureSlots(this->start.base(), this->step.base(), this->end.base());
				this->Close();
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:
				this->start = ClockFaceMinutes::FromClockFace(index, this->start.ClockMinute());
				break;
			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE:
				this->start = ClockFaceMinutes::FromClockFace(this->start.ClockHour(), index);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:
				this->step = ClockFaceMinutes::FromClockFace(index, this->step.ClockMinute());
				break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:
				this->step = ClockFaceMinutes::FromClockFace(this->step.ClockHour(), index);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:
				this->end = ClockFaceMinutes::FromClockFace(index, this->end.ClockMinute());
				break;
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:
				this->end = ClockFaceMinutes::FromClockFace(this->end.ClockHour(), index);
				break;
		}


		this->SetWidgetDirty(widget);
	}
};

static const NWidgetPart _nested_scheduled_dispatch_add_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_TIME_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_SCHDISPATCH_ADD_SLOT_START_TEXT), SetDataTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_START, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_START_HOUR), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_HOUR_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_START_MINUTE), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_MINUTE_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT), SetDataTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_HOUR_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_MINUTE_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_SCHDISPATCH_ADD_SLOT_END_TEXT), SetDataTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END, STR_NULL),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_END_HOUR), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_HOUR_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_END_MINUTE), SetFill(1, 0), SetDataTip(STR_JUST_INT, STR_DATE_MINUTES_MINUTE_TOOLTIP),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON), SetMinimalSize(100, 12), SetDataTip(STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static WindowDesc _scheduled_dispatch_add_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_SET_DATE, WC_NONE,
	0,
	std::begin(_nested_scheduled_dispatch_add_widgets), std::end(_nested_scheduled_dispatch_add_widgets)
);

void ShowScheduledDispatchAddSlotsWindow(SchdispatchWindow *parent, int window_number)
{
	CloseWindowByClass(WC_SET_DATE);

	new ScheduledDispatchAddSlotsWindow(&_scheduled_dispatch_add_desc, window_number, parent);
}

void SchdispatchInvalidateWindows(const Vehicle *v)
{
	v = v->FirstShared();
	for (Window *w : Window::Iterate()) {
		if (w->window_class == WC_VEHICLE_TIMETABLE) {
			if (static_cast<GeneralVehicleWindow *>(w)->vehicle->FirstShared() == v) w->SetDirty();
		}
		if (w->window_class == WC_SCHDISPATCH_SLOTS || w->window_class == WC_VEHICLE_ORDERS) {
			if (static_cast<GeneralVehicleWindow *>(w)->vehicle->FirstShared() == v) w->InvalidateData(VIWD_MODIFY_ORDERS, false);
		}
	}
}
