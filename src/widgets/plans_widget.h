/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file plans_widget.h Types related to the plans widgets. */

#ifndef WIDGETS_PLANS_WIDGET_H
#define WIDGETS_PLANS_WIDGET_H

/** Widgets of the #PlansWindow class. */
enum PlansWidgets : WidgetID {
	WID_PLN_CAPTION,        ///< Caption of the window.
	WID_PLN_SORT_ORDER,     ///< Direction of sort dropdown.
	WID_PLN_SORT_CRITERIA,  ///< Criteria of sort dropdown.
	WID_PLN_OWN_ONLY,       ///< Only show own plans.
	WID_PLN_FILTER,         ///< Filter of name.
	WID_PLN_LIST,
	WID_PLN_SCROLLBAR,
	WID_PLN_NEW,
	WID_PLN_ADD_LINES,
	WID_PLN_VISIBILITY,
	WID_PLN_COLOUR,
	WID_PLN_HIDE_ALL,
	WID_PLN_SHOW_ALL,
	WID_PLN_DELETE,
	WID_PLN_HIDE_ALL_SEL,
	WID_PLN_RENAME,
	WID_PLN_RENAME_SEL,
	WID_PLN_TAKE_OWNERSHIP,
};

#endif /* WIDGETS_PLANS_WIDGET_H */
