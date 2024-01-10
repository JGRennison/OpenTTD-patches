/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file water_regions.cpp Handles dividing the water in the map into square regions to assist pathfinding. */

#include "stdafx.h"
#include "map_func.h"
#include "water_regions.h"
#include "map_func.h"
#include "tilearea_type.h"
#include "track_func.h"
#include "transport_type.h"
#include "landscape.h"
#include "tunnelbridge_map.h"
#include "follow_track.hpp"
#include "ship.h"

#include <algorithm>
#include <array>

using TWaterRegionTraversabilityBits = uint16_t;
constexpr TWaterRegionPatchLabel FIRST_REGION_LABEL = 1;
constexpr TWaterRegionPatchLabel INVALID_WATER_REGION_PATCH = 0;

static_assert(sizeof(TWaterRegionTraversabilityBits) * 8 == WATER_REGION_EDGE_LENGTH);

static inline TrackBits GetWaterTracks(TileIndex tile) { return TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_WATER, 0)); }
static inline bool IsAqueductTile(TileIndex tile) { return IsBridgeTile(tile) && GetTunnelBridgeTransportType(tile) == TRANSPORT_WATER; }

static inline uint32_t GetWaterRegionX(TileIndex tile) { return TileX(tile) / WATER_REGION_EDGE_LENGTH; }
static inline uint32_t GetWaterRegionY(TileIndex tile) { return TileY(tile) / WATER_REGION_EDGE_LENGTH; }

static inline uint32_t GetWaterRegionMapSizeX() { return MapSizeX() / WATER_REGION_EDGE_LENGTH; }
static inline uint32_t GetWaterRegionMapSizeY() { return MapSizeY() / WATER_REGION_EDGE_LENGTH; }

static inline uint32_t GetWaterRegionYShift() { return MapLogX() - WATER_REGION_EDGE_LENGTH_LOG; }

static inline TWaterRegionIndex GetWaterRegionIndex(uint32_t region_x, uint32_t region_y) { return (region_y << GetWaterRegionYShift()) + region_x; }
static inline TWaterRegionIndex GetWaterRegionIndex(TileIndex tile) { return GetWaterRegionIndex(GetWaterRegionX(tile), GetWaterRegionY(tile)); }

struct WaterRegionTileIterator {
	uint32_t x;
	uint32_t y;

	inline operator TileIndex () const
	{
		return TileXY(this->x, this->y);
	}

	inline TileIndex operator *() const
	{
		return TileXY(this->x, this->y);
	}

	WaterRegionTileIterator& operator ++()
	{
		this->x++;
		if ((this->x & WATER_REGION_EDGE_MASK) == 0)  {
			/* reached end of row */
			this->x -= WATER_REGION_EDGE_LENGTH;
			this->y++;
		}
		return *this;
	}
};

/**
 * Represents a square section of the map of a fixed size. Within this square individual unconnected patches of water are
 * identified using a Connected Component Labeling (CCL) algorithm. Note that all information stored in this class applies
 * only to tiles within the square section, there is no knowledge about the rest of the map. This makes it easy to invalidate
 * and update a water region if any changes are made to it, such as construction or terraforming.
 */
class WaterRegion
{
private:
	std::array<TWaterRegionTraversabilityBits, DIAGDIR_END> edge_traversability_bits{};
	const uint32_t tile_x;
	const uint32_t tile_y;
	bool has_cross_region_aqueducts = false;
	TWaterRegionPatchLabel number_of_patches = 0; // 0 = no water, 1 = one single patch of water, etc...
	std::array<TWaterRegionPatchLabel, WATER_REGION_NUMBER_OF_TILES> tile_patch_labels{};
	bool initialized = false;

	inline bool ContainsTile(TileIndex tile) const
	{
		const uint32_t x = TileX(tile);
		const uint32_t y = TileY(tile);
		return x >= this->tile_x && x < this->tile_x + WATER_REGION_EDGE_LENGTH
				&& y >= this->tile_y && y < this->tile_y + WATER_REGION_EDGE_LENGTH;
	}

	/**
	 * Returns the local index of the tile within the region. The N corner represents 0,
	 * the x direction is positive in the SW direction, and Y is positive in the SE direction.
	 * @param tile Tile within the water region.
	 * @returns The local index.
	 */
	inline int GetLocalIndex(TileIndex tile) const
	{
		assert(this->ContainsTile(tile));
		return (TileX(tile) - this->tile_x) + WATER_REGION_EDGE_LENGTH * (TileY(tile) - this->tile_y);
	}

public:
	WaterRegion(uint32_t region_x, uint32_t region_y)
		: tile_x(region_x * WATER_REGION_EDGE_LENGTH), tile_y(region_y * WATER_REGION_EDGE_LENGTH)
	{}

	WaterRegionTileIterator begin() const { return { this->tile_x, this->tile_y }; }
	WaterRegionTileIterator end() const { return { this->tile_x, this->tile_y + WATER_REGION_EDGE_LENGTH }; }

	bool IsInitialized() const { return this->initialized; }

	void Invalidate() { this->initialized = false; }

	/**
	 * Returns a set of bits indicating whether an edge tile on a particular side is traversable or not. These
	 * values can be used to determine whether a ship can enter/leave the region through a particular edge tile.
	 * @see GetLocalIndex() for a description of the coordinate system used.
	 * @param side Which side of the region we want to know the edge traversability of.
	 * @returns A value holding the edge traversability bits.
	 */
	TWaterRegionTraversabilityBits GetEdgeTraversabilityBits(DiagDirection side) const { return edge_traversability_bits[side]; }

	/**
	 * @returns The amount of individual water patches present within the water region. A value of
	 * 0 means there is no water present in the water region at all.
	 */
	int NumberOfPatches() const { return this->number_of_patches; }

	/**
	 * @returns Whether the water region contains aqueducts that cross the region boundaries.
	 */
	bool HasCrossRegionAqueducts() const { return this->has_cross_region_aqueducts; }

	/**
	 * Returns the patch label that was assigned to the tile.
	 * @param tile The tile of which we want to retrieve the label.
	 * @returns The label assigned to the tile.
	 */
	TWaterRegionPatchLabel GetLabel(TileIndex tile) const
	{
		assert(this->ContainsTile(tile));
		return this->tile_patch_labels[GetLocalIndex(tile)];
	}

	/**
	 * Performs the connected component labeling and other data gathering.
	 * @see WaterRegion
	 */
	void ForceUpdate()
	{
		this->has_cross_region_aqueducts = false;

		this->tile_patch_labels.fill(INVALID_WATER_REGION_PATCH);

		TWaterRegionPatchLabel current_label = 1;
		TWaterRegionPatchLabel highest_assigned_label = 0;

		/* Perform connected component labeling. This uses a flooding algorithm that expands until no
		 * additional tiles can be added. Only tiles inside the water region are considered. */
		for (const TileIndex start_tile : *this) {
			static std::vector<TileIndex> tiles_to_check;
			tiles_to_check.clear();
			tiles_to_check.push_back(start_tile);

			if (!this->has_cross_region_aqueducts && IsAqueductTile(start_tile)) {
				const TileIndex other_aqueduct_end = GetOtherBridgeEnd(start_tile);
				if (!this->ContainsTile(other_aqueduct_end)) {
					this->has_cross_region_aqueducts = true;
				}
			}

			bool increase_label = false;
			while (!tiles_to_check.empty()) {
				const TileIndex tile = tiles_to_check.back();
				tiles_to_check.pop_back();

				const TrackdirBits valid_dirs = TrackBitsToTrackdirBits(GetWaterTracks(tile));
				if (valid_dirs == TRACKDIR_BIT_NONE) continue;

				if (this->tile_patch_labels[GetLocalIndex(tile)] != INVALID_WATER_REGION_PATCH) continue;

				this->tile_patch_labels[GetLocalIndex(tile)] = current_label;
				highest_assigned_label = current_label;
				increase_label = true;

				for (const Trackdir dir : SetTrackdirBitIterator(valid_dirs)) {
					/* By using a TrackFollower we "play by the same rules" as the actual ship pathfinder */
					CFollowTrackWater ft;
					if (ft.Follow(tile, dir) && this->ContainsTile(ft.m_new_tile)) tiles_to_check.push_back(ft.m_new_tile);
				}
			}

			if (increase_label) current_label++;
		}

		this->number_of_patches = highest_assigned_label;
		this->initialized = true;

		/* Calculate the traversability (whether the tile can be entered / exited) for all edges. Note that
		 * we always follow the same X and Y scanning direction, this is important for comparisons later on! */
		this->edge_traversability_bits.fill(0);
		const uint32_t top_x = this->tile_x;
		const uint32_t top_y = this->tile_y;
		for (uint32_t i = 0; i < WATER_REGION_EDGE_LENGTH; ++i) {
			if (GetWaterTracks(TileXY(top_x + i, top_y)) & TRACK_BIT_3WAY_NW) SetBit(this->edge_traversability_bits[DIAGDIR_NW], i); // NW edge
			if (GetWaterTracks(TileXY(top_x + i, top_y + WATER_REGION_EDGE_LENGTH - 1)) & TRACK_BIT_3WAY_SE) SetBit(this->edge_traversability_bits[DIAGDIR_SE], i); // SE edge
			if (GetWaterTracks(TileXY(top_x, top_y + i)) & TRACK_BIT_3WAY_NE) SetBit(this->edge_traversability_bits[DIAGDIR_NE], i); // NE edge
			if (GetWaterTracks(TileXY(top_x + WATER_REGION_EDGE_LENGTH - 1, top_y + i)) & TRACK_BIT_3WAY_SW) SetBit(this->edge_traversability_bits[DIAGDIR_SW], i); // SW edge
		}
	}

	/**
	 * Updates the patch labels and other data, but only if the region is not yet initialized.
	 */
	inline void UpdateIfNotInitialized()
	{
		if (!this->initialized) ForceUpdate();
	}

	inline uint32_t CountPatchLabelOccurence(TWaterRegionPatchLabel label) const
	{
		return std::count(this->tile_patch_labels.begin(), this->tile_patch_labels.end(), label);
	}
};

std::vector<WaterRegion> _water_regions;

TileIndex GetTileIndexFromLocalCoordinate(uint32_t region_x, uint32_t region_y, uint32_t local_x, uint32_t local_y)
{
	assert(local_x < WATER_REGION_EDGE_LENGTH);
	assert(local_y < WATER_REGION_EDGE_LENGTH);
	return TileXY(WATER_REGION_EDGE_LENGTH * region_x + local_x, WATER_REGION_EDGE_LENGTH * region_y + local_y);
}

TileIndex GetEdgeTileCoordinate(uint32_t region_x, uint32_t region_y, DiagDirection side, uint32_t x_or_y)
{
	assert(x_or_y < WATER_REGION_EDGE_LENGTH);
	switch (side) {
		case DIAGDIR_NE: return GetTileIndexFromLocalCoordinate(region_x, region_y, 0, x_or_y);
		case DIAGDIR_SW: return GetTileIndexFromLocalCoordinate(region_x, region_y, WATER_REGION_EDGE_LENGTH - 1, x_or_y);
		case DIAGDIR_NW: return GetTileIndexFromLocalCoordinate(region_x, region_y, x_or_y, 0);
		case DIAGDIR_SE: return GetTileIndexFromLocalCoordinate(region_x, region_y, x_or_y, WATER_REGION_EDGE_LENGTH - 1);
		default: NOT_REACHED();
	}
}

WaterRegion &GetUpdatedWaterRegion(uint32_t region_x, uint32_t region_y)
{
	WaterRegion &result = _water_regions[GetWaterRegionIndex(region_x, region_y)];
	result.UpdateIfNotInitialized();
	return result;
}

WaterRegion &GetUpdatedWaterRegion(TileIndex tile)
{
	WaterRegion &result = _water_regions[GetWaterRegionIndex(tile)];
	result.UpdateIfNotInitialized();
	return result;
}

/**
 * Returns the index of the water region
 * @param water_region The Water region to return the index for
 */
TWaterRegionIndex GetWaterRegionIndex(const WaterRegionDesc &water_region)
{
	return GetWaterRegionIndex(water_region.x, water_region.y);
}

/**
 * Returns the center tile of a particular water region.
 * @param water_region The water region to find the center tile for.
 * @returns The center tile of the water region.
 */
TileIndex GetWaterRegionCenterTile(const WaterRegionDesc &water_region)
{
	return TileXY(water_region.x * WATER_REGION_EDGE_LENGTH + (WATER_REGION_EDGE_LENGTH / 2), water_region.y * WATER_REGION_EDGE_LENGTH + (WATER_REGION_EDGE_LENGTH / 2));
}

/**
 * Returns basic water region information for the provided tile.
 * @param tile The tile for which the information will be calculated.
 */
WaterRegionDesc GetWaterRegionInfo(TileIndex tile)
{
	return WaterRegionDesc{ GetWaterRegionX(tile), GetWaterRegionY(tile) };
}

/**
 * Returns basic water region patch information for the provided tile.
 * @param tile The tile for which the information will be calculated.
 */
WaterRegionPatchDesc GetWaterRegionPatchInfo(TileIndex tile)
{
	WaterRegion &region = GetUpdatedWaterRegion(tile);
	return WaterRegionPatchDesc{ GetWaterRegionX(tile), GetWaterRegionY(tile), region.GetLabel(tile)};
}

/**
 * Marks the water region that tile is part of as invalid.
 * @param tile Tile within the water region that we wish to invalidate.
 */
void InvalidateWaterRegion(TileIndex tile)
{
	const uint32_t index = GetWaterRegionIndex(tile);
	if (index > static_cast<uint32_t>(_water_regions.size())) return;
	_water_regions[index].Invalidate();
}

/**
 * Calls the provided callback function for all water region patches
 * accessible from one particular side of the starting patch.
 * @param water_region_patch Water patch within the water region to start searching from
 * @param side Side of the water region to look for neigboring patches of water
 * @param callback The function that will be called for each neighbor that is found
 */
static inline void VisitAdjacentWaterRegionPatchNeighbors(const WaterRegionPatchDesc &water_region_patch, DiagDirection side, TVisitWaterRegionPatchCallBack &func)
{
	const WaterRegion &current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

	const TileIndexDiffC offset = TileIndexDiffCByDiagDir(side);
	/* Unsigned underflow is allowed here, not UB */
	const uint32_t nx = water_region_patch.x + (uint32_t)offset.x;
	const uint32_t ny = water_region_patch.y + (uint32_t)offset.y;

	if (nx >= GetWaterRegionMapSizeX() || ny >= GetWaterRegionMapSizeY()) return;

	const WaterRegion &neighboring_region = GetUpdatedWaterRegion(nx, ny);
	const DiagDirection opposite_side = ReverseDiagDir(side);

	/* Indicates via which local x or y coordinates (depends on the "side" parameter) we can cross over into the adjacent region. */
	const TWaterRegionTraversabilityBits traversability_bits = current_region.GetEdgeTraversabilityBits(side)
		& neighboring_region.GetEdgeTraversabilityBits(opposite_side);
	if (traversability_bits == 0) return;

	if (current_region.NumberOfPatches() == 1 && neighboring_region.NumberOfPatches() == 1) {
		func(WaterRegionPatchDesc{ nx, ny, FIRST_REGION_LABEL }); // No further checks needed because we know there is just one patch for both adjacent regions
		return;
	}

	/* Multiple water patches can be reached from the current patch. Check each edge tile individually. */
	static std::vector<TWaterRegionPatchLabel> unique_labels; // static and vector-instead-of-map for performance reasons
	unique_labels.clear();
	for (uint32_t x_or_y = 0; x_or_y < WATER_REGION_EDGE_LENGTH; ++x_or_y) {
		if (!HasBit(traversability_bits, x_or_y)) continue;

		const TileIndex current_edge_tile = GetEdgeTileCoordinate(water_region_patch.x, water_region_patch.y, side, x_or_y);
		const TWaterRegionPatchLabel current_label = current_region.GetLabel(current_edge_tile);
		if (current_label != water_region_patch.label) continue;

		const TileIndex neighbor_edge_tile = GetEdgeTileCoordinate(nx, ny, opposite_side, x_or_y);
		const TWaterRegionPatchLabel neighbor_label = neighboring_region.GetLabel(neighbor_edge_tile);
		if (std::find(unique_labels.begin(), unique_labels.end(), neighbor_label) == unique_labels.end()) unique_labels.push_back(neighbor_label);
	}
	for (TWaterRegionPatchLabel unique_label : unique_labels) func(WaterRegionPatchDesc{ nx, ny, unique_label });
}

/**
 * Calls the provided callback function on all accessible water region patches in
 * each cardinal direction, plus any others that are reachable via aqueducts.
 * @param water_region_patch Water patch within the water region to start searching from
 * @param callback The function that will be called for each accessible water patch that is found
 */
void VisitWaterRegionPatchNeighbors(const WaterRegionPatchDesc &water_region_patch, TVisitWaterRegionPatchCallBack &callback)
{
	const WaterRegion &current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

	/* Visit adjacent water region patches in each cardinal direction */
	for (DiagDirection side = DIAGDIR_BEGIN; side < DIAGDIR_END; side++) VisitAdjacentWaterRegionPatchNeighbors(water_region_patch, side, callback);

	/* Visit neigboring water patches accessible via cross-region aqueducts */
	if (current_region.HasCrossRegionAqueducts()) {
		for (const TileIndex tile : current_region) {
			if (GetWaterRegionPatchInfo(tile) == water_region_patch && IsAqueductTile(tile)) {
				const TileIndex other_end_tile = GetOtherBridgeEnd(tile);
				if (GetWaterRegionIndex(tile) != GetWaterRegionIndex(other_end_tile)) callback(GetWaterRegionPatchInfo(other_end_tile));
			}
		}
	}
}

/**
 * Initializes all water regions. All water tiles will be scanned and interconnected water patches within regions will be identified.
 */
void InitializeWaterRegions()
{
	_water_regions.clear();
	_water_regions.reserve(static_cast<size_t>(GetWaterRegionMapSizeX()) * GetWaterRegionMapSizeY());

	for (uint32_t region_y = 0; region_y < GetWaterRegionMapSizeY(); region_y++) {
		for (uint32_t region_x = 0; region_x < GetWaterRegionMapSizeX(); region_x++) {
			_water_regions.emplace_back(region_x, region_y);
		}
	}
}
