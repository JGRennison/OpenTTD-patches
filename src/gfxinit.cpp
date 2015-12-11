/* $Id$ */

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
#include "table/tree_land.h"
#include "blitter/32bpp_base.hpp"

/* The type of set we're replacing */
#define SET_TYPE "graphics"
#include "base_media_func.h"

#include "table/sprites.h"

#include "safeguards.h"

/** Whether the given NewGRFs must get a palette remap from windows to DOS or not. */
bool _palette_remap_grf[MAX_FILE_SLOTS];

#include "table/landscape_sprite.h"

/** Offsets for loading the different "replacement" sprites in the files. */
static const SpriteID * const _landscape_spriteindexes[] = {
	_landscape_spriteindexes_arctic,
	_landscape_spriteindexes_tropic,
	_landscape_spriteindexes_toyland,
};

/** file index of first user-added GRF file */
int _first_user_grf_file_index;
int _opengfx_grf_file_index;

/**
 * Load an old fashioned GRF file.
 * @param filename   The name of the file to open.
 * @param load_index The offset of the first sprite.
 * @param file_index The Fio offset to load the file in.
 * @return The number of loaded sprites.
 */
static uint LoadGrfFile(const char *filename, uint load_index, int file_index)
{
	uint load_index_org = load_index;
	uint sprite_id = 0;

	FioOpenFile(file_index, filename, BASESET_DIR);

	DEBUG(sprite, 2, "Reading grf-file '%s'", filename);

	byte container_ver = GetGRFContainerVersion();
	if (container_ver == 0) usererror("Base grf '%s' is corrupt", filename);
	ReadGRFSpriteOffsets(container_ver);
	if (container_ver >= 2) {
		/* Read compression. */
		byte compression = FioReadByte();
		if (compression != 0) usererror("Unsupported compression format");
	}

	while (LoadNextSprite(load_index, file_index, sprite_id, container_ver)) {
		load_index++;
		sprite_id++;
		if (load_index >= MAX_SPRITES) {
			usererror("Too many sprites. Recompile with higher MAX_SPRITES value or remove some custom GRF files.");
		}
	}
	DEBUG(sprite, 2, "Currently %i sprites are loaded", load_index);

	return load_index - load_index_org;
}

/**
 * Load an old fashioned GRF file to replace already loaded sprites.
 * @param filename   The name of the file to open.
 * @param index_tlb  The offsets of each of the sprites.
 * @param file_index The Fio offset to load the file in.
 * @return The number of loaded sprites.
 */
static void LoadGrfFileIndexed(const char *filename, const SpriteID *index_tbl, int file_index)
{
	uint start;
	uint sprite_id = 0;

	FioOpenFile(file_index, filename, BASESET_DIR);

	DEBUG(sprite, 2, "Reading indexed grf-file '%s'", filename);

	byte container_ver = GetGRFContainerVersion();
	if (container_ver == 0) usererror("Base grf '%s' is corrupt", filename);
	ReadGRFSpriteOffsets(container_ver);
	if (container_ver >= 2) {
		/* Read compression. */
		byte compression = FioReadByte();
		if (compression != 0) usererror("Unsupported compression format");
	}

	while ((start = *index_tbl++) != END) {
		uint end = *index_tbl++;

		do {
			bool b = LoadNextSprite(start, file_index, sprite_id, container_ver);
			assert(b);
			sprite_id++;
		} while (++start <= end);
	}
}

/**
 * Checks whether the MD5 checksums of the files are correct.
 *
 * @note Also checks sample.cat and other required non-NewGRF GRFs for corruption.
 */
void CheckExternalFiles()
{
	if (BaseGraphics::GetUsedSet() == NULL || BaseSounds::GetUsedSet() == NULL) return;

	const GraphicsSet *used_set = BaseGraphics::GetUsedSet();

	DEBUG(grf, 1, "Using the %s base graphics set", used_set->name);

	static const size_t ERROR_MESSAGE_LENGTH = 256;
	static const size_t MISSING_FILE_MESSAGE_LENGTH = 128;

	/* Allocate for a message for each missing file and for one error
	 * message per set.
	 */
	char error_msg[MISSING_FILE_MESSAGE_LENGTH * (GraphicsSet::NUM_FILES + SoundsSet::NUM_FILES) + 2 * ERROR_MESSAGE_LENGTH];
	error_msg[0] = '\0';
	char *add_pos = error_msg;
	const char *last = lastof(error_msg);

	if (used_set->GetNumInvalid() != 0) {
		/* Not all files were loaded successfully, see which ones */
		add_pos += seprintf(add_pos, last, "Trying to load graphics set '%s', but it is incomplete. The game will probably not run correctly until you properly install this set or select another one. See section 4.1 of readme.txt.\n\nThe following files are corrupted or missing:\n", used_set->name);
		for (uint i = 0; i < GraphicsSet::NUM_FILES; i++) {
			MD5File::ChecksumResult res = GraphicsSet::CheckMD5(&used_set->files[i], BASESET_DIR);
			if (res != MD5File::CR_MATCH) add_pos += seprintf(add_pos, last, "\t%s is %s (%s)\n", used_set->files[i].filename, res == MD5File::CR_MISMATCH ? "corrupt" : "missing", used_set->files[i].missing_warning);
		}
		add_pos += seprintf(add_pos, last, "\n");
	}

	const SoundsSet *sounds_set = BaseSounds::GetUsedSet();
	if (sounds_set->GetNumInvalid() != 0) {
		add_pos += seprintf(add_pos, last, "Trying to load sound set '%s', but it is incomplete. The game will probably not run correctly until you properly install this set or select another one. See section 4.1 of readme.txt.\n\nThe following files are corrupted or missing:\n", sounds_set->name);

		assert_compile(SoundsSet::NUM_FILES == 1);
		/* No need to loop each file, as long as there is only a single
		 * sound file. */
		add_pos += seprintf(add_pos, last, "\t%s is %s (%s)\n", sounds_set->files->filename, SoundsSet::CheckMD5(sounds_set->files, BASESET_DIR) == MD5File::CR_MISMATCH ? "corrupt" : "missing", sounds_set->files->missing_warning);
	}

	if (add_pos != error_msg) ShowInfoF("%s", error_msg);
}

/** Actually load the sprite tables. */
static void LoadSpriteTables()
{
	memset(_palette_remap_grf, 0, sizeof(_palette_remap_grf));
	uint i = FIRST_GRF_SLOT;
	const GraphicsSet *used_set = BaseGraphics::GetUsedSet();

	_palette_remap_grf[i] = (PAL_DOS != used_set->palette);
	LoadGrfFile(used_set->files[GFT_BASE].filename, 0, i++);

	/* Progsignal sprites. */
	LoadGrfFile("progsignals.grf", SPR_PROGSIGNAL_BASE, i++);

	/* Tracerestrict sprites. */
	LoadGrfFile("tracerestrict.grf", SPR_TRACERESTRICT_BASE, i++);

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
	_palette_remap_grf[i] = (PAL_DOS != used_set->palette);
	LoadGrfFile(used_set->files[GFT_LOGOS].filename, 4793, i++);

	/*
	 * Load additional sprites for climates other than temperate.
	 * This overwrites some of the temperate sprites, such as foundations
	 * and the ground sprites.
	 */
	if (_settings_game.game_creation.landscape != LT_TEMPERATE) {
		_palette_remap_grf[i] = (PAL_DOS != used_set->palette);
		LoadGrfFileIndexed(
			used_set->files[GFT_ARCTIC + _settings_game.game_creation.landscape - 1].filename,
			_landscape_spriteindexes[_settings_game.game_creation.landscape - 1],
			i++
		);
	}

	LoadGrfFile("innerhighlight.grf", SPR_ZONING_INNER_HIGHLIGHT_BASE, i++);

	/* Load route step graphics */
	LoadGrfFile("route_step.grf", SPR_ROUTE_STEP_BASE, i++);

	/* Initialize the unicode to sprite mapping table */
	InitializeUnicodeGlyphMap();

	/*
	 * Load the base NewGRF with OTTD required graphics as first NewGRF.
	 * However, we do not want it to show up in the list of used NewGRFs,
	 * so we have to manually add it, and then remove it later.
	 */
	GRFConfig *top = _grfconfig;
	GRFConfig *master = new GRFConfig(used_set->files[GFT_EXTRA].filename);

	/* We know the palette of the base set, so if the base NewGRF is not
	 * setting one, use the palette of the base set and not the global
	 * one which might be the wrong palette for this base NewGRF.
	 * The value set here might be overridden via action14 later. */
	switch (used_set->palette) {
		case PAL_DOS:     master->palette |= GRFP_GRF_DOS;     break;
		case PAL_WINDOWS: master->palette |= GRFP_GRF_WINDOWS; break;
		default: break;
	}
	FillGRFDetails(master, false, BASESET_DIR);

	ClrBit(master->flags, GCF_INIT_ONLY);
	master->next = top;
	_grfconfig = master;

	LoadNewGRF(SPR_NEWGRFS_BASE, i);

	_first_user_grf_file_index = i + 1;
	_opengfx_grf_file_index = -1;
	uint index = i;
	for (GRFConfig *c = master; c != NULL; c = c->next, index++) {
		if (c->status == GCS_DISABLED || c->status == GCS_NOT_FOUND || HasBit(c->flags, GCF_INIT_ONLY)) continue;
		if (c->ident.grfid == BSWAP32(0xFF4F4701)) {
			/* Detect OpenGFX GRF ID */
			_opengfx_grf_file_index = index;
			break;
		}
	}

	/* Free and remove the top element. */
	delete master;
	_grfconfig = top;
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
	uint depth_wanted_by_grf = _support8bpp == S8BPP_NONE ? 32 : 8;
	for (GRFConfig *c = _grfconfig; c != NULL; c = c->next) {
		if (c->status == GCS_DISABLED || c->status == GCS_NOT_FOUND || HasBit(c->flags, GCF_INIT_ONLY)) continue;
		if (c->palette & GRFP_BLT_32BPP) depth_wanted_by_grf = 32;
	}

	/* Search the best blitter. */
	static const struct {
		const char *name;
		uint animation; ///< 0: no support, 1: do support, 2: both
		uint min_base_depth, max_base_depth, min_grf_depth, max_grf_depth;
	} replacement_blitters[] = {
#ifdef WITH_SSE
		{ "32bpp-sse4",      0, 32, 32,  8, 32 },
		{ "32bpp-ssse3",     0, 32, 32,  8, 32 },
		{ "32bpp-sse2",      0, 32, 32,  8, 32 },
		{ "32bpp-sse4-anim", 1, 32, 32,  8, 32 },
#endif
		{ "8bpp-optimized",  2,  8,  8,  8,  8 },
		{ "32bpp-optimized", 0,  8, 32,  8, 32 },
		{ "32bpp-anim",      1,  8, 32,  8, 32 },
	};

	const bool animation_wanted = HasBit(_display_opt, DO_FULL_ANIMATION);
	const char *cur_blitter = BlitterFactory::GetCurrentBlitter()->GetName();

	for (uint i = 0; i < lengthof(replacement_blitters); i++) {
		if (animation_wanted && (replacement_blitters[i].animation == 0)) continue;
		if (!animation_wanted && (replacement_blitters[i].animation == 1)) continue;

		if (!IsInsideMM(depth_wanted_by_base, replacement_blitters[i].min_base_depth, replacement_blitters[i].max_base_depth + 1)) continue;
		if (!IsInsideMM(depth_wanted_by_grf, replacement_blitters[i].min_grf_depth, replacement_blitters[i].max_grf_depth + 1)) continue;
		const char *repl_blitter = replacement_blitters[i].name;

		if (strcmp(repl_blitter, cur_blitter) == 0) return false;
		if (BlitterFactory::GetBlitterFactory(repl_blitter) == NULL) continue;

		DEBUG(misc, 1, "Switching blitter from '%s' to '%s'... ", cur_blitter, repl_blitter);
		Blitter *new_blitter = BlitterFactory::SelectBlitter(repl_blitter);
		if (new_blitter == NULL) NOT_REACHED();
		DEBUG(misc, 1, "Successfully switched to %s.", repl_blitter);
		break;
	}

	if (!VideoDriver::GetInstance()->AfterBlitterChange()) {
		/* Failed to switch blitter, let's hope we can return to the old one. */
		if (BlitterFactory::SelectBlitter(cur_blitter) == NULL || !VideoDriver::GetInstance()->AfterBlitterChange()) usererror("Failed to reinitialize video driver. Specify a fixed blitter in the config");
	}

	return true;
}

/** Check whether we still use the right blitter, or use another (better) one. */
void CheckBlitter()
{
	if (!SwitchNewGRFBlitter()) return;

	ClearFontCache();
	GfxClearSpriteCache();
	ReInitAllWindows();
}

static void UpdateRouteStepSpriteSize()
{
	extern uint _vp_route_step_width;
	extern uint _vp_route_step_height_top;
	extern uint _vp_route_step_height_middle;
	extern uint _vp_route_step_height_bottom;
	extern SubSprite _vp_route_step_subsprite;

	Dimension d = GetSpriteSize(SPR_ROUTE_STEP_TOP);
	_vp_route_step_width = d.width;
	_vp_route_step_height_top = d.height;

	d = GetSpriteSize(SPR_ROUTE_STEP_MIDDLE);
	_vp_route_step_height_middle = d.height;
	assert(_vp_route_step_width == d.width);

	d = GetSpriteSize(SPR_ROUTE_STEP_BOTTOM);
	_vp_route_step_height_bottom = d.height;
	assert(_vp_route_step_width == d.width);

	const int char_height = GetCharacterHeight(FS_SMALL) + 1;
	_vp_route_step_subsprite.right = ScaleByZoom(_vp_route_step_width, ZOOM_LVL_GUI);
	_vp_route_step_subsprite.bottom = ScaleByZoom(char_height, ZOOM_LVL_GUI);
	_vp_route_step_subsprite.left = 0;
	_vp_route_step_subsprite.top = 0;
}

/* multi can be density, field type, ... */
static SpriteID GetSpriteIDForClearGround(const ClearGround cg, const Slope slope, const uint multi)
{
	switch (cg) {
		case CLEAR_GRASS:
			return GetSpriteIDForClearLand(slope, (byte) multi);
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

/** Once the sprites are loaded, we can determine main colours of ground/water/... */
void GfxDetermineMainColours()
{
	/* Water. */
	extern uint32 _vp_map_water_colour[5];
	_vp_map_water_colour[0] = GetSpriteMainColour(SPR_FLAT_WATER_TILE, PAL_NONE);
	if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 32) {
		_vp_map_water_colour[1] = Blitter_32bppBase::MakeTransparent(_vp_map_water_colour[0], 256, 192).data; // lighter
		_vp_map_water_colour[2] = Blitter_32bppBase::MakeTransparent(_vp_map_water_colour[0], 192, 256).data; // darker
		_vp_map_water_colour[3] = _vp_map_water_colour[2];
		_vp_map_water_colour[4] = _vp_map_water_colour[1];
	}

	/* Clear ground. */
	extern uint32 _vp_map_vegetation_clear_colours[16][6][8];
	memset(_vp_map_vegetation_clear_colours, 0, sizeof(_vp_map_vegetation_clear_colours));
	const struct {
		byte min;
		byte max;
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
	extern uint32 _vp_map_vegetation_tree_colours[5][MAX_TREE_COUNT_BY_LANDSCAPE];
	const uint base  = _tree_base_by_landscape[_settings_game.game_creation.landscape];
	const uint count = _tree_count_by_landscape[_settings_game.game_creation.landscape];
	for (uint tg = 0; tg < 5; tg++) {
		for (uint i = base; i < base + count; i++) {
			_vp_map_vegetation_tree_colours[tg][i - base] = GetSpriteMainColour(_tree_sprites[i].sprite, _tree_sprites[i].pal);
		}
		const int diff = MAX_TREE_COUNT_BY_LANDSCAPE - count;
		if (diff > 0) {
			for (uint i = count; i < MAX_TREE_COUNT_BY_LANDSCAPE; i++)
				_vp_map_vegetation_tree_colours[tg][i] = _vp_map_vegetation_tree_colours[tg][i - count];
		}
	}
}

/** Initialise and load all the sprites. */
void GfxLoadSprites()
{
	DEBUG(sprite, 2, "Loading sprite set %d", _settings_game.game_creation.landscape);

	SwitchNewGRFBlitter();
	ClearFontCache();
	GfxInitSpriteMem();
	LoadSpriteTables();
	GfxInitPalettes();
	GfxDetermineMainColours();

	UpdateRouteStepSpriteSize();
	UpdateCursorSize();
}

bool GraphicsSet::FillSetDetails(IniFile *ini, const char *path, const char *full_filename)
{
	bool ret = this->BaseSet<GraphicsSet, MAX_GFT, true>::FillSetDetails(ini, path, full_filename, false);
	if (ret) {
		IniGroup *metadata = ini->GetGroup("metadata");
		IniItem *item;

		fetch_metadata("palette");
		this->palette = (*item->value == 'D' || *item->value == 'd') ? PAL_DOS : PAL_WINDOWS;

		/* Get optional blitter information. */
		item = metadata->GetItem("blitter", false);
		this->blitter = (item != NULL && *item->value == '3') ? BLT_32BPP : BLT_8BPP;
	}
	return ret;
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
	FILE *f = FioFOpenFile(file->filename, "rb", subdir, &size);
	if (f == NULL) return MD5File::CR_NO_FILE;

	size_t max = GRFGetSizeOfDataSection(f);

	FioFCloseFile(f);

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
	FILE *f = FioFOpenFile(this->filename, "rb", subdir, &size);

	if (f == NULL) return CR_NO_FILE;

	size = min(size, max_size);

	Md5 checksum;
	uint8 buffer[1024];
	uint8 digest[16];
	size_t len;

	while ((len = fread(buffer, 1, (size > sizeof(buffer)) ? sizeof(buffer) : size, f)) != 0 && size != 0) {
		size -= len;
		checksum.Append(buffer, len);
	}

	FioFCloseFile(f);

	checksum.Finish(digest);
	return memcmp(this->hash, digest, sizeof(this->hash)) == 0 ? CR_MATCH : CR_MISMATCH;
}

/** Names corresponding to the GraphicsFileType */
static const char * const _graphics_file_names[] = { "base", "logos", "arctic", "tropical", "toyland", "extra" };

/** Implementation */
template <class T, size_t Tnum_files, bool Tsearch_in_tars>
/* static */ const char * const *BaseSet<T, Tnum_files, Tsearch_in_tars>::file_names = _graphics_file_names;

template <class Tbase_set>
/* static */ bool BaseMedia<Tbase_set>::DetermineBestSet()
{
	if (BaseMedia<Tbase_set>::used_set != NULL) return true;

	const Tbase_set *best = NULL;
	for (const Tbase_set *c = BaseMedia<Tbase_set>::available_sets; c != NULL; c = c->next) {
		/* Skip unusable sets */
		if (c->GetNumMissing() != 0) continue;

		if (best == NULL ||
				(best->fallback && !c->fallback) ||
				best->valid_files < c->valid_files ||
				(best->valid_files == c->valid_files && (
					(best->shortname == c->shortname && best->version < c->version) ||
					(best->palette != PAL_DOS && c->palette == PAL_DOS)))) {
			best = c;
		}
	}

	BaseMedia<Tbase_set>::used_set = best;
	return BaseMedia<Tbase_set>::used_set != NULL;
}

template <class Tbase_set>
/* static */ const char *BaseMedia<Tbase_set>::GetExtension()
{
	return ".obg"; // OpenTTD Base Graphics
}

INSTANTIATE_BASE_MEDIA_METHODS(BaseMedia<GraphicsSet>, GraphicsSet)
