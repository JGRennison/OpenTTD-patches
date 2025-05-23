/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dropdown_common_type.h Common drop down list components. */

#ifndef DROPDOWN_COMMON_TYPE_H
#define DROPDOWN_COMMON_TYPE_H

#include "gfx_func.h"
#include "gfx_type.h"
#include "palette_func.h"
#include "string_func.h"
#include "strings_func.h"
#include "table/strings.h"
#include "window_gui.h"

/**
 * Drop down divider component.
 * @tparam TBase Base component.
 * @tparam TFs Font size -- used to determine height.
 */
template <class TBase, FontSize TFs = FS_NORMAL>
class DropDownDivider : public TBase {
public:
	template <typename... Args>
	explicit DropDownDivider(Args&&... args) : TBase(std::forward<Args>(args)...) {}

	bool Selectable() const override { return false; }
	uint Height() const override { return std::max<uint>(GetCharacterHeight(TFs), this->TBase::Height()); }

	void Draw(const Rect &full, const Rect &, bool, Colours bg_colour) const override
	{
		uint8_t c1 = GetColourGradient(bg_colour, SHADE_DARK);
		uint8_t c2 = GetColourGradient(bg_colour, SHADE_LIGHTEST);

		int mid = CenterBounds(full.top, full.bottom, 0);
		GfxFillRect(full.left, mid - WidgetDimensions::scaled.bevel.bottom, full.right, mid - 1, c1);
		GfxFillRect(full.left, mid, full.right, mid + WidgetDimensions::scaled.bevel.top - 1, c2);
	}
};

/**
 * Drop down string component.
 * @tparam TBase Base component.
 * @tparam TFs Font size.
 * @tparam TEnd Position string at end if true, or start if false.
 */
template <class TBase, FontSize TFs = FS_NORMAL, bool TEnd = false>
class DropDownString : public TBase {
	std::string string; ///< String to be drawn.
	Dimension dim; ///< Dimensions of string.
public:
	template <typename... Args>
	explicit DropDownString(StringID string, Args&&... args) : TBase(std::forward<Args>(args)...)
	{
		this->SetString(GetString(string));
	}

	template <typename... Args>
	explicit DropDownString(const std::string &string, Args&&... args) : TBase(std::forward<Args>(args)...)
	{
		SetDParamStr(0, string);
		this->SetString(GetString(STR_JUST_RAW_STRING));
	}

	void SetString(std::string &&string)
	{
		this->string = std::move(string);
		this->dim = GetStringBoundingBox(this->string, TFs);
	}

	uint Height() const override
	{
		return std::max<uint>(this->dim.height, this->TBase::Height());
	}

	uint Width() const override { return this->dim.width + this->TBase::Width(); }

	void Draw(const Rect &full, const Rect &r, bool sel, Colours bg_colour) const override
	{
		bool rtl = TEnd ^ (_current_text_dir == TD_RTL);
		DrawStringMultiLine(r.WithWidth(this->dim.width, rtl), this->string, this->GetColour(sel), SA_CENTER, false, TFs);
		this->TBase::Draw(full, r.Indent(this->dim.width, rtl), sel, bg_colour);
	}

	void SetColourFlags(TextColour colour_flags) { this->colour_flags = colour_flags; }

	/**
	 * Natural sorting comparator function for DropDownList::sort().
	 * @param first Left side of comparison.
	 * @param second Right side of comparison.
	 * @return true if \a first precedes \a second.
	 * @warning All items in the list need to be derivates of DropDownListStringItem.
	 */
	static bool NatSortFunc(std::unique_ptr<const DropDownListItem> const &first, std::unique_ptr<const DropDownListItem> const &second)
	{
		const std::string &str1 = static_cast<const DropDownString*>(first.get())->string;
		const std::string &str2 = static_cast<const DropDownString*>(second.get())->string;
		return StrNaturalCompare(str1, str2) < 0;
	}
};

/**
 * Drop down icon component.
 * @tparam TBase Base component.
 * @tparam TEnd Position icon at end if true, or start if false.
 */
template <class TBase, bool TEnd = false>
class DropDownIcon : public TBase {
	SpriteID sprite; ///< Sprite ID to be drawn.
	PaletteID palette; ///< Palette ID to use.
	Dimension dsprite; ///< Bounding box dimensions of sprite.
	Dimension dbounds; ///< Bounding box dimensions of bounds.
public:
	template <typename... Args>
	explicit DropDownIcon(SpriteID sprite, PaletteID palette, Args&&... args) : TBase(std::forward<Args>(args)...), sprite(sprite), palette(palette)
	{
		this->dsprite = GetSpriteSize(this->sprite);
		this->dbounds = this->dsprite;
	}

	template <typename... Args>
	explicit DropDownIcon(const Dimension &dim, SpriteID sprite, PaletteID palette, Args&&... args) : TBase(std::forward<Args>(args)...), sprite(sprite), palette(palette), dbounds(dim)
	{
		this->dsprite = GetSpriteSize(this->sprite);
	}

	uint Height() const override { return std::max(this->dbounds.height, this->TBase::Height()); }
	uint Width() const override { return this->dbounds.width + WidgetDimensions::scaled.hsep_normal + this->TBase::Width(); }

	void Draw(const Rect &full, const Rect &r, bool sel, Colours bg_colour) const override
	{
		bool rtl = TEnd ^ (_current_text_dir == TD_RTL);
		Rect ir = r.WithWidth(this->dbounds.width, rtl);
		DrawSprite(this->sprite, this->palette, CenterBounds(ir.left, ir.right, this->dsprite.width), CenterBounds(r.top, r.bottom, this->dsprite.height));
		this->TBase::Draw(full, r.Indent(this->dbounds.width + WidgetDimensions::scaled.hsep_normal, rtl), sel, bg_colour);
	}
};

/**
 * Drop down checkmark component.
 * @tparam TBase Base component.
 * @tparam TFs Font size.
 * @tparam TEnd Position checkmark at end if true, or start if false.
 */
template <class TBase, bool TEnd = false, FontSize TFs = FS_NORMAL>
class DropDownCheck : public TBase {
	bool checked; ///< Is item checked.
	Dimension dim; ///< Dimension of checkmark.
public:
	template <typename... Args>
	explicit DropDownCheck(bool checked, Args&&... args) : TBase(std::forward<Args>(args)...), checked(checked)
	{
		this->dim = GetStringBoundingBox(STR_JUST_CHECKMARK, TFs);
	}

	uint Height() const override { return std::max<uint>(this->dim.height, this->TBase::Height()); }
	uint Width() const override { return this->dim.width + WidgetDimensions::scaled.hsep_wide + this->TBase::Width(); }

	void Draw(const Rect &full, const Rect &r, bool sel, Colours bg_colour) const override
	{
		bool rtl = TEnd ^ (_current_text_dir == TD_RTL);
		if (this->checked) {
			DrawStringMultiLine(r.WithWidth(this->dim.width, rtl), STR_JUST_CHECKMARK, this->GetColour(sel), SA_CENTER, false, TFs);
		}
		this->TBase::Draw(full, r.Indent(this->dim.width + WidgetDimensions::scaled.hsep_wide, rtl), sel, bg_colour);
	}
};

/**
 * Drop down indent component.
 * @tparam TBase Base component.
 * @tparam TEnd Position indent at end if true, or start if false.
 */
template <class TBase, bool TEnd = false>
class DropDownIndent : public TBase {
	uint indent; ///< Indent level.
public:
	template <typename... Args>
	explicit DropDownIndent(uint indent, Args&&... args) : TBase(std::forward<Args>(args)...), indent(indent) {}

	uint Width() const override { return this->indent * WidgetDimensions::scaled.hsep_indent + this->TBase::Width(); }

	void Draw(const Rect &full, const Rect &r, bool sel, Colours bg_colour) const override
	{
		bool rtl = TEnd ^ (_current_text_dir == TD_RTL);
		this->TBase::Draw(full, r.Indent(this->indent * WidgetDimensions::scaled.hsep_indent, rtl), sel, bg_colour);
	}
};

/* Commonly used drop down list items. */
using DropDownListDividerItem = DropDownDivider<DropDownListItem>;
using DropDownListStringItem = DropDownString<DropDownListItem>;
using DropDownListIconItem = DropDownIcon<DropDownString<DropDownListItem>>;
using DropDownListCheckedItem = DropDownIndent<DropDownCheck<DropDownString<DropDownListItem>>>;
using DropDownListIndentStringItem = DropDownIndent<DropDownString<DropDownListItem>>;

#endif /* DROPDOWN_COMMON_TYPE_H */
