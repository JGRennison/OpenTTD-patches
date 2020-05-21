/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures_gui.cpp Scheduled departures from a station. */

#include "stdafx.h"
#include "debug.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "window_gui.h"
#include "timetable.h"
#include "vehiclelist.h"
#include "company_base.h"
#include "date_func.h"
#include "departures_gui.h"
#include "station_base.h"
#include "vehicle_gui_base.h"
#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "order_base.h"
#include "settings_type.h"
#include "core/smallvec_type.hpp"
#include "date_type.h"
#include "company_type.h"
#include "departures_func.h"
#include "cargotype.h"

#include "table/sprites.h"
#include "table/strings.h"

static const NWidgetPart _nested_departures_list[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_DB_CAPTION), SetDataTip(STR_DEPARTURES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_DB_LIST), SetMinimalSize(0, 0), SetFill(1, 0), SetResize(1, 1), SetScrollbar(WID_DB_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_DB_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetResize(1, 0), SetFill(1, 1), EndContainer(),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_PAX), SetMinimalSize(6, 12), SetFill(0, 1), SetDataTip(STR_DEPARTURES_PAX, STR_DEPARTURES_PAX_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_FREIGHT), SetMinimalSize(6, 12), SetFill(0, 1), SetDataTip(STR_DEPARTURES_FREIGHT, STR_DEPARTURES_FREIGHT_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_ARRS), SetMinimalSize(6, 12), SetFill(0, 1), SetDataTip(STR_DEPARTURES_ARRIVALS, STR_DEPARTURES_ARRIVALS_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_DEPS), SetMinimalSize(6, 12), SetFill(0, 1), SetDataTip(STR_DEPARTURES_DEPARTURES, STR_DEPARTURES_DEPARTURES_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_VIA), SetMinimalSize(11, 12), SetFill(0, 1), SetDataTip(STR_DEPARTURES_VIA_BUTTON, STR_DEPARTURES_VIA_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_TRAINS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_TRAIN, STR_STATION_VIEW_SCHEDULED_TRAINS_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_ROADVEHS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_LORRY, STR_STATION_VIEW_SCHEDULED_ROAD_VEHICLES_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_SHIPS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_SHIP, STR_STATION_VIEW_SCHEDULED_SHIPS_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_DB_SHOW_PLANES),  SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_PLANE, STR_STATION_VIEW_SCHEDULED_AIRCRAFT_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _departures_desc(
	WDP_AUTO, nullptr, 260, 246,
	WC_DEPARTURES_BOARD, WC_NONE,
	0,
	_nested_departures_list, lengthof(_nested_departures_list)
);

static uint cached_date_width = 0;         ///< The cached maximum width required to display a date.
static uint cached_status_width = 0;       ///< The cached maximum width required to show the status field.
static uint cached_date_arrow_width = 0;   ///< The cached width of the red/green arrows that may be displayed alongside times.
static uint cached_veh_type_width = 0;     ///< The cached width of the vehicle type icon.
static bool cached_date_display_method;    ///< Whether the above cached values refers to original (d,m,y) dates or the 24h clock.
static bool cached_arr_dep_display_method; ///< Whether to show departures and arrivals on a single line.

void FlushDeparturesWindowTextCaches()
{
	cached_date_width = cached_status_width = cached_date_arrow_width = cached_veh_type_width = 0;
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
}

template<bool Twaypoint = false>
struct DeparturesWindow : public Window {
protected:
	StationID station;         ///< The station whose departures we're showing.
	DepartureList *departures; ///< The current list of departures from this station.
	DepartureList *arrivals;   ///< The current list of arrivals from this station.
	bool departures_invalid;   ///< The departures and arrivals list are currently invalid.
	uint entry_height;         ///< The height of an entry in the departures list.
	uint tick_count;           ///< The number of ticks that have elapsed since the window was created. Used for scrolling text.
	int calc_tick_countdown;   ///< The number of ticks to wait until recomputing the departure list. Signed in case it goes below zero.
	bool show_types[4];        ///< The vehicle types to show in the departure list.
	bool departure_types[3];   ///< The types of departure to show in the departure list.
	bool show_pax;             ///< Show passenger vehicles
	bool show_freight;         ///< Show freight vehicles
	bool cargo_buttons_disabled;///< Show pax/freight buttons disabled
	uint min_width;            ///< The minimum width of this window.
	Scrollbar *vscroll;
	std::vector<const Vehicle *> vehicles; /// current set of vehicles
	int veh_width;                         /// current width of vehicle field
	int group_width;                       /// current width of group field
	int toc_width;                         /// current width of company field

	virtual uint GetMinWidth() const;
	static void RecomputeDateWidth();
	virtual void DrawDeparturesListItems(const Rect &r) const;
	void DeleteDeparturesList(DepartureList* list);

	void ToggleCargoFilter(int widget, bool &flag)
	{
		flag = !flag;
		this->SetWidgetLoweredState(widget, flag);
		/* We need to recompute the departures list. */
		this->calc_tick_countdown = 0;
		/* We need to redraw the button that was pressed. */
		this->SetWidgetDirty(widget);
	}

	void SetCargoFilterDisabledState()
	{
		this->cargo_buttons_disabled = _settings_client.gui.departure_only_passengers;
		this->SetWidgetDisabledState(WID_DB_SHOW_PAX, cargo_buttons_disabled);
		this->SetWidgetDisabledState(WID_DB_SHOW_FREIGHT, cargo_buttons_disabled);
		if (this->cargo_buttons_disabled) {
			this->show_pax = true;
			this->LowerWidget(WID_DB_SHOW_PAX);
			this->show_freight = false;
			this->RaiseWidget(WID_DB_SHOW_FREIGHT);
		}
	}

	void FillVehicleList()
	{
		this->vehicles.clear();
		this->veh_width = 0;
		this->group_width = 0;
		this->toc_width = 0;

		btree::btree_set<GroupID> groups;
		CompanyMask companies = 0;
		int unitnumber_max[4] = { -1, -1, -1, -1 };

		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->type < 4 && this->show_types[v->type] && v->IsPrimaryVehicle()) {
				const Order *order;

				FOR_VEHICLE_ORDERS(v, order) {
					if ((order->IsType(OT_GOTO_STATION) || order->IsType(OT_GOTO_WAYPOINT) || order->IsType(OT_IMPLICIT))
							&& order->GetDestination() == this->station) {
						this->vehicles.push_back(v);

						if (v->name.empty()) {
							if (v->unitnumber > unitnumber_max[v->type]) unitnumber_max[v->type] = v->unitnumber;
						} else {
							SetDParam(0, (uint64)(v->index));
							int width = (GetStringBoundingBox(STR_DEPARTURES_VEH)).width;
							if (width > this->veh_width) this->veh_width = width;
						}

						if (v->group_id != INVALID_GROUP && v->group_id != DEFAULT_GROUP) {
							groups.insert(v->group_id);
						}

						SetBit(companies, v->owner);
						break;
					}
				}
			}
		}

		for (uint i = 0; i < 4; i++) {
			if (unitnumber_max[i] >= 0) {
				uint unitnumber_digits = 2;
				if (unitnumber_max[i] >= 10000) {
					unitnumber_digits = 5;
				} else if (unitnumber_max[i] >= 1000) {
					unitnumber_digits = 4;
				} else if (unitnumber_max[i] >= 100) {
					unitnumber_digits = 3;
				}
				SetDParamMaxDigits(0, unitnumber_digits);
				int width = (GetStringBoundingBox(STR_SV_TRAIN_NAME + i)).width + 4;
				if (width > this->veh_width) this->veh_width = width;
			}
		}

		for (GroupID gid : groups) {
			SetDParam(0, (uint64)gid);
			int width = (GetStringBoundingBox(STR_DEPARTURES_GROUP)).width + 4;
			if (width > this->group_width) this->group_width = width;
		}

		uint owner;
		FOR_EACH_SET_BIT(owner, companies) {
			SetDParam(0, owner);
			int width = (GetStringBoundingBox(STR_DEPARTURES_TOC)).width + 4;
			if (width > this->toc_width) this->toc_width = width;
		}
	}

	void RefreshVehicleList() {
		this->FillVehicleList();
		this->calc_tick_countdown = 0;
	}

public:

	DeparturesWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc),
		station(window_number),
		departures(new DepartureList()),
		arrivals(new DepartureList()),
		departures_invalid(true),
		entry_height(1 + FONT_HEIGHT_NORMAL + 1 + (_settings_client.gui.departure_larger_font ? FONT_HEIGHT_NORMAL : FONT_HEIGHT_SMALL) + 1 + 1),
		tick_count(0),
		calc_tick_countdown(0),
		min_width(400)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_DB_SCROLLBAR);
		this->FinishInitNested(window_number);

		/* By default, only show departures. */
		departure_types[0] = true;
		departure_types[1] = false;
		departure_types[2] = false;
		show_pax = true;
		show_freight = true;
		this->LowerWidget(WID_DB_SHOW_DEPS);
		this->RaiseWidget(WID_DB_SHOW_ARRS);
		this->RaiseWidget(WID_DB_SHOW_VIA);
		this->LowerWidget(WID_DB_SHOW_PAX);
		this->LowerWidget(WID_DB_SHOW_FREIGHT);
		this->SetCargoFilterDisabledState();

		for (uint i = 0; i < 4; ++i) {
			show_types[i] = true;
			this->LowerWidget(WID_DB_SHOW_TRAINS + i);
		}

		if (Twaypoint) {
			this->GetWidget<NWidgetCore>(WID_DB_CAPTION)->SetDataTip(STR_DEPARTURES_CAPTION_WAYPOINT, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS);

			for (uint i = 0; i < 4; ++i) {
				this->DisableWidget(WID_DB_SHOW_TRAINS + i);
			}

			this->DisableWidget(WID_DB_SHOW_ARRS);
			this->DisableWidget(WID_DB_SHOW_DEPS);
			this->DisableWidget(WID_DB_SHOW_VIA);

			departure_types[2] = true;

			this->LowerWidget(WID_DB_SHOW_VIA);
		}

		if (cached_veh_type_width == 0) {
			cached_veh_type_width = GetStringBoundingBox(STR_DEPARTURES_TYPE_PLANE).width;
		}

		this->RefreshVehicleList();

		if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
	}

	virtual ~DeparturesWindow()
	{
		this->DeleteDeparturesList(departures);
		this->DeleteDeparturesList(this->arrivals);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_DB_LIST:
				resize->height = DeparturesWindow::entry_height;
				size->height = 2 * resize->height;
				break;
		}
	}

	virtual void SetStringParameters(int widget) const override
	{
		if (widget == WID_DB_CAPTION) {
			const Station *st = Station::Get(this->station);
			SetDParam(0, st->index);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_DB_SHOW_TRAINS:   // Show trains to this station
			case WID_DB_SHOW_ROADVEHS: // Show road vehicles to this station
			case WID_DB_SHOW_SHIPS:    // Show ships to this station
			case WID_DB_SHOW_PLANES: {  // Show aircraft to this station
				if (_ctrl_pressed) {
					for (int w = WID_DB_SHOW_TRAINS; w <= WID_DB_SHOW_PLANES; w++) {
						if (w == widget) {
							this->show_types[w - WID_DB_SHOW_TRAINS] = true;
							this->LowerWidget(w);
						} else {
							this->show_types[w - WID_DB_SHOW_TRAINS] = false;
							this->RaiseWidget(w);
						}
						this->SetWidgetDirty(w);
					}
				} else {
					this->show_types[widget - WID_DB_SHOW_TRAINS] = !this->show_types[widget - WID_DB_SHOW_TRAINS];
					if (this->show_types[widget - WID_DB_SHOW_TRAINS]) {
						this->LowerWidget(widget);
					}
					else {
						this->RaiseWidget(widget);
					}
					this->SetWidgetDirty(widget);
				}
				/* We need to recompute the departures list. */
				this->RefreshVehicleList();
				/* We need to redraw the button that was pressed. */
				if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
				break;
			}

			case WID_DB_SHOW_DEPS:
			case WID_DB_SHOW_ARRS:
				if (_settings_client.gui.departure_show_both) break;
				/* FALL THROUGH */

			case WID_DB_SHOW_VIA:

				this->departure_types[widget - WID_DB_SHOW_DEPS] = !this->departure_types[widget - WID_DB_SHOW_DEPS];
				if (this->departure_types[widget - WID_DB_SHOW_DEPS]) {
					this->LowerWidget(widget);
				} else {
					this->RaiseWidget(widget);
				}

				if (!this->departure_types[0]) {
					this->RaiseWidget(WID_DB_SHOW_VIA);
					this->DisableWidget(WID_DB_SHOW_VIA);
				} else {
					this->EnableWidget(WID_DB_SHOW_VIA);

					if (this->departure_types[2]) {
						this->LowerWidget(WID_DB_SHOW_VIA);
					}
				}
				/* We need to recompute the departures list. */
				this->calc_tick_countdown = 0;
				/* We need to redraw the button that was pressed. */
				this->SetWidgetDirty(widget);
				if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
				break;

			case WID_DB_LIST: {  // Matrix to show departures
				if (this->departures_invalid) return;

				/* We need to find the departure corresponding to where the user clicked. */
				uint32 id_v = (pt.y - this->GetWidget<NWidgetBase>(WID_DB_LIST)->pos_y) / this->entry_height;

				if (id_v >= this->vscroll->GetCapacity()) return; // click out of bounds

				id_v += this->vscroll->GetPosition();

				if (id_v >= (this->departures->size() + this->arrivals->size())) return; // click out of list bound

				uint departure = 0;
				uint arrival = 0;

				/* Draw each departure. */
				for (uint i = 0; i <= id_v; ++i) {
					const Departure *d;

					if (arrival == this->arrivals->size()) {
						d = (*(this->departures))[departure++];
					} else if (departure == this->departures->size()) {
						d = (*(this->arrivals))[arrival++];
					} else {
						d = (*(this->departures))[departure];
						const Departure *a = (*(this->arrivals))[arrival];

						if (a->scheduled_date < d->scheduled_date) {
							d = a;
							arrival++;
						} else {
							departure++;
						}
					}

					if (i == id_v) {
						ShowVehicleViewWindow(d->vehicle);
						break;
					}
				}

				break;
			}

			case WID_DB_SHOW_PAX:
				this->ToggleCargoFilter(widget, this->show_pax);
				if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
				break;

			case WID_DB_SHOW_FREIGHT:
				this->ToggleCargoFilter(widget, this->show_freight);
				if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
				break;
		}
	}

	virtual void OnGameTick() override
	{
		if (_pause_mode == PM_UNPAUSED) {
			this->tick_count += 1;
			this->calc_tick_countdown -= 1;
		}

		/* Recompute the minimum date display width if the cached one is no longer valid. */
		if (cached_date_width == 0 ||
				_settings_client.gui.time_in_minutes != cached_date_display_method ||
				_settings_client.gui.departure_show_both != cached_arr_dep_display_method) {
			this->RecomputeDateWidth();
		}

		if (this->cargo_buttons_disabled != _settings_client.gui.departure_only_passengers) {
			this->SetCargoFilterDisabledState();
			this->calc_tick_countdown = 0;
			this->SetWidgetDirty(WID_DB_SHOW_PAX);
			this->SetWidgetDirty(WID_DB_SHOW_FREIGHT);
		}

		/* We need to redraw the scrolling text in its new position. */
		this->SetWidgetDirty(WID_DB_LIST);

		/* Recompute the list of departures if we're due to. */
		if (this->calc_tick_countdown <= 0) {
			this->calc_tick_countdown = _settings_client.gui.departure_calc_frequency;
			this->DeleteDeparturesList(this->departures);
			this->DeleteDeparturesList(this->arrivals);
			bool show_pax = _settings_client.gui.departure_only_passengers ? true : this->show_pax;
			bool show_freight = _settings_client.gui.departure_only_passengers ? false : this->show_freight;
			this->departures = (this->departure_types[0] ? MakeDepartureList(this->station, this->vehicles, D_DEPARTURE, Twaypoint || this->departure_types[2], show_pax, show_freight) : new DepartureList());
			this->arrivals   = (this->departure_types[1] && !_settings_client.gui.departure_show_both ? MakeDepartureList(this->station, this->vehicles, D_ARRIVAL, false, show_pax, show_freight) : new DepartureList());
			this->departures_invalid = false;
			this->SetWidgetDirty(WID_DB_LIST);
		}

		uint new_width = this->GetMinWidth();

		if (new_width != this->min_width) {
			NWidgetCore *n = this->GetWidget<NWidgetCore>(WID_DB_LIST);
			n->SetMinimalSize(new_width, 0);
			this->ReInit();
			this->min_width = new_width;
		}

		uint new_height = 1 + FONT_HEIGHT_NORMAL + 1 + (_settings_client.gui.departure_larger_font ? FONT_HEIGHT_NORMAL : FONT_HEIGHT_SMALL) + 1 + 1;

		if (new_height != this->entry_height) {
			this->entry_height = new_height;
			this->SetWidgetDirty(WID_DB_LIST);
			this->ReInit();
		}
	}

	virtual void OnRealtimeTick(uint delta_ms) override
	{
		if (_pause_mode != PM_UNPAUSED && this->calc_tick_countdown <= 0) {
			this->OnGameTick();
		}
	}

	virtual void OnPaint() override
	{
		if (Twaypoint || _settings_client.gui.departure_show_both) {
			this->DisableWidget(WID_DB_SHOW_ARRS);
			this->DisableWidget(WID_DB_SHOW_DEPS);
		} else {
			this->EnableWidget(WID_DB_SHOW_ARRS);
			this->EnableWidget(WID_DB_SHOW_DEPS);
		}

		this->vscroll->SetCount(min(_settings_client.gui.max_departures, this->departures->size() + this->arrivals->size()));
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_DB_LIST:
				this->DrawDeparturesListItems(r);
				break;
		}
	}

	virtual void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_DB_LIST);
		this->GetWidget<NWidgetCore>(WID_DB_LIST)->widget_data = (this->vscroll->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		this->RefreshVehicleList();
		this->departures_invalid = true;
	}
};

/**
 * Shows a window of scheduled departures for a station.
 * @param station the station to show a departures window for
 */
void ShowStationDepartures(StationID station)
{
	AllocateWindowDescFront<DeparturesWindow<> >(&_departures_desc, station);
}

/**
 * Shows a window of scheduled departures for a station.
 * @param station the station to show a departures window for
 */
void ShowWaypointDepartures(StationID waypoint)
{
	AllocateWindowDescFront<DeparturesWindow<true> >(&_departures_desc, waypoint);
}

template<bool Twaypoint>
void DeparturesWindow<Twaypoint>::RecomputeDateWidth()
{
	cached_date_width = 0;
	cached_status_width = 0;
	cached_date_display_method = _settings_client.gui.time_in_minutes;
	cached_arr_dep_display_method = _settings_client.gui.departure_show_both;

	cached_status_width = max((GetStringBoundingBox(STR_DEPARTURES_ON_TIME)).width, cached_status_width);
	cached_status_width = max((GetStringBoundingBox(STR_DEPARTURES_DELAYED)).width, cached_status_width);
	cached_status_width = max((GetStringBoundingBox(STR_DEPARTURES_CANCELLED)).width, cached_status_width);

	uint interval = cached_date_display_method ? _settings_client.gui.ticks_per_minute : DAY_TICKS;
	uint count = cached_date_display_method ? 24*60 : 365;

	for (uint i = 0; i < count; ++i) {
		SetDParam(0, INT_MAX - (i*interval));
		SetDParam(1, INT_MAX - (i*interval));
		cached_date_width = max(GetStringBoundingBox(cached_arr_dep_display_method ? STR_DEPARTURES_TIME_BOTH : STR_DEPARTURES_TIME_DEP).width, cached_date_width);
		cached_status_width = max((GetStringBoundingBox(STR_DEPARTURES_EXPECTED)).width, cached_status_width);
	}

	SetDParam(0, 0);
	cached_date_arrow_width = GetStringBoundingBox(STR_DEPARTURES_TIME_DEP).width - GetStringBoundingBox(STR_DEPARTURES_TIME).width;

	if (!_settings_client.gui.departure_show_both) {
		cached_date_width -= cached_date_arrow_width;
	}
}

template<bool Twaypoint>
uint DeparturesWindow<Twaypoint>::GetMinWidth() const
{
	uint result = 0;

	/* Time */
	result = cached_date_width;

	/* Vehicle type icon */
	result += _settings_client.gui.departure_show_vehicle_type ? cached_veh_type_width : 0;

	/* Status */
	result += cached_status_width;

	result += this->toc_width + this->veh_width + this->group_width;

	return result + 140;
}

/**
 * Deletes this window's departure list.
 */
template<bool Twaypoint>
void DeparturesWindow<Twaypoint>::DeleteDeparturesList(DepartureList *list)
{
	/* SmallVector uses free rather than delete on its contents (which doesn't invoke the destructor), so we need to delete each departure manually. */
	for (uint i = 0; i < list->size(); ++i) {
		Departure **d = &(*list)[i];
		delete *d;
		/* Make sure a double free doesn't happen. */
		*d = nullptr;
	}
	delete list;
	list = nullptr;
}

/**
 * Draws a list of departures.
 */
template<bool Twaypoint>
void DeparturesWindow<Twaypoint>::DrawDeparturesListItems(const Rect &r) const
{
	int left = r.left + WD_MATRIX_LEFT;
	int right = r.right - WD_MATRIX_RIGHT;

	bool rtl = _current_text_dir == TD_RTL;
	bool ltr = !rtl;

	int text_offset = WD_FRAMERECT_RIGHT;
	int text_left  = left  + (rtl ?           0 : text_offset);
	int text_right = right - (rtl ? text_offset :           0);

	int y = r.top + 1;
	uint max_departures = min(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->departures->size() + this->arrivals->size());

	if (max_departures > _settings_client.gui.max_departures) {
		max_departures = _settings_client.gui.max_departures;
	}

	byte small_font_size = _settings_client.gui.departure_larger_font ? FONT_HEIGHT_NORMAL : FONT_HEIGHT_SMALL;

	/* Draw the black background. */
	GfxFillRect(r.left + 1, r.top, r.right - 1, r.bottom, PC_BLACK);

	/* Nothing selected? Then display the information text. */
	bool none_selected[2] = {true, true};
	for (uint i = 0; i < 4; ++i)
	{
		if (this->show_types[i]) {
			none_selected[0] = false;
			break;
		}
	}

	for (uint i = 0; i < 2; ++i)
	{
		if (this->departure_types[i]) {
			none_selected[1] = false;
			break;
		}
	}

	if (none_selected[0] || none_selected[1]) {
		DrawString(text_left, text_right, y + 1, STR_DEPARTURES_NONE_SELECTED);
		return;
	}

	/* No scheduled departures? Then display the information text. */
	if (max_departures == 0) {
		DrawString(text_left, text_right, y + 1, STR_DEPARTURES_EMPTY);
		return;
	}

	/* Find the maximum possible width of the departure time and "Expt <time>" fields. */
	int time_width = cached_date_width;

	if (!_settings_client.gui.departure_show_both) {
		time_width += (departure_types[0] && departure_types[1] ? cached_date_arrow_width : 0);
	}

	/* Vehicle type icon */
	int type_width = _settings_client.gui.departure_show_vehicle_type ? cached_veh_type_width : 0;

	/* Find the maximum width of the status field */
	int status_width = cached_status_width;

	/* Find the width of the "Calling at:" field. */
	int calling_at_width = (GetStringBoundingBox(_settings_client.gui.departure_larger_font ? STR_DEPARTURES_CALLING_AT_LARGE : STR_DEPARTURES_CALLING_AT)).width;

	/* Find the maximum company name width. */
	int toc_width = _settings_client.gui.departure_show_company ? this->toc_width : 0;

	/* Find the maximum group name width. */
	int group_width = _settings_client.gui.departure_show_group ? this->group_width : 0;

	/* Find the maximum vehicle name width. */
	int veh_width = _settings_client.gui.departure_show_vehicle ? this->veh_width : 0;

	uint departure = 0;
	uint arrival = 0;

	DateTicksScaled now_date = _scaled_date_ticks;
	DateTicksScaled max_date = now_date + (_settings_client.gui.max_departure_time * DAY_TICKS * _settings_game.economy.day_length_factor);

	/* Draw each departure. */
	for (uint i = 0; i < max_departures; ++i) {
		const Departure *d;

		if (arrival == this->arrivals->size()) {
			d = (*(this->departures))[departure++];
		} else if (departure == this->departures->size()) {
			d = (*(this->arrivals))[arrival++];
		} else {
			d = (*(this->departures))[departure];
			const Departure *a = (*(this->arrivals))[arrival];

			if (a->scheduled_date < d->scheduled_date) {
				d = a;
				arrival++;
			} else {
				departure++;
			}
		}

		if (i < this->vscroll->GetPosition()) {
			continue;
		}

		/* If for some reason the departure is too far in the future or is at a negative time, skip it. */
		if (d->scheduled_date > max_date || d->scheduled_date < 0) {
			continue;
		}

		if (d->terminus == INVALID_STATION) continue;

		StringID time_str = (departure_types[0] && departure_types[1]) ? (d->type == D_DEPARTURE ? STR_DEPARTURES_TIME_DEP : STR_DEPARTURES_TIME_ARR) : STR_DEPARTURES_TIME;

		if (_settings_client.gui.departure_show_both) time_str = STR_DEPARTURES_TIME_BOTH;

		/* Time */
		SetDParam(0, d->scheduled_date);
		SetDParam(1, d->scheduled_date - (d->scheduled_waiting_time > 0 ? d->scheduled_waiting_time : d->order->GetWaitTime()));
		ltr ? DrawString(              text_left, text_left + time_width, y + 1, time_str)
			: DrawString(text_right - time_width,             text_right, y + 1, time_str);

		/* Vehicle type icon, with thanks to sph */
		if (_settings_client.gui.departure_show_vehicle_type) {
			StringID type = STR_DEPARTURES_TYPE_TRAIN;
			int offset = (_settings_client.gui.departure_show_vehicle_color ? 1 : 0);

			switch (d->vehicle->type) {
				case VEH_TRAIN:
					type = STR_DEPARTURES_TYPE_TRAIN;
					break;
				case VEH_ROAD:
					type = IsCargoInClass(d->vehicle->cargo_type, CC_PASSENGERS) ? STR_DEPARTURES_TYPE_BUS : STR_DEPARTURES_TYPE_LORRY;
					break;
				case VEH_SHIP:
					type = STR_DEPARTURES_TYPE_SHIP;
					break;
				case VEH_AIRCRAFT:
					type = STR_DEPARTURES_TYPE_PLANE;
					break;
				default:
					break;
			}

			type += offset;

			DrawString(text_left + time_width + 3, text_left + time_width + type_width + 3, y, type);
		}

		/* The icons to show with the destination and via stations. */
		StringID icon = STR_DEPARTURES_STATION_NONE;
		StringID icon_via = STR_DEPARTURES_STATION_NONE;

		if (_settings_client.gui.departure_destination_type) {
			Station *t = Station::Get(d->terminus.station);

			if (t->facilities & FACIL_DOCK &&
					t->facilities & FACIL_AIRPORT &&
					d->vehicle->type != VEH_SHIP &&
					d->vehicle->type != VEH_AIRCRAFT) {
				icon = STR_DEPARTURES_STATION_PORTAIRPORT;
			} else if (t->facilities & FACIL_DOCK &&
					d->vehicle->type != VEH_SHIP) {
				icon = STR_DEPARTURES_STATION_PORT;
			} else if (t->facilities & FACIL_AIRPORT &&
					d->vehicle->type != VEH_AIRCRAFT) {
				icon = STR_DEPARTURES_STATION_AIRPORT;
			}
		}

		if (_settings_client.gui.departure_destination_type && d->via != INVALID_STATION) {
			Station *t = Station::Get(d->via);

			if (t->facilities & FACIL_DOCK &&
					t->facilities & FACIL_AIRPORT &&
					d->vehicle->type != VEH_SHIP &&
					d->vehicle->type != VEH_AIRCRAFT) {
				icon_via = STR_DEPARTURES_STATION_PORTAIRPORT;
			} else if (t->facilities & FACIL_DOCK &&
					d->vehicle->type != VEH_SHIP) {
				icon_via = STR_DEPARTURES_STATION_PORT;
			} else if (t->facilities & FACIL_AIRPORT &&
					d->vehicle->type != VEH_AIRCRAFT) {
				icon_via = STR_DEPARTURES_STATION_AIRPORT;
			}
		}

		/* Destination */
		if (d->via == INVALID_STATION) {
			/* Only show the terminus. */
			SetDParam(0, d->terminus.station);
			SetDParam(1, icon);
			ltr ? DrawString(              text_left + time_width + type_width + 6,   text_right - status_width - (toc_width + veh_width + group_width + 2) - 2, y + 1, STR_DEPARTURES_TERMINUS)
				: DrawString(text_left + status_width + (toc_width + veh_width + group_width + 2) + 2,                 text_right - time_width - type_width - 6, y + 1, STR_DEPARTURES_TERMINUS);
		} else {
			/* Show the terminus and the via station. */
			SetDParam(0, d->terminus.station);
			SetDParam(1, icon);
			SetDParam(2, d->via);
			SetDParam(3, icon_via);
			int text_width = (GetStringBoundingBox(STR_DEPARTURES_TERMINUS_VIA_STATION)).width;

			if (text_width < text_right - status_width - (toc_width + veh_width + group_width + 2) - 2 - (text_left + time_width + type_width + 6)) {
				/* They will both fit, so show them both. */
				SetDParam(0, d->terminus.station);
				SetDParam(1, icon);
				SetDParam(2, d->via);
				SetDParam(3, icon_via);
				ltr ? DrawString(              text_left + time_width + type_width + 6, text_right - status_width - (toc_width + veh_width + group_width + 2) - 2, y + 1, STR_DEPARTURES_TERMINUS_VIA_STATION)
					: DrawString(text_left + status_width + (toc_width + veh_width + group_width + 2) + 2,               text_right - time_width - type_width - 6, y + 1, STR_DEPARTURES_TERMINUS_VIA_STATION);
			} else {
				/* They won't both fit, so switch between showing the terminus and the via station approximately every 4 seconds. */
				if (this->tick_count & (1 << 7)) {
					SetDParam(0, d->via);
					SetDParam(1, icon_via);
					ltr ? DrawString(              text_left + time_width + type_width + 6, text_right - status_width - (toc_width + veh_width + group_width + 2) - 2, y + 1, STR_DEPARTURES_VIA)
						: DrawString(text_left + status_width + (toc_width + veh_width + group_width + 2) + 2,               text_right - time_width - type_width - 6, y + 1, STR_DEPARTURES_VIA);
				} else {
					SetDParam(0, d->terminus.station);
					SetDParam(1, icon);
					ltr ? DrawString(              text_left + time_width + type_width + 6, text_right - status_width - (toc_width + veh_width + group_width + 2) - 2, y + 1, STR_DEPARTURES_TERMINUS_VIA)
						: DrawString(text_left + status_width + (toc_width + veh_width + group_width + 2) + 2,               text_right - time_width - type_width - 6, y + 1, STR_DEPARTURES_TERMINUS_VIA);
				}
			}
		}

		/* Status */
		{
			int status_left = ltr ? text_right - status_width - 2 - (toc_width + veh_width + group_width + 2) : text_left + (toc_width + veh_width + group_width + 2) + 2;
			int status_right = ltr ? text_right - (toc_width + veh_width + group_width + 2) + 2 : text_left + status_width + 2 + (toc_width + veh_width + group_width + 2);

			if (d->status == D_ARRIVED) {
				/* The vehicle has arrived. */
				DrawString(status_left, status_right, y + 1, STR_DEPARTURES_ARRIVED);
			} else if(d->status == D_CANCELLED) {
				/* The vehicle has been cancelled. */
				DrawString(status_left, status_right, y + 1, STR_DEPARTURES_CANCELLED);
			} else{
				if (d->lateness <= DATE_UNIT_SIZE && d->scheduled_date > now_date) {
					/* We have no evidence that the vehicle is late, so assume it is on time. */
					DrawString(status_left, status_right, y + 1, STR_DEPARTURES_ON_TIME);
				} else {
					if ((d->scheduled_date + d->lateness) < now_date) {
						/* The vehicle was expected to have arrived by now, even if we knew it was going to be late. */
						/* We assume that the train stays at least a day at a station so it won't accidentally be marked as delayed for a fraction of a day. */
						DrawString(status_left, status_right, y + 1, STR_DEPARTURES_DELAYED);
					} else {
						/* The vehicle is expected to be late and is not yet due to arrive. */
						SetDParam(0, d->scheduled_date + d->lateness);
						DrawString(status_left, status_right, y + 1, STR_DEPARTURES_EXPECTED);
					}
				}
			}
		}

		/* Vehicle name */

		if (_settings_client.gui.departure_show_vehicle) {
			SetDParam(0, (uint64)(d->vehicle->index));
			ltr ? DrawString(text_right - (toc_width + veh_width + group_width + 2),              text_right - toc_width - group_width - 2, y + 1, STR_DEPARTURES_VEH)
				: DrawString(               text_left + toc_width + group_width + 2, text_left + (toc_width + veh_width + group_width + 2), y + 1, STR_DEPARTURES_VEH);
		}

		/* Group name */

		if (_settings_client.gui.departure_show_group && d->vehicle->group_id != INVALID_GROUP && d->vehicle->group_id != DEFAULT_GROUP) {
			SetDParam(0, (uint64)(d->vehicle->group_id));
			ltr ? DrawString(text_right - (toc_width + group_width + 2),              text_right - toc_width - 2, y + 1, STR_DEPARTURES_GROUP)
				: DrawString(               text_left + toc_width + 2, text_left + (toc_width + group_width + 2), y + 1, STR_DEPARTURES_GROUP);
		}

		/* Operating company */
		if (_settings_client.gui.departure_show_company) {
			SetDParam(0, (uint64)(d->vehicle->owner));
			ltr ? DrawString(text_right - toc_width,            text_right, y + 1, STR_DEPARTURES_TOC, TC_FROMSTRING, SA_RIGHT)
				: DrawString(             text_left, text_left + toc_width, y + 1, STR_DEPARTURES_TOC, TC_FROMSTRING, SA_LEFT);
		}

		int bottom_y = y + this->entry_height - small_font_size - (_settings_client.gui.departure_larger_font ? 1 : 3);

		/* Calling at */
		ltr ? DrawString(                    text_left,  text_left + calling_at_width, bottom_y, _settings_client.gui.departure_larger_font ? STR_DEPARTURES_CALLING_AT_LARGE : STR_DEPARTURES_CALLING_AT)
			: DrawString(text_right - calling_at_width,                    text_right, bottom_y, _settings_client.gui.departure_larger_font ? STR_DEPARTURES_CALLING_AT_LARGE : STR_DEPARTURES_CALLING_AT);

		/* List of stations */
		/* RTL languages can be handled in the language file, e.g. by having the following: */
		/* STR_DEPARTURES_CALLING_AT_STATION      :{STATION}, {RAW_STRING} */
		/* STR_DEPARTURES_CALLING_AT_LAST_STATION :{STATION} & {RAW_STRING}*/
		char buffer[512], scratch[512];

		if (d->calling_at.size() != 0) {
			SetDParam(0, (uint64)(d->calling_at[0]).station);
			GetString(scratch, STR_DEPARTURES_CALLING_AT_FIRST_STATION, lastof(scratch));

			StationID continuesTo = INVALID_STATION;

			if (d->calling_at[0].station == d->terminus.station && d->calling_at.size() > 1) {
				continuesTo = d->calling_at[d->calling_at.size() - 1].station;
			} else if (d->calling_at.size() > 1) {
				/* There's more than one stop. */

				uint i;
				/* For all but the last station, write out ", <station>". */
				for (i = 1; i < d->calling_at.size() - 1; ++i) {
					StationID s = d->calling_at[i].station;
					if (s == d->terminus.station) {
						continuesTo = d->calling_at[d->calling_at.size() - 1].station;
						break;
					}
					SetDParam(0, (uint64)scratch);
					SetDParam(1, (uint64)s);
					GetString(buffer, STR_DEPARTURES_CALLING_AT_STATION, lastof(buffer));
					strncpy(scratch, buffer, sizeof(scratch));
				}

				/* Finally, finish off with " and <station>". */
				SetDParam(0, (uint64)scratch);
				SetDParam(1, (uint64)d->calling_at[i].station);
				GetString(buffer, STR_DEPARTURES_CALLING_AT_LAST_STATION, lastof(buffer));
				strncpy(scratch, buffer, sizeof(scratch));
			}

			SetDParam(0, (uint64)scratch);
			StringID string;
			if (continuesTo == INVALID_STATION) {
				string = _settings_client.gui.departure_larger_font ? STR_DEPARTURES_CALLING_AT_LIST_LARGE : STR_DEPARTURES_CALLING_AT_LIST;
			} else {
				SetDParam(1, continuesTo);
				string = _settings_client.gui.departure_larger_font ? STR_DEPARTURES_CALLING_AT_LIST_SMART_TERMINUS_LARGE : STR_DEPARTURES_CALLING_AT_LIST_SMART_TERMINUS;
			}
			GetString(buffer, string, lastof(buffer));
		} else {
			buffer[0] = 0;
			//SetDParam(0, d->terminus);
			//GetString(scratch, STR_DEPARTURES_CALLING_AT_FIRST_STATION, lastof(scratch));
		}

		int list_width = (GetStringBoundingBox(buffer, _settings_client.gui.departure_larger_font ? FS_NORMAL : FS_SMALL)).width;

		/* Draw the whole list if it will fit. Otherwise scroll it. */
		if (list_width < text_right - (text_left + calling_at_width + 2)) {
			ltr ? DrawString(text_left + calling_at_width + 2,                        text_right, bottom_y, buffer)
				: DrawString(                       text_left, text_right - calling_at_width - 2, bottom_y, buffer);
		} else {
			DrawPixelInfo tmp_dpi;
			if (ltr
				? !FillDrawPixelInfo(&tmp_dpi, text_left + calling_at_width + 2, bottom_y, text_right - (text_left + calling_at_width + 2), small_font_size + 3)
				: !FillDrawPixelInfo(&tmp_dpi, text_left                       , bottom_y, text_right - (text_left + calling_at_width + 2), small_font_size + 3)) {
				y += this->entry_height;
				continue;
			}
			DrawPixelInfo *old_dpi = _cur_dpi;
			_cur_dpi = &tmp_dpi;

			/* The scrolling text starts out of view at the right of the screen and finishes when it is out of view at the left of the screen. */
			int pos = ltr
				? text_right - (this->tick_count % (list_width + text_right - text_left))
				:  text_left + (this->tick_count % (list_width + text_right - text_left));

			ltr ? DrawString(       pos, INT16_MAX, 0, buffer, TC_FROMSTRING,  SA_LEFT | SA_FORCE)
				: DrawString(-INT16_MAX,       pos, 0, buffer, TC_FROMSTRING, SA_RIGHT | SA_FORCE);

			_cur_dpi = old_dpi;
		}

		y += this->entry_height;
	}
}
