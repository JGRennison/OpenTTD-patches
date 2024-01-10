/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file water_regions.h Handles dividing the water in the map into regions to assist pathfinding. */

#ifndef WATER_REGIONS_H
#define WATER_REGIONS_H

#include "tile_type.h"
#include "map_func.h"

#include <functional>

using TWaterRegionPatchLabel = uint8_t;
using TWaterRegionIndex = uint32_t;

constexpr uint32_t WATER_REGION_EDGE_LENGTH = 16;
constexpr uint32_t WATER_REGION_EDGE_LENGTH_LOG = 4;
static_assert(1 << WATER_REGION_EDGE_LENGTH_LOG == WATER_REGION_EDGE_LENGTH);

constexpr uint32_t WATER_REGION_EDGE_MASK = WATER_REGION_EDGE_LENGTH - 1;
static_assert((WATER_REGION_EDGE_LENGTH & WATER_REGION_EDGE_MASK) == 0);

constexpr uint32_t WATER_REGION_NUMBER_OF_TILES = WATER_REGION_EDGE_LENGTH * WATER_REGION_EDGE_LENGTH;

/**
 * Describes a single interconnected patch of water within a particular water region.
 */
struct WaterRegionPatchDesc
{
	uint32_t x; ///< The X coordinate of the water region, i.e. X=2 is the 3rd water region along the X-axis
	uint32_t y; ///< The Y coordinate of the water region, i.e. Y=2 is the 3rd water region along the Y-axis
	TWaterRegionPatchLabel label; ///< Unique label identifying the patch within the region

	bool operator==(const WaterRegionPatchDesc &other) const { return x == other.x && y == other.y && label == other.label; }
	bool operator!=(const WaterRegionPatchDesc &other) const { return !(*this == other); }
};


/**
 * Describes a single square water region.
 */
struct WaterRegionDesc
{
	uint32_t x; ///< The X coordinate of the water region, i.e. X=2 is the 3rd water region along the X-axis
	uint32_t y; ///< The Y coordinate of the water region, i.e. Y=2 is the 3rd water region along the Y-axis

	WaterRegionDesc(const uint32_t x, const uint32_t y) : x(x), y(y) {}
	WaterRegionDesc(const WaterRegionPatchDesc &water_region_patch) : x(water_region_patch.x), y(water_region_patch.y) {}

	bool operator==(const WaterRegionDesc &other) const { return x == other.x && y == other.y; }
	bool operator!=(const WaterRegionDesc &other) const { return !(*this == other); }
};

TWaterRegionIndex GetWaterRegionIndex(const WaterRegionDesc &water_region);

TileIndex GetWaterRegionCenterTile(const WaterRegionDesc &water_region);

WaterRegionDesc GetWaterRegionInfo(TileIndex tile);
WaterRegionPatchDesc GetWaterRegionPatchInfo(TileIndex tile);

void InvalidateWaterRegion(TileIndex tile);
void DebugInvalidateAllWaterRegions();
void DebugInitAllWaterRegions();

using TVisitWaterRegionPatchCallBack = std::function<void(const WaterRegionPatchDesc &)>;
void VisitWaterRegionPatchNeighbors(const WaterRegionPatchDesc &water_region_patch, TVisitWaterRegionPatchCallBack &callback);

void InitializeWaterRegions();

struct WaterRegionSaveLoadInfo
{
	bool initialized;
};

#endif /* WATER_REGIONS_H */
