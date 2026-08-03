/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file tile_track_func.h Track related 'commands' that can be performed on all tiles. */

#ifndef TILE_TRACK_FUNC_H
#define TILE_TRACK_FUNC_H

#include "direction_type.h"
#include "track_type.h"
#include "transport_type.h"

struct Vehicle;

enum TileTrackStatusSubMode {
	TTSSM_ROAD_RTT_MASK       =    0xFF,
	TTSSM_ROAD_ROADTYPE_MASK  =  0xFF00,
	TTSSM_NO_RED_SIGNALS      = 0x10000,
};

enum class RoadTramType : uint8_t;

TrackStatus GetTileTrackStatus(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side = DiagDirection::Invalid);

inline TrackStatus GetTileTrackStatus(TileIndex tile, TransportType mode, RoadTramType sub_mode, DiagDirection side = DiagDirection::Invalid)
{
	return GetTileTrackStatus(tile, mode, to_underlying(sub_mode), side);
}

inline TrackdirBits GetTileTrackdirBits(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side = DiagDirection::Invalid)
{
	return GetTileTrackStatus(tile, mode, sub_mode | TTSSM_NO_RED_SIGNALS, side).trackdirs;
}

inline TrackdirBits GetTileTrackdirBits(TileIndex tile, TransportType mode, RoadTramType sub_mode, DiagDirection side = DiagDirection::Invalid)
{
	return GetTileTrackdirBits(tile, mode, to_underlying(sub_mode), side);
}

#endif /* TILE_TRACK_FUNC_H */
