/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cheat_gui.cpp GUI related to cheating. */

#include "stdafx.h"
#include "command_func.h"
#include "cheat_type.h"
#include "company_base.h"
#include "company_func.h"
#include "date_func.h"
#include "sl/saveload.h"
#include "textbuf_gui.h"
#include "window_gui.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "rail_gui.h"
#include "settings_gui.h"
#include "company_gui.h"
#include "linkgraph/linkgraphschedule.h"
#include "map_func.h"
#include "tile_map.h"
#include "newgrf.h"
#include "error.h"
#include "network/network.h"
#include "order_base.h"
#include "vehicle_base.h"
#include "currency.h"
#include "core/geometry_func.hpp"

#include "widgets/cheat_widget.h"

#include "table/sprites.h"

#include "safeguards.h"


/**
 * The 'amount' to cheat with.
 * This variable is semantically a constant value, but because the cheat
 * code requires to be able to write to the variable it is not constified.
 */
static int32_t _money_cheat_amount = 10000000;

/**
 * Handle cheating of money.
 * Note that the amount of money of a company must be changed through a command
 * rather than by setting a variable. Since the cheat data structure expects a
 * variable, the amount of given/taken money is used for this purpose.
 * @param p1 not used.
 * @param p2 is -1 or +1 (down/up)
 * @return Amount of money cheat.
 */
static int32_t ClickMoneyCheat(int32_t p1, int32_t p2)
{
	DoCommandPEx(0, 0, 0, (uint64_t)(p2 * _money_cheat_amount), _network_server || _network_settings_access ? CMD_MONEY_CHEAT_ADMIN : CMD_MONEY_CHEAT);
	return _money_cheat_amount;
}

/**
 * Handle changing of company.
 * @param p1 company to set to
 * @param p2 is -1 or +1 (down/up)
 * @return The new company.
 */
static int32_t ClickChangeCompanyCheat(int32_t p1, int32_t p2)
{
	while ((uint)p1 < Company::GetPoolSize()) {
		if (Company::IsValidID((CompanyID)p1)) {
			SetLocalCompany((CompanyID)p1);
			return _local_company;
		}
		p1 += p2;
	}

	return _local_company;
}

/**
 * Allow (or disallow) changing production of all industries.
 * @param p1 new value
 * @param p2 unused
 * @return New value allowing change of industry production.
 */
static int32_t ClickSetProdCheat(int32_t p1, int32_t p2)
{
	_cheats.setup_prod.value = (p1 != 0);
	InvalidateWindowClassesData(WC_INDUSTRY_VIEW);
	return _cheats.setup_prod.value;
}

extern void EnginesMonthlyLoop();

/**
 * Handle changing of the current year.
 * @param p1 The chosen year to change to.
 * @param p2 +1 (increase) or -1 (decrease).
 * @return New year.
 */
static int32_t ClickChangeDateCheat(int32_t p1, int32_t p2)
{
	/* Don't allow changing to an invalid year, or the current year. */
	p1 = Clamp(p1, MIN_YEAR, MAX_YEAR);
	if (p1 == _cur_year) return _cur_year;

	YearMonthDay ymd = ConvertDateToYMD(_date);
	Date new_date = ConvertYMDToDate(p1, ymd.month, ymd.day);

	/* Shift cached dates. */
	LinkGraphSchedule::instance.ShiftDates(new_date - _date);
	ShiftOrderDates(new_date - _date);
	ShiftVehicleDates(new_date - _date);

	/* Change the date. */
	SetDate(new_date, _date_fract);

	EnginesMonthlyLoop();
	InvalidateWindowClassesData(WC_BUILD_STATION, 0);
	InvalidateWindowClassesData(WC_BUS_STATION, 0);
	InvalidateWindowClassesData(WC_BUILD_OBJECT, 0);
	ResetSignalVariant();
	MarkWholeScreenDirty();
	return _cur_year;
}

/**
 * Allow (or disallow) a change of the maximum allowed heightlevel.
 * @param p1 new value
 * @param p2 unused
 * @return New value (or unchanged old value) of the maximum
 *         allowed heightlevel value.
 */
static int32_t ClickChangeMaxHlCheat(int32_t p1, int32_t p2)
{
	p1 = Clamp(p1, MIN_MAP_HEIGHT_LIMIT, MAX_MAP_HEIGHT_LIMIT);

	/* Check if at least one mountain on the map is higher than the new value.
	 * If yes, disallow the change. */
	for (TileIndex t = 0; t < MapSize(); t++) {
		if ((int32_t)TileHeight(t) > p1) {
			ShowErrorMessage(STR_CONFIG_SETTING_TOO_HIGH_MOUNTAIN, INVALID_STRING_ID, WL_ERROR);
			/* Return old, unchanged value */
			return _settings_game.construction.map_height_limit;
		}
	}

	/* Execute the change and reload GRF Data */
	_settings_game.construction.map_height_limit = p1;
	ReloadNewGRFData();

	/* The smallmap uses an index from heightlevels to colours. Trigger rebuilding it. */
	InvalidateWindowClassesData(WC_SMALLMAP, 2);

	return _settings_game.construction.map_height_limit;
}

/**
 * Signature of handler function when user clicks at a cheat.
 * @param p1 The new value.
 * @param p2 Change direction (+1, +1), \c 0 for boolean settings.
 */
typedef int32_t CheckButtonClick(int32_t p1, int32_t p2);

enum CheatNetworkMode {
	CNM_ALL,
	CNM_LOCAL_ONLY,
	CNM_MONEY,
};

/** Information of a cheat. */
struct CheatEntry {
	CheatNetworkMode mode; ///< network/local mode
	VarType type;          ///< type of selector
	StringID str;          ///< string with descriptive text
	void *variable;        ///< pointer to the variable
	bool *been_used;       ///< has this cheat been used before?
	CheckButtonClick *proc;///< procedure
};

/**
 * The available cheats.
 * Order matches with the values of #CheatNumbers
 */
static const CheatEntry _cheats_ui[] = {
	{CNM_MONEY,      SLE_INT32,       STR_CHEAT_MONEY,            &_money_cheat_amount,                          &_cheats.money.been_used,                  &ClickMoneyCheat           },
	{CNM_LOCAL_ONLY, SLE_UINT8,       STR_CHEAT_CHANGE_COMPANY,   &_local_company,                               &_cheats.switch_company.been_used,         &ClickChangeCompanyCheat   },
	{CNM_ALL,        SLE_BOOL,        STR_CHEAT_EXTRA_DYNAMITE,   &_cheats.magic_bulldozer.value,                &_cheats.magic_bulldozer.been_used,        nullptr                    },
	{CNM_ALL,        SLE_BOOL,        STR_CHEAT_CROSSINGTUNNELS,  &_cheats.crossing_tunnels.value,               &_cheats.crossing_tunnels.been_used,       nullptr                    },
	{CNM_ALL,        SLE_BOOL,        STR_CHEAT_NO_JETCRASH,      &_cheats.no_jetcrash.value,                    &_cheats.no_jetcrash.been_used,            nullptr                    },
	{CNM_LOCAL_ONLY, SLE_BOOL,        STR_CHEAT_SETUP_PROD,       &_cheats.setup_prod.value,                     &_cheats.setup_prod.been_used,             &ClickSetProdCheat         },
	{CNM_LOCAL_ONLY, SLE_UINT8,       STR_CHEAT_EDIT_MAX_HL,      &_settings_game.construction.map_height_limit, &_cheats.edit_max_hl.been_used,            &ClickChangeMaxHlCheat     },
	{CNM_LOCAL_ONLY, SLE_INT32,       STR_CHEAT_CHANGE_DATE,      &_cur_date_ymd.year,                           &_cheats.change_date.been_used,            &ClickChangeDateCheat      },
	{CNM_ALL,        SLF_ALLOW_CONTROL, STR_CHEAT_INFLATION_COST,   &_economy.inflation_prices,                  &_cheats.inflation_cost.been_used,         nullptr                    },
	{CNM_ALL,        SLF_ALLOW_CONTROL, STR_CHEAT_INFLATION_INCOME, &_economy.inflation_payment,                 &_cheats.inflation_income.been_used,       nullptr                    },
	{CNM_ALL,        SLE_BOOL,        STR_CHEAT_STATION_RATING,   &_cheats.station_rating.value,                 &_cheats.station_rating.been_used,         nullptr                    },
	{CNM_ALL,        SLE_BOOL,        STR_CHEAT_TOWN_RATING,      &_cheats.town_rating.value,                    &_cheats.town_rating.been_used,            nullptr                    },
};

static bool IsCheatAllowed(CheatNetworkMode mode)
{
	switch (mode) {
		case CNM_ALL:
			return !_networking || _network_server || _network_settings_access;

		case CNM_LOCAL_ONLY:
			return !_networking;

		case CNM_MONEY:
			return !_networking || _network_server || _network_settings_access || _settings_game.difficulty.money_cheat_in_multiplayer;
	}
	return false;
}

static_assert(CHT_NUM_CHEATS == lengthof(_cheats_ui));

/** Widget definitions of the cheat GUI. */
static constexpr NWidgetPart _nested_cheat_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY), SetDataTip(STR_CHEATS, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_C_PANEL), SetDataTip(0x0, STR_CHEATS_TOOLTIP), EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(WWT_LABEL, COLOUR_GREY, WID_C_NOTE), SetFill(1, 1), SetDataTip(STR_CHEATS_NOTE, STR_NULL), SetPadding(WidgetDimensions::unscaled.frametext),
	EndContainer(),
};

/** GUI for the cheats. */
struct CheatWindow : Window {
	int clicked;
	int clicked_widget;
	uint line_height;
	Dimension box;      ///< Dimension of box sprite
	Dimension icon;     ///< Dimension of company icon sprite

	CheatWindow(WindowDesc *desc) : Window(desc)
	{
		this->InitNested();
	}

	void OnInit() override
	{
		this->box = maxdim(GetSpriteSize(SPR_BOX_EMPTY), GetSpriteSize(SPR_BOX_CHECKED));
		this->icon = GetSpriteSize(SPR_COMPANY_ICON);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_C_PANEL) return;

		const Rect ir = r.Shrink(WidgetDimensions::scaled.framerect);
		int y = ir.top;

		bool rtl = _current_text_dir == TD_RTL;
		uint box_left    = rtl ? ir.right - this->box.width - WidgetDimensions::scaled.hsep_wide : ir.left + WidgetDimensions::scaled.hsep_wide;
		uint button_left = rtl ? ir.right - this->box.width - WidgetDimensions::scaled.hsep_wide * 2 - SETTING_BUTTON_WIDTH : ir.left + this->box.width + WidgetDimensions::scaled.hsep_wide * 2;
		uint text_left   = ir.left + (rtl ? 0 : WidgetDimensions::scaled.hsep_wide * 4 + this->box.width + SETTING_BUTTON_WIDTH);
		uint text_right  = ir.right - (rtl ? WidgetDimensions::scaled.hsep_wide * 4 + this->box.width + SETTING_BUTTON_WIDTH : 0);

		int text_y_offset = (this->line_height - GetCharacterHeight(FS_NORMAL)) / 2;
		int box_y_offset = (this->line_height - this->box.height) / 2;
		int button_y_offset = (this->line_height - SETTING_BUTTON_HEIGHT) / 2;
		int icon_y_offset = (this->line_height - this->icon.height) / 2;

		for (int i = 0; i != lengthof(_cheats_ui); i++) {
			const CheatEntry *ce = &_cheats_ui[i];
			if (!IsCheatAllowed(ce->mode)) continue;

			DrawSprite((*ce->been_used) ? SPR_BOX_CHECKED : SPR_BOX_EMPTY, PAL_NONE, box_left, y + box_y_offset);

			switch (ce->type) {
				case SLF_ALLOW_CONTROL: {
					/* Change inflation factors */

					/* Draw [<][>] boxes for settings of an integer-type */
					DrawArrowButtons(button_left, y + button_y_offset, COLOUR_YELLOW, clicked - (i * 2), true, true);

					uint64_t val = (uint64_t)ReadValue(ce->variable, SLE_UINT64);
					SetDParam(0, val * 1000 >> 16);
					SetDParam(1, 3);
					break;
				}

				case SLE_BOOL: {
					bool on = (*(bool*)ce->variable);

					DrawBoolButton(button_left, y + button_y_offset, on, true);
					SetDParam(0, on ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF);
					break;
				}

				default: {
					int32_t val = (int32_t)ReadValue(ce->variable, ce->type);

					/* Draw [<][>] boxes for settings of an integer-type */
					DrawArrowButtons(button_left, y + button_y_offset, COLOUR_YELLOW, clicked - (i * 2), true, true);

					switch (ce->str) {
						/* Display date for change date cheat */
						case STR_CHEAT_CHANGE_DATE: SetDParam(0, _date); break;

						/* Draw coloured flag for change company cheat */
						case STR_CHEAT_CHANGE_COMPANY: {
							SetDParam(0, val + 1);
							uint offset = WidgetDimensions::scaled.hsep_indent + GetStringBoundingBox(ce->str).width;
							DrawCompanyIcon(_local_company, rtl ? text_right - offset - WidgetDimensions::scaled.hsep_indent : text_left + offset, y + icon_y_offset);
							break;
						}

						default: SetDParam(0, val);
					}
					break;
				}
			}

			DrawString(text_left, text_right, y + text_y_offset, ce->str);

			y += this->line_height;
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		if (widget != WID_C_PANEL) return;

		uint width = 0;
		uint lines = 0;
		for (int i = 0; i != lengthof(_cheats_ui); i++) {
			const CheatEntry *ce = &_cheats_ui[i];
			if (!IsCheatAllowed(ce->mode)) continue;
			lines++;
			switch (ce->type) {
				case SLF_ALLOW_CONTROL:
					/* Change inflation factors */
					break;

				case SLE_BOOL:
					SetDParam(0, STR_CONFIG_SETTING_ON);
					width = std::max(width, GetStringBoundingBox(ce->str).width);
					SetDParam(0, STR_CONFIG_SETTING_OFF);
					width = std::max(width, GetStringBoundingBox(ce->str).width);
					break;

				default:
					switch (ce->str) {
						/* Display date for change date cheat */
						case STR_CHEAT_CHANGE_DATE:
							SetDParam(0, ConvertYMDToDate(MAX_YEAR, 11, 31));
							width = std::max(width, GetStringBoundingBox(ce->str).width);
							break;

						/* Draw coloured flag for change company cheat */
						case STR_CHEAT_CHANGE_COMPANY:
							SetDParamMaxValue(0, MAX_COMPANIES);
							width = std::max(width, GetStringBoundingBox(ce->str).width + WidgetDimensions::scaled.hsep_wide * 4);
							break;

						default:
							SetDParam(0, INT64_MAX);
							width = std::max(width, GetStringBoundingBox(ce->str).width);
							break;
					}
					break;
			}
		}

		this->line_height = std::max(this->box.height, this->icon.height);
		this->line_height = std::max<uint>(this->line_height, SETTING_BUTTON_HEIGHT);
		this->line_height = std::max<uint>(this->line_height, GetCharacterHeight(FS_NORMAL)) + WidgetDimensions::scaled.framerect.Vertical();

		size->width = width + WidgetDimensions::scaled.hsep_wide * 4 + this->box.width + SETTING_BUTTON_WIDTH /* stuff on the left */ + WidgetDimensions::scaled.hsep_wide * 2 /* extra spacing on right */;
		size->height = WidgetDimensions::scaled.framerect.Vertical() + this->line_height * lines;
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_C_PANEL) return;

		Rect r = this->GetWidget<NWidgetBase>(WID_C_PANEL)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
		uint btn = (pt.y - r.top) / this->line_height;
		uint x = pt.x - r.left;
		bool rtl = _current_text_dir == TD_RTL;
		if (rtl) x = r.Width() - 1 - x;

		for (uint i = 0; i != lengthof(_cheats_ui) && i <= btn; i++) {
			const CheatEntry *ce = &_cheats_ui[i];
			if (!IsCheatAllowed(ce->mode)) btn++;
		}

		if (btn >= lengthof(_cheats_ui)) return;

		const CheatEntry *ce = &_cheats_ui[btn];
		int value = (int32_t)ReadValue(ce->variable, ce->type);
		int oldvalue = value;

		if (btn == CHT_CHANGE_DATE && x >= WidgetDimensions::scaled.hsep_wide * 2 + this->box.width + SETTING_BUTTON_WIDTH) {
			/* Click at the date text directly. */
			clicked_widget = CHT_CHANGE_DATE;
			SetDParam(0, value);
			ShowQueryString(STR_JUST_INT, STR_CHEAT_CHANGE_DATE_QUERY_CAPT, 8, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
			return;
		} else if (btn == CHT_EDIT_MAX_HL && x >= WidgetDimensions::scaled.hsep_wide * 2 + this->box.width + SETTING_BUTTON_WIDTH) {
			clicked_widget = CHT_EDIT_MAX_HL;
			SetDParam(0, value);
			ShowQueryString(STR_JUST_INT, STR_CHEAT_EDIT_MAX_HL_QUERY_CAPT, 8, this, CS_NUMERAL, QSF_ACCEPT_UNCHANGED);
			return;
		} else if (btn == CHT_MONEY && x >= 20 + this->box.width + SETTING_BUTTON_WIDTH) {
			clicked_widget = CHT_MONEY;
			SetDParam(0, value);
			ShowQueryString(STR_JUST_INT, STR_CHEAT_EDIT_MONEY_QUERY_CAPT, 20, this, CS_NUMERAL_SIGNED, QSF_ACCEPT_UNCHANGED);
			return;
		} else if (ce->type == SLF_ALLOW_CONTROL && x >= 20 + this->box.width + SETTING_BUTTON_WIDTH) {
			clicked_widget = btn;
			uint64_t val = (uint64_t)ReadValue(ce->variable, SLE_UINT64);
			SetDParam(0, val * 1000 >> 16);
			SetDParam(1, 3);
			StringID str = (btn == CHT_INFLATION_COST) ? STR_CHEAT_INFLATION_COST_QUERY_CAPT : STR_CHEAT_INFLATION_INCOME_QUERY_CAPT;
			std::string saved = std::move(_settings_game.locale.digit_group_separator);
			_settings_game.locale.digit_group_separator = "";
			ShowQueryString(STR_JUST_DECIMAL, str, 12, this, CS_NUMERAL_DECIMAL, QSF_ACCEPT_UNCHANGED);
			_settings_game.locale.digit_group_separator = std::move(saved);
			return;
		}

		/* Not clicking a button? */
		if (!IsInsideMM(x, WidgetDimensions::scaled.hsep_wide * 2 + this->box.width, WidgetDimensions::scaled.hsep_wide * 2 + this->box.width + SETTING_BUTTON_WIDTH)) return;

		if (!_networking) *ce->been_used = true;

		auto get_arrow_button_value = [&]() -> int {
			return (x >= WidgetDimensions::scaled.hsep_wide * 2 + this->box.width + SETTING_BUTTON_WIDTH / 2) ? 1 : -1;
		};

		auto register_arrow_button_clicked = [&]() {
			this->clicked = btn * 2 + 1 + ((x >= WidgetDimensions::scaled.hsep_wide * 2 + this->box.width + SETTING_BUTTON_WIDTH / 2) != rtl ? 1 : 0);
		};

		switch (ce->type) {
			case SLF_ALLOW_CONTROL: {
				/* Change inflation factors */
				uint64_t oldvalue = (uint64_t)ReadValue(ce->variable, SLE_UINT64);
				uint64_t value = oldvalue + (uint64_t)(get_arrow_button_value() << 16);
				value = Clamp<uint64_t>(value, 1 << 16, MAX_INFLATION);
				DoCommandP(0, (uint32_t)btn, (uint32_t)value, CMD_CHEAT_SETTING);
				if (value != oldvalue) register_arrow_button_clicked();
				break;
			}

			case SLE_BOOL:
				value ^= 1;
				if (ce->proc != nullptr && !_networking) ce->proc(value, 0);
				break;

			default:
				/* Take whatever the function returns */
				int offset = get_arrow_button_value();
				value = ce->proc(value + offset, offset);

				/* The first cheat (money), doesn't return a different value. */
				if (value != oldvalue || btn == CHT_MONEY) register_arrow_button_clicked();
				break;
		}

		if (value != oldvalue) {
			if (_networking || btn == CHT_STATION_RATING || btn == CHT_TOWN_RATING) {
				if (btn != CHT_MONEY) DoCommandP(0, (uint32_t)btn, (uint32_t)value, CMD_CHEAT_SETTING);
			} else {
				WriteValue(ce->variable, ce->type, (int64_t)value);
			}
		}

		this->SetTimeout();

		this->SetDirty();
	}

	void OnTimeout() override
	{
		this->clicked = 0;
		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		/* Was 'cancel' pressed or nothing entered? */
		if (str == nullptr || StrEmpty(str)) return;

		const CheatEntry *ce = &_cheats_ui[clicked_widget];

		if (ce->type == SLF_ALLOW_CONTROL) {
			char tmp_buffer[32];
			strecpy(tmp_buffer, str, lastof(tmp_buffer));
			str_replace_wchar(tmp_buffer, lastof(tmp_buffer), GetDecimalSeparatorChar(), '.');
			DoCommandP(0, (uint32_t)clicked_widget, (uint32_t)Clamp<uint64_t>(atof(tmp_buffer) * 65536.0, 1 << 16, MAX_INFLATION), CMD_CHEAT_SETTING);
			return;
		}
		if (ce->mode == CNM_MONEY) {
			if (!_networking) *ce->been_used = true;
			DoCommandPEx(0, 0, 0, (std::strtoll(str, nullptr, 10) / _currency->rate), _network_server || _network_settings_access ? CMD_MONEY_CHEAT_ADMIN : CMD_MONEY_CHEAT);
			return;
		}

		if (_networking) return;
		int oldvalue = (int32_t)ReadValue(ce->variable, ce->type);
		int value = atoi(str);
		*ce->been_used = true;
		value = ce->proc(value, value - oldvalue);

		if (value != oldvalue) WriteValue(ce->variable, ce->type, (int64_t)value);
		this->SetDirty();
	}
};

/** Window description of the cheats GUI. */
static WindowDesc _cheats_desc(__FILE__, __LINE__,
	WDP_AUTO, "cheats", 0, 0,
	WC_CHEATS, WC_NONE,
	0,
	std::begin(_nested_cheat_widgets), std::end(_nested_cheat_widgets)
);

/** Open cheat window. */
void ShowCheatWindow()
{
	CloseWindowById(WC_CHEATS, 0);
	if (!_networking || _network_server || _network_settings_access || _settings_game.difficulty.money_cheat_in_multiplayer) {
		new CheatWindow(&_cheats_desc);
	}
}
