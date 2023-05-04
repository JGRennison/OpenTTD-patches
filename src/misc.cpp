/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file misc.cpp Misc functions that shouldn't be here. */

#include "stdafx.h"
#include "landscape.h"
#include "news_func.h"
#include "ai/ai.hpp"
#include "script/script_gui.h"
#include "newgrf.h"
#include "newgrf_house.h"
#include "economy_func.h"
#include "date_func.h"
#include "texteff.hpp"
#include "gfx_func.h"
#include "gamelog.h"
#include "animated_tile_func.h"
#include "tilehighlight_func.h"
#include "network/network_func.h"
#include "window_func.h"
#include "core/pool_type.hpp"
#include "game/game.hpp"
#include "linkgraph/linkgraphschedule.h"
#include "station_kdtree.h"
#include "town_kdtree.h"
#include "viewport_kdtree.h"
#include "newgrf_profiling.h"
#include "tracerestrict.h"
#include "programmable_signals.h"
#include "viewport_func.h"
#include "bridge_signal_map.h"
#include "command_func.h"
#include "zoning.h"
#include "cargopacket.h"
#include "tbtr_template_vehicle_func.h"
#include "event_logs.h"

#include "safeguards.h"


extern TileIndex _cur_tileloop_tile;
extern TileIndex _aux_tileloop_tile;
extern void ClearAllSignalSpeedRestrictions();
extern void MakeNewgameSettingsLive();

void InitializeSound();
void InitializeMusic();
void InitializeVehicles();
void InitializeRailGui();
void InitializeRoadGui();
void InitializeAirportGui();
void InitializeDockGui();
void InitializeGraphGui();
void InitializeObjectGui();
void InitializeTownGui();
void InitializeIndustries();
void InitializeObjects();
void InitializeTrees();
void InitializeCompanies();
void InitializeCheats();
void InitializeNPF();
void InitializeOldNames();

void InitializeGame(uint size_x, uint size_y, bool reset_date, bool reset_settings)
{
	/* Make sure there isn't any window that can influence anything
	 * related to the new game we're about to start/load. */
	UnInitWindowSystem();

	/* Clear link graph schedule and stop any link graph threads before
	 * changing the map size. This avoids data races on the map size variables. */
	LinkGraphSchedule::Clear();

	AllocateMap(size_x, size_y);

	ViewportMapClearTunnelCache();
	ClearCommandLog();
	ClearCommandQueue();
	ClearSpecialEventsLog();
	ClearDesyncMsgLog();

	_pause_mode = PM_UNPAUSED;
	_pause_countdown = 0;
	_game_speed = 100;
	_tick_counter = 0;
	_tick_skip_counter = 0;
	_scaled_tick_counter = 0;
	_scaled_date_ticks_offset = 0;
	_cur_tileloop_tile = 1;
	_aux_tileloop_tile = 1;
	_thd.redsq = INVALID_TILE;
	_road_layout_change_counter = 0;
	_loaded_local_company = COMPANY_SPECTATOR;
	_game_events_since_load = (GameEventFlags) 0;
	_game_events_overall = (GameEventFlags) 0;
	_game_load_cur_date_ymd = { 0, 0, 0 };
	_game_load_date_fract = 0;
	_game_load_tick_skip_counter = 0;
	_game_load_time = 0;
	_extra_aspects = 0;
	_aspect_cfg_hash = 0;
	InitGRFGlobalVars();
	_loadgame_DBGL_data.clear();
	if (reset_settings) MakeNewgameSettingsLive();

	_newgrf_profilers.clear();

	if (reset_date) {
		SetDate(ConvertYMDToDate(_settings_game.game_creation.starting_year, 0, 1), 0, false);
		InitializeOldNames();
	} else {
		SetScaledTickVariables();
	}
	UpdateCachedSnowLine();
	UpdateCachedSnowLineBounds();

	ClearTraceRestrictMapping();
	ClearBridgeSimulatedSignalMapping();
	ClearBridgeSignalStyleMapping();
	ClearCargoPacketDeferredPayments();
	PoolBase::Clean(PT_NORMAL);

	extern void ClearNewSignalStyleMapping();
	ClearNewSignalStyleMapping();

	RebuildStationKdtree();
	RebuildTownKdtree();
	RebuildViewportKdtree();

	FreeSignalPrograms();
	FreeSignalDependencies();

	ClearAllSignalSpeedRestrictions();

	ClearZoningCaches();
	IntialiseOrderDestinationRefcountMap();

	ResetPersistentNewGRFData();

	InitializeSound();
	InitializeMusic();

	InitializeVehicles();

	InitNewsItemStructs();
	InitializeLandscape();
	InitializeRailGui();
	InitializeRoadGui();
	InitializeAirportGui();
	InitializeDockGui();
	InitializeGraphGui();
	InitializeObjectGui();
	InitializeTownGui();
	InitializeScriptGui();
	InitializeTrees();
	InitializeIndustries();
	InitializeObjects();
	InitializeBuildingCounts();

	InitializeNPF();

	InitializeCompanies();
	AI::Initialize();
	Game::Initialize();
	InitializeCheats();

	InitTextEffects();
	NetworkInitChatMessage();
	InitializeAnimatedTiles();

	InitializeEconomy();

	InvalidateVehicleTickCaches();
	ClearVehicleTickCaches();
	InvalidateTemplateReplacementImages();

	ResetObjectToPlace();
	ResetRailPlacementSnapping();

	GamelogReset();
	GamelogStartAction(GLAT_START);
	GamelogRevision();
	GamelogMode();
	GamelogGRFAddList(_grfconfig);
	GamelogStopAction();
}
