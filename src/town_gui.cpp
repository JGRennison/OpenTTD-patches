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
#include "stringfilter_type.h"
#include "widgets/dropdown_func.h"
#include "newgrf_config.h"
#include "newgrf_house.h"
#include "date_func.h"
#include "core/random_func.hpp"
#include "town_kdtree.h"

#include "widgets/town_widget.h"
#include "table/strings.h"
#include "newgrf_debug.h"
#include <algorithm>

#include "safeguards.h"

TownKdtree _town_local_authority_kdtree(&Kdtree_TownXYFunc);

typedef GUIList<const Town*> GUITownList;

static void PlaceProc_House(TileIndex tile);

static const NWidgetPart _nested_town_authority_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TA_CAPTION), SetDataTip(STR_LOCAL_AUTHORITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TA_ZONE_BUTTON), SetMinimalSize(50, 0), SetMinimalTextLines(1, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM + 2), SetDataTip(STR_LOCAL_AUTHORITY_ZONE, STR_LOCAL_AUTHORITY_ZONE_TOOLTIP),
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

	void OnPaint() override
	{
		int numact;
		uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
		if (buttons != displayed_actions_on_previous_painting) this->SetDirty();
		displayed_actions_on_previous_painting = buttons;

		this->vscroll->SetCount(numact + 1);

		if (this->sel_index != -1 && !HasBit(buttons, this->sel_index)) {
			this->sel_index = -1;
		}

		this->SetWidgetLoweredState(WID_TA_ZONE_BUTTON, this->town->show_zone);
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
		for (const Company *c : Company::Iterate()) {
			if ((HasBit(this->town->have_ratings, c->index) || this->town->exclusivity == c->index)) {
				DrawCompanyIcon(c->index, icon_left, y + icon_y_offset);

				SetDParam(0, c->index);
				SetDParam(1, c->index);

				int r = this->town->ratings[c->index];
				StringID str = STR_CARGO_RATING_APPALLING;
				if (r > RATING_APPALLING) str++;
				if (r > RATING_VERYPOOR)  str++;
				if (r > RATING_POOR)      str++;
				if (r > RATING_MEDIOCRE)  str++;
				if (r > RATING_GOOD)      str++;
				if (r > RATING_VERYGOOD)  str++;
				if (r > RATING_EXCELLENT) str++;

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
			ResizeWindow(this, 0, y - nwid->current_y, false);
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TA_CAPTION) SetDParam(0, this->window_number);
	}

	void DrawWidget(const Rect &r, int widget) const override
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

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
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

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_TA_ZONE_BUTTON: {
				bool new_show_state = !this->town->show_zone;
				TownID index = this->town->index;

				new_show_state ? _town_local_authority_kdtree.Insert(index) : _town_local_authority_kdtree.Remove(index);

				this->town->show_zone = new_show_state;
				this->SetWidgetLoweredState(widget, new_show_state);
				MarkWholeScreenDirty();
				break;
			}

			case WID_TA_COMMAND_LIST: {
				int y = this->GetRowFromWidget(pt.y, WID_TA_COMMAND_LIST, 1, FONT_HEIGHT_NORMAL);
				if (!IsInsideMM(y, 0, 5)) return;

				y = GetNthSetBit(GetMaskOfTownActions(nullptr, _local_company, this->town), y + this->vscroll->GetPosition() - 1);
				if (y >= 0) {
					this->sel_index = y;
					this->SetDirty();
				}
				/* When double-clicking, continue */
				if (click_count == 1 || y < 0) break;
				FALLTHROUGH;
			}

			case WID_TA_EXECUTE:
				DoCommandP(this->town->xy, this->window_number, this->sel_index, CMD_DO_TOWN_ACTION | CMD_MSG(STR_ERROR_CAN_T_DO_THIS));
				break;
		}
	}

	void OnHundredthTick() override
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
	}

	~TownViewWindow()
	{
		SetViewportCatchmentTown(Town::Get(this->window_number), false);
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TV_CAPTION) SetDParam(0, this->town->index);
	}

	void OnPaint() override
	{
		extern const Town *_viewport_highlight_town;
		this->SetWidgetLoweredState(WID_TV_CATCHMENT, _viewport_highlight_town == this->town);
		this->SetWidgetDisabledState(WID_TV_CHANGE_NAME, _networking && !(_network_server || _network_settings_access));

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_TV_INFO) return;

		uint y = r.top + WD_FRAMERECT_TOP;

		SetDParam(0, this->town->cache.population);
		SetDParam(1, this->town->cache.num_houses);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y, STR_TOWN_VIEW_POPULATION_HOUSES);

		SetDParam(0, 1 << CT_PASSENGERS);
		SetDParam(1, this->town->supplied[CT_PASSENGERS].old_act);
		SetDParam(2, this->town->supplied[CT_PASSENGERS].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_CARGO_LAST_MONTH_MAX);

		SetDParam(0, 1 << CT_MAIL);
		SetDParam(1, this->town->supplied[CT_MAIL].old_act);
		SetDParam(2, this->town->supplied[CT_MAIL].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_CARGO_LAST_MONTH_MAX);

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
			assert(cargo != nullptr);

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
			SetDParam(0, RoundDivSU(this->town->growth_rate + 1, DAY_TICKS));
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

		if (!this->town->text.empty()) {
			SetDParamStr(0, this->town->text.c_str());
			DrawStringMultiLine(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, UINT16_MAX, STR_JUST_RAW_STRING, TC_BLACK);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
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

			case WID_TV_CATCHMENT:
				SetViewportCatchmentTown(Town::Get(this->window_number), !this->IsWidgetLowered(WID_TV_CATCHMENT));
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

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
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

		if (!this->town->text.empty()) {
			SetDParamStr(0, this->town->text.c_str());
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

	void OnResize() override
	{
		if (this->viewport != nullptr) {
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
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* Called when setting station noise or required cargoes have changed, in order to resize the window */
		this->SetDirty(); // refresh display for current size. This will allow to avoid glitches when downgrading
		this->ResizeWindowAsNeeded();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_TOWN | CMD_MSG(STR_ERROR_CAN_T_RENAME_TOWN), nullptr, str);
	}

	bool IsNewGRFInspectable() const override
	{
		return ::IsNewGRFInspectable(GSF_FAKE_TOWNS, this->window_number);
	}

	void ShowNewGRFInspectWindow() const override
	{
		::ShowNewGRFInspectWindow(GSF_FAKE_TOWNS, this->window_number);
	}
};

static const NWidgetPart _nested_town_game_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEBUGBOX, COLOUR_BROWN),
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
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TV_CATCHMENT), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_BUTTON_CATCHMENT, STR_TOOLTIP_CATCHMENT),
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
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TV_CATCHMENT), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_BUTTON_CATCHMENT, STR_TOOLTIP_CATCHMENT),
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
				NWidget(WWT_EDITBOX, COLOUR_BROWN, WID_TD_FILTER), SetFill(35, 12), SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
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

	/* Constants for sorting towns */
	static const StringID sorter_names[];
	static GUITownList::SortFunction * const sorter_funcs[];

	StringFilter string_filter;             ///< Filter for towns
	QueryString townname_editbox;           ///< Filter editbox

	GUITownList towns;

	Scrollbar *vscroll;

	void BuildSortTownList()
	{
		if (this->towns.NeedRebuild()) {
			this->towns.clear();

			for (const Town *t : Town::Iterate()) {
				if (this->string_filter.IsEmpty()) {
					this->towns.push_back(t);
					continue;
				}
				this->string_filter.ResetState();
				this->string_filter.AddLine(t->GetCachedName());
				if (this->string_filter.GetState()) this->towns.push_back(t);
			}

			this->towns.shrink_to_fit();
			this->towns.RebuildDone();
			this->vscroll->SetCount((uint)this->towns.size()); // Update scrollbar as well.
		}
		/* Always sort the towns. */
		this->towns.Sort();
		this->SetWidgetDirty(WID_TD_LIST); // Force repaint of the displayed towns.
	}

	/** Sort by town name */
	static bool TownNameSorter(const Town * const &a, const Town * const &b)
	{
		return strnatcmp(a->GetCachedName(), b->GetCachedName()) < 0; // Sort by name (natural sorting).
	}

	/** Sort by population (default descending, as big towns are of the most interest). */
	static bool TownPopulationSorter(const Town * const &a, const Town * const &b)
	{
		uint32 a_population = a->cache.population;
		uint32 b_population = b->cache.population;
		if (a_population == b_population) return TownDirectoryWindow::TownNameSorter(a, b);
		return a_population < b_population;
	}

	/** Sort by town rating */
	static bool TownRatingSorter(const Town * const &a, const Town * const &b)
	{
		bool before = !TownDirectoryWindow::last_sorting.order; // Value to get 'a' before 'b'.

		/* Towns without rating are always after towns with rating. */
		if (HasBit(a->have_ratings, _local_company)) {
			if (HasBit(b->have_ratings, _local_company)) {
				int16 a_rating = a->ratings[_local_company];
				int16 b_rating = b->ratings[_local_company];
				if (a_rating == b_rating) return TownDirectoryWindow::TownNameSorter(a, b);
				return a_rating < b_rating;
			}
			return before;
		}
		if (HasBit(b->have_ratings, _local_company)) return !before;

		/* Sort unrated towns always on ascending town name. */
		if (before) return TownDirectoryWindow::TownNameSorter(a, b);
		return !TownDirectoryWindow::TownNameSorter(a, b);
	}

public:
	TownDirectoryWindow(WindowDesc *desc) : Window(desc), townname_editbox(MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH, MAX_LENGTH_TOWN_NAME_CHARS)
	{
		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_TD_SCROLLBAR);

		this->towns.SetListing(this->last_sorting);
		this->towns.SetSortFuncs(TownDirectoryWindow::sorter_funcs);
		this->towns.ForceRebuild();
		this->BuildSortTownList();

		this->FinishInitNested(0);

		this->querystrings[WID_TD_FILTER] = &this->townname_editbox;
		this->townname_editbox.cancel_button = QueryString::ACTION_CLEAR;
	}

	void SetStringParameters(int widget) const override
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

	/**
	 * Get the string to draw the town name.
	 * @param t Town to draw.
	 * @return The string to use.
	 */
	static StringID GetTownString(const Town *t)
	{
		return t->larger_town ? STR_TOWN_DIRECTORY_CITY : STR_TOWN_DIRECTORY_TOWN;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_TD_SORT_ORDER:
				this->DrawSortButtonState(widget, this->towns.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_TD_LIST: {
				int n = 0;
				int y = r.top + WD_FRAMERECT_TOP;
				if (this->towns.size() == 0) { // No towns available.
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right, y, STR_TOWN_DIRECTORY_NONE);
					break;
				}

				/* At least one town available. */
				bool rtl = _current_text_dir == TD_RTL;
				Dimension icon_size = GetSpriteSize(SPR_TOWN_RATING_GOOD);
				int text_left  = r.left + WD_FRAMERECT_LEFT + (rtl ? 0 : icon_size.width + 2);
				int text_right = r.right - WD_FRAMERECT_RIGHT - (rtl ? icon_size.width + 2 : 0);
				int icon_x = rtl ? r.right - WD_FRAMERECT_RIGHT - icon_size.width : r.left + WD_FRAMERECT_LEFT;

				for (uint i = this->vscroll->GetPosition(); i < this->towns.size(); i++) {
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
					DrawString(text_left, text_right, y + (this->resize.step_height - FONT_HEIGHT_NORMAL) / 2, GetTownString(t));

					y += this->resize.step_height;
					if (++n == this->vscroll->GetCapacity()) break; // max number of towns in 1 window
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
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
				for (uint i = 0; i < this->towns.size(); i++) {
					const Town *t = this->towns[i];

					assert(t != nullptr);

					SetDParam(0, t->index);
					SetDParamMaxDigits(1, 8);
					d = maxdim(d, GetStringBoundingBox(GetTownString(t)));
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

	void OnClick(Point pt, int widget, int click_count) override
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
				if (id_v >= this->towns.size()) return; // click out of town bounds

				const Town *t = this->towns[id_v];
				assert(t != nullptr);
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(t->xy);
				} else {
					ScrollMainWindowToTile(t->xy);
				}
				break;
			}
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		if (widget != WID_TD_SORT_CRITERIA) return;

		if (this->towns.SortType() != index) {
			this->towns.SetSortType(index);
			this->last_sorting = this->towns.GetListing(); // Store new sorting order.
			this->BuildSortTownList();
		}
	}

	void OnPaint() override
	{
		if (this->towns.NeedRebuild()) this->BuildSortTownList();
		this->DrawWidgets();
	}

	void OnHundredthTick() override
	{
		this->BuildSortTownList();
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_TD_LIST);
	}

	void OnEditboxChanged(int wid) override
	{
		if (wid == WID_TD_FILTER) {
			this->string_filter.SetFilterTerm(this->townname_editbox.text.buf);
			this->InvalidateData(TDIWD_FORCE_REBUILD);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		switch (data) {
			case TDIWD_FORCE_REBUILD:
				/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
				this->towns.ForceRebuild();
				break;

			case TDIWD_POPULATION_CHANGE:
				if (this->towns.SortType() == 1) this->towns.ForceResort();
				break;

			default:
				this->towns.ForceResort();
		}
	}
};

Listing TownDirectoryWindow::last_sorting = {false, 0};

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

void CcFoundTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_SPLAT_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

void CcFoundRandomTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint32 cmd)
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
		const char *name = nullptr;

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

	void OnClick(Point pt, int widget, int click_count) override
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

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		this->ExecuteFoundTownCommand(tile, false, STR_ERROR_CAN_T_FOUND_TOWN_HERE, CcFoundTown);
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
		this->UpdateButtons(false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
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
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<FoundTownWindow>(&_found_town_desc, 0);
}

class GUIHouseList : public std::vector<HouseID> {
protected:
	std::vector<uint16> house_sets; ///< list of house sets, each item points the first house of the set in the houses array

	static bool HouseSorter(const HouseID &a, const HouseID &b)
	{
		const HouseSpec *a_hs = HouseSpec::Get(a);
		const GRFFile *a_set = a_hs->grf_prop.grffile;
		const HouseSpec *b_hs = HouseSpec::Get(b);
		const GRFFile *b_set = b_hs->grf_prop.grffile;

		int ret = (a_set != nullptr) - (b_set != nullptr);
		if (ret == 0) {
			if (a_set != nullptr) {
				assert_compile(sizeof(a_set->grfid) <= sizeof(int));
				ret = a_set->grfid - b_set->grfid;
				if (ret == 0) ret = a_hs->grf_prop.local_id - b_hs->grf_prop.local_id;
			} else {
				ret = a - b;
			}
		}
		return ret < 0;
	}

public:
	GUIHouseList()
	{
		this->house_sets.push_back(0); // terminator
	}

	inline HouseID GetHouseAtOffset(uint house_set, uint house_offset) const
	{
		return (*this)[this->house_sets[house_set] + house_offset];
	}

	uint NumHouseSets() const
	{
		return this->house_sets.size() - 1; // last item is a terminator
	}

	uint NumHousesInHouseSet(uint house_set) const
	{
		assert(house_set < this->NumHouseSets());
		/* There is a terminator on the list of house sets. It's equal to the number
		 * of all houses. We can safely use "house_set + 1" even for the last
		 * house set. */
		return this->house_sets[house_set + 1] - this->house_sets[house_set];
	}

	int FindHouseSet(HouseID house) const
	{
		const GRFFile *house_set = HouseSpec::Get(house)->grf_prop.grffile;
		for (uint i = 0; i < this->NumHouseSets(); i++) {
			if (HouseSpec::Get(this->GetHouseAtOffset(i, 0))->grf_prop.grffile == house_set) return i;
		}
		return -1;
	}

	int FindHouseOffset(uint house_set, HouseID house) const
	{
		assert(house_set < this->NumHouseSets());
		uint count = this->NumHousesInHouseSet(house_set);
		for (uint i = 0; i < count; i++) {
			if (this->GetHouseAtOffset(house_set, i) == house) return i;
		}
		return -1;
	}

	const char *GetNameOfHouseSet(uint house_set) const
	{
		assert(house_set < this->NumHouseSets());
		const GRFFile *gf = HouseSpec::Get(this->GetHouseAtOffset(house_set, 0))->grf_prop.grffile;
		if (gf != nullptr) return GetGRFConfig(gf->grfid)->GetName();

		static char name[DRAW_STRING_BUFFER];
		GetString(name, STR_BASIC_HOUSE_SET_NAME, lastof(name));
		return name;
	}

	/**
	 * Notify the sortlist that the rebuild is done
	 *
	 * @note This forces a resort
	 */
	void Build()
	{
		/* collect items */
		this->clear();
		for (HouseID house = 0; house < NUM_HOUSES; house++) {
			const HouseSpec *hs = HouseSpec::Get(house);
			/* is the house enabled? */
			if (!hs->enabled) continue;
			/* is the house overriden? */
			if (hs->grf_prop.override != INVALID_HOUSE_ID) continue;
			/* is the house allownd in current landscape? */
			HouseZones landscapes = (HouseZones)(HZ_TEMP << _settings_game.game_creation.landscape);
			if (_settings_game.game_creation.landscape == LT_ARCTIC) landscapes |= HZ_SUBARTC_ABOVE;
			if (!(hs->building_availability & landscapes)) continue;
			/* is the house allowed at any of house zones at all? */
			if (!(hs->building_availability & HZ_ZONALL)) continue;
			/* is there any year in which the house is allowed? */
			if (hs->min_year > hs->max_year) continue;

			/* add the house */
			this->push_back(house);
		}

		/* arrange items */
		std::sort(this->begin(), this->end(), HouseSorter);

		/* list house sets */
		this->house_sets.clear();
		const GRFFile *last_set = nullptr;
		for (uint i = 0; i < this->size(); i++) {
			const HouseSpec *hs = HouseSpec::Get((*this)[i]);
			/* add house set */
			if (this->house_sets.size() == 0 || last_set != hs->grf_prop.grffile) {
				last_set = hs->grf_prop.grffile;
				this->house_sets.push_back(i);
			}
		}
		/* put a terminator on the list to make counting easier */
		this->house_sets.push_back(this->size());
	}
};

static HouseID _cur_house = INVALID_HOUSE_ID; ///< house selected in the house picker window

/** The window used for building houses. */
class HousePickerWindow : public Window {
protected:
	GUIHouseList house_list; ///< list of houses and house sets
	int house_offset;        ///< index of selected house
	uint house_set;          ///< index of selected house set
	uint line_height;        ///< height of a single line in the list of house sets
	HouseID display_house;   ///< house ID of currently displayed house

	void RestoreSelectedHouseIndex()
	{
		this->house_set = 0;
		this->house_offset = 0;

		if (this->house_list.size() == 0) { // no houses at all?
			_cur_house = INVALID_HOUSE_ID;
			this->display_house = _cur_house;
			return;
		}

		if (_cur_house != INVALID_HOUSE_ID) {
			int house_set = this->house_list.FindHouseSet(_cur_house);
			if (house_set >= 0) {
				this->house_set = house_set;
				int house_offset = this->house_list.FindHouseOffset(house_set, _cur_house);
				if (house_offset >= 0) {
					this->house_offset = house_offset;
					return;
				}
			}
		}
		_cur_house = this->house_list.GetHouseAtOffset(this->house_set, this->house_offset);
		this->display_house = _cur_house;
	}

	void SelectHouseIntl(uint new_house_set, int new_house_offset)
	{
		SetObjectToPlaceWnd(SPR_CURSOR_TOWN, PAL_NONE, HT_RECT, this);
		this->house_set = new_house_set;
		this->house_offset = new_house_offset;
		_cur_house = this->house_list.GetHouseAtOffset(new_house_set, new_house_offset);
		this->display_house = _cur_house;
	}

	/**
	 * Select another house.
	 * @param new_house_set index of the house set
	 * @param new_house_offset offset of the house
	 */
	void SelectOtherHouse(uint new_house_set, int new_house_offset)
	{
		assert(new_house_set < this->house_list.NumHouseSets());
		assert(new_house_offset < (int) this->house_list.NumHousesInHouseSet(new_house_set));
		assert(new_house_offset >= 0);

		SelectHouseIntl(new_house_set, new_house_offset);

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
		matrix->SetCount(this->house_list.NumHousesInHouseSet(this->house_set));
		matrix->SetClicked(this->house_offset);
		this->UpdateSelectSize();
		this->SetDirty();
	}

	void UpdateSelectSize()
	{
		uint w = 1, h = 1;
		if (_cur_house != INVALID_HOUSE_ID) {
			const HouseSpec *hs = HouseSpec::Get(_cur_house);
			if (hs->building_flags & BUILDING_2_TILES_X) w++;
			if (hs->building_flags & BUILDING_2_TILES_Y) h++;
		}
		SetTileSelectSize(w, h);
	}

public:
	HousePickerWindow(WindowDesc *desc, WindowNumber number) : Window(desc)
	{
		this->CreateNestedTree();
		/* there is no shade box but we will shade the window if there is no house to show */
		this->shade_select = this->GetWidget<NWidgetStacked>(WID_HP_MAIN_PANEL_SEL);
		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
		matrix->SetScrollbar(this->GetScrollbar(WID_HP_HOUSE_SELECT_SCROLL));
		this->FinishInitNested(number);

		if (_cur_house != INVALID_HOUSE_ID) matrix->SetClicked(this->house_offset); // set clicked item again to make it visible
	}

	virtual void OnInit() OVERRIDE
	{
		this->house_list.Build();
		this->RestoreSelectedHouseIndex();
		this->UpdateSelectSize();

		/* if we have exactly one set of houses and it's not the default one then display it's name in the title bar */
		this->GetWidget<NWidgetCore>(WID_HP_CAPTION)->widget_data =
				(this->house_list.NumHouseSets() == 1 && HouseSpec::Get(this->house_list[0])->grf_prop.grffile != nullptr) ?
				STR_HOUSE_BUILD_CUSTOM_CAPTION : STR_HOUSE_BUILD_CAPTION;

		/* hide widgets if we have no houses to show */
		this->SetShaded(this->house_list.size() == 0);

		if (this->house_list.size() != 0) {
			/* show the list of house sets if we have at least 2 items to show */
			this->GetWidget<NWidgetStacked>(WID_HP_HOUSE_SETS_SEL)->SetDisplayedPlane(this->house_list.NumHouseSets() > 1 ? 0 : SZSP_NONE);
			/* set number of items in the list of house sets */
			this->GetWidget<NWidgetCore>(WID_HP_HOUSE_SETS)->widget_data = (this->house_list.NumHouseSets() << MAT_ROW_START) | (1 << MAT_COL_START);
			/* show the landscape info only in arctic climate (above/below snowline) */
			this->GetWidget<NWidgetStacked>(WID_HP_HOUSE_LANDSCAPE_SEL)->SetDisplayedPlane(_settings_game.game_creation.landscape == LT_ARCTIC ? 0 : SZSP_NONE);
			/* update the matrix of houses */
			NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
			matrix->SetCount(this->house_list.NumHousesInHouseSet(this->house_set));
			matrix->SetClicked(this->house_offset);
			SelectHouseIntl(this->house_set, this->house_offset);
		} else {
			ResetObjectToPlace();
		}
	}

	virtual void SetStringParameters(int widget) const override
	{
		if (widget == WID_HP_CAPTION) {
			if (this->house_list.NumHouseSets() == 1) SetDParamStr(0, this->house_list.GetNameOfHouseSet(0));
		} else if (this->display_house == INVALID_HOUSE_ID) {
			switch (widget) {
				case WID_HP_CAPTION:
					break;

				case WID_HP_HOUSE_ZONES:
					for (int i = 0; i < HZB_END; i++) {
						SetDParam(2 * i, STR_HOUSE_BUILD_HOUSE_ZONE_DISABLED);
						SetDParam(2 * i + 1, i + 1);
					}
					break;

				case WID_HP_HOUSE_YEARS:
					SetDParam(0, STR_HOUSE_BUILD_YEARS_BAD_YEAR);
					SetDParam(1, 0);
					SetDParam(2, STR_HOUSE_BUILD_YEARS_BAD_YEAR);
					SetDParam(3, 0);
					break;

				case WID_HP_HOUSE_ACCEPTANCE:
					SetDParamStr(0, "");
					break;

				case WID_HP_HOUSE_SUPPLY:
					SetDParam(0, 0);
					break;

				default:
					SetDParam(0, STR_EMPTY);
					break;
			}
		} else {
			switch (widget) {
				case WID_HP_HOUSE_NAME:
					SetDParam(0, GetHouseName(this->display_house));
					break;

				case WID_HP_HISTORICAL_BUILDING:
					SetDParam(0, HouseSpec::Get(this->display_house)->extra_flags & BUILDING_IS_HISTORICAL ? STR_HOUSE_BUILD_HISTORICAL_BUILDING : STR_EMPTY);
					break;

				case WID_HP_HOUSE_POPULATION:
					SetDParam(0, HouseSpec::Get(this->display_house)->population);
					break;

				case WID_HP_HOUSE_ZONES: {
					HouseZones zones = (HouseZones)(HouseSpec::Get(this->display_house)->building_availability & HZ_ZONALL);
					for (int i = 0; i < HZB_END; i++) {
						/* colour: gold(enabled)/grey(disabled)  */
						SetDParam(2 * i, HasBit(zones, HZB_END - i - 1) ? STR_HOUSE_BUILD_HOUSE_ZONE_ENABLED : STR_HOUSE_BUILD_HOUSE_ZONE_DISABLED);
						/* digit: 1(center)/2/3/4/5(edge) */
						SetDParam(2 * i + 1, i + 1);
					}
					break;
				}

				case WID_HP_HOUSE_LANDSCAPE: {
					StringID info = STR_HOUSE_BUILD_LANDSCAPE_ABOVE_OR_BELOW_SNOWLINE;
					switch (HouseSpec::Get(this->display_house)->building_availability & (HZ_SUBARTC_ABOVE | HZ_SUBARTC_BELOW)) {
						case HZ_SUBARTC_ABOVE: info = STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE; break;
						case HZ_SUBARTC_BELOW: info = STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE; break;
						default: break;
					}
					SetDParam(0, info);
					break;
				}

				case WID_HP_HOUSE_YEARS: {
					const HouseSpec *hs = HouseSpec::Get(this->display_house);
					SetDParam(0, hs->min_year <= _cur_year ? STR_HOUSE_BUILD_YEARS_GOOD_YEAR : STR_HOUSE_BUILD_YEARS_BAD_YEAR);
					SetDParam(1, hs->min_year);
					SetDParam(2, hs->max_year >= _cur_year ? STR_HOUSE_BUILD_YEARS_GOOD_YEAR : STR_HOUSE_BUILD_YEARS_BAD_YEAR);
					SetDParam(3, hs->max_year);
					break;
				}

				case WID_HP_HOUSE_ACCEPTANCE: {
					static char buff[DRAW_STRING_BUFFER] = "";
					char *str = buff;
					CargoArray cargo;
					CargoTypes dummy = 0;
					AddAcceptedHouseCargo(this->display_house, INVALID_TILE, cargo, &dummy);
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
					AddProducedHouseCargo(this->display_house, INVALID_TILE, cargo);
					uint32 cargo_mask = 0;
					for (uint i = 0; i < NUM_CARGO; i++) if (cargo[i] != 0) SetBit(cargo_mask, i);
					SetDParam(0, cargo_mask);
					break;
				}

				default: break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_HP_HOUSE_SETS: {
				uint max_w = 0;
				for (uint i = 0; i < this->house_list.NumHouseSets(); i++) {
					max_w = max(max_w, GetStringBoundingBox(this->house_list.GetNameOfHouseSet(i)).width);
				}
				size->width = max(size->width, max_w + padding.width);
				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = this->house_list.NumHouseSets() * this->line_height;
				break;
			}

			case WID_HP_HOUSE_NAME:
				size->width = 120; // we do not want this window to get too wide, better clip
				break;

			case WID_HP_HISTORICAL_BUILDING:
				size->width = max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HISTORICAL_BUILDING).width + padding.width);
				break;

			case WID_HP_HOUSE_POPULATION:
				SetDParam(0, 0);
				/* max popultion is 255 - 3 digits */
				size->width = max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HOUSE_POPULATION).width + 3 * GetDigitWidth() + padding.width);
				break;

			case WID_HP_HOUSE_ZONES: {
				for (int i = 0; i < HZB_END; i++) {
					SetDParam(2 * i, STR_HOUSE_BUILD_HOUSE_ZONE_ENABLED); // colour
					SetDParam(2 * i + 1, i + 1); // digit: 1(center)/2/3/4/5(edge)
				}
				size->width = max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HOUSE_ZONES).width + padding.width);
				break;
			}

			case WID_HP_HOUSE_LANDSCAPE: {
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ABOVE_OR_BELOW_SNOWLINE);
				Dimension dim = GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE);
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_ABOVE_SNOWLINE);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				SetDParam(0, STR_HOUSE_BUILD_LANDSCAPE_ONLY_BELOW_SNOWLINE);
				dim = maxdim(dim, GetStringBoundingBox(STR_HOUSE_BUILD_LANDSCAPE));
				dim.width += padding.width;
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}

			case WID_HP_HOUSE_YEARS: {
				SetDParam(0, STR_HOUSE_BUILD_YEARS_GOOD_YEAR);
				SetDParam(1, 0);
				SetDParam(2, STR_HOUSE_BUILD_YEARS_GOOD_YEAR);
				SetDParam(3, 0);
				Dimension dim = GetStringBoundingBox(STR_HOUSE_BUILD_YEARS);
				dim.width += 14 * GetDigitWidth() + padding.width; // space for about 16 digits (14 + two zeros) should be enough, don't make the window too wide
				dim.height += padding.height;
				*size = maxdim(*size, dim);
				break;
			}

			case WID_HP_HOUSE_SELECT_MATRIX:
				resize->height = 1; // don't snap to rows of this matrix
				break;

			/* these texts can be long, better clip */
			case WID_HP_HOUSE_ACCEPTANCE:
			case WID_HP_HOUSE_SUPPLY:
				size->width = 0;
				break;

			default: break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const override
	{
		switch (GB(widget, 0, 16)) {
			case WID_HP_HOUSE_SETS: {
				int y = r.top + WD_MATRIX_TOP;
				for (uint i = 0; i < this->house_list.NumHouseSets(); i++) {
					SetDParamStr(0, this->house_list.GetNameOfHouseSet(i));
					DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_RIGHT, y, STR_JUST_RAW_STRING, i == this->house_set ? TC_WHITE : TC_BLACK);
					y += this->line_height;
				}
				break;
			}

			case WID_HP_HOUSE_PREVIEW:
				if (this->display_house != INVALID_HOUSE_ID) {
					DrawHouseImage(this->display_house, r.left, r.top, r.right, r.bottom);
				}
				break;

			case WID_HP_HOUSE_SELECT: {
				HouseID house = this->house_list.GetHouseAtOffset(this->house_set, GB(widget, 16, 16));
				int lowered = (house == _cur_house) ? 1 : 0;
				DrawHouseImage(house,
						r.left  + WD_MATRIX_LEFT  + lowered, r.top    + WD_MATRIX_TOP    + lowered,
						r.right - WD_MATRIX_RIGHT + lowered, r.bottom - WD_MATRIX_BOTTOM + lowered);
				const HouseSpec *hs = HouseSpec::Get(house);
				/* disabled? */
				if (_cur_year < hs->min_year || _cur_year > hs->max_year) {
					GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				}
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count) override
	{
		switch (GB(widget, 0, 16)) {
			case WID_HP_HOUSE_SETS: {
				uint index = (uint)(pt.y - this->GetWidget<NWidgetBase>(widget)->pos_y) / this->line_height;
				if (index < this->house_list.NumHouseSets() && index != this->house_set) this->SelectOtherHouse(index, 0);
				break;
			}

			case WID_HP_HOUSE_SELECT:
				this->SelectOtherHouse(this->house_set, GB(widget, 16, 16));
				break;
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile) OVERRIDE
	{
		PlaceProc_House(tile);
	}

	virtual void OnPlaceObjectAbort() OVERRIDE
	{
		this->house_offset = -1;
		_cur_house = INVALID_HOUSE_ID;
		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_HP_HOUSE_SELECT_MATRIX);
		matrix->SetClicked(-1);
		this->UpdateSelectSize();
		this->SetDirty();
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
					/* HOUSE PICTURE AND LABEL */
					NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_HP_HOUSE_PREVIEW), SetFill(1, 1), SetResize(0, 1), SetMinimalSize(2 * TILE_PIXELS, 142),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_HP_HOUSE_NAME), SetDataTip(STR_HOUSE_BUILD_HOUSE_NAME, STR_NULL), SetMinimalSize(120, 0),
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
				NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_HP_HOUSE_SELECT_MATRIX), SetPIP(0, 2, 0), SetPadding(2, 2, 2, 2), SetScrollbar(WID_HP_HOUSE_SELECT_SCROLL),
					NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_HP_HOUSE_SELECT), SetMinimalSize(64, 64), SetFill(0, 0), SetResize(0, 0),
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
 * @param parent The toolbar window we're associated with.
 */
void ShowBuildHousePicker()
{
	AllocateWindowDescFront<HousePickerWindow>(&_house_picker_desc, 0);
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
		this->vscroll->SetCount(this->towns.size());
		this->FinishInitNested();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget != WID_ST_PANEL) return;

		/* Determine the widest string */
		Dimension d = { 0, 0 };
		for (uint i = 0; i < this->towns.size(); i++) {
			SetDParam(0, this->towns[i]);
			d = maxdim(d, GetStringBoundingBox(STR_SELECT_TOWN_LIST_ITEM));
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
			SetDParam(0, this->towns[i]);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_SELECT_TOWN_LIST_ITEM);
			y += this->resize.step_height;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget != WID_ST_PANEL) return;

		uint pos = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ST_PANEL, WD_FRAMERECT_TOP);
		if (pos >= this->towns.size()) return;

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

static void PlaceProc_House(TileIndex tile)
{
	if (_town_pool.items == 0) {
		ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HOUSE_HERE, STR_ERROR_MUST_FOUND_TOWN_FIRST, WL_INFO);
		return;
	}

	DeleteWindowById(WC_SELECT_TOWN, 0);

	if (_cur_house == INVALID_HOUSE_ID) return;

	/* build a list of towns to join to */
	TownList towns;
	HouseZones house_zones = HouseSpec::Get(_cur_house)->building_availability & HZ_ZONALL;
	uint best_dist = UINT_MAX;
	int best_zone = (int)HZB_BEGIN - 1;
	for (const Town *t : Town::Iterate()) {
		HouseZonesBits town_zone = TryGetTownRadiusGroup(t, tile);
		if (HasBit(house_zones, town_zone)) {
			/* If CTRL is NOT pressed keep only single town on the list, the best one.
			 * Otherwise add all towns to the list so they can be shown to the player. */
			if (!_ctrl_pressed) {
				if ((int)town_zone < best_zone) continue;
				uint dist = DistanceSquare(tile, t->xy);
				if (dist >= best_dist) continue;
				best_dist = dist;
				best_zone = town_zone;
				towns.clear();
			}
			towns.push_back(t->index);
		}
	}

	if (towns.size() == 0) {
		ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HOUSE_HERE, STR_ERROR_BUILDING_NOT_ALLOWED_IN_THIS_TOWN_ZONE, WL_INFO);
		return;
	}

	CommandContainer cmd = {
		tile,
		_cur_house, // p1 - house type and town index (town not yet set)
		InteractiveRandom(), // p2 - random bits for the house
		CMD_BUILD_HOUSE | CMD_MSG(STR_ERROR_CAN_T_BUILD_HOUSE_HERE),
		CcPlaySound_SPLAT_RAIL,
		0,
		""
	};

	if (!_ctrl_pressed) {
		SB(cmd.p1, 16, 16, towns[0]); // set the town, it's alone on the list
		DoCommandP(&cmd);
	} else {
		if (!_settings_client.gui.persistent_buildingtools) DeleteWindowById(WC_BUILD_HOUSE, 0);
		ShowSelectTownWindow(towns, cmd);
	}
}

void InitializeTownGui()
{
	_town_local_authority_kdtree.Clear();
}
