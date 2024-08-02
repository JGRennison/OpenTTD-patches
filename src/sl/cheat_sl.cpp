/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cheat_sl.cpp Code handling saving and loading of cheats */

#include "../stdafx.h"
#include "../cheat_type.h"
#include "../debug.h"

#include "saveload.h"

#include <map>
#include <string>

#include "../safeguards.h"

extern std::map<std::string, bool> _unknown_cheat_fields; // This requires reference stability (during load)

struct ExtraCheatNameDesc {
	const char *name;
	Cheat *cht;
};

static ExtraCheatNameDesc _extra_cheat_descs[] = {
	{ "inflation_cost",   &_cheats.inflation_cost },
	{ "inflation_income", &_cheats.inflation_income },
	{ "station_rating",   &_cheats.station_rating },
	{ "town_rating",      &_cheats.town_rating },
};

std::vector<NamedSaveLoad> GetCheatsDesc(bool save) {
	static const NamedSaveLoad _cheats_desc[] = {
		NSL("magic_bulldozer.been_used",  SLE_VAR(Cheats, magic_bulldozer.been_used, SLE_BOOL)),
		NSL("magic_bulldozer.value",      SLE_VAR(Cheats, magic_bulldozer.value, SLE_BOOL)),
		NSL("switch_company.been_used",   SLE_VAR(Cheats, switch_company.been_used, SLE_BOOL)),
		NSL("switch_company.value",       SLE_VAR(Cheats, switch_company.value, SLE_BOOL)),
		NSL("money.been_used",            SLE_VAR(Cheats, money.been_used, SLE_BOOL)),
		NSL("money.value",                SLE_VAR(Cheats, money.value, SLE_BOOL)),
		NSL("crossing_tunnels.been_used", SLE_VAR(Cheats, crossing_tunnels.been_used, SLE_BOOL)),
		NSL("crossing_tunnels.value",     SLE_VAR(Cheats, crossing_tunnels.value, SLE_BOOL)),
		NSL("", SLE_NULL(1)),
		NSL("", SLE_NULL(1)), // Needs to be two NULL fields. See Load_CHTS().
		NSL("no_jetcrash.been_used",      SLE_VAR(Cheats, no_jetcrash.been_used, SLE_BOOL)),
		NSL("no_jetcrash.value,",         SLE_VAR(Cheats, no_jetcrash.value, SLE_BOOL)),
		NSL("", SLE_NULL(1)),
		NSL("", SLE_NULL(1)), // Needs to be two NULL fields. See Load_CHTS().
		NSL("change_date.been_used",      SLE_VAR(Cheats, change_date.been_used, SLE_BOOL)),
		NSL("change_date.value",          SLE_VAR(Cheats, change_date.value, SLE_BOOL)),
		NSL("setup_prod.been_used",       SLE_VAR(Cheats, setup_prod.been_used, SLE_BOOL)),
		NSL("setup_prod.value",           SLE_VAR(Cheats, setup_prod.value, SLE_BOOL)),
		NSL("", SLE_NULL(1)),
		NSL("", SLE_NULL(1)), // Needs to be two NULL fields. See Load_CHTS().
		NSL("edit_max_hl.been_used",      SLE_VAR(Cheats, edit_max_hl.been_used, SLE_BOOL)),
		NSL("edit_max_hl.value",          SLE_VAR(Cheats, edit_max_hl.value, SLE_BOOL)),
		NSLT("station_rating.been_used",  SLE_VAR(Cheats, station_rating.been_used, SLE_BOOL)),
		NSLT("station_rating.value",      SLE_VAR(Cheats, station_rating.value, SLE_BOOL)),
		NSLT("inflation_cost.been_used",  SLE_VAR(Cheats, inflation_cost.been_used, SLE_BOOL)),
		NSLT("inflation_cost.value",      SLE_VAR(Cheats, inflation_cost.value, SLE_BOOL)),
		NSLT("inflation_income.been_used",SLE_VAR(Cheats, inflation_income.been_used, SLE_BOOL)),
		NSLT("inflation_income.value",    SLE_VAR(Cheats, inflation_income.value, SLE_BOOL)),
		NSLT("town_rating.been_used",     SLE_VAR(Cheats, town_rating.been_used, SLE_BOOL)),
		NSLT("town_rating.value",         SLE_VAR(Cheats, town_rating.value, SLE_BOOL)),
	};

	std::vector<NamedSaveLoad> desc(std::begin(_cheats_desc), std::end(_cheats_desc));
	if (save) {
		for (auto &it : _unknown_cheat_fields) {
			desc.push_back(NSLT(it.first.c_str(), SLEG_VAR(it.second, SLE_BOOL)));
		}
	}
	return desc;
}

/**
 * Save the cheat values.
 */
static void Save_CHTS()
{
	SaveLoadTableData slt = SlTableHeader(GetCheatsDesc(true));

	SlSetArrayIndex(0);
	SlObjectSaveFiltered(&_cheats, slt);
}

/**
 * Load the cheat values.
 */
static void Load_CHTS()
{
	if (SlIsTableChunk()) {
		struct UnknownCheatHandler : public TableHeaderSpecialHandler {
			bool MissingField(const std::string &key, uint8_t type, std::vector<SaveLoad> &saveloads) override {
				if (type == SLE_FILE_I8) {
					DEBUG(sl, 1, "CHTS chunk: Unknown cheat field: '%s'", key.c_str());
					saveloads.push_back(SLEG_VAR(_unknown_cheat_fields[key], SLE_BOOL));
					return true;
				}

				return false;
			}
		};

		UnknownCheatHandler uch{};
		SaveLoadTableData slt = SlTableHeader(GetCheatsDesc(false), &uch);

		SlLoadTableObjectChunk(slt, &_cheats);
	} else {
		size_t count = SlGetFieldLength();
		SaveLoadTableData slt = SlTableHeaderOrRiff(GetCheatsDesc(false));

		/* Cheats were added over the years without a savegame bump. They are
		 * stored as 2 SLE_BOOLs per entry. "count" indicates how many SLE_BOOLs
		 * are stored for this savegame. So read only "count" SLE_BOOLs (and in
		 * result "count / 2" cheats). */
		if (count < slt.size()) slt.resize(count);

		SlObject(&_cheats, slt);
	}
}

/**
 * Load the extra cheat values.
 */
static void Load_CHTX()
{
	struct CheatsExtLoad {
		char name[256];
		Cheat cht;
	};

	static const SaveLoad _cheats_ext_load_desc[] = {
		SLE_STR(CheatsExtLoad, name,           SLE_STRB, 256),
		SLE_VAR(CheatsExtLoad, cht.been_used,  SLE_BOOL),
		SLE_VAR(CheatsExtLoad, cht.value,      SLE_BOOL),
	};

	CheatsExtLoad current_cheat;

	uint32_t chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("CHTX chunk: unknown chunk header flags: 0x%X", chunk_flags);

	uint32_t cheat_count = SlReadUint32();
	for (uint32_t i = 0; i < cheat_count; i++) {
		SlObject(&current_cheat, _cheats_ext_load_desc);

		bool found = false;
		for (uint j = 0; j < lengthof(_extra_cheat_descs); j++) {
			const ExtraCheatNameDesc &desc = _extra_cheat_descs[j];
			if (strcmp(desc.name, current_cheat.name) == 0) {
				*(desc.cht) = current_cheat.cht;
				found = true;
				break;
			}
		}
		if (!found) {
			DEBUG(sl, 1, "CHTX chunk: Could not find cheat: '%s'", current_cheat.name);
			_unknown_cheat_fields[std::string(current_cheat.name) + ".been_used"] = current_cheat.cht.been_used;
			_unknown_cheat_fields[std::string(current_cheat.name) + ".value"] = current_cheat.cht.value;
		}
	}
}

/** Chunk handlers related to cheats. */
static const ChunkHandler cheat_chunk_handlers[] = {
	{ 'CHTS', Save_CHTS, Load_CHTS, nullptr, nullptr, CH_TABLE },
	{ 'CHTX', nullptr,   Load_CHTX, nullptr, nullptr, CH_READONLY },
};

extern const ChunkHandlerTable _cheat_chunk_handlers(cheat_chunk_handlers);
