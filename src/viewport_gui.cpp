/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file viewport_gui.cpp Extra viewport window. */

#include "stdafx.h"
#include "landscape.h"
#include "window_gui.h"
#include "viewport_func.h"
#include "strings_func.h"
#include "tunnelbridge.h"
#include "tilehighlight_func.h"
#include "zoom_func.h"
#include "window_func.h"
#include "gfx_func.h"
#include "industry.h"
#include "town.h"
#include "town_map.h"

#include "widgets/viewport_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

/* Extra Viewport Window Stuff */
static const NWidgetPart _nested_extra_viewport_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_EV_CAPTION), SetDataTip(STR_EXTRA_VIEWPORT_TITLE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_EV_VIEWPORT), SetPadding(2, 2, 2, 2), SetResize(1, 1), SetFill(1, 1),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_EV_ZOOM_IN), SetDataTip(SPR_IMG_ZOOMIN, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_IN),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_EV_ZOOM_OUT), SetDataTip(SPR_IMG_ZOOMOUT, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_OUT),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_EV_MAIN_TO_VIEW), SetFill(1, 1), SetResize(1, 0),
										SetDataTip(STR_EXTRA_VIEW_MOVE_MAIN_TO_VIEW, STR_EXTRA_VIEW_MOVE_MAIN_TO_VIEW_TT),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_EV_VIEW_TO_MAIN), SetFill(1, 1), SetResize(1, 0),
										SetDataTip(STR_EXTRA_VIEW_MOVE_VIEW_TO_MAIN, STR_EXTRA_VIEW_MOVE_VIEW_TO_MAIN_TT),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), SetResize(1, 0), EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

class ExtraViewportWindow : public Window {
public:
	ExtraViewportWindow(WindowDesc *desc, int window_number, TileIndex tile) : Window(desc)
	{
		this->InitNested(window_number);

		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_EV_VIEWPORT);
		nvp->InitializeViewport(this, 0, ScaleZoomGUI(ZOOM_LVL_VIEWPORT));
		if (_settings_client.gui.zoom_min == viewport->zoom) this->DisableWidget(WID_EV_ZOOM_IN);

		Point pt;
		if (tile == INVALID_TILE) {
			/* No tile? Use center of main viewport. */
			const Window *w = GetMainWindow();

			/* center on same place as main window (zoom is maximum, no adjustment needed) */
			pt.x = w->viewport->scrollpos_x + w->viewport->virtual_width / 2;
			pt.y = w->viewport->scrollpos_y + w->viewport->virtual_height / 2;
		} else {
			pt = RemapCoords(TileX(tile) * TILE_SIZE + TILE_SIZE / 2, TileY(tile) * TILE_SIZE + TILE_SIZE / 2, TilePixelHeight(tile));
		}

		this->viewport->scrollpos_x = pt.x - this->viewport->virtual_width / 2;
		this->viewport->scrollpos_y = pt.y - this->viewport->virtual_height / 2;
		this->viewport->dest_scrollpos_x = this->viewport->scrollpos_x;
		this->viewport->dest_scrollpos_y = this->viewport->scrollpos_y;
		this->viewport->map_type = (ViewportMapType) _settings_client.gui.default_viewport_map_mode;
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_EV_CAPTION:
				/* set the number in the title bar */
				SetDParam(0, this->window_number + 1);
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_EV_ZOOM_IN: DoZoomInOutWindow(ZOOM_IN,  this); break;
			case WID_EV_ZOOM_OUT: DoZoomInOutWindow(ZOOM_OUT, this); break;

			case WID_EV_MAIN_TO_VIEW: { // location button (move main view to same spot as this view) 'Paste Location'
				Window *w = GetMainWindow();
				int x = this->viewport->scrollpos_x; // Where is the main looking at
				int y = this->viewport->scrollpos_y;

				/* set this view to same location. Based on the center, adjusting for zoom */
				w->viewport->dest_scrollpos_x =  x - (w->viewport->virtual_width -  this->viewport->virtual_width) / 2;
				w->viewport->dest_scrollpos_y =  y - (w->viewport->virtual_height - this->viewport->virtual_height) / 2;
				w->viewport->follow_vehicle   = INVALID_VEHICLE;
				break;
			}

			case WID_EV_VIEW_TO_MAIN: { // inverse location button (move this view to same spot as main view) 'Copy Location'
				const Window *w = GetMainWindow();
				int x = w->viewport->scrollpos_x;
				int y = w->viewport->scrollpos_y;

				this->viewport->dest_scrollpos_x =  x + (w->viewport->virtual_width -  this->viewport->virtual_width) / 2;
				this->viewport->dest_scrollpos_y =  y + (w->viewport->virtual_height - this->viewport->virtual_height) / 2;
				break;
			}
		}
	}

	void OnResize() override
	{
		if (this->viewport != nullptr) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_EV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);
		}
	}

	void OnScroll(Point delta) override
	{
		this->viewport->scrollpos_x += ScaleByZoom(delta.x, this->viewport->zoom);
		this->viewport->scrollpos_y += ScaleByZoom(delta.y, this->viewport->zoom);
		this->viewport->dest_scrollpos_x = this->viewport->scrollpos_x;
		this->viewport->dest_scrollpos_y = this->viewport->scrollpos_y;
	}

	bool OnRightClick(Point pt, int widget) override
	{
		return widget == WID_EV_VIEWPORT;
	}

	void OnMouseWheel(int wheel) override
	{
		if (_ctrl_pressed) {
			/* Cycle through the drawing modes */
			ChangeRenderMode(this->viewport, wheel < 0);
			this->SetDirty();
		} else if (_settings_client.gui.scrollwheel_scrolling != 2) {
			ZoomInOrOutToCursorWindow(wheel < 0, this);
		}
	}

	virtual void OnMouseOver(Point pt, int widget) override
	{
		if (pt.x != -1 && IsViewportMouseHoverActive()) {
			/* Show tooltip with last month production or town name */
			const Point p = GetTileBelowCursor();
			const TileIndex tile = TileVirtXY(p.x, p.y);
			if (tile < MapSize()) ShowTooltipForTile(this, tile);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* Only handle zoom message if intended for us (msg ZOOM_IN/ZOOM_OUT) */
		HandleZoomMessage(this, this->viewport, WID_EV_ZOOM_IN, WID_EV_ZOOM_OUT);
	}
};

static WindowDesc _extra_viewport_desc(
	WDP_AUTO, "extra_viewport", 300, 268,
	WC_EXTRA_VIEWPORT, WC_NONE,
	0,
	_nested_extra_viewport_widgets, lengthof(_nested_extra_viewport_widgets)
);

/**
 * Show a new Extra Viewport window.
 * @param tile Tile to center the view on. INVALID_TILE means to use the center of main viewport.
 */
void ShowExtraViewportWindow(TileIndex tile)
{
	int i = 0;

	/* find next free window number for extra viewport */
	while (FindWindowById(WC_EXTRA_VIEWPORT, i) != nullptr) i++;

	new ExtraViewportWindow(&_extra_viewport_desc, i, tile);
}

/**
 * Show a new Extra Viewport window.
 * When building a tunnel, the tunnel end-tile is used as center for new viewport.
 * Otherwise center it on the tile under the cursor, if the cursor is inside a viewport.
 * If that fails, center it on main viewport center.
 */
void ShowExtraViewportWindowForTileUnderCursor()
{
	if (_build_tunnel_endtile != 0 && _thd.place_mode & HT_TUNNEL) {
		ShowExtraViewportWindow(_build_tunnel_endtile);
		return;
	}

	/* Use tile under mouse as center for new viewport.
	 * Do this before creating the window, it might appear just below the mouse. */
	Point pt = GetTileBelowCursor();
	ShowExtraViewportWindow(pt.x != -1 ? TileVirtXY(pt.x, pt.y) : INVALID_TILE);
}

enum TownNameTooltipMode : uint8 {
	TNTM_OFF,
	TNTM_ON_IF_HIDDEN,
	TNTM_ALWAYS_ON
};

enum StationTooltipNameMode : uint8 {
	STNM_OFF,
	STNM_ON_IF_HIDDEN,
	STNM_ALWAYS_ON
};

void ShowTownNameTooltip(Window *w, const TileIndex tile)
{
	if (_settings_client.gui.town_name_tooltip_mode == TNTM_OFF) return;
	if (HasBit(_display_opt, DO_SHOW_TOWN_NAMES) && _settings_client.gui.town_name_tooltip_mode == TNTM_ON_IF_HIDDEN) return; // No need for a town name tooltip when it is already displayed

	TownID town_id = GetTownIndex(tile);
	const Town *town = Town::Get(town_id);

	if (_settings_client.gui.population_in_label) {
		SetDParam(0, STR_TOWN_NAME_POP_TOOLTIP);
		SetDParam(1, town_id);
		SetDParam(2, town->cache.population);
	} else {
		SetDParam(0, STR_TOWN_NAME_TOOLTIP);
		SetDParam(1, town_id);
	}

	StringID tooltip_string;
	if (_game_mode == GM_NORMAL && _local_company < MAX_COMPANIES && HasBit(town->have_ratings, _local_company)) {
		const int local_authority_rating_thresholds[] = { RATING_APPALLING, RATING_VERYPOOR, RATING_POOR, RATING_MEDIOCRE, RATING_GOOD, RATING_VERYGOOD,
													RATING_EXCELLENT, RATING_OUTSTANDING };
		constexpr size_t threshold_count = lengthof(local_authority_rating_thresholds);

		int local_rating = town->ratings[_local_company];
		StringID rating_string = STR_CARGO_RATING_APPALLING;
		for (size_t i = 0; i < threshold_count && local_rating > local_authority_rating_thresholds[i]; ++i) ++rating_string;
		SetDParam(3, rating_string);
		tooltip_string = STR_TOWN_NAME_RATING_TOOLTIP;
	} else {
		tooltip_string = STR_JUST_STRING2;
	}
	GuiShowTooltips(w, tooltip_string, 0, nullptr, TCC_HOVER_VIEWPORT);
}

void ShowStationViewportTooltip(Window *w, const TileIndex tile)
{
	const StationID station_id = GetStationIndex(tile);
	const Station *station = Station::Get(station_id);

	std::string msg;

	if ( _settings_client.gui.station_viewport_tooltip_name == STNM_ALWAYS_ON ||
			(_settings_client.gui.station_viewport_tooltip_name == STNM_ON_IF_HIDDEN && !HasBit(_display_opt, DO_SHOW_STATION_NAMES))) {
		SetDParam(0, station_id);
		SetDParam(1, station->facilities);
		msg = GetString(STR_STATION_VIEW_NAME_TOOLTIP);
	}

	if (_settings_client.gui.station_viewport_tooltip_cargo) {
		for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
			const GoodsEntry *goods_entry = &station->goods[cs->Index()];
			if (!goods_entry->HasRating()) continue;

			if (!msg.empty()) msg += '\n';

			SetDParam(0, cs->name);
			SetDParam(1, ToPercent8(goods_entry->rating));
			SetDParam(2, cs->Index());
			SetDParam(3, goods_entry->cargo.TotalCount());
			msg += GetString(STR_STATION_VIEW_CARGO_LINE_TOOLTIP);
		}
	}

	if (!msg.empty()) {
		_temp_special_strings[0] = std::move(msg);
		GuiShowTooltips(w, SPECSTR_TEMP_START, 0, nullptr, TCC_HOVER_VIEWPORT);
	}
}

void ShowTooltipForTile(Window *w, const TileIndex tile)
{
	extern void ShowDepotTooltip(Window *w, const TileIndex tile);
	extern void ShowIndustryTooltip(Window *w, const TileIndex tile);

	switch (GetTileType(tile)) {
		case MP_ROAD:
			if (IsRoadDepot(tile)) {
				ShowDepotTooltip(w, tile);
				return;
			}
			/* FALL THROUGH */
		case MP_HOUSE: {
			ShowTownNameTooltip(w, tile);
			break;
		}
		case MP_INDUSTRY: {
			ShowIndustryTooltip(w, tile);
			break;
		}
		case MP_RAILWAY: {
			if (!IsRailDepot(tile)) return;
			ShowDepotTooltip(w, tile);
			break;
		}
		case MP_WATER: {
			if (!IsShipDepot(tile)) return;
			ShowDepotTooltip(w, tile);
			break;
		}
		case MP_STATION: {
			if (IsHangar(tile)) {
				ShowDepotTooltip(w, tile);
			} else {
				ShowStationViewportTooltip(w, tile);
			}
			break;
		}
		default:
			return;
	}
}
