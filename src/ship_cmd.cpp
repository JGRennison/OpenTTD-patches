/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ship_cmd.cpp Handling of ships. */

#include "stdafx.h"
#include "ship.h"
#include "landscape.h"
#include "timetable.h"
#include "news_func.h"
#include "company_func.h"
#include "pathfinder/npf/npf_func.h"
#include "depot_base.h"
#include "station_base.h"
#include "newgrf_engine.h"
#include "pathfinder/yapf/yapf.h"
#include "newgrf_sound.h"
#include "spritecache.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "engine_base.h"
#include "company_base.h"
#include "infrastructure_func.h"
#include "tunnelbridge_map.h"
#include "zoom_func.h"
#include "framerate_type.h"
#include "industry.h"
#include "industry_map.h"
#include "core/checksum_func.hpp"
#include "articulated_vehicles.h"

#include "table/strings.h"

#include "safeguards.h"

/** Directions to search towards given track bits and the ship's enter direction. */
const DiagDirection _ship_search_directions[6][4] = {
	{ DIAGDIR_NE,      INVALID_DIAGDIR, DIAGDIR_SW,      INVALID_DIAGDIR },
	{ INVALID_DIAGDIR, DIAGDIR_SE,      INVALID_DIAGDIR, DIAGDIR_NW      },
	{ INVALID_DIAGDIR, DIAGDIR_NE,      DIAGDIR_NW,      INVALID_DIAGDIR },
	{ DIAGDIR_SE,      INVALID_DIAGDIR, INVALID_DIAGDIR, DIAGDIR_SW      },
	{ DIAGDIR_NW,      DIAGDIR_SW,      INVALID_DIAGDIR, INVALID_DIAGDIR },
	{ INVALID_DIAGDIR, INVALID_DIAGDIR, DIAGDIR_SE,      DIAGDIR_NE      },
};

/**
 * Determine the effective #WaterClass for a ship travelling on a tile.
 * @param tile Tile of interest
 * @return the waterclass to be used by the ship.
 */
WaterClass GetEffectiveWaterClass(TileIndex tile)
{
	if (HasTileWaterClass(tile)) return GetWaterClass(tile);
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		dbg_assert_tile(GetTunnelBridgeTransportType(tile) == TRANSPORT_WATER, tile);
		return WATER_CLASS_CANAL;
	}
	if (IsTileType(tile, MP_RAILWAY)) {
		dbg_assert_tile(GetRailGroundType(tile) == RAIL_GROUND_WATER, tile);
		return WATER_CLASS_SEA;
	}
	NOT_REACHED();
}

static const uint16 _ship_sprites[] = {0x0E5D, 0x0E55, 0x0E65, 0x0E6D};

template <>
bool IsValidImageIndex<VEH_SHIP>(uint8 image_index)
{
	return image_index < lengthof(_ship_sprites);
}

static inline TrackBits GetTileShipTrackStatus(TileIndex tile)
{
	return TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_WATER, 0));
}

static void GetShipIcon(EngineID engine, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	uint8 spritenum = e->u.ship.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, DIR_W, image_type, result);
		if (result->IsValid()) return;

		spritenum = e->original_image_index;
	}

	dbg_assert(IsValidImageIndex<VEH_SHIP>(spritenum));
	result->Set(DIR_W + _ship_sprites[spritenum]);
}

void DrawShipEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetShipIcon(engine, image_type, &seq);

	Rect16 rect = seq.GetBounds();
	preferred_x = SoftClamp(preferred_x,
			left - UnScaleGUI(rect.left),
			right - UnScaleGUI(rect.right));

	seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
}

/**
 * Get the size of the sprite of a ship sprite heading west (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetShipSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetShipIcon(engine, image_type, &seq);

	Rect rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

	width  = UnScaleGUI(rect.Width());
	height = UnScaleGUI(rect.Height());
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);
}

void Ship::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8 spritenum = this->spritenum;

	if (image_type == EIT_ON_MAP) direction = this->rotation;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, direction, image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	dbg_assert(IsValidImageIndex<VEH_SHIP>(spritenum));
	result->Set(_ship_sprites[spritenum] + direction);
}

static const Depot *FindClosestShipDepot(const Vehicle *v, uint max_distance)
{
	/* Find the closest depot */
	const Depot *best_depot = nullptr;
	/* If we don't have a maximum distance, i.e. distance = 0,
	 * we want to find any depot so the best distance of no
	 * depot must be more than any correct distance. On the
	 * other hand if we have set a maximum distance, any depot
	 * further away than max_distance can safely be ignored. */
	uint best_dist = max_distance == 0 ? UINT_MAX : max_distance + 1;

	for (const Depot *depot : Depot::Iterate()) {
		TileIndex tile = depot->xy;
		if (IsShipDepotTile(tile) && IsInfraTileUsageAllowed(VEH_SHIP, v->owner, tile)) {
			uint dist = DistanceManhattan(tile, v->tile);
			if (dist < best_dist) {
				best_dist = dist;
				best_depot = depot;
			}
		}
	}

	return best_depot;
}

static void CheckIfShipNeedsService(Vehicle *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_ships == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	uint max_distance;
	switch (_settings_game.pf.pathfinder_for_ships) {
		case VPF_NPF:  max_distance = _settings_game.pf.npf.maximum_go_to_depot_penalty  / NPF_TILE_LENGTH;  break;
		case VPF_YAPF: max_distance = _settings_game.pf.yapf.maximum_go_to_depot_penalty / YAPF_TILE_LENGTH; break;
		default: NOT_REACHED();
	}

	const Depot *depot = FindClosestShipDepot(v, max_distance);

	if (depot == nullptr) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		return;
	}

	v->current_order.MakeGoToDepot(depot->index, ODTFB_SERVICE);
	v->SetDestTile(depot->xy);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
}

/**
 * Update the caches of this ship.
 */
void Ship::UpdateCache()
{
	const ShipVehicleInfo *svi = ShipVehInfo(this->engine_type);

	/* Get speed fraction for the current water type. Aqueducts are always canals. */
	bool is_ocean = GetEffectiveWaterClass(this->tile) == WATER_CLASS_SEA;
	uint raw_speed = GetVehicleProperty(this, PROP_SHIP_SPEED, svi->max_speed);
	this->vcache.cached_max_speed = svi->ApplyWaterClassSpeedFrac(raw_speed, is_ocean);

	/* Update cargo aging period. */
	for (Ship *u = this; u != nullptr; u = u->Next()) {
		u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_SHIP_CARGO_AGE_PERIOD, EngInfo(u->engine_type)->cargo_age_period);
	}

	this->UpdateVisualEffect();

	SetBit(this->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);
}

Money Ship::GetRunningCost() const
{
	const Engine *e = this->GetEngine();
	uint cost_factor = GetVehicleProperty(this, PROP_SHIP_RUNNING_COST_FACTOR, e->u.ship.running_cost);
	Money cost = GetPrice(PR_RUNNING_SHIP, cost_factor, e->GetGRF());

	if (this->cur_speed == 0) {
		if (this->IsInDepot()) {
			/* running costs if in depot */
			cost = CeilDivT<Money>(cost, _settings_game.difficulty.vehicle_costs_in_depot);
		} else {
			/* running costs if stopped */
			cost = CeilDivT<Money>(cost, _settings_game.difficulty.vehicle_costs_when_stopped);
		}
	}
	return cost;
}

void Ship::OnNewDay()
{
	if (!this->IsPrimaryVehicle()) return;

	if ((++this->day_counter & 7) == 0) {
		DecreaseVehicleValue(this);
	}
	AgeVehicle(this);
}

void Ship::OnPeriodic()
{
	if (!this->IsPrimaryVehicle()) return;

	CheckVehicleBreakdown(this);
	CheckIfShipNeedsService(this);

	CheckOrders(this);

	if (this->running_ticks == 0) return;

	CommandCost cost(EXPENSES_SHIP_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR * DAY_TICKS));

	this->profit_this_year -= cost.GetCost();
	this->running_ticks = 0;

	SubtractMoneyFromCompanyFract(this->owner, cost);

	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	/* we need this for the profit */
	DirtyVehicleListWindowForVehicle(this);
}

Trackdir Ship::GetVehicleTrackdir() const
{
	if (this->vehstatus & VS_CRASHED) return INVALID_TRACKDIR;

	if (this->IsInDepot()) {
		/* We'll assume the ship is facing outwards */
		return DiagDirToDiagTrackdir(GetShipDepotDirection(this->tile));
	}

	if (this->state == TRACK_BIT_WORMHOLE) {
		/* ship on aqueduct, so just use its direction and assume a diagonal track */
		return DiagDirToDiagTrackdir(DirToDiagDir(this->direction));
	}

	return TrackDirectionToTrackdir(FindFirstTrack(this->state), this->direction);
}

void Ship::MarkDirty()
{
	this->colourmap = PAL_NONE;
	this->InvalidateImageCache();
	this->UpdateViewport(true, false);
	this->UpdateCache();
}

void Ship::PlayLeaveStationSound(bool force) const
{
	if (PlayVehicleSound(this, VSE_START, force)) return;
	SndPlayVehicleFx(ShipVehInfo(this->engine_type)->sfx, this);
}

TileIndex Ship::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	const Station *st = Station::Get(station);
	if (CanVehicleUseStation(this, st)) {
		return st->xy;
	} else {
		this->IncrementRealOrderIndex();
		return 0;
	}
}

void Ship::UpdateDeltaXY()
{
	static const int8 _delta_xy_table[8][4] = {
		/* y_extent, x_extent, y_offs, x_offs */
		{ 6,  6,  -3,  -3}, // N
		{ 6, 32,  -3, -16}, // NE
		{ 6,  6,  -3,  -3}, // E
		{32,  6, -16,  -3}, // SE
		{ 6,  6,  -3,  -3}, // S
		{ 6, 32,  -3, -16}, // SW
		{ 6,  6,  -3,  -3}, // W
		{32,  6, -16,  -3}, // NW
	};

	const int8 *bb = _delta_xy_table[this->rotation];
	this->x_offs        = bb[3];
	this->y_offs        = bb[2];
	this->x_extent      = bb[1];
	this->y_extent      = bb[0];
	this->z_extent      = 6;

	if (this->direction != this->rotation) {
		/* If we are rotating, then it is possible the ship was moved to its next position. In that
		 * case, because we are still showing the old direction, the ship will appear to glitch sideways
		 * slightly. We can work around this by applying an additional offset to make the ship appear
		 * where it was before it moved. */
		this->x_offs -= this->x_pos - this->rotation_x_pos;
		this->y_offs -= this->y_pos - this->rotation_y_pos;
	}
}

bool RecentreShipSpriteBounds(Vehicle *v)
{
	Ship *ship = Ship::From(v);
	if (ship->rotation != ship->cur_image_valid_dir) {
		ship->cur_image_valid_dir  = INVALID_DIR;
		Point offset = RemapCoords(ship->x_offs, ship->y_offs, 0);
		ship->sprite_seq_bounds.left = -offset.x - 16;
		ship->sprite_seq_bounds.right = ship->sprite_seq_bounds.left + 32;
		ship->sprite_seq_bounds.top = -offset.y - 16;
		ship->sprite_seq_bounds.bottom = ship->sprite_seq_bounds.top + 32;
		return true;
	}
	return false;
}

int Ship::GetEffectiveMaxSpeed() const
{
	int max_speed = this->vcache.cached_max_speed;

	if (this->critical_breakdown_count == 0) return max_speed;

	for (uint i = 0; i < this->critical_breakdown_count; i++) {
		max_speed = std::min(max_speed - (max_speed / 3) + 1, max_speed);
	}

	/* clamp speed to be no less than lower of 5mph and 1/8 of base speed */
	return std::max<uint16>(max_speed, std::min<uint16>(10, (this->vcache.cached_max_speed + 7) >> 3));
}

/**
 * Test-procedure for HasVehicleOnPos to check for any ships which are visible and not stopped by the player.
 */
static Vehicle *EnsureNoMovingShipProc(Vehicle *v, void *data)
{
	return (v->vehstatus & (VS_HIDDEN | VS_STOPPED)) == 0 ? v : nullptr;
}

static bool CheckReverseShip(const Ship *v, Trackdir *trackdir = nullptr)
{
	/* Ask pathfinder for best direction */
	bool reverse = false;
	switch (_settings_game.pf.pathfinder_for_ships) {
		case VPF_NPF: reverse = NPFShipCheckReverse(v, trackdir); break;
		case VPF_YAPF: reverse = YapfShipCheckReverse(v, trackdir); break;
		default: NOT_REACHED();
	}
	return reverse;
}

static bool CheckShipLeaveDepot(Ship *v)
{
	if (!v->IsChainInDepot()) return false;

	if (v->current_order.IsWaitTimetabled()) {
		v->HandleWaiting(false, true);
	}
	if (v->current_order.IsType(OT_WAITING)) {
		return true;
	}

	/* We are leaving a depot, but have to go to the exact same one; re-enter */
	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			IsShipDepotTile(v->tile) && GetDepotIndex(v->tile) == v->current_order.GetDestination()) {
		VehicleEnterDepot(v);
		return true;
	}

	/* Don't leave depot if no destination set */
	if (v->dest_tile == 0) return true;

	/* Don't leave depot if another vehicle is already entering/leaving */
	/* This helps avoid CPU load if many ships are set to start at the same time */
	if (HasVehicleOnPos(v->tile, VEH_SHIP, nullptr, &EnsureNoMovingShipProc)) return true;

	TileIndex tile = v->tile;
	Axis axis = GetShipDepotAxis(tile);

	DiagDirection north_dir = ReverseDiagDir(AxisToDiagDir(axis));
	TileIndex north_neighbour = TILE_ADD(tile, TileOffsByDiagDir(north_dir));
	DiagDirection south_dir = AxisToDiagDir(axis);
	TileIndex south_neighbour = TILE_ADD(tile, 2 * TileOffsByDiagDir(south_dir));

	TrackBits north_tracks = DiagdirReachesTracks(north_dir) & GetTileShipTrackStatus(north_neighbour);
	TrackBits south_tracks = DiagdirReachesTracks(south_dir) & GetTileShipTrackStatus(south_neighbour);
	if (north_tracks && south_tracks) {
		if (CheckReverseShip(v)) north_tracks = TRACK_BIT_NONE;
	}

	if (north_tracks) {
		/* Leave towards north */
		v->rotation = v->direction = DiagDirToDir(north_dir);
	} else if (south_tracks) {
		/* Leave towards south */
		v->rotation = v->direction = DiagDirToDir(south_dir);
	} else {
		/* Both ways blocked */
		return false;
	}

	v->state = AxisToTrackBits(axis);
	v->vehstatus &= ~VS_HIDDEN;
	v->UpdateIsDrawn();

	v->cur_speed = 0;
	v->UpdateViewport(true, true);
	SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);

	v->PlayLeaveStationSound();
	VehicleServiceInDepot(v);
	InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	DirtyVehicleListWindowForVehicle(v);

	return false;
}

static inline void UpdateShipSpeed(Vehicle *v, uint speed)
{
	if (v->cur_speed == speed) return;

	v->cur_speed = speed;

	/* updates statusbar only if speed have changed to save CPU time */
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);

	if (HasBit(v->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE)) {
		v->InvalidateImageCacheOfChain();
	}
}

/**
 * Accelerates the ship towards its target speed.
 * @param v Ship to accelerate.
 * @return Number of steps to move the ship.
 */
static uint ShipAccelerate(Vehicle *v)
{
	uint speed;

	speed = std::min<uint>(v->cur_speed + 1, Ship::From(v)->GetEffectiveMaxSpeed());
	speed = std::min<uint>(speed, v->current_order.GetMaxSpeed() * 2);

	if (v->breakdown_ctr == 1 && v->breakdown_type == BREAKDOWN_LOW_POWER && v->cur_speed > (v->breakdown_severity * ShipVehInfo(v->engine_type)->max_speed) >> 8) {
		if ((v->tick_counter & 0x7) == 0 && v->cur_speed > 0) {
			speed = v->cur_speed - 1;
		} else {
			speed = v->cur_speed;
		}
	}

	if (v->breakdown_ctr == 1 && v->breakdown_type == BREAKDOWN_LOW_SPEED) {
		speed = std::min<uint>(speed, v->breakdown_severity);
	}

	UpdateShipSpeed(v, speed);

	const uint advance_speed = v->GetAdvanceSpeed(speed);
	const uint number_of_steps = (advance_speed + v->progress) / v->GetAdvanceDistance();
	const uint remainder = (advance_speed + v->progress) % v->GetAdvanceDistance();
	dbg_assert(remainder <= std::numeric_limits<byte>::max());
	v->progress = static_cast<byte>(remainder);
	return number_of_steps;
}

/**
 * Ship arrives at a dock. If it is the first time, send out a news item.
 * @param v  Ship that arrived.
 * @param st Station being visited.
 */
static void ShipArrivesAt(const Vehicle *v, Station *st)
{
	/* Check if station was ever visited before */
	if (!(st->had_vehicle_of_type & HVOT_SHIP)) {
		st->had_vehicle_of_type |= HVOT_SHIP;

		SetDParam(0, st->index);
		AddVehicleNewsItem(
			STR_NEWS_FIRST_SHIP_ARRIVAL,
			(v->owner == _local_company) ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
			v->index,
			st->index
		);
		AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
	}
}


/**
 * Runs the pathfinder to choose a track to continue along.
 *
 * @param v Ship to navigate
 * @param tile Tile, the ship is about to enter
 * @param enterdir Direction of entering
 * @param tracks Available track choices on \a tile
 * @return Track to choose, or INVALID_TRACK when to reverse.
 */
static Track ChooseShipTrack(Ship *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks)
{
	dbg_assert(IsValidDiagDirection(enterdir));

	bool path_found = true;
	Track track;

	if (v->dest_tile == 0) {
		/* No destination, don't invoke pathfinder. */
		track = TrackBitsToTrack(v->state);
		if (!IsDiagonalTrack(track)) track = TrackToOppositeTrack(track);
		if (!HasBit(tracks, track)) track = FindFirstTrack(tracks);
		path_found = false;
	} else {
		/* Attempt to follow cached path. */
		if (!v->path.empty()) {
			track = TrackdirToTrack(v->path.front());

			if (HasBit(tracks, track)) {
				v->path.pop_front();
				/* HandlePathfindResult() is not called here because this is not a new pathfinder result. */
				return track;
			}

			/* Cached path is invalid so continue with pathfinder. */
			v->path.clear();
		}

		switch (_settings_game.pf.pathfinder_for_ships) {
			case VPF_NPF: track = NPFShipChooseTrack(v, path_found); break;
			case VPF_YAPF: track = YapfShipChooseTrack(v, tile, enterdir, tracks, path_found, v->path); break;
			default: NOT_REACHED();
		}
	}
	DEBUG_UPDATESTATECHECKSUM("ChooseShipTrack: v: %u, path_found: %d, track: %d", v->index, path_found, track);
	UpdateStateChecksum((((uint64) v->index) << 32) | (path_found << 16) | track);

	v->HandlePathfindingResult(path_found);
	return track;
}

/**
 * Get the available water tracks on a tile for a ship entering a tile.
 * @param tile The tile about to enter.
 * @param dir The entry direction.
 * @return The available trackbits on the next tile.
 */
static inline TrackBits GetAvailShipTracks(TileIndex tile, DiagDirection dir)
{
	TrackBits tracks = GetTileShipTrackStatus(tile) & DiagdirReachesTracks(dir);

	return tracks;
}

/** Structure for ship sub-coordinate data for moving into a new tile via a Diagdir onto a Track. */
struct ShipSubcoordData {
	byte x_subcoord; ///< New X sub-coordinate on the new tile
	byte y_subcoord; ///< New Y sub-coordinate on the new tile
	Direction dir;   ///< New Direction to move in on the new track
};
/** Ship sub-coordinate data for moving into a new tile via a Diagdir onto a Track.
 * Array indexes are Diagdir, Track.
 * There will always be three possible tracks going into an adjacent tile via a Diagdir,
 * so each Diagdir sub-array will have three valid and three invalid structures per Track.
 */
static const ShipSubcoordData _ship_subcoord[DIAGDIR_END][TRACK_END] = {
	// DIAGDIR_NE
	{
		{15,  8, DIR_NE},      // TRACK_X
		{ 0,  0, INVALID_DIR}, // TRACK_Y
		{ 0,  0, INVALID_DIR}, // TRACK_UPPER
		{15,  8, DIR_E},       // TRACK_LOWER
		{15,  7, DIR_N},       // TRACK_LEFT
		{ 0,  0, INVALID_DIR}, // TRACK_RIGHT
	},
	// DIAGDIR_SE
	{
		{ 0,  0, INVALID_DIR}, // TRACK_X
		{ 8,  0, DIR_SE},      // TRACK_Y
		{ 7,  0, DIR_E},       // TRACK_UPPER
		{ 0,  0, INVALID_DIR}, // TRACK_LOWER
		{ 8,  0, DIR_S},       // TRACK_LEFT
		{ 0,  0, INVALID_DIR}, // TRACK_RIGHT
	},
	// DIAGDIR_SW
	{
		{ 0,  8, DIR_SW},      // TRACK_X
		{ 0,  0, INVALID_DIR}, // TRACK_Y
		{ 0,  7, DIR_W},       // TRACK_UPPER
		{ 0,  0, INVALID_DIR}, // TRACK_LOWER
		{ 0,  0, INVALID_DIR}, // TRACK_LEFT
		{ 0,  8, DIR_S},       // TRACK_RIGHT
	},
	// DIAGDIR_NW
	{
		{ 0,  0, INVALID_DIR}, // TRACK_X
		{ 8, 15, DIR_NW},      // TRACK_Y
		{ 0,  0, INVALID_DIR}, // TRACK_UPPER
		{ 8, 15, DIR_W},       // TRACK_LOWER
		{ 0,  0, INVALID_DIR}, // TRACK_LEFT
		{ 7, 15, DIR_N},       // TRACK_RIGHT
	}
};

/** Temporary data storage for testing collisions. */
struct ShipCollideChecker {

	TrackBits track_bits;   ///< Pathfinder chosen track converted to trackbits, or is v->state of requesting ship. (one bit set)
	TileIndex search_tile;  ///< The tile that we really want to check.
	Ship *v;                ///< Ship we are testing for collision.
};

/** Helper function for collision avoidance. */
static Vehicle *FindShipOnTile(Vehicle *v, void *data)
{
	ShipCollideChecker *scc = (ShipCollideChecker*)data;

	/* Don't detect vehicles on different parallel tracks. */
	TrackBits bits = scc->track_bits | Ship::From(v)->state;
	if (bits == TRACK_BIT_HORZ || bits == TRACK_BIT_VERT) return nullptr;

	/* Don't detect ships passing on aquaduct. */
	if (abs(v->z_pos - scc->v->z_pos) >= 8) return nullptr;

	/* Only requested tiles are checked. avoid desync. */
	if (TileVirtXY(v->x_pos, v->y_pos) != scc->search_tile) return nullptr;

	return v;
}

/**
 * Adjust speed while on aqueducts.
 * @param search_tile  Tile that the requesting ship will check, one will be added to look in front of the bow.
 * @param ramp         Ramp tile from aqueduct.
 * @param v            Ship that does the request.
 * @return Allways false.
 */
static bool HandleSpeedOnAqueduct(Ship *v, TileIndex tile, TileIndex ramp)
{
	TileIndexDiffC ti = TileIndexDiffCByDir(v->direction);

	ShipCollideChecker scc;
	scc.v = v;
	scc.track_bits = TRACK_BIT_NONE;
	scc.search_tile = TileAddWrap(tile, ti.x, ti.y);

	if (scc.search_tile == INVALID_TILE) return false;

	if (IsValidTile(scc.search_tile) &&
			(HasVehicleOnPos(ramp, VEH_SHIP, &scc, FindShipOnTile) ||
			HasVehicleOnPos(GetOtherTunnelBridgeEnd(ramp), VEH_SHIP, &scc, FindShipOnTile))) {
		UpdateShipSpeed(v, v->cur_speed / 4);
	}
	return false;
}

/**
 * If there is imminent collision or worse, direction and speed will be adjusted.
 * @param tile        Tile that the ship is about to enter.
 * @param v           Ship that does the request.
 * @param tracks      The available tracks that could be followed.
 * @param track_old   The track that the pathfinder assigned.
 * @param diagdir     The DiagDirection that tile will be entered.
 * @return The new track if found.
 */
static void CheckDistanceBetweenShips(TileIndex tile, Ship *v, TrackBits tracks, Track *track_old, DiagDirection diagdir)
{
	// No checking close to docks and depots.
	if (v->current_order.IsType(OT_GOTO_STATION)) {
		Station *st = Station::Get(v->current_order.GetDestination());
		if (st->IsWithinRangeOfDockingTile(tile, 3)) return;
	} else if (!v->current_order.IsType(OT_GOTO_WAYPOINT)) {
		if (DistanceManhattan(v->dest_tile, tile) <= 3) return;
	}

	Track track = *track_old;
	TrackBits track_bits = TrackToTrackBits(track);

	/* Only check for collision when pathfinder did not change direction.
	 * This is done in order to keep ships moving towards the intended target. */
	TrackBits combine = (v->state | track_bits);
	if (combine != TRACK_BIT_HORZ && combine != TRACK_BIT_VERT && combine != track_bits) return;

	TileIndexDiffC ti;
	ShipCollideChecker scc;
	scc.v = v;
	scc.track_bits = track_bits;
	scc.search_tile = tile;

	bool found = HasVehicleOnPos(tile, VEH_SHIP, &scc, FindShipOnTile);

	if (!found) {
		/* Bridge entrance */
		if (IsBridgeTile(tile) && HandleSpeedOnAqueduct(v, tile, tile)) return;

		scc.track_bits = TrackToTrackBits(IsDiagonalTrack(track) ? track : TrackToOppositeTrack(track));
		ti = TileIndexDiffCByDiagDir(_ship_search_directions[track][diagdir]);
		scc.search_tile = TileAddWrap(tile, ti.x, ti.y);
		if (scc.search_tile == INVALID_TILE) return;

		found = HasVehicleOnPos(scc.search_tile, VEH_SHIP, &scc, FindShipOnTile);
	}
	if (!found) {
		scc.track_bits = track_bits;
		ti = TileIndexDiffCByDiagDir(diagdir);
		scc.search_tile = TileAddWrap(scc.search_tile, ti.x, ti.y);
		if (scc.search_tile == INVALID_TILE) return;

		found = HasVehicleOnPos(scc.search_tile, VEH_SHIP, &scc, FindShipOnTile);
	}
	if (found) {

		/* Speed adjustment related to distance. */
		UpdateShipSpeed(v, v->cur_speed / (scc.search_tile == tile ? 8 : 2));

		/* Clean none wanted trackbits, including pathfinder track, TRACK_BIT_WORMHOLE and no 90 degree turns. */
		if (IsDiagonalTrack(track)) {
			ClrBit(tracks, track);
		} else {
			tracks &= TRACK_BIT_CROSS;
		}

		/* Just follow track 1 tile and see if there is a track to follow. (try not to bang in coast or ship) */
		while (tracks != TRACK_BIT_NONE) {
			track = RemoveFirstTrack(&tracks);

			ti = TileIndexDiffCByDiagDir(_ship_search_directions[track][diagdir]);
			TileIndex tile_check = TileAddWrap(tile, ti.x, ti.y);
			if (tile_check == INVALID_TILE) continue;

			scc.search_tile = tile_check;
			scc.track_bits = TrackToTrackBits(IsDiagonalTrack(track) ? track : TrackToOppositeTrack(track));
			if (HasVehicleOnPos(scc.search_tile, VEH_SHIP, &scc, FindShipOnTile)) continue;

			TrackBits bits = GetTileShipTrackStatus(tile_check) & DiagdirReachesTracks(_ship_search_directions[track][diagdir]);
			if (!IsDiagonalTrack(track)) bits &= TRACK_BIT_CROSS;  // No 90 degree turns.

			if (bits != INVALID_TRACK_BIT && bits != TRACK_BIT_NONE) {
				*track_old = track;
				break;
			}
		}
	}
}

/**
 * Test if a ship is in the centre of a lock and should move up or down.
 * @param v Ship being tested.
 * @return 0 if ship is not moving in lock, or -1 to move down, 1 to move up.
 */
static int ShipTestUpDownOnLock(const Ship *v)
{
	/* Suitable tile? */
	if (!IsTileType(v->tile, MP_WATER) || !IsLock(v->tile) || GetLockPart(v->tile) != LOCK_PART_MIDDLE) return 0;

	/* Must be at the centre of the lock */
	if ((v->x_pos & 0xF) != 8 || (v->y_pos & 0xF) != 8) return 0;

	DiagDirection diagdir = GetInclinedSlopeDirection(GetTileSlope(v->tile));
	dbg_assert(IsValidDiagDirection(diagdir));

	if (DirToDiagDir(v->direction) == diagdir) {
		/* Move up */
		return (v->z_pos < GetTileMaxZ(v->tile) * (int)TILE_HEIGHT) ? 1 : 0;
	} else {
		/* Move down */
		return (v->z_pos > GetTileZ(v->tile) * (int)TILE_HEIGHT) ? -1 : 0;
	}
}

/**
 * Test and move a ship up or down in a lock.
 * @param v Ship to move.
 * @return true iff ship is moving up or down in a lock.
 */
static bool ShipMoveUpDownOnLock(Ship *v)
{
	/* Moving up/down through lock */
	int dz = ShipTestUpDownOnLock(v);
	if (dz == 0) return false;

	UpdateShipSpeed(v, 0);

	if ((v->tick_counter & 7) == 0) {
		v->z_pos += dz;
		v->UpdatePosition();
		v->UpdateViewport(true, true);
	}

	return true;
}

/**
 * Test if a tile is a docking tile for the given station.
 * @param tile Docking tile to test.
 * @param station Destination station.
 * @return true iff docking tile is next to station.
 */
bool IsShipDestinationTile(TileIndex tile, StationID station)
{
	dbg_assert(IsDockingTile(tile));
	/* Check each tile adjacent to docking tile. */
	for (DiagDirection d = DIAGDIR_BEGIN; d != DIAGDIR_END; d++) {
		TileIndex t = tile + TileOffsByDiagDir(d);
		if (!IsValidTile(t)) continue;
		if (IsDockTile(t) && GetStationIndex(t) == station && IsValidDockingDirectionForDock(t, d)) return true;
		if (IsTileType(t, MP_INDUSTRY)) {
			const Industry *i = Industry::GetByTile(t);
			if (i->neutral_station != nullptr && i->neutral_station->index == station) return true;
		}
		if (IsTileType(t, MP_STATION) && IsOilRig(t) && GetStationIndex(t) == station) return true;
	}
	return false;
}

static void ReverseShipIntoTrackdir(Ship *v, Trackdir trackdir)
{
	static constexpr Direction _trackdir_to_direction[] = {
		DIR_NE, DIR_SE, DIR_E, DIR_E, DIR_S, DIR_S, INVALID_DIR, INVALID_DIR,
		DIR_SW, DIR_NW, DIR_W, DIR_W, DIR_N, DIR_N, INVALID_DIR, INVALID_DIR,
	};

	v->direction = _trackdir_to_direction[trackdir];
	dbg_assert(v->direction != INVALID_DIR);
	v->state = TrackdirBitsToTrackBits(TrackdirToTrackdirBits(trackdir));

	/* Remember our current location to avoid movement glitch */
	v->rotation_x_pos = v->x_pos;
	v->rotation_y_pos = v->y_pos;
	UpdateShipSpeed(v, 0);
	v->path.clear();

	v->UpdatePosition();
	v->UpdateViewport(true, true);
}

static void ReverseShip(Ship *v)
{
	v->direction = ReverseDir(v->direction);

	/* Remember our current location to avoid movement glitch */
	v->rotation_x_pos = v->x_pos;
	v->rotation_y_pos = v->y_pos;
	UpdateShipSpeed(v, 0);
	v->path.clear();

	v->UpdatePosition();
	v->UpdateViewport(true, true);
}

static void ShipController(Ship *v)
{
	v->tick_counter++;
	v->current_order_time++;

	if (v->HandleBreakdown()) return;

	if (v->vehstatus & VS_STOPPED) return;

	if (ProcessOrders(v) && CheckReverseShip(v)) return ReverseShip(v);

	v->HandleLoading();

	if (v->current_order.IsType(OT_LOADING)) return;

	if (CheckShipLeaveDepot(v)) return;

	v->ShowVisualEffect(UINT_MAX);

	/* Rotating on spot */
	if (v->direction != v->rotation) {
		if ((v->tick_counter & 7) == 0) {
			DirDiff diff = DirDifference(v->direction, v->rotation);
			v->rotation = ChangeDir(v->rotation, diff > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
			v->UpdateViewport(true, true);
		}
		return;
	}

	if (ShipMoveUpDownOnLock(v)) return;

	uint number_of_steps = ShipAccelerate(v);
	if (number_of_steps == 0 && v->current_order.IsType(OT_LEAVESTATION)) number_of_steps = 1;
	for (uint i = 0; i < number_of_steps; ++i) {
		GetNewVehiclePosResult gp = GetNewVehiclePos(v);
		if (v->state != TRACK_BIT_WORMHOLE) {
			/* Not on a bridge */
			if (gp.old_tile == gp.new_tile) {
				/* Staying in tile */
				if (v->IsInDepot()) {
					gp.x = v->x_pos;
					gp.y = v->y_pos;
				} else {
					/* Not inside depot */
					const VehicleEnterTileStatus r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
					if (HasBit(r, VETS_CANNOT_ENTER)) return ReverseShip(v);

					/* A leave station order only needs one tick to get processed, so we can
					 * always skip ahead. */
					if (v->current_order.IsType(OT_LEAVESTATION)) {
						StationID station_id = v->current_order.GetDestination();
						v->current_order.Free();

						bool may_reverse = ProcessOrders(v);

						if (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == station_id &&
								IsDockingTile(gp.new_tile) && Company::Get(v->owner)->settings.remain_if_next_order_same_station) {
							Station *st = Station::Get(station_id);
							if (st->facilities & FACIL_DOCK && st->docking_station.Contains(gp.new_tile) && IsShipDestinationTile(gp.new_tile, station_id)) {
								v->last_station_visited = station_id;
								ShipArrivesAt(v, st);
								v->BeginLoading();
								return;
							}
						}

						v->PlayLeaveStationSound();

						SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
						if (may_reverse && CheckReverseShip(v)) return ReverseShip(v);
						/* Test if continuing forward would lead to a dead-end, moving into the dock. */
						const DiagDirection exitdir = VehicleExitDir(v->direction, v->state);
						const TileIndex tile = TileAddByDiagDir(v->tile, exitdir);
						if (TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_WATER, 0, exitdir)) == TRACK_BIT_NONE) return ReverseShip(v);
					} else if (v->dest_tile != 0) {
						/* We have a target, let's see if we reached it... */
						if (v->current_order.IsType(OT_GOTO_WAYPOINT) &&
								DistanceManhattan(v->dest_tile, gp.new_tile) <= 3) {
							/* We got within 3 tiles of our target buoy, so let's skip to our
							 * next order */
							UpdateVehicleTimetable(v, true);
							v->IncrementRealOrderIndex();
							v->current_order.MakeDummy();
						} else if (v->current_order.IsType(OT_GOTO_DEPOT) &&
								v->dest_tile == gp.new_tile) {
							/* Depot orders really need to reach the tile */
							if ((gp.x & 0xF) == 8 && (gp.y & 0xF) == 8) {
								VehicleEnterDepot(v);
								return;
							}
						} else if (v->current_order.IsType(OT_GOTO_STATION) && IsDockingTile(gp.new_tile)) {
							/* Process station in the orderlist. */
							Station *st = Station::Get(v->current_order.GetDestination());
							if (st->docking_station.Contains(gp.new_tile) && IsShipDestinationTile(gp.new_tile, st->index)) {
								v->last_station_visited = st->index;
								if (st->facilities & FACIL_DOCK) { // ugly, ugly workaround for problem with ships able to drop off cargo at wrong stations
									ShipArrivesAt(v, st);
									v->BeginLoading();
								} else { // leave stations without docks right away
									v->current_order.MakeLeaveStation();
									v->IncrementRealOrderIndex();
								}
							}
						}
					}
				}
			} else {
				/* New tile */
				if (!IsValidTile(gp.new_tile)) return ReverseShip(v);

				const DiagDirection diagdir = DiagdirBetweenTiles(gp.old_tile, gp.new_tile);
				dbg_assert(diagdir != INVALID_DIAGDIR);
				const TrackBits tracks = GetAvailShipTracks(gp.new_tile, diagdir);
				if (tracks == TRACK_BIT_NONE) {
					Trackdir trackdir = INVALID_TRACKDIR;
					CheckReverseShip(v, &trackdir);
					if (trackdir == INVALID_TRACKDIR) return ReverseShip(v);
					return ReverseShipIntoTrackdir(v, trackdir);
				}

				/* Choose a direction, and continue if we find one */
				Track track = ChooseShipTrack(v, gp.new_tile, diagdir, tracks);
				if (track == INVALID_TRACK) return ReverseShip(v);

				/* Try to avoid collision and keep distance between ships. */
				if (_settings_game.vehicle.ship_collision_avoidance) CheckDistanceBetweenShips(gp.new_tile, v, tracks, &track, diagdir);

				const ShipSubcoordData &b = _ship_subcoord[diagdir][track];

				gp.x = (gp.x & ~0xF) | b.x_subcoord;
				gp.y = (gp.y & ~0xF) | b.y_subcoord;

				/* Call the landscape function and tell it that the vehicle entered the tile */
				const VehicleEnterTileStatus r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
				if (HasBit(r, VETS_CANNOT_ENTER)) return ReverseShip(v);

				if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
					v->tile = gp.new_tile;
					v->state = TrackToTrackBits(track);

					/* Update ship cache when the water class changes. Aqueducts are always canals. */
					if (GetEffectiveWaterClass(gp.old_tile) != GetEffectiveWaterClass(gp.new_tile)) v->UpdateCache();
				}

				const Direction new_direction = b.dir;
				const DirDiff diff = DirDifference(new_direction, v->direction);
				switch (diff) {
					case DIRDIFF_SAME:
					case DIRDIFF_45RIGHT:
					case DIRDIFF_45LEFT:
						/* Continue at speed */
						v->rotation = v->direction = new_direction;
						break;

					default:
						/* Stop for rotation */
						UpdateShipSpeed(v, 0);
						v->direction = new_direction;
						/* Remember our current location to avoid movement glitch */
						v->rotation_x_pos = v->x_pos;
						v->rotation_y_pos = v->y_pos;
						break;
				}
			}
		} else {
			/* On a bridge */
			if (!IsTileType(gp.new_tile, MP_TUNNELBRIDGE) || !HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
				if (_settings_game.vehicle.ship_collision_avoidance && gp.new_tile != TileVirtXY(v->x_pos, v->y_pos)) HandleSpeedOnAqueduct(v, gp.new_tile, v->tile);
				v->x_pos = gp.x;
				v->y_pos = gp.y;
				v->UpdatePosition();
				if ((v->vehstatus & VS_HIDDEN) == 0) v->Vehicle::UpdateViewport(true);
				return;
			}
			/* Bridge exit */
			if (_settings_game.vehicle.ship_collision_avoidance && gp.new_tile != TileVirtXY(v->x_pos, v->y_pos)) HandleSpeedOnAqueduct(v, gp.new_tile, v->tile);

			/* Ship is back on the bridge head, we need to consume its path
			 * cache entry here as we didn't have to choose a ship track. */
			if (!v->path.empty()) v->path.pop_front();
		}

		/* update image of ship, as well as delta XY */
		v->x_pos = gp.x;
		v->y_pos = gp.y;

		v->UpdatePosition();
		v->UpdateViewport(true, true);
	}
}

bool Ship::Tick()
{
	DEBUG_UPDATESTATECHECKSUM("Ship::Tick: v: %u, x: %d, y: %d", this->index, this->x_pos, this->y_pos);
	UpdateStateChecksum((((uint64) this->x_pos) << 32) | this->y_pos);
	if (!((this->vehstatus & VS_STOPPED) || this->IsWaitingInDepot())) this->running_ticks++;

	ShipController(this);

	return true;
}

void Ship::SetDestTile(TileIndex tile)
{
	if (tile == this->dest_tile) return;
	this->path.clear();
	this->dest_tile = tile;
}

/**
 * Build a ship.
 * @param tile     tile of the depot where ship is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param data     unused.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildShip(TileIndex tile, DoCommandFlag flags, const Engine *e, uint16 data, Vehicle **ret)
{
	tile = GetShipDepotNorthTile(tile);
	if (flags & DC_EXEC) {
		int x;
		int y;

		const ShipVehicleInfo *svi = &e->u.ship;

		Ship *v = new Ship();
		*ret = v;

		v->owner = _current_company;
		v->tile = tile;
		x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
		y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y);

		v->UpdateDeltaXY();
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;

		v->spritenum = svi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		v->cargo_cap = svi->capacity;
		v->refit_cap = 0;

		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;
		v->engine_type = e->index;

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->breakdown_chance_factor = 64; // ships have a 50% lower breakdown chance than normal
		v->max_age = e->GetLifeLengthInDays();
		_new_vehicle_id = v->index;

		v->state = TRACK_BIT_DEPOT;

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_ships);
		v->date_of_last_service = _date;
		v->build_year = _cur_year;
		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = VehicleRandomBits();

		v->UpdateCache();

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
		SB(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, 1, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
		SB(v->vehicle_flags, VF_TIMETABLE_SEPARATION, 1, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

		v->InvalidateNewGRFCacheOfChain();

		v->cargo_cap = e->DetermineCapacity(v);

		AddArticulatedParts(v);
		v->InvalidateNewGRFCacheOfChain();

		v->UpdatePosition();
		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

ClosestDepot Ship::FindClosestDepot()
{
	const Depot *depot = FindClosestShipDepot(this, 0);
	if (depot == nullptr) return ClosestDepot();

	return ClosestDepot(depot->xy, depot->index);
}
