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
#include "core/backup_type.hpp"
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
#include "zoom_func.h"

TownKdtree _town_local_authority_kdtree(&Kdtree_TownXYFunc);

typedef GUIList<const Town*> GUITownList;

static void PlaceProc_House(TileIndex tile);

static const NWidgetPart _nested_town_authority_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TA_CAPTION), SetDataTip(STR_LOCAL_AUTHORITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TA_ZONE_BUTTON), SetMinimalSize(50, 0), SetMinimalTextLines(1, WidgetDimensions::unscaled.framerect.Vertical() + 2), SetDataTip(STR_LOCAL_AUTHORITY_ZONE, STR_LOCAL_AUTHORITY_ZONE_TOOLTIP),
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
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_TA_BTN_SEL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TA_EXECUTE),  SetMinimalSize(317, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_LOCAL_AUTHORITY_DO_IT_BUTTON, STR_LOCAL_AUTHORITY_DO_IT_TOOLTIP),
			NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_TA_SETTING),  SetMinimalSize(317, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING1, STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_TOOLTIP),
		EndContainer(),
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

	Dimension icon_size;      ///< Dimensions of company icon
	Dimension exclusive_size; ///< Dimensions of exlusive icon

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
			for (uint i : SetBitIterator(bits)) {
				n--;
				if (n < 0) return i;
			}
		}
		return -1;
	}

	static bool ChangeSettingsDisabled()
	{
		return _networking && !(_network_server || _network_settings_access) &&
				!(_local_company != COMPANY_SPECTATOR && _settings_game.difficulty.override_town_settings_in_multiplayer);
	}

	static const uint SETTING_OVERRIDE_COUNT = 6;

public:
	TownAuthorityWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc), sel_index(-1), displayed_actions_on_previous_painting(0)
	{
		this->town = Town::Get(window_number);
		this->InitNested(window_number);
		this->vscroll = this->GetScrollbar(WID_TA_SCROLLBAR);
		this->vscroll->SetCapacity((this->GetWidget<NWidgetBase>(WID_TA_COMMAND_LIST)->current_y - WidgetDimensions::scaled.framerect.Vertical()) / FONT_HEIGHT_NORMAL);
	}

	void OnInit() override
	{
		this->icon_size      = GetSpriteSize(SPR_COMPANY_ICON);
		this->exclusive_size = GetSpriteSize(SPR_EXCLUSIVE_TRANSPORT);
	}

	void OnPaint() override
	{
		int numact;
		uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
		numact += SETTING_OVERRIDE_COUNT;
		if (buttons != displayed_actions_on_previous_painting) this->SetDirty();
		displayed_actions_on_previous_painting = buttons;

		this->vscroll->SetCount(numact + 1);

		if (this->sel_index != -1 && this->sel_index < 0x100 && !HasBit(buttons, this->sel_index)) {
			this->sel_index = -1;
		}

		this->SetWidgetLoweredState(WID_TA_ZONE_BUTTON, this->town->show_zone);
		this->SetWidgetDisabledState(WID_TA_EXECUTE, this->sel_index == -1 || this->sel_index >= 0x100);
		this->SetWidgetDisabledState(WID_TA_SETTING, ChangeSettingsDisabled());
		this->GetWidget<NWidgetStacked>(WID_TA_BTN_SEL)->SetDisplayedPlane(this->sel_index >= 0x100 ? 1 : 0);

		this->DrawWidgets();
		if (!this->IsShaded()) this->DrawRatings();
	}

	/** Draw the contents of the ratings panel. May request a resize of the window if the contents does not fit. */
	void DrawRatings()
	{
		Rect r = this->GetWidget<NWidgetBase>(WID_TA_RATING_INFO)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);

		int text_y_offset      = (this->resize.step_height - FONT_HEIGHT_NORMAL) / 2;
		int icon_y_offset      = (this->resize.step_height - this->icon_size.height) / 2;
		int exclusive_y_offset = (this->resize.step_height - this->exclusive_size.height) / 2;

		DrawString(r.left, r.right, r.top + text_y_offset, STR_LOCAL_AUTHORITY_COMPANY_RATINGS);
		r.top += this->resize.step_height;

		bool rtl = _current_text_dir == TD_RTL;
		Rect icon      = r.WithWidth(this->icon_size.width, rtl);
		Rect exclusive = r.Indent(this->icon_size.width + WidgetDimensions::scaled.hsep_normal, rtl).WithWidth(this->exclusive_size.width, rtl);
		Rect text      = r.Indent(this->icon_size.width + WidgetDimensions::scaled.hsep_normal + this->exclusive_size.width + WidgetDimensions::scaled.hsep_normal, rtl);

		/* Draw list of companies */
		for (const Company *c : Company::Iterate()) {
			if ((HasBit(this->town->have_ratings, c->index) || this->town->exclusivity == c->index)) {
				DrawCompanyIcon(c->index, icon.left, text.top + icon_y_offset);

				SetDParam(0, c->index);
				SetDParam(1, c->index);

				int rating = this->town->ratings[c->index];
				StringID str = STR_CARGO_RATING_APPALLING;
				if (rating > RATING_APPALLING) str++;
				if (rating > RATING_VERYPOOR)  str++;
				if (rating > RATING_POOR)      str++;
				if (rating > RATING_MEDIOCRE)  str++;
				if (rating > RATING_GOOD)      str++;
				if (rating > RATING_VERYGOOD)  str++;
				if (rating > RATING_EXCELLENT) str++;

				SetDParam(2, str);
				if (this->town->exclusivity == c->index) {
					DrawSprite(SPR_EXCLUSIVE_TRANSPORT, COMPANY_SPRITE_COLOUR(c->index), exclusive.left, text.top + exclusive_y_offset);
				}

				DrawString(text.left, text.right, text.top + text_y_offset, STR_LOCAL_AUTHORITY_COMPANY_RATING);
				text.top += this->resize.step_height;
			}
		}

		text.bottom = text.top - 1;
		if (text.bottom > r.bottom) {
			/* If the company list is too big to fit, mark ourself dirty and draw again. */
			ResizeWindow(this, 0, text.bottom - r.bottom, false);
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_TA_CAPTION) {
			SetDParam(0, this->window_number);
		} else if (widget == WID_TA_SETTING) {
			SetDParam(0, STR_EMPTY);
			if (this->sel_index >= 0x100 && this->sel_index < (int)(0x100 + SETTING_OVERRIDE_COUNT)) {
				if (!HasBit(this->town->override_flags, this->sel_index - 0x100)) {
					SetDParam(0, STR_COLOUR_DEFAULT);
				} else {
					int idx = this->sel_index - 0x100;
					switch (idx) {
						case TSOF_OVERRIDE_BUILD_ROADS:
						case TSOF_OVERRIDE_BUILD_LEVEL_CROSSINGS:
						case TSOF_OVERRIDE_BUILD_BRIDGES:
							SetDParam(0, HasBit(this->town->override_values, idx) ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
							break;
						case TSOF_OVERRIDE_BUILD_TUNNELS:
							SetDParam(0, STR_CONFIG_SETTING_TOWN_TUNNELS_FORBIDDEN + this->town->build_tunnels);
							break;
						case TSOF_OVERRIDE_BUILD_INCLINED_ROADS:
							SetDParam(0, STR_CONFIG_SETTING_TOWN_MAX_ROAD_SLOPE_VALUE + ((this->town->max_road_slope == 0) ? 1 : 0));
							SetDParam(1, this->town->max_road_slope);
							break;
						case TSOF_OVERRIDE_GROWTH:
							SetDParam(0, STR_CONFIG_SETTING_TOWN_GROWTH_NONE);
							break;
					}
				}
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_TA_ACTION_INFO:
				if (this->sel_index != -1) {
					TextColour colour = TC_FROMSTRING;
					StringID text = STR_NULL;
					if (this->sel_index >= 0x100) {
						SetDParam(1, STR_EMPTY);
						switch (this->sel_index - 0x100) {
							case TSOF_OVERRIDE_BUILD_ROADS:
								SetDParam(1, STR_CONFIG_SETTING_ALLOW_TOWN_ROADS_HELPTEXT);
								break;
							case TSOF_OVERRIDE_BUILD_LEVEL_CROSSINGS:
								SetDParam(1, STR_CONFIG_SETTING_ALLOW_TOWN_LEVEL_CROSSINGS_HELPTEXT);
								break;
							case TSOF_OVERRIDE_BUILD_TUNNELS:
								SetDParam(1, STR_CONFIG_SETTING_TOWN_TUNNELS_HELPTEXT);
								break;
							case TSOF_OVERRIDE_BUILD_INCLINED_ROADS:
								SetDParam(1, STR_CONFIG_SETTING_TOWN_MAX_ROAD_SLOPE_HELPTEXT);
								break;
							case TSOF_OVERRIDE_GROWTH:
								SetDParam(1, STR_CONFIG_SETTING_TOWN_GROWTH_HELPTEXT);
								break;
							case TSOF_OVERRIDE_BUILD_BRIDGES:
								SetDParam(1, STR_CONFIG_SETTING_ALLOW_TOWN_BRIDGES_HELPTEXT);
								break;
						}
						text = STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_TEXT;
						SetDParam(0, STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_ALLOW_ROADS + this->sel_index - 0x100);
					} else {
						colour = TC_YELLOW;
						text = STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + this->sel_index;
						SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[this->sel_index] >> 8);
					}
					DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.framerect), text, colour);
				}
				break;
			case WID_TA_COMMAND_LIST: {
				int numact;
				uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
				numact += SETTING_OVERRIDE_COUNT;
				Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
				int y = ir.top;
				int pos = this->vscroll->GetPosition();

				if (--pos < 0) {
					DrawString(ir.left, ir.right, y, STR_LOCAL_AUTHORITY_ACTIONS_TITLE);
					y += FONT_HEIGHT_NORMAL;
				}

				for (int i = 0; buttons; i++, buttons >>= 1) {
					if ((buttons & 1) && --pos < 0) {
						DrawString(ir.left, ir.right, y,
								STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i, this->sel_index == i ? TC_WHITE : TC_ORANGE);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				for (int i = 0; i < (int)SETTING_OVERRIDE_COUNT; i++) {
					if (--pos < 0) {
						const bool disabled = ChangeSettingsDisabled();
						const bool selected = (this->sel_index == (0x100 + i));
						const TextColour tc = disabled ? (TC_NO_SHADE | (selected ? TC_SILVER : TC_GREY)) : (selected ? TC_WHITE : TC_ORANGE);
						const bool overriden = HasBit(this->town->override_flags, i);
						SetDParam(0, STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_ALLOW_ROADS + i);
						SetDParam(1, overriden ? STR_JUST_STRING1 : STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_DEFAULT);
						switch (i) {
							case TSOF_OVERRIDE_BUILD_ROADS:
								SetDParam(2, this->town->GetAllowBuildRoads() ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
								break;

							case TSOF_OVERRIDE_BUILD_LEVEL_CROSSINGS:
								SetDParam(2, this->town->GetAllowBuildLevelCrossings() ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
								break;

							case TSOF_OVERRIDE_BUILD_TUNNELS: {
								TownTunnelMode tunnel_mode = this->town->GetBuildTunnelMode();
								SetDParam(2, STR_CONFIG_SETTING_TOWN_TUNNELS_FORBIDDEN + tunnel_mode);
								break;
							}

							case TSOF_OVERRIDE_BUILD_INCLINED_ROADS: {
								uint8 max_slope = this->town->GetBuildMaxRoadSlope();
								SetDParam(2, STR_CONFIG_SETTING_TOWN_MAX_ROAD_SLOPE_VALUE + ((max_slope == 0) ? 1 : 0));
								SetDParam(3, max_slope);
								break;
							}

							case TSOF_OVERRIDE_GROWTH:
								SetDParam(1, overriden ? STR_CONFIG_SETTING_TOWN_GROWTH_NONE : STR_COLOUR_DEFAULT);
								break;

							case TSOF_OVERRIDE_BUILD_BRIDGES:
								SetDParam(2, this->town->GetAllowBuildBridges() ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
								break;
						}
						DrawString(ir.left, ir.right, y,
								STR_LOCAL_AUTHORITY_SETTING_OVERRIDE_STR, tc);
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
				Dimension d = {0, 0};
				for (int i = 0; i < TACT_COUNT; i++) {
					SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[i] >> 8);
					d = maxdim(d, GetStringMultiLineBoundingBox(STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + i, *size));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_TA_COMMAND_LIST:
				size->height = (5 + SETTING_OVERRIDE_COUNT) * FONT_HEIGHT_NORMAL + padding.height;
				size->width = GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTIONS_TITLE).width;
				for (uint i = 0; i < TACT_COUNT; i++ ) {
					size->width = std::max(size->width, GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i).width + padding.width);
				}
				size->width += padding.width;
				break;

			case WID_TA_RATING_INFO:
				resize->height = std::max({this->icon_size.height + WidgetDimensions::scaled.vsep_normal, this->exclusive_size.height + WidgetDimensions::scaled.vsep_normal, (uint)FONT_HEIGHT_NORMAL});
				size->height = 9 * resize->height + padding.height;
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
				this->SetWidgetDirty(widget);
				MarkWholeNonMapViewportsDirty();
				break;
			}

			case WID_TA_COMMAND_LIST: {
				int y = this->GetRowFromWidget(pt.y, WID_TA_COMMAND_LIST, 1, FONT_HEIGHT_NORMAL);
				if (!IsInsideMM(y, 0, 5 + SETTING_OVERRIDE_COUNT)) return;

				const uint setting_override_offset = 32 - SETTING_OVERRIDE_COUNT;

				y = GetNthSetBit(GetMaskOfTownActions(nullptr, _local_company, this->town) | (UINT32_MAX << setting_override_offset), y + this->vscroll->GetPosition() - 1);
				if (y >= (int)setting_override_offset) {
					this->sel_index = y + 0x100 - setting_override_offset;
					this->SetDirty();
					break;
				} else if (y >= 0) {
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

			case WID_TA_SETTING: {
				uint8 idx = this->sel_index - 0x100;
				switch (idx) {
					case TSOF_OVERRIDE_BUILD_ROADS:
					case TSOF_OVERRIDE_BUILD_LEVEL_CROSSINGS:
					case TSOF_OVERRIDE_BUILD_BRIDGES: {
						int value = HasBit(this->town->override_flags, idx) ? (HasBit(this->town->override_values, idx) ? 2 : 1) : 0;
						const StringID names[] = {
							STR_COLOUR_DEFAULT,
							STR_CONFIG_SETTING_OFF,
							STR_CONFIG_SETTING_ON,
							INVALID_STRING_ID
						};
						ShowDropDownMenu(this, names, value, WID_TA_SETTING, 0, 0);
						break;
					}
					case TSOF_OVERRIDE_BUILD_TUNNELS: {
						const StringID names[] = {
							STR_COLOUR_DEFAULT,
							STR_CONFIG_SETTING_TOWN_TUNNELS_FORBIDDEN,
							STR_CONFIG_SETTING_TOWN_TUNNELS_ALLOWED_OBSTRUCTION,
							STR_CONFIG_SETTING_TOWN_TUNNELS_ALLOWED,
							INVALID_STRING_ID
						};
						ShowDropDownMenu(this, names, HasBit(this->town->override_flags, idx) ? this->town->build_tunnels + 1 : 0, WID_TA_SETTING, 0, 0);
						break;
					}
					case TSOF_OVERRIDE_BUILD_INCLINED_ROADS: {
						DropDownList dlist;
						dlist.emplace_back(new DropDownListStringItem(STR_COLOUR_DEFAULT, 0, false));
						dlist.emplace_back(new DropDownListStringItem(STR_CONFIG_SETTING_TOWN_MAX_ROAD_SLOPE_ZERO, 1, false));
						for (int i = 1; i <= 8; i++) {
							DropDownListParamStringItem *item = new DropDownListParamStringItem(STR_CONFIG_SETTING_TOWN_MAX_ROAD_SLOPE_VALUE, i + 1, false);
							item->SetParam(0, i);
							dlist.emplace_back(item);
						}
						ShowDropDownList(this, std::move(dlist), HasBit(this->town->override_flags, idx) ? this->town->max_road_slope + 1 : 0, WID_TA_SETTING);
						break;
					}
					case TSOF_OVERRIDE_GROWTH: {
						int value = HasBit(this->town->override_flags, idx) ? 1 : 0;
						const StringID names[] = {
							STR_COLOUR_DEFAULT,
							STR_CONFIG_SETTING_TOWN_GROWTH_NONE,
							INVALID_STRING_ID
						};
						ShowDropDownMenu(this, names, value, WID_TA_SETTING, 0, 0);
						break;
					}
				}
				break;
			}
		}
	}


	virtual void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_TA_SETTING: {
				if (index < 0) break;
				uint32 p2 = this->sel_index - 0x100;
				if (index > 0) {
					SetBit(p2, 16);
					p2 |= (index - 1) << 8;
				}
				Commands cmd = (_networking && !(_network_server || _network_settings_access)) ? CMD_TOWN_SETTING_OVERRIDE_NON_ADMIN : CMD_TOWN_SETTING_OVERRIDE;
				DoCommandP(this->town->xy, this->window_number, p2, cmd | CMD_MSG(STR_ERROR_CAN_T_DO_THIS));
				break;
			}

			default: NOT_REACHED();
		}

		this->SetDirty();
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
		nvp->InitializeViewport(this, this->town->xy, ScaleZoomGUI(ZOOM_LVL_TOWN));
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
		this->SetWidgetDisabledState(WID_TV_CHANGE_NAME, _networking && !(_network_server || _network_settings_access) &&
				!(_local_company != COMPANY_SPECTATOR && _settings_game.difficulty.rename_towns_in_multiplayer));

		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_TV_INFO) return;

		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

		SetDParam(0, this->town->cache.population);
		SetDParam(1, this->town->cache.num_houses);
		DrawString(tr, STR_TOWN_VIEW_POPULATION_HOUSES);
		tr.top += FONT_HEIGHT_NORMAL;

		SetDParam(0, 1 << CT_PASSENGERS);
		SetDParam(1, this->town->supplied[CT_PASSENGERS].old_act);
		SetDParam(2, this->town->supplied[CT_PASSENGERS].old_max);
		DrawString(tr, STR_TOWN_VIEW_CARGO_LAST_MONTH_MAX);
		tr.top += FONT_HEIGHT_NORMAL;

		SetDParam(0, 1 << CT_MAIL);
		SetDParam(1, this->town->supplied[CT_MAIL].old_act);
		SetDParam(2, this->town->supplied[CT_MAIL].old_max);
		DrawString(tr, STR_TOWN_VIEW_CARGO_LAST_MONTH_MAX);
		tr.top += FONT_HEIGHT_NORMAL;

		bool first = true;
		for (int i = TE_BEGIN; i < TE_END; i++) {
			if (this->town->goal[i] == 0) continue;
			if (this->town->goal[i] == TOWN_GROWTH_WINTER && (TileHeight(this->town->xy) < LowestSnowLine() || this->town->cache.population <= 90)) continue;
			if (this->town->goal[i] == TOWN_GROWTH_DESERT && (GetTropicZone(this->town->xy) != TROPICZONE_DESERT || this->town->cache.population <= 60)) continue;

			if (first) {
				DrawString(tr, STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH);
				tr.top += FONT_HEIGHT_NORMAL;
				first = false;
			}

			bool rtl = _current_text_dir == TD_RTL;

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
			DrawString(tr.Indent(20, rtl), string);
			tr.top += FONT_HEIGHT_NORMAL;
		}

		if (HasBit(this->town->flags, TOWN_IS_GROWING)) {
			SetDParam(0, RoundDivSU(this->town->growth_rate + 1, DAY_TICKS));
			DrawString(tr, this->town->fund_buildings_months == 0 ? STR_TOWN_VIEW_TOWN_GROWS_EVERY : STR_TOWN_VIEW_TOWN_GROWS_EVERY_FUNDED);
			tr.top += FONT_HEIGHT_NORMAL;
		} else {
			DrawString(tr, STR_TOWN_VIEW_TOWN_GROW_STOPPED);
			tr.top += FONT_HEIGHT_NORMAL;
		}

		/* only show the town noise, if the noise option is activated. */
		if (_settings_game.economy.station_noise_level) {
			uint16 max_noise = this->town->MaxTownNoise();
			SetDParam(0, this->town->noise_reached);
			SetDParam(1, max_noise);
			DrawString(tr, max_noise == UINT16_MAX ? STR_TOWN_VIEW_NOISE_IN_TOWN_NO_LIMIT : STR_TOWN_VIEW_NOISE_IN_TOWN);
			tr.top += FONT_HEIGHT_NORMAL;
		}

		if (!this->town->text.empty()) {
			SetDParamStr(0, this->town->text);
			tr.top = DrawStringMultiLine(tr, STR_JUST_RAW_STRING, TC_BLACK);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_TV_CENTER_VIEW: // scroll to location
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(this->town->xy);
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

				if (!Town::Get(this->window_number)->GetAllowBuildRoads() && !_warn_town_no_roads) {
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
				size->height = GetDesiredInfoHeight(size->width) + padding.height;
				break;
		}
	}

	/**
	 * Gets the desired height for the information panel.
	 * @return the desired height in pixels.
	 */
	uint GetDesiredInfoHeight(int width) const
	{
		uint aimed_height = 3 * FONT_HEIGHT_NORMAL;

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
			SetDParamStr(0, this->town->text);
			aimed_height += GetStringHeight(STR_JUST_RAW_STRING, width - WidgetDimensions::scaled.framerect.Horizontal());
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

		DoCommandP(0, this->window_number, 0, ((_networking && !(_network_server || _network_settings_access)) ? CMD_RENAME_TOWN_NON_ADMIN : CMD_RENAME_TOWN) | CMD_MSG(STR_ERROR_CAN_T_RENAME_TOWN), nullptr, str);
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
		NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(12, 14), SetDataTip(SPR_GOTO_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_DEBUGBOX, COLOUR_BROWN),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0), SetResize(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_SHOW_AUTHORITY), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_LOCAL_AUTHORITY_BUTTON, STR_TOWN_VIEW_LOCAL_AUTHORITY_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TV_CATCHMENT), SetMinimalSize(40, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_CATCHMENT, STR_TOOLTIP_CATCHMENT),
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
		NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(12, 14), SetDataTip(SPR_GOTO_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_DEBUGBOX, COLOUR_BROWN),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 1), SetResize(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_EXPAND), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_EXPAND_BUTTON, STR_TOWN_VIEW_EXPAND_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_DELETE), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_DELETE_BUTTON, STR_TOWN_VIEW_DELETE_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_TV_CATCHMENT), SetMinimalSize(40, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_CATCHMENT, STR_TOOLTIP_CATCHMENT),
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
				NWidget(WWT_EDITBOX, COLOUR_BROWN, WID_TD_FILTER), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_TD_LIST), SetDataTip(0x0, STR_TOWN_DIRECTORY_LIST_TOOLTIP),
							SetFill(1, 0), SetResize(1, 1), SetScrollbar(WID_TD_SCROLLBAR), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_TD_WORLD_POPULATION), SetPadding(2, 0, 2, 2), SetMinimalTextLines(1, 0), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_TOWN_DIRECTORY_INFO, STR_NULL),
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
			this->vscroll->SetCount(this->towns.size()); // Update scrollbar as well.
		}
		/* Always sort the towns. */
		this->towns.Sort();
		this->SetWidgetDirty(WID_TD_LIST); // Force repaint of the displayed towns.
	}

	/** Sort by town name */
	static bool TownNameSorter(const Town * const &a, const Town * const &b)
	{
		return StrNaturalCompare(a->GetCachedName(), b->GetCachedName()) < 0; // Sort by name (natural sorting).
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
		return TownDirectoryWindow::TownNameSorter(b, a);
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
				SetDParam(0, STR_TOWN_POPULATION);
				SetDParam(1, GetWorldPopulation());
				SetDParam(2, Town::GetNumItems());
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
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				if (this->towns.size() == 0) { // No towns available.
					DrawString(tr, STR_TOWN_DIRECTORY_NONE);
					break;
				}

				/* At least one town available. */
				bool rtl = _current_text_dir == TD_RTL;
				Dimension icon_size = GetSpriteSize(SPR_TOWN_RATING_GOOD);
				int icon_x = tr.WithWidth(icon_size.width, rtl).left;
				tr = tr.Indent(icon_size.width + WidgetDimensions::scaled.hsep_normal, rtl);

				for (uint i = this->vscroll->GetPosition(); i < this->towns.size(); i++) {
					const Town *t = this->towns[i];
					assert(t->xy != INVALID_TILE);

					/* Draw rating icon. */
					if (_game_mode == GM_EDITOR || !HasBit(t->have_ratings, _local_company)) {
						DrawSprite(SPR_TOWN_RATING_NA, PAL_NONE, icon_x, tr.top + (this->resize.step_height - icon_size.height) / 2);
					} else {
						SpriteID icon = SPR_TOWN_RATING_APALLING;
						if (t->ratings[_local_company] > RATING_VERYPOOR) icon = SPR_TOWN_RATING_MEDIOCRE;
						if (t->ratings[_local_company] > RATING_GOOD)     icon = SPR_TOWN_RATING_GOOD;
						DrawSprite(icon, PAL_NONE, icon_x, tr.top + (this->resize.step_height - icon_size.height) / 2);
					}

					SetDParam(0, t->index);
					SetDParam(1, t->cache.population);
					DrawString(tr.left, tr.right, tr.top + (this->resize.step_height - FONT_HEIGHT_NORMAL) / 2, GetTownString(t));

					tr.top += this->resize.step_height;
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
				d.height = std::max(d.height, icon_size.height);
				resize->height = d.height;
				d.height *= 5;
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_WORLD_POPULATION: {
				SetDParam(0, STR_TOWN_POPULATION);
				SetDParamMaxDigits(1, 10);
				SetDParamMaxDigits(2, 5);
				Dimension d = GetStringBoundingBox(STR_TOWN_DIRECTORY_INFO);
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
				auto it = this->vscroll->GetScrolledItemFromWidget(this->towns, pt.y, this, WID_TD_LIST, WidgetDimensions::scaled.framerect.top);
				if (it == this->towns.end()) return; // click out of town bounds

				const Town *t = *it;
				assert(t != nullptr);
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(t->xy);
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

void CcFoundTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

void CcFoundRandomTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
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
										SetDataTip(STR_FOUND_TOWN_MANY_RANDOM_TOWNS, STR_FOUND_TOWN_RANDOM_TOWNS_TOOLTIP), SetPadding(0, 2, 1, 2),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_TF_EXPAND_ALL_TOWNS), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_EXPAND_ALL_TOWNS, STR_FOUND_TOWN_EXPAND_ALL_TOWNS_TOOLTIP), SetPadding(0, 2, 0, 2),
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
		this->townnamevalid = GenerateTownName(_interactive_random, &this->townnameparts);

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
			this->SetWidgetsDisabledState(true, WID_TF_RANDOM_TOWN, WID_TF_MANY_RANDOM_TOWNS, WID_TF_EXPAND_ALL_TOWNS, WID_TF_SIZE_LARGE, WIDGET_LIST_END);
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

			case WID_TF_MANY_RANDOM_TOWNS: {
				Backup<bool> old_generating_world(_generating_world, true, FILE_LINE);
				UpdateNearestTownForRoadTiles(true);
				if (!GenerateTowns(this->town_layout)) {
					ShowErrorMessage(STR_ERROR_CAN_T_GENERATE_TOWN, STR_ERROR_NO_SPACE_FOR_TOWN, WL_INFO);
				}
				UpdateNearestTownForRoadTiles(false);
				old_generating_world.Restore();
				break;
			}

			case WID_TF_EXPAND_ALL_TOWNS:
				for (Town *t : Town::Iterate()) {
					DoCommand(0, t->index, 0, DC_EXEC, CMD_EXPAND_TOWN);
				}
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
				static_assert(sizeof(a_set->grfid) <= sizeof(int));
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
		return (uint)this->house_sets.size() - 1; // last item is a terminator
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
		this->house_sets.push_back((uint16)this->size());
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

	virtual void OnInit() override
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
						SetDParam(2 * i + 1, 4 - i);
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
						/* digit: 4(center)/3/1/1/0(edge) */
						SetDParam(2 * i + 1, 4 - i);
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
					CargoArray cargo{};
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
					CargoArray cargo{};
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
					max_w = std::max(max_w, GetStringBoundingBox(this->house_list.GetNameOfHouseSet(i)).width);
				}
				size->width = std::max(size->width, max_w + padding.width);
				this->line_height = FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.matrix.Vertical();
				size->height = this->house_list.NumHouseSets() * this->line_height;
				break;
			}

			case WID_HP_HOUSE_NAME:
				size->width = 120; // we do not want this window to get too wide, better clip
				break;

			case WID_HP_HISTORICAL_BUILDING:
				size->width = std::max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HISTORICAL_BUILDING).width + padding.width);
				break;

			case WID_HP_HOUSE_POPULATION:
				SetDParam(0, 0);
				/* max popultion is 255 - 3 digits */
				size->width = std::max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HOUSE_POPULATION).width + 3 * GetDigitWidth() + padding.width);
				break;

			case WID_HP_HOUSE_ZONES: {
				for (int i = 0; i < HZB_END; i++) {
					SetDParam(2 * i, STR_HOUSE_BUILD_HOUSE_ZONE_ENABLED); // colour
					SetDParam(2 * i + 1, i + 1); // digit: 1(center)/2/3/4/5(edge)
				}
				size->width = std::max(size->width, GetStringBoundingBox(STR_HOUSE_BUILD_HOUSE_ZONES).width + padding.width);
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
				int y = r.top + WidgetDimensions::scaled.matrix.top;
				for (uint i = 0; i < this->house_list.NumHouseSets(); i++) {
					SetDParamStr(0, this->house_list.GetNameOfHouseSet(i));
					DrawString(r.left + WidgetDimensions::scaled.matrix.left, r.right - WidgetDimensions::scaled.matrix.right, y, STR_JUST_RAW_STRING, i == this->house_set ? TC_WHITE : TC_BLACK);
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
						r.left  + WidgetDimensions::scaled.matrix.left  + lowered, r.top    + WidgetDimensions::scaled.matrix.top    + lowered,
						r.right - WidgetDimensions::scaled.matrix.right + lowered, r.bottom - WidgetDimensions::scaled.matrix.bottom + lowered);
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

	virtual void OnPlaceObject(Point pt, TileIndex tile) override
	{
		PlaceProc_House(tile);
	}

	virtual void OnPlaceObjectAbort() override
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
		this->vscroll->SetCount((uint)this->towns.size());
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
		d.width += WidgetDimensions::scaled.framerect.Horizontal();
		d.height += WidgetDimensions::scaled.framerect.Vertical();
		*size = d;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_ST_PANEL) return;

		Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		uint y = ir.top;
		uint end = std::min<uint>(this->vscroll->GetCount(), this->vscroll->GetPosition() + this->vscroll->GetCapacity());
		for (uint i = this->vscroll->GetPosition(); i < end; i++) {
			SetDParam(0, this->towns[i]);
			DrawString(ir.left, ir.right, y, STR_SELECT_TOWN_LIST_ITEM);
			y += this->resize.step_height;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget != WID_ST_PANEL) return;

		uint pos = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ST_PANEL, WidgetDimensions::scaled.framerect.top);
		if (pos >= this->towns.size()) return;

		/* Place a house */
		SB(this->cmd.p1, 16, 16, this->towns[pos]);
		DoCommandP(&this->cmd);

		/* Close the window */
		delete this;
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_ST_PANEL, WidgetDimensions::scaled.framerect.Vertical());
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
		if (HasBit(house_zones, town_zone) || (_settings_client.scenario.house_ignore_zones == 1 && town_zone != HZB_END) || _settings_client.scenario.house_ignore_zones == 2) {
			/* If CTRL is NOT pressed keep only single town on the list, the best one.
			 * Otherwise add all towns to the list so they can be shown to the player. */
			if (!_ctrl_pressed) {
				if ((int)town_zone < best_zone) continue;
				uint dist = DistanceSquare(tile, t->xy);
				if (dist >= best_dist) continue;
				best_dist = dist;
				if (town_zone != HZB_END) best_zone = town_zone;
				towns.clear();
			}
			towns.push_back(t->index);
		}
	}

	if (towns.size() == 0) {
		ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HOUSE_HERE, STR_ERROR_BUILDING_NOT_ALLOWED_IN_THIS_TOWN_ZONE, WL_INFO);
		return;
	}

	if (towns.size() > 16 && _settings_client.scenario.house_ignore_zones == 2) {
		std::sort(towns.begin(), towns.end(), [&](const TownID a, const TownID b) {
			return DistanceSquare(tile, Town::Get(a)->xy) < DistanceSquare(tile, Town::Get(a)->xy);
		});
		towns.resize(16);
	}

	CommandContainer cmd = NewCommandContainerBasic(
		tile,
		_cur_house, // p1 - house type and town index (town not yet set)
		InteractiveRandom(), // p2 - random bits for the house
		CMD_BUILD_HOUSE | CMD_MSG(STR_ERROR_CAN_T_BUILD_HOUSE_HERE),
		CcPlaySound_CONSTRUCTION_RAIL
	);

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
