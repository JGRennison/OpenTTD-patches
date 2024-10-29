/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_cmd.cpp Handling of trains. */

#include "stdafx.h"
#include "error.h"
#include "articulated_vehicles.h"
#include "command_func.h"
#include "pathfinder/yapf/yapf.hpp"
#include "news_func.h"
#include "company_func.h"
#include "newgrf_sound.h"
#include "newgrf_text.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "newgrf_station.h"
#include "effectvehicle_func.h"
#include "network/network.h"
#include "spritecache.h"
#include "core/random_func.hpp"
#include "company_base.h"
#include "newgrf.h"
#include "infrastructure_func.h"
#include "order_backup.h"
#include "zoom_func.h"
#include "newgrf_debug.h"
#include "framerate_type.h"
#include "tracerestrict.h"
#include "tbtr_template_vehicle_func.h"
#include "autoreplace_func.h"
#include "engine_func.h"
#include "bridge_signal_map.h"
#include "scope_info.h"
#include "scope.h"
#include "core/checksum_func.hpp"
#include "debug_dbg_assert.h"
#include "debug_settings.h"
#include "train_speed_adaptation.h"
#include "event_logs.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include "table/strings.h"
#include "table/train_cmd.h"

#include "safeguards.h"

extern btree::btree_multimap<VehicleID, PendingSpeedRestrictionChange> _pending_speed_restriction_change_map;

enum {
	REALISTIC_BRAKING_MIN_SPEED = 5,
};

enum ChooseTrainTrackLookAheadStateFlags {
	CTTLASF_STOP_FOUND       = 0,         ///< Stopping destination found
	CTTLASF_REVERSE_FOUND    = 1,         ///< Reverse destination found
	CTTLASF_NO_RES_VEH_TILE  = 2,         ///< Do not reserve the vehicle tile
};

struct ChooseTrainTrackLookAheadState {
	uint          order_items_start = 0;  ///< Order items start for VehicleOrderSaver
	uint16_t      flags = 0;              ///< Flags
	DestinationID reverse_dest = 0;       ///< Reverse station ID when CTTLASF_REVERSE_FOUND is set
};

/** Flags for ChooseTrainTrack */
enum ChooseTrainTrackFlags {
	CTTF_NONE                   = 0,      ///< No flags
	CTTF_FORCE_RES              = 0x01,   ///< Force a reservation to be made
	CTTF_MARK_STUCK             = 0x02,   ///< The train has to be marked as stuck when needed
	CTTF_NON_LOOKAHEAD          = 0x04,   ///< Any lookahead should not be used, if necessary reset the lookahead state
	CTTF_NO_LOOKAHEAD_VALIDATE  = 0x08,   ///< Don't validate the lookahead state as it has already been done
};
DECLARE_ENUM_AS_BIT_SET(ChooseTrainTrackFlags)

/** Result flags for ChooseTrainTrack */
enum ChooseTrainTrackResultFlags {
	CTTRF_NONE                  = 0,      ///< No flags
	CTTRF_RESERVATION_MADE      = 0x01,   ///< A reservation was made
	CTTRF_REVERSE_AT_SIGNAL     = 0x02,   ///< Reverse at signal
};
DECLARE_ENUM_AS_BIT_SET(ChooseTrainTrackResultFlags)

struct ChooseTrainTrackResult {
	Track track;
	ChooseTrainTrackResultFlags ctt_flags;
};

btree::btree_map<SignalSpeedKey, SignalSpeedValue> _signal_speeds;

static void TryLongReserveChooseTrainTrackFromReservationEnd(Train *v, bool no_reserve_vehicle_tile = false);
static ChooseTrainTrackResult ChooseTrainTrack(Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, ChooseTrainTrackFlags flags, ChooseTrainTrackLookAheadState lookahead_state = {});
static bool TrainApproachingLineEnd(Train *v, bool signal, bool reverse);
static bool TrainCheckIfLineEnds(Train *v, bool reverse = true);
static bool TrainCanLeaveTile(const Train *v);
static inline bool CheckCompatibleRail(const Train *v, TileIndex tile, DiagDirection enterdir);
int ReversingDistanceTargetSpeed(const Train *v);
bool TrainController(Train *v, Vehicle *nomove, bool reverse = true); // Also used in vehicle_sl.cpp.
static TileIndex TrainApproachingCrossingTile(const Train *v);
static void CheckIfTrainNeedsService(Train *v);
static void CheckNextTrainTile(Train *v);
extern TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode);
static void TrainEnterStation(Train *v, StationID station);
static void UnreserveBridgeTunnelTile(TileIndex tile);
static bool CheckTrainStayInWormHolePathReserve(Train *t, TileIndex tile);

/** Return the scaled date ticks by which the speed restriction
 *  at the current position of the train is going to be invalid */
static StateTicks GetSpeedRestrictionTimeout(const Train *t)
{
	const int64_t velocity = std::max<int64_t>(25, t->cur_speed);
	const int64_t look_ahead_distance = Clamp(t->cur_speed / 8, 4, 16); // In tiles, varying between 4 and 16 depending on current speed

	// This assumes travel along the X or Y map axis, not diagonally. See GetAdvanceDistance, GetAdvanceSpeed.
	const int64_t ticks_per_tile = (192 * 16 * 4 / 3) / velocity;

	const int64_t ticks = ticks_per_tile * look_ahead_distance;

	return _state_ticks + ticks;
}

/** Removes all speed restrictions from all signals */
void ClearAllSignalSpeedRestrictions()
{
	_signal_speeds.clear();
}

void AdjustAllSignalSpeedRestrictionTickValues(StateTicksDelta delta)
{
	for (auto &it : _signal_speeds) {
		it.second.time_stamp += delta;
	}
}

/** Removes all speed restrictions which have passed their timeout from all signals */
void ClearOutOfDateSignalSpeedRestrictions()
{
	for (auto key_value_pair = _signal_speeds.begin(); key_value_pair != _signal_speeds.end(); ) {
		if (key_value_pair->second.IsOutOfDate()) {
			key_value_pair = _signal_speeds.erase(key_value_pair);
		} else {
			++key_value_pair;
		}
	}
}

inline void ClearLookAheadIfInvalid(Train *v)
{
	if (v->lookahead != nullptr && !ValidateLookAhead(v)) v->lookahead.reset();
}

static const uint8_t _vehicle_initial_x_fract[4] = {10, 8, 4,  8};
static const uint8_t _vehicle_initial_y_fract[4] = { 8, 4, 8, 10};

template <>
bool IsValidImageIndex<VEH_TRAIN>(uint8_t image_index)
{
	return image_index < lengthof(_engine_sprite_base);
}


/**
 * Return the cargo weight multiplier to use for a rail vehicle
 * @param cargo Cargo type to get multiplier for
 * @return Cargo weight multiplier
 */
uint8_t FreightWagonMult(CargoID cargo)
{
	if (!CargoSpec::Get(cargo)->is_freight) return 1;
	return _settings_game.vehicle.freight_trains;
}

/** Checks if lengths of all rail vehicles are valid. If not, shows an error message. */
void CheckTrainsLengths()
{
	bool first = true;

	for (const Train *v : Train::IterateFrontOnly()) {
		if (!(v->vehstatus & VS_CRASHED) && !v->IsVirtual()) {
			for (const Train *u = v, *w = v->Next(); w != nullptr; u = w, w = w->Next()) {
				if (u->track != TRACK_BIT_DEPOT) {
					if ((w->track != TRACK_BIT_DEPOT &&
							std::max(abs(u->x_pos - w->x_pos), abs(u->y_pos - w->y_pos)) != u->CalcNextVehicleOffset()) ||
							(w->track == TRACK_BIT_DEPOT && TicksToLeaveDepot(u) <= 0)) {
						SetDParam(0, v->index);
						SetDParam(1, v->owner);
						ShowErrorMessage(STR_BROKEN_VEHICLE_LENGTH, INVALID_STRING_ID, WL_CRITICAL);

						if (!_networking && first) {
							first = false;
							DoCommandP(0, PM_PAUSED_ERROR, 1, CMD_PAUSE);
						}
						/* Break so we warn only once for each train. */
						break;
					}
				}
			}
		}
	}
}

/**
 * Checks the breakdown flags (VehicleRailFlags 9-12) and sets the correct value in the first vehicle of the consist.
 * This function is generally only called to check if a flag may be cleared.
 * @param v the front engine
 * @param flags bitmask of the flags to check.
 */
void CheckBreakdownFlags(Train *v)
{
	dbg_assert(v->IsFrontEngine());
	/* clear the flags we're gonna check first, we'll set them again later (if applicable) */
	CLRBITS(v->flags, (1 << VRF_BREAKDOWN_BRAKING) | VRF_IS_BROKEN);

	for (const Train *w = v; w != nullptr; w = w->Next()) {
		if (v->IsEngine() || w->IsMultiheaded()) {
			if (w->breakdown_ctr == 2) {
				SetBit(v->flags, VRF_BREAKDOWN_BRAKING);
			} else if (w->breakdown_ctr == 1) {
				switch (w->breakdown_type) {
					case BREAKDOWN_CRITICAL:
					case BREAKDOWN_RV_CRASH:
					case BREAKDOWN_EM_STOP:   SetBit(v->flags, VRF_BREAKDOWN_STOPPED); break;
					case BREAKDOWN_LOW_SPEED: SetBit(v->flags, VRF_BREAKDOWN_SPEED);   break;
					case BREAKDOWN_LOW_POWER: SetBit(v->flags, VRF_BREAKDOWN_POWER);   break;
				}
			}
		}
	}
}

uint16_t GetTrainVehicleMaxSpeed(const Train *u, const RailVehicleInfo *rvi_u, const Train *front)
{
	const uint16_t base_speed = GetVehicleProperty(u, PROP_TRAIN_SPEED, rvi_u->max_speed);
	uint16_t speed = base_speed;
	if (HasBit(u->flags, VRF_NEED_REPAIR) && front->IsFrontEngine()) {
		for (uint i = 0; i < u->critical_breakdown_count; i++) {
			speed = std::min<uint16_t>(speed - (speed / (front->tcache.cached_num_engines + 2)) + 1, speed);
		}
	}

	/* clamp speed to be no less than lower of 5mph and 1/8 of base speed */
	speed = std::max<uint16_t>(speed, std::min<uint16_t>(5, (base_speed + 7) >> 3));

	if (HasBit(u->flags, VRF_HAS_HIT_RV) && front->IsFrontEngine()) {
		speed = std::min<uint16_t>(speed, 30);
	}
	return speed;
}

/**
 * Recalculates the cached stuff of a train. Should be called each time a vehicle is added
 * to/removed from the chain, and when the game is loaded.
 * Note: this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @param allowed_changes Stuff that is allowed to change.
 */
void Train::ConsistChanged(ConsistChangeFlags allowed_changes)
{
	uint16_t max_speed = UINT16_MAX;

	dbg_assert(this->IsFrontEngine() || this->IsFreeWagon());

	const RailVehicleInfo *rvi_v = RailVehInfo(this->engine_type);
	EngineID first_engine = this->IsFrontEngine() ? this->engine_type : INVALID_ENGINE;
	this->gcache.cached_total_length = 0;
	this->compatible_railtypes = RAILTYPES_NONE;
	this->tcache.cached_num_engines = 0;

	bool train_can_tilt = true;
	bool speed_varies_by_railtype = false;
	int16_t min_curve_speed_mod = INT16_MAX;

	for (Train *u = this; u != nullptr; u = u->Next()) {
		const RailVehicleInfo *rvi_u = RailVehInfo(u->engine_type);

		/* Check the this->first cache. */
		dbg_assert_msg(u->First() == this, "u: {}, this: {}",
				VehicleInfoDumper(u), VehicleInfoDumper(this));

		/* update the 'first engine' */
		u->gcache.first_engine = this == u ? INVALID_ENGINE : first_engine;
		u->railtype = rvi_u->railtype;

		if (u->IsEngine()) first_engine = u->engine_type;

		/* Set user defined data to its default value */
		u->tcache.user_def_data = rvi_u->user_def_data;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}

	for (Train *u = this; u != nullptr; u = u->Next()) {
		/* Update user defined data (must be done before other properties) */
		u->tcache.user_def_data = GetVehicleProperty(u, PROP_TRAIN_USER_DATA, u->tcache.user_def_data);
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();

		if (!u->IsArticulatedPart()) {
			if (u->IsEngine() || u->IsMultiheaded()) {
				this->tcache.cached_num_engines++;
			}
		}
	}

	Vehicle *last_vis_effect = this;
	for (Train *u = this; u != nullptr; u = u->Next()) {
		const Engine *e_u = u->GetEngine();
		const RailVehicleInfo *rvi_u = &e_u->u.rail;

		if (!HasBit(e_u->info.misc_flags, EF_RAIL_TILTS)) train_can_tilt = false;
		if (e_u->callbacks_used & SGCU_CB36_SPEED_RAILTYPE) speed_varies_by_railtype = true;
		min_curve_speed_mod = std::min(min_curve_speed_mod, u->GetCurveSpeedModifier());

		/* Cache wagon override sprite group. nullptr is returned if there is none */
		u->tcache.cached_override = GetWagonOverrideSpriteSet(u->engine_type, u->cargo_type, u->gcache.first_engine);

		/* Reset colour map */
		u->colourmap = PAL_NONE;

		/* Update powered-wagon-status and visual effect */
		u->UpdateVisualEffect(true);
		ClrBit(u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);
		if (!(HasBit(u->vcache.cached_vis_effect, VE_ADVANCED_EFFECT) && GB(u->vcache.cached_vis_effect, 0, VE_ADVANCED_EFFECT) == VESM_NONE)) last_vis_effect = u;

		if (rvi_v->pow_wag_power != 0 && rvi_u->railveh_type == RAILVEH_WAGON &&
				UsesWagonOverride(u) && !HasBit(u->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER)) {
			/* wagon is powered */
			SetBit(u->flags, VRF_POWEREDWAGON); // cache 'powered' status
		} else {
			ClrBit(u->flags, VRF_POWEREDWAGON);
		}

		if (!u->IsArticulatedPart()) {
			/* Do not count powered wagons for the compatible railtypes, as wagons always
			   have railtype normal */
			if (rvi_u->power > 0) {
				this->compatible_railtypes |= GetRailTypeInfo(u->railtype)->powered_railtypes;
			}

			/* Some electric engines can be allowed to run on normal rail. It happens to all
			 * existing electric engines when elrails are disabled and then re-enabled */
			if (HasBit(u->flags, VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL)) {
				u->railtype = RAILTYPE_RAIL;
				u->compatible_railtypes |= RAILTYPES_RAIL;
			}

			/* max speed is the minimum of the speed limits of all vehicles in the consist */
			if ((rvi_u->railveh_type != RAILVEH_WAGON || _settings_game.vehicle.wagon_speed_limits) && !UsesWagonOverride(u)) {
				uint16_t speed = GetTrainVehicleMaxSpeed(u, rvi_u, this);
				if (speed != 0) max_speed = std::min(speed, max_speed);
			}
		}

		uint16_t new_cap = e_u->DetermineCapacity(u);
		if (allowed_changes & CCF_CAPACITY) {
			/* Update vehicle capacity. */
			if (u->cargo_cap > new_cap) u->cargo.Truncate(new_cap);
			u->refit_cap = std::min(new_cap, u->refit_cap);
			u->cargo_cap = new_cap;
		} else {
			/* Verify capacity hasn't changed. */
			if (new_cap != u->cargo_cap) ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_CAPACITY, GBUG_VEH_CAPACITY, true);
		}
		u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_TRAIN_CARGO_AGE_PERIOD, e_u->info.cargo_age_period);

		/* check the vehicle length (callback) */
		uint16_t veh_len = CALLBACK_FAILED;
		if (e_u->GetGRF() != nullptr && e_u->GetGRF()->grf_version >= 8) {
			/* Use callback 36 */
			veh_len = GetVehicleProperty(u, PROP_TRAIN_SHORTEN_FACTOR, CALLBACK_FAILED);

			if (veh_len != CALLBACK_FAILED && veh_len >= VEHICLE_LENGTH) {
				ErrorUnknownCallbackResult(e_u->GetGRFID(), CBID_VEHICLE_LENGTH, veh_len);
			}
		} else if (HasBit(e_u->info.callback_mask, CBM_VEHICLE_LENGTH)) {
			/* Use callback 11 */
			veh_len = GetVehicleCallback(CBID_VEHICLE_LENGTH, 0, 0, u->engine_type, u);
		}
		if (veh_len == CALLBACK_FAILED) veh_len = rvi_u->shorten_factor;
		veh_len = VEHICLE_LENGTH - Clamp(veh_len, 0, VEHICLE_LENGTH - 1);

		if (allowed_changes & CCF_LENGTH) {
			/* Update vehicle length. */
			u->gcache.cached_veh_length = veh_len;
		} else {
			/* Verify length hasn't changed. */
			if (veh_len != u->gcache.cached_veh_length) VehicleLengthChanged(u);
		}

		this->gcache.cached_total_length += u->gcache.cached_veh_length;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}
	SetBit(last_vis_effect->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);

	/* store consist weight/max speed in cache */
	this->vcache.cached_max_speed = max_speed;
	this->tcache.cached_tflags = (train_can_tilt ? TCF_TILT : TCF_NONE) | (speed_varies_by_railtype ? TCF_SPD_RAILTYPE : TCF_NONE);
	this->tcache.cached_curve_speed_mod = min_curve_speed_mod;
	this->tcache.cached_max_curve_speed = this->GetCurveSpeedLimit();

	/* recalculate cached weights and power too (we do this *after* the rest, so it is known which wagons are powered and need extra weight added) */
	this->CargoChanged();

	this->UpdateAcceleration();
	if (this->IsFrontEngine()) {
		if (!HasBit(this->subtype, GVSF_VIRTUAL)) SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
		InvalidateWindowData(WC_VEHICLE_REFIT, this->index, VIWD_CONSIST_CHANGED);
		InvalidateWindowData(WC_VEHICLE_ORDERS, this->index, VIWD_CONSIST_CHANGED);
		InvalidateNewGRFInspectWindow(GSF_TRAINS, this->index);
	}
	if (allowed_changes & CCF_LENGTH) {
		for (Train *u = this->Next(); u != nullptr; u = u->Next()) {
			u->vcache.cached_max_speed = 0;
			u->gcache.cached_weight = 0;
			u->gcache.cached_max_te = 0;
			u->gcache.cached_axle_resistance = 0;
			u->gcache.cached_max_track_speed = 0;
			u->gcache.cached_power = 0;
			u->gcache.cached_air_drag = 0;
			u->gcache.cached_total_length = 0;
			u->tcache.cached_num_engines = 0;
			u->tcache.cached_centre_mass = 0;
			u->tcache.cached_braking_length = 0;
			u->tcache.cached_deceleration = 0;
			u->tcache.cached_uncapped_decel = 0;
			u->tcache.cached_tflags = TCF_NONE;
			u->tcache.cached_curve_speed_mod = 0;
			u->tcache.cached_max_curve_speed = 0;
		}
	}
}

/**
 * Get the fraction of the vehicle's current tile which is in front of it.
 * This is equal to how many more steps it could travel without having to stop/reverse if it was an end of line.
 *
 * See also wrapper without x_pos, y_pos in train.h
 *
 * @param v              the vehicle to use (not required to be the front)
 * @param x_pos          vehicle x position
 * @param y_pos          vehicle y position
 * @return the fraction of the current tile in front of the vehicle
 */
int GetTileMarginInFrontOfTrain(const Train *v, int x_pos, int y_pos)
{
	if (IsDiagonalDirection(v->direction)) {
		DiagDirection dir = DirToDiagDir(v->direction);
		int offset = ((DiagDirToAxis(dir) == AXIS_X) ? x_pos : y_pos) & 0xF;
		return ((dir == DIAGDIR_SE || dir == DIAGDIR_SW) ? TILE_SIZE - 1 - offset : offset) - ((v->gcache.cached_veh_length + 1) / 2);
	} else {
		/* Calc position within the current tile */
		uint x = x_pos & 0xF;
		uint y = y_pos & 0xF;

		/* for non-diagonal directions, x will be 1, 3, 5, ..., 15 */
		switch (v->direction) {
			case DIR_N : x = ~x + ~y + 25; break;
			case DIR_E : x = ~x + y + 9;   break;
			case DIR_S : x = x + y - 7;    break;
			case DIR_W : x = ~y + x + 9;   break;
			default: break;
		}
		x >>= 1; // x is now in range 0 ... 7
		return (TILE_SIZE / 2) - 1 - x - (v->gcache.cached_veh_length + 1) / 2;
	}
}

/**
 * Get the stop location of (the center) of the front vehicle of a train at
 * a platform of a station.
 *
 * See also wrapper without x_pos, y_pos in train.h
 *
 * @param station_id     the ID of the station where we're stopping
 * @param tile           the tile where the vehicle currently is
 * @param v              the vehicle to get the stop location of
 * @param update_train_state whether the state of the train v may be changed
 * @param station_ahead  'return' the amount of 1/16th tiles in front of the train
 * @param station_length 'return' the station length in 1/16th tiles
 * @return the location, calculated from the begin of the station to stop at.
 */
int GetTrainStopLocation(StationID station_id, TileIndex tile, Train *v, bool update_train_state, int *station_ahead, int *station_length)
{
	Train *front = v->First();
	if (IsRailWaypoint(tile)) {
		*station_ahead = *station_length = TILE_SIZE;
	} else {
		const Station *st = Station::Get(station_id);
		*station_ahead  = st->GetPlatformLength(tile, DirToDiagDir(v->direction)) * TILE_SIZE;
		*station_length = st->GetPlatformLength(tile) * TILE_SIZE;
	}

	/* Default to the middle of the station for stations stops that are not in
	 * the order list like intermediate stations when non-stop is disabled */
	OrderStopLocation osl = OSL_PLATFORM_MIDDLE;
	if (front->current_order.IsType(OT_GOTO_STATION) && front->current_order.GetDestination() == station_id) {
		osl = front->current_order.GetStopLocation();
	} else if (front->current_order.IsType(OT_LOADING_ADVANCE) && front->current_order.GetDestination() == station_id) {
		osl = OSL_PLATFORM_THROUGH;
	} else if (front->current_order.IsType(OT_GOTO_WAYPOINT) && front->current_order.GetDestination() == station_id) {
		osl = OSL_PLATFORM_FAR_END;
	}
	int overhang = front->gcache.cached_total_length - *station_length;
	int adjust = 0;
	if (osl == OSL_PLATFORM_THROUGH && overhang > 0) {
		for (Train *u = front; u != nullptr; u = u->Next()) {
			/* Passengers may not be through-loaded */
			if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CC_PASSENGERS)) {
				osl = OSL_PLATFORM_FAR_END;
				break;
			}
		}
	}
	if (osl == OSL_PLATFORM_THROUGH && overhang > 0) {
		/* The train is longer than the station, and we can run through the station to load/unload */
		bool advance_beyond_platform_end = false;
		if (update_train_state) {
			/* Only advance beyond platform end if there is at least one vehicle with capacity in the active part of the train.
			 * This avoids the entire train being beyond the platform end. */
			for (Train *u = v; u != nullptr; u = u->Next()) {
				if (u->cargo_cap != 0) {
					advance_beyond_platform_end = true;
					break;
				}
			}
		}
		for (Train *u = v; u != nullptr; u = u->Next()) {
			if (advance_beyond_platform_end && overhang > 0 && !HasBit(u->flags, VRF_BEYOND_PLATFORM_END) && !u->IsArticulatedPart()) {
				bool skip = true;
				for (const Train *part = u; part != nullptr; part = part->HasArticulatedPart() ? part->GetNextArticulatedPart() : nullptr) {
					if (part->cargo_cap != 0) {
						skip = false;
						break;
					}
				}
				if (skip) {
					for (Train *part = u; part != nullptr; part = part->HasArticulatedPart() ? part->GetNextArticulatedPart() : nullptr) {
						SetBit(part->flags, VRF_BEYOND_PLATFORM_END);
					}
				}
			}
			if (HasBit(u->flags, VRF_BEYOND_PLATFORM_END)) {
				overhang -= u->gcache.cached_veh_length;
				adjust += u->gcache.cached_veh_length;
			} else {
				break;
			}
		}
		for (Train *u = front; u != v; u = u->Next()) overhang -= u->gcache.cached_veh_length; // only advance until rear of train is in platform
		if (overhang < 0) adjust += overhang;
	} else if (overhang >= 0) {
		/* The train is longer than the station, make it stop at the far end of the platform */
		osl = OSL_PLATFORM_FAR_END;
	}

	/* The stop location of the FRONT! of the train */
	int stop;
	switch (osl) {
		default: NOT_REACHED();

		case OSL_PLATFORM_NEAR_END:
			stop = front->gcache.cached_total_length;
			break;

		case OSL_PLATFORM_MIDDLE:
			stop = *station_length - (*station_length -front->gcache.cached_total_length) / 2;
			break;

		case OSL_PLATFORM_FAR_END:
		case OSL_PLATFORM_THROUGH:
			stop = *station_length;
			break;
	}

	/* Subtract half the front vehicle length of the train so we get the real
	 * stop location of the train. */
	int result = stop - ((v->gcache.cached_veh_length + 1) / 2) + adjust;

	if (osl == OSL_PLATFORM_THROUGH && v != front) {
		/* Check front of train for obstructions */

		if (TrainCanLeaveTile(front)) {
			/* Determine the non-diagonal direction in which we will exit this tile */
			DiagDirection dir = VehicleExitDir(front->direction, front->track);
			/* Calculate next tile */
			TileIndex next_tile = front->tile + TileOffsByDiagDir(dir);

			/* Determine the track status on the next tile */
			TrackdirBits trackdirbits = GetTileTrackdirBits(next_tile, TRANSPORT_RAIL, 0, ReverseDiagDir(dir)) & DiagdirReachesTrackdirs(dir);

			/* mask unreachable track bits if we are forbidden to do 90deg turns */
			TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
			if (_settings_game.pf.forbid_90_deg) {
				bits &= ~TrackCrossesTracks(FindFirstTrack(front->track));
			}

			if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(front, next_tile, dir) || IsRailDepotTile(next_tile) ||
					(KillFirstBit(trackdirbits) == TRACKDIR_BIT_NONE && HasOnewaySignalBlockingTrackdir(next_tile, FindFirstTrackdir(trackdirbits)))) {
				/* next tile is an effective dead end */
				int current_platform_remaining = *station_ahead - TILE_SIZE + GetTileMarginInFrontOfTrain(v);
				int limit = GetTileMarginInFrontOfTrain(front) + (*station_length - current_platform_remaining) - ((v->gcache.cached_veh_length + 1) / 2);
				result = std::min(limit, result);
			}
		}
	}

	return result;
}


/**
 * Computes train speed limit caused by curves
 * @return imposed speed limit
 */
uint16_t Train::GetCurveSpeedLimit() const
{
	dbg_assert(this->First() == this);

	static const int absolute_max_speed = UINT16_MAX;
	int max_speed = absolute_max_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) return max_speed;

	int curvecount[2] = {0, 0};

	/* first find the curve speed limit */
	int numcurve = 0;
	int sum = 0;
	int pos = 0;
	int lastpos = -1;
	for (const Train *u = this; u->Next() != nullptr; u = u->Next(), pos += u->gcache.cached_veh_length) {
		Direction this_dir = u->direction;
		Direction next_dir = u->Next()->direction;

		DirDiff dirdiff = DirDifference(this_dir, next_dir);
		if (dirdiff == DIRDIFF_SAME) continue;

		if (dirdiff == DIRDIFF_45LEFT) curvecount[0]++;
		if (dirdiff == DIRDIFF_45RIGHT) curvecount[1]++;
		if (dirdiff == DIRDIFF_45LEFT || dirdiff == DIRDIFF_45RIGHT) {
			if (lastpos != -1) {
				numcurve++;
				sum += pos - lastpos;
				if (pos - lastpos <= static_cast<int>(VEHICLE_LENGTH) && max_speed > 88) {
					max_speed = 88;
				}
			}
			lastpos = pos;
		}

		/* if we have a 90 degree turn, fix the speed limit to 60 */
		if (dirdiff == DIRDIFF_90LEFT || dirdiff == DIRDIFF_90RIGHT) {
			max_speed = 61;
		}
	}

	if (numcurve > 0 && max_speed > 88) {
		if (curvecount[0] == 1 && curvecount[1] == 1) {
			max_speed = absolute_max_speed;
		} else {
			sum = CeilDiv(sum, VEHICLE_LENGTH);
			sum /= numcurve;
			max_speed = 232 - (13 - Clamp(sum, 1, 12)) * (13 - Clamp(sum, 1, 12));
		}
	}

	if (max_speed != absolute_max_speed) {
		/* Apply the current railtype's curve speed advantage */
		const RailTypeInfo *rti = GetRailTypeInfo(GetRailTypeByTrackBit(this->tile, this->track));
		max_speed += (max_speed / 2) * rti->curve_speed;

		if (this->tcache.cached_tflags & TCF_TILT) {
			/* Apply max_speed bonus of 20% for a tilting train */
			max_speed += max_speed / 5;
		}

		/* Apply max_speed modifier (cached value is fixed-point binary with 8 fractional bits)
		 * and clamp the result to an acceptable range. */
		max_speed += (max_speed * this->tcache.cached_curve_speed_mod) / 256;
		max_speed = Clamp(max_speed, 2, absolute_max_speed);
	}

	return static_cast<uint16_t>(max_speed);
}

void AdvanceOrderIndex(const Vehicle *v, VehicleOrderID &index)
{
	int depth = 0;

	do {
		/* Wrap around. */
		if (index >= v->GetNumOrders()) index = 0;

		const Order *order = v->GetOrder(index);
		dbg_assert(order != nullptr);

		switch (order->GetType()) {
			case OT_GOTO_DEPOT:
				/* Skip service in depot orders when the train doesn't need service. */
				if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !v->NeedsServicing()) break;
				[[fallthrough]];
			case OT_GOTO_STATION:
			case OT_GOTO_WAYPOINT:
				return;
			case OT_CONDITIONAL: {
				VehicleOrderID next = ProcessConditionalOrder(order, v, PCO_DRY_RUN);
				if (next != INVALID_VEH_ORDER_ID) {
					depth++;
					index = next;
					/* Don't increment next, so no break here. */
					continue;
				}
				break;
			}
			default:
				break;
		}
		/* Don't increment inside the while because otherwise conditional
		 * orders can lead to an infinite loop. */
		++index;
		depth++;
	} while (depth < v->GetNumOrders());

	/* Wrap around. */
	if (index >= v->GetNumOrders()) index = 0;
}

int PredictStationStoppingLocation(const Train *v, const Order *order, int station_length, DestinationID dest)
{
	/* Default to the middle of the station for stations stops that are not in
	 * the order list like intermediate stations when non-stop is disabled */
	OrderStopLocation osl = OSL_PLATFORM_MIDDLE;
	if (order->IsType(OT_GOTO_STATION) && order->GetDestination() == dest) {
		osl = order->GetStopLocation();
	} else if (order->IsType(OT_LOADING_ADVANCE) && order->GetDestination() == dest) {
		osl = OSL_PLATFORM_THROUGH;
	} else if (order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == dest) {
		osl = OSL_PLATFORM_FAR_END;
	}

	int overhang = v->gcache.cached_total_length - station_length;
	int adjust = 0;
	if (osl == OSL_PLATFORM_THROUGH && overhang > 0) {
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			/* Passengers may not be through-loaded */
			if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CC_PASSENGERS)) {
				osl = OSL_PLATFORM_FAR_END;
				break;
			}
		}
	}
	if (osl == OSL_PLATFORM_THROUGH && overhang > 0) {
		/* The train is longer than the station, and we can run through the station to load/unload */

		/* Check whether the train has already reached the platform and set VRF_BEYOND_PLATFORM_END on the front part */
		if (HasBit(v->flags, VRF_BEYOND_PLATFORM_END)) {
			/* Compute how much of the train should stop beyond the station, using already set flags */
			int beyond = 0;
			for (const Train *u = v; u != nullptr && HasBit(u->flags, VRF_BEYOND_PLATFORM_END); u = u->Next()) {
				beyond += u->gcache.cached_veh_length;
			}
			/* Adjust for the remaining amount of train being less than the station length */
			int overshoot = station_length - std::min(v->gcache.cached_total_length - beyond, station_length);
			adjust = beyond - overshoot;
		} else {
			/* Train hasn't reached the platform yet, or no advancing has occured, use predictive mode */
			for (const Train *u = v; u != nullptr; u = u->Next()) {
				if (overhang > 0 && !u->IsArticulatedPart()) {
					bool skip = true;
					for (const Train *part = u; part != nullptr; part = part->HasArticulatedPart() ? part->GetNextArticulatedPart() : nullptr) {
						if (part->cargo_cap != 0) {
							skip = false;
							break;
						}
					}
					if (skip) {
						for (const Train *part = u; part != nullptr; part = part->HasArticulatedPart() ? part->GetNextArticulatedPart() : nullptr) {
							overhang -= part->gcache.cached_veh_length;
							adjust += part->gcache.cached_veh_length;
						}
						continue;
					}
				}
				break;
			}
			if (overhang < 0) adjust += overhang;
		}
	} else if (overhang >= 0) {
		/* The train is longer than the station, make it stop at the far end of the platform */
		osl = OSL_PLATFORM_FAR_END;
	}

	int stop;
	switch (osl) {
		default: NOT_REACHED();

		case OSL_PLATFORM_NEAR_END:
			stop = v->gcache.cached_total_length;
			break;

		case OSL_PLATFORM_MIDDLE:
			stop = station_length - (station_length - v->gcache.cached_total_length) / 2;
			break;

		case OSL_PLATFORM_FAR_END:
		case OSL_PLATFORM_THROUGH:
			stop = station_length;
			break;
	}
	return stop + adjust;
}

TrainDecelerationStats::TrainDecelerationStats(const Train *t, int z_pos)
{
	this->deceleration_x2 = 2 * t->tcache.cached_deceleration;
	this->uncapped_deceleration_x2 = 2 * t->tcache.cached_uncapped_decel;
	this->z_pos = z_pos;
	this->t = t;
}

static int64_t GetRealisticBrakingDistanceForSpeed(const TrainDecelerationStats &stats, int start_speed, int end_speed, int z_delta)
{
	/* v^2 = u^2 + 2as */

	auto sqr = [](int64_t speed) -> int64_t { return speed * speed; };

	int64_t ke_delta = sqr(start_speed) - sqr(end_speed);

	int64_t dist = ke_delta / stats.deceleration_x2;

	if (z_delta < 0 && _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
		/* descending */
		/* (5/18) is due to KE being in km/h derived units instead of m/s */
		int64_t slope_dist = (ke_delta - (z_delta * ((400 * 5) / 18) * _settings_game.vehicle.train_slope_steepness)) / stats.uncapped_deceleration_x2;
		dist = std::max<int64_t>(dist, slope_dist);
	}
	return dist;
}

static int GetRealisticBrakingSpeedForDistance(const TrainDecelerationStats &stats, int distance, int end_speed, int z_delta)
{
	/* v^2 = u^2 + 2as */

	auto sqr = [](int64_t speed) -> int64_t { return speed * speed; };

	int64_t target_ke = sqr(end_speed);
	int64_t speed_sqr = target_ke + ((int64_t)stats.deceleration_x2 * (int64_t)distance);

	if (speed_sqr <= REALISTIC_BRAKING_MIN_SPEED * REALISTIC_BRAKING_MIN_SPEED) return REALISTIC_BRAKING_MIN_SPEED;

	if (z_delta < 0 && _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
		/* descending */
		/* (5/18) is due to KE being in km/h derived units instead of m/s */
		int64_t sloped_ke = target_ke + (z_delta * ((400 * 5) / 18) * _settings_game.vehicle.train_slope_steepness);
		int64_t slope_speed_sqr = sloped_ke + ((int64_t)stats.uncapped_deceleration_x2 * (int64_t)distance);
		if (slope_speed_sqr < speed_sqr &&
				_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && GetRailTypeInfo(stats.t->railtype)->acceleration_type != 2) {
			/* calculate speed at which braking would be sufficient */

			uint weight = stats.t->gcache.cached_weight;
			int64_t power_w = (stats.t->gcache.cached_power * 746ll) + (stats.t->tcache.cached_braking_length * (int64_t)RBC_BRAKE_POWER_PER_LENGTH);
			int64_t min_braking_force = (stats.t->tcache.cached_braking_length * (int64_t)RBC_BRAKE_FORCE_PER_LENGTH) + stats.t->gcache.cached_axle_resistance + (weight * 16);

			/* F = (7/8) * (F_min + ((power_w * 18) / (5 * v)))
			 * v^2 = sloped_ke + F * s / (4 * m)
			 * let k = sloped_ke + ((7 * F_min * s) / (8 * 4 * m))
			 * v^3 - k * v - (7 * 18 * power_w * s) / (5 * 8 * 4 * m) = 0
			 * v^3 + p * v + q = 0
			 *   where: p = -k
			 *          q = -(7 * 18 * power_w * s) / (5 * 8 * 4 * m)
			 *
			 * v = cbrt(-q / 2 + sqrt((q^2 / 4) - (k^3 / 27))) + cbrt(-q / 2 - sqrt((q^2 / 4) - (k^3 / 27)))
			 * let r = - q / 2 = (7 * 9 * power_w * s) / (5 * 8 * 4 * m)
			 * let l = k / 3
			 * v = cbrt(r + sqrt(r^2 - l^3)) + cbrt(r - sqrt(r^2 - l^3))
			 */
			int64_t l = (sloped_ke + ((7 * min_braking_force * (int64_t)distance) / (8 * weight))) / 3;
			int64_t r = (7 * 9 * power_w * (int64_t)distance) / (160 * weight);
			int64_t sqrt_factor = (r * r) - (l * l * l);
			if (sqrt_factor >= 0) {
				int64_t part = IntSqrt64(sqrt_factor);
				int32_t v_calc = IntCbrt(r + part);
				int cb2 = r - part;
				if (cb2 > 0) {
					v_calc += IntCbrt(cb2);
				} else if (cb2 < 0) {
					v_calc -= IntCbrt(-cb2);
				}
				int64_t v_calc_sq = sqr(v_calc);
				if (v_calc_sq < speed_sqr && v_calc_sq > slope_speed_sqr) {
					return std::max((int)REALISTIC_BRAKING_MIN_SPEED, v_calc);
				}
			}
		}
		speed_sqr = std::min<int64_t>(speed_sqr, slope_speed_sqr);
	}
	if (speed_sqr <= REALISTIC_BRAKING_MIN_SPEED * REALISTIC_BRAKING_MIN_SPEED) return REALISTIC_BRAKING_MIN_SPEED;
	if (speed_sqr > UINT_MAX) speed_sqr = UINT_MAX;

	return IntSqrt((uint) speed_sqr);
}

void LimitSpeedFromLookAhead(int &max_speed, const TrainDecelerationStats &stats, int current_position, int position, int end_speed, int z_delta)
{
	if (position <= current_position) {
		max_speed = std::min(max_speed, std::max(15, end_speed));
	} else if (end_speed < max_speed) {
		int64_t distance = GetRealisticBrakingDistanceForSpeed(stats, max_speed, end_speed, z_delta);
		if (distance + current_position > position) {
			/* Speed is too fast, we would overshoot */
			if (z_delta < 0 && (position - current_position) < stats.t->gcache.cached_total_length) {
				int effective_length = std::min<int>(stats.t->gcache.cached_total_length, stats.t->tcache.cached_centre_mass * 2);
				if ((position - current_position) < effective_length) {
					/* Reduce z delta near target to compensate for target z not taking into account that z varies across the whole train */
					z_delta = (z_delta * (position - current_position)) / effective_length;
				}
			}
			max_speed = std::min(max_speed, GetRealisticBrakingSpeedForDistance(stats, position - current_position, end_speed, z_delta));
		}
	}
}

static void ApplyLookAheadItem(const Train *v, const TrainReservationLookAheadItem &item, int &max_speed, int &advisory_max_speed,
		VehicleOrderID &current_order_index, const Order *&order, StationID &last_station_visited, const TrainDecelerationStats &stats, int current_position)
{
	auto limit_speed = [&](int position, int end_speed, int z) {
		LimitSpeedFromLookAhead(max_speed, stats, current_position, position, end_speed, z - stats.z_pos);
		advisory_max_speed = std::min(advisory_max_speed, max_speed);
	};
	auto limit_advisory_speed = [&](int position, int end_speed, int z) {
		LimitSpeedFromLookAhead(advisory_max_speed, stats, current_position, position, end_speed, z - stats.z_pos);
	};

	switch (item.type) {
		case TRLIT_STATION: {
			if (order->ShouldStopAtStation(last_station_visited, item.data_id, Waypoint::GetIfValid(item.data_id) != nullptr)) {
				limit_advisory_speed(item.start + PredictStationStoppingLocation(v, order, item.end - item.start, item.data_id), 0, item.z_pos);
				last_station_visited = item.data_id;
			} else if (order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == item.data_id && (order->GetWaypointFlags() & OWF_REVERSE)) {
				limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos);
				if (order->IsWaitTimetabled()) last_station_visited = item.data_id;
			}
			if (order->IsBaseStationOrder() && order->GetDestination() == item.data_id && v->GetNumOrders() > 0) {
				current_order_index++;
				AdvanceOrderIndex(v, current_order_index);
				order = v->GetOrder(current_order_index);
				uint16_t order_max_speed = order->GetMaxSpeed();
				if (order_max_speed < UINT16_MAX) limit_advisory_speed(item.start, order_max_speed, item.z_pos);
			}
			break;
		}

		case TRLIT_REVERSE:
			limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos);
			break;

		case TRLIT_TRACK_SPEED:
			limit_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SPEED_RESTRICTION:
			if (item.data_id > 0) limit_advisory_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SIGNAL:
			if (_settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
					(v->lookahead->lookahead_end_position == item.start || v->lookahead->lookahead_end_position == item.start + 1)) {
				limit_advisory_speed(item.start, 0, item.z_pos);
			}
			break;

		case TRLIT_CURVE_SPEED:
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) limit_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SPEED_ADAPTATION:
			break;
	}
}

static void AdvanceLookAheadPosition(Train *v)
{
	v->lookahead->current_position++;
	if (v->lookahead->zpos_refresh_remaining > 0) v->lookahead->zpos_refresh_remaining--;

	if (v->lookahead->current_position > v->lookahead->reservation_end_position + 8 && v->track != TRACK_BIT_DEPOT) {
		/* Beyond end of lookahead, delete it, it will be recreated later with a new reservation */
		v->lookahead.reset();
		return;
	}

	if (unlikely(v->lookahead->current_position >= (1 << 30))) {
		/* Prevent signed overflow by rebasing all position values */
		const int32_t old_position = v->lookahead->current_position;
		v->lookahead->current_position = 0;
		v->lookahead->reservation_end_position -= old_position;
		v->lookahead->lookahead_end_position -= old_position;
		v->lookahead->next_extend_position -= old_position;
		for (TrainReservationLookAheadItem &item : v->lookahead->items) {
			item.start -= old_position;
			item.end -= old_position;
		}
		for (TrainReservationLookAheadCurve &curve : v->lookahead->curves) {
			curve.position -= old_position;
		}
	}

	while (!v->lookahead->items.empty() && v->lookahead->items.front().end < v->lookahead->current_position) {
		if (v->lookahead->items.front().type == TRLIT_STATION) {
			int trim_position = v->lookahead->current_position - 4;
			for (const Train *u = v; u != nullptr; u = u->Next()) {
				if (HasBit(u->flags, VRF_BEYOND_PLATFORM_END)) {
					trim_position -= u->gcache.cached_veh_length;
				} else {
					break;
				}
			}
			if (v->lookahead->items.front().end >= trim_position) break;
		}
		v->lookahead->items.pop_front();
	}

	if (v->lookahead->current_position == v->lookahead->next_extend_position) {
		SetTrainReservationLookaheadEnd(v);

		/* This may clear the lookahead if it has become invalid */
		TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
		if (v->lookahead == nullptr) return;

		v->lookahead->SetNextExtendPositionIfUnset();
	}
}

/**
 * Calculates the maximum speed information of the vehicle under its current conditions.
 * @return Maximum speed information of the vehicle.
 */
Train::MaxSpeedInfo Train::GetCurrentMaxSpeedInfoInternal(bool update_state) const
{
	int max_speed = _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ?
			this->gcache.cached_max_track_speed :
			std::min<int>(this->tcache.cached_max_curve_speed, this->gcache.cached_max_track_speed);

	if (this->current_order.IsType(OT_LOADING_ADVANCE)) max_speed = std::min<int>(max_speed, _settings_game.vehicle.through_load_speed_limit);

	int advisory_max_speed = max_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && this->lookahead == nullptr) {
		Train *v_platform = const_cast<Train *>(this->GetStationLoadingVehicle());
		TileIndex platform_tile = v_platform->tile;
		if (HasStationTileRail(platform_tile)) {
			StationID sid = GetStationIndex(platform_tile);
			if (this->current_order.ShouldStopAtStation(this, sid, IsRailWaypoint(platform_tile))) {
				int station_ahead;
				int station_length;
				int stop_at = GetTrainStopLocation(sid, platform_tile, v_platform, update_state, &station_ahead, &station_length);

				/* The distance to go is whatever is still ahead of the train minus the
				 * distance from the train's stop location to the end of the platform */
				int distance_to_go = station_ahead / TILE_SIZE - (station_length - stop_at) / TILE_SIZE;

				if (distance_to_go > 0) {
					if (this->UsingRealisticBraking()) {
						advisory_max_speed = std::min(advisory_max_speed, 15 * distance_to_go);
					} else {
						int st_max_speed = 120;

						int delta_v = this->cur_speed / (distance_to_go + 1);
						if (max_speed > (this->cur_speed - delta_v)) {
							st_max_speed = this->cur_speed - (delta_v / 10);
						}

						st_max_speed = std::max(st_max_speed, 25 * distance_to_go);
						max_speed = std::min(max_speed, st_max_speed);
					}
				}
			}
		}
	}

	if (HasBit(this->flags, VRF_CONSIST_SPEED_REDUCTION)) {
		ClrBit(const_cast<Train *>(this)->flags, VRF_CONSIST_SPEED_REDUCTION);
		for (const Train *u = this; u != nullptr; u = u->Next()) {
			if (u->track == TRACK_BIT_DEPOT) {
				SetBit(const_cast<Train *>(this)->flags, VRF_CONSIST_SPEED_REDUCTION);
				if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC) {
					max_speed = std::min<int>(max_speed, _settings_game.vehicle.rail_depot_speed_limit);
				}
				continue;
			}

			/* Vehicle is on the middle part of a bridge. */
			if (u->track & TRACK_BIT_WORMHOLE && !(u->vehstatus & VS_HIDDEN)) {
				SetBit(const_cast<Train *>(this)->flags, VRF_CONSIST_SPEED_REDUCTION);
				max_speed = std::min<int>(max_speed, GetBridgeSpec(GetBridgeType(u->tile))->speed);
			}
		}
	}

	advisory_max_speed = std::min<int>(advisory_max_speed, this->current_order.GetMaxSpeed());
	if (HasBit(this->flags, VRF_BREAKDOWN_SPEED)) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->GetBreakdownSpeed());
	}
	if (this->speed_restriction != 0) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->speed_restriction);
	}
	if (this->signal_speed_restriction != 0 && _settings_game.vehicle.train_speed_adaptation && !HasBit(this->flags, VRF_SPEED_ADAPTATION_EXEMPT)) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->signal_speed_restriction);
	}
	if (this->reverse_distance >= 1) {
		advisory_max_speed = std::min<int>(advisory_max_speed, ReversingDistanceTargetSpeed(this));
	}

	if (this->UsingRealisticBraking()) {
		if (this->lookahead != nullptr) {
			if (update_state && this->lookahead->zpos_refresh_remaining == 0) {
				this->lookahead->cached_zpos = this->CalculateOverallZPos();
				this->lookahead->zpos_refresh_remaining = this->GetZPosCacheUpdateInterval();
			}
			TrainDecelerationStats stats(this, this->lookahead->cached_zpos);
			if (HasBit(this->lookahead->flags, TRLF_DEPOT_END)) {
				LimitSpeedFromLookAhead(max_speed, stats, this->lookahead->current_position, this->lookahead->reservation_end_position - TILE_SIZE,
						_settings_game.vehicle.rail_depot_speed_limit, this->lookahead->reservation_end_z - stats.z_pos);
			} else {
				LimitSpeedFromLookAhead(max_speed, stats, this->lookahead->current_position, this->lookahead->reservation_end_position,
						0, this->lookahead->reservation_end_z - stats.z_pos);
			}
			advisory_max_speed = std::min(advisory_max_speed, max_speed);
			VehicleOrderID current_order_index = this->cur_real_order_index;
			const Order *order = &(this->current_order);
			StationID last_station_visited = this->last_station_visited;
			for (const TrainReservationLookAheadItem &item : this->lookahead->items) {
				ApplyLookAheadItem(this, item, max_speed, advisory_max_speed, current_order_index, order, last_station_visited, stats, this->lookahead->current_position);
			}
			if (HasBit(this->lookahead->flags, TRLF_APPLY_ADVISORY)) {
				max_speed = std::min(max_speed, advisory_max_speed);
			}
		} else {
			advisory_max_speed = std::min(advisory_max_speed, 30);
		}
	}

	return { max_speed, advisory_max_speed };
}

/**
 * Calculates the maximum speed of the vehicle under its current conditions.
 * @return Maximum speed of the vehicle.
 */
int Train::GetCurrentMaxSpeed() const
{
	MaxSpeedInfo info = this->GetCurrentMaxSpeedInfo();
	return std::min(info.strict_max_speed, info.advisory_max_speed);
}

uint32_t Train::CalculateOverallZPos() const
{
	if (likely(HasBit(this->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST))) {
		return this->z_pos;
	} else {
		int64_t sum = 0;
		for (const Train *u = this; u != nullptr; u = u->Next()) {
			sum += ((int)u->z_pos * (int)u->tcache.cached_veh_weight);
		}
		return sum / this->gcache.cached_weight;
	}
}

/** Update acceleration of the train from the cached power and weight. */
void Train::UpdateAcceleration()
{
	dbg_assert(this->IsFrontEngine() || this->IsFreeWagon());

	uint power = this->gcache.cached_power;
	uint weight = this->gcache.cached_weight;
	assert(weight != 0);
	this->acceleration = Clamp(power / weight * 4, 1, 255);

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && !HasBit(GetRailTypeInfo(this->railtype)->ctrl_flags, RTCF_NOREALISTICBRAKING) && this->IsFrontEngine()) {
		this->tcache.cached_tflags |= TCF_RL_BRAKING;
		switch (_settings_game.vehicle.train_acceleration_model) {
			default: NOT_REACHED();
			case AM_ORIGINAL:
				this->tcache.cached_uncapped_decel = this->tcache.cached_deceleration = Clamp((this->acceleration * 7) / 2, 1, 200);
				this->tcache.cached_braking_length = this->gcache.cached_total_length;
				break;

			case AM_REALISTIC: {
				int acceleration_type = this->GetAccelerationType();
				bool maglev = (acceleration_type == 2);
				int64_t power_w = power * 746ll;

				/* Increase the effective length used for brake force/power value when using the freight weight multiplier */
				uint length = this->gcache.cached_total_length;
				if (_settings_game.vehicle.freight_trains > 1) {
					uint adjust = (_settings_game.vehicle.freight_trains - 1);
					for (const Train *u = this; u != nullptr; u = u->Next()) {
						if (u->cargo_cap > 0 && CargoSpec::Get(u->cargo_type)->is_freight) {
							length += ((u->gcache.cached_veh_length * adjust) + 1) / 2;
						}
					}
					length = Clamp<uint>(length, 0, UINT16_MAX);
				}
				this->tcache.cached_braking_length = length;

				int64_t min_braking_force = (int64_t)length * (int64_t)RBC_BRAKE_FORCE_PER_LENGTH;
				if (!maglev) {
					/* From GroundVehicle::GetAcceleration()
					 * force = power * 18 / (speed * 5);
					 * resistance += (area * this->gcache.cached_air_drag * speed * speed) / 1000;
					 *
					 * let:
					 * F = force + resistance
					 * P = power
					 * v = speed
					 * d = area * this->gcache.cached_air_drag / 1000
					 *
					 * F = (18 P / 5 v) + d v^2
					 * Minimum occurs at d F / d v = 0
					 * This is v^3 = 9 P / 5 d
					 * If d == 0 or v > max v, evaluate at max v
					 */
					int evaluation_speed = this->vcache.cached_max_speed;
					int area = 14;
					int64_t power_b = power_w + ((int64_t)length * RBC_BRAKE_POWER_PER_LENGTH);
					if (this->gcache.cached_air_drag > 0) {
						uint64_t v_3 = 1800 * (uint64_t)power_b / (area * this->gcache.cached_air_drag);
						evaluation_speed = std::min<int>(evaluation_speed, IntCbrt(v_3));
					}
					if (evaluation_speed > 0) {
						min_braking_force += power_b * 18 / (evaluation_speed * 5);
						min_braking_force += (area * this->gcache.cached_air_drag * evaluation_speed * evaluation_speed) / 1000;
					}

					min_braking_force += this->gcache.cached_axle_resistance;
					int rolling_friction = 16; // 16 is the minimum value of v->GetRollingFriction() for a moving vehicle
					min_braking_force += weight * rolling_friction;
				} else {
					/* From GroundVehicle::GetAcceleration()
					 * Braking force does not decrease with speed,
					 * therefore air drag can be omitted.
					 * There is no rolling/axle drag. */
					min_braking_force += power_w / 25;
				}
				min_braking_force -= (min_braking_force >> 3); // Slightly underestimate braking for defensive driving purposes
				this->tcache.cached_uncapped_decel = Clamp(min_braking_force / (weight * 4), 1, UINT16_MAX);
				this->tcache.cached_deceleration = Clamp(this->tcache.cached_uncapped_decel, 1, GetTrainRealisticBrakingTargetDecelerationLimit(acceleration_type));
				break;
			}
		}
	} else {
		this->tcache.cached_tflags &= ~TCF_RL_BRAKING;
		this->tcache.cached_deceleration = 0;
		this->tcache.cached_uncapped_decel = 0;
		this->tcache.cached_braking_length = this->gcache.cached_total_length;
	}

	if (_settings_game.vehicle.improved_breakdowns) {
		if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
			this->breakdown_chance_factor = std::max(128 * 3 / (this->tcache.cached_num_engines + 2), 5);
		}
	}
}

bool Train::ConsistNeedsRepair() const
{
	if (!HasBit(this->flags, VRF_CONSIST_BREAKDOWN)) return false;

	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (HasBit(u->flags, VRF_NEED_REPAIR)) return true;
	}
	return false;
}

int Train::GetCursorImageOffset() const
{
	if (this->gcache.cached_veh_length != 8 && HasBit(this->flags, VRF_REVERSE_DIRECTION) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_RAIL_FLIPS)) {
		int reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;

		const Engine *e = this->GetEngine();
		if (e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
			reference_width = e->GetGRF()->traininfo_vehicle_width;
		}

		return ScaleSpriteTrad((this->gcache.cached_veh_length - (int)VEHICLE_LENGTH) * reference_width / (int)VEHICLE_LENGTH);
	}
	return 0;
}

/**
 * Get the width of a train vehicle image in the GUI.
 * @param offset Additional offset for positioning the sprite; set to nullptr if not needed
 * @return Width in pixels
 */
int Train::GetDisplayImageWidth(Point *offset) const
{
	int reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;
	int vehicle_pitch = 0;

	const Engine *e = this->GetEngine();
	if (e->GetGRF() != nullptr && is_custom_sprite(e->u.rail.image_index)) {
		reference_width = e->GetGRF()->traininfo_vehicle_width;
		vehicle_pitch = e->GetGRF()->traininfo_vehicle_pitch;
	}

	if (offset != nullptr) {
		if (HasBit(this->flags, VRF_REVERSE_DIRECTION) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_RAIL_FLIPS)) {
			offset->x = ScaleSpriteTrad(((int)this->gcache.cached_veh_length - (int)VEHICLE_LENGTH / 2) * reference_width / (int)VEHICLE_LENGTH);
		} else {
			offset->x = ScaleSpriteTrad(reference_width) / 2;
		}
		offset->y = ScaleSpriteTrad(vehicle_pitch);
	}
	return ScaleSpriteTrad(this->gcache.cached_veh_length * reference_width / VEHICLE_LENGTH);
}

static SpriteID GetDefaultTrainSprite(uint8_t spritenum, Direction direction)
{
	dbg_assert(IsValidImageIndex<VEH_TRAIN>(spritenum));
	return ((direction + _engine_sprite_add[spritenum]) & _engine_sprite_and[spritenum]) + _engine_sprite_base[spritenum];
}

/**
 * Get the sprite to display the train.
 * @param direction Direction of view/travel.
 * @param image_type Visualisation context.
 * @return Sprite to display.
 */
void Train::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8_t spritenum = this->spritenum;

	if (HasBit(this->flags, VRF_REVERSE_DIRECTION)) direction = ReverseDir(direction);

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleSprite(this, (Direction)(direction + 4 * IS_CUSTOM_SECONDHEAD_SPRITE(spritenum)), image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	dbg_assert(IsValidImageIndex<VEH_TRAIN>(spritenum));
	SpriteID sprite = GetDefaultTrainSprite(spritenum, direction);

	if (this->cargo.StoredCount() >= this->cargo_cap / 2U) sprite += _wagon_full_adder[spritenum];

	result->Set(sprite);
}

static void GetRailIcon(EngineID engine, bool rear_head, int &y, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	Direction dir = rear_head ? DIR_E : DIR_W;
	uint8_t spritenum = e->u.rail.image_index;

	if (is_custom_sprite(spritenum)) {
		GetCustomVehicleIcon(engine, dir, image_type, result);
		if (result->IsValid()) {
			if (e->GetGRF() != nullptr) {
				y += ScaleSpriteTrad(e->GetGRF()->traininfo_vehicle_pitch);
			}
			return;
		}

		spritenum = Engine::Get(engine)->original_image_index;
	}

	if (rear_head) spritenum++;

	result->Set(GetDefaultTrainSprite(spritenum, DIR_W));
}

void DrawTrainEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	if (RailVehInfo(engine)->railveh_type == RAILVEH_MULTIHEAD) {
		int yf = y;
		int yr = y;

		VehicleSpriteSeq seqf, seqr;
		GetRailIcon(engine, false, yf, image_type, &seqf);
		GetRailIcon(engine, true, yr, image_type, &seqr);

		Rect16 rectf = seqf.GetBounds();
		Rect16 rectr = seqr.GetBounds();

		preferred_x = SoftClamp(preferred_x,
				left - UnScaleGUI(rectf.left) + ScaleSpriteTrad(14),
				right - UnScaleGUI(rectr.right) - ScaleSpriteTrad(15));

		seqf.Draw(preferred_x - ScaleSpriteTrad(14), yf, pal, pal == PALETTE_CRASH);
		seqr.Draw(preferred_x + ScaleSpriteTrad(15), yr, pal, pal == PALETTE_CRASH);
	} else {
		VehicleSpriteSeq seq;
		GetRailIcon(engine, false, y, image_type, &seq);

		Rect16 rect = seq.GetBounds();
		preferred_x = SoftClamp(preferred_x,
				left - UnScaleGUI(rect.left),
				right - UnScaleGUI(rect.right));

		seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
	}
}

/**
 * Get the size of the sprite of a train sprite heading west, or both heads (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetTrainSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	int y = 0;

	VehicleSpriteSeq seq;
	GetRailIcon(engine, false, y, image_type, &seq);

	Rect rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

	width  = UnScaleGUI(rect.Width());
	height = UnScaleGUI(rect.Height());
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);

	if (RailVehInfo(engine)->railveh_type == RAILVEH_MULTIHEAD) {
		GetRailIcon(engine, true, y, image_type, &seq);
		rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

		/* Calculate values relative to an imaginary center between the two sprites. */
		width = ScaleSpriteTrad(TRAININFO_DEFAULT_VEHICLE_WIDTH) + UnScaleGUI(rect.right) - xoffs;
		height = std::max<uint>(height, UnScaleGUI(rect.Height()));
		xoffs  = xoffs - ScaleSpriteTrad(TRAININFO_DEFAULT_VEHICLE_WIDTH) / 2;
		yoffs  = std::min(yoffs, UnScaleGUI(rect.top));
	}
}

/**
 * Build a railroad wagon.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
static CommandCost CmdBuildRailWagon(TileIndex tile, DoCommandFlag flags, const Engine *e, Vehicle **ret)
{
	const RailVehicleInfo *rvi = &e->u.rail;

	/* Check that the wagon can drive on the track in question */
	if (!IsCompatibleRail(rvi->railtype, GetRailType(tile))) return_cmd_error(STR_ERROR_DEPOT_HAS_WRONG_RAIL_TYPE);

	if (flags & DC_EXEC) {
		Train *v = new Train();
		*ret = v;
		v->spritenum = rvi->image_index;

		v->engine_type = e->index;
		v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

		DiagDirection dir = GetRailDepotDirection(tile);

		v->direction = DiagDirToDir(dir);
		v->tile = tile;

		int x = TileX(tile) * TILE_SIZE | _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE | _vehicle_initial_y_fract[dir];

		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y, true);
		v->owner = _current_company;
		v->track = TRACK_BIT_DEPOT;
		v->vehstatus = VS_HIDDEN | VS_DEFPAL;
		v->reverse_distance = 0;
		v->speed_restriction = 0;
		v->signal_speed_restriction = 0;

		v->SetWagon();

		v->SetFreeWagon();
		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);

		v->cargo_type = e->GetDefaultCargoType();
		assert(IsValidCargoID(v->cargo_type));
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;

		v->railtype = rvi->railtype;

		v->date_of_last_service = EconTime::CurDate();
		v->date_of_last_service_newgrf = CalTime::CurDate();
		v->build_year = CalTime::CurYear();
		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = Random();

		v->group_id = DEFAULT_GROUP;

		if (TestVehicleBuildProbability(v, v->engine_type, BuildProbabilityType::Reversed)) SetBit(v->flags, VRF_REVERSE_DIRECTION);
		AddArticulatedParts(v);

		_new_vehicle_id = v->index;

		v->UpdatePosition();
		v->First()->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v->First());

		CheckConsistencyOfArticulatedVehicle(v);

		/* Try to connect the vehicle to one of free chains of wagons. */
		std::vector<Train *> candidates;
		for (Train *w = Train::From(GetFirstVehicleOnPos(tile, VEH_TRAIN)); w != nullptr; w = w->HashTileNext()) {
			if (w->IsFreeWagon() &&                 ///< A free wagon chain
					w->engine_type == e->index &&   ///< Same type
					w->First() != v &&              ///< Don't connect to ourself
					!(w->vehstatus & VS_CRASHED) && ///< Not crashed/flooded
					w->owner == v->owner) {         ///< Same owner
				candidates.push_back(w);
			}
		}
		std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
			return a->index < b->index;
		});
		for (Train *w : candidates) {
			if (DoCommand(0, v->index | 1 << 20, w->Last()->index, DC_EXEC, CMD_MOVE_RAIL_VEHICLE).Succeeded()) {
				break;
			}
		}

		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

/** Move all free vehicles in the depot to the train */
void NormalizeTrainVehInDepot(const Train *u)
{
	assert(u->IsEngine());
	std::vector<Train *> candidates;
	for (Train *v = Train::From(GetFirstVehicleOnPos(u->tile, VEH_TRAIN)); v != nullptr; v = v->HashTileNext()) {
		if (v->IsFreeWagon() &&
				v->track == TRACK_BIT_DEPOT &&
				v->owner == u->owner) {
			candidates.push_back(v);
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
		return a->index < b->index;
	});
	for (Train *v : candidates) {
		if (DoCommand(0, v->index | 1 << 20, u->index, DC_EXEC, CMD_MOVE_RAIL_VEHICLE).Failed()) break;
	}
}

static void AddRearEngineToMultiheadedTrain(Train *v)
{
	Train *u = new Train();
	v->value >>= 1;
	u->value = v->value;
	u->direction = v->direction;
	u->owner = v->owner;
	u->tile = v->tile;
	u->x_pos = v->x_pos;
	u->y_pos = v->y_pos;
	u->z_pos = v->z_pos;
	u->track = TRACK_BIT_DEPOT;
	u->vehstatus = v->vehstatus & ~VS_STOPPED;
	u->spritenum = v->spritenum + 1;
	u->cargo_type = v->cargo_type;
	u->cargo_subtype = v->cargo_subtype;
	u->cargo_cap = v->cargo_cap;
	u->refit_cap = v->refit_cap;
	u->railtype = v->railtype;
	u->engine_type = v->engine_type;
	u->reliability = v->reliability;
	u->reliability_spd_dec = v->reliability_spd_dec;
	u->date_of_last_service = v->date_of_last_service;
	u->date_of_last_service_newgrf = v->date_of_last_service_newgrf;
	u->build_year = v->build_year;
	u->sprite_seq.Set(SPR_IMG_QUERY);
	u->random_bits = Random();
	v->SetMultiheaded();
	u->SetMultiheaded();
	if (v->IsVirtual()) u->SetVirtual();
	v->SetNext(u);
	if (TestVehicleBuildProbability(u, u->engine_type, BuildProbabilityType::Reversed)) SetBit(u->flags, VRF_REVERSE_DIRECTION);
	u->UpdatePosition();

	/* Now we need to link the front and rear engines together */
	v->other_multiheaded_part = u;
	u->other_multiheaded_part = v;
}

/**
 * Build a railroad vehicle.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildRailVehicle(TileIndex tile, DoCommandFlag flags, const Engine *e, Vehicle **ret)
{
	const RailVehicleInfo *rvi = &e->u.rail;

	if (rvi->railveh_type == RAILVEH_WAGON) return CmdBuildRailWagon(tile, flags, e, ret);

	/* Check if depot and new engine uses the same kind of tracks *
	 * We need to see if the engine got power on the tile to avoid electric engines in non-electric depots */
	if (!HasPowerOnRail(rvi->railtype, GetRailType(tile))) return_cmd_error(STR_ERROR_DEPOT_HAS_WRONG_RAIL_TYPE);

	if (flags & DC_EXEC) {
		DiagDirection dir = GetRailDepotDirection(tile);
		int x = TileX(tile) * TILE_SIZE + _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE + _vehicle_initial_y_fract[dir];

		Train *v = new Train();
		*ret = v;
		v->direction = DiagDirToDir(dir);
		v->tile = tile;
		v->owner = _current_company;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y, true);
		v->track = TRACK_BIT_DEPOT;
		SetBit(v->flags, VRF_CONSIST_SPEED_REDUCTION);
		v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;
		v->spritenum = rvi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		assert(IsValidCargoID(v->cargo_type));
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;
		v->last_station_visited = INVALID_STATION;
		v->last_loading_station = INVALID_STATION;
		v->reverse_distance = 0;
		v->speed_restriction = 0;
		v->signal_speed_restriction = 0;

		v->engine_type = e->index;
		v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->GetLifeLengthInDays();

		v->railtype = rvi->railtype;
		_new_vehicle_id = v->index;

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_trains);
		v->date_of_last_service = EconTime::CurDate();
		v->date_of_last_service_newgrf = CalTime::CurDate();
		v->build_year = CalTime::CurYear();
		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = Random();

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) SetBit(v->vehicle_flags, VF_BUILT_AS_PROTOTYPE);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
		AssignBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
		AssignBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

		v->group_id = DEFAULT_GROUP;

		v->SetFrontEngine();
		v->SetEngine();

		if (TestVehicleBuildProbability(v, v->engine_type, BuildProbabilityType::Reversed)) SetBit(v->flags, VRF_REVERSE_DIRECTION);
		v->UpdatePosition();

		if (rvi->railveh_type == RAILVEH_MULTIHEAD) {
			AddRearEngineToMultiheadedTrain(v);
		} else {
			AddArticulatedParts(v);
		}

		v->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v);

		CheckConsistencyOfArticulatedVehicle(v);

		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

static std::vector<Train *> FindGoodVehiclePosList(const Train *src)
{
	EngineID eng = src->engine_type;
	TileIndex tile = src->tile;

	std::vector<Train *> candidates;

	for (Train *dst = Train::From(GetFirstVehicleOnPos(tile, VEH_TRAIN)); dst != nullptr; dst = dst->HashTileNext()) {
		if (dst->IsFreeWagon() && !(dst->vehstatus & VS_CRASHED) && dst->owner == src->owner) {
			/* check so all vehicles in the line have the same engine. */
			Train *t = dst;
			while (t->engine_type == eng) {
				t = t->Next();
				if (t == nullptr) {
					candidates.push_back(dst);
					break;
				}
			}
		}
	}

	std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
		return a->index < b->index;
	});

	return candidates;
}

/** Helper type for lists/vectors of trains */
typedef std::vector<Train *> TrainList;

/**
 * Make a backup of a train into a train list.
 * @param list to make the backup in
 * @param t    the train to make the backup of
 */
static void MakeTrainBackup(TrainList &list, Train *t)
{
	for (; t != nullptr; t = t->Next()) list.push_back(t);
}

/**
 * Restore the train from the backup list.
 * @param list the train to restore.
 */
static void RestoreTrainBackup(TrainList &list)
{
	/* No train, nothing to do. */
	if (list.empty()) return;

	Train *prev = nullptr;
	/* Iterate over the list and rebuild it. */
	for (Train *t : list) {
		if (prev != nullptr) {
			prev->SetNext(t);
		} else if (t->Previous() != nullptr) {
			/* Make sure the head of the train is always the first in the chain. */
			t->Previous()->SetNext(nullptr);
		}
		prev = t;
	}
}

/**
 * Remove the given wagon from its consist.
 * @param part the part of the train to remove.
 * @param chain whether to remove the whole chain.
 */
static void RemoveFromConsist(Train *part, bool chain = false)
{
	Train *tail = chain ? part->Last() : part->GetLastEnginePart();

	/* Unlink at the front, but make it point to the next
	 * vehicle after the to be remove part. */
	if (part->Previous() != nullptr) part->Previous()->SetNext(tail->Next());

	/* Unlink at the back */
	tail->SetNext(nullptr);
}

/**
 * Inserts a chain into the train at dst.
 * @param dst   the place where to append after.
 * @param chain the chain to actually add.
 */
static void InsertInConsist(Train *dst, Train *chain)
{
	/* We do not want to add something in the middle of an articulated part. */
	assert(dst != nullptr && (dst->Next() == nullptr || !dst->Next()->IsArticulatedPart()));

	chain->Last()->SetNext(dst->Next());
	dst->SetNext(chain);
}

/**
 * Normalise the dual heads in the train, i.e. if one is
 * missing move that one to this train.
 * @param t the train to normalise.
 */
static void NormaliseDualHeads(Train *t)
{
	for (; t != nullptr; t = t->GetNextVehicle()) {
		if (!t->IsMultiheaded() || !t->IsEngine()) continue;

		/* Make sure that there are no free cars before next engine */
		Train *u;
		for (u = t; u->Next() != nullptr && !u->Next()->IsEngine(); u = u->Next()) {}

		if (u == t->other_multiheaded_part) continue;

		/* Remove the part from the 'wrong' train */
		RemoveFromConsist(t->other_multiheaded_part);
		/* And add it to the 'right' train */
		InsertInConsist(u, t->other_multiheaded_part);
	}
}

/**
 * Normalise the sub types of the parts in this chain.
 * @param chain the chain to normalise.
 */
static void NormaliseSubtypes(Train *chain)
{
	/* Nothing to do */
	if (chain == nullptr) return;

	/* We must be the first in the chain. */
	assert(chain->Previous() == nullptr);

	/* Set the appropriate bits for the first in the chain. */
	if (chain->IsWagon()) {
		chain->SetFreeWagon();
	} else {
		assert(chain->IsEngine());
		chain->SetFrontEngine();
	}

	/* Now clear the bits for the rest of the chain */
	for (Train *t = chain->Next(); t != nullptr; t = t->Next()) {
		t->ClearFreeWagon();
		t->ClearFrontEngine();
	}
}

/**
 * Check/validate whether we may actually build a new train.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @return possible error of this command.
 */
static CommandCost CheckNewTrain(Train *original_dst, Train *dst, Train *original_src, Train *src)
{
	/* Just add 'new' engines and subtract the original ones.
	 * If that's less than or equal to 0 we can be sure we did
	 * not add any engines (read: trains) along the way. */
	if ((src          != nullptr && src->IsEngine()          ? 1 : 0) +
			(dst          != nullptr && dst->IsEngine()          ? 1 : 0) -
			(original_src != nullptr && original_src->IsEngine() ? 1 : 0) -
			(original_dst != nullptr && original_dst->IsEngine() ? 1 : 0) <= 0) {
		return CommandCost();
	}

	/* Get a free unit number and check whether it's within the bounds.
	 * There will always be a maximum of one new train. */
	if (GetFreeUnitNumber(VEH_TRAIN) <= _settings_game.vehicle.max_trains) return CommandCost();

	return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
}

/**
 * Check whether the train parts can be attached.
 * @param t the train to check
 * @return possible error of this command.
 */
static CommandCost CheckTrainAttachment(Train *t)
{
	/* No multi-part train, no need to check. */
	if (t == nullptr || t->Next() == nullptr) return CommandCost();

	/* The maximum length for a train. For each part we decrease this by one
	 * and if the result is negative the train is simply too long. */
	int allowed_len = _settings_game.vehicle.max_train_length * TILE_SIZE - t->gcache.cached_veh_length;

	/* For free-wagon chains, check if they are within the max_train_length limit. */
	if (!t->IsEngine()) {
		t = t->Next();
		while (t != nullptr) {
			allowed_len -= t->gcache.cached_veh_length;

			t = t->Next();
		}

		if (allowed_len < 0) return_cmd_error(STR_ERROR_TRAIN_TOO_LONG);
		return CommandCost();
	}

	Train *head = t;
	Train *prev = t;

	/* Break the prev -> t link so it always holds within the loop. */
	t = t->Next();
	prev->SetNext(nullptr);

	/* Make sure the cache is cleared. */
	head->InvalidateNewGRFCache();

	while (t != nullptr) {
		allowed_len -= t->gcache.cached_veh_length;

		Train *next = t->Next();

		/* Unlink the to-be-added piece; it is already unlinked from the previous
		 * part due to the fact that the prev -> t link is broken. */
		t->SetNext(nullptr);

		/* Don't check callback for articulated or rear dual headed parts */
		if (!t->IsArticulatedPart() && !t->IsRearDualheaded()) {
			/* Back up and clear the first_engine data to avoid using wagon override group */
			EngineID first_engine = t->gcache.first_engine;
			t->gcache.first_engine = INVALID_ENGINE;

			/* We don't want the cache to interfere. head's cache is cleared before
			 * the loop and after each callback does not need to be cleared here. */
			t->InvalidateNewGRFCache();

			uint16_t callback = GetVehicleCallbackParent(CBID_TRAIN_ALLOW_WAGON_ATTACH, 0, 0, head->engine_type, t, head);

			/* Restore original first_engine data */
			t->gcache.first_engine = first_engine;

			/* We do not want to remember any cached variables from the test run */
			t->InvalidateNewGRFCache();
			head->InvalidateNewGRFCache();

			if (callback != CALLBACK_FAILED) {
				/* A failing callback means everything is okay */
				StringID error = STR_NULL;

				if (head->GetGRF()->grf_version < 8) {
					if (callback == 0xFD) error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
					if (callback  < 0xFD) error = GetGRFStringID(head->GetGRF(), 0xD000 + callback);
					if (callback >= 0x100) ErrorUnknownCallbackResult(head->GetGRFID(), CBID_TRAIN_ALLOW_WAGON_ATTACH, callback);
				} else {
					if (callback < 0x400) {
						error = GetGRFStringID(head->GetGRF(), 0xD000 + callback);
					} else {
						switch (callback) {
							case 0x400: // allow if railtypes match (always the case for OpenTTD)
							case 0x401: // allow
								break;

							default:    // unknown reason -> disallow
							case 0x402: // disallow attaching
								error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
								break;
						}
					}
				}

				if (error != STR_NULL) return_cmd_error(error);
			}
		}

		/* And link it to the new part. */
		prev->SetNext(t);
		prev = t;
		t = next;
	}

	if (allowed_len < 0) return_cmd_error(STR_ERROR_TRAIN_TOO_LONG);
	return CommandCost();
}

/**
 * Validate whether we are going to create valid trains.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @param check_limit  Whether to check the vehicle limit.
 * @return possible error of this command.
 */
static CommandCost ValidateTrains(Train *original_dst, Train *dst, Train *original_src, Train *src, bool check_limit)
{
	/* Check whether we may actually construct the trains. */
	CommandCost ret = CheckTrainAttachment(src);
	if (ret.Failed()) return ret;
	ret = CheckTrainAttachment(dst);
	if (ret.Failed()) return ret;

	/* Check whether we need to build a new train. */
	return check_limit ? CheckNewTrain(original_dst, dst, original_src, src) : CommandCost();
}

/**
 * Arrange the trains in the wanted way.
 * @param dst_head   The destination chain of the to be moved vehicle.
 * @param dst        The destination for the to be moved vehicle.
 * @param src_head   The source chain of the to be moved vehicle.
 * @param src        The to be moved vehicle.
 * @param move_chain Whether to move all vehicles after src or not.
 */
static void ArrangeTrains(Train **dst_head, Train *dst, Train **src_head, Train *src, bool move_chain)
{
	/* First determine the front of the two resulting trains */
	if (*src_head == *dst_head) {
		/* If we aren't moving part(s) to a new train, we are just moving the
		 * front back and there is not destination head. */
		*dst_head = nullptr;
	} else if (*dst_head == nullptr) {
		/* If we are moving to a new train the head of the move train would become
		 * the head of the new vehicle. */
		*dst_head = src;
	}

	if (src == *src_head) {
		/* If we are moving the front of a train then we are, in effect, creating
		 * a new head for the train. Point to that. Unless we are moving the whole
		 * train in which case there is not 'source' train anymore.
		 * In case we are a multiheaded part we want the complete thing to come
		 * with us, so src->GetNextUnit(), however... when we are e.g. a wagon
		 * that is followed by a rear multihead we do not want to include that. */
		*src_head = move_chain ? nullptr :
				(src->IsMultiheaded() ? src->GetNextUnit() : src->GetNextVehicle());
	}

	/* Now it's just simply removing the part that we are going to move from the
	 * source train and *if* the destination is a not a new train add the chain
	 * at the destination location. */
	RemoveFromConsist(src, move_chain);
	if (*dst_head != src) InsertInConsist(dst, src);

	/* Now normalise the dual heads, that is move the dual heads around in such
	 * a way that the head and rear of a dual head are in the same train */
	NormaliseDualHeads(*src_head);
	NormaliseDualHeads(*dst_head);
}

/**
 * Normalise the head of the train again, i.e. that is tell the world that
 * we have changed and update all kinds of variables.
 * @param head the train to update.
 */
static void NormaliseTrainHead(Train *head)
{
	/* Not much to do! */
	if (head == nullptr) return;

	/* Tell the 'world' the train changed. */
	head->ConsistChanged(CCF_ARRANGE);
	UpdateTrainGroupID(head);
	SetBit(head->flags, VRF_CONSIST_SPEED_REDUCTION);

	/* Not a front engine, i.e. a free wagon chain. No need to do more. */
	if (!head->IsFrontEngine()) return;

	/* Update the refit button and window */
	InvalidateWindowData(WC_VEHICLE_REFIT, head->index, VIWD_CONSIST_CHANGED);
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, head->index, WID_VV_REFIT);

	/* If we don't have a unit number yet, set one. */
	if (head->unitnumber != 0 || HasBit(head->subtype, GVSF_VIRTUAL)) return;
	head->unitnumber = Company::Get(head->owner)->freeunits[head->type].UseID(GetFreeUnitNumber(VEH_TRAIN));
}

CommandCost CmdMoveVirtualRailVehicle(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Train *src = Train::GetIfValid(GB(p1, 0, 20));
	if (src == nullptr || !src->IsVirtual()) return CMD_ERROR;

	return CmdMoveRailVehicle(tile, flags, p1, p2, text);
}

/**
 * Move a rail vehicle around inside the depot.
 * @param tile unused
 * @param flags type of operation
 *              Note: DC_AUTOREPLACE is set when autoreplace tries to undo its modifications or moves vehicles to temporary locations inside the depot.
 * @param p1 various bitstuffed elements
 * - p1 (bit  0 - 19) source vehicle index
 * - p1 (bit      20) move all vehicles following the source vehicle
 * - p1 (bit      21) this is a virtual vehicle (for creating TemplateVehicles)
 * - p1 (bit      22) when moving a head vehicle, always reset the head state
 * - p1 (bit      23) if move fails, and source vehicle is virtual, delete it
 * @param p2 what wagon to put the source wagon AFTER, XXX - INVALID_VEHICLE to make a new line
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdMoveRailVehicle(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	VehicleID s = GB(p1, 0, 20);
	VehicleID d = GB(p2, 0, 20);
	bool move_chain = HasBit(p1, 20);
	bool new_head = HasBit(p1, 22);
	bool delete_failed_virtual = HasBit(p1, 23);

	Train *src = Train::GetIfValid(s);
	if (src == nullptr) return CMD_ERROR;

	auto check_on_failure = [&](CommandCost cost) -> CommandCost {
		if (delete_failed_virtual && src->IsVirtual()) {
			CommandCost res = DoCommand(src->tile, src->index | (1 << 21), 0, flags, CMD_SELL_VEHICLE);
			if (res.Failed() || cost.GetErrorMessage() == INVALID_STRING_ID) return res;
			cost.MakeSuccessWithMessage();
			return cost;
		} else {
			return cost;
		}
	};

	CommandCost ret = CheckOwnership(src->owner);
	if (ret.Failed()) return check_on_failure(ret);

	/* Do not allow moving crashed vehicles inside the depot, it is likely to cause asserts later */
	if (src->vehstatus & VS_CRASHED) return CMD_ERROR;

	/* if nothing is selected as destination, try and find a matching vehicle to drag to. */
	Train *dst;
	if (d == INVALID_VEHICLE) {
		if (!src->IsEngine() && !src->IsVirtual() && !(flags & DC_AUTOREPLACE)) {
			/* Try each possible destination target, if none succeed do not append to a free wagon chain */
			std::vector<Train *> destination_candidates = FindGoodVehiclePosList(src);
			for (Train *try_dest : destination_candidates) {
				uint32_t try_p2 = p2;
				SB(try_p2, 0, 20, try_dest->index);
				CommandCost cost = CmdMoveRailVehicle(tile, flags, p1, try_p2, text);
				if (cost.Succeeded()) return cost;
			}
		}
		dst = nullptr;
	} else {
		dst = Train::GetIfValid(d);
		if (dst == nullptr) return check_on_failure(CMD_ERROR);

		CommandCost ret = CheckOwnership(dst->owner);
		if (ret.Failed()) return check_on_failure(ret);

		/* Do not allow appending to crashed vehicles, too */
		if (dst->vehstatus & VS_CRASHED) return CMD_ERROR;
	}

	/* if an articulated part is being handled, deal with its parent vehicle */
	src = src->GetFirstEnginePart();
	if (dst != nullptr) {
		dst = dst->GetFirstEnginePart();
		if (HasBit(dst->subtype, GVSF_VIRTUAL) != HasBit(src->subtype, GVSF_VIRTUAL)) return CMD_ERROR;
	}

	/* don't move the same vehicle.. */
	if (src == dst) return CommandCost();

	/* locate the head of the two chains */
	Train *src_head = src->First();
	assert(HasBit(src_head->subtype, GVSF_VIRTUAL) == HasBit(src->subtype, GVSF_VIRTUAL));
	Train *dst_head;
	if (dst != nullptr) {
		dst_head = dst->First();
		assert(HasBit(dst_head->subtype, GVSF_VIRTUAL) == HasBit(dst->subtype, GVSF_VIRTUAL));
		if (dst_head->tile != src_head->tile) return CMD_ERROR;
		/* Now deal with articulated part of destination wagon */
		dst = dst->GetLastEnginePart();
	} else {
		dst_head = nullptr;
	}

	if (src->IsRearDualheaded()) return_cmd_error(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	/* When moving all wagons, we can't have the same src_head and dst_head */
	if (move_chain && src_head == dst_head) return CommandCost();

	/* When moving a multiheaded part to be place after itself, bail out. */
	if (!move_chain && dst != nullptr && dst->IsRearDualheaded() && src == dst->other_multiheaded_part) return CommandCost();

	/* Check if all vehicles in the source train are stopped inside a depot. */
	/* Do this check only if the vehicle to be moved is non-virtual */
	if (!HasBit(p1, 21)) {
		if (!src_head->IsStoppedInDepot()) return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
	}

	/* Check if all vehicles in the destination train are stopped inside a depot. */
	/* Do this check only if the destination vehicle is non-virtual */
	if (!HasBit(p1, 21)) {
		if (dst_head != nullptr && !dst_head->IsStoppedInDepot()) return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
	}

	/* First make a backup of the order of the trains. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original_src;
	TrainList original_dst;

	MakeTrainBackup(original_src, src_head);
	MakeTrainBackup(original_dst, dst_head);

	/* Also make backup of the original heads as ArrangeTrains can change them.
	 * For the destination head we do not care if it is the same as the source
	 * head because in that case it's just a copy. */
	Train *original_src_head = src_head;
	Train *original_dst_head = (dst_head == src_head ? nullptr : dst_head);

	/* We want this information from before the rearrangement, but execute this after the validation.
	 * original_src_head can't be nullptr; src is by definition != nullptr, so src_head can't be nullptr as
	 * src->GetFirst() always yields non-nullptr, so eventually original_src_head != nullptr as well. */
	bool original_src_head_front_engine = original_src_head->IsFrontEngine();
	bool original_dst_head_front_engine = original_dst_head != nullptr && original_dst_head->IsFrontEngine();

	/* (Re)arrange the trains in the wanted arrangement. */
	ArrangeTrains(&dst_head, dst, &src_head, src, move_chain);

	if ((flags & DC_AUTOREPLACE) == 0) {
		/* If the autoreplace flag is set we do not need to test for the validity
		 * because we are going to revert the train to its original state. As we
		 * assume the original state was correct autoreplace can skip this. */
		ret = ValidateTrains(original_dst_head, dst_head, original_src_head, src_head, true);
		if (ret.Failed()) {
			/* Restore the train we had. */
			RestoreTrainBackup(original_src);
			RestoreTrainBackup(original_dst);
			return check_on_failure(ret);
		}
	}

	/* do it? */
	if (flags & DC_EXEC) {
		/* Remove old heads from the statistics */
		if (original_src_head_front_engine) GroupStatistics::CountVehicle(original_src_head, -1);
		if (original_dst_head_front_engine) GroupStatistics::CountVehicle(original_dst_head, -1);

		/* First normalise the sub types of the chains. */
		NormaliseSubtypes(src_head);
		NormaliseSubtypes(dst_head);

		/* There are 14 different cases:
		 *  1) front engine gets moved to a new train, it stays a front engine.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  2) front engine gets moved to another train, it is not a front engine anymore
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  3) front engine gets moved to later in the current train, it is not a front engine anymore.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *  4) free wagon gets moved
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  5) non front engine gets moved and becomes a new train, nothing else happens
		 *  6) non front engine gets moved within a train / to another train, nothing happens
		 *  7) wagon gets moved, nothing happens
		 */
		if (src == original_src_head && src->IsEngine() && (!src->IsFrontEngine() || new_head)) {
			/* Cases #2 and #3: the front engine gets trashed. */
			CloseWindowById(WC_VEHICLE_VIEW, src->index);
			CloseWindowById(WC_VEHICLE_ORDERS, src->index);
			CloseWindowById(WC_VEHICLE_REFIT, src->index);
			CloseWindowById(WC_VEHICLE_DETAILS, src->index);
			CloseWindowById(WC_VEHICLE_TIMETABLE, src->index);
			CloseWindowById(WC_SCHDISPATCH_SLOTS, src->index);
			DeleteNewGRFInspectWindow(GSF_TRAINS, src->index);
			SetWindowDirty(WC_COMPANY, _current_company);

			if (src_head != nullptr && src_head->IsFrontEngine()) {
				/* Cases #?b: Transfer order, unit number and other stuff
				 * to the new front engine. */
				src_head->orders = src->orders;
				if (src_head->orders != nullptr) src_head->AddToShared(src);
				src_head->CopyVehicleConfigAndStatistics(src);
			}
			/* Remove stuff not valid anymore for non-front engines. */
			DeleteVehicleOrders(src);
			src->ReleaseUnitNumber();
			src->dispatch_records.clear();
			if (!_settings_game.vehicle.non_leading_engines_keep_name) {
				src->name.clear();
			}
			if (HasBit(src->vehicle_flags, VF_HAVE_SLOT)) {
				TraceRestrictRemoveVehicleFromAllSlots(src->index);
				ClrBit(src->vehicle_flags, VF_HAVE_SLOT);
			}
			ClrBit(src->vehicle_flags, VF_REPLACEMENT_PENDING);
			OrderBackup::ClearVehicle(src);
		}

		/* We weren't a front engine but are becoming one. So
		 * we should be put in the default group. */
		if ((original_src_head != src || new_head) && dst_head == src) {
			SetTrainGroupID(src, DEFAULT_GROUP);
			SetWindowDirty(WC_COMPANY, _current_company);
		}

		/* Handle 'new engine' part of cases #1b, #2b, #3b, #4b and #5 in NormaliseTrainHead. */
		NormaliseTrainHead(src_head);
		NormaliseTrainHead(dst_head);

		/* Add new heads to statistics.
		 * This should be done after NormaliseTrainHead due to engine total limit checks in GetFreeUnitNumber. */
		if (src_head != nullptr && src_head->IsFrontEngine()) GroupStatistics::CountVehicle(src_head, 1);
		if (dst_head != nullptr && dst_head->IsFrontEngine()) GroupStatistics::CountVehicle(dst_head, 1);

		if ((flags & DC_NO_CARGO_CAP_CHECK) == 0) {
			CheckCargoCapacity(src_head);
			CheckCargoCapacity(dst_head);
		}

		if (src_head != nullptr) {
			src_head->last_loading_station = INVALID_STATION;
			ClrBit(src_head->vehicle_flags, VF_LAST_LOAD_ST_SEP);
		}
		if (dst_head != nullptr) {
			dst_head->last_loading_station = INVALID_STATION;
			ClrBit(dst_head->vehicle_flags, VF_LAST_LOAD_ST_SEP);
		}

		if (src_head != nullptr) src_head->First()->MarkDirty();
		if (dst_head != nullptr) dst_head->First()->MarkDirty();

		/* We are undoubtedly changing something in the depot and train list. */
		/* But only if the moved vehicle is not virtual */
		if (!HasBit(src->subtype, GVSF_VIRTUAL)) {
			InvalidateWindowData(WC_VEHICLE_DEPOT, src->tile);
			InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
			InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS, 0);
			InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
		}
	} else {
		/* We don't want to execute what we're just tried. */
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
	}

	InvalidateVehicleTickCaches();

	return CommandCost();
}

/**
 * Sell a (single) train wagon/engine.
 * @param flags type of operation
 * @param t     the train wagon to sell
 * @param data  the selling mode
 * - data = 0: only sell the single dragged wagon/engine (and any belonging rear-engines)
 * - data = 1: sell the vehicle and all vehicles following it in the chain
 *             if the wagon is dragged, don't delete the possibly belonging rear-engine to some front
 * @param user  the user for the order backup.
 * @return the cost of this operation or an error
 */
CommandCost CmdSellRailWagon(DoCommandFlag flags, Vehicle *t, uint16_t data, uint32_t user)
{
	/* Sell a chain of vehicles or not? */
	bool sell_chain = HasBit(data, 0);

	Train *v = Train::From(t)->GetFirstEnginePart();
	Train *first = v->First();

	if (v->IsRearDualheaded()) return_cmd_error(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	/* First make a backup of the order of the train. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original;
	MakeTrainBackup(original, first);

	/* We need to keep track of the new head and the head of what we're going to sell. */
	Train *new_head = first;
	Train *sell_head = nullptr;

	/* Split the train in the wanted way. */
	ArrangeTrains(&sell_head, nullptr, &new_head, v, sell_chain);

	/* We don't need to validate the second train; it's going to be sold. */
	CommandCost ret = ValidateTrains(nullptr, nullptr, first, new_head, (flags & DC_AUTOREPLACE) == 0);
	if (ret.Failed()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		return ret;
	}

	if (first->orders == nullptr && !OrderList::CanAllocateItem()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		return_cmd_error(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	}

	CommandCost cost(EXPENSES_NEW_VEHICLES);
	for (Train *part = sell_head; part != nullptr; part = part->Next()) cost.AddCost(-part->value);

	/* do it? */
	if (flags & DC_EXEC) {
		/* First normalise the sub types of the chain. */
		NormaliseSubtypes(new_head);

		if (v == first && !sell_chain && new_head != nullptr && new_head->IsFrontEngine()) {
			if (v->IsEngine()) {
				/* We are selling the front engine. In this case we want to
				 * 'give' the order, unit number and such to the new head. */
				new_head->orders = first->orders;
				new_head->AddToShared(first);
				DeleteVehicleOrders(first);

				/* Copy other important data from the front engine */
				new_head->CopyVehicleConfigAndStatistics(first);
				new_head->speed_restriction = first->speed_restriction;
				AssignBit(Train::From(new_head)->flags, VRF_SPEED_ADAPTATION_EXEMPT, HasBit(Train::From(first)->flags, VRF_SPEED_ADAPTATION_EXEMPT));
			}
			GroupStatistics::CountVehicle(new_head, 1); // after copying over the profit, if required
		} else if (v->IsPrimaryVehicle() && data & (MAKE_ORDER_BACKUP_FLAG >> 20)) {
			OrderBackup::Backup(v, user);
		}

		/* We need to update the information about the train. */
		NormaliseTrainHead(new_head);

		/* We are undoubtedly changing something in the depot and train list. */
		/* Unless its a virtual train */
		if (!HasBit(v->subtype, GVSF_VIRTUAL)) {
			InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
			InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
			InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS, 0);
			InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 0);
		}

		/* Actually delete the sold 'goods' */
		delete sell_head;
	} else {
		/* We don't want to execute what we're just tried. */
		RestoreTrainBackup(original);
	}

	return cost;
}

void Train::UpdateDeltaXY()
{
	/* Set common defaults. */
	this->x_offs    = -1;
	this->y_offs    = -1;
	this->x_extent  =  3;
	this->y_extent  =  3;
	this->z_extent  =  6;
	this->x_bb_offs =  0;
	this->y_bb_offs =  0;

	/* Set if flipped and engine is NOT flagged with custom flip handling. */
	int flipped = HasBit(this->flags, VRF_REVERSE_DIRECTION) && !HasBit(EngInfo(this->engine_type)->misc_flags, EF_RAIL_FLIPS);
	/* If flipped and vehicle length is odd, we need to adjust the bounding box offset slightly. */
	int flip_offs = flipped && (this->gcache.cached_veh_length & 1);

	Direction dir = this->direction;
	if (flipped) dir = ReverseDir(dir);

	if (!IsDiagonalDirection(dir)) {
		static const int _sign_table[] =
		{
			/* x, y */
			-1, -1, // DIR_N
			-1,  1, // DIR_E
			 1,  1, // DIR_S
			 1, -1, // DIR_W
		};

		int half_shorten = (VEHICLE_LENGTH - this->gcache.cached_veh_length + flipped) / 2;

		/* For all straight directions, move the bound box to the centre of the vehicle, but keep the size. */
		this->x_offs -= half_shorten * _sign_table[dir];
		this->y_offs -= half_shorten * _sign_table[dir + 1];
		this->x_extent += this->x_bb_offs = half_shorten * _sign_table[dir];
		this->y_extent += this->y_bb_offs = half_shorten * _sign_table[dir + 1];
	} else {
		switch (dir) {
				/* Shorten southern corner of the bounding box according the vehicle length
				 * and center the bounding box on the vehicle. */
			case DIR_NE:
				this->x_offs    = 1 - (this->gcache.cached_veh_length + 1) / 2 + flip_offs;
				this->x_extent  = this->gcache.cached_veh_length - 1;
				this->x_bb_offs = -1;
				break;

			case DIR_NW:
				this->y_offs    = 1 - (this->gcache.cached_veh_length + 1) / 2 + flip_offs;
				this->y_extent  = this->gcache.cached_veh_length - 1;
				this->y_bb_offs = -1;
				break;

				/* Move northern corner of the bounding box down according to vehicle length
				 * and center the bounding box on the vehicle. */
			case DIR_SW:
				this->x_offs    = 1 + (this->gcache.cached_veh_length + 1) / 2 - VEHICLE_LENGTH - flip_offs;
				this->x_extent  = VEHICLE_LENGTH - 1;
				this->x_bb_offs = VEHICLE_LENGTH - this->gcache.cached_veh_length - 1;
				break;

			case DIR_SE:
				this->y_offs    = 1 + (this->gcache.cached_veh_length + 1) / 2 - VEHICLE_LENGTH - flip_offs;
				this->y_extent  = VEHICLE_LENGTH - 1;
				this->y_bb_offs = VEHICLE_LENGTH - this->gcache.cached_veh_length - 1;
				break;

			default:
				NOT_REACHED();
		}
	}
}

/**
 * Mark a train as stuck and stop it if it isn't stopped right now.
 * @param v %Train to mark as being stuck.
 */
static void MarkTrainAsStuck(Train *v, bool waiting_restriction = false)
{
	if (!HasBit(v->flags, VRF_TRAIN_STUCK)) {
		/* It is the first time the problem occurred, set the "train stuck" flag. */
		SetBit(v->flags, VRF_TRAIN_STUCK);
		AssignBit(v->flags, VRF_WAITING_RESTRICTION, waiting_restriction);

		v->wait_counter = 0;

		/* Stop train */
		v->cur_speed = 0;
		v->subspeed = 0;
		v->SetLastSpeed();

		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	} else if (waiting_restriction != HasBit(v->flags, VRF_WAITING_RESTRICTION)) {
		ToggleBit(v->flags, VRF_WAITING_RESTRICTION);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}
}

/**
 * Swap the two up/down flags in two ways:
 * - Swap values of \a swap_flag1 and \a swap_flag2, and
 * - If going up previously (#GVF_GOINGUP_BIT set), the #GVF_GOINGDOWN_BIT is set, and vice versa.
 * @param[in,out] swap_flag1 First train flag.
 * @param[in,out] swap_flag2 Second train flag.
 */
static void SwapTrainFlags(uint16_t *swap_flag1, uint16_t *swap_flag2)
{
	uint16_t flag1 = *swap_flag1;
	uint16_t flag2 = *swap_flag2;

	/* Clear the flags */
	ClrBit(*swap_flag1, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	ClrBit(*swap_flag1, GVF_CHUNNEL_BIT);
	ClrBit(*swap_flag2, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag2, GVF_GOINGDOWN_BIT);
	ClrBit(*swap_flag2, GVF_CHUNNEL_BIT);

	/* Reverse the rail-flags (if needed) */
	if (HasBit(flag1, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag1, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGUP_BIT);
	}
	if (HasBit(flag2, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag2, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGUP_BIT);
	}
	if (HasBit(flag1, GVF_CHUNNEL_BIT)) {
		SetBit(*swap_flag2, GVF_CHUNNEL_BIT);
	}
	if (HasBit(flag2, GVF_CHUNNEL_BIT)) {
		SetBit(*swap_flag1, GVF_CHUNNEL_BIT);
	}
}

/**
 * Updates some variables after swapping the vehicle.
 * @param v swapped vehicle
 */
static void UpdateStatusAfterSwap(Train *v)
{
	v->InvalidateImageCache();

	/* Reverse the direction. */
	if (v->track != TRACK_BIT_DEPOT) v->direction = ReverseDir(v->direction);

	v->UpdateIsDrawn();

	/* Call the proper EnterTile function unless we are in a wormhole. */
	if (!(v->track & TRACK_BIT_WORMHOLE)) {
		VehicleEnterTile(v, v->tile, v->x_pos, v->y_pos);
	} else {
		/* VehicleEnter_TunnelBridge() may set TRACK_BIT_WORMHOLE when the vehicle
		 * is on the last bit of the bridge head (frame == TILE_SIZE - 1).
		 * If we were swapped with such a vehicle, we have set TRACK_BIT_WORMHOLE,
		 * when we shouldn't have. Check if this is the case. */
		TileIndex vt = TileVirtXY(v->x_pos, v->y_pos);
		if (IsTileType(vt, MP_TUNNELBRIDGE)) {
			VehicleEnterTile(v, vt, v->x_pos, v->y_pos);
			if (!(v->track & TRACK_BIT_WORMHOLE) && IsBridgeTile(v->tile)) {
				/* We have just left the wormhole, possibly set the
				 * "goingdown" bit. UpdateInclination() can be used
				 * because we are at the border of the tile. */
				v->UpdatePosition();
				v->UpdateInclination(true, true);
				return;
			}
		}
	}

	v->UpdatePosition();
	if (v->track & TRACK_BIT_WORMHOLE) v->UpdateInclination(false, false, true);
	v->UpdateViewport(true, true);
}

/**
 * Swap vehicles \a l and \a r in consist \a v, and reverse their direction.
 * @param v Consist to change.
 * @param l %Vehicle index in the consist of the first vehicle.
 * @param r %Vehicle index in the consist of the second vehicle.
 */
void ReverseTrainSwapVeh(Train *v, int l, int r)
{
	Train *a, *b;

	/* locate vehicles to swap */
	for (a = v; l != 0; l--) a = a->Next();
	for (b = v; r != 0; r--) b = b->Next();

	if (a != b) {
		/* swap the hidden bits */
		{
			uint16_t tmp = (a->vehstatus & ~VS_HIDDEN) | (b->vehstatus & VS_HIDDEN);
			b->vehstatus = (b->vehstatus & ~VS_HIDDEN) | (a->vehstatus & VS_HIDDEN);
			a->vehstatus = tmp;
		}

		Swap(a->track, b->track);
		Swap(a->direction, b->direction);
		Swap(a->x_pos, b->x_pos);
		Swap(a->y_pos, b->y_pos);
		Swap(a->tile,  b->tile);
		Swap(a->z_pos, b->z_pos);

		SwapTrainFlags(&a->gv_flags, &b->gv_flags);

		UpdateStatusAfterSwap(a);
		UpdateStatusAfterSwap(b);
	} else {
		/* Swap GVF_GOINGUP_BIT/GVF_GOINGDOWN_BIT.
		 * This is a little bit redundant way, a->gv_flags will
		 * be (re)set twice, but it reduces code duplication */
		SwapTrainFlags(&a->gv_flags, &a->gv_flags);
		UpdateStatusAfterSwap(a);
	}
}


/**
 * Check if the vehicle is a train
 * @param v vehicle on tile
 * @return v if it is a train, nullptr otherwise
 */
static Vehicle *TrainOnTileEnum(Vehicle *v, void *)
{
	return v;
}

/**
 * Check if a level crossing tile has a train on it
 * @param tile tile to test
 * @return true if a train is on the crossing
 * @pre tile is a level crossing
 */
bool TrainOnCrossing(TileIndex tile)
{
	assert(IsLevelCrossingTile(tile));

	return HasVehicleOnPos(tile, VEH_TRAIN, nullptr, &TrainOnTileEnum);
}


/**
 * Checks if a train is approaching a rail-road crossing
 * @param v vehicle on tile
 * @param data tile with crossing we are testing
 * @return v if it is approaching a crossing, nullptr otherwise
 */
static Vehicle *TrainApproachingCrossingEnum(Vehicle *v, void *data)
{
	if ((v->vehstatus & VS_CRASHED)) return nullptr;

	Train *t = Train::From(v);
	if (!t->IsFrontEngine()) return nullptr;

	TileIndex tile = (TileIndex) reinterpret_cast<uintptr_t>(data);

	if (TrainApproachingCrossingTile(t) != tile) return nullptr;

	return t;
}


/**
 * Finds a vehicle approaching rail-road crossing
 * @param tile tile to test
 * @return true if a vehicle is approaching the crossing
 * @pre tile is a rail-road crossing
 */
static bool TrainApproachingCrossing(TileIndex tile)
{
	dbg_assert_tile(IsLevelCrossingTile(tile), tile);

	DiagDirection dir = AxisToDiagDir(GetCrossingRailAxis(tile));
	TileIndex tile_from = tile + TileOffsByDiagDir(dir);

	if (HasVehicleOnPos(tile_from, VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainApproachingCrossingEnum)) return true;

	dir = ReverseDiagDir(dir);
	tile_from = tile + TileOffsByDiagDir(dir);

	return HasVehicleOnPos(tile_from, VEH_TRAIN, reinterpret_cast<void *>((uintptr_t)tile), &TrainApproachingCrossingEnum);
}

/** Check if the crossing should be closed
 *  @return train on crossing || train approaching crossing || reserved
 */
static inline bool CheckLevelCrossing(TileIndex tile)
{
	/* reserved || train on crossing || train approaching crossing */
	return HasCrossingReservation(tile) || TrainOnCrossing(tile) || TrainApproachingCrossing(tile);
}

/**
 * Sets correct crossing state
 * @param tile tile to update
 * @param sound should we play sound?
 * @param is_forced force set the crossing state to that of forced_state
 * @param forced_state the crossing state to set when using is_forced
 * @pre tile is a rail-road crossing
 */
static void UpdateLevelCrossingTile(TileIndex tile, bool sound, bool is_forced, bool forced_state)
{
	dbg_assert_tile(IsLevelCrossingTile(tile), tile);
	bool new_state;

	if (is_forced) {
		new_state = forced_state;
	} else {
		new_state = CheckLevelCrossing(tile);
	}

	if (new_state != IsCrossingBarred(tile)) {
		if (new_state && sound) {
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, tile);
		}
		SetCrossingBarred(tile, new_state);
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
}

/**
 * Cycles the adjacent crossings and sets their state
 * @param tile tile to update
 * @param sound should we play sound?
 * @param force_close force close the crossing
 */
void UpdateLevelCrossing(TileIndex tile, bool sound, bool force_close)
{
	bool forced_state = force_close;
	if (!IsLevelCrossingTile(tile)) return;

	const Axis axis = GetCrossingRoadAxis(tile);
	const DiagDirection dir = AxisToDiagDir(axis);
	const DiagDirection reverse_dir = ReverseDiagDir(dir);

	const bool adjacent_crossings = _settings_game.vehicle.adjacent_crossings;
	if (adjacent_crossings) {
		for (TileIndex t = tile; !forced_state && t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, dir)) {
			forced_state |= CheckLevelCrossing(t);
		}
		for (TileIndex t = TileAddByDiagDir(tile, reverse_dir); !forced_state && t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, reverse_dir)) {
			forced_state |= CheckLevelCrossing(t);
		}
	}

	UpdateLevelCrossingTile(tile, sound, adjacent_crossings || force_close, forced_state);
	for (TileIndex t = TileAddByDiagDir(tile, dir); t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, dir)) {
		UpdateLevelCrossingTile(t, sound, adjacent_crossings, forced_state);
	}
	for (TileIndex t = TileAddByDiagDir(tile, reverse_dir); t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, reverse_dir)) {
		UpdateLevelCrossingTile(t, sound, adjacent_crossings, forced_state);
	}
}

void MarkDirtyAdjacentLevelCrossingTilesOnAdd(TileIndex tile, Axis road_axis)
{
	if (!_settings_game.vehicle.adjacent_crossings) return;

	const DiagDirection dir1 = AxisToDiagDir(road_axis);
	const DiagDirection dir2 = ReverseDiagDir(dir1);
	for (DiagDirection dir : { dir1, dir2 }) {
		const TileIndex t = TileAddByDiagDir(tile, dir);
		if (t < MapSize() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis) {
			MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
		}
	}
}

void UpdateAdjacentLevelCrossingTilesOnRemove(TileIndex tile, Axis road_axis)
{
	const DiagDirection dir1 = AxisToDiagDir(road_axis);
	const DiagDirection dir2 = ReverseDiagDir(dir1);
	for (DiagDirection dir : { dir1, dir2 }) {
		const TileIndexDiff diff = TileOffsByDiagDir(dir);
		bool occupied = false;
		for (TileIndex t = tile + diff; IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis; t += diff) {
			occupied |= CheckLevelCrossing(t);
		}
		if (occupied) {
			/* Mark the immediately adjacent tile dirty */
			const TileIndex t = tile + diff;
			if (IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis) {
				MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
			}
		} else {
			/* Unbar the crossing tiles in this direction as necessary */
			for (TileIndex t = tile + diff; IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis; t += diff) {
				if (IsCrossingBarred(t)) {
					/* The crossing tile is barred, unbar it and continue to check the next tile */
					SetCrossingBarred(t, false);
					MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
				} else {
					/* The crossing tile is already unbarred, mark the tile dirty and stop checking */
					MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
					break;
				}
			}
		}
	}
}

/**
 * Check if the level crossing is occupied by road vehicle(s).
 * @param t The tile to query.
 * @pre IsLevelCrossing(t)
 * @return True if the level crossing is marked as occupied.
 */
bool IsCrossingOccupiedByRoadVehicle(TileIndex t)
{
	if (!IsCrossingPossiblyOccupiedByRoadVehicle(t)) return false;
	const bool occupied = IsTrainCollidableRoadVehicleOnGround(t);
	SetCrossingOccupiedByRoadVehicle(t, occupied);
	return occupied;
}


/**
 * Bars crossing and plays ding-ding sound if not barred already
 * @param tile tile with crossing
 * @pre tile is a rail-road crossing
 */
static inline void MaybeBarCrossingWithSound(TileIndex tile)
{
	if (!IsCrossingBarred(tile)) {
		UpdateLevelCrossing(tile, true, true);
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called before the train is reversed.
 * @param v First vehicle in chain
 */
static void AdvanceWagonsBeforeSwap(Train *v)
{
	Train *base = v;
	Train *first = base; // first vehicle to move
	Train *last = v->Last(); // last vehicle to move
	uint length = CountVehiclesInChain(v);

	while (length > 2) {
		last = last->Previous();
		first = first->Next();

		int differential = base->CalcNextVehicleOffset() - last->CalcNextVehicleOffset();

		/* do not update images now
		 * negative differential will be handled in AdvanceWagonsAfterSwap() */
		for (int i = 0; i < differential; i++) TrainController(first, last->Next());

		base = first; // == base->Next()
		length -= 2;
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called after the train is reversed.
 * @param v First vehicle in chain
 */
static void AdvanceWagonsAfterSwap(Train *v)
{
	/* first of all, fix the situation when the train was entering a depot */
	Train *dep = v; // last vehicle in front of just left depot
	while (dep->Next() != nullptr && (dep->track == TRACK_BIT_DEPOT || dep->Next()->track != TRACK_BIT_DEPOT)) {
		dep = dep->Next(); // find first vehicle outside of a depot, with next vehicle inside a depot
	}

	Train *leave = dep->Next(); // first vehicle in a depot we are leaving now

	if (leave != nullptr) {
		/* 'pull' next wagon out of the depot, so we won't miss it (it could stay in depot forever) */
		int d = TicksToLeaveDepot(dep);

		if (d <= 0) {
			leave->vehstatus &= ~VS_HIDDEN; // move it out of the depot
			leave->track = TrackToTrackBits(GetRailDepotTrack(leave->tile));
			for (int i = 0; i >= d; i--) TrainController(leave, nullptr); // maybe move it, and maybe let another wagon leave
		}
	} else {
		dep = nullptr; // no vehicle in a depot, so no vehicle leaving a depot
	}

	Train *base = v;
	Train *first = base; // first vehicle to move
	Train *last = v->Last(); // last vehicle to move
	uint length = CountVehiclesInChain(v);

	/* We have to make sure all wagons that leave a depot because of train reversing are moved correctly
	 * they have already correct spacing, so we have to make sure they are moved how they should */
	bool nomove = (dep == nullptr); // If there is no vehicle leaving a depot, limit the number of wagons moved immediately.

	while (length > 2) {
		/* we reached vehicle (originally) in front of a depot, stop now
		 * (we would move wagons that are already moved with new wagon length). */
		if (base == dep) break;

		/* the last wagon was that one leaving a depot, so do not move it anymore */
		if (last == dep) nomove = true;

		last = last->Previous();
		first = first->Next();

		int differential = last->CalcNextVehicleOffset() - base->CalcNextVehicleOffset();

		/* do not update images now */
		for (int i = 0; i < differential; i++) TrainController(first, (nomove ? last->Next() : nullptr));

		base = first; // == base->Next()
		length -= 2;
	}
}

static bool IsWholeTrainInsideDepot(const Train *v)
{
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return false;
	}
	return true;
}

/**
 * Turn a train around.
 * @param v %Train to turn around.
 */
void ReverseTrainDirection(Train *v)
{
	if (IsRailDepotTile(v->tile)) {
		if (IsWholeTrainInsideDepot(v)) return;
		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	}

	if (_local_company == v->owner && (v->current_order.IsType(OT_LOADING_ADVANCE) || HasBit(v->flags, VRF_BEYOND_PLATFORM_END))) {
		SetDParam(0, v->index);
		SetDParam(1, v->current_order.GetDestination());
		AddNewsItem(STR_VEHICLE_LOAD_THROUGH_ABORTED_INSUFFICIENT_TRACK, NT_ADVICE, NF_INCOLOUR | NF_SMALL | NF_VEHICLE_PARAM0,
				NR_VEHICLE, v->index,
				NR_STATION, v->current_order.GetDestination());
	}
	if (v->current_order.IsType(OT_LOADING_ADVANCE)) {
		v->LeaveStation();

		/* Only advance to next order if we are loading at the current one */
		const Order *order = v->GetOrder(v->cur_implicit_order_index);
		if (order != nullptr && order->IsType(OT_GOTO_STATION) && order->GetDestination() == v->last_station_visited) {
			v->IncrementImplicitOrderIndex();
		}
	} else if (v->current_order.IsAnyLoadingType()) {
		const Vehicle *last = v;
		while (last->Next() != nullptr) last = last->Next();

		/* not a station || different station --> leave the station */
		if (!IsTileType(last->tile, MP_STATION) || !IsTileType(v->tile, MP_STATION) ||
				GetStationIndex(last->tile) != GetStationIndex(v->tile) ||
				HasBit(v->flags, VRF_BEYOND_PLATFORM_END)) {
			v->LeaveStation();
		}
	}

	for (Train *u = v; u != nullptr; u = u->Next()) {
		ClrBit(u->flags, VRF_BEYOND_PLATFORM_END);
		ClrBit(u->flags, VRF_NOT_YET_IN_PLATFORM);
	}

	v->reverse_distance = 0;

	bool no_near_end_unreserve = false;
	bool no_far_end_unreserve = false;
	{
		/* Temporarily clear and restore reservations to bidi tunnel/bridge entrances when reversing train inside,
		 * to avoid outgoing and incoming reservations becoming merged */
		auto find_train_reservations = [&v](TileIndex tile, bool &found_reservation) {
			TrackBits reserved = GetAcrossTunnelBridgeReservationTrackBits(tile);
			Track track;
			while ((track = RemoveFirstTrack(&reserved)) != INVALID_TRACK) {
				Train *res_train = GetTrainForReservation(tile, track);
				if (res_train != nullptr && res_train != v) {
					found_reservation = true;
				}
			}
		};
		if (IsTunnelBridgeWithSignalSimulation(v->tile) && IsTunnelBridgeSignalSimulationBidirectional(v->tile)) {
			find_train_reservations(v->tile, no_near_end_unreserve);
			find_train_reservations(GetOtherTunnelBridgeEnd(v->tile), no_far_end_unreserve);
		}
	}

	/* Clear path reservation in front if train is not stuck. */
	if (!HasBit(v->flags, VRF_TRAIN_STUCK) && !no_near_end_unreserve && !no_far_end_unreserve) {
		FreeTrainTrackReservation(v);
	} else {
		v->lookahead.reset();
	}

	if ((v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(v->tile)) {
		/* Clear exit tile reservation if train was on approach to exit and had reserved it */
		Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(v->tile));
		DiagDirection axial_dir = DirToDiagDirAlongAxis(v->direction, axis);
		TileIndex next_tile = TileVirtXY(v->x_pos, v->y_pos) + TileOffsByDiagDir(axial_dir);
		if ((!no_near_end_unreserve && next_tile == v->tile) || (!no_far_end_unreserve && next_tile == GetOtherTunnelBridgeEnd(v->tile))) {
			Trackdir exit_td = GetTunnelBridgeExitTrackdir(next_tile);
			CFollowTrackRail ft(GetTileOwner(next_tile), GetRailTypeInfo(v->railtype)->all_compatible_railtypes);
			if (ft.Follow(next_tile, exit_td)) {
				TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));
				if (reserved == TRACKDIR_BIT_NONE) {
					UnreserveBridgeTunnelTile(next_tile);
					MarkTileDirtyByTile(next_tile, VMDF_NOT_MAP_MODE);
				}
			} else {
				UnreserveBridgeTunnelTile(next_tile);
				MarkTileDirtyByTile(next_tile, VMDF_NOT_MAP_MODE);
			}
		}
	}

	/* Check if we were approaching a rail/road-crossing */
	TileIndex crossing = TrainApproachingCrossingTile(v);

	/* count number of vehicles */
	int r = CountVehiclesInChain(v) - 1;  // number of vehicles - 1

	AdvanceWagonsBeforeSwap(v);

	/* swap start<>end, start+1<>end-1, ... */
	int l = 0;
	do {
		ReverseTrainSwapVeh(v, l++, r--);
	} while (l <= r);

	AdvanceWagonsAfterSwap(v);

	ClrBit(v->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);

	if (IsRailDepotTile(v->tile)) {
		InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);
	}

	ToggleBit(v->flags, VRF_TOGGLE_REVERSE);

	ClrBit(v->flags, VRF_REVERSING);

	/* recalculate cached data */
	v->ConsistChanged(CCF_TRACK);

	/* update all images */
	for (Train *u = v; u != nullptr; u = u->Next()) u->UpdateViewport(false, false);

	/* update crossing we were approaching */
	if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

	/* maybe we are approaching crossing now, after reversal */
	crossing = TrainApproachingCrossingTile(v);
	if (crossing != INVALID_TILE) MaybeBarCrossingWithSound(crossing);

	if (HasBit(v->flags, VRF_PENDING_SPEED_RESTRICTION)) {
		for (auto it = _pending_speed_restriction_change_map.lower_bound(v->index); it != _pending_speed_restriction_change_map.end() && it->first == v->index;) {
			it->second.distance = (v->gcache.cached_total_length + (HasBit(it->second.flags, PSRCF_DIAGONAL) ? 8 : 4)) - it->second.distance;
			if (it->second.distance == 0) {
				v->speed_restriction = it->second.prev_speed;
				it = _pending_speed_restriction_change_map.erase(it);
			} else {
				std::swap(it->second.prev_speed, it->second.new_speed);
				++it;
			}
		}
	}

	/* If we are inside a depot after reversing, don't bother with path reserving. */
	if (v->track == TRACK_BIT_DEPOT) {
		/* Can't be stuck here as inside a depot is always a safe tile. */
		if (HasBit(v->flags, VRF_TRAIN_STUCK)) SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		return;
	}

	auto update_check_tunnel_bridge_signal_counters = [](Train *t) {
		if (!(t->track & TRACK_BIT_WORMHOLE)) {
			/* Not in wormhole, clear counters */
			t->wait_counter = 0;
			t->tunnel_bridge_signal_num = 0;
			return;
		}

		DiagDirection tb_dir = GetTunnelBridgeDirection(t->tile);
		if (DirToDiagDirAlongAxis(t->direction, DiagDirToAxis(tb_dir)) == tb_dir) {
			/* Now going in correct direction, fix counters */
			const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(t->tile);
			const uint delta = DistanceManhattan(t->tile, TileVirtXY(t->x_pos, t->y_pos));
			t->wait_counter = TILE_SIZE * ((simulated_wormhole_signals - 1) - (delta % simulated_wormhole_signals));
			t->tunnel_bridge_signal_num = delta / simulated_wormhole_signals;
		} else {
			/* Now going in wrong direction, all bets are off.
			 * Prevent setting the wrong signals by making wait_counter a non-integer multiple of TILE_SIZE.
			 * Use a huge value so that the train will reverse again if there is another vehicle coming the other way.
			 */
			t->wait_counter = static_cast<uint16_t>(-((int)TILE_SIZE / 2));
			t->tunnel_bridge_signal_num = 0;
		}
	};

	Train *last = v->Last();
	if (IsTunnelBridgeWithSignalSimulation(last->tile) && IsTunnelBridgeSignalSimulationEntrance(last->tile)) {
		update_check_tunnel_bridge_signal_counters(last);
	}

	/* We are inside tunnel/bridge with signals, reversing will close the entrance. */
	if (IsTunnelBridgeWithSignalSimulation(v->tile) && IsTunnelBridgeSignalSimulationEntrance(v->tile)) {
		/* Flip signal on tunnel entrance tile red. */
		SetTunnelBridgeEntranceSignalState(v->tile, SIGNAL_STATE_RED);
		if (_extra_aspects > 0) {
			PropagateAspectChange(v->tile, GetTunnelBridgeEntranceTrackdir(v->tile), 0);
		}
		MarkTileDirtyByTile(v->tile, VMDF_NOT_MAP_MODE);
		update_check_tunnel_bridge_signal_counters(v);
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		return;
	}

	/* VehicleExitDir does not always produce the desired dir for depots and
	 * tunnels/bridges that is needed for UpdateSignalsOnSegment. */
	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	if (IsRailDepotTile(v->tile) || (IsTileType(v->tile, MP_TUNNELBRIDGE) && (v->track & TRACK_BIT_WORMHOLE || dir == GetTunnelBridgeDirection(v->tile)))) dir = INVALID_DIAGDIR;

	if (UpdateSignalsOnSegment(v->tile, dir, v->owner) == SIGSEG_PBS || _settings_game.pf.reserve_paths) {
		/* If we are currently on a tile with conventional signals, we can't treat the
		 * current tile as a safe tile or we would enter a PBS block without a reservation. */
		bool first_tile_okay = !(IsTileType(v->tile, MP_RAILWAY) &&
			HasSignalOnTrackdir(v->tile, v->GetVehicleTrackdir()) &&
			!IsPbsSignal(GetSignalType(v->tile, FindFirstTrack(v->track))));

		/* If we are on a depot tile facing outwards, do not treat the current tile as safe. */
		if (IsRailDepotTile(v->tile) && TrackdirToExitdir(v->GetVehicleTrackdir()) == GetRailDepotDirection(v->tile)) first_tile_okay = false;

		if (IsRailStationTile(v->tile)) SetRailStationPlatformReservation(v->tile, TrackdirToExitdir(v->GetVehicleTrackdir()), true);
		if (TryPathReserve(v, false, first_tile_okay)) {
			/* Do a look-ahead now in case our current tile was already a safe tile. */
			CheckNextTrainTile(v);
		} else if (v->current_order.GetType() != OT_LOADING) {
			/* Do not wait for a way out when we're still loading */
			MarkTrainAsStuck(v);
		}
	} else if (HasBit(v->flags, VRF_TRAIN_STUCK)) {
		/* A train not inside a PBS block can't be stuck. */
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		v->wait_counter = 0;
	}
}

/**
 * Reverse train.
 * @param tile unused
 * @param flags type of operation
 * @param p1 train to reverse
 * @param p2 if true, reverse a unit in a train (needs to be in a depot)
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdReverseTrainDirection(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Train *v = Train::GetIfValid(p1);
	if (v == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (p2 != 0) {
		/* turn a single unit around */

		if (v->IsMultiheaded() || HasBit(EngInfo(v->engine_type)->callback_mask, CBM_VEHICLE_ARTIC_ENGINE)) {
			return_cmd_error(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE_MULTIPLE_UNITS);
		}

		Train *front = v->First();
		/* make sure the vehicle is stopped in the depot */
		if (!front->IsStoppedInDepot() && !front->IsVirtual()) {
			return_cmd_error(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
		}

		if (flags & DC_EXEC) {
			ToggleBit(v->flags, VRF_REVERSE_DIRECTION);

			front->ConsistChanged(CCF_ARRANGE);
			SetWindowDirty(WC_VEHICLE_DEPOT, front->tile);
			SetWindowDirty(WC_VEHICLE_DETAILS, front->index);
			SetWindowDirty(WC_VEHICLE_VIEW, front->index);
			DirtyVehicleListWindowForVehicle(front);
		}
	} else {
		/* turn the whole train around */
		if (!v->IsPrimaryVehicle()) return CMD_ERROR;
		if ((v->vehstatus & VS_CRASHED) || HasBit(v->flags, VRF_BREAKDOWN_STOPPED)) return CMD_ERROR;

		if (flags & DC_EXEC) {
			/* Properly leave the station if we are loading and won't be loading anymore */
			if (v->current_order.IsAnyLoadingType()) {
				const Vehicle *last = v;
				while (last->Next() != nullptr) last = last->Next();

				/* not a station || different station --> leave the station */
				if (!IsTileType(last->tile, MP_STATION) || !IsTileType(v->tile, MP_STATION) ||
						GetStationIndex(last->tile) != GetStationIndex(v->tile) ||
						HasBit(v->flags, VRF_BEYOND_PLATFORM_END) ||
						v->current_order.IsType(OT_LOADING_ADVANCE)) {
					v->LeaveStation();
				}
			}

			/* We cancel any 'skip signal at dangers' here */
			v->force_proceed = TFP_NONE;
			SetWindowDirty(WC_VEHICLE_VIEW, v->index);

			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && v->cur_speed != 0) {
				ToggleBit(v->flags, VRF_REVERSING);
			} else {
				v->cur_speed = 0;
				v->SetLastSpeed();
				HideFillingPercent(&v->fill_percent_te_id);
				ReverseTrainDirection(v);
			}

			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();
		}
	}
	return CommandCost();
}

/**
 * Force a train through a red signal
 * @param tile unused
 * @param flags type of operation
 * @param p1 train to ignore the red signal
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdForceTrainProceed(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Train *t = Train::GetIfValid(p1);
	if (t == nullptr) return CMD_ERROR;

	if (!t->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(t);
	if (ret.Failed()) return ret;


	if (flags & DC_EXEC) {
		/* If we are forced to proceed, cancel that order.
		 * If we are marked stuck we would want to force the train
		 * to proceed to the next signal. In the other cases we
		 * would like to pass the signal at danger and run till the
		 * next signal we encounter. */
		t->force_proceed = t->force_proceed == TFP_SIGNAL ? TFP_NONE : HasBit(t->flags, VRF_TRAIN_STUCK) || t->IsChainInDepot() ? TFP_STUCK : TFP_SIGNAL;
		SetWindowDirty(WC_VEHICLE_VIEW, t->index);

		/* Unbunching data is no longer valid. */
		t->ResetDepotUnbunching();
	}

	return CommandCost();
}

/**
 * Try to find a depot nearby.
 * @param v %Train that wants a depot.
 * @param max_distance Maximal search distance.
 * @return Information where the closest train depot is located.
 * @pre The given vehicle must not be crashed!
 */
static FindDepotData FindClosestTrainDepot(Train *v, int max_distance)
{
	assert(!(v->vehstatus & VS_CRASHED));

	if (IsRailDepotTile(v->tile)) return FindDepotData(v->tile, 0);

	if (v->lookahead != nullptr && !ValidateLookAhead(v)) return FindDepotData();

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FTRF_OKAY_UNUSED);
	if (IsRailDepotTile(origin.tile)) return FindDepotData(origin.tile, 0);

	return YapfTrainFindNearestDepot(v, max_distance);
}

ClosestDepot Train::FindClosestDepot()
{
	FindDepotData tfdd = FindClosestTrainDepot(this, 0);
	if (tfdd.best_length == UINT_MAX) return ClosestDepot();

	return ClosestDepot(tfdd.tile, GetDepotIndex(tfdd.tile), tfdd.reverse);
}

/** Play a sound for a train leaving the station. */
void Train::PlayLeaveStationSound(bool force) const
{
	static const SoundFx sfx[] = {
		SND_04_DEPARTURE_STEAM,
		SND_0A_DEPARTURE_TRAIN,
		SND_0A_DEPARTURE_TRAIN,
		SND_47_DEPARTURE_MONORAIL,
		SND_41_DEPARTURE_MAGLEV
	};

	if (PlayVehicleSound(this, VSE_START, force)) return;

	SndPlayVehicleFx(sfx[RailVehInfo(this->engine_type)->engclass], this);
}

/**
 * Check if the train is on the last reserved tile and try to extend the path then.
 * @param v %Train that needs its path extended.
 */
static void CheckNextTrainTile(Train *v)
{
	/* Don't do any look-ahead if path_backoff_interval is 255. */
	if (_settings_game.pf.path_backoff_interval == 255) return;

	/* Exit if we are inside a depot. */
	if (v->track == TRACK_BIT_DEPOT) return;

	/* Exit if we are currently in a waiting order */
	if (v->current_order.IsType(OT_WAITING)) return;

	/* Exit if we are on a station tile and are going to stop. */
	if (HasStationTileRail(v->tile) && v->current_order.ShouldStopAtStation(v, GetStationIndex(v->tile), IsRailWaypoint(v->tile))) return;

	switch (v->current_order.GetType()) {
		/* Exit if we reached our destination depot. */
		case OT_GOTO_DEPOT:
			if (v->tile == v->dest_tile) return;
			break;

		case OT_GOTO_WAYPOINT:
			/* If we reached our waypoint, make sure we see that. */
			if (IsRailWaypointTile(v->tile) && GetStationIndex(v->tile) == v->current_order.GetDestination()) ProcessOrders(v);
			break;

		case OT_NOTHING:
		case OT_LEAVESTATION:
		case OT_LOADING:
			/* Exit if the current order doesn't have a destination, but the train has orders. */
			if (v->GetNumOrders() > 0) return;
			break;

		default:
			break;
	}

	Trackdir td = v->GetVehicleTrackdir();

	/* On a tile with a red non-pbs signal, don't look ahead. */
	if (IsTileType(v->tile, MP_RAILWAY) && HasSignalOnTrackdir(v->tile, td) &&
			!IsPbsSignal(GetSignalType(v->tile, TrackdirToTrack(td))) &&
			GetSignalStateByTrackdir(v->tile, td) == SIGNAL_STATE_RED) return;

	CFollowTrackRail ft(v);
	if (!ft.Follow(v->tile, td)) return;

	if (!HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits))) {
		/* Next tile is not reserved. */
		if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
			Trackdir td = FindFirstTrackdir(ft.new_td_bits);
			if (HasPbsSignalOnTrackdir(ft.new_tile, td) && !IsNoEntrySignal(ft.new_tile, TrackdirToTrack(td))) {
				/* If the next tile is a PBS signal, try to make a reservation. */
				TrackBits tracks = TrackdirBitsToTrackBits(ft.new_td_bits);
				if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td)) {
					tracks &= ~TrackCrossesTracks(TrackdirToTrack(ft.old_td));
				}
				ChooseTrainTrack(v, ft.new_tile, ft.exitdir, tracks, CTTF_NONE);
			}
		}
	} else if (v->lookahead != nullptr && v->lookahead->reservation_end_tile == ft.new_tile && IsTileType(ft.new_tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationEntrance(ft.new_tile) &&
			v->lookahead->reservation_end_trackdir == FindFirstTrackdir(ft.new_td_bits)) {
		/* If the lookahead ends at the next tile which is a signalled tunnel/bridge entrance, try to make a reservation. */
		TryLongReserveChooseTrainTrackFromReservationEnd(v);
	}
}

/**
 * Will the train stay in the depot the next tick?
 * @param v %Train to check.
 * @return True if it stays in the depot, false otherwise.
 */
static bool CheckTrainStayInDepot(Train *v)
{
	/* bail out if not all wagons are in the same depot or not in a depot at all */
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return false;
	}

	/* if the train got no power, then keep it in the depot */
	if (v->gcache.cached_power == 0) {
		v->vehstatus |= VS_STOPPED;
		SetWindowDirty(WC_VEHICLE_DEPOT, v->tile);
		return true;
	}

	if (v->current_order.IsWaitTimetabled()) {
		v->HandleWaiting(false, true);
	}
	if (v->current_order.IsType(OT_WAITING)) {
		return true;
	}

	/* Check if we should wait here for unbunching. */
	if (v->IsWaitingForUnbunching()) return true;

	if (v->reverse_distance > 0) {
		v->reverse_distance--;
		if (v->reverse_distance == 0) SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		return true;
	}

	SigSegState seg_state;
	bool exit_blocked = false;

	if (v->force_proceed == TFP_NONE) {
		/* force proceed was not pressed */
		if (++v->wait_counter < 37) {
			return true;
		}

		v->wait_counter = 0;

		seg_state = _settings_game.pf.reserve_paths ? SIGSEG_PBS : UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
		if (seg_state == SIGSEG_FULL || HasDepotReservation(v->tile)) {
			/* Full and no PBS signal in block or depot reserved, can't exit. */
			exit_blocked = true;
		}
	} else {
		seg_state = _settings_game.pf.reserve_paths ? SIGSEG_PBS : UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
	}

	/* We are leaving a depot, but have to go to the exact same one; re-enter. */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && v->tile == v->dest_tile) {
		if (exit_blocked) return true;
		/* Service when depot has no reservation. */
		if (!HasDepotReservation(v->tile)) VehicleEnterDepot(v);
		return true;
	}

	if (_settings_game.vehicle.drive_through_train_depot) {
		const TileIndex depot_tile = v->tile;
		const DiagDirection depot_dir = GetRailDepotDirection(depot_tile);
		const DiagDirection behind_depot_dir = ReverseDiagDir(depot_dir);
		const int depot_z = GetTileMaxZ(depot_tile);
		const TileIndexDiffC tile_diff = TileIndexDiffCByDiagDir(behind_depot_dir);

		TileIndex behind_depot_tile = depot_tile;
		uint skipped = 0;

		while (true) {
			TileIndex tile = AddTileIndexDiffCWrap(behind_depot_tile, tile_diff);
			if (tile == INVALID_TILE) break;
			if (!IsRailDepotTile(tile)) break;
			DiagDirection dir = GetRailDepotDirection(tile);
			if (dir != depot_dir && dir != behind_depot_dir) break;
			if (!HasBit(v->compatible_railtypes, GetRailType(tile))) break;
			if (GetTileMaxZ(tile) != depot_z) break;
			behind_depot_tile = tile;
			skipped++;
		}

		if (skipped > 0 && GetRailDepotDirection(behind_depot_tile) == behind_depot_dir &&
				YapfTrainCheckDepotReverse(v, depot_tile, behind_depot_tile)) {
			Direction direction = DiagDirToDir(behind_depot_dir);
			int x = TileX(behind_depot_tile) * TILE_SIZE | _vehicle_initial_x_fract[behind_depot_dir];
			int y = TileY(behind_depot_tile) * TILE_SIZE | _vehicle_initial_y_fract[behind_depot_dir];
			if (v->gcache.cached_total_length < skipped * TILE_SIZE) {
				int delta = (skipped * TILE_SIZE) - v->gcache.cached_total_length;
				int speed = std::max(1, v->GetCurrentMaxSpeed());
				v->reverse_distance = (1 + (((192 * 3 / 2) * delta) / speed));
				SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
			}

			for (Train *u = v; u != nullptr; u = u->Next()) {
				u->tile = behind_depot_tile;
				u->direction = direction;
				u->x_pos = x;
				u->y_pos = y;
				u->UpdatePosition();
				u->Vehicle::UpdateViewport(false);
			}

			InvalidateWindowData(WC_VEHICLE_DEPOT, depot_tile);
			InvalidateWindowData(WC_VEHICLE_DEPOT, behind_depot_tile);
			return true;
		}
	}

	if (exit_blocked) return true;

	/* Only leave when we can reserve a path to our destination. */
	if (seg_state == SIGSEG_PBS && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
		/* No path and no force proceed. */
		MarkTrainAsStuck(v);
		return true;
	}

	SetDepotReservation(v->tile, true);
	if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(v->tile, VMDF_NOT_MAP_MODE);

	VehicleServiceInDepot(v);
	v->LeaveUnbunchingDepot();
	DirtyVehicleListWindowForVehicle(v);
	v->PlayLeaveStationSound();

	v->track = TRACK_BIT_X;
	if (v->direction & 2) v->track = TRACK_BIT_Y;

	v->vehstatus &= ~VS_HIDDEN;
	v->UpdateIsDrawn();
	v->cur_speed = 0;

	v->UpdateViewport(true, true);
	v->UpdatePosition();
	UpdateSignalsOnSegment(v->tile, INVALID_DIAGDIR, v->owner);
	v->UpdateAcceleration();
	InvalidateWindowData(WC_VEHICLE_DEPOT, v->tile);

	return false;
}

static int GetAndClearLastBridgeEntranceSetSignalIndex(TileIndex bridge_entrance)
{
	uint16_t m = _m[bridge_entrance].m2;
	if (m & BRIDGE_M2_SIGNAL_STATE_EXT_FLAG) {
		auto it = _long_bridge_signal_sim_map.find(bridge_entrance);
		if (it != _long_bridge_signal_sim_map.end()) {
			LongBridgeSignalStorage &lbss = it->second;
			uint slot = (uint)lbss.signal_red_bits.size();
			while (slot > 0) {
				slot--;
				uint64_t &slot_bits = lbss.signal_red_bits[slot];
				if (slot_bits) {
					uint8_t i = FindLastBit(slot_bits);
					ClrBit(slot_bits, i);
					return 1 + BRIDGE_M2_SIGNAL_STATE_COUNT + (64 * slot) + i;
				}
			}
		}
	}
	uint16_t m_masked = GB(m & (~BRIDGE_M2_SIGNAL_STATE_EXT_FLAG), BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE);
	if (m_masked) {
		uint8_t i = FindLastBit(m_masked);
		ClrBit(_m[bridge_entrance].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET + i);
		return 1 + i;
	}

	return 0;
}

static void UpdateTunnelBridgeEntranceSignalAspect(TileIndex tile)
{
	Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(tile);
	uint8_t aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
	uint8_t old_aspect = GetTunnelBridgeEntranceSignalAspect(tile);
	if (aspect != old_aspect) {
		SetTunnelBridgeEntranceSignalAspect(tile, aspect);
		MarkTunnelBridgeSignalDirty(tile, false);
		PropagateAspectChange(tile, trackdir, aspect);
	}
}

static void SetTunnelBridgeEntranceSignalGreen(TileIndex tile)
{
	if (GetTunnelBridgeEntranceSignalState(tile) == SIGNAL_STATE_RED) {
		SetTunnelBridgeEntranceSignalState(tile, SIGNAL_STATE_GREEN);
		MarkTunnelBridgeSignalDirty(tile, false);
		if (_extra_aspects > 0) {
			SetTunnelBridgeEntranceSignalAspect(tile, 0);
			UpdateAspectDeferred(tile, GetTunnelBridgeEntranceTrackdir(tile));
		}
	} else if (_extra_aspects > 0) {
		UpdateTunnelBridgeEntranceSignalAspect(tile);
	}
}

static void UpdateEntranceAspectFromMiddleSignalChange(TileIndex entrance, int signal_number)
{
	if (signal_number < _extra_aspects && GetTunnelBridgeEntranceSignalState(entrance) == SIGNAL_STATE_GREEN) {
		UpdateTunnelBridgeEntranceSignalAspect(entrance);
	}
}

static void UpdateAspectFromBridgeMiddleSignalChange(TileIndex entrance, TileIndexDiff diff, int signal_number)
{
	UpdateEntranceAspectFromMiddleSignalChange(entrance, signal_number);
	if (signal_number > 0) {
		for (int i = std::max<int>(0, signal_number - _extra_aspects); i < signal_number; i++) {
			MarkSingleBridgeSignalDirty(entrance + (diff * (i + 1)), entrance);
		}
	}
}

static void HandleLastTunnelBridgeSignals(TileIndex tile, TileIndex end, DiagDirection dir, bool free)
{
	if (IsBridge(end) && _m[end].m2 != 0 && IsTunnelBridgeSignalSimulationEntrance(end)) {
		/* Clearing last bridge signal. */
		int signal_offset = GetAndClearLastBridgeEntranceSetSignalIndex(end);
		if (signal_offset) {
			TileIndexDiff diff = TileOffsByDiagDir(dir) * GetTunnelBridgeSignalSimulationSpacing(tile);
			TileIndex last_signal_tile = end + (diff * signal_offset);
			MarkSingleBridgeSignalDirty(last_signal_tile, end);
			if (_extra_aspects > 0) UpdateAspectFromBridgeMiddleSignalChange(end, diff, signal_offset - 1);
		}
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
	if (free) {
	/* Open up the wormhole and clear m2. */
		if (IsBridge(end)) {
			bool redraw = false;
			if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
				redraw |= SetAllBridgeEntranceSimulatedSignalsGreen(tile);
			}
			if (IsTunnelBridgeSignalSimulationEntrance(end)) {
				redraw |= SetAllBridgeEntranceSimulatedSignalsGreen(end);
			}
			if (redraw) MarkBridgeDirty(tile, end, GetTunnelBridgeDirection(tile), GetBridgeHeight(tile), VMDF_NOT_MAP_MODE);
		}

		if (IsTunnelBridgeSignalSimulationEntrance(end)) SetTunnelBridgeEntranceSignalGreen(end);
		if (IsTunnelBridgeSignalSimulationEntrance(tile)) SetTunnelBridgeEntranceSignalGreen(tile);
	} else if (IsTunnel(end) && _extra_aspects > 0 && IsTunnelBridgeSignalSimulationEntrance(end)) {
		uint signal_count = GetTunnelBridgeLength(tile, end) / GetTunnelBridgeSignalSimulationSpacing(end);
		if (signal_count > 0) UpdateEntranceAspectFromMiddleSignalChange(end, signal_count - 1);
	}
}

static void UnreserveBridgeTunnelTile(TileIndex tile)
{
	UnreserveAcrossRailTunnelBridge(tile);
	if (IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile)) {
		if (IsTunnelBridgePBS(tile)) {
			SetTunnelBridgeExitSignalState(tile, SIGNAL_STATE_RED);
			if (_extra_aspects > 0) PropagateAspectChange(tile, GetTunnelBridgeExitTrackdir(tile), 0);
		} else {
			UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, GetTileOwner(tile));
		}
	}
}

/**
 * Clear the reservation of \a tile that was just left by a wagon on \a track_dir.
 * @param v %Train owning the reservation.
 * @param tile Tile with reservation to clear.
 * @param track_dir Track direction to clear.
 * @param tunbridge_clear_unsignaled_other_end Whether to clear the far end of unsignalled tunnels/bridges.
 */
static void ClearPathReservation(const Train *v, TileIndex tile, Trackdir track_dir, bool tunbridge_clear_unsignaled_other_end = false)
{
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		if (IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(track_dir))) {
			UnreserveBridgeTunnelTile(tile);

			if (IsTunnelBridgeWithSignalSimulation(tile)) {
				/* Are we just leaving a tunnel/bridge? */
				if (TrackdirExitsTunnelBridge(tile, track_dir)) {
					TileIndex end = GetOtherTunnelBridgeEnd(tile);
					bool free = TunnelBridgeIsFree(tile, end, v, TBIFM_ACROSS_ONLY).Succeeded();
					HandleLastTunnelBridgeSignals(tile, end, ReverseDiagDir(GetTunnelBridgeDirection(tile)), free);
				}
			} else if (tunbridge_clear_unsignaled_other_end) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				UnreserveAcrossRailTunnelBridge(end);
				if (_settings_client.gui.show_track_reservation) {
					MarkTileDirtyByTile(end, VMDF_NOT_MAP_MODE);
				}
			}

			if (_settings_client.gui.show_track_reservation || IsTunnelBridgeSignalSimulationBidirectional(tile)) {
				MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
			}
		} else {
			UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
			if (_settings_client.gui.show_track_reservation) {
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
		}
	} else if (IsRailStationTile(tile)) {
		DiagDirection dir = TrackdirToExitdir(track_dir);
		TileIndex new_tile = TileAddByDiagDir(tile, dir);
		/* If the new tile is not a further tile of the same station, we
		 * clear the reservation for the whole platform. */
		if (!IsCompatibleTrainStationTile(new_tile, tile)) {
			SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);
		}
	} else {
		/* Any other tile */
		UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
	}
}

/**
 * Free the reserved path in front of a vehicle.
 * @param v %Train owning the reserved path.
 * @param origin %Tile to start clearing (if #INVALID_TILE, use the current tile of \a v).
 * @param orig_td Track direction (if #INVALID_TRACKDIR, use the track direction of \a v).
 */
void FreeTrainTrackReservation(Train *v, TileIndex origin, Trackdir orig_td)
{
	assert(v->IsFrontEngine());

	if (origin == INVALID_TILE) v->lookahead.reset();

	bool free_origin_tunnel_bridge = false;

	if (origin == INVALID_TILE && (v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(v->tile)) {
		TileIndex other_end = GetOtherTunnelBridgeEnd(v->tile);
		Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(v->tile));
		DiagDirection axial_dir = DirToDiagDirAlongAxis(v->direction, axis);
		TileIndex exit = v->tile;
		TileIndex entrance = other_end;
		if (axial_dir == GetTunnelBridgeDirection(v->tile)) std::swap(exit, entrance);
		if (GetTrainClosestToTunnelBridgeEnd(exit, entrance) == v) {
			origin = exit;
			TrackBits tracks = GetAcrossTunnelBridgeTrackBits(origin);
			orig_td = ReverseTrackdir(TrackExitdirToTrackdir(FindFirstTrack(tracks), GetTunnelBridgeDirection(origin)));
			free_origin_tunnel_bridge = true;
		} else {
			return;
		}
	}

	TileIndex tile = origin != INVALID_TILE ? origin : v->tile;
	Trackdir  td = orig_td != INVALID_TRACKDIR ? orig_td : v->GetVehicleTrackdir();
	bool      free_tile = tile != v->tile || !(IsRailStationTile(v->tile) || IsTileType(v->tile, MP_TUNNELBRIDGE));
	StationID station_id = IsRailStationTile(v->tile) ? GetStationIndex(v->tile) : INVALID_STATION;

	/* Can't be holding a reservation if we enter a depot. */
	if (IsRailDepotTile(tile) && TrackdirToExitdir(td) != GetRailDepotDirection(tile)) return;
	if (v->track == TRACK_BIT_DEPOT) {
		/* Front engine is in a depot. We enter if some part is not in the depot. */
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return;
		}
	}
	/* Don't free reservation if it's not ours. */
	if (TracksOverlap(GetReservedTrackbits(tile) | TrackToTrackBits(TrackdirToTrack(td)))) return;

	/* Do not attempt to unreserve out of a signalled tunnel/bridge entrance, as this would unreserve the reservations of another train coming in */
	if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirExitsTunnelBridge(tile, td) && IsTunnelBridgeSignalSimulationEntranceOnly(tile)) return;

	if (free_origin_tunnel_bridge) {
		if (!HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(td)))) return;
		UnreserveRailTrack(tile, TrackdirToTrack(td));
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && !IsTunnelBridgePBS(tile)) {
			UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, GetTileOwner(tile));
		}
	}

	CFollowTrackRail ft(v, GetRailTypeInfo(v->railtype)->all_compatible_railtypes);
	while (ft.Follow(tile, td)) {
		tile = ft.new_tile;
		TrackdirBits bits = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
		td = RemoveFirstTrackdir(&bits);
		dbg_assert(bits == TRACKDIR_BIT_NONE);

		if (!IsValidTrackdir(td)) break;

		bool update_signal = false;

		if (IsTileType(tile, MP_RAILWAY)) {
			if (HasSignalOnTrackdir(tile, td) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)))) {
				/* Conventional signal along trackdir: remove reservation and stop. */
				UnreserveRailTrack(tile, TrackdirToTrack(td));
				break;
			}
			if (HasPbsSignalOnTrackdir(tile, td)) {
				if (GetSignalStateByTrackdir(tile, td) == SIGNAL_STATE_RED || IsNoEntrySignal(tile, TrackdirToTrack(td))) {
					/* Red PBS signal? Can't be our reservation, would be green then. */
					break;
				} else {
					/* Turn the signal back to red. */
					if (GetSignalType(tile, TrackdirToTrack(td)) == SIGTYPE_BLOCK) {
						update_signal = true;
					} else {
						SetSignalStateByTrackdir(tile, td, SIGNAL_STATE_RED);
					}
					MarkSingleSignalDirty(tile, td);
				}
			} else if (HasSignalOnTrackdir(tile, ReverseTrackdir(td)) && IsOnewaySignal(tile, TrackdirToTrack(td))) {
				break;
			}
		} else if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirExitsTunnelBridge(tile, td)) {
			TileIndex end = GetOtherTunnelBridgeEnd(tile);
			bool free = TunnelBridgeIsFree(tile, end, v, TBIFM_ACROSS_ONLY).Succeeded();
			if (!free) break;
		} else if (IsTunnelBridgeWithSignalSimulation(tile) && IsTunnelBridgeSignalSimulationExitOnly(tile) && TrackdirEntersTunnelBridge(tile, td)) {
			break;
		}

		/* Don't free first station/bridge/tunnel if we are on it. */
		if (free_tile || (!(ft.is_station && GetStationIndex(ft.new_tile) == station_id) && !ft.is_tunnel && !ft.is_bridge)) ClearPathReservation(v, tile, td);
		if (update_signal) {
			AddSideToSignalBuffer(tile, TrackdirToExitdir(td), GetTileOwner(tile));
			UpdateSignalsInBuffer();
		}

		free_tile = true;
	}
}

static const uint8_t _initial_tile_subcoord[6][4][3] = {
{{ 15, 8, 1 }, { 0, 0, 0 }, { 0, 8, 5 }, { 0,  0, 0 }},
{{  0, 0, 0 }, { 8, 0, 3 }, { 0, 0, 0 }, { 8, 15, 7 }},
{{  0, 0, 0 }, { 7, 0, 2 }, { 0, 7, 6 }, { 0,  0, 0 }},
{{ 15, 8, 2 }, { 0, 0, 0 }, { 0, 0, 0 }, { 8, 15, 6 }},
{{ 15, 7, 0 }, { 8, 0, 4 }, { 0, 0, 0 }, { 0,  0, 0 }},
{{  0, 0, 0 }, { 0, 0, 0 }, { 0, 8, 4 }, { 7, 15, 0 }},
};

/**
 * Perform pathfinding for a train.
 *
 * @param v The train
 * @param tile The tile the train is about to enter
 * @param enterdir Diagonal direction the train is coming from
 * @param tracks Usable tracks on the new tile
 * @param[out] path_found Whether a path has been found or not.
 * @param do_track_reservation Path reservation is requested
 * @param[out] dest State and destination of the requested path
 * @param[out] final_dest Final tile of the best path found
 * @return The best track the train should follow
 */
static Track DoTrainPathfind(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool do_track_reservation, PBSTileInfo *dest, TileIndex *final_dest)
{
	if (final_dest != nullptr) *final_dest = INVALID_TILE;
	return YapfTrainChooseTrack(v, tile, enterdir, tracks, path_found, do_track_reservation, dest, final_dest);
}

/**
 * Extend a train path as far as possible. Stops on encountering a safe tile,
 * another reservation or a track choice.
 * @param v The train.
 * @param origin The tile from which the reservation have to be extended
 * @param new_tracks [out] Tracks to choose from when encountering a choice
 * @param enterdir [out] The direction from which the choice tile is to be entered
 * @param temporary_slot_state The temporary slot to use (will be activated/deactivated as necessary if it isn't already)
 * @return INVALID_TILE indicates that the reservation failed.
 */
static PBSTileInfo ExtendTrainReservation(const Train *v, const PBSTileInfo &origin, TrackBits *new_tracks, DiagDirection *enterdir, TraceRestrictSlotTemporaryState &temporary_slot_state)
{
	CFollowTrackRail ft(v);

	TileIndex tile = origin.tile;
	Trackdir  cur_td = origin.trackdir;
	while (ft.Follow(tile, cur_td)) {
		if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
			/* Possible signal tile. */
			if (HasOnewaySignalBlockingTrackdir(ft.new_tile, FindFirstTrackdir(ft.new_td_bits))) break;
		}

		if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td)) {
			ft.new_td_bits &= ~TrackdirCrossesTrackdirs(ft.old_td);
			if (ft.new_td_bits == TRACKDIR_BIT_NONE) break;
		}

		/* Station, depot or waypoint are a possible target. */
		bool target_seen = ft.is_station || (IsTileType(ft.new_tile, MP_RAILWAY) && !IsPlainRail(ft.new_tile));
		if (target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) {
			/* Choice found or possible target encountered.
			 * On finding a possible target, we need to stop and let the pathfinder handle the
			 * remaining path. This is because we don't know if this target is in one of our
			 * orders, so we might cause pathfinding to fail later on if we find a choice.
			 * This failure would cause a bogous call to TryReserveSafePath which might reserve
			 * a wrong path not leading to our next destination. */
			if (HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))) break;

			/* If we did skip some tiles, backtrack to the first skipped tile so the pathfinder
			 * actually starts its search at the first unreserved tile. */
			if (ft.tiles_skipped != 0) ft.new_tile -= TileOffsByDiagDir(ft.exitdir) * ft.tiles_skipped;

			/* Choice found, path valid but not okay. Save info about the choice tile as well. */
			if (new_tracks != nullptr) *new_tracks = TrackdirBitsToTrackBits(ft.new_td_bits);
			if (enterdir != nullptr) *enterdir = ft.exitdir;
			return PBSTileInfo(ft.new_tile, ft.old_td, false);
		}

		tile = ft.new_tile;
		cur_td = FindFirstTrackdir(ft.new_td_bits);

		if (IsSafeWaitingPosition(v, tile, cur_td, true, _settings_game.pf.forbid_90_deg)) {
			PBSWaitingPositionRestrictedSignalState restricted_signal_state;
			bool wp_free = IsWaitingPositionFree(v, tile, cur_td, _settings_game.pf.forbid_90_deg, &restricted_signal_state);
			if (!(wp_free && TryReserveRailTrackdir(v, tile, cur_td))) break;
			/* Safe position is all good, path valid and okay. */
			restricted_signal_state.TraceRestrictExecuteResEndSlot(v);
			return PBSTileInfo(tile, cur_td, true);
		}

		if (IsTileType(tile, MP_RAILWAY) && HasSignals(tile) && IsRestrictedSignal(tile) && HasSignalOnTrack(tile, TrackdirToTrack(cur_td))) {
			const bool front_side = HasSignalOnTrackdir(tile, cur_td);

			TraceRestrictProgramActionsUsedFlags au_flags = TRPAUF_SLOT_ACQUIRE;
			if (front_side) {
				/* Passing through a signal from the front side */
				au_flags |= TRPAUF_WAIT_AT_PBS;
			}

			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(cur_td));
			if (prog != nullptr && prog->actions_used_flags & au_flags) {
				TraceRestrictProgramInput input(tile, cur_td, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
				if (prog->actions_used_flags & TRPAUF_SLOT_ACQUIRE) {
					input.permitted_slot_operations = TRPISP_ACQUIRE_TEMP_STATE;

					if (!temporary_slot_state.IsActive()) {
						/* The temporary slot state needs to be be pushed because permission to use it is granted by TRPISP_ACQUIRE_TEMP_STATE */
						temporary_slot_state.PushToChangeStack();
					}
				}

				TraceRestrictProgramResult out;
				prog->Execute(v, input, out);
				if (front_side && (out.flags & TRPRF_WAIT_AT_PBS)) {
					/* Wait at PBS is set, take this as waiting at the start signal, handle as a reservation failure */
					break;
				}
			}
		}

		if (!TryReserveRailTrackdir(v, tile, cur_td)) break;
	}

	if (ft.err == CFollowTrackRail::EC_OWNER || ft.err == CFollowTrackRail::EC_NO_WAY) {
		/* End of line, path valid and okay. */
		return PBSTileInfo(ft.old_tile, ft.old_td, true);
	}

	/* Sorry, can't reserve path, back out. */
	tile = origin.tile;
	cur_td = origin.trackdir;
	TileIndex stopped = ft.old_tile;
	Trackdir  stopped_td = ft.old_td;
	while (tile != stopped || cur_td != stopped_td) {
		if (!ft.Follow(tile, cur_td)) break;

		if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td)) {
			ft.new_td_bits &= ~TrackdirCrossesTrackdirs(ft.old_td);
			dbg_assert(ft.new_td_bits != TRACKDIR_BIT_NONE);
		}
		dbg_assert(KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE);

		tile = ft.new_tile;
		cur_td = FindFirstTrackdir(ft.new_td_bits);

		UnreserveRailTrackdir(tile, cur_td);
	}

	if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);

	/* Path invalid. */
	return PBSTileInfo();
}

/**
 * Try to reserve any path to a safe tile, ignoring the vehicle's destination.
 * Safe tiles are tiles in front of a signal, depots and station tiles at end of line.
 *
 * @param v The vehicle.
 * @param tile The tile the search should start from.
 * @param td The trackdir the search should start from.
 * @param override_railtype Whether all physically compatible railtypes should be followed.
 * @return True if a path to a safe stopping tile could be reserved.
 */
static bool TryReserveSafeTrack(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	return YapfTrainFindNearestSafeTile(v, tile, td, override_railtype);
}

const Order *_choose_train_track_saved_current_order = nullptr;

/** This class will save the current order of a vehicle and restore it on destruction. */
class VehicleOrderSaver {
private:
	Train          *v;
	Order          old_order;
	TileIndex      old_dest_tile;
	StationID      old_last_station_visited;
	VehicleOrderID old_index;
	VehicleOrderID old_impl_index;
	VehicleOrderID old_tt_index;
	bool           suppress_implicit_orders;
	bool           clear_saved_order_ptr;
	bool           restored;

public:
	VehicleOrderSaver(Train *_v) :
		v(_v),
		old_order(_v->current_order),
		old_dest_tile(_v->dest_tile),
		old_last_station_visited(_v->last_station_visited),
		old_index(_v->cur_real_order_index),
		old_impl_index(_v->cur_implicit_order_index),
		old_tt_index(_v->cur_timetable_order_index),
		suppress_implicit_orders(HasBit(_v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS)),
		restored(false)
	{
		if (_choose_train_track_saved_current_order == nullptr) {
			_choose_train_track_saved_current_order = &(this->old_order);
			this->clear_saved_order_ptr = true;
		} else {
			this->clear_saved_order_ptr = false;
		}
	}

	/**
	 * Restore the saved order to the vehicle.
	 */
	void Restore()
	{
		this->v->current_order = std::move(this->old_order);
		this->v->dest_tile = this->old_dest_tile;
		this->v->last_station_visited = this->old_last_station_visited;
		this->v->cur_real_order_index = this->old_index;
		this->v->cur_implicit_order_index = this->old_impl_index;
		this->v->cur_timetable_order_index = this->old_tt_index;
		AssignBit(this->v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS, suppress_implicit_orders);
		if (this->clear_saved_order_ptr) _choose_train_track_saved_current_order = nullptr;
		this->restored = true;
	}

	/**
	 * Restore the saved order to the vehicle, if Restore() has not already been called.
	 */
	~VehicleOrderSaver()
	{
		if (!this->restored) this->Restore();
	}

	/**
	 * Set the current vehicle order to the next order in the order list.
	 * @param skip_first Shall the first (i.e. active) order be skipped?
	 * @return True if a suitable next order could be found.
	 */
	bool SwitchToNextOrder(bool skip_first)
	{
		if (this->v->GetNumOrders() == 0) return false;

		if (skip_first) ++this->v->cur_real_order_index;

		int depth = 0;

		do {
			/* Wrap around. */
			if (this->v->cur_real_order_index >= this->v->GetNumOrders()) this->v->cur_real_order_index = 0;

			Order *order = this->v->GetOrder(this->v->cur_real_order_index);
			dbg_assert(order != nullptr);

			switch (order->GetType()) {
				case OT_GOTO_DEPOT:
					/* Skip service in depot orders when the train doesn't need service. */
					if ((order->GetDepotOrderType() & ODTFB_SERVICE) && !this->v->NeedsServicing()) break;
					[[fallthrough]];
				case OT_GOTO_STATION:
				case OT_GOTO_WAYPOINT:
					this->v->current_order = *order;
					return UpdateOrderDest(this->v, order, 0, true);
				case OT_CONDITIONAL: {
					VehicleOrderID next = ProcessConditionalOrder(order, this->v, PCO_DRY_RUN);
					if (next != INVALID_VEH_ORDER_ID) {
						depth++;
						this->v->cur_real_order_index = next;
						/* Don't increment next, so no break here. */
						continue;
					}
					break;
				}
				default:
					break;
			}
			/* Don't increment inside the while because otherwise conditional
			 * orders can lead to an infinite loop. */
			++this->v->cur_real_order_index;
			depth++;
		} while (this->v->cur_real_order_index != this->old_index && depth < this->v->GetNumOrders());

		return false;
	}

	void AdvanceOrdersFromVehiclePosition(ChooseTrainTrackLookAheadState &state)
	{
		/* If the current tile is the destination of the current order and
		 * a reservation was requested, advance to the next order.
		 * Don't advance on a depot order as depots are always safe end points
		 * for a path and no look-ahead is necessary. This also avoids a
		 * problem with depot orders not part of the order list when the
		 * order list itself is empty. */
		Train *v = this->v;
		if (v->current_order.IsType(OT_LEAVESTATION)) {
			this->SwitchToNextOrder(false);
		} else if (v->current_order.IsAnyLoadingType() || (!v->current_order.IsType(OT_GOTO_DEPOT) && (
				v->current_order.IsBaseStationOrder() ?
				HasStationTileRail(v->tile) && v->current_order.GetDestination() == GetStationIndex(v->tile) :
				v->tile == v->dest_tile))) {
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->current_order.IsBaseStationOrder()) {
				if (v->current_order.ShouldStopAtStation(v, v->current_order.GetDestination(), v->current_order.IsType(OT_GOTO_WAYPOINT))) {
					SetBit(state.flags, CTTLASF_STOP_FOUND);
					v->last_station_visited = v->current_order.GetDestination();
				}
			}
			if (v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) SetBit(state.flags, CTTLASF_STOP_FOUND);
			this->SwitchToNextOrder(true);
		}
	}

	void AdvanceOrdersFromLookahead(ChooseTrainTrackLookAheadState &state)
	{
		TrainReservationLookAhead *lookahead = this->v->lookahead.get();
		if (lookahead == nullptr) return;

		for (size_t i = state.order_items_start; i < lookahead->items.size(); i++) {
			const TrainReservationLookAheadItem &item = lookahead->items[i];
			switch (item.type) {
				case TRLIT_STATION:
					if (this->v->current_order.IsBaseStationOrder()) {
						/* we've already seen this station in the lookahead, advance current order */
						if (this->v->current_order.ShouldStopAtStation(this->v, item.data_id, Waypoint::GetIfValid(item.data_id) != nullptr)) {
							SetBit(state.flags, CTTLASF_STOP_FOUND);
							this->v->last_station_visited = item.data_id;
						} else if (this->v->current_order.IsType(OT_GOTO_WAYPOINT) && this->v->current_order.GetDestination() == item.data_id && (this->v->current_order.GetWaypointFlags() & OWF_REVERSE)) {
							if (!HasBit(state.flags, CTTLASF_REVERSE_FOUND)) {
								SetBit(state.flags, CTTLASF_REVERSE_FOUND);
								state.reverse_dest = item.data_id;
								if (this->v->current_order.IsWaitTimetabled()) {
									this->v->last_station_visited = item.data_id;
									SetBit(state.flags, CTTLASF_STOP_FOUND);
								}
							}
						}
						if (this->v->current_order.GetDestination() == item.data_id) {
							this->SwitchToNextOrder(true);
						}
					}
					break;

				default:
					break;
			}
		}
		state.order_items_start = (uint)lookahead->items.size();
	}
};

static bool IsReservationLookAheadLongEnough(const Train *v, const ChooseTrainTrackLookAheadState &lookahead_state)
{
	if (!v->UsingRealisticBraking() || v->lookahead == nullptr) return true;

	if (v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) return true;

	if (HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND) || HasBit(v->lookahead->flags, TRLF_DEPOT_END)) return true;

	if (v->reverse_distance >= 1) {
		if (v->lookahead->reservation_end_position >= v->lookahead->current_position + v->reverse_distance - 1) return true;
	}

	if (v->lookahead->lookahead_end_position <= v->lookahead->reservation_end_position && _settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
			v->lookahead->reservation_end_position > v->lookahead->current_position + 24) {
		return true;
	}

	TrainDecelerationStats stats(v, v->lookahead->cached_zpos);

	bool found_signal = false;
	int signal_speed = 0;
	int signal_position = 0;
	int signal_z = 0;
	bool signal_limited_lookahead_check = false;

	for (const TrainReservationLookAheadItem &item : v->lookahead->items) {
		if (item.type == TRLIT_REVERSE) {
			if (v->lookahead->reservation_end_position >= item.start + v->gcache.cached_total_length) return true;
		}
		if (item.type == TRLIT_STATION && HasBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND) && lookahead_state.reverse_dest == item.data_id) {
			if (v->lookahead->reservation_end_position >= item.start + v->gcache.cached_total_length) return true;
		}

		if (found_signal) {
			if (item.type == TRLIT_TRACK_SPEED || item.type == TRLIT_SPEED_RESTRICTION || item.type == TRLIT_CURVE_SPEED) {
				if (item.data_id > 0) LimitSpeedFromLookAhead(signal_speed, stats, signal_position, item.start, item.data_id, item.z_pos - stats.z_pos);
			}
		} else if (item.type == TRLIT_SIGNAL && item.start > v->lookahead->current_position + 24) {
			signal_speed = std::min<int>(item.data_id > 0 ? item.data_id : UINT16_MAX, v->vcache.cached_max_speed);
			signal_position = item.start;
			signal_z = item.z_pos;
			found_signal = true;
		}

		if (item.type == TRLIT_SIGNAL && _settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON && item.start <= v->lookahead->current_position + 24) {
			if (HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC) || HasBit(item.data_aux, TRSLAI_NEXT_ONLY) || HasBit(item.data_aux, TRSLAI_COMBINED_SHUNT)) {
				signal_limited_lookahead_check = true;
			}
		}
	}

	if (signal_limited_lookahead_check) {
		/* Do not unnecessarily extend the reservation when passing a signal within the reservation which could not display an aspect
		 * beyond the current end of the reservation, e.g. banner repeaters and shunt signals */
		if (AdvanceTrainReservationLookaheadEnd(v, v->lookahead->current_position + 24) <= v->lookahead->reservation_end_position &&
				v->lookahead->reservation_end_position > v->lookahead->current_position + 24) {
			return true;
		}
	}

	if (found_signal) {
		int delta_z = v->lookahead->reservation_end_z - signal_z;
		delta_z += (delta_z >> 2); // Slightly overestimate slope changes to compensate for non-uniform descents
		int64_t distance = GetRealisticBrakingDistanceForSpeed(stats, signal_speed, 0, delta_z);
		if (signal_position + distance <= v->lookahead->reservation_end_position) return true;
	}

	return false;
}

static bool LookaheadWithinCurrentTunnelBridge(const Train *t)
{
	return t->lookahead->current_position >= t->lookahead->reservation_end_position - ((int)TILE_SIZE * t->lookahead->tunnel_bridge_reserved_tiles) && !HasBit(t->lookahead->flags, TRLF_TB_EXIT_FREE);
}

static bool HasLongReservePbsSignalOnTrackdir(Train* v, TileIndex tile, Trackdir trackdir, bool default_value, uint16_t lookahead_state_flags)
{
	if (HasPbsSignalOnTrackdir(tile, trackdir)) {
		if (IsNoEntrySignal(tile, TrackdirToTrack(trackdir))) return false;
		if (IsRestrictedSignal(tile)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
			if (prog != nullptr && prog->actions_used_flags & TRPAUF_LONG_RESERVE) {
				TraceRestrictProgramResult out;
				if (default_value) out.flags |= TRPRF_LONG_RESERVE;
				TraceRestrictProgramInput input(tile, trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
				if (HasBit(lookahead_state_flags, CTTLASF_STOP_FOUND)) input.input_flags |= TRPIF_PASSED_STOP;
				prog->Execute(v, input, out);
				return (out.flags & TRPRF_LONG_RESERVE);
			}
		}
		return default_value;
	}

	return false;
}

static TileIndex CheckLongReservePbsTunnelBridgeOnTrackdir(Train* v, TileIndex tile, Trackdir trackdir, bool restricted_only = false)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntranceTile(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {

		TileIndex end = GetOtherTunnelBridgeEnd(tile);
		if (restricted_only && !IsTunnelBridgeRestrictedSignal(end)) return INVALID_TILE;
		int raw_free_tiles;
		if (v->lookahead != nullptr && v->lookahead->reservation_end_tile == tile && v->lookahead->reservation_end_trackdir == trackdir) {
			if (HasBit(v->lookahead->flags, TRLF_TB_EXIT_FREE)) {
				raw_free_tiles = INT_MAX;
			} else {
				raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(tile, end, v->lookahead->tunnel_bridge_reserved_tiles + 1);
				ApplyAvailableFreeTunnelBridgeTiles(v->lookahead.get(), raw_free_tiles, tile, end);
				FlushDeferredDetermineCombineNormalShuntMode(v);
				SetTrainReservationLookaheadEnd(v);
			}
		} else {
			raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(tile, end, tile);
		}
		if (!HasAcrossTunnelBridgeReservation(end) && raw_free_tiles == INT_MAX) {
			return end;
		}
	}
	return INVALID_TILE;
}

bool _long_reserve_disabled = false;

static void TryLongReserveChooseTrainTrack(Train *v, TileIndex tile, Trackdir td, bool force_res, ChooseTrainTrackLookAheadState lookahead_state)
{
	if (_long_reserve_disabled) return;

	const bool long_enough = IsReservationLookAheadLongEnough(v, lookahead_state);

	// We reserved up to a unoccupied signalled tunnel/bridge, reserve past it as well. recursion
	TileIndex exit_tile = CheckLongReservePbsTunnelBridgeOnTrackdir(v, tile, td, long_enough);
	if (exit_tile != INVALID_TILE) {
		CFollowTrackRail ft(v);
		Trackdir exit_td = GetTunnelBridgeExitTrackdir(exit_tile);
		if (ft.Follow(exit_tile, exit_td)) {
			const TrackBits reserved_bits = GetReservedTrackbits(ft.new_tile);
			if ((ft.new_td_bits & TrackBitsToTrackdirBits(reserved_bits)) == TRACKDIR_BIT_NONE) {
				/* next tile is not reserved */

				bool long_reserve = !long_enough;
				if (IsTunnelBridgeRestrictedSignal(exit_tile)) {
					/* Test for TRPRF_LONG_RESERVE in a separate execution from TRPRF_WAIT_AT_PBS/slot operations.
					 * This is to avoid prematurely acquiring slots on the exit signal before we try to make an exit reservation.
					 */
					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(exit_tile, TrackdirToTrack(exit_td));
					if (prog != nullptr && (prog->actions_used_flags & TRPAUF_LONG_RESERVE)) {
						TraceRestrictProgramResult out;
						if (long_reserve) out.flags |= TRPRF_LONG_RESERVE;
						TraceRestrictProgramInput input(exit_tile, exit_td, nullptr, nullptr);
						if (HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND)) input.input_flags |= TRPIF_PASSED_STOP;
						prog->Execute(v, input, out);
						long_reserve = (out.flags & TRPRF_LONG_RESERVE);
					}
					if (!long_reserve) return;
					if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE | TRPAUF_REVERSE_AT)) {
						TraceRestrictProgramResult out;
						TraceRestrictProgramInput input(exit_tile, exit_td, nullptr, nullptr);
						input.permitted_slot_operations = TRPISP_ACQUIRE;
						prog->Execute(v, input, out);
						if (out.flags & (TRPRF_WAIT_AT_PBS | TRPRF_REVERSE_AT)) {
							return;
						}
					}
				}
				if (!long_reserve) return;

				const SignalState orig_exit_state = GetTunnelBridgeExitSignalState(exit_tile);

				/* reserve exit to make contiguous reservation */
				if (IsBridge(exit_tile)) {
					TryReserveRailBridgeHead(exit_tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(exit_tile)));
				} else {
					SetTunnelReservation(exit_tile, true);
				}
				if (orig_exit_state == SIGNAL_STATE_RED && _extra_aspects > 0) {
					SetTunnelBridgeExitSignalAspect(exit_tile, 0);
					UpdateAspectDeferredWithVehicleTunnelBridgeExit(v, exit_tile, GetTunnelBridgeExitTrackdir(exit_tile));
				}
				SetTunnelBridgeExitSignalState(exit_tile, SIGNAL_STATE_GREEN);

				ChooseTrainTrack(v, ft.new_tile, ft.exitdir, TrackdirBitsToTrackBits(ft.new_td_bits), CTTF_NO_LOOKAHEAD_VALIDATE | (force_res ? CTTF_FORCE_RES : CTTF_NONE), lookahead_state);
				FlushDeferredDetermineCombineNormalShuntMode(v);

				if (reserved_bits == GetReservedTrackbits(ft.new_tile)) {
					/* next tile is still not reserved, so unreserve exit and restore signal state */
					if (IsBridge(exit_tile)) {
						UnreserveRailBridgeHeadTrack(exit_tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(exit_tile)));
					} else {
						SetTunnelReservation(exit_tile, false);
					}
					SetTunnelBridgeExitSignalState(exit_tile, orig_exit_state);
				} else {
					if (orig_exit_state == SIGNAL_STATE_GREEN && _extra_aspects > 0) {
						SetTunnelBridgeExitSignalAspect(exit_tile, 0);
						UpdateAspectDeferred(exit_tile, GetTunnelBridgeExitTrackdir(exit_tile));
					}
					MarkTileDirtyByTile(exit_tile, VMDF_NOT_MAP_MODE);
				}
			}
		}
		return;
	}

	CFollowTrackRail ft(v);
	if (ft.Follow(tile, td) && HasLongReservePbsSignalOnTrackdir(v, ft.new_tile, FindFirstTrackdir(ft.new_td_bits), !long_enough, lookahead_state.flags)) {
		/* We reserved up to a LR signal, reserve past it as well. recursion */
		ChooseTrainTrack(v, ft.new_tile, ft.exitdir, TrackdirBitsToTrackBits(ft.new_td_bits), CTTF_NO_LOOKAHEAD_VALIDATE | (force_res ? CTTF_FORCE_RES : CTTF_NONE), lookahead_state);
	}
}

static void TryLongReserveChooseTrainTrackFromReservationEnd(Train *v, bool no_reserve_vehicle_tile)
{
	ClearLookAheadIfInvalid(v);

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FTRF_OKAY_UNUSED);
	if (IsRailDepotTile(origin.tile)) return;

	ChooseTrainTrackLookAheadState lookahead_state;
	if (no_reserve_vehicle_tile) SetBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE);
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		VehicleOrderSaver orders(v);
		orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
		orders.AdvanceOrdersFromLookahead(lookahead_state);

		/* Note that this must be called before the VehicleOrderSaver destructor, above */
		TryLongReserveChooseTrainTrack(v, origin.tile, origin.trackdir, true, lookahead_state);
	} else {
		TryLongReserveChooseTrainTrack(v, origin.tile, origin.trackdir, true, lookahead_state);
	}
}

/**
 * Choose a track and reserve if necessary
 *
 * @param v The vehicle
 * @param tile The tile from which to start
 * @param enterdir
 * @param tracks
 * @param flags ChooseTrainTrackFlags flags
 * @return The track the train should take and the result flags
 */
static ChooseTrainTrackResult ChooseTrainTrack(Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, ChooseTrainTrackFlags flags, ChooseTrainTrackLookAheadState lookahead_state)
{
	Track best_track = INVALID_TRACK;
	bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
	Trackdir changed_signal = INVALID_TRACKDIR;
	TileIndex final_dest = INVALID_TILE;

	dbg_assert((tracks & ~TRACK_BIT_MASK) == 0);

	ChooseTrainTrackResultFlags result_flags = CTTRF_NONE;

	/* Don't use tracks here as the setting to forbid 90 deg turns might have been switched between reservation and now. */
	TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE) return { FindFirstTrack(res_tracks), result_flags };

	bool mark_stuck = (flags & CTTF_MARK_STUCK);

	/* Quick return in case only one possible track is available */
	if (KillFirstBit(tracks) == TRACK_BIT_NONE) {
		Track track = FindFirstTrack(tracks);
		/* We need to check for signals only here, as a junction tile can't have signals. */
		if (track != INVALID_TRACK && HasPbsSignalOnTrackdir(tile, TrackEnterdirToTrackdir(track, enterdir)) && !IsNoEntrySignal(tile, track)) {
			if (IsRestrictedSignal(tile) && v->force_proceed != TFP_SIGNAL) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, track);
				if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE | TRPAUF_TRAIN_NOT_STUCK | TRPAUF_REVERSE_AT)) {
					TraceRestrictProgramResult out;
					TraceRestrictProgramInput input(tile, TrackEnterdirToTrackdir(track, enterdir), nullptr, nullptr);
					input.permitted_slot_operations = TRPISP_ACQUIRE;
					prog->Execute(v, input, out);
					if (out.flags & TRPRF_TRAIN_NOT_STUCK && !(v->track & TRACK_BIT_WORMHOLE) && !(v->track == TRACK_BIT_DEPOT)) {
						v->wait_counter = 0;
					}
					if (out.flags & TRPRF_REVERSE_AT) {
						result_flags |= CTTRF_REVERSE_AT_SIGNAL;
					}
					if (out.flags & (TRPRF_WAIT_AT_PBS | TRPRF_REVERSE_AT)) {
						if (mark_stuck) MarkTrainAsStuck(v, true);
						return { track, result_flags };
					}
				}
			}
			ClrBit(v->flags, VRF_WAITING_RESTRICTION);

			do_track_reservation = true;
			changed_signal = TrackEnterdirToTrackdir(track, enterdir);
			SetSignalStateByTrackdir(tile, changed_signal, SIGNAL_STATE_GREEN);
			if (_extra_aspects > 0) {
				SetSignalAspect(tile, track, 0);
				UpdateAspectDeferredWithVehicleRail(v, tile, changed_signal);
			}
		} else if (!do_track_reservation) {
			return { track, result_flags };
		}
		best_track = track;
	}

	if ((flags & CTTF_NON_LOOKAHEAD) && v->lookahead != nullptr) {
		/* We have reached a diverging junction with no reservation, yet we have a lookahead state.
		 * Clear the lookahead state. */
		v->lookahead.reset();
	}

	if (!(flags & CTTF_NO_LOOKAHEAD_VALIDATE)) {
		ClearLookAheadIfInvalid(v);
	}

	/* The temporary slot state only needs to be pushed to the stack (i.e. activated) on first use */
	TraceRestrictSlotTemporaryState temporary_slot_state;

	/* All exit paths except success should revert the temporary slot state if required */
	auto slot_state_guard = scope_guard([&]() {
		if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);
	});

	PBSTileInfo   origin = FollowTrainReservation(v, nullptr, FTRF_OKAY_UNUSED);
	PBSTileInfo   res_dest(tile, INVALID_TRACKDIR, false);
	DiagDirection dest_enterdir = enterdir;
	if (do_track_reservation) {
		res_dest = ExtendTrainReservation(v, origin, &tracks, &dest_enterdir, temporary_slot_state);
		if (res_dest.tile == INVALID_TILE) {
			/* Reservation failed? */
			if (mark_stuck) MarkTrainAsStuck(v);
			if (changed_signal != INVALID_TRACKDIR) SetSignalStateByTrackdir(tile, changed_signal, SIGNAL_STATE_RED);
			return { FindFirstTrack(tracks), result_flags };
		}
		if (res_dest.okay) {
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(v);
			bool long_reserve = (CheckLongReservePbsTunnelBridgeOnTrackdir(v, res_dest.tile, res_dest.trackdir) != INVALID_TILE);
			if (!long_reserve) {
				CFollowTrackRail ft(v);
				if (ft.Follow(res_dest.tile, res_dest.trackdir)) {
					Trackdir  new_td = FindFirstTrackdir(ft.new_td_bits);
					long_reserve = HasLongReservePbsSignalOnTrackdir(v, ft.new_tile, new_td, _settings_game.vehicle.train_braking_model == TBM_REALISTIC, lookahead_state.flags);
				}
			}

			if (!long_reserve) {
				/* Got a valid reservation that ends at a safe target, quick exit. */
				result_flags |= CTTRF_RESERVATION_MADE;
				if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
				if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));
				if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(v);
				return { best_track, result_flags };
			}
		}

		/* Check if the train needs service here, so it has a chance to always find a depot.
		 * Also check if the current order is a service order so we don't reserve a path to
		 * the destination but instead to the next one if service isn't needed. */
		CheckIfTrainNeedsService(v);
		if (v->current_order.IsType(OT_DUMMY) || v->current_order.IsType(OT_CONDITIONAL) || v->current_order.IsType(OT_GOTO_DEPOT) ||
				v->current_order.IsType(OT_SLOT) || v->current_order.IsType(OT_COUNTER) || v->current_order.IsType(OT_LABEL)) {
			ProcessOrders(v);
		}
	}

	/* Save the current train order. The destructor will restore the old order on function exit. */
	VehicleOrderSaver orders(v);

	if (lookahead_state.order_items_start == 0) {
		orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
	}
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) orders.AdvanceOrdersFromLookahead(lookahead_state);

	if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
		/* Pathfinders are able to tell that route was only 'guessed'. */
		bool      path_found = true;
		TileIndex new_tile = res_dest.tile;

		Track next_track = DoTrainPathfind(v, new_tile, dest_enterdir, tracks, path_found, do_track_reservation, &res_dest, &final_dest);
		DEBUG_UPDATESTATECHECKSUM("ChooseTrainTrack: v: {}, path_found: {}, next_track: {}", v->index, path_found, next_track);
		UpdateStateChecksum((((uint64_t) v->index) << 32) | (path_found << 16) | next_track);
		if (new_tile == tile) best_track = next_track;
		v->HandlePathfindingResult(path_found);
	}

	/* No track reservation requested -> finished. */
	if (!do_track_reservation) return { best_track, result_flags };

	/* A path was found, but could not be reserved. */
	if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
		if (mark_stuck) MarkTrainAsStuck(v);
		FreeTrainTrackReservation(v, origin.tile, origin.trackdir);
		return { best_track, result_flags };
	}

	/* No possible reservation target found, we are probably lost. */
	if (res_dest.tile == INVALID_TILE) {
		/* Try to find any safe destination. */
		PBSTileInfo path_end = FollowTrainReservation(v, nullptr, FTRF_OKAY_UNUSED);
		if (TryReserveSafeTrack(v, path_end.tile, path_end.trackdir, false)) {
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(v);
			TrackBits res = GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir);
			best_track = FindFirstTrack(res);
			if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));
			result_flags |= CTTRF_RESERVATION_MADE;
			if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(v);
		} else {
			FreeTrainTrackReservation(v, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(v);
		}
		return { best_track, result_flags };;
	}

	result_flags |= CTTRF_RESERVATION_MADE;

	auto check_destination_seen = [&](TileIndex tile) {
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->current_order.IsBaseStationOrder() &&
				HasStationTileRail(tile)) {
			if (v->current_order.ShouldStopAtStation(v, GetStationIndex(tile), IsRailWaypoint(tile))) {
				SetBit(lookahead_state.flags, CTTLASF_STOP_FOUND);
			} else if (v->current_order.IsType(OT_GOTO_WAYPOINT) && v->current_order.GetDestination() == GetStationIndex(tile) && (v->current_order.GetWaypointFlags() & OWF_REVERSE)) {
				if (!HasBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND)) {
					SetBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND);
					lookahead_state.reverse_dest = GetStationIndex(tile);
				}
			}
		}
	};

	check_destination_seen(res_dest.tile);

	/* Reservation target found and free, check if it is safe. */
	while (!IsSafeWaitingPosition(v, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg)) {
		/* Extend reservation until we have found a safe position. */
		DiagDirection exitdir = TrackdirToExitdir(res_dest.trackdir);
		TileIndex     next_tile = TileAddByDiagDir(res_dest.tile, exitdir);
		TrackBits     reachable = TrackdirBitsToTrackBits(GetTileTrackdirBits(next_tile, TRANSPORT_RAIL, 0)) & DiagdirReachesTracks(exitdir);
		if (Rail90DegTurnDisallowedTilesFromDiagDir(res_dest.tile, next_tile, exitdir)) {
			reachable &= ~TrackCrossesTracks(TrackdirToTrack(res_dest.trackdir));
		}

		/* Get next order with destination. */
		if (orders.SwitchToNextOrder(true)) {
			PBSTileInfo cur_dest;
			bool path_found;
			DoTrainPathfind(v, next_tile, exitdir, reachable, path_found, true, &cur_dest, nullptr);
			if (cur_dest.tile != INVALID_TILE) {
				res_dest = cur_dest;
				if (res_dest.okay) {
					check_destination_seen(res_dest.tile);
					continue;
				}
				/* Path found, but could not be reserved. */
				FreeTrainTrackReservation(v, origin.tile, origin.trackdir);
				if (mark_stuck) MarkTrainAsStuck(v);
				result_flags &= ~CTTRF_RESERVATION_MADE;
				changed_signal = INVALID_TRACKDIR;
				if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);
				break;
			}
		}
		/* No order or no safe position found, try any position. */
		if (!TryReserveSafeTrack(v, res_dest.tile, res_dest.trackdir, true)) {
			FreeTrainTrackReservation(v, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(v);
			result_flags &= ~CTTRF_RESERVATION_MADE;
			changed_signal = INVALID_TRACKDIR;
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);
		}
		break;
	}

	if (result_flags & CTTRF_RESERVATION_MADE) {
		if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(v);
		if (v->current_order.IsBaseStationOrder() && HasStationTileRail(res_dest.tile) && v->current_order.GetDestination() == GetStationIndex(res_dest.tile)) {
			if (v->current_order.ShouldStopAtStation(v, v->current_order.GetDestination(), v->current_order.IsType(OT_GOTO_WAYPOINT))) {
				v->last_station_visited = v->current_order.GetDestination();
			}
			orders.SwitchToNextOrder(true);
		}
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(v);
			if (v->lookahead != nullptr) lookahead_state.order_items_start = (uint)v->lookahead->items.size();
		}
		TryLongReserveChooseTrainTrack(v, res_dest.tile, res_dest.trackdir, (flags & CTTF_FORCE_RES), lookahead_state);
	}

	if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) TryReserveRailTrack(v->tile, TrackdirToTrack(v->GetVehicleTrackdir()));

	if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);

	orders.Restore();
	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			(v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) &&
			final_dest != INVALID_TILE && IsRailDepotTile(final_dest)) {
		v->current_order.SetDestination(GetDepotIndex(final_dest));
		v->dest_tile = final_dest;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	return { best_track, result_flags };
}

/**
 * Try to reserve a path to a safe position.
 *
 * @param v The vehicle
 * @param mark_as_stuck Should the train be marked as stuck on a failed reservation?
 * @param first_tile_okay True if no path should be reserved if the current tile is a safe position.
 * @return Result flags.
 */
TryPathReserveResultFlags TryPathReserveWithResultFlags(Train *v, bool mark_as_stuck, bool first_tile_okay)
{
	dbg_assert(v->IsFrontEngine());

	ClearLookAheadIfInvalid(v);

	if (v->lookahead != nullptr && HasBit(v->lookahead->flags, TRLF_DEPOT_END)) return TPRRF_RESERVATION_OK;

	/* We have to handle depots specially as the track follower won't look
	 * at the depot tile itself but starts from the next tile. If we are still
	 * inside the depot, a depot reservation can never be ours. */
	if (v->track == TRACK_BIT_DEPOT) {
		if (HasDepotReservation(v->tile)) {
			if (mark_as_stuck) MarkTrainAsStuck(v);
			return TPRRF_NONE;
		} else {
			/* Depot not reserved, but the next tile might be. */
			TileIndex next_tile = TileAddByDiagDir(v->tile, GetRailDepotDirection(v->tile));
			if (HasReservedTracks(next_tile, DiagdirReachesTracks(GetRailDepotDirection(v->tile)))) return TPRRF_NONE;
		}
	}

	if (IsTileType(v->tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExitOnly(v->tile) &&
			TrackdirEntersTunnelBridge(v->tile, v->GetVehicleTrackdir())) {
		/* prevent any attempt to reserve the wrong way onto a tunnel/bridge exit */
		return TPRRF_NONE;
	}
	if (IsTunnelBridgeWithSignalSimulation(v->tile) && ((v->track & TRACK_BIT_WORMHOLE) || TrackdirEntersTunnelBridge(v->tile, v->GetVehicleTrackdir()))) {
		DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(v->tile);
		Axis axis = DiagDirToAxis(tunnel_bridge_dir);
		DiagDirection axial_dir = DirToDiagDirAlongAxis(v->direction, axis);
		if (axial_dir == tunnel_bridge_dir) {
			/* prevent use of the entrance tile for reservations when the train is already in the wormhole */

			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
				/* Initialise a lookahead if there isn't one already */
				if (v->lookahead == nullptr) FillTrainReservationLookAhead(v);
				if (v->lookahead != nullptr && !LookaheadWithinCurrentTunnelBridge(v)) {
					/* Try to extend the reservation beyond the tunnel/bridge exit */
					TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
				}
			} else {
				TileIndex exit = GetOtherTunnelBridgeEnd(v->tile);
				TileIndex v_pos = TileVirtXY(v->x_pos, v->y_pos);
				if (v_pos != exit) {
					v_pos += TileOffsByDiagDir(tunnel_bridge_dir);
				}
				if (v_pos == exit) {
					return CheckTrainStayInWormHolePathReserve(v, exit) ? TPRRF_RESERVATION_OK : TPRRF_NONE;
				}
			}
			return TPRRF_NONE;
		}
	}

	Vehicle *other_train = nullptr;
	PBSTileInfo origin = FollowTrainReservation(v, &other_train);
	/* The path we are driving on is already blocked by some other train.
	 * This can only happen in certain situations when mixing path and
	 * block signals or when changing tracks and/or signals.
	 * Exit here as doing any further reservations will probably just
	 * make matters worse. */
	if (other_train != nullptr && other_train->index != v->index) {
		if (mark_as_stuck) MarkTrainAsStuck(v);
		return TPRRF_NONE;
	}
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (v->tile != origin.tile || first_tile_okay)) {
		/* Can't be stuck then. */
		if (HasBit(v->flags, VRF_TRAIN_STUCK)) SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(v);
			TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
		}
		return TPRRF_RESERVATION_OK;
	}

	/* If we are in a depot, tentatively reserve the depot. */
	if (v->track == TRACK_BIT_DEPOT && v->tile == origin.tile) {
		SetDepotReservation(v->tile, true);
		if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(v->tile, VMDF_NOT_MAP_MODE);
	}

	DiagDirection exitdir = TrackdirToExitdir(origin.trackdir);
	TileIndex new_tile;
	if (IsTileType(origin.tile, MP_TUNNELBRIDGE) && GetTunnelBridgeDirection(origin.tile) == exitdir) {
		new_tile = GetOtherTunnelBridgeEnd(origin.tile);
	} else {
		new_tile = TileAddByDiagDir(origin.tile, exitdir);
	}
	TrackBits reachable = TrackdirBitsToTrackBits(GetTileTrackdirBits(new_tile, TRANSPORT_RAIL, 0) & DiagdirReachesTrackdirs(exitdir));

	if (Rail90DegTurnDisallowedTilesFromDiagDir(origin.tile, new_tile, exitdir)) reachable &= ~TrackCrossesTracks(TrackdirToTrack(origin.trackdir));

	TryPathReserveResultFlags result_flags = TPRRF_NONE;
	if (reachable != TRACK_BIT_NONE) {
		ChooseTrainTrackResult result = ChooseTrainTrack(v, new_tile, exitdir, reachable, CTTF_FORCE_RES | (mark_as_stuck ? CTTF_MARK_STUCK : CTTF_NONE));
		if (result.ctt_flags & CTTRF_RESERVATION_MADE) {
			result_flags |= TPRRF_RESERVATION_OK;
		} else if (result.ctt_flags & CTTRF_REVERSE_AT_SIGNAL) {
			result_flags |= TPRRF_REVERSE_AT_SIGNAL;
		}
	}

	if ((result_flags & TPRRF_RESERVATION_OK) == 0) {
		/* Free the depot reservation as well. */
		if (v->track == TRACK_BIT_DEPOT && v->tile == origin.tile) SetDepotReservation(v->tile, false);
		return result_flags;
	}

	if (HasBit(v->flags, VRF_TRAIN_STUCK)) {
		v->wait_counter = 0;
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}
	ClrBit(v->flags, VRF_TRAIN_STUCK);
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(v);
	return result_flags;
}


static bool CheckReverseTrain(const Train *v)
{
	if (_settings_game.difficulty.line_reverse_mode != 0 ||
			v->track == TRACK_BIT_DEPOT) {
		return false;
	}

	dbg_assert(v->track != TRACK_BIT_NONE);

	return YapfTrainCheckReverse(v);
}

/**
 * Get the location of the next station to visit.
 * @param station Next station to visit.
 * @return Location of the new station.
 */
TileIndex Train::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = INVALID_STATION;

	const Station *st = Station::Get(station);
	if (!(st->facilities & FACIL_TRAIN)) {
		/* The destination station has no trainstation tiles. */
		this->IncrementRealOrderIndex();
		return 0;
	}

	return st->xy;
}

/** Goods at the consist have changed, update the graphics, cargo, and acceleration. */
void Train::MarkDirty()
{
	Train *v = this;
	do {
		v->colourmap = PAL_NONE;
		v->InvalidateImageCache();
		v->UpdateViewport(true, false);
	} while ((v = v->Next()) != nullptr);

	/* need to update acceleration and cached values since the goods on the train changed. */
	this->CargoChanged();
	this->UpdateAcceleration();
}

/**
 * This function looks at the vehicle and updates its speed (cur_speed
 * and subspeed) variables. Furthermore, it returns the distance that
 * the train can drive this tick. #Vehicle::GetAdvanceDistance() determines
 * the distance to drive before moving a step on the map.
 * @return distance to drive.
 */
int Train::UpdateSpeed(MaxSpeedInfo max_speed_info)
{
	AccelStatus accel_status = this->GetAccelerationStatus();
	if (this->lookahead != nullptr && HasBit(this->lookahead->flags, TRLF_APPLY_ADVISORY) && this->cur_speed <= max_speed_info.strict_max_speed) {
		ClrBit(this->lookahead->flags, TRLF_APPLY_ADVISORY);
	}
	switch (_settings_game.vehicle.train_acceleration_model) {
		default: NOT_REACHED();
		case AM_ORIGINAL:
			return this->DoUpdateSpeed({ this->acceleration * (accel_status == AS_BRAKE ? -4 : 2), this->acceleration * -4 }, 0,
					max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed, this->UsingRealisticBraking());

		case AM_REALISTIC:
			return this->DoUpdateSpeed(this->GetAcceleration(), accel_status == AS_BRAKE ? 0 : 2,
					max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed, this->UsingRealisticBraking());
	}
}
/**
 * Handle all breakdown related stuff for a train consist.
 * @param v The front engine.
 */
static bool HandlePossibleBreakdowns(Train *v)
{
	dbg_assert(v->IsFrontEngine());
	for (Train *u = v; u != nullptr; u = u->Next()) {
		if (u->breakdown_ctr != 0 && (u->IsEngine() || u->IsMultiheaded())) {
			if (u->breakdown_ctr <= 2) {
				if (u->HandleBreakdown()) return true;
				/* We check the order of v (the first vehicle) instead of u here! */
			} else if (!v->current_order.IsType(OT_LOADING)) {
				u->breakdown_ctr--;
			}
		}
	}
	return false;
}

/**
 * Trains enters a station, send out a news item if it is the first train, and start loading.
 * @param v Train that entered the station.
 * @param station Station visited.
 */
static void TrainEnterStation(Train *v, StationID station)
{
	v->last_station_visited = station;

	BaseStation *bst = BaseStation::Get(station);

	if (Waypoint::IsExpected(bst)) {
		v->DeleteUnreachedImplicitOrders();
		UpdateVehicleTimetable(v, true);
		v->last_station_visited = station;
		v->force_proceed = TFP_NONE;
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);
		v->current_order.MakeWaiting();
		v->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
		v->cur_speed = 0;
		v->UpdateTrainSpeedAdaptationLimit(0);
		return;
	}

	/* check if a train ever visited this station before */
	Station *st = Station::From(bst);
	if (!(st->had_vehicle_of_type & HVOT_TRAIN)) {
		st->had_vehicle_of_type |= HVOT_TRAIN;
		SetDParam(0, st->index);
		AddVehicleNewsItem(
			STR_NEWS_FIRST_TRAIN_ARRIVAL,
			v->owner == _local_company ? NT_ARRIVAL_COMPANY : NT_ARRIVAL_OTHER,
			v->index,
			st->index
		);
		AI::NewEvent(v->owner, new ScriptEventStationFirstVehicle(st->index, v->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, v->index));
	}

	v->force_proceed = TFP_NONE;
	SetWindowDirty(WC_VEHICLE_VIEW, v->index);

	v->BeginLoading();

	TileIndex station_tile = v->GetStationLoadingVehicle()->tile;
	TriggerStationRandomisation(st, station_tile, SRT_TRAIN_ARRIVES);
	TriggerStationAnimation(st, station_tile, SAT_TRAIN_ARRIVES);
}

/* Check if the vehicle is compatible with the specified tile */
static inline bool CheckCompatibleRail(const Train *v, TileIndex tile, DiagDirection enterdir)
{
	return IsInfraTileUsageAllowed(VEH_TRAIN, v->owner, tile) &&
			(!v->IsFrontEngine() || HasBit(v->compatible_railtypes, GetRailTypeByEntryDir(tile, enterdir)));
}

/** Data structure for storing engine speed changes of an acceleration type. */
struct AccelerationSlowdownParams {
	uint8_t small_turn; ///< Speed change due to a small turn.
	uint8_t large_turn; ///< Speed change due to a large turn.
	uint8_t z_up;       ///< Fraction to remove when moving up.
	uint8_t z_down;     ///< Fraction to add when moving down.
};

/** Speed update fractions for each acceleration type. */
static const AccelerationSlowdownParams _accel_slowdown[] = {
	/* normal accel */
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< normal
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< monorail
	{0,       256 / 2, 256 / 4, 2}, ///< maglev
};

/**
 * Modify the speed of the vehicle due to a change in altitude.
 * @param v %Train to update.
 * @param old_z Previous height.
 */
static inline void AffectSpeedByZChange(Train *v, int old_z)
{
	if (old_z == v->z_pos || _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) return;

	const AccelerationSlowdownParams *asp = &_accel_slowdown[GetRailTypeInfo(v->railtype)->acceleration_type];

	if (old_z < v->z_pos) {
		v->cur_speed -= (v->cur_speed * asp->z_up >> 8);
	} else {
		uint16_t spd = v->cur_speed + asp->z_down;
		if (spd <= v->gcache.cached_max_track_speed) v->cur_speed = spd;
	}
}

enum TrainMovedChangeSignalEnum {
	CHANGED_NOTHING, ///< No special signals were changed
	CHANGED_NORMAL_TO_PBS_BLOCK, ///< A PBS block with a non-PBS signal facing us
	CHANGED_LR_PBS ///< A long reserve PBS signal
};

static TrainMovedChangeSignalEnum TrainMovedChangeSignal(Train* v, TileIndex tile, DiagDirection dir, bool front)
{
	if (IsTileType(tile, MP_RAILWAY) &&
			GetRailTileType(tile) == RAIL_TILE_SIGNALS) {
		TrackdirBits tracks = TrackBitsToTrackdirBits(GetTrackBits(tile)) & DiagdirReachesTrackdirs(dir);
		Trackdir trackdir = FindFirstTrackdir(tracks);
		if (UpdateSignalsOnSegment(tile,  TrackdirToExitdir(trackdir), GetTileOwner(tile)) == SIGSEG_PBS && HasSignalOnTrackdir(tile, trackdir)) {
			/* A PBS block with a non-PBS signal facing us? */
			if (!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return CHANGED_NORMAL_TO_PBS_BLOCK;

			if (front && HasLongReservePbsSignalOnTrackdir(v, tile, trackdir, _settings_game.vehicle.train_braking_model == TBM_REALISTIC, 0)) return CHANGED_LR_PBS;
		}
	}
	if (IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(tile) && GetTunnelBridgeDirection(tile) == ReverseDiagDir(dir)) {
		if (UpdateSignalsOnSegment(tile, dir, GetTileOwner(tile)) == SIGSEG_PBS) {
			return CHANGED_NORMAL_TO_PBS_BLOCK;
		}
	}
	if (front && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTileType(tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationEntrance(tile)) {
		TrackdirBits tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(tile)) & DiagdirReachesTrackdirs(dir);
		Trackdir trackdir = FindFirstTrackdir(tracks);
		if (CheckLongReservePbsTunnelBridgeOnTrackdir(v, tile, trackdir) != INVALID_TILE) return CHANGED_LR_PBS;
	}

	return CHANGED_NOTHING;
}

/** Tries to reserve track under whole train consist. */
void Train::ReserveTrackUnderConsist() const
{
	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (u->track & TRACK_BIT_WORMHOLE) {
			if (IsRailCustomBridgeHeadTile(u->tile)) {
				/* reserve the first available track */
				TrackBits bits = GetAcrossTunnelBridgeTrackBits(u->tile);
				Track first_track = RemoveFirstTrack(&bits);
				dbg_assert(IsValidTrack(first_track));
				TryReserveRailTrack(u->tile, first_track);
			} else {
				TryReserveRailTrack(u->tile, DiagDirToDiagTrack(GetTunnelBridgeDirection(u->tile)));
			}
		} else if (u->track != TRACK_BIT_DEPOT) {
			TryReserveRailTrack(u->tile, TrackBitsToTrack(u->track));
		}
	}
}

/**
 * The train vehicle crashed!
 * Update its status and other parts around it.
 * @param flooded Crash was caused by flooding.
 * @return Number of people killed.
 */
uint Train::Crash(bool flooded)
{
	uint victims = 0;
	if (this->IsFrontEngine()) {
		victims += 2; // driver

		/* Remove the reserved path in front of the train if it is not stuck.
		 * Also clear all reserved tracks the train is currently on. */
		if (!HasBit(this->flags, VRF_TRAIN_STUCK)) FreeTrainTrackReservation(this);
		for (const Train *v = this; v != nullptr; v = v->Next()) {
			ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);
		}

		/* we may need to update crossing we were approaching,
		 * but must be updated after the train has been marked crashed */
		TileIndex crossing = TrainApproachingCrossingTile(this);
		if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

		/* Remove the loading indicators (if any) */
		HideFillingPercent(&this->fill_percent_te_id);
	}

	RegisterGameEvents(GEF_TRAIN_CRASH);

	victims += this->GroundVehicleBase::Crash(flooded);

	this->crash_anim_pos = flooded ? 4000 : 1; // max 4440, disappear pretty fast when flooded
	return victims;
}

/**
 * Marks train as crashed and creates an AI event.
 * Doesn't do anything if the train is crashed already.
 * @param v first vehicle of chain
 * @return number of victims (including 2 drivers; zero if train was already crashed)
 */
static uint TrainCrashed(Train *v)
{
	uint victims = 0;

	/* do not crash train twice */
	if (!(v->vehstatus & VS_CRASHED)) {
		victims = v->Crash();
		AI::NewEvent(v->owner, new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_TRAIN, victims));
		Game::NewEvent(new ScriptEventVehicleCrashed(v->index, v->tile, ScriptEventVehicleCrashed::CRASH_TRAIN, victims));
	}

	/* Try to re-reserve track under already crashed train too.
	 * Crash() clears the reservation! */
	v->ReserveTrackUnderConsist();

	return victims;
}

/** Temporary data storage for testing collisions. */
struct TrainCollideChecker {
	Train *v; ///< %Vehicle we are testing for collision.
	uint num; ///< Total number of victims if train collided.
};

/**
 * Collision test function.
 * @param v %Train vehicle to test collision with.
 * @param data %Train being examined.
 * @return \c nullptr (always continue search)
 */
static Vehicle *FindTrainCollideEnum(Vehicle *v, void *data)
{
	TrainCollideChecker *tcc = (TrainCollideChecker*)data;

	/* not in depot */
	if (Train::From(v)->track == TRACK_BIT_DEPOT) return nullptr;

	if (_settings_game.vehicle.no_train_crash_other_company) {
		/* do not crash into trains of another company. */
		if (v->owner != tcc->v->owner) return nullptr;
	}

	/* get first vehicle now to make most usual checks faster */
	Train *coll = Train::From(v)->First();

	/* can't collide with own wagons */
	if (coll == tcc->v) return nullptr;

	int x_diff = v->x_pos - tcc->v->x_pos;
	int y_diff = v->y_pos - tcc->v->y_pos;

	/* Do fast calculation to check whether trains are not in close vicinity
	 * and quickly reject trains distant enough for any collision.
	 * Differences are shifted by 7, mapping range [-7 .. 8] into [0 .. 15]
	 * Differences are then ORed and then we check for any higher bits */
	uint hash = (y_diff + 7) | (x_diff + 7);
	if (hash & ~15) return nullptr;

	/* Slower check using multiplication */
	int min_diff = (Train::From(v)->gcache.cached_veh_length + 1) / 2 + (tcc->v->gcache.cached_veh_length + 1) / 2 - 1;
	if (x_diff * x_diff + y_diff * y_diff >= min_diff * min_diff) return nullptr;

	/* Happens when there is a train under bridge next to bridge head */
	if (abs(v->z_pos - tcc->v->z_pos) > 5) return nullptr;

	/* crash both trains */
	tcc->num += TrainCrashed(tcc->v);
	tcc->num += TrainCrashed(coll);

	return nullptr; // continue searching
}

/**
 * Checks whether the specified train has a collision with another vehicle. If
 * so, destroys this vehicle, and the other vehicle if its subtype has TS_Front.
 * Reports the incident in a flashy news item, modifies station ratings and
 * plays a sound.
 * @param v %Train to test.
 */
static bool CheckTrainCollision(Train *v)
{
	/* can't collide in depot */
	if (v->track == TRACK_BIT_DEPOT) return false;

	dbg_assert(v->track & TRACK_BIT_WORMHOLE || TileVirtXY(v->x_pos, v->y_pos) == v->tile);

	TrainCollideChecker tcc;
	tcc.v = v;
	tcc.num = 0;

	/* find colliding vehicles */
	if (v->track & TRACK_BIT_WORMHOLE) {
		FindVehicleOnPos(v->tile, VEH_TRAIN, &tcc, FindTrainCollideEnum);
		FindVehicleOnPos(GetOtherTunnelBridgeEnd(v->tile), VEH_TRAIN, &tcc, FindTrainCollideEnum);
	} else {
		FindVehicleOnPosXY(v->x_pos, v->y_pos, VEH_TRAIN, &tcc, FindTrainCollideEnum);
	}

	/* any dead -> no crash */
	if (tcc.num == 0) return false;

	SetDParam(0, tcc.num);
	AddTileNewsItem(STR_NEWS_TRAIN_CRASH, NT_ACCIDENT, v->tile);

	ModifyStationRatingAround(v->tile, v->owner, -160, 30);
	if (_settings_client.sound.disaster) SndPlayVehicleFx(SND_13_TRAIN_COLLISION, v);
	return true;
}

static Vehicle *CheckTrainAtSignal(Vehicle *v, void *data)
{
	if ((v->vehstatus & VS_CRASHED)) return nullptr;

	Train *t = Train::From(v);
	DiagDirection exitdir = *(DiagDirection *)data;

	/* not front engine of a train, inside wormhole or depot, crashed */
	if (!t->IsFrontEngine() || !(t->track & TRACK_BIT_MASK)) return nullptr;

	if (t->cur_speed > 5 || VehicleExitDir(t->direction, t->track) != exitdir) return nullptr;

	return t;
}

struct FindSpaceBetweenTrainsChecker {
	int32_t pos;
	uint16_t distance;
	DiagDirection direction;
};

/** Find train in front and keep distance between trains in tunnel/bridge. */
static Vehicle *FindSpaceBetweenTrainsEnum(Vehicle *v, void *data)
{
	/* Don't look at wagons between front and back of train. */
	if ((v->Previous() != nullptr && v->Next() != nullptr)) return nullptr;

	if (!IsDiagonalDirection(v->direction)) {
		/* Check for vehicles on non-across track pieces of custom bridge head */
		if ((GetAcrossTunnelBridgeTrackBits(v->tile) & Train::From(v)->track & TRACK_BIT_ALL) == TRACK_BIT_NONE) return nullptr;
	}

	const FindSpaceBetweenTrainsChecker *checker = (FindSpaceBetweenTrainsChecker*) data;
	int32_t a, b = 0;

	switch (checker->direction) {
		default: NOT_REACHED();
		case DIAGDIR_NE: a = checker->pos; b = v->x_pos; break;
		case DIAGDIR_SE: a = v->y_pos; b = checker->pos; break;
		case DIAGDIR_SW: a = v->x_pos; b = checker->pos; break;
		case DIAGDIR_NW: a = checker->pos; b = v->y_pos; break;
	}

	if (a > b && a <= (b + (int)(checker->distance)) + (int)(TILE_SIZE) - 1) return v;
	return nullptr;
}

static bool IsTooCloseBehindTrain(Train *t, TileIndex tile, uint16_t distance, bool check_endtile)
{
	if (t->force_proceed != 0) return false;

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		if (unlikely(t->lookahead == nullptr)) {
			FillTrainReservationLookAhead(t);
		}
		if (likely(t->lookahead != nullptr)) {
			if (LookaheadWithinCurrentTunnelBridge(t)) {
				/* lookahead is within tunnel/bridge */
				TileIndex end = GetOtherTunnelBridgeEnd(t->tile);
				const int raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(t->tile, end, tile);
				ApplyAvailableFreeTunnelBridgeTiles(t->lookahead.get(), raw_free_tiles + ((raw_free_tiles != INT_MAX) ? DistanceManhattan(t->tile, tile) : 0), t->tile, end);
				SetTrainReservationLookaheadEnd(t);

				if (!LookaheadWithinCurrentTunnelBridge(t)) {
					/* Try to extend the reservation beyond the tunnel/bridge exit */
					TryLongReserveChooseTrainTrackFromReservationEnd(t, true);
				}

				if (raw_free_tiles <= (int)(distance / TILE_SIZE)) {
					/* Revert train if not going with tunnel direction. */
					DiagDirection tb_dir = GetTunnelBridgeDirection(t->tile);
					if (DirToDiagDirAlongAxis(t->direction, DiagDirToAxis(tb_dir)) != tb_dir) {
						SetBit(t->flags, VRF_REVERSING);
					}
					return true;
				}
				return false;
			} else {
				/* Try to extend the reservation beyond the tunnel/bridge exit */
				TryLongReserveChooseTrainTrackFromReservationEnd(t, true);
			}
		}
	}

	FindSpaceBetweenTrainsChecker checker;
	checker.distance = distance;
	checker.direction = DirToDiagDirAlongAxis(t->direction, DiagDirToAxis(GetTunnelBridgeDirection(t->tile)));
	switch (checker.direction) {
		default: NOT_REACHED();
		case DIAGDIR_NE: checker.pos = (TileX(tile) * TILE_SIZE) + TILE_UNIT_MASK; break;
		case DIAGDIR_SE: checker.pos = (TileY(tile) * TILE_SIZE); break;
		case DIAGDIR_SW: checker.pos = (TileX(tile) * TILE_SIZE); break;
		case DIAGDIR_NW: checker.pos = (TileY(tile) * TILE_SIZE) + TILE_UNIT_MASK; break;
	}

	if (HasVehicleOnPos(t->tile, VEH_TRAIN, &checker, &FindSpaceBetweenTrainsEnum)) {
		/* Revert train if not going with tunnel direction. */
		if (checker.direction != GetTunnelBridgeDirection(t->tile)) {
			SetBit(t->flags, VRF_REVERSING);
		}
		return true;
	}
	/* Cover blind spot at end of tunnel bridge. */
	if (check_endtile){
		if (HasVehicleOnPos(GetOtherTunnelBridgeEnd(t->tile), VEH_TRAIN, &checker, &FindSpaceBetweenTrainsEnum)) {
			/* Revert train if not going with tunnel direction. */
			if (checker.direction != GetTunnelBridgeDirection(t->tile)) {
				SetBit(t->flags, VRF_REVERSING);
			}
			return true;
		}
	}

	return false;
}

static bool CheckTrainStayInWormHolePathReserve(Train *t, TileIndex tile)
{
	bool mark_dirty = false;
	auto guard = scope_guard([&]() {
		if (mark_dirty) MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	});

	Trackdir td = GetTunnelBridgeExitTrackdir(tile);
	CFollowTrackRail ft(GetTileOwner(tile), GetRailTypeInfo(t->railtype)->all_compatible_railtypes);

	if (ft.Follow(tile, td)) {
		TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));
		if (reserved == TRACKDIR_BIT_NONE) {
			/* next tile is not reserved, so reserve the exit tile */
			if (IsBridge(tile)) {
				TryReserveRailBridgeHead(tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(tile)));
			} else {
				SetTunnelReservation(tile, true);
			}
			mark_dirty = true;
		}
	}

	auto try_exit_reservation = [&]() -> bool {
		if (IsTunnelBridgeRestrictedSignal(tile)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(td));
			if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE)) {
				TraceRestrictProgramResult out;
				TraceRestrictProgramInput input(tile, td, nullptr, nullptr);
				input.permitted_slot_operations = TRPISP_ACQUIRE;
				prog->Execute(t, input, out);
				if (out.flags & TRPRF_WAIT_AT_PBS) {
					return false;
				}
			}
		}

		if (_extra_aspects > 0) {
			SetTunnelBridgeExitSignalAspect(tile, 0);
			UpdateAspectDeferredWithVehicleTunnelBridgeExit(t, tile, GetTunnelBridgeExitTrackdir(tile));
		}

		bool ok = TryPathReserve(t);
		FlushDeferredDetermineCombineNormalShuntMode(t);
		return ok;
	};

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		if (unlikely(t->lookahead == nullptr)) {
			FillTrainReservationLookAhead(t);
		}
		if (likely(t->lookahead != nullptr)) {
			if (!HasAcrossTunnelBridgeReservation(tile)) return false;
			if (t->lookahead->reservation_end_tile == t->tile && t->lookahead->reservation_end_position - t->lookahead->current_position <= (int)TILE_SIZE && !HasBit(t->lookahead->flags, TRLF_TB_EXIT_FREE)) return false;
			SignalState exit_state = GetTunnelBridgeExitSignalState(tile);
			SetTunnelBridgeExitSignalState(tile, SIGNAL_STATE_GREEN);

			/* Get tile margin before changing vehicle direction */
			const int tile_margin = GetTileMarginInFrontOfTrain(t);

			TileIndex veh_orig_tile = t->tile;
			TrackBits veh_orig_track = t->track;
			Direction veh_orig_direction = t->direction;
			t->tile = tile;
			t->track = TRACK_BIT_WORMHOLE;
			t->direction = TrackdirToDirection(td);

			if (t->Next() == nullptr) {
				/* If this is a single-vehicle train, temporarily update the tile hash so that it can be found when scanning tiles.
				 * This is so that the whole train does not become invisible.
				 * Otherwise if the outgoing reservation reaches the entrance tile at the opposite end of this tunnel/bridge,
				 * the reservation would form a loop, resulting in various ill-effects and invariant violations. */
				t->UpdatePosition();
			}

			bool ok;
			if (t->lookahead->reservation_end_position >= t->lookahead->current_position && t->lookahead->reservation_end_position > t->lookahead->current_position + tile_margin) {
				/* Reservation was made previously and was valid then.
				 * To avoid unexpected braking due to stopping short of the lookahead end,
				 * just carry on even if the end is not a safe waiting point now. */
				ok = true;
			} else {
				ok = try_exit_reservation();
			}
			if (ok) {
				mark_dirty = true;
				if (t->lookahead->reservation_end_tile == veh_orig_tile && t->lookahead->reservation_end_position - t->lookahead->current_position <= (int)TILE_SIZE) {
					/* Less than a tile of lookahead, advance tile */
					t->lookahead->reservation_end_tile = tile;
					t->lookahead->reservation_end_trackdir = td;
					ClrBit(t->lookahead->flags, TRLF_TB_EXIT_FREE);
					ClrBit(t->lookahead->flags, TRLF_CHUNNEL);
					t->lookahead->reservation_end_position += (DistanceManhattan(veh_orig_tile, tile) - 1 - t->lookahead->tunnel_bridge_reserved_tiles) * (int)TILE_SIZE;
					t->lookahead->reservation_end_position += IsDiagonalTrackdir(td) ? 16 : 8;
					t->lookahead->tunnel_bridge_reserved_tiles = 0;
					FillTrainReservationLookAhead(t);
				}
				/* Try to extend the reservation */
				TryLongReserveChooseTrainTrackFromReservationEnd(t);
			} else {
				SetTunnelBridgeExitSignalState(tile, exit_state);
			}
			t->tile = veh_orig_tile;
			t->track = veh_orig_track;
			t->direction = veh_orig_direction;
			if (t->Next() == nullptr) {
				/* See equivalent UpdatePosition call above */
				t->UpdatePosition();
			}
			return ok;
		}
	}


	TileIndex veh_orig_tile = t->tile;
	TrackBits veh_orig_track = t->track;
	Direction veh_orig_direction = t->direction;
	t->tile = tile;
	t->track = TRACK_BIT_WORMHOLE;
	t->direction = TrackdirToDirection(td);
	bool ok = try_exit_reservation();
	t->tile = veh_orig_tile;
	t->track = veh_orig_track;
	t->direction = veh_orig_direction;
	if (ok && IsTunnelBridgeEffectivelyPBS(tile)) {
		SetTunnelBridgeExitSignalState(tile, SIGNAL_STATE_GREEN);
		if (_extra_aspects > 0) {
			SetTunnelBridgeExitSignalAspect(tile, 0);
			UpdateAspectDeferred(tile, GetTunnelBridgeExitTrackdir(tile));
		}
		mark_dirty = true;
	}
	return ok;
}

/** Simulate signals in tunnel - bridge. */
static bool CheckTrainStayInWormHole(Train *t, TileIndex tile)
{
	if (t->force_proceed != 0) return false;

	/* When not exit reverse train. */
	if (!IsTunnelBridgeSignalSimulationExit(tile)) {
		SetBit(t->flags, VRF_REVERSING);
		return true;
	}
	SigSegState seg_state = (_settings_game.pf.reserve_paths || IsTunnelBridgeEffectivelyPBS(tile)) ? SIGSEG_PBS : UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, t->owner);
	if (seg_state != SIGSEG_PBS) {
		CFollowTrackRail ft(GetTileOwner(tile), GetRailTypeInfo(t->railtype)->all_compatible_railtypes);
		if (ft.Follow(tile, GetTunnelBridgeExitTrackdir(tile))) {
			if (ft.new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
				Trackdir td = FindFirstTrackdir(ft.new_td_bits);
				if (HasPbsSignalOnTrackdir(ft.new_tile, td)) {
					/* immediately after the exit, there is a PBS signal, switch to PBS mode */
					seg_state = SIGSEG_PBS;
				}
			}
		}
	}
	if (seg_state == SIGSEG_FULL || (seg_state == SIGSEG_PBS && !CheckTrainStayInWormHolePathReserve(t, tile))) {
		t->vehstatus |= VS_TRAIN_SLOWING;
		return true;
	}

	return false;
}

static void HandleSignalBehindTrain(Train *v, int signal_number)
{
	if (!IsTunnelBridgeSignalSimulationEntrance(v->tile)) return;

	const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(v->tile);

	TileIndex tile;
	switch (v->direction) {
		default: NOT_REACHED();
		case DIR_NE: tile = TileVirtXY(v->x_pos + (TILE_SIZE * simulated_wormhole_signals), v->y_pos); break;
		case DIR_SE: tile = TileVirtXY(v->x_pos, v->y_pos - (TILE_SIZE * simulated_wormhole_signals) ); break;
		case DIR_SW: tile = TileVirtXY(v->x_pos - (TILE_SIZE * simulated_wormhole_signals), v->y_pos); break;
		case DIR_NW: tile = TileVirtXY(v->x_pos, v->y_pos + (TILE_SIZE * simulated_wormhole_signals)); break;
	}

	if (tile == v->tile) {
		/* Flip signal on ramp. */
		SetTunnelBridgeEntranceSignalGreen(tile);
	} else if (IsBridge(v->tile) && signal_number >= 0) {
		SetBridgeEntranceSimulatedSignalState(v->tile, signal_number, SIGNAL_STATE_GREEN);
		MarkSingleBridgeSignalDirty(tile, v->tile);
		if (_extra_aspects > 0) UpdateAspectFromBridgeMiddleSignalChange(v->tile, TileOffsByDiagDir(GetTunnelBridgeDirection(v->tile)) * simulated_wormhole_signals, signal_number);
	} else if (IsTunnel(v->tile) && signal_number >= 0 && _extra_aspects > 0) {
		UpdateEntranceAspectFromMiddleSignalChange(v->tile, signal_number);
	}
}

inline void DecreaseReverseDistance(Train *v)
{
	if (v->reverse_distance > 1) {
		v->reverse_distance--;
	}
}

int ReversingDistanceTargetSpeed(const Train *v)
{
	if (v->UsingRealisticBraking()) {
		TrainDecelerationStats stats(v, v->lookahead != nullptr ? v->lookahead->cached_zpos : v->CalculateOverallZPos());
		return GetRealisticBrakingSpeedForDistance(stats, v->reverse_distance - 1, 0, 0);
	}
	int target_speed;
	if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC) {
		target_speed = ((v->reverse_distance - 1) * 5) / 2;
	} else {
		target_speed = (v->reverse_distance - 1) * 10 - 5;
	}
	return std::max(0, target_speed);
}

void DecrementPendingSpeedRestrictions(Train *v)
{
	bool remaining = false;
	for (auto it = _pending_speed_restriction_change_map.lower_bound(v->index); it != _pending_speed_restriction_change_map.end() && it->first == v->index;) {
		if (--it->second.distance == 0) {
			v->speed_restriction = it->second.new_speed;
			it = _pending_speed_restriction_change_map.erase(it);
		} else {
			++it;
			remaining = true;
		}
	}
	if (!remaining) ClrBit(v->flags, VRF_PENDING_SPEED_RESTRICTION);
}

void HandleTraceRestrictSpeedRestrictionAction(const TraceRestrictProgramResult &out, Train *v, Trackdir signal_td)
{
	if (out.flags & TRPRF_SPEED_RESTRICTION_SET) {
		SetBit(v->flags, VRF_PENDING_SPEED_RESTRICTION);
		for (auto it = _pending_speed_restriction_change_map.lower_bound(v->index); it != _pending_speed_restriction_change_map.end() && it->first == v->index; ++it) {
			if ((uint16_t) (out.speed_restriction + 0xFFFF) < (uint16_t) (it->second.new_speed + 0xFFFF)) it->second.new_speed = out.speed_restriction;
		}
		uint16_t flags = 0;
		if (IsDiagonalTrack(TrackdirToTrack(signal_td))) SetBit(flags, PSRCF_DIAGONAL);
		_pending_speed_restriction_change_map.insert({ v->index, { (uint16_t) (v->gcache.cached_total_length + (HasBit(flags, PSRCF_DIAGONAL) ? 8 : 4)), out.speed_restriction, v->speed_restriction, flags } });
		if ((uint16_t) (out.speed_restriction + 0xFFFF) < (uint16_t) (v->speed_restriction + 0xFFFF)) v->speed_restriction = out.speed_restriction;
	}
	if (out.flags & TRPRF_SPEED_ADAPT_EXEMPT && !HasBit(v->flags, VRF_SPEED_ADAPTATION_EXEMPT)) {
		SetBit(v->flags, VRF_SPEED_ADAPTATION_EXEMPT);
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}
	if (out.flags & TRPRF_RM_SPEED_ADAPT_EXEMPT && HasBit(v->flags, VRF_SPEED_ADAPTATION_EXEMPT)) {
		ClrBit(v->flags, VRF_SPEED_ADAPTATION_EXEMPT);
		SetWindowDirty(WC_VEHICLE_DETAILS, v->index);
	}
}

template <typename AllowSlotAcquireT, typename PostProcessResultT>
void TrainControllerTraceRestrictFrontEvaluation(TileIndex tile, Trackdir dir, Train *v, TraceRestrictProgramActionsUsedFlags extra_action_used_flags, AllowSlotAcquireT allow_slot_acquire, PostProcessResultT post_process_result)
{
	const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(dir));
	if (prog == nullptr) return;

	TraceRestrictProgramActionsUsedFlags actions_used_flags = extra_action_used_flags | TRPAUF_SLOT_RELEASE_FRONT | TRPAUF_SPEED_RESTRICTION | TRPAUF_SPEED_ADAPTATION | TRPAUF_CHANGE_COUNTER;

	const bool slot_acquire_allowed = allow_slot_acquire();
	if (slot_acquire_allowed) actions_used_flags |= TRPAUF_SLOT_ACQUIRE;

	if ((prog->actions_used_flags & actions_used_flags) == 0) return;

	TraceRestrictProgramResult out;
	TraceRestrictProgramInput input(tile, dir, nullptr, nullptr);
	input.permitted_slot_operations = TRPISP_RELEASE_FRONT | TRPISP_CHANGE_COUNTER;
	if (slot_acquire_allowed) input.permitted_slot_operations |= TRPISP_ACQUIRE;

	prog->Execute(v, input, out);

	HandleTraceRestrictSpeedRestrictionAction(out, v, dir);
	post_process_result(out);
}

/**
 * Move a vehicle chain one movement stop forwards.
 * @param v First vehicle to move.
 * @param nomove Stop moving this and all following vehicles.
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return True if the vehicle could be moved forward, false otherwise.
 */
bool TrainController(Train *v, Vehicle *nomove, bool reverse)
{
	Train *first = v->First();
	Train *prev = nullptr;
	SCOPE_INFO_FMT([&], "TrainController: {}, {}, {}", VehicleInfoDumper(v), VehicleInfoDumper(prev), VehicleInfoDumper(nomove));
	bool direction_changed = false; // has direction of any part changed?
	bool update_signal_tunbridge_exit = false;
	Direction old_direction = INVALID_DIR;
	TrackBits old_trackbits = INVALID_TRACK_BIT;
	uint16_t old_gv_flags = 0;

	auto notify_direction_changed = [&](Direction old_direction, Direction new_direction) {
		if (prev == nullptr && _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
			const AccelerationSlowdownParams *asp = &_accel_slowdown[GetRailTypeInfo(v->railtype)->acceleration_type];
			DirDiff diff = DirDifference(old_direction, new_direction);
			v->cur_speed -= (diff == DIRDIFF_45RIGHT || diff == DIRDIFF_45LEFT ? asp->small_turn : asp->large_turn) * v->cur_speed >> 8;
		}
		direction_changed = true;
	};

	if (reverse && v->reverse_distance == 1 && (v->cur_speed <= 15 || !v->UsingRealisticBraking())) {
		/* Train is not moving too fast and reversing distance has been reached */
		goto reverse_train_direction;
	}

	/* For every vehicle after and including the given vehicle */
	for (prev = v->Previous(); v != nomove; prev = v, v = v->Next()) {
		old_direction = v->direction;
		old_trackbits = v->track;
		old_gv_flags = v->gv_flags;
		DiagDirection enterdir = DIAGDIR_BEGIN;
		bool update_signals_crossing = false; // will we update signals or crossing state?


		GetNewVehiclePosResult gp = GetNewVehiclePos(v);
		if (!(v->track & TRACK_BIT_WORMHOLE) && gp.old_tile != gp.new_tile &&
				IsRailBridgeHeadTile(gp.old_tile) && DiagdirBetweenTiles(gp.old_tile, gp.new_tile) == GetTunnelBridgeDirection(gp.old_tile)) {
			/* left a bridge headtile into a wormhole */
			Direction old_direction = v->direction;
			uint32_t r = VehicleEnterTile(v, gp.old_tile, gp.x, gp.y); // NB: old tile, the bridge head which the train just left
			if (HasBit(r, VETS_CANNOT_ENTER)) {
				goto invalid_rail;
			}
			if (old_direction != v->direction) notify_direction_changed(old_direction, v->direction);
			DiagDirection dir = GetTunnelBridgeDirection(gp.old_tile);
			const uint8_t *b = _initial_tile_subcoord[AxisToTrack(DiagDirToAxis(dir))][dir];
			gp.x = (gp.x & ~0xF) | b[0];
			gp.y = (gp.y & ~0xF) | b[1];
		}
		if (!(v->track & TRACK_BIT_WORMHOLE)) {
			/* Not inside tunnel */
			if (gp.old_tile == gp.new_tile) {
				/* Staying in the old tile */
				if (v->track == TRACK_BIT_DEPOT) {
					/* Inside depot */
					gp.x = v->x_pos;
					gp.y = v->y_pos;
					v->reverse_distance = 0;
				} else {
					/* Not inside depot */

					/* Reverse when we are at the end of the track already, do not move to the new position */
					if (v->IsFrontEngine() && !TrainCheckIfLineEnds(v, reverse)) return false;

					uint32_t r = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
					if (HasBit(r, VETS_CANNOT_ENTER)) {
						goto invalid_rail;
					}
					if (HasBit(r, VETS_ENTERED_STATION)) {
						/* The new position is the end of the platform */
						TrainEnterStation(v->First(), r >> VETS_STATION_ID_OFFSET);
					}
					if (old_direction != v->direction) notify_direction_changed(old_direction, v->direction);
				}
			} else {
				/* A new tile is about to be entered. */

				/* Determine what direction we're entering the new tile from */
				enterdir = DiagdirBetweenTiles(gp.old_tile, gp.new_tile);
				dbg_assert(IsValidDiagDirection(enterdir));

				enter_new_tile:

				/* Get the status of the tracks in the new tile and mask
				 * away the bits that aren't reachable. */
				TrackStatus ts = GetTileTrackStatus(gp.new_tile, TRANSPORT_RAIL, 0, (v->track & TRACK_BIT_WORMHOLE) ? INVALID_DIAGDIR : ReverseDiagDir(enterdir));
				TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(enterdir);

				TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts) & reachable_trackdirs;
				TrackBits red_signals = TrackdirBitsToTrackBits(TrackStatusToRedSignals(ts) & reachable_trackdirs);

				TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
				if (Rail90DegTurnDisallowedTilesFromDiagDir(gp.old_tile, gp.new_tile, enterdir) && prev == nullptr) {
					/* We allow wagons to make 90 deg turns, because forbid_90_deg
					 * can be switched on halfway a turn */
					if (!(v->track & TRACK_BIT_WORMHOLE)) {
						bits &= ~TrackCrossesTracks(FindFirstTrack(v->track));
					} else if (v->track & TRACK_BIT_MASK) {
						bits &= ~TrackCrossesTracks(FindFirstTrack(v->track & TRACK_BIT_MASK));
					}
				}

				if (bits == TRACK_BIT_NONE) goto invalid_rail;

				/* Check if the new tile constrains tracks that are compatible
				 * with the current train, if not, bail out. */
				if (!CheckCompatibleRail(v, gp.new_tile, enterdir)) goto invalid_rail;

				TrackBits chosen_track;
				bool reverse_at_signal = false;
				if (prev == nullptr) {
					/* Currently the locomotive is active. Determine which one of the
					 * available tracks to choose */
					ChooseTrainTrackResult result = ChooseTrainTrack(v, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
					chosen_track = TrackToTrackBits(result.track);
					reverse_at_signal = (result.ctt_flags & CTTRF_REVERSE_AT_SIGNAL);
					dbg_assert_msg_tile(chosen_track & (bits | GetReservedTrackbits(gp.new_tile)), gp.new_tile, "0x{:X}, 0x{:X}, 0x{:X}", chosen_track, bits, GetReservedTrackbits(gp.new_tile));

					if (v->force_proceed != TFP_NONE && IsPlainRailTile(gp.new_tile) && HasSignals(gp.new_tile)) {
						/* For each signal we find decrease the counter by one.
						 * We start at two, so the first signal we pass decreases
						 * this to one, then if we reach the next signal it is
						 * decreased to zero and we won't pass that new signal. */
						Trackdir dir = FindFirstTrackdir(trackdirbits);
						if (HasSignalOnTrackdir(gp.new_tile, dir) ||
								(HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(dir)) &&
								GetSignalType(gp.new_tile, TrackdirToTrack(dir)) != SIGTYPE_PBS)) {
							/* However, we do not want to be stopped by PBS signals
							 * entered via the back. */
							v->force_proceed = (v->force_proceed == TFP_SIGNAL) ? TFP_STUCK : TFP_NONE;
							SetWindowDirty(WC_VEHICLE_VIEW, v->index);
						}
					}

					/* Check if it's a red signal and that force proceed is not clicked. */
					if ((red_signals & chosen_track) && v->force_proceed == TFP_NONE) {
						/* In front of a red signal */
						Trackdir i = FindFirstTrackdir(trackdirbits);

						if (reverse_at_signal) {
							ClrBit(v->flags, VRF_TRAIN_STUCK);
							goto reverse_train_direction;
						}

						/* Don't handle stuck trains here. */
						if (HasBit(v->flags, VRF_TRAIN_STUCK)) return false;

						if (IsNoEntrySignal(gp.new_tile, TrackdirToTrack(i)) && HasSignalOnTrackdir(gp.new_tile, i)) {
							goto reverse_train_direction;
						}

						if (!HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(i))) {
							v->cur_speed = 0;
							v->subspeed = 0;
							v->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++v->wait_counter < _settings_game.pf.wait_oneway_signal * DAY_TICKS * 2) return false;
						} else if (HasSignalOnTrackdir(gp.new_tile, i)) {
							v->cur_speed = 0;
							v->subspeed = 0;
							v->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++v->wait_counter < _settings_game.pf.wait_twoway_signal * DAY_TICKS * 2) {
								DiagDirection exitdir = TrackdirToExitdir(i);
								TileIndex o_tile = TileAddByDiagDir(gp.new_tile, exitdir);

								exitdir = ReverseDiagDir(exitdir);

								/* check if a train is waiting on the other side */
								if (!HasVehicleOnPos(o_tile, VEH_TRAIN, &exitdir, &CheckTrainAtSignal)) return false;
							}
						}

						/* If we would reverse but are currently in a PBS block and
						 * reversing of stuck trains is disabled, don't reverse.
						 * This does not apply if the reason for reversing is a one-way
						 * signal blocking us, because a train would then be stuck forever. */
						if (!_settings_game.pf.reverse_at_signals && !HasOnewaySignalBlockingTrackdir(gp.new_tile, i) &&
								UpdateSignalsOnSegment(v->tile, enterdir, v->owner) == SIGSEG_PBS) {
							v->wait_counter = 0;
							return false;
						}
						goto reverse_train_direction;
					} else if (!(v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(gp.new_tile) &&
							IsTunnelBridgeSignalSimulationExitOnly(gp.new_tile) && TrackdirEntersTunnelBridge(gp.new_tile, FindFirstTrackdir(trackdirbits)) &&
							v->force_proceed == TFP_NONE) {
						goto reverse_train_direction;
					} else {
						TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false);

						if (IsPlainRailTile(gp.new_tile) && HasSignals(gp.new_tile) && IsRestrictedSignal(gp.new_tile)) {
							const Trackdir dir = FindFirstTrackdir(trackdirbits);
							if (HasSignalOnTrack(gp.new_tile, TrackdirToTrack(dir))) {
								TrainControllerTraceRestrictFrontEvaluation(gp.new_tile, dir, v, TRPAUF_REVERSE_BEHIND, [&]() -> bool {
									return !IsPbsSignal(GetSignalType(gp.new_tile, TrackdirToTrack(dir)));
								}, [&](const TraceRestrictProgramResult &out) {
									if (out.flags & TRPRF_REVERSE_BEHIND && GetSignalType(gp.new_tile, TrackdirToTrack(dir)) == SIGTYPE_PBS &&
											!HasSignalOnTrackdir(gp.new_tile, dir)) {
										v->reverse_distance = v->gcache.cached_total_length + (IsDiagonalTrack(TrackdirToTrack(dir)) ? 16 : 8);
										SetWindowDirty(WC_VEHICLE_VIEW, v->index);
									}
								});
							}
						}
					}
				} else {
					/* The wagon is active, simply follow the prev vehicle. */
					if (TileVirtXY(prev->x_pos, prev->y_pos) == gp.new_tile) {
						/* Choose the same track as prev */
						if (prev->track & TRACK_BIT_WORMHOLE) {
							/* Vehicles entering tunnels enter the wormhole earlier than for bridges.
							 * However, just choose the track into the wormhole. */
							dbg_assert_tile(IsTunnel(prev->tile), prev->tile);
							chosen_track = bits;
						} else {
							chosen_track = prev->track;
						}
					} else {
						/* Choose the track that leads to the tile where prev is.
						 * This case is active if 'prev' is already on the second next tile, when 'v' just enters the next tile.
						 * I.e. when the tile between them has only space for a single vehicle like
						 *  1) horizontal/vertical track tiles and
						 *  2) some orientations of tunnel entries, where the vehicle is already inside the wormhole at 8/16 from the tile edge.
						 *     Is also the train just reversing, the wagon inside the tunnel is 'on' the tile of the opposite tunnel entry.
						 */
						static const TrackBits _connecting_track[DIAGDIR_END][DIAGDIR_END] = {
							{TRACK_BIT_X,     TRACK_BIT_LOWER, TRACK_BIT_NONE,  TRACK_BIT_LEFT },
							{TRACK_BIT_UPPER, TRACK_BIT_Y,     TRACK_BIT_LEFT,  TRACK_BIT_NONE },
							{TRACK_BIT_NONE,  TRACK_BIT_RIGHT, TRACK_BIT_X,     TRACK_BIT_UPPER},
							{TRACK_BIT_RIGHT, TRACK_BIT_NONE,  TRACK_BIT_LOWER, TRACK_BIT_Y    }
						};
						DiagDirection exitdir = DiagdirBetweenTiles(gp.new_tile, TileVirtXY(prev->x_pos, prev->y_pos));
						dbg_assert(IsValidDiagDirection(exitdir));
						chosen_track = _connecting_track[enterdir][exitdir];
					}
					chosen_track &= bits;
				}

				/* Make sure chosen track is a valid track */
				dbg_assert(
						chosen_track == TRACK_BIT_X     || chosen_track == TRACK_BIT_Y ||
						chosen_track == TRACK_BIT_UPPER || chosen_track == TRACK_BIT_LOWER ||
						chosen_track == TRACK_BIT_LEFT  || chosen_track == TRACK_BIT_RIGHT);

				/* Update XY to reflect the entrance to the new tile, and select the direction to use */
				const uint8_t *b = _initial_tile_subcoord[FindFirstBit(chosen_track)][enterdir];
				gp.x = (gp.x & ~0xF) | b[0];
				gp.y = (gp.y & ~0xF) | b[1];
				Direction chosen_dir = (Direction)b[2];

				/* Call the landscape function and tell it that the vehicle entered the tile */
				uint32_t r = (v->track & TRACK_BIT_WORMHOLE) ? 0 : VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
				if (HasBit(r, VETS_CANNOT_ENTER)) {
					goto invalid_rail;
				}

				if (!(v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(gp.new_tile) && (GetAcrossTunnelBridgeTrackBits(gp.new_tile) & chosen_track)) {
					/* If red signal stop. */
					if (v->IsFrontEngine() && v->force_proceed == 0) {
						if (IsTunnelBridgeSignalSimulationEntrance(gp.new_tile) && GetTunnelBridgeEntranceSignalState(gp.new_tile) == SIGNAL_STATE_RED) {
							v->cur_speed = 0;
							v->vehstatus |= VS_TRAIN_SLOWING;
							return false;
						}
						if (IsTunnelBridgeSignalSimulationExitOnly(gp.new_tile) &&
								TrackdirEntersTunnelBridge(gp.new_tile, TrackDirectionToTrackdir(FindFirstTrack(chosen_track), chosen_dir))) {
							v->cur_speed = 0;
							goto invalid_rail;
						}
						/* Flip signal on tunnel entrance tile red. */
						SetTunnelBridgeEntranceSignalState(gp.new_tile, SIGNAL_STATE_RED);
						if (_extra_aspects > 0) {
							PropagateAspectChange(gp.new_tile, GetTunnelBridgeEntranceTrackdir(gp.new_tile), 0);
						}
						MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
						if (IsTunnelBridgeSignalSimulationBidirectional(gp.new_tile)) {
							/* Set incoming signals in other direction to red as well */
							TileIndex other_end = GetOtherTunnelBridgeEnd(gp.new_tile);
							SetTunnelBridgeEntranceSignalState(other_end, SIGNAL_STATE_RED);
							if (_extra_aspects > 0) {
								PropagateAspectChange(other_end, GetTunnelBridgeEntranceTrackdir(other_end), 0);
							}
							if (IsBridge(other_end)) {
								SetAllBridgeEntranceSimulatedSignalsRed(other_end, gp.new_tile);
								MarkBridgeDirty(other_end, gp.new_tile, VMDF_NOT_MAP_MODE);
							} else {
								MarkTileDirtyByTile(other_end, VMDF_NOT_MAP_MODE);
							}
						}
					}
				}

				if (!HasBit(r, VETS_ENTERED_WORMHOLE)) {
					Track track = FindFirstTrack(chosen_track);
					Trackdir tdir = TrackDirectionToTrackdir(track, chosen_dir);
					if (v->IsFrontEngine() && HasPbsSignalOnTrackdir(gp.new_tile, tdir)) {
						SetSignalStateByTrackdir(gp.new_tile, tdir, SIGNAL_STATE_RED);
						MarkSingleSignalDirty(gp.new_tile, tdir);
					}

					/* Clear any track reservation when the last vehicle leaves the tile */
					if (v->Next() == nullptr && !(v->track & TRACK_BIT_WORMHOLE)) ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);

					v->tile = gp.new_tile;
					v->track = chosen_track;
					dbg_assert(v->track);

					if (GetTileRailTypeByTrackBit(gp.new_tile, chosen_track) != GetTileRailTypeByTrackBit(gp.old_tile, old_trackbits)) {
						/* v->track and v->tile must both be valid and consistent before this is called */
						v->First()->ConsistChanged(CCF_TRACK);
					}
				}

				/* We need to update signal status, but after the vehicle position hash
				 * has been updated by UpdateInclination() */
				update_signals_crossing = true;

				if (chosen_dir != v->direction) {
					notify_direction_changed(v->direction, chosen_dir);
					v->direction = chosen_dir;
				}

				if (v->IsFrontEngine()) {
					v->wait_counter = 0;

					/* If we are approaching a crossing that is reserved, play the sound now. */
					TileIndex crossing = TrainApproachingCrossingTile(v);
					if (crossing != INVALID_TILE && HasCrossingReservation(crossing) && _settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, crossing);

					/* Always try to extend the reservation when entering a tile. */
					CheckNextTrainTile(v);
				}

				if (HasBit(r, VETS_ENTERED_STATION)) {
					/* The new position is the location where we want to stop */
					TrainEnterStation(v->First(), r >> VETS_STATION_ID_OFFSET);
				}
			}
		} else {
			/* Handle signal simulation on tunnel/bridge. */
			TileIndex old_tile = TileVirtXY(v->x_pos, v->y_pos);
			if (old_tile != gp.new_tile && IsTunnelBridgeWithSignalSimulation(v->tile) && (v->IsFrontEngine() || v->Next() == nullptr)) {
				const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(v->tile);
				if (old_tile == v->tile) {
					if (v->IsFrontEngine() && v->force_proceed == 0 && IsTunnelBridgeSignalSimulationExitOnly(v->tile)) goto invalid_rail;
					/* Entered wormhole set counters. */
					v->wait_counter = (TILE_SIZE * simulated_wormhole_signals) - TILE_SIZE;
					v->tunnel_bridge_signal_num = 0;

					if (v->IsFrontEngine() && IsTunnelBridgeSignalSimulationEntrance(old_tile) && (IsTunnelBridgeRestrictedSignal(old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
						const Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(old_tile);
						if (IsTunnelBridgeRestrictedSignal(old_tile)) {
							TrainControllerTraceRestrictFrontEvaluation(old_tile, trackdir, v, TRPAUF_NONE, [&]() -> bool {
								/* Only acquire slot when not using realistic braking, as the tunnel/bridge entrance otherwise acts as a block signal */
								return _settings_game.vehicle.train_braking_model != TBM_REALISTIC;
							}, [&](const TraceRestrictProgramResult &out) {});
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, old_tile, TrackdirToTrack(trackdir));
						}
					}

					if (v->Next() == nullptr && IsTunnelBridgeSignalSimulationEntrance(old_tile) && (IsTunnelBridgeRestrictedSignal(old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
						const Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(old_tile);
						const Track track = TrackdirToTrack(trackdir);

						if (IsTunnelBridgeRestrictedSignal(old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(old_tile, trackdir, nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							ApplySignalTrainAdaptationSpeed(v, old_tile, track);
						}
					}
				}

				uint distance = v->wait_counter;
				bool leaving = false;
				if (distance == 0) v->wait_counter = (TILE_SIZE * simulated_wormhole_signals);

				if (v->IsFrontEngine()) {
					/* Check if track in front is free and see if we can leave wormhole. */
					int z = GetSlopePixelZ(gp.x, gp.y, true) - v->z_pos;
					if (IsTileType(gp.new_tile, MP_TUNNELBRIDGE) &&	!(abs(z) > 2)) {
						if (CheckTrainStayInWormHole(v, gp.new_tile)) {
							v->cur_speed = 0;
							return false;
						}
						leaving = true;
						if (IsTunnelBridgeRestrictedSignal(gp.new_tile) && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
							const Trackdir trackdir = GetTunnelBridgeExitTrackdir(gp.new_tile);
							TrainControllerTraceRestrictFrontEvaluation(gp.new_tile, trackdir, v, TRPAUF_NONE, [&]() -> bool {
								return !IsTunnelBridgeEffectivelyPBS(gp.new_tile);
							}, [&](const TraceRestrictProgramResult &out) {});
						}
					} else {
						if (IsTooCloseBehindTrain(v, gp.new_tile, v->wait_counter, distance == 0)) {
							if (distance == 0) v->wait_counter = 0;
							v->cur_speed = 0;
							v->vehstatus |= VS_TRAIN_SLOWING;
							return false;
						}
						/* flip signal in front to red on bridges*/
						if (distance == 0 && IsBridge(v->tile) && IsTunnelBridgeSignalSimulationEntrance(v->tile)) {
							SetBridgeEntranceSimulatedSignalState(v->tile, v->tunnel_bridge_signal_num, SIGNAL_STATE_RED);
							MarkSingleBridgeSignalDirty(gp.new_tile, v->tile);
						}
						if (_settings_game.vehicle.train_speed_adaptation && distance == 0 && IsTunnelBridgeSignalSimulationEntrance(v->tile)) {
							ApplySignalTrainAdaptationSpeed(v, v->tile, 0x100 + v->tunnel_bridge_signal_num);
						}
					}
				}
				if (v->Next() == nullptr) {
					if (v->tunnel_bridge_signal_num > 0 && distance == (TILE_SIZE * simulated_wormhole_signals) - TILE_SIZE) {
						HandleSignalBehindTrain(v, v->tunnel_bridge_signal_num - 2);
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, v->tile, 0x100 + v->tunnel_bridge_signal_num - 1);
						}
					}
					DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(v->tile);
					Axis axis = DiagDirToAxis(tunnel_bridge_dir);
					DiagDirection axial_dir = DirToDiagDirAlongAxis(v->direction, axis);
					if (old_tile == ((axial_dir == tunnel_bridge_dir) ? v->tile : GetOtherTunnelBridgeEnd(v->tile))) {
						/* We left ramp into wormhole. */
						v->x_pos = gp.x;
						v->y_pos = gp.y;
						UpdateSignalsOnSegment(old_tile, INVALID_DIAGDIR, v->owner);
						UnreserveBridgeTunnelTile(old_tile);
						if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(old_tile, VMDF_NOT_MAP_MODE);
					}
				}
				if (distance == 0) v->tunnel_bridge_signal_num++;
				v->wait_counter -= TILE_SIZE;

				if (leaving) { // Reset counters.
					v->force_proceed = TFP_NONE;
					v->wait_counter = 0;
					v->tunnel_bridge_signal_num = 0;
					update_signal_tunbridge_exit = true;
				}
			}
			if (old_tile == gp.new_tile && IsTunnelBridgeWithSignalSimulation(v->tile) && v->IsFrontEngine()) {
				Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(v->tile));
				DiagDirection axial_dir = DirToDiagDirAlongAxis(v->direction, axis);
				TileIndex next_tile = old_tile + TileOffsByDiagDir(axial_dir);
				bool is_exit = false;
				if (IsTileType(next_tile, MP_TUNNELBRIDGE) && IsTunnelBridgeWithSignalSimulation(next_tile) &&
						ReverseDiagDir(GetTunnelBridgeDirection(next_tile)) == axial_dir) {
					if (IsBridge(next_tile) && IsBridge(v->tile)) {
						// bridge ramp facing towards us
						is_exit = true;
					} else if (IsTunnel(next_tile) && IsTunnel(v->tile)) {
						// tunnel exit at same height
						is_exit = (GetTileZ(next_tile) == GetTileZ(v->tile));
					}
				}
				if (is_exit) {
					if (CheckTrainStayInWormHole(v, next_tile)) {
						TrainApproachingLineEnd(v, true, false);
					}
				} else if (v->wait_counter == 0) {
					if (IsTooCloseBehindTrain(v, next_tile, TILE_SIZE * GetTunnelBridgeSignalSimulationSpacing(v->tile), true)) {
						TrainApproachingLineEnd(v, true, false);
					}
				}
			}

			if (IsTileType(gp.new_tile, MP_TUNNELBRIDGE) && HasBit(VehicleEnterTile(v, gp.new_tile, gp.x, gp.y), VETS_ENTERED_WORMHOLE)) {
				/* Perform look-ahead on tunnel exit. */
				if (IsRailCustomBridgeHeadTile(gp.new_tile)) {
					enterdir = ReverseDiagDir(GetTunnelBridgeDirection(gp.new_tile));
					goto enter_new_tile;
				}
				if (v->IsFrontEngine()) {
					TryReserveRailTrack(gp.new_tile, DiagDirToDiagTrack(GetTunnelBridgeDirection(gp.new_tile)));
					CheckNextTrainTile(v);
				}
				/* Prevent v->UpdateInclination() being called with wrong parameters.
				 * This could happen if the train was reversed inside the tunnel/bridge. */
				if (gp.old_tile == gp.new_tile) {
					gp.old_tile = GetOtherTunnelBridgeEnd(gp.old_tile);
				}
			} else {
				v->x_pos = gp.x;
				v->y_pos = gp.y;
				v->UpdatePosition();
				v->UpdateDeltaXY();
				DecreaseReverseDistance(v);
				if (v->lookahead != nullptr) AdvanceLookAheadPosition(v);
				if (HasBit(v->flags, VRF_PENDING_SPEED_RESTRICTION)) DecrementPendingSpeedRestrictions(v);
				if (HasBit(v->gv_flags, GVF_CHUNNEL_BIT)) {
					/* update the Z position of the vehicle */
					int old_z = v->UpdateInclination(false, false, true);

					if (prev == nullptr) {
						/* This is the first vehicle in the train */
						AffectSpeedByZChange(v, old_z);
					}
				}
				if (v->IsDrawn()) v->Vehicle::UpdateViewport(true);
				if (update_signal_tunbridge_exit) {
					UpdateSignalsOnSegment(gp.new_tile, INVALID_DIAGDIR, v->owner);
					update_signal_tunbridge_exit = false;
					if (v->IsFrontEngine() && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
						SetTunnelBridgeExitSignalState(gp.new_tile, SIGNAL_STATE_RED);
						MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
					}
				}
				continue;
			}
		}

		/* update image of train, as well as delta XY */
		v->UpdateDeltaXY();

		v->x_pos = gp.x;
		v->y_pos = gp.y;
		v->UpdatePosition();
		DecreaseReverseDistance(v);
		if (v->lookahead != nullptr) AdvanceLookAheadPosition(v);
		if (HasBit(v->flags, VRF_PENDING_SPEED_RESTRICTION)) DecrementPendingSpeedRestrictions(v);

		/* update the Z position of the vehicle */
		int old_z = v->UpdateInclination(gp.new_tile != gp.old_tile, false, v->track == TRACK_BIT_WORMHOLE);

		if (prev == nullptr) {
			/* This is the first vehicle in the train */
			AffectSpeedByZChange(v, old_z);
		}

		if (update_signal_tunbridge_exit) {
			UpdateSignalsOnSegment(gp.new_tile, INVALID_DIAGDIR, v->owner);
			update_signal_tunbridge_exit = false;
			if (v->IsFrontEngine() && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
				SetTunnelBridgeExitSignalState(gp.new_tile, SIGNAL_STATE_RED);
				MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
			}
		}

		if (update_signals_crossing) {

			if (v->IsFrontEngine()) {
				if (_settings_game.vehicle.train_speed_adaptation && IsTileType(gp.old_tile, MP_RAILWAY) && HasSignals(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					if (HasSignalOnTrackdir(gp.old_tile, ReverseTrackdir(rev_trackdir))) {
						ApplySignalTrainAdaptationSpeed(v, gp.old_tile, TrackdirToTrack(rev_trackdir));
					}
				}
				if (_settings_game.vehicle.train_speed_adaptation && IsTileType(gp.old_tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					ApplySignalTrainAdaptationSpeed(v, gp.old_tile, TrackdirToTrack(rev_trackdir));
				}

				switch (TrainMovedChangeSignal(v, gp.new_tile, enterdir, true)) {
					case CHANGED_NORMAL_TO_PBS_BLOCK:
						/* We are entering a block with PBS signals right now, but
						* not through a PBS signal. This means we don't have a
						* reservation right now. As a conventional signal will only
						* ever be green if no other train is in the block, getting
						* a path should always be possible. If the player built
						* such a strange network that it is not possible, the train
						* will be marked as stuck and the player has to deal with
						* the problem. */
						if ((!HasReservedTracks(gp.new_tile, v->track) &&
								!TryReserveRailTrack(gp.new_tile, FindFirstTrack(v->track))) ||
								!TryPathReserve(v)) {
							MarkTrainAsStuck(v);
						}

						break;

					case CHANGED_LR_PBS:
						{
							/* We went past a long reserve PBS signal. Try to extend the
							* reservation if reserving failed at another LR signal. */
							TryLongReserveChooseTrainTrackFromReservationEnd(v);
							break;
						}

					default:
						break;
				}
			}

			/* Signals can only change when the first
			 * (above) or the last vehicle moves. */
			if (v->Next() == nullptr) {
				TrainMovedChangeSignal(v, gp.old_tile, ReverseDiagDir(enterdir), false);
				if (IsLevelCrossingTile(gp.old_tile)) UpdateLevelCrossing(gp.old_tile);

				if (IsTileType(gp.old_tile, MP_RAILWAY) && HasSignals(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					const Track track = TrackdirToTrack(rev_trackdir);

					if (_settings_game.vehicle.train_speed_adaptation && HasSignalOnTrackdir(gp.old_tile, ReverseTrackdir(rev_trackdir))) {
						SetSignalTrainAdaptationSpeed(v, gp.old_tile, track);
					}

					if (HasSignalOnTrack(gp.old_tile, track)) {
						if (IsRestrictedSignal(gp.old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(gp.old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(gp.old_tile, ReverseTrackdir(rev_trackdir), nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
					}
				}

				if (IsTileType(gp.old_tile, MP_TUNNELBRIDGE) && IsTunnelBridgeSignalSimulationExit(gp.old_tile) && (IsTunnelBridgeRestrictedSignal(gp.old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					const Track track = TrackdirToTrack(rev_trackdir);

					if (TrackdirEntersTunnelBridge(gp.old_tile, rev_trackdir)) {
						if (IsTunnelBridgeRestrictedSignal(gp.old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(gp.old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(gp.old_tile, ReverseTrackdir(rev_trackdir), nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, gp.old_tile, track);
						}
					}
				}
			}
		}

		/* Do not check on every tick to save some computing time. */
		if (v->IsFrontEngine() && (v->lookahead != nullptr && v->cur_speed > 0 && v->lookahead->reservation_end_position <= v->lookahead->current_position + 24)) {
			TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
		} else if (v->IsFrontEngine() && (v->tick_counter % _settings_game.pf.path_backoff_interval == 0)) {
			CheckNextTrainTile(v);
		}
	}

	if (direction_changed) first->tcache.cached_max_curve_speed = first->GetCurveSpeedLimit();

	return true;

invalid_rail:
	/* We've reached end of line?? */
	if (prev != nullptr) return true; //FatalError("Disconnecting train");

reverse_train_direction:
	if (old_trackbits != INVALID_TRACK_BIT && (v->track ^ old_trackbits) & TRACK_BIT_WORMHOLE) {
		/* Entering/exiting wormhole failed/aborted, back out changes to vehicle direction and track */
		v->track = old_trackbits;
		v->direction = old_direction;
		v->gv_flags = old_gv_flags;
		if (!(v->track & TRACK_BIT_WORMHOLE)) v->z_pos = GetSlopePixelZ(v->x_pos, v->y_pos, true);
	}
	if (reverse) {
		v->wait_counter = 0;
		v->cur_speed = 0;
		v->subspeed = 0;
		ReverseTrainDirection(v);
	}

	return false;
}

static TrackBits GetTrackbitsFromCrashedVehicle(Train *t)
{
	TrackBits train_tbits = t->track;
	if (train_tbits & TRACK_BIT_WORMHOLE) {
		/* Vehicle is inside a wormhole, v->track contains no useful value then. */
		train_tbits = GetAcrossTunnelBridgeReservationTrackBits(t->tile);
		if (train_tbits != TRACK_BIT_NONE) return train_tbits;
		/* Pick the first available tunnel/bridge head track which could be reserved */
		train_tbits = GetAcrossTunnelBridgeTrackBits(t->tile);
		return train_tbits ^ KillFirstBit(train_tbits);
	} else {
		return train_tbits;
	}
}

/**
 * Collect trackbits of all crashed train vehicles on a tile
 * @param v Vehicle passed from Find/HasVehicleOnPos()
 * @param data trackdirbits for the result
 * @return nullptr to iterate over all vehicles on the tile.
 */
static Vehicle *CollectTrackbitsFromCrashedVehiclesEnum(Vehicle *v, void *data)
{
	TrackBits *trackbits = (TrackBits *)data;

	if ((v->vehstatus & VS_CRASHED) != 0) {
		if (Train::From(v)->track != TRACK_BIT_DEPOT) {
			*trackbits |= GetTrackbitsFromCrashedVehicle(Train::From(v));
		}
	}

	return nullptr;
}

static void SetSignalledBridgeTunnelGreenIfClear(TileIndex tile, TileIndex end)
{
	if (TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded()) {
		auto process_tile = [&](TileIndex t) {
			if (IsTunnelBridgeSignalSimulationEntrance(t)) {
				if (IsBridge(t)) {
					SetAllBridgeEntranceSimulatedSignalsGreen(t);
					MarkBridgeDirty(tile, end, VMDF_NOT_MAP_MODE);
				}
				SetTunnelBridgeEntranceSignalGreen(t);
			}
		};
		process_tile(tile);
		process_tile(end);
	}
}

static bool IsRailStationPlatformOccupied(TileIndex tile)
{
	TileIndexDiff delta = TileOffsByAxis(GetRailStationAxis(tile));

	for (TileIndex t = tile; IsCompatibleTrainStationTile(t, tile); t -= delta) {
		if (HasVehicleOnPos(t, VEH_TRAIN, nullptr, &TrainOnTileEnum)) return true;
	}
	for (TileIndex t = tile + delta; IsCompatibleTrainStationTile(t, tile); t += delta) {
		if (HasVehicleOnPos(t, VEH_TRAIN, nullptr, &TrainOnTileEnum)) return true;
	}

	return false;
}

/**
 * Deletes/Clears the last wagon of a crashed train. It takes the engine of the
 * train, then goes to the last wagon and deletes that. Each call to this function
 * will remove the last wagon of a crashed train. If this wagon was on a crossing,
 * or inside a tunnel/bridge, recalculate the signals as they might need updating
 * @param v the Vehicle of which last wagon is to be removed
 */
static void DeleteLastWagon(Train *v)
{
	Train *first = v->First();

	/* Go to the last wagon and delete the link pointing there
	 * *u is then the one-before-last wagon, and *v the last
	 * one which will physically be removed */
	Train *u = v;
	for (; v->Next() != nullptr; v = v->Next()) u = v;
	u->SetNext(nullptr);

	if (first != v) {
		/* Recalculate cached train properties */
		first->ConsistChanged(CCF_ARRANGE);
		/* Update the depot window if the first vehicle is in depot -
		 * if v == first, then it is updated in PreDestructor() */
		if (first->track == TRACK_BIT_DEPOT) {
			SetWindowDirty(WC_VEHICLE_DEPOT, first->tile);
		}
		v->last_station_visited = first->last_station_visited; // for PreDestructor
	}

	/* 'v' shouldn't be accessed after it has been deleted */
	const TrackBits orig_trackbits = v->track;
	TrackBits trackbits = GetTrackbitsFromCrashedVehicle(v);
	const TileIndex tile = v->tile;
	const Owner owner = v->owner;

	delete v;
	v = nullptr; // make sure nobody will try to read 'v' anymore

	Track track = TrackBitsToTrack(trackbits);
	if (HasReservedTracks(tile, trackbits)) {
		UnreserveRailTrack(tile, track);

		/* If there are still crashed vehicles on the tile, give the track reservation to them */
		TrackBits remaining_trackbits = TRACK_BIT_NONE;
		FindVehicleOnPos(tile, VEH_TRAIN, &remaining_trackbits, CollectTrackbitsFromCrashedVehiclesEnum);

		/* It is important that these two are the first in the loop, as reservation cannot deal with every trackbit combination */
		dbg_assert(TRACK_BEGIN == TRACK_X && TRACK_Y == TRACK_BEGIN + 1);
		for (Track t : SetTrackBitIterator(remaining_trackbits)) TryReserveRailTrack(tile, t);
	}

	/* check if the wagon was on a road/rail-crossing */
	if (IsLevelCrossingTile(tile)) UpdateLevelCrossing(tile);

	if (IsRailStationTile(tile)) {
		bool occupied = IsRailStationPlatformOccupied(tile);
		DiagDirection dir = AxisToDiagDir(GetRailStationAxis(tile));
		SetRailStationPlatformReservation(tile, dir, occupied);
		SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), occupied);
	}

	/* Update signals */
	if (IsTunnelBridgeWithSignalSimulation(tile)) {
		TileIndex end = GetOtherTunnelBridgeEnd(tile);
		UpdateSignalsOnSegment(end, INVALID_DIAGDIR, owner);
		SetSignalledBridgeTunnelGreenIfClear(tile, end);
	}
	if ((orig_trackbits & TRACK_BIT_WORMHOLE) || IsRailDepotTile(tile)) {
		UpdateSignalsOnSegment(tile, INVALID_DIAGDIR, owner);
	} else {
		SetSignalsOnBothDir(tile, track, owner);
	}
}

/**
 * Rotate all vehicles of a (crashed) train chain randomly to animate the crash.
 * @param v First crashed vehicle.
 */
static void ChangeTrainDirRandomly(Train *v)
{
	static const DirDiff delta[] = {
		DIRDIFF_45LEFT, DIRDIFF_SAME, DIRDIFF_SAME, DIRDIFF_45RIGHT
	};

	do {
		/* We don't need to twist around vehicles if they're not visible */
		if (!(v->vehstatus & VS_HIDDEN)) {
			v->direction = ChangeDir(v->direction, delta[GB(Random(), 0, 2)]);
			/* Refrain from updating the z position of the vehicle when on
			 * a bridge, because UpdateInclination() will put the vehicle under
			 * the bridge in that case */
			if (!(v->track & TRACK_BIT_WORMHOLE)) {
				v->UpdatePosition();
				v->UpdateInclination(false, true);
			} else {
				v->UpdateViewport(false, true);
			}
		}
	} while ((v = v->Next()) != nullptr);
}

/**
 * Handle a crashed train.
 * @param v First train vehicle.
 * @return %Vehicle chain still exists.
 */
static bool HandleCrashedTrain(Train *v)
{
	int state = ++v->crash_anim_pos;

	if (state == 4 && !(v->vehstatus & VS_HIDDEN)) {
		CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	}

	uint32_t r;
	if (state <= 200 && Chance16R(1, 7, r)) {
		int index = (r * 10 >> 16);

		Vehicle *u = v;
		do {
			if (--index < 0) {
				r = Random();

				CreateEffectVehicleRel(u,
					GB(r,  8, 3) + 2,
					GB(r, 16, 3) + 2,
					GB(r,  0, 3) + 5,
					EV_EXPLOSION_SMALL);
				break;
			}
		} while ((u = u->Next()) != nullptr);
	}

	if (state <= 240 && !(v->tick_counter & 3)) ChangeTrainDirRandomly(v);

	if (state >= 4440 && !(v->tick_counter & 0x1F)) {
		bool ret = v->Next() != nullptr;
		DeleteLastWagon(v);
		return ret;
	}

	return true;
}

/** Maximum speeds for train that is broken down or approaching line end */
static const uint16_t _breakdown_speeds[16] = {
	225, 210, 195, 180, 165, 150, 135, 120, 105, 90, 75, 60, 45, 30, 15, 15
};


/**
 * Train is approaching line end, slow down and possibly reverse
 *
 * @param v front train engine
 * @param signal not line end, just a red signal
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainApproachingLineEnd(Train *v, bool signal, bool reverse)
{
	/* Calc position within the current tile */
	uint x = v->x_pos & 0xF;
	uint y = v->y_pos & 0xF;

	/* for diagonal directions, 'x' will be 0..15 -
	 * for other directions, it will be 1, 3, 5, ..., 15 */
	switch (v->direction) {
		case DIR_N : x = ~x + ~y + 25; break;
		case DIR_NW: x = y;            [[fallthrough]];
		case DIR_NE: x = ~x + 16;      break;
		case DIR_E : x = ~x + y + 9;   break;
		case DIR_SE: x = y;            break;
		case DIR_S : x = x + y - 7;    break;
		case DIR_W : x = ~y + x + 9;   break;
		default: break;
	}

	/* Do not reverse when approaching red signal. Make sure the vehicle's front
	 * does not cross the tile boundary when we do reverse, but as the vehicle's
	 * location is based on their center, use half a vehicle's length as offset.
	 * Multiply the half-length by two for straight directions to compensate that
	 * we only get odd x offsets there. */
	if (!signal && x + (v->gcache.cached_veh_length + 1) / 2 * (IsDiagonalDirection(v->direction) ? 1 : 2) >= TILE_SIZE) {
		/* we are too near the tile end, reverse now */
		v->cur_speed = 0;
		if (reverse) ReverseTrainDirection(v);
		return false;
	}

	/* slow down */
	v->vehstatus |= VS_TRAIN_SLOWING;
	uint16_t break_speed = _breakdown_speeds[x & 0xF];
	if (break_speed < v->cur_speed) v->cur_speed = break_speed;

	return true;
}


/**
 * Determines whether train would like to leave the tile
 * @param v train to test
 * @return true iff vehicle is NOT entering or inside a depot or tunnel/bridge
 */
static bool TrainCanLeaveTile(const Train *v)
{
	/* Exit if inside a tunnel/bridge or a depot */
	if (v->track & TRACK_BIT_WORMHOLE || v->track == TRACK_BIT_DEPOT) return false;

	TileIndex tile = v->tile;

	/* entering a tunnel/bridge? */
	if (IsTileType(tile, MP_TUNNELBRIDGE)) {
		DiagDirection dir = GetTunnelBridgeDirection(tile);
		if (DiagDirToDir(dir) == v->direction) return false;
		if (IsRailCustomBridgeHeadTile(tile) && VehicleExitDir(v->direction, v->track) == dir) {
			if (_settings_game.pf.forbid_90_deg && v->Previous() == nullptr && GetTunnelBridgeLength(tile, GetOtherTunnelBridgeEnd(tile)) == 0) {
				/* Check for 90 degree turn on zero-length bridge span */
				if (!(GetCustomBridgeHeadTrackBits(tile) & ~TrackCrossesTracks(FindFirstTrack(v->track)))) return true;
			}
			return false;
		}
	}

	/* entering a depot? */
	if (IsRailDepotTile(tile)) {
		DiagDirection dir = ReverseDiagDir(GetRailDepotDirection(tile));
		if (DiagDirToDir(dir) == v->direction) return false;
	}

	return true;
}


/**
 * Determines whether train is approaching a rail-road crossing
 *   (thus making it barred)
 * @param v front engine of train
 * @return TileIndex of crossing the train is approaching, else INVALID_TILE
 * @pre v in non-crashed front engine
 */
static TileIndex TrainApproachingCrossingTile(const Train *v)
{
	dbg_assert(v->IsFrontEngine());
	dbg_assert(!(v->vehstatus & VS_CRASHED));

	if (!TrainCanLeaveTile(v)) return INVALID_TILE;

	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	TileIndex tile = v->tile + TileOffsByDiagDir(dir);

	/* not a crossing || wrong axis || unusable rail (wrong type or owner) */
	if (!IsLevelCrossingTile(tile) || DiagDirToAxis(dir) == GetCrossingRoadAxis(tile) ||
			!CheckCompatibleRail(v, tile, dir)) {
		return INVALID_TILE;
	}

	return tile;
}


/**
 * Checks for line end. Also, bars crossing at next tile if needed
 *
 * @param v vehicle we are checking
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainCheckIfLineEnds(Train *v, bool reverse)
{
	/* First, handle broken down train */

	if (HasBit(v->flags, VRF_BREAKDOWN_BRAKING)) {
		v->vehstatus |= VS_TRAIN_SLOWING;
	} else {
		v->vehstatus &= ~VS_TRAIN_SLOWING;
	}

	if (!TrainCanLeaveTile(v)) return true;

	/* Determine the non-diagonal direction in which we will exit this tile */
	DiagDirection dir = VehicleExitDir(v->direction, v->track);
	/* Calculate next tile */
	TileIndex tile = v->tile + TileOffsByDiagDir(dir);

	/* Determine the track status on the next tile */
	TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_RAIL, 0, ReverseDiagDir(dir));
	TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(dir);

	TrackdirBits trackdirbits = TrackStatusToTrackdirBits(ts) & reachable_trackdirs;
	TrackdirBits red_signals = TrackStatusToRedSignals(ts) & reachable_trackdirs;

	/* We are sure the train is not entering a depot, it is detected above */

	/* mask unreachable track bits if we are forbidden to do 90deg turns */
	TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
	if (Rail90DegTurnDisallowedTilesFromDiagDir(v->tile, tile, dir)) {
		bits &= ~TrackCrossesTracks(FindFirstTrack(v->track));
	}

	/* no suitable trackbits at all || unusable rail (wrong type or owner) */
	if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(v, tile, dir)) {
		return TrainApproachingLineEnd(v, false, reverse);
	}

	/* approaching red signal */
	if ((trackdirbits & red_signals) != 0) return TrainApproachingLineEnd(v, true, reverse);

	/* approaching a rail/road crossing? then make it red */
	if (IsLevelCrossingTile(tile)) MaybeBarCrossingWithSound(tile);

	if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && GetTunnelBridgeEntranceSignalState(tile) == SIGNAL_STATE_RED) {
		return TrainApproachingLineEnd(v, true, reverse);
	}

	return true;
}

/* Calculate the summed up value of all parts of a train */
Money Train::CalculateCurrentOverallValue() const
{
	Money ovr_value = 0;
	const Train *v = this;
	do {
		ovr_value += v->value;
	} while ((v = v->GetNextVehicle()) != nullptr);
	return ovr_value;
}

static bool TrainLocoHandler(Train *v, bool mode)
{
	/* train has crashed? */
	if (v->vehstatus & VS_CRASHED) {
		return mode ? true : HandleCrashedTrain(v); // 'this' can be deleted here
	} else if (v->crash_anim_pos > 0) {
		/* Reduce realistic braking brake overheating */
		v->crash_anim_pos -= (v->crash_anim_pos + 255) >> 8;
	}

	if (v->force_proceed != TFP_NONE) {
		ClrBit(v->flags, VRF_TRAIN_STUCK);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
	}

	/* train is broken down? */
	if (HasBit(v->flags, VRF_CONSIST_BREAKDOWN) && HandlePossibleBreakdowns(v)) return true;

	if (HasBit(v->flags, VRF_REVERSING) && v->cur_speed == 0) {
		ReverseTrainDirection(v);
	}

	/* exit if train is stopped */
	if ((v->vehstatus & VS_STOPPED) && v->cur_speed == 0) return true;

	bool valid_order = !v->current_order.IsType(OT_NOTHING) && v->current_order.GetType() != OT_CONDITIONAL && !v->current_order.IsType(OT_SLOT) && !v->current_order.IsType(OT_COUNTER) && !v->current_order.IsType(OT_LABEL);
	if (ProcessOrders(v) && CheckReverseTrain(v)) {
		v->wait_counter = 0;
		v->cur_speed = 0;
		v->subspeed = 0;
		ClrBit(v->flags, VRF_LEAVING_STATION);
		ReverseTrainDirection(v);
		return true;
	} else if (HasBit(v->flags, VRF_LEAVING_STATION)) {
		/* Try to reserve a path when leaving the station as we
		 * might not be marked as wanting a reservation, e.g.
		 * when an overlength train gets turned around in a station. */
		DiagDirection dir = VehicleExitDir(v->direction, v->track);
		if (IsRailDepotTile(v->tile) || IsTileType(v->tile, MP_TUNNELBRIDGE)) dir = INVALID_DIAGDIR;

		if (UpdateSignalsOnSegment(v->tile, dir, v->owner) == SIGSEG_PBS || _settings_game.pf.reserve_paths) {
			TryPathReserve(v, true, true);
		}
		ClrBit(v->flags, VRF_LEAVING_STATION);
	}

	v->HandleLoading(mode);

	if (v->current_order.IsType(OT_LOADING)) return true;

	if (CheckTrainStayInDepot(v)) return true;

	if (v->current_order.IsType(OT_WAITING) && v->reverse_distance == 0) {
		if (mode) return true;
		v->HandleWaiting(false, true);
		if (v->current_order.IsType(OT_WAITING)) return true;
		if (IsRailWaypointTile(v->tile)) {
			StationID station_id = GetStationIndex(v->tile);
			if (v->current_order.ShouldStopAtStation(v, station_id, true)) {
				UpdateVehicleTimetable(v, true);
				v->last_station_visited = station_id;
				SetWindowDirty(WC_VEHICLE_VIEW, v->index);
				v->current_order.MakeWaiting();
				v->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
				return true;
			}
		}
	}

	/* We had no order but have an order now, do look ahead. */
	if (!valid_order && !v->current_order.IsType(OT_NOTHING)) {
		CheckNextTrainTile(v);
	}

	/* Handle stuck trains. */
	if (!mode && HasBit(v->flags, VRF_TRAIN_STUCK)) {
		++v->wait_counter;

		/* Should we try reversing this tick if still stuck? */
		bool turn_around = v->wait_counter % (_settings_game.pf.wait_for_pbs_path * DAY_TICKS) == 0 && _settings_game.pf.reverse_at_signals;

		if (!turn_around && v->wait_counter % _settings_game.pf.path_backoff_interval != 0 && v->force_proceed == TFP_NONE) return true;
		TryPathReserveResultFlags path_result = TryPathReserveWithResultFlags(v);
		if ((path_result & TPRRF_RESERVATION_OK) == 0) {
			/* Still stuck. */
			if (turn_around || (path_result & TPRRF_REVERSE_AT_SIGNAL)) ReverseTrainDirection(v);

			if (HasBit(v->flags, VRF_TRAIN_STUCK) && v->wait_counter > 2 * _settings_game.pf.wait_for_pbs_path * DAY_TICKS) {
				/* Show message to player. */
				if (v->owner == _local_company && (HasBit(v->flags, VRF_WAITING_RESTRICTION) ? _settings_client.gui.restriction_wait_vehicle_warn : _settings_client.gui.lost_vehicle_warn)) {
					SetDParam(0, v->index);
					AddVehicleAdviceNewsItem(STR_NEWS_TRAIN_IS_STUCK, v->index);
				}
				v->wait_counter = 0;
			}
			/* Exit if force proceed not pressed, else reset stuck flag anyway. */
			if (v->force_proceed == TFP_NONE) return true;
			ClrBit(v->flags, VRF_TRAIN_STUCK);
			v->wait_counter = 0;
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
	}

	if (v->current_order.IsType(OT_LEAVESTATION)) {
		StationID station_id = v->current_order.GetDestination();
		v->current_order.Free();

		bool may_reverse = ProcessOrders(v);

		if (IsRailStationTile(v->tile) && GetStationIndex(v->tile) == station_id && Company::Get(v->owner)->settings.remain_if_next_order_same_station) {
			if (v->current_order.IsType(OT_GOTO_STATION) && v->current_order.GetDestination() == station_id &&
					!(v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
				v->last_station_visited = station_id;
				v->BeginLoading();
				return true;
			}
		}

		v->PlayLeaveStationSound();

		if (may_reverse && CheckReverseTrain(v)) {
			v->wait_counter = 0;
			v->cur_speed = 0;
			v->subspeed = 0;
			ClrBit(v->flags, VRF_LEAVING_STATION);
			ReverseTrainDirection(v);
		}

		SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		return true;
	}

	int j;
	{
		Train::MaxSpeedInfo max_speed_info = v->GetCurrentMaxSpeedInfoAndUpdate();

		if (!mode) v->ShowVisualEffect(std::min(max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed));
		j = v->UpdateSpeed(max_speed_info);
	}

	/* we need to invalidate the widget if we are stopping from 'Stopping 0 km/h' to 'Stopped' */
	if (v->cur_speed == 0 && (v->vehstatus & VS_STOPPED)) {
		/* If we manually stopped, we're not force-proceeding anymore. */
		v->force_proceed = TFP_NONE;
		SetWindowDirty(WC_VEHICLE_VIEW, v->index);
	}

	int adv_spd = v->GetAdvanceDistance();
	if (j < adv_spd) {
		/* if the vehicle has speed 0, update the last_speed field. */
		if (v->cur_speed == 0) v->SetLastSpeed();
	} else {
		TrainCheckIfLineEnds(v);
		/* Loop until the train has finished moving. */
		for (;;) {
			j -= adv_spd;
			TrainController(v, nullptr);
			/* Don't continue to move if the train crashed. */
			if (CheckTrainCollision(v)) break;
			/* Determine distance to next map position */
			adv_spd = v->GetAdvanceDistance();

			/* No more moving this tick */
			if (j < adv_spd || v->cur_speed == 0) break;

			OrderType order_type = v->current_order.GetType();
			/* Do not skip waypoints (incl. 'via' stations) when passing through at full speed. */
			if ((order_type == OT_GOTO_WAYPOINT || order_type == OT_GOTO_STATION) &&
						(v->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) &&
						IsTileType(v->tile, MP_STATION) &&
						v->current_order.GetDestination() == GetStationIndex(v->tile)) {
				ProcessOrders(v);
			}
		}
		v->SetLastSpeed();
	}

	for (Train *u = v; u != nullptr; u = u->Next()) {
		if (!(u->IsDrawn())) continue;

		u->UpdateViewport(false, false);
	}

	if (v->progress == 0) v->progress = j; // Save unused spd for next time, if TrainController didn't set progress

	return true;
}

/**
 * Get running cost for the train consist.
 * @return Yearly running costs.
 */
Money Train::GetRunningCost() const
{
	Money cost = 0;
	const Train *v = this;

	do {
		const Engine *e = v->GetEngine();
		if (e->u.rail.running_cost_class == INVALID_PRICE) continue;

		uint cost_factor = GetVehicleProperty(v, PROP_TRAIN_RUNNING_COST_FACTOR, e->u.rail.running_cost);
		if (cost_factor == 0) continue;

		/* Halve running cost for multiheaded parts */
		if (v->IsMultiheaded()) cost_factor /= 2;

		cost += GetPrice(e->u.rail.running_cost_class, cost_factor, e->GetGRF());
	} while ((v = v->GetNextVehicle()) != nullptr);

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

/**
 * Update train vehicle data for a tick.
 * @return True if the vehicle still exists, false if it has ceased to exist (front of consists only).
 */
bool Train::Tick()
{
	DEBUG_UPDATESTATECHECKSUM("Train::Tick: v: {}, x: {}, y: {}, track: {}", this->index, this->x_pos, this->y_pos, this->track);
	UpdateStateChecksum((((uint64_t) this->x_pos) << 32) | (this->y_pos << 16) | this->track);
	if (this->IsFrontEngine()) {
		if (!((this->vehstatus & VS_STOPPED) || this->IsWaitingInDepot()) || this->cur_speed > 0) this->running_ticks++;

		this->current_order_time++;

		if (!TrainLocoHandler(this, false)) return false;

		return TrainLocoHandler(this, true);
	} else if (this->IsFreeWagon() && (this->vehstatus & VS_CRASHED)) {
		/* Delete flooded standalone wagon chain */
		if (++this->crash_anim_pos >= 4400) {
			delete this;
			return false;
		}
	}

	return true;
}

/**
 * Check whether a train needs service, and if so, find a depot or service it.
 * @return v %Train to check.
 */
static void CheckIfTrainNeedsService(Train *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_trains == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	uint max_penalty = _settings_game.pf.yapf.maximum_go_to_depot_penalty;

	FindDepotData tfdd = FindClosestTrainDepot(v, max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) ? 2 : 1));
	/* Only go to the depot if it is not too far out of our way. */
	if (tfdd.best_length == UINT_MAX || tfdd.best_length > max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDestination() == GetDepotIndex(tfdd.tile) ? 2 : 1)) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			/* If we were already heading for a depot but it has
			 * suddenly moved farther away, we continue our normal
			 * schedule? */
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);
		}
		return;
	}

	DepotID depot = GetDepotIndex(tfdd.tile);

	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			v->current_order.GetDestination() != depot &&
			!Chance16(3, 16)) {
		return;
	}

	SetBit(v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
	v->current_order.MakeGoToDepot(depot, ODTFB_SERVICE, ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, ODATFB_NEAREST_DEPOT);
	v->dest_tile = tfdd.tile;
	SetWindowWidgetDirty(WC_VEHICLE_VIEW, v->index, WID_VV_START_STOP);

	for (Train *u = v; u != nullptr; u = u->Next()) {
		ClrBit(u->flags, VRF_BEYOND_PLATFORM_END);
	}
}

/** Update day counters of the train vehicle. */
void Train::OnNewDay()
{
	if (!EconTime::UsingWallclockUnits()) AgeVehicle(this);
	EconomyAgeVehicle(this);

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);
}

void Train::OnPeriodic()
{
	if (this->IsFrontEngine()) {
		CheckIfTrainNeedsService(this);

		CheckOrders(this);

		/* update destination */
		if (this->current_order.IsType(OT_GOTO_STATION)) {
			TileIndex tile = Station::Get(this->current_order.GetDestination())->train_station.tile;
			if (tile != INVALID_TILE) this->dest_tile = tile;
		}

		if (this->running_ticks != 0) {
			/* running costs */
			CommandCost cost(EXPENSES_TRAIN_RUN, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR  * DAY_TICKS));

			/* sharing fee */
			PayDailyTrackSharingFee(this);

			this->profit_this_year -= cost.GetCost();
			this->running_ticks = 0;

			SubtractMoneyFromCompanyFract(this->owner, cost);

			SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
			DirtyVehicleListWindowForVehicle(this);
		}
	}
	if (IsEngine() || IsMultiheaded()) {
		CheckVehicleBreakdown(this);
	}
}

/**
 * Get the tracks of the train vehicle.
 * @return Current tracks of the vehicle.
 */
Trackdir Train::GetVehicleTrackdir() const
{
	if (this->vehstatus & VS_CRASHED) return INVALID_TRACKDIR;

	if (this->track == TRACK_BIT_DEPOT) {
		/* We'll assume the train is facing outwards */
		return DiagDirToDiagTrackdir(GetRailDepotDirection(this->tile)); // Train in depot
	}

	if (this->track == TRACK_BIT_WORMHOLE) {
		/* Train in tunnel or on bridge, so just use its direction and make an educated guess
		 * given the track bits on the tunnel/bridge head tile.
		 * If a reachable track piece is reserved, use that, otherwise use the first reachable track piece.
		 */
		TrackBits tracks = GetAcrossTunnelBridgeReservationTrackBits(this->tile);
		if (!tracks) tracks = GetAcrossTunnelBridgeTrackBits(this->tile);
		Trackdir td = TrackExitdirToTrackdir(FindFirstTrack(tracks), GetTunnelBridgeDirection(this->tile));
		if (GetTunnelBridgeDirection(this->tile) != DirToDiagDir(this->direction)) td = ReverseTrackdir(td);
		return td;
	} else if (this->track & TRACK_BIT_WORMHOLE) {
		return TrackDirectionToTrackdir(FindFirstTrack(this->track & TRACK_BIT_MASK), this->direction);
	}

	return TrackDirectionToTrackdir(FindFirstTrack(this->track), this->direction);
}

/**
 * Delete a train while it is visible.
 * This happens when a company bankrupts when infrastructure sharing is enabled.
 * @param v The train to delete.
 */
void DeleteVisibleTrain(Train *v)
{
	SCOPE_INFO_FMT([v], "DeleteVisibleTrain: {}", VehicleInfoDumper(v));

	assert(!v->IsVirtual());

	FreeTrainTrackReservation(v);
	TileIndex crossing = TrainApproachingCrossingTile(v);

	/* delete train from back to front */
	Train *u;
	Train *prev = v->Last();
	FreeTrainStationPlatformReservation(v);
	do {
		u = prev;
		prev = u->Previous();
		if (prev != nullptr) prev->SetNext(nullptr);

		/* 'u' shouldn't be accessed after it has been deleted */
		TileIndex tile = u->tile;
		TrackBits trackbits = u->track;
		bool in_wormhole = trackbits & TRACK_BIT_WORMHOLE;

		delete u;

		if (in_wormhole) {
			/* Vehicle is inside a wormhole, u->track contains no useful value then. */
			if (IsTunnelBridgeWithSignalSimulation(tile)) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				AddSideToSignalBuffer(end, INVALID_DIAGDIR, GetTileOwner(tile));
				SetSignalledBridgeTunnelGreenIfClear(tile, end);
			}
		} else {
			Track track = TrackBitsToTrack(trackbits);
			if (HasReservedTracks(tile, trackbits)) UnreserveRailTrack(tile, track);
			if (IsLevelCrossingTile(tile)) UpdateLevelCrossing(tile);
		}

		/* Update signals */
		if (in_wormhole || IsRailDepotTile(tile)) {
			AddSideToSignalBuffer(tile, INVALID_DIAGDIR, GetTileOwner(tile));
		} else {
			AddTrackToSignalBuffer(tile, TrackBitsToTrack(trackbits), GetTileOwner(tile));
		}
	} while (prev != nullptr);

	if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

	UpdateSignalsInBuffer();
}

Train* CmdBuildVirtualRailWagon(const Engine *e, uint32_t user, bool no_consist_change)
{
	const RailVehicleInfo *rvi = &e->u.rail;

	Train *v = new Train();

	v->x_pos = 0;
	v->y_pos = 0;

	v->spritenum = rvi->image_index;

	v->engine_type = e->index;
	v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

	v->direction = DIR_W;
	v->tile = 0; // INVALID_TILE;

	v->owner = _current_company;
	v->track = TRACK_BIT_DEPOT;
	SetBit(v->flags, VRF_CONSIST_SPEED_REDUCTION);
	v->vehstatus = VS_HIDDEN | VS_DEFPAL;
	v->motion_counter = user;

	v->SetWagon();
	v->SetFreeWagon();
	v->SetVirtual();

	v->cargo_type = e->GetDefaultCargoType();
	v->cargo_cap = rvi->capacity;

	v->railtype = rvi->railtype;

	v->build_year = CalTime::CurYear();
	v->sprite_seq.Set(SPR_IMG_QUERY);
	v->random_bits = Random();

	v->group_id = DEFAULT_GROUP;

	AddArticulatedParts(v);

	// Make sure we set EVERYTHING to virtual, even articulated parts.
	for (Train* train_part = v; train_part != nullptr; train_part = train_part->Next()) {
		train_part->SetVirtual();
	}

	_new_vehicle_id = v->index;

	if (no_consist_change) return v;

	v->First()->ConsistChanged(CCF_ARRANGE);

	CheckConsistencyOfArticulatedVehicle(v);

	InvalidateVehicleTickCaches();

	return v;
}

Train* BuildVirtualRailVehicle(EngineID eid, StringID &error, uint32_t user, bool no_consist_change)
{
	const Engine *e = Engine::GetIfValid(eid);
	if (e == nullptr || e->type != VEH_TRAIN) {
		error = STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + VEH_TRAIN;
		return nullptr;
	}

	const RailVehicleInfo *rvi = &e->u.rail;

	int num_vehicles = (e->u.rail.railveh_type == RAILVEH_MULTIHEAD ? 2 : 1) + CountArticulatedParts(eid, false);
	if (!Train::CanAllocateItem(num_vehicles)) {
		error = STR_ERROR_TOO_MANY_VEHICLES_IN_GAME;
		return nullptr;
	}

	RegisterGameEvents(GEF_VIRT_TRAIN);

	if (rvi->railveh_type == RAILVEH_WAGON) {
		return CmdBuildVirtualRailWagon(e, user, no_consist_change);
	}

	Train *v = new Train();

	v->x_pos = 0;
	v->y_pos = 0;

	v->direction = DIR_W;
	v->tile = 0; // INVALID_TILE;
	v->owner = _current_company;
	v->track = TRACK_BIT_DEPOT;
	SetBit(v->flags, VRF_CONSIST_SPEED_REDUCTION);
	v->vehstatus = VS_HIDDEN | VS_STOPPED | VS_DEFPAL;
	v->spritenum = rvi->image_index;
	v->cargo_type = e->GetDefaultCargoType();
	v->cargo_cap = rvi->capacity;
	v->last_station_visited = INVALID_STATION;
	v->motion_counter = user;

	v->engine_type = e->index;
	v->gcache.first_engine = INVALID_ENGINE; // needs to be set before first callback

	v->reliability = e->reliability;
	v->reliability_spd_dec = e->reliability_spd_dec;
	v->max_age = e->GetLifeLengthInDays();

	v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_trains);
	v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
	AssignBit(v->vehicle_flags, VF_AUTOMATE_TIMETABLE, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
	AssignBit(v->vehicle_flags, VF_TIMETABLE_SEPARATION, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

	v->railtype = rvi->railtype;
	_new_vehicle_id = v->index;

	v->build_year = CalTime::CurYear();
	v->sprite_seq.Set(SPR_IMG_QUERY);
	v->random_bits = Random();

	v->group_id = DEFAULT_GROUP;

	v->SetFrontEngine();
	v->SetEngine();
	v->SetVirtual();

	if (rvi->railveh_type == RAILVEH_MULTIHEAD) {
		AddRearEngineToMultiheadedTrain(v);
	} else {
		AddArticulatedParts(v);
	}

	// Make sure we set EVERYTHING to virtual, even articulated parts.
	for (Train* train_part = v; train_part != nullptr; train_part = train_part->Next()) {
		train_part->SetVirtual();
	}

	if (no_consist_change) return v;

	v->ConsistChanged(CCF_ARRANGE);

	CheckConsistencyOfArticulatedVehicle(v);

	InvalidateVehicleTickCaches();

	return v;
}

/**
 * Build a virtual train vehicle.
 * @param tile unused
 * @param flags type of operation
 * @param p1 various bitstuffed data
 *  bits  0-15: vehicle type being built.
 *  bits 24-31: refit cargo type.
 * @param p2 user
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildVirtualRailVehicle(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	EngineID eid = GB(p1, 0, 16);

	if (!IsEngineBuildable(eid, VEH_TRAIN, _current_company)) {
		return_cmd_error(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + VEH_TRAIN);
	}

	/* Validate the cargo type. */
	CargoID cargo = GB(p1, 24, 8);
	if (cargo >= NUM_CARGO && cargo != INVALID_CARGO) return CMD_ERROR;

	bool should_execute = (flags & DC_EXEC) != 0;

	if (should_execute) {
		StringID err = INVALID_STRING_ID;
		Train* train = BuildVirtualRailVehicle(eid, err, p2, false);

		if (train == nullptr) {
			return_cmd_error(err);
		}

		if (cargo != INVALID_CARGO) {
			CargoID default_cargo = Engine::Get(eid)->GetDefaultCargoType();
			if (default_cargo != cargo) {
				CommandCost refit_res = CmdRefitVehicle(tile, flags, train->index, cargo, nullptr);
				if (!refit_res.Succeeded()) return refit_res;
			}
		}
	}

	return CommandCost();
}

void ClearVehicleWindows(const Train *v)
{
	if (v->IsPrimaryVehicle()) {
		CloseWindowById(WC_VEHICLE_VIEW, v->index);
		CloseWindowById(WC_VEHICLE_ORDERS, v->index);
		CloseWindowById(WC_VEHICLE_REFIT, v->index);
		CloseWindowById(WC_VEHICLE_DETAILS, v->index);
		CloseWindowById(WC_VEHICLE_TIMETABLE, v->index);
		CloseWindowById(WC_SCHDISPATCH_SLOTS, v->index);
		CloseWindowById(WC_VEHICLE_CARGO_TYPE_LOAD_ORDERS, v->index);
		CloseWindowById(WC_VEHICLE_CARGO_TYPE_UNLOAD_ORDERS, v->index);
	}
}

/**
 * Issue a start/stop command
 * @param v a vehicle
 * @param evaluate_callback shall the start/stop callback be evaluated?
 * @return success or error
 */
static inline CommandCost CmdStartStopVehicle(const Vehicle *v, bool evaluate_callback)
{
	return DoCommand(0, v->index, evaluate_callback ? 1 : 0, DC_EXEC | DC_AUTOREPLACE, CMD_START_STOP_VEHICLE);
}

/**
* Replace a vehicle based on a template replacement order.
* @param tile unused
* @param flags type of operation
* @param p1 the ID of the vehicle to replace.
* @param p2 unused
* @param text unused
* @return the cost of this operation or an error
*/
CommandCost CmdTemplateReplaceVehicle(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Train *incoming = Train::GetIfValid(p1);

	if (incoming == nullptr || !incoming->IsPrimaryVehicle() || !incoming->IsChainInDepot()) {
		return CMD_ERROR;
	}

	CommandCost buy(EXPENSES_NEW_VEHICLES);

	const bool was_stopped = (incoming->vehstatus & VS_STOPPED) != 0;
	if (!was_stopped) {
		CommandCost cost = CmdStartStopVehicle(incoming, true);
		if (cost.Failed()) return cost;
	}
	auto guard = scope_guard([&]() {
		_new_vehicle_id = incoming->index;
		if (!was_stopped) buy.AddCost(CmdStartStopVehicle(incoming, false));
	});

	Train *new_chain = nullptr;
	Train *remainder_chain = nullptr;
	TemplateVehicle *tv = GetTemplateVehicleByGroupIDRecursive(incoming->group_id);
	if (tv == nullptr) {
		return CMD_ERROR;
	}
	EngineID eid = tv->engine_type;

	CommandCost tmp_result(EXPENSES_NEW_VEHICLES);

	/* first some tests on necessity and sanity */
	if (tv == nullptr) return CommandCost();
	if (tv->IsReplaceOldOnly() && !incoming->NeedsAutorenewing(Company::Get(incoming->owner), false)) {
		return CommandCost();
	}
	const TBTRDiffFlags diff = TrainTemplateDifference(incoming, tv);
	if (diff == TBTRDF_NONE) return CommandCost();

	const bool need_replacement = (diff & TBTRDF_CONSIST);
	const bool need_refit = (diff & TBTRDF_REFIT);
	const bool refit_to_template = tv->refit_as_template;

	CargoID store_refit_ct = INVALID_CARGO;
	uint16_t store_refit_csubt = 0;
	// if a train shall keep its old refit, store the refit setting of its first vehicle
	if (!refit_to_template) {
		for (Train *getc = incoming; getc != nullptr; getc = getc->GetNextUnit()) {
			if (getc->cargo_type != INVALID_CARGO && getc->cargo_cap > 0) {
				store_refit_ct = getc->cargo_type;
				break;
			}
		}
	}

	if (need_replacement) {
		CommandCost buy_cost = TestBuyAllTemplateVehiclesInChain(tv, tile);
		if (buy_cost.Failed()) {
			if (buy_cost.GetErrorMessage() == INVALID_STRING_ID) return CommandCost(STR_ERROR_CAN_T_BUY_TRAIN);
			return buy_cost;
		} else if (!CheckCompanyHasMoney(buy_cost)) {
			return CommandCost(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY);
		}
	}

	TemplateDepotVehicles depot_vehicles;
	if (tv->IsSetReuseDepotVehicles()) depot_vehicles.Init(tile);

	auto refit_unit = [&](const Train *unit, CargoID cid, uint16_t csubt) {
		CommandCost refit_cost = DoCommand(unit->tile, unit->index, cid | csubt << 8 | (1 << 16), flags, GetCmdRefitVeh(unit));
		if (refit_cost.Succeeded()) buy.AddCost(refit_cost);
	};

	if (!(flags & DC_EXEC)) {
		/* Simplified operation for cost estimation, this doesn't have to exactly match the actual cost due to CMD_NO_TEST */
		if (need_replacement || need_refit) {
			std::vector<const Train *> in;
			for (const Train *u = incoming; u != nullptr; u = u->GetNextUnit()) {
				in.push_back(u);
			}
			auto process_unit = [&](const TemplateVehicle *cur_tmpl) {
				for (auto iter = in.begin(); iter != in.end(); ++iter) {
					const Train *u = *iter;
					if (u->engine_type == cur_tmpl->engine_type) {
						/* use existing engine */
						in.erase(iter);
						if (refit_to_template) {
							buy.AddCost(DoCommand(u->tile, u->index, cur_tmpl->cargo_type | cur_tmpl->cargo_subtype << 8 | (1 << 16) | (1 << 31), flags, GetCmdRefitVeh(u)));
						} else {
							refit_unit(u, store_refit_ct, store_refit_csubt);
						}
						return;
					}
				}

				if (tv->IsSetReuseDepotVehicles()) {
					const Train *depot_eng = depot_vehicles.ContainsEngine(cur_tmpl->engine_type, incoming);
					if (depot_eng != nullptr) {
						depot_vehicles.RemoveVehicle(depot_eng->index);
						if (refit_to_template) {
							buy.AddCost(DoCommand(depot_eng->tile, depot_eng->index, cur_tmpl->cargo_type | cur_tmpl->cargo_subtype << 8 | (1 << 16) | (1 << 31), flags, GetCmdRefitVeh(depot_eng)));
						} else {
							refit_unit(depot_eng, store_refit_ct, store_refit_csubt);
						}
						return;
					}
				}

				CargoID refit_cargo = refit_to_template ? cur_tmpl->cargo_type : store_refit_ct;
				uint32_t refit_cmd = (refit_cargo != INVALID_CARGO) ? (refit_cargo << 24) : 0;
				buy.AddCost(DoCommand(tile, cur_tmpl->engine_type | (1 << 16) | refit_cmd, 0, flags, CMD_BUILD_VEHICLE));
			};
			for (const TemplateVehicle *cur_tmpl = tv; cur_tmpl != nullptr; cur_tmpl = cur_tmpl->GetNextUnit()) {
				process_unit(cur_tmpl);
			}
			if (!tv->IsSetKeepRemainingVehicles()) {
				/* Sell leftovers */
				for (const Train *u : in) {
					/* Do not dry-run selling each part using CMD_SELL_VEHICLE because this can fail due to consist/wagon-attachment callbacks */
					buy.AddCost(-u->value);
					if (u->other_multiheaded_part != nullptr) {
						buy.AddCost(-u->other_multiheaded_part->value);
					}
				}
			}
		}
		if (buy.Failed()) buy.MultiplyCost(0);
		return buy;
	}

	RegisterGameEvents(GEF_TBTR_REPLACEMENT);

	if (need_replacement) {
		// step 1: generate primary for newchain and generate remainder_chain
		// 1. primary of incoming might already fit the template
		//    leave incoming's primary as is and move the rest to a free chain = remainder_chain
		// 2. needed primary might be one of incoming's member vehicles
		// 3. primary might be available as orphan vehicle in the depot
		// 4. we need to buy a new engine for the primary
		// all options other than 1. need to make sure to copy incoming's primary's status
		auto setup_head = [&]() -> CommandCost {
			/* Case 1 */
			if (eid == incoming->engine_type) {
				new_chain = incoming;
				remainder_chain = incoming->GetNextUnit();
				if (remainder_chain) {
					CommandCost move_cost = CmdMoveRailVehicle(tile, flags, remainder_chain->index | (1 << 20), INVALID_VEHICLE, 0);
					if (move_cost.Failed()) {
						/* This should not fail, if it does give up immediately */
						return move_cost;
					}
				}
				return CommandCost();
			}

			/* Case 2 */
			new_chain = ChainContainsEngine(eid, incoming);
			if (new_chain != nullptr) {
				/* new_chain is the needed engine, move it to an empty spot in the depot */
				CommandCost move_cost = DoCommand(tile, new_chain->index, INVALID_VEHICLE, flags, CMD_MOVE_RAIL_VEHICLE);
				if (move_cost.Succeeded()) {
					remainder_chain = incoming;
					return CommandCost();
				}
			}

			/* Case 3 */
			if (tv->IsSetReuseDepotVehicles()) {
				new_chain = depot_vehicles.ContainsEngine(eid, incoming);
				if (new_chain != nullptr) {
					ClearVehicleWindows(new_chain);
					CommandCost move_cost = DoCommand(tile, new_chain->index, INVALID_VEHICLE, flags, CMD_MOVE_RAIL_VEHICLE);
					if (move_cost.Succeeded()) {
						depot_vehicles.RemoveVehicle(new_chain->index);
						remainder_chain = incoming;
						return CommandCost();
					}
				}
			}

			/* Case 4 */
			CommandCost buy_cost = DoCommand(tile, eid | (1 << 16), 0, flags, CMD_BUILD_VEHICLE);
			/* break up in case buying the vehicle didn't succeed */
			if (buy_cost.Failed()) {
				return buy_cost;
			}
			buy.AddCost(buy_cost);
			new_chain = Train::Get(_new_vehicle_id);
			/* prepare the remainder chain */
			remainder_chain = incoming;
			return CommandCost();
		};
		CommandCost head_result = setup_head();
		if (head_result.Failed()) return head_result;

		// If we bought a new engine or reused one from the depot, copy some parameters from the incoming primary engine
		if (incoming != new_chain) {
			CopyHeadSpecificThings(incoming, new_chain, flags, false);
			NeutralizeStatus(incoming);

			// additionally, if we don't want to use the template refit, refit as incoming
			// the template refit will be set further down, if we use it at all
			if (!refit_to_template) {
				refit_unit(new_chain, store_refit_ct, store_refit_csubt);
			}
		}

		// step 2: fill up newchain according to the template
		// foreach member of template (after primary):
		// 1. needed engine might be within remainder_chain already
		// 2. needed engine might be orphaned within the depot (copy status)
		// 3. we need to buy (again)                           (copy status)
		Train *last_veh = new_chain;
		for (TemplateVehicle *cur_tmpl = tv->GetNextUnit(); cur_tmpl != nullptr; cur_tmpl = cur_tmpl->GetNextUnit()) {
			Train *new_part = nullptr;
			auto setup_chain_part = [&]() {
				/* Case 1: engine contained in remainder chain */
				new_part = ChainContainsEngine(cur_tmpl->engine_type, remainder_chain);
				if (new_part != nullptr) {
					Train *remainder_chain_next = remainder_chain;
					if (new_part == remainder_chain) {
						remainder_chain_next = remainder_chain->GetNextUnit();
					}
					CommandCost move_cost = CmdMoveRailVehicle(tile, flags, new_part->index, last_veh->index, 0);
					if (move_cost.Succeeded()) {
						remainder_chain = remainder_chain_next;
						return;
					}
				}

				/* Case 2: engine contained somewhere else in the depot */
				if (tv->IsSetReuseDepotVehicles()) {
					new_part = depot_vehicles.ContainsEngine(cur_tmpl->engine_type, new_chain);
					if (new_part != nullptr) {
						CommandCost move_cost = CmdMoveRailVehicle(tile, flags, new_part->index, last_veh->index, 0);
						if (move_cost.Succeeded()) {
							depot_vehicles.RemoveVehicle(new_part->index);
							return;
						}
					}
				}

				/* Case 3: must buy new engine */
				CommandCost buy_cost = DoCommand(tile, cur_tmpl->engine_type | (1 << 16), 0, flags, CMD_BUILD_VEHICLE);
				if (buy_cost.Failed()) {
					new_part = nullptr;
					return;
				}
				new_part = Train::Get(_new_vehicle_id);
				CommandCost move_cost = CmdMoveRailVehicle(tile, flags, new_part->index, last_veh->index, 0);
				if (move_cost.Succeeded()) {
					buy.AddCost(buy_cost);
				} else {
					DoCommand(tile, new_part->index, 0, flags, CMD_SELL_VEHICLE);
					new_part = nullptr;
				}
			};
			setup_chain_part();
			if (new_part != nullptr) {
				last_veh = new_part;
			}

			if (!refit_to_template && new_part != nullptr) {
				refit_unit(new_part, store_refit_ct, store_refit_csubt);
			}
		}
	} else {
		/* no replacement done */
		new_chain = incoming;
	}

	/// step 3: reorder and neutralize the remaining vehicles from incoming
	// wagons remaining from remainder_chain should be filled up in as few free wagon chains as possible
	// each loco might be left as singular in the depot
	// neutralize each remaining engine's status

	// refit, only if the template option is set so
	if (refit_to_template && (need_refit || need_replacement)) {
		buy.AddCost(CmdRefitTrainFromTemplate(new_chain, tv, flags));
	}

	buy.AddCost(CmdSetTrainUnitDirectionFromTemplate(new_chain, tv, flags));

	if (new_chain && remainder_chain) {
		for (Train *ct = remainder_chain; ct != nullptr; ct = ct->Next()) {
			TransferCargoForTrain(ct, new_chain);
		}
	}

	// point incoming to the newly created train so that starting/stopping affects the replacement train
	incoming = new_chain;

	if (remainder_chain && tv->IsSetKeepRemainingVehicles()) {
		BreakUpRemainders(remainder_chain);
	} else if (remainder_chain) {
		buy.AddCost(DoCommand(tile, remainder_chain->index | (1 << 20), 0, flags, CMD_SELL_VEHICLE));
	}

	/* Redraw main gui for changed statistics */
	SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);

	return buy;
}

void TrainRoadVehicleCrashBreakdown(Vehicle *v)
{
	Train *t = Train::From(v)->First();
	t->breakdown_ctr = 2;
	SetBit(t->flags, VRF_CONSIST_BREAKDOWN);
	t->breakdown_delay = 255;
	t->breakdown_type = BREAKDOWN_RV_CRASH;
	t->breakdown_severity = 0;
	t->reliability = 0;
}

void TrainBrakesOverheatedBreakdown(Vehicle *v, int speed, int max_speed)
{
	if (v->type != VEH_TRAIN) return;
	Train *t = Train::From(v)->First();
	if (t->breakdown_ctr != 0 || (t->vehstatus & VS_CRASHED)) return;

	if (unlikely(HasBit(_misc_debug_flags, MDF_OVERHEAT_BREAKDOWN_OPEN_WIN)) && !IsHeadless()) {
		ShowVehicleViewWindow(t);
	}

	t->crash_anim_pos = static_cast<uint16_t>(std::min<uint>(1500, t->crash_anim_pos + Clamp(((speed - max_speed) * speed) / 2, 0, 500)));
	if (t->crash_anim_pos < 1500) return;

	t->breakdown_ctr = 2;
	SetBit(t->flags, VRF_CONSIST_BREAKDOWN);
	t->breakdown_delay = 255;
	t->breakdown_type = BREAKDOWN_BRAKE_OVERHEAT;
	t->breakdown_severity = 0;
}

int GetTrainRealisticAccelerationAtSpeed(const int speed, const int mass, const uint32_t cached_power, const uint32_t max_te, const uint32_t air_drag, const RailType railtype)
{
	const int64_t power = cached_power * 746ll;
	int64_t resistance = 0;

	const bool maglev = (GetRailTypeInfo(railtype)->acceleration_type == 2);

	if (!maglev) {
		/* Static resistance plus rolling friction. */
		resistance = 10 * mass;
		resistance += (int64_t)mass * (int64_t)(15 * (512 + speed) / 512);
	}

	const int area = 14;

	resistance += (area * air_drag * speed * speed) / 1000;

	int64_t force;

	if (speed > 0) {
		if (!maglev) {
			/* Conversion factor from km/h to m/s is 5/18 to get [N] in the end. */
			force = power * 18 / (speed * 5);

			if (force > static_cast<int>(max_te)) {
				force = max_te;
			}
		} else {
			force = power / 25;
		}
	} else {
		force = (!maglev) ? std::min<uint64_t>(max_te, power) : power;
		force = std::max(force, (mass * 8) + resistance);
	}

	/* Easy way out when there is no acceleration. */
	if (force == resistance) return 0;

	int acceleration = ClampTo<int32_t>((force - resistance) / (mass * 4));
	acceleration = force < resistance ? std::min(-1, acceleration) : std::max(1, acceleration);

	return acceleration;
}

int GetTrainEstimatedMaxAchievableSpeed(const Train *train, int mass, const int speed_cap)
{
	int max_speed = 0;
	int acceleration;

	if (mass < 1) mass = 1;

	do
	{
		max_speed++;
		acceleration = GetTrainRealisticAccelerationAtSpeed(max_speed, mass, train->gcache.cached_power, train->gcache.cached_max_te, train->gcache.cached_air_drag, train->railtype);
	} while (acceleration > 0 && max_speed < speed_cap);

	return max_speed;
}

void SetSignalTrainAdaptationSpeed(const Train *v, TileIndex tile, uint16_t track)
{
	SignalSpeedKey speed_key = {};
	speed_key.signal_tile = tile;
	speed_key.signal_track = track;
	speed_key.last_passing_train_dir = v->GetVehicleTrackdir();

	SignalSpeedValue speed_value = {};
	speed_value.train_speed = v->First()->cur_speed;
	speed_value.time_stamp = GetSpeedRestrictionTimeout(v->First());

	_signal_speeds[speed_key] = speed_value;
}

static uint16_t GetTrainAdaptationSpeed(TileIndex tile, uint16_t track, Trackdir last_passing_train_dir)
{
	SignalSpeedKey speed_key = {
		speed_key.signal_tile = tile,
		speed_key.signal_track = track,
		speed_key.last_passing_train_dir = last_passing_train_dir
	};
	const auto found_speed_restriction = _signal_speeds.find(speed_key);

	if (found_speed_restriction != _signal_speeds.end()) {
		if (found_speed_restriction->second.IsOutOfDate()) {
			_signal_speeds.erase(found_speed_restriction);
			return 0;
		} else {
			return std::max<uint16_t>(25, found_speed_restriction->second.train_speed);
		}
	} else {
		return 0;
	}
}

void ApplySignalTrainAdaptationSpeed(Train *v, TileIndex tile, uint16_t track)
{
	uint16_t speed = GetTrainAdaptationSpeed(tile, track, v->GetVehicleTrackdir());

	if (speed > 0 && v->lookahead != nullptr) {
		for (const TrainReservationLookAheadItem &item : v->lookahead->items) {
			if (item.type == TRLIT_SPEED_ADAPTATION && item.end + 1 < v->lookahead->reservation_end_position) {
				uint16_t signal_speed = GetLowestSpeedTrainAdaptationSpeedAtSignal(item.data_id, item.data_aux);

				if (signal_speed == 0) {
					/* unrestricted signal ahead, disregard speed adaptation at earlier signal */
					v->UpdateTrainSpeedAdaptationLimit(0);
					return;
				}
				if (signal_speed > speed) {
					/* signal ahead with higher speed adaptation speed, override speed adaptation at earlier signal */
					speed = signal_speed;
				}
			}
		}
	}

	v->UpdateTrainSpeedAdaptationLimit(speed);
}

uint16_t GetLowestSpeedTrainAdaptationSpeedAtSignal(TileIndex tile, uint16_t track)
{
	uint16_t lowest_speed = 0;

	SignalSpeedKey speed_key = { tile, track, (Trackdir)0 };
	for (auto iter = _signal_speeds.lower_bound(speed_key); iter != _signal_speeds.end() && iter->first.signal_tile == tile && iter->first.signal_track == track;) {
		if (iter->second.IsOutOfDate()) {
			iter = _signal_speeds.erase(iter);
		} else {
			uint16_t adapt_speed = std::max<uint16_t>(25, iter->second.train_speed);
			if (lowest_speed == 0 || adapt_speed < lowest_speed) lowest_speed = adapt_speed;
			++iter;
		}
	}

	return lowest_speed;
}

uint16_t Train::GetMaxWeight() const
{
	uint16_t weight = CargoSpec::Get(this->cargo_type)->WeightOfNUnitsInTrain(this->GetEngine()->DetermineCapacity(this));

	/* Vehicle weight is not added for articulated parts. */
	if (!this->IsArticulatedPart()) {
		weight += GetVehicleProperty(this, PROP_TRAIN_WEIGHT, RailVehInfo(this->engine_type)->weight);
	}

	/* Powered wagons have extra weight added. */
	if (HasBit(this->flags, VRF_POWEREDWAGON)) {
		weight += RailVehInfo(this->gcache.first_engine)->pow_wag_weight;
	}

	return weight;
}

void Train::UpdateTrainSpeedAdaptationLimitInternal(uint16_t speed)
{
	this->signal_speed_restriction = speed;
	if (!HasBit(this->flags, VRF_SPEED_ADAPTATION_EXEMPT)) {
		SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
	}
}

/**
 * Set train speed restriction
 * @param tile unused
 * @param flags type of operation
 * @param p1 vehicle
 * @param p2 new speed restriction value
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSetTrainSpeedRestriction(TileIndex tile, DoCommandFlag flags, uint32_t p1, uint32_t p2, const char *text)
{
	Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || v->type != VEH_TRAIN || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(v);
	if (ret.Failed()) return ret;

	if (v->vehstatus & VS_CRASHED) return_cmd_error(STR_ERROR_VEHICLE_IS_DESTROYED);

	if (flags & DC_EXEC) {
		Train *t = Train::From(v);
		if (HasBit(t->flags, VRF_PENDING_SPEED_RESTRICTION)) {
			_pending_speed_restriction_change_map.erase(t->index);
			ClrBit(t->flags, VRF_PENDING_SPEED_RESTRICTION);
		}
		t->speed_restriction = (uint16_t)p2;

		SetWindowDirty(WC_VEHICLE_DETAILS, t->index);
	}
	return CommandCost();
}

bool Train::StopFoundAtVehiclePosition() const
{
	ChooseTrainTrackLookAheadState lookahead_state;
	VehicleOrderSaver orders(const_cast<Train *>(this));
	orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
	return HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND);
}
