/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_sl.cpp Code handling saving and loading of towns and houses */

#include "../stdafx.h"
#include "../newgrf_house.h"
#include "../town.h"
#include "../landscape.h"
#include "../subsidy_func.h"
#include "../strings_func.h"
#include "../network/network.h"

#include "saveload.h"
#include "newgrf_sl.h"

#include "../safeguards.h"

static bool _town_zone_radii_no_update = false;

extern bool _town_noise_no_update;
extern bool IsGetTownZonesCallbackHandlerPresent();

HouseID SLGetCleanHouseType(TileIndex t, bool old_map_position)
{
	if (old_map_position && SlXvIsFeatureMissing(XSLFI_MORE_HOUSES)) {
		return _m[t].m4 | (GB(_m[t].m3, 6, 1) << 8);
	} else {
		return GetCleanHouseType(t);
	}
}

/**
 * Rebuild all the cached variables of towns.
 */
void RebuildTownCaches(bool cargo_update_required, bool old_map_position)
{
	InitializeBuildingCounts();
	RebuildTownKdtree();

	/* Reset town population and num_houses */
	for (Town *town : Town::Iterate()) {
		town->cache.population = 0;
		town->cache.num_houses = 0;
	}

	for (TileIndex t = 0; t < MapSize(); t++) {
		if (!IsTileType(t, MP_HOUSE)) continue;

		HouseID house_id = GetTranslatedHouseID(SLGetCleanHouseType(t, old_map_position));
		Town *town = Town::GetByTile(t);
		IncreaseBuildingCount(town, house_id);
		if (IsHouseCompleted(t)) town->cache.population += HouseSpec::Get(house_id)->population;

		/* Increase the number of houses for every house, but only once. */
		if (GetHouseNorthPart(house_id) == 0) town->cache.num_houses++;
	}

	if (!_town_zone_radii_no_update) {
		/* Update the population and num_house dependent values */
		for (Town *town : Town::Iterate()) {
			UpdateTownRadius(town);
		}
	}
}

static void CheckMultiTileHouseTypes(bool &cargo_update_required, bool old_map_position, bool translate_house_types)
{
	auto get_clean_house_type = [&](TileIndex t) -> HouseID {
		HouseID type = SLGetCleanHouseType(t, old_map_position);
		if (translate_house_types) type = GetTranslatedHouseID(type);
		return type;
	};

	/* Check for cases when a NewGRF has set a wrong house substitute type. */
	for (TileIndex t = 0; t < MapSize(); t++) {
		if (!IsTileType(t, MP_HOUSE)) continue;

		HouseID house_type = get_clean_house_type(t);
		TileIndex north_tile = t + GetHouseNorthPart(house_type); // modifies 'house_type'!
		if (t == north_tile) {
			const HouseSpec *hs = HouseSpec::Get(house_type);
			bool valid_house = true;
			if (hs->building_flags & TILE_SIZE_2x1) {
				TileIndex tile = t + TileDiffXY(1, 0);
				if (!IsTileType(tile, MP_HOUSE) || get_clean_house_type(tile) != house_type + 1) valid_house = false;
			} else if (hs->building_flags & TILE_SIZE_1x2) {
				TileIndex tile = t + TileDiffXY(0, 1);
				if (!IsTileType(tile, MP_HOUSE) || get_clean_house_type(tile) != house_type + 1) valid_house = false;
			} else if (hs->building_flags & TILE_SIZE_2x2) {
				TileIndex tile = t + TileDiffXY(0, 1);
				if (!IsTileType(tile, MP_HOUSE) || get_clean_house_type(tile) != house_type + 1) valid_house = false;
				tile = t + TileDiffXY(1, 0);
				if (!IsTileType(tile, MP_HOUSE) || get_clean_house_type(tile) != house_type + 2) valid_house = false;
				tile = t + TileDiffXY(1, 1);
				if (!IsTileType(tile, MP_HOUSE) || get_clean_house_type(tile) != house_type + 3) valid_house = false;
			}
			/* If not all tiles of this house are present remove the house.
			 * The other tiles will get removed later in this loop because
			 * their north tile is not the correct type anymore. */
			if (!valid_house) {
				DoClearSquare(t);
				cargo_update_required = true;
			}
		} else if (!IsTileType(north_tile, MP_HOUSE) || get_clean_house_type(north_tile) != house_type) {
			/* This tile should be part of a multi-tile building but the
			 * north tile of this house isn't on the map. */
			DoClearSquare(t);
			cargo_update_required = true;
		}
	}
}

/**
 * Check and update town and house values.
 *
 * Checked are the HouseIDs. Updated are the
 * town population the number of houses per
 * town, the town radius and the max passengers
 * of the town.
 */
void UpdateHousesAndTowns(bool cargo_update_required, bool old_map_position)
{
	auto get_clean_house_type = [&](TileIndex t) -> HouseID {
		return SLGetCleanHouseType(t, old_map_position);
	};
	for (TileIndex t = 0; t < MapSize(); t++) {
		if (!IsTileType(t, MP_HOUSE)) continue;

		HouseID house_id = get_clean_house_type(t);
		if (!HouseSpec::Get(house_id)->enabled && house_id >= NEW_HOUSE_OFFSET) {
			/* The specs for this type of house are not available any more, so
			 * replace it with the substitute original house type. */
			house_id = _house_mngr.GetSubstituteID(house_id);
			if (old_map_position && SlXvIsFeatureMissing(XSLFI_MORE_HOUSES)) {
				_m[t].m4 = GB(house_id, 0, 8);
				SB(_m[t].m3, 6, 1, GB(house_id, 8, 1));
			} else {
				SetHouseType(t, house_id);
			}
			cargo_update_required = true;
		}
	}

	CheckMultiTileHouseTypes(cargo_update_required, old_map_position, false);
	if (cargo_update_required || SlXvIsFeatureMissing(XSLFI_MORE_HOUSES, 2)) CheckMultiTileHouseTypes(cargo_update_required, old_map_position, true);

	RebuildTownCaches(cargo_update_required, old_map_position);
}

/** Save and load of towns. */
static const SaveLoad _town_desc[] = {
	SLE_CONDVAR(Town, xy,                    SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Town, xy,                    SLE_UINT32,                 SLV_6, SL_MAX_VERSION),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_3),                   ///< population, no longer in use
	SLE_CONDNULL(4, SLV_3, SLV_85),                  ///< population, no longer in use
	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_92),                  ///< num_houses, no longer in use

	SLE_CONDVAR(Town, townnamegrfid,         SLE_UINT32, SLV_66, SL_MAX_VERSION),
	    SLE_VAR(Town, townnametype,          SLE_UINT16),
	    SLE_VAR(Town, townnameparts,         SLE_UINT32),
	SLE_CONDSTR(Town, name,                  SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION),

	    SLE_VAR(Town, flags,                 SLE_UINT8),
	SLE_CONDVAR_X(Town, church_count,        SLE_UINT16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_MULTI_BUILDING)),
	SLE_CONDVAR_X(Town, stadium_count,       SLE_UINT16, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_MULTI_BUILDING)),
	SLE_CONDVAR(Town, statues,               SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_104),
	SLE_CONDVAR(Town, statues,               SLE_UINT16,               SLV_104, SL_MAX_VERSION),

	SLE_CONDNULL(1, SL_MIN_VERSION, SLV_2),                   ///< sort_index, no longer in use

	SLE_CONDVAR(Town, have_ratings,          SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_104),
	SLE_CONDVAR(Town, have_ratings,          SLE_UINT16,               SLV_104, SL_MAX_VERSION),
	SLE_CONDARR(Town, ratings,               SLE_INT16, 8,               SL_MIN_VERSION, SLV_104),
	SLE_CONDARR(Town, ratings,               SLE_INT16, MAX_COMPANIES, SLV_104, SL_MAX_VERSION),
	SLE_CONDNULL_X(MAX_COMPANIES, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_SPRINGPP)),
	/* failed bribe attempts are stored since savegame format 4 */
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  8,               SLV_4, SLV_104),
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  MAX_COMPANIES, SLV_104, SL_MAX_VERSION),

	SLE_CONDVAR(Town, supplied[0].old_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[2].old_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[0].new_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[2].new_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[0].old_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[2].old_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[0].new_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[2].new_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),

	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[0].old_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[2].old_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[0].new_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[2].new_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[0].old_act, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[2].old_act, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[0].new_act, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDNULL_X(4, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, supplied[2].new_act, SLE_UINT32,                 SLV_9, SLV_165),

	SLE_CONDNULL(2, SL_MIN_VERSION, SLV_164),                 ///< pct_pass_transported / pct_mail_transported, now computed on the fly
	SLE_CONDNULL_X(3, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),

	SLE_CONDVAR(Town, received[TAE_FOOD].old_act,      SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDVAR(Town, received[TAE_WATER].old_act,     SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, received[TAE_FOOD].new_act,      SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDVAR(Town, received[TAE_WATER].new_act,     SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),

	SLE_CONDARR(Town, goal, SLE_UINT32, NUM_TAE, SLV_165, SL_MAX_VERSION),

	SLE_CONDSSTR(Town, text,                 SLE_STR | SLF_ALLOW_CONTROL, SLV_168, SL_MAX_VERSION),

	SLE_CONDVAR(Town, time_until_rebuild,    SLE_FILE_U8 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_54),
	SLE_CONDVAR(Town, grow_counter,          SLE_FILE_U8 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_54),
	SLE_CONDVAR(Town, growth_rate,           SLE_FILE_U8 | SLE_VAR_I16,  SL_MIN_VERSION, SLV_54),

	SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP)),
	SLE_CONDVAR(Town, time_until_rebuild,    SLE_UINT16,                SLV_54, SL_MAX_VERSION),
	SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_26)),
	SLE_CONDVAR(Town, grow_counter,          SLE_UINT16,                SLV_54, SL_MAX_VERSION),

	SLE_CONDVAR(Town, growth_rate,           SLE_FILE_I16 | SLE_VAR_U16, SLV_54, SLV_165),
	SLE_CONDNULL_X(2, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_JOKERPP, SL_JOKER_1_26)),
	SLE_CONDVAR(Town, growth_rate,           SLE_UINT16,                 SLV_165, SL_MAX_VERSION),

	    SLE_VAR(Town, fund_buildings_months, SLE_UINT8),
	    SLE_VAR(Town, road_build_months,     SLE_UINT8),

	SLE_CONDVAR(Town, exclusivity,           SLE_UINT8,                  SLV_2, SL_MAX_VERSION),
	SLE_CONDNULL_X(1, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_CHILLPP, SL_CHILLPP_232)),
	SLE_CONDVAR(Town, exclusive_counter,     SLE_UINT8,                  SLV_2, SL_MAX_VERSION),

	SLE_CONDVAR(Town, larger_town,           SLE_BOOL,                  SLV_56, SL_MAX_VERSION),
	SLE_CONDVAR(Town, layout,                SLE_UINT8,                SLV_113, SL_MAX_VERSION),

	SLE_CONDREFLIST(Town, psa_list,          REF_STORAGE,              SLV_161, SL_MAX_VERSION),

	SLE_CONDNULL(4, SLV_166, SLV_EXTEND_CARGOTYPES),  ///< cargo_produced, no longer in use
	SLE_CONDNULL(8, SLV_EXTEND_CARGOTYPES, SLV_REMOVE_TOWN_CARGO_CACHE),  ///< cargo_produced, no longer in use
	SLE_CONDNULL(30, SLV_2, SLV_REMOVE_TOWN_CARGO_CACHE), ///< old reserved space

	SLE_CONDVAR_X(Town, override_flags,      SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_SETTING_OVERRIDE)),
	SLE_CONDVAR_X(Town, override_values,     SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_SETTING_OVERRIDE)),
	SLE_CONDVAR_X(Town, build_tunnels,       SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_SETTING_OVERRIDE)),
	SLE_CONDVAR_X(Town, max_road_slope,      SLE_UINT8, SL_MIN_VERSION, SL_MAX_VERSION, SlXvFeatureTest(XSLFTO_AND, XSLFI_TOWN_SETTING_OVERRIDE)),
};

static const SaveLoad _town_supplied_desc[] = {
	SLE_CONDVAR(TransportedCargoStat<uint32_t>, old_max, SLE_UINT32, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint32_t>, new_max, SLE_UINT32, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint32_t>, old_act, SLE_UINT32, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint32_t>, new_act, SLE_UINT32, SLV_165, SL_MAX_VERSION),
};

static const SaveLoad _town_received_desc[] = {
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, old_max, SLE_UINT16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, new_max, SLE_UINT16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, old_act, SLE_UINT16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, new_act, SLE_UINT16, SLV_165, SL_MAX_VERSION),
};

static const SaveLoad _town_received_desc_spp[] = {
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, old_max, SLE_FILE_U32 | SLE_VAR_U16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, new_max, SLE_FILE_U32 | SLE_VAR_U16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, old_act, SLE_FILE_U32 | SLE_VAR_U16, SLV_165, SL_MAX_VERSION),
	SLE_CONDVAR(TransportedCargoStat<uint16_t>, new_act, SLE_FILE_U32 | SLE_VAR_U16, SLV_165, SL_MAX_VERSION),
};

std::vector<SaveLoad> _filtered_town_desc;
std::vector<SaveLoad> _filtered_town_supplied_desc;
std::vector<SaveLoad> _filtered_town_received_desc;

static void SetupDescs_TOWN()
{
	_filtered_town_desc = SlFilterObject(_town_desc);
	_filtered_town_supplied_desc = SlFilterObject(_town_supplied_desc);
	_filtered_town_received_desc = SlFilterObject(_town_received_desc);
}

static void Save_HIDS()
{
	Save_NewGRFMapping(_house_mngr);
}

static void Load_HIDS()
{
	Load_NewGRFMapping(_house_mngr);
}

static void RealSave_Town(Town *t)
{
	SlObjectSaveFiltered(t, _filtered_town_desc);

	for (CargoID i = 0; i < NUM_CARGO; i++) {
		SlObjectSaveFiltered(&t->supplied[i], _filtered_town_supplied_desc);
	}
	for (int i = TAE_BEGIN; i < NUM_TAE; i++) {
		SlObjectSaveFiltered(&t->received[i], _filtered_town_received_desc);
	}
}

static void Save_TOWN()
{
	SetupDescs_TOWN();
	for (Town *t : Town::Iterate()) {
		SlSetArrayIndex(t->index);
		SlAutolength((AutolengthProc*)RealSave_Town, t);
	}
}

static void Load_TOWN()
{
	SetupDescs_TOWN();
	int index;
	uint num_cargo = IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES) ? 32 : NUM_CARGO;

	while ((index = SlIterateArray()) != -1) {
		Town *t = new (index) Town();
		SlObjectLoadFiltered(t, _filtered_town_desc);

		for (CargoID i = 0; i < num_cargo; i++) {
			SlObjectLoadFiltered(&t->supplied[i], _filtered_town_supplied_desc);
		}
		if (SlXvIsFeaturePresent(XSLFI_SPRINGPP)) {
			for (int i = TAE_BEGIN; i < NUM_TAE; i++) {
				SlObject(&t->received[i], _town_received_desc_spp);
			}
		} else {
			for (int i = TAE_BEGIN; i < NUM_TAE; i++) {
				SlObjectLoadFiltered(&t->received[i], _filtered_town_received_desc);
			}
		}

		if (t->townnamegrfid == 0 && !IsInsideMM(t->townnametype, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_LAST + 1) && GetStringTab(t->townnametype) != TEXT_TAB_OLD_CUSTOM) {
			SlErrorCorrupt("Invalid town name generator");
		}

		if ((!IsSavegameVersionBefore(SLV_166) && IsSavegameVersionBefore(SLV_REMOVE_TOWN_CARGO_CACHE)) || SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_MATRIX)) {
			SlSkipBytes(4); // tile
			uint16_t w = SlReadUint16();
			uint16_t h = SlReadUint16();
			if (w != 0) {
				SlSkipBytes((SlXvIsFeaturePresent(XSLFI_TOWN_CARGO_MATRIX) ? 8 : 4) * ((uint)(w / 4) * (uint)(h / 4)));
			}
		}
	}
}

/** Fix pointers when loading town data. */
static void Ptrs_TOWN()
{
	/* Don't run when savegame version lower than 161. */
	if (IsSavegameVersionBefore(SLV_161)) return;

	SetupDescs_TOWN();
	for (Town *t : Town::Iterate()) {
		SlObjectPtrOrNullFiltered(t, _filtered_town_desc);
	}
}

void SlResetTNNC()
{
	_town_noise_no_update = false;
	_town_zone_radii_no_update = false;
}

static_assert(std::tuple_size<decltype(TownCache::squared_town_zone_radius)>::value == HZB_END);

void Save_TNNC()
{
	assert(_sl_xv_feature_versions[XSLFI_TNNC_CHUNK] != 0);

	if (!IsNetworkServerSave()) {
		SlSetLength(0);
		return;
	}

	size_t length = 8 + (Town::GetNumItems() * 6);
	uint32_t flags = 0;

	if (IsGetTownZonesCallbackHandlerPresent()) {
		flags |= 1;
		length += Town::GetNumItems() * HZB_END * 4;
	}

	SlSetLength(length);

	SlWriteUint32(flags);
	SlWriteUint32((uint32_t)Town::GetNumItems());

	for (const Town *t : Town::Iterate()) {
		SlWriteUint32(t->index);
		SlWriteUint16(t->noise_reached);
		if (flags & 1) {
			for (uint i = 0; i < HZB_END; i++) {
				SlWriteUint32(t->cache.squared_town_zone_radius[i]);
			}
		}
	}
}

void Load_TNNC()
{
	if (SlGetFieldLength() == 0) return;

	if (!_networking || _network_server) {
		SlSkipBytes(SlGetFieldLength());
		return;
	}

	const uint32_t flags = SlReadUint32();
	const uint32_t count = SlReadUint32();

	_town_noise_no_update = true;
	_town_zone_radii_no_update = (flags & 1);

	for (uint32_t idx = 0; idx < count; idx++) {
		Town *t = Town::GetIfValid(SlReadUint32());
		if (t == nullptr) SlErrorCorrupt("TNNC: invalid town ID");
		t->noise_reached = SlReadUint16();
		if (flags & 1) {
			for (uint i = 0; i < HZB_END; i++) {
				t->cache.squared_town_zone_radius[i] = SlReadUint32();
			}
		}
	}
}

static ChunkSaveLoadSpecialOpResult Special_TNNC(uint32_t chunk_id, ChunkSaveLoadSpecialOp op)
{
	switch (op) {
		case CSLSO_SHOULD_SAVE_CHUNK:
			if (_sl_xv_feature_versions[XSLFI_TNNC_CHUNK] == 0) return CSLSOR_DONT_SAVE_CHUNK;
			break;

		default:
			break;
	}
	return CSLSOR_NONE;
}

/** Chunk handler for towns. */
static const ChunkHandler town_chunk_handlers[] = {
	{ 'HIDS', Save_HIDS, Load_HIDS, nullptr,   nullptr, CH_TABLE },
	{ 'CITY', Save_TOWN, Load_TOWN, Ptrs_TOWN, nullptr, CH_ARRAY },
	{ 'TNNC', Save_TNNC, Load_TNNC, nullptr,   nullptr, CH_RIFF,  Special_TNNC },
};

extern const ChunkHandlerTable _town_chunk_handlers(town_chunk_handlers);
