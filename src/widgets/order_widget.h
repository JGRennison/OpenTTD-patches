/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file order_widget.h Types related to the order widgets. */

#ifndef WIDGETS_ORDER_WIDGET_H
#define WIDGETS_ORDER_WIDGET_H

#include "../cargo_type.h"

/** Widgets of the #OrdersWindow class. */
enum OrderWidgets {
	WID_O_CAPTION,                   ///< Caption of the window.
	WID_O_TIMETABLE_VIEW,            ///< Toggle timetable view.
	WID_O_ORDER_LIST,                ///< Order list panel.
	WID_O_SCROLLBAR,                 ///< Order list scrollbar.
	WID_O_SKIP,                      ///< Skip current order.
	WID_O_DELETE,                    ///< Delete selected order.
	WID_O_STOP_SHARING,              ///< Stop sharing orders.
	WID_O_NON_STOP,                  ///< Goto non-stop to destination.
	WID_O_GOTO,                      ///< Goto destination.
	WID_O_FULL_LOAD,                 ///< Select full load.
	WID_O_UNLOAD,                    ///< Select unload.
	WID_O_REFIT,                     ///< Select refit.
	WID_O_SERVICE,                   ///< Select service (at depot).
	WID_O_EMPTY,                     ///< Placeholder for refit dropdown when not owner.
	WID_O_REFIT_DROPDOWN,            ///< Open refit options.
	WID_O_REVERSE,                   ///< Select waypoint reverse type
	WID_O_COND_VARIABLE,             ///< Choose condition variable.
	WID_O_COND_COMPARATOR,           ///< Choose condition type.
	WID_O_COND_VALUE,                ///< Choose condition value.
	WID_O_COND_CARGO,                ///< Choose condition cargo.
	WID_O_COND_AUX_CARGO,            ///< Choose condition cargo.
	WID_O_COND_SLOT,                 ///< Choose condition slot.
	WID_O_COND_AUX_VIA,              ///< Condition via button.
	WID_O_SEL_COND_VALUE,            ///< Widget for conditional value or conditional cargo type.
	WID_O_SEL_COND_AUX,              ///< Widget for auxiliary conditional cargo type.
	WID_O_SEL_COND_AUX2,             ///< Widget for auxiliary conditional via button.
	WID_O_SEL_TOP_LEFT,              ///< #NWID_SELECTION widget for left part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_MIDDLE,            ///< #NWID_SELECTION widget for middle part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_RIGHT,             ///< #NWID_SELECTION widget for right part of the top row of the 'your train' order window.
	WID_O_SEL_TOP_ROW_GROUNDVEHICLE, ///< #NWID_SELECTION widget for the top row of the 'your train' order window.
	WID_O_SEL_TOP_ROW,               ///< #NWID_SELECTION widget for the top row of the 'your non-trains' order window.
	WID_O_SEL_BOTTOM_MIDDLE,         ///< #NWID_SELECTION widget for the middle part of the bottom row of the 'your train' order window.
	WID_O_SEL_SHARED,                ///< #NWID_SELECTION widget for WID_O_SHARED_ORDER_LIST and WID_O_ADD_VEH_GROUP
	WID_O_SHARED_ORDER_LIST,         ///< Open list of shared vehicles.
	WID_O_ADD_VEH_GROUP,             ///< Add single vehicle to new group button.
	WID_O_SEL_OCCUPANCY,             ///< #NWID_SELECTION widget for the occupancy list panel.
	WID_O_OCCUPANCY_LIST,            ///< Occupancy list panel.
	WID_O_OCCUPANCY_TOGGLE,          ///< Toggle display of occupancy measures.
};

/** Widgets of the #CargoTypeOrdersWindow class. */
enum CargoTypeOrdersWidgets {
	WID_CTO_CAPTION,                                                            ///< Caption of the window.
	WID_CTO_HEADER,                                                             ///< Window header.
	WID_CTO_CLOSEBTN,                                                           ///< Close button.
	WID_CTO_SET_TO_ALL_LABEL,                                                   ///< 'Set to all' dropdown label
	WID_CTO_SET_TO_ALL_DROPDOWN,                                                ///< 'Set to all' dropdown
	WID_CTO_CARGO_ROW_FIRST,                                                    ///< First cargo type order row.
	WID_CTO_CARGO_ROW_LAST = WID_CTO_CARGO_ROW_FIRST + NUM_CARGO - 1,           ///< Last cargo type order row.
	WID_CTO_CARGO_LABEL_FIRST,                                                  ///< First cargo label.
	WID_CTO_CARGO_LABEL_LAST = WID_CTO_CARGO_LABEL_FIRST + NUM_CARGO - 1,       ///< Last cargo label.
	WID_CTO_CARGO_DROPDOWN_FIRST,                                               ///< First order dropdown.
	WID_CTO_CARGO_DROPDOWN_LAST = WID_CTO_CARGO_DROPDOWN_FIRST + NUM_CARGO - 1, ///< Last order dropdown.
};

#endif /* WIDGETS_ORDER_WIDGET_H */
