/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file gfxinit.cpp Initializing of the (GRF) graphics. */

#include "stdafx.h"
#include "fios.h"
#include "newgrf.h"
#include "3rdparty/md5/md5.h"
#include "fontcache.h"
#include "gfx_func.h"
#include "transparency.h"
#include "blitter/factory.hpp"
#include "video/video_driver.hpp"
#include "window_func.h"
#include "zoom_func.h"
#include "clear_map.h"
#include "clear_func.h"
#include "tree_map.h"
#include "scope.h"
#include "debug.h"
#include "table/tree_land.h"
#include "blitter/32bpp_base.hpp"

/* The type of set we're replacing */
#define SET_TYPE "graphics"
#include "base_media_func.h"

#include "table/sprites.h"

#include "safeguards.h"

#include "table/landscape_sprite.h"

/** Offsets for loading the different "replacement" sprites in the files. */
static constexpr std::span<const std::pair<SpriteID, SpriteID>> _landscape_spriteindexes[] = {
	_landscape_spriteindexes_arctic,
	_landscape_spriteindexes_tropic,
	_landscape_spriteindexes_toyland,
};

/**
 * Load an old fashioned GRF file.
 * @param filename   The name of the file to open.
 * @param load_index The offset of the first sprite.
 * @param needs_palette_remap Whether the colours in the GRF file need a palette remap.
 */
static SpriteFile &LoadGrfFile(const std::string &filename, SpriteID load_index, bool needs_palette_remap)
{
	SpriteID sprite_id = 0;

	SpriteFile &file = OpenCachedSpriteFile(filename, BASESET_DIR, needs_palette_remap);

	Debug(sprite, 2, "Reading grf-file '{}'", filename);

	uint8_t container_ver = file.GetContainerVersion();
	if (container_ver == 0) UserError("Base grf '{}' is corrupt", filename);
	ReadGRFSpriteOffsets(file);
	if (container_ver >= 2) {
		/* Read compression. */
		uint8_t compression = file.ReadByte();
		if (compression != 0) UserError("Unsupported compression format");
	}

	while (LoadNextSprite(load_index, file, sprite_id)) {
		load_index++;
		sprite_id++;
		if (load_index >= MAX_SPRITES) {
			UserError("Too many sprites. Recompile with higher MAX_SPRITES value or remove some custom GRF files.");
		}
	}
	Debug(sprite, 2, "Currently {} sprites are loaded", load_index);

	return file;
}

/**
 * Load an old fashioned GRF file to replace already loaded sprites.
 * @param filename   The name of the file to open.
 * @param index_tbl  The offsets of each of the sprites.
 * @param needs_palette_remap Whether the colours in the GRF file need a palette remap.
 * @return The number of loaded sprites.
 */
static void LoadGrfFileIndexed(const std::string &filename, std::span<const std::pair<SpriteID, SpriteID>> index_tbl, bool needs_palette_remap)
{
	uint sprite_id = 0;

	SpriteFile &file = OpenCachedSpriteFile(filename, BASESET_DIR, needs_palette_remap);

	Debug(sprite, 2, "Reading indexed grf-file '{}'", filename);

	uint8_t container_ver = file.GetContainerVersion();
	if (container_ver == 0) UserError("Base grf '{}' is corrupt", filename);
	ReadGRFSpriteOffsets(file);
	if (container_ver >= 2) {
		/* Read compression. */
		uint8_t compression = file.ReadByte();
		if (compression != 0) UserError("Unsupported compression format");
	}

	for (const auto &pair : index_tbl) {
		for (SpriteID load_index = pair.first; load_index <= pair.second; ++load_index) {
			[[maybe_unused]] bool b = LoadNextSprite(load_index, file, sprite_id);
			assert(b);
			sprite_id++;
		}
	}
}

/**
 * Checks whether the MD5 checksums of the files are correct.
 *
 * @note Also checks sample.cat and other required non-NewGRF GRFs for corruption.
 */
void CheckExternalFiles()
{
	if (BaseGraphics::GetUsedSet() == nullptr || BaseSounds::GetUsedSet() == nullptr) return;

	const GraphicsSet *used_set = BaseGraphics::GetUsedSet();

	Debug(grf, 1, "Using the {} base graphics set", used_set->name);

	std::string error_msg;
	auto output_iterator = std::back_inserter(error_msg);
	if (used_set->GetNumInvalid() != 0) {
		/* Not all files were loaded successfully, see which ones */
		fmt::format_to(output_iterator, "Trying to load graphics set '{}', but it is incomplete. The game will probably not run correctly until you properly install this set or select another one. See section 4.1 of README.md.\n\nThe following files are corrupted or missing:\n", used_set->name);
		for (const auto &file : used_set->files) {
			MD5File::ChecksumResult res = GraphicsSet::CheckMD5(&file, BASESET_DIR);
			if (res != MD5File::CR_MATCH) fmt::format_to(output_iterator, "\t{} is {} ({})\n", file.filename, res == MD5File::CR_MISMATCH ? "corrupt" : "missing", file.missing_warning);
		}
		fmt::format_to(output_iterator, "\n");
	}

	const SoundsSet *sounds_set = BaseSounds::GetUsedSet();
	if (sounds_set->GetNumInvalid() != 0) {
		fmt::format_to(output_iterator, "Trying to load sound set '{}', but it is incomplete. The game will probably not run correctly until you properly install this set or select another one. See section 4.1 of README.md.\n\nThe following files are corrupted or missing:\n", sounds_set->name);

		static_assert(SoundsSet::NUM_FILES == 1);
		/* No need to loop each file, as long as there is only a single
		 * sound file. */
		fmt::format_to(output_iterator, "\t{} is {} ({})\n", sounds_set->files->filename, SoundsSet::CheckMD5(sounds_set->files, BASESET_DIR) == MD5File::CR_MISMATCH ? "corrupt" : "missing", sounds_set->files->missing_warning);
	}

	if (!error_msg.empty()) ShowInfoI(error_msg);
}

void InitGRFGlobalVars()
{
	extern void ClearExtraStationNames();
	ClearExtraStationNames();

	extern bool _allow_rocks_desert;
	_allow_rocks_desert = false;
}

/**
 * Get GRFConfig for the default extra graphics.
 * @return Managed pointer to default extra GRFConfig.
 */
static std::unique_ptr<GRFConfig> GetDefaultExtraGRFConfig()
{
	auto gc = std::make_unique<GRFConfig>("OPENTTD.GRF");
	gc->palette |= GRFP_GRF_DOS;
	FillGRFDetails(*gc, false, BASESET_DIR);
	gc->flags.Reset(GRFConfigFlag::InitOnly);
	return gc;
}

/**
 * Get GRFConfig for the baseset extra graphics.
 * @return Managed pointer to baseset extra GRFConfig.
 */
static std::unique_ptr<GRFConfig> GetBasesetExtraGRFConfig()
{
	auto gc = std::make_unique<GRFConfig>(BaseGraphics::GetUsedSet()->GetOrCreateExtraConfig());
	if (gc->param.empty()) gc->SetParameterDefaults();
	gc->flags.Reset(GRFConfigFlag::InitOnly);
	return gc;
}

/** Actually load the sprite tables. */
static void LoadSpriteTables()
{
	const GraphicsSet *used_set = BaseGraphics::GetUsedSet();

	SpriteFile &baseset_file = LoadGrfFile(used_set->files[GFT_BASE].filename, 0, PAL_DOS != used_set->palette);
	if (used_set->name.starts_with("original_")) {
		baseset_file.flags |= SFF_OPENTTDGRF;
	}

	/* Progsignal sprites. */
	SpriteFile &progsig_file = LoadGrfFile("progsignals.grf", SPR_PROGSIGNAL_BASE, false);
	progsig_file.flags |= SFF_PROGSIG;

	/* Fill duplicate programmable pre-signal graphics sprite block */
	for (uint i = 0; i < PROGSIGNAL_SPRITE_COUNT; i++) {
		DupSprite(SPR_PROGSIGNAL_BASE + i, SPR_DUP_PROGSIGNAL_BASE + i);
	}

	/* Extra signal sprites. */
	SpriteFile &extrasig_file = LoadGrfFile("extra_signals.grf", SPR_EXTRASIGNAL_BASE, false);
	extrasig_file.flags |= SFF_PROGSIG;

	/* Fill duplicate extra signal graphics sprite block */
	for (uint i = 0; i < EXTRASIGNAL_SPRITE_COUNT; i++) {
		DupSprite(SPR_EXTRASIGNAL_BASE + i, SPR_DUP_EXTRASIGNAL_BASE + i);
	}

	/* Tracerestrict sprites. */
	LoadGrfFile("tracerestrict.grf", SPR_TRACERESTRICT_BASE, false);

	/* Misc GUI sprites. */
	LoadGrfFile("misc_gui.grf", SPR_MISC_GUI_BASE, false);

	/* Fill duplicate original signal graphics sprite block */
	for (uint i = 0; i < DUP_ORIGINAL_SIGNALS_SPRITE_COUNT; i++) {
		DupSprite(SPR_ORIGINAL_SIGNALS_BASE + i, SPR_DUP_ORIGINAL_SIGNALS_BASE + i);
	}

	/*
	 * The second basic file always starts at the given location and does
	 * contain a different amount of sprites depending on the "type"; DOS
	 * has a few sprites less. However, we do not care about those missing
	 * sprites as they are not shown anyway (logos in intro game).
	 */
	LoadGrfFile(used_set->files[GFT_LOGOS].filename, 4793, PAL_DOS != used_set->palette);

	/*
	 * Load additional sprites for climates other than temperate.
	 * This overwrites some of the temperate sprites, such as foundations
	 * and the ground sprites.
	 */
	if (_settings_game.game_creation.landscape != LandscapeType::Temperate) {
		LoadGrfFileIndexed(
			used_set->files[GFT_ARCTIC + to_underlying(_settings_game.game_creation.landscape) - 1].filename,
			_landscape_spriteindexes[to_underlying(_settings_game.game_creation.landscape) - 1],
			PAL_DOS != used_set->palette
		);
	}

	LoadGrfFile("innerhighlight.grf", SPR_ZONING_INNER_HIGHLIGHT_BASE, false);

	/* Load route step graphics */
	LoadGrfFile("route_step.grf", SPR_ROUTE_STEP_BASE, false);

	/* Initialize the unicode to sprite mapping table */
	InitializeUnicodeGlyphMap();

	InitGRFGlobalVars();

	/*
	 * Load the base and extra NewGRF with OTTD required graphics as first NewGRF.
	 * However, we do not want it to show up in the list of used NewGRFs,
	 * so we have to manually add it, and then remove it later.
	 */

	auto default_extra = GetDefaultExtraGRFConfig();
	auto baseset_extra = GetBasesetExtraGRFConfig();
	std::string default_filename = default_extra->filename;

	_grfconfig.insert(std::begin(_grfconfig), std::move(default_extra));
	_grfconfig.insert(std::next(std::begin(_grfconfig)), std::move(baseset_extra));

	LoadNewGRF(SPR_NEWGRFS_BASE, 2);

	uint total_extra_graphics = SPR_NEWGRFS_BASE - SPR_OPENTTD_BASE;
	Debug(sprite, 4, "Checking sprites from fallback grf");
	_missing_extra_graphics = GetSpriteCountForFile(default_filename, SPR_OPENTTD_BASE, SPR_NEWGRFS_BASE);
	Debug(sprite, 1, "{} extra sprites, {} from baseset, {} from fallback", total_extra_graphics, total_extra_graphics - _missing_extra_graphics, _missing_extra_graphics);

	/* The original baseset extra graphics intentionally make use of the fallback graphics.
	 * Let's say everything which provides less than 500 sprites misses the rest intentionally. */
	if (500 + _missing_extra_graphics > total_extra_graphics) _missing_extra_graphics = 0;

	/* Remove the default and baseset extra graphics from the config. */
	_grfconfig.erase(std::begin(_grfconfig), std::next(std::begin(_grfconfig), 2));
}


static void RealChangeBlitter(const std::string_view repl_blitter)
{
	const std::string_view cur_blitter = BlitterFactory::GetCurrentBlitter()->GetName();
	if (cur_blitter == repl_blitter) return;

	Debug(driver, 1, "Switching blitter from '{}' to '{}'... ", cur_blitter, repl_blitter);
	Blitter *new_blitter = BlitterFactory::SelectBlitter(repl_blitter);
	if (new_blitter == nullptr) NOT_REACHED();
	Debug(driver, 1, "Successfully switched to {}.", repl_blitter);

	if (!VideoDriver::GetInstance()->AfterBlitterChange()) {
		/* Failed to switch blitter, let's hope we can return to the old one. */
		if (BlitterFactory::SelectBlitter(cur_blitter) == nullptr || !VideoDriver::GetInstance()->AfterBlitterChange()) UserError("Failed to reinitialize video driver. Specify a fixed blitter in the config");
	}

	/* Clear caches that might have sprites for another blitter. */
	VideoDriver::GetInstance()->ClearSystemSprites();
	ClearFontCache();
	GfxClearSpriteCache();
	ReInitAllWindows(false);
}

/**
 * Check blitter needed by NewGRF config and switch if needed.
 * @return False when nothing changed, true otherwise.
 */
static bool SwitchNewGRFBlitter()
{
	/* Never switch if the blitter was specified by the user. */
	if (!_blitter_autodetected) return false;

	/* Null driver => dedicated server => do nothing. */
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 0) return false;

	/* Get preferred depth.
	 *  - depth_wanted_by_base: Depth required by the baseset, i.e. the majority of the sprites.
	 *  - depth_wanted_by_grf:  Depth required by some NewGRF.
	 * Both can force using a 32bpp blitter. depth_wanted_by_base is used to select
	 * between multiple 32bpp blitters, which perform differently with 8bpp sprites.
	 */
	uint depth_wanted_by_base = BaseGraphics::GetUsedSet()->blitter == BLT_32BPP ? 32 : 8;
	uint depth_wanted_by_grf = _support8bpp != S8BPP_NONE ? 8 : 32;
	for (const auto &c : _grfconfig) {
		if (c->status == GCS_DISABLED || c->status == GCS_NOT_FOUND || c->flags.Test(GRFConfigFlag::InitOnly)) continue;
		if (c->palette & GRFP_BLT_32BPP) depth_wanted_by_grf = 32;
	}
	/* We need a 32bpp blitter for font anti-alias. */
	if (GetFontAAState()) depth_wanted_by_grf = 32;

	/* Search the best blitter. */
	static const struct {
		const std::string_view name;
		uint animation; ///< 0: no support, 1: do support, 2: both
		uint min_base_depth, max_base_depth, min_grf_depth, max_grf_depth;
	} replacement_blitters[] = {
		{ "8bpp-optimized",  2,  8,  8,  8,  8 },
		{ "40bpp-anim",      2,  8, 32,  8, 32 },
#ifdef WITH_SSE
		{ "32bpp-sse4",      0, 32, 32,  8, 32 },
		{ "32bpp-ssse3",     0, 32, 32,  8, 32 },
		{ "32bpp-sse2",      0, 32, 32,  8, 32 },
		{ "32bpp-sse4-anim", 1, 32, 32,  8, 32 },
#endif
		{ "32bpp-optimized", 0,  8, 32,  8, 32 },
#ifdef WITH_SSE
		{ "32bpp-sse2-anim", 1,  8, 32,  8, 32 },
#endif
		{ "32bpp-anim",      1,  8, 32,  8, 32 },
	};

	const bool animation_wanted = HasBit(_display_opt, DO_FULL_ANIMATION);
	const std::string_view cur_blitter = BlitterFactory::GetCurrentBlitter()->GetName();

	for (const auto &replacement_blitter : replacement_blitters) {
		if (animation_wanted && (replacement_blitter.animation == 0)) continue;
		if (!animation_wanted && (replacement_blitter.animation == 1)) continue;

		if (!IsInsideMM(depth_wanted_by_base, replacement_blitter.min_base_depth, replacement_blitter.max_base_depth + 1)) continue;
		if (!IsInsideMM(depth_wanted_by_grf, replacement_blitter.min_grf_depth, replacement_blitter.max_grf_depth + 1)) continue;

		if (replacement_blitter.name == cur_blitter) {
			return false;
		}
		if (BlitterFactory::GetBlitterFactory(replacement_blitter.name) == nullptr) continue;

		/* Inform the video driver we want to switch blitter as soon as possible. */
		VideoDriver::GetInstance()->QueueOnMainThread(std::bind(&RealChangeBlitter, replacement_blitter.name));
		break;
	}

	return true;
}

/** Check whether we still use the right blitter, or use another (better) one. */
void CheckBlitter()
{
	if (!SwitchNewGRFBlitter()) return;

	ClearFontCache();
	GfxClearSpriteCache();
	ReInitAllWindows(false);
}

void UpdateRouteStepSpriteSize()
{
	extern uint _vp_route_step_sprite_width;
	extern uint _vp_route_step_base_width;
	extern uint _vp_route_step_height_top;
	extern uint _vp_route_step_height_bottom;
	extern uint _vp_route_step_string_width[4];

	Dimension d0 = GetSpriteSize(SPR_ROUTE_STEP_TOP);
	_vp_route_step_sprite_width = d0.width;
	_vp_route_step_height_top = d0.height;

	_vp_route_step_base_width = (_vp_route_step_height_top + 1) * 2;

	Dimension d2 = GetSpriteSize(SPR_ROUTE_STEP_BOTTOM);
	_vp_route_step_height_bottom = d2.height;

	const uint min_width = _vp_route_step_sprite_width > _vp_route_step_base_width ? _vp_route_step_sprite_width - _vp_route_step_base_width : 0;
	uint extra = 0;
	for (uint i = 0; i < 4; i++) {
		SetDParamMaxDigits(0, i + 2, FS_SMALL);
		SetDParam(1, STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_STATION);
		const uint base_width = GetStringBoundingBox(STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP, FS_SMALL).width;
		if (i == 0) {
			uint width = base_width;
			auto process_string = [&](StringID str) {
				SetDParam(1, str);
				width = std::max(width, GetStringBoundingBox(STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP, FS_SMALL).width);
			};
			process_string(STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_DEPOT);
			process_string(STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_WAYPOINT);
			process_string(STR_VIEWPORT_SHOW_VEHICLE_ROUTE_STEP_IMPLICIT);
			extra = width - base_width;
		}
		_vp_route_step_string_width[i] = std::max(min_width, base_width + extra);
	}
}

#if !defined(DEDICATED)
/* multi can be density, field type, ... */
static SpriteID GetSpriteIDForClearGround(const ClearGround cg, const Slope slope, const uint multi)
{
	switch (cg) {
		case CLEAR_GRASS:
			return GetSpriteIDForClearLand(slope, (uint8_t)multi);
		case CLEAR_ROUGH:
			return GetSpriteIDForHillyLand(slope, multi);
		case CLEAR_ROCKS:
			return GetSpriteIDForRocks(slope, multi);
		case CLEAR_FIELDS:
			return GetSpriteIDForFields(slope, multi);
		case CLEAR_SNOW:
		case CLEAR_DESERT:
			return GetSpriteIDForSnowDesert(slope, multi);
		default: NOT_REACHED();
	}
}
#endif /* !DEDICATED */

/** Once the sprites are loaded, we can determine main colours of ground/water/... */
void GfxDetermineMainColours()
{
#if !defined(DEDICATED)
	/* Water. */
	extern uint32_t _vp_map_water_colour[5];
	_vp_map_water_colour[0] = GetSpriteMainColour(SPR_FLAT_WATER_TILE, PAL_NONE);
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 32) {
		_vp_map_water_colour[1] = Blitter_32bppBase::MakeTransparent(_vp_map_water_colour[0], 256, 192).data; // lighter
		_vp_map_water_colour[2] = Blitter_32bppBase::MakeTransparent(_vp_map_water_colour[0], 192, 256).data; // darker
		_vp_map_water_colour[3] = _vp_map_water_colour[2];
		_vp_map_water_colour[4] = _vp_map_water_colour[1];
	}

	/* Clear ground. */
	extern uint32_t _vp_map_vegetation_clear_colours[16][6][8];
	memset(_vp_map_vegetation_clear_colours, 0, sizeof(_vp_map_vegetation_clear_colours));
	const struct {
		uint8_t min;
		uint8_t max;
	} multi[6] = {
		{ 0, 3 }, // CLEAR_GRASS, density
		{ 0, 7 }, // CLEAR_ROUGH, "random" based on position
		{ 0, 1 }, // CLEAR_ROCKS, tile hash parity
		{ 0, 7 }, // CLEAR_FIELDS, some field types
		{ 0, 3 }, // CLEAR_SNOW, density
		{ 1, 3 }, // CLEAR_DESERT, density
	};
	for (uint s = 0; s <= SLOPE_ELEVATED; s++) {
		for (uint cg = 0; cg < 6; cg++) {
			for (uint m = multi[cg].min; m <= multi[cg].max; m++) {
				_vp_map_vegetation_clear_colours[s][cg][m] = GetSpriteMainColour(GetSpriteIDForClearGround((ClearGround) cg, (Slope) s, m), PAL_NONE);
			}
		}
	}

	/* Trees. */
	extern uint32_t _vp_map_vegetation_tree_colours[16][5][MAX_TREE_COUNT_BY_LANDSCAPE];
	const uint base  = _tree_base_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
	const uint count = _tree_count_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
	for (uint tg = 0; tg < 5; tg++) {
		for (uint i = base; i < base + count; i++) {
			_vp_map_vegetation_tree_colours[0][tg][i - base] = GetSpriteMainColour(_tree_sprites[i].sprite, _tree_sprites[i].pal);
		}
		const int diff = MAX_TREE_COUNT_BY_LANDSCAPE - count;
		if (diff > 0) {
			for (uint i = count; i < MAX_TREE_COUNT_BY_LANDSCAPE; i++)
				_vp_map_vegetation_tree_colours[0][tg][i] = _vp_map_vegetation_tree_colours[0][tg][i - count];
		}
	}
	for (int s = 1; s <= SLOPE_ELEVATED; ++s) {
		extern int GetSlopeTreeBrightnessAdjust(Slope slope);
		int brightness_adjust = (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 32) ? GetSlopeTreeBrightnessAdjust((Slope)s) * 2 : 0;
		if (brightness_adjust != 0) {
			for (uint tg = 0; tg < 5; tg++) {
				for (uint i = 0; i < MAX_TREE_COUNT_BY_LANDSCAPE; i++) {
					_vp_map_vegetation_tree_colours[s][tg][i] = AdjustBrightness(Colour(_vp_map_vegetation_tree_colours[0][tg][i]), DEFAULT_BRIGHTNESS + brightness_adjust).data;
				}
			}
		} else {
			memcpy(&(_vp_map_vegetation_tree_colours[s]), &(_vp_map_vegetation_tree_colours[0]), sizeof(_vp_map_vegetation_tree_colours[0]));
		}
	}
#endif /* !DEDICATED */
}

/** Initialise and load all the sprites. */
void GfxLoadSprites()
{
	Debug(sprite, 2, "Loading sprite set {}", _settings_game.game_creation.landscape);

	_grf_bug_too_many_strings = false;

	SwitchNewGRFBlitter();
	VideoDriver::GetInstance()->ClearSystemSprites();
	ClearFontCache();
	GfxInitSpriteMem();
	GfxInitPalettes();
	LoadSpriteTables();
	GfxClearSpriteCacheLoadIndex();
	GfxDetermineMainColours();

	UpdateRouteStepSpriteSize();
	UpdateCursorSize();

	Debug(sprite, 2, "Completed loading sprite set {}", _settings_game.game_creation.landscape);
}

GraphicsSet::GraphicsSet()
	: BaseSet<GraphicsSet, MAX_GFT, true>{}, palette{}, blitter{}
{
	// instantiate here, because unique_ptr needs a complete type
}

GraphicsSet::~GraphicsSet()
{
	// instantiate here, because unique_ptr needs a complete type
}

bool GraphicsSet::FillSetDetails(const IniFile &ini, const std::string &path, const std::string &full_filename)
{
	bool ret = this->BaseSet<GraphicsSet, MAX_GFT, true>::FillSetDetails(ini, path, full_filename, false);
	if (ret) {
		const IniGroup *metadata = ini.GetGroup("metadata");
		assert(metadata != nullptr); /* ret can't be true if metadata isn't present. */
		const IniItem *item;

		fetch_metadata("palette");
		this->palette = ((*item->value)[0] == 'D' || (*item->value)[0] == 'd') ? PAL_DOS : PAL_WINDOWS;

		/* Get optional blitter information. */
		item = metadata->GetItem("blitter");
		this->blitter = (item != nullptr && (*item->value)[0] == '3') ? BLT_32BPP : BLT_8BPP;
	}
	return ret;
}

/**
 * Return configuration for the extra GRF, or lazily create it.
 * @return NewGRF configuration
 */
GRFConfig &GraphicsSet::GetOrCreateExtraConfig() const
{
	if (!this->extra_cfg) {
		this->extra_cfg = std::make_unique<GRFConfig>(this->files[GFT_EXTRA].filename);

		/* We know the palette of the base set, so if the base NewGRF is not
		 * setting one, use the palette of the base set and not the global
		 * one which might be the wrong palette for this base NewGRF.
		 * The value set here might be overridden via action14 later. */
		switch (this->palette) {
			case PAL_DOS:     this->extra_cfg->palette |= GRFP_GRF_DOS;     break;
			case PAL_WINDOWS: this->extra_cfg->palette |= GRFP_GRF_WINDOWS; break;
			default: break;
		}
		FillGRFDetails(*this->extra_cfg, false, BASESET_DIR);
	}
	return *this->extra_cfg;
}

bool GraphicsSet::IsConfigurable() const
{
	const GRFConfig &cfg = this->GetOrCreateExtraConfig();
	/* This check is more strict than the one for NewGRF Settings.
	 * There are no legacy basesets with parameters, but without Action14 */
	return !cfg.param_info.empty();
}

void GraphicsSet::CopyCompatibleConfig(const GraphicsSet &src)
{
	const GRFConfig *src_cfg = src.GetExtraConfig();
	if (src_cfg == nullptr || src_cfg->param.empty()) return;
	GRFConfig &dest_cfg = this->GetOrCreateExtraConfig();
	if (dest_cfg.IsCompatible(src_cfg->version)) return;
	dest_cfg.CopyParams(*src_cfg);
}

/**
 * Calculate and check the MD5 hash of the supplied GRF.
 * @param file The file get the hash of.
 * @param subdir The sub directory to get the files from.
 * @return
 * - #CR_MATCH if the MD5 hash matches
 * - #CR_MISMATCH if the MD5 does not match
 * - #CR_NO_FILE if the file misses
 */
/* static */ MD5File::ChecksumResult GraphicsSet::CheckMD5(const MD5File *file, Subdirectory subdir)
{
	size_t size = 0;
	auto f = FioFOpenFile(file->filename, "rb", subdir, &size);
	if (!f.has_value()) return MD5File::CR_NO_FILE;

	size_t max = GRFGetSizeOfDataSection(*f);

	return file->CheckMD5(subdir, max);
}


/**
 * Calculate and check the MD5 hash of the supplied filename.
 * @param subdir The sub directory to get the files from
 * @param max_size Only calculate the hash for this many bytes from the file start.
 * @return
 * - #CR_MATCH if the MD5 hash matches
 * - #CR_MISMATCH if the MD5 does not match
 * - #CR_NO_FILE if the file misses
 */
MD5File::ChecksumResult MD5File::CheckMD5(Subdirectory subdir, size_t max_size) const
{
	size_t size;
	auto f = FioFOpenFile(this->filename, "rb", subdir, &size);
	if (!f.has_value()) return CR_NO_FILE;

	size = std::min(size, max_size);

	Md5 checksum;
	uint8_t buffer[1024];
	MD5Hash digest;
	size_t len;

	while ((len = fread(buffer, 1, (size > sizeof(buffer)) ? sizeof(buffer) : size, *f)) != 0 && size != 0) {
		size -= len;
		checksum.Append(buffer, len);
	}

	checksum.Finish(digest);
	return this->hash == digest ? CR_MATCH : CR_MISMATCH;
}

/** Names corresponding to the GraphicsFileType */
static const char * const _graphics_file_names[] = { "base", "logos", "arctic", "tropical", "toyland", "extra" };

/** Implementation */
template <class T, size_t Tnum_files, bool Tsearch_in_tars>
/* static */ const char * const *BaseSet<T, Tnum_files, Tsearch_in_tars>::file_names = _graphics_file_names;

template <class Tbase_set>
/* static */ bool BaseMedia<Tbase_set>::DetermineBestSet()
{
	if (BaseMedia<Tbase_set>::used_set != nullptr) return true;

	const Tbase_set *best = nullptr;
	for (const Tbase_set *c = BaseMedia<Tbase_set>::available_sets; c != nullptr; c = c->next) {
		/* Skip unusable sets */
		if (c->GetNumMissing() != 0) continue;

		if (best == nullptr ||
				(best->fallback && !c->fallback) ||
				best->valid_files < c->valid_files ||
				(best->valid_files == c->valid_files && (
					(best->shortname == c->shortname && best->version < c->version) ||
					(best->palette != PAL_DOS && c->palette == PAL_DOS)))) {
			best = c;
		}
	}

	BaseMedia<Tbase_set>::used_set = best;
	return BaseMedia<Tbase_set>::used_set != nullptr;
}

template <class Tbase_set>
/* static */ const char *BaseMedia<Tbase_set>::GetExtension()
{
	return ".obg"; // OpenTTD Base Graphics
}

INSTANTIATE_BASE_MEDIA_METHODS(BaseMedia<GraphicsSet>, GraphicsSet)
