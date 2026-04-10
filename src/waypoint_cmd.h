/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file waypoint_cmd.h Command definitions related to waypoints. */

#ifndef WAYPOINT_CMD_H
#define WAYPOINT_CMD_H

#include "command_type.h"
#include "station_type.h"
#include "newgrf_station_id.h"

DEF_CMD_TUPLE   (Commands::BuildRailWaypoint,           CmdBuildRailWaypoint,                     {}, CommandType::LandscapeConstruction, CmdDataT<Axis, uint8_t, uint8_t, StationClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (Commands::RemoveFromRailWaypoint,      CmdRemoveFromRailWaypoint,                {}, CommandType::LandscapeConstruction, CmdDataT<TileIndex, bool>)
DEF_CMD_TUPLE   (Commands::BuildRoadWaypoint,           CmdBuildRoadWaypoint,                     {}, CommandType::LandscapeConstruction, CmdDataT<Axis, uint8_t, uint8_t, RoadStopClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (Commands::RemoveFromRoadWaypoint,      CmdRemoveFromRoadWaypoint,                {}, CommandType::LandscapeConstruction, CmdDataT<TileIndex>)
DEF_CMD_TUPLE   (Commands::BuildBuoy,                   CmdBuildBuoy,                       CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<>)
DEF_CMD_TUPLE_NT(Commands::RenameWaypoint,              CmdRenameWaypoint,                        {}, CommandType::OtherManagement,       CmdDataT<StationID, std::string>)
DEF_CMD_TUPLE_NT(Commands::MoveWaypointName,            CmdMoveWaypointName,                      {}, CommandType::OtherManagement,       CmdDataT<StationID, TileIndex>)
DEF_CMD_TUPLE_NT(Commands::SetWaypointLabelHidden,      CmdSetWaypointLabelHidden,                {}, CommandType::OtherManagement,       CmdDataT<StationID, bool>)
DEF_CMD_TUPLE_NT(Commands::ExchangeWaypointNames,       CmdExchangeWaypointNames,                 {}, CommandType::OtherManagement,       CmdDataT<StationID, StationID>)

#endif /* WAYPOINT_CMD_H */
