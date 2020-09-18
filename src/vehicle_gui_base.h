/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_gui_base.h Functions/classes shared between the different vehicle list GUIs. */

#ifndef VEHICLE_GUI_BASE_H
#define VEHICLE_GUI_BASE_H

#include "sortlist_type.h"
#include "vehiclelist.h"
#include "window_gui.h"
#include "widgets/dropdown_type.h"
#include "cargo_type.h"

typedef GUIList<const Vehicle*, CargoID> GUIVehicleList;

struct BaseVehicleListWindow : public Window {
	GUIVehicleList vehicles;  ///< The list of vehicles
	uint own_vehicles = 0;    ///< Count of vehicles of the local company
	CompanyID own_company;    ///< Company ID used for own_vehicles
	Listing *sorting;         ///< Pointer to the vehicle type related sorting.
	byte unitnumber_digits;   ///< The number of digits of the highest unit number
	Scrollbar *vscroll;
	VehicleListIdentifier vli; ///< Identifier of the vehicle list we want to currently show.
	VehicleID vehicle_sel;    ///< Selected vehicle

	/** Special cargo filter criteria */
	enum CargoFilterSpecialType {
		CF_ANY = CT_NO_REFIT,                   ///< Show all vehicles independent of carried cargo (i.e. no filtering)
		CF_NONE = CT_INVALID,                   ///< Show only vehicles which do not carry cargo (e.g. train engines)
		CF_FREIGHT = CT_AUTO_REFIT,             ///< Show only vehicles which carry any freight (non-passenger) cargo
	};

	CargoID cargo_filter[NUM_CARGO + 3];        ///< Available cargo filters; CargoID or CF_ANY or CF_NONE
	StringID cargo_filter_texts[NUM_CARGO + 4]; ///< Texts for filter_cargo, terminated by INVALID_STRING_ID
	byte cargo_filter_criteria;                 ///< Selected cargo filter

	enum ActionDropdownItem {
		ADI_TEMPLATE_REPLACE,
		ADI_REPLACE,
		ADI_SERVICE,
		ADI_DEPOT,
		ADI_DEPOT_SELL,
		ADI_CANCEL_DEPOT,
		ADI_ADD_SHARED,
		ADI_REMOVE_ALL,
		ADI_CHANGE_ORDER,
		ADI_CREATE_GROUP,
		ADI_TRACERESTRICT_SLOT_MGMT,
		ADI_TRACERESTRICT_COUNTER_MGMT,
	};

	static const StringID vehicle_depot_name[];
	static const StringID vehicle_depot_sell_name[];
	static const StringID vehicle_sorter_names[];
	static GUIVehicleList::SortFunction * const vehicle_sorter_funcs[];
	const uint vehicle_sorter_non_ground_veh_disable_mask = (1 << 11); // STR_SORT_BY_LENGTH

	BaseVehicleListWindow(WindowDesc *desc, WindowNumber wno) : Window(desc), vli(VehicleListIdentifier::UnPack(wno))
	{
		this->vehicle_sel = INVALID_VEHICLE;
		this->vehicles.SetSortFuncs(this->vehicle_sorter_funcs);
	}

	void DrawVehicleListItems(VehicleID selected_vehicle, int line_height, const Rect &r) const;
	void SortVehicleList();
	void CountOwnVehicles();
	void BuildVehicleList();
	void SetCargoFilterIndex(int index);
	void SetCargoFilterArray();
	void FilterVehicleList();
	void OnInit() override;
	void CheckCargoFilterEnableState(int plane_widget, bool re_init, bool possible = true);
	Dimension GetActionDropdownSize(bool show_autoreplace, bool show_group, bool show_template_replace, StringID change_order_str = 0);
	DropDownList BuildActionDropdownList(bool show_autoreplace, bool show_group, bool show_template_replace,
			StringID change_order_str = 0, bool show_create_group = false, bool consider_top_level = false);
	bool ShouldShowActionDropdownList() const;
};

uint GetVehicleListHeight(VehicleType type, uint divisor = 1);

struct Sorting {
	Listing aircraft;
	Listing roadveh;
	Listing ship;
	Listing train;
};

extern Sorting _sorting;

#endif /* VEHICLE_GUI_BASE_H */
