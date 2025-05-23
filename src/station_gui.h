/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_gui.h Contains enums and function declarations connected with stations GUI */

#ifndef STATION_GUI_H
#define STATION_GUI_H

#include "core/geometry_type.hpp"
#include "command_type.h"
#include "tilearea_type.h"
#include "window_type.h"

struct Station;
struct CargoSpec;


/** Types of cargo to display for station coverage. */
enum StationCoverageType : uint8_t {
	SCT_PASSENGERS_ONLY,     ///< Draw only passenger class cargoes.
	SCT_NON_PASSENGERS_ONLY, ///< Draw all non-passenger class cargoes.
	SCT_ALL,                 ///< Draw all cargoes.
};

int DrawStationCoverageAreaText(const Rect &r, StationCoverageType sct, int rad, bool supplies);
void CheckRedrawStationCoverage(Window *w);
void CheckRedrawRailWaypointCoverage(Window *w);
void CheckRedrawRoadWaypointCoverage(Window *w);

using StationPickerCmdProc = std::function<bool(bool test, StationID to_join)>;

void ShowSelectStationIfNeeded(TileArea ta, StationPickerCmdProc proc);
void ShowSelectRailWaypointIfNeeded(TileArea ta, StationPickerCmdProc proc);
void ShowSelectRoadWaypointIfNeeded(TileArea ta, StationPickerCmdProc proc);

void GuiShowStationRatingTooltip(Window *parent, const Station *st, const CargoSpec *cs);

#endif /* STATION_GUI_H */
