/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dock_gui.h Functions/types related to the dock GUIs. */

#ifndef DOCK_GUI_H
#define DOCK_GUI_H

#include "tile_type.h"

struct Window;

Window *ShowBuildDocksToolbar();
Window *ShowBuildDocksScenToolbar();
void ShowBuildDocksToolbarFromTile(TileIndex tile);

#endif /* DOCK_GUI_H */
