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
};

/**
 * Save the cheat values.
 */
static void Save_CHTS()
{
	/* Cannot use lengthof because _cheats is of type Cheats, not Cheat */
	byte count = sizeof(_cheats) / sizeof(Cheat);
	Cheat *cht = (Cheat*) &_cheats;
	Cheat *cht_last = &cht[count];

	SlSetLength(count * 2);
	for (; cht != cht_last; cht++) {
		SlWriteByte(cht->been_used);
		SlWriteByte(cht->value);
	}
}

/**
 * Load the cheat values.
 */
static void Load_CHTS()
{
	Cheat *cht = (Cheat*)&_cheats;
	size_t count = SlGetFieldLength() / 2;
	/* Cannot use lengthof because _cheats is of type Cheats, not Cheat */
	if (count > sizeof(_cheats) / sizeof(Cheat)) SlErrorCorrupt("Too many cheat values");

	for (uint i = 0; i < count; i++) {
		cht[i].been_used = (SlReadByte() != 0);
		cht[i].value     = (SlReadByte() != 0);
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
		SLE_END()
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
		SLE_END()
	};

	SlAutolength([](void *) {
		SlWriteUint32(0);                                                     // flags
		SlWriteUint32(lengthof(_extra_cheat_descs) + _unknown_cheats.size()); // cheat count

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
	SLE_END()
};


/** Chunk handlers related to cheats. */
extern const ChunkHandler _cheat_chunk_handlers[] = {
	{ 'CHTS', Save_CHTS, Load_CHTS, nullptr, nullptr, CH_RIFF},
	{ 'CHTX', Save_CHTX, Load_CHTX, nullptr, nullptr, CH_RIFF | CH_LAST},
};
