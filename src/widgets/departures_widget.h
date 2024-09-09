/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures_widget.h Types related to the departures widgets. */

#ifndef WIDGETS_DEPARTURES_WIDGET_H
#define WIDGETS_DEPARTURES_WIDGET_H

/** Widgets of the WC_DEPARTURES_BOARD. */
enum DeparturesWindowWidgets {
	WID_DB_CAPTION,             ///< Window caption
	WID_DB_LIST,                ///< List of departures
	WID_DB_SCROLLBAR,           ///< List scrollbar
	WID_DB_CARGO_MODE,          ///< Cargo filter mode
	WID_DB_DEPARTURE_MODE,      ///< Departure type mode
	WID_DB_SOURCE_MODE,         ///< Departure source mode
	WID_DB_SHOW_TIMES,          ///< Toggle show times button
	WID_DB_SHOW_EMPTY,          ///< Toggle show empty button
	WID_DB_SHOW_VIA,            ///< Toggle via button
	WID_DB_SHOW_TRAINS,         ///< Toggle trains button
	WID_DB_SHOW_ROADVEHS,       ///< Toggle road vehicles button
	WID_DB_SHOW_SHIPS,          ///< Toggle ships button
	WID_DB_SHOW_PLANES,         ///< Toggle planes button
};

#endif /* WIDGETS_DEPARTURES_WIDGET_H */
