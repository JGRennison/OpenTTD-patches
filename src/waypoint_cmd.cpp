/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file waypoint_cmd.cpp %Command Handling for waypoints. */

#include "stdafx.h"

#include "command_func.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "bridge_map.h"
#include "town.h"
#include "waypoint_base.h"
#include "pathfinder/yapf/yapf_cache.h"
#include "pathfinder/water_regions.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "viewport_kdtree.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "string_func.h"
#include "company_func.h"
#include "newgrf_station.h"
#include "newgrf_roadstop.h"
#include "company_base.h"
#include "water.h"
#include "company_gui.h"
#include "waypoint_cmd.h"

#include "table/strings.h"

#include "safeguards.h"

/**
 * Update the virtual coords needed to draw the waypoint sign.
 */
void Waypoint::UpdateVirtCoord()
{
	if (IsHeadless()) return;
	Point pt = RemapCoords2(TileX(this->xy) * TILE_SIZE, TileY(this->xy) * TILE_SIZE);
	if (_viewport_sign_kdtree_valid && this->sign.kdtree_valid) _viewport_sign_kdtree.Remove(ViewportSignKdtreeItem::MakeWaypoint(this->index));

	auto params = MakeParameters(this->index);
	this->sign.UpdatePosition(ShouldShowBaseStationViewportLabel(this) ? ZOOM_LVL_DRAW_SPR : ZOOM_LVL_END, pt.x, pt.y - 32 * ZOOM_BASE, params, STR_WAYPOINT_NAME);

	if (_viewport_sign_kdtree_valid) _viewport_sign_kdtree.Insert(ViewportSignKdtreeItem::MakeWaypoint(this->index));

	/* Recenter viewport */
	InvalidateWindowData(WC_WAYPOINT_VIEW, this->index);
}

/**
 * Move the waypoint main coordinate somewhere else.
 * @param new_xy new tile location of the sign
 */
void Waypoint::MoveSign(TileIndex new_xy)
{
	if (this->xy == new_xy) return;

	this->BaseStation::MoveSign(new_xy);
}

/**
 * Find a deleted waypoint close to a tile.
 * @param tile to search from
 * @param str  the string to get the 'type' of
 * @param cid previous owner of the waypoint
 * @return the deleted nearby waypoint
 */
static Waypoint *FindDeletedWaypointCloseTo(TileIndex tile, StringID str, CompanyID cid, bool is_road)
{
	Waypoint *best = nullptr;
	uint thres = 8;

	for (Waypoint *wp : Waypoint::Iterate()) {
		if (!wp->IsInUse() && wp->string_id == str && wp->owner == cid && HasBit(wp->waypoint_flags, WPF_ROAD) == is_road) {
			uint cur_dist = DistanceManhattan(tile, wp->xy);

			if (cur_dist < thres) {
				thres = cur_dist;
				best = wp;
			}
		}
	}

	return best;
}

/**
 * Get the axis for a new waypoint. This means that if it is a valid
 * tile to build a waypoint on it returns a valid Axis, otherwise an
 * invalid one.
 * @param tile the tile to look at.
 * @return the axis for the to-be-build waypoint.
 */
Axis GetAxisForNewWaypoint(TileIndex tile)
{
	/* The axis for rail waypoints is easy. */
	if (IsRailWaypointTile(tile)) return GetRailStationAxis(tile);

	/* Non-plain rail type, no valid axis for waypoints. */
	if (!IsTileType(tile, MP_RAILWAY) || GetRailTileType(tile) != RAIL_TILE_NORMAL) return INVALID_AXIS;

	switch (GetTrackBits(tile)) {
		case TRACK_BIT_X: return AXIS_X;
		case TRACK_BIT_Y: return AXIS_Y;
		default:          return INVALID_AXIS;
	}
}

/**
 * Get the axis for a new road waypoint. This means that if it is a valid
 * tile to build a waypoint on it returns a valid Axis, otherwise an
 * invalid one.
 * @param tile the tile to look at.
 * @return the axis for the to-be-build waypoint.
 */
Axis GetAxisForNewRoadWaypoint(TileIndex tile)
{
	/* The axis for existing road waypoints is easy. */
	if (IsRoadWaypointTile(tile)) return GetDriveThroughStopAxis(tile);

	/* Non-plain road type, no valid axis for waypoints. */
	if (!IsNormalRoadTile(tile)) return INVALID_AXIS;

	RoadBits bits = GetAllRoadBits(tile);

	if ((bits & ROAD_Y) == 0) return AXIS_X;
	if ((bits & ROAD_X) == 0) return AXIS_Y;

	return INVALID_AXIS;
}

extern CommandCost ClearTile_Station(TileIndex tile, DoCommandFlag flags);

/**
 * Check whether the given tile is suitable for a waypoint.
 * @param tile the tile to check for suitability
 * @param axis the axis of the waypoint
 * @param waypoint Waypoint the waypoint to check for is already joined to. If we find another waypoint it can join to it will throw an error.
 */
static CommandCost IsValidTileForWaypoint(TileIndex tile, Axis axis, StationID *waypoint)
{
	/* if waypoint is set, then we have special handling to allow building on top of already existing waypoints.
	 * so waypoint points to INVALID_STATION if we can build on any waypoint.
	 * Or it points to a waypoint if we're only allowed to build on exactly that waypoint. */
	if (waypoint != nullptr && IsTileType(tile, MP_STATION)) {
		if (!IsRailWaypoint(tile)) {
			return ClearTile_Station(tile, DC_AUTO); // get error message
		} else {
			StationID wp = GetStationIndex(tile);
			if (*waypoint == INVALID_STATION) {
				*waypoint = wp;
			} else if (*waypoint != wp) {
				return CommandCost(STR_ERROR_WAYPOINT_ADJOINS_MORE_THAN_ONE_EXISTING);
			}
		}
	}

	if (GetAxisForNewWaypoint(tile) != axis) return CommandCost(STR_ERROR_NO_SUITABLE_RAILROAD_TRACK);

	Owner owner = GetTileOwner(tile);
	CommandCost ret = CheckOwnership(owner);
	if (ret.Succeeded()) ret = EnsureNoVehicleOnGround(tile);
	if (ret.Failed()) return ret;

	Slope tileh = GetTileSlope(tile);
	if (tileh != SLOPE_FLAT &&
			(!_settings_game.construction.build_on_slopes || IsSteepSlope(tileh) || !(tileh & (0x3 << axis)) || !(tileh & ~(0x3 << axis)))) {
		return CommandCost(STR_ERROR_FLAT_LAND_REQUIRED);
	}

	return CommandCost();
}

extern void GetStationLayout(uint8_t *layout, uint numtracks, uint plat_len, const StationSpec *statspec);
extern CommandCost FindJoiningWaypoint(StationID existing_station, StationID station_to_join, bool adjacent, TileArea ta, Waypoint **wp, bool is_road);
extern CommandCost CanExpandRailStation(const BaseStation *st, TileArea &new_ta);
extern CommandCost IsRailStationBridgeAboveOk(TileIndex tile, const StationSpec *statspec, uint8_t layout);

/**
 * Convert existing rail to waypoint. Eg build a waypoint station over
 * piece of rail
 * @param flags type of operation
 * @param start_tile northern most tile where waypoint will be built
 * @param axis orientation (Axis)
 * @param width width of waypoint
 * @param height height of waypoint
 * @param spec_class custom station class
 * @param spec_index custom station id
 * @param station_to_join station ID to join (NEW_STATION if build new one)
 * @param adjacent allow waypoints directly adjacent to other waypoints.
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildRailWaypoint(DoCommandFlag flags, TileIndex start_tile, Axis axis, uint8_t width, uint8_t height, StationClassID spec_class, uint16_t spec_index, StationID station_to_join, bool adjacent)
{
	if (!IsValidAxis(axis)) return CMD_ERROR;
	/* Check if the given station class is valid */
	if (static_cast<uint>(spec_class) >= StationClass::GetClassCount()) return CMD_ERROR;
	const StationClass *cls = StationClass::Get(spec_class);
	if (!IsWaypointClass(*cls)) return CMD_ERROR;
	if (spec_index >= cls->GetSpecCount()) return CMD_ERROR;

	/* The number of parts to build */
	uint8_t count = axis == AXIS_X ? height : width;

	if ((axis == AXIS_X ? width : height) != 1) return CMD_ERROR;
	if (count == 0 || count > _settings_game.station.station_spread) return CMD_ERROR;

	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Waypoint::IsValidID(station_to_join))) return CMD_ERROR;

	const StationSpec *spec = StationClass::Get(spec_class)->GetSpec(spec_index);
	TempBufferST<uint8_t> layout_ptr(count);
	if (spec == nullptr) {
		/* The layout must be 0 for the 'normal' waypoints by design. */
		memset(layout_ptr, 0, count);
	} else {
		/* But for NewGRF waypoints we like to have their style. */
		GetStationLayout(layout_ptr, count, 1, spec);
	}

	/* Make sure the area below consists of clear tiles. (OR tiles belonging to a certain rail station) */
	StationID est = INVALID_STATION;

	/* Check whether the tiles we're building on are valid rail or not. */
	TileIndexDiff offset = TileOffsByAxis(OtherAxis(axis));
	for (int i = 0; i < count; i++) {
		TileIndex tile = start_tile + i * offset;
		CommandCost ret = IsValidTileForWaypoint(tile, axis, &est);
		if (ret.Failed()) return ret;
		ret = IsRailStationBridgeAboveOk(tile, spec, layout_ptr[i]);
		if (ret.Failed()) {
			return CommandCost::DualErrorMessage(STR_ERROR_MUST_DEMOLISH_BRIDGE_FIRST, ret.GetErrorMessage());
		}
	}

	Waypoint *wp = nullptr;
	TileArea new_location(start_tile, width, height);
	CommandCost ret = FindJoiningWaypoint(est, station_to_join, adjacent, new_location, &wp, false);
	if (ret.Failed()) return ret;

	/* Check if there is an already existing, deleted, waypoint close to us that we can reuse. */
	TileIndex center_tile = start_tile + (count / 2) * offset;
	if (wp == nullptr && reuse) wp = FindDeletedWaypointCloseTo(center_tile, STR_SV_STNAME_WAYPOINT, _current_company, false);

	if (wp != nullptr) {
		/* Reuse an existing waypoint. */
		if (HasBit(wp->waypoint_flags, WPF_ROAD)) return CMD_ERROR;
		if (wp->owner != _current_company) return CommandCost(STR_ERROR_TOO_CLOSE_TO_ANOTHER_WAYPOINT);

		/* Check if we want to expand an already existing waypoint. */
		if (wp->train_station.tile != INVALID_TILE) {
			CommandCost ret = CanExpandRailStation(wp, new_location);
			if (ret.Failed()) return ret;
		}

		CommandCost ret = wp->rect.BeforeAddRect(start_tile, width, height, StationRect::ADD_TEST);
		if (ret.Failed()) return ret;
	} else {
		/* Check if we can create a new waypoint. */
		if (!Waypoint::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_STATIONS_LOADING);
	}

	/* Check if we can allocate a custom stationspec to this station */
	if (AllocateSpecToStation(spec, wp, false) == -1) return CommandCost(STR_ERROR_TOO_MANY_STATION_SPECS);

	if (flags & DC_EXEC) {
		if (wp == nullptr) {
			wp = new Waypoint(start_tile);
		} else if (!wp->IsInUse()) {
			/* Move existing (recently deleted) waypoint to the new location */
			wp->xy = start_tile;
		}
		wp->owner = GetTileOwner(start_tile);

		wp->rect.BeforeAddRect(start_tile, width, height, StationRect::ADD_TRY);

		wp->delete_ctr = 0;
		wp->facilities |= FACIL_TRAIN;
		wp->build_date = CalTime::CurDate();
		wp->string_id = STR_SV_STNAME_WAYPOINT;
		wp->train_station = new_location;

		if (wp->town == nullptr) MakeDefaultName(wp);

		wp->UpdateVirtCoord();

		uint8_t map_spec_index = AllocateSpecToStation(spec, wp, true);

		Company *c = Company::Get(wp->owner);
		for (int i = 0; i < count; i++) {
			TileIndex tile = start_tile + i * offset;
			uint8_t old_specindex = HasStationTileRail(tile) ? GetCustomStationSpecIndex(tile) : 0;
			if (!HasStationTileRail(tile)) c->infrastructure.station++;
			bool reserved = IsTileType(tile, MP_RAILWAY) ?
					HasBit(GetRailReservationTrackBits(tile), AxisToTrack(axis)) :
					HasStationReservation(tile);
			MakeRailWaypoint(tile, wp->owner, wp->index, axis, layout_ptr[i], GetRailType(tile));
			if (old_specindex != map_spec_index) DeallocateSpecFromStation(wp, old_specindex);
			SetCustomStationSpecIndex(tile, map_spec_index);

			SetRailStationTileFlags(tile, spec);

			SetRailStationReservation(tile, reserved);
			MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);

			YapfNotifyTrackLayoutChange(tile, AxisToTrack(axis));
		}
		DirtyCompanyInfrastructureWindows(wp->owner);
	}

	return CommandCost(EXPENSES_CONSTRUCTION, count * _price[PR_BUILD_WAYPOINT_RAIL]);
}

/**
 * Build a road waypoint on an existing road.
 * @param flags type of operation.
 * @param start_tile northern most tile where waypoint will be built.
 * @param axis orientation (Axis).
 * @param width width of waypoint.
 * @param height height of waypoint.
 * @param spec_class custom road stop class.
 * @param spec_index custom road stop id.
 * @param station_to_join station ID to join (NEW_STATION if build new one).
 * @param adjacent allow waypoints directly adjacent to other waypoints.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildRoadWaypoint(DoCommandFlag flags, TileIndex start_tile, Axis axis, uint8_t width, uint8_t height, RoadStopClassID spec_class, uint16_t spec_index, StationID station_to_join, bool adjacent)
{
	if (!IsValidAxis(axis)) return CMD_ERROR;
	/* Check if the given road stop class is valid */
	if (static_cast<uint>(spec_class) >= RoadStopClass::GetClassCount()) return CMD_ERROR;
	const RoadStopClass *cls = RoadStopClass::Get(spec_class);
	if (!IsWaypointClass(*cls)) return CMD_ERROR;
	if (spec_index >= cls->GetSpecCount()) return CMD_ERROR;

	const RoadStopSpec *spec = cls->GetSpec(spec_index);

	/* The number of parts to build */
	uint8_t count = axis == AXIS_X ? height : width;

	if ((axis == AXIS_X ? width : height) != 1) return CMD_ERROR;
	if (count == 0 || count > _settings_game.station.station_spread) return CMD_ERROR;

	bool reuse = (station_to_join != NEW_STATION);
	if (!reuse) station_to_join = INVALID_STATION;
	bool distant_join = (station_to_join != INVALID_STATION);

	if (distant_join && (!_settings_game.station.distant_join_stations || !Waypoint::IsValidID(station_to_join))) return CMD_ERROR;

	/* Check if the first tile and the last tile are valid */
	if (!IsValidTile(start_tile) || TileAddWrap(start_tile, width - 1, height - 1) == INVALID_TILE) return CMD_ERROR;

	TileArea roadstop_area(start_tile, width, height);
	/* Total road stop cost. */
	Money unit_cost;
	if (spec != nullptr) {
		unit_cost = spec->GetBuildCost(PR_BUILD_STATION_TRUCK);
	} else {
		unit_cost = _price[PR_BUILD_STATION_TRUCK];
	}
	CommandCost cost(EXPENSES_CONSTRUCTION, roadstop_area.w * roadstop_area.h * unit_cost);
	StationID est = INVALID_STATION;
	extern CommandCost CheckFlatLandRoadStop(TileArea tile_area, const RoadStopSpec *spec, DoCommandFlag flags, uint invalid_dirs, bool is_drive_through, StationType station_type, Axis axis, StationID *station, RoadType rt, bool require_road);
	CommandCost ret = CheckFlatLandRoadStop(roadstop_area, spec, flags, 5 << axis, true, StationType::RoadWaypoint, axis, &est, INVALID_ROADTYPE, true);
	if (ret.Failed()) return ret;
	cost.AddCost(ret);

	Waypoint *wp = nullptr;
	ret = FindJoiningWaypoint(est, station_to_join, adjacent, roadstop_area, &wp, true);
	if (ret.Failed()) return ret;

	/* Check if there is an already existing, deleted, waypoint close to us that we can reuse. */
	TileIndex center_tile = start_tile + (count / 2) * TileOffsByAxis(OtherAxis(axis));
	if (wp == nullptr && reuse) wp = FindDeletedWaypointCloseTo(center_tile, STR_SV_STNAME_WAYPOINT, _current_company, true);

	if (wp != nullptr) {
		/* Reuse an existing waypoint. */
		if (!HasBit(wp->waypoint_flags, WPF_ROAD)) return CMD_ERROR;
		if (wp->owner != _current_company) return CommandCost(STR_ERROR_TOO_CLOSE_TO_ANOTHER_WAYPOINT);

		CommandCost ret = wp->rect.BeforeAddRect(start_tile, width, height, StationRect::ADD_TEST);
		if (ret.Failed()) return ret;
	} else {
		/* allocate and initialize new waypoint */
		if (!Waypoint::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_STATIONS_LOADING);
	}

	/* Check if we can allocate a custom stationspec to this station */
	if (AllocateRoadStopSpecToStation(spec, wp, false) == -1) return CommandCost(STR_ERROR_TOO_MANY_STATION_SPECS);

	if (flags & DC_EXEC) {
		if (wp == nullptr) {
			wp = new Waypoint(start_tile);
			SetBit(wp->waypoint_flags, WPF_ROAD);
		} else if (!wp->IsInUse()) {
			/* Move existing (recently deleted) waypoint to the new location */
			wp->xy = start_tile;
		}
		wp->owner = _current_company;

		wp->rect.BeforeAddRect(start_tile, width, height, StationRect::ADD_TRY);

		if (spec != nullptr) {
			/* Include this road stop spec's animation trigger bitmask
			 * in the station's cached copy. */
			wp->cached_roadstop_anim_triggers |= spec->animation.triggers;
		}

		wp->delete_ctr = 0;
		wp->facilities |= FACIL_BUS_STOP | FACIL_TRUCK_STOP;
		wp->build_date = CalTime::CurDate();
		wp->string_id = STR_SV_STNAME_WAYPOINT;

		if (wp->town == nullptr) MakeDefaultName(wp);

		wp->UpdateVirtCoord();

		uint8_t map_spec_index = AllocateRoadStopSpecToStation(spec, wp, true);

		/* Check every tile in the area. */
		for (TileIndex cur_tile : roadstop_area) {
			/* Get existing road types and owners before any tile clearing */
			RoadType road_rt = MayHaveRoad(cur_tile) ? GetRoadType(cur_tile, RTT_ROAD) : INVALID_ROADTYPE;
			RoadType tram_rt = MayHaveRoad(cur_tile) ? GetRoadType(cur_tile, RTT_TRAM) : INVALID_ROADTYPE;
			Owner road_owner = road_rt != INVALID_ROADTYPE ? GetRoadOwner(cur_tile, RTT_ROAD) : _current_company;
			Owner tram_owner = tram_rt != INVALID_ROADTYPE ? GetRoadOwner(cur_tile, RTT_TRAM) : _current_company;

			DisallowedRoadDirections drd = DRD_NONE;
			if (road_rt != INVALID_ROADTYPE) {
				if (IsNormalRoadTile(cur_tile)){
					drd = GetDisallowedRoadDirections(cur_tile);
				} else if (IsDriveThroughStopTile(cur_tile)) {
					drd = GetDriveThroughStopDisallowedRoadDirections(cur_tile);
				}
			}

			extern CommandCost RemoveRoadStop(TileIndex tile, DoCommandFlag flags, int replacement_spec_index);
			if (IsTileType(cur_tile, MP_STATION) && IsAnyRoadStop(cur_tile)) {
				RemoveRoadStop(cur_tile, flags, map_spec_index);
			}

			wp->road_waypoint_area.Add(cur_tile);

			wp->rect.BeforeAddTile(cur_tile, StationRect::ADD_TRY);

			/* Update company infrastructure counts. If the current tile is a normal road tile, remove the old
			 * bits first. */
			if (IsNormalRoadTile(cur_tile)) {
				UpdateCompanyRoadInfrastructure(road_rt, road_owner, -(int)CountBits(GetRoadBits(cur_tile, RTT_ROAD)));
				UpdateCompanyRoadInfrastructure(tram_rt, tram_owner, -(int)CountBits(GetRoadBits(cur_tile, RTT_TRAM)));
			}

			UpdateCompanyRoadInfrastructure(road_rt, road_owner, ROAD_STOP_TRACKBIT_FACTOR);
			UpdateCompanyRoadInfrastructure(tram_rt, tram_owner, ROAD_STOP_TRACKBIT_FACTOR);

			MakeDriveThroughRoadStop(cur_tile, wp->owner, road_owner, tram_owner, wp->index, StationType::RoadWaypoint, road_rt, tram_rt, axis);
			SetDriveThroughStopDisallowedRoadDirections(cur_tile, drd);
			SetCustomRoadStopSpecIndex(cur_tile, map_spec_index);
			if (spec != nullptr) wp->SetRoadStopRandomBits(cur_tile, 0);

			Company::Get(wp->owner)->infrastructure.station++;

			MarkTileDirtyByTile(cur_tile);
			UpdateRoadCachedOneWayStatesAroundTile(cur_tile);
		}
		NotifyRoadLayoutChanged(true);
		DirtyCompanyInfrastructureWindows(wp->owner);
	}
	return cost;
}

/**
 * Build a buoy.
 * @param flags operation to perform
 * @param tile tile where to place the buoy
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildBuoy(DoCommandFlag flags, TileIndex tile)
{
	if (tile == 0 || !HasTileWaterGround(tile)) return CommandCost(STR_ERROR_SITE_UNSUITABLE);

	if (!IsTileFlat(tile)) return CommandCost(STR_ERROR_SITE_UNSUITABLE);

	/* Check if there is an already existing, deleted, waypoint close to us that we can reuse. */
	Waypoint *wp = FindDeletedWaypointCloseTo(tile, STR_SV_STNAME_BUOY, OWNER_NONE, false);
	if (wp == nullptr && !Waypoint::CanAllocateItem()) return CommandCost(STR_ERROR_TOO_MANY_STATIONS_LOADING);

	CommandCost cost(EXPENSES_CONSTRUCTION, _price[PR_BUILD_WAYPOINT_BUOY]);
	if (!IsWaterTile(tile)) {
		CommandCost ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags | DC_AUTO, tile);
		if (ret.Failed()) return ret;
		cost.AddCost(ret);
	}

	if (flags & DC_EXEC) {
		if (wp == nullptr) {
			wp = new Waypoint(tile);
		} else {
			/* Move existing (recently deleted) buoy to the new location */
			wp->xy = tile;
			InvalidateWindowData(WC_WAYPOINT_VIEW, wp->index);
		}
		wp->rect.BeforeAddTile(tile, StationRect::ADD_TRY);

		wp->string_id = STR_SV_STNAME_BUOY;

		wp->facilities |= FACIL_DOCK;
		wp->owner = OWNER_NONE;

		wp->build_date = CalTime::CurDate();

		if (wp->town == nullptr) MakeDefaultName(wp);

		MakeBuoy(tile, wp->index, GetWaterClass(tile));
		InvalidateWaterRegion(tile);
		CheckForDockingTile(tile);
		MarkTileDirtyByTile(tile);
		ClearNeighbourNonFloodingStates(tile);

		wp->UpdateVirtCoord();
		InvalidateWindowData(WC_WAYPOINT_VIEW, wp->index);
	}

	return cost;
}

/**
 * Remove a buoy
 * @param tile TileIndex been queried
 * @param flags operation to perform
 * @pre IsBuoyTile(tile)
 * @return cost or failure of operation
 */
CommandCost RemoveBuoy(TileIndex tile, DoCommandFlag flags)
{
	/* XXX: strange stuff, allow clearing as invalid company when clearing landscape */
	if (!Company::IsValidID(_current_company) && !(flags & DC_BANKRUPT)) return CommandCost(INVALID_STRING_ID);

	Waypoint *wp = Waypoint::GetByTile(tile);

	if (HasStationInUse(wp->index, false, _current_company)) return CommandCost(STR_ERROR_BUOY_IS_IN_USE);
	/* remove the buoy if there is a ship on tile when company goes bankrupt... */
	if (!(flags & DC_BANKRUPT)) {
		CommandCost ret = EnsureNoVehicleOnGround(tile);
		if (ret.Failed()) return ret;
	}

	if (flags & DC_EXEC) {
		wp->facilities &= ~FACIL_DOCK;

		InvalidateWindowData(WC_WAYPOINT_VIEW, wp->index);

		/* We have to set the water tile's state to the same state as before the
		 * buoy was placed. Otherwise one could plant a buoy on a canal edge,
		 * remove it and flood the land (if the canal edge is at level 0) */
		MakeWaterKeepingClass(tile, GetTileOwner(tile));

		wp->rect.AfterRemoveTile(wp, tile);

		wp->UpdateVirtCoord();
		wp->delete_ctr = 0;
	}

	return CommandCost(EXPENSES_CONSTRUCTION, _price[PR_CLEAR_WAYPOINT_BUOY]);
}

/**
 * Check whether the name is unique amongst the waypoints.
 * @param name The name to check.
 * @return True iff the name is unique.
 */
static bool IsUniqueWaypointName(std::string_view name)
{
	for (const Waypoint *wp : Waypoint::Iterate()) {
		if (!wp->name.empty() && wp->name == name) return false;
	}

	return true;
}

/**
 * Rename a waypoint.
 * @param flags type of operation
 * @param waypoint_id id of waypoint
 * @param text the new name or an empty string when resetting to the default
 * @return the cost of this operation or an error
 */
CommandCost CmdRenameWaypoint(DoCommandFlag flags, StationID waypoint_id, const std::string &text)
{
	Waypoint *wp = Waypoint::GetIfValid(waypoint_id);
	if (wp == nullptr) return CMD_ERROR;

	if (wp->owner != OWNER_NONE) {
		CommandCost ret = CheckOwnership(wp->owner);
		if (ret.Failed()) return ret;
	}

	bool reset = text.empty();

	if (!reset) {
		if (Utf8StringLength(text) >= MAX_LENGTH_STATION_NAME_CHARS) return CMD_ERROR;
		if (!IsUniqueWaypointName(text)) return CommandCost(STR_ERROR_NAME_MUST_BE_UNIQUE);
	}

	if (flags & DC_EXEC) {
		if (reset) {
			wp->name.clear();
		} else {
			wp->name = text;
		}

		wp->UpdateVirtCoord();
	}
	return CommandCost();
}

/**
 * Set whether waypoint label is hidden
 * @param flags type of operation
 * @param waypoint_id id of waypoint
 * @param hidden hidden state
 * @return the cost of this operation or an error
 */
CommandCost CmdSetWaypointLabelHidden(DoCommandFlag flags, StationID waypoint_id, bool hidden)
{
	Waypoint *wp = Waypoint::GetIfValid(waypoint_id);
	if (wp == nullptr) return CMD_ERROR;

	if (wp->owner != OWNER_NONE) {
		CommandCost ret = CheckOwnership(wp->owner);
		if (ret.Failed()) return ret;
	}

	if (flags & DC_EXEC) {
		AssignBit(wp->waypoint_flags, WPF_HIDE_LABEL, hidden);

		if (HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) &&
				!(_local_company != wp->owner && wp->owner != OWNER_NONE && !HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS))) {
			wp->sign.MarkDirty(ZOOM_LVL_DRAW_SPR);
		}

		InvalidateWindowData(WC_WAYPOINT_VIEW, wp->index);
	}
	return CommandCost();
}

/**
 * Exchange waypoint names
 * @param flags operation to perform
 * @param waypoint_id1 station ID to exchange name with
 * @param waypoint_id2 station ID to exchange name with
 * @return the cost of this operation or an error
 */
CommandCost CmdExchangeWaypointNames(DoCommandFlag flags, StationID waypoint_id1, StationID waypoint_id2)
{
	Waypoint *wp = Waypoint::GetIfValid(waypoint_id1);
	if (wp == nullptr) return CMD_ERROR;

	if (wp->owner != OWNER_NONE) {
		CommandCost ret = CheckOwnership(wp->owner);
		if (ret.Failed()) return ret;
	}

	Waypoint *wp2 = Waypoint::GetIfValid(waypoint_id2);
	if (wp2 == nullptr) return CMD_ERROR;

	if (wp2->owner != OWNER_NONE) {
		CommandCost ret = CheckOwnership(wp2->owner);
		if (ret.Failed()) return ret;
	}

	if (wp->town != wp2->town) return CommandCost(STR_ERROR_WAYPOINTS_NOT_IN_SAME_TOWN);
	if (!wp->IsOfType(wp2)) return CommandCost(STR_ERROR_WAYPOINTS_NOT_COMPATIBLE);

	if (flags & DC_EXEC) {
		wp->cached_name.clear();
		wp2->cached_name.clear();
		std::swap(wp->name, wp2->name);
		std::swap(wp->town_cn, wp2->town_cn);
		wp->UpdateVirtCoord();
		wp2->UpdateVirtCoord();
	}

	return CommandCost();
}
