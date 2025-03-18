/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_cmd.h Command definitions related to stations. */

#ifndef STATION_CMD_H
#define STATION_CMD_H

#include "command_type.h"
#include "cargo_type.h"
#include "station_type.h"

enum StationClassID : uint16_t;
enum RoadStopClassID : uint16_t;
enum RailType : uint8_t;
enum RoadType : uint8_t;

DEF_CMD_TUPLE   (CMD_BUILD_AIRPORT,                    CmdBuildAirport,     CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<uint8_t, uint8_t, StationID, bool>)
DEF_CMD_TUPLE   (CMD_BUILD_DOCK,                       CmdBuildDock,                       CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<StationID, bool>)
DEF_CMD_TUPLE   (CMD_BUILD_RAIL_STATION,               CmdBuildRailStation, CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<RailType, Axis, uint8_t, uint8_t, StationClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (CMD_REMOVE_FROM_RAIL_STATION,         CmdRemoveFromRailStation,                 {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<TileIndex, bool>)
DEF_CMD_TUPLE   (CMD_BUILD_ROAD_STOP,                  CmdBuildRoadStop,    CMD_NO_WATER | CMD_AUTO, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<uint8_t, uint8_t, RoadStopType, bool, DiagDirection, RoadType, RoadStopClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (CMD_REMOVE_ROAD_STOP,                 CmdRemoveRoadStop,                        {}, CMDT_LANDSCAPE_CONSTRUCTION, CmdDataT<uint8_t, uint8_t, RoadStopType, bool>)
DEF_CMD_TUPLE_NT(CMD_RENAME_STATION,                   CmdRenameStation,                         {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, bool, std::string>)
DEF_CMD_TUPLE_NT(CMD_OPEN_CLOSE_AIRPORT,               CmdOpenCloseAirport,                      {}, CMDT_ROUTE_MANAGEMENT,       CmdDataT<StationID>)
DEF_CMD_TUPLE_NT(CMD_EXCHANGE_STATION_NAMES,           CmdExchangeStationNames,                  {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, StationID>)
DEF_CMD_TUPLE_NT(CMD_SET_STATION_CARGO_ALLOWED_SUPPLY, CmdSetStationCargoAllowedSupply,          {}, CMDT_OTHER_MANAGEMENT,       CmdDataT<StationID, CargoType, bool>)

#endif /* STATION_CMD_H */
