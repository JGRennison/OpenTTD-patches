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
#include "track_type.h"
#include "tile_type.h"

#include <unordered_map>

struct SignalSpeedKey
{
	TileIndex signal_tile;
	uint16 signal_track;
	Trackdir last_passing_train_dir;

	bool operator==(const SignalSpeedKey& other) const
	{
		return signal_tile == other.signal_tile &&
			signal_track == other.signal_track &&
			last_passing_train_dir == other.last_passing_train_dir;
	}
};

struct SignalSpeedValue
{
	uint16 train_speed;
	DateTicksScaled time_stamp;
};

struct SignalSpeedKeyHashFunc
{
	std::size_t operator() (const SignalSpeedKey &key) const
	{
		const std::size_t h1 = std::hash<TileIndex>()(key.signal_tile);
		const std::size_t h2 = std::hash<Trackdir>()(key.last_passing_train_dir);
		const std::size_t h3 = std::hash<uint16>()(key.signal_track);

		return (h1 ^ h2) ^ h3;
	}
};

extern std::unordered_map<SignalSpeedKey, SignalSpeedValue, SignalSpeedKeyHashFunc> _signal_speeds;

struct Train;
void SetSignalTrainAdaptationSpeed(const Train *v, TileIndex tile, uint16 track);
void ApplySignalTrainAdaptationSpeed(Train *v, TileIndex tile, uint16 track);

#endif /* TRAIN_SPEED_ADAPTATION_H */
