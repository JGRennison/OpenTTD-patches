/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file engine_base.h Base class for engines. */

#ifndef ENGINE_BASE_H
#define ENGINE_BASE_H

#include "engine_type.h"
#include "vehicle_type.h"
#include "core/pool_type.hpp"
#include "core/tinystring_type.hpp"
#include "newgrf_commons.h"

#include "3rdparty/cpp-btree/btree_map.h"
#include <vector>

struct WagonOverride {
	std::vector<EngineID> engines;
	CargoID cargo;
	const SpriteGroup *group;
};

/** Flags used client-side in the purchase/autorenew engine list. */
enum class EngineDisplayFlags : uint8_t {
	None        = 0,         ///< No flag set.
	HasVariants = (1U << 0), ///< Set if engine has variants.
	IsFolded    = (1U << 1), ///< Set if display of variants should be folded (hidden).
	Shaded      = (1U << 2), ///< Set if engine should be masked.
};
DECLARE_ENUM_AS_BIT_SET(EngineDisplayFlags)

typedef Pool<Engine, EngineID, 64, 64000> EnginePool;
extern EnginePool _engine_pool;

struct EngineRefitCapacityValue {
	CargoTypes cargoes;
	uint32_t capacity;
};

struct Engine : EnginePool::PoolItem<&_engine_pool> {
	TinyString name;              ///< Custom name of engine.
	CalTime::Date intro_date;     ///< Date of introduction of the engine.
	int32_t age;                  ///< Age of the engine in months.
	uint16_t reliability;         ///< Current reliability of the engine.
	uint16_t reliability_spd_dec; ///< Speed of reliability decay between services (per day).
	uint16_t reliability_start;   ///< Initial reliability of the engine.
	uint16_t reliability_max;     ///< Maximal reliability of the engine.
	uint16_t reliability_final;   ///< Final reliability of the engine.
	uint16_t duration_phase_1;    ///< First reliability phase in months, increasing reliability from #reliability_start to #reliability_max.
	uint16_t duration_phase_2;    ///< Second reliability phase in months, keeping #reliability_max.
	uint16_t duration_phase_3;    ///< Third reliability phase in months, decaying to #reliability_final.
	uint8_t flags;                ///< Flags of the engine. @see EngineFlags
	CompanyMask preview_asked;    ///< Bit for each company which has already been offered a preview.
	CompanyID preview_company;    ///< Company which is currently being offered a preview \c INVALID_COMPANY means no company.
	uint8_t preview_wait;         ///< Daily countdown timer for timeout of offering the engine to the #preview_company company.
	CompanyMask company_avail;    ///< Bit for each company whether the engine is available for that company.
	CompanyMask company_hidden;   ///< Bit for each company whether the engine is normally hidden in the build gui for that company.
	uint8_t original_image_index; ///< Original vehicle image index, thus the image index of the overridden vehicle
	VehicleType type;             ///< %Vehicle type, ie #VEH_ROAD, #VEH_TRAIN, etc.

	EngineDisplayFlags display_flags; ///< NOSAVE client-side-only display flags for build engine list.
	EngineID display_last_variant;    ///< NOSAVE client-side-only last variant selected.

	EngineInfo info;

	union {
		RailVehicleInfo rail;
		RoadVehicleInfo road;
		ShipVehicleInfo ship;
		AircraftVehicleInfo air;
	} u;

	/* NewGRF related data */
	/**
	 * Properties related the the grf file.
	 * NUM_CARGO real cargo plus two pseudo cargo sprite groups.
	 * Used for obtaining the sprite offset of custom sprites, and for
	 * evaluating callbacks.
	 */
	GRFFilePropsBase<NUM_CARGO + 2> grf_prop;
	std::vector<WagonOverride> overrides;
	uint16_t list_position;

	SpriteGroupCallbacksUsed callbacks_used = SGCU_ALL;
	uint64_t cb36_properties_used = UINT64_MAX;
	btree::btree_map<const SpriteGroup *, uint64_t> sprite_group_cb36_properties_used;

	std::unique_ptr<EngineRefitCapacityValue, FreeDeleter> refit_capacity_values;

	Engine() {}
	Engine(VehicleType type, EngineID base);
	bool IsEnabled() const;

	/**
	 * Determines the default cargo type of an engine.
	 *
	 * Usually a valid cargo is returned, even though the vehicle has zero capacity, and can therefore not carry anything. But the cargotype is still used
	 * for livery selection etc..
	 *
	 * Vehicles with INVALID_CARGO as default cargo are usually not available, but it can appear as default cargo of articulated parts.
	 *
	 * @return The default cargo type.
	 * @see CanCarryCargo
	 */
	CargoID GetDefaultCargoType() const
	{
		return this->info.cargo_type;
	}

	uint DetermineCapacity(const Vehicle *v, uint16_t *mail_capacity = nullptr, CargoID attempt_refit = INVALID_CARGO) const;

	bool CanCarryCargo() const;
	bool CanPossiblyCarryCargo() const;

	/**
	 * Determines the default cargo capacity of an engine for display purposes.
	 *
	 * For planes carrying both passenger and mail this is the passenger capacity.
	 * For multiheaded engines this is the capacity of both heads.
	 * For articulated engines use GetCapacityOfArticulatedParts
	 *
	 * @param mail_capacity returns secondary cargo (mail) capacity of aircraft
	 * @param attempt_refit cargo ID to attempt to use
	 * @return The default capacity
	 * @see GetDefaultCargoType
	 */
	uint GetDisplayDefaultCapacity(uint16_t *mail_capacity = nullptr, CargoID attempt_refit = INVALID_CARGO) const
	{
		return this->DetermineCapacity(nullptr, mail_capacity, attempt_refit);
	}

	Money GetRunningCost() const;
	Money GetDisplayRunningCost() const;
	Money GetCost() const;
	uint GetDisplayMaxSpeed() const;
	uint GetPower() const;
	uint GetDisplayWeight() const;
	uint GetDisplayMaxTractiveEffort() const;
	DateDelta GetLifeLengthInDays() const;
	uint16_t GetRange() const;
	StringID GetAircraftTypeText() const;

	/**
	 * Check whether the engine is hidden in the GUI for the given company.
	 * @param c Company to check.
	 * @return \c true iff the engine is hidden in the GUI for the given company.
	 */
	inline bool IsHidden(CompanyID c) const
	{
		return c < MAX_COMPANIES && HasBit(this->company_hidden, c);
	}

	/**
	 * Get the last display variant for an engine.
	 * @return Engine's last display variant or engine itself if no last display variant is set.
	 */
	const Engine *GetDisplayVariant() const
	{
		if (this->display_last_variant == this->index || this->display_last_variant == INVALID_ENGINE) return this;
		return Engine::Get(this->display_last_variant);
	}

	bool IsVariantHidden(CompanyID c) const;

	/**
	 * Check if the engine is a ground vehicle.
	 * @return True iff the engine is a train or a road vehicle.
	 */
	inline bool IsGroundVehicle() const
	{
		return this->type == VEH_TRAIN || this->type == VEH_ROAD;
	}

	/**
	 * Check if the vehicle type supports articulation.
	 * @return True iff the vehicle is a train, road vehicle or ship.
	 */
	inline bool IsArticulatedCallbackVehicleType() const
	{
		return this->type == VEH_TRAIN || this->type == VEH_ROAD || this->type == VEH_SHIP;
	}

	/**
	 * Retrieve the NewGRF the engine is tied to.
	 * This is the GRF providing the Action 3.
	 * @return NewGRF associated to the engine.
	 */
	const GRFFile *GetGRF() const
	{
		return this->grf_prop.grffile;
	}

	uint32_t GetGRFID() const;

	struct EngineTypeFilter {
		VehicleType vt;

		bool operator() (size_t index) { return Engine::Get(index)->type == this->vt; }
	};

	/**
	 * Returns an iterable ensemble of all valid engines of the given type
	 * @param vt the VehicleType for engines to be valid
	 * @param from index of the first engine to consider
	 * @return an iterable ensemble of all valid engines of the given type
	 */
	static Pool::IterateWrapperFiltered<Engine, EngineTypeFilter> IterateType(VehicleType vt, size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<Engine, EngineTypeFilter>(from, EngineTypeFilter{ vt });
	}
};

inline const EngineInfo *EngInfo(EngineID e)
{
	return &Engine::Get(e)->info;
}

inline const RailVehicleInfo *RailVehInfo(EngineID e)
{
	return &Engine::Get(e)->u.rail;
}

inline const RoadVehicleInfo *RoadVehInfo(EngineID e)
{
	return &Engine::Get(e)->u.road;
}

inline const ShipVehicleInfo *ShipVehInfo(EngineID e)
{
	return &Engine::Get(e)->u.ship;
}

inline const AircraftVehicleInfo *AircraftVehInfo(EngineID e)
{
	return &Engine::Get(e)->u.air;
}

#endif /* ENGINE_BASE_H */
