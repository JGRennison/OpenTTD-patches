/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file aircraft.h Base for aircraft. */

#ifndef AIRCRAFT_H
#define AIRCRAFT_H

#include "station_map.h"
#include "vehicle_base.h"

/**
 * Base values for flight levels above ground level for 'normal' flight and holding patterns.
 * Due to speed and direction, the actual flight level may be higher.
 */
static constexpr int AIRCRAFT_MIN_FLYING_ALTITUDE = 120; ///< Minimum flying altitude above tile.
static constexpr int AIRCRAFT_MAX_FLYING_ALTITUDE = 360; ///< Maximum flying altitude above tile.
static constexpr int PLANE_HOLD_MAX_FLYING_ALTITUDE = 150; ///< holding flying altitude above tile of planes.
static constexpr int HELICOPTER_HOLD_MAX_FLYING_ALTITUDE = 184; ///< holding flying altitude above tile of helicopters.

struct Aircraft;

/** An aircraft can be one of those types. */
enum AircraftSubType : uint8_t {
	AIR_HELICOPTER = 0, ///< an helicopter
	AIR_AIRCRAFT   = 2, ///< an airplane
	AIR_SHADOW     = 4, ///< shadow of the aircraft
	AIR_ROTOR      = 6, ///< rotor of an helicopter
};

/** Flags for air vehicles; shared with disaster vehicles. */
enum AirVehicleFlags : uint8_t {
	VAF_DEST_TOO_FAR             = 0, ///< Next destination is too far away.

	/* The next two flags are to prevent stair climbing of the aircraft. The idea is that the aircraft
	 * will ascend or descend multiple flight levels at a time instead of following the contours of the
	 * landscape at a fixed altitude. This only has effect when there are more than 15 height levels. */
	VAF_IN_MAX_HEIGHT_CORRECTION = 1, ///< The vehicle is currently lowering its altitude because it hit the upper bound.
	VAF_IN_MIN_HEIGHT_CORRECTION = 2, ///< The vehicle is currently raising its altitude because it hit the lower bound.

	VAF_HELI_DIRECT_DESCENT      = 3, ///< The helicopter is descending directly at its destination (helipad or in front of hangar)
};

static const int ROTOR_Z_OFFSET         = 5;    ///< Z Offset between helicopter- and rotorsprite.

void HandleAircraftEnterHangar(Aircraft *v);
void GetAircraftSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type);
void UpdateAirplanesOnNewStation(const Station *st);
void UpdateAircraftCache(Aircraft *v, bool update_range = false);

void AircraftLeaveHangar(Aircraft *v, Direction exit_dir);
void AircraftNextAirportPos_and_Order(Aircraft *v);
void SetAircraftPosition(Aircraft *v, int x, int y, int z);
void FindBreakdownDestination(Aircraft *v);

void GetAircraftFlightLevelBounds(const Vehicle *v, int *min, int *max);
template <class T>
int GetAircraftFlightLevel(T *v, bool takeoff = false);

/** Variables that are cached to improve performance and such. */
struct AircraftCache {
	uint32_t cached_max_range_sqr; ///< Cached squared maximum range.
	uint16_t cached_max_range;     ///< Cached maximum range.
	uint8_t image_movement_state;  ///< Cached image aircraft movement state

	bool operator==(const AircraftCache &) const = default;
};

/**
 * Aircraft, helicopters, rotors and their shadows belong to this class.
 */
struct Aircraft final : public SpecializedVehicle<Aircraft, VEH_AIRCRAFT> {
	uint16_t crashed_counter;         ///< Timer for handling crash animations.
	uint8_t pos;                      ///< Next desired position of the aircraft.
	uint8_t previous_pos;             ///< Previous desired position of the aircraft.
	StationID targetairport;          ///< Airport to go to next.
	uint8_t state;                    ///< State of the airport. @see AirportMovementStates
	Direction last_direction;
	uint8_t number_consecutive_turns; ///< Protection to prevent the aircraft of making a lot of turns in order to reach a specific point.
	uint8_t turn_counter;             ///< Ticks between each turn to prevent > 45 degree turns.
	uint8_t flags;                    ///< Aircraft flags. @see AirVehicleFlags

	AircraftCache acache;

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	Aircraft() : SpecializedVehicleBase() {}
	/** We want to 'destruct' the right class. */
	virtual ~Aircraft() { this->PreDestructor(); }

	void MarkDirty() override;
	void UpdateDeltaXY() override;
	ExpensesType GetExpenseType(bool income) const override { return income ? EXPENSES_AIRCRAFT_REVENUE : EXPENSES_AIRCRAFT_RUN; }
	bool IsPrimaryVehicle() const override                  { return this->IsNormalAircraft(); }
	void GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const override;
	Direction GetMapImageDirection() const { return this->First()->direction; }
	int GetDisplaySpeed() const override    { return this->cur_speed; }
	int GetDisplayMaxSpeed() const override { return this->vcache.cached_max_speed; }
	int GetSpeedOldUnits() const            { return this->vcache.cached_max_speed * 10 / 128; }
	int GetCurrentMaxSpeed() const override { return this->GetSpeedOldUnits(); }
	Money GetRunningCost() const override;

	bool IsInDepot() const override
	{
		assert(this->IsPrimaryVehicle());
		return (this->vehstatus & VS_HIDDEN) != 0 && IsHangarTile(this->tile);
	}

	bool Tick() override;
	void OnNewDay() override;
	void OnPeriodic() override;
	uint Crash(bool flooded = false) override;
	TileIndex GetOrderStationLocation(StationID station) override;
	TileIndex GetCargoTile() const override { return this->First()->tile; }
	ClosestDepot FindClosestDepot() override;

	/**
	 * Check if the aircraft type is a normal flying device; eg
	 * not a rotor or a shadow
	 * @return Returns true if the aircraft is a helicopter/airplane and
	 * false if it is a shadow or a rotor
	 */
	inline bool IsNormalAircraft() const
	{
		/* To be fully correct the commented out functionality is the proper one,
		 * but since value can only be 0 or 2, it is sufficient to only check <= 2
		 * return (this->subtype == AIR_HELICOPTER) || (this->subtype == AIR_AIRCRAFT); */
		return this->subtype <= AIR_AIRCRAFT;
	}

	/**
	 * Get the range of this aircraft.
	 * @return Range in tiles or 0 if unlimited range.
	 */
	uint16_t GetRange() const
	{
		return this->acache.cached_max_range;
	}
};

void GetRotorImage(const Aircraft *v, EngineImageType image_type, VehicleSpriteSeq *result);

Station *GetTargetAirportIfValid(const Aircraft *v);
void HandleMissingAircraftOrders(Aircraft *v);

const char *AirportMovementStateToString(uint8_t state);

#endif /* AIRCRAFT_H */
