/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file train_speed_adaptation.h Train speed adaptation data structures. */

#ifndef TRAIN_SPEED_ADAPTATION_H
#define TRAIN_SPEED_ADAPTATION_H

#include "date_type.h"
#include "date_func.h"
#include "track_type.h"
#include "tile_type.h"
#include "3rdparty/cpp-btree/btree_map.h"

struct SignalSpeedKey {
	TileIndex signal_tile;
	uint16_t signal_track;
	Trackdir last_passing_train_dir;

	bool operator==(const SignalSpeedKey& other) const
	{
		return signal_tile == other.signal_tile &&
			signal_track == other.signal_track &&
			last_passing_train_dir == other.last_passing_train_dir;
	}

	bool operator<(const SignalSpeedKey& other) const
	{
		return std::tie(this->signal_tile, this->signal_track, this->last_passing_train_dir) < std::tie(other.signal_tile, other.signal_track, other.last_passing_train_dir);
	}
};

struct SignalSpeedValue {
	uint16_t train_speed;
	StateTicks time_stamp;

	/** Checks if the timeout has passed */
	bool IsOutOfDate() const
	{
		return _state_ticks > this->time_stamp;
	}
};

extern btree::btree_map<SignalSpeedKey, SignalSpeedValue> _signal_speeds;

struct Train;
void SetSignalTrainAdaptationSpeed(const Train *v, TileIndex tile, uint16_t track);
void ApplySignalTrainAdaptationSpeed(Train *v, TileIndex tile, uint16_t track);
uint16_t GetLowestSpeedTrainAdaptationSpeedAtSignal(TileIndex tile, uint16_t track);

#endif /* TRAIN_SPEED_ADAPTATION_H */
