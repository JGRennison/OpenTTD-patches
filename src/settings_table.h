/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file settings_table.h Definition of the configuration tables of the settings. */

#ifndef SETTINGS_TABLE_H
#define SETTINGS_TABLE_H

#include "settings_internal.h"

#include <array>

extern const SettingTable _company_settings;
extern const SettingTable _currency_settings;
extern const SettingTable _difficulty_settings;
extern const SettingTable _economy_settings;
extern const SettingTable _game_settings;
extern const SettingTable _gui_settings;
extern const SettingTable _linkgraph_settings;
extern const SettingTable _locale_settings;
extern const SettingTable _misc_settings;
extern const SettingTable _multimedia_settings;
extern const SettingTable _network_private_settings;
extern const SettingTable _network_secrets_settings;
extern const SettingTable _network_settings;
extern const SettingTable _news_display_settings;
extern const SettingTable _old_gameopt_settings;
extern const SettingTable _pathfinding_settings;
extern const SettingTable _scenario_settings;
extern const SettingTable _script_settings;
extern const SettingTable _window_settings;
extern const SettingTable _world_settings;

#if defined(_WIN32) && !defined(DEDICATED)
extern const SettingTable _win32_settings;
#endif

extern const std::initializer_list<SettingTable> _generic_setting_tables;
extern const std::initializer_list<SettingTable> _saveload_setting_tables;
extern const std::initializer_list<SettingTable> _private_setting_tables;
extern const std::initializer_list<SettingTable> _secrets_setting_tables;

extern const std::initializer_list<SettingsCompat> _gameopt_compat;
extern const std::initializer_list<SettingsCompat> _settings_compat;

static constexpr uint GAME_DIFFICULTY_NUM = 18;
extern const std::array<std::string, GAME_DIFFICULTY_NUM> _old_diff_settings;
extern std::array<uint16_t, GAME_DIFFICULTY_NUM> _old_diff_custom;

#endif /* SETTINGS_TABLE_H */
