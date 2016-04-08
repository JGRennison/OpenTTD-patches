/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_gui.cpp GUI for towns. */

#include "stdafx.h"
#include "town.h"
#include "viewport_func.h"
#include "error.h"
#include "gui.h"
#include "command_func.h"
#include "company_func.h"
#include "company_base.h"
#include "company_gui.h"
#include "core/random_func.hpp"
#include "network/network.h"
#include "string_func.h"
#include "strings_func.h"
#include "sound_func.h"
#include "tilehighlight_func.h"
#include "sortlist_type.h"
#include "road_cmd.h"
#include "landscape.h"
#include "querystring_gui.h"
#include "window_func.h"
#include "townname_func.h"
#include "core/geometry_func.hpp"
#include "genworld.h"
#include "widgets/dropdown_func.h"
#include "newgrf_config.h"
#include "newgrf_house.h"
#include "date_func.h"
#include "zoom_func.h"

#include "widgets/town_widget.h"

#include "table/strings.h"

#include <algorithm>

#include "safeguards.h"

typedef GUIList<const Town*> GUITownList;

static CommandCost ListTownsToJoinHouseTo(HouseID house, TileIndex tile, TownList *towns);
static void ShowSelectTownWindow(const TownList &towns, const CommandContainer &cmd);

static const NWidgetPart _nested_town_authority_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TA_CAPTION), SetDataTip(STR_LOCAL_AUTHORITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_RATING_INFO), SetMinimalSize(317, 92), SetResize(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_COMMAND_LIST), SetMinimalSize(305, 52), SetResize(1, 0), SetDataTip(0x0, STR_LOCAL_AUTHORITY_ACTIONS_TOOLTIP), SetScrollbar(WID_TA_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_TA_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_ACTION_INFO), SetMinimalSize(317, 52), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TA_EXECUTE),  SetMinimalSize(317, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_LOCAL_AUTHORITY_DO_IT_BUTTON, STR_LOCAL_AUTHORITY_DO_IT_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer()
};

/** Town authority window. */
struct TownAuthorityWindow : Window {
private:
	Town *town;    ///< Town being displayed.
	int sel_index; ///< Currently selected town action, \c 0 to \c TACT_COUNT-1, \c -1 means no action selected.
	Scrollbar *vscroll;
	uint displayed_actions_on_previous_painting; ///< Actions that were available on the previous call to OnPaint()

	/**
	 * Get the position of the Nth set bit.
	 *
	 * If there is no Nth bit set return -1
	 *
	 * @param bits The value to search in
	 * @param n The Nth set bit from which we want to know the position
	 * @return The position of the Nth set bit
	 */
	static int GetNthSetBit(uint32 bits, int n)
	{
		if (n >= 0) {
			uint i;
			FOR_EACH_SET_BIT(i, bits) {
				n--;
				if (n < 0) return i;
			}
		}
		return -1;
	}

public:
	TownAuthorityWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc), sel_index(-1), displayed_actions_on_previous_painting(0)
	{
		this->town = Town::Get(window_number);
		this->InitNested(window_number);
		this->vscroll = this->GetScrollbar(WID_TA_SCROLLBAR);
		this->vscroll->SetCapacity((this->GetWidget<NWidgetBase>(WID_TA_COMMAND_LIST)->current_y - WD_FRAMERECT_TOP - WD_FRAMERECT_BOTTOM) / FONT_HEIGHT_NORMAL);
	}

	virtual void OnPaint()
	{
		int numact;
		uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
		if (buttons != displayed_actions_on_previous_painting) this->SetDirty();
		displayed_actions_on_previous_painting = buttons;

		this->vscroll->SetCount(numact + 1);

		if (this->sel_index != -1 && !HasBit(buttons, this->sel_index)) {
			this->sel_index = -1;
		}

		this->SetWidgetDisabledState(WID_TA_EXECUTE, this->sel_index == -1);

		this->DrawWidgets();
		if (!this->IsShaded()) this->DrawRatings();
	}

	/** Draw the contents of the ratings panel. May request a resize of the window if the contents does not fit. */
	void DrawRatings()
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_TA_RATING_INFO);
		uint left = nwid->pos_x + WD_FRAMERECT_LEFT;
		uint right = nwid->pos_x + nwid->current_x - 1 - WD_FRAMERECT_RIGHT;

		uint y = nwid->pos_y + WD_FRAMERECT_TOP;

		DrawString(left, right, y, STR_LOCAL_AUTHORITY_COMPANY_RATINGS);
		y += FONT_HEIGHT_NORMAL;

		Dimension icon_size = GetSpriteSize(SPR_COMPANY_ICON);
		int icon_width      = icon_size.width;
		int icon_y_offset   = (FONT_HEIGHT_NORMAL - icon_size.height) / 2;

		Dimension exclusive_size = GetSpriteSize(SPR_EXCLUSIVE_TRANSPORT);
		int exclusive_width      = exclusive_size.width;
		int exclusive_y_offset   = (FONT_HEIGHT_NORMAL - exclusive_size.height) / 2;

		bool rtl = _current_text_dir == TD_RTL;
		uint text_left      = left  + (rtl ? 0 : icon_width + exclusive_width + 4);
		uint text_right     = right - (rtl ? icon_width + exclusive_width + 4 : 0);
		uint icon_left      = rtl ? right - icon_width : left;
		uint exclusive_left = rtl ? right - icon_width - exclusive_width - 2 : left + icon_width + 2;

		/* Draw list of companies */
		const Company *c;
		FOR_ALL_COMPANIES(c) {
			if ((HasBit(this->town->have_ratings, c->index) || this->town->exclusivity == c->index)) {
				DrawCompanyIcon(c->index, icon_left, y + icon_y_offset);

				SetDParam(0, c->index);
				SetDParam(1, c->index);

				int r = this->town->ratings[c->index];
				StringID str;
				(str = STR_CARGO_RATING_APPALLING, r <= RATING_APPALLING) || // Apalling
				(str++,                    r <= RATING_VERYPOOR)  || // Very Poor
				(str++,                    r <= RATING_POOR)      || // Poor
				(str++,                    r <= RATING_MEDIOCRE)  || // Mediocore
				(str++,                    r <= RATING_GOOD)      || // Good
				(str++,                    r <= RATING_VERYGOOD)  || // Very Good
				(str++,                    r <= RATING_EXCELLENT) || // Excellent
				(str++,                    true);                    // Outstanding

				SetDParam(2, str);
				if (this->town->exclusivity == c->index) {
					DrawSprite(SPR_EXCLUSIVE_TRANSPORT, COMPANY_SPRITE_COLOUR(c->index), exclusive_left, y + exclusive_y_offset);
				}

				DrawString(text_left, text_right, y, STR_LOCAL_AUTHORITY_COMPANY_RATING);
				y += FONT_HEIGHT_NORMAL;
			}
		}

		y = y + WD_FRAMERECT_BOTTOM - nwid->pos_y; // Compute needed size of the widget.
		if (y > nwid->current_y) {
			/* If the company list is too big to fit, mark ourself dirty and draw again. */
			ResizeWindow(this, 0, y - nwid->current_y);
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_TA_CAPTION) SetDParam(0, this->window_number);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_TA_ACTION_INFO:
				if (this->sel_index != -1) {
					SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[this->sel_index] >> 8);
					DrawStringMultiLine(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, r.bottom - WD_FRAMERECT_BOTTOM,
								STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + this->sel_index);
				}
				break;
			case WID_TA_COMMAND_LIST: {
				int numact;
				uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
				int y = r.top + WD_FRAMERECT_TOP;
				int pos = this->vscroll->GetPosition();

				if (--pos < 0) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_LOCAL_AUTHORITY_ACTIONS_TITLE);
					y += FONT_HEIGHT_NORMAL;
				}

				for (int i = 0; buttons; i++, buttons >>= 1) {
					if (pos <= -5) break; ///< Draw only the 5 fitting lines

					if ((buttons & 1) && --pos < 0) {
						DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y,
								STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i, this->sel_index == i ? TC_WHITE : TC_ORANGE);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TA_ACTION_INFO: {
				assert(size->width > padding.width && size->height > padding.height);
				size->width -= WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height -= WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				Dimension d = {0, 0};
				for (int i = 0; i < TACT_COUNT; i++) {
					SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[i] >> 8);
					d = maxdim(d, GetStringMultiLineBoundingBox(STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + i, *size));
				}
				*size = maxdim(*size, d);
				size->width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
			}

			case WID_TA_COMMAND_LIST:
				size->height = WD_FRAMERECT_TOP + 5 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				size->width = GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTIONS_TITLE).width;
				for (uint i = 0; i < TACT_COUNT; i++ ) {
					size->width = max(size->width, GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i).width);
				}
				size->width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				break;

			case WID_TA_RATING_INFO:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = WD_FRAMERECT_TOP + 9 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TA_COMMAND_LIST: {
				int y = this->GetRowFromWidget(pt.y, WID_TA_COMMAND_LIST, 1, FONT_HEIGHT_NORMAL);
				if (!IsInsideMM(y, 0, 5)) return;

				y = GetNthSetBit(GetMaskOfTownActions(NULL, _local_company, this->town), y + this->vscroll->GetPosition() - 1);
				if (y >= 0) {
					this->sel_index = y;
					this->SetDirty();
				}
				/* FALL THROUGH, when double-clicking. */
				if (click_count == 1 || y < 0) break;
			}

			case WID_TA_EXECUTE:
				DoCommandP(this->town->xy, this->window_number, this->sel_index, CMD_DO_TOWN_ACTION | CMD_MSG(STR_ERROR_CAN_T_DO_THIS));
				break;
		}
	}

	virtual void OnHundredthTick()
	{
		this->SetDirty();
	}
};

static WindowDesc _town_authority_desc(
	WDP_AUTO, "view_town_authority", 317, 222,
	WC_TOWN_AUTHORITY, WC_NONE,
	0,
	_nested_town_authority_widgets, lengthof(_nested_town_authority_widgets)
);

static void ShowTownAuthorityWindow(uint town)
{
	AllocateWindowDescFront<TownAuthorityWindow>(&_town_authority_desc, town);
}


/* Town view window. */
struct TownViewWindow : Window {
private:
	Town *town; ///< Town displayed by the window.

public:
	static const int WID_TV_HEIGHT_NORMAL = 150;

	TownViewWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();

		this->town = Town::Get(window_number);
		if (this->town->larger_town) this->GetWidget<NWidgetCore>(WID_TV_CAPTION)->widget_data = STR_TOWN_VIEW_CITY_CAPTION;

		this->FinishInitNested(window_number);

		this->flags |= WF_DISABLE_VP_SCROLL;
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_TV_VIEWPORT);
		nvp->InitializeViewport(this, this->town->xy, ZOOM_LVL_NEWS);

		/* disable renaming town in network games if you are not the server */
		this->SetWidgetDisabledState(WID_TV_CHANGE_NAME, _networking && !_network_server);
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_TV_CAPTION) SetDParam(0, this->town->index);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_TV_INFO) return;

		uint y = r.top + WD_FRAMERECT_TOP;

		SetDParam(0, this->town->cache.population);
		SetDParam(1, this->town->cache.num_houses);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y, STR_TOWN_VIEW_POPULATION_HOUSES);

		SetDParam(0, this->town->supplied[CT_PASSENGERS].old_act);
		SetDParam(1, this->town->supplied[CT_PASSENGERS].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_PASSENGERS_LAST_MONTH_MAX);

		SetDParam(0, this->town->supplied[CT_MAIL].old_act);
		SetDParam(1, this->town->supplied[CT_MAIL].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_MAIL_LAST_MONTH_MAX);

		bool first = true;
		for (int i = TE_BEGIN; i < TE_END; i++) {
			if (this->town->goal[i] == 0) continue;
			if (this->town->goal[i] == TOWN_GROWTH_WINTER && (TileHeight(this->town->xy) < LowestSnowLine() || this->town->cache.population <= 90)) continue;
			if (this->town->goal[i] == TOWN_GROWTH_DESERT && (GetTropicZone(this->town->xy) != TROPICZONE_DESERT || this->town->cache.population <= 60)) continue;

			if (first) {
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH);
				first = false;
			}

			bool rtl = _current_text_dir == TD_RTL;
			uint cargo_text_left = r.left + WD_FRAMERECT_LEFT + (rtl ? 0 : 20);
			uint cargo_text_right = r.right - WD_FRAMERECT_RIGHT - (rtl ? 20 : 0);

			const CargoSpec *cargo = FindFirstCargoWithTownEffect((TownEffect)i);
			assert(cargo != NULL);

			StringID string;

			if (this->town->goal[i] == TOWN_GROWTH_DESERT || this->town->goal[i] == TOWN_GROWTH_WINTER) {
				/* For 'original' gameplay, don't show the amount required (you need 1 or more ..) */
				string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_DELIVERED_GENERAL;
				if (this->town->received[i].old_act == 0) {
					string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED_GENERAL;

					if (this->town->goal[i] == TOWN_GROWTH_WINTER && TileHeight(this->town->xy) < GetSnowLine()) {
						string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED_WINTER;
					}
				}

				SetDParam(0, cargo->name);
			} else {
				string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_DELIVERED;
				if (this->town->received[i].old_act < this->town->goal[i]) {
					string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED;
				}

				SetDParam(0, cargo->Index());
				SetDParam(1, this->town->received[i].old_act);
				SetDParam(2, cargo->Index());
				SetDParam(3, this->town->goal[i]);
			}
			DrawString(cargo_text_left, cargo_text_right, y += FONT_HEIGHT_NORMAL, string);
		}

		if (HasBit(this->town->flags, TOWN_IS_GROWING)) {
			SetDParam(0, ((this->town->growth_rate & (~TOWN_GROW_RATE_CUSTOM)) * TOWN_GROWTH_TICKS + DAY_TICKS) / DAY_TICKS);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, this->town->fund_buildings_months == 0 ? STR_TOWN_VIEW_TOWN_GROWS_EVERY : STR_TOWN_VIEW_TOWN_GROWS_EVERY_FUNDED);
		} else {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_TOWN_GROW_STOPPED);
		}

		/* only show the town noise, if the noise option is activated. */
		if (_settings_game.economy.station_noise_level) {
			SetDParam(0, this->town->noise_reached);
			SetDParam(1, this->town->MaxTownNoise());
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_NOISE_IN_TOWN);
		}

		if (this->town->text != NULL) {
			SetDParamStr(0, this->town->text);
			DrawStringMultiLine(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, UINT16_MAX, STR_JUST_RAW_STRING, TC_BLACK);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TV_CENTER_VIEW: // scroll to location
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(this->town->xy);
				} else {
					ScrollMainWindowToTile(this->town->xy);
				}
				break;

			case WID_TV_SHOW_AUTHORITY: // town authority
				ShowTownAuthorityWindow(this->window_number);
				break;

			case WID_TV_CHANGE_NAME: // rename
				SetDParam(0, this->window_number);
				ShowQueryString(STR_TOWN_NAME, STR_TOWN_VIEW_RENAME_TOWN_BUTTON, MAX_LENGTH_TOWN_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_TV_EXPAND: { // expand town - only available on Scenario editor
				/* Warn the user if towns are not allowed to build roads, but do this only once per OpenTTD run. */
				static bool _warn_town_no_roads = false;

				if (!_settings_game.economy.allow_town_roads && !_warn_town_no_roads) {
					ShowErrorMessage(STR_ERROR_TOWN_EXPAND_WARN_NO_ROADS, INVALID_STRING_ID, WL_WARNING);
					_warn_town_no_roads = true;
				}

				DoCommandP(0, this->window_number, 0, CMD_EXPAND_TOWN | CMD_MSG(STR_ERROR_CAN_T_EXPAND_TOWN));
				break;
			}

			case WID_TV_DELETE: // delete town - only available on Scenario editor
				DoCommandP(0, this->window_number, 0, CMD_DELETE_TOWN | CMD_MSG(STR_ERROR_TOWN_CAN_T_DELETE));
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TV_INFO:
				size->height = GetDesiredInfoHeight(size->width);
				break;
		}
	}

	/**
	 * Gets the desired height for the information panel.
	 * @return the desired height in pixels.
	 */
	uint GetDesiredInfoHeight(int width) const
	{
		uint aimed_height = 3 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		bool first = true;
		for (int i = TE_BEGIN; i < TE_END; i++) {
			if (this->town->goal[i] == 0) continue;
			if (this->town->goal[i] == TOWN_GROWTH_WINTER && (TileHeight(this->town->xy) < LowestSnowLine() || this->town->cache.population <= 90)) continue;
			if (this->town->goal[i] == TOWN_GROWTH_DESERT && (GetTropicZone(this->town->xy) != TROPICZONE_DESERT || this->town->cache.population <= 60)) continue;

			if (first) {
				aimed_height += FONT_HEIGHT_NORMAL;
				first = false;
			}
			aimed_height += FONT_HEIGHT_NORMAL;
		}
		aimed_height += FONT_HEIGHT_NORMAL;

		if (_settings_game.economy.station_noise_level) aimed_height += FONT_HEIGHT_NORMAL;

		if (this->town->text != NULL) {
			SetDParamStr(0, this->town->text);
			aimed_height += GetStringHeight(STR_JUST_RAW_STRING, width - WD_FRAMERECT_LEFT - WD_FRAMERECT_RIGHT);
		}

		return aimed_height;
	}

	void ResizeWindowAsNeeded()
	{
		const NWidgetBase *nwid_info = this->GetWidget<NWidgetBase>(WID_TV_INFO);
		uint aimed_height = GetDesiredInfoHeight(nwid_info->current_x);
		if (aimed_height > nwid_info->current_y || (aimed_height < nwid_info->current_y && nwid_info->current_y > nwid_info->smallest_y)) {
			this->ReInit();
		}
	}

	virtual void OnResize()
	{
		if (this->viewport != NULL) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_TV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);

			ScrollWindowToTile(this->town->xy, this, true); // Re-center viewport.
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		/* Called when setting station noise or required cargoes have changed, in order to resize the window */
		this->SetDirty(); // refresh display for current size. This will allow to avoid glitches when downgrading
		this->ResizeWindowAsNeeded();
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_TOWN | CMD_MSG(STR_ERROR_CAN_T_RENAME_TOWN), NULL, str);
	}
};

static const NWidgetPart _nested_town_game_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0), SetResize(1, 1), SetPadding(1, 1, 1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_SHOW_AUTHORITY), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_LOCAL_AUTHORITY_BUTTON, STR_TOWN_VIEW_LOCAL_AUTHORITY_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer(),
};

static WindowDesc _town_game_view_desc(
	WDP_AUTO, "view_town", 260, TownViewWindow::WID_TV_HEIGHT_NORMAL,
	WC_TOWN_VIEW, WC_NONE,
	0,
	_nested_town_game_view_widgets, lengthof(_nested_town_game_view_widgets)
);

static const NWidgetPart _nested_town_editor_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(76, 14), SetDataTip(STR_BUTTON_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 1), SetResize(1, 1), SetPadding(1, 1, 1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_EXPAND), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_EXPAND_BUTTON, STR_TOWN_VIEW_EXPAND_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_DELETE), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_DELETE_BUTTON, STR_TOWN_VIEW_DELETE_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer(),
};

static WindowDesc _town_editor_view_desc(
	WDP_AUTO, "view_town_scen", 260, TownViewWindow::WID_TV_HEIGHT_NORMAL,
	WC_TOWN_VIEW, WC_NONE,
	0,
	_nested_town_editor_view_widgets, lengthof(_nested_town_editor_view_widgets)
);

void ShowTownViewWindow(TownID town)
{
	if (_game_mode == GM_EDITOR) {
		AllocateWindowDescFront<TownViewWindow>(&_town_editor_view_desc, town);
	} else {
		AllocateWindowDescFront<TownViewWindow>(&_town_game_view_desc, town);
	}
}

static const NWidgetPart _nested_town_directory_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_TOWN_DIRECTORY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TD_SORT_ORDER), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_TD_SORT_CRITERIA), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(WWT_PANEL, COLOUR_BROWN), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_TD_LIST), SetMinimalSize(196, 0), SetDataTip(0x0, STR_TOWN_DIRECTORY_LIST_TOOLTIP),
							SetFill(1, 0), SetResize(0, 10), SetScrollbar(WID_TD_SCROLLBAR), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_TD_WORLD_POPULATION), SetPadding(2, 0, 0, 2), SetMinimalSize(196, 12), SetFill(1, 0), SetDataTip(STR_TOWN_POPULATION, STR_NULL),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_TD_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

/** Town directory window class. */
struct TownDirectoryWindow : public Window {
private:
	/* Runtime saved values */
	static Listing last_sorting;
	static const Town *last_town;

	/* Constants for sorting towns */
	static const StringID sorter_names[];
	static GUITownList::SortFunction * const sorter_funcs[];

	GUITownList towns;

	Scrollbar *vscroll;

	void BuildSortTownList()
	{
		if (this->towns.NeedRebuild()) {
			this->towns.Clear();

			const Town *t;
			FOR_ALL_TOWNS(t) {
				*this->towns.Append() = t;
			}

			this->towns.Compact();
			this->towns.RebuildDone();
			this->vscroll->SetCount(this->towns.Length()); // Update scrollbar as well.
		}
		/* Always sort the towns. */
		this->last_town = NULL;
		this->towns.Sort();
		this->SetWidgetDirty(WID_TD_LIST); // Force repaint of the displayed towns.
	}

	/** Sort by town name */
	static int CDECL TownNameSorter(const Town * const *a, const Town * const *b)
	{
		static char buf_cache[64];
		const Town *ta = *a;
		const Town *tb = *b;
		char buf[64];

		SetDParam(0, ta->index);
		GetString(buf, STR_TOWN_NAME, lastof(buf));

		/* If 'b' is the same town as in the last round, use the cached value
		 * We do this to speed stuff up ('b' is called with the same value a lot of
		 * times after each other) */
		if (tb != last_town) {
			last_town = tb;
			SetDParam(0, tb->index);
			GetString(buf_cache, STR_TOWN_NAME, lastof(buf_cache));
		}

		return strnatcmp(buf, buf_cache); // Sort by name (natural sorting).
	}

	/** Sort by population (default descending, as big towns are of the most interest). */
	static int CDECL TownPopulationSorter(const Town * const *a, const Town * const *b)
	{
		uint32 a_population = (*a)->cache.population;
		uint32 b_population = (*b)->cache.population;
		if (a_population == b_population) return TownDirectoryWindow::TownNameSorter(a, b);
		return (a_population < b_population) ? -1 : 1;
	}

	/** Sort by town rating */
	static int CDECL TownRatingSorter(const Town * const *a, const Town * const *b)
	{
		int before = TownDirectoryWindow::last_sorting.order ? 1 : -1; // Value to get 'a' before 'b'.

		/* Towns without rating are always after towns with rating. */
		if (HasBit((*a)->have_ratings, _local_company)) {
			if (HasBit((*b)->have_ratings, _local_company)) {
				int16 a_rating = (*a)->ratings[_local_company];
				int16 b_rating = (*b)->ratings[_local_company];
				if (a_rating == b_rating) return TownDirectoryWindow::TownNameSorter(a, b);
				return (a_rating < b_rating) ? -1 : 1;
			}
			return before;
		}
		if (HasBit((*b)->have_ratings, _local_company)) return -before;
		return -before * TownDirectoryWindow::TownNameSorter(a, b); // Sort unrated towns always on ascending town name.
	}

public:
	TownDirectoryWindow(WindowDesc *desc) : Window(desc)
	{
		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_TD_SCROLLBAR);

		this->towns.SetListing(this->last_sorting);
		this->towns.SetSortFuncs(TownDirectoryWindow::sorter_funcs);
		this->towns.ForceRebuild();
		this->BuildSortTownList();

		this->FinishInitNested(0);
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_TD_WORLD_POPULATION:
				SetDParam(0, GetWorldPopulation());
				break;

			case WID_TD_SORT_CRITERIA:
				SetDParam(0, TownDirectoryWindow::sorter_names[this->towns.SortType()]);
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_TD_SORT_ORDER:
				this->DrawSortButtonState(widget, this->towns.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_TD_LIST: {
				int n = 0;
				int y = r.top + WD_FRAMERECT_TOP;
				if (this->towns.Length() == 0) { // No towns available.
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right, y, STR_TOWN_DIRECTORY_NONE);
					break;
				}

				/* At least one town available. */
				bool rtl = _current_text_dir == TD_RTL;
				Dimension icon_size = GetSpriteSize(SPR_TOWN_RATING_GOOD);
				int text_left  = r.left + WD_FRAMERECT_LEFT + (rtl ? 0 : icon_size.width + 2);
				int text_right = r.right - WD_FRAMERECT_RIGHT - (rtl ? icon_size.width + 2 : 0);
				int icon_x = rtl ? r.right - WD_FRAMERECT_RIGHT - icon_size.width : r.left + WD_FRAMERECT_LEFT;

				for (uint i = this->vscroll->GetPosition(); i < this->towns.Length(); i++) {
					const Town *t = this->towns[i];
					assert(t->xy != INVALID_TILE);

					/* Draw rating icon. */
					if (_game_mode == GM_EDITOR || !HasBit(t->have_ratings, _local_company)) {
						DrawSprite(SPR_TOWN_RATING_NA, PAL_NONE, icon_x, y + (this->resize.step_height - icon_size.height) / 2);
					} else {
						SpriteID icon = SPR_TOWN_RATING_APALLING;
						if (t->ratings[_local_company] > RATING_VERYPOOR) icon = SPR_TOWN_RATING_MEDIOCRE;
						if (t->ratings[_local_company] > RATING_GOOD)     icon = SPR_TOWN_RATING_GOOD;
						DrawSprite(icon, PAL_NONE, icon_x, y + (this->resize.step_height - icon_size.height) / 2);
					}

					SetDParam(0, t->index);
					SetDParam(1, t->cache.population);
					DrawString(text_left, text_right, y + (this->resize.step_height - FONT_HEIGHT_NORMAL) / 2, STR_TOWN_DIRECTORY_TOWN);

					y += this->resize.step_height;
					if (++n == this->vscroll->GetCapacity()) break; // max number of towns in 1 window
				}
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TD_SORT_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_SORT_CRITERIA: {
				Dimension d = {0, 0};
				for (uint i = 0; TownDirectoryWindow::sorter_names[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(TownDirectoryWindow::sorter_names[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_LIST: {
				Dimension d = GetStringBoundingBox(STR_TOWN_DIRECTORY_NONE);
				for (uint i = 0; i < this->towns.Length(); i++) {
					const Town *t = this->towns[i];

					assert(t != NULL);

					SetDParam(0, t->index);
					SetDParamMaxDigits(1, 8);
					d = maxdim(d, GetStringBoundingBox(STR_TOWN_DIRECTORY_TOWN));
				}
				Dimension icon_size = GetSpriteSize(SPR_TOWN_RATING_GOOD);
				d.width += icon_size.width + 2;
				d.height = max(d.height, icon_size.height);
				resize->height = d.height;
				d.height *= 5;
				d.width += padding.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				d.height += padding.height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_WORLD_POPULATION: {
				SetDParamMaxDigits(0, 10);
				Dimension d = GetStringBoundingBox(STR_TOWN_POPULATION);
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TD_SORT_ORDER: // Click on sort order button
				if (this->towns.SortType() != 2) { // A different sort than by rating.
					this->towns.ToggleSortOrder();
					this->last_sorting = this->towns.GetListing(); // Store new sorting order.
				} else {
					/* Some parts are always sorted ascending on name. */
					this->last_sorting.order = !this->last_sorting.order;
					this->towns.SetListing(this->last_sorting);
					this->towns.ForceResort();
					this->towns.Sort();
				}
				this->SetDirty();
				break;

			case WID_TD_SORT_CRITERIA: // Click on sort criteria dropdown
				ShowDropDownMenu(this, TownDirectoryWindow::sorter_names, this->towns.SortType(), WID_TD_SORT_CRITERIA, 0, 0);
				break;

			case WID_TD_LIST: { // Click on Town Matrix
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TD_LIST, WD_FRAMERECT_TOP);
				if (id_v >= this->towns.Length()) return; // click out of town bounds

				const Town *t = this->towns[id_v];
				assert(t != NULL);
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(t->xy);
				} else {
					ScrollMainWindowToTile(t->xy);
				}
				break;
			}
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (widget != WID_TD_SORT_CRITERIA) return;

		if (this->towns.SortType() != index) {
			this->towns.SetSortType(index);
			this->last_sorting = this->towns.GetListing(); // Store new sorting order.
			this->BuildSortTownList();
		}
	}

	virtual void OnPaint()
	{
		if (this->towns.NeedRebuild()) this->BuildSortTownList();
		this->DrawWidgets();
	}

	virtual void OnHundredthTick()
	{
		this->BuildSortTownList();
		this->SetDirty();
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_TD_LIST);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->towns.ForceRebuild();
		} else {
			this->towns.ForceResort();
		}
	}
};

Listing TownDirectoryWindow::last_sorting = {false, 0};
const Town *TownDirectoryWindow::last_town = NULL;

/** Names of the sorting functions. */
const StringID TownDirectoryWindow::sorter_names[] = {
	STR_SORT_BY_NAME,
	STR_SORT_BY_POPULATION,
	STR_SORT_BY_RATING,
	INVALID_STRING_ID
};

/** Available town directory sorting functions. */
GUITownList::SortFunction * const TownDirectoryWindow::sorter_funcs[] = {
	&TownNameSorter,
	&TownPopulationSorter,
	&TownRatingSorter,
};

static WindowDesc _town_directory_desc(
	WDP_AUTO, "list_towns", 208, 202,
	WC_TOWN_DIRECTORY, WC_NONE,
	0,
	_nested_town_directory_widgets, lengthof(_nested_town_directory_widgets)
);

void ShowTownDirectory()
{
	if (BringWindowToFrontById(WC_TOWN_DIRECTORY, 0)) return;
	new TownDirectoryWindow(&_town_directory_desc);
}

void CcFoundTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_SPLAT_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

void CcFoundRandomTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) ScrollMainWindowToTile(Town::Get(_new_town_id)->xy);
}

static const NWidgetPart _nested_found_town_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_FOUND_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	/* Construct new town(s) buttons. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_NEW_TOWN), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_NEW_TOWN_BUTTON, STR_FOUND_TOWN_NEW_TOWN_TOOLTIP), SetPadding(0, 2, 1, 2),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TF_RANDOM_TOWN), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_RANDOM_TOWN_BUTTON, STR_FOUND_TOWN_RANDOM_TOWN_TOOLTIP), SetPadding(0, 2, 1, 2),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TF_MANY_RANDOM_TOWNS), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_MANY_RANDOM_TOWNS, STR_FOUND_TOWN_RANDOM_TOWNS_TOOLTIP), SetPadding(0, 2, 0, 2),
		/* Town name selection. */
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(156, 14), SetPadding(0, 2, 0, 2), SetDataTip(STR_FOUND_TOWN_NAME_TITLE, STR_NULL),
		NWidget(WWT_EDITBOX, COLOUR_GREY, WID_TF_TOWN_NAME_EDITBOX), SetMinimalSize(156, 12), SetPadding(0, 2, 3, 2),
										SetDataTip(STR_FOUND_TOWN_NAME_EDITOR_TITLE, STR_FOUND_TOWN_NAME_EDITOR_HELP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TF_TOWN_NAME_RANDOM), SetMinimalSize(78, 12), SetPadding(0, 2, 0, 2), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_NAME_RANDOM_BUTTON, STR_FOUND_TOWN_NAME_RANDOM_TOOLTIP),
		/* Town size selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_TITLE, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_SMALL), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_SMALL_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_MEDIUM), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_MEDIUM_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_LARGE), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_LARGE_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_RANDOM), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_SIZE_RANDOM, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_CITY), SetPadding(0, 2, 0, 2), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_CITY, STR_FOUND_TOWN_CITY_TOOLTIP), SetFill(1, 0),
		/* Town roads selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetDataTip(STR_FOUND_TOWN_ROAD_LAYOUT, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_ORIGINAL), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_ORIGINAL, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_BETTER), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_BETTER_ROADS, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_GRID2), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_2X2_GRID, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_GRID3), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_3X3_GRID, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_RANDOM), SetPadding(0, 2, 0, 2), SetMinimalSize(0, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_RANDOM, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT), SetFill(1, 0),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
	EndContainer(),
};

/** Found a town window class. */
struct FoundTownWindow : Window {
private:
	TownSize town_size;     ///< Selected town size
	TownLayout town_layout; ///< Selected town layout
	bool city;              ///< Are we building a city?
	QueryString townname_editbox; ///< Townname editbox
	bool townnamevalid;     ///< Is generated town name valid?
	uint32 townnameparts;   ///< Generated town name
	TownNameParams params;  ///< Town name parameters

public:
	FoundTownWindow(WindowDesc *desc, WindowNumber window_number) :
			Window(desc),
			town_size(TSZ_MEDIUM),
			town_layout(_settings_game.economy.town_layout),
			townname_editbox(MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH, MAX_LENGTH_TOWN_NAME_CHARS),
			params(_settings_game.game_creation.town_name)
	{
		this->InitNested(window_number);
		this->querystrings[WID_TF_TOWN_NAME_EDITBOX] = &this->townname_editbox;
		this->RandomTownName();
		this->UpdateButtons(true);
	}

	void RandomTownName()
	{
		this->townnamevalid = GenerateTownName(&this->townnameparts);

		if (!this->townnamevalid) {
			this->townname_editbox.text.DeleteAll();
		} else {
			GetTownName(this->townname_editbox.text.buf, &this->params, this->townnameparts, &this->townname_editbox.text.buf[this->townname_editbox.text.max_bytes - 1]);
			this->townname_editbox.text.UpdateSize();
		}
		UpdateOSKOriginalText(this, WID_TF_TOWN_NAME_EDITBOX);

		this->SetWidgetDirty(WID_TF_TOWN_NAME_EDITBOX);
	}

	void UpdateButtons(bool check_availability)
	{
		if (check_availability && _game_mode != GM_EDITOR) {
			this->SetWidgetsDisabledState(true, WID_TF_RANDOM_TOWN, WID_TF_MANY_RANDOM_TOWNS, WID_TF_SIZE_LARGE, WIDGET_LIST_END);
			this->SetWidgetsDisabledState(_settings_game.economy.found_town != TF_CUSTOM_LAYOUT,
					WID_TF_LAYOUT_ORIGINAL, WID_TF_LAYOUT_BETTER, WID_TF_LAYOUT_GRID2, WID_TF_LAYOUT_GRID3, WID_TF_LAYOUT_RANDOM, WIDGET_LIST_END);
			if (_settings_game.economy.found_town != TF_CUSTOM_LAYOUT) town_layout = _settings_game.economy.town_layout;
		}

		for (int i = WID_TF_SIZE_SMALL; i <= WID_TF_SIZE_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == WID_TF_SIZE_SMALL + this->town_size);
		}

		this->SetWidgetLoweredState(WID_TF_CITY, this->city);

		for (int i = WID_TF_LAYOUT_ORIGINAL; i <= WID_TF_LAYOUT_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == WID_TF_LAYOUT_ORIGINAL + this->town_layout);
		}

		this->SetDirty();
	}

	void ExecuteFoundTownCommand(TileIndex tile, bool random, StringID errstr, CommandCallback cc)
	{
		const char *name = NULL;

		if (!this->townnamevalid) {
			name = this->townname_editbox.text.buf;
		} else {
			/* If user changed the name, send it */
			char buf[MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH];
			GetTownName(buf, &this->params, this->townnameparts, lastof(buf));
			if (strcmp(buf, this->townname_editbox.text.buf) != 0) name = this->townname_editbox.text.buf;
		}

		bool success = DoCommandP(tile, this->town_size | this->city << 2 | this->town_layout << 3 | random << 6,
				townnameparts, CMD_FOUND_TOWN | CMD_MSG(errstr), cc, name);

		/* Rerandomise name, if success and no cost-estimation. */
		if (success && !_shift_pressed) this->RandomTownName();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TF_NEW_TOWN:
				HandlePlacePushButton(this, WID_TF_NEW_TOWN, SPR_CURSOR_TOWN, HT_RECT);
				break;

			case WID_TF_RANDOM_TOWN:
				this->ExecuteFoundTownCommand(0, true, STR_ERROR_CAN_T_GENERATE_TOWN, CcFoundRandomTown);
				break;

			case WID_TF_TOWN_NAME_RANDOM:
				this->RandomTownName();
				this->SetFocusedWidget(WID_TF_TOWN_NAME_EDITBOX);
				break;

			case WID_TF_MANY_RANDOM_TOWNS:
				_generating_world = true;
				UpdateNearestTownForRoadTiles(true);
				if (!GenerateTowns(this->town_layout)) {
					ShowErrorMessage(STR_ERROR_CAN_T_GENERATE_TOWN, STR_ERROR_NO_SPACE_FOR_TOWN, WL_INFO);
				}
				UpdateNearestTownForRoadTiles(false);
				_generating_world = false;
				break;

			case WID_TF_SIZE_SMALL: case WID_TF_SIZE_MEDIUM: case WID_TF_SIZE_LARGE: case WID_TF_SIZE_RANDOM:
				this->town_size = (TownSize)(widget - WID_TF_SIZE_SMALL);
				this->UpdateButtons(false);
				break;

			case WID_TF_CITY:
				this->city ^= true;
				this->SetWidgetLoweredState(WID_TF_CITY, this->city);
				this->SetDirty();
				break;

			case WID_TF_LAYOUT_ORIGINAL: case WID_TF_LAYOUT_BETTER: case WID_TF_LAYOUT_GRID2:
			case WID_TF_LAYOUT_GRID3: case WID_TF_LAYOUT_RANDOM:
				this->town_layout = (TownLayout)(widget - WID_TF_LAYOUT_ORIGINAL);
				this->UpdateButtons(false);
				break;
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		this->ExecuteFoundTownCommand(tile, false, STR_ERROR_CAN_T_FOUND_TOWN_HERE, CcFoundTown);
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->UpdateButtons(false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		this->UpdateButtons(true);
	}
};

static WindowDesc _found_town_desc(
	WDP_AUTO, "build_town", 160, 162,
	WC_FOUND_TOWN, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_found_town_widgets, lengthof(_nested_found_town_widgets)
);

void ShowFoundTownWindow()
{
	if (_game_mode != GM_EDITOR) {
		if (_settings_game.economy.found_town == TF_FORBIDDEN) return;
		if (!Company::IsValidID(_local_company)) return;
	}
	AllocateWindowDescFront<FoundTownWindow>(&_found_town_desc, 0);
}

/** List of buildable houses and house sets. */
class GUIHouseList : protected SmallVector<HouseID, 32> {
protected:
	SmallVector<uint16, 4> house_sets; ///< list of house sets, each item points the first house of the set in the houses array

	static int CDECL HouseSorter(const HouseID *a, const HouseID *b)
	{
		const HouseSpec *a_hs = HouseSpec::Get(*a);
		const GRFFile *a_set = a_hs->grf_prop.grffile;
		const HouseSpec *b_hs = HouseSpec::Get(*b);
		const GRFFile *b_set = b_hs->grf_prop.grffile;

		int ret = (a_set != NULL) - (b_set != NULL);
		if (ret == 0) {
			if (a_set != NULL) {
				assert_compile(sizeof(a_set->grfid) <= sizeof(int));
				ret = a_set->grfid - b_set->grfid;
				if (ret == 0) ret = a_hs->grf_prop.local_id - b_hs->grf_prop.local_id;
			} else {
				ret = *a - *b;
			}
		}
		return ret;
	}

public:
	GUIHouseList()
	{
		*this->house_sets.Append() = 0; // terminator
	}

	/**
	 * Get house at given offset.
	 * @param house_set House set (or a negative number).
	 * @param house_offset Offset of the house within the set (or a negative number).
	 * @return House at given offset or #INVALID_HOUSE_ID if \c house_set or \c house_offset is negative.
	 */
	inline HouseID GetHouseAtOffset(int house_set, int house_offset) const
	{
		if (house_set < 0 || house_offset < 0) return INVALID_HOUSE_ID;
		assert(house_set < (int)this->NumHouseSets());
		assert(this->house_sets[house_set] + house_offset < (int)this->Length());
		return *this->Get(this->house_sets[house_set] + house_offset);
	}

	/**
	 * Get the number of house sets.
	 * @return Number of house sets.
	 */
	uint NumHouseSets() const
	{
		return this->house_sets.Length() - 1; // last item is a terminator
	}

	/**
	 * Get number of houses in a given set.
	 * @param house_set House set (or a negative number).
	 * @return Number of houses in the given set or 0 if \c house_set is negative.
	 */
	uint NumHousesInHouseSet(int house_set) const
	{
		assert(house_set < (int)this->NumHouseSets());
		/* There is a terminator on the list of house sets. It's equal to the number
		 * of all houses. We can safely use "house_set + 1" even for the last
		 * house set. */
		return house_set < 0 ? 0 : this->house_sets[house_set + 1] - this->house_sets[house_set];
	}

	/**
	 * Find the house set of a given house.
	 *
	 * This operation is O(number of house sets).
	 *
	 * @param house The house (or #INVALID_HOUSE_ID).
	 * @return House set of the house or -1 if not found.
	 */
	int FindHouseSet(HouseID house) const
	{
		if (house != INVALID_HOUSE_ID) {
			const GRFFile *house_set = HouseSpec::Get(house)->grf_prop.grffile;
			for (uint i = 0; i < this->NumHouseSets(); i++) {
				if (HouseSpec::Get(this->GetHouseAtOffset(i, 0))->grf_prop.grffile == house_set) return i;
			}
		}
		return -1;
	}

	/**
	 * Find offset of a given house within a given house set.
	 *
	 * This operation is O(number of houses in the set).
	 *
	 * @param house_set House set to search in (or a negative number).
	 * @param house The house (or #INVALID_HOUSE_ID).
	 * @return Offset of the house within the set or -1 if not found or \c house_set is negative.
	 */
	int FindHouseOffset(int house_set, HouseID house) const
	{
		assert(house_set < (int)this->NumHouseSets());
		if (house_set >= 0 && house != INVALID_HOUSE_ID) {
			const HouseID *begin = this->Begin() + this->house_sets[house_set];
			const HouseID *end = this->Begin() + this->house_sets[house_set + 1];
			const HouseID *pos = std::lower_bound(begin, end, house);
			if (pos != end && *pos == house) return pos - begin;
		}
		return -1;
	}

	/**
	 * Get the name of a given house set.
	 *
	 * @param house_set The set.
	 * @return The name (appropriate string parameters will be set).
	 */
	StringID GetNameOfHouseSet(uint house_set) const
	{
		assert(house_set < this->NumHouseSets());
		const GRFFile *grf = HouseSpec::Get(this->GetHouseAtOffset(house_set, 0))->grf_prop.grffile;
		if (grf != NULL) {
			SetDParamStr(0, GetGRFConfig(grf->grfid)->GetName());
			return STR_JUST_RAW_STRING;
		} else {
			SetDParam(0, STR_BASIC_HOUSE_SET_NAME);
			return STR_JUST_STRING;
		}
	}

	/** (Re)build the list. */
	void Build()
	{
		/* collect items */
		this->Clear();
		HouseZones zones = CurrentClimateHouseZones();
		for (HouseID house = 0; house < NUM_HOUSES; house++) {
			if (IsHouseTypeAllowed(house, zones, _game_mode == GM_EDITOR).Succeeded()) *this->Append() = house;
		}
		this->Compact();

		/* arrange items */
		QSortT(this->Begin(), this->Length(), HouseSorter);

		/* list house sets */
		this->house_sets.Clear();
		const GRFFile *last_set = NULL;
		for (uint i = 0; i < this->Length(); i++) {
			const HouseSpec *hs = HouseSpec::Get((*this)[i]);
			/* add house set */
			if (this->house_sets.Length() == 0 || last_set != hs->grf_prop.grffile) {
				last_set = hs->grf_prop.grffile;
				*this->house_sets.Append() = i;
			}
		}
		/* put a terminator on the list to make counting easier */
		*this->house_sets.Append() = this->Length();
	}
};

static struct {
	HouseID id;
	HouseVariant variant;
}
_cur_house = { INVALID_HOUSE_ID, HOUSE_NO_VARIANT }; ///< house selected in the house picker window

/** The window used for building houses. */
class HousePickerWindow : public Window {
	friend void ShowBuildHousePicker();

protected:
	GUIHouseList house_list;        ///< list of houses and house sets
	HouseZonesBits tileselect_zone; ///< house zone (closest town) of currently highlighted tile
	bool tileselect_bad_land;       ///< whether currently highlighted tile has wrong landscape for the house (e.g. wrong side of the snowline)

	/**
	 * Get the height of a single line in the list of house sets.
	 * @return the height
	 */
	inline uint GetLineHeight() const
	{
		return FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
	}

	/**
	 * Test whether this window is currently being a callback window for ongoing tile selection.
	 * @return result of the test
	 * @see SetObjectToPlace
	 */
	inline bool IsObjectToPlaceSet() const
	{
		return _thd.window_class == WC_BUILD_HOUSE;
	}

	/**
	 * Test whether a given house is currently disabled (greyed out houses).
	 * @return result of the test
	 */
	inline bool IsHouseDisabled(HouseID house) const
	{
		if (_game_mode == GM_EDITOR) return false;
		const HouseSpec *hs = HouseSpec::Get(house);
		return _cur_year < hs->min_year || _cur_year > hs->max_year;
	}

	/**
	 * Get currently selected house set.
	 * @return index of the house set
	 */
	int GetCurrentHouseSet() const
	{
		return this->house_list.FindHouseSet(_cur_house.id);
	}

	/**
	 * Select another house.
	 * @param new_house_set index of the house set, -1 to auto-select
	 * @param new_house_offset offset of the house, -1 to auto-select
	 * @param new_house_variant variant of the house, -1 to auto-select
	 * @param clicked whether to make the house button "clicked" and activate house placement (>0 yes, 0 no, -1 auto)
	 */
	void SelectOtherHouse(int new_house_set, int new_house_offset, int new_house_variant, int clicked)
	{
		if (this->house_list.NumHouseSets() == 0) { // special handling needed
			_cur_house.id = INVALID_HOUSE_ID;
			_cur_house.variant = HOUSE_NO_VARIANT;
			if (this->IsObjectToPlaceSet()) ResetObjectToPlace();
			return;
		}

		/* auto-select */
		if (new_house_set < 0) new_house_set = max(0, this->GetCurrentHouseSet());
		if (new_house_offset < 0) new_house_offset = max(0, this->house_list.FindHouseOffset(new_house_set, _cur_house.id));
		if (clicked < 0) clicked = this->IsObjectToPlaceSet() ? 1 : 0;

		HouseID new_house_id = this->house_list.GetHouseAtOffset(new_house_set, new_house_offset);

		const HouseSpec *hs =  HouseSpec::Get(new_house_id);
		if (hs->num_variants == 0) {
			new_house_variant = HOUSE_NO_VARIANT;
		} else {
			if (new_house_variant < 0 && new_house_id == _cur_house.id) new_house_variant = _cur_house.variant;
			if (!IsInsideBS(new_house_variant, HOUSE_FIRST_VARIANT, hs->num_variants)) new_house_variant = HOUSE_FIRST_VARIANT;
		}

		_cur_house.id = new_house_id;
		_cur_house.variant = new_house_variant;

		bool disabled = this->IsHouseDisabled(_cur_house.id);

		if (clicked > 0 ? disabled : this->IsObjectToPlaceSet()) {
			ResetObjectToPlace(); // warning, this may cause recursion through OnPlaceObjectAbort
		}

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
		matrix->SetCount(this->house_list.NumHousesInHouseSet(new_house_set));
		matrix->SetClicked(clicked > 0 ? new_house_offset : -1);

		this->GetWidget<NWidgetStacked>(WID_HP_PREV_VARIANT_SEL)->SetDisplayedPlane(hs->num_variants > 1 ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_HP_NEXT_VARIANT_SEL)->SetDisplayedPlane(hs->num_variants > 1 ? 0 : SZSP_NONE);
		this->SetWidgetDisabledState(WID_HP_PREV_VARIANT, _cur_house.variant <= HOUSE_FIRST_VARIANT);
		this->SetWidgetDisabledState(WID_HP_NEXT_VARIANT, _cur_house.variant >= HOUSE_FIRST_VARIANT + hs->num_variants - 1);

		this->SetDirty();

		if (clicked > 0 && !disabled) {
			if (!this->IsObjectToPlaceSet()) SetObjectToPlaceWnd(SPR_CURSOR_TOWN, PAL_NONE, HT_RECT, this);
			SetTileSelectSize(
					hs->building_flags & BUILDING_2_TILES_X ? 2 : 1,
					hs->building_flags & BUILDING_2_TILES_Y ? 2 : 1);
		}
	}

public:
	HousePickerWindow(WindowDesc *desc, WindowNumber number) : Window(desc), tileselect_zone(HZB_END), tileselect_bad_land(false)
	{
		this->CreateNestedTree();
		/* there is no shade box but we will shade the window if there is no house to show */
		this->shade_select = this->GetWidget<NWidgetStacked>(WID_HP_MAIN_PANEL_SEL);
		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
		matrix->SetScrollbar(this->GetScrollbar(WID_HP_HOUSE_SELECT_SCROLL));
		this->FinishInitNested(number);
	}

	~HousePickerWindow()
	{
		DeleteWindowById(WC_SELECT_TOWN, 0);
	}

	virtual void OnInit()
	{
		this->house_list.Build();

		/* restore last house */
		this->SelectOtherHouse(-1, -1, -1, -1);

		/* hide widgets if we have no houses to show */
		this->SetShaded(this->house_list.NumHouseSets() == 0);

		if (this->house_list.NumHouseSets() != 0) {
			/* show the list of house sets if we have at least 2 items to show */
			this->GetWidget<NWidgetStacked>(WID_HP_HOUSE_SETS_SEL)->SetDisplayedPlane(this->house_list.NumHouseSets() > 1 ? 0 : SZSP_NONE);
			/* set number of items in the list of house sets */
			this->GetWidget<NWidgetCore>(WID_HP_HOUSE_SETS)->widget_data = (this->house_list.NumHouseSets() << MAT_ROW_START) | (1 << MAT_COL_START);
			/* show the landscape info only in arctic climate (above/below snowline) */
			this->GetWidget<NWidgetStacked>(WID_HP_HOUSE_LANDSCAPE_SEL)->SetDisplayedPlane(_settings_game.game_creation.landscape == LT_ARCTIC ? 0 : SZSP_NONE);
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_HP_CAPTION:
				if (this->house_list.NumHouseSets() == 1 && _loaded_newgrf_features.has_newhouses) {
					StringID str = this->house_list.GetNameOfHouseSet(0);
					InjectDParam(1);
					SetDParam(0, str);
				} else {
					SetDParam(0, STR_JUST_STRING);
					SetDParam(1, STR_HOUSE_BUILD_CAPTION_DEFAULT_TEXT);
				}
				break;

			case WID_HP_HOUSE_NAME:
				SetDParam(0, GetHouseName(_cur_house.id, INVALID_TILE, _cur_house.variant));
				break;

			case WID_HP_HISTORICAL_BUILDING:
				SetDParam(0, HouseSpec::Get(_cur_house.id)->extra_flags & BUILDING_IS_HISTORICAL ? STR_HOUSE_BUILD_HISTORICAL_BUILDING : STR_EMPTY);
				break;

			case WID_HP_HOUSE_POPULATION:
				SetDParam(0, HouseSpec::Get(_cur_house.id)->population);
				break;

			case WID_HP_HOUSE_ZONES: {
				HouseZones zones = (HouseZones)(HouseSpec::Get(_cur_house.id)->building_availability & HZ_ZONALL);
				for (HouseZonesBits i = HZB_BEGIN; i < HZB_END; i++) {
					StringID str = STR_HOUSE_BUILD_HOUSE_ZONE_GOOD;
					str += !HasBit(zones, i)            ? 1 : 0; // bad : good
					str += (this->tileselect_zone == i) ? 2 : 0; // highlighted : not highlighted
					SetDParam(2 * i, str);
					SetDParam(2 * i + 1, i + 1);
				}
				break;
			}

			case WID_HP_HOUSE_LANDSCAPE:
				switch (HouseSpec::Get(_cur_house.id)->building_availability & (HZ_SUBARTC_ABOVE | HZ_SUBARTC_BELOW)) {
					case HZ_SUBARTC_ABOVE: SetDParam(0, this->tileselect_bad_land ? STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE_BAD : STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE_GOOD); break;
					case HZ_SUBARTC_BELOW: SetDParam(0, this->tileselect_bad_land ? STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE_BAD : STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE_GOOD); break;
					default:               SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ABOVE_OR_BELOW_SNOWLINE); break;
				}
				break;

			case WID_HP_HOUSE_YEARS: {
				const HouseSpec *hs = HouseSpec::Get(_cur_house.id);
				SetDParam(0, hs->min_year <= _cur_year ? STR_HOUSE_BUILD_YEAR_GOOD : STR_HOUSE_BUILD_YEAR_BAD);
				SetDParam(1, hs->min_year);
				SetDParam(2, hs->max_year >= _cur_year ? STR_HOUSE_BUILD_YEAR_GOOD : STR_HOUSE_BUILD_YEAR_BAD);
				SetDParam(3, hs->max_year);
				break;
			}

			case WID_HP_HOUSE_ACCEPTANCE: {
				static char buff[DRAW_STRING_BUFFER] = "";
				char *str = buff;
				CargoArray cargo;
				uint32 dummy = 0;
				AddAcceptedHouseCargo(_cur_house.id, INVALID_TILE, cargo, &dummy, _cur_house.variant);
				for (uint i = 0; i < NUM_CARGO; i++) {
					if (cargo[i] == 0) continue;
					/* If the accepted value is less than 8, show it in 1/8:ths */
					SetDParam(0, cargo[i] < 8 ? STR_HOUSE_BUILD_CARGO_VALUE_EIGHTS : STR_HOUSE_BUILD_CARGO_VALUE_JUST_NAME);
					SetDParam(1, cargo[i]);
					SetDParam(2, CargoSpec::Get(i)->name);
					str = GetString(str, str == buff ? STR_HOUSE_BUILD_CARGO_FIRST : STR_HOUSE_BUILD_CARGO_SEPARATED, lastof(buff));
				}
				if (str == buff) GetString(buff, STR_JUST_NOTHING, lastof(buff));
				SetDParamStr(0, buff);
				break;
			}

			case WID_HP_HOUSE_SUPPLY: {
				CargoArray cargo;
				AddProducedHouseCargo(_cur_house.id, INVALID_TILE, cargo, _cur_house.variant);
				uint32 cargo_mask = 0;
				for (uint i = 0; i < NUM_CARGO; i++) if (cargo[i] != 0) SetBit(cargo_mask, i);
				SetDParam(0, cargo_mask);
				break;
			}

			default: break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_HP_HOUSE_SETS: {
				uint max_w = 0;
				for (uint i = 0; i < this->house_list.NumHouseSets(); i++) {
					max_w = max(max_w, GetStringBoundingBox(this->house_list.GetNameOfHouseSet(i)).width);
				}
				size->width = max(size->width, max_w + padding.width);
				size->height = this->house_list.NumHouseSets() * this->GetLineHeight();
				break;
			}

			case WID_HP_HOUSE_PREVIEW:
				size->width  = max(size->width, ScaleGUITrad(4 * TILE_PIXELS) + padding.width);
				size->height = max(size->height, ScaleGUITrad(140) + padding.height); // this is slightly less then MAX_BUILDING_PIXELS, buildings will be clipped if necessary
				break;

			case WID_HP_HOUSE_NAME:
				size->width = 120; // we do not want this window to get too wide, better clip
				break;

			case WID_HP_HISTORICAL_BUILDING:
				size->width = max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HISTORICAL_BUILDING).width + padding.width);
				break;

			case WID_HP_HOUSE_POPULATION:
				SetDParam(0, 0);
				SetDParamMaxValue(0, UINT8_MAX);
				size->width = max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HOUSE_POPULATION).width + padding.width);
				break;

			case WID_HP_HOUSE_LANDSCAPE: {
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ABOVE_OR_BELOW_SNOWLINE);
				Dimension dim = GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE);
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE_GOOD);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE_BAD);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE_GOOD);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE_BAD);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				dim.width += padding.width;
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}

			case WID_HP_HOUSE_YEARS: {
				SetDParamMaxValue(1, MAX_YEAR);
				SetDParamMaxValue(3, MAX_YEAR);
				Dimension dim = { 0, 0 };
				for (uint good_bad_from = 0; good_bad_from < 2; good_bad_from++) {
					for (uint good_bad_to = 0; good_bad_to < 2; good_bad_to++) {
						SetDParam(0, STR_HOUSE_BUILD_YEAR_GOOD + good_bad_from);
						SetDParam(2, STR_HOUSE_BUILD_YEAR_GOOD + good_bad_to);
						dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_YEARS));
					}
				}
				dim.width += padding.width;
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}

			case WID_HP_HOUSE_SELECT_MATRIX:
				resize->height = 1; // don't snap to rows of this matrix
				break;

			case WID_HP_HOUSE_SELECT:
				size->width  = ScaleGUITrad(2 * TILE_PIXELS) + WD_IMGBTN_LEFT + WD_IMGBTN_RIGHT;
				size->height = ScaleGUITrad(58) + WD_IMGBTN_TOP + WD_IMGBTN_BOTTOM;
				break;

			/* these texts can be long, better clip */
			case WID_HP_HOUSE_ACCEPTANCE:
			case WID_HP_HOUSE_SUPPLY:
				size->width = 0;
				break;

			default: break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (GB(widget, 0, 16)) {
			case WID_HP_HOUSE_SETS: {
				int y = r.top + WD_MATRIX_TOP;
				int sel = this->GetCurrentHouseSet();
				for (uint i = 0; i < this->house_list.NumHouseSets(); i++) {
					DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_RIGHT, y, this->house_list.GetNameOfHouseSet(i), (int)i == sel ? TC_WHITE : TC_BLACK);
					y += this->GetLineHeight();
				}
				break;
			}

			case WID_HP_HOUSE_PREVIEW:
				DrawHouseImage(_cur_house.id, r.left, r.top, r.right, r.bottom, HIT_GUI_HOUSE_PREVIEW, _cur_house.variant);
				break;

			case WID_HP_HOUSE_SELECT: {
				HouseID house = this->house_list.GetHouseAtOffset(this->GetCurrentHouseSet(), GB(widget, 16, 16));
				int lowered = (house == _cur_house.id) ? 1 : 0;
				DrawHouseImage(house,
						r.left  + WD_IMGBTN_LEFT  + lowered, r.top    + WD_IMGBTN_TOP    + lowered,
						r.right - WD_IMGBTN_RIGHT + lowered, r.bottom - WD_IMGBTN_BOTTOM + lowered,
						HIT_GUI_HOUSE_LIST);
				/* grey out outdated houses */
				if (!this->IsHouseDisabled(house)) break;
				GfxFillRect(
						r.left + WD_BEVEL_LEFT,  r.top + WD_BEVEL_TOP,
						r.right - WD_BEVEL_LEFT, r.bottom - WD_BEVEL_BOTTOM,
						PC_BLACK, FILLRECT_CHECKER);
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (GB(widget, 0, 16)) {
			case WID_HP_HOUSE_SETS: {
				uint index = (uint)(pt.y - this->GetWidget<NWidgetBase>(widget)->pos_y) / this->GetLineHeight();
				if (index < this->house_list.NumHouseSets()) {
					this->SelectOtherHouse(index, -1, -1, -1);
				}
				break;
			}

			case WID_HP_PREV_VARIANT:
				this->SelectOtherHouse(-1, -1, _cur_house.variant - 1, -1);
				break;

			case WID_HP_NEXT_VARIANT:
				this->SelectOtherHouse(-1, -1, _cur_house.variant + 1, -1);
				break;

			case WID_HP_HOUSE_SELECT:
				this->SelectOtherHouse(-1, GB(widget, 16, 16), -1, 1);
				break;
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		DeleteWindowById(WC_SELECT_TOWN, 0);

		TownList towns;
		CommandCost ret = ListTownsToJoinHouseTo(_cur_house.id, tile, &towns);
		if (ret.Failed()) {
			ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HOUSE_HERE, ret.GetErrorMessage(), WL_INFO);
			return;
		}

		CommandContainer cmd = {
			tile,
			_cur_house.id, // p1 - house type and town index (town not yet set)
			InteractiveRandomRange(1 << 8), // p2 - 8 random bits for the house
			CMD_BUILD_HOUSE | CMD_MSG(STR_ERROR_CAN_T_BUILD_HOUSE_HERE),
			CcFoundTown,
			""
		};

		/* Place the house right away if CTRL is not pressed. */
		if (!_ctrl_pressed) {
			SB(cmd.p1, 16, 16, towns[0]);
			DoCommandP(&cmd);
			return;
		}

		/* Check if the place is buildable. */
		ret = CheckFlatLandHouse(_cur_house.id, tile);
		if (ret.Failed()) {
			ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HOUSE_HERE, ret.GetErrorMessage(), WL_INFO);
			return;
		}

		/* Show the joiner window. */
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
		ShowSelectTownWindow(towns, cmd);
	}

	virtual void OnPlaceObjectAbort()
	{
		this->SelectOtherHouse(-1, -1, -1, 0);
		this->tileselect_zone = HZB_END;
		this->tileselect_bad_land = false;
	}

	virtual void OnTick()
	{
		if (this->IsObjectToPlaceSet() && (_thd.dirty & 4)) {
			_thd.dirty &= ~4;

			HouseZonesBits prev_zone = this->tileselect_zone;
			bool prev_land = this->tileselect_bad_land;

			this->tileselect_zone = HZB_END;
			this->tileselect_bad_land = false;
			if (_thd.drawstyle == HT_RECT) {
				TileIndex tile = TileVirtXY(_thd.pos.x, _thd.pos.y);
				if (tile < MapSize()) {
					/* find best town zone */
					const Town *t;
					FOR_ALL_TOWNS(t) {
						if (CheckHouseDistanceFromTown(t, tile, false).Failed()) continue;
						HouseZonesBits zone = GetTownRadiusGroup(t, tile);
						if (!IsInsideMM(this->tileselect_zone, zone, HZB_END)) this->tileselect_zone = zone;
					}
					/* check the snowline */
					if (_settings_game.game_creation.landscape == LT_ARCTIC) {
						HouseZones zone = HouseSpec::Get(_cur_house.id)->building_availability & (HZ_SUBARTC_ABOVE | HZ_SUBARTC_BELOW);
						this->tileselect_bad_land = HasExactlyOneBit(zone) && ((GetTileMaxZ(tile) > HighestSnowLine()) != (zone == HZ_SUBARTC_ABOVE));
					}
				}
			}

			if (prev_zone != this->tileselect_zone) this->SetWidgetDirty(WID_HP_HOUSE_ZONES);
			if (prev_land != this->tileselect_bad_land) this->SetWidgetDirty(WID_HP_HOUSE_LANDSCAPE);
		}
	}
};

static const NWidgetPart _nested_house_picker_widgets[] = {
	/* TOP */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_HP_CAPTION), SetDataTip(STR_HOUSE_BUILD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_SELECTION, COLOUR_DARK_GREEN, WID_HP_MAIN_PANEL_SEL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_HP_HOUSE_SELECT_SCROLL),
			/* MIDDLE */
			NWidget(NWID_HORIZONTAL), SetPIP(5, 0, 0),
				/* LEFT */
				NWidget(NWID_VERTICAL), SetPIP(5, 2, 2),
					/* LIST OF HOUSE SETS */
					NWidget(NWID_SELECTION, COLOUR_DARK_GREEN, WID_HP_HOUSE_SETS_SEL),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_MATRIX, COLOUR_GREY, WID_HP_HOUSE_SETS), SetMinimalSize(0, 60), SetFill(1, 0), SetResize(0, 0),
									SetMatrixDataTip(1, 1, STR_HOUSE_BUILD_HOUSESET_LIST_TOOLTIP),
						EndContainer(),
					EndContainer(),
					/* HOUSE PICTURE AND PREV/NEXT BUTTONS */
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_VERTICAL),
							NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 1),
							NWidget(NWID_HORIZONTAL),
								NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
								NWidget(NWID_SELECTION, COLOUR_DARK_GREEN, WID_HP_PREV_VARIANT_SEL),
									NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_HP_PREV_VARIANT), SetDataTip(SPR_ARROW_LEFT, STR_NULL),
								EndContainer(),
							EndContainer(),
						EndContainer(),
						NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_PREVIEW), SetFill(0, 1), SetResize(0, 1), SetPadding(0, 8, 0, 8),
						NWidget(NWID_VERTICAL),
							NWidget(NWID_SPACER), SetFill(1, 1), SetResize(1, 1),
							NWidget(NWID_HORIZONTAL),
								NWidget(NWID_SELECTION, COLOUR_DARK_GREEN, WID_HP_NEXT_VARIANT_SEL),
									NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_HP_NEXT_VARIANT), SetDataTip(SPR_ARROW_RIGHT, STR_NULL),
								EndContainer(),
								NWidget(NWID_SPACER), SetFill(1, 0), SetResize(1, 0),
							EndContainer(),
						EndContainer(),
					EndContainer(),
					/* HOUSE LABEL */
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_HP_HOUSE_NAME), SetDataTip(STR_HOUSE_BUILD_HOUSE_NAME, STR_NULL), SetMinimalSize(120, 0), SetPadding(5, 0, 0, 0),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_HP_HISTORICAL_BUILDING), SetDataTip(STR_JUST_STRING, STR_NULL),
					/* HOUSE INFOS (SHORT TEXTS) */
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_POPULATION), SetDataTip(STR_HOUSE_BUILD_HOUSE_POPULATION, STR_NULL), SetPadding(5, 0, 0, 0),
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_ZONES), SetDataTip(STR_HOUSE_BUILD_HOUSE_ZONES, STR_NULL),
					NWidget(NWID_SELECTION, COLOUR_DARK_GREEN, WID_HP_HOUSE_LANDSCAPE_SEL),
						NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_LANDSCAPE), SetDataTip(STR_HOUSE_BUILD_LANDSCAPE, STR_NULL),
					EndContainer(),
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_YEARS), SetDataTip(STR_HOUSE_BUILD_YEARS, STR_NULL),
				EndContainer(),
				/* RIGHT: MATRIX OF HOUSES */
				NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_HP_HOUSE_SELECT_MATRIX), SetPIP(0, 2, 0), SetPadding(5, 2, 2, 4), SetScrollbar(WID_HP_HOUSE_SELECT_SCROLL),
					NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_HP_HOUSE_SELECT),
							SetDataTip(0x0, STR_HOUSE_BUILD_SELECT_HOUSE_TOOLTIP), SetScrollbar(WID_HP_HOUSE_SELECT_SCROLL),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_HP_HOUSE_SELECT_SCROLL),
			EndContainer(),
			/* BOTTOM */
			NWidget(NWID_HORIZONTAL), SetPIP(5, 2, 0),
				/* HOUSE INFOS (LONG TEXTS) */
				NWidget(NWID_VERTICAL), SetPIP(0, 2, 5),
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_ACCEPTANCE), SetDataTip(STR_HOUSE_BUILD_ACCEPTED_CARGO, STR_NULL), SetFill(1, 0), SetResize(1, 0),
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_SUPPLY), SetDataTip(STR_HOUSE_BUILD_SUPPLIED_CARGO, STR_NULL), SetFill(1, 0), SetResize(1, 0),
				EndContainer(),
				/* RESIZE BOX */
				NWidget(NWID_VERTICAL),
					NWidget(NWID_SPACER), SetFill(0, 1),
					NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _house_picker_desc(
	WDP_AUTO, "build_house", 0, 0,
	WC_BUILD_HOUSE, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_house_picker_widgets, lengthof(_nested_house_picker_widgets)
);

/**
 * Show our house picker.
 */
void ShowBuildHousePicker()
{
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	HousePickerWindow *w = AllocateWindowDescFront<HousePickerWindow>(&_house_picker_desc, 0, true);
	if (w != NULL) w->SelectOtherHouse(-1, -1, -1, 1); // push the button
}


/**
 * Window for selecting towns to build a house in.
 */
struct SelectTownWindow : Window {
	TownList towns;       ///< list of towns
	CommandContainer cmd; ///< command to build the house (CMD_BUILD_HOUSE)
	Scrollbar *vscroll;   ///< scrollbar for the town list

	SelectTownWindow(WindowDesc *desc, const TownList &towns, const CommandContainer &cmd) : Window(desc), towns(towns), cmd(cmd)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_ST_SCROLLBAR);
		this->vscroll->SetCount(this->towns.Length());
		this->FinishInitNested();
	}

	/**
	 * Get list i-th item string. Appropriate string parameters will be set.
	 * @param i Index of the item.
	 * @return The string.
	 */
	StringID GetTownString(uint i) const
	{
		SetDParam(0, this->towns[i]);
		if (CheckHouseDistanceFromTown(Town::Get(this->towns[i]), this->cmd.tile, false).Failed()) {
			return STR_SELECT_TOWN_LIST_TOWN_OUTSIDE;
		}
		SetDParam(1, GetTownRadiusGroup(Town::Get(this->towns[i]), this->cmd.tile) + 1);
		return STR_SELECT_TOWN_LIST_TOWN_ZONE;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget != WID_ST_PANEL) return;

		/* Determine the widest string */
		Dimension d = { 0, 0 };
		for (uint i = 0; i < this->towns.Length(); i++) {
			d = maxdim(d, GetStringBoundingBox(this->GetTownString(i)));
		}

		resize->height = d.height;
		d.height *= 5;
		d.width += WD_FRAMERECT_RIGHT + WD_FRAMERECT_LEFT;
		d.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		*size = d;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_ST_PANEL) return;

		uint y = r.top + WD_FRAMERECT_TOP;
		uint end = min(this->vscroll->GetCount(), this->vscroll->GetPosition() + this->vscroll->GetCapacity());
		for (uint i = this->vscroll->GetPosition(); i < end; i++) {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, this->GetTownString(i));
			y += this->resize.step_height;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget != WID_ST_PANEL) return;

		uint pos = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ST_PANEL, WD_FRAMERECT_TOP);
		if (pos >= this->towns.Length()) return;

		/* Place a house */
		SB(this->cmd.p1, 16, 16, this->towns[pos]);
		DoCommandP(&this->cmd);

		/* Close the window */
		delete this;
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_ST_PANEL, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}
};

static const NWidgetPart _nested_select_town_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ST_CAPTION), SetDataTip(STR_SELECT_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_ST_PANEL), SetResize(1, 0), SetScrollbar(WID_ST_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_ST_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _select_town_desc(
	WDP_AUTO, "select_town", 100, 0,
	WC_SELECT_TOWN, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_select_town_widgets, lengthof(_nested_select_town_widgets)
);

static void ShowSelectTownWindow(const TownList &towns, const CommandContainer &cmd)
{
	DeleteWindowByClass(WC_SELECT_TOWN);
	new SelectTownWindow(&_select_town_desc, towns, cmd);
}

/** Helper class for sorting a list of towns to join house to. */
struct TownsToJoinHouseToListSorter {
	TileIndex tile; // Tile where the house is about to be placed.

	bool operator () (const TownID &a, const TownID &b) const
	{
		uint dist_a, dist_b, inner_a, inner_b, outer_a, outer_b;
		HouseZonesBits zone_a = TryGetTownRadiusGroup(Town::Get(a), this->tile, &dist_a, &inner_a, &outer_a);
		HouseZonesBits zone_b = TryGetTownRadiusGroup(Town::Get(b), this->tile, &dist_b, &inner_b, &outer_b);

		if (zone_a != zone_b) return zone_a != HZB_END && (zone_b == HZB_END || zone_a > zone_b);

		if (zone_a == HZB_END) return dist_a - inner_a < dist_b - inner_b;

		return (uint64)(dist_a - inner_a) * (uint64)(outer_b - inner_b) <
				(uint64)(dist_b - inner_b) * (uint64)(outer_a - inner_a);
	}
};

/**
 * Make a list of towns for which a house can be joined to.
 *
 * @param house Type of the house to join.
 * @param tile Tile where the house is about to be placed.
 * @param [out] towns Container where sorted towns will be stored.
 * @return Success or an error.
 */
static CommandCost ListTownsToJoinHouseTo(HouseID house, TileIndex tile, TownList *towns)
{
	const bool deity = (_game_mode == GM_EDITOR);
	StringID error = STR_ERROR_MUST_FOUND_TOWN_FIRST;
	const Town *t;
	FOR_ALL_TOWNS(t) {
		CommandCost ret = CheckHouseDistanceFromTown(t, tile, deity);
		if (ret.Failed()) {
			if (error == STR_ERROR_MUST_FOUND_TOWN_FIRST) error = ret.GetErrorMessage();
			continue;
		}
		if (!deity && !HasBit(HouseSpec::Get(house)->building_availability, GetTownRadiusGroup(t, tile))) {
			error = STR_ERROR_BUILDING_NOT_ALLOWED_IN_THIS_TOWN_ZONE;
			continue;
		}
		*(towns->Append()) = t->index;
	}

	if (towns->Length() == 0) return CommandCost(error);
	TownsToJoinHouseToListSorter compare = { tile };
	std::sort(towns->Begin(), towns->End(), compare);
	return CommandCost();
}
