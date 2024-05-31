/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_gui.cpp %File for dealing with rail construction user interface */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "waypoint_func.h"
#include "newgrf_station.h"
#include "company_base.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "tunnelbridge.h"
#include "tilehighlight_func.h"
#include "spritecache.h"
#include "core/geometry_func.hpp"
#include "hotkeys.h"
#include "engine_base.h"
#include "vehicle_func.h"
#include "zoom_func.h"
#include "rail_gui.h"
#include "querystring_gui.h"
#include "sortlist_type.h"
#include "stringfilter_type.h"
#include "string_func.h"
#include "tracerestrict.h"
#include "programmable_signals.h"
#include "newgrf_newsignals.h"
#include "core/backup_type.hpp"

#include "station_map.h"
#include "tunnelbridge_map.h"

#include "widgets/rail_widget.h"

#include "safeguards.h"


static RailType _cur_railtype;               ///< Rail type of the current build-rail toolbar.
static bool _remove_button_clicked;          ///< Flag whether 'remove' toggle-button is currently enabled
static DiagDirection _build_depot_direction; ///< Currently selected depot direction
static uint16_t _cur_waypoint_type;          ///< Currently selected waypoint type
static bool _convert_signal_button;          ///< convert signal button in the signal GUI pressed
static bool _trace_restrict_button;          ///< trace restrict button in the signal GUI pressed
static bool _program_signal_button;          ///< program signal button in the signal GUI pressed
static SignalVariant _cur_signal_variant;    ///< set the signal variant (for signal GUI)
static SignalType _cur_signal_type;          ///< set the signal type (for signal GUI)
static uint8_t _cur_signal_style;            ///< set the signal style (for signal GUI)
static uint _cur_signal_button;              ///< set the signal button (for signal GUI)

extern TileIndex _rail_track_endtile; // rail_cmd.cpp

static const int HOTKEY_POLYRAIL     = 0x1000;
static const int HOTKEY_NEW_POLYRAIL = 0x1001;

struct RailStationGUISettings {
	Axis orientation;                 ///< Currently selected rail station orientation

	bool newstations;                 ///< Are custom station definitions available?
	StationClassID station_class;     ///< Currently selected custom station class (if newstations is \c true )
	uint16_t station_type;            ///< %Station type within the currently selected custom station class (if newstations is \c true )
	uint16_t station_count;           ///< Number of custom stations (if newstations is \c true )
};
static RailStationGUISettings _railstation; ///< Settings of the station builder GUI


static void HandleStationPlacement(TileIndex start, TileIndex end);
static void ShowBuildTrainDepotPicker(Window *parent);
static void ShowBuildWaypointPicker(Window *parent);
static Window *ShowStationBuilder(Window *parent);
static void ShowSignalBuilder(Window *parent);

/**
 * Check whether a station type can be build.
 * @return true if building is allowed.
 */
static bool IsStationAvailable(const StationSpec *statspec)
{
	if (statspec == nullptr || !HasBit(statspec->callback_mask, CBM_STATION_AVAIL)) return true;

	uint16_t cb_res = GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, nullptr, INVALID_TILE, _cur_railtype);
	if (cb_res == CALLBACK_FAILED) return true;

	return Convert8bitBooleanCallback(statspec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res);
}

void CcPlaySound_CONSTRUCTION_RAIL(const CommandCost &result, TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd)
{
	if (result.Succeeded() && _settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
}

static CommandContainer GenericPlaceRailCmd(TileIndex tile, Track track)
{
	CommandContainer ret = NewCommandContainerBasic(
		tile,          // tile
		_cur_railtype, // p1
		track | (_settings_client.gui.auto_remove_signals << 3), // p2
		(uint32_t) (_remove_button_clicked ?
				CMD_REMOVE_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
				CMD_BUILD_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK)), // cmd
		CcPlaySound_CONSTRUCTION_RAIL // callback
	);

	return ret;
}

/**
 * Try to add an additional rail-track at the entrance of a depot
 * @param tile  Tile to use for adding the rail-track
 * @param dir   Direction to check for already present tracks
 * @param track Track to add
 * @see CcRailDepot()
 */
static void PlaceExtraDepotRail(TileIndex tile, DiagDirection dir, Track track)
{
	if (GetRailTileType(tile) == RAIL_TILE_DEPOT) return;
	if (GetRailTileType(tile) == RAIL_TILE_SIGNALS && !_settings_client.gui.auto_remove_signals) return;
	if ((GetTrackBits(tile) & DiagdirReachesTracks(dir)) == 0) return;

	DoCommandP(tile, _cur_railtype, track | (_settings_client.gui.auto_remove_signals << 3), CMD_BUILD_SINGLE_RAIL);
}

/** Additional pieces of track to add at the entrance of a depot. */
static const Track _place_depot_extra_track[12] = {
	TRACK_LEFT,  TRACK_UPPER, TRACK_UPPER, TRACK_RIGHT, // First additional track for directions 0..3
	TRACK_X,     TRACK_Y,     TRACK_X,     TRACK_Y,     // Second additional track
	TRACK_LOWER, TRACK_LEFT,  TRACK_RIGHT, TRACK_LOWER, // Third additional track
};

/** Direction to check for existing track pieces. */
static const DiagDirection _place_depot_extra_dir[12] = {
	DIAGDIR_SE, DIAGDIR_SW, DIAGDIR_SE, DIAGDIR_SW,
	DIAGDIR_SW, DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_SE,
	DIAGDIR_NW, DIAGDIR_NE, DIAGDIR_NW, DIAGDIR_NE,
};

void CcRailDepot(const CommandCost &result, TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)p2;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	tile += TileOffsByDiagDir(dir);

	if (IsTileType(tile, MP_RAILWAY)) {
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir], _place_depot_extra_track[dir]);
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir + 4], _place_depot_extra_track[dir + 4]);
		PlaceExtraDepotRail(tile, _place_depot_extra_dir[dir + 8], _place_depot_extra_track[dir + 8]);
	}
}

/**
 * Place a rail waypoint.
 * @param tile Position to start dragging a waypoint.
 */
static void PlaceRail_Waypoint(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_STATION);
		return;
	}

	Axis axis = GetAxisForNewWaypoint(tile);
	if (IsValidAxis(axis)) {
		/* Valid tile for waypoints */
		VpStartPlaceSizing(tile, axis == AXIS_X ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_STATION);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		/* Tile where we can't build rail waypoints. This is always going to fail,
		 * but provides the user with a proper error message. */
		DoCommandP(tile, 1 << 8 | 1 << 16, STAT_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT));
	}
}

void CcStation(const CommandCost &result, TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd)
{
	if (result.Failed()) return;

	if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
	/* Only close the station builder window if the default station and non persistent building is chosen. */
	if (_railstation.station_class == STAT_CLASS_DFLT && _railstation.station_type == 0 && !_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

/**
 * Place a rail station.
 * @param tile Position to place or start dragging a station.
 */
static void PlaceRail_Station(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_REMOVE_STATION);
		VpSetPlaceSizingLimit(-1);
	} else if (_settings_client.gui.station_dragdrop) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_STATION);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		uint32_t p1 = _cur_railtype | _railstation.orientation << 6 | _settings_client.gui.station_numtracks << 8 | _settings_client.gui.station_platlength << 16 | _ctrl_pressed << 24;
		uint32_t p2 = _railstation.station_class | INVALID_STATION << 16;
		uint64_t p3 = _railstation.station_type;

		int w = _settings_client.gui.station_numtracks;
		int h = _settings_client.gui.station_platlength;
		if (!_railstation.orientation) Swap(w, h);

		CommandContainer cmdcont = NewCommandContainerBasic(tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation);
		cmdcont.p3 = p3;
		ShowSelectStationIfNeeded(cmdcont, TileArea(tile, w, h));
	}
}

static SignalType GetDefaultSignalType()
{
	SignalType sigtype = _settings_client.gui.default_signal_type;
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsSignalTypeUnsuitableForRealisticBraking(sigtype)) return SIGTYPE_PBS_ONEWAY;
	return sigtype;
}

/**
 * Build a new signal or edit/remove a present signal, use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp
 *
 * @param tile The tile where the signal will build or edit
 */
static void GenericPlaceSignals(TileIndex tile)
{
	TrackBits trackbits = TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0));

	if (trackbits & TRACK_BIT_VERT) { // N-S direction
		trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
	}

	if (trackbits & TRACK_BIT_HORZ) { // E-W direction
		trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
	}

	Track track = FindFirstTrack(trackbits);

	if (_remove_button_clicked) {
		DoCommandP(tile, track, 0, CMD_REMOVE_SIGNALS | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM), CcPlaySound_CONSTRUCTION_RAIL);
		return;
	}

	if (_trace_restrict_button) {
		if (IsPlainRailTile(tile) && HasTrack(tile, track) && HasSignalOnTrack(tile, track)) {
			ShowTraceRestrictProgramWindow(tile, track);
		}
		if (IsTunnelBridgeWithSignalSimulation(tile) && HasTrack(GetAcrossTunnelBridgeTrackBits(tile), track)) {
			ShowTraceRestrictProgramWindow(tile, track);
		}
		return;
	}

	if (_program_signal_button) {
		if (IsPlainRailTile(tile) && HasTrack(tile, track) && HasSignalOnTrack(tile,track) && IsPresignalProgrammable(tile, track)) {
			// Show program gui if there is a programmable pre-signal
			ShowSignalProgramWindow(SignalReference(tile, track));
			return;
		}

		// Don't display error here even though program-button is pressed and there is no programmable pre-signal,
		// instead just handle it normally. That way player can keep the program-button pressed all the time
		// to build slightly faster.
	}

	const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

	/* various bitstuffed elements for CmdBuildSingleSignal() */
	uint32_t p1 = track;

	/* Which signals should we cycle through? */
	SignalCycleGroups cycle_types;
	if (_settings_client.gui.cycle_signal_types == SIGNAL_CYCLE_PATH) {
		cycle_types = SCG_PBS;
	} else if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		cycle_types = SCG_BLOCK | SCG_PBS;
	} else if (_settings_client.gui.cycle_signal_types == SIGNAL_CYCLE_ALL) {
		cycle_types = SCG_PBS;
		if (_settings_client.gui.signal_gui_mode == SIGNAL_GUI_ALL) cycle_types |= SCG_BLOCK;
	} else {
		cycle_types = SCG_CURRENT_GROUP;
	}

	if (w != nullptr) {
		/* signal GUI is used */
		SB(p1, 3, 1, _ctrl_pressed);
		SB(p1, 4, 1, _cur_signal_variant);
		SB(p1, 5, 3, _cur_signal_type);
		SB(p1, 8, 1, _convert_signal_button);
		SB(p1, 9, 2, cycle_types);
		SB(p1, 19, 4, _cur_signal_style);
		if (_cur_signal_type == SIGTYPE_NO_ENTRY) SB(p1, 15, 2, 1); // reverse default signal direction
	} else {
		SB(p1, 3, 1, _ctrl_pressed);
		SB(p1, 4, 1, (CalTime::CurYear() < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
		SB(p1, 5, 3, GetDefaultSignalType());
		SB(p1, 8, 1, 0);
		SB(p1, 9, 2, cycle_types);
	}
	SB(p1, 18, 1, _settings_client.gui.adv_sig_bridge_tun_modes);
	SB(p1, 23, 5, Clamp<int>(_settings_client.gui.drag_signals_density, 1, 16));

	DoCommandP(tile, p1, 0, CMD_BUILD_SIGNALS |
			CMD_MSG((w != nullptr && _convert_signal_button) ? STR_ERROR_SIGNAL_CAN_T_CONVERT_SIGNALS_HERE : STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
			CcPlaySound_CONSTRUCTION_RAIL);
}

/**
 * Start placing a rail bridge.
 * @param tile Position of the first tile of the bridge.
 * @param w    Rail toolbar window.
 */
static void PlaceRail_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = {0, 0};
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	} else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/** Command callback for building a tunnel */
void CcBuildRailTunnel(const CommandCost &result, TileIndex tile, uint32_t p1, uint32_t p2, uint64_t p3, uint32_t cmd)
{
	if (result.Succeeded()) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_20_CONSTRUCTION_RAIL, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
		StoreRailPlacementEndpoints(tile, _build_tunnel_endtile, TileX(tile) == TileX(_build_tunnel_endtile) ? TRACK_Y : TRACK_X, false);
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/**
 * Toggles state of the Remove button of Build rail toolbar
 * @param w window the button belongs to
 */
static void ToggleRailButton_Remove(Window *w)
{
	CloseWindowById(WC_SELECT_STATION, 0);
	w->ToggleWidgetLoweredState(WID_RAT_REMOVE);
	w->SetWidgetDirty(WID_RAT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_RAT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
	if (_remove_button_clicked && _trace_restrict_button) {
		_trace_restrict_button = false;
		InvalidateWindowData(WC_BUILD_SIGNAL, 0);
	}
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RailToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_RAT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (WidgetID i = WID_RAT_BUILD_NS; i <= WID_RAT_BUILD_STATION; i++) {
		if ((i <= WID_RAT_POLYRAIL || i >= WID_RAT_BUILD_WAYPOINT) && w->IsWidgetLowered(i)) {
			ToggleRailButton_Remove(w);
			return true;
		}
	}

	return false;
}


/**
 * The "remove"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbarWindow::OnClick()
 */
static void BuildRailClick_Remove(Window *w)
{
	if (w->IsWidgetDisabled(WID_RAT_REMOVE)) return;
	ToggleRailButton_Remove(w);
	if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);

	/* handle station builder */
	if (w->IsWidgetLowered(WID_RAT_BUILD_STATION)) {
		if (_remove_button_clicked) {
			/* starting drag & drop remove */
			if (!_settings_client.gui.station_dragdrop) {
				SetTileSelectSize(1, 1);
			} else {
				VpSetPlaceSizingLimit(-1);
			}
		} else {
			/* starting station build mode */
			if (!_settings_client.gui.station_dragdrop) {
				int x = _settings_client.gui.station_numtracks;
				int y = _settings_client.gui.station_platlength;
				if (_railstation.orientation == 0) Swap(x, y);
				SetTileSelectSize(x, y);
			} else {
				VpSetPlaceSizingLimit(_settings_game.station.station_spread);
			}
		}
	}
}

static CommandContainer DoRailroadTrackCmd(TileIndex start_tile, TileIndex end_tile, Track track)
{
	CommandContainer ret = NewCommandContainerBasic(
		start_tile,                   // tile
		end_tile,                     // p1
		(uint32_t) (_cur_railtype | (track << 6) | (_settings_client.gui.auto_remove_signals << 13)), // p2
		(uint32_t) (_remove_button_clicked ?
				CMD_REMOVE_RAILROAD_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
				CMD_BUILD_RAILROAD_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK)), // cmd
		CcPlaySound_CONSTRUCTION_RAIL       // callback
	);

	return ret;
}

static void HandleAutodirPlacement()
{
	Track track = (Track)(_thd.drawstyle & HT_DIR_MASK); // 0..5
	TileIndex start_tile = TileVirtXY(_thd.selstart.x, _thd.selstart.y);
	TileIndex end_tile = TileVirtXY(_thd.selend.x, _thd.selend.y);

	CommandContainer cmd = (_thd.drawstyle & HT_RAIL) ?
			GenericPlaceRailCmd(end_tile, track) : // one tile case
			DoRailroadTrackCmd(start_tile, end_tile, track); // multitile selection

	/* When overbuilding existing tracks in polyline mode we just want to move the
	 * snap point without altering the user with the "already built" error. Don't
	 * execute the command right away, firstly check if tracks are being overbuilt. */
	if (!(_thd.place_mode & HT_POLY) || _shift_pressed ||
			DoCommand(&cmd, DC_AUTO | DC_NO_WATER).GetErrorMessage() != STR_ERROR_ALREADY_BUILT) {
		/* place tracks */
		if (!DoCommandP(&cmd)) return;
	}

	/* save new snap points for the polyline tool */
	if (!_shift_pressed && _rail_track_endtile != INVALID_TILE) {
		StoreRailPlacementEndpoints(start_tile, _rail_track_endtile, track, true);
	}
}

/**
 * Build new signals or remove signals or (if only one tile marked) edit a signal.
 *
 * If one tile marked abort and use GenericPlaceSignals()
 * else use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp to build many signals
 */
static void HandleAutoSignalPlacement()
{
	uint32_t p2 = GB(_thd.drawstyle, 0, 3); // 0..5
	uint64_t p3 = 0;

	if ((_thd.drawstyle & HT_DRAG_MASK) == HT_RECT) { // one tile case
		GenericPlaceSignals(TileVirtXY(_thd.selend.x, _thd.selend.y));
		return;
	}

	const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

	if (w != nullptr) {
		/* signal GUI is used */
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, _cur_signal_variant);
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, _cur_signal_type);
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
		SB(p2, 10, 1, !_settings_client.gui.drag_signals_fixed_distance);
		SB(p2, 11, 4, _cur_signal_style);
	} else {
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, (CalTime::CurYear() < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, GetDefaultSignalType());
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
		SB(p2, 10, 1, !_settings_client.gui.drag_signals_fixed_distance);
	}
	SB(p3, 0, 1, _settings_client.gui.drag_signals_skip_stations);

	/* _settings_client.gui.drag_signals_density is given as a parameter such that each user
	 * in a network game can specify their own signal density */
	DoCommandPEx(TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), p2, p3,
			_remove_button_clicked ?
			CMD_REMOVE_SIGNAL_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM) :
			CMD_BUILD_SIGNAL_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
			CcPlaySound_CONSTRUCTION_RAIL);
}


/** Rail toolbar management class. */
struct BuildRailToolbarWindow : Window {
	RailType railtype;    ///< Rail type to build.
	int last_user_action; ///< Last started user action.

	BuildRailToolbarWindow(WindowDesc *desc, RailType railtype) : Window(desc)
	{
		this->CreateNestedTree();
		if (!_settings_client.gui.show_rail_polyline_tool) {
			this->GetWidget<NWidgetStacked>(WID_RAT_POLYRAIL_SEL)->SetDisplayedPlane(SZSP_NONE);
		}
		this->FinishInitNested(TRANSPORT_RAIL);
		this->SetupRailToolbar(railtype);
		this->DisableWidget(WID_RAT_REMOVE);
		this->OnInvalidateData();
		this->last_user_action = INVALID_WID_RAT;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) SetViewportCatchmentStation(nullptr, true);
		if (this->IsWidgetLowered(WID_RAT_BUILD_WAYPOINT)) SetViewportCatchmentWaypoint(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WC_SCEN_LAND_GEN, 0, false);
		CloseWindowById(WC_SELECT_STATION, 0);
		this->Window::Close();
	}

	/** List of widgets to be disabled if infrastructure limit prevents building. */
	static inline const std::initializer_list<WidgetID> can_build_widgets = {
		WID_RAT_BUILD_NS, WID_RAT_BUILD_X, WID_RAT_BUILD_EW, WID_RAT_BUILD_Y, WID_RAT_AUTORAIL,
		WID_RAT_BUILD_DEPOT, WID_RAT_BUILD_WAYPOINT, WID_RAT_BUILD_STATION, WID_RAT_BUILD_SIGNALS,
		WID_RAT_BUILD_BRIDGE, WID_RAT_BUILD_TUNNEL, WID_RAT_CONVERT_RAIL,
	};

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		if (this->GetWidget<NWidgetStacked>(WID_RAT_POLYRAIL_SEL)->SetDisplayedPlane(_settings_client.gui.show_rail_polyline_tool ? 0 : SZSP_NONE)) {
			if (this->IsWidgetLowered(WID_RAT_POLYRAIL)) {
				ResetObjectToPlace();
			}
			this->ReInit();
		}

		bool can_build = CanBuildVehicleInfrastructure(VEH_TRAIN);
		for (const WidgetID widget : can_build_widgets) this->SetWidgetDisabledState(widget, !can_build);
		if (!can_build) {
			CloseWindowById(WC_BUILD_SIGNAL, TRANSPORT_RAIL);
			CloseWindowById(WC_BUILD_STATION, TRANSPORT_RAIL);
			CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_RAIL);
			CloseWindowById(WC_BUILD_WAYPOINT, TRANSPORT_RAIL);
			CloseWindowById(WC_SELECT_STATION, 0);
		}
	}

	bool OnTooltip([[maybe_unused]] Point pt, WidgetID widget, TooltipCloseCondition close_cond) override
	{
		bool can_build = CanBuildVehicleInfrastructure(VEH_TRAIN);
		if (can_build) {
			if (widget == WID_RAT_CONVERT_RAIL) {
				SetDParam(0, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL);
				GuiShowTooltips(this, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL_EXTRA, close_cond, 1);
				return true;
			}
			return false;
		}

		if (std::find(std::begin(can_build_widgets), std::end(can_build_widgets), widget) == std::end(can_build_widgets)) return false;

		GuiShowTooltips(this, STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE, close_cond);
		return true;
	}

	/**
	 * Configures the rail toolbar for railtype given
	 * @param railtype the railtype to display
	 */
	void SetupRailToolbar(RailType railtype)
	{
		this->railtype = railtype;
		const RailTypeInfo *rti = GetRailTypeInfo(railtype);

		assert(railtype < RAILTYPE_END);
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_NS)->widget_data     = rti->gui_sprites.build_ns_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_X)->widget_data      = rti->gui_sprites.build_x_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_EW)->widget_data     = rti->gui_sprites.build_ew_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_Y)->widget_data      = rti->gui_sprites.build_y_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_AUTORAIL)->widget_data     = rti->gui_sprites.auto_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_POLYRAIL)->widget_data     = rti->gui_sprites.auto_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_DEPOT)->widget_data  = rti->gui_sprites.build_depot;
		this->GetWidget<NWidgetCore>(WID_RAT_CONVERT_RAIL)->widget_data = rti->gui_sprites.convert_rail;
		this->GetWidget<NWidgetCore>(WID_RAT_BUILD_TUNNEL)->widget_data = rti->gui_sprites.build_tunnel;
	}

	/**
	 * Switch to another rail type.
	 * @param railtype New rail type.
	 */
	void ModifyRailType(RailType railtype)
	{
		this->SetupRailToolbar(railtype);
		this->ReInit();
	}

	void UpdateRemoveWidgetStatus(WidgetID clicked_widget)
	{
		switch (clicked_widget) {
			case WID_RAT_REMOVE:
				/* If it is the removal button that has been clicked, do nothing,
				 * as it is up to the other buttons to drive removal status */
				return;

			case WID_RAT_BUILD_NS:
			case WID_RAT_BUILD_X:
			case WID_RAT_BUILD_EW:
			case WID_RAT_BUILD_Y:
			case WID_RAT_AUTORAIL:
			case WID_RAT_POLYRAIL:
			case WID_RAT_BUILD_WAYPOINT:
			case WID_RAT_BUILD_STATION:
			case WID_RAT_BUILD_SIGNALS:
				/* Removal button is enabled only if the rail/signal/waypoint/station
				 * button is still lowered.  Once raised, it has to be disabled */
				this->SetWidgetDisabledState(WID_RAT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			default:
				/* When any other buttons than rail/signal/waypoint/station, raise and
				 * disable the removal button */
				this->DisableWidget(WID_RAT_REMOVE);
				this->RaiseWidget(WID_RAT_REMOVE);
				break;
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_RAT_CAPTION) {
			const RailTypeInfo *rti = GetRailTypeInfo(this->railtype);
			if (rti->max_speed > 0) {
				SetDParam(0, STR_TOOLBAR_RAILTYPE_VELOCITY);
				SetDParam(1, rti->strings.toolbar_caption);
				SetDParam(2, PackVelocity(rti->max_speed, VEH_TRAIN));
			} else {
				SetDParam(0, rti->strings.toolbar_caption);
			}
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_RAT_POLYRAIL) {
			Dimension d = GetSpriteSize(SPR_BLOT);
			uint offset = this->IsWidgetLowered(WID_RAT_POLYRAIL) ? 1 : 0;
			DrawSprite(SPR_BLOT, PALETTE_TO_GREY, (r.left + r.right - d.width) / 2 + offset, (r.top + r.bottom - d.height) / 2 + offset);
		}
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget < WID_RAT_BUILD_NS) return;

		_remove_button_clicked = false;
		switch (widget) {
			case WID_RAT_BUILD_NS:
				HandlePlacePushButton(this, WID_RAT_BUILD_NS, GetRailTypeInfo(_cur_railtype)->cursor.rail_ns, HT_LINE | HT_DIR_VL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_X:
				HandlePlacePushButton(this, WID_RAT_BUILD_X, GetRailTypeInfo(_cur_railtype)->cursor.rail_swne, HT_LINE | HT_DIR_X);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_EW:
				HandlePlacePushButton(this, WID_RAT_BUILD_EW, GetRailTypeInfo(_cur_railtype)->cursor.rail_ew, HT_LINE | HT_DIR_HL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_Y:
				HandlePlacePushButton(this, WID_RAT_BUILD_Y, GetRailTypeInfo(_cur_railtype)->cursor.rail_nwse, HT_LINE | HT_DIR_Y);
				this->last_user_action = widget;
				break;

			case WID_RAT_AUTORAIL:
				HandlePlacePushButton(this, WID_RAT_AUTORAIL, GetRailTypeInfo(_cur_railtype)->cursor.autorail, HT_RAIL);
				this->last_user_action = widget;
				break;

			case WID_RAT_POLYRAIL: {
				if (!_settings_client.gui.show_rail_polyline_tool) break;
				bool was_snap = CurrentlySnappingRailPlacement();
				bool was_open = this->IsWidgetLowered(WID_RAT_POLYRAIL);
				bool do_snap;
				bool do_open;
				/* "polyrail" hotkey     - activate polyline tool in snapping mode, close the tool if snapping mode is already active
				 * "new_polyrail" hotkey - activate polyline tool in non-snapping (new line) mode, close the tool if non-snapping mode is already active
				 * button ctrl-clicking  - switch between snapping and non-snapping modes, open the tool in non-snapping mode if it is closed
				 * button clicking       - open the tool in non-snapping mode, close the tool if it is opened */
				if (this->last_user_action == HOTKEY_POLYRAIL) {
					do_snap = true;
					do_open = !was_open || !was_snap;
				} else if (this->last_user_action == HOTKEY_NEW_POLYRAIL) {
					do_snap = false;
					do_open = !was_open || was_snap;
				} else if (_ctrl_pressed) {
					do_snap = !was_open || !was_snap;
					do_open = true;
				} else {
					do_snap = false;
					do_open = !was_open;
				}
				/* close the tool explicitly so it can be re-opened in different snapping mode */
				if (was_open) ResetObjectToPlace();
				/* open the tool in desired mode */
				if (do_open && HandlePlacePushButton(this, WID_RAT_POLYRAIL, GetRailTypeInfo(railtype)->cursor.autorail, do_snap ? (HT_RAIL | HT_POLY) : (HT_RAIL | HT_NEW_POLY))) {
					/* if we are re-opening the tool but we couldn't switch the snapping
					 * then close the tool instead of appearing to be doing nothing */
					if (was_open && do_snap != CurrentlySnappingRailPlacement()) ResetObjectToPlace();
				}
				this->last_user_action = WID_RAT_POLYRAIL;
				break;
			}

			case WID_RAT_DEMOLISH:
				HandlePlacePushButton(this, WID_RAT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_DEPOT:
				if (HandlePlacePushButton(this, WID_RAT_BUILD_DEPOT, GetRailTypeInfo(_cur_railtype)->cursor.depot, HT_RECT)) {
					ShowBuildTrainDepotPicker(this);
					this->last_user_action = widget;
				}
				break;

			case WID_RAT_BUILD_WAYPOINT:
				this->last_user_action = widget;
				if (HandlePlacePushButton(this, WID_RAT_BUILD_WAYPOINT, SPR_CURSOR_WAYPOINT, HT_RECT)) {
					if (StationClass::Get(STAT_CLASS_WAYP)->GetSpecCount() > 1) {
						ShowBuildWaypointPicker(this);
					} else {
						_cur_waypoint_type = 0;
					}
				}
				break;

			case WID_RAT_BUILD_STATION:
				if (HandlePlacePushButton(this, WID_RAT_BUILD_STATION, SPR_CURSOR_RAIL_STATION, HT_RECT)) {
					ShowStationBuilder(this);
					this->last_user_action = widget;
				}
				break;

			case WID_RAT_BUILD_SIGNALS: {
				this->last_user_action = widget;
				bool started = HandlePlacePushButton(this, WID_RAT_BUILD_SIGNALS, ANIMCURSOR_BUILDSIGNALS, HT_RECT);
				if (started != _ctrl_pressed) {
					ShowSignalBuilder(this);
				}
				break;
			}

			case WID_RAT_BUILD_BRIDGE:
				HandlePlacePushButton(this, WID_RAT_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT);
				this->last_user_action = widget;
				break;

			case WID_RAT_BUILD_TUNNEL:
				HandlePlacePushButton(this, WID_RAT_BUILD_TUNNEL, GetRailTypeInfo(_cur_railtype)->cursor.tunnel, HT_SPECIAL | HT_TUNNEL);
				this->last_user_action = widget;
				break;

			case WID_RAT_REMOVE:
				BuildRailClick_Remove(this);
				break;

			case WID_RAT_CONVERT_RAIL: {
				bool active = HandlePlacePushButton(this, WID_RAT_CONVERT_RAIL, GetRailTypeInfo(_cur_railtype)->cursor.convert, _ctrl_pressed ? HT_RAIL : HT_RECT | HT_DIAGONAL);
				if (active && _ctrl_pressed) _thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_GREEN;
				this->last_user_action = widget;
				break;
			}

			default: NOT_REACHED();
		}
		this->UpdateRemoveWidgetStatus(widget);
		if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
	}

	EventState OnHotkey(int hotkey) override
	{
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection

		switch (hotkey) {
			case HOTKEY_POLYRAIL:
			case HOTKEY_NEW_POLYRAIL:
				if (!_settings_client.gui.show_rail_polyline_tool) return ES_HANDLED;
				/* Indicate to the OnClick that the action comes from a hotkey rather
				 * then from a click and that the CTRL state should be ignored. */
				this->last_user_action = hotkey;
				hotkey = WID_RAT_POLYRAIL;
				return this->Window::OnHotkey(hotkey);

			case WID_RAT_CONVERT_RAIL: {
				HandlePlacePushButton(this, WID_RAT_CONVERT_RAIL, GetRailTypeInfo(_cur_railtype)->cursor.convert, HT_RECT | HT_DIAGONAL);
				this->last_user_action = WID_RAT_CONVERT_RAIL;
				this->UpdateRemoveWidgetStatus(WID_RAT_CONVERT_RAIL);
				if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
				return ES_HANDLED;
			}

			case WID_RAT_CONVERT_RAIL_TRACK: {
				bool active = HandlePlacePushButton(this, WID_RAT_CONVERT_RAIL, GetRailTypeInfo(_cur_railtype)->cursor.convert, HT_RAIL);
				if (active) _thd.square_palette = SPR_ZONING_INNER_HIGHLIGHT_GREEN;
				this->last_user_action = WID_RAT_CONVERT_RAIL;
				this->UpdateRemoveWidgetStatus(WID_RAT_CONVERT_RAIL);
				if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
				return ES_HANDLED;
			}

			default:
				return this->Window::OnHotkey(hotkey);
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		switch (this->last_user_action) {
			case WID_RAT_BUILD_NS:
				VpStartPlaceSizing(tile, VPM_FIX_VERTICAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_X:
				VpStartPlaceSizing(tile, VPM_FIX_Y | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_EW:
				VpStartPlaceSizing(tile, VPM_FIX_HORIZONTAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_BUILD_Y:
				VpStartPlaceSizing(tile, VPM_FIX_X | VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_AUTORAIL:
			case WID_RAT_POLYRAIL:
				VpStartPlaceSizing(tile, VPM_RAILDIRS, DDSP_PLACE_RAIL);
				break;

			case WID_RAT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			case WID_RAT_BUILD_DEPOT:
				DoCommandP(tile, _cur_railtype, _build_depot_direction,
						CMD_BUILD_TRAIN_DEPOT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_DEPOT),
						CcRailDepot);
				break;

			case WID_RAT_BUILD_WAYPOINT:
				PlaceRail_Waypoint(tile);
				break;

			case WID_RAT_BUILD_STATION:
				PlaceRail_Station(tile);
				break;

			case WID_RAT_BUILD_SIGNALS:
				VpStartPlaceSizing(tile, VPM_SIGNALDIRS, DDSP_BUILD_SIGNALS);
				break;

			case WID_RAT_BUILD_BRIDGE:
				PlaceRail_Bridge(tile, this);
				break;

			case WID_RAT_BUILD_TUNNEL:
				DoCommandP(tile, _cur_railtype | (TRANSPORT_RAIL << 8), 0, CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRailTunnel);
				break;

			case WID_RAT_CONVERT_RAIL:
				if (_thd.place_mode & HT_RAIL) {
					VpStartPlaceSizing(tile, VPM_RAILDIRS, DDSP_CONVERT_RAIL_TRACK);
				} else {
					VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_RAIL);
				}
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
	{
		/* no dragging if you have pressed the convert button */
		if (FindWindowById(WC_BUILD_SIGNAL, 0) != nullptr && _convert_signal_button && this->IsWidgetLowered(WID_RAT_BUILD_SIGNALS)) return;

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_RAIL, _cur_railtype);
					break;

				case DDSP_PLACE_RAIL:
					HandleAutodirPlacement();
					break;

				case DDSP_BUILD_SIGNALS:
					HandleAutoSignalPlacement();
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_CONVERT_RAIL:
					DoCommandP(end_tile, start_tile, _cur_railtype | (_ctrl_pressed ? (1 << 6) : 0), CMD_CONVERT_RAIL | CMD_MSG(STR_ERROR_CAN_T_CONVERT_RAIL), CcPlaySound_CONSTRUCTION_RAIL);
					break;

				case DDSP_CONVERT_RAIL_TRACK: {
					Track track = (Track)(_thd.drawstyle & HT_DIR_MASK); // 0..5
					TileIndex start_tile = TileVirtXY(_thd.selstart.x, _thd.selstart.y);
					TileIndex end_tile = TileVirtXY(_thd.selend.x, _thd.selend.y);
					DoCommandP((_thd.drawstyle & HT_RAIL) ? end_tile : start_tile, end_tile, _cur_railtype | (track << 6), CMD_CONVERT_RAIL_TRACK | CMD_MSG(STR_ERROR_CAN_T_CONVERT_RAIL), CcPlaySound_CONSTRUCTION_RAIL);
					break;
				}

				case DDSP_REMOVE_STATION:
				case DDSP_BUILD_STATION:
					if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) {
						/* Station */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_REMOVE_PART_OF_STATION), CcPlaySound_CONSTRUCTION_RAIL);
						} else {
							HandleStationPlacement(start_tile, end_tile);
						}
					} else {
						/* Waypoint */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_REMOVE_TRAIN_WAYPOINT), CcPlaySound_CONSTRUCTION_RAIL);
						} else {
							TileArea ta(start_tile, end_tile);
							uint32_t p1 = _cur_railtype | (select_method == VPM_X_LIMITED ? AXIS_X : AXIS_Y) << 6 | ta.w << 8 | ta.h << 16 | _ctrl_pressed << 24;
							uint32_t p2 = STAT_CLASS_WAYP | INVALID_STATION << 16;
							uint64_t p3 = _cur_waypoint_type;

							CommandContainer cmdcont = NewCommandContainerBasic(ta.tile, p1, p2, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT), CcPlaySound_CONSTRUCTION_RAIL);
							cmdcont.p3 = p3;
							ShowSelectWaypointIfNeeded(cmdcont, ta);
						}
					}
					break;
			}
		}
	}

	void OnPlaceObjectAbort() override
	{
		if (this->IsWidgetLowered(WID_RAT_BUILD_STATION)) SetViewportCatchmentStation(nullptr, true);
		if (this->IsWidgetLowered(WID_RAT_BUILD_WAYPOINT)) SetViewportCatchmentWaypoint(nullptr, true);

		this->RaiseButtons();
		this->DisableWidget(WID_RAT_REMOVE);
		this->SetWidgetDirty(WID_RAT_REMOVE);

		CloseWindowById(WC_BUILD_SIGNAL, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_STATION, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_RAIL);
		CloseWindowById(WC_BUILD_WAYPOINT, TRANSPORT_RAIL);
		CloseWindowById(WC_SELECT_STATION, 0);
		CloseWindowByClass(WC_BUILD_BRIDGE);
	}

	void OnPlacePresize([[maybe_unused]] Point pt, TileIndex tile) override
	{
		DoCommand(tile, _cur_railtype | (TRANSPORT_RAIL << 8), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		/* do not toggle Remove button by Ctrl when placing station */
		if (!this->IsWidgetLowered(WID_RAT_BUILD_STATION) && !this->IsWidgetLowered(WID_RAT_BUILD_WAYPOINT) && RailToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		if (this->IsWidgetLowered(WID_RAT_BUILD_WAYPOINT)) CheckRedrawWaypointCoverage(this, false);
	}

	static HotkeyList hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRailToolbarWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RailToolbarGlobalHotkeys(int hotkey)
{
	if (_game_mode != GM_NORMAL) return ES_NOT_HANDLED;
	extern RailType _last_built_railtype;
	Window *w = ShowBuildRailToolbar(_last_built_railtype);
	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

const uint16_t _railtoolbar_autorail_keys[] = {'5', 'A' | WKC_GLOBAL_HOTKEY, 0};
const uint16_t _railtoolbar_polyrail_keys[] = {'Y', 'A' | WKC_CTRL | WKC_GLOBAL_HOTKEY, 0};
const uint16_t _railtoolbar_new_poly_keys[] = {'Y' | WKC_CTRL, 'A' | WKC_CTRL | WKC_SHIFT | WKC_GLOBAL_HOTKEY, 0};

static Hotkey railtoolbar_hotkeys[] = {
	Hotkey('1', "build_ns", WID_RAT_BUILD_NS),
	Hotkey('2', "build_x", WID_RAT_BUILD_X),
	Hotkey('3', "build_ew", WID_RAT_BUILD_EW),
	Hotkey('4', "build_y", WID_RAT_BUILD_Y),
	Hotkey(_railtoolbar_autorail_keys, "autorail", WID_RAT_AUTORAIL),
	Hotkey(_railtoolbar_polyrail_keys, "polyrail", HOTKEY_POLYRAIL),
	Hotkey(_railtoolbar_new_poly_keys, "new_polyrail", HOTKEY_NEW_POLYRAIL),
	Hotkey('6', "demolish", WID_RAT_DEMOLISH),
	Hotkey('7', "depot", WID_RAT_BUILD_DEPOT),
	Hotkey('8', "waypoint", WID_RAT_BUILD_WAYPOINT),
	Hotkey('9', "station", WID_RAT_BUILD_STATION),
	Hotkey('S', "signal", WID_RAT_BUILD_SIGNALS),
	Hotkey('B', "bridge", WID_RAT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_RAT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_RAT_REMOVE),
	Hotkey('C', "convert", WID_RAT_CONVERT_RAIL),
	Hotkey(WKC_CTRL | 'C', "convert_track", WID_RAT_CONVERT_RAIL_TRACK),
	HOTKEY_LIST_END
};
HotkeyList BuildRailToolbarWindow::hotkeys("railtoolbar", railtoolbar_hotkeys, RailToolbarGlobalHotkeys);

static constexpr NWidgetPart _nested_build_rail_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_RAT_CAPTION), SetDataTip(STR_JUST_STRING2, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_NS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_EW),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_EW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_AUTORAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTORAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_AUTORAIL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_RAT_POLYRAIL_SEL),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_POLYRAIL),
							SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTORAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_POLYRAIL),
		EndContainer(),

		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(4, 22), EndContainer(),

		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DEPOT_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_TRAIN_DEPOT_FOR_BUILDING),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_WAYPOINT, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_STATION),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_RAIL_STATION, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_SIGNALS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_SIGNALS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_SIGNALS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_BRIDGE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(20, 22), SetDataTip(SPR_IMG_TUNNEL_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_RAIL_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_RAT_CONVERT_RAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_RAIL, 0),
	EndContainer(),
};

static WindowDesc _build_rail_desc(__FILE__, __LINE__,
	WDP_ALIGN_TOOLBAR, "toolbar_rail", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	std::begin(_nested_build_rail_widgets), std::end(_nested_build_rail_widgets),
	&BuildRailToolbarWindow::hotkeys
);


/**
 * Open the build rail toolbar window for a specific rail type.
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @param railtype Rail type to open the window for
 * @return newly opened rail toolbar, or nullptr if the toolbar could not be opened.
 */
Window *ShowBuildRailToolbar(RailType railtype)
{
	if (!Company::IsValidID(_local_company)) return nullptr;
	if (!ValParamRailType(railtype)) return nullptr;

	CloseWindowByClass(WC_BUILD_TOOLBAR);
	_cur_railtype = railtype;
	_remove_button_clicked = false;
	return new BuildRailToolbarWindow(&_build_rail_desc, railtype);
}

/* TODO: For custom stations, respect their allowed platforms/lengths bitmasks!
 * --pasky */

static void HandleStationPlacement(TileIndex start, TileIndex end)
{
	TileArea ta(start, end);
	uint numtracks = ta.w;
	uint platlength = ta.h;

	if (_railstation.orientation == AXIS_X) Swap(numtracks, platlength);

	uint32_t p1 = _cur_railtype | _railstation.orientation << 6 | numtracks << 8 | platlength << 16 | _ctrl_pressed << 24;
	uint32_t p2 = _railstation.station_class | INVALID_STATION << 16;
	uint64_t p3 = _railstation.station_type;

	CommandContainer cmdcont = NewCommandContainerBasic(ta.tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation);
	cmdcont.p3 = p3;
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/** Enum referring to the Hotkeys in the build rail station window */
enum BuildRalStationHotkeys {
	BRASHK_FOCUS_FILTER_BOX, ///< Focus the edit box for editing the filter string
};

struct BuildRailStationWindow : public PickerWindowBase {
private:
	uint line_height;     ///< Height of a single line in the newstation selection matrix (#WID_BRAS_NEWST_LIST widget).
	uint coverage_height; ///< Height of the coverage texts.
	Scrollbar *vscroll;   ///< Vertical scrollbar of the new station list.
	Scrollbar *vscroll2;  ///< Vertical scrollbar of the matrix with new stations.

	typedef GUIList<StationClassID, std::nullptr_t, StringFilter &> GUIStationClassList; ///< Type definition for the list to hold available station classes.

	static const uint EDITBOX_MAX_SIZE = 16; ///< The maximum number of characters for the filter edit box.

	static Listing   last_sorting;           ///< Default sorting of #GUIStationClassList.
	static Filtering last_filtering;         ///< Default filtering of #GUIStationClassList.
	static GUIStationClassList::SortFunction * const sorter_funcs[];   ///< Sort functions of the #GUIStationClassList.
	static GUIStationClassList::FilterFunction * const filter_funcs[]; ///< Filter functions of the #GUIStationClassList.
	GUIStationClassList station_classes;     ///< Available station classes.
	StringFilter string_filter;              ///< Filter for available station classes.
	QueryString filter_editbox;              ///< Filter editbox.

	/**
	 * Scrolls #WID_BRAS_NEWST_SCROLL so that the selected station class is visible.
	 *
	 * Note that this method should be called shortly after SelectClassAndStation() which will ensure
	 * an actual existing station class is selected, or the one at position 0 which will always be
	 * the default TTD rail station.
	 */
	void EnsureSelectedStationClassIsVisible()
	{
		/* No additional station types present */
		if (this->vscroll == nullptr) return;

		uint pos = 0;
		for (auto station_class : this->station_classes) {
			if (station_class == _railstation.station_class) break;
			pos++;
		}
		this->vscroll->SetCount(this->station_classes.size());
		this->vscroll->ScrollTowards(pos);
	}

	/**
	 * Verify whether the currently selected station size is allowed after selecting a new station class/type.
	 * If not, change the station size variables ( _settings_client.gui.station_numtracks and _settings_client.gui.station_platlength ).
	 * @param statspec Specification of the new station class/type
	 */
	void CheckSelectedSize(const StationSpec *statspec)
	{
		if (statspec == nullptr || _settings_client.gui.station_dragdrop) return;

		/* If current number of tracks is not allowed, make it as big as possible */
		if (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
			this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
			_settings_client.gui.station_numtracks = 1;
			if (statspec->disallowed_platforms != UINT8_MAX) {
				while (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					_settings_client.gui.station_numtracks++;
				}
				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
			}
		}

		if (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
			this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
			_settings_client.gui.station_platlength = 1;
			if (statspec->disallowed_lengths != UINT8_MAX) {
				while (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					_settings_client.gui.station_platlength++;
				}
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
			}
		}
	}

	void SelectClass(StationClassID station_class_id) {
		if (_railstation.station_class != station_class_id) {
			StationClass *station_class = StationClass::Get(station_class_id);
			_railstation.station_class = station_class_id;
			_railstation.station_count = station_class->GetSpecCount();
			_railstation.station_type  = 0;

			this->CheckSelectedSize(station_class->GetSpec(_railstation.station_type));

			NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRAS_MATRIX);
			matrix->SetCount(_railstation.station_count);
			matrix->SetClicked(_railstation.station_type);
			this->SetDirty();
		}
	}

public:
	BuildRailStationWindow(WindowDesc *desc, Window *parent, bool newstation) : PickerWindowBase(desc, parent), filter_editbox(EDITBOX_MAX_SIZE * MAX_CHAR_LENGTH, EDITBOX_MAX_SIZE)
	{
		this->coverage_height = 2 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;
		this->vscroll = nullptr;
		_railstation.newstations = newstation;

		this->CreateNestedTree();
		this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_ADDITIONS)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_MATRIX)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_DEFSIZE)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BRAS_SHOW_NEWST_RESIZE)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		/* Hide the station class filter if no stations other than the default one are available. */
		this->GetWidget<NWidgetStacked>(WID_BRAS_FILTER_CONTAINER)->SetDisplayedPlane(newstation ? 0 : SZSP_NONE);
		if (newstation) {
			this->vscroll = this->GetScrollbar(WID_BRAS_NEWST_SCROLL);
			this->vscroll2 = this->GetScrollbar(WID_BRAS_MATRIX_SCROLL);

			this->querystrings[WID_BRAS_FILTER_EDITBOX] = &this->filter_editbox;
			this->station_classes.SetListing(this->last_sorting);
			this->station_classes.SetFiltering(this->last_filtering);
			this->station_classes.SetSortFuncs(this->sorter_funcs);
			this->station_classes.SetFilterFuncs(this->filter_funcs);
		}

		this->station_classes.ForceRebuild();

		BuildStationClassesAvailable();
		SelectClassAndStation();

		this->FinishInitNested(TRANSPORT_RAIL);

		this->LowerWidget(WID_BRAS_PLATFORM_DIR_X + _railstation.orientation);
		if (_settings_client.gui.station_dragdrop) {
			this->LowerWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);
		} else {
			this->LowerWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
			this->LowerWidget(WID_BRAS_PLATFORM_LEN_BEGIN + _settings_client.gui.station_platlength);
		}
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
		this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);

		if (!newstation) {
			_railstation.station_class = StationClassID::STAT_CLASS_DFLT;
			_railstation.station_type = 0;
			this->vscroll2 = nullptr;
		} else {
			_railstation.station_count = StationClass::Get(_railstation.station_class)->GetSpecCount();
			_railstation.station_type = std::min<int>(_railstation.station_type, _railstation.station_count - 1);

			NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRAS_MATRIX);
			matrix->SetScrollbar(this->vscroll2);
			matrix->SetCount(_railstation.station_count);
			matrix->SetClicked(_railstation.station_type);

			EnsureSelectedStationClassIsVisible();
		}

		this->InvalidateData();
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindowBase::Close();
	}

	/** Sort station classes by StationClassID. */
	static bool StationClassIDSorter(StationClassID const &a, StationClassID const &b)
	{
		return a < b;
	}

	/** Filter station classes by class name. */
	static bool TagNameFilter(StationClassID const * sc, StringFilter &filter)
	{
		filter.ResetState();
		filter.AddLine(GetString(StationClass::Get(*sc)->name));
		return filter.GetState();
	}

	/** Builds the filter list of available station classes. */
	void BuildStationClassesAvailable()
	{
		if (!this->station_classes.NeedRebuild()) return;

		this->station_classes.clear();

		for (uint i = 0; StationClass::IsClassIDValid((StationClassID)i); i++) {
			StationClassID station_class_id = (StationClassID)i;
			if (station_class_id == StationClassID::STAT_CLASS_WAYP) {
				// Skip waypoints.
				continue;
			}
			StationClass *station_class = StationClass::Get(station_class_id);
			if (station_class->GetUISpecCount() == 0) continue;
			station_classes.push_back(station_class_id);
		}

		if (_railstation.newstations) {
			this->station_classes.Filter(this->string_filter);
			this->station_classes.shrink_to_fit();
			this->station_classes.RebuildDone();
			this->station_classes.Sort();

			this->vscroll->SetCount(this->station_classes.size());
		}
	}

	/**
	 * Checks if the previously selected current station class and station
	 * can be shown as selected to the user when the dialog is opened.
	 */
	void SelectClassAndStation()
	{
		if (_railstation.station_class == StationClassID::STAT_CLASS_DFLT) {
			/* This happens during the first time the window is open during the game life cycle. */
			this->SelectOtherClass(StationClassID::STAT_CLASS_DFLT);
		} else {
			/* Check if the previously selected station class is not available anymore as a
			 * result of starting a new game without the corresponding NewGRF. */
			bool available = _railstation.station_class < StationClass::GetClassCount();
			this->SelectOtherClass(available ? _railstation.station_class : StationClassID::STAT_CLASS_DFLT);
		}
	}

	/**
	 * Select the specified station class.
	 * @param station_class Station class select.
	 */
	void SelectOtherClass(StationClassID station_class)
	{
		_railstation.station_class = station_class;
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		this->BuildStationClassesAvailable();
	}

	EventState OnHotkey(int hotkey) override
	{
		switch (hotkey) {
			case BRASHK_FOCUS_FILTER_BOX:
				this->SetFocusedWidget(WID_BRAS_FILTER_EDITBOX);
				SetFocusedWindow(this); // The user has asked to give focus to the text box, so make sure this window is focused.
				break;

			default:
				return ES_NOT_HANDLED;
		}

		return ES_HANDLED;
	}

	void OnEditboxChanged(WidgetID widget) override
	{
		if (widget == WID_BRAS_FILTER_EDITBOX) {
			string_filter.SetFilterTerm(this->filter_editbox.text.buf);
			this->station_classes.SetFilterState(!string_filter.IsEmpty());
			this->station_classes.ForceRebuild();
			this->InvalidateData();
		}
	}

	void OnPaint() override
	{
		bool newstations = _railstation.newstations;
		const StationSpec *statspec = newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;

		if (_settings_client.gui.station_dragdrop) {
			SetTileSelectSize(1, 1);
		} else {
			int x = _settings_client.gui.station_numtracks;
			int y = _settings_client.gui.station_platlength;
			if (_railstation.orientation == AXIS_X) Swap(x, y);
			if (!_remove_button_clicked) {
				SetTileSelectSize(x, y);
			}
		}

		int rad = (_settings_game.station.modified_catchment) ? CA_TRAIN : CA_UNMODIFIED;
		rad += _settings_game.station.catchment_increase;

		if (_settings_client.gui.station_show_coverage) SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);

		for (uint bits = 0; bits < 7; bits++) {
			bool disable = bits >= _settings_game.station.station_spread;
			if (statspec == nullptr) {
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_NUM_1, disable);
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_LEN_1, disable);
			} else {
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_NUM_1, HasBit(statspec->disallowed_platforms, bits) || disable);
				this->SetWidgetDisabledState(bits + WID_BRAS_PLATFORM_LEN_1, HasBit(statspec->disallowed_lengths,   bits) || disable);
			}
		}

		this->DrawWidgets();

		if (this->IsShaded()) return;
		/* 'Accepts' and 'Supplies' texts. */
		Rect r = this->GetWidget<NWidgetBase>(WID_BRAS_COVERAGE_TEXTS)->GetCurrentRect();
		int top = r.top;
		top = DrawStationCoverageAreaText(r.left, r.right, top, SCT_ALL, rad, false) + WidgetDimensions::scaled.vsep_normal;
		top = DrawStationCoverageAreaText(r.left, r.right, top, SCT_ALL, rad, true);
		/* Resize background if the window is too small.
		 * Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		 * (This is the case, if making the window bigger moves the mouse into the window.) */
		if (top > r.bottom) {
			this->coverage_height += top - r.bottom;
			this->ReInit();
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_BRAS_NEWST_LIST: {
				Dimension d = {0, 0};
				for (auto station_class : this->station_classes) {
					d = maxdim(d, GetStringBoundingBox(StationClass::Get(station_class)->name));
				}
				size.width = std::max(size.width, d.width + padding.width);
				this->line_height = GetCharacterHeight(FS_NORMAL) + padding.height;
				size.height = 5 * this->line_height;
				resize.height = this->line_height;
				break;
			}

			case WID_BRAS_SHOW_NEWST_TYPE: {
				if (!_railstation.newstations) {
					size.width = 0;
					size.height = 0;
					break;
				}

				/* If newstations exist, compute the non-zero minimal size. */
				Dimension d = {0, 0};
				StringID str = this->GetWidget<NWidgetCore>(widget)->widget_data;
				for (auto station_class : this->station_classes) {
					StationClass *stclass = StationClass::Get(station_class);
					for (uint j = 0; j < stclass->GetSpecCount(); j++) {
						const StationSpec *statspec = stclass->GetSpec(j);
						SetDParam(0, (statspec != nullptr && statspec->name != 0) ? statspec->name : STR_STATION_CLASS_DFLT_STATION);
						d = maxdim(d, GetStringBoundingBox(str));
					}
				}
				size.width = std::max(size.width, d.width + padding.width);
				size.width = std::min<uint>(size.width, ScaleGUITrad(400));
				break;
			}

			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
			case WID_BRAS_IMAGE:
				size.width  = ScaleGUITrad(64) + WidgetDimensions::scaled.fullbevel.Horizontal();
				size.height = ScaleGUITrad(58) + WidgetDimensions::scaled.fullbevel.Vertical();
				break;

			case WID_BRAS_COVERAGE_TEXTS:
				size.height = this->coverage_height;
				break;

			case WID_BRAS_MATRIX:
				fill.height = 1;
				resize.height = 1;
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		DrawPixelInfo tmp_dpi;

		switch (widget) {
			case WID_BRAS_PLATFORM_DIR_X: {
				/* Set up a clipping area for the '/' station preview */
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
					int y = (ir.Height() + ScaleSpriteTrad(58)) / 2 - ScaleSpriteTrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, AXIS_X, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 2);
					}
				}
				break;
			}

			case WID_BRAS_PLATFORM_DIR_Y: {
				/* Set up a clipping area for the '\' station preview */
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
					int y = (ir.Height() + ScaleSpriteTrad(58)) / 2 - ScaleSpriteTrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, AXIS_Y, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 3);
					}
				}
				break;
			}

			case WID_BRAS_NEWST_LIST: {
				Rect ir = r.Shrink(WidgetDimensions::scaled.matrix);
				uint statclass = 0;
				for (auto station_class : this->station_classes) {
					if (this->vscroll->IsVisible(statclass)) {
						DrawString(ir,
								StationClass::Get(station_class)->name,
								station_class == _railstation.station_class ? TC_WHITE : TC_BLACK);
						ir.top += this->line_height;
					}
					statclass++;
				}
				break;
			}

			case WID_BRAS_IMAGE: {
				uint16_t type = this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->GetCurrentElement();
				assert(type < _railstation.station_count);
				/* Check station availability callback */
				const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(type);

				/* Set up a clipping area for the station preview. */
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
					int y = (ir.Height() + ScaleSpriteTrad(58)) / 2 - ScaleSpriteTrad(31);
					if (!DrawStationTile(x, y, _cur_railtype, _railstation.orientation, _railstation.station_class, type)) {
						StationPickerDrawSprite(x, y, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 2 + _railstation.orientation);
					}
				}
				if (!IsStationAvailable(statspec)) {
					GfxFillRect(ir, PC_BLACK, FILLRECT_CHECKER);
				}
				break;
			}
		}
	}

	void OnResize() override
	{
		if (this->vscroll != nullptr) { // New stations available.
			this->vscroll->SetCapacityFromWidget(this, WID_BRAS_NEWST_LIST);
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_BRAS_SHOW_NEWST_TYPE) {
			const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type);
			SetDParam(0, (statspec != nullptr && statspec->name != 0) ? statspec->name : STR_STATION_CLASS_DFLT_STATION);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BRAS_PLATFORM_DIR_X:
			case WID_BRAS_PLATFORM_DIR_Y:
				this->RaiseWidget(WID_BRAS_PLATFORM_DIR_X + _railstation.orientation);
				_railstation.orientation = (Axis)(widget - WID_BRAS_PLATFORM_DIR_X);
				this->LowerWidget(WID_BRAS_PLATFORM_DIR_X + _railstation.orientation);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BRAS_PLATFORM_NUM_1:
			case WID_BRAS_PLATFORM_NUM_2:
			case WID_BRAS_PLATFORM_NUM_3:
			case WID_BRAS_PLATFORM_NUM_4:
			case WID_BRAS_PLATFORM_NUM_5:
			case WID_BRAS_PLATFORM_NUM_6:
			case WID_BRAS_PLATFORM_NUM_7: {
				this->RaiseWidget(WID_BRAS_PLATFORM_NUM_BEGIN + _settings_client.gui.station_numtracks);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_numtracks = widget - WID_BRAS_PLATFORM_NUM_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					/* The previously selected number of platforms in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_PLATFORM_LEN_1:
			case WID_BRAS_PLATFORM_LEN_2:
			case WID_BRAS_PLATFORM_LEN_3:
			case WID_BRAS_PLATFORM_LEN_4:
			case WID_BRAS_PLATFORM_LEN_5:
			case WID_BRAS_PLATFORM_LEN_6:
			case WID_BRAS_PLATFORM_LEN_7: {
				this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				this->RaiseWidget(WID_BRAS_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_platlength = widget - WID_BRAS_PLATFORM_LEN_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					/* The previously selected number of tracks in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_PLATFORM_DRAG_N_DROP: {
				_settings_client.gui.station_dragdrop ^= true;

				this->ToggleWidgetLoweredState(WID_BRAS_PLATFORM_DRAG_N_DROP);

				/* get the first allowed length/number of platforms */
				const StationSpec *statspec = _railstation.newstations ? StationClass::Get(_railstation.station_class)->GetSpec(_railstation.station_type) : nullptr;
				if (statspec != nullptr && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}
				if (statspec != nullptr && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->SetWidgetLoweredState(_settings_client.gui.station_numtracks + WID_BRAS_PLATFORM_NUM_BEGIN, !_settings_client.gui.station_dragdrop);
				this->SetWidgetLoweredState(_settings_client.gui.station_platlength + WID_BRAS_PLATFORM_LEN_BEGIN, !_settings_client.gui.station_dragdrop);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_HIGHLIGHT_OFF:
			case WID_BRAS_HIGHLIGHT_ON:
				_settings_client.gui.station_show_coverage = (widget != WID_BRAS_HIGHLIGHT_OFF);

				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
				this->SetWidgetLoweredState(WID_BRAS_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			case WID_BRAS_NEWST_LIST: {
				auto it = this->vscroll->GetScrolledItemFromWidget(this->station_classes, pt.y, this, WID_BRAS_NEWST_LIST);
				if (it == this->station_classes.end()) return;
				StationClassID station_class_id = *it;
				this->SelectClass(station_class_id);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BRAS_IMAGE: {
				uint16_t y = this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->GetCurrentElement();
				if (y >= _railstation.station_count) return;

				/* Check station availability callback */
				const StationSpec *statspec = StationClass::Get(_railstation.station_class)->GetSpec(y);
				if (!IsStationAvailable(statspec)) return;

				_railstation.station_type = y;

				this->CheckSelectedSize(statspec);
				this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->SetClicked(_railstation.station_type);

				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
				break;
			}
		}
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}

	void SelectClassAndSpec(StationClassID class_id, int spec_id)
	{
		this->SelectClass(class_id);
		this->EnsureSelectedStationClassIsVisible();
		this->GetWidget<NWidgetBase>(WID_BRAS_IMAGE)->GetParentWidget<NWidgetMatrix>()->SetCurrentElement(spec_id);
		this->OnClick({}, WID_BRAS_IMAGE, 1);
	}

	static HotkeyList hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRailStationWindow.
 * @param hotkey Hotkey
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState BuildRailStationGlobalHotkeys(int hotkey)
{
	if (_game_mode == GM_MENU) return ES_NOT_HANDLED;
	Window *w = ShowStationBuilder(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static Hotkey buildrailstation_hotkeys[] = {
	Hotkey('F', "focus_filter_box", BRASHK_FOCUS_FILTER_BOX),
	HOTKEY_LIST_END
};
HotkeyList BuildRailStationWindow::hotkeys("buildrailstation", buildrailstation_hotkeys, BuildRailStationGlobalHotkeys);

Listing BuildRailStationWindow::last_sorting = { false, 0 };
Filtering BuildRailStationWindow::last_filtering = { false, 0 };

BuildRailStationWindow::GUIStationClassList::SortFunction * const BuildRailStationWindow::sorter_funcs[] = {
	&StationClassIDSorter,
};

BuildRailStationWindow::GUIStationClassList::FilterFunction * const BuildRailStationWindow::filter_funcs[] = {
	&TagNameFilter,
};

static constexpr NWidgetPart _nested_station_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_RAIL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_DEFSIZE),
			NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0), SetPadding(WidgetDimensions::unscaled.picker),
				NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
					NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_FILTER_CONTAINER),
							NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
								NWidget(WWT_TEXT, COLOUR_DARK_GREEN), SetFill(0, 1), SetDataTip(STR_LIST_FILTER_TITLE, STR_NULL),
								NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BRAS_FILTER_EDITBOX), SetFill(1, 0), SetResize(1, 0),
										SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_ADDITIONS),
							NWidget(NWID_HORIZONTAL),
								NWidget(WWT_MATRIX, COLOUR_GREY, WID_BRAS_NEWST_LIST), SetMinimalSize(122, 71), SetFill(1, 0),
										SetMatrixDataTip(1, 0, STR_STATION_BUILD_STATION_CLASS_TOOLTIP), SetScrollbar(WID_BRAS_NEWST_SCROLL),
								NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BRAS_NEWST_SCROLL),
							EndContainer(),
						EndContainer(),
						NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL),
						NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
							NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_X), SetMinimalSize(66, 60), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
							NWidget(WWT_PANEL, COLOUR_GREY, WID_BRAS_PLATFORM_DIR_Y), SetMinimalSize(66, 60), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
						EndContainer(),
						NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BRAS_SHOW_NEWST_TYPE), SetMinimalSize(144, 11), SetDataTip(STR_JUST_STRING, STR_NULL), SetTextStyle(TC_ORANGE),
						NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_NUMBER_OF_TRACKS, STR_NULL),
						NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_NUM_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
						EndContainer(),
						NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_PLATFORM_LENGTH, STR_NULL),
						NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_LEN_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_PLATFORM_DRAG_N_DROP), SetMinimalSize(75, 12), SetDataTip(STR_STATION_BUILD_DRAG_DROP, STR_STATION_BUILD_DRAG_DROP_TOOLTIP),
						EndContainer(),
						NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_OFF), SetMinimalSize(60, 12),
									SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAS_HIGHLIGHT_ON), SetMinimalSize(60, 12),
									SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_MATRIX),
						/* We need an additional background for the matrix, as the matrix cannot handle the scrollbar due to not being an NWidgetCore. */
						NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_BRAS_MATRIX_SCROLL),
							NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BRAS_MATRIX), SetPIP(0, 2, 0),
								NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BRAS_IMAGE), SetMinimalSize(66, 60),
										SetFill(0, 0), SetResize(0, 0), SetDataTip(0x0, STR_STATION_BUILD_STATION_TYPE_TOOLTIP), SetScrollbar(WID_BRAS_MATRIX_SCROLL),
								EndContainer(),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BRAS_COVERAGE_TEXTS), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, WidgetDimensions::unscaled.vsep_normal),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BRAS_SHOW_NEWST_RESIZE),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BRAS_MATRIX_SCROLL),
				NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** High level window description of the station-build window (default & newGRF) */
static WindowDesc _station_builder_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_station_rail", 350, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	std::begin(_nested_station_builder_widgets), std::end(_nested_station_builder_widgets),
	&BuildRailStationWindow::hotkeys
);

/** Open station build window */
static Window *ShowStationBuilder(Window *parent)
{
	bool newstations = StationClass::GetClassCount() > 2 || StationClass::Get(STAT_CLASS_DFLT)->GetSpecCount() != 1;
	return new BuildRailStationWindow(&_station_builder_desc, parent, newstations);
}

struct BuildSignalWindow : public PickerWindowBase {
private:
	Dimension sig_sprite_size;     ///< Maximum size of signal GUI sprites.
	int sig_sprite_bottom_offset;  ///< Maximum extent of signal GUI sprite from reference point towards bottom.
	bool all_signal_mode;          ///< Whether all signal mode is shown
	bool progsig_ui_shown;         ///< Whether programmable pre-signal UI is shown
	bool realistic_braking_mode;   ///< Whether realistic braking mode UI is shown
	bool noentry_ui_shown;         ///< Whether no-entry signal UI is shown
	bool style_selector_shown;     ///< Whether the style selector is shown

	/**
	 * Draw dynamic a signal-sprite in a button in the signal GUI
	 * @param image        the sprite to draw
	 */
	void DrawSignalSprite(const Rect &r, PalSpriteID image) const
	{
		Point offset;
		Dimension sprite_size = GetSpriteSize(image.sprite, &offset);
		Rect ir = r.Shrink(WidgetDimensions::scaled.imgbtn);
		int x = CenterBounds(ir.left, ir.right, sprite_size.width - offset.x) - offset.x; // centered
		int y = ir.top - sig_sprite_bottom_offset +
				(ir.Height() + sig_sprite_size.height) / 2; // aligned to bottom

		DrawSprite(image.sprite, image.pal, x, y);
	}

	void SetDisableStates()
	{
		for (WidgetID widget = WID_BS_SEMAPHORE_NORM; widget <= WID_BS_SEMAPHORE_NO_ENTRY; widget++) {
			this->SetWidgetDisabledState(widget, _cur_signal_style > 0 && !HasBit(_new_signal_styles[_cur_signal_style - 1].semaphore_mask, TypeForClick(widget - WID_BS_SEMAPHORE_NORM)));
		}
		for (WidgetID widget = WID_BS_ELECTRIC_NORM; widget <= WID_BS_ELECTRIC_NO_ENTRY; widget++) {
			this->SetWidgetDisabledState(widget, _cur_signal_style > 0 && !HasBit(_new_signal_styles[_cur_signal_style - 1].electric_mask, TypeForClick(widget - WID_BS_ELECTRIC_NORM)));
		}
		if (_cur_signal_style > 0) {
			const NewSignalStyle &style = _new_signal_styles[_cur_signal_style - 1];
			if (!HasBit(_cur_signal_variant == SIG_SEMAPHORE ? style.semaphore_mask : style.electric_mask, _cur_signal_type)) {
				/* Currently selected signal type isn't allowed, pick another */
				this->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_button);

				_cur_signal_variant = SIG_ELECTRIC;
				_cur_signal_button = 0;

				const uint type_count = (WID_BS_SEMAPHORE_NO_ENTRY + 1 - WID_BS_SEMAPHORE_NORM);
				for (uint i = 0; i < type_count * 2; i++) {
					SignalVariant var = (i < type_count) ? SIG_ELECTRIC : SIG_SEMAPHORE;
					uint button = i % type_count;
					if (HasBit(var == SIG_SEMAPHORE ? style.semaphore_mask : style.electric_mask, TypeForClick(button))) {
						_cur_signal_variant = var;
						_cur_signal_button = button;
						break;
					}
				}

				_cur_signal_type = TypeForClick(_cur_signal_button);
				this->LowerWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_button);
			}
		}
	}

	void SetSignalUIMode()
	{
		this->all_signal_mode = (_settings_client.gui.signal_gui_mode == SIGNAL_GUI_ALL);
		this->realistic_braking_mode = (_settings_game.vehicle.train_braking_model == TBM_REALISTIC);
		this->progsig_ui_shown = _settings_client.gui.show_progsig_ui;
		this->noentry_ui_shown = _settings_client.gui.show_noentrysig_ui;
		this->style_selector_shown = _enabled_new_signal_styles_mask > 1;

		bool show_norm = this->realistic_braking_mode || this->all_signal_mode;
		bool show_presig = !this->realistic_braking_mode && this->all_signal_mode;
		bool show_progsig = show_presig && this->progsig_ui_shown;

		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_NORM_SEL)->SetDisplayedPlane(show_norm ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_NORM_SEL)->SetDisplayedPlane(show_norm ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_ENTRY_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_ENTRY_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_EXIT_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_EXIT_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_COMBO_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_COMBO_SEL)->SetDisplayedPlane(show_presig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_PROG_SEL)->SetDisplayedPlane(show_progsig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_PROG_SEL)->SetDisplayedPlane(show_progsig ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_SEMAPHORE_NOEN_SEL)->SetDisplayedPlane(this->noentry_ui_shown ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_ELECTRIC_NOEN_SEL)->SetDisplayedPlane(this->noentry_ui_shown ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_BS_PROGRAM_SEL)->SetDisplayedPlane(show_progsig ? 0 : 1);
		this->SetWidgetDisabledState(WID_BS_PROGRAM, !show_progsig);
		this->SetWidgetsDisabledState(!show_norm, WID_BS_SEMAPHORE_NORM, WID_BS_ELECTRIC_NORM);
		this->SetWidgetsDisabledState(!show_presig, WID_BS_SEMAPHORE_ENTRY, WID_BS_ELECTRIC_ENTRY, WID_BS_SEMAPHORE_EXIT,
				WID_BS_ELECTRIC_EXIT, WID_BS_SEMAPHORE_COMBO, WID_BS_ELECTRIC_COMBO);
		this->SetWidgetsDisabledState(!show_progsig, WID_BS_SEMAPHORE_PROG, WID_BS_ELECTRIC_PROG);
		this->SetWidgetsDisabledState(!this->noentry_ui_shown, WID_BS_SEMAPHORE_NO_ENTRY, WID_BS_ELECTRIC_NO_ENTRY);

		this->GetWidget<NWidgetStacked>(WID_BS_TOGGLE_SIZE_SEL)->SetDisplayedPlane(!this->realistic_braking_mode ? 0 : SZSP_NONE);
		this->SetWidgetDisabledState(WID_BS_TOGGLE_SIZE, this->realistic_braking_mode);

		this->GetWidget<NWidgetStacked>(WID_BS_STYLE_SEL)->SetDisplayedPlane(this->style_selector_shown ? 0 : SZSP_NONE);

		this->SetDisableStates();
	}

	void ClearRemoveState()
	{
		if (_remove_button_clicked) {
			Window *w = FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);
			if (w != nullptr) ToggleRailButton_Remove(w);
		}
	}

public:
	BuildSignalWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();
		this->SetSignalUIMode();
		this->FinishInitNested(TRANSPORT_RAIL);
		this->OnInvalidateData();
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		_convert_signal_button = false;
		_trace_restrict_button = false;
		_program_signal_button = false;
		this->PickerWindowBase::Close();
	}

	void OnInit() override
	{
		/* Calculate maximum signal sprite size. */
		this->sig_sprite_size.width = 0;
		this->sig_sprite_size.height = 0;
		this->sig_sprite_bottom_offset = 0;

		auto process_signals = [&](const PalSpriteID signals[SIGTYPE_END][2][2]) {
			for (uint type = SIGTYPE_BLOCK; type < SIGTYPE_END; type++) {
				for (uint variant = SIG_ELECTRIC; variant <= SIG_SEMAPHORE; variant++) {
					for (uint lowered = 0; lowered < 2; lowered++) {
						Point offset;
						SpriteID spr = signals[type][variant][lowered].sprite;
						if (spr == 0) continue;
						Dimension sprite_size = GetSpriteSize(spr, &offset);
						this->sig_sprite_bottom_offset = std::max<int>(this->sig_sprite_bottom_offset, sprite_size.height);
						this->sig_sprite_size.width = std::max<int>(this->sig_sprite_size.width, sprite_size.width - offset.x);
						this->sig_sprite_size.height = std::max<int>(this->sig_sprite_size.height, sprite_size.height - offset.y);
					}
				}
			}
		};
		process_signals(GetRailTypeInfo(_cur_railtype)->gui_sprites.signals);
		for (uint i = 0; i < _num_new_signal_styles; i++) {
			process_signals(_new_signal_styles[i].signals);
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget == WID_BS_DRAG_SIGNALS_DENSITY_LABEL) {
			/* Two digits for signals density. */
			size.width = std::max(size.width, 2 * GetDigitWidth() + padding.width + WidgetDimensions::scaled.framerect.Horizontal());
		} else if (IsInsideMM(widget, WID_BS_SEMAPHORE_NORM, WID_BS_ELECTRIC_PBS_OWAY + 1)) {
			size.width = std::max(size.width, this->sig_sprite_size.width + padding.width);
			size.height = std::max(size.height, this->sig_sprite_size.height + padding.height);
		} else if (widget == WID_BS_CAPTION) {
			size.width += WidgetDimensions::scaled.frametext.Horizontal();
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_BS_DRAG_SIGNALS_DENSITY_LABEL:
				SetDParam(0, _settings_client.gui.drag_signals_density);
				break;

			case WID_BS_STYLE:
				SetDParam(0, _cur_signal_style == 0 ? STR_BUILD_SIGNAL_DEFAULT_STYLE : _new_signal_styles[_cur_signal_style - 1].name);
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (IsInsideMM(widget, WID_BS_SEMAPHORE_NORM, WID_BS_ELECTRIC_NO_ENTRY + 1)) {
			/* Extract signal from widget number. */
			SignalType type = TypeForClick((widget - WID_BS_SEMAPHORE_NORM) % SIGTYPE_END);
			int var = SIG_SEMAPHORE - (widget - WID_BS_SEMAPHORE_NORM) / SIGTYPE_END; // SignalVariant order is reversed compared to the widgets.
			PalSpriteID sprite = { 0, 0 };
			if (_cur_signal_style > 0) {
				const NewSignalStyle &style = _new_signal_styles[_cur_signal_style - 1];
				if (!HasBit(var == SIG_SEMAPHORE ? style.semaphore_mask : style.electric_mask, type)) return;
				sprite = style.signals[type][var][this->IsWidgetLowered(widget)];
			}
			if (sprite.sprite == 0) {
				sprite = GetRailTypeInfo(_cur_railtype)->gui_sprites.signals[type][var][this->IsWidgetLowered(widget)];
			}

			this->DrawSignalSprite(r, sprite);
		}
	}

	static SignalType TypeForClick(uint id)
	{
		switch (id) {
			case 0: return SIGTYPE_BLOCK;
			case 1: return SIGTYPE_ENTRY;
			case 2: return SIGTYPE_EXIT;
			case 3: return SIGTYPE_COMBO;
			case 4: return SIGTYPE_PROG;
			case 5: return SIGTYPE_PBS;
			case 6: return SIGTYPE_PBS_ONEWAY;
			case 7: return SIGTYPE_NO_ENTRY;
			default:
				assert(!"Bad signal type button ID");
				return SIGTYPE_BLOCK;
		}
	}

	static uint ClickForType(SignalType type)
	{
		switch (type) {
			case SIGTYPE_BLOCK:      return 0;
			case SIGTYPE_ENTRY:      return 1;
			case SIGTYPE_EXIT:       return 2;
			case SIGTYPE_COMBO:      return 3;
			case SIGTYPE_PROG:       return 4;
			case SIGTYPE_PBS:        return 5;
			case SIGTYPE_PBS_ONEWAY: return 6;
			case SIGTYPE_NO_ENTRY:   return 7;
			default:
				assert(!"Bad signal type");
				return 0;
		}
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		switch (widget) {
			case WID_BS_SEMAPHORE_NORM:
			case WID_BS_SEMAPHORE_ENTRY:
			case WID_BS_SEMAPHORE_EXIT:
			case WID_BS_SEMAPHORE_COMBO:
			case WID_BS_SEMAPHORE_PROG:
			case WID_BS_SEMAPHORE_PBS:
			case WID_BS_SEMAPHORE_PBS_OWAY:
			case WID_BS_SEMAPHORE_NO_ENTRY:
			case WID_BS_ELECTRIC_NORM:
			case WID_BS_ELECTRIC_ENTRY:
			case WID_BS_ELECTRIC_EXIT:
			case WID_BS_ELECTRIC_COMBO:
			case WID_BS_ELECTRIC_PROG:
			case WID_BS_ELECTRIC_PBS:
			case WID_BS_ELECTRIC_PBS_OWAY:
			case WID_BS_ELECTRIC_NO_ENTRY:
				this->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_button);

				_cur_signal_button = (uint)((widget - WID_BS_SEMAPHORE_NORM) % (SIGTYPE_END));
				_cur_signal_type = TypeForClick(_cur_signal_button);
				_cur_signal_variant = widget >= WID_BS_ELECTRIC_NORM ? SIG_ELECTRIC : SIG_SEMAPHORE;

				/* Update default (last-used) signal type in config file. */
				_settings_client.gui.default_signal_type = Clamp<SignalType>(_cur_signal_type, SIGTYPE_BLOCK, SIGTYPE_PBS_ONEWAY);

				/* If 'remove' button of rail build toolbar is active, disable it. */
				ClearRemoveState();
				break;

			case WID_BS_CONVERT:
				_convert_signal_button = !_convert_signal_button;
				if (_convert_signal_button) {
					_trace_restrict_button = false;
					_program_signal_button = false;
				}
				break;

			case WID_BS_TRACE_RESTRICT:
				_trace_restrict_button = !_trace_restrict_button;
				if (_trace_restrict_button) {
					_convert_signal_button = false;
					_program_signal_button = false;
					ClearRemoveState();
				}
				break;

			case WID_BS_PROGRAM:
				_program_signal_button = !_program_signal_button;
				if(_program_signal_button) {
					_trace_restrict_button = false;
					_convert_signal_button = false;
				}
				break;

			case WID_BS_DRAG_SIGNALS_DENSITY_DECREASE:
				if (_settings_client.gui.drag_signals_density > 1) {
					_settings_client.gui.drag_signals_density--;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_SETTINGS);
				}
				break;

			case WID_BS_DRAG_SIGNALS_DENSITY_INCREASE:
				if (_settings_client.gui.drag_signals_density < 20) {
					_settings_client.gui.drag_signals_density++;
					SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_SETTINGS);
				}
				break;

			case WID_BS_TOGGLE_SIZE:
				_settings_client.gui.signal_gui_mode = (_settings_client.gui.signal_gui_mode == SIGNAL_GUI_ALL) ? SIGNAL_GUI_PATH : SIGNAL_GUI_ALL;
				this->SetSignalUIMode();
				this->ReInit();
				break;

			case WID_BS_STYLE: {
				DropDownList list;
				list.push_back(MakeDropDownListStringItem(STR_BUILD_SIGNAL_DEFAULT_STYLE, 0, false));
				for (uint i = 0; i < _num_new_signal_styles; i++) {
					if (HasBit(_enabled_new_signal_styles_mask, i + 1)) {
						list.push_back(MakeDropDownListStringItem(_new_signal_styles[i].name, i + 1, false));
					}
				}
				ShowDropDownList(this, std::move(list), _cur_signal_style, widget);
				break;
			}

			default: break;
		}

		this->InvalidateData();
	}

	virtual void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch (widget) {
			case WID_BS_STYLE:
				_cur_signal_style = std::min<uint>(index, _num_new_signal_styles);
				this->SetDisableStates();
				this->SetDirty();
				break;

			default: break;
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
		this->LowerWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_button);

		this->SetWidgetLoweredState(WID_BS_CONVERT, _convert_signal_button);
		this->SetWidgetLoweredState(WID_BS_TRACE_RESTRICT, _trace_restrict_button);
		this->SetWidgetLoweredState(WID_BS_PROGRAM, _program_signal_button);

		this->SetWidgetDisabledState(WID_BS_DRAG_SIGNALS_DENSITY_DECREASE, _settings_client.gui.drag_signals_density == 1);
		this->SetWidgetDisabledState(WID_BS_DRAG_SIGNALS_DENSITY_INCREASE, _settings_client.gui.drag_signals_density == 20);

		if (_cur_signal_style > _num_new_signal_styles || !HasBit(_enabled_new_signal_styles_mask, _cur_signal_style)) _cur_signal_style = 0;

		if (this->all_signal_mode != (_settings_client.gui.signal_gui_mode == SIGNAL_GUI_ALL) || this->progsig_ui_shown != _settings_client.gui.show_progsig_ui ||
				this->realistic_braking_mode != (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) ||
				this->noentry_ui_shown != _settings_client.gui.show_noentrysig_ui ||
				this->style_selector_shown != (_enabled_new_signal_styles_mask > 1)) {
			this->SetSignalUIMode();
			this->ReInit();
		}
	}

	static HotkeyList hotkeys;
};

static Hotkey signaltoolbar_hotkeys[] = {
	Hotkey('N', "routing_restriction", WID_BS_TRACE_RESTRICT),
	Hotkey('K', "convert", WID_BS_CONVERT),
	Hotkey((uint16_t)0, "program_signal", WID_BS_PROGRAM),
	Hotkey((uint16_t)0, "semaphore_normal", WID_BS_SEMAPHORE_NORM),
	Hotkey((uint16_t)0, "semaphore_entry", WID_BS_SEMAPHORE_ENTRY),
	Hotkey((uint16_t)0, "semaphore_exit", WID_BS_SEMAPHORE_EXIT),
	Hotkey((uint16_t)0, "semaphore_combo", WID_BS_SEMAPHORE_COMBO),
	Hotkey((uint16_t)0, "semaphore_prog", WID_BS_SEMAPHORE_PROG),
	Hotkey((uint16_t)0, "semaphore_pbs", WID_BS_SEMAPHORE_PBS),
	Hotkey((uint16_t)0, "semaphore_pbs_oneway", WID_BS_SEMAPHORE_PBS_OWAY),
	Hotkey((uint16_t)0, "semaphore_no_entry", WID_BS_SEMAPHORE_NO_ENTRY),
	Hotkey('G', "signal_normal", WID_BS_ELECTRIC_NORM),
	Hotkey((uint16_t)0, "signal_entry", WID_BS_ELECTRIC_ENTRY),
	Hotkey((uint16_t)0, "signal_exit", WID_BS_ELECTRIC_EXIT),
	Hotkey((uint16_t)0, "signal_combo", WID_BS_ELECTRIC_COMBO),
	Hotkey((uint16_t)0, "signal_prog", WID_BS_ELECTRIC_PROG),
	Hotkey('H', "signal_pbs", WID_BS_ELECTRIC_PBS),
	Hotkey('J', "signal_pbs_oneway", WID_BS_ELECTRIC_PBS_OWAY),
	Hotkey((uint16_t)0, "signal_no_entry", WID_BS_ELECTRIC_NO_ENTRY),
	HOTKEY_LIST_END
};
HotkeyList BuildSignalWindow::hotkeys("signaltoolbar", signaltoolbar_hotkeys);

/** Nested widget definition of the build signal window */
static constexpr NWidgetPart _nested_signal_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BS_CAPTION), SetDataTip(STR_BUILD_SIGNAL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_TOGGLE_SIZE_SEL),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_BS_TOGGLE_SIZE), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_BUILD_SIGNAL_TOGGLE_ADVANCED_SIGNAL_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_VERTICAL, NC_EQUALSIZE),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_NORM_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_NORM_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_ENTRY_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_ENTRY_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_EXIT_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_EXIT_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_COMBO_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_COMBO_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_PROG_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_PROG), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PROG_TOOLTIP), EndContainer(), SetFill(1, 1),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_SEMAPHORE_NOEN_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_SEMAPHORE_NO_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_NO_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			EndContainer(),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_BS_CONVERT), SetDataTip(SPR_IMG_SIGNAL_CONVERT, STR_BUILD_SIGNAL_CONVERT_TOOLTIP), SetFill(1, 1),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_BS_TRACE_RESTRICT), SetDataTip(SPR_IMG_SETTINGS, STR_TRACE_RESTRICT_SIGNAL_GUI_TOOLTIP), SetFill(1, 1),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_NORM_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_NORM_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_ENTRY_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_ENTRY_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_EXIT_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_EXIT_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_COMBO_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_COMBO_TOOLTIP), EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_PROG_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_PROG), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PROG_TOOLTIP), EndContainer(), SetFill(1, 1),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_ELECTRIC_NOEN_SEL),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BS_ELECTRIC_NO_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_NO_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetFill(1, 1),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BS_DRAG_SIGNALS_DENSITY_LABEL), SetDataTip(STR_JUST_INT, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetTextStyle(TC_ORANGE), SetFill(1, 1),
				NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_BS_DRAG_SIGNALS_DENSITY_DECREASE), SetMinimalSize(9, 12), SetDataTip(AWV_DECREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_DECREASE_TOOLTIP),
					NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_BS_DRAG_SIGNALS_DENSITY_INCREASE), SetMinimalSize(9, 12), SetDataTip(AWV_INCREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_INCREASE_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_PROGRAM_SEL),
				NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_BS_PROGRAM), SetDataTip(SPR_IMG_SETTINGS, STR_PROGRAM_SIGNAL_TOOLTIP), SetFill(1, 1),
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN), EndContainer(), SetFill(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BS_STYLE_SEL),
			NWidget(WWT_DROPDOWN, COLOUR_DARK_GREEN, WID_BS_STYLE), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_BUILD_SIGNAL_STYLE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

/** Signal selection window description */
static WindowDesc _signal_builder_desc(__FILE__, __LINE__,
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_SIGNAL, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	std::begin(_nested_signal_builder_widgets), std::end(_nested_signal_builder_widgets),
	&BuildSignalWindow::hotkeys
);

/**
 * Open the signal selection window
 */
static void ShowSignalBuilder(Window *parent)
{
	new BuildSignalWindow(&_signal_builder_desc, parent);
}

struct BuildRailDepotWindow : public PickerWindowBase {
	BuildRailDepotWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->InitNested(TRANSPORT_RAIL);
		this->LowerWidget(WID_BRAD_DEPOT_NE + _build_depot_direction);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (!IsInsideMM(widget, WID_BRAD_DEPOT_NE, WID_BRAD_DEPOT_NW + 1)) return;

		size.width  = ScaleGUITrad(64) + WidgetDimensions::scaled.fullbevel.Horizontal();
		size.height = ScaleGUITrad(48) + WidgetDimensions::scaled.fullbevel.Vertical();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (!IsInsideMM(widget, WID_BRAD_DEPOT_NE, WID_BRAD_DEPOT_NW + 1)) return;

		DrawPixelInfo tmp_dpi;
		Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
		if (FillDrawPixelInfo(&tmp_dpi, ir)) {
			AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
			int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
			int y = (ir.Height() + ScaleSpriteTrad(48)) / 2 - ScaleSpriteTrad(31);
			DrawTrainDepotSprite(x, y, widget - WID_BRAD_DEPOT_NE + DIAGDIR_NE, _cur_railtype);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BRAD_DEPOT_NE:
			case WID_BRAD_DEPOT_SE:
			case WID_BRAD_DEPOT_SW:
			case WID_BRAD_DEPOT_NW:
				this->RaiseWidget(WID_BRAD_DEPOT_NE + _build_depot_direction);
				_build_depot_direction = (DiagDirection)(widget - WID_BRAD_DEPOT_NE);
				this->LowerWidget(WID_BRAD_DEPOT_NE + _build_depot_direction);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
		}
	}
};

/** Nested widget definition of the build rail depot window */
static constexpr NWidgetPart _nested_build_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_BUILD_DEPOT_TRAIN_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1), SetPadding(WidgetDimensions::unscaled.picker),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAD_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAD_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAD_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BRAD_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _build_depot_desc(__FILE__, __LINE__,
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	std::begin(_nested_build_depot_widgets), std::end(_nested_build_depot_widgets)
);

static void ShowBuildTrainDepotPicker(Window *parent)
{
	new BuildRailDepotWindow(&_build_depot_desc, parent);
}

struct BuildRailWaypointWindow : PickerWindowBase {
	using WaypointList = GUIList<uint>;
	static const uint FILTER_LENGTH = 20;

	const StationClass *waypoints;
	WaypointList list;
	StringFilter string_filter; ///< Filter for waypoint name
	static QueryString editbox; ///< Filter editbox

	BuildRailWaypointWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->waypoints = StationClass::Get(STAT_CLASS_WAYP);

		this->CreateNestedTree();

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRW_WAYPOINT_MATRIX);
		matrix->SetScrollbar(this->GetScrollbar(WID_BRW_SCROLL));

		this->FinishInitNested(TRANSPORT_RAIL);

		this->querystrings[WID_BRW_FILTER] = &this->editbox;
		this->editbox.cancel_button = QueryString::ACTION_CLEAR;
		this->string_filter.SetFilterTerm(this->editbox.text.buf);

		this->list.ForceRebuild();
		this->BuildPickerList();
	}

	bool FilterByText(const StationSpec *statspec)
	{
		if (this->string_filter.IsEmpty()) return true;
		this->string_filter.ResetState();
		if (statspec == nullptr) {
			this->string_filter.AddLine(GetString(STR_STATION_CLASS_WAYP_WAYPOINT));
		} else {
			this->string_filter.AddLine(GetString(statspec->name));
			if (statspec->grf_prop.grffile != nullptr) {
				const GRFConfig *gc = GetGRFConfig(statspec->grf_prop.grffile->grfid);
				this->string_filter.AddLine(gc->GetName());
			}
		}
		return this->string_filter.GetState();
	}

	void BuildPickerList()
	{
		if (!this->list.NeedRebuild()) return;

		this->list.clear();
		this->list.reserve(this->waypoints->GetSpecCount());
		for (uint i = 0; i < this->waypoints->GetSpecCount(); i++) {
			const StationSpec *statspec = this->waypoints->GetSpec(i);
			if (!FilterByText(statspec)) continue;

			this->list.push_back(i);
		}
		this->list.RebuildDone();

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BRW_WAYPOINT_MATRIX);
		matrix->SetCount((int)this->list.size());
		matrix->SetClicked(this->UpdateSelection(_cur_waypoint_type));
	}

	uint UpdateSelection(uint type)
	{
		auto found = std::find(std::begin(this->list), std::end(this->list), type);
		if (found != std::end(this->list)) return found - std::begin(this->list);

		/* Selection isn't in the list, default to first */
		if (this->list.empty()) {
			_cur_waypoint_type = 0;
			return -1;
		} else {
			_cur_waypoint_type = this->list.front();
			return 0;
		}
	}

	void Close(int data = 0) override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindowBase::Close(data);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_BRW_WAYPOINT_MATRIX:
				/* Two blobs high and three wide. */
				size.width  += resize.width  * 2;
				size.height += resize.height * 1;

				/* Resizing in X direction only at blob size, but at pixel level in Y. */
				resize.height = 1;
				break;

			case WID_BRW_WAYPOINT:
				size.width  = ScaleGUITrad(64) + WidgetDimensions::scaled.fullbevel.Horizontal();
				size.height = ScaleGUITrad(58) + WidgetDimensions::scaled.fullbevel.Vertical();
				break;
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_BRW_NAME) {
			if (!this->list.empty() && IsInsideBS(_cur_waypoint_type, 0, this->waypoints->GetSpecCount())) {
				const StationSpec *statspec = this->waypoints->GetSpec(_cur_waypoint_type);
				if (statspec == nullptr) {
					SetDParam(0, STR_STATION_CLASS_WAYP_WAYPOINT);
				} else {
					SetDParam(0, statspec->name);
				}
			} else {
				SetDParam(0, STR_EMPTY);
			}
		}
	}

	void OnPaint() override
	{
		this->BuildPickerList();
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_BRW_WAYPOINT: {
				uint16_t type = this->list.at(this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->GetCurrentElement());
				const StationSpec *statspec = this->waypoints->GetSpec(type);

				DrawPixelInfo tmp_dpi;
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
					int y = (ir.Height() + ScaleSpriteTrad(58)) / 2 - ScaleSpriteTrad(31);
					DrawWaypointSprite(x, y, type, _cur_railtype);
				}

				if (!IsStationAvailable(statspec)) {
					GfxFillRect(ir, PC_BLACK, FILLRECT_CHECKER);
				}
			}
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BRW_WAYPOINT: {
				uint16_t sel = this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->GetCurrentElement();
				assert(sel < this->list.size());
				uint16_t type = this->list.at(sel);

				/* Check station availability callback */
				const StationSpec *statspec = this->waypoints->GetSpec(type);
				if (!IsStationAvailable(statspec)) return;

				_cur_waypoint_type = type;
				this->GetWidget<NWidgetBase>(widget)->GetParentWidget<NWidgetMatrix>()->SetClicked(sel);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
			}
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawWaypointCoverage(this, false);
	}

	void SelectWaypointSpec(uint16_t spec_id)
	{
		for (uint i = 0; i < (uint)this->list.size(); i++) {
			if (this->list[i] == spec_id) {
				this->GetWidget<NWidgetBase>(WID_BRW_WAYPOINT)->GetParentWidget<NWidgetMatrix>()->SetCurrentElement(i);
				this->OnClick({}, WID_BRW_WAYPOINT, 1);
				break;
			}
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		this->list.ForceRebuild();
	}

	void OnEditboxChanged(WidgetID wid) override
	{
		if (wid == WID_BRW_FILTER) {
			this->string_filter.SetFilterTerm(this->editbox.text.buf);
			this->InvalidateData();
		}
	}
};

/* static */ QueryString BuildRailWaypointWindow::editbox(BuildRailWaypointWindow::FILTER_LENGTH * MAX_CHAR_LENGTH, BuildRailWaypointWindow::FILTER_LENGTH);

/** Nested widget definition for the build NewGRF rail waypoint window */
static constexpr NWidgetPart _nested_build_waypoint_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(WWT_EDITBOX, COLOUR_DARK_GREEN, WID_BRW_FILTER), SetPadding(2), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_BRW_SCROLL),
			NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BRW_WAYPOINT_MATRIX), SetPIP(0, 2, 0), SetPadding(WidgetDimensions::unscaled.picker),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BRW_WAYPOINT), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), SetScrollbar(WID_BRW_SCROLL), EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BRW_SCROLL),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
			NWidget(WWT_TEXT, COLOUR_DARK_GREEN, WID_BRW_NAME), SetPadding(2), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_NULL), SetTextStyle(TC_ORANGE), SetAlignment(SA_CENTER),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
};

static WindowDesc _build_waypoint_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_waypoint", 0, 0,
	WC_BUILD_WAYPOINT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	std::begin(_nested_build_waypoint_widgets), std::end(_nested_build_waypoint_widgets)
);

static void ShowBuildWaypointPicker(Window *parent)
{
	new BuildRailWaypointWindow(&_build_waypoint_desc, parent);
}

/**
 * Initialize rail building GUI settings
 */
void InitializeRailGui()
{
	_build_depot_direction = DIAGDIR_NW;
	_railstation.station_class = StationClassID::STAT_CLASS_DFLT;
}

/**
 * Re-initialize rail-build toolbar after toggling support for electric trains
 * @param disable Boolean whether electric trains are disabled (removed from the game)
 */
void ReinitGuiAfterToggleElrail(bool disable)
{
	extern RailType _last_built_railtype;
	if (disable && _last_built_railtype == RAILTYPE_ELECTRIC) {
		_last_built_railtype = _cur_railtype = RAILTYPE_RAIL;
		BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
		if (w != nullptr) w->ModifyRailType(_cur_railtype);
	}
	MarkWholeScreenDirty();
}

/** Set the initial (default) railtype to use */
static void SetDefaultRailGui()
{
	if (_local_company == COMPANY_SPECTATOR || !Company::IsValidID(_local_company)) return;

	extern RailType _last_built_railtype;
	RailType rt;
	switch (_settings_client.gui.default_rail_type) {
		case 2: {
			/* Find the most used rail type */
			uint count[RAILTYPE_END];
			memset(count, 0, sizeof(count));
			for (TileIndex t = 0; t < MapSize(); t++) {
				if (IsTileType(t, MP_RAILWAY) || IsLevelCrossingTile(t) || HasStationTileRail(t) ||
						(IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL)) {
					count[GetRailType(t)]++;
				}
			}

			rt = static_cast<RailType>(std::max_element(count + RAILTYPE_BEGIN, count + RAILTYPE_END) - count);
			if (count[rt] > 0) break;

			/* No rail, just get the first available one */
			[[fallthrough]];
		}
		case 0: {
			/* Use first available type */
			std::vector<RailType>::const_iterator it = std::find_if(_sorted_railtypes.begin(), _sorted_railtypes.end(),
					[](RailType r){ return HasRailTypeAvail(_local_company, r); });
			rt = it != _sorted_railtypes.end() ? *it : RAILTYPE_BEGIN;
			break;
		}
		case 1: {
			/* Use last available type */
			std::vector<RailType>::const_reverse_iterator it = std::find_if(_sorted_railtypes.rbegin(), _sorted_railtypes.rend(),
					[](RailType r){ return HasRailTypeAvail(_local_company, r); });
			rt = it != _sorted_railtypes.rend() ? *it : RAILTYPE_BEGIN;
			break;
		}
		default:
			NOT_REACHED();
	}

	_last_built_railtype = _cur_railtype = rt;
	BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
	if (w != nullptr) w->ModifyRailType(_cur_railtype);
}

/**
 * Updates the current signal variant used in the signal GUI
 * to the one adequate to current year.
 */
void ResetSignalVariant(int32_t new_value)
{
	SignalVariant new_variant = (CalTime::CurYear() < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC);

	if (new_variant != _cur_signal_variant) {
		Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);
		if (w != nullptr) {
			w->SetDirty();
			w->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? WID_BS_ELECTRIC_NORM : WID_BS_SEMAPHORE_NORM) + _cur_signal_button);
		}
		_cur_signal_variant = new_variant;
	}
}

/**
 * Resets the rail GUI - sets default railtype to build
 * and resets the signal GUI
 */
void InitializeRailGUI()
{
	SetDefaultRailGui();

	_convert_signal_button = false;
	_trace_restrict_button = false;
	_program_signal_button = false;
	_cur_signal_type   = GetDefaultSignalType();
	_cur_signal_button =
		_cur_signal_type == SIGTYPE_PROG ? 4 :
		_cur_signal_type == SIGTYPE_PBS ? 5 :
		_cur_signal_type == SIGTYPE_PBS_ONEWAY ? 6 :
		_cur_signal_type == SIGTYPE_NO_ENTRY ? 7 : _cur_signal_type;
	ResetSignalVariant();
}

/**
 * Create a drop down list for all the rail types of the local company.
 * @param for_replacement Whether this list is for the replacement window.
 * @param all_option Whether to add an 'all types' item.
 * @return The populated and sorted #DropDownList.
 */
DropDownList GetRailTypeDropDownList(bool for_replacement, bool all_option)
{
	RailTypes used_railtypes;
	RailTypes avail_railtypes;

	const Company *c = Company::Get(_local_company);

	/* Find the used railtypes. */
	if (for_replacement) {
		avail_railtypes = GetCompanyRailTypes(c->index, false);
		used_railtypes  = GetRailTypes(false);
	} else {
		avail_railtypes = c->avail_railtypes;
		used_railtypes  = GetRailTypes(true);
	}

	DropDownList list;

	if (all_option) {
		list.push_back(MakeDropDownListStringItem(STR_REPLACE_ALL_RAILTYPE, INVALID_RAILTYPE));
	}

	Dimension d = { 0, 0 };
	/* Get largest icon size, to ensure text is aligned on each menu item. */
	if (!for_replacement) {
		for (const auto &rt : _sorted_railtypes) {
			if (!HasBit(used_railtypes, rt)) continue;
			const RailTypeInfo *rti = GetRailTypeInfo(rt);
			d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_rail));
		}
	}

	for (const auto &rt : _sorted_railtypes) {
		/* If it's not used ever, don't show it to the user. */
		if (!HasBit(used_railtypes, rt)) continue;

		const RailTypeInfo *rti = GetRailTypeInfo(rt);

		SetDParam(0, rti->strings.menu_text);
		SetDParam(1, rti->max_speed);
		if (for_replacement) {
			list.push_back(MakeDropDownListStringItem(rti->strings.replace_text, rt, !HasBit(avail_railtypes, rt)));
		} else {
			StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
			list.push_back(MakeDropDownListIconItem(d, rti->gui_sprites.build_x_rail, PAL_NONE, str, rt, !HasBit(avail_railtypes, rt)));
		}
	}

	if (list.empty()) {
		/* Empty dropdowns are not allowed */
		list.push_back(MakeDropDownListStringItem(STR_NONE, INVALID_RAILTYPE, true));
	}

	return list;
}

void ShowBuildRailStationPickerAndSelect(StationType station_type, const StationSpec *spec)
{
	if (!IsStationAvailable(spec)) return;

	StationClassID class_id;
	if (spec != nullptr) {
		if ((spec->cls_id == STAT_CLASS_WAYP) != (station_type == STATION_WAYPOINT)) return;
		class_id = spec->cls_id;
	} else {
		class_id = (station_type == STATION_ROADWAYPOINT) ? STAT_CLASS_WAYP : STAT_CLASS_DFLT;
	}

	int spec_id = -1;
	const StationClass *stclass = StationClass::Get(class_id);
	for (int i = 0; i < (int)stclass->GetSpecCount(); i++) {
		if (stclass->GetSpec(i) == spec) {
			spec_id = i;
		}
	}
	if (spec_id < 0) return;


	Window *w = FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);
	if (w == nullptr) {
		extern RailType _last_built_railtype;
		w = ShowBuildRailToolbar(_last_built_railtype);
	}
	if (w == nullptr) return;

	auto trigger_widget = [&](WidgetID widget) {
		if (!w->IsWidgetLowered(widget)) {
			w->OnHotkey(widget);
		}
	};

	if (station_type == STATION_WAYPOINT) {
		trigger_widget(WID_RAT_BUILD_WAYPOINT);

		BuildRailWaypointWindow *waypoint_window = dynamic_cast<BuildRailWaypointWindow *>(FindWindowById(WC_BUILD_WAYPOINT, TRANSPORT_RAIL));
		if (waypoint_window != nullptr) waypoint_window->SelectWaypointSpec((uint16_t)spec_id);
	} else {
		trigger_widget(WID_RAT_BUILD_STATION);

		BuildRailStationWindow *station_window = dynamic_cast<BuildRailStationWindow *>(FindWindowById(WC_BUILD_STATION, TRANSPORT_RAIL));
		if (station_window != nullptr) station_window->SelectClassAndSpec(class_id, spec_id);
	}
}

static void OpenBuildSignalWindow(BuildRailToolbarWindow *w, SignalVariant variant, SignalType type, uint8_t style)
{
	if (!w->IsWidgetLowered(WID_RAT_BUILD_SIGNALS)) {
		w->OnHotkey(WID_RAT_BUILD_SIGNALS);
	}

	BuildSignalWindow *signal_window = dynamic_cast<BuildSignalWindow *>(FindWindowById(WC_BUILD_SIGNAL, TRANSPORT_RAIL));
	if (signal_window == nullptr) return;

	signal_window->OnDropdownSelect(WID_BS_STYLE, style);

	if (_settings_client.gui.signal_gui_mode == SIGNAL_GUI_PATH && _settings_game.vehicle.train_braking_model != TBM_REALISTIC && !IsPbsSignalNonExtended(type) && !IsNoEntrySignal(type)) {
		signal_window->OnClick(Point(), WID_BS_TOGGLE_SIZE, 1);
	}

	signal_window->OnClick(Point(), ((variant == SIG_SEMAPHORE) ? WID_BS_SEMAPHORE_NORM : WID_BS_ELECTRIC_NORM) + BuildSignalWindow::ClickForType(type), 1);
}

void ShowBuildRailToolbarWithPickTile(RailType railtype, TileIndex tile)
{
	BuildRailToolbarWindow *w = static_cast<BuildRailToolbarWindow *>(ShowBuildRailToolbar(railtype));
	if (w == nullptr) return;

	if (IsPlainRailTile(tile) || IsRailTunnelBridgeTile(tile)) {
		TrackBits trackbits = TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0));
		if (trackbits & TRACK_BIT_VERT) { // N-S direction
			trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
		}

		if (trackbits & TRACK_BIT_HORZ) { // E-W direction
			trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
		}

		Track track = FindFirstTrack(trackbits);
		if (track != INVALID_TRACK) {
			if (IsTileType(tile, MP_RAILWAY) && HasTrack(tile, track) && HasSignalOnTrack(tile, track)) {
				OpenBuildSignalWindow(w, GetSignalVariant(tile, track), GetSignalType(tile, track), GetSignalStyle(tile, track));
			}
			if (IsRailTunnelBridgeTile(tile) && IsTunnelBridgeWithSignalSimulation(tile) && HasTrack(GetTunnelBridgeTrackBits(tile), track)) {
				OpenBuildSignalWindow(w, IsTunnelBridgeSemaphore(tile) ? SIG_SEMAPHORE : SIG_ELECTRIC,
						IsTunnelBridgePBS(tile) ? SIGTYPE_PBS_ONEWAY : SIGTYPE_BLOCK, GetTunnelBridgeSignalStyle(tile));
			}
		}
	}
}
