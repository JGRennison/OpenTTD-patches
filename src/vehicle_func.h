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

#define is_custom_sprite(x) (x >= 0xFD)
#define IS_CUSTOM_FIRSTHEAD_SPRITE(x) (x == 0xFD)
#define IS_CUSTOM_SECONDHEAD_SPRITE(x) (x == 0xFE)

static constexpr DateDelta VEHICLE_PROFIT_MIN_AGE = DAYS_IN_YEAR * 2; ///< Only vehicles older than this have a meaningful profit.
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

typedef Vehicle *VehicleFromPosProc(Vehicle *v, void *data);

void VehicleServiceInDepot(Vehicle *v);
uint CountVehiclesInChain(const Vehicle *v);

/**
 * Find a vehicle from a specific location. It will call \a proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of \a proc will be ignored.
 * @note Use this function when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The proc that determines whether a vehicle will be "found".
 */
inline void FindVehicleOnPos(TileIndex tile, VehicleType type, void *data, VehicleFromPosProc *proc)
{
	extern Vehicle *VehicleFromPos(TileIndex tile, VehicleType type, void *data, VehicleFromPosProc *proc, bool find_first);
	VehicleFromPos(tile, type, data, proc, false);
}

/**
 * Checks whether a vehicle is on a specific location. It will call \a proc for
 * vehicles until it returns non-nullptr.
 * @note Use #FindVehicleOnPos when you have the intention that all vehicles
 *       should be iterated over.
 * @param tile The location on the map
 * @param data Arbitrary data passed to \a proc.
 * @param proc The \a proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-nullptr.
 */
inline bool HasVehicleOnPos(TileIndex tile, VehicleType type, void *data, VehicleFromPosProc *proc)
{
	extern Vehicle *VehicleFromPos(TileIndex tile, VehicleType type, void *data, VehicleFromPosProc *proc, bool find_first);
	return VehicleFromPos(tile, type, data, proc, true) != nullptr;
}

/**
 * Find a vehicle from a specific location. It will call proc for ALL vehicles
 * on the tile and YOU must make SURE that the "best one" is stored in the
 * data value and is ALWAYS the same regardless of the order of the vehicles
 * where proc was called on!
 * When you fail to do this properly you create an almost untraceable DESYNC!
 * @note The return value of proc will be ignored.
 * @note Use this when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 */
inline void FindVehicleOnPosXY(int x, int y, VehicleType type, void *data, VehicleFromPosProc *proc)
{
	extern Vehicle *VehicleFromPosXY(int x, int y, VehicleType type, void *data, VehicleFromPosProc *proc, bool find_first);
	VehicleFromPosXY(x, y, type, data, proc, false);
}

/**
 * Checks whether a vehicle in on a specific location. It will call proc for
 * vehicles until it returns non-nullptr.
 * @note Use FindVehicleOnPosXY when you have the intention that all vehicles
 *       should be iterated over.
 * @param x    The X location on the map
 * @param y    The Y location on the map
 * @param data Arbitrary data passed to proc
 * @param proc The proc that determines whether a vehicle will be "found".
 * @return True if proc returned non-nullptr.
 */
inline bool HasVehicleOnPosXY(int x, int y, VehicleType type, void *data, VehicleFromPosProc *proc)
{
	extern Vehicle *VehicleFromPosXY(int x, int y, VehicleType type, void *data, VehicleFromPosProc *proc, bool find_first);
	return VehicleFromPosXY(x, y, type, data, proc, true) != nullptr;
}

void CallVehicleTicks();
uint8_t CalcPercentVehicleFilled(const Vehicle *v, StringID *colour);
uint8_t CalcPercentVehicleFilledOfCargo(const Vehicle *v, CargoID cargo);

void VehicleLengthChanged(const Vehicle *u);

void ResetVehicleHash();
void ResetVehicleColourMap();

byte GetBestFittingSubType(const Vehicle *v_from, Vehicle *v_for, CargoID dest_cargo_type);

void ViewportAddVehicles(DrawPixelInfo *dpi, bool update_vehicles);
void ViewportMapDrawVehicles(DrawPixelInfo *dpi, Viewport *vp);

void ShowNewGrfVehicleError(EngineID engine, StringID part1, StringID part2, GRFBugs bug_type, bool critical);

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
void AgeVehicle(Vehicle *v);
void VehicleEnteredDepotThisTick(Vehicle *v);

UnitID GetFreeUnitNumber(VehicleType type);

void VehicleEnterDepot(Vehicle *v);

bool CanBuildVehicleInfrastructure(VehicleType type, byte subtype = 0);

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
	switch (type) {
		case VEH_TRAIN:
		case VEH_ROAD:
		case VEH_SHIP:
		case VEH_AIRCRAFT:
			return true;

		default: return false;
	}
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
const struct Livery *GetEngineLivery(EngineID engine_type, CompanyID company, EngineID parent_engine_type, const Vehicle *v, byte livery_setting, bool ignore_group = false);

SpriteID GetEnginePalette(EngineID engine_type, CompanyID company);
SpriteID GetVehiclePalette(const Vehicle *v);
SpriteID GetUncachedTrainPaletteIgnoringGroup(const Train *v);

extern const uint32_t _veh_build_proc_table[];
extern const uint32_t _veh_sell_proc_table[];
extern const uint32_t _veh_refit_proc_table[];
extern const uint32_t _send_to_depot_proc_table[];

/* Functions to find the right command for certain vehicle type */
inline uint32_t GetCmdBuildVeh(VehicleType type)
{
	return _veh_build_proc_table[type];
}

inline uint32_t GetCmdBuildVeh(const BaseVehicle *v)
{
	return GetCmdBuildVeh(v->type);
}

inline uint32_t GetCmdSellVeh(VehicleType type)
{
	return _veh_sell_proc_table[type];
}

inline uint32_t GetCmdSellVeh(const BaseVehicle *v)
{
	return GetCmdSellVeh(v->type);
}

inline uint32_t GetCmdRefitVeh(VehicleType type)
{
	return _veh_refit_proc_table[type];
}

inline uint32_t GetCmdRefitVeh(const BaseVehicle *v)
{
	return GetCmdRefitVeh(v->type);
}

inline uint32_t GetCmdSendToDepot(VehicleType type)
{
	return _send_to_depot_proc_table[type];
}

inline uint32_t GetCmdSendToDepot(const BaseVehicle *v)
{
	return GetCmdSendToDepot(v->type);
}

CommandCost EnsureNoVehicleOnGround(TileIndex tile);
bool IsTrainCollidableRoadVehicleOnGround(TileIndex tile);
CommandCost EnsureNoTrainOnTrackBits(TileIndex tile, TrackBits track_bits);

extern VehicleID _new_vehicle_id;
extern uint _returned_refit_capacity;
extern uint16_t _returned_mail_refit_capacity;
extern CargoArray _returned_vehicle_capacities;

bool CanVehicleUseStation(EngineID engine_type, const struct Station *st);
bool CanVehicleUseStation(const Vehicle *v, const struct Station *st);
StringID GetVehicleCannotUseStationReason(const Vehicle *v, const Station *st);

void ReleaseDisastersTargetingVehicle(VehicleID vehicle);

typedef std::vector<VehicleID> VehicleSet;
void GetVehicleSet(VehicleSet &set, Vehicle *v, uint8_t num_vehicles);

void CheckCargoCapacity(Vehicle *v);

bool VehiclesHaveSameEngineList(const Vehicle *v1, const Vehicle *v2);
bool VehiclesHaveSameOrderList(const Vehicle *v1, const Vehicle *v2);

bool IsUniqueVehicleName(const char *name);

#endif /* VEHICLE_FUNC_H */
