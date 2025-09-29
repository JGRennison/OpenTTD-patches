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
#include "core/random_func.hpp"
#include "sound_func.h"
#include "strings_func.h"
#include "zoom_func.h"
#include "tree_map.h"
#include "viewport_func.h"
#include "tree_cmd.h"
#include "tree_func.h"
#include "error.h"

#include "widgets/tree_widget.h"

#include "table/sprites.h"
#include "table/strings.h"
#include "table/tree_land.h"

#include "safeguards.h"

/**
 * Calculate the maximum size of all tree sprites
 * @return Dimension of the largest tree sprite
 */
static Dimension GetMaxTreeSpriteSize()
{
	const uint16_t base = _tree_base_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
	const uint16_t count = _tree_count_by_landscape[to_underlying(_settings_game.game_creation.landscape)];

	Dimension size, this_size;
	Point offset;
	/* Avoid to use it uninitialized */
	size.width = ScaleGUITrad(32); // default width - WD_FRAMERECT_LEFT
	size.height = ScaleGUITrad(39); // default height - BUTTON_BOTTOM_OFFSET
	offset.x = 0;
	offset.y = 0;

	for (int i = base; i < base + count; i++) {
		if (i >= (int)lengthof(_tree_sprites)) return size;
		this_size = GetSpriteSize(_tree_sprites[i].sprite, &offset);
		size.width = std::max<int>(size.width, 2 * std::max<int>(this_size.width, -offset.x));
		size.height = std::max<int>(size.height, std::max<int>(this_size.height, -offset.y));
	}

	return size;
}


/**
 * The build trees window.
 */
class BuildTreesWindow : public Window
{
	/** Visual Y offset of tree root from the bottom of the tree type buttons */
	static const int BUTTON_BOTTOM_OFFSET = 7;

	enum PlantingMode : uint8_t {
		PM_NORMAL,
		PM_FOREST_SM,
		PM_FOREST_LG,
	};

	TreeTypes trees_to_plant = {}; /// < Container with every TreeType selected by the user.
	PlantingMode mode = PM_NORMAL; ///< Current mode for planting
	TileIndex last_tile = INVALID_TILE;

	/**
	 * Update the GUI and enable/disable planting to reflect selected options.
	 */
	void UpdateMode()
	{
		if (this->trees_to_plant.Any()) {
			/* Activate placement */
			if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
			TreeTypes trees_archive = this->trees_to_plant;
			SetObjectToPlace(SPR_CURSOR_TREE, PAL_NONE, HT_RECT | HT_DIAGONAL, this->window_class, this->window_number);
			this->trees_to_plant = trees_archive; // This is to handle the case where SetObjectToPlace was previously active and is reset (in OnPlaceObjectAbort).
		} else {
			/* Deactivate placement */
			ResetObjectToPlace();
		}

		const uint8_t tree_types_base = _tree_base_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
		const uint8_t tree_types_count = _tree_count_by_landscape[to_underlying(_settings_game.game_creation.landscape)];

		if (CountBits(this->trees_to_plant) == tree_types_count) {
			this->LowerWidget(WID_BT_TYPE_RANDOM);
		} else {
			this->RaiseWidget(WID_BT_TYPE_RANDOM);
		}

		for (uint8_t i = 0; i < tree_types_count; i++) {
			if (this->trees_to_plant.Test(static_cast<TreeType>(i + tree_types_base))) {
				this->LowerWidget(WID_BT_TYPE_BUTTON_FIRST + i + tree_types_base);
			} else {
				this->RaiseWidget(WID_BT_TYPE_BUTTON_FIRST + i + tree_types_base);
			}
		}

		this->RaiseWidget(WID_BT_MODE_NORMAL);
		this->RaiseWidget(WID_BT_MODE_FOREST_SM);
		this->RaiseWidget(WID_BT_MODE_FOREST_LG);
		switch (this->mode) {
			case PM_NORMAL: this->LowerWidget(WID_BT_MODE_NORMAL); break;
			case PM_FOREST_SM: this->LowerWidget(WID_BT_MODE_FOREST_SM); break;
			case PM_FOREST_LG: this->LowerWidget(WID_BT_MODE_FOREST_LG); break;
			default: NOT_REACHED();
		}

		this->SetDirty();
	}

	void DoPlantForest(TileIndex tile)
	{
		if (tile == this->last_tile) {
			return;
		}

		uint radius = 0;
		uint count = 0;
		switch (this->mode) {
			case PM_NORMAL:
				radius = 0;
				count = 1;
				break;
			case PM_FOREST_SM:
				radius = 5;
				count = 5;
				break;
			case PM_FOREST_LG:
				radius = 12;
				count = 12;
				break;
			default: NOT_REACHED();
		}

		PlaceTreeGroupAroundTile(tile, this->trees_to_plant, radius, count);

		this->last_tile = tile;
	}

	void ResetToolData()
	{
		this->last_tile = INVALID_TILE;
		for (const auto &it : _tree_placer_memory) {
			MarkTileDirtyByTile(it.first, VMDF_NOT_MAP_MODE);
		}
		_tree_placer_memory.clear();
		_tree_placer_preview_active = false;
	}

public:
	BuildTreesWindow(WindowDesc &desc, WindowNumber window_number) : Window(desc)
	{
		this->CreateNestedTree();
		ResetObjectToPlace();

		this->LowerWidget(WID_BT_MODE_NORMAL);
		/* Show scenario editor tools in editor */
		if (_game_mode != GM_EDITOR) {
			this->GetWidget<NWidgetStacked>(WID_BT_SE_PANE)->SetDisplayedPlane(SZSP_HORIZONTAL);
		}
		this->FinishInitNested(window_number);
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		this->Window::Close();
		_tree_placer_memory.rehash(0); // shrink_to_fit
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		if (widget >= WID_BT_TYPE_BUTTON_FIRST) {
			/* Ensure tree type buttons are sized after the largest tree type */
			Dimension d = GetMaxTreeSpriteSize();
			size.width = d.width + padding.width;
			size.height = d.height + padding.height + ScaleGUITrad(BUTTON_BOTTOM_OFFSET); // we need some more space
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget >= WID_BT_TYPE_BUTTON_FIRST) {
			const int index = widget - WID_BT_TYPE_BUTTON_FIRST;
			/* Trees "grow" in the centre on the bottom line of the buttons */
			DrawSprite(_tree_sprites[index].sprite, _tree_sprites[index].pal, CentreBounds(r.left, r.right, 0), r.bottom - ScaleGUITrad(BUTTON_BOTTOM_OFFSET));
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BT_TYPE_RANDOM: { // tree of random type.
				const uint8_t tree_types_base = _tree_base_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
				const uint8_t tree_types_count = _tree_count_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
				if (CountBits(this->trees_to_plant) == tree_types_count) {
					this->trees_to_plant = {};
				} else {
					for (uint8_t i = 0; i < tree_types_count; i++) {
						this->trees_to_plant.Set(static_cast<TreeType>(i + tree_types_base));
					}
				}
				this->UpdateMode();
				break;
			}

			case WID_BT_MANY_RANDOM: // place trees randomly over the landscape
				if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
				PlaceTreesRandomly();
				MarkWholeNonMapViewportsDirty();
				break;

			case WID_BT_REMOVE_ALL: // remove all trees over the landscape
				if (_settings_client.sound.confirm) SndPlayFx(SND_15_BEEP);
				RemoveAllTrees();
				MarkWholeNonMapViewportsDirty();
				break;

			case WID_BT_MODE_NORMAL:
				this->mode = PM_NORMAL;
				this->UpdateMode();
				break;

			case WID_BT_MODE_FOREST_SM:
				this->mode = PM_FOREST_SM;
				this->UpdateMode();
				break;

			case WID_BT_MODE_FOREST_LG:
				this->mode = PM_FOREST_LG;
				this->UpdateMode();
				break;

			default:
				if (widget >= WID_BT_TYPE_BUTTON_FIRST) {
					const int index = widget - WID_BT_TYPE_BUTTON_FIRST;
					NWidgetCore *nwid = this->GetWidget<NWidgetCore>(widget);
					const bool was_lowered = nwid->IsLowered();
					nwid->SetLowered(!was_lowered);
					this->trees_to_plant.Set(static_cast<TreeType>(index), !was_lowered);

					this->UpdateMode();
				}
				break;
		}
	}

	void OnPlaceObject([[maybe_unused]] Point pt, TileIndex tile) override
	{
		if (_game_mode != GM_EDITOR && this->mode == PM_NORMAL) {
			VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_PLANT_TREES);
		} else {
			VpStartDragging(DDSP_PLANT_TREES);
		}
	}

	void OnPlaceDrag(ViewportPlaceMethod select_method, [[maybe_unused]] ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt) override
	{
		if (this->mode == PM_NORMAL) {
			VpSelectTilesWithMethod(pt.x, pt.y, select_method);
			return;
		}

		TileIndex tile = TileVirtXY(pt.x, pt.y);

		if (_pause_mode.Any() && !IsCommandAllowedWhilePaused(CMD_BULK_TREE) && !_shift_pressed) {
			ShowErrorMessage(GetEncodedString(STR_ERROR_CAN_T_PLANT_TREE_HERE), GetEncodedString(STR_ERROR_NOT_ALLOWED_WHILE_PAUSED), WL_INFO, ::TileX(tile), ::TileY(tile));
			ResetObjectToPlace();
			return;
		}
		this->DoPlantForest(tile);
	}

	void OnPlaceMouseUp([[maybe_unused]] ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, [[maybe_unused]] Point pt, TileIndex start_tile, TileIndex end_tile) override
	{
		if (this->mode == PM_NORMAL) {
			if (pt.x != -1 && select_proc == DDSP_PLANT_TREES && this->trees_to_plant.Any()) {
				Command<CMD_PLANT_TREE>::Post(STR_ERROR_CAN_T_PLANT_TREE_HERE, end_tile, start_tile, this->trees_to_plant, 1, _ctrl_pressed);
			}
		} else {
			if (_game_mode != GM_EDITOR && pt.x != -1 && select_proc == DDSP_PLANT_TREES && this->trees_to_plant.Any()) {
				SendSyncTrees(this->last_tile);
			}

			this->ResetToolData();
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->ResetToolData();

		this->trees_to_plant = {};
		this->UpdateMode();
	}
};

/**
 * Make widgets for the current available tree types.
 * This does not use a NWID_MATRIX or WWT_MATRIX control as those are more difficult to
 * get producing the correct result than dynamically building the widgets is.
 * @see NWidgetFunctionType
 */
static std::unique_ptr<NWidgetBase> MakeTreeTypeButtons()
{
	const uint8_t type_base = _tree_base_by_landscape[to_underlying(_settings_game.game_creation.landscape)];
	const uint8_t type_count = _tree_count_by_landscape[to_underlying(_settings_game.game_creation.landscape)];

	/* Toyland has 9 tree types, which look better in 3x3 than 4x3 */
	const int num_columns = type_count == 9 ? 3 : 4;
	const int num_rows = CeilDiv(type_count, num_columns);
	uint8_t cur_type = type_base;

	auto vstack = std::make_unique<NWidgetVertical>(NWidContainerFlag::EqualSize);
	vstack->SetPIP(0, 1, 0);

	for (int row = 0; row < num_rows; row++) {
		auto hstack = std::make_unique<NWidgetHorizontal>(NWidContainerFlag::EqualSize);
		hstack->SetPIP(0, 1, 0);
		for (int col = 0; col < num_columns; col++) {
			if (cur_type > type_base + type_count) break;
			auto button = std::make_unique<NWidgetBackground>(WWT_PANEL, COLOUR_GREY, WID_BT_TYPE_BUTTON_FIRST + cur_type);
			button->SetToolTip(STR_PLANT_TREE_TOOLTIP);
			hstack->Add(std::move(button));
			cur_type++;
		}
		vstack->Add(std::move(hstack));
	}

	return vstack;
}

static constexpr NWidgetPart _nested_build_trees_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetStringTip(STR_PLANT_TREE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_VERTICAL), SetPIP(0, 1, 0), SetPadding(2),
			NWidgetFunction(MakeTreeTypeButtons),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BT_TYPE_RANDOM), SetStringTip(STR_TREES_RANDOM_TYPE, STR_TREES_RANDOM_TYPE_TOOLTIP),
			NWidget(NWID_HORIZONTAL, NWidContainerFlag::EqualSize),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BT_MODE_NORMAL), SetFill(1, 0), SetStringTip(STR_TREES_MODE_NORMAL_BUTTON, STR_SCENEDIT_TOOLBAR_PLANT_TREES_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BT_MODE_FOREST_SM), SetFill(1, 0), SetStringTip(STR_TREES_MODE_FOREST_SM_BUTTON, STR_TREES_MODE_FOREST_SM_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BT_MODE_FOREST_LG), SetFill(1, 0), SetStringTip(STR_TREES_MODE_FOREST_LG_BUTTON, STR_TREES_MODE_FOREST_LG_TOOLTIP),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BT_SE_PANE),
				NWidget(NWID_VERTICAL), SetPIP(0, 1, 0),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BT_MANY_RANDOM), SetStringTip(STR_TREES_RANDOM_TREES_BUTTON, STR_TREES_RANDOM_TREES_TOOLTIP),
					NWidget(NWID_SPACER), SetMinimalSize(0, 1),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BT_REMOVE_ALL), SetStringTip(STR_TREES_REMOVE_TREES_BUTTON, STR_TREES_REMOVE_TREES_TOOLTIP),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _build_trees_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_tree", 0, 0,
	WC_BUILD_TREES, WC_NONE,
	WindowDefaultFlag::Construction,
	_nested_build_trees_widgets
);

void ShowBuildTreesToolbar()
{
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<BuildTreesWindow>(_build_trees_desc, 0);
}
