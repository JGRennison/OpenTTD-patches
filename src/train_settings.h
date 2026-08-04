/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_settings.h Train settings related functions. */

#ifndef TRAIN_SETTINGS_H
#define TRAIN_SETTINGS_H

#include "engine_type.h"
#include "settings_type.h"

inline int GetTrainRealisticBrakingTargetDecelerationLimit(VehicleAccelerationModel acceleration_type)
{
	return _settings_game.vehicle.train_acc_braking_percent * (120 + (static_cast<int>(acceleration_type) * 48)) / 100;
}

#endif /* TRAIN_SETTINGS_H */
