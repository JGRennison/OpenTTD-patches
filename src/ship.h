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

extern const DiagDirection _ship_search_directions[TRACK_END][DIAGDIR_END];

void GetShipSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type);
WaterClass GetEffectiveWaterClass(TileIndex tile);

/** Maximum segments of ship path cache */
static const uint8 SHIP_PATH_CACHE_LENGTH = 32;
static const uint8 SHIP_PATH_CACHE_MASK = (SHIP_PATH_CACHE_LENGTH - 1);
static_assert((SHIP_PATH_CACHE_LENGTH & SHIP_PATH_CACHE_MASK) == 0, ""); // Must be a power of 2

struct ShipPathCache {
	std::array<Trackdir, SHIP_PATH_CACHE_LENGTH> td;
	uint8 start = 0;
	uint8 count = 0;

	inline bool empty() const { return this->count == 0; }
	inline uint8 size() const { return this->count; }
	inline bool full() const { return this->count >= SHIP_PATH_CACHE_LENGTH; }

	inline void clear()
	{
		this->start = 0;
		this->count = 0;
	}

	inline Trackdir front() const { return this->td[this->start]; }
	inline Trackdir back() const { return this->td[(this->start + this->count - 1) & SHIP_PATH_CACHE_MASK]; }

	/* push an item to the front of the ring, if the ring is already full, the back item is overwritten */
	inline void push_front(Trackdir td)
	{
		this->start = (this->start - 1) & SHIP_PATH_CACHE_MASK;
		if (!this->full()) this->count++;
		this->td[this->start] = td;
	}

	inline void pop_front()
	{
		this->start = (this->start + 1) & SHIP_PATH_CACHE_MASK;
		this->count--;
	}

	inline void pop_back()
	{
		this->count--;
	}
};

/**
 * All ships have this type.
 */
struct Ship FINAL : public SpecializedVehicle<Ship, VEH_SHIP> {
	TrackBits state;      ///< The "track" the ship is following.
	std::unique_ptr<ShipPathCache> cached_path;   ///< Cached path.
	Direction rotation;   ///< Visible direction.
	int16 rotation_x_pos; ///< NOSAVE: X Position before rotation.
	int16 rotation_y_pos; ///< NOSAVE: Y Position before rotation.
	uint8 lost_count;     ///< Count of number of failed pathfinder attempts
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
	ClosestDepot FindClosestDepot() override;
	void UpdateCache();
	void SetDestTile(TileIndex tile) override;

	inline ShipPathCache &GetOrCreatePathCache()
	{
		if (!this->cached_path) this->cached_path.reset(new ShipPathCache());
		return *this->cached_path;
	}
};

bool IsShipDestinationTile(TileIndex tile, StationID station);

#endif /* SHIP_H */
