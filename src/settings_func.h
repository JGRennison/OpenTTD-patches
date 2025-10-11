/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_func.h Functions related to setting/changing the settings. */

#ifndef SETTINGS_FUNC_H
#define SETTINGS_FUNC_H

#include "company_type.h"
#include "string_type.h"
#include "newgrf_config.h"

struct IniFile;

void IConsoleSetSetting(std::string_view name, std::string_view value, bool force_newgame = false);
void IConsoleSetSetting(std::string_view name, int32_t value);
void IConsoleGetSetting(std::string_view name, bool force_newgame = false);
void IConsoleListSettings(std::string_view prefilter, bool show_defaults);

void LoadFromConfig(bool minimal = false);

enum SaveToConfigFlags : uint32_t {
	STCF_NONE = 0,
	STCF_GENERIC = 1 << 0,
	STCF_PRIVATE = 1 << 1,
	STCF_SECRETS = 1 << 2,
	STCF_FAVS    = 1 << 3,
	STCF_ALL     = STCF_GENERIC | STCF_PRIVATE | STCF_SECRETS | STCF_FAVS,
};
DECLARE_ENUM_AS_BIT_SET(SaveToConfigFlags)

void SaveToConfig(SaveToConfigFlags flags);

void IniLoadWindowSettings(IniFile &ini, std::string_view grpname, struct WindowDescPreferences *desc);
void IniSaveWindowSettings(IniFile &ini, std::string_view grpname, struct WindowDescPreferences *desc);

StringList GetGRFPresetList();
GRFConfigList LoadGRFPresetFromConfig(std::string_view config_name);
void SaveGRFPresetToConfig(std::string_view config_name, GRFConfigList &config);
void DeleteGRFPresetFromConfig(std::string_view config_name);

void SetDefaultCompanySettings(CompanyID cid);

void SyncCompanySettings();

void SetupTimeSettings();

const char *GetCompanySettingNameByIndex(uint32_t idx);

#endif /* SETTINGS_FUNC_H */
