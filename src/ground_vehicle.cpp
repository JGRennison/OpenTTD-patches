/* $Id$ */

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

#include "safeguards.h"

/**
 * Recalculates the cached total power of a vehicle. Should be called when the consist is changed.
 */
template <class T, VehicleType Type>
void GroundVehicle<T, Type>::PowerChanged()
{
	assert(this->First() == this);
	const T *v = T::From(this);

	uint32 total_power = 0;
	uint32 max_te = 0;
	uint32 number_of_parts = 0;
	uint16 max_track_speed = v->GetDisplayMaxSpeed();

	this->CalculatePower(total_power, max_te, false);

	for (const T *u = v; u != NULL; u = u->Next()) {
		number_of_parts++;

		/* Get minimum max speed for this track. */
		uint16 track_speed = u->GetMaxTrackSpeed();
		if (track_speed > 0) max_track_speed = min(max_track_speed, track_speed);
	}

	byte air_drag;
	byte air_drag_value = v->GetAirDrag();

	/* If air drag is set to zero (default), the resulting air drag coefficient is dependent on max speed. */
	if (air_drag_value == 0) {
		uint16 max_speed = v->GetDisplayMaxSpeed();
		/* Simplification of the method used in TTDPatch. It uses <= 10 to change more steadily from 128 to 196. */
		air_drag = (max_speed <= 10) ? 192 : max(2048 / max_speed, 1);
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
void GroundVehicle<T, Type>::CalculatePower(uint32& total_power, uint32& max_te, bool breakdowns) const {

	total_power = 0;
	max_te = 0;

	const T *v = T::From(this);

	for (const T *u = v; u != NULL; u = u->Next()) {
		uint32 current_power = u->GetPower() + u->GetPoweredPartPower(u);

		if (breakdowns && u->breakdown_ctr == 1 && u->breakdown_type == BREAKDOWN_LOW_POWER) {
			current_power = current_power * u->breakdown_severity / 256;
		}

		total_power += current_power;

		/* Only powered parts add tractive effort. */
		if (current_power > 0) max_te += u->GetWeight() * u->GetTractiveEffort();
	}

	max_te *= 9800; // Tractive effort in (tonnes * 1000 * 9.8 =) N.
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
	uint32 weight = 0;

	for (T *u = T::From(this); u != NULL; u = u->Next()) {
		uint32 current_weight = u->GetWeight();
		weight += current_weight;
		/* Slope steepness is in percent, result in N. */
		u->gcache.cached_slope_resistance = current_weight * u->GetSlopeSteepness() * 100;
		u->cur_image_valid_dir = INVALID_DIR;
	}

	/* Store consist weight in cache. */
	this->gcache.cached_weight = max<uint32>(1, weight);
	/* Friction in bearings and other mechanical parts is 0.1% of the weight (result in N). */
	this->gcache.cached_axle_resistance = 10 * weight;

	/* Now update vehicle power (tractive effort is dependent on weight). */
	this->PowerChanged();
}

/**
 * Calculates the acceleration of the vehicle under its current conditions.
 * @return Current acceleration of the vehicle.
 */
template <class T, VehicleType Type>
int GroundVehicle<T, Type>::GetAcceleration()
{
	/* Templated class used for function calls for performance reasons. */
	const T *v = T::From(this);
	/* Speed is used squared later on, so U16 * U16, and then multiplied by other values. */
	int64 speed = v->GetCurrentSpeed(); // [km/h-ish]

	/* Weight is stored in tonnes. */
	int32 mass = this->gcache.cached_weight;

	/* Power is stored in HP, we need it in watts.
	 * Each vehicle can have U16 power, 128 vehicles, HP -> watt
	 * and km/h to m/s conversion below result in a maxium of
	 * about 1.1E11, way more than 4.3E9 of int32. */
	int64 power = this->gcache.cached_power * 746ll;

	/* This is constructed from:
	 *  - axle resistance:  U16 power * 10 for 128 vehicles.
	 *     * 8.3E7
	 *  - rolling friction: U16 power * 144 for 128 vehicles.
	 *     * 1.2E9
	 *  - slope resistance: U16 weight * 100 * 10 (steepness) for 128 vehicles.
	 *     * 8.4E9
	 *  - air drag: 28 * (U8 drag + 3 * U8 drag * 128 vehicles / 20) * U16 speed * U16 speed
	 *     * 6.2E14 before dividing by 1000
	 * Sum is 6.3E11, more than 4.3E9 of int32, so int64 is needed.
	 */
	int64 resistance = 0;

	bool maglev = v->GetAccelerationType() == 2;

	const int area = v->GetAirDragArea();
	if (!maglev) {
		/* Static resistance plus rolling friction. */
		resistance = this->gcache.cached_axle_resistance;
		resistance += mass * v->GetRollingFriction();
	}
	/* Air drag; the air drag coefficient is in an arbitrary NewGRF-unit,
	 * so we need some magic conversion factor. */
	resistance += (area * this->gcache.cached_air_drag * speed * speed) / 1000;

	resistance += this->GetSlopeResistance();

	/* This value allows to know if the vehicle is accelerating or braking. */
	AccelStatus mode = v->GetAccelerationStatus();

	/* handle breakdown power reduction */
	uint32 max_te = this->gcache.cached_max_te; // [N]
	if (Type == VEH_TRAIN && mode == AS_ACCEL && HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER)) {
		/* We'd like to cache this, but changing cached_power has too many unwanted side-effects */
		uint32 power_temp;
		this->CalculatePower(power_temp, max_te, true);
		power = power_temp * 746ll;
	}


	/* Constructued from power, with need to multiply by 18 and assuming
	 * low speed, it needs to be a 64 bit integer too. */
	int64 force;
	if (speed > 0) {
		if (!maglev) {
			/* Conversion factor from km/h to m/s is 5/18 to get [N] in the end. */
			force = power * 18 / (speed * 5);
			if (mode == AS_ACCEL && force > (int)max_te) force = max_te;
		} else {
			force = power / 25;
		}
	} else {
		/* "Kickoff" acceleration. */
		force = (mode == AS_ACCEL && !maglev) ? min(max_te, power) : power;
		force = max(force, (mass * 8) + resistance);
	}

	/* If power is 0 because of a breakdown, we make the force 0 if accelerating */
	if (Type == VEH_TRAIN && mode == AS_ACCEL && HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER) && power == 0) {
		force = 0;
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
		uint64 breakdown_factor = (uint64)abs(resistance) * (uint64)(this->cur_speed << 16);
		breakdown_factor /= (max(force, (int64)100) * this->gcache.cached_max_track_speed);
		breakdown_factor = min((64 << 16) + (breakdown_factor * 128), 255 << 16);
		if (Type == VEH_TRAIN && Train::From(this)->tcache.cached_num_engines > 1) {
			/* For multiengine trains, breakdown chance is multiplied by 3 / (num_engines + 2) */
			breakdown_factor *= 3;
			breakdown_factor /= (Train::From(this)->tcache.cached_num_engines + 2);
		}
		/* breakdown_chance is at least 5 (5 / 128 = ~4% of the normal chance) */
		this->breakdown_chance_factor = max(breakdown_factor >> 16, (uint64)5);
	}

	if (mode == AS_ACCEL) {
		/* Easy way out when there is no acceleration. */
		if (force == resistance) return 0;

		/* When we accelerate, make sure we always keep doing that, even when
		 * the excess force is more than the mass. Otherwise a vehicle going
		 * down hill will never slow down enough, and a vehicle that came up
		 * a hill will never speed up enough to (eventually) get back to the
		 * same (maximum) speed. */
		int accel = ClampToI32((force - resistance) / (mass * 4));
		accel = force < resistance ? min(-1, accel) : max(1, accel);
		if (this->type == VEH_TRAIN) {
			if(_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL &&
					HasBit(Train::From(this)->flags, VRF_BREAKDOWN_POWER)) {
				/* We need to apply the power reducation for non-realistic acceleration here */
				uint32 power;
				CalculatePower(power, max_te, true);
				accel = accel * power / this->gcache.cached_power;
				accel -= this->acceleration >> 1;
			}

			if (this->IsFrontEngine() && !(this->current_order_time & 0x1FF) &&
					!(this->current_order.IsType(OT_LOADING)) &&
					!(Train::From(this)->flags & (VRF_IS_BROKEN | (1 << VRF_TRAIN_STUCK))) &&
					this->cur_speed < 3 && accel < 5) {
				SetBit(Train::From(this)->flags, VRF_TOO_HEAVY);
			}
		}

		return accel;
	} else {
		return ClampToI32(min(-force - resistance, -10000) / mass);
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
	assert_compile((int)TRANSPORT_RAIL == (int)VEH_TRAIN);
	assert_compile((int)TRANSPORT_ROAD == (int)VEH_ROAD);
	if (!IsDepotTypeTile(v->tile, (TransportType)Type) || v->cur_speed != 0) return false;

	/* Check whether the rest is also already trying to enter the depot. */
	for (; v != NULL; v = v->Next()) {
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
		}
	} else if ((delta = south_coord - pos_coord) <= 3) {
		this->z_pos = TILE_HEIGHT * (delta == 3 ? -2 : -1);
		if (delta != 2) {
			slope = SLOPE_ELEVATED ^ slope_north;
			SetBit(this->gv_flags, going_north ? GVF_GOINGDOWN_BIT : GVF_GOINGUP_BIT);
		}
	}

	if (slope != SLOPE_FLAT) this->z_pos += GetPartialPixelZ(this->x_pos & 0xF, this->y_pos & 0xF, slope);
}

/* Instantiation for Train */
template struct GroundVehicle<Train, VEH_TRAIN>;
/* Instantiation for RoadVehicle */
template struct GroundVehicle<RoadVehicle, VEH_ROAD>;
