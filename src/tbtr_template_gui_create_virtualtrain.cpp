/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_gui_create_virtualtrain.cpp Template-based train replacement: template creation vehicle build GUI. */

#include "stdafx.h"
#include "engine_base.h"
#include "engine_func.h"
#include "station_base.h"
#include "articulated_vehicles.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "company_func.h"
#include "vehicle_gui.h"
#include "newgrf_engine.h"
#include "newgrf_text.h"
#include "group.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "widgets/dropdown_func.h"
#include "engine_gui.h"
#include "cargotype.h"
#include "core/geometry_func.hpp"
#include "vehicle_gui.h"
#include "tbtr_template_gui_create_virtualtrain.h"

#include "widgets/build_vehicle_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static const NWidgetPart _nested_build_vehicle_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_BV_CAPTION), SetDataTip(STR_WHITE_STRING, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_SORT_ASCENDING_DESCENDING), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER), SetFill(1, 0),
				NWidget(NWID_SPACER), SetFill(1, 1),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_SORT_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_BV_CARGO_FILTER_DROPDOWN), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Vehicle list. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_BV_LIST), SetResize(1, 1), SetFill(1, 0), SetDataTip(0x101, STR_NULL), SetScrollbar(WID_BV_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_BV_SCROLLBAR),
	EndContainer(),
	/* Panel with details. */
	NWidget(WWT_PANEL, COLOUR_GREY, WID_BV_PANEL), SetMinimalSize(240, 122), SetResize(1, 0), EndContainer(),
	/* Build/rename buttons, resize button. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_BUILD), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_TMPL_CONFIRM, STR_TMPL_CONFIRM),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_BV_RENAME), SetResize(1, 0), SetFill(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/** Special cargo filter criteria */
static const CargoID CF_ANY  = CT_NO_REFIT; ///< Show all vehicles independent of carried cargo (i.e. no filtering)
static const CargoID CF_NONE = CT_INVALID;  ///< Show only vehicles which do not carry cargo (e.g. train engines)

static bool _internal_sort_order;           ///< false = descending, true = ascending
static byte _last_sort_criteria[]      = {0, 0, 0, 0};
static bool _last_sort_order[]         = {false, false, false, false};
static CargoID _last_filter_criteria[] = {CF_ANY, CF_ANY, CF_ANY, CF_ANY};

/**
 * Determines order of engines by engineID
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineNumberSorter(const EngineID *a, const EngineID *b)
{
	int r = Engine::Get(*a)->list_position - Engine::Get(*b)->list_position;

	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by introduction date
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineIntroDateSorter(const EngineID *a, const EngineID *b)
{
	const int va = Engine::Get(*a)->intro_date;
	const int vb = Engine::Get(*b)->intro_date;
	const int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by name
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineNameSorter(const EngineID *a, const EngineID *b)
{
	static EngineID last_engine[2] = { INVALID_ENGINE, INVALID_ENGINE };
	static char     last_name[2][64] = { "\0", "\0" };

	const EngineID va = *a;
	const EngineID vb = *b;

	if (va != last_engine[0]) {
		last_engine[0] = va;
		SetDParam(0, va);
		GetString(last_name[0], STR_ENGINE_NAME, lastof(last_name[0]));
	}

	if (vb != last_engine[1]) {
		last_engine[1] = vb;
		SetDParam(0, vb);
		GetString(last_name[1], STR_ENGINE_NAME, lastof(last_name[1]));
	}

	int r = strnatcmp(last_name[0], last_name[1]); // Sort by name (natural sorting).

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by reliability
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineReliabilitySorter(const EngineID *a, const EngineID *b)
{
	const int va = Engine::Get(*a)->reliability;
	const int vb = Engine::Get(*b)->reliability;
	const int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by purchase cost
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineCostSorter(const EngineID *a, const EngineID *b)
{
	Money va = Engine::Get(*a)->GetCost();
	Money vb = Engine::Get(*b)->GetCost();
	int r = ClampToI32(va - vb);

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by speed
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineSpeedSorter(const EngineID *a, const EngineID *b)
{
	int va = Engine::Get(*a)->GetDisplayMaxSpeed();
	int vb = Engine::Get(*b)->GetDisplayMaxSpeed();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by power
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EnginePowerSorter(const EngineID *a, const EngineID *b)
{
	int va = Engine::Get(*a)->GetPower();
	int vb = Engine::Get(*b)->GetPower();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by tractive effort
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineTractiveEffortSorter(const EngineID *a, const EngineID *b)
{
	int va = Engine::Get(*a)->GetDisplayMaxTractiveEffort();
	int vb = Engine::Get(*b)->GetDisplayMaxTractiveEffort();
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by running costs
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EngineRunningCostSorter(const EngineID *a, const EngineID *b)
{
	Money va = Engine::Get(*a)->GetRunningCost();
	Money vb = Engine::Get(*b)->GetRunningCost();
	int r = ClampToI32(va - vb);

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of engines by running costs
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL EnginePowerVsRunningCostSorter(const EngineID *a, const EngineID *b)
{
	const Engine *e_a = Engine::Get(*a);
	const Engine *e_b = Engine::Get(*b);

	/* Here we are using a few tricks to get the right sort.
	 * We want power/running cost, but since we usually got higher running cost than power and we store the result in an int,
	 * we will actually calculate cunning cost/power (to make it more than 1).
	 * Because of this, the return value have to be reversed as well and we return b - a instead of a - b.
	 * Another thing is that both power and running costs should be doubled for multiheaded engines.
	 * Since it would be multipling with 2 in both numerator and denumerator, it will even themselves out and we skip checking for multiheaded. */
	Money va = (e_a->GetRunningCost()) / max(1U, (uint)e_a->GetPower());
	Money vb = (e_b->GetRunningCost()) / max(1U, (uint)e_b->GetPower());
	int r = ClampToI32(vb - va);

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/* Train sorting functions */

/**
 * Determines order of train engines by capacity
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL TrainEngineCapacitySorter(const EngineID *a, const EngineID *b)
{
	const RailVehicleInfo *rvi_a = RailVehInfo(*a);
	const RailVehicleInfo *rvi_b = RailVehInfo(*b);

	int va = GetTotalCapacityOfArticulatedParts(*a) * (rvi_a->railveh_type == RAILVEH_MULTIHEAD ? 2 : 1);
	int vb = GetTotalCapacityOfArticulatedParts(*b) * (rvi_b->railveh_type == RAILVEH_MULTIHEAD ? 2 : 1);
	int r = va - vb;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/**
 * Determines order of train engines by engine / wagon
 * @param *a first engine to compare
 * @param *b second engine to compare
 * @return for descending order: returns < 0 if a < b and > 0 for a > b. Vice versa for ascending order and 0 for equal
 */
static int CDECL TrainEnginesThenWagonsSorter(const EngineID *a, const EngineID *b)
{
	int val_a = (RailVehInfo(*a)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int val_b = (RailVehInfo(*b)->railveh_type == RAILVEH_WAGON ? 1 : 0);
	int r = val_a - val_b;

	/* Use EngineID to sort instead since we want consistent sorting */
	if (r == 0) return EngineNumberSorter(a, b);
	return _internal_sort_order ? -r : r;
}

/** Sort functions for the vehicle sort criteria, for each vehicle type. */
static EngList_SortTypeFunction * const _sorter[][11] = {{
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
}};

static const StringID _sort_listing[][12] = {{
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
	INVALID_STRING_ID
}};

/** Cargo filter functions */
static bool CDECL CargoFilter(const EngineID *eid, const CargoID cid)
{
	if (cid == CF_ANY) return true;
	uint32 refit_mask = GetUnionOfArticulatedRefitMasks(*eid, true);
	return (cid == CF_NONE ? refit_mask == 0 : HasBit(refit_mask, cid));
}

static GUIEngineList::FilterFunction * const _filter_funcs[] = {
	&CargoFilter,
};

/**
 * Engine drawing loop
 * @param type Type of vehicle (VEH_*)
 * @param l The left most location of the list
 * @param r The right most location of the list
 * @param y The top most location of the list
 * @param eng_list What engines to draw
 * @param min where to start in the list
 * @param max where in the list to end
 * @param selected_id what engine to highlight as selected, if any
 * @param show_count Whether to show the amount of engines or not
 * @param selected_group the group to list the engines of
 */
static void DrawEngineList(VehicleType type, int l, int r, int y, const GUIEngineList *eng_list, uint16 min, uint16 max, EngineID selected_id, bool show_count, GroupID selected_group)
{
	static const int sprite_widths[]  = { 60, 60, 76, 67 };
	static const int sprite_y_offsets[] = { -1, -1, -2, -2 };

	/* Obligatory sanity checks! */
	assert((uint)type < lengthof(sprite_widths));
	assert_compile(lengthof(sprite_y_offsets) == lengthof(sprite_widths));
	assert(max <= eng_list->Length());

	bool rtl = _current_text_dir == TD_RTL;
	int step_size = GetEngineListHeight(type);
	int sprite_width = sprite_widths[type];

	int sprite_x        = (rtl ? r - sprite_width / 2 : l + sprite_width / 2) - 1;
	int sprite_y_offset = sprite_y_offsets[type] + step_size / 2;

	int text_left  = l + (rtl ? WD_FRAMERECT_LEFT : sprite_width);
	int text_right = r - (rtl ? sprite_width : WD_FRAMERECT_RIGHT);

	int normal_text_y_offset = (step_size - FONT_HEIGHT_NORMAL) / 2;
	int small_text_y_offset  = step_size - FONT_HEIGHT_SMALL - WD_FRAMERECT_BOTTOM - 1;

	for (; min < max; min++, y += step_size) {
		const EngineID engine = (*eng_list)[min];
		/* Note: num_engines is only used in the autoreplace GUI, so it is correct to use _local_company here. */
		const uint num_engines = GetGroupNumEngines(_local_company, selected_group, engine);

		SetDParam(0, engine);
		DrawString(text_left, text_right, y + normal_text_y_offset, STR_ENGINE_NAME, engine == selected_id ? TC_WHITE : TC_BLACK);
		DrawVehicleEngine(l, r, sprite_x, y + sprite_y_offset, engine, (show_count && num_engines == 0) ? PALETTE_CRASH : GetEnginePalette(engine, _local_company), EIT_PURCHASE);
		if (show_count) {
			SetDParam(0, num_engines);
			DrawString(text_left, text_right, y + small_text_y_offset, STR_TINY_BLACK_COMA, TC_FROMSTRING, SA_RIGHT);
		}
	}
}


struct BuildVirtualTrainWindow : Window {
	VehicleType vehicle_type;
	union {
		RailTypeByte railtype;
		RoadTypes roadtypes;
	} filter;
	bool descending_sort_order;
	byte sort_criteria;
	bool listview_mode;
	EngineID sel_engine;
	EngineID rename_engine;
	GUIEngineList eng_list;
	CargoID cargo_filter[NUM_CARGO + 2];        ///< Available cargo filters; CargoID or CF_ANY or CF_NONE
	StringID cargo_filter_texts[NUM_CARGO + 3]; ///< Texts for filter_cargo, terminated by INVALID_STRING_ID
	byte cargo_filter_criteria;                 ///< Selected cargo filter
	int details_height;                         ///< Minimal needed height of the details panels (found so far).
	Scrollbar *vscroll;
	Train **virtual_train;                      ///< the virtual train that is currently being created
	bool *noticeParent;

	BuildVirtualTrainWindow(WindowDesc *desc, Train **vt, bool *notice) : Window(desc)
	{
		this->vehicle_type = VEH_TRAIN;
		this->window_number = 0;

		this->sel_engine      = INVALID_ENGINE;

		this->sort_criteria         = _last_sort_criteria[VEH_TRAIN];
		this->descending_sort_order = _last_sort_order[VEH_TRAIN];

		this->filter.railtype = RAILTYPE_END;

		this->listview_mode = (this->window_number <= VEH_END);

		this->CreateNestedTree(desc);

		this->vscroll = this->GetScrollbar(WID_BV_SCROLLBAR);

		NWidgetCore *widget = this->GetWidget<NWidgetCore>(WID_BV_LIST);

		widget = this->GetWidget<NWidgetCore>(WID_BV_BUILD);

		widget = this->GetWidget<NWidgetCore>(WID_BV_RENAME);
		widget->widget_data = STR_BUY_VEHICLE_TRAIN_RENAME_BUTTON + VEH_TRAIN;
		widget->tool_tip    = STR_BUY_VEHICLE_TRAIN_RENAME_TOOLTIP + VEH_TRAIN;

		this->details_height = ((this->vehicle_type == VEH_TRAIN) ? 10 : 9) * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		this->FinishInitNested(VEH_TRAIN);

		this->owner = _local_company;

		this->eng_list.ForceRebuild();
		this->GenerateBuildList();

		if (this->eng_list.Length() > 0) this->sel_engine = this->eng_list[0];

		this->virtual_train = vt;
		this->noticeParent = notice;
	}

	/** Populate the filter list and set the cargo filter criteria. */
	void SetCargoFilterArray()
	{
		uint filter_items = 0;

		/* Add item for disabling filtering. */
		this->cargo_filter[filter_items] = CF_ANY;
		this->cargo_filter_texts[filter_items] = STR_PURCHASE_INFO_ALL_TYPES;
		filter_items++;

		/* Add item for vehicles not carrying anything, e.g. train engines.
		 * This could also be useful for eyecandy vehicles of other types, but is likely too confusing for joe, */
		if (this->vehicle_type == VEH_TRAIN) {
			this->cargo_filter[filter_items] = CF_NONE;
			this->cargo_filter_texts[filter_items] = STR_LAND_AREA_INFORMATION_LOCAL_AUTHORITY_NONE;
			filter_items++;
		}

		/* Collect available cargo types for filtering. */
		const CargoSpec *cs;
		FOR_ALL_SORTED_STANDARD_CARGOSPECS(cs) {
			this->cargo_filter[filter_items] = cs->Index();
			this->cargo_filter_texts[filter_items] = cs->name;
			filter_items++;
		}

		/* Terminate the filter list. */
		this->cargo_filter_texts[filter_items] = INVALID_STRING_ID;

		/* If not found, the cargo criteria will be set to all cargoes. */
		this->cargo_filter_criteria = 0;

		/* Find the last cargo filter criteria. */
		for (uint i = 0; i < filter_items; ++i) {
			if (this->cargo_filter[i] == _last_filter_criteria[this->vehicle_type]) {
				this->cargo_filter_criteria = i;
				break;
			}
		}

		this->eng_list.SetFilterFuncs(_filter_funcs);
		this->eng_list.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
	}

	void OnInit()
	{
		this->SetCargoFilterArray();
	}

	/** Filter the engine list against the currently selected cargo filter */
	void FilterEngineList()
	{
		this->eng_list.Filter(this->cargo_filter[this->cargo_filter_criteria]);
		if (0 == this->eng_list.Length()) { // no engine passed through the filter, invalidate the previously selected engine
			this->sel_engine = INVALID_ENGINE;
		} else if (!this->eng_list.Contains(this->sel_engine)) { // previously selected engine didn't pass the filter, select the first engine of the list
			this->sel_engine = this->eng_list[0];
		}
	}

	/** Filter a single engine */
	bool FilterSingleEngine(EngineID eid)
	{
		CargoID filter_type = this->cargo_filter[this->cargo_filter_criteria];
		return (filter_type == CF_ANY || CargoFilter(&eid, filter_type));
	}

	/* Figure out what train EngineIDs to put in the list */
	void GenerateBuildTrainList()
	{
		EngineID sel_id = INVALID_ENGINE;
		int num_engines = 0;
		int num_wagons  = 0;

		this->filter.railtype = (this->listview_mode) ? RAILTYPE_END : GetRailType(this->window_number);

		this->eng_list.Clear();

		/* Make list of all available train engines and wagons.
		 * Also check to see if the previously selected engine is still available,
		 * and if not, reset selection to INVALID_ENGINE. This could be the case
		 * when engines become obsolete and are removed */
		const Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, VEH_TRAIN) {
			EngineID eid = e->index;
			const RailVehicleInfo *rvi = &e->u.rail;

			if (this->filter.railtype != RAILTYPE_END && !HasPowerOnRail(rvi->railtype, this->filter.railtype)) continue;
			if (!IsEngineBuildable(eid, VEH_TRAIN, _local_company)) continue;

			/* Filter now! So num_engines and num_wagons is valid */
			if (!FilterSingleEngine(eid)) continue;

			*this->eng_list.Append() = eid;

			if (rvi->railveh_type != RAILVEH_WAGON) {
				num_engines++;
			} else {
				num_wagons++;
			}

			if (eid == this->sel_engine) sel_id = eid;
		}

		this->sel_engine = sel_id;

		/* make engines first, and then wagons, sorted by ListPositionOfEngine() */
		_internal_sort_order = false;
		EngList_Sort(&this->eng_list, TrainEnginesThenWagonsSorter);

		/* and then sort engines */
		_internal_sort_order = this->descending_sort_order;
		EngList_SortPartial(&this->eng_list, _sorter[0][this->sort_criteria], 0, num_engines);

		/* and finally sort wagons */
		EngList_SortPartial(&this->eng_list, _sorter[0][this->sort_criteria], num_engines, num_wagons);
	}

	/* Generate the list of vehicles */
	void GenerateBuildList()
	{
		if (!this->eng_list.NeedRebuild()) return;

		this->GenerateBuildTrainList();
		this->eng_list.Compact();
		this->eng_list.RebuildDone();
		return; // trains should not reach the last sorting


		this->FilterEngineList();

		_internal_sort_order = this->descending_sort_order;
		EngList_Sort(&this->eng_list, _sorter[this->vehicle_type][this->sort_criteria]);

		this->eng_list.Compact();
		this->eng_list.RebuildDone();
	}

	virtual	void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->descending_sort_order ^= true;
				_last_sort_order[this->vehicle_type] = this->descending_sort_order;
				this->eng_list.ForceRebuild();
				this->SetDirty();
				break;

			case WID_BV_LIST: {
				uint i = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_BV_LIST);
				size_t num_items = this->eng_list.Length();
				this->sel_engine = (i < num_items) ? this->eng_list[i] : INVALID_ENGINE;
				this->SetDirty();
				if (click_count > 1 && !this->listview_mode) this->OnClick(pt, WID_BV_BUILD, 1);
				break;
			}
			case WID_BV_SORT_DROPDOWN: { // Select sorting criteria dropdown menu
				uint32 hidden_mask = 0;
				/* Disable sorting by power or tractive effort when the original acceleration model for road vehicles is being used. */
				if (this->vehicle_type == VEH_ROAD &&
						_settings_game.vehicle.roadveh_acceleration_model == AM_ORIGINAL) {
					SetBit(hidden_mask, 3); // power
					SetBit(hidden_mask, 4); // tractive effort
					SetBit(hidden_mask, 8); // power by running costs
				}
				/* Disable sorting by tractive effort when the original acceleration model for trains is being used. */
				if (this->vehicle_type == VEH_TRAIN &&
						_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
					SetBit(hidden_mask, 4); // tractive effort
				}
				ShowDropDownMenu(this, _sort_listing[this->vehicle_type], this->sort_criteria, WID_BV_SORT_DROPDOWN, 0, hidden_mask);
				break;
			}

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select cargo filtering criteria dropdown menu
				ShowDropDownMenu(this, this->cargo_filter_texts, this->cargo_filter_criteria, WID_BV_CARGO_FILTER_DROPDOWN, 0, 0);
				break;

			case WID_BV_BUILD: {
				EngineID sel_eng = this->sel_engine;
				if (sel_eng != INVALID_ENGINE) {
					DoCommandP(0, sel_engine, 0, CMD_BUILD_VIRTUAL_RAIL_VEHICLE, CcAddVirtualEngine);
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
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		/* When switching to original acceleration model for road vehicles, clear the selected sort criteria if it is not available now. */

		this->eng_list.ForceRebuild();
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_BV_CAPTION:
				if (this->vehicle_type == VEH_TRAIN && !this->listview_mode) {
					const RailtypeInfo *rti = GetRailTypeInfo(this->filter.railtype);
					SetDParam(0, rti->strings.build_caption);
				} else {
					SetDParam(0, (this->listview_mode ? STR_VEHICLE_LIST_AVAILABLE_TRAINS : STR_BUY_VEHICLE_TRAIN_ALL_CAPTION) + this->vehicle_type);
				}
				break;

			case WID_BV_SORT_DROPDOWN:
				SetDParam(0, _sort_listing[this->vehicle_type][this->sort_criteria]);
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN:
				SetDParam(0, this->cargo_filter_texts[this->cargo_filter_criteria]);
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_BV_LIST:
				resize->height = GetEngineListHeight(this->vehicle_type);
				size->height = 3 * resize->height;
				break;

			case WID_BV_PANEL:
				size->height = this->details_height;
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + WD_CLOSEBOX_WIDTH * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_BV_LIST:
				DrawEngineList(this->vehicle_type, r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP,
						&this->eng_list, this->vscroll->GetPosition(), min(this->vscroll->GetPosition() + this->vscroll->GetCapacity(),
						this->eng_list.Length()), this->sel_engine, false, DEFAULT_GROUP);
				break;

			case WID_BV_SORT_ASCENDING_DESCENDING:
				this->DrawSortButtonState(WID_BV_SORT_ASCENDING_DESCENDING, this->descending_sort_order ? SBS_DOWN : SBS_UP);
				break;
		}
	}

	virtual void OnPaint()
	{
		this->GenerateBuildList();
		this->vscroll->SetCount(this->eng_list.Length());

		this->DrawWidgets();

		if (!this->IsShaded()) {
			int needed_height = this->details_height;
			/* Draw details panels. */
			if (this->sel_engine != INVALID_ENGINE) {
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_BV_PANEL);
				int text_end = DrawVehiclePurchaseInfo(nwi->pos_x + WD_FRAMETEXT_LEFT, nwi->pos_x + nwi->current_x - WD_FRAMETEXT_RIGHT,
						nwi->pos_y + WD_FRAMERECT_TOP, this->sel_engine);
				needed_height = max(needed_height, text_end - (int)nwi->pos_y + WD_FRAMERECT_BOTTOM);
			}
			if (needed_height != this->details_height) { // Details window are not high enough, enlarge them.
				int resize = needed_height - this->details_height;
				this->details_height = needed_height;
				this->ReInit(0, resize);
				return;
			}
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		DoCommandP(0, this->rename_engine, 0, CMD_RENAME_ENGINE | CMD_MSG(STR_ERROR_CAN_T_RENAME_TRAIN_TYPE + this->vehicle_type), NULL, str);
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case WID_BV_SORT_DROPDOWN:
				if (this->sort_criteria != index) {
					this->sort_criteria = index;
					_last_sort_criteria[this->vehicle_type] = this->sort_criteria;
					this->eng_list.ForceRebuild();
				}
				break;

			case WID_BV_CARGO_FILTER_DROPDOWN: // Select a cargo filter criteria
				if (this->cargo_filter_criteria != index) {
					this->cargo_filter_criteria = index;
					_last_filter_criteria[this->vehicle_type] = this->cargo_filter[this->cargo_filter_criteria];
					/* deactivate filter if criteria is 'Show All', activate it otherwise */
					this->eng_list.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
					this->eng_list.ForceRebuild();
				}
				break;
		}
		this->SetDirty();
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_BV_LIST);
		this->GetWidget<NWidgetCore>(WID_BV_LIST)->widget_data = (this->vscroll->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
	}

	void AddVirtualEngine(Train *toadd)
	{
		if (*virtual_train == NULL) {
			*virtual_train = toadd;
		} else {
			VehicleID target = (*(this->virtual_train))->GetLastUnit()->index;

			DoCommandP(0, (1 << 21) | toadd->index, target, CMD_MOVE_RAIL_VEHICLE);
		}
		*noticeParent = true;
	}
};

void CcAddVirtualEngine(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	Window* window = FindWindowById(WC_BUILD_VIRTUAL_TRAIN, 0);
	if (window) {
		Train* train = Train::From(Vehicle::Get(_new_vehicle_id));
		((BuildVirtualTrainWindow*)window)->AddVirtualEngine(train);
		window->InvalidateData();
	}
}

static WindowDesc _build_vehicle_desc(
	WDP_AUTO,                        // window position
	"template create virtual train", // const char* ini_key
	240, 268,                        // window size
	WC_BUILD_VIRTUAL_TRAIN,          // window class
	WC_CREATE_TEMPLATE,              // parent window class
	WDF_CONSTRUCTION,                // window flags
	_nested_build_vehicle_widgets, lengthof(_nested_build_vehicle_widgets)  // widgets + num widgets
);

void ShowBuildVirtualTrainWindow(Train **vt, bool *noticeParent)
{
	// '0' as in VEH_TRAIN = Tile=0
	assert(IsCompanyBuildableVehicleType(VEH_TRAIN));

	DeleteWindowById(WC_BUILD_VEHICLE, 0);

	new BuildVirtualTrainWindow(&_build_vehicle_desc, vt, noticeParent);
}
