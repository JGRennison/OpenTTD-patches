/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file  vehicle_base.h Base class for all vehicles. */

#ifndef VEHICLE_BASE_H
#define VEHICLE_BASE_H

#include "track_type.h"
#include "command_type.h"
#include "order_base.h"
#include "cargopacket.h"
#include "texteff.hpp"
#include "engine_type.h"
#include "order_func.h"
#include "transport_type.h"
#include "group_type.h"
#include "timetable.h"
#include "base_consist.h"
#include "newgrf_cache_check.h"
#include "landscape.h"
#include "network/network.h"
#include "core/alignment.hpp"
#include "sl/saveload_common.h"
#include <array>
#include <list>
#include <map>
#include <vector>

/** Vehicle state bits in #Vehicle::vehstatus. */
enum class VehState : uint8_t {
	Hidden         = 0, ///< Vehicle is not visible.
	Stopped        = 1, ///< Vehicle is stopped by the player.
	Unclickable    = 2, ///< Vehicle is not clickable by the user (shadow vehicles).
	DefaultPalette = 3, ///< Use default vehicle palette. @see DoDrawVehicle
	TrainSlowing   = 4, ///< Train is slowing down.
	Shadow         = 5, ///< Vehicle is a shadow vehicle.
	AircraftBroken = 6, ///< Aircraft is broken down.
	Crashed        = 7, ///< Vehicle is crashed.
};
using VehStates = EnumBitSet<VehState, uint8_t>;

/** Bit numbers used to indicate which of the #NewGRFCache values are valid. */
enum NewGRFCacheValidValues : uint8_t {
	NCVV_POSITION_CONSIST_LENGTH   = 0, ///< This bit will be set if the NewGRF var 40 currently stored is valid.
	NCVV_POSITION_SAME_ID_LENGTH   = 1, ///< This bit will be set if the NewGRF var 41 currently stored is valid.
	NCVV_CONSIST_CARGO_INFORMATION = 2, ///< This bit will be set if the NewGRF var 42 currently stored is valid.
	NCVV_COMPANY_INFORMATION       = 3, ///< This bit will be set if the NewGRF var 43 currently stored is valid.
	NCVV_POSITION_IN_VEHICLE       = 4, ///< This bit will be set if the NewGRF var 4D currently stored is valid.
	NCVV_CONSIST_CARGO_INFORMATION_UD = 5, ///< This bit will be set if the uppermost byte of NewGRF var 42 currently stored is valid.
	NCVV_END,                           ///< End of the bits.
};

/** Cached often queried (NewGRF) values */
struct NewGRFCache {
	/* Values calculated when they are requested for the first time after invalidating the NewGRF cache. */
	uint32_t position_consist_length = 0;      ///< Cache for NewGRF var 40.
	uint32_t position_same_id_length = 0;      ///< Cache for NewGRF var 41.
	uint32_t consist_cargo_information = 0;    ///< Cache for NewGRF var 42. (Note: The cargotype is untranslated in the cache because the accessing GRF is yet unknown.)
	uint32_t company_information = 0;          ///< Cache for NewGRF var 43.
	uint32_t position_in_vehicle = 0;          ///< Cache for NewGRF var 4D.
	NO_UNIQUE_ADDRESS uint8_t cache_valid = 0; ///< Bitset that indicates which cache values are valid.

	bool operator==(const NewGRFCache&) const = default;
};

/** Meaning of the various bits of the visual effect. */
enum VisualEffect : uint8_t {
	VE_OFFSET_START        = 0, ///< First bit that contains the offset (0 = front, 8 = centre, 15 = rear)
	VE_OFFSET_COUNT        = 4, ///< Number of bits used for the offset
	VE_OFFSET_CENTRE       = 8, ///< Value of offset corresponding to a position above the centre of the vehicle

	VE_TYPE_START          = 4, ///< First bit used for the type of effect
	VE_TYPE_COUNT          = 2, ///< Number of bits used for the effect type
	VE_TYPE_DEFAULT        = 0, ///< Use default from engine class
	VE_TYPE_STEAM          = 1, ///< Steam plumes
	VE_TYPE_DIESEL         = 2, ///< Diesel fumes
	VE_TYPE_ELECTRIC       = 3, ///< Electric sparks

	VE_DISABLE_EFFECT      = 6, ///< Flag to disable visual effect
	VE_ADVANCED_EFFECT     = VE_DISABLE_EFFECT, ///< Flag for advanced effects
	VE_DISABLE_WAGON_POWER = 7, ///< Flag to disable wagon power

	VE_DEFAULT = 0xFF,          ///< Default value to indicate that visual effect should be based on engine class
};

/** Models for spawning visual effects. */
enum VisualEffectSpawnModel : uint8_t {
	VESM_NONE              = 0, ///< No visual effect
	VESM_STEAM,                 ///< Steam model
	VESM_DIESEL,                ///< Diesel model
	VESM_ELECTRIC,              ///< Electric model

	VESM_END
};

/**
 * Enum to handle ground vehicle subtypes.
 * This is defined here instead of at #GroundVehicle because some common function require access to these flags.
 * Do not access it directly unless you have to. Use the subtype access functions.
 */
enum GroundVehicleSubtypeFlags : uint8_t {
	GVSF_FRONT            = 0, ///< Leading engine of a consist.
	GVSF_ARTICULATED_PART = 1, ///< Articulated part of an engine.
	GVSF_WAGON            = 2, ///< Wagon (not used for road vehicles).
	GVSF_ENGINE           = 3, ///< Engine that can be front engine, but might be placed behind another engine (not used for road vehicles).
	GVSF_FREE_WAGON       = 4, ///< First in a wagon chain (in depot) (not used for road vehicles).
	GVSF_MULTIHEADED      = 5, ///< Engine is multiheaded (not used for road vehicles).
	GVSF_VIRTUAL          = 6, ///< Used for virtual trains during template design, it is needed to skip checks for tile or depot status
};

/**
 * Enum to handle vehicle cache flags.
 */
enum VehicleCacheFlags {
	VCF_LAST_VISUAL_EFFECT      = 0, ///< Last vehicle in the consist with a visual effect.
	VCF_GV_ZERO_SLOPE_RESIST    = 1, ///< GroundVehicle: Consist has zero slope resistance (valid only for the first engine), may be false negative.
	VCF_IS_DRAWN                = 2, ///< Vehicle is currently drawn
	VCF_REDRAW_ON_TRIGGER       = 3, ///< Clear cur_image_valid_dir on changes to waiting_random_triggers (valid only for the first engine)
	VCF_REDRAW_ON_SPEED_CHANGE  = 4, ///< Clear cur_image_valid_dir on changes to cur_speed (ground vehicles) or aircraft movement state (aircraft) (valid only for the first engine)
	VCF_IMAGE_REFRESH           = 5, ///< Image should be refreshed before drawing
	VCF_IMAGE_REFRESH_NEXT      = 6, ///< Set VCF_IMAGE_REFRESH in next UpdateViewport call, if the image is not updated there
	VCF_IMAGE_CURVATURE         = 7, ///< Image should be refreshed if cached curvature in cached_image_curvature no longer matches curvature of neighbours
};

/** Cached often queried values common to all vehicles. */
struct VehicleCache {
	uint16_t cached_max_speed = 0;        ///< Maximum speed of the consist (minimum of the max speed of all vehicles in the consist).
	uint16_t cached_cargo_age_period = 0; ///< Number of ticks before carried cargo is aged.
	uint16_t cached_image_curvature = 0;  ///< Cached neighbour curvature, see: VCF_IMAGE_CURVATURE

	uint8_t cached_vis_effect = 0;  ///< Visual effect to show (see #VisualEffect)
	uint8_t cached_veh_flags = 0;   ///< Vehicle cache flags (see #VehicleCacheFlags)
};

/** Sprite sequence for a vehicle part. */
struct VehicleSpriteSeq {
/* Reduce the size of struct Vehicle in dedicated builds */
#if defined(DEDICATED)
	std::array<PalSpriteID, 1> seq;
#else
	std::array<PalSpriteID, 8> seq;
#endif
	uint count;

	bool operator==(const VehicleSpriteSeq &other) const
	{
		return std::ranges::equal(std::span(this->seq.data(), this->count), std::span(other.seq.data(), other.count));
	}

	/**
	 * Check whether the sequence contains any sprites.
	 */
	bool IsValid() const
	{
		return this->count != 0;
	}

	/**
	 * Clear all information.
	 */
	void Clear()
	{
		this->count = 0;
	}

	/**
	 * Assign a single sprite to the sequence.
	 */
	void Set(SpriteID sprite)
	{
		this->count = 1;
		this->seq[0].sprite = sprite;
		this->seq[0].pal = 0;
	}

	/**
	 * Copy data from another sprite sequence, while dropping all recolouring information.
	 */
	void CopyWithoutPalette(const VehicleSpriteSeq &src)
	{
		this->count = src.count;
		for (uint i = 0; i < src.count; ++i) {
			this->seq[i].sprite = src.seq[i].sprite;
			this->seq[i].pal = 0;
		}
	}

	Rect16 GetBounds() const;
	void Draw(int x, int y, PaletteID default_pal, bool force_pal) const;
};

enum PendingSpeedRestrictionChangeFlags {
	PSRCF_DIAGONAL                    = 0,
};

struct PendingSpeedRestrictionChange {
	uint16_t distance = 0;
	uint16_t new_speed = 0;
	uint16_t prev_speed;
	uint16_t flags;
};

/** A vehicle pool for a little over 1 million vehicles. */
#if OTTD_UPPER_TAGGED_PTR
struct VehiclePoolOps {
	using Tptr = uintptr_t;
	using Tparam_type = VehicleType;

	static inline Vehicle *GetPtr(uintptr_t ptr) {
		return reinterpret_cast<Vehicle *>(ptr & ((static_cast<uintptr_t>(1) << 60) - 1)); // GB can't be used here because its return type is limited to 32 bits
	}

	static inline uintptr_t PutPtr(Vehicle *v, VehicleType vtype)
	{
		uintptr_t ptr = reinterpret_cast<uintptr_t>(v);
		SB(ptr, 60, 3, vtype & 7);
		return ptr;
	}

	static constexpr uintptr_t NullValue() { return 0; }
	static constexpr VehicleType DefaultItemParam() { return VEH_INVALID; }

	static constexpr VehicleType GetVehicleType(uintptr_t ptr) { return static_cast<VehicleType>(GB(ptr, 60, 3)); }
	static constexpr bool IsNonFrontVehiclePtr(uintptr_t ptr) { return HasBit(ptr, 63); }

	static constexpr void SetIsNonFrontVehiclePtr(uintptr_t &ptr, bool non_front) { AssignBit(ptr, 63, non_front); }
};

typedef Pool<Vehicle, VehicleID, 512, PoolType::Normal, false, VehiclePoolOps> VehiclePool;
#else
typedef Pool<Vehicle, VehicleID, 512> VehiclePool;
#endif

extern VehiclePool _vehicle_pool;

/* Some declarations of functions, so we can make them friendly */
struct GroundVehicleCache;
extern NamedSaveLoadTable GetVehicleDescription(VehicleType vt);
struct LoadgameState;
extern bool LoadOldVehicle(LoadgameState &ls, int num);
extern void FixOldVehicles(LoadgameState &ls);

struct GRFFile;

namespace upstream_sl {
	class SlVehicleCommon;
	class SlVehicleDisaster;
}

/**
 * Structure to return information about the closest depot location,
 * and whether it could be found.
 */
struct ClosestDepot {
	TileIndex location = INVALID_TILE;
	DestinationID destination{}; ///< The DestinationID as used for orders.
	bool reverse = false;
	bool found = false;

	ClosestDepot() = default;

	ClosestDepot(TileIndex location, DestinationID destination, bool reverse = false) :
		location(location), destination(destination), reverse(reverse), found(true) {}
};

struct VehicleUnbunchState {
	StateTicks depot_unbunching_last_departure = INVALID_STATE_TICKS; ///< When the vehicle last left its unbunching depot.
	StateTicks depot_unbunching_next_departure = INVALID_STATE_TICKS; ///< When the vehicle will next try to leave its unbunching depot.
	Ticks round_trip_time = 0;                                        ///< How many ticks for a single circumnavigation of the orders.
};

/** %Vehicle data structure. */
struct Vehicle : VehiclePool::PoolItem<&_vehicle_pool>, BaseVehicle, BaseConsist {
	/* These are here for structure packing purposes */

	CargoType cargo_type{};                      ///< type of cargo this vehicle is carrying
	EngineID engine_type = EngineID::Invalid();  ///< The type of engine used for this vehicle.
	TileIndex tile = INVALID_TILE;               ///< Current tile index

private:
	Vehicle *next = nullptr;                     ///< pointer to the next vehicle in the chain
	Vehicle *previous = nullptr;                 ///< NOSAVE: pointer to the previous vehicle in the chain
	Vehicle *first = nullptr;                    ///< NOSAVE: pointer to the first vehicle in the chain

	Vehicle *next_shared = nullptr;              ///< pointer to the next vehicle that shares the order
	Vehicle *previous_shared = nullptr;          ///< NOSAVE: pointer to the previous vehicle in the shared order chain

public:
	friend NamedSaveLoadTable GetVehicleDescription(VehicleType vt); ///< So we can use private/protected variables in the saveload code
	friend void FixOldVehicles(LoadgameState &ls);
	friend void AfterLoadVehiclesPhase1(bool part_of_load);     ///< So we can set the #previous and #first pointers while loading
	friend bool LoadOldVehicle(LoadgameState &ls, int num);     ///< So we can set the proper next pointer while loading

	friend upstream_sl::SlVehicleCommon;
	friend upstream_sl::SlVehicleDisaster;

	static void PreCleanPool();

	Money profit_this_year = 0;                  ///< Profit this year << 8, low 8 bits are fract
	Money profit_last_year = 0;                  ///< Profit last year << 8, low 8 bits are fract
	Money profit_lifetime = 0;                   ///< Profit lifetime << 8, low 8 bits are fract
	Money value = 0;                             ///< Value of the vehicle

	CargoPayment *cargo_payment = nullptr;       ///< The cargo payment we're currently in

	/**
	 * Heading for this tile.
	 * For airports and train stations this tile does not necessarily belong to the destination station,
	 * but it can be used for heuristic purposes to estimate the distance.
	 */
	TileIndex dest_tile = INVALID_TILE;

	/* Used for timetabling. */
	uint32_t current_loading_time = 0;           ///< How long loading took. Less than current_order_time if vehicle is early.

	Rect coord{};                                ///< NOSAVE: Graphical bounding box of the vehicle, i.e. what to redraw on moves.

	Vehicle *hash_viewport_next = nullptr;       ///< NOSAVE: Next vehicle in the visual location hash.
	Vehicle **hash_viewport_prev = nullptr;      ///< NOSAVE: Previous vehicle in the visual location hash.

	Vehicle *hash_tile_next = nullptr;           ///< NOSAVE: Next vehicle in the tile location hash.
	Vehicle *hash_tile_prev = nullptr;           ///< NOSAVE: Previous vehicle in the tile location hash.
	TileIndex hash_tile_current = INVALID_TILE;  ///< NOSAVE: current tile used for tile location hash.

	uint8_t breakdown_severity = 0;              ///< severity of the breakdown. Note that lower means more severe
	uint8_t breakdown_type = 0;                  ///< Type of breakdown
	uint8_t breakdown_chance_factor = 0;         ///< Improved breakdowns: current multiplier for breakdown_chance * 128, used for head vehicle only
	Owner owner = INVALID_OWNER;                 ///< Which company owns the vehicle?

	SpriteID colourmap{};                        ///< NOSAVE: cached colour mapping

	/* Related to age and service time */
	CalTime::Year build_year{};                  ///< Year the vehicle has been built.
	CalTime::DateDelta age{};                    ///< Age in days
	EconTime::DateDelta economy_age{};           ///< Age in economy days.
	CalTime::DateDelta max_age{};                ///< Maximum age
	EconTime::Date date_of_last_service{};       ///< Last date the vehicle had a service at a depot.
	CalTime::Date date_of_last_service_newgrf{}; ///< Last date the vehicle had a service at a depot, unchanged by the date cheat to protect against unsafe NewGRF behavior.
	uint16_t reliability = 0;                    ///< Reliability.
	uint16_t reliability_spd_dec = 0;            ///< Reliability decrease speed.
	uint8_t breakdown_ctr = 0;                   ///< Counter for managing breakdown events. @see Vehicle::HandleBreakdown
	uint8_t breakdown_delay = 0;                 ///< Counter for managing breakdown length.
	uint8_t breakdowns_since_last_service = 0;   ///< Counter for the amount of breakdowns.
	uint8_t breakdown_chance = 0;                ///< Current chance of breakdowns.

	int32_t x_pos = 0;                           ///< x coordinate.
	int32_t y_pos = 0;                           ///< y coordinate.
	int32_t z_pos = 0;                           ///< z coordinate.
	Direction direction = INVALID_DIR;           ///< facing

	/**
	 * currently displayed sprite index
	 * 0xfd == custom sprite, 0xfe == custom second head sprite
	 * 0xff == reserved for another custom sprite
	 */
	uint8_t spritenum = 0;
	UnitID unitnumber = 0;                       ///< unit number, for display purposes only
	VehicleSpriteSeq sprite_seq{};               ///< Vehicle appearance.
	Rect16 sprite_seq_bounds{};
	uint8_t x_extent = 0;                        ///< x-extent of vehicle bounding box
	uint8_t y_extent = 0;                        ///< y-extent of vehicle bounding box
	uint8_t z_extent = 0;                        ///< z-extent of vehicle bounding box
	int8_t x_bb_offs = 0;                        ///< x offset of vehicle bounding box
	int8_t y_bb_offs = 0;                        ///< y offset of vehicle bounding box
	int8_t x_offs = 0;                           ///< x offset for vehicle sprite
	int8_t y_offs = 0;                           ///< y offset for vehicle sprite

	uint8_t progress = 0;                        ///< The percentage (if divided by 256) this vehicle already crossed the tile unit.
	TextEffectID fill_percent_te_id = INVALID_TE_ID; ///< a text-effect id to a loading indicator object
	uint16_t load_unload_ticks = 0;              ///< Ticks to wait before starting next cycle.

	uint16_t cur_speed = 0;                      ///< current speed
	uint8_t subspeed = 0;                        ///< fractional speed
	uint8_t acceleration = 0;                    ///< used by train & aircraft

	uint32_t motion_counter = 0;                 ///< counter to occasionally play a vehicle sound. (Also used as virtual train client ID).

	uint16_t random_bits = 0;                    ///< Bits used for randomized variational spritegroups.
	VehicleRandomTriggers waiting_random_triggers; ///< Triggers to be yet matched before rerandomizing the random bits.

	uint8_t cargo_subtype = 0;                   ///< Used for livery refits (NewGRF variations)

	StationID last_station_visited = StationID::Invalid(); ///< The last station we stopped at.
	StationID last_loading_station = StationID::Invalid(); ///< Last station the vehicle has stopped at and could possibly leave from with any cargo loaded. (See VehicleFlag::LastLoadStationSeparate).
	StateTicks last_loading_tick{};              ///< Last tick (_state_ticks) the vehicle has stopped at a station and could possibly leave with any cargo loaded. (See VehicleFlag::LastLoadStationSeparate).

	VehicleCargoList cargo{};                    ///< The cargo this vehicle is carrying
	uint16_t cargo_cap = 0;                      ///< total capacity
	uint16_t refit_cap = 0;                      ///< Capacity left over from before last refit.
	uint16_t cargo_age_counter = 0;              ///< Ticks till cargo is aged next.
	int8_t trip_occupancy = 0;                   ///< NOSAVE: Occupancy of vehicle of the current trip (updated after leaving a station).

	uint8_t day_counter = 0;                     ///< Increased by one for each day
	uint8_t tick_counter = 0;                    ///< Increased by one for each tick
	uint8_t order_occupancy_average = 0;         ///< NOSAVE: order occupancy average. 0 = invalid, 1 = n/a, 16-116 = 0-100%
	uint16_t running_ticks = 0;                  ///< Number of ticks this vehicle was not stopped this day

	VehStates vehstatus{};                       ///< Status
	uint8_t subtype = 0;                         ///< subtype (Filled with values from #AircraftSubType/#DisasterSubType/#EffectVehicleType/#GroundVehicleSubtypeFlags)
	GroupID group_id = GroupID::Invalid();       ///< Index of group Pool array

	Order current_order{};                       ///< The current order (+ status, like: loading)

	OrderList *orders = nullptr;                 ///< Pointer to the order list for this vehicle

	NO_UNIQUE_ADDRESS NewGRFCache grf_cache{};   ///< Cache of often used calculated NewGRF values
	Direction cur_image_valid_dir = INVALID_DIR; ///< NOSAVE: direction for which cur_image does not need to be regenerated on the next tick

	VehicleCache vcache{};                       ///< Cache of often used vehicle values.

	std::unique_ptr<VehicleUnbunchState> unbunch_state{};

	/**
	 * Calculates the weight value that this vehicle will have when fully loaded with its current cargo.
	 * @return Weight value in tonnes.
	 */
	virtual uint16_t GetMaxWeight() const
	{
		return 0;
	}

	Vehicle(VehicleType type = VEH_INVALID);

	void PreDestructor();
	/** We want to 'destruct' the right class. */
	virtual ~Vehicle();

	CargoTypes GetLastLoadingStationValidCargoMask() const;

	void BeginLoading();
	void CancelReservation(StationID next, Station *st);
	void LeaveStation();
	void AdvanceLoadingInStation();

	GroundVehicleCache *GetGroundVehicleCache();
	const GroundVehicleCache *GetGroundVehicleCache() const;

	uint16_t &GetGroundVehicleFlags();
	const uint16_t &GetGroundVehicleFlags() const;

	void DeleteUnreachedImplicitOrders();

	void HandleLoading(bool mode = false);

	void HandleWaiting(bool stop_waiting, bool process_orders = false);

	/**
	 * Marks the vehicles to be redrawn and updates cached variables
	 *
	 * This method marks the area of the vehicle on the screen as dirty.
	 * It can be use to repaint the vehicle.
	 *
	 * @ingroup dirty
	 */
	virtual void MarkDirty() {}

	/**
	 * Updates the x and y offsets and the size of the sprite used
	 * for this vehicle.
	 */
	virtual void UpdateDeltaXY() {}

	/**
	 * Determines the effective direction-specific vehicle movement speed.
	 *
	 * This method belongs to the old vehicle movement method:
	 * A vehicle moves a step every 256 progress units.
	 * The vehicle speed is scaled by 3/4 when moving in X or Y direction due to the longer distance.
	 *
	 * However, this method is slightly wrong in corners, as the leftover progress is not scaled correctly
	 * when changing movement direction. #GetAdvanceSpeed() and #GetAdvanceDistance() are better wrt. this.
	 *
	 * @param speed Direction-independent unscaled speed.
	 * @return speed scaled by movement direction. 256 units are required for each movement step.
	 */
	inline uint GetOldAdvanceSpeed(uint speed)
	{
		return (this->direction & 1) ? speed : speed * 3 / 4;
	}

	/**
	 * Determines the effective vehicle movement speed.
	 *
	 * Together with #GetAdvanceDistance() this function is a replacement for #GetOldAdvanceSpeed().
	 *
	 * A vehicle progresses independent of it's movement direction.
	 * However different amounts of "progress" are needed for moving a step in a specific direction.
	 * That way the leftover progress does not need any adaption when changing movement direction.
	 *
	 * @param speed Direction-independent unscaled speed.
	 * @return speed, scaled to match #GetAdvanceDistance().
	 */
	static inline uint GetAdvanceSpeed(uint speed)
	{
		return speed * 3 / 4;
	}

	/**
	 * Determines the vehicle "progress" needed for moving a step.
	 *
	 * Together with #GetAdvanceSpeed() this function is a replacement for #GetOldAdvanceSpeed().
	 *
	 * @return distance to drive for a movement step on the map.
	 */
	inline uint GetAdvanceDistance()
	{
		return (this->direction & 1) ? 192 : 256;
	}

	/**
	 * Sets the expense type associated to this vehicle type
	 * @param income whether this is income or (running) expenses of the vehicle
	 */
	virtual ExpensesType GetExpenseType([[maybe_unused]] bool income) const { return EXPENSES_OTHER; }

	/**
	 * Play the sound associated with leaving the station
	 * @param force Should we play the sound even if sound effects are muted? (horn hotkey)
	 */
	virtual void PlayLeaveStationSound([[maybe_unused]] bool force = false) const {}

	/**
	 * Whether this is the primary vehicle in the chain.
	 */
	virtual bool IsPrimaryVehicle() const { return false; }

	const Engine *GetEngine() const;

	/**
	 * Gets the sprite to show for the given direction
	 * @param direction the direction the vehicle is facing
	 * @param[out] result Vehicle sprite sequence.
	 */
	virtual void GetImage([[maybe_unused]] Direction direction, [[maybe_unused]] EngineImageType image_type, [[maybe_unused]] VehicleSpriteSeq *result) const { result->Clear(); }

	Direction GetMapImageDirection() const { return this->direction; }

	const GRFFile *GetGRF() const;
	uint32_t GetGRFID() const;

	/**
	 * Invalidates cached NewGRF variables
	 * @see InvalidateNewGRFCacheOfChain
	 */
	inline void InvalidateNewGRFCache()
	{
		this->grf_cache.cache_valid = 0;
	}

	/**
	 * Invalidates cached NewGRF variables of all vehicles in the chain (after the current vehicle)
	 * @see InvalidateNewGRFCache
	 */
	inline void InvalidateNewGRFCacheOfChain()
	{
		for (Vehicle *u = this; u != nullptr; u = u->Next()) {
			u->InvalidateNewGRFCache();
		}
	}

	/**
	 * Invalidates cached image
	 * @see InvalidateNewGRFCacheOfChain
	 */
	inline void InvalidateImageCache()
	{
		this->cur_image_valid_dir = INVALID_DIR;
	}

	/**
	 * Invalidates cached image of all vehicles in the chain (after the current vehicle)
	 * @see InvalidateImageCache
	 */
	inline void InvalidateImageCacheOfChain()
	{
		ClrBit(this->vcache.cached_veh_flags, VCF_REDRAW_ON_SPEED_CHANGE);
		ClrBit(this->vcache.cached_veh_flags, VCF_REDRAW_ON_TRIGGER);
		ClrBit(this->vcache.cached_veh_flags, VCF_IMAGE_CURVATURE);
		for (Vehicle *u = this; u != nullptr; u = u->Next()) {
			u->InvalidateImageCache();
		}
	}

	/**
	 * Check if the vehicle is a ground vehicle.
	 * @return True iff the vehicle is a train or a road vehicle.
	 */
	debug_inline bool IsGroundVehicle() const
	{
		return this->type == VEH_TRAIN || this->type == VEH_ROAD;
	}

	/**
	 * Check if the vehicle type supports articulation.
	 * @return True iff the vehicle is a train, road vehicle or ship.
	 */
	debug_inline bool IsArticulatedCallbackVehicleType() const
	{
		return this->type == VEH_TRAIN || this->type == VEH_ROAD || this->type == VEH_SHIP;
	}

	/**
	 * Gets the speed in km-ish/h that can be sent into string parameters for string processing.
	 * @return the vehicle's speed
	 */
	virtual int GetDisplaySpeed() const { return 0; }

	/**
	 * Gets the maximum speed in km-ish/h that can be sent into string parameters for string processing.
	 * @return the vehicle's maximum speed
	 */
	virtual int GetDisplayMaxSpeed() const { return 0; }

	/**
	 * Calculates the maximum speed of the vehicle under its current conditions.
	 * @return Current maximum speed in native units.
	 */
	virtual int GetCurrentMaxSpeed() const { return 0; }

	/**
	 * Gets the running cost of a vehicle
	 * @return the vehicle's running cost
	 */
	virtual Money GetRunningCost() const { return 0; }

	/**
	 * Check whether the vehicle is in the depot.
	 * @return true if and only if the vehicle is in the depot.
	 */
	virtual bool IsInDepot() const { return false; }

	/**
	 * Check whether the whole vehicle chain is in the depot.
	 * @return true if and only if the whole chain is in the depot.
	 */
	virtual bool IsChainInDepot() const { return this->IsInDepot(); }

	/**
	 * Check whether the vehicle is in the depot *and* stopped.
	 * @return true if and only if the vehicle is in the depot and stopped.
	 */
	bool IsStoppedInDepot() const
	{
		assert(this == this->First());
		/* Free wagons have no VehState::Stopped state */
		if (this->IsPrimaryVehicle() && !this->vehstatus.Test(VehState::Stopped)) return false;
		return this->IsChainInDepot();
	}

	bool IsWaitingInDepot() const {
		assert(this == this->First());
		return this->current_order.IsType(OT_WAITING) && this->IsChainInDepot();
	}

	/**
	 * Calls the tick handler of the vehicle
	 * @return is this vehicle still valid?
	 */
	virtual bool Tick() { return true; };

	/**
	 * Calls the new day handler of the vehicle
	 */
	virtual void OnNewDay() {};

	/**
	 * Calls the periodic handler of the vehicle
	 * OnPeriodic is decoupled from OnNewDay at day lengths >= 8
	 */
	virtual void OnPeriodic() {};

	/**
	 * Crash the (whole) vehicle chain.
	 * @param flooded whether the cause of the crash is flooding or not.
	 * @return the number of lost souls.
	 */
	virtual uint Crash(bool flooded = false);

	/**
	 * Returns the Trackdir on which the vehicle is currently located.
	 * Works for trains and ships.
	 * Currently works only sortof for road vehicles, since they have a fuzzy
	 * concept of being "on" a trackdir. Dunno really what it returns for a road
	 * vehicle that is halfway a tile, never really understood that part. For road
	 * vehicles that are at the beginning or end of the tile, should just return
	 * the diagonal trackdir on which they are driving. I _think_.
	 * For other vehicles types, or vehicles with no clear trackdir (such as those
	 * in depots), returns 0xFF.
	 * @return the trackdir of the vehicle
	 */
	virtual Trackdir GetVehicleTrackdir() const { return INVALID_TRACKDIR; }

	Money GetDisplayRunningCost() const;

	/**
	 * Gets the profit vehicle had this year. It can be sent into string parameters for string processing.
	 * @return the vehicle's profit this year
	 */
	Money GetDisplayProfitThisYear() const { return (this->profit_this_year >> 8); }

	/**
	 * Gets the profit vehicle had last year. It can be sent into string parameters for string processing.
	 * @return the vehicle's profit last year
	 */
	Money GetDisplayProfitLastYear() const { return (this->profit_last_year >> 8); }

	/**
	 * Gets the lifetime profit of vehicle. It can be sent into string parameters for string processing.
	 * @return the vehicle's lifetime profit
	 */
	Money GetDisplayProfitLifetime() const { return ((this->profit_lifetime + this->profit_this_year) >> 8); }

	void SetNext(Vehicle *next);
	inline void SetFirst(Vehicle *f) { this->first = f; }

	/**
	 * Get the next vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the next vehicle or nullptr when there isn't a next vehicle.
	 */
	inline Vehicle *Next() const { return this->next; }

	/**
	 * Get the previous vehicle of this vehicle.
	 * @note articulated parts are also counted as vehicles.
	 * @return the previous vehicle or nullptr when there isn't a previous vehicle.
	 */
	inline Vehicle *Previous() const { return this->previous; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *First() const { return this->first; }

	/**
	 * Get the last vehicle of this vehicle chain.
	 * @return the last vehicle of the chain.
	 */
	inline Vehicle *Last()
	{
		Vehicle *v = this;
		while (v->Next() != nullptr) v = v->Next();
		return v;
	}

	/**
	 * Get the last vehicle of this vehicle chain.
	 * @return the last vehicle of the chain.
	 */
	inline const Vehicle *Last() const
	{
		const Vehicle *v = this;
		while (v->Next() != nullptr) v = v->Next();
		return v;
	}

	/**
	 * Get the next vehicle in the tile hash chain.
	 * @return the next vehicle in the tile hash chain or nullptr when there isn't a next vehicle.
	 */
	inline Vehicle *HashTileNext() const { return this->hash_tile_next; }

	/**
	 * Get the vehicle at offset \a n of this vehicle chain.
	 * @param n Offset from the current vehicle.
	 * @return The new vehicle or nullptr if the offset is out-of-bounds.
	 */
	inline Vehicle *Move(int n)
	{
		Vehicle *v = this;
		if (n < 0) {
			for (int i = 0; i != n && v != nullptr; i--) v = v->Previous();
		} else {
			for (int i = 0; i != n && v != nullptr; i++) v = v->Next();
		}
		return v;
	}

	/**
	 * Get the vehicle at offset \a n of this vehicle chain.
	 * @param n Offset from the current vehicle.
	 * @return The new vehicle or nullptr if the offset is out-of-bounds.
	 */
	inline const Vehicle *Move(int n) const
	{
		const Vehicle *v = this;
		if (n < 0) {
			for (int i = 0; i != n && v != nullptr; i--) v = v->Previous();
		} else {
			for (int i = 0; i != n && v != nullptr; i++) v = v->Next();
		}
		return v;
	}

	/**
	 * Get the first order of the vehicles order list.
	 * @return first order of order list.
	 */
	inline Order *GetFirstOrder() const { return (this->orders == nullptr) ? nullptr : this->orders->GetFirstOrder(); }

	/**
	 * Clears this vehicle's separation status
	 */
	inline void ClearSeparation() { this->vehicle_flags.Reset(VehicleFlag::SeparationActive); }

	void AddToShared(Vehicle *shared_chain);
	void RemoveFromShared();

	/**
	 * Get the next vehicle of the shared vehicle chain.
	 * @return the next shared vehicle or nullptr when there isn't a next vehicle.
	 */
	inline Vehicle *NextShared() const { return this->next_shared; }

	/**
	 * Get the previous vehicle of the shared vehicle chain
	 * @return the previous shared vehicle or nullptr when there isn't a previous vehicle.
	 */
	inline Vehicle *PreviousShared() const { return this->previous_shared; }

	/**
	 * Get the first vehicle of this vehicle chain.
	 * @return the first vehicle of the chain.
	 */
	inline Vehicle *FirstShared() const { return (this->orders == nullptr) ? this->First() : this->orders->GetFirstSharedVehicle(); }

	/**
	 * Check if we share our orders with another vehicle.
	 * @return true if there are other vehicles sharing the same order
	 */
	inline bool IsOrderListShared() const { return this->orders != nullptr && this->orders->IsShared(); }

	/**
	 * Get the number of orders this vehicle has.
	 * @return the number of orders this vehicle has.
	 */
	inline VehicleOrderID GetNumOrders() const { return (this->orders == nullptr) ? 0 : this->orders->GetNumOrders(); }

	/**
	 * Get the number of manually added orders this vehicle has.
	 * @return the number of manually added orders this vehicle has.
	 */
	inline VehicleOrderID GetNumManualOrders() const { return (this->orders == nullptr) ? 0 : this->orders->GetNumManualOrders(); }

	/**
	 * Get the next station the vehicle will stop at.
	 * @return ID of the next station the vehicle will stop at or StationID::Invalid().
	 */
	inline CargoStationIDStackSet GetNextStoppingStation() const
	{
		CargoStationIDStackSet set;
		if (this->orders != nullptr) set.FillNextStoppingStation(this, this->orders);
		return set;
	}

	/**
	 * Get the next station the vehicle will stop at.
	 * @return ID of the next station the vehicle will stop at or StationID::Invalid().
	 */
	inline StationIDStack GetNextStoppingStationCargoIndependent() const
	{
		StationIDStack set;
		if (this->orders != nullptr) set = this->orders->GetNextStoppingStation(this, 0).station;
		return set;
	}

	void RecalculateOrderOccupancyAverage();

	inline uint8_t GetOrderOccupancyAverage() const
	{
		if (order_occupancy_average == 0) const_cast<Vehicle *>(this)->RecalculateOrderOccupancyAverage();
		return this->order_occupancy_average;
	}

	void ResetRefitCaps();

	void ReleaseUnitNumber();

	/**
	 * Copy certain configurations and statistics of a vehicle after successful autoreplace/renew
	 * The function shall copy everything that cannot be copied by a command (like orders / group etc),
	 * and that shall not be resetted for the new vehicle.
	 * @param src The old vehicle
	 */
	inline void CopyVehicleConfigAndStatistics(Vehicle *src)
	{
		this->CopyConsistPropertiesFrom(src);

		this->ReleaseUnitNumber();
		this->unitnumber = src->unitnumber;

		this->current_order = src->current_order;
		this->dest_tile  = src->dest_tile;

		this->profit_this_year = src->profit_this_year;
		this->profit_last_year = src->profit_last_year;
		this->profit_lifetime = -this->profit_this_year;

		this->current_loading_time = src->current_loading_time;

		if (src->vehicle_flags.Test(VehicleFlag::TimetableStarted)) this->vehicle_flags.Set(VehicleFlag::TimetableStarted);
		if (src->vehicle_flags.Test(VehicleFlag::AutofillTimetable)) this->vehicle_flags.Set(VehicleFlag::AutofillTimetable);
		if (src->vehicle_flags.Test(VehicleFlag::AutofillPreserveWaitTime)) this->vehicle_flags.Set(VehicleFlag::AutofillPreserveWaitTime);

		this->service_interval = src->service_interval;

		src->unitnumber = 0;
	}


	bool HandleBreakdown();

	bool NeedsAutorenewing(const Company *c, bool use_renew_setting = true) const;

	bool NeedsServicing() const;
	bool NeedsAutomaticServicing() const;

	/**
	 * Determine the location for the station where the vehicle goes to next.
	 * Things done for example are allocating slots in a road stop or exact
	 * location of the platform is determined for ships.
	 * @param station the station to make the next location of the vehicle.
	 * @return the location (tile) to aim for.
	 */
	virtual TileIndex GetOrderStationLocation([[maybe_unused]] StationID station) { return INVALID_TILE; }

	virtual TileIndex GetCargoTile() const { return this->tile; }

	/**
	 * Find the closest depot for this vehicle and tell us the location,
	 * DestinationID and whether we should reverse.
	 * @return A structure with information about the closest depot, if found.
	 */
	virtual ClosestDepot FindClosestDepot() { return {}; }

	virtual void SetDestTile(TileIndex tile) { this->dest_tile = tile; }

	CommandCost SendToDepot(DoCommandFlags flags, DepotCommandFlags command, TileIndex specific_depot = {});

	void UpdateVisualEffect(bool allow_power_change = true);
	void ShowVisualEffect(uint max_speed) const;

	/**
	 * Update the position of the vehicle. This will update the hash that tells
	 *  which vehicles are on a tile.
	 */
	void UpdatePosition()
	{
		extern void UpdateVehicleTileHash(Vehicle *v, bool remove);
		if (this->type < VEH_COMPANY_END) UpdateVehicleTileHash(this, false);
	}

	void UpdateViewport(bool dirty);
	void UpdateViewportDeferred();
	void UpdatePositionAndViewport();
	void MarkAllViewportsDirty() const;

	inline uint16_t GetServiceInterval() const { return this->service_interval; }

	inline void SetServiceInterval(uint16_t interval) { this->service_interval = interval; }

	inline bool ServiceIntervalIsCustom() const { return this->vehicle_flags.Test(VehicleFlag::ServiceIntervalIsCustom); }

	inline bool ServiceIntervalIsPercent() const { return this->vehicle_flags.Test(VehicleFlag::ServiceIntervalIsPercent); }

	inline void SetServiceIntervalIsCustom(bool on) { this->vehicle_flags.Set(VehicleFlag::ServiceIntervalIsCustom, on); }

	inline void SetServiceIntervalIsPercent(bool on) { this->vehicle_flags.Set(VehicleFlag::ServiceIntervalIsPercent, on); }

	inline void ResetDepotUnbunching() { this->unbunch_state.reset(); }

	bool HasFullLoadOrder() const;
	bool HasConditionalOrder() const;
	bool HasUnbunchingOrder() const;
	void LeaveUnbunchingDepot();
	bool IsWaitingForUnbunching() const;

	VehicleOrderID GetFirstWaitingLocation(bool require_wait_timetabled) const;

private:
	/**
	 * Advance cur_real_order_index to the next real order.
	 * cur_implicit_order_index is not touched.
	 */
	void SkipToNextRealOrderIndex()
	{
		if (this->GetNumManualOrders() > 0) {
			/* Advance to next real order */
			do {
				this->cur_real_order_index++;
				if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;
			} while (this->GetOrder(this->cur_real_order_index)->IsType(OT_IMPLICIT));
			this->cur_timetable_order_index = this->cur_real_order_index;
		} else {
			this->cur_real_order_index = 0;
			this->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
		}
	}

public:
	/**
	 * Increments cur_implicit_order_index, keeps care of the wrap-around and invalidates the GUI.
	 * cur_real_order_index is incremented as well, if needed.
	 * Note: current_order is not invalidated.
	 */
	void IncrementImplicitOrderIndex()
	{
		if (this->cur_implicit_order_index == this->cur_real_order_index) {
			/* Increment real order index as well */
			this->SkipToNextRealOrderIndex();
		}

		assert(this->cur_real_order_index == 0 || this->cur_real_order_index < this->GetNumOrders());

		/* Advance to next implicit order */
		do {
			this->cur_implicit_order_index++;
			if (this->cur_implicit_order_index >= this->GetNumOrders()) this->cur_implicit_order_index = 0;
		} while (this->cur_implicit_order_index != this->cur_real_order_index && !this->GetOrder(this->cur_implicit_order_index)->IsType(OT_IMPLICIT));

		InvalidateVehicleOrder(this, 0);
	}

	/**
	 * Advanced cur_real_order_index to the next real order, keeps care of the wrap-around and invalidates the GUI.
	 * cur_implicit_order_index is incremented as well, if it was equal to cur_real_order_index, i.e. cur_real_order_index is skipped
	 * but not any implicit orders.
	 * Note: current_order is not invalidated.
	 */
	void IncrementRealOrderIndex()
	{
		if (this->cur_implicit_order_index == this->cur_real_order_index) {
			/* Increment both real and implicit order */
			this->IncrementImplicitOrderIndex();
		} else {
			/* Increment real order only */
			this->SkipToNextRealOrderIndex();
			InvalidateVehicleOrder(this, 0);
		}
	}

	/**
	 * Skip implicit orders until cur_real_order_index is a non-implicit order.
	 */
	void UpdateRealOrderIndex()
	{
		/* Make sure the index is valid */
		if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;

		if (this->GetNumManualOrders() > 0) {
			/* Advance to next real order */
			while (this->GetOrder(this->cur_real_order_index)->IsType(OT_IMPLICIT)) {
				this->cur_real_order_index++;
				if (this->cur_real_order_index >= this->GetNumOrders()) this->cur_real_order_index = 0;
			}
		} else {
			this->cur_real_order_index = 0;
		}
	}

	/**
	 * Returns order 'index' of a vehicle or nullptr when it doesn't exists
	 * @param index the order to fetch
	 * @return the found (or not) order
	 */
	inline const Order *GetOrder(VehicleOrderID index) const
	{
		return (this->orders == nullptr) ? nullptr : this->orders->GetOrderAt(index);
	}

	inline Order *GetOrder(VehicleOrderID index) { return const_cast<Order *>(const_cast<const Vehicle *>(this)->GetOrder(index)); }

	/**
	 * Get the index of an order of the order chain, or INVALID_VEH_ORDER_ID.
	 * @param order order to get the index of.
	 * @return the position index of the given order, or INVALID_VEH_ORDER_ID.
	 */
	inline VehicleOrderID GetIndexOfOrder(const Order *order) const
	{
		return (this->orders == nullptr) ? INVALID_VEH_ORDER_ID : this->orders->GetIndexOfOrder(order);
	}

	/**
	 * Returns the last order of a vehicle, or nullptr if it doesn't exists
	 * @return last order of a vehicle, if available
	 */
	inline const Order *GetLastOrder() const
	{
		return (this->orders == nullptr) ? nullptr : this->orders->GetLastOrder();
	}

	bool IsEngineCountable() const;
	bool HasEngineType() const;
	bool HasDepotOrder() const;
	void HandlePathfindingResult(bool path_found);

	/**
	 * Check if the vehicle is a front engine.
	 * @return Returns true if the vehicle is a front engine.
	 */
	debug_inline bool IsFrontEngine() const
	{
		return this->IsGroundVehicle() && HasBit(this->subtype, GVSF_FRONT);
	}

	/**
	 * Check if the vehicle is an articulated part of an engine.
	 * @return Returns true if the vehicle is an articulated part.
	 */
	inline bool IsArticulatedPart() const
	{
		return this->IsGroundVehicle() && HasBit(this->subtype, GVSF_ARTICULATED_PART);
	}

	/**
	 * Check if an engine has an articulated part.
	 * @return True if the engine has an articulated part.
	 */
	inline bool HasArticulatedPart() const
	{
		return this->Next() != nullptr && this->Next()->IsArticulatedPart();
	}

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline Vehicle *GetNextArticulatedPart() const
	{
		assert(this->HasArticulatedPart());
		return this->Next();
	}

	inline uint GetEnginePartsCount() const
	{
		uint count = 1;
		const Vehicle *v = this->Next();
		while (v != nullptr && v->IsArticulatedPart()) {
			count++;
			v = v->Next();
		}
		return count;
	}

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline Vehicle *GetFirstEnginePart()
	{
		Vehicle *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline const Vehicle *GetFirstEnginePart() const
	{
		const Vehicle *v = this;
		while (v->IsArticulatedPart()) v = v->Previous();
		return v;
	}

	/**
	 * Get the last part of an articulated engine.
	 * @return Last part of the engine.
	 */
	inline Vehicle *GetLastEnginePart()
	{
		Vehicle *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart();
		return v;
	}

	/**
	 * Get the next real (non-articulated part) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	inline Vehicle *GetNextVehicle() const
	{
		const Vehicle *v = this;
		while (v->HasArticulatedPart()) v = v->GetNextArticulatedPart();

		/* v now contains the last articulated part in the engine */
		return v->Next();
	}

	/**
	 * Get the previous real (non-articulated part) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	inline Vehicle *GetPrevVehicle() const
	{
		Vehicle *v = this->Previous();
		while (v != nullptr && v->IsArticulatedPart()) v = v->Previous();

		return v;
	}

	bool IsDrawn() const
	{
		return HasBit(this->vcache.cached_veh_flags, VCF_IS_DRAWN);
	}

	void UpdateIsDrawn();

	inline void UpdateSpriteSeqBound()
	{
		this->sprite_seq_bounds = this->sprite_seq.GetBounds();
	}

	void DumpVehicleFlags(struct format_target &buffer, bool include_tile) const;
	void DumpVehicleFlagsMultiline(struct format_target &buffer, const char *base_indent, const char *extra_indent) const;

	/**
	 * Returns an iterable ensemble of orders of a vehicle
	 * @return an iterable ensemble of orders of a vehicle
	 */
	OrderIterateWrapper<const Order> Orders(VehicleOrderID from = 0) const
	{
		return this->orders == nullptr ? OrderIterateWrapper<const Order>(nullptr, nullptr) : const_cast<const OrderList *>(this->orders)->Orders(from);
	}

	OrderIterateWrapper<Order> Orders(VehicleOrderID from = 0)
	{
		return this->orders == nullptr ? OrderIterateWrapper<Order>(nullptr, nullptr) : this->orders->Orders(from);
	}

	uint32_t GetDisplayMaxWeight() const;
	uint32_t GetDisplayMinPowerToWeight() const;

	struct VehicleTypeFilter {
		VehicleType vt;

		bool operator() (size_t index)
		{
#if OTTD_UPPER_TAGGED_PTR
			return VehiclePoolOps::GetVehicleType(_vehicle_pool.GetRaw(index)) == this->vt;
#else
			return Vehicle::Get(index)->type == this->vt;
#endif
		}
	};

	struct VehicleFrontOnlyFilter {
		bool operator() (size_t index)
		{
#if OTTD_UPPER_TAGGED_PTR
			return !VehiclePoolOps::IsNonFrontVehiclePtr(_vehicle_pool.GetRaw(index));
#else
			return Vehicle::Get(index)->Previous() == nullptr;
#endif
		}
	};

	struct VehicleFrontOnlyTypeFilter {
		VehicleType vt;

		bool operator() (size_t index)
		{
#if OTTD_UPPER_TAGGED_PTR
			uintptr_t vptr = _vehicle_pool.GetRaw(index);
			return !VehiclePoolOps::IsNonFrontVehiclePtr(vptr) && VehiclePoolOps::GetVehicleType(vptr) == this->vt;
#else
			const Vehicle *v = Vehicle::Get(index);
			return v->type == this->vt && v->Previous() == nullptr;
#endif
		}
	};

	struct VehicleFrontOnlyTypeMaskFilter {
		VehicleTypeMask vt_mask;

		bool operator() (size_t index)
		{
#if OTTD_UPPER_TAGGED_PTR
			uintptr_t vptr = _vehicle_pool.GetRaw(index);
			return !VehiclePoolOps::IsNonFrontVehiclePtr(vptr) && HasBit(this->vt_mask, VehiclePoolOps::GetVehicleType(vptr));
#else
			const Vehicle *v = Vehicle::Get(index);
			return HasBit(this->vt_mask, v->type) && v->Previous() == nullptr;
#endif
		}
	};

	/**
	 * Returns an iterable ensemble of all valid vehicles of the given type
	 * @param vt the VehicleType to filter
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid vehicles of the given type
	 */
	static Pool::IterateWrapperFiltered<Vehicle, VehicleTypeFilter> IterateType(VehicleType vt, size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<Vehicle, VehicleTypeFilter>(from, VehicleTypeFilter{ vt });
	}

	/**
	 * Returns an iterable ensemble of all valid front vehicles (i.e. Previous() == nullptr)
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid front vehicles
	 */
	static Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyFilter> IterateFrontOnly(size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyFilter>(from, VehicleFrontOnlyFilter{});
	}

	/**
	 * Returns an iterable ensemble of all valid front vehicles of the given type
	 * @param vt the VehicleType to filter
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid front vehicles of the given type
	 */
	static Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyTypeFilter> IterateTypeFrontOnly(VehicleType vt, size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyTypeFilter>(from, VehicleFrontOnlyTypeFilter{ vt });
	}

	/**
	 * Returns an iterable ensemble of all valid front vehicles of the given type
	 * @param VehicleTypeMask the set of VehicleType to filter, as a VehicleTypeMask
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid front vehicles of the given type
	 */
	static Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyTypeMaskFilter> IterateTypeMaskFrontOnly(VehicleTypeMask vt_mask, size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<Vehicle, VehicleFrontOnlyTypeMaskFilter>(from, VehicleFrontOnlyTypeMaskFilter{ vt_mask });
	}
};

inline bool IsPointInViewportVehicleRedrawArea(const std::vector<Rect> &viewport_redraw_rects, const Point &pt)
{
	for (const Rect &r : viewport_redraw_rects) {
		if (pt.x >= r.left &&
				pt.x <= r.right &&
				pt.y >= r.top &&
				pt.y <= r.bottom) {
			return true;
		}
	}
	return false;
}

/**
 * Class defining several overloaded accessors so we don't
 * have to cast vehicle types that often
 */
template <class T, VehicleType Type>
struct SpecializedVehicle : public Vehicle {
	static const VehicleType EXPECTED_TYPE = Type; ///< Specialized type

	typedef SpecializedVehicle<T, Type> SpecializedVehicleBase; ///< Our type

#if OTTD_UPPER_TAGGED_PTR
	inline void *operator new(size_t size)
	{
		return Vehicle::NewWithParam(size, Type);
	}

	inline void *operator new(size_t size, VehicleID index)
	{
		return Vehicle::NewWithParam(size, index.base(), Type);
	}

	inline void operator delete(void *p)
	{
		Vehicle::operator delete(p);
	}

	void *operator new(size_t, void *ptr) = delete;
#endif

	/**
	 * Set vehicle type correctly
	 */
	inline SpecializedVehicle() : Vehicle(Type)
	{
		this->sprite_seq.count = 1;
	}

	/**
	 * Get the first vehicle in the chain
	 * @return first vehicle in the chain
	 */
	inline T *First() const { return (T *)this->Vehicle::First(); }

	/**
	 * Get the last vehicle in the chain
	 * @return last vehicle in the chain
	 */
	inline T *Last() { return (T *)this->Vehicle::Last(); }

	/**
	 * Get the last vehicle in the chain
	 * @return last vehicle in the chain
	 */
	inline const T *Last() const { return (const T *)this->Vehicle::Last(); }

	/**
	 * Get next vehicle in the chain
	 * @return next vehicle in the chain
	 */
	inline T *Next() const { return (T *)this->Vehicle::Next(); }

	/**
	 * Get previous vehicle in the chain
	 * @return previous vehicle in the chain
	 */
	inline T *Previous() const { return (T *)this->Vehicle::Previous(); }

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline T *GetNextArticulatedPart() { return (T *)this->Vehicle::GetNextArticulatedPart(); }

	/**
	 * Get the next part of an articulated engine.
	 * @return Next part of the articulated engine.
	 * @pre The vehicle is an articulated engine.
	 */
	inline T *GetNextArticulatedPart() const { return (T *)this->Vehicle::GetNextArticulatedPart(); }

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline T *GetFirstEnginePart() { return (T *)this->Vehicle::GetFirstEnginePart(); }

	/**
	 * Get the first part of an articulated engine.
	 * @return First part of the engine.
	 */
	inline const T *GetFirstEnginePart() const { return (const T *)this->Vehicle::GetFirstEnginePart(); }

	/**
	 * Get the last part of an articulated engine.
	 * @return Last part of the engine.
	 */
	inline T *GetLastEnginePart() { return (T *)this->Vehicle::GetLastEnginePart(); }

	/**
	 * Get the next real (non-articulated part) vehicle in the consist.
	 * @return Next vehicle in the consist.
	 */
	inline T *GetNextVehicle() const { return (T *)this->Vehicle::GetNextVehicle(); }

	/**
	 * Get the previous real (non-articulated part) vehicle in the consist.
	 * @return Previous vehicle in the consist.
	 */
	inline T *GetPrevVehicle() const { return (T *)this->Vehicle::GetPrevVehicle(); }

	/**
	 * Get the next vehicle in the tile hash chain.
	 * @return the next vehicle in the tile hash chain or nullptr when there isn't a next vehicle.
	 */
	inline T *HashTileNext() const { return (T *)this->Vehicle::HashTileNext(); }

	/**
	 * Tests whether given index is a valid index for vehicle of this type
	 * @param index tested index
	 * @return is this index valid index of T?
	 */
	static inline bool IsValidID(auto index)
	{
#if OTTD_UPPER_TAGGED_PTR
		return Vehicle::IsValidID(index) && VehiclePoolOps::GetVehicleType(_vehicle_pool.GetRaw(GetRawIndex(index))) == Type;
#else
		return Vehicle::IsValidID(index) && Vehicle::Get(index)->type == Type;
#endif
	}

	/**
	 * Gets vehicle with given index
	 * @return pointer to vehicle with given index casted to T *
	 */
	static inline T *Get(auto index)
	{
		return (T *)Vehicle::Get(index);
	}

	/**
	 * Returns vehicle if the index is a valid index for this vehicle type
	 * @return pointer to vehicle with given index if it's a vehicle of this type
	 */
	static inline T *GetIfValid(auto index)
	{
		return IsValidID(index) ? Get(index) : nullptr;
	}

	/**
	 * Converts a Vehicle to SpecializedVehicle with type checking.
	 * @param v Vehicle pointer
	 * @return pointer to SpecializedVehicle
	 */
	static inline T *From(Vehicle *v)
	{
		assert(v->type == Type);
		return (T *)v;
	}

	/**
	 * Converts a const Vehicle to const SpecializedVehicle with type checking.
	 * @param v Vehicle pointer
	 * @return pointer to SpecializedVehicle
	 */
	static inline const T *From(const Vehicle *v)
	{
		assert(v->type == Type);
		return (const T *)v;
	}

private:
	inline uint16_t GetVehicleCurvature() const
	{
		uint16_t curvature = 0;
		if (this->Previous() != nullptr) {
			SB(curvature, 0, 4, this->Previous()->direction);
			if (this->Previous()->Previous() != nullptr) SB(curvature, 4, 4, this->Previous()->Previous()->direction);
		}
		if (this->Next() != nullptr) {
			SB(curvature, 8, 4, this->Next()->direction);
			if (this->Next()->Next() != nullptr) SB(curvature, 12, 4, this->Next()->Next()->direction);
		}
		return curvature;
	}

	inline bool CheckVehicleCurvature() const {
		if (!(EXPECTED_TYPE == VEH_TRAIN || EXPECTED_TYPE == VEH_ROAD)) return false;
		if (likely(!HasBit(this->vcache.cached_veh_flags, VCF_IMAGE_CURVATURE))) return false;
		return this->vcache.cached_image_curvature != this->GetVehicleCurvature();
	};

public:
	inline void UpdateImageState(Direction current_direction, VehicleSpriteSeq &seq)
	{
		ClrBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH);
		_sprite_group_resolve_check_veh_check = true;
		if (EXPECTED_TYPE == VEH_TRAIN || EXPECTED_TYPE == VEH_ROAD) _sprite_group_resolve_check_veh_curvature_check = true;
		((T *)this)->T::GetImage(current_direction, EIT_ON_MAP, &seq);
		if (EXPECTED_TYPE == VEH_TRAIN || EXPECTED_TYPE == VEH_ROAD) {
			AssignBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH_NEXT, !_sprite_group_resolve_check_veh_check);
			if (unlikely(!_sprite_group_resolve_check_veh_curvature_check)) {
				SetBit(this->vcache.cached_veh_flags, VCF_IMAGE_CURVATURE);
				this->vcache.cached_image_curvature = this->GetVehicleCurvature();
			}
			_sprite_group_resolve_check_veh_curvature_check = false;
			this->cur_image_valid_dir = current_direction;
		} else {
			this->cur_image_valid_dir = _sprite_group_resolve_check_veh_check ? current_direction : INVALID_DIR;
		}
		_sprite_group_resolve_check_veh_check = false;
	}

	inline void UpdateImageStateUsingMapDirection(VehicleSpriteSeq &seq)
	{
		this->UpdateImageState(((T *)this)->GetMapImageDirection(), seq);
	}

private:
	inline void UpdateViewportNormalViewportMode(bool force_update, Point pt)
	{
		const Direction current_direction = ((T *)this)->GetMapImageDirection();
		if (this->cur_image_valid_dir != current_direction || this->CheckVehicleCurvature()) {
			VehicleSpriteSeq seq;
			this->UpdateImageState(current_direction, seq);
			if (force_update || this->sprite_seq != seq) {
				this->sprite_seq = seq;
				this->UpdateSpriteSeqBound();
				this->Vehicle::UpdateViewport(true);
			}
		} else {
			if ((EXPECTED_TYPE == VEH_TRAIN || EXPECTED_TYPE == VEH_ROAD) && HasBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH_NEXT)) {
				SetBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH);
			}
			if (force_update) {
				this->Vehicle::UpdateViewport(true);
			}
		}
	}

public:
	/**
	 * Update vehicle sprite- and position caches
	 * @param force_update Force updating the vehicle on the viewport.
	 * @param update_delta Also update the delta?
	 */
	inline void UpdateViewport(bool force_update, bool update_delta)
	{
		/* Skip updating sprites on dedicated servers without screen */
		if (IsHeadless()) return;

		/* Explicitly choose method to call to prevent vtable dereference -
		 * it gives ~3% runtime improvements in games with many vehicles */
		if (update_delta) ((T *)this)->T::UpdateDeltaXY();

		extern std::vector<Rect> _viewport_vehicle_normal_redraw_rects;
		extern std::vector<Rect> _viewport_vehicle_map_redraw_rects;

		Point pt = RemapCoords(this->x_pos + this->x_offs, this->y_pos + this->y_offs, this->z_pos);
		if (EXPECTED_TYPE >= VEH_COMPANY_END || IsPointInViewportVehicleRedrawArea(_viewport_vehicle_normal_redraw_rects, pt)) {
			UpdateViewportNormalViewportMode(force_update, pt);
			return;
		}

		bool always_update_viewport = false;

		if (EXPECTED_TYPE == VEH_SHIP && update_delta) {
			extern bool RecentreShipSpriteBounds(Vehicle *v);
			always_update_viewport = RecentreShipSpriteBounds(this);
		}

		SetBit(this->vcache.cached_veh_flags, VCF_IMAGE_REFRESH);

		if (force_update) {
			this->Vehicle::UpdateViewport(IsPointInViewportVehicleRedrawArea(_viewport_vehicle_map_redraw_rects, pt));
		} else if (always_update_viewport) {
			this->Vehicle::UpdateViewport(false);
		}
	}

	/**
	 * Returns an iterable ensemble of all valid vehicles of type T
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid vehicles of type T
	 */
	static Pool::IterateWrapper<T> Iterate(size_t from = 0) { return Pool::IterateWrapper<T>(from); }

	/**
	 * Returns an iterable ensemble of all valid front vehicles (i.e. Previous() == nullptr) of type T
	 * @param from index of the first vehicle to consider
	 * @return an iterable ensemble of all valid front vehicles of type T
	 */
	static Pool::IterateWrapperFiltered<T, VehicleFrontOnlyFilter> IterateFrontOnly(size_t from = 0)
	{
		return Pool::IterateWrapperFiltered<T, VehicleFrontOnlyFilter>(from, VehicleFrontOnlyFilter{});
	}

private:
	static constexpr size_t GetRawIndex(size_t index) { return index; }
	template <typename S> requires std::is_base_of_v<PoolIDBase, S>
	static constexpr size_t GetRawIndex(const S &index) { return index.base(); }
};

/** Sentinel for an invalid coordinate. */
static const int32_t INVALID_COORD = 0x7fffffff;

inline void InvalidateVehicleTickCaches()
{
	extern bool _tick_caches_valid;
	_tick_caches_valid = false;
}

void ClearVehicleTickCaches();
void RemoveFromOtherVehicleTickCache(const Vehicle *v);
void UpdateAllVehiclesIsDrawn();

void ShiftVehicleDates(EconTime::DateDelta interval);

#endif /* VEHICLE_BASE_H */
