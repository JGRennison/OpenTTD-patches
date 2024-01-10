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

using TWaterRegionPatchLabelArray = std::array<TWaterRegionPatchLabel, WATER_REGION_NUMBER_OF_TILES>;

/**
 * Represents a square section of the map of a fixed size. Within this square individual unconnected patches of water are
 * identified using a Connected Component Labeling (CCL) algorithm. Note that all information stored in this class applies
 * only to tiles within the square section, there is no knowledge about the rest of the map. This makes it easy to invalidate
 * and update a water region if any changes are made to it, such as construction or terraforming.
 */
class WaterRegion
{
	friend class WaterRegionReference;

	std::array<TWaterRegionTraversabilityBits, DIAGDIR_END> edge_traversability_bits{};
	bool initialized = false;
	bool has_cross_region_aqueducts = false;
	TWaterRegionPatchLabel number_of_patches = 0; // 0 = no water, 1 = one single patch of water, etc...
	std::unique_ptr<TWaterRegionPatchLabelArray> tile_patch_labels;
};

static std::unique_ptr<TWaterRegionPatchLabelArray> _spare_labels;

class WaterRegionReference {
	const uint32_t tile_x;
	const uint32_t tile_y;
	WaterRegion &wr;

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

	inline bool HasNonMatchingPatchLabel(TWaterRegionPatchLabel expected_label) const
	{
		for (TWaterRegionPatchLabel label : *this->wr.tile_patch_labels) {
			if (label != expected_label) return true;
		}
		return false;
	}

public:
	WaterRegionReference(uint32_t region_x, uint32_t region_y, WaterRegion &wr)
		: tile_x(region_x * WATER_REGION_EDGE_LENGTH), tile_y(region_y * WATER_REGION_EDGE_LENGTH), wr(wr)
	{}

	WaterRegionTileIterator begin() const { return { this->tile_x, this->tile_y }; }
	WaterRegionTileIterator end() const { return { this->tile_x, this->tile_y + WATER_REGION_EDGE_LENGTH }; }

	bool IsInitialized() const { return this->wr.initialized; }

	void Invalidate() { this->wr.initialized = false; }

	/**
	 * Returns a set of bits indicating whether an edge tile on a particular side is traversable or not. These
	 * values can be used to determine whether a ship can enter/leave the region through a particular edge tile.
	 * @see GetLocalIndex() for a description of the coordinate system used.
	 * @param side Which side of the region we want to know the edge traversability of.
	 * @returns A value holding the edge traversability bits.
	 */
	TWaterRegionTraversabilityBits GetEdgeTraversabilityBits(DiagDirection side) const { return this->wr.edge_traversability_bits[side]; }

	/**
	 * @returns The amount of individual water patches present within the water region. A value of
	 * 0 means there is no water present in the water region at all.
	 */
	int NumberOfPatches() const { return this->wr.number_of_patches; }

	/**
	 * @returns Whether the water region contains aqueducts that cross the region boundaries.
	 */
	bool HasCrossRegionAqueducts() const { return this->wr.has_cross_region_aqueducts; }

	/**
	 * Returns the patch label that was assigned to the tile.
	 * @param tile The tile of which we want to retrieve the label.
	 * @returns The label assigned to the tile.
	 */
	TWaterRegionPatchLabel GetLabel(TileIndex tile) const
	{
		assert(this->ContainsTile(tile));
		if (this->wr.tile_patch_labels == nullptr) {
			return this->NumberOfPatches() == 0 ? INVALID_WATER_REGION_PATCH : 1;
		}
		return (*this->wr.tile_patch_labels)[this->GetLocalIndex(tile)];
	}

	/**
	 * Performs the connected component labeling and other data gathering.
	 * @see WaterRegion
	 */
	void ForceUpdate()
	{
		this->wr.has_cross_region_aqueducts = false;

		if (this->wr.tile_patch_labels == nullptr) {
			if (_spare_labels != nullptr) {
				this->wr.tile_patch_labels = std::move(_spare_labels);
			} else {
				this->wr.tile_patch_labels = std::make_unique<TWaterRegionPatchLabelArray>();
			}
		}

		this->wr.tile_patch_labels->fill(INVALID_WATER_REGION_PATCH);

		TWaterRegionPatchLabel current_label = 1;
		TWaterRegionPatchLabel highest_assigned_label = 0;

		/* Perform connected component labeling. This uses a flooding algorithm that expands until no
		 * additional tiles can be added. Only tiles inside the water region are considered. */
		for (const TileIndex start_tile : *this) {
			static std::vector<TileIndex> tiles_to_check;
			tiles_to_check.clear();
			tiles_to_check.push_back(start_tile);

			if (!this->wr.has_cross_region_aqueducts && IsAqueductTile(start_tile)) {
				const TileIndex other_aqueduct_end = GetOtherBridgeEnd(start_tile);
				if (!this->ContainsTile(other_aqueduct_end)) {
					this->wr.has_cross_region_aqueducts = true;
				}
			}

			bool increase_label = false;
			while (!tiles_to_check.empty()) {
				const TileIndex tile = tiles_to_check.back();
				tiles_to_check.pop_back();

				const TrackdirBits valid_dirs = TrackBitsToTrackdirBits(GetWaterTracks(tile));
				if (valid_dirs == TRACKDIR_BIT_NONE) continue;

				TWaterRegionPatchLabel &tile_patch = (*this->wr.tile_patch_labels)[GetLocalIndex(tile)];
				if (tile_patch != INVALID_WATER_REGION_PATCH) continue;

				tile_patch = current_label;
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

		this->wr.number_of_patches = highest_assigned_label;
		this->wr.initialized = true;

		/* Calculate the traversability (whether the tile can be entered / exited) for all edges. Note that
		 * we always follow the same X and Y scanning direction, this is important for comparisons later on! */
		this->wr.edge_traversability_bits.fill(0);
		const uint32_t top_x = this->tile_x;
		const uint32_t top_y = this->tile_y;
		for (uint32_t i = 0; i < WATER_REGION_EDGE_LENGTH; ++i) {
			if (GetWaterTracks(TileXY(top_x + i, top_y)) & TRACK_BIT_3WAY_NW) SetBit(this->wr.edge_traversability_bits[DIAGDIR_NW], i); // NW edge
			if (GetWaterTracks(TileXY(top_x + i, top_y + WATER_REGION_EDGE_LENGTH - 1)) & TRACK_BIT_3WAY_SE) SetBit(this->wr.edge_traversability_bits[DIAGDIR_SE], i); // SE edge
			if (GetWaterTracks(TileXY(top_x, top_y + i)) & TRACK_BIT_3WAY_NE) SetBit(this->wr.edge_traversability_bits[DIAGDIR_NE], i); // NE edge
			if (GetWaterTracks(TileXY(top_x + WATER_REGION_EDGE_LENGTH - 1, top_y + i)) & TRACK_BIT_3WAY_SW) SetBit(this->wr.edge_traversability_bits[DIAGDIR_SW], i); // SW edge
		}

		if (this->wr.number_of_patches == 0 || (this->wr.number_of_patches == 1 && !this->HasNonMatchingPatchLabel(1))) {
			/* No need for patch storage: trivial cases */
			_spare_labels = std::move(this->wr.tile_patch_labels);
		}
	}

	/**
	 * Updates the patch labels and other data, but only if the region is not yet initialized.
	 */
	inline void UpdateIfNotInitialized()
	{
		if (!this->wr.initialized) this->ForceUpdate();
	}

	inline bool HasPatchStorage() const
	{
		return this->wr.tile_patch_labels != nullptr;
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

inline WaterRegionReference GetWaterRegionRef(uint32_t region_x, uint32_t region_y)
{
	return WaterRegionReference(region_x, region_y, _water_regions[GetWaterRegionIndex(region_x, region_y)]);
}

inline WaterRegionReference GetWaterRegionRef(TileIndex tile)
{
	return GetWaterRegionRef(GetWaterRegionX(tile), GetWaterRegionY(tile));
}

WaterRegionReference GetUpdatedWaterRegion(uint32_t region_x, uint32_t region_y)
{
	WaterRegionReference ref(region_x, region_y, _water_regions[GetWaterRegionIndex(region_x, region_y)]);
	ref.UpdateIfNotInitialized();
	return ref;
}

WaterRegionReference GetUpdatedWaterRegion(TileIndex tile)
{
	return GetUpdatedWaterRegion(GetWaterRegionX(tile), GetWaterRegionY(tile));
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
	WaterRegionReference region = GetUpdatedWaterRegion(tile);
	return WaterRegionPatchDesc{ GetWaterRegionX(tile), GetWaterRegionY(tile), region.GetLabel(tile) };
}

/**
 * Marks the water region that tile is part of as invalid.
 * @param tile Tile within the water region that we wish to invalidate.
 */
void InvalidateWaterRegion(TileIndex tile)
{
	if (tile < MapSize()) {
		GetWaterRegionRef(tile).Invalidate();
	}
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
	const WaterRegionReference current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

	const TileIndexDiffC offset = TileIndexDiffCByDiagDir(side);
	/* Unsigned underflow is allowed here, not UB */
	const uint32_t nx = water_region_patch.x + (uint32_t)offset.x;
	const uint32_t ny = water_region_patch.y + (uint32_t)offset.y;

	if (nx >= GetWaterRegionMapSizeX() || ny >= GetWaterRegionMapSizeY()) return;

	const WaterRegionReference neighboring_region = GetUpdatedWaterRegion(nx, ny);
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
	const WaterRegionReference current_region = GetUpdatedWaterRegion(water_region_patch.x, water_region_patch.y);

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
	_water_regions.resize(static_cast<size_t>(GetWaterRegionMapSizeX()) * GetWaterRegionMapSizeY());
}

uint GetWaterRegionTileDebugColourIndex(TileIndex tile)
{
	const uint32_t sub_x = TileX(tile) & WATER_REGION_EDGE_MASK;
	const uint32_t sub_y = TileY(tile) & WATER_REGION_EDGE_MASK;

	auto get_edge_distance = [&](uint32_t sub) -> uint32_t {
		if (sub > WATER_REGION_EDGE_LENGTH / 2) sub = WATER_REGION_EDGE_MASK - sub;
		return sub;
	};
	uint32_t mode = std::min(get_edge_distance(sub_x), get_edge_distance(sub_y));

	switch (mode) {
		case 0: {
			const WaterRegionReference wr = GetWaterRegionRef(tile);
			if (!wr.IsInitialized()) return 1;

			return 2 + wr.NumberOfPatches();
		}

		case 1: {
			const WaterRegionReference wr = GetWaterRegionRef(tile);
			if (wr.HasPatchStorage()) return 2;

			return 0;
		}

		case 2: {
			const WaterRegionReference wr = GetWaterRegionRef(tile);
			if (wr.IsInitialized() && wr.HasCrossRegionAqueducts()) return 9;

			return 0;
		}

		default:
			return 0;
	}
}
