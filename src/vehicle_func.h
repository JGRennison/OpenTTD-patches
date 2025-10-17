/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_func.h Functions related to vehicles. */

#ifndef VEHICLE_FUNC_H
#define VEHICLE_FUNC_H

#include "gfx_type.h"
#include "direction_type.h"
#include "command_type.h"
#include "vehicle_type.h"
#include "engine_type.h"
#include "transport_type.h"
#include "newgrf_config.h"
#include "track_type.h"
#include "livery.h"
#include "cargo_type.h"
#include <vector>

/**
 * Special values for Vehicle::spritenum and (Aircraft|Rail|Road|Ship)VehicleInfo::image_index
 */
enum CustomVehicleSpriteNum {
	CUSTOM_VEHICLE_SPRITENUM = 0xFD, ///< Vehicle sprite from NewGRF
	CUSTOM_VEHICLE_SPRITENUM_REVERSED = 0xFE, ///< Vehicle sprite from NewGRF with reverse driving direction (from articulation callback)
};

static inline bool IsCustomVehicleSpriteNum(uint8_t spritenum)
{
	return spritenum >= CUSTOM_VEHICLE_SPRITENUM;
}

static constexpr EconTime::DateDelta VEHICLE_PROFIT_MIN_AGE{DAYS_IN_YEAR * 2}; ///< Only vehicles older than this have a meaningful profit.
static const Money VEHICLE_PROFIT_THRESHOLD = 10000;        ///< Threshold for a vehicle to be considered making good profit.

struct Viewport;

/**
 * Helper to check whether an image index is valid for a particular vehicle.
 * @tparam T The type of vehicle.
 * @param image_index The image index to check.
 * @return True iff the image index is valid.
 */
template <VehicleType T>
bool IsValidImageIndex(uint8_t image_index);

void VehicleServiceInDepot(Vehicle *v);
uint CountVehiclesInChain(const Vehicle *v);
void CallVehicleTicks();
uint8_t CalcPercentVehicleFilled(const Vehicle *v, StringID *colour);
uint8_t CalcPercentVehicleFilledOfCargo(const Vehicle *v, CargoType cargo);

void VehicleLengthChanged(const Vehicle *u);

void ResetVehicleHash();
void ResetVehicleColourMap();

uint8_t GetBestFittingSubType(const Vehicle *v_from, Vehicle *v_for, CargoType dest_cargo_type);

void ViewportAddVehicles(DrawPixelInfo *dpi, bool update_vehicles);
void ViewportMapDrawVehicles(DrawPixelInfo *dpi, Viewport *vp);

void ShowNewGrfVehicleError(EngineID engine, StringID part1, StringID part2, GRFBug bug_type, bool critical);

enum TunnelBridgeIsFreeMode {
	TBIFM_ALL,
	TBIFM_ACROSS_ONLY,
	TBIFM_PRIMARY_ONLY,
};
CommandCost TunnelBridgeIsFree(TileIndex tile, TileIndex endtile, const Vehicle *ignore = nullptr, TunnelBridgeIsFreeMode mode = TBIFM_ALL);
Train *GetTrainClosestToTunnelBridgeEnd(TileIndex tile, TileIndex other_tile);
int GetAvailableFreeTilesInSignalledTunnelBridge(TileIndex entrance, TileIndex exit, TileIndex tile);
int GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(TileIndex entrance, TileIndex exit, int offset);

void DecreaseVehicleValue(Vehicle *v);
void CheckVehicleBreakdown(Vehicle *v);
void EconomyAgeVehicle(Vehicle *v);
void AgeVehicle(Vehicle *v);

UnitID GetFreeUnitNumber(VehicleType type);

void VehicleEnterDepot(Vehicle *v);

bool CanBuildVehicleInfrastructure(VehicleType type, uint8_t subtype = 0);

/** Position information of a vehicle after it moved */
struct GetNewVehiclePosResult {
	int x, y;  ///< x and y position of the vehicle after moving
	TileIndex old_tile; ///< Current tile of the vehicle
	TileIndex new_tile; ///< Tile of the vehicle after moving
};

GetNewVehiclePosResult GetNewVehiclePos(const Vehicle *v);
Direction GetDirectionTowards(const Vehicle *v, int x, int y);

/**
 * Is the given vehicle type buildable by a company?
 * @param type Vehicle type being queried.
 * @return Vehicle type is buildable by a company.
 */
inline bool IsCompanyBuildableVehicleType(VehicleType type)
{
	return type < VEH_COMPANY_END;
}

/**
 * Is the given vehicle buildable by a company?
 * @param v Vehicle being queried.
 * @return Vehicle is buildable by a company.
 */
inline bool IsCompanyBuildableVehicleType(const BaseVehicle *v)
{
	return IsCompanyBuildableVehicleType(v->type);
}

LiveryScheme GetEngineLiveryScheme(EngineID engine_type, EngineID parent_engine_type, const Vehicle *v);
const struct Livery *GetEngineLivery(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v, uint8_t livery_setting, bool ignore_group = false);

SpriteID GetEnginePalette(EngineID engine_type, CompanyID company);
SpriteID GetVehiclePalette(const Vehicle *v);
SpriteID GetUncachedTrainPaletteIgnoringGroup(const Train *v);

extern const StringID _veh_build_msg_table[];
extern const StringID _veh_sell_msg_table[];
extern const StringID _veh_sell_all_msg_table[];
extern const StringID _veh_autoreplace_msg_table[];
extern const StringID _veh_refit_msg_table[];
extern const StringID _send_to_depot_msg_table[];

/* Functions to find the right command for certain vehicle type */
inline StringID GetCmdBuildVehMsg(VehicleType type)
{
	return _veh_build_msg_table[type];
}

inline StringID GetCmdBuildVehMsg(const BaseVehicle *v)
{
	return GetCmdBuildVehMsg(v->type);
}

inline StringID GetCmdSellVehMsg(VehicleType type)
{
	return _veh_sell_msg_table[type];
}

inline StringID GetCmdSellVehMsg(const BaseVehicle *v)
{
	return GetCmdSellVehMsg(v->type);
}

inline StringID GetCmdSellAllVehMsg(VehicleType type)
{
	return _veh_sell_all_msg_table[type];
}

inline StringID GetCmdAutoreplaceVehMsg(VehicleType type)
{
	return _veh_autoreplace_msg_table[type];
}

inline StringID GetCmdRefitVehMsg(VehicleType type)
{
	return _veh_refit_msg_table[type];
}

inline StringID GetCmdRefitVehMsg(const BaseVehicle *v)
{
	return GetCmdRefitVehMsg(v->type);
}

inline StringID GetCmdSendToDepotMsg(VehicleType type)
{
	return _send_to_depot_msg_table[type];
}

inline StringID GetCmdSendToDepotMsg(const BaseVehicle *v)
{
	return GetCmdSendToDepotMsg(v->type);
}

CommandCost EnsureNoVehicleOnGround(TileIndex tile);
bool IsTrainCollidableRoadVehicleOnGround(TileIndex tile);
CommandCost EnsureNoTrainOnTrackBits(TileIndex tile, TrackBits track_bits);

extern uint _returned_refit_capacity;
extern uint16_t _returned_mail_refit_capacity;
extern CargoArray _returned_vehicle_capacities;

bool CanVehicleUseStation(EngineID engine_type, const struct Station *st);
bool CanVehicleUseStation(const Vehicle *v, const struct Station *st);
StringID GetVehicleCannotUseStationReason(const Vehicle *v, const Station *st);

void ResetDisasterVehicleTargeting();
void ReleaseDisasterVehicleTargetingVehicle(VehicleID vehicle);
bool SetDisasterVehicleTargetingVehicle(VehicleID vehicle, VehicleID disaster_vehicle);

typedef std::vector<VehicleID> VehicleSet;
void GetVehicleSet(VehicleSet &set, Vehicle *v, uint8_t num_vehicles);

void CheckCargoCapacity(Vehicle *v);

bool VehiclesHaveSameEngineList(const Vehicle *v1, const Vehicle *v2);
bool VehiclesHaveSameOrderList(const Vehicle *v1, const Vehicle *v2);

bool IsUniqueVehicleName(std::string_view name);

void ShowTrainTooHeavyAdviceMessage(const Vehicle *v);

#endif /* VEHICLE_FUNC_H */
