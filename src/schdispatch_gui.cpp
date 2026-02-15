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
#include "querystring_gui.h"
#include "core/string_builder.hpp"
#include "core/string_consumer.hpp"
#include "strings_func.h"
#include "vehicle_base.h"
#include "string_func.h"
#include "string_func_extra.h"
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
#include "dropdown_func.h"
#include "dropdown_common_type.h"
#include "core/geometry_func.hpp"
#include "tilehighlight_func.h"
#include "timetable_cmd.h"
#include "schdispatch.h"
#include "error.h"
#include "3rdparty/cpp-btree/btree_set.h"

#include <vector>
#include <algorithm>

#include "table/strings.h"
#include "table/string_colours.h"
#include "table/sprites.h"

#include "safeguards.h"

enum SchdispatchWidgets : WidgetID {
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

	WID_SCHDISPATCH_SLOT_DISPLAY_MODE, ///< Slot display mode toggle
	WID_SCHDISPATCH_ADD,             ///< Add Departure Time button
	WID_SCHDISPATCH_SET_DURATION,    ///< Duration button
	WID_SCHDISPATCH_SET_START_DATE,  ///< Start Date button
	WID_SCHDISPATCH_SET_DELAY,       ///< Delay button
	WID_SCHDISPATCH_MANAGEMENT,      ///< Management button
	WID_SCHDISPATCH_ADJUST,          ///< Adjust departure times
	WID_SCHDISPATCH_REMOVE,          ///< Remove departure times
	WID_SCHDISPATCH_MANAGE_SLOT,     ///< Manage slot button
};

/**
 * Callback for when a time has been chosen to start the schedule
 * @param window the window related to the setting of the date
 * @param date the actually chosen date
 * @param callback_data callback data
 */
static void SetScheduleStartDateCallback(const Window *w, StateTicks date, void *callback_data)
{
	Command<CMD_SCH_DISPATCH_SET_START_DATE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, w->window_number, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(callback_data)), date);
}

/**
 * Callback for when a time has been chosen to add to the schedule
 */
static void ScheduleAddIntl(VehicleID veh, uint schedule_index, StateTicks date, uint extra_slots, uint offset, uint16_t slot_flags, DispatchSlotRouteID route_id, bool wrap_mode = false)
{
	Vehicle *v = Vehicle::GetIfValid(veh);
	if (v == nullptr || !v->IsPrimaryVehicle() || schedule_index >= v->orders->GetScheduledDispatchScheduleCount()) return;

	const DispatchSchedule &ds = v->orders->GetDispatchScheduleByIndex(schedule_index);

	/* Make sure the time is the closest future to the timetable start */
	StateTicks start_tick = ds.GetScheduledDispatchStartTick();
	uint32_t duration = ds.GetScheduledDispatchDuration();
	uint32_t slot = WrapTickToScheduledDispatchRange(start_tick, duration, date);

	if (extra_slots > 0 && offset > 0 && !wrap_mode && slot < duration) {
		uint max_extra_slots = (duration - 1 - slot) / offset;
		if (max_extra_slots < extra_slots) extra_slots = max_extra_slots;
		extra_slots = std::min<uint>(extra_slots, UINT16_MAX);
	}

	Command<CMD_SCH_DISPATCH_ADD>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, veh, schedule_index, slot, offset, extra_slots, slot_flags, route_id);
}

/**
 * Callback for when a time has been chosen to add to the schedule
 * @param window the window related to the setting of the date
 * @param date the actually chosen date
 * @param callback_data callback data
 */
static void ScheduleAddCallback(const Window *w, StateTicks date, void *callback_data)
{
	ScheduleAddIntl(w->window_number, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(callback_data)), date, 0, 0, 0, 0);
}

/**
 * Calculate the maximum number of vehicle required to run this timetable according to the dispatch schedule
 * @param timetable_duration  timetable duration in scaled tick
 * @param schedule_duration  scheduled dispatch duration in scaled tick
 * @param offsets list of all dispatch offsets in the schedule
 * @return maximum number of vehicles required
 */
static int CalculateMaxRequiredVehicle(Ticks timetable_duration, uint32_t schedule_duration, const std::vector<DispatchSlot> &slots)
{
	if (timetable_duration == INVALID_TICKS) return -1;
	if (slots.size() == 0) return -1;

	/* Number of time required to ensure all vehicle are counted */
	int required_loop = CeilDiv(timetable_duration, schedule_duration) + 1;

	/* Create indice array to count maximum overlapping range */
	std::vector<std::pair<uint32_t, int>> indices;
	for (int i = 0; i < required_loop; i++) {
		for (const DispatchSlot &slot : slots) {
			if (slot.offset >= schedule_duration) continue;
			indices.push_back(std::make_pair(i * schedule_duration + slot.offset, 1));
			indices.push_back(std::make_pair(i * schedule_duration + slot.offset + timetable_duration, -1));
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

void AddNewScheduledDispatchSchedule(VehicleID vindex)
{
	StateTicks start_tick;
	uint32_t duration;

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
		/* Set Jan 1st and 365 day, calendar and economy time must be locked together for this to result in a useful schedule */
		start_tick = DateToStateTicks(EconTime::DateAtStartOfYear(EconTime::CurYear()));
		duration = (EconTime::UsingWallclockUnits() ? EconTime::DAYS_IN_ECONOMY_WALLCLOCK_YEAR : DAYS_IN_YEAR) * DAY_TICKS;
	}

	Command<CMD_SCH_DISPATCH_ADD_NEW_SCHEDULE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, CommandCallback::AddNewSchDispatchSchedule, vindex, start_tick, duration);
}

struct SchdispatchWindow : GeneralVehicleWindow {
	int schedule_index = -1;
	int clicked_widget = -1;      ///< The widget that was clicked (used to determine what to do in OnQueryTextFinished)
	int click_subaction = -1;     ///< Subaction for clicked_widget
	Scrollbar *vscroll = nullptr; ///< Vertical scrollbar
	uint num_columns = 0;         ///< Number of columns.

	StateTicks next_departure_update = STATE_TICKS_INT_MAX; ///< Time after which the last departure value should be re-drawn
	uint warning_count = 0;
	uint extra_line_count = 0;

	int base_width = 0;
	int header_width = 0;
	int delete_flag_width = 0;
	int delete_flag_height = 0;
	int arrow_flag_width = 0;
	int arrow_flag_height = 0;

	bool remove_slot_mode = false;
	bool slot_display_long_mode = false;

	btree::btree_set<uint32_t> selected_slots;
	ScheduledDispatchSlotSet adjust_slot_set;

	ScheduledDispatchSlotSet GetSelectedSlotSet() const
	{
		ScheduledDispatchSlotSet slot_set;
		slot_set.slots.reserve(this->selected_slots.size());
		for (uint32_t slot : this->selected_slots) {
			slot_set.slots.push_back(slot);
		}
		return slot_set;
	}

	enum ManagementDropdown {
		SCH_MD_RESET_LAST_DISPATCHED,
		SCH_MD_CLEAR_SCHEDULE,
		SCH_MD_REMOVE_SCHEDULE,
		SCH_MD_DUPLICATE_SCHEDULE,
		SCH_MD_APPEND_VEHICLE_SCHEDULES,
		SCH_MD_REUSE_DEPARTURE_SLOTS,
		SCH_MD_RENAME_TAG,
		SCH_MD_EDIT_ROUTE,
	};

	struct DispatchSlotPositionHandler {
		StateTicks start_tick;
		uint num_columns;
		uint last_column = 0;
		int last_row = -1;
		int last_hour = INT_MIN;

		DispatchSlotPositionHandler(StateTicks start_tick, uint num_columns) : start_tick(start_tick), num_columns(num_columns) {}

		void AddSlot(DispatchSlot slot)
		{
			int hour = -1;
			if (_settings_time.time_in_minutes) {
				ClockFaceMinutes slot_minutes = _settings_time.ToTickMinutes(this->start_tick + slot.offset).ToClockFaceMinutes();
				hour = slot_minutes.ClockHour();
			}
			if (hour != this->last_hour || this->last_column + 1 == this->num_columns) {
				this->last_hour = hour;
				this->last_row++;
				this->last_column = 0;
			} else {
				this->last_column++;
			}
		}

		int GetNumberOfRows() const
		{
			return this->last_row + 1;
		}
	};

	SchdispatchWindow(WindowDesc &desc, WindowNumber window_number) :
			GeneralVehicleWindow(desc, Vehicle::Get(window_number))
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SCHDISPATCH_V_SCROLL);
		this->FinishInitNested(window_number);

		this->owner = this->vehicle->owner;
		this->AutoSelectSchedule();
	}

	void Close(int data = 0) override
	{
		FocusWindowById(WC_VEHICLE_VIEW, this->window_number);
		this->GeneralVehicleWindow::Close();
	}

	bool TimeUnitsUsable() const
	{
		return _settings_time.time_in_minutes || !EconTime::UsingWallclockUnits();
	}

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
			this->selected_slots.clear();
		}
	}

	const DispatchSchedule &GetSelectedSchedule() const
	{
		return this->vehicle->orders->GetDispatchScheduleByIndex(this->schedule_index);
	}

	template <typename F>
	void IterateSelectedSlots(F handler)
	{
		if (this->selected_slots.empty()) return;

		if (!this->IsScheduleSelected()) {
			this->selected_slots.clear();
			return;
		}

		auto it = this->selected_slots.begin();
		for (const DispatchSlot &slot : this->GetSelectedSchedule().GetScheduledDispatch()) {
			while (slot.offset > *it) {
				/* Selected slot no longer in schedule, erase. */
				it = this->selected_slots.erase(it);
				if (it == this->selected_slots.end()) return;
			}
			if (slot.offset == *it) {
				handler(slot);
				++it;
				if (it == this->selected_slots.end()) return;
			}
		}
		if (it != this->selected_slots.end()) {
			this->selected_slots.erase(it, this->selected_slots.end());
		}
	}

	void ValidateSelectedSlots()
	{
		/* Clear any missing selected slots. */
		this->IterateSelectedSlots([](const DispatchSlot &) {});
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: {
				uint min_height = 0;

				int64_t max_value = GetParamMaxValue(_settings_time.time_in_minutes ? 0 : EconTime::MAX_YEAR.base() * DAYS_IN_YEAR);
				Dimension unumber = GetStringBoundingBox(GetString(STR_SCHDISPATCH_DATE_WALLCLOCK_TINY_FLAGGED, max_value));

				const Sprite *spr = GetSprite(SPR_FLAG_VEH_STOPPED, SpriteType::Normal, LowZoomMask(_gui_zoom));
				this->delete_flag_width = UnScaleGUI(spr->width);
				this->delete_flag_height = UnScaleGUI(spr->height);

				const Sprite *spr_left_arrow = GetSprite(SPR_ARROW_LEFT, SpriteType::Normal, LowZoomMask(_gui_zoom));
				const Sprite *spr_right_arrow = GetSprite(SPR_ARROW_RIGHT, SpriteType::Normal, LowZoomMask(_gui_zoom));
				this->arrow_flag_width = UnScaleGUI(std::max(spr_left_arrow->width, spr_right_arrow->width));
				this->arrow_flag_height = UnScaleGUI(std::max(spr_left_arrow->height, spr_right_arrow->height));

				min_height = std::max<uint>(unumber.height + WidgetDimensions::scaled.matrix.top, UnScaleGUI(spr->height));
				this->header_width = std::max(this->delete_flag_width, this->arrow_flag_width);
				this->base_width = unumber.width + this->header_width + 4;

				resize.height = min_height;
				resize.width = base_width + WidgetDimensions::scaled.framerect.left + WidgetDimensions::scaled.framerect.right;
				size.height = resize.height * 3;
				if (this->slot_display_long_mode) {
					resize.width *= 4;
					size.width = resize.width * 2;
				} else {
					size.width = resize.width * 3;
				}

				fill.width = resize.width;
				fill.height = resize.height;
				break;
			}

			case WID_SCHDISPATCH_SUMMARY_PANEL:
				size.height = (6 + this->extra_line_count) * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical() + (WidgetDimensions::scaled.vsep_wide * 2);
				uint warning_count = this->warning_count;
				if (warning_count > 0) {
					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					size.height += warning_count * std::max<int>(warning_dimensions.height, GetCharacterHeight(FS_NORMAL));
				}
				break;
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

		const bool unviewable = (v->orders == nullptr) || !this->TimeUnitsUsable();
		const bool uneditable = (v->orders == nullptr) || (v->owner != _local_company);
		const bool unusable = unviewable || uneditable;

		this->SetWidgetDisabledState(WID_SCHDISPATCH_ENABLED, uneditable || (!v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) && (unviewable || v->vehicle_flags.Test(VehicleFlag::TimetableSeparation) || v->HasUnbunchingOrder())));

		this->SetWidgetDisabledState(WID_SCHDISPATCH_RENAME, unusable || v->orders->GetScheduledDispatchScheduleCount() == 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_PREV, unviewable || this->schedule_index <= 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_NEXT, unviewable || this->schedule_index >= (int)(v->orders->GetScheduledDispatchScheduleCount() - 1));
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MOVE_LEFT, unviewable || this->schedule_index <= 0);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MOVE_RIGHT, unviewable || this->schedule_index >= (int)(v->orders->GetScheduledDispatchScheduleCount() - 1));
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADD_SCHEDULE, unusable || v->orders->GetScheduledDispatchScheduleCount() >= 4096);

		const bool disabled = unusable || !v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch)  || !this->IsScheduleSelected();
		const bool no_editable_slots = disabled || this->GetSelectedSchedule().GetScheduledDispatch().empty();
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SLOT_DISPLAY_MODE, unviewable);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADD, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_DURATION, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_START_DATE, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_SET_DELAY, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MANAGEMENT, disabled);
		this->SetWidgetDisabledState(WID_SCHDISPATCH_ADJUST, no_editable_slots);

		if (no_editable_slots || !this->IsScheduleSelected()) {
			this->selected_slots.clear();
		} else {
			this->ValidateSelectedSlots();
		}
		this->SetWidgetDisabledState(WID_SCHDISPATCH_MANAGE_SLOT, this->selected_slots.empty());

		NWidgetCore *remove_slot_widget = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_REMOVE);
		remove_slot_widget->SetDisabled(no_editable_slots);
		if (no_editable_slots) {
			remove_slot_widget->SetLowered(false);
			this->remove_slot_mode = false;
		}

		NWidgetCore *start_date_widget = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_SET_START_DATE);
		if (_settings_time.time_in_minutes) {
			start_date_widget->SetStringTip(STR_SCHDISPATCH_START_TIME, STR_SCHDISPATCH_SET_START_TIME);
		} else {
			start_date_widget->SetStringTip(STR_SCHDISPATCH_START, STR_SCHDISPATCH_SET_START);
		}

		if (this->IsScheduleSelected()) {
			const DispatchSchedule &ds = this->GetSelectedSchedule();
			DispatchSlotPositionHandler handler(ds.GetScheduledDispatchStartTick(), this->num_columns);
			for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
				handler.AddSlot(slot);
			}
			this->vscroll->SetCount(handler.GetNumberOfRows());
		} else {
			this->vscroll->SetCount(0);
		}

		this->SetWidgetLoweredState(WID_SCHDISPATCH_ENABLED, v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch));
		this->DrawWidgets();
	}

	virtual std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_SCHDISPATCH_CAPTION:
				return GetString(STR_SCHDISPATCH_CAPTION, this->vehicle->index);

			case WID_SCHDISPATCH_HEADER:
				if (this->IsScheduleSelected()) {
					const DispatchSchedule &ds = this->GetSelectedSchedule();
					if (ds.ScheduleName().empty()) {
						return GetString(STR_SCHDISPATCH_SCHEDULE_ID,
								this->schedule_index + 1,
								this->vehicle->orders->GetScheduledDispatchScheduleCount());
					} else {
						return GetString(STR_SCHDISPATCH_NAMED_SCHEDULE_ID,
								ds.ScheduleName(),
								this->schedule_index + 1,
								this->vehicle->orders->GetScheduledDispatchScheduleCount());
					}
				} else {
					return GetString(STR_SCHDISPATCH_NO_SCHEDULES);
				}

			default:
				return this->Window::GetWidgetString(widget, stringid);
		}
	}

	virtual bool OnTooltip(Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		switch (widget) {
			case WID_SCHDISPATCH_ENABLED: {
				if (!this->TimeUnitsUsable()) {
					GuiShowTooltips(this, GetEncodedString(STR_TOOLTIP_SEPARATION_CANNOT_ENABLE, STR_SCHDISPATCH_ENABLED_TOOLTIP, STR_CANNOT_ENABLE_BECAUSE_TIME_UNITS_UNUSABLE), close_cond);
				} else if (this->vehicle->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
					GuiShowTooltips(this, GetEncodedString(STR_TOOLTIP_SEPARATION_CANNOT_ENABLE, STR_SCHDISPATCH_ENABLED_TOOLTIP, STR_CANNOT_ENABLE_BECAUSE_AUTO_SEPARATION), close_cond);
				} else if (this->vehicle->HasUnbunchingOrder()) {
					GuiShowTooltips(this, GetEncodedString(STR_TOOLTIP_SEPARATION_CANNOT_ENABLE, STR_SCHDISPATCH_ENABLED_TOOLTIP, STR_CANNOT_ENABLE_BECAUSE_UNBUNCHING), close_cond);
				} else {
					GuiShowTooltips(this, GetEncodedString(STR_SCHDISPATCH_ENABLED_TOOLTIP), close_cond);
				}
				return true;
			}

			case WID_SCHDISPATCH_ADD: {
				if (_settings_time.time_in_minutes) {
					GuiShowTooltips(this, GetEncodedString(STR_SCHDISPATCH_ADD_TOOLTIP_EXTRA, STR_SCHDISPATCH_ADD_TOOLTIP), close_cond);
					return true;
				}
				break;
			}

			case WID_SCHDISPATCH_ADJUST: {
				GuiShowTooltips(this, GetEncodedString(STR_SCHDISPATCH_ADJUST_TOOLTIP_SELECTED, STR_SCHDISPATCH_ADJUST_TOOLTIP), close_cond);
				return true;
			}

			case WID_SCHDISPATCH_MANAGEMENT: {
				format_buffer buf;
				AppendStringInPlace(buf, STR_SCHDISPATCH_RESET_LAST_DISPATCH_TOOLTIP);
				auto add_suffix = [&](StringID str) {
					AppendStringInPlace(buf, STR_SCHDISPATCH_MANAGE_TOOLTIP_SUFFIX, str);
				};
				add_suffix(STR_SCHDISPATCH_CLEAR_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_REMOVE_SCHEDULE_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_DUPLICATE_SCHEDULE_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_APPEND_VEHICLE_SCHEDULES_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_REUSE_DEPARTURE_SLOTS_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_RENAME_DEPARTURE_TAG_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_EDIT_DEPARTURE_ROUTE_TOOLTIP);
				GuiShowTooltips(this, GetEncodedRawString(buf), close_cond);
				return true;
			}

			case WID_SCHDISPATCH_MANAGE_SLOT: {
				format_buffer buf;
				AppendStringInPlace(buf, STR_SCHDISPATCH_REUSE_THIS_DEPARTURE_SLOT_TOOLTIP);
				auto add_suffix = [&](StringID str) {
					AppendStringInPlace(buf, STR_SCHDISPATCH_MANAGE_TOOLTIP_SUFFIX, str);
				};
				add_suffix(STR_SCHDISPATCH_TAG_DEPARTURE_TOOLTIP);
				add_suffix(STR_SCHDISPATCH_ROUTE_DEPARTURE_TOOLTIP);
				GuiShowTooltips(this, GetEncodedRawString(buf), close_cond);
				return true;
			}

			case WID_SCHDISPATCH_MATRIX: {
				if (!this->TimeUnitsUsable()) return false;
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_SCHDISPATCH_MATRIX);
				const DispatchSlot *slot;
				bool is_header;
				std::tie(slot, is_header) = this->GetSlotFromMatrixPoint(pt.x - nwi->pos_x, pt.y - nwi->pos_y);
				if (slot == nullptr) {
					GuiShowTooltips(this, GetEncodedString(STR_SCHDISPATCH_SELECT_SLOT_TOOLTIP), close_cond);
					return true;
				}

				if (is_header && this->remove_slot_mode) {
					GuiShowTooltips(this, GetEncodedString(STR_SCHDISPATCH_REMOVE_SLOT), close_cond);
				} else {
					const DispatchSchedule &ds = this->GetSelectedSchedule();
					const StateTicks start_tick = ds.GetScheduledDispatchStartTick();

					format_buffer buf;
					AppendStringInPlace(buf, STR_SCHDISPATCH_SLOT_TOOLTIP, start_tick + slot->offset);
					if (_settings_time.time_in_minutes) {
						ClockFaceMinutes start_minutes = _settings_time.ToTickMinutes(start_tick).ToClockFaceMinutes();
						if (start_minutes != 0) {
							TickMinutes offset_minutes = TickMinutes{slot->offset / _settings_time.ticks_per_minute};
							AppendStringInPlace(buf, STR_SCHDISPATCH_SLOT_TOOLTIP_RELATIVE, offset_minutes.ClockHHMM());
						}
					}

					bool have_extra = false;
					auto show_time = [&](StringID msg, StateTicks dispatch_tick) {
						if (!have_extra) buf.push_back('\n');
						AppendStringInPlace(buf, msg);
						if (_settings_time.time_in_minutes) {
							ClockFaceMinutes mins = _settings_time.ToTickMinutes(dispatch_tick).ToClockFaceMinutes();
							if (mins != _settings_time.ToTickMinutes(start_tick + slot->offset).ToClockFaceMinutes()) {
								AppendStringInPlace(buf, STR_SCHDISPATCH_SLOT_TOOLTIP_TIME_SUFFIX, dispatch_tick);
							}
						}
						have_extra = true;
					};

					auto record_iter = this->vehicle->dispatch_records.find(static_cast<uint16_t>(this->schedule_index));
					if (record_iter != this->vehicle->dispatch_records.end()) {
						const LastDispatchRecord &record = record_iter->second;
						int32_t veh_dispatch = ((record.dispatched - start_tick) % ds.GetScheduledDispatchDuration()).base();
						if (veh_dispatch < 0) veh_dispatch += ds.GetScheduledDispatchDuration();
						if (veh_dispatch == (int32_t)slot->offset) {
							show_time(STR_SCHDISPATCH_SLOT_TOOLTIP_VEHICLE, record.dispatched);
						}
					}

					int32_t last_dispatch = ds.GetScheduledDispatchLastDispatch();
					if (last_dispatch != INVALID_SCHEDULED_DISPATCH_OFFSET && (last_dispatch % ds.GetScheduledDispatchDuration() == slot->offset)) {
						show_time(STR_SCHDISPATCH_SLOT_TOOLTIP_LAST, start_tick + last_dispatch);
					}

					StateTicks next_slot = GetScheduledDispatchTime(ds, _state_ticks).first;
					if (next_slot != INVALID_STATE_TICKS && ((next_slot - ds.GetScheduledDispatchStartTick()).AsTicks() % ds.GetScheduledDispatchDuration() == slot->offset)) {
						show_time(STR_SCHDISPATCH_SLOT_TOOLTIP_NEXT, next_slot);
					}

					auto flags = slot->flags;
					if (ds.GetScheduledDispatchReuseSlots()) ClrBit(flags, DispatchSlot::SDSF_REUSE_SLOT);
					if (flags != 0 || slot->route_id != 0) buf.push_back('\n');

					if (HasBit(flags, DispatchSlot::SDSF_REUSE_SLOT)) {
						AppendStringInPlace(buf, STR_SCHDISPATCH_SLOT_TOOLTIP_REUSE);
					}

					if (slot->route_id != 0) {
						buf.push_back('\n');
						AppendStringInPlace(buf, STR_SCHDISPATCH_ROUTE, ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::RouteID, slot->route_id));
					}

					if (flags != 0) {
						for (uint8_t flag_bit = DispatchSlot::SDSF_FIRST_TAG; flag_bit <= DispatchSlot::SDSF_LAST_TAG; flag_bit++) {
							if (!HasBit(flags, flag_bit)) continue;

							std::string_view name = ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, flag_bit - DispatchSlot::SDSF_FIRST_TAG);
							buf.push_back('\n');
							AppendStringInPlace(buf, name.empty() ? STR_SCHDISPATCH_TAG_DEPARTURE : STR_SCHDISPATCH_TAG_DEPARTURE_NAMED, 1 + flag_bit - DispatchSlot::SDSF_FIRST_TAG, name);
						}
					}
					GuiShowTooltips(this, GetEncodedRawString(buf), close_cond);
				}
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
	void DrawScheduledTime(const StateTicks time, int left, int right, int y, TextColour colour, bool last, bool next, bool veh, bool flagged) const
	{
		bool rtl = _current_text_dir == TD_RTL;

		int text_left  = rtl ? right - this->base_width - 1 : left + this->header_width;
		int text_right = rtl ? right - this->header_width : left + this->base_width - 1;

		if (this->remove_slot_mode) {
			int diff_y = (this->resize.step_height - this->delete_flag_height) / 2 - 2;
			int offset_x = (this->header_width - this->delete_flag_width) / 2;
			DrawSprite(SPR_FLAG_VEH_STOPPED, PAL_NONE, offset_x + (rtl ? right - this->delete_flag_width : left), y + diff_y);
		} else {
			auto draw_arrow = [&](bool right_arrow) {
				SpriteID sprite = right_arrow ? SPR_ARROW_RIGHT : SPR_ARROW_LEFT;
				int diff_y = (this->resize.step_height - this->arrow_flag_height) / 2;
				int offset_x = (this->header_width - this->arrow_flag_width) / 2;
				DrawSprite(sprite, PAL_NONE, offset_x + (rtl ? right - this->delete_flag_width : left), y + diff_y);
			};
			if (veh) {
				int width = ScaleSpriteTrad(1);
				int x = left - WidgetDimensions::scaled.framerect.left;
				int top = y - WidgetDimensions::scaled.framerect.top;
				DrawRectOutline({ x, top, x + (int)this->resize.step_width - width, top + (int)this->resize.step_height - width }, PC_LIGHT_BLUE, width);
			}
			if (next) {
				draw_arrow(!rtl);
			} else if (last) {
				draw_arrow(rtl);
			}
		}

		DrawString(text_left, text_right, y + (this->resize.step_height - GetCharacterHeight(FS_NORMAL)) / 2,
				GetString(flagged ? STR_SCHDISPATCH_DATE_WALLCLOCK_TINY_FLAGGED : STR_JUST_TT_TIME, time), colour, SA_HOR_CENTER);
	}

	virtual void OnGameTick() override
	{
		if (_state_ticks >= this->next_departure_update) {
			this->next_departure_update = STATE_TICKS_INT_MAX;
			SetWidgetDirty(WID_SCHDISPATCH_SUMMARY_PANEL);
		}
	}

	virtual void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		const Vehicle *v = this->vehicle;

		switch (widget) {
			case WID_SCHDISPATCH_MATRIX: {
				/* If order is not initialized, don't draw */
				if (!this->IsScheduleSelected() || !this->TimeUnitsUsable()) break;

				bool rtl = _current_text_dir == TD_RTL;

				/* Set the row and number of boxes in each row based on the number of boxes drawn in the matrix */
				const NWidgetCore *wid = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
				const uint16_t rows_in_display = wid->current_y / wid->resize_y;

				const DispatchSchedule &ds = this->GetSelectedSchedule();
				const StateTicks start_tick = ds.GetScheduledDispatchStartTick();
				const StateTicks end_tick = ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchDuration();

				StateTicks slot = GetScheduledDispatchTime(ds, _state_ticks).first;
				int32_t next_offset = (slot != INVALID_STATE_TICKS) ? (slot - ds.GetScheduledDispatchStartTick()).AsTicks() % ds.GetScheduledDispatchDuration() : INT32_MIN;

				int32_t last_dispatch;
				if (ds.GetScheduledDispatchLastDispatch() != INVALID_SCHEDULED_DISPATCH_OFFSET) {
					last_dispatch = ds.GetScheduledDispatchLastDispatch() % ds.GetScheduledDispatchDuration();
				} else {
					last_dispatch = INT32_MIN;
				}

				int32_t veh_dispatch;
				auto record_iter = v->dispatch_records.find(static_cast<uint16_t>(this->schedule_index));
				if (record_iter != v->dispatch_records.end()) {
					const LastDispatchRecord &record = record_iter->second;
					veh_dispatch = ((record.dispatched - start_tick) % ds.GetScheduledDispatchDuration()).base();
					if (veh_dispatch < 0) veh_dispatch += ds.GetScheduledDispatchDuration();
				} else {
					veh_dispatch = INT32_MIN;
				}

				const int begin_row = this->vscroll->GetPosition();
				const int end_row = begin_row + rows_in_display;

				DispatchSlotPositionHandler handler(start_tick, this->num_columns);
				for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
					handler.AddSlot(slot);
					if (handler.last_row < begin_row || handler.last_row >= end_row) continue;

					int x = r.left + (rtl ? (this->num_columns - handler.last_column - 1) : handler.last_column) * this->resize.step_width;
					int y = r.top + WidgetDimensions::scaled.framerect.top + ((handler.last_row - begin_row) * this->resize.step_height);

					StateTicks draw_time = start_tick + slot.offset;
					bool last = last_dispatch == (int32_t)slot.offset;
					bool next = next_offset == (int32_t)slot.offset;
					bool veh = veh_dispatch == (int32_t)slot.offset;
					TextColour colour;
					if (this->selected_slots.count(slot.offset) > 0) {
						colour = TC_WHITE;
					} else {
						colour = draw_time >= end_tick ? TC_RED : TC_BLACK;
					}
					auto flags = slot.flags;
					if (ds.GetScheduledDispatchReuseSlots()) ClrBit(flags, DispatchSlot::SDSF_REUSE_SLOT);
					const int left = x + WidgetDimensions::scaled.framerect.left;
					const int right = x + this->resize.step_width - 1 - (2 * WidgetDimensions::scaled.framerect.left);

					if (this->slot_display_long_mode) {
						int detail_left = left;
						int detail_right = right;
						if (_current_text_dir == TD_RTL) {
							detail_right -= this->base_width + WidgetDimensions::scaled.vsep_wide;
						} else {
							detail_left += this->base_width + WidgetDimensions::scaled.vsep_wide;
						}

						format_buffer str;
						auto prepare_str = [&](bool short_mode) {
							if (HasBit(flags, DispatchSlot::SDSF_REUSE_SLOT)) {
								AppendStringInPlace(str, STR_SCHDISPATCH_REUSE_DEPARTURE_SLOTS_SHORT);
							}

							if (slot.route_id != 0) {
								if (!str.empty()) str.append(GetListSeparator());
								AppendStringInPlace(str, STR_SCHDISPATCH_ROUTE, ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::RouteID, slot.route_id));
							}

							if ((flags & GetBitMaskFL<uint16_t>(DispatchSlot::SDSF_FIRST_TAG, DispatchSlot::SDSF_LAST_TAG)) != 0) {
								uint tag_count = 0;
								uint named_tag_count = 0;
								std::array<std::string_view, 1 + DispatchSlot::SDSF_LAST_TAG - DispatchSlot::SDSF_FIRST_TAG> tag_names{};
								for (uint8_t flag_bit = DispatchSlot::SDSF_FIRST_TAG; flag_bit <= DispatchSlot::SDSF_LAST_TAG; flag_bit++) {
									if (!HasBit(flags, flag_bit)) continue;

									tag_count++;
									if (!short_mode) {
										std::string_view name = ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, flag_bit - DispatchSlot::SDSF_FIRST_TAG);
										if (!name.empty()) {
											named_tag_count++;
											tag_names[flag_bit - DispatchSlot::SDSF_FIRST_TAG] = name;
										}
									}
								}

								const bool condense = ((named_tag_count == 0) && (tag_count > 1));
								bool first = true;
								for (uint8_t flag_bit = DispatchSlot::SDSF_FIRST_TAG; flag_bit <= DispatchSlot::SDSF_LAST_TAG; flag_bit++) {
									if (!HasBit(flags, flag_bit)) continue;

									if (!str.empty()) str.append(GetListSeparator());
									const uint tag_num = 1 + flag_bit - DispatchSlot::SDSF_FIRST_TAG;
									if (condense) {
										if (first) AppendStringInPlace(str, STR_SCHDISPATCH_TAGS_PREFIX);
										AppendStringInPlace(str, STR_JUST_INT, tag_num);
									} else {
										std::string_view name = tag_names[flag_bit - DispatchSlot::SDSF_FIRST_TAG];
										AppendStringInPlace(str, name.empty() ? STR_SCHDISPATCH_TAG_DEPARTURE : STR_SCHDISPATCH_TAG_DEPARTURE_NAMED, tag_num, name);
									}
									first = false;
								}
							}
						};
						prepare_str(false);
						if ((int)GetStringBoundingBox(str).width > detail_right - detail_left) {
							/* Use shortened version of string */
							str.clear();
							prepare_str(true);
						}

						DrawString(detail_left, detail_right, y + (this->resize.step_height - GetCharacterHeight(FS_NORMAL)) / 2, str, colour);
					}

					this->DrawScheduledTime(draw_time, left, right, y, colour, last, next, veh, !this->slot_display_long_mode && (flags != 0 || slot.route_id != 0));
				}
				break;
			}

			case WID_SCHDISPATCH_SUMMARY_PANEL: {
				const_cast<SchdispatchWindow*>(this)->next_departure_update = STATE_TICKS_INT_MAX;
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				int y = ir.top;

				if (!this->TimeUnitsUsable()) {
					const Dimension warning_dimensions = GetSpriteSize(SPR_WARNING_SIGN);
					int left = ir.left;
					int right = ir.right;
					const bool rtl = (_current_text_dir == TD_RTL);
					DrawSprite(SPR_WARNING_SIGN, 0, rtl ? right - warning_dimensions.width - 5 : left + 5, y);
					if (rtl) {
						right -= (warning_dimensions.width + 10);
					} else {
						left += (warning_dimensions.width + 10);
					}
					DrawStringMultiLine(left, right, y, ir.bottom, STR_CANNOT_ENABLE_BECAUSE_TIME_UNITS_UNUSABLE, TC_BLACK);
					break;
				}

				auto set_next_departure_update = [&](StateTicks time) {
					if (time < this->next_departure_update) const_cast<SchdispatchWindow*>(this)->next_departure_update = time;
				};

				auto draw_warning_generic = [&](std::string_view text, TextColour colour) {
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
					DrawString(left, right, y + (step_height - GetCharacterHeight(FS_NORMAL)) / 2, text, colour);
					y += step_height;
				};

				if (!v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch) || !this->IsScheduleSelected()) {
					y += GetCharacterHeight(FS_NORMAL);
					DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_NOT_ENABLED);
					y += GetCharacterHeight(FS_NORMAL) * 2;

					if (v->vehicle_flags.Test(VehicleFlag::TimetableSeparation)) {
						draw_warning_generic(GetString(STR_CANNOT_ENABLE_BECAUSE_AUTO_SEPARATION), TC_BLACK);
					} else if (v->HasUnbunchingOrder()) {
						draw_warning_generic(GetString(STR_CANNOT_ENABLE_BECAUSE_UNBUNCHING), TC_BLACK);
					}
				} else {
					const DispatchSchedule &ds = this->GetSelectedSchedule();

					uint warnings = 0;
					uint extra_lines = 0;

					auto draw_warning = [&]<typename... T>(StringID text, T&&... params) {
						draw_warning_generic(GetString(text, std::forward<T>(params)...), TC_FROMSTRING);
						warnings++;
					};

					auto departure_time_warnings = [&](StateTicks time) {
						if (_settings_time.time_in_minutes && time > (_state_ticks + (1350 * (uint)_settings_time.ticks_per_minute))) {
							/* If the departure slot is more than 23 hours ahead of now, show a warning */
							const TickMinutes now = _settings_time.NowInTickMinutes();
							const TickMinutes target = _settings_time.ToTickMinutes(time);
							const TickMinutes delta = target - now;
							if (delta >= (23 * 60)) {
								const uint hours = delta.base() / 60;
								draw_warning(STR_SCHDISPATCH_MORE_THAN_N_HOURS_IN_FUTURE, hours);

								set_next_departure_update(_settings_time.FromTickMinutes(target - (hours * 60) + 1));
							}
						}
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
					if (schedule_order_index < 0) {
						draw_warning(STR_SCHDISPATCH_NOT_ASSIGNED_TO_ORDER);
					} else {
						const Order *order = v->GetOrder(schedule_order_index);

						format_buffer buf;
						auto set_text = [&](StringParameter p1, StringParameter p2 = {}, StringParameter p3 = {}) {
							AppendStringInPlace(buf, STR_SCHDISPATCH_ASSIGNED_TO_ORDER, schedule_order_index + 1, std::move(p1), std::move(p2), std::move(p3));
						};
						switch (order->GetType()) {
							case OT_GOTO_STATION:
								set_text(STR_STATION_NAME, order->GetDestination().ToStationID());
								break;

							case OT_GOTO_WAYPOINT:
								set_text(STR_WAYPOINT_NAME, order->GetDestination().ToStationID());
								break;

							case OT_GOTO_DEPOT:
								if (order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
									if (v->type == VEH_AIRCRAFT) {
										set_text(STR_ORDER_GO_TO_NEAREST_HANGAR);
									} else {
										set_text(STR_ORDER_GO_TO_NEAREST_DEPOT);
									}
								} else {
									set_text(STR_DEPOT_NAME, v->type, order->GetDestination().ToDepotID());
								}
								break;

							default:
								set_text(STR_INVALID_ORDER);
								break;
						}

						DrawString(ir.left, ir.right, y, buf);
						y += GetCharacterHeight(FS_NORMAL);
						extra_lines++;
					}

					y += WidgetDimensions::scaled.vsep_wide;

					auto show_last_departure = [&](const StateTicks last_departure, bool vehicle_mode, std::string_view details) {
						StringID str;
						if (_state_ticks < last_departure) {
							str = STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_FUTURE;
							set_next_departure_update(last_departure);
						} else {
							str = STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_PAST;
						}
						if (vehicle_mode) str += (STR_SCHDISPATCH_SUMMARY_VEHICLE_DEPARTURE_PAST - STR_SCHDISPATCH_SUMMARY_LAST_DEPARTURE_PAST);

						if (details.empty()) {
							DrawString(ir.left, ir.right, y, GetString(str, last_departure, STR_EMPTY, std::monostate{}));
						} else {
							DrawString(ir.left, ir.right, y, GetString(str, last_departure, STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAILS, details));
						}
						y += GetCharacterHeight(FS_NORMAL);

						departure_time_warnings(last_departure);

						if (_settings_time.time_in_minutes && last_departure < (_state_ticks + (1350 * (uint)_settings_time.ticks_per_minute))) {
							/* If the departure slot is more than 23 hours behind now, show a warning */
							const TickMinutes now = _settings_time.NowInTickMinutes();
							const TickMinutes target = _settings_time.ToTickMinutes(last_departure);
							const TickMinutes delta = now - target;
							if (delta >= (23 * 60)) {
								const uint hours = delta.base() / 60;
								DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_MORE_THAN_N_HOURS_IN_PAST, hours));
								extra_lines++;
								y += GetCharacterHeight(FS_NORMAL);

								set_next_departure_update(_settings_time.FromTickMinutes(target + ((hours + 1) * 60) + 1));
							}
						}
					};

					auto record_iter = v->dispatch_records.find(static_cast<uint16_t>(this->schedule_index));
					if (record_iter != v->dispatch_records.end()) {
						const LastDispatchRecord &record = record_iter->second;
						format_buffer details;
						auto add_detail = [&](StringID str) {
							if (!details.empty()) details.append(GetListSeparator());
							AppendStringInPlace(details, STR_JUST_STRING, str);
						};
						if (HasBit(record.record_flags, LastDispatchRecord::RF_FIRST_SLOT)) add_detail(STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAIL_WAS_FIRST);
						if (HasBit(record.record_flags, LastDispatchRecord::RF_LAST_SLOT)) add_detail(STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAIL_WAS_LAST);

						for (uint8_t flag_bit = DispatchSlot::SDSF_FIRST_TAG; flag_bit <= DispatchSlot::SDSF_LAST_TAG; flag_bit++) {
							if (HasBit(record.slot_flags, flag_bit)) {
								if (!details.empty()) details.append(GetListSeparator());

								std::string_view name = ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, flag_bit - DispatchSlot::SDSF_FIRST_TAG);
								AppendStringInPlace(details, name.empty() ? STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAIL_TAG : STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAIL_TAG_NAMED,
										1 + flag_bit - DispatchSlot::SDSF_FIRST_TAG, name);
							}
						}

						if (record.route_id != 0) {
							if (!details.empty()) details.append(GetListSeparator());

							std::string_view name = ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::RouteID, record.route_id);
							AppendStringInPlace(details, STR_SCHDISPATCH_SUMMARY_DEPARTURE_DETAIL_ROUTE, name);
						}

						show_last_departure(record.dispatched, true, details);
					} else {
						DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_VEHICLE_NO_LAST_DEPARTURE);
						y += GetCharacterHeight(FS_NORMAL);
					}

					if (ds.GetScheduledDispatchLastDispatch() != INVALID_SCHEDULED_DISPATCH_OFFSET) {
						show_last_departure(ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchLastDispatch(), false, {});
					} else {
						DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_NO_LAST_DEPARTURE);
						y += GetCharacterHeight(FS_NORMAL);
					}

					const StateTicks next_departure = GetScheduledDispatchTime(ds, _state_ticks).first;
					if (next_departure != INVALID_STATE_TICKS) {
						set_next_departure_update(next_departure + ds.GetScheduledDispatchDelay());
						DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_SUMMARY_NEXT_AVAILABLE_DEPARTURE, next_departure));
					}
					y += GetCharacterHeight(FS_NORMAL);

					departure_time_warnings(next_departure);

					y += WidgetDimensions::scaled.vsep_wide;

					if (ds.GetScheduledDispatchReuseSlots()) {
						DrawString(ir.left, ir.right, y, STR_SCHDISPATCH_SUMMARY_REUSE_SLOTS_ENABLED);
						extra_lines++;
						y += GetCharacterHeight(FS_NORMAL);
					}

					auto tt_params = GetTimetableParameters(ds.GetScheduledDispatchDuration(), true);
					DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_SUMMARY_L2, std::move(tt_params.first), std::move(tt_params.second)));
					y += GetCharacterHeight(FS_NORMAL);

					DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_SUMMARY_L3, ds.GetScheduledDispatchStartTick(), ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchDuration()));
					y += GetCharacterHeight(FS_NORMAL);

					tt_params = GetTimetableParameters(ds.GetScheduledDispatchDelay());
					DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_SUMMARY_L4, std::move(tt_params.first), std::move(tt_params.second)));
					y += GetCharacterHeight(FS_NORMAL);

					if (!ds.GetScheduledDispatchReuseSlots() && !have_conditional) {
						const int required_vehicle = CalculateMaxRequiredVehicle(v->orders->GetTimetableTotalDuration(), ds.GetScheduledDispatchDuration(), ds.GetScheduledDispatch());
						if (required_vehicle > 0) {
							DrawString(ir.left, ir.right, y, GetString(STR_SCHDISPATCH_SUMMARY_L1, required_vehicle));
							extra_lines++;
							y += GetCharacterHeight(FS_NORMAL);
						}
					}

					uint32_t duration = ds.GetScheduledDispatchDuration();
					for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
						if (slot.offset >= duration) {
							draw_warning(STR_SCHDISPATCH_SLOT_OUTSIDE_SCHEDULE);
							break;
						}
					}

					if (warnings != this->warning_count || extra_lines != this->extra_line_count) {
						SchdispatchWindow *mutable_this = const_cast<SchdispatchWindow *>(this);
						mutable_this->warning_count = warnings;
						mutable_this->extra_line_count = extra_lines;
						mutable_this->ReInit();
					}
				}

				break;
			}
		}
	}

	/**
	 * Get slot and whether it's in the header section in the departure time matrix.
	 * @param x Horizontal position in the matrix widget in pixels.
	 * @param y Vertical position in the matrix widget in pixels.
	 * @return slot, and whether the position was in the header section
	 */
	std::pair<const DispatchSlot *, bool> GetSlotFromMatrixPoint(int x, int y) const
	{
		if (!this->IsScheduleSelected()) return { nullptr, false };

		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(WID_SCHDISPATCH_MATRIX);
		/* In case of RTL the widgets are swapped as a whole */
		if (_current_text_dir == TD_RTL) x = matrix_widget->current_x - x;

		uint xt = x / this->resize.step_width;
		int xm = x % this->resize.step_width;
		if (xt >= this->num_columns) return { nullptr, false };

		int32_t row = y / this->resize.step_height;
		if (row >= this->vscroll->GetCapacity()) return { nullptr, false };

		row += this->vscroll->GetPosition();

		const DispatchSchedule &ds = this->GetSelectedSchedule();
		DispatchSlotPositionHandler handler(ds.GetScheduledDispatchStartTick(), this->num_columns);
		for (const DispatchSlot &slot : ds.GetScheduledDispatch()) {
			handler.AddSlot(slot);
			if (handler.last_row == row && handler.last_column == xt) {
				return { &slot, xm <= this->header_width };
			}
		}

		return { nullptr, false };
	}

	/**
	 * Handle click in the departure time matrix.
	 * @param x Horizontal position in the matrix widget in pixels.
	 * @param y Vertical position in the matrix widget in pixels.
	 */
	void TimeClick(int x, int y)
	{
		const DispatchSlot *slot;
		bool is_header;
		std::tie(slot, is_header) = this->GetSlotFromMatrixPoint(x, y);

		if (slot == nullptr) {
			if (!_ctrl_pressed && !this->selected_slots.empty()) {
				this->selected_slots.clear();
				this->SetWidgetDirty(WID_SCHDISPATCH_MATRIX);
			}
			return;
		}

		if (is_header && this->remove_slot_mode) {
			Command<CMD_SCH_DISPATCH_REMOVE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index, slot->offset);
			return;
		}

		if (_ctrl_pressed) {
			auto [iter, inserted] = this->selected_slots.insert(slot->offset);
			if (!inserted) {
				/* Slot was already in selection. */
				this->selected_slots.erase(iter);
			}
		} else if (this->selected_slots.size() > 1) {
			this->selected_slots.clear();
			this->selected_slots.insert(slot->offset);
		} else {
			auto iter = this->selected_slots.find(slot->offset);
			if (iter != this->selected_slots.end()) {
				/* Slot was already in selection. */
				this->selected_slots.erase(iter);
			} else {
				this->selected_slots.clear();
				this->selected_slots.insert(slot->offset);
			}
		}
		this->SetWidgetDirty(WID_SCHDISPATCH_MATRIX);
	}

	int32_t ProcessDurationForQueryString(int32_t duration) const
	{
		if (!_settings_client.gui.timetable_in_ticks) duration = RoundDivSU(duration, TimetableDisplayUnitSize());
		return duration;
	}

	int GetQueryStringCaptionOffset() const
	{
		if (_settings_client.gui.timetable_in_ticks) return 2;
		if (_settings_time.time_in_minutes) return 0;
		return 1;
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
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
				bool enable = !v->vehicle_flags.Test(VehicleFlag::ScheduledDispatch);

				Command<CMD_SCH_DISPATCH>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, enable);
				if (enable && this->vehicle->orders != nullptr && this->vehicle->orders->GetScheduledDispatchScheduleCount() == 0) {
					AddNewScheduledDispatchSchedule(v->index);
				}
				break;
			}

			case WID_SCHDISPATCH_ADD: {
				if (!this->IsScheduleSelected()) break;
				if (_settings_time.time_in_minutes) {
					void ShowScheduledDispatchAddSlotsWindow(SchdispatchWindow *parent, WindowNumber window_number, bool multiple);
					ShowScheduledDispatchAddSlotsWindow(this, v->index, _ctrl_pressed);
				} else {
					ShowSetDateWindow(this, v->index.base(), _state_ticks, EconTime::CurYear(), EconTime::CurYear() + 15,
							ScheduleAddCallback, reinterpret_cast<void *>(static_cast<uintptr_t>(this->schedule_index)), STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				std::string str = GetString(STR_JUST_INT, ProcessDurationForQueryString(this->GetSelectedSchedule().GetScheduledDispatchDuration()));
				ShowQueryString(str, STR_SCHDISPATCH_DURATION_CAPTION_MINUTE + this->GetQueryStringCaptionOffset(), 31, this, charset_filter, {});
				break;
			}

			case WID_SCHDISPATCH_SET_START_DATE: {
				if (!this->IsScheduleSelected()) break;
				if (_settings_time.time_in_minutes && _settings_client.gui.timetable_start_text_entry) {
					ShowQueryString(GetString(STR_JUST_INT, _settings_time.NowInTickMinutes().ClockHHMM()), STR_SCHDISPATCH_START_CAPTION_MINUTE, 31, this, CS_NUMERAL, QueryStringFlag::AcceptUnchanged);
				} else {
					ShowSetDateWindow(this, v->index.base(), _state_ticks, EconTime::CurYear(), EconTime::CurYear() + 15,
							SetScheduleStartDateCallback, reinterpret_cast<void *>(static_cast<uintptr_t>(this->schedule_index)), STR_SCHDISPATCH_SET_START, STR_SCHDISPATCH_START_TOOLTIP);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL : CS_NUMERAL_DECIMAL;
				std::string str = GetString(STR_JUST_INT, ProcessDurationForQueryString(this->GetSelectedSchedule().GetScheduledDispatchDelay()));
				ShowQueryString(str, STR_SCHDISPATCH_DELAY_CAPTION_MINUTE + this->GetQueryStringCaptionOffset(), 31, this, charset_filter, {});
				break;
			}

			case WID_SCHDISPATCH_MANAGEMENT: {
				if (!this->IsScheduleSelected()) break;
				const DispatchSchedule &schedule = this->GetSelectedSchedule();
				DropDownList list;
				auto add_str_item = [&](std::string &&str, int result) {
					std::unique_ptr<DropDownListStringItem> item = std::make_unique<DropDownListStringItem>(std::move(str), result, false);
					item->SetColourFlags(TC_FORCED);
					list.emplace_back(std::move(item));
				};
				auto add_item = [&](StringID str, int result) {
					add_str_item(GetString(str), result);
				};
				add_item(STR_SCHDISPATCH_RESET_LAST_DISPATCH, SCH_MD_RESET_LAST_DISPATCHED);
				list.push_back(MakeDropDownListDividerItem());
				add_item(STR_SCHDISPATCH_CLEAR, SCH_MD_CLEAR_SCHEDULE);
				add_item(STR_SCHDISPATCH_REMOVE_SCHEDULE, SCH_MD_REMOVE_SCHEDULE);
				add_item(STR_SCHDISPATCH_DUPLICATE_SCHEDULE, SCH_MD_DUPLICATE_SCHEDULE);
				add_item(STR_SCHDISPATCH_APPEND_VEHICLE_SCHEDULES, SCH_MD_APPEND_VEHICLE_SCHEDULES);
				list.push_back(MakeDropDownListDividerItem());
				list.push_back(MakeDropDownListCheckedItem(schedule.GetScheduledDispatchReuseSlots(), STR_SCHDISPATCH_REUSE_DEPARTURE_SLOTS, SCH_MD_REUSE_DEPARTURE_SLOTS, false));
				list.push_back(MakeDropDownListDividerItem());
				for (uint8_t tag = 0; tag < DispatchSchedule::DEPARTURE_TAG_COUNT; tag++) {
					std::string_view name = schedule.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, tag);
					std::string str = GetString(name.empty() ? STR_SCHDISPATCH_RENAME_DEPARTURE_TAG : STR_SCHDISPATCH_RENAME_DEPARTURE_TAG_NAMED, tag + 1, name);
					add_str_item(std::move(str), SCH_MD_RENAME_TAG | (tag << 16));
				}

				list.push_back(MakeDropDownListDividerItem());
				add_item(STR_SCHDISPATCH_CREATE_DEPARTURE_ROUTE, SCH_MD_EDIT_ROUTE);

				std::vector<std::pair<DispatchSlotRouteID, std::string_view>> route_names = schedule.GetSortedRouteIDNames();
				if (!route_names.empty()) {
					auto item = std::make_unique<DropDownUnselectable<DropDownListStringItem>>(GetString(STR_SCHDISPATCH_EDIT_DEPARTURE_ROUTE), -1);
					item->SetColourFlags(TC_FORCED);
					list.push_back(std::move(item));

					for (const auto &it : route_names) {
						auto item = std::make_unique<DropDownListIndentStringItem>(1, std::string{it.second}, SCH_MD_EDIT_ROUTE | (it.first << 16));
						item->SetColourFlags(TC_FORCED);
						list.push_back(std::move(item));
					}
				}

				ShowDropDownList(this, std::move(list), -1, WID_SCHDISPATCH_MANAGEMENT, 0, DropDownOptions{}, DDSF_SHARED);
				break;
			}

			case WID_SCHDISPATCH_PREV:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index > 0) {
					this->schedule_index--;
					this->selected_slots.clear();
				}
				this->ReInit();
				break;

			case WID_SCHDISPATCH_NEXT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index < (int)(this->vehicle->orders->GetScheduledDispatchScheduleCount() - 1)) {
					this->schedule_index++;
					this->selected_slots.clear();
				}
				this->ReInit();
				break;

			case WID_SCHDISPATCH_ADD_SCHEDULE:
				AddNewScheduledDispatchSchedule(this->vehicle->index);
				break;

			case WID_SCHDISPATCH_RENAME:
				if (!this->IsScheduleSelected()) break;
				ShowQueryString(this->GetSelectedSchedule().ScheduleName(), STR_SCHDISPATCH_RENAME_SCHEDULE_CAPTION,
						MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
				break;

			case WID_SCHDISPATCH_ADJUST: {
				if (!this->IsScheduleSelected()) break;
				CharSetFilter charset_filter = _settings_client.gui.timetable_in_ticks ? CS_NUMERAL_SIGNED : CS_NUMERAL_DECIMAL_SIGNED;
				StringID caption = STR_SCHDISPATCH_ADJUST_CAPTION_MINUTE + this->GetQueryStringCaptionOffset();

				if (_ctrl_pressed) {
					uint32_t first_slot_offset = 0;
					uint32_t slot_count = 0;
					this->IterateSelectedSlots([&](const DispatchSlot &slot) {
						if (slot_count == 0) {
							first_slot_offset = slot.offset;
						}
						slot_count++;
					});
					if (slot_count > 0) {
						const DispatchSchedule &ds = this->GetSelectedSchedule();
						EncodedString caption_str;
						if (slot_count == 1) {
							caption_str = GetEncodedString(STR_SCHDISPATCH_ADJUST_CAPTION_SLOT_PREFIXED,
									ds.GetScheduledDispatchStartTick() + first_slot_offset, caption);
						} else {
							caption_str = GetEncodedString(STR_SCHDISPATCH_ADJUST_CAPTION_MULTI_SLOT_PREFIXED,
									slot_count, ds.GetScheduledDispatchStartTick() + first_slot_offset, caption);
						}
						this->adjust_slot_set = this->GetSelectedSlotSet();
						ShowQueryString(GetString(STR_JUST_INT, 0), std::move(caption_str), 31, this, charset_filter, {});
					}
				} else {
					this->adjust_slot_set = {};
					ShowQueryString(GetString(STR_JUST_INT, 0), caption, 31, this, charset_filter, {});
				}
				break;
			}

			case WID_SCHDISPATCH_REMOVE: {
				if (!this->IsScheduleSelected()) break;
				this->remove_slot_mode = !this->remove_slot_mode;
				this->SetWidgetLoweredState(WID_SCHDISPATCH_REMOVE, this->remove_slot_mode);
				break;
			}

			case WID_SCHDISPATCH_MANAGE_SLOT: {
				uint16_t merged_flags = 0;
				bool non_default_route_id = false;
				std::bitset<256> route_ids{};
				uint count = 0;
				this->IterateSelectedSlots([&](const DispatchSlot &slot) {
					merged_flags |= slot.flags;
					route_ids.set(slot.route_id);
					if (slot.route_id != 0) non_default_route_id = true;
					count++;
				});
				if (count == 0) break;

				const DispatchSchedule &schedule = this->GetSelectedSchedule();

				DropDownList list;
				auto add_item = [&](std::string &&str, uint bit, bool disabled) {
					if (!HasBit(merged_flags, bit)) bit |= 0x100;
					list.push_back(MakeDropDownListCheckedItem(HasBit(merged_flags, bit), std::move(str), bit, disabled));
				};
				add_item(GetString(STR_SCHDISPATCH_REUSE_THIS_DEPARTURE_SLOT), DispatchSlot::SDSF_REUSE_SLOT, schedule.GetScheduledDispatchReuseSlots());
				list.push_back(MakeDropDownListDividerItem());
				for (uint8_t flag_bit = DispatchSlot::SDSF_FIRST_TAG; flag_bit <= DispatchSlot::SDSF_LAST_TAG; flag_bit++) {
					std::string_view name = schedule.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, flag_bit - DispatchSlot::SDSF_FIRST_TAG);
					std::string str;
					if (name.empty()) {
						str = GetString(STR_SCHDISPATCH_TAG_DEPARTURE, 1 + flag_bit - DispatchSlot::SDSF_FIRST_TAG);
					} else {
						str = GetString(STR_SCHDISPATCH_TAG_DEPARTURE_NAMED, 1 + flag_bit - DispatchSlot::SDSF_FIRST_TAG, name);
					}
					add_item(std::move(str), flag_bit, false);
				}


				std::vector<std::pair<DispatchSlotRouteID, std::string_view>> route_names = schedule.GetSortedRouteIDNames();
				if (!route_names.empty() || non_default_route_id) {
					list.push_back(MakeDropDownListDividerItem());
					list.push_back(MakeDropDownListCheckedItem(route_ids.test(0), STR_ORDER_CONDITIONAL_DISPATCH_SLOT_DEF_ROUTE, 1 << 16));

					for (const auto &it : route_names) {
						list.push_back(MakeDropDownListCheckedItem(route_ids.test(it.first), std::string{it.second}, (1 << 16) | it.first));
					}
				}

				ShowDropDownList(this, std::move(list), -1, WID_SCHDISPATCH_MANAGE_SLOT, 0, DropDownOptions{}, DDSF_SHARED);
				break;
			}

			case WID_SCHDISPATCH_MOVE_LEFT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index > 0) {
					Command<CMD_SCH_DISPATCH_SWAP_SCHEDULES>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, CommandCallback::SwapSchDispatchSchedules, this->vehicle->index, this->schedule_index - 1, this->schedule_index);
				}
				break;

			case WID_SCHDISPATCH_MOVE_RIGHT:
				if (!this->IsScheduleSelected()) break;
				if (this->schedule_index < (int)(this->vehicle->orders->GetScheduledDispatchScheduleCount() - 1)) {
					Command<CMD_SCH_DISPATCH_SWAP_SCHEDULES>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, CommandCallback::SwapSchDispatchSchedules, this->vehicle->index, this->schedule_index + 1, this->schedule_index);
				}
				break;

			case WID_SCHDISPATCH_SLOT_DISPLAY_MODE: {
				this->slot_display_long_mode = !this->slot_display_long_mode;
				this->SetWidgetLoweredState(WID_SCHDISPATCH_SLOT_DISPLAY_MODE, this->slot_display_long_mode);
				this->ReInit();
				break;
			}
		}

		this->SetDirty();
	}

	static void ClearScheduleCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			SchdispatchWindow *w = (SchdispatchWindow*)win;
			if (w->IsScheduleSelected()) {
				Command<CMD_SCH_DISPATCH_CLEAR>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, w->vehicle->index, w->schedule_index);
			}
		}
	}

	static void RemoveScheduleCallback(Window *win, bool confirmed)
	{
		if (confirmed) {
			SchdispatchWindow *w = (SchdispatchWindow*)win;
			if (w->IsScheduleSelected()) {
				Command<CMD_SCH_DISPATCH_REMOVE_SCHEDULE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, w->vehicle->index, w->schedule_index);
			}
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (!this->TimeUnitsUsable()) return;

		switch (widget) {
			case WID_SCHDISPATCH_MANAGEMENT: {
				if (!this->IsScheduleSelected()) break;
				switch((ManagementDropdown)index & 0xFFFF) {
					case SCH_MD_RESET_LAST_DISPATCHED:
						Command<CMD_SCH_DISPATCH_RESET_LAST_DISPATCH>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index);
						break;

					case SCH_MD_CLEAR_SCHEDULE: {
						if (this->GetSelectedSchedule().GetScheduledDispatch().empty()) return;
						EncodedString msg = GetEncodedString(STR_SCHDISPATCH_QUERY_CLEAR_SCHEDULE_TEXT, this->GetSelectedSchedule().GetScheduledDispatch().size());
						ShowQuery(GetEncodedString(STR_SCHDISPATCH_QUERY_CLEAR_SCHEDULE_CAPTION), std::move(msg), this, ClearScheduleCallback);
						break;
					}

					case SCH_MD_REMOVE_SCHEDULE: {
						EncodedString msg = GetEncodedString(STR_SCHDISPATCH_QUERY_REMOVE_SCHEDULE_TEXT, this->GetSelectedSchedule().GetScheduledDispatch().size());
						ShowQuery(GetEncodedString(STR_SCHDISPATCH_QUERY_REMOVE_SCHEDULE_CAPTION), std::move(msg), this, RemoveScheduleCallback);
						break;
					}

					case SCH_MD_DUPLICATE_SCHEDULE:
						Command<CMD_SCH_DISPATCH_DUPLICATE_SCHEDULE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index);
						break;

					case SCH_MD_APPEND_VEHICLE_SCHEDULES: {
						static const CursorID clone_icons[] = {
							SPR_CURSOR_CLONE_TRAIN, SPR_CURSOR_CLONE_ROADVEH,
							SPR_CURSOR_CLONE_SHIP, SPR_CURSOR_CLONE_AIRPLANE
						};
						SetObjectToPlaceWnd(clone_icons[this->vehicle->type], PAL_NONE, HT_VEHICLE, this);
						break;
					}

					case SCH_MD_REUSE_DEPARTURE_SLOTS: {
						Command<CMD_SCH_DISPATCH_SET_REUSE_SLOTS>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index, !this->GetSelectedSchedule().GetScheduledDispatchReuseSlots());
						break;
					}

					case SCH_MD_RENAME_TAG: {
						this->clicked_widget = WID_SCHDISPATCH_MANAGEMENT;
						this->click_subaction = index;
						std::string_view str = this->GetSelectedSchedule().GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, index >> 16);
						ShowQueryString(str, STR_SCHDISPATCH_RENAME_DEPARTURE_TAG_CAPTION, MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::LengthIsInChars});
						break;
					}

					case SCH_MD_EDIT_ROUTE: {
						this->clicked_widget = WID_SCHDISPATCH_MANAGEMENT;
						this->click_subaction = index;
						uint16_t route_id = index >> 16;
						if (route_id != 0) {
							std::string_view str = this->GetSelectedSchedule().GetSupplementaryName(DispatchSchedule::SupplementaryNameType::RouteID, route_id);
							ShowQueryString(str, STR_SCHDISPATCH_RENAME_DEPARTURE_ROUTE_CAPTION, MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::EnableDefault, QueryStringFlag::DefaultIsDelete, QueryStringFlag::LengthIsInChars});
						} else {
							ShowQueryString({}, STR_SCHDISPATCH_RENAME_DEPARTURE_ROUTE_CAPTION, MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, {QueryStringFlag::LengthIsInChars});
						}
						break;
					}
				}
				break;
			}

			case WID_SCHDISPATCH_MANAGE_SLOT: {
				this->ValidateSelectedSlots();
				if (this->selected_slots.empty()) break;

				switch (index >> 16) {
					case 0: {
						uint16_t mask = 1 << (index & 0xFF);
						uint16_t values = HasBit(index, 8) ? mask : 0;
						Command<CMD_SCH_DISPATCH_SET_SLOT_FLAGS>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index, this->GetSelectedSlotSet(), values, mask);
						break;
					}

					case 1:
						Command<CMD_SCH_DISPATCH_SET_SLOT_ROUTE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, this->schedule_index, this->GetSelectedSlotSet(), index & 0xFFFF);
						break;
				}
				break;
			}

			default:
				break;
		}
	}

	virtual void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!this->TimeUnitsUsable()) return;

		if (!str.has_value()) return;
		const Vehicle *v = this->vehicle;

		switch (this->clicked_widget) {
			default: NOT_REACHED();

			case WID_SCHDISPATCH_SET_START_DATE: {
				if (!this->IsScheduleSelected()) break;

				if (str->empty()) break;

				auto try_value = ParseInteger<uint>(*str);
				if (try_value.has_value()) {
					uint minutes = (*try_value % 100) % 60;
					uint hours = (*try_value / 100) % 24;
					StateTicks start = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(hours, minutes));
					Command<CMD_SCH_DISPATCH_SET_START_DATE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, this->schedule_index, start);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DURATION: {
				if (!this->IsScheduleSelected()) break;
				Ticks val = ParseTimetableDuration(*str);

				if (val > 0) {
					Command<CMD_SCH_DISPATCH_SET_DURATION>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, this->schedule_index, val);
				}
				break;
			}

			case WID_SCHDISPATCH_SET_DELAY: {
				if (!this->IsScheduleSelected()) break;

				if (str->empty()) break;

				Command<CMD_SCH_DISPATCH_SET_DELAY>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, v->index, this->schedule_index, ParseTimetableDuration(*str));
				break;
			}

			case WID_SCHDISPATCH_RENAME: {
				if (!this->IsScheduleSelected()) break;

				Command<CMD_SCH_DISPATCH_RENAME_SCHEDULE>::Post(STR_ERROR_CAN_T_RENAME_SCHEDULE, v->index, this->schedule_index, *str);
				break;
			}

			case WID_SCHDISPATCH_ADJUST: {
				if (!this->IsScheduleSelected()) break;
				Ticks val = ParseTimetableDuration(*str);

				if (val != 0) {
					if (!this->adjust_slot_set.slots.empty()) {
						Command<CMD_SCH_DISPATCH_ADJUST_SLOT>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, CommandCallback::AdjustSchDispatchSlot, v->index, this->schedule_index, this->adjust_slot_set, val);
					} else {
						Command<CMD_SCH_DISPATCH_ADJUST>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, CommandCallback::AdjustSchDispatch, v->index, this->schedule_index, val);
					}
				}
				break;
			}

			case WID_SCHDISPATCH_MANAGEMENT: {
				switch (this->click_subaction & 0xFFFF) {
					case SCH_MD_RENAME_TAG:
						Command<CMD_SCH_DISPATCH_RENAME_TAG>::Post(STR_ERROR_CAN_T_RENAME_DEPARTURE_TAG, v->index, this->schedule_index, this->click_subaction >> 16, *str);
						break;

					case SCH_MD_EDIT_ROUTE:
						Command<CMD_SCH_DISPATCH_EDIT_ROUTE>::Post(STR_ERROR_CAN_T_RENAME_DEPARTURE_ROUTE, v->index, this->schedule_index, this->click_subaction >> 16, *str);
						break;
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

	bool OnVehicleSelect(const Vehicle *v) override
	{
		if (v->orders == nullptr || v->orders->GetScheduledDispatchScheduleCount() == 0) return false;

		Command<CMD_SCH_DISPATCH_APPEND_VEH_SCHEDULE>::Post(STR_ERROR_CAN_T_TIMETABLE_VEHICLE, this->vehicle->index, v->index);
		ResetObjectToPlace();
		return true;
	}

	const Vehicle *GetVehicle()
	{
		return this->vehicle;
	}

	void AddSingleDepartureSlot(uint mins, uint16_t slot_flags, DispatchSlotRouteID route_id)
	{
		if (!this->IsScheduleSelected()) return;
		StateTicks slot = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, mins));
		ScheduleAddIntl(this->vehicle->index, this->schedule_index, slot, 0, 0, slot_flags, route_id);
	}

	void AddMultipleDepartureSlots(uint start, uint step, uint end, uint16_t slot_flags, DispatchSlotRouteID route_id)
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

		StateTicks slot = _settings_time.FromTickMinutes(_settings_time.NowInTickMinutes().ToSameDayClockTime(0, start));
		ScheduleAddIntl(this->vehicle->index, this->schedule_index, slot, (end - start) / step, step * _settings_time.ticks_per_minute, slot_flags, route_id, wrap_mode);
	}
};

void CcAddNewSchDispatchSchedule(const CommandCost &result, VehicleID veh, StateTicks start_tick, uint32_t duration)
{
	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow *>(FindWindowById(WC_SCHDISPATCH_SLOTS, veh));
	if (w != nullptr) {
		w->schedule_index = INT_MAX;
		w->AutoSelectSchedule();
		w->ReInit();
	}
}

void CcSwapSchDispatchSchedules(const CommandCost &result, VehicleID veh, uint32_t schedule_index_1, uint32_t schedule_index_2)
{
	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow *>(FindWindowById(WC_SCHDISPATCH_SLOTS, veh));
	if (w != nullptr) {
		w->schedule_index = schedule_index_1;
		w->AutoSelectSchedule();
		w->ReInit();
	}
}

void CcAdjustSchDispatch(const CommandCost &result, VehicleID veh, uint32_t schedule_index, int32_t adjustment)
{
	if (!result.Succeeded()) return;

	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow *>(FindWindowById(WC_SCHDISPATCH_SLOTS, veh));
	if (w != nullptr && w->schedule_index == static_cast<int>(schedule_index)) {
		const DispatchSchedule &ds = w->GetSelectedSchedule();
		btree::btree_set<uint32_t> new_selection;
		for (uint32_t slot : w->selected_slots) {
			new_selection.insert(ds.AdjustScheduledDispatchOffset(slot, adjustment));
		}
		w->selected_slots = std::move(new_selection);
	}
}

void CcAdjustSchDispatchSlot(const CommandCost &result, VehicleID veh, uint32_t schedule_index, const ScheduledDispatchSlotSet &slots, int32_t adjustment)
{
	if (!result.Succeeded()) return;
	auto changes = result.GetLargeResult<ScheduledDispatchAdjustSlotResult>();
	if (changes == nullptr) return;

	SchdispatchWindow *w = dynamic_cast<SchdispatchWindow *>(FindWindowById(WC_SCHDISPATCH_SLOTS, veh));
	if (w != nullptr && w->schedule_index == static_cast<int>(schedule_index)) {
		btree::btree_set<uint32_t> new_selection;
		for (const ScheduledDispatchAdjustSlotResult::Change &change : changes->changes) {
			auto it = w->selected_slots.find(change.old_slot);
			if (it != w->selected_slots.end()) {
				new_selection.insert(change.new_slot);
				w->selected_slots.erase(it);
			}
		}
		w->selected_slots.insert(new_selection.begin(), new_selection.end());
	}
}

static constexpr NWidgetPart _nested_schdispatch_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_RENAME), SetAspect(WidgetDimensions::ASPECT_RENAME), SetSpriteTip(SPR_RENAME, STR_SCHDISPATCH_RENAME_SCHEDULE_TOOLTIP),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_MOVE_LEFT), SetMinimalSize(12, 14), SetSpriteTip(SPR_ARROW_LEFT, STR_SCHDISPATCH_MOVE_SCHEDULE),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_SCHDISPATCH_MOVE_RIGHT), SetMinimalSize(12, 14), SetSpriteTip(SPR_ARROW_RIGHT, STR_SCHDISPATCH_MOVE_SCHEDULE),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCHDISPATCH_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ENABLED), SetStringTip(STR_SCHDISPATCH_ENABLED, STR_NULL), SetFill(1, 1), SetResize(1, 0),
			NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_HEADER), SetAlignment(SA_CENTER), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_PREV), SetStringTip(STR_SCHDISPATCH_PREV_SCHEDULE, STR_SCHDISPATCH_PREV_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_NEXT), SetStringTip(STR_SCHDISPATCH_NEXT_SCHEDULE, STR_SCHDISPATCH_NEXT_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADD_SCHEDULE), SetStringTip(STR_SCHDISPATCH_ADD_SCHEDULE, STR_SCHDISPATCH_ADD_SCHEDULE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_MATRIX, COLOUR_GREY, WID_SCHDISPATCH_MATRIX), SetResize(1, 1), SetScrollbar(WID_SCHDISPATCH_V_SCROLL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SCHDISPATCH_V_SCROLL),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCHDISPATCH_SLOT_DISPLAY_MODE), SetSpriteTip(SPR_LARGE_SMALL_WINDOW, STR_SCHDISPATCH_SLOT_DISPLAY_MODE_TOOLTIP), SetAspect(WidgetDimensions::ASPECT_TOGGLE_SIZE),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADD), SetStringTip(STR_SCHDISPATCH_ADD, STR_SCHDISPATCH_ADD_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_ADJUST), SetStringTip(STR_SCHDISPATCH_ADJUST, STR_SCHDISPATCH_ADJUST_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCHDISPATCH_REMOVE), SetStringTip(STR_SCHDISPATCH_REMOVE, STR_SCHDISPATCH_REMOVE_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCHDISPATCH_MANAGE_SLOT), SetStringTip(STR_SCHDISPATCH_MANAGE_SLOT, STR_NULL), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_SCHDISPATCH_SUMMARY_PANEL), SetMinimalSize(400, 22), SetResize(1, 0), EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_START_DATE), SetStringTip(STR_SCHDISPATCH_START, STR_SCHDISPATCH_START_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DURATION), SetStringTip(STR_SCHDISPATCH_DURATION, STR_SCHDISPATCH_DURATION_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCHDISPATCH_SET_DELAY), SetStringTip(STR_SCHDISPATCH_DELAY, STR_SCHDISPATCH_DELAY_TOOLTIP), SetFill(1, 1), SetResize(1, 0),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCHDISPATCH_MANAGEMENT), SetStringTip(STR_SCHDISPATCH_MANAGE, STR_NULL), SetFill(1, 1), SetResize(1, 0),
			EndContainer(),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _schdispatch_desc(__FILE__, __LINE__,
	WDP_AUTO, "scheduled_dispatch_slots", 400, 130,
	WC_SCHDISPATCH_SLOTS, WC_VEHICLE_TIMETABLE,
	WindowDefaultFlag::Construction,
	_nested_schdispatch_widgets
);

/**
 * Show the slot dispatching slots
 * @param v The vehicle to show the slot dispatching slots for
 */
void ShowSchdispatchWindow(const Vehicle *v)
{
	AllocateWindowDescFront<SchdispatchWindow>(_schdispatch_desc, v->index);
}

enum ScheduledDispatchAddSlotsWindowWidgets : WidgetID {
	WID_SCHDISPATCH_ADD_SLOT_START_SEL,
	WID_SCHDISPATCH_ADD_SLOT_START_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_START_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT,
	WID_SCHDISPATCH_ADD_SLOT_STEP_SEL,
	WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_STEP_TEXTEDIT,
	WID_SCHDISPATCH_ADD_SLOT_END_SEL,
	WID_SCHDISPATCH_ADD_SLOT_END_HOUR,
	WID_SCHDISPATCH_ADD_SLOT_END_MINUTE,
	WID_SCHDISPATCH_ADD_SLOT_END_TEXTEDIT,
	WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON,
	WID_SCHDISPATCH_ADD_SLOT_START_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_END_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_REUSE_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_REUSE,
	WID_SCHDISPATCH_ADD_SLOT_TAG1_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_TAG2_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_TAG3_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_TAG4_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_TAG1,
	WID_SCHDISPATCH_ADD_SLOT_TAG2,
	WID_SCHDISPATCH_ADD_SLOT_TAG3,
	WID_SCHDISPATCH_ADD_SLOT_TAG4,
	WID_SCHDISPATCH_ADD_SLOT_ROUTE_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_ROUTE,
	WID_SCHDISPATCH_ADD_SLOT_ROUTE_SEL,
	WID_SCHDISPATCH_ADD_SLOT_MULTIPLE_TEXT,
	WID_SCHDISPATCH_ADD_SLOT_MULTIPLE,
};

struct ScheduledDispatchAddSlotsWindow : Window {
	static constexpr uint16_t MAX_TIME_CHARS = 5;
	ClockFaceMinutes start{};
	ClockFaceMinutes step{};
	ClockFaceMinutes end{};
	QueryString start_editbox{MAX_TIME_CHARS * MAX_CHAR_LENGTH, MAX_TIME_CHARS};
	QueryString step_editbox{MAX_TIME_CHARS * MAX_CHAR_LENGTH, MAX_TIME_CHARS};
	QueryString end_editbox{MAX_TIME_CHARS * MAX_CHAR_LENGTH, MAX_TIME_CHARS};

	uint16_t slot_flags = 0;
	DispatchSlotRouteID route_id = 0;
	bool multiple;
	const bool text_mode;
	std::array<std::string, 4> tag_names;
	std::vector<std::pair<DispatchSlotRouteID, std::string>> route_names;

	ScheduledDispatchAddSlotsWindow(WindowDesc &desc, WindowNumber window_number, SchdispatchWindow *parent, bool multiple) :
			Window(desc), multiple(multiple), text_mode(_settings_client.gui.timetable_start_text_entry)
	{
		this->Window::flags.Set(WindowFlag::NoTabFastForward);

		const DispatchSchedule &ds = parent->GetSelectedSchedule();
		this->start = _settings_time.ToTickMinutes(ds.GetScheduledDispatchStartTick()).ToClockFaceMinutes();
		this->step = ClockFaceMinutes{30};
		this->end = (_settings_time.ToTickMinutes(ds.GetScheduledDispatchStartTick() + ds.GetScheduledDispatchDuration()) - 1).ToClockFaceMinutes();

		if (this->text_mode) {
			format_buffer_sized<32> buf;
			auto fill = [&](const ClockFaceMinutes &mins, QueryString &editbox) {
				buf.format("{:04}", mins.ClockHHMM());
				editbox.text.Assign(buf);
				editbox.text.afilter = CS_NUMERAL;
				editbox.ok_button = WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON;
				buf.clear();
			};
			fill(this->start, this->start_editbox);
			fill(this->step, this->step_editbox);
			fill(this->end, this->end_editbox);
		}

		for (uint i = 0; i < 4; i++) {
			this->tag_names[i] = ds.GetSupplementaryName(DispatchSchedule::SupplementaryNameType::DepartureTag, i);
		}
		auto route_names_view = ds.GetSortedRouteIDNames();
		this->route_names.reserve(route_names_view.size());
		for (const std::pair<DispatchSlotRouteID, std::string_view> &it : route_names_view) {
			this->route_names.emplace_back(it.first, it.second);
		}

		this->parent = parent;
		this->CreateNestedTree();
		this->SetWidgetLoweredState(WID_SCHDISPATCH_ADD_SLOT_MULTIPLE, this->multiple);
		this->GetWidget<NWidgetStacked>(WID_SCHDISPATCH_ADD_SLOT_ROUTE_SEL)->SetDisplayedPlane(this->route_names.empty() ? SZSP_NONE : 0);
		this->SetupTimeDisplayPanes();
		this->FinishInitNested(window_number);
		this->querystrings[WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT] = &this->start_editbox;
		this->querystrings[WID_SCHDISPATCH_ADD_SLOT_STEP_TEXTEDIT] = &this->step_editbox;
		this->querystrings[WID_SCHDISPATCH_ADD_SLOT_END_TEXTEDIT] = &this->end_editbox;
		this->SetFocusedWidget(WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT);
	}

	void SetupTimeDisplayPanes()
	{
		const int time_plane = this->text_mode ? 1 : 0;
		this->GetWidget<NWidgetStacked>(WID_SCHDISPATCH_ADD_SLOT_START_SEL)->SetDisplayedPlane(time_plane);
		this->GetWidget<NWidgetStacked>(WID_SCHDISPATCH_ADD_SLOT_STEP_SEL)->SetDisplayedPlane(this->multiple ? time_plane : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_SCHDISPATCH_ADD_SLOT_END_SEL)->SetDisplayedPlane(this->multiple ? time_plane : SZSP_NONE);
	}

	EventState OnKeyPress(char32_t key, uint16_t keycode) override
	{
		if (keycode == WKC_TAB && this->multiple && this->nested_focus != nullptr) {
			auto focus_wid = this->nested_focus->GetIndex();
			switch (focus_wid) {
				case WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT:
					this->SetFocusedWidget(WID_SCHDISPATCH_ADD_SLOT_STEP_TEXTEDIT);
					break;
				case WID_SCHDISPATCH_ADD_SLOT_STEP_TEXTEDIT:
					this->SetFocusedWidget(WID_SCHDISPATCH_ADD_SLOT_END_TEXTEDIT);
					break;
				case WID_SCHDISPATCH_ADD_SLOT_END_TEXTEDIT:
					this->SetFocusedWidget(WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT);
					break;
				default:
					return ES_NOT_HANDLED;
			}
			return ES_HANDLED;
		} else {
			return ES_NOT_HANDLED;
		}
	}

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, int window_number) override
	{
		Point pt = { this->parent->left + this->parent->width / 2 - sm_width / 2, this->parent->top + this->parent->height / 2 - sm_height / 2 };
		return pt;
	}

	virtual void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		Dimension d = {0, 0};
		switch (widget) {
			default: return;

			case WID_SCHDISPATCH_ADD_SLOT_START_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_END_TEXT:
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_TIME));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_START));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END));
				break;

			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:
			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE:
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:
				d = maxdim(d, GetStringBoundingBox(GetString(STR_JUST_INT, GetParamMaxDigits(2))));
				break;

			case WID_SCHDISPATCH_ADD_SLOT_TAG1_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG2_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG3_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG4_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_ROUTE_TEXT:
				d = maxdim(d, GetStringBoundingBox(this->GetWidgetString(widget, STR_NULL)));
				break;

			case WID_SCHDISPATCH_ADD_SLOT_ROUTE:
				d = maxdim(d, GetStringBoundingBox(STR_ORDER_CONDITIONAL_DISPATCH_SLOT_DEF_ROUTE));
				for (const auto &it : this->route_names) {
					d = maxdim(d, GetStringBoundingBox(it.second));
				}
				break;

			case WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON:
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD));
				d = maxdim(d, GetStringBoundingBox(STR_SCHDISPATCH_ADD_MULTIPLE_SLOTS));
				break;
		}

		d.width += padding.width;
		d.height += padding.height;
		size = d;
	}

	virtual std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		switch (widget) {
			case WID_SCHDISPATCH_ADD_SLOT_START_TEXT:
				return GetString(this->multiple ? STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_START : STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_TIME);

			case WID_SCHDISPATCH_ADD_SLOT_START_HOUR:   return GetString(STR_JUST_INT, start.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_START_MINUTE: return GetString(STR_JUST_INT, start.ClockMinute()); break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR:    return GetString(STR_JUST_INT, step.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE:  return GetString(STR_JUST_INT, step.ClockMinute()); break;
			case WID_SCHDISPATCH_ADD_SLOT_END_HOUR:     return GetString(STR_JUST_INT, end.ClockHour()); break;
			case WID_SCHDISPATCH_ADD_SLOT_END_MINUTE:   return GetString(STR_JUST_INT, end.ClockMinute()); break;

			case WID_SCHDISPATCH_ADD_SLOT_TAG1_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG2_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG3_TEXT:
			case WID_SCHDISPATCH_ADD_SLOT_TAG4_TEXT: {
				const uint tag = widget - WID_SCHDISPATCH_ADD_SLOT_TAG1_TEXT;
				return GetString(this->tag_names[tag].empty() ? STR_SCHDISPATCH_TAG_DEPARTURE : STR_SCHDISPATCH_TAG_DEPARTURE_NAMED, tag + 1, this->tag_names[tag]);
			}

			case WID_SCHDISPATCH_ADD_SLOT_ROUTE_TEXT:
				return GetString(STR_SCHDISPATCH_ROUTE, std::string_view{});

			case WID_SCHDISPATCH_ADD_SLOT_ROUTE:
				if (this->route_id == 0) return GetString(STR_ORDER_CONDITIONAL_DISPATCH_SLOT_DEF_ROUTE);
				for (const auto &it : this->route_names) {
					if (it.first == this->route_id) return it.second;
				}
				return {};

			case WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON:
				return GetString(this->multiple ? STR_SCHDISPATCH_ADD_MULTIPLE_SLOTS : STR_SCHDISPATCH_ADD);

			default: return this->Window::GetWidgetString(widget, stringid);
		}
	}

	virtual void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		auto handle_hours_dropdown = [&](ClockFaceMinutes current) {
			DropDownList list;
			for (uint i = 0; i < 24; i++) {
				list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, i), i, false));
			}
			ShowDropDownList(this, std::move(list), current.ClockHour(), widget);
		};

		auto handle_minutes_dropdown = [&](ClockFaceMinutes current) {
			DropDownList list;
			for (uint i = 0; i < 60; i++) {
				list.push_back(MakeDropDownListStringItem(GetString(STR_JUST_INT, i), i, false));
			}
			ShowDropDownList(this, std::move(list), current.ClockMinute(), widget);
		};

		switch (widget) {
			case WID_SCHDISPATCH_ADD_SLOT_MULTIPLE:
				this->multiple = !this->multiple;
				this->SetWidgetLoweredState(widget, this->multiple);
				this->SetupTimeDisplayPanes();
				this->ReInit();
				this->SetFocusedWidget(WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT);
				break;

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
			case WID_SCHDISPATCH_ADD_SLOT_REUSE:
				ToggleBit(this->slot_flags, DispatchSlot::SDSF_REUSE_SLOT);
				this->SetWidgetLoweredState(widget, HasBit(this->slot_flags, DispatchSlot::SDSF_REUSE_SLOT));
				this->SetWidgetDirty(widget);
				break;
			case WID_SCHDISPATCH_ADD_SLOT_TAG1:
			case WID_SCHDISPATCH_ADD_SLOT_TAG2:
			case WID_SCHDISPATCH_ADD_SLOT_TAG3:
			case WID_SCHDISPATCH_ADD_SLOT_TAG4: {
				const uint8_t flag_bit =  DispatchSlot::SDSF_FIRST_TAG + (widget - WID_SCHDISPATCH_ADD_SLOT_TAG1);
				ToggleBit(this->slot_flags, flag_bit);
				this->SetWidgetLoweredState(widget, HasBit(this->slot_flags, flag_bit));
				this->SetWidgetDirty(widget);
				break;
			}

			case WID_SCHDISPATCH_ADD_SLOT_ROUTE:
				if (!this->route_names.empty()) {
					DropDownList list;
					list.push_back(MakeDropDownListStringItem(STR_ORDER_CONDITIONAL_DISPATCH_SLOT_DEF_ROUTE, 0));

					for (const auto &it : route_names) {
						list.push_back(MakeDropDownListStringItem(std::string{it.second}, it.first));
					}
					ShowDropDownList(this, std::move(list), this->route_id, widget);
				}
				break;

			case WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON:
				if (!this->HandleTimeText(this->start, this->start_editbox, this->multiple ? STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_START : STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_TIME)) break;
				if (this->multiple) {
					if (!this->HandleTimeText(this->step, this->step_editbox, STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP)) break;
					if (!this->HandleTimeText(this->end, this->end_editbox, STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END)) break;
					static_cast<SchdispatchWindow *>(this->parent)->AddMultipleDepartureSlots(this->start.base(), this->step.base(), this->end.base(), this->slot_flags, this->route_id);
				} else {
					static_cast<SchdispatchWindow *>(this->parent)->AddSingleDepartureSlot(this->start.base(), this->slot_flags, this->route_id);
				}
				this->Close();
				break;
		}
	}

	virtual void OnDropdownSelect(WidgetID widget, int index, int) override
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
			case WID_SCHDISPATCH_ADD_SLOT_ROUTE:
				this->route_id = static_cast<DispatchSlotRouteID>(index);
				break;
		}

		this->SetWidgetDirty(widget);
	}

	bool HandleTimeTextParse(ClockFaceMinutes &mins, const QueryString &editbox) const
	{
		if (!this->text_mode) return true;
		auto result = IntFromChars<int32_t>(editbox.text.GetText());
		if (!result.has_value() || *result < 0) return false;
		uint hours = (*result / 100) % 24;
		uint minutes = (*result % 100);
		if (minutes >= 60) return false;
		mins = ClockFaceMinutes::FromClockFace(hours, minutes);
		return true;
	}

	bool HandleTimeText(ClockFaceMinutes &mins, const QueryString &editbox, StringID label) const
	{
		bool ok = this->HandleTimeTextParse(mins, editbox);
		if (!ok) {
			ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR_INVALID_VALUE, editbox.text.GetText(), strip_leading_colours(GetString(label))), {}, WL_INFO);
		}
		return ok;
	}
};

static constexpr NWidgetPart _nested_scheduled_dispatch_add_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetTextStyle(TC_WHITE | TC_FORCED), SetStringTip(STR_SCHDISPATCH_ADD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(NWID_VERTICAL), SetPIP(6, 6, 6),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_MULTIPLE_TEXT), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_MULTIPLE, STR_NULL),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_MULTIPLE),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_START_SEL),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_START_TEXT),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_START_HOUR), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_HOUR_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_START_MINUTE), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_MINUTE_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_START_TEXT),
					NWidget(WWT_EDITBOX, COLOUR_GREY, WID_SCHDISPATCH_ADD_SLOT_START_TEXTEDIT), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_STEP_SEL),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_STEP_HOUR), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_HOUR_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_STEP_MINUTE), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_MINUTE_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_STEP_TEXT), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_STEP, STR_NULL),
					NWidget(WWT_EDITBOX, COLOUR_GREY, WID_SCHDISPATCH_ADD_SLOT_STEP_TEXTEDIT), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_END_SEL),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_END_TEXT), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END, STR_NULL),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_END_HOUR), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_HOUR_TOOLTIP),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_END_MINUTE), SetFill(1, 0), SetToolTip(STR_DATE_MINUTES_MINUTE_TOOLTIP),
				EndContainer(),
				NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_END_TEXT), SetStringTip(STR_SCHDISPATCH_ADD_DEPARTURE_SLOTS_END, STR_NULL),
					NWidget(WWT_EDITBOX, COLOUR_GREY, WID_SCHDISPATCH_ADD_SLOT_END_TEXTEDIT), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_REUSE_TEXT), SetFill(1, 0), SetResize(1, 0), SetStringTip(STR_SCHDISPATCH_REUSE_DEPARTURE_SLOTS_SHORT, STR_NULL),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_REUSE),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_TAG1_TEXT), SetFill(1, 0), SetResize(1, 0),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_TAG1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_TAG2_TEXT), SetFill(1, 0), SetResize(1, 0),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_TAG2),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_TAG3_TEXT), SetFill(1, 0), SetResize(1, 0),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_TAG3),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
				NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_TAG4_TEXT), SetFill(1, 0), SetResize(1, 0),
				NWidget(WWT_BOOLBTN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_TAG4),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_ROUTE_SEL),
				NWidget(NWID_HORIZONTAL), SetPIP(6, 6, 6),
					NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCHDISPATCH_ADD_SLOT_ROUTE_TEXT), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_ORANGE, WID_SCHDISPATCH_ADD_SLOT_ROUTE),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SPACER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_SCHDISPATCH_ADD_SLOT_ADD_BUTTON), SetMinimalSize(100, 12), SetToolTip(STR_SCHDISPATCH_ADD_TOOLTIP),
				NWidget(NWID_SPACER), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static WindowDesc _scheduled_dispatch_add_desc(__FILE__, __LINE__,
	WDP_CENTER, nullptr, 0, 0,
	WC_SET_DATE, WC_NONE,
	{},
	_nested_scheduled_dispatch_add_widgets
);

void ShowScheduledDispatchAddSlotsWindow(SchdispatchWindow *parent, WindowNumber window_number, bool multiple)
{
	CloseWindowByClass(WC_SET_DATE);

	new ScheduledDispatchAddSlotsWindow(_scheduled_dispatch_add_desc, window_number, parent, multiple);
}

void SchdispatchInvalidateWindows(const Vehicle *v)
{
	if (_pause_mode.Any()) InvalidateWindowClassesData(WC_DEPARTURES_BOARD);

	if (!HaveWindowByClass(WC_VEHICLE_TIMETABLE) && !HaveWindowByClass(WC_SCHDISPATCH_SLOTS) && !HaveWindowByClass(WC_VEHICLE_ORDERS)) return;

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
