/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train.h Base for the train class. */

#ifndef TRAIN_H
#define TRAIN_H

#include "core/enum_type.hpp"

#include "newgrf_engine.h"
#include "cargotype.h"
#include "rail.h"
#include "engine_base.h"
#include "rail_map.h"
#include "ground_vehicle.hpp"
#include "pbs.h"

struct Train;

/** Rail vehicle flags. */
enum VehicleRailFlags {
	VRF_REVERSING                     = 0,
	VRF_WAITING_RESTRICTION           = 1, ///< Train is waiting due to a routing restriction, only valid when VRF_TRAIN_STUCK is also set.
	/* gap, was VRF_HAVE_SLOT */
	VRF_POWEREDWAGON                  = 3, ///< Wagon is powered.
	VRF_REVERSE_DIRECTION             = 4, ///< Reverse the visible direction of the vehicle.
	VRF_HAS_HIT_RV                    = 5, ///< Train has hit road vehicle
	VRF_EL_ENGINE_ALLOWED_NORMAL_RAIL = 6, ///< Electric train engine is allowed to run on normal rail. */
	VRF_TOGGLE_REVERSE                = 7, ///< Used for vehicle var 0xFE bit 8 (toggled each time the train is reversed, accurate for first vehicle only).
	VRF_TRAIN_STUCK                   = 8, ///< Train can't get a path reservation.
	VRF_LEAVING_STATION               = 9, ///< Train is just leaving a station.
	VRF_BREAKDOWN_BRAKING             = 10,///< used to mark a train that is braking because it is broken down
	VRF_BREAKDOWN_POWER               = 11,///< used to mark a train in which the power of one (or more) of the engines is reduced because of a breakdown
	VRF_BREAKDOWN_SPEED               = 12,///< used to mark a train that has a reduced maximum speed because of a breakdown
	VRF_BREAKDOWN_STOPPED             = 13,///< used to mark a train that is stopped because of a breakdown
	VRF_NEED_REPAIR                   = 14,///< used to mark a train that has a reduced maximum speed because of a critical breakdown
	VRF_BEYOND_PLATFORM_END           = 16,
	VRF_NOT_YET_IN_PLATFORM           = 17,
	VRF_ADVANCE_IN_PLATFORM           = 18,
	VRF_CONSIST_BREAKDOWN             = 19,///< one or more vehicles in this consist have a breakdown of some sort (breakdown_ctr != 0)
	VRF_CONSIST_SPEED_REDUCTION       = 20,///< one or more vehicles in this consist may be in a depot or on a bridge (may be false positive but not false negative)
	VRF_PENDING_SPEED_RESTRICTION     = 21,///< This vehicle has one or more pending speed restriction changes
	VRF_SPEED_ADAPTATION_EXEMPT       = 22,///< This vehicle is exempt from train speed adaptation

	VRF_IS_BROKEN = (1 << VRF_BREAKDOWN_POWER) | (1 << VRF_BREAKDOWN_SPEED) | (1 << VRF_BREAKDOWN_STOPPED), ///< Bitmask of all flags that indicate a broken train (braking is not included)
};

/** Modes for ignoring signals. */
enum TrainForceProceeding : uint8_t {
	TFP_NONE   = 0,    ///< Normal operation.
	TFP_STUCK  = 1,    ///< Proceed till next signal, but ignore being stuck till then. This includes force leaving depots.
	TFP_SIGNAL = 2,    ///< Ignore next signal, after the signal ignore being stuck.
};

/** Flags for Train::ConsistChanged */
enum ConsistChangeFlags {
	CCF_LENGTH     = 0x01,     ///< Allow vehicles to change length.
	CCF_CAPACITY   = 0x02,     ///< Allow vehicles to change capacity.

	CCF_TRACK      = 0,                          ///< Valid changes while vehicle is driving, and possibly changing tracks.
	CCF_LOADUNLOAD = 0,                          ///< Valid changes while vehicle is loading/unloading.
	CCF_AUTOREFIT  = CCF_CAPACITY,               ///< Valid changes for autorefitting in stations.
	CCF_REFIT      = CCF_LENGTH | CCF_CAPACITY,  ///< Valid changes for refitting in a depot.
	CCF_ARRANGE    = CCF_LENGTH | CCF_CAPACITY,  ///< Valid changes for arranging the consist in a depot.
	CCF_SAVELOAD   = CCF_LENGTH,                 ///< Valid changes when loading a savegame. (Everything that is not stored in the save.)
};
DECLARE_ENUM_AS_BIT_SET(ConsistChangeFlags)

enum RealisticBrakingConstants {
	RBC_BRAKE_FORCE_PER_LENGTH      = 2400,      ///< Additional force-based brake force per unit of train length
	RBC_BRAKE_POWER_PER_LENGTH      = 15000,     ///< Additional power-based brake force per unit of train length (excludes maglevs)
};

uint8_t FreightWagonMult(CargoID cargo);

void CheckTrainsLengths();

void FreeTrainTrackReservation(Train *v, TileIndex origin = INVALID_TILE, Trackdir orig_td = INVALID_TRACKDIR);
bool TryPathReserve(Train *v, bool mark_as_stuck = false, bool first_tile_okay = false);

void DeleteVisibleTrain(Train *v);

void CheckBreakdownFlags(Train *v);
void GetTrainSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type);

bool TrainOnCrossing(TileIndex tile);
void NormalizeTrainVehInDepot(const Train *u);

inline int GetTrainRealisticBrakingTargetDecelerationLimit(int acceleration_type)
{
	return 120 + (acceleration_type * 48);
}

/** Flags for TrainCache::cached_tflags */
enum TrainCacheFlags : uint8_t {
	TCF_NONE         = 0,        ///< No flags
	TCF_TILT         = 0x01,     ///< Train can tilt; feature provides a bonus in curves.
	TCF_RL_BRAKING   = 0x02,     ///< Train realistic braking (movement physics) in effect for this vehicle
	TCF_SPD_RAILTYPE = 0x04,     ///< Train speed varies depending on railtype
};
DECLARE_ENUM_AS_BIT_SET(TrainCacheFlags)

/** Variables that are cached to improve performance and such */
struct TrainCache {
	/* Cached wagon override spritegroup */
	const struct SpriteGroup *cached_override;

	/* cached values, recalculated on load and each time a vehicle is added to/removed from the consist. */
	TrainCacheFlags cached_tflags;  ///< train cached flags
	uint8_t cached_num_engines;     ///< total number of engines, including rear ends of multiheaded engines
	uint16_t cached_centre_mass;    ///< Cached position of the centre of mass, from the front
	uint16_t cached_braking_length; ///< Cached effective length used for deceleration force and power purposes
	uint16_t cached_veh_weight;     ///< Cached individual vehicle weight
	uint16_t cached_uncapped_decel; ///< Uncapped cached deceleration for realistic braking lookahead purposes
	uint8_t cached_deceleration;    ///< Cached deceleration for realistic braking lookahead purposes

	uint8_t user_def_data;          ///< Cached property 0x25. Can be set by Callback 0x36.

	int16_t cached_curve_speed_mod; ///< curve speed modifier of the entire train
	uint16_t cached_max_curve_speed; ///< max consist speed limited by curves

	bool operator==(const TrainCache &) const = default;
};

/**
 * 'Train' is either a loco or a wagon.
 */
struct Train final : public GroundVehicle<Train, VEH_TRAIN> {
	TrackBits track;
	RailType railtype;
	uint32_t flags;
	TrainCache tcache;

	/* Link between the two ends of a multiheaded engine */
	Train *other_multiheaded_part;

	std::unique_ptr<TrainReservationLookAhead> lookahead;

	RailTypes compatible_railtypes;

	TrainForceProceeding force_proceed;
	uint8_t critical_breakdown_count; ///< Counter for the number of critical breakdowns since last service

	/** Ticks waiting in front of a signal, ticks being stuck or a counter for forced proceeding through signals. */
	uint16_t wait_counter;

	uint16_t reverse_distance;
	uint16_t tunnel_bridge_signal_num;
	uint16_t speed_restriction;
	uint16_t signal_speed_restriction;
	uint16_t crash_anim_pos; ///< Crash animation counter, also used for realistic braking train brake overheating

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	Train() : GroundVehicleBase() {}
	/** We want to 'destruct' the right class. */
	virtual ~Train() { this->PreDestructor(); }

	friend struct GroundVehicle<Train, VEH_TRAIN>; // GroundVehicle needs to use the acceleration functions defined at Train.

	void MarkDirty() override;
	void UpdateDeltaXY() override;
	ExpensesType GetExpenseType(bool income) const override { return income ? EXPENSES_TRAIN_REVENUE : EXPENSES_TRAIN_RUN; }
	void PlayLeaveStationSound(bool force = false) const override;
	bool IsPrimaryVehicle() const override { return this->IsFrontEngine(); }
	void GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const override;
	int GetDisplaySpeed() const override { return this->gcache.last_speed; }
	int GetDisplayMaxSpeed() const override { return this->vcache.cached_max_speed; }
	Money GetRunningCost() const override;
	int GetCursorImageOffset() const;
	int GetDisplayImageWidth(Point *offset = nullptr) const;
	bool IsInDepot() const override { return this->track == TRACK_BIT_DEPOT; }
	bool Tick() override;
	void OnNewDay() override;
	void OnPeriodic() override;
	uint Crash(bool flooded = false) override;
	Money CalculateCurrentOverallValue() const;
	Trackdir GetVehicleTrackdir() const override;
	TileIndex GetOrderStationLocation(StationID station) override;
	ClosestDepot FindClosestDepot() override;

	void ReserveTrackUnderConsist() const;

	uint16_t GetCurveSpeedLimit() const;

	void ConsistChanged(ConsistChangeFlags allowed_changes);

	struct MaxSpeedInfo {
		int strict_max_speed;
		int advisory_max_speed;
	};

	int UpdateSpeed(MaxSpeedInfo max_speed_info);

	void UpdateAcceleration();

	bool ConsistNeedsRepair() const;

private:
	MaxSpeedInfo GetCurrentMaxSpeedInfoInternal(bool update_state) const;

public:
	MaxSpeedInfo GetCurrentMaxSpeedInfo() const
	{
		return this->GetCurrentMaxSpeedInfoInternal(false);
	}

	MaxSpeedInfo GetCurrentMaxSpeedInfoAndUpdate()
	{
		return this->GetCurrentMaxSpeedInfoInternal(true);
	}

	int GetCurrentMaxSpeed() const override;

	uint8_t GetZPosCacheUpdateInterval() const
	{
		return Clamp<uint16_t>(std::min<uint16_t>(this->gcache.cached_total_length / 4, this->tcache.cached_centre_mass / 2), 2, 32);
	}

	uint32_t CalculateOverallZPos() const;

	bool UsingRealisticBraking() const { return this->tcache.cached_tflags & TCF_RL_BRAKING; }

	/**
	 * Get the next real (non-articulated part and non rear part of dualheaded engine) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	inline Train *GetNextUnit() const
	{
		Train *v = this->GetNextVehicle();
		if (v != nullptr && v->IsRearDualheaded()) v = v->GetNextVehicle();

		return v;
	}

	/**
	 * Get the previous real (non-articulated part and non rear part of dualheaded engine) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	inline Train *GetPrevUnit()
	{
		Train *v = this->GetPrevVehicle();
		if (v != nullptr && v->IsRearDualheaded()) v = v->GetPrevVehicle();

		return v;
	}

	/* Get the last vehicle of a chain
	 * @return pointer the last vehicle in a chain
	 */
	inline Train *GetLastUnit() {
		Train *tmp = this;
		while (tmp->GetNextUnit()) {
			tmp = tmp->GetNextUnit();
		}
		return tmp;
	}

	/**
	 * Calculate the offset from this vehicle's center to the following center taking the vehicle lengths into account.
	 * @return Offset from center to center.
	 */
	int CalcNextVehicleOffset() const
	{
		/* For vehicles with odd lengths the part before the center will be one unit
		 * longer than the part after the center. This means we have to round up the
		 * length of the next vehicle but may not round the length of the current
		 * vehicle. */
		return this->gcache.cached_veh_length / 2 + (this->Next() != nullptr ? this->Next()->gcache.cached_veh_length + 1 : 0) / 2;
	}

	const Train *GetStationLoadingVehicle() const
	{
		const Train *v = this->First();
		while (v && HasBit(v->flags, VRF_BEYOND_PLATFORM_END)) v = v->Next();
		return v;
	}

	Train *GetStationLoadingVehicle()
	{
		return const_cast<Train *>(const_cast<const Train *>(this)->GetStationLoadingVehicle());
	}

	uint16_t GetCargoWeight(uint cargo_amount) const
	{
		if (cargo_amount > 0) {
			CargoSpec::Get(this->cargo_type)->WeightOfNUnitsInTrain(cargo_amount);
			return (CargoSpec::Get(this->cargo_type)->weight * cargo_amount * FreightWagonMult(this->cargo_type)) / 16;
		} else {
			return 0;
		}
	}

	/**
	 * Allows to know the weight value that this vehicle will use (excluding cargo).
	 * @return Weight value from the engine in tonnes.
	 */
	uint16_t GetWeightWithoutCargo() const
	{
		uint16_t weight = 0;

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

	/**
	 * Allows to know the weight value that this vehicle will use (cargo only).
	 * @return Weight value from the engine in tonnes.
	 */
	uint16_t GetCargoWeight() const
	{
		return this->GetCargoWeight(this->cargo.StoredCount());
	}

	/**
	 * Allows to know the acceleration type of a vehicle.
	 * @return Acceleration type of the vehicle.
	 */
	inline int GetAccelerationType() const
	{
		return GetRailTypeInfo(this->railtype)->acceleration_type;
	}

protected: // These functions should not be called outside acceleration code.
	/**
	 * Gets the speed a broken down train (low speed breakdown) is limited to.
	 * @note This value is not cached, because changing cached_max_speed would have unwanted consequences (e.g. in the GUI).
	 * @param v The front engine of the vehicle.
	 * @return The speed the train is limited to.
	 */
	inline uint16_t GetBreakdownSpeed() const
	{
		assert(this->IsFrontEngine());
		uint16_t speed = UINT16_MAX;

		for (const Train *w = this; w != nullptr; w = w->Next()) {
			if (w->breakdown_ctr == 1 && w->breakdown_type == BREAKDOWN_LOW_SPEED) {
				speed = std::min<uint16_t>(speed, w->breakdown_severity);
			}
		}
		return speed;
	}

	/**
	 * Allows to know the power value that this vehicle will use.
	 * @return Power value from the engine in HP, or zero if the vehicle is not powered.
	 */
	inline uint16_t GetPower() const
	{
		/* Power is not added for articulated parts */
		if (!this->IsArticulatedPart() && (this->IsVirtual() || HasPowerOnRail(this->railtype, GetRailTypeByTrackBit(this->tile, this->track)))) {
			uint16_t power = GetVehicleProperty(this, PROP_TRAIN_POWER, RailVehInfo(this->engine_type)->power);
			/* Halve power for multiheaded parts */
			if (this->IsMultiheaded()) power /= 2;
			return power;
		}

		return 0;
	}

	/**
	 * Returns a value if this articulated part is powered.
	 * @return Power value from the articulated part in HP, or zero if it is not powered.
	 */
	inline uint16_t GetPoweredPartPower(const Train *head) const
	{
		/* For powered wagons the engine defines the type of engine (i.e. railtype) */
		if (HasBit(this->flags, VRF_POWEREDWAGON) && (head->IsVirtual() || HasPowerOnRail(head->railtype, GetRailTypeByTrackBit(this->tile, this->track)))) {
			return RailVehInfo(this->gcache.first_engine)->pow_wag_power;
		}

		return 0;
	}

	/**
	 * Allows to know the weight value that this vehicle will use.
	 * @return Weight value from the engine in tonnes.
	 */
	inline uint16_t GetWeight() const
	{
		return this->GetWeightWithoutCargo() + this->GetCargoWeight();
	}

	/**
	 * Calculates the weight value that this vehicle will have when fully loaded with its current cargo.
	 * @return Weight value in tonnes.
	 */
	uint16_t GetMaxWeight() const override;

	/**
	 * Allows to know the tractive effort value that this vehicle will use.
	 * @return Tractive effort value from the engine.
	 */
	inline uint8_t GetTractiveEffort() const
	{
		return GetVehicleProperty(this, PROP_TRAIN_TRACTIVE_EFFORT, RailVehInfo(this->engine_type)->tractive_effort);
	}

	/**
	 * Gets the area used for calculating air drag.
	 * @return Area of the engine in m^2.
	 */
	inline uint8_t GetAirDragArea() const
	{
		/* Air drag is higher in tunnels due to the limited cross-section. */
		return (this->track & TRACK_BIT_WORMHOLE && this->vehstatus & VS_HIDDEN) ? 28 : 14;
	}

	/**
	 * Gets the air drag coefficient of this vehicle.
	 * @return Air drag value from the engine.
	 */
	inline uint8_t GetAirDrag() const
	{
		return RailVehInfo(this->engine_type)->air_drag;
	}

	/**
	 * Checks the current acceleration status of this vehicle.
	 * @return Acceleration status.
	 */
	inline AccelStatus GetAccelerationStatus() const
	{
		return ((this->vehstatus & VS_STOPPED) || HasBit(this->flags, VRF_REVERSING) || HasBit(this->flags, VRF_TRAIN_STUCK) || HasBit(this->flags, VRF_BREAKDOWN_BRAKING)) ? AS_BRAKE : AS_ACCEL;
	}

	/**
	 * Calculates the current speed of this vehicle.
	 * @return Current speed in km/h-ish.
	 */
	inline uint16_t GetCurrentSpeed() const
	{
		return this->cur_speed;
	}

	/**
	 * Returns the rolling friction coefficient of this vehicle.
	 * @return Rolling friction coefficient in [1e-4].
	 */
	inline uint32_t GetRollingFriction() const
	{
		/* Rolling friction for steel on steel is between 0.1% and 0.2%.
		 * The friction coefficient increases with speed in a way that
		 * it doubles at 512 km/h, triples at 1024 km/h and so on. */
		return 15 * (512 + this->GetCurrentSpeed()) / 512;
	}

	/**
	 * Returns the slope steepness used by this vehicle.
	 * @return Slope steepness used by the vehicle.
	 */
	inline uint32_t GetSlopeSteepness() const
	{
		return _settings_game.vehicle.train_slope_steepness;
	}

	/**
	 * Gets the maximum speed allowed by the track for this vehicle.
	 * @return Maximum speed allowed.
	 */
	inline uint16_t GetMaxTrackSpeed() const
	{
		return GetRailTypeInfo(GetRailTypeByTrackBit(this->tile, this->track))->max_speed;
	}

	/**
	 * Returns the curve speed modifier of this vehicle.
	 * @return Current curve speed modifier, in fixed-point binary representation with 8 fractional bits.
	 */
	inline int16_t GetCurveSpeedModifier() const
	{
		return GetVehicleProperty(this, PROP_TRAIN_CURVE_SPEED_MOD, RailVehInfo(this->engine_type)->curve_speed_mod, true);
	}

	/**
	 * Checks if the vehicle is at a tile that can be sloped.
	 * @return True if the tile can be sloped.
	 */
	inline bool TileMayHaveSlopedTrack() const
	{
		/* Any track that isn't TRACK_BIT_X or TRACK_BIT_Y cannot be sloped. */
		return this->track == TRACK_BIT_X || this->track == TRACK_BIT_Y;
	}

	/**
	 * Trains can always use the faster algorithm because they
	 * have always the same direction as the track under them.
	 * @return false
	 */
	inline bool HasToUseGetSlopePixelZ()
	{
		return false;
	}
};

struct TrainDecelerationStats {
	int deceleration_x2;
	int uncapped_deceleration_x2;
	int z_pos;
	const Train *t;

	TrainDecelerationStats(const Train *t, int z_pos);
};

CommandCost CmdMoveRailVehicle(TileIndex, DoCommandFlag , uint32_t, uint32_t, const char *);
CommandCost CmdMoveVirtualRailVehicle(TileIndex, DoCommandFlag, uint32_t, uint32_t, const char*);

Train* BuildVirtualRailVehicle(EngineID, StringID &error, uint32_t user, bool no_consist_change);

int GetTileMarginInFrontOfTrain(const Train *v, int x_pos, int y_pos);

inline int GetTileMarginInFrontOfTrain(const Train *v)
{
	return GetTileMarginInFrontOfTrain(v, v->x_pos, v->y_pos);
}

int GetTrainStopLocation(StationID station_id, TileIndex tile, Train *v, bool update_train_state, int *station_ahead, int *station_length);

int GetTrainRealisticAccelerationAtSpeed(const int speed, const int mass, const uint32_t cached_power, const uint32_t max_te, const uint32_t air_drag, const RailType railtype);
int GetTrainEstimatedMaxAchievableSpeed(const Train *train, int mass, const int speed_cap);

#endif /* TRAIN_H */
