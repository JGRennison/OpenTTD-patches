/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file tbtr_template_gui_main.h Template-based train replacement: main GUI header. */

#ifndef TEMPLATE_GUI_H
#define TEMPLATE_GUI_H

#include "engine_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "string_func.h"
#include "strings_func.h"

#include "tbtr_template_vehicle.h"
#include "tbtr_template_vehicle_func.h"

typedef GUIList<const Group*> GUIGroupList;

void ShowTemplateReplaceWindow();

bool TemplateVehicleClicked(const TemplateVehicle *v);

#endif
