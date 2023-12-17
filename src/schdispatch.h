/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file schdispatch.h Functions related to scheduled dispatch. */

#ifndef SCHDISPATCH_H
#define SCHDISPATCH_H

#include "date_func.h"
#include "vehicle_type.h"
#include "settings_type.h"

void ShowSchdispatchWindow(const Vehicle *v);
void SchdispatchInvalidateWindows(const Vehicle *v);

#endif /* SCHDISPATCH_H */
