/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file test_map.cpp Map-related tests. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"

#include "../map_func.h"
#include "../road_map.h"
#include "../station_map.h"
#include "../bridge_map.h"
#include "../tunnel_map.h"


TEST_CASE("MayHaveRoad - Station")
{
	AllocateMap(64, 64);

	TileIndex t{0};
	auto test_station_type = [&](StationType st, bool has_road) {
		MakeStation(t, OWNER_NONE, StationID{0}, st, 0);
		bool ok = MayHaveRoad(t) == has_road;
		t++;
		return ok;
	};
	CHECK(test_station_type(StationType::Rail, false));
	CHECK(test_station_type(StationType::Airport, false));
	CHECK(test_station_type(StationType::Truck, true));
	CHECK(test_station_type(StationType::Bus, true));
	CHECK(test_station_type(StationType::Oilrig, false));
	CHECK(test_station_type(StationType::Dock, false));
	CHECK(test_station_type(StationType::Buoy, false));
	CHECK(test_station_type(StationType::RailWaypoint, false));
	CHECK(test_station_type(StationType::RoadWaypoint, true));

	DeallocateMap();
}

TEST_CASE("MayHaveRoad - Tunnel/bridge")
{
	AllocateMap(64, 64);

	ResetRoadTypes();

	MakeRoadTunnel(TileIndex{0}, OWNER_NONE, TunnelID{0}, DIAGDIR_NE, ROADTYPE_ROAD, INVALID_ROADTYPE);
	CHECK(MayHaveRoad(TileIndex{0}) == true);

	MakeRailTunnel(TileIndex{1}, OWNER_NONE, TunnelID{0}, DIAGDIR_NE, RailType{0});
	CHECK(MayHaveRoad(TileIndex{1}) == false);

	MakeRoadBridgeRamp(TileIndex{2}, OWNER_NONE, OWNER_NONE, OWNER_NONE, BridgeType{0}, DIAGDIR_NE, ROADTYPE_ROAD, INVALID_ROADTYPE);
	CHECK(MayHaveRoad(TileIndex{2}) == true);

	MakeRailBridgeRamp(TileIndex{3}, OWNER_NONE, BridgeType{0}, DIAGDIR_NE, RailType{0}, false);
	CHECK(MayHaveRoad(TileIndex{3}) == false);

	MakeAqueductBridgeRamp(TileIndex{4}, OWNER_NONE, DIAGDIR_NE);
	CHECK(MayHaveRoad(TileIndex{4}) == false);

	DeallocateMap();
}
