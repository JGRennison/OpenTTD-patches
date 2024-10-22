/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file water_regions.cpp Handles dividing the water in the map into square regions to assist pathfinding. */

#include "stdafx.h"
#include "debug.h"
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
#include <limits>
#include <vector>

using RegionValidBlockT = size_t;
static constexpr uint REGION_VALID_BLOCK_BITS = std::numeric_limits<RegionValidBlockT>::digits;

using TWaterRegionTraversabilityBits = uint16_t;
constexpr TWaterRegionPatchLabel FIRST_REGION_LABEL = 1;

static_assert(sizeof(TWaterRegionTraversabilityBits) * 8 == WATER_REGION_EDGE_LENGTH);
static_assert(sizeof(TWaterRegionPatchLabel) == sizeof(uint8_t)); // Important for the hash calculation.

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
	bool has_cross_region_aqueducts = false;
	TWaterRegionPatchLabel number_of_patches = 0; // 0 = no water, 1 = one single patch of water, etc...
	std::unique_ptr<TWaterRegionPatchLabelArray> tile_patch_labels;

public:
	static bool IsInitialized(uint region_id);
	static void Invalidate(uint region_id);
	static bool MarkedValid(uint region_id);
};

static std::unique_ptr<TWaterRegionPatchLabelArray> _spare_labels;

class WaterRegionReference {
	const uint32_t tile_x;
	const uint32_t tile_y;
	const TWaterRegionIndex region_id;
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
	WaterRegionReference(uint32_t region_x, uint32_t region_y, TWaterRegionIndex region_id, WaterRegion &wr)
		: tile_x(region_x * WATER_REGION_EDGE_LENGTH), tile_y(region_y * WATER_REGION_EDGE_LENGTH), region_id(region_id), wr(wr)
	{}

	WaterRegionTileIterator begin() const { return { this->tile_x, this->tile_y }; }
	WaterRegionTileIterator end() const { return { this->tile_x, this->tile_y + WATER_REGION_EDGE_LENGTH }; }

	bool IsInitialized() const { return WaterRegion::IsInitialized(this->region_id); }
	void Invalidate() { WaterRegion::Invalidate(this->region_id); }
	bool MarkedValid() { return WaterRegion::MarkedValid(this->region_id); }

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
		this->wr.edge_traversability_bits.fill(0);

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
					if (ft.Follow(tile, dir)) {
						if (this->ContainsTile(ft.m_new_tile)) {
							tiles_to_check.push_back(ft.m_new_tile);
						} else if (!ft.m_is_bridge) {
							assert(DistanceManhattan(ft.m_new_tile, tile) == 1);
							const auto side = DiagdirBetweenTiles(tile, ft.m_new_tile);
							const int local_x_or_y = DiagDirToAxis(side) == AXIS_X ? TileY(tile) - this->tile_y : TileX(tile) - this->tile_x;
							SetBit(this->wr.edge_traversability_bits[side], local_x_or_y);
						} else {
							this->wr.has_cross_region_aqueducts = true;
						}
					}
				}
			}

			if (increase_label) current_label++;
		}

		this->wr.number_of_patches = highest_assigned_label;

		if (this->wr.number_of_patches == 0 || (this->wr.number_of_patches == 1 && !this->HasNonMatchingPatchLabel(1))) {
			/* No need for patch storage: trivial cases */
			_spare_labels = std::move(this->wr.tile_patch_labels);
		}
	}

	inline bool HasPatchStorage() const
	{
		return this->wr.tile_patch_labels != nullptr;
	}

	TWaterRegionPatchLabelArray CopyPatchLabelArray() const
	{
		TWaterRegionPatchLabelArray out;
		if (this->HasPatchStorage()) {
			out = *this->wr.tile_patch_labels;
		} else {
			out.fill(this->NumberOfPatches() == 0 ? INVALID_WATER_REGION_PATCH : 1);
		}
		return out;
	}

	void PrintDebugInfo()
	{
		Debug(map, 9, "Water region {},{} labels and edge traversability = ...", this->tile_x / WATER_REGION_EDGE_LENGTH, this->tile_y / WATER_REGION_EDGE_LENGTH);

		const size_t max_element_width = std::to_string(this->wr.number_of_patches).size();

		std::string traversability = fmt::format("{:0{}b}", this->GetEdgeTraversabilityBits(DIAGDIR_NW), WATER_REGION_EDGE_LENGTH);
		Debug(map, 9, "    {:{}}", fmt::join(traversability, " "), max_element_width);
		Debug(map, 9, "  +{:->{}}+", "", WATER_REGION_EDGE_LENGTH * (max_element_width + 1) + 1);

		for (uint y = 0; y < WATER_REGION_EDGE_LENGTH; ++y) {
			std::string line{};
			for (uint x = 0; x < WATER_REGION_EDGE_LENGTH; ++x) {
				const auto label = this->GetLabel(TileXY(this->tile_x + x, this->tile_y + y));
				const std::string label_str = label == INVALID_WATER_REGION_PATCH ? "." : std::to_string(label);
				line = fmt::format("{:{}}", label_str, max_element_width) + " " + line;
			}
			Debug(map, 9, "{} | {}| {}", GB(this->GetEdgeTraversabilityBits(DIAGDIR_SW), y, 1), line, GB(this->GetEdgeTraversabilityBits(DIAGDIR_NE), y, 1));
		}

		Debug(map, 9, "  +{:->{}}+", "", WATER_REGION_EDGE_LENGTH * (max_element_width + 1) + 1);
		traversability = fmt::format("{:0{}b}", this->GetEdgeTraversabilityBits(DIAGDIR_SE), WATER_REGION_EDGE_LENGTH);
		Debug(map, 9, "    {:{}}", fmt::join(traversability, " "), max_element_width);
	}
};

std::unique_ptr<WaterRegion[]> _water_regions;
std::unique_ptr<RegionValidBlockT[]> _is_water_region_valid;

bool WaterRegion::IsInitialized(uint region_id)
{
	return HasBit(_is_water_region_valid[region_id / REGION_VALID_BLOCK_BITS], region_id % REGION_VALID_BLOCK_BITS);
}

void WaterRegion::Invalidate(uint region_id)
{
	ClrBit(_is_water_region_valid[region_id / REGION_VALID_BLOCK_BITS], region_id % REGION_VALID_BLOCK_BITS);
}

bool WaterRegion::MarkedValid(uint region_id)
{
	RegionValidBlockT &block = _is_water_region_valid[region_id / REGION_VALID_BLOCK_BITS];
	if (HasBit(block, region_id % REGION_VALID_BLOCK_BITS)) return false;

	SetBit(block, region_id % REGION_VALID_BLOCK_BITS);
	return true;
}

static TileIndex GetTileIndexFromLocalCoordinate(uint32_t region_x, uint32_t region_y, uint32_t local_x, uint32_t local_y)
{
	assert(local_x < WATER_REGION_EDGE_LENGTH);
	assert(local_y < WATER_REGION_EDGE_LENGTH);
	return TileXY(WATER_REGION_EDGE_LENGTH * region_x + local_x, WATER_REGION_EDGE_LENGTH * region_y + local_y);
}

static TileIndex GetEdgeTileCoordinate(uint32_t region_x, uint32_t region_y, DiagDirection side, uint32_t x_or_y)
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
	TWaterRegionIndex region_id = GetWaterRegionIndex(region_x, region_y);
	return WaterRegionReference(region_x, region_y, region_id, _water_regions[region_id]);
}

inline WaterRegionReference GetWaterRegionRef(TileIndex tile)
{
	return GetWaterRegionRef(GetWaterRegionX(tile), GetWaterRegionY(tile));
}

static WaterRegionReference GetUpdatedWaterRegion(uint32_t region_x, uint32_t region_y)
{
	TWaterRegionIndex region_id = GetWaterRegionIndex(region_x, region_y);
	WaterRegionReference ref(region_x, region_y, region_id, _water_regions[region_id]);
	if (WaterRegion::MarkedValid(region_id)) ref.ForceUpdate();
	return ref;
}

static WaterRegionReference GetUpdatedWaterRegion(TileIndex tile)
{
	return GetUpdatedWaterRegion(GetWaterRegionX(tile), GetWaterRegionY(tile));
}

/**
 * Returns the index of the water region.
 * @param water_region The water region to return the index for.
 */
static TWaterRegionIndex GetWaterRegionIndex(const WaterRegionDesc &water_region)
{
	return GetWaterRegionIndex(water_region.x, water_region.y);
}

/**
 * Calculates a number that uniquely identifies the provided water region patch.
 * @param water_region_patch The Water region to calculate the hash for.
 */
uint32_t CalculateWaterRegionPatchHash(const WaterRegionPatchDesc &water_region_patch)
{
	return water_region_patch.label | GetWaterRegionIndex(water_region_patch) << 8;
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
	if (tile >= MapSize()) return;

	const TWaterRegionIndex region = GetWaterRegionIndex(tile);
	WaterRegion::Invalidate(region);

	/* When updating the water region we look into the first tile of adjacent water regions to determine edge
	 * traversability. This means that if we invalidate any region edge tiles we might also change the traversability
	 * of the adjacent region. This code ensures the adjacent regions also get invalidated in such a case. */
	const uint x = TileX(tile);
	const uint y = TileY(tile);
	if ((x & WATER_REGION_EDGE_MASK) ==                      0 && x >         0) WaterRegion::Invalidate(region - 1);
	if ((x & WATER_REGION_EDGE_MASK) == WATER_REGION_EDGE_MASK && x < MapMaxX()) WaterRegion::Invalidate(region + 1);
	if ((y & WATER_REGION_EDGE_MASK) ==                      0 && y >         0) WaterRegion::Invalidate(region - GetWaterRegionMapSizeX());
	if ((y & WATER_REGION_EDGE_MASK) == WATER_REGION_EDGE_MASK && y < MapMaxY()) WaterRegion::Invalidate(region + GetWaterRegionMapSizeX());
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
	if (water_region_patch.label == INVALID_WATER_REGION_PATCH) return;

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
		assert(neighbor_label != INVALID_WATER_REGION_PATCH);
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
	if (water_region_patch.label == INVALID_WATER_REGION_PATCH) return;

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

static size_t GetWaterRegionValidSize()
{
	return CeilDivT<size_t>(GetWaterRegionMapSizeX() * GetWaterRegionMapSizeY(), REGION_VALID_BLOCK_BITS);
}

/**
 * Initializes all water regions. All water tiles will be scanned and interconnected water patches within regions will be identified.
 */
void InitializeWaterRegions()
{
	_water_regions.reset(new WaterRegion[GetWaterRegionMapSizeX() * GetWaterRegionMapSizeY()]);
	_is_water_region_valid.reset(new RegionValidBlockT[GetWaterRegionValidSize()]{});
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

void DebugInvalidateAllWaterRegions()
{
	std::fill(_is_water_region_valid.get(), _is_water_region_valid.get() + GetWaterRegionValidSize(), 0);
}

void DebugInitAllWaterRegions()
{
	const uint32_t size_x = GetWaterRegionMapSizeX();
	const uint32_t size_y = GetWaterRegionMapSizeY();
	for (uint32_t y = 0; y < size_y; y++) {
		for (uint32_t x = 0; x < size_x; x++) {
			WaterRegionReference wr = GetWaterRegionRef(x, y);
			if (wr.MarkedValid()) wr.ForceUpdate();
		}
	}
}

void WaterRegionCheckCaches(std::function<void(std::string_view)> log)
{
	const uint32_t size_x = GetWaterRegionMapSizeX();
	const uint32_t size_y = GetWaterRegionMapSizeY();
	for (uint32_t y = 0; y < size_y; y++) {
		for (uint32_t x = 0; x < size_x; x++) {
			auto cclog = [&]<typename... T>(fmt::format_string<T...> fmtstr, T&&... args) {
				format_buffer cc_buffer;
				cc_buffer.format("Region: {} x {} to {} x {}: ", x * WATER_REGION_EDGE_LENGTH, y * WATER_REGION_EDGE_LENGTH, (x * WATER_REGION_EDGE_LENGTH) + WATER_REGION_EDGE_MASK, (y * WATER_REGION_EDGE_LENGTH) + WATER_REGION_EDGE_MASK);
				cc_buffer.format(fmtstr, std::forward<T>(args)...);
				log(cc_buffer);
			};

			WaterRegionReference wr = GetWaterRegionRef(x, y);
			if (!wr.IsInitialized()) continue;

			const bool old_has_cross_region_aqueducts = wr.HasCrossRegionAqueducts();
			const int old_number_of_patches = wr.NumberOfPatches();
			const TWaterRegionPatchLabelArray old_patch_labels = wr.CopyPatchLabelArray();

			wr.ForceUpdate();

			if (old_has_cross_region_aqueducts != wr.HasCrossRegionAqueducts()) {
				cclog("Has cross region aqueducts mismatch: {} -> {}", old_has_cross_region_aqueducts, wr.HasCrossRegionAqueducts());
			}
			if (old_number_of_patches != wr.NumberOfPatches()) {
				cclog("Number of patches mismatch: {} -> {}", old_number_of_patches, wr.NumberOfPatches());
			}
			if (old_patch_labels != wr.CopyPatchLabelArray()) {
				cclog("Patch label mismatch");
			}
		}
	}
}

void PrintWaterRegionDebugInfo(TileIndex tile)
{
	if (GetDebugLevel(DebugLevelID::map) >= 9) GetUpdatedWaterRegion(tile).PrintDebugInfo();
}
