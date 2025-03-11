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
#include "tunnelbridge_cmd.h"
#include "tunnelbridge_map.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "hotkeys.h"
#include "road_gui.h"
#include "zoom_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "engine_base.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "strings_func.h"
#include "core/geometry_func.hpp"
#include "date_func.h"
#include "station_map.h"
#include "waypoint_func.h"
#include "newgrf_roadstop.h"
#include "debug.h"
#include "newgrf_station.h"
#include "core/backup_type.hpp"
#include "picker_gui.h"
#include "newgrf_extension.h"

#include "widgets/road_widget.h"
#include "table/strings.h"

#include <array>

#include "safeguards.h"

static void ShowRVStationPicker(Window *parent, RoadStopType rs);
static void ShowRoadDepotPicker(Window *parent);
static void ShowBuildRoadWaypointPicker(Window *parent);

static bool _remove_button_clicked;
static bool _one_way_button_clicked;

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

static DiagDirection _road_depot_orientation;

struct RoadWaypointPickerSelection {
	RoadStopClassID sel_class; ///< Selected road waypoint class.
	uint16_t sel_type; ///< Selected road waypoint type within the class.
};
static RoadWaypointPickerSelection _waypoint_gui; ///< Settings of the road waypoint picker.

struct RoadStopPickerSelection {
	RoadStopClassID sel_class; ///< Selected road stop class.
	uint16_t sel_type; ///< Selected road stop type within the class.
	DiagDirection orientation; ///< Selected orientation of the road stop.
};
static RoadStopPickerSelection _roadstop_gui;

static bool IsRoadStopEverAvailable(const RoadStopSpec *spec, StationType type)
{
	if (spec == nullptr) return true;

	if (HasBit(spec->flags, RSF_BUILD_MENU_ROAD_ONLY) && !RoadTypeIsRoad(_cur_roadtype)) return false;
	if (HasBit(spec->flags, RSF_BUILD_MENU_TRAM_ONLY) && !RoadTypeIsTram(_cur_roadtype)) return false;

	if (type == StationType::RoadWaypoint && spec->stop_type != ROADSTOPTYPE_ALL) {
		if (spec->grf_prop.grffile != nullptr && HasBit(spec->grf_prop.grffile->observed_feature_tests, GFTOF_ROAD_STOPS)) return true;
	}

	switch (spec->stop_type) {
		case ROADSTOPTYPE_ALL: return true;
		case ROADSTOPTYPE_PASSENGER: return type == StationType::Bus;
		case ROADSTOPTYPE_FREIGHT: return type == StationType::Truck;
		default: NOT_REACHED();
	}
}

/**
 * Check whether a road stop type can be built.
 * @return true if building is allowed.
 */
static bool IsRoadStopAvailable(const RoadStopSpec *spec, StationType type)
{
	if (spec == nullptr) return true;
	if (!IsRoadStopEverAvailable(spec, type)) return false;

	if (!HasBit(spec->callback_mask, CBM_ROAD_STOP_AVAIL)) return true;

	uint16_t cb_res = GetRoadStopCallback(CBID_STATION_AVAILABILITY, 0, 0, spec, nullptr, INVALID_TILE, _cur_roadtype, type, 0);
	if (cb_res == CALLBACK_FAILED) return true;

	return Convert8bitBooleanCallback(spec->grf_prop.grffile, CBID_STATION_AVAILABILITY, cb_res);
}

void CcPlaySound_CONSTRUCTION_OTHER(const CommandCost &result, TileIndex tile)
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
 */
void CcBuildRoadTunnel(const CommandCost &result, TileIndex start_tile)
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
			DoCommandPOld(tile, _cur_roadtype << 4 | DiagDirToRoadBits(ReverseDiagDir(direction)), INVALID_TOWN, CMD_BUILD_ROAD);
		}
	}
}

void CcRoadDepot(const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param)
{
	if (result.Failed()) return;

	auto *data = dynamic_cast<const typename CommandTraits<CMD_BUILD_ROAD_DEPOT>::PayloadType *>(&payload);
	if (data == nullptr) return;

	DiagDirection dir = (DiagDirection)GB(data->p1, 0, 2);
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
 *           bit 5..10: The roadtype.
 *           bit 16..31: Station ID to join (NEW_STATION if build new one).
 * @param p3 bit 0..15: Roadstop class.
 *           bit 16..31: Roadstopspec index.
 * @param cmd Unused.
 * @see CmdBuildRoadStop
 */
void CcRoadStop(const CommandCost &result, Commands cmd, TileIndex tile, const CommandPayloadBase &payload, CallbackParameter param)
{
	if (result.Failed()) return;

	auto *data = dynamic_cast<const typename CommandTraits<CMD_BUILD_ROAD_STOP>::PayloadType *>(&payload);
	if (data == nullptr) return;

	DiagDirection dir = (DiagDirection)GB(data->p2, 3, 2);
	if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	bool connect_to_road = true;

	RoadStopClassID spec_class = Extract<RoadStopClassID, 0, 16>(data->p3);
	uint16_t spec_index        = GB(data->p3, 16, 16);
	if ((uint)spec_class < RoadStopClass::GetClassCount() && spec_index < RoadStopClass::Get(spec_class)->GetSpecCount()) {
		const RoadStopSpec *roadstopspec = RoadStopClass::Get(spec_class)->GetSpec(spec_index);
		if (roadstopspec != nullptr && HasBit(roadstopspec->flags, RSF_NO_AUTO_ROAD_CONNECTION)) connect_to_road = false;
	}

	if (connect_to_road) {
		TileArea roadstop_area(tile, GB(data->p1, 0, 8), GB(data->p1, 8, 8));
		for (TileIndex cur_tile : roadstop_area) {
			ConnectRoadToStructure(cur_tile, dir);
			/* For a drive-through road stop build connecting road for other entrance. */
			if (HasBit(data->p2, 1)) ConnectRoadToStructure(cur_tile, ReverseDiagDir(dir));
		}
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
static void PlaceRoadStop(TileIndex start_tile, TileIndex end_tile, uint32_t p2, uint32_t cmd)
{
	uint8_t ddir = _roadstop_gui.orientation;

	if (ddir >= DIAGDIR_END) {
		SetBit(p2, 1); // It's a drive-through stop.
		ddir -= DIAGDIR_END; // Adjust picker result to actual direction.
	}
	p2 |= ddir << 3; // Set the DiagDirecion into p2 bits 3 and 4.
	p2 |= INVALID_STATION << 16; // no station to join

	TileArea ta(start_tile, end_tile);
	CommandContainer<P123CmdData> cmdcont = NewCommandContainerBasic(ta.tile, (uint32_t)(ta.w | ta.h << 8), p2, cmd, CommandCallback::RoadStop);
	cmdcont.payload.p3 = (_roadstop_gui.sel_type << 16) | _roadstop_gui.sel_class;
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
		DoCommandPOld(tile, 1 | 1 << 8, ROADSTOP_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_ROAD_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT));
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
		if (_roadstop_gui.orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui.orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_BUSSTOP);
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
		if (_roadstop_gui.orientation < DIAGDIR_END) { // Not a drive-through stop.
			VpStartPlaceSizing(tile, (DiagDirToAxis(_roadstop_gui.orientation) == AXIS_X) ? VPM_X_LIMITED : VPM_Y_LIMITED, DDSP_BUILD_TRUCKSTOP);
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
	for (WidgetID i = WID_ROT_ROAD_X; i <= WID_ROT_AUTOROAD; i++) {
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
	int last_started_action;    ///< Last started user action.

	BuildRoadToolbarWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->roadtype = _cur_roadtype;
		this->CreateNestedTree();
		this->FinishInitNested(window_number);
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
		}

		this->OnInvalidateData();
		this->last_started_action = INVALID_WID_ROT;

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (_game_mode == GM_NORMAL && (this->IsWidgetLowered(WID_ROT_BUS_STATION) || this->IsWidgetLowered(WID_ROT_TRUCK_STATION))) SetViewportCatchmentStation(nullptr, true);
		if (_game_mode == GM_NORMAL && this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) SetViewportCatchmentWaypoint(nullptr, true);
		if (_settings_client.gui.link_terraform_toolbar) CloseWindowById(WC_SCEN_LAND_GEN, 0, false);
		CloseWindowById(WC_SELECT_STATION, 0);
		this->Window::Close();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;

		if (!ValParamRoadType(this->roadtype)) {
			/* Close toolbar if road type is not available. */
			this->Close();
			return;
		}

		RoadTramType rtt = GetRoadTramType(this->roadtype);

		bool can_build = CanBuildVehicleInfrastructure(VEH_ROAD, rtt);
		this->SetWidgetsDisabledState(!can_build,
			WID_ROT_DEPOT,
			WID_ROT_BUILD_WAYPOINT,
			WID_ROT_BUS_STATION,
			WID_ROT_TRUCK_STATION);
		if (!can_build) {
			CloseWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
			CloseWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
			CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
			CloseWindowById(WC_BUILD_WAYPOINT, TRANSPORT_ROAD);
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

	void OnInit() override
	{
		/* Configure the road toolbar for the roadtype. */
		const RoadTypeInfo *rti = GetRoadTypeInfo(this->roadtype);
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_X)->SetSprite(rti->gui_sprites.build_x_road);
		this->GetWidget<NWidgetCore>(WID_ROT_ROAD_Y)->SetSprite(rti->gui_sprites.build_y_road);
		this->GetWidget<NWidgetCore>(WID_ROT_AUTOROAD)->SetSprite(rti->gui_sprites.auto_road);
		if (_game_mode != GM_EDITOR) {
			this->GetWidget<NWidgetCore>(WID_ROT_DEPOT)->SetSprite(rti->gui_sprites.build_depot);
		}
		this->GetWidget<NWidgetCore>(WID_ROT_CONVERT_ROAD)->SetSprite(rti->gui_sprites.convert_road);
		this->GetWidget<NWidgetCore>(WID_ROT_BUILD_TUNNEL)->SetSprite(rti->gui_sprites.build_tunnel);
		if (HasBit(rti->extra_flags, RXTF_NO_TUNNELS)) this->DisableWidget(WID_ROT_BUILD_TUNNEL);
	}

	/**
	 * Switch to another road type.
	 * @param roadtype New road type.
	 */
	void ModifyRoadType(RoadType roadtype)
	{
		this->roadtype = roadtype;
		this->ReInit();
	}

	void SetStringParameters(WidgetID widget) const override
	{
		if (widget == WID_ROT_CAPTION) {
			const RoadTypeInfo *rti = GetRoadTypeInfo(this->roadtype);
			if (rti->max_speed > 0) {
				SetDParam(0, STR_TOOLBAR_RAILTYPE_VELOCITY);
				SetDParam(1, rti->strings.toolbar_caption);
				SetDParam(2, PackVelocity(rti->max_speed / 2, VEH_ROAD));
			} else {
				SetDParam(0, rti->strings.toolbar_caption);
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

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		_remove_button_clicked = false;
		_one_way_button_clicked = false;
		switch (widget) {
			case WID_ROT_ROAD_X:
				HandlePlacePushButton(this, WID_ROT_ROAD_X, GetRoadTypeInfo(this->roadtype)->cursor.road_nwse, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_ROAD_Y:
				HandlePlacePushButton(this, WID_ROT_ROAD_Y, GetRoadTypeInfo(this->roadtype)->cursor.road_swne, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_AUTOROAD:
				HandlePlacePushButton(this, WID_ROT_AUTOROAD, GetRoadTypeInfo(this->roadtype)->cursor.autoroad, HT_RECT);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEMOLISH:
				HandlePlacePushButton(this, WID_ROT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				this->last_started_action = widget;
				break;

			case WID_ROT_DEPOT:
				if (HandlePlacePushButton(this, WID_ROT_DEPOT, GetRoadTypeInfo(this->roadtype)->cursor.depot, HT_RECT)) {
					ShowRoadDepotPicker(this);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_BUILD_WAYPOINT:
				if (HandlePlacePushButton(this, WID_ROT_BUILD_WAYPOINT, SPR_CURSOR_WAYPOINT, HT_RECT)) {
					ShowBuildRoadWaypointPicker(this);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_BUS_STATION:
				if (HandlePlacePushButton(this, WID_ROT_BUS_STATION, SPR_CURSOR_BUS_STATION, HT_RECT)) {
					ShowRVStationPicker(this, RoadStopType::Bus);
					this->last_started_action = widget;
				}
				break;

			case WID_ROT_TRUCK_STATION:
				if (HandlePlacePushButton(this, WID_ROT_TRUCK_STATION, SPR_CURSOR_TRUCK_STATION, HT_RECT)) {
					ShowRVStationPicker(this, RoadStopType::Truck);
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
				HandlePlacePushButton(this, WID_ROT_BUILD_TUNNEL, GetRoadTypeInfo(this->roadtype)->cursor.tunnel, HT_SPECIAL | HT_TUNNEL);
				this->last_started_action = widget;
				break;

			case WID_ROT_REMOVE:
				if (this->IsWidgetDisabled(WID_ROT_REMOVE)) return;

				CloseWindowById(WC_SELECT_STATION, 0);
				ToggleRoadButton_Remove(this);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				break;

			case WID_ROT_CONVERT_ROAD:
				HandlePlacePushButton(this, WID_ROT_CONVERT_ROAD, GetRoadTypeInfo(this->roadtype)->cursor.convert_road, HT_RECT);
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

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
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
				DoCommandPOld(tile, _cur_roadtype << 2 | _road_depot_orientation, 0,
						CMD_BUILD_ROAD_DEPOT | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_depot), CommandCallback::RoadDepot);
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
				Command<CMD_BUILD_TUNNEL>::Post(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE, CommandCallback::BuildRoadTunnel,
						tile, TRANSPORT_ROAD, _cur_roadtype);
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
		if (_game_mode != GM_EDITOR && this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) SetViewportCatchmentWaypoint(nullptr, true);

		this->RaiseButtons();
		this->SetWidgetDisabledState(WID_ROT_REMOVE, true);
		this->SetWidgetDirty(WID_ROT_REMOVE);

		if (RoadTypeIsRoad(this->roadtype)) {
			this->SetWidgetDisabledState(WID_ROT_ONE_WAY, true);
			this->SetWidgetDirty(WID_ROT_ONE_WAY);
		}

		CloseWindowById(WC_BUS_STATION, TRANSPORT_ROAD);
		CloseWindowById(WC_TRUCK_STATION, TRANSPORT_ROAD);
		CloseWindowById(WC_BUILD_DEPOT, TRANSPORT_ROAD);
		CloseWindowById(WC_BUILD_WAYPOINT, TRANSPORT_ROAD);
		CloseWindowById(WC_SELECT_STATION, 0);
		CloseWindowByClass(WC_BUILD_BRIDGE);
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
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

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
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

					/* Even if _cur_roadtype_id is a uint8_t we only use 5 bits so
					 * we could ignore the last 3 bits and reuse them for other
					 * flags */
					_place_road_flag = (RoadFlags)((_place_road_flag & RF_DIR_Y) ? (_place_road_flag & 0x07) : (_place_road_flag >> 3));

					DoCommandPOld(start_tile, end_tile, _place_road_flag | (_cur_roadtype << 3) | (_one_way_button_clicked << 10),
							_remove_button_clicked ?
							CMD_REMOVE_LONG_ROAD | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_remove_road) :
							CMD_BUILD_LONG_ROAD | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_build_road), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
					break;

				case DDSP_BUILD_ROAD_WAYPOINT:
				case DDSP_REMOVE_ROAD_WAYPOINT:
					if (this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) {
						TileArea ta(start_tile, end_tile);
						if (_remove_button_clicked) {
							DoCommandPOld(ta.tile, ta.w | ta.h << 8, (1 << 2), CMD_REMOVE_ROAD_STOP | CMD_MSG(STR_ERROR_CAN_T_REMOVE_ROAD_WAYPOINT), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
						} else {
							uint32_t p1 = ta.w | ta.h << 8 | _ctrl_pressed << 16 | (select_method == VPM_X_LIMITED ? AXIS_X : AXIS_Y) << 17;
							uint32_t p2 = _waypoint_gui.sel_class | INVALID_STATION << 16;

							CommandContainer<P123CmdData> cmdcont = NewCommandContainerBasic(ta.tile, p1, p2, CMD_BUILD_ROAD_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_ROAD_WAYPOINT), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
							cmdcont.payload.p3 = _waypoint_gui.sel_type;
							ShowSelectWaypointIfNeeded(cmdcont, ta);
						}
					}
					break;

				case DDSP_BUILD_BUSSTOP:
				case DDSP_REMOVE_BUSSTOP:
					if (this->IsWidgetLowered(WID_ROT_BUS_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui.sel_class), RoadStopType::Bus, _cur_roadtype)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandPOld(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | to_underlying(RoadStopType::Bus), CMD_REMOVE_ROAD_STOP | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_remove_station[to_underlying(RoadStopType::Bus)]), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, (_cur_roadtype << 5) | (_ctrl_pressed << 2) | to_underlying(RoadStopType::Bus), CMD_BUILD_ROAD_STOP | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_build_station[to_underlying(RoadStopType::Bus)]));
						}
					}
					break;

				case DDSP_BUILD_TRUCKSTOP:
				case DDSP_REMOVE_TRUCKSTOP:
					if (this->IsWidgetLowered(WID_ROT_TRUCK_STATION) && GetIfClassHasNewStopsByType(RoadStopClass::Get(_roadstop_gui.sel_class), RoadStopType::Truck, _cur_roadtype)) {
						if (_remove_button_clicked) {
							TileArea ta(start_tile, end_tile);
							DoCommandPOld(ta.tile, ta.w | ta.h << 8, (_ctrl_pressed << 1) | to_underlying(RoadStopType::Truck), CMD_REMOVE_ROAD_STOP | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_remove_station[to_underlying(RoadStopType::Truck)]), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
						} else {
							PlaceRoadStop(start_tile, end_tile, (_cur_roadtype << 5) | (_ctrl_pressed << 2) | to_underlying(RoadStopType::Truck), CMD_BUILD_ROAD_STOP | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_build_station[to_underlying(RoadStopType::Truck)]));
						}
					}
					break;

				case DDSP_CONVERT_ROAD:
					DoCommandPOld(end_tile, start_tile, _cur_roadtype, CMD_CONVERT_ROAD | CMD_MSG(GetRoadTypeInfo(this->roadtype)->strings.err_convert_road), CommandCallback::PlaySound_CONSTRUCTION_OTHER);
					break;
			}
		}
	}

	void OnPlacePresize([[maybe_unused]] Point pt, TileIndex tile) override
	{
		Command<CMD_BUILD_TUNNEL>::Do(DC_AUTO, tile, TRANSPORT_ROAD, _cur_roadtype);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	EventState OnCTRLStateChange() override
	{
		if (RoadToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		if (_game_mode == GM_NORMAL && this->IsWidgetLowered(WID_ROT_BUILD_WAYPOINT)) CheckRedrawWaypointCoverage(this, true);
	}

	static HotkeyList road_hotkeys;
	static HotkeyList tram_hotkeys;
};

Window *CreateRoadTramToolbarForRoadType(RoadType roadtype, RoadTramType rtt)
{
	Window *w = nullptr;
	switch (_game_mode) {
		case GM_NORMAL:
			w = ShowBuildRoadToolbar(roadtype);
			break;

		case GM_EDITOR:
			if ((GetRoadTypes(true) & ((rtt == RTT_ROAD) ? ~_roadtypes_type : _roadtypes_type)) == ROADTYPES_NONE) return nullptr;
			w = ShowBuildRoadScenToolbar(roadtype);
			break;

		default:
			break;
	}
	return w;
}

/**
 * Handler for global hotkeys of the BuildRoadToolbarWindow.
 * @param hotkey Hotkey
 * @param last_build Last build road type
 * @return ES_HANDLED if hotkey was accepted.
 */
static EventState RoadTramToolbarGlobalHotkeys(int hotkey, RoadType last_build, RoadTramType rtt)
{
	Window *w = CreateRoadTramToolbarForRoadType(last_build, rtt);

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
};
HotkeyList BuildRoadToolbarWindow::tram_hotkeys("tramtoolbar", tramtoolbar_hotkeys, TramToolbarGlobalHotkeys);


static constexpr NWidgetPart _nested_build_road_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetStringTip(STR_JUST_STRING2, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_BUS_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRUCK_LOADING_BAY),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_desc(__FILE__, __LINE__,
	WDP_ALIGN_TOOLBAR, "toolbar_road", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

static constexpr NWidgetPart _nested_build_tramway_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetStringTip(STR_JUST_STRING2, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_DEPOT, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAM_VEHICLE_DEPOT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_WAYPOINT, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUS_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_BUS_STATION, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_PASSENGER_TRAM_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_TRUCK_STATION),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRUCK_BAY, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_CARGO_TRAM_STATION),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_desc(__FILE__, __LINE__,
	WDP_ALIGN_TOOLBAR, "toolbar_tramway", 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_widgets,
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

	CloseWindowByClass(WC_BUILD_TOOLBAR);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? _build_road_desc : _build_tramway_desc, TRANSPORT_ROAD);
}

static constexpr NWidgetPart _nested_build_road_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetStringTip(STR_JUST_STRING2, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_AUTOROAD, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ONE_WAY),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_ONE_WAY, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_ONE_WAY_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_ROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_ROAD),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_ROAD),
	EndContainer(),
};

static WindowDesc _build_road_scen_desc(__FILE__, __LINE__,
	WDP_AUTO, "toolbar_road_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_road_scen_widgets,
	&BuildRoadToolbarWindow::road_hotkeys
);

static constexpr NWidgetPart _nested_build_tramway_scen_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_ROT_CAPTION), SetStringTip(STR_JUST_STRING2, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRAMWAY_X_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_ROAD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_TRAMWAY_Y_DIR, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_SECTION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_AUTOROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_AUTOTRAM, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_AUTOTRAM),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, -1), SetMinimalSize(0, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(43, 22), SetSpriteTip(SPR_IMG_BRIDGE, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_ROAD_TUNNEL, STR_ROAD_TOOLBAR_TOOLTIP_BUILD_TRAMWAY_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_REMOVE, STR_ROAD_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR_TRAMWAYS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_ROT_CONVERT_ROAD),
						SetFill(0, 1), SetMinimalSize(22, 22), SetSpriteTip(SPR_IMG_CONVERT_ROAD, STR_ROAD_TOOLBAR_TOOLTIP_CONVERT_TRAM),
	EndContainer(),
};

static WindowDesc _build_tramway_scen_desc(__FILE__, __LINE__,
	WDP_AUTO, "toolbar_tram_scen", 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_tramway_scen_widgets,
	&BuildRoadToolbarWindow::tram_hotkeys
);

/**
 * Show the road building toolbar in the scenario editor.
 * @return The just opened toolbar, or \c nullptr if the toolbar was already open.
 */
Window *ShowBuildRoadScenToolbar(RoadType roadtype)
{
	CloseWindowById(WC_SCEN_BUILD_TOOLBAR, TRANSPORT_ROAD);
	_cur_roadtype = roadtype;

	return AllocateWindowDescFront<BuildRoadToolbarWindow>(RoadTypeIsRoad(_cur_roadtype) ? _build_road_scen_desc : _build_tramway_scen_desc, TRANSPORT_ROAD);
}

struct BuildRoadDepotWindow : public PickerWindowBase {
	BuildRoadDepotWindow(WindowDesc &desc, Window *parent) : PickerWindowBase(desc, parent)
	{
		this->CreateNestedTree();

		this->LowerWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
		if (RoadTypeIsTram(_cur_roadtype)) {
			this->GetWidget<NWidgetCore>(WID_BROD_CAPTION)->SetString(STR_BUILD_DEPOT_TRAM_ORIENTATION_CAPTION);
			for (WidgetID i = WID_BROD_DEPOT_NE; i <= WID_BROD_DEPOT_NW; i++) {
				this->GetWidget<NWidgetCore>(i)->SetToolTip(STR_BUILD_DEPOT_TRAM_ORIENTATION_SELECT_TOOLTIP);
			}
		}

		this->FinishInitNested(TRANSPORT_ROAD);
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		size.width  = ScaleGUITrad(64) + WidgetDimensions::scaled.fullbevel.Horizontal();
		size.height = ScaleGUITrad(48) + WidgetDimensions::scaled.fullbevel.Vertical();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (!IsInsideMM(widget, WID_BROD_DEPOT_NE, WID_BROD_DEPOT_NW + 1)) return;

		DrawPixelInfo tmp_dpi;
		Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
		if (FillDrawPixelInfo(&tmp_dpi, ir)) {
			AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
			int x = (ir.Width()  - ScaleSpriteTrad(64)) / 2 + ScaleSpriteTrad(31);
			int y = (ir.Height() + ScaleSpriteTrad(48)) / 2 - ScaleSpriteTrad(31);
			DrawRoadDepotSprite(x, y, (DiagDirection)(widget - WID_BROD_DEPOT_NE + DIAGDIR_NE), _cur_roadtype);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BROD_DEPOT_NW:
			case WID_BROD_DEPOT_NE:
			case WID_BROD_DEPOT_SW:
			case WID_BROD_DEPOT_SE:
				this->RaiseWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
				_road_depot_orientation = (DiagDirection)(widget - WID_BROD_DEPOT_NE);
				this->LowerWidget(WID_BROD_DEPOT_NE + _road_depot_orientation);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			default:
				break;
		}
	}
};

static constexpr NWidgetPart _nested_build_road_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_BROD_CAPTION), SetStringTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1), SetPadding(WidgetDimensions::unscaled.picker),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROD_DEPOT_NW), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROD_DEPOT_SW), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
			EndContainer(),
			NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROD_DEPOT_NE), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROD_DEPOT_SE), SetFill(0, 0), SetToolTip(STR_BUILD_DEPOT_ROAD_ORIENTATION_SELECT_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static WindowDesc _build_road_depot_desc(__FILE__, __LINE__,
	WDP_AUTO, nullptr, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_road_depot_widgets
);

static void ShowRoadDepotPicker(Window *parent)
{
	new BuildRoadDepotWindow(_build_road_depot_desc, parent);
}

template <RoadStopType roadstoptype>
class RoadStopPickerCallbacks : public PickerCallbacksNewGRFClass<RoadStopClass> {
public:
	RoadStopPickerCallbacks(const std::string &ini_group) : PickerCallbacksNewGRFClass<RoadStopClass>(ini_group) {}

	StringID GetClassTooltip() const override;
	StringID GetTypeTooltip() const override;

	bool IsActive() const override
	{
		for (const auto &cls : RoadStopClass::Classes()) {
			if (IsWaypointClass(cls)) continue;
			for (const auto *spec : cls.Specs()) {
				if (spec == nullptr) continue;
				if (roadstoptype == RoadStopType::Truck && spec->stop_type != ROADSTOPTYPE_FREIGHT && spec->stop_type != ROADSTOPTYPE_ALL) continue;
				if (roadstoptype == RoadStopType::Bus && spec->stop_type != ROADSTOPTYPE_PASSENGER && spec->stop_type != ROADSTOPTYPE_ALL) continue;
				return true;
			}
		}
		return false;
	}

	static bool IsClassChoice(const RoadStopClass &cls)
	{
		return !IsWaypointClass(cls) && GetIfClassHasNewStopsByType(&cls, roadstoptype, _cur_roadtype);
	}

	bool HasClassChoice() const override
	{
		return std::ranges::count_if(RoadStopClass::Classes(), IsClassChoice);
	}

	int GetSelectedClass() const override { return _roadstop_gui.sel_class; }
	void SetSelectedClass(int id) const override { _roadstop_gui.sel_class = this->GetClassIndex(id); }

	StringID GetClassName(int id) const override
	{
		const auto *rsc = this->GetClass(id);
		if (!IsClassChoice(*rsc)) return INVALID_STRING_ID;
		return rsc->name;
	}

	int GetSelectedType() const override { return _roadstop_gui.sel_type; }
	void SetSelectedType(int id) const override { _roadstop_gui.sel_type = id; }

	StringID GetTypeName(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (!IsRoadStopEverAvailable(spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck)) return INVALID_STRING_ID;
		return (spec == nullptr) ? STR_STATION_CLASS_DFLT_ROADSTOP : spec->name;
	}

	bool IsTypeAvailable(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		return IsRoadStopAvailable(spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck);
	}

	void DrawType(int x, int y, int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (spec == nullptr) {
			StationPickerDrawSprite(x, y, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck, INVALID_RAILTYPE, _cur_roadtype, _roadstop_gui.orientation);
		} else {
			DiagDirection orientation = _roadstop_gui.orientation;
			if (orientation < DIAGDIR_END && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) orientation = DIAGDIR_END;
			DrawRoadStopTile(x, y, _cur_roadtype, spec, roadstoptype == RoadStopType::Bus ? StationType::Bus : StationType::Truck, (uint8_t)orientation);
		}
	}

	void FillUsedItems(btree::btree_set<PickerItem> &items) override
	{
		for (const Station *st : Station::Iterate()) {
			if (st->owner != _local_company) continue;
			if (roadstoptype == RoadStopType::Truck && !(st->facilities & FACIL_TRUCK_STOP)) continue;
			if (roadstoptype == RoadStopType::Bus && !(st->facilities & FACIL_BUS_STOP)) continue;
			items.insert({0, 0, ROADSTOP_CLASS_DFLT, 0}); // We would need to scan the map to find out if default is used.
			for (const auto &sm : st->roadstop_speclist) {
				if (sm.spec == nullptr) continue;
				if (roadstoptype == RoadStopType::Truck && sm.spec->stop_type != ROADSTOPTYPE_FREIGHT && sm.spec->stop_type != ROADSTOPTYPE_ALL) continue;
				if (roadstoptype == RoadStopType::Bus && sm.spec->stop_type != ROADSTOPTYPE_PASSENGER && sm.spec->stop_type != ROADSTOPTYPE_ALL) continue;
				items.insert({sm.grfid, sm.localidx, sm.spec->class_index, sm.spec->index});
			}
		}
	}
};

template <> StringID RoadStopPickerCallbacks<RoadStopType::Bus>::GetClassTooltip() const { return STR_PICKER_ROADSTOP_BUS_CLASS_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Bus>::GetTypeTooltip() const { return STR_PICKER_ROADSTOP_BUS_TYPE_TOOLTIP; }

template <> StringID RoadStopPickerCallbacks<RoadStopType::Truck>::GetClassTooltip() const { return STR_PICKER_ROADSTOP_TRUCK_CLASS_TOOLTIP; }
template <> StringID RoadStopPickerCallbacks<RoadStopType::Truck>::GetTypeTooltip() const { return STR_PICKER_ROADSTOP_TRUCK_TYPE_TOOLTIP; }

static RoadStopPickerCallbacks<RoadStopType::Bus> _bus_callback_instance("fav_passenger_roadstops");
static RoadStopPickerCallbacks<RoadStopType::Truck> _truck_callback_instance("fav_freight_roadstops");

static PickerCallbacks &GetRoadStopPickerCallbacks(RoadStopType rs)
{
	return rs == RoadStopType::Bus ? static_cast<PickerCallbacks &>(_bus_callback_instance) : static_cast<PickerCallbacks &>(_truck_callback_instance);
}

struct BuildRoadStationWindow : public PickerWindow {
private:
	uint coverage_height; ///< Height of the coverage texts.

	void CheckOrientationValid()
	{
		const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);

		/* Raise and lower to ensure the correct widget is lowered after changing displayed orientation plane. */
		if (RoadTypeIsRoad(_cur_roadtype)) {
			this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			this->GetWidget<NWidgetStacked>(WID_BROS_AVAILABLE_ORIENTATIONS)->SetDisplayedPlane((spec != nullptr && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) ? 1 : 0);
			this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
		}

		if (_roadstop_gui.orientation >= DIAGDIR_END) return;

		if (spec != nullptr && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) {
			this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			_roadstop_gui.orientation = DIAGDIR_END;
			this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
			this->SetDirty();
			CloseWindowById(WC_SELECT_STATION, 0);
		}
	}

public:
	BuildRoadStationWindow(WindowDesc &desc, Window *parent, RoadStopType rs) : PickerWindow(desc, parent, TRANSPORT_ROAD, GetRoadStopPickerCallbacks(rs))
	{
		this->coverage_height = 2 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.vsep_normal;

		/* Trams don't have non-drivethrough stations */
		if (RoadTypeIsTram(_cur_roadtype) && _roadstop_gui.orientation < DIAGDIR_END) {
			_roadstop_gui.orientation = DIAGDIR_END;
		}
		this->ConstructWindow();

		const RoadTypeInfo *rti = GetRoadTypeInfo(_cur_roadtype);
		this->GetWidget<NWidgetCore>(WID_BROS_CAPTION)->SetString(rti->strings.picker_title[to_underlying(rs)]);

		for (WidgetID i = RoadTypeIsTram(_cur_roadtype) ? WID_BROS_STATION_X : WID_BROS_STATION_NE; i < WID_BROS_LT_OFF; i++) {
			this->GetWidget<NWidgetCore>(i)->SetToolTip(rti->strings.picker_tooltip[to_underlying(rs)]);
		}

		this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
		this->LowerWidget(WID_BROS_LT_OFF + _settings_client.gui.station_show_coverage);

		this->ChangeWindowClass((rs == RoadStopType::Bus) ? WC_BUS_STATION : WC_TRUCK_STATION);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		CloseWindowById(WC_SELECT_STATION, 0);
		this->PickerWindow::Close();
	}

	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		this->PickerWindow::OnInvalidateData(data, gui_scope);

		if (gui_scope) {
			this->CheckOrientationValid();
		}
	}

	void OnPaint() override
	{
		this->DrawWidgets();

		int rad = _settings_game.station.modified_catchment ? ((this->window_class == WC_BUS_STATION) ? CA_BUS : CA_TRUCK) : CA_UNMODIFIED;
		rad += _settings_game.station.catchment_increase;
		if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		if (this->IsShaded()) return;

		/* 'Accepts' and 'Supplies' texts. */
		StationCoverageType sct = (this->window_class == WC_BUS_STATION) ? SCT_PASSENGERS_ONLY : SCT_NON_PASSENGERS_ONLY;
		Rect r = this->GetWidget<NWidgetBase>(WID_BROS_ACCEPTANCE)->GetCurrentRect();
		int top = r.top;
		top = DrawStationCoverageAreaText(r.left, r.right, top, sct, rad, false) + WidgetDimensions::scaled.vsep_normal;
		top = DrawStationCoverageAreaText(r.left, r.right, top, sct, rad, true);
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
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				size.width  = ScaleGUITrad(PREVIEW_WIDTH) + WidgetDimensions::scaled.fullbevel.Horizontal();
				size.height = ScaleGUITrad(PREVIEW_HEIGHT) + WidgetDimensions::scaled.fullbevel.Vertical();
				break;

			case WID_BROS_ACCEPTANCE:
				size.height = this->coverage_height;
				break;

			default:
				this->PickerWindow::UpdateWidgetSize(widget, size, padding, fill, resize);
				break;
		}
	}

	/**
	 * Simply to have a easier way to get the StationType for bus, truck and trams from the WindowClass.
	 */
	StationType GetRoadStationTypeByWindowClass(WindowClass window_class) const
	{
		switch (window_class) {
			case WC_BUS_STATION:          return StationType::Bus;
			case WC_TRUCK_STATION:        return StationType::Truck;
			default: NOT_REACHED();
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y: {
				StationType st = GetRoadStationTypeByWindowClass(this->window_class);
				const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
				DrawPixelInfo tmp_dpi;
				Rect ir = r.Shrink(WidgetDimensions::scaled.bevel);
				if (FillDrawPixelInfo(&tmp_dpi, ir)) {
					AutoRestoreBackup dpi_backup(_cur_dpi, &tmp_dpi);
					int x = (ir.Width()  - ScaleSpriteTrad(PREVIEW_WIDTH)) / 2 + ScaleSpriteTrad(PREVIEW_LEFT);
					int y = (ir.Height() + ScaleSpriteTrad(PREVIEW_HEIGHT)) / 2 - ScaleSpriteTrad(PREVIEW_BOTTOM);
					if (spec == nullptr) {
						StationPickerDrawSprite(x, y, st, INVALID_RAILTYPE, _cur_roadtype, widget - WID_BROS_STATION_NE);
					} else {
						DrawRoadStopTile(x, y, _cur_roadtype, spec, st, widget - WID_BROS_STATION_NE);
					}
				}
				break;
			}

			default:
				this->PickerWindow::DrawWidget(r, widget);
				break;
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BROS_STATION_NE:
			case WID_BROS_STATION_SE:
			case WID_BROS_STATION_SW:
			case WID_BROS_STATION_NW:
			case WID_BROS_STATION_X:
			case WID_BROS_STATION_Y:
				if (widget < WID_BROS_STATION_X) {
					const RoadStopSpec *spec = RoadStopClass::Get(_roadstop_gui.sel_class)->GetSpec(_roadstop_gui.sel_type);
					if (spec != nullptr && HasBit(spec->flags, RSF_DRIVE_THROUGH_ONLY)) return;
				}
				this->RaiseWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
				_roadstop_gui.orientation = (DiagDirection)(widget - WID_BROS_STATION_NE);
				this->LowerWidget(WID_BROS_STATION_NE + _roadstop_gui.orientation);
				if (_settings_client.sound.click_beep) SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				CloseWindowById(WC_SELECT_STATION, 0);
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

			default:
				this->PickerWindow::OnClick(pt, widget, click_count);
				break;
		}
	}

	void OnRealtimeTick([[maybe_unused]] uint delta_ms) override
	{
		CheckRedrawStationCoverage(this);
	}

	static inline HotkeyList road_hotkeys{"buildroadstop", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};

	static inline HotkeyList tram_hotkeys{"buildtramstop", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};
};

/** Widget definition of the build road station window */
static constexpr NWidgetPart _nested_road_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidgetFunction(MakePickerClassWidgets),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
				NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0), SetPadding(WidgetDimensions::unscaled.picker),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BROS_AVAILABLE_ORIENTATIONS),
						/* 6-orientation plane. */
						NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
							NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
									NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NW), SetFill(0, 0), EndContainer(),
									NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_NE), SetFill(0, 0), EndContainer(),
								EndContainer(),
								NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
							EndContainer(),
							NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0),
									NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SW), SetFill(0, 0), EndContainer(),
									NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_SE), SetFill(0, 0), EndContainer(),
								EndContainer(),
								NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
							EndContainer(),
						EndContainer(),
						/* 2-orientation plane. */
						NWidget(NWID_VERTICAL), SetPIPRatio(0, 0, 1),
							NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
								NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
								NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
							EndContainer(),
						EndContainer(),
					EndContainer(),
					NWidget(WWT_LABEL, INVALID_COLOUR), SetStringTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					EndContainer(),
					NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BROS_ACCEPTANCE), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

static WindowDesc _road_station_picker_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_station_road", 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_road_station_picker_widgets,
	&BuildRoadStationWindow::road_hotkeys
);

/** Widget definition of the build tram station window */
static constexpr NWidgetPart _nested_tram_station_picker_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION,  COLOUR_DARK_GREEN, WID_BROS_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidgetFunction(MakePickerClassWidgets),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
				NWidget(NWID_VERTICAL), SetPIP(0, WidgetDimensions::unscaled.vsep_picker, 0), SetPadding(WidgetDimensions::unscaled.picker),
					NWidget(NWID_HORIZONTAL_LTR), SetPIP(0, WidgetDimensions::unscaled.hsep_normal, 0), SetPIPRatio(1, 0, 1),
						NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_X), SetFill(0, 0), EndContainer(),
						NWidget(WWT_PANEL, COLOUR_GREY, WID_BROS_STATION_Y), SetFill(0, 0), EndContainer(),
					EndContainer(),
					NWidget(WWT_LABEL, INVALID_COLOUR), SetStringTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE), SetFill(1, 0),
					NWidget(NWID_HORIZONTAL), SetPIPRatio(1, 0, 1),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_OFF), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BROS_LT_ON), SetMinimalSize(60, 12),
								SetStringTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
					EndContainer(),
					NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BROS_ACCEPTANCE), SetFill(1, 1), SetResize(1, 0), SetMinimalTextLines(2, 0),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

static WindowDesc _tram_station_picker_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_station_tram", 0, 0,
	WC_BUS_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_tram_station_picker_widgets,
	&BuildRoadStationWindow::tram_hotkeys
);

static void ShowRVStationPicker(Window *parent, RoadStopType rs)
{
	new BuildRoadStationWindow(RoadTypeIsRoad(_cur_roadtype) ? _road_station_picker_desc : _tram_station_picker_desc, parent, rs);
}

class RoadWaypointPickerCallbacks : public PickerCallbacksNewGRFClass<RoadStopClass> {
public:
	RoadWaypointPickerCallbacks() : PickerCallbacksNewGRFClass<RoadStopClass>("fav_road_waypoints") {}

	StringID GetClassTooltip() const override { return STR_PICKER_WAYPOINT_CLASS_TOOLTIP; }
	StringID GetTypeTooltip() const override { return STR_PICKER_WAYPOINT_TYPE_TOOLTIP; }

	bool IsActive() const override
	{
		for (const auto &cls : RoadStopClass::Classes()) {
			if (!IsWaypointClass(cls)) continue;
			for (const auto *spec : cls.Specs()) {
				if (spec != nullptr) return true;
			}
		}
		return false;
	}

	static bool IsWaypointClassChoice(const RoadStopClass &cls)
	{
		return IsWaypointClass(cls);
	}

	bool HasClassChoice() const override
	{
		return std::ranges::count_if(RoadStopClass::Classes(), IsWaypointClassChoice) > 1;
	}

	void Close(int) override { ResetObjectToPlace(); }
	int GetSelectedClass() const override { return _waypoint_gui.sel_class; }
	void SetSelectedClass(int id) const override { _waypoint_gui.sel_class = this->GetClassIndex(id); }

	StringID GetClassName(int id) const override
	{
		const auto *sc = GetClass(id);
		if (!IsWaypointClass(*sc)) return INVALID_STRING_ID;
		return sc->name;
	}

	int GetSelectedType() const override { return _waypoint_gui.sel_type; }
	void SetSelectedType(int id) const override { _waypoint_gui.sel_type = id; }

	StringID GetTypeName(int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		return (spec == nullptr) ? STR_STATION_CLASS_WAYP_WAYPOINT : spec->name;
	}

	bool IsTypeAvailable(int cls_id, int id) const override
	{
		return IsRoadStopAvailable(this->GetSpec(cls_id, id), StationType::RoadWaypoint);
	}

	void DrawType(int x, int y, int cls_id, int id) const override
	{
		const auto *spec = this->GetSpec(cls_id, id);
		if (spec == nullptr) {
			StationPickerDrawSprite(x, y, StationType::RoadWaypoint, INVALID_RAILTYPE, _cur_roadtype, RSV_DRIVE_THROUGH_X);
		} else {
			DrawRoadStopTile(x, y, _cur_roadtype, spec, StationType::RoadWaypoint, RSV_DRIVE_THROUGH_X);
		}
	}

	void FillUsedItems(btree::btree_set<PickerItem> &items) override
	{
		for (const Waypoint *wp : Waypoint::Iterate()) {
			if (wp->owner != _local_company || !HasBit(wp->waypoint_flags, WPF_ROAD)) continue;
			items.insert({0, 0, ROADSTOP_CLASS_WAYP, 0}); // We would need to scan the map to find out if default is used.
			for (const auto &sm : wp->roadstop_speclist) {
				if (sm.spec == nullptr) continue;
				items.insert({sm.grfid, sm.localidx, sm.spec->class_index, sm.spec->index});
			}
		}
	}

	static RoadWaypointPickerCallbacks instance;
};
/* static */ RoadWaypointPickerCallbacks RoadWaypointPickerCallbacks::instance;

struct BuildRoadWaypointWindow : public PickerWindow {
	BuildRoadWaypointWindow(WindowDesc &desc, Window *parent) : PickerWindow(desc, parent, TRANSPORT_ROAD, RoadWaypointPickerCallbacks::instance)
	{
		this->ConstructWindow();
		this->InvalidateData();
	}

	static inline HotkeyList hotkeys{"buildroadwaypoint", {
		Hotkey('F', "focus_filter_box", PCWHK_FOCUS_FILTER_BOX),
	}};
};

/** Nested widget definition for the build NewGRF road waypoint window */
static constexpr NWidgetPart _nested_build_road_waypoint_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetStringTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_DEFSIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidgetFunction(MakePickerClassWidgets),
		NWidgetFunction(MakePickerTypeWidgets),
	EndContainer(),
};

static WindowDesc _build_road_waypoint_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_road_waypoint", 0, 0,
	WC_BUILD_WAYPOINT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_road_waypoint_widgets
);

static void ShowBuildRoadWaypointPicker(Window *parent)
{
	if (!RoadWaypointPickerCallbacks::instance.IsActive()) return;
	new BuildRoadWaypointWindow(_build_road_waypoint_desc, parent);
}

void InitializeRoadGui()
{
	_road_depot_orientation = DIAGDIR_NW;
	_roadstop_gui.orientation = DIAGDIR_NW;
	_waypoint_gui.sel_class = RoadStopClassID::ROADSTOP_CLASS_WAYP;
	_waypoint_gui.sel_type = 0;
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
			for (TileIndex t(0); t < MapSize(); t++) {
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
		list.push_back(MakeDropDownListStringItem(STR_REPLACE_ALL_ROADTYPE, INVALID_ROADTYPE));
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

		SetDParam(0, rti->strings.menu_text);
		SetDParam(1, rti->max_speed / 2);
		if (for_replacement) {
			list.push_back(MakeDropDownListStringItem(rti->strings.replace_text, rt, !HasBit(avail_roadtypes, rt)));
		} else {
			StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
			list.push_back(MakeDropDownListIconItem(d, rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt)));
		}
	}

	if (list.empty()) {
		/* Empty dropdowns are not allowed */
		list.push_back(MakeDropDownListStringItem(STR_NONE, INVALID_ROADTYPE, true));
	}

	return list;
}

DropDownList GetScenRoadTypeDropDownList(RoadTramTypes rtts, bool use_name)
{
	RoadTypes avail_roadtypes = GetRoadTypes(false);
	avail_roadtypes = AddDateIntroducedRoadTypes(avail_roadtypes, CalTime::CurDate());
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

		SetDParam(0, use_name ? rti->strings.name : rti->strings.menu_text);
		SetDParam(1, rti->max_speed / 2);
		StringID str = rti->max_speed > 0 ? STR_TOOLBAR_RAILTYPE_VELOCITY : STR_JUST_STRING;
		list.push_back(MakeDropDownListIconItem(d, rti->gui_sprites.build_x_road, PAL_NONE, str, rt, !HasBit(avail_roadtypes, rt)));
	}

	if (list.empty()) {
		/* Empty dropdowns are not allowed */
		list.push_back(MakeDropDownListStringItem(STR_NONE, -1, true));
	}

	return list;
}

static BuildRoadToolbarWindow *GetRoadToolbarWindowForRoadStop(const RoadStopSpec *spec, RoadTramType rtt_preferred)
{
	extern RoadType _last_built_roadtype;
	extern RoadType _last_built_tramtype;

	BuildRoadToolbarWindow *w = dynamic_cast<BuildRoadToolbarWindow *>(FindWindowById(_game_mode == GM_EDITOR ? WC_SCEN_BUILD_TOOLBAR : WC_BUILD_TOOLBAR, TRANSPORT_ROAD));
	if (w != nullptr) {
		if (spec != nullptr && ((HasBit(spec->flags, RSF_BUILD_MENU_ROAD_ONLY) && !RoadTypeIsRoad(_cur_roadtype)) ||
				(HasBit(spec->flags, RSF_BUILD_MENU_TRAM_ONLY) && !RoadTypeIsTram(_cur_roadtype)))) {
			w->Close();
		} else {
			return w;
		}
	}

	return dynamic_cast<BuildRoadToolbarWindow *>(CreateRoadTramToolbarForRoadType(rtt_preferred == RTT_TRAM ? _last_built_tramtype : _last_built_roadtype, rtt_preferred));
}

void ShowBuildRoadStopPickerAndSelect(StationType station_type, const RoadStopSpec *spec, RoadTramType rtt_preferred)
{
	if (!IsRoadStopAvailable(spec, station_type)) return;

	RoadStopClassID class_index;
	uint16_t spec_index;
	if (spec != nullptr) {
		if (IsWaypointClass(*RoadStopClass::Get(spec->class_index)) != (station_type == StationType::RoadWaypoint)) return;
		class_index = spec->class_index;
		spec_index = spec->index;
	} else {
		class_index = (station_type == StationType::RoadWaypoint) ? ROADSTOP_CLASS_WAYP : ROADSTOP_CLASS_DFLT;
		spec_index = 0;
	}

	BuildRoadToolbarWindow *w = GetRoadToolbarWindowForRoadStop(spec, rtt_preferred);
	if (w == nullptr) return;

	auto trigger_widget = [&](WidgetID widget) {
		if (!w->IsWidgetLowered(widget)) {
			w->OnHotkey(widget);
		}
	};

	if (station_type == StationType::RoadWaypoint) {
		trigger_widget(WID_ROT_BUILD_WAYPOINT);

		BuildRoadWaypointWindow *waypoint_window = dynamic_cast<BuildRoadWaypointWindow *>(FindWindowById(WC_BUILD_WAYPOINT, TRANSPORT_ROAD));
		if (waypoint_window != nullptr) waypoint_window->PickItem(class_index, spec_index);
	} else {
		trigger_widget((station_type == StationType::Bus) ? WID_ROT_BUS_STATION : WID_ROT_TRUCK_STATION);

		BuildRoadStationWindow *roadstop_window = dynamic_cast<BuildRoadStationWindow *>(FindWindowById((station_type == StationType::Bus) ? WC_BUS_STATION : WC_TRUCK_STATION, TRANSPORT_ROAD));
		if (roadstop_window != nullptr) roadstop_window->PickItem(class_index, spec_index);
	}
}
