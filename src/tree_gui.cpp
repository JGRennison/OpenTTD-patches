/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tree_gui.cpp GUIs for building trees. */

#include "stdafx.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "tilehighlight_func.h"
#include "company_func.h"
#include "company_base.h"
#include "command_func.h"
#include "sound_func.h"
#include "tree_map.h"

#include "widgets/tree_widget.h"

#include "table/sprites.h"
#include "table/strings.h"
#include "table/tree_land.h"

#include "safeguards.h"

void PlaceTreesRandomly();
void RemoveAllTrees();

/**
 * The build trees window.
 */
class BuildTreesWindow : public Window
{
	uint16 base;        ///< Base tree number used for drawing the window.
	uint16 count;       ///< Number of different trees available.
	TreeType tree_to_plant; ///< Tree number to plant, \c TREE_INVALID for a random tree.

public:
	BuildTreesWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		ResetObjectToPlace();
	}

	/**
	 * Calculate the maximum size of all tree sprites
	 * @return Dimension of the largest tree sprite
	 */
	Dimension GetMaxTreeSpriteSize()
	{
		Dimension size, this_size;
		Point offset;
		/* Avoid to use it uninitialized */
		size.width  = 32; // default width - 2
		size.height = 39; // default height - 7
		offset.x = 0;
		offset.y = 0;

		for (int i = this->base; i < this->base + this->count; i++) {
			if (i >= (int)lengthof(_tree_sprites)) return size;
			this_size = GetSpriteSize(_tree_sprites[i].sprite, &offset);
			size.width = max<int>(size.width, 2 * max<int>(this_size.width, -offset.x));
			size.height = max<int>(size.height, max<int>(this_size.height, -offset.y));
		}

		return size;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget >= WID_BT_TYPE_11 && widget <= WID_BT_TYPE_34) {
			Dimension d = GetMaxTreeSpriteSize();
			/* Allow some pixels extra width and height */
			size->width = d.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
			size->height = d.height + WD_FRAMERECT_RIGHT + WD_FRAMERECT_BOTTOM + 7; // we need some more space
			return;
		}

		if (widget != WID_BT_MANY_RANDOM && widget != WID_BT_REMOVE_ALL) return;

		if (_game_mode != GM_EDITOR) {
			size->width = 0;
			size->height = 0;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget < WID_BT_TYPE_11 || widget > WID_BT_TYPE_34 || widget - WID_BT_TYPE_11 >= this->count) return;

		int i = this->base + widget - WID_BT_TYPE_11;
		/* Trees "grow" in the centre on the bottom line of the buttons */
		DrawSprite(_tree_sprites[i].sprite, _tree_sprites[i].pal, (r.left + r.right) / 2 + WD_FRAMERECT_LEFT, r.bottom - 7);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_BT_TYPE_11: case WID_BT_TYPE_12: case WID_BT_TYPE_13: case WID_BT_TYPE_14:
			case WID_BT_TYPE_21: case WID_BT_TYPE_22: case WID_BT_TYPE_23: case WID_BT_TYPE_24:
			case WID_BT_TYPE_31: case WID_BT_TYPE_32: case WID_BT_TYPE_33: case WID_BT_TYPE_34:
				if (widget - WID_BT_TYPE_11 >= this->count) break;

				if (HandlePlacePushButton(this, widget, SPR_CURSOR_TREE, HT_RECT)) {
					this->tree_to_plant = (TreeType)(this->base + widget - WID_BT_TYPE_11);
				}
				break;

			case WID_BT_TYPE_RANDOM: // tree of random type.
				if (HandlePlacePushButton(this, WID_BT_TYPE_RANDOM, SPR_CURSOR_TREE, HT_RECT)) {
					this->tree_to_plant = TREE_INVALID;
				}
				break;

			case WID_BT_MANY_RANDOM: // place trees randomly over the landscape
				if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
				PlaceTreesRandomly();
				MarkWholeScreenDirty();
				break;

			case WID_BT_REMOVE_ALL: // remove all trees over the landscape
				if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
				RemoveAllTrees();
				MarkWholeScreenDirty();
				break;

		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_PLANT_TREES);
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1 && select_proc == DDSP_PLANT_TREES) {
			DoCommandP(end_tile, this->tree_to_plant, start_tile,
				CMD_PLANT_TREE | CMD_MSG(STR_ERROR_CAN_T_PLANT_TREE_HERE));
		}
	}

	/**
	 * Initialize the window data
	 */
	virtual void OnInit()
	{
		this->base  = _tree_base_by_landscape[_settings_game.game_creation.landscape];
		this->count = _tree_count_by_landscape[_settings_game.game_creation.landscape];
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
	}
};

static const NWidgetPart _nested_build_trees_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_PLANT_TREE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_11), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_12), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_13), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_14), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_21), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_22), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_23), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_24), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_31), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_32), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_33), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
					NWidget(NWID_SPACER), SetMinimalSize(1, 0),
					NWidget(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_34), SetMinimalSize(34, 46), SetDataTip(0x0, STR_PLANT_TREE_TOOLTIP),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BT_TYPE_RANDOM), SetMinimalSize(139, 12), SetDataTip(STR_TREES_RANDOM_TYPE, STR_TREES_RANDOM_TYPE_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BT_MANY_RANDOM), SetMinimalSize(139, 12), SetDataTip(STR_TREES_RANDOM_TREES_BUTTON, STR_TREES_RANDOM_TREES_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BT_REMOVE_ALL), SetMinimalSize(139, 12), SetDataTip(STR_TREES_REMOVE_TREES_BUTTON, STR_TREES_REMOVE_TREES_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _build_trees_desc(
	WDP_AUTO, "build_tree", 0, 0,
	WC_BUILD_TREES, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_trees_widgets, lengthof(_nested_build_trees_widgets)
);

void ShowBuildTreesToolbar()
{
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<BuildTreesWindow>(&_build_trees_desc, 0);
}
