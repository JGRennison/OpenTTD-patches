/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file waypoint_cmd.h Command definitions related to waypoints. */

#ifndef WAYPOINT_CMD_H
#define WAYPOINT_CMD_H

#include "command_type.h"
#include "station_type.h"

enum StationClassID : uint16_t;
enum RoadStopClassID : uint16_t;

DEF_CMD_TUPLE   (CMD_BUILD_RAIL_WAYPOINT,              CmdBuildRailWaypoint,                     {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<Axis, uint8_t, uint8_t, StationClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (CMD_REMOVE_FROM_RAIL_WAYPOINT,        CmdRemoveFromRailWaypoint,                {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, bool>)
DEF_CMD_TUPLE   (CMD_BUILD_ROAD_WAYPOINT,              CmdBuildRoadWaypoint,                     {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<Axis, uint8_t, uint8_t, RoadStopClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (CMD_REMOVE_FROM_ROAD_WAYPOINT,        CmdRemoveFromRoadWaypoint,                {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex>)
DEF_CMD_TUPLE   (CMD_BUILD_BUOY,                       CmdBuildBuoy,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<>)
DEF_CMD_TUPLE_NT(CMD_RENAME_WAYPOINT,                  CmdRenameWaypoint,                        {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, std::string>)
DEF_CMD_TUPLE_NT(CMD_SET_WAYPOINT_LABEL_HIDDEN,        CmdSetWaypointLabelHidden,                {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, bool>)
DEF_CMD_TUPLE_NT(CMD_EXCHANGE_WAYPOINT_NAMES,          CmdExchangeWaypointNames,                 {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, StationID>)

#endif /* WAYPOINT_CMD_H */
