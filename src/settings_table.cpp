/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file settings_table.cpp The tables of all the settings as well as the implementation of most of their callbacks. */

#include "stdafx.h"
#include "base_media_base.h"
#include "base_media_music.h"
#include "base_media_sounds.h"
#include "currency.h"
#include "date_func.h"
#include "elrail_func.h"
#include "engine_override.h"
#include "station_base.h"
#include "station_func.h"
#include "error.h"
#include "genworld.h"
#include "gfx_func.h"
#include "graph_gui.h"
#include "gui.h"
#include "infrastructure_func.h"
#include "order_func.h"
#include "plans_func.h"
#include "rail.h"
#include "rail_gui.h"
#include "roadveh.h"
#include "scope_info.h"
#include "screenshot.h"
#include "settings_internal.h"
#include "settings_func.h"
#include "settings_table.h"
#include "ship.h"
#include "smallmap_gui.h"
#include "spritecache.h"
#include "statusbar_gui.h"
#include "strings_type.h"
#include "textbuf_gui.h"
#include "town.h"
#include "train.h"
#include "vehicle_base.h"
#include "vehicle_func.h"
#include "viewport_func.h"
#include "viewport_type.h"
#include "void_map.h"
#include "widget_type.h"
#include "window_func.h"
#include "window_type.h"
#include "zoning.h"
#include "ai/ai.hpp"
#include "ai/ai_config.hpp"
#include "ai/ai_instance.hpp"
#include "game/game.hpp"
#include "game/game_config.hpp"
#include "game/game_instance.hpp"
#include "network/network.h"
#include "network/network_func.h"
#include "network/core/config.h"
#include "pathfinder/pathfinder_type.h"
#include "blitter/factory.hpp"
#include "music/music_driver.hpp"
#include "sound/sound_driver.hpp"
#include "video/video_driver.hpp"

#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
#define HAS_TRUETYPE_FONT
#include "fontcache.h"
#endif

#include "sl/saveload.h"

#include "table/strings.h"
#include "table/settings.h"
#include "table/settings_compat.h"

bool _fallback_gui_zoom_max = false;

/**
 * List of all the generic setting tables.
 *
 * There are a few tables that are special and not processed like the rest:
 * - _currency_settings
 * - _misc_settings
 * - _company_settings
 * - _win32_settings
 * As such, they are not part of this list.
 */
extern const std::initializer_list<SettingTable> _generic_setting_tables{
	_difficulty_settings,
	_economy_settings,
	_game_settings,
	_gui_settings,
	_linkgraph_settings,
	_locale_settings,
	_multimedia_settings,
	_network_settings,
	_news_display_settings,
	_pathfinding_settings,
	_script_settings,
	_world_settings,
	_scenario_settings,
};

/**
 * List of all the save/load (PATS/PATX) setting tables.
 */
extern const std::initializer_list<SettingTable> _saveload_setting_tables{
	_difficulty_settings,
	_economy_settings,
	_game_settings,
	_linkgraph_settings,
	_locale_settings,
	_pathfinding_settings,
	_script_settings,
	_world_settings,
};

/**
 * List of all the private setting tables.
 */
extern const std::initializer_list<SettingTable> _private_setting_tables{
	_network_private_settings,
};

/**
 * List of all the secrets setting tables.
 */
extern const std::initializer_list<SettingTable> _secrets_setting_tables{
	_network_secrets_settings,
};

/* Begin - Callback Functions for the various settings. */

/** Switch setting title depending on wallclock setting */
static StringID SettingTitleWallclock(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str + 1 : sd.str;
}

/** Switch setting help depending on wallclock setting */
static StringID SettingHelpWallclock(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_help + 1 : sd.str_help;
}

/** Switch setting help depending on wallclock setting */
static StringID SettingHelpWallclockTriple(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_help + ((GetGameSettings().economy.day_length_factor > 1) ? 2 : 1) : sd.str_help;
}

/** Setting values for velocity unit localisation */
static std::pair<StringParameter, StringParameter> SettingsValueVelocityUnit(const IntSettingDesc &, int32_t value)
{
	StringID val;
	switch (value) {
		case 0: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_IMPERIAL; break;
		case 1: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_METRIC; break;
		case 2: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_SI; break;
		case 3: val = (EconTime::UsingWallclockUnits(_game_mode == GM_MENU) || GetGameSettings().economy.day_length_factor > 1) ? STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_GAMEUNITS_SECS : STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_GAMEUNITS_DAYS; break;
		case 4: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_KNOTS; break;
		default: NOT_REACHED();
	}
	return {val, {}};
}

/** A negative value has another string (the one after "strval"). */
static std::pair<StringParameter, StringParameter> SettingsValueAbsolute(const IntSettingDesc &sd, int32_t value)
{
	return {sd.str_val + ((value >= 0) ? 1 : 0), abs(value)};
}

/** Service Interval Settings Default Value displays the correct units or as a percentage */
static std::pair<StringParameter, StringParameter> ServiceIntervalSettingsValueText(const IntSettingDesc &sd, int32_t value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	StringID str;
	if (value == 0) {
		str = sd.str_val + 3;
	} else if (vds->servint_ispercent) {
		str = sd.str_val + 2;
	} else if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		str = sd.str_val + 1;
	} else {
		str = sd.str_val;
	}
	return {str, value};
}

/** Reposition the main toolbar as the setting changed. */
static void v_PositionMainToolbar(int32_t new_value)
{
	if (_game_mode != GM_MENU) PositionMainToolbar(nullptr);
}

/** Reposition the statusbar as the setting changed. */
static void v_PositionStatusbar(int32_t new_value)
{
	if (_game_mode != GM_MENU) {
		PositionStatusbar(nullptr);
		PositionNewsMessage(nullptr);
		PositionNetworkChatWindow(nullptr);
	}
}

/**
 * Redraw the smallmap after a colour scheme change.
 * @param new_value Callback parameter.
 */
static void RedrawSmallmap(int32_t new_value)
{
	BuildLandLegend();
	BuildOwnerLegend();
	SetWindowClassesDirty(WC_SMALLMAP);

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void StationSpreadChanged(int32_t new_value)
{
	InvalidateWindowData(WC_SELECT_STATION, 0);
	InvalidateWindowData(WC_BUILD_STATION, 0);
	InvalidateWindowData(WC_BUS_STATION, 0);
	InvalidateWindowData(WC_TRUCK_STATION, 0);
}

static void UpdateConsists(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		/* Update the consist of all trains so the maximum speed is set correctly. */
		if (t->IsFrontEngine() || t->IsFreeWagon()) {
			t->ConsistChanged(CCF_TRACK);
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}

	extern void AfterLoadTemplateVehiclesUpdateProperties();
	AfterLoadTemplateVehiclesUpdateProperties();

	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);
	SetWindowClassesDirty(WC_CREATE_TEMPLATE);
}

/**
 * Check and update if needed all vehicle service intervals.
 * @param new_value Contains 0 if service intervals are in days, otherwise intervals use percents.
 */
static void UpdateAllServiceInterval(int32_t new_value)
{
	bool update_vehicles;
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
		update_vehicles = false;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
		update_vehicles = true;
	}

	if (new_value != 0) {
		/* Service intervals are in percents. */
		vds->servint_trains   = DEF_SERVINT_PERCENT;
		vds->servint_roadveh  = DEF_SERVINT_PERCENT;
		vds->servint_aircraft = DEF_SERVINT_PERCENT;
		vds->servint_ships    = DEF_SERVINT_PERCENT;
	} else if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		/* Service intervals are in minutes. */
		vds->servint_trains   = DEF_SERVINT_MINUTES_TRAINS;
		vds->servint_roadveh  = DEF_SERVINT_MINUTES_ROADVEH;
		vds->servint_aircraft = DEF_SERVINT_MINUTES_AIRCRAFT;
		vds->servint_ships    = DEF_SERVINT_MINUTES_SHIPS;
	} else {
		/* Service intervals are in days. */
		vds->servint_trains   = DEF_SERVINT_DAYS_TRAINS;
		vds->servint_roadveh  = DEF_SERVINT_DAYS_ROADVEH;
		vds->servint_aircraft = DEF_SERVINT_DAYS_AIRCRAFT;
		vds->servint_ships    = DEF_SERVINT_DAYS_SHIPS;
	}

	if (update_vehicles) {
		const Company *c = Company::Get(_current_company);
		for (Vehicle *v : Vehicle::IterateFrontOnly()) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(CompanyServiceInterval(c, v->type));
				v->SetServiceIntervalIsPercent(new_value != 0);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static bool CanUpdateServiceInterval(VehicleType type, int32_t &new_value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	/* Test if the interval is valid */
	int32_t interval = GetServiceIntervalClamped(new_value, vds->servint_ispercent);
	return interval == new_value;
}

static void UpdateServiceInterval(VehicleType type, int32_t new_value)
{
	if (_game_mode != GM_MENU && Company::IsValidID(_current_company)) {
		for (Vehicle *v : Vehicle::IterateTypeFrontOnly(type)) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(new_value);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * Checks if the service intervals in the settings are specified as percentages and corrects the default value accordingly.
 * @param new_value Contains the service interval's default value in days, or 50 (default in percentage).
 */
static int32_t GetDefaultServiceInterval(const IntSettingDesc &sd, VehicleType type)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	if (vds->servint_ispercent) return DEF_SERVINT_PERCENT;

	if (EconTime::UsingWallclockUnits((_game_mode == GM_MENU))) {
		switch (type) {
			case VEH_TRAIN:    return DEF_SERVINT_MINUTES_TRAINS;
			case VEH_ROAD:     return DEF_SERVINT_MINUTES_ROADVEH;
			case VEH_AIRCRAFT: return DEF_SERVINT_MINUTES_AIRCRAFT;
			case VEH_SHIP:     return DEF_SERVINT_MINUTES_SHIPS;
			default: NOT_REACHED();
		}
	}

	return sd.def;
}

/**
 * Callback for when the player changes the timekeeping units.
 * @param Unused.
 */
static void ChangeTimekeepingUnits(int32_t)
{
	/* If service intervals are in time units (calendar days or real-world minutes), reset them to the correct defaults if not already in a game. */
	if (!_settings_client.company.vehicle.servint_ispercent && _game_mode != GM_NORMAL) {
		UpdateAllServiceInterval(0);
	}

	/* If we are using calendar timekeeping, "minutes per year" must be default. */
	if (_game_mode == GM_MENU && !EconTime::UsingWallclockUnits(true)) {
		_settings_newgame.economy.minutes_per_calendar_year = CalTime::DEF_MINUTES_PER_YEAR;
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);

	/* It is possible to change these units in-game. We must set the economy date appropriately. */
	if (_game_mode != GM_MENU) {
		/* Update effective day length before setting dates, so that the state ticks offset is calculated correctly */
		UpdateEffectiveDayLengthFactor();

		EconTime::Date new_economy_date;
		EconTime::DateFract new_economy_date_fract;

		if (EconTime::UsingWallclockUnits()) {
			/* If the new mode is wallclock units, adjust the economy date to account for different month/year lengths. */
			new_economy_date = EconTime::ConvertYMDToDate(EconTime::CurYear(), EconTime::CurMonth(), Clamp<EconTime::Day>(EconTime::CurDay(), 1, EconTime::DAYS_IN_ECONOMY_WALLCLOCK_MONTH));
			new_economy_date_fract = EconTime::CurDateFract();
		} else {
			/* If the new mode is calendar units, sync the economy date with the calendar date. */
			new_economy_date = ToEconTimeCast(CalTime::CurDate());
			new_economy_date_fract = CalTime::CurDateFract();
			EconTime::Detail::period_display_offset -= EconTime::YearDelta{CalTime::CurYear().base() - EconTime::CurYear().base()};
		}

		/* Update link graphs and vehicles, as these include stored economy dates. */
		LinkGraphSchedule::instance.ShiftDates(new_economy_date - EconTime::CurDate());
		ShiftVehicleDates(new_economy_date - EconTime::CurDate());

		/* Only change the date after changing cached values above. */
		EconTime::Detail::SetDate(new_economy_date, new_economy_date_fract);

		UpdateOrderUIOnDateChange();
	}

	UpdateTimeSettings(0);
	CloseWindowByClass(WC_PAYMENT_RATES);
	CloseWindowByClass(WC_COMPANY_VALUE);
	CloseWindowByClass(WC_PERFORMANCE_HISTORY);
	CloseWindowByClass(WC_DELIVERED_CARGO);
	CloseWindowByClass(WC_OPERATING_PROFIT);
	CloseWindowByClass(WC_INCOME_GRAPH);
	CloseWindowByClass(WC_STATION_CARGO);
	CloseWindowByClass(WC_INDUSTRY_PRODUCTION);
}

/**
 * Callback after the player changes the minutes per year.
 * @param new_value The intended new value of the setting, used for clamping.
 */
static void ChangeMinutesPerYear(int32_t new_value)
{
	/* We don't allow setting Minutes Per Year below default, unless it's to 0 for frozen calendar time. */
	if (new_value < CalTime::DEF_MINUTES_PER_YEAR) {
		int clamped;

		/* If the new value is 1, we're probably at 0 and trying to increase the value, so we should jump up to default. */
		if (new_value == 1) {
			clamped = CalTime::DEF_MINUTES_PER_YEAR;
		} else {
			clamped = CalTime::FROZEN_MINUTES_PER_YEAR;
		}

		/* Override the setting with the clamped value. */
		if (_game_mode == GM_MENU) {
			_settings_newgame.economy.minutes_per_calendar_year = clamped;
		} else {
			_settings_game.economy.minutes_per_calendar_year = clamped;
		}
	}

	UpdateEffectiveDayLengthFactor();
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 1);

	/* If the setting value is not the default, force the game to use wallclock timekeeping units.
	 * This can only happen in the menu, since the pre_cb ensures this setting can only be changed there, or if we're already using wallclock units.
	 */
	if (_game_mode == GM_MENU && (_settings_newgame.economy.minutes_per_calendar_year != CalTime::DEF_MINUTES_PER_YEAR)) {
		if (_settings_newgame.economy.timekeeping_units != TKU_WALLCLOCK) {
			_settings_newgame.economy.timekeeping_units = TKU_WALLCLOCK;
			ChangeTimekeepingUnits(TKU_WALLCLOCK);
		}
	}
}

static std::pair<int32_t, uint32_t> GetServiceIntervalRange(const IntSettingDesc &)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	if (vds->servint_ispercent) return { MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT };

	if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		return { MIN_SERVINT_MINUTES, MAX_SERVINT_MINUTES };
	}

	return { MIN_SERVINT_DAYS, MAX_SERVINT_DAYS };
}

static void TrainAccelerationModelChanged(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		if (t->IsFrontEngine()) {
			t->tcache.cached_max_curve_speed = t->GetCurveSpeedLimit();
			t->UpdateAcceleration();
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}

	extern void AfterLoadTemplateVehiclesUpdateProperties();
	AfterLoadTemplateVehiclesUpdateProperties();

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
	SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);
	SetWindowClassesDirty(WC_CREATE_TEMPLATE);
}

static bool CheckTrainBrakingModelChange(int32_t &new_value)
{
	if (new_value == TBM_REALISTIC && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		for (TileIndex t(0); t < Map::Size(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RailTileType::Signals) {
				uint signals = GetPresentSignals(t);
				if ((signals & 0x3) & ((signals & 0x3) - 1) || (signals & 0xC) & ((signals & 0xC) - 1)) {
					/* Signals in both directions */
					ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED), {}, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
				if (((signals & 0x3) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_LOWER))) ||
						((signals & 0xC) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_UPPER)))) {
					/* Banned signal types present */
					ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED), {}, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
			}
		}
	}

	return true;
}

static void TrainBrakingModelChanged(int32_t new_value)
{
	for (Train *t : Train::Iterate()) {
		if (!t->vehstatus.Test(VehState::Crashed)) {
			t->crash_anim_pos = 0;
		}
		if (t->IsFrontEngine()) {
			t->UpdateAcceleration();
		}
	}
	if (new_value == TBM_REALISTIC && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		for (TileIndex t(0); t < Map::Size(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RailTileType::Signals) {
				TrackBits bits = GetTrackBits(t);
				do {
					Track track = RemoveFirstTrack(&bits);
					if (HasSignalOnTrack(t, track) && GetSignalType(t, track) == SIGTYPE_BLOCK && HasBit(GetRailReservationTrackBits(t), track)) {
						if (EnsureNoTrainOnTrackBits(t, TrackToTrackBits(track)).Succeeded()) {
							UnreserveTrack(t, track);
						}
					}
				} while (bits != TRACK_BIT_NONE);
			}
		}
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: {}", VehicleInfoDumper(v_cur));
		extern bool _long_reserve_disabled;
		_long_reserve_disabled = true;
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || v->vehstatus.Test(VehState::Crashed) || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
		}
		_long_reserve_disabled = false;
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || v->vehstatus.Test(VehState::Crashed) || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
			if (v->lookahead != nullptr) v->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	} else if (new_value == TBM_ORIGINAL && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: {}", VehicleInfoDumper(v_cur));
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || v->vehstatus.Test(VehState::Crashed) || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) {
				v->lookahead.reset();
				continue;
			}
			if (!v->flags.Test(VehicleRailFlag::Stuck)) {
				_settings_game.vehicle.train_braking_model = TBM_REALISTIC;
				FreeTrainTrackReservation(v);
				_settings_game.vehicle.train_braking_model = new_value;
				TryPathReserve(v, true, HasStationTileRail(v->tile));
			} else {
				v->lookahead.reset();
			}
		}
	}

	UpdateExtraAspectsVariable();
	UpdateAllBlockSignals();

	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	MarkWholeScreenDirty();
}

/**
 * This function updates the train acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void TrainSlopeSteepnessChanged(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		if (t->IsFrontEngine()) {
			t->CargoChanged();
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}
}

/**
 * This function updates realistic acceleration caches when the setting "Road vehicle acceleration model" is set.
 * @param new_value Unused new value of setting.
 */
static void RoadVehAccelerationModelChanged(int32_t new_value)
{
	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
			rv->CargoChanged();
		}
	}
	if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL || !_settings_game.vehicle.improved_breakdowns) {
		for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
			rv->breakdown_chance_factor = 128;
		}
	}

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * This function updates the road vehicle acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void RoadVehSlopeSteepnessChanged(int32_t new_value)
{
	for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
		rv->CargoChanged();
	}
}

static void ProgrammableSignalsShownChanged(int32_t new_value)
{
	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
}

static void TownFoundingChanged(int32_t new_value)
{
	if (_game_mode != GM_EDITOR && _settings_game.economy.found_town == TF_FORBIDDEN) {
		CloseWindowById(WC_FOUND_TOWN, 0);
	} else {
		InvalidateWindowData(WC_FOUND_TOWN, 0);
	}
}

static void InvalidateVehTimetableWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE, VIWD_MODIFY_ORDERS);
	InvalidateWindowClassesData(WC_SCHDISPATCH_SLOTS, VIWD_MODIFY_ORDERS);
}

static void ChangeTimetableInTicksMode(int32_t new_value)
{
	SetWindowClassesDirty(WC_VEHICLE_ORDERS);
	InvalidateVehTimetableWindow(new_value);
}

static void UpdateTimeSettings(int32_t new_value)
{
	SetupTimeSettings();
	InvalidateVehTimetableWindow(new_value);
	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_REINIT);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 1);
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	MarkWholeScreenDirty();
}

static void ChangeTimeOverrideMode(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTimeSettings(new_value);
}

static void ZoomMinMaxChanged(int32_t new_value)
{
	extern void ConstrainAllViewportsZoom();
	extern void UpdateFontHeightCache();
	ConstrainAllViewportsZoom();
	GfxClearSpriteCache();
	InvalidateWindowClassesData(WC_SPRITE_ALIGNER);
	if (AdjustGUIZoom(AGZM_MANUAL)) {
		ReInitAllWindows(false);
	}
}

static void SpriteZoomMinChanged(int32_t new_value)
{
	GfxClearSpriteCache();
	/* Force all sprites to redraw at the new chosen zoom level */
	MarkWholeScreenDirty();
}

static void DeveloperModeChanged(int32_t new_value)
{
	DebugReconsiderSendRemoteMessages();
}

static void PlanDisplayModeChanged(int32_t new_value)
{
	InvalidatePlanCaches();
	MarkWholeScreenDirty();
}

/**
 * Update any possible saveload window and delete any newgrf dialogue as
 * its widget parts might change. Reinit all windows as it allows access to the
 * newgrf debug button.
 * @param new_value unused.
 */
static void InvalidateNewGRFChangeWindows(int32_t new_value)
{
	InvalidateWindowClassesData(WC_SAVELOAD);
	CloseWindowByClass(WC_GAME_OPTIONS);
	ReInitAllWindows(false);
}

static void InvalidateCompanyLiveryWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_COMPANY_COLOUR, -1);
	ResetVehicleColourMap();
	MarkWholeScreenDirty();
}

static void ScriptMaxOpsChange(int32_t new_value)
{
	if (_networking && !_network_server) return;

	GameInstance *g = Game::GetInstance();
	if (g != nullptr && !g->IsDead()) {
		g->LimitOpsTillSuspend(new_value);
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			c->ai_instance->LimitOpsTillSuspend(new_value);
		}
	}
}

static bool CheckScriptMaxMemoryChange(int32_t &new_value)
{
	if (_networking && !_network_server) return true;

	size_t limit = static_cast<size_t>(new_value) << 20;

	GameInstance *g = Game::GetInstance();
	if (g != nullptr && !g->IsDead()) {
		if (g->GetAllocatedMemory() > limit) return false;
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			if (c->ai_instance->GetAllocatedMemory() > limit) return false;
		}
	}

	return true;
}

static void ScriptMaxMemoryChange(int32_t new_value)
{
	if (_networking && !_network_server) return;

	size_t limit = static_cast<size_t>(new_value) << 20;

	GameInstance *g = Game::GetInstance();
	if (g != nullptr && !g->IsDead()) {
		g->SetMemoryAllocationLimit(limit);
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			c->ai_instance->SetMemoryAllocationLimit(limit);
		}
	}
}

/**
 * Invalidate the company details window after the shares setting changed.
 * @param new_value Unused.
 * @return Always true.
 */
static void InvalidateCompanyWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_COMPANY);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void EnableSingleVehSharedOrderGuiChanged(int32_t new_value)
{
	for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
		InvalidateWindowClassesData(GetWindowClassForVehicleType(type));
	}
	SetWindowClassesDirty(WC_VEHICLE_TIMETABLE);
	InvalidateWindowClassesData(WC_VEHICLE_ORDERS);
}

static void CheckYapfRailSignalPenalties(int32_t new_value)
{
	extern void YapfCheckRailSignalPenalties();
	YapfCheckRailSignalPenalties();
}

static void ViewportMapShowTunnelModeChanged(int32_t new_value)
{
	extern void ViewportMapBuildTunnelCache();
	ViewportMapBuildTunnelCache();

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void ShowVehicleRouteModeChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	InvalidateWindowClassesData(WC_VEHICLE_VIEW, VIWD_ROUTE_OVERLAY);
	MarkWholeScreenDirty();
}

static void ViewportMapLandscapeModeChanged(int32_t new_value)
{
	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void MarkAllViewportsDirty(int32_t new_value)
{
	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();

	extern void MarkWholeNonMapViewportsDirty();
	MarkWholeNonMapViewportsDirty();
}

static void UpdateLinkgraphColours(int32_t new_value)
{
	BuildLinkStatsLegend();
	MarkWholeScreenDirty();
}

static void ClimateThresholdModeChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GENERATE_LANDSCAPE);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void VelocityUnitsChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	MarkWholeScreenDirty();
}

static void DecimalSeparatorCharChanged(const std::string &new_value)
{
	extern void FillDecimalSeparatorChar();
	FillDecimalSeparatorChar();
	MarkWholeScreenDirty();
}

static void ChangeTrackTypeSortMode(int32_t new_value)
{
	extern void SortRailTypes();
	SortRailTypes();
	MarkWholeScreenDirty();
}

static void TrainSpeedAdaptationChanged(int32_t new_value)
{
	extern void ClearAllSignalSpeedRestrictions();
	ClearAllSignalSpeedRestrictions();
	for (Train *t : Train::Iterate()) {
		t->signal_speed_restriction = 0;
	}
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static void AutosaveModeChanged(int32_t new_value)
{
	extern void ChangeAutosaveFrequency(bool reset);
	ChangeAutosaveFrequency(false);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static bool TownCouncilToleranceAdjust(int32_t &new_value)
{
	if (new_value == 255) new_value = TOWN_COUNCIL_PERMISSIVE;
	return true;
}

static void DifficultyNoiseChange(int32_t new_value)
{
	if (_game_mode == GM_NORMAL) {
		UpdateAirportsNoise();
		if (_settings_game.economy.station_noise_level) {
			InvalidateWindowClassesData(WC_TOWN_VIEW, 0);
		}
	}
}

static void DifficultyMoneyCheatMultiplayerChange(int32_t new_value)
{
	CloseWindowById(WC_CHEATS, 0);
}

static void DifficultyRenameTownsMultiplayerChange(int32_t new_value)
{
	SetWindowClassesDirty(WC_TOWN_VIEW);
}

static void DifficultyOverrideTownSettingsMultiplayerChange(int32_t new_value)
{
	SetWindowClassesDirty(WC_TOWN_AUTHORITY);
}

static void MaxNoAIsChange(int32_t new_value)
{
	if (GetGameSettings().difficulty.max_no_competitors != 0 &&
			AI::GetInfoList()->size() == 0 &&
			!IsNonAdminNetworkClient()) {
		ShowErrorMessage(GetEncodedString(STR_WARNING_NO_SUITABLE_AI), {}, WL_CRITICAL);
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);
}

/**
 * Check whether the road side may be changed.
 * @param new_value unused
 * @return true if the road side may be changed.
 */
static bool CheckRoadSide(int32_t &new_value)
{
	extern bool RoadVehiclesExistOutsideDepots();
	return (_game_mode == GM_MENU || !RoadVehiclesExistOutsideDepots());
}

static void RoadSideChanged(int32_t new_value)
{
	extern void RecalculateRoadCachedOneWayStates();
	RecalculateRoadCachedOneWayStates();
}

/**
 * Conversion callback for _gameopt_settings_game.landscape
 * It converts (or try) between old values and the new ones,
 * without losing initial setting of the user
 * @param value that was read from config file
 * @return the "hopefully" converted value
 */
static std::optional<uint32_t> ConvertLandscape(std::string_view value)
{
	/* try with the old values */
	static constexpr std::initializer_list<const char *> _old_landscape_values{"normal", "hilly", "desert", "candy"};
	return OneOfManySettingDesc::ParseSingleValue(value, _old_landscape_values);
}

static bool CheckFreeformEdges(int32_t &new_value)
{
	if (_game_mode == GM_MENU) return true;
	if (new_value != 0) {
		for (Ship *s : Ship::Iterate()) {
			/* Check if there is a ship on the northern border. */
			if (TileX(s->tile) == 0 || TileY(s->tile) == 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_EMPTY), {}, WL_ERROR);
				return false;
			}
		}
		for (const BaseStation *st : BaseStation::Iterate()) {
			/* Check if there is a non-deleted buoy on the northern border. */
			if (st->IsInUse() && (TileX(st->xy) == 0 || TileY(st->xy) == 0)) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_EMPTY), {}, WL_ERROR);
				return false;
			}
		}
	} else {
		for (uint i = 0; i < Map::MaxX(); i++) {
			if (TileHeight(TileXY(i, 1)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < Map::MaxX(); i++) {
			if (!IsTileType(TileXY(i, Map::MaxY() - 1), MP_WATER) || TileHeight(TileXY(1, Map::MaxY())) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 0; i < Map::MaxY(); i++) {
			if (TileHeight(TileXY(1, i)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < Map::MaxY(); i++) {
			if (!IsTileType(TileXY(Map::MaxX() - 1, i), MP_WATER) || TileHeight(TileXY(Map::MaxX(), i)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
	}
	return true;
}

static void UpdateFreeformEdges(int32_t new_value)
{
	if (_game_mode == GM_MENU) return;

	if (new_value != 0) {
		for (uint x = 0; x < Map::SizeX(); x++) MakeVoid(TileXY(x, 0));
		for (uint y = 0; y < Map::SizeY(); y++) MakeVoid(TileXY(0, y));
	} else {
		/* Make tiles at the border water again. */
		for (uint i = 0; i < Map::MaxX(); i++) {
			SetTileHeight(TileXY(i, 0), 0);
			MakeSea(TileXY(i, 0));
		}
		for (uint i = 0; i < Map::MaxY(); i++) {
			SetTileHeight(TileXY(0, i), 0);
			MakeSea(TileXY(0, i));
		}
	}
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->tile == 0) v->UpdatePosition();
	}
	MarkWholeScreenDirty();
}

bool CheckMapEdgesAreWater(bool allow_non_flat_void)
{
	auto check_tile = [&](uint x, uint y, Slope inner_edge) -> bool {
		int h = 0;
		Slope slope;
		std::tie(slope, h) = GetTilePixelSlopeOutsideMap(x, y);
		if (slope == SLOPE_FLAT && h == 0) return true;
		if (allow_non_flat_void && h == 0 && (slope & inner_edge) == 0 && IsTileType(TileXY(x, y), MP_VOID)) return true;
		return false;
	};
	check_tile(        0,         0, SLOPE_S);
	check_tile(        0, Map::MaxY(), SLOPE_W);
	check_tile(Map::MaxX(),         0, SLOPE_E);
	check_tile(Map::MaxX(), Map::MaxY(), SLOPE_N);

	for (uint x = 1; x < Map::MaxX(); x++) {
		if (!check_tile(x, 0, SLOPE_SE)) return false;
		if (!check_tile(x, Map::MaxY(), SLOPE_NW)) return false;
	}
	for (uint y = 1; y < Map::MaxY(); y++) {
		if (!check_tile(0, y, SLOPE_SW)) return false;
		if (!check_tile(Map::MaxX(), y, SLOPE_NE)) return false;
	}

	return true;
}

static bool CheckMapEdgeMode(int32_t &new_value)
{
	if (_game_mode == GM_MENU || !_settings_game.construction.freeform_edges || new_value == 0) return true;

	if (!CheckMapEdgesAreWater(true)) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
		return false;
	}

	return true;
}

static void MapEdgeModeChanged(int32_t new_value)
{
	MarkAllViewportsDirty(new_value);
	SetWindowClassesDirty(WC_SMALLMAP);

	if (_game_mode == GM_MENU || !_settings_game.construction.freeform_edges || new_value == 0) return;

	for (uint x = 0; x <= Map::MaxX(); x++) {
		SetTileHeight(TileXY(x, 0), 0);
		SetTileHeight(TileXY(x, Map::MaxY()), 0);
	}
	for (uint y = 1; y < Map::MaxY(); y++) {
		SetTileHeight(TileXY(0, y), 0);
		SetTileHeight(TileXY(Map::MaxX(), y), 0);
	}
}

/**
 * Changing the setting "allow multiple NewGRF sets" is not allowed
 * if there are vehicles.
 */
static bool CheckDynamicEngines(int32_t &new_value)
{
	if (_game_mode == GM_MENU) return true;

	if (!EngineOverrideManager::ResetToCurrentNewGRFConfig()) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_DYNAMIC_ENGINES_EXISTING_VEHICLES), {}, WL_ERROR);
		return false;
	}

	return true;
}

static bool CheckMaxHeightLevel(int32_t &new_value)
{
	if (_game_mode == GM_NORMAL) return false;
	if (_game_mode != GM_EDITOR) return true;

	/* Check if at least one mountain on the map is higher than the new value.
	 * If yes, disallow the change. */
	for (TileIndex t(0); t < Map::Size(); t++) {
		if ((int32_t)TileHeight(t) > new_value) {
			ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_TOO_HIGH_MOUNTAIN), {}, WL_ERROR);
			/* Return old, unchanged value */
			return false;
		}
	}

	return true;
}

static void StationCatchmentChanged(int32_t new_value)
{
	Station::RecomputeCatchmentForAll();
	for (Station *st : Station::Iterate()) UpdateStationAcceptance(st, true);
	MarkWholeScreenDirty();
}

static bool CheckSharingRail(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_TRAIN, new_value);
}

static void SharingRailChanged(int32_t new_value)
{
	UpdateAllBlockSignals();
}

static bool CheckSharingRoad(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_ROAD, new_value);
}

static bool CheckSharingWater(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_SHIP, new_value);
}

static bool CheckSharingAir(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_AIRCRAFT, new_value);
}

static void MaxVehiclesChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_BUILD_TOOLBAR);
	MarkWholeScreenDirty();
}

static void ImprovedBreakdownsSettingChanged(int32_t new_value)
{
	if (!_settings_game.vehicle.improved_breakdowns) return;

	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		switch (v->type) {
			case VEH_TRAIN:
				if (v->IsFrontEngine()) {
					v->breakdown_chance_factor = 128;
					Train::From(v)->UpdateAcceleration();
				}
				break;

			case VEH_ROAD:
				if (v->IsFrontEngine()) {
					v->breakdown_chance_factor = 128;
				}
				break;

			default:
				break;
		}
	}
}

static void DayLengthChanged(int32_t new_value)
{
	UpdateEffectiveDayLengthFactor();
	RecalculateStateTicksOffset();

	MarkWholeScreenDirty();
}

static void IndustryEventRateChanged(int32_t new_value)
{
	if (_game_mode != GM_MENU) StartupIndustryDailyChanges(false);
}

static void DefaultAllowTownGrowthChanged(int32_t new_value)
{
	if (_game_mode != GM_MENU) {
		extern void UpdateTownGrowthForAllTowns();
		UpdateTownGrowthForAllTowns();
	}
}

static void TownZoneModeChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTownRadii();
}

static void TownZoneCustomValueChanged(int32_t new_value)
{
	if (_settings_game.economy.town_zone_calc_mode) UpdateTownRadii();
}

static bool CheckTTDPatchSettingFlag(uint flag)
{
	extern bool HasTTDPatchFlagBeenObserved(uint flag);
	if (_networking && HasTTDPatchFlagBeenObserved(flag)) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED), GetEncodedString(STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED_NEWGRF), WL_ERROR);
		return false;
	}

	return true;
}

/**
 * Replace a passwords that are a literal asterisk with an empty string.
 * @param newval The new string value for this password field.
 * @return Always true.
 */
static bool ReplaceAsteriskWithEmptyPassword(std::string &newval)
{
	if (newval == "*") newval.clear();
	return true;
}

static bool IsValidHexKeyString(const std::string &newval)
{
	for (const char c : newval) {
		if (!IsValidChar(c, CS_HEXADECIMAL)) return false;
	}
	return true;
}

static bool IsValidHex128BitKeyString(std::string &newval)
{
	return newval.size() == 32 && IsValidHexKeyString(newval);
}

static bool IsValidHex256BitKeyString(std::string &newval)
{
	return newval.size() == 64 && IsValidHexKeyString(newval);
}

static void ParseCompanyPasswordStorageToken(const std::string &value)
{
	extern std::array<uint8_t, 16> _network_company_password_storage_token;
	if (value.size() != 32) return;
	ConvertHexToBytes(value, _network_company_password_storage_token);
}

static void ParseCompanyPasswordStorageSecret(const std::string &value)
{
	extern std::array<uint8_t, 32> _network_company_password_storage_key;
	if (value.size() != 64) return;
	ConvertHexToBytes(value, _network_company_password_storage_key);
}

/** Update the game info, and send it to the clients when we are running as a server. */
static void UpdateClientConfigValues()
{
	NetworkServerUpdateGameInfo();

	InvalidateWindowData(WC_CLIENT_LIST, 0);

	if (_network_server) {
		NetworkServerSendConfigUpdate();
	}
}

/* End - Callback Functions */

/* Begin - xref conversion callbacks */

static int64_t LinkGraphDistModeXrefChillPP(int64_t val)
{
	return val ^ 2;
}

/* End - xref conversion callbacks */

/* Begin - GUI callbacks */

static bool OrderTownGrowthRate(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DROPDOWN_ORDER: {
			int in = data.val;
			int out;
			if (in == 0) {
				out = 0;
			} else if (in <= 2) {
				out = in - 3;
			} else {
				out = in - 2;
			}
			data.val = out;
			return true;
		}

		default:
			return false;
	}
}

static bool LinkGraphDistributionSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			_temp_special_strings[0] = GetString(STR_CONFIG_SETTING_DISTRIBUTION_HELPTEXT_EXTRA, data.text);
			data.text = SPECSTR_TEMP_START;
			return true;

		default:
			return false;
	}
}

static bool ZoomMaxCfgName(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_CFG_NAME:
			data.str = "gui.zoom_max_extra";
			_fallback_gui_zoom_max = false;
			return true;

		case SOGCT_CFG_FALLBACK_NAME:
			data.str = "zoom_max";
			_fallback_gui_zoom_max = true;
			return true;

		default:
			return false;
	}
}

static bool TreePlacerSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			_temp_special_strings[0] = GetString(STR_CONFIG_SETTING_TREE_PLACER_HELPTEXT_EXTRA, data.text);
			data.text = SPECSTR_TEMP_START;
			return true;

		default:
			return false;
	}
}

static bool DefaultSignalsSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			_temp_special_strings[0] = GetString(STR_CONFIG_SETTING_SHOW_ALL_SIG_DEF_HELPTEXT_EXTRA, data.text);
			data.text = SPECSTR_TEMP_START;
			return true;

		default:
			return false;
	}
}

static bool ChunnelSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			_temp_special_strings[0] = GetString(data.text, 3, 8);
			data.text = SPECSTR_TEMP_START;
			return true;

		default:
			return false;
	}
}

static std::pair<StringParameter, StringParameter> TownCargoScaleValueText(const IntSettingDesc &sd, int32_t value)
{
	StringID str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE;
	if (GetGameSettings().economy.day_length_factor > 1 && GetGameSettings().economy.town_cargo_scale_mode == CSM_DAYLENGTH) {
		str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE_ECON_SPEED_REDUCTION_MULT;
	}
	return {str, value};
}

static std::pair<StringParameter, StringParameter> IndustryCargoScaleValueText(const IntSettingDesc &sd, int32_t value)
{
	StringID str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE;
	if (GetGameSettings().economy.day_length_factor > 1 && GetGameSettings().economy.industry_cargo_scale_mode == CSM_DAYLENGTH) {
		str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE_ECON_SPEED_REDUCTION_MULT;
	}
	return {str, value};
}

static bool IndustryCargoScaleGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			_temp_special_strings[0] = GetString(STR_CONFIG_SETTING_INDUSTRY_CARGO_SCALE_HELPTEXT_EXTRA, data.text);
			data.text = SPECSTR_TEMP_START;
			return true;

		default:
			return false;
	}
}

static std::pair<StringParameter, StringParameter> CalendarModeDisabledValueText(const IntSettingDesc &sd, int32_t value)
{
	return {EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_val : STR_CONFIG_SETTING_DISABLED_TIMEKEEPING_MODE_CALENDAR, value};
}

static bool CalendarModeDisabledGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DISABLE:
			if (!EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) data.val = 1;
			return true;

		default:
			return false;
	}
}

[[maybe_unused]] static std::pair<StringParameter, StringParameter> WallclockModeDisabledDropDownText(const IntSettingDesc &sd, int32_t value)
{
	return {EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? STR_CONFIG_SETTING_DISABLED_TIMEKEEPING_MODE_WALLCLOCK : sd.str_val + value, std::monostate{}};
}

[[maybe_unused]] static bool WallclockModeDisabledGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DISABLE:
			if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) data.val = 1;
			return true;

		default:
			return false;
	}
}

/* End - GUI callbacks */
