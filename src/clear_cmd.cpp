/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file clear_cmd.cpp Commands related to clear tiles. */

#include "stdafx.h"
#include "clear_map.h"
#include "command_func.h"
#include "landscape.h"
#include "landscape_cmd.h"
#include "genworld.h"
#include "viewport_func.h"
#include "core/random_func.hpp"
#include "newgrf_generic.h"
#include "newgrf_newlandscape.h"
#include "tree_func.h"

#include "table/strings.h"
#include "table/sprites.h"
#include "table/clear_land.h"

#include "safeguards.h"

/** @copydoc ClearTileProc */
static CommandCost ClearTile_Clear(TileIndex tile, DoCommandFlags flags)
{
	static constexpr Price clear_price_table[to_underlying(ClearGround::MaxSize)] = {
		Price::ClearGrass, // Base price for clearing grass.
		Price::ClearRough, // Base price for clearing rough land.
		Price::ClearRocks, // Base price for clearing rocks.
		Price::ClearFields, // Base price for clearing fields.
		Price::ClearRough, // Unused.
		Price::ClearRough, // Base price for clearing desert.
		Price::ClearRough, // Unused.
		Price::ClearRough, // Unused.
	};
	CommandCost price(EXPENSES_CONSTRUCTION);

	ClearGround ground = GetClearGround(tile);
	uint8_t density = GetClearDensity(tile);
	if (IsSnowTile(tile)) {
		price.AddCost(_price[clear_price_table[to_underlying(ground)]]);
		/* Add a little more for removing snow. */
		price.AddCost(std::abs(_price[Price::ClearRough] - _price[Price::ClearGrass]));
	} else if (ground != ClearGround::Grass || density != 0) {
		price.AddCost(_price[clear_price_table[to_underlying(ground)]]);
	}

	if (flags.Test(DoCommandFlag::Execute)) DoClearSquare(tile);

	return price;
}

SpriteID GetSpriteIDForClearLand(const Slope slope, uint8_t set)
{
	return SPR_FLAT_BARE_LAND + SlopeToSpriteOffset(slope) + set * 19;
}

void DrawClearLandTile(const TileInfo *ti, uint8_t set)
{
	DrawGroundSprite(GetSpriteIDForClearLand(ti->tileh, set), PAL_NONE);
}

SpriteID GetSpriteIDForHillyLand(const Slope slope, const uint rough_index)
{
	if (slope != SLOPE_FLAT) {
		return SPR_FLAT_ROUGH_LAND + SlopeToSpriteOffset(slope);
	} else {
		return _landscape_clear_sprites_rough[rough_index];
	}
}

void DrawHillyLandTile(const TileInfo *ti)
{
	DrawGroundSprite(GetSpriteIDForHillyLand(ti->tileh, GB(TileHash(ti->x, ti->y), 4, 3)), PAL_NONE);
}

SpriteID GetSpriteIDForRocks(const Slope slope, const uint tile_hash)
{
	return ((HasGrfMiscBit(GrfMiscBit::SecondRockyTileSet) && (tile_hash & 1)) ? SPR_FLAT_ROCKY_LAND_2 : SPR_FLAT_ROCKY_LAND_1) + SlopeToSpriteOffset(slope);
}

inline SpriteID GetSpriteIDForRocksUsingOffset(const uint slope_to_sprite_offset, const uint x, const uint y)
{
	return ((HasGrfMiscBit(GrfMiscBit::SecondRockyTileSet) && (TileHash(x, y) & 1)) ? SPR_FLAT_ROCKY_LAND_2 : SPR_FLAT_ROCKY_LAND_1) + slope_to_sprite_offset;
}

bool DrawCustomSpriteIDForRocks(const TileInfo *ti, uint8_t slope_to_sprite_offset, bool require_snow_flag)
{
	for (const GRFFile *grf : _new_landscape_rocks_grfs) {
		if (require_snow_flag && !HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_DRAW_SNOWY_ENABLED)) continue;

		NewLandscapeResolverObject object(grf, ti, NEW_LANDSCAPE_ROCKS);

		const ResultSpriteGroup *group = object.Resolve<ResultSpriteGroup>();
		if (group != nullptr && group->num_sprites > slope_to_sprite_offset) {
			PaletteID pal = HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_RECOLOUR_ENABLED) ? GB(GetRegister(0x100), 0, 24) : PAL_NONE;
			DrawGroundSprite(group->sprite + slope_to_sprite_offset, pal);
			return true;
		}
	}

	return false;
}

SpriteID GetSpriteIDForFields(const Slope slope, const uint field_type)
{
	return _clear_land_sprites_farmland[field_type] + SlopeToSpriteOffset(slope);
}

inline SpriteID GetSpriteIDForSnowDesertUsingOffset(const uint slope_to_sprite_offset, const uint density)
{
	return _clear_land_sprites_snow_desert[density] + slope_to_sprite_offset;
}

SpriteID GetSpriteIDForSnowDesert(const Slope slope, const uint density)
{
	return _clear_land_sprites_snow_desert[density] + SlopeToSpriteOffset(slope);
}

static void DrawClearLandFence(const TileInfo *ti)
{
	/* combine fences into one sprite object */
	StartSpriteCombine();

	SpriteBounds bounds{{}, {TILE_SIZE, TILE_SIZE, 4}, {}};

	bounds.extent.z += GetSlopeMaxPixelZ(ti->tileh);

	uint fence_nw = GetFence(ti->tile, DIAGDIR_NW);
	if (fence_nw != 0) {
		bounds.offset.x = 0;
		bounds.offset.y = -static_cast<int>(TILE_SIZE);
		bounds.offset.z = GetSlopePixelZInCorner(ti->tileh, CORNER_W);
		SpriteID sprite = _clear_land_fence_sprites[fence_nw - 1] + _fence_mod_by_tileh_nw[ti->tileh];
		AddSortableSpriteToDraw(sprite, PAL_NONE, *ti, bounds, false);
	}

	uint fence_ne = GetFence(ti->tile, DIAGDIR_NE);
	if (fence_ne != 0) {
		bounds.offset.x = -static_cast<int>(TILE_SIZE);
		bounds.offset.y = 0;
		bounds.offset.z = GetSlopePixelZInCorner(ti->tileh, CORNER_E);
		SpriteID sprite = _clear_land_fence_sprites[fence_ne - 1] + _fence_mod_by_tileh_ne[ti->tileh];
		AddSortableSpriteToDraw(sprite, PAL_NONE, *ti, bounds, false);
	}

	uint fence_sw = GetFence(ti->tile, DIAGDIR_SW);
	uint fence_se = GetFence(ti->tile, DIAGDIR_SE);

	if (fence_sw != 0 || fence_se != 0) {
		bounds.offset.x = 0;
		bounds.offset.y = 0;
		bounds.offset.z = GetSlopePixelZInCorner(ti->tileh, CORNER_S);

		if (fence_sw != 0) {
			SpriteID sprite = _clear_land_fence_sprites[fence_sw - 1] + _fence_mod_by_tileh_sw[ti->tileh];
			AddSortableSpriteToDraw(sprite, PAL_NONE, *ti, bounds, false);
		}

		if (fence_se != 0) {
			SpriteID sprite = _clear_land_fence_sprites[fence_se - 1] + _fence_mod_by_tileh_se[ti->tileh];
			AddSortableSpriteToDraw(sprite, PAL_NONE, *ti, bounds, false);

		}
	}
	EndSpriteCombine();
}

/** @copydoc DrawTileProc */
static void DrawTile_Clear(TileInfo *ti, DrawTileProcParams params)
{
	ClearGround real_ground = GetClearGround(ti->tile);
	ClearGround ground = IsSnowTile(ti->tile) ? ClearGround::Snow : real_ground;

	switch (ground) {
		case ClearGround::Grass:
			if (!params.no_ground_tiles) DrawClearLandTile(ti, GetClearDensity(ti->tile));
			break;

		case ClearGround::Rough:
			if (!params.no_ground_tiles) DrawHillyLandTile(ti);
			break;

		case ClearGround::Rocks:
			if (!params.no_ground_tiles) {
				if (GetTropicZone(ti->tile) == TROPICZONE_DESERT) {
					DrawGroundSprite(_clear_land_sprites_snow_desert[GetClearDensity(ti->tile)] + SlopeToSpriteOffset(ti->tileh), PAL_NONE);
					DrawGroundSprite(SPR_OVERLAY_ROCKS_BASE + SlopeToSpriteOffset(ti->tileh), PAL_NONE);
				} else {
					uint8_t slope_to_sprite_offset = SlopeToSpriteOffset(ti->tileh);
					if (DrawCustomSpriteIDForRocks(ti, slope_to_sprite_offset, false)) break;
					DrawGroundSprite(GetSpriteIDForRocksUsingOffset(slope_to_sprite_offset, ti->x, ti->y), PAL_NONE);
				}
			}
			break;

		case ClearGround::Fields:
			if (params.min_visible_height <= (4 * ZOOM_BASE)) {
				DrawGroundSprite(GetSpriteIDForFields(ti->tileh, GetFieldType(ti->tile)), PAL_NONE);
				DrawClearLandFence(ti);
			}
			break;

		case ClearGround::Snow:
			if (!params.no_ground_tiles) {
				uint8_t slope_to_sprite_offset = SlopeToSpriteOffset(ti->tileh);
				if (real_ground == ClearGround::Rocks && !_new_landscape_rocks_grfs.empty()) {
					if (DrawCustomSpriteIDForRocks(ti, slope_to_sprite_offset, true)) break;
				}
				uint8_t density = GetClearDensity(ti->tile);
				DrawGroundSprite(_clear_land_sprites_snow_desert[density] + slope_to_sprite_offset, PAL_NONE);
				if (real_ground == ClearGround::Rocks) {
					/* There 4 levels of snowy overlay rocks, each with 19 sprites. */
					++density;
					DrawGroundSprite(SPR_OVERLAY_ROCKS_BASE + (density * 19) + slope_to_sprite_offset, PAL_NONE);
				}
			}
			break;

		case ClearGround::Desert:
			if (!params.no_ground_tiles) DrawGroundSprite(GetSpriteIDForSnowDesert(ti->tileh, GetClearDensity(ti->tile)), PAL_NONE);
			break;

		default:
			NOT_REACHED();
	}

	if (unlikely(_tree_placer_preview_active) && ground != ClearGround::Fields && ground != ClearGround::Rocks && !IsInvisibilitySet(TO_TREES)) {
		auto it = _tree_placer_memory.find(ti->tile);
		if (it != _tree_placer_memory.end()) {
			extern void DrawClearTileSimulatedTreeTileOverlay(TileInfo *ti, bool secondary_ground, TreeType tree_type, uint8_t count);
			DrawClearTileSimulatedTreeTileOverlay(ti, ground == ClearGround::Snow || ground == ClearGround::Desert, it->second.tree_type, it->second.count);
		}
	}

	DrawBridgeMiddle(ti);
}

/** @copydoc GetSlopePixelZProc */
static int GetSlopePixelZ_Clear(TileIndex tile, uint x, uint y, [[maybe_unused]] bool ground_vehicle)
{
	auto [tileh, z] = GetTilePixelSlope(tile);

	return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
}

static void UpdateFences(TileIndex tile)
{
	assert_tile(IsTileType(tile, TileType::Clear) && IsClearGround(tile, ClearGround::Fields), tile);
	bool dirty = false;

	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		if (GetFence(tile, dir) != 0) continue;
		TileIndex neighbour = tile + TileOffsByDiagDir(dir);
		if (IsTileType(neighbour, TileType::Clear) && IsClearGround(neighbour, ClearGround::Fields)) continue;
		SetFence(tile, dir, 3);
		dirty = true;
	}

	if (dirty) MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
}


/**
 * Convert to or from snowy tiles.
 * @param tile The tile to consider.
 */
static void TileLoopClearAlps(TileIndex tile)
{
	int k;
	int h = (int)TileHeight(tile);
	if (h < GetSnowLine() - 1) {
		/* Fast path to avoid needing to check all 4 corners */
		k = -1;
	} else if (h >= GetSnowLine() + 4) {
		/* Fast path to avoid needing to check all 4 corners */
		k = 3;
	} else {
		k = GetTileZ(tile) - GetSnowLine() + 1;
	}

	if (!IsSnowTile(tile)) {
		/* Below the snow line, do nothing if no snow. */
		/* At or above the snow line, make snow tile if needed. */
		if (k >= 0) {
			/* Snow density is started at 0 so that it can gradually reach the required density. */
			MakeSnow(tile, 0);
			MarkTileDirtyByTile(tile);
		}
		return;
	}

	/* Update snow density. */
	uint current_density = GetClearDensity(tile);
	uint req_density = (k < 0) ? 0u : std::min<uint>(k, 3u);

	if (current_density == req_density) {
		/* Density at the required level. */
		if (k >= 0) return;
		ClearSnow(tile);
	} else {
		AddClearDensity(tile, current_density < req_density ? 1 : -1);
	}

	MarkTileDirtyByTile(tile);
}

/**
 * Tests if at least one surrounding tile is non-desert
 * @param tile tile to check
 * @return does this tile have at least one non-desert tile around?
 */
static inline bool NeighbourIsNormal(TileIndex tile)
{
	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		TileIndex t = tile + TileOffsByDiagDir(dir);
		if (!IsValidTile(t)) continue;
		if (GetTropicZone(t) != TROPICZONE_DESERT) return true;
		if (HasTileWaterClass(t) && GetWaterClass(t) == WaterClass::Sea) return true;
	}
	return false;
}

static void TileLoopClearDesert(TileIndex tile)
{
	ClearGround ground = GetClearGround(tile);

	/* Current desert level - 0 if it is not desert */
	uint current = 0;
	if (ground == ClearGround::Desert || ground == ClearGround::Rocks) current = GetClearDensity(tile);

	/* Expected desert level - 0 if it shouldn't be desert */
	uint expected = 0;
	if (GetTropicZone(tile) == TROPICZONE_DESERT) {
		expected = NeighbourIsNormal(tile) ? 1 : 3;
	}

	if (current == expected) return;

	if (ground == ClearGround::Rocks) {
		SetClearGroundDensity(tile, ClearGround::Rocks, expected);
	} else if (expected == 0) {
		SetClearGroundDensity(tile, ClearGround::Grass, 3);
	} else {
		/* Transition from clear to desert is not smooth (after clearing desert tile) */
		SetClearGroundDensity(tile, ClearGround::Desert, expected);
	}

	MarkTileDirtyByTile(tile);
}

/** @copydoc TileLoopProc */
static void TileLoop_Clear(TileIndex tile)
{
	AmbientSoundEffect(tile);

	switch (_settings_game.game_creation.landscape) {
		case LandscapeType::Tropic: TileLoopClearDesert(tile); break;
		case LandscapeType::Arctic: TileLoopClearAlps(tile);   break;
		default: break;
	}

	if (IsSnowTile(tile)) return;

	switch (GetClearGround(tile)) {
		case ClearGround::Grass:
			if (GetClearDensity(tile) == 3) return;

			if (_game_mode != GM_EDITOR) {
				if (GetClearCounter(tile) < 7) {
					AddClearCounter(tile, 1);
					return;
				} else {
					SetClearCounter(tile, 0);
					AddClearDensity(tile, 1);
				}
			} else {
				SetClearGroundDensity(tile, GB(Random(), 0, 8) > 21 ? ClearGround::Grass : ClearGround::Rough, 3);
			}
			break;

		case ClearGround::Fields:
			UpdateFences(tile);

			if (_game_mode == GM_EDITOR) return;

			if (GetClearCounter(tile) < 7) {
				AddClearCounter(tile, 1);
				return;
			} else {
				SetClearCounter(tile, 0);
			}

			if (GetIndustryIndexOfField(tile) == IndustryID::Invalid() && GetFieldType(tile) >= 7) {
				/* This farmfield is no longer farmfield, so make it grass again */
				MakeClear(tile, ClearGround::Grass, 2);
			} else {
				uint field_type = GetFieldType(tile);
				field_type = (field_type < 8) ? field_type + 1 : 0;
				SetFieldType(tile, field_type);
			}
			break;

		default:
			return;
	}

	MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE_NON_VEG);
}

void GenerateClearTile()
{
	uint i, gi;
	TileIndex tile;

	/* add rough tiles */
	i = Map::ScaleBySize(GB(Random(), 0, 10) + 0x400);
	gi = Map::ScaleBySize(GB(Random(), 0, 7) + 0x80);

	SetGeneratingWorldProgress(GWP_ROUGH_ROCKY, gi + i);
	do {
		IncreaseGeneratingWorldProgress(GWP_ROUGH_ROCKY);
		tile = RandomTile();
		if (IsTileType(tile, TileType::Clear) && !IsClearGround(tile, ClearGround::Desert)) SetClearGroundDensity(tile, ClearGround::Rough, 3);
	} while (--i);

	/* add rocky tiles */
	i = gi;
	do {
		uint32_t r = Random();
		tile = RandomTileSeed(r);

		IncreaseGeneratingWorldProgress(GWP_ROUGH_ROCKY);
		if (IsTileType(tile, TileType::Clear)) {
			uint j = GB(r, 16, 4) + _settings_game.game_creation.amount_of_rocks + ((int)TileHeight(tile) * _settings_game.game_creation.height_affects_rocks);
			for (;;) {
				TileIndex tile_new;

				SetClearGroundDensity(tile, ClearGround::Rocks, 3);
				MarkTileDirtyByTile(tile);
				do {
					if (--j == 0) goto get_out;
					tile_new = tile + TileOffsByDiagDir((DiagDirection)GB(Random(), 0, 2));
				} while (!IsTileType(tile_new, TileType::Clear));
				tile = tile_new;
			}
get_out:;
		}
	} while (--i);
}

/** @copydoc GetTileDescProc */
static void GetTileDesc_Clear(TileIndex tile, TileDesc &td)
{
	/* Each pair holds a normal and a snowy ClearGround description. */
	static constexpr std::pair<StringID, StringID> clear_land_str[to_underlying(ClearGround::MaxSize)] = {
		{STR_LAI_CLEAR_DESCRIPTION_GRASS,      STR_LAI_CLEAR_DESCRIPTION_SNOWY_GRASS}, // Description for grass.
		{STR_LAI_CLEAR_DESCRIPTION_ROUGH_LAND, STR_LAI_CLEAR_DESCRIPTION_SNOWY_ROUGH_LAND}, // Description for rough land.
		{STR_LAI_CLEAR_DESCRIPTION_ROCKS,      STR_LAI_CLEAR_DESCRIPTION_SNOWY_ROCKS}, // Description for rocks.
		{STR_LAI_CLEAR_DESCRIPTION_FIELDS,     STR_EMPTY}, // Description for fields.
		{STR_EMPTY,                            STR_EMPTY}, // unused entry does not appear in the map.
		{STR_LAI_CLEAR_DESCRIPTION_DESERT,     STR_EMPTY}, // Description for desert.
		{STR_EMPTY,                            STR_EMPTY}, // unused entry does not appear in the map.
		{STR_EMPTY,                            STR_EMPTY}, // unused entry does not appear in the map.
	};

	if (!IsSnowTile(tile) && IsClearGround(tile, ClearGround::Grass) && GetClearDensity(tile) == 0) {
		td.str = STR_LAI_CLEAR_DESCRIPTION_BARE_LAND;
	} else {
		const auto &[name, snowy_name] = clear_land_str[to_underlying(GetClearGround(tile))];
		td.str = IsSnowTile(tile) ? snowy_name : name;
	}
	td.owner[0] = GetTileOwner(tile);
}

/** @copydoc TerraformTileProc */
static CommandCost TerraformTile_Clear(TileIndex tile, DoCommandFlags flags, int, Slope)
{
	return Command<Commands::LandscapeClear>::Do(flags, tile);
}

/** TileTypeProcs definitions for TileType::Clear tiles. */
extern const TileTypeProcs _tile_type_clear_procs = {
	.draw_tile_proc = DrawTile_Clear,
	.get_slope_pixel_z_proc = GetSlopePixelZ_Clear,
	.clear_tile_proc = ClearTile_Clear,
	.get_tile_desc_proc = GetTileDesc_Clear,
	.tile_loop_proc = TileLoop_Clear,
	.terraform_tile_proc = TerraformTile_Clear,
};
