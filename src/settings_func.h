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

struct IniFile;

void IConsoleSetSetting(const char *name, const char *value, bool force_newgame = false);
void IConsoleSetSetting(const char *name, int32 value);
void IConsoleGetSetting(const char *name, bool force_newgame = false);
void IConsoleListSettings(const char *prefilter, bool show_defaults);

void LoadFromConfig(bool minimal = false);

enum SaveToConfigFlags : uint32 {
	STCF_NONE = 0,
	STCF_GENERIC = 1 << 0,
	STCF_PRIVATE = 1 << 1,
	STCF_SECRETS = 1 << 2,
	STCF_ALL     = STCF_GENERIC | STCF_PRIVATE | STCF_SECRETS,
};
DECLARE_ENUM_AS_BIT_SET(SaveToConfigFlags)

void SaveToConfig(SaveToConfigFlags flags);

void IniLoadWindowSettings(IniFile &ini, const char *grpname, void *desc);
void IniSaveWindowSettings(IniFile &ini, const char *grpname, void *desc);

StringList GetGRFPresetList();
struct GRFConfig *LoadGRFPresetFromConfig(const char *config_name);
void SaveGRFPresetToConfig(const char *config_name, struct GRFConfig *config);
void DeleteGRFPresetFromConfig(const char *config_name);

void SetDefaultCompanySettings(CompanyID cid);

void SyncCompanySettings();

void SetupTimeSettings();

const char *GetSettingNameByIndex(uint32 idx);
const char *GetCompanySettingNameByIndex(uint32 idx);

#endif /* SETTINGS_FUNC_H */
