/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file statusbar_gui.cpp The GUI for the bottom status bar. */

#include "stdafx.h"
#include "core/backup_type.hpp"
#include "date_func.h"
#include "gfx_func.h"
#include "news_func.h"
#include "company_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "company_base.h"
#include "tilehighlight_func.h"
#include "news_gui.h"
#include "company_gui.h"
#include "window_gui.h"
#include "sl/saveload.h"
#include "window_func.h"
#include "statusbar_gui.h"
#include "toolbar_gui.h"
#include "core/geometry_func.hpp"
#include "guitimer_func.h"
#include "zoom_func.h"

#include "widgets/statusbar_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static bool DrawScrollingStatusText(const NewsItem *ni, int scroll_pos, int left, int right, int top, int bottom)
{
	CopyInDParam(ni->params);

	/* Replace newlines and the likes with spaces. */
	std::string message = StrMakeValid(GetString(ni->string_id), SVS_REPLACE_TAB_CR_NL_WITH_SPACE);

	DrawPixelInfo tmp_dpi;
	if (!FillDrawPixelInfo(&tmp_dpi, left, top, right - left, bottom)) return true;

	int width = GetStringBoundingBox(message).width;
	int pos = (_current_text_dir == TD_RTL) ? (scroll_pos - width) : (right - scroll_pos - left);

	AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
	DrawString(pos, INT16_MAX, 0, message, TC_LIGHT_BLUE, SA_LEFT | SA_FORCE);

	return (_current_text_dir == TD_RTL) ? (pos < right - left) : (pos + width > 0);
}

struct StatusBarWindow : Window {
	bool saving;
	int ticker_scroll;
	GUITimer ticker_timer;
	GUITimer reminder_timeout;
	TickMinutes last_minute = 0;

	static const int TICKER_STOP    = 1640; ///< scrolling is finished when counter reaches this value
	static const int REMINDER_START = 1350; ///< time in ms for reminder notification (red dot on the right) to stay
	static const int REMINDER_STOP  =    0; ///< reminder disappears when counter reaches this value
	static const int COUNTER_STEP   =    2; ///< this is subtracted from active counters every tick

	StatusBarWindow(WindowDesc &desc) : Window(desc)
	{
		this->ticker_scroll = TICKER_STOP;
		this->ticker_timer.SetInterval(15);
		this->reminder_timeout.SetInterval(REMINDER_STOP);

		this->InitNested();
		CLRBITS(this->flags, WF_WHITE_BORDER);
		PositionStatusbar(this);
	}

	Point OnInitialPosition(int16_t sm_width, int16_t sm_height, int window_number) override
	{
		Point pt = { 0, _screen.height - sm_height };
		return pt;
	}

	void FindWindowPlacementAndResize([[maybe_unused]] int def_width, [[maybe_unused]] int def_height) override
	{
		Window::FindWindowPlacementAndResize(_toolbar_width, def_height);
	}

	StringID PrepareHHMMDateString(int hhmm, CalTime::Date date, CalTime::Year year) const
	{
		SetDParam(0, hhmm);
		switch (_settings_client.gui.date_with_time) {
			case 0:
				return STR_JUST_TIME_HHMM;

			case 1:
				SetDParam(1, year);
				return STR_HHMM_WITH_DATE_Y;

			case 2:
				SetDParam(1, date);
				return STR_HHMM_WITH_DATE_YM;

			case 3:
				SetDParam(1, date);
				return STR_HHMM_WITH_DATE_YMD;

			default:
				NOT_REACHED();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		Dimension d;
		switch (widget) {
			case WID_S_LEFT:
				if (_settings_time.time_in_minutes) {
					StringID str = PrepareHHMMDateString(GetBroadestDigitsValue(4), CalTime::MAX_DATE, CalTime::MAX_YEAR);
					d = GetStringBoundingBox(str);
				} else {
					SetDParam(0, CalTime::MAX_DATE);
					d = GetStringBoundingBox(STR_JUST_DATE_LONG);
				}
				break;

			case WID_S_RIGHT: {
				int64_t max_money = UINT32_MAX;
				for (const Company *c : Company::Iterate()) max_money = std::max<int64_t>(c->money, max_money);
				SetDParam(0, 100LL * max_money);
				d = GetStringBoundingBox(STR_JUST_CURRENCY_LONG);
				break;
			}

			default:
				return;
		}

		d.width += padding.width;
		d.height += padding.height;
		size = maxdim(d, size);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect, RectPadding::zero);
		tr.top = CenterBounds(r.top, r.bottom, GetCharacterHeight(FS_NORMAL));
		switch (widget) {
			case WID_S_LEFT:
				/* Draw the date */
				if (_settings_time.time_in_minutes) {
					StringID str = PrepareHHMMDateString(_settings_time.ToTickMinutes(_state_ticks).ClockHHMM(), CalTime::CurDate(), CalTime::CurYear());
					DrawString(tr, str, TC_WHITE, SA_HOR_CENTER);
				} else {
					SetDParam(0, CalTime::CurDate());
					DrawString(tr, STR_JUST_DATE_LONG, TC_WHITE, SA_HOR_CENTER);
				}
				break;

			case WID_S_RIGHT: {
				if (_local_company == COMPANY_SPECTATOR) {
					DrawString(tr, STR_STATUSBAR_SPECTATOR, TC_FROMSTRING, SA_HOR_CENTER);
				} else if (_settings_game.difficulty.infinite_money) {
					DrawString(tr, STR_STATUSBAR_INFINITE_MONEY, TC_FROMSTRING, SA_HOR_CENTER);
				} else {
					/* Draw company money, if any */
					const Company *c = Company::GetIfValid(_local_company);
					if (c != nullptr) {
						SetDParam(0, c->money);
						DrawString(tr, STR_JUST_CURRENCY_LONG, TC_WHITE, SA_HOR_CENTER);
					}
				}
				break;
			}

			case WID_S_MIDDLE:
				/* Draw status bar */
				if (this->saving) { // true when saving is active
					DrawString(tr, STR_STATUSBAR_SAVING_GAME, TC_FROMSTRING, SA_HOR_CENTER | SA_VERT_CENTER);
				} else if (_do_autosave) {
					DrawString(tr, STR_STATUSBAR_AUTOSAVE, TC_FROMSTRING, SA_HOR_CENTER);
				} else if (_pause_mode != PM_UNPAUSED) {
					StringID msg = (_pause_mode & PM_PAUSED_LINK_GRAPH) ? STR_STATUSBAR_PAUSED_LINK_GRAPH : STR_STATUSBAR_PAUSED;
					DrawString(tr, msg, TC_FROMSTRING, SA_HOR_CENTER);
				} else if (this->ticker_scroll < TICKER_STOP && GetStatusbarNews() != nullptr && GetStatusbarNews()->string_id != 0) {
					/* Draw the scrolling news text */
					if (!DrawScrollingStatusText(GetStatusbarNews(), ScaleGUITrad(this->ticker_scroll), tr.left, tr.right, tr.top, tr.bottom)) {
						InvalidateWindowData(WC_STATUS_BAR, 0, SBI_NEWS_DELETED);
						if (Company::IsValidID(_local_company)) {
							/* This is the default text */
							SetDParam(0, _local_company);
							DrawString(tr, STR_STATUSBAR_COMPANY_NAME, TC_FROMSTRING, SA_HOR_CENTER);
						}
					}
				} else {
					if (Company::IsValidID(_local_company)) {
						/* This is the default text */
						SetDParam(0, _local_company);
						DrawString(tr, STR_STATUSBAR_COMPANY_NAME, TC_FROMSTRING, SA_HOR_CENTER);
					}
				}

				if (!this->reminder_timeout.HasElapsed()) {
					Dimension icon_size = GetSpriteSize(SPR_UNREAD_NEWS);
					DrawSprite(SPR_UNREAD_NEWS, PAL_NONE, tr.right - icon_size.width, CenterBounds(r.top, r.bottom, icon_size.height));
				}
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		switch (data) {
			default: NOT_REACHED();
			case SBI_SAVELOAD_START:  this->saving = true;  break;
			case SBI_SAVELOAD_FINISH: this->saving = false; break;
			case SBI_SHOW_TICKER:     this->ticker_scroll = 0; break;
			case SBI_SHOW_REMINDER:   this->reminder_timeout.SetInterval(REMINDER_START); break;
			case SBI_NEWS_DELETED:
				this->ticker_scroll    =   TICKER_STOP; // reset ticker ...
				this->reminder_timeout.SetInterval(REMINDER_STOP); // ... and reminder
				break;
			case SBI_REINIT:
				this->ReInit();
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_S_MIDDLE: ShowLastNewsMessage(); break;
			case WID_S_RIGHT:  if (_local_company != COMPANY_SPECTATOR) ShowCompanyFinances(_local_company); break;
			default: ResetObjectToPlace();
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (_pause_mode != PM_UNPAUSED) return;

		if (_settings_time.time_in_minutes) {
			const TickMinutes now = _settings_time.NowInTickMinutes();
			if (this->last_minute != now) {
				this->last_minute = now;
				this->SetWidgetDirty(WID_S_LEFT);
			}
		}

		if (this->ticker_scroll < TICKER_STOP) { // Scrolling text
			uint count = this->ticker_timer.CountElapsed(delta_ms);
			if (count > 0) {
				this->ticker_scroll += count;
				this->SetWidgetDirty(WID_S_MIDDLE);
			}
		}

		// Red blot to show there are new unread newsmessages
		if (this->reminder_timeout.Elapsed(delta_ms)) {
			this->SetWidgetDirty(WID_S_MIDDLE);
		}
	}
};

static constexpr NWidgetPart _nested_main_status_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_S_LEFT), SetMinimalSize(160, 12), EndContainer(),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_S_MIDDLE), SetMinimalSize(40, 12), SetDataTip(0x0, STR_STATUSBAR_TOOLTIP_SHOW_LAST_NEWS), SetResize(1, 0),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_S_RIGHT), SetMinimalSize(140, 12),
	EndContainer(),
};

static WindowDesc _main_status_desc(__FILE__, __LINE__,
	WDP_MANUAL, nullptr, 0, 0,
	WC_STATUS_BAR, WC_NONE,
	WDF_NO_FOCUS | WDF_NO_CLOSE,
	_nested_main_status_widgets
);

/**
 * Checks whether the news ticker is currently being used.
 */
bool IsNewsTickerShown()
{
	const StatusBarWindow *w = dynamic_cast<StatusBarWindow*>(FindWindowById(WC_STATUS_BAR, 0));
	return w != nullptr && w->ticker_scroll < StatusBarWindow::TICKER_STOP;
}

/**
 * Show our status bar.
 */
void ShowStatusBar()
{
	new StatusBarWindow(_main_status_desc);
}
