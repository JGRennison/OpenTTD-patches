/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file articulated_vehicles.h Functions related to articulated vehicles. */

#ifndef ARTICULATED_VEHICLES_H
#define ARTICULATED_VEHICLES_H

#include "vehicle_type.h"
#include "engine_type.h"
#include <vector>

uint CountArticulatedParts(EngineID engine_type, bool purchase_window);
void GetArticulatedPartsEngineIDs(EngineID engine_type, bool purchase_window, std::vector<EngineID> &ids);
CargoArray GetCapacityOfArticulatedParts(EngineID engine, CargoType attempt_refit = INVALID_CARGO);
CargoTypes GetCargoTypesOfArticulatedParts(EngineID engine);
void AddArticulatedParts(Vehicle *first);
void GetArticulatedRefitMasks(EngineID engine, bool include_initial_cargo_type, CargoTypes *union_mask, CargoTypes *intersection_mask);
std::vector<CargoTypes> GetArticulatedRefitMaskVector(EngineID engine, bool include_initial_cargo_type);
CargoTypes GetUnionOfArticulatedRefitMasks(EngineID engine, bool include_initial_cargo_type);
CargoTypes GetCargoTypesOfArticulatedVehicle(const Vehicle *v, CargoType *cargo_type);
CargoType GetOverallCargoOfArticulatedVehicle(const Vehicle *v);
bool IsArticulatedVehicleRefittable(EngineID engine);
bool IsArticulatedEngine(EngineID engine_type);
void CheckConsistencyOfArticulatedVehicle(const Vehicle *v);


#endif /* ARTICULATED_VEHICLES_H */
