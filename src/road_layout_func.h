/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file road_layout_func.h Functions related to road layout changes. */

#ifndef ROAD_LAYOUT_FUNC_H
#define ROAD_LAYOUT_FUNC_H

#include "road_type.h"
#include "settings_type.h"

inline bool RoadLayoutChangeNotificationEnabled(bool added)
{
	return _settings_game.pf.reroute_rv_on_layout_change >= (added ? 2 : 1);
}

inline void NotifyRoadLayoutChanged()
{
	_road_layout_change_counter++;
}

inline void NotifyRoadLayoutChanged(bool added)
{
	if (RoadLayoutChangeNotificationEnabled(added)) NotifyRoadLayoutChanged();
}

void NotifyRoadLayoutChangedIfTileNonLeaf(TileIndex tile, RoadTramType rtt, RoadBits present_bits);
void NotifyRoadLayoutChangedIfSimpleTunnelBridgeNonLeaf(TileIndex start, TileIndex end, DiagDirection start_dir, RoadTramType rtt);

#endif /* ROAD_LAYOUT_FUNC_H */
