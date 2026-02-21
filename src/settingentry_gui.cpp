/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file settingentry_gui.cpp Definitions of classes for handling display of individual configuration settings. */

#include "stdafx.h"
#include "company_base.h"
#include "company_func.h"
#include "debug.h"
#include "settingentry_gui.h"
#include "settings_gui.h"
#include "settings_internal.h"
#include "stringfilter_type.h"
#include "strings_func.h"

#include "table/sprites.h"
#include "table/strings.h"

#include "safeguards.h"


/* == BaseSettingEntry methods == */

/**
 * Initialization of a setting entry
 * @param level      Page nesting level of this entry
 */
void BaseSettingEntry::Init(uint8_t level)
{
	this->level = level;
}

/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool BaseSettingEntry::IsVisible(const BaseSettingEntry *item) const
{
	if (this->IsFiltered()) return false;
	return this == item;
}

/**
 * Find setting entry at row \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Current row number
 * @return The requested setting entry or \c nullptr if it not found (folded or filtered)
 */
BaseSettingEntry *BaseSettingEntry::FindEntry(uint row_num, uint *cur_row)
{
	if (this->IsFiltered()) return nullptr;
	if (row_num == *cur_row) return this;
	(*cur_row)++;
	return nullptr;
}

/**
 * Draw a row in the settings panel.
 *
 * The scrollbar uses rows of the page, while the page data structure is a tree of #SettingsPage and #SettingEntry objects.
 * As a result, the drawing routing traverses the tree from top to bottom, counting rows in \a cur_row until it reaches \a first_row.
 * Then it enables drawing rows while traversing until \a max_row is reached, at which point drawing is terminated.
 *
 * The \a parent_last parameter ensures that the vertical lines at the left are
 * only drawn when another entry follows, that it prevents output like
 * \verbatim
 *  |-- setting
 *  |-- (-) - Title
 *  |    |-- setting
 *  |    |-- setting
 * \endverbatim
 * The left-most vertical line is not wanted. It is prevented by setting the
 * appropriate bit in the \a parent_last parameter.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint BaseSettingEntry::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	if (this->IsFiltered()) return cur_row;
	if (cur_row >= max_row) return cur_row;

	bool rtl = _current_text_dir == TD_RTL;
	int offset = (rtl ? -static_cast<int>(BaseSettingEntry::circle_size.width) : static_cast<int>(BaseSettingEntry::circle_size.width)) / 2;
	int level_width = rtl ? -WidgetDimensions::scaled.hsep_indent : WidgetDimensions::scaled.hsep_indent;

	int x = rtl ? right : left;
	if (cur_row >= first_row) {
		PixelColour colour = GetColourGradient(COLOUR_ORANGE, SHADE_NORMAL);
		y += (cur_row - first_row) * BaseSettingEntry::line_height; // Compute correct y start position

		/* Draw vertical for parent nesting levels */
		for (uint lvl = 0; lvl < this->level; lvl++) {
			if (!HasBit(parent_last, lvl)) GfxDrawLine(x + offset, y, x + offset, y + BaseSettingEntry::line_height - 1, colour);
			x += level_width;
		}
		/* draw own |- prefix */
		int halfway_y = y + BaseSettingEntry::line_height / 2;
		int bottom_y = flags.Test(SettingEntryFlag::LastField) ? halfway_y : y + BaseSettingEntry::line_height - 1;
		GfxDrawLine(x + offset, y, x + offset, bottom_y, colour);
		/* Small horizontal line from the last vertical line */
		GfxDrawLine(x + offset, halfway_y, x + level_width - (rtl ? -WidgetDimensions::scaled.hsep_normal : WidgetDimensions::scaled.hsep_normal), halfway_y, colour);
		x += level_width;

		this->DrawSetting(settings_ptr, rtl ? left : x, rtl ? x : right, y, this == selected);
	}
	cur_row++;

	return cur_row;
}

/* == SettingEntry methods == */

/**
 * Initialization of a setting entry
 * @param level      Page nesting level of this entry
 */
void SettingEntry::Init(uint8_t level)
{
	BaseSettingEntry::Init(level);
	const SettingDesc *st = GetSettingFromName(this->name);
	assert_msg(st != nullptr, "name: {}", this->name);
	this->setting = st->AsIntSetting();
}

/* Sets the given setting entry to its default value */
void SettingEntry::ResetAll()
{
	SetSettingValue(this->setting, this->setting->GetDefaultValue());
}

/**
 * Set the button-depressed flags (#SettingsEntryFlag::LeftDepressed and #SettingsEntryFlag::RightDepressed) to a specified value
 * @param new_val New value for the button flags
 * @see SettingEntryFlags
 */
void SettingEntry::SetButtons(SettingEntryFlags new_val)
{
	assert((new_val & SEF_BUTTONS_MASK) == new_val); // Should not touch any flags outside the buttons
	this->flags.Set(SettingEntryFlag::LeftDepressed, new_val.Test(SettingEntryFlag::LeftDepressed));
	this->flags.Set(SettingEntryFlag::RightDepressed, new_val.Test(SettingEntryFlag::RightDepressed));
}

/** Return number of rows needed to display the (filtered) entry */
uint SettingEntry::Length() const
{
	return this->IsFiltered() ? 0 : 1;
}

/**
 * Get the biggest height of the help text(s), if the width is at least \a maxw. Help text gets wrapped if needed.
 * @param maxw Maximal width of a line help text.
 * @return Biggest height needed to display any help text of this node (and its descendants).
 */
uint SettingEntry::GetMaxHelpHeight(int maxw)
{
	return GetStringHeight(this->setting->GetHelp(), maxw);
}

bool SettingEntry::IsGUIEditable() const
{
	bool editable = this->setting->IsEditable();
	if (editable && this->setting->guiproc != nullptr) {
		SettingOnGuiCtrlData data;
		data.type = SOGCT_GUI_DISABLE;
		data.val = 0;
		if (this->setting->guiproc(data)) {
			editable = (data.val == 0);
		}
	}
	return editable;
}

/**
 * Checks whether an entry shall be made visible based on the restriction mode.
 * @param mode The current status of the restriction drop down box.
 * @return true if the entry shall be visible.
 */
bool SettingEntry::IsVisibleByRestrictionMode(RestrictionMode mode) const
{
	/* There shall not be any restriction, i.e. all settings shall be visible. */
	if (mode == RM_ALL) return true;

	const IntSettingDesc *sd = this->setting;

	if (mode == RM_BASIC) return (this->setting->cat & SC_BASIC_LIST) != 0;
	if (mode == RM_ADVANCED) return (this->setting->cat & SC_ADVANCED_LIST) != 0;
	if (mode == RM_PATCH) return this->setting->flags.Test(SettingFlag::Patch);

	/* Read the current value. */
	const void *object = ResolveObject(&GetGameSettings(), sd);
	int64_t current_value = sd->Read(object);
	int64_t filter_value;

	if (mode == RM_CHANGED_AGAINST_DEFAULT) {
		/* This entry shall only be visible, if the value deviates from its default value. */

		/* Read the default value. */
		filter_value = sd->GetDefaultValue();
	} else {
		assert(mode == RM_CHANGED_AGAINST_NEW);
		/* This entry shall only be visible, if the value deviates from
		 * its value is used when starting a new game. */

		/* Make sure we're not comparing the new game settings against itself. */
		assert(&GetGameSettings() != &_settings_newgame);

		/* Read the new game's value. */
		filter_value = sd->Read(ResolveObject(&_settings_newgame, sd));
	}

	return current_value != filter_value;
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what (due to filter text; not affected by restriction drop down box).
 * @return true if item remains visible
 */
bool SettingEntry::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	if (this->setting->flags.Test(SettingFlag::NoNewgame) && _game_mode == GM_MENU) {
		this->flags.Set(SettingEntryFlag::Filtered);
		return false;
	}
	this->flags.Reset(SettingEntryFlag::Filtered);

	bool visible = true;

	const IntSettingDesc *sd = this->setting;
	if (!force_visible && !filter.string.IsEmpty()) {
		/* Process the search text filter for this item. */
		filter.string.ResetState();

		filter.string.AddLine(GetString(sd->GetTitle(), STR_EMPTY));
		filter.string.AddLine(GetString(sd->GetHelp()));

		visible = filter.string.GetState();
	}

	if (visible) {
		if (filter.type != ST_ALL && sd->GetType() != filter.type) {
			filter.type_hides = true;
			visible = false;
		}
		if (!this->IsVisibleByRestrictionMode(filter.mode)) {
			if (filter.mode == RM_PATCH) filter.min_cat = RM_ALL;
			while (filter.min_cat < RM_ALL && (filter.min_cat == filter.mode || !this->IsVisibleByRestrictionMode(filter.min_cat))) filter.min_cat++;
			visible = false;
		}
	}

	if (!visible) this->flags.Set(SettingEntryFlag::Filtered);
	return visible;
}

const void *ResolveObject(const GameSettings *settings_ptr, const IntSettingDesc *sd)
{
	if (sd->flags.Test(SettingFlag::PerCompany)) {
		if (Company::IsValidID(_local_company) && _game_mode != GM_MENU) {
			return &Company::Get(_local_company)->settings;
		}
		return &_settings_client.company;
	}
	return settings_ptr;
}

/**
 * Function to draw setting value (button + text + current value)
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing
 * @param right        Right-most position in window/panel to draw
 * @param y            Upper-most position in window/panel to start drawing
 * @param highlight    Highlight entry.
 */
void SettingEntry::DrawSetting(GameSettings *settings_ptr, int left, int right, int y, bool highlight) const
{
	const IntSettingDesc *sd = this->setting;
	int state = (this->flags & SEF_BUTTONS_MASK).base();

	bool rtl = _current_text_dir == TD_RTL;
	uint buttons_left = rtl ? right + 1 - SETTING_BUTTON_WIDTH : left;
	uint text_left  = left + (rtl ? 0 : SETTING_BUTTON_WIDTH + WidgetDimensions::scaled.hsep_wide);
	uint text_right = right - (rtl ? SETTING_BUTTON_WIDTH + WidgetDimensions::scaled.hsep_wide : 0);
	uint button_y = y + (BaseSettingEntry::line_height - SETTING_BUTTON_HEIGHT) / 2;

	/* We do not allow changes of some items when we are a client in a networkgame */
	bool editable = this->IsGUIEditable();

	auto [min_val, max_val] = sd->GetRange();
	int32_t value = sd->Read(ResolveObject(settings_ptr, sd));
	if (sd->IsBoolSetting()) {
		/* Draw checkbox for boolean-value either on/off */
		DrawBoolButton(buttons_left, button_y, COLOUR_YELLOW, COLOUR_MAUVE, value != 0, editable);
	} else if (sd->flags.Any({SettingFlag::GuiDropdown, SettingFlag::Enum})) {
		/* Draw [v] button for settings of an enum-type */
		DrawDropDownButton(buttons_left, button_y, COLOUR_YELLOW, state != 0, editable);
	} else {
		/* Draw [<][>] boxes for settings of an integer-type */
		DrawArrowButtons(buttons_left, button_y, COLOUR_YELLOW, state,
				editable && value != (sd->flags.Test(SettingFlag::GuiZeroIsSpecial) ? 0 : min_val), editable && static_cast<uint32_t>(value) != max_val);
	}
	this->DrawSettingString(text_left, text_right, y + (BaseSettingEntry::line_height - GetCharacterHeight(FS_NORMAL)) / 2, highlight, value);
}

void SettingEntry::DrawSettingString(uint left, uint right, int y, bool highlight, int32_t value) const
{
	const IntSettingDesc *sd = this->setting;
	auto [param1, param2] = sd->GetValueParams(value);
	int edge = DrawString(left, right, y, GetString(sd->GetTitle(), STR_CONFIG_SETTING_VALUE, param1, param2), highlight ? TC_WHITE : TC_LIGHT_BLUE);

	if (this->setting->guiproc != nullptr && edge != 0) {
		SettingOnGuiCtrlData data;
		data.type = SOGCT_GUI_SPRITE;
		data.val = value;
		if (this->setting->guiproc(data)) {
			SpriteID sprite = (SpriteID)data.output;
			const Dimension warning_dimensions = GetSpriteSize(sprite);
			if ((int)warning_dimensions.height <= BaseSettingEntry::line_height) {
				DrawSprite(sprite, 0, (_current_text_dir == TD_RTL) ? edge - warning_dimensions.width - 5 : edge + 5,
						y + (((int)GetCharacterHeight(FS_NORMAL) - (int)warning_dimensions.height) / 2));
			}
		}
	}
}

/* == CargoDestPerCargoSettingEntry methods == */

CargoDestPerCargoSettingEntry::CargoDestPerCargoSettingEntry(CargoType cargo, const IntSettingDesc *setting)
	: SettingEntry(setting), cargo(cargo) {}

void CargoDestPerCargoSettingEntry::Init(uint8_t level)
{
	BaseSettingEntry::Init(level);
}

void CargoDestPerCargoSettingEntry::DrawSettingString(uint left, uint right, int y, bool highlight, int32_t value) const
{
	assert(this->setting->str == STR_CONFIG_SETTING_DISTRIBUTION_PER_CARGO);
	auto [param1, param2] = this->setting->GetValueParams(value);
	std::string str = GetString(STR_CONFIG_SETTING_DISTRIBUTION_PER_CARGO_PARAM, CargoSpec::Get(this->cargo)->name, STR_CONFIG_SETTING_VALUE, param1, param2);
	DrawString(left, right, y, str, highlight ? TC_WHITE : TC_LIGHT_BLUE);
}

bool CargoDestPerCargoSettingEntry::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	if (!HasBit(_cargo_mask, this->cargo)) {
		this->flags.Set(SettingEntryFlag::Filtered);
		return false;
	} else {
		return SettingEntry::UpdateFilterState(filter, force_visible);
	}
}

bool ConditionallyHiddenSettingEntry::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	if (this->hide_callback && this->hide_callback()) {
		this->flags.Set(SettingEntryFlag::Filtered);
		return false;
	} else {
		return SettingEntry::UpdateFilterState(filter, force_visible);
	}
}

/* == SettingsContainer methods == */

/**
 * Initialization of an entire setting page
 * @param level Nesting level of this page (internal variable, do not provide a value for it when calling)
 */
void SettingsContainer::Init(uint8_t level)
{
	for (auto &it : this->entries) {
		it->Init(level);
	}
}

/** Resets all settings to their default values */
void SettingsContainer::ResetAll()
{
	for (auto settings_entry : this->entries) {
		settings_entry->ResetAll();
	}
}

/** Recursively close all folds of sub-pages */
void SettingsContainer::FoldAll()
{
	for (auto &it : this->entries) {
		it->FoldAll();
	}
}

/** Recursively open all folds of sub-pages */
void SettingsContainer::UnFoldAll()
{
	for (auto &it : this->entries) {
		it->UnFoldAll();
	}
}

/**
 * Recursively accumulate the folding state of the tree.
 * @param[in,out] all_folded Set to false, if one entry is not folded.
 * @param[in,out] all_unfolded Set to false, if one entry is folded.
 */
void SettingsContainer::GetFoldingState(bool &all_folded, bool &all_unfolded) const
{
	for (auto &it : this->entries) {
		it->GetFoldingState(all_folded, all_unfolded);
	}
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what
 * @return true if item remains visible
 */
bool SettingsContainer::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	bool visible = false;
	bool first_visible = true;
	for (EntryVector::reverse_iterator it = this->entries.rbegin(); it != this->entries.rend(); ++it) {
		visible |= (*it)->UpdateFilterState(filter, force_visible);
		(*it)->SetLastField(first_visible);
		if (visible && first_visible) first_visible = false;
	}
	return visible;
}


/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool SettingsContainer::IsVisible(const BaseSettingEntry *item) const
{
	for (const auto &it : this->entries) {
		if (it->IsVisible(item)) return true;
	}
	return false;
}

/** Return number of rows needed to display the whole page */
uint SettingsContainer::Length() const
{
	uint length = 0;
	for (const auto &it : this->entries) {
		length += it->Length();
	}
	return length;
}

/**
 * Find the setting entry at row number \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Variable used for keeping track of the current row number. Should point to memory initialized to \c 0 when first called.
 * @return The requested setting entry or \c nullptr if it does not exist
 */
BaseSettingEntry *SettingsContainer::FindEntry(uint row_num, uint *cur_row)
{
	BaseSettingEntry *pe = nullptr;
	for (const auto &it : this->entries) {
		pe = it->FindEntry(row_num, cur_row);
		if (pe != nullptr) {
			break;
		}
	}
	return pe;
}

/**
 * Get the biggest height of the help texts, if the width is at least \a maxw. Help text gets wrapped if needed.
 * @param maxw Maximal width of a line help text.
 * @return Biggest height needed to display any help text of this (sub-)tree.
 */
uint SettingsContainer::GetMaxHelpHeight(int maxw)
{
	uint biggest = 0;
	for (const auto &it : this->entries) {
		biggest = std::max(biggest, it->GetMaxHelpHeight(maxw));
	}
	return biggest;
}


/**
 * Draw a row in the settings panel.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint SettingsContainer::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	for (const auto &it : this->entries) {
		cur_row = it->Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);
		if (cur_row >= max_row) break;
	}
	return cur_row;
}

/* == SettingsPage methods == */

/**
 * Constructor for a sub-page in the 'advanced settings' window
 * @param title Title of the sub-page
 */
SettingsPage::SettingsPage(StringID title)
{
	this->title = title;
	this->folded = true;
}

/**
 * Initialization of an entire setting page
 * @param level Nesting level of this page (internal variable, do not provide a value for it when calling)
 */
void SettingsPage::Init(uint8_t level)
{
	BaseSettingEntry::Init(level);
	SettingsContainer::Init(level + 1);
}

/** Resets all settings to their default values */
void SettingsPage::ResetAll()
{
	for (auto settings_entry : this->entries) {
		settings_entry->ResetAll();
	}
}

/** Recursively close all (filtered) folds of sub-pages */
void SettingsPage::FoldAll()
{
	if (this->IsFiltered()) return;
	this->folded = true;

	SettingsContainer::FoldAll();
}

/** Recursively open all (filtered) folds of sub-pages */
void SettingsPage::UnFoldAll()
{
	if (this->IsFiltered()) return;
	this->folded = false;

	SettingsContainer::UnFoldAll();
}

/**
 * Recursively accumulate the folding state of the (filtered) tree.
 * @param[in,out] all_folded Set to false, if one entry is not folded.
 * @param[in,out] all_unfolded Set to false, if one entry is folded.
 */
void SettingsPage::GetFoldingState(bool &all_folded, bool &all_unfolded) const
{
	if (this->IsFiltered()) return;

	if (this->folded) {
		all_unfolded = false;
	} else {
		all_folded = false;
	}

	SettingsContainer::GetFoldingState(all_folded, all_unfolded);
}

/**
 * Update the filter state.
 * @param filter Filter
 * @param force_visible Whether to force all items visible, no matter what (due to filter text; not affected by restriction drop down box).
 * @return true if item remains visible
 */
bool SettingsPage::UpdateFilterState(SettingFilter &filter, bool force_visible)
{
	if (!force_visible && !filter.string.IsEmpty()) {
		filter.string.ResetState();
		filter.string.AddLine(GetString(this->title));
		force_visible = filter.string.GetState();
	}

	bool visible = SettingsContainer::UpdateFilterState(filter, force_visible);
	if (this->hide_callback && this->hide_callback()) visible = false;
	this->flags.Set(SettingEntryFlag::Filtered, !visible);
	return visible;
}

/**
 * Check whether an entry is visible and not folded or filtered away.
 * Note: This does not consider the scrolling range; it might still require scrolling to make the setting really visible.
 * @param item Entry to search for.
 * @return true if entry is visible.
 */
bool SettingsPage::IsVisible(const BaseSettingEntry *item) const
{
	if (this->IsFiltered()) return false;
	if (this == item) return true;
	if (this->folded) return false;

	return SettingsContainer::IsVisible(item);
}

/** Return number of rows needed to display the (filtered) entry */
uint SettingsPage::Length() const
{
	if (this->IsFiltered()) return 0;
	if (this->folded) return 1; // Only displaying the title

	return 1 + SettingsContainer::Length();
}

/**
 * Find setting entry at row \a row_num
 * @param row_num Index of entry to return
 * @param cur_row Current row number
 * @return The requested setting entry or \c nullptr if it not found (folded or filtered)
 */
BaseSettingEntry *SettingsPage::FindEntry(uint row_num, uint *cur_row)
{
	if (this->IsFiltered()) return nullptr;
	if (row_num == *cur_row) return this;
	(*cur_row)++;
	if (this->folded) return nullptr;

	return SettingsContainer::FindEntry(row_num, cur_row);
}

/**
 * Draw a row in the settings panel.
 *
 * @param settings_ptr Pointer to current values of all settings
 * @param left         Left-most position in window/panel to start drawing \a first_row
 * @param right        Right-most x position to draw strings at.
 * @param y            Upper-most position in window/panel to start drawing \a first_row
 * @param first_row    First row number to draw
 * @param max_row      Row-number to stop drawing (the row-number of the row below the last row to draw)
 * @param selected     Selected entry by the user.
 * @param cur_row      Current row number (internal variable)
 * @param parent_last  Last-field booleans of parent page level (page level \e i sets bit \e i to 1 if it is its last field)
 * @return Row number of the next row to draw
 */
uint SettingsPage::Draw(GameSettings *settings_ptr, int left, int right, int y, uint first_row, uint max_row, BaseSettingEntry *selected, uint cur_row, uint parent_last) const
{
	if (this->IsFiltered()) return cur_row;
	if (cur_row >= max_row) return cur_row;

	cur_row = BaseSettingEntry::Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);

	if (!this->folded) {
		if (this->flags.Test(SettingEntryFlag::LastField)) {
			assert(this->level < 8 * sizeof(parent_last));
			SetBit(parent_last, this->level); // Add own last-field state
		}

		cur_row = SettingsContainer::Draw(settings_ptr, left, right, y, first_row, max_row, selected, cur_row, parent_last);
	}

	return cur_row;
}

/**
 * Function to draw setting value (button + text + current value)
 * @param left         Left-most position in window/panel to start drawing
 * @param right        Right-most position in window/panel to draw
 * @param y            Upper-most position in window/panel to start drawing
 */
void SettingsPage::DrawSetting(GameSettings *, int left, int right, int y, bool) const
{
	bool rtl = _current_text_dir == TD_RTL;
	DrawSprite((this->folded ? SPR_CIRCLE_FOLDED : SPR_CIRCLE_UNFOLDED), PAL_NONE, rtl ? right - BaseSettingEntry::circle_size.width : left, y + (BaseSettingEntry::line_height - BaseSettingEntry::circle_size.height) / 2);
	DrawString(rtl ? left : left + BaseSettingEntry::circle_size.width + WidgetDimensions::scaled.hsep_normal, rtl ? right - BaseSettingEntry::circle_size.width - WidgetDimensions::scaled.hsep_normal : right, y + (BaseSettingEntry::line_height - GetCharacterHeight(FS_NORMAL)) / 2, this->title, TC_ORANGE);
}

/** Construct settings tree */
SettingsContainer &GetSettingsTree()
{
	static SettingsContainer *main = nullptr;

	if (main == nullptr)
	{
		/* Build up the dynamic settings-array only once per OpenTTD session */
		main = new SettingsContainer();

		SettingsPage *localisation = main->Add(new SettingsPage(STR_CONFIG_SETTING_LOCALISATION));
		{
			localisation->Add(new SettingEntry("locale.units_velocity"));
			localisation->Add(new SettingEntry("locale.units_velocity_nautical"));
			localisation->Add(new SettingEntry("locale.units_power"));
			localisation->Add(new SettingEntry("locale.units_weight"));
			localisation->Add(new SettingEntry("locale.units_volume"));
			localisation->Add(new SettingEntry("locale.units_force"));
			localisation->Add(new SettingEntry("locale.units_height"));
			localisation->Add(new SettingEntry("gui.date_format_in_default_names"));
			localisation->Add(new SettingEntry("client_locale.sync_locale_network_server"));
		}

		SettingsPage *graphics = main->Add(new SettingsPage(STR_CONFIG_SETTING_GRAPHICS));
		{
			graphics->Add(new SettingEntry("gui.zoom_min"));
			graphics->Add(new SettingEntry("gui.zoom_max"));
			graphics->Add(new SettingEntry("gui.sprite_zoom_min"));
			graphics->Add(new SettingEntry("gui.shade_trees_on_slopes"));
			graphics->Add(new SettingEntry("gui.smallmap_land_colour"));
			graphics->Add(new SettingEntry("gui.linkgraph_colours"));
			graphics->Add(new SettingEntry("gui.graph_line_thickness"));
		}

		SettingsPage *sound = main->Add(new SettingsPage(STR_CONFIG_SETTING_SOUND));
		{
			sound->Add(new SettingEntry("sound.click_beep"));
			sound->Add(new SettingEntry("sound.confirm"));
			sound->Add(new SettingEntry("sound.news_ticker"));
			sound->Add(new SettingEntry("sound.news_full"));
			sound->Add(new SettingEntry("sound.new_year"));
			sound->Add(new SettingEntry("sound.disaster"));
			sound->Add(new SettingEntry("sound.vehicle"));
			sound->Add(new SettingEntry("sound.ambient"));
		}

		SettingsPage *interface = main->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE));
		{
			SettingsPage *general = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_GENERAL));
			{
				general->Add(new SettingEntry("gui.osk_activation"));
				general->Add(new SettingEntry("gui.errmsg_duration"));
				general->Add(new SettingEntry("gui.window_snap_radius"));
				general->Add(new SettingEntry("gui.window_soft_limit"));
				general->Add(new SettingEntry("gui.right_click_wnd_close"));
				general->Add(new SettingEntry("gui.toolbar_dropdown_autoselect"));
			}

			SettingsPage *tooltips = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_TOOLTIPS));
			{
				tooltips->Add(new SettingEntry("gui.hover_delay_ms"));
				tooltips->Add(new ConditionallyHiddenSettingEntry("gui.instant_tile_tooltip", []() -> bool { return _settings_client.gui.hover_delay_ms != 0; }));
				tooltips->Add(new SettingEntry("gui.town_name_tooltip_mode"));
				tooltips->Add(new SettingEntry("gui.industry_tooltip_show"));
				tooltips->Add(new ConditionallyHiddenSettingEntry("gui.industry_tooltip_show_name", []() -> bool { return !_settings_client.gui.industry_tooltip_show; }));
				tooltips->Add(new ConditionallyHiddenSettingEntry("gui.industry_tooltip_show_required", []() -> bool { return !_settings_client.gui.industry_tooltip_show; }));
				tooltips->Add(new ConditionallyHiddenSettingEntry("gui.industry_tooltip_show_stockpiled", []() -> bool { return !_settings_client.gui.industry_tooltip_show; }));
				tooltips->Add(new ConditionallyHiddenSettingEntry("gui.industry_tooltip_show_produced", []() -> bool { return !_settings_client.gui.industry_tooltip_show; }));
				tooltips->Add(new SettingEntry("gui.depot_tooltip_mode"));
				tooltips->Add(new SettingEntry("gui.waypoint_viewport_tooltip_name"));
				tooltips->Add(new SettingEntry("gui.station_viewport_tooltip_name"));
				tooltips->Add(new SettingEntry("gui.station_viewport_tooltip_cargo"));
				tooltips->Add(new SettingEntry("gui.station_rating_tooltip_mode"));
			}

			SettingsPage *save = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_SAVE));
			{
				save->Add(new SettingEntry("gui.autosave_interval"));
				save->Add(new SettingEntry("gui.autosave_realtime"));
				save->Add(new SettingEntry("gui.autosave_on_network_disconnect"));
				save->Add(new SettingEntry("gui.savegame_overwrite_confirm"));
			}

			SettingsPage *viewports = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_VIEWPORTS));
			{
				SettingsPage *viewport_map = viewports->Add(new SettingsPage(STR_CONFIG_SETTING_VIEWPORT_MAP_OPTIONS));
				{
					viewport_map->Add(new SettingEntry("gui.default_viewport_map_mode"));
					viewport_map->Add(new SettingEntry("gui.action_when_viewport_map_is_dblclicked"));
					viewport_map->Add(new SettingEntry("gui.show_scrolling_viewport_on_map"));
					viewport_map->Add(new SettingEntry("gui.show_slopes_on_viewport_map"));
					viewport_map->Add(new SettingEntry("gui.show_height_on_viewport_map"));
					viewport_map->Add(new SettingEntry("gui.show_bridges_on_map"));
					viewport_map->Add(new SettingEntry("gui.show_tunnels_on_map"));
					viewport_map->Add(new SettingEntry("gui.use_owner_colour_for_tunnelbridge"));
				}
				SettingsPage *viewport_plans = viewports->Add(new SettingsPage(STR_CONFIG_SETTING_PLANS));
				{
					viewport_plans->Add(new SettingEntry("gui.dash_level_of_plan_lines"));
					viewport_plans->Add(new SettingEntry("gui.selected_plan_line_mode"));
				}
				SettingsPage *viewport_route_overlay = viewports->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLE_ROUTE_OVERLAY));
				{
					viewport_route_overlay->Add(new SettingEntry("gui.show_vehicle_route_mode"));
					viewport_route_overlay->Add(new ConditionallyHiddenSettingEntry("gui.show_vehicle_route_steps", []() -> bool { return _settings_client.gui.show_vehicle_route_mode == 0; }));
					viewport_route_overlay->Add(new ConditionallyHiddenSettingEntry("gui.show_vehicle_route", []() -> bool { return _settings_client.gui.show_vehicle_route_mode == 0; }));
					viewport_route_overlay->Add(new ConditionallyHiddenSettingEntry("gui.dash_level_of_route_lines", []() -> bool { return _settings_client.gui.show_vehicle_route_mode == 0 || !_settings_client.gui.show_vehicle_route; }));
				}

				viewports->Add(new SettingEntry("gui.auto_scrolling"));
				viewports->Add(new SettingEntry("gui.scroll_mode"));
				viewports->Add(new SettingEntry("gui.smooth_scroll"));
				/* While the horizontal scrollwheel scrolling is written as general code, only
				 *  the cocoa (OSX) driver generates input for it.
				 *  Since it's also able to completely disable the scrollwheel will we display it on all platforms anyway */
				viewports->Add(new SettingEntry("gui.scrollwheel_scrolling"));
				viewports->Add(new SettingEntry("gui.scrollwheel_multiplier"));
#ifdef __APPLE__
				/* We might need to emulate a right mouse button on mac */
				viewports->Add(new SettingEntry("gui.right_mouse_btn_emulation"));
#endif
				viewports->Add(new SettingEntry("gui.population_in_label"));
				viewports->Add(new SettingEntry("gui.city_in_label"));
				viewports->Add(new SettingEntry("gui.liveries"));
				viewports->Add(new SettingEntry("gui.measure_tooltip"));
				viewports->Add(new SettingEntry("gui.loading_indicators"));
				viewports->Add(new SettingEntry("gui.show_track_reservation"));
				viewports->Add(new SettingEntry("gui.disable_water_animation"));
			}

			SettingsPage *construction = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_CONSTRUCTION));
			{
				construction->Add(new SettingEntry("gui.link_terraform_toolbar"));
				construction->Add(new SettingEntry("gui.persistent_buildingtools"));
				construction->Add(new SettingEntry("gui.default_rail_type"));
				construction->Add(new SettingEntry("gui.default_road_type"));
				construction->Add(new SettingEntry("gui.demolish_confirm_mode"));
				construction->Add(new SettingEntry("gui.show_rail_polyline_tool"));
			}

			SettingsPage *vehicle_windows = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_VEHICLE_WINDOWS));
			{
				vehicle_windows->Add(new SettingEntry("gui.advanced_vehicle_list"));
				vehicle_windows->Add(new SettingEntry("gui.show_newgrf_name"));
				vehicle_windows->Add(new SettingEntry("gui.show_cargo_in_vehicle_lists"));
				vehicle_windows->Add(new SettingEntry("gui.show_wagon_intro_year"));
				vehicle_windows->Add(new SettingEntry("gui.show_train_length_in_details"));
				vehicle_windows->Add(new SettingEntry("gui.show_train_weight_ratios_in_details"));
				vehicle_windows->Add(new SettingEntry("gui.show_vehicle_group_in_details"));
				vehicle_windows->Add(new SettingEntry("gui.show_vehicle_list_company_colour"));
				vehicle_windows->Add(new SettingEntry("gui.show_adv_load_mode_features"));
				vehicle_windows->Add(new SettingEntry("gui.disable_top_veh_list_mass_actions"));
				vehicle_windows->Add(new SettingEntry("gui.show_depot_sell_gui"));
				vehicle_windows->Add(new SettingEntry("gui.open_vehicle_gui_clone_share"));
				vehicle_windows->Add(new SettingEntry("gui.vehicle_names"));
				vehicle_windows->Add(new SettingEntry("gui.dual_pane_train_purchase_window"));
				vehicle_windows->Add(new ConditionallyHiddenSettingEntry("gui.dual_pane_train_purchase_window_dual_buttons", []() -> bool { return !_settings_client.gui.dual_pane_train_purchase_window; }));
				vehicle_windows->Add(new SettingEntry("gui.show_order_occupancy_by_default"));
				vehicle_windows->Add(new SettingEntry("gui.show_group_hierarchy_name"));
				vehicle_windows->Add(new ConditionallyHiddenSettingEntry("gui.show_vehicle_group_hierarchy_name", []() -> bool { return !_settings_client.gui.show_group_hierarchy_name; }));
				vehicle_windows->Add(new SettingEntry("gui.show_vehicle_route_id_vehicle_view"));
				vehicle_windows->Add(new SettingEntry("gui.enable_single_veh_shared_order_gui"));
				vehicle_windows->Add(new SettingEntry("gui.show_order_number_vehicle_view"));
				vehicle_windows->Add(new SettingEntry("gui.shorten_vehicle_view_status"));
				vehicle_windows->Add(new SettingEntry("gui.show_speed_first_vehicle_view"));
				vehicle_windows->Add(new SettingEntry("gui.hide_default_stop_location"));
				vehicle_windows->Add(new SettingEntry("gui.show_running_costs_calendar_year"));
			}

			SettingsPage *departureboards = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_DEPARTUREBOARDS));
			{
				departureboards->Add(new SettingEntry("gui.max_departures"));
				departureboards->Add(new ConditionallyHiddenSettingEntry("gui.max_departure_time", []() -> bool { return _settings_time.time_in_minutes; }));
				departureboards->Add(new ConditionallyHiddenSettingEntry("gui.max_departure_time_minutes", []() -> bool { return !_settings_time.time_in_minutes; }));
				departureboards->Add(new SettingEntry("gui.departure_calc_frequency"));
				departureboards->Add(new SettingEntry("gui.departure_show_vehicle"));
				departureboards->Add(new SettingEntry("gui.departure_show_group"));
				departureboards->Add(new SettingEntry("gui.departure_show_company"));
				departureboards->Add(new SettingEntry("gui.departure_show_vehicle_type"));
				departureboards->Add(new SettingEntry("gui.departure_show_vehicle_color"));
				departureboards->Add(new SettingEntry("gui.departure_larger_font"));
				departureboards->Add(new SettingEntry("gui.departure_destination_type"));
				departureboards->Add(new SettingEntry("gui.departure_smart_terminus"));
				departureboards->Add(new SettingEntry("gui.departure_conditionals"));
				departureboards->Add(new SettingEntry("gui.departure_merge_identical"));
			}

			SettingsPage *timetable = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_TIMETABLE));
			{
				SettingsPage *clock = timetable->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_TIMETABLE_CLOCK));
				{
					clock->Add(new SettingEntry("gui.override_time_settings"));
					SettingsPage *game = clock->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_TIME_SAVEGAME));
					{
						game->hide_callback = []() -> bool {
							return _game_mode == GM_MENU;
						};
						game->Add(new SettingEntry("game_time.time_in_minutes"));
						game->Add(new SettingEntry("game_time.ticks_per_minute"));
						game->Add(new SettingEntry("game_time.clock_offset"));
					}
					SettingsPage *client = clock->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_TIME_CLIENT));
					{
						client->hide_callback = []() -> bool {
							return _game_mode != GM_MENU && !_settings_client.gui.override_time_settings;
						};
						client->Add(new SettingEntry("gui.time_in_minutes"));
						client->Add(new SettingEntry("gui.ticks_per_minute"));
						client->Add(new SettingEntry("gui.clock_offset"));
					}

					clock->Add(new SettingEntry("gui.date_with_time"));
				}

				timetable->Add(new SettingEntry("gui.timetable_in_ticks"));
				timetable->Add(new SettingEntry("gui.timetable_leftover_time"));
				timetable->Add(new SettingEntry("gui.timetable_arrival_departure"));
				timetable->Add(new SettingEntry("gui.timetable_start_text_entry"));
			}

			SettingsPage *signals = interface->Add(new SettingsPage(STR_CONFIG_SETTING_INTERFACE_SIGNALS));
			{
				signals->Add(new SettingEntry("construction.train_signal_side"));
				signals->Add(new SettingEntry("gui.semaphore_build_before"));
				signals->Add(new SettingEntry("gui.signal_gui_mode"));
				signals->Add(new SettingEntry("gui.cycle_signal_types"));
				signals->Add(new SettingEntry("gui.drag_signals_fixed_distance"));
				signals->Add(new SettingEntry("gui.drag_signals_skip_stations"));
				signals->Add(new SettingEntry("gui.drag_signals_stop_restricted_signal"));
				signals->Add(new SettingEntry("gui.auto_remove_signals"));
				signals->Add(new SettingEntry("gui.show_restricted_signal_recolour"));
				signals->Add(new SettingEntry("gui.show_all_signal_default"));
				signals->Add(new SettingEntry("gui.show_progsig_ui"));
				signals->Add(new SettingEntry("gui.show_noentrysig_ui"));
				signals->Add(new SettingEntry("gui.show_adv_tracerestrict_features"));
				signals->Add(new SettingEntry("gui.adv_sig_bridge_tun_modes"));
			}

			interface->Add(new SettingEntry("gui.toolbar_pos"));
			interface->Add(new SettingEntry("gui.statusbar_pos"));
			interface->Add(new SettingEntry("gui.prefer_teamchat"));
			interface->Add(new SettingEntry("gui.show_rail_road_cost_dropdown"));
			interface->Add(new SettingEntry("gui.sort_track_types_by_speed"));
			interface->Add(new SettingEntry("gui.show_town_growth_status"));
			interface->Add(new SettingEntry("gui.allow_hiding_waypoint_labels"));
		}

		SettingsPage *advisors = main->Add(new SettingsPage(STR_CONFIG_SETTING_ADVISORS));
		{
			advisors->Add(new SettingEntry("gui.coloured_news_year"));
			advisors->Add(new SettingEntry("news_display.general"));
			advisors->Add(new SettingEntry("news_display.new_vehicles"));
			advisors->Add(new SettingEntry("news_display.accident"));
			advisors->Add(new SettingEntry("news_display.accident_other"));
			advisors->Add(new SettingEntry("news_display.company_info"));
			advisors->Add(new SettingEntry("news_display.acceptance"));
			advisors->Add(new SettingEntry("news_display.arrival_player"));
			advisors->Add(new SettingEntry("news_display.arrival_other"));
			advisors->Add(new SettingEntry("news_display.advice"));
			advisors->Add(new SettingEntry("gui.order_review_system"));
			advisors->Add(new SettingEntry("gui.no_depot_order_warn"));
			advisors->Add(new SettingEntry("gui.vehicle_income_warn"));
			advisors->Add(new SettingEntry("gui.lost_vehicle_warn"));
			advisors->Add(new SettingEntry("gui.old_vehicle_warn"));
			advisors->Add(new SettingEntry("gui.restriction_wait_vehicle_warn"));
			advisors->Add(new SettingEntry("gui.show_finances"));
			advisors->Add(new SettingEntry("news_display.economy"));
			advisors->Add(new SettingEntry("news_display.subsidies"));
			advisors->Add(new SettingEntry("news_display.open"));
			advisors->Add(new SettingEntry("news_display.close"));
			advisors->Add(new SettingEntry("news_display.production_player"));
			advisors->Add(new SettingEntry("news_display.production_other"));
			advisors->Add(new SettingEntry("news_display.production_nobody"));
		}

		SettingsPage *company = main->Add(new SettingsPage(STR_CONFIG_SETTING_COMPANY));
		{
			company->Add(new SettingEntry("gui.starting_colour"));
			company->Add(new SettingEntry("gui.starting_colour_secondary"));
			company->Add(new SettingEntry("company.engine_renew"));
			company->Add(new SettingEntry("company.engine_renew_months"));
			company->Add(new SettingEntry("company.engine_renew_money"));
			company->Add(new SettingEntry("vehicle.servint_ispercent"));
			company->Add(new SettingEntry("vehicle.servint_trains"));
			company->Add(new SettingEntry("vehicle.servint_roadveh"));
			company->Add(new SettingEntry("vehicle.servint_ships"));
			company->Add(new SettingEntry("vehicle.servint_aircraft"));
			company->Add(new SettingEntry("vehicle.auto_timetable_by_default"));
			company->Add(new SettingEntry("vehicle.auto_separation_by_default"));
			company->Add(new SettingEntry("auto_timetable_separation_rate"));
			company->Add(new SettingEntry("timetable_autofill_rounding"));
			company->Add(new SettingEntry("order_occupancy_smoothness"));
			company->Add(new SettingEntry("company.advance_order_on_clone"));
			company->Add(new SettingEntry("company.copy_clone_add_to_group"));
			company->Add(new SettingEntry("company.remain_if_next_order_same_station"));
			company->Add(new SettingEntry("company.default_sched_dispatch_duration"));
		}

		SettingsPage *accounting = main->Add(new SettingsPage(STR_CONFIG_SETTING_ACCOUNTING));
		{
			accounting->Add(new SettingEntry("difficulty.infinite_money"));
			accounting->Add(new SettingEntry("economy.inflation"));
			accounting->Add(new SettingEntry("economy.inflation_fixed_dates"));
			accounting->Add(new SettingEntry("difficulty.initial_interest"));
			accounting->Add(new SettingEntry("difficulty.max_loan"));
			accounting->Add(new SettingEntry("difficulty.subsidy_multiplier"));
			accounting->Add(new SettingEntry("difficulty.subsidy_duration"));
			accounting->Add(new SettingEntry("economy.feeder_payment_share"));
			accounting->Add(new SettingEntry("economy.infrastructure_maintenance"));
			accounting->Add(new SettingEntry("difficulty.vehicle_costs"));
			accounting->Add(new SettingEntry("difficulty.vehicle_costs_in_depot"));
			accounting->Add(new SettingEntry("difficulty.vehicle_costs_when_stopped"));
			accounting->Add(new SettingEntry("difficulty.construction_cost"));
			accounting->Add(new SettingEntry("economy.payment_algorithm"));
		}

		SettingsPage *vehicles = main->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES));
		{
			SettingsPage *physics = vehicles->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES_PHYSICS));
			{
				physics->Add(new SettingEntry("vehicle.train_acceleration_model"));
				physics->Add(new SettingEntry("vehicle.train_braking_model"));
				physics->Add(new ConditionallyHiddenSettingEntry("vehicle.realistic_braking_aspect_limited", []() -> bool { return GetGameSettings().vehicle.train_braking_model != TBM_REALISTIC; }));
				physics->Add(new ConditionallyHiddenSettingEntry("vehicle.limit_train_acceleration", []() -> bool { return GetGameSettings().vehicle.train_braking_model != TBM_REALISTIC; }));
				physics->Add(new ConditionallyHiddenSettingEntry("vehicle.train_acc_braking_percent", []() -> bool { return GetGameSettings().vehicle.train_braking_model != TBM_REALISTIC; }));
				physics->Add(new ConditionallyHiddenSettingEntry("vehicle.track_edit_ignores_realistic_braking", []() -> bool { return GetGameSettings().vehicle.train_braking_model != TBM_REALISTIC; }));
				physics->Add(new SettingEntry("vehicle.train_slope_steepness"));
				physics->Add(new SettingEntry("vehicle.wagon_speed_limits"));
				physics->Add(new SettingEntry("vehicle.train_speed_adaptation"));
				physics->Add(new SettingEntry("vehicle.freight_trains"));
				physics->Add(new SettingEntry("vehicle.roadveh_acceleration_model"));
				physics->Add(new SettingEntry("vehicle.roadveh_slope_steepness"));
				physics->Add(new SettingEntry("vehicle.smoke_amount"));
				physics->Add(new SettingEntry("vehicle.plane_speed"));
				physics->Add(new SettingEntry("vehicle.ship_collision_avoidance"));
				physics->Add(new SettingEntry("vehicle.roadveh_articulated_overtaking"));
				physics->Add(new SettingEntry("vehicle.roadveh_cant_quantum_tunnel"));
				physics->Add(new SettingEntry("vehicle.slow_road_vehicles_in_curves"));
			}

			SettingsPage *routing = vehicles->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES_ROUTING));
			{
				routing->Add(new SettingEntry("vehicle.road_side"));
				routing->Add(new SettingEntry("difficulty.line_reverse_mode"));
				routing->Add(new SettingEntry("pf.reverse_at_signals"));
				routing->Add(new SettingEntry("pf.back_of_one_way_pbs_waiting_point"));
				routing->Add(new SettingEntry("pf.forbid_90_deg"));
				routing->Add(new SettingEntry("pf.reroute_rv_on_layout_change"));
				routing->Add(new SettingEntry("vehicle.drive_through_train_depot"));
			}

			SettingsPage *orders = vehicles->Add(new SettingsPage(STR_CONFIG_SETTING_VEHICLES_ORDERS));
			{
				orders->Add(new SettingEntry("gui.new_nonstop"));
				orders->Add(new SettingEntry("gui.quick_goto"));
				orders->Add(new SettingEntry("gui.stop_location"));
				orders->Add(new SettingEntry("order.nonstop_only"));
			}

			vehicles->Add(new SettingEntry("vehicle.adjacent_crossings"));
			vehicles->Add(new SettingEntry("vehicle.safer_crossings"));
			vehicles->Add(new SettingEntry("vehicle.non_leading_engines_keep_name"));
		}

		SettingsPage *limitations = main->Add(new SettingsPage(STR_CONFIG_SETTING_LIMITATIONS));
		{
			limitations->Add(new SettingEntry("construction.command_pause_level"));
			limitations->Add(new SettingEntry("construction.autoslope"));
			limitations->Add(new SettingEntry("construction.extra_dynamite"));
			limitations->Add(new SettingEntry("construction.map_height_limit"));
			limitations->Add(new SettingEntry("construction.max_bridge_length"));
			limitations->Add(new SettingEntry("construction.max_bridge_height"));
			limitations->Add(new SettingEntry("construction.max_tunnel_length"));
			limitations->Add(new SettingEntry("construction.chunnel"));
			limitations->Add(new SettingEntry("station.never_expire_airports"));
			limitations->Add(new SettingEntry("vehicle.never_expire_vehicles"));
			limitations->Add(new SettingEntry("vehicle.no_expire_vehicles_after"));
			limitations->Add(new SettingEntry("vehicle.no_introduce_vehicles_after"));
			limitations->Add(new SettingEntry("vehicle.max_trains"));
			limitations->Add(new SettingEntry("vehicle.max_roadveh"));
			limitations->Add(new SettingEntry("vehicle.max_aircraft"));
			limitations->Add(new SettingEntry("vehicle.max_ships"));
			limitations->Add(new SettingEntry("vehicle.max_train_length"));
			limitations->Add(new SettingEntry("vehicle.through_load_speed_limit"));
			limitations->Add(new SettingEntry("vehicle.rail_depot_speed_limit"));
			limitations->Add(new SettingEntry("station.station_spread"));
			limitations->Add(new SettingEntry("station.distant_join_stations"));
			limitations->Add(new SettingEntry("station.modified_catchment"));
			limitations->Add(new SettingEntry("station.catchment_increase"));
			limitations->Add(new SettingEntry("construction.road_stop_on_town_road"));
			limitations->Add(new SettingEntry("construction.road_stop_on_competitor_road"));
			limitations->Add(new SettingEntry("construction.crossing_with_competitor"));
			limitations->Add(new SettingEntry("construction.convert_town_road_no_houses"));
			limitations->Add(new SettingEntry("vehicle.disable_elrails"));
			limitations->Add(new SettingEntry("order.station_length_loading_penalty"));
			limitations->Add(new SettingEntry("construction.maximum_signal_evaluations"));
			limitations->Add(new SettingEntry("construction.enable_build_river"));
			limitations->Add(new SettingEntry("construction.enable_remove_water"));
			limitations->Add(new SettingEntry("construction.allow_grf_objects_under_bridges"));
			limitations->Add(new SettingEntry("construction.allow_stations_under_bridges"));
			limitations->Add(new SettingEntry("construction.purchase_land_permitted"));
			limitations->Add(new SettingEntry("construction.build_object_area_permitted"));
			limitations->Add(new SettingEntry("construction.no_expire_objects_after"));
			limitations->Add(new SettingEntry("construction.ignore_object_intro_dates"));
		}

		SettingsPage *disasters = main->Add(new SettingsPage(STR_CONFIG_SETTING_ACCIDENTS));
		{
			disasters->Add(new SettingEntry("difficulty.disasters"));
			disasters->Add(new SettingEntry("difficulty.economy"));
			disasters->Add(new SettingEntry("vehicle.plane_crashes"));
			disasters->Add(new SettingEntry("vehicle.no_train_crash_other_company"));
			disasters->Add(new SettingEntry("vehicle.train_self_collision"));
			disasters->Add(new SettingEntry("difficulty.vehicle_breakdowns"));
			disasters->Add(new SettingEntry("difficulty.max_reliability_floor"));
			disasters->Add(new SettingEntry("difficulty.reliability_decay_speed"));
			disasters->Add(new SettingEntry("vehicle.improved_breakdowns"));
			disasters->Add(new SettingEntry("vehicle.pay_for_repair"));
			disasters->Add(new SettingEntry("vehicle.repair_cost"));
			disasters->Add(new SettingEntry("order.no_servicing_if_no_breakdowns"));
			disasters->Add(new SettingEntry("order.serviceathelipad"));
		}

		SettingsPage *genworld = main->Add(new SettingsPage(STR_CONFIG_SETTING_GENWORLD));
		{
			SettingsPage *rivers = genworld->Add(new SettingsPage(STR_CONFIG_SETTING_GENWORLD_RIVERS_LAKES));
			{
				rivers->Add(new SettingEntry("game_creation.amount_of_rivers"));
				rivers->Add(new SettingEntry("game_creation.min_river_length"));
				rivers->Add(new SettingEntry("game_creation.river_route_random"));
				rivers->Add(new SettingEntry("game_creation.rivers_top_of_hill"));
				rivers->Add(new SettingEntry("game_creation.river_tropics_width"));
				rivers->Add(new SettingEntry("game_creation.lake_tropics_width"));
				rivers->Add(new SettingEntry("game_creation.coast_tropics_width"));
				rivers->Add(new SettingEntry("game_creation.lake_size"));
				rivers->Add(new SettingEntry("game_creation.lakes_allowed_in_deserts"));
				rivers->Add(new SettingEntry("game_creation.wetlands_percentage"));
			}
			genworld->Add(new SettingEntry("game_creation.landscape"));
			genworld->Add(new SettingEntry("game_creation.land_generator"));
			genworld->Add(new SettingEntry("difficulty.terrain_type"));
			genworld->Add(new SettingEntry("game_creation.tgen_smoothness"));
			genworld->Add(new SettingEntry("game_creation.variety"));
			genworld->Add(new SettingEntry("game_creation.climate_threshold_mode"));
			auto coverage_hide = []() -> bool { return GetGameSettings().game_creation.climate_threshold_mode != 0; };
			auto snow_line_height_hide = []() -> bool { return GetGameSettings().game_creation.climate_threshold_mode != 1 && _game_mode == GM_MENU; };
			auto rainforest_line_height_hide = []() -> bool { return GetGameSettings().game_creation.climate_threshold_mode != 1; };
			genworld->Add(new ConditionallyHiddenSettingEntry("game_creation.snow_coverage", coverage_hide));
			genworld->Add(new ConditionallyHiddenSettingEntry("game_creation.snow_line_height", snow_line_height_hide));
			genworld->Add(new ConditionallyHiddenSettingEntry("game_creation.desert_coverage", coverage_hide));
			genworld->Add(new ConditionallyHiddenSettingEntry("game_creation.rainforest_line_height", rainforest_line_height_hide));
			genworld->Add(new SettingEntry("game_creation.amount_of_rocks"));
			genworld->Add(new SettingEntry("game_creation.height_affects_rocks"));
			genworld->Add(new SettingEntry("game_creation.build_public_roads"));
			genworld->Add(new SettingEntry("game_creation.better_town_placement"));
			auto better_town_placement_hide = []() -> bool { return !GetGameSettings().game_creation.better_town_placement; };
			genworld->Add(new ConditionallyHiddenSettingEntry("game_creation.better_town_placement_radius", better_town_placement_hide));
		}

		SettingsPage *environment = main->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT));
		{
			SettingsPage *time = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_TIME));
			{
				time->Add(new SettingEntry("economy.timekeeping_units"));
				time->Add(new SettingEntry("economy.minutes_per_calendar_year"));
				time->Add(new SettingEntry("game_creation.ending_year"));
				time->Add(new SettingEntry("gui.pause_on_newgame"));
				time->Add(new SettingEntry("gui.fast_forward_speed_limit"));
				time->Add(new SettingEntry("economy.day_length_factor"));
			}

			SettingsPage *authorities = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_AUTHORITIES));
			{
				authorities->Add(new SettingEntry("difficulty.town_council_tolerance"));
				authorities->Add(new SettingEntry("economy.bribe"));
				authorities->Add(new SettingEntry("economy.exclusive_rights"));
				authorities->Add(new SettingEntry("economy.fund_roads"));
				authorities->Add(new SettingEntry("economy.fund_buildings"));
				authorities->Add(new SettingEntry("economy.station_noise_level"));
			}

			SettingsPage *towns = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_TOWNS));
			{
				SettingsPage *town_zone = towns->Add(new SettingsPage(STR_CONFIG_SETTING_TOWN_ZONES));
				{
					town_zone->hide_callback = []() -> bool {
						return !GetGameSettings().economy.town_zone_calc_mode;
					};
					town_zone->Add(new SettingEntry("economy.town_zone_0_mult"));
					town_zone->Add(new SettingEntry("economy.town_zone_1_mult"));
					town_zone->Add(new SettingEntry("economy.town_zone_2_mult"));
					town_zone->Add(new SettingEntry("economy.town_zone_3_mult"));
					town_zone->Add(new SettingEntry("economy.town_zone_4_mult"));
					town_zone->Add(new SettingEntry("economy.city_zone_0_mult"));
					town_zone->Add(new SettingEntry("economy.city_zone_1_mult"));
					town_zone->Add(new SettingEntry("economy.city_zone_2_mult"));
					town_zone->Add(new SettingEntry("economy.city_zone_3_mult"));
					town_zone->Add(new SettingEntry("economy.city_zone_4_mult"));
				}
				towns->Add(new SettingEntry("economy.town_cargo_scale"));
				towns->Add(new SettingEntry("economy.town_cargo_scale_mode"));
				towns->Add(new SettingEntry("economy.town_growth_rate"));
				towns->Add(new SettingEntry("economy.town_growth_cargo_transported"));
				towns->Add(new SettingEntry("economy.default_allow_town_growth"));
				towns->Add(new SettingEntry("economy.town_zone_calc_mode"));
				towns->Add(new SettingEntry("economy.allow_town_roads"));
				towns->Add(new SettingEntry("economy.allow_town_road_branch_non_build"));
				towns->Add(new SettingEntry("economy.allow_town_level_crossings"));
				towns->Add(new SettingEntry("economy.allow_town_bridges"));
				towns->Add(new SettingEntry("economy.town_build_tunnels"));
				towns->Add(new SettingEntry("economy.town_max_road_slope"));
				towns->Add(new SettingEntry("economy.found_town"));
				towns->Add(new SettingEntry("economy.place_houses"));
				towns->Add(new SettingEntry("economy.town_layout"));
				towns->Add(new SettingEntry("economy.larger_towns"));
				towns->Add(new SettingEntry("economy.initial_city_size"));
				towns->Add(new SettingEntry("economy.town_min_distance"));
				towns->Add(new SettingEntry("economy.max_town_heightlevel"));
				towns->Add(new SettingEntry("economy.min_town_land_area"));
				towns->Add(new SettingEntry("economy.min_city_land_area"));
				towns->Add(new SettingEntry("economy.town_cargogen_mode"));
				towns->Add(new SettingEntry("economy.random_road_reconstruction"));
			}

			SettingsPage *industries = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_INDUSTRIES));
			{
				industries->Add(new SettingEntry("economy.industry_cargo_scale"));
				industries->Add(new SettingEntry("economy.industry_cargo_scale_mode"));
				industries->Add(new SettingEntry("difficulty.industry_density"));
				industries->Add(new SettingEntry("construction.raw_industry_construction"));
				industries->Add(new SettingEntry("construction.industry_platform"));
				industries->Add(new SettingEntry("economy.multiple_industry_per_town"));
				industries->Add(new SettingEntry("game_creation.oil_refinery_limit"));
				industries->Add(new SettingEntry("economy.type"));
				industries->Add(new SettingEntry("station.serve_neutral_industries"));
				industries->Add(new SettingEntry("station.station_delivery_mode"));
				industries->Add(new SettingEntry("economy.spawn_primary_industry_only"));
				industries->Add(new SettingEntry("economy.industry_event_rate"));
			}

			SettingsPage *cdist = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_CARGODIST));
			{
				cdist->Add(new SettingEntry("linkgraph.recalc_time"));
				cdist->Add(new SettingEntry("linkgraph.recalc_interval"));
				cdist->Add(new SettingEntry("linkgraph.distribution_pax"));
				cdist->Add(new SettingEntry("linkgraph.distribution_mail"));
				cdist->Add(new SettingEntry("linkgraph.distribution_armoured"));
				cdist->Add(new SettingEntry("linkgraph.distribution_default"));
				SettingsPage *cdist_override = cdist->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_CARGODIST_PER_CARGO_OVERRIDE));
				{
					const SettingTable &linkgraph_table = GetLinkGraphSettingTable();
					uint base_index = GetSettingIndexByFullName(linkgraph_table, "linkgraph.distribution_per_cargo[0]");
					assert(base_index != UINT32_MAX);
					for (CargoType c = 0; c < NUM_CARGO; c++) {
						cdist_override->Add(new CargoDestPerCargoSettingEntry(c, GetSettingDescription(linkgraph_table, base_index + c)->AsIntSetting()));
					}
				}
				cdist->Add(new SettingEntry("linkgraph.accuracy"));
				cdist->Add(new SettingEntry("linkgraph.demand_distance"));
				cdist->Add(new SettingEntry("linkgraph.demand_size"));
				cdist->Add(new SettingEntry("linkgraph.short_path_saturation"));
				cdist->Add(new SettingEntry("linkgraph.aircraft_link_scale"));
			}

			SettingsPage *trees = environment->Add(new SettingsPage(STR_CONFIG_SETTING_ENVIRONMENT_TREES));
			{
				trees->Add(new SettingEntry("game_creation.tree_placer"));
				trees->Add(new SettingEntry("construction.extra_tree_placement"));
				trees->Add(new SettingEntry("construction.trees_around_snow_line_enabled"));
				trees->Add(new SettingEntry("construction.trees_around_snow_line_range"));
				trees->Add(new SettingEntry("construction.trees_around_snow_line_dynamic_range"));
				trees->Add(new SettingEntry("construction.tree_growth_rate"));
			}

			environment->Add(new SettingEntry("construction.flood_from_edges"));
			environment->Add(new SettingEntry("construction.map_edge_mode"));
			environment->Add(new SettingEntry("station.cargo_class_rating_wait_time"));
			environment->Add(new SettingEntry("station.station_size_rating_cargo_amount"));
			environment->Add(new SettingEntry("station.truncate_cargo"));
			environment->Add(new SettingEntry("construction.purchased_land_clear_ground"));
		}

		SettingsPage *ai = main->Add(new SettingsPage(STR_CONFIG_SETTING_AI));
		{
			SettingsPage *npc = ai->Add(new SettingsPage(STR_CONFIG_SETTING_AI_NPC));
			{
				npc->Add(new SettingEntry("script.script_max_opcode_till_suspend"));
				npc->Add(new SettingEntry("script.script_max_memory_megabytes"));
				npc->Add(new SettingEntry("difficulty.competitor_speed"));
				npc->Add(new SettingEntry("ai.ai_in_multiplayer"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_train"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_roadveh"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_aircraft"));
				npc->Add(new SettingEntry("ai.ai_disable_veh_ship"));
			}

			SettingsPage *sharing = ai->Add(new SettingsPage(STR_CONFIG_SETTING_SHARING));
			{
				sharing->Add(new SettingEntry("economy.infrastructure_sharing[0]"));
				sharing->Add(new SettingEntry("economy.infrastructure_sharing[1]"));
				sharing->Add(new SettingEntry("economy.infrastructure_sharing[2]"));
				sharing->Add(new SettingEntry("economy.infrastructure_sharing[3]"));
				sharing->Add(new SettingEntry("economy.sharing_fee[0]"));
				sharing->Add(new SettingEntry("economy.sharing_fee[1]"));
				sharing->Add(new SettingEntry("economy.sharing_fee[2]"));
				sharing->Add(new SettingEntry("economy.sharing_fee[3]"));
				sharing->Add(new SettingEntry("economy.sharing_payment_in_debt"));
			}

			ai->Add(new SettingEntry("economy.give_money"));
			ai->Add(new SettingEntry("economy.allow_shares"));
			ai->Add(new ConditionallyHiddenSettingEntry("economy.min_years_for_shares", []() -> bool { return !GetGameSettings().economy.allow_shares; }));
			ai->Add(new SettingEntry("difficulty.money_cheat_in_multiplayer"));
			ai->Add(new SettingEntry("difficulty.rename_towns_in_multiplayer"));
			ai->Add(new SettingEntry("difficulty.override_town_settings_in_multiplayer"));
		}

		SettingsPage *network = main->Add(new SettingsPage(STR_CONFIG_SETTING_NETWORK));
		{
			network->Add(new SettingEntry("network.use_relay_service"));
		}

		main->Init();
	}
	return *main;
}
