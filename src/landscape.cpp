/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file landscape.cpp Functions related to the landscape (slopes etc.). */

/** @defgroup SnowLineGroup Snowline functions and data structures */

#include "stdafx.h"
#include "heightmap.h"
#include "clear_map.h"
#include "spritecache.h"
#include "viewport_func.h"
#include "command_func.h"
#include "landscape.h"
#include "void_map.h"
#include "tgp.h"
#include "genworld.h"
#include "fios.h"
#include "error_func.h"
#include "date_func.h"
#include "water.h"
#include "effectvehicle_func.h"
#include "landscape_cmd.h"
#include "landscape_type.h"
#include "animated_tile_func.h"
#include "core/random_func.hpp"
#include "object_base.h"
#include "company_func.h"
#include "tunnelbridge_map.h"
#include "pathfinder/aystar.h"
#include "sl/saveload.h"
#include "framerate_type.h"
#include "town.h"
#include "3rdparty/cpp-btree/btree_set.h"
#include "scope_info.h"
#include "core/ring_buffer.hpp"
#include "network/network_sync.h"
#include <array>
#include <list>
#include <set>

#include "table/strings.h"
#include "table/sprites.h"

#include INCLUDE_FOR_PREFETCH_NTA

#include "safeguards.h"

extern const TileTypeProcs
	_tile_type_clear_procs,
	_tile_type_rail_procs,
	_tile_type_road_procs,
	_tile_type_town_procs,
	_tile_type_trees_procs,
	_tile_type_station_procs,
	_tile_type_water_procs,
	_tile_type_void_procs,
	_tile_type_industry_procs,
	_tile_type_tunnelbridge_procs,
	_tile_type_object_procs;

/**
 * Tile callback functions for each type of tile.
 * @ingroup TileCallbackGroup
 * @see TileType
 */
const TileTypeProcs * const _tile_type_procs[16] = {
	&_tile_type_clear_procs,        ///< Callback functions for MP_CLEAR tiles
	&_tile_type_rail_procs,         ///< Callback functions for MP_RAILWAY tiles
	&_tile_type_road_procs,         ///< Callback functions for MP_ROAD tiles
	&_tile_type_town_procs,         ///< Callback functions for MP_HOUSE tiles
	&_tile_type_trees_procs,        ///< Callback functions for MP_TREES tiles
	&_tile_type_station_procs,      ///< Callback functions for MP_STATION tiles
	&_tile_type_water_procs,        ///< Callback functions for MP_WATER tiles
	&_tile_type_void_procs,         ///< Callback functions for MP_VOID tiles
	&_tile_type_industry_procs,     ///< Callback functions for MP_INDUSTRY tiles
	&_tile_type_tunnelbridge_procs, ///< Callback functions for MP_TUNNELBRIDGE tiles
	&_tile_type_object_procs,       ///< Callback functions for MP_OBJECT tiles
};

/** landscape slope => sprite */
extern const uint8_t _slope_to_sprite_offset[32] = {
	0, 1, 2, 3, 4, 5, 6,  7, 8, 9, 10, 11, 12, 13, 14, 0,
	0, 0, 0, 0, 0, 0, 0, 16, 0, 0,  0, 17,  0, 15, 18, 0,
};

static const uint TILE_UPDATE_FREQUENCY_LOG = 8;  ///< The logarithm of how many ticks it takes between tile updates (log base 2).
static const uint TILE_UPDATE_FREQUENCY = 1 << TILE_UPDATE_FREQUENCY_LOG;  ///< How many ticks it takes between tile updates (has to be a power of 2).

/**
 * Description of the snow line throughout the year.
 *
 * If it is \c nullptr, a static snowline height is used, as set by \c _settings_game.game_creation.snow_line_height.
 * Otherwise it points to a table loaded from a newGRF file that describes the variable snowline.
 * @ingroup SnowLineGroup
 * @see GetSnowLine() GameCreationSettings
 */
static std::unique_ptr<SnowLine> _snow_line;

/** The current spring during river generation */
static TileIndex _current_spring = INVALID_TILE;

/** Whether the current river is a big river that others flow into */
static bool _is_main_river = false;

uint8_t _cached_snowline = 0;
uint8_t _cached_highest_snowline = 0;
uint8_t _cached_lowest_snowline = 0;
uint8_t _cached_tree_placement_highest_snowline = 0;

/**
 * Map 2D viewport or smallmap coordinate to 3D world or tile coordinate.
 * Function takes into account height of tiles and foundations.
 *
 * @param x X viewport 2D coordinate.
 * @param y Y viewport 2D coordinate.
 * @param clamp_to_map Clamp the coordinate outside of the map to the closest, non-void tile within the map.
 * @param[out] clamped Whether coordinates were clamped.
 * @return 3D world coordinate of point visible at the given screen coordinate (3D perspective).
 *
 * @note Inverse of #RemapCoords2 function. Smaller values may get rounded.
 * @see InverseRemapCoords
 */
Point InverseRemapCoords2(int x, int y, bool clamp_to_map, bool *clamped)
{
	if (clamped != nullptr) *clamped = false; // Not clamping yet.

	/* Initial x/y world coordinate is like if the landscape
	 * was completely flat on height 0. */
	Point pt = InverseRemapCoords(x, y);

	const uint min_coord = _settings_game.construction.freeform_edges ? TILE_SIZE : 0;
	const uint max_x = Map::MaxX() * TILE_SIZE - 1;
	const uint max_y = Map::MaxY() * TILE_SIZE - 1;

	if (clamp_to_map) {
		/* Bring the coordinates near to a valid range. At the top we allow a number
		 * of extra tiles. This is mostly due to the tiles on the north side of
		 * the map possibly being drawn higher due to the extra height levels. */
		int extra_tiles = CeilDiv(_settings_game.construction.map_height_limit * TILE_HEIGHT, TILE_PIXELS);
		Point old_pt = pt;
		pt.x = Clamp(pt.x, -extra_tiles * TILE_SIZE, max_x);
		pt.y = Clamp(pt.y, -extra_tiles * TILE_SIZE, max_y);
		if (clamped != nullptr) *clamped = (pt.x != old_pt.x) || (pt.y != old_pt.y);
	}

	/* Now find the Z-world coordinate by fix point iteration.
	 * This is a bit tricky because the tile height is non-continuous at foundations.
	 * The clicked point should be approached from the back, otherwise there are regions that are not clickable.
	 * (FOUNDATION_HALFTILE_LOWER on SLOPE_STEEP_S hides north halftile completely)
	 * So give it a z-malus of 4 in the first iterations. */
	int z = 0;
	if (clamp_to_map) {
		for (int i = 0; i < 5; i++) z = GetSlopePixelZ(Clamp(pt.x + std::max(z, 4) - 4, min_coord, max_x), Clamp(pt.y + std::max(z, 4) - 4, min_coord, max_y)) / 2;
		for (int m = 3; m > 0; m--) z = GetSlopePixelZ(Clamp(pt.x + std::max(z, m) - m, min_coord, max_x), Clamp(pt.y + std::max(z, m) - m, min_coord, max_y)) / 2;
		for (int i = 0; i < 5; i++) z = GetSlopePixelZ(Clamp(pt.x + z,             min_coord, max_x), Clamp(pt.y + z,             min_coord, max_y)) / 2;
	} else {
		for (int i = 0; i < 5; i++) z = GetSlopePixelZOutsideMap(pt.x + std::max(z, 4) - 4, pt.y + std::max(z, 4) - 4) / 2;
		for (int m = 3; m > 0; m--) z = GetSlopePixelZOutsideMap(pt.x + std::max(z, m) - m, pt.y + std::max(z, m) - m) / 2;
		for (int i = 0; i < 5; i++) z = GetSlopePixelZOutsideMap(pt.x + z,             pt.y + z            ) / 2;
	}

	pt.x += z;
	pt.y += z;
	if (clamp_to_map) {
		Point old_pt = pt;
		pt.x = Clamp(pt.x, min_coord, max_x);
		pt.y = Clamp(pt.y, min_coord, max_y);
		if (clamped != nullptr) *clamped = *clamped || (pt.x != old_pt.x) || (pt.y != old_pt.y);
	}

	return pt;
}

/**
 * Applies a foundation to a slope.
 *
 * @pre      Foundation and slope must be valid combined.
 * @param f  The #Foundation.
 * @param s  The #Slope to modify.
 * @return   Increment to the tile Z coordinate.
 */
uint ApplyFoundationToSlope(Foundation f, Slope &s)
{
	if (!IsFoundation(f)) return 0;

	if (IsLeveledFoundation(f)) {
		uint dz = 1 + (IsSteepSlope(s) ? 1 : 0);
		s = SLOPE_FLAT;
		return dz;
	}

	if (f != FOUNDATION_STEEP_BOTH && IsNonContinuousFoundation(f)) {
		s = HalftileSlope(s, GetHalftileFoundationCorner(f));
		return 0;
	}

	if (IsSpecialRailFoundation(f)) {
		s = SlopeWithThreeCornersRaised(OppositeCorner(GetRailFoundationCorner(f)));
		return 0;
	}

	uint dz = IsSteepSlope(s) ? 1 : 0;
	Corner highest_corner = GetHighestSlopeCorner(s);

	switch (f) {
		case FOUNDATION_INCLINED_X:
			s = (((highest_corner == CORNER_W) || (highest_corner == CORNER_S)) ? SLOPE_SW : SLOPE_NE);
			break;

		case FOUNDATION_INCLINED_Y:
			s = (((highest_corner == CORNER_S) || (highest_corner == CORNER_E)) ? SLOPE_SE : SLOPE_NW);
			break;

		case FOUNDATION_STEEP_LOWER:
			s = SlopeWithOneCornerRaised(highest_corner);
			break;

		case FOUNDATION_STEEP_BOTH:
			s = HalftileSlope(SlopeWithOneCornerRaised(highest_corner), highest_corner);
			break;

		default: NOT_REACHED();
	}
	return dz;
}

/**
 * Return world \c Z coordinate of a given point of a tile. Normally this is the
 * Z of the ground/foundation at the given location, but in some cases the
 * ground/foundation can differ from the Z coordinate that the (ground) vehicle
 * passing over it would take. For example when entering a tunnel or bridge.
 *
 * @param x World X coordinate in tile "units".
 * @param y World Y coordinate in tile "units".
 * @param ground_vehicle Whether to get the Z coordinate of the ground vehicle, or the ground.
 * @return World Z coordinate at tile ground (vehicle) level, including slopes and foundations.
 */
int GetSlopePixelZ(int x, int y, bool ground_vehicle)
{
	TileIndex tile = TileVirtXY(x, y);

	return _tile_type_procs[GetTileType(tile)]->get_slope_z_proc(tile, x, y, ground_vehicle);
}

/**
 * Return world \c z coordinate of a given point of a tile,
 * also for tiles outside the map (virtual "black" tiles).
 *
 * @param x World X coordinate in tile "units", may be outside the map.
 * @param y World Y coordinate in tile "units", may be outside the map.
 * @return World Z coordinate at tile ground level, including slopes and foundations.
 */
int GetSlopePixelZOutsideMap(int x, int y)
{
	if (IsInsideBS(x, 0, Map::SizeX() * TILE_SIZE) && IsInsideBS(y, 0, Map::SizeY() * TILE_SIZE)) {
		return GetSlopePixelZ(x, y, false);
	} else {
		return _tile_type_procs[MP_VOID]->get_slope_z_proc(INVALID_TILE, x, y, false);
	}
}

/**
 * Determine the Z height of a corner relative to TileZ.
 *
 * @pre The slope must not be a halftile slope.
 *
 * @param tileh The slope.
 * @param corner The corner.
 * @return Z position of corner relative to TileZ.
 */
int GetSlopeZInCorner(Slope tileh, Corner corner)
{
	assert(!IsHalftileSlope(tileh));
	return ((tileh & SlopeWithOneCornerRaised(corner)) != 0 ? 1 : 0) + (tileh == SteepSlope(corner) ? 1 : 0);
}

/**
 * Determine the Z height of the corners of a specific tile edge
 *
 * @note If a tile has a non-continuous halftile foundation, a corner can have different heights wrt. its edges.
 *
 * @pre z1 and z2 must be initialized (typ. with TileZ). The corner heights just get added.
 *
 * @param tileh The slope of the tile.
 * @param edge The edge of interest.
 * @param z1 Gets incremented by the height of the first corner of the edge. (near corner wrt. the camera)
 * @param z2 Gets incremented by the height of the second corner of the edge. (far corner wrt. the camera)
 */
void GetSlopePixelZOnEdge(Slope tileh, DiagDirection edge, int &z1, int &z2)
{
	static const Slope corners[4][4] = {
		/*    corner     |          steep slope
		 *  z1      z2   |       z1             z2        */
		{SLOPE_E, SLOPE_N, SLOPE_STEEP_E, SLOPE_STEEP_N}, // DIAGDIR_NE, z1 = E, z2 = N
		{SLOPE_S, SLOPE_E, SLOPE_STEEP_S, SLOPE_STEEP_E}, // DIAGDIR_SE, z1 = S, z2 = E
		{SLOPE_S, SLOPE_W, SLOPE_STEEP_S, SLOPE_STEEP_W}, // DIAGDIR_SW, z1 = S, z2 = W
		{SLOPE_W, SLOPE_N, SLOPE_STEEP_W, SLOPE_STEEP_N}, // DIAGDIR_NW, z1 = W, z2 = N
	};

	int halftile_test = (IsHalftileSlope(tileh) ? SlopeWithOneCornerRaised(GetHalftileSlopeCorner(tileh)) : 0);
	if (halftile_test == corners[edge][0]) z2 += TILE_HEIGHT; // The slope is non-continuous in z2. z2 is on the upper side.
	if (halftile_test == corners[edge][1]) z1 += TILE_HEIGHT; // The slope is non-continuous in z1. z1 is on the upper side.

	if ((tileh & corners[edge][0]) != 0) z1 += TILE_HEIGHT; // z1 is raised
	if ((tileh & corners[edge][1]) != 0) z2 += TILE_HEIGHT; // z2 is raised
	if (RemoveHalftileSlope(tileh) == corners[edge][2]) z1 += TILE_HEIGHT; // z1 is highest corner of a steep slope
	if (RemoveHalftileSlope(tileh) == corners[edge][3]) z2 += TILE_HEIGHT; // z2 is highest corner of a steep slope
}

Slope UpdateFoundationSlopeFromTileSlope(TileIndex tile, Slope tileh, int &tilez)
{
	Foundation f = _tile_type_procs[GetTileType(tile)]->get_foundation_proc(tile, tileh);
	tilez += ApplyFoundationToSlope(f, tileh);
	return tileh;
}

/**
 * Get slope of a tile on top of a (possible) foundation
 * If a tile does not have a foundation, the function returns the same as GetTileSlope.
 *
 * @param tile The tile of interest.
 * @return The slope on top of the foundation and the z of the foundation slope.
 */
std::tuple<Slope, int> GetFoundationSlope(TileIndex tile)
{
	auto [tileh, z] = GetTileSlopeZ(tile);
	tileh = UpdateFoundationSlopeFromTileSlope(tile, tileh, z);
	return {tileh, z};
}


bool HasFoundationNW(TileIndex tile, Slope slope_here, uint z_here)
{
	if (IsCustomBridgeHeadTile(tile) && GetTunnelBridgeDirection(tile) == DIAGDIR_NW) return false;

	int z_W_here = z_here;
	int z_N_here = z_here;
	GetSlopePixelZOnEdge(slope_here, DIAGDIR_NW, z_W_here, z_N_here);

	auto [slope, z] = GetFoundationPixelSlope(TileAddXY(tile, 0, -1));
	int z_W = z;
	int z_N = z;
	GetSlopePixelZOnEdge(slope, DIAGDIR_SE, z_W, z_N);

	return (z_N_here > z_N) || (z_W_here > z_W);
}


bool HasFoundationNE(TileIndex tile, Slope slope_here, uint z_here)
{
	if (IsCustomBridgeHeadTile(tile) && GetTunnelBridgeDirection(tile) == DIAGDIR_NE) return false;

	int z_E_here = z_here;
	int z_N_here = z_here;
	GetSlopePixelZOnEdge(slope_here, DIAGDIR_NE, z_E_here, z_N_here);

	auto [slope, z] = GetFoundationPixelSlope(TileAddXY(tile, -1, 0));
	int z_E = z;
	int z_N = z;
	GetSlopePixelZOnEdge(slope, DIAGDIR_SW, z_E, z_N);

	return (z_N_here > z_N) || (z_E_here > z_E);
}

/**
 * Draw foundation \a f at tile \a ti. Updates \a ti.
 * @param ti Tile to draw foundation on
 * @param f  Foundation to draw
 */
void DrawFoundation(TileInfo *ti, Foundation f)
{
	if (!IsFoundation(f)) return;

	/* Two part foundations must be drawn separately */
	assert(f != FOUNDATION_STEEP_BOTH);

	uint sprite_block = 0;
	auto [slope, z] = GetFoundationPixelSlope(ti->tile);

	/* Select the needed block of foundations sprites
	 * Block 0: Walls at NW and NE edge
	 * Block 1: Wall  at        NE edge
	 * Block 2: Wall  at NW        edge
	 * Block 3: No walls at NW or NE edge
	 */
	if (!HasFoundationNW(ti->tile, slope, z)) sprite_block += 1;
	if (!HasFoundationNE(ti->tile, slope, z)) sprite_block += 2;

	/* Use the original slope sprites if NW and NE borders should be visible */
	SpriteID leveled_base = (sprite_block == 0 ? (int)SPR_FOUNDATION_BASE : (SPR_SLOPES_VIRTUAL_BASE + sprite_block * TRKFOUND_BLOCK_SIZE));
	SpriteID inclined_base = SPR_SLOPES_VIRTUAL_BASE + SLOPES_INCLINED_OFFSET + sprite_block * TRKFOUND_BLOCK_SIZE;
	SpriteID halftile_base = SPR_HALFTILE_FOUNDATION_BASE + sprite_block * HALFTILE_BLOCK_SIZE;

	if (IsSteepSlope(ti->tileh)) {
		if (!IsNonContinuousFoundation(f)) {
			/* Lower part of foundation */
			AddSortableSpriteToDraw(
				leveled_base + (ti->tileh & ~SLOPE_STEEP), PAL_NONE, ti->x, ti->y, TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1, ti->z
			);
		}

		Corner highest_corner = GetHighestSlopeCorner(ti->tileh);
		ti->z += ApplyPixelFoundationToSlope(f, ti->tileh);

		if (IsInclinedFoundation(f)) {
			/* inclined foundation */
			uint8_t inclined = highest_corner * 2 + (f == FOUNDATION_INCLINED_Y ? 1 : 0);

			AddSortableSpriteToDraw(inclined_base + inclined, PAL_NONE, ti->x, ti->y,
				f == FOUNDATION_INCLINED_X ? TILE_SIZE : 1,
				f == FOUNDATION_INCLINED_Y ? TILE_SIZE : 1,
				TILE_HEIGHT, ti->z
			);
			OffsetGroundSprite(0, 0);
		} else if (IsLeveledFoundation(f)) {
			AddSortableSpriteToDraw(leveled_base + SlopeWithOneCornerRaised(highest_corner), PAL_NONE, ti->x, ti->y, TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1, ti->z - TILE_HEIGHT);
			OffsetGroundSprite(0, -(int)TILE_HEIGHT);
		} else if (f == FOUNDATION_STEEP_LOWER) {
			/* one corner raised */
			OffsetGroundSprite(0, -(int)TILE_HEIGHT);
		} else {
			/* halftile foundation */
			int x_bb = (((highest_corner == CORNER_W) || (highest_corner == CORNER_S)) ? TILE_SIZE / 2 : 0);
			int y_bb = (((highest_corner == CORNER_S) || (highest_corner == CORNER_E)) ? TILE_SIZE / 2 : 0);

			AddSortableSpriteToDraw(halftile_base + highest_corner, PAL_NONE, ti->x + x_bb, ti->y + y_bb, TILE_SIZE / 2, TILE_SIZE / 2, TILE_HEIGHT - 1, ti->z + TILE_HEIGHT);
			/* Reposition ground sprite back to original position after bounding box change above. This is similar to
			 * RemapCoords() but without zoom scaling. */
			Point pt = {(y_bb - x_bb) * 2, y_bb + x_bb};
			OffsetGroundSprite(-pt.x, -pt.y);
		}
	} else {
		if (IsLeveledFoundation(f)) {
			/* leveled foundation */
			AddSortableSpriteToDraw(leveled_base + ti->tileh, PAL_NONE, ti->x, ti->y, TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1, ti->z);
			OffsetGroundSprite(0, -(int)TILE_HEIGHT);
		} else if (IsNonContinuousFoundation(f)) {
			/* halftile foundation */
			Corner halftile_corner = GetHalftileFoundationCorner(f);
			int x_bb = (((halftile_corner == CORNER_W) || (halftile_corner == CORNER_S)) ? TILE_SIZE / 2 : 0);
			int y_bb = (((halftile_corner == CORNER_S) || (halftile_corner == CORNER_E)) ? TILE_SIZE / 2 : 0);

			AddSortableSpriteToDraw(halftile_base + halftile_corner, PAL_NONE, ti->x + x_bb, ti->y + y_bb, TILE_SIZE / 2, TILE_SIZE / 2, TILE_HEIGHT - 1, ti->z);
			/* Reposition ground sprite back to original position after bounding box change above. This is similar to
			 * RemapCoords() but without zoom scaling. */
			Point pt = {(y_bb - x_bb) * 2, y_bb + x_bb};
			OffsetGroundSprite(-pt.x, -pt.y);
		} else if (IsSpecialRailFoundation(f)) {
			/* anti-zig-zag foundation */
			SpriteID spr;
			if (ti->tileh == SLOPE_NS || ti->tileh == SLOPE_EW) {
				/* half of leveled foundation under track corner */
				spr = leveled_base + SlopeWithThreeCornersRaised(GetRailFoundationCorner(f));
			} else {
				/* tile-slope = sloped along X/Y, foundation-slope = three corners raised */
				spr = inclined_base + 2 * GetRailFoundationCorner(f) + ((ti->tileh == SLOPE_SW || ti->tileh == SLOPE_NE) ? 1 : 0);
			}
			AddSortableSpriteToDraw(spr, PAL_NONE, ti->x, ti->y, TILE_SIZE, TILE_SIZE, TILE_HEIGHT - 1, ti->z);
			OffsetGroundSprite(0, 0);
		} else {
			/* inclined foundation */
			uint8_t inclined = GetHighestSlopeCorner(ti->tileh) * 2 + (f == FOUNDATION_INCLINED_Y ? 1 : 0);

			AddSortableSpriteToDraw(inclined_base + inclined, PAL_NONE, ti->x, ti->y,
				f == FOUNDATION_INCLINED_X ? TILE_SIZE : 1,
				f == FOUNDATION_INCLINED_Y ? TILE_SIZE : 1,
				TILE_HEIGHT, ti->z
			);
			OffsetGroundSprite(0, 0);
		}
		ti->z += ApplyPixelFoundationToSlope(f, ti->tileh);
	}
}

void DoClearSquare(TileIndex tile)
{
	/* If the tile can have animation and we clear it, delete it from the animated tile list. */
	if (MayAnimateTile(tile)) DeleteAnimatedTile(tile);

	MakeClear(tile, CLEAR_GRASS, _generating_world ? 3 : 0);
	MarkTileDirtyByTile(tile);
}

/**
 * Returns information about trackdirs and signal states.
 * If there is any trackbit at 'side', return all trackdirbits.
 * For TRANSPORT_ROAD, return no trackbits if there is no roadbit (of given subtype) at given side.
 * @param tile tile to get info about
 * @param mode transport type
 * @param sub_mode for TRANSPORT_ROAD, roadtypes to check
 * @param side side we are entering from, INVALID_DIAGDIR to return all trackbits
 * @return trackdirbits and other info depending on 'mode'
 */
TrackStatus GetTileTrackStatus(TileIndex tile, TransportType mode, uint sub_mode, DiagDirection side)
{
	return _tile_type_procs[GetTileType(tile)]->get_tile_track_status_proc(tile, mode, sub_mode, side);
}

/**
 * Change the owner of a tile
 * @param tile      Tile to change
 * @param old_owner Current owner of the tile
 * @param new_owner New owner of the tile
 */
void ChangeTileOwner(TileIndex tile, Owner old_owner, Owner new_owner)
{
	_tile_type_procs[GetTileType(tile)]->change_tile_owner_proc(tile, old_owner, new_owner);
}

void GetTileDesc(TileIndex tile, TileDesc *td)
{
	_tile_type_procs[GetTileType(tile)]->get_tile_desc_proc(tile, td);
}

/**
 * Has a snow line table already been loaded.
 * @return true if the table has been loaded already.
 * @ingroup SnowLineGroup
 */
bool IsSnowLineSet()
{
	return _snow_line != nullptr;
}

/**
 * Set a variable snow line, as loaded from a newgrf file.
 * @param snow_line The new snow line configuration.
 * @ingroup SnowLineGroup
 */
void SetSnowLine(std::unique_ptr<SnowLine> snow_line)
{
	_snow_line = std::move(snow_line);
	UpdateCachedSnowLine();
	UpdateCachedSnowLineBounds();
}

/**
 * Get the current snow line, either variable or static.
 * @return the snow line height.
 * @ingroup SnowLineGroup
 */
uint8_t GetSnowLineUncached()
{
	if (_snow_line == nullptr) return _settings_game.game_creation.snow_line_height;

	return _snow_line->table[CalTime::CurMonth()][CalTime::CurDay()];
}

void UpdateCachedSnowLine()
{
	_cached_snowline = GetSnowLineUncached();
}

/**
 * Cache the lowest and highest possible snow line heights, either variable or static.
 * @ingroup SnowLineGroup
 */
void UpdateCachedSnowLineBounds()
{
	_cached_highest_snowline = _snow_line == nullptr ? _settings_game.game_creation.snow_line_height : _snow_line->highest_value;
	_cached_lowest_snowline = _snow_line == nullptr ? _settings_game.game_creation.snow_line_height : _snow_line->lowest_value;

	uint snowline_range = ((_settings_game.construction.trees_around_snow_line_dynamic_range * (HighestSnowLine() - LowestSnowLine())) + 50) / 100;
	_cached_tree_placement_highest_snowline = LowestSnowLine() + snowline_range;
}

/**
 * Clear the variable snow line table and free the memory.
 * @ingroup SnowLineGroup
 */
void ClearSnowLine()
{
	_snow_line = nullptr;
	UpdateCachedSnowLine();
	UpdateCachedSnowLineBounds();
}

/**
 * Clear a piece of landscape
 * @param flags of operation to conduct
 * @param tile tile to clear
 * @return the cost of this operation or an error
 */
CommandCost CmdLandscapeClear(DoCommandFlag flags, TileIndex tile)
{
	CommandCost cost(EXPENSES_CONSTRUCTION);
	bool do_clear = false;
	/* Test for stuff which results in water when cleared. Then add the cost to also clear the water. */
	if ((flags & DC_FORCE_CLEAR_TILE) && HasTileWaterClass(tile) && IsTileOnWater(tile) && !IsWaterTile(tile) && !IsCoastTile(tile)) {
		if ((flags & DC_AUTO) && GetWaterClass(tile) == WATER_CLASS_CANAL) return CommandCost(STR_ERROR_MUST_DEMOLISH_CANAL_FIRST);
		do_clear = true;
		const bool is_canal = GetWaterClass(tile) == WATER_CLASS_CANAL;
		if (!is_canal && _game_mode != GM_EDITOR && !_settings_game.construction.enable_remove_water && !(flags & DC_ALLOW_REMOVE_WATER)) return CommandCost(STR_ERROR_CAN_T_BUILD_ON_WATER);
		cost.AddCost(is_canal ? _price[PR_CLEAR_CANAL] : _price[PR_CLEAR_WATER]);
	}

	Company *c = (flags & (DC_AUTO | DC_BANKRUPT)) ? nullptr : Company::GetIfValid(_current_company);
	if (c != nullptr && (int)GB(c->clear_limit, 16, 16) < 1) {
		return CommandCost(STR_ERROR_CLEARING_LIMIT_REACHED);
	}

	if ((flags & DC_TOWN) && !MayTownModifyRoad(tile)) return CMD_ERROR;

	const ClearedObjectArea *coa = FindClearedObject(tile);

	/* If this tile was the first tile which caused object destruction, always
	 * pass it on to the tile_type_proc. That way multiple test runs and the exec run stay consistent. */
	if (coa != nullptr && coa->first_tile != tile) {
		/* If this tile belongs to an object which was already cleared via another tile, pretend it has been
		 * already removed.
		 * However, we need to check stuff, which is not the same for all object tiles. (e.g. being on water or not) */

		/* If a object is removed, it leaves either bare land or water. */
		if ((flags & DC_NO_WATER) && HasTileWaterClass(tile) && IsTileOnWater(tile)) {
			return CommandCost(STR_ERROR_CAN_T_BUILD_ON_WATER);
		}
	} else {
		cost.AddCost(_tile_type_procs[GetTileType(tile)]->clear_tile_proc(tile, flags));
	}

	if (flags & DC_EXEC) {
		if (c != nullptr) c->clear_limit -= 1 << 16;
		if (do_clear) ForceClearWaterTile(tile);
	}
	return cost;
}

/**
 * Clear a big piece of landscape
 * @param flags of operation to conduct
 * @param tile end tile of area dragging
 * @param start_tile start tile of area dragging
 * @param diagonal Whether to use the Orthogonal (false) or Diagonal (true) iterator.
 * @return the cost of this operation or an error
 */
CommandCost CmdClearArea(DoCommandFlag flags, TileIndex tile, TileIndex start_tile, bool diagonal)
{
	if (start_tile >= Map::Size()) return CMD_ERROR;

	Money money = GetAvailableMoneyForCommand();
	CommandCost cost(EXPENSES_CONSTRUCTION);
	CommandCost last_error = CMD_ERROR;
	bool had_success = false;

	const Company *c = (flags & (DC_AUTO | DC_BANKRUPT)) ? nullptr : Company::GetIfValid(_current_company);
	int limit = (c == nullptr ? INT32_MAX : GB(c->clear_limit, 16, 16));

	if (tile != start_tile) flags |= DC_FORCE_CLEAR_TILE;

	OrthogonalOrDiagonalTileIterator iter(tile, start_tile, diagonal);
	for (; *iter != INVALID_TILE; ++iter) {
		TileIndex t = *iter;
		CommandCost ret = Command<CMD_LANDSCAPE_CLEAR>::Do(flags & ~DC_EXEC, t);
		if (ret.Failed()) {
			last_error = ret;

			/* We may not clear more tiles. */
			if (c != nullptr && GB(c->clear_limit, 16, 16) < 1) break;
			continue;
		}

		had_success = true;
		if (flags & DC_EXEC) {
			money -= ret.GetCost();
			if (ret.GetCost() > 0 && money < 0) {
				cost.SetAdditionalCashRequired(ret.GetCost());
				return cost;
			}
			Command<CMD_LANDSCAPE_CLEAR>::Do(flags, t);

			/* draw explosion animation...
			 * Disable explosions when game is paused. Looks silly and blocks the view. */
			if ((t == tile || t == start_tile) && _pause_mode == PM_UNPAUSED) {
				/* big explosion in two corners, or small explosion for single tiles */
				CreateEffectVehicleAbove(TileX(t) * TILE_SIZE + TILE_SIZE / 2, TileY(t) * TILE_SIZE + TILE_SIZE / 2, 2,
					TileX(tile) == TileX(start_tile) && TileY(tile) == TileY(start_tile) ? EV_EXPLOSION_SMALL : EV_EXPLOSION_LARGE
				);
			}
		} else {
			/* When we're at the clearing limit we better bail (unneed) testing as well. */
			if (ret.GetCost() != 0 && --limit <= 0) break;
		}
		cost.AddCost(ret);
	}

	return had_success ? cost : last_error;
}


TileIndex _cur_tileloop_tile;
TileIndex _aux_tileloop_tile;

static uint32_t GetTileLoopFeedback()
{
	/* The pseudorandom sequence of tiles is generated using a Galois linear feedback
	 * shift register (LFSR). This allows a deterministic pseudorandom ordering, but
	 * still with minimal state and fast iteration. */

	/* Maximal length LFSR feedback terms, from 12-bit (for 64x64 maps) to 28-bit (for 16kx16k maps).
	 * Extracted from http://www.ece.cmu.edu/~koopman/lfsr/ */
	static const uint32_t feedbacks[] = {
		0xD8F, 0x1296, 0x2496, 0x4357, 0x8679, 0x1030E, 0x206CD, 0x403FE, 0x807B8, 0x1004B2, 0x2006A8,
		0x4004B2, 0x800B87, 0x10004F3, 0x200072D, 0x40006AE, 0x80009E3,
	};
	static_assert(lengthof(feedbacks) == MAX_MAP_TILES_BITS - 2 * MIN_MAP_SIZE_BITS + 1);
	return feedbacks[Map::LogX() + Map::LogY() - 2 * MIN_MAP_SIZE_BITS];
}

static std::vector<uint> _tile_loop_counts;

void SetupTileLoopCounts()
{
	_tile_loop_counts.resize(DayLengthFactor());
	if (DayLengthFactor() == 0) return;

	uint64_t count_per_tick_fp16 = (static_cast<uint64_t>(1) << (Map::LogX() + Map::LogY() + TILE_UPDATE_FREQUENCY_LOG)) / DayLengthFactor();
	uint64_t accumulator = 0;
	for (uint &count : _tile_loop_counts) {
		accumulator += count_per_tick_fp16;
		count = static_cast<uint32_t>(accumulator >> 16);
		accumulator &= 0xFFFF;
	}
	if (accumulator > 0) _tile_loop_counts[0]++;
}

/**
 * Gradually iterate over all tiles on the map, calling their TileLoopProcs once every TILE_UPDATE_FREQUENCY ticks.
 */
void RunTileLoop(bool apply_day_length)
{
	/* We update every tile every TILE_UPDATE_FREQUENCY ticks, so divide the map size by 2^TILE_UPDATE_FREQUENCY_LOG = TILE_UPDATE_FREQUENCY */
	uint count;
	if (apply_day_length && DayLengthFactor() > 1) {
		count = _tile_loop_counts[TickSkipCounter()];
		if (count == 0) return;
	} else {
		count = 1 << (Map::LogX() + Map::LogY() - TILE_UPDATE_FREQUENCY_LOG);
	}

	PerformanceAccumulator framerate(PFE_GL_LANDSCAPE);

	const uint32_t feedback = GetTileLoopFeedback();

	TileIndex tile = _cur_tileloop_tile;
	/* The LFSR cannot have a zeroed state. */
	dbg_assert(tile != 0);

	SCOPE_INFO_FMT([&], "RunTileLoop: tile: {}x{}", TileX(tile), TileY(tile));

	/* Manually update tile 0 every TILE_UPDATE_FREQUENCY ticks - the LFSR never iterates over it itself.  */
	if (_tick_counter % TILE_UPDATE_FREQUENCY == 0) {
		_tile_type_procs[GetTileType(TileIndex(0))]->tile_loop_proc(TileIndex(0));
		count--;
	}

	while (count--) {
		/* Get the next tile in sequence using a Galois LFSR. */
		TileIndex next = TileIndex((tile.base() >> 1) ^ (-(int32_t)(tile.base() & 1) & feedback));
		if (count > 0) {
			PREFETCH_NTA(&_m[next]);
		}

		_tile_type_procs[GetTileType(tile)]->tile_loop_proc(tile);

		tile = next;
	}

	_cur_tileloop_tile = tile;
	RecordSyncEvent(NSRE_TILE);
}

void RunAuxiliaryTileLoop()
{
	/* At day lengths <= 4, flooding is handled by main tile loop */
	if (DayLengthFactor() <= 4 || (_scaled_tick_counter % 4) != 0) return;

	PerformanceAccumulator framerate(PFE_GL_LANDSCAPE);

	const uint32_t feedback = GetTileLoopFeedback();
	uint count = 1 << (Map::LogX() + Map::LogY() - 8);
	TileIndex tile = _aux_tileloop_tile;

	while (count--) {
		/* Get the next tile in sequence using a Galois LFSR. */
		TileIndex next = TileIndex((tile.base() >> 1) ^ (-(int32_t)(tile.base() & 1) & feedback));
		if (count > 0) {
			PREFETCH_NTA(&_m[next]);
		}

		if (IsFloodingTypeTile(tile) && !IsNonFloodingWaterTile(tile)) {
			FloodingBehaviour fb = GetFloodingBehaviour(tile);
			if (fb != FLOOD_NONE) TileLoopWaterFlooding(fb, tile);
		}

		tile = next;
	}

	_aux_tileloop_tile = tile;
	RecordSyncEvent(NSRE_AUX_TILE);
}

void InitializeLandscape()
{
	for (uint y = _settings_game.construction.freeform_edges ? 1 : 0; y < Map::MaxY(); y++) {
		for (uint x = _settings_game.construction.freeform_edges ? 1 : 0; x < Map::MaxX(); x++) {
			MakeClear(TileXY(x, y), CLEAR_GRASS, 3);
			SetTileHeight(TileXY(x, y), 0);
			SetTropicZone(TileXY(x, y), TROPICZONE_NORMAL);
			ClearBridgeMiddle(TileXY(x, y));
		}
	}

	for (uint x = 0; x < Map::SizeX(); x++) MakeVoid(TileXY(x, Map::MaxY()));
	for (uint y = 0; y < Map::SizeY(); y++) MakeVoid(TileXY(Map::MaxX(), y));
}

static const uint8_t _genterrain_tbl_1[5] = { 10, 22, 33, 37, 4  };
static const uint8_t _genterrain_tbl_2[5] = {  0,  0,  0,  0, 33 };

static void GenerateTerrain(int type, uint flag)
{
	uint32_t r = Random();

	/* Choose one of the templates from the graphics file. */
	const Sprite *templ = GetSprite((((r >> 24) * _genterrain_tbl_1[type]) >> 8) + _genterrain_tbl_2[type] + SPR_MAPGEN_BEGIN, SpriteType::MapGen, 0);
	if (templ == nullptr) UserError("Map generator sprites could not be loaded");

	/* Chose a random location to apply the template to. */
	uint x = r & Map::MaxX();
	uint y = (r >> Map::LogX()) & Map::MaxY();

	/* Make sure the template is not too close to the upper edges; bottom edges are checked later. */
	uint edge_distance = 1 + (_settings_game.construction.freeform_edges ? 1 : 0);
	if (x <= edge_distance || y <= edge_distance) return;

	DiagDirection direction = (DiagDirection)GB(r, 22, 2);
	uint w = templ->width;
	uint h = templ->height;

	if (DiagDirToAxis(direction) == AXIS_Y) Swap(w, h);

	const uint8_t *p = templ->data;

	if ((flag & 4) != 0) {
		/* This is only executed in secondary/tertiary loops to generate the terrain for arctic and tropic.
		 * It prevents the templates to be applied to certain parts of the map based on the flags, thus
		 * creating regions with different elevations/topography. */
		uint xw = x * Map::SizeY();
		uint yw = y * Map::SizeX();
		uint bias = (Map::SizeX() + Map::SizeY()) * 16;

		switch (flag & 3) {
			default: NOT_REACHED();
			case 0:
				if (xw + yw > Map::Size() - bias) return;
				break;

			case 1:
				if (yw < xw + bias) return;
				break;

			case 2:
				if (xw + yw < Map::Size() + bias) return;
				break;

			case 3:
				if (xw < yw + bias) return;
				break;
		}
	}

	/* Ensure the template does not overflow at the bottom edges of the map; upper edges were checked before. */
	if (x + w >= Map::MaxX()) return;
	if (y + h >= Map::MaxY()) return;

	TileIndex tile = TileXY(x, y);

	/* Get the template and overlay in a particular direction over the map's height from the given
	 * origin point (tile), and update the map's height everywhere where the height from the template
	 * is higher than the height of the map. In other words, this only raises the tile heights. */
	switch (direction) {
		default: NOT_REACHED();
		case DIAGDIR_NE:
			do {
				TileIndex tile_cur = tile;

				for (uint w_cur = w; w_cur != 0; --w_cur) {
					if (GB(*p, 0, 4) >= TileHeight(tile_cur)) SetTileHeight(tile_cur, GB(*p, 0, 4));
					p++;
					tile_cur += TileDiffXY(1, 0);
				}
				tile += TileDiffXY(0, 1);
			} while (--h != 0);
			break;

		case DIAGDIR_SE:
			do {
				TileIndex tile_cur = tile;

				for (uint h_cur = h; h_cur != 0; --h_cur) {
					if (GB(*p, 0, 4) >= TileHeight(tile_cur)) SetTileHeight(tile_cur, GB(*p, 0, 4));
					p++;
					tile_cur += TileDiffXY(0, 1);
				}
				tile += TileDiffXY(1, 0);
			} while (--w != 0);
			break;

		case DIAGDIR_SW:
			tile += TileDiffXY(w - 1, 0);
			do {
				TileIndex tile_cur = tile;

				for (uint w_cur = w; w_cur != 0; --w_cur) {
					if (GB(*p, 0, 4) >= TileHeight(tile_cur)) SetTileHeight(tile_cur, GB(*p, 0, 4));
					p++;
					tile_cur -= TileDiffXY(1, 0);
				}
				tile += TileDiffXY(0, 1);
			} while (--h != 0);
			break;

		case DIAGDIR_NW:
			tile += TileDiffXY(0, h - 1);
			do {
				TileIndex tile_cur = tile;

				for (uint h_cur = h; h_cur != 0; --h_cur) {
					if (GB(*p, 0, 4) >= TileHeight(tile_cur)) SetTileHeight(tile_cur, GB(*p, 0, 4));
					p++;
					tile_cur -= TileDiffXY(0, 1);
				}
				tile += TileDiffXY(1, 0);
			} while (--w != 0);
			break;
	}
}


#include "table/genland.h"

static std::pair<const Rect16 *, const Rect16 *> GetDesertOrRainforestData()
{
	switch (_settings_game.game_creation.coast_tropics_width) {
		case 0:
			return { _make_desert_or_rainforest_data, endof(_make_desert_or_rainforest_data) };
		case 1:
			return { _make_desert_or_rainforest_data_medium, endof(_make_desert_or_rainforest_data_medium) };
		case 2:
			return { _make_desert_or_rainforest_data_large, endof(_make_desert_or_rainforest_data_large) };
		case 3:
			return { _make_desert_or_rainforest_data_extralarge, endof(_make_desert_or_rainforest_data_extralarge) };
		default:
			NOT_REACHED();
	}
}

template <typename F>
bool DesertOrRainforestProcessTiles(const std::pair<const Rect16 *, const Rect16 *> desert_rainforest_data, TileIndex tile, F handle_tile)
{
	for (const Rect16 *data = desert_rainforest_data.first; data != desert_rainforest_data.second; ++data) {
		const Rect16 r = *data;
		for (int16_t x = r.left; x <= r.right; x++) {
			for (int16_t y = r.top; y <= r.bottom; y++) {
				TileIndex t = AddTileIndexDiffCWrap(tile, { x, y });
				if (handle_tile(t)) return false;
			}
		}
	}
	return true;
}

static void CreateDesertOrRainForest(uint desert_tropic_line)
{
	uint update_freq = Map::Size() / 4;

	const std::pair<const Rect16 *, const Rect16 *> desert_rainforest_data = GetDesertOrRainforestData();

	for (TileIndex tile(0); tile != Map::Size(); ++tile) {
		if ((tile.base() % update_freq) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		if (!IsValidTile(tile)) continue;

		bool ok = DesertOrRainforestProcessTiles(desert_rainforest_data, tile, [&](TileIndex t) -> bool {
			return (t != INVALID_TILE && (TileHeight(t) >= desert_tropic_line || IsTileType(t, MP_WATER)));
		});
		if (ok) {
			SetTropicZone(tile, TROPICZONE_DESERT);
		}
	}

	for (uint i = 0; i != TILE_UPDATE_FREQUENCY; i++) {
		if ((i % 64) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		RunTileLoop();
	}

	for (TileIndex tile(0); tile != Map::Size(); ++tile) {
		if ((tile.base() % update_freq) == 0) IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

		if (!IsValidTile(tile)) continue;

		bool ok = DesertOrRainforestProcessTiles(desert_rainforest_data, tile, [&](TileIndex t) -> bool {
			return (t != INVALID_TILE && IsTileType(t, MP_CLEAR) && IsClearGround(t, CLEAR_DESERT));
		});
		if (ok) {
			SetTropicZone(tile, TROPICZONE_RAINFOREST);
		}
	}
}

/**
 * Find the spring of a river.
 * @param tile The tile to consider for being the spring.
 * @return True iff it is suitable as a spring.
 */
static bool FindSpring(TileIndex tile, void *)
{
	int reference_height;
	if (!IsTileFlat(tile, &reference_height) || IsWaterTile(tile)) return false;

	/* In the tropics rivers start in the rainforest. */
	if (_settings_game.game_creation.landscape == LandscapeType::Tropic && GetTropicZone(tile) != TROPICZONE_RAINFOREST && !_settings_game.game_creation.lakes_allowed_in_deserts) return false;

	/* Are there enough higher tiles to warrant a 'spring'? */
	uint num = 0;
	for (int dx = -1; dx <= 1; dx++) {
		for (int dy = -1; dy <= 1; dy++) {
			TileIndex t = TileAddWrap(tile, dx, dy);
			if (t != INVALID_TILE && GetTileMaxZ(t) > reference_height) num++;
		}
	}

	if (num < 4) return false;

	if (_settings_game.game_creation.rivers_top_of_hill) {
		/* Are we near the top of a hill? */
		for (int dx = -16; dx <= 16; dx++) {
			for (int dy = -16; dy <= 16; dy++) {
				TileIndex t = TileAddWrap(tile, dx, dy);
				if (t != INVALID_TILE && GetTileMaxZ(t) > reference_height + 2) return false;
			}
		}
	}

	return true;
}

struct MakeLakeData {
	TileIndex centre;            ///< Lake centre tile
	uint height;                 ///< Lake height
	int max_distance;            ///< Max radius
	int secondary_axis_scale;    ///< Multiplier for ellipse narrow axis, 16 bit fixed point
	int sin_fp;                  ///< sin of ellipse rotation angle, 16 bit fixed point
	int cos_fp;                  ///< cos of ellipse rotation angle, 16 bit fixed point
};

/**
 * Make a connected lake; fill all tiles in the circular tile search that are connected.
 * @param tile The tile to consider for lake making.
 * @param user_data The height of the lake.
 * @return Always false, so it continues searching.
 */
static bool MakeLake(TileIndex tile, void *user_data)
{
	const MakeLakeData *data = (const MakeLakeData *)user_data;
	if (!IsValidTile(tile) || TileHeight(tile) != data->height || !IsTileFlat(tile)) return false;
	if (_settings_game.game_creation.landscape == LandscapeType::Tropic && GetTropicZone(tile) == TROPICZONE_DESERT && !_settings_game.game_creation.lakes_allowed_in_deserts) return false;

	/* Offset from centre tile */
	const int64_t x_delta = (int)TileX(tile) - (int)TileX(data->centre);
	const int64_t y_delta = (int)TileY(tile) - (int)TileY(data->centre);

	/* Rotate to new coordinate system */
	const int64_t a_delta = (x_delta * data->cos_fp + y_delta * data->sin_fp) >> 8;
	const int64_t b_delta = (-x_delta * data->sin_fp + y_delta * data->cos_fp) >> 8;

	int max_distance = data->max_distance;
	if (max_distance >= 6) {
		/* Vary radius a bit for larger lakes */
		uint coord = (std::abs(x_delta) > std::abs(y_delta)) ? TileY(tile) : TileX(tile);
		static const int8_t offset_fuzz[4] = { 0, 1, 0, -1 };
		max_distance += offset_fuzz[(coord / 3) & 3];
	}

	/* Check if inside ellipse */
	if ((a_delta * a_delta) + ((data->secondary_axis_scale * b_delta * b_delta) >> 16) > ((int64_t)(max_distance * max_distance) << 16)) return false;

	for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
		TileIndex t2 = tile + TileOffsByDiagDir(d);
		if (IsWaterTile(t2)) {
			MakeRiver(tile, Random());
			MarkTileDirtyByTile(tile);
			/* Remove desert directly around the river tile. */
			IterateCurvedCircularTileArea(tile, _settings_game.game_creation.lake_tropics_width, RiverModifyDesertZone, nullptr);
			return false;
		}
	}

	return false;
}

/**
 * Check whether a river at begin could (logically) flow down to end.
 * @param begin The origin of the flow.
 * @param end The destination of the flow.
 * @return True iff the water can be flowing down.
 */
static bool FlowsDown(TileIndex begin, TileIndex end)
{
	dbg_assert(DistanceManhattan(begin, end) == 1);

	auto [slope_end, height_end] = GetTileSlopeZ(end);

	/* Slope either is inclined or flat; rivers don't support other slopes. */
	if (slope_end != SLOPE_FLAT && !IsInclinedSlope(slope_end)) return false;

	auto [slope_begin, height_begin] = GetTileSlopeZ(begin);

	/* It can't flow uphill. */
	if (height_end > height_begin) return false;

	/* Slope continues, then it must be lower... */
	if (slope_end == slope_begin && height_end < height_begin) return true;

	/* ... or either end must be flat. */
	return slope_end == SLOPE_FLAT || slope_begin == SLOPE_FLAT;
}

/* AyStar callback for checking whether we reached our destination. */
static AyStarStatus River_EndNodeCheck(const AyStar *aystar, const OpenListNode *current)
{
	return current->path.node.tile == *static_cast<TileIndex *>(aystar->user_target) ? AyStarStatus::FoundEndNode : AyStarStatus::Done;
}

/* AyStar callback for getting the cost of the current node. */
static int32_t River_CalculateG(AyStar *aystar, AyStarNode *current, OpenListNode *parent)
{
	return 1 + RandomRange(_settings_game.game_creation.river_route_random);
}

/* AyStar callback for getting the estimated cost to the destination. */
static int32_t River_CalculateH(AyStar *aystar, AyStarNode *current, OpenListNode *parent)
{
	return DistanceManhattan(*static_cast<TileIndex *>(aystar->user_target), current->tile);
}

/* AyStar callback for getting the neighbouring nodes of the given node. */
static void River_GetNeighbours(AyStar *aystar, OpenListNode *current)
{
	TileIndex tile = current->path.node.tile;

	aystar->num_neighbours = 0;
	for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
		TileIndex t = tile + TileOffsByDiagDir(d);
		if (IsValidTile(t) && FlowsDown(tile, t)) {
			aystar->neighbours[aystar->num_neighbours].tile = t;
			aystar->neighbours[aystar->num_neighbours].direction = INVALID_TRACKDIR;
			aystar->num_neighbours++;
		}
	}
}

/** Callback to widen a river tile. */
static bool RiverMakeWider(TileIndex tile, void *user_data)
{
	if (IsValidTile(tile) && !IsWaterTile(tile) && GetTileSlope(tile) == GetTileSlope(*(TileIndex *)user_data)) {
		MakeRiver(tile, Random());
		/* Remove desert directly around the river tile. */

		MarkTileDirtyByTile(tile);
		IterateCurvedCircularTileArea(tile, _settings_game.game_creation.river_tropics_width, RiverModifyDesertZone, nullptr);
	}
	return false;
}

/* AyStar callback when an route has been found. */
static void River_FoundEndNode(AyStar *aystar, OpenListNode *current)
{
	for (PathNode *path = &current->path; path != nullptr; path = path->parent) {
		TileIndex tile = path->node.tile;
		if (!IsWaterTile(tile)) {
			MakeRiver(tile, Random());

			// Widen river depending on how far we are away from the source.
			const uint current_river_length = DistanceManhattan(_current_spring, path->node.tile);
			const uint long_river_length = _settings_game.game_creation.min_river_length * 4;
			const uint radius = std::min(3u, (current_river_length / (long_river_length / 3u)) + 1u);

			MarkTileDirtyByTile(tile);

			if (_settings_game.game_creation.land_generator != LG_ORIGINAL && _is_main_river && (radius > 1)) {
				CircularTileSearch(&tile, radius + RandomRange(1), RiverMakeWider, (void *)&path->node.tile);
			} else {
				/* Remove desert directly around the river tile. */
				IterateCurvedCircularTileArea(tile, _settings_game.game_creation.river_tropics_width, RiverModifyDesertZone, nullptr);
			}
		}
	}
}

static const uint RIVER_HASH_SIZE = 8; ///< The number of bits the hash for river finding should have.

/**
 * Actually build the river between the begin and end tiles using AyStar.
 * @param begin The begin of the river.
 * @param end The end of the river.
 */
static void BuildRiver(TileIndex begin, TileIndex end)
{
	AyStar finder = {};
	finder.CalculateG = River_CalculateG;
	finder.CalculateH = River_CalculateH;
	finder.GetNeighbours = River_GetNeighbours;
	finder.EndNodeCheck = River_EndNodeCheck;
	finder.FoundEndNode = River_FoundEndNode;
	finder.user_target = &end;
	finder.max_search_nodes = 100 * AYSTAR_DEF_MAX_SEARCH_NODES;

	finder.Init(1 << RIVER_HASH_SIZE);

	AyStarNode start;
	start.tile = begin;
	start.direction = INVALID_TRACKDIR;
	finder.AddStartNode(&start, 0);
	finder.Main();
	finder.Free();
}

/**
 * Try to flow the river down from a given begin.
 * @param spring The springing point of the river.
 * @param begin  The begin point we are looking from; somewhere down hill from the spring.
 * @param min_river_length The minimum length for the river.
 * @return True iff a river could/has been built, otherwise false.
 */
static bool FlowRiver(TileIndex spring, TileIndex begin, uint min_river_length)
{
	uint height_begin = TileHeight(begin);

	if (IsWaterTile(begin)) {
		if (GetTileZ(begin) == 0) {
			_is_main_river = true;
		}

		return DistanceManhattan(spring, begin) > min_river_length;
	}

	btree::btree_set<TileIndex> marks;
	marks.insert(begin);

	/* Breadth first search for the closest tile we can flow down to. */
	ring_buffer<TileIndex> queue;
	queue.push_back(begin);

	bool found = false;
	uint count = 0; // Number of tiles considered; to be used for lake location guessing.
	TileIndex end;
	do {
		end = queue.front();
		queue.pop_front();

		uint height_end = TileHeight(end);
		if (IsTileFlat(end) && (height_end < height_begin || (height_end == height_begin && IsWaterTile(end)))) {
			found = true;
			break;
		}

		for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++) {
			TileIndex t = end + TileOffsByDiagDir(d);
			if (IsValidTile(t) && !marks.contains(t) && FlowsDown(end, t)) {
				marks.insert(t);
				count++;
				queue.push_back(t);
			}
		}
	} while (!queue.empty());

	if (found) {
		/* Flow further down hill. */
		found = FlowRiver(spring, end, min_river_length);
	} else if (count > 32 && _settings_game.game_creation.lake_size != 0) {
		/* Maybe we can make a lake. Find the Nth of the considered tiles. */
		TileIndex lake_centre(0);
		int i = RandomRange(count - 1) + 1;
		btree::btree_set<TileIndex>::const_iterator cit = marks.begin();
		while (--i) cit++;
		lake_centre = *cit;

		if (IsValidTile(lake_centre) &&
				/* A river, or lake, can only be built on flat slopes. */
				IsTileFlat(lake_centre) &&
				/* We want the lake to be built at the height of the river. */
				TileHeight(begin) == TileHeight(lake_centre) &&
				/* We don't want the lake at the entry of the valley. */
				lake_centre != begin &&
				/* We don't want lakes in the desert. */
				(_settings_game.game_creation.landscape != LandscapeType::Tropic || _settings_game.game_creation.lakes_allowed_in_deserts || GetTropicZone(lake_centre) != TROPICZONE_DESERT) &&
				/* We only want a lake if the river is long enough. */
				DistanceManhattan(spring, lake_centre) > min_river_length) {
			end = lake_centre;
			MakeRiver(lake_centre, Random());
			MarkTileDirtyByTile(lake_centre);
			/* Remove desert directly around the river tile. */
			IterateCurvedCircularTileArea(lake_centre, _settings_game.game_creation.river_tropics_width, RiverModifyDesertZone, nullptr);

			// Setting lake size +- 25%
			const auto random_percentage = 75 + RandomRange(50);
			const uint range = ((_settings_game.game_creation.lake_size * random_percentage) / 100) + 3;

			MakeLakeData data;
			data.centre = lake_centre;
			data.height = height_begin;
			data.max_distance = range / 2;

			/* Square of ratio of ellipse dimensions: 1 to 5 (16 bit fixed point) */
			data.secondary_axis_scale = (1 << 16) + RandomRange(1 << 18);

			/* Range from -1 to 1 (16 bit fixed point) */
			data.sin_fp = RandomRange(1 << 17) - (1 << 16);

			/* sin^2 + cos^2 = 1 */
			data.cos_fp = IntSqrt64(((int64_t)1 << 32) - ((int64_t)data.sin_fp * (int64_t)data.sin_fp));

			CircularTileSearch(&lake_centre, range, MakeLake, &data);
			/* Call the search a second time so artefacts from going circular in one direction get (mostly) hidden. */
			lake_centre = end;
			CircularTileSearch(&lake_centre, range, MakeLake, &data);
			found = true;
		}
	}

	marks.clear();
	if (found) BuildRiver(begin, end);
	return found;
}

/**
 * Actually (try to) create some rivers.
 */
static void CreateRivers()
{
	int amount = _settings_game.game_creation.amount_of_rivers;
	if (amount == 0) return;

	uint wells = Map::ScaleBySize(4 << _settings_game.game_creation.amount_of_rivers);
	const uint num_short_rivers = wells - std::max(1u, wells / 10);
	SetGeneratingWorldProgress(GWP_RIVER, wells + TILE_UPDATE_FREQUENCY / 64); // Include the tile loop calls below.

	for (; wells > num_short_rivers; wells--) {
		IncreaseGeneratingWorldProgress(GWP_RIVER);
		for (int tries = 0; tries < 128; tries++) {
			TileIndex t = RandomTile();
			if (!CircularTileSearch(&t, 8, FindSpring, nullptr)) continue;
			_current_spring = t;
			_is_main_river = false;
			if (FlowRiver(t, t, _settings_game.game_creation.min_river_length * 4)) break;
		}
	}

	for (; wells != 0; wells--) {
		IncreaseGeneratingWorldProgress(GWP_RIVER);
		for (int tries = 0; tries < 128; tries++) {
			TileIndex t = RandomTile();
			if (!CircularTileSearch(&t, 8, FindSpring, nullptr)) continue;
			_current_spring = t;
			_is_main_river = false;
			if (FlowRiver(t, t, _settings_game.game_creation.min_river_length)) break;
		}
	}

	/* Widening rivers may have left some tiles requiring to be watered. */
	ConvertGroundTilesIntoWaterTiles();

	/* Run tile loop to update the ground density. */
	for (uint i = 0; i != TILE_UPDATE_FREQUENCY; i++) {
		if (i % 64 == 0) IncreaseGeneratingWorldProgress(GWP_RIVER);
		RunTileLoop();
	}
}

/**
 * Calculate what height would be needed to cover N% of the landmass.
 *
 * The function allows both snow and desert/tropic line to be calculated. It
 * tries to find the closest height which covers N% of the landmass; it can
 * be below or above it.
 *
 * Tropic has a mechanism where water and tropic tiles in mountains grow
 * inside the desert. To better approximate the requested coverage, this is
 * taken into account via an edge histogram, which tells how many neighbouring
 * tiles are lower than the tiles of that height. The multiplier indicates how
 * severe this has to be taken into account.
 *
 * @param coverage A value between 0 and 100 indicating a percentage of landmass that should be covered.
 * @param edge_multiplier How much effect neighbouring tiles that are of a lower height level have on the score.
 * @return The estimated best height to use to cover N% of the landmass.
 */
static uint CalculateCoverageLine(uint coverage, uint edge_multiplier)
{
	/* Histogram of how many tiles per height level exist. */
	std::array<int, MAX_TILE_HEIGHT + 1> histogram = {};
	/* Histogram of how many neighbour tiles are lower than the tiles of the height level. */
	std::array<int, MAX_TILE_HEIGHT + 1> edge_histogram = {};

	/* Build a histogram of the map height. */
	for (TileIndex tile(0); tile < Map::Size(); ++tile) {
		uint h = TileHeight(tile);
		histogram[h]++;

		if (edge_multiplier != 0) {
			/* Check if any of our neighbours is below us. */
			for (DiagDirection dir = DIAGDIR_BEGIN; dir != DIAGDIR_END; dir++) {
				TileIndex neighbour_tile = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(dir));
				if (IsValidTile(neighbour_tile) && TileHeight(neighbour_tile) < h) {
					edge_histogram[h]++;
				}
			}
		}
	}

	/* The amount of land we have is the map size minus the first (sea) layer. */
	uint land_tiles = Map::Size() - histogram[0];
	int best_score = land_tiles;

	/* Our goal is the coverage amount of the land-mass. */
	int goal_tiles = land_tiles * coverage / 100;

	/* We scan from top to bottom. */
	uint h = MAX_TILE_HEIGHT;
	uint best_h = h;

	int current_tiles = 0;
	for (; h > 0; h--) {
		current_tiles += histogram[h];
		int current_score = goal_tiles - current_tiles;

		/* Tropic grows from water and mountains into the desert. This is a
		 * great visual, but it also means we* need to take into account how
		 * much less desert tiles are being created if we are on this
		 * height-level. We estimate this based on how many neighbouring
		 * tiles are below us for a given length, assuming that is where
		 * tropic is growing from.
		 */
		if (edge_multiplier != 0 && h > 1) {
			/* From water tropic tiles grow for a few tiles land inward. */
			current_score -= edge_histogram[1] * edge_multiplier;
			/* Tropic tiles grow into the desert for a few tiles. */
			current_score -= edge_histogram[h] * edge_multiplier;
		}

		if (std::abs(current_score) < std::abs(best_score)) {
			best_score = current_score;
			best_h = h;
		}

		/* Always scan all height-levels, as h == 1 might give a better
		 * score than any before. This is true for example with 0% desert
		 * coverage. */
	}

	return best_h;
}

/**
 * Calculate the line from which snow begins.
 */
static void CalculateSnowLine()
{
	if (_settings_game.game_creation.climate_threshold_mode == 0) {
		/* We do not have snow sprites on coastal tiles, so never allow "1" as height. */
		_settings_game.game_creation.snow_line_height = std::max(CalculateCoverageLine(_settings_game.game_creation.snow_coverage, 0), 2u);
	}
	UpdateCachedSnowLine();
	UpdateCachedSnowLineBounds();
}

/**
 * Calculate the line (in height) between desert and tropic.
 * @return The height of the line between desert and tropic.
 */
static uint8_t CalculateDesertLine()
{
	if (_settings_game.game_creation.climate_threshold_mode != 0) return _settings_game.game_creation.rainforest_line_height;

	/* CalculateCoverageLine() runs from top to bottom, so we need to invert the coverage. */
	return CalculateCoverageLine(100 - _settings_game.game_creation.desert_coverage, 4);
}

bool GenerateLandscape(uint8_t mode)
{
	/* Number of steps of landscape generation */
	static constexpr uint GLS_HEIGHTMAP = 3; ///< Loading a heightmap
	static constexpr uint GLS_TERRAGENESIS = 4; ///< Terragenesis generator
	static constexpr uint GLS_ORIGINAL = 2; ///< Original generator
	static constexpr uint GLS_TROPIC = 12; ///< Extra steps needed for tropic landscape
	static constexpr uint GLS_OTHER = 0; ///< Extra steps for other landscapes
	uint steps = (_settings_game.game_creation.landscape == LandscapeType::Tropic) ? GLS_TROPIC : GLS_OTHER;

	if (mode == GWM_HEIGHTMAP) {
		SetGeneratingWorldProgress(GWP_LANDSCAPE, steps + GLS_HEIGHTMAP);
		if (!LoadHeightmap(_file_to_saveload.detail_ftype, _file_to_saveload.name.c_str())) {
			return false;
		}
		IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);
	} else if (_settings_game.game_creation.land_generator == LG_TERRAGENESIS) {
		SetGeneratingWorldProgress(GWP_LANDSCAPE, steps + GLS_TERRAGENESIS);
		GenerateTerrainPerlin();
	} else {
		SetGeneratingWorldProgress(GWP_LANDSCAPE, steps + GLS_ORIGINAL);
		if (_settings_game.construction.freeform_edges) {
			for (uint x = 0; x < Map::SizeX(); x++) MakeVoid(TileXY(x, 0));
			for (uint y = 0; y < Map::SizeY(); y++) MakeVoid(TileXY(0, y));
		}
		switch (_settings_game.game_creation.landscape) {
			case LandscapeType::Arctic: {
				uint32_t r = Random();

				for (uint i = Map::ScaleBySize(GB(r, 0, 7) + 950); i != 0; --i) {
					GenerateTerrain(2, 0);
				}

				uint flag = GB(r, 7, 2) | 4;
				for (uint i = Map::ScaleBySize(GB(r, 9, 7) + 450); i != 0; --i) {
					GenerateTerrain(4, flag);
				}
				break;
			}

			case LandscapeType::Tropic: {
				uint32_t r = Random();

				for (uint i = Map::ScaleBySize(GB(r, 0, 7) + 170); i != 0; --i) {
					GenerateTerrain(0, 0);
				}

				uint flag = GB(r, 7, 2) | 4;
				for (uint i = Map::ScaleBySize(GB(r, 9, 8) + 1700); i != 0; --i) {
					GenerateTerrain(0, flag);
				}

				flag ^= 2;

				for (uint i = Map::ScaleBySize(GB(r, 17, 7) + 410); i != 0; --i) {
					GenerateTerrain(3, flag);
				}
				break;
			}

			default: {
				uint32_t r = Random();

				assert(_settings_game.difficulty.quantity_sea_lakes != CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY);
				uint i = Map::ScaleBySize(GB(r, 0, 7) + (3 - _settings_game.difficulty.quantity_sea_lakes) * 256 + 100);
				for (; i != 0; --i) {
					/* Make sure we do not overflow. */
					GenerateTerrain(Clamp(_settings_game.difficulty.terrain_type, 0, 3), 0);
				}
				break;
			}
		}
	}

	/* Do not call IncreaseGeneratingWorldProgress() before FixSlopes(),
	 * it allows screen redraw. Drawing of broken slopes crashes the game */
	FixSlopes();
	MarkWholeScreenDirty();
	IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

	ConvertGroundTilesIntoWaterTiles();
	MarkWholeScreenDirty();
	IncreaseGeneratingWorldProgress(GWP_LANDSCAPE);

	switch (_settings_game.game_creation.landscape) {
		case LandscapeType::Arctic:
			CalculateSnowLine();
			break;

		case LandscapeType::Tropic: {
			uint desert_tropic_line = CalculateDesertLine();
			CreateDesertOrRainForest(desert_tropic_line);
			break;
		}

		default:
			break;
	}

	CreateRivers();
	return true;
}

void OnTick_Town();
void OnTick_Trees();
void OnTick_Station();
void OnTick_Industry();

void CallLandscapeTick()
{
	{
		PerformanceAccumulator framerate(PFE_GL_LANDSCAPE);

		OnTick_Town();
		RecordSyncEvent(NSRE_TOWN);
		OnTick_Trees();
		RecordSyncEvent(NSRE_TREE);
		OnTick_Station();
		RecordSyncEvent(NSRE_STATION);
		OnTick_Industry();
		RecordSyncEvent(NSRE_INDUSTRY);
	}
}
