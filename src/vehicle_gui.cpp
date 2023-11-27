/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_gui.cpp The base GUI for all vehicles. */

#include "stdafx.h"
#include "debug.h"
#include "company_func.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "vehicle_gui_base.h"
#include "viewport_func.h"
#include "newgrf_text.h"
#include "newgrf_debug.h"
#include "roadveh.h"
#include "train.h"
#include "aircraft.h"
#include "depot_map.h"
#include "group_gui.h"
#include "strings_func.h"
#include "vehicle_func.h"
#include "autoreplace_gui.h"
#include "string_func.h"
#include "widgets/dropdown_func.h"
#include "timetable.h"
#include "articulated_vehicles.h"
#include "spritecache.h"
#include "core/geometry_func.hpp"
#include "core/container_func.hpp"
#include "company_base.h"
#include "engine_func.h"
#include "station_base.h"
#include "infrastructure_func.h"
#include "tilehighlight_func.h"
#include "train.h"
#include "tbtr_template_gui_main.h"
#include "zoom_func.h"
#include "tracerestrict.h"
#include "depot_base.h"
#include "hotkeys.h"

#include <vector>
#include <algorithm>

#include "safeguards.h"


BaseVehicleListWindow::GroupBy _grouping[VLT_END][VEH_COMPANY_END];
Sorting _sorting[BaseVehicleListWindow::GB_END];

static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleNumberSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleNameSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleAgeSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleProfitThisYearSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleProfitLastYearSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleProfitLifetimeSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleCargoSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleReliabilitySorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleMaxSpeedSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleModelSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleValueSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleLengthSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleTimeToLiveSorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleTimetableDelaySorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleAverageOrderOccupancySorter;
static BaseVehicleListWindow::VehicleIndividualSortFunction VehicleMaxSpeedLoadedSorter;
static BaseVehicleListWindow::VehicleGroupSortFunction VehicleGroupLengthSorter;
static BaseVehicleListWindow::VehicleGroupSortFunction VehicleGroupTotalProfitThisYearSorter;
static BaseVehicleListWindow::VehicleGroupSortFunction VehicleGroupTotalProfitLastYearSorter;
static BaseVehicleListWindow::VehicleGroupSortFunction VehicleGroupAverageProfitThisYearSorter;
static BaseVehicleListWindow::VehicleGroupSortFunction VehicleGroupAverageProfitLastYearSorter;

/** Wrapper to convert a VehicleIndividualSortFunction to a VehicleGroupSortFunction */
template <BaseVehicleListWindow::VehicleIndividualSortFunction func>
static bool VehicleIndividualToGroupSorterWrapper(GUIVehicleGroup const &a, GUIVehicleGroup const &b)
{
	return func(*(a.vehicles_begin), *(b.vehicles_begin));
}

enum VehicleSortType
{
	VST_NUMBER,
	VST_NAME,
	VST_AGE,
	VST_PROFIT_THIS_YEAR,
	VST_PROFIT_LAST_YEAR,
	VST_PROFIT_LIFETIME,
	VST_CARGO,
	VST_RELIABILITY,
	VST_MAX_SPEED,
	VST_MODEL,
	VST_VALUE,
	VST_LENGTH,
	VST_TIME_TO_LIVE,
	VST_TIMETABLE_DELAY,
	VST_AVERAGE_ORDER_OCCUPANCY,
	VST_MAX_SPEED_LOADED,
};

BaseVehicleListWindow::VehicleGroupSortFunction * const BaseVehicleListWindow::vehicle_group_none_sorter_funcs[] = {
	&VehicleIndividualToGroupSorterWrapper<VehicleNumberSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleNameSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleAgeSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleProfitThisYearSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleProfitLastYearSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleProfitLifetimeSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleCargoSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleReliabilitySorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleMaxSpeedSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleModelSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleValueSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleLengthSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleTimeToLiveSorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleTimetableDelaySorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleAverageOrderOccupancySorter>,
	&VehicleIndividualToGroupSorterWrapper<VehicleMaxSpeedLoadedSorter>,
};

const StringID BaseVehicleListWindow::vehicle_group_none_sorter_names[] = {
	STR_SORT_BY_NUMBER,
	STR_SORT_BY_NAME,
	STR_SORT_BY_AGE,
	STR_SORT_BY_PROFIT_THIS_YEAR,
	STR_SORT_BY_PROFIT_LAST_YEAR,
	STR_SORT_BY_PROFIT_LIFETIME,
	STR_SORT_BY_TOTAL_CAPACITY_PER_CARGOTYPE,
	STR_SORT_BY_RELIABILITY,
	STR_SORT_BY_MAX_SPEED,
	STR_SORT_BY_MODEL,
	STR_SORT_BY_VALUE,
	STR_SORT_BY_LENGTH,
	STR_SORT_BY_LIFE_TIME,
	STR_SORT_BY_TIMETABLE_DELAY,
	STR_SORT_BY_AVG_ORDER_OCCUPANCY,
	STR_SORT_BY_MAX_SPEED_LOADED,
	INVALID_STRING_ID
};

BaseVehicleListWindow::VehicleGroupSortFunction * const BaseVehicleListWindow::vehicle_group_shared_orders_sorter_funcs[] = {
	&VehicleGroupLengthSorter,
	&VehicleGroupTotalProfitThisYearSorter,
	&VehicleGroupTotalProfitLastYearSorter,
	&VehicleGroupAverageProfitThisYearSorter,
	&VehicleGroupAverageProfitLastYearSorter,
};

const StringID BaseVehicleListWindow::vehicle_group_shared_orders_sorter_names[] = {
	STR_SORT_BY_NUM_VEHICLES,
	STR_SORT_BY_TOTAL_PROFIT_THIS_YEAR,
	STR_SORT_BY_TOTAL_PROFIT_LAST_YEAR,
	STR_SORT_BY_AVERAGE_PROFIT_THIS_YEAR,
	STR_SORT_BY_AVERAGE_PROFIT_LAST_YEAR,
	INVALID_STRING_ID
};

const StringID BaseVehicleListWindow::vehicle_group_by_names[] = {
	STR_GROUP_BY_NONE,
	STR_GROUP_BY_SHARED_ORDERS,
	INVALID_STRING_ID
};

const StringID BaseVehicleListWindow::vehicle_depot_name[] = {
	STR_VEHICLE_LIST_SEND_TRAIN_TO_DEPOT,
	STR_VEHICLE_LIST_SEND_ROAD_VEHICLE_TO_DEPOT,
	STR_VEHICLE_LIST_SEND_SHIP_TO_DEPOT,
	STR_VEHICLE_LIST_SEND_AIRCRAFT_TO_HANGAR
};

const StringID BaseVehicleListWindow::vehicle_depot_sell_name[] = {
	STR_VEHICLE_LIST_SEND_TRAIN_TO_DEPOT_SELL,
	STR_VEHICLE_LIST_SEND_ROAD_VEHICLE_TO_DEPOT_SELL,
	STR_VEHICLE_LIST_SEND_SHIP_TO_DEPOT_SELL,
	STR_VEHICLE_LIST_SEND_AIRCRAFT_TO_HANGAR_SELL
};

BaseVehicleListWindow::BaseVehicleListWindow(WindowDesc *desc, WindowNumber wno) : Window(desc), vli(VehicleListIdentifier::UnPack(wno))
{
	this->grouping = _grouping[vli.type][vli.vtype];
	this->vehicle_sel = INVALID_VEHICLE;
	this->UpdateSortingFromGrouping();
}

/**
 * Get the number of digits of space required for the given number.
 * @param number The number.
 * @return The number of digits to allocate space for.
 */
uint CountDigitsForAllocatingSpace(uint number)
{
	if (number >= 10000) return 5;
	if (number >= 1000) return 4;
	if (number >= 100) return 3;

	/*
	 * When the smallest unit number is less than 10, it is
	 * quite likely that it will expand to become more than
	 * 10 quite soon.
	 */
	return 2;
}

/**
 * Get the number of digits the biggest unit number of a set of vehicles has.
 * @param vehicles The list of vehicles.
 * @return The number of digits to allocate space for.
 */
uint GetUnitNumberDigits(VehicleList &vehicles)
{
	uint unitnumber = 0;
	for (const Vehicle *v : vehicles) {
		unitnumber = std::max<uint>(unitnumber, v->unitnumber);
	}

	return CountDigitsForAllocatingSpace(unitnumber);
}

void BaseVehicleListWindow::CountOwnVehicles()
{
	this->own_vehicles = 0;
	for (const GUIVehicleGroup &vg : this->vehgroups) {
		if ((*(vg.vehicles_begin))->owner == _local_company) this->own_vehicles++;
	}
	this->own_company = _local_company;
}

void BaseVehicleListWindow::BuildVehicleList()
{
	if (!this->vehgroups.NeedRebuild()) return;

	DEBUG(misc, 3, "Building vehicle list type %d for company %d given index %d", this->vli.type, this->vli.company, this->vli.index);

	this->vehgroups.clear();

	GenerateVehicleSortList(&this->vehicles, this->vli);

	if (this->grouping == GB_NONE) {
		uint max_unitnumber = 0;
		for (auto it = this->vehicles.begin(); it != this->vehicles.end(); ++it) {
			this->vehgroups.emplace_back(it, it + 1);

			max_unitnumber = std::max<uint>(max_unitnumber, (*it)->unitnumber);
		}
		this->unitnumber_digits = CountDigitsForAllocatingSpace(max_unitnumber);
	} else {
		/* Sort by the primary vehicle; we just want all vehicles that share the same orders to form a contiguous range. */
		std::stable_sort(this->vehicles.begin(), this->vehicles.end(), [](const Vehicle * const &u, const Vehicle * const &v) {
			return u->FirstShared() < v->FirstShared();
		});

		uint max_num_vehicles = 0;

		VehicleList::const_iterator begin = this->vehicles.begin();
		while (begin != this->vehicles.end()) {
			VehicleList::const_iterator end = std::find_if_not(begin, this->vehicles.cend(), [first_shared = (*begin)->FirstShared()](const Vehicle * const &v) {
				return v->FirstShared() == first_shared;
			});

			this->vehgroups.emplace_back(begin, end);

			max_num_vehicles = std::max<uint>(max_num_vehicles, static_cast<uint>(end - begin));

			begin = end;
		}

		this->unitnumber_digits = CountDigitsForAllocatingSpace(max_num_vehicles);
	}
	this->FilterVehicleList();
	this->CountOwnVehicles();

	this->vehgroups.RebuildDone();
	this->vscroll->SetCount(this->vehgroups.size());
}

static bool GroupCargoFilter(const GUIVehicleGroup* group, const CargoID cid)
{
	if (cid == CF_ANY) return true;
	for (VehicleList::const_iterator v = group->vehicles_begin; v != group->vehicles_end; ++v) {
		if (VehicleCargoFilter(*v, cid)) return true;
	}
	return false;
}

static GUIVehicleGroupList::FilterFunction * const _filter_funcs[] = {
	&GroupCargoFilter,
};

/** Set cargo filter list item index. */
void BaseVehicleListWindow::SetCargoFilterIndex(int index)
{
	if (this->cargo_filter_criteria != index) {
		this->cargo_filter_criteria = index;
		/* deactivate filter if criteria is 'Show All', activate it otherwise */
		this->vehgroups.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
		this->vehgroups.SetFilterType(0);
		this->vehgroups.ForceRebuild();
	}
}

/** Populate the filter list and set the cargo filter criteria. */
void BaseVehicleListWindow::SetCargoFilterArray()
{
	uint filter_items = 0;

	/* Add item for disabling filtering. */
	this->cargo_filter[filter_items] = CF_ANY;
	this->cargo_filter_texts[filter_items] = STR_CARGO_TYPE_FILTER_ALL;
	this->cargo_filter_criteria = filter_items;
	filter_items++;

	/* Add item for freight (i.e. vehicles with cargo capacity and with no passenger capacity) */
	this->cargo_filter[filter_items] = CF_FREIGHT;
	this->cargo_filter_texts[filter_items] = STR_CARGO_TYPE_FILTER_FREIGHT;
	filter_items++;

	/* Add item for vehicles not carrying anything, e.g. train engines.
	* This could also be useful for eyecandy vehicles of other types, but is likely too confusing for joe, */
	this->cargo_filter[filter_items] = CF_NONE;
	this->cargo_filter_texts[filter_items] = STR_CARGO_TYPE_FILTER_NONE;
	filter_items++;

	/* Collect available cargo types for filtering. */
	for (const CargoSpec *cs : _sorted_standard_cargo_specs) {
		this->cargo_filter[filter_items] = cs->Index();
		this->cargo_filter_texts[filter_items] = cs->name;
		filter_items++;
	}

	/* Terminate the filter list. */
	this->cargo_filter_texts[filter_items] = INVALID_STRING_ID;

	this->vehgroups.SetFilterFuncs(_filter_funcs);
	this->vehgroups.SetFilterState(this->cargo_filter[this->cargo_filter_criteria] != CF_ANY);
}

/** Filter the engine list against the currently selected cargo filter */
void BaseVehicleListWindow::FilterVehicleList()
{
	this->vehgroups.Filter(this->cargo_filter[this->cargo_filter_criteria]);
	if (this->vehicles.empty()) {
		/* No vehicle passed through the filter, invalidate the previously selected vehicle */
		this->vehicle_sel = INVALID_VEHICLE;
	} else if (this->vehicle_sel != INVALID_VEHICLE && std::find(this->vehicles.begin(), this->vehicles.end(), Vehicle::Get(this->vehicle_sel)) == this->vehicles.end()) { // previously selected engine didn't pass the filter, remove selection
		this->vehicle_sel = INVALID_VEHICLE;
	}
}

void BaseVehicleListWindow::CheckCargoFilterEnableState(int plane_widget, bool re_init, bool possible)
{
	NWidgetStacked *sel = this->GetWidget<NWidgetStacked>(plane_widget);
	const int plane = (possible && _settings_client.gui.show_veh_list_cargo_filter) ? 0 : SZSP_NONE;
	if (plane != sel->shown_plane) {
		sel->SetDisplayedPlane(plane);
		if (re_init) this->ReInit();
	}
}

/**
 * Compute the size for the Action dropdown.
 * @param show_autoreplace If true include the autoreplace item.
 * @param show_group If true include group-related stuff.
 * @return Required size.
 */
Dimension BaseVehicleListWindow::GetActionDropdownSize(bool show_autoreplace, bool show_group, bool show_template_replace, StringID change_order_str)
{
	Dimension d = {0, 0};

	if (show_autoreplace) d = maxdim(d, GetStringBoundingBox(STR_VEHICLE_LIST_REPLACE_VEHICLES));
	if (show_autoreplace && show_template_replace) {
		d = maxdim(d, GetStringBoundingBox(STR_TMPL_TEMPLATE_REPLACEMENT));
	}
	d = maxdim(d, GetStringBoundingBox(STR_VEHICLE_LIST_SEND_FOR_SERVICING));
	d = maxdim(d, GetStringBoundingBox(this->vehicle_depot_name[this->vli.vtype]));

	if (show_group) {
		d = maxdim(d, GetStringBoundingBox(STR_GROUP_ADD_SHARED_VEHICLE));
		d = maxdim(d, GetStringBoundingBox(STR_GROUP_REMOVE_ALL_VEHICLES));
	}

	if (change_order_str != 0) {
		d = maxdim(d, GetStringBoundingBox(change_order_str));
	}

	d = maxdim(d, GetStringBoundingBox(STR_VEHICLE_LIST_CREATE_GROUP));

	return d;
}

void BaseVehicleListWindow::OnInit()
{
	this->order_arrow_width = GetStringBoundingBox(STR_JUST_RIGHT_ARROW, FS_SMALL).width;
	this->SetCargoFilterArray();
}

/**
 * Whether the Action dropdown window should be shown/available.
 * @return Whether available
 */
bool BaseVehicleListWindow::ShouldShowActionDropdownList() const
{
	return this->own_vehicles != 0 || (this->vli.vtype == VEH_TRAIN && _settings_client.gui.show_adv_tracerestrict_features);
}

/**
 * Display the Action dropdown window.
 * @param show_autoreplace If true include the autoreplace item.
 * @param show_group If true include group-related stuff.
 * @return Itemlist for dropdown
 */
DropDownList BaseVehicleListWindow::BuildActionDropdownList(bool show_autoreplace, bool show_group, bool show_template_replace,
		StringID change_order_str, bool show_create_group, bool consider_top_level)
{
	DropDownList list;
	bool disable = this->own_vehicles == 0;
	bool mass_action_disable = disable || (_settings_client.gui.disable_top_veh_list_mass_actions && consider_top_level);

	if (show_autoreplace) list.push_back(std::make_unique<DropDownListStringItem>(STR_VEHICLE_LIST_REPLACE_VEHICLES, ADI_REPLACE, disable));
	if (show_autoreplace && show_template_replace) {
		list.push_back(std::make_unique<DropDownListStringItem>(STR_TMPL_TEMPLATE_REPLACEMENT, ADI_TEMPLATE_REPLACE, disable));
	}
	list.push_back(std::make_unique<DropDownListStringItem>(STR_VEHICLE_LIST_SEND_FOR_SERVICING, ADI_SERVICE, mass_action_disable));
	list.push_back(std::make_unique<DropDownListStringItem>(this->vehicle_depot_name[this->vli.vtype], ADI_DEPOT, mass_action_disable));
	if (_settings_client.gui.show_depot_sell_gui) list.push_back(std::make_unique<DropDownListStringItem>(this->vehicle_depot_sell_name[this->vli.vtype], ADI_DEPOT_SELL, mass_action_disable));
	list.push_back(std::make_unique<DropDownListStringItem>(STR_VEHICLE_LIST_CANCEL_DEPOT_SERVICE, ADI_CANCEL_DEPOT, mass_action_disable));

	if (show_group) {
		list.push_back(std::make_unique<DropDownListStringItem>(STR_GROUP_ADD_SHARED_VEHICLE, ADI_ADD_SHARED, disable));
		list.push_back(std::make_unique<DropDownListStringItem>(STR_GROUP_REMOVE_ALL_VEHICLES, ADI_REMOVE_ALL, disable));
	}
	if (_settings_client.gui.show_adv_tracerestrict_features) {
		list.push_back(std::make_unique<DropDownListStringItem>(STR_TRACE_RESTRICT_SLOT_MANAGE, ADI_TRACERESTRICT_SLOT_MGMT, false));
		list.push_back(std::make_unique<DropDownListStringItem>(STR_TRACE_RESTRICT_COUNTER_MANAGE, ADI_TRACERESTRICT_COUNTER_MGMT, false));
	}
	if (change_order_str != 0) {
		list.push_back(std::make_unique<DropDownListStringItem>(change_order_str, ADI_CHANGE_ORDER, disable));
	}
	if (show_create_group) {
		list.push_back(std::make_unique<DropDownListStringItem>(STR_VEHICLE_LIST_CREATE_GROUP, ADI_CREATE_GROUP, disable));
	}

	return list;
}

/* cached values for VehicleNameSorter to spare many GetString() calls */
static const Vehicle *_last_vehicle[2] = { nullptr, nullptr };

static btree::btree_map<VehicleID, int> _vehicle_max_speed_loaded;

void BaseVehicleListWindow::SortVehicleList()
{
	if (this->vehgroups.Sort()) return;

	/* invalidate cached values for name sorter - vehicle names could change */
	_last_vehicle[0] = _last_vehicle[1] = nullptr;
	_vehicle_max_speed_loaded.clear();
}

void DepotSortList(VehicleList *list)
{
	if (list->size() < 2) return;
	std::sort(list->begin(), list->end(), &VehicleNumberSorter);
}

/** draw the vehicle profit button in the vehicle list window. */
static void DrawVehicleProfitButton(Date age, Money display_profit_last_year, uint num_vehicles, int x, int y)
{
	SpriteID spr;

	/* draw profit-based coloured icons */
	if (age <= VEHICLE_PROFIT_MIN_AGE) {
		spr = SPR_PROFIT_NA;
	} else if (display_profit_last_year < 0) {
		spr = SPR_PROFIT_NEGATIVE;
	} else if (display_profit_last_year < VEHICLE_PROFIT_THRESHOLD * num_vehicles) {
		spr = SPR_PROFIT_SOME;
	} else {
		spr = SPR_PROFIT_LOT;
	}
	DrawSprite(spr, PAL_NONE, x, y);
}

/** Maximum number of refit cycles we try, to prevent infinite loops. And we store only a byte anyway */
static const uint MAX_REFIT_CYCLE = 256;

/**
 * Get the best fitting subtype when 'cloning'/'replacing' \a v_from with \a v_for.
 * All articulated parts of both vehicles are tested to find a possibly shared subtype.
 * For \a v_for only vehicle refittable to \a dest_cargo_type are considered.
 * @param v_from the vehicle to match the subtype from
 * @param v_for  the vehicle to get the subtype for
 * @param dest_cargo_type Destination cargo type.
 * @return the best sub type
 */
byte GetBestFittingSubType(const Vehicle *v_from, Vehicle *v_for, CargoID dest_cargo_type)
{
	v_from = v_from->GetFirstEnginePart();
	v_for = v_for->GetFirstEnginePart();

	/* Create a list of subtypes used by the various parts of v_for */
	static std::vector<StringID> subtypes;
	subtypes.clear();
	for (; v_from != nullptr; v_from = v_from->HasArticulatedPart() ? v_from->GetNextArticulatedPart() : nullptr) {
		const Engine *e_from = v_from->GetEngine();
		if (!e_from->CanCarryCargo() || !HasBit(e_from->info.callback_mask, CBM_VEHICLE_CARGO_SUFFIX)) continue;
		include(subtypes, GetCargoSubtypeText(v_from));
	}

	byte ret_refit_cyc = 0;
	bool success = false;
	if (!subtypes.empty()) {
		/* Check whether any articulated part is refittable to 'dest_cargo_type' with a subtype listed in 'subtypes' */
		for (Vehicle *v = v_for; v != nullptr; v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr) {
			const Engine *e = v->GetEngine();
			if (!e->CanCarryCargo() || !HasBit(e->info.callback_mask, CBM_VEHICLE_CARGO_SUFFIX)) continue;
			if (!HasBit(e->info.refit_mask, dest_cargo_type) && v->cargo_type != dest_cargo_type) continue;

			CargoID old_cargo_type = v->cargo_type;
			byte old_cargo_subtype = v->cargo_subtype;

			/* Set the 'destination' cargo */
			v->cargo_type = dest_cargo_type;

			/* Cycle through the refits */
			for (uint refit_cyc = 0; refit_cyc < MAX_REFIT_CYCLE; refit_cyc++) {
				v->cargo_subtype = refit_cyc;

				/* Make sure we don't pick up anything cached. */
				v->First()->InvalidateNewGRFCache();
				v->InvalidateNewGRFCache();

				StringID subtype = GetCargoSubtypeText(v);
				if (subtype == STR_EMPTY) break;

				if (std::find(subtypes.begin(), subtypes.end(), subtype) == subtypes.end()) continue;

				/* We found something matching. */
				ret_refit_cyc = refit_cyc;
				success = true;
				break;
			}

			/* Reset the vehicle's cargo type */
			v->cargo_type    = old_cargo_type;
			v->cargo_subtype = old_cargo_subtype;

			/* Make sure we don't taint the vehicle. */
			v->First()->InvalidateNewGRFCache();
			v->InvalidateNewGRFCache();

			if (success) break;
		}
	}

	return ret_refit_cyc;
}

/**
 * Get the engine that suffers from the most severe breakdown.
 * This means the engine with the lowest breakdown_type.
 * If the breakdown types of 2 engines are equal, the one with the lowest breakdown_severity (most severe) is picked.
 * @param v The front engine of the train.
 * @return The most severly broken engine.
 */
const Vehicle *GetMostSeverelyBrokenEngine(const Train *v)
{
	assert(v->IsFrontEngine());
	const Vehicle *w = v;
	byte most_severe_type = 255;
	for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
		if (u->breakdown_ctr == 1) {
			if (u->breakdown_type < most_severe_type) {
				most_severe_type = u->breakdown_type;
				w = u;
			} else if (u->breakdown_type == most_severe_type && u->breakdown_severity < w->breakdown_severity) {
				w = u;
			}
		}
	}
	return w;
}

/** Option to refit a vehicle chain */
struct RefitOption {
	CargoID cargo;    ///< Cargo to refit to
	byte subtype;     ///< Subcargo to use
	StringID string;  ///< GRF-local String to display for the cargo

	/**
	 * Inequality operator for #RefitOption.
	 * @param other Compare to this #RefitOption.
	 * @return True if both #RefitOption are different.
	 */
	inline bool operator != (const RefitOption &other) const
	{
		return other.cargo != this->cargo || other.string != this->string;
	}

	/**
	 * Equality operator for #RefitOption.
	 * @param other Compare to this #RefitOption.
	 * @return True if both #RefitOption are equal.
	 */
	inline bool operator == (const RefitOption &other) const
	{
		return other.cargo == this->cargo && other.string == this->string;
	}
};

using RefitOptions = std::map<CargoID, std::vector<RefitOption>, CargoIDComparator>; ///< Available refit options (subtype and string) associated with each cargo type.

/**
 * Draw the list of available refit options for a consist and highlight the selected refit option (if any).
 * @param refits Available refit options for each (sorted) cargo.
 * @param sel   Selected refit option in the window
 * @param pos   Position of the selected item in caller widow
 * @param rows  Number of rows(capacity) in caller window
 * @param delta Step height in caller window
 * @param r     Rectangle of the matrix widget.
 */
static void DrawVehicleRefitWindow(const RefitOptions &refits, const RefitOption *sel, uint pos, uint rows, uint delta, const Rect &r)
{
	Rect ir = r.Shrink(WidgetDimensions::scaled.matrix);
	uint current = 0;

	bool rtl = _current_text_dir == TD_RTL;
	uint iconwidth = std::max(GetSpriteSize(SPR_CIRCLE_FOLDED).width, GetSpriteSize(SPR_CIRCLE_UNFOLDED).width);
	uint iconheight = GetSpriteSize(SPR_CIRCLE_FOLDED).height;
	int linecolour = _colour_gradient[COLOUR_ORANGE][4];

	int iconleft   = rtl ? ir.right - iconwidth     : ir.left;
	int iconcenter = rtl ? ir.right - iconwidth / 2 : ir.left + iconwidth / 2;
	int iconinner  = rtl ? ir.right - iconwidth     : ir.left + iconwidth;

	Rect tr = ir.Indent(iconwidth + WidgetDimensions::scaled.hsep_wide, rtl);

	/* Draw the list of subtypes for each cargo, and find the selected refit option (by its position). */
	for (const auto &pair : refits) {
		bool has_subtypes = pair.second.size() > 1;
		for (const RefitOption &refit : pair.second) {
			if (current >= pos + rows) break;

			/* Hide subtypes if selected cargo type does not match */
			if ((sel == nullptr || sel->cargo != refit.cargo) && refit.subtype != UINT8_MAX) continue;

			/* Refit options with a position smaller than pos don't have to be drawn. */
			if (current < pos) {
				current++;
				continue;
			}

			if (has_subtypes) {
				if (refit.subtype != UINT8_MAX) {
					/* Draw tree lines */
					int ycenter = tr.top + FONT_HEIGHT_NORMAL / 2;
					GfxDrawLine(iconcenter, tr.top - WidgetDimensions::scaled.matrix.top, iconcenter, (&refit == &pair.second.back()) ? ycenter : tr.top - WidgetDimensions::scaled.matrix.top + delta - 1, linecolour);
					GfxDrawLine(iconcenter, ycenter, iconinner, ycenter, linecolour);
				} else {
					/* Draw expand/collapse icon */
					DrawSprite((sel != nullptr && sel->cargo == refit.cargo) ? SPR_CIRCLE_UNFOLDED : SPR_CIRCLE_FOLDED, PAL_NONE, iconleft, tr.top + (FONT_HEIGHT_NORMAL - iconheight) / 2);
				}
			}

			TextColour colour = (sel != nullptr && sel->cargo == refit.cargo && sel->subtype == refit.subtype) ? TC_WHITE : TC_BLACK;
			/* Get the cargo name. */
			SetDParam(0, CargoSpec::Get(refit.cargo)->name);
			SetDParam(1, refit.string);
			DrawString(tr, STR_JUST_STRING_STRING, colour);

			tr.top += delta;
			current++;
		}
	}
}

/** Refit cargo window. */
struct RefitWindow : public Window {
	const RefitOption *selected_refit; ///< Selected refit option.
	RefitOptions refit_list; ///< List of refit subtypes available for each sorted cargo.
	VehicleOrderID order;        ///< If not #INVALID_VEH_ORDER_ID, selection is part of a refit order (rather than execute directly).
	uint information_width;      ///< Width required for correctly displaying all cargoes in the information panel.
	Scrollbar *vscroll;          ///< The main scrollbar.
	Scrollbar *hscroll;          ///< Only used for long vehicles.
	int vehicle_width;           ///< Width of the vehicle being drawn.
	int sprite_left;             ///< Left position of the vehicle sprite.
	int sprite_right;            ///< Right position of the vehicle sprite.
	uint vehicle_margin;         ///< Margin to use while selecting vehicles when the vehicle image is centered.
	int click_x;                 ///< Position of the first click while dragging.
	VehicleID selected_vehicle;  ///< First vehicle in the current selection.
	uint8 num_vehicles;          ///< Number of selected vehicles.
	bool auto_refit;             ///< Select cargo for auto-refitting.
	bool is_virtual_train;       ///< TemplateReplacement, whether the selected vehicle is virtual
	mutable std::map<VehicleID, std::string> ship_part_names; ///< Ship part name strings

	/**
	 * Collects all (cargo, subcargo) refit options of a vehicle chain.
	 */
	void BuildRefitList()
	{
		/* Store the currently selected RefitOption. */
		std::optional<RefitOption> current_refit_option;
		if (this->selected_refit != nullptr) current_refit_option = *(this->selected_refit);
		this->selected_refit = nullptr;

		this->refit_list.clear();
		Vehicle *v = Vehicle::Get(this->window_number);

		/* Check only the selected vehicles. */
		VehicleSet vehicles_to_refit;
		GetVehicleSet(vehicles_to_refit, Vehicle::Get(this->selected_vehicle), this->num_vehicles);

		do {
			if (v->type == VEH_TRAIN && std::find(vehicles_to_refit.begin(), vehicles_to_refit.end(), v->index) == vehicles_to_refit.end()) continue;
			if (v->type == VEH_SHIP && this->num_vehicles == 1 && v->index != this->selected_vehicle) continue;
			const Engine *e = v->GetEngine();
			CargoTypes cmask = e->info.refit_mask;
			byte callback_mask = e->info.callback_mask;

			/* Skip this engine if it does not carry anything */
			if (!e->CanCarryCargo()) continue;
			/* Skip this engine if we build the list for auto-refitting and engine doesn't allow it. */
			if (this->auto_refit && !HasBit(e->info.misc_flags, EF_AUTO_REFIT)) continue;

			/* Loop through all cargoes in the refit mask */
			for (const auto &cs : _sorted_cargo_specs) {
				CargoID cid = cs->Index();
				/* Skip cargo type if it's not listed */
				if (!HasBit(cmask, cid)) continue;

				auto &list = this->refit_list[cid];
				bool first_vehicle = list.empty();
				if (first_vehicle) {
					/* Keeping the current subtype is always an option. It also serves as the option in case of no subtypes */
					list.push_back({cid, UINT8_MAX, STR_EMPTY});
				}

				/* Check the vehicle's callback mask for cargo suffixes.
				 * This is not supported for ordered refits, since subtypes only have a meaning
				 * for a specific vehicle at a specific point in time, which conflicts with shared orders,
				 * autoreplace, autorenew, clone, order restoration, ... */
				if (this->order == INVALID_VEH_ORDER_ID && HasBit(callback_mask, CBM_VEHICLE_CARGO_SUFFIX)) {
					/* Make a note of the original cargo type. It has to be
					 * changed to test the cargo & subtype... */
					CargoID temp_cargo = v->cargo_type;
					byte temp_subtype  = v->cargo_subtype;

					v->cargo_type = cid;

					for (uint refit_cyc = 0; refit_cyc < MAX_REFIT_CYCLE; refit_cyc++) {
						v->cargo_subtype = refit_cyc;

						/* Make sure we don't pick up anything cached. */
						v->First()->InvalidateNewGRFCache();
						v->InvalidateNewGRFCache();

						StringID subtype = GetCargoSubtypeText(v);

						if (first_vehicle) {
							/* Append new subtype (don't add duplicates though) */
							if (subtype == STR_EMPTY) break;

							RefitOption option;
							option.cargo   = cid;
							option.subtype = refit_cyc;
							option.string  = subtype;
							include(list, option);
						} else {
							/* Intersect the subtypes of earlier vehicles with the subtypes of this vehicle */
							if (subtype == STR_EMPTY) {
								/* No more subtypes for this vehicle, delete all subtypes >= refit_cyc */
								/* UINT8_MAX item is in front, other subtypes are sorted. So just truncate the list in the right spot */
								for (uint i = 1; i < list.size(); i++) {
									if (list[i].subtype >= refit_cyc) {
										list.resize(i);
										break;
									}
								}
								break;
							} else {
								/* Check whether the subtype matches with the subtype of earlier vehicles. */
								uint pos = 1;
								while (pos < list.size() && list[pos].subtype != refit_cyc) pos++;
								if (pos < list.size() && list[pos].string != subtype) {
									/* String mismatch, remove item keeping the order */
									list.erase(list.begin() + pos);
								}
							}
						}
					}

					/* Reset the vehicle's cargo type */
					v->cargo_type    = temp_cargo;
					v->cargo_subtype = temp_subtype;

					/* And make sure we haven't tainted the cache */
					v->First()->InvalidateNewGRFCache();
					v->InvalidateNewGRFCache();
				}
			}
		} while (v->IsArticulatedCallbackVehicleType() && (v = v->Next()) != nullptr);

		/* Restore the previously selected RefitOption. */
		if (current_refit_option.has_value()) {
			for (const auto &pair : this->refit_list) {
				for (const auto &refit : pair.second) {
					if (refit.cargo == current_refit_option->cargo && refit.subtype == current_refit_option->subtype) {
						this->selected_refit = &refit;
						break;
					}
				}
				if (this->selected_refit != nullptr) break;
			}
		}

		this->SetWidgetDisabledState(WID_VR_REFIT, this->selected_refit == nullptr);
	}

	/**
	 * Refresh scrollbar after selection changed
	 */
	void RefreshScrollbar()
	{
		size_t scroll_row = 0;
		size_t rows = 0;
		CargoID cargo = this->selected_refit == nullptr ? (CargoID)CT_INVALID : this->selected_refit->cargo;

		for (const auto &pair : this->refit_list) {
			if (pair.first == cargo) {
				/* selected_refit points to an element in the vector so no need to search for it. */
				scroll_row = rows + (this->selected_refit - pair.second.data());
				rows += pair.second.size();
			} else {
				rows++; /* Unselected cargo type is collapsed into one row. */
			}
		}

		this->vscroll->SetCount(rows);
		this->vscroll->ScrollTowards(static_cast<int>(scroll_row));
	}

	/**
	 * Select a row.
	 * @param click_row Clicked row
	 */
	void SetSelection(uint click_row)
	{
		uint row = 0;

		for (const auto &pair : refit_list) {
			for (const RefitOption &refit : pair.second) {
				if (row == click_row) {
					this->selected_refit = &refit;
					return;
				}
				row++;
				/* If this cargo type is not already selected then its subtypes are not visible, so skip the rest. */
				if (this->selected_refit == nullptr || this->selected_refit->cargo != refit.cargo) break;
			}
		}

		/* No selection made */
		this->selected_refit = nullptr;
	}

	RefitWindow(WindowDesc *desc, const Vehicle *v, VehicleOrderID order, bool auto_refit, bool is_virtual) : Window(desc)
	{
		this->auto_refit = auto_refit;
		this->is_virtual_train = is_virtual;
		this->order = order;
		this->CreateNestedTree();

		this->vscroll = this->GetScrollbar(WID_VR_SCROLLBAR);
		this->hscroll = (v->IsGroundVehicle() ? this->GetScrollbar(WID_VR_HSCROLLBAR) : nullptr);
		this->GetWidget<NWidgetCore>(WID_VR_SELECT_HEADER)->tool_tip = STR_REFIT_TRAIN_LIST_TOOLTIP + v->type;
		this->GetWidget<NWidgetCore>(WID_VR_MATRIX)->tool_tip        = STR_REFIT_TRAIN_LIST_TOOLTIP + v->type;
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_VR_REFIT);
		nwi->widget_data = STR_REFIT_TRAIN_REFIT_BUTTON + v->type;
		nwi->tool_tip    = STR_REFIT_TRAIN_REFIT_TOOLTIP + v->type;
		int hscrollbar_pane;
		if (v->IsGroundVehicle()) {
			hscrollbar_pane = 0;
		} else if (v->type == VEH_SHIP && v->Next() != nullptr && this->order == INVALID_VEH_ORDER_ID) {
			hscrollbar_pane = 1;
		} else {
			hscrollbar_pane = SZSP_HORIZONTAL;
		}
		this->GetWidget<NWidgetStacked>(WID_VR_SHOW_HSCROLLBAR)->SetDisplayedPlane(hscrollbar_pane);
		this->GetWidget<NWidgetCore>(WID_VR_VEHICLE_PANEL_DISPLAY)->tool_tip = (v->type == VEH_TRAIN) ? STR_REFIT_SELECT_VEHICLES_TOOLTIP : STR_NULL;

		this->FinishInitNested(v->index);
		this->owner = v->owner;

		this->SetWidgetDisabledState(WID_VR_REFIT, this->selected_refit == nullptr);
	}

	void Close(int data = 0) override
	{
		if (this->window_number != INVALID_VEHICLE) {
			if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
				if (this->window_number != INVALID_VEHICLE) {
					const Vehicle *v = Vehicle::Get(this->window_number);
					MarkDirtyFocusedRoutePaths(v);
				}
			}
		}
		this->Window::Close();
	}

	void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}

	void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}

	void OnInit() override
	{
		/* (Re)build the refit list */
		this->OnInvalidateData(VIWD_CONSIST_CHANGED);
	}

	void OnPaint() override
	{
		/* Determine amount of items for scroller. */
		if (this->hscroll != nullptr) this->hscroll->SetCount(this->vehicle_width);

		/* Calculate sprite position. */
		NWidgetCore *vehicle_panel_display = this->GetWidget<NWidgetCore>(WID_VR_VEHICLE_PANEL_DISPLAY);
		int sprite_width = std::max(0, ((int)vehicle_panel_display->current_x - this->vehicle_width) / 2);
		this->sprite_left = vehicle_panel_display->pos_x;
		this->sprite_right = vehicle_panel_display->pos_x + vehicle_panel_display->current_x - 1;
		if (_current_text_dir == TD_RTL) {
			this->sprite_right -= sprite_width;
			this->vehicle_margin = vehicle_panel_display->current_x - sprite_right;
		} else {
			this->sprite_left += sprite_width;
			this->vehicle_margin = sprite_left;
		}

		this->DrawWidgets();
	}

	void UpdateWidgetSize(int widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		switch (widget) {
			case WID_VR_MATRIX:
				resize->height = FONT_HEIGHT_NORMAL + padding.height;
				size->height = resize->height * 8;
				break;

			case WID_VR_VEHICLE_PANEL_DISPLAY:
				size->height = ScaleGUITrad(GetVehicleHeight(Vehicle::Get(this->window_number)->type));
				break;

			case WID_VR_INFO:
				size->width = this->information_width + padding.height;
				break;
		}
	}

	const std::string &GetShipPartName(const Vehicle *v) const
	{
		std::string &name = this->ship_part_names[v->index];
		if (name.empty()) {
			char buffer[128] = "";
			const Vehicle *front = v->First();
			uint offset = 0;
			for (const Vehicle *u = front; u != v; u = u->Next()) offset++;
			uint16 callback = GetVehicleCallback(XCBID_SHIP_REFIT_PART_NAME, offset, 0, front->engine_type, front);
			if (callback != CALLBACK_FAILED && callback < 0x400) {
				const GRFFile *grffile = v->GetGRF();
				assert(grffile != nullptr);

				StartTextRefStackUsage(grffile, 6);
				char *end = GetString(buffer, GetGRFStringID(grffile->grfid, 0xD000 + callback), lastof(buffer));
				StopTextRefStackUsage();

				name.assign(buffer, end - buffer);
			} else {
				SetDParam(0, offset + 1);
				char *end = GetString(buffer, STR_REFIT_SHIP_PART, lastof(buffer));
				name.assign(buffer, end - buffer);
			}
		}
		return name;
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_VR_CAPTION) SetDParam(0, Vehicle::Get(this->window_number)->index);

		if (widget == WID_VR_VEHICLE_DROPDOWN) {
			if (this->num_vehicles == 1) {
				SetDParam(0, STR_JUST_RAW_STRING);
				SetDParamStr(1, this->GetShipPartName(Vehicle::Get(this->selected_vehicle)));
			} else {
				SetDParam(0, STR_REFIT_WHOLE_SHIP);
			}
		}
	}

	/**
	 * Gets the #StringID to use for displaying capacity.
	 * @param option Cargo and cargo subtype to check for capacity.
	 * @return INVALID_STRING_ID if there is no capacity. StringID to use in any other case.
	 * @post String parameters have been set.
	 */
	StringID GetCapacityString(const RefitOption &option) const
	{
		assert(_current_company == _local_company);
		Vehicle *v = Vehicle::Get(this->window_number);
		CommandCost cost = DoCommand(v->tile, this->selected_vehicle, option.cargo | option.subtype << 8 | this->num_vehicles << 16 |
				(int)this->auto_refit << 24, DC_QUERY_COST, GetCmdRefitVeh(v->type));

		if (cost.Failed()) return INVALID_STRING_ID;

		SetDParam(0, option.cargo);
		SetDParam(1, _returned_refit_capacity);

		Money money = cost.GetCost();
		if (_returned_mail_refit_capacity > 0) {
			SetDParam(2, CT_MAIL);
			SetDParam(3, _returned_mail_refit_capacity);
			if (this->order != INVALID_VEH_ORDER_ID) {
				/* No predictable cost */
				return STR_PURCHASE_INFO_AIRCRAFT_CAPACITY;
			} else if (money <= 0) {
				SetDParam(4, -money);
				return STR_REFIT_NEW_CAPACITY_INCOME_FROM_AIRCRAFT_REFIT;
			} else {
				SetDParam(4, money);
				return STR_REFIT_NEW_CAPACITY_COST_OF_AIRCRAFT_REFIT;
			}
		} else {
			if (this->order != INVALID_VEH_ORDER_ID) {
				/* No predictable cost */
				SetDParam(2, STR_EMPTY);
				return STR_PURCHASE_INFO_CAPACITY;
			} else if (money <= 0) {
				SetDParam(2, -money);
				return STR_REFIT_NEW_CAPACITY_INCOME_FROM_REFIT;
			} else {
				SetDParam(2, money);
				return STR_REFIT_NEW_CAPACITY_COST_OF_REFIT;
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_VR_VEHICLE_PANEL_DISPLAY: {
				Vehicle *v = Vehicle::Get(this->window_number);
				DrawVehicleImage(v, {this->sprite_left, r.top, this->sprite_right, r.bottom},
					INVALID_VEHICLE, EIT_IN_DETAILS, this->hscroll != nullptr ? this->hscroll->GetPosition() : 0);

				/* Highlight selected vehicles. */
				if (this->order != INVALID_VEH_ORDER_ID) break;
				int x = 0;
				switch (v->type) {
					case VEH_TRAIN: {
						VehicleSet vehicles_to_refit;
						GetVehicleSet(vehicles_to_refit, Vehicle::Get(this->selected_vehicle), this->num_vehicles);

						int left = INT32_MIN;
						int width = 0;

						/* Determine top & bottom position of the highlight.*/
						const int height = ScaleSpriteTrad(12);
						const int highlight_top = CenterBounds(r.top, r.bottom, height);
						const int highlight_bottom = highlight_top + height - 1;

						for (Train *u = Train::From(v); u != nullptr; u = u->Next()) {
							/* Start checking. */
							const bool contained = std::find(vehicles_to_refit.begin(), vehicles_to_refit.end(), u->index) != vehicles_to_refit.end();
							if (contained && left == INT32_MIN) {
								left = x - this->hscroll->GetPosition() + r.left + this->vehicle_margin;
								width = 0;
							}

							/* Draw a selection. */
							if ((!contained || u->Next() == nullptr) && left != INT32_MIN) {
								if (u->Next() == nullptr && contained) {
									int current_width = u->GetDisplayImageWidth();
									width += current_width;
									x += current_width;
								}

								int right = Clamp(left + width, 0, r.right);
								left = std::max(0, left);

								if (_current_text_dir == TD_RTL) {
									right = r.Width() - left;
									left = right - width;
								}

								if (left != right) {
									Rect hr = {left, highlight_top, right, highlight_bottom};
									DrawFrameRect(hr.Expand(WidgetDimensions::scaled.bevel), COLOUR_WHITE, FR_BORDERONLY);
								}

								left = INT32_MIN;
							}

							int current_width = u->GetDisplayImageWidth();
							width += current_width;
							x += current_width;
						}
						break;
					}

					default: break;
				}
				break;
			}

			case WID_VR_MATRIX:
				DrawVehicleRefitWindow(this->refit_list, this->selected_refit, this->vscroll->GetPosition(), this->vscroll->GetCapacity(), this->resize.step_height, r);
				break;

			case WID_VR_INFO:
				if (this->selected_refit != nullptr) {
					StringID string = this->GetCapacityString(*this->selected_refit);
					if (string != INVALID_STRING_ID) {
						DrawStringMultiLine(r.Shrink(WidgetDimensions::scaled.framerect), string);
					}
				}
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		switch (data) {
			case VIWD_AUTOREPLACE: // Autoreplace replaced the vehicle; selected_vehicle became invalid.
			case VIWD_CONSIST_CHANGED: { // The consist has changed; rebuild the entire list.
				/* Clear the selection. */
				Vehicle *v = Vehicle::Get(this->window_number);
				this->selected_vehicle = v->index;
				this->num_vehicles = UINT8_MAX;
				this->ship_part_names.clear();
				FALLTHROUGH;
			}

			case 2: { // The vehicle selection has changed; rebuild the entire list.
				if (!gui_scope) break;
				this->BuildRefitList();

				/* The vehicle width has changed too. */
				this->vehicle_width = GetVehicleWidth(Vehicle::Get(this->window_number), EIT_IN_DETAILS);
				uint max_width = 0;

				/* Check the width of all cargo information strings. */
				for (const auto &list : this->refit_list) {
					for (const RefitOption &refit : list.second) {
						StringID string = this->GetCapacityString(refit);
						if (string != INVALID_STRING_ID) {
							Dimension dim = GetStringBoundingBox(string);
							max_width = std::max(dim.width, max_width);
						}
					}
				}

				if (this->information_width < max_width) {
					this->information_width = max_width;
					this->ReInit();
				}
				FALLTHROUGH;
			}

			case 1: // A new cargo has been selected.
				if (!gui_scope) break;
				this->RefreshScrollbar();
				break;
		}
	}

	int GetClickPosition(int click_x)
	{
		const NWidgetCore *matrix_widget = this->GetWidget<NWidgetCore>(WID_VR_VEHICLE_PANEL_DISPLAY);
		if (_current_text_dir == TD_RTL) click_x = matrix_widget->current_x - click_x;
		click_x -= this->vehicle_margin;
		if (this->hscroll != nullptr) click_x += this->hscroll->GetPosition();

		return click_x;
	}

	void SetSelectedVehicles(int drag_x)
	{
		drag_x = GetClickPosition(drag_x);

		int left_x  = std::min(this->click_x, drag_x);
		int right_x = std::max(this->click_x, drag_x);
		this->num_vehicles = 0;

		Vehicle *v = Vehicle::Get(this->window_number);
		/* Find the vehicle part that was clicked. */
		switch (v->type) {
			case VEH_TRAIN: {
				/* Don't select anything if we are not clicking in the vehicle. */
				if (left_x >= 0) {
					const Train *u = Train::From(v);
					bool start_counting = false;
					for (; u != nullptr; u = u->Next()) {
						int current_width = u->GetDisplayImageWidth();
						left_x  -= current_width;
						right_x -= current_width;

						if (left_x < 0 && !start_counting) {
							this->selected_vehicle = u->index;
							start_counting = true;

							/* Count the first vehicle, even if articulated part */
							this->num_vehicles++;
						} else if (start_counting && !u->IsArticulatedPart()) {
							/* Do not count articulated parts */
							this->num_vehicles++;
						}

						if (right_x < 0) break;
					}
				}

				/* If the selection is not correct, clear it. */
				if (this->num_vehicles != 0) {
					if (_ctrl_pressed) this->num_vehicles = UINT8_MAX;
					break;
				}
				FALLTHROUGH;
			}

			default:
				/* Clear the selection. */
				this->selected_vehicle = v->index;
				this->num_vehicles = UINT8_MAX;
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index) override
	{
		if (widget != WID_VR_VEHICLE_DROPDOWN) return;

		const Vehicle *v = Vehicle::Get(this->window_number);

		if (index > 0) {
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				if (index == 1) {
					this->selected_vehicle = u->index;
					this->num_vehicles = 1;
					this->InvalidateData(2);
					return;
				}
				index--;
			}
		}

		this->selected_vehicle = v->index;
		this->num_vehicles = UINT8_MAX;
		this->InvalidateData(2);
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_VR_VEHICLE_PANEL_DISPLAY: { // Vehicle image.
				if (this->order != INVALID_VEH_ORDER_ID) break;
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_VR_VEHICLE_PANEL_DISPLAY);
				this->click_x = GetClickPosition(pt.x - nwi->pos_x);
				this->SetSelectedVehicles(pt.x - nwi->pos_x);
				this->SetWidgetDirty(WID_VR_VEHICLE_PANEL_DISPLAY);
				if (!_ctrl_pressed) {
					SetObjectToPlaceWnd(SPR_CURSOR_MOUSE, PAL_NONE, HT_DRAG, this);
				} else {
					/* The vehicle selection has changed. */
					this->InvalidateData(2);
				}
				break;
			}

			case WID_VR_MATRIX: { // listbox
				this->SetSelection(this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_VR_MATRIX));
				this->SetWidgetDisabledState(WID_VR_REFIT, this->selected_refit == nullptr);
				this->InvalidateData(1);

				if (click_count == 1) break;
				FALLTHROUGH;
			}

			case WID_VR_REFIT: // refit button
				if (this->selected_refit != nullptr) {
					const Vehicle *v = Vehicle::Get(this->window_number);

					if (this->order == INVALID_VEH_ORDER_ID) {
						bool delete_window = this->selected_vehicle == v->index && this->num_vehicles == UINT8_MAX;
						if (DoCommandP(v->tile, this->selected_vehicle, this->selected_refit->cargo | this->selected_refit->subtype << 8 | this->num_vehicles << 16 | this->is_virtual_train << 31,
								GetCmdRefitVeh(v)) && delete_window) {
							this->Close();
						}
					} else {
						if (DoCommandP(v->tile, v->index, this->selected_refit->cargo | this->selected_refit->subtype << 8 | this->order << 16, CMD_ORDER_REFIT)) this->Close();
					}
				}
				break;

			case WID_VR_VEHICLE_DROPDOWN: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				if (v->type != VEH_SHIP) break;

				DropDownList dlist;
				int selected = 0;
				dlist.emplace_back(new DropDownListStringItem(STR_REFIT_WHOLE_SHIP, 0, false));

				int offset = 1;
				for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
					if (u->index == this->selected_vehicle && this->num_vehicles == 1) selected = offset;
					dlist.emplace_back(new DropDownListStringItem(this->GetShipPartName(u), offset, false));
					offset++;
				}

				ShowDropDownList(this, std::move(dlist), selected, WID_VR_VEHICLE_DROPDOWN);
				break;
			}
		}
	}

	void OnMouseDrag(Point pt, int widget) override
	{
		switch (widget) {
			case WID_VR_VEHICLE_PANEL_DISPLAY: { // Vehicle image.
				if (this->order != INVALID_VEH_ORDER_ID) break;
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_VR_VEHICLE_PANEL_DISPLAY);
				this->SetSelectedVehicles(pt.x - nwi->pos_x);
				this->SetWidgetDirty(WID_VR_VEHICLE_PANEL_DISPLAY);
				break;
			}
		}
	}

	void OnDragDrop(Point pt, int widget) override
	{
		switch (widget) {
			case WID_VR_VEHICLE_PANEL_DISPLAY: { // Vehicle image.
				if (this->order != INVALID_VEH_ORDER_ID) break;
				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_VR_VEHICLE_PANEL_DISPLAY);
				this->SetSelectedVehicles(pt.x - nwi->pos_x);
				this->InvalidateData(2);
				break;
			}
		}
	}

	void OnResize() override
	{
		this->vehicle_width = GetVehicleWidth(Vehicle::Get(this->window_number), EIT_IN_DETAILS);
		this->vscroll->SetCapacityFromWidget(this, WID_VR_MATRIX);
		if (this->hscroll != nullptr) this->hscroll->SetCapacityFromWidget(this, WID_VR_VEHICLE_PANEL_DISPLAY);
	}
};

static const NWidgetPart _nested_vehicle_refit_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VR_CAPTION), SetDataTip(STR_REFIT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
	EndContainer(),
	/* Vehicle display + scrollbar. */
	NWidget(NWID_VERTICAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VR_VEHICLE_PANEL_DISPLAY), SetMinimalSize(228, 14), SetResize(1, 0), SetScrollbar(WID_VR_HSCROLLBAR), EndContainer(),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VR_SHOW_HSCROLLBAR),
			NWidget(NWID_HSCROLLBAR, COLOUR_GREY, WID_VR_HSCROLLBAR),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VR_VEHICLE_DROPDOWN), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_JUST_STRING1, STR_REFIT_SHIP_PART_DROPDOWN_TOOLTIP),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_VR_SELECT_HEADER), SetDataTip(STR_REFIT_TITLE, STR_NULL), SetResize(1, 0),
	/* Matrix + scrollbar. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_VR_MATRIX), SetMinimalSize(228, 112), SetResize(1, 14), SetFill(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_VR_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VR_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VR_INFO), SetMinimalTextLines(2, WidgetDimensions::unscaled.framerect.Vertical()), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VR_REFIT), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static WindowDesc _vehicle_refit_desc(
	WDP_AUTO, "view_vehicle_refit", 240, 174,
	WC_VEHICLE_REFIT, WC_VEHICLE_VIEW,
	WDF_CONSTRUCTION,
	std::begin(_nested_vehicle_refit_widgets), std::end(_nested_vehicle_refit_widgets)
);

/**
 * Show the refit window for a vehicle
 * @param *v The vehicle to show the refit window for
 * @param order of the vehicle to assign refit to, or INVALID_VEH_ORDER_ID to refit the vehicle now
 * @param parent the parent window of the refit window
 * @param auto_refit Choose cargo for auto-refitting
 */
void ShowVehicleRefitWindow(const Vehicle *v, VehicleOrderID order, Window *parent, bool auto_refit, bool is_virtual_train)
{
	CloseWindowById(WC_VEHICLE_REFIT, v->index);
	RefitWindow *w = new RefitWindow(&_vehicle_refit_desc, v, order, auto_refit, is_virtual_train);
	w->parent = parent;
}

/** Display list of cargo types of the engine, for the purchase information window */
uint ShowRefitOptionsList(int left, int right, int y, EngineID engine)
{
	/* List of cargo types of this engine */
	CargoTypes cmask = GetUnionOfArticulatedRefitMasks(engine, false);
	/* List of cargo types available in this climate */
	CargoTypes lmask = _cargo_mask;

	/* Draw nothing if the engine is not refittable */
	if (HasAtMostOneBit(cmask)) return y;

	if (cmask == lmask) {
		/* Engine can be refitted to all types in this climate */
		SetDParam(0, STR_PURCHASE_INFO_ALL_TYPES);
	} else {
		/* Check if we are able to refit to more cargo types and unable to. If
		 * so, invert the cargo types to list those that we can't refit to. */
		if (CountBits(cmask ^ lmask) < CountBits(cmask) && CountBits(cmask ^ lmask) <= 7) {
			cmask ^= lmask;
			SetDParam(0, STR_PURCHASE_INFO_ALL_BUT);
		} else {
			SetDParam(0, STR_JUST_CARGO_LIST);
		}
		SetDParam(1, cmask);
	}

	return DrawStringMultiLine(left, right, y, INT32_MAX, STR_PURCHASE_INFO_REFITTABLE_TO);
}

/** Get the cargo subtype text from NewGRF for the vehicle details window. */
StringID GetCargoSubtypeText(const Vehicle *v)
{
	if (HasBit(EngInfo(v->engine_type)->callback_mask, CBM_VEHICLE_CARGO_SUFFIX)) {
		uint16 cb = GetVehicleCallback(CBID_VEHICLE_CARGO_SUFFIX, 0, 0, v->engine_type, v);
		if (cb != CALLBACK_FAILED) {
			if (cb > 0x400) ErrorUnknownCallbackResult(v->GetGRFID(), CBID_VEHICLE_CARGO_SUFFIX, cb);
			if (cb >= 0x400 || (v->GetGRF()->grf_version < 8 && cb == 0xFF)) cb = CALLBACK_FAILED;
		}
		if (cb != CALLBACK_FAILED) {
			return GetGRFStringID(v->GetGRFID(), 0xD000 + cb);
		}
	}
	return STR_EMPTY;
}

/** Sort vehicle groups by the number of vehicles in the group */
static bool VehicleGroupLengthSorter(const GUIVehicleGroup &a, const GUIVehicleGroup &b)
{
	return a.NumVehicles() < b.NumVehicles();
}

/** Sort vehicle groups by the total profit this year */
static bool VehicleGroupTotalProfitThisYearSorter(const GUIVehicleGroup &a, const GUIVehicleGroup &b)
{
	return a.GetDisplayProfitThisYear() < b.GetDisplayProfitThisYear();
}

/** Sort vehicle groups by the total profit last year */
static bool VehicleGroupTotalProfitLastYearSorter(const GUIVehicleGroup &a, const GUIVehicleGroup &b)
{
	return a.GetDisplayProfitLastYear() < b.GetDisplayProfitLastYear();
}

/** Sort vehicle groups by the average profit this year */
static bool VehicleGroupAverageProfitThisYearSorter(const GUIVehicleGroup &a, const GUIVehicleGroup &b)
{
	return a.GetDisplayProfitThisYear() * static_cast<uint>(b.NumVehicles()) < b.GetDisplayProfitThisYear() * static_cast<uint>(a.NumVehicles());
}

/** Sort vehicle groups by the average profit last year */
static bool VehicleGroupAverageProfitLastYearSorter(const GUIVehicleGroup &a, const GUIVehicleGroup &b)
{
	return a.GetDisplayProfitLastYear() * static_cast<uint>(b.NumVehicles()) < b.GetDisplayProfitLastYear() * static_cast<uint>(a.NumVehicles());
}

/** Sort vehicles by their number */
static bool VehicleNumberSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	return a->unitnumber < b->unitnumber;
}

/** Sort vehicles by their name */
static bool VehicleNameSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	static std::string last_name[2] = { {}, {} };

	if (a != _last_vehicle[0]) {
		_last_vehicle[0] = a;
		SetDParam(0, a->index);
		last_name[0] = GetString(STR_VEHICLE_NAME);
	}

	if (b != _last_vehicle[1]) {
		_last_vehicle[1] = b;
		SetDParam(0, b->index);
		last_name[1] = GetString(STR_VEHICLE_NAME);
	}

	int r = StrNaturalCompare(last_name[0], last_name[1]); // Sort by name (natural sorting).
	return (r != 0) ? r < 0: VehicleNumberSorter(a, b);
}

/** Sort vehicles by their age */
static bool VehicleAgeSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	auto r = a->age - b->age;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by this year profit */
static bool VehicleProfitThisYearSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = ClampTo<int32_t>(a->GetDisplayProfitThisYear() - b->GetDisplayProfitThisYear());
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by last year profit */
static bool VehicleProfitLastYearSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = ClampTo<int32>(a->GetDisplayProfitLastYear() - b->GetDisplayProfitLastYear());
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by lifetime profit */
static bool VehicleProfitLifetimeSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = ClampTo<int32>(a->GetDisplayProfitLifetime() - b->GetDisplayProfitLifetime());
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their cargo */
static bool VehicleCargoSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	const Vehicle *v;
	CargoArray diff{};

	/* Append the cargo of the connected waggons */
	for (v = a; v != nullptr; v = v->Next()) diff[v->cargo_type] += v->cargo_cap;
	for (v = b; v != nullptr; v = v->Next()) diff[v->cargo_type] -= v->cargo_cap;

	int r = 0;
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		r = diff[i];
		if (r != 0) break;
	}

	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their reliability */
static bool VehicleReliabilitySorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->reliability - b->reliability;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their max speed */
static bool VehicleMaxSpeedSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->vcache.cached_max_speed - b->vcache.cached_max_speed;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by model */
static bool VehicleModelSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->engine_type - b->engine_type;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their value */
static bool VehicleValueSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	const Vehicle *u;
	Money diff = 0;

	for (u = a; u != nullptr; u = u->Next()) diff += u->value;
	for (u = b; u != nullptr; u = u->Next()) diff -= u->value;

	int r = ClampTo<int32_t>(diff);
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by their length */
static bool VehicleLengthSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->GetGroundVehicleCache()->cached_total_length - b->GetGroundVehicleCache()->cached_total_length;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by the time they can still live */
static bool VehicleTimeToLiveSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = ClampTo<int32_t>((a->max_age - a->age) - (b->max_age - b->age));
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by the timetable delay */
static bool VehicleTimetableDelaySorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->lateness_counter - b->lateness_counter;
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by the average order occupancy */
static bool VehicleAverageOrderOccupancySorter(const Vehicle * const &a, const Vehicle * const &b)
{
	int r = a->GetOrderOccupancyAverage() - b->GetOrderOccupancyAverage();
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

/** Sort vehicles by the max speed (fully loaded) */
static bool VehicleMaxSpeedLoadedSorter(const Vehicle * const &a, const Vehicle * const &b)
{
	auto get_max_speed_loaded = [](const Train * const v) -> int {
		auto res = _vehicle_max_speed_loaded.insert({ v->index, 0 });
		if (!res.second) {
			/* This vehicle's speed was already in _vehicle_max_speed_loaded */
			return res.first->second;
		}
		int loaded_weight = 0;
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			loaded_weight += u->GetWeightWithoutCargo() + u->GetCargoWeight(u->cargo_cap);
		}

		int loaded_max_speed = GetTrainEstimatedMaxAchievableSpeed(v, loaded_weight, v->GetDisplayMaxSpeed());
		res.first->second = loaded_max_speed;
		return loaded_max_speed;
	};

	int r = get_max_speed_loaded(Train::From(a)) - get_max_speed_loaded(Train::From(b));
	return (r != 0) ? r < 0 : VehicleNumberSorter(a, b);
}

void InitializeGUI()
{
	MemSetT(&_grouping, 0);
	MemSetT(&_sorting, 0);
}

/**
 * Assign a vehicle window a new vehicle
 * @param window_class WindowClass to search for
 * @param from_index the old vehicle ID
 * @param to_index the new vehicle ID
 */
static inline void ChangeVehicleWindow(WindowClass window_class, VehicleID from_index, VehicleID to_index)
{
	Window *w = FindWindowById(window_class, from_index);
	if (w != nullptr) {
		/* Update window_number */
		w->window_number = to_index;
		if (w->viewport != nullptr) w->viewport->follow_vehicle = to_index;

		/* Update vehicle drag data */
		if (_thd.window_class == window_class && _thd.window_number == (WindowNumber)from_index) {
			_thd.window_number = to_index;
		}

		/* Notify the window. */
		w->InvalidateData(VIWD_AUTOREPLACE, false);
	}
}

/**
 * Report a change in vehicle IDs (due to autoreplace) to affected vehicle windows.
 * @param from_index the old vehicle ID
 * @param to_index the new vehicle ID
 */
void ChangeVehicleViewWindow(VehicleID from_index, VehicleID to_index)
{
	ChangeVehicleWindow(WC_VEHICLE_VIEW,      from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_ORDERS,    from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_REFIT,     from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_DETAILS,   from_index, to_index);
	ChangeVehicleWindow(WC_VEHICLE_TIMETABLE, from_index, to_index);
	ChangeFixedViewportRoutePath(from_index, to_index);
}

static const NWidgetPart _nested_vehicle_list[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VL_CAPTION_SELECTION),
			NWidget(WWT_CAPTION, COLOUR_GREY, WID_VL_CAPTION),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_CAPTION, COLOUR_GREY, WID_VL_CAPTION_SHARED_ORDERS),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VL_ORDER_VIEW), SetMinimalSize(61, 14), SetDataTip(STR_GOTO_ORDER_VIEW, STR_GOTO_ORDER_VIEW_TOOLTIP),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_VL_GROUP_ORDER), SetMinimalSize(0, 12), SetFill(1, 1), SetDataTip(STR_STATION_VIEW_GROUP, STR_TOOLTIP_GROUP_ORDER),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VL_SORT_ORDER), SetMinimalSize(0, 12), SetFill(1, 1), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VL_GROUP_BY_PULLDOWN), SetMinimalSize(0, 12), SetFill(1, 0), SetDataTip(0x0, STR_TOOLTIP_GROUP_ORDER),
			NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VL_SORT_BY_PULLDOWN), SetMinimalSize(0, 12), SetFill(1, 0), SetDataTip(0x0, STR_TOOLTIP_SORT_CRITERIA),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetFill(1, 1), SetResize(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VL_FILTER_BY_CARGO_SEL),
					NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VL_FILTER_BY_CARGO), SetMinimalSize(0, 12), SetFill(0, 0), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_FILTER_CRITERIA),
				EndContainer(),
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetFill(1, 1), SetResize(1, 0), EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_VL_LIST), SetMinimalSize(248, 0), SetFill(1, 0), SetResize(1, 1), SetMatrixDataTip(1, 0, STR_NULL), SetScrollbar(WID_VL_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VL_SCROLLBAR),
	EndContainer(),

	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VL_HIDE_BUTTONS),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VL_AVAILABLE_VEHICLES), SetMinimalSize(106, 12), SetFill(0, 1),
								SetDataTip(STR_JUST_STRING, STR_VEHICLE_LIST_AVAILABLE_ENGINES_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetResize(1, 0), SetFill(1, 1), EndContainer(),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VL_MANAGE_VEHICLES_DROPDOWN), SetMinimalSize(118, 12), SetFill(0, 1),
								SetDataTip(STR_VEHICLE_LIST_MANAGE_LIST, STR_VEHICLE_LIST_MANAGE_LIST_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VL_STOP_ALL), SetMinimalSize(12, 12), SetFill(0, 1),
								SetDataTip(SPR_FLAG_VEH_STOPPED, STR_VEHICLE_LIST_MASS_STOP_LIST_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VL_START_ALL), SetMinimalSize(12, 12), SetFill(0, 1),
								SetDataTip(SPR_FLAG_VEH_RUNNING, STR_VEHICLE_LIST_MASS_START_LIST_TOOLTIP),
			EndContainer(),
			/* Widget to be shown for other companies hiding the previous 5 widgets. */
			NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), SetResize(1, 0), EndContainer(),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

static void DrawSmallOrderList(const Vehicle *v, int left, int right, int y, uint order_arrow_width, VehicleOrderID start)
{
	const Order *order = v->GetOrder(start);
	if (order == nullptr) return;

	bool rtl = _current_text_dir == TD_RTL;
	int l_offset = rtl ? 0 : order_arrow_width;
	int r_offset = rtl ? order_arrow_width : 0;
	int i = 0;
	VehicleOrderID oid = start;

	do {
		if (oid == v->cur_real_order_index) DrawString(left, right, y, STR_JUST_RIGHT_ARROW, TC_BLACK, SA_LEFT, false, FS_SMALL);

		if (order->IsType(OT_GOTO_STATION)) {
			SetDParam(0, order->GetDestination());
			DrawString(left + l_offset, right - r_offset, y, STR_STATION_NAME, TC_BLACK, SA_LEFT, false, FS_SMALL);

			y += FONT_HEIGHT_SMALL;
			if (++i == 4) break;
		}

		oid++;
		order = order->next;
		if (order == nullptr) {
			order = v->orders->GetFirstOrder();
			oid = 0;
		}
	} while (oid != start);
}

/** Draw small order list in the vehicle GUI, but without the little black arrow.  This is used for shared order groups. */
static void DrawSmallOrderList(const Order *order, int left, int right, int y, uint order_arrow_width)
{
	bool rtl = _current_text_dir == TD_RTL;
	int l_offset = rtl ? 0 : order_arrow_width;
	int r_offset = rtl ? order_arrow_width : 0;
	int i = 0;
	while (order != nullptr) {
		if (order->IsType(OT_GOTO_STATION)) {
			SetDParam(0, order->GetDestination());
			DrawString(left + l_offset, right - r_offset, y, STR_STATION_NAME, TC_BLACK, SA_LEFT, false, FS_SMALL);

			y += FONT_HEIGHT_SMALL;
			if (++i == 4) break;
		}
		order = order->next;
	}
}

/**
 * Draws an image of a vehicle chain
 * @param v         Front vehicle
 * @param r         Rect to draw at
 * @param selection Selected vehicle to draw a frame around
 * @param skip      Number of pixels to skip at the front (for scrolling)
 */
void DrawVehicleImage(const Vehicle *v, const Rect &r, VehicleID selection, EngineImageType image_type, int skip)
{
	switch (v->type) {
		case VEH_TRAIN:    DrawTrainImage(Train::From(v), r, selection, image_type, skip); break;
		case VEH_ROAD:     DrawRoadVehImage(v, r, selection, image_type, skip);  break;
		case VEH_SHIP:     DrawShipImage(v, r, selection, image_type);     break;
		case VEH_AIRCRAFT: DrawAircraftImage(v, r, selection, image_type); break;
		default: NOT_REACHED();
	}
}

/**
 * Get the height of a vehicle in the vehicle list GUIs.
 * @param type    the vehicle type to look at
 * @param divisor the resulting height must be dividable by this
 * @return the height
 */
uint GetVehicleListHeight(VehicleType type, uint divisor)
{
	/* Name + vehicle + profit */
	uint base = ScaleGUITrad(GetVehicleHeight(type)) + 2 * FONT_HEIGHT_SMALL + ScaleGUITrad(1);
	/* Drawing of the 4 small orders + profit*/
	if (type >= VEH_SHIP) base = std::max(base, 5U * FONT_HEIGHT_SMALL + ScaleGUITrad(1));

	if (divisor == 1) return base;

	/* Make sure the height is dividable by divisor */
	uint rem = base % divisor;
	return base + (rem == 0 ? 0 : divisor - rem);
}

/**
 * Draw all the vehicle list items.
 * @param selected_vehicle The vehicle that is to be highlighted.
 * @param line_height      Height of a single item line.
 * @param r                Rectangle with edge positions of the matrix widget.
 */
void BaseVehicleListWindow::DrawVehicleListItems(VehicleID selected_vehicle, int line_height, const Rect &r) const
{
	Rect ir = r.WithHeight(line_height).Shrink(WidgetDimensions::scaled.matrix, RectPadding::zero);
	bool rtl = _current_text_dir == TD_RTL;

	Dimension profit = GetSpriteSize(SPR_PROFIT_LOT);
	int text_offset = std::max<int>(profit.width, GetDigitWidth() * this->unitnumber_digits) + WidgetDimensions::scaled.hsep_normal;
	Rect tr = ir.Indent(text_offset, rtl);

	bool show_orderlist = this->vli.vtype >= VEH_SHIP;
	Rect olr = ir.Indent(std::max(ScaleGUITrad(100) + text_offset, ir.Width() / 2), rtl);

	int image_left  = (rtl && show_orderlist) ? olr.right : tr.left;
	int image_right = (!rtl && show_orderlist) ? olr.left : tr.right;

	int vehicle_button_x = rtl ? ir.right - profit.width : ir.left;

	uint max = static_cast<uint>(std::min<size_t>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->vehgroups.size()));
	for (uint i = this->vscroll->GetPosition(); i < max; ++i) {
		const GUIVehicleGroup &vehgroup = this->vehgroups[i];
		if (this->grouping == GB_NONE) {
			const Vehicle *v = vehgroup.GetSingleVehicle();

			SetDParam(0, STR_VEHICLE_LIST_PROFIT_THIS_YEAR_LAST_YEAR);
			SetDParam(1, v->GetDisplayProfitThisYear());
			SetDParam(2, v->GetDisplayProfitLastYear());

			StringID str;
			switch (this->vehgroups.SortType()) {
				case VST_AGE: {
					str = (v->age + DAYS_IN_YEAR < v->max_age) ? STR_VEHICLE_LIST_AGE : STR_VEHICLE_LIST_AGE_RED;
					SetDParam(3, v->age / DAYS_IN_LEAP_YEAR);
					SetDParam(4, v->max_age / DAYS_IN_LEAP_YEAR);
					break;
				}

				case VST_CARGO: {
					CargoTypes cargoes = 0;
					for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
						if (u->cargo_cap > 0) SetBit(cargoes, u->cargo_type);
					}
					str = STR_VEHICLE_LIST_CARGO_LIST;
					SetDParam(3, cargoes);
					break;
				}

				case VST_RELIABILITY: {
					str = ToPercent16(v->reliability) >= 50 ? STR_VEHICLE_LIST_RELIABILITY : STR_VEHICLE_LIST_RELIABILITY_RED;
					SetDParam(3, ToPercent16(v->reliability));
					break;
				}

				case VST_MAX_SPEED: {
					str = STR_VEHICLE_LIST_MAX_SPEED;
					SetDParam(3, v->GetDisplayMaxSpeed());
					break;
				}

				case VST_MODEL: {
					str = STR_VEHICLE_LIST_ENGINE_BUILT;
					SetDParam(3, v->engine_type);
					SetDParam(4, v->build_year);
					break;
				}

				case VST_VALUE: {
					Money total_value = 0;
					for (const Vehicle *u = v; u != nullptr; u = u->GetNextVehicle()) {
						total_value += u->value;
					}
					str = STR_VEHICLE_LIST_VALUE;
					SetDParam(3, total_value);
					break;
				}

				case VST_LENGTH: {
					const GroundVehicleCache* gcache = v->GetGroundVehicleCache();
					assert(gcache != nullptr);
					str = STR_VEHICLE_LIST_LENGTH;
					SetDParam(3, CeilDiv(gcache->cached_total_length * 10, TILE_SIZE));
					SetDParam(4, 1);
					break;
				}

				case VST_TIME_TO_LIVE: {
					auto years_remaining = (v->max_age / DAYS_IN_LEAP_YEAR) - (v->age / DAYS_IN_LEAP_YEAR);
					str = (years_remaining > 1) ? STR_VEHICLE_LIST_TIME_TO_LIVE : ((years_remaining < 0) ? STR_VEHICLE_LIST_TIME_TO_LIVE_OVERDUE : STR_VEHICLE_LIST_TIME_TO_LIVE_RED);
					SetDParam(3, std::abs(years_remaining));
					break;
				}

				case VST_TIMETABLE_DELAY: {
					if (v->lateness_counter == 0 || (!_settings_client.gui.timetable_in_ticks && v->lateness_counter / DATE_UNIT_SIZE == 0)) {
						str = STR_VEHICLE_LIST_TIMETABLE_DELAY_ON_TIME;
					} else {
						str = v->lateness_counter > 0 ? STR_VEHICLE_LIST_TIMETABLE_DELAY_LATE : STR_VEHICLE_LIST_TIMETABLE_DELAY_EARLY;
						SetTimetableParams(3, std::abs(v->lateness_counter));
					}
					break;
				}

				case VST_PROFIT_LIFETIME: {
					str = STR_VEHICLE_LIST_PROFIT_THIS_YEAR_LAST_YEAR_LIFETIME;
					SetDParam(3, v->GetDisplayProfitLifetime());
					break;
				}

				case VST_AVERAGE_ORDER_OCCUPANCY: {
					uint8 occupancy_average = v->GetOrderOccupancyAverage();
					if (occupancy_average >= 16) {
						str = STR_VEHICLE_LIST_ORDER_OCCUPANCY_AVERAGE;
						SetDParam(3, occupancy_average - 16);
					} else {
						str = STR_JUST_STRING2;
					}
					break;
				}

				default: {
					str = STR_JUST_STRING2;
					break;
				}
			}

			DrawVehicleImage(v, {image_left, ir.top, image_right, ir.bottom}, selected_vehicle, EIT_IN_LIST, 0);
			DrawString(tr.left, tr.right, ir.top + line_height - FONT_HEIGHT_SMALL - WidgetDimensions::scaled.framerect.bottom - 1, str);

			/* company colour stripe along vehicle description row */
			if (_settings_client.gui.show_vehicle_list_company_colour && v->owner != this->vli.company) {
				byte ccolour = 0;
				Company *c = Company::Get(v->owner);
				if (c != nullptr) {
					ccolour = _colour_gradient[c->colour][6];
				}
				GfxFillRect((tr.right - 1) - (FONT_HEIGHT_SMALL - 2), ir.top + 1, tr.right - 1, (ir.top + 1) + (FONT_HEIGHT_SMALL - 2), ccolour, FILLRECT_OPAQUE);
			}
		} else {
			SetDParam(0, vehgroup.GetDisplayProfitThisYear());
			SetDParam(1, vehgroup.GetDisplayProfitLastYear());
			DrawString(tr.left, tr.right, ir.bottom - FONT_HEIGHT_SMALL - WidgetDimensions::scaled.framerect.bottom, STR_VEHICLE_LIST_PROFIT_THIS_YEAR_LAST_YEAR);
		}

		DrawVehicleProfitButton(vehgroup.GetOldestVehicleAge(), vehgroup.GetDisplayProfitLastYear(), vehgroup.NumVehicles(), vehicle_button_x, ir.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal);

		switch (this->grouping) {
			case GB_NONE: {
				const Vehicle *v = vehgroup.GetSingleVehicle();

				if (HasBit(v->vehicle_flags, VF_PATHFINDER_LOST)) {
					DrawSprite(SPR_WARNING_SIGN, PAL_NONE, vehicle_button_x, ir.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal + profit.height);
				}

				DrawVehicleImage(v, {image_left, ir.top, image_right, ir.bottom}, selected_vehicle, EIT_IN_LIST, 0);

				if (_settings_client.gui.show_cargo_in_vehicle_lists) {
					/* Get the cargoes the vehicle can carry */
					CargoTypes vehicle_cargoes = 0;

					for (auto u = v; u != nullptr; u = u->Next()) {
						if (u->cargo_cap == 0) continue;

						SetBit(vehicle_cargoes, u->cargo_type);
					}

					if (!v->name.empty()) {
						/* The vehicle got a name so we will print it and the cargoes */
						SetDParam(0, STR_VEHICLE_NAME);
						SetDParam(1, v->index);
						SetDParam(2, STR_VEHICLE_LIST_CARGO);
						SetDParam(3, vehicle_cargoes);
						DrawString(tr.left, tr.right, ir.top, STR_VEHICLE_LIST_NAME_AND_CARGO, TC_BLACK, SA_LEFT, false, FS_SMALL);
					} else if (v->group_id != DEFAULT_GROUP) {
						/* The vehicle has no name, but is member of a group, so print group name and the cargoes */
						SetDParam(0, STR_GROUP_NAME);
						SetDParam(1, v->group_id);
						SetDParam(2, STR_VEHICLE_LIST_CARGO);
						SetDParam(3, vehicle_cargoes);
						DrawString(tr.left, tr.right, ir.top, STR_VEHICLE_LIST_NAME_AND_CARGO, TC_BLACK, SA_LEFT, false, FS_SMALL);
					} else {
						/* The vehicle has no name, and is not a member of a group, so just print the cargoes */
						SetDParam(0, vehicle_cargoes);
						DrawString(tr.left, tr.right, ir.top, STR_VEHICLE_LIST_CARGO, TC_BLACK, SA_LEFT, false, FS_SMALL);
					}
				} else if (!v->name.empty()) {
					/* The vehicle got a name so we will print it */
					SetDParam(0, v->index);
					DrawString(tr.left, tr.right, ir.top, STR_VEHICLE_NAME, TC_BLACK, SA_LEFT, false, FS_SMALL);
				} else if (v->group_id != DEFAULT_GROUP) {
					/* The vehicle has no name, but is member of a group, so print group name */
					SetDParam(0, v->group_id | GROUP_NAME_HIERARCHY);
					DrawString(tr.left, tr.right, ir.top, STR_GROUP_NAME, TC_BLACK, SA_LEFT, false, FS_SMALL);
				}

				if (show_orderlist) DrawSmallOrderList(v, olr.left, olr.right, ir.top, this->order_arrow_width, v->cur_real_order_index);

				TextColour tc;
				if (v->IsChainInDepot()) {
					tc = TC_BLUE;
				} else {
					tc = (v->age > v->max_age - DAYS_IN_LEAP_YEAR) ? TC_RED : TC_BLACK;
				}

				SetDParam(0, v->unitnumber);
				DrawString(ir.left, ir.right, ir.top + WidgetDimensions::scaled.framerect.top, STR_JUST_COMMA, tc);
				break;
			}

			case GB_SHARED_ORDERS:
				assert(vehgroup.NumVehicles() > 0);

				for (int i = 0; i < static_cast<int>(vehgroup.NumVehicles()); ++i) {
					if (image_left + WidgetDimensions::scaled.hsep_wide * i >= image_right) break; // Break if there is no more space to draw any more vehicles anyway.
					DrawVehicleImage(vehgroup.vehicles_begin[i], {image_left + WidgetDimensions::scaled.hsep_wide * i, ir.top, image_right, ir.bottom}, selected_vehicle, EIT_IN_LIST, 0);
				}

				if (vehgroup.vehicles_begin[0]->group_id != DEFAULT_GROUP) {
					/* If all vehicles are in the same group, print group name */
					GroupID gid = vehgroup.vehicles_begin[0]->group_id;
					bool show_group = true;
					for (int i = 1; i < static_cast<int>(vehgroup.NumVehicles()); ++i) {
						if (vehgroup.vehicles_begin[i]->group_id != gid) {
							show_group = false;
							break;
						}
					}
					if (show_group) {
						SetDParam(0, gid | GROUP_NAME_HIERARCHY);
						DrawString(tr.left, tr.right, ir.top, STR_GROUP_NAME, TC_BLACK, SA_LEFT, false, FS_SMALL);
					}
				}

				if (show_orderlist) DrawSmallOrderList((vehgroup.vehicles_begin[0])->GetFirstOrder(), olr.left, olr.right, ir.top, this->order_arrow_width);

				SetDParam(0, vehgroup.NumVehicles());
				DrawString(ir.left, ir.right, ir.top + WidgetDimensions::scaled.framerect.top, STR_JUST_COMMA, TC_BLACK);
				break;

			default:
				NOT_REACHED();
		}

		ir = ir.Translate(0, line_height);
	}
}

void BaseVehicleListWindow::UpdateSortingInterval()
{
	uint16 resort_interval = DAY_TICKS * 10;
	if (this->grouping == GB_NONE && this->vehgroups.SortType() == VST_TIMETABLE_DELAY) resort_interval = DAY_TICKS;
	this->vehgroups.SetResortInterval(resort_interval);
}

void BaseVehicleListWindow::UpdateSortingFromGrouping()
{
	/* Set up sorting. Make the window-specific _sorting variable
	 * point to the correct global _sorting struct so we are freed
	 * from having conditionals during window operation */
	switch (this->vli.vtype) {
		case VEH_TRAIN:    this->sorting = &_sorting[this->grouping].train; break;
		case VEH_ROAD:     this->sorting = &_sorting[this->grouping].roadveh; break;
		case VEH_SHIP:     this->sorting = &_sorting[this->grouping].ship; break;
		case VEH_AIRCRAFT: this->sorting = &_sorting[this->grouping].aircraft; break;
		default: NOT_REACHED();
	}
	this->vehgroups.SetSortFuncs(this->GetVehicleSorterFuncs());
	this->vehgroups.SetListing(*this->sorting);
	this->vehgroups.ForceRebuild();
	this->vehgroups.NeedResort();
	this->UpdateSortingInterval();
}

void BaseVehicleListWindow::UpdateVehicleGroupBy(GroupBy group_by)
{
	if (this->grouping != group_by) {
		/* Save the old sorting option, so that if we change the grouping option back later on,
		 * UpdateSortingFromGrouping() will automatically restore the saved sorting option. */
		*this->sorting = this->vehgroups.GetListing();

		this->grouping = group_by;
		_grouping[this->vli.type][this->vli.vtype] = group_by;
		this->UpdateSortingFromGrouping();
	}
}

uint BaseVehicleListWindow::GetSorterDisableMask(VehicleType type) const
{
	uint mask = 0;
	if (this->grouping == GB_NONE) {
		if (type != VEH_TRAIN && type != VEH_ROAD) mask |= (1 << VST_LENGTH);
		if (type != VEH_TRAIN || _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) mask |= (1 << VST_MAX_SPEED_LOADED);
	}
	return mask;
}

/**
 * Window for the (old) vehicle listing.
 *
 * bitmask for w->window_number
 * 0-7 CompanyID (owner)
 * 8-10 window type (use flags in vehicle_gui.h)
 * 11-15 vehicle type (using VEH_, but can be compressed to fewer bytes if needed)
 * 16-31 StationID or OrderID depending on window type (bit 8-10)
 */
struct VehicleListWindow : public BaseVehicleListWindow {
private:
	/** Enumeration of planes of the button row at the bottom. */
	enum ButtonPlanes {
		BP_SHOW_BUTTONS, ///< Show the buttons.
		BP_HIDE_BUTTONS, ///< Show the empty panel.
	};

	StringID GetChangeOrderStringID() const
	{
		if (VehicleListIdentifier::UnPack(this->window_number).type == VL_STATION_LIST) {
			return (BaseStation::Get(this->vli.index)->facilities & FACIL_WAYPOINT) ? STR_VEHICLE_LIST_CHANGE_ORDER_WAYPOINT : STR_VEHICLE_LIST_CHANGE_ORDER_STATION;
		} else if (VehicleListIdentifier::UnPack(this->window_number).type == VL_DEPOT_LIST) {
			return STR_VEHICLE_LIST_CHANGE_ORDER_TRAIN_DEPOT + this->vli.vtype;
		} else {
			return 0;
		}
	}

	/** Enumeration of planes of the title row at the top. */
	enum CaptionPlanes {
		BP_NORMAL,        ///< Show shared orders caption and buttons.
		BP_SHARED_ORDERS, ///< Show the normal caption.
	};

	void RefreshRouteOverlay() const
	{
		if (this->vli.type == VL_SHARED_ORDERS) {
			const Vehicle *v = Vehicle::GetIfValid(this->vli.index);
			MarkDirtyFocusedRoutePaths(v);
		}
	}

public:
	VehicleListWindow(WindowDesc *desc, WindowNumber window_number) : BaseVehicleListWindow(desc, window_number)
	{
		this->CreateNestedTree();

		this->CheckCargoFilterEnableState(WID_VL_FILTER_BY_CARGO_SEL, false, this->vli.type != VL_SHARED_ORDERS && this->vli.type != VL_SINGLE_VEH);

		this->vscroll = this->GetScrollbar(WID_VL_SCROLLBAR);

		/* Set up the window widgets */
		this->GetWidget<NWidgetCore>(WID_VL_LIST)->tool_tip = STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP + this->vli.vtype;

		NWidgetStacked *nwi = this->GetWidget<NWidgetStacked>(WID_VL_CAPTION_SELECTION);
		if (this->vli.type == VL_SHARED_ORDERS) {
			this->GetWidget<NWidgetCore>(WID_VL_CAPTION_SHARED_ORDERS)->widget_data = STR_VEHICLE_LIST_SHARED_ORDERS_LIST_CAPTION;
			/* If we are in the shared orders window, then disable the group-by dropdown menu.
			 * Remove this when the group-by dropdown menu has another option apart from grouping by shared orders. */
			this->SetWidgetDisabledState(WID_VL_GROUP_ORDER, true);
			this->SetWidgetDisabledState(WID_VL_GROUP_BY_PULLDOWN, true);
			nwi->SetDisplayedPlane(BP_SHARED_ORDERS);
		} else {
			this->GetWidget<NWidgetCore>(WID_VL_CAPTION)->widget_data = STR_VEHICLE_LIST_TRAIN_CAPTION + this->vli.vtype;
			nwi->SetDisplayedPlane(BP_NORMAL);
		}

		this->FinishInitNested(window_number);
		if (this->vli.company != OWNER_NONE) this->owner = this->vli.company;

		this->BuildVehicleList();
		this->SortVehicleList();
	}

	void Close(int data = 0) override
	{
		*this->sorting = this->vehgroups.GetListing();
		this->RefreshRouteOverlay();
		this->Window::Close();
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		this->RefreshRouteOverlay();
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		this->RefreshRouteOverlay();
	}

	void UpdateWidgetSize(int widget, Dimension *size, [[maybe_unused]] const Dimension &padding, [[maybe_unused]] Dimension *fill, [[maybe_unused]] Dimension *resize) override
	{
		switch (widget) {
			case WID_VL_LIST:
				resize->height = GetVehicleListHeight(this->vli.vtype, 1);

				switch (this->vli.vtype) {
					case VEH_TRAIN:
					case VEH_ROAD:
						size->height = 6 * resize->height;
						break;
					case VEH_SHIP:
					case VEH_AIRCRAFT:
						size->height = 4 * resize->height;
						break;
					default: NOT_REACHED();
				}
				break;

			case WID_VL_SORT_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + Window::SortButtonWidth() * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_VL_GROUP_BY_PULLDOWN:
				size->width = GetStringListWidth(this->vehicle_group_by_names) + padding.width;
				break;

			case WID_VL_SORT_BY_PULLDOWN:
				size->width = GetStringListWidth(this->vehicle_group_none_sorter_names);
				size->width = std::max(size->width, GetStringListWidth(this->vehicle_group_shared_orders_sorter_names));
				size->width += padding.width;
				break;

			case WID_VL_FILTER_BY_CARGO:
				size->width = GetStringListWidth(this->cargo_filter_texts) + padding.width;
				break;

			case WID_VL_MANAGE_VEHICLES_DROPDOWN: {
				Dimension d = this->GetActionDropdownSize(this->vli.type == VL_STANDARD, false,
						this->vli.vtype == VEH_TRAIN, this->GetChangeOrderStringID());
				d.height += padding.height;
				d.width  += padding.width;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_VL_AVAILABLE_VEHICLES:
				SetDParam(0, STR_VEHICLE_LIST_AVAILABLE_TRAINS + this->vli.vtype);
				break;

			case WID_VL_FILTER_BY_CARGO:
				SetDParam(0, this->cargo_filter_texts[this->cargo_filter_criteria]);
				break;

			case WID_VL_CAPTION:
			case WID_VL_CAPTION_SHARED_ORDERS: {
				switch (this->vli.type) {
					case VL_SHARED_ORDERS: // Shared Orders
						SetDParam(0, this->vehicles.size());
						break;

					case VL_STANDARD: // Company Name
						SetDParam(0, STR_COMPANY_NAME);
						SetDParam(1, this->vli.index);
						SetDParam(3, this->vehicles.size());
						break;

					case VL_STATION_LIST: // Station/Waypoint Name
						SetDParam(0, Station::IsExpected(BaseStation::Get(this->vli.index)) ? STR_STATION_NAME : STR_WAYPOINT_NAME);
						SetDParam(1, this->vli.index);
						SetDParam(3, this->vehicles.size());
						break;

					case VL_DEPOT_LIST:
						SetDParam(0, STR_DEPOT_CAPTION);
						SetDParam(1, this->vli.vtype);
						SetDParam(2, this->vli.index);
						SetDParam(3, this->vehicles.size());
						break;
					default: NOT_REACHED();
				}
				break;
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_VL_SORT_ORDER:
				/* draw arrow pointing up/down for ascending/descending sorting */
				this->DrawSortButtonState(widget, this->vehgroups.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_VL_LIST:
				this->DrawVehicleListItems(INVALID_VEHICLE, this->resize.step_height, r);
				break;
		}
	}

	void OnPaint() override
	{
		this->BuildVehicleList();
		this->SortVehicleList();

		if (_local_company != this->own_company) this->CountOwnVehicles();

		if (!this->ShouldShowActionDropdownList() && this->IsWidgetLowered(WID_VL_MANAGE_VEHICLES_DROPDOWN)) {
			HideDropDownMenu(this);
		}

		/* Hide the widgets that we will not use in this window
		 * Some windows contains actions only fit for the owner */
		bool show_buttons = this->owner == _local_company || (_local_company != INVALID_COMPANY && _settings_game.economy.infrastructure_sharing[this->vli.vtype]);
		int plane_to_show = show_buttons ? BP_SHOW_BUTTONS : BP_HIDE_BUTTONS;
		NWidgetStacked *nwi = this->GetWidget<NWidgetStacked>(WID_VL_HIDE_BUTTONS);
		if (plane_to_show != nwi->shown_plane) {
			nwi->SetDisplayedPlane(plane_to_show);
			nwi->SetDirty(this);
		}
		if (show_buttons) {
			this->SetWidgetDisabledState(WID_VL_AVAILABLE_VEHICLES, this->owner != _local_company || this->vli.type != VL_STANDARD);
			this->SetWidgetDisabledState(WID_VL_MANAGE_VEHICLES_DROPDOWN, !this->ShouldShowActionDropdownList());
			this->SetWidgetsDisabledState(this->owner != _local_company || this->vehicles.empty() || (this->vli.type == VL_STANDARD && _settings_client.gui.disable_top_veh_list_mass_actions),
				WID_VL_STOP_ALL,
				WID_VL_START_ALL);
		}

		/* Set text of group by dropdown widget. */
		this->GetWidget<NWidgetCore>(WID_VL_GROUP_BY_PULLDOWN)->widget_data = this->vehicle_group_by_names[this->grouping];

		/* Set text of sort by dropdown widget. */
		this->GetWidget<NWidgetCore>(WID_VL_SORT_BY_PULLDOWN)->widget_data = this->GetVehicleSorterNames()[this->vehgroups.SortType()];

		this->GetWidget<NWidgetCore>(WID_VL_FILTER_BY_CARGO)->widget_data = this->cargo_filter_texts[this->cargo_filter_criteria];

		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, int widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
		    case WID_VL_ORDER_VIEW: // Open the shared orders window
				assert(this->vli.type == VL_SHARED_ORDERS);
				assert(!this->vehicles.empty());
				ShowOrdersWindow(this->vehicles[0]);
				break;

			case WID_VL_SORT_ORDER: // Flip sorting method ascending/descending
				this->vehgroups.ToggleSortOrder();
				this->vehgroups.ForceResort();
				this->SetDirty();
				break;

			case WID_VL_GROUP_BY_PULLDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->vehicle_group_by_names, this->grouping, WID_VL_GROUP_BY_PULLDOWN, 0, 0);
				return;

			case WID_VL_SORT_BY_PULLDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->GetVehicleSorterNames(), this->vehgroups.SortType(), WID_VL_SORT_BY_PULLDOWN, 0,
						this->GetSorterDisableMask(this->vli.vtype), 0, DDSF_LOST_FOCUS);
				return;

			case WID_VL_FILTER_BY_CARGO: // Cargo filter dropdown
				ShowDropDownMenu(this, this->cargo_filter_texts, this->cargo_filter_criteria, WID_VL_FILTER_BY_CARGO, 0, 0);
				break;

			case WID_VL_LIST: { // Matrix to show vehicles
				auto it = this->vscroll->GetScrolledItemFromWidget(this->vehgroups, pt.y, this, WID_VL_LIST);
				if (it == this->vehgroups.end()) return; // click out of list bound

				const GUIVehicleGroup &vehgroup = *it;
				switch (this->grouping) {
					case GB_NONE: {
						const Vehicle *v = vehgroup.GetSingleVehicle();
						if (!VehicleClicked(v)) {
							if (_ctrl_pressed) {
								ShowCompanyGroupForVehicle(v);
							} else {
								ShowVehicleViewWindow(v);
							}
						}
						break;
					}

					case GB_SHARED_ORDERS: {
						assert(vehgroup.NumVehicles() > 0);
						if (!VehicleClicked(vehgroup)) {
							const Vehicle *v = vehgroup.vehicles_begin[0];
							if (_ctrl_pressed) {
								ShowOrdersWindow(v);
							} else {
								if (vehgroup.NumVehicles() == 1) {
									ShowVehicleViewWindow(v);
								} else {
									ShowVehicleListWindow(v);
								}
							}
						}
						break;
					}

					default: NOT_REACHED();
				}

				break;
			}

			case WID_VL_AVAILABLE_VEHICLES:
				ShowBuildVehicleWindow(INVALID_TILE, this->vli.vtype);
				break;

			case WID_VL_MANAGE_VEHICLES_DROPDOWN: {
				VehicleListIdentifier vli = VehicleListIdentifier::UnPack(this->window_number);
				DropDownList list = this->BuildActionDropdownList(vli.type == VL_STANDARD, false,
						this->vli.vtype == VEH_TRAIN, this->GetChangeOrderStringID(), true, vli.type == VL_STANDARD);
				ShowDropDownList(this, std::move(list), -1, WID_VL_MANAGE_VEHICLES_DROPDOWN);
				break;
			}

			case WID_VL_STOP_ALL:
			case WID_VL_START_ALL:
				DoCommandP(0, (1 << 1) | (widget == WID_VL_START_ALL ? (1 << 0) : 0) | (this->GetCargoFilter() << 8), this->window_number, CMD_MASS_START_STOP);
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_VL_GROUP_BY_PULLDOWN:
				this->UpdateVehicleGroupBy(static_cast<GroupBy>(index));
				break;

			case WID_VL_SORT_BY_PULLDOWN:
				this->vehgroups.SetSortType(index);
				this->UpdateSortingInterval();
				break;

			case WID_VL_FILTER_BY_CARGO:
				this->SetCargoFilterIndex(index);
				break;

			case WID_VL_MANAGE_VEHICLES_DROPDOWN:
				switch (index) {
					case ADI_REPLACE: // Replace window
						ShowReplaceGroupVehicleWindow(ALL_GROUP, this->vli.vtype);
						break;
					case ADI_TEMPLATE_REPLACE:
						if (vli.vtype == VEH_TRAIN) {
							ShowTemplateReplaceWindow();
						}
						break;
					case ADI_SERVICE: // Send for servicing
					case ADI_DEPOT: // Send to Depots
						DoCommandP(0, DEPOT_MASS_SEND | (index == ADI_SERVICE ? DEPOT_SERVICE : (DepotCommand)0) | this->GetCargoFilter(), this->window_number, GetCmdSendToDepot(this->vli.vtype));
						break;
					case ADI_CANCEL_DEPOT:
						DoCommandP(0, DEPOT_MASS_SEND | DEPOT_CANCEL | this->GetCargoFilter(), this->window_number, GetCmdSendToDepot(this->vli.vtype));
						break;

					case ADI_DEPOT_SELL:
						DoCommandP(0, DEPOT_MASS_SEND | DEPOT_SELL | this->GetCargoFilter(), this->window_number, GetCmdSendToDepot(this->vli.vtype));
						break;

					case ADI_CHANGE_ORDER:
						SetObjectToPlaceWnd(ANIMCURSOR_PICKSTATION, PAL_NONE, HT_RECT, this);
						break;

					case ADI_CREATE_GROUP:
						ShowQueryString(STR_EMPTY, STR_GROUP_RENAME_CAPTION, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
						break;

					case ADI_TRACERESTRICT_SLOT_MGMT: {
						extern void ShowTraceRestrictSlotWindow(CompanyID company, VehicleType vehtype);
						ShowTraceRestrictSlotWindow(this->owner, this->vli.vtype);
						break;
					}

					case ADI_TRACERESTRICT_COUNTER_MGMT: {
						extern void ShowTraceRestrictCounterWindow(CompanyID company);
						ShowTraceRestrictCounterWindow(this->owner);
						break;
					}

					default: NOT_REACHED();
				}
				break;

			default: NOT_REACHED();
		}
		this->SetDirty();
	}

	void OnQueryTextFinished(char *str) override
	{
		DoCommandP(0, this->window_number, this->GetCargoFilter(), CMD_CREATE_GROUP_FROM_LIST | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), nullptr, str);
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile) override
	{
		/* check depot first */
		if (IsDepotTile(tile) && GetDepotVehicleType(tile) == this->vli.vtype) {
			if (this->vli.type != VL_DEPOT_LIST) return;
			if (!IsInfraTileUsageAllowed(this->vli.vtype, this->vli.company, tile)) return;
			if (this->vli.vtype == VEH_ROAD && GetPresentRoadTramTypes(Depot::Get(this->vli.index)->xy) != GetPresentRoadTramTypes(tile)) return;

			DestinationID dest = (this->vli.vtype == VEH_AIRCRAFT) ? GetStationIndex(tile) : GetDepotIndex(tile);
			DoCommandP(0, this->vli.index | (this->vli.vtype << 16) | (OT_GOTO_DEPOT << 20) | (this->GetCargoFilter() << 24), dest, CMD_MASS_CHANGE_ORDER);
			ResetObjectToPlace();
			return;
		}

		/* check rail waypoint or buoy (no ownership) */
		if ((IsRailWaypointTile(tile) && this->vli.vtype == VEH_TRAIN && IsInfraTileUsageAllowed(VEH_TRAIN, this->vli.company, tile))
				|| (IsRoadWaypointTile(tile) && this->vli.vtype == VEH_ROAD && IsInfraTileUsageAllowed(VEH_ROAD, this->vli.company, tile))
				|| (IsBuoyTile(tile) && this->vli.vtype == VEH_SHIP)) {
			if (this->vli.type != VL_STATION_LIST) return;
			if (!(Station::Get(this->vli.index)->facilities & FACIL_WAYPOINT)) return;
			DoCommandP(0, this->vli.index | (this->vli.vtype << 16) | (OT_GOTO_WAYPOINT << 20) | (this->GetCargoFilter() << 24), GetStationIndex(tile), CMD_MASS_CHANGE_ORDER);
			ResetObjectToPlace();
			return;
		}

		if (IsTileType(tile, MP_STATION)) {
			if (this->vli.type != VL_STATION_LIST) return;
			if (BaseStation::Get(this->vli.index)->facilities & FACIL_WAYPOINT) return;

			StationID st_index = GetStationIndex(tile);
			const Station *st = Station::Get(st_index);

			if (!IsInfraUsageAllowed(this->vli.vtype, this->vli.company, st->owner)) return;

			if ((this->vli.vtype == VEH_SHIP && st->facilities & FACIL_DOCK) ||
					(this->vli.vtype == VEH_TRAIN && st->facilities & FACIL_TRAIN) ||
					(this->vli.vtype == VEH_AIRCRAFT && st->facilities & FACIL_AIRPORT) ||
					(this->vli.vtype == VEH_ROAD && st->facilities & (FACIL_BUS_STOP | FACIL_TRUCK_STOP))) {
				DoCommandP(0, this->vli.index | (this->vli.vtype << 16) | (OT_GOTO_STATION << 20) | (this->GetCargoFilter() << 24), GetStationIndex(tile), CMD_MASS_CHANGE_ORDER);
				ResetObjectToPlace();
				return;
			}
		}
	}

	void OnGameTick() override
	{
		if (this->vehgroups.NeedResort()) {
			StationID station = (this->vli.type == VL_STATION_LIST) ? this->vli.index : INVALID_STATION;

			DEBUG(misc, 3, "Periodic resort %d list company %d at station %d", this->vli.vtype, this->owner, station);
			this->SetDirty();
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_VL_LIST);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (!gui_scope && HasBit(data, 31) && this->vli.type == VL_SHARED_ORDERS) {
			/* Needs to be done in command-scope, so everything stays valid */
			this->vli.index = GB(data, 0, 20);
			this->window_number = this->vli.Pack();
			this->vehgroups.ForceRebuild();
			return;
		}

		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->vehgroups.ForceRebuild();
			if (this->vli.type == VL_SHARED_ORDERS && !_settings_client.gui.enable_single_veh_shared_order_gui && this->vehicles.size() == 1) {
				this->Close();
				return;
			}
		} else {
			this->vehgroups.ForceResort();
		}

		this->CheckCargoFilterEnableState(WID_VL_FILTER_BY_CARGO_SEL, true, this->vli.type != VL_SHARED_ORDERS && this->vli.type != VL_SINGLE_VEH);
	}
};

static WindowDesc _vehicle_list_other_desc(
	WDP_AUTO, "list_vehicles", 260, 246,
	WC_INVALID, WC_NONE,
	0,
	std::begin(_nested_vehicle_list), std::end(_nested_vehicle_list)
);

static WindowDesc _vehicle_list_train_desc(
	WDP_AUTO, "list_vehicles_train", 325, 246,
	WC_TRAINS_LIST, WC_NONE,
	0,
	std::begin(_nested_vehicle_list), std::end(_nested_vehicle_list)
);

static void ShowVehicleListWindowLocal(CompanyID company, VehicleListType vlt, VehicleType vehicle_type, uint32 unique_number)
{
	if (!Company::IsValidID(company) && company != OWNER_NONE) return;

	WindowNumber num = VehicleListIdentifier(vlt, vehicle_type, company, unique_number).Pack();
	if (vehicle_type == VEH_TRAIN) {
		AllocateWindowDescFront<VehicleListWindow>(&_vehicle_list_train_desc, num);
	} else {
		_vehicle_list_other_desc.cls = GetWindowClassForVehicleType(vehicle_type);
		AllocateWindowDescFront<VehicleListWindow>(&_vehicle_list_other_desc, num);
	}
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type)
{
	/* If _settings_client.gui.advanced_vehicle_list > 1, display the Advanced list
	 * if _settings_client.gui.advanced_vehicle_list == 1, display Advanced list only for local company
	 * if _ctrl_pressed, do the opposite action (Advanced list x Normal list)
	 */

	if ((_settings_client.gui.advanced_vehicle_list > (uint)(company != _local_company)) != _ctrl_pressed) {
		ShowCompanyGroup(company, vehicle_type);
	} else {
		ShowVehicleListWindowLocal(company, VL_STANDARD, vehicle_type, company);
	}
}

void ShowVehicleListWindow(const Vehicle *v)
{
	ShowVehicleListWindowLocal(v->owner, VL_SHARED_ORDERS, v->type, v->FirstShared()->index);
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, StationID station)
{
	ShowVehicleListWindowLocal(company, VL_STATION_LIST, vehicle_type, station);
}

void ShowVehicleListWindow(CompanyID company, VehicleType vehicle_type, TileIndex depot_tile)
{
	uint16 depot_airport_index;

	if (vehicle_type == VEH_AIRCRAFT) {
		depot_airport_index = GetStationIndex(depot_tile);
	} else {
		depot_airport_index = GetDepotIndex(depot_tile);
	}
	ShowVehicleListWindowLocal(company, VL_DEPOT_LIST, vehicle_type, depot_airport_index);
}

void DirtyVehicleListWindowForVehicle(const Vehicle *v)
{
	WindowClass cls = static_cast<WindowClass>(WC_TRAINS_LIST + v->type);
	WindowClass cls2 = (v->type == VEH_TRAIN) ? WC_TRACE_RESTRICT_SLOTS : cls;
	if (!HaveWindowByClass(cls) && !HaveWindowByClass(cls2)) return;
	for (Window *w : Window::Iterate()) {
		if (w->window_class == cls || w->window_class == cls2) {
			BaseVehicleListWindow *listwin = static_cast<BaseVehicleListWindow *>(w);
			uint max = std::min<uint>(listwin->vscroll->GetPosition() + listwin->vscroll->GetCapacity(), (uint)listwin->vehgroups.size());
			switch (listwin->grouping) {
				case BaseVehicleListWindow::GB_NONE:
					for (uint i = listwin->vscroll->GetPosition(); i < max; ++i) {
						if (v == listwin->vehgroups[i].vehicles_begin[0]) {
							listwin->SetWidgetDirty(0);
							break;
						}
					}
					break;

				case BaseVehicleListWindow::GB_SHARED_ORDERS: {
					const Vehicle *v_first_shared = v->FirstShared();
					for (uint i = listwin->vscroll->GetPosition(); i < max; ++i) {
						if (v_first_shared == listwin->vehgroups[i].vehicles_begin[0]->FirstShared()) {
							listwin->SetWidgetDirty(0);
							break;
						}
					}
					break;
				}

				default:
					NOT_REACHED();
			}
		}
	}
}

/* Unified vehicle GUI - Vehicle Details Window */

static_assert(WID_VD_DETAILS_CARGO_CARRIED    == WID_VD_DETAILS_CARGO_CARRIED + TDW_TAB_CARGO   );
static_assert(WID_VD_DETAILS_TRAIN_VEHICLES   == WID_VD_DETAILS_CARGO_CARRIED + TDW_TAB_INFO    );
static_assert(WID_VD_DETAILS_CAPACITY_OF_EACH == WID_VD_DETAILS_CARGO_CARRIED + TDW_TAB_CAPACITY);
static_assert(WID_VD_DETAILS_TOTAL_CARGO      == WID_VD_DETAILS_CARGO_CARRIED + TDW_TAB_TOTALS  );

/** Vehicle details widgets (other than train). */
static const NWidgetPart _nested_nontrain_vehicle_details_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VD_CAPTION), SetDataTip(STR_VEHICLE_DETAILS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VD_TOP_DETAILS), SetMinimalSize(405, 42), SetResize(1, 0), EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VD_MIDDLE_DETAILS), SetMinimalSize(405, 45), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_VD_DECREASE_SERVICING_INTERVAL), SetFill(0, 1),
				SetDataTip(AWV_DECREASE, STR_VEHICLE_DETAILS_DECREASE_SERVICING_INTERVAL_TOOLTIP),
		NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_VD_INCREASE_SERVICING_INTERVAL), SetFill(0, 1),
				SetDataTip(AWV_INCREASE, STR_VEHICLE_DETAILS_INCREASE_SERVICING_INTERVAL_TOOLTIP),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VD_SERVICE_INTERVAL_DROPDOWN), SetFill(0, 1),
				SetDataTip(STR_EMPTY, STR_SERVICE_INTERVAL_DROPDOWN_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VD_SERVICING_INTERVAL), SetFill(1, 1), SetResize(1, 0), EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/** Train details widgets. */
static const NWidgetPart _nested_train_vehicle_details_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VD_CAPTION), SetDataTip(STR_VEHICLE_DETAILS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_VD_TOP_DETAILS), SetResize(1, 0), SetMinimalSize(405, 42), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_VD_MATRIX), SetResize(1, 1), SetMinimalSize(393, 45), SetMatrixDataTip(1, 0, STR_NULL), SetFill(1, 0), SetScrollbar(WID_VD_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_VD_SCROLLBAR),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_VD_DECREASE_SERVICING_INTERVAL), SetFill(0, 1),
				SetDataTip(AWV_DECREASE, STR_VEHICLE_DETAILS_DECREASE_SERVICING_INTERVAL_TOOLTIP),
		NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_VD_INCREASE_SERVICING_INTERVAL), SetFill(0, 1),
				SetDataTip(AWV_INCREASE, STR_VEHICLE_DETAILS_INCREASE_SERVICING_INTERVAL_TOOLTIP),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_VD_SERVICE_INTERVAL_DROPDOWN), SetFill(0, 1),
				SetDataTip(STR_EMPTY, STR_SERVICE_INTERVAL_DROPDOWN_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_VD_SERVICING_INTERVAL), SetFill(1, 1), SetResize(1, 0), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VD_DETAILS_CARGO_CARRIED), SetMinimalSize(96, 12),
				SetDataTip(STR_VEHICLE_DETAIL_TAB_CARGO, STR_VEHICLE_DETAILS_TRAIN_CARGO_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VD_DETAILS_TRAIN_VEHICLES), SetMinimalSize(99, 12),
				SetDataTip(STR_VEHICLE_DETAIL_TAB_INFORMATION, STR_VEHICLE_DETAILS_TRAIN_INFORMATION_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VD_DETAILS_CAPACITY_OF_EACH), SetMinimalSize(99, 12),
				SetDataTip(STR_VEHICLE_DETAIL_TAB_CAPACITIES, STR_VEHICLE_DETAILS_TRAIN_CAPACITIES_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_VD_DETAILS_TOTAL_CARGO), SetMinimalSize(99, 12),
				SetDataTip(STR_VEHICLE_DETAIL_TAB_TOTAL_CARGO, STR_VEHICLE_DETAILS_TRAIN_TOTAL_CARGO_TOOLTIP), SetFill(1, 0), SetResize(1, 0),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};


extern int GetTrainDetailsWndVScroll(VehicleID veh_id, TrainDetailsWindowTabs det_tab);
extern void DrawTrainDetails(const Train *v, const Rect &r, int vscroll_pos, uint16 vscroll_cap, TrainDetailsWindowTabs det_tab);
extern void DrawRoadVehDetails(const Vehicle *v, const Rect &r);
extern void DrawShipDetails(const Vehicle *v, const Rect &r);
extern void DrawAircraftDetails(const Aircraft *v, const Rect &r);

static StringID _service_interval_dropdown[] = {
	STR_VEHICLE_DETAILS_DEFAULT,
	STR_VEHICLE_DETAILS_DAYS,
	STR_VEHICLE_DETAILS_PERCENT,
	INVALID_STRING_ID,
};

/** Class for managing the vehicle details window. */
struct VehicleDetailsWindow : Window {
	TrainDetailsWindowTabs tab; ///< For train vehicles: which tab is displayed.
	Scrollbar *vscroll;
	bool vehicle_group_line_shown;
	bool vehicle_weight_ratio_line_shown;
	bool vehicle_slots_line_shown;
	bool vehicle_speed_restriction_line_shown;
	bool vehicle_speed_adaptation_exempt_line_shown;

	/** Initialize a newly created vehicle details window */
	VehicleDetailsWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		const Vehicle *v = Vehicle::Get(window_number);

		this->CreateNestedTree();
		this->vscroll = (v->type == VEH_TRAIN ? this->GetScrollbar(WID_VD_SCROLLBAR) : nullptr);
		this->FinishInitNested(window_number);

		this->owner = v->owner;
		this->tab = TDW_TAB_CARGO;
		if (v->type == VEH_TRAIN && _shift_pressed) this->tab = TDW_TAB_TOTALS;
	}

	void Close(int data = 0) override
	{
		if (this->window_number != INVALID_VEHICLE) {
			if (!FocusWindowById(WC_VEHICLE_VIEW, this->window_number)) {
				if (this->window_number != INVALID_VEHICLE) {
					const Vehicle *v = Vehicle::Get(this->window_number);
					MarkDirtyFocusedRoutePaths(v);
				}
			}
		}
		this->Window::Close();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (data == VIWD_AUTOREPLACE) {
			/* Autoreplace replaced the vehicle.
			 * Nothing to do for this window. */
			return;
		}
		if (!gui_scope) return;
		const Vehicle *v = Vehicle::Get(this->window_number);
		if (v->type == VEH_ROAD || v->type == VEH_SHIP) {
			const NWidgetBase *nwid_info = this->GetWidget<NWidgetBase>(WID_VD_MIDDLE_DETAILS);
			uint aimed_height = this->GetRoadOrShipVehDetailsHeight(v);
			/* If the number of articulated parts changes, the size of the window must change too. */
			if (aimed_height != nwid_info->current_y) {
				this->ReInit();
			}
		}

		/* If the presence of the group line changes, the size of the top details widget must change */
		if (this->vehicle_group_line_shown != this->ShouldShowGroupLine(v)) {
			this->ReInit();
		}
	}

	/**
	 * Gets the desired height for the road vehicle and ship details panel.
	 * @param v Road vehicle being shown.
	 * @return Desired height in pixels.
	 */
	uint GetRoadOrShipVehDetailsHeight(const Vehicle *v)
	{
		uint desired_height;
		if (v->Next() != nullptr) {
			/* An articulated RV has its text drawn under the sprite instead of after it, hence 15 pixels extra. */
			desired_height = 4 * FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal * 2;
			if (v->type == VEH_ROAD) desired_height += ScaleGUITrad(15);
			/* Add space for the cargo amount for each part. */
			for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
				if (u->cargo_cap != 0) desired_height += FONT_HEIGHT_NORMAL;
			}
		} else {
			desired_height = 5 * FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal * 2;
		}
		return desired_height;
	}

	bool ShouldShowGroupLine(const Vehicle *v) const
	{
		return (_settings_client.gui.show_vehicle_group_in_details && v->group_id != INVALID_GROUP && v->group_id != DEFAULT_GROUP);
	}

	bool ShouldShowWeightRatioLine(const Vehicle *v) const
	{
		return (v->type == VEH_TRAIN && _settings_client.gui.show_train_weight_ratios_in_details);
	}

	bool ShouldShowSlotsLine(const Vehicle *v) const
	{
		return HasBit(v->vehicle_flags, VF_HAVE_SLOT);
	}

	bool ShouldShowSpeedRestrictionLine(const Vehicle *v) const
	{
		if (v->type != VEH_TRAIN) return false;
		return Train::From(v)->speed_restriction != 0;
	}

	bool ShouldShowSpeedAdaptationExemptLine(const Vehicle *v) const
	{
		if (v->type != VEH_TRAIN) return false;
		return HasBit(Train::From(v)->flags, VRF_SPEED_ADAPTATION_EXEMPT);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_VD_TOP_DETAILS: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				Dimension dim = { 0, 0 };
				this->vehicle_group_line_shown = ShouldShowGroupLine(v);
				this->vehicle_weight_ratio_line_shown = ShouldShowWeightRatioLine(v);
				this->vehicle_slots_line_shown = ShouldShowSlotsLine(v);
				this->vehicle_speed_restriction_line_shown = ShouldShowSpeedRestrictionLine(v);
				this->vehicle_speed_adaptation_exempt_line_shown = ShouldShowSpeedAdaptationExemptLine(v);
				int lines = 4;
				if (this->vehicle_group_line_shown) lines++;
				if (this->vehicle_weight_ratio_line_shown) lines++;
				if (this->vehicle_slots_line_shown) lines++;
				if (this->vehicle_speed_restriction_line_shown) lines++;
				if (this->vehicle_speed_adaptation_exempt_line_shown) lines++;
				size->height = lines * FONT_HEIGHT_NORMAL + padding.height;

				for (uint i = 0; i < 5; i++) SetDParamMaxValue(i, INT16_MAX);
				static const StringID info_strings[] = {
					STR_VEHICLE_INFO_MAX_SPEED,
					STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED,
					STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE,
					STR_VEHICLE_INFO_RELIABILITY_BREAKDOWNS
				};
				for (uint i = 0; i < lengthof(info_strings); i++) {
					dim = maxdim(dim, GetStringBoundingBox(info_strings[i]));
				}
				if (v->type == VEH_TRAIN && _settings_client.gui.show_train_length_in_details) {
					SetDParamMaxValue(0, _settings_game.vehicle.max_train_length * 10);
					SetDParam(1, 1);
					SetDParam(2, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR_LIFETIME);
					SetDParam(3, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR);
					for (uint i = 4; i < 7; i++) SetDParamMaxValue(i, 1 << 24);
					dim = maxdim(dim, GetStringBoundingBox(STR_VEHICLE_INFO_TRAIN_LENGTH));
				} else {
					SetDParam(0, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR);
					for (uint i = 1; i < 4; i++) SetDParamMaxValue(i, 1 << 24);
					dim = maxdim(dim, GetStringBoundingBox(STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR_LIFETIME));
				}
				if (this->vehicle_group_line_shown) {
					SetDParam(0, v->group_id | GROUP_NAME_HIERARCHY);
					dim = maxdim(dim, GetStringBoundingBox(STR_VEHICLE_INFO_GROUP));
				}
				if (this->vehicle_weight_ratio_line_shown) {
					SetDParam(0, STR_VEHICLE_INFO_POWER_WEIGHT_RATIO);
					SetDParamMaxValue(1, 1 << 16);
					SetDParam(2, (v->type != VEH_TRAIN || Train::From(v)->GetAccelerationType() == 2) ? STR_EMPTY : STR_VEHICLE_INFO_TE_WEIGHT_RATIO);
					SetDParamMaxValue(3, 1 << 16);
					dim = maxdim(dim, GetStringBoundingBox(STR_VEHICLE_INFO_WEIGHT_RATIOS));
				}
				SetDParam(0, STR_VEHICLE_INFO_AGE);
				dim = maxdim(dim, GetStringBoundingBox(STR_VEHICLE_INFO_AGE_RUNNING_COST_YR));
				size->width = dim.width + padding.width;
				break;
			}

			case WID_VD_MIDDLE_DETAILS: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				switch (v->type) {
					case VEH_ROAD:
					case VEH_SHIP:
						size->height = this->GetRoadOrShipVehDetailsHeight(v) + padding.height;
						break;

					case VEH_AIRCRAFT:
						size->height = 5 * FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_normal * 2 + padding.height;
						break;

					default:
						NOT_REACHED(); // Train uses WID_VD_MATRIX instead.
				}
				break;
			}

			case WID_VD_MATRIX:
				resize->height = std::max<uint>(ScaleGUITrad(14), FONT_HEIGHT_NORMAL + padding.height);
				size->height = 4 * resize->height;
				break;

			case WID_VD_SERVICE_INTERVAL_DROPDOWN: {
				Dimension d{0, 0};
				StringID *strs = _service_interval_dropdown;
				while (*strs != INVALID_STRING_ID) {
					d = maxdim(d, GetStringBoundingBox(*strs++));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_VD_SERVICING_INTERVAL:
				SetDParamMaxValue(0, MAX_SERVINT_DAYS); // Roughly the maximum interval
				SetDParamMaxValue(1, MAX_YEAR * DAYS_IN_YEAR); // Roughly the maximum year
				size->width = std::max(
					GetStringBoundingBox(STR_VEHICLE_DETAILS_SERVICING_INTERVAL_PERCENT).width,
					GetStringBoundingBox(STR_VEHICLE_DETAILS_SERVICING_INTERVAL_DAYS).width
				) + padding.width;
				size->height = FONT_HEIGHT_NORMAL + padding.height;
				break;
		}
	}

	/** Checks whether service interval is enabled for the vehicle. */
	static bool IsVehicleServiceIntervalEnabled(const VehicleType vehicle_type, CompanyID company_id)
	{
		const VehicleDefaultSettings *vds = &Company::Get(company_id)->settings.vehicle;
		switch (vehicle_type) {
			default: NOT_REACHED();
			case VEH_TRAIN:    return vds->servint_trains   != 0;
			case VEH_ROAD:     return vds->servint_roadveh  != 0;
			case VEH_SHIP:     return vds->servint_ships    != 0;
			case VEH_AIRCRAFT: return vds->servint_aircraft != 0;
		}
	}

	/**
	 * Draw the details for the given vehicle at the position of the Details windows
	 *
	 * @param v     current vehicle
	 * @param r     the Rect to draw within
	 * @param vscroll_pos Position of scrollbar (train only)
	 * @param vscroll_cap Number of lines currently displayed (train only)
	 * @param det_tab Selected details tab (train only)
	 */
	static void DrawVehicleDetails(const Vehicle *v, const Rect &r, int vscroll_pos, uint vscroll_cap, TrainDetailsWindowTabs det_tab)
	{
		switch (v->type) {
			case VEH_TRAIN:    DrawTrainDetails(Train::From(v), r, vscroll_pos, vscroll_cap, det_tab);  break;
			case VEH_ROAD:     DrawRoadVehDetails(v, r);  break;
			case VEH_SHIP:     DrawShipDetails(v, r);     break;
			case VEH_AIRCRAFT: DrawAircraftDetails(Aircraft::From(v), r); break;
			default: NOT_REACHED();
		}
	}

	void SetStringParameters(int widget) const override
	{
		if (widget == WID_VD_CAPTION) SetDParam(0, Vehicle::Get(this->window_number)->index);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);

		switch (widget) {
			case WID_VD_TOP_DETAILS: {
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

				/* Draw running cost */
				SetDParam(1, DateToYear(v->age));
				SetDParam(0, (v->age + DAYS_IN_YEAR < v->max_age) ? STR_VEHICLE_INFO_AGE : STR_VEHICLE_INFO_AGE_RED);
				SetDParam(2, DateToYear(v->max_age));
				SetDParam(3, v->GetDisplayRunningCost());
				DrawString(tr, STR_VEHICLE_INFO_AGE_RUNNING_COST_YR);
				tr.top += FONT_HEIGHT_NORMAL;

				/* Draw max speed */
				StringID string;
				if (v->type == VEH_TRAIN ||
						(v->type == VEH_ROAD && _settings_game.vehicle.roadveh_acceleration_model != AM_ORIGINAL)) {
					const GroundVehicleCache *gcache = v->GetGroundVehicleCache();
					SetDParam(2, PackVelocity(v->GetDisplayMaxSpeed(), v->type));
					SetDParam(1, gcache->cached_power);
					SetDParam(0, gcache->cached_weight);
					SetDParam(3, gcache->cached_max_te);
					if (v->type == VEH_TRAIN && (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ||
							GetRailTypeInfo(Train::From(v)->railtype)->acceleration_type == 2)) {
						string = STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED;
					} else {
						string = STR_VEHICLE_INFO_WEIGHT_POWER_MAX_SPEED_MAX_TE;
					}
				} else {
					SetDParam(0, PackVelocity(v->GetDisplayMaxSpeed(), v->type));
					if (v->type == VEH_AIRCRAFT) {
						SetDParam(1, v->GetEngine()->GetAircraftTypeText());
						if (Aircraft::From(v)->GetRange() > 0) {
							SetDParam(2, Aircraft::From(v)->GetRange());
							string = STR_VEHICLE_INFO_MAX_SPEED_TYPE_RANGE;
						} else {
							string = STR_VEHICLE_INFO_MAX_SPEED_TYPE;
						}
					} else {
						string = STR_VEHICLE_INFO_MAX_SPEED;
					}
				}
				DrawString(tr, string);
				tr.top += FONT_HEIGHT_NORMAL;

				bool should_show_weight_ratio = this->ShouldShowWeightRatioLine(v);
				if (should_show_weight_ratio) {
					SetDParam(0, STR_VEHICLE_INFO_POWER_WEIGHT_RATIO);
					SetDParam(1, (100 * Train::From(v)->gcache.cached_power) / std::max<uint>(1, Train::From(v)->gcache.cached_weight));
					SetDParam(2, Train::From(v)->GetAccelerationType() == 2 ? STR_EMPTY : STR_VEHICLE_INFO_TE_WEIGHT_RATIO);
					SetDParam(3, (100 * Train::From(v)->gcache.cached_max_te) / std::max<uint>(1, Train::From(v)->gcache.cached_weight));
					DrawString(tr, STR_VEHICLE_INFO_WEIGHT_RATIOS);
					tr.top += FONT_HEIGHT_NORMAL;
				}

				/* Draw profit */
				if (v->type == VEH_TRAIN && _settings_client.gui.show_train_length_in_details) {
					const GroundVehicleCache *gcache = v->GetGroundVehicleCache();
					SetDParam(0, CeilDiv(gcache->cached_total_length * 10, TILE_SIZE));
					SetDParam(1, 1);
					SetDParam(2, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR_LIFETIME);
					SetDParam(3, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR);
					SetDParam(4, v->GetDisplayProfitThisYear());
					SetDParam(5, v->GetDisplayProfitLastYear());
					SetDParam(6, v->GetDisplayProfitLifetime());
					DrawString(tr, STR_VEHICLE_INFO_TRAIN_LENGTH);
				} else {
					SetDParam(0, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR);
					SetDParam(1, v->GetDisplayProfitThisYear());
					SetDParam(2, v->GetDisplayProfitLastYear());
					SetDParam(3, v->GetDisplayProfitLifetime());
					DrawString(tr, STR_VEHICLE_INFO_PROFIT_THIS_YEAR_LAST_YEAR_LIFETIME);
				}
				tr.top += FONT_HEIGHT_NORMAL;

				/* Draw breakdown & reliability */
				byte total_engines = 0;
				if (v->type == VEH_TRAIN) {
					/* we want to draw the average reliability and total number of breakdowns */
					uint32 total_reliability = 0;
					uint16 total_breakdowns  = 0;
					for (const Vehicle *w = v; w != nullptr; w = w->Next()) {
						if (Train::From(w)->IsEngine() || Train::From(w)->IsMultiheaded()) {
							total_reliability += w->reliability;
							total_breakdowns += w->breakdowns_since_last_service;
						}
					}
					total_engines = Train::From(v)->tcache.cached_num_engines;
					assert(total_engines > 0);
					SetDParam(0, ToPercent16(total_reliability / total_engines));
					SetDParam(1, total_breakdowns);
				} else {
					SetDParam(0, ToPercent16(v->reliability));
					SetDParam(1, v->breakdowns_since_last_service);
				}
				DrawString(tr, STR_VEHICLE_INFO_RELIABILITY_BREAKDOWNS);
				tr.top += FONT_HEIGHT_NORMAL;

				bool should_show_group = this->ShouldShowGroupLine(v);
				if (should_show_group) {
					SetDParam(0, v->group_id | GROUP_NAME_HIERARCHY);
					DrawString(tr, STR_VEHICLE_INFO_GROUP);
					tr.top += FONT_HEIGHT_NORMAL;
				}

				bool should_show_slots = this->ShouldShowSlotsLine(v);
				if (should_show_slots) {
					std::vector<TraceRestrictSlotID> slots;
					TraceRestrictGetVehicleSlots(v->index, slots);

					char text_buffer[512];
					char *buffer = text_buffer;
					const char * const last = lastof(text_buffer);
					SetDParam(0, slots.size());
					buffer = GetString(buffer, STR_TRACE_RESTRICT_SLOT_LIST_HEADER, last);

					for (size_t i = 0; i < slots.size(); i++) {
						if (i != 0) buffer = GetString(buffer, STR_TRACE_RESTRICT_SLOT_LIST_SEPARATOR, last);
						buffer = strecpy(buffer, TraceRestrictSlot::Get(slots[i])->name.c_str(), last);
					}
					SetDParamStr(0, text_buffer);
					DrawString(tr, STR_JUST_RAW_STRING);
					tr.top += FONT_HEIGHT_NORMAL;
				}

				bool should_show_speed_restriction = this->ShouldShowSpeedRestrictionLine(v);
				if (should_show_speed_restriction) {
					SetDParam(0, Train::From(v)->speed_restriction);
					DrawString(tr, STR_VEHICLE_INFO_SPEED_RESTRICTION);
					tr.top += FONT_HEIGHT_NORMAL;
				}

				bool should_show_speed_adaptation_exempt = this->ShouldShowSpeedAdaptationExemptLine(v);
				if (should_show_speed_adaptation_exempt) {
					DrawString(tr, STR_VEHICLE_INFO_SPEED_ADAPTATION_EXEMPT);
					tr.top += FONT_HEIGHT_NORMAL;
				}

				if (this->vehicle_weight_ratio_line_shown != should_show_weight_ratio ||
						this->vehicle_weight_ratio_line_shown != should_show_weight_ratio ||
						this->vehicle_slots_line_shown != should_show_slots ||
						this->vehicle_speed_restriction_line_shown != should_show_speed_restriction ||
						this->vehicle_speed_adaptation_exempt_line_shown != should_show_speed_adaptation_exempt) {
					const_cast<VehicleDetailsWindow *>(this)->ReInit();
				}
				break;
			}

			case WID_VD_MATRIX: {
				/* For trains only. */
				DrawVehicleDetails(v, r.Shrink(WidgetDimensions::scaled.matrix, RectPadding::zero).WithHeight(this->resize.step_height), this->vscroll->GetPosition(), this->vscroll->GetCapacity(), this->tab);
				break;
			}

			case WID_VD_MIDDLE_DETAILS: {
				/* For other vehicles, at the place of the matrix. */
				bool rtl = _current_text_dir == TD_RTL;
				uint sprite_width = GetSingleVehicleWidth(v, EIT_IN_DETAILS) + WidgetDimensions::scaled.framerect.Horizontal();
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);

				/* Articulated road vehicles use a complete line. */
				if (v->type == VEH_ROAD && v->HasArticulatedPart()) {
					DrawVehicleImage(v, tr.WithHeight(ScaleGUITrad(GetVehicleHeight(v->type)), false), INVALID_VEHICLE, EIT_IN_DETAILS, 0);
				} else {
					Rect sr = tr.WithWidth(sprite_width, rtl);
					DrawVehicleImage(v, sr.WithHeight(ScaleGUITrad(GetVehicleHeight(v->type)), false), INVALID_VEHICLE, EIT_IN_DETAILS, 0);
				}

				DrawVehicleDetails(v, tr.Indent(sprite_width, rtl), 0, 0, this->tab);
				break;
			}

			case WID_VD_SERVICING_INTERVAL: {
				/* Draw service interval text */
				Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
				SetDParam(0, v->GetServiceInterval());
				SetDParam(1, v->date_of_last_service);
				DrawString(tr.left, tr.right, CenterBounds(r.top, r.bottom, FONT_HEIGHT_NORMAL),
						v->ServiceIntervalIsPercent() ? STR_VEHICLE_DETAILS_SERVICING_INTERVAL_PERCENT : STR_VEHICLE_DETAILS_SERVICING_INTERVAL_DAYS);
				break;
			}
		}
	}

	/** Repaint vehicle details window. */
	void OnPaint() override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);

		if (v->type == VEH_TRAIN) {
			this->LowerWidget(this->tab + WID_VD_DETAILS_CARGO_CARRIED);
			this->vscroll->SetCount(GetTrainDetailsWndVScroll(v->index, this->tab));
		}

		/* Disable service-scroller when interval is set to disabled */
		this->SetWidgetsDisabledState(!IsVehicleServiceIntervalEnabled(v->type, v->owner),
			WID_VD_INCREASE_SERVICING_INTERVAL,
			WID_VD_DECREASE_SERVICING_INTERVAL);

		StringID str = v->ServiceIntervalIsCustom() ?
			(v->ServiceIntervalIsPercent() ? STR_VEHICLE_DETAILS_PERCENT : STR_VEHICLE_DETAILS_DAYS) :
			STR_VEHICLE_DETAILS_DEFAULT;
		this->GetWidget<NWidgetCore>(WID_VD_SERVICE_INTERVAL_DROPDOWN)->widget_data = str;

		this->DrawWidgets();
	}

	void OnClick([[maybe_unused]] Point pt, int widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_VD_INCREASE_SERVICING_INTERVAL:   // increase int
			case WID_VD_DECREASE_SERVICING_INTERVAL: { // decrease int
				int mod = _ctrl_pressed ? 5 : 10;
				const Vehicle *v = Vehicle::Get(this->window_number);

				mod = (widget == WID_VD_DECREASE_SERVICING_INTERVAL) ? -mod : mod;
				mod = GetServiceIntervalClamped(mod + v->GetServiceInterval(), v->ServiceIntervalIsPercent());
				if (mod == v->GetServiceInterval()) return;

				DoCommandP(v->tile, v->index, mod | (1 << 16) | (v->ServiceIntervalIsPercent() << 17), CMD_CHANGE_SERVICE_INT | CMD_MSG(STR_ERROR_CAN_T_CHANGE_SERVICING));
				break;
			}

			case WID_VD_SERVICE_INTERVAL_DROPDOWN: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				ShowDropDownMenu(this, _service_interval_dropdown, v->ServiceIntervalIsCustom() ? (v->ServiceIntervalIsPercent() ? 2 : 1) : 0, widget, 0, 0, 0, DDSF_LOST_FOCUS);
				break;
			}

			case WID_VD_DETAILS_CARGO_CARRIED:
			case WID_VD_DETAILS_TRAIN_VEHICLES:
			case WID_VD_DETAILS_CAPACITY_OF_EACH:
			case WID_VD_DETAILS_TOTAL_CARGO:
				this->SetWidgetsLoweredState(false,
					WID_VD_DETAILS_CARGO_CARRIED,
					WID_VD_DETAILS_TRAIN_VEHICLES,
					WID_VD_DETAILS_CAPACITY_OF_EACH,
					WID_VD_DETAILS_TOTAL_CARGO);

				this->tab = (TrainDetailsWindowTabs)(widget - WID_VD_DETAILS_CARGO_CARRIED);
				this->SetDirty();
				break;
		}
	}

	void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_VD_SERVICE_INTERVAL_DROPDOWN: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				bool iscustom = index != 0;
				bool ispercent = iscustom ? (index == 2) : Company::Get(v->owner)->settings.vehicle.servint_ispercent;
				uint16 interval = GetServiceIntervalClamped(v->GetServiceInterval(), ispercent);
				DoCommandP(v->tile, v->index, interval | (iscustom << 16) | (ispercent << 17), CMD_CHANGE_SERVICE_INT | CMD_MSG(STR_ERROR_CAN_T_CHANGE_SERVICING));
				break;
			}
		}
	}

	void OnResize() override
	{
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_VD_MATRIX);
		if (nwi != nullptr) {
			this->vscroll->SetCapacityFromWidget(this, WID_VD_MATRIX);
		}
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}
};

/** Vehicle details window descriptor. */
static WindowDesc _train_vehicle_details_desc(
	WDP_AUTO, "view_vehicle_details_train", 405, 178,
	WC_VEHICLE_DETAILS, WC_VEHICLE_VIEW,
	0,
	std::begin(_nested_train_vehicle_details_widgets), std::end(_nested_train_vehicle_details_widgets)
);

/** Vehicle details window descriptor for other vehicles than a train. */
static WindowDesc _nontrain_vehicle_details_desc(
	WDP_AUTO, "view_vehicle_details", 405, 113,
	WC_VEHICLE_DETAILS, WC_VEHICLE_VIEW,
	0,
	std::begin(_nested_nontrain_vehicle_details_widgets), std::end(_nested_nontrain_vehicle_details_widgets)
);

/** Shows the vehicle details window of the given vehicle. */
static void ShowVehicleDetailsWindow(const Vehicle *v)
{
	CloseWindowById(WC_VEHICLE_ORDERS, v->index, false);
	CloseWindowById(WC_VEHICLE_TIMETABLE, v->index, false);
	AllocateWindowDescFront<VehicleDetailsWindow>((v->type == VEH_TRAIN) ? &_train_vehicle_details_desc : &_nontrain_vehicle_details_desc, v->index);
}


/* Unified vehicle GUI - Vehicle View Window */

/** Vehicle view widgets. */
static const NWidgetPart _nested_vehicle_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_RENAME), SetMinimalSize(12, 14), SetDataTip(SPR_RENAME, STR_NULL /* filled in later */),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_VV_CAPTION), SetDataTip(STR_VEHICLE_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_VV_LOCATION), SetMinimalSize(12, 14), SetDataTip(SPR_GOTO_LOCATION, STR_NULL /* filled in later */),
		NWidget(WWT_DEBUGBOX, COLOUR_GREY),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_DEFSIZEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(WWT_INSET, COLOUR_GREY), SetPadding(2, 2, 2, 2),
				NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_VV_VIEWPORT), SetMinimalSize(226, 84), SetResize(1, 1),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VV_SELECT_DEPOT_CLONE),
				NWidget(WWT_IMGBTN, COLOUR_GREY, WID_VV_GOTO_DEPOT), SetMinimalSize(18, 18), SetDataTip(0x0 /* filled later */, 0x0 /* filled later */),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_CLONE), SetMinimalSize(18, 18), SetDataTip(0x0 /* filled later */, 0x0 /* filled later */),
			EndContainer(),
			/* For trains only, 'ignore signal' button. */
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_FORCE_PROCEED), SetMinimalSize(18, 18),
											SetDataTip(SPR_IGNORE_SIGNALS, STR_VEHICLE_VIEW_TRAIN_IGNORE_SIGNAL_TOOLTIP),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_VV_SELECT_REFIT_TURN),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_REFIT), SetMinimalSize(18, 18), SetDataTip(SPR_REFIT_VEHICLE, 0x0 /* filled later */),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_TURN_AROUND), SetMinimalSize(18, 18),
												SetDataTip(SPR_FORCE_VEHICLE_TURN, STR_VEHICLE_VIEW_ROAD_VEHICLE_REVERSE_TOOLTIP),
			EndContainer(),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_SHOW_ORDERS), SetMinimalSize(18, 18), SetDataTip(SPR_SHOW_ORDERS, 0x0 /* filled later */),
			NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_SHOW_DETAILS), SetMinimalSize(18, 18), SetDataTip(SPR_SHOW_VEHICLE_DETAILS, 0x0 /* filled later */),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(18, 0), SetResize(0, 1), EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, WID_VV_START_STOP), SetResize(1, 0), SetFill(1, 0),
		NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_VV_ORDER_LOCATION), SetMinimalSize(12, 14), SetDataTip(SPR_GOTO_LOCATION, STR_VEHICLE_VIEW_ORDER_LOCATION_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/* Just to make sure, nobody has changed the vehicle type constants, as we are
	 using them for array indexing in a number of places here. */
static_assert(VEH_TRAIN == 0);
static_assert(VEH_ROAD == 1);
static_assert(VEH_SHIP == 2);
static_assert(VEH_AIRCRAFT == 3);

/** Zoom levels for vehicle views indexed by vehicle type. */
static const ZoomLevel _vehicle_view_zoom_levels[] = {
	ZOOM_LVL_TRAIN,
	ZOOM_LVL_ROADVEH,
	ZOOM_LVL_SHIP,
	ZOOM_LVL_AIRCRAFT,
};

/* Constants for geometry of vehicle view viewport */
static const int VV_INITIAL_VIEWPORT_WIDTH = 226;
static const int VV_INITIAL_VIEWPORT_HEIGHT = 84;
static const int VV_INITIAL_VIEWPORT_HEIGHT_TRAIN = 102;

/** Command indices for the _vehicle_command_translation_table. */
enum VehicleCommandTranslation {
	VCT_CMD_START_STOP = 0,
	VCT_CMD_CLONE_VEH,
	VCT_CMD_TURN_AROUND,
};

/** Command codes for the shared buttons indexed by VehicleCommandTranslation and vehicle type. */
static const uint32 _vehicle_command_translation_table[][4] = {
	{ // VCT_CMD_START_STOP
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_STOP_START_TRAIN),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_STOP_START_ROAD_VEHICLE),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_STOP_START_SHIP),
		CMD_START_STOP_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_STOP_START_AIRCRAFT)
	},
	{ // VCT_CMD_CLONE_VEH
		CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_TRAIN),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_ROAD_VEHICLE),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_SHIP),
		CMD_CLONE_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_BUY_AIRCRAFT)
	},
	{ // VCT_CMD_TURN_AROUND
		CMD_REVERSE_TRAIN_DIRECTION | CMD_MSG(STR_ERROR_CAN_T_REVERSE_DIRECTION_TRAIN),
		CMD_TURN_ROADVEH            | CMD_MSG(STR_ERROR_CAN_T_MAKE_ROAD_VEHICLE_TURN),
		0xffffffff, // invalid for ships
		0xffffffff  // invalid for aircraft
	},
};

/**
 * This is the Callback method after attempting to start/stop a vehicle
 * @param result the result of the start/stop command
 * @param tile unused
 * @param p1 vehicle ID
 * @param p2 unused
 */
void CcStartStopVehicle(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	const Vehicle *v = Vehicle::GetIfValid(p1);
	if (v == nullptr || !v->IsPrimaryVehicle()) return;

	StringID msg = (v->vehstatus & VS_STOPPED) ? STR_VEHICLE_COMMAND_STOPPED : STR_VEHICLE_COMMAND_STARTED;
	Point pt = RemapCoords(v->x_pos, v->y_pos, v->z_pos);
	AddTextEffect(msg, pt.x, pt.y, DAY_TICKS, TE_RISING);
}

/**
 * Executes #CMD_START_STOP_VEHICLE for given vehicle.
 * @param v Vehicle to start/stop
 * @param texteffect Should a texteffect be shown?
 */
void StartStopVehicle(const Vehicle *v, bool texteffect)
{
	assert(v->IsPrimaryVehicle());
	DoCommandP(v->tile, v->index, 0, _vehicle_command_translation_table[VCT_CMD_START_STOP][v->type], texteffect ? CcStartStopVehicle : nullptr);
}

/** Strings for aircraft breakdown types */
static const StringID _aircraft_breakdown_strings[] = {
	STR_BREAKDOWN_TYPE_LOW_SPEED,
	STR_BREAKDOWN_TYPE_DEPOT,
	STR_BREAKDOWN_TYPE_LANDING,
};

/** Checks whether the vehicle may be refitted at the moment.*/
static bool IsVehicleRefitable(const Vehicle *v)
{
	if (!v->IsStoppedInDepot()) return false;

	do {
		if (IsEngineRefittable(v->engine_type)) return true;
	} while (v->IsArticulatedCallbackVehicleType() && (v = v->Next()) != nullptr);

	return false;
}

/** Window manager class for viewing a vehicle. */
struct VehicleViewWindow : Window {
private:
	bool depot_select_active = false;
	bool depot_select_ctrl_pressed = false;
	bool fixed_route_overlay_active = false;

	/** Display planes available in the vehicle view window. */
	enum PlaneSelections {
		SEL_DC_GOTO_DEPOT,  ///< Display 'goto depot' button in #WID_VV_SELECT_DEPOT_CLONE stacked widget.
		SEL_DC_CLONE,       ///< Display 'clone vehicle' button in #WID_VV_SELECT_DEPOT_CLONE stacked widget.

		SEL_RT_REFIT,       ///< Display 'refit' button in #WID_VV_SELECT_REFIT_TURN stacked widget.
		SEL_RT_TURN_AROUND, ///< Display 'turn around' button in #WID_VV_SELECT_REFIT_TURN stacked widget.

		SEL_DC_BASEPLANE = SEL_DC_GOTO_DEPOT, ///< First plane of the #WID_VV_SELECT_DEPOT_CLONE stacked widget.
		SEL_RT_BASEPLANE = SEL_RT_REFIT,      ///< First plane of the #WID_VV_SELECT_REFIT_TURN stacked widget.
	};
	bool mouse_over_start_stop = false;

	/**
	 * Display a plane in the window.
	 * @param plane Plane to show.
	 */
	void SelectPlane(PlaneSelections plane)
	{
		switch (plane) {
			case SEL_DC_GOTO_DEPOT:
			case SEL_DC_CLONE:
				this->GetWidget<NWidgetStacked>(WID_VV_SELECT_DEPOT_CLONE)->SetDisplayedPlane(plane - SEL_DC_BASEPLANE);
				break;

			case SEL_RT_REFIT:
			case SEL_RT_TURN_AROUND:
				this->GetWidget<NWidgetStacked>(WID_VV_SELECT_REFIT_TURN)->SetDisplayedPlane(plane - SEL_RT_BASEPLANE);
				break;

			default:
				NOT_REACHED();
		}
	}

public:
	VehicleViewWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->flags |= WF_DISABLE_VP_SCROLL;
		this->CreateNestedTree();

		/* Sprites for the 'send to depot' button indexed by vehicle type. */
		static const SpriteID vehicle_view_goto_depot_sprites[] = {
			SPR_SEND_TRAIN_TODEPOT,
			SPR_SEND_ROADVEH_TODEPOT,
			SPR_SEND_SHIP_TODEPOT,
			SPR_SEND_AIRCRAFT_TODEPOT,
		};
		const Vehicle *v = Vehicle::Get(window_number);
		this->GetWidget<NWidgetCore>(WID_VV_GOTO_DEPOT)->widget_data = vehicle_view_goto_depot_sprites[v->type];

		/* Sprites for the 'clone vehicle' button indexed by vehicle type. */
		static const SpriteID vehicle_view_clone_sprites[] = {
			SPR_CLONE_TRAIN,
			SPR_CLONE_ROADVEH,
			SPR_CLONE_SHIP,
			SPR_CLONE_AIRCRAFT,
		};
		this->GetWidget<NWidgetCore>(WID_VV_CLONE)->widget_data = vehicle_view_clone_sprites[v->type];

		switch (v->type) {
			case VEH_TRAIN:
				this->GetWidget<NWidgetCore>(WID_VV_TURN_AROUND)->tool_tip = STR_VEHICLE_VIEW_TRAIN_REVERSE_TOOLTIP;
				break;

			case VEH_ROAD:
				break;

			case VEH_SHIP:
			case VEH_AIRCRAFT:
				this->SelectPlane(SEL_RT_REFIT);
				break;

			default: NOT_REACHED();
		}
		this->FinishInitNested(window_number);
		this->owner = v->owner;
		this->GetWidget<NWidgetViewport>(WID_VV_VIEWPORT)->InitializeViewport(this, this->window_number | (1 << 31), ScaleZoomGUI(_vehicle_view_zoom_levels[v->type]));

		this->GetWidget<NWidgetCore>(WID_VV_START_STOP)->tool_tip       = STR_VEHICLE_VIEW_TRAIN_STATUS_START_STOP_TOOLTIP + v->type;
		this->GetWidget<NWidgetCore>(WID_VV_RENAME)->tool_tip           = STR_VEHICLE_DETAILS_TRAIN_RENAME + v->type;
		this->GetWidget<NWidgetCore>(WID_VV_REFIT)->tool_tip            = STR_VEHICLE_VIEW_TRAIN_REFIT_TOOLTIP + v->type;
		this->GetWidget<NWidgetCore>(WID_VV_SHOW_ORDERS)->tool_tip      = STR_VEHICLE_VIEW_TRAIN_ORDERS_TOOLTIP + v->type;
		this->GetWidget<NWidgetCore>(WID_VV_SHOW_DETAILS)->tool_tip     = STR_VEHICLE_VIEW_TRAIN_SHOW_DETAILS_TOOLTIP + v->type;
		this->GetWidget<NWidgetCore>(WID_VV_CLONE)->tool_tip            = STR_VEHICLE_VIEW_CLONE_TRAIN_INFO + v->type;

		this->UpdateButtonStatus();
	}

	void Close([[maybe_unused]] int data = 0) override
	{
		if (this->window_number != INVALID_VEHICLE) {
			const Vehicle *v = Vehicle::Get(this->window_number);
			MarkDirtyFocusedRoutePaths(v);
		}
		CloseWindowById(WC_VEHICLE_ORDERS, this->window_number, false);
		CloseWindowById(WC_VEHICLE_REFIT, this->window_number, false);
		CloseWindowById(WC_VEHICLE_DETAILS, this->window_number, false);
		CloseWindowById(WC_VEHICLE_TIMETABLE, this->window_number, false);

		if (this->fixed_route_overlay_active) {
			RemoveFixedViewportRoutePath(this->window_number);
		}

		this->Window::Close();
	}

	virtual void OnFocus(Window *previously_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, previously_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}

	virtual void OnFocusLost(bool closing, Window *newly_focused_window) override
	{
		if (HasFocusedVehicleChanged(this->window_number, newly_focused_window)) {
			if (this->window_number != INVALID_VEHICLE) {
				const Vehicle *v = Vehicle::Get(this->window_number);
				MarkDirtyFocusedRoutePaths(v);
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);
		switch (widget) {
			case WID_VV_START_STOP:
				size->height = std::max<uint>({size->height, (uint)FONT_HEIGHT_NORMAL, GetScaledSpriteSize(SPR_WARNING_SIGN).height, GetScaledSpriteSize(SPR_FLAG_VEH_STOPPED).height, GetScaledSpriteSize(SPR_FLAG_VEH_RUNNING).height}) + padding.height;
				break;

			case WID_VV_FORCE_PROCEED:
				if (v->type != VEH_TRAIN) {
					size->height = 0;
					size->width = 0;
				}
				break;

			case WID_VV_VIEWPORT:
				size->width = VV_INITIAL_VIEWPORT_WIDTH;
				size->height = (v->type == VEH_TRAIN) ? VV_INITIAL_VIEWPORT_HEIGHT_TRAIN : VV_INITIAL_VIEWPORT_HEIGHT;
				break;
		}
	}

	void OnPaint() override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);
		bool is_localcompany = v->owner == _local_company;
		bool can_control = IsVehicleControlAllowed(v, _local_company);
		bool refitable_and_stopped_in_depot = IsVehicleRefitable(v);

		this->SetWidgetDisabledState(WID_VV_RENAME, !is_localcompany);
		this->SetWidgetDisabledState(WID_VV_GOTO_DEPOT, !is_localcompany);
		this->SetWidgetDisabledState(WID_VV_REFIT, !refitable_and_stopped_in_depot || !is_localcompany);
		this->SetWidgetDisabledState(WID_VV_CLONE, !is_localcompany);

		if (v->type == VEH_TRAIN) {
			this->SetWidgetLoweredState(WID_VV_FORCE_PROCEED, Train::From(v)->force_proceed == TFP_SIGNAL);
			this->SetWidgetDisabledState(WID_VV_FORCE_PROCEED, !can_control);
		}

		if (v->type == VEH_TRAIN || v->type == VEH_ROAD) {
			this->SetWidgetDisabledState(WID_VV_TURN_AROUND, !can_control);
		}

		this->SetWidgetDisabledState(WID_VV_ORDER_LOCATION, v->current_order.GetLocation(v) == INVALID_TILE);

		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		if (widget != WID_VV_CAPTION) return;

		const Vehicle *v = Vehicle::Get(this->window_number);
		SetDParam(0, v->index);
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_VV_START_STOP) return;

		const Vehicle *v = Vehicle::Get(this->window_number);
		bool show_order_number = false;
		StringID str;
		TextColour text_colour = TC_FROMSTRING;
		if (v->vehstatus & VS_CRASHED) {
			str = STR_VEHICLE_STATUS_CRASHED;
		} else if ((v->breakdown_ctr == 1 || (v->type == VEH_TRAIN && Train::From(v)->flags & VRF_IS_BROKEN)) && !mouse_over_start_stop) {
			const Vehicle *w = (v->type == VEH_TRAIN) ? GetMostSeverelyBrokenEngine(Train::From(v)) : v;
			if (_settings_game.vehicle.improved_breakdowns || w->breakdown_type == BREAKDOWN_RV_CRASH || w->breakdown_type == BREAKDOWN_BRAKE_OVERHEAT) {
				str = STR_VEHICLE_STATUS_BROKEN_DOWN_VEL;
				SetDParam(3, v->GetDisplaySpeed());
			} else {
				str = STR_VEHICLE_STATUS_BROKEN_DOWN;
			}

			if (v->type == VEH_AIRCRAFT) {
				SetDParam(0, _aircraft_breakdown_strings[v->breakdown_type]);
				if (v->breakdown_type == BREAKDOWN_AIRCRAFT_SPEED) {
					SetDParam(1, v->breakdown_severity << 3);
				} else {
					SetDParam(1, v->current_order.GetDestination());
				}
			} else {
				SetDParam(0, STR_BREAKDOWN_TYPE_CRITICAL + w->breakdown_type);

				if (w->breakdown_type == BREAKDOWN_LOW_SPEED) {
					SetDParam(1, std::min(w->First()->GetDisplayMaxSpeed(), w->breakdown_severity >> ((v->type == VEH_TRAIN) ? 0 : 1)));
				} else if (w->breakdown_type == BREAKDOWN_LOW_POWER) {
					int percent;
					if (v->type == VEH_TRAIN) {
						uint32 power, te;
						Train::From(v)->CalculatePower(power, te, true);
						percent = (100 * power) / Train::From(v)->gcache.cached_power;
					} else {
						percent = w->breakdown_severity * 100 / 256;
					}
					SetDParam(1, percent);
				}
			}
		} else if (v->vehstatus & VS_STOPPED && (!mouse_over_start_stop || v->IsStoppedInDepot())) {
			if (v->type == VEH_TRAIN) {
				if (v->cur_speed == 0) {
					if (Train::From(v)->gcache.cached_power == 0) {
						str = STR_VEHICLE_STATUS_TRAIN_NO_POWER;
					} else {
						str = STR_VEHICLE_STATUS_STOPPED;
					}
				} else {
					SetDParam(0, PackVelocity(v->GetDisplaySpeed(), v->type));
					str = STR_VEHICLE_STATUS_TRAIN_STOPPING_VEL;
				}
			} else if (v->type == VEH_ROAD) {
				if (RoadVehicle::From(v)->IsRoadVehicleStopped()) {
					str = STR_VEHICLE_STATUS_STOPPED;
				} else {
					SetDParam(0, v->GetDisplaySpeed());
					str = STR_VEHICLE_STATUS_TRAIN_STOPPING_VEL;
				}
			} else { // no train/RV
				str = STR_VEHICLE_STATUS_STOPPED;
			}
		} else if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_TRAIN_STUCK) && !v->current_order.IsType(OT_LOADING) && !mouse_over_start_stop) {
			str = HasBit(Train::From(v)->flags, VRF_WAITING_RESTRICTION) ? STR_VEHICLE_STATUS_TRAIN_STUCK_WAIT_RESTRICTION : STR_VEHICLE_STATUS_TRAIN_STUCK;
		} else if (v->type == VEH_TRAIN && Train::From(v)->reverse_distance > 1) {
			if (Train::From(v)->track == TRACK_BIT_DEPOT) {
				str = STR_VEHICLE_STATUS_TRAIN_MOVING_DEPOT;
			} else {
				str = STR_VEHICLE_STATUS_TRAIN_REVERSING;
				SetDParam(0, v->GetDisplaySpeed());
			}
		} else if (v->type == VEH_AIRCRAFT && HasBit(Aircraft::From(v)->flags, VAF_DEST_TOO_FAR) && !v->current_order.IsType(OT_LOADING)) {
			str = STR_VEHICLE_STATUS_AIRCRAFT_TOO_FAR;
		} else { // vehicle is in a "normal" state, show current order
			switch (v->current_order.GetType()) {
				case OT_GOTO_STATION: {
					show_order_number = true;
					text_colour = TC_LIGHT_BLUE;
					SetDParam(0, v->current_order.GetDestination());
					SetDParam(1, PackVelocity(v->GetDisplaySpeed(), v->type));
					str = HasBit(v->vehicle_flags, VF_PATHFINDER_LOST) ? STR_VEHICLE_STATUS_CANNOT_REACH_STATION_VEL : STR_VEHICLE_STATUS_HEADING_FOR_STATION_VEL;
					break;
				}

				case OT_GOTO_DEPOT: {
					show_order_number = true;
					text_colour = TC_ORANGE;
					SetDParam(0, v->type);
					SetDParam(1, v->current_order.GetDestination());
					SetDParam(2, PackVelocity(v->GetDisplaySpeed(), v->type));
					if (v->current_order.GetDestination() == INVALID_DEPOT) {
						/* This case *only* happens when multiple nearest depot orders
						 * follow each other (including an order list only one order: a
						 * nearest depot order) and there are no reachable depots.
						 * It is primarily to guard for the case that there is no
						 * depot with index 0, which would be used as fallback for
						 * evaluating the string in the status bar. */
						str = STR_EMPTY;
					} else if (v->current_order.GetDepotActionType() & ODATFB_SELL) {
						str = STR_VEHICLE_STATUS_HEADING_FOR_DEPOT_SELL_VEL;
					} else if (v->current_order.GetDepotActionType() & ODATFB_HALT) {
						str = HasBit(v->vehicle_flags, VF_PATHFINDER_LOST) ? STR_VEHICLE_STATUS_CANNOT_REACH_DEPOT_VEL : STR_VEHICLE_STATUS_HEADING_FOR_DEPOT_VEL;
					} else {
						str = HasBit(v->vehicle_flags, VF_PATHFINDER_LOST) ? STR_VEHICLE_STATUS_CANNOT_REACH_DEPOT_SERVICE_VEL : STR_VEHICLE_STATUS_HEADING_FOR_DEPOT_SERVICE_VEL;
					}
					break;
				}

				case OT_LOADING:
					str = STR_VEHICLE_STATUS_LOADING_UNLOADING;
					break;

				case OT_LOADING_ADVANCE:
					str = STR_VEHICLE_STATUS_LOADING_UNLOADING_ADVANCE;
					SetDParam(0, STR_VEHICLE_STATUS_LOADING_UNLOADING);
					SetDParam(1, v->GetDisplaySpeed());
					break;

				case OT_GOTO_WAYPOINT: {
					show_order_number = true;
					text_colour = TC_LIGHT_BLUE;
					assert(v->type == VEH_TRAIN || v->type == VEH_ROAD || v->type == VEH_SHIP);
					SetDParam(0, v->current_order.GetDestination());
					str = HasBit(v->vehicle_flags, VF_PATHFINDER_LOST) ? STR_VEHICLE_STATUS_CANNOT_REACH_WAYPOINT_VEL : STR_VEHICLE_STATUS_HEADING_FOR_WAYPOINT_VEL;
					SetDParam(1, PackVelocity(v->GetDisplaySpeed(), v->type));
					break;
				}

				case OT_WAITING: {
					str = STR_VEHICLE_STATUS_TRAIN_WAITING_TIMETABLE;
					break;
				}

				case OT_LEAVESTATION:
					if (v->type != VEH_AIRCRAFT) {
						str = STR_VEHICLE_STATUS_LEAVING;
						break;
					}
					FALLTHROUGH;
				default:
					if (v->GetNumManualOrders() == 0) {
						str = STR_VEHICLE_STATUS_NO_ORDERS_VEL;
						SetDParam(0, PackVelocity(v->GetDisplaySpeed(), v->type));
					} else {
						str = STR_EMPTY;
					}
					break;
			}

			if (mouse_over_start_stop) {
				if (v->vehstatus & VS_STOPPED || (v->breakdown_ctr == 1 || (v->type == VEH_TRAIN && Train::From(v)->flags & VRF_IS_BROKEN))) {
					text_colour = TC_RED | TC_FORCED;
				} else if (v->type == VEH_TRAIN && HasBit(Train::From(v)->flags, VRF_TRAIN_STUCK) && !v->current_order.IsType(OT_LOADING)) {
					text_colour = TC_ORANGE | TC_FORCED;
				}
			}
		}

		if (_settings_client.gui.show_order_number_vehicle_view && show_order_number && v->cur_implicit_order_index < v->GetNumOrders()) {
			_temp_special_strings[0] = GetString(str);
			SetDParam(0, v->cur_implicit_order_index + 1);
			SetDParam(1, SPECSTR_TEMP_START);
			str = STR_VEHICLE_VIEW_ORDER_NUMBER;
		}

		/* Draw the flag plus orders. */
		bool rtl = (_current_text_dir == TD_RTL);
		uint icon_width = std::max({GetScaledSpriteSize(SPR_WARNING_SIGN).width, GetScaledSpriteSize(SPR_FLAG_VEH_STOPPED).width, GetScaledSpriteSize(SPR_FLAG_VEH_RUNNING).width});
		int lowered = this->IsWidgetLowered(widget) ? WidgetDimensions::scaled.pressed : 0;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect).Translate(lowered, lowered);
		SpriteID image = ((v->vehstatus & VS_STOPPED) != 0) ? SPR_FLAG_VEH_STOPPED : (HasBit(v->vehicle_flags, VF_PATHFINDER_LOST)) ? SPR_WARNING_SIGN : SPR_FLAG_VEH_RUNNING;
		DrawSpriteIgnorePadding(image, PAL_NONE, tr.WithWidth(icon_width, rtl), false, SA_CENTER);
		tr = tr.Indent(icon_width + WidgetDimensions::scaled.imgbtn.Horizontal(), rtl);
		DrawString(tr.left, tr.right, CenterBounds(tr.top, tr.bottom, FONT_HEIGHT_NORMAL), str, text_colour, SA_HOR_CENTER);
	}

	void OnClick([[maybe_unused]] Point pt, int widget, [[maybe_unused]] int click_count) override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);

		switch (widget) {
			case WID_VV_RENAME: { // rename
				SetDParam(0, v->index);
				ShowQueryString(STR_VEHICLE_NAME, STR_QUERY_RENAME_TRAIN_CAPTION + v->type,
						MAX_LENGTH_VEHICLE_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;
			}

			case WID_VV_START_STOP: // start stop
				StartStopVehicle(v, false);
				break;

			case WID_VV_ORDER_LOCATION: {
				/* Scroll to current order destination */
				TileIndex tile = v->current_order.GetLocation(v);
				if (tile == INVALID_TILE) break;

				if (_ctrl_pressed) {
					ShowExtraViewportWindow(tile);
				} else {
					ScrollMainWindowToTile(tile);
				}
				break;
			}

			case WID_VV_LOCATION: // center main view
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(TileVirtXY(v->x_pos, v->y_pos));
					this->HandleButtonClick(widget);
				} else if (_shift_pressed) {
					this->fixed_route_overlay_active = !this->fixed_route_overlay_active;
					this->SetWidgetLoweredState(widget, this->fixed_route_overlay_active);
					this->SetWidgetDirty(widget);
					if (this->fixed_route_overlay_active) {
						AddFixedViewportRoutePath(this->window_number);
					} else {
						RemoveFixedViewportRoutePath(this->window_number);
					}
				} else {
					const Window *mainwindow = GetMainWindow();
					if (click_count > 1 && mainwindow->viewport->zoom < ZOOM_LVL_DRAW_MAP) {
						/* main window 'follows' vehicle */
						mainwindow->viewport->follow_vehicle = v->index;
					} else {
						ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos);
					}
					this->HandleButtonClick(widget);
				}
				break;

			case WID_VV_GOTO_DEPOT: // goto hangar
				if (_shift_pressed) {
					if (HandlePlacePushButton(this, WID_VV_GOTO_DEPOT, ANIMCURSOR_PICKSTATION, HT_RECT)) {
						this->depot_select_ctrl_pressed = _ctrl_pressed;
						this->depot_select_active = true;
					}
				} else if (_ctrl_pressed && _settings_client.gui.show_depot_sell_gui && v->current_order.IsType(OT_GOTO_DEPOT)) {
					OrderDepotActionFlags flags = v->current_order.GetDepotActionType() & (ODATFB_HALT | ODATFB_SELL);
					DropDownList list;
					list.emplace_back(new DropDownListStringItem(STR_VEHICLE_LIST_SEND_FOR_SERVICING, DEPOT_SERVICE | DEPOT_DONT_CANCEL, !flags));
					list.emplace_back(new DropDownListStringItem(BaseVehicleListWindow::vehicle_depot_name[v->type], DEPOT_DONT_CANCEL, flags == ODATFB_HALT));
					list.emplace_back(new DropDownListStringItem(BaseVehicleListWindow::vehicle_depot_sell_name[v->type], DEPOT_SELL | DEPOT_DONT_CANCEL, flags == (ODATFB_HALT | ODATFB_SELL)));
					list.emplace_back(new DropDownListStringItem(STR_VEHICLE_LIST_CANCEL_DEPOT_SERVICE, DEPOT_CANCEL, false));
					ShowDropDownList(this, std::move(list), -1, widget);
				} else {
					this->HandleButtonClick(WID_VV_GOTO_DEPOT);
					DoCommandP(v->tile, v->index | (_ctrl_pressed ? DEPOT_SERVICE : 0U), 0, GetCmdSendToDepot(v));
				}
				break;
			case WID_VV_REFIT: // refit
				ShowVehicleRefitWindow(v, INVALID_VEH_ORDER_ID, this);
				break;
			case WID_VV_SHOW_ORDERS: // show orders
				if (_ctrl_pressed) {
					ShowTimetableWindow(v);
				} else {
					ShowOrdersWindow(v);
				}
				break;
			case WID_VV_SHOW_DETAILS: // show details
				if (_ctrl_pressed) {
					ShowCompanyGroupForVehicle(v);
				} else {
					ShowVehicleDetailsWindow(v);
				}
				break;
			case WID_VV_CLONE: // clone vehicle
				/* Suppress the vehicle GUI when share-cloning.
				 * There is no point to it except for starting the vehicle.
				 * For starting the vehicle the player has to open the depot GUI, which is
				 * most likely already open, but is also visible in the vehicle viewport. */
				DoCommandP(v->tile, v->index, _ctrl_pressed ? 1 : 0,
										_vehicle_command_translation_table[VCT_CMD_CLONE_VEH][v->type],
										_ctrl_pressed ? nullptr : CcCloneVehicle);
				break;
			case WID_VV_TURN_AROUND: // turn around
				assert(v->IsGroundVehicle());
				DoCommandP(v->tile, v->index, 0,
										_vehicle_command_translation_table[VCT_CMD_TURN_AROUND][v->type]);
				break;
			case WID_VV_FORCE_PROCEED: // force proceed
				assert(v->type == VEH_TRAIN);
				DoCommandP(v->tile, v->index, 0, CMD_FORCE_TRAIN_PROCEED | CMD_MSG(STR_ERROR_CAN_T_MAKE_TRAIN_PASS_SIGNAL));
				break;
		}
	}

	EventState OnHotkey(int hotkey) override
	{
		/* If the hotkey is not for any widget in the UI (i.e. for honking) */
		if (hotkey == WID_VV_HONK_HORN) {
			const Window *mainwindow = GetMainWindow();
			const Vehicle *v = Vehicle::Get(window_number);
			/* Only play the sound if we're following this vehicle */
			if (mainwindow->viewport->follow_vehicle == v->index) {
				v->PlayLeaveStationSound(true);
			}
		}
		return Window::OnHotkey(hotkey);
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_VEHICLE | CMD_MSG(STR_ERROR_CAN_T_RENAME_TRAIN + Vehicle::Get(this->window_number)->type), nullptr, str);
	}

	virtual void OnDropdownSelect(int widget, int index) override
	{
		switch (widget) {
			case WID_VV_GOTO_DEPOT: {
				const Vehicle *v = Vehicle::Get(this->window_number);
				DoCommandP(v->tile, v->index | index, 0, GetCmdSendToDepot(v));
				break;
			}
		}
	}

	virtual void OnTimeout() override
	{
		if (!this->depot_select_active) {
			this->RaiseWidget(WID_VV_GOTO_DEPOT);
			this->SetWidgetDirty(WID_VV_GOTO_DEPOT);
		}
		if (!this->fixed_route_overlay_active) {
			this->RaiseWidget(WID_VV_LOCATION);
			this->SetWidgetDirty(WID_VV_LOCATION);
		}
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile) override
	{
		const Vehicle *v = Vehicle::Get(this->window_number);
		if (IsDepotTile(tile) && GetDepotVehicleType(tile) == v->type && IsInfraTileUsageAllowed(v->type, v->owner, tile)) {
			if (v->type == VEH_ROAD && (GetPresentRoadTypes(tile) & RoadVehicle::From(v)->compatible_roadtypes) == 0) return;
			if (v->type == VEH_TRAIN && !HasBit(Train::From(v)->compatible_railtypes, GetRailType(tile))) return;
			DoCommandP(v->tile, v->index | (this->depot_select_ctrl_pressed ? DEPOT_SERVICE : 0U) | DEPOT_SPECIFIC, tile, GetCmdSendToDepot(v));
			ResetObjectToPlace();
			this->RaiseButtons();
		}
	}

	virtual void OnPlaceObjectAbort() override
	{
		this->depot_select_active = false;
		this->RaiseWidget(WID_VV_GOTO_DEPOT);
		this->SetWidgetDirty(WID_VV_GOTO_DEPOT);
	}

	virtual bool OnRightClick(Point pt, int widget) override
	{
		if (widget == WID_VV_GOTO_DEPOT && _settings_client.gui.hover_delay_ms == 0) {
			const Vehicle *v = Vehicle::Get(this->window_number);
			if (_settings_client.gui.show_depot_sell_gui && v->current_order.IsType(OT_GOTO_DEPOT)) {
				GuiShowTooltips(this, STR_VEHICLE_VIEW_SEND_TO_DEPOT_MENU, TCC_RIGHT_CLICK);
			} else {
				SetDParam(0, STR_VEHICLE_VIEW_TRAIN_SEND_TO_DEPOT_TOOLTIP + v->type);
				GuiShowTooltips(this, STR_VEHICLE_VIEW_SEND_TO_DEPOT_TOOLTIP_SHIFT, TCC_RIGHT_CLICK, 1);
			}
		}
		return false;
	}

	virtual bool OnTooltip(Point pt, int widget, TooltipCloseCondition close_cond) override
	{
		if (widget == WID_VV_GOTO_DEPOT) {
			const Vehicle *v = Vehicle::Get(this->window_number);
			if (_settings_client.gui.show_depot_sell_gui && v->current_order.IsType(OT_GOTO_DEPOT)) {
				GuiShowTooltips(this, STR_VEHICLE_VIEW_SEND_TO_DEPOT_MENU, close_cond);
			} else {
				SetDParam(0, STR_VEHICLE_VIEW_TRAIN_SEND_TO_DEPOT_TOOLTIP + v->type);
				GuiShowTooltips(this, STR_VEHICLE_VIEW_SEND_TO_DEPOT_TOOLTIP_SHIFT, close_cond, 1);
			}
			return true;
		}
		if (widget == WID_VV_LOCATION) {
			const Vehicle *v = Vehicle::Get(this->window_number);
			SetDParam(0, STR_VEHICLE_VIEW_TRAIN_CENTER_TOOLTIP + v->type);
			GuiShowTooltips(this, STR_VEHICLE_VIEW_TRAIN_CENTER_TOOLTIP_EXTRA, close_cond, 1);
			return true;
		}
		return false;
	}

	void OnMouseOver([[maybe_unused]] Point pt, int widget) override
	{
		bool start_stop = widget == WID_VV_START_STOP;
		if (start_stop != mouse_over_start_stop) {
			mouse_over_start_stop = start_stop;
			this->SetWidgetDirty(WID_VV_START_STOP);
		}
	}

	void OnResize() override
	{
		if (this->viewport != nullptr) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_VV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);
		}
	}

	void UpdateButtonStatus()
	{
		const Vehicle *v = Vehicle::Get(this->window_number);
		bool veh_stopped = v->IsStoppedInDepot();

		/* Widget WID_VV_GOTO_DEPOT must be hidden if the vehicle is already stopped in depot.
		 * Widget WID_VV_CLONE_VEH should then be shown, since cloning is allowed only while in depot and stopped.
		 */
		PlaneSelections plane = veh_stopped ? SEL_DC_CLONE : SEL_DC_GOTO_DEPOT;
		NWidgetStacked *nwi = this->GetWidget<NWidgetStacked>(WID_VV_SELECT_DEPOT_CLONE); // Selection widget 'send to depot' / 'clone'.
		if (nwi->shown_plane + SEL_DC_BASEPLANE != plane) {
			this->SelectPlane(plane);
			this->SetWidgetDirty(WID_VV_SELECT_DEPOT_CLONE);
		}
		/* The same system applies to widget WID_VV_REFIT_VEH and VVW_WIDGET_TURN_AROUND.*/
		if (v->IsGroundVehicle()) {
			plane = veh_stopped ? SEL_RT_REFIT : SEL_RT_TURN_AROUND;
			nwi = this->GetWidget<NWidgetStacked>(WID_VV_SELECT_REFIT_TURN);
			if (nwi->shown_plane + SEL_RT_BASEPLANE != plane) {
				this->SelectPlane(plane);
				this->SetWidgetDirty(WID_VV_SELECT_REFIT_TURN);
			}
		}
	}

	virtual void OnRealtimeTick(uint delta_ms) override
	{
		if (_pause_mode != PM_UNPAUSED) this->OnGameTick();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData([[maybe_unused]] int data = 0, [[maybe_unused]] bool gui_scope = true) override
	{
		if (data == VIWD_AUTOREPLACE) {
			/* Autoreplace replaced the vehicle.
			 * Nothing to do for this window. */
			return;
		}

		this->UpdateButtonStatus();
	}

	bool IsNewGRFInspectable() const override
	{
		return ::IsNewGRFInspectable(GetGrfSpecFeature(Vehicle::Get(this->window_number)->type), this->window_number);
	}

	void ShowNewGRFInspectWindow() const override
	{
		::ShowNewGRFInspectWindow(GetGrfSpecFeature(Vehicle::Get(this->window_number)->type), this->window_number);
	}

	static HotkeyList hotkeys;
};

static Hotkey vehicleview_hotkeys[] = {
	Hotkey('H', "honk", WID_VV_HONK_HORN),
	HOTKEY_LIST_END
};
HotkeyList VehicleViewWindow::hotkeys("vehicleview", vehicleview_hotkeys);

/** Vehicle view window descriptor for all vehicles but trains. */
static WindowDesc _vehicle_view_desc(
	WDP_AUTO, "view_vehicle", 250, 116,
	WC_VEHICLE_VIEW, WC_NONE,
	0,
	std::begin(_nested_vehicle_view_widgets), std::end(_nested_vehicle_view_widgets),
	&VehicleViewWindow::hotkeys
);

/**
 * Vehicle view window descriptor for trains. Only minimum_height and
 *  default_height are different for train view.
 */
static WindowDesc _train_view_desc(
	WDP_AUTO, "view_vehicle_train", 250, 134,
	WC_VEHICLE_VIEW, WC_NONE,
	0,
	std::begin(_nested_vehicle_view_widgets), std::end(_nested_vehicle_view_widgets),
	&VehicleViewWindow::hotkeys
);

/** Shows the vehicle view window of the given vehicle. */
void ShowVehicleViewWindow(const Vehicle *v)
{
	AllocateWindowDescFront<VehicleViewWindow>((v->type == VEH_TRAIN) ? &_train_view_desc : &_vehicle_view_desc, v->index);
}

/**
 * Dispatch a "vehicle selected" event if any window waits for it.
 * @param v selected vehicle;
 * @return did any window accept vehicle selection?
 */
bool VehicleClicked(const Vehicle *v)
{
	assert(v != nullptr);
	if (!(_thd.place_mode & HT_VEHICLE)) return false;

	v = v->First();
	if (!v->IsPrimaryVehicle()) return false;

	return _thd.GetCallbackWnd()->OnVehicleSelect(v);
}

/**
 * Dispatch a "vehicle group selected" event if any window waits for it.
 * @param begin iterator to the start of the range of vehicles
 * @param end iterator to the end of the range of vehicles
 * @return did any window accept vehicle group selection?
 */
bool VehicleClicked(VehicleList::const_iterator begin, VehicleList::const_iterator end)
{
	assert(begin != end);
	if (!(_thd.place_mode & HT_VEHICLE)) return false;

	/* If there is only one vehicle in the group, act as if we clicked a single vehicle */
	if (begin + 1 == end) return _thd.GetCallbackWnd()->OnVehicleSelect(*begin);

	return _thd.GetCallbackWnd()->OnVehicleSelect(begin, end);
}

/**
 * Dispatch a "vehicle group selected" event if any window waits for it.
 * @param vehgroup the GUIVehicleGroup representing the vehicle group
 * @return did any window accept vehicle group selection?
 */
bool VehicleClicked(const GUIVehicleGroup &vehgroup)
{
	return VehicleClicked(vehgroup.vehicles_begin, vehgroup.vehicles_end);
}

void StopGlobalFollowVehicle(const Vehicle *v)
{
	Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
	if (w != nullptr && w->viewport->follow_vehicle == v->index) {
		ScrollMainWindowTo(v->x_pos, v->y_pos, v->z_pos, true); // lock the main view on the vehicle's last position
		w->viewport->follow_vehicle = INVALID_VEHICLE;
	}
}


/**
 * This is the Callback method after the construction attempt of a primary vehicle
 * @param result indicates completion (or not) of the operation
 * @param tile unused
 * @param p1 unused
 * @param p2 unused
 * @param cmd unused
 */
void CcBuildPrimaryVehicle(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2, uint64 p3, uint32 cmd)
{
	if (result.Failed()) return;

	const Vehicle *v = Vehicle::Get(_new_vehicle_id);
	ShowVehicleViewWindow(v);
}

/**
 * Get the width of a vehicle (part) in pixels.
 * @param v Vehicle to get the width for.
 * @return Width of the vehicle.
 */
int GetSingleVehicleWidth(const Vehicle *v, EngineImageType image_type)
{
	switch (v->type) {
		case VEH_TRAIN:
			return Train::From(v)->GetDisplayImageWidth();

		case VEH_ROAD:
			return RoadVehicle::From(v)->GetDisplayImageWidth();

		default:
			bool rtl = _current_text_dir == TD_RTL;
			VehicleSpriteSeq seq;
			v->GetImage(rtl ? DIR_E : DIR_W, image_type, &seq);
			Rect rec = ConvertRect<Rect16, Rect>(seq.GetBounds());
			return UnScaleGUI(rec.Width());
	}
}

/**
 * Get the width of a vehicle (including all parts of the consist) in pixels.
 * @param v Vehicle to get the width for.
 * @return Width of the vehicle.
 */
int GetVehicleWidth(const Vehicle *v, EngineImageType image_type)
{
	if (v->type == VEH_TRAIN || v->type == VEH_ROAD) {
		int vehicle_width = 0;
		for (const Vehicle *u = v; u != nullptr; u = u->Next()) {
			vehicle_width += GetSingleVehicleWidth(u, image_type);
		}
		return vehicle_width;
	} else {
		return GetSingleVehicleWidth(v, image_type);
	}
}

/**
 * Set the mouse cursor to look like a vehicle.
 * @param v Vehicle
 * @param image_type Type of vehicle image to use.
 */
void SetMouseCursorVehicle(const Vehicle *v, EngineImageType image_type)
{
	bool rtl = _current_text_dir == TD_RTL;

	_cursor.sprite_count = 0;
	int total_width = 0;
	int y_offset = 0;
	bool rotor_seq = false; // Whether to draw the rotor of the vehicle in this step.
	bool is_ground_vehicle = v->IsGroundVehicle();

	while (v != nullptr) {
		if (total_width >= ScaleSpriteTrad(2 * (int)VEHICLEINFO_FULL_VEHICLE_WIDTH)) break;

		PaletteID pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);
		VehicleSpriteSeq seq;

		if (rotor_seq) {
			GetCustomRotorSprite(Aircraft::From(v), image_type, &seq);
			if (!seq.IsValid()) seq.Set(SPR_ROTOR_STOPPED);
			y_offset = -ScaleSpriteTrad(5);
		} else {
			v->GetImage(rtl ? DIR_E : DIR_W, image_type, &seq);
		}

		if (_cursor.sprite_count + seq.count > lengthof(_cursor.sprite_seq)) break;

		int x_offs = 0;
		if (v->type == VEH_TRAIN) x_offs = Train::From(v)->GetCursorImageOffset();

		for (uint i = 0; i < seq.count; ++i) {
			PaletteID pal2 = (v->vehstatus & VS_CRASHED) || !seq.seq[i].pal ? pal : seq.seq[i].pal;
			_cursor.sprite_seq[_cursor.sprite_count].sprite = seq.seq[i].sprite;
			_cursor.sprite_seq[_cursor.sprite_count].pal = pal2;
			_cursor.sprite_pos[_cursor.sprite_count].x = rtl ? (-total_width + x_offs) : (total_width + x_offs);
			_cursor.sprite_pos[_cursor.sprite_count].y = y_offset;
			_cursor.sprite_count++;
		}

		if (v->type == VEH_AIRCRAFT && v->subtype == AIR_HELICOPTER && !rotor_seq) {
			/* Draw rotor part in the next step. */
			rotor_seq = true;
		} else {
			total_width += GetSingleVehicleWidth(v, image_type);
			v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr;
		}
	}

	if (is_ground_vehicle) {
		/* Center trains and road vehicles on the front vehicle */
		int offs = (ScaleSpriteTrad(VEHICLEINFO_FULL_VEHICLE_WIDTH) - total_width) / 2;
		if (rtl) offs = -offs;
		for (uint i = 0; i < _cursor.sprite_count; ++i) {
			_cursor.sprite_pos[i].x += offs;
		}
	}

	UpdateCursorSize();
}
