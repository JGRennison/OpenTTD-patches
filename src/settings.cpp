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

#include "void_map.h"
#include "station_base.h"
#include "infrastructure_func.h"

#if defined(WITH_FREETYPE) || defined(_WIN32) || defined(WITH_COCOA)
#define HAS_TRUETYPE_FONT
#endif

#include "sl/saveload.h"

#include "table/strings.h"
#include "table/settings.h"

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
	_settings,
	_network_settings,
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
	handler(_currency_settings, &_custom_currency);
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
static bool DecodeHexText(const char *pos, uint8 *dest, size_t dest_size);


/**
 * IniFile to store a configuration.
 */
class ConfigIniFile : public IniFile {
private:
	inline static const char * const list_group_names[] = {
		"bans",
		"newgrf",
		"servers",
		"server_bind_addresses",
		nullptr,
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
enum IniFileVersion : uint32 {
	IFV_0,                                                 ///< 0  All versions prior to introduction.
	IFV_PRIVATE_SECRETS,                                   ///< 1  PR#9298  Moving of settings from openttd.cfg to private.cfg / secrets.cfg.
	IFV_GAME_TYPE,                                         ///< 2  PR#9515  Convert server_advertise to server_game_type.
	IFV_LINKGRAPH_SECONDS,                                 ///< 3  PR#10610 Store linkgraph update intervals in seconds instead of days.
	IFV_NETWORK_PRIVATE_SETTINGS,                          ///< 4  PR#10762 Move no_http_content_downloads / use_relay_service to private settings.

	IFV_MAX_VERSION,       ///< Highest possible ini-file version.
};

const uint16 INIFILE_VERSION = (IniFileVersion)(IFV_MAX_VERSION - 1); ///< Current ini-file version of OpenTTD.

/**
 * Get the setting at the given index into the settings table.
 * @param index The index to look for.
 * @return The setting at the given index, or nullptr when the index is invalid.
 */
const SettingDesc *GetSettingDescription(uint index)
{
	if (index >= _settings.size()) return nullptr;
	return _settings.begin()[index].get();
}

/**
 * Find the index value of a ONEofMANY type in a string separated by |
 * @param str the current value of the setting for which a value needs found
 * @param len length of the string
 * @param many full domain of values the ONEofMANY setting can have
 * @return the integer index of the full-list, or -1 if not found
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

	return (size_t)-1;
}

/**
 * Find the set-integer value MANYofMANY type in a string
 * @param many full domain of values the MANYofMANY setting can have
 * @param str the current string value of the setting, each individual
 * of separated by a whitespace,tab or | character
 * @return the 'fully' set integer, or -1 if a set is not found
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
		if (r == (size_t)-1) return r;

		SetBit(res, (uint8)r); // value found, set it
		if (*s == 0) break;
		str = s + 1;
	}
	return res;
}

/**
 * Parse an integerlist string and set each found value
 * @param p the string to be parsed. Each element in the list is separated by a
 * comma or a space character
 * @param items pointer to the integerlist-array that will be filled with values
 * @param maxitems the maximum number of elements the integerlist-array has
 * @return returns the number of items found, or -1 on an error
 */
template<typename T>
static int ParseIntList(const char *p, T *items, size_t maxitems)
{
	size_t n = 0; // number of items read so far
	bool comma = false; // do we accept comma?

	while (*p != '\0') {
		switch (*p) {
			case ',':
				/* Do not accept multiple commas between numbers */
				if (!comma) return -1;
				comma = false;
				FALLTHROUGH;

			case ' ':
				p++;
				break;

			default: {
				if (n == maxitems) return -1; // we don't accept that many numbers
				char *end;
				unsigned long v = std::strtoul(p, &end, 0);
				if (p == end) return -1; // invalid character (not a number)
				if (sizeof(T) < sizeof(v)) v = Clamp<unsigned long>(v, std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
				items[n++] = v;
				p = end; // first non-number
				comma = true; // we accept comma now
				break;
			}
		}
	}

	/* If we have read comma but no number after it, fail.
	 * We have read comma when (n != 0) and comma is not allowed */
	if (n != 0 && !comma) return -1;

	return ClampTo<int>(n);
}

/**
 * Load parsed string-values into an integer-array (intlist)
 * @param str the string that contains the values (and will be parsed)
 * @param array pointer to the integer-arrays that will be filled
 * @param nelems the number of elements the array holds. Maximum is 64 elements
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.)
 * @return return true on success and false on error
 */
static bool LoadIntList(const char *str, void *array, int nelems, VarType type)
{
	unsigned long items[64];
	int i, nitems;

	if (str == nullptr) {
		memset(items, 0, sizeof(items));
		nitems = nelems;
	} else {
		nitems = ParseIntList(str, items, lengthof(items));
		if (nitems != nelems) return false;
	}

	switch (type) {
		case SLE_VAR_BL:
		case SLE_VAR_I8:
		case SLE_VAR_U8:
			for (i = 0; i != nitems; i++) ((byte*)array)[i] = items[i];
			break;

		case SLE_VAR_I16:
		case SLE_VAR_U16:
			for (i = 0; i != nitems; i++) ((uint16*)array)[i] = items[i];
			break;

		case SLE_VAR_I32:
		case SLE_VAR_U32:
			for (i = 0; i != nitems; i++) ((uint32*)array)[i] = items[i];
			break;

		default: NOT_REACHED();
	}

	return true;
}

/**
 * Convert an integer-array (intlist) to a string representation. Each value
 * is separated by a comma or a space character
 * @param buf output buffer where the string-representation will be stored
 * @param last last item to write to in the output buffer
 * @param array pointer to the integer-arrays that is read from
 * @param nelems the number of elements the array holds.
 * @param type the type of elements the array holds (eg INT8, UINT16, etc.)
 */
void ListSettingDesc::FormatValue(char *buf, const char *last, const void *object) const
{
	const byte *p = static_cast<const byte *>(GetVariableAddress(object, this->save));
	int i, v = 0;

	for (i = 0; i != this->save.length; i++) {
		switch (GetVarMemType(this->save.conv)) {
			case SLE_VAR_BL:
			case SLE_VAR_I8:  v = *(const   int8 *)p; p += 1; break;
			case SLE_VAR_U8:  v = *(const  uint8 *)p; p += 1; break;
			case SLE_VAR_I16: v = *(const  int16 *)p; p += 2; break;
			case SLE_VAR_U16: v = *(const uint16 *)p; p += 2; break;
			case SLE_VAR_I32: v = *(const  int32 *)p; p += 4; break;
			case SLE_VAR_U32: v = *(const uint32 *)p; p += 4; break;
			default: NOT_REACHED();
		}
		if (IsSignedVarMemType(this->save.conv)) {
			buf += seprintf(buf, last, (i == 0) ? "%d" : ",%d", v);
		} else {
			buf += seprintf(buf, last, (i == 0) ? "%u" : ",%u", v);
		}
	}
}

char *OneOfManySettingDesc::FormatSingleValue(char *buf, const char *last, uint id) const
{
	if (id >= this->many.size()) {
		return buf + seprintf(buf, last, "%d", id);
	}
	return strecpy(buf, this->many[id].c_str(), last);
}

void OneOfManySettingDesc::FormatIntValue(char *buf, const char *last, uint32 value) const
{
	this->FormatSingleValue(buf, last, value);
}

void ManyOfManySettingDesc::FormatIntValue(char *buf, const char *last, uint32 value) const
{
	uint bitmask = (uint)value;
	if (bitmask == 0) {
		buf[0] = '\0';
		return;
	}
	bool first = true;
	for (uint id : SetBitIterator(bitmask)) {
		if (!first) buf = strecpy(buf, "|", last);
		buf = this->FormatSingleValue(buf, last, id);
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
		if (this->flags & SF_CONVERT_BOOL_TO_INT) {
			if (strcmp(str, "true") == 0 || strcmp(str, "on") == 0) return 1;
			if (strcmp(str, "false") == 0 || strcmp(str, "off") == 0) return 0;
		}
		ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_VALUE);
		msg.SetDParamStr(0, str);
		msg.SetDParamStr(1, this->name);
		_settings_error_list.push_back(msg);
		return this->def;
	}
	if (*end != '\0') {
		ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_TRAILING_CHARACTERS);
		msg.SetDParamStr(0, this->name);
		_settings_error_list.push_back(msg);
	}
	return val;
}

size_t OneOfManySettingDesc::ParseValue(const char *str) const
{
	size_t r = OneOfManySettingDesc::ParseSingleValue(str, strlen(str), this->many);
	/* if the first attempt of conversion from string to the appropriate value fails,
		* look if we have defined a converter from old value to new value. */
	if (r == (size_t)-1 && this->many_cnvt != nullptr) r = this->many_cnvt(str);
	if (r != (size_t)-1) return r; // and here goes converted value

	ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_VALUE);
	msg.SetDParamStr(0, str);
	msg.SetDParamStr(1, this->name);
	_settings_error_list.push_back(msg);
	return this->def;
}

size_t ManyOfManySettingDesc::ParseValue(const char *str) const
{
	size_t r = LookupManyOfMany(this->many, str);
	if (r != (size_t)-1) return r;
	ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_VALUE);
	msg.SetDParamStr(0, str);
	msg.SetDParamStr(1, this->name);
	_settings_error_list.push_back(msg);
	return this->def;
}

size_t BoolSettingDesc::ParseValue(const char *str) const
{
	if (strcmp(str, "true") == 0 || strcmp(str, "on") == 0 || strcmp(str, "1") == 0) return true;
	if (strcmp(str, "false") == 0 || strcmp(str, "off") == 0 || strcmp(str, "0") == 0) return false;

	ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_VALUE);
	msg.SetDParamStr(0, str);
	msg.SetDParamStr(1, this->name);
	_settings_error_list.push_back(msg);
	return this->def;
}

static bool ValidateEnumSetting(const IntSettingDesc *sdb, int32 &val)
{
	if (sdb->flags & SF_ENUM_PRE_CB_VALIDATE && sdb->pre_check != nullptr && !sdb->pre_check(val)) return false;
	for (const SettingDescEnumEntry *enumlist = sdb->enumlist; enumlist != nullptr && enumlist->str != STR_NULL; enumlist++) {
		if (enumlist->val == val) {
			return true;
		}
	}
	return false;
}

/**
 * Make the value valid and then write it to the setting.
 * See #MakeValidValid and #Write for more details.
 * @param object The object the setting is to be saved in.
 * @param val Signed version of the new value.
 */
void IntSettingDesc::MakeValueValidAndWrite(const void *object, int32 val) const
{
	this->MakeValueValid(val);
	this->Write(object, val);
}

/**
 * Make the value valid given the limitations of this setting.
 *
 * In the case of int settings this is ensuring the value is between the minimum and
 * maximum value, with a special case for 0 if SF_GUI_0_IS_SPECIAL is set.
 * This is generally done by clamping the value so it is within the allowed value range.
 * However, for SF_GUI_DROPDOWN the default is used when the value is not valid.
 * @param val The value to make valid.
 */
void IntSettingDesc::MakeValueValid(int32 &val) const
{
	/* We need to take special care of the uint32 type as we receive from the function
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
			if (!(this->flags & SF_GUI_0_IS_SPECIAL) || val != 0) {
				if (this->flags & SF_ENUM) {
					if (!ValidateEnumSetting(this, val)) val = (int32)(size_t)this->def;
				} else if (!(this->flags & SF_GUI_DROPDOWN)) {
					/* Clamp value-type setting to its valid range */
					val = Clamp(val, this->min, this->max);
				} else if (val < this->min || val > (int32)this->max) {
					/* Reset invalid discrete setting (where different values change gameplay) to its default value */
					val = this->def;
				}
			}
			break;
		}
		case SLE_VAR_U32: {
			/* Override the minimum value. No value below this->min, except special value 0 */
			uint32 uval = (uint32)val;
			if (!(this->flags & SF_GUI_0_IS_SPECIAL) || uval != 0) {
				if (this->flags & SF_ENUM) {
					if (!ValidateEnumSetting(this, val)) {
						uval = (uint32)(size_t)this->def;
					} else {
						uval = (uint32)val;
					}
				} else if (!(this->flags & SF_GUI_DROPDOWN)) {
					/* Clamp value-type setting to its valid range */
					uval = ClampU(uval, this->min, this->max);
				} else if (uval < (uint)this->min || uval > this->max) {
					/* Reset invalid discrete setting to its default value */
					uval = (uint32)this->def;
				}
			}
			val = (int32)uval;
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
void IntSettingDesc::Write(const void *object, int32 val) const
{
	void *ptr = GetVariableAddress(object, this->save);
	WriteValue(ptr, this->save.conv, (int64)val);
}

/**
 * Read the integer from the the actual setting.
 * @param object The object the setting is to be saved in.
 * @return The value of the saved integer.
 */
int32 IntSettingDesc::Read(const void *object) const
{
	void *ptr = GetVariableAddress(object, this->save);
	return (int32)ReadValue(ptr, this->save.conv);
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
	std::string stdstr(str, this->max_length - 1);
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
	IniGroup *group;
	IniGroup *group_def = ini.GetGroup(grpname);

	for (auto &sd : settings_table) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (sd->startup != only_startup) continue;
		IniItem *item;
		if (sd->flags & SF_NO_NEWGAME) {
			item = nullptr;
		} else {
			/* For settings.xx.yy load the settings from [xx] yy = ? */
			std::string s{ GetSettingConfigName(*sd) };
			auto sc = s.find('.');
			if (sc != std::string::npos) {
				group = ini.GetGroup(s.substr(0, sc));
				s = s.substr(sc + 1);
			} else {
				group = group_def;
			}

			item = group->GetItem(s, false);
			if (item == nullptr && group != group_def) {
				/* For settings.xx.yy load the settings from [settings] yy = ? in case the previous
				 * did not exist (e.g. loading old config files with a [settings] section */
				item = group_def->GetItem(s, false);
			}
			if (item == nullptr) {
				/* For settings.xx.zz.yy load the settings from [zz] yy = ? in case the previous
				 * did not exist (e.g. loading old config files with a [yapf] section */
				sc = s.find('.');
				if (sc != std::string::npos) item = ini.GetGroup(s.substr(0, sc))->GetItem(s.substr(sc + 1), false);
			}
			if (item == nullptr && sd->guiproc != nullptr) {
				SettingOnGuiCtrlData data;
				data.type = SOGCT_CFG_FALLBACK_NAME;
				if (sd->guiproc(data)) {
					item = group->GetItem(data.str, false);
				}
			}
		}

		sd->ParseValue(item, object);
	}
}

void IntSettingDesc::ParseValue(const IniItem *item, void *object) const
{
	size_t val = (item == nullptr) ? this->def : this->ParseValue(item->value.has_value() ? item->value->c_str() : "");
	this->MakeValueValidAndWrite(object, (int32)val);
}

void StringSettingDesc::ParseValue(const IniItem *item, void *object) const
{
	std::string str = (item == nullptr) ? this->def : item->value.value_or("");
	this->MakeValueValid(str);
	if (this->flags & SF_RUN_CALLBACKS_ON_PARSE) {
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
		ErrorMessageData msg(STR_CONFIG_ERROR, STR_CONFIG_ERROR_ARRAY);
		msg.SetDParamStr(0, this->name);
		_settings_error_list.push_back(msg);

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
	IniItem *item;
	char buf[512];

	for (auto &sd : settings_table) {
		/* If the setting is not saved to the configuration
		 * file, just continue with the next setting */
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (sd->flags & SF_NOT_IN_CONFIG) continue;
		if (sd->flags & SF_NO_NEWGAME) continue;

		/* XXX - wtf is this?? (group override?) */
		std::string s{ GetSettingConfigName(*sd) };
		auto sc = s.find('.');
		if (sc != std::string::npos) {
			group = ini.GetGroup(s.substr(0, sc));
			s = s.substr(sc + 1);
		} else {
			if (group_def == nullptr) group_def = ini.GetGroup(grpname);
			group = group_def;
		}

		item = group->GetItem(s, true);

		if (!item->value.has_value() || !sd->IsSameValue(item, object)) {
			/* Value has changed, get the new value and put it into a buffer */
			sd->FormatValue(buf, lastof(buf), object);

			/* The value is different, that means we have to write it to the ini */
			item->value.emplace(buf);
		}
	}
}

void IntSettingDesc::FormatValue(char *buf, const char *last, const void *object) const
{
	uint32 i = (uint32)this->Read(object);
	this->FormatIntValue(buf, last, i);
}

void IntSettingDesc::FormatIntValue(char *buf, const char *last, uint32 value) const
{
	seprintf(buf, last, IsSignedVarMemType(this->save.conv) ? "%d" : "%u", value);
}

void BoolSettingDesc::FormatIntValue(char *buf, const char *last, uint32 value) const
{
	strecpy(buf, (value != 0) ? "true" : "false", last);
}

bool IntSettingDesc::IsSameValue(const IniItem *item, void *object) const
{
	int32 item_value = (int32)this->ParseValue(item->value->c_str());
	int32 object_value = this->Read(object);
	return item_value == object_value;
}

void StringSettingDesc::FormatValue(char *buf, const char *last, const void *object) const
{
	const std::string &str = this->Read(object);
	switch (GetVarMemType(this->save.conv)) {
		case SLE_VAR_STR: strecpy(buf, str.c_str(), last); break;

		case SLE_VAR_STRQ:
			if (str.empty()) {
				buf[0] = '\0';
			} else {
				seprintf(buf, last, "\"%s\"", str.c_str());
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

bool ListSettingDesc::IsSameValue(const IniItem *item, void *object) const
{
	/* Checking for equality is way more expensive than just writing the value. */
	return false;
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
	IniGroup *group = ini.GetGroup(grpname);

	if (group == nullptr) return;

	list.clear();

	for (const IniItem *item = group->item; item != nullptr; item = item->next) {
		if (!item->name.empty()) list.push_back(item->name);
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
	IniGroup *group = ini.GetGroup(grpname);

	if (group == nullptr) return;
	group->Clear();

	for (const auto &iter : list) {
		group->GetItem(iter.c_str(), true)->SetValue("");
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
	if (!do_command && !(this->flags & SF_NO_NETWORK_SYNC) && _networking && !(_network_server || _network_settings_access) && !(this->flags & SF_PER_COMPANY)) return false;
	if (do_command && (this->flags & SF_NO_NETWORK_SYNC)) return false;
	if ((this->flags & SF_NETWORK_ONLY) && !_networking && _game_mode != GM_MENU) return false;
	if ((this->flags & SF_NO_NETWORK) && _networking) return false;
	if ((this->flags & SF_NEWGAME_ONLY) &&
			(_game_mode == GM_NORMAL ||
			(_game_mode == GM_EDITOR && !(this->flags & SF_SCENEDIT_TOO)))) return false;
	if ((this->flags & SF_SCENEDIT_ONLY) && _game_mode != GM_EDITOR) return false;
	return true;
}

/**
 * Return the type of the setting.
 * @return type of setting
 */
SettingType SettingDesc::GetType() const
{
	if (this->flags & SF_PER_COMPANY) return ST_COMPANY;
	return (this->flags & SF_NOT_IN_SAVE) ? ST_CLIENT : ST_GAME;
}

/**
 * Get the setting description of this setting as an integer setting.
 * @return The integer setting description.
 */
const IntSettingDesc *SettingDesc::AsIntSetting() const
{
	assert_msg(this->IsIntSetting(), "name: %s", this->name);
	return static_cast<const IntSettingDesc *>(this);
}

/**
 * Get the setting description of this setting as a string setting.
 * @return The string setting description.
 */
const StringSettingDesc *SettingDesc::AsStringSetting() const
{
	assert_msg(this->IsStringSetting(), "name: %s", this->name);
	return static_cast<const StringSettingDesc *>(this);
}

/* Begin - Callback Functions for the various settings. */

/** Reposition the main toolbar as the setting changed. */
static void v_PositionMainToolbar(int32 new_value)
{
	if (_game_mode != GM_MENU) PositionMainToolbar(nullptr);
}

/** Reposition the statusbar as the setting changed. */
static void v_PositionStatusbar(int32 new_value)
{
	if (_game_mode != GM_MENU) {
		PositionStatusbar(nullptr);
		PositionNewsMessage(nullptr);
		PositionNetworkChatWindow(nullptr);
	}
}

/**
 * Redraw the smallmap after a colour scheme change.
 * @param p1 Callback parameter.
 */
static void RedrawSmallmap(int32 new_value)
{
	BuildLandLegend();
	BuildOwnerLegend();
	SetWindowClassesDirty(WC_SMALLMAP);

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void StationSpreadChanged(int32 new_value)
{
	InvalidateWindowData(WC_SELECT_STATION, 0);
	InvalidateWindowData(WC_BUILD_STATION, 0);
	InvalidateWindowData(WC_BUS_STATION, 0);
	InvalidateWindowData(WC_TRUCK_STATION, 0);
}

static void UpdateConsists(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		/* Update the consist of all trains so the maximum speed is set correctly. */
		if (t->IsFrontEngine() || t->IsFreeWagon()) {
			t->ConsistChanged(CCF_TRACK);
			if (t->lookahead != nullptr) SetBit(t->lookahead->flags, TRLF_APPLY_ADVISORY);
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
static void UpdateAllServiceInterval(int32 new_value)
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
	} else {
		/* Service intervals are in days. */
		vds->servint_trains   = DEF_SERVINT_DAYS_TRAINS;
		vds->servint_roadveh  = DEF_SERVINT_DAYS_ROADVEH;
		vds->servint_aircraft = DEF_SERVINT_DAYS_AIRCRAFT;
		vds->servint_ships    = DEF_SERVINT_DAYS_SHIPS;
	}

	if (update_vehicles) {
		const Company *c = Company::Get(_current_company);
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == _current_company && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(CompanyServiceInterval(c, v->type));
				v->SetServiceIntervalIsPercent(new_value != 0);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static bool CanUpdateServiceInterval(VehicleType type, int32 &new_value)
{
	VehicleDefaultSettings *vds;
	if (_game_mode == GM_MENU || !Company::IsValidID(_current_company)) {
		vds = &_settings_client.company.vehicle;
	} else {
		vds = &Company::Get(_current_company)->settings.vehicle;
	}

	/* Test if the interval is valid */
	int32 interval = GetServiceIntervalClamped(new_value, vds->servint_ispercent);
	return interval == new_value;
}

static void UpdateServiceInterval(VehicleType type, int32 new_value)
{
	if (_game_mode != GM_MENU && Company::IsValidID(_current_company)) {
		for (Vehicle *v : Vehicle::Iterate()) {
			if (v->owner == _current_company && v->type == type && v->IsPrimaryVehicle() && !v->ServiceIntervalIsCustom()) {
				v->SetServiceInterval(new_value);
			}
		}
	}

	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
}

static void TrainAccelerationModelChanged(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		if (t->IsFrontEngine()) {
			t->tcache.cached_max_curve_speed = t->GetCurveSpeedLimit();
			t->UpdateAcceleration();
			if (t->lookahead != nullptr) SetBit(t->lookahead->flags, TRLF_APPLY_ADVISORY);
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

static bool CheckTrainBrakingModelChange(int32 &new_value)
{
	if (new_value == TBM_REALISTIC && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		for (TileIndex t = 0; t < MapSize(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RAIL_TILE_SIGNALS) {
				uint signals = GetPresentSignals(t);
				if ((signals & 0x3) & ((signals & 0x3) - 1) || (signals & 0xC) & ((signals & 0xC) - 1)) {
					/* Signals in both directions */
					ShowErrorMessage(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED, INVALID_STRING_ID, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
				if (((signals & 0x3) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_LOWER))) ||
						((signals & 0xC) && IsSignalTypeUnsuitableForRealisticBraking(GetSignalType(t, TRACK_UPPER)))) {
					/* Banned signal types present */
					ShowErrorMessage(STR_CONFIG_SETTING_REALISTIC_BRAKING_SIGNALS_NOT_ALLOWED, INVALID_STRING_ID, WL_ERROR);
					ShowExtraViewportWindow(t);
					SetRedErrorSquare(t);
					return false;
				}
			}
		}
	}

	return true;
}

static void TrainBrakingModelChanged(int32 new_value)
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
		for (TileIndex t = 0; t < MapSize(); t++) {
			if (IsTileType(t, MP_RAILWAY) && GetRailTileType(t) == RAIL_TILE_SIGNALS) {
				TrackBits bits = GetTrackBits(t);
				do {
					Track track = RemoveFirstTrack(&bits);
					if (HasSignalOnTrack(t, track) && GetSignalType(t, track) == SIGTYPE_NORMAL && HasBit(GetRailReservationTrackBits(t), track)) {
						if (EnsureNoTrainOnTrackBits(t, TrackToTrackBits(track)).Succeeded()) {
							UnreserveTrack(t, track);
						}
					}
				} while (bits != TRACK_BIT_NONE);
			}
		}
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: %s", scope_dumper().VehicleInfo(v_cur));
		extern bool _long_reserve_disabled;
		_long_reserve_disabled = true;
		for (Train *v : Train::Iterate()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0 || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
		}
		_long_reserve_disabled = false;
		for (Train *v : Train::Iterate()) {
			v_cur = v;
			if (!v->IsPrimaryVehicle() || (v->vehstatus & VS_CRASHED) != 0 || HasBit(v->subtype, GVSF_VIRTUAL) || v->track == TRACK_BIT_DEPOT) continue;
			TryPathReserve(v, true, HasStationTileRail(v->tile));
			if (v->lookahead != nullptr) SetBit(v->lookahead->flags, TRLF_APPLY_ADVISORY);
		}
	} else if (new_value == TBM_ORIGINAL && (_game_mode == GM_NORMAL || _game_mode == GM_EDITOR)) {
		Train *v_cur = nullptr;
		SCOPE_INFO_FMT([&v_cur], "TrainBrakingModelChanged: %s", scope_dumper().VehicleInfo(v_cur));
		for (Train *v : Train::Iterate()) {
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
}

/**
 * This function updates the train acceleration cache after a steepness change.
 * @param new_value Unused new value of setting.
 */
static void TrainSlopeSteepnessChanged(int32 new_value)
{
	for (Train *t : Train::Iterate()) {
		if (t->IsFrontEngine()) {
			t->CargoChanged();
			if (t->lookahead != nullptr) SetBit(t->lookahead->flags, TRLF_APPLY_ADVISORY);
		}
	}
}

/**
 * This function updates realistic acceleration caches when the setting "Road vehicle acceleration model" is set.
 * @param new_value Unused new value of setting.
 */
static void RoadVehAccelerationModelChanged(int32 new_value)
{
	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (rv->IsFrontEngine()) {
				rv->CargoChanged();
			}
		}
	}
	if (_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL || !_settings_game.vehicle.improved_breakdowns) {
		for (RoadVehicle *rv : RoadVehicle::Iterate()) {
			if (rv->IsFrontEngine()) {
				rv->breakdown_chance_factor = 128;
			}
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
static void RoadVehSlopeSteepnessChanged(int32 new_value)
{
	for (RoadVehicle *rv : RoadVehicle::Iterate()) {
		if (rv->IsFrontEngine()) rv->CargoChanged();
	}
}

static void ProgrammableSignalsShownChanged(int32 new_value)
{
	InvalidateWindowData(WC_BUILD_SIGNAL, 0);
}

static void VehListCargoFilterShownChanged(int32 new_value)
{
	InvalidateWindowClassesData(WC_TRACE_RESTRICT_SLOTS, 0);
	InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
	InvalidateWindowClassesData(WC_SHIPS_LIST, 0);
	InvalidateWindowClassesData(WC_ROADVEH_LIST, 0);
	InvalidateWindowClassesData(WC_AIRCRAFT_LIST, 0);
}

static void TownFoundingChanged(int32 new_value)
{
	if (_game_mode != GM_EDITOR && _settings_game.economy.found_town == TF_FORBIDDEN) {
		DeleteWindowById(WC_FOUND_TOWN, 0);
	} else {
		InvalidateWindowData(WC_FOUND_TOWN, 0);
	}
}

static void InvalidateVehTimetableWindow(int32 new_value)
{
	InvalidateWindowClassesData(WC_VEHICLE_TIMETABLE, VIWD_MODIFY_ORDERS);
	InvalidateWindowClassesData(WC_SCHDISPATCH_SLOTS, VIWD_MODIFY_ORDERS);
}

static void ChangeTimetableInTicksMode(int32 new_value)
{
	SetWindowClassesDirty(WC_VEHICLE_ORDERS);
	InvalidateVehTimetableWindow(new_value);
}

static void UpdateTimeSettings(int32 new_value)
{
	SetupTimeSettings();
	InvalidateVehTimetableWindow(new_value);
	InvalidateWindowData(WC_STATUS_BAR, 0, SBI_REINIT);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	InvalidateWindowClassesData(WC_DEPARTURES_BOARD, 1);
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	MarkWholeScreenDirty();
}

static void ChangeTimeOverrideMode(int32 new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTimeSettings(new_value);
}

static void ZoomMinMaxChanged(int32 new_value)
{
	extern void ConstrainAllViewportsZoom();
	extern void UpdateFontHeightCache();
	ConstrainAllViewportsZoom();
	GfxClearSpriteCache();
	if (_settings_client.gui.zoom_min > _gui_zoom) {
		/* Restrict GUI zoom if it is no longer available. */
		_gui_zoom = _settings_client.gui.zoom_min;
		UpdateCursorSize();
		UpdateRouteStepSpriteSize();
		UpdateFontHeightCache();
		LoadStringWidthTable();
		ReInitAllWindows(false);
	}
}

static void SpriteZoomMinChanged(int32 new_value)
{
	GfxClearSpriteCache();
	/* Force all sprites to redraw at the new chosen zoom level */
	MarkWholeScreenDirty();
}

static void DeveloperModeChanged(int32 new_value)
{
	DebugReconsiderSendRemoteMessages();
}

/**
 * Update any possible saveload window and delete any newgrf dialogue as
 * its widget parts might change. Reinit all windows as it allows access to the
 * newgrf debug button.
 * @param new_value unused.
 */
static void InvalidateNewGRFChangeWindows(int32 new_value)
{
	InvalidateWindowClassesData(WC_SAVELOAD);
	DeleteWindowByClass(WC_GAME_OPTIONS);
	ReInitAllWindows(false);
}

static void InvalidateCompanyLiveryWindow(int32 new_value)
{
	InvalidateWindowClassesData(WC_COMPANY_COLOUR, -1);
	ResetVehicleColourMap();
	MarkWholeScreenDirty();
}

static void ScriptMaxOpsChange(int32 new_value)
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

static bool CheckScriptMaxMemoryChange(int32 &new_value)
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

static void ScriptMaxMemoryChange(int32 new_value)
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
 * @param p1 Unused.
 * @return Always true.
 */
static void InvalidateCompanyWindow(int32 new_value)
{
	InvalidateWindowClassesData(WC_COMPANY);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void EnableSingleVehSharedOrderGuiChanged(int32 new_value)
{
	for (VehicleType type = VEH_BEGIN; type < VEH_COMPANY_END; type++) {
		InvalidateWindowClassesData(GetWindowClassForVehicleType(type), 0);
	}
	SetWindowClassesDirty(WC_VEHICLE_TIMETABLE);
	InvalidateWindowClassesData(WC_VEHICLE_ORDERS, 0);
}

static void CheckYapfRailSignalPenalties(int32 new_value)
{
	extern void YapfCheckRailSignalPenalties();
	YapfCheckRailSignalPenalties();
}

static void ViewportMapShowTunnelModeChanged(int32 new_value)
{
	extern void ViewportMapBuildTunnelCache();
	ViewportMapBuildTunnelCache();

	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void ViewportMapLandscapeModeChanged(int32 new_value)
{
	extern void MarkAllViewportMapLandscapesDirty();
	MarkAllViewportMapLandscapesDirty();
}

static void UpdateLinkgraphColours(int32 new_value)
{
	BuildLinkStatsLegend();
	MarkWholeScreenDirty();
}

static void ClimateThresholdModeChanged(int32 new_value)
{
	InvalidateWindowClassesData(WC_GENERATE_LANDSCAPE);
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
}

static void VelocityUnitsChanged(int32 new_value) {
	InvalidateWindowClassesData(WC_PAYMENT_RATES);
	InvalidateWindowClassesData(WC_TRACE_RESTRICT);
	MarkWholeScreenDirty();
}

static void ChangeTrackTypeSortMode(int32 new_value) {
	extern void SortRailTypes();
	SortRailTypes();
	MarkWholeScreenDirty();
}

static void PublicRoadsSettingChange(int32 new_value) {
	InvalidateWindowClassesData(WC_SCEN_LAND_GEN);
}

static void TrainSpeedAdaptationChanged(int32 new_value) {
	extern void ClearAllSignalSpeedRestrictions();
	ClearAllSignalSpeedRestrictions();
	for (Train *t : Train::Iterate()) {
		t->signal_speed_restriction = 0;
	}
}

static void AutosaveModeChanged(int32 new_value) {
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

static bool TownCouncilToleranceAdjust(int32 &new_value)
{
	if (new_value == 255) new_value = TOWN_COUNCIL_PERMISSIVE;
	return true;
}

static void DifficultyNoiseChange(int32 new_value)
{
	if (_game_mode == GM_NORMAL) {
		UpdateAirportsNoise();
		if (_settings_game.economy.station_noise_level) {
			InvalidateWindowClassesData(WC_TOWN_VIEW, 0);
		}
	}
}

static void DifficultyMoneyCheatMultiplayerChange(int32 new_value)
{
	DeleteWindowById(WC_CHEATS, 0);
}

static void DifficultyRenameTownsMultiplayerChange(int32 new_value)
{
	SetWindowClassesDirty(WC_TOWN_VIEW);
}

static void DifficultyOverrideTownSettingsMultiplayerChange(int32 new_value)
{
	SetWindowClassesDirty(WC_TOWN_AUTHORITY);
}

static void MaxNoAIsChange(int32 new_value)
{
	if (GetGameSettings().difficulty.max_no_competitors != 0 &&
			AI::GetInfoList()->size() == 0 &&
			(!_networking || (_network_server || _network_settings_access))) {
		ShowErrorMessage(STR_WARNING_NO_SUITABLE_AI, INVALID_STRING_ID, WL_CRITICAL);
	}

	InvalidateWindowClassesData(WC_GAME_OPTIONS, 0);
}

/**
 * Check whether the road side may be changed.
 * @param new_value unused
 * @return true if the road side may be changed.
 */
static bool CheckRoadSide(int32 &new_value)
{
	extern bool RoadVehiclesExistOutsideDepots();
	return (_game_mode == GM_MENU || !RoadVehiclesExistOutsideDepots());
}

static void RoadSideChanged(int32 new_value)
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

static bool CheckFreeformEdges(int32 &new_value)
{
	if (_game_mode == GM_MENU) return true;
	if (new_value != 0) {
		for (Ship *s : Ship::Iterate()) {
			/* Check if there is a ship on the northern border. */
			if (TileX(s->tile) == 0 || TileY(s->tile) == 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_EMPTY, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (const BaseStation *st : BaseStation::Iterate()) {
			/* Check if there is a non-deleted buoy on the northern border. */
			if (st->IsInUse() && (TileX(st->xy) == 0 || TileY(st->xy) == 0)) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_EMPTY, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
	} else {
		for (uint i = 0; i < MapMaxX(); i++) {
			if (TileHeight(TileXY(i, 1)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxX(); i++) {
			if (!IsTileType(TileXY(i, MapMaxY() - 1), MP_WATER) || TileHeight(TileXY(1, MapMaxY())) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			if (TileHeight(TileXY(1, i)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
		for (uint i = 1; i < MapMaxY(); i++) {
			if (!IsTileType(TileXY(MapMaxX() - 1, i), MP_WATER) || TileHeight(TileXY(MapMaxX(), i)) != 0) {
				ShowErrorMessage(STR_CONFIG_SETTING_EDGES_NOT_WATER, INVALID_STRING_ID, WL_ERROR);
				return false;
			}
		}
	}
	return true;
}

static void UpdateFreeformEdges(int32 new_value)
{
	if (_game_mode == GM_MENU) return;

	if (new_value != 0) {
		for (uint x = 0; x < MapSizeX(); x++) MakeVoid(TileXY(x, 0));
		for (uint y = 0; y < MapSizeY(); y++) MakeVoid(TileXY(0, y));
	} else {
		/* Make tiles at the border water again. */
		for (uint i = 0; i < MapMaxX(); i++) {
			SetTileHeight(TileXY(i, 0), 0);
			MakeSea(TileXY(i, 0));
		}
		for (uint i = 0; i < MapMaxY(); i++) {
			SetTileHeight(TileXY(0, i), 0);
			MakeSea(TileXY(0, i));
		}
	}
	MarkWholeScreenDirty();
}

/**
 * Changing the setting "allow multiple NewGRF sets" is not allowed
 * if there are vehicles.
 */
static bool CheckDynamicEngines(int32 &new_value)
{
	if (_game_mode == GM_MENU) return true;

	if (!EngineOverrideManager::ResetToCurrentNewGRFConfig()) {
		ShowErrorMessage(STR_CONFIG_SETTING_DYNAMIC_ENGINES_EXISTING_VEHICLES, INVALID_STRING_ID, WL_ERROR);
		return false;
	}

	return true;
}

static bool CheckMaxHeightLevel(int32 &new_value)
{
	if (_game_mode == GM_NORMAL) return false;
	if (_game_mode != GM_EDITOR) return true;

	/* Check if at least one mountain on the map is higher than the new value.
	 * If yes, disallow the change. */
	for (TileIndex t = 0; t < MapSize(); t++) {
		if ((int32)TileHeight(t) > new_value) {
			ShowErrorMessage(STR_CONFIG_SETTING_TOO_HIGH_MOUNTAIN, INVALID_STRING_ID, WL_ERROR);
			/* Return old, unchanged value */
			return false;
		}
	}

	return true;
}

static void StationCatchmentChanged(int32 new_value)
{
	Station::RecomputeCatchmentForAll();
	for (Station *st : Station::Iterate()) UpdateStationAcceptance(st, true);
	MarkWholeScreenDirty();
}

static bool CheckSharingRail(int32 &new_value)
{
	return CheckSharingChangePossible(VEH_TRAIN, new_value);
}

static void SharingRailChanged(int32 new_value)
{
	UpdateAllBlockSignals();
}

static bool CheckSharingRoad(int32 &new_value)
{
	return CheckSharingChangePossible(VEH_ROAD, new_value);
}

static bool CheckSharingWater(int32 &new_value)
{
	return CheckSharingChangePossible(VEH_SHIP, new_value);
}

static bool CheckSharingAir(int32 &new_value)
{
	return CheckSharingChangePossible(VEH_AIRCRAFT, new_value);
}

static void MaxVehiclesChanged(int32 new_value)
{
	InvalidateWindowClassesData(WC_BUILD_TOOLBAR);
	MarkWholeScreenDirty();
}

static void InvalidateShipPathCache(int32 new_value)
{
	for (Ship *s : Ship::Iterate()) {
		s->cached_path.reset();
	}
}

static void ImprovedBreakdownsSettingChanged(int32 new_value)
{
	if (!_settings_game.vehicle.improved_breakdowns) return;

	for (Vehicle *v : Vehicle::Iterate()) {
		switch(v->type) {
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

static uint8 _pre_change_day_length_factor;

static bool DayLengthPreChange(int32 &new_value)
{
	_pre_change_day_length_factor = _settings_game.economy.day_length_factor;

	return true;
}

static void DayLengthChanged(int32 new_value)
{
	DateTicksScaled old_scaled_date_ticks = _scaled_date_ticks;
	DateTicksScaled old_scaled_date_ticks_offset = _scaled_date_ticks_offset;

	extern void RebaseScaledDateTicksBase();
	RebaseScaledDateTicksBase();

	extern void VehicleDayLengthChanged(DateTicksScaled old_scaled_date_ticks, DateTicksScaled old_scaled_date_ticks_offset, uint8 old_day_length_factor);
	VehicleDayLengthChanged(old_scaled_date_ticks, old_scaled_date_ticks_offset, _pre_change_day_length_factor);

	SetupTileLoopCounts();

	MarkWholeScreenDirty();
}

static void TownZoneModeChanged(int32 new_value)
{
	InvalidateWindowClassesData(WC_GAME_OPTIONS);
	UpdateTownRadii();
}

static void TownZoneCustomValueChanged(int32 new_value)
{
	if (_settings_game.economy.town_zone_calc_mode) UpdateTownRadii();
}

static bool CheckTTDPatchSettingFlag(uint flag)
{
	extern bool HasTTDPatchFlagBeenObserved(uint flag);
	if (_networking && HasTTDPatchFlagBeenObserved(flag)) {
		ShowErrorMessage(STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED, STR_CONFIG_SETTING_NETWORK_CHANGE_NOT_ALLOWED_NEWGRF, WL_ERROR);
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
	extern uint8 _network_company_password_storage_token[16];
	if (value.size() != 32) return;
	DecodeHexText(value.c_str(), _network_company_password_storage_token, 16);
}

static void ParseCompanyPasswordStorageSecret(const std::string &value)
{
	extern uint8 _network_company_password_storage_key[32];
	if (value.size() != 64) return;
	DecodeHexText(value.c_str(), _network_company_password_storage_key, 32);
}

/** Update the game info, and send it to the clients when we are running as a server. */
static void UpdateClientConfigValues()
{
	NetworkServerUpdateGameInfo();
	if (_network_server) {
		NetworkServerSendConfigUpdate();
		SetWindowClassesDirty(WC_CLIENT_LIST);
	}
}

/* End - Callback Functions */

/* Begin - xref conversion callbacks */

static int64 LinkGraphDistModeXrefChillPP(int64 val)
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

		int32 value = (int32)((name == "max_loan" ? 1000 : 1) * _old_diff_custom[i++]);
		sd->AsIntSetting()->MakeValueValidAndWrite(savegame ? &_settings_game : &_settings_newgame, value);
	}
}

static void AILoadConfig(IniFile &ini, const char *grpname)
{
	IniGroup *group = ini.GetGroup(grpname);
	IniItem *item;

	/* Clean any configured AI */
	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME)->Change(std::nullopt);
	}

	/* If no group exists, return */
	if (group == nullptr) return;

	CompanyID c = COMPANY_FIRST;
	for (item = group->item; c < MAX_COMPANIES && item != nullptr; c++, item = item->next) {
		AIConfig *config = AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME);

		config->Change(item->name);
		if (!config->HasScript()) {
			if (item->name != "none") {
				DEBUG(script, 0, "The AI by the name '%s' was no longer found, and removed from the list.", item->name.c_str());
				continue;
			}
		}
		if (item->value.has_value()) config->StringToSettings(item->value->c_str());
	}
}

static void GameLoadConfig(IniFile &ini, const char *grpname)
{
	IniGroup *group = ini.GetGroup(grpname);
	IniItem *item;

	/* Clean any configured GameScript */
	GameConfig::GetConfig(GameConfig::SSS_FORCE_NEWGAME)->Change(std::nullopt);

	/* If no group exists, return */
	if (group == nullptr) return;

	item = group->item;
	if (item == nullptr) return;

	GameConfig *config = GameConfig::GetConfig(AIConfig::SSS_FORCE_NEWGAME);

	config->Change(item->name);
	if (!config->HasScript()) {
		if (item->name != "none") {
			DEBUG(script, 0, "The GameScript by the name '%s' was no longer found, and removed from the list.", item->name.c_str());
			return;
		}
	}
	if (item->value.has_value()) config->StringToSettings(item->value->c_str());
}

/**
 * Convert a character to a hex nibble value, or \c -1 otherwise.
 * @param c Character to convert.
 * @return Hex value of the character, or \c -1 if not a hex digit.
 */
static int DecodeHexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c + 10 - 'A';
	if (c >= 'a' && c <= 'f') return c + 10 - 'a';
	return -1;
}

/**
 * Parse a sequence of characters (supposedly hex digits) into a sequence of bytes.
 * After the hex number should be a \c '|' character.
 * @param pos First character to convert.
 * @param[out] dest Output byte array to write the bytes.
 * @param dest_size Number of bytes in \a dest.
 * @return Whether reading was successful.
 */
static bool DecodeHexText(const char *pos, uint8 *dest, size_t dest_size)
{
	while (dest_size > 0) {
		int hi = DecodeHexNibble(pos[0]);
		int lo = (hi >= 0) ? DecodeHexNibble(pos[1]) : -1;
		if (lo < 0) return false;
		*dest++ = (hi << 4) | lo;
		pos += 2;
		dest_size--;
	}
	return *pos == '|';
}

/**
 * Load a GRF configuration
 * @param ini       The configuration to read from.
 * @param grpname   Group name containing the configuration of the GRF.
 * @param is_static GRF is static.
 */
static GRFConfig *GRFLoadConfig(IniFile &ini, const char *grpname, bool is_static)
{
	IniGroup *group = ini.GetGroup(grpname);
	IniItem *item;
	GRFConfig *first = nullptr;
	GRFConfig **curr = &first;

	if (group == nullptr) return nullptr;

	uint num_grfs = 0;
	for (item = group->item; item != nullptr; item = item->next) {
		GRFConfig *c = nullptr;

		uint8 grfid_buf[4];
		MD5Hash md5sum;
		const char *filename = item->name.c_str();
		bool has_grfid = false;
		bool has_md5sum = false;

		/* Try reading "<grfid>|" and on success, "<md5sum>|". */
		has_grfid = DecodeHexText(filename, grfid_buf, lengthof(grfid_buf));
		if (has_grfid) {
			filename += 1 + 2 * lengthof(grfid_buf);
			has_md5sum = DecodeHexText(filename, md5sum.data(), md5sum.size());
			if (has_md5sum) filename += 1 + 2 * md5sum.size();

			uint32 grfid = grfid_buf[0] | (grfid_buf[1] << 8) | (grfid_buf[2] << 16) | (grfid_buf[3] << 24);
			if (has_md5sum) {
				const GRFConfig *s = FindGRFConfig(grfid, FGCM_EXACT, &md5sum);
				if (s != nullptr) c = new GRFConfig(*s);
			}
			if (c == nullptr && !FioCheckFileExists(filename, NEWGRF_DIR)) {
				const GRFConfig *s = FindGRFConfig(grfid, FGCM_NEWEST_VALID);
				if (s != nullptr) c = new GRFConfig(*s);
			}
		}
		if (c == nullptr) c = new GRFConfig(filename);

		/* Parse parameters */
		if (item->value.has_value() && !item->value->empty()) {
			int count = ParseIntList(item->value->c_str(), c->param.data(), c->param.size());
			if (count < 0) {
				SetDParamStr(0, filename);
				ShowErrorMessage(STR_CONFIG_ERROR, STR_CONFIG_ERROR_ARRAY, WL_CRITICAL);
				count = 0;
			}
			c->num_params = count;
		}

		/* Check if item is valid */
		if (!FillGRFDetails(c, is_static) || HasBit(c->flags, GCF_INVALID)) {
			if (c->status == GCS_NOT_FOUND) {
				SetDParam(1, STR_CONFIG_ERROR_INVALID_GRF_NOT_FOUND);
			} else if (HasBit(c->flags, GCF_UNSAFE)) {
				SetDParam(1, STR_CONFIG_ERROR_INVALID_GRF_UNSAFE);
			} else if (HasBit(c->flags, GCF_SYSTEM)) {
				SetDParam(1, STR_CONFIG_ERROR_INVALID_GRF_SYSTEM);
			} else if (HasBit(c->flags, GCF_INVALID)) {
				SetDParam(1, STR_CONFIG_ERROR_INVALID_GRF_INCOMPATIBLE);
			} else {
				SetDParam(1, STR_CONFIG_ERROR_INVALID_GRF_UNKNOWN);
			}

			SetDParamStr(0, StrEmpty(filename) ? item->name.c_str() : filename);
			ShowErrorMessage(STR_CONFIG_ERROR, STR_CONFIG_ERROR_INVALID_GRF, WL_CRITICAL);
			delete c;
			continue;
		}

		/* Check for duplicate GRFID (will also check for duplicate filenames) */
		bool duplicate = false;
		for (const GRFConfig *gc = first; gc != nullptr; gc = gc->next) {
			if (gc->ident.grfid == c->ident.grfid) {
				SetDParamStr(0, c->filename);
				SetDParamStr(1, gc->filename);
				ShowErrorMessage(STR_CONFIG_ERROR, STR_CONFIG_ERROR_DUPLICATE_GRFID, WL_CRITICAL);
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			delete c;
			continue;
		}

		if (is_static) {
			/* Mark file as static to avoid saving in savegame. */
			SetBit(c->flags, GCF_STATIC);
		} else if (++num_grfs > NETWORK_MAX_GRF_COUNT) {
			/* Check we will not load more non-static NewGRFs than allowed. This could trigger issues for game servers. */
			ShowErrorMessage(STR_CONFIG_ERROR, STR_NEWGRF_ERROR_TOO_MANY_NEWGRFS_LOADED, WL_CRITICAL);
			break;
		}

		/* Add item to list */
		*curr = c;
		curr = &c->next;
	}

	return first;
}

static IniFileVersion LoadVersionFromConfig(IniFile &ini)
{
	IniGroup *group = ini.GetGroup("version");

	auto version_number = group->GetItem("ini_version", false);
	/* Older ini-file versions don't have this key yet. */
	if (version_number == nullptr || !version_number->value.has_value()) return IFV_0;

	uint32 version = 0;
	IntFromChars(version_number->value->data(), version_number->value->data() + version_number->value->size(), version);

	return static_cast<IniFileVersion>(version);
}

static void AISaveConfig(IniFile &ini, const char *grpname)
{
	IniGroup *group = ini.GetGroup(grpname);

	if (group == nullptr) return;
	group->Clear();

	for (CompanyID c = COMPANY_FIRST; c < MAX_COMPANIES; c++) {
		AIConfig *config = AIConfig::GetConfig(c, AIConfig::SSS_FORCE_NEWGAME);
		std::string name;
		std::string value = config->SettingsToString();

		if (config->HasScript()) {
			name = config->GetName();
		} else {
			name = "none";
		}

		IniItem *item = new IniItem(group, name);
		item->SetValue(value);
	}
}

static void GameSaveConfig(IniFile &ini, const char *grpname)
{
	IniGroup *group = ini.GetGroup(grpname);

	if (group == nullptr) return;
	group->Clear();

	GameConfig *config = GameConfig::GetConfig(AIConfig::SSS_FORCE_NEWGAME);
	std::string name;
	std::string value = config->SettingsToString();

	if (config->HasScript()) {
		name = config->GetName();
	} else {
		name = "none";
	}

	IniItem *item = new IniItem(group, name);
	item->SetValue(value);
}

/**
 * Save the version of OpenTTD to the ini file.
 * @param ini the ini to write to
 */
static void SaveVersionInConfig(IniFile &ini)
{
	IniGroup *group = ini.GetGroup("version");
	group->GetItem("version_string", true)->SetValue(_openttd_revision);
	group->GetItem("version_number", true)->SetValue(stdstr_fmt("%08X", _openttd_newgrf_version));
	group->GetItem("ini_version", true)->SetValue(std::to_string(INIFILE_VERSION));
}

/* Save a GRF configuration to the given group name */
static void GRFSaveConfig(IniFile &ini, const char *grpname, const GRFConfig *list)
{
	ini.RemoveGroup(grpname);
	IniGroup *group = ini.GetGroup(grpname);
	const GRFConfig *c;

	for (c = list; c != nullptr; c = c->next) {
		/* Hex grfid (4 bytes in nibbles), "|", hex md5sum (16 bytes in nibbles), "|", file system path. */
		char key[4 * 2 + 1 + 16 * 2 + 1 + MAX_PATH];
		char *pos = key + seprintf(key, lastof(key), "%08X|", BSWAP32(c->ident.grfid));
		pos = md5sumToString(pos, lastof(key), c->ident.md5sum);
		seprintf(pos, lastof(key), "|%s", c->filename.c_str());
		group->GetItem(key, true)->SetValue(GRFBuildParamList(c));
	}
}

/* Common handler for saving/loading variables to the configuration file */
static void HandleSettingDescs(IniFile &generic_ini, IniFile &private_ini, IniFile &secrets_ini, SettingDescProc *proc, SettingDescProcList *proc_list, bool only_startup = false)
{
	proc(generic_ini, _misc_settings, "misc", nullptr, only_startup);
#if defined(_WIN32) && !defined(DEDICATED)
	proc(generic_ini, _win32_settings, "win32", nullptr, only_startup);
#endif /* _WIN32 */

	/* The name "patches" is a fallback, as every setting should sets its own group. */

	for (auto &table : _generic_setting_tables) {
		proc(generic_ini, table, "patches", &_settings_newgame, only_startup);
	}
	for (auto &table : _private_setting_tables) {
		proc(private_ini, table, "patches", &_settings_newgame, only_startup);
	}
	for (auto &table : _secrets_setting_tables) {
		proc(secrets_ini, table, "patches", &_settings_newgame, only_startup);
	}

	proc(generic_ini, _currency_settings, "currency", &_custom_currency, only_startup);
	proc(generic_ini, _company_settings, "company", &_settings_client.company, only_startup);

	if (!only_startup) {
		proc_list(private_ini, "server_bind_addresses", _network_bind_list);
		proc_list(private_ini, "servers", _network_host_list);
		proc_list(private_ini, "bans", _network_ban_list);
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
		s = s.substr(sc + 1);

		group->RemoveItem(s);
	}
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

	if (!startup) ResetCurrencies(false); // Initialize the array of currencies, without preserving the custom one

	IniFileVersion generic_version = LoadVersionFromConfig(generic_ini);

	/* Before the split of private/secrets, we have to look in the generic for these settings. */
	if (generic_version < IFV_PRIVATE_SECRETS) {
		HandleSettingDescs(generic_ini, generic_ini, generic_ini, IniLoadSettings, IniLoadSettingList, startup);
	} else {
		HandleSettingDescs(generic_ini, private_ini, secrets_ini, IniLoadSettings, IniLoadSettingList, startup);
	}

	/* Load basic settings only during bootstrap, load other settings not during bootstrap */
	if (!startup) {
		/* Convert network.server_advertise to network.server_game_type, but only if network.server_game_type is set to default value. */
		if (generic_version < IFV_GAME_TYPE) {
			if (_settings_client.network.server_game_type == SERVER_GAME_TYPE_LOCAL) {
				IniGroup *network = generic_ini.GetGroup("network", false);
				if (network != nullptr) {
					IniItem *server_advertise = network->GetItem("server_advertise", false);
					if (server_advertise != nullptr && server_advertise->value == "true") {
						_settings_client.network.server_game_type = SERVER_GAME_TYPE_PUBLIC;
					}
				}
			}
		}

		if (generic_version < IFV_LINKGRAPH_SECONDS) {
			_settings_newgame.linkgraph.recalc_interval *= SECONDS_PER_DAY;
			_settings_newgame.linkgraph.recalc_time     *= SECONDS_PER_DAY;
		}

		if (generic_version < IFV_NETWORK_PRIVATE_SETTINGS) {
			IniGroup *network = generic_ini.GetGroup("network", false);
			if (network != nullptr) {
				IniItem *no_http_content_downloads = network->GetItem("no_http_content_downloads", false);
				if (no_http_content_downloads != nullptr) {
					if (no_http_content_downloads->value == "true") {
						_settings_client.network.no_http_content_downloads = true;
					} else if (no_http_content_downloads->value == "false") {
						_settings_client.network.no_http_content_downloads = false;
					}
				}

				IniItem *use_relay_service = network->GetItem("use_relay_service", false);
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

		_grfconfig_newgame = GRFLoadConfig(generic_ini, "newgrf", false);
		_grfconfig_static  = GRFLoadConfig(generic_ini, "newgrf-static", true);
		AILoadConfig(generic_ini, "ai_players");
		GameLoadConfig(generic_ini, "game_scripts");

		PrepareOldDiffCustom();
		IniLoadSettings(generic_ini, _gameopt_settings, "gameopt", &_settings_newgame, false);
		HandleOldDiffCustom(false);

		ValidateSettings();
		DebugReconsiderSendRemoteMessages();

		PostZoningModeChange();

		/* Display scheduled errors */
		ScheduleErrorMessage(_settings_error_list);
		if (FindWindowById(WC_ERRMSG, 0) == nullptr) ShowFirstError();
	} else {
		PostTransparencyOptionLoad();
		if (_fallback_gui_zoom_max && _settings_client.gui.zoom_max <= ZOOM_LVL_OUT_32X) {
			_settings_client.gui.zoom_max = ZOOM_LVL_MAX;
		}
	}
}

/** Save the values to the configuration file */
void SaveToConfig()
{
	PreTransparencyOptionSave();

	ConfigIniFile generic_ini(_config_file);
	ConfigIniFile private_ini(_private_file);
	ConfigIniFile secrets_ini(_secrets_file);

	IniFileVersion generic_version = LoadVersionFromConfig(generic_ini);

	/* If we newly create the private/secrets file, add a dummy group on top
	 * just so we can add a comment before it (that is how IniFile works).
	 * This to explain what the file is about. After doing it once, never touch
	 * it again, as otherwise we might be reverting user changes. */
	if (!private_ini.GetGroup("private", false)) private_ini.GetGroup("private")->comment = "; This file possibly contains private information which can identify you as person.\n";
	if (!secrets_ini.GetGroup("secrets", false)) secrets_ini.GetGroup("secrets")->comment = "; Do not share this file with others, not even if they claim to be technical support.\n; This file contains saved passwords and other secrets that should remain private to you!\n";

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

	/* Remove network.server_advertise. */
	if (generic_version < IFV_GAME_TYPE) {
		IniGroup *network = generic_ini.GetGroup("network", false);
		if (network != nullptr) {
			network->RemoveItem("server_advertise");
		}
	}
	if (generic_version < IFV_NETWORK_PRIVATE_SETTINGS) {
		IniGroup *network = generic_ini.GetGroup("network", false);
		if (network != nullptr) {
			network->RemoveItem("no_http_content_downloads");
			network->RemoveItem("use_relay_service");
		}
	}

	HandleSettingDescs(generic_ini, private_ini, secrets_ini, IniSaveSettings, IniSaveSettingList);
	GRFSaveConfig(generic_ini, "newgrf", _grfconfig_newgame);
	GRFSaveConfig(generic_ini, "newgrf-static", _grfconfig_static);
	AISaveConfig(generic_ini, "ai_players");
	GameSaveConfig(generic_ini, "game_scripts");

	SaveVersionInConfig(generic_ini);
	SaveVersionInConfig(private_ini);
	SaveVersionInConfig(secrets_ini);

	generic_ini.SaveToDisk(_config_file);
	private_ini.SaveToDisk(_private_file);
	secrets_ini.SaveToDisk(_secrets_file);
}

/**
 * Get the list of known NewGrf presets.
 * @returns List of preset names.
 */
StringList GetGRFPresetList()
{
	StringList list;

	ConfigIniFile ini(_config_file);
	for (IniGroup *group = ini.group; group != nullptr; group = group->next) {
		if (group->name.compare(0, 7, "preset-") == 0) {
			list.push_back(group->name.substr(7));
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
GRFConfig *LoadGRFPresetFromConfig(const char *config_name)
{
	size_t len = strlen(config_name) + 8;
	char *section = (char*)alloca(len);
	seprintf(section, section + len - 1, "preset-%s", config_name);

	ConfigIniFile ini(_config_file);
	GRFConfig *config = GRFLoadConfig(ini, section, false);

	return config;
}

/**
 * Save a NewGRF configuration with a preset name.
 * @param config_name Name of the preset.
 * @param config      NewGRF configuration to save.
 * @see GetGRFPresetList
 */
void SaveGRFPresetToConfig(const char *config_name, GRFConfig *config)
{
	size_t len = strlen(config_name) + 8;
	char *section = (char*)alloca(len);
	seprintf(section, section + len - 1, "preset-%s", config_name);

	ConfigIniFile ini(_config_file);
	GRFSaveConfig(ini, section, config);
	ini.SaveToDisk(_config_file);
}

/**
 * Delete a NewGRF configuration by preset name.
 * @param config_name Name of the preset.
 */
void DeleteGRFPresetFromConfig(const char *config_name)
{
	size_t len = strlen(config_name) + 8;
	char *section = (char*)alloca(len);
	seprintf(section, section + len - 1, "preset-%s", config_name);

	ConfigIniFile ini(_config_file);
	ini.RemoveGroup(section);
	ini.SaveToDisk(_config_file);
}

/**
 * Handle changing a value. This performs validation of the input value and
 * calls the appropriate callbacks, and saves it when the value is changed.
 * @param object The object the setting is in.
 * @param newval The new value for the setting.
 */
void IntSettingDesc::ChangeValue(const void *object, int32 newval) const
{
	int32 oldval = this->Read(object);
	this->MakeValueValid(newval);
	if (this->pre_check != nullptr && !this->pre_check(newval)) return;
	if (oldval == newval) return;

	this->Write(object, newval);
	if (this->post_callback != nullptr) this->post_callback(newval);

	if (this->flags & SF_NO_NETWORK) {
		GamelogStartAction(GLAT_SETTING);
		GamelogSetting(this->name, oldval, newval);
		GamelogStopAction();
	}

	SetWindowClassesDirty(WC_GAME_OPTIONS);

	if (_save_config) SaveToConfig();
}

/**
 * Given a name of setting, return a setting description from the table.
 * @param name Name of the setting to return a setting description of.
 * @param settings Table to look in for the setting.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c nullptr indicates failure to obtain the description.
 */
static const SettingDesc *GetSettingFromName(const char *name, const SettingTable &settings)
{
	/* First check all full names */
	for (auto &sd : settings) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (strcmp(sd->name, name) == 0) return sd.get();
	}

	/* Then check the shortcut variant of the name. */
	for (auto &sd : settings) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		const char *short_name = strchr(sd->name, '.');
		if (short_name != nullptr) {
			short_name++;
			if (strcmp(short_name, name) == 0) return sd.get();
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
static const SettingDesc *GetCompanySettingFromName(const char *name)
{
	if (strncmp(name, "company.", 8) == 0) name += 8;
	return GetSettingFromName(name, _company_settings);
}

/**
 * Given a name of any setting, return any setting description of it.
 * @param name  Name of the setting to return a setting description of.
 * @return Pointer to the setting description of setting \a name if it can be found,
 *         \c nullptr indicates failure to obtain the description.
 */
const SettingDesc *GetSettingFromName(const char *name)
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

/**
 * Network-safe changing of settings (server-only).
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 the new value for the setting
 * The new value is properly clamped to its minimum/maximum when setting
 * @param text the name of the setting to change
 * @return the cost of this operation or an error
 * @see _settings
 */
CommandCost CmdChangeSetting(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (StrEmpty(text)) return CMD_ERROR;
	const SettingDesc *sd = GetSettingFromName(text);

	if (sd == nullptr) return CMD_ERROR;
	if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) return CMD_ERROR;
	if (!sd->IsIntSetting()) return CMD_ERROR;

	if (!sd->IsEditable(true)) return CMD_ERROR;

	if (flags & DC_EXEC) {
		SCOPE_INFO_FMT([=], "CmdChangeSetting: %s -> %d", sd->name, p2);

		sd->AsIntSetting()->ChangeValue(&GetGameSettings(), p2);
	}

	return CommandCost();
}

const char *GetSettingNameByIndex(uint32 idx)
{
	const SettingDesc *sd = GetSettingDescription(idx);
	if (sd == nullptr) return nullptr;

	return sd->name;
}

/**
 * Change one of the per-company settings.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 unused
 * @param p2 the new value for the setting
 * The new value is properly clamped to its minimum/maximum when setting
 * @param text the name of the company setting to change
 * @return the cost of this operation or an error
 */
CommandCost CmdChangeCompanySetting(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (StrEmpty(text)) return CMD_ERROR;
	const SettingDesc *sd = GetCompanySettingFromName(text);

	if (sd == nullptr) return CMD_ERROR;
	if (!sd->IsIntSetting()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		SCOPE_INFO_FMT([=], "CmdChangeCompanySetting: %s -> %d", sd->name, p2);

		sd->AsIntSetting()->ChangeValue(&Company::Get(_current_company)->settings, p2);
	}

	return CommandCost();
}

const char *GetCompanySettingNameByIndex(uint32 idx)
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
bool SetSettingValue(const IntSettingDesc *sd, int32 value, bool force_newgame)
{
	const IntSettingDesc *setting = sd->AsIntSetting();
	if ((setting->flags & SF_PER_COMPANY) != 0) {
		if (Company::IsValidID(_local_company) && _game_mode != GM_MENU) {
			return DoCommandP(0, 0, value, CMD_CHANGE_COMPANY_SETTING, nullptr, setting->name);
		} else if (setting->flags & SF_NO_NEWGAME) {
			return false;
		}

		setting->ChangeValue(&_settings_client.company, value);
		return true;
	}

	/* If an item is company-based, we do not send it over the network
	 * (if any) to change. Also *hack*hack* we update the _newgame version
	 * of settings because changing a company-based setting in a game also
	 * changes its defaults. At least that is the convention we have chosen */
	bool no_newgame = setting->flags & SF_NO_NEWGAME;
	if (no_newgame && _game_mode == GM_MENU) return false;
	if (setting->flags & SF_NO_NETWORK_SYNC) {
		if (_game_mode != GM_MENU && !no_newgame) {
			setting->ChangeValue(&_settings_newgame, value);
		}
		setting->ChangeValue(&GetGameSettings(), value);
		return true;
	}

	if (force_newgame && !no_newgame) {
		setting->ChangeValue(&_settings_newgame, value);
		return true;
	}

	/* send non-company-based settings over the network */
	if (!_networking || (_networking && (_network_server || _network_settings_access))) {
		return DoCommandP(0, 0, value, CMD_CHANGE_SETTING, nullptr, setting->name);
	}
	return false;
}

/**
 * Set the company settings for a new company to their default values.
 */
void SetDefaultCompanySettings(CompanyID cid)
{
	Company *c = Company::Get(cid);
	for (auto &sd : _company_settings) {
		if (sd->IsIntSetting()) {
			const IntSettingDesc *int_setting = sd->AsIntSetting();
			int_setting->MakeValueValidAndWrite(&c->settings, int_setting->def);
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
		uint32 old_value = (uint32)sd->AsIntSetting()->Read(old_object);
		uint32 new_value = (uint32)sd->AsIntSetting()->Read(new_object);
		if (old_value != new_value) NetworkSendCommand(0, 0, new_value, 0, CMD_CHANGE_COMPANY_SETTING, nullptr, sd->name, _local_company, nullptr);
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
	assert(sd->flags & SF_NO_NETWORK_SYNC);

	if (GetVarMemType(sd->save.conv) == SLE_VAR_STRQ && value.compare("(null)") == 0) {
		value.clear();
	}

	const void *object = (_game_mode == GM_MENU || force_newgame) ? &_settings_newgame : &_settings_game;
	sd->AsStringSetting()->ChangeValue(object, value);
	return true;
}

/**
 * Handle changing a string value. This performs validation of the input value
 * and calls the appropriate callbacks, and saves it when the value is changed.
 * @param object The object the setting is in.
 * @param newval The new value for the setting.
 */
void StringSettingDesc::ChangeValue(const void *object, std::string &newval) const
{
	this->MakeValueValid(newval);
	if (this->pre_check != nullptr && !this->pre_check(newval)) return;

	this->Write(object, newval);
	if (this->post_callback != nullptr) this->post_callback(newval);

	if (_save_config) SaveToConfig();
}

uint GetSettingIndexByFullName(const char *name)
{
	uint index = 0;
	for (auto &sd : _settings) {
		if (sd->name != nullptr && strcmp(sd->name, name) == 0) return index;
		index++;
	}
	return UINT32_MAX;
}

const SettingDesc *GetSettingFromFullName(const char *name)
{
	for (auto &sd : _settings) {
		if (sd->name != nullptr && strcmp(sd->name, name) == 0) return sd.get();
	}
	return nullptr;
}

/* Those 2 functions need to be here, else we have to make some stuff non-static
 * and besides, it is also better to keep stuff like this at the same place */
void IConsoleSetSetting(const char *name, const char *value, bool force_newgame)
{
	const SettingDesc *sd = GetSettingFromName(name);

	if (sd == nullptr || ((sd->flags & SF_NO_NEWGAME) && (_game_mode == GM_MENU || force_newgame))) {
		IConsolePrintF(CC_WARNING, "'%s' is an unknown setting.", name);
		return;
	}

	bool success = true;
	if (sd->IsStringSetting()) {
		success = SetSettingValue(sd->AsStringSetting(), value, force_newgame);
	} else if (sd->IsIntSetting()) {
		const IntSettingDesc *isd = sd->AsIntSetting();
		size_t val = isd->ParseValue(value);
		if (!_settings_error_list.empty()) {
			IConsolePrintF(CC_ERROR, "'%s' is not a valid value for this setting.", value);
			_settings_error_list.clear();
			return;
		}
		success = SetSettingValue(isd, (int32)val, force_newgame);
	}

	if (!success) {
		if ((_network_server || _network_settings_access)) {
			IConsoleError("This command/variable is not available during network games.");
		} else {
			IConsoleError("This command/variable is only available to a network server.");
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

	if (sd == nullptr || ((sd->flags & SF_NO_NEWGAME) && (_game_mode == GM_MENU || force_newgame))) {
		IConsolePrintF(CC_WARNING, "'%s' is an unknown setting.", name);
		return;
	}

	const void *object = (_game_mode == GM_MENU || force_newgame) ? &_settings_newgame : &_settings_game;

	if (sd->IsStringSetting()) {
		IConsolePrintF(CC_WARNING, "Current value for '%s' is: '%s'", name, sd->AsStringSetting()->Read(object).c_str());
	} else if (sd->IsIntSetting()) {
		const IntSettingDesc *int_setting = sd->AsIntSetting();

		bool show_min_max = true;
		int64 min_value = int_setting->min;
		int64 max_value = int_setting->max;
		if (sd->flags & SF_ENUM) {
			min_value = INT64_MAX;
			max_value = INT64_MIN;
			int count = 0;
			for (const SettingDescEnumEntry *enumlist = int_setting->enumlist; enumlist != nullptr && enumlist->str != STR_NULL; enumlist++) {
				if (enumlist->val < min_value) min_value = enumlist->val;
				if (enumlist->val > max_value) max_value = enumlist->val;
				count++;
			}
			if (max_value - min_value != (int64)(count - 1)) {
				/* Discontinuous range */
				show_min_max = false;
			}
		}

		char value[20];
		sd->FormatValue(value, lastof(value), object);

		if (show_min_max) {
			IConsolePrintF(CC_WARNING, "Current value for '%s' is: '%s' (min: %s" OTTD_PRINTF64 ", max: " OTTD_PRINTF64 ")",
				name, value, (sd->flags & SF_GUI_0_IS_SPECIAL) ? "(0) " : "", min_value, max_value);
		} else {
			IConsolePrintF(CC_WARNING, "Current value for '%s' is: '%s'",
				name, value);
		}
	}
}

static void IConsoleListSettingsTable(const SettingTable &table, const char *prefilter, bool show_defaults)
{
	for (auto &sd : table) {
		if (!SlIsObjectCurrentlyValid(sd->save.version_from, sd->save.version_to, sd->save.ext_feature_test)) continue;
		if (prefilter != nullptr && strstr(sd->name, prefilter) == nullptr) continue;
		if ((sd->flags & SF_NO_NEWGAME) && _game_mode == GM_MENU) continue;
		char value[80];
		sd->FormatValue(value, lastof(value), &GetGameSettings());
		if (show_defaults && sd->IsIntSetting()) {
			const IntSettingDesc *int_setting = sd->AsIntSetting();
			char defvalue[80];
			int_setting->FormatIntValue(defvalue, lastof(defvalue), int_setting->def);
			TextColour colour = (int_setting->Read(&GetGameSettings()) != int_setting->def) ? CC_WARNING : CC_DEFAULT;
			IConsolePrintF(colour, "%s = %s (default: %s)", sd->name, value, defvalue);
		} else {
			IConsolePrintF(CC_DEFAULT, "%s = %s", sd->name, value);
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
	IConsolePrintF(CC_WARNING, "All settings with their current %s:", show_defaults ? "and default values" : "value");

	for (auto &table : _generic_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}
	for (auto &table : _private_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}
	for (auto &table : _secrets_setting_tables) {
		IConsoleListSettingsTable(table, prefilter, show_defaults);
	}

	IConsolePrintF(CC_WARNING, "Use 'setting' command to change a value");
}

/**
 * Load handler for settings, which don't go in the PATX chunk, and which are a cross-reference to another setting
 * @param osd SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettingsXref(const SettingDesc *osd, void *object) {
	DEBUG(sl, 3, "PATS chunk: Loading xref setting: '%s'", osd->xref.target);
	const SettingDesc *setting_xref = GetSettingFromFullName(osd->xref.target);
	assert(setting_xref != nullptr);

	// Generate a new SaveLoad from the xref target using the version params from the source
	SaveLoad sld = setting_xref->save;
	sld.version_from     = osd->save.version_from;
	sld.version_to       = osd->save.version_to;
	sld.ext_feature_test = osd->save.ext_feature_test;

	if (!SlObjectMember(object, sld)) return;
	if (setting_xref->IsIntSetting()) {
		const IntSettingDesc *int_setting = setting_xref->AsIntSetting();
		int64 val = int_setting->Read(object);
		if (osd->xref.conv != nullptr) val = osd->xref.conv(val);
		int_setting->MakeValueValidAndWrite(object, val);
	}
}

/**
 * Save and load handler for settings, except for those which go in the PATX chunk
 * @param settings SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettings(const SettingTable &settings, void *object)
{
	extern SaveLoadVersion _sl_version;

	for (auto &osd : settings) {
		if (osd->flags & SF_NOT_IN_SAVE) continue;
		if (osd->patx_name != nullptr) continue;
		const SaveLoad &sld = osd->save;
		if (osd->xref.target != nullptr) {
			if (sld.ext_feature_test.IsFeaturePresent(_sl_version, sld.version_from, sld.version_to)) LoadSettingsXref(osd.get(), object);
			continue;
		}

		if (!SlObjectMember(object, osd->save)) continue;
		if (osd->IsIntSetting()) {
			const IntSettingDesc *int_setting = osd->AsIntSetting();
			int_setting->MakeValueValidAndWrite(object, int_setting->Read(object));
		}
	}
}

/**
 * Save and load handler for settings, except for those which go in the PATX chunk
 * @param settings SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void SaveSettings(const SettingTable &settings, void *object)
{
	/* We need to write the CH_RIFF header, but unfortunately can't call
	 * SlCalcLength() because we have a different format. So do this manually */
	size_t length = 0;
	for (auto &sd : settings) {
		if (sd->flags & SF_NOT_IN_SAVE) continue;
		if (sd->patx_name != nullptr) continue;
		if (sd->xref.target != nullptr) continue;
		length += SlCalcObjMemberLength(object, sd->save);
	}
	SlSetLength(length);

	for (auto &sd : settings) {
		if (sd->flags & SF_NOT_IN_SAVE) continue;
		if (sd->patx_name != nullptr) continue;
		if (sd->xref.target != nullptr) continue;
		SlObjectMember(object, sd->save);
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
 * uint32                               chunk flags (unused)
 * uint32                               number of settings
 *     For each of N settings:
 *     uint32                           setting flags (unused)
 *     SLE_STR                          setting name
 *     uint32                           length of setting field
 *         N bytes                      setting field
 */

/** Sorted list of PATX settings, generated by MakeSettingsPatxList */
static std::vector<const SettingDesc *> _sorted_patx_settings;

/**
 * Prepare a sorted list of settings to be potentially be loaded out of the PATX chunk
 * This is to enable efficient lookup of settings by name
 * This is stored in _sorted_patx_settings
 */
static void MakeSettingsPatxList(const SettingTable &settings)
{
	static const SettingTable *previous = nullptr;

	if (&settings == previous) return;
	previous = &settings;

	_sorted_patx_settings.clear();
	for (auto &sd : settings) {
		if (sd->patx_name == nullptr) continue;
		_sorted_patx_settings.push_back(sd.get());
	}

	std::sort(_sorted_patx_settings.begin(), _sorted_patx_settings.end(), [](const SettingDesc *a, const SettingDesc *b) {
		return strcmp(a->patx_name, b->patx_name) < 0;
	});
}

/**
 * Internal structure used in LoadSettingsPatx() and LoadSettingsPlyx()
 */
struct SettingsExtLoad {
	uint32 flags;
	char name[256];
	uint32 setting_length;
};

static const SaveLoad _settings_ext_load_desc[] = {
	SLE_VAR(SettingsExtLoad, flags,          SLE_UINT32),
	SLE_STR(SettingsExtLoad, name,           SLE_STRB, 256),
	SLE_VAR(SettingsExtLoad, setting_length, SLE_UINT32),
};

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

/**
 * Load handler for settings which go in the PATX chunk
 * @param osd SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void LoadSettingsPatx(const SettingTable &settings, void *object)
{
	MakeSettingsPatxList(settings);

	SettingsExtLoad current_setting;

	uint32 flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (flags != 0) SlErrorCorruptFmt("PATX chunk: unknown chunk header flags: 0x%X", flags);

	uint32 settings_count = SlReadUint32();
	for (uint32 i = 0; i < settings_count; i++) {
		SlObject(&current_setting, _settings_ext_load_desc);

		// flags are not in use yet, reserve for future expansion
		if (current_setting.flags != 0) SlErrorCorruptFmt("PATX chunk: unknown setting header flags: 0x%X", current_setting.flags);

		// now try to find corresponding setting
		bool exact_match = false;
		auto iter = std::lower_bound(_sorted_patx_settings.begin(), _sorted_patx_settings.end(), current_setting.name, [&](const SettingDesc *a, const char *b) {
			int result = strcmp(a->patx_name, b);
			if (result == 0) exact_match = true;
			return result < 0;
		});

		if (exact_match) {
			assert(iter != _sorted_patx_settings.end());
			// found setting
			const SettingDesc *setting = (*iter);
			const SaveLoad &sld = setting->save;
			size_t read = SlGetBytesRead();
			SlObjectMember(object, sld);
			if (SlGetBytesRead() != read + current_setting.setting_length) {
				SlErrorCorruptFmt("PATX chunk: setting read length mismatch for setting: '%s'", current_setting.name);
			}
			if (setting->IsIntSetting()) {
				const IntSettingDesc *int_setting = setting->AsIntSetting();
				int_setting->MakeValueValidAndWrite(object, int_setting->Read(object));
			}
		} else {
			DEBUG(sl, 1, "PATX chunk: Could not find setting: '%s', ignoring", current_setting.name);
			SlSkipBytes(current_setting.setting_length);
		}
	}
}

/**
 * Save handler for settings which go in the PATX chunk
 * @param sd SettingDesc struct containing all information
 * @param object can be either nullptr in which case we load global variables or
 * a pointer to a struct which is getting saved
 */
static void SaveSettingsPatx(const SettingTable &settings, void *object)
{
	SettingsExtSave current_setting;

	struct SettingToAdd {
		const SettingDesc *setting;
		uint32 setting_length;
	};
	std::vector<SettingToAdd> settings_to_add;

	size_t length = 8;
	for (auto &sd : settings) {
		if (sd->patx_name == nullptr) continue;
		uint32 setting_length = (uint32)SlCalcObjMemberLength(object, sd->save);
		if (!setting_length) continue;

		current_setting.name = sd->patx_name;

		// add length of setting header
		length += SlCalcObjLength(&current_setting, _settings_ext_save_desc);

		// add length of actual setting
		length += setting_length;

		// duplicate copy made for compiler backwards compatibility
		SettingToAdd new_setting = { sd.get(), setting_length };
		settings_to_add.push_back(new_setting);
	}
	SlSetLength(length);

	SlWriteUint32(0);                              // flags
	SlWriteUint32((uint32)settings_to_add.size()); // settings count

	for (size_t i = 0; i < settings_to_add.size(); i++) {
		const SettingDesc *desc = settings_to_add[i].setting;
		current_setting.flags = 0;
		current_setting.name = desc->patx_name;
		current_setting.setting_length = settings_to_add[i].setting_length;
		SlObject(&current_setting, _settings_ext_save_desc);
		SlObjectMember(object, desc->save);
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
 * uint32                               chunk flags (unused)
 * uint32                               number of companies
 *     For each of N companies:
 *     uint32                           company ID
 *     uint32                           company flags (unused)
 *     uint32                           number of settings
 *         For each of N settings:
 *         uint32                       setting flags (unused)
 *         SLE_STR                      setting name
 *         uint32                       length of setting field
 *             N bytes                  setting field
 */

/**
 * Load handler for company settings which go in the PLYX chunk
 * @param check_mode Whether to skip over settings without reading
 */
void LoadSettingsPlyx(bool skip)
{
	SettingsExtLoad current_setting;

	uint32 chunk_flags = SlReadUint32();
	// flags are not in use yet, reserve for future expansion
	if (chunk_flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown chunk header flags: 0x%X", chunk_flags);

	uint32 company_count = SlReadUint32();
	for (uint32 i = 0; i < company_count; i++) {
		uint32 company_id = SlReadUint32();
		if (company_id >= MAX_COMPANIES) SlErrorCorruptFmt("PLYX chunk: invalid company ID: %u", company_id);

		const Company *c = nullptr;
		if (!skip) {
			c = Company::GetIfValid(company_id);
			if (c == nullptr) SlErrorCorruptFmt("PLYX chunk: non-existant company ID: %u", company_id);
		}

		uint32 company_flags = SlReadUint32();
		// flags are not in use yet, reserve for future expansion
		if (company_flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown company flags: 0x%X", company_flags);

		uint32 settings_count = SlReadUint32();
		for (uint32 j = 0; j < settings_count; j++) {
			SlObject(&current_setting, _settings_ext_load_desc);

			// flags are not in use yet, reserve for future expansion
			if (current_setting.flags != 0) SlErrorCorruptFmt("PLYX chunk: unknown setting header flags: 0x%X", current_setting.flags);

			if (skip) {
				SlSkipBytes(current_setting.setting_length);
				continue;
			}

			const SettingDesc *setting = nullptr;

			// not many company settings, so perform a linear scan
			for (auto &sd : _company_settings) {
				if (sd->patx_name != nullptr && strcmp(sd->patx_name, current_setting.name) == 0) {
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
					SlErrorCorruptFmt("PLYX chunk: setting read length mismatch for setting: '%s'", current_setting.name);
				}
				if (setting->IsIntSetting()) {
					const IntSettingDesc *int_setting = setting->AsIntSetting();
					int_setting->MakeValueValidAndWrite(&(c->settings), int_setting->Read(&(c->settings)));
				}
			} else {
				DEBUG(sl, 1, "PLYX chunk: Could not find company setting: '%s', ignoring", current_setting.name);
				SlSkipBytes(current_setting.setting_length);
			}
		}
	}
}

/**
 * Save handler for settings which go in the PLYX chunk
 */
void SaveSettingsPlyx()
{
	SettingsExtSave current_setting;

	static const SaveLoad _settings_plyx_desc[] = {
		SLE_VAR(SettingsExtSave, flags,          SLE_UINT32),
		SLE_STR(SettingsExtSave, name,           SLE_STR, 0),
		SLE_VAR(SettingsExtSave, setting_length, SLE_UINT32),
	};

	std::vector<uint32> company_setting_counts;

	size_t length = 8;
	uint32 companies_count = 0;

	for (Company *c : Company::Iterate()) {
		length += 12;
		companies_count++;
		uint32 setting_count = 0;
		for (auto &sd : _company_settings) {
			if (sd->patx_name == nullptr) continue;
			uint32 setting_length = (uint32)SlCalcObjMemberLength(&(c->settings), sd->save);
			if (!setting_length) continue;

			current_setting.name = sd->patx_name;

			// add length of setting header
			length += SlCalcObjLength(&current_setting, _settings_ext_save_desc);

			// add length of actual setting
			length += setting_length;

			setting_count++;
		}
		company_setting_counts.push_back(setting_count);
	}
	SlSetLength(length);

	SlWriteUint32(0);                          // flags
	SlWriteUint32(companies_count);            // companies count

	size_t index = 0;
	for (Company *c : Company::Iterate()) {
		length += 12;
		companies_count++;
		SlWriteUint32(c->index);               // company ID
		SlWriteUint32(0);                      // flags
		SlWriteUint32(company_setting_counts[index]); // setting count
		index++;

		for (auto &sd : _company_settings) {
			if (sd->patx_name == nullptr) continue;
			uint32 setting_length = (uint32)SlCalcObjMemberLength(&(c->settings), sd->save);
			if (!setting_length) continue;

			current_setting.flags = 0;
			current_setting.name = sd->patx_name;
			current_setting.setting_length = setting_length;
			SlObject(&current_setting, _settings_plyx_desc);
			SlObjectMember(&(c->settings), sd->save);
		}
	}
}

static void Load_OPTS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * autosave-frequency stays when joining a network-server */
	PrepareOldDiffCustom();
	LoadSettings(_gameopt_settings, &_settings_game);
	HandleOldDiffCustom(true);
}

static void Load_PATS()
{
	/* Copy over default setting since some might not get loaded in
	 * a networking environment. This ensures for example that the local
	 * currency setting stays when joining a network-server */
	LoadSettings(_settings, &_settings_game);
}

static void Check_PATS()
{
	LoadSettings(_settings, &_load_check_data.settings);
}

static void Save_PATS()
{
	SaveSettings(_settings, &_settings_game);
}

static void Load_PATX()
{
	LoadSettingsPatx(_settings, &_settings_game);
}

static void Check_PATX()
{
	LoadSettingsPatx(_settings, &_load_check_data.settings);
}

static void Save_PATX()
{
	SaveSettingsPatx(_settings, &_settings_game);
}

static const ChunkHandler setting_chunk_handlers[] = {
	{ 'OPTS', nullptr,   Load_OPTS, nullptr, nullptr,    CH_RIFF },
	{ 'PATS', Save_PATS, Load_PATS, nullptr, Check_PATS, CH_RIFF },
	{ 'PATX', Save_PATX, Load_PATX, nullptr, Check_PATX, CH_RIFF },
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

const SettingTable &GetSettingsTableInternal()
{
	return _settings;
}
