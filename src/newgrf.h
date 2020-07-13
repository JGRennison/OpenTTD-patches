/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf.h Base for the NewGRF implementation. */

#ifndef NEWGRF_H
#define NEWGRF_H

#include "cargotype.h"
#include "rail_type.h"
#include "road_type.h"
#include "fileio_type.h"
#include "debug.h"
#include "core/bitmath_func.hpp"
#include "core/alloc_type.hpp"
#include "core/smallvec_type.hpp"
#include "3rdparty/cpp-btree/btree_map.h"
#include <bitset>

/**
 * List of different canal 'features'.
 * Each feature gets an entry in the canal spritegroup table
 */
enum CanalFeature {
	CF_WATERSLOPE,
	CF_LOCKS,
	CF_DIKES,
	CF_ICON,
	CF_DOCKS,
	CF_RIVER_SLOPE,
	CF_RIVER_EDGE,
	CF_RIVER_GUI,
	CF_BUOY,
	CF_END,
};

/** Canal properties local to the NewGRF */
struct CanalProperties {
	uint8 callback_mask;  ///< Bitmask of canal callbacks that have to be called.
	uint8 flags;          ///< Flags controlling display.
};

enum GrfLoadingStage {
	GLS_FILESCAN,
	GLS_SAFETYSCAN,
	GLS_LABELSCAN,
	GLS_INIT,
	GLS_RESERVE,
	GLS_ACTIVATION,
	GLS_END,
};

DECLARE_POSTFIX_INCREMENT(GrfLoadingStage)

enum GrfMiscBit {
	GMB_DESERT_TREES_FIELDS    = 0, // Unsupported.
	GMB_DESERT_PAVED_ROADS     = 1,
	GMB_FIELD_BOUNDING_BOX     = 2, // Unsupported.
	GMB_TRAIN_WIDTH_32_PIXELS  = 3, ///< Use 32 pixels per train vehicle in depot gui and vehicle details. Never set in the global variable; @see GRFFile::traininfo_vehicle_width
	GMB_AMBIENT_SOUND_CALLBACK = 4,
	GMB_CATENARY_ON_3RD_TRACK  = 5, // Unsupported.
	GMB_SECOND_ROCKY_TILE_SET  = 6,
};

enum GrfSpecFeature {
	GSF_TRAINS,
	GSF_ROADVEHICLES,
	GSF_SHIPS,
	GSF_AIRCRAFT,
	GSF_STATIONS,
	GSF_CANALS,
	GSF_BRIDGES,
	GSF_HOUSES,
	GSF_GLOBALVAR,
	GSF_INDUSTRYTILES,
	GSF_INDUSTRIES,
	GSF_CARGOES,
	GSF_SOUNDFX,
	GSF_AIRPORTS,
	GSF_SIGNALS,
	GSF_OBJECTS,
	GSF_RAILTYPES,
	GSF_AIRPORTTILES,
	GSF_ROADTYPES,
	GSF_TRAMTYPES,
	GSF_END,

	GSF_FAKE_TOWNS = GSF_END, ///< Fake town GrfSpecFeature for NewGRF debugging (parent scope)
	GSF_FAKE_STATION_STRUCT,  ///< Fake station struct GrfSpecFeature for NewGRF debugging
	GSF_FAKE_END,             ///< End of the fake features

	GSF_INVALID = 0xFF,       ///< An invalid spec feature
};

static const uint32 INVALID_GRFID = 0xFFFFFFFF;

struct GRFLabel {
	byte label;
	uint32 nfo_line;
	size_t pos;
	struct GRFLabel *next;
};

enum Action0RemapPropertyIds {
	A0RPI_CHECK_PROPERTY_LENGTH = 0x10000,
	A0RPI_UNKNOWN_IGNORE = 0x200,
	A0RPI_UNKNOWN_ERROR,

	A0RPI_STATION_MIN_BRIDGE_HEIGHT,
	A0RPI_STATION_DISALLOWED_BRIDGE_PILLARS,
	A0RPI_BRIDGE_MENU_ICON,
	A0RPI_BRIDGE_PILLAR_FLAGS,
};

enum GRFPropertyMapFallbackMode {
	GPMFM_IGNORE,
	GPMFM_ERROR_ON_USE,
	GPMFM_ERROR_ON_DEFINITION,
	GPMFM_END,
};

struct GRFPropertyMapDefinition {
	const char *name; // nullptr indicates the end of the list
	int id;
	uint8 feature;

	/** Create empty object used to identify the end of a list. */
	GRFPropertyMapDefinition() :
		name(nullptr),
		id(0),
		feature(0)
	{}

	GRFPropertyMapDefinition(uint8 feature, int id, const char *name) :
		name(name),
		id(id),
		feature(feature)
	{}
};

struct GRFFilePropertyRemapEntry {
	const char *name = nullptr;
	int id = 0;
	uint8 feature = 0;
	uint8 property_id = 0;
};

struct GRFFilePropertyRemapSet {
	std::bitset<256> remapped_ids;
	btree::btree_map<uint8, GRFFilePropertyRemapEntry> mapping;

	GRFFilePropertyRemapEntry &Entry(uint8 property)
	{
		this->remapped_ids.set(property);
		return this->mapping[property];
	}
};

/** The type of action 5 type. */
enum Action5BlockType {
	A5BLOCK_FIXED,                ///< Only allow replacing a whole block of sprites. (TTDP compatible)
	A5BLOCK_ALLOW_OFFSET,         ///< Allow replacing any subset by specifiing an offset.
	A5BLOCK_INVALID,              ///< unknown/not-implemented type
};
/** Information about a single action 5 type. */
struct Action5Type {
	Action5BlockType block_type;  ///< How is this Action5 type processed?
	SpriteID sprite_base;         ///< Load the sprites starting from this sprite.
	uint16 min_sprites;           ///< If the Action5 contains less sprites, the whole block will be ignored.
	uint16 max_sprites;           ///< If the Action5 contains more sprites, only the first max_sprites sprites will be used.
	const char *name;             ///< Name for error messages.
};

struct Action5TypeRemapDefinition {
	const char *name; // nullptr indicates the end of the list
	const Action5Type info;

	/** Create empty object used to identify the end of a list. */
	Action5TypeRemapDefinition() :
		name(nullptr),
		info({ A5BLOCK_INVALID, 0, 0, 0, nullptr })
	{}

	Action5TypeRemapDefinition(const char *type_name, Action5BlockType block_type, SpriteID sprite_base, uint16 min_sprites, uint16 max_sprites, const char *info_name) :
		name(type_name),
		info({ block_type, sprite_base, min_sprites, max_sprites, info_name })
	{}
};

struct Action5TypeRemapEntry {
	const Action5Type *info = nullptr;
	const char *name = nullptr;
	uint8 type_id = 0;
	GRFPropertyMapFallbackMode fallback_mode = GPMFM_IGNORE;
};

struct Action5TypeRemapSet {
	std::bitset<256> remapped_ids;
	btree::btree_map<uint8, Action5TypeRemapEntry> mapping;

	Action5TypeRemapEntry &Entry(uint8 property)
	{
		this->remapped_ids.set(property);
		return this->mapping[property];
	}
};

/** Dynamic data of a loaded NewGRF */
struct GRFFile : ZeroedMemoryAllocator {
	char *filename;
	uint32 grfid;
	byte grf_version;

	uint sound_offset;
	uint16 num_sounds;

	struct StationSpec **stations;
	struct HouseSpec **housespec;
	struct IndustrySpec **industryspec;
	struct IndustryTileSpec **indtspec;
	struct ObjectSpec **objectspec;
	struct AirportSpec **airportspec;
	struct AirportTileSpec **airtspec;

	GRFFilePropertyRemapSet action0_property_remaps[GSF_END];
	Action5TypeRemapSet action5_type_remaps;
	std::vector<std::unique_ptr<const char, FreeDeleter>> remap_unknown_property_names;

	uint32 param[0x80];
	uint param_end;  ///< one more than the highest set parameter

	GRFLabel *label; ///< Pointer to the first label. This is a linked list, not an array.

	std::vector<CargoLabel> cargo_list;             ///< Cargo translation table (local ID -> label)
	uint8 cargo_map[NUM_CARGO];                     ///< Inverse cargo translation table (CargoID -> local ID)

	std::vector<RailTypeLabel> railtype_list;       ///< Railtype translation table
	RailType railtype_map[RAILTYPE_END];

	std::vector<RoadTypeLabel> roadtype_list;       ///< Roadtype translation table (road)
	RoadType roadtype_map[ROADTYPE_END];

	std::vector<RoadTypeLabel> tramtype_list;       ///< Roadtype translation table (tram)
	RoadType tramtype_map[ROADTYPE_END];

	CanalProperties canal_local_properties[CF_END]; ///< Canal properties as set by this NewGRF

	struct LanguageMap *language_map; ///< Mappings related to the languages.

	int traininfo_vehicle_pitch;  ///< Vertical offset for drawing train images in depot GUI and vehicle details
	uint traininfo_vehicle_width; ///< Width (in pixels) of a 8/8 train vehicle in depot GUI and vehicle details

	uint32 grf_features;                     ///< Bitset of GrfSpecFeature the grf uses
	PriceMultipliers price_base_multipliers; ///< Price base multipliers as set by the grf.

	uint32 var8D_overlay;                    ///< Overlay for global variable 8D (action 0x14)
	uint32 var9D_overlay;                    ///< Overlay for global variable 9D (action 0x14)

	GRFFile(const struct GRFConfig *config);
	~GRFFile();

	/** Get GRF Parameter with range checking */
	uint32 GetParam(uint number) const
	{
		/* Note: We implicitly test for number < lengthof(this->param) and return 0 for invalid parameters.
		 *       In fact this is the more important test, as param is zeroed anyway. */
		assert(this->param_end <= lengthof(this->param));
		return (number < this->param_end) ? this->param[number] : 0;
	}
};

enum ShoreReplacement {
	SHORE_REPLACE_NONE,       ///< No shore sprites were replaced.
	SHORE_REPLACE_ACTION_5,   ///< Shore sprites were replaced by Action5.
	SHORE_REPLACE_ACTION_A,   ///< Shore sprites were replaced by ActionA (using grass tiles for the corner-shores).
	SHORE_REPLACE_ONLY_NEW,   ///< Only corner-shores were loaded by Action5 (openttd(w/d).grf only).
};

enum TramReplacement {
	TRAMWAY_REPLACE_DEPOT_NONE,       ///< No tram depot graphics were loaded.
	TRAMWAY_REPLACE_DEPOT_WITH_TRACK, ///< Electrified depot graphics with tram track were loaded.
	TRAMWAY_REPLACE_DEPOT_NO_TRACK,   ///< Electrified depot graphics without tram track were loaded.
};

struct GRFLoadedFeatures {
	bool has_2CC;             ///< Set if any vehicle is loaded which uses 2cc (two company colours).
	uint64 used_liveries;     ///< Bitmask of #LiveryScheme used by the defined engines.
	ShoreReplacement shore;   ///< In which way shore sprites were replaced.
	TramReplacement tram;     ///< In which way tram depots were replaced.
};

/**
 * Check for grf miscellaneous bits
 * @param bit The bit to check.
 * @return Whether the bit is set.
 */
static inline bool HasGrfMiscBit(GrfMiscBit bit)
{
	extern byte _misc_grf_features;
	return HasBit(_misc_grf_features, bit);
}

/* Indicates which are the newgrf features currently loaded ingame */
extern GRFLoadedFeatures _loaded_newgrf_features;

byte GetGRFContainerVersion();

void LoadNewGRFFile(struct GRFConfig *config, uint file_index, GrfLoadingStage stage, Subdirectory subdir);
void LoadNewGRF(uint load_index, uint file_index, uint num_baseset);
void ReloadNewGRFData(); // in saveload/afterload.cpp
void ResetNewGRFData();
void ResetPersistentNewGRFData();

#define grfmsg(severity, ...) if ((severity) == 0 || _debug_grf_level >= (severity)) _intl_grfmsg(severity, __VA_ARGS__)
void CDECL _intl_grfmsg(int severity, const char *str, ...) WARN_FORMAT(2, 3);

bool GetGlobalVariable(byte param, uint32 *value, const GRFFile *grffile);

StringID MapGRFStringID(uint32 grfid, StringID str);
void ShowNewGRFError();
uint CountSelectedGRFs(GRFConfig *grfconf);

struct TemplateVehicle;

#endif /* NEWGRF_H */
