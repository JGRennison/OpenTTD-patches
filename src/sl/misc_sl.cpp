/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc_sl.cpp Saving and loading of things that didn't fit anywhere else */

#include "../stdafx.h"
#include "../date_func.h"
#include "../zoom_func.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../viewport_func.h"
#include "../gfx_func.h"
#include "../core/random_func.hpp"
#include "../fios.h"
#include "../load_check.h"
#include "../road_type.h"
#include "../core/checksum_func.hpp"
#include "../event_logs.h"
#include "../timer/timer.h"
#include "../timer/timer_game_tick.h"

#include "saveload.h"

#include "../safeguards.h"

extern TileIndex _cur_tileloop_tile;
extern TileIndex _aux_tileloop_tile;
extern uint16_t _disaster_delay;
extern uint8_t _trees_tick_ctr;
extern uint64_t _aspect_cfg_hash;

/* Keep track of current game position */
int _saved_scrollpos_x;
int _saved_scrollpos_y;
ZoomLevel _saved_scrollpos_zoom;

void SaveViewportBeforeSaveGame()
{
	/* Don't use GetMainWindow() in case the window does not exist. */
	const Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
	if (w == nullptr || w->viewport == nullptr) {
		/* Ensure saved position is clearly invalid. */
		_saved_scrollpos_x = INT_MAX;
		_saved_scrollpos_y = INT_MAX;
		_saved_scrollpos_zoom = ZOOM_LVL_END;
	} else {
		_saved_scrollpos_x = w->viewport->scrollpos_x;
		_saved_scrollpos_y = w->viewport->scrollpos_y;
		_saved_scrollpos_zoom = w->viewport->zoom;
	}
}

void ResetViewportAfterLoadGame()
{
	Window *w = GetMainWindow();

	w->viewport->scrollpos_x = _saved_scrollpos_x;
	w->viewport->scrollpos_y = _saved_scrollpos_y;
	w->viewport->dest_scrollpos_x = _saved_scrollpos_x;
	w->viewport->dest_scrollpos_y = _saved_scrollpos_y;

	Viewport *vp = w->viewport;
	vp->zoom = std::min(_saved_scrollpos_zoom, ZOOM_LVL_MAX);
	vp->virtual_width = ScaleByZoom(vp->width, vp->zoom);
	vp->virtual_height = ScaleByZoom(vp->height, vp->zoom);

	/* If zoom_max is ZOOM_LVL_MIN then the setting has not been loaded yet, therefore all levels are allowed. */
	if (_settings_client.gui.zoom_max != ZOOM_LVL_MIN) {
		/* Ensure zoom level is allowed */
		while (vp->zoom < _settings_client.gui.zoom_min) DoZoomInOutWindow(ZOOM_OUT, w);
		while (vp->zoom > _settings_client.gui.zoom_max) DoZoomInOutWindow(ZOOM_IN, w);
	}

	DoZoomInOutWindow(ZOOM_NONE, w); // update button status
	MarkWholeScreenDirty();
}

uint8_t _age_cargo_skip_counter; ///< Skip aging of cargo? Used before savegame version 162.
extern TimeoutTimer<TimerGameTick> _new_competitor_timeout;

static const NamedSaveLoad _date_desc[] = {
	NSL("",                             SLEG_CONDVAR(CalTime::Detail::now.cal_date, SLE_FILE_U16 | SLE_VAR_I32, SL_MIN_VERSION,         SLV_31)),
	NSL("date",                         SLEG_CONDVAR(CalTime::Detail::now.cal_date,              SLE_INT32,         SLV_31, SL_MAX_VERSION)),
	NSL("date_fract",                   SLEG_VAR(CalTime::Detail::now.cal_date_fract,           SLE_UINT16)),
	NSL("",                             SLEG_CONDVAR_X(_tick_counter,           SLE_FILE_U16 | SLE_VAR_U64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER, 0, 0))),
	NSL("tick_counter",                 SLEG_CONDVAR_X(_tick_counter,                           SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER))),
	NSL("tick_skip_counter",            SLEG_CONDVAR_X(DateDetail::_tick_skip_counter,           SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH))),
	NSL("scaled_tick_counter",          SLEG_CONDVAR_X(_scaled_tick_counter,                    SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 3))),
	NSL("",                             SLEG_CONDVAR_X(DateDetail::_state_ticks_offset,          SLE_INT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 3, 3))),
	NSL("state_ticks",                  SLEG_CONDVAR_X(_state_ticks,                             SLE_INT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 4))),
	NSL("",                             SLE_CONDNULL(2,                                                     SL_MIN_VERSION,        SLV_157)), // _vehicle_id_ctr_day
	NSL("",                             SLEG_CONDVAR(_age_cargo_skip_counter,                    SLE_UINT8, SL_MIN_VERSION,        SLV_162)),
	NSL("",                             SLE_CONDNULL(1,                                                     SL_MIN_VERSION,         SLV_46)),
	NSL("",                             SLEG_CONDVAR(_cur_tileloop_tile,        SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION,          SLV_6)),
	NSL("cur_tileloop_tile",            SLEG_CONDVAR(_cur_tileloop_tile,                        SLE_UINT32,          SLV_6, SL_MAX_VERSION)),
	NSL("next_disaster_start",          SLEG_VAR(_disaster_delay,                               SLE_UINT16)),
	NSL("",                             SLE_CONDNULL(2,                                                     SL_MIN_VERSION,        SLV_120)),
	NSL("random_state[0]",              SLEG_VAR(_random.state[0],                              SLE_UINT32)),
	NSL("random_state[1]",              SLEG_VAR(_random.state[1],                              SLE_UINT32)),
	NSL("state_checksum",               SLEG_CONDVAR_X(_state_checksum.state,                   SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_STATE_CHECKSUM))),
	NSL("",                             SLE_CONDNULL(1,                                                     SL_MIN_VERSION,         SLV_10)),
	NSL("",                             SLE_CONDNULL(4,                                                             SLV_10,        SLV_120)),
	NSL("company_tick_counter",         SLEG_VAR(_cur_company_tick_index,       SLE_FILE_U8  | SLE_VAR_U32)),
	NSL("",                             SLEG_CONDVAR(_new_competitor_timeout.period.value, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION,   SLV_109)),
	NSL("",                             SLEG_CONDVAR_X(_new_competitor_timeout.period.value,    SLE_UINT32,        SLV_109, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE, 0, 0))),
	NSL("trees_tick_counter",           SLEG_VAR(_trees_tick_ctr,                                SLE_UINT8)),
	NSL("pause_mode",                   SLEG_CONDVAR(_pause_mode,                                SLE_UINT8,          SLV_4, SL_MAX_VERSION)),
	NSL("game_events_overall",          SLEG_CONDVAR_X(_game_events_overall,                    SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GAME_EVENTS))),
	NSL("road_layout_change_counter",   SLEG_CONDVAR_X(_road_layout_change_counter,             SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_LAYOUT_CHANGE_CTR))),
	NSL("",                             SLE_CONDNULL_X(1,                                                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 4, 6))), // _extra_aspects
	NSL("aspect_cfg_hash",              SLEG_CONDVAR_X(_aspect_cfg_hash,                        SLE_UINT64, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 7))),
	NSL("aux_tileloop_tile",            SLEG_CONDVAR_X(_aux_tileloop_tile,                      SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUX_TILE_LOOP))),
	NSL("",                             SLE_CONDNULL(4,                                                             SLV_11,        SLV_120)),
	NSL("competitors_interval",         SLEG_CONDVAR_X(_new_competitor_timeout.period.value,    SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE))),
	NSL("competitors_interval_elapsed", SLEG_CONDVAR_X(_new_competitor_timeout.storage.elapsed, SLE_UINT32, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE))),
	NSL("competitors_interval_fired",   SLEG_CONDVAR_X(_new_competitor_timeout.fired,             SLE_BOOL, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE))),

	/* New (table only) fields below */
	NSLT("id",                          SLEG_CONDSSTR_X(_game_session_stats.savegame_id,           SLE_STR,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SAVEGAME_ID))),
	NSLT("economy_date",                SLEG_CONDVAR_X(EconTime::Detail::now.econ_date,          SLE_INT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 5))),
	NSLT("economy_date_fract",          SLEG_CONDVAR_X(EconTime::Detail::now.econ_date_fract,   SLE_UINT16,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 5))),
	NSLT("calendar_sub_date_fract",     SLEG_CONDVAR_X(CalTime::Detail::now.sub_date_fract,     SLE_UINT16,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 5))),
	NSLT("economy_years_elapsed",       SLEG_CONDVAR_X(EconTime::Detail::years_elapsed,          SLE_INT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 6))),
	NSLT("period_display_offset",       SLEG_CONDVAR_X(EconTime::Detail::period_display_offset,  SLE_INT32,                   SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 6))),
};

static const NamedSaveLoad _date_check_desc[] = {
	NSL("date", SLEG_CONDVAR(_load_check_data.current_date,  SLE_FILE_U16 | SLE_VAR_I32,  SL_MIN_VERSION,  SLV_31)),
	NSL("date", SLEG_CONDVAR(_load_check_data.current_date,  SLE_INT32,                  SLV_31, SL_MAX_VERSION)),
	NSL("",         SLE_NULL(2)),                       // _date_fract
	NSL("",     SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER, 0, 0))), // _tick_counter
	NSL("",     SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_U64_TICK_COUNTER))),       // _tick_counter
	NSL("",     SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH))), // _tick_skip_counter
	NSL("",     SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 3))), // _scaled_tick_counter
	NSL("",     SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 3, 3))), // _state_ticks_offset
	NSL("",     SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_VARIABLE_DAY_LENGTH, 4))), // _state_ticks
	NSL("",     SLE_CONDNULL(2, SL_MIN_VERSION, SLV_157)),               // _vehicle_id_ctr_day
	NSL("",     SLE_CONDNULL(1, SL_MIN_VERSION, SLV_162)),               // _age_cargo_skip_counter
	NSL("",     SLE_CONDNULL(1, SL_MIN_VERSION, SLV_46)),
	NSL("",     SLE_CONDNULL(2, SL_MIN_VERSION, SLV_6)),                 // _cur_tileloop_tile
	NSL("",     SLE_CONDNULL(4, SLV_6, SL_MAX_VERSION)),    // _cur_tileloop_tile
	NSL("",         SLE_NULL(2)),                       // _disaster_delay
	NSL("",     SLE_CONDNULL(2, SL_MIN_VERSION, SLV_120)),
	NSL("",         SLE_NULL(4)),                       // _random.state[0]
	NSL("",         SLE_NULL(4)),                       // _random.state[1]
	NSL("",   SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_STATE_CHECKSUM))), // _state_checksum.state
	NSL("",     SLE_CONDNULL(1,  SL_MIN_VERSION,  SLV_10)),
	NSL("",     SLE_CONDNULL(4, SLV_10, SLV_120)),
	NSL("",         SLE_NULL(1)),                       // _cur_company_tick_index
	NSL("",     SLE_CONDNULL(2, SL_MIN_VERSION, SLV_109)),                                                           // _new_competitor_timeout.period.value
	NSL("",     SLE_CONDNULL_X(4, SLV_109, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE, 0, 0))), // _new_competitor_timeout.period.value
	NSL("",         SLE_NULL(1)),                       // _trees_tick_ctr
	NSL("",     SLE_CONDNULL(1, SLV_4, SL_MAX_VERSION)),    // _pause_mode
	NSL("",     SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_GAME_EVENTS))), // _game_events_overall
	NSL("",     SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_ROAD_LAYOUT_CHANGE_CTR))), // _road_layout_change_counter
	NSL("",     SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 4, 6))), // _extra_aspects
	NSL("",     SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_REALISTIC_TRAIN_BRAKING, 7))), // _aspect_cfg_hash
	NSL("",     SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AUX_TILE_LOOP))), // _aux_tileloop_tile
	NSL("",     SLE_CONDNULL(4, SLV_11, SLV_120)),
	NSL("",     SLE_CONDNULL_X(9, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_AI_START_DATE))), // _new_competitor_timeout
};

/* Save load date related variables as well as persistent tick counters
 * XXX: currently some unrelated stuff is just put here */
static void Save_DATE()
{
	SlSaveTableObjectChunk(_date_desc);
}

static void Load_DATE()
{
	SlLoadTableOrRiffFiltered(_date_desc);
}

static void Check_DATE()
{
	SlLoadTableOrRiffFiltered(_date_check_desc);
	if (IsSavegameVersionBefore(SLV_31)) {
		_load_check_data.current_date += CalTime::DAYS_TILL_ORIGINAL_BASE_YEAR.AsDelta();
	}
}


static const SaveLoad _view_desc[] = {
	SLEG_CONDVAR(_saved_scrollpos_x,    SLE_FILE_I16 | SLE_VAR_I32, SL_MIN_VERSION, SLV_6),
	SLEG_CONDVAR(_saved_scrollpos_x,    SLE_INT32,                  SLV_6, SL_MAX_VERSION),
	SLEG_CONDVAR(_saved_scrollpos_y,    SLE_FILE_I16 | SLE_VAR_I32, SL_MIN_VERSION, SLV_6),
	SLEG_CONDVAR(_saved_scrollpos_y,    SLE_INT32,                  SLV_6, SL_MAX_VERSION),
	    SLEG_VAR(_saved_scrollpos_zoom, SLE_UINT8),
};

static void Load_VIEW()
{
	SlGlobList(_view_desc);
}

static const SaveLoad _misc_desc[] = {
	SLEG_CONDSSTR_X(_game_session_stats.savegame_id, SLE_STR, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SAVEGAME_ID)),
};

static void Load_MISC()
{
	SlGlobList(_misc_desc);
}

static const ChunkHandler misc_chunk_handlers[] = {
	{ 'DATE', Save_DATE, Load_DATE, nullptr, Check_DATE, CH_TABLE },
	MakeSaveUpstreamFeatureConditionalLoadUpstreamChunkHandler<'VIEW', XSLFI_TABLE_MISC_SL>(Load_VIEW, nullptr, nullptr),
	{ 'MISC', nullptr, Load_MISC, nullptr, nullptr, CH_UNUSED },
};

extern const ChunkHandlerTable _misc_chunk_handlers(misc_chunk_handlers);
