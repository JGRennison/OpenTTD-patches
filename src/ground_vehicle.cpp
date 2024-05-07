/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ground_vehicle.cpp Implementation of GroundVehicle. */

#include "stdafx.h"
#include "train.h"
#include "roadveh.h"
#include "depot_map.h"
#include "tunnel_base.h"
#include "slope_type.h"
#include "company_func.h"
#include "vehicle_func.h"

#include "safeguards.h"

/**
 * Recalculates the cached total power of a vehicle. Should be called when the consist is changed.
 */
template <class T, VehicleType Type>
void GroundVehicle<T, Type>::PowerChanged()
{
	assert(this->First() == this);
	const T *v = T::From(this);

	uint32_t total_power = 0;
	uint32_t max_te = 0;
	uint32_t number_of_parts = 0;
	uint16_t max_track_speed = this->vcache.cached_max_speed; // Max track speed in internal units.

	this->CalculatePower(total_power, max_te, false);

	for (const T *u = v; u != nullptr; u = u->Next()) {
		number_of_parts++;

		/* Get minimum max speed for this track. */
		uint16_t track_speed = u->GetMaxTrackSpeed();
		if (track_speed > 0) max_track_speed = std::min(max_track_speed, track_speed);
	}

	uint8_t air_drag;
	uint8_t air_drag_value = v->GetAirDrag();

	/* If air drag is set to zero (default), the resulting air drag coefficient is dependent on max speed. */
	if (air_drag_value == 0) {
		uint16_t max_speed = v->GetDisplayMaxSpeed();
		/* Simplification of the method used in TTDPatch. It uses <= 10 to change more steadily from 128 to 196. */
		air_drag = (max_speed <= 10) ? 192 : std::max(2048 / max_speed, 1);
	} else {
		/* According to the specs, a value of 0x01 in the air drag property means "no air drag". */
		air_drag = (air_drag_value == 1) ? 0 : air_drag_value;
	}

	this->gcache.cached_air_drag = air_drag + 3 * air_drag * number_of_parts / 20;

	if (this->gcache.cached_power != total_power || this->gcache.cached_max_te != max_te) {
		/* Stop the vehicle if it has no power. */
		if (total_power == 0) this->vehstatus |= VS_STOPPED;

		this->gcache.cached_power = total_power;
		this->gcache.cached_max_te = max_te;
		SetWindowDirty(WC_VEHICLE_DETAILS, this->index);
		SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
	}

	this->gcache.cached_max_track_speed = max_track_speed;
}

template <class T, VehicleType Type>
void GroundVehicle<T, Type>::CalculatePower(uint32_t &total_power, uint32_t &max_te, bool breakdowns) const {

	total_power = 0;
	max_te = 0;

	const T *v = T::From(this);

	for (const T *u = v; u != nullptr; u = u->Next()) {
		uint32_t current_power = u->GetPower() + u->GetPoweredPartPower(u);

		if (breakdowns && u->breakdown_ctr == 1 && u->breakdown_type == BREAKDOWN_LOW_POWER) {
			current_power = current_power * u->breakdown_severity / 256;
		}

		total_power += current_power;

		/* Only powered parts add tractive effort. */
		if (current_power > 0) max_te += u->GetWeight() * u->GetTractiveEffort();
	}

	max_te *= GROUND_ACCELERATION; // Tractive effort in (tonnes * 1000 * 9.8 =) N.
	max_te /= 256;  // Tractive effort is a [0-255] coefficient.
}

/**
 * Recalculates the cached weight of a vehicle and its parts. Should be called each time the cargo on
 * the consist changes.
 */
template <class T, VehicleType Type>
void GroundVehicle<T, Type>::CargoChanged()
{
	assert(this->First() == this);
	uint32_t weight = 0;
	uint64_t mass_offset = 0;
	uint32_t veh_offset = 0;
	uint16_t articulated_weight = 0;

	for (T *u = T::From(this); u != nullptr; u = u->Next()) {
		uint32_t current_weight = u->GetCargoWeight();
		if (u->IsArticulatedPart()) {
			current_weight += articulated_weight;
		} else {
			uint16_t engine_weight = u->GetWeightWithoutCargo();
			uint part_count = u->GetEnginePartsCount();
			articulated_weight = engine_weight / part_count;
			current_weight += articulated_weight + (engine_weight % part_count);
		}
		if (Type == VEH_TRAIN) {
			Train::From(u)->tcache.cached_veh_weight = current_weight;
			mass_offset += current_weight * (veh_offset + (Train::From(u)->gcache.cached_veh_length / 2));
			veh_offset += Train::From(u)->gcache.cached_veh_length;
		}
		weight += current_weight;
		/* Slope steepness is in percent, result in N. */
		u->gcache.cached_slope_resistance = current_weight * u->GetSlopeSteepness() * 100;
		u->InvalidateImageCache();
	}
	ClrBit(this->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);
	if (Type == VEH_TRAIN) {
		Train::From(this)->tcache.cached_centre_mass = (weight != 0) ? (mass_offset / weight) : (this->gcache.cached_total_length / 2);
	}

	/* Store consist weight in cache. */
	this->gcache.cached_weight = std::max(1u, weight);
	/* Friction in bearings and other mechanical parts is 0.1% of the weight (result in N). */
	this->gcache.cached_axle_resistance = 10 * weight;

	/* Now update vehicle power (tractive effort is dependent on weight). */
	this->PowerChanged();
}

/**
 * Calculates the acceleration of the vehicle under its current conditions.
 * @return Current upper and lower bounds of acceleration of the vehicle.
 */
template <class T, VehicleType Type>
GroundVehicleAcceleration GroundVehicle<T, Type>::GetAcceleration()
{
	/* Templated class used for function calls for performance reasons. */
	const T *v = T::From(this);
	/* Speed is used squared later on, so U16 * U16, and then multiplied by other values. */
	int64_t speed = v->GetCurrentSpeed(); // [km/h-ish]

	/* Weight is stored in tonnes. */
	int32_t mass = this->gcache.cached_weight;

	/* Power is stored in HP, we need it in watts.
	 * Each vehicle can have U16 power, 128 vehicles, HP -> watt
	 * and km/h to m/s conversion below result in a maximum of
	 * about 1.1E11, way more than 4.3E9 of int32. */
	int64_t power = this->gcache.cached_power * 746ll;

	/* This is constructed from:
	 *  - axle resistance:  U16 power * 10 for 128 vehicles.
	 *     * 8.3E7
	 *  - rolling friction: U16 power * 144 for 128 vehicles.
	 *     * 1.2E9
	 *  - slope resistance: U16 weight * 100 * 10 (steepness) for 128 vehicles.
	 *     * 8.4E9
	 *  - air drag: 28 * (U8 drag + 3 * U8 drag * 128 vehicles / 20) * U16 speed * U16 speed
	 *     * 6.2E14 before dividing by 1000
	 * Sum is 6.3E11, more than 4.3E9 of int32_t, so int64_t is needed.
	 */
	int64_t resistance = 0;

	int acceleration_type = v->GetAccelerationType();
	bool maglev = (acceleration_type == 2);

	const int area = v->GetAirDragArea();
	if (!maglev) {
		/* Static resistance plus rolling friction. */
		resistance = this->gcache.cached_axle_resistance;
		resistance += mass * v->GetRollingFriction();
	}
	/* Air drag; the air drag coefficient is in an arbitrary NewGRF-unit,
	 * so we need some magic conversion factor. */
	resistance += static_cast<int64_t>(area) * this->gcache.cached_air_drag * speed * speed / 1000;

	resistance += this->GetSlopeResistance();

	/* This value allows to know if the vehicle is accelerating or braking. */
	AccelStatus mode = v->GetAccelerationStatus();

	int braking_power = power;

	/* handle breakdown power reduction */
	uint32_t max_te = this->gcache.cached_max_te; // [N]
	if (Type == VEH_TRAIN && mode == AS_ACCEL && HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER)) {
		/* We'd like to cache this, but changing cached_power has too many unwanted side-effects */
		uint32_t power_temp;
		this->CalculatePower(power_temp, max_te, true);
		power = power_temp * 746ll;
	}


	/* Constructued from power, with need to multiply by 18 and assuming
	 * low speed, it needs to be a 64 bit integer too. */
	int64_t force;
	int64_t braking_force;
	if (speed > 0) {
		if (!maglev) {
			/* Conversion factor from km/h to m/s is 5/18 to get [N] in the end. */
			force = power * 18 / (speed * 5);
			braking_force = force;
			if (mode == AS_ACCEL && force > (int)max_te) force = max_te;
		} else {
			force = power / 25;
			braking_force = force;
		}
	} else {
		/* "Kickoff" acceleration. */
		force = (mode == AS_ACCEL && !maglev) ? std::min<uint64_t>(max_te, power) : power;
		force = std::max(force, (mass * 8) + resistance);
		braking_force = force;
	}

	if (Type == VEH_TRAIN && Train::From(this)->UsingRealisticBraking()) {
		braking_power += (Train::From(this)->tcache.cached_braking_length * (int64_t)RBC_BRAKE_POWER_PER_LENGTH);
	}

	/* If power is 0 because of a breakdown, we make the force 0 if accelerating */
	if (Type == VEH_TRAIN && mode == AS_ACCEL && HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER) && power == 0) {
		force = 0;
	}

	if (power != braking_power) {
		if (!maglev && speed > 0) {
			/* Conversion factor from km/h to m/s is 5/18 to get [N] in the end. */
			braking_force = braking_power * 18 / (speed * 5);
		} else {
			braking_force = braking_power / 25;
		}
	}

	/* Calculate the breakdown chance */
	if (_settings_game.vehicle.improved_breakdowns) {
		assert(this->gcache.cached_max_track_speed > 0);
		/** First, calculate (resistance / force * current speed / max speed) << 16.
		 * This yields a number x on a 0-1 scale, but shifted 16 bits to the left.
		 * We then calculate 64 + 128x, clamped to 0-255, but still shifted 16 bits to the left.
		 * Then we apply a correction for multiengine trains, and in the end we shift it 16 bits to the right to get a 0-255 number.
		 * @note A seperate correction for multiheaded engines is done in CheckVehicleBreakdown. We can't do that here because it would affect the whole consist.
		 */
		uint64_t breakdown_factor = (uint64_t)abs(resistance) * (uint64_t)(this->cur_speed << 16);
		breakdown_factor /= (std::max(force, (int64_t)100) * this->gcache.cached_max_track_speed);
		breakdown_factor = std::min<uint64_t>((64 << 16) + (breakdown_factor * 128), 255 << 16);
		if (Type == VEH_TRAIN && Train::From(this)->tcache.cached_num_engines > 1) {
			/* For multiengine trains, breakdown chance is multiplied by 3 / (num_engines + 2) */
			breakdown_factor *= 3;
			breakdown_factor /= (Train::From(this)->tcache.cached_num_engines + 2);
		}
		/* breakdown_chance is at least 5 (5 / 128 = ~4% of the normal chance) */
		this->breakdown_chance_factor = Clamp<uint64_t>(breakdown_factor >> 16, 5, 255);
	}

	int braking_accel;
	if (Type == VEH_TRAIN && Train::From(this)->UsingRealisticBraking()) {
		/* Assume that every part of a train is braked, not just the engine.
		 * Exceptionally heavy freight trains should still have a sensible braking distance.
		 * The total braking force is generally larger than the total tractive force. */
		braking_accel = ClampTo<int32_t>((-braking_force - resistance - (Train::From(this)->tcache.cached_braking_length * (int64_t)RBC_BRAKE_FORCE_PER_LENGTH)) / (mass * 4));

		/* Defensive driving: prevent ridiculously fast deceleration.
		 * -130 corresponds to a braking distance of about 6.2 tiles from 160 km/h. */
		braking_accel = std::max(braking_accel, -(GetTrainRealisticBrakingTargetDecelerationLimit(acceleration_type) + 10));
	} else {
		braking_accel = ClampTo<int32_t>(std::min<int64_t>(-braking_force - resistance, -10000) / mass);
	}

	if (mode == AS_ACCEL) {
		/* Easy way out when there is no acceleration. */
		if (force == resistance) return { 0, braking_accel };

		/* When we accelerate, make sure we always keep doing that, even when
		 * the excess force is more than the mass. Otherwise a vehicle going
		 * down hill will never slow down enough, and a vehicle that came up
		 * a hill will never speed up enough to (eventually) get back to the
		 * same (maximum) speed. */
		int accel = ClampTo<int32_t>((force - resistance) / (mass * 4));
		accel = force < resistance ? std::min(-1, accel) : std::max(1, accel);
		if (this->type == VEH_TRAIN) {
			if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL &&
					HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER)) {
				/* We need to apply the power reducation for non-realistic acceleration here */
				uint32_t power;
				CalculatePower(power, max_te, true);
				accel = accel * power / this->gcache.cached_power;
				accel -= this->acceleration >> 1;
			}

			if (this->cur_speed < 3 && accel < 5 &&
					this->IsFrontEngine() && !(this->current_order_time & 0x3FF) &&
					!(this->current_order.IsType(OT_LOADING)) &&
					!(Train::From(this)->flags & (VRF_IS_BROKEN | (1 << VRF_TRAIN_STUCK))) &&
					this->owner == _local_company) {
				ShowTrainTooHeavyAdviceMessage(this);
			}

			if (Train::From(this)->UsingRealisticBraking() && _settings_game.vehicle.limit_train_acceleration) {
				accel = std::min(accel, 250);
			}
		}

		return { accel, braking_accel };
	} else {
		return { braking_accel, braking_accel };
	}
}

/**
 * Check whether the whole vehicle chain is in the depot.
 * @return true if and only if the whole chain is in the depot.
 */
template <class T, VehicleType Type>
bool GroundVehicle<T, Type>::IsChainInDepot() const
{
	const T *v = this->First();
	/* Is the front engine stationary in the depot? */
	static_assert((int)TRANSPORT_RAIL == (int)VEH_TRAIN);
	static_assert((int)TRANSPORT_ROAD == (int)VEH_ROAD);
	if (!IsDepotTypeTile(v->tile, (TransportType)Type) || v->cur_speed != 0) return false;

	/* Check whether the rest is also already trying to enter the depot. */
	for (; v != nullptr; v = v->Next()) {
		if (!v->T::IsInDepot() || v->tile != this->tile) return false;
	}

	return true;
}

/**
 * Updates vehicle's Z inclination inside a wormhole, where applicable.
 */
template <class T, VehicleType Type>
void GroundVehicle<T, Type>::UpdateZPositionInWormhole()
{
	if (!IsTunnel(this->tile)) return;

	const Tunnel *t = Tunnel::GetByTile(this->tile);
	if (!t->is_chunnel) return;

	TileIndex pos_tile = TileVirtXY(this->x_pos, this->y_pos);

	ClrBit(this->gv_flags, GVF_GOINGUP_BIT);
	ClrBit(this->gv_flags, GVF_GOINGDOWN_BIT);

	if (pos_tile == t->tile_n || pos_tile == t->tile_s) {
		this->z_pos = 0;
		return;
	}

	int north_coord, south_coord, pos_coord;
	bool going_north;
	Slope slope_north;
	if (t->tile_s - t->tile_n > MapMaxX()) {
		// tunnel extends along Y axis (DIAGDIR_SE from north end), has same X values
		north_coord = TileY(t->tile_n);
		south_coord = TileY(t->tile_s);
		pos_coord = TileY(pos_tile);
		going_north = (this->direction == DIR_NW);
		slope_north = SLOPE_NW;
	} else {
		// tunnel extends along X axis (DIAGDIR_SW from north end), has same Y values
		north_coord = TileX(t->tile_n);
		south_coord = TileX(t->tile_s);
		pos_coord = TileX(pos_tile);
		going_north = (this->direction == DIR_NE);
		slope_north = SLOPE_NE;
	}

	Slope slope = SLOPE_FLAT;

	int delta;
	if ((delta = pos_coord - north_coord) <= 3) {
		this->z_pos = TILE_HEIGHT * (delta == 3 ? -2 : -1);
		if (delta != 2) {
			slope = slope_north;
			SetBit(this->gv_flags, going_north ? GVF_GOINGUP_BIT : GVF_GOINGDOWN_BIT);
			ClrBit(this->First()->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);
		}
	} else if ((delta = south_coord - pos_coord) <= 3) {
		this->z_pos = TILE_HEIGHT * (delta == 3 ? -2 : -1);
		if (delta != 2) {
			slope = SLOPE_ELEVATED ^ slope_north;
			SetBit(this->gv_flags, going_north ? GVF_GOINGDOWN_BIT : GVF_GOINGUP_BIT);
			ClrBit(this->First()->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);
		}
	}

	if (slope != SLOPE_FLAT) this->z_pos += GetPartialPixelZ(this->x_pos & 0xF, this->y_pos & 0xF, slope);
}

/* Instantiation for Train */
template struct GroundVehicle<Train, VEH_TRAIN>;
/* Instantiation for RoadVehicle */
template struct GroundVehicle<RoadVehicle, VEH_ROAD>;
