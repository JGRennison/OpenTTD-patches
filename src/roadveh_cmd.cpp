/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file roadveh_cmd.cpp Handling of road vehicles. */

#include "stdafx.h"
#include "roadveh.h"
#include "command_func.h"
#include "news_func.h"
#include "pathfinder/npf/npf_func.h"
#include "station_base.h"
#include "company_func.h"
#include "articulated_vehicles.h"
#include "newgrf_sound.h"
#include "pathfinder/yapf/yapf.h"
#include "strings_func.h"
#include "tunnelbridge_map.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "depot_map.h"
#include "effectvehicle_func.h"
#include "roadstop_base.h"
#include "spritecache.h"
#include "core/random_func.hpp"
#include "company_base.h"
#include "core/backup_type.hpp"
#include "infrastructure_func.h"
#include "newgrf.h"
#include "zoom_func.h"
#include "framerate_type.h"
#include "scope_info.h"
#include "string_func.h"
#include "core/checksum_func.hpp"
#include "newgrf_roadstop.h"

#include "table/strings.h"

#include "safeguards.h"

static const uint16_t _roadveh_images[] = {
	0xCD4, 0xCDC, 0xCE4, 0xCEC, 0xCF4, 0xCFC, 0xD0C, 0xD14,
	0xD24, 0xD1C, 0xD2C, 0xD04, 0xD1C, 0xD24, 0xD6C, 0xD74,
	0xD7C, 0xC14, 0xC1C, 0xC24, 0xC2C, 0xC34, 0xC3C, 0xC4C,
	0xC54, 0xC64, 0xC5C, 0xC6C, 0xC44, 0xC5C, 0xC64, 0xCAC,
	0xCB4, 0xCBC, 0xD94, 0xD9C, 0xDA4, 0xDAC, 0xDB4, 0xDBC,
	0xDCC, 0xDD4, 0xDE4, 0xDDC, 0xDEC, 0xDC4, 0xDDC, 0xDE4,
	0xE2C, 0xE34, 0xE3C, 0xC14, 0xC1C, 0xC2C, 0xC3C, 0xC4C,
	0xC5C, 0xC64, 0xC6C, 0xC74, 0xC84, 0xC94, 0xCA4
};

static const uint16_t _roadveh_full_adder[] = {
	 0,  88,   0,   0,   0,   0,  48,  48,
	48,  48,   0,   0,  64,  64,   0,  16,
	16,   0,  88,   0,   0,   0,   0,  48,
	48,  48,  48,   0,   0,  64,  64,   0,
	16,  16,   0,  88,   0,   0,   0,   0,
	48,  48,  48,  48,   0,   0,  64,  64,
	 0,  16,  16,   0,   8,   8,   8,   8,
	 0,   0,   0,   8,   8,   8,   8
};
static_assert(lengthof(_roadveh_images) == lengthof(_roadveh_full_adder));

template <>
bool IsValidImageIndex<VEH_ROAD>(uint8_t image_index)
{
	return image_index < lengthof(_roadveh_images);
}

static const Trackdir _road_reverse_table[DIAGDIR_END] = {
	TRACKDIR_RVREV_NE, TRACKDIR_RVREV_SE, TRACKDIR_RVREV_SW, TRACKDIR_RVREV_NW
};

/**
 * Check whether a roadvehicle is a bus
 * @return true if bus
 */
bool RoadVehicle::IsBus() const
{
	assert(this->IsFrontEngine());
	return IsCargoInClass(this->cargo_type, CC_PASSENGERS);
}

/**
 * Get the width of a road vehicle image in the GUI.
 * @param offset Additional offset for positioning the sprite; set to nullptr if not needed
 * @return Width in pixels
 */
int RoadVehicle::GetDisplayImageWidth(Point *offset) const
{
	int reference_width = ROADVEHINFO_DEFAULT_VEHICLE_WIDTH;

	if (offset != nullptr) {
		offset->x = ScaleSpriteTrad(reference_width) / 2;
		offset->y = 0;
	}
	return ScaleSpriteTrad(this->gcache.cached_veh_length * reference_width / VEHICLE_LENGTH);
}

static void GetRoadVehIcon(EngineID engine, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	uint8_t spritenum = e->u.road.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, DIR_W, image_type, result);
		if (result->IsValid()) return;

		spritenum = e->original_image_index;
	}

	assert(IsValidImageIndex<VEH_ROAD>(spritenum));
	result->Set(DIR_W + _roadveh_images[spritenum]);
}

void RoadVehicle::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8_t spritenum = this->spritenum;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, (Direction)(direction + 4 * IS_CUSTOM_SECONDHEAD_SPRITE(spritenum)), image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	assert(IsValidImageIndex<VEH_ROAD>(spritenum));
	SpriteID sprite = direction + _roadveh_images[spritenum];

	if (this->cargo.StoredCount() >= this->cargo_cap / 2U) sprite += _roadveh_full_adder[spritenum];

	result->Set(sprite);
}

/**
 * Draw a road vehicle engine.
 * @param left Left edge to draw within.
 * @param right Right edge to draw within.
 * @param preferred_x Preferred position of the engine.
 * @param y Vertical position of the engine.
 * @param engine Engine to draw
 * @param pal Palette to use.
 */
void DrawRoadVehEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetRoadVehIcon(engine, image_type, &seq);

	Rect16 rect = seq.GetBounds();
	preferred_x = SoftClamp(preferred_x,
			left - UnScaleGUI(rect.left),
			right - UnScaleGUI(rect.right));

	seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
}

/**
 * Get the size of the sprite of a road vehicle sprite heading west (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetRoadVehSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	VehicleSpriteSeq seq;
	GetRoadVehIcon(engine, image_type, &seq);

	Rect rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

	width  = UnScaleGUI(rect.Width());
	height = UnScaleGUI(rect.Height());
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);
}

/**
 * Get length of a road vehicle.
 * @param v Road vehicle to query length.
 * @return Length of the given road vehicle.
 */
static uint GetRoadVehLength(const RoadVehicle *v)
{
	const Engine *e = v->GetEngine();
	uint length = VEHICLE_LENGTH;

	uint16_t veh_len = CALLBACK_FAILED;
	if (e->GetGRF() != nullptr && e->GetGRF()->grf_version >= 8) {
		/* Use callback 36 */
		veh_len = GetVehicleProperty(v, PROP_ROADVEH_SHORTEN_FACTOR, CALLBACK_FAILED);
		if (veh_len != CALLBACK_FAILED && veh_len >= VEHICLE_LENGTH) ErrorUnknownCallbackResult(e->GetGRFID(), CBID_VEHICLE_LENGTH, veh_len);
	} else {
		/* Use callback 11 */
		veh_len = GetVehicleCallback(CBID_VEHICLE_LENGTH, 0, 0, v->engine_type, v);
	}
	if (veh_len == CALLBACK_FAILED) veh_len = e->u.road.shorten_factor;
	if (veh_len != 0) {
		length -= Clamp(veh_len, 0, VEHICLE_LENGTH - 1);
	}

	return length;
}

/**
 * Update the cache of a road vehicle.
 * @param v Road vehicle needing an update of its cache.
 * @param same_length should length of vehicles stay the same?
 * @pre \a v must be first road vehicle.
 */
void RoadVehUpdateCache(RoadVehicle *v, bool same_length)
{
	assert(v->type == VEH_ROAD);
	assert(v->IsFrontEngine());

	v->InvalidateNewGRFCacheOfChain();

	const uint16_t old_total_length = v->gcache.cached_total_length;
	v->gcache.cached_total_length = 0;

	Vehicle *last_vis_effect = v;
	for (RoadVehicle *u = v; u != nullptr; u = u->Next()) {
		/* Check the v->first cache. */
		assert(u->First() == v);

		/* Update the 'first engine' */
		u->gcache.first_engine = (v == u) ? INVALID_ENGINE : v->engine_type;

		/* Update the length of the vehicle. */
		uint veh_len = GetRoadVehLength(u);
		/* Verify length hasn't changed. */
		if (same_length && veh_len != u->gcache.cached_veh_length) VehicleLengthChanged(u);

		u->gcache.cached_veh_length = veh_len;
		v->gcache.cached_total_length += u->gcache.cached_veh_length;

		/* Update visual effect */
		u->UpdateVisualEffect();
		ClrBit(u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);
		if (!(HasBit(u->vcache.cached_vis_effect, VE_ADVANCED_EFFECT) && GB(u->vcache.cached_vis_effect, 0, VE_ADVANCED_EFFECT) == VESM_NONE)) last_vis_effect = u;

		/* Update cargo aging period. */
		if (unlikely(v->GetGRFID() == BSWAP32(0x44450602))) {
			/* skip callback for known bad GRFs */
			u->vcache.cached_cargo_age_period = EngInfo(u->engine_type)->cargo_age_period;
		} else {
			u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_ROADVEH_CARGO_AGE_PERIOD, EngInfo(u->engine_type)->cargo_age_period);
		}
	}
	SetBit(last_vis_effect->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);

	uint max_speed = GetVehicleProperty(v, PROP_ROADVEH_SPEED, 0);
	v->vcache.cached_max_speed = (max_speed != 0) ? max_speed * 4 : RoadVehInfo(v->engine_type)->max_speed;

	if (same_length && old_total_length != v->gcache.cached_total_length) {
		if (IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) {
			RoadStop *rs = RoadStop::GetByTile(v->tile, GetRoadStopType(v->tile));
			rs->GetEntry(v)->AdjustOccupation((int)v->gcache.cached_total_length - (int)old_total_length);
		}
	}
}

/**
 * Build a road vehicle.
 * @param tile     tile of the depot where road vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildRoadVehicle(TileIndex tile, DoCommandFlag flags, const Engine *e, Vehicle **ret)
{
	/* Check that the vehicle can drive on the road in question */
	RoadType rt = e->u.road.roadtype;
	const RoadTypeInfo *rti = GetRoadTypeInfo(rt);
	if (!HasTileAnyRoadType(tile, rti->powered_roadtypes)) return_cmd_error(STR_ERROR_DEPOT_WRONG_DEPOT_TYPE);

	if (flags & DC_EXEC) {
		const RoadVehicleInfo *rvi = &e->u.road;

		RoadVehicle *v = new RoadVehicle();
		*ret = v;
		v->direction = DiagDirToDir(GetRoadDepotDirection(tile));
		v->owner = _current_company;

		v->tile = tile;
		int x = TileX(tile) * TILE_SIZE + TILE_SIZE / 2;
		int y = TileY(tile) * TILE_SIZE + TILE_SIZE / 2;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y, true);

		v->state = RVSB_IN_DEPOT;
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;

		v->spritenum = rvi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		assert(IsValidCargoID(v->cargo_type));
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;

		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;
		v->engine_type = e->index;
		v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->breakdown_chance_factor = 128;
		v->max_age = e->GetLifeLengthInDays();
		_new_vehicle_id = v->index;

		v->SetServiceInterval(Company::Get(v->owner)->settings.vehicle.servint_roadveh);

		v->date_of_last_service = EconTime::CurDate();
		v->date_of_last_service_newgrf = CalTime::CurDate();
		v->build_year = CalTime::CurYear();

		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = Random();
		v->SetFrontEngine();

		v->roadtype = rt;
		v->compatible_roadtypes = rti->powered_roadtypes;
		v->gcache.cached_veh_length = VEHICLE_LENGTH;

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
		SB(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, 1, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
		SB(v->vehicle_flags, VF_TIMETABLE_SEPARATION, 1, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

		AddArticulatedParts(v);
		v->InvalidateNewGRFCacheOfChain();

		/* Call various callbacks after the whole consist has been constructed */
		for (RoadVehicle *u = v; u != nullptr; u = u->Next()) {
			u->cargo_cap = u->GetEngine()->DetermineCapacity(u);
			u->refit_cap = 0;
			v->InvalidateNewGRFCache();
			u->InvalidateNewGRFCache();
		}
		RoadVehUpdateCache(v);
		/* Initialize cached values for realistic acceleration. */
		if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) v->CargoChanged();

		v->UpdatePosition();

		CheckConsistencyOfArticulatedVehicle(v);

		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

static FindDepotData FindClosestRoadDepot(const RoadVehicle *v, int max_distance)
{
	if (IsRoadDepotTile(v->tile)) return FindDepotData(v->tile, 0);

	switch (_settings_game.pf.pathfinder_for_roadvehs) {
		case VPF_NPF: return NPFRoadVehicleFindNearestDepot(v, max_distance);
		case VPF_YAPF: return YapfRoadVehicleFindNearestDepot(v, max_distance);

		default: NOT_REACHED();
	}
}

ClosestDepot RoadVehicle::FindClosestDepot()
{
	FindDepotData rfdd = FindClosestRoadDepot(this, 0);
	if (rfdd.best_length == UINT_MAX) return ClosestDepot();

	return ClosestDepot(rfdd.tile, GetDepotIndex(rfdd.tile));
}

inline bool IsOneWayRoadTile(TileIndex tile)
{
	return MayHaveRoad(tile) && GetRoadCachedOneWayState(tile) != RCOWS_NORMAL;
}

inline bool IsOneWaySideJunctionRoadTile(TileIndex tile)
{
	return MayHaveRoad(tile) && (GetRoadCachedOneWayState(tile) == RCOWS_SIDE_JUNCTION || GetRoadCachedOneWayState(tile) == RCOWS_SIDE_JUNCTION_NO_EXIT);
}

static bool MayReverseOnOneWayRoadTile(TileIndex tile, DiagDirection dir)
{
	TrackdirBits bits = GetTileTrackdirBits(tile, TRANSPORT_ROAD, RTT_ROAD);
	return bits & DiagdirReachesTrackdirs(ReverseDiagDir(dir));
}

/**
 * Turn a roadvehicle around.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 vehicle ID to turn
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdTurnRoadVeh(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	RoadVehicle *v = RoadVehicle::GetIfValid(p1);
	if (v == nullptr) return CMD_ERROR;

	if (!v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(v);
	if (ret.Failed()) return ret;

	if ((v->vehstatus & VS_STOPPED) ||
			(v->vehstatus & VS_CRASHED) ||
			v->overtaking != 0 ||
			v->state == RVSB_WORMHOLE ||
			v->IsInDepot() ||
			v->current_order.IsType(OT_LOADING)) {
		return CMD_ERROR;
	}

	if (IsOneWayRoadTile(v->tile)) return CMD_ERROR;

	if (IsTileType(v->tile, MP_TUNNELBRIDGE) && DirToDiagDir(v->direction) == GetTunnelBridgeDirection(v->tile)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->reverse_ctr = 180;

		/* Unbunching data is no longer valid. */
		v->ResetDepotUnbunching();
	}

	return CommandCost();
}


void RoadVehicle::MarkDirty()
{
	for (RoadVehicle *v = this; v != nullptr; v = v->Next()) {
		v->colourmap = PAL_NONE;
		v->InvalidateImageCache();
		v->UpdateViewport(true, false);
	}
	this->CargoChanged();
}

void RoadVehicle::UpdateDeltaXY()
{
	static const int8_t _delta_xy_table[8][10] = {
		/* y_extent, x_extent, y_offs, x_offs, y_bb_offs, x_bb_offs, y_extent_shorten, x_extent_shorten, y_bb_offs_shorten, x_bb_offs_shorten */
		{3, 3, -1, -1,  0,  0, -1, -1, -1, -1}, // N
		{3, 7, -1, -3,  0, -1,  0, -1,  0,  0}, // NE
		{3, 3, -1, -1,  0,  0,  1, -1,  1, -1}, // E
		{7, 3, -3, -1, -1,  0,  0,  0,  1,  0}, // SE
		{3, 3, -1, -1,  0,  0,  1,  1,  1,  1}, // S
		{3, 7, -1, -3,  0, -1,  0,  0,  0,  1}, // SW
		{3, 3, -1, -1,  0,  0, -1,  1, -1,  1}, // W
		{7, 3, -3, -1, -1,  0, -1,  0,  0,  0}, // NW
	};

	int shorten = VEHICLE_LENGTH - this->gcache.cached_veh_length;
	if (!IsDiagonalDirection(this->direction)) shorten >>= 1;

	const int8_t *bb = _delta_xy_table[this->direction];
	this->x_bb_offs     = bb[5] + bb[9] * shorten;
	this->y_bb_offs     = bb[4] + bb[8] * shorten;;
	this->x_offs        = bb[3];
	this->y_offs        = bb[2];
	this->x_extent      = bb[1] + bb[7] * shorten;
	this->y_extent      = bb[0] + bb[6] * shorten;
	this->z_extent      = 6;
}

/**
 * Calculates the maximum speed of the vehicle, taking into account speed reductions following critical breakdowns
 * @return Maximum speed of the vehicle.
 */
int RoadVehicle::GetEffectiveMaxSpeed() const
{
	int max_speed = this->vcache.cached_max_speed;

	if (this->critical_breakdown_count == 0) return max_speed;

	for (uint i = 0; i < this->critical_breakdown_count; i++) {
		max_speed = std::min(max_speed - (max_speed / 3) + 1, max_speed);
	}

	/* clamp speed to be no less than lower of 5mph and 1/8 of base speed */
	return std::max<uint16_t>(max_speed, std::min<uint16_t>(10, (this->vcache.cached_max_speed + 7) >> 3));
}

/**
 * Calculates the maximum speed of the vehicle under its current conditions.
 * @return Maximum speed of the vehicle.
 */
inline int RoadVehicle::GetCurrentMaxSpeed() const
{
	int max_speed = std::min<int>(this->GetEffectiveMaxSpeed(), this->gcache.cached_max_track_speed);

	/* Limit speed to 50% while reversing, 75% in curves. */
	for (const RoadVehicle *u = this; u != nullptr; u = u->Next()) {
		if (_settings_game.vehicle.roadveh_acceleration_model == AM_REALISTIC) {
			if (this->state <= RVSB_TRACKDIR_MASK && IsReversingRoadTrackdir((Trackdir)this->state)) {
				max_speed = std::min(max_speed, this->gcache.cached_max_track_speed / 2);
			} else if ((u->direction & 1) == 0) {
				// Are we in a curve and should slow down?
				if (_settings_game.vehicle.slow_road_vehicles_in_curves) {
					max_speed = std::min(max_speed, this->gcache.cached_max_track_speed * 3 / 4);
				}
			}
		}

		/* Vehicle is on the middle part of a bridge. */
		if (u->state == RVSB_WORMHOLE && !(u->vehstatus & VS_HIDDEN)) {
			max_speed = std::min(max_speed, GetBridgeSpec(GetBridgeType(u->tile))->speed * 2);
		}
	}

	return std::min(max_speed, this->current_order.GetMaxSpeed() * 2);
}

/**
 * Delete last vehicle of a chain road vehicles.
 * @param v First roadvehicle.
 */
static void DeleteLastRoadVeh(RoadVehicle *v)
{
	RoadVehicle *first = v->First();
	Vehicle *u = v;
	for (; v->Next() != nullptr; v = v->Next()) u = v;
	u->SetNext(nullptr);
	v->last_station_visited = first->last_station_visited; // for PreDestructor

	delete v;
}

static void RoadVehSetRandomDirection(RoadVehicle *v)
{
	static const DirDiff delta[] = {
		DIRDIFF_45LEFT, DIRDIFF_SAME, DIRDIFF_SAME, DIRDIFF_45RIGHT
	};

	do {
		uint32_t r = Random();

		v->direction = ChangeDir(v->direction, delta[r & 3]);
		v->UpdateViewport(true, true);
	} while ((v = v->Next()) != nullptr);
}

/**
 * Road vehicle chain has crashed.
 * @param v First roadvehicle.
 * @return whether the chain still exists.
 */
static bool RoadVehIsCrashed(RoadVehicle *v)
{
	v->crashed_ctr++;
	if (v->crashed_ctr == 2) {
		CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	} else if (v->crashed_ctr <= 45) {
		if ((v->tick_counter & 7) == 0) RoadVehSetRandomDirection(v);
	} else if (v->crashed_ctr >= 2220 && !(v->tick_counter & 0x1F)) {
		bool ret = v->Next() != nullptr;
		DeleteLastRoadVeh(v);
		return ret;
	}

	return true;
}

struct CheckRoadVehCrashTrainInfo {
	const Vehicle *u;
	bool found = false;

	CheckRoadVehCrashTrainInfo(const Vehicle *u_)
			: u(u_) { }
};

/**
 * Check routine whether a road and a train vehicle have collided.
 * @param v    %Train vehicle to test.
 * @param data Info including road vehicle to test.
 * @return %Train vehicle if the vehicles collided, else \c nullptr.
 */
static Vehicle *EnumCheckRoadVehCrashTrain(Vehicle *v, void *data)
{
	CheckRoadVehCrashTrainInfo *info = (CheckRoadVehCrashTrainInfo*) data;

	if (abs(v->z_pos - info->u->z_pos) <= 6 &&
			abs(v->x_pos - info->u->x_pos) <= 4 &&
			abs(v->y_pos - info->u->y_pos) <= 4) {
		info->found = true;
		extern void TrainRoadVehicleCrashBreakdown(Vehicle *v);
		TrainRoadVehicleCrashBreakdown(v);
		return v;
	} else {
		return nullptr;
	}
}

uint RoadVehicle::Crash(bool flooded)
{
	uint pass = this->GroundVehicleBase::Crash(flooded);
	if (this->IsFrontEngine()) {
		pass += 1; // driver

		/* If we're in a drive through road stop we ought to leave it */
		if (IsInsideMM(this->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) {
			RoadStop::GetByTile(this->tile, GetRoadStopType(this->tile))->Leave(this);
			this->state &= RVSB_ROAD_STOP_TRACKDIR_MASK;
		}
	}
	this->crashed_ctr = flooded ? 2000 : 1; // max 2220, disappear pretty fast when flooded
	return pass;
}

static void RoadVehCrash(RoadVehicle *v)
{
	uint pass = v->Crash();

	AI::NewEvent(v->owner, new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_RV_LEVEL_CROSSING));
	Game::NewEvent(new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_RV_LEVEL_CROSSING));

	SetDParam(0, pass);
	StringID newsitem = (pass == 1) ? STR_NEWS_ROAD_VEHICLE_CRASH_DRIVER : STR_NEWS_ROAD_VEHICLE_CRASH;
	NewsType newstype = NT_ACCIDENT;

	if (v->owner != _local_company) {
		newstype = NT_ACCIDENT_OTHER;
	}

	AddTileNewsItem(newsitem, newstype, v->tile);

	ModifyStationRatingAround(v->tile, v->owner, -160, 22);
	if (_settings_client.sound.disaster) SndPlayVehicleFx(SND_12_EXPLOSION, v);
}

static bool RoadVehCheckTrainCrash(RoadVehicle *v)
{
	if (!HasBit(v->rvflags, RVF_ON_LEVEL_CROSSING)) return false;
	if (HasBit(_roadtypes_non_train_colliding, v->roadtype)) return false;

	bool still_on_level_crossing = false;

	for (RoadVehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->state == RVSB_WORMHOLE) continue;

		TileIndex tile = u->tile;

		if (!IsLevelCrossingTile(tile)) continue;

		still_on_level_crossing = true;

		CheckRoadVehCrashTrainInfo info(u);
		FindVehicleOnPosXY(v->x_pos, v->y_pos, VEH_TRAIN, &info, EnumCheckRoadVehCrashTrain);
		if (info.found) {
			RoadVehCrash(v);
			return true;
		}
	}

	if (!still_on_level_crossing) {
		ClrBit(v->rvflags, RVF_ON_LEVEL_CROSSING);
	}

	return false;
}

TileIndex RoadVehicle::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	const Station *st = Station::Get(station);
	if (!CanVehicleUseStation(this, st)) {
		/* There is no stop left at the station, so don't even TRY to go there */
		this->IncrementRealOrderIndex();
		return 0;
	}

	return st->xy;
}

static void StartRoadVehSound(const RoadVehicle *v)
{
	if (!PlayVehicleSound(v, VSE_START)) {
		SoundID s = RoadVehInfo(v->engine_type)->sfx;
		if (s == SND_19_DEPARTURE_OLD_RV_1 && (v->tick_counter & 3) == 0) {
			s = SND_1A_DEPARTURE_OLD_RV_2;
		}
		SndPlayVehicleFx(s, v);
	}
}

struct RoadVehFindData {
	int x;
	int y;
	const RoadVehicle *veh;
	RoadVehicle *best;
	uint best_diff;
	Direction dir;
	RoadTypeCollisionMode collision_mode;
};

static Vehicle *EnumCheckRoadVehClose(Vehicle *veh, void *data)
{
	static const int8_t dist_x[] = { -4, -8, -4, -1, 4, 8, 4, 1 };
	static const int8_t dist_y[] = { -4, -1, 4, 8, 4, 1, -4, -8 };

	RoadVehFindData *rvf = (RoadVehFindData*)data;
	RoadVehicle *v = RoadVehicle::From(veh);

	short x_diff = v->x_pos - rvf->x;
	short y_diff = v->y_pos - rvf->y;

	if (!v->IsInDepot() &&
			abs(v->z_pos - rvf->veh->z_pos) < 6 &&
			v->direction == rvf->dir &&
			rvf->veh->First() != v->First() &&
			HasBit(_collision_mode_roadtypes[rvf->collision_mode], v->roadtype) &&
			(dist_x[v->direction] >= 0 || (x_diff > dist_x[v->direction] && x_diff <= 0)) &&
			(dist_x[v->direction] <= 0 || (x_diff < dist_x[v->direction] && x_diff >= 0)) &&
			(dist_y[v->direction] >= 0 || (y_diff > dist_y[v->direction] && y_diff <= 0)) &&
			(dist_y[v->direction] <= 0 || (y_diff < dist_y[v->direction] && y_diff >= 0))) {
		uint diff = abs(x_diff) + abs(y_diff);

		if (diff < rvf->best_diff || (diff == rvf->best_diff && v->index < rvf->best->index)) {
			rvf->best = v;
			rvf->best_diff = diff;
		}
	}

	return nullptr;
}

static RoadVehicle *RoadVehFindCloseTo(RoadVehicle *v, int x, int y, Direction dir, bool update_blocked_ctr = true)
{
	RoadTypeCollisionMode collision_mode = GetRoadTypeInfo(v->roadtype)->collision_mode;
	if (collision_mode == RTCM_NONE) return nullptr;

	RoadVehicle *front = v->First();
	if (front->reverse_ctr != 0) return nullptr;

	RoadVehFindData rvf;
	rvf.x = x;
	rvf.y = y;
	rvf.dir = dir;
	rvf.veh = v;
	rvf.best_diff = UINT_MAX;
	rvf.collision_mode = collision_mode;

	if (front->state == RVSB_WORMHOLE) {
		FindVehicleOnPos(v->tile, VEH_ROAD, &rvf, EnumCheckRoadVehClose);
		FindVehicleOnPos(GetOtherTunnelBridgeEnd(v->tile), VEH_ROAD, &rvf, EnumCheckRoadVehClose);
	} else {
		FindVehicleOnPosXY(x, y, VEH_ROAD, &rvf, EnumCheckRoadVehClose);
	}

	/* This code protects a roadvehicle from being blocked for ever
	 * If more than 1480 / 74 days a road vehicle is blocked, it will
	 * drive just through it. The ultimate backup-code of TTD.
	 * It can be disabled. */
	if (rvf.best_diff == UINT_MAX) {
		front->blocked_ctr = 0;
		return nullptr;
	}

	if (update_blocked_ctr && ++front->blocked_ctr > 1480 && (!_settings_game.vehicle.roadveh_cant_quantum_tunnel)) return nullptr;

	RoadVehicle *rv = rvf.best;
	if (rv != nullptr && front->IsRoadVehicleOnLevelCrossing() && (rv->First()->cur_speed == 0 || rv->First()->IsRoadVehicleStopped())) return nullptr;

	return rv;
}

/**
 * A road vehicle arrives at a station. If it is the first time, create a news item.
 * @param v  Road vehicle that arrived.
 * @param st Station where the road vehicle arrived.
 */
static void RoadVehArrivesAt(const RoadVehicle *v, Station *st)
{
	if (v->IsBus()) {
		/* Check if station was ever visited before */
		if (!(st->had_vehicle_of_type & HVOT_BUS)) {
			st->had_vehicle_of_type |= HVOT_BUS;
			SetDParam(0, st->index);
			AddVehicleNewsItem(
				RoadTypeIsRoad(v->roadtype) ? STR_NEWS_FIRST_BUS_ARRIVAL : STR_NEWS_FIRST_PASSENGER_TRAM_ARRIVAL,
				(v->owner == _local_company) ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
				v->index,
				st->index
			);
			AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
			Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
		}
	} else {
		/* Check if station was ever visited before */
		if (!(st->had_vehicle_of_type & HVOT_TRUCK)) {
			st->had_vehicle_of_type |= HVOT_TRUCK;
			SetDParam(0, st->index);
			AddVehicleNewsItem(
				RoadTypeIsRoad(v->roadtype) ? STR_NEWS_FIRST_TRUCK_ARRIVAL : STR_NEWS_FIRST_CARGO_TRAM_ARRIVAL,
				(v->owner == _local_company) ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
				v->index,
				st->index
			);
			AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
			Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
		}
	}
}

/**
 * This function looks at the vehicle and updates its speed (cur_speed
 * and subspeed) variables. Furthermore, it returns the distance that
 * the vehicle can drive this tick. #Vehicle::GetAdvanceDistance() determines
 * the distance to drive before moving a step on the map.
 * @param max_speed maximum speed as from GetCurrentMaxSpeed()
 * @return distance to drive.
 */
int RoadVehicle::UpdateSpeed(int max_speed)
{
	switch (_settings_game.vehicle.roadveh_acceleration_model) {
		default: NOT_REACHED();
		case AM_ORIGINAL: {
			int acceleration = this->overtaking != 0 ? 512 : 256;
			return this->DoUpdateSpeed({ acceleration, acceleration }, 0, max_speed, max_speed, false);
		}

		case AM_REALISTIC: {
			GroundVehicleAcceleration acceleration = this->GetAcceleration();
			if (this->overtaking != 0) acceleration.acceleration += 256;
			return this->DoUpdateSpeed(acceleration, this->GetAccelerationStatus() == AS_BRAKE ? 0 : 4, max_speed, max_speed, false);
		}
	}
}

static Direction RoadVehGetNewDirection(const RoadVehicle *v, int x, int y)
{
	static const Direction _roadveh_new_dir[] = {
		DIR_N , DIR_NW, DIR_W , INVALID_DIR,
		DIR_NE, DIR_N , DIR_SW, INVALID_DIR,
		DIR_E , DIR_SE, DIR_S
	};

	x = x - v->x_pos + 1;
	y = y - v->y_pos + 1;

	if ((uint)x > 2 || (uint)y > 2) return v->direction;
	return _roadveh_new_dir[y * 4 + x];
}

static Direction RoadVehGetSlidingDirection(const RoadVehicle *v, int x, int y)
{
	Direction new_dir = RoadVehGetNewDirection(v, x, y);
	Direction old_dir = v->direction;
	DirDiff delta;

	if (new_dir == old_dir) return old_dir;
	delta = (DirDifference(new_dir, old_dir) > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
	return ChangeDir(old_dir, delta);
}

struct OvertakeData {
	const RoadVehicle *u;
	const RoadVehicle *v;
	TileIndex tile;
	Trackdir trackdir;
	int tunnelbridge_min;
	int tunnelbridge_max;
	RoadTypeCollisionMode collision_mode;
};

static Vehicle *EnumFindVehBlockingOvertake(Vehicle *v, void *data)
{
	const OvertakeData *od = (OvertakeData*)data;

	if (v->First() == od->u || v->First() == od->v) return nullptr;
	if (!HasBit(_collision_mode_roadtypes[od->collision_mode], RoadVehicle::From(v)->roadtype)) return nullptr;
	if (RoadVehicle::From(v)->overtaking != 0 || v->direction != od->v->direction) return v;

	/* Check if other vehicle is behind */
	switch (DirToDiagDir(v->direction)) {
		case DIAGDIR_NE:
			if (v->x_pos > od->v->x_pos) return nullptr;
			break;
		case DIAGDIR_SE:
			if (v->y_pos < od->v->y_pos) return nullptr;
			break;
		case DIAGDIR_SW:
			if (v->x_pos < od->v->x_pos) return nullptr;
			break;
		case DIAGDIR_NW:
			if (v->y_pos > od->v->y_pos) return nullptr;
			break;
		default:
			NOT_REACHED();
	}
	return v;
}

static Vehicle *EnumFindVehBlockingOvertakeTunnelBridge(Vehicle *v, void *data)
{
	const OvertakeData *od = (OvertakeData*)data;

	switch (DiagDirToAxis(DirToDiagDir(v->direction))) {
		case AXIS_X:
			if (v->x_pos < od->tunnelbridge_min || v->x_pos > od->tunnelbridge_max) return nullptr;
			break;
		case AXIS_Y:
			if (v->y_pos < od->tunnelbridge_min || v->y_pos > od->tunnelbridge_max) return nullptr;
			break;
		default:
			NOT_REACHED();
	}
	return EnumFindVehBlockingOvertake(v, data);
}

static Vehicle *EnumFindVehBlockingOvertakeBehind(Vehicle *v, void *data)
{
	const OvertakeData *od = (OvertakeData*)data;

	if (v->First() == od->u || v->First() == od->v) return nullptr;
	if (!HasBit(_collision_mode_roadtypes[od->collision_mode], RoadVehicle::From(v)->roadtype)) return nullptr;
	if (RoadVehicle::From(v)->overtaking != 0 && TileVirtXY(v->x_pos, v->y_pos) == od->tile) return v;
	return nullptr;
}

static bool CheckRoadInfraUnsuitableForOvertaking(OvertakeData *od)
{
	if (!HasTileAnyRoadType(od->tile, od->v->compatible_roadtypes)) return true;
	TrackStatus ts = GetTileTrackStatus(od->tile, TRANSPORT_ROAD, ((od->v->roadtype + 1) << 8) | GetRoadTramType(od->v->roadtype));
	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts);
	TrackdirBits red_signals = TrackStatusToRedSignals(ts); // barred level crossing
	TrackBits trackbits = TrackdirBitsToTrackBits(trackdirbits);

	/* Track does not continue along overtaking direction || levelcrossing is barred */
	if (!HasBit(trackdirbits, od->trackdir) || (red_signals != TRACKDIR_BIT_NONE)) return true;
	/* Track has junction */
	if (trackbits & ~TRACK_BIT_CROSS) {
		RoadCachedOneWayState rcows = GetRoadCachedOneWayState(od->tile);
		if (rcows == RCOWS_SIDE_JUNCTION) {
			const RoadVehPathCache *pc = od->v->cached_path.get();
			if (pc != nullptr && !pc->empty() && pc->front_tile() == od->tile && !IsStraightRoadTrackdir(pc->front_td())) {
				/* cached path indicates that we are turning here, do not overtake */
				return true;
			}
		} else {
			return rcows == RCOWS_NORMAL || rcows == RCOWS_NO_ACCESS;
		}
	}

	return false;
}

/**
 * Check if overtaking is possible on a piece of track
 *
 * @param od Information about the tile and the involved vehicles
 * @return true if we have to abort overtaking
 */
static bool CheckRoadBlockedForOvertaking(OvertakeData *od)
{
	/* Are there more vehicles on the tile except the two vehicles involved in overtaking */
	return HasVehicleOnPos(od->tile, VEH_ROAD, od, EnumFindVehBlockingOvertake);
}

/**
 * Check if overtaking is possible on a piece of track
 *
 * @param od Information about the tile and the involved vehicles
 * @return true if we have to abort overtaking
 */
static bool IsNonOvertakingStationTile(TileIndex tile, DiagDirection diag_dir)
{
	if (!IsTileType(tile, MP_STATION)) return false;
	if (!IsDriveThroughStopTile(tile)) return true;
	const DisallowedRoadDirections diagdir_to_drd[DIAGDIR_END] = { DRD_NORTHBOUND, DRD_NORTHBOUND, DRD_SOUTHBOUND, DRD_SOUTHBOUND };
	return GetDriveThroughStopDisallowedRoadDirections(tile) != diagdir_to_drd[diag_dir];
}

inline bool IsValidRoadVehStateForOvertake(const RoadVehicle *v)
{
	if (v->state == RVSB_IN_DEPOT) return false;
	if (v->state < TRACKDIR_END && !(IsValidTrackdir((Trackdir)v->state) && IsDiagonalTrackdir((Trackdir)v->state))) return false;
	return true;
}

static bool CheckTunnelBridgeBlockedForOvertaking(OvertakeData *od, TileIndex behind_end, TileIndex ahead_end, TileIndex pos, int ahead_extent, int behind_extent)
{
	switch (DirToDiagDir(od->v->direction)) {
		case DIAGDIR_NE:
			od->tunnelbridge_min = (TileX(pos) - ahead_extent) * TILE_SIZE;
			od->tunnelbridge_max = ((TileX(pos) + behind_extent) * TILE_SIZE) + TILE_UNIT_MASK;
			break;
		case DIAGDIR_SE:
			od->tunnelbridge_min = (TileY(pos) - behind_extent) * TILE_SIZE;
			od->tunnelbridge_max = ((TileY(pos) + ahead_extent) * TILE_SIZE) + TILE_UNIT_MASK;
			break;
		case DIAGDIR_SW:
			od->tunnelbridge_min = (TileX(pos) - behind_extent) * TILE_SIZE;
			od->tunnelbridge_max = ((TileX(pos) + ahead_extent) * TILE_SIZE) + TILE_UNIT_MASK;
			break;
		case DIAGDIR_NW:
			od->tunnelbridge_min = (TileY(pos) - ahead_extent) * TILE_SIZE;
			od->tunnelbridge_max = ((TileY(pos) + behind_extent) * TILE_SIZE) + TILE_UNIT_MASK;
			break;
		default:
			NOT_REACHED();
	}

	if (HasVehicleOnPos(behind_end, VEH_ROAD, od, EnumFindVehBlockingOvertakeTunnelBridge)) return true;
	if (HasVehicleOnPos(ahead_end, VEH_ROAD, od, EnumFindVehBlockingOvertakeTunnelBridge)) return true;
	return false;
}

static void RoadVehCheckOvertake(RoadVehicle *v, RoadVehicle *u)
{
	/* Trams can't overtake other trams */
	if (RoadTypeIsTram(v->roadtype)) return;

	/* Other vehicle is facing the opposite direction || direction is not a diagonal direction */
	if (v->direction == ReverseDir(u->Last()->direction) || !(v->direction & 1)) return;

	if (!IsValidRoadVehStateForOvertake(v)) return;

	/* Don't overtake in stations */
	if (IsNonOvertakingStationTile(u->tile, DirToDiagDir(u->direction))) return;

	/* If not permitted, articulated road vehicles can't overtake anything. */
	if (!_settings_game.vehicle.roadveh_articulated_overtaking && v->HasArticulatedPart()) return;

	/* Don't overtake if the vehicle is broken or about to break down */
	if (v->breakdown_ctr != 0) return;

	/* Vehicles chain is too long to overtake */
	if (v->GetOvertakingCounterThreshold() > 255) return;

	for (RoadVehicle *w = v; w != nullptr; w = w->Next()) {
		if (!IsValidRoadVehStateForOvertake(w)) return;

		/* Don't overtake in stations */
		if (IsNonOvertakingStationTile(w->tile, DirToDiagDir(w->direction))) return;

		/* Don't overtake if vehicle parts not all in same direction */
		if (w->direction != v->direction) return;

		/* Check if vehicle is in a road stop, depot, or not on a straight road */
		if ((w->state >= RVSB_IN_ROAD_STOP || !IsStraightRoadTrackdir((Trackdir)(w->state & RVSB_TRACKDIR_MASK))) &&
				!IsInsideMM(w->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) && w->state != RVSB_WORMHOLE) {
			return;
		}
	}

	/* Can't overtake a vehicle that is moving faster than us. If the vehicle in front is
	 * accelerating, take the maximum speed for the comparison, else the current speed.
	 * Original acceleration always accelerates, so always use the maximum speed. */
	int u_speed = (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL || u->GetAcceleration().acceleration > 0) ? u->GetCurrentMaxSpeed() : u->cur_speed;
	if (u_speed >= v->GetCurrentMaxSpeed() &&
			!(u->vehstatus & VS_STOPPED) &&
			u->cur_speed != 0) {
		return;
	}

	OvertakeData od;
	od.v = v;
	od.u = u;
	od.trackdir = DiagDirToDiagTrackdir(DirToDiagDir(v->direction));
	od.collision_mode = GetRoadTypeInfo(v->roadtype)->collision_mode;

	/* Are the current and the next tile suitable for overtaking?
	 *  - Does the track continue along od.trackdir
	 *  - No junctions
	 *  - No barred levelcrossing
	 *  - No other vehicles in the way
	 */
	int tile_count = 1 + CeilDiv(v->gcache.cached_total_length, TILE_SIZE);
	TileIndex check_tile = v->tile;
	DiagDirection dir = DirToDiagDir(v->direction);
	TileIndexDiff check_tile_diff = TileOffsByDiagDir(DirToDiagDir(v->direction));
	TileIndex behind_check_tile = v->tile - check_tile_diff;

	int tile_offset = ((DiagDirToAxis(DirToDiagDir(v->direction)) == AXIS_X) ? v->x_pos : v->y_pos) & 0xF;
	int tile_ahead_margin = ((dir == DIAGDIR_SE || dir == DIAGDIR_SW) ? TILE_SIZE - 1 - tile_offset : tile_offset);;
	int behind_tile_count = (v->gcache.cached_total_length + tile_ahead_margin) / TILE_SIZE;

	if (IsTileType(check_tile, MP_TUNNELBRIDGE)) {
		TileIndex behind_end = GetOtherTunnelBridgeEnd(check_tile);
		if (IsBridgeTile(check_tile) && (IsRoadCustomBridgeHeadTile(check_tile) || IsRoadCustomBridgeHeadTile(behind_end))) return;
		if (GetTunnelBridgeDirection(check_tile) == dir) std::swap(check_tile, behind_end);
		TileIndex veh_tile = TileVirtXY(v->x_pos, v->y_pos);
		bool one_way = GetRoadCachedOneWayState(check_tile) != RCOWS_NORMAL;
		if (CheckTunnelBridgeBlockedForOvertaking(&od, behind_end, check_tile, veh_tile, one_way ? 0 : (tile_count  - 1), behind_tile_count)) return;

		tile_count -= DistanceManhattan(check_tile, veh_tile);
		behind_tile_count -= DistanceManhattan(behind_end, veh_tile);
		check_tile += check_tile_diff;
		behind_check_tile = behind_end - check_tile_diff;
	}
	for (; tile_count > 0; tile_count--, check_tile += check_tile_diff) {
		od.tile = check_tile;
		if (CheckRoadInfraUnsuitableForOvertaking(&od)) return;
		if (IsTileType(check_tile, MP_TUNNELBRIDGE)) {
			TileIndex ahead_end = GetOtherTunnelBridgeEnd(check_tile);
			if (IsBridgeTile(check_tile) && (IsRoadCustomBridgeHeadTile(check_tile) || IsRoadCustomBridgeHeadTile(ahead_end))) return;
			if (GetRoadCachedOneWayState(check_tile) == RCOWS_NORMAL && CheckTunnelBridgeBlockedForOvertaking(&od, check_tile, ahead_end, check_tile, tile_count - 1, 0)) return;
			tile_count -= DistanceManhattan(check_tile, ahead_end);
			check_tile = ahead_end;
			continue;
		}
		if (IsStationRoadStopTile(check_tile) && IsDriveThroughStopTile(check_tile) && GetDriveThroughStopDisallowedRoadDirections(check_tile) != DRD_NONE) {
			const RoadStop *rs = RoadStop::GetByTile(check_tile, GetRoadStopType(check_tile));
			DiagDirection dir = DirToDiagDir(v->direction);
			const RoadStop::Entry *entry = rs->GetEntry(dir);
			const RoadStop::Entry *opposite_entry = rs->GetEntry(ReverseDiagDir(dir));
			if (entry->GetOccupied() < opposite_entry->GetOccupied()) return;
			break;
		}
		if (check_tile != v->tile && GetRoadCachedOneWayState(check_tile) != RCOWS_NORMAL) {
			/* one-way road, don't worry about other vehicles */
			continue;
		}
		if (CheckRoadBlockedForOvertaking(&od)) return;
	}

	for (; behind_tile_count > 0; behind_tile_count--, behind_check_tile -= check_tile_diff) {
		od.tile = behind_check_tile;
		if (behind_tile_count == 1) {
			RoadBits rb = GetAnyRoadBits(behind_check_tile, RTT_ROAD);
			if ((rb & DiagDirToRoadBits(dir)) && HasVehicleOnPos(behind_check_tile, VEH_ROAD, &od, EnumFindVehBlockingOvertakeBehind)) return;
		} else {
			if (CheckRoadInfraUnsuitableForOvertaking(&od)) return;
			if (IsTileType(behind_check_tile, MP_TUNNELBRIDGE)) {
				TileIndex behind_end = GetOtherTunnelBridgeEnd(behind_check_tile);
				if (IsBridgeTile(behind_check_tile) && (IsRoadCustomBridgeHeadTile(behind_check_tile) || IsRoadCustomBridgeHeadTile(behind_end))) return;
				if (CheckTunnelBridgeBlockedForOvertaking(&od, behind_check_tile, behind_end, behind_check_tile, 0, behind_tile_count - 1)) return;
				behind_tile_count -= DistanceManhattan(behind_check_tile, behind_end);
				check_tile = behind_end;
				continue;
			}
			if (CheckRoadBlockedForOvertaking(&od)) return;
		}
	}

	/* When the vehicle in front of us is stopped we may only take
	 * half the time to pass it than when the vehicle is moving. */
	v->overtaking_ctr = (od.u->cur_speed == 0 || od.u->IsRoadVehicleStopped()) ? RV_OVERTAKE_TIMEOUT / 2 : 0;
	v->SetRoadVehicleOvertaking(RVSB_DRIVE_SIDE);
}

static void RoadZPosAffectSpeed(RoadVehicle *v, int old_z)
{
	if (old_z == v->z_pos || _settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) return;

	if (old_z < v->z_pos) {
		v->cur_speed = v->cur_speed * 232 / 256; // slow down by ~10%
	} else {
		uint16_t spd = v->cur_speed + 2;
		if (spd <= v->gcache.cached_max_track_speed) v->cur_speed = spd;
	}
}

static int PickRandomBit(uint bits)
{
	uint i;
	uint num = RandomRange(CountBits(bits));

	for (i = 0; !(bits & 1) || (int)--num >= 0; bits >>= 1, i++) {}
	return i;
}

/**
 * Returns direction to for a road vehicle to take or
 * INVALID_TRACKDIR if the direction is currently blocked
 * @param v        the Vehicle to do the pathfinding for
 * @param tile     the where to start the pathfinding
 * @param enterdir the direction the vehicle enters the tile from
 * @return the Trackdir to take
 */
static Trackdir RoadFindPathToDest(RoadVehicle *v, TileIndex tile, DiagDirection enterdir)
{
#define return_track(x) { best_track = (Trackdir)x; goto found_best_track; }

	TileIndex desttile;
	Trackdir best_track;
	bool path_found = true;

	TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_ROAD, ((v->roadtype + 1) << 8) | GetRoadTramType(v->roadtype));
	TrackdirBits red_signals = TrackStatusToRedSignals(ts); // crossing
	TrackdirBits trackdirs = TrackStatusToTrackdirBits(ts);

	if (IsTileType(tile, MP_ROAD)) {
		if (IsRoadDepot(tile) && (!IsInfraTileUsageAllowed(VEH_ROAD, v->owner, tile) || GetRoadDepotDirection(tile) == enterdir)) {
			/* Road depot owned by another company or with the wrong orientation */
			trackdirs = TRACKDIR_BIT_NONE;
		}
	} else if (IsTileType(tile, MP_STATION) && IsBayRoadStopTile(tile)) {
		/* Standard road stop (drive-through stops are treated as normal road) */

		if (!IsInfraTileUsageAllowed(VEH_ROAD, v->owner, tile) || GetRoadStopDir(tile) == enterdir || v->HasArticulatedPart()) {
			/* different station owner or wrong orientation or the vehicle has articulated parts */
			trackdirs = TRACKDIR_BIT_NONE;
		} else {
			/* Our station */
			RoadStopType rstype = v->IsBus() ? ROADSTOP_BUS : ROADSTOP_TRUCK;

			if (GetRoadStopType(tile) != rstype) {
				/* Wrong station type */
				trackdirs = TRACKDIR_BIT_NONE;
			} else {
				/* Proper station type, check if there is free loading bay */
				if (!_settings_game.pf.roadveh_queue && IsBayRoadStopTile(tile) &&
						!RoadStop::GetByTile(tile, rstype)->HasFreeBay()) {
					/* Station is full and RV queuing is off */
					trackdirs = TRACKDIR_BIT_NONE;
				}
			}
		}
	}
	/* The above lookups should be moved to GetTileTrackStatus in the
	 * future, but that requires more changes to the pathfinder and other
	 * stuff, probably even more arguments to GTTS.
	 */

	/* Remove tracks unreachable from the enter dir */
	trackdirs &= DiagdirReachesTrackdirs(enterdir);
	if (trackdirs == TRACKDIR_BIT_NONE) {
		/* If vehicle expected a path, it no longer exists, so invalidate it. */
		if (v->cached_path != nullptr) v->cached_path->clear();
		/* No reachable tracks, so we'll reverse */
		return_track(_road_reverse_table[enterdir]);
	}

	if (v->reverse_ctr != 0) {
		bool reverse = true;
		if (RoadTypeIsTram(v->roadtype)) {
			/* Trams may only reverse on a tile if it contains at least the straight
			 * trackbits or when it is a valid turning tile (i.e. one roadbit) */
			RoadBits rb = GetAnyRoadBits(tile, RTT_TRAM);
			RoadBits straight = AxisToRoadBits(DiagDirToAxis(enterdir));
			reverse = ((rb & straight) == straight) ||
			          (rb == DiagDirToRoadBits(enterdir));
		}
		if (reverse) {
			v->reverse_ctr = 0;
			if (v->tile != tile) {
				return_track(_road_reverse_table[enterdir]);
			}
		}
	}

	desttile = v->dest_tile;
	if (desttile == 0) {
		/* We've got no destination, pick a random track */
		return_track(PickRandomBit(trackdirs));
	}

	/* Only one track to choose between? */
	if (KillFirstBit(trackdirs) == TRACKDIR_BIT_NONE) {
		if (v->cached_path != nullptr && !v->cached_path->empty() && v->cached_path->front_tile() == tile) {
			/* Vehicle expected a choice here, invalidate its path. */
			v->cached_path->clear();
		}
		return_track(FindFirstBit(trackdirs));
	}

	/* Path cache is out of date, clear it */
	if (v->cached_path != nullptr && !v->cached_path->empty() && v->cached_path->layout_ctr != _road_layout_change_counter) {
		v->cached_path->clear();
	}

	/* Attempt to follow cached path. */
	if (v->cached_path != nullptr && !v->cached_path->empty()) {
		if (v->cached_path->front_tile() != tile) {
			/* Vehicle didn't expect a choice here, invalidate its path. */
			v->cached_path->clear();
		} else {
			Trackdir trackdir = v->cached_path->front_td();

			if (HasBit(trackdirs, trackdir)) {
				v->cached_path->pop_front();
				return_track(trackdir);
			}

			/* Vehicle expected a choice which is no longer available. */
			v->cached_path->clear();
		}
	}

	switch (_settings_game.pf.pathfinder_for_roadvehs) {
		case VPF_NPF:  best_track = NPFRoadVehicleChooseTrack(v, tile, enterdir, path_found); break;
		case VPF_YAPF: best_track = YapfRoadVehicleChooseTrack(v, tile, enterdir, trackdirs, path_found, v->GetOrCreatePathCache()); break;

		default: NOT_REACHED();
	}
	DEBUG_UPDATESTATECHECKSUM("RoadFindPathToDest: v: %u, path_found: %d, best_track: %d", v->index, path_found, best_track);
	UpdateStateChecksum((((uint64_t) v->index) << 32) | (path_found << 16) | best_track);
	v->HandlePathfindingResult(path_found);

found_best_track:;

	if (HasBit(red_signals, best_track)) return INVALID_TRACKDIR;

	return best_track;
}

struct RoadDriveEntry {
	byte x, y;
};

#include "table/roadveh_movement.h"

static bool RoadVehLeaveDepot(RoadVehicle *v, bool first)
{
	/* Don't leave unless v and following wagons are in the depot. */
	for (const RoadVehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->state != RVSB_IN_DEPOT || u->tile != v->tile) return false;
	}

	DiagDirection dir = GetRoadDepotDirection(v->tile);
	v->direction = DiagDirToDir(dir);

	Trackdir tdir = DiagDirToDiagTrackdir(dir);
	const RoadDriveEntry *rdp = _road_drive_data[GetRoadTramType(v->roadtype)][(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE) + tdir];

	int x = TileX(v->tile) * TILE_SIZE + (rdp[RVC_DEPOT_START_FRAME].x & 0xF);
	int y = TileY(v->tile) * TILE_SIZE + (rdp[RVC_DEPOT_START_FRAME].y & 0xF);

	if (first) {
		/* We are leaving a depot, but have to go to the exact same one; re-enter */
		if (v->current_order.IsType(OT_GOTO_DEPOT) && v->tile == v->dest_tile) {
			VehicleEnterDepot(v);
			return true;
		}

		if (RoadVehFindCloseTo(v, x, y, v->direction, false) != nullptr) return true;

		VehicleServiceInDepot(v);
		v->LeaveUnbunchingDepot();

		StartRoadVehSound(v);

		/* Vehicle is about to leave a depot */
		v->cur_speed = 0;
	}

	v->vehstatus &= ~VS_HIDDEN;
	v->InvalidateImageCache();
	v->state = tdir;
	v->frame = RVC_DEPOT_START_FRAME;
	v->UpdateIsDrawn();

	v->x_pos = x;
	v->y_pos = y;
	v->UpdatePosition();
	v->UpdateInclination(true, true);

	InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);

	return true;
}

static Trackdir FollowPreviousRoadVehicle(const RoadVehicle *v, const RoadVehicle *prev, TileIndex tile, DiagDirection entry_dir, bool already_reversed)
{
	if (prev->tile == v->tile && !already_reversed) {
		/* If the previous vehicle is on the same tile as this vehicle is
		 * then it must have reversed. */
		return _road_reverse_table[entry_dir];
	}

	byte prev_state = prev->state;
	Trackdir dir;

	if (prev_state == RVSB_WORMHOLE || prev_state == RVSB_IN_DEPOT) {
		DiagDirection diag_dir = INVALID_DIAGDIR;

		if (IsTileType(tile, MP_TUNNELBRIDGE)) {
			diag_dir = GetTunnelBridgeDirection(tile);
		} else if (IsRoadDepotTile(tile)) {
			diag_dir = ReverseDiagDir(GetRoadDepotDirection(tile));
		}

		if (diag_dir == INVALID_DIAGDIR) return INVALID_TRACKDIR;
		dir = DiagDirToDiagTrackdir(diag_dir);
	} else {
		if (already_reversed && prev->tile != tile) {
			/*
			 * The vehicle has reversed, but did not go straight back.
			 * It immediately turn onto another tile. This means that
			 * the roadstate of the previous vehicle cannot be used
			 * as the direction we have to go with this vehicle.
			 *
			 * Next table is build in the following way:
			 *  - first row for when the vehicle in front went to the northern or
			 *    western tile, second for southern and eastern.
			 *  - columns represent the entry direction.
			 *  - cell values are determined by the Trackdir one has to take from
			 *    the entry dir (column) to the tile in north or south by only
			 *    going over the trackdirs used for turning 90 degrees, i.e.
			 *    TRACKDIR_{UPPER,RIGHT,LOWER,LEFT}_{N,E,S,W}.
			 */
			static const Trackdir reversed_turn_lookup[2][DIAGDIR_END] = {
				{ TRACKDIR_UPPER_W, TRACKDIR_RIGHT_N, TRACKDIR_LEFT_N,  TRACKDIR_UPPER_E },
				{ TRACKDIR_RIGHT_S, TRACKDIR_LOWER_W, TRACKDIR_LOWER_E, TRACKDIR_LEFT_S  }};
			dir = reversed_turn_lookup[prev->tile < tile ? 0 : 1][ReverseDiagDir(entry_dir)];
		} else if (HasBit(prev_state, RVS_IN_DT_ROAD_STOP)) {
			dir = (Trackdir)(prev_state & RVSB_ROAD_STOP_TRACKDIR_MASK);
		} else if (prev_state < TRACKDIR_END) {
			dir = (Trackdir)prev_state;
		} else {
			return INVALID_TRACKDIR;
		}
	}

	/* Do some sanity checking. */
	static const RoadBits required_roadbits[] = {
		ROAD_X,            ROAD_Y,            ROAD_NW | ROAD_NE, ROAD_SW | ROAD_SE,
		ROAD_NW | ROAD_SW, ROAD_NE | ROAD_SE, ROAD_X,            ROAD_Y
	};
	RoadBits required = required_roadbits[dir & 0x07];

	if ((required & GetAnyRoadBits(tile, GetRoadTramType(v->roadtype), false)) == ROAD_NONE) {
		dir = INVALID_TRACKDIR;
	}

	return dir;
}

/**
 * Can a tram track build without destruction on the given tile?
 * @param c the company that would be building the tram tracks
 * @param t the tile to build on.
 * @param rt the tram type to build.
 * @param r the road bits needed.
 * @return true when a track track can be build on 't'
 */
static bool CanBuildTramTrackOnTile(CompanyID c, TileIndex t, RoadType rt, RoadBits r)
{
	/* The 'current' company is not necessarily the owner of the vehicle. */
	Backup<CompanyID> cur_company(_current_company, c, FILE_LINE);

	CommandCost ret = DoCommand(t, rt << 4 | r, 0, DC_NO_WATER, CMD_BUILD_ROAD);

	cur_company.Restore();
	return ret.Succeeded();
}

static bool IsRoadVehicleOnOtherSideOfRoad(const RoadVehicle *v)
{
	bool is_right;
	switch (DirToDiagDir(v->direction)) {
		case DIAGDIR_NE:
			is_right = ((TILE_UNIT_MASK & v->y_pos) == 9);
			break;
		case DIAGDIR_SE:
			is_right = ((TILE_UNIT_MASK & v->x_pos) == 9);
			break;
		case DIAGDIR_SW:
			is_right = ((TILE_UNIT_MASK & v->y_pos) == 5);
			break;
		case DIAGDIR_NW:
			is_right = ((TILE_UNIT_MASK & v->x_pos) == 5);
			break;
		default:
			NOT_REACHED();
	}

	return is_right != (bool) _settings_game.vehicle.road_side;
}

struct FinishOvertakeData {
	Direction direction;
	const Vehicle *v;
	int min_coord;
	int max_coord;
	uint8_t not_road_pos;
	RoadTypeCollisionMode collision_mode;
};

static Vehicle *EnumFindVehBlockingFinishOvertake(Vehicle *v, void *data)
{
	const FinishOvertakeData *od = (FinishOvertakeData*)data;

	if (v->First() == od->v) return nullptr;
	if (!HasBit(_collision_mode_roadtypes[od->collision_mode], RoadVehicle::From(v)->roadtype)) return nullptr;

	/* Check if other vehicle is behind */
	switch (DirToDiagDir(v->direction)) {
		case DIAGDIR_NE:
		case DIAGDIR_SW:
			if ((v->y_pos & TILE_UNIT_MASK) == od->not_road_pos) return nullptr;
			if (v->x_pos >= od->min_coord && v->x_pos <= od->max_coord) return v;
			break;
		case DIAGDIR_SE:
		case DIAGDIR_NW:
			if ((v->x_pos & TILE_UNIT_MASK) == od->not_road_pos) return nullptr;
			if (v->y_pos >= od->min_coord && v->y_pos <= od->max_coord) return v;
			break;
		default:
			NOT_REACHED();
	}
	return nullptr;
}

static void RoadVehCheckFinishOvertake(RoadVehicle *v)
{
	/* Cancel overtake if the vehicle is broken or about to break down */
	if (v->breakdown_ctr != 0) {
		v->SetRoadVehicleOvertaking(0);
		return;
	}

	FinishOvertakeData od;
	od.direction = v->direction;
	od.v = v;
	od.collision_mode = GetRoadTypeInfo(v->roadtype)->collision_mode;

	const RoadVehicle *last = v->Last();
	const int front_margin = 10;
	const int back_margin = 10;
	DiagDirection dir = DirToDiagDir(v->direction);
	switch (dir) {
		case DIAGDIR_NE:
			od.min_coord = v->x_pos - front_margin;
			od.max_coord = last->x_pos + back_margin;
			od.not_road_pos = (_settings_game.vehicle.road_side ? 5 : 9);
			break;
		case DIAGDIR_SE:
			od.min_coord = last->y_pos - back_margin;
			od.max_coord = v->y_pos + front_margin;
			od.not_road_pos = (_settings_game.vehicle.road_side ? 5 : 9);
			break;
		case DIAGDIR_SW:
			od.min_coord = last->x_pos - back_margin;
			od.max_coord = v->x_pos + front_margin;
			od.not_road_pos = (_settings_game.vehicle.road_side ? 9 : 5);
			break;
		case DIAGDIR_NW:
			od.min_coord = v->y_pos - front_margin;
			od.max_coord = last->y_pos + back_margin;
			od.not_road_pos = (_settings_game.vehicle.road_side ? 9 : 5);
			break;
		default:
			NOT_REACHED();
	}

	TileIndexDiffC ti = TileIndexDiffCByDiagDir(DirToDiagDir(v->direction));
	bool check_ahead = true;
	int tiles_behind = 1 + CeilDiv(v->gcache.cached_total_length, TILE_SIZE);

	TileIndex check_tile = v->tile;
	if (IsTileType(check_tile, MP_TUNNELBRIDGE)) {
		TileIndex ahead = GetOtherTunnelBridgeEnd(check_tile);
		if (v->state == RVSB_WORMHOLE) {
			check_ahead = false;
		}
		if (GetTunnelBridgeDirection(check_tile) == dir) {
			check_ahead = false;
		} else if (GetTunnelBridgeDirection(check_tile) == ReverseDiagDir(dir)) {
			std::swap(ahead, check_tile);
		}

		if (HasVehicleOnPos(ahead, VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
		if (HasVehicleOnPos(check_tile, VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
		tiles_behind -= 1 + DistanceManhattan(check_tile, TileVirtXY(v->x_pos, v->y_pos));
		check_tile = TileAddWrap(check_tile, -ti.x, -ti.y);
	}

	if (check_ahead) {
		TileIndex ahead_tile = TileAddWrap(check_tile, ti.x, ti.y);
		if (ahead_tile != INVALID_TILE) {
			if (HasVehicleOnPos(ahead_tile, VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
			if (IsTileType(ahead_tile, MP_TUNNELBRIDGE) && HasVehicleOnPos(GetOtherTunnelBridgeEnd(ahead_tile), VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
		}
	}

	for (; check_tile != INVALID_TILE && tiles_behind > 0; tiles_behind--, check_tile = TileAddWrap(check_tile, -ti.x, -ti.y)) {
		if (HasVehicleOnPos(check_tile, VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
		if (IsTileType(check_tile, MP_TUNNELBRIDGE)) {
			TileIndex other_end = GetOtherTunnelBridgeEnd(check_tile);
			tiles_behind -= DistanceManhattan(other_end, check_tile);
			if (HasVehicleOnPos(other_end, VEH_ROAD, &od, EnumFindVehBlockingFinishOvertake)) return;
			check_tile = other_end;
		}
	}

	/* road on the normal side is clear, finish overtake */
	v->SetRoadVehicleOvertaking(0);
}

inline byte IncreaseOvertakingCounter(RoadVehicle *v)
{
	if (v->overtaking_ctr != 255) v->overtaking_ctr++;
	return v->overtaking_ctr;
}

static bool CheckRestartLoadingAtRoadStop(RoadVehicle *v)
{
	if (v->GetNumOrders() < 1 || !Company::Get(v->owner)->settings.remain_if_next_order_same_station) return false;

	StationID station_id = v->current_order.GetDestination();
	VehicleOrderID next_order_idx = AdvanceOrderIndexDeferred(v, v->cur_implicit_order_index);
	const Order *next_order = v->GetOrder(next_order_idx);
	FlushAdvanceOrderIndexDeferred(v, false);
	if (next_order != nullptr && next_order->IsType(OT_GOTO_STATION) && next_order->GetDestination() == station_id &&
			!(next_order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) &&
			IsInfraTileUsageAllowed(VEH_ROAD, v->owner, v->tile) &&
			GetRoadStopType(v->tile) == (v->IsBus() ? ROADSTOP_BUS : ROADSTOP_TRUCK)) {
		v->current_order.Free();
		ProcessOrders(v);

		/* Double check that order prediction was correct and v->current_order is now for the same station */
		if (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == station_id &&
				!(v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
			v->last_station_visited = station_id;
			v->BeginLoading();
			return true;
		} else {
			/* Order prediction was incorrect, this should not be reached, just restore the leave station order */
			v->current_order.MakeLeaveStation();
			v->current_order.SetDestination(station_id);
		}
	}

	return false;
}

bool IndividualRoadVehicleController(RoadVehicle *v, const RoadVehicle *prev)
{
	SCOPE_INFO_FMT([&], "IndividualRoadVehicleController: %s, %s", scope_dumper().VehicleInfo(v), scope_dumper().VehicleInfo(prev));
	if (v->overtaking & RVSB_DRIVE_SIDE && v->IsFrontEngine())  {
		if (IsNonOvertakingStationTile(v->tile, DirToDiagDir(v->direction))) {
			/* Force us to be not overtaking! */
			v->SetRoadVehicleOvertaking(0);
		} else if (v->HasArticulatedPart() && (v->state >= RVSB_IN_ROAD_STOP || !IsStraightRoadTrackdir((Trackdir)v->state)) && !IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) && v->state != RVSB_WORMHOLE) {
			/* Articulated RVs may not overtake on corners */
			v->SetRoadVehicleOvertaking(0);
		} else if (v->HasArticulatedPart() && IsBridgeTile(v->tile) && (IsRoadCustomBridgeHeadTile(v->tile) || IsRoadCustomBridgeHeadTile(GetOtherBridgeEnd(v->tile)))) {
			/* Articulated RVs may not overtake on custom bridge heads */
			v->SetRoadVehicleOvertaking(0);
		} else if (v->state < RVSB_IN_ROAD_STOP && !IsStraightRoadTrackdir((Trackdir)v->state) && IsOneWaySideJunctionRoadTile(v->tile)) {
			/* No turning to/from overtaking lane on one way side road junctions */
			v->SetRoadVehicleOvertaking(0);
		} else if (IncreaseOvertakingCounter(v) >= RV_OVERTAKE_TIMEOUT) {
			/* If overtaking just aborts at a random moment, we can have a out-of-bound problem,
			 *  if the vehicle started a corner. To protect that, only allow an abort of
			 *  overtake if we are on straight roads */
			if (v->overtaking_ctr >= v->GetOvertakingCounterThreshold() && (v->state == RVSB_WORMHOLE || (v->state < RVSB_IN_ROAD_STOP && IsStraightRoadTrackdir((Trackdir)v->state)))) {
				if (IsOneWayRoadTile(v->tile)) {
					RoadVehCheckFinishOvertake(v);
				} else {
					v->SetRoadVehicleOvertaking(0);
				}
			}
		}
	}

	/* If this vehicle is in a depot and we've reached this point it must be
	 * one of the articulated parts. It will stay in the depot until activated
	 * by the previous vehicle in the chain when it gets to the right place. */
	if (v->IsInDepot()) return true;

	bool no_advance_tile = false;

	if (v->state == RVSB_WORMHOLE) {
		/* Vehicle is entering a depot or is on a bridge or in a tunnel */
		GetNewVehiclePosResult gp = GetNewVehiclePos(v);
		if (v->overtaking & 1) {
			DiagDirection dir = DirToDiagDir(v->direction);
			switch (dir) {
				case DIAGDIR_NE:
				case DIAGDIR_SW:
					SB(gp.y, 0, 4, (_settings_game.vehicle.road_side ^ (dir >> 1) ^ (v->overtaking >> RVS_DRIVE_SIDE)) ? 9 : 5);
					break;
				case DIAGDIR_SE:
				case DIAGDIR_NW:
					SB(gp.x, 0, 4, (_settings_game.vehicle.road_side ^ (dir >> 1) ^ (v->overtaking >> RVS_DRIVE_SIDE)) ? 9 : 5);
					break;
				default:
					NOT_REACHED();
			}
		}
		if (v->IsFrontEngine()) {
			RoadVehicle *u = RoadVehFindCloseTo(v, gp.x, gp.y, v->direction);
			if (u != nullptr) {
				u = u->First();
				/* There is a vehicle in front overtake it if possible */
				byte old_overtaking = v->overtaking;
				if (v->overtaking == 0) RoadVehCheckOvertake(v, u);
				if (v->overtaking == old_overtaking) {
					v->cur_speed = u->cur_speed;
				}
				return false;
			}
		}
		v->overtaking &= ~1;

		if (IsTileType(gp.new_tile, MP_TUNNELBRIDGE) && HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
			if (IsRoadCustomBridgeHeadTile(gp.new_tile)) {
				v->frame = 15;
				no_advance_tile = true;
			} else {
				/* Vehicle has just entered a bridge or tunnel */
				v->x_pos = gp.x;
				v->y_pos = gp.y;
				v->UpdatePosition();
				v->UpdateInclination(true, true);
				return true;
			}
		} else {
			v->x_pos = gp.x;
			v->y_pos = gp.y;
			v->UpdatePosition();
			RoadZPosAffectSpeed(v, v->UpdateInclination(false, false, true));
			if (v->IsDrawn()) v->Vehicle::UpdateViewport(true);
			return true;
		}
	}

	/* Get move position data for next frame.
	 * For a drive-through road stop use 'straight road' move data.
	 * In this case v->state is masked to give the road stop entry direction. */
	RoadDriveEntry rd = _road_drive_data[GetRoadTramType(v->roadtype)][(
		(HasBit(v->state, RVS_IN_DT_ROAD_STOP) ? v->state & RVSB_ROAD_STOP_TRACKDIR_MASK : v->state) +
		(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)) ^ v->overtaking][v->frame + 1];

	if (rd.x & RDE_NEXT_TILE) {
		TileIndex tile = v->tile;
		if (!no_advance_tile) tile += TileOffsByDiagDir((DiagDirection)(rd.x & 3));
		Trackdir dir;

		if (v->IsFrontEngine()) {
			/* If this is the front engine, look for the right path. */
			if (HasTileAnyRoadType(tile, v->compatible_roadtypes)) {
				dir = RoadFindPathToDest(v, tile, (DiagDirection)(rd.x & 3));
			} else {
				dir = _road_reverse_table[(DiagDirection)(rd.x & 3)];
			}
		} else if (no_advance_tile) {
			/* Follow previous vehicle out of custom bridge wormhole */
			dir = (Trackdir) prev->state;
		} else {
			dir = FollowPreviousRoadVehicle(v, prev, tile, (DiagDirection)(rd.x & 3), false);
		}

		if (dir == INVALID_TRACKDIR) {
			if (!v->IsFrontEngine()) error("Disconnecting road vehicle.");
			v->cur_speed = 0;
			return false;
		}

again:
		uint start_frame = RVC_DEFAULT_START_FRAME;
		if (IsReversingRoadTrackdir(dir)) {
			/* When turning around we can't be overtaking. */
			v->SetRoadVehicleOvertaking(0);

			if (no_advance_tile) {
				DEBUG(misc, 0, "Road vehicle attempted to turn around on a single road piece bridge head");
			}

			/* Turning around */
			if (RoadTypeIsTram(v->roadtype)) {
				/* Determine the road bits the tram needs to be able to turn around
				 * using the 'big' corner loop. */
				RoadBits needed;
				switch (dir) {
					default: NOT_REACHED();
					case TRACKDIR_RVREV_NE: needed = ROAD_SW; break;
					case TRACKDIR_RVREV_SE: needed = ROAD_NW; break;
					case TRACKDIR_RVREV_SW: needed = ROAD_NE; break;
					case TRACKDIR_RVREV_NW: needed = ROAD_SE; break;
				}
				auto tile_turn_ok = [&]() -> bool {
					if (IsNormalRoadTile(tile)) {
						return !HasRoadWorks(tile) && HasTileAnyRoadType(tile, v->compatible_roadtypes) && (needed & GetRoadBits(tile, RTT_TRAM)) != ROAD_NONE;
					} else if (IsRoadCustomBridgeHeadTile(tile)) {
						return HasTileAnyRoadType(tile, v->compatible_roadtypes) && (needed & GetCustomBridgeHeadRoadBits(tile, RTT_TRAM) & ~DiagDirToRoadBits(GetTunnelBridgeDirection(tile))) != ROAD_NONE;
					} else {
						return false;
					}
				};
				if ((v->Previous() != nullptr && v->Previous()->tile == tile) || (v->IsFrontEngine() && tile_turn_ok())) {
					/*
					 * Taking the 'big' corner for trams only happens when:
					 * - The previous vehicle in this (articulated) tram chain is
					 *   already on the 'next' tile, we just follow them regardless of
					 *   anything. When it is NOT on the 'next' tile, the tram started
					 *   doing a reversing turn when the piece of tram track on the next
					 *   tile did not exist yet. Do not use the big tram loop as that is
					 *   going to cause the tram to split up.
					 * - Or the front of the tram can drive over the next tile.
					 */
				} else if (!v->IsFrontEngine() || !CanBuildTramTrackOnTile(v->owner, tile, v->roadtype, needed) || ((~needed & GetAnyRoadBits(v->tile, RTT_TRAM, false)) == ROAD_NONE)) {
					/*
					 * Taking the 'small' corner for trams only happens when:
					 * - We are not the from vehicle of an articulated tram.
					 * - Or when the company cannot build on the next tile.
					 *
					 * The 'small' corner means that the vehicle is on the end of a
					 * tram track and needs to start turning there. To do this properly
					 * the tram needs to start at an offset in the tram turning 'code'
					 * for 'big' corners. It furthermore does not go to the next tile,
					 * so that needs to be fixed too.
					 */
					tile = v->tile;
					start_frame = RVC_TURN_AROUND_START_FRAME_SHORT_TRAM;
				} else {
					/* The company can build on the next tile, so wait till they do. */
					v->cur_speed = 0;
					return false;
				}
			} else if (IsOneWayRoadTile(v->tile) && !MayReverseOnOneWayRoadTile(v->tile, (DiagDirection)(rd.x & 3))) {
				v->cur_speed = 0;
				return false;
			} else {
				tile = v->tile;
			}
		}

		/* Get position data for first frame on the new tile */
		const RoadDriveEntry *rdp = _road_drive_data[GetRoadTramType(v->roadtype)][(dir + (_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)) ^ v->overtaking];

		int x = TileX(tile) * TILE_SIZE + rdp[start_frame].x;
		int y = TileY(tile) * TILE_SIZE + rdp[start_frame].y;

		Direction new_dir = RoadVehGetSlidingDirection(v, x, y);
		if (v->IsFrontEngine()) {
			const Vehicle *u = RoadVehFindCloseTo(v, x, y, new_dir);
			if (u != nullptr) {
				v->cur_speed = u->First()->cur_speed;
				/* We might be blocked, prevent pathfinding rerun as we already know where we are heading to. */
				v->GetOrCreatePathCache().push_front(tile, dir);
				return false;
			}
		}

		uint32_t r = VehicleEnterTile(v, tile, x, y);
		if (HasBit(r, VETS_CANNOT_ENTER)) {
			if (!IsTileType(tile, MP_TUNNELBRIDGE)) {
				v->cur_speed = 0;
				return false;
			}
			/* Try an about turn to re-enter the previous tile */
			dir = _road_reverse_table[rd.x & 3];
			goto again;
		}

		if (IsInsideMM(v->state, RVSB_IN_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) && IsTileType(v->tile, MP_STATION)) {
			if (IsReversingRoadTrackdir(dir) && IsInsideMM(v->state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END)) {
				/* New direction is trying to turn vehicle around.
				 * We can't turn at the exit of a road stop so wait.*/
				v->cur_speed = 0;
				return false;
			}

			/* If we are a drive through road stop and the next tile is of
			 * the same road stop and the next tile isn't this one (i.e. we
			 * are not reversing), then keep the reservation and state.
			 * This way we will not be shortly unregister from the road
			 * stop. It also makes it possible to load when on the edge of
			 * two road stops; otherwise you could get vehicles that should
			 * be loading but are not actually loading. */
			if (IsStationRoadStopTile(v->tile) && IsDriveThroughStopTile(v->tile) &&
					RoadStop::IsDriveThroughRoadStopContinuation(v->tile, tile) &&
					v->tile != tile) {
				/* So, keep 'our' state */
				dir = (Trackdir)v->state;
			} else if (IsStationRoadStop(v->tile)) {
				/* We're not continuing our drive through road stop, so leave. */
				RoadStop::GetByTile(v->tile, GetRoadStopType(v->tile))->Leave(v);
			}
		}

		if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
			v->InvalidateImageCache();
			TileIndex old_tile = v->tile;

			v->tile = tile;
			v->state = (byte)dir;
			v->frame = start_frame;
			RoadTramType rtt = GetRoadTramType(v->roadtype);
			if (GetRoadType(old_tile, rtt) != GetRoadType(tile, rtt)) {
				if (v->IsFrontEngine()) {
					RoadVehUpdateCache(v);
				}
				v->First()->CargoChanged();
			}
		}
		if (new_dir != v->direction) {
			v->direction = new_dir;
			if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) v->cur_speed -= v->cur_speed >> 2;
		}
		v->x_pos = x;
		v->y_pos = y;
		v->UpdatePosition();
		RoadZPosAffectSpeed(v, v->UpdateInclination(true, true));
		return true;
	}

	if (rd.x & RDE_TURNED) {
		/* Vehicle has finished turning around, it will now head back onto the same tile */
		Trackdir dir;
		uint turn_around_start_frame = RVC_TURN_AROUND_START_FRAME;

		if (RoadTypeIsTram(v->roadtype) && !IsRoadDepotTile(v->tile) && HasExactlyOneBit(GetAnyRoadBits(v->tile, RTT_TRAM, false))) {
			/*
			 * The tram is turning around with one tram 'roadbit'. This means that
			 * it is using the 'big' corner 'drive data'. However, to support the
			 * trams to take a small corner, there is a 'turned' marker in the middle
			 * of the turning 'drive data'. When the tram took the long corner, we
			 * will still use the 'big' corner drive data, but we advance it one
			 * frame. We furthermore set the driving direction so the turning is
			 * going to be properly shown.
			 */
			turn_around_start_frame = RVC_START_FRAME_AFTER_LONG_TRAM;
			switch (rd.x & 0x3) {
				default: NOT_REACHED();
				case DIAGDIR_NW: dir = TRACKDIR_RVREV_SE; break;
				case DIAGDIR_NE: dir = TRACKDIR_RVREV_SW; break;
				case DIAGDIR_SE: dir = TRACKDIR_RVREV_NW; break;
				case DIAGDIR_SW: dir = TRACKDIR_RVREV_NE; break;
			}
		} else {
			if (v->IsFrontEngine()) {
				/* If this is the front engine, look for the right path. */
				dir = RoadFindPathToDest(v, v->tile, (DiagDirection)(rd.x & 3));
			} else {
				dir = FollowPreviousRoadVehicle(v, prev, v->tile, (DiagDirection)(rd.x & 3), true);
			}
		}

		if (dir == INVALID_TRACKDIR) {
			v->cur_speed = 0;
			return false;
		}

		const RoadDriveEntry *rdp = _road_drive_data[GetRoadTramType(v->roadtype)][(_settings_game.vehicle.road_side << RVS_DRIVE_SIDE) + dir];

		int x = TileX(v->tile) * TILE_SIZE + rdp[turn_around_start_frame].x;
		int y = TileY(v->tile) * TILE_SIZE + rdp[turn_around_start_frame].y;

		Direction new_dir = RoadVehGetSlidingDirection(v, x, y);
		if (v->IsFrontEngine()) {
			const Vehicle *u = RoadVehFindCloseTo(v, x, y, new_dir);
			if (u != nullptr) {
				v->cur_speed = u->First()->cur_speed;
				/* We might be blocked, prevent pathfinding rerun as we already know where we are heading to. */
				v->GetOrCreatePathCache().push_front(v->tile, dir);
				return false;
			}
		}

		uint32_t r = VehicleEnterTile(v, v->tile, x, y);
		if (HasBit(r, VETS_CANNOT_ENTER)) {
			v->cur_speed = 0;
			return false;
		}

		v->InvalidateImageCache();
		v->state = dir;
		v->frame = turn_around_start_frame;

		if (new_dir != v->direction) {
			v->direction = new_dir;
			if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) v->cur_speed -= v->cur_speed >> 2;
		}

		v->x_pos = x;
		v->y_pos = y;
		v->UpdatePosition();
		RoadZPosAffectSpeed(v, v->UpdateInclination(true, true));
		return true;
	}

	/* This vehicle is not in a wormhole and it hasn't entered a new tile. If
	 * it's on a depot tile, check if it's time to activate the next vehicle in
	 * the chain yet. */
	if (v->Next() != nullptr && IsRoadDepotTile(v->tile)) {
		if (v->frame == v->gcache.cached_veh_length + RVC_DEPOT_START_FRAME) {
			RoadVehLeaveDepot(v->Next(), false);
		}
	}

	/* Calculate new position for the vehicle */
	int x = (v->x_pos & ~15) + (rd.x & 15);
	int y = (v->y_pos & ~15) + (rd.y & 15);

	Direction new_dir = RoadVehGetSlidingDirection(v, x, y);

	if (v->IsFrontEngine() && !IsInsideMM(v->state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END)) {
		/* Vehicle is not in a road stop.
		 * Check for another vehicle to overtake */
		RoadVehicle *u = RoadVehFindCloseTo(v, x, y, new_dir);

		if (u != nullptr) {
			u = u->First();
			/* There is a vehicle in front overtake it if possible */
			byte old_overtaking = v->overtaking;
			if (v->overtaking == 0) RoadVehCheckOvertake(v, u);
			if (v->overtaking == old_overtaking) v->cur_speed = u->cur_speed;

			/* In case an RV is stopped in a road stop, why not try to load? */
			if (v->cur_speed == 0 && IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) &&
					v->current_order.ShouldStopAtStation(v, GetStationIndex(v->tile), false) &&
					IsInfraTileUsageAllowed(VEH_ROAD, v->owner, v->tile) && !v->current_order.IsType(OT_LEAVESTATION) &&
					GetRoadStopType(v->tile) == (v->IsBus() ? ROADSTOP_BUS : ROADSTOP_TRUCK)) {
				byte cur_overtaking = IsRoadVehicleOnOtherSideOfRoad(v) ? RVSB_DRIVE_SIDE : 0;
				if (cur_overtaking != v->overtaking) v->SetRoadVehicleOvertaking(cur_overtaking);
				Station *st = Station::GetByTile(v->tile);
				v->last_station_visited = st->index;
				RoadVehArrivesAt(v, st);
				v->BeginLoading();
				TriggerRoadStopRandomisation(st, v->tile, RSRT_VEH_ARRIVES);
				TriggerRoadStopAnimation(st, v->tile, SAT_TRAIN_ARRIVES);
			}
			return false;
		}
	}

	Direction old_dir = v->direction;
	if (new_dir != old_dir) {
		v->direction = new_dir;
		if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) v->cur_speed -= v->cur_speed >> 2;

		/* Delay the vehicle in curves by making it require one additional frame per turning direction (two in total).
		 * A vehicle has to spend at least 9 frames on a tile, so the following articulated part can follow.
		 * (The following part may only be one tile behind, and the front part is moved before the following ones.)
		 * The short (inner) curve has 8 frames, this elongates it to 10. */
		v->UpdateViewport(true, true);
		return true;
	}

	/* If the vehicle is in a normal road stop and the frame equals the stop frame OR
	 * if the vehicle is in a drive-through road stop and this is the destination station
	 * and it's the correct type of stop (bus or truck) and the frame equals the stop frame...
	 * (the station test and stop type test ensure that other vehicles, using the road stop as
	 * a through route, do not stop) */
	if (v->IsFrontEngine() && ((IsInsideMM(v->state, RVSB_IN_ROAD_STOP, RVSB_IN_ROAD_STOP_END) &&
			_road_stop_stop_frame[v->state - RVSB_IN_ROAD_STOP + (_settings_game.vehicle.road_side << RVS_DRIVE_SIDE)] == v->frame) ||
			(IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END) &&
			v->current_order.ShouldStopAtStation(v, GetStationIndex(v->tile), false) &&
			IsInfraTileUsageAllowed(VEH_ROAD, v->owner, v->tile) &&
			GetRoadStopType(v->tile) == (v->IsBus() ? ROADSTOP_BUS : ROADSTOP_TRUCK) &&
			v->frame == RVC_DRIVE_THROUGH_STOP_FRAME))) {

		RoadStop *rs = RoadStop::GetByTile(v->tile, GetRoadStopType(v->tile));
		Station *st = Station::GetByTile(v->tile);

		/* Vehicle is at the stop position (at a bay) in a road stop.
		 * Note, if vehicle is loading/unloading it has already been handled,
		 * so if we get here the vehicle has just arrived or is just ready to leave. */
		if (!HasBit(v->state, RVS_ENTERED_STOP)) {
			/* Vehicle has arrived at a bay in a road stop */

			if (IsDriveThroughStopTile(v->tile)) {
				TileIndex next_tile = TileAddByDir(v->tile, v->direction);

				/* Check if next inline bay is free and has compatible road. */
				if (RoadStop::IsDriveThroughRoadStopContinuation(v->tile, next_tile) && HasTileAnyRoadType(next_tile, v->compatible_roadtypes)) {
					v->frame++;
					v->x_pos = x;
					v->y_pos = y;
					v->UpdatePosition();
					RoadZPosAffectSpeed(v, v->UpdateInclination(true, false));
					return true;
				}
			}

			rs->SetEntranceBusy(false);
			SetBit(v->state, RVS_ENTERED_STOP);

			v->last_station_visited = st->index;

			if (IsDriveThroughStopTile(v->tile) || (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == st->index)) {
				RoadVehArrivesAt(v, st);
				v->BeginLoading();
				TriggerRoadStopRandomisation(st, v->tile, RSRT_VEH_ARRIVES);
				TriggerRoadStopAnimation(st, v->tile, SAT_TRAIN_ARRIVES);
				return false;
			}
		} else {
			if (v->current_order.IsType(OT_LEAVESTATION)) {
				if (CheckRestartLoadingAtRoadStop(v)) return false;
			}

			/* Vehicle is ready to leave a bay in a road stop */
			if (rs->IsEntranceBusy()) {
				/* Road stop entrance is busy, so wait as there is nowhere else to go */
				v->cur_speed = 0;
				return false;
			}
			if (v->current_order.IsType(OT_LEAVESTATION)) {
				v->PlayLeaveStationSound();
				v->current_order.Free();
			}
		}

		if (IsBayRoadStopTile(v->tile)) rs->SetEntranceBusy(true);

		StartRoadVehSound(v);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	/* Check tile position conditions - i.e. stop position in depot,
	 * entry onto bridge or into tunnel */
	uint32_t r = VehicleEnterTile(v, v->tile, x, y);
	if (HasBit(r, VETS_CANNOT_ENTER)) {
		v->cur_speed = 0;
		return false;
	}

	if (v->current_order.IsType(OT_LEAVESTATION) && IsDriveThroughStopTile(v->tile)) {
		if (CheckRestartLoadingAtRoadStop(v)) return false;
		v->PlayLeaveStationSound();
		v->current_order.Free();
	}

	/* Move to next frame unless vehicle arrived at a stop position
	 * in a depot or entered a tunnel/bridge */
	if (!HasBit(r, VETS_ENTERED_WORMHOLE)) v->frame++;
	v->x_pos = x;
	v->y_pos = y;
	v->UpdatePosition();
	RoadZPosAffectSpeed(v, v->UpdateInclination(false, true, v->state == RVSB_WORMHOLE));
	return true;
}

static bool RoadVehController(RoadVehicle *v)
{
	/* decrease counters */
	v->current_order_time++;
	if (v->reverse_ctr != 0) v->reverse_ctr--;

	/* handle crashed */
	if (v->vehstatus & VS_CRASHED || RoadVehCheckTrainCrash(v)) {
		return RoadVehIsCrashed(v);
	}

	/* road vehicle has broken down? */
	if (v->HandleBreakdown()) return true;
	if (v->IsRoadVehicleStopped()) {
		v->cur_speed = 0;
		v->SetLastSpeed();
		return true;
	}

	ProcessOrders(v);
	v->HandleLoading();

	if (v->current_order.IsType(OT_LOADING)) return true;

	v->HandleWaiting(false, true);
	if (v->current_order.IsType(OT_WAITING)) return true;

	if (v->IsInDepot()) {
		/* Check if we should wait here for unbunching. */
		if (v->IsWaitingForUnbunching()) return true;
		if (RoadVehLeaveDepot(v, true)) return true;
	}

	int j;
	{
		int max_speed = v->GetCurrentMaxSpeed();
		v->ShowVisualEffect(max_speed);

		/* Check how far the vehicle needs to proceed */
		j = v->UpdateSpeed(max_speed);
	 }

	int adv_spd = v->GetAdvanceDistance();
	bool blocked = false;
	while (j >= adv_spd) {
		j -= adv_spd;

		RoadVehicle *u = v;
		for (RoadVehicle *prev = nullptr; u != nullptr; prev = u, u = u->Next()) {
			if (!IndividualRoadVehicleController(u, prev)) {
				blocked = true;
				break;
			}
		}
		if (blocked) break;

		/* Determine distance to next map position */
		adv_spd = v->GetAdvanceDistance();

		/* Test for a collision, but only if another movement will occur. */
		if (j >= adv_spd && RoadVehCheckTrainCrash(v)) break;
	}

	v->SetLastSpeed();

	for (RoadVehicle *u = v; u != nullptr; u = u->Next()) {
		if (!(u->IsDrawn())) continue;

		u->UpdateViewport(false, false);
	}

	/* If movement is blocked, set 'progress' to its maximum, so the roadvehicle does
	 * not accelerate again before it can actually move. I.e. make sure it tries to advance again
	 * on next tick to discover whether it is still blocked. */
	if (v->progress == 0) v->progress = blocked ? adv_spd - 1 : j;

	return true;
}

Money RoadVehicle::GetRunningCost() const
{
	const Engine *e = this->GetEngine();
	if (e->u.road.running_cost_class == INVALID_PRICE) return 0;

	uint cost_factor = GetVehicleProperty(this, PROP_ROADVEH_RUNNING_COST_FACTOR, e->u.road.running_cost);
	if (cost_factor == 0) return 0;

	Money cost = GetPrice(e->u.road.running_cost_class, cost_factor, e->GetGRF());

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

bool RoadVehicle::Tick()
{
	DEBUG_UPDATESTATECHECKSUM("RoadVehicle::Tick 1: v: %u, x: %d, y: %d", this->index, this->x_pos, this->y_pos);
	UpdateStateChecksum((((uint64_t) this->x_pos) << 32) | this->y_pos);
	DEBUG_UPDATESTATECHECKSUM("RoadVehicle::Tick 2: v: %u, state: %d, frame: %d", this->index, this->state, this->frame);
	UpdateStateChecksum((((uint64_t) this->state) << 32) | this->frame);
	if (this->IsFrontEngine()) {
		if (!(this->IsRoadVehicleStopped() || this->IsWaitingInDepot())) this->running_ticks++;
		return RoadVehController(this);
	}

	return true;
}

void RoadVehicle::SetDestTile(TileIndex tile)
{
	if (tile == this->dest_tile) return;
	if (this->cached_path != nullptr) this->cached_path->clear();
	this->dest_tile = tile;
}

void RoadVehicle::SetRoadVehicleOvertaking(byte overtaking)
{
	if (IsInsideMM(this->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) RoadStop::GetByTile(this->tile, GetRoadStopType(this->tile))->Leave(this);

	for (RoadVehicle *u = this; u != nullptr; u = u->Next()) {
		u->overtaking = overtaking;
		if (u->state == RVSB_WORMHOLE) u->overtaking |= 1;
	}

	if (IsInsideMM(this->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) RoadStop::GetByTile(this->tile, GetRoadStopType(this->tile))->Enter(this);
}

static void CheckIfRoadVehNeedsService(RoadVehicle *v)
{
	/* If we already got a slot at a stop, use that FIRST, and go to a depot later */
	if (Company::Get(v->owner)->settings.vehicle.servint_roadveh == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	uint max_penalty;
	switch (_settings_game.pf.pathfinder_for_roadvehs) {
		case VPF_NPF:  max_penalty = _settings_game.pf.npf.maximum_go_to_depot_penalty;  break;
		case VPF_YAPF: max_penalty = _settings_game.pf.yapf.maximum_go_to_depot_penalty; break;
		default: NOT_REACHED();
	}

	FindDepotData rfdd = FindClosestRoadDepot(v, max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) ? 2 : 1));
	/* Only go to the depot if it is not too far out of our way. */
	if (rfdd.best_length == UINT_MAX || rfdd.best_length > max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDestination() == GetDepotIndex(rfdd.tile) ? 2 : 1)) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			/* If we were already heading for a depot but it has
			 * suddenly moved farther away, we continue our normal
			 * schedule? */
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		return;
	}

	DepotID depot = GetDepotIndex(rfdd.tile);

	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS &&
			!Chance16(1, 20)) {
		return;
	}

	SetBit(v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
	v->current_order.MakeGoToDepot(depot, ODTFB_SERVICE);
	v->SetDestTile(rfdd.tile);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
}

void RoadVehicle::OnNewDay()
{
	if (!EconTime::UsingWallclockUnits()) AgeVehicle(this);

	if (!this->IsFrontEngine()) return;

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);
}

void RoadVehicle::OnPeriodic()
{
	if (!this->IsFrontEngine()) return;

	if (this->blocked_ctr == 0) CheckVehicleBreakdown(this);

	CheckIfRoadVehNeedsService(this);

	CheckOrders(this);

	if (this->running_ticks == 0) return;

	CommandCost cost(EXPENSES_ROADVEH_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR * DAY_TICKS));

	this->profit_this_year -= cost.GetCost();
	this->running_ticks = 0;

	SubtractMoneyFromCompanyFract(this->owner, cost);

	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	DirtyVehicleListWindowForVehicle(this);
}

Trackdir RoadVehicle::GetVehicleTrackdir() const
{
	if (this->vehstatus & VS_CRASHED) return INVALID_TRACKDIR;

	if (this->IsInDepot()) {
		/* We'll assume the road vehicle is facing outwards */
		return DiagDirToDiagTrackdir(GetRoadDepotDirection(this->tile));
	}

	if (IsBayRoadStopTile(this->tile)) {
		/* We'll assume the road vehicle is facing outwards */
		return DiagDirToDiagTrackdir(GetRoadStopDir(this->tile)); // Road vehicle in a station
	}

	/* Drive through road stops / wormholes (tunnels) */
	if (this->state > RVSB_TRACKDIR_MASK) return DiagDirToDiagTrackdir(DirToDiagDir(this->direction));

	/* If vehicle's state is a valid track direction (vehicle is not turning around) return it,
	 * otherwise transform it into a valid track direction */
	return (Trackdir)((IsReversingRoadTrackdir((Trackdir)this->state)) ? (this->state - 6) : this->state);
}

uint16_t RoadVehicle::GetMaxWeight() const
{
	uint16_t weight = CargoSpec::Get(this->cargo_type)->WeightOfNUnits(this->GetEngine()->DetermineCapacity(this));

	/* Vehicle weight is not added for articulated parts. */
	if (!this->IsArticulatedPart()) {
		/* Road vehicle weight is in units of 1/4 t. */
		weight += GetVehicleProperty(this, PROP_ROADVEH_WEIGHT, RoadVehInfo(this->engine_type)->weight) / 4;
	}

	return weight;
}
