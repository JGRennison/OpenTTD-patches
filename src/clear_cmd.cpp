/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
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

#include "table/strings.h"
#include "table/sprites.h"
#include "table/clear_land.h"

#include "safeguards.h"

bool _allow_rocks_desert = false;

static CommandCost ClearTile_Clear(TileIndex tile, DoCommandFlag flags)
{
	static const Price clear_price_table[] = {
		PR_CLEAR_GRASS,
		PR_CLEAR_ROUGH,
		PR_CLEAR_ROCKS,
		PR_CLEAR_FIELDS,
		PR_CLEAR_ROUGH,
		PR_CLEAR_ROUGH,
	};
	CommandCost price(EXPENSES_CONSTRUCTION);

	if (!IsClearGround(tile, CLEAR_GRASS) || GetClearDensity(tile) != 0) {
		price.AddCost(_price[clear_price_table[GetClearGround(tile)]]);
	}

	if (flags & DC_EXEC) DoClearSquare(tile);

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
	return ((HasGrfMiscBit(GMB_SECOND_ROCKY_TILE_SET) && (tile_hash & 1)) ? SPR_FLAT_ROCKY_LAND_2 : SPR_FLAT_ROCKY_LAND_1) + SlopeToSpriteOffset(slope);
}

inline SpriteID GetSpriteIDForRocksUsingOffset(const uint slope_to_sprite_offset, const uint x, const uint y)
{
	return ((HasGrfMiscBit(GMB_SECOND_ROCKY_TILE_SET) && (TileHash(x, y) & 1)) ? SPR_FLAT_ROCKY_LAND_2 : SPR_FLAT_ROCKY_LAND_1) + slope_to_sprite_offset;
}

bool DrawCustomSpriteIDForRocks(const TileInfo *ti, uint8_t slope_to_sprite_offset, bool require_snow_flag)
{
	for (const GRFFile *grf : _new_landscape_rocks_grfs) {
		if (require_snow_flag && !HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_DRAW_SNOWY_ENABLED)) continue;

		NewLandscapeResolverObject object(grf, ti, NEW_LANDSCAPE_ROCKS);

		const SpriteGroup *group = object.Resolve();
		if (group != nullptr && group->GetNumResults() > slope_to_sprite_offset) {
			PaletteID pal = HasBit(grf->new_landscape_ctrl_flags, NLCF_ROCKS_RECOLOUR_ENABLED) ? GB(GetRegister(0x100), 0, 24) : PAL_NONE;
			DrawGroundSprite(group->GetResult() + slope_to_sprite_offset, pal);
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

	int maxz = GetSlopeMaxPixelZ(ti->tileh);

	uint fence_nw = GetFence(ti->tile, DIAGDIR_NW);
	if (fence_nw != 0) {
		int z = GetSlopePixelZInCorner(ti->tileh, CORNER_W);
		SpriteID sprite = _clear_land_fence_sprites[fence_nw - 1] + _fence_mod_by_tileh_nw[ti->tileh];
		AddSortableSpriteToDraw(sprite, PAL_NONE, ti->x, ti->y - 16, 16, 32, maxz - z + 4, ti->z + z, false, 0, 16, -z);
	}

	uint fence_ne = GetFence(ti->tile, DIAGDIR_NE);
	if (fence_ne != 0) {
		int z = GetSlopePixelZInCorner(ti->tileh, CORNER_E);
		SpriteID sprite = _clear_land_fence_sprites[fence_ne - 1] + _fence_mod_by_tileh_ne[ti->tileh];
		AddSortableSpriteToDraw(sprite, PAL_NONE, ti->x - 16, ti->y, 32, 16, maxz - z + 4, ti->z + z, false, 16, 0, -z);
	}

	uint fence_sw = GetFence(ti->tile, DIAGDIR_SW);
	uint fence_se = GetFence(ti->tile, DIAGDIR_SE);

	if (fence_sw != 0 || fence_se != 0) {
		int z = GetSlopePixelZInCorner(ti->tileh, CORNER_S);

		if (fence_sw != 0) {
			SpriteID sprite = _clear_land_fence_sprites[fence_sw - 1] + _fence_mod_by_tileh_sw[ti->tileh];
			AddSortableSpriteToDraw(sprite, PAL_NONE, ti->x, ti->y, 16, 16, maxz - z + 4, ti->z + z, false, 0, 0, -z);
		}

		if (fence_se != 0) {
			SpriteID sprite = _clear_land_fence_sprites[fence_se - 1] + _fence_mod_by_tileh_se[ti->tileh];
			AddSortableSpriteToDraw(sprite, PAL_NONE, ti->x, ti->y, 16, 16, maxz - z + 4, ti->z + z, false, 0, 0, -z);
		}
	}
	EndSpriteCombine();
}

static void DrawTile_Clear(TileInfo *ti, DrawTileProcParams params)
{
	switch (GetClearGround(ti->tile)) {
		case CLEAR_GRASS:
			if (!params.no_ground_tiles) DrawClearLandTile(ti, GetClearDensity(ti->tile));
			break;

		case CLEAR_ROUGH:
			if (!params.no_ground_tiles) DrawHillyLandTile(ti);
			break;

		case CLEAR_ROCKS:
			if (!params.no_ground_tiles) {
				uint8_t slope_to_sprite_offset = SlopeToSpriteOffset(ti->tileh);
				if (DrawCustomSpriteIDForRocks(ti, slope_to_sprite_offset, false)) break;
				DrawGroundSprite(GetSpriteIDForRocksUsingOffset(slope_to_sprite_offset, ti->x, ti->y), PAL_NONE);
			}
			break;

		case CLEAR_FIELDS:
			if (params.min_visible_height <= (4 * ZOOM_BASE)) {
				DrawGroundSprite(GetSpriteIDForFields(ti->tileh, GetFieldType(ti->tile)), PAL_NONE);
				DrawClearLandFence(ti);
			}
			break;

		case CLEAR_SNOW:
			if (!params.no_ground_tiles) {
				uint8_t slope_to_sprite_offset = SlopeToSpriteOffset(ti->tileh);
				if (GetRawClearGround(ti->tile) == CLEAR_ROCKS && !_new_landscape_rocks_grfs.empty()) {
					if (DrawCustomSpriteIDForRocks(ti, slope_to_sprite_offset, true)) break;
				}
				DrawGroundSprite(GetSpriteIDForSnowDesertUsingOffset(slope_to_sprite_offset, GetClearDensity(ti->tile)), PAL_NONE);
			}
			break;

		case CLEAR_DESERT:
			if (!params.no_ground_tiles) DrawGroundSprite(GetSpriteIDForSnowDesert(ti->tileh, GetClearDensity(ti->tile)), PAL_NONE);
			break;
	}

	DrawBridgeMiddle(ti);
}

static int GetSlopePixelZ_Clear(TileIndex tile, uint x, uint y, bool)
{
	auto [tileh, z] = GetTilePixelSlope(tile);

	return z + GetPartialPixelZ(x & 0xF, y & 0xF, tileh);
}

static Foundation GetFoundation_Clear(TileIndex, Slope)
{
	return FOUNDATION_NONE;
}

static void UpdateFences(TileIndex tile)
{
	assert_tile(IsTileType(tile, MP_CLEAR) && IsClearGround(tile, CLEAR_FIELDS), tile);
	bool dirty = false;

	for (DiagDirection dir = DIAGDIR_BEGIN; dir < DIAGDIR_END; dir++) {
		if (GetFence(tile, dir) != 0) continue;
		TileIndex neighbour = tile + TileOffsByDiagDir(dir);
		if (IsTileType(neighbour, MP_CLEAR) && IsClearGround(neighbour, CLEAR_FIELDS)) continue;
		SetFence(tile, dir, 3);
		dirty = true;
	}

	if (dirty) MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
}


/** Convert to or from snowy tiles. */
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
			MakeSnow(tile);
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
		if (HasTileWaterClass(t) && GetWaterClass(t) == WATER_CLASS_SEA) return true;
	}
	return false;
}

static void TileLoopClearDesert(TileIndex tile)
{
	/* Current desert level - 0 if it is not desert */
	uint current = 0;
	if (IsClearGround(tile, CLEAR_DESERT)) current = GetClearDensity(tile);

	/* Expected desert level - 0 if it shouldn't be desert */
	uint expected = 0;
	if (GetTropicZone(tile) == TROPICZONE_DESERT) {
		expected = NeighbourIsNormal(tile) ? 1 : 3;
	}

	if (current == expected) return;

	if (_allow_rocks_desert && IsClearGround(tile, CLEAR_ROCKS)) return;

	if (expected == 0) {
		SetClearGroundDensity(tile, CLEAR_GRASS, 3);
	} else {
		/* Transition from clear to desert is not smooth (after clearing desert tile) */
		SetClearGroundDensity(tile, CLEAR_DESERT, expected);
	}

	MarkTileDirtyByTile(tile);
}

static void TileLoop_Clear(TileIndex tile)
{
	AmbientSoundEffect(tile);

	switch (_settings_game.game_creation.landscape) {
		case LT_TROPIC: TileLoopClearDesert(tile); break;
		case LT_ARCTIC: TileLoopClearAlps(tile);   break;
	}

	switch (GetClearGround(tile)) {
		case CLEAR_GRASS:
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
				SetClearGroundDensity(tile, GB(Random(), 0, 8) > 21 ? CLEAR_GRASS : CLEAR_ROUGH, 3);
			}
			break;

		case CLEAR_FIELDS:
			UpdateFences(tile);

			if (_game_mode == GM_EDITOR) return;

			if (GetClearCounter(tile) < 7) {
				AddClearCounter(tile, 1);
				return;
			} else {
				SetClearCounter(tile, 0);
			}

			if (GetIndustryIndexOfField(tile) == INVALID_INDUSTRY && GetFieldType(tile) >= 7) {
				/* This farmfield is no longer farmfield, so make it grass again */
				MakeClear(tile, CLEAR_GRASS, 2);
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
	i = ScaleByMapSize(GB(Random(), 0, 10) + 0x400);
	gi = ScaleByMapSize(GB(Random(), 0, 7) + 0x80);

	SetGeneratingWorldProgress(GWP_ROUGH_ROCKY, gi + i);
	do {
		IncreaseGeneratingWorldProgress(GWP_ROUGH_ROCKY);
		tile = RandomTile();
		if (IsTileType(tile, MP_CLEAR) && !IsClearGround(tile, CLEAR_DESERT)) SetClearGroundDensity(tile, CLEAR_ROUGH, 3);
	} while (--i);

	/* add rocky tiles */
	i = gi;
	do {
		uint32_t r = Random();
		tile = RandomTileSeed(r);

		IncreaseGeneratingWorldProgress(GWP_ROUGH_ROCKY);
		auto IsUsableTile = [&](TileIndex t) -> bool {
			return IsTileType(t, MP_CLEAR) && (_allow_rocks_desert || !IsClearGround(t, CLEAR_DESERT));
		};
		if (IsUsableTile(tile)) {
			uint j = GB(r, 16, 4) + _settings_game.game_creation.amount_of_rocks + ((int)TileHeight(tile) * _settings_game.game_creation.height_affects_rocks);
			for (;;) {
				TileIndex tile_new;

				SetClearGroundDensity(tile, CLEAR_ROCKS, 3);
				MarkTileDirtyByTile(tile);
				do {
					if (--j == 0) goto get_out;
					tile_new = tile + TileOffsByDiagDir((DiagDirection)GB(Random(), 0, 2));
				} while (!IsUsableTile(tile_new));
				tile = tile_new;
			}
get_out:;
		}
	} while (--i);
}

static TrackStatus GetTileTrackStatus_Clear(TileIndex, TransportType, uint, DiagDirection)
{
	return 0;
}

static const StringID _clear_land_str[] = {
	STR_LAI_CLEAR_DESCRIPTION_GRASS,
	STR_LAI_CLEAR_DESCRIPTION_ROUGH_LAND,
	STR_LAI_CLEAR_DESCRIPTION_ROCKS,
	STR_LAI_CLEAR_DESCRIPTION_FIELDS,
	STR_LAI_CLEAR_DESCRIPTION_SNOW_COVERED_LAND,
	STR_LAI_CLEAR_DESCRIPTION_DESERT
};

static void GetTileDesc_Clear(TileIndex tile, TileDesc *td)
{
	if (IsClearGround(tile, CLEAR_GRASS) && GetClearDensity(tile) == 0) {
		td->str = STR_LAI_CLEAR_DESCRIPTION_BARE_LAND;
	} else {
		td->str = _clear_land_str[GetClearGround(tile)];
	}
	td->owner[0] = GetTileOwner(tile);
}

static void ChangeTileOwner_Clear(TileIndex, Owner, Owner)
{
	return;
}

static CommandCost TerraformTile_Clear(TileIndex tile, DoCommandFlag flags, int, Slope)
{
	return Command<CMD_LANDSCAPE_CLEAR>::Do(flags, tile);
}

extern const TileTypeProcs _tile_type_clear_procs = {
	DrawTile_Clear,           ///< draw_tile_proc
	GetSlopePixelZ_Clear,     ///< get_slope_z_proc
	ClearTile_Clear,          ///< clear_tile_proc
	nullptr,                     ///< add_accepted_cargo_proc
	GetTileDesc_Clear,        ///< get_tile_desc_proc
	GetTileTrackStatus_Clear, ///< get_tile_track_status_proc
	nullptr,                     ///< click_tile_proc
	nullptr,                     ///< animate_tile_proc
	TileLoop_Clear,           ///< tile_loop_proc
	ChangeTileOwner_Clear,    ///< change_tile_owner_proc
	nullptr,                     ///< add_produced_cargo_proc
	nullptr,                     ///< vehicle_enter_tile_proc
	GetFoundation_Clear,      ///< get_foundation_proc
	TerraformTile_Clear,      ///< terraform_tile_proc
};
