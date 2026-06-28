/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file map_extend.h Declaration of the map-extend operation (the 'extend_map' console command). */

#ifndef MAP_EXTEND_H
#define MAP_EXTEND_H

#include "map_func.h"

bool ExtendMap(uint new_w, uint new_h, uint dx, uint dy);

#endif /* MAP_EXTEND_H */
