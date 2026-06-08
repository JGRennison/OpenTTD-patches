/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file rail_settings.h Rail settings related functions. */

#ifndef RAIL_SETTINGS_H
#define RAIL_SETTINGS_H

#include "settings_type.h"

/**
 * Helper to determine whether the train signals are to be placed on the right side.
 * @return \c true iff signals are to be drawn on the right side.
 */
inline bool IsTrainSignalSideRight()
{
	switch (_settings_game.construction.train_signal_side) {
		case TrainSignalSide::Left:
			return false;
		case TrainSignalSide::Right:
			return true;
		case TrainSignalSide::RoadVehicleDrivingSide:
			return _settings_game.vehicle.road_side == RoadVehicleDrivingSide::Right;
		default:
			NOT_REACHED();
	}
}

#endif /* RAIL_SETTINGS_H */
