/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_sl.cpp Handles the saveload part of the settings. */

#include "../stdafx.h"

#include "saveload.h"
#include "debug.h"
#include "compat/settings_sl_compat.h"

#include "../settings_type.h"
#include "../settings_internal.h"
#include "../network/network.h"
#include "../fios.h"
#include "../load_check.h"

#include "../safeguards.h"

const SettingTable &GetSettingsTableInternal();

namespace upstream_sl {

/**
 * Get the SaveLoad description for the SettingTable.
 * @param settings SettingDesc struct containing all information.
 * @param is_loading True iff the SaveLoad table is for loading.
 * @return Vector with SaveLoad entries for the SettingTable.
 */
static std::vector<SaveLoad> GetSettingsDesc(bool is_loading)
{
	std::vector<SaveLoad> saveloads;
	for (auto &sd : GetSettingsTableInternal()) {
		if (sd->flags & SF_NOT_IN_SAVE) continue;
		if (is_loading && !SlXvIsFeaturePresent(XSLFI_TABLE_PATS) && (sd->flags & SF_PATCH)) continue;
		if (!sd->save.ext_feature_test.IsFeaturePresent(_sl_version, sd->save.version_from, sd->save.version_to)) continue;

		VarType new_type = 0;
		switch (sd->save.conv & 0x0F) {
			case ::SLE_FILE_I8:
				new_type |= SLE_FILE_I8;
				break;
			case ::SLE_FILE_U8:
				new_type |= SLE_FILE_U8;
				break;
			case ::SLE_FILE_I16:
				new_type |= SLE_FILE_I16;
				break;
			case ::SLE_FILE_U16:
				new_type |= SLE_FILE_U16;
				break;
			case ::SLE_FILE_I32:
				new_type |= SLE_FILE_I32;
				break;
			case ::SLE_FILE_U32:
				new_type |= SLE_FILE_U32;
				break;
			case ::SLE_FILE_I64:
				new_type |= SLE_FILE_I64;
				break;
			case ::SLE_FILE_U64:
				new_type |= SLE_FILE_U64;
				break;
			case ::SLE_FILE_STRINGID:
				new_type |= SLE_FILE_STRINGID;
				break;
			case ::SLE_FILE_STRING:
				new_type |= SLE_FILE_STRING;
				break;
			default:
				error("Unexpected save conv for %s: 0x%02X", sd->name, sd->save.conv);
		}
		switch (sd->save.conv & 0xF0) {
			case ::SLE_VAR_BL:
				new_type |= SLE_VAR_BL;
				break;
			case ::SLE_VAR_I8:
				new_type |= SLE_VAR_I8;
				break;
			case ::SLE_VAR_U8:
				new_type |= SLE_VAR_U8;
				break;
			case ::SLE_VAR_I16:
				new_type |= SLE_VAR_I16;
				break;
			case ::SLE_VAR_U16:
				new_type |= SLE_VAR_U16;
				break;
			case ::SLE_VAR_I32:
				new_type |= SLE_VAR_I32;
				break;
			case ::SLE_VAR_U32:
				new_type |= SLE_VAR_U32;
				break;
			case ::SLE_VAR_I64:
				new_type |= SLE_VAR_I64;
				break;
			case ::SLE_VAR_U64:
				new_type |= SLE_VAR_U64;
				break;
			case ::SLE_VAR_NULL:
				new_type |= SLE_VAR_NULL;
				break;
			case ::SLE_VAR_STRB:
				new_type |= SLE_VAR_STRB;
				break;
			case ::SLE_VAR_STR:
				new_type |= SLE_VAR_STR;
				break;
			case ::SLE_VAR_STRQ:
				new_type |= SLE_VAR_STRQ;
				break;
			default:
				error("Unexpected save conv for %s: 0x%02X", sd->name, sd->save.conv);
		}

		/* economy.town_growth_rate is int8 here, but uint8 in upstream saves */
		if (is_loading && !SlXvIsFeaturePresent(XSLFI_TABLE_PATS) && strcmp(sd->name, "economy.town_growth_rate") == 0) {
			SB(new_type, 0, 4, SLE_FILE_U8);
		}

		SaveLoadType new_cmd;
		switch (sd->save.cmd) {
			case ::SL_VAR:
				new_cmd = SL_VAR;
				break;
			case ::SL_STR:
				new_cmd = SL_STR;
				break;
			case ::SL_STDSTR:
				new_cmd = SL_STDSTR;
				break;
			default:
				error("Unexpected save cmd for %s: %u", sd->name, sd->save.cmd);
		}

		if (is_loading && (sd->flags & SF_NO_NETWORK_SYNC) && _networking && !_network_server) {
			if (IsSavegameVersionBefore(SLV_TABLE_CHUNKS)) {
				/* We don't want to read this setting, so we do need to skip over it. */
				saveloads.push_back({sd->name, new_cmd, GetVarFileType(new_type) | SLE_VAR_NULL, sd->save.length, SL_MIN_VERSION, SL_MAX_VERSION, 0, nullptr, 0, nullptr});
			}
			continue;
		}

		SaveLoadAddrProc *address_proc = [](void *base, size_t extra) -> void* {
			return const_cast<byte *>((const byte *)base + (ptrdiff_t)extra);
		};
		saveloads.push_back({sd->name, new_cmd, new_type, sd->save.length, SL_MIN_VERSION, SL_MAX_VERSION, sd->save.size, address_proc, reinterpret_cast<uintptr_t>(sd->save.address), nullptr});
	}

	return saveloads;
}

/**
 * Save and load handler for settings
 * @param settings SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettings(void *object, const SaveLoadCompatTable &slct)
{
	const std::vector<SaveLoad> slt = SlCompatTableHeader(GetSettingsDesc(true), slct);

	if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() == -1) return;
	SlObject(object, slt);
	if (!IsSavegameVersionBefore(SLV_RIFF_TO_ARRAY) && SlIterateArray() != -1) SlErrorCorrupt("Too many settings entries");

	/* Ensure all IntSettings are valid (min/max could have changed between versions etc). */
	for (auto &sd : GetSettingsTableInternal()) {
		if (sd->flags & SF_NOT_IN_SAVE) continue;
		if ((sd->flags & SF_NO_NETWORK_SYNC) && _networking && !_network_server) continue;
		if (!sd->save.ext_feature_test.IsFeaturePresent(_sl_xv_feature_static_versions, MAX_LOAD_SAVEGAME_VERSION, sd->save.version_from, sd->save.version_to)) continue;

		if (sd->IsIntSetting()) {
			const IntSettingDesc *int_setting = sd->AsIntSetting();
			int_setting->MakeValueValidAndWrite(object, int_setting->Read(object));
		}
	}
}

/**
 * Save and load handler for settings
 * @param settings SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void SaveSettings(void *object)
{
	const std::vector<SaveLoad> slt = GetSettingsDesc(false);

	SlTableHeader(slt);

	SlSetArrayIndex(0);
	SlObject(object, slt);
}

struct PATSChunkHandler : ChunkHandler {
	PATSChunkHandler() : ChunkHandler('PATS', CH_TABLE) {}

	void Load() const override
	{
		/* Copy over default setting since some might not get loaded in
		 * a networking environment. This ensures for example that the local
		 * currency setting stays when joining a network-server */
		LoadSettings(&_settings_game, _settings_sl_compat);
	}

	void LoadCheck(size_t) const override
	{
		LoadSettings(&_load_check_data.settings, _settings_sl_compat);
	}

	void Save() const override
	{
		SaveSettings(&_settings_game);
	}
};

static const PATSChunkHandler PATS;
static const ChunkHandlerRef setting_chunk_handlers[] = {
	PATS,
};

extern const ChunkHandlerTable _setting_chunk_handlers(setting_chunk_handlers);

}
