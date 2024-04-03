/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ship.h Base for ships. */

#ifndef SHIP_H
#define SHIP_H

#include <array>

#include "vehicle_base.h"
#include "water_map.h"
#include "core/ring_buffer.hpp"

extern const DiagDirection _ship_search_directions[TRACK_END][DIAGDIR_END];

void GetShipSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type);
WaterClass GetEffectiveWaterClass(TileIndex tile);

typedef ring_buffer<Trackdir> ShipPathCache;

/** Maximum segments of ship path cache */
static const uint8_t SHIP_PATH_CACHE_LENGTH = 32;
static const uint8_t SHIP_PATH_CACHE_MASK = (SHIP_PATH_CACHE_LENGTH - 1);
static_assert((SHIP_PATH_CACHE_LENGTH & SHIP_PATH_CACHE_MASK) == 0, ""); // Must be a power of 2

/**
 * All ships have this type.
 */
struct Ship final : public SpecializedVehicle<Ship, VEH_SHIP> {
	TrackBits state;               ///< The "track" the ship is following.
	ShipPathCache cached_path;     ///< Cached path.
	Direction rotation;            ///< Visible direction.
	int16_t rotation_x_pos;        ///< NOSAVE: X Position before rotation.
	int16_t rotation_y_pos;        ///< NOSAVE: Y Position before rotation.
	uint8_t lost_count;            ///< Count of number of failed pathfinder attempts
	byte critical_breakdown_count; ///< Counter for the number of critical breakdowns since last service

	/** We don't want GCC to zero our struct! It already is zeroed and has an index! */
	Ship() : SpecializedVehicleBase() {}
	/** We want to 'destruct' the right class. */
	virtual ~Ship() { this->PreDestructor(); }

	void MarkDirty() override;
	void UpdateDeltaXY() override;
	ExpensesType GetExpenseType(bool income) const override { return income ? EXPENSES_SHIP_REVENUE : EXPENSES_SHIP_RUN; }
	void PlayLeaveStationSound(bool force = false) const override;
	bool IsPrimaryVehicle() const override { return this->Previous() == nullptr; }
	void GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const override;
	Direction GetMapImageDirection() const { return this->rotation; }
	int GetDisplaySpeed() const  override{ return this->cur_speed / 2; }
	int GetDisplayMaxSpeed() const override{ return this->vcache.cached_max_speed / 2; }
	int GetEffectiveMaxSpeed() const;
	int GetDisplayEffectiveMaxSpeed() const { return this->GetEffectiveMaxSpeed() / 2; }
	int GetCurrentMaxSpeed() const override { return std::min<int>(this->GetEffectiveMaxSpeed(), this->current_order.GetMaxSpeed() * 2); }
	Money GetRunningCost() const override;
	bool IsInDepot() const override { return this->state == TRACK_BIT_DEPOT; }
	bool Tick() override;
	void OnNewDay() override;
	void OnPeriodic() override;
	Trackdir GetVehicleTrackdir() const override;
	TileIndex GetOrderStationLocation(StationID station) override;
	TileIndex GetCargoTile() const override { return this->First()->tile; }
	ClosestDepot FindClosestDepot() override;
	void UpdateCache();
	void SetDestTile(TileIndex tile) override;
};

bool IsShipDestinationTile(TileIndex tile, StationID station);

#endif /* SHIP_H */
