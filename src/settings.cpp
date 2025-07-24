/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file settings.cpp
 * All actions handling saving and loading of the settings/configuration goes on in this file.
 * The file consists of three parts:
 * <ol>
 * <li>Parsing the configuration file (openttd.cfg). This is achieved with the ini_ functions which
 *     handle various types, such as normal 'key = value' pairs, lists and value combinations of
 *     lists, strings, integers, 'bit'-masks and element selections.
 * <li>Handle reading and writing to the setting-structures from inside the game either from
 *     the console for example or through the gui with CMD_ functions.
 * <li>Handle saving/loading of the PATS chunk inside the savegame.
 * </ol>
 * @see SettingDesc
 * @see SaveLoad
 */

#include "stdafx.h"
#include <array>
#include <limits>
#include "currency.h"
#include "screenshot.h"
#include "network/network.h"
#include "network/network_func.h"
#include "network/core/config.h"
#include "settings_internal.h"
#include "command_func.h"
#include "console_func.h"
#include "pathfinder/pathfinder_type.h"
#include "genworld.h"
#include "train.h"
#include "news_func.h"
#include "window_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "rev.h"
#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
#include "fontcache.h"
#endif
#include "textbuf_gui.h"
#include "rail_gui.h"
#include "elrail_func.h"
#include "error.h"
#include "town.h"
#include "video/video_driver.hpp"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "blitter/factory.hpp"
#include "base_media_base.h"
#include "gamelog.h"
#include "settings_cmd.h"
#include "settings_func.h"
#include "ini_type.h"
#include "ai/ai_config.hpp"
#include "ai/ai.hpp"
#include "game/game_config.hpp"
#include "game/game.hpp"
#include "ai/ai_instance.hpp"
#include "game/game_instance.hpp"
#include "ship.h"
#include "smallmap_gui.h"
#include "roadveh.h"
#include "newgrf_config.h"
#include "picker_func.h"
#include "fios.h"
#include "load_check.h"
#include "strings_func.h"
#include "string_func.h"
#include "debug.h"
#include "zoning.h"
#include "vehicle_func.h"
#include "scope_info.h"
#include "viewport_func.h"
#include "gui.h"
#include "statusbar_gui.h"
#include "graph_gui.h"
#include "string_func_extra.h"
#include "engine_override.h"
#include "core/backup_type.hpp"

#include "void_map.h"
#include "station_base.h"
#include "infrastructure_func.h"

#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
#define HAS_TRUETYPE_FONT
#endif

#include "sl/saveload.h"

#include "table/strings.h"
#include "table/settings.h"
#include "table/settings_compat.h"

#include <algorithm>
#include <vector>

#include "safeguards.h"

ClientSettings _settings_client;
GameSettings _settings_game;     ///< Game settings of a running game or the scenario editor.
GameSettings _settings_newgame;  ///< Game settings for new games (updated from the intro screen).
TimeSettings _settings_time; ///< The effective settings that are used for time display.
VehicleDefaultSettings _old_vds; ///< Used for loading default vehicles settings from old savegames.
std::string _config_file; ///< Configuration file of OpenTTD.
std::string _config_file_text;
std::string _private_file; ///< Private configuration file of OpenTTD.
std::string _secrets_file; ///< Secrets configuration file of OpenTTD.
std::string _favs_file; ///< Picker favourites configuration file of OpenTTD.

static ErrorList _settings_error_list; ///< Errors while loading minimal settings.

static bool _fallback_gui_zoom_max = false;


/**
 * List of all the generic setting tables.
 *
 * There are a few tables that are special and not processed like the rest:
 * - _currency_settings
 * - _misc_settings
 * - _company_settings
 * - _win32_settings
 * As such, they are not part of this list.
 */
static const SettingTable _generic_setting_tables[] = {
	_difficulty_settings,
	_economy_settings,
	_game_settings,
	_gui_settings,
	_linkgraph_settings,
	_locale_settings,
	_multimedia_settings,
	_network_settings,
	_news_display_settings,
	_pathfinding_settings,
	_script_settings,
	_world_settings,
	_scenario_settings,
};

/**
 * List of all the save/load (PATS/PATX) setting tables.
 */
static const std::initializer_list<SettingTable> _saveload_setting_tables{
	_difficulty_settings,
	_economy_settings,
	_game_settings,
	_linkgraph_settings,
	_locale_settings,
	_pathfinding_settings,
	_script_settings,
	_world_settings,
};

void IterateSettingsTables(std::function<void(const SettingTable &, void *)> handler)
{
	handler(_misc_settings, nullptr);
#if defined(_WIN32) && !defined(DEDICATED)
	handler(_win32_settings, nullptr);
#endif
	for (auto &table : _generic_setting_tables) {
		handler(table, &_settings_game);
	}
	handler(_currency_settings, &GetCustomCurrency());
	handler(_company_settings, &_settings_client.company);
}

/**
 * List of all the private setting tables.
 */
static const SettingTable _private_setting_tables[] = {
	_network_private_settings,
};

/**
 * List of all the secrets setting tables.
 */
static const SettingTable _secrets_setting_tables[] = {
	_network_secrets_settings,
};

typedef void SettingDescProc(IniFile &ini, const SettingTable &desc, const char *grpname, void *object, bool only_startup);
typedef void SettingDescProcList(IniFile &ini, const char *grpname, StringList &list);

static bool IsSignedVarMemType(VarType vt);


/**
 * IniFile to store a configuration.
 */
class ConfigIniFile : public IniFile {
private:
	inline static const IniGroupNameList list_group_names = {
		"bans",
		"newgrf",
		"servers",
		"server_bind_addresses",
		"server_authorized_keys",
		"rcon_authorized_keys",
		"admin_authorized_keys",
		"settings_authorized_keys",
	};

public:
	ConfigIniFile(const std::string &filename, std::string *save = nullptr) : IniFile(list_group_names)
	{
		this->LoadFromDisk(filename, NO_DIRECTORY, save);
	}
};

/**
 * Ini-file versions.
 *
 * Sometimes we move settings between different ini-files, as we need to know
 * when we have to load/remove it from the old versus reading it from the new
 * location. These versions assist with situations like that.
 */
enum IniFileVersion : uint32_t {
	IFV_0,                                                 ///< 0  All versions prior to introduction.
	IFV_PRIVATE_SECRETS,                                   ///< 1  PR#9298  Moving of settings from openttd.cfg to private.cfg / secrets.cfg.
	IFV_GAME_TYPE,                                         ///< 2  PR#9515  Convert server_advertise to server_game_type.
	IFV_LINKGRAPH_SECONDS,                                 ///< 3  PR#10610 Store linkgraph update intervals in seconds instead of days.
	IFV_NETWORK_PRIVATE_SETTINGS,                          ///< 4  PR#10762 Move use_relay_service to private settings.

	IFV_AUTOSAVE_RENAME,                                   ///< 5  PR#11143 Renamed values of autosave to be in minutes.
	IFV_RIGHT_CLICK_CLOSE,                                 ///< 6  PR#10204 Add alternative right click to close windows setting.
	IFV_REMOVE_GENERATION_SEED,                            ///< 7  PR#11927 Remove "generation_seed" from configuration.

	IFV_MAX_VERSION,       ///< Highest possible ini-file version.
};

const uint16_t INIFILE_VERSION = (IniFileVersion)(IFV_MAX_VERSION - 1); ///< Current ini-file version of OpenTTD.

/**
 * Find the index value of a ONEofMANY type in a string separated by |
 * @param str the current value of the setting for which a value needs found
 * @param len length of the string
 * @param many full domain of values the ONEofMANY setting can have
 * @return the integer index of the full-list, or SIZE_MAX if not found
 */
size_t OneOfManySettingDesc::ParseSingleValue(const char *str, size_t len, const std::vector<std::string> &many)
{
	/* check if it's an integer */
	if (isdigit(*str)) return std::strtoul(str, nullptr, 0);

	size_t idx = 0;
	for (auto one : many) {
		if (one.size() == len && strncmp(one.c_str(), str, len) == 0) return idx;
		idx++;
	}

	return SIZE_MAX;
}

/**
 * Find whether a string was a boolean true or a boolean false.
 *
 * @param str the current value of the setting for which a value needs found.
 * @return Either true/false, or nullopt if no boolean value found.
 */
std::optional<bool> BoolSettingDesc::ParseSingleValue(const char *str)
{
	if (strcmp(str, "true") == 0 || strcmp(str, "on") == 0 || strcmp(str, "1") == 0) return true;
	if (strcmp(str, "false") == 0 || strcmp(str, "off") == 0 || strcmp(str, "0") == 0) return false;

	return std::nullopt;
}

/**
 * Find the set-integer value MANYofMANY type in a string
 * @param many full domain of values the MANYofMANY setting can have
 * @param str the current string value of the setting, each individual
 * of separated by a whitespace,tab or | character
 * @return the 'fully' set integer, or SIZE_MAX if a set is not found
 */
static size_t LookupManyOfMany(const std::vector<std::string> &many, const char *str)
{
	const char *s;
	size_t r;
	size_t res = 0;

	for (;;) {
		/* skip "whitespace" */
		while (*str == ' ' || *str == '\t' || *str == '|') str++;
		if (*str == 0) break;

		s = str;
		while (*s != 0 && *s != ' ' && *s != '\t' && *s != '|') s++;

		r = OneOfManySettingDesc::ParseSingleValue(str, s - str, many);
		if (r == SIZE_MAX) return r;

		SetBit(res, (uint8_t)r); // value found, set it
		if (*s == 0) break;
		str = s + 1;
	}
	return res;
}

/**
 * Parse a string into a vector of uint32s.
 * @param p the string to be parsed. Each element in the list is separated by a comma or a space character
 * @return std::optional with a vector of parsed integers. The optional is empty upon an error.
 */
static std::optional<std::vector<uint32_t>> ParseIntList(const char *p)
{
	bool comma = false; // do we accept comma?
	std::vector<uint32_t> result;

	while (*p != '\0') {
		switch (*p) {
			case ',':
				/* Do not accept multiple commas between numbers */
				if (!comma) return std::nullopt;
				comma = false;
				[[fallthrough]];

			case ' ':
				p++;
				break;

			default: {
				char *end;
				unsigned long v = std::strtoul(p, &end, 0);
				if (p == end) return std::nullopt; // invalid character (not a number)

				result.push_back(ClampTo<uint32_t>(v));
				p = end; // first non-number
				comma = true; // we accept comma now
				break;
			}
		}
	}

	/* If we have read comma but no number after it, fail.
	 * We have read comma when (n != 0) and comma is not allowed */
	if (!result.empty() && !comma) return std::nullopt;

	return result;
}

/**
 * Load parsed string-values into an integer-array (intlist)
 * @param str the string that contains the values (and will be parsed)
 * @param array pointer to the integer-arrays that will be filled
 * @param nelems the number of elements the array holds.
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.)
 * @return return true on success and false on error
 */
static bool LoadIntList(const char *str, void *array, int nelems, VarType type)
{
	size_t elem_size = SlVarSize(type);
	if (str == nullptr) {
		memset(array, 0, nelems * elem_size);
		return true;
	}

	auto opt_items = ParseIntList(str);
	if (!opt_items.has_value() || opt_items->size() != (size_t)nelems) return false;

	char *p = static_cast<char *>(array);
	for (auto item : *opt_items) {
		WriteValue(p, type, item);
		p += elem_size;
	}
	return true;
}

/**
 * Convert an integer-array (intlist) to a string representation. Each value
 * is separated by a comma or a space character
 * @param buf The buffer to format into.
 * @param array pointer to the integer-arrays that is read from
 * @param nelems the number of elements the array holds.
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.)
 */
void ListSettingDesc::FormatValue(format_target &buf, const void *object) const
{
	const uint8_t *p = static_cast<const uint8_t *>(GetVariableAddress(object, this->save));
	int i, v = 0;

	for (i = 0; i != this->save.length; i++) {
		switch (GetVarMemType(this->save.conv)) {
			case SLE_VAR_BL:
			case SLE_VAR_I8:  v = *(const   int8_t *)p; p += 1; break;
			case SLE_VAR_U8:  v = *(const  uint8_t *)p; p += 1; break;
			case SLE_VAR_I16: v = *(const  int16_t *)p; p += 2; break;
			case SLE_VAR_U16: v = *(const uint16_t *)p; p += 2; break;
			case SLE_VAR_I32: v = *(const  int32_t *)p; p += 4; break;
			case SLE_VAR_U32: v = *(const uint32_t *)p; p += 4; break;
			default: NOT_REACHED();
		}
		if (i != 0) buf.push_back(',');
		if (IsSignedVarMemType(this->save.conv)) {
			buf.format("{}", (int)v);
		} else {
			buf.format("{}", (uint)v);
		}
	}
}

void OneOfManySettingDesc::FormatSingleValue(format_target &buf, uint id) const
{
	if (id >= this->many.size()) {
		buf.format("{}", id);
		return;
	}
	buf.append(this->many[id]);
}

void OneOfManySettingDesc::FormatIntValue(format_target &buf, uint32_t value) const
{
	this->FormatSingleValue(buf, value);
}

void ManyOfManySettingDesc::FormatIntValue(format_target &buf, uint32_t value) const
{
	uint bitmask = (uint)value;
	if (bitmask == 0) {
		return;
	}
	bool first = true;
	for (uint id : SetBitIterator(bitmask)) {
		if (!first) buf.push_back('|');
		this->FormatSingleValue(buf, id);
		first = false;
	}
}

/**
 * Convert a string representation (external) of an integer-like setting to an integer.
 * @param str Input string that will be parsed based on the type of desc.
 * @return The value from the parse string, or the default value of the setting.
 */
size_t IntSettingDesc::ParseValue(const char *str) const
{
	char *end;
	size_t val = std::strtoul(str, &end, 0);
	if (end == str) {
		if (this->flags.Test(SettingFlag::ConvertBoolToInt)) {
			if (strcmp(str, "true") == 0 || strcmp(str, "on") == 0) return 1;
			if (strcmp(str, "false") == 0 || strcmp(str, "off") == 0) return 0;
		}
		_settings_error_list.emplace_back(
			GetEncodedString(STR_CONFIG_ERROR),
			GetEncodedString(STR_CONFIG_ERROR_INVALID_VALUE, str, this->name));
		return this->GetDefaultValue();
	}
	if (*end != '\0') {
		_settings_error_list.emplace_back(
			GetEncodedString(STR_CONFIG_ERROR),
			GetEncodedString(STR_CONFIG_ERROR_TRAILING_CHARACTERS, this->name));
	}
	return val;
}

size_t OneOfManySettingDesc::ParseValue(const char *str) const
{
	size_t r = OneOfManySettingDesc::ParseSingleValue(str, strlen(str), this->many);
	/* if the first attempt of conversion from string to the appropriate value fails,
	 * look if we have defined a converter from old value to new value. */
	if (r == SIZE_MAX && this->many_cnvt != nullptr) r = this->many_cnvt(str);
	if (r != SIZE_MAX) return r; // and here goes converted value

	_settings_error_list.emplace_back(
		GetEncodedString(STR_CONFIG_ERROR),
		GetEncodedString(STR_CONFIG_ERROR_INVALID_VALUE, str, this->name));
	return this->GetDefaultValue();
}

size_t ManyOfManySettingDesc::ParseValue(const char *str) const
{
	size_t r = LookupManyOfMany(this->many, str);
	if (r != SIZE_MAX) return r;

	_settings_error_list.emplace_back(
		GetEncodedString(STR_CONFIG_ERROR),
		GetEncodedString(STR_CONFIG_ERROR_INVALID_VALUE, str, this->name));
	return this->GetDefaultValue();
}

size_t BoolSettingDesc::ParseValue(const char *str) const
{
	auto r = BoolSettingDesc::ParseSingleValue(str);
	if (r.has_value()) return *r;

	_settings_error_list.emplace_back(
		GetEncodedString(STR_CONFIG_ERROR),
		GetEncodedString(STR_CONFIG_ERROR_INVALID_VALUE, str, this->name));
	return this->GetDefaultValue();
}

static bool ValidateEnumSetting(const IntSettingDesc *sdb, int32_t &val)
{
	if (sdb->flags.Test(SettingFlag::EnumPreCallbackValidate) && sdb->pre_check != nullptr && !sdb->pre_check(val)) return false;
	for (const SettingDescEnumEntry *enumlist = sdb->enumlist; enumlist != nullptr && enumlist->str != STR_NULL; enumlist++) {
		if (enumlist->val == val) {
			return true;
		}
	}
	return false;
}

/**
 * Get the title of the setting.
 * The string should include a {STRING2} to show the current value.
 * @return The title string.
 */
StringID IntSettingDesc::GetTitle() const
{
	return this->get_title_cb != nullptr ? this->get_title_cb(*this) : this->str;
}

/**
 * Get the help text of the setting.
 * @return The requested help text.
 */
StringID IntSettingDesc::GetHelp() const
{
	StringID str = this->get_help_cb != nullptr ? this->get_help_cb(*this) : this->str_help;
	if (this->guiproc != nullptr) {
		SettingOnGuiCtrlData data;
		data.type = SOGCT_DESCRIPTION_TEXT;
		data.text = str;
		if (this->guiproc(data)) {
			str = data.text;
		}
	}
	return str;
}

/**
 * Get parameters for drawing the value of the setting.
 * @param value Setting value to set params for.
 */
std::pair<StringParameter, StringParameter> IntSettingDesc::GetValueParams(int32_t value) const
{
	if (this->get_value_params_cb != nullptr) {
		return this->get_value_params_cb(*this, value);
	}

	if (this->IsBoolSetting()) {
		return {value != 0 ? STR_CONFIG_SETTING_ON : STR_CONFIG_SETTING_OFF, {}};
	}

	if (this->flags.Test(SettingFlag::Enum)) {
		StringID str = STR_UNDEFINED;
		for (const SettingDescEnumEntry *enumlist = this->enumlist; enumlist != nullptr && enumlist->str != STR_NULL; enumlist++) {
			if (enumlist->val == value) {
				str = enumlist->str;
				break;
			}
		}
		return {str, value};
	}

	if (this->flags.Test(SettingFlag::GuiDropdown)) {
		auto [min_val, _] = this->GetRange();
		return {this->str_val - min_val + value, value};
	}

	return {this->str_val + ((value == 0 && this->flags.Test(SettingFlag::GuiZeroIsSpecial)) ? 1 : 0), value};
}

/**
 * Get the default value of the setting.
 * @return The default value.
 */
int32_t IntSettingDesc::GetDefaultValue() const
{
	return this->get_def_cb != nullptr ? this->get_def_cb(*this) : this->def;
}

/**
 * Get the min/max range for the setting.
 * @return The min/max range.
 */
std::tuple<int32_t, uint32_t> IntSettingDesc::GetRange() const
{
	return this->get_range_cb != nullptr ? this->get_range_cb(*this) : std::tuple(this->min, this->max);
}

/**
 * Make the value valid and then write it to the setting.
 * See #MakeValidValid and #Write for more details.
 * @param object The object the setting is to be saved in.
 * @param val Signed version of the new value.
 */
void IntSettingDesc::MakeValueValidAndWrite(const void *object, int32_t val) const
{
	this->MakeValueValid(val);
	this->Write(object, val);
}

/**
 * Make the value valid given the limitations of this setting.
 *
 * In the case of int settings this is ensuring the value is between the minimum and
 * maximum value, with a special case for 0 if SettingFlag::GuiZeroIsSpecial is set.
 * This is generally done by clamping the value so it is within the allowed value range.
 * However, for SettingFlag::GuiDropdown the default is used when the value is not valid.
 * @param val The value to make valid.
 */
void IntSettingDesc::MakeValueValid(int32_t &val) const
{
	auto [min_val, max_val] = this->GetRange();
	/* We need to take special care of the uint32_t type as we receive from the function
	 * a signed integer. While here also bail out on 64-bit settings as those are not
	 * supported. Unsigned 8 and 16-bit variables are safe since they fit into a signed
	 * 32-bit variable
	 * TODO: Support 64-bit settings/variables; requires 64 bit over command protocol! */
	switch (GetVarMemType(this->save.conv)) {
		case SLE_VAR_NULL: return;
		case SLE_VAR_BL:
		case SLE_VAR_I8:
		case SLE_VAR_U8:
		case SLE_VAR_I16:
		case SLE_VAR_U16:
		case SLE_VAR_I32: {
			/* Override the minimum value. No value below this->min, except special value 0 */
			if (!this->flags.Test(SettingFlag::GuiZeroIsSpecial) || val != 0) {
				if (this->flags.Test(SettingFlag::Enum)) {
					if (!ValidateEnumSetting(this, val)) val = GetDefaultValue();
				} else if (!this->flags.Test(SettingFlag::GuiDropdown)) {
					/* Clamp value-type setting to its valid range */
					val = Clamp(val, min_val, max_val);
				} else if (val < min_val || val > static_cast<int32_t>(max_val)) {
					/* Reset invalid discrete setting (where different values change gameplay) to its default value */
					val = this->GetDefaultValue();
				}
			}
			break;
		}
		case SLE_VAR_U32: {
			/* Override the minimum value. No value below this->min, except special value 0 */
			uint32_t uval = static_cast<uint32_t>(val);
			if (!this->flags.Test(SettingFlag::GuiZeroIsSpecial) || uval != 0) {
				if (this->flags.Test(SettingFlag::Enum)) {
					if (!ValidateEnumSetting(this, val)) {
						uval = (uint32_t)this->GetDefaultValue();
					} else {
						uval = (uint32_t)val;
					}
				} else if (!this->flags.Test(SettingFlag::GuiDropdown)) {
					/* Clamp value-type setting to its valid range */
					uval = ClampU(uval, min_val, max_val);
				} else if (uval < static_cast<uint32_t>(min_val) || uval > max_val) {
					/* Reset invalid discrete setting to its default value */
					uval = static_cast<uint32_t>(this->GetDefaultValue());
				}
			}
			val = static_cast<int32_t>(uval);
			return;
		}
		case SLE_VAR_I64:
		case SLE_VAR_U64:
		default: NOT_REACHED();
	}
}

/**
 * Set the value of a setting.
 * @param object The object the setting is to be saved in.
 * @param val Signed version of the new value.
 */
void IntSettingDesc::Write(const void *object, int32_t val) const
{
	void *ptr = GetVariableAddress(object, this->save);
	WriteValue(ptr, this->save.conv, (int64_t)val);
}

/**
 * Read the integer from the the actual setting.
 * @param object The object the setting is to be saved in.
 * @return The value of the saved integer.
 */
int32_t IntSettingDesc::Read(const void *object) const
{
	void *ptr = GetVariableAddress(object, this->save);
	return (int32_t)ReadValue(ptr, this->save.conv);
}

/**
 * Make the value valid given the limitations of this setting.
 *
 * In the case of string settings this is ensuring the string contains only accepted
 * Utf8 characters and is at most the maximum length defined in this setting.
 * @param str The string to make valid.
 */
void StringSettingDesc::MakeValueValid(std::string &str) const
{
	if (this->max_length == 0 || str.size() < this->max_length) return;

	/* In case a maximum length is imposed by the setting, the length
	 * includes the '\0' termination for network transfer purposes.
	 * Also ensure the string is valid after chopping of some bytes. */
	std::string stdstr(str, 0, this->max_length - 1);
	str.assign(StrMakeValid(stdstr, SVS_NONE));
}

/**
 * Write a string to the actual setting.
 * @param object The object the setting is to be saved in.
 * @param str The string to save.
 */
void StringSettingDesc::Write(const void *object, const std::string &str) const
{
	reinterpret_cast<std::string *>(GetVariableAddress(object, this->save))->assign(str);
}

/**
 * Read the string from the the actual setting.
 * @param object The object the setting is to be saved in.
 * @return The value of the saved string.
 */
const std::string &StringSettingDesc::Read(const void *object) const
{
	return *reinterpret_cast<std::string *>(GetVariableAddress(object, this->save));
}

static const char *GetSettingConfigName(const SettingDesc &sd)
{
	const char *name = sd.name;
	if (sd.guiproc != nullptr) {
		SettingOnGuiCtrlData data;
		data.type = SOGCT_CFG_NAME;
		data.str = name;
		if (sd.guiproc(data)) {
			name = data.str;
		}
	}
	return name;
}

/**
 * Load values from a group of an IniFile structure into the internal representation
 * @param ini pointer to IniFile structure that holds administrative information
 * @param settings_table table with SettingDesc structures whose internally pointed variables will
 *        be given values
 * @param grpname the group of the IniFile to search in for the new values
 * @param object pointer to the object been loaded
 * @param only_startup load only the startup settings set
 */
static void IniLoadSettings(IniFile &ini, const SettingTable &settings_table, const char *grpname, void *object, bool only_startup)
{
	const IniGroup *group;
	const IniGroup *group_def = ini.GetGroup(grpname);

	for (auto &sd : settings_table) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (sd->startup != only_startup) continue;
		const IniItem *item = nullptr;
		if (!sd->flags.Test(SettingFlag::NoNewgame)) {
			/* For settings.xx.yy load the settings from [xx] yy = ? */
			std::string s{ GetSettingConfigName(*sd) };
			auto sc = s.find('.');
			if (sc != std::string::npos) {
				group = ini.GetGroup(s.substr(0, sc));
				s = s.substr(sc + 1);
			} else {
				group = group_def;
			}

			if (group != nullptr) item = group->GetItem(s);
			if (item == nullptr && group != group_def && group_def != nullptr) {
				/* For settings.xx.yy load the settings from [settings] yy = ? in case the previous
				 * did not exist (e.g. loading old config files with a [settings] section */
				item = group_def->GetItem(s);
			}
			if (item == nullptr) {
				/* For settings.xx.zz.yy load the settings from [zz] yy = ? in case the previous
				 * did not exist (e.g. loading old config files with a [yapf] section */
				sc = s.find('.');
				if (sc != std::string::npos) {
					if (group = ini.GetGroup(s.substr(0, sc)); group != nullptr) item = group->GetItem(s.substr(sc + 1));
				}
			}
			if (group != nullptr && item == nullptr && sd->guiproc != nullptr) {
				SettingOnGuiCtrlData data;
				data.type = SOGCT_CFG_FALLBACK_NAME;
				if (sd->guiproc(data)) {
					item = group->GetItem(data.str);
				}
			}
		}

		sd->ParseValue(item, object);
	}
}

void IntSettingDesc::ParseValue(const IniItem *item, void *object) const
{
	size_t val = (item == nullptr) ? this->GetDefaultValue() : this->ParseValue(item->value.has_value() ? item->value->c_str() : "");
	this->MakeValueValidAndWrite(object, (int32_t)val);
}

void StringSettingDesc::ParseValue(const IniItem *item, void *object) const
{
	std::string str = (item == nullptr) ? this->def : item->value.value_or("");
	this->MakeValueValid(str);
	if (this->flags.Test(SettingFlag::RunCallbacksOnParse)) {
		if (this->pre_check != nullptr && !this->pre_check(str)) str = this->def;
		if (this->post_callback != nullptr) this->post_callback(str);
	}
	this->Write(object, str);
}

void ListSettingDesc::ParseValue(const IniItem *item, void *object) const
{
	const char *str = (item == nullptr) ? this->def : item->value.has_value() ? item->value->c_str() : nullptr;
	void *ptr = GetVariableAddress(object, this->save);
	if (!LoadIntList(str, ptr, this->save.length, GetVarMemType(this->save.conv))) {
		_settings_error_list.emplace_back(
			GetEncodedString(STR_CONFIG_ERROR),
			GetEncodedString(STR_CONFIG_ERROR_ARRAY, this->name));

		/* Use default */
		LoadIntList(this->def, ptr, this->save.length, GetVarMemType(this->save.conv));
	}
}

/**
 * Save the values of settings to the inifile.
 * @param ini pointer to IniFile structure
 * @param sd read-only SettingDesc structure which contains the unmodified,
 *        loaded values of the configuration file and various information about it
 * @param grpname holds the name of the group (eg. [network]) where these will be saved
 * @param object pointer to the object been saved
 * The function works as follows: for each item in the SettingDesc structure we
 * have a look if the value has changed since we started the game (the original
 * values are reloaded when saving). If settings indeed have changed, we get
 * these and save them.
 */
static void IniSaveSettings(IniFile &ini, const SettingTable &settings_table, const char *grpname, void *object, bool)
{
	IniGroup *group_def = nullptr, *group;

	for (auto &sd : settings_table) {
		/* If the setting is not saved to the configuration
		 * file, just continue with the next setting */
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (sd->flags.Test(SettingFlag::NotInConfig)) continue;
		if (sd->flags.Test(SettingFlag::NoNewgame)) continue;

		/* XXX - wtf is this?? (group override?) */
		std::string s{ GetSettingConfigName(*sd) };
		auto sc = s.find('.');
		if (sc != std::string::npos) {
			group = &ini.GetOrCreateGroup(s.substr(0, sc));
			s = s.substr(sc + 1);
		} else {
			if (group_def == nullptr) group_def = &ini.GetOrCreateGroup(grpname);
			group = group_def;
		}

		IniItem &item = group->GetOrCreateItem(s);

		if (!item.value.has_value() || !sd->IsSameValue(&item, object)) {
			/* Value has changed, get the new value and put it into a buffer */
			format_buffer buf;
			sd->FormatValue(buf, object);

			/* The value is different, that means we have to write it to the ini */
			item.value.emplace(buf);
		}
	}
}

void IntSettingDesc::FormatValue(format_target &buf, const void *object) const
{
	uint32_t i = (uint32_t)this->Read(object);
	this->FormatIntValue(buf, i);
}

void IntSettingDesc::FormatIntValue(format_target &buf, uint32_t value) const
{
	if (IsSignedVarMemType(this->save.conv)) {
		buf.format("{}", (int)value);
	} else {
		buf.format("{}", (uint)value);
	}
}

void BoolSettingDesc::FormatIntValue(format_target &buf, uint32_t value) const
{
	buf.append((value != 0) ? "true" : "false");
}

bool IntSettingDesc::IsSameValue(const IniItem *item, void *object) const
{
	int32_t item_value = (int32_t)this->ParseValue(item->value->c_str());
	int32_t object_value = this->Read(object);
	return item_value == object_value;
}

bool IntSettingDesc::IsDefaultValue(void *object) const
{
	int32_t object_value = this->Read(object);
	return this->GetDefaultValue() == object_value;
}

void IntSettingDesc::ResetToDefault(void *object) const
{
	this->Write(object, this->GetDefaultValue());
}

void StringSettingDesc::FormatValue(format_target &buf, const void *object) const
{
	const std::string &str = this->Read(object);
	switch (GetVarMemType(this->save.conv)) {
		case SLE_VAR_STR:
			buf.append(str);
			break;

		case SLE_VAR_STRQ:
			if (!str.empty()) {
				buf.format("\"{}\"", str);
			}
			break;

		default: NOT_REACHED();
	}
}

bool StringSettingDesc::IsSameValue(const IniItem *item, void *object) const
{
	/* The ini parsing removes the quotes, which are needed to retain the spaces in STRQs,
	 * so those values are always different in the parsed ini item than they should be. */
	if (GetVarMemType(this->save.conv) == SLE_VAR_STRQ) return false;

	const std::string &str = this->Read(object);
	return item->value->compare(str) == 0;
}

bool StringSettingDesc::IsDefaultValue(void *object) const
{
	const std::string &str = this->Read(object);
	return this->def == str;
}

void StringSettingDesc::ResetToDefault(void *object) const
{
	this->Write(object, this->def);
}

bool ListSettingDesc::IsSameValue(const IniItem *item, void *object) const
{
	/* Checking for equality is way more expensive than just writing the value. */
	return false;
}

bool ListSettingDesc::IsDefaultValue(void *) const
{
	/* Defaults of lists are often complicated, and hard to compare. */
	return false;
}

void ListSettingDesc::ResetToDefault(void *) const
{
	/* Resetting a list to default is not supported. */
	NOT_REACHED();
}

/**
 * Loads all items from a 'grpname' section into a list
 * The list parameter can be a nullptr pointer, in this case nothing will be
 * saved and a callback function should be defined that will take over the
 * list-handling and store the data itself somewhere.
 * @param ini IniFile handle to the ini file with the source data
 * @param grpname character string identifying the section-header of the ini file that will be parsed
 * @param list new list with entries of the given section
 */
static void IniLoadSettingList(IniFile &ini, const char *grpname, StringList &list)
{
	const IniGroup *group = ini.GetGroup(grpname);

	if (group == nullptr) return;

	list.clear();

	for (const IniItem &item : group->items) {
		if (!item.name.empty()) list.push_back(item.name);
	}
}

/**
 * Saves all items from a list into the 'grpname' section
 * The list parameter can be a nullptr pointer, in this case a callback function
 * should be defined that will provide the source data to be saved.
 * @param ini IniFile handle to the ini file where the destination data is saved
 * @param grpname character string identifying the section-header of the ini file
 * @param list pointer to an string(pointer) array that will be used as the
 *             source to be saved into the relevant ini section
 */
static void IniSaveSettingList(IniFile &ini, const char *grpname, StringList &list)
{
	IniGroup &group = ini.GetOrCreateGroup(grpname);
	group.Clear();

	for (const auto &iter : list) {
		group.GetOrCreateItem(iter).SetValue("");
	}
}

/**
 * Load a WindowDesc from config.
 * @param ini IniFile handle to the ini file with the source data
 * @param grpname character string identifying the section-header of the ini file that will be parsed
 * @param desc Destination WindowDescPreferences
 */
void IniLoadWindowSettings(IniFile &ini, const char *grpname, void *desc)
{
	IniLoadSettings(ini, _window_settings, grpname, desc, false);
}

/**
 * Save a WindowDesc to config.
 * @param ini IniFile handle to the ini file where the destination data is saved
 * @param grpname character string identifying the section-header of the ini file
 * @param desc Source WindowDescPreferences
 */
void IniSaveWindowSettings(IniFile &ini, const char *grpname, void *desc)
{
	IniSaveSettings(ini, _window_settings, grpname, desc, false);
}

/**
 * Check whether the setting is editable in the current gamemode.
 * @param do_command true if this is about checking a command from the server.
 * @return true if editable.
 */
bool SettingDesc::IsEditable(bool do_command) const
{
	if (!do_command && !this->flags.Test(SettingFlag::NoNetworkSync) && IsNonAdminNetworkClient() && !this->flags.Test(SettingFlag::PerCompany)) return false;
	if (do_command && this->flags.Test(SettingFlag::NoNetworkSync)) return false;
	if (this->flags.Test(SettingFlag::NetworkOnly) && !_networking && _game_mode != GM_MENU) return false;
	if (this->flags.Test(SettingFlag::NoNetwork) && _networking) return false;
	if (this->flags.Test(SettingFlag::NewgameOnly) &&
			(_game_mode == GM_NORMAL ||
			(_game_mode == GM_EDITOR && !this->flags.Test(SettingFlag::SceneditToo)))) return false;
	if (this->flags.Test(SettingFlag::SceneditOnly) && _game_mode != GM_EDITOR) return false;
	return true;
}

/**
 * Return the type of the setting.
 * @return type of setting
 */
SettingType SettingDesc::GetType() const
{
	if (this->flags.Test(SettingFlag::PerCompany)) return ST_COMPANY;
	return this->flags.Test(SettingFlag::NotInSave) ? ST_CLIENT : ST_GAME;
}

/**
 * Get the setting description of this setting as an integer setting.
 * @return The integer setting description.
 */
const IntSettingDesc *SettingDesc::AsIntSetting() const
{
	assert_msg(this->IsIntSetting(), "name: {}", this->name);
	return static_cast<const IntSettingDesc *>(this);
}

/**
 * Get the setting description of this setting as a string setting.
 * @return The string setting description.
 */
const StringSettingDesc *SettingDesc::AsStringSetting() const
{
	assert_msg(this->IsStringSetting(), "name: {}", this->name);
	return static_cast<const StringSettingDesc *>(this);
}

/* Begin - Callback Functions for the various settings. */

/** Switch setting title depending on wallclock setting */
static StringID SettingTitleWallclock(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str + 1 : sd.str;
}

/** Switch setting help depending on wallclock setting */
static StringID SettingHelpWallclock(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_help + 1 : sd.str_help;
}

/** Switch setting help depending on wallclock setting */
static StringID SettingHelpWallclockTriple(const IntSettingDesc &sd)
{
	return EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_help + ((GetGameSettings().economy.day_length_factor > 1) ? 2 : 1) : sd.str_help;
}

/** Setting values for velocity unit localisation */
static std::pair<StringParameter, StringParameter> SettingsValueVelocityUnit(const IntSettingDesc &, int32_t value)
{
	StringID val;
	switch (value) {
		case 0: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_IMPERIAL; break;
		case 1: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_METRIC; break;
		case 2: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_SI; break;
		case 3: val = EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_GAMEUNITS_SECS : STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_GAMEUNITS_DAYS; break;
		case 4: val = STR_CONFIG_SETTING_LOCALISATION_UNITS_VELOCITY_KNOTS; break;
		default: NOT_REACHED();
	}
	return {val, {}};
}

/** A negative value has another string (the one after "strval"). */
static std::pair<StringParameter, StringParameter> SettingsValueAbsolute(const IntSettingDesc &sd, int32_t value)
{
	return {sd.str_val + ((value >= 0) ? 1 : 0), abs(value)};
}

/** Service Interval Settings Default Value displays the correct units or as a percentage */
static std::pair<StringParameter, StringParameter> ServiceIntervalSettingsValueText(const IntSettingDesc &sd, int32_t value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	StringID str;
	if (value == 0) {
		str = sd.str_val + 3;
	} else if (vds->servint_ispercent) {
		str = sd.str_val + 2;
	} else if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		str = sd.str_val + 1;
	} else {
		str = sd.str_val;
	}
	return {str, value};
}

/** Reposition the main toolbar as the setting changed. */
static void v_PositionMainToolbar(int32_t new_value)
{
	if (_game_mode != GM_MENU) PositionMainToolbar(nullptr);
}

/** Reposition the statusbar as the setting changed. */
static void v_PositionStatusbar(int32_t new_value)
{
	if (_game_mode != GM_MENU) {
		PositionStatusbar(nullptr);
		PositionNewsMessage(nullptr);
		PositionNetworkChatWindow(nullptr);
	}
}

/**
 * Redraw the smallmap after a colour scheme change.
 * @param new_value Callback parameter.
 */
static void RedrawSmallmap(int32_t new_value)
{
	BuildLandLegend();
	BuildOwnerLegend();
	SetWindowClassesDirty(WC_SMALLMAP);

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void StationSpreadChanged(int32_t new_value)
{
	InvalidateWindowData(WC_SELECT_STATION, 0);
	InvalidateWindowData(WC_BUILD_STATION, 0);
	InvalidateWindowData(WC_BUS_STATION, 0);
	InvalidateWindowData(WC_TRUCK_STATION, 0);
}

static void UpdateConsists(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		/* Update the consist of all trains so the maximum speed is set correctly. */
		if (t->IsFrontEngine() || t->IsFreeWagon()) {
			t->ConsistChanged(CCF_TRACK);
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}

	extern void AfterLoadTemplateVehiclesUpdateProperties();
	AfterLoadTemplateVehiclesUpdateProperties();

	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);
	SetWindowClassesDirty(WC_CREATE_TEMPLATE);
}

/**
 * Check and update if needed all vehicle service intervals.
 * @param new_value Contains 0 if service intervals are in days, otherwise intervals use percents.
 */
static void UpdateAllServiceInterval(int32_t new_value)
{
	bool update_vehicles;
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
		update_vehicles = false;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
		update_vehicles = true;
	}

	if (new_value != 0) {
		/* Service intervals are in percents. */
		vds->servint_trains   = DEF_SERVINT_PERCENT;
		vds->servint_roadveh  = DEF_SERVINT_PERCENT;
		vds->servint_aircraft = DEF_SERVINT_PERCENT;
		vds->servint_ships    = DEF_SERVINT_PERCENT;
	} else if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		/* Service intervals are in minutes. */
		vds->servint_trains   = DEF_SERVINT_MINUTES_TRAINS;
		vds->servint_roadveh  = DEF_SERVINT_MINUTES_ROADVEH;
		vds->servint_aircraft = DEF_SERVINT_MINUTES_AIRCRAFT;
		vds->servint_ships    = DEF_SERVINT_MINUTES_SHIPS;
	} else {
		/* Service intervals are in days. */
		vds->servint_trains   = DEF_SERVINT_DAYS_TRAINS;
		vds->servint_roadveh  = DEF_SERVINT_DAYS_ROADVEH;
		vds->servint_aircraft = DEF_SERVINT_DAYS_AIRCRAFT;
		vds->servint_ships    = DEF_SERVINT_DAYS_SHIPS;
	}

	if (update_vehicles) {
		const Company *c = Company::Get(_current_company);
		for (Vehicle *v : Vehicle::IterateFrontOnly()) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(CompanyServiceInterval(c, v->type));
				v->SetServiceIntervalIsPercent(new_value != 0);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static bool CanUpdateServiceInterval(VehicleType type, int32_t &new_value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	/* Test if the interval is valid */
	int32_t interval = GetServiceIntervalClamped(new_value, vds->servint_ispercent);
	return interval == new_value;
}

static void UpdateServiceInterval(VehicleType type, int32_t new_value)
{
	if (_game_mode != GM_MENU && Company::IsValidID(_current_company)) {
		for (Vehicle *v : Vehicle::IterateTypeFrontOnly(type)) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(new_value);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * Checks if the service intervals in the settings are specified as percentages and corrects the default value accordingly.
 * @param new_value Contains the service interval's default value in days, or 50 (default in percentage).
 */
static int32_t GetDefaultServiceInterval(const IntSettingDesc &sd, VehicleType type)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	if (vds->servint_ispercent) return DEF_SERVINT_PERCENT;

	if (EconTime::UsingWallclockUnits((_game_mode == GM_MENU))) {
		switch (type) {
			case VEH_TRAIN:    return DEF_SERVINT_MINUTES_TRAINS;
			case VEH_ROAD:     return DEF_SERVINT_MINUTES_ROADVEH;
			case VEH_AIRCRAFT: return DEF_SERVINT_MINUTES_AIRCRAFT;
			case VEH_SHIP:     return DEF_SERVINT_MINUTES_SHIPS;
			default: NOT_REACHED();
		}
	}

	return sd.def;
}

/**
 * Callback for when the player changes the timekeeping units.
 * @param Unused.
 */
static void ChangeTimekeepingUnits(int32_t)
{
	/* If service intervals are in time units (calendar days or real-world minutes), reset them to the correct defaults if not already in a game. */
	if (!_settings_client.company.vehicle.servint_ispercent && _game_mode != GM_NORMAL) {
		UpdateAllServiceInterval(0);
	}

	/* If we are using calendar timekeeping, "minutes per year" must be default. */
	if (_game_mode == GM_MENU && !EconTime::UsingWallclockUnits(true)) {
		_settings_newgame.economy.minutes_per_calendar_year = CalTime::DEF_MINUTES_PER_YEAR;
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);

	/* It is possible to change these units in-game. We must set the economy date appropriately. */
	if (_game_mode != GM_MENU) {
		/* Update effective day length before setting dates, so that the state ticks offset is calculated correctly */
		UpdateEffectiveDayLengthFactor();

		EconTime::Date new_economy_date;
		EconTime::DateFract new_economy_date_fract;

		if (EconTime::UsingWallclockUnits()) {
			/* If the new mode is wallclock units, adjust the economy date to account for different month/year lengths. */
			new_economy_date = EconTime::ConvertYMDToDate(EconTime::CurYear(), EconTime::CurMonth(), Clamp<EconTime::Day>(EconTime::CurDay(), 1, EconTime::DAYS_IN_ECONOMY_WALLCLOCK_MONTH));
			new_economy_date_fract = EconTime::CurDateFract();
		} else {
			/* If the new mode is calendar units, sync the economy date with the calendar date. */
			new_economy_date = ToEconTimeCast(CalTime::CurDate());
			new_economy_date_fract = CalTime::CurDateFract();
			EconTime::Detail::period_display_offset -= EconTime::YearDelta{CalTime::CurYear().base() - EconTime::CurYear().base()};
		}

		/* Update link graphs and vehicles, as these include stored economy dates. */
		LinkGraphSchedule::instance.ShiftDates(new_economy_date - EconTime::CurDate());
		ShiftVehicleDates(new_economy_date - EconTime::CurDate());

		/* Only change the date after changing cached values above. */
		EconTime::Detail::SetDate(new_economy_date, new_economy_date_fract);

		UpdateOrderUIOnDateChange();
		SetupTickRate();
	}

	UpdateTimeSettings(0);
	CloseWindowByClass(WC_PAYMENT_RATES);
	CloseWindowByClass(WC_COMPANY_VALUE);
	CloseWindowByClass(WC_PERFORMANCE_HISTORY);
	CloseWindowByClass(WC_DELIVERED_CARGO);
	CloseWindowByClass(WC_OPERATING_PROFIT);
	CloseWindowByClass(WC_INCOME_GRAPH);
	CloseWindowByClass(WC_STATION_CARGO);
	CloseWindowByClass(WC_INDUSTRY_PRODUCTION);
}

/**
 * Callback after the player changes the minutes per year.
 * @param new_value The intended new value of the setting, used for clamping.
 */
static void ChangeMinutesPerYear(int32_t new_value)
{
	/* We don't allow setting Minutes Per Year below default, unless it's to 0 for frozen calendar time. */
	if (new_value < CalTime::DEF_MINUTES_PER_YEAR) {
		int clamped;

		/* If the new value is 1, we're probably at 0 and trying to increase the value, so we should jump up to default. */
		if (new_value == 1) {
			clamped = CalTime::DEF_MINUTES_PER_YEAR;
		} else {
			clamped = CalTime::FROZEN_MINUTES_PER_YEAR;
		}

		/* Override the setting with the clamped value. */
		if (_game_mode == GM_MENU) {
			_settings_newgame.economy.minutes_per_calendar_year = clamped;
		} else {
			_settings_game.economy.minutes_per_calendar_year = clamped;
		}
	}

	UpdateEffectiveDayLengthFactor();
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 1);

	/* If the setting value is not the default, force the game to use wallclock timekeeping units.
	 * This can only happen in the menu, since the pre_cb ensures this setting can only be changed there, or if we're already using wallclock units.
	 */
	if (_game_mode == GM_MENU && (_settings_newgame.economy.minutes_per_calendar_year != CalTime::DEF_MINUTES_PER_YEAR)) {
		_settings_newgame.economy.timekeeping_units = TKU_WALLCLOCK;
		InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);
	}
}

static std::tuple<int32_t, uint32_t> GetServiceIntervalRange(const IntSettingDesc &)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	if (vds->servint_ispercent) return { MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT };

	if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) {
		return { MIN_SERVINT_MINUTES, MAX_SERVINT_MINUTES };
	}

	return { MIN_SERVINT_DAYS, MAX_SERVINT_DAYS };
}

static void TrainAccelerationModelChanged(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		if (t->IsFrontEngine()) {
			t->tcache.cached_max_curve_speed = t->GetCurveSpeedLimit();
			t->UpdateAcceleration();
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}

	extern void AfterLoadTemplateVehiclesUpdateProperties();
	AfterLoadTemplateVehiclesUpdateProperties();

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
	SetWindowClassesDirty(WC_TEMPLATEGUI_MAIN);
	SetWindowClassesDirty(WC_CREATE_TEMPLATE);
}

static bool CheckTrainBrakingModelChange(int32_t &new_value)
{
	if (new_value == TBM_REALISTIC && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		for (TileIndex t(0); t < Map::Size(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RAIL_TILE_SIGNALS) {
				uint signals = GetPresentSignals(t);
				if ((signals & 0x3) & ((signals & 0x3) - 1) || (signals & 0xC) & ((signals & 0xC) - 1)) {
					/* Signals in both directions */
					ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED), {}, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
				if (((signals & 0x3) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_LOWER))) ||
						((signals & 0xC) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_UPPER)))) {
					/* Banned signal types present */
					ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED), {}, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
			}
		}
	}

	return true;
}

static void TrainBrakingModelChanged(int32_t new_value)
{
	for (Train *t : Train::Iterate()) {
		if (!(t->vehstatus & VS_CRASHED)) {
			t->crash_anim_pos = 0;
		}
		if (t->IsFrontEngine()) {
			t->UpdateAcceleration();
		}
	}
	if (new_value == TBM_REALISTIC && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		for (TileIndex t(0); t < Map::Size(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RAIL_TILE_SIGNALS) {
				TrackBits bits = GetTrackBits(t);
				do {
					Track track = RemoveFirstTrack(&bits);
					if (HasSignalOnTrack(t, track) && GetSignalType(t, track) == SIGTYPE_BLOCK && HasBit(GetRailReservationTrackBits(t), track)) {
						if (EnsureNoTrainOnTrackBits(t, TrackToTrackBits(track)).Succeeded()) {
							UnreserveTrack(t, track);
						}
					}
				} while (bits != TRACK_BIT_NONE);
			}
		}
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: {}", VehicleInfoDumper(v_cur));
		extern bool _long_reserve_disabled;
		_long_reserve_disabled = true;
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0 || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
		}
		_long_reserve_disabled = false;
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0 || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
			if (v->lookahead != nullptr) v->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	} else if (new_value == TBM_ORIGINAL && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: {}", VehicleInfoDumper(v_cur));
		for (Train *v : Train::IterateFrontOnly()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0 || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) {
				v->lookahead.reset();
				continue;
			}
			if (!HasBit(v->flags, VRF_TRAIN_STUCK)) {
				_settings_game.vehicle.train_braking_model = TBM_REALISTIC;
				FreeTrainTrackReservation(v);
				_settings_game.vehicle.train_braking_model = new_value;
				TryPathReserve(v, true, HasStationTileRail(v->tile));
			} else {
				v->lookahead.reset();
			}
		}
	}

	UpdateExtraAspectsVariable();
	UpdateAllBlockSignals();

	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	MarkWholeScreenDirty();
}

/**
 * This function updates the train acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void TrainSlopeSteepnessChanged(int32_t new_value)
{
	for (Train *t : Train::IterateFrontOnly()) {
		if (t->IsFrontEngine()) {
			t->CargoChanged();
			if (t->lookahead != nullptr) t->lookahead->flags.Set(TrainReservationLookAheadFlag::ApplyAdvisory);
		}
	}
}

/**
 * This function updates realistic acceleration caches when the setting "Road vehicle acceleration model" is set.
 * @param new_value Unused new value of setting.
 */
static void RoadVehAccelerationModelChanged(int32_t new_value)
{
	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
			rv->CargoChanged();
		}
	}
	if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL || !_settings_game.vehicle.improved_breakdowns) {
		for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
			rv->breakdown_chance_factor = 128;
		}
	}

	/* These windows show acceleration values only when realistic acceleration is on. They must be redrawn after a setting change. */
	SetWindowClassesDirty(WC_ENGINE_PREVIEW);
	InvalidateWindowClassesData(WC_BUILD_VEHICLE, 0);
	InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN, 0);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

/**
 * This function updates the road vehicle acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void RoadVehSlopeSteepnessChanged(int32_t new_value)
{
	for (RoadVehicle *rv : RoadVehicle::IterateFrontOnly()) {
		rv->CargoChanged();
	}
}

static void ProgrammableSignalsShownChanged(int32_t new_value)
{
	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
}

static void TownFoundingChanged(int32_t new_value)
{
	if (_game_mode != GM_EDITOR && _settings_game.economy.found_town == TF_FORBIDDEN) {
		CloseWindowById(WC_FOUND_TOWN, 0);
	} else {
		InvalidateWindowData(WC_FOUND_TOWN, 0);
	}
}

static void InvalidateVehTimetableWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE, VIWD_MODIFY_ORDERS);
	InvalidateWindowClassesData(WC_SCHDISPATCH_SLOTS, VIWD_MODIFY_ORDERS);
}

static void ChangeTimetableInTicksMode(int32_t new_value)
{
	SetWindowClassesDirty(WC_VEHICLE_ORDERS);
	InvalidateVehTimetableWindow(new_value);
}

static void UpdateTimeSettings(int32_t new_value)
{
	SetupTimeSettings();
	InvalidateVehTimetableWindow(new_value);
	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_REINIT);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 1);
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	MarkWholeScreenDirty();
}

static void ChangeTimeOverrideMode(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTimeSettings(new_value);
}

static void ZoomMinMaxChanged(int32_t new_value)
{
	extern void ConstrainAllViewportsZoom();
	extern void UpdateFontHeightCache();
	ConstrainAllViewportsZoom();
	GfxClearSpriteCache();
	InvalidateWindowClassesData(WC_SPRITE_ALIGNER);
	if (AdjustGUIZoom(AGZM_MANUAL)) {
		ReInitAllWindows(false);
	}
}

static void SpriteZoomMinChanged(int32_t new_value)
{
	GfxClearSpriteCache();
	/* Force all sprites to redraw at the new chosen zoom level */
	MarkWholeScreenDirty();
}

static void DeveloperModeChanged(int32_t new_value)
{
	DebugReconsiderSendRemoteMessages();
}

/**
 * Update any possible saveload window and delete any newgrf dialogue as
 * its widget parts might change. Reinit all windows as it allows access to the
 * newgrf debug button.
 * @param new_value unused.
 */
static void InvalidateNewGRFChangeWindows(int32_t new_value)
{
	InvalidateWindowClassesData(WC_SAVELOAD);
	CloseWindowByClass(WC_GAME_OPTIONS);
	ReInitAllWindows(false);
}

static void InvalidateCompanyLiveryWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_COMPANY_COLOUR, -1);
	ResetVehicleColourMap();
	MarkWholeScreenDirty();
}

static void ScriptMaxOpsChange(int32_t new_value)
{
	if (_networking && !_network_server) return;

	GameInstance *g = Game::GetGameInstance();
	if (g != nullptr && !g->IsDead()) {
		g->LimitOpsTillSuspend(new_value);
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			c->ai_instance->LimitOpsTillSuspend(new_value);
		}
	}
}

static bool CheckScriptMaxMemoryChange(int32_t &new_value)
{
	if (_networking && !_network_server) return true;

	size_t limit = static_cast<size_t>(new_value) << 20;

	GameInstance *g = Game::GetGameInstance();
	if (g != nullptr && !g->IsDead()) {
		if (g->GetAllocatedMemory() > limit) return false;
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			if (c->ai_instance->GetAllocatedMemory() > limit) return false;
		}
	}

	return true;
}

static void ScriptMaxMemoryChange(int32_t new_value)
{
	if (_networking && !_network_server) return;

	size_t limit = static_cast<size_t>(new_value) << 20;

	GameInstance *g = Game::GetGameInstance();
	if (g != nullptr && !g->IsDead()) {
		g->SetMemoryAllocationLimit(limit);
	}

	for (const Company *c : Company::Iterate()) {
		if (c->is_ai && c->ai_instance != nullptr && !c->ai_instance->IsDead()) {
			c->ai_instance->SetMemoryAllocationLimit(limit);
		}
	}
}

/**
 * Invalidate the company details window after the shares setting changed.
 * @param new_value Unused.
 * @return Always true.
 */
static void InvalidateCompanyWindow(int32_t new_value)
{
	InvalidateWindowClassesData(WC_COMPANY);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void EnableSingleVehSharedOrderGuiChanged(int32_t new_value)
{
	for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
		InvalidateWindowClassesData(GetWindowClassForVehicleType(type), 0);
	}
	SetWindowClassesDirty(WC_VEHICLE_TIMETABLE);
	InvalidateWindowClassesData(WC_VEHICLE_ORDERS, 0);
}

static void CheckYapfRailSignalPenalties(int32_t new_value)
{
	extern void YapfCheckRailSignalPenalties();
	YapfCheckRailSignalPenalties();
}

static void ViewportMapShowTunnelModeChanged(int32_t new_value)
{
	extern void ViewportMapBuildTunnelCache();
	ViewportMapBuildTunnelCache();

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void ViewportMapLandscapeModeChanged(int32_t new_value)
{
	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void MarkAllViewportsDirty(int32_t new_value)
{
	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();

	extern void MarkWholeNonMapViewportsDirty();
	MarkWholeNonMapViewportsDirty();
}

static void UpdateLinkgraphColours(int32_t new_value)
{
	BuildLinkStatsLegend();
	MarkWholeScreenDirty();
}

static void ClimateThresholdModeChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GENERATE_LANDSCAPE);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void VelocityUnitsChanged(int32_t new_value) {
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	MarkWholeScreenDirty();
}

static void ChangeTrackTypeSortMode(int32_t new_value) {
	extern void SortRailTypes();
	SortRailTypes();
	MarkWholeScreenDirty();
}

static void TrainSpeedAdaptationChanged(int32_t new_value) {
	extern void ClearAllSignalSpeedRestrictions();
	ClearAllSignalSpeedRestrictions();
	for (Train *t : Train::Iterate()) {
		t->signal_speed_restriction = 0;
	}
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static void AutosaveModeChanged(int32_t new_value) {
	extern void ChangeAutosaveFrequency(bool reset);
	ChangeAutosaveFrequency(false);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

/** Checks if any settings are set to incorrect values, and sets them to correct values in that case. */
static void ValidateSettings()
{
	/* Do not allow a custom sea level with the original land generator. */
	if (_settings_newgame.game_creation.land_generator == LG_ORIGINAL &&
			_settings_newgame.difficulty.quantity_sea_lakes == CUSTOM_SEA_LEVEL_NUMBER_DIFFICULTY) {
		_settings_newgame.difficulty.quantity_sea_lakes = CUSTOM_SEA_LEVEL_MIN_PERCENTAGE;
	}
}

static bool TownCouncilToleranceAdjust(int32_t &new_value)
{
	if (new_value == 255) new_value = TOWN_COUNCIL_PERMISSIVE;
	return true;
}

static void DifficultyNoiseChange(int32_t new_value)
{
	if (_game_mode == GM_NORMAL) {
		UpdateAirportsNoise();
		if (_settings_game.economy.station_noise_level) {
			InvalidateWindowClassesData(WC_TOWN_VIEW, 0);
		}
	}
}

static void DifficultyMoneyCheatMultiplayerChange(int32_t new_value)
{
	CloseWindowById(WC_CHEATS, 0);
}

static void DifficultyRenameTownsMultiplayerChange(int32_t new_value)
{
	SetWindowClassesDirty(WC_TOWN_VIEW);
}

static void DifficultyOverrideTownSettingsMultiplayerChange(int32_t new_value)
{
	SetWindowClassesDirty(WC_TOWN_AUTHORITY);
}

static void MaxNoAIsChange(int32_t new_value)
{
	if (GetGameSettings().difficulty.max_no_competitors != 0 &&
			AI::GetInfoList()->size() == 0 &&
			!IsNonAdminNetworkClient()) {
		ShowErrorMessage(GetEncodedString(STR_WARNING_NO_SUITABLE_AI), {}, WL_CRITICAL);
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);
}

/**
 * Check whether the road side may be changed.
 * @param new_value unused
 * @return true if the road side may be changed.
 */
static bool CheckRoadSide(int32_t &new_value)
{
	extern bool RoadVehiclesExistOutsideDepots();
	return (_game_mode == GM_MENU || !RoadVehiclesExistOutsideDepots());
}

static void RoadSideChanged(int32_t new_value)
{
	extern void RecalculateRoadCachedOneWayStates();
	RecalculateRoadCachedOneWayStates();
}

/**
 * Conversion callback for _gameopt_settings_game.landscape
 * It converts (or try) between old values and the new ones,
 * without losing initial setting of the user
 * @param value that was read from config file
 * @return the "hopefully" converted value
 */
static size_t ConvertLandscape(const char *value)
{
	/* try with the old values */
	static std::vector<std::string> _old_landscape_values{"normal", "hilly", "desert", "candy"};
	return OneOfManySettingDesc::ParseSingleValue(value, strlen(value), _old_landscape_values);
}

static bool CheckFreeformEdges(int32_t &new_value)
{
	if (_game_mode == GM_MENU) return true;
	if (new_value != 0) {
		for (Ship *s : Ship::Iterate()) {
			/* Check if there is a ship on the northern border. */
			if (TileX(s->tile) == 0 || TileY(s->tile) == 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_EMPTY), {}, WL_ERROR);
				return false;
			}
		}
		for (const BaseStation *st : BaseStation::Iterate()) {
			/* Check if there is a non-deleted buoy on the northern border. */
			if (st->IsInUse() && (TileX(st->xy) == 0 || TileY(st->xy) == 0)) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_EMPTY), {}, WL_ERROR);
				return false;
			}
		}
	} else {
		for (uint i = 0; i < Map::MaxX(); i++) {
			if (TileHeight(TileXY(i, 1)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < Map::MaxX(); i++) {
			if (!IsTileType(TileXY(i, Map::MaxY() - 1), MP_WATER) || TileHeight(TileXY(1, Map::MaxY())) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 0; i < Map::MaxY(); i++) {
			if (TileHeight(TileXY(1, i)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < Map::MaxY(); i++) {
			if (!IsTileType(TileXY(Map::MaxX() - 1, i), MP_WATER) || TileHeight(TileXY(Map::MaxX(), i)) != 0) {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
				return false;
			}
		}
	}
	return true;
}

static void UpdateFreeformEdges(int32_t new_value)
{
	if (_game_mode == GM_MENU) return;

	if (new_value != 0) {
		for (uint x = 0; x < Map::SizeX(); x++) MakeVoid(TileXY(x, 0));
		for (uint y = 0; y < Map::SizeY(); y++) MakeVoid(TileXY(0, y));
	} else {
		/* Make tiles at the border water again. */
		for (uint i = 0; i < Map::MaxX(); i++) {
			SetTileHeight(TileXY(i, 0), 0);
			MakeSea(TileXY(i, 0));
		}
		for (uint i = 0; i < Map::MaxY(); i++) {
			SetTileHeight(TileXY(0, i), 0);
			MakeSea(TileXY(0, i));
		}
	}
	for (Vehicle *v : Vehicle::Iterate()) {
		if (v->tile == 0) v->UpdatePosition();
	}
	MarkWholeScreenDirty();
}

bool CheckMapEdgesAreWater(bool allow_non_flat_void)
{
	auto check_tile = [&](uint x, uint y, Slope inner_edge) -> bool {
		int h = 0;
		Slope slope;
		std::tie(slope, h) = GetTilePixelSlopeOutsideMap(x, y);
		if (slope == SLOPE_FLAT && h == 0) return true;
		if (allow_non_flat_void && h == 0 && (slope & inner_edge) == 0 && IsTileType(TileXY(x, y), MP_VOID)) return true;
		return false;
	};
	check_tile(        0,         0, SLOPE_S);
	check_tile(        0, Map::MaxY(), SLOPE_W);
	check_tile(Map::MaxX(),         0, SLOPE_E);
	check_tile(Map::MaxX(), Map::MaxY(), SLOPE_N);

	for (uint x = 1; x < Map::MaxX(); x++) {
		if (!check_tile(x, 0, SLOPE_SE)) return false;
		if (!check_tile(x, Map::MaxY(), SLOPE_NW)) return false;
	}
	for (uint y = 1; y < Map::MaxY(); y++) {
		if (!check_tile(0, y, SLOPE_SW)) return false;
		if (!check_tile(Map::MaxX(), y, SLOPE_NE)) return false;
	}

	return true;
}

static bool CheckMapEdgeMode(int32_t &new_value)
{
	if (_game_mode == GM_MENU || !_settings_game.construction.freeform_edges || new_value == 0) return true;

	if (!CheckMapEdgesAreWater(true)) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_EDGES_NOT_WATER), {}, WL_ERROR);
		return false;
	}

	return true;
}

static void MapEdgeModeChanged(int32_t new_value)
{
	MarkAllViewportsDirty(new_value);

	if (_game_mode == GM_MENU || !_settings_game.construction.freeform_edges || new_value == 0) return;

	for (uint x = 0; x <= Map::MaxX(); x++) {
		SetTileHeight(TileXY(x, 0), 0);
		SetTileHeight(TileXY(x, Map::MaxY()), 0);
	}
	for (uint y = 1; y < Map::MaxY(); y++) {
		SetTileHeight(TileXY(0, y), 0);
		SetTileHeight(TileXY(Map::MaxX(), y), 0);
	}
}

/**
 * Changing the setting "allow multiple NewGRF sets" is not allowed
 * if there are vehicles.
 */
static bool CheckDynamicEngines(int32_t &new_value)
{
	if (_game_mode == GM_MENU) return true;

	if (!EngineOverrideManager::ResetToCurrentNewGRFConfig()) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_DYNAMIC_ENGINES_EXISTING_VEHICLES), {}, WL_ERROR);
		return false;
	}

	return true;
}

static bool CheckMaxHeightLevel(int32_t &new_value)
{
	if (_game_mode == GM_NORMAL) return false;
	if (_game_mode != GM_EDITOR) return true;

	/* Check if at least one mountain on the map is higher than the new value.
	 * If yes, disallow the change. */
	for (TileIndex t(0); t < Map::Size(); t++) {
		if ((int32_t)TileHeight(t) > new_value) {
			ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_TOO_HIGH_MOUNTAIN), {}, WL_ERROR);
			/* Return old, unchanged value */
			return false;
		}
	}

	return true;
}

static void StationCatchmentChanged(int32_t new_value)
{
	Station::RecomputeCatchmentForAll();
	for (Station *st : Station::Iterate()) UpdateStationAcceptance(st, true);
	MarkWholeScreenDirty();
}

static bool CheckSharingRail(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_TRAIN, new_value);
}

static void SharingRailChanged(int32_t new_value)
{
	UpdateAllBlockSignals();
}

static bool CheckSharingRoad(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_ROAD, new_value);
}

static bool CheckSharingWater(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_SHIP, new_value);
}

static bool CheckSharingAir(int32_t &new_value)
{
	return CheckSharingChangePossible(VEH_AIRCRAFT, new_value);
}

static void MaxVehiclesChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_BUILD_TOOLBAR);
	MarkWholeScreenDirty();
}

static void ImprovedBreakdownsSettingChanged(int32_t new_value)
{
	if (!_settings_game.vehicle.improved_breakdowns) return;

	for (Vehicle *v : Vehicle::IterateFrontOnly()) {
		switch (v->type) {
			case VEH_TRAIN:
				if (v->IsFrontEngine()) {
					v->breakdown_chance_factor = 128;
					Train::From(v)->UpdateAcceleration();
				}
				break;

			case VEH_ROAD:
				if (v->IsFrontEngine()) {
					v->breakdown_chance_factor = 128;
				}
				break;

			default:
				break;
		}
	}
}

static void DayLengthChanged(int32_t new_value)
{
	UpdateEffectiveDayLengthFactor();
	RecalculateStateTicksOffset();

	MarkWholeScreenDirty();
}

static void IndustryEventRateChanged(int32_t new_value)
{
	if (_game_mode != GM_MENU) StartupIndustryDailyChanges(false);
}

static void DefaultAllowTownGrowthChanged(int32_t new_value)
{
	if (_game_mode != GM_MENU) {
		extern void UpdateTownGrowthForAllTowns();
		UpdateTownGrowthForAllTowns();
	}
}

static void TownZoneModeChanged(int32_t new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTownRadii();
}

static void TownZoneCustomValueChanged(int32_t new_value)
{
	if (_settings_game.economy.town_zone_calc_mode) UpdateTownRadii();
}

static bool CheckTTDPatchSettingFlag(uint flag)
{
	extern bool HasTTDPatchFlagBeenObserved(uint flag);
	if (_networking && HasTTDPatchFlagBeenObserved(flag)) {
		ShowErrorMessage(GetEncodedString(STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED), GetEncodedString(STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED_NEWGRF), WL_ERROR);
		return false;
	}

	return true;
}

/**
 * Replace a passwords that are a literal asterisk with an empty string.
 * @param newval The new string value for this password field.
 * @return Always true.
 */
static bool ReplaceAsteriskWithEmptyPassword(std::string &newval)
{
	if (newval.compare("*") == 0) newval.clear();
	return true;
}

static bool IsValidHexKeyString(const std::string &newval)
{
	for (const char c : newval) {
		if (!IsValidChar(c, CS_HEXADECIMAL)) return false;
	}
	return true;
}

static bool IsValidHex128BitKeyString(std::string &newval)
{
	return newval.size() == 32 && IsValidHexKeyString(newval);
}

static bool IsValidHex256BitKeyString(std::string &newval)
{
	return newval.size() == 64 && IsValidHexKeyString(newval);
}

static void ParseCompanyPasswordStorageToken(const std::string &value)
{
	extern std::array<uint8_t, 16> _network_company_password_storage_token;
	if (value.size() != 32) return;
	ConvertHexToBytes(value, _network_company_password_storage_token);
}

static void ParseCompanyPasswordStorageSecret(const std::string &value)
{
	extern std::array<uint8_t, 32> _network_company_password_storage_key;
	if (value.size() != 64) return;
	ConvertHexToBytes(value, _network_company_password_storage_key);
}

/** Update the game info, and send it to the clients when we are running as a server. */
static void UpdateClientConfigValues()
{
	NetworkServerUpdateGameInfo();

	InvalidateWindowData(WC_CLIENT_LIST, 0);

	if (_network_server) {
		NetworkServerSendConfigUpdate();
	}
}

/* End - Callback Functions */

/* Begin - xref conversion callbacks */

static int64_t LinkGraphDistModeXrefChillPP(int64_t val)
{
	return val ^ 2;
}

/* End - xref conversion callbacks */

/* Begin - GUI callbacks */

static bool OrderTownGrowthRate(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DROPDOWN_ORDER: {
			int in = data.val;
			int out;
			if (in == 0) {
				out = 0;
			} else if (in <= 2) {
				out = in - 3;
			} else {
				out = in - 2;
			}
			data.val = out;
			return true;
		}

		default:
			return false;
	}
}

static bool LinkGraphDistributionSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, data.text);
			data.text = STR_CONFIG_SETTING_DISTRIBUTION_HELPTEXT_EXTRA;
			return true;

		default:
			return false;
	}
}

static bool AllowRoadStopsUnderBridgesSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, data.text);
			data.text = STR_CONFIG_SETTING_ALLOW_ROAD_STATIONS_UNDER_BRIDGES_HELPTEXT_EXTRA;
			return true;

		default:
			return false;
	}
}

static bool ZoomMaxCfgName(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_CFG_NAME:
			data.str = "gui.zoom_max_extra";
			_fallback_gui_zoom_max = false;
			return true;

		case SOGCT_CFG_FALLBACK_NAME:
			data.str = "zoom_max";
			_fallback_gui_zoom_max = true;
			return true;

		default:
			return false;
	}
}

static bool TreePlacerSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, data.text);
			data.text = STR_CONFIG_SETTING_TREE_PLACER_HELPTEXT_EXTRA;
			return true;

		default:
			return false;
	}
}

static bool DefaultSignalsSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, data.text);
			data.text = STR_CONFIG_SETTING_SHOW_ALL_SIG_DEF_HELPTEXT_EXTRA;
			return true;

		default:
			return false;
	}
}

static bool ChunnelSettingGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, 3);
			SetDParam(1, 8);
			return true;

		default:
			return false;
	}
}

static std::pair<StringParameter, StringParameter> TownCargoScaleValueText(const IntSettingDesc &sd, int32_t value)
{
	StringID str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE;
	if (GetGameSettings().economy.day_length_factor > 1 && GetGameSettings().economy.town_cargo_scale_mode == CSM_DAYLENGTH) {
		str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE_ECON_SPEED_REDUCTION_MULT;
	}
	return {str, value};
}

static std::pair<StringParameter, StringParameter> IndustryCargoScaleValueText(const IntSettingDesc &sd, int32_t value)
{
	StringID str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE;
	if (GetGameSettings().economy.day_length_factor > 1 && GetGameSettings().economy.industry_cargo_scale_mode == CSM_DAYLENGTH) {
		str = STR_CONFIG_SETTING_CARGO_SCALE_VALUE_ECON_SPEED_REDUCTION_MULT;
	}
	return {str, value};
}

static bool IndustryCargoScaleGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_DESCRIPTION_TEXT:
			SetDParam(0, data.text);
			data.text = STR_CONFIG_SETTING_INDUSTRY_CARGO_SCALE_HELPTEXT_EXTRA;
			return true;

		default:
			return false;
	}
}

static std::pair<StringParameter, StringParameter> CalendarModeDisabledValueText(const IntSettingDesc &sd, int32_t value)
{
	return {EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? sd.str_val : STR_CONFIG_SETTING_DISABLED_TIMEKEEPING_MODE_CALENDAR, value};
}

static bool CalendarModeDisabledGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DISABLE:
			if (!EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) data.val = 1;
			return true;

		default:
			return false;
	}
}

static std::pair<StringParameter, StringParameter> WallclockModeDisabledValueText(const IntSettingDesc &sd, int32_t value)
{
	return {EconTime::UsingWallclockUnits(_game_mode == GM_MENU) ? STR_CONFIG_SETTING_DISABLED_TIMEKEEPING_MODE_WALLCLOCK : sd.str_val, value};
}

static bool WallclockModeDisabledGUI(SettingOnGuiCtrlData &data)
{
	switch (data.type) {
		case SOGCT_GUI_DISABLE:
			if (EconTime::UsingWallclockUnits(_game_mode == GM_MENU)) data.val = 1;
			return true;

		default:
			return false;
	}
}

/* End - GUI callbacks */

/**
 * Prepare for reading and old diff_custom by zero-ing the memory.
 */
static void PrepareOldDiffCustom()
{
	memset(_old_diff_custom, 0, sizeof(_old_diff_custom));
}

/**
 * Reading of the old diff_custom array and transforming it to the new format.
 * @param savegame is it read from the config or savegame. In the latter case
 *                 we are sure there is an array; in the former case we have
 *                 to check that.
 */
static void HandleOldDiffCustom(bool savegame)
{
	/* Savegames before v4 didn't have "town_council_tolerance" in savegame yet. */
	bool has_no_town_council_tolerance = savegame && IsSavegameVersionBefore(SLV_4);
	uint options_to_load = GAME_DIFFICULTY_NUM - (has_no_town_council_tolerance ? 1 : 0);

	if (!savegame) {
		/* If we did read to old_diff_custom, then at least one value must be non 0. */
		bool old_diff_custom_used = false;
		for (uint i = 0; i < options_to_load && !old_diff_custom_used; i++) {
			old_diff_custom_used = (_old_diff_custom[i] != 0);
		}

		if (!old_diff_custom_used) return;
	}

	/* Iterate over all the old difficulty settings, and convert the list-value to the new setting. */
	uint i = 0;
	for (const auto &name : _old_diff_settings) {
		if (has_no_town_council_tolerance && name == "town_council_tolerance") continue;

		std::string fullname = "difficulty." + name;
		const SettingDesc *sd = GetSettingFromName(fullname.c_str());

		/* Some settings are no longer in use; skip reading those. */
		if (sd == nullptr) {
			i++;
			continue;
		}

		int32_t value = (int32_t)((name == "max_loan" ? 1000 : 1) * _old_diff_custom[i++]);
		sd->AsIntSetting()->MakeValueValidAndWrite(savegame ? &_settings_game : &_settings_newgame, value);
	}
}

static void AILoadConfig(const IniFile &ini, const char *grpname)
{
	const IniGroup *group = ini.GetGroup(grpname);

	/* Clean any configured AI */
	for (CompanyID c = CompanyID::Begin(); c < MAX_COMPANIES; ++c) {
		AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME)->Change(std::nullopt);
	}

	/* If no group exists, return */
	if (group == nullptr) return;

	CompanyID c = CompanyID::Begin();
	for (const IniItem &item : group->items) {
		AIConfig *config = AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME);

		config->Change(item.name);
		if (!config->HasScript()) {
			if (item.name != "none") {
				Debug(script, 0, "The AI by the name '{}' was no longer found, and removed from the list.", item.name);
				continue;
			}
		}
		if (item.value.has_value()) config->StringToSettings(*item.value);
		++c;
		if (c >= MAX_COMPANIES) break;
	}
}

static void GameLoadConfig(const IniFile &ini, const char *grpname)
{
	const IniGroup *group = ini.GetGroup(grpname);

	/* Clean any configured GameScript */
	GameConfig::GetConfig(GameConfig::SSS_FORCE_NEWGAME)->Change(std::nullopt);

	/* If no group exists, return */
	if (group == nullptr || group->items.empty()) return;

	const IniItem &item = group->items.front();

	GameConfig *config = GameConfig::GetConfig(AIConfig::SSS_FORCE_NEWGAME);

	config->Change(item.name);
	if (!config->HasScript()) {
		if (item.name != "none") {
			Debug(script, 0, "The GameScript by the name '{}' was no longer found, and removed from the list.", item.name);
			return;
		}
	}
	if (item.value.has_value()) config->StringToSettings(*item.value);
}

/**
 * Load BaseGraphics set selection and configuration.
 */
static void GraphicsSetLoadConfig(IniFile &ini)
{
	if (const IniGroup *group = ini.GetGroup("misc"); group != nullptr) {
		/* Load old setting first. */
		if (const IniItem *item = group->GetItem("graphicsset"); item != nullptr && item->value) BaseGraphics::ini_data.name = *item->value;
	}

	if (const IniGroup *group = ini.GetGroup("graphicsset"); group != nullptr) {
		/* Load new settings. */
		if (const IniItem *item = group->GetItem("name"); item != nullptr && item->value) BaseGraphics::ini_data.name = *item->value;

		if (const IniItem *item = group->GetItem("shortname"); item != nullptr && item->value && item->value->size() == 8) {
			BaseGraphics::ini_data.shortname = std::byteswap<uint32_t>(std::strtoul(item->value->c_str(), nullptr, 16));
		}

		if (const IniItem *item = group->GetItem("extra_version"); item != nullptr && item->value) BaseGraphics::ini_data.extra_version = std::strtoul(item->value->c_str(), nullptr, 10);

		if (const IniItem *item = group->GetItem("extra_params"); item != nullptr && item->value) {
			auto params = ParseIntList(item->value->c_str());
			if (params.has_value()) {
				BaseGraphics::ini_data.extra_params = params.value();
			} else {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR),
					GetEncodedString(STR_CONFIG_ERROR_ARRAY, BaseGraphics::ini_data.name),
					WL_CRITICAL);
			}
		}
	}
}

/**
 * Load a GRF configuration
 * @param ini       The configuration to read from.
 * @param grpname   Group name containing the configuration of the GRF.
 * @param is_static GRF is static.
 */
static GRFConfigList GRFLoadConfig(const IniFile &ini, const char *grpname, bool is_static)
{
	const IniGroup *group = ini.GetGroup(grpname);
	GRFConfigList list;

	if (group == nullptr) return list;

	uint num_grfs = 0;
	for (const IniItem &item : group->items) {
		std::unique_ptr<GRFConfig> c{};

		std::array<uint8_t, 4> grfid_buf;
		MD5Hash md5sum;
		std::string_view item_name = item.name;
		bool has_md5sum = false;

		/* Try reading "<grfid>|" and on success, "<md5sum>|". */
		auto grfid_pos = item_name.find("|");
		if (grfid_pos != std::string_view::npos) {
			std::string_view grfid_str = item_name.substr(0, grfid_pos);

			if (ConvertHexToBytes(grfid_str, grfid_buf)) {
				item_name = item_name.substr(grfid_pos + 1);

				auto md5sum_pos = item_name.find("|");
				if (md5sum_pos != std::string_view::npos) {
					std::string_view md5sum_str = item_name.substr(0, md5sum_pos);

					has_md5sum = ConvertHexToBytes(md5sum_str, md5sum);
					if (has_md5sum) item_name = item_name.substr(md5sum_pos + 1);
				}

				uint32_t grfid = grfid_buf[0] | (grfid_buf[1] << 8) | (grfid_buf[2] << 16) | (grfid_buf[3] << 24);
				if (has_md5sum) {
					const GRFConfig *s = FindGRFConfig(grfid, FGCM_EXACT, &md5sum);
					if (s != nullptr) c = std::make_unique<GRFConfig>(*s);
				}
				if (c == nullptr && !FioCheckFileExists(std::string(item_name), NEWGRF_DIR)) {
					const GRFConfig *s = FindGRFConfig(grfid, FGCM_NEWEST_VALID);
					if (s != nullptr) c = std::make_unique<GRFConfig>(*s);
				}
			}
		}
		std::string filename = std::string(item_name);

		if (c == nullptr) c = std::make_unique<GRFConfig>(filename);

		/* Parse parameters */
		if (item.value.has_value() && !item.value->empty()) {
			auto params = ParseIntList(item.value->c_str());
			if (params.has_value()) {
				c->SetParams(params.value());
			} else {
				ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR),
					GetEncodedString(STR_CONFIG_ERROR_ARRAY, filename),
					WL_CRITICAL);
			}
		}

		/* Check if item is valid */
		if (!FillGRFDetails(*c, is_static) || c->flags.Test(GRFConfigFlag::Invalid)) {
			StringID reason;
			if (c->status == GCS_NOT_FOUND) {
				reason = STR_CONFIG_ERROR_INVALID_GRF_NOT_FOUND;
			} else if (c->flags.Test(GRFConfigFlag::Unsafe)) {
				reason = STR_CONFIG_ERROR_INVALID_GRF_UNSAFE;
			} else if (c->flags.Test(GRFConfigFlag::System)) {
				reason = STR_CONFIG_ERROR_INVALID_GRF_SYSTEM;
			} else if (c->flags.Test(GRFConfigFlag::Invalid)) {
				reason = STR_CONFIG_ERROR_INVALID_GRF_INCOMPATIBLE;
			} else {
				reason = STR_CONFIG_ERROR_INVALID_GRF_UNKNOWN;
			}

			ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR),
				GetEncodedString(STR_CONFIG_ERROR_INVALID_GRF, filename.empty() ? item.name.c_str() : filename, reason),
				WL_CRITICAL);
			continue;
		}

		/* Check for duplicate GRFID (will also check for duplicate filenames) */
		auto found = std::ranges::find_if(list, [&c](const auto &gc) { return gc->ident.grfid == c->ident.grfid; });
		if (found != std::end(list)) {
			ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR),
				GetEncodedString(STR_CONFIG_ERROR_DUPLICATE_GRFID, c->filename, (*found)->filename),
				WL_CRITICAL);
			continue;
		}

		if (is_static) {
			/* Mark file as static to avoid saving in savegame. */
			c->flags.Set(GRFConfigFlag::Static);
		} else if (++num_grfs > NETWORK_MAX_GRF_COUNT) {
			/* Check we will not load more non-static NewGRFs than allowed. This could trigger issues for game servers. */
			ShowErrorMessage(GetEncodedString(STR_CONFIG_ERROR),
				GetEncodedString(STR_NEWGRF_ERROR_TOO_MANY_NEWGRFS_LOADED), WL_CRITICAL);
			break;
		}

		/* Add item to list */
		list.push_back(std::move(c));
	}

	return list;
}

static IniFileVersion LoadVersionFromConfig(const IniFile &ini)
{
	const IniGroup *group = ini.GetGroup("version");
	if (group == nullptr) return IFV_0;

	auto version_number = group->GetItem("ini_version");
	/* Older ini-file versions don't have this key yet. */
	if (version_number == nullptr || !version_number->value.has_value()) return IFV_0;

	auto version_result = IntFromChars<uint32_t>(*version_number->value);
	if (!version_result.has_value()) return IFV_0;

	return static_cast<IniFileVersion>(*version_result);
}

static void AISaveConfig(IniFile &ini, const char *grpname)
{
	IniGroup &group = ini.GetOrCreateGroup(grpname);
	group.Clear();

	for (CompanyID c = CompanyID::Begin(); c < MAX_COMPANIES; ++c) {
		AIConfig *config = AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME);
		std::string name;
		std::string value = config->SettingsToString();

		if (config->HasScript()) {
			name = config->GetName();
		} else {
			name = "none";
		}

		group.CreateItem(name).SetValue(value);
	}
}

static void GameSaveConfig(IniFile &ini, const char *grpname)
{
	IniGroup &group = ini.GetOrCreateGroup(grpname);
	group.Clear();

	GameConfig *config = GameConfig::GetConfig(AIConfig::SSS_FORCE_NEWGAME);
	std::string name;
	std::string value = config->SettingsToString();

	if (config->HasScript()) {
		name = config->GetName();
	} else {
		name = "none";
	}

	group.CreateItem(name).SetValue(value);
}

/**
 * Save the version of OpenTTD to the ini file.
 * @param ini the ini to write to
 */
static void SaveVersionInConfig(IniFile &ini)
{
	IniGroup &group = ini.GetOrCreateGroup("version");
	group.GetOrCreateItem("version_string").SetValue(_openttd_revision);
	group.GetOrCreateItem("version_number").SetValue(fmt::format("{:08X}", _openttd_newgrf_version));
	group.GetOrCreateItem("ini_version").SetValue(std::to_string(INIFILE_VERSION));
}

/**
 * Save BaseGraphics set selection and configuration.
 */
static void GraphicsSetSaveConfig(IniFile &ini)
{
	const GraphicsSet *used_set = BaseGraphics::GetUsedSet();
	if (used_set == nullptr) return;

	IniGroup &group = ini.GetOrCreateGroup("graphicsset");
	group.Clear();

	group.GetOrCreateItem("name").SetValue(used_set->name);
	group.GetOrCreateItem("shortname").SetValue(fmt::format("{:08X}", std::byteswap(used_set->shortname)));

	const GRFConfig *extra_cfg = used_set->GetExtraConfig();
	if (extra_cfg != nullptr && !extra_cfg->param.empty()) {
		group.GetOrCreateItem("extra_version").SetValue(fmt::format("{}", extra_cfg->version));
		group.GetOrCreateItem("extra_params").SetValue(GRFBuildParamList(*extra_cfg));
	}
}

/* Save a GRF configuration to the given group name */
static void GRFSaveConfig(IniFile &ini, const char *grpname, const GRFConfigList &list)
{
	IniGroup &group = ini.GetOrCreateGroup(grpname);
	group.Clear();

	for (const auto &c : list) {
		/* Hex grfid (4 bytes in nibbles), "|", hex md5sum (16 bytes in nibbles), "|", file system path. */
		format_buffer key;
		key.format("{:08X}|{}|{}", std::byteswap(c->ident.grfid), c->ident.md5sum, c->filename);
		group.GetOrCreateItem(key).SetValue(GRFBuildParamList(*c));
	}
}

/* Common handler for saving/loading variables to the configuration file */
static void HandleSettingDescs(IniFile &generic_ini, SettingDescProc *proc, SettingDescProcList *proc_list, bool only_startup = false)
{
	proc(generic_ini, _misc_settings, "misc", nullptr, only_startup);
#if defined(_WIN32) && !defined(DEDICATED)
	proc(generic_ini, _win32_settings, "win32", nullptr, only_startup);
#endif /* _WIN32 */

	/* The name "patches" is a fallback, as every setting should sets its own group. */

	for (auto &table : _generic_setting_tables) {
		proc(generic_ini, table, "patches", &_settings_newgame, only_startup);
	}

	proc(generic_ini, _currency_settings, "currency", &GetCustomCurrency(), only_startup);
	proc(generic_ini, _company_settings, "company", &_settings_client.company, only_startup);

}

static void HandlePrivateSettingDescs(IniFile &private_ini, SettingDescProc *proc, SettingDescProcList *proc_list, bool only_startup = false)
{
	for (auto &table : _private_setting_tables) {
		proc(private_ini, table, "patches", &_settings_newgame, only_startup);
	}

	if (!only_startup) {
		proc_list(private_ini, "server_bind_addresses", _network_bind_list);
		proc_list(private_ini, "servers", _network_host_list);
		proc_list(private_ini, "bans", _network_ban_list);
		proc_list(private_ini, "server_authorized_keys", _settings_client.network.server_authorized_keys);
		proc_list(private_ini, "rcon_authorized_keys", _settings_client.network.rcon_authorized_keys);
		proc_list(private_ini, "admin_authorized_keys", _settings_client.network.admin_authorized_keys);
		proc_list(private_ini, "settings_authorized_keys", _settings_client.network.settings_authorized_keys);
	}
}

static void HandleSecretsSettingDescs(IniFile &secrets_ini, SettingDescProc *proc, SettingDescProcList *proc_list, bool only_startup = false)
{
	for (auto &table : _secrets_setting_tables) {
		proc(secrets_ini, table, "patches", &_settings_newgame, only_startup);
	}
}

/**
 * Remove all entries from a settings table from an ini-file.
 *
 * This is only useful if those entries are moved to another file, and you
 * want to clean up what is left behind.
 *
 * @param ini The ini file to remove the entries from.
 * @param table The table to look for entries to remove.
 */
static void RemoveEntriesFromIni(IniFile &ini, const SettingTable &table)
{
	for (auto &sd : table) {
		/* For settings.xx.yy load the settings from [xx] yy = ? */
		std::string s{ GetSettingConfigName(*sd) };
		auto sc = s.find('.');
		if (sc == std::string::npos) continue;

		IniGroup *group = ini.GetGroup(s.substr(0, sc));
		if (group == nullptr) continue;
		s = s.substr(sc + 1);

		group->RemoveItem(s);
	}
}

/**
 * Check whether a conversion should be done, and based on what old setting information.
 *
 * To prevent errors when switching back and forth between older and newer
 * version of OpenTTD, the type of a setting is never changed. Instead, the
 * setting is renamed, and this function is used to check whether a conversion
 * between the old and new setting is required.
 *
 * This checks if the new setting doesn't exist, and if the old does.
 *
 * Doing it this way means that if you switch to an older client, the old
 * setting is used, and only on the first time starting a new client, the
 * old setting is converted to the new. After that, they are independent
 * of each other. And you can safely, without errors on either, switch
 * between old and new client.
 *
 * @param ini The ini-file to use.
 * @param group The group the setting is in.
 * @param old_var The old name of the setting.
 * @param new_var The new name of the setting.
 * @param[out] old_item The old item to base upgrading on.
 * @return Whether upgrading should happen; if false, old_item is a nullptr.
 */
bool IsConversionNeeded(const ConfigIniFile &ini, const std::string &group, const std::string &old_var, const std::string &new_var, const IniItem **old_item)
{
	*old_item = nullptr;

	const IniGroup *igroup = ini.GetGroup(group);
	/* If the group doesn't exist, there is nothing to convert. */
	if (igroup == nullptr) return false;

	const IniItem *tmp_old_item = igroup->GetItem(old_var);
	const IniItem *new_item = igroup->GetItem(new_var);

	/* If the old item doesn't exist, there is nothing to convert. */
	if (tmp_old_item == nullptr) return false;

	/* If the new item exists, it means conversion was already done. We only
	 * do the conversion the first time, and after that these settings are
	 * independent. This allows users to freely change between older and
	 * newer clients without breaking anything. */
	if (new_item != nullptr) return false;

	*old_item = tmp_old_item;
	return true;
}

/**
 * Load the values from the configuration files
 * @param startup Load the minimal amount of the configuration to "bootstrap" the blitter and such.
 */
void LoadFromConfig(bool startup)
{
	PreTransparencyOptionSave();

	ConfigIniFile generic_ini(_config_file, &_config_file_text);
	ConfigIniFile private_ini(_private_file);
	ConfigIniFile secrets_ini(_secrets_file);
	ConfigIniFile favs_ini(_favs_file);

	if (!startup) ResetCurrencies(false); // Initialize the array of currencies, without preserving the custom one

	IniFileVersion generic_version = LoadVersionFromConfig(generic_ini);

	if (startup) {
		GraphicsSetLoadConfig(generic_ini);
	}

	HandleSettingDescs(generic_ini, IniLoadSettings, IniLoadSettingList, startup);

	/* Before the split of private/secrets, we have to look in the generic for these settings. */
	if (generic_version < IFV_PRIVATE_SECRETS) {
		HandlePrivateSettingDescs(generic_ini, IniLoadSettings, IniLoadSettingList, startup);
		HandleSecretsSettingDescs(generic_ini, IniLoadSettings, IniLoadSettingList, startup);
	} else {
		HandlePrivateSettingDescs(private_ini, IniLoadSettings, IniLoadSettingList, startup);
		HandleSecretsSettingDescs(secrets_ini, IniLoadSettings, IniLoadSettingList, startup);
	}

	/* Load basic settings only during bootstrap, load other settings not during bootstrap */
	if (!startup) {
		if (generic_version < IFV_LINKGRAPH_SECONDS) {
			_settings_newgame.linkgraph.recalc_interval *= SECONDS_PER_DAY;
			_settings_newgame.linkgraph.recalc_time     *= SECONDS_PER_DAY;
		}

		/* Move use_relay_service from generic_ini to private_ini. */
		if (generic_version < IFV_NETWORK_PRIVATE_SETTINGS) {
			const IniGroup *network = generic_ini.GetGroup("network");
			if (network != nullptr) {
				const IniItem *use_relay_service = network->GetItem("use_relay_service");
				if (use_relay_service != nullptr) {
					if (use_relay_service->value == "never") {
						_settings_client.network.use_relay_service = UseRelayService::URS_NEVER;
					} else if (use_relay_service->value == "ask") {
						_settings_client.network.use_relay_service = UseRelayService::URS_ASK;
					} else if (use_relay_service->value == "allow") {
						_settings_client.network.use_relay_service = UseRelayService::URS_ALLOW;
					}
				}
			}
		}

		const IniItem *old_item;

		if (generic_version < IFV_GAME_TYPE && IsConversionNeeded(generic_ini, "network", "server_advertise", "server_game_type", &old_item)) {
			auto old_value = BoolSettingDesc::ParseSingleValue(old_item->value->c_str());
			_settings_client.network.server_game_type = old_value.value_or(false) ? SERVER_GAME_TYPE_PUBLIC : SERVER_GAME_TYPE_LOCAL;
		}

		if (generic_version < IFV_AUTOSAVE_RENAME && IsConversionNeeded(generic_ini, "gui", "autosave", "autosave_interval", &old_item)) {
			static std::vector<std::string> _old_autosave_interval{"off", "monthly", "quarterly", "half year", "yearly", "custom_days", "custom_realtime_minutes"};
			auto old_value = OneOfManySettingDesc::ParseSingleValue(old_item->value->c_str(), old_item->value->size(), _old_autosave_interval);

			switch (old_value) {
				case 0: _settings_client.gui.autosave_interval = 0; break;
				case 1: _settings_client.gui.autosave_interval = 10; break;
				case 2: _settings_client.gui.autosave_interval = 30; break;
				case 3: _settings_client.gui.autosave_interval = 60; break;
				case 4: _settings_client.gui.autosave_interval = 120; break;
				case 5: {
					const IniItem *old_autosave_custom_days;
					if (IsConversionNeeded(generic_ini, "gui", "autosave_custom_days", "autosave_interval", &old_autosave_custom_days)) {
						_settings_client.gui.autosave_interval = (std::strtoul(old_autosave_custom_days->value->c_str(), nullptr, 10) + 2) / 3;
					}
					break;
				}
				case 6: {
					const IniItem *old_autosave_custom_minutes;
					if (IsConversionNeeded(generic_ini, "gui", "autosave_custom_minutes", "autosave_interval", &old_autosave_custom_minutes)) {
						_settings_client.gui.autosave_interval = std::strtoul(old_autosave_custom_minutes->value->c_str(), nullptr, 10);
					}
					break;
				}
				default: break;
			}
		}

		/* Persist the right click close option from older versions. */
		if (generic_version < IFV_RIGHT_CLICK_CLOSE && IsConversionNeeded(generic_ini, "gui", "right_mouse_wnd_close", "right_click_wnd_close", &old_item)) {
			auto old_value = BoolSettingDesc::ParseSingleValue(old_item->value->c_str());
			_settings_client.gui.right_click_wnd_close = old_value.value_or(false) ? RCC_YES : RCC_NO;
		}

		_grfconfig_newgame = GRFLoadConfig(generic_ini, "newgrf", false);
		_grfconfig_static  = GRFLoadConfig(generic_ini, "newgrf-static", true);
		AILoadConfig(generic_ini, "ai_players");
		GameLoadConfig(generic_ini, "game_scripts");
		PickerLoadConfig(favs_ini);

		PrepareOldDiffCustom();
		IniLoadSettings(generic_ini, _old_gameopt_settings, "gameopt", &_settings_newgame, false);
		HandleOldDiffCustom(false);

		ValidateSettings();
		DebugReconsiderSendRemoteMessages();

		PostZoningModeChange();

		/* Display scheduled errors */
		ScheduleErrorMessage(_settings_error_list);
		if (FindWindowById(WC_ERRMSG, 0) == nullptr) ShowFirstError();
	} else {
		PostTransparencyOptionLoad();
		if (_fallback_gui_zoom_max && _settings_client.gui.zoom_max <= ZOOM_LVL_OUT_8X) {
			_settings_client.gui.zoom_max = ZOOM_LVL_MAX;
		}
	}
}

/** Save the values to the configuration file */
void SaveToConfig(SaveToConfigFlags flags)
{
	if (flags & STCF_PRIVATE) {
		ConfigIniFile private_ini(_private_file);

		/* If we newly create the private/secrets file, add a dummy group on top
		 * just so we can add a comment before it (that is how IniFile works).
		 * This to explain what the file is about. After doing it once, never touch
		 * it again, as otherwise we might be reverting user changes. */
		if (IniGroup *group = private_ini.GetGroup("private"); group != nullptr) group->comment = "; This file possibly contains private information which can identify you as person.\n";

		HandlePrivateSettingDescs(private_ini, IniSaveSettings, IniSaveSettingList);
		SaveVersionInConfig(private_ini);
		private_ini.SaveToDisk(_private_file);
	}

	if (flags & STCF_SECRETS) {
		ConfigIniFile secrets_ini(_secrets_file);

		/* If we newly create the private/secrets file, add a dummy group on top
		 * just so we can add a comment before it (that is how IniFile works).
		 * This to explain what the file is about. After doing it once, never touch
		 * it again, as otherwise we might be reverting user changes. */
		if (IniGroup *group = secrets_ini.GetGroup("secrets"); group != nullptr) group->comment = "; Do not share this file with others, not even if they claim to be technical support.\n; This file contains saved passwords and other secrets that should remain private to you!\n";

		HandleSecretsSettingDescs(secrets_ini, IniSaveSettings, IniSaveSettingList);
		SaveVersionInConfig(secrets_ini);
		secrets_ini.SaveToDisk(_secrets_file);
	}

	if (flags & STCF_FAVS) {
		ConfigIniFile favs_ini(_favs_file);
		PickerSaveConfig(favs_ini);
		SaveVersionInConfig(favs_ini);
		favs_ini.SaveToDisk(_favs_file);
	}

	if ((flags & STCF_GENERIC) == 0) return;

	PreTransparencyOptionSave();

	ConfigIniFile generic_ini(_config_file);

	IniFileVersion generic_version = LoadVersionFromConfig(generic_ini);

	if (generic_version == IFV_0) {
		/* Remove some obsolete groups. These have all been loaded into other groups. */
		generic_ini.RemoveGroup("patches");
		generic_ini.RemoveGroup("yapf");
		generic_ini.RemoveGroup("gameopt");

		/* Remove all settings from the generic ini that are now in the private ini. */
		generic_ini.RemoveGroup("server_bind_addresses");
		generic_ini.RemoveGroup("servers");
		generic_ini.RemoveGroup("bans");
		for (auto &table : _private_setting_tables) {
			RemoveEntriesFromIni(generic_ini, table);
		}

		/* Remove all settings from the generic ini that are now in the secrets ini. */
		for (auto &table : _secrets_setting_tables) {
			RemoveEntriesFromIni(generic_ini, table);
		}
	}

	if (generic_version < IFV_REMOVE_GENERATION_SEED) {
		IniGroup *game_creation = generic_ini.GetGroup("game_creation");
		if (game_creation != nullptr) {
			game_creation->RemoveItem("generation_seed");
		}
	}

	/* These variables are migrated from generic ini to private ini now. */
	if (generic_version < IFV_NETWORK_PRIVATE_SETTINGS) {
		IniGroup *network = generic_ini.GetGroup("network");
		if (network != nullptr) {
			network->RemoveItem("use_relay_service");
		}
	}

	HandleSettingDescs(generic_ini, IniSaveSettings, IniSaveSettingList);
	GraphicsSetSaveConfig(generic_ini);
	GRFSaveConfig(generic_ini, "newgrf", _grfconfig_newgame);
	GRFSaveConfig(generic_ini, "newgrf-static", _grfconfig_static);
	AISaveConfig(generic_ini, "ai_players");
	GameSaveConfig(generic_ini, "game_scripts");

	SaveVersionInConfig(generic_ini);
	generic_ini.SaveToDisk(_config_file);
}

/**
 * Get the list of known NewGrf presets.
 * @returns List of preset names.
 */
StringList GetGRFPresetList()
{
	StringList list;

	ConfigIniFile ini(_config_file);
	for (const IniGroup &group : ini.groups) {
		if (group.name.compare(0, 7, "preset-") == 0) {
			list.push_back(group.name.substr(7));
		}
	}

	return list;
}

/**
 * Load a NewGRF configuration by preset-name.
 * @param config_name Name of the preset.
 * @return NewGRF configuration.
 * @see GetGRFPresetList
 */
GRFConfigList LoadGRFPresetFromConfig(const char *config_name)
{
	format_buffer section;
	section.format("preset-{}", config_name);

	ConfigIniFile ini(_config_file);
	GRFConfigList config = GRFLoadConfig(ini, section.c_str(), false);

	return config;
}

/**
 * Save a NewGRF configuration with a preset name.
 * @param config_name Name of the preset.
 * @param config      NewGRF configuration to save.
 * @see GetGRFPresetList
 */
void SaveGRFPresetToConfig(const char *config_name, GRFConfigList &config)
{
	format_buffer section;
	section.format("preset-{}", config_name);

	ConfigIniFile ini(_config_file);
	GRFSaveConfig(ini, section.c_str(), config);
	ini.SaveToDisk(_config_file);
}

/**
 * Delete a NewGRF configuration by preset name.
 * @param config_name Name of the preset.
 */
void DeleteGRFPresetFromConfig(const char *config_name)
{
	format_buffer section;
	section.format("preset-{}", config_name);

	ConfigIniFile ini(_config_file);
	ini.RemoveGroup(section.c_str());
	ini.SaveToDisk(_config_file);
}

/**
 * Handle changing a value. This performs validation of the input value and
 * calls the appropriate callbacks, and saves it when the value is changed.
 * @param object The object the setting is in.
 * @param newval The new value for the setting.
 */
void IntSettingDesc::ChangeValue(const void *object, int32_t newval, SaveToConfigFlags ini_save_flags) const
{
	int32_t oldval = this->Read(object);
	this->MakeValueValid(newval);
	if (this->pre_check != nullptr && !this->pre_check(newval)) return;
	if (oldval == newval) return;

	this->Write(object, newval);
	if (this->post_callback != nullptr) this->post_callback(newval);

	if (this->flags.Test(SettingFlag::NoNetwork) || this->flags.Test(SettingFlag::Sandbox)) {
		GamelogStartAction(GLAT_SETTING);
		GamelogSetting(this->name, oldval, newval);
		GamelogStopAction();
	}

	SetWindowClassesDirty(WC_GAME_OPTIONS);
	if (this->flags.Test(SettingFlag::Sandbox)) SetWindowClassesDirty(WC_CHEATS);

	if (_save_config) SaveToConfig(ini_save_flags);
}

/**
 * Given a name of setting, return a setting description from the table.
 * @param name Name of the setting to return a setting description of.
 * @param settings Table to look in for the setting.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c nullptr indicates failure to obtain the description.
 */
static const SettingDesc *GetSettingFromName(std::string_view name, const SettingTable &settings)
{
	/* First check all full names */
	for (auto &sd : settings) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (sd->name == name) return sd.get();
	}

	/* Then check the shortcut variant of the name. */
	for (auto &sd : settings) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		const char *short_name = strchr(sd->name, '.');
		if (short_name != nullptr) {
			short_name++;
			if (short_name == name) return sd.get();
		}
	}

	return nullptr;
}

/**
 * Given a name of setting, return a company setting description of it.
 * @param name  Name of the company setting to return a setting description of.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c nullptr indicates failure to obtain the description.
 */
static const SettingDesc *GetCompanySettingFromName(std::string_view name)
{
	if (name.starts_with("company.")) name.remove_prefix(8);
	return GetSettingFromName(name, _company_settings);
}

/**
 * Given a name of any setting, return any setting description of it.
 * @param name  Name of the setting to return a setting description of.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c nullptr indicates failure to obtain the description.
 */
const SettingDesc *GetSettingFromName(std::string_view name)
{
	for (auto &table : _generic_setting_tables) {
		auto sd = GetSettingFromName(name, table);
		if (sd != nullptr) return sd;
	}
	for (auto &table : _private_setting_tables) {
		auto sd = GetSettingFromName(name, table);
		if (sd != nullptr) return sd;
	}
	for (auto &table : _secrets_setting_tables) {
		auto sd = GetSettingFromName(name, table);
		if (sd != nullptr) return sd;
	}

	return GetCompanySettingFromName(name);
}

SaveToConfigFlags ConfigSaveFlagsFor(const SettingDesc *sd)
{
	if (sd->flags.Test(SettingFlag::Private)) return STCF_PRIVATE;
	if (sd->flags.Test(SettingFlag::Secret)) return STCF_SECRETS;
	return STCF_GENERIC;
}

SaveToConfigFlags ConfigSaveFlagsUsingGameSettingsFor(const SettingDesc *sd)
{
	SaveToConfigFlags flags = ConfigSaveFlagsFor(sd);
	if (_game_mode != GM_MENU && !sd->save.global) flags &= ~STCF_GENERIC;
	return flags;
}

/**
 * Get a collection of settings matching a custom filter.
 * @param func Function to filter each setting.
 * @returns Vector containing the list of collections.
 */
std::vector<const SettingDesc *> GetFilteredSettingCollection(std::function<bool(const SettingDesc &desc)> func)
{
	std::vector<const SettingDesc *> collection;

	IterateSettingsTables([&](const SettingTable &table, void *object) {
		for (const auto &sd : table) {
			if (!func(*sd)) continue;

			collection.push_back(sd.get());
		}
	});

	return collection;
}

/**
 * Network-safe changing of settings (server-only).
 * @param flags operation to perform
 * @param name the name of the setting to change
 * @param value the new value for the setting
 * The new value is properly clamped to its minimum/maximum when setting
 * @return the cost of this operation or an error
 * @see _settings
 */
CommandCost CmdChangeSetting(DoCommandFlags flags, const std::string &name, int32_t value)
{
	if (name.empty()) return CMD_ERROR;
	const SettingDesc *sd = GetSettingFromName(name);

	if (sd == nullptr) return CMD_ERROR;
	if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) return CMD_ERROR;
	if (!sd->IsIntSetting()) return CMD_ERROR;

	if (!sd->IsEditable(true)) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		SCOPE_INFO_FMT([=], "CmdChangeSetting: {} -> {}", sd->name, value);

		sd->AsIntSetting()->ChangeValue(&GetGameSettings(), value, ConfigSaveFlagsUsingGameSettingsFor(sd));
	}

	return CommandCost();
}

/**
 * Change one of the per-company settings.
 * @param flags operation to perform
 * @param name the name of the setting to change
 * @param value the new value for the setting
 * The new value is properly clamped to its minimum/maximum when setting
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeCompanySetting(DoCommandFlags flags, const std::string &name, int32_t value)
{
	if (name.empty()) return CMD_ERROR;
	const SettingDesc *sd = GetCompanySettingFromName(name);

	if (sd == nullptr) return CMD_ERROR;
	if (!sd->IsIntSetting()) return CMD_ERROR;

	if (flags.Test(DoCommandFlag::Execute)) {
		SCOPE_INFO_FMT([=], "CmdChangeCompanySetting: {} -> {}", sd->name, value);

		sd->AsIntSetting()->ChangeValue(&Company::Get(_current_company)->settings, value, STCF_NONE);
	}

	return CommandCost();
}

const char *GetCompanySettingNameByIndex(uint32_t idx)
{
	if (idx >= _company_settings.size()) return nullptr;

	return _company_settings.begin()[idx]->name;
}

/**
 * Top function to save the new value of an element of the Settings struct
 * @param index offset in the SettingDesc array of the Settings struct which
 * identifies the setting member we want to change
 * @param value new value of the setting
 * @param force_newgame force the newgame settings
 */
bool SetSettingValue(const IntSettingDesc *sd, int32_t value, bool force_newgame)
{
	const IntSettingDesc *setting = sd->AsIntSetting();
	if (setting->flags.Test(SettingFlag::PerCompany)) {
		if (Company::IsValidID(_local_company) && _game_mode != GM_MENU) {
			return Command<CMD_CHANGE_COMPANY_SETTING>::Post(setting->name, value);
		} else if (setting->flags.Test(SettingFlag::NoNewgame)) {
			return false;
		}

		setting->ChangeValue(&_settings_client.company, value, ConfigSaveFlagsFor(setting));
		return true;
	}

	/* If an item is company-based, we do not send it over the network
	 * (if any) to change. Also *hack*hack* we update the _newgame version
	 * of settings because changing a company-based setting in a game also
	 * changes its defaults. At least that is the convention we have chosen */
	bool no_newgame = setting->flags.Test(SettingFlag::NoNewgame);
	if (no_newgame && _game_mode == GM_MENU) return false;
	if (setting->flags.Test(SettingFlag::NoNetworkSync)) {
		if (_game_mode != GM_MENU && !no_newgame) {
			setting->ChangeValue(&_settings_newgame, value, ConfigSaveFlagsFor(setting));
		}
		setting->ChangeValue(&GetGameSettings(), value, ConfigSaveFlagsUsingGameSettingsFor(setting));
		return true;
	}

	if (force_newgame && !no_newgame) {
		setting->ChangeValue(&_settings_newgame, value, ConfigSaveFlagsFor(setting));
		return true;
	}

	/* send non-company-based settings over the network */
	if (!IsNonAdminNetworkClient()) {
		return Command<CMD_CHANGE_SETTING>::Post(setting->name, value);
	}
	return false;
}

/**
 * Set the company settings for a new company to their default values.
 */
void SetDefaultCompanySettings(CompanyID cid)
{
	Company *c = Company::Get(cid);
	AutoRestoreBackup backup(_current_company, cid);
	for (auto &sd : _company_settings) {
		if (sd->IsIntSetting()) {
			const IntSettingDesc *int_setting = sd->AsIntSetting();
			int_setting->MakeValueValidAndWrite(&c->settings, int_setting->GetDefaultValue());
		}
	}
}

/**
 * Sync all company settings in a multiplayer game.
 */
void SyncCompanySettings()
{
	const void *old_object = &Company::Get(_current_company)->settings;
	const void *new_object = &_settings_client.company;
	for (auto &sd : _company_settings) {
		if (!sd->IsIntSetting()) continue;
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		uint32_t old_value = (uint32_t)sd->AsIntSetting()->Read(old_object);
		uint32_t new_value = (uint32_t)sd->AsIntSetting()->Read(new_object);
		if (old_value != new_value) {
			NetworkSendCommand<CMD_CHANGE_COMPANY_SETTING>({}, ChangeSettingCmdData::Make(sd->name, new_value), (StringID)0, CommandCallback::None, 0, _local_company);
		}
	}
}

/**
 * Set a setting value with a string.
 * @param sd the setting to change.
 * @param value the value to write
 * @param force_newgame force the newgame settings
 * @note Strings WILL NOT be synced over the network
 */
bool SetSettingValue(const StringSettingDesc *sd, std::string value, bool force_newgame)
{
	assert(sd->flags.Test(SettingFlag::NoNetworkSync));

	if (GetVarMemType(sd->save.conv) == SLE_VAR_STRQ && value.compare("(null)") == 0) {
		value.clear();
	}

	const void *object = (_game_mode == GM_MENU || force_newgame) ? &_settings_newgame : &_settings_game;
	sd->AsStringSetting()->ChangeValue(object, value, object == &_settings_newgame ? ConfigSaveFlagsFor(sd) : STCF_NONE);
	return true;
}

/**
 * Handle changing a string value. This performs validation of the input value
 * and calls the appropriate callbacks, and saves it when the value is changed.
 * @param object The object the setting is in.
 * @param newval The new value for the setting.
 */
void StringSettingDesc::ChangeValue(const void *object, std::string &newval, SaveToConfigFlags ini_save_flags) const
{
	this->MakeValueValid(newval);
	if (this->pre_check != nullptr && !this->pre_check(newval)) return;

	this->Write(object, newval);
	if (this->post_callback != nullptr) this->post_callback(newval);

	if (_save_config) SaveToConfig(ini_save_flags);
}

uint GetSettingIndexByFullName(const SettingTable &table, const char *name)
{
	uint index = 0;
	for (auto &sd : table) {
		if (sd->name != nullptr && strcmp(sd->name, name) == 0) return index;
		index++;
	}
	return UINT32_MAX;
}

/* Those 2 functions need to be here, else we have to make some stuff non-static
 * and besides, it is also better to keep stuff like this at the same place */
void IConsoleSetSetting(const char *name, const char *value, bool force_newgame)
{
	const SettingDesc *sd = GetSettingFromName(name);
	/* Company settings are not in "list_settings", so don't try to modify them. */
	if (sd == nullptr || sd->flags.Test(SettingFlag::PerCompany) || (sd->flags.Test(SettingFlag::NoNewgame) && (_game_mode == GM_MENU || force_newgame))) {
		IConsolePrint(CC_ERROR, "'{}' is an unknown setting.", name);
		return;
	}

	const auto old_game_mode = _game_mode;
	if (force_newgame) _game_mode = GM_MENU;
	auto guard = scope_guard([force_newgame, old_game_mode]() {
		if (force_newgame) _game_mode = old_game_mode;
	});

	bool success = true;
	if (sd->IsStringSetting()) {
		success = SetSettingValue(sd->AsStringSetting(), value, force_newgame);
	} else if (sd->IsIntSetting()) {
		const IntSettingDesc *isd = sd->AsIntSetting();
		size_t val = isd->ParseValue(value);
		if (!_settings_error_list.empty()) {
			IConsolePrint(CC_ERROR, "'{}' is not a valid value for this setting.", value);
			_settings_error_list.clear();
			return;
		}
		success = SetSettingValue(isd, (int32_t)val, force_newgame);
	}

	if (!success) {
		if (IsNetworkSettingsAdmin()) {
			IConsolePrint(CC_ERROR, "This command/variable is not available during network games.");
		} else {
			IConsolePrint(CC_ERROR, "This command/variable is only available to a network server.");
		}
	}
}

void IConsoleSetSetting(const char *name, int value)
{
	const SettingDesc *sd = GetSettingFromName(name);
	assert(sd != nullptr);
	SetSettingValue(sd->AsIntSetting(), value);
}

/**
 * Output value of a specific setting to the console
 * @param name  Name of the setting to output its value
 * @param force_newgame force the newgame settings
 */
void IConsoleGetSetting(const char *name, bool force_newgame)
{
	const SettingDesc *sd = GetSettingFromName(name);
	/* Company settings are not in "list_settings", so don't try to read them. */
	if (sd == nullptr || sd->flags.Test(SettingFlag::PerCompany) || (sd->flags.Test(SettingFlag::NoNewgame) && (_game_mode == GM_MENU || force_newgame))) {
		IConsolePrint(CC_ERROR, "'{}' is an unknown setting.", name);
		return;
	}

	const void *object = (_game_mode == GM_MENU || force_newgame) ? &_settings_newgame : &_settings_game;

	if (sd->IsStringSetting()) {
		IConsolePrint(CC_WARNING, "Current value for '{}' is: '{}'", name, sd->AsStringSetting()->Read(object));
	} else if (sd->IsIntSetting()) {
		const IntSettingDesc *int_setting = sd->AsIntSetting();

		bool show_min_max = true;
		auto [min_val, max_val] = int_setting->GetRange();
		if (sd->flags.Test(SettingFlag::Enum)) {
			int64_t min_value = INT64_MAX;
			int64_t max_value = INT64_MIN;
			int count = 0;
			for (const SettingDescEnumEntry *enumlist = int_setting->enumlist; enumlist != nullptr && enumlist->str != STR_NULL; enumlist++) {
				if (enumlist->val < min_value) min_value = enumlist->val;
				if (enumlist->val > max_value) max_value = enumlist->val;
				count++;
			}
			if (max_value - min_value != (int64_t)(count - 1)) {
				/* Discontinuous range */
				show_min_max = false;
			} else {
				min_val = min_value;
				max_val = (uint64_t)max_value;
			}
		}

		format_buffer value;
		sd->FormatValue(value, object);

		if (show_min_max) {
			IConsolePrint(CC_WARNING, "Current value for '{}' is: '{}' (min: {}{}, max: {})",
				name, value, (sd->flags.Test(SettingFlag::GuiZeroIsSpecial)) ? "(0) " : "", min_val, max_val);
		} else {
			IConsolePrint(CC_WARNING, "Current value for '{}' is: '{}'",
				name, value);
		}
	}
}

static void IConsoleListSettingsTable(const SettingTable &table, const char *prefilter, bool show_defaults)
{
	for (auto &sd : table) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (prefilter != nullptr && strstr(sd->name, prefilter) == nullptr) continue;
		if (sd->flags.Test(SettingFlag::NoNewgame) && _game_mode == GM_MENU) continue;
		format_buffer value;
		sd->FormatValue(value, &GetGameSettings());
		if (show_defaults && sd->IsIntSetting()) {
			const IntSettingDesc *int_setting = sd->AsIntSetting();
			auto def = int_setting->GetDefaultValue();
			format_buffer defvalue;
			int_setting->FormatIntValue(defvalue, def);
			TextColour colour = (int_setting->Read(&GetGameSettings()) != def) ? CC_WARNING : CC_DEFAULT;
			IConsolePrint(colour, "{} = {} (default: {})", sd->name, value, defvalue);
		} else {
			IConsolePrint(CC_DEFAULT, "{} = {}", sd->name, value);
		}
	}
}

/**
 * List all settings and their value to the console
 *
 * @param prefilter  If not \c nullptr, only list settings with names that begin with \a prefilter prefix
 */
void IConsoleListSettings(const char *prefilter, bool show_defaults)
{
	IConsolePrint(CC_WARNING, "All settings with their current {}:", show_defaults ? "and default values" : "value");

	for (auto &table : _generic_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}
	for (auto &table : _private_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}
	for (auto &table : _secrets_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}

	IConsolePrint(CC_WARNING, "Use 'setting' command to change a value");
}

struct LoadSettingsItem {
	SettingsCompat compat;
	const SettingDesc *setting;
};
std::vector<LoadSettingsItem> _gameopt_compat_items;
std::vector<LoadSettingsItem> _settings_compat_items;

/**
 * Load handler for settings from old-style non-table OPTS and PATS chunks
 * @param settings SettingDesc struct containing all information
 * @param compat Compatibility table
 * @param items Load items (filled in on first run)
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettings(std::initializer_list<SettingTable> settings, std::initializer_list<SettingsCompat> compat, std::vector<LoadSettingsItem> &items, void *object)
{
	if (items.empty()) {
		/* Populate setting references */

		btree::btree_multimap<std::string_view, const SettingDesc *> names;
		for (auto &osd : IterateSettingTables(settings)) {
			if (osd->flags.Test(SettingFlag::NotInSave)) continue;
			if (osd->name == nullptr) continue;
			names.insert({osd->name, osd.get()});
		}

		for (const SettingsCompat &c : compat) {
			if (c.type == SettingsCompatType::Setting || c.type == SettingsCompatType::Xref) {
				auto iters = names.equal_range(c.name);
				assert_msg(iters.first != iters.second, "Setting: {}", c.name);
				for (auto it = iters.first; it != iters.second; ++it) {
					items.push_back({ c, it->second });
				}
			} else {
				items.push_back({ c, nullptr });
			}
		}
	}

	extern SaveLoadVersion _sl_version;

	for (LoadSettingsItem &item : items) {
		switch (item.compat.type) {
			case SettingsCompatType::Null:
				if (item.compat.ext_feature_test.IsFeaturePresent(_sl_version, item.compat.version_from, item.compat.version_to)) SlSkipBytes(item.compat.length);
				break;
			case SettingsCompatType::Setting:
				if (!SlObjectMember(object, item.setting->save)) continue;
				if (item.setting->IsIntSetting()) {
					const IntSettingDesc *int_setting = item.setting->AsIntSetting();
					int_setting->MakeValueValidAndWrite(object, int_setting->Read(object));
				}
				break;
			case SettingsCompatType::Xref:
				if (item.compat.ext_feature_test.IsFeaturePresent(_sl_version, item.compat.version_from, item.compat.version_to)) {
					Debug(sl, 3, "PATS chunk: Loading xref setting: '{}'", item.compat.name);

					/* Generate a new SaveLoad from the xref target using the version params from the source */
					SaveLoad sld = item.setting->save;
					sld.version_from     = item.compat.version_from;
					sld.version_to       = item.compat.version_to;
					sld.ext_feature_test = item.compat.ext_feature_test;

					if (!SlObjectMember(object, sld)) continue;
					if (item.setting->IsIntSetting()) {
						const IntSettingDesc *int_setting = item.setting->AsIntSetting();
						int64_t val = int_setting->Read(object);
						if (item.compat.xrefconv != nullptr) val = item.compat.xrefconv(val);
						int_setting->MakeValueValidAndWrite(object, val);
					}
				}
				break;
		}
	}
}

/** @file
 *
 * The PATX chunk stores additional settings in an unordered format
 * which is tolerant of extra, missing or reordered settings.
 * Additional settings generally means those that aren't in trunk.
 *
 * The PATX chunk contents has the following format:
 *
 * uint32_t                             chunk flags (unused)
 * uint32_t                             number of settings
 *     For each of N settings:
 *     uint32_t                         setting flags (unused)
 *     SLE_STR                          setting name
 *     uint32_t                         length of setting field
 *         N bytes                      setting field
 */

/**
 * Prepare a sorted list of settings to be potentially be loaded out of the PATX chunk
 * This is to enable efficient lookup of settings by name
 */
static std::vector<const SettingDesc *> MakeSettingsPatxList(std::initializer_list<SettingTable> settings)
{
	std::vector<const SettingDesc *> sorted_patx_settings;

	for (auto &sd : IterateSettingTables(settings)) {
		if (sd->patx_name == nullptr) continue;
		sorted_patx_settings.push_back(sd.get());
	}

	std::sort(sorted_patx_settings.begin(), sorted_patx_settings.end(), [](const SettingDesc *a, const SettingDesc *b) {
		return strcmp(a->patx_name, b->patx_name) < 0;
	});

	return sorted_patx_settings;
}

/**
 * Internal structure used in LoadSettingsPatx() and LoadSettingsPlyx()
 */
struct SettingsExtLoad {
	uint32_t flags;
	std::string name;
	uint32_t setting_length;
};

static const SaveLoad _settings_ext_load_desc[] = {
	SLE_VAR(SettingsExtLoad, flags,          SLE_UINT32),
	SLE_SSTR(SettingsExtLoad, name,          SLE_STR),
	SLE_VAR(SettingsExtLoad, setting_length, SLE_UINT32),
};

/**
 * Load handler for settings which go in the PATX chunk
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettingsPatx(void *object)
{
	static std::vector<const SettingDesc *> sorted_patx_settings;
	if (sorted_patx_settings.empty()) {
		sorted_patx_settings = MakeSettingsPatxList(_saveload_setting_tables);
	}

	SettingsExtLoad current_setting;

	uint32_t flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (flags != 0) SlErrorCorruptFmt("PATX chunk: unknown chunk header flags: 0x{:X}", flags);

	uint32_t settings_count = SlReadUint32();
	for (uint32_t i = 0; i < settings_count; i++) {
		SlObject(&current_setting, _settings_ext_load_desc);

		// flags are not in use yet, reserve for future expansion
		if (current_setting.flags != 0) SlErrorCorruptFmt("PATX chunk: unknown setting header flags: 0x{:X}", current_setting.flags);

		// now try to find corresponding setting
		bool exact_match = false;
		auto iter = std::lower_bound(sorted_patx_settings.begin(), sorted_patx_settings.end(), current_setting.name.c_str(), [&](const SettingDesc *a, const char *b) {
			int result = strcmp(a->patx_name, b);
			if (result == 0) exact_match = true;
			return result < 0;
		});

		if (exact_match) {
			assert(iter != sorted_patx_settings.end());
			// found setting
			const SettingDesc *setting = (*iter);
			const SaveLoad &sld = setting->save;
			size_t read = SlGetBytesRead();
			SlObjectMember(object, sld);
			if (SlGetBytesRead() != read + current_setting.setting_length) {
				SlErrorCorruptFmt("PATX chunk: setting read length mismatch for setting: '{}'", current_setting.name);
			}
			if (setting->IsIntSetting()) {
				const IntSettingDesc *int_setting = setting->AsIntSetting();
				int_setting->MakeValueValidAndWrite(object, int_setting->Read(object));
			}
		} else {
			Debug(sl, 1, "PATX chunk: Could not find setting: '{}', ignoring", current_setting.name);
			SlSkipBytes(current_setting.setting_length);
		}
	}
}

/** @file
 *
 * The PLYX chunk stores additional company settings in an unordered
 * format which is tolerant of extra, missing or reordered settings.
 * The format is similar to the PATX chunk.
 * Additional settings generally means those that aren't in trunk.
 *
 * The PLYX chunk contents has the following format:
 *
 * uint32_t                             chunk flags (unused)
 * uint32_t                             number of companies
 *     For each of N companies:
 *     uint32_t                         company ID
 *     uint32_t                         company flags (unused)
 *     uint32_t                         number of settings
 *         For each of N settings:
 *         uint32_t                     setting flags (unused)
 *         SLE_STR                      setting name
 *         uint32_t                     length of setting field
 *             N bytes                  setting field
 */

/**
 * Load handler for company settings which go in the PLYX chunk
 * @param check_mode Whether to skip over settings without reading
 */
void LoadSettingsPlyx(bool skip)
{
	SettingsExtLoad current_setting;

	uint32_t chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown chunk header flags: 0x{:X}", chunk_flags);

	uint32_t company_count = SlReadUint32();
	for (uint32_t i = 0; i < company_count; i++) {
		uint32_t company_id = SlReadUint32();
		if (company_id >= MAX_COMPANIES) SlErrorCorruptFmt("PLYX chunk: invalid company ID: {}", company_id);

		const Company *c = nullptr;
		if (!skip) {
			c = Company::GetIfValid(company_id);
			if (c == nullptr) SlErrorCorruptFmt("PLYX chunk: non-existent company ID: {}", company_id);
		}

		uint32_t company_flags = SlReadUint32();
		// flags are not in use yet, reserve for future expansion
		if (company_flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown company flags: 0x{:X}", company_flags);

		uint32_t settings_count = SlReadUint32();
		for (uint32_t j = 0; j < settings_count; j++) {
			SlObject(&current_setting, _settings_ext_load_desc);

			// flags are not in use yet, reserve for future expansion
			if (current_setting.flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown setting header flags: 0x{:X}", current_setting.flags);

			if (skip) {
				SlSkipBytes(current_setting.setting_length);
				continue;
			}

			const SettingDesc *setting = nullptr;

			// not many company settings, so perform a linear scan
			for (auto &sd : _company_settings) {
				if (sd->patx_name != nullptr && current_setting.name == sd->patx_name) {
					setting = sd.get();
					break;
				}
			}

			if (setting != nullptr) {
				// found setting
				const SaveLoad &sld = setting->save;
				size_t read = SlGetBytesRead();
				SlObjectMember(const_cast<CompanySettings *>(&(c->settings)), sld);
				if (SlGetBytesRead() != read + current_setting.setting_length) {
					SlErrorCorruptFmt("PLYX chunk: setting read length mismatch for setting: '{}'", current_setting.name);
				}
				if (setting->IsIntSetting()) {
					const IntSettingDesc *int_setting = setting->AsIntSetting();
					int_setting->MakeValueValidAndWrite(&(c->settings), int_setting->Read(&(c->settings)));
				}
			} else {
				Debug(sl, 1, "PLYX chunk: Could not find company setting: '{}', ignoring", current_setting.name);
				SlSkipBytes(current_setting.setting_length);
			}
		}
	}
}

std::vector<NamedSaveLoad> FillPlyrExtraSettingsDesc()
{
	std::vector<NamedSaveLoad> settings_desc;

	for (auto &sd : _company_settings) {
		if (sd->patx_name != nullptr) {
			settings_desc.push_back(NSL(sd->patx_name, sd->save));
		}
	}

	return settings_desc;
}

static void Load_OPTS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * autosave-frequency stays when joining a network-server */
	PrepareOldDiffCustom();
	LoadSettings({ _old_gameopt_settings }, _gameopt_compat, _gameopt_compat_items, &_settings_game);
	HandleOldDiffCustom(true);
}

static void Load_PATS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * currency setting stays when joining a network-server */
	LoadSettings(_saveload_setting_tables, _settings_compat, _settings_compat_items, &_settings_game);
}

static void Check_PATS()
{
	LoadSettings(_saveload_setting_tables, _settings_compat, _settings_compat_items, &_load_check_data.settings);
}

static void Load_PATX()
{
	LoadSettingsPatx(&_settings_game);
}

static void Check_PATX()
{
	LoadSettingsPatx(&_load_check_data.settings);
}

static const ChunkHandler setting_chunk_handlers[] = {
	{ 'OPTS', nullptr,   Load_OPTS, nullptr, nullptr,    CH_READONLY },
	MakeSaveUpstreamFeatureConditionalLoadUpstreamChunkHandler<'PATS', XSLFI_TABLE_PATS>(Load_PATS, nullptr, Check_PATS),
	{ 'PATX', nullptr,   Load_PATX, nullptr, Check_PATX, CH_READONLY },
};

extern const ChunkHandlerTable _setting_chunk_handlers(setting_chunk_handlers);

static bool IsSignedVarMemType(VarType vt)
{
	switch (GetVarMemType(vt)) {
		case SLE_VAR_I8:
		case SLE_VAR_I16:
		case SLE_VAR_I32:
		case SLE_VAR_I64:
			return true;
	}
	return false;
}

void SetupTimeSettings()
{
	_settings_time = (_game_mode == GM_MENU || _settings_client.gui.override_time_settings) ? _settings_client.gui : _settings_game.game_time;
}

std::initializer_list<SettingTable> GetSaveLoadSettingsTables()
{
	return _saveload_setting_tables;
}

const SettingTable &GetLinkGraphSettingTable()
{
	return _linkgraph_settings;
}

void ResetSettingsToDefaultForLoad()
{
	for (auto &sd : IterateSettingTables(GetSaveLoadSettingsTables())) {
		if (sd->flags.Test(SettingFlag::NotInSave)) continue;
		if (sd->flags.Test(SettingFlag::NoNetworkSync) && _networking && !_network_server) continue;

		sd->ResetToDefault(&_settings_game);
	}
}
