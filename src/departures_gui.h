/* $Id: departures_gui.h $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file departures_gui.h */

#ifndef DEPARTURES_GUI_H
#define DEPARTURES_GUI_H

#include "departures_type.h"
#include "station_base.h"
#include "widgets/departures_widget.h"

void ShowStationDepartures(StationID station);
void ShowWaypointDepartures(StationID waypoint);

#endif /* DEPARTURES_GUI_H */
