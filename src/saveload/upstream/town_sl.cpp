/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_sl.cpp Code handling saving and loading of towns and houses */

#include "../../stdafx.h"

#include "saveload.h"
#include "compat/town_sl_compat.h"

#include "newgrf_sl.h"
#include "../../newgrf_house.h"
#include "../../town.h"
#include "../../landscape.h"
#include "../../subsidy_func.h"
#include "../../strings_func.h"
#include "../../tilematrix_type.hpp"

#include "../../safeguards.h"

namespace upstream_sl {

typedef TileMatrix<CargoTypes, 4> AcceptanceMatrix;

class SlTownSupplied : public DefaultSaveLoadHandler<SlTownSupplied, Town> {
public:
	inline static const SaveLoad description[] = {
		SLE_CONDVAR(TransportedCargoStat<uint32>, old_max, SLE_UINT32, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint32>, new_max, SLE_UINT32, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint32>, old_act, SLE_UINT32, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint32>, new_act, SLE_UINT32, SLV_165, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _town_supplied_sl_compat;

	/**
	 * Get the number of cargoes used by this savegame version.
	 * @return The number of cargoes used by this savegame version.
	 */
	size_t GetNumCargo() const
	{
		if (IsSavegameVersionBefore(SLV_EXTEND_CARGOTYPES)) return 32;
		if (IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH)) return NUM_CARGO;
		/* Read from the savegame how long the list is. */
		return SlGetStructListLength(NUM_CARGO);
	}

	void Save(Town *t) const override
	{
		SlSetStructListLength(NUM_CARGO);
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			SlObject(&t->supplied[i], this->GetDescription());
		}
	}

	void Load(Town *t) const override
	{
		size_t num_cargo = this->GetNumCargo();
		for (size_t i = 0; i < num_cargo; i++) {
			SlObject(&t->supplied[i], this->GetLoadDescription());
		}
	}
};

class SlTownReceived : public DefaultSaveLoadHandler<SlTownReceived, Town> {
public:
	inline static const SaveLoad description[] = {
		SLE_CONDVAR(TransportedCargoStat<uint16>, old_max, SLE_UINT16, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint16>, new_max, SLE_UINT16, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint16>, old_act, SLE_UINT16, SLV_165, SL_MAX_VERSION),
		SLE_CONDVAR(TransportedCargoStat<uint16>, new_act, SLE_UINT16, SLV_165, SL_MAX_VERSION),
	};
	inline const static SaveLoadCompatTable compat_description = _town_received_sl_compat;

	void Save(Town *t) const override
	{
		SlSetStructListLength(NUM_TE);
		for (size_t i = TE_BEGIN; i < TE_END; i++) {
			SlObject(&t->received[i], this->GetDescription());
		}
	}

	void Load(Town *t) const override
	{
		size_t length = IsSavegameVersionBefore(SLV_SAVELOAD_LIST_LENGTH) ? (size_t)TE_END : SlGetStructListLength(TE_END);
		for (size_t i = 0; i < length; i++) {
			SlObject(&t->received[i], this->GetLoadDescription());
		}
	}
};

class SlTownAcceptanceMatrix : public DefaultSaveLoadHandler<SlTownAcceptanceMatrix, Town> {
public:
	inline static const SaveLoad description[] = {
		SLE_VAR(AcceptanceMatrix, area.tile, SLE_UINT32),
		SLE_VAR(AcceptanceMatrix, area.w,    SLE_UINT16),
		SLE_VAR(AcceptanceMatrix, area.h,    SLE_UINT16),
	};
	inline const static SaveLoadCompatTable compat_description = _town_acceptance_matrix_sl_compat;

	void Load(Town *t) const override
	{
		/* Discard now unused acceptance matrix. */
		AcceptanceMatrix dummy;
		SlObject(&dummy, this->GetLoadDescription());
		if (dummy.area.w != 0) {
			uint arr_len = dummy.area.w / AcceptanceMatrix::GRID * dummy.area.h / AcceptanceMatrix::GRID;
			SlSkipBytes(4 * arr_len);
		}
	}
};

static const SaveLoad _town_desc[] = {
	SLE_CONDVAR(Town, xy,                    SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_6),
	SLE_CONDVAR(Town, xy,                    SLE_UINT32,                 SLV_6, SL_MAX_VERSION),

	SLE_CONDVAR(Town, townnamegrfid,         SLE_UINT32, SLV_66, SL_MAX_VERSION),
	    SLE_VAR(Town, townnametype,          SLE_UINT16),
	    SLE_VAR(Town, townnameparts,         SLE_UINT32),
	SLE_CONDSTR(Town, name,                  SLE_STR | SLF_ALLOW_CONTROL, 0, SLV_84, SL_MAX_VERSION),

	    SLE_VAR(Town, flags,                 SLE_UINT8),
	SLE_CONDVAR(Town, statues,               SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_104),
	SLE_CONDVAR(Town, statues,               SLE_UINT16,               SLV_104, SL_MAX_VERSION),

	SLE_CONDVAR(Town, have_ratings,          SLE_FILE_U8  | SLE_VAR_U16, SL_MIN_VERSION, SLV_104),
	SLE_CONDVAR(Town, have_ratings,          SLE_UINT16,               SLV_104, SL_MAX_VERSION),
	SLE_CONDARR(Town, ratings,               SLE_INT16, 8,               SL_MIN_VERSION, SLV_104),
	SLE_CONDARR(Town, ratings,               SLE_INT16, MAX_COMPANIES, SLV_104, SL_MAX_VERSION),
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  8,               SLV_4, SLV_104),
	SLE_CONDARR(Town, unwanted,              SLE_INT8,  MAX_COMPANIES, SLV_104, SL_MAX_VERSION),

	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].old_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].old_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_MAIL].old_max,       SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_MAIL].old_max,       SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].new_max, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].new_max, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_MAIL].new_max,       SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_MAIL].new_max,       SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].old_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].old_act, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_MAIL].old_act,       SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_MAIL].old_act,       SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].new_act, SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_PASSENGERS].new_act, SLE_UINT32,                 SLV_9, SLV_165),
	SLE_CONDVAR(Town, supplied[CT_MAIL].new_act,       SLE_FILE_U16 | SLE_VAR_U32, SL_MIN_VERSION, SLV_9),
	SLE_CONDVAR(Town, supplied[CT_MAIL].new_act,       SLE_UINT32,                 SLV_9, SLV_165),

	SLE_CONDVAR(Town, received[TE_FOOD].old_act,       SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDVAR(Town, received[TE_WATER].old_act,      SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDVAR(Town, received[TE_FOOD].new_act,       SLE_UINT16,                 SL_MIN_VERSION, SLV_165),
	SLE_CONDVAR(Town, received[TE_WATER].new_act,      SLE_UINT16,                 SL_MIN_VERSION, SLV_165),

	SLE_CONDARR(Town, goal, SLE_UINT32, NUM_TE, SLV_165, SL_MAX_VERSION),

	SLE_CONDSSTR(Town, text,                 SLE_STR | SLF_ALLOW_CONTROL, SLV_168, SL_MAX_VERSION),

	SLE_CONDVAR(Town, time_until_rebuild,    SLE_FILE_U8 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_54),
	SLE_CONDVAR(Town, time_until_rebuild,    SLE_UINT16,                SLV_54, SL_MAX_VERSION),
	SLE_CONDVAR(Town, grow_counter,          SLE_FILE_U8 | SLE_VAR_U16,  SL_MIN_VERSION, SLV_54),
	SLE_CONDVAR(Town, grow_counter,          SLE_UINT16,                SLV_54, SL_MAX_VERSION),
	SLE_CONDVAR(Town, growth_rate,           SLE_FILE_U8 | SLE_VAR_I16,  SL_MIN_VERSION, SLV_54),
	SLE_CONDVAR(Town, growth_rate,           SLE_FILE_I16 | SLE_VAR_U16, SLV_54, SLV_165),
	SLE_CONDVAR(Town, growth_rate,           SLE_UINT16,                 SLV_165, SL_MAX_VERSION),

	    SLE_VAR(Town, fund_buildings_months, SLE_UINT8),
	    SLE_VAR(Town, road_build_months,     SLE_UINT8),

	SLE_CONDVAR(Town, exclusivity,           SLE_UINT8,                  SLV_2, SL_MAX_VERSION),
	SLE_CONDVAR(Town, exclusive_counter,     SLE_UINT8,                  SLV_2, SL_MAX_VERSION),

	SLE_CONDVAR(Town, larger_town,           SLE_BOOL,                  SLV_56, SL_MAX_VERSION),
	SLE_CONDVAR(Town, layout,                SLE_UINT8,                SLV_113, SL_MAX_VERSION),

	SLE_CONDREFLIST(Town, psa_list,          REF_STORAGE,              SLV_161, SL_MAX_VERSION),

	SLEG_CONDSTRUCTLIST("supplied", SlTownSupplied,                    SLV_165, SL_MAX_VERSION),
	SLEG_CONDSTRUCTLIST("received", SlTownReceived,                    SLV_165, SL_MAX_VERSION),
	SLEG_CONDSTRUCTLIST("acceptance_matrix", SlTownAcceptanceMatrix,   SLV_166, SLV_REMOVE_TOWN_CARGO_CACHE),
};

struct HIDSChunkHandler : NewGRFMappingChunkHandler {
	HIDSChunkHandler() : NewGRFMappingChunkHandler('HIDS', _house_mngr) {}
};

struct CITYChunkHandler : ChunkHandler {
	CITYChunkHandler() : ChunkHandler('CITY', CH_TABLE) {}

	void Save() const override
	{
		SlTableHeader(_town_desc);

		for (Town *t : Town::Iterate()) {
			SlSetArrayIndex(t->index);
			SlObject(t, _town_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_town_desc, _town_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			Town *t = new (index) Town();
			SlObject(t, slt);

			if (t->townnamegrfid == 0 && !IsInsideMM(t->townnametype, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_LAST + 1) && GetStringTab(t->townnametype) != TEXT_TAB_OLD_CUSTOM) {
				SlErrorCorrupt("Invalid town name generator");
			}
		}
	}

	void FixPointers() const override
	{
		if (IsSavegameVersionBefore(SLV_161)) return;

		for (Town *t : Town::Iterate()) {
			SlObject(t, _town_desc);
		}
	}
};

static const HIDSChunkHandler HIDS;
static const CITYChunkHandler CITY;
static const ChunkHandlerRef town_chunk_handlers[] = {
	HIDS,
	CITY,
};

extern const ChunkHandlerTable _town_chunk_handlers(town_chunk_handlers);

}
