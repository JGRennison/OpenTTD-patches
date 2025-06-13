/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settings_internal.h Functions and types used internally for the settings configurations. */

#ifndef SETTINGS_INTERNAL_H
#define SETTINGS_INTERNAL_H

#include "sl/saveload_types.h"

#include <functional>
#include <initializer_list>
#include <vector>

enum SaveToConfigFlags : uint32_t;

enum class SettingFlag : uint8_t {
	GuiZeroIsSpecial,        ///< A value of zero is possible and has a custom string (the one after "strval").
	GuiDropdown,             ///< The value represents a limited number of string-options (internally integer) presented as dropdown.
	GuiCurrency,             ///< The number represents money, so when reading value multiply by exchange rate.
	NetworkOnly,             ///< This setting only applies to network games.
	NoNetwork,               ///< This setting does not apply to network games; it may not be changed during the game.
	NewgameOnly,             ///< This setting cannot be changed in a game.
	SceneditToo,             ///< This setting can be changed in the scenario editor (only makes sense when SettingFlag::NewgameOnly is set).
	SceneditOnly,            ///< This setting can only be changed in the scenario editor.
	PerCompany,              ///< This setting can be different for each company (saved in company struct).
	NotInSave,               ///< Do not save with savegame, basically client-based.
	NotInConfig,             ///< Do not save to config file.
	NoNetworkSync,           ///< Do not synchronize over network (but it is saved if SettingFlag::NotInSave is not set).
	Sandbox,                 ///< This setting is a sandbox setting.
	Enum,                    ///< The setting can take one of the values given by an array of struct SettingDescEnumEntry.
	NoNewgame,               ///< The setting does not apply and is not shown in a new game context.
	RunCallbacksOnParse,     ///< Run callbacks when parsing from config file.
	GuiVelocity,             ///< Setting value is a velocity.
	EnumPreCallbackValidate, ///< Call the pre_check callback for enum incoming value validation.
	ConvertBoolToInt,        ///< Accept a boolean value when loading an int-type setting from the config file.
	Patch,                   ///< Do not load from upstream table-mode PATS, also for GUI filtering of "patch" settings.
	Private,                 ///< Setting is in private ini.
	Secret,                  ///< Setting is in secrets ini.
};
using SettingFlags = EnumBitSet<SettingFlag, uint32_t>;

/**
 * A SettingCategory defines a grouping of the settings.
 * The group #SC_BASIC is intended for settings which also a novice player would like to change and is able to understand.
 * The group #SC_ADVANCED is intended for settings which an experienced player would like to use. This is the case for most settings.
 * Finally #SC_EXPERT settings only few people want to see in rare cases.
 * The grouping is meant to be inclusive, i.e. all settings in #SC_BASIC also will be included
 * in the set of settings in #SC_ADVANCED. The group #SC_EXPERT contains all settings.
 */
enum SettingCategory : uint8_t {
	SC_NONE = 0,

	/* Filters for the list */
	SC_BASIC_LIST      = 1 << 0,    ///< Settings displayed in the list of basic settings.
	SC_ADVANCED_LIST   = 1 << 1,    ///< Settings displayed in the list of advanced settings.
	SC_EXPERT_LIST     = 1 << 2,    ///< Settings displayed in the list of expert settings.

	/* Setting classification */
	SC_BASIC           = SC_BASIC_LIST | SC_ADVANCED_LIST | SC_EXPERT_LIST,  ///< Basic settings are part of all lists.
	SC_ADVANCED        = SC_ADVANCED_LIST | SC_EXPERT_LIST,                  ///< Advanced settings are part of advanced and expert list.
	SC_EXPERT          = SC_EXPERT_LIST,                                     ///< Expert settings can only be seen in the expert list.

	SC_END,
};
DECLARE_ENUM_AS_BIT_SET(SettingCategory)

/**
 * Type of settings for filtering.
 */
enum SettingType : uint8_t {
	ST_GAME,      ///< Game setting.
	ST_COMPANY,   ///< Company setting.
	ST_CLIENT,    ///< Client setting.

	ST_ALL,       ///< Used in setting filter to match all types.
};

enum SettingOnGuiCtrlType {
	SOGCT_DESCRIPTION_TEXT,   ///< Description text callback
	SOGCT_GUI_DROPDOWN_ORDER, ///< SettingFlag::GuiDropdown reordering callback
	SOGCT_CFG_NAME,           ///< Config file name override
	SOGCT_CFG_FALLBACK_NAME,  ///< Config file name within group fallback
	SOGCT_GUI_SPRITE,         ///< Show sprite after setting value (i.e. warning)
	SOGCT_GUI_WARNING_TEXT,   ///< Show warning text
	SOGCT_GUI_DISABLE,        ///< Disable setting in GUI
};

struct SettingOnGuiCtrlData {
	SettingOnGuiCtrlType type;
	StringID text;
	int val;
	const char *str = nullptr;
	int output = 0;
};

struct IniItem;
typedef bool OnGuiCtrl(SettingOnGuiCtrlData &data); ///< callback prototype for GUI operations
typedef int64_t OnXrefValueConvert(int64_t val); ///< callback prototype for xref value conversion

/** The last entry in an array of struct SettingDescEnumEntry must use STR_NULL. */
struct SettingDescEnumEntry {
	int32_t val;
	StringID str;
};

/** Properties of config file settings. */
struct SettingDesc {
	SettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name) :
		name(name), flags(flags), startup(startup), save(save), guiproc(guiproc), patx_name(patx_name) {}
	virtual ~SettingDesc() = default;

	const char *name;       ///< Name of the setting. Used in configuration file and for console
	SettingFlags flags;     ///< Handles how a setting would show up in the GUI (text/currency, etc.)
	bool startup;           ///< Setting has to be loaded directly at startup?
	SaveLoad save;          ///< Internal structure (going to savegame, parts to config)
	OnGuiCtrl *guiproc;     ///< Callback procedure for GUI operations

	const char *patx_name;  ///< Name to save/load setting from in PATX chunk, if nullptr save/load from PATS chunk as normal

	bool IsEditable(bool do_command = false) const;
	SettingType GetType() const;

	/**
	 * Check whether this setting is an integer type setting.
	 * @return True when the underlying type is an integer.
	 */
	virtual bool IsIntSetting() const { return false; }

	/**
	 * Check whether this setting is an string type setting.
	 * @return True when the underlying type is a string.
	 */
	virtual bool IsStringSetting() const { return false; }

	const struct IntSettingDesc *AsIntSetting() const;
	const struct StringSettingDesc *AsStringSetting() const;

	/**
	 * Format the value of the setting associated with this object.
	 * @param buf The buffer to format into.
	 * @param object The object the setting is in.
	 */
	virtual void FormatValue(struct format_target &buf, const void *object) const = 0;

	/**
	 * Parse/read the value from the Ini item into the setting associated with this object.
	 * @param item The Ini item with the content of this setting.
	 * @param object The object the setting is in.
	 */
	virtual void ParseValue(const IniItem *item, void *object) const = 0;

	/**
	 * Check whether the value in the Ini item is the same as is saved in this setting in the object.
	 * It might be that determining whether the value is the same is way more expensive than just
	 * writing the value. In those cases this function may unconditionally return false even though
	 * the value might be the same as in the Ini item.
	 * @param item The Ini item with the content of this setting.
	 * @param object The object the setting is in.
	 * @return True if the value is definitely the same (might be false when the same).
	 */
	virtual bool IsSameValue(const IniItem *item, void *object) const = 0;

	/**
	 * Check whether the value is the same as the default value.
	 *
	 * @param object The object the setting is in.
	 * @return true iff the value is the default value.
	 */
	virtual bool IsDefaultValue(void *object) const = 0;

	/**
	 * Reset the setting to its default value.
	 */
	virtual void ResetToDefault(void *object) const = 0;
};

/** Base integer type, including boolean, settings. Only these are shown in the settings UI. */
struct IntSettingDesc : SettingDesc {
	using GetTitleCallback = StringID(const IntSettingDesc &sd);
	using GetHelpCallback = StringID(const IntSettingDesc &sd);
	using GetValueParamsCallback = std::pair<StringParameter, StringParameter>(const IntSettingDesc &sd, int32_t value);
	using GetDefaultValueCallback = int32_t(const IntSettingDesc &sd);
	using GetRangeCallback = std::tuple<int32_t, uint32_t>(const IntSettingDesc &sd);

	/**
	 * A check to be performed before the setting gets changed. The passed integer may be
	 * changed by the check if that is important, for example to remove some unwanted bit.
	 * The return value denotes whether the value, potentially after the changes,
	 * is allowed to be used/set in the configuration.
	 * @param value The prospective new value for the setting.
	 * @return True when the setting is accepted.
	 */
	using PreChangeCheck = bool(int32_t &value);
	/**
	 * A callback to denote that a setting has been changed.
	 * @param The new value for the setting.
	 */
	using PostChangeCallback = void(int32_t value);

	IntSettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name, int32_t def,
			int32_t min, uint32_t max, int32_t interval, StringID str, StringID str_help, StringID str_val,
			SettingCategory cat, PreChangeCheck pre_check, PostChangeCallback post_callback,
			GetTitleCallback get_title_cb, GetHelpCallback get_help_cb, GetValueParamsCallback get_value_params_cb,
			GetDefaultValueCallback get_def_cb, GetRangeCallback get_range_cb,
			const SettingDescEnumEntry *enumlist) :
		SettingDesc(save, name, flags, guiproc, startup, patx_name), def(def), min(min), max(max), interval(interval),
			str(str), str_help(str_help), str_val(str_val), cat(cat), pre_check(pre_check), post_callback(post_callback),
			get_title_cb(get_title_cb), get_help_cb(get_help_cb), get_value_params_cb(get_value_params_cb),
			get_def_cb(get_def_cb), get_range_cb(get_range_cb),
			enumlist(enumlist) {}

	int32_t def;            ///< default value given when none is present
	int32_t min;            ///< minimum values
	uint32_t max;           ///< maximum values
	int32_t interval;       ///< the interval to use between settings in the 'settings' window. If interval is '0' the interval is dynamically determined
	StringID str;           ///< (translated) string with descriptive text; gui and console
	StringID str_help;      ///< (Translated) string with help text; gui only.
	StringID str_val;       ///< (Translated) first string describing the value.
	SettingCategory cat;    ///< assigned categories of the setting
	PreChangeCheck *pre_check;         ///< Callback to check for the validity of the setting.
	PostChangeCallback *post_callback; ///< Callback when the setting has been changed.
	GetTitleCallback *get_title_cb;
	GetHelpCallback *get_help_cb;
	GetValueParamsCallback *get_value_params_cb;
	GetDefaultValueCallback *get_def_cb; ///< Callback to set the correct default value
	GetRangeCallback *get_range_cb;

	const SettingDescEnumEntry *enumlist; ///< For SF_ENUM. The last entry must use STR_NULL

	StringID GetTitle() const;
	StringID GetHelp() const;
	std::pair<StringParameter, StringParameter> GetValueParams(int32_t value) const;
	int32_t GetDefaultValue() const;
	std::tuple<int32_t, uint32_t> GetRange() const;

	/**
	 * Check whether this setting is a boolean type setting.
	 * @return True when the underlying type is an integer.
	 */
	virtual bool IsBoolSetting() const { return false; }
	bool IsIntSetting() const override { return true; }

	void ChangeValue(const void *object, int32_t newvalue, SaveToConfigFlags ini_save_flags) const;
	void MakeValueValidAndWrite(const void *object, int32_t value) const;

	virtual size_t ParseValue(const char *str) const;
	void FormatValue(struct format_target &buf, const void *object) const override;
	virtual void FormatIntValue(struct format_target &buf, uint32_t value) const;
	void ParseValue(const IniItem *item, void *object) const override;
	bool IsSameValue(const IniItem *item, void *object) const override;
	bool IsDefaultValue(void *object) const override;
	void ResetToDefault(void *object) const override;
	int32_t Read(const void *object) const;

private:
	void MakeValueValid(int32_t &value) const;
	void Write(const void *object, int32_t value) const;
};

/** Boolean setting. */
struct BoolSettingDesc : IntSettingDesc {
	BoolSettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name, bool def,
			StringID str, StringID str_help, StringID str_val, SettingCategory cat,
			PreChangeCheck pre_check, PostChangeCallback post_callback,
			GetTitleCallback get_title_cb, GetHelpCallback get_help_cb, GetValueParamsCallback get_value_params_cb,
			GetDefaultValueCallback get_def_cb) :
		IntSettingDesc(save, name, flags, guiproc, startup, patx_name, def, 0, 1, 0, str, str_help, str_val, cat,
			pre_check, post_callback, get_title_cb, get_help_cb, get_value_params_cb, get_def_cb, nullptr, nullptr) {}

	static std::optional<bool> ParseSingleValue(const char *str);

	bool IsBoolSetting() const override { return true; }
	size_t ParseValue(const char *str) const override;
	void FormatIntValue(struct format_target &buf, uint32_t value) const override;
};

/** One of many setting. */
struct OneOfManySettingDesc : IntSettingDesc {
	typedef size_t OnConvert(const char *value); ///< callback prototype for conversion error

	OneOfManySettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name,
			int32_t def, int32_t max, StringID str, StringID str_help, StringID str_val, SettingCategory cat,
			PreChangeCheck pre_check, PostChangeCallback post_callback,
			GetTitleCallback get_title_cb, GetHelpCallback get_help_cb, GetValueParamsCallback get_value_params_cb,
			GetDefaultValueCallback get_def_cb, std::initializer_list<const char *> many, OnConvert *many_cnvt) :
		IntSettingDesc(save, name, flags, guiproc, startup, patx_name, def, 0, max, 0, str, str_help, str_val, cat,
			pre_check, post_callback, get_title_cb, get_help_cb, get_value_params_cb, get_def_cb, nullptr, nullptr), many_cnvt(many_cnvt)
	{
		for (auto one : many) this->many.push_back(one);
	}

	std::vector<std::string> many; ///< possible values for this type
	OnConvert *many_cnvt;          ///< callback procedure when loading value mechanism fails

	static size_t ParseSingleValue(const char *str, size_t len, const std::vector<std::string> &many);
	void FormatSingleValue(struct format_target &buf, uint id) const;

	size_t ParseValue(const char *str) const override;
	void FormatIntValue(struct format_target &buf, uint32_t value) const override;
};

/** Many of many setting. */
struct ManyOfManySettingDesc : OneOfManySettingDesc {
	ManyOfManySettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name,
			int32_t def, StringID str, StringID str_help, StringID str_val, SettingCategory cat,
			PreChangeCheck pre_check, PostChangeCallback post_callback,
			GetTitleCallback get_title_cb, GetHelpCallback get_help_cb, GetValueParamsCallback get_value_params_cb,
			GetDefaultValueCallback get_def_cb, std::initializer_list<const char *> many, OnConvert *many_cnvt) :
		OneOfManySettingDesc(save, name, flags, guiproc, startup, patx_name, def, (1 << many.size()) - 1, str, str_help,
			str_val, cat, pre_check, post_callback, get_title_cb, get_help_cb, get_value_params_cb, get_def_cb, many, many_cnvt) {}

	size_t ParseValue(const char *str) const override;
	void FormatIntValue(struct format_target &buf, uint32_t value) const override;
};

/** String settings. */
struct StringSettingDesc : SettingDesc {
	/**
	 * A check to be performed before the setting gets changed. The passed string may be
	 * changed by the check if that is important, for example to remove unwanted white
	 * space. The return value denotes whether the value, potentially after the changes,
	 * is allowed to be used/set in the configuration.
	 * @param value The prospective new value for the setting.
	 * @return True when the setting is accepted.
	 */
	typedef bool PreChangeCheck(std::string &value);
	/**
	 * A callback to denote that a setting has been changed.
	 * @param The new value for the setting.
	 */
	typedef void PostChangeCallback(const std::string &value);

	StringSettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name, const char *def,
			uint32_t max_length, PreChangeCheck pre_check, PostChangeCallback post_callback) :
		SettingDesc(save, name, flags, guiproc, startup, patx_name), def(def == nullptr ? "" : def), max_length(max_length),
			pre_check(pre_check), post_callback(post_callback) {}

	std::string def;                   ///< Default value given when none is present
	uint32_t max_length;               ///< Maximum length of the string, 0 means no maximum length
	PreChangeCheck *pre_check;         ///< Callback to check for the validity of the setting.
	PostChangeCallback *post_callback; ///< Callback when the setting has been changed.

	bool IsStringSetting() const override { return true; }
	void ChangeValue(const void *object, std::string &newval, SaveToConfigFlags ini_save_flags) const;

	void FormatValue(struct format_target &buf, const void *object) const override;
	void ParseValue(const IniItem *item, void *object) const override;
	bool IsSameValue(const IniItem *item, void *object) const override;
	bool IsDefaultValue(void *object) const override;
	void ResetToDefault(void *object) const override;
	const std::string &Read(const void *object) const;

private:
	void MakeValueValid(std::string &str) const;
	void Write(const void *object, const std::string &str) const;
};

/** List/array settings. */
struct ListSettingDesc : SettingDesc {
	ListSettingDesc(const SaveLoad &save, const char *name, SettingFlags flags, OnGuiCtrl *guiproc, bool startup, const char *patx_name, const char *def) :
		SettingDesc(save, name, flags, guiproc, startup, patx_name), def(def) {}

	const char *def;        ///< default value given when none is present

	void FormatValue(struct format_target &buf, const void *object) const override;
	void ParseValue(const IniItem *item, void *object) const override;
	bool IsSameValue(const IniItem *item, void *object) const override;
	bool IsDefaultValue(void *object) const override;
	void ResetToDefault(void *object) const override;
};

/** Placeholder for settings that have been removed, but might still linger in the savegame. */
struct NullSettingDesc : SettingDesc {
	NullSettingDesc(const SaveLoad &save) :
		SettingDesc(save, "", SettingFlag::NotInConfig, nullptr, false, nullptr) {}
	NullSettingDesc(const SaveLoad &save, const char *name, const char *patx_name) :
		SettingDesc(save, name, SettingFlag::NotInConfig, nullptr, false, patx_name) {}

	void FormatValue(struct format_target &buf, const void *object) const override { NOT_REACHED(); }
	void ParseValue(const IniItem *item, void *object) const override { NOT_REACHED(); }
	bool IsSameValue(const IniItem *item, void *object) const override { NOT_REACHED(); }
	bool IsDefaultValue(void *object) const override { NOT_REACHED(); }
	void ResetToDefault(void *object) const override {}
};

typedef std::initializer_list<std::unique_ptr<const SettingDesc>> SettingTable;

const SettingDesc *GetSettingFromName(std::string_view name);

bool SetSettingValue(const IntSettingDesc *sd, int32_t value, bool force_newgame = false);
bool SetSettingValue(const StringSettingDesc *sd, const std::string value, bool force_newgame = false);

std::vector<const SettingDesc *> GetFilteredSettingCollection(std::function<bool(const SettingDesc &desc)> func);

void IterateSettingsTables(std::function<void(const SettingTable &, void *)> handler);
std::initializer_list<SettingTable> GetSaveLoadSettingsTables();
const SettingTable &GetLinkGraphSettingTable();
uint GetSettingIndexByFullName(const SettingTable &table, const char *name);

/**
 * Get the setting at the given index into a settings table.
 * @param table The settings table.
 * @param index The index to look for.
 * @return The setting at the given index, or nullptr when the index is invalid.
 */
inline const SettingDesc *GetSettingDescription(const SettingTable &table, uint index)
{
	if (index >= table.size()) return nullptr;
	return table.begin()[index].get();
}

struct SettingTablesIterator {
	typedef const std::unique_ptr<const SettingDesc> value_type;
	typedef const std::unique_ptr<const SettingDesc> *pointer;
	typedef const std::unique_ptr<const SettingDesc> &reference;
	typedef size_t difference_type;
	typedef std::forward_iterator_tag iterator_category;

	explicit SettingTablesIterator(std::initializer_list<SettingTable> &src, std::initializer_list<SettingTable>::iterator outer)
		: src(src), outer(outer)
	{
		this->ResetInner();
		this->ValidateIndex();
	};

	explicit SettingTablesIterator(std::initializer_list<SettingTable> &src, std::initializer_list<SettingTable>::iterator outer, SettingTable::iterator inner)
		: src(src), outer(outer), inner(inner) {}

	bool operator==(const SettingTablesIterator &other) const { return this->outer == other.outer && this->inner == other.inner; }
	bool operator!=(const SettingTablesIterator &other) const { return !(*this == other); }
	const std::unique_ptr<const SettingDesc> &operator*() const { return *this->inner; }
	SettingTablesIterator &operator++() { ++this->inner; this->ValidateIndex(); return *this; }

private:
	std::initializer_list<SettingTable> &src;
	std::initializer_list<SettingTable>::iterator outer;
	SettingTable::iterator inner;

	void ResetInner()
	{
		this->inner = (this->outer != this->src.end()) ? this->outer->begin() : SettingTable::iterator();
	}

	void ValidateIndex()
	{
		while (this->outer != this->src.end() && this->inner == this->outer->end()) {
			++this->outer;
			this->ResetInner();
		}
	}
};

/* Wrapper to iterate the settings within a set of settings tables: std::initializer_list<SettingTable> */
struct IterateSettingTables {
	std::initializer_list<SettingTable> tables;

	IterateSettingTables(std::initializer_list<SettingTable> tables) : tables(tables) {}
	SettingTablesIterator begin() { return SettingTablesIterator(this->tables, this->tables.begin()); }
	SettingTablesIterator end() { return SettingTablesIterator(this->tables, this->tables.end(), SettingTable::iterator()); }
};

enum class SettingsCompatType : uint8_t {
	Null,
	Setting,
	Xref,
};

struct SettingsCompat {
	std::string name;                 ///< Name of the field.
	SettingsCompatType type;          ///< Compat type
	uint16_t length;                  ///< Length of the NULL field.
	SaveLoadVersion version_from;     ///< Save/load the variable starting from this savegame version.
	SaveLoadVersion version_to;       ///< Save/load the variable before this savegame version.
	SlXvFeatureTest ext_feature_test; ///< Extended feature test
	OnXrefValueConvert *xrefconv;     ///< Value conversion for xref
};

#endif /* SETTINGS_INTERNAL_H */
