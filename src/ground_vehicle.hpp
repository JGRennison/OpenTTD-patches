/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ground_vehicle.hpp Base class and functions for all vehicles that move through ground. */

#ifndef GROUND_VEHICLE_HPP
#define GROUND_VEHICLE_HPP

#include "vehicle_base.h"
#include "vehicle_gui.h"
#include "landscape.h"
#include "window_func.h"
#include "tunnel_map.h"

#include "widgets/vehicle_widget.h"

/** What is the status of our acceleration? */
enum AccelStatus : uint8_t {
	AS_ACCEL, ///< We want to go faster, if possible of course.
	AS_BRAKE, ///< We want to stop.
};

/**
 * Cached, frequently calculated values.
 * All of these values except cached_slope_resistance are set only for the first part of a vehicle.
 */
struct GroundVehicleCache {
	/* Cached acceleration values, recalculated when the cargo on a vehicle changes (in addition to the conditions below) */
	uint32_t cached_weight;           ///< Total weight of the consist (valid only for the first engine).
	uint32_t cached_slope_resistance; ///< Resistance caused by weight when this vehicle part is at a slope.
	uint32_t cached_max_te;           ///< Maximum tractive effort of consist (valid only for the first engine).
	uint16_t cached_axle_resistance;  ///< Resistance caused by the axles of the vehicle (valid only for the first engine).

	/* Cached acceleration values, recalculated on load and each time a vehicle is added to/removed from the consist. */
	uint16_t cached_max_track_speed;  ///< Maximum consist speed (in internal units) limited by track type (valid only for the first engine).
	uint32_t cached_power;            ///< Total power of the consist (valid only for the first engine).
	uint32_t cached_air_drag;         ///< Air drag coefficient of the vehicle (valid only for the first engine).

	/* Cached NewGRF values, recalculated on load and each time a vehicle is added to/removed from the consist. */
	uint16_t cached_total_length;     ///< Length of the whole vehicle (valid only for the first engine).
	EngineID first_engine;            ///< Cached EngineID of the front vehicle. INVALID_ENGINE for the front vehicle itself.
	uint8_t cached_veh_length;        ///< Length of this vehicle in units of 1/VEHICLE_LENGTH of normal length. It is cached because this can be set by a callback.

	/* Cached UI information. */
	uint16_t last_speed;              ///< The last speed we did display, so we only have to redraw when this changes.

	bool operator==(const GroundVehicleCache &) const = default;
};

/** Ground vehicle flags. */
enum GroundVehicleFlags : uint8_t {
	GVF_GOINGUP_BIT              = 0,  ///< Vehicle is currently going uphill. (Cached track information for acceleration)
	GVF_GOINGDOWN_BIT            = 1,  ///< Vehicle is currently going downhill. (Cached track information for acceleration)
	GVF_SUPPRESS_IMPLICIT_ORDERS = 2,  ///< Disable insertion and removal of automatic orders until the vehicle completes the real order.
	GVF_CHUNNEL_BIT              = 3,  ///< Vehicle may currently be in a chunnel. (Cached track information for inclination changes)
};

struct GroundVehicleAcceleration {
	int acceleration;
	int braking;
};

/**
 * Base class for all vehicles that move through ground.
 *
 * Child classes must define all of the following functions.
 * These functions are not defined as pure virtual functions at this class to improve performance.
 *
 * virtual uint16_t      GetPower() const = 0;
 * virtual uint16_t      GetPoweredPartPower(const T *head) const = 0;
 * virtual uint16_t      GetWeightWithoutCargo() const = 0;
 * virtual uint16_t      GetCargoWeight() const = 0;
 * virtual uint16_t      GetWeight() const = 0;
 * virtual uint8_t       GetTractiveEffort() const = 0;
 * virtual uint8_t       GetAirDrag() const = 0;
 * virtual uint8_t       GetAirDragArea() const = 0;
 * virtual AccelStatus   GetAccelerationStatus() const = 0;
 * virtual uint16_t      GetCurrentSpeed() const = 0;
 * virtual uint32_t      GetRollingFriction() const = 0;
 * virtual int           GetAccelerationType() const = 0;
 * virtual int32_t       GetSlopeSteepness() const = 0;
 * virtual int           GetDisplayMaxSpeed() const = 0;
 * virtual uint16_t      GetMaxTrackSpeed() const = 0;
 * virtual bool          TileMayHaveSlopedTrack() const = 0;
 */
template <class T, VehicleType Type>
struct GroundVehicle : public SpecializedVehicle<T, Type> {
	GroundVehicleCache gcache; ///< Cache of often calculated values.
	uint16_t gv_flags;         ///< @see GroundVehicleFlags.

	typedef GroundVehicle<T, Type> GroundVehicleBase; ///< Our type

	/**
	 * The constructor at SpecializedVehicle must be called.
	 */
	GroundVehicle() : SpecializedVehicle<T, Type>() {}

	void PowerChanged();
	void CargoChanged();
	bool IsChainInDepot() const override;

	void CalculatePower(uint32_t &power, uint32_t &max_te, bool breakdowns) const;

	GroundVehicleAcceleration GetAcceleration();

	/**
	 * Common code executed for crashed ground vehicles
	 * @param flooded was this vehicle flooded?
	 * @return number of victims
	 */
	uint Crash(bool flooded) override
	{
		/* Crashed vehicles aren't going up or down */
		for (T *v = T::From(this); v != nullptr; v = v->Next()) {
			ClrBit(v->gv_flags, GVF_GOINGUP_BIT);
			ClrBit(v->gv_flags, GVF_GOINGDOWN_BIT);
		}
		return this->Vehicle::Crash(flooded);
	}

	/**
	 * Calculates the total slope resistance for this vehicle.
	 * @return Slope resistance.
	 */
	inline int64_t GetSlopeResistance()
	{
		if (likely(HasBit(this->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST))) return 0;

		int64_t incl = 0;
		bool zero_slope_resist = true;

		for (const T *u = T::From(this); u != nullptr; u = u->Next()) {
			if (HasBit(u->gv_flags, GVF_GOINGUP_BIT)) {
				incl += u->gcache.cached_slope_resistance;
			} else if (HasBit(u->gv_flags, GVF_GOINGDOWN_BIT)) {
				incl -= u->gcache.cached_slope_resistance;
			}
			if (incl != 0) zero_slope_resist = false;
		}
		AssignBit(this->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST, zero_slope_resist);

		return incl;
	}

	/**
	 * Updates vehicle's Z position and inclination.
	 * Used when the vehicle entered given tile.
	 * @pre The vehicle has to be at (or near to) a border of the tile,
	 *      directed towards tile centre
	 */
	inline void UpdateZPositionAndInclination()
	{
		this->z_pos = GetSlopePixelZ(this->x_pos, this->y_pos, true);
		ClrBit(this->gv_flags, GVF_GOINGUP_BIT);
		ClrBit(this->gv_flags, GVF_GOINGDOWN_BIT);

		if (T::From(this)->TileMayHaveSlopedTrack()) {
			/* To check whether the current tile is sloped, and in which
			 * direction it is sloped, we get the 'z' at the center of
			 * the tile (middle_z) and the edge of the tile (old_z),
			 * which we then can compare. */
			int middle_z = GetSlopePixelZ((this->x_pos & ~TILE_UNIT_MASK) | (TILE_SIZE / 2), (this->y_pos & ~TILE_UNIT_MASK) | (TILE_SIZE / 2), true);

			if (middle_z != this->z_pos) {
				SetBit(this->gv_flags, (middle_z > this->z_pos) ? GVF_GOINGUP_BIT : GVF_GOINGDOWN_BIT);
				ClrBit(this->First()->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);
			}
		}
	}

	/**
	 * Updates vehicle's Z position.
	 * Inclination can't change in the middle of a tile.
	 * The faster code is used for trains and road vehicles unless they are
	 * reversing on a sloped tile.
	 */
	inline void UpdateZPosition()
	{
#if 0
		/* The following code does this: */

		if (HasBit(this->gv_flags, GVF_GOINGUP_BIT)) {
			switch (this->direction) {
				case DIR_NE:
					this->z_pos += (this->x_pos & 1) ^ 1; break;
				case DIR_SW:
					this->z_pos += (this->x_pos & 1); break;
				case DIR_NW:
					this->z_pos += (this->y_pos & 1) ^ 1; break;
				case DIR_SE:
					this->z_pos += (this->y_pos & 1); break;
				default: break;
			}
		} else if (HasBit(this->gv_flags, GVF_GOINGDOWN_BIT)) {
			switch (this->direction) {
				case DIR_NE:
					this->z_pos -= (this->x_pos & 1) ^ 1; break;
				case DIR_SW:
					this->z_pos -= (this->x_pos & 1); break;
				case DIR_NW:
					this->z_pos -= (this->y_pos & 1) ^ 1; break;
				case DIR_SE:
					this->z_pos -= (this->y_pos & 1); break;
				default: break;
			}
		}

		/* But gcc 4.4.5 isn't able to nicely optimise it, and the resulting
		 * code is full of conditional jumps. */
#endif

		/* Vehicle's Z position can change only if it has GVF_GOINGUP_BIT or GVF_GOINGDOWN_BIT set.
		 * Furthermore, if this function is called once every time the vehicle's position changes,
		 * we know the Z position changes by +/-1 at certain moments - when x_pos, y_pos is odd/even,
		 * depending on orientation of the slope and vehicle's direction */

		if (HasBit(this->gv_flags, GVF_GOINGUP_BIT) || HasBit(this->gv_flags, GVF_GOINGDOWN_BIT)) {
			if (T::From(this)->HasToUseGetSlopePixelZ()) {
				/* In some cases, we have to use GetSlopePixelZ() */
				this->z_pos = GetSlopePixelZ(this->x_pos, this->y_pos, true);
				return;
			}
			/* DirToDiagDir() is a simple right shift */
			DiagDirection dir = DirToDiagDir(this->direction);
			/* Read variables, so the compiler knows the access doesn't trap */
			int8_t x_pos = this->x_pos;
			int8_t y_pos = this->y_pos;
			/* DiagDirToAxis() is a simple mask */
			int8_t d = DiagDirToAxis(dir) == AXIS_X ? x_pos : y_pos;
			/* We need only the least significant bit */
			d &= 1;
			d ^= (int8_t)(dir == DIAGDIR_NW || dir == DIAGDIR_NE);
			/* Subtraction instead of addition because we are testing for GVF_GOINGUP_BIT.
			 * GVF_GOINGUP_BIT is used because it's bit 0, so simple AND can be used,
			 * without any shift */
			this->z_pos += HasBit(this->gv_flags, GVF_GOINGUP_BIT) ? d : -d;
		}

#ifdef _DEBUG
		assert(this->z_pos == GetSlopePixelZ(this->x_pos, this->y_pos, true));
#endif

		if (HasBit(this->gv_flags, GVF_CHUNNEL_BIT) && !IsTunnelTile(this->tile)) {
			ClrBit(this->gv_flags, GVF_CHUNNEL_BIT);
		}
	}

	void UpdateZPositionInWormhole();

	/**
	 * Checks if the vehicle is in a slope and sets the required flags in that case.
	 * @param new_tile True if the vehicle reached a new tile.
	 * @param update_delta Indicates to also update the delta.
	 * @return Old height of the vehicle.
	 */
	inline int UpdateInclination(bool new_tile, bool update_delta, bool in_wormhole = false)
	{
		int old_z = this->z_pos;

		if (in_wormhole) {
			if (HasBit(this->gv_flags, GVF_CHUNNEL_BIT)) this->UpdateZPositionInWormhole();
		} else if (new_tile) {
			this->UpdateZPositionAndInclination();
		} else {
			this->UpdateZPosition();
		}

		this->UpdateViewport(true, update_delta);
		return old_z;
	}

	/**
	 * Set front engine state.
	 */
	inline void SetFrontEngine() { SetBit(this->subtype, GVSF_FRONT); }

	/**
	 * Remove the front engine state.
	 */
	inline void ClearFrontEngine() { ClrBit(this->subtype, GVSF_FRONT); }

	/**
	 * Set a vehicle to be an articulated part.
	 */
	inline void SetArticulatedPart() { SetBit(this->subtype, GVSF_ARTICULATED_PART); }

	/**
	 * Clear a vehicle from being an articulated part.
	 */
	inline void ClearArticulatedPart() { ClrBit(this->subtype, GVSF_ARTICULATED_PART); }

	/**
	 * Set a vehicle to be a wagon.
	 */
	inline void SetWagon() { SetBit(this->subtype, GVSF_WAGON); }

	/**
	 * Clear wagon property.
	 */
	inline void ClearWagon() { ClrBit(this->subtype, GVSF_WAGON); }

	/**
	 * Set engine status.
	 */
	inline void SetEngine() { SetBit(this->subtype, GVSF_ENGINE); }

	/**
	 * Clear engine status.
	 */
	inline void ClearEngine() { ClrBit(this->subtype, GVSF_ENGINE); }

	/**
	 * Set a vehicle as a free wagon.
	 */
	inline void SetFreeWagon() { SetBit(this->subtype, GVSF_FREE_WAGON); }

	/**
	 * Clear a vehicle from being a free wagon.
	 */
	inline void ClearFreeWagon() { ClrBit(this->subtype, GVSF_FREE_WAGON); }

	/**
	 * Set a vehicle as a virtual vehicle.
	 */
	inline void SetVirtual() { SetBit(this->subtype, GVSF_VIRTUAL); }

	/**
	 * Clear a vehicle from being a virtual vehicle.
	 */
	inline void ClearVirtual() { ClrBit(this->subtype, GVSF_VIRTUAL); }

	/**
	 * Set a vehicle as a multiheaded engine.
	 */
	inline void SetMultiheaded() { SetBit(this->subtype, GVSF_MULTIHEADED); }

	/**
	 * Clear multiheaded engine property.
	 */
	inline void ClearMultiheaded() { ClrBit(this->subtype, GVSF_MULTIHEADED); }

	/**
	 * Check if the vehicle is a free wagon (got no engine in front of it).
	 * @return Returns true if the vehicle is a free wagon.
	 */
	inline bool IsFreeWagon() const { return HasBit(this->subtype, GVSF_FREE_WAGON); }

	/**
	 * Check if a vehicle is an engine (can be first in a consist).
	 * @return Returns true if vehicle is an engine.
	 */
	inline bool IsEngine() const { return HasBit(this->subtype, GVSF_ENGINE); }

	/**
	 * Check if a vehicle is a wagon.
	 * @return Returns true if vehicle is a wagon.
	 */
	inline bool IsWagon() const { return HasBit(this->subtype, GVSF_WAGON); }

	/**
	 * Check if the vehicle is a multiheaded engine.
	 * @return Returns true if the vehicle is a multiheaded engine.
	 */
	inline bool IsMultiheaded() const { return HasBit(this->subtype, GVSF_MULTIHEADED); }

	/**
	* Tell if we are dealing with a virtual vehicle (used for templates).
	* @return True if the vehicle is a virtual vehicle.
	*/
	inline bool IsVirtual() const { return HasBit(this->subtype, GVSF_VIRTUAL); }

	/**
	 * Tell if we are dealing with the rear end of a multiheaded engine.
	 * @return True if the engine is the rear part of a dualheaded engine.
	 */
	inline bool IsRearDualheaded() const { return this->IsMultiheaded() && !this->IsEngine(); }

	/**
	 * Check if the vehicle is a front engine.
	 * @return Returns true if the vehicle is a front engine.
	 */
	inline bool IsFrontEngine() const
	{
		return HasBit(this->subtype, GVSF_FRONT);
	}

	/**
	 * Check if the vehicle is an articulated part of an engine.
	 * @return Returns true if the vehicle is an articulated part.
	 */
	inline bool IsArticulatedPart() const
	{
		return HasBit(this->subtype, GVSF_ARTICULATED_PART);
	}

	/**
	 * Update the GUI variant of the current speed of the vehicle.
	 * Also mark the widget dirty when that is needed, i.e. when
	 * the speed of this vehicle has changed.
	 */
	inline void SetLastSpeed()
	{
		if (this->cur_speed != this->gcache.last_speed) {
			SetWindowWidgetDirty(WC_VEHICLE_VIEW, this->index, WID_VV_START_STOP);
			this->gcache.last_speed = this->cur_speed;
			if (HasBit(this->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE)) {
				this->RefreshImageCacheOfChain();
			}
		}
	}

	/**
	 * Refresh cached image of all vehicles in the chain (after the current vehicle)
	 */
	inline void RefreshImageCacheOfChain()
	{
		ClrBit(this->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE);
		ClrBit(this->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER);
		for (Vehicle *u = this; u != nullptr; u = u->Next()) {
			SetBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH_NEXT);
		}
	}

protected:
	uint DoUpdateSpeed(GroundVehicleAcceleration accel, int min_speed, int max_speed, int advisory_max_speed, bool use_realistic_braking);
};

#endif /* GROUND_VEHICLE_HPP */
