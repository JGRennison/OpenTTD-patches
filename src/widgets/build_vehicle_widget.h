/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file build_vehicle_widget.h Types related to the build_vehicle widgets. */

#ifndef WIDGETS_BUILD_VEHICLE_WIDGET_H
#define WIDGETS_BUILD_VEHICLE_WIDGET_H

/** Widgets of the #BuildVehicleWindow class. */
enum BuildVehicleWidgets {
	WID_BV_CAPTION,                   ///< Caption of window.
	WID_BV_SORT_ASCENDING_DESCENDING, ///< Sort direction.
	WID_BV_SORT_DROPDOWN,             ///< Criteria of sorting dropdown.
	WID_BV_CARGO_FILTER_DROPDOWN,     ///< Cargo filter dropdown.
	WID_BV_FILTER,                    ///< Filter by name.
	WID_BV_SHOW_HIDDEN_ENGINES,       ///< Toggle whether to display the hidden vehicles.
	WID_BV_LIST,                      ///< List of vehicles.
	WID_BV_SCROLLBAR,                 ///< Scrollbar of list.
	WID_BV_PANEL,                     ///< Button panel.
	WID_BV_BUILD,                     ///< Build panel.
	WID_BV_SHOW_HIDE,                 ///< Button to hide or show the selected engine.
	WID_BV_BUILD_SEL,                 ///< Build button.
	WID_BV_RENAME,                    ///< Rename button.

	WID_BV_CAPTION_LOCO,                   ///< Caption of locomotive half of the window.
	WID_BV_SORT_ASCENDING_DESCENDING_LOCO, ///< Sort direction.
	WID_BV_SORT_DROPDOWN_LOCO,             ///< Criteria of sorting dropdown.
	WID_BV_CARGO_FILTER_DROPDOWN_LOCO,     ///< Cargo filter dropdown.
	WID_BV_SHOW_HIDDEN_LOCOS,              ///< Toggle whether to display the hidden locomotives.
	WID_BV_LIST_LOCO,                      ///< List of vehicles.
	WID_BV_SCROLLBAR_LOCO,                 ///< Scrollbar of list.
	WID_BV_PANEL_LOCO,                     ///< Button panel.
	WID_BV_SHOW_HIDE_LOCO,                 ///< Button to hide or show the selected locomotives.
	WID_BV_BUILD_LOCO,                     ///< Build panel.
	WID_BV_BUILD_SEL_LOCO,                 ///< Build button.
	WID_BV_RENAME_LOCO,                    ///< Rename button.
	WID_BV_FILTER_LOCO,                    ///< Filter by name.

	WID_BV_CAPTION_WAGON,                   ///< Caption of wagon half of the window.
	WID_BV_SORT_ASCENDING_DESCENDING_WAGON, ///< Sort direction.
	WID_BV_SORT_DROPDOWN_WAGON,             ///< Criteria of sorting dropdown.
	WID_BV_CARGO_FILTER_DROPDOWN_WAGON,     ///< Cargo filter dropdown.
	WID_BV_SHOW_HIDDEN_WAGONS,              ///< Toggle whether to display the hidden wagons.
	WID_BV_LIST_WAGON,                      ///< List of vehicles.
	WID_BV_SCROLLBAR_WAGON,                 ///< Scrollbar of list.
	WID_BV_PANEL_WAGON,                     ///< Button panel.
	WID_BV_SHOW_HIDE_WAGON,                 ///< Button to hide or show the selected wagons.
	WID_BV_BUILD_WAGON,                     ///< Build panel.
	WID_BV_BUILD_SEL_WAGON,                 ///< Build button.
	WID_BV_RENAME_WAGON,                    ///< Rename button.
	WID_BV_FILTER_WAGON,                    ///< Filter by name.

	WID_BV_LOCO_BUTTONS_SEL,                ///< Locomotive buttons selector.
	WID_BV_WAGON_BUTTONS_SEL,               ///< Wagon buttons selector.

	WID_BV_COMB_BUTTONS_SEL,                ///< Combined buttons: section selector.
	WID_BV_COMB_BUILD_SEL,                  ///< Combined buttons: build button selector.
	WID_BV_COMB_BUILD,                      ///< Combined buttons: build button.
	WID_BV_COMB_SHOW_HIDE,                  ///< Combined buttons: show/hide button.
	WID_BV_COMB_RENAME,                     ///< Combined buttons: rename button.
};

#endif /* WIDGETS_BUILD_VEHICLE_WIDGET_H */
