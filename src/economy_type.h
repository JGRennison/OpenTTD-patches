/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file economy_type.h Types related to the economy. */

#ifndef ECONOMY_TYPE_H
#define ECONOMY_TYPE_H

#include "core/overflowsafe_type.hpp"
#include "core/enum_type.hpp"
#include "core/pool_id_type.hpp"
#include <array>

typedef OverflowSafeInt64 Money;

/** Type of the game economy. */
enum class EconomyType : uint8_t {
	Begin, ///< The lowest valid value.
	Original = EconomyType::Begin, ///< Imitates original TTD economy.
	Smooth, ///< Makes production changes more often, and in smaller steps.
	Frozen, ///< Stops production changes and industry closures.
	End, ///< Economy type end marker.
};

/** Data of the economy. */
struct Economy {
	Money max_loan;                         ///< NOSAVE: Maximum possible loan
	int16_t fluct;                          ///< Economy fluctuation status
	uint8_t interest_rate;                  ///< Interest
	uint8_t infl_amount;                    ///< inflation amount
	uint8_t infl_amount_pr;                 ///< inflation rate for payment rates
	uint32_t industry_daily_change_counter; ///< Bits 31-16 are number of industry to be performed, 15-0 are fractional collected daily
	uint32_t industry_daily_increment;      ///< The value which will increment industry_daily_change_counter. Computed value. NOSAVE
	uint64_t inflation_prices;              ///< Cumulated inflation of prices since game start; 16 bit fractional part
	uint64_t inflation_payment;             ///< Cumulated inflation of cargo payment since game start; 16 bit fractional part

	/* Old stuff for savegame conversion only */
	Money old_max_loan_unround;           ///< Old: Unrounded max loan
	uint16_t old_max_loan_unround_fract;    ///< Old: Fraction of the unrounded max loan
};

/** Score categories in the detailed performance rating. */
enum class ScoreID : uint8_t {
	Begin, ///< The lowest valid value.
	Vehicles = ScoreID::Begin, ///< Number of vehicles that turned profit last year.
	Stations, ///< Number of recently-serviced stations.
	MinProfit, ///< The profit of the vehicle with the lowest income.
	MinIncome, ///< Income in the quater with the lowest profit of the last 12 quaters.
	MaxIncome, ///< Income in the quater with the highest profit of the last 12 quaters.
	Delivered, ///< Units of cargo delivered in the last four quaters.
	Cargo, ///< Number of types of cargo delivered in the last four quaters.
	Money, ///< Amount of money company has in the bank.
	Loan, ///< The amount of money company can take as a loan.
	Total, ///< Total points out of possible points ,must always be the last entry.
	End, ///< Score ID end marker.
};
DECLARE_INCREMENT_DECREMENT_OPERATORS(ScoreID)

/**
 * The max score that can be in the performance history.
 * The scores together of score_info is allowed to be more!
 */
static constexpr int SCORE_MAX = 1000;

/** Data structure for storing how the score is computed for a single score id. */
struct ScoreInfo {
	int score;  ///< How much score it will give
	int needed; ///< How much you need to get the perfect score
};

/**
 * Enumeration of all base prices for use with #Prices.
 * The prices are ordered as they are expected by NewGRF cost multipliers, so don't shuffle them.
 */
enum class Price : uint8_t {
	Begin, ///< The lowest valid value.
	StationValue = Price::Begin, ///< Stations value and additional constant company running fee.
	BuildRail, ///< Price for building rails.
	BuildRoad, ///< Price for building roads.
	BuildSignals, ///< Price for building rail signals.
	BuildBridge, ///< Price for building bridges.
	BuildDepotTrain, ///< Price for building train depots.
	BuildDepotRoad, ///< Price for building road vehicle depots.
	BuildDepotShip, ///< Price for building ship depots.
	BuildTunnel, ///< Price for building tunnels.
	BuildStationRail, ///< Price for building rail stations.
	BuildStationRailLength, ///< Additional price for building rail stations dependent on their length.
	BuildStationAirport, ///< Price for building airports.
	BuildStationBus, ///< Price for building bus stops.
	BuildStationTruck, ///< Price for building lorry stations.
	BuildStationDock, ///< Price for building docks.
	BuildVehicleTrain, ///< Price for purchasing new train engines.
	BuildVehicleWagon, ///< Price for purchasing new wagons.
	BuildVehicleAircraft, ///< Price for purchasing new aircrafts.
	BuildVehicleRoad, ///< Price for purchasing new road vehicles.
	BuildVehicleShip, ///< Price for purchasing new ships.
	BuildTrees, ///< Price for planting trees.
	Terraform, ///< Price for terraforming land, e.g. rising, lowering and flattening.
	ClearGrass, ///< Price for destroying grass.
	ClearRough, ///< Price for destroying rough land.
	ClearRocks, ///< Price for destroying rocks.
	ClearFields, ///< Price for destroying fields.
	ClearTrees, ///< Price for destroying trees.
	ClearRail, ///< Price for destroying rails.
	ClearSignals, ///< Price for destroying rail signals.
	ClearBridge, ///< Price for destroying bridges.
	ClearDepotTrain, ///< Price for destroying train depots.
	ClearDepotRoad, ///< Price for destroying road vehicle depots.
	ClearDepotShip, ///< Price for destroying ship depots.
	ClearTunnel, ///< Price for destroying tunnels.
	ClearWater, ///< Price for destroying water e.g. see, rives.
	ClearStationRail, ///< Price for destroying rail stations.
	ClearStationAirport, ///< Price for destroying airports.
	ClearStationBus, ///< Price for destroying bus stops.
	ClearStationTruck, ///< Price for destroying lorry stations.
	ClearStationDock, ///< Price for destroying docks.
	ClearHouse, ///< Price for destroying houses and other town buildings.
	ClearRoad, ///< Price for destroying roads.
	RunningTrainSteam, ///< Running cost of steam trains.
	RunningTrainDiesel, ///< Running cost of diesel trains.
	RunningTrainElectric, ///< Running cost of electric trains.
	RunningAircraft, ///< Running cost of aircrafts.
	RunningRoadveh, ///< Running cost of road vehicles.
	RunningShip, ///< Running cost of ships.
	BuildIndustry, ///< Price for funding new industries.
	ClearIndustry, ///< Price for destroying industries.
	BuildObject, ///< Price for building new objects.
	ClearObject, ///< Price for destroying objects.
	BuildWaypointRail, ///< Price for building new rail waypoints.
	ClearWaypointRail, ///< Price for destroying rail waypoints.
	BuildWaypointBuoy, ///< Price for building new buoys.
	ClearWaypointBuoy, ///< Price for destroying buoys.
	TownAction, ///< Price for interaction with local authorities.
	BuildFoundation, ///< Price for building foundation under other constructions e.g. roads, rails, depots, objects, etc., etc..
	BuildIndustryRaw, ///< Price for funding new raw industries, e.g. coal mine, forest.
	BuildTown, ///< Price for funding new towns and cities.
	BuildCanal, ///< Price for building new canals.
	ClearCanal, ///< Price for destroying canals.
	BuildAqueduct, ///< Price for building new aqueducts.
	ClearAqueduct, ///< Price for destroying aqueducts.
	BuildLock, ///< Price for building new locks.
	ClearLock, ///< Price for destroying locks.
	InfrastructureRail, ///< Rails maintenance cost.
	InfrastructureRoad, ///< Roads maintenance cost.
	InfrastructureWater, ///< Canals maintenance cost.
	InfrastructureStation, ///< Stations maintenance cost.
	InfrastructureAirport, ///< Airports maintenance cost.
	End, ///< Price base end marker.
	Invalid = 0xFF ///< Invalid base price.
};
DECLARE_INCREMENT_DECREMENT_OPERATORS(Price)

using Prices = EnumClassIndexContainer<std::array<Money,to_underlying(Price::End)>, Price>; ///< Prices of everything. @see Price
using PriceMultipliers = EnumClassIndexContainer<std::array<int8_t, to_underlying(Price::End)>, Price>;

/** Types of expenses. */
enum ExpensesType : uint8_t {
	EXPENSES_CONSTRUCTION =  0,   ///< Construction costs.
	EXPENSES_NEW_VEHICLES,        ///< New vehicles.
	EXPENSES_TRAIN_RUN,           ///< Running costs trains.
	EXPENSES_ROADVEH_RUN,         ///< Running costs road vehicles.
	EXPENSES_AIRCRAFT_RUN,        ///< Running costs aircraft.
	EXPENSES_SHIP_RUN,            ///< Running costs ships.
	EXPENSES_PROPERTY,            ///< Property costs.
	EXPENSES_TRAIN_REVENUE,       ///< Revenue from trains.
	EXPENSES_ROADVEH_REVENUE,     ///< Revenue from road vehicles.
	EXPENSES_AIRCRAFT_REVENUE,    ///< Revenue from aircraft.
	EXPENSES_SHIP_REVENUE,        ///< Revenue from ships.
	EXPENSES_LOAN_INTEREST,       ///< Interest payments over the loan.
	EXPENSES_OTHER,               ///< Other expenses.
	EXPENSES_SHARING_COST,        ///< Infrastructure sharing costs
	EXPENSES_SHARING_INC,         ///< Infrastructure sharing income
	EXPENSES_END,                 ///< Number of expense types.
	INVALID_EXPENSES      = 0xFF, ///< Invalid expense type.
};

/**
 * Data type for storage of Money for each #ExpensesType category.
 */
using Expenses = std::array<Money, EXPENSES_END>;

/**
 * Categories of a price bases.
 */
enum PriceCategory : uint8_t {
	PCAT_NONE,         ///< Not affected by difficulty settings
	PCAT_RUNNING,      ///< Price is affected by "vehicle running cost" difficulty setting
	PCAT_CONSTRUCTION, ///< Price is affected by "construction cost" difficulty setting
};

/** The "steps" in loan size, in British Pounds! */
static const int LOAN_INTERVAL = 10000;
/** The size of loan for a new company, in British Pounds! */
static const int64_t INITIAL_LOAN = 100000;
/** The max amount possible to configure for a max loan of a company. */
static const int64_t MAX_LOAN_LIMIT = 2000000000;

/**
 * Maximum inflation (including fractional part) without causing overflows in int64_t price computations.
 * This allows for 32 bit base prices (21 are currently needed).
 * Considering the sign bit and 16 fractional bits, there are 15 bits left.
 * 170 years of 4% inflation result in a inflation of about 822, so 10 bits are actually enough.
 * Note that NewGRF multipliers share the 16 fractional bits.
 * @see MAX_PRICE_MODIFIER
 */
static const uint64_t MAX_INFLATION = (1ull << (63 - 32)) - 1;

/**
 * Maximum NewGRF price modifiers.
 * Increasing base prices by factor 65536 should be enough.
 * @see MAX_INFLATION
 */
static const int MIN_PRICE_MODIFIER = -8;
static const int MAX_PRICE_MODIFIER = 16;
static const int INVALID_PRICE_MODIFIER = MIN_PRICE_MODIFIER - 1;

/** Multiplier for how many regular track bits a tunnel/bridge counts. */
static const uint TUNNELBRIDGE_TRACKBIT_FACTOR = 4;
/** Multiplier for how many regular track bits a level crossing counts. */
static const uint LEVELCROSSING_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular track bits a road depot counts. */
static const uint ROAD_DEPOT_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular track bits a bay stop counts. */
static const uint ROAD_STOP_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular tiles a lock counts. */
static const uint LOCK_DEPOT_TILE_FACTOR = 2;

struct CargoPayment;
struct CargoPaymentIDTag : public PoolIDTraits<uint32_t, 0xFF000, 0xFFFFF> {};
using CargoPaymentID = PoolID<CargoPaymentIDTag>;

enum CargoPaymentAlgorithm : uint8_t {
	CPA_BEGIN = 0,       ///< Used for iterations and limit testing
	CPA_TRADITIONAL = 0, ///< Traditional algorithm
	CPA_MODERN,          ///< Modern algorithm
	CPA_END,             ///< Used for iterations and limit testing
};

enum CargoScalingMode : uint8_t {
	CSM_BEGIN = 0,         ///< Used for iterations and limit testing
	CSM_NORMAL = 0,        ///< Normal cargo scaling
	CSM_DAYLENGTH,         ///< Also scale by day length
	CSM_END,               ///< Used for iterations and limit testing
};

struct CargoScaler {
private:
	uint32_t scale16 = 1 << 16;

	inline uint ScaleWithBias(uint num, uint32_t bias)
	{
		return (uint)((((uint64_t)num * (uint64_t)this->scale16) + bias) >> 16);
	}

public:
	inline bool HasScaling() const
	{
		return this->scale16 != (1 << 16);
	}

	inline void SetScale(uint32_t scale16)
	{
		this->scale16 = scale16;
	}

	inline uint Scale(uint num)
	{
		if (num == 0) return 0;
		return std::max<uint>(1, this->ScaleWithBias(num, 0x8000));
	}

	uint ScaleAllowTrunc(uint num);
};

#endif /* ECONOMY_TYPE_H */
