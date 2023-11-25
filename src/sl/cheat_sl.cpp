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

extern std::map<std::string, Cheat> _unknown_cheats;

struct ExtraCheatNameDesc {
	const char *name;
	Cheat *cht;
};

static ExtraCheatNameDesc _extra_cheat_descs[] = {
	{ "inflation_cost",   &_extra_cheats.inflation_cost },
	{ "inflation_income", &_extra_cheats.inflation_income },
	{ "station_rating",   &_cheats.station_rating },
	{ "town_rating",      &_extra_cheats.town_rating },
};

static const SaveLoad _cheats_desc[] = {
	SLE_VAR(Cheats, magic_bulldozer.been_used, SLE_BOOL),
	SLE_VAR(Cheats, magic_bulldozer.value, SLE_BOOL),
	SLE_VAR(Cheats, switch_company.been_used, SLE_BOOL),
	SLE_VAR(Cheats, switch_company.value, SLE_BOOL),
	SLE_VAR(Cheats, money.been_used, SLE_BOOL),
	SLE_VAR(Cheats, money.value, SLE_BOOL),
	SLE_VAR(Cheats, crossing_tunnels.been_used, SLE_BOOL),
	SLE_VAR(Cheats, crossing_tunnels.value, SLE_BOOL),
	SLE_NULL(1),
	SLE_NULL(1), // Needs to be two NULL fields. See Load_CHTS().
	SLE_VAR(Cheats, no_jetcrash.been_used, SLE_BOOL),
	SLE_VAR(Cheats, no_jetcrash.value, SLE_BOOL),
	SLE_NULL(1),
	SLE_NULL(1), // Needs to be two NULL fields. See Load_CHTS().
	SLE_VAR(Cheats, change_date.been_used, SLE_BOOL),
	SLE_VAR(Cheats, change_date.value, SLE_BOOL),
	SLE_VAR(Cheats, setup_prod.been_used, SLE_BOOL),
	SLE_VAR(Cheats, setup_prod.value, SLE_BOOL),
	SLE_NULL(1),
	SLE_NULL(1), // Needs to be two NULL fields. See Load_CHTS().
	SLE_VAR(Cheats, edit_max_hl.been_used, SLE_BOOL),
	SLE_VAR(Cheats, edit_max_hl.value, SLE_BOOL),
};

/**
 * Save the cheat values.
 */
static void Save_CHTS()
{
	SlSetLength(std::size(_cheats_desc));
	SlObject(&_cheats, _cheats_desc);
}

/**
 * Load the cheat values.
 */
static void Load_CHTS()
{
	size_t count = SlGetFieldLength();
	std::vector<SaveLoad> slt;

	/* Cheats were added over the years without a savegame bump. They are
	 * stored as 2 SLE_BOOLs per entry. "count" indicates how many SLE_BOOLs
	 * are stored for this savegame. So read only "count" SLE_BOOLs (and in
	 * result "count / 2" cheats). */
	for (auto &sld : _cheats_desc) {
		count--;
		slt.push_back(sld);

		if (count == 0) break;
	}

	SlObject(&_cheats, slt);
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

	uint32 chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("CHTX chunk: unknown chunk header flags: 0x%X", chunk_flags);

	uint32 cheat_count = SlReadUint32();
	for (uint32 i = 0; i < cheat_count; i++) {
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
			_unknown_cheats[current_cheat.name] = current_cheat.cht;
		}
	}
}

/**
 * Save the extra cheat values.
 */
static void Save_CHTX()
{
	struct CheatsExtSave {
		const char *name;
		Cheat cht;
	};

	static const SaveLoad _cheats_ext_save_desc[] = {
		SLE_STR(CheatsExtSave, name,           SLE_STR, 0),
		SLE_VAR(CheatsExtSave, cht.been_used,  SLE_BOOL),
		SLE_VAR(CheatsExtSave, cht.value,      SLE_BOOL),
	};

	SlAutolength([](void *) {
		SlWriteUint32(0);                                                               // flags
		SlWriteUint32((uint32)(lengthof(_extra_cheat_descs) + _unknown_cheats.size())); // cheat count

		for (uint j = 0; j < lengthof(_extra_cheat_descs); j++) {
			CheatsExtSave save = { _extra_cheat_descs[j].name, *(_extra_cheat_descs[j].cht) };
			SlObject(&save, _cheats_ext_save_desc);
		}
		for (const auto &iter : _unknown_cheats) {
			CheatsExtSave save = { iter.first.c_str(), iter.second };
			SlObject(&save, _cheats_ext_save_desc);
		}
	}, nullptr);
}

/**
 * Internal structure used in SaveSettingsPatx() and SaveSettingsPlyx()
 */
struct SettingsExtSave {
	uint32 flags;
	const char *name;
	uint32 setting_length;
};

static const SaveLoad _settings_ext_save_desc[] = {
	SLE_VAR(SettingsExtSave, flags,          SLE_UINT32),
	SLE_STR(SettingsExtSave, name,           SLE_STR, 0),
	SLE_VAR(SettingsExtSave, setting_length, SLE_UINT32),
};


/** Chunk handlers related to cheats. */
static const ChunkHandler cheat_chunk_handlers[] = {
	{ 'CHTS', Save_CHTS, Load_CHTS, nullptr, nullptr, CH_RIFF },
	{ 'CHTX', Save_CHTX, Load_CHTX, nullptr, nullptr, CH_RIFF },
};

extern const ChunkHandlerTable _cheat_chunk_handlers(cheat_chunk_handlers);
