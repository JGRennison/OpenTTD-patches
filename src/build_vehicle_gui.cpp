/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file build_vehicle_gui.cpp GUI for building vehicles. */

#include "stdafx.h"
#include "engine_base.h"
#include "engine_cmd.h"
#include "engine_func.h"
#include "station_base.h"
#include "network/network.h"
#include "articulated_vehicles.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "company_func.h"
#include "vehicle_gui.h"
#include "newgrf_badge.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "group.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "engine_gui.h"
#include "cargotype.h"
#include "core/geometry_func.hpp"
#include "autoreplace_func.h"
#include "train.h"
#include "error.h"
#include "zoom_func.h"
#include "querystring_gui.h"
#include "stringfilter_type.h"
#include "hotkeys.h"
#include "vehicle_cmd.h"
#include "tbtr_template_vehicle_cmd.h"

#include "widgets/build_vehicle_widget.h"

#include "table/strings.h"

#include <optional>

#include "safeguards.h"

/**
 * Get the height of a single 'entry' in the engine lists.
 * @param type the vehicle type to get the height of
 * @return the height for the entry
 */
uint GetEngineListHeight(VehicleType type)
{
	return std::max<uint>(GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.matrix.Vertical(), GetVehicleImageCellSize(type, EIT_PURCHASE).height);
}

/* Normal layout for roadvehicles, ships and airplanes. */
static constexpr NWidgetPart _nested_build_vehicle_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_BV_CAPTION), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_TOGGLE_DUAL_PANE_SEL),
			NWidget(WWT_IMGBTN, COLOUR_GREY, WID_BV_TOGGLE_DUAL_PANE), SetSpriteTip(SPR_LARGE_SMALL_WINDOW, STR_BUY_VEHICLE_TRAIN_TOGGLE_DUAL_PANE_TOOLTIP), SetAspect(WidgetDimensions::ASPECT_TOGGLE_SIZE),
		EndContainer(),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_VERTICAL),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SORT_ASCENDING_DESCENDING), SetStringTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_SORT_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
		EndContainer(),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDDEN_ENGINES),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_CARGO_FILTER_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
		EndContainer(),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BV_FILTER), SetResize(1, 0), SetFill(1, 0), SetPadding(2), SetStringTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
		EndContainer(),
	EndContainer(),
	/* Vehicle list. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_BV_LIST), SetResize(1, 1), SetFill(1, 0), SetMatrixDataTip(1, 0), SetScrollbar(WID_BV_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BV_SCROLLBAR),
	EndContainer(),
	/* Panel with details. */
	NWidget(WWT_PANEL, COLOUR_GREY, WID_BV_PANEL), SetMinimalSize(240, 122), SetResize(1, 0), EndContainer(),
	/* Build/rename buttons, resize button. */
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_BUILD_SEL),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_BUILD), SetResize(1, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDE), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_RENAME), SetResize(1, 0), SetFill(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/* Advanced layout for trains. */
static constexpr NWidgetPart _nested_build_vehicle_widgets_train_advanced[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_BV_CAPTION), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS), SetTextStyle(TC_WHITE),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_BV_TOGGLE_DUAL_PANE), SetSpriteTip(SPR_LARGE_SMALL_WINDOW, STR_BUY_VEHICLE_TRAIN_TOGGLE_DUAL_PANE_TOOLTIP), SetAspect(WidgetDimensions::ASPECT_TOGGLE_SIZE),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		/* First half of the window contains locomotives. */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0),
					NWidget(WWT_LABEL, INVALID_COLOUR, WID_BV_CAPTION_LOCO), SetStringTip(STR_JUST_STRING, STR_NULL), SetTextStyle(TC_WHITE), SetResize(1, 0), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SORT_ASCENDING_DESCENDING_LOCO), SetStringTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_SORT_DROPDOWN_LOCO), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDDEN_LOCOS),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_CARGO_FILTER_DROPDOWN_LOCO), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
				EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY),
					NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BV_FILTER_LOCO), SetResize(1, 0), SetFill(1, 0), SetPadding(2), SetStringTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
				EndContainer(),
			EndContainer(),
			/* Vehicle list for locomotives. */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_BV_LIST_LOCO), SetResize(1, 1), SetFill(1, 0), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_BV_SCROLLBAR_LOCO),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BV_SCROLLBAR_LOCO),
			EndContainer(),
			/* Panel with details for locomotives. */
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BV_PANEL_LOCO), SetMinimalSize(240, 122), SetResize(1, 0), EndContainer(),
			/* Build/rename buttons, resize button for locomotives. */
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_LOCO_BUTTONS_SEL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_BUILD_SEL_LOCO),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_BUILD_LOCO), SetMinimalSize(50, 1), SetResize(1, 0), SetFill(1, 0),
					EndContainer(),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDE_LOCO), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_RENAME_LOCO), SetResize(1, 0), SetFill(1, 0),
				EndContainer(),
			EndContainer(),

		EndContainer(),
		/* Second half of the window contains wagons. */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 0),
					NWidget(WWT_LABEL, INVALID_COLOUR, WID_BV_CAPTION_WAGON), SetStringTip(STR_JUST_STRING, STR_NULL), SetTextStyle(TC_WHITE), SetResize(1, 0), SetFill(1, 0),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SORT_ASCENDING_DESCENDING_WAGON), SetStringTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER), SetFill(1, 0),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_SORT_DROPDOWN_WAGON), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDDEN_WAGONS),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_CARGO_FILTER_DROPDOWN_WAGON), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
				EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY),
					NWidget(WWT_EDITBOX, COLOUR_GREY, WID_BV_FILTER_WAGON), SetResize(1, 0), SetFill(1, 0), SetPadding(2), SetStringTip(STR_LIST_FILTER_OSKTITLE, STR_LIST_FILTER_TOOLTIP),
				EndContainer(),
			EndContainer(),
			/* Vehicle list for wagons. */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_BV_LIST_WAGON), SetResize(1, 1), SetFill(1, 0), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_BV_SCROLLBAR_WAGON),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BV_SCROLLBAR_WAGON),
			EndContainer(),
			/* Panel with details for wagons. */
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BV_PANEL_WAGON), SetMinimalSize(240, 122), SetResize(1, 0), EndContainer(),
			/* Build/rename buttons, resize button for wagons. */
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_WAGON_BUTTONS_SEL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_BUILD_SEL_WAGON),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_BUILD_WAGON), SetMinimalSize(50, 1), SetResize(1, 0), SetFill(1, 0),
					EndContainer(),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SHOW_HIDE_WAGON), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_RENAME_WAGON), SetResize(1, 0), SetFill(1, 0),
					NWidget(WWT_RESIZEBOX, COLOUR_GREY),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_COMB_BUTTONS_SEL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_BV_COMB_BUILD_SEL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_COMB_BUILD), SetMinimalSize(50, 1), SetResize(1, 0), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_COMB_SHOW_HIDE), SetResize(1, 0), SetFill(1, 0), SetStringTip(STR_JUST_STRING, STR_BUY_VEHICLE_TRAIN_HIDE_SHOW_TOGGLE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_COMB_RENAME), SetResize(1, 0), SetFill(1, 0),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

bool _engine_sort_direction;                                            ///< \c false = descending, \c true = ascending.
uint8_t _engine_sort_last_criteria[]    = {0, 0, 0, 0};                 ///< Last set sort criteria, for each vehicle type.
bool _engine_sort_last_order[]          = {false, false, false, false}; ///< Last set direction of the sort order, for each vehicle type.
bool _engine_sort_show_hidden_engines[] = {false, false, false, false}; ///< Last set 'show hidden engines' setting for each vehicle type.
bool _engine_sort_show_hidden_locos     = false;                        ///< Last set 'show hidden locos' setting.
bool _engine_sort_show_hidden_wagons    = false;                        ///< Last set 'show hidden wagons' setting.
static CargoType _engine_sort_last_cargo_criteria[] = {CargoFilterCriteria::CF_ANY, CargoFilterCriteria::CF_ANY, CargoFilterCriteria::CF_ANY, CargoFilterCriteria::CF_ANY}; ///< Last set filter criteria, for each vehicle type.

static uint8_t _last_sort_criteria_loco   = 0;
static bool _last_sort_order_loco         = false;
static CargoType _last_filter_criteria_loco = CargoFilterCriteria::CF_ANY;

static uint8_t _last_sort_criteria_wagon   = 0;
static bool _last_sort_order_wagon         = false;
static CargoType _last_filter_criteria_wagon = CargoFilterCriteria::CF_ANY;

/**
 * Determines order of engines by engineID
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineNumberSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int r = Engine::Get(a.engine_id)->list_position - Engine::Get(b.engine_id)->list_position;

	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by introduction date
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineIntroDateSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const auto va = Engine::Get(a.engine_id)->intro_date;
	const auto vb = Engine::Get(b.engine_id)->intro_date;
	const auto r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by vehicle count
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineVehicleCountSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const GroupStatistics &stats = GroupStatistics::Get(_local_company, ALL_GROUP, Engine::Get(a.engine_id)->type);
	const int r = ((int) stats.GetNumEngines(a.engine_id)) - ((int) stats.GetNumEngines(b.engine_id));

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* cached values for EngineNameSorter to spare many GetString() calls */
static EngineID _last_engine[2] = { INVALID_ENGINE, INVALID_ENGINE };

/**
 * Determines order of engines by name
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineNameSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	static format_buffer last_name[2] = { {}, {} };

	if (a.engine_id != _last_engine[0]) {
		_last_engine[0] = a.engine_id;
		SetDParam(0, PackEngineNameDParam(a.engine_id, EngineNameContext::PurchaseList));
		last_name[0].clear();
		AppendStringInPlace(last_name[0], STR_ENGINE_NAME);
	}

	if (b.engine_id != _last_engine[1]) {
		_last_engine[1] = b.engine_id;
		SetDParam(0, PackEngineNameDParam(b.engine_id, EngineNameContext::PurchaseList));
		last_name[1].clear();
		AppendStringInPlace(last_name[1], STR_ENGINE_NAME);
	}

	int r = StrNaturalCompare(last_name[0], last_name[1]); // Sort by name (natural sorting).

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by reliability
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineReliabilitySorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const int va = Engine::Get(a.engine_id)->reliability;
	const int vb = Engine::Get(b.engine_id)->reliability;
	const int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by purchase cost
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	Money va = Engine::Get(a.engine_id)->GetCost();
	Money vb = Engine::Get(b.engine_id)->GetCost();
	int r = ClampTo<int32_t>(va - vb);

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by speed
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineSpeedSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int va = Engine::Get(a.engine_id)->GetDisplayMaxSpeed();
	int vb = Engine::Get(b.engine_id)->GetDisplayMaxSpeed();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by power
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EnginePowerSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int va = Engine::Get(a.engine_id)->GetPower();
	int vb = Engine::Get(b.engine_id)->GetPower();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by tractive effort
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineTractiveEffortSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int va = Engine::Get(a.engine_id)->GetDisplayMaxTractiveEffort();
	int vb = Engine::Get(b.engine_id)->GetDisplayMaxTractiveEffort();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of engines by running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EngineRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	Money va = Engine::Get(a.engine_id)->GetRunningCost();
	Money vb = Engine::Get(b.engine_id)->GetRunningCost();
	int r = ClampTo<int32_t>(va - vb);

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

static bool GenericEngineValueVsRunningCostSorter(const GUIEngineListItem &a, const uint value_a, const GUIEngineListItem &b, const uint value_b, const GUIEngineListSortCache &cache)
{
	const Engine *e_a = Engine::Get(a.engine_id);
	const Engine *e_b = Engine::Get(b.engine_id);
	Money r_a = e_a->GetRunningCost();
	Money r_b = e_b->GetRunningCost();
	/* Check if running cost is zero in one or both engines.
	 * If only one of them is zero then that one has higher value,
	 * else if both have zero cost then compare powers. */
	if (r_a == 0) {
		if (r_b == 0) {
			/* If it is ambiguous which to return go with their ID */
			if (value_a == value_b) return EngineNumberSorter(a, b, cache);
			return _engine_sort_direction != (value_a < value_b);
		}
		return !_engine_sort_direction;
	}
	if (r_b == 0) return _engine_sort_direction;
	/* Using double for more precision when comparing close values.
	 * This shouldn't have any major effects in performance nor in keeping
	 * the game in sync between players since it's used in GUI only in client side */
	double v_a = (double)value_a / (double)r_a;
	double v_b = (double)value_b / (double)r_b;
	/* Use EngineID to sort if both have same power/running cost,
	 * since we want consistent sorting.
	 * Also if both have no power then sort with reverse of running cost to simulate
	 * previous sorting behaviour for wagons. */
	if (v_a == 0 && v_b == 0) return EngineRunningCostSorter(b, a, cache);
	if (v_a == v_b)  return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction != (v_a < v_b);
}

/**
 * Determines order of engines by power / running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool EnginePowerVsRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	return GenericEngineValueVsRunningCostSorter(a, Engine::Get(a.engine_id)->GetPower(), b, Engine::Get(b.engine_id)->GetPower(), cache);
}

/* Train sorting functions */

/**
 * Determines order of train engines by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool TrainEngineCapacitySorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const RailVehicleInfo *rvi_a = RailVehInfo(a.engine_id);
	const RailVehicleInfo *rvi_b = RailVehInfo(b.engine_id);

	int va = cache.GetArticulatedCapacity(a.engine_id, rvi_a->railveh_type == RAILVEH_MULTIHEAD);
	int vb = cache.GetArticulatedCapacity(b.engine_id, rvi_b->railveh_type == RAILVEH_MULTIHEAD);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of train engines by cargo capacity / running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool TrainEngineCapacityVsRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const RailVehicleInfo *rvi_a = RailVehInfo(a.engine_id);
	const RailVehicleInfo *rvi_b = RailVehInfo(b.engine_id);

	uint va = cache.GetArticulatedCapacity(a.engine_id, rvi_a->railveh_type == RAILVEH_MULTIHEAD);
	uint vb = cache.GetArticulatedCapacity(b.engine_id, rvi_b->railveh_type == RAILVEH_MULTIHEAD);

	return GenericEngineValueVsRunningCostSorter(a, va, b, vb, cache);
}

/**
 * Determines order of train engines by engine / wagon
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool TrainEnginesThenWagonsSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int val_a = (RailVehInfo(a.engine_id)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int val_b = (RailVehInfo(b.engine_id)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int r = val_a - val_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/* Road vehicle sorting functions */

/**
 * Determines order of road vehicles by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool RoadVehEngineCapacitySorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int va = cache.GetArticulatedCapacity(a.engine_id);
	int vb = cache.GetArticulatedCapacity(b.engine_id);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of road vehicles by cargo capacity / running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool RoadVehEngineCapacityVsRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int capacity_a = cache.GetArticulatedCapacity(a.engine_id);
	int capacity_b = cache.GetArticulatedCapacity(b.engine_id);
	return GenericEngineValueVsRunningCostSorter(a, capacity_a, b, capacity_b, cache);
}

/* Ship vehicle sorting functions */

/**
 * Determines order of ships by capacity
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool ShipEngineCapacitySorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int va = cache.GetArticulatedCapacity(a.engine_id);
	int vb = cache.GetArticulatedCapacity(b.engine_id);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of ships by cargo capacity / running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool ShipEngineCapacityVsRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	int capacity_a = cache.GetArticulatedCapacity(a.engine_id);
	int capacity_b = cache.GetArticulatedCapacity(b.engine_id);
	return GenericEngineValueVsRunningCostSorter(a, capacity_a, b, capacity_b, cache);
}

/* Aircraft sorting functions */

/**
 * Determines order of aircraft by cargo
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool AircraftEngineCargoSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const Engine *e_a = Engine::Get(a.engine_id);
	const Engine *e_b = Engine::Get(b.engine_id);

	uint16_t mail_a, mail_b;
	int va = e_a->GetDisplayDefaultCapacity(&mail_a);
	int vb = e_b->GetDisplayDefaultCapacity(&mail_b);
	int r = va - vb;

	if (r == 0) {
		/* The planes have the same passenger capacity. Check mail capacity instead */
		r = mail_a - mail_b;

		if (r == 0) {
			/* Use EngineID to sort instead since we want consistent sorting */
			return EngineNumberSorter(a, b, cache);
		}
	}
	return _engine_sort_direction ? r > 0 : r < 0;
}

/**
 * Determines order of aircraft by cargo capacity / running costs
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool AircraftEngineCapacityVsRunningCostSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	const Engine *e_a = Engine::Get(a.engine_id);
	const Engine *e_b = Engine::Get(b.engine_id);

	uint16_t mail_a, mail_b;
	int va = e_a->GetDisplayDefaultCapacity(&mail_a);
	int vb = e_b->GetDisplayDefaultCapacity(&mail_b);

	return GenericEngineValueVsRunningCostSorter(a, va + mail_a, b, vb + mail_b, cache);
}

/**
 * Determines order of aircraft by range.
 * @param a first engine to compare
 * @param b second engine to compare
 * @return for descending order: returns true if a < b. Vice versa for ascending order
 */
static bool AircraftRangeSorter(const GUIEngineListItem &a, const GUIEngineListItem &b, const GUIEngineListSortCache &cache)
{
	uint16_t r_a = Engine::Get(a.engine_id)->GetRange();
	uint16_t r_b = Engine::Get(b.engine_id)->GetRange();

	int r = r_a - r_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b, cache);
	return _engine_sort_direction ? r > 0 : r < 0;
}

/** Sort functions for the vehicle sort criteria, for each vehicle type. */
EngList_SortTypeFunction * const _engine_sort_functions[][13] = {{
	/* Trains */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EnginePowerSorter,
	&EngineTractiveEffortSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EnginePowerVsRunningCostSorter,
	&EngineReliabilitySorter,
	&TrainEngineCapacitySorter,
	&TrainEngineCapacityVsRunningCostSorter,
	&EngineVehicleCountSorter,
}, {
	/* Road vehicles */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EnginePowerSorter,
	&EngineTractiveEffortSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EnginePowerVsRunningCostSorter,
	&EngineReliabilitySorter,
	&RoadVehEngineCapacitySorter,
	&RoadVehEngineCapacityVsRunningCostSorter,
	&EngineVehicleCountSorter,
}, {
	/* Ships */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EngineReliabilitySorter,
	&ShipEngineCapacitySorter,
	&ShipEngineCapacityVsRunningCostSorter,
	&EngineVehicleCountSorter,
}, {
	/* Aircraft */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EngineReliabilitySorter,
	&AircraftEngineCargoSorter,
	&AircraftEngineCapacityVsRunningCostSorter,
	&EngineVehicleCountSorter,
	&AircraftRangeSorter,
}};

/** Dropdown menu strings for the vehicle sort criteria. */
const std::initializer_list<const StringID> _engine_sort_listing[] = {{
	/* Trains */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_POWER,
	STR_SORT_BY_TRACTIVE_EFFORT,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_POWER_VS_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
	STR_SORT_BY_VEHICLE_COUNT,
}, {
	/* Road vehicles */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_POWER,
	STR_SORT_BY_TRACTIVE_EFFORT,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_POWER_VS_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
	STR_SORT_BY_VEHICLE_COUNT,
}, {
	/* Ships */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
	STR_SORT_BY_VEHICLE_COUNT,
}, {
	/* Aircraft */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
	STR_SORT_BY_VEHICLE_COUNT,
	STR_SORT_BY_RANGE,
}};

/** Filters vehicles by cargo and engine (in case of rail vehicle). */
static bool CargoAndEngineFilter(const GUIEngineListItem *item, const CargoType cargo_type)
{
	if (cargo_type == CargoFilterCriteria::CF_ANY) {
		return true;
	} else if (cargo_type == CargoFilterCriteria::CF_ENGINES) {
		return Engine::Get(item->engine_id)->GetPower() != 0;
	} else {
		CargoTypes refit_mask = GetUnionOfArticulatedRefitMasks(item->engine_id, true) & _standard_cargo_mask;
		return (cargo_type == CargoFilterCriteria::CF_NONE ? refit_mask == 0 : HasBit(refit_mask, cargo_type));
	}
}

static GUIEngineList::FilterFunction * const _engine_filter_funcs[] = {
	&CargoAndEngineFilter,
};

static uint GetCargoWeight(const CargoArray &cap, VehicleType vtype)
{
	uint weight = 0;
	for (CargoType c = 0; c < NUM_CARGO; c++) {
		if (cap[c] != 0) {
			if (vtype == VEH_TRAIN) {
				weight += CargoSpec::Get(c)->WeightOfNUnitsInTrain(cap[c]);
			} else {
				weight += CargoSpec::Get(c)->WeightOfNUnits(cap[c]);
			}
		}
	}
	return weight;
}

static int DrawCargoCapacityInfo(int left, int right, int y, TestedEngineDetails &te, bool refittable)
{
	for (const CargoSpec *cs : _sorted_cargo_specs) {
		CargoType cargo_type = cs->Index();
		if (te.all_capacities[cargo_type] == 0) continue;

		SetDParam(0, cargo_type);
		SetDParam(1, te.all_capacities[cargo_type]);
		SetDParam(2, refittable ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
		DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}

static StringID GetRunningCostString()
{
	if (DayLengthFactor() > 1 && !_settings_client.gui.show_running_costs_calendar_year) {
		return STR_PURCHASE_INFO_RUNNINGCOST_ORIG_YEAR;
	} else if (EconTime::UsingWallclockUnits()) {
		return STR_PURCHASE_INFO_RUNNINGCOST_PERIOD;
	} else {
		return STR_PURCHASE_INFO_RUNNINGCOST_YEAR;
	}
}

/* Draw rail wagon specific details */
static int DrawRailWagonPurchaseInfo(int left, int right, int y, EngineID engine_number, const RailVehicleInfo *rvi, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
	} else {
		SetDParam(0, e->GetCost());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST);
	}
	y += GetCharacterHeight(FS_NORMAL);

	/* Wagon weight - (including cargo) */
	uint weight = e->GetDisplayWeight();
	SetDParam(0, weight);
	SetDParam(1, GetCargoWeight(te.all_capacities, VEH_TRAIN) + weight);
	DrawString(left, right, y, STR_PURCHASE_INFO_WEIGHT_CWEIGHT);
	y += GetCharacterHeight(FS_NORMAL);

	/* Wagon speed limit, displayed if above zero */
	if (_settings_game.vehicle.wagon_speed_limits) {
		uint max_speed = e->GetDisplayMaxSpeed();
		if (max_speed > 0) {
			SetDParam(0, PackVelocity(max_speed, e->type));
			DrawString(left, right, y, STR_PURCHASE_INFO_SPEED);
			y += GetCharacterHeight(FS_NORMAL);
		}
	}

	/* Running cost */
	if (rvi->running_cost_class != INVALID_PRICE) {
		SetDParam(0, e->GetDisplayRunningCost());
		DrawString(left, right, y, GetRunningCostString());
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}

/* Draw locomotive specific details */
static int DrawRailEnginePurchaseInfo(int left, int right, int y, EngineID engine_number, const RailVehicleInfo *rvi, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase Cost - Engine weight */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		SetDParam(2, e->GetDisplayWeight());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_WEIGHT);
	} else {
		SetDParam(0, e->GetCost());
		SetDParam(1, e->GetDisplayWeight());
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_WEIGHT);
	}
	y += GetCharacterHeight(FS_NORMAL);

	/* Max speed - Engine power */
	SetDParam(0, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
	SetDParam(1, e->GetPower());
	DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_POWER);
	y += GetCharacterHeight(FS_NORMAL);

	/* Max tractive effort - not applicable if old acceleration or maglev */
	if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && GetRailTypeInfo(rvi->railtype)->acceleration_type != 2) {
		SetDParam(0, e->GetDisplayMaxTractiveEffort());
		DrawString(left, right, y, STR_PURCHASE_INFO_MAX_TE);
		y += GetCharacterHeight(FS_NORMAL);
	}

	/* Running cost */
	if (rvi->running_cost_class != INVALID_PRICE) {
		SetDParam(0, e->GetDisplayRunningCost());
		DrawString(left, right, y, GetRunningCostString());
		y += GetCharacterHeight(FS_NORMAL);
	}

	/* Powered wagons power - Powered wagons extra weight */
	if (rvi->pow_wag_power != 0) {
		SetDParam(0, rvi->pow_wag_power);
		SetDParam(1, rvi->pow_wag_weight);
		DrawString(left, right, y, STR_PURCHASE_INFO_PWAGPOWER_PWAGWEIGHT);
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}

/* Draw road vehicle specific details */
static int DrawRoadVehPurchaseInfo(int left, int right, int y, EngineID engine_number, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	if (_settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL) {
		/* Purchase Cost */
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
		} else {
			SetDParam(0, e->GetCost());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST);
		}
		y += GetCharacterHeight(FS_NORMAL);

		/* Road vehicle weight - (including cargo) */
		int16_t weight = e->GetDisplayWeight();
		SetDParam(0, weight);
		SetDParam(1, GetCargoWeight(te.all_capacities, VEH_ROAD) + weight);
		DrawString(left, right, y, STR_PURCHASE_INFO_WEIGHT_CWEIGHT);
		y += GetCharacterHeight(FS_NORMAL);

		/* Max speed - Engine power */
		SetDParam(0, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
		SetDParam(1, e->GetPower());
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_POWER);
		y += GetCharacterHeight(FS_NORMAL);

		/* Max tractive effort */
		SetDParam(0, e->GetDisplayMaxTractiveEffort());
		DrawString(left, right, y, STR_PURCHASE_INFO_MAX_TE);
		y += GetCharacterHeight(FS_NORMAL);
	} else {
		/* Purchase cost - Max speed */
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			SetDParam(2, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
		} else {
			SetDParam(0, e->GetCost());
			SetDParam(1, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
		}
		y += GetCharacterHeight(FS_NORMAL);
	}

	/* Running cost */
	SetDParam(0, e->GetDisplayRunningCost());
	DrawString(left, right, y, GetRunningCostString());
	y += GetCharacterHeight(FS_NORMAL);

	return y;
}

/* Draw ship specific details */
static int DrawShipPurchaseInfo(int left, int right, int y, EngineID engine_number, bool refittable, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost - Max speed */
	uint raw_speed = e->GetDisplayMaxSpeed();
	uint ocean_speed = e->u.ship.ApplyWaterClassSpeedFrac(raw_speed, true);
	uint canal_speed = e->u.ship.ApplyWaterClassSpeedFrac(raw_speed, false);

	if (ocean_speed == canal_speed) {
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			SetDParam(2, PackVelocity(ocean_speed, e->type));
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
		} else {
			SetDParam(0, e->GetCost());
			SetDParam(1, PackVelocity(ocean_speed, e->type));
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
		}
		y += GetCharacterHeight(FS_NORMAL);
	} else {
		if (te.cost != 0) {
			SetDParam(0, e->GetCost() + te.cost);
			SetDParam(1, te.cost);
			DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT);
		} else {
			SetDParam(0, e->GetCost());
			DrawString(left, right, y, STR_PURCHASE_INFO_COST);
		}
		y += GetCharacterHeight(FS_NORMAL);

		SetDParam(0, PackVelocity(ocean_speed, e->type));
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_OCEAN);
		y += GetCharacterHeight(FS_NORMAL);

		SetDParam(0, PackVelocity(canal_speed, e->type));
		DrawString(left, right, y, STR_PURCHASE_INFO_SPEED_CANAL);
		y += GetCharacterHeight(FS_NORMAL);
	}

	/* Running cost */
	SetDParam(0, e->GetDisplayRunningCost());
	DrawString(left, right, y, GetRunningCostString());
	y += GetCharacterHeight(FS_NORMAL);

	if (!IsArticulatedEngine(engine_number)) {
		/* Cargo type + capacity */
		SetDParam(0, te.cargo);
		SetDParam(1, te.capacity);
		SetDParam(2, refittable ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
		DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}

/**
 * Draw aircraft specific details in the buy window.
 * @param left Left edge of the window to draw in.
 * @param right Right edge of the window to draw in.
 * @param y Top of the area to draw in.
 * @param engine_number Engine to display.
 * @param refittable If set, the aircraft can be refitted.
 * @return Bottom of the used area.
 */
static int DrawAircraftPurchaseInfo(int left, int right, int y, EngineID engine_number, bool refittable, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);

	/* Purchase cost - Max speed */
	if (te.cost != 0) {
		SetDParam(0, e->GetCost() + te.cost);
		SetDParam(1, te.cost);
		SetDParam(2, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_REFIT_SPEED);
	} else {
		SetDParam(0, e->GetCost());
		SetDParam(1, PackVelocity(e->GetDisplayMaxSpeed(), e->type));
		DrawString(left, right, y, STR_PURCHASE_INFO_COST_SPEED);
	}
	y += GetCharacterHeight(FS_NORMAL);

	/* Cargo capacity */
	if (te.mail_capacity > 0) {
		SetDParam(0, te.cargo);
		SetDParam(1, te.capacity);
		SetDParam(2, GetCargoTypeByLabel(CT_MAIL));
		SetDParam(3, te.mail_capacity);
		DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_CAPACITY);
	} else {
		/* Note, if the default capacity is selected by the refit capacity
		 * callback, then the capacity shown is likely to be incorrect. */
		SetDParam(0, te.cargo);
		SetDParam(1, te.capacity);
		SetDParam(2, refittable ? STR_PURCHASE_INFO_REFITTABLE : STR_EMPTY);
		DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
	}
	y += GetCharacterHeight(FS_NORMAL);

	/* Running cost */
	SetDParam(0, e->GetDisplayRunningCost());
	DrawString(left, right, y, GetRunningCostString());
	y += GetCharacterHeight(FS_NORMAL);

	/* Aircraft type */
	SetDParam(0, e->GetAircraftTypeText());
	DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_TYPE);
	y += GetCharacterHeight(FS_NORMAL);

	/* Aircraft range, if available. */
	uint16_t range = e->GetRange();
	if (range != 0) {
		SetDParam(0, range);
		DrawString(left, right, y, STR_PURCHASE_INFO_AIRCRAFT_RANGE);
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}


/**
 * Try to get the NewGRF engine additional text callback as an optional std::string.
 * @param engine The engine whose additional text to get.
 * @return The std::string if present, otherwise std::nullopt.
 */
static std::optional<std::string> GetNewGRFAdditionalText(EngineID engine)
{
	uint16_t callback = GetVehicleCallback(CBID_VEHICLE_ADDITIONAL_TEXT, 0, 0, engine, nullptr);
	if (callback == CALLBACK_FAILED || callback == 0x400) return std::nullopt;
	const GRFFile *grffile = Engine::Get(engine)->GetGRF();
	assert(grffile != nullptr);
	if (callback > 0x400) {
		ErrorUnknownCallbackResult(grffile->grfid, CBID_VEHICLE_ADDITIONAL_TEXT, callback);
		return std::nullopt;
	}

	StartTextRefStackUsage(grffile, 6);
	std::string result = GetString(GetGRFStringID(grffile, GRFSTR_MISC_GRF_TEXT + callback));
	StopTextRefStackUsage();
	return result;
}

/**
 * Display additional text from NewGRF in the purchase information window
 * @param left   Left border of text bounding box
 * @param right  Right border of text bounding box
 * @param y      Top border of text bounding box
 * @param engine Engine to query the additional purchase information for
 * @return       Bottom border of text bounding box
 */
static uint ShowAdditionalText(int left, int right, int y, EngineID engine)
{
	auto text = GetNewGRFAdditionalText(engine);
	if (!text) return y;
	return DrawStringMultiLine(left, right, y, INT32_MAX, *text, TC_BLACK);
}

void TestedEngineDetails::FillDefaultCapacities(const Engine *e)
{
	this->cargo = e->GetDefaultCargoType();
	if (e->type == VEH_TRAIN || e->type == VEH_ROAD || e->type == VEH_SHIP) {
		this->all_capacities = GetCapacityOfArticulatedParts(e->index);
		this->capacity = this->all_capacities[this->cargo];
		this->mail_capacity = 0;
	} else {
		this->capacity = e->GetDisplayDefaultCapacity(&this->mail_capacity);
		this->all_capacities[this->cargo] = this->capacity;
		if (IsValidCargoType(GetCargoTypeByLabel(CT_MAIL))) {
			this->all_capacities[GetCargoTypeByLabel(CT_MAIL)] = this->mail_capacity;
		} else {
			this->mail_capacity = 0;
		}
	}
	if (this->all_capacities.GetCount() == 0) this->cargo = INVALID_CARGO;
}

/**
 * Draw the purchase info details of a vehicle at a given location.
 * @param left,right,y location where to draw the info
 * @param engine_number the engine of which to draw the info of
 * @return y after drawing all the text
 */
int DrawVehiclePurchaseInfo(int left, int right, int y, EngineID engine_number, TestedEngineDetails &te)
{
	const Engine *e = Engine::Get(engine_number);
	CalTime::YearMonthDay ymd = CalTime::ConvertDateToYMD(e->intro_date);
	bool refittable = IsArticulatedVehicleRefittable(engine_number);
	bool articulated_cargo = false;

	switch (e->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:
			if (e->u.rail.railveh_type == RAILVEH_WAGON) {
				y = DrawRailWagonPurchaseInfo(left, right, y, engine_number, &e->u.rail, te);
			} else {
				y = DrawRailEnginePurchaseInfo(left, right, y, engine_number, &e->u.rail, te);
			}
			articulated_cargo = true;
			break;

		case VEH_ROAD:
			y = DrawRoadVehPurchaseInfo(left, right, y, engine_number, te);
			articulated_cargo = true;
			break;

		case VEH_SHIP:
			y = DrawShipPurchaseInfo(left, right, y, engine_number, refittable, te);
			if (IsArticulatedEngine(engine_number)) articulated_cargo = true;
			break;

		case VEH_AIRCRAFT:
			y = DrawAircraftPurchaseInfo(left, right, y, engine_number, refittable, te);
			break;
	}

	if (articulated_cargo) {
		/* Cargo type + capacity, or N/A */
		int new_y = DrawCargoCapacityInfo(left, right, y, te, refittable);

		if (new_y == y) {
			SetDParam(0, INVALID_CARGO);
			SetDParam(2, STR_EMPTY);
			DrawString(left, right, y, STR_PURCHASE_INFO_CAPACITY);
			y += GetCharacterHeight(FS_NORMAL);
		} else {
			y = new_y;
		}
	}

	/* Draw details that apply to all types except rail wagons. */
	if (e->type != VEH_TRAIN || e->u.rail.railveh_type != RAILVEH_WAGON) {
		/* Design date - Life length */
		SetDParam(0, ymd.year);
		SetDParam(1, DateDeltaToYearDelta(e->GetLifeLengthInDays()));
		DrawString(left, right, y, STR_PURCHASE_INFO_DESIGNED_LIFE);
		y += GetCharacterHeight(FS_NORMAL);

		/* Reliability */
		SetDParam(0, ToPercent16(e->reliability));
		DrawString(left, right, y, STR_PURCHASE_INFO_RELIABILITY);
		y += GetCharacterHeight(FS_NORMAL);
	} else if (_settings_client.gui.show_wagon_intro_year) {
		SetDParam(0, ymd.year);
		DrawString(left, right, y, STR_PURCHASE_INFO_DESIGNED);
		y += GetCharacterHeight(FS_NORMAL);
	}

	if (refittable) y = ShowRefitOptionsList(left, right, y, engine_number);

	y = DrawBadgeNameList({left, y, right, INT16_MAX}, e->badges, static_cast<GrfSpecFeature>(GSF_TRAINS + e->type));

	/* Additional text from NewGRF */
	y = ShowAdditionalText(left, right, y, engine_number);

	/* The NewGRF's name which the vehicle comes from */
	const GRFConfig *config = GetGRFConfig(e->GetGRFID());
	if (_settings_client.gui.show_newgrf_name && config != nullptr)
	{
		DrawString(left, right, y, config->GetName(), TC_BLACK);
		y += GetCharacterHeight(FS_NORMAL);
	}

	return y;
}

static void DrawEngineBadgeColumn(const Rect &r, int column_group, const GUIBadgeClasses &badge_classes, const Engine *e, PaletteID remap)
{
	DrawBadgeColumn(r, column_group, badge_classes, e->badges, static_cast<GrfSpecFeature>(GSF_TRAINS + e->type), e->info.base_intro, remap);
}

/**
 * Engine drawing loop
 * @param type Type of vehicle (VEH_*)
 * @param r The Rect of the list
 * @param eng_list What engines to draw
 * @param sb Scrollbar of list.
 * @param selected_id what engine to highlight as selected, if any
 * @param show_count Whether to show the amount of engines or not
 * @param selected_group the group to list the engines of
 */
void DrawEngineList(VehicleType type, const Rect &r, const GUIEngineList &eng_list, const Scrollbar &sb, EngineID selected_id, bool show_count, GroupID selected_group, const GUIBadgeClasses &badge_classes)
{
	static const std::array<int8_t, VehicleType::VEH_COMPANY_END> sprite_y_offsets = { 0, 0, -1, -1 };

	auto [first, last] = sb.GetVisibleRangeIterators(eng_list);

	bool rtl = _current_text_dir == TD_RTL;
	int step_size = GetEngineListHeight(type);
	int sprite_left  = GetVehicleImageCellSize(type, EIT_PURCHASE).extend_left;
	int sprite_right = GetVehicleImageCellSize(type, EIT_PURCHASE).extend_right;
	int sprite_width = sprite_left + sprite_right;
	int circle_width = std::max(GetScaledSpriteSize(SPR_CIRCLE_FOLDED).width, GetScaledSpriteSize(SPR_CIRCLE_UNFOLDED).width);
	int linecolour = GetColourGradient(COLOUR_ORANGE, SHADE_NORMAL);

	auto badge_column_widths = badge_classes.GetColumnWidths();

	Rect ir = r.WithHeight(step_size).Shrink(WidgetDimensions::scaled.matrix, RectPadding::zero);
	int sprite_y_offset = ScaleSpriteTrad(sprite_y_offsets[type]) + ir.Height() / 2;

	Dimension replace_icon = {0, 0};
	int count_width = 0;
	if (show_count) {
		replace_icon = GetSpriteSize(SPR_GROUP_REPLACE_ACTIVE);

		uint biggest_num_engines = 0;
		for (auto it = first; it != last; ++it) {
			const uint num_engines = GetGroupNumEngines(_local_company, selected_group, it->engine_id);
			biggest_num_engines = std::max(biggest_num_engines, num_engines);
		}

		SetDParam(0, biggest_num_engines);
		count_width = GetStringBoundingBox(STR_JUST_COMMA, FS_SMALL).width;
	}

	const int text_row_height = ir.Shrink(WidgetDimensions::scaled.matrix).Height();
	const int normal_text_y_offset = (text_row_height - GetCharacterHeight(FS_NORMAL)) / 2;
	const int small_text_y_offset  = text_row_height - GetCharacterHeight(FS_SMALL);

	const int offset = (rtl ? -circle_width : circle_width) / 2;
	const int level_width = rtl ? -WidgetDimensions::scaled.hsep_indent : WidgetDimensions::scaled.hsep_indent;

	for (auto it = first; it != last; ++it) {
		const auto &item = *it;
		const Engine *e = Engine::Get(item.engine_id);

		uint indent       = item.indent * WidgetDimensions::scaled.hsep_indent;
		bool has_variants = item.flags.Test(EngineDisplayFlag::HasVariants);
		bool is_folded    = item.flags.Test(EngineDisplayFlag::IsFolded);
		bool shaded       = item.flags.Test(EngineDisplayFlag::Shaded);

		Rect textr = ir.Shrink(WidgetDimensions::scaled.matrix);
		Rect tr = ir.Indent(indent, rtl);

		if (item.indent > 0) {
			/* Draw tree continuation lines. */
			int tx = (rtl ? ir.right : ir.left) + offset;
			for (uint lvl = 1; lvl <= item.indent; ++lvl) {
				if (HasBit(item.level_mask, lvl)) GfxDrawLine(tx, ir.top, tx, ir.bottom, linecolour, WidgetDimensions::scaled.fullbevel.top);
				if (lvl < item.indent) tx += level_width;
			}
			/* Draw our node in the tree. */
			int ycentre = CenterBounds(textr.top, textr.bottom, WidgetDimensions::scaled.fullbevel.top);
			if (!HasBit(item.level_mask, item.indent)) GfxDrawLine(tx, ir.top, tx, ycentre, linecolour, WidgetDimensions::scaled.fullbevel.top);
			GfxDrawLine(tx, ycentre, tx + offset - (rtl ? -1 : 1), ycentre, linecolour, WidgetDimensions::scaled.fullbevel.top);
		}

		if (has_variants) {
			Rect fr = tr.WithWidth(circle_width, rtl);
			DrawSpriteIgnorePadding(is_folded ? SPR_CIRCLE_FOLDED : SPR_CIRCLE_UNFOLDED, PAL_NONE, {fr.left, textr.top, fr.right, textr.bottom}, SA_CENTER);
		}

		tr = tr.Indent(circle_width + WidgetDimensions::scaled.hsep_normal, rtl);

		/* Note: num_engines is only used in the autoreplace GUI, so it is correct to use _local_company here. */
		const uint num_engines = GetGroupNumEngines(_local_company, selected_group, item.engine_id);
		const PaletteID pal = (show_count && num_engines == 0) ? PALETTE_CRASH : GetEnginePalette(item.engine_id, _local_company);

		if (badge_column_widths.size() >= 1 && badge_column_widths[0] > 0) {
			Rect br = tr.WithWidth(badge_column_widths[0], rtl);
			DrawEngineBadgeColumn(br, 0, badge_classes, e, pal);
			tr = tr.Indent(badge_column_widths[0], rtl);
		}

		int sprite_x = tr.WithWidth(sprite_width, rtl).left + sprite_left;
		DrawVehicleEngine(r.left, r.right, sprite_x, tr.top + sprite_y_offset, item.engine_id, pal, EIT_PURCHASE);

		tr = tr.Indent(sprite_width + WidgetDimensions::scaled.hsep_wide, rtl);

		if (badge_column_widths.size() >= 2 && badge_column_widths[1] > 0) {
			Rect br = tr.WithWidth(badge_column_widths[1], rtl);
			DrawEngineBadgeColumn(br, 1, badge_classes, e, pal);
			tr = tr.Indent(badge_column_widths[1], rtl);
		}

		if (show_count) {
			/* Rect for replace-protection icon. */
			Rect rr = tr.WithWidth(replace_icon.width, !rtl);
			tr = tr.Indent(replace_icon.width + WidgetDimensions::scaled.hsep_normal, !rtl);
			/* Rect for engine type count text. */
			Rect cr = tr.WithWidth(count_width, !rtl);
			tr = tr.Indent(count_width + WidgetDimensions::scaled.hsep_normal, !rtl);

			SetDParam(0, num_engines);
			DrawString(cr.left, cr.right, textr.top + small_text_y_offset, STR_JUST_COMMA, TC_BLACK, SA_RIGHT | SA_FORCE, false, FS_SMALL);

			if (EngineHasReplacementForCompany(Company::Get(_local_company), item.engine_id, selected_group)) {
				DrawSpriteIgnorePadding(SPR_GROUP_REPLACE_ACTIVE, num_engines == 0 ? PALETTE_CRASH : PAL_NONE, rr, SA_CENTER);
			}
		}

		if (badge_column_widths.size() >= 3 && badge_column_widths[2] > 0) {
			Rect br = tr.WithWidth(badge_column_widths[2], !rtl).Indent(WidgetDimensions::scaled.hsep_wide, rtl);
			DrawEngineBadgeColumn(br, 2, badge_classes, e, pal);
			tr = tr.Indent(badge_column_widths[2], !rtl);
		}

		bool hidden = e->company_hidden.Test(_local_company);
		StringID str = hidden ? STR_HIDDEN_ENGINE_NAME : STR_ENGINE_NAME;
		TextColour tc = (item.engine_id == selected_id) ? TC_WHITE : ((hidden | shaded) ? (TC_GREY | TC_FORCED | TC_NO_SHADE) : TC_BLACK);

		if (show_count) {
			/* relies on show_count to find 'Vehicle in use' panel of autoreplace window */
			SetDParam(0, PackEngineNameDParam(item.engine_id, EngineNameContext::AutoreplaceVehicleInUse, item.indent));
		} else {
			SetDParam(0, PackEngineNameDParam(item.engine_id, EngineNameContext::PurchaseList, item.indent));
		}
		DrawString(tr.left, tr.right, textr.top + normal_text_y_offset, str, tc);

		ir = ir.Translate(0, step_size);
	}
}

/**
 * Display the dropdown for the vehicle sort criteria.
 * @param w Parent window (holds the dropdown button).
 * @param vehicle_type %Vehicle type being sorted.
 * @param selected Currently selected sort criterion.
 * @param button Widget button.
 */
void DisplayVehicleSortDropDown(Window *w, VehicleType vehicle_type, int selected, WidgetID button)
{
	uint32_t hidden_mask = 0;
	/* Disable sorting by power or tractive effort when the original acceleration model for road vehicles is being used. */
	if (vehicle_type == VEH_ROAD && _settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) {
		SetBit(hidden_mask, 3); // power
		SetBit(hidden_mask, 4); // tractive effort
		SetBit(hidden_mask, 8); // power by running costs
	}
	/* Disable sorting by tractive effort when the original acceleration model for trains is being used. */
	if (vehicle_type == VEH_TRAIN && _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
		SetBit(hidden_mask, 4); // tractive effort
	}
	ShowDropDownMenu(w, _engine_sort_listing[vehicle_type], selected, button, 0, hidden_mask);
}

/**
 * Add children to GUI engine list to build a hierarchical tree.
 * @param dst Destination list.
 * @param src Source list.
 * @param parent Current tree parent (set by self with recursion).
 * @param indent Current tree indentation level (set by self with recursion).
 */
void GUIEngineListAddChildren(GUIEngineList &dst, const GUIEngineList &src, EngineID parent, uint8_t indent)
{
	for (const auto &item : src) {
		if (item.variant_id != parent || item.engine_id == parent) continue;

		const Engine *e = Engine::Get(item.engine_id);
		EngineDisplayFlags flags = item.flags;
		if (e->display_last_variant != INVALID_ENGINE) flags.Reset(EngineDisplayFlag::Shaded);
		dst.emplace_back(e->display_last_variant == INVALID_ENGINE ? item.engine_id : e->display_last_variant, item.engine_id, flags, indent);

		/* Add variants if not folded */
		if (item.flags.Test(EngineDisplayFlag::HasVariants) && !item.flags.Test(EngineDisplayFlag::IsFolded)) {
			/* Add this engine again as a child */
			if (!item.flags.Test(EngineDisplayFlag::Shaded)) {
				dst.emplace_back(item.engine_id, item.engine_id, EngineDisplayFlags{}, indent + 1);
			}
			GUIEngineListAddChildren(dst, src, item.engine_id, indent + 1);
		}
	}

	if (indent > 0 || dst.empty()) return;

	/* Hierarchy is complete, traverse in reverse to find where indentation levels continue. */
	uint16_t level_mask = 0;
	for (auto it = std::rbegin(dst); std::next(it) != std::rend(dst); ++it) {
		auto next_it = std::next(it);
		SB(level_mask, it->indent, 1, it->indent <= next_it->indent);
		next_it->level_mask = level_mask;
	}
}

/** Enum referring to the Hotkeys in the build vehicle window */
enum BuildVehicleHotkeys : int32_t {
	BVHK_FOCUS_FILTER_BOX, ///< Focus the edit box for editing the filter string
};

struct BuildVehicleWindowBase : Window {
	VehicleType vehicle_type;                   ///< Type of vehicles shown in the window.
	TileIndex tile;                             ///< Original tile.
	bool virtual_train_mode;                    ///< Are we building a virtual train?
	Train **virtual_train_out;                  ///< Virtual train ptr
	bool listview_mode;                         ///< If set, only display the available vehicles and do not show a 'build' button.

	BuildVehicleWindowBase(WindowDesc &desc, TileIndex tile, VehicleType type, Train **virtual_train_out) : Window(desc)
	{
		this->vehicle_type = type;
		this->tile = tile;
		this->window_number = tile == INVALID_TILE ? (uint)type : tile.base();
		this->virtual_train_out = virtual_train_out;
		this->virtual_train_mode = (virtual_train_out != nullptr);
		if (this->virtual_train_mode) this->window_number = 0;
		this->listview_mode = (tile == INVALID_TILE) && !this->virtual_train_mode;
	}

	void AddVirtualEngine(Train *toadd)
	{
		if (this->virtual_train_out == nullptr) return;

		if (*(this->virtual_train_out) == nullptr) {
			*(this->virtual_train_out) = toadd;
		}

		InvalidateWindowClassesData(WC_CREATE_TEMPLATE);
	}

	VehicleID GetNewVirtualEngineMoveTarget() const
	{
		assert(this->virtual_train_out != nullptr);

		Train *current = *(this->virtual_train_out);
		return (current != nullptr) ? current->index : INVALID_VEHICLE;
	}

	StringID GetCargoFilterLabel(CargoType cid) const
	{
		switch (cid) {
			case CargoFilterCriteria::CF_ANY: return STR_PURCHASE_INFO_ALL_TYPES;
			case CargoFilterCriteria::CF_ENGINES: return STR_PURCHASE_INFO_ENGINES_ONLY;
			case CargoFilterCriteria::CF_NONE: return STR_PURCHASE_INFO_NONE;
			default: return CargoSpec::Get(cid)->name;
		}
	}

	DropDownList BuildCargoDropDownList(bool hide_engines = false) const
	{
		DropDownList list;

		/* Add item for disabling filtering. */
		list.push_back(MakeDropDownListStringItem(this->GetCargoFilterLabel(CargoFilterCriteria::CF_ANY), CargoFilterCriteria::CF_ANY, false));
		/* Specific filters for trains. */
		if (this->vehicle_type == VEH_TRAIN) {
			if (!hide_engines) {
				/* Add item for locomotives only in case of trains. */
				list.push_back(MakeDropDownListStringItem(this->GetCargoFilterLabel(CargoFilterCriteria::CF_ENGINES), CargoFilterCriteria::CF_ENGINES, false));
			}

			/* Add item for vehicles not carrying anything, e.g. train engines.
			 * This could also be useful for eyecandy vehicles of other types, but is likely too confusing for joe, */
			list.push_back(MakeDropDownListStringItem(this->GetCargoFilterLabel(CargoFilterCriteria::CF_NONE), CargoFilterCriteria::CF_NONE, false));
		}

		/* Add cargos */
		Dimension d = GetLargestCargoIconSize();
		for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
			list.push_back(MakeDropDownListIconItem(d, cs->GetCargoIcon(), PAL_NONE, cs->name, cs->Index(), false));
		}

		return list;
	}

	void FillTestedEngineCapacity(EngineID engine, CargoType cargo, TestedEngineDetails &te) const
	{
		const Engine *e = Engine::Get(engine);
		if (!e->CanPossiblyCarryCargo()) {
			te.cost = 0;
			te.cargo = INVALID_CARGO;
			te.all_capacities.Clear();
			return;
		}

		if (this->virtual_train_mode) {
			if (cargo != INVALID_CARGO && cargo != e->GetDefaultCargoType()) {
				SavedRandomSeeds saved_seeds;
				SaveRandomSeeds(&saved_seeds);
				StringID err;
				Train *t = BuildVirtualRailVehicle(engine, err, (ClientID)0, false);
				if (t != nullptr) {
					const CommandCost ret = Command<CMD_REFIT_VEHICLE>::Do(DC_QUERY_COST, t->index, cargo, 0, false, false, 1);
					te.cost          = ret.GetCost();
					te.capacity      = _returned_refit_capacity;
					te.mail_capacity = _returned_mail_refit_capacity;
					te.cargo         = cargo;
					te.all_capacities = _returned_vehicle_capacities;
					delete t;
					RestoreRandomSeeds(saved_seeds);
					return;
				} else {
					RestoreRandomSeeds(saved_seeds);
				}
			}
		} else if (!this->listview_mode) {
			/* Query for cost and refitted capacity */
			CommandCost ret = Command<CMD_BUILD_VEHICLE>::Do(DC_QUERY_COST, TileIndex(this->window_number), engine, true, cargo, INVALID_CLIENT_ID);
			if (ret.Succeeded()) {
				te.cost          = ret.GetCost() - e->GetCost();
				te.capacity      = _returned_refit_capacity;
				te.mail_capacity = _returned_mail_refit_capacity;
				te.cargo         = (cargo == INVALID_CARGO) ? e->GetDefaultCargoType() : cargo;
				te.all_capacities = _returned_vehicle_capacities;
				return;
			}
		}

		/* Purchase test was not possible or failed, fill in the defaults instead. */
		te = {};
		te.FillDefaultCapacities(e);
	}

	void ChangeDualPaneMode(bool new_value)
	{
		_settings_client.gui.dual_pane_train_purchase_window = new_value;
		SetWindowDirty(WC_GAME_OPTIONS, WN_GAME_OPTIONS_GAME_SETTINGS);

		if (this->virtual_train_out != nullptr) {
			ShowTemplateTrainBuildVehicleWindow(this->virtual_train_out);
		} else {
			ShowBuildVehicleWindow(this->tile, this->vehicle_type);
		}
	}
};

/**
 * Update cargo filter
 * @param parent parent window, may be nullptr
 * @param cargo_filter_criteria cargo filter criteria
 */
void GUIEngineListSortCache::UpdateCargoFilter(const BuildVehicleWindowBase *parent, CargoType cargo_filter_criteria)
{
	this->parent = parent;

	if (cargo_filter_criteria >= NUM_CARGO) cargo_filter_criteria = INVALID_CARGO;

	if (cargo_filter_criteria != this->current_cargo) {
		this->current_cargo = cargo_filter_criteria;
		this->capacities.clear();
	}
}

uint GUIEngineListSortCache::GetArticulatedCapacity(EngineID eng, bool dual_headed) const
{
	auto iter = this->capacities.insert({ eng, 0 });
	if (iter.second) {
		/* New cache entry */
		const Engine *e = Engine::Get(eng);
		if (this->current_cargo != INVALID_CARGO && this->current_cargo != e->GetDefaultCargoType() && e->info.callback_mask.Test(VehicleCallbackMask::RefitCapacity) && e->refit_capacity_values == nullptr && this->parent != nullptr) {
			/* Expensive path simulating vehicle construction is required to determine capacity */
			TestedEngineDetails te{};
			this->parent->FillTestedEngineCapacity(eng, this->current_cargo, te);
			iter.first->second = te.all_capacities.GetSum<uint>();
		} else {
			iter.first->second = GetTotalCapacityOfArticulatedParts(eng, this->current_cargo) * (dual_headed ? 2 : 1);
		}
	}
	return iter.first->second;
}

/** GUI for building vehicles. */
struct BuildVehicleWindow : BuildVehicleWindowBase {
	union {
		RailType railtype;   ///< Rail type to show, or #INVALID_RAILTYPE.
		RoadType roadtype;   ///< Road type to show, or #INVALID_ROADTYPE.
	} filter;                                   ///< Filter to apply.
	bool descending_sort_order;                 ///< Sort direction, @see _engine_sort_direction
	uint8_t sort_criteria;                      ///< Current sort criterium.
	bool show_hidden_engines;                   ///< State of the 'show hidden engines' button.
	EngineID sel_engine;                        ///< Currently selected engine, or #INVALID_ENGINE
	EngineID rename_engine;                     ///< Engine being renamed.
	GUIEngineList eng_list;
	CargoType cargo_filter_criteria;              ///< Selected cargo filter
	int details_height;                         ///< Minimal needed height of the details panels, in text lines (found so far).
	Scrollbar *vscroll;
	TestedEngineDetails te;                     ///< Tested cost and capacity after refit.
	GUIBadgeClasses badge_classes;

	StringFilter string_filter;                 ///< Filter for vehicle name
	QueryString vehicle_editbox;                ///< Filter editbox

	void SetBuyVehicleText()
	{
		NWidgetCore *widget = this->GetWidget<NWidgetCore>(WID_BV_BUILD);

		bool refit = this->sel_engine != INVALID_ENGINE && this->cargo_filter_criteria != CargoFilterCriteria::CF_ANY && this->cargo_filter_criteria != CargoFilterCriteria::CF_NONE && this->cargo_filter_criteria != CargoFilterCriteria::CF_ENGINES;
		if (refit) refit = Engine::Get(this->sel_engine)->GetDefaultCargoType() != this->cargo_filter_criteria;

		if (this->virtual_train_mode) {
			if (refit) {
				widget->SetStringTip(STR_TMPL_ADD_VEHICLE_REFIT, STR_TMPL_ADD_REFIT_TOOLTIP);
			} else {
				widget->SetStringTip(STR_TMPL_ADD_VEHICLE, STR_TMPL_ADD_TOOLTIP);
			}
		} else {
			if (refit) {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_BUTTON + this->vehicle_type, STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_TOOLTIP + this->vehicle_type);
			} else {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_BUTTON + this->vehicle_type, STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_TOOLTIP + this->vehicle_type);
			}
		}
	}

	BuildVehicleWindow(WindowDesc &desc, TileIndex tile, VehicleType type, Train **virtual_train_out) : BuildVehicleWindowBase(desc, tile, type, virtual_train_out), vehicle_editbox(MAX_LENGTH_VEHICLE_NAME_CHARS * MAX_CHAR_LENGTH, MAX_LENGTH_VEHICLE_NAME_CHARS)
	{
		this->sel_engine = INVALID_ENGINE;

		this->sort_criteria         = _engine_sort_last_criteria[type];
		this->descending_sort_order = _engine_sort_last_order[type];
		this->show_hidden_engines   = _engine_sort_show_hidden_engines[type];

		this->UpdateFilterByTile();

		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_BV_SCROLLBAR);

		/* If we are just viewing the list of vehicles, we do not need the Build button.
		 * So we just hide it, and enlarge the Rename button by the now vacant place. */
		if (this->listview_mode) {
			this->GetWidget<NWidgetStacked>(WID_BV_BUILD_SEL)->SetDisplayedPlane(SZSP_NONE);
		}

		NWidgetCore *widget = this->GetWidget<NWidgetCore>(WID_BV_LIST);
		widget->SetToolTip(STR_BUY_VEHICLE_TRAIN_LIST_TOOLTIP + type);

		widget = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDE);
		widget->SetToolTip(STR_BUY_VEHICLE_TRAIN_HIDE_SHOW_TOGGLE_TOOLTIP + type);

		widget = this->GetWidget<NWidgetCore>(WID_BV_RENAME);
		widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_RENAME_BUTTON + type, STR_BUY_VEHICLE_TRAIN_RENAME_TOOLTIP + type);

		widget = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDDEN_ENGINES);
		widget->SetStringTip(STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN + type, STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN_TOOLTIP + type);
		widget->SetLowered(this->show_hidden_engines);

		this->details_height = ((this->vehicle_type == VEH_TRAIN) ? 10 : 9);

		this->GetWidget<NWidgetStacked>(WID_BV_TOGGLE_DUAL_PANE_SEL)->SetDisplayedPlane((this->vehicle_type == VEH_TRAIN) ? 0 : SZSP_NONE);

		this->FinishInitNested(this->window_number);

		this->querystrings[WID_BV_FILTER] = &this->vehicle_editbox;
		this->vehicle_editbox.cancel_button = QueryString::ACTION_CLEAR;

		this->owner = (tile != INVALID_TILE) ? GetTileOwner(tile) : _local_company;

		this->eng_list.ForceRebuild();
		this->GenerateBuildList(); // generate the list, since we need it in the next line
		this->vscroll->SetCount(this->eng_list.size());

		/* Select the first unshaded engine in the list as default when opening the window */
		EngineID engine = INVALID_ENGINE;
		auto it = std::ranges::find_if(this->eng_list, [](const GUIEngineListItem &item) { return !item.flags.Test(EngineDisplayFlag::Shaded); });
		if (it != this->eng_list.end()) engine = it->engine_id;
		this->SelectEngine(engine);
	}

	/** Set the filter type according to the depot type */
	void UpdateFilterByTile()
	{
		switch (this->vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:
				if (this->listview_mode || this->virtual_train_mode) {
					this->filter.railtype = INVALID_RAILTYPE;
				} else {
					this->filter.railtype = GetRailType(TileIndex(this->window_number));
				}
				break;

			case VEH_ROAD:
				if (this->listview_mode || this->virtual_train_mode) {
					this->filter.roadtype = INVALID_ROADTYPE;
				} else {
					this->filter.roadtype = GetRoadTypeRoad(TileIndex(this->window_number));
					if (this->filter.roadtype == INVALID_ROADTYPE) {
						this->filter.roadtype = GetRoadTypeTram(TileIndex(this->window_number));
					}
				}
				break;

			case VEH_SHIP:
			case VEH_AIRCRAFT:
				break;
		}
	}

	/** Populate the filter list and set the cargo filter criteria. */
	void SetCargoFilterArray()
	{
		/* Set the last cargo filter criteria. */
		this->cargo_filter_criteria = _engine_sort_last_cargo_criteria[this->vehicle_type];
		if (this->cargo_filter_criteria < NUM_CARGO && !HasBit(_standard_cargo_mask, this->cargo_filter_criteria)) this->cargo_filter_criteria = CargoFilterCriteria::CF_ANY;

		this->eng_list.SetFilterFuncs(_engine_filter_funcs);
		this->eng_list.SetFilterState(this->cargo_filter_criteria != CargoFilterCriteria::CF_ANY);
	}

	void SelectEngine(EngineID engine)
	{
		CargoType cargo = this->cargo_filter_criteria;
		if (cargo == CargoFilterCriteria::CF_ANY || cargo == CargoFilterCriteria::CF_ENGINES || cargo == CargoFilterCriteria::CF_NONE) cargo = INVALID_CARGO;

		this->sel_engine = engine;
		this->SetBuyVehicleText();

		if (this->sel_engine == INVALID_ENGINE) return;

		this->FillTestedEngineCapacity(this->sel_engine, cargo, this->te);
	}

	void OnInit() override
	{
		this->badge_classes = GUIBadgeClasses(static_cast<GrfSpecFeature>(GSF_TRAINS + this->vehicle_type));
		this->SetCargoFilterArray();
		this->vscroll->SetCount(this->eng_list.size());
	}

	/** Filter the engine list against the currently selected cargo filter */
	void FilterEngineList()
	{
		this->eng_list.Filter(this->cargo_filter_criteria);
		if (0 == this->eng_list.size()) { // no engine passed through the filter, invalidate the previously selected engine
			this->SelectEngine(INVALID_ENGINE);
		} else if (std::ranges::find(this->eng_list, this->sel_engine, &GUIEngineListItem::engine_id) == this->eng_list.end()) { // previously selected engine didn't pass the filter, select the first engine of the list
			this->SelectEngine(this->eng_list[0].engine_id);
		}
	}

	/** Filter a single engine */
	bool FilterSingleEngine(EngineID eid)
	{
		GUIEngineListItem item = {eid, eid, EngineDisplayFlags{}, 0};
		return CargoAndEngineFilter(&item, this->cargo_filter_criteria);
	}

	/** Filter by name and NewGRF extra text */
	bool FilterByText(const Engine *e)
	{
		/* Do not filter if the filter text box is empty */
		if (this->string_filter.IsEmpty()) return true;

		/* Filter engine name */
		this->string_filter.ResetState();
		SetDParam(0, PackEngineNameDParam(e->index, EngineNameContext::PurchaseList));
		this->string_filter.AddLine(GetString(STR_ENGINE_NAME));

		/* Filter NewGRF extra text */
		auto text = GetNewGRFAdditionalText(e->index);
		if (text) this->string_filter.AddLine(*text);

		return this->string_filter.GetState();
	}

	/* Figure out what train EngineIDs to put in the list */
	void GenerateBuildTrainList(GUIEngineList &list)
	{
		std::vector<EngineID> variants;
		EngineID sel_id = INVALID_ENGINE;
		size_t num_engines = 0;

		list.clear();

		BadgeTextFilter btf(this->string_filter, GSF_TRAINS);

		/* Make list of all available train engines and wagons.
		 * Also check to see if the previously selected engine is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when engines become obsolete and are removed */
		for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
			if (!this->show_hidden_engines && e->IsVariantHidden(_local_company)) continue;
			EngineID eid = e->index;
			const RailVehicleInfo *rvi = &e->u.rail;

			if (this->filter.railtype != INVALID_RAILTYPE && !HasPowerOnRail(rvi->railtype, this->filter.railtype)) continue;
			if (!IsEngineBuildable(eid, VEH_TRAIN, _local_company)) continue;

			/* Filter now! So num_engines and num_wagons is valid */
			if (!FilterSingleEngine(eid)) continue;

			/* Filter by name or NewGRF extra text */
			if (!FilterByText(e) && !btf.Filter(e->badges)) continue;

			list.emplace_back(eid, e->info.variant_id, e->display_flags, 0);

			if (rvi->railveh_type != RAILVEH_WAGON) num_engines++;

			/* Add all parent variants of this engine to the variant list */
			EngineID parent = e->info.variant_id;
			while (parent != INVALID_ENGINE) {
				variants.push_back(parent);
				parent = Engine::Get(parent)->info.variant_id;
			}

			if (eid == this->sel_engine) sel_id = eid;
		}

		/* ensure primary engine of variant group is in list */
		for (const auto &variant : variants) {
			if (std::ranges::find(list, variant, &GUIEngineListItem::engine_id) == list.end()) {
				const Engine *e = Engine::Get(variant);
				list.emplace_back(variant, e->info.variant_id, e->display_flags | EngineDisplayFlag::Shaded, 0);
				if (e->u.rail.railveh_type != RAILVEH_WAGON) num_engines++;
			}
		}

		this->SelectEngine(sel_id);

		/* invalidate cached values for name sorter - engine names could change */
		_last_engine[0] = _last_engine[1] = INVALID_ENGINE;

		/* setup engine capacity cache */
		list.SortParameterData().UpdateCargoFilter(this, this->cargo_filter_criteria);

		/* make engines first, and then wagons, sorted by selected sort_criteria */
		_engine_sort_direction = false;
		EngList_Sort(list, TrainEnginesThenWagonsSorter);

		/* and then sort engines */
		_engine_sort_direction = this->descending_sort_order;
		EngList_SortPartial(list, _engine_sort_functions[0][this->sort_criteria], 0, num_engines);

		/* and finally sort wagons */
		EngList_SortPartial(list, _engine_sort_functions[0][this->sort_criteria], num_engines, list.size() - num_engines);
	}

	/* Figure out what road vehicle EngineIDs to put in the list */
	void GenerateBuildRoadVehList()
	{
		EngineID sel_id = INVALID_ENGINE;

		this->eng_list.clear();

		BadgeTextFilter btf(this->string_filter, GSF_ROADVEHICLES);

		for (const Engine *e : Engine::IterateType(VEH_ROAD)) {
			if (!this->show_hidden_engines && e->IsVariantHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_ROAD, _local_company)) continue;
			if (this->filter.roadtype != INVALID_ROADTYPE && !HasPowerOnRoad(e->u.road.roadtype, this->filter.roadtype)) continue;

			/* Filter by name or NewGRF extra text */
			if (!FilterByText(e) && !btf.Filter(e->badges)) continue;

			this->eng_list.emplace_back(eid, e->info.variant_id, e->display_flags, 0);

			if (eid == this->sel_engine) sel_id = eid;
		}
		this->SelectEngine(sel_id);
	}

	/* Figure out what ship EngineIDs to put in the list */
	void GenerateBuildShipList()
	{
		EngineID sel_id = INVALID_ENGINE;
		this->eng_list.clear();

		BadgeTextFilter btf(this->string_filter, GSF_SHIPS);

		for (const Engine *e : Engine::IterateType(VEH_SHIP)) {
			if (!this->show_hidden_engines && e->IsVariantHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_SHIP, _local_company)) continue;

			/* Filter by name or NewGRF extra text */
			if (!FilterByText(e) && !btf.Filter(e->badges)) continue;

			this->eng_list.emplace_back(eid, e->info.variant_id, e->display_flags, 0);

			if (eid == this->sel_engine) sel_id = eid;
		}
		this->SelectEngine(sel_id);
	}

	/* Figure out what aircraft EngineIDs to put in the list */
	void GenerateBuildAircraftList()
	{
		EngineID sel_id = INVALID_ENGINE;

		this->eng_list.clear();

		const Station *st = this->listview_mode ? nullptr : Station::GetByTile(TileIndex(this->window_number));

		BadgeTextFilter btf(this->string_filter, GSF_AIRCRAFT);

		/* Make list of all available planes.
		 * Also check to see if the previously selected plane is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when planes become obsolete and are removed */
		for (const Engine *e : Engine::IterateType(VEH_AIRCRAFT)) {
			if (!this->show_hidden_engines && e->IsVariantHidden(_local_company)) continue;
			EngineID eid = e->index;
			if (!IsEngineBuildable(eid, VEH_AIRCRAFT, _local_company)) continue;
			/* First VEH_END window_numbers are fake to allow a window open for all different types at once */
			if (!this->listview_mode && !CanVehicleUseStation(eid, st)) continue;

			/* Filter by name or NewGRF extra text */
			if (!FilterByText(e) && !btf.Filter(e->badges)) continue;

			this->eng_list.emplace_back(eid, e->info.variant_id, e->display_flags, 0);

			if (eid == this->sel_engine) sel_id = eid;
		}

		this->SelectEngine(sel_id);
	}

	/* Generate the list of vehicles */
	void GenerateBuildList()
	{
		if (!this->eng_list.NeedRebuild()) return;

		/* Update filter type in case the road/railtype of the depot got converted */
		this->UpdateFilterByTile();

		this->eng_list.clear();

		GUIEngineList list;

		switch (this->vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:
				this->GenerateBuildTrainList(list);
				GUIEngineListAddChildren(this->eng_list, list);
				this->eng_list.RebuildDone();
				return;
			case VEH_ROAD:
				this->GenerateBuildRoadVehList();
				break;
			case VEH_SHIP:
				this->GenerateBuildShipList();
				break;
			case VEH_AIRCRAFT:
				this->GenerateBuildAircraftList();
				break;
		}

		this->FilterEngineList();

		/* ensure primary engine of variant group is in list after filtering */
		std::vector<EngineID> variants;
		for (const auto &item : this->eng_list) {
			EngineID parent = item.variant_id;
			while (parent != INVALID_ENGINE) {
				variants.push_back(parent);
				parent = Engine::Get(parent)->info.variant_id;
			}
		}

		for (const auto &variant : variants) {
			if (std::ranges::find(this->eng_list, variant, &GUIEngineListItem::engine_id) == this->eng_list.end()) {
				const Engine *e = Engine::Get(variant);
				this->eng_list.emplace_back(variant, e->info.variant_id, e->display_flags | EngineDisplayFlag::Shaded, 0);
			}
		}

		/* setup engine capacity cache */
		this->eng_list.SortParameterData().UpdateCargoFilter(this, this->cargo_filter_criteria);

		_engine_sort_direction = this->descending_sort_order;
		EngList_Sort(this->eng_list, _engine_sort_functions[this->vehicle_type][this->sort_criteria]);

		this->eng_list.swap(list);
		GUIEngineListAddChildren(this->eng_list, list, INVALID_ENGINE, 0);
		this->eng_list.RebuildDone();
	}

	void BuildVehicle()
	{
		EngineID sel_eng = this->sel_engine;
		if (sel_eng == INVALID_ENGINE) return;

		CargoType cargo = this->cargo_filter_criteria;
		if (cargo == CargoFilterCriteria::CF_ANY || cargo == CargoFilterCriteria::CF_ENGINES || cargo == CargoFilterCriteria::CF_NONE) cargo = INVALID_CARGO;
		if (this->virtual_train_mode) {
			Command<CMD_BUILD_VIRTUAL_RAIL_VEHICLE>::Post(GetCmdBuildVehMsg(VEH_TRAIN), CommandCallback::AddVirtualEngine, sel_eng, cargo, INVALID_CLIENT_ID, this->GetNewVirtualEngineMoveTarget());
		} else {
			CommandCallback callback = (this->vehicle_type == VEH_TRAIN && RailVehInfo(sel_eng)->railveh_type == RAILVEH_WAGON)
					? CommandCallback::BuildWagon : CommandCallback::BuildPrimaryVehicle;
			Command<CMD_BUILD_VEHICLE>::Post(GetCmdBuildVehMsg(this->vehicle_type), callback, TileIndex(this->window_number), sel_eng, true, cargo, INVALID_CLIENT_ID);
		}

		/* Update last used variant in hierarchy and refresh if necessary. */
		bool refresh = false;
		EngineID parent = sel_eng;
		while (parent != INVALID_ENGINE) {
			Engine *e = Engine::Get(parent);
			refresh |= (e->display_last_variant != sel_eng);
			e->display_last_variant = sel_eng;
			parent = e->info.variant_id;
		}
		if (refresh) {
			InvalidateWindowData(WC_REPLACE_VEHICLE, this->vehicle_type, 0); // Update the autoreplace window
			InvalidateWindowClassesData(WC_BUILD_VEHICLE); // The build windows needs updating as well
			InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->descending_sort_order ^= true;
				_engine_sort_last_order[this->vehicle_type] = this->descending_sort_order;
				this->eng_list.ForceRebuild();
				this->SetDirty();
				break;

			case WID_BV_SHOW_HIDDEN_ENGINES:
				this->show_hidden_engines ^= true;
				_engine_sort_show_hidden_engines[this->vehicle_type] = this->show_hidden_engines;
				this->eng_list.ForceRebuild();
				this->SetWidgetLoweredState(widget, this->show_hidden_engines);
				this->SetDirty();
				break;

			case WID_BV_LIST: {
				EngineID e = INVALID_ENGINE;
				const auto it = this->vscroll->GetScrolledItemFromWidget(this->eng_list, pt.y, this, WID_BV_LIST);
				if (it != this->eng_list.end()) {
					const auto &item = *it;
					const Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.matrix).WithWidth(WidgetDimensions::scaled.hsep_indent * (item.indent + 1), _current_text_dir == TD_RTL);
					if (item.flags.Test(EngineDisplayFlag::HasVariants) && IsInsideMM(r.left, r.right, pt.x)) {
						/* toggle folded flag on engine */
						assert(item.variant_id != INVALID_ENGINE);
						Engine *engine = Engine::Get(item.variant_id);
						engine->display_flags.Flip(EngineDisplayFlag::IsFolded);

						InvalidateWindowData(WC_REPLACE_VEHICLE, this->vehicle_type, 0); // Update the autoreplace window
						InvalidateWindowClassesData(WC_BUILD_VEHICLE); // The build windows needs updating as well
						InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN);
						return;
					}
					if (!item.flags.Test(EngineDisplayFlag::Shaded)) e = item.engine_id;
				}
				this->SelectEngine(e);
				this->SetDirty();
				if (_ctrl_pressed) {
					this->OnClick(pt, WID_BV_SHOW_HIDE, 1);
				} else if (click_count > 1 && !this->listview_mode) {
					this->OnClick(pt, WID_BV_BUILD, 1);
				}
				break;
			}

			case WID_BV_SORT_DROPDOWN: // Select sorting criteria dropdown menu
				DisplayVehicleSortDropDown(this, this->vehicle_type, this->sort_criteria, WID_BV_SORT_DROPDOWN);
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select cargo filtering criteria dropdown menu
				ShowDropDownList(this, this->BuildCargoDropDownList(), this->cargo_filter_criteria, widget);
				break;

			case WID_BV_SHOW_HIDE: {
				const Engine *e = (this->sel_engine == INVALID_ENGINE) ? nullptr : Engine::Get(this->sel_engine);
				if (e != nullptr) {
					Command<CMD_SET_VEHICLE_VISIBILITY>::Post(this->sel_engine, !e->IsHidden(_current_company));
				}
				break;
			}

			case WID_BV_BUILD:
				this->BuildVehicle();
				break;

			case WID_BV_RENAME: {
				EngineID sel_eng = this->sel_engine;
				if (sel_eng != INVALID_ENGINE) {
					this->rename_engine = sel_eng;
					ShowQueryString(GetString(STR_ENGINE_NAME, PackEngineNameDParam(sel_eng, EngineNameContext::Generic)), STR_QUERY_RENAME_TRAIN_TYPE_CAPTION + this->vehicle_type, MAX_LENGTH_ENGINE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				}
				break;
			}

			case WID_BV_TOGGLE_DUAL_PANE: {
				this->ChangeDualPaneMode(true);
				break;
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope) return;
		/* When switching to original acceleration model for road vehicles, clear the selected sort criteria if it is not available now. */
		if (this->vehicle_type == VEH_ROAD &&
				_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL &&
				this->sort_criteria > 7) {
			this->sort_criteria = 0;
			_engine_sort_last_criteria[VEH_ROAD] = 0;
		}
		this->eng_list.ForceRebuild();
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_BV_CAPTION:
				if (this->vehicle_type == VEH_TRAIN && !this->listview_mode && !this->virtual_train_mode) {
					const RailTypeInfo *rti = GetRailTypeInfo(this->filter.railtype);
					SetDParam(0, rti->strings.build_caption);
				} else if (this->vehicle_type == VEH_ROAD && !this->listview_mode) {
					const RoadTypeInfo *rti = GetRoadTypeInfo(this->filter.roadtype);
					SetDParam(0, rti->strings.build_caption);
				} else {
					SetDParam(0, (this->listview_mode ? STR_VEHICLE_LIST_AVAILABLE_TRAINS : STR_BUY_VEHICLE_TRAIN_ALL_CAPTION) + this->vehicle_type);
				}
				break;

			case WID_BV_SORT_DROPDOWN:
				SetDParam(0, std::data(_engine_sort_listing[this->vehicle_type])[this->sort_criteria]);
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN:
				SetDParam(0, this->GetCargoFilterLabel(this->cargo_filter_criteria));
				break;

			case WID_BV_SHOW_HIDE: {
				const Engine *e = (this->sel_engine == INVALID_ENGINE) ? nullptr : Engine::Get(this->sel_engine);
				if (e != nullptr && e->IsHidden(_local_company)) {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type);
				} else {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension &fill, [[maybe_unused]] Dimension &resize) override
	{
		switch (widget) {
			case WID_BV_LIST:
				resize.height = GetEngineListHeight(this->vehicle_type);
				size.height = 3 * resize.height;
				size.width = std::max(size.width, this->badge_classes.GetTotalColumnsWidth() + GetVehicleImageCellSize(this->vehicle_type, EIT_PURCHASE).extend_left + GetVehicleImageCellSize(this->vehicle_type, EIT_PURCHASE).extend_right + 165) + padding.width;
				break;

			case WID_BV_PANEL:
				size.height = GetCharacterHeight(FS_NORMAL) * this->details_height + padding.height;
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->GetString());
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN:
				size.width = std::max(size.width, GetDropDownListDimension(this->BuildCargoDropDownList()).width + padding.width);
				break;

			case WID_BV_BUILD:
				size = GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_BUY_VEHICLE_BUTTON + this->vehicle_type);
				size = maxdim(size, GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_BUY_REFIT_VEHICLE_BUTTON + this->vehicle_type));
				size.width += padding.width;
				size.height += padding.height;
				break;

			case WID_BV_SHOW_HIDE:
				size = GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				size = maxdim(size, GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type));
				size.width += padding.width;
				size.height += padding.height;
				break;
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_BV_LIST:
				DrawEngineList(
					this->vehicle_type,
					r,
					this->eng_list,
					*this->vscroll,
					this->sel_engine,
					false,
					DEFAULT_GROUP,
					this->badge_classes
				);
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->DrawSortButtonState(WID_BV_SORT_ASCENDING_DESCENDING, this->descending_sort_order ? SBS_DOWN : SBS_UP);
				break;
		}
	}

	void OnPaint() override
	{
		this->GenerateBuildList();
		this->vscroll->SetCount(this->eng_list.size());

		this->SetWidgetsDisabledState(this->sel_engine == INVALID_ENGINE, WID_BV_SHOW_HIDE, WID_BV_BUILD);

		/* Disable renaming engines in network games if you are not the server. */
		this->SetWidgetDisabledState(WID_BV_RENAME, this->sel_engine == INVALID_ENGINE || IsNonAdminNetworkClient());

		this->DrawWidgets();

		if (!this->IsShaded()) {
			int needed_height = this->details_height;
			/* Draw details panels. */
			if (this->sel_engine != INVALID_ENGINE) {
				const Rect r = this->GetWidget<NWidgetBase>(WID_BV_PANEL)->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
				int text_end = DrawVehiclePurchaseInfo(r.left, r.right, r.top, this->sel_engine, this->te);
				needed_height = std::max(needed_height, (text_end - r.top) / GetCharacterHeight(FS_NORMAL));
			}
			if (needed_height != this->details_height) { // Details window are not high enough, enlarge them.
				int resize = needed_height - this->details_height;
				this->details_height = needed_height;
				this->ReInit(0, resize * GetCharacterHeight(FS_NORMAL));
				return;
			}
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;

		Command<CMD_RENAME_ENGINE>::Post(STR_ERROR_CAN_T_RENAME_TRAIN_TYPE + this->vehicle_type, this->rename_engine, *str);
	}

	void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch (widget) {
			case WID_BV_SORT_DROPDOWN:
				if (this->sort_criteria != index) {
					this->sort_criteria = index;
					_engine_sort_last_criteria[this->vehicle_type] = this->sort_criteria;
					this->eng_list.ForceRebuild();
				}
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select a cargo filter criteria
				if (this->cargo_filter_criteria != index) {
					this->cargo_filter_criteria = index;
					_engine_sort_last_cargo_criteria[this->vehicle_type] = this->cargo_filter_criteria;
					/* deactivate filter if criteria is 'Show All', activate it otherwise */
					this->eng_list.SetFilterState(this->cargo_filter_criteria != CargoFilterCriteria::CF_ANY);
					this->eng_list.ForceRebuild();
					this->SelectEngine(this->sel_engine);
				}
				break;
		}
		this->SetDirty();
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_BV_LIST);
	}

	void OnEditboxChanged(WidgetID wid) override
	{
		if (wid == WID_BV_FILTER) {
			this->string_filter.SetFilterTerm(this->vehicle_editbox.text.GetText());
			this->InvalidateData();
		}
	}

	EventState OnHotkey(int hotkey) override
	{
		switch (hotkey) {
			case BVHK_FOCUS_FILTER_BOX:
				this->SetFocusedWidget(WID_BV_FILTER);
				SetFocusedWindow(this); // The user has asked to give focus to the text box, so make sure this window is focused.
				return ES_HANDLED;

			default:
				return ES_NOT_HANDLED;
		}

		return ES_HANDLED;
	}

	static HotkeyList hotkeys;
};

static Hotkey buildvehicle_hotkeys[] = {
	Hotkey('F', "focus_filter_box", BVHK_FOCUS_FILTER_BOX),
};
HotkeyList BuildVehicleWindow::hotkeys("buildvehicle", buildvehicle_hotkeys);

static EngList_SortTypeFunction * const  _sorter_loco[12] = {
	/* Locomotives */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EnginePowerSorter,
	&EngineTractiveEffortSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&EnginePowerVsRunningCostSorter,
	&EngineReliabilitySorter,
	&TrainEngineCapacitySorter,
	&TrainEngineCapacityVsRunningCostSorter
};

static EngList_SortTypeFunction * const _sorter_wagon[8] = {
	/* Wagons */
	&EngineNumberSorter,
	&EngineCostSorter,
	&EngineSpeedSorter,
	&EngineIntroDateSorter,
	&EngineNameSorter,
	&EngineRunningCostSorter,
	&TrainEngineCapacitySorter,
	&TrainEngineCapacityVsRunningCostSorter
};

static const StringID _sort_listing_loco[12] = {
	/* Locomotives */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_POWER,
	STR_SORT_BY_TRACTIVE_EFFORT,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_POWER_VS_RUNNING_COST,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
};

static const StringID _sort_listing_wagon[8] = {
	/* Wagons */
	STR_SORT_BY_ENGINE_ID,
	STR_SORT_BY_COST,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_INTRO_DATE,
	STR_SORT_BY_NAME,
	STR_SORT_BY_RUNNING_COST,
	STR_SORT_BY_CARGO_CAPACITY,
	STR_SORT_BY_CARGO_CAPACITY_VS_RUNNING_COST,
};

/**
 * Display the dropdown for the locomotive sort criteria.
 * @param w Parent window (holds the dropdown button).
 * @param selected Currently selected sort criterion.
 */
void DisplayLocomotiveSortDropDown(Window *w, int selected)
{
	uint32_t hidden_mask = 0;
	/* Disable sorting by tractive effort when the original acceleration model for trains is being used. */
	if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
		SetBit(hidden_mask, 4); // tractive effort
	}
	ShowDropDownMenu(w, _sort_listing_loco, selected, WID_BV_SORT_DROPDOWN_LOCO, 0, hidden_mask);
}

/**
 * Display the dropdown for the wagon sort criteria.
 * @param w Parent window (holds the dropdown button).
 * @param selected Currently selected sort criterion.
 */
void DisplayWagonSortDropDown(Window *w, int selected)
{
	uint32_t hidden_mask = 0;
	/* Disable sorting by maximum speed when wagon speed is disabled. */
	if (!_settings_game.vehicle.wagon_speed_limits) {
		SetBit(hidden_mask, 2); // maximum speed
	}
	ShowDropDownMenu(w, _sort_listing_wagon, selected, WID_BV_SORT_DROPDOWN_WAGON, 0, hidden_mask);
}

/** Advanced window for trains. It is divided into two parts, one for locomotives and one for wagons. */
struct BuildVehicleWindowTrainAdvanced final : BuildVehicleWindowBase {

	/* Locomotives and wagons */

	RailType railtype;                               ///< Filter to apply.

	struct PanelState {
		bool descending_sort_order; ///< Sort direction, @see _engine_sort_direction
		uint8_t sort_criteria;      ///< Current sort criterium.
		EngineID sel_engine;        ///< Currently selected engine, or #INVALID_ENGINE
		EngineID rename_engine {};  ///< Engine being renamed.
		GUIEngineList eng_list;
		Scrollbar *vscroll;
		CargoType cargo_filter_criteria;                 ///< Selected cargo filter
		bool show_hidden;                              ///< State of the 'show hidden' button.
		int details_height;                            ///< Minimal needed height of the details panels (found so far).
		TestedEngineDetails te;                        ///< Tested cost and capacity after refit.
		StringFilter string_filter;                    ///< Filter for vehicle name
		QueryString vehicle_editbox { MAX_LENGTH_VEHICLE_NAME_CHARS * MAX_CHAR_LENGTH, MAX_LENGTH_VEHICLE_NAME_CHARS }; ///< Filter editbox
	};

	PanelState loco {};
	PanelState wagon {};
	bool wagon_selected = false;
	bool dual_button_mode = false;
	GUIBadgeClasses badge_classes;

	bool GetRefitButtonMode(const PanelState &state) const
	{
		bool refit = state.sel_engine != INVALID_ENGINE && state.cargo_filter_criteria != CargoFilterCriteria::CF_ANY && state.cargo_filter_criteria != CargoFilterCriteria::CF_NONE && state.cargo_filter_criteria != CargoFilterCriteria::CF_ENGINES;
		if (refit) refit = Engine::Get(state.sel_engine)->GetDefaultCargoType() != state.cargo_filter_criteria;
		return refit;
	}

	void SetBuyLocomotiveText(int widget_id = WID_BV_BUILD_LOCO)
	{
		const auto widget = this->GetWidget<NWidgetCore>(widget_id);

		if (this->virtual_train_mode) {
			if (GetRefitButtonMode(this->loco)) {
				widget->SetStringTip(STR_TMPL_ADD_LOCOMOTIVE_REFIT, STR_TMPL_ADD_REFIT_TOOLTIP);
			} else {
				widget->SetStringTip(STR_TMPL_ADD_LOCOMOTIVE, STR_TMPL_ADD_TOOLTIP);
			}
		} else {
			if (GetRefitButtonMode(this->loco)) {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_REFIT_LOCOMOTIVE_BUTTON, STR_BUY_VEHICLE_TRAIN_BUY_REFIT_LOCOMOTIVE_TOOLTIP);
			} else {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_LOCOMOTIVE_BUTTON, STR_BUY_VEHICLE_TRAIN_BUY_LOCOMOTIVE_TOOLTIP);
			}
		}
	}

	void SetBuyWagonText(int widget_id = WID_BV_BUILD_WAGON)
	{
		const auto widget = this->GetWidget<NWidgetCore>(widget_id);

		if (this->virtual_train_mode) {
			if (GetRefitButtonMode(this->wagon)) {
				widget->SetStringTip(STR_TMPL_ADD_WAGON_REFIT, STR_TMPL_ADD_REFIT_TOOLTIP);
			} else {
				widget->SetStringTip(STR_TMPL_ADD_WAGON, STR_TMPL_ADD_TOOLTIP);
			}
		} else {
			if (GetRefitButtonMode(this->wagon)) {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_REFIT_WAGON_BUTTON, STR_BUY_VEHICLE_TRAIN_BUY_REFIT_WAGON_TOOLTIP);
			} else {
				widget->SetStringTip(STR_BUY_VEHICLE_TRAIN_BUY_WAGON_BUTTON, STR_BUY_VEHICLE_TRAIN_BUY_WAGON_TOOLTIP);
			}
		}
	}

	BuildVehicleWindowTrainAdvanced(WindowDesc &desc, TileIndex tile, Train **virtual_train_out) : BuildVehicleWindowBase(desc, tile, VEH_TRAIN, virtual_train_out)
	{
		this->loco.sel_engine             = INVALID_ENGINE;
		this->loco.sort_criteria          = _last_sort_criteria_loco;
		this->loco.descending_sort_order  = _last_sort_order_loco;
		this->loco.show_hidden            = _engine_sort_show_hidden_locos;

		this->wagon.sel_engine            = INVALID_ENGINE;
		this->wagon.sort_criteria         = _last_sort_criteria_wagon;
		this->wagon.descending_sort_order = _last_sort_order_wagon;
		this->wagon.show_hidden           = _engine_sort_show_hidden_wagons;

		this->railtype = (tile == INVALID_TILE) ? RAILTYPE_END : GetRailType(tile);

		this->UpdateFilterByTile();

		this->CreateNestedTree();

		this->loco.vscroll = this->GetScrollbar(WID_BV_SCROLLBAR_LOCO);
		this->wagon.vscroll = this->GetScrollbar(WID_BV_SCROLLBAR_WAGON);

		/* If we are just viewing the list of vehicles, we do not need the Build button.
		 * So we just hide it, and enlarge the Rename button by the now vacant place. */
		if (this->listview_mode) this->GetWidget<NWidgetStacked>(WID_BV_BUILD_SEL_LOCO)->SetDisplayedPlane(SZSP_NONE);
		if (this->listview_mode) this->GetWidget<NWidgetStacked>(WID_BV_BUILD_SEL_WAGON)->SetDisplayedPlane(SZSP_NONE);
		if (this->listview_mode) this->GetWidget<NWidgetStacked>(WID_BV_COMB_BUILD_SEL)->SetDisplayedPlane(SZSP_NONE);

		/* Locomotives */

		auto widget_loco = this->GetWidget<NWidgetCore>(WID_BV_LIST_LOCO);
		widget_loco->SetToolTip(STR_BUY_VEHICLE_TRAIN_LIST_TOOLTIP + VEH_TRAIN);

		widget_loco = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDE_LOCO);
		widget_loco->SetToolTip(STR_BUY_VEHICLE_TRAIN_HIDE_SHOW_TOGGLE_TOOLTIP + VEH_TRAIN);

		widget_loco = this->GetWidget<NWidgetCore>(WID_BV_RENAME_LOCO);
		widget_loco->SetStringTip(STR_BUY_VEHICLE_TRAIN_RENAME_LOCOMOTIVE_BUTTON, STR_BUY_VEHICLE_TRAIN_RENAME_LOCOMOTIVE_TOOLTIP);

		widget_loco = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDDEN_LOCOS);
		widget_loco->SetStringTip(STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN + VEH_TRAIN, STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN_TOOLTIP + VEH_TRAIN);
		widget_loco->SetLowered(this->loco.show_hidden);

		/* Wagons */

		auto widget_wagon = this->GetWidget<NWidgetCore>(WID_BV_LIST_WAGON);
		widget_wagon->SetToolTip(STR_BUY_VEHICLE_TRAIN_LIST_TOOLTIP + VEH_TRAIN);

		widget_wagon = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDE_WAGON);
		widget_wagon->SetToolTip(STR_BUY_VEHICLE_TRAIN_HIDE_SHOW_TOGGLE_TOOLTIP + VEH_TRAIN);

		widget_wagon = this->GetWidget<NWidgetCore>(WID_BV_RENAME_WAGON);
		widget_wagon->SetStringTip(STR_BUY_VEHICLE_TRAIN_RENAME_WAGON_BUTTON, STR_BUY_VEHICLE_TRAIN_RENAME_WAGON_TOOLTIP);

		widget_wagon = this->GetWidget<NWidgetCore>(WID_BV_SHOW_HIDDEN_WAGONS);
		widget_wagon->SetStringTip(STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN + VEH_TRAIN, STR_SHOW_HIDDEN_ENGINES_VEHICLE_TRAIN_TOOLTIP + VEH_TRAIN);
		widget_wagon->SetLowered(this->wagon.show_hidden);

		this->UpdateButtonMode();

		this->loco.details_height = this->wagon.details_height = 10 * GetCharacterHeight(FS_NORMAL) + WidgetDimensions::scaled.framerect.Vertical();

		this->FinishInitNested(this->window_number);

		this->querystrings[WID_BV_FILTER_LOCO] = &this->loco.vehicle_editbox;
		this->querystrings[WID_BV_FILTER_WAGON] = &this->wagon.vehicle_editbox;
		this->loco.vehicle_editbox.cancel_button = QueryString::ACTION_CLEAR;
		this->wagon.vehicle_editbox.cancel_button = QueryString::ACTION_CLEAR;

		this->owner = (tile != INVALID_TILE) ? GetTileOwner(tile) : _local_company;

		this->loco.eng_list.ForceRebuild();
		this->wagon.eng_list.ForceRebuild();

		this->GenerateBuildList(); // generate the list, since we need it in the next line

		/* Select the first engine in the list as default when opening the window */
		this->SelectFirstEngine(this->loco);
		this->SelectFirstEngine(this->wagon);

		this->SetBuyLocomotiveText();
		this->SetBuyWagonText();
		this->SelectColumn(false);
	}

	/** Set the filter type according to the depot type */
	void UpdateFilterByTile()
	{
		if (this->listview_mode || this->virtual_train_mode) {
			this->railtype = INVALID_RAILTYPE;
		} else {
			this->railtype = GetRailType(TileIndex(this->window_number));
		}
	}

	/** Populate the filter list and set the cargo filter criteria. */
	void SetCargoFilterArray(PanelState &state, const CargoType last_filter)
	{
		/* Set the last cargo filter criteria. */
		state.cargo_filter_criteria = last_filter;
		if (state.cargo_filter_criteria < NUM_CARGO && !HasBit(_standard_cargo_mask, state.cargo_filter_criteria)) state.cargo_filter_criteria = CargoFilterCriteria::CF_ANY;

		state.eng_list.SetFilterFuncs(_engine_filter_funcs);
		state.eng_list.SetFilterState(state.cargo_filter_criteria != CargoFilterCriteria::CF_ANY);
	}

	void SelectFirstEngine(PanelState &state)
	{
		EngineID engine = INVALID_ENGINE;
		auto it = std::find_if(state.eng_list.begin(), state.eng_list.end(), [&](GUIEngineListItem &item){ return !item.flags.Test(EngineDisplayFlag::Shaded); });
		if (it != state.eng_list.end()) engine = it->engine_id;
		this->SelectEngine(state, engine);
	}

	void SelectEngine(PanelState &state, const EngineID engine)
	{
		CargoType cargo = state.cargo_filter_criteria;
		if (cargo == CargoFilterCriteria::CF_ANY || cargo == CargoFilterCriteria::CF_ENGINES || cargo == CargoFilterCriteria::CF_NONE) cargo = INVALID_CARGO;

		state.sel_engine = engine;

		if (state.sel_engine == INVALID_ENGINE) return;

		this->FillTestedEngineCapacity(state.sel_engine, cargo, state.te);
	}

	void SelectColumn(bool wagon)
	{
		this->wagon_selected = wagon;
		if (wagon) {
			this->SetBuyWagonText(WID_BV_COMB_BUILD);
		} else {
			this->SetBuyLocomotiveText(WID_BV_COMB_BUILD);
		}

		NWidgetCore *rename = this->GetWidget<NWidgetCore>(WID_BV_COMB_RENAME);
		if (wagon) {
			rename->SetStringTip(STR_BUY_VEHICLE_TRAIN_RENAME_WAGON_BUTTON, STR_BUY_VEHICLE_TRAIN_RENAME_WAGON_TOOLTIP);
		} else {
			rename->SetStringTip(STR_BUY_VEHICLE_TRAIN_RENAME_LOCOMOTIVE_BUTTON, STR_BUY_VEHICLE_TRAIN_RENAME_LOCOMOTIVE_TOOLTIP);
		}
	}

	void UpdateButtonMode()
	{
		this->dual_button_mode = _settings_client.gui.dual_pane_train_purchase_window_dual_buttons;
		this->GetWidget<NWidgetStacked>(WID_BV_LOCO_BUTTONS_SEL)->SetDisplayedPlane(this->dual_button_mode ? 0 : SZSP_HORIZONTAL);
		this->GetWidget<NWidgetStacked>(WID_BV_WAGON_BUTTONS_SEL)->SetDisplayedPlane(this->dual_button_mode ? 0 : SZSP_HORIZONTAL);
		this->GetWidget<NWidgetStacked>(WID_BV_COMB_BUTTONS_SEL)->SetDisplayedPlane(this->dual_button_mode ? SZSP_HORIZONTAL : 0);
	}

	void OnInit() override
	{
		this->badge_classes = GUIBadgeClasses(GSF_TRAINS);

		this->SetCargoFilterArray(this->loco, _last_filter_criteria_loco);
		this->SetCargoFilterArray(this->wagon, _last_filter_criteria_wagon);

		this->loco.vscroll->SetCount(this->loco.eng_list.size());
		this->wagon.vscroll->SetCount(this->wagon.eng_list.size());
	}

	/* Filter a single engine */
	bool FilterSingleEngine(PanelState &state, EngineID eid)
	{
		GUIEngineListItem item = {eid, eid, EngineDisplayFlags{}, 0};
		return state.cargo_filter_criteria == CargoFilterCriteria::CF_ANY || CargoAndEngineFilter(&item, state.cargo_filter_criteria);
	}

	/** Filter by name and NewGRF extra text */
	bool FilterByText(PanelState &state, const Engine *e)
	{
		/* Do not filter if the filter text box is empty */
		if (state.string_filter.IsEmpty()) return true;

		/* Filter engine name */
		state.string_filter.ResetState();
		SetDParam(0, PackEngineNameDParam(e->index, EngineNameContext::PurchaseList));
		state.string_filter.AddLine(GetString(STR_ENGINE_NAME));

		/* Filter NewGRF extra text */
		auto text = GetNewGRFAdditionalText(e->index);
		if (text) state.string_filter.AddLine(*text);

		return state.string_filter.GetState();
	}

	/* Figure out what train EngineIDs to put in the list */
	void GenerateBuildTrainList(GUIEngineList &list, PanelState &state, const bool wagon, EngList_SortTypeFunction * const sorters[])
	{
		std::vector<EngineID> variants;
		EngineID sel_id = INVALID_ENGINE;

		list.clear();

		/* Make list of all available train engines and wagons.
		 * Also check to see if the previously selected engine is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when engines become obsolete and are removed */
		for (const Engine *engine : Engine::IterateType(VEH_TRAIN)) {
			if (!state.show_hidden && engine->IsVariantHidden(_local_company)) continue;
			EngineID eid = engine->index;
			const RailVehicleInfo *rvi = &engine->u.rail;

			if (this->railtype != RAILTYPE_END && !HasPowerOnRail(rvi->railtype, this->railtype)) continue;
			if (!IsEngineBuildable(eid, VEH_TRAIN, _local_company)) continue;

			if (!FilterSingleEngine(state, eid)) continue;

			const Engine *top_engine = engine;
			for (int depth = 0; depth < 16; depth++) {
				if (top_engine->info.variant_id == INVALID_ENGINE) break;
				top_engine = Engine::Get(top_engine->info.variant_id);
			}
			if ((top_engine->u.rail.railveh_type == RAILVEH_WAGON) != wagon) continue;

			/* Filter by name or NewGRF extra text */
			if (!FilterByText(state, engine)) continue;

			list.emplace_back(eid, engine->info.variant_id, engine->display_flags, 0);

			/* Add all parent variants of this engine to the variant list */
			EngineID parent = engine->info.variant_id;
			while (parent != INVALID_ENGINE) {
				variants.push_back(parent);
				parent = Engine::Get(parent)->info.variant_id;
			}

			if (eid == state.sel_engine) sel_id = eid;
		}

		/* ensure primary engine of variant group is in list */
		for (const auto &variant : variants) {
			if (std::ranges::find(list, variant, &GUIEngineListItem::engine_id) == list.end()) {
				const Engine *e = Engine::Get(variant);
				list.emplace_back(variant, e->info.variant_id, e->display_flags | EngineDisplayFlag::Shaded, 0);
			}
		}

		this->SelectEngine(state, sel_id);

		/* invalidate cached values for name sorter - engine names could change */
		_last_engine[0] = _last_engine[1] = INVALID_ENGINE;

		/* setup engine capacity cache */
		list.SortParameterData().UpdateCargoFilter(this, state.cargo_filter_criteria);

		/* Sort */
		_engine_sort_direction = state.descending_sort_order;
		EngList_Sort(list, sorters[state.sort_criteria]);
	}

	/* Generate the list of vehicles */
	void GenerateBuildList()
	{
		if (!this->loco.eng_list.NeedRebuild() && !this->wagon.eng_list.NeedRebuild()) return;

		/* Update filter type in case the rail type of the depot got converted */
		this->UpdateFilterByTile();

		this->railtype = (this->listview_mode || this->virtual_train_mode) ? RAILTYPE_END : GetRailType(TileIndex(this->window_number));

		this->loco.eng_list.clear();
		this->wagon.eng_list.clear();

		GUIEngineList list;

		this->GenerateBuildTrainList(list, this->loco, false, _sorter_loco);
		GUIEngineListAddChildren(this->loco.eng_list, list, INVALID_ENGINE, 0);

		this->GenerateBuildTrainList(list, this->wagon, true, _sorter_wagon);
		GUIEngineListAddChildren(this->wagon.eng_list, list, INVALID_ENGINE, 0);

		this->loco.eng_list.shrink_to_fit();
		this->loco.eng_list.RebuildDone();

		this->wagon.eng_list.shrink_to_fit();
		this->wagon.eng_list.RebuildDone();
	}

	void BuildEngine(const EngineID selected, CargoType cargo)
	{
		if (selected != INVALID_ENGINE) {
			if (cargo == CargoFilterCriteria::CF_ANY || cargo == CargoFilterCriteria::CF_ENGINES || cargo == CargoFilterCriteria::CF_NONE) cargo = INVALID_CARGO;
			if (this->virtual_train_mode) {
				Command<CMD_BUILD_VIRTUAL_RAIL_VEHICLE>::Post(GetCmdBuildVehMsg(VEH_TRAIN), CommandCallback::AddVirtualEngine, selected, cargo, INVALID_CLIENT_ID, this->GetNewVirtualEngineMoveTarget());
			} else {
				CommandCallback callback = (this->vehicle_type == VEH_TRAIN && RailVehInfo(selected)->railveh_type == RAILVEH_WAGON)
						? CommandCallback::BuildWagon : CommandCallback::BuildPrimaryVehicle;
				Command<CMD_BUILD_VEHICLE>::Post(GetCmdBuildVehMsg(this->vehicle_type), callback, TileIndex(this->window_number), selected, true, cargo, INVALID_CLIENT_ID);
			}

			/* Update last used variant in hierarchy and refresh if necessary. */
			bool refresh = false;
			EngineID parent = selected;
			while (parent != INVALID_ENGINE) {
				Engine *e = Engine::Get(parent);
				refresh |= (e->display_last_variant != selected);
				e->display_last_variant = selected;
				parent = e->info.variant_id;
			}
			if (refresh) {
				InvalidateWindowData(WC_REPLACE_VEHICLE, this->vehicle_type, 0); // Update the autoreplace window
				InvalidateWindowClassesData(WC_BUILD_VEHICLE); // The build windows needs updating as well
				InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN);
				return;
			}
		}
	}

	bool OnClickList(Point pt, WidgetID widget, PanelState &state, bool column)
	{
		const uint i = state.vscroll->GetScrolledRowFromWidget(pt.y, this, widget);
		const size_t num_items = state.eng_list.size();
		EngineID e = INVALID_ENGINE;
		if (i < num_items) {
			const auto &item = state.eng_list[i];
			const Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect().Shrink(WidgetDimensions::scaled.matrix).WithWidth(WidgetDimensions::scaled.hsep_indent * (item.indent + 1), _current_text_dir == TD_RTL);
			if (item.flags.Test(EngineDisplayFlag::HasVariants) && IsInsideMM(r.left, r.right, pt.x)) {
				/* toggle folded flag on engine */
				assert(item.variant_id != INVALID_ENGINE);
				Engine *engine = Engine::Get(item.variant_id);
				engine->display_flags.Flip(EngineDisplayFlag::IsFolded);

				InvalidateWindowData(WC_REPLACE_VEHICLE, this->vehicle_type, 0); // Update the autoreplace window
				InvalidateWindowClassesData(WC_BUILD_VEHICLE); // The build windows needs updating as well
				InvalidateWindowClassesData(WC_BUILD_VIRTUAL_TRAIN);
				return true;
			}
			if (!item.flags.Test(EngineDisplayFlag::Shaded)) e = item.engine_id;
		}
		this->SelectEngine(state, e);
		this->SelectColumn(column);
		this->SetDirty();
		return false;
	}

	void OnClick(Point pt, WidgetID widget, int click_count) override
	{
		if (widget == WID_BV_COMB_BUILD) {
			widget = !this->wagon_selected ? WID_BV_BUILD_LOCO : WID_BV_BUILD_WAGON;
		} else if (widget == WID_BV_COMB_SHOW_HIDE) {
			widget = !this->wagon_selected ? WID_BV_SHOW_HIDE_LOCO : WID_BV_SHOW_HIDE_WAGON;
		} else if (widget == WID_BV_COMB_RENAME) {
			widget = !this->wagon_selected ? WID_BV_RENAME_LOCO : WID_BV_RENAME_WAGON;
		}

		switch (widget) {
			case WID_BV_TOGGLE_DUAL_PANE: {
				this->ChangeDualPaneMode(false);
				break;
			}

			/* Locomotives */

			case WID_BV_SORT_ASCENDING_DESCENDING_LOCO: {
				this->loco.descending_sort_order ^= true;
				_last_sort_order_loco = this->loco.descending_sort_order;
				this->loco.eng_list.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_BV_SHOW_HIDDEN_LOCOS: {
				this->loco.show_hidden ^= true;
				_engine_sort_show_hidden_locos = this->loco.show_hidden;
				this->loco.eng_list.ForceRebuild();
				this->SetWidgetLoweredState(widget, this->loco.show_hidden);
				this->SetDirty();
				break;
			}

			case WID_BV_LIST_LOCO: {
				if (this->OnClickList(pt, widget, this->loco, false)) return;

				if (_ctrl_pressed) {
					this->OnClick(pt, WID_BV_SHOW_HIDE_LOCO, 1);
				} else if (click_count > 1 && !this->listview_mode) {
					this->OnClick(pt, WID_BV_BUILD_LOCO, 1);
				}
				break;
			}

			case WID_BV_SORT_DROPDOWN_LOCO: {
				DisplayLocomotiveSortDropDown(this, this->loco.sort_criteria);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_LOCO: { // Select cargo filtering criteria dropdown menu
				ShowDropDownList(this, this->BuildCargoDropDownList(true), this->loco.cargo_filter_criteria, widget);
				break;
			}

			case WID_BV_SHOW_HIDE_LOCO: {
				const Engine *engine = (this->loco.sel_engine == INVALID_ENGINE) ? nullptr : Engine::GetIfValid(this->loco.sel_engine);
				if (engine != nullptr) {
					Command<CMD_SET_VEHICLE_VISIBILITY>::Post(this->loco.sel_engine, !engine->IsHidden(_current_company));
				}
				break;
			}

			case WID_BV_BUILD_LOCO: {
				this->BuildEngine(this->loco.sel_engine, this->loco.cargo_filter_criteria);
				break;
			}

			case WID_BV_RENAME_LOCO: {
				const EngineID selected_loco = this->loco.sel_engine;
				if (selected_loco != INVALID_ENGINE) {
					this->loco.rename_engine = selected_loco;
					this->wagon.rename_engine = INVALID_ENGINE;
					std::string str = GetString(STR_ENGINE_NAME, PackEngineNameDParam(selected_loco, EngineNameContext::Generic));
					ShowQueryString(str, STR_QUERY_RENAME_TRAIN_TYPE_LOCOMOTIVE_CAPTION + this->vehicle_type, MAX_LENGTH_ENGINE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				}
				break;
			}

			/* Wagons */

			case WID_BV_SORT_ASCENDING_DESCENDING_WAGON: {
				this->wagon.descending_sort_order ^= true;
				_last_sort_order_wagon = this->wagon.descending_sort_order;
				this->wagon.eng_list.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_BV_SHOW_HIDDEN_WAGONS: {
				this->wagon.show_hidden ^= true;
				_engine_sort_show_hidden_wagons = this->wagon.show_hidden;
				this->wagon.eng_list.ForceRebuild();
				this->SetWidgetLoweredState(widget, this->wagon.show_hidden);
				this->SetDirty();
				break;
			}

			case WID_BV_LIST_WAGON: {
				if (this->OnClickList(pt, widget, this->wagon, true)) return;

				if (_ctrl_pressed) {
					this->OnClick(pt, WID_BV_SHOW_HIDE_WAGON, 1);
				} else if (click_count > 1 && !this->listview_mode) {
					this->OnClick(pt, WID_BV_BUILD_WAGON, 1);
				}
				break;
			}

			case WID_BV_SORT_DROPDOWN_WAGON: {
				DisplayWagonSortDropDown(this, this->wagon.sort_criteria);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_WAGON: { // Select cargo filtering criteria dropdown menu
				ShowDropDownList(this, this->BuildCargoDropDownList(true), this->wagon.cargo_filter_criteria, widget);
				break;
			}

			case WID_BV_SHOW_HIDE_WAGON: {
				const Engine *engine = (this->wagon.sel_engine == INVALID_ENGINE) ? nullptr : Engine::GetIfValid(this->wagon.sel_engine);
				if (engine != nullptr) {
					Command<CMD_SET_VEHICLE_VISIBILITY>::Post(this->wagon.sel_engine, !engine->IsHidden(_current_company));
				}
				break;
			}

			case WID_BV_BUILD_WAGON: {
				this->BuildEngine(this->wagon.sel_engine, this->wagon.cargo_filter_criteria);
				break;
			}

			case WID_BV_RENAME_WAGON: {
				const EngineID selected_wagon = this->wagon.sel_engine;
				if (selected_wagon != INVALID_ENGINE) {
					this->loco.rename_engine = INVALID_ENGINE;
					this->wagon.rename_engine = selected_wagon;
					std::string str = GetString(STR_ENGINE_NAME, PackEngineNameDParam(selected_wagon, EngineNameContext::Generic));
					ShowQueryString(str, STR_QUERY_RENAME_TRAIN_TYPE_WAGON_CAPTION + this->vehicle_type, MAX_LENGTH_ENGINE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				}
				break;
			}
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		/* When switching to original acceleration model for road vehicles, clear the selected sort criteria if it is not available now. */
		this->loco.eng_list.ForceRebuild();
		this->wagon.eng_list.ForceRebuild();

		if (this->dual_button_mode != _settings_client.gui.dual_pane_train_purchase_window_dual_buttons) {
			this->UpdateButtonMode();
			this->ReInit();
		}
	}

	void SetStringParameters(WidgetID widget) const override
	{
		switch (widget) {
			case WID_BV_CAPTION: {
				if (!this->listview_mode && !this->virtual_train_mode) {
					const RailTypeInfo *rti = GetRailTypeInfo(this->railtype);
					SetDParam(0, rti->strings.build_caption);
				} else {
					SetDParam(0, (this->listview_mode ? STR_VEHICLE_LIST_AVAILABLE_TRAINS : STR_BUY_VEHICLE_TRAIN_ALL_CAPTION) + this->vehicle_type);
				}
				break;
			}

			case WID_BV_CAPTION_LOCO: {
				SetDParam(0, STR_BUY_VEHICLE_TRAIN_LOCOMOTIVES);
				break;
			}

			case WID_BV_SHOW_HIDE_LOCO: {
				const Engine *engine = (this->loco.sel_engine == INVALID_ENGINE) ? nullptr : Engine::GetIfValid(this->loco.sel_engine);
				if (engine != nullptr && engine->IsHidden(_local_company)) {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type);
				} else {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				}
				break;
			}

			case WID_BV_CAPTION_WAGON: {
				SetDParam(0, STR_BUY_VEHICLE_TRAIN_WAGONS);
				break;
			}

			case WID_BV_SORT_DROPDOWN_LOCO: {
				SetDParam(0, _sort_listing_loco[this->loco.sort_criteria]);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_LOCO: {
				SetDParam(0, this->GetCargoFilterLabel(this->loco.cargo_filter_criteria));
				break;
			}

			case WID_BV_SORT_DROPDOWN_WAGON: {
				SetDParam(0, _sort_listing_wagon[this->wagon.sort_criteria]);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_WAGON: {
				SetDParam(0, this->GetCargoFilterLabel(this->wagon.cargo_filter_criteria));
				break;
			}

			case WID_BV_SHOW_HIDE_WAGON: {
				const Engine *engine = (this->wagon.sel_engine == INVALID_ENGINE) ? nullptr : Engine::GetIfValid(this->wagon.sel_engine);
				if (engine != nullptr && engine->IsHidden(_local_company)) {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type);
				} else {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				}
				break;
			}

			case WID_BV_COMB_SHOW_HIDE: {
				const PanelState &state = this->wagon_selected ? this->wagon : this->loco;
				const Engine *engine = (state.sel_engine == INVALID_ENGINE) ? nullptr : Engine::GetIfValid(state.sel_engine);
				if (engine != nullptr && engine->IsHidden(_local_company)) {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type);
				} else {
					SetDParam(0, STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				}
				break;
			}
		}
	}

	void UpdateWidgetSize(WidgetID widget, Dimension &size, const Dimension &padding, Dimension &fill, Dimension &resize) override
	{
		switch (widget) {
			case WID_BV_LIST_LOCO: {
				resize.height = GetEngineListHeight(this->vehicle_type);
				size.height = 3 * resize.height;
				break;
			}

			case WID_BV_PANEL_LOCO: {
				size.height = this->loco.details_height;
				break;
			}

			case WID_BV_SORT_ASCENDING_DESCENDING_LOCO: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->GetString());
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}

			case WID_BV_LIST_WAGON: {
				resize.height = GetEngineListHeight(this->vehicle_type);
				size.height = 3 * resize.height;
				break;
			}

			case WID_BV_PANEL_WAGON: {
				size.height = this->wagon.details_height;
				break;
			}

			case WID_BV_SORT_ASCENDING_DESCENDING_WAGON: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->GetString());
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				size = maxdim(size, d);
				break;
			}

			case WID_BV_SHOW_HIDE_LOCO: // Fallthrough
			case WID_BV_SHOW_HIDE_WAGON:
			case WID_BV_COMB_SHOW_HIDE: {
				size = GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_HIDE_TOGGLE_BUTTON + this->vehicle_type);
				size = maxdim(size, GetStringBoundingBox(STR_BUY_VEHICLE_TRAIN_SHOW_TOGGLE_BUTTON + this->vehicle_type));
				size.width += padding.width;
				size.height += padding.height;
				break;
			}

			case WID_BV_RENAME_LOCO: {
				size = maxdim(size, NWidgetLeaf::GetResizeBoxDimension());
				break;
			}
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		switch (widget) {
			case WID_BV_LIST_LOCO: {
				DrawEngineList(this->vehicle_type, r,
					this->loco.eng_list, *(this->loco.vscroll), this->loco.sel_engine, false,
					DEFAULT_GROUP, this->badge_classes);
				break;
			}

			case WID_BV_SORT_ASCENDING_DESCENDING_LOCO: {
				this->DrawSortButtonState(WID_BV_SORT_ASCENDING_DESCENDING_LOCO, this->loco.descending_sort_order ? SBS_DOWN : SBS_UP);
				break;
			}

			case WID_BV_LIST_WAGON: {
				DrawEngineList(this->vehicle_type, r,
					this->wagon.eng_list, *(this->wagon.vscroll), this->wagon.sel_engine, false,
					DEFAULT_GROUP, this->badge_classes);
				break;
			}

			case WID_BV_SORT_ASCENDING_DESCENDING_WAGON: {
				this->DrawSortButtonState(WID_BV_SORT_ASCENDING_DESCENDING_WAGON, this->wagon.descending_sort_order ? SBS_DOWN : SBS_UP);
				break;
			}
		}
	}

	bool DrawDetailsPanel(PanelState &state, int widget_id)
	{
		int needed_height = state.details_height;
		/* Draw details panels. */
		if (state.sel_engine != INVALID_ENGINE) {
			const auto widget = this->GetWidget<NWidgetBase>(widget_id);
			const int text_end = DrawVehiclePurchaseInfo(widget->pos_x + WidgetDimensions::scaled.framerect.left,
				static_cast<int>(
					widget->pos_x + widget->current_x -
					WidgetDimensions::scaled.framerect.right), widget->pos_y + WidgetDimensions::scaled.framerect.top,
				state.sel_engine, state.te);
			needed_height = std::max(needed_height, text_end - widget->pos_y + WidgetDimensions::scaled.framerect.bottom);
		}
		if (needed_height != state.details_height) { // Details window are not high enough, enlarge them.
			const int resize = needed_height - state.details_height;
			state.details_height = needed_height;
			this->ReInit(0, resize);
			return true;
		}
		return false;
	}

	void OnPaint() override
	{
		this->GenerateBuildList();
		this->SetBuyLocomotiveText();
		this->SetBuyWagonText();

		this->loco.vscroll->SetCount(this->loco.eng_list.size());
		this->wagon.vscroll->SetCount(this->wagon.eng_list.size());

		this->SetWidgetDisabledState(WID_BV_SHOW_HIDE_LOCO, this->loco.sel_engine == INVALID_ENGINE);
		this->SetWidgetDisabledState(WID_BV_SHOW_HIDE_WAGON, this->wagon.sel_engine == INVALID_ENGINE);

		/* disable renaming engines in network games if you are not the server */
		this->SetWidgetDisabledState(WID_BV_RENAME_LOCO, (this->loco.sel_engine == INVALID_ENGINE) || (_networking && !_network_server));
		this->SetWidgetDisabledState(WID_BV_BUILD_LOCO, this->loco.sel_engine == INVALID_ENGINE);

		/* disable renaming engines in network games if you are not the server */
		this->SetWidgetDisabledState(WID_BV_RENAME_WAGON, (this->wagon.sel_engine == INVALID_ENGINE) || (_networking && !_network_server));
		this->SetWidgetDisabledState(WID_BV_BUILD_WAGON, this->wagon.sel_engine == INVALID_ENGINE);

		this->DrawWidgets();

		if (!this->IsShaded()) {
			if (this->DrawDetailsPanel(this->loco, WID_BV_PANEL_LOCO)) return;
			if (this->DrawDetailsPanel(this->wagon, WID_BV_PANEL_WAGON)) return;
		}
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;

		if (this->loco.rename_engine != INVALID_ENGINE) {
			Command<CMD_RENAME_ENGINE>::Post(STR_ERROR_CAN_T_RENAME_TRAIN_TYPE + this->vehicle_type, this->loco.rename_engine, *str);
		} else {
			Command<CMD_RENAME_ENGINE>::Post(STR_ERROR_CAN_T_RENAME_TRAIN_TYPE + this->vehicle_type, this->wagon.rename_engine, *str);
		}
	}

	void OnDropdownSelect(WidgetID widget, int index) override
	{
		switch (widget) {
			case WID_BV_SORT_DROPDOWN_LOCO: {
				if (this->loco.sort_criteria != index) {
					this->loco.sort_criteria = static_cast<uint8_t>(index);
					_last_sort_criteria_loco = this->loco.sort_criteria;
					this->loco.eng_list.ForceRebuild();
				}
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_LOCO: { // Select a cargo filter criteria
				if (this->loco.cargo_filter_criteria != index) {
					this->loco.cargo_filter_criteria = static_cast<uint8_t>(index);
					_last_filter_criteria_loco = this->loco.cargo_filter_criteria;
					/* deactivate filter if criteria is 'Show All', activate it otherwise */
					this->loco.eng_list.SetFilterState(this->loco.cargo_filter_criteria != CargoFilterCriteria::CF_ANY);
					this->loco.eng_list.ForceRebuild();
				}
				break;
			}

			case WID_BV_SORT_DROPDOWN_WAGON: {
				if (this->wagon.sort_criteria != index) {
					this->wagon.sort_criteria = static_cast<uint8_t>(index);
					_last_sort_criteria_wagon = this->wagon.sort_criteria;
					this->wagon.eng_list.ForceRebuild();
				}
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN_WAGON: { // Select a cargo filter criteria
				if (this->wagon.cargo_filter_criteria != index) {
					this->wagon.cargo_filter_criteria = static_cast<uint8_t>(index);
					_last_filter_criteria_wagon = this->wagon.cargo_filter_criteria;
					/* deactivate filter if criteria is 'Show All', activate it otherwise */
					this->wagon.eng_list.SetFilterState(this->wagon.cargo_filter_criteria != CargoFilterCriteria::CF_ANY);
					this->wagon.eng_list.ForceRebuild();
				}
				break;
			}
		}

		this->SetDirty();
	}

	void OnResize() override
	{
		this->loco.vscroll->SetCapacityFromWidget(this, WID_BV_LIST_LOCO);
		this->wagon.vscroll->SetCapacityFromWidget(this, WID_BV_LIST_WAGON);
	}

	void OnEditboxChanged(WidgetID wid) override
	{
		if (wid == WID_BV_FILTER_LOCO) {
			this->loco.string_filter.SetFilterTerm(this->loco.vehicle_editbox.text.GetText());
			this->loco.eng_list.ForceRebuild();
			this->SetDirty();
		}
		if (wid == WID_BV_FILTER_WAGON) {
			this->wagon.string_filter.SetFilterTerm(this->wagon.vehicle_editbox.text.GetText());
			this->wagon.eng_list.ForceRebuild();
			this->SetDirty();
		}
	}

	EventState OnHotkey(int hotkey) override
	{
		switch (hotkey) {
			case BVHK_FOCUS_FILTER_BOX:
				this->SetFocusedWidget(this->wagon_selected ? WID_BV_FILTER_WAGON : WID_BV_FILTER_LOCO);
				SetFocusedWindow(this); // The user has asked to give focus to the text box, so make sure this window is focused.
				return ES_HANDLED;

			default:
				return ES_NOT_HANDLED;
		}

		return ES_HANDLED;
	}
};

void CcAddVirtualEngine(const CommandCost &result)
{
	if (result.Failed() || !result.HasResultData()) return;

	Window *window = FindWindowById(WC_BUILD_VIRTUAL_TRAIN, 0);

	if (window != nullptr) {
		Train *train = Train::Get(result.GetResultData());
		dynamic_cast<BuildVehicleWindowBase *>(window)->AddVirtualEngine(train);
	} else {
		Command<CMD_SELL_VIRTUAL_VEHICLE>::Post(result.GetResultData(), SellVehicleFlags::None, INVALID_CLIENT_ID);
	}
}

void CcMoveNewVirtualEngine(const CommandCost &result)
{
	if (result.Failed()) return;

	InvalidateWindowClassesData(WC_CREATE_TEMPLATE);
}

static WindowDesc _build_vehicle_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_vehicle", 240, 268,
	WC_BUILD_VEHICLE, WC_NONE,
	WindowDefaultFlag::Construction,
	_nested_build_vehicle_widgets,
	&BuildVehicleWindow::hotkeys
);

static WindowDesc _build_template_vehicle_desc(__FILE__, __LINE__,
	WDP_AUTO, "build_template_vehicle", 240, 268,
	WC_BUILD_VIRTUAL_TRAIN, WC_CREATE_TEMPLATE,
	WindowDefaultFlag::Construction,
	_nested_build_vehicle_widgets,
	&BuildVehicleWindow::hotkeys, &_build_vehicle_desc
);

static WindowDesc _build_vehicle_desc_train_advanced(__FILE__, __LINE__,
	WDP_AUTO, "build_vehicle_dual", 480, 268,
	WC_BUILD_VEHICLE, WC_NONE,
	WindowDefaultFlag::Construction,
	_nested_build_vehicle_widgets_train_advanced,
	&BuildVehicleWindow::hotkeys
);

static WindowDesc _build_template_vehicle_desc_advanced(__FILE__, __LINE__,
	WDP_AUTO, "build_template_vehicle_dual", 480, 268,
	WC_BUILD_VIRTUAL_TRAIN, WC_CREATE_TEMPLATE,
	WindowDefaultFlag::Construction,
	_nested_build_vehicle_widgets_train_advanced,
	&BuildVehicleWindow::hotkeys, &_build_vehicle_desc_train_advanced
);


void ShowBuildVehicleWindow(const TileIndex tile, const VehicleType type)
{
	/* We want to be able to open both Available Train as Available Ships,
	 *  so if tile == INVALID_TILE (Available XXX Window), use 'type' as unique number.
	 *  As it always is a low value, it won't collide with any real tile
	 *  number. */
	const uint num = (tile == INVALID_TILE) ? static_cast<uint>(type) : tile.base();

	assert(IsCompanyBuildableVehicleType(type));

	CloseWindowById(WC_BUILD_VEHICLE, num);

	if (type == VEH_TRAIN && _settings_client.gui.dual_pane_train_purchase_window) {
		new BuildVehicleWindowTrainAdvanced(_build_vehicle_desc_train_advanced, tile, nullptr);
	} else {
		new BuildVehicleWindow(_build_vehicle_desc, tile, type, nullptr);
	}
}

void ShowTemplateTrainBuildVehicleWindow(Train **virtual_train)
{
	assert(IsCompanyBuildableVehicleType(VEH_TRAIN));

	CloseWindowById(WC_BUILD_VIRTUAL_TRAIN, 0);

	if (_settings_client.gui.dual_pane_train_purchase_window) {
		new BuildVehicleWindowTrainAdvanced(_build_template_vehicle_desc_advanced, INVALID_TILE, virtual_train);
	} else {
		new BuildVehicleWindow(_build_template_vehicle_desc, INVALID_TILE, VEH_TRAIN, virtual_train);
	}
}
