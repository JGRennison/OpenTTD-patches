/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle.cpp Base implementations of all vehicles. */

#include "stdafx.h"
#include "error.h"
#include "roadveh.h"
#include "ship.h"
#include "spritecache.h"
#include "timetable.h"
#include "viewport_func.h"
#include "news_func.h"
#include "command_func.h"
#include "company_func.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_debug.h"
#include "newgrf_sound.h"
#include "newgrf_station.h"
#include "group_gui.h"
#include "strings_func.h"
#include "zoom_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "station_base.h"
#include "ai/ai.hpp"
#include "depot_func.h"
#include "network/network.h"
#include "core/pool_func.hpp"
#include "economy_base.h"
#include "articulated_vehicles.h"
#include "roadstop_base.h"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"
#include "infrastructure_func.h"
#include "order_backup.h"
#include "sound_func.h"
#include "effectvehicle_func.h"
#include "effectvehicle_base.h"
#include "vehiclelist.h"
#include "bridge_map.h"
#include "tunnel_map.h"
#include "depot_map.h"
#include "gamelog.h"
#include "tracerestrict.h"
#include "linkgraph/linkgraph.h"
#include "linkgraph/refresh.h"
#include "framerate_type.h"
#include "blitter/factory.hpp"
#include "tbtr_template_vehicle_func.h"
#include "string_func.h"
#include "scope_info.h"
#include "3rdparty/cpp-btree/btree_set.h"

#include "table/strings.h"

#include <algorithm>

#include "safeguards.h"

/* Number of bits in the hash to use from each vehicle coord */
static const uint GEN_HASHX_BITS = 6;
static const uint GEN_HASHY_BITS = 6;

/* Size of each hash bucket */
static const uint GEN_HASHX_BUCKET_BITS = 7;
static const uint GEN_HASHY_BUCKET_BITS = 6;

/* Compute hash for vehicle coord */
#define GEN_HASHX(x)    GB((x), GEN_HASHX_BUCKET_BITS + ZOOM_LVL_SHIFT, GEN_HASHX_BITS)
#define GEN_HASHY(y)   (GB((y), GEN_HASHY_BUCKET_BITS + ZOOM_LVL_SHIFT, GEN_HASHY_BITS) << GEN_HASHX_BITS)
#define GEN_HASH(x, y) (GEN_HASHY(y) + GEN_HASHX(x))

/* Maximum size until hash repeats */
static const int GEN_HASHX_SIZE = 1 << (GEN_HASHX_BUCKET_BITS + GEN_HASHX_BITS + ZOOM_LVL_SHIFT);
static const int GEN_HASHY_SIZE = 1 << (GEN_HASHY_BUCKET_BITS + GEN_HASHY_BITS + ZOOM_LVL_SHIFT);

/* Increments to reach next bucket in hash table */
static const int GEN_HASHX_INC = 1;
static const int GEN_HASHY_INC = 1 << GEN_HASHX_BITS;

/* Mask to wrap-around buckets */
static const uint GEN_HASHX_MASK =  (1 << GEN_HASHX_BITS) - 1;
static const uint GEN_HASHY_MASK = ((1 << GEN_HASHY_BITS) - 1) << GEN_HASHX_BITS;

VehicleID _new_vehicle_id;
uint16 _returned_refit_capacity;      ///< Stores the capacity after a refit operation.
uint16 _returned_mail_refit_capacity; ///< Stores the mail capacity after a refit operation (Aircraft only).


/** The pool with all our precious vehicles. */
VehiclePool _vehicle_pool("Vehicle");
INSTANTIATE_POOL_METHODS(Vehicle)

static btree::btree_set<Vehicle *> _vehicles_to_pay_repair;
static btree::btree_set<Vehicle *> _vehicles_to_sell;

/**
 * Determine shared bounds of all sprites.
 * @param [out] bounds Shared bounds.
 */
Rect16 VehicleSpriteSeq::GetBounds() const
{
	Rect16 bounds;
	bounds.left = bounds.top = bounds.right = bounds.bottom = 0;
	for (uint i = 0; i < this->count; ++i) {
		const Sprite *spr = GetSprite(this->seq[i].sprite, ST_NORMAL);
		if (i == 0) {
			bounds.left = spr->x_offs;
			bounds.top  = spr->y_offs;
			bounds.right  = spr->width  + spr->x_offs - 1;
			bounds.bottom = spr->height + spr->y_offs - 1;
		} else {
			if (spr->x_offs < bounds.left) bounds.left = spr->x_offs;
			if (spr->y_offs < bounds.top)  bounds.top  = spr->y_offs;
			int right  = spr->width  + spr->x_offs - 1;
			int bottom = spr->height + spr->y_offs - 1;
			if (right  > bounds.right)  bounds.right  = right;
			if (bottom > bounds.bottom) bounds.bottom = bottom;
		}
	}
	return bounds;
}

/**
 * Draw the sprite sequence.
 * @param x X position
 * @param y Y position
 * @param default_pal Vehicle palette
 * @param force_pal Whether to ignore individual palettes, and draw everything with \a default_pal.
 */
void VehicleSpriteSeq::Draw(int x, int y, PaletteID default_pal, bool force_pal) const
{
	for (uint i = 0; i < this->count; ++i) {
		PaletteID pal = force_pal || !this->seq[i].pal ? default_pal : this->seq[i].pal;
		DrawSprite(this->seq[i].sprite, pal, x, y);
	}
}

/**
 * Function to tell if a vehicle needs to be autorenewed
 * @param *c The vehicle owner
 * @param use_renew_setting Should the company renew setting be considered?
 * @return true if the vehicle is old enough for replacement
 */
bool Vehicle::NeedsAutorenewing(const Company *c, bool use_renew_setting) const
{
	/* We can always generate the Company pointer when we have the vehicle.
	 * However this takes time and since the Company pointer is often present
	 * when this function is called then it's faster to pass the pointer as an
	 * argument rather than finding it again. */
	assert(c == Company::Get(this->owner));

	if (use_renew_setting && !c->settings.engine_renew) return false;
	if (this->age - this->max_age < (c->settings.engine_renew_months * 30)) return false;

	/* Only engines need renewing */
	if (this->type == VEH_TRAIN && !Train::From(this)->IsEngine()) return false;

	return true;
}

/**
 * Service a vehicle and all subsequent vehicles in the consist
 *
 * @param *v The vehicle or vehicle chain being serviced
 */
void VehicleServiceInDepot(Vehicle *v)
{
	assert(v != NULL);
	const Engine *e = Engine::Get(v->engine_type);
	if (v->type == VEH_TRAIN) {
		if (v->Next() != NULL) VehicleServiceInDepot(v->Next());
		if (!(Train::From(v)->IsEngine()) && !(Train::From(v)->IsRearDualheaded())) return;
		ClrBit(Train::From(v)->flags, VRF_NEED_REPAIR);
		ClrBit(Train::From(v)->flags, VRF_HAS_HIT_RV);
		Train::From(v)->critical_breakdown_count = 0;
		const RailVehicleInfo *rvi = &e->u.rail;
		v->vcache.cached_max_speed = rvi->max_speed;
		if (Train::From(v)->IsFrontEngine()) {
			Train::From(v)->ConsistChanged(CCF_REFIT);
			CLRBITS(Train::From(v)->flags, (1 << VRF_BREAKDOWN_BRAKING) | VRF_IS_BROKEN );
		}
	} else if (v->type == VEH_ROAD) {
		RoadVehicle::From(v)->critical_breakdown_count = 0;
	}
	v->vehstatus &= ~VS_AIRCRAFT_BROKEN;
	SetWindowDirty(WC_VEHICLE_DETAILS, v->index); // ensure that last service date and reliability are updated

	do {
		v->date_of_last_service = _date;
		if (_settings_game.vehicle.pay_for_repair && v->breakdowns_since_last_service) {
			_vehicles_to_pay_repair.insert(v);
		} else {
			v->breakdowns_since_last_service = 0;
		}
		v->reliability = v->GetEngine()->reliability;
		/* Prevent vehicles from breaking down directly after exiting the depot. */
		v->breakdown_chance = 0;
		v->breakdown_ctr = 0;
		v = v->Next();
	} while (v != NULL && v->HasEngineType());
}

/**
 * Check if the vehicle needs to go to a depot in near future (if a opportunity presents itself) for service or replacement.
 *
 * @see NeedsAutomaticServicing()
 * @return true if the vehicle should go to a depot if a opportunity presents itself.
 */
bool Vehicle::NeedsServicing() const
{
	/* Stopped or crashed vehicles will not move, as such making unmovable
	 * vehicles to go for service is lame. */
	if (this->vehstatus & (VS_STOPPED | VS_CRASHED)) return false;

	/* Are we ready for the next service cycle? */
	const Company *c = Company::Get(this->owner);
	if ((this->ServiceIntervalIsPercent() ?
			(this->reliability >= this->GetEngine()->reliability * (100 - this->service_interval) / 100) :
			(this->date_of_last_service + this->service_interval >= _date))
			&& !(this->type == VEH_TRAIN && HasBit(Train::From(this)->flags, VRF_NEED_REPAIR))
			&& !(this->type == VEH_ROAD && RoadVehicle::From(this)->critical_breakdown_count > 0)) {
		return false;
	}

	/* If we're servicing anyway, because we have not disabled servicing when
	 * there are no breakdowns or we are playing with breakdowns, bail out. */
	if (!_settings_game.order.no_servicing_if_no_breakdowns ||
			_settings_game.difficulty.vehicle_breakdowns != 0) {
		return true;
	}

	/* Is vehicle old and renewing is enabled */
	if (this->NeedsAutorenewing(c, true)) {
		return true;
	}

	/* Test whether there is some pending autoreplace.
	 * Note: We do this after the service-interval test.
	 * There are a lot more reasons for autoreplace to fail than we can test here reasonably. */
	bool pending_replace = false;
	Money needed_money = c->settings.engine_renew_money;
	if (needed_money > c->money) return false;

	for (const Vehicle *v = this; v != NULL; v = (v->type == VEH_TRAIN) ? Train::From(v)->GetNextUnit() : NULL) {
		bool replace_when_old = false;
		EngineID new_engine = EngineReplacementForCompany(c, v->engine_type, v->group_id, &replace_when_old);

		/* Check engine availability */
		if (new_engine == INVALID_ENGINE || !HasBit(Engine::Get(new_engine)->company_avail, v->owner)) continue;
		/* Is the vehicle old if we are not always replacing? */
		if (replace_when_old && !v->NeedsAutorenewing(c, false)) continue;

		/* Check refittability */
		CargoTypes available_cargo_types, union_mask;
		GetArticulatedRefitMasks(new_engine, true, &union_mask, &available_cargo_types);
		/* Is there anything to refit? */
		if (union_mask != 0) {
			CargoID cargo_type;
			/* We cannot refit to mixed cargoes in an automated way */
			if (IsArticulatedVehicleCarryingDifferentCargoes(v, &cargo_type)) continue;

			/* Did the old vehicle carry anything? */
			if (cargo_type != CT_INVALID) {
				/* We can't refit the vehicle to carry the cargo we want */
				if (!HasBit(available_cargo_types, cargo_type)) continue;
			}
		}

		/* Check money.
		 * We want 2*(the price of the new vehicle) without looking at the value of the vehicle we are going to sell. */
		pending_replace = true;
		needed_money += 2 * Engine::Get(new_engine)->GetCost();
		if (needed_money > c->money) return false;
	}

	return pending_replace;
}

/**
 * Checks if the current order should be interrupted for a service-in-depot order.
 * @see NeedsServicing()
 * @return true if the current order should be interrupted.
 */
bool Vehicle::NeedsAutomaticServicing() const
{
	if (this->HasDepotOrder()) return false;
	if (this->current_order.IsType(OT_LOADING)) return false;
	if (this->current_order.IsType(OT_LOADING_ADVANCE)) return false;
	if (this->current_order.IsType(OT_GOTO_DEPOT) && this->current_order.GetDepotOrderType() != ODTFB_SERVICE) return false;
	return NeedsServicing();
}

uint Vehicle::Crash(bool flooded)
{
	assert((this->vehstatus & VS_CRASHED) == 0);
	assert(this->Previous() == NULL); // IsPrimaryVehicle fails for free-wagon-chains

	uint pass = 0;
	/* Stop the vehicle. */
	if (this->IsPrimaryVehicle()) this->vehstatus |= VS_STOPPED;
	/* crash all wagons, and count passengers */
	for (Vehicle *v = this; v != NULL; v = v->Next()) {
		/* We do not transfer reserver cargo back, so TotalCount() instead of StoredCount() */
		if (IsCargoInClass(v->cargo_type, CC_PASSENGERS)) pass += v->cargo.TotalCount();
		v->vehstatus |= VS_CRASHED;
		v->MarkAllViewportsDirty();
	}

	this->ClearSeparation();
	if (HasBit(this->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(this->vehicle_flags, VF_TIMETABLE_STARTED);

	/* Dirty some windows */
	InvalidateWindowClassesData(GetWindowClassForVehicleType(this->type), 0);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	SetWindowDirty(WC_VEHICLE_DEPOT, this->tile);

	delete this->cargo_payment;
	assert(this->cargo_payment == NULL); // cleared by ~CargoPayment

	return RandomRange(pass + 1); // Randomise deceased passengers.
}

/**
 * Get whether a the vehicle should be drawn (i.e. if it isn't hidden, or it is in a tunnel but being shown transparently)
 * @return whether to show vehicle
 */
bool Vehicle::IsDrawn() const
{
	return !(HasBit(this->subtype, GVSF_VIRTUAL)) && (!(this->vehstatus & VS_HIDDEN) ||
			(IsTransparencySet(TO_TUNNELS) &&
				((this->type == VEH_TRAIN && Train::From(this)->track == TRACK_BIT_WORMHOLE) ||
				(this->type == VEH_ROAD && RoadVehicle::From(this)->state == RVSB_WORMHOLE))));
}

/**
 * Displays a "NewGrf Bug" error message for a engine, and pauses the game if not networking.
 * @param engine The engine that caused the problem
 * @param part1  Part 1 of the error message, taking the grfname as parameter 1
 * @param part2  Part 2 of the error message, taking the engine as parameter 2
 * @param bug_type Flag to check and set in grfconfig
 * @param critical Shall the "OpenTTD might crash"-message be shown when the player tries to unpause?
 */
void ShowNewGrfVehicleError(EngineID engine, StringID part1, StringID part2, GRFBugs bug_type, bool critical)
{
	const Engine *e = Engine::Get(engine);
	GRFConfig *grfconfig = GetGRFConfig(e->GetGRFID());

	/* Missing GRF. Nothing useful can be done in this situation. */
	if (grfconfig == NULL) return;

	if (!HasBit(grfconfig->grf_bugs, bug_type)) {
		SetBit(grfconfig->grf_bugs, bug_type);
		SetDParamStr(0, grfconfig->GetName());
		SetDParam(1, engine);
		ShowErrorMessage(part1, part2, WL_CRITICAL);
		if (!_networking) DoCommand(0, critical ? PM_PAUSED_ERROR : PM_PAUSED_NORMAL, 1, DC_EXEC, CMD_PAUSE);
	}

	/* debug output */
	char buffer[512];

	SetDParamStr(0, grfconfig->GetName());
	GetString(buffer, part1, lastof(buffer));
	DEBUG(grf, 0, "%s", buffer + 3);

	SetDParam(1, engine);
	GetString(buffer, part2, lastof(buffer));
	DEBUG(grf, 0, "%s", buffer + 3);
}

/**
 * Logs a bug in GRF and shows a warning message if this
 * is for the first time this happened.
 * @param u first vehicle of chain
 */
void VehicleLengthChanged(const Vehicle *u)
{
	/* show a warning once for each engine in whole game and once for each GRF after each game load */
	const Engine *engine = u->GetEngine();
	uint32 grfid = engine->grf_prop.grffile->grfid;
	GRFConfig *grfconfig = GetGRFConfig(grfid);
	if (GamelogGRFBugReverse(grfid, engine->grf_prop.local_id) || !HasBit(grfconfig->grf_bugs, GBUG_VEH_LENGTH)) {
		ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_VEHICLE_LENGTH, GBUG_VEH_LENGTH, true);
	}
}

/**
 * Vehicle constructor.
 * @param type Type of the new vehicle.
 */
Vehicle::Vehicle(VehicleType type)
{
	this->type               = type;
	this->coord.left         = INVALID_COORD;
	this->group_id           = DEFAULT_GROUP;
	this->fill_percent_te_id = INVALID_TE_ID;
	this->first              = this;
	this->colourmap          = PAL_NONE;
	this->cargo_age_counter  = 1;
	this->last_station_visited = INVALID_STATION;
	this->last_loading_station = INVALID_STATION;
	this->cur_image_valid_dir  = INVALID_DIR;
}

/**
 * Get a value for a vehicle's random_bits.
 * @return A random value from 0 to 255.
 */
byte VehicleRandomBits()
{
	return GB(Random(), 0, 8);
}

/* Size of the hash, 6 = 64 x 64, 7 = 128 x 128. Larger sizes will (in theory) reduce hash
 * lookup times at the expense of memory usage. */
const int HASH_BITS = 7;
const int HASH_SIZE = 1 << HASH_BITS;
const int HASH_MASK = HASH_SIZE - 1;
const int TOTAL_HASH_SIZE = 1 << (HASH_BITS * 2);
const int TOTAL_HASH_MASK = TOTAL_HASH_SIZE - 1;

/* Resolution of the hash, 0 = 1*1 tile, 1 = 2*2 tiles, 2 = 4*4 tiles, etc.
 * Profiling results show that 0 is fastest. */
const int HASH_RES = 0;

static Vehicle *_vehicle_tile_hash[TOTAL_HASH_SIZE];

static Vehicle *VehicleFromTileHash(int xl, int yl, int xu, int yu, void *data, VehicleFromPosProc *proc, bool find_first)
{
	for (int y = yl; ; y = (y + (1 << HASH_BITS)) & (HASH_MASK << HASH_BITS)) {
		for (int x = xl; ; x = (x + 1) & HASH_MASK) {
			Vehicle *v = _vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
			for (; v != NULL; v = v->hash_tile_next) {
				Vehicle *a = proc(v, data);
				if (find_first && a != NULL) return a;
			}
			if (x == xu) break;
		}
		if (y == yu) break;
	}

	return NULL;
}


/**
 * Helper function for FindVehicleOnPos/HasVehicleOnPos.
 * @note Do not call this function directly!
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 * @param find_first Whether to return on the first found or iterate over
 *                   all vehicles
 * @return the best matching or first vehicle (depending on find_first).
 */
static Vehicle *VehicleFromPosXY(int x, int y, void *data, VehicleFromPosProc *proc, bool find_first)
{
	const int COLL_DIST = 6;

	/* Hash area to scan is from xl,yl to xu,yu */
	int xl = GB((x - COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS);
	int xu = GB((x + COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS);
	int yl = GB((y - COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS) << HASH_BITS;
	int yu = GB((y + COLL_DIST) / TILE_SIZE, HASH_RES, HASH_BITS) << HASH_BITS;

	return VehicleFromTileHash(xl, yl, xu, yu, data, proc, find_first);
}

/**
 * Find a vehicle from a specific location. It will call proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of proc will be ignored.
 * @note Use this when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 */
void FindVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc)
{
	VehicleFromPosXY(x, y, data, proc, false);
}

/**
 * Checks whether a vehicle in on a specific location. It will call proc for
 * vehicles until it returns non-NULL.
 * @note Use FindVehicleOnPosXY when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-NULL.
 */
bool HasVehicleOnPosXY(int x, int y, void *data, VehicleFromPosProc *proc)
{
	return VehicleFromPosXY(x, y, data, proc, true) != NULL;
}

/**
 * Helper function for FindVehicleOnPos/HasVehicleOnPos.
 * @note Do not call this function directly!
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The proc that determines whether a vehicle will be "found".
 * @param find_first Whether to return on the first found or iterate over
 *                   all vehicles
 * @return the best matching or first vehicle (depending on find_first).
 */
static Vehicle *VehicleFromPos(TileIndex tile, void *data, VehicleFromPosProc *proc, bool find_first)
{
	int x = GB(TileX(tile), HASH_RES, HASH_BITS);
	int y = GB(TileY(tile), HASH_RES, HASH_BITS) << HASH_BITS;

	Vehicle *v = _vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
	for (; v != NULL; v = v->hash_tile_next) {
		if (v->tile != tile) continue;

		Vehicle *a = proc(v, data);
		if (find_first && a != NULL) return a;
	}

	return NULL;
}

/**
 * Find a vehicle from a specific location. It will call \a proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of \a proc will be ignored.
 * @note Use this function when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The proc that determines whether a vehicle will be "found".
 */
void FindVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc)
{
	VehicleFromPos(tile, data, proc, false);
}

/**
 * Checks whether a vehicle is on a specific location. It will call \a proc for
 * vehicles until it returns non-NULL.
 * @note Use #FindVehicleOnPos when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The \a proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-NULL.
 */
bool HasVehicleOnPos(TileIndex tile, void *data, VehicleFromPosProc *proc)
{
	return VehicleFromPos(tile, data, proc, true) != NULL;
}

/**
 * Callback that returns 'real' vehicles lower or at height \c *(int*)data .
 * @param v Vehicle to examine.
 * @param data Pointer to height data.
 * @return \a v if conditions are met, else \c NULL.
 */
static Vehicle *EnsureNoVehicleProcZ(Vehicle *v, void *data)
{
	int z = *(int*)data;

	if (v->type == VEH_DISASTER || (v->type == VEH_AIRCRAFT && v->subtype == AIR_SHADOW)) return NULL;
	if (v->z_pos > z) return NULL;

	return v;
}

/**
 * Ensure there is no vehicle at the ground at the given position.
 * @param tile Position to examine.
 * @return Succeeded command (ground is free) or failed command (a vehicle is found).
 */
CommandCost EnsureNoVehicleOnGround(TileIndex tile)
{
	int z = GetTileMaxPixelZ(tile);

	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, &z, &EnsureNoVehicleProcZ, true);
	if (v != NULL) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

/**
 * Callback that returns 'real' vehicles lower or at height \c *(int*)data, for road vehicles.
 * @param v Vehicle to examine.
 * @param data Pointer to height data.
 * @return \a v if conditions are met, else \c NULL.
 */
static Vehicle *EnsureNoRoadVehicleProcZ(Vehicle *v, void *data)
{
	int z = *(int*)data;

	if (v->type != VEH_ROAD) return NULL;
	if (v->z_pos > z) return NULL;

	return v;
}

/**
 * Ensure there is no road vehicle at the ground at the given position.
 * @param tile Position to examine.
 * @return Succeeded command (ground is free) or failed command (a vehicle is found).
 */
CommandCost EnsureNoRoadVehicleOnGround(TileIndex tile)
{
	int z = GetTileMaxPixelZ(tile);

	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, &z, &EnsureNoRoadVehicleProcZ, true);
	if (v != NULL) return_cmd_error(STR_ERROR_ROAD_VEHICLE_IN_THE_WAY);
	return CommandCost();
}

struct GetVehicleTunnelBridgeProcData {
	const Vehicle *v;
	TileIndex t;
	bool across_only;
};

/** Procedure called for every vehicle found in tunnel/bridge in the hash map */
static Vehicle *GetVehicleTunnelBridgeProc(Vehicle *v, void *data)
{
	const GetVehicleTunnelBridgeProcData *info = (GetVehicleTunnelBridgeProcData*) data;
	if (v->type != VEH_TRAIN && v->type != VEH_ROAD && v->type != VEH_SHIP) return NULL;
	if (v == info->v) return NULL;

	if (v->type == VEH_TRAIN && info->across_only && IsBridge(info->t)) {
		TrackBits vehicle_track = Train::From(v)->track;
		if (!(vehicle_track & TRACK_BIT_WORMHOLE) && !(GetAcrossBridgePossibleTrackBits(info->t) & vehicle_track)) return NULL;
	}

	return v;
}

/**
 * Finds vehicle in tunnel / bridge
 * @param tile first end
 * @param endtile second end
 * @param ignore Ignore this vehicle when searching
 * @param across_only Only find vehicles which are passing across the bridge/tunnel or on connecting bridge head track pieces
 * @return Succeeded command (if tunnel/bridge is free) or failed command (if a vehicle is using the tunnel/bridge).
 */
CommandCost TunnelBridgeIsFree(TileIndex tile, TileIndex endtile, const Vehicle *ignore, bool across_only)
{
	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	GetVehicleTunnelBridgeProcData data;
	data.v = ignore;
	data.t = tile;
	data.across_only = across_only;
	Vehicle *v = VehicleFromPos(tile, &data, &GetVehicleTunnelBridgeProc, true);
	if (v == NULL) {
		data.t = endtile;
		v = VehicleFromPos(endtile, &data, &GetVehicleTunnelBridgeProc, true);
	}

	if (v != NULL) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

static Vehicle *EnsureNoTrainOnTrackProc(Vehicle *v, void *data)
{
	TrackBits rail_bits = *(TrackBits *)data;

	if (v->type != VEH_TRAIN) return NULL;

	Train *t = Train::From(v);
	if (rail_bits & TRACK_BIT_WORMHOLE) {
		if (t->track & TRACK_BIT_WORMHOLE) return v;
		rail_bits &= ~TRACK_BIT_WORMHOLE;
	} else if (t->track & TRACK_BIT_WORMHOLE) {
		return NULL;
	}
	if ((t->track != rail_bits) && !TracksOverlap(t->track | rail_bits)) return NULL;

	return v;
}

/**
 * Tests if a vehicle interacts with the specified track bits.
 * All track bits interact except parallel #TRACK_BIT_HORZ or #TRACK_BIT_VERT.
 *
 * @param tile The tile.
 * @param track_bits The track bits.
 * @return \c true if no train that interacts, is found. \c false if a train is found.
 */
CommandCost EnsureNoTrainOnTrackBits(TileIndex tile, TrackBits track_bits)
{
	/* Value v is not safe in MP games, however, it is used to generate a local
	 * error message only (which may be different for different machines).
	 * Such a message does not affect MP synchronisation.
	 */
	Vehicle *v = VehicleFromPos(tile, &track_bits, &EnsureNoTrainOnTrackProc, true);
	if (v != NULL) return_cmd_error(STR_ERROR_TRAIN_IN_THE_WAY + v->type);
	return CommandCost();
}

static void UpdateVehicleTileHash(Vehicle *v, bool remove)
{
	Vehicle **old_hash = v->hash_tile_current;
	Vehicle **new_hash;

	if (remove) {
		new_hash = NULL;
	} else {
		int x = GB(TileX(v->tile), HASH_RES, HASH_BITS);
		int y = GB(TileY(v->tile), HASH_RES, HASH_BITS) << HASH_BITS;
		new_hash = &_vehicle_tile_hash[(x + y) & TOTAL_HASH_MASK];
	}

	if (old_hash == new_hash) return;

	/* Remove from the old position in the hash table */
	if (old_hash != NULL) {
		if (v->hash_tile_next != NULL) v->hash_tile_next->hash_tile_prev = v->hash_tile_prev;
		*v->hash_tile_prev = v->hash_tile_next;
	}

	/* Insert vehicle at beginning of the new position in the hash table */
	if (new_hash != NULL) {
		v->hash_tile_next = *new_hash;
		if (v->hash_tile_next != NULL) v->hash_tile_next->hash_tile_prev = &v->hash_tile_next;
		v->hash_tile_prev = new_hash;
		*new_hash = v;
	}

	/* Remember current hash position */
	v->hash_tile_current = new_hash;
}

static Vehicle *_vehicle_viewport_hash[1 << (GEN_HASHX_BITS + GEN_HASHY_BITS)];

static void UpdateVehicleViewportHash(Vehicle *v, int x, int y)
{
	Vehicle **old_hash, **new_hash;
	int old_x = v->coord.left;
	int old_y = v->coord.top;

	new_hash = (x == INVALID_COORD) ? NULL : &_vehicle_viewport_hash[GEN_HASH(x, y)];
	old_hash = (old_x == INVALID_COORD) ? NULL : &_vehicle_viewport_hash[GEN_HASH(old_x, old_y)];

	if (old_hash == new_hash) return;

	/* remove from hash table? */
	if (old_hash != NULL) {
		if (v->hash_viewport_next != NULL) v->hash_viewport_next->hash_viewport_prev = v->hash_viewport_prev;
		*v->hash_viewport_prev = v->hash_viewport_next;
	}

	/* insert into hash table? */
	if (new_hash != NULL) {
		v->hash_viewport_next = *new_hash;
		if (v->hash_viewport_next != NULL) v->hash_viewport_next->hash_viewport_prev = &v->hash_viewport_next;
		v->hash_viewport_prev = new_hash;
		*new_hash = v;
	}
}

void ResetVehicleHash()
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v) { v->hash_tile_current = NULL; }
	memset(_vehicle_viewport_hash, 0, sizeof(_vehicle_viewport_hash));
	memset(_vehicle_tile_hash, 0, sizeof(_vehicle_tile_hash));
}

void ResetVehicleColourMap()
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v) { v->colourmap = PAL_NONE; }
}

/**
 * List of vehicles that should check for autoreplace this tick.
 * Mapping of vehicle -> leave depot immediately after autoreplace.
 */
typedef SmallMap<Vehicle *, bool, 4> AutoreplaceMap;
static AutoreplaceMap _vehicles_to_autoreplace;

/**
 * List of vehicles that are issued for template replacement this tick.
 * Mapping is {vehicle : leave depot after replacement}
 */
typedef SmallMap<Train *, bool, 4> TemplateReplacementMap;
static TemplateReplacementMap _vehicles_to_templatereplace;

void InitializeVehicles()
{
	_vehicles_to_autoreplace.Reset();
	ResetVehicleHash();
}

uint CountVehiclesInChain(const Vehicle *v)
{
	uint count = 0;
	do count++; while ((v = v->Next()) != NULL);
	return count;
}

/**
 * Check if a vehicle is counted in num_engines in each company struct
 * @return true if the vehicle is counted in num_engines
 */
bool Vehicle::IsEngineCountable() const
{
	if (HasBit(this->subtype, GVSF_VIRTUAL)) return false;
	switch (this->type) {
		case VEH_AIRCRAFT: return Aircraft::From(this)->IsNormalAircraft(); // don't count plane shadows and helicopter rotors
		case VEH_TRAIN:
			return !this->IsArticulatedPart() && // tenders and other articulated parts
					!Train::From(this)->IsRearDualheaded(); // rear parts of multiheaded engines
		case VEH_ROAD: return RoadVehicle::From(this)->IsFrontEngine();
		case VEH_SHIP: return true;
		default: return false; // Only count company buildable vehicles
	}
}

/**
 * Check whether Vehicle::engine_type has any meaning.
 * @return true if the vehicle has a useable engine type.
 */
bool Vehicle::HasEngineType() const
{
	switch (this->type) {
		case VEH_AIRCRAFT: return Aircraft::From(this)->IsNormalAircraft();
		case VEH_TRAIN:
		case VEH_ROAD:
		case VEH_SHIP: return true;
		default: return false;
	}
}

/**
 * Retrieves the engine of the vehicle.
 * @return Engine of the vehicle.
 * @pre HasEngineType() == true
 */
const Engine *Vehicle::GetEngine() const
{
	return Engine::Get(this->engine_type);
}

/**
 * Retrieve the NewGRF the vehicle is tied to.
 * This is the GRF providing the Action 3 for the engine type.
 * @return NewGRF associated to the vehicle.
 */
const GRFFile *Vehicle::GetGRF() const
{
	return this->GetEngine()->GetGRF();
}

/**
 * Retrieve the GRF ID of the NewGRF the vehicle is tied to.
 * This is the GRF providing the Action 3 for the engine type.
 * @return GRF ID of the associated NewGRF.
 */
uint32 Vehicle::GetGRFID() const
{
	return this->GetEngine()->GetGRFID();
}

/**
 * Handle the pathfinding result, especially the lost status.
 * If the vehicle is now lost and wasn't previously fire an
 * event to the AIs and a news message to the user. If the
 * vehicle is not lost anymore remove the news message.
 * @param path_found Whether the vehicle has a path to its destination.
 */
void Vehicle::HandlePathfindingResult(bool path_found)
{
	if (path_found) {
		/* Route found, is the vehicle marked with "lost" flag? */
		if (!HasBit(this->vehicle_flags, VF_PATHFINDER_LOST)) return;

		/* Clear the flag as the PF's problem was solved. */
		ClrBit(this->vehicle_flags, VF_PATHFINDER_LOST);
		/* Delete the news item. */
		DeleteVehicleNews(this->index, STR_NEWS_VEHICLE_IS_LOST);
		return;
	}

	/* Were we already lost? */
	if (HasBit(this->vehicle_flags, VF_PATHFINDER_LOST)) return;

	/* It is first time the problem occurred, set the "lost" flag. */
	SetBit(this->vehicle_flags, VF_PATHFINDER_LOST);
	/* Notify user about the event. */
	AI::NewEvent(this->owner, new ScriptEventVehicleLost(this->index));
	if (_settings_client.gui.lost_vehicle_warn && this->owner == _local_company) {
		SetDParam(0, this->index);
		AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_IS_LOST, this->index);
	}
}

/** Destroy all stuff that (still) needs the virtual functions to work properly */
void Vehicle::PreDestructor()
{
	if (CleaningPool()) return;

	SCOPE_INFO_FMT([this], "Vehicle::PreDestructor: %s", scope_dumper().VehicleInfo(this));

	if (Station::IsValidID(this->last_station_visited)) {
		Station *st = Station::Get(this->last_station_visited);
		st->loading_vehicles.erase(std::remove(st->loading_vehicles.begin(), st->loading_vehicles.end(), this), st->loading_vehicles.end());

		HideFillingPercent(&this->fill_percent_te_id);
		this->CancelReservation(INVALID_STATION, st);
		delete this->cargo_payment;
		assert(this->cargo_payment == NULL); // cleared by ~CargoPayment
	}

	if (this->IsEngineCountable()) {
		GroupStatistics::CountEngine(this, -1);
		if (this->IsPrimaryVehicle()) GroupStatistics::CountVehicle(this, -1);
		GroupStatistics::UpdateAutoreplace(this->owner);

		if (this->owner == _local_company) InvalidateAutoreplaceWindow(this->engine_type, this->group_id);
		DeleteGroupHighlightOfVehicle(this);
		if (this->type == VEH_TRAIN) {
			extern void DeleteTraceRestrictSlotHighlightOfVehicle(const Vehicle *v);

			DeleteTraceRestrictSlotHighlightOfVehicle(this);
		}
	}

	if (this->type == VEH_AIRCRAFT && this->IsPrimaryVehicle()) {
		Aircraft *a = Aircraft::From(this);
		Station *st = GetTargetAirportIfValid(a);
		if (st != NULL) {
			const AirportFTA *layout = st->airport.GetFTA()->layout;
			CLRBITS(st->airport.flags, layout[a->previous_pos].block | layout[a->pos].block);
		}
	}


	if (this->type == VEH_ROAD && this->IsPrimaryVehicle()) {
		RoadVehicle *v = RoadVehicle::From(this);
		if (!(v->vehstatus & VS_CRASHED) && IsInsideMM(v->state, RVSB_IN_DT_ROAD_STOP, RVSB_IN_DT_ROAD_STOP_END)) {
			/* Leave the drive through roadstop, when you have not already left it. */
			RoadStop::GetByTile(v->tile, GetRoadStopType(v->tile))->Leave(v);
		}
	}

	if (this->type == VEH_TRAIN && HasBit(Train::From(this)->flags, VRF_HAVE_SLOT)) {
		TraceRestrictRemoveVehicleFromAllSlots(this->index);
		ClrBit(Train::From(this)->flags, VRF_HAVE_SLOT);
	}

	if (this->Previous() == NULL) {
		InvalidateWindowData(WC_VEHICLE_DEPOT, this->tile);
	}

	if (this->IsPrimaryVehicle()) {
		DeleteWindowById(WC_VEHICLE_VIEW, this->index);
		DeleteWindowById(WC_VEHICLE_ORDERS, this->index);
		DeleteWindowById(WC_VEHICLE_REFIT, this->index);
		DeleteWindowById(WC_VEHICLE_DETAILS, this->index);
		DeleteWindowById(WC_VEHICLE_TIMETABLE, this->index);
		DeleteWindowById(WC_SCHDISPATCH_SLOTS, this->index);
		DeleteWindowById(WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, this->index);
		DeleteWindowById(WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, this->index);
		SetWindowDirty(WC_COMPANY, this->owner);
		OrderBackup::ClearVehicle(this);
	}
	InvalidateWindowClassesData(GetWindowClassForVehicleType(this->type), 0);

	this->cargo.Truncate();
	DeleteVehicleOrders(this);
	DeleteDepotHighlightOfVehicle(this);

	extern void StopGlobalFollowVehicle(const Vehicle *v);
	StopGlobalFollowVehicle(this);

	ReleaseDisastersTargetingVehicle(this->index);

	/* sometimes, eg. for disaster vehicles, when company bankrupts, when removing crashed/flooded vehicles,
	 * it may happen that vehicle chain is deleted when visible */
	if (this->IsDrawn()) this->MarkAllViewportsDirty();
}

Vehicle::~Vehicle()
{
	if (CleaningPool()) {
		this->cargo.OnCleanPool();
		return;
	}

	if (this->breakdowns_since_last_service) _vehicles_to_pay_repair.erase(this);

	if (this->type < VEH_BEGIN || this->type >= VEH_COMPANY_END) {
		/* sometimes, eg. for disaster vehicles, when company bankrupts, when removing crashed/flooded vehicles,
		 * it may happen that vehicle chain is deleted when visible.
		 * Do not redo this for vehicle types where it is done in PreDestructor(). */
		if (this->IsDrawn()) this->MarkAllViewportsDirty();
	}

	Vehicle *v = this->Next();
	this->SetNext(NULL);

	delete v;

	UpdateVehicleTileHash(this, true);
	UpdateVehicleViewportHash(this, INVALID_COORD, 0);
	DeleteVehicleNews(this->index, INVALID_STRING_ID);
	DeleteNewGRFInspectWindow(GetGrfSpecFeature(this->type), this->index);
}

/**
 * Adds a vehicle to the list of vehicles that visited a depot this tick
 * @param *v vehicle to add
 */
void VehicleEnteredDepotThisTick(Vehicle *v)
{
	/* Template Replacement Setup stuff */
	bool stayInDepot = v->current_order.GetDepotActionType();
	TemplateReplacement *tr = GetTemplateReplacementByGroupID(v->group_id);
	if (tr != NULL) {
		_vehicles_to_templatereplace[(Train*) v] = stayInDepot;
	} else {
		/* Moved the assignment for auto replacement here to prevent auto replacement
		 * from happening if template replacement is also scheduled */

		/* Vehicle should stop in the depot if it was in 'stopping' state */
		_vehicles_to_autoreplace[v] = !(v->vehstatus & VS_STOPPED);
	}

	/* We ALWAYS set the stopped state. Even when the vehicle does not plan on
	 * stopping in the depot, so we stop it to ensure that it will not reserve
	 * the path out of the depot before we might autoreplace it to a different
	 * engine. The new engine would not own the reserved path we store that we
	 * stopped the vehicle, so autoreplace can start it again */
	v->vehstatus |= VS_STOPPED;
}

/**
 * Increases the day counter for all vehicles and calls 1-day and 32-day handlers.
 * Each tick, it processes vehicles with "index % DAY_TICKS == _date_fract",
 * so each day, all vehicles are processes in DAY_TICKS steps.
 */
static void RunVehicleDayProc()
{
	if (_game_mode != GM_NORMAL) return;

	/* Run the day_proc for every DAY_TICKS vehicle starting at _date_fract. */
	Vehicle *v = NULL;
	SCOPE_INFO_FMT([&v], "RunVehicleDayProc: %s", scope_dumper().VehicleInfo(v));
	for (size_t i = _date_fract; i < Vehicle::GetPoolSize(); i += DAY_TICKS) {
		v = Vehicle::Get(i);
		if (v == NULL) continue;

		/* Call the 32-day callback if needed */
		if ((v->day_counter & 0x1F) == 0 && v->HasEngineType()) {
			uint16 callback = GetVehicleCallback(CBID_VEHICLE_32DAY_CALLBACK, 0, 0, v->engine_type, v);
			if (callback != CALLBACK_FAILED) {
				if (HasBit(callback, 0)) {
					TriggerVehicle(v, VEHICLE_TRIGGER_CALLBACK_32); // Trigger vehicle trigger 10
				}

				/* After a vehicle trigger, the graphics and properties of the vehicle could change.
				 * Note: MarkDirty also invalidates the palette, which is the meaning of bit 1. So, nothing special there. */
				if (callback != 0) v->First()->MarkDirty();

				if (callback & ~3) ErrorUnknownCallbackResult(v->GetGRFID(), CBID_VEHICLE_32DAY_CALLBACK, callback);
			}
		}

		/* This is called once per day for each vehicle, but not in the first tick of the day */
		v->OnNewDay();
	}
}

static void ShowAutoReplaceAdviceMessage(const CommandCost &res, const Vehicle *v)
{
	StringID error_message = res.GetErrorMessage();
	if (error_message == STR_ERROR_AUTOREPLACE_NOTHING_TO_DO || error_message == INVALID_STRING_ID) return;

	if (error_message == STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY) error_message = STR_ERROR_AUTOREPLACE_MONEY_LIMIT;

	StringID message;
	if (error_message == STR_ERROR_TRAIN_TOO_LONG_AFTER_REPLACEMENT) {
		message = error_message;
	} else {
		message = STR_NEWS_VEHICLE_AUTORENEW_FAILED;
	}

	SetDParam(0, v->index);
	SetDParam(1, error_message);
	AddVehicleAdviceNewsItem(message, v->index);
}

void CallVehicleTicks()
{
	_vehicles_to_autoreplace.Clear();
	_vehicles_to_templatereplace.Clear();
	_vehicles_to_pay_repair.clear();
	_vehicles_to_sell.clear();

	if (_tick_skip_counter == 0) RunVehicleDayProc();

	{
		PerformanceMeasurer framerate(PFE_GL_ECONOMY);
		Station *st = nullptr;
		SCOPE_INFO_FMT([&st], "CallVehicleTicks: LoadUnloadStation: %s", scope_dumper().StationInfo(st));
		FOR_ALL_STATIONS(st) LoadUnloadStation(st);
	}
	PerformanceAccumulator::Reset(PFE_GL_TRAINS);
	PerformanceAccumulator::Reset(PFE_GL_ROADVEHS);
	PerformanceAccumulator::Reset(PFE_GL_SHIPS);
	PerformanceAccumulator::Reset(PFE_GL_AIRCRAFT);

	Vehicle *v = NULL;
	SCOPE_INFO_FMT([&v], "CallVehicleTicks: %s", scope_dumper().VehicleInfo(v));
	FOR_ALL_VEHICLES(v) {
		/* Vehicle could be deleted in this tick */
		if (!v->Tick()) {
			assert(Vehicle::Get(vehicle_index) == NULL);
			continue;
		}

		assert(Vehicle::Get(vehicle_index) == v);

		switch (v->type) {
			default: break;

			case VEH_TRAIN:
				if (HasBit(Train::From(v)->flags, VRF_TOO_HEAVY)) {
					if (v->owner == _local_company) {
						SetDParam(0, v->index);
						SetDParam(1, STR_ERROR_TRAIN_TOO_HEAVY);
						AddVehicleNewsItem(STR_ERROR_TRAIN_TOO_HEAVY, NT_ADVICE, v->index);
					}
					ClrBit(Train::From(v)->flags, VRF_TOO_HEAVY);
				}
				/* FALL THROUGH */
			case VEH_ROAD:
			case VEH_AIRCRAFT:
			case VEH_SHIP: {
				Vehicle *front = v->First();

				if (v->vcache.cached_cargo_age_period != 0) {
					v->cargo_age_counter = min(v->cargo_age_counter, v->vcache.cached_cargo_age_period);
					if (--v->cargo_age_counter == 0) {
						v->cargo.AgeCargo();
						v->cargo_age_counter = v->vcache.cached_cargo_age_period;
					}
				}

				/* Do not play any sound when crashed */
				if (front->vehstatus & VS_CRASHED) continue;

				/* Do not play any sound when in depot or tunnel */
				if (v->vehstatus & VS_HIDDEN) continue;

				/* Do not play any sound when stopped */
				if ((front->vehstatus & VS_STOPPED) && (front->type != VEH_TRAIN || front->cur_speed == 0)) continue;

				/* Check vehicle type specifics */
				switch (v->type) {
					case VEH_TRAIN:
						if (Train::From(v)->IsWagon()) continue;
						break;

					case VEH_ROAD:
						if (!RoadVehicle::From(v)->IsFrontEngine()) continue;
						break;

					case VEH_AIRCRAFT:
						if (!Aircraft::From(v)->IsNormalAircraft()) continue;
						break;

					default:
						break;
				}

				v->motion_counter += front->cur_speed;
				/* Play a running sound if the motion counter passes 256 (Do we not skip sounds?) */
				if (GB(v->motion_counter, 0, 8) < front->cur_speed) PlayVehicleSound(v, VSE_RUNNING);

				/* Play an alternating running sound every 16 ticks */
				if (GB(v->tick_counter, 0, 4) == 0) {
					/* Play running sound when speed > 0 and not braking */
					bool running = (front->cur_speed > 0) && !(front->vehstatus & (VS_STOPPED | VS_TRAIN_SLOWING));
					PlayVehicleSound(v, running ? VSE_RUNNING_16 : VSE_STOPPED_16);
				}

				break;
			}
		}
	}
	v = NULL;

	/* do Template Replacement */
	Backup<CompanyByte> sell_cur_company(_current_company, FILE_LINE);
	for (Vehicle *v : _vehicles_to_sell) {
		SCOPE_INFO_FMT([v], "CallVehicleTicks: sell: %s", scope_dumper().VehicleInfo(v));
		Train *t = (v->type == VEH_TRAIN) ? Train::From(v) : nullptr;

		sell_cur_company.Change(v->owner);

		int x = v->x_pos;
		int y = v->y_pos;
		int z = v->z_pos;

		CommandCost cost = DoCommand(v->tile, v->index | (1 << 20), 0, DC_EXEC, GetCmdSellVeh(v));

		if (!cost.Succeeded()) continue;

		if (IsLocalCompany() && cost.Succeeded()) {
			if (cost.GetCost() != 0) {
				ShowCostOrIncomeAnimation(x, y, z, cost.GetCost());
			}
		}

		_vehicles_to_pay_repair.erase(v);
		if (t) _vehicles_to_templatereplace.Erase(t);
		_vehicles_to_autoreplace.Erase(v);
	}
	sell_cur_company.Restore();

	/* do Template Replacement */
	Backup<CompanyByte> tmpl_cur_company(_current_company, FILE_LINE);
	for (TemplateReplacementMap::iterator it = _vehicles_to_templatereplace.Begin(); it != _vehicles_to_templatereplace.End(); it++) {
		Train *t = it->first;

		SCOPE_INFO_FMT([t], "CallVehicleTicks: template replace: %s", scope_dumper().VehicleInfo(t));

		_vehicles_to_autoreplace.Erase(t);

		/* Store the position of the effect as the vehicle pointer will become invalid later */
		int x = t->x_pos;
		int y = t->y_pos;
		int z = t->z_pos;

		tmpl_cur_company.Change(t->owner);

		bool stayInDepot = it->second;

		it->first->vehstatus |= VS_STOPPED;
		CommandCost res = DoCommand(t->tile, t->index, stayInDepot ? 1 : 0, DC_EXEC, CMD_TEMPLATE_REPLACE_VEHICLE);

		if (res.Succeeded()) {
			VehicleID t_new = _new_vehicle_id;
			t = Train::From(Vehicle::Get(t_new));
			const Company *c = Company::Get(_current_company);
			SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, (Money)c->settings.engine_renew_money));
			CommandCost res2 = DoCommand(0, t_new, 1, DC_EXEC, CMD_AUTOREPLACE_VEHICLE);
			SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, -(Money)c->settings.engine_renew_money));
			if (res2.Succeeded() || res.GetCost() == 0) res.AddCost(res2);
		}

		if (!IsLocalCompany()) continue;

		if (res.Succeeded()) {
			if (res.GetCost() != 0) {
				ShowCostOrIncomeAnimation(x, y, z, res.GetCost());
			}
			continue;
		}

		ShowAutoReplaceAdviceMessage(res, t);
	}
	tmpl_cur_company.Restore();

	/* do Auto Replacement */
	Backup<CompanyByte> cur_company(_current_company, FILE_LINE);
	for (AutoreplaceMap::iterator it = _vehicles_to_autoreplace.Begin(); it != _vehicles_to_autoreplace.End(); it++) {
		v = it->first;
		/* Autoreplace needs the current company set as the vehicle owner */
		cur_company.Change(v->owner);

		if (v->type == VEH_TRAIN) {
			assert(!_vehicles_to_templatereplace.Contains(Train::From(v)));
		}

		/* Start vehicle if we stopped them in VehicleEnteredDepotThisTick()
		 * We need to stop them between VehicleEnteredDepotThisTick() and here or we risk that
		 * they are already leaving the depot again before being replaced. */
		if (it->second) v->vehstatus &= ~VS_STOPPED;

		/* Store the position of the effect as the vehicle pointer will become invalid later */
		int x = v->x_pos;
		int y = v->y_pos;
		int z = v->z_pos;

		const Company *c = Company::Get(_current_company);
		SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, (Money)c->settings.engine_renew_money));
		CommandCost res = DoCommand(0, v->index, 0, DC_EXEC, CMD_AUTOREPLACE_VEHICLE);
		SubtractMoneyFromCompany(CommandCost(EXPENSES_NEW_VEHICLES, -(Money)c->settings.engine_renew_money));

		if (!IsLocalCompany()) continue;

		if (res.Succeeded()) {
			ShowCostOrIncomeAnimation(x, y, z, res.GetCost());
			continue;
		}

		ShowAutoReplaceAdviceMessage(res, v);
	}
	cur_company.Restore();

	Backup<CompanyByte> repair_cur_company(_current_company, FILE_LINE);
	for (Vehicle *v : _vehicles_to_pay_repair) {
		SCOPE_INFO_FMT([v], "CallVehicleTicks: repair: %s", scope_dumper().VehicleInfo(v));

		ExpensesType type = INVALID_EXPENSES;
		_current_company = v->owner;
		switch (v->type) {
			case VEH_AIRCRAFT:
				type = EXPENSES_AIRCRAFT_RUN;
				break;

			case VEH_TRAIN:
				type = EXPENSES_TRAIN_RUN;
				break;

			case VEH_SHIP:
				type = EXPENSES_SHIP_RUN;
				break;

			case VEH_ROAD:
				type = EXPENSES_ROADVEH_RUN;
				break;

			default:
				NOT_REACHED();
		}
		assert(type != INVALID_EXPENSES);

		Money vehicle_new_value = v->GetEngine()->GetCost();

		// The static cast is to fix compilation on (old) MSVC as the overload for OverflowSafeInt operator / is ambiguous.
		Money repair_cost = (v->breakdowns_since_last_service * vehicle_new_value / static_cast<uint>(_settings_game.vehicle.repair_cost)) + 1;
		if (v->age > v->max_age) repair_cost <<= 1;
		CommandCost cost(type, repair_cost);
		v->First()->profit_this_year -= cost.GetCost() << 8;
		SubtractMoneyFromCompany(cost);
		ShowCostOrIncomeAnimation(v->x_pos, v->y_pos, v->z_pos, cost.GetCost());
		v->breakdowns_since_last_service = 0;
	}
	repair_cur_company.Restore();
	_vehicles_to_pay_repair.clear();
}

/**
 * Add vehicle sprite for drawing to the screen.
 * @param v Vehicle to draw.
 */
static void DoDrawVehicle(const Vehicle *v)
{
	PaletteID pal = PAL_NONE;

	if (v->vehstatus & VS_DEFPAL) pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);

	/* Check whether the vehicle shall be transparent due to the game state */
	bool shadowed = (v->vehstatus & (VS_SHADOW | VS_HIDDEN)) != 0;

	if (v->type == VEH_EFFECT) {
		/* Check whether the vehicle shall be transparent/invisible due to GUI settings.
		 * However, transparent smoke and bubbles look weird, so always hide them. */
		TransparencyOption to = EffectVehicle::From(v)->GetTransparencyOption();
		if (to != TO_INVALID && (IsTransparencySet(to) || IsInvisibilitySet(to))) return;
	}

	StartSpriteCombine();
	for (uint i = 0; i < v->sprite_seq.count; ++i) {
		PaletteID pal2 = v->sprite_seq.seq[i].pal;
		if (!pal2 || (v->vehstatus & VS_CRASHED)) pal2 = pal;
		AddSortableSpriteToDraw(v->sprite_seq.seq[i].sprite, pal2, v->x_pos + v->x_offs, v->y_pos + v->y_offs,
			v->x_extent, v->y_extent, v->z_extent, v->z_pos, shadowed, v->x_bb_offs, v->y_bb_offs);
	}
	EndSpriteCombine();
}

struct ViewportHashBound {
	int xl, xu, yl, yu;
};

static ViewportHashBound GetViewportHashBound(int l, int r, int t, int b) {
	int xl = (l - (70 * ZOOM_LVL_BASE)) >> (7 + ZOOM_LVL_SHIFT);
	int xu = (r                       ) >> (7 + ZOOM_LVL_SHIFT);
	/* compare after shifting instead of before, so that lower bits don't affect comparison result */
	if (xu - xl < (1 << 6)) {
		xl &= 0x3F;
		xu &= 0x3F;
	} else {
		/* scan whole hash row */
		xl = 0;
		xu = 0x3F;
	}

	int yl = (t - (70 * ZOOM_LVL_BASE)) >> (6 + ZOOM_LVL_SHIFT);
	int yu = (b                       ) >> (6 + ZOOM_LVL_SHIFT);
	/* compare after shifting instead of before, so that lower bits don't affect comparison result */
	if (yu - yl < (1 << 6)) {
		yl = (yl & 0x3F) << 6;
		yu = (yu & 0x3F) << 6;
	} else {
		/* scan whole column */
		yl = 0;
		yu = 0x3F << 6;
	}
	return { xl, xu, yl, yu };
};

/**
 * Add the vehicle sprites that should be drawn at a part of the screen.
 * @param dpi Rectangle being drawn.
 */
void ViewportAddVehicles(DrawPixelInfo *dpi)
{
	/* The bounding rectangle */
	const int l = dpi->left;
	const int r = dpi->left + dpi->width;
	const int t = dpi->top;
	const int b = dpi->top + dpi->height;

	/* The hash area to scan */
	const ViewportHashBound vhb = GetViewportHashBound(l, r, t, b);

	for (int y = vhb.yl;; y = (y + (1 << 6)) & (0x3F << 6)) {
		for (int x = vhb.xl;; x = (x + 1) & 0x3F) {
			const Vehicle *v = _vehicle_viewport_hash[x + y]; // already masked & 0xFFF

			while (v != NULL) {
				if (v->IsDrawn() &&
						l <= v->coord.right &&
						t <= v->coord.bottom &&
						r >= v->coord.left &&
						b >= v->coord.top) {
					DoDrawVehicle(v);
				}
				v = v->hash_viewport_next;
			}

			if (x == vhb.xu) break;
		}

		if (y == vhb.yu) break;
	}
}

void ViewportMapDrawVehicles(DrawPixelInfo *dpi)
{
	/* The bounding rectangle */
	const int l = dpi->left;
	const int r = dpi->left + dpi->width;
	const int t = dpi->top;
	const int b = dpi->top + dpi->height;

	/* The hash area to scan */
	const ViewportHashBound vhb = GetViewportHashBound(l, r, t, b);

	const int w = UnScaleByZoom(dpi->width, dpi->zoom);
	const int h = UnScaleByZoom(dpi->height, dpi->zoom);
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	for (int y = vhb.yl;; y = (y + (1 << 6)) & (0x3F << 6)) {
		for (int x = vhb.xl;; x = (x + 1) & 0x3F) {
			const Vehicle *v = _vehicle_viewport_hash[x + y]; // already masked & 0xFFF

			while (v != NULL) {
				if (!(v->vehstatus & (VS_HIDDEN | VS_UNCLICKABLE)) && (v->type != VEH_EFFECT)) {
					Point pt = RemapCoords(v->x_pos, v->y_pos, v->z_pos);
					const int pixel_x = UnScaleByZoomLower(pt.x - dpi->left, dpi->zoom);
					if (IsInsideMM(pixel_x, 0, w)) {
						const int pixel_y = UnScaleByZoomLower(pt.y - dpi->top, dpi->zoom);
						if (IsInsideMM(pixel_y, 0, h))
							blitter->SetPixel(dpi->dst_ptr, pixel_x, pixel_y, PC_WHITE);
					}
				}
				v = v->hash_viewport_next;
			}

			if (x == vhb.xu) break;
		}

		if (y == vhb.yu) break;
	}
}

/**
 * Find the vehicle close to the clicked coordinates.
 * @param vp Viewport clicked in.
 * @param x  X coordinate in the viewport.
 * @param y  Y coordinate in the viewport.
 * @return Closest vehicle, or \c NULL if none found.
 */
Vehicle *CheckClickOnVehicle(const ViewPort *vp, int x, int y)
{
	Vehicle *found = NULL, *v;
	uint dist, best_dist = UINT_MAX;

	if ((uint)(x -= vp->left) >= (uint)vp->width || (uint)(y -= vp->top) >= (uint)vp->height) return NULL;

	x = ScaleByZoom(x, vp->zoom) + vp->virtual_left;
	y = ScaleByZoom(y, vp->zoom) + vp->virtual_top;

	FOR_ALL_VEHICLES(v) {
		if (((v->vehstatus & VS_UNCLICKABLE) == 0) && v->IsDrawn() &&
				x >= v->coord.left && x <= v->coord.right &&
				y >= v->coord.top && y <= v->coord.bottom) {

			dist = max(
				abs(((v->coord.left + v->coord.right) >> 1) - x),
				abs(((v->coord.top + v->coord.bottom) >> 1) - y)
			);

			if (dist < best_dist) {
				found = v;
				best_dist = dist;
			}
		}
	}

	return found;
}

/**
 * Decrease the value of a vehicle.
 * @param v %Vehicle to devaluate.
 */
void DecreaseVehicleValue(Vehicle *v)
{
	v->value -= v->value >> 8;
	SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
}

/** The chances for the different types of vehicles to suffer from different types of breakdowns
 * The chance for a given breakdown type n is _breakdown_chances[vehtype][n] - _breakdown_chances[vehtype][n-1] */
static const byte _breakdown_chances[4][4] = {
	{ //Trains:
		25,  ///< 10% chance for BREAKDOWN_CRITICAL.
		51,  ///< 10% chance for BREAKDOWN_EM_STOP.
		127, ///< 30% chance for BREAKDOWN_LOW_SPEED.
		255, ///< 50% chance for BREAKDOWN_LOW_POWER.
	},
	{ //Road Vehicles:
		51,  ///< 20% chance for BREAKDOWN_CRITICAL.
		76,  ///< 10% chance for BREAKDOWN_EM_STOP.
		153, ///< 30% chance for BREAKDOWN_LOW_SPEED.
		255, ///< 40% chance for BREAKDOWN_LOW_POWER.
	},
	{ //Ships:
		51,  ///< 20% chance for BREAKDOWN_CRITICAL.
		76,  ///< 10% chance for BREAKDOWN_EM_STOP.
		178, ///< 40% chance for BREAKDOWN_LOW_SPEED.
		255, ///< 30% chance for BREAKDOWN_LOW_POWER.
	},
	{ //Aircraft:
		178, ///< 70% chance for BREAKDOWN_AIRCRAFT_SPEED.
		229, ///< 20% chance for BREAKDOWN_AIRCRAFT_DEPOT.
		255, ///< 10% chance for BREAKDOWN_AIRCRAFT_EM_LANDING.
		255, ///< Aircraft have only 3 breakdown types, so anything above 0% here will cause a crash.
	},
};

/**
 * Determine the type of breakdown a vehicle will have.
 * Results are saved in breakdown_type and breakdown_severity.
 * @param v the vehicle in question.
 * @param r the random number to use. (Note that bits 0..6 are already used)
 */
void DetermineBreakdownType(Vehicle *v, uint32 r) {
	/* if 'improved breakdowns' is off, just do the classic breakdown */
	if (!_settings_game.vehicle.improved_breakdowns) {
		v->breakdown_type = BREAKDOWN_CRITICAL;
		v->breakdown_severity = 40; //only used by aircraft (321 km/h)
		return;
	}
	byte rand = GB(r, 8, 8);
	const byte *breakdown_type_chance = _breakdown_chances[v->type];

	if (v->type == VEH_AIRCRAFT) {
		if (rand <= breakdown_type_chance[BREAKDOWN_AIRCRAFT_SPEED]) {
			v->breakdown_type = BREAKDOWN_AIRCRAFT_SPEED;
			/* all speed values here are 1/8th of the real max speed in km/h */
			byte max_speed = max(1, min(v->vcache.cached_max_speed >> 3, 255));
			byte min_speed = max(1, min(15 + (max_speed >> 2), v->vcache.cached_max_speed >> 4));
			v->breakdown_severity = min_speed + (((v->reliability + GB(r, 16, 16)) * (max_speed - min_speed)) >> 17);
		} else if (rand <= breakdown_type_chance[BREAKDOWN_AIRCRAFT_DEPOT]) {
			v->breakdown_type = BREAKDOWN_AIRCRAFT_DEPOT;
		} else if (rand <= breakdown_type_chance[BREAKDOWN_AIRCRAFT_EM_LANDING]) {
			/* emergency landings only happen when reliability < 87% */
			if (v->reliability < 0xDDDD) {
				v->breakdown_type = BREAKDOWN_AIRCRAFT_EM_LANDING;
			} else {
				/* try again */
				DetermineBreakdownType(v, Random());
			}
		} else {
			NOT_REACHED();
		}
		return;
	}

	if (rand <= breakdown_type_chance[BREAKDOWN_CRITICAL]) {
		v->breakdown_type = BREAKDOWN_CRITICAL;
	} else if (rand <= breakdown_type_chance[BREAKDOWN_EM_STOP]) {
		/* Non-front engines cannot have emergency stops */
		if (v->type == VEH_TRAIN && !(Train::From(v)->IsFrontEngine())) {
			return DetermineBreakdownType(v, Random());
		}
		v->breakdown_type = BREAKDOWN_EM_STOP;
		v->breakdown_delay >>= 2; //emergency stops don't last long (1/4 of normal)
	} else if (rand <= breakdown_type_chance[BREAKDOWN_LOW_SPEED]) {
		v->breakdown_type = BREAKDOWN_LOW_SPEED;
		/* average of random and reliability */
		uint16 rand2 = (GB(r, 16, 16) + v->reliability) >> 1;
		uint16 max_speed =
			(v->type == VEH_TRAIN) ?
			GetVehicleProperty(v, PROP_TRAIN_SPEED, RailVehInfo(v->engine_type)->max_speed) :
			(v->type == VEH_ROAD ) ?
			GetVehicleProperty(v, PROP_ROADVEH_SPEED, RoadVehInfo(v->engine_type)->max_speed) :
			(v->type == VEH_SHIP) ?
			GetVehicleProperty(v, PROP_SHIP_SPEED, ShipVehInfo(v->engine_type)->max_speed ) :
			GetVehicleProperty(v, PROP_AIRCRAFT_SPEED, AircraftVehInfo(v->engine_type)->max_speed);
		byte min_speed = min(41, max_speed >> 2);
		/* we use the min() function here because we want to use the real value of max_speed for the min_speed calculation */
		max_speed = min(max_speed, 255);
		v->breakdown_severity = Clamp((max_speed * rand2) >> 16, min_speed, max_speed);
	} else if (rand <= breakdown_type_chance[BREAKDOWN_LOW_POWER]) {
		v->breakdown_type = BREAKDOWN_LOW_POWER;
		/** within this type there are two possibilities: (50/50)
		 * power reduction (10-90%), or no power at all */
		if (GB(r, 7, 1)) {
			v->breakdown_severity = Clamp((GB(r, 16, 16) + v->reliability) >> 9, 26, 231);
		} else {
			v->breakdown_severity = 0;
		}
	} else {
		NOT_REACHED();
	}
}

void CheckVehicleBreakdown(Vehicle *v)
{
	int rel, rel_old;

	/* decrease reliability */
	v->reliability = rel = max((rel_old = v->reliability) - v->reliability_spd_dec, 0);
	if ((rel_old >> 8) != (rel >> 8)) SetWindowDirty(WC_VEHICLE_DETAILS, v->First()->index);

	if (v->breakdown_ctr != 0 || (v->First()->vehstatus & VS_STOPPED) ||
			_settings_game.difficulty.vehicle_breakdowns < 1 ||
			v->First()->cur_speed < 5 || _game_mode == GM_MENU ||
			(v->type == VEH_AIRCRAFT && ((Aircraft*)v)->state != FLYING) ||
			(v->type == VEH_TRAIN && !(Train::From(v)->IsFrontEngine()) && !_settings_game.vehicle.improved_breakdowns)) {
		return;
	}

	uint32 r = Random();

	/* increase chance of failure */
	int chance = v->breakdown_chance + 1;
	if (Chance16I(1, 25, r)) chance += 25;
	chance = min(255, chance);
	v->breakdown_chance = chance;

	if (_settings_game.vehicle.improved_breakdowns) {
		if (v->type == VEH_TRAIN && Train::From(v)->IsMultiheaded()) {
			/* Dual engines have their breakdown chances reduced to 70% of the normal value */
			chance = chance * 7 / 10;
		}
		chance *= v->First()->breakdown_chance_factor;
		chance >>= 7;
	}
	/**
	 * Chance is (1 - reliability) * breakdown_setting * breakdown_chance / 10.
	 * At 90% reliabilty, normal setting (2) and average breakdown_chance (128),
	 * a vehicle will break down (on average) every 100 days.
	 * This *should* mean that vehicles break down about as often as (or a little less than) they used to.
	 * However, because breakdowns are no longer by definition a complete stop,
	 * their impact will be significantly less.
	 */
	uint32 r1 = Random();
	if ((uint32) (0xffff - v->reliability) * _settings_game.difficulty.vehicle_breakdowns * chance > GB(r1, 0, 24) * 10) {
		uint32 r2 = Random();
		v->breakdown_ctr = GB(r1, 24, 6) + 0xF;
		v->breakdown_delay = GB(r2, 0, 7) + 0x80;
		v->breakdown_chance = 0;
		DetermineBreakdownType(v, r2);
	}
}

/**
 * Handle all of the aspects of a vehicle breakdown
 * This includes adding smoke and sounds, and ending the breakdown when appropriate.
 * @return true iff the vehicle is stopped because of a breakdown
 * @note This function always returns false for aircraft, since these never stop for breakdowns
 */
bool Vehicle::HandleBreakdown()
{
	/* Possible states for Vehicle::breakdown_ctr
	 * 0  - vehicle is running normally
	 * 1  - vehicle is currently broken down
	 * 2  - vehicle is going to break down now
	 * >2 - vehicle is counting down to the actual breakdown event */
	switch (this->breakdown_ctr) {
		case 0:
			return false;

		case 2:
			this->breakdown_ctr = 1;

			if (this->breakdowns_since_last_service != 255) {
				this->breakdowns_since_last_service++;
			}

			if (this->type == VEH_AIRCRAFT) {
				this->MarkDirty();
				assert(this->breakdown_type <= BREAKDOWN_AIRCRAFT_EM_LANDING);
				/* Aircraft just need this flag, the rest is handled elsewhere */
				this->vehstatus |= VS_AIRCRAFT_BROKEN;
				if(this->breakdown_type == BREAKDOWN_AIRCRAFT_SPEED ||
						(this->current_order.IsType(OT_GOTO_DEPOT) &&
						(this->current_order.GetDepotOrderType() & ODTFB_BREAKDOWN) &&
						GetTargetAirportIfValid(Aircraft::From(this)) != NULL)) return false;
				FindBreakdownDestination(Aircraft::From(this));
			} else if (this->type == VEH_TRAIN) {
				if (this->breakdown_type == BREAKDOWN_LOW_POWER ||
						this->First()->cur_speed <= ((this->breakdown_type == BREAKDOWN_LOW_SPEED) ? this->breakdown_severity : 0)) {
					switch (this->breakdown_type) {
						case BREAKDOWN_RV_CRASH:
							if (_settings_game.vehicle.improved_breakdowns) SetBit(Train::From(this)->flags, VRF_HAS_HIT_RV);
						/* FALL THROUGH */
						case BREAKDOWN_CRITICAL:
							if (!PlayVehicleSound(this, VSE_BREAKDOWN)) {
								bool train_or_ship = this->type == VEH_TRAIN || this->type == VEH_SHIP;
								SndPlayVehicleFx((_settings_game.game_creation.landscape != LT_TOYLAND) ?
										(train_or_ship ? SND_10_TRAIN_BREAKDOWN : SND_0F_VEHICLE_BREAKDOWN) :
										(train_or_ship ? SND_3A_COMEDY_BREAKDOWN_2 : SND_35_COMEDY_BREAKDOWN), this);
							}
							if (!(this->vehstatus & VS_HIDDEN) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_NO_BREAKDOWN_SMOKE) && this->breakdown_delay > 0) {
								EffectVehicle *u = CreateEffectVehicleRel(this, 4, 4, 5, EV_BREAKDOWN_SMOKE);
								if (u != NULL) u->animation_state = this->breakdown_delay * 2;
							}
							/* Max Speed reduction*/
							if (_settings_game.vehicle.improved_breakdowns) {
								if (!HasBit(Train::From(this)->flags, VRF_NEED_REPAIR)) {
									SetBit(Train::From(this)->flags, VRF_NEED_REPAIR);
									Train::From(this)->critical_breakdown_count = 1;
								} else if (Train::From(this)->critical_breakdown_count != 255) {
									Train::From(this)->critical_breakdown_count++;
								}
								Train::From(this->First())->ConsistChanged(CCF_TRACK);
							}
						/* FALL THROUGH */
						case BREAKDOWN_EM_STOP:
							CheckBreakdownFlags(Train::From(this->First()));
							SetBit(Train::From(this->First())->flags, VRF_BREAKDOWN_STOPPED);
							break;
						case BREAKDOWN_LOW_SPEED:
							CheckBreakdownFlags(Train::From(this->First()));
							SetBit(Train::From(this->First())->flags, VRF_BREAKDOWN_SPEED);
							break;
						case BREAKDOWN_LOW_POWER:
							SetBit(Train::From(this->First())->flags, VRF_BREAKDOWN_POWER);
							break;
						default: NOT_REACHED();
					}
					this->First()->MarkDirty();
					SetWindowDirty(WC_VEHICLE_VIEW, this->index);
					SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
				} else {
					this->breakdown_ctr = 2; // wait until slowdown
					this->breakdowns_since_last_service--;
					SetBit(Train::From(this)->flags, VRF_BREAKDOWN_BRAKING);
					return false;
				}
				if ((!(this->vehstatus & VS_HIDDEN)) && (this->breakdown_type == BREAKDOWN_LOW_SPEED || this->breakdown_type == BREAKDOWN_LOW_POWER)
						&& !HasBit(EngInfo(this->engine_type)->misc_flags, EF_NO_BREAKDOWN_SMOKE)) {
					EffectVehicle *u = CreateEffectVehicleRel(this, 0, 0, 2, EV_BREAKDOWN_SMOKE); //some grey clouds to indicate a broken engine
					if (u != NULL) u->animation_state = 25;
				}
			} else {
				switch (this->breakdown_type) {
					case BREAKDOWN_CRITICAL:
						if (!PlayVehicleSound(this, VSE_BREAKDOWN)) {
							SndPlayVehicleFx((_settings_game.game_creation.landscape != LT_TOYLAND) ? SND_0F_VEHICLE_BREAKDOWN : SND_35_COMEDY_BREAKDOWN, this);
						}
						if (!(this->vehstatus & VS_HIDDEN) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_NO_BREAKDOWN_SMOKE) && this->breakdown_delay > 0) {
							EffectVehicle *u = CreateEffectVehicleRel(this, 4, 4, 5, EV_BREAKDOWN_SMOKE);
							if (u != NULL) u->animation_state = this->breakdown_delay * 2;
						}
						if (_settings_game.vehicle.improved_breakdowns) {
							if (this->type == VEH_ROAD) {
								if (RoadVehicle::From(this)->critical_breakdown_count != 255) {
									RoadVehicle::From(this)->critical_breakdown_count++;
								}
							}
						}
					/* FALL THROUGH */
					case BREAKDOWN_EM_STOP:
						this->cur_speed = 0;
						break;
					case BREAKDOWN_LOW_SPEED:
					case BREAKDOWN_LOW_POWER:
						/* do nothing */
						break;
					default: NOT_REACHED();
				}
				if ((!(this->vehstatus & VS_HIDDEN)) &&
						(this->breakdown_type == BREAKDOWN_LOW_SPEED || this->breakdown_type == BREAKDOWN_LOW_POWER)) {
					/* Some gray clouds to indicate a broken RV */
					EffectVehicle *u = CreateEffectVehicleRel(this, 0, 0, 2, EV_BREAKDOWN_SMOKE);
					if (u != NULL) u->animation_state = 25;
				}
				this->First()->MarkDirty();
				SetWindowDirty(WC_VEHICLE_VIEW, this->index);
				SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
				return (this->breakdown_type == BREAKDOWN_CRITICAL || this->breakdown_type == BREAKDOWN_EM_STOP);
			}

			FALLTHROUGH;
		case 1:
			/* Aircraft breakdowns end only when arriving at the airport */
			if (this->type == VEH_AIRCRAFT) return false;

			/* For trains this function is called twice per tick, so decrease v->breakdown_delay at half the rate */
			if ((this->tick_counter & (this->type == VEH_TRAIN ? 3 : 1)) == 0) {
				if (--this->breakdown_delay == 0) {
					this->breakdown_ctr = 0;
					if (this->type == VEH_TRAIN) {
						CheckBreakdownFlags(Train::From(this->First()));
						this->First()->MarkDirty();
						SetWindowDirty(WC_VEHICLE_VIEW, this->First()->index);
					} else {
						this->MarkDirty();
						SetWindowDirty(WC_VEHICLE_VIEW, this->index);
					}
				}
			}
			return (this->breakdown_type == BREAKDOWN_CRITICAL || this->breakdown_type == BREAKDOWN_EM_STOP || this->breakdown_type == BREAKDOWN_RV_CRASH);

		default:
			if (!this->current_order.IsType(OT_LOADING)) this->breakdown_ctr--;
			return false;
	}
}

/**
 * Update age of a vehicle.
 * @param v Vehicle to update.
 */
void AgeVehicle(Vehicle *v)
{
	/* Stop if a virtual vehicle */
	if (HasBit(v->subtype, GVSF_VIRTUAL)) return;

	if (v->age < MAX_DAY) {
		v->age++;
		if (v->IsPrimaryVehicle() && v->age == VEHICLE_PROFIT_MIN_AGE + 1) GroupStatistics::VehicleReachedProfitAge(v);
	}

	if (!v->IsPrimaryVehicle() && (v->type != VEH_TRAIN || !Train::From(v)->IsEngine())) return;

	int age = v->age - v->max_age;
	if (age == DAYS_IN_LEAP_YEAR * 0 || age == DAYS_IN_LEAP_YEAR * 1 ||
			age == DAYS_IN_LEAP_YEAR * 2 || age == DAYS_IN_LEAP_YEAR * 3 || age == DAYS_IN_LEAP_YEAR * 4) {
		v->reliability_spd_dec <<= 1;
	}

	SetWindowDirty(WC_VEHICLE_DETAILS, v->index);

	/* Don't warn about non-primary or not ours vehicles or vehicles that are crashed */
	if (v->Previous() != NULL || v->owner != _local_company || (v->vehstatus & VS_CRASHED) != 0) return;

	/* Don't warn if a renew is active */
	if (Company::Get(v->owner)->settings.engine_renew && v->GetEngine()->company_avail != 0) return;

	StringID str;
	if (age == -DAYS_IN_LEAP_YEAR) {
		str = STR_NEWS_VEHICLE_IS_GETTING_OLD;
	} else if (age == 0) {
		str = STR_NEWS_VEHICLE_IS_GETTING_VERY_OLD;
	} else if (age > 0 && (age % DAYS_IN_LEAP_YEAR) == 0) {
		str = STR_NEWS_VEHICLE_IS_GETTING_VERY_OLD_AND;
	} else {
		return;
	}

	SetDParam(0, v->index);
	AddVehicleAdviceNewsItem(str, v->index);
}

/**
 * Calculates how full a vehicle is.
 * @param front The front vehicle of the consist to check.
 * @param colour The string to show depending on if we are unloading or loading
 * @return A percentage of how full the Vehicle is.
 *         Percentages are rounded towards 50%, so that 0% and 100% are only returned
 *         if the vehicle is completely empty or full.
 *         This is useful for both display and conditional orders.
 */
uint8 CalcPercentVehicleFilled(const Vehicle *front, StringID *colour)
{
	int count = 0;
	int max = 0;
	int cars = 0;
	int unloading = 0;
	bool loading = false;

	bool is_loading = front->current_order.IsType(OT_LOADING);

	/* The station may be NULL when the (colour) string does not need to be set. */
	const Station *st = Station::GetIfValid(front->last_station_visited);
	assert(colour == NULL || (st != NULL && is_loading));

	bool order_no_load = is_loading && (front->current_order.GetLoadType() & OLFB_NO_LOAD);
	bool order_full_load = is_loading && (front->current_order.GetLoadType() & OLFB_FULL_LOAD);

	/* Count up max and used */
	for (const Vehicle *v = front; v != NULL; v = v->Next()) {
		count += v->cargo.StoredCount();
		max += v->cargo_cap;
		if (v->cargo_cap != 0 && colour != NULL) {
			unloading += HasBit(v->vehicle_flags, VF_CARGO_UNLOADING) ? 1 : 0;
			loading |= !order_no_load &&
					(order_full_load || st->goods[v->cargo_type].HasRating()) &&
					!HasBit(v->vehicle_flags, VF_LOADING_FINISHED) && !HasBit(v->vehicle_flags, VF_STOP_LOADING);
			cars++;
		}
	}

	if (colour != NULL) {
		if (unloading == 0 && loading) {
			*colour = STR_PERCENT_UP;
		} else if (unloading == 0 && !loading) {
			*colour = STR_PERCENT_NONE;
		} else if (cars == unloading || !loading) {
			*colour = STR_PERCENT_DOWN;
		} else {
			*colour = STR_PERCENT_UP_DOWN;
		}
	}

	/* Train without capacity */
	if (max == 0) return 100;

	/* Return the percentage */
	if (count * 2 < max) {
		/* Less than 50%; round up, so that 0% means really empty. */
		return CeilDiv(count * 100, max);
	} else {
		/* More than 50%; round down, so that 100% means really full. */
		return (count * 100) / max;
	}
}

/**
 * Vehicle entirely entered the depot, update its status, orders, vehicle windows, service it, etc.
 * @param v Vehicle that entered a depot.
 */
void VehicleEnterDepot(Vehicle *v)
{
	/* Always work with the front of the vehicle */
	assert(v == v->First());

	switch (v->type) {
		case VEH_TRAIN: {
			Train *t = Train::From(v);
			SetWindowClassesDirty(WC_TRAINS_LIST);
			/* Clear path reservation */
			SetDepotReservation(t->tile, false);
			if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(t->tile, ZOOM_LVL_DRAW_MAP);

			UpdateSignalsOnSegment(t->tile, INVALID_DIAGDIR, t->owner);
			t->wait_counter = 0;
			t->force_proceed = TFP_NONE;
			ClrBit(t->flags, VRF_TOGGLE_REVERSE);
			t->ConsistChanged(CCF_ARRANGE);
			t->reverse_distance = 0;
			break;
		}

		case VEH_ROAD:
			SetWindowClassesDirty(WC_ROADVEH_LIST);
			break;

		case VEH_SHIP: {
			SetWindowClassesDirty(WC_SHIPS_LIST);
			Ship *ship = Ship::From(v);
			ship->state = TRACK_BIT_DEPOT;
			ship->UpdateCache();
			ship->UpdateViewport(true, true);
			SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
			break;
		}

		case VEH_AIRCRAFT:
			SetWindowClassesDirty(WC_AIRCRAFT_LIST);
			HandleAircraftEnterHangar(Aircraft::From(v));
			break;
		default: NOT_REACHED();
	}
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);

	if (v->type != VEH_TRAIN) {
		/* Trains update the vehicle list when the first unit enters the depot and calls VehicleEnterDepot() when the last unit enters.
		 * We only increase the number of vehicles when the first one enters, so we will not need to search for more vehicles in the depot */
		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	}
	SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);

	v->vehstatus |= VS_HIDDEN;
	v->cur_speed = 0;

	VehicleServiceInDepot(v);

	/* After a vehicle trigger, the graphics and properties of the vehicle could change. */
	TriggerVehicle(v, VEHICLE_TRIGGER_DEPOT);
	v->MarkDirty();

	if (v->current_order.IsType(OT_GOTO_DEPOT)) {
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);

		const Order *real_order = v->GetOrder(v->cur_real_order_index);

		/* Test whether we are heading for this depot. If not, do nothing.
		 * Note: The target depot for nearest-/manual-depot-orders is only updated on junctions, but we want to accept every depot. */
		if ((v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) &&
				real_order != NULL && !(real_order->GetDepotActionType() & ODATFB_NEAREST_DEPOT) &&
				(v->type == VEH_AIRCRAFT ? v->current_order.GetDestination() != GetStationIndex(v->tile) : v->dest_tile != v->tile)) {
			/* We are heading for another depot, keep driving. */
			return;
		}

		if (v->current_order.GetDepotActionType() & ODATFB_SELL) {
			_vehicles_to_sell.insert(v);
			return;
		}

		if (v->current_order.IsRefit()) {
			Backup<CompanyByte> cur_company(_current_company, v->owner, FILE_LINE);
			CommandCost cost = DoCommand(v->tile, v->index, v->current_order.GetRefitCargo() | 0xFF << 8, DC_EXEC, GetCmdRefitVeh(v));
			cur_company.Restore();

			if (cost.Failed()) {
				_vehicles_to_autoreplace[v] = false;
				if (v->owner == _local_company) {
					/* Notify the user that we stopped the vehicle */
					SetDParam(0, v->index);
					AddVehicleAdviceNewsItem(STR_NEWS_ORDER_REFIT_FAILED, v->index);
				}
			} else if (cost.GetCost() != 0) {
				v->profit_this_year -= cost.GetCost() << 8;
				if (v->owner == _local_company) {
					ShowCostOrIncomeAnimation(v->x_pos, v->y_pos, v->z_pos, cost.GetCost());
				}
			}
		}

		/* Handle the ODTFB_PART_OF_ORDERS case. If there is a timetabled wait time, hold the train, otherwise skip to the next order.
		Note that if there is a only a travel_time, but no wait_time defined for the order, and the train arrives to the depot sooner as scheduled,
		he doesn't wait in it, as it would in stations. Thus, the original behaviour is maintained if there's no defined wait_time.*/
		if (v->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) {
			v->DeleteUnreachedImplicitOrders();
			UpdateVehicleTimetable(v, true);
			if (v->current_order.IsWaitTimetabled() && !(v->current_order.GetDepotActionType() & ODATFB_HALT)) {
				v->current_order.MakeWaiting();
				v->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
				return;
			} else {
				v->IncrementImplicitOrderIndex();
			}
		}

		if (v->current_order.GetDepotActionType() & ODATFB_HALT) {
			/* Vehicles are always stopped on entering depots. Do not restart this one. */
			_vehicles_to_autoreplace[v] = false;
			/* Invalidate last_loading_station. As the link from the station
			 * before the stop to the station after the stop can't be predicted
			 * we shouldn't construct it when the vehicle visits the next stop. */
			v->last_loading_station = INVALID_STATION;
			ClrBit(v->vehicle_flags, VF_LAST_LOAD_ST_SEP);
			if (v->owner == _local_company) {
				SetDParam(0, v->index);
				AddVehicleAdviceNewsItem(STR_NEWS_TRAIN_IS_WAITING + v->type, v->index);
			}
			AI::NewEvent(v->owner, new ScriptEventVehicleWaitingInDepot(v->index));
		}
		v->current_order.MakeDummy();
	}
}


/**
 * Update the position of the vehicle. This will update the hash that tells
 *  which vehicles are on a tile.
 */
void Vehicle::UpdatePosition()
{
	UpdateVehicleTileHash(this, false);
}

/**
 * Update the vehicle on the viewport, updating the right hash and setting the
 *  new coordinates.
 * @param dirty Mark the (new and old) coordinates of the vehicle as dirty.
 */
void Vehicle::UpdateViewport(bool dirty)
{
	Rect new_coord = ConvertRect<Rect16, Rect>(this->sprite_seq_bounds);

	Point pt = RemapCoords(this->x_pos + this->x_offs, this->y_pos + this->y_offs, this->z_pos);
	new_coord.left   += pt.x;
	new_coord.top    += pt.y;
	new_coord.right  += pt.x + 2 * ZOOM_LVL_BASE;
	new_coord.bottom += pt.y + 2 * ZOOM_LVL_BASE;

	UpdateVehicleViewportHash(this, new_coord.left, new_coord.top);

	Rect old_coord = this->coord;
	this->coord = new_coord;

	if (dirty) {
		if (old_coord.left == INVALID_COORD) {
			this->MarkAllViewportsDirty();
		} else {
			::MarkAllViewportsDirty(
					min(old_coord.left,   this->coord.left),
					min(old_coord.top,    this->coord.top),
					max(old_coord.right,  this->coord.right),
					max(old_coord.bottom, this->coord.bottom),
					this->type != VEH_EFFECT ? ZOOM_LVL_END : ZOOM_LVL_DRAW_MAP
			);
		}
	}
}

/**
 * Update the position of the vehicle, and update the viewport.
 */
void Vehicle::UpdatePositionAndViewport()
{
	this->UpdatePosition();
	this->UpdateViewport(true);
}

/**
 * Marks viewports dirty where the vehicle's image is.
 */
void Vehicle::MarkAllViewportsDirty() const
{
	::MarkAllViewportsDirty(this->coord.left, this->coord.top, this->coord.right, this->coord.bottom);
}

/**
 * Get position information of a vehicle when moving one pixel in the direction it is facing
 * @param v Vehicle to move
 * @return Position information after the move
 */
GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v)
{
	static const int8 _delta_coord[16] = {
		-1,-1,-1, 0, 1, 1, 1, 0, /* x */
		-1, 0, 1, 1, 1, 0,-1,-1, /* y */
	};

	int x = v->x_pos + _delta_coord[v->direction];
	int y = v->y_pos + _delta_coord[v->direction + 8];

	GetNewVehiclePosResult gp;
	gp.x = x;
	gp.y = y;
	gp.old_tile = v->tile;
	gp.new_tile = TileVirtXY(x, y);
	return gp;
}

static const Direction _new_direction_table[] = {
	DIR_N,  DIR_NW, DIR_W,
	DIR_NE, DIR_SE, DIR_SW,
	DIR_E,  DIR_SE, DIR_S
};

Direction GetDirectionTowards(const Vehicle *v, int x, int y)
{
	int i = 0;

	if (y >= v->y_pos) {
		if (y != v->y_pos) i += 3;
		i += 3;
	}

	if (x >= v->x_pos) {
		if (x != v->x_pos) i++;
		i++;
	}

	Direction dir = v->direction;

	DirDiff dirdiff = DirDifference(_new_direction_table[i], dir);
	if (dirdiff == DIRDIFF_SAME) return dir;
	return ChangeDir(dir, dirdiff > DIRDIFF_REVERSE ? DIRDIFF_45LEFT : DIRDIFF_45RIGHT);
}

/**
 * Call the tile callback function for a vehicle entering a tile
 * @param v    Vehicle entering the tile
 * @param tile Tile entered
 * @param x    X position
 * @param y    Y position
 * @return Some meta-data over the to be entered tile.
 * @see VehicleEnterTileStatus to see what the bits in the return value mean.
 */
VehicleEnterTileStatus VehicleEnterTile(Vehicle *v, TileIndex tile, int x, int y)
{
	return _tile_type_procs[GetTileType(tile)]->vehicle_enter_tile_proc(v, tile, x, y);
}

/**
 * Initializes the structure. Vehicle unit numbers are supposed not to change after
 * struct initialization, except after each call to this->NextID() the returned value
 * is assigned to a vehicle.
 * @param type type of vehicle
 * @param owner owner of vehicles
 */
FreeUnitIDGenerator::FreeUnitIDGenerator(VehicleType type, CompanyID owner) : cache(NULL), maxid(0), curid(0)
{
	/* Find maximum */
	const Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (v->type == type && v->owner == owner) {
			this->maxid = max<UnitID>(this->maxid, v->unitnumber);
		}
	}

	if (this->maxid == 0) return;

	/* Reserving 'maxid + 2' because we need:
	 * - space for the last item (with v->unitnumber == maxid)
	 * - one free slot working as loop terminator in FreeUnitIDGenerator::NextID() */
	this->cache = CallocT<bool>(this->maxid + 2);

	/* Fill the cache */
	FOR_ALL_VEHICLES(v) {
		if (v->type == type && v->owner == owner) {
			this->cache[v->unitnumber] = true;
		}
	}
}

/** Returns next free UnitID. Supposes the last returned value was assigned to a vehicle. */
UnitID FreeUnitIDGenerator::NextID()
{
	if (this->maxid <= this->curid) return ++this->curid;

	while (this->cache[++this->curid]) { } // it will stop, we reserved more space than needed

	return this->curid;
}

/**
 * Get an unused unit number for a vehicle (if allowed).
 * @param type Type of vehicle
 * @return A unused unit number for the given type of vehicle if it is allowed to build one, else \c UINT16_MAX.
 */
UnitID GetFreeUnitNumber(VehicleType type)
{
	/* Check whether it is allowed to build another vehicle. */
	uint max_veh;
	switch (type) {
		case VEH_TRAIN:    max_veh = _settings_game.vehicle.max_trains;   break;
		case VEH_ROAD:     max_veh = _settings_game.vehicle.max_roadveh;  break;
		case VEH_SHIP:     max_veh = _settings_game.vehicle.max_ships;    break;
		case VEH_AIRCRAFT: max_veh = _settings_game.vehicle.max_aircraft; break;
		default: NOT_REACHED();
	}

	const Company *c = Company::Get(_current_company);
	if (c->group_all[type].num_vehicle >= max_veh) return UINT16_MAX; // Currently already at the limit, no room to make a new one.

	FreeUnitIDGenerator gen(type, _current_company);

	return gen.NextID();
}


/**
 * Check whether we can build infrastructure for the given
 * vehicle type. This to disable building stations etc. when
 * you are not allowed/able to have the vehicle type yet.
 * @param type the vehicle type to check this for
 * @return true if there is any reason why you may build
 *         the infrastructure for the given vehicle type
 */
bool CanBuildVehicleInfrastructure(VehicleType type)
{
	assert(IsCompanyBuildableVehicleType(type));

	if (!Company::IsValidID(_local_company)) return false;
	if (!_settings_client.gui.disable_unsuitable_building) return true;

	UnitID max;
	switch (type) {
		case VEH_TRAIN:    max = _settings_game.vehicle.max_trains; break;
		case VEH_ROAD:     max = _settings_game.vehicle.max_roadveh; break;
		case VEH_SHIP:     max = _settings_game.vehicle.max_ships; break;
		case VEH_AIRCRAFT: max = _settings_game.vehicle.max_aircraft; break;
		default: NOT_REACHED();
	}

	/* We can build vehicle infrastructure when we may build the vehicle type */
	if (max > 0) {
		/* Can we actually build the vehicle type? */
		const Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, type) {
			if (HasBit(e->company_avail, _local_company)) return true;
		}
		return false;
	}

	/* We should be able to build infrastructure when we have the actual vehicle type */
	const Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (v->owner == _local_company && v->type == type) return true;
	}

	return false;
}


/**
 * Determines the #LiveryScheme for a vehicle.
 * @param engine_type Engine of the vehicle.
 * @param parent_engine_type Engine of the front vehicle, #INVALID_ENGINE if vehicle is at front itself.
 * @param v the vehicle, \c NULL if in purchase list etc.
 * @return livery scheme to use.
 */
LiveryScheme GetEngineLiveryScheme(EngineID engine_type, EngineID parent_engine_type, const Vehicle *v)
{
	CargoID cargo_type = v == NULL ? (CargoID)CT_INVALID : v->cargo_type;
	const Engine *e = Engine::Get(engine_type);
	switch (e->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			if (v != NULL && parent_engine_type != INVALID_ENGINE && (UsesWagonOverride(v) || (v->IsArticulatedPart() && e->u.rail.railveh_type != RAILVEH_WAGON))) {
				/* Wagonoverrides use the colour scheme of the front engine.
				 * Articulated parts use the colour scheme of the first part. (Not supported for articulated wagons) */
				engine_type = parent_engine_type;
				e = Engine::Get(engine_type);
				/* Note: Luckily cargo_type is not needed for engines */
			}

			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo
			if (e->u.rail.railveh_type == RAILVEH_WAGON) {
				if (!CargoSpec::Get(cargo_type)->is_freight) {
					if (parent_engine_type == INVALID_ENGINE) {
						return LS_PASSENGER_WAGON_STEAM;
					} else {
						switch (RailVehInfo(parent_engine_type)->engclass) {
							default: NOT_REACHED();
							case EC_STEAM:    return LS_PASSENGER_WAGON_STEAM;
							case EC_DIESEL:   return LS_PASSENGER_WAGON_DIESEL;
							case EC_ELECTRIC: return LS_PASSENGER_WAGON_ELECTRIC;
							case EC_MONORAIL: return LS_PASSENGER_WAGON_MONORAIL;
							case EC_MAGLEV:   return LS_PASSENGER_WAGON_MAGLEV;
						}
					}
				} else {
					return LS_FREIGHT_WAGON;
				}
			} else {
				bool is_mu = HasBit(e->info.misc_flags, EF_RAIL_IS_MU);

				switch (e->u.rail.engclass) {
					default: NOT_REACHED();
					case EC_STEAM:    return LS_STEAM;
					case EC_DIESEL:   return is_mu ? LS_DMU : LS_DIESEL;
					case EC_ELECTRIC: return is_mu ? LS_EMU : LS_ELECTRIC;
					case EC_MONORAIL: return LS_MONORAIL;
					case EC_MAGLEV:   return LS_MAGLEV;
				}
			}

		case VEH_ROAD:
			/* Always use the livery of the front */
			if (v != NULL && parent_engine_type != INVALID_ENGINE) {
				engine_type = parent_engine_type;
				e = Engine::Get(engine_type);
				cargo_type = v->First()->cargo_type;
			}
			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo

			/* Important: Use Tram Flag of front part. Luckily engine_type refers to the front part here. */
			if (HasBit(e->info.misc_flags, EF_ROAD_TRAM)) {
				/* Tram */
				return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_PASSENGER_TRAM : LS_FREIGHT_TRAM;
			} else {
				/* Bus or truck */
				return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_BUS : LS_TRUCK;
			}

		case VEH_SHIP:
			if (cargo_type == CT_INVALID) cargo_type = e->GetDefaultCargoType();
			if (cargo_type == CT_INVALID) cargo_type = CT_GOODS; // The vehicle does not carry anything, let's pick some freight cargo
			return IsCargoInClass(cargo_type, CC_PASSENGERS) ? LS_PASSENGER_SHIP : LS_FREIGHT_SHIP;

		case VEH_AIRCRAFT:
			switch (e->u.air.subtype) {
				case AIR_HELI: return LS_HELICOPTER;
				case AIR_CTOL: return LS_SMALL_PLANE;
				case AIR_CTOL | AIR_FAST: return LS_LARGE_PLANE;
				default: NOT_REACHED();
			}
	}
}

/**
 * Determines the livery for a vehicle.
 * @param engine_type EngineID of the vehicle
 * @param company Owner of the vehicle
 * @param parent_engine_type EngineID of the front vehicle. INVALID_VEHICLE if vehicle is at front itself.
 * @param v the vehicle. NULL if in purchase list etc.
 * @param livery_setting The livery settings to use for acquiring the livery information.
 * @return livery to use
 */
const Livery *GetEngineLivery(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v, byte livery_setting)
{
	const Company *c = Company::Get(company);
	LiveryScheme scheme = LS_DEFAULT;

	/* The default livery is always available for use, but its in_use flag determines
	 * whether any _other_ liveries are in use. */
	if (c->livery[LS_DEFAULT].in_use && (livery_setting == LIT_ALL || (livery_setting == LIT_COMPANY && company == _local_company))) {
		/* Determine the livery scheme to use */
		scheme = GetEngineLiveryScheme(engine_type, parent_engine_type, v);

		/* Switch back to the default scheme if the resolved scheme is not in use */
		if (!c->livery[scheme].in_use) scheme = LS_DEFAULT;
	}

	return &c->livery[scheme];
}


static PaletteID GetEngineColourMap(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v)
{
	PaletteID map = (v != NULL) ? v->colourmap : PAL_NONE;

	/* Return cached value if any */
	if (map != PAL_NONE) return map;

	const Engine *e = Engine::Get(engine_type);

	/* Check if we should use the colour map callback */
	if (HasBit(e->info.callback_mask, CBM_VEHICLE_COLOUR_REMAP)) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_COLOUR_MAPPING, 0, 0, engine_type, v);
		/* Failure means "use the default two-colour" */
		if (callback != CALLBACK_FAILED) {
			assert_compile(PAL_NONE == 0); // Returning 0x4000 (resp. 0xC000) coincidences with default value (PAL_NONE)
			map = GB(callback, 0, 14);
			/* If bit 14 is set, then the company colours are applied to the
			 * map else it's returned as-is. */
			if (!HasBit(callback, 14)) {
				/* Update cache */
				if (v != NULL) const_cast<Vehicle *>(v)->colourmap = map;
				return map;
			}
		}
	}

	bool twocc = HasBit(e->info.misc_flags, EF_USES_2CC);

	if (map == PAL_NONE) map = twocc ? (PaletteID)SPR_2CCMAP_BASE : (PaletteID)PALETTE_RECOLOUR_START;

	/* Spectator has news shown too, but has invalid company ID - as well as dedicated server */
	if (!Company::IsValidID(company)) return map;

	const Livery *livery = GetEngineLivery(engine_type, company, parent_engine_type, v, _settings_client.gui.liveries);

	map += livery->colour1;
	if (twocc) map += livery->colour2 * 16;

	/* Update cache */
	if (v != NULL) const_cast<Vehicle *>(v)->colourmap = map;
	return map;
}

/**
 * Get the colour map for an engine. This used for unbuilt engines in the user interface.
 * @param engine_type ID of engine
 * @param company ID of company
 * @return A ready-to-use palette modifier
 */
PaletteID GetEnginePalette(EngineID engine_type, CompanyID company)
{
	return GetEngineColourMap(engine_type, company, INVALID_ENGINE, NULL);
}

/**
 * Get the colour map for a vehicle.
 * @param v Vehicle to get colour map for
 * @return A ready-to-use palette modifier
 */
PaletteID GetVehiclePalette(const Vehicle *v)
{
	if (v->IsGroundVehicle()) {
		return GetEngineColourMap(v->engine_type, v->owner, v->GetGroundVehicleCache()->first_engine, v);
	}

	return GetEngineColourMap(v->engine_type, v->owner, INVALID_ENGINE, v);
}

/**
 * Delete all implicit orders which were not reached.
 */
void Vehicle::DeleteUnreachedImplicitOrders()
{
	if (this->IsGroundVehicle()) {
		uint16 &gv_flags = this->GetGroundVehicleFlags();
		if (HasBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS)) {
			/* Do not delete orders, only skip them */
			ClrBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
			this->cur_implicit_order_index = this->cur_real_order_index;
			InvalidateVehicleOrder(this, 0);
			return;
		}
	}

	const Order *order = this->GetOrder(this->cur_implicit_order_index);
	while (order != NULL) {
		if (this->cur_implicit_order_index == this->cur_real_order_index) break;

		if (order->IsType(OT_IMPLICIT)) {
			DeleteOrder(this, this->cur_implicit_order_index);
			/* DeleteOrder does various magic with order_indices, so resync 'order' with 'cur_implicit_order_index' */
			order = this->GetOrder(this->cur_implicit_order_index);
		} else {
			/* Skip non-implicit orders, e.g. service-orders */
			order = order->next;
			this->cur_implicit_order_index++;
		}

		/* Wrap around */
		if (order == NULL) {
			order = this->GetOrder(0);
			this->cur_implicit_order_index = 0;
		}
	}
}

/**
 * Increase capacity for all link stats associated with vehicles in the given consist.
 * @param st Station to get the link stats from.
 * @param front First vehicle in the consist.
 * @param next_station_id Station the consist will be travelling to next.
 */
static void VehicleIncreaseStats(const Vehicle *front)
{
	for (const Vehicle *v = front; v != NULL; v = v->Next()) {
		StationID last_loading_station = HasBit(front->vehicle_flags, VF_LAST_LOAD_ST_SEP) ? v->last_loading_station : front->last_loading_station;
		if (v->refit_cap > 0 &&
				last_loading_station != INVALID_STATION &&
				last_loading_station != front->last_station_visited &&
				((front->current_order.GetCargoLoadType(v->cargo_type) & OLFB_NO_LOAD) == 0 ||
				(front->current_order.GetCargoUnloadType(v->cargo_type) & OUFB_NO_UNLOAD) == 0)) {
			/* The cargo count can indeed be higher than the refit_cap if
			 * wagons have been auto-replaced and subsequently auto-
			 * refitted to a higher capacity. The cargo gets redistributed
			 * among the wagons in that case.
			 * As usage is not such an important figure anyway we just
			 * ignore the additional cargo then.*/
			IncreaseStats(Station::Get(last_loading_station), v->cargo_type, front->last_station_visited, v->refit_cap,
				min(v->refit_cap, v->cargo.StoredCount()), EUM_INCREASE);
		}
	}
}

/**
 * Prepare everything to begin the loading when arriving at a station.
 * @pre IsTileType(this->tile, MP_STATION) || this->type == VEH_SHIP.
 */
void Vehicle::BeginLoading()
{
	if (this->type == VEH_TRAIN) {
		assert_tile(IsTileType(Train::From(this)->GetStationLoadingVehicle()->tile, MP_STATION), Train::From(this)->GetStationLoadingVehicle()->tile);
	} else {
		assert_tile(IsTileType(this->tile, MP_STATION) || this->type == VEH_SHIP, this->tile);
	}

	bool no_load_prepare = false;
	if (this->current_order.IsType(OT_GOTO_STATION) &&
			this->current_order.GetDestination() == this->last_station_visited) {
		this->DeleteUnreachedImplicitOrders();

		/* Now both order indices point to the destination station, and we can start loading */
		this->current_order.MakeLoading(true);
		UpdateVehicleTimetable(this, true);

		/* Furthermore add the Non Stop flag to mark that this station
		 * is the actual destination of the vehicle, which is (for example)
		 * necessary to be known for HandleTrainLoading to determine
		 * whether the train is lost or not; not marking a train lost
		 * that arrives at random stations is bad. */
		this->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
	} else if (this->current_order.IsType(OT_LOADING_ADVANCE)) {
		this->current_order.MakeLoading(true);
		no_load_prepare = true;
	} else {
		/* We weren't scheduled to stop here. Insert an implicit order
		 * to show that we are stopping here.
		 * While only groundvehicles have implicit orders, e.g. aircraft might still enter
		 * the 'wrong' terminal when skipping orders etc. */
		Order *in_list = this->GetOrder(this->cur_implicit_order_index);
		if (this->IsGroundVehicle() &&
				(in_list == NULL || !in_list->IsType(OT_IMPLICIT) ||
				in_list->GetDestination() != this->last_station_visited)) {
			bool suppress_implicit_orders = HasBit(this->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS);
			/* Do not create consecutive duplicates of implicit orders */
			Order *prev_order = this->cur_implicit_order_index > 0 ? this->GetOrder(this->cur_implicit_order_index - 1) : (this->GetNumOrders() > 1 ? this->GetLastOrder() : NULL);
			if (prev_order == NULL ||
					(!prev_order->IsType(OT_IMPLICIT) && !prev_order->IsType(OT_GOTO_STATION)) ||
					prev_order->GetDestination() != this->last_station_visited) {

				/* Prefer deleting implicit orders instead of inserting new ones,
				 * so test whether the right order follows later. In case of only
				 * implicit orders treat the last order in the list like an
				 * explicit one, except if the overall number of orders surpasses
				 * IMPLICIT_ORDER_ONLY_CAP. */
				int target_index = this->cur_implicit_order_index;
				bool found = false;
				while (target_index != this->cur_real_order_index || this->GetNumManualOrders() == 0) {
					const Order *order = this->GetOrder(target_index);
					if (order == NULL) break; // No orders.
					if (order->IsType(OT_IMPLICIT) && order->GetDestination() == this->last_station_visited) {
						found = true;
						break;
					}
					target_index++;
					if (target_index >= this->orders.list->GetNumOrders()) {
						if (this->GetNumManualOrders() == 0 &&
								this->GetNumOrders() < IMPLICIT_ORDER_ONLY_CAP) {
							break;
						}
						target_index = 0;
					}
					if (target_index == this->cur_implicit_order_index) break; // Avoid infinite loop.
				}

				if (found) {
					if (suppress_implicit_orders) {
						/* Skip to the found order */
						this->cur_implicit_order_index = target_index;
						InvalidateVehicleOrder(this, 0);
					} else {
						/* Delete all implicit orders up to the station we just reached */
						const Order *order = this->GetOrder(this->cur_implicit_order_index);
						while (!order->IsType(OT_IMPLICIT) || order->GetDestination() != this->last_station_visited) {
							if (order->IsType(OT_IMPLICIT)) {
								DeleteOrder(this, this->cur_implicit_order_index);
								/* DeleteOrder does various magic with order_indices, so resync 'order' with 'cur_implicit_order_index' */
								order = this->GetOrder(this->cur_implicit_order_index);
							} else {
								/* Skip non-implicit orders, e.g. service-orders */
								order = order->next;
								this->cur_implicit_order_index++;
							}

							/* Wrap around */
							if (order == NULL) {
								order = this->GetOrder(0);
								this->cur_implicit_order_index = 0;
							}
							assert(order != NULL);
						}
					}
				} else if (!suppress_implicit_orders &&
						((this->orders.list == NULL ? OrderList::CanAllocateItem() : this->orders.list->GetNumOrders() < MAX_VEH_ORDER_ID)) &&
						Order::CanAllocateItem()) {
					/* Insert new implicit order */
					Order *implicit_order = new Order();
					implicit_order->MakeImplicit(this->last_station_visited);
					InsertOrder(this, implicit_order, this->cur_implicit_order_index);
					if (this->cur_implicit_order_index > 0) --this->cur_implicit_order_index;

					/* InsertOrder disabled creation of implicit orders for all vehicles with the same implicit order.
					 * Reenable it for this vehicle */
					uint16 &gv_flags = this->GetGroundVehicleFlags();
					ClrBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
				}
			}
		}
		this->current_order.MakeLoading(false);
	}

	if (!no_load_prepare) {
		VehicleIncreaseStats(this);

		PrepareUnload(this);
	}

	SetWindowDirty(GetWindowClassForVehicleType(this->type), this->owner);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
	SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	SetWindowDirty(WC_STATION_VIEW, this->last_station_visited);

	Station::Get(this->last_station_visited)->MarkTilesDirty(true);
	this->cur_speed = 0;
	this->MarkDirty();
}

/**
 * Return all reserved cargo packets to the station and reset all packets
 * staged for transfer.
 * @param st the station where the reserved packets should go.
 */
void Vehicle::CancelReservation(StationID next, Station *st)
{
	for (Vehicle *v = this; v != NULL; v = v->next) {
		VehicleCargoList &cargo = v->cargo;
		if (cargo.ActionCount(VehicleCargoList::MTA_LOAD) > 0) {
			DEBUG(misc, 1, "cancelling cargo reservation");
			cargo.Return(UINT_MAX, &st->goods[v->cargo_type].cargo, next);
			cargo.SetTransferLoadPlace(st->xy);
		}
		cargo.KeepAll();
	}
}

CargoTypes Vehicle::GetLastLoadingStationValidCargoMask() const
{
	if (!HasBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP)) {
		return (this->last_loading_station != INVALID_STATION) ? ALL_CARGOTYPES : 0;
	} else {
		CargoTypes cargo_mask = 0;
		for (const Vehicle *u = this; u != NULL; u = u->Next()) {
			if (u->cargo_type < NUM_CARGO && u->last_loading_station != INVALID_STATION) {
				SetBit(cargo_mask, u->cargo_type);
			}
		}
		return cargo_mask;
	}
}

/**
 * Perform all actions when leaving a station.
 * @pre this->current_order.IsType(OT_LOADING)
 */
void Vehicle::LeaveStation()
{
	assert(this->current_order.IsAnyLoadingType());

	delete this->cargo_payment;
	assert(this->cargo_payment == NULL); // cleared by ~CargoPayment

	TileIndex station_tile = INVALID_TILE;

	if (this->type == VEH_TRAIN) {
		station_tile = Train::From(this)->GetStationLoadingVehicle()->tile;
		for (Train *v = Train::From(this); v != nullptr; v = v->Next()) {
			ClrBit(v->flags, VRF_BEYOND_PLATFORM_END);
			ClrBit(v->flags, VRF_NOT_YET_IN_PLATFORM);
			ClrBit(v->vehicle_flags, VF_CARGO_UNLOADING);
		}
	}

	/* Only update the timetable if the vehicle was supposed to stop here. */
	if (this->current_order.GetNonStopType() != ONSF_STOP_EVERYWHERE) UpdateVehicleTimetable(this, false);

	CargoTypes cargoes_can_load_unload = this->current_order.FilterLoadUnloadTypeCargoMask([&](const Order *o, CargoID cargo) {
		return ((o->GetCargoLoadType(cargo) & OLFB_NO_LOAD) == 0) || ((o->GetCargoUnloadType(cargo) & OUFB_NO_UNLOAD) == 0);
	});
	CargoTypes has_cargo_mask = this->GetLastLoadingStationValidCargoMask();
	CargoTypes cargoes_can_leave_with_cargo = FilterCargoMask([&](CargoID cargo) {
		return this->current_order.CanLeaveWithCargo(HasBit(has_cargo_mask, cargo), cargo);
	}, cargoes_can_load_unload);

	if (cargoes_can_load_unload != 0) {
		if (cargoes_can_leave_with_cargo != 0) {
			/* Refresh next hop stats to make sure we've done that at least once
			 * during the stop and that refit_cap == cargo_cap for each vehicle in
			 * the consist. */
			this->ResetRefitCaps();
			LinkRefresher::Run(this, true, false, cargoes_can_leave_with_cargo);
		}

		if (cargoes_can_leave_with_cargo == ALL_CARGOTYPES) {
			/* can leave with all cargoes */

			/* if the vehicle could load here or could stop with cargo loaded set the last loading station */
			this->last_loading_station = this->last_station_visited;
			ClrBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP);
		} else if (cargoes_can_leave_with_cargo == 0) {
			/* can leave with no cargoes */

			/* if the vehicle couldn't load and had to unload or transfer everything
			 * set the last loading station to invalid as it will leave empty. */
			this->last_loading_station = INVALID_STATION;
			ClrBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP);
		} else {
			/* mix of cargoes loadable or could not leave with all cargoes */

			/* NB: this is saved here as we overwrite it on the first iteration of the loop below */
			StationID head_last_loading_station = this->last_loading_station;
			for (Vehicle *u = this; u != NULL; u = u->Next()) {
				StationID last_loading_station = HasBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP) ? u->last_loading_station : head_last_loading_station;
				if (u->cargo_type < NUM_CARGO && HasBit(cargoes_can_load_unload, u->cargo_type)) {
					if (HasBit(cargoes_can_leave_with_cargo, u->cargo_type)) {
						u->last_loading_station = this->last_station_visited;
					} else {
						u->last_loading_station = INVALID_STATION;
					}
				} else {
					u->last_loading_station = last_loading_station;
				}
			}
			SetBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP);
		}
	}

	this->current_order.MakeLeaveStation();
	Station *st = Station::Get(this->last_station_visited);
	this->CancelReservation(INVALID_STATION, st);
	st->loading_vehicles.erase(std::remove(st->loading_vehicles.begin(), st->loading_vehicles.end(), this), st->loading_vehicles.end());

	HideFillingPercent(&this->fill_percent_te_id);
	trip_occupancy = CalcPercentVehicleFilled(this, NULL);

	if (this->type == VEH_TRAIN && !(this->vehstatus & VS_CRASHED)) {
		/* Trigger station animation (trains only) */
		if (IsRailStationTile(station_tile)) {
			TriggerStationRandomisation(st, station_tile, SRT_TRAIN_DEPARTS);
			TriggerStationAnimation(st, station_tile, SAT_TRAIN_DEPARTS);
		}

		SetBit(Train::From(this)->flags, VRF_LEAVING_STATION);
	}

	if (this->cur_real_order_index < this->GetNumOrders()) {
		Order *real_current_order = this->GetOrder(this->cur_real_order_index);
		uint current_occupancy = CalcPercentVehicleFilled(this, NULL);
		uint old_occupancy = real_current_order->GetOccupancy();
		uint new_occupancy;
		if (old_occupancy == 0) {
			new_occupancy = current_occupancy;
		} else {
			Company *owner = Company::GetIfValid(this->owner);
			uint8 occupancy_smoothness = owner ? owner->settings.order_occupancy_smoothness : 0;
			// Exponential weighted moving average using occupancy_smoothness
			new_occupancy = (old_occupancy - 1) * occupancy_smoothness;
			new_occupancy += current_occupancy * (100 - occupancy_smoothness);
			new_occupancy += 50; // round to nearest integer percent, rather than just floor
			new_occupancy /= 100;
		}
		if (new_occupancy + 1 != old_occupancy) {
			this->order_occupancy_average = 0;
			real_current_order->SetOccupancy(static_cast<uint8>(new_occupancy + 1));
			for (const Vehicle *v = this->FirstShared(); v != NULL; v = v->NextShared()) {
				SetWindowDirty(WC_VEHICLE_ORDERS, v->index);
			}
		}
	}

	this->MarkDirty();
}
/**
 * Perform all actions when switching to advancing within a station for loading/unloading
 * @pre this->current_order.IsType(OT_LOADING)
 * @pre this->type == VEH_TRAIN
 */
void Vehicle::AdvanceLoadingInStation()
{
	assert(this->current_order.IsType(OT_LOADING));
	assert(this->type == VEH_TRAIN);

	ClrBit(Train::From(this)->flags, VRF_ADVANCE_IN_PLATFORM);

	for (Train *v = Train::From(this); v != nullptr; v = v->Next()) {
		if (HasBit(v->flags, VRF_NOT_YET_IN_PLATFORM)) {
			ClrBit(v->flags, VRF_NOT_YET_IN_PLATFORM);
		} else {
			SetBit(v->flags, VRF_BEYOND_PLATFORM_END);
		}
	}

	HideFillingPercent(&this->fill_percent_te_id);
	this->current_order.MakeLoadingAdvance(this->last_station_visited);
	this->MarkDirty();
}

void Vehicle::RecalculateOrderOccupancyAverage()
{
	uint num_valid = 0;
	uint total = 0;
	uint order_count = this->GetNumOrders();
	for (uint i = 0; i < order_count; i++) {
		uint occupancy = this->GetOrder(i)->GetOccupancy();
		if (occupancy > 0) {
			num_valid++;
			total += (occupancy - 1);
		}
	}
	if (num_valid > 0) {
		this->order_occupancy_average = 16 + ((total + (num_valid / 2)) / num_valid);
	} else {
		this->order_occupancy_average = 1;
	}
}

/**
 * Reset all refit_cap in the consist to cargo_cap.
 */
void Vehicle::ResetRefitCaps()
{
	for (Vehicle *v = this; v != NULL; v = v->Next()) v->refit_cap = v->cargo_cap;
}

/**
 * Handle the loading of the vehicle; when not it skips through dummy
 * orders and does nothing in all other cases.
 * @param mode is the non-first call for this vehicle in this tick?
 */
void Vehicle::HandleLoading(bool mode)
{
	switch (this->current_order.GetType()) {
		case OT_LOADING: {
			uint wait_time = max(this->current_order.GetTimetabledWait() - this->lateness_counter, 0);

			/* Save time just loading took since that is what goes into the timetable */
			if (!HasBit(this->vehicle_flags, VF_LOADING_FINISHED)) {
				this->current_loading_time = this->current_order_time;
			}

			/* Pay the loading fee for using someone else's station, if appropriate */
			if (!mode && this->type != VEH_TRAIN) PayStationSharingFee(this, Station::Get(this->last_station_visited));

			/* Not the first call for this tick, or still loading */
			if (mode || !HasBit(this->vehicle_flags, VF_LOADING_FINISHED) || this->current_order_time < wait_time) {
				if (!mode && this->type == VEH_TRAIN && HasBit(Train::From(this)->flags, VRF_ADVANCE_IN_PLATFORM)) this->AdvanceLoadingInStation();
				return;
			}

			this->PlayLeaveStationSound();

			this->LeaveStation();

			/* Only advance to next order if we just loaded at the current one */
			const Order *order = this->GetOrder(this->cur_implicit_order_index);
			if (order == NULL ||
					(!order->IsType(OT_IMPLICIT) && !order->IsType(OT_GOTO_STATION)) ||
					order->GetDestination() != this->last_station_visited) {
				return;
			}
			break;
		}

		case OT_DUMMY: break;

		default: return;
	}

	this->IncrementImplicitOrderIndex();
}

/**
 * Handle the waiting time everywhere else as in stations (basically in depot but, eventually, also elsewhere ?)
 * Function is called when order's wait_time is defined.
 * @param stop_waiting should we stop waiting (or definitely avoid) even if there is still time left to wait ?
 */
void Vehicle::HandleWaiting(bool stop_waiting)
{
	switch (this->current_order.GetType()) {
		case OT_WAITING: {
			uint wait_time = max(this->current_order.GetTimetabledWait() - this->lateness_counter, 0);
			/* Vehicles holds on until waiting Timetabled time expires. */
			if (!stop_waiting && this->current_order_time < wait_time) {
				return;
			}

			/* When wait_time is expired, we move on. */
			UpdateVehicleTimetable(this, false);
			this->IncrementImplicitOrderIndex();
			this->current_order.MakeDummy();

			break;
		}

		default:
			return;
	}
}

/**
 * Send this vehicle to the depot using the given command(s).
 * @param flags   the command flags (like execute and such).
 * @param command the command to execute.
 * @return the cost of the depot action.
 */
CommandCost Vehicle::SendToDepot(DoCommandFlag flags, DepotCommand command, TileIndex specific_depot)
{
	CommandCost ret = CheckOwnership(this->owner);
	if (ret.Failed()) return ret;

	if (this->vehstatus & VS_CRASHED) return CMD_ERROR;
	if (this->IsStoppedInDepot()) return CMD_ERROR;

	auto cancel_order = [&]() {
		if (flags & DC_EXEC) {
			/* If the orders to 'goto depot' are in the orders list (forced servicing),
			 * then skip to the next order; effectively cancelling this forced service */
			if (this->current_order.GetDepotOrderType() & ODTFB_PART_OF_ORDERS) this->IncrementRealOrderIndex();

			if (this->IsGroundVehicle()) {
				uint16 &gv_flags = this->GetGroundVehicleFlags();
				SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
			}

			/* We don't cancel a breakdown-related goto depot order, we only change whether to halt or not */
			if (this->current_order.GetDepotOrderType() & ODTFB_BREAKDOWN) {
				this->current_order.SetDepotActionType(this->current_order.GetDepotActionType() == ODATFB_HALT ? ODATF_SERVICE_ONLY : ODATFB_HALT);
			} else {
				this->ClearSeparation();
				if (HasBit(this->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(this->vehicle_flags, VF_TIMETABLE_STARTED);

				this->current_order.MakeDummy();
				SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
			}

			/* prevent any attempt to update timetable for current order, as actual travel time will be incorrect due to depot command */
			this->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		}
	};

	if (command & DEPOT_CANCEL) {
		if (this->current_order.IsType(OT_GOTO_DEPOT)) {
			cancel_order();
			return CommandCost();
		} else {
			return CMD_ERROR;
		}
	}

	if (this->current_order.IsType(OT_GOTO_DEPOT) && !(command & DEPOT_SPECIFIC)) {
		bool halt_in_depot = (this->current_order.GetDepotActionType() & ODATFB_HALT) != 0;
		bool sell_in_depot = (this->current_order.GetDepotActionType() & ODATFB_SELL) != 0;
		if (!!(command & DEPOT_SERVICE) == halt_in_depot || !!(command & DEPOT_SELL) != sell_in_depot) {
			/* We called with a different DEPOT_SERVICE or DEPOT_SELL setting.
			 * Now we change the setting to apply the new one and let the vehicle head for the same depot.
			 * Note: the if is (true for requesting service == true for ordered to stop in depot)          */
			if (flags & DC_EXEC) {
				if (!(this->current_order.GetDepotOrderType() & ODTFB_BREAKDOWN)) this->current_order.SetDepotOrderType(ODTF_MANUAL);
				this->current_order.SetDepotActionType((command & DEPOT_SELL) ? ODATFB_HALT | ODATFB_SELL : ((command & DEPOT_SERVICE) ? ODATF_SERVICE_ONLY : ODATFB_HALT));
				this->ClearSeparation();
				if (HasBit(this->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(this->vehicle_flags, VF_TIMETABLE_STARTED);
				SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
			}
			return CommandCost();
		}

		if (command & DEPOT_DONT_CANCEL) return CMD_ERROR; // Requested no cancelation of depot orders
		cancel_order();
		return CommandCost();
	}

	TileIndex location;
	DestinationID destination;
	bool reverse;
	static const StringID no_depot[] = {STR_ERROR_UNABLE_TO_FIND_ROUTE_TO, STR_ERROR_UNABLE_TO_FIND_LOCAL_DEPOT, STR_ERROR_UNABLE_TO_FIND_LOCAL_DEPOT, STR_ERROR_CAN_T_SEND_AIRCRAFT_TO_HANGAR};
	if (command & DEPOT_SPECIFIC) {
		if (!(IsDepotTile(specific_depot) && GetDepotVehicleType(specific_depot) == this->type &&
				IsInfraTileUsageAllowed(this->type, this->owner, specific_depot))) {
			return_cmd_error(no_depot[this->type]);
		}
		location = specific_depot;
		destination = (this->type == VEH_AIRCRAFT) ? GetStationIndex(specific_depot) : GetDepotIndex(specific_depot);
		reverse = false;
	} else {
		if (!this->FindClosestDepot(&location, &destination, &reverse)) return_cmd_error(no_depot[this->type]);
	}

	if (flags & DC_EXEC) {
		if (this->current_order.IsAnyLoadingType()) this->LeaveStation();

		if (this->IsGroundVehicle() && this->GetNumManualOrders() > 0) {
			uint16 &gv_flags = this->GetGroundVehicleFlags();
			SetBit(gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
		}

		this->dest_tile = location;
		this->current_order.MakeGoToDepot(destination, ODTF_MANUAL);
		if (command & DEPOT_SELL) {
			this->current_order.SetDepotActionType(ODATFB_HALT | ODATFB_SELL);
		} else if (!(command & DEPOT_SERVICE)) {
			this->current_order.SetDepotActionType(ODATFB_HALT);
		}
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);

		/* If there is no depot in front, reverse automatically (trains only) */
		if (this->type == VEH_TRAIN && reverse) DoCommand(this->tile, this->index, 0, DC_EXEC, CMD_REVERSE_TRAIN_DIRECTION);

		if (this->type == VEH_AIRCRAFT) {
			Aircraft *a = Aircraft::From(this);
			if (a->state == FLYING && a->targetairport != destination) {
				/* The aircraft is now heading for a different hangar than the next in the orders */
				extern void AircraftNextAirportPos_and_Order(Aircraft *a);
				AircraftNextAirportPos_and_Order(a);
			}
		}
	}

	return CommandCost();

}

/**
 * Update the cached visual effect.
 * @param allow_power_change true if the wagon-is-powered-state may change.
 */
void Vehicle::UpdateVisualEffect(bool allow_power_change)
{
	bool powered_before = HasBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER);
	const Engine *e = this->GetEngine();

	/* Evaluate properties */
	byte visual_effect;
	switch (e->type) {
		case VEH_TRAIN: visual_effect = e->u.rail.visual_effect; break;
		case VEH_ROAD:  visual_effect = e->u.road.visual_effect; break;
		case VEH_SHIP:  visual_effect = e->u.ship.visual_effect; break;
		default:        visual_effect = 1 << VE_DISABLE_EFFECT;  break;
	}

	/* Check powered wagon / visual effect callback */
	if (HasBit(e->info.callback_mask, CBM_VEHICLE_VISUAL_EFFECT)) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_VISUAL_EFFECT, 0, 0, this->engine_type, this);

		if (callback != CALLBACK_FAILED) {
			if (callback >= 0x100 && e->GetGRF()->grf_version >= 8) ErrorUnknownCallbackResult(e->GetGRFID(), CBID_VEHICLE_VISUAL_EFFECT, callback);

			callback = GB(callback, 0, 8);
			/* Avoid accidentally setting 'visual_effect' to the default value
			 * Since bit 6 (disable effects) is set anyways, we can safely erase some bits. */
			if (callback == VE_DEFAULT) {
				assert(HasBit(callback, VE_DISABLE_EFFECT));
				SB(callback, VE_TYPE_START, VE_TYPE_COUNT, 0);
			}
			visual_effect = callback;
		}
	}

	/* Apply default values */
	if (visual_effect == VE_DEFAULT ||
			(!HasBit(visual_effect, VE_DISABLE_EFFECT) && GB(visual_effect, VE_TYPE_START, VE_TYPE_COUNT) == VE_TYPE_DEFAULT)) {
		/* Only train engines have default effects.
		 * Note: This is independent of whether the engine is a front engine or articulated part or whatever. */
		if (e->type != VEH_TRAIN || e->u.rail.railveh_type == RAILVEH_WAGON || !IsInsideMM(e->u.rail.engclass, EC_STEAM, EC_MONORAIL)) {
			if (visual_effect == VE_DEFAULT) {
				visual_effect = 1 << VE_DISABLE_EFFECT;
			} else {
				SetBit(visual_effect, VE_DISABLE_EFFECT);
			}
		} else {
			if (visual_effect == VE_DEFAULT) {
				/* Also set the offset */
				visual_effect = (VE_OFFSET_CENTRE - (e->u.rail.engclass == EC_STEAM ? 4 : 0)) << VE_OFFSET_START;
			}
			SB(visual_effect, VE_TYPE_START, VE_TYPE_COUNT, e->u.rail.engclass - EC_STEAM + VE_TYPE_STEAM);
		}
	}

	this->vcache.cached_vis_effect = visual_effect;

	if (!allow_power_change && powered_before != HasBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER)) {
		ToggleBit(this->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER);
		ShowNewGrfVehicleError(this->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_POWERED_WAGON, GBUG_VEH_POWERED_WAGON, false);
	}
}

static const int8 _vehicle_smoke_pos[8] = {
	1, 1, 1, 0, -1, -1, -1, 0
};

/**
 * Call CBID_VEHICLE_SPAWN_VISUAL_EFFECT and spawn requested effects.
 * @param v Vehicle to create effects for.
 */
static void SpawnAdvancedVisualEffect(const Vehicle *v)
{
	uint16 callback = GetVehicleCallback(CBID_VEHICLE_SPAWN_VISUAL_EFFECT, 0, Random(), v->engine_type, v);
	if (callback == CALLBACK_FAILED) return;

	uint count = GB(callback, 0, 2);
	bool auto_center = HasBit(callback, 13);
	bool auto_rotate = !HasBit(callback, 14);

	int8 l_center = 0;
	if (auto_center) {
		/* For road vehicles: Compute offset from vehicle position to vehicle center */
		if (v->type == VEH_ROAD) l_center = -(int)(VEHICLE_LENGTH - RoadVehicle::From(v)->gcache.cached_veh_length) / 2;
	} else {
		/* For trains: Compute offset from vehicle position to sprite position */
		if (v->type == VEH_TRAIN) l_center = (VEHICLE_LENGTH - Train::From(v)->gcache.cached_veh_length) / 2;
	}

	Direction l_dir = v->direction;
	if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) l_dir = ReverseDir(l_dir);
	Direction t_dir = ChangeDir(l_dir, DIRDIFF_90RIGHT);

	int8 x_center = _vehicle_smoke_pos[l_dir] * l_center;
	int8 y_center = _vehicle_smoke_pos[t_dir] * l_center;

	for (uint i = 0; i < count; i++) {
		uint32 reg = GetRegister(0x100 + i);
		uint type = GB(reg,  0, 8);
		int8 x    = GB(reg,  8, 8);
		int8 y    = GB(reg, 16, 8);
		int8 z    = GB(reg, 24, 8);

		if (auto_rotate) {
			int8 l = x;
			int8 t = y;
			x = _vehicle_smoke_pos[l_dir] * l + _vehicle_smoke_pos[t_dir] * t;
			y = _vehicle_smoke_pos[t_dir] * l - _vehicle_smoke_pos[l_dir] * t;
		}

		if (type >= 0xF0) {
			switch (type) {
				case 0xF1: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_STEAM_SMOKE); break;
				case 0xF2: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_DIESEL_SMOKE); break;
				case 0xF3: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_ELECTRIC_SPARK); break;
				case 0xFA: CreateEffectVehicleRel(v, x_center + x, y_center + y, z, EV_BREAKDOWN_SMOKE_AIRCRAFT); break;
				default: break;
			}
		}
	}
}

uint16 ReversingDistanceTargetSpeed(const Train *v);

/**
 * Draw visual effects (smoke and/or sparks) for a vehicle chain.
 * @pre this->IsPrimaryVehicle()
 */
void Vehicle::ShowVisualEffect() const
{
	assert(this->IsPrimaryVehicle());
	bool sound = false;

	/* Do not show any smoke when:
	 * - vehicle smoke is disabled by the player
	 * - the vehicle is slowing down or stopped (by the player)
	 * - the vehicle is moving very slowly
	 */
	if (_settings_game.vehicle.smoke_amount == 0 ||
			this->vehstatus & (VS_TRAIN_SLOWING | VS_STOPPED) ||
			this->cur_speed < 2) {
		return;
	}

	/* Use the speed as limited by underground and orders. */
	uint max_speed = this->GetCurrentMaxSpeed();

	if (this->type == VEH_TRAIN) {
		const Train *t = Train::From(this);
		/* For trains, do not show any smoke when:
		 * - the train is reversing
		 * - is entering a station with an order to stop there and its speed is equal to maximum station entering speed
		 * - is approaching a reversing point and its speed is equal to maximum approach speed
		 */
		if (HasBit(t->flags, VRF_REVERSING) ||
				(IsRailStationTile(t->tile) && t->IsFrontEngine() && t->current_order.ShouldStopAtStation(t, GetStationIndex(t->tile)) &&
				t->cur_speed >= max_speed) ||
				(t->reverse_distance >= 1 && t->cur_speed >= ReversingDistanceTargetSpeed(t))) {
			return;
		}
	}

	const Vehicle *v = this;

	do {
		bool advanced = HasBit(v->vcache.cached_vis_effect, VE_ADVANCED_EFFECT);
		int effect_offset = GB(v->vcache.cached_vis_effect, VE_OFFSET_START, VE_OFFSET_COUNT) - VE_OFFSET_CENTRE;
		VisualEffectSpawnModel effect_model = VESM_NONE;
		if (advanced) {
			effect_offset = VE_OFFSET_CENTRE;
			effect_model = (VisualEffectSpawnModel)GB(v->vcache.cached_vis_effect, 0, VE_ADVANCED_EFFECT);
			if (effect_model >= VESM_END) effect_model = VESM_NONE; // unknown spawning model
		} else {
			effect_model = (VisualEffectSpawnModel)GB(v->vcache.cached_vis_effect, VE_TYPE_START, VE_TYPE_COUNT);
			assert(effect_model != (VisualEffectSpawnModel)VE_TYPE_DEFAULT); // should have been resolved by UpdateVisualEffect
			assert_compile((uint)VESM_STEAM    == (uint)VE_TYPE_STEAM);
			assert_compile((uint)VESM_DIESEL   == (uint)VE_TYPE_DIESEL);
			assert_compile((uint)VESM_ELECTRIC == (uint)VE_TYPE_ELECTRIC);
		}

		/* Show no smoke when:
		 * - Smoke has been disabled for this vehicle
		 * - The vehicle is not visible
		 * - The vehicle is under a bridge
		 * - The vehicle is on a depot tile
		 * - The vehicle is on a tunnel tile
		 * - The vehicle is a train engine that is currently unpowered */
		if (effect_model == VESM_NONE ||
				v->vehstatus & VS_HIDDEN ||
				IsBridgeAbove(v->tile) ||
				IsDepotTile(v->tile) ||
				IsTunnelTile(v->tile) ||
				(v->type == VEH_TRAIN &&
				!HasPowerOnRail(Train::From(v)->railtype, GetTileRailType(v->tile)))) {
			continue;
		}

		EffectVehicleType evt = EV_END;
		switch (effect_model) {
			case VESM_STEAM:
				/* Steam smoke - amount is gradually falling until vehicle reaches its maximum speed, after that it's normal.
				 * Details: while vehicle's current speed is gradually increasing, steam plumes' density decreases by one third each
				 * third of its maximum speed spectrum. Steam emission finally normalises at very close to vehicle's maximum speed.
				 * REGULATION:
				 * - instead of 1, 4 / 2^smoke_amount (max. 2) is used to provide sufficient regulation to steam puffs' amount. */
				if (GB(v->tick_counter, 0, ((4 >> _settings_game.vehicle.smoke_amount) + ((this->cur_speed * 3) / max_speed))) == 0) {
					evt = EV_STEAM_SMOKE;
				}
				break;

			case VESM_DIESEL: {
				/* Diesel smoke - thicker when vehicle is starting, gradually subsiding till it reaches its maximum speed
				 * when smoke emission stops.
				 * Details: Vehicle's (max.) speed spectrum is divided into 32 parts. When max. speed is reached, chance for smoke
				 * emission erodes by 32 (1/4). For trains, power and weight come in handy too to either increase smoke emission in
				 * 6 steps (1000HP each) if the power is low or decrease smoke emission in 6 steps (512 tonnes each) if the train
				 * isn't overweight. Power and weight contributions are expressed in a way that neither extreme power, nor
				 * extreme weight can ruin the balance (e.g. FreightWagonMultiplier) in the formula. When the vehicle reaches
				 * maximum speed no diesel_smoke is emitted.
				 * REGULATION:
				 * - up to which speed a diesel vehicle is emitting smoke (with reduced/small setting only until 1/2 of max_speed),
				 * - in Chance16 - the last value is 512 / 2^smoke_amount (max. smoke when 128 = smoke_amount of 2). */
				int power_weight_effect = 0;
				if (v->type == VEH_TRAIN) {
					power_weight_effect = (32 >> (Train::From(this)->gcache.cached_power >> 10)) - (32 >> (Train::From(this)->gcache.cached_weight >> 9));
				}
				if (this->cur_speed < (max_speed >> (2 >> _settings_game.vehicle.smoke_amount)) &&
						Chance16((64 - ((this->cur_speed << 5) / max_speed) + power_weight_effect), (512 >> _settings_game.vehicle.smoke_amount))) {
					evt = EV_DIESEL_SMOKE;
				}
				break;
			}

			case VESM_ELECTRIC:
				/* Electric train's spark - more often occurs when train is departing (more load)
				 * Details: Electric locomotives are usually at least twice as powerful as their diesel counterparts, so spark
				 * emissions are kept simple. Only when starting, creating huge force are sparks more likely to happen, but when
				 * reaching its max. speed, quarter by quarter of it, chance decreases until the usual 2,22% at train's top speed.
				 * REGULATION:
				 * - in Chance16 the last value is 360 / 2^smoke_amount (max. sparks when 90 = smoke_amount of 2). */
				if (GB(v->tick_counter, 0, 2) == 0 &&
						Chance16((6 - ((this->cur_speed << 2) / max_speed)), (360 >> _settings_game.vehicle.smoke_amount))) {
					evt = EV_ELECTRIC_SPARK;
				}
				break;

			default:
				NOT_REACHED();
		}

		if (evt != EV_END && advanced) {
			sound = true;
			SpawnAdvancedVisualEffect(v);
		} else if (evt != EV_END) {
			sound = true;

			/* The effect offset is relative to a point 4 units behind the vehicle's
			 * front (which is the center of an 8/8 vehicle). Shorter vehicles need a
			 * correction factor. */
			if (v->type == VEH_TRAIN) effect_offset += (VEHICLE_LENGTH - Train::From(v)->gcache.cached_veh_length) / 2;

			int x = _vehicle_smoke_pos[v->direction] * effect_offset;
			int y = _vehicle_smoke_pos[(v->direction + 2) % 8] * effect_offset;

			if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_REVERSE_DIRECTION)) {
				x = -x;
				y = -y;
			}

			CreateEffectVehicleRel(v, x, y, 10, evt);
		}
	} while ((v = v->Next()) != NULL);

	if (sound) PlayVehicleSound(this, VSE_VISUAL_EFFECT);
}

/**
 * Set the next vehicle of this vehicle.
 * @param next the next vehicle. NULL removes the next vehicle.
 */
void Vehicle::SetNext(Vehicle *next)
{
	assert(this != next);

	if (this->next != NULL) {
		/* We had an old next vehicle. Update the first and previous pointers */
		for (Vehicle *v = this->next; v != NULL; v = v->Next()) {
			v->first = this->next;
		}
		this->next->previous = NULL;
	}

	this->next = next;

	if (this->next != NULL) {
		/* A new next vehicle. Update the first and previous pointers */
		if (this->next->previous != NULL) this->next->previous->next = NULL;
		this->next->previous = this;
		for (Vehicle *v = this->next; v != NULL; v = v->Next()) {
			v->first = this->first;
		}
	}
}

void Vehicle::ClearSeparation()
{
	if (this->ahead_separation == NULL && this->behind_separation == NULL) return;

	assert(this->ahead_separation != NULL);
	assert(this->behind_separation != NULL);

	this->ahead_separation->behind_separation = this->behind_separation;
	this->behind_separation->ahead_separation = this->ahead_separation;

	this->ahead_separation = NULL;
	this->behind_separation = NULL;

	SetWindowDirty(WC_VEHICLE_TIMETABLE, this->index);
}

void Vehicle::InitSeparation()
{
	assert(this->ahead_separation == NULL && this->behind_separation == NULL);
	Vehicle *best_match = this;
	int lowest_separation;
	for (Vehicle *v_other = this->FirstShared(); v_other != NULL; v_other = v_other->NextShared()) {
		if ((HasBit(v_other->vehicle_flags, VF_TIMETABLE_STARTED)) && v_other != this) {
			if (best_match == this) {
				best_match = v_other;
				lowest_separation = 0; // TODO call SeparationBetween() here
			} else {
				int temp_sep = 0; // TODO call SeparationBetween() here
				if (temp_sep < lowest_separation && temp_sep != -1) {
					best_match = v_other;
					lowest_separation = temp_sep;
				}
			}
		}
	}
	this->AddToSeparationBehind(best_match);
}

void Vehicle::AddToSeparationBehind(Vehicle *v_other)
{
	if (v_other->ahead_separation == NULL) v_other->ahead_separation = v_other;
	if (v_other->behind_separation == NULL) v_other->behind_separation = v_other;

	this->ahead_separation = v_other;
	v_other->behind_separation->ahead_separation = this;
	this->behind_separation = v_other->behind_separation;
	v_other->behind_separation = this;
}

/**
 * Adds this vehicle to a shared vehicle chain.
 * @param shared_chain a vehicle of the chain with shared vehicles.
 * @pre !this->IsOrderListShared()
 */
void Vehicle::AddToShared(Vehicle *shared_chain)
{
	assert(this->previous_shared == NULL && this->next_shared == NULL);

	if (shared_chain->orders.list == NULL) {
		assert(shared_chain->previous_shared == NULL);
		assert(shared_chain->next_shared == NULL);
		this->orders.list = shared_chain->orders.list = new OrderList(NULL, shared_chain);
	}

	this->next_shared     = shared_chain->next_shared;
	this->previous_shared = shared_chain;

	shared_chain->next_shared = this;

	if (this->next_shared != NULL) this->next_shared->previous_shared = this;

	shared_chain->orders.list->AddVehicle(this);
}

/**
 * Removes the vehicle from the shared order list.
 */
void Vehicle::RemoveFromShared()
{
	/* Remember if we were first and the old window number before RemoveVehicle()
	 * as this changes first if needed. */
	bool were_first = (this->FirstShared() == this);
	VehicleListIdentifier vli(VL_SHARED_ORDERS, this->type, this->owner, this->FirstShared()->index);

	this->orders.list->RemoveVehicle(this);

	if (!were_first) {
		/* We are not the first shared one, so only relink our previous one. */
		this->previous_shared->next_shared = this->NextShared();
	}

	if (this->next_shared != NULL) this->next_shared->previous_shared = this->previous_shared;


	if (this->orders.list->GetNumVehicles() == 1) InvalidateVehicleOrder(this->FirstShared(), VIWD_MODIFY_ORDERS);

	if (this->orders.list->GetNumVehicles() == 1 && !_settings_client.gui.enable_single_veh_shared_order_gui) {
		/* When there is only one vehicle, remove the shared order list window. */
		DeleteWindowById(GetWindowClassForVehicleType(this->type), vli.Pack());
	} else if (were_first) {
		/* If we were the first one, update to the new first one.
		 * Note: FirstShared() is already the new first */
		InvalidateWindowData(GetWindowClassForVehicleType(this->type), vli.Pack(), this->FirstShared()->index | (1U << 31));
	}

	this->next_shared     = NULL;
	this->previous_shared = NULL;

	this->ClearSeparation();
	if (HasBit(this->vehicle_flags, VF_TIMETABLE_SEPARATION)) ClrBit(this->vehicle_flags, VF_TIMETABLE_STARTED);
}

char *Vehicle::DumpVehicleFlags(char *b, const char *last) const
{
	auto dump = [&](char c, bool flag) {
		if (flag) b += seprintf(b, last, "%c", c);
	};
	b += seprintf(b, last, "st:");
	dump('F', HasBit(this->subtype, GVSF_FRONT));
	dump('A', HasBit(this->subtype, GVSF_ARTICULATED_PART));
	dump('W', HasBit(this->subtype, GVSF_WAGON));
	dump('E', HasBit(this->subtype, GVSF_ENGINE));
	dump('f', HasBit(this->subtype, GVSF_FREE_WAGON));
	dump('M', HasBit(this->subtype, GVSF_MULTIHEADED));
	dump('V', HasBit(this->subtype, GVSF_VIRTUAL));
	b += seprintf(b, last, ", vs:");
	dump('H', this->vehstatus & VS_HIDDEN);
	dump('S', this->vehstatus & VS_STOPPED);
	dump('U', this->vehstatus & VS_UNCLICKABLE);
	dump('D', this->vehstatus & VS_DEFPAL);
	dump('s', this->vehstatus & VS_TRAIN_SLOWING);
	dump('X', this->vehstatus & VS_SHADOW);
	dump('B', this->vehstatus & VS_AIRCRAFT_BROKEN);
	dump('C', this->vehstatus & VS_CRASHED);
	b += seprintf(b, last, ", vf:");
	dump('F', HasBit(this->vehicle_flags, VF_LOADING_FINISHED));
	dump('U', HasBit(this->vehicle_flags, VF_CARGO_UNLOADING));
	dump('P', HasBit(this->vehicle_flags, VF_BUILT_AS_PROTOTYPE));
	dump('T', HasBit(this->vehicle_flags, VF_TIMETABLE_STARTED));
	dump('A', HasBit(this->vehicle_flags, VF_AUTOFILL_TIMETABLE));
	dump('w', HasBit(this->vehicle_flags, VF_AUTOFILL_PRES_WAIT_TIME));
	dump('S', HasBit(this->vehicle_flags, VF_STOP_LOADING));
	dump('L', HasBit(this->vehicle_flags, VF_PATHFINDER_LOST));
	dump('c', HasBit(this->vehicle_flags, VF_SERVINT_IS_CUSTOM));
	dump('p', HasBit(this->vehicle_flags, VF_SERVINT_IS_PERCENT));
	dump('D', HasBit(this->vehicle_flags, VF_SCHEDULED_DISPATCH));
	dump('x', HasBit(this->vehicle_flags, VF_LAST_LOAD_ST_SEP));
	dump('s', HasBit(this->vehicle_flags, VF_TIMETABLE_SEPARATION));
	dump('a', HasBit(this->vehicle_flags, VF_AUTOMATE_TIMETABLE));
	if (this->type == VEH_TRAIN) {
		const Train *t = Train::From(this);
		b += seprintf(b, last, ", tf:");
		dump('R', HasBit(t->flags, VRF_REVERSING));
		dump('W', HasBit(t->flags, VRF_WAITING_RESTRICTION));
		dump('S', HasBit(t->flags, VRF_HAVE_SLOT));
		dump('P', HasBit(t->flags, VRF_POWEREDWAGON));
		dump('r', HasBit(t->flags, VRF_REVERSE_DIRECTION));
		dump('h', HasBit(t->flags, VRF_HAS_HIT_RV));
		dump('e', HasBit(t->flags, VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL));
		dump('q', HasBit(t->flags, VRF_TOGGLE_REVERSE));
		dump('s', HasBit(t->flags, VRF_TRAIN_STUCK));
		dump('L', HasBit(t->flags, VRF_LEAVING_STATION));
		dump('b', HasBit(t->flags, VRF_BREAKDOWN_BRAKING));
		dump('p', HasBit(t->flags, VRF_BREAKDOWN_POWER));
		dump('v', HasBit(t->flags, VRF_BREAKDOWN_SPEED));
		dump('z', HasBit(t->flags, VRF_BREAKDOWN_STOPPED));
		dump('F', HasBit(t->flags, VRF_NEED_REPAIR));
		dump('H', HasBit(t->flags, VRF_TOO_HEAVY));
		dump('B', HasBit(t->flags, VRF_BEYOND_PLATFORM_END));
		dump('Y', HasBit(t->flags, VRF_NOT_YET_IN_PLATFORM));
		dump('A', HasBit(t->flags, VRF_ADVANCE_IN_PLATFORM));
	} else if (this->type == VEH_ROAD) {
		const RoadVehicle *r = RoadVehicle::From(this);
		b += seprintf(b, last, ", rvs:%X, rvf:%X", r->state, r->frame);
	}
	b += seprintf(b, last, ", t:%X", this->tile);
	if (this->cargo_payment) b += seprintf(b, last, ", CP");
	return b;
}


void VehiclesYearlyLoop()
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (v->IsPrimaryVehicle()) {
			/* show warning if vehicle is not generating enough income last 2 years (corresponds to a red icon in the vehicle list) */
			Money profit = v->GetDisplayProfitThisYear();
			if (v->age >= 730 && profit < 0) {
				if (_settings_client.gui.vehicle_income_warn && v->owner == _local_company) {
					SetDParam(0, v->index);
					SetDParam(1, profit);
					AddVehicleAdviceNewsItem(STR_NEWS_VEHICLE_IS_UNPROFITABLE, v->index);
				}
				AI::NewEvent(v->owner, new ScriptEventVehicleUnprofitable(v->index));
			}

			v->profit_last_year = v->profit_this_year;
			v->profit_lifetime += v->profit_this_year;
			v->profit_this_year = 0;
			SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
		}
	}
	GroupStatistics::UpdateProfits();
	SetWindowClassesDirty(WC_TRAINS_LIST);
	SetWindowClassesDirty(WC_SHIPS_LIST);
	SetWindowClassesDirty(WC_ROADVEH_LIST);
	SetWindowClassesDirty(WC_AIRCRAFT_LIST);
}


/**
 * Can this station be used by the given engine type?
 * @param engine_type the type of vehicles to test
 * @param st the station to test for
 * @return true if and only if the vehicle of the type can use this station.
 * @note For road vehicles the Vehicle is needed to determine whether it can
 *       use the station. This function will return true for road vehicles
 *       when at least one of the facilities is available.
 */
bool CanVehicleUseStation(EngineID engine_type, const Station *st)
{
	const Engine *e = Engine::GetIfValid(engine_type);
	assert(e != NULL);

	switch (e->type) {
		case VEH_TRAIN:
			return (st->facilities & FACIL_TRAIN) != 0;

		case VEH_ROAD:
			/* For road vehicles we need the vehicle to know whether it can actually
			 * use the station, but if it doesn't have facilities for RVs it is
			 * certainly not possible that the station can be used. */
			return (st->facilities & (FACIL_BUS_STOP | FACIL_TRUCK_STOP)) != 0;

		case VEH_SHIP:
			return (st->facilities & FACIL_DOCK) != 0;

		case VEH_AIRCRAFT:
			return (st->facilities & FACIL_AIRPORT) != 0 &&
					(st->airport.GetFTA()->flags & (e->u.air.subtype & AIR_CTOL ? AirportFTAClass::AIRPLANES : AirportFTAClass::HELICOPTERS)) != 0;

		default:
			return false;
	}
}

/**
 * Can this station be used by the given vehicle?
 * @param v the vehicle to test
 * @param st the station to test for
 * @return true if and only if the vehicle can use this station.
 */
bool CanVehicleUseStation(const Vehicle *v, const Station *st)
{
	if (v->type == VEH_ROAD) return st->GetPrimaryRoadStop(RoadVehicle::From(v)) != NULL;

	return CanVehicleUseStation(v->engine_type, st);
}

/**
 * Access the ground vehicle cache of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleCache of the vehicle.
 */
GroundVehicleCache *Vehicle::GetGroundVehicleCache()
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return &Train::From(this)->gcache;
	} else {
		return &RoadVehicle::From(this)->gcache;
	}
}

/**
 * Access the ground vehicle cache of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleCache of the vehicle.
 */
const GroundVehicleCache *Vehicle::GetGroundVehicleCache() const
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return &Train::From(this)->gcache;
	} else {
		return &RoadVehicle::From(this)->gcache;
	}
}

/**
 * Access the ground vehicle flags of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleFlags of the vehicle.
 */
uint16 &Vehicle::GetGroundVehicleFlags()
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return Train::From(this)->gv_flags;
	} else {
		return RoadVehicle::From(this)->gv_flags;
	}
}

/**
 * Access the ground vehicle flags of the vehicle.
 * @pre The vehicle is a #GroundVehicle.
 * @return #GroundVehicleFlags of the vehicle.
 */
const uint16 &Vehicle::GetGroundVehicleFlags() const
{
	assert(this->IsGroundVehicle());
	if (this->type == VEH_TRAIN) {
		return Train::From(this)->gv_flags;
	} else {
		return RoadVehicle::From(this)->gv_flags;
	}
}

/**
 * Calculates the set of vehicles that will be affected by a given selection.
 * @param set [inout] Set of affected vehicles.
 * @param v First vehicle of the selection.
 * @param num_vehicles Number of vehicles in the selection (not counting articulated parts).
 * @pre \a set must be empty.
 * @post \a set will contain the vehicles that will be refitted.
 */
void GetVehicleSet(VehicleSet &set, Vehicle *v, uint8 num_vehicles)
{
	if (v->type == VEH_TRAIN) {
		Train *u = Train::From(v);
		/* Only include whole vehicles, so start with the first articulated part */
		u = u->GetFirstEnginePart();

		/* Include num_vehicles vehicles, not counting articulated parts */
		for (; u != NULL && num_vehicles > 0; num_vehicles--) {
			do {
				/* Include current vehicle in the selection. */
				set.Include(u->index);

				/* If the vehicle is multiheaded, add the other part too. */
				if (u->IsMultiheaded()) set.Include(u->other_multiheaded_part->index);

				u = u->Next();
			} while (u != NULL && u->IsArticulatedPart());
		}
	}
}
