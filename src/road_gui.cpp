/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file road_gui.cpp GUI for building roads. */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "road_cmd.h"
#include "station_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "tunnelbridge.h"
#include "tunnelbridge_map.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "hotkeys.h"
#include "road_gui.h"
#include "zoom_func.h"
#include "engine_base.h"
#include "strings_func.h"
#include "core/geometry_func.hpp"
#include "date_func.h"
#include "station_map.h"
#include "waypoint_func.h"
#include "newgrf_roadstop.h"
#include "debug.h"
#include "newgrf_station.h"
#include "querystring_gui.h"
#include "sortlist_type.h"
#include "stringfilter_type.h"
#include "string_func.h"

#include "widgets/road_widget.h"
#include "table/strings.h"

#include <array>

#include "safeguards.h"

static void ShowRVStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);
static void ShowBuildWaypointPicker(Window *parent);

static bool _remove_button_clicked;
static bool _one_way_button_clicked;

static DiagDirection _build_depot_direction;

struct RoadStopGUISettings {
	DiagDirection orientation; // This replaces _road_station_picker_orientation

	RoadStopClassID roadstop_class;
	byte roadstop_type;
	byte roadstop_count;
};
static RoadStopGUISettings _roadstop_gui_settings;

static uint _waypoint_count = 1;             ///< Number of waypoint types
static uint _cur_waypoint_type;              ///< Currently selected waypoint type

/**
 * Define the values of the RoadFlags
 * @see CmdBuildLongRoad
 */
enum RoadFlags {
	RF_NONE             = 0x00,
	RF_START_HALFROAD_Y = 0x01,    // The start tile in Y-dir should have only a half road
	RF_END_HALFROAD_Y   = 0x02,    // The end tile in Y-dir should have only a half road
	RF_DIR_Y            = 0x04,    // The direction is Y-dir
	RF_DIR_X            = RF_NONE, // Dummy; Dir X is set when RF_DIR_Y is not set
	RF_START_HALFROAD_X = 0x08,    // The start tile in X-dir should have only a half road
	RF_END_HALFROAD_X   = 0x10,    // The end tile in X-dir should have only a half road
};
DECLARE_ENUM_AS_BIT_SET(RoadFlags)

static RoadFlags _place_road_flag;

static RoadType _cur_roadtype;


/**
 * Check whether a road stop type can be built.
 * @return true if building is allowed.
 */
static bool IsRoadStopAvailable(const RoadStopSpec *roadstopspec, StationType type)
{
	if (roadstopspec == nullptr || !HasBit(roadstopspec->callback_mask, CBM_ROAD_STOP_AVAIL)) return true;

	uint16 cb_res = GetRoadStopCallback(CBID_STATION_AVAILABILITY, 0, 0, roadstopspec, nullptr, INVALID_TILE, _cur_roadtype, type, 0);
	if (cb_res == CALLBACK_FAILED) return true;

	return Convert8bitBooleanCallback(roadstopspec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res);
}

void CcPlaySound_CONSTRUCTION_OTHER(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Succeeded() && _settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
}

/**
 * Callback to start placing a bridge.
 * @param tile Start tile of the bridge.
 */
static void PlaceRoad_Bridge(TileIndex tile, Window *w)
{
	if (IsBridgeTile(tile)) {
		TileIndex other_tile = GetOtherTunnelBridgeEnd(tile);
		Point pt = {0, 0};
		w->OnPlaceMouseUp(VPM_X_OR_Y, DDSP_BUILD_BRIDGE, pt, other_tile, tile);
	} else {
		VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
	}
}

/**
 * Callback executed after a build road tunnel command has been called.
 *
 * @param result Whether the build succeeded.
 * @param start_tile Starting tile of the tunnel.
 * @param p1 bit 0-3 railtype or roadtypes
 *           bit 8-9 transport type
 * @param p2 unused
 * @param cmd unused
 */
void CcBuildRoadTunnel(const CommandCost &result, TileIndex start_tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Succeeded()) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, start_tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

		DiagDirection start_direction = ReverseDiagDir(GetTunnelBridgeDirection(start_tile));
		ConnectRoadToStructure(start_tile, start_direction);

		TileIndex end_tile = GetOtherTunnelBridgeEnd(start_tile);
		DiagDirection end_direction = ReverseDiagDir(GetTunnelBridgeDirection(end_tile));
		ConnectRoadToStructure(end_tile, end_direction);
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

/**
 * If required, connects a new structure to an existing road or tram by building the missing roadbit.
 * @param tile Tile containing the structure to connect.
 * @param direction Direction to check.
 */
void ConnectRoadToStructure(TileIndex tile, DiagDirection direction)
{
	tile += TileOffsByDiagDir(direction);
	/* if there is a roadpiece just outside of the station entrance, build a connecting route */
	if (IsNormalRoadTile(tile)) {
		if (GetRoadBits(tile, GetRoadTramType(_cur_roadtype)) != ROAD_NONE) {
			DoCommandP(tile, _cur_roadtype << 4 | DiagDirToRoadBits(ReverseDiagDir(direction)), 0, CMD_BUILD_ROAD);
		}
	}
}

void CcRoadDepot(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)GB(p1, 0, 2);
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	ConnectRoadToStructure(tile, dir);
}

/**
 * Command callback for building road stops.
 * @param result Result of the build road stop command.
 * @param tile Start tile.
 * @param p1 bit 0..7: Width of the road stop.
 *           bit 8..15: Length of the road stop.
 * @param p2 bit 0: 0 For bus stops, 1 for truck stops.
 *           bit 1: 0 For normal stops, 1 for drive-through.
 *           bit 2: Allow stations directly adjacent to other stations.
 *           bit 3..4: Entrance direction (#DiagDirection) for normal stops.
 *           bit 3: #Axis of the road for drive-through stops.
 *           bit 5..9: The roadtype.
 *           bit 16..31: Station ID to join (NEW_STATION if build new one).
 * @param cmd Unused.
 * @see CmdBuildRoadStop
 */
void CcRoadStop(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)GB(p2, 3, 2);
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	TileArea roadstop_area(tile, GB(p1, 0, 8), GB(p1, 8, 8));
	for (TileIndex cur_tile : roadstop_area) {
		ConnectRoadToStructure(cur_tile, dir);
		/* For a drive-through road stop build connecting road for other entrance. */
		if (HasBit(p2, 1)) ConnectRoadToStructure(cur_tile, ReverseDiagDir(dir));
	}
}

/**
 * Place a new road stop.
 * @param start_tile First tile of the area.
 * @param end_tile Last tile of the area.
 * @param p2 bit 0: 0 For bus stops, 1 for truck stops.
 *           bit 2: Allow stations directly adjacent to other stations.
 *           bit 5..10: The roadtypes.
 * @param cmd Command to use.
 * @see CcRoadStop()
 */
static void PlaceRoadStop(TileIndex start_tile, TileIndex end_tile, uint32 p2, uint32 cmd)
{
	uint8 ddir = _roadstop_gui_settings.orientation;

	if (ddir >= DIAGDIR_END) {
		SetBit(p2, 1); // It's a drive-through stop.
		ddir -= DIAGDIR_END; // Adjust picker result to actual direction.
	}
	p2 |= ddir << 3; // Set the DiagDirecion into p2 bits 3 and 4.
	p2 |= INVALID_STATION << 16; // no station to join

	TileArea ta(start_tile, end_tile);
	CommandContainer cmdcont = NewCommandContainerBasic(ta.tile, (uint32)(ta.w | ta.h << 8), p2, cmd, CcRoadStop);
	cmdcont.p3 = (_roadstop_gui_settings.roadstop_type << 8) | _roadstop_gui_settings.roadstop_class;
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/**
 * Place a road waypoint.
 * @param tile Position to start dragging a waypoint.
 */
static void PlaceRoad_Waypoint(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_ROAD_WAYPOINT);
		return;
	}

	Axis axis = GetAxisForNewRoadWaypoint(tile);
	if (IsValidAxis(axis)) {
		/* Valid tile for waypoints */
		VpStartPlaceSizing(tile, axis == AXIS_X ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_ROAD_WAYPOINT);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		/* Tile where we can't build rail waypoints. This is always going to fail,
		 * but provides the user with a proper error message. */
		DoCommandP(tile, 1 | 1 << 8, ROADSTOP_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_ROAD_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT));
	}
}

/**
 * Callback for placing a bus station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_BusStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_BUSSTOP);
	} else {
		if (_roadstop_gui_settings.orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui_settings.orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_BUSSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

/**
 * Callback for placing a truck station.
 * @param tile Position to place the station.
 */
static void PlaceRoad_TruckStation(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_TRUCKSTOP);
	} else {
		if (_roadstop_gui_settings.orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui_settings.orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		} else {
			VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
		}
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	}
}

typedef void OnButtonClick(Window *w);

/**
 * Toggles state of the Remove button of Build road toolbar
 * @param w window the button belongs to
 */
static void ToggleRoadButton_Remove(Window *w)
{
	w->ToggleWidgetLoweredState(WID_ROT_REMOVE);
	w->SetWidgetDirty(WID_ROT_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(WID_ROT_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/**
 * Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove button was changed
 */
static bool RoadToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(WID_ROT_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = WID_ROT_ROAD_X; i <= WID_ROT_AUTOROAD; i++) {
		if (w->IsWidgetLowered(i)) {
			ToggleRoadButton_Remove(w);
			return true;
		}
	}

	return false;
}

/** Road toolbar window handler. */
struct BuildRoadToolbarWindow : Window {
	RoadType roadtype;          ///< Road type to build.
	const RoadTypeInfo *rti;    ///< Information about current road type
	int last_started_action;    ///< Last started user action.

	BuildRoadToolbarWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->Initialize(_cur_roadtype);
		this->InitNested(window_number);
		this->SetupRoadToolbar();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
		}

		this->OnInvalidateData();
		this->last_started_action = WIDGET_LIST_END;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildRoadToolbarWindow()
	{
		if (_game_mode == GM_NORMAL && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;
		RoadTramType rtt = GetRoadTramType(this->roadtype);

		bool can_build = CanBuildVehicleInfrastructure(VEH_ROAD, rtt);
		this->SetWidgetsDisabledState(!can_build,
			WID_ROT_DEPOT,
			WID_ROT_BUILD_WAYPOINT,
			WID_ROT_BUS_STATION,
			WID_ROT_TRUCK_STATION,
			WIDGET_LIST_END);
		if (!can_build) {
			DeleteWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
			DeleteWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
			DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
			DeleteWindowById(WC_BUILD_WAYPOINT, TRANSPORT_ROAD);
		}

		if (_game_mode != GM_EDITOR) {
			if (!can_build) {
				/* Show in the tooltip why this button is disabled. */
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_BUILD_WAYPOINT)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(STR_TOOLBAR_DISABLED_NO_VEHICLE_AVAILABLE);
			} else {
				this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT);
				this->GetWidget<NWidgetCore>(WID_ROT_BUILD_WAYPOINT)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT : STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM_TO_WAYPOINT);
				this->GetWidget<NWidgetCore>(WID_ROT_BUS_STATION)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION);
				this->GetWidget<NWidgetCore>(WID_ROT_TRUCK_STATION)->SetToolTip(rtt == RTT_ROAD ? STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY : STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION);
			}
		}
	}

	void Initialize(RoadType roadtype)
	{
		assert(roadtype < ROADTYPE_END);
		this->roadtype = roadtype;
		this->rti = GetRoadTypeInfo(this->roadtype);
	}

	/**
	 * Configures the road toolbar for roadtype given
	 * @param roadtype the roadtype to display
	 */
	void SetupRoadToolbar()
	{
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_X)->widget_data = rti->gui_sprites.build_x_road;
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_Y)->widget_data = rti->gui_sprites.build_y_road;
		this->GetWidget<NWidgetCore>(WID_ROT_AUTOROAD)->widget_data = rti->gui_sprites.auto_road;
		if (_game_mode != GM_EDITOR) {
			this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->widget_data = rti->gui_sprites.build_depot;
		}
		this->GetWidget<NWidgetCore>(WID_ROT_CONVERT_ROAD)->widget_data = rti->gui_sprites.convert_road;
		this->GetWidget<NWidgetCore>(WID_ROT_BUILD_TUNNEL)->widget_data = rti->gui_sprites.build_tunnel;
	}

	/**
	 * Switch to another road type.
	 * @param roadtype New road type.
	 */
	void ModifyRoadType(RoadType roadtype)
	{
		this->Initialize(roadtype);
		this->SetupRoadToolbar();
		this->ReInit();
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_ROT_CAPTION) {
			if (this->rti->max_speed > 0) {
				SetDParam(0, STR_TOOLBAR_RAILTYPE_VELOCITY);
				SetDParam(1, this->rti->strings.toolbar_caption);
				SetDParam(2, this->rti->max_speed / 2);
			} else {
				SetDParam(0, this->rti->strings.toolbar_caption);
			}
		}
	}

	/**
	 * Update the remove button lowered state of the road toolbar
	 *
	 * @param clicked_widget The widget which the client clicked just now
	 */
	void UpdateOptionWidgetStatus(RoadToolbarWidgets clicked_widget)
	{
		/* The remove and the one way button state is driven
		 * by the other buttons so they don't act on themselves.
		 * Both are only valid if they are able to apply as options. */
		switch (clicked_widget) {
			case WID_ROT_REMOVE:
				if (RoadTypeIsRoad(this->roadtype)) {
					this->RaiseWidget(WID_ROT_ONE_WAY);
					this->SetWidgetDirty(WID_ROT_ONE_WAY);
				}

				break;

			case WID_ROT_ONE_WAY:
				this->RaiseWidget(WID_ROT_REMOVE);
				this->SetWidgetDirty(WID_ROT_REMOVE);
				break;

			case WID_ROT_BUS_STATION:
			case WID_ROT_TRUCK_STATION:
			case WID_ROT_BUILD_WAYPOINT:
				if (RoadTypeIsRoad(this->roadtype)) this->DisableWidget(WID_ROT_ONE_WAY);
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			case WID_ROT_ROAD_X:
			case WID_ROT_ROAD_Y:
			case WID_ROT_AUTOROAD:
				this->SetWidgetDisabledState(WID_ROT_REMOVE, !this->IsWidgetLowered(clicked_widget));
				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, !this->IsWidgetLowered(clicked_widget));
				}
				break;

			default:
				/* When any other buttons than road/station, raise and
				 * disable the removal button */
				this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
				this->SetWidgetLoweredState(WID_ROT_REMOVE, false);

				if (RoadTypeIsRoad(this->roadtype)) {
					this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
					this->SetWidgetLoweredState(WID_ROT_ONE_WAY, false);
				}

				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		_remove_button_clicked = false;
		_one_way_button_clicked = false;
		switch (widget) {
			case WID_ROT_ROAD_X:
				HandlePlacePushButton(this, WID_ROT_ROAD_X, this->rti->cursor.road_nwse, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_ROAD_Y:
				HandlePlacePushButton(this, WID_ROT_ROAD_Y, this->rti->cursor.road_swne, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_AUTOROAD:
				HandlePlacePushButton(this, WID_ROT_AUTOROAD, this->rti->cursor.autoroad, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEMOLISH:
				HandlePlacePushButton(this, WID_ROT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEPOT:
				if (HandlePlacePushButton(this, WID_ROT_DEPOT, this->rti->cursor.depot, HT_RECT)) {
					ShowRoadDepotPicker(this);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_BUILD_WAYPOINT:
				if (HandlePlacePushButton(this, WID_ROT_BUILD_WAYPOINT, SPR_CURSOR_WAYPOINT, HT_RECT)) {
					this->last_started_action = widget;
					_waypoint_count = RoadStopClass::Get(ROADSTOP_CLASS_WAYP)->GetSpecCount();
					if (_waypoint_count > 1) ShowBuildWaypointPicker(this);
				}
				break;

			case WID_ROT_BUS_STATION:
				if (HandlePlacePushButton(this, WID_ROT_BUS_STATION, SPR_CURSOR_BUS_STATION, HT_RECT)) {
					ShowRVStationPicker(this, ROADSTOP_BUS);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_TRUCK_STATION:
				if (HandlePlacePushButton(this, WID_ROT_TRUCK_STATION, SPR_CURSOR_TRUCK_STATION, HT_RECT)) {
					ShowRVStationPicker(this, ROADSTOP_TRUCK);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_ONE_WAY:
				if (this->IsWidgetDisabled(WID_ROT_ONE_WAY)) return;
				this->SetDirty();
				this->ToggleWidgetLoweredState(WID_ROT_ONE_WAY);
				SetSelectionRed(false);
				break;

			case WID_ROT_BUILD_BRIDGE:
				HandlePlacePushButton(this, WID_ROT_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_BUILD_TUNNEL:
				HandlePlacePushButton(this, WID_ROT_BUILD_TUNNEL, this->rti->cursor.tunnel, HT_SPECIAL | HT_TUNNEL);
				this->last_started_action = widget;
				break;

			case WID_ROT_REMOVE:
				if (this->IsWidgetDisabled(WID_ROT_REMOVE)) return;

				DeleteWindowById(WC_SELECT_STATION, 0);
				ToggleRoadButton_Remove(this);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				break;

			case WID_ROT_CONVERT_ROAD:
				HandlePlacePushButton(this, WID_ROT_CONVERT_ROAD, this->rti->cursor.convert_road, HT_RECT);
				this->last_started_action = widget;
				break;

			default: NOT_REACHED();
		}
		this->UpdateOptionWidgetStatus((RoadToolbarWidgets)widget);
		if (_ctrl_pressed) RoadToolbar_CtrlChanged(this);
	}

	EventState OnHotkey(int hotkey) override
	{
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return Window::OnHotkey(hotkey);
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		_remove_button_clicked = this->IsWidgetLowered(WID_ROT_REMOVE);
		_one_way_button_clicked = RoadTypeIsRoad(this->roadtype) ? this->IsWidgetLowered(WID_ROT_ONE_WAY) : false;
		switch (this->last_started_action) {
			case WID_ROT_ROAD_X:
				_place_road_flag = RF_DIR_X;
				if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
				VpStartPlaceSizing(tile, VPM_FIX_Y, DDSP_PLACE_ROAD_X_DIR);
				break;

			case WID_ROT_ROAD_Y:
				_place_road_flag = RF_DIR_Y;
				if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
				VpStartPlaceSizing(tile, VPM_FIX_X, DDSP_PLACE_ROAD_Y_DIR);
				break;

			case WID_ROT_AUTOROAD:
				_place_road_flag = RF_NONE;
				if (_tile_fract_coords.x >= 8) _place_road_flag |= RF_START_HALFROAD_X;
				if (_tile_fract_coords.y >= 8) _place_road_flag |= RF_START_HALFROAD_Y;
				VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_PLACE_AUTOROAD);
				break;

			case WID_ROT_DEMOLISH:
				PlaceProc_DemolishArea(tile);
				break;

			case WID_ROT_DEPOT:
				DoCommandP(tile, _cur_roadtype << 2 | _build_depot_direction, 0,
						CMD_BUILD_ROAD_DEPOT | CMD_MSG(this->rti->strings.err_depot), CcRoadDepot);
				break;

			case WID_ROT_BUILD_WAYPOINT:
				PlaceRoad_Waypoint(tile);
				break;

			case WID_ROT_BUS_STATION:
				PlaceRoad_BusStation(tile);
				break;

			case WID_ROT_TRUCK_STATION:
				PlaceRoad_TruckStation(tile);
				break;

			case WID_ROT_BUILD_BRIDGE:
				PlaceRoad_Bridge(tile, this);
				break;

			case WID_ROT_BUILD_TUNNEL:
				DoCommandP(tile, _cur_roadtype | (TRANSPORT_ROAD << 8), 0,
						CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRoadTunnel);
				break;

			case WID_ROT_CONVERT_ROAD:
				VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_ROAD);
				break;

			default: NOT_REACHED();
		}
	}

	void OnPlaceObjectAbort() override
	{
		if (_game_mode != GM_EDITOR && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);

		this->RaiseButtons();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
		this->SetWidgetDirty(WID_ROT_REMOVE);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
			this->SetWidgetDirty(WID_ROT_ONE_WAY);
		}

		DeleteWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
		DeleteWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
		DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
		DeleteWindowById(WC_BUILD_WAYPOINT, TRANSPORT_ROAD);
		DeleteWindowById(WC_SELECT_STATION, 0);
		DeleteWindowByClass(WC_BUILD_BRIDGE);
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt) override
	{
		/* Here we update the end tile flags
		 * of the road placement actions.
		 * At first we reset the end halfroad
		 * bits and if needed we set them again. */
		switch (select_proc) {
			case DDSP_PLACE_ROAD_X_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_X;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;
				break;

			case DDSP_PLACE_ROAD_Y_DIR:
				_place_road_flag &= ~RF_END_HALFROAD_Y;
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				break;

			case DDSP_PLACE_AUTOROAD:
				_place_road_flag &= ~(RF_END_HALFROAD_Y | RF_END_HALFROAD_X);
				if (pt.y & 8) _place_road_flag |= RF_END_HALFROAD_Y;
				if (pt.x & 8) _place_road_flag |= RF_END_HALFROAD_X;

				/* For autoroad we need to update the
				 * direction of the road */
				if (_thd.size.x > _thd.size.y || (_thd.size.x == _thd.size.y &&
						( (_tile_fract_coords.x < _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) < 16) ||
						(_tile_fract_coords.x > _tile_fract_coords.y && (_tile_fract_coords.x + _tile_fract_coords.y) > 16) ))) {
					/* Set dir = X */
					_place_road_flag &= ~RF_DIR_Y;
				} else {
					/* Set dir = Y */
					_place_road_flag |= RF_DIR_Y;
				}

				break;

			default:
				break;
		}

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_ROAD, _cur_roadtype);
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_PLACE_ROAD_X_DIR:
				case DDSP_PLACE_ROAD_Y_DIR:
				case DDSP_PLACE_AUTOROAD:
					/* Flag description:
					 * Use the first three bits (0x07) if dir == Y
					 * else use the last 2 bits (X dir has
					 * not the 3rd bit set) */

					/* Even if _cur_roadtype_id is a uint8 we only use 5 bits so
					 * we could ignore the last 3 bits and reuse them for other
					 * flags */
					_place_road_flag = (RoadFlags)((_place_road_flag & RF_DIR_Y) ? (_place_road_flag & 0x07) : (_place_road_flag >> 3));

					DoCommandP(start_tile, end_tile, _place_road_flag | (_cur_roadtype << 3) | (_one_way_button_clicked << 10),
							_remove_button_clicked ?
							CMD_REMOVE_LONG_ROAD | CMD_MSG(this->rti->strings.err_remove_road) :
							CMD_BUILD_LONG_ROAD | CMD_MSG(this->rti->strings.err_build_road), CcPlaySound_CONSTRUCTION_OTHER);
					break;

				case DDSP_BUILD_ROAD_WAYPOINT:
				case DDSP_REMOVE_ROAD_WAYPOINT:
					if (this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) {
						TileArea ta(start_tile, end_tile);
						if (_remove_button_clicked) {
							DoCommandP(ta.tile, ta.w | ta.h << 8, (1 << 2), CMD_REMOVE_ROAD_STOP | CMD_MSG(STR_ERROR_CAN_T_REMOVE_ROAD_WAYPOINT), CcPlaySound_CONSTRUCTION_OTHER);
						} else {
							uint32 p1 = ta.w | ta.h << 8 | _ctrl_pressed << 16 | (select_method == VPM_X_LIMITED ? AXIS_X : AXIS_Y) << 17;
							uint32 p2 = ROADSTOP_CLASS_WAYP | INVALID_STATION << 16;

							CommandContainer cmdcont = NewCommandContainerBasic(ta.tile, p1, p2, CMD_BUILD_ROAD_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT), CcPlaySound_CONSTRUCTION_OTHER);
							cmdcont.p3 = _cur_waypoint_type;
							ShowSelectWaypointIfNeeded(cmdcont, ta);
						}
					}
					break;

				case DDSP_BUILD_BUSSTOP:
				case DDSP_REMOVE_BUSSTOP:
					if (this->IsWidgetLowered(WID_ROT_BUS_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui_settings.roadstop_class), ROADSTOP_BUS)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_BUS, CMD_REMOVE_ROAD_STOP | CMD_MSG(this->rti->strings.err_remove_station[ROADSTOP_BUS]), CcPlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, (_cur_roadtype << 5) | (_ctrl_pressed << 2) | ROADSTOP_BUS, CMD_BUILD_ROAD_STOP | CMD_MSG(this->rti->strings.err_build_station[ROADSTOP_BUS]));
						}
					}
					break;

				case DDSP_BUILD_TRUCKSTOP:
				case DDSP_REMOVE_TRUCKSTOP:
					if (this->IsWidgetLowered(WID_ROT_TRUCK_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui_settings.roadstop_class), ROADSTOP_TRUCK)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandP(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | ROADSTOP_TRUCK, CMD_REMOVE_ROAD_STOP | CMD_MSG(this->rti->strings.err_remove_station[ROADSTOP_TRUCK]), CcPlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, (_cur_roadtype << 5) | (_ctrl_pressed << 2) | ROADSTOP_TRUCK, CMD_BUILD_ROAD_STOP | CMD_MSG(this->rti->strings.err_build_station[ROADSTOP_TRUCK]));
						}
					}
					break;

				case DDSP_CONVERT_ROAD:
					DoCommandP(end_tile, start_tile, _cur_roadtype, CMD_CONVERT_ROAD | CMD_MSG(rti->strings.err_convert_road), CcPlaySound_CONSTRUCTION_OTHER);
					break;
			}
		}
	}

	void OnPlacePresize(Point pt, TileIndex tile) override
	{
		DoCommand(tile, _cur_roadtype | (TRANSPORT_ROAD << 8), 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		if (RoadToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	static HotkeyList road_hotkeys;
	static HotkeyList tram_hotkeys;
};

/**
 * Handler for global hotkeys of the BuildRoadToolbarWindow.
 * @param hotkey Hotkey
 * @param last_build Last build road type
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RoadTramToolbarGlobalHotkeys(int hotkey, RoadType last_build, RoadTramType rtt)
{
	Window* w = nullptr;
	switch (_game_mode) {
		case GM_NORMAL:
			w = ShowBuildRoadToolbar(last_build);
			break;

		case GM_EDITOR:
			if ((GetRoadTypes(true) & ((rtt == RTT_ROAD) ? ~_roadtypes_type : _roadtypes_type)) == ROADTYPES_NONE) return ES_NOT_HANDLED;
			w = ShowBuildRoadScenToolbar(last_build);
			break;

		default:
			break;
	}

	if (w == nullptr) return ES_NOT_HANDLED;
	return w->OnHotkey(hotkey);
}

static EventState RoadToolbarGlobalHotkeys(int hotkey)
{
	extern RoadType _last_built_roadtype;
	return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_roadtype, RTT_ROAD);
}

static EventState TramToolbarGlobalHotkeys(int hotkey)
{
	extern RoadType _last_built_tramtype;
	return RoadTramToolbarGlobalHotkeys(hotkey, _last_built_tramtype, RTT_TRAM);
}

static Hotkey roadtoolbar_hotkeys[] = {
	Hotkey('1', "build_x", WID_ROT_ROAD_X),
	Hotkey('2', "build_y", WID_ROT_ROAD_Y),
	Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
	Hotkey('4', "demolish", WID_ROT_DEMOLISH),
	Hotkey('5', "depot", WID_ROT_DEPOT),
	Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
	Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
	Hotkey('8', "oneway", WID_ROT_ONE_WAY),
	Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_ROT_REMOVE),
	Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
	Hotkey('9', "waypoint", WID_ROT_BUILD_WAYPOINT),
	HOTKEY_LIST_END
};
HotkeyList BuildRoadToolbarWindow::road_hotkeys("roadtoolbar", roadtoolbar_hotkeys, RoadToolbarGlobalHotkeys);

static Hotkey tramtoolbar_hotkeys[] = {
	Hotkey('1', "build_x", WID_ROT_ROAD_X),
	Hotkey('2', "build_y", WID_ROT_ROAD_Y),
	Hotkey('3', "autoroad", WID_ROT_AUTOROAD),
	Hotkey('4', "demolish", WID_ROT_DEMOLISH),
	Hotkey('5', "depot", WID_ROT_DEPOT),
	Hotkey('6', "bus_station", WID_ROT_BUS_STATION),
	Hotkey('7', "truck_station", WID_ROT_TRUCK_STATION),
	Hotkey('B', "bridge", WID_ROT_BUILD_BRIDGE),
	Hotkey('T', "tunnel", WID_ROT_BUILD_TUNNEL),
	Hotkey('R', "remove", WID_ROT_REMOVE),
	Hotkey('C', "convert", WID_ROT_CONVERT_ROAD),
	Hotkey('9', "waypoint", WID_ROT_BUILD_WAYPOINT),
	HOTKEY_LIST_END
};
HotkeyList BuildRoadToolbarWindow::tram_hotkeys("tramtoolbar", tramtoolbar_hotkeys, TramToolbarGlobalHotkeys);


static const NWidgetPart _nested_build_road_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_road", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_widgets, lengthof(_nested_build_road_widgets),
	&BuildRoadToolbarWindow::road_hotkeys
);

static const NWidgetPart _nested_build_tramway_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_desc(
	WDP_ALIGN_TOOLBAR, "toolbar_tramway", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_widgets, lengthof(_nested_build_tramway_widgets),
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Open the build road toolbar window
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @return newly opened road toolbar, or nullptr if the toolbar could not be opened.
 */
Window *ShowBuildRoadToolbar(RoadType roadtype)
{
	if (!Company::IsValidID(_local_company)) return nullptr;
	if (!ValParamRoadType(roadtype)) return nullptr;

	DeleteWindowByClass(WC_BUILD_TOOLBAR);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? &_build_road_desc : &_build_tramway_desc, TRANSPORT_ROAD);
}

static const NWidgetPart _nested_build_road_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_scen_desc(
	WDP_AUTO, "toolbar_road_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_scen_widgets, lengthof(_nested_build_road_scen_widgets),
	&BuildRoadToolbarWindow::road_hotkeys
);

static const NWidgetPart _nested_build_tramway_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetDataTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_scen_desc(
	WDP_AUTO, "toolbar_tram_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_scen_widgets, lengthof(_nested_build_tramway_scen_widgets),
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Show the road building toolbar in the scenario editor.
 * @return The just opened toolbar, or \c nullptr if the toolbar was already open.
 */
Window *ShowBuildRoadScenToolbar(RoadType roadtype)
{
	DeleteWindowById(WC_SCEN_BUILD_TOOLBAR, TRANSPORT_ROAD);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? &_build_road_scen_desc : &_build_tramway_scen_desc, TRANSPORT_ROAD);
}

struct BuildRoadDepotWindow : public PickerWindowBase {
	BuildRoadDepotWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		this->LowerWidget(_build_depot_direction + WID_BROD_DEPOT_NE);
		if (RoadTypeIsTram(_cur_roadtype)) {
			this->GetWidget<NWidgetCore>(WID_BROD_CAPTION)->widget_data = STR_BUILD_DEPOT_TRAM_ORIENTATION_CAPTION;
			for (int i = WID_BROD_DEPOT_NE; i <= WID_BROD_DEPOT_NW; i++) this->GetWidget<NWidgetCore>(i)->tool_tip = STR_BUILD_DEPOT_TRAM_ORIENTATION_SELECT_TOOLTIP;
		}

		this->FinishInitNested(TRANSPORT_ROAD);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		size->width  = ScaleGUITrad(64) + 2;
		size->height = ScaleGUITrad(48) + 2;
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		DrawRoadDepotSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), (DiagDirection)(widget - WID_BROD_DEPOT_NE + DIAGDIR_NE), _cur_roadtype);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BROD_DEPOT_NW:
			case WID_BROD_DEPOT_NE:
			case WID_BROD_DEPOT_SW:
			case WID_BROD_DEPOT_SE:
				this->RaiseWidget(_build_depot_direction + WID_BROD_DEPOT_NE);
				_build_depot_direction = (DiagDirection)(widget - WID_BROD_DEPOT_NE);
				this->LowerWidget(_build_depot_direction + WID_BROD_DEPOT_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}
};

static const NWidgetPart _nested_build_road_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BROD_CAPTION), SetDataTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, WID_BROD_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static WindowDesc _build_road_depot_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_road_depot_widgets, lengthof(_nested_build_road_depot_widgets)
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(&_build_road_depot_desc, parent);
}

/** Enum referring to the Hotkeys in the build road stop window */
enum BuildRoadStopHotkeys {
	BROSHK_FOCUS_FILTER_BOX, ///< Focus the edit box for editing the filter string
};

struct BuildRoadStationWindow : public PickerWindowBase {
private:
	RoadStopType roadStopType; ///< The RoadStopType for this Window.
	uint line_height; ///< Height of a single line in the newstation selection matrix.
	uint coverage_height; ///< Height of the coverage texts.
	Scrollbar *vscrollList; ///< Vertical scrollbar of the new station list.
	Scrollbar *vscrollMatrix; ///< Vertical scrollbar of the station picker matrix.

	typedef GUIList<RoadStopClassID, StringFilter &> GUIRoadStopClassList; ///< Type definition for the list to hold available road stop classes.

	static const uint EDITBOX_MAX_SIZE = 16; ///< The maximum number of characters for the filter edit box.

	static Listing   last_sorting;           ///< Default sorting of #GUIRoadStopClassList.
	static Filtering last_filtering;         ///< Default filtering of #GUIRoadStopClassList.
	static GUIRoadStopClassList::SortFunction * const sorter_funcs[];   ///< Sort functions of the #GUIRoadStopClassList.
	static GUIRoadStopClassList::FilterFunction * const filter_funcs[]; ///< Filter functions of the #GUIRoadStopClassList.
	GUIRoadStopClassList roadstop_classes;     ///< Available road stop classes.
	StringFilter string_filter;              ///< Filter for available road stop classes.
	QueryString filter_editbox;              ///< Filter editbox.

	void EnsureSelectedClassIsVisible()
	{
		uint pos = 0;
		for (auto rs_class : this->roadstop_classes) {
			if (rs_class == _roadstop_gui_settings.roadstop_class) break;
			pos++;
		}
		this->vscrollList->SetCount((int)this->roadstop_classes.size());
		this->vscrollList->ScrollTowards(pos);
	}

	void CheckOrientationValid()
	{
		if (_roadstop_gui_settings.orientation >= DIAGDIR_END) return;
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(_roadstop_gui_settings.roadstop_type);
		if (spec != nullptr && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) {
			this->RaiseWidget(_roadstop_gui_settings.orientation + WID_BROS_STATION_NE);
			_roadstop_gui_settings.orientation = DIAGDIR_END;
			this->LowerWidget(_roadstop_gui_settings.orientation + WID_BROS_STATION_NE);
			this->SetDirty();
			DeleteWindowById(WC_SELECT_STATION, 0);
		}
	}

public:
	BuildRoadStationWindow(WindowDesc *desc, Window *parent, RoadStopType rs) : PickerWindowBase(desc, parent), filter_editbox(EDITBOX_MAX_SIZE * MAX_CHAR_LENGTH, EDITBOX_MAX_SIZE)
	{
		this->coverage_height = 2 * FONT_HEIGHT_NORMAL + 3 * WD_PAR_VSEP_NORMAL;
		this->vscrollList = nullptr;
		this->vscrollMatrix = nullptr;
		this->roadStopType = rs;
		bool newstops = GetIfNewStopsByType(rs);

		this->CreateNestedTree();

		NWidgetStacked *newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_ADDITIONS);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_MATRIX);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_DEFSIZE);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_RESIZE);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_ORIENTATION);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		newst_additions = this->GetWidget<NWidgetStacked>(WID_BROS_SHOW_NEWST_TYPE_SEL);
		newst_additions->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		/* Hide the station class filter if no stations other than the default one are available. */
		this->GetWidget<NWidgetStacked>(WID_BROS_FILTER_CONTAINER)->SetDisplayedPlane(newstops ? 0 : SZSP_NONE);
		if (newstops) {
			this->vscrollList = this->GetScrollbar(WID_BROS_NEWST_SCROLL);
			this->vscrollMatrix = this->GetScrollbar(WID_BROS_MATRIX_SCROLL);

			this->querystrings[WID_BROS_FILTER_EDITBOX] = &this->filter_editbox;
			this->roadstop_classes.SetListing(this->last_sorting);
			this->roadstop_classes.SetFiltering(this->last_filtering);
			this->roadstop_classes.SetSortFuncs(this->sorter_funcs);
			this->roadstop_classes.SetFilterFuncs(this->filter_funcs);
		}

		this->roadstop_classes.ForceRebuild();
		BuildRoadStopClassesAvailable();

		// Trams don't have non-drivethrough stations
		if (RoadTypeIsTram(_cur_roadtype) && _roadstop_gui_settings.orientation < DIAGDIR_END) {
			_roadstop_gui_settings.orientation = DIAGDIR_END;
		}
		const RoadTypeInfo *rti = GetRoadTypeInfo(_cur_roadtype);
		this->GetWidget<NWidgetCore>(WID_BROS_CAPTION)->widget_data = rti->strings.picker_title[rs];

		for (uint i = RoadTypeIsTram(_cur_roadtype) ? WID_BROS_STATION_X : WID_BROS_STATION_NE; i < WID_BROS_LT_OFF; i++) {
			this->GetWidget<NWidgetCore>(i)->tool_tip = rti->strings.picker_tooltip[rs];
		}

		this->LowerWidget(_roadstop_gui_settings.orientation + WID_BROS_STATION_NE);
		this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);

		this->FinishInitNested(TRANSPORT_ROAD);

		this->window_class = (rs == ROADSTOP_BUS) ? WC_BUS_STATION : WC_TRUCK_STATION;
		if (!newstops || _roadstop_gui_settings.roadstop_class >= (int)RoadStopClass::GetClassCount()) {
			/* There's no new stops available or the list has reduced in size.
			 * Now, set the default road stops as selected. */
			_roadstop_gui_settings.roadstop_class = ROADSTOP_CLASS_DFLT;
			_roadstop_gui_settings.roadstop_type = 0;
		}
		if (newstops) {
			/* The currently selected class doesn't have any stops for this RoadStopType, reset the selection. */
			if (!GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui_settings.roadstop_class), rs)) {
				_roadstop_gui_settings.roadstop_class = ROADSTOP_CLASS_DFLT;
				_roadstop_gui_settings.roadstop_type = 0;
			}
			_roadstop_gui_settings.roadstop_count = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpecCount();
			_roadstop_gui_settings.roadstop_type = std::min((int)_roadstop_gui_settings.roadstop_type, _roadstop_gui_settings.roadstop_count - 1);

			/* Reset back to default class if the previously selected class is not available for this road stop type. */
			if (!GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui_settings.roadstop_class), roadStopType)) {
				_roadstop_gui_settings.roadstop_class = ROADSTOP_CLASS_DFLT;
			}

			NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BROS_MATRIX);
			matrix->SetScrollbar(this->vscrollMatrix);
			matrix->SetCount(_roadstop_gui_settings.roadstop_count);
			matrix->SetClicked(_roadstop_gui_settings.roadstop_type);

			this->EnsureSelectedClassIsVisible();
			this->CheckOrientationValid();
		}
	}

	virtual ~BuildRoadStationWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	/** Sort classes by RoadStopClassID. */
	static bool RoadStopClassIDSorter(RoadStopClassID const &a, RoadStopClassID const &b)
	{
		return a < b;
	}

	/** Filter classes by class name. */
	static bool CDECL TagNameFilter(RoadStopClassID const *sc, StringFilter &filter)
	{
		char buffer[DRAW_STRING_BUFFER];
		GetString(buffer, RoadStopClass::Get(*sc)->name, lastof(buffer));

		filter.ResetState();
		filter.AddLine(buffer);
		return filter.GetState();
	}

	inline bool ShowNewStops() const
	{
		return this->vscrollList != nullptr;
	}

	void BuildRoadStopClassesAvailable()
	{
		if (!this->roadstop_classes.NeedRebuild()) return;

		this->roadstop_classes.clear();

		for (uint i = 0; i < RoadStopClass::GetClassCount(); i++) {
			RoadStopClassID rs_id = (RoadStopClassID)i;
			if (rs_id == ROADSTOP_CLASS_WAYP) {
				// Skip waypoints.
				continue;
			}
			RoadStopClass *rs_class = RoadStopClass::Get(rs_id);
			if (GetIfClassHasNewStopsByType(rs_class, this->roadStopType)) this->roadstop_classes.push_back(rs_id);
		}

		if (this->ShowNewStops()) {
			this->roadstop_classes.Filter(this->string_filter);
			this->roadstop_classes.shrink_to_fit();
			this->roadstop_classes.RebuildDone();
			this->roadstop_classes.Sort();

			this->vscrollList->SetCount((uint)this->roadstop_classes.size());
		}
	}

	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		this->BuildRoadStopClassesAvailable();
	}

	EventState OnHotkey(int hotkey) override
	{
		switch (hotkey) {
			case BROSHK_FOCUS_FILTER_BOX:
				this->SetFocusedWidget(WID_BROS_FILTER_EDITBOX);
				SetFocusedWindow(this); // The user has asked to give focus to the text box, so make sure this window is focused.
				break;

			default:
				return ES_NOT_HANDLED;
		}

		return ES_HANDLED;
	}

	void OnEditboxChanged(int wid) override
	{
		string_filter.SetFilterTerm(this->filter_editbox.text.buf);
		this->roadstop_classes.SetFilterState(!string_filter.IsEmpty());
		this->roadstop_classes.ForceRebuild();
		this->InvalidateData();
	}

	void OnPaint() override
	{
		int rad = _settings_game.station.modified_catchment ? ((this->window_class == WC_BUS_STATION) ? CA_BUS : CA_TRUCK) : CA_UNMODIFIED;
		rad += _settings_game.station.catchment_increase;
		if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		this->DrawWidgets();

		if (this->IsShaded()) return;
		/* 'Accepts' and 'Supplies' texts. */
		StationCoverageType sct = (this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY;

		NWidgetBase *cov = this->GetWidget<NWidgetBase>(WID_BROS_INFO);
		int top = cov->pos_y + WD_PAR_VSEP_NORMAL;
		int left = cov->pos_x + WD_FRAMERECT_LEFT;
		int right = cov->pos_x + cov->current_x - WD_FRAMERECT_RIGHT;
		int bottom = cov->pos_y + cov->current_y;
		top = DrawStationCoverageAreaText(left, right, top, sct, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(left, right, top, sct, rad, true) + WD_PAR_VSEP_NORMAL;

		/*
		int top = this->GetWidget<NWidgetBase>(WID_BROS_LT_ON)->pos_y + this->GetWidget<NWidgetBase>(WID_BROS_LT_ON)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *back_nwi = this->GetWidget<NWidgetBase>(WID_BROS_BACKGROUND);
		int right = back_nwi->pos_x + back_nwi->current_x;
		int bottom = back_nwi->pos_y + back_nwi->current_y;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, sct, rad, true) + WD_PAR_VSEP_NORMAL;
		*/
		/* Resize background if the window is too small.
		 * Never make the window smaller to avoid oscillating if the size change affects the acceptance.
		 * (This is the case, if making the window bigger moves the mouse into the window.) */
		if (top > bottom) {
			this->coverage_height += top - bottom;
			this->ReInit();
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BROS_NEWST_LIST: {
				Dimension d = { 0, 0 };
				for (auto rs_class : this->roadstop_classes) {
					d = maxdim(d, GetStringBoundingBox(RoadStopClass::Get(rs_class)->name));
				}
				size->width = std::max(size->width, d.width + padding.width);
				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = 5 * this->line_height;
				resize->height = this->line_height;
				break;
			}

			case WID_BROS_IMAGE:
				size->width = ScaleGUITrad(64) + 2;
				size->height = ScaleGUITrad(58) + 2;
				break;

			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
			case WID_BROS_MATRIX:
				fill->height = 1;
				resize->height = 1;
				break;

			case WID_BROS_INFO:
				size->height = this->coverage_height;
				break;
		}
	}

	/**
	 * Simply to have a easier way to get the StationType for bus, truck and trams from the WindowClass.
	 */
	StationType GetRoadStationTypeByWindowClass(WindowClass window_class) const {
		switch (window_class) {
			case WC_BUS_STATION:          return STATION_BUS;
			case WC_TRUCK_STATION:        return STATION_TRUCK;
			default: NOT_REACHED();
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		DrawPixelInfo tmp_dpi;

		switch (GB(widget, 0, 16)) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y: {
				StationType st = GetRoadStationTypeByWindowClass(this->window_class);
				const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(_roadstop_gui_settings.roadstop_type);
				bool disabled = (spec != nullptr && widget < WID_BROS_STATION_X && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY));
				if (spec == nullptr || disabled) {
					StationPickerDrawSprite(r.left + WD_MATRIX_LEFT + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), st, INVALID_RAILTYPE, _cur_roadtype, widget - WID_BROS_STATION_NE);
					if (disabled) GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				} else {
					DrawRoadStopTile(r.left + WD_MATRIX_LEFT + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), _cur_roadtype, spec, st, (int)widget - WID_BROS_STATION_NE);
				}
				break;
			}

			case WID_BROS_NEWST_LIST: {
				uint statclass = 0;
				uint row = 0;
				for (auto rs_class : this->roadstop_classes) {
					if (this->vscrollList->IsVisible(statclass)) {
						DrawString(r.left + WD_MATRIX_LEFT, r.right, row * this->line_height + r.top + WD_MATRIX_TOP,
								RoadStopClass::Get(rs_class)->name,
								rs_class == _roadstop_gui_settings.roadstop_class ? TC_WHITE : TC_BLACK);
						row++;
					}
					statclass++;
				}
				break;
			}

			case WID_BROS_IMAGE: {
				byte type = GB(widget, 16, 16);
				assert(type < _roadstop_gui_settings.roadstop_count);

				const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(type);
				StationType st = GetRoadStationTypeByWindowClass(this->window_class);

				if (!IsRoadStopAvailable(spec, st)) {
					GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				}

				// Set up a clipping area for the sprite preview.
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					int x = ScaleGUITrad(31) + 1;
					int y = r.bottom - r.top - ScaleGUITrad(31);
					// Instead of "5" (5th view), pass the orientation clicked in the selection.
					if (spec == nullptr) {
						StationPickerDrawSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), st, INVALID_RAILTYPE, _cur_roadtype, _roadstop_gui_settings.orientation);
					} else {
						DiagDirection orientation = _roadstop_gui_settings.orientation;
						if (orientation < DIAGDIR_END && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) orientation = DIAGDIR_END;
						DrawRoadStopTile(x, y, _cur_roadtype, spec, st, (uint8)orientation);
					}
					_cur_dpi = old_dpi;
				}
				break;
			}
		}
	}

	void OnResize() override {
		if (this->vscrollList != nullptr) {
			this->vscrollList->SetCapacityFromWidget(this, WID_BROS_NEWST_LIST);
		}
	}

	void SetStringParameters(int widget) const override {
		if (widget == WID_BROS_SHOW_NEWST_TYPE) {
			const RoadStopSpec *roadstopspec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(_roadstop_gui_settings.roadstop_type);
			SetDParam(0, (roadstopspec != nullptr && roadstopspec->name != 0) ? roadstopspec->name : STR_STATION_CLASS_DFLT);
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				if (widget < WID_BROS_STATION_X) {
					const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(_roadstop_gui_settings.roadstop_type);
					if (spec != nullptr && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) return;
				}
				this->RaiseWidget(_roadstop_gui_settings.orientation + WID_BROS_STATION_NE);
				_roadstop_gui_settings.orientation = (DiagDirection)(widget - WID_BROS_STATION_NE);
				this->LowerWidget(_roadstop_gui_settings.orientation + WID_BROS_STATION_NE);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;

			case WID_BROS_LT_OFF:
			case WID_BROS_LT_ON:
				this->RaiseWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				_settings_client.gui.station_show_coverage = (widget != WID_BROS_LT_OFF);
				this->LowerWidget(_settings_client.gui.station_show_coverage + WID_BROS_LT_OFF);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				SetViewportCatchmentStation(nullptr, true);
				break;

			case WID_BROS_NEWST_LIST: {
				int y = this->vscrollList->GetScrolledRowFromWidget(pt.y, this, WID_BROS_NEWST_LIST);
				if (y >= (int)this->roadstop_classes.size()) return;
				RoadStopClassID class_id = this->roadstop_classes[y];
				if (_roadstop_gui_settings.roadstop_class != class_id && GetIfClassHasNewStopsByType(RoadStopClass::Get(class_id), roadStopType)) {
					_roadstop_gui_settings.roadstop_class = class_id;
					RoadStopClass *rsclass   = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class);
					_roadstop_gui_settings.roadstop_count = rsclass->GetSpecCount();
					_roadstop_gui_settings.roadstop_type = std::min((int)_roadstop_gui_settings.roadstop_type, std::max(0, (int)_roadstop_gui_settings.roadstop_count - 1));

					NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BROS_MATRIX);
					matrix->SetCount(_roadstop_gui_settings.roadstop_count);
					matrix->SetClicked(_roadstop_gui_settings.roadstop_type);
					this->CheckOrientationValid();
				}
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case WID_BROS_IMAGE: {
				int y = GB(widget, 16, 16);
				if (y >= _roadstop_gui_settings.roadstop_count) return;

				const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui_settings.roadstop_class)->GetSpec(y);
				StationType st = GetRoadStationTypeByWindowClass(this->window_class);

				if (!IsRoadStopAvailable(spec, st)) return;

				/* Check station availability callback */
				_roadstop_gui_settings.roadstop_type = y;

				this->GetWidget<NWidgetMatrix>(WID_BROS_MATRIX)->SetClicked(_roadstop_gui_settings.roadstop_type);

				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				this->CheckOrientationValid();
				break;
			}

			default:
				break;
		}
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}

	static HotkeyList hotkeys;
};

static Hotkey buildroadstop_hotkeys[] = {
	Hotkey('F', "focus_filter_box", BROSHK_FOCUS_FILTER_BOX),
	HOTKEY_LIST_END
};
HotkeyList BuildRoadStationWindow::hotkeys("buildroadstop", buildroadstop_hotkeys);

Listing BuildRoadStationWindow::last_sorting = { false, 0 };
Filtering BuildRoadStationWindow::last_filtering = { false, 0 };

BuildRoadStationWindow::GUIRoadStopClassList::SortFunction * const BuildRoadStationWindow::sorter_funcs[] = {
	&RoadStopClassIDSorter,
};

BuildRoadStationWindow::GUIRoadStopClassList::FilterFunction * const BuildRoadStationWindow::filter_funcs[] = {
	&TagNameFilter,
};

/** Widget definition of the build road station window */
static const NWidgetPart _nested_road_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_DEFSIZE),
			NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_BACKGROUND),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_FILTER_CONTAINER),
					NWidget(NWID_HORIZONTAL), SetPadding(0, 5, 2, 2),
						NWidget(WWT_TEXT, COLOUR_DARK_GREEN), SetFill(0, 1), SetDataTip(STR_LIST_FILTER_TITLE, STR_NULL),
						NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BROS_FILTER_EDITBOX), SetFill(1, 0), SetResize(1, 0),
								SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_ADDITIONS),
					NWidget(NWID_HORIZONTAL), SetPIP(7, 0, 7), SetPadding(2, 0, 1, 0),
						NWidget(WWT_MATRIX, COLOUR_GREY, WID_BROS_NEWST_LIST), SetMinimalSize(122, 71), SetFill(1, 0),
							SetMatrixDataTip(1, 0, STR_STATION_BUILD_STATION_CLASS_TOOLTIP), SetScrollbar(WID_BROS_NEWST_SCROLL),
						NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BROS_NEWST_SCROLL),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_ORIENTATION),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL), SetPadding(4, 2, 1, 2),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(5, 2, 5), SetPadding(0, 0, 1, 0),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NW), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NE), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(NWID_HORIZONTAL), SetPIP(5, 2, 5), SetPadding(0, 0, 1, 0), // For PIP, 5 because the 2 is applied before and after aswell. We want to use 7 to be the same as the class list
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SW), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SE), SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_TYPE_SEL),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BROS_SHOW_NEWST_TYPE), SetMinimalSize(144, 8), SetDataTip(STR_ORANGE_STRING, STR_NULL), SetPadding(4, 2, 4, 2),
				EndContainer(),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(140, 14), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL), SetPadding(3, 2, 0, 2),
				NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
													SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
													SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_MATRIX),
				/* We need an additional background for the matrix, as the matrix cannot handle the scrollbar due to not being an NWidgetCore. */
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_BROS_MATRIX_SCROLL),
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BROS_MATRIX), SetScrollbar(WID_BROS_MATRIX_SCROLL), SetPIP(0, 2, 0), SetPadding(2, 0, 0, 0),
							NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_IMAGE), SetMinimalSize(66, 60),
									SetFill(0, 0), SetResize(0, 0), SetDataTip(0x0, STR_STATION_BUILD_STATION_TYPE_TOOLTIP), SetScrollbar(WID_BROS_MATRIX_SCROLL),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BROS_MATRIX_SCROLL),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BROS_INFO), SetPadding(2, 5, 0, 1), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_RESIZE),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetFill(0, 1), EndContainer(),
					NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _road_station_picker_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_road_station_picker_widgets, lengthof(_nested_road_station_picker_widgets)
);

/** Widget definition of the build tram station window */
static const NWidgetPart _nested_tram_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_DEFSIZE),
			NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_BACKGROUND),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_FILTER_CONTAINER),
					NWidget(NWID_HORIZONTAL), SetPadding(0, 5, 2, 2),
						NWidget(WWT_TEXT, COLOUR_DARK_GREEN), SetFill(0, 1), SetDataTip(STR_LIST_FILTER_TITLE, STR_NULL),
						NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BROS_FILTER_EDITBOX), SetFill(1, 0), SetResize(1, 0),
								SetDataTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_ADDITIONS),
					NWidget(NWID_HORIZONTAL), SetPIP(7, 0, 7), SetPadding(2, 0, 1, 0),
						NWidget(WWT_MATRIX, COLOUR_GREY, WID_BROS_NEWST_LIST), SetMinimalSize(122, 71), SetFill(1, 0),
							SetMatrixDataTip(1, 0, STR_STATION_BUILD_STATION_CLASS_TOOLTIP), SetScrollbar(WID_BROS_NEWST_SCROLL),
						NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BROS_NEWST_SCROLL),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_ORIENTATION),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL), SetPadding(4, 2, 1, 2),
				EndContainer(),
				NWidget(NWID_HORIZONTAL), SetPIP(0, 2, 0), SetPadding(0, 0, 1, 0),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y),  SetMinimalSize(66, 50), SetFill(0, 0), EndContainer(),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_TYPE_SEL),
					NWidget(WWT_LABEL, COLOUR_DARK_GREEN, WID_BROS_SHOW_NEWST_TYPE), SetMinimalSize(144, 8), SetDataTip(STR_ORANGE_STRING, STR_NULL), SetPadding(4, 2, 4, 2),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
													SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
													SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 10), SetResize(0, 1),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_MATRIX),
				/* We need an additional background for the matrix, as the matrix cannot handle the scrollbar due to not being an NWidgetCore. */
				NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetScrollbar(WID_BROS_MATRIX_SCROLL),
					NWidget(NWID_HORIZONTAL),
						NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BROS_MATRIX), SetScrollbar(WID_BROS_MATRIX_SCROLL), SetPIP(0, 2, 0), SetPadding(2, 0, 0, 0),
							NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROS_IMAGE), SetMinimalSize(66, 60),
									SetFill(0, 0), SetResize(0, 0), SetDataTip(0x0, STR_STATION_BUILD_STATION_TYPE_TOOLTIP), SetScrollbar(WID_BROS_MATRIX_SCROLL),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BROS_MATRIX_SCROLL),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BROS_INFO), SetFill(1, 1), SetResize(1, 0),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_SHOW_NEWST_RESIZE),
				NWidget(NWID_VERTICAL),
					NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetFill(0, 1), EndContainer(),
					NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _tram_station_picker_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_tram_station_picker_widgets, lengthof(_nested_tram_station_picker_widgets)
);

static void ShowRVStationPicker(Window *parent, RoadStopType rs)
{
	new BuildRoadStationWindow(RoadTypeIsRoad(_cur_roadtype) ? &_road_station_picker_desc : &_tram_station_picker_desc, parent, rs);
}

struct BuildRoadWaypointWindow : PickerWindowBase {
	BuildRoadWaypointWindow(WindowDesc *desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		NWidgetMatrix *matrix = this->GetWidget<NWidgetMatrix>(WID_BROW_WAYPOINT_MATRIX);
		matrix->SetScrollbar(this->GetScrollbar(WID_BROW_SCROLL));

		this->FinishInitNested(TRANSPORT_ROAD);

		matrix->SetCount(_waypoint_count);
		if (_cur_waypoint_type >= _waypoint_count) _cur_waypoint_type = 0;
		matrix->SetClicked(_cur_waypoint_type);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BROW_WAYPOINT_MATRIX:
				/* Three blobs high and wide. */
				size->width  += resize->width  * 2;
				size->height += resize->height * 2;

				/* Resizing in X direction only at blob size, but at pixel level in Y. */
				resize->height = 1;
				break;

			case WID_BROW_WAYPOINT:
				size->width  = ScaleGUITrad(64) + 2;
				size->height = ScaleGUITrad(58) + 2;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BROW_WAYPOINT: {
				uint type = GB(widget, 16, 16);
				const RoadStopSpec *spec = RoadStopClass::Get(ROADSTOP_CLASS_WAYP)->GetSpec(type);
				if (spec == nullptr) {
					StationPickerDrawSprite(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), STATION_ROADWAYPOINT, INVALID_RAILTYPE, _cur_roadtype, 4);
				} else {
					DrawRoadStopTile(r.left + 1 + ScaleGUITrad(31), r.bottom - ScaleGUITrad(31), _cur_roadtype, spec, STATION_ROADWAYPOINT, 4);
				}
				if (!IsRoadStopAvailable(spec, STATION_ROADWAYPOINT)) {
					GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, PC_BLACK, FILLRECT_CHECKER);
				}
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (GB(widget, 0, 16)) {
			case WID_BROW_WAYPOINT: {
				uint type = GB(widget, 16, 16);

				const RoadStopSpec *spec = RoadStopClass::Get(ROADSTOP_CLASS_WAYP)->GetSpec(type);
				if (!IsRoadStopAvailable(spec, STATION_ROADWAYPOINT)) return;

				this->GetWidget<NWidgetMatrix>(WID_BROW_WAYPOINT_MATRIX)->SetClicked(_cur_waypoint_type);

				_cur_waypoint_type = type;
				this->GetWidget<NWidgetMatrix>(WID_BROW_WAYPOINT_MATRIX)->SetClicked(_cur_waypoint_type);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
			}
		}
	}
};

/** Nested widget definition for the build NewGRF road waypoint window */
static const NWidgetPart _nested_build_waypoint_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_MATRIX, COLOUR_DARK_GREEN, WID_BROW_WAYPOINT_MATRIX), SetPIP(3, 2, 3), SetScrollbar(WID_BROW_SCROLL),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BROW_WAYPOINT), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), SetScrollbar(WID_BROW_SCROLL), EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_BROW_SCROLL),
			NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _build_waypoint_desc(
	WDP_AUTO, "build_waypoint", 0, 0,
	WC_BUILD_WAYPOINT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_waypoint_widgets, lengthof(_nested_build_waypoint_widgets)
);

static void ShowBuildWaypointPicker(Window *parent)
{
	new BuildRoadWaypointWindow(&_build_waypoint_desc, parent);
}

void InitializeRoadGui()
{
	_build_depot_direction = DIAGDIR_NW;
	_roadstop_gui_settings.orientation = DIAGDIR_NW;
}


/** Set the initial (default) road and tram types to use */
static void SetDefaultRoadGui()
{
	extern RoadType _last_built_roadtype;
	extern RoadType _last_built_tramtype;

	/* Clean old GUI values; railtype is (re)set by rail_gui.cpp */
	_last_built_roadtype = ROADTYPE_ROAD;
	_last_built_tramtype = ROADTYPE_TRAM;

	if (_local_company == COMPANY_SPECTATOR || !Company::IsValidID(_local_company)) return;

	auto get_first_road_type = [](RoadTramType rtt, RoadType &out) {
		auto it = std::find_if(_sorted_roadtypes.begin(), _sorted_roadtypes.end(),
				[&](RoadType r){ return GetRoadTramType(r) == rtt && HasRoadTypeAvail(_local_company, r); });
		if (it != _sorted_roadtypes.end()) out = *it;
	};
	auto get_last_road_type = [](RoadTramType rtt, RoadType &out) {
		auto it = std::find_if(_sorted_roadtypes.rbegin(), _sorted_roadtypes.rend(),
				[&](RoadType r){ return GetRoadTramType(r) == rtt && HasRoadTypeAvail(_local_company, r); });
		if (it != _sorted_roadtypes.rend()) out = *it;
	};

	switch (_settings_client.gui.default_road_type) {
		case 3: {
			/* Use defaults above */
			break;
		}
		case 2: {
			/* Find the most used types */
			std::array<uint, ROADTYPE_END> road_count = {};
			std::array<uint, ROADTYPE_END> tram_count = {};
			for (TileIndex t = 0; t < MapSize(); t++) {
				if (MayHaveRoad(t)) {
					if (IsTileType(t, MP_STATION) && !IsAnyRoadStop(t)) continue;
					RoadType road_type = GetRoadTypeRoad(t);
					if (road_type != INVALID_ROADTYPE) road_count[road_type]++;
					RoadType tram_type = GetRoadTypeTram(t);
					if (tram_type != INVALID_ROADTYPE) tram_count[tram_type]++;
				}
			}

			auto get_best_road_type = [&](RoadTramType rtt, RoadType &out, const std::array<uint, ROADTYPE_END> &count) {
				uint highest = 0;
				for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
					if (count[rt] > highest && HasRoadTypeAvail(_local_company, rt)) {
						out = rt;
						highest = count[rt];
					}
				}
				if (highest == 0) get_first_road_type(rtt, out);
			};
			get_best_road_type(RTT_ROAD, _last_built_roadtype, road_count);
			get_best_road_type(RTT_TRAM, _last_built_tramtype, tram_count);
			break;
		}
		case 0: {
			/* Use first available types */
			get_first_road_type(RTT_ROAD, _last_built_roadtype);
			get_first_road_type(RTT_TRAM, _last_built_tramtype);
			break;
		}
		case 1: {
			/* Use last available type */
			get_last_road_type(RTT_ROAD, _last_built_roadtype);
			get_last_road_type(RTT_TRAM, _last_built_tramtype);
			break;
		}
		default:
			NOT_REACHED();
	}
}

/**
 * I really don't know why rail_gui.cpp has this too, shouldn't be included in the other one?
 */
void InitializeRoadGUI()
{
	SetDefaultRoadGui();

	BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_ROAD));
	if (w != nullptr) w->ModifyRoadType(_cur_roadtype);
}

DropDownList GetRoadTypeDropDownList(RoadTramTypes rtts, bool for_replacement, bool all_option)
{
	RoadTypes used_roadtypes;
	RoadTypes avail_roadtypes;

	const Company *c = Company::Get(_local_company);

	/* Find the used roadtypes. */
	if (for_replacement) {
		avail_roadtypes = GetCompanyRoadTypes(c->index, false);
		used_roadtypes  = GetRoadTypes(false);
	} else {
		avail_roadtypes = c->avail_roadtypes;
		used_roadtypes  = GetRoadTypes(true);
	}

	/* Filter listed road types */
	if (!HasBit(rtts, RTT_ROAD)) used_roadtypes &= _roadtypes_type;
	if (!HasBit(rtts, RTT_TRAM)) used_roadtypes &= ~_roadtypes_type;

	DropDownList list;

	if (all_option) {
		list.emplace_back(new DropDownListStringItem(STR_REPLACE_ALL_ROADTYPE, INVALID_ROADTYPE, false));
	}

	Dimension d = { 0, 0 };
	/* Get largest icon size, to ensure text is aligned on each menu item. */
	if (!for_replacement) {
		for (const auto &rt : _sorted_roadtypes) {
			if (!HasBit(used_roadtypes, rt)) continue;
			const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
			d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
		}
	}

	for (const auto &rt : _sorted_roadtypes) {
		/* If it's not used ever, don't show it to the user. */
		if (!HasBit(used_roadtypes, rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		DropDownListParamStringItem *item;
		if (for_replacement) {
			item = new DropDownListParamStringItem(rti->strings.replace_text, rt, !HasBit(avail_roadtypes, rt));
		} else {
			StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
			DropDownListIconItem *iconitem = new DropDownListIconItem(rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt));
			iconitem->SetDimension(d);
			item = iconitem;
		}
		item->SetParam(0, rti->strings.menu_text);
		item->SetParam(1, rti->max_speed / 2);
		list.emplace_back(item);
	}

	if (list.size() == 0) {
		/* Empty dropdowns are not allowed */
		list.emplace_back(new DropDownListStringItem(STR_NONE, INVALID_ROADTYPE, true));
	}

	return list;
}

DropDownList GetScenRoadTypeDropDownList(RoadTramTypes rtts)
{
	RoadTypes avail_roadtypes = GetRoadTypes(false);
	avail_roadtypes = AddDateIntroducedRoadTypes(avail_roadtypes, _date);
	RoadTypes used_roadtypes = GetRoadTypes(true);

	/* Filter listed road types */
	if (!HasBit(rtts, RTT_ROAD)) used_roadtypes &= _roadtypes_type;
	if (!HasBit(rtts, RTT_TRAM)) used_roadtypes &= ~_roadtypes_type;

	DropDownList list;

	/* If it's not used ever, don't show it to the user. */
	Dimension d = { 0, 0 };
	for (const auto &rt : _sorted_roadtypes) {
		if (!HasBit(used_roadtypes, rt)) continue;
		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
		d = maxdim(d, GetSpriteSize(rti->gui_sprites.build_x_road));
	}
	for (const auto &rt : _sorted_roadtypes) {
		if (!HasBit(used_roadtypes, rt)) continue;

		const RoadTypeInfo *rti = GetRoadTypeInfo(rt);

		StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
		DropDownListIconItem *item = new DropDownListIconItem(rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt));
		item->SetDimension(d);
		item->SetParam(0, rti->strings.menu_text);
		item->SetParam(1, rti->max_speed / 2);
		list.emplace_back(item);
	}

	if (list.size() == 0) {
		/* Empty dropdowns are not allowed */
		list.emplace_back(new DropDownListStringItem(STR_NONE, -1, true));
	}

	return list;
}
