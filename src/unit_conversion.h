/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file unit_conversion.h Functions related to unit conversion. */

#ifndef UNIT_CONVERSION_H
#define UNIT_CONVERSION_H

uint ConvertSpeedToDisplaySpeed(uint speed, VehicleType type);
uint ConvertSpeedToUnitDisplaySpeed(uint speed, VehicleType type);
uint ConvertDisplaySpeedToSpeed(uint speed, VehicleType type);
uint ConvertWeightToDisplayWeight(uint weight);
uint ConvertDisplayWeightToWeight(uint weight);
uint ConvertPowerToDisplayPower(uint power);
uint ConvertDisplayPowerToPower(uint power);
int64_t ConvertForceToDisplayForce(int64_t force);
int64_t ConvertDisplayForceToForce(int64_t force);
void ConvertPowerWeightRatioToDisplay(int64_t ratio, int64_t &value, int64_t &decimals);
void ConvertForceWeightRatioToDisplay(int64_t ratio, int64_t &value, int64_t &decimals);
uint ConvertDisplayToPowerWeightRatio(double in);
uint ConvertDisplayToForceWeightRatio(double in);

#endif /* UNIT_CONVERSION_H */
